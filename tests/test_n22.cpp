#include "qbrain/codeintel/scan.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/storage/database.hpp"
#include "qbrain/util/paths.hpp"
#include "wave3_test_support.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#define QB_CHECK(cond)                                                               \
  do {                                                                               \
    if (!(cond))                                                                     \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond + " @ " +      \
                               __FILE__ + ":" + std::to_string(__LINE__));            \
  } while (0)

namespace {

using json = nlohmann::json;
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

struct SnapshotEvidence {
  size_t index = 0;
  std::string label;
  std::string selected_before;
  std::string selected_after;
  std::string decoy_before;
  std::string decoy_after;
};

std::vector<SnapshotEvidence> g_snapshot_evidence;

class SnapshotMatrix {
 public:
  SnapshotMatrix(qbrain::Brain& selected, qbrain::Brain& decoy)
      : selected_(selected), decoy_(decoy) {}

  template <typename Callable>
  auto run(const std::string& label, Callable&& callable) {
    const auto selected_before = logical_snapshot(selected_);
    const auto decoy_before = logical_snapshot(decoy_);
    const auto selected_before_hash = snapshot_sha256(selected_before);
    const auto decoy_before_hash = snapshot_sha256(decoy_before);
    auto result = callable();
    const auto selected_after = logical_snapshot(selected_);
    const auto decoy_after = logical_snapshot(decoy_);
    const auto selected_after_hash = snapshot_sha256(selected_after);
    const auto decoy_after_hash = snapshot_sha256(decoy_after);
    if (selected_before != selected_after || decoy_before != decoy_after) {
      throw std::runtime_error("N22 call mutated database state: " + label);
    }
    QB_CHECK(selected_before_hash == selected_after_hash);
    QB_CHECK(decoy_before_hash == decoy_after_hash);
    g_snapshot_evidence.push_back(
        {g_snapshot_evidence.size() + 1, label, selected_before_hash,
         selected_after_hash, decoy_before_hash, decoy_after_hash});
    return result;
  }

 private:
  qbrain::Brain& selected_;
  qbrain::Brain& decoy_;
};

class DataReadObserver {
 public:
  explicit DataReadObserver(qbrain::Brain& brain) : database_(brain.db().handle()) {
    QB_CHECK(sqlite3_set_authorizer(database_, &DataReadObserver::authorize, this) ==
             SQLITE_OK);
  }

  ~DataReadObserver() { sqlite3_set_authorizer(database_, nullptr, nullptr); }

  int page_reads() const { return page_reads_; }

 private:
  static int authorize(void* context, int action, const char* table, const char*,
                       const char*, const char*) {
    if (action != SQLITE_READ || !table) return SQLITE_OK;
    const std::string_view table_name(table);
    if (table_name == "pages" || table_name == "content_chunks" ||
        table_name == "links" || table_name.starts_with("pages_fts")) {
      ++static_cast<DataReadObserver*>(context)->page_reads_;
    }
    return SQLITE_OK;
  }

  sqlite3* database_ = nullptr;
  int page_reads_ = 0;
};

void require_keys(const json& object,
                  std::initializer_list<std::string_view> expected) {
  QB_CHECK(object.is_object());
  std::set<std::string> actual;
  for (auto it = object.begin(); it != object.end(); ++it) actual.insert(it.key());
  std::set<std::string> wanted;
  for (const auto key : expected) wanted.emplace(key);
  QB_CHECK(actual == wanted);
}

json require_operation_error(const qbrain::ops::OpResult& result,
                             const std::string& code, const std::string& field,
                             const std::string& forbidden = {}) {
  QB_CHECK(!result.ok);
  QB_CHECK(result.exit_code != 0);
  QB_CHECK(!result.json.empty() && result.json.size() <= 2048);
  const auto payload = json::parse(result.json);
  require_keys(payload, {"error"});
  require_keys(payload["error"], {"code", "field", "message"});
  QB_CHECK(payload["error"]["code"] == code);
  QB_CHECK(payload["error"]["field"] == field);
  if (!forbidden.empty()) {
    QB_CHECK(result.json.find(forbidden) == std::string::npos);
    QB_CHECK(result.text.find(forbidden) == std::string::npos);
  }
  return payload["error"];
}

json mcp_call(qbrain::Brain& brain, const qbrain::mcp::ServeOptions& options,
              const std::string& operation, const json& arguments, int id) {
  const json request = {{"jsonrpc", "2.0"},
                        {"id", id},
                        {"method", "tools/call"},
                        {"params", {{"name", operation}, {"arguments", arguments}}}};
  return json::parse(
      qbrain::mcp::handle_rpc_body(brain, options, request.dump()));
}

json structured_mcp_content(const json& response) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(response["result"].contains("content"));
  const auto& content = response["result"]["content"];
  for (auto it = content.rbegin(); it != content.rend(); ++it) {
    if (!it->is_object() || it->value("type", "") != "text" ||
        !it->contains("text") || !(*it)["text"].is_string())
      continue;
    try {
      return json::parse((*it)["text"].get<std::string>());
    } catch (const json::parse_error&) {
    }
  }
  throw std::runtime_error("MCP response did not contain structured JSON");
}

json require_mcp_error(const json& response, const std::string& code,
                       const std::string& field) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(response["result"].value("isError", false));
  QB_CHECK(response["result"].contains("content"));
  const auto& content = response["result"]["content"];
  QB_CHECK(content.is_array() && content.size() == 1);
  QB_CHECK(content[0].is_object() && content[0].value("type", "") == "text");
  QB_CHECK(content[0].contains("text") && content[0]["text"].is_string());
  const auto payload = json::parse(content[0]["text"].get<std::string>());
  require_keys(payload, {"error"});
  require_keys(payload["error"], {"code", "field", "message"});
  QB_CHECK(payload["error"]["code"] == code);
  QB_CHECK(payload["error"]["field"] == field);
  return payload["error"];
}

json require_mcp_rows(const json& response) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(!response["result"].value("isError", true));
  const auto rows = structured_mcp_content(response);
  QB_CHECK(rows.is_array());
  return rows;
}

json hits_to_json(const std::vector<qbrain::codeintel::Hit>& hits) {
  json rows = json::array();
  for (const auto& hit : hits) {
    rows.push_back({{"source_id", hit.source_id},
                    {"slug", hit.slug},
                    {"line", hit.line},
                    {"snippet", hit.snippet},
                    {"kind", hit.kind}});
  }
  return rows;
}

void require_hit_shape(const json& rows, const std::string& source_id) {
  QB_CHECK(rows.is_array());
  for (const auto& row : rows) {
    require_keys(row, {"source_id", "slug", "line", "snippet", "kind"});
    QB_CHECK(row["source_id"] == source_id);
    QB_CHECK(row["slug"].is_string() && !row["slug"].get<std::string>().empty());
    QB_CHECK(row["line"].is_number_integer() && row["line"].get<int>() >= 1);
    QB_CHECK(row["snippet"].is_string());
    QB_CHECK(row["snippet"].get<std::string>().size() <= 200);
    QB_CHECK(row["kind"].is_string() && !row["kind"].get<std::string>().empty());
    QB_CHECK(!json(row["snippet"]).dump().empty());
  }
}

void require_no_disclosure(const qbrain::ops::OpResult& result) {
  static constexpr std::array<std::string_view, 15> forbidden = {
      "DECOY_BRAIN_SENTINEL", "OTHER_SOURCE_SENTINEL", "DELETED_SENTINEL",
      "AFTER_CLOSE_SENTINEL", "LATER_LEAK_SENTINEL", "UNAUTHORIZED_SENTINEL",
      "SECRET_CONFIG_VALUE", "provider-secret", "PREFIX_BRACE_LEAK",
      "DECOY_FLOW_SENTINEL", "DECOY_BLAST_SENTINEL", "OTHER_SOURCE_FLOW_SENTINEL",
      "OTHER_SOURCE_BLAST_SENTINEL", "DELETED_FLOW_SENTINEL",
      "DELETED_BLAST_SENTINEL"};
  for (const auto marker : forbidden) {
    QB_CHECK(result.json.find(marker) == std::string::npos);
    QB_CHECK(result.text.find(marker) == std::string::npos);
  }
}

void set_updated_at(qbrain::Brain& brain, int64_t page_id,
                    const std::string& timestamp) {
  auto statement = brain.db().prepare("UPDATE pages SET updated_at=? WHERE id=?");
  statement.bind_text(1, timestamp);
  statement.bind_int(2, page_id);
  statement.step_done();
  QB_CHECK(brain.db().changes() == 1);
}

std::string ascii_lower(std::string value) {
  for (char& c : value) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return value;
}

std::string filesystem_snapshot(const std::filesystem::path& root) {
  namespace fs = std::filesystem;
  std::vector<std::string> entries;
  if (!fs::exists(root)) return {};
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    const auto relative = qbrain::util::path_to_utf8(fs::relative(entry.path(), root));
    if (entry.is_directory()) {
      entries.push_back("d|" + relative);
      continue;
    }
    if (!entry.is_regular_file()) continue;
    const auto leaf = entry.path().filename().string();
    static constexpr std::array<std::string_view, 3> kLiveTestDatabases = {
        "selected.db", "decoy.db", "damaged.db"};
    const auto is_live_test_database = std::any_of(
        kLiveTestDatabases.begin(), kLiveTestDatabases.end(),
        [&](const std::string_view database) {
          return leaf == database ||
                 (leaf.size() > database.size() && leaf.starts_with(database) &&
                  leaf[database.size()] == '-');
        });
    if (is_live_test_database) continue;
    std::ifstream input(entry.path(), std::ios::binary);
    std::ostringstream bytes;
    bytes << input.rdbuf();
    entries.push_back("f|" + relative + "|" +
                      std::to_string(entry.file_size()) + "|" +
                      qbrain::test_support::snapshot_sha256(bytes.str()));
  }
  std::sort(entries.begin(), entries.end());
  std::ostringstream snapshot;
  for (const auto& entry : entries) snapshot << entry << '\n';
  return snapshot.str();
}

template <typename Callable>
auto run_with_filesystem_snapshot(SnapshotMatrix& matrix,
                                  const std::filesystem::path& root,
                                  const std::string& label,
                                  Callable&& callable) {
  const auto before = filesystem_snapshot(root);
  auto result = matrix.run(label, std::forward<Callable>(callable));
  const auto after = filesystem_snapshot(root);
  if (before != after)
    throw std::runtime_error("N22 filesystem snapshot changed: " + label);
  return result;
}

void exercise_callee_contract(qbrain::Brain& brain, SnapshotMatrix& matrix) {
  const json expected = json::array({
      {{"source_id", "team_a"}, {"slug", "n22/body"}, {"line", 3},
       {"snippet", "Alpha(); Beta ();"}, {"kind", "callee:Alpha"}},
      {{"source_id", "team_a"}, {"slug", "n22/body"}, {"line", 3},
       {"snippet", "Alpha(); Beta ();"}, {"kind", "callee:Beta"}},
      {{"source_id", "team_a"}, {"slug", "n22/body"}, {"line", 4},
       {"snippet", "if (Guard()) {"}, {"kind", "callee:Guard"}},
      {{"source_id", "team_a"}, {"slug", "n22/body"}, {"line", 5},
       {"snippet", "Nested();"}, {"kind", "callee:Nested"}},
      {{"source_id", "team_a"}, {"slug", "n22/body"}, {"line", 7},
       {"snippet", "Dup(); Dup();"}, {"kind", "callee:Dup"}},
      {{"source_id", "team_a"}, {"slug", "n22/body"}, {"line", 8},
       {"snippet", "Dup();"}, {"kind", "callee:Dup"}},
  });

  const auto direct = matrix.run("scanner:code_callees:body", [&] {
    return qbrain::codeintel::find_callees_in_source(brain, "team_a", "BodyRoot",
                                                      200, 500);
  });
  QB_CHECK(hits_to_json(direct) == expected);

  const auto result = matrix.run("local:code_callees:body", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "team_a"}, {"symbol", "BodyRoot"},
         {"limit", "200"}, {"page_limit", "500"}});
  });
  QB_CHECK(result.ok && result.exit_code == 0);
  const auto rows = json::parse(result.json);
  QB_CHECK(rows == expected);
  require_hit_shape(rows, "team_a");
  require_no_disclosure(result);
  QB_CHECK(result.json.find("callee:BodyRoot") == std::string::npos);
  QB_CHECK(result.json.find("callee:if") == std::string::npos);
  QB_CHECK(result.json.find("callee:AfterClose") == std::string::npos);
  QB_CHECK(result.json.find("callee:UnrelatedLeak") == std::string::npos);

  const auto same_line = matrix.run("local:code_callees:same-line-close", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "team_a"}, {"symbol", "SameLineRoot"}, {"limit", "20"}});
  });
  const json same_line_expected = json::array({
      {{"source_id", "team_a"}, {"slug", "n22/same-line"}, {"line", 1},
       {"snippet", "void SameLineRoot() { SameOne(); SameTwo(); } AFTER_CLOSE_SENTINEL();"},
       {"kind", "callee:SameOne"}},
      {{"source_id", "team_a"}, {"slug", "n22/same-line"}, {"line", 1},
       {"snippet", "void SameLineRoot() { SameOne(); SameTwo(); } AFTER_CLOSE_SENTINEL();"},
       {"kind", "callee:SameTwo"}},
  });
  QB_CHECK(same_line.ok && json::parse(same_line.json) == same_line_expected);
  QB_CHECK(same_line.json.find("callee:AFTER_CLOSE_SENTINEL") == std::string::npos);

  const auto prefix_brace = matrix.run("local:code_callees:declaration-prefix-brace", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "team_a"}, {"symbol", "PrefixBraceRoot"}, {"limit", "20"}});
  });
  const json prefix_brace_expected = json::array({
      {{"source_id", "team_a"}, {"slug", "n22/prefix-brace"}, {"line", 1},
       {"snippet", "namespace N { void PrefixBraceRoot() { PrefixBraceCall(); } void Later() { PREFIX_BRACE_LEAK(); } }"},
       {"kind", "callee:PrefixBraceCall"}},
  });
  QB_CHECK(prefix_brace.ok && json::parse(prefix_brace.json) == prefix_brace_expected);
  QB_CHECK(prefix_brace.json.find("callee:PrefixBraceRoot") == std::string::npos);
  QB_CHECK(prefix_brace.json.find("callee:Later") == std::string::npos);
  QB_CHECK(prefix_brace.json.find("callee:PREFIX_BRACE_LEAK") == std::string::npos);

  const auto control_keywords = matrix.run("local:code_callees:control-keywords", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "team_a"}, {"symbol", "KeywordRoot"}, {"limit", "50"}});
  });
  QB_CHECK(control_keywords.ok);
  const auto control_rows = json::parse(control_keywords.json);
  require_hit_shape(control_rows, "team_a");
  QB_CHECK(std::any_of(control_rows.begin(), control_rows.end(), [](const auto& row) {
    return row.value("kind", "") == "callee:RealKeywordCall";
  }));
  for (const auto* keyword : {"requires", "alignas", "asm", "await", "yield", "function"}) {
    QB_CHECK(std::none_of(control_rows.begin(), control_rows.end(), [&](const auto& row) {
      return row.value("kind", "") == std::string("callee:") + keyword;
    }));
  }

  const auto multi = matrix.run("local:code_callees:multiple-definitions", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "team_a"}, {"symbol", "MultiRoot"}, {"limit", "20"}});
  });
  const json multi_expected = json::array({
      {{"source_id", "team_a"}, {"slug", "n22/multiple"}, {"line", 3},
       {"snippet", "FirstMulti();"}, {"kind", "callee:FirstMulti"}},
      {{"source_id", "team_a"}, {"slug", "n22/multiple"}, {"line", 5},
       {"snippet", "void MultiRoot() { SecondMulti(); }"},
       {"kind", "callee:SecondMulti"}},
      {{"source_id", "team_a"}, {"slug", "n22/multiple"}, {"line", 6},
       {"snippet", "void MultiRoot(int) { InlineFirst(); } void MultiRoot() { InlineSecond(); }"},
       {"kind", "callee:InlineFirst"}},
      {{"source_id", "team_a"}, {"slug", "n22/multiple"}, {"line", 6},
       {"snippet", "void MultiRoot(int) { InlineFirst(); } void MultiRoot() { InlineSecond(); }"},
       {"kind", "callee:InlineSecond"}},
  });
  QB_CHECK(multi.ok && json::parse(multi.json) == multi_expected);
  require_no_disclosure(multi);

  const auto recursive = matrix.run("local:code_callees:recursive", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "team_a"}, {"symbol", "RecursiveRoot"}});
  });
  const json recursive_rows = json::parse(recursive.json);
  QB_CHECK(recursive.ok && recursive_rows.size() == 1);
  QB_CHECK(recursive_rows[0]["line"] == 2);
  QB_CHECK(recursive_rows[0]["kind"] == "callee:RecursiveRoot");

  const auto window = matrix.run("local:code_callees:brace-window-boundary", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "team_a"}, {"symbol", "WindowRoot"}});
  });
  const auto window_rows = json::parse(window.json);
  QB_CHECK(window.ok && window_rows.size() == 1);
  QB_CHECK(window_rows[0]["line"] == 11);
  QB_CHECK(window_rows[0]["kind"] == "callee:BoundaryCall");

  for (const auto& symbol : {"TooFarRoot", "UnbalancedRoot", "RefOnlyRoot",
                             "PrefixRoot", "PrototypeRoot"}) {
    const auto empty = matrix.run("local:code_callees:empty:" + std::string(symbol), [&] {
      return qbrain::test_support::call_op(
          brain, "code_callees",
          {{"source_id", "team_a"}, {"symbol", symbol}, {"limit", "200"}});
    });
    QB_CHECK(empty.ok && json::parse(empty.json).empty());
    require_no_disclosure(empty);
  }

  const auto prototype_flow = matrix.run("local:code_flow:prototype-no-body", [&] {
    return qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "team_a"}, {"entry_point", "PrototypeRoot"}});
  });
  QB_CHECK(prototype_flow.ok && json::parse(prototype_flow.json).empty());

  const auto utf8 = matrix.run("local:code_callees:utf8-disclosure", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "team_a"}, {"symbol", "UtfRoot"}});
  });
  const auto utf8_rows = json::parse(utf8.json);
  require_hit_shape(utf8_rows, "team_a");
  QB_CHECK(utf8_rows.size() == 1 && utf8_rows[0]["line"] == 2);
  const auto utf8_snippet = utf8_rows[0]["snippet"].get<std::string>();
  QB_CHECK(utf8_rows[0]["kind"] == "callee:EscapedCall");
  QB_CHECK(utf8_snippet.find("\t") != std::string::npos);
  QB_CHECK(utf8_snippet.find("\"quote\"") != std::string::npos);
  QB_CHECK(utf8_snippet.find("C:\\path") != std::string::npos);
  QB_CHECK(utf8_snippet.find("\xEF\xBF\xBD") != std::string::npos);
  QB_CHECK(utf8_snippet.size() <= 200);
}

void exercise_flow_contract(qbrain::Brain& brain, SnapshotMatrix& matrix) {
  const json depth1_expected = json::array({
      {{"source_id", "team_a"}, {"slug", "n22/flow-root"}, {"line", 1},
       {"snippet", "void FlowRoot() { Alpha(); Beta(); }"},
       {"kind", "flow:d1:Alpha"}},
      {{"source_id", "team_a"}, {"slug", "n22/flow-root"}, {"line", 1},
       {"snippet", "void FlowRoot() { Alpha(); Beta(); }"},
       {"kind", "flow:d1:Beta"}},
  });
  auto depth1 = matrix.run("local:code_flow:depth-1", [&] {
    return qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "team_a"}, {"entry_point", "FlowRoot"},
         {"depth", "1"}, {"limit", "50"}, {"page_limit", "500"}});
  });
  QB_CHECK(depth1.ok && json::parse(depth1.json) == depth1_expected);

  json depth2_expected = depth1_expected;
  depth2_expected.push_back(
      {{"source_id", "team_a"}, {"slug", "n22/flow-alpha"}, {"line", 1},
       {"snippet", "void Alpha() { Gamma(); }"}, {"kind", "flow:d2:Gamma"}});
  auto depth2 = matrix.run("local:code_flow:depth-2", [&] {
    return qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "team_a"}, {"entry_point", "FlowRoot"},
         {"depth", "2"}, {"limit", "50"}});
  });
  QB_CHECK(depth2.ok && json::parse(depth2.json) == depth2_expected);

  json depth3_expected = depth2_expected;
  depth3_expected.push_back(
      {{"source_id", "team_a"}, {"slug", "n22/flow-gamma"}, {"line", 1},
       {"snippet", "void Gamma() { Delta(); }"}, {"kind", "flow:d3:Delta"}});
  auto depth3 = matrix.run("local:code_flow:depth-3", [&] {
    return qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "team_a"}, {"entry_point", "FlowRoot"},
         {"depth", "3"}, {"limit", "50"}});
  });
  QB_CHECK(depth3.ok && json::parse(depth3.json) == depth3_expected);
  require_hit_shape(json::parse(depth3.json), "team_a");
  require_no_disclosure(depth3);

  const auto direct = matrix.run("scanner:code_flow:bfs", [&] {
    return qbrain::codeintel::find_flow_in_source(brain, "team_a", "FlowRoot", 3,
                                                   50, 500);
  });
  QB_CHECK(hits_to_json(direct) == depth3_expected);

  auto depth8 = matrix.run("local:code_flow:depth-max", [&] {
    return qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "team_a"}, {"entry_point", "FlowRoot"},
         {"depth", "8"}, {"limit", "50"}});
  });
  auto depth9 = matrix.run("local:code_flow:depth-over-max", [&] {
    return qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "team_a"}, {"entry_point", "FlowRoot"},
         {"depth", "9"}, {"limit", "50"}});
  });
  QB_CHECK(depth8.ok && depth9.ok && depth8.json == depth9.json);
  QB_CHECK(json::parse(depth8.json) == depth3_expected);

  auto limited = matrix.run("local:code_flow:global-limit", [&] {
    return qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "team_a"}, {"entry_point", "FlowRoot"},
         {"depth", "8"}, {"limit", "2"}});
  });
  QB_CHECK(limited.ok && json::parse(limited.json) == depth1_expected);

  auto repeated = matrix.run("local:code_flow:deterministic-repeat", [&] {
    return qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "team_a"}, {"entry_point", "FlowRoot"},
         {"depth", "3"}, {"limit", "50"}});
  });
  QB_CHECK(repeated.ok && repeated.json == depth3.json && repeated.text == depth3.text);
  QB_CHECK(repeated.json.find("flow:d2:FlowRoot") == std::string::npos);
  const auto repeated_rows = json::parse(repeated.json);
  QB_CHECK(std::count_if(repeated_rows.begin(), repeated_rows.end(), [](const auto& row) {
                           return row.value("kind", "") == "flow:d2:Gamma";
                         }) == 1);
}

void exercise_blast_contract(qbrain::Brain& brain, SnapshotMatrix& matrix) {
  const json expected = json::array({
      {{"source_id", "team_a"}, {"slug", "n22/blast-def"}, {"line", 1},
       {"snippet", "void BlastRoot() {"}, {"kind", "def"}},
      {{"source_id", "team_a"}, {"slug", "n22/blast-call"}, {"line", 1},
       {"snippet", "BlastRoot();"}, {"kind", "ref"}},
      {{"source_id", "team_a"}, {"slug", "n22/blast-ref"}, {"line", 1},
       {"snippet", "auto reference = BlastRoot;"}, {"kind", "ref"}},
      {{"source_id", "team_a"}, {"slug", "n22/blast-def"}, {"line", 2},
       {"snippet", "BlastCallee();"}, {"kind", "callee:BlastCallee"}},
  });

  const auto direct = matrix.run("scanner:code_blast:priority", [&] {
    return qbrain::codeintel::find_blast_in_source(brain, "team_a", "BlastRoot",
                                                    80, 500);
  });
  QB_CHECK(hits_to_json(direct) == expected);

  auto result = matrix.run("local:code_blast:priority-dedup", [&] {
    return qbrain::test_support::call_op(
        brain, "code_blast",
        {{"source_id", "team_a"}, {"symbol", "BlastRoot"},
         {"limit", "80"}, {"page_limit", "500"}});
  });
  const auto rows = json::parse(result.json);
  QB_CHECK(result.ok && rows == expected);
  require_hit_shape(rows, "team_a");
  QB_CHECK(std::none_of(rows.begin(), rows.end(), [](const auto& row) {
    return row.value("kind", "") == "call";
  }));
  require_no_disclosure(result);

  auto one = matrix.run("local:code_blast:global-limit-one", [&] {
    return qbrain::test_support::call_op(
        brain, "code_blast",
        {{"source_id", "team_a"}, {"symbol", "BlastRoot"}, {"limit", "1"}});
  });
  QB_CHECK(one.ok && json::parse(one.json) == json::array({expected[0]}));

  auto empty = matrix.run("local:code_blast:empty", [&] {
    return qbrain::test_support::call_op(
        brain, "code_blast",
        {{"source_id", "team_a"}, {"symbol", "NoSuchBlastRoot"}});
  });
  QB_CHECK(empty.ok && json::parse(empty.json).empty());

  const auto serialized = ascii_lower(result.json + result.text);
  for (const auto forbidden : {"depth_groups", "confidence", "terminal_node",
                               "ast_result", "transitive_upstream"}) {
    QB_CHECK(serialized.find(forbidden) == std::string::npos);
  }
}

struct ReadOperationSpec {
  std::string name;
  std::string canonical;
  std::string fixture_symbol;
  std::vector<std::string> numeric_fields;
};

const std::array<ReadOperationSpec, 3>& read_operations() {
  static const std::array<ReadOperationSpec, 3> operations = {{
      {"code_callees", "symbol", "BodyRoot", {"limit", "page_limit"}},
      {"code_flow", "entry_point", "FlowRoot", {"depth", "limit", "page_limit"}},
      {"code_blast", "symbol", "BlastRoot", {"limit", "page_limit"}},
  }};
  return operations;
}

std::unordered_map<std::string, std::string> valid_args(
    const ReadOperationSpec& operation, const std::string& source_id = "team_a") {
  return {{operation.canonical, operation.fixture_symbol}, {"source_id", source_id}};
}

void exercise_symbol_and_alias_contract(qbrain::Brain& brain,
                                        SnapshotMatrix& matrix) {
  for (const auto& operation : read_operations()) {
    auto canonical = matrix.run("alias:" + operation.name + ":canonical", [&] {
      return qbrain::test_support::call_op(brain, operation.name,
                                           valid_args(operation));
    });
    QB_CHECK(canonical.ok);

    auto legacy_symbol_args = valid_args(operation);
    legacy_symbol_args.erase(operation.canonical);
    legacy_symbol_args["symbol"] = operation.fixture_symbol;
    auto symbol_alias = matrix.run("alias:" + operation.name + ":symbol", [&] {
      return qbrain::test_support::call_op(brain, operation.name,
                                           legacy_symbol_args);
    });
    QB_CHECK(symbol_alias.ok && symbol_alias.json == canonical.json);

    auto name_args = valid_args(operation);
    name_args.erase(operation.canonical);
    name_args["name"] = operation.fixture_symbol;
    auto name_alias = matrix.run("alias:" + operation.name + ":name", [&] {
      return qbrain::test_support::call_op(brain, operation.name, name_args);
    });
    QB_CHECK(name_alias.ok && name_alias.json == canonical.json);

    auto equal_args = valid_args(operation);
    equal_args["symbol"] = operation.fixture_symbol;
    equal_args["name"] = operation.fixture_symbol;
    auto equal = matrix.run("alias:" + operation.name + ":all-equal", [&] {
      return qbrain::test_support::call_op(brain, operation.name, equal_args);
    });
    QB_CHECK(equal.ok && equal.json == canonical.json);

    auto conflict_args = valid_args(operation);
    conflict_args["name"] = "DifferentSymbol";
    auto conflict = matrix.run("alias:" + operation.name + ":conflict", [&] {
      DataReadObserver observer(brain);
      auto value = qbrain::test_support::call_op(brain, operation.name, conflict_args);
      QB_CHECK(observer.page_reads() == 0);
      return value;
    });
    require_operation_error(conflict, "invalid_argument", operation.canonical);

    auto empty_args = valid_args(operation);
    empty_args["name"] = "";
    auto empty_alias = matrix.run("alias:" + operation.name + ":empty", [&] {
      DataReadObserver observer(brain);
      auto value = qbrain::test_support::call_op(brain, operation.name, empty_args);
      QB_CHECK(observer.page_reads() == 0);
      return value;
    });
    require_operation_error(empty_alias, "invalid_argument", operation.canonical);

    auto missing = matrix.run("alias:" + operation.name + ":missing", [&] {
      DataReadObserver observer(brain);
      auto value = qbrain::test_support::call_op(
          brain, operation.name, {{"source_id", "team_a"}});
      QB_CHECK(observer.page_reads() == 0);
      return value;
    });
    require_operation_error(missing, "invalid_argument", operation.canonical);

    auto unexpected_args = valid_args(operation);
    unexpected_args["unexpected"] = "SECRET_CONFIG_VALUE";
    auto unexpected = matrix.run("alias:" + operation.name + ":unexpected", [&] {
      DataReadObserver observer(brain);
      auto value = qbrain::test_support::call_op(brain, operation.name,
                                                 unexpected_args);
      QB_CHECK(observer.page_reads() == 0);
      return value;
    });
    require_operation_error(unexpected, "invalid_argument", "unexpected",
                            "SECRET_CONFIG_VALUE");
  }

  const std::vector<std::string> valid_no_match = {
      "$value", "_value", "~Widget", "ns::member", "ns::~Widget",
      std::string(256, 'a')};
  for (const auto& operation : read_operations()) {
    for (size_t index = 0; index < valid_no_match.size(); ++index) {
      auto arguments = valid_args(operation);
      arguments[operation.canonical] = valid_no_match[index];
      auto result = matrix.run("symbol:" + operation.name + ":valid-no-match:" +
                                   std::to_string(index),
                               [&] {
        return qbrain::test_support::call_op(brain, operation.name, arguments);
      });
      QB_CHECK(result.ok && json::parse(result.json).empty());
    }
  }

  std::string nul_symbol("bad\0symbol", 10);
  const std::vector<std::string> invalid_symbols = {
      "", "Bad Secret Symbol", "bad\tsymbol", "bad\nsymbol", "bad.symbol",
      "bad[0]", "bad*", ":", "bad:::symbol", "bad::", "1bad",
      std::string(257, 'a'), nul_symbol};
  for (const auto& operation : read_operations()) {
    for (size_t index = 0; index < invalid_symbols.size(); ++index) {
      auto arguments = valid_args(operation);
      arguments[operation.canonical] = invalid_symbols[index];
      auto result = matrix.run("symbol:" + operation.name + ":invalid:" +
                                   std::to_string(index),
                               [&] {
        DataReadObserver observer(brain);
        auto value = qbrain::test_support::call_op(brain, operation.name, arguments);
        QB_CHECK(observer.page_reads() == 0);
        return value;
      });
      const std::string forbidden =
          invalid_symbols[index] == "Bad Secret Symbol" ? invalid_symbols[index] : "";
      require_operation_error(result, "invalid_argument", operation.canonical, forbidden);
    }
  }
}

void exercise_numeric_contract(qbrain::Brain& brain, SnapshotMatrix& matrix) {
  auto size_for = [&](const std::string& label, const std::string& operation,
                      std::unordered_map<std::string, std::string> arguments) {
    auto result = matrix.run(label, [&] {
      return qbrain::test_support::call_op(brain, operation, std::move(arguments));
    });
    QB_CHECK(result.ok);
    const auto rows = json::parse(result.json);
    require_hit_shape(rows, "number_source");
    return rows.size();
  };

  QB_CHECK(size_for("numeric:code_callees:default", "code_callees",
                    {{"source_id", "number_source"}, {"symbol", "FanRoot"}}) == 50);
  QB_CHECK(size_for("numeric:code_callees:zero", "code_callees",
                    {{"source_id", "number_source"}, {"symbol", "FanRoot"},
                     {"limit", "0"}}) == 1);
  QB_CHECK(size_for("numeric:code_callees:one", "code_callees",
                    {{"source_id", "number_source"}, {"symbol", "FanRoot"},
                     {"limit", "1"}}) == 1);
  QB_CHECK(size_for("numeric:code_callees:max", "code_callees",
                    {{"source_id", "number_source"}, {"symbol", "FanRoot"},
                     {"limit", "200"}}) == 200);
  QB_CHECK(size_for("numeric:code_callees:over-max", "code_callees",
                    {{"source_id", "number_source"}, {"symbol", "FanRoot"},
                     {"limit", "201"}}) == 200);

  QB_CHECK(size_for("numeric:code_flow:default", "code_flow",
                    {{"source_id", "number_source"}, {"entry_point", "FanRoot"}}) ==
           50);
  QB_CHECK(size_for("numeric:code_flow:limit-zero", "code_flow",
                    {{"source_id", "number_source"}, {"entry_point", "FanRoot"},
                     {"depth", "1"}, {"limit", "0"}}) == 1);
  QB_CHECK(size_for("numeric:code_flow:limit-one", "code_flow",
                    {{"source_id", "number_source"}, {"entry_point", "FanRoot"},
                     {"depth", "1"}, {"limit", "1"}}) == 1);
  QB_CHECK(size_for("numeric:code_flow:limit-max", "code_flow",
                    {{"source_id", "number_source"}, {"entry_point", "FanRoot"},
                     {"depth", "1"}, {"limit", "200"}}) == 200);
  QB_CHECK(size_for("numeric:code_flow:limit-over-max", "code_flow",
                    {{"source_id", "number_source"}, {"entry_point", "FanRoot"},
                     {"depth", "1"}, {"limit", "201"}}) == 200);

  auto flow_depth_zero = matrix.run("numeric:code_flow:depth-zero", [&] {
    return qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "number_source"}, {"entry_point", "FanRoot"},
         {"depth", "0"}, {"limit", "200"}});
  });
  auto flow_depth_one = matrix.run("numeric:code_flow:depth-one", [&] {
    return qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "number_source"}, {"entry_point", "FanRoot"},
         {"depth", "1"}, {"limit", "200"}});
  });
  QB_CHECK(flow_depth_zero.ok && flow_depth_one.ok &&
           flow_depth_zero.json == flow_depth_one.json);

  QB_CHECK(size_for("numeric:code_blast:default", "code_blast",
                    {{"source_id", "number_source"}, {"symbol", "FanRoot"}}) == 80);
  QB_CHECK(size_for("numeric:code_blast:zero", "code_blast",
                    {{"source_id", "number_source"}, {"symbol", "FanRoot"},
                     {"limit", "0"}}) == 1);
  QB_CHECK(size_for("numeric:code_blast:max", "code_blast",
                    {{"source_id", "number_source"}, {"symbol", "FanRoot"},
                     {"limit", "200"}}) == 200);
  QB_CHECK(size_for("numeric:code_blast:over-max", "code_blast",
                    {{"source_id", "number_source"}, {"symbol", "FanRoot"},
                     {"limit", "201"}}) == 200);

  for (const auto& operation : read_operations()) {
    for (const auto& page_limit : {"0", "1", "2000", "2001"}) {
      auto arguments = valid_args(operation, "number_source");
      arguments[operation.canonical] = "FanRoot";
      arguments["page_limit"] = page_limit;
      arguments["limit"] = "1";
      if (operation.name == "code_flow") arguments["depth"] = "1";
      const auto count = size_for("numeric:" + operation.name + ":page-limit-" +
                                      page_limit,
                                  operation.name, std::move(arguments));
      QB_CHECK(count == 1);
    }
  }

  const std::vector<std::string> invalid_numbers = {
      "", "+1", "-1", " 1", "1 ", "1 0", "1.0", "1suffix",
      "18446744073709551616"};
  for (const auto& operation : read_operations()) {
    for (const auto& field : operation.numeric_fields) {
      for (size_t index = 0; index < invalid_numbers.size(); ++index) {
        auto arguments = valid_args(operation, "number_source");
        arguments[operation.canonical] = "FanRoot";
        arguments[field] = invalid_numbers[index];
        auto result = matrix.run("numeric:" + operation.name + ":invalid:" + field +
                                     ":" + std::to_string(index),
                                 [&] {
          DataReadObserver observer(brain);
          auto value = qbrain::test_support::call_op(brain, operation.name,
                                                     arguments);
          QB_CHECK(observer.page_reads() == 0);
          return value;
        });
        require_operation_error(result, "invalid_argument", field,
                                invalid_numbers[index] == "1suffix"
                                    ? invalid_numbers[index]
                                    : "");
      }
    }
  }
}

void exercise_source_contract(qbrain::Brain& brain, SnapshotMatrix& matrix) {
  for (const auto& operation : read_operations()) {
    auto local = matrix.run("source:" + operation.name + ":local-mixed-case", [&] {
      auto arguments = valid_args(operation, "Team_A");
      return qbrain::test_support::call_op(brain, operation.name, arguments);
    });
    QB_CHECK(local.ok);
    const auto rows = json::parse(local.json);
    require_hit_shape(rows, "team_a");
    require_no_disclosure(local);

    auto remote_default = matrix.run("source:" + operation.name + ":remote-default", [&] {
      std::unordered_map<std::string, std::string> arguments = {
          {operation.canonical, "AmbientRoot"}, {"source_id", "default"}};
      return qbrain::test_support::call_op(brain, operation.name, arguments, true,
                                           false);
    });
    QB_CHECK(remote_default.ok);
    require_hit_shape(json::parse(remote_default.json), "default");

    auto denied = matrix.run("source:" + operation.name + ":remote-denied", [&] {
      DataReadObserver observer(brain);
      auto value = qbrain::test_support::call_op(brain, operation.name,
                                                 valid_args(operation), true, false);
      QB_CHECK(observer.page_reads() == 0);
      return value;
    });
    require_operation_error(denied, "source_not_allowed", "source_id");

    auto write_does_not_authorize =
        matrix.run("source:" + operation.name + ":write-does-not-authorize", [&] {
          DataReadObserver observer(brain);
          auto value = qbrain::test_support::call_op(
              brain, operation.name, valid_args(operation), true, true);
          QB_CHECK(observer.page_reads() == 0);
          return value;
        });
    require_operation_error(write_does_not_authorize, "source_not_allowed",
                            "source_id");
  }

  const std::vector<std::pair<std::string, std::string>> invalid_sources = {
      {"", "invalid_source"},
      {"bad/slash", "invalid_source"},
      {"CON", "invalid_source"},
      {std::string(65, 's'), "invalid_source"},
      {"valid_but_unknown", "source_not_found"},
  };
  for (const auto& operation : read_operations()) {
    for (size_t index = 0; index < invalid_sources.size(); ++index) {
      auto arguments = valid_args(operation, invalid_sources[index].first);
      auto result = matrix.run("source:" + operation.name + ":invalid:" +
                                   std::to_string(index),
                               [&] {
        DataReadObserver observer(brain);
        auto value = qbrain::test_support::call_op(brain, operation.name,
                                                   arguments);
        QB_CHECK(observer.page_reads() == 0);
        return value;
      });
      require_operation_error(result, invalid_sources[index].second, "source_id");
    }
  }
  QB_CHECK(!brain.source_exists("valid_but_unknown"));

  auto ordered = matrix.run("source:code_callees:active-before-page-limit", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "team_a"}, {"symbol", "OrderRoot"},
         {"page_limit", "2"}, {"limit", "20"}});
  });
  const json ordered_expected = json::array({
      {{"source_id", "team_a"}, {"slug", "n22/order-new"}, {"line", 1},
       {"snippet", "void OrderRoot() { NewOrderCall(); }"},
       {"kind", "callee:NewOrderCall"}},
      {{"source_id", "team_a"}, {"slug", "n22/order-old"}, {"line", 1},
       {"snippet", "void OrderRoot() { OldOrderCall(); }"},
       {"kind", "callee:OldOrderCall"}},
  });
  QB_CHECK(ordered.ok && json::parse(ordered.json) == ordered_expected);
  require_no_disclosure(ordered);

  auto ordered_flow = matrix.run("source:code_flow:active-before-page-limit", [&] {
    return qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "team_a"}, {"entry_point", "OrderRoot"},
         {"depth", "1"}, {"page_limit", "2"}, {"limit", "20"}});
  });
  json ordered_flow_expected = ordered_expected;
  ordered_flow_expected[0]["kind"] = "flow:d1:NewOrderCall";
  ordered_flow_expected[1]["kind"] = "flow:d1:OldOrderCall";
  QB_CHECK(ordered_flow.ok && json::parse(ordered_flow.json) == ordered_flow_expected);
  require_no_disclosure(ordered_flow);

  auto ordered_blast = matrix.run("source:code_blast:active-before-page-limit", [&] {
    return qbrain::test_support::call_op(
        brain, "code_blast",
        {{"source_id", "team_a"}, {"symbol", "OrderRoot"},
         {"page_limit", "2"}, {"limit", "20"}});
  });
  json ordered_blast_expected = ordered_expected;
  ordered_blast_expected[0]["kind"] = "def";
  ordered_blast_expected[1]["kind"] = "def";
  QB_CHECK(ordered_blast.ok && json::parse(ordered_blast.json) == ordered_blast_expected);
  require_no_disclosure(ordered_blast);

  brain.save_config_value("mcp.allowed_sources", "TEAM_A");
  for (const auto& operation : read_operations()) {
    auto allowed = matrix.run("source:" + operation.name + ":allowlisted", [&] {
      auto arguments = valid_args(operation, "Team_A");
      return qbrain::test_support::call_op(brain, operation.name, arguments, true,
                                           false);
    });
    QB_CHECK(allowed.ok);
    require_hit_shape(json::parse(allowed.json), "team_a");
    require_no_disclosure(allowed);
  }
}

void require_schema_number(const json& schema, const std::string& field,
                           int default_value, int maximum) {
  const auto& value = schema["properties"][field];
  QB_CHECK(value["type"] == "integer");
  QB_CHECK(value["minimum"] == 0);
  QB_CHECK(value["maximum"] == maximum);
  QB_CHECK(value["default"] == default_value);
}

void exercise_registry_contract(qbrain::Brain& brain, SnapshotMatrix& matrix) {
  matrix.run("registry:operations", [&] {
    const auto& registry = qbrain::ops::global_registry();
    const auto names = registry.names();
    for (const auto& name : {"code_callees", "code_flow", "code_blast",
                             "code_traversal_cache_clear"}) {
      QB_CHECK(std::count(names.begin(), names.end(), name) == 1);
    }

    const auto* callees = registry.find("code_callees");
    const auto* flow = registry.find("code_flow");
    const auto* blast = registry.find("code_blast");
    const auto* cache = registry.find("code_traversal_cache_clear");
    QB_CHECK(callees && flow && blast && cache);
    QB_CHECK(callees->scope == qbrain::ops::Scope::Read && !callees->local_only);
    QB_CHECK(flow->scope == qbrain::ops::Scope::Read && !flow->local_only);
    QB_CHECK(blast->scope == qbrain::ops::Scope::Read && !blast->local_only);
    QB_CHECK(cache->scope == qbrain::ops::Scope::Admin && cache->local_only);

    const auto callee_description = ascii_lower(callees->description);
    const auto flow_description = ascii_lower(flow->description);
    const auto blast_description = ascii_lower(blast->description);
    const auto cache_description = ascii_lower(cache->description);
    const auto require_bounded_heuristic_description = [](const std::string& description) {
      QB_CHECK(description.find("stateless bounded source-text heuristic") !=
               std::string::npos);
      QB_CHECK(description.find("16 kib/page") != std::string::npos);
      QB_CHECK(description.find("8 mib") != std::string::npos);
      QB_CHECK(description.find("16384 lines/corpus") != std::string::npos);
      QB_CHECK(description.find("exact lexical identifier matching") !=
               std::string::npos);
      QB_CHECK(description.find("no ast, tree-sitter, or compiler index") !=
               std::string::npos);
      QB_CHECK(description.find("no overload/type resolution") !=
               std::string::npos);
      QB_CHECK(description.find("no persisted call edges/cache") !=
               std::string::npos);
      QB_CHECK(description.find("not recursive/transitive upstream parity") !=
               std::string::npos);
      for (const auto overclaim : {"ast-backed", "compiler-index-backed",
                                   "semantic call graph", "exact call graph",
                                   "overload-aware", "type-resolved",
                                   "persisted call graph", "persistent cache"}) {
        QB_CHECK(description.find(overclaim) == std::string::npos);
      }
    };
    require_bounded_heuristic_description(callee_description);
    require_bounded_heuristic_description(flow_description);
    require_bounded_heuristic_description(blast_description);
    QB_CHECK(callee_description.find(
                 "one-hop source-scoped bounded brace-body callee scan") !=
             std::string::npos);
    QB_CHECK(flow_description.find("deterministic breadth-first traversal") !=
             std::string::npos);
    QB_CHECK(flow_description.find("source-scoped brace-body callees") !=
             std::string::npos);
    QB_CHECK(flow_description.find("no terminal/sink classification") !=
             std::string::npos);
    QB_CHECK(blast_description.find(
                 "bounded one-hop source-scoped def/ref/caller/callee heuristic subset") !=
             std::string::npos);
    QB_CHECK(blast_description.find("using brace-body callees") !=
             std::string::npos);
    QB_CHECK(cache_description.find("stateless") != std::string::npos);
    QB_CHECK(cache_description.find("guarded") != std::string::npos);
    QB_CHECK(cache_description.find("clears zero rows") != std::string::npos);
    QB_CHECK(cache_description.find("no persisted traversal cache") !=
             std::string::npos);
    QB_CHECK(cache_description.find("no cache table") != std::string::npos);
    QB_CHECK(cache_description.find("no schema migration") != std::string::npos);

    const auto callee_schema = json::parse(callees->input_schema_json);
    const auto flow_schema = json::parse(flow->input_schema_json);
    const auto blast_schema = json::parse(blast->input_schema_json);
    const auto cache_schema = json::parse(cache->input_schema_json);
    for (const auto* schema : {&callee_schema, &flow_schema, &blast_schema,
                               &cache_schema}) {
      QB_CHECK((*schema)["type"] == "object");
      QB_CHECK((*schema)["additionalProperties"] == false);
    }

    require_keys(callee_schema["properties"],
                 {"symbol", "name", "source_id", "limit", "page_limit"});
    require_keys(blast_schema["properties"],
                 {"symbol", "name", "source_id", "limit", "page_limit"});
    require_keys(flow_schema["properties"],
                 {"entry_point", "symbol", "name", "source_id", "depth", "limit",
                  "page_limit"});
    for (const auto* schema : {&callee_schema, &blast_schema, &flow_schema}) {
      QB_CHECK((*schema)["properties"]["source_id"]["type"] == "string");
      QB_CHECK((*schema)["properties"]["source_id"]["default"] == "default");
      QB_CHECK((*schema)["properties"]["source_id"]["minLength"] == 1);
      QB_CHECK((*schema)["properties"]["source_id"]["maxLength"] == 64);
      QB_CHECK((*schema)["properties"]["source_id"]["pattern"] ==
               "^(?!(?:[Cc][Oo][Nn]|[Pp][Rr][Nn]|[Aa][Uu][Xx]|[Nn][Uu][Ll]|"
               "[Cc][Oo][Mm][1-9]|[Ll][Pp][Tt][1-9])$)[A-Za-z0-9_-]+$");
      QB_CHECK((*schema).contains("anyOf"));
      require_schema_number(*schema, "page_limit", 500, 2000);
    }
    for (const auto& field : {"symbol", "name"}) {
      QB_CHECK(callee_schema["properties"][field]["type"] == "string");
      QB_CHECK(callee_schema["properties"][field]["maxLength"] == 256);
      QB_CHECK(blast_schema["properties"][field]["maxLength"] == 256);
      QB_CHECK(flow_schema["properties"][field]["maxLength"] == 256);
    }
    QB_CHECK(flow_schema["properties"]["entry_point"]["type"] == "string");
    QB_CHECK(flow_schema["properties"]["entry_point"]["maxLength"] == 256);
    require_schema_number(callee_schema, "limit", 50, 200);
    require_schema_number(flow_schema, "depth", 2, 8);
    require_schema_number(flow_schema, "limit", 50, 200);
    require_schema_number(blast_schema, "limit", 80, 200);
    require_keys(cache_schema["properties"], {});
    return true;
  });

  const auto tools_list = matrix.run("registry:tools-list", [&] {
    return json::parse(qbrain::mcp::handle_rpc_body(
        brain, {},
        R"({"jsonrpc":"2.0","id":2200,"method":"tools/list","params":{}})"));
  });
  QB_CHECK(tools_list["result"]["tools"].is_array());
  for (const auto& name : {"code_callees", "code_flow", "code_blast",
                           "code_traversal_cache_clear"}) {
    const auto count = std::count_if(
        tools_list["result"]["tools"].begin(),
        tools_list["result"]["tools"].end(),
        [&](const auto& tool) { return tool.value("name", "") == name; });
    QB_CHECK(count == 1);
    const auto tool = std::find_if(
        tools_list["result"]["tools"].begin(),
        tools_list["result"]["tools"].end(),
        [&](const auto& value) { return value.value("name", "") == name; });
    QB_CHECK(tool != tools_list["result"]["tools"].end());
    const auto* operation = qbrain::ops::global_registry().find(name);
    QB_CHECK(operation);
    QB_CHECK((*tool)["description"] == operation->description);
    QB_CHECK((*tool)["inputSchema"] == json::parse(operation->input_schema_json));
  }
}

void exercise_mcp_contract(qbrain::Brain& brain, SnapshotMatrix& matrix) {
  brain.save_config_value("mcp.allowed_sources", "TEAM_A,NUMBER_SOURCE");
  qbrain::mcp::ServeOptions options;
  int request_id = 2300;

  for (const auto& operation : read_operations()) {
    json arguments = {{operation.canonical, operation.fixture_symbol},
                      {"source_id", "Team_A"}};
    if (operation.name == "code_flow") arguments["depth"] = 3;
    auto success = matrix.run("mcp:" + operation.name + ":success", [&] {
      return mcp_call(brain, options, operation.name, arguments, request_id++);
    });
    const auto rows = require_mcp_rows(success);
    QB_CHECK(!rows.empty());
    require_hit_shape(rows, "team_a");

    auto empty_arguments = json{{operation.canonical, "NoSuchMcpSymbol"},
                                {"source_id", "Team_A"}};
    auto empty = matrix.run("mcp:" + operation.name + ":empty", [&] {
      return mcp_call(brain, options, operation.name, empty_arguments,
                      request_id++);
    });
    QB_CHECK(require_mcp_rows(empty).empty());

    json clamp_arguments = {{operation.canonical, "FanRoot"},
                            {"source_id", "number_source"},
                            {"limit", 999u},
                            {"page_limit", 9999u}};
    if (operation.name == "code_flow") clamp_arguments["depth"] = 99u;
    auto clamped = matrix.run("mcp:" + operation.name + ":clamp", [&] {
      return mcp_call(brain, options, operation.name, clamp_arguments,
                      request_id++);
    });
    QB_CHECK(require_mcp_rows(clamped).size() == 200);

    auto non_object = matrix.run("mcp:" + operation.name + ":non-object", [&] {
      DataReadObserver observer(brain);
      auto response = mcp_call(brain, options, operation.name, json::array(),
                               request_id++);
      QB_CHECK(observer.page_reads() == 0);
      return response;
    });
    require_mcp_error(non_object, "invalid_argument", "arguments");

    auto unknown = matrix.run("mcp:" + operation.name + ":unknown-field", [&] {
      DataReadObserver observer(brain);
      auto response = mcp_call(
          brain, options, operation.name,
          json{{operation.canonical, operation.fixture_symbol},
               {"unexpected", "UNAUTHORIZED_SENTINEL"}},
          request_id++);
      QB_CHECK(observer.page_reads() == 0);
      return response;
    });
    require_mcp_error(unknown, "invalid_argument", "unexpected");

    const std::vector<json> wrong_strings = {
        nullptr, true, 7, json::object(), json::array()};
    for (size_t index = 0; index < wrong_strings.size(); ++index) {
      auto wrong_symbol = matrix.run(
          "mcp:" + operation.name + ":wrong-symbol:" + std::to_string(index), [&] {
        DataReadObserver observer(brain);
        auto response = mcp_call(
            brain, options, operation.name,
            json{{operation.canonical, wrong_strings[index]}}, request_id++);
        QB_CHECK(observer.page_reads() == 0);
        return response;
      });
      require_mcp_error(wrong_symbol, "invalid_argument", operation.canonical);

      auto wrong_source = matrix.run(
          "mcp:" + operation.name + ":wrong-source:" + std::to_string(index), [&] {
        DataReadObserver observer(brain);
        auto response = mcp_call(
            brain, options, operation.name,
            json{{operation.canonical, operation.fixture_symbol},
                 {"source_id", wrong_strings[index]}},
            request_id++);
        QB_CHECK(observer.page_reads() == 0);
        return response;
      });
      require_mcp_error(wrong_source, "invalid_argument", "source_id");
    }

    const std::vector<json> wrong_numbers = {
        nullptr, true, "1", json::object(), json::array(), -1, 1.5};
    for (const auto& field : operation.numeric_fields) {
      for (size_t index = 0; index < wrong_numbers.size(); ++index) {
        auto malformed = matrix.run(
            "mcp:" + operation.name + ":wrong-number:" + field + ":" +
                std::to_string(index),
            [&] {
          DataReadObserver observer(brain);
          auto response = mcp_call(
              brain, options, operation.name,
              json{{operation.canonical, operation.fixture_symbol},
                   {field, wrong_numbers[index]}},
              request_id++);
          QB_CHECK(observer.page_reads() == 0);
          return response;
        });
        require_mcp_error(malformed, "invalid_argument", field);
      }
    }

    json conflict_arguments = {{operation.canonical, operation.fixture_symbol},
                               {"name", "DifferentMcpSymbol"},
                               {"source_id", "team_a"}};
    auto conflict = matrix.run("mcp:" + operation.name + ":alias-conflict", [&] {
      DataReadObserver observer(brain);
      auto response = mcp_call(brain, options, operation.name, conflict_arguments,
                               request_id++);
      QB_CHECK(observer.page_reads() == 0);
      return response;
    });
    require_mcp_error(conflict, "invalid_argument", operation.canonical);

    auto unknown_source = matrix.run("mcp:" + operation.name + ":unknown-source", [&] {
      DataReadObserver observer(brain);
      auto response = mcp_call(
          brain, options, operation.name,
          json{{operation.canonical, operation.fixture_symbol},
               {"source_id", "valid_but_unknown"}},
          request_id++);
      QB_CHECK(observer.page_reads() == 0);
      return response;
    });
    require_mcp_error(unknown_source, "source_not_allowed", "source_id");

    auto denied_source = matrix.run("mcp:" + operation.name + ":denied-source", [&] {
      DataReadObserver observer(brain);
      auto response = mcp_call(
          brain, options, operation.name,
          json{{operation.canonical, operation.fixture_symbol},
               {"source_id", "team_b"}},
          request_id++);
      QB_CHECK(observer.page_reads() == 0);
      return response;
    });
    require_mcp_error(denied_source, "source_not_allowed", "source_id");

    qbrain::mcp::ServeOptions write_options;
    write_options.allow_write = true;
    auto write_does_not_authorize =
        matrix.run("mcp:" + operation.name + ":write-does-not-authorize", [&] {
          DataReadObserver observer(brain);
          auto response = mcp_call(
              brain, write_options, operation.name,
              json{{operation.canonical, operation.fixture_symbol},
                   {"source_id", "team_b"}},
              request_id++);
          QB_CHECK(observer.page_reads() == 0);
          return response;
        });
    require_mcp_error(write_does_not_authorize, "source_not_allowed", "source_id");
  }

  {
    ScopedEnvironmentVariable ambient_source("QBRAIN_SOURCE", "team_a");
    for (const auto& operation : read_operations()) {
      auto response = matrix.run("mcp:" + operation.name + ":ambient-default", [&] {
        return mcp_call(brain, options, operation.name,
                        json{{operation.canonical, "AmbientRoot"}}, request_id++);
      });
      const auto rows = require_mcp_rows(response);
      QB_CHECK(!rows.empty());
      require_hit_shape(rows, "default");
      QB_CHECK(response.dump().find("TeamAmbient") == std::string::npos);
    }
  }
}

void exercise_damaged_database_contract(const std::filesystem::path& database_path,
                                        qbrain::Brain& decoy) {
  qbrain::Brain damaged("n22-damaged");
  damaged.open_at(qbrain::util::path_to_utf8(database_path));
  damaged.db().exec("PRAGMA foreign_keys=OFF;");
  damaged.db().exec("DROP TABLE sources;");
  SnapshotMatrix matrix(damaged, decoy);
  qbrain::mcp::ServeOptions options;
  int request_id = 2800;

  for (const auto& operation : read_operations()) {
    auto direct = matrix.run("damaged:direct:" + operation.name, [&] {
      return qbrain::test_support::call_op(
          damaged, operation.name, valid_args(operation, "default"));
    });
    require_operation_error(direct, "database_error", "database", database_path.string());

    json arguments = {{operation.canonical, operation.fixture_symbol},
                      {"source_id", "default"}};
    auto remote = matrix.run("damaged:mcp:" + operation.name, [&] {
      return mcp_call(damaged, options, operation.name, arguments, request_id++);
    });
    require_mcp_error(remote, "database_error", "database");
    QB_CHECK(remote.dump().find(qbrain::util::path_to_utf8(database_path)) ==
             std::string::npos);
  }
  damaged.close();
}

void require_cache_success(const qbrain::ops::OpResult& result) {
  QB_CHECK(result.ok && result.exit_code == 0);
  const auto payload = json::parse(result.json);
  require_keys(payload, {"cleared", "stateless"});
  QB_CHECK(payload["cleared"] == 0);
  QB_CHECK(payload["stateless"] == true);
  const auto text = ascii_lower(result.text);
  QB_CHECK(text.size() <= 512);
  QB_CHECK(text.find("0") != std::string::npos);
  QB_CHECK(text.find("stateless") != std::string::npos);
}

void exercise_cache_contract(qbrain::Brain& brain, SnapshotMatrix& matrix,
                             const std::filesystem::path& root) {
  auto local = run_with_filesystem_snapshot(matrix, root, "cache:local-success", [&] {
    return qbrain::test_support::call_op(brain, "code_traversal_cache_clear");
  });
  require_cache_success(local);

  auto unexpected = run_with_filesystem_snapshot(
      matrix, root, "cache:unexpected-argument:local", [&] {
        DataReadObserver observer(brain);
        auto value = qbrain::test_support::call_op(
            brain, "code_traversal_cache_clear",
            {{"unexpected", "SECRET_CONFIG_VALUE"}});
        QB_CHECK(observer.page_reads() == 0);
        return value;
      });
  require_operation_error(unexpected, "invalid_argument", "unexpected",
                          "SECRET_CONFIG_VALUE");

  qbrain::mcp::ServeOptions denied_options;
  int request_id = 2600;
  auto denied = run_with_filesystem_snapshot(
      matrix, root, "mcp:code_traversal_cache_clear:remote-denied", [&] {
        return mcp_call(brain, denied_options, "code_traversal_cache_clear",
                        json::object(), request_id++);
      });
  require_mcp_error(denied, "write_denied", "operation");

  qbrain::mcp::ServeOptions allowed_options;
  allowed_options.allow_write = true;
  {
    ScopedEnvironmentVariable ambient_source("QBRAIN_SOURCE", "team_a");
    auto allowed = run_with_filesystem_snapshot(
        matrix, root, "mcp:code_traversal_cache_clear:remote-allowed", [&] {
          return mcp_call(brain, allowed_options, "code_traversal_cache_clear",
                          json::object(), request_id++);
        });
    QB_CHECK(!allowed["result"].value("isError", true));
    const auto payload = structured_mcp_content(allowed);
    require_keys(payload, {"cleared", "stateless"});
    QB_CHECK(payload["cleared"] == 0 && payload["stateless"] == true);
  }

  auto mcp_unexpected = run_with_filesystem_snapshot(
      matrix, root, "mcp:code_traversal_cache_clear:unexpected-argument", [&] {
        DataReadObserver observer(brain);
        auto response = mcp_call(
            brain, allowed_options, "code_traversal_cache_clear",
            json{{"unexpected", "UNAUTHORIZED_SENTINEL"}}, request_id++);
        QB_CHECK(observer.page_reads() == 0);
        return response;
      });
  require_mcp_error(mcp_unexpected, "invalid_argument", "unexpected");

  auto mcp_non_object = run_with_filesystem_snapshot(
      matrix, root, "mcp:code_traversal_cache_clear:non-object", [&] {
        DataReadObserver observer(brain);
        auto response = mcp_call(brain, allowed_options,
                                 "code_traversal_cache_clear", json::array(),
                                 request_id++);
        QB_CHECK(observer.page_reads() == 0);
        return response;
      });
  require_mcp_error(mcp_non_object, "invalid_argument", "arguments");

  QB_CHECK(qbrain::test_support::scalar(
               brain,
               "SELECT COUNT(*) FROM sqlite_master WHERE "
               "lower(name) LIKE '%traversal%cache%'") == 0);
}

void exercise_page_limit_contract(qbrain::Brain& brain, SnapshotMatrix& matrix) {
  QB_CHECK(brain.ensure_source("page_default"));
  qbrain::test_support::put_page(
      brain, "page_default", "target", "void PageDefaultRoot() { DefaultTarget(); }");
  brain.db().exec("BEGIN;");
  for (int index = 0; index < 500; ++index) {
    qbrain::test_support::put_page(brain, "page_default",
                                   "filler-" + std::to_string(index), "no match");
  }
  brain.db().exec("COMMIT;");

  auto default_limit = matrix.run("page-limit:default-500", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "page_default"}, {"symbol", "PageDefaultRoot"}});
  });
  auto expanded = matrix.run("page-limit:explicit-501", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "page_default"}, {"symbol", "PageDefaultRoot"},
         {"page_limit", "501"}});
  });
  QB_CHECK(default_limit.ok && json::parse(default_limit.json).empty());
  QB_CHECK(expanded.ok && json::parse(expanded.json).size() == 1);

  QB_CHECK(brain.ensure_source("page_cap"));
  qbrain::test_support::put_page(
      brain, "page_cap", "target", "void PageCapRoot() { CapTarget(); }");
  brain.db().exec("BEGIN;");
  for (int index = 0; index < 2000; ++index) {
    qbrain::test_support::put_page(brain, "page_cap",
                                   "filler-" + std::to_string(index), "no match");
  }
  brain.db().exec("COMMIT;");

  std::string baseline;
  for (const auto& page_limit : {"2000", "2001", "999999"}) {
    auto result = matrix.run("page-limit:cap:" + std::string(page_limit), [&] {
      return qbrain::test_support::call_op(
          brain, "code_callees",
          {{"source_id", "page_cap"}, {"symbol", "PageCapRoot"},
           {"page_limit", page_limit}});
    });
    QB_CHECK(result.ok && json::parse(result.json).empty());
    if (baseline.empty()) baseline = result.json;
    QB_CHECK(result.json == baseline);
  }
}

void exercise_resource_bound_contract(qbrain::Brain& brain, SnapshotMatrix& matrix) {
  QB_CHECK(brain.ensure_source("body_budget"));
  std::string oversized_body = "void BodyBudgetRoot() { BudgetCall(); }\n";
  oversized_body.append(17 * 1024, 'x');
  qbrain::test_support::put_page(brain, "body_budget", "n22/body-budget",
                                 oversized_body);
  const auto body_budget = matrix.run("resource:page-body-budget", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "body_budget"}, {"symbol", "BodyBudgetRoot"}});
  });
  require_operation_error(body_budget, "resource_limit", "source_id");

  QB_CHECK(brain.ensure_source("line_budget"));
  std::string line_body = "void LineBudgetRoot() { BudgetCall(); }\n";
  for (int line = 0; line < 6000; ++line) line_body += "x\n";
  for (int page = 0; page < 3; ++page) {
    qbrain::test_support::put_page(brain, "line_budget",
                                   "n22/line-budget-" + std::to_string(page), line_body);
  }
  const auto line_budget = matrix.run("resource:source-line-budget", [&] {
    return qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "line_budget"}, {"entry_point", "LineBudgetRoot"},
         {"depth", "8"}, {"page_limit", "3"}});
  });
  require_operation_error(line_budget, "resource_limit", "source_id");

  QB_CHECK(brain.ensure_source("slug_length"));
  const std::string long_slug(513, 's');
  qbrain::test_support::put_page(brain, "slug_length", long_slug,
                                 "void SlugLengthRoot() { BudgetCall(); }\n");
  const auto slug_length = matrix.run("resource:slug-length-bound", [&] {
    return qbrain::test_support::call_op(
        brain, "code_blast",
        {{"source_id", "slug_length"}, {"symbol", "SlugLengthRoot"}});
  });
  require_operation_error(slug_length, "resource_limit", "source_id", long_slug);

  QB_CHECK(brain.ensure_source("slug_control"));
  const std::string control_slug = "n22/control\nslug";
  qbrain::test_support::put_page(brain, "slug_control", control_slug,
                                 "void SlugControlRoot() { BudgetCall(); }\n");
  const auto slug_control = matrix.run("resource:slug-control-bound", [&] {
    return qbrain::test_support::call_op(
        brain, "code_callees",
        {{"source_id", "slug_control"}, {"symbol", "SlugControlRoot"}});
  });
  require_operation_error(slug_control, "resource_limit", "source_id", control_slug);
}

void seed_core_fixtures(qbrain::Brain& brain) {
  QB_CHECK(brain.ensure_source("team_a"));
  QB_CHECK(brain.ensure_source("team_b"));
  QB_CHECK(brain.ensure_source("number_source"));

  qbrain::test_support::put_page(
      brain, "team_a", "n22/body",
      "void BodyRoot()\n"
      "{\n"
      "  Alpha(); Beta ();\n"
      "  if (Guard()) {\n"
      "    Nested();\n"
      "  }\n"
      "  Dup(); Dup();\n"
      "  Dup();\n"
      "}\n"
      "AFTER_CLOSE_SENTINEL();\n"
      "void Unrelated() { LATER_LEAK_SENTINEL(); }\n");
  qbrain::test_support::put_page(
      brain, "team_a", "n22/same-line",
      "void SameLineRoot() { SameOne(); SameTwo(); } AFTER_CLOSE_SENTINEL();\n");
  qbrain::test_support::put_page(
      brain, "team_a", "n22/prefix-brace",
      "namespace N { void PrefixBraceRoot() { PrefixBraceCall(); } void Later() { PREFIX_BRACE_LEAK(); } }\n");
  qbrain::test_support::put_page(
      brain, "team_a", "n22/control-keywords",
      "void KeywordRoot() { requires(CheckRequirement()); alignas(AlignValue()); "
      "asm(AsmValue()); await(AwaitValue()); yield(YieldValue()); function(FunctionValue()); "
      "RealKeywordCall(); }\n");
  qbrain::test_support::put_page(
      brain, "team_a", "n22/multiple",
      "void MultiRoot()\n"
      "{\n"
      "  FirstMulti();\n"
      "}\n"
      "void MultiRoot() { SecondMulti(); }\n"
      "void MultiRoot(int) { InlineFirst(); } void MultiRoot() { InlineSecond(); }\n"
      "void LaterDefinition() { LATER_LEAK_SENTINEL(); }\n");
  qbrain::test_support::put_page(
      brain, "team_a", "n22/recursive",
      "void RecursiveRoot() {\n  RecursiveRoot();\n}\n");

  std::string window = "void WindowRoot()\n";
  window.append(9, '\n');
  window += "{ BoundaryCall(); }\n";
  qbrain::test_support::put_page(brain, "team_a", "n22/window", window);
  std::string too_far = "void TooFarRoot()\n";
  too_far.append(10, '\n');
  too_far += "{ TooFarCall(); }\n";
  qbrain::test_support::put_page(brain, "team_a", "n22/too-far", too_far);
  qbrain::test_support::put_page(
      brain, "team_a", "n22/unbalanced",
      "void UnbalancedRoot() {\n"
      "  BeforeUnbalanced();\n"
      "void OtherDefinition() { LATER_LEAK_SENTINEL(); }\n");
  qbrain::test_support::put_page(
      brain, "team_a", "n22/reference-only",
      "auto pointer = RefOnlyRoot;\n"
      "void PrefixRootLonger() { LATER_LEAK_SENTINEL(); }\n");
  qbrain::test_support::put_page(
      brain, "team_a", "n22/prototype",
      "void PrototypeRoot();\n"
      "void PrototypeRoot()\n"
      "void UnrelatedPrototypeTarget() { PrototypeLeak(); }\n");

  std::string utf8_line = "\tEscapedCall();\t// \"quote\" C:\\path ";
  utf8_line.push_back(static_cast<char>(0xff));
  utf8_line.append(260, 'x');
  qbrain::test_support::put_page(
      brain, "team_a", "n22/utf8",
      "void UtfRoot() {\n" + utf8_line + "\n}\n");

  qbrain::test_support::put_page(
      brain, "team_a", "n22/flow-root", "void FlowRoot() { Alpha(); Beta(); }\n");
  qbrain::test_support::put_page(
      brain, "team_a", "n22/flow-alpha", "void Alpha() { Gamma(); }\n");
  qbrain::test_support::put_page(
      brain, "team_a", "n22/flow-beta", "void Beta() { Gamma(); FlowRoot(); }\n");
  qbrain::test_support::put_page(
      brain, "team_a", "n22/flow-gamma", "void Gamma() { Delta(); }\n");
  qbrain::test_support::put_page(
      brain, "team_a", "n22/flow-delta", "void Delta() {}\n");
  const auto deleted_flow = qbrain::test_support::put_page(
      brain, "team_a", "n22/flow-deleted",
      "void FlowRoot() { DELETED_FLOW_SENTINEL(); }\n");
  set_updated_at(brain, deleted_flow.id, "2199-01-01 00:00:00");
  QB_CHECK(brain.soft_delete(deleted_flow.slug, "team_a"));
  qbrain::test_support::put_page(
      brain, "default", "n22/flow-other-source",
      "void FlowRoot() { OTHER_SOURCE_FLOW_SENTINEL(); }\n");

  const auto blast_def = qbrain::test_support::put_page(
      brain, "team_a", "n22/blast-def",
      "void BlastRoot() {\n  BlastCallee();\n}\n");
  const auto blast_ref = qbrain::test_support::put_page(
      brain, "team_a", "n22/blast-ref", "auto reference = BlastRoot;\n");
  const auto blast_call = qbrain::test_support::put_page(
      brain, "team_a", "n22/blast-call", "BlastRoot();\n");
  set_updated_at(brain, blast_def.id, "2070-01-01 00:00:00");
  set_updated_at(brain, blast_ref.id, "2071-01-01 00:00:00");
  set_updated_at(brain, blast_call.id, "2072-01-01 00:00:00");
  const auto deleted_blast = qbrain::test_support::put_page(
      brain, "team_a", "n22/blast-deleted",
      "void BlastRoot() { DELETED_BLAST_SENTINEL(); }\n");
  set_updated_at(brain, deleted_blast.id, "2199-01-01 00:00:00");
  QB_CHECK(brain.soft_delete(deleted_blast.slug, "team_a"));
  qbrain::test_support::put_page(
      brain, "default", "n22/blast-other-source",
      "void BlastRoot() { OTHER_SOURCE_BLAST_SENTINEL(); }\n");

  const auto order_old = qbrain::test_support::put_page(
      brain, "team_a", "n22/order-old",
      "void OrderRoot() { OldOrderCall(); }\n");
  const auto order_new = qbrain::test_support::put_page(
      brain, "team_a", "n22/order-new",
      "void OrderRoot() { NewOrderCall(); }\n");
  set_updated_at(brain, order_old.id, "2090-01-01 00:00:00");
  set_updated_at(brain, order_new.id, "2090-01-01 00:00:00");
  const auto deleted = qbrain::test_support::put_page(
      brain, "team_a", "n22/order-deleted",
      "void OrderRoot() { DELETED_SENTINEL(); }\n");
  set_updated_at(brain, deleted.id, "2100-01-01 00:00:00");
  QB_CHECK(brain.soft_delete(deleted.slug, "team_a"));
  const auto other_source = qbrain::test_support::put_page(
      brain, "default", "n22/order-other-source",
      "void OrderRoot() { OTHER_SOURCE_SENTINEL(); }\n");
  set_updated_at(brain, other_source.id, "2101-01-01 00:00:00");

  qbrain::test_support::put_page(
      brain, "default", "n22/ambient-default",
      "void AmbientRoot() { DefaultAmbient(); }\n");
  qbrain::test_support::put_page(
      brain, "team_a", "n22/ambient-team",
      "void AmbientRoot() { TeamAmbient(); }\n");
  qbrain::test_support::put_page(
      brain, "team_b", "n22/unauthorized",
      "void BodyRoot() { UNAUTHORIZED_SENTINEL(); }\n");

  std::string fan_body = "void FanRoot() {\n";
  for (int index = 0; index < 205; ++index) {
    std::ostringstream name;
    name << "  Fan";
    name.width(3);
    name.fill('0');
    name << index << "();\n";
    fan_body += name.str();
  }
  fan_body += "}\n";
  qbrain::test_support::put_page(brain, "number_source", "n22/fan", fan_body);
}

void seed_decoy(qbrain::Brain& decoy) {
  QB_CHECK(decoy.ensure_source("team_a"));
  qbrain::test_support::put_page(
      decoy, "team_a", "n22/body",
      "void BodyRoot() { DECOY_BRAIN_SENTINEL(); }\n");
  qbrain::test_support::put_page(
      decoy, "default", "n22/ambient-default",
      "void AmbientRoot() { DECOY_BRAIN_SENTINEL(); }\n");
  qbrain::test_support::put_page(
      decoy, "team_a", "n22/flow-root",
      "void FlowRoot() { DECOY_FLOW_SENTINEL(); }\n");
  qbrain::test_support::put_page(
      decoy, "team_a", "n22/blast-def",
      "void BlastRoot() { DECOY_BLAST_SENTINEL(); }\n");
}

}  // namespace

void test_n22() {
  namespace fs = std::filesystem;
  const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  const auto root = fs::temp_directory_path() /
                    ("qbrain_n22_test_" + std::to_string(nonce));
  QB_CHECK(!fs::exists(root));
  fs::create_directories(root);
  ScopedEnvironmentVariable local_app_data(
      "LOCALAPPDATA", qbrain::util::path_to_utf8(root / "localappdata"));

  g_snapshot_evidence.clear();
  qbrain::ops::register_builtin_ops();
  qbrain::Brain brain("n22-selected");
  brain.open_at(qbrain::util::path_to_utf8(root / "selected.db"));
  qbrain::Brain decoy("n22-decoy");
  decoy.open_at(qbrain::util::path_to_utf8(root / "decoy.db"));
  SnapshotMatrix matrix(brain, decoy);

  matrix.run("schema:fresh-v12", [&] {
    const auto selected_integrity = qbrain::storage::check_schema_integrity(brain.db());
    const auto decoy_integrity = qbrain::storage::check_schema_integrity(decoy.db());
    QB_CHECK(selected_integrity.ok && selected_integrity.schema_version == 12);
    QB_CHECK(decoy_integrity.ok && decoy_integrity.schema_version == 12);
    return true;
  });

  seed_core_fixtures(brain);
  seed_decoy(decoy);
  const auto selected_before_reopen = logical_snapshot(brain);
  const auto decoy_before_reopen = logical_snapshot(decoy);
  brain.close();
  decoy.close();
  brain.open_at(qbrain::util::path_to_utf8(root / "selected.db"));
  decoy.open_at(qbrain::util::path_to_utf8(root / "decoy.db"));
  QB_CHECK(logical_snapshot(brain) == selected_before_reopen);
  QB_CHECK(logical_snapshot(decoy) == decoy_before_reopen);

  matrix.run("schema:populated-reopen-v12", [&] {
    const auto selected_integrity = qbrain::storage::check_schema_integrity(brain.db());
    const auto decoy_integrity = qbrain::storage::check_schema_integrity(decoy.db());
    QB_CHECK(selected_integrity.ok && selected_integrity.schema_version == 12);
    QB_CHECK(decoy_integrity.ok && decoy_integrity.schema_version == 12);
    return true;
  });

  exercise_callee_contract(brain, matrix);
  exercise_flow_contract(brain, matrix);
  exercise_blast_contract(brain, matrix);
  exercise_symbol_and_alias_contract(brain, matrix);
  exercise_numeric_contract(brain, matrix);
  exercise_source_contract(brain, matrix);
  exercise_registry_contract(brain, matrix);
  exercise_mcp_contract(brain, matrix);
  exercise_damaged_database_contract(root / "damaged.db", decoy);
  exercise_cache_contract(brain, matrix, root);
  exercise_page_limit_contract(brain, matrix);
  exercise_resource_bound_contract(brain, matrix);

  matrix.run("schema:final-v12", [&] {
    const auto selected_integrity = qbrain::storage::check_schema_integrity(brain.db());
    const auto decoy_integrity = qbrain::storage::check_schema_integrity(decoy.db());
    QB_CHECK(selected_integrity.ok && selected_integrity.schema_version == 12);
    QB_CHECK(decoy_integrity.ok && decoy_integrity.schema_version == 12);
    return true;
  });

  const auto selected_snapshot = logical_snapshot(brain);
  const auto decoy_snapshot = logical_snapshot(decoy);
  QB_CHECK(!g_snapshot_evidence.empty());
  std::cout << "[INFO] n22 schema_v12=pass callee_body_matrix=pass "
               "flow_bfs_matrix=pass blast_matrix=pass symbol_alias_matrix=pass "
               "numeric_matrix=pass source_scope=pass active_page_ordering=pass "
               "remote_authorization=pass ambient_default=pass selected_decoy=pass "
               "registry_schema=pass tools_list=pass mcp_type_validation=pass "
                "cache_clear=pass stateless=pass disclosure_bounds=pass "
                "resource_bounds=pass "
               "deterministic=pass read_only=pass snapshot_call_count="
            << g_snapshot_evidence.size() << " selected_snapshot_sha256="
            << snapshot_sha256(selected_snapshot) << " decoy_snapshot_sha256="
            << snapshot_sha256(decoy_snapshot) << '\n';
  for (const auto& evidence : g_snapshot_evidence) {
    std::cout << "[INFO] n22 snapshot_call=" << evidence.index
              << " label=" << evidence.label
              << " selected_before_sha256=" << evidence.selected_before
              << " selected_after_sha256=" << evidence.selected_after
              << " decoy_before_sha256=" << evidence.decoy_before
              << " decoy_after_sha256=" << evidence.decoy_after << '\n';
  }

  decoy.close();
  brain.close();
  fs::remove_all(root);
}
