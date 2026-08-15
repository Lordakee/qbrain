#include "qbrain/codeintel/scan.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/util/paths.hpp"
#include "wave3_test_support.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#define QB_CHECK(cond)                                                               \
  do {                                                                               \
    if (!(cond)) throw std::runtime_error(std::string("CHECK failed: ") + #cond); \
  } while (0)

namespace {

using qbrain::test_support::logical_snapshot;
using qbrain::test_support::snapshot_sha256;

class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(std::string name, const std::string& value)
      : name_(std::move(name)) {
    if (const char* previous = std::getenv(name_.c_str())) previous_ = previous;
    if (_putenv_s(name_.c_str(), value.c_str()) != 0) {
      throw std::runtime_error("failed to set test environment variable");
    }
  }

  ~ScopedEnvironmentVariable() {
    _putenv_s(name_.c_str(), previous_ ? previous_->c_str() : "");
  }

  ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
  ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

 private:
  std::string name_;
  std::optional<std::string> previous_;
};

struct SnapshotCallEvidence {
  size_t index = 0;
  std::string selected_before;
  std::string selected_after;
  std::string decoy_before;
  std::string decoy_after;
};

std::vector<SnapshotCallEvidence> g_snapshot_calls;

template <typename Callable>
auto call_without_mutation(qbrain::Brain& selected, qbrain::Brain& decoy,
                           Callable&& callable) {
  const auto selected_before = logical_snapshot(selected);
  const auto decoy_before = logical_snapshot(decoy);
  const auto selected_before_hash = snapshot_sha256(selected_before);
  const auto decoy_before_hash = snapshot_sha256(decoy_before);
  auto result = callable();
  const auto selected_after = logical_snapshot(selected);
  const auto decoy_after = logical_snapshot(decoy);
  const auto selected_after_hash = snapshot_sha256(selected_after);
  const auto decoy_after_hash = snapshot_sha256(decoy_after);
  QB_CHECK(selected_before == selected_after);
  QB_CHECK(decoy_before == decoy_after);
  QB_CHECK(selected_before_hash == selected_after_hash);
  QB_CHECK(decoy_before_hash == decoy_after_hash);
  g_snapshot_calls.push_back({g_snapshot_calls.size() + 1, selected_before_hash,
                              selected_after_hash, decoy_before_hash, decoy_after_hash});
  return result;
}

nlohmann::json operation_error(const qbrain::ops::OpResult& result,
                               const std::string& code,
                               const std::string& field) {
  QB_CHECK(!result.ok);
  QB_CHECK(result.exit_code != 0);
  const auto error = nlohmann::json::parse(result.json)["error"];
  QB_CHECK(error["code"] == code);
  QB_CHECK(error["field"] == field);
  return error;
}

nlohmann::json mcp_call(qbrain::Brain& brain, const qbrain::mcp::ServeOptions& options,
                        int id, const std::string& name,
                        const nlohmann::json& arguments) {
  const nlohmann::json request = {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"method", "tools/call"},
      {"params", {{"name", name}, {"arguments", arguments}}}};
  return nlohmann::json::parse(
      qbrain::mcp::handle_rpc_body(brain, options, request.dump()));
}

nlohmann::json mcp_success_rows(const nlohmann::json& response) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(!response["result"].value("isError", true));
  for (auto it = response["result"]["content"].rbegin();
       it != response["result"]["content"].rend(); ++it) {
    if (it->value("type", "") != "text" || !it->contains("text") ||
        !(*it)["text"].is_string())
      continue;
    try {
      auto parsed = nlohmann::json::parse((*it)["text"].get<std::string>());
      if (parsed.is_array()) return parsed;
    } catch (const nlohmann::json::parse_error&) {
    }
  }
  throw std::runtime_error("MCP success response did not contain JSON rows");
}

nlohmann::json mcp_error(const nlohmann::json& response) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(response["result"].value("isError", false));
  for (const auto& content : response["result"]["content"]) {
    if (content.value("type", "") != "text" || !content.contains("text") ||
        !content["text"].is_string())
      continue;
    try {
      auto parsed = nlohmann::json::parse(content["text"].get<std::string>());
      if (parsed.contains("error")) return parsed["error"];
    } catch (const nlohmann::json::parse_error&) {
    }
  }
  throw std::runtime_error("MCP response did not contain a structured error");
}

bool contains_slug(const nlohmann::json& rows, const std::string& slug) {
  return std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
    return row.value("slug", "") == slug;
  });
}

void require_hit_shape(const nlohmann::json& rows, const std::string& source,
                       const std::string& kind) {
  for (const auto& row : rows) {
    QB_CHECK(row.is_object() && row.size() == 5);
    QB_CHECK(row.contains("source_id") && row.contains("slug") && row.contains("line") &&
             row.contains("snippet") && row.contains("kind"));
    QB_CHECK(row["source_id"] == source);
    QB_CHECK(row["kind"] == kind);
    QB_CHECK(row["line"].is_number_integer() && row["line"].get<int>() >= 1);
    QB_CHECK(row["snippet"].is_string() &&
             row["snippet"].get<std::string>().size() <= 200);
    QB_CHECK(!nlohmann::json({{"snippet", row["snippet"]}}).dump().empty());
  }
}

void set_updated_at(qbrain::Brain& brain, int64_t page_id, const std::string& timestamp) {
  auto statement = brain.db().prepare("UPDATE pages SET updated_at=? WHERE id=?");
  statement.bind_text(1, timestamp);
  statement.bind_int(2, page_id);
  statement.step_done();
}

}  // namespace

void test_n16() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "qbrain_n16_test";
  fs::remove_all(root);
  fs::create_directories(root);
  ScopedEnvironmentVariable local_app_data(
      "LOCALAPPDATA", qbrain::util::path_to_utf8(root / "localappdata"));

  g_snapshot_calls.clear();
  qbrain::ops::register_builtin_ops();
  qbrain::Brain brain("n16-primary");
  brain.open_at(qbrain::util::path_to_utf8(root / "primary.db"));
  QB_CHECK(brain.ensure_source("team_a"));
  QB_CHECK(brain.ensure_source("team_b"));

  qbrain::Brain decoy_brain("n16-decoy");
  decoy_brain.open_at(qbrain::util::path_to_utf8(root / "decoy.db"));
  QB_CHECK(decoy_brain.ensure_source("team_a"));
  qbrain::test_support::put_page(decoy_brain, "team_a", "n16/defs",
                                 "void foo() { DECOY_BRAIN_SENTINEL; }");

  const std::string definition_body =
      "class DefMatrix {};\n"
      "struct DefMatrix {};\n"
      "interface DefMatrix {}\n"
      "enum DefMatrix {}\n"
      "type DefMatrix = {};\n"
      "namespace DefMatrix {\n"
      "function DefMatrix() {}\n"
      "async function DefMatrix() {}\n"
      "def DefMatrix():\n"
      "const DefMatrix = 1;\n"
      "let DefMatrix: number;\n"
      "var DefMatrix = 2;\n"
      "Result DefMatrix() {}\n"
      "DefMatrix() {}\n"
      "DefMatrix(value): Result {}\n";
  qbrain::test_support::put_page(brain, "team_a", "n16/definitions", definition_body);

  const auto definitions = call_without_mutation(brain, decoy_brain, [&] {
    return qbrain::test_support::call_op(
        brain, "code_def", {{"source_id", "Team_A"}, {"symbol", "DefMatrix"},
                            {"limit", "200"}, {"page_limit", "500"}});
  });
  QB_CHECK(definitions.ok);
  const auto definition_rows = nlohmann::json::parse(definitions.json);
  QB_CHECK(definition_rows.size() == 15);
  require_hit_shape(definition_rows, "team_a", "def");
  for (size_t index = 0; index < definition_rows.size(); ++index) {
    QB_CHECK(definition_rows[index]["line"] == static_cast<int>(index + 1));
  }

  qbrain::test_support::put_page(
      brain, "team_a", "n16/references",
      "void foo() {}\nfoo();\nfoo ( );\nauto callback = foo;\nfood();\n");
  const auto references = call_without_mutation(brain, decoy_brain, [&] {
    return qbrain::test_support::call_op(
        brain, "code_refs", {{"source_id", "team_a"}, {"name", "foo"}, {"limit", "200"}});
  });
  const auto reference_rows = nlohmann::json::parse(references.json);
  require_hit_shape(reference_rows, "team_a", "ref");
  QB_CHECK(reference_rows.size() == 4);
  QB_CHECK(references.json.find("food") == std::string::npos);
  const auto callers = call_without_mutation(brain, decoy_brain, [&] {
    return qbrain::test_support::call_op(
        brain, "code_callers", {{"source_id", "team_a"}, {"symbol", "foo"},
                                {"limit", "200"}});
  });
  const auto caller_rows = nlohmann::json::parse(callers.json);
  require_hit_shape(caller_rows, "team_a", "call");
  QB_CHECK(caller_rows.size() == 3);
  QB_CHECK(std::none_of(caller_rows.begin(), caller_rows.end(), [](const auto& row) {
    return row.value("line", 0) == 4 || row.value("line", 0) == 5;
  }));

  QB_CHECK(qbrain::codeintel::is_valid_symbol("foo"));
  QB_CHECK(qbrain::codeintel::is_valid_symbol("$value"));
  QB_CHECK(qbrain::codeintel::is_valid_symbol("_value"));
  QB_CHECK(qbrain::codeintel::is_valid_symbol("~Widget"));
  QB_CHECK(qbrain::codeintel::is_valid_symbol("ns::member"));
  QB_CHECK(qbrain::codeintel::is_valid_symbol("ns::~Widget"));

  const std::vector<std::unordered_map<std::string, std::string>> invalid_symbols = {
      {},
      {{"symbol", "foo"}, {"name", "bar"}},
      {{"symbol", ""}},
      {{"symbol", "foo bar"}},
      {{"symbol", "foo\tbar"}},
      {{"symbol", "foo\nbar"}},
      {{"symbol", "foo.bar"}},
      {{"symbol", "foo[0]"}},
      {{"symbol", "foo*"}},
      {{"symbol", ":"}},
      {{"symbol", "foo:::bar"}},
      {{"symbol", "foo::"}},
      {{"symbol", "1foo"}},
      {{"symbol", std::string(257, 'a')}}};
  for (const auto& arguments : invalid_symbols) {
    const auto result = call_without_mutation(brain, decoy_brain, [&] {
      return qbrain::test_support::call_op(brain, "code_def", arguments);
    });
    const auto error = operation_error(result, "invalid_argument", "symbol");
    QB_CHECK(error.value("message", "").find("foo bar") == std::string::npos);
  }
  const auto no_match = call_without_mutation(brain, decoy_brain, [&] {
    return qbrain::test_support::call_op(
        brain, "code_refs", {{"source_id", "team_a"}, {"symbol", "NoSuchSymbol"}});
  });
  QB_CHECK(no_match.ok && nlohmann::json::parse(no_match.json).empty());

  std::string split_utf8_line = "const UtfBoundary = \"";
  split_utf8_line.append(178, 'a');
  split_utf8_line += "\xE2\x82\xAC\";";
  std::string invalid_utf8_line = "const InvalidUtf = \"";
  invalid_utf8_line.append(170, 'b');
  invalid_utf8_line.push_back(static_cast<char>(0xff));
  invalid_utf8_line += "tail\";";
  qbrain::test_support::put_page(
      brain, "team_a", "n16/utf8",
      split_utf8_line + "\n" + invalid_utf8_line +
          "\nconst EscapeSymbol = \"quote\\\\path\tvalue\";\n");
  for (const auto& symbol : {"UtfBoundary", "InvalidUtf", "EscapeSymbol"}) {
    const auto result = call_without_mutation(brain, decoy_brain, [&] {
      return qbrain::test_support::call_op(
          brain, "code_def", {{"source_id", "team_a"}, {"symbol", symbol}});
    });
    QB_CHECK(result.ok);
    const auto rows = nlohmann::json::parse(result.json);
    QB_CHECK(rows.size() == 1);
    require_hit_shape(rows, "team_a", "def");
  }

  auto order_old = qbrain::test_support::put_page(
      brain, "team_a", "n16/order-old", "OrderSymbol;\nOrderSymbol;\n");
  auto order_new = qbrain::test_support::put_page(
      brain, "team_a", "n16/order-new", "OrderSymbol;\nOrderSymbol;\n");
  set_updated_at(brain, order_old.id, "2040-01-01 00:00:00");
  set_updated_at(brain, order_new.id, "2040-01-01 00:00:00");
  auto deleted = qbrain::test_support::put_page(
      brain, "team_a", "n16/deleted", "OrderSymbol; DELETED_SENTINEL");
  set_updated_at(brain, deleted.id, "2099-01-01 00:00:00");
  QB_CHECK(brain.soft_delete(deleted.slug, "team_a"));
  auto other_source = qbrain::test_support::put_page(
      brain, "default", "n16/order-decoy", "OrderSymbol; DEFAULT_SENTINEL");
  set_updated_at(brain, other_source.id, "2099-01-02 00:00:00");
  const auto ordered = call_without_mutation(brain, decoy_brain, [&] {
    return qbrain::test_support::call_op(
        brain, "code_refs", {{"source_id", "team_a"}, {"symbol", "OrderSymbol"},
                             {"page_limit", "2"}, {"limit", "20"}});
  });
  const auto ordered_rows = nlohmann::json::parse(ordered.json);
  QB_CHECK(ordered_rows.size() == 4);
  QB_CHECK(ordered_rows[0]["slug"] == "n16/order-new" && ordered_rows[0]["line"] == 1);
  QB_CHECK(ordered_rows[1]["slug"] == "n16/order-new" && ordered_rows[1]["line"] == 2);
  QB_CHECK(ordered_rows[2]["slug"] == "n16/order-old" && ordered_rows[2]["line"] == 1);
  QB_CHECK(ordered_rows[3]["slug"] == "n16/order-old" && ordered_rows[3]["line"] == 2);
  QB_CHECK(ordered.json.find("DELETED_SENTINEL") == std::string::npos);
  QB_CHECK(ordered.json.find("DEFAULT_SENTINEL") == std::string::npos);
  const auto ordered_repeat = call_without_mutation(brain, decoy_brain, [&] {
    return qbrain::test_support::call_op(
        brain, "code_refs", {{"source_id", "team_a"}, {"symbol", "OrderSymbol"},
                             {"page_limit", "2"}, {"limit", "20"}});
  });
  QB_CHECK(ordered_repeat.json == ordered.json);

  const std::vector<std::string> invalid_numbers = {
      "", "+1", "-1", " 1", "1 ", "1 0", "1.0", "1junk",
      "18446744073709551616"};
  for (const auto& field : {"limit", "page_limit"}) {
    for (const auto& value : invalid_numbers) {
      const auto result = call_without_mutation(brain, decoy_brain, [&] {
        return qbrain::test_support::call_op(
            brain, "code_refs", {{"source_id", "team_a"}, {"symbol", "foo"},
                                 {field, value}});
      });
      operation_error(result, "invalid_argument", field);
    }
  }

  const std::vector<std::pair<std::string, std::string>> invalid_sources = {
      {"", "invalid_source"},
      {"bad/slash", "invalid_source"},
      {"CON", "invalid_source"},
      {"NUL", "invalid_source"},
      {std::string(65, 'a'), "invalid_source"},
      {"valid_but_unknown", "source_not_found"}};
  for (const auto& [source, code] : invalid_sources) {
    const auto result = call_without_mutation(brain, decoy_brain, [&] {
      return qbrain::test_support::call_op(
          brain, "code_refs", {{"source_id", source}, {"symbol", "foo"}});
    });
    operation_error(result, code, "source_id");
  }
  QB_CHECK(!brain.source_exists("valid_but_unknown"));

  const std::array<std::string, 3> operation_names = {
      "code_def", "code_refs", "code_callers"};
  for (const auto& name : operation_names) {
    auto* operation = qbrain::ops::global_registry().find(name);
    QB_CHECK(operation && operation->scope == qbrain::ops::Scope::Read &&
             !operation->local_only);
    const auto schema = nlohmann::json::parse(operation->input_schema_json);
    QB_CHECK(schema["type"] == "object" && schema["additionalProperties"] == false);
    QB_CHECK(schema["properties"].size() == 5);
    QB_CHECK(schema["properties"]["source_id"]["default"] == "default");
    QB_CHECK(schema["properties"]["limit"]["default"] == 50);
    QB_CHECK(schema["properties"]["limit"]["minimum"] == 0);
    QB_CHECK(schema["properties"]["limit"]["maximum"] == 200);
    QB_CHECK(schema["properties"]["page_limit"]["default"] == 500);
    QB_CHECK(schema["properties"]["page_limit"]["minimum"] == 0);
    QB_CHECK(schema["properties"]["page_limit"]["maximum"] == 2000);
    QB_CHECK(schema.contains("anyOf"));
  }

  qbrain::mcp::ServeOptions mcp_options;
  const auto tools_list = nlohmann::json::parse(qbrain::mcp::handle_rpc_body(
      brain, mcp_options,
      R"({"jsonrpc":"2.0","id":90,"method":"tools/list","params":{}})"));
  for (const auto& name : operation_names) {
    const auto matching = std::count_if(
        tools_list["result"]["tools"].begin(), tools_list["result"]["tools"].end(),
        [&](const auto& tool) { return tool.value("name", "") == name; });
    QB_CHECK(matching == 1);
    const auto* operation = qbrain::ops::global_registry().find(name);
    const auto tool = std::find_if(
        tools_list["result"]["tools"].begin(), tools_list["result"]["tools"].end(),
        [&](const auto& value) { return value.value("name", "") == name; });
    QB_CHECK(tool != tools_list["result"]["tools"].end());
    QB_CHECK((*tool)["inputSchema"] == nlohmann::json::parse(operation->input_schema_json));
  }

  const auto remote_default = call_without_mutation(brain, decoy_brain, [&] {
    return mcp_call(brain, mcp_options, 91, "code_refs",
                    {{"source_id", "default"}, {"symbol", "OrderSymbol"}});
  });
  QB_CHECK(!remote_default["result"]["isError"]);
  const auto remote_denied = call_without_mutation(brain, decoy_brain, [&] {
    return mcp_call(brain, mcp_options, 92, "code_refs",
                    {{"source_id", "team_a"}, {"symbol", "foo"}});
  });
  QB_CHECK(mcp_error(remote_denied)["code"] == "source_not_allowed");

  brain.save_config_value("mcp.allowed_sources", "TEAM_A");
  for (size_t index = 0; index < operation_names.size(); ++index) {
    const auto response = call_without_mutation(brain, decoy_brain, [&] {
      return mcp_call(brain, mcp_options, 100 + static_cast<int>(index),
                      operation_names[index],
                      {{"source_id", "Team_A"}, {"symbol", "foo"}});
    });
    const auto rows = mcp_success_rows(response);
    QB_CHECK(std::all_of(rows.begin(), rows.end(), [](const auto& row) {
      return row.value("source_id", "") == "team_a" &&
             row.value("snippet", "").find("DECOY_BRAIN_SENTINEL") == std::string::npos;
    }));
  }
  const auto remote_other = call_without_mutation(brain, decoy_brain, [&] {
    return qbrain::test_support::call_op(
        brain, "code_refs", {{"source_id", "team_b"}, {"symbol", "foo"}}, true, true);
  });
  operation_error(remote_other, "source_not_allowed", "source_id");

  qbrain::test_support::put_page(brain, "default", "n16/ambient-default",
                                 "void AmbientProbe() { DEFAULT_AMBIENT; }");
  qbrain::test_support::put_page(brain, "team_a", "n16/ambient-team",
                                 "void AmbientProbe() { TEAM_AMBIENT; }");
  {
    ScopedEnvironmentVariable ambient_source("QBRAIN_SOURCE", "team_a");
    const auto response = call_without_mutation(brain, decoy_brain, [&] {
      return mcp_call(brain, mcp_options, 110, "code_def", {{"symbol", "AmbientProbe"}});
    });
    const auto rows = mcp_success_rows(response);
    QB_CHECK(!rows.empty());
    QB_CHECK(std::all_of(rows.begin(), rows.end(), [](const auto& row) {
      return row.value("source_id", "") == "default" &&
             row.value("snippet", "").find("TEAM_AMBIENT") == std::string::npos;
    }));
  }

  const std::vector<std::pair<nlohmann::json, std::string>> malformed_mcp = {
      {nlohmann::json::array(), "arguments"},
      {nlohmann::json({{"symbol", "foo"}, {"limit", true}}), "limit"},
      {nlohmann::json({{"symbol", "foo"}, {"limit", 1.5}}), "limit"},
      {nlohmann::json({{"symbol", "foo"}, {"limit", -1}}), "limit"},
      {nlohmann::json({{"symbol", "foo"}, {"source_id", nullptr}}), "source_id"},
      {nlohmann::json({{"symbol", "foo"}, {"unexpected", "x"}}), "unexpected"}};
  int malformed_id = 120;
  for (const auto& [arguments, field] : malformed_mcp) {
    const auto response = call_without_mutation(brain, decoy_brain, [&] {
      return mcp_call(brain, mcp_options, malformed_id++, "code_refs", arguments);
    });
    const auto error = mcp_error(response);
    QB_CHECK(error["code"] == "invalid_argument" && error["field"] == field);
  }

  std::string many_hits_body;
  for (int index = 0; index < 205; ++index) many_hits_body += "ManyHits;\n";
  qbrain::test_support::put_page(brain, "team_a", "n16/many-hits", many_hits_body);
  const auto limit_snapshot = logical_snapshot(brain);
  const auto decoy_limit_snapshot = logical_snapshot(decoy_brain);
  const auto omitted_limit = qbrain::test_support::call_op(
      brain, "code_refs", {{"source_id", "team_a"}, {"symbol", "ManyHits"}});
  const auto zero_limit = qbrain::test_support::call_op(
      brain, "code_refs", {{"source_id", "team_a"}, {"symbol", "ManyHits"},
                           {"limit", "0"}});
  const auto one_limit = qbrain::test_support::call_op(
      brain, "code_refs", {{"source_id", "team_a"}, {"symbol", "ManyHits"},
                           {"limit", "1"}});
  const auto maximum_limit = qbrain::test_support::call_op(
      brain, "code_refs", {{"source_id", "team_a"}, {"symbol", "ManyHits"},
                           {"limit", "200"}});
  const auto clamped_limit = qbrain::test_support::call_op(
      brain, "code_refs", {{"source_id", "team_a"}, {"symbol", "ManyHits"},
                           {"limit", "201"}});
  QB_CHECK(nlohmann::json::parse(omitted_limit.json).size() == 50);
  QB_CHECK(nlohmann::json::parse(zero_limit.json).size() == 1);
  QB_CHECK(nlohmann::json::parse(one_limit.json).size() == 1);
  QB_CHECK(nlohmann::json::parse(maximum_limit.json).size() == 200);
  QB_CHECK(nlohmann::json::parse(clamped_limit.json).size() == 200);
  QB_CHECK(logical_snapshot(brain) == limit_snapshot);
  QB_CHECK(logical_snapshot(decoy_brain) == decoy_limit_snapshot);

  QB_CHECK(brain.ensure_source("page_default"));
  qbrain::test_support::put_page(brain, "page_default", "target", "PageDefaultTarget;");
  brain.db().exec("BEGIN;");
  for (int index = 0; index < 500; ++index) {
    qbrain::test_support::put_page(brain, "page_default",
                                   "filler-" + std::to_string(index), "no match");
  }
  brain.db().exec("COMMIT;");
  const auto default_page_limit = qbrain::test_support::call_op(
      brain, "code_refs", {{"source_id", "page_default"},
                           {"symbol", "PageDefaultTarget"}});
  const auto expanded_page_limit = qbrain::test_support::call_op(
      brain, "code_refs", {{"source_id", "page_default"},
                           {"symbol", "PageDefaultTarget"}, {"page_limit", "501"}});
  QB_CHECK(nlohmann::json::parse(default_page_limit.json).empty());
  QB_CHECK(nlohmann::json::parse(expanded_page_limit.json).size() == 1);

  QB_CHECK(brain.ensure_source("page_cap"));
  qbrain::test_support::put_page(brain, "page_cap", "target", "PageCapTarget;");
  brain.db().exec("BEGIN;");
  for (int index = 0; index < 2000; ++index) {
    qbrain::test_support::put_page(brain, "page_cap",
                                   "filler-" + std::to_string(index), "no match");
  }
  brain.db().exec("COMMIT;");
  const auto cap_snapshot = logical_snapshot(brain);
  for (const auto& page_limit : {"2000", "2001", "999999"}) {
    const auto result = qbrain::test_support::call_op(
        brain, "code_refs", {{"source_id", "page_cap"}, {"symbol", "PageCapTarget"},
                             {"page_limit", page_limit}});
    QB_CHECK(result.ok && nlohmann::json::parse(result.json).empty());
  }
  QB_CHECK(logical_snapshot(brain) == cap_snapshot);

  const auto final_selected_snapshot = logical_snapshot(brain);
  const auto final_decoy_snapshot = logical_snapshot(decoy_brain);
  std::cout << "[INFO] n16 snapshot_schema=pass snapshot_matrix=pass source_scope=pass "
            << "definition_matrix=pass reference_caller_matrix=pass symbol_grammar=pass "
            << "limit_matrix=pass page_limit_matrix=pass ordering_matrix=pass "
            << "deterministic=pass utf8_bounds=pass registry_schema=pass "
            << "mcp_type_validation=pass ambient_default=pass remote_authorization=pass "
            << "mcp_rpc=pass selected_snapshot_sha256="
            << snapshot_sha256(final_selected_snapshot) << " decoy_snapshot_sha256="
            << snapshot_sha256(final_decoy_snapshot) << " snapshot_call_count="
            << g_snapshot_calls.size() << " read_only=pass\n";
  for (const auto& evidence : g_snapshot_calls) {
    std::cout << "[INFO] n16 snapshot_call=" << evidence.index
              << " selected_before_sha256=" << evidence.selected_before
              << " selected_after_sha256=" << evidence.selected_after
              << " decoy_before_sha256=" << evidence.decoy_before
              << " decoy_after_sha256=" << evidence.decoy_after << '\n';
  }

  decoy_brain.close();
  brain.close();
  fs::remove_all(root);
}
