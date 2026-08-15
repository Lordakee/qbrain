#include "qbrain/core/brain.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/storage/database.hpp"
#include "qbrain/util/paths.hpp"
#include "qbrain/util/time_util.hpp"
#include "wave3_test_support.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
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
  int index = 0;
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

  template <typename Fn>
  auto run(const std::string& label, Fn&& fn) {
    const auto selected_before = logical_snapshot(selected_);
    const auto decoy_before = logical_snapshot(decoy_);
    auto result = fn();
    const auto selected_after = logical_snapshot(selected_);
    const auto decoy_after = logical_snapshot(decoy_);
    if (selected_before != selected_after || decoy_before != decoy_after) {
      throw std::runtime_error("N19 read mutated database state: " + label);
    }
    g_snapshot_evidence.push_back(
        {static_cast<int>(g_snapshot_evidence.size() + 1), label,
         snapshot_sha256(selected_before), snapshot_sha256(selected_after),
         snapshot_sha256(decoy_before), snapshot_sha256(decoy_after)});
    return result;
  }

 private:
  qbrain::Brain& selected_;
  qbrain::Brain& decoy_;
};

class DatabaseReadObserver {
 public:
  explicit DatabaseReadObserver(qbrain::Brain& brain) : database_(brain.db().handle()) {
    QB_CHECK(sqlite3_set_authorizer(database_, &DatabaseReadObserver::authorize, this) ==
             SQLITE_OK);
  }

  ~DatabaseReadObserver() { sqlite3_set_authorizer(database_, nullptr, nullptr); }

  int application_reads() const { return application_reads_; }
  int data_reads() const { return data_reads_; }

 private:
  static int authorize(void* context, int action, const char* table, const char*,
                       const char*, const char*) {
    if (action == SQLITE_READ && table && !std::string_view(table).starts_with("sqlite_")) {
      auto* observer = static_cast<DatabaseReadObserver*>(context);
      ++observer->application_reads_;
      const std::string_view table_name(table);
      if (table_name == "pages" || table_name == "content_chunks" ||
          table_name == "links" || table_name.starts_with("pages_fts")) {
        ++observer->data_reads_;
      }
    }
    return SQLITE_OK;
  }

  sqlite3* database_ = nullptr;
  int application_reads_ = 0;
  int data_reads_ = 0;
};

qbrain::Page put_page(qbrain::Brain& brain, const std::string& source_id,
                      const std::string& slug, const std::string& title,
                      const std::string& body, const std::string& type = "note") {
  qbrain::PageInput input;
  input.source_id = source_id;
  input.slug = slug;
  input.title = title;
  input.body = body;
  input.type = type;
  return brain.put_page(input);
}

void set_times(qbrain::Brain& brain, int64_t page_id, const std::string& created_at,
               const std::string& updated_at) {
  auto statement = brain.db().prepare(
      "UPDATE pages SET created_at=?, updated_at=? WHERE id=?");
  statement.bind_text(1, created_at);
  statement.bind_text(2, updated_at);
  statement.bind_int(3, page_id);
  statement.step_done();
  QB_CHECK(brain.db().changes() == 1);
}

void set_config_direct(qbrain::Brain& brain, const std::string& key,
                       const std::string& value) {
  auto statement = brain.db().prepare(
      "INSERT INTO config(key,value) VALUES(?,?) "
      "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  statement.bind_text(1, key);
  statement.bind_text(2, value);
  statement.step_done();
}

void add_link(qbrain::Brain& brain, const std::string& source_id,
              const std::string& from, const std::string& to,
              const std::string& link_source = "n19-test") {
  qbrain::Link link;
  link.source_id = source_id;
  link.from_slug = from;
  link.to_slug = to;
  link.link_type = "related";
  link.link_source = link_source;
  brain.add_link(link);
}

int64_t scalar_for_source(qbrain::Brain& brain, const std::string& sql,
                          const std::string& source_id) {
  auto statement = brain.db().prepare(sql);
  statement.bind_text(1, source_id);
  return statement.step() ? statement.column_int(0) : 0;
}

void require_keys(const json& object, std::initializer_list<std::string_view> expected);

struct ExactIdentityExpected {
  int schema_version = 0;
  int64_t pages = 0;
  int64_t chunks = 0;
  int64_t embedded_chunks = 0;
  int64_t links = 0;
};

ExactIdentityExpected exact_identity_expected(qbrain::Brain& brain,
                                              const std::string& source_id) {
  const auto integrity = qbrain::storage::check_schema_integrity(brain.db());
  QB_CHECK(integrity.ok);

  ExactIdentityExpected expected;
  expected.schema_version = integrity.schema_version;
  expected.pages = scalar_for_source(
      brain, "SELECT COUNT(*) FROM pages WHERE source_id=? AND deleted_at IS NULL",
      source_id);
  expected.chunks = scalar_for_source(
      brain,
      "SELECT COUNT(*) FROM content_chunks c JOIN pages p ON p.id=c.page_id "
      "WHERE p.source_id=?",
      source_id);
  expected.embedded_chunks = scalar_for_source(
      brain,
      "SELECT COUNT(*) FROM content_chunks c JOIN pages p ON p.id=c.page_id "
      "WHERE p.source_id=? AND c.embedding IS NOT NULL",
      source_id);
  expected.links = scalar_for_source(
      brain, "SELECT COUNT(*) FROM links WHERE source_id=?", source_id);
  return expected;
}

void require_exact_local_identity(const qbrain::ops::OpResult& result,
                                  const qbrain::Brain& brain,
                                  const std::string& expected_brain_id,
                                  const std::string& expected_source_id,
                                  const ExactIdentityExpected& expected) {
  QB_CHECK(result.ok);
  const auto identity = json::parse(result.json);
  require_keys(identity, {"brain_id", "source_id", "schema_version", "pages", "chunks",
                          "links", "embedded_chunks", "db_path"});
  QB_CHECK(identity["brain_id"] == expected_brain_id);
  QB_CHECK(identity["source_id"] == expected_source_id);
  QB_CHECK(identity["schema_version"].get<int>() == expected.schema_version);
  QB_CHECK(identity["pages"].get<int64_t>() == expected.pages);
  QB_CHECK(identity["chunks"].get<int64_t>() == expected.chunks);
  QB_CHECK(identity["embedded_chunks"].get<int64_t>() == expected.embedded_chunks);
  QB_CHECK(identity["links"].get<int64_t>() == expected.links);
  QB_CHECK(identity["db_path"] == brain.db_path());
}

std::string format_sql_utc(std::chrono::system_clock::time_point time) {
  const auto raw = std::chrono::system_clock::to_time_t(time);
  std::tm tm{};
  gmtime_s(&tm, &raw);
  char buffer[32]{};
  const auto written = std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
  QB_CHECK(written == 19);
  return buffer;
}

std::chrono::system_clock::time_point utc_point(int year, unsigned month, unsigned day,
                                                int hour = 0) {
  using namespace std::chrono;
  return sys_days{std::chrono::year{year} / std::chrono::month{month} /
                  std::chrono::day{day}} +
         hours{hour};
}

void require_keys(const json& object, std::initializer_list<std::string_view> expected) {
  QB_CHECK(object.is_object());
  std::set<std::string> actual;
  for (auto it = object.begin(); it != object.end(); ++it) actual.insert(it.key());
  std::set<std::string> wanted;
  for (auto key : expected) wanted.emplace(key);
  QB_CHECK(actual == wanted);
}

void require_rows_from_source(const json& rows, const std::string& source_id) {
  QB_CHECK(rows.is_array());
  for (const auto& row : rows) QB_CHECK(row.value("source_id", "") == source_id);
}

void require_bounded_json_string(const std::string& value, std::size_t maximum) {
  QB_CHECK(value.size() <= maximum);
  QB_CHECK(!json(value).dump().empty());
}

json require_operation_error(const qbrain::ops::OpResult& result,
                             const std::string& code, const std::string& field,
                             const std::string& forbidden = {}) {
  QB_CHECK(!result.ok);
  QB_CHECK(result.exit_code != 0);
  QB_CHECK(result.json.size() <= 2048);
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
              const std::string& operation_name, const json& arguments, int request_id) {
  const json request = {{"jsonrpc", "2.0"},
                        {"id", request_id},
                        {"method", "tools/call"},
                        {"params", {{"name", operation_name}, {"arguments", arguments}}}};
  return json::parse(qbrain::mcp::handle_rpc_body(brain, options, request.dump()));
}

json structured_mcp_content(const json& response) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(response["result"].contains("content"));
  const auto& content = response["result"]["content"];
  for (auto it = content.rbegin(); it != content.rend(); ++it) {
    if (!it->is_object() || it->value("type", "") != "text") continue;
    try {
      return json::parse(it->value("text", ""));
    } catch (...) {
    }
  }
  throw std::runtime_error("MCP response did not contain structured JSON");
}

json require_mcp_error(const json& response, const std::string& code,
                       const std::string& field) {
  QB_CHECK(response["result"]["isError"] == true);
  const auto payload = structured_mcp_content(response);
  require_keys(payload, {"error"});
  QB_CHECK(payload["error"]["code"] == code);
  QB_CHECK(payload["error"]["field"] == field);
  return payload["error"];
}

const json& find_tool(const json& tools, const std::string& name) {
  for (const auto& tool : tools) {
    if (tool.value("name", "") == name) return tool;
  }
  throw std::runtime_error("missing tool registration: " + name);
}

std::vector<std::string> row_slugs(const json& rows) {
  std::vector<std::string> slugs;
  for (const auto& row : rows) slugs.push_back(row.value("slug", ""));
  return slugs;
}

void seed_search_fixture(qbrain::Brain& brain, bool reverse_order) {
  brain.ensure_source("search_src");
  std::vector<std::string> slugs = {"tie/a", "tie/b", "tie/c",
                                    "tie/d", "tie/e", "tie/f"};
  if (reverse_order) std::reverse(slugs.begin(), slugs.end());
  const std::string body =
      "needle alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu "
      "nu xi omicron BODY_SECRET_SHOULD_NOT_BE_RETURNED";
  for (const auto& slug : slugs) put_page(brain, "search_src", slug, "Neutral", body);
}

void prepare_search_fallback_fixture(qbrain::Brain& brain, bool reverse_order) {
  seed_search_fixture(brain, reverse_order);
  brain.db().exec(
      "UPDATE pages SET updated_at='2025-05-01 00:00:00' "
      "WHERE source_id='search_src';"
      "DROP TABLE pages_fts;");
}

void exercise_utc_helper() {
  QB_CHECK(qbrain::util::utc_seven_day_boundary(utc_point(2026, 8, 4, 12)) ==
           "2026-07-29T00:00:00Z");
  QB_CHECK(qbrain::util::utc_seven_day_boundary(utc_point(2026, 3, 2, 23)) ==
           "2026-02-24T00:00:00Z");
  QB_CHECK(qbrain::util::utc_seven_day_boundary(utc_point(2026, 1, 3, 1)) ==
           "2025-12-28T00:00:00Z");
  QB_CHECK(qbrain::util::utc_seven_day_boundary(utc_point(2024, 3, 1, 8)) ==
           "2024-02-24T00:00:00Z");
  const auto live = qbrain::util::utc_seven_day_boundary();
  QB_CHECK(live.size() == 20 && live[4] == '-' && live[7] == '-' && live[10] == 'T');
  QB_CHECK(live.ends_with("T00:00:00Z"));
}

void exercise_source_contract(qbrain::Brain& selected, qbrain::Brain& decoy,
                              SnapshotMatrix& matrix) {
  const std::array<std::string, 4> operations = {
      "get_brain_identity", "volunteer_context", "get_timeline", "volunteer_chronicle"};
  const auto sources_before = qbrain::test_support::scalar(selected, "SELECT COUNT(*) FROM sources");

  for (const auto& operation : operations) {
    auto omitted = matrix.run(operation + ":source:omitted", [&] {
      return qbrain::test_support::call_op(selected, operation);
    });
    QB_CHECK(omitted.ok);

    auto mixed_case = matrix.run(operation + ":source:mixed-case", [&] {
      return qbrain::test_support::call_op(selected, operation, {{"source_id", "TeAm_A"}});
    });
    QB_CHECK(mixed_case.ok);

    auto unexpected = matrix.run(operation + ":argument:unexpected", [&] {
      return qbrain::test_support::call_op(selected, operation, {{"unexpected", "value"}});
    });
    require_operation_error(unexpected, "invalid_argument", "unexpected", "value");

    const std::vector<std::pair<std::string, std::string>> invalid_sources = {
        {"", "invalid_source"},
        {"bad/source", "invalid_source"},
        {"CON", "invalid_source"},
        {std::string(65, 's'), "invalid_source"},
        {"unknown_n19", "source_not_found"},
    };
    for (const auto& [source, code] : invalid_sources) {
      auto rejected = matrix.run(operation + ":source:reject", [&] {
        return qbrain::test_support::call_op(selected, operation, {{"source_id", source}});
      });
      require_operation_error(rejected, code, "source_id", source);
    }

    auto remote_default = matrix.run(operation + ":source:remote-default", [&] {
      return qbrain::test_support::call_op(selected, operation, {}, true, false);
    });
    QB_CHECK(remote_default.ok);

    auto remote_denied = matrix.run(operation + ":source:remote-denied", [&] {
      DatabaseReadObserver observer(selected);
      auto result = qbrain::test_support::call_op(selected, operation,
                                                  {{"source_id", "team_a"}}, true, false);
      QB_CHECK(observer.data_reads() == 0);
      return result;
    });
    require_operation_error(remote_denied, "source_not_allowed", "source_id");

    auto write_does_not_authorize = matrix.run(operation + ":source:allow-write-denied", [&] {
      DatabaseReadObserver observer(selected);
      auto result = qbrain::test_support::call_op(selected, operation,
                                                  {{"source_id", "recent_src"}}, true, true);
      QB_CHECK(observer.data_reads() == 0);
      return result;
    });
    require_operation_error(write_does_not_authorize, "source_not_allowed", "source_id");
  }

  QB_CHECK(qbrain::test_support::scalar(selected, "SELECT COUNT(*) FROM sources") ==
           sources_before);
  QB_CHECK(logical_snapshot(decoy) == logical_snapshot(decoy));

  set_config_direct(selected, "mcp.allowed_sources", "TeAm_A,UnKnOwN_N19,empty");
  for (const auto& operation : operations) {
    auto remote_allowed = matrix.run(operation + ":source:remote-allowed", [&] {
      return qbrain::test_support::call_op(selected, operation,
                                           {{"source_id", "TEAM_A"}}, true, false);
    });
    QB_CHECK(remote_allowed.ok);
  }
}

void exercise_identity_contract(qbrain::Brain& selected, qbrain::Brain& decoy,
                                SnapshotMatrix& matrix, const std::filesystem::path& root) {
  const auto selected_default_expected = exact_identity_expected(selected, "default");
  auto selected_default = matrix.run("identity:matrix:selected:default", [&] {
    return qbrain::test_support::call_op(selected, "get_brain_identity");
  });
  require_exact_local_identity(selected_default, selected, "n19-selected", "default",
                               selected_default_expected);
  QB_CHECK(json::parse(selected_default.json)["db_path"] ==
           qbrain::util::path_to_utf8(root / "selected.db"));

  const auto selected_team_expected = exact_identity_expected(selected, "team_a");
  auto selected_team = matrix.run("identity:matrix:selected:team", [&] {
    return qbrain::test_support::call_op(selected, "get_brain_identity",
                                         {{"source_id", "TEAM_A"}});
  });
  require_exact_local_identity(selected_team, selected, "n19-selected", "team_a",
                               selected_team_expected);

  const auto decoy_default_expected = exact_identity_expected(decoy, "default");
  auto decoy_default = matrix.run("identity:matrix:decoy:default", [&] {
    return qbrain::test_support::call_op(decoy, "get_brain_identity");
  });
  require_exact_local_identity(decoy_default, decoy, "n19-decoy", "default",
                               decoy_default_expected);

  const auto decoy_team_expected = exact_identity_expected(decoy, "team_a");
  auto decoy_team = matrix.run("identity:matrix:decoy:team", [&] {
    return qbrain::test_support::call_op(decoy, "get_brain_identity",
                                         {{"source_id", "TEAM_A"}});
  });
  require_exact_local_identity(decoy_team, decoy, "n19-decoy", "team_a",
                               decoy_team_expected);

  auto remote = matrix.run("identity:remote-redaction", [&] {
    return qbrain::test_support::call_op(selected, "get_brain_identity", {}, true, false);
  });
  QB_CHECK(remote.ok);
  const auto remote_identity = json::parse(remote.json);
  require_keys(remote_identity, {"brain_id", "source_id", "schema_version", "pages", "chunks",
                                 "links", "embedded_chunks"});
  QB_CHECK(!remote_identity.contains("db_path"));
  const std::vector<std::string> path_sentinels = {
      selected.db_path(), qbrain::util::path_to_utf8(root), "C:\\Users\\N19_SECRET_USER",
      "\\\\n19-server\\private", "\\\\?\\Volume{N19-SECRET}", "selected.db",
      "qbrain_n19_test", "N19_SECRET_USER", "n19-server", "Volume{N19-SECRET}",
      "N19_DECOY_PATH"};
  for (const auto& sentinel : path_sentinels) {
    QB_CHECK(remote.json.find(sentinel) == std::string::npos);
    QB_CHECK(remote.text.find(sentinel) == std::string::npos);
  }
}

void exercise_limit_rejections(qbrain::Brain& selected, SnapshotMatrix& matrix,
                               const std::string& operation, int maximum,
                               std::unordered_map<std::string, std::string> base = {}) {
  const std::vector<std::string> invalid = {
      "", "-1", "+1", " 1", "1 ", "1.0", "1x", "18446744073709551616"};
  for (const auto& value : invalid) {
    auto args = base;
    args["limit"] = value;
    auto rejected = matrix.run(operation + ":limit:reject", [&] {
      return qbrain::test_support::call_op(selected, operation, args);
    });
    require_operation_error(rejected, "invalid_argument", "limit", value);
  }

  auto zero_args = base;
  zero_args["limit"] = "0";
  auto one_args = base;
  one_args["limit"] = "1";
  auto zero = matrix.run(operation + ":limit:zero", [&] {
    return qbrain::test_support::call_op(selected, operation, zero_args);
  });
  auto one = matrix.run(operation + ":limit:one", [&] {
    return qbrain::test_support::call_op(selected, operation, one_args);
  });
  QB_CHECK(zero.ok && one.ok && zero.json == one.json);

  auto max_args = base;
  max_args["limit"] = std::to_string(maximum);
  auto over_args = base;
  over_args["limit"] = std::to_string(maximum + 1000);
  auto at_max = matrix.run(operation + ":limit:max", [&] {
    return qbrain::test_support::call_op(selected, operation, max_args);
  });
  auto over_max = matrix.run(operation + ":limit:over-max", [&] {
    return qbrain::test_support::call_op(selected, operation, over_args);
  });
  QB_CHECK(at_max.ok && over_max.ok && at_max.json == over_max.json);
}

void exercise_context_contract(qbrain::Brain& selected, qbrain::Brain& decoy,
                               qbrain::Brain& reverse_fixture, SnapshotMatrix& matrix,
                               SnapshotMatrix& reverse_matrix) {
  auto query_result = matrix.run("context:query", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context",
        {{"source_id", "search_src"}, {"query", "needle"}, {"limit", "2"}});
  });
  QB_CHECK(query_result.ok);
  const auto rows = json::parse(query_result.json);
  QB_CHECK(rows.size() == 2);
  QB_CHECK(row_slugs(rows) == std::vector<std::string>({"tie/a", "tie/b"}));
  require_rows_from_source(rows, "search_src");
  for (std::size_t index = 0; index < rows.size(); ++index) {
    require_keys(rows[index], {"source_id", "slug", "title", "snippet", "score"});
    require_bounded_json_string(rows[index]["title"].get<std::string>(), 512);
    require_bounded_json_string(rows[index]["snippet"].get<std::string>(), 512);
    QB_CHECK(rows[index]["snippet"].get<std::string>().find("BODY_SECRET_SHOULD_NOT_BE_RETURNED") ==
             std::string::npos);
    if (index > 0) {
      const double previous = rows[index - 1]["score"].get<double>();
      const double current = rows[index]["score"].get<double>();
      QB_CHECK(previous >= current);
      if (previous == current) QB_CHECK(rows[index - 1]["slug"] < rows[index]["slug"]);
    }
  }
  QB_CHECK(query_result.json.find("DECOY_SEARCH_SENTINEL") == std::string::npos);

  auto repeated = matrix.run("context:query-repeat", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context",
        {{"source_id", "search_src"}, {"query", "needle"}, {"limit", "2"}});
  });
  QB_CHECK(repeated.json == query_result.json && repeated.text == query_result.text);

  auto reverse = reverse_matrix.run("context:reverse-fixture", [&] {
    return qbrain::test_support::call_op(
        reverse_fixture, "volunteer_context",
        {{"source_id", "search_src"}, {"query", "needle"}, {"limit", "2"}});
  });
  QB_CHECK(reverse.ok && reverse.json == query_result.json && reverse.text == query_result.text);

  auto canonical_alias = matrix.run("context:query-alias", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context",
        {{"source_id", "search_src"}, {"q", "needle"}, {"limit", "2"}});
  });
  auto equal_aliases = matrix.run("context:equal-aliases", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context",
        {{"source_id", "search_src"}, {"query", "needle"}, {"q", "needle"},
         {"limit", "2"}});
  });
  auto empty_canonical_alias = matrix.run("context:empty-query-alias", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context",
        {{"source_id", "search_src"}, {"query", ""}, {"q", "needle"}, {"limit", "2"}});
  });
  QB_CHECK(canonical_alias.json == query_result.json);
  QB_CHECK(equal_aliases.json == query_result.json);
  QB_CHECK(empty_canonical_alias.json == query_result.json);

  auto conflict = matrix.run("context:alias-conflict", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context",
        {{"source_id", "search_src"}, {"query", "needle"}, {"q", "different"}});
  });
  require_operation_error(conflict, "invalid_argument", "query", "different");

  auto exact_max_query = matrix.run("context:query-4096", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context",
        {{"source_id", "search_src"}, {"query", std::string(4096, 'n')}});
  });
  QB_CHECK(exact_max_query.ok);
  const std::string oversized_query(4097, 'x');
  auto oversized = matrix.run("context:query-4097", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context",
        {{"source_id", "search_src"}, {"query", oversized_query}});
  });
  require_operation_error(oversized, "invalid_argument", "query", oversized_query);
  std::string malformed_query = "needle";
  malformed_query.push_back(static_cast<char>(0xff));
  auto malformed = matrix.run("context:query-malformed-utf8", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context",
        {{"source_id", "search_src"}, {"query", malformed_query}});
  });
  require_operation_error(malformed, "invalid_argument", "query", malformed_query);

  auto recent = matrix.run("context:recent", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context", {{"source_id", "recent_src"}, {"limit", "2"}});
  });
  auto empty_query = matrix.run("context:recent-empty", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context",
        {{"source_id", "recent_src"}, {"query", ""}, {"limit", "2"}});
  });
  auto whitespace_query = matrix.run("context:recent-whitespace", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context",
        {{"source_id", "recent_src"}, {"query", " \t\r\n"}, {"limit", "2"}});
  });
  QB_CHECK(recent.ok && recent.json == empty_query.json && recent.json == whitespace_query.json);
  const auto recent_rows = json::parse(recent.json);
  QB_CHECK(row_slugs(recent_rows) ==
           std::vector<std::string>({"recent/b", "recent/a"}));
  require_rows_from_source(recent_rows, "recent_src");
  for (const auto& row : recent_rows) {
    require_keys(row, {"source_id", "slug", "title", "type", "updated_at"});
  }

  auto utf8_recent = matrix.run("context:recent-utf8", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context", {{"source_id", "utf8_src"}, {"limit", "1"}});
  });
  const auto utf8_title = json::parse(utf8_recent.json)[0]["title"].get<std::string>();
  require_bounded_json_string(utf8_title, 512);
  QB_CHECK(utf8_title.find(std::string("\xEF\xBF\xBD", 3)) != std::string::npos);
  QB_CHECK(utf8_title.find("truncated") != std::string::npos);

  auto utf8_query = matrix.run("context:query-utf8-snippet", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_context",
        {{"source_id", "utf8_search"}, {"query", "needle"}, {"limit", "1"}});
  });
  const auto utf8_snippet = json::parse(utf8_query.json)[0]["snippet"].get<std::string>();
  require_bounded_json_string(utf8_snippet, 512);
  QB_CHECK(utf8_snippet.find(std::string("\xEF\xBF\xBD", 3)) != std::string::npos);

  exercise_limit_rejections(selected, matrix, "volunteer_context", 50,
                            {{"source_id", "search_src"}, {"query", "needle"}});
  QB_CHECK(logical_snapshot(decoy).find("DECOY_SEARCH_SENTINEL") != std::string::npos);
}

void exercise_context_fallback_contract(qbrain::Brain& fallback_fixture,
                                        qbrain::Brain& reverse_fallback_fixture,
                                        qbrain::Brain& decoy) {
  SnapshotMatrix fallback_matrix(fallback_fixture, decoy);
  SnapshotMatrix reverse_matrix(reverse_fallback_fixture, decoy);
  const auto invoke = [](qbrain::Brain& brain) {
    return qbrain::test_support::call_op(
        brain, "volunteer_context",
        {{"source_id", "search_src"}, {"query", "needle"}, {"limit", "2"}});
  };

  auto first = fallback_matrix.run("context:fallback", [&] {
    return invoke(fallback_fixture);
  });
  auto repeated = fallback_matrix.run("context:fallback-repeat", [&] {
    return invoke(fallback_fixture);
  });
  auto reverse = reverse_matrix.run("context:fallback-reverse", [&] {
    return invoke(reverse_fallback_fixture);
  });
  QB_CHECK(first.ok && repeated.ok && reverse.ok);
  QB_CHECK(first.json == repeated.json && first.text == repeated.text);
  QB_CHECK(first.json == reverse.json && first.text == reverse.text);
  QB_CHECK(row_slugs(json::parse(first.json)) ==
           std::vector<std::string>({"tie/a", "tie/b"}));
}

void exercise_timeline_contract(qbrain::Brain& selected, SnapshotMatrix& matrix) {
  auto timeline = matrix.run("timeline:default", [&] {
    return qbrain::test_support::call_op(selected, "get_timeline", {{"limit", "2"}});
  });
  QB_CHECK(timeline.ok);
  const auto rows = json::parse(timeline.json);
  QB_CHECK(row_slugs(rows) ==
           std::vector<std::string>({"timeline/c", "timeline/b"}));
  require_rows_from_source(rows, "default");
  for (const auto& row : rows) {
    require_keys(row, {"source_id", "slug", "type", "title", "created_at", "updated_at",
                       "effective_at"});
    QB_CHECK(row["type"] == "timeline");
    QB_CHECK(row.dump().find("TIMELINE_BODY_SECRET") == std::string::npos);
  }
  QB_CHECK(rows[0]["effective_at"] == "2025-03-03 00:00:00");
  QB_CHECK(rows[1]["effective_at"] == "2025-03-03 00:00:00");

  auto repeated = matrix.run("timeline:repeat", [&] {
    return qbrain::test_support::call_op(selected, "get_timeline", {{"limit", "2"}});
  });
  QB_CHECK(repeated.json == timeline.json && repeated.text == timeline.text);

  auto team = matrix.run("timeline:team", [&] {
    return qbrain::test_support::call_op(selected, "get_timeline",
                                         {{"source_id", "TEAM_A"}, {"limit", "10"}});
  });
  const auto team_rows = json::parse(team.json);
  QB_CHECK(team_rows.size() == 1);
  QB_CHECK(team_rows[0]["slug"] == "team/timeline-newer");
  require_rows_from_source(team_rows, "team_a");

  exercise_limit_rejections(selected, matrix, "get_timeline", 200);
}

void exercise_chronicle_contract(qbrain::Brain& selected, SnapshotMatrix& matrix,
                                 const std::string& live_boundary_slug,
                                 const std::string& live_before_slug,
                                 const std::string& live_old_slug) {
  auto date_only = matrix.run("chronicle:date-only", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_chronicle",
        {{"source_id", "chron_src"}, {"since", "2024-02-29"}, {"limit", "20"}});
  });
  auto timestamp_t = matrix.run("chronicle:timestamp-t", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_chronicle",
        {{"source_id", "chron_src"}, {"since", "2024-02-29T00:00:00Z"},
         {"limit", "20"}});
  });
  auto timestamp_space = matrix.run("chronicle:timestamp-space", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_chronicle",
        {{"source_id", "chron_src"}, {"since", "2024-02-29 00:00:00Z"},
         {"limit", "20"}});
  });
  QB_CHECK(date_only.ok && date_only.json == timestamp_t.json &&
           date_only.json == timestamp_space.json);
  const auto rows = json::parse(date_only.json);
  QB_CHECK(row_slugs(rows) == std::vector<std::string>(
                                   {"chron/tie-b", "chron/tie-a", "chron/updated-at",
                                    "chron/created-at"}));
  require_rows_from_source(rows, "chron_src");
  for (const auto& row : rows) {
    require_keys(row, {"source_id", "slug", "title", "created_at", "updated_at",
                       "effective_at", "type"});
  }

  const std::vector<std::string> invalid_since = {
      "",          "2023-02-29",          "2024-02-30",
      "2024-13-01", "2024-00-01",          "2024-01-01T24:00:00Z",
      "2024-01-01T23:60:00Z", "2024-01-01T23:59:60Z",
      "2024-01-01T00:00:00+00:00", "2024-01-01T00:00:00z",
      "2024-01",   "2024-01-01 trailing"};
  for (const auto& since : invalid_since) {
    auto rejected = matrix.run("chronicle:since-reject", [&] {
      return qbrain::test_support::call_op(
          selected, "volunteer_chronicle", {{"source_id", "chron_src"}, {"since", since}});
    });
    require_operation_error(rejected, "invalid_argument", "since", since);
  }
  std::string malformed_since = "2024-01-01T00:00:00Z";
  malformed_since.push_back(static_cast<char>(0xff));
  auto malformed = matrix.run("chronicle:since-malformed-utf8", [&] {
    return qbrain::test_support::call_op(
        selected, "volunteer_chronicle",
        {{"source_id", "chron_src"}, {"since", malformed_since}});
  });
  require_operation_error(malformed, "invalid_argument", "since", malformed_since);

  auto live_window = matrix.run("chronicle:default-seven-days", [&] {
    return qbrain::test_support::call_op(selected, "volunteer_chronicle",
                                         {{"source_id", "default"}, {"limit", "200"}});
  });
  const auto live_rows = json::parse(live_window.json);
  const auto live_slugs = row_slugs(live_rows);
  QB_CHECK(std::find(live_slugs.begin(), live_slugs.end(), live_boundary_slug) !=
           live_slugs.end());
  QB_CHECK(std::find(live_slugs.begin(), live_slugs.end(), live_before_slug) ==
           live_slugs.end());
  QB_CHECK(std::find(live_slugs.begin(), live_slugs.end(), live_old_slug) ==
           live_slugs.end());

  auto empty = matrix.run("chronicle:empty-window", [&] {
    return qbrain::test_support::call_op(selected, "volunteer_chronicle",
                                         {{"source_id", "empty"}});
  });
  QB_CHECK(empty.ok && json::parse(empty.json).empty());

  exercise_limit_rejections(selected, matrix, "volunteer_chronicle", 200,
                            {{"source_id", "chron_src"}, {"since", "2024-02-29"}});
}

void exercise_registry_contract(qbrain::Brain& selected, SnapshotMatrix& matrix) {
  struct ExpectedSchema {
    std::string name;
    std::set<std::string> properties;
    std::optional<int> default_limit;
    std::optional<int> maximum_limit;
  };
  const std::vector<ExpectedSchema> expected = {
      {"get_brain_identity", {"source_id"}, std::nullopt, std::nullopt},
      {"volunteer_context", {"source_id", "query", "q", "limit"}, 8, 50},
      {"get_timeline", {"source_id", "limit"}, 50, 200},
      {"volunteer_chronicle", {"source_id", "since", "limit"}, 50, 200},
  };

  auto tools_response = matrix.run("registry:tools-list", [&] {
    return json::parse(qbrain::mcp::handle_rpc_body(
        selected, {}, R"({"jsonrpc":"2.0","id":190,"method":"tools/list","params":{}})"));
  });
  const auto& tools = tools_response["result"]["tools"];

  for (const auto& item : expected) {
    const auto* operation = qbrain::ops::global_registry().find(item.name);
    QB_CHECK(operation != nullptr);
    QB_CHECK(operation->scope == qbrain::ops::Scope::Read);
    QB_CHECK(!operation->local_only);
    QB_CHECK(!operation->description.empty());
    const auto schema = json::parse(operation->input_schema_json);
    QB_CHECK(schema["type"] == "object");
    QB_CHECK(schema["additionalProperties"] == false);
    std::set<std::string> properties;
    for (auto it = schema["properties"].begin(); it != schema["properties"].end(); ++it) {
      properties.insert(it.key());
    }
    QB_CHECK(properties == item.properties);
    QB_CHECK(schema["properties"]["source_id"]["type"] == "string");
    QB_CHECK(schema["properties"]["source_id"]["default"] == "default");
    if (item.default_limit) {
      const auto& limit = schema["properties"]["limit"];
      QB_CHECK(limit["type"] == "integer");
      QB_CHECK(limit["minimum"] == 0);
      QB_CHECK(limit["maximum"] == *item.maximum_limit);
      QB_CHECK(limit["default"] == *item.default_limit);
    }
    if (item.name == "volunteer_context") {
      QB_CHECK(schema["properties"]["query"]["type"] == "string");
      QB_CHECK(schema["properties"]["query"]["maxLength"] == 4096);
      QB_CHECK(schema["properties"]["q"]["type"] == "string");
      QB_CHECK(schema["properties"]["q"]["maxLength"] == 4096);
    }
    if (item.name == "volunteer_chronicle") {
      QB_CHECK(schema["properties"]["since"]["type"] == "string");
    }
    const auto& tool = find_tool(tools, item.name);
    QB_CHECK(tool["description"] == operation->description);
    QB_CHECK(tool["inputSchema"] == schema);
  }
}

void exercise_mcp_contract(qbrain::Brain& selected, SnapshotMatrix& matrix) {
  qbrain::mcp::ServeOptions options;
  options.allow_write = false;
  int request_id = 200;

  const std::array<std::string, 4> operations = {
      "get_brain_identity", "volunteer_context", "get_timeline", "volunteer_chronicle"};
  for (const auto& operation : operations) {
    auto response = matrix.run("mcp:authorized:" + operation, [&] {
      return mcp_call(selected, options, operation, json::object(), request_id++);
    });
    QB_CHECK(response["result"]["isError"] == false);
    const auto payload = structured_mcp_content(response);
    if (operation == "get_brain_identity") {
      QB_CHECK(payload["source_id"] == "default");
      QB_CHECK(!payload.contains("db_path"));
      QB_CHECK(response.dump().find(selected.db_path()) == std::string::npos);
    } else {
      require_rows_from_source(payload, "default");
    }
  }

  auto empty_result = matrix.run("mcp:empty-result", [&] {
    return mcp_call(selected, options, "volunteer_context",
                    json{{"source_id", "empty"}}, request_id++);
  });
  QB_CHECK(empty_result["result"]["isError"] == false);
  QB_CHECK(structured_mcp_content(empty_result).empty());

  auto alias_conflict = matrix.run("mcp:alias-conflict", [&] {
    return mcp_call(selected, options, "volunteer_context",
                    json{{"query", "needle"}, {"q", "different"}}, request_id++);
  });
  require_mcp_error(alias_conflict, "invalid_argument", "query");

  const std::vector<json> invalid_limits = {
      -1, 1.5, true, nullptr, "1", json::array(), json::object()};
  for (const auto& operation : {"volunteer_context", "get_timeline",
                                "volunteer_chronicle"}) {
    for (const auto& limit : invalid_limits) {
      auto response = matrix.run(std::string("mcp:limit-type:") + operation, [&] {
        DatabaseReadObserver observer(selected);
        auto result = mcp_call(selected, options, operation, json{{"limit", limit}}, request_id++);
        QB_CHECK(observer.application_reads() == 0);
        return result;
      });
      require_mcp_error(response, "invalid_argument", "limit");
    }
  }

  const std::vector<json> invalid_sources = {true, nullptr, 1, json::array(), json::object()};
  for (const auto& operation : operations) {
    for (const auto& source : invalid_sources) {
      auto response = matrix.run("mcp:source-type:" + operation, [&] {
        DatabaseReadObserver observer(selected);
        auto result = mcp_call(selected, options, operation,
                               json{{"source_id", source}}, request_id++);
        QB_CHECK(observer.application_reads() == 0);
        return result;
      });
      require_mcp_error(response, "invalid_argument", "source_id");
    }

    auto non_object = matrix.run("mcp:arguments-type:" + operation, [&] {
      DatabaseReadObserver observer(selected);
      auto result = mcp_call(selected, options, operation, json::array(), request_id++);
      QB_CHECK(observer.application_reads() == 0);
      return result;
    });
    require_mcp_error(non_object, "invalid_argument", "arguments");

    auto unknown_field = matrix.run("mcp:unknown-field:" + operation, [&] {
      DatabaseReadObserver observer(selected);
      auto result = mcp_call(selected, options, operation,
                             json{{"unexpected", "value"}}, request_id++);
      QB_CHECK(observer.application_reads() == 0);
      return result;
    });
    require_mcp_error(unknown_field, "invalid_argument", "unexpected");
  }

  const std::vector<std::pair<std::string, json>> wrong_string_fields = {
      {"volunteer_context", json{{"query", 7}}},
      {"volunteer_context", json{{"q", true}}},
      {"volunteer_chronicle", json{{"since", json::object()}}},
  };
  for (const auto& [operation, arguments] : wrong_string_fields) {
    auto response = matrix.run("mcp:string-type:" + operation, [&] {
      DatabaseReadObserver observer(selected);
      auto result = mcp_call(selected, options, operation, arguments, request_id++);
      QB_CHECK(observer.application_reads() == 0);
      return result;
    });
    const std::string field = arguments.begin().key();
    require_mcp_error(response, "invalid_argument", field);
  }

  for (const auto& operation : operations) {
    auto unknown_source = matrix.run("mcp:unknown-source:" + operation, [&] {
      return mcp_call(selected, options, operation,
                      json{{"source_id", "unknown_n19"}}, request_id++);
    });
    require_mcp_error(unknown_source, "source_not_found", "source_id");

    auto denied_source = matrix.run("mcp:denied-source:" + operation, [&] {
      return mcp_call(selected, options, operation,
                      json{{"source_id", "recent_src"}}, request_id++);
    });
    require_mcp_error(denied_source, "source_not_allowed", "source_id");

    auto allowed_source = matrix.run("mcp:allowed-source:" + operation, [&] {
      return mcp_call(selected, options, operation,
                      json{{"source_id", "TEAM_A"}}, request_id++);
    });
    QB_CHECK(allowed_source["result"]["isError"] == false);
    const auto allowed_payload = structured_mcp_content(allowed_source);
    if (operation == "get_brain_identity") {
      QB_CHECK(allowed_payload["source_id"] == "team_a");
      QB_CHECK(!allowed_payload.contains("db_path"));
    } else {
      require_rows_from_source(allowed_payload, "team_a");
    }
  }

  {
    ScopedEnvironmentVariable ambient_source("QBRAIN_SOURCE", "team_a");
    for (const auto& operation : operations) {
      auto response = matrix.run("mcp:ambient-default:" + operation, [&] {
        return mcp_call(selected, options, operation, json::object(), request_id++);
      });
      QB_CHECK(response["result"]["isError"] == false);
      const auto payload = structured_mcp_content(response);
      if (operation == "get_brain_identity") {
        QB_CHECK(payload["source_id"] == "default");
      } else {
        require_rows_from_source(payload, "default");
      }
    }
  }
}

void exercise_damaged_database(const std::filesystem::path& path, qbrain::Brain& decoy) {
  qbrain::Brain damaged("n19-damaged");
  damaged.open_at(qbrain::util::path_to_utf8(path));
  put_page(damaged, "default", "damaged/page", "Damaged", "body");
  damaged.db().exec("DROP TABLE links");
  SnapshotMatrix matrix(damaged, decoy);
  auto result = matrix.run("identity:damaged-database", [&] {
    return qbrain::test_support::call_op(damaged, "get_brain_identity");
  });
  require_operation_error(result, "database_error", "database");
  QB_CHECK(result.json.find(damaged.db_path()) == std::string::npos);
  QB_CHECK(result.text.find(damaged.db_path()) == std::string::npos);
  QB_CHECK(result.json.find("damaged.db") == std::string::npos);
  QB_CHECK(result.text.find("qbrain_n19_test") == std::string::npos);
  QB_CHECK(qbrain::test_support::scalar(
               damaged,
               "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='links'") == 0);
  damaged.close();
}

}  // namespace

void test_n19() {
  namespace fs = std::filesystem;
  using namespace std::chrono;

  const auto root = fs::temp_directory_path() / "qbrain_n19_test";
  fs::remove_all(root);
  fs::create_directories(root);
  ScopedEnvironmentVariable local_app_data("LOCALAPPDATA",
                                            qbrain::util::path_to_utf8(root / "localappdata"));

  g_snapshot_evidence.clear();
  qbrain::ops::register_builtin_ops();
  exercise_utc_helper();

  qbrain::Brain selected("n19-selected");
  selected.open_at(qbrain::util::path_to_utf8(root / "selected.db"));
  const auto fresh_integrity = qbrain::storage::check_schema_integrity(selected.db());
  QB_CHECK(fresh_integrity.ok && fresh_integrity.schema_version == 12);
  QB_CHECK(selected.db_path() == qbrain::util::path_to_utf8(root / "selected.db"));

  for (const auto& source : {"team_a", "search_src", "recent_src", "utf8_src",
                             "utf8_search", "chron_src", "empty"}) {
    QB_CHECK(selected.ensure_source(source));
  }
  set_config_direct(selected, "n19.path_sentinel",
                    "C:\\Users\\N19_SECRET_USER|\\\\n19-server\\private|"
                    "\\\\?\\Volume{N19-SECRET}|N19_DECOY_PATH");

  const auto identity_default = put_page(selected, "default", "identity/default", "Default",
                                         "default identity body");
  selected.replace_chunks(identity_default.id, {"default chunk zero", "default chunk one"});
  auto default_chunks = selected.get_chunks(identity_default.id);
  QB_CHECK(default_chunks.size() == 2);
  selected.update_chunk_embedding(default_chunks[0].id, {0.25F, 0.5F}, "n19-test");
  const auto identity_deleted = put_page(selected, "default", "identity/deleted", "Deleted",
                                         "deleted body");
  selected.replace_chunks(identity_deleted.id, {"deleted stored chunk"});
  auto deleted_chunks = selected.get_chunks(identity_deleted.id);
  selected.update_chunk_embedding(deleted_chunks[0].id, {0.75F}, "n19-test");
  QB_CHECK(selected.soft_delete(identity_deleted.slug));
  put_page(selected, "default", "identity/target", "Target", "target body");
  add_link(selected, "default", "identity/default", "identity/target", "manual-a");
  add_link(selected, "default", "identity/target", "identity/default", "manual-b");

  const auto identity_team = put_page(selected, "team_a", "identity/team", "Team",
                                      "team identity body");
  selected.replace_chunks(identity_team.id, {"team chunk"});
  put_page(selected, "team_a", "identity/team-target", "Team target", "target");
  add_link(selected, "team_a", "identity/team", "identity/team-target", "team-manual");
  const auto identity_team_deleted =
      put_page(selected, "team_a", "identity/team-deleted", "Team deleted", "deleted");
  selected.replace_chunks(identity_team_deleted.id,
                          {"team deleted chunk zero", "team deleted chunk one"});
  auto team_deleted_chunks = selected.get_chunks(identity_team_deleted.id);
  QB_CHECK(team_deleted_chunks.size() == 2);
  selected.update_chunk_embedding(team_deleted_chunks[1].id, {0.125F}, "n19-test");
  QB_CHECK(selected.soft_delete(identity_team_deleted.slug, "team_a"));

  selected.log_ingest("n19_fixture", "C:\\n19\\fixture.md", R"({"rows":1})", 100,
                      "default");
  selected.add_fact("n19/entity", "mentions", "sentinel", identity_default.id);
  selected.db().exec(
      "INSERT INTO jobs(queue,type,status,payload_json,priority) "
      "VALUES('n19','fixture','completed','{}',7)");

  seed_search_fixture(selected, true);
  const auto search_deleted = put_page(selected, "search_src", "tie/deleted", "Neutral",
                                       "needle deleted candidate");
  QB_CHECK(selected.soft_delete(search_deleted.slug, "search_src"));
  put_page(selected, "team_a", "search/other-source", "Neutral",
           "needle OTHER_SOURCE_SEARCH_SENTINEL");

  auto recent_old = put_page(selected, "recent_src", "recent/old", "Old", "old");
  auto recent_a = put_page(selected, "recent_src", "recent/a", "A", "a");
  auto recent_b = put_page(selected, "recent_src", "recent/b", "B", "b");
  auto recent_deleted = put_page(selected, "recent_src", "recent/deleted", "Deleted", "deleted");
  set_times(selected, recent_old.id, "2025-01-01 00:00:00", "2025-01-01 00:00:00");
  set_times(selected, recent_a.id, "2025-01-02 00:00:00", "2025-01-03 00:00:00");
  set_times(selected, recent_b.id, "2025-01-02 00:00:00", "2025-01-03 00:00:00");
  set_times(selected, recent_deleted.id, "2098-01-01 00:00:00", "2098-01-01 00:00:00");
  QB_CHECK(selected.soft_delete(recent_deleted.slug, "recent_src"));
  auto other_source_newer = put_page(selected, "team_a", "recent/out-of-source", "Newer", "new");
  set_times(selected, other_source_newer.id, "2099-01-01 00:00:00", "2099-01-01 00:00:00");

  std::string invalid_title = "bad-";
  invalid_title.push_back(static_cast<char>(0xff));
  invalid_title.append(700, 't');
  auto invalid_title_page =
      put_page(selected, "utf8_src", "utf8/title", invalid_title, "valid body");
  set_times(selected, invalid_title_page.id, "2025-04-01 00:00:00",
            "2025-04-01 00:00:00");
  std::string invalid_snippet = "needle bad-";
  invalid_snippet.push_back(static_cast<char>(0xff));
  invalid_snippet.append(700, 's');
  put_page(selected, "utf8_search", "utf8/snippet", "Snippet", invalid_snippet);

  auto timeline_a = put_page(selected, "default", "timeline/a", "Timeline A",
                             "TIMELINE_BODY_SECRET_A", "timeline");
  auto timeline_b = put_page(selected, "default", "timeline/b", "Timeline B",
                             "TIMELINE_BODY_SECRET_B", "timeline");
  auto timeline_c = put_page(selected, "default", "timeline/c", "Timeline C",
                             "TIMELINE_BODY_SECRET_C", "timeline");
  auto timeline_deleted = put_page(selected, "default", "timeline/deleted", "Deleted",
                                   "TIMELINE_BODY_SECRET_DELETED", "timeline");
  auto non_timeline = put_page(selected, "default", "timeline/not-exact", "Not timeline",
                               "TIMELINE_BODY_SECRET_NOTE", "Timeline");
  auto team_timeline = put_page(selected, "team_a", "team/timeline-newer", "Team timeline",
                                "TIMELINE_BODY_SECRET_TEAM", "timeline");
  set_times(selected, timeline_a.id, "2025-03-01 00:00:00", "2025-03-02 00:00:00");
  set_times(selected, timeline_b.id, "2025-03-03 00:00:00", "2025-03-01 00:00:00");
  set_times(selected, timeline_c.id, "2025-03-01 00:00:00", "2025-03-03 00:00:00");
  set_times(selected, timeline_deleted.id, "2098-01-01 00:00:00", "2098-01-01 00:00:00");
  set_times(selected, non_timeline.id, "2100-01-01 00:00:00", "2100-01-01 00:00:00");
  set_times(selected, team_timeline.id, "2099-01-01 00:00:00", "2099-01-01 00:00:00");
  QB_CHECK(selected.soft_delete(timeline_deleted.slug));

  auto chron_before = put_page(selected, "chron_src", "chron/before", "Before", "before");
  auto chron_created =
      put_page(selected, "chron_src", "chron/created-at", "Created", "created");
  auto chron_updated =
      put_page(selected, "chron_src", "chron/updated-at", "Updated", "updated");
  auto chron_tie_a = put_page(selected, "chron_src", "chron/tie-a", "Tie A", "tie");
  auto chron_tie_b = put_page(selected, "chron_src", "chron/tie-b", "Tie B", "tie");
  auto chron_deleted =
      put_page(selected, "chron_src", "chron/deleted", "Deleted", "deleted");
  set_times(selected, chron_before.id, "2024-02-28 23:59:59", "2024-02-28 23:59:59");
  set_times(selected, chron_created.id, "2024-02-29 00:00:00", "2024-02-01 00:00:00");
  set_times(selected, chron_updated.id, "2024-02-01 00:00:00", "2024-02-29 00:00:00");
  set_times(selected, chron_tie_a.id, "2024-03-01 00:00:00", "2024-03-01 00:00:00");
  set_times(selected, chron_tie_b.id, "2024-03-01 00:00:00", "2024-03-01 00:00:00");
  set_times(selected, chron_deleted.id, "2024-03-02 00:00:00", "2024-03-02 00:00:00");
  QB_CHECK(selected.soft_delete(chron_deleted.slug, "chron_src"));

  const auto fixed_now = system_clock::now();
  const auto live_boundary_time = floor<days>(fixed_now) - days{6};
  const auto live_before_time = live_boundary_time - seconds{1};
  auto live_boundary = put_page(selected, "default", "window/at-boundary", "At boundary", "at");
  auto live_before = put_page(selected, "default", "window/before-boundary", "Before boundary",
                              "before");
  auto live_old = put_page(selected, "default", "window/old-history", "Old history", "old");
  set_times(selected, live_boundary.id, format_sql_utc(live_boundary_time),
            format_sql_utc(live_boundary_time));
  set_times(selected, live_before.id, format_sql_utc(live_before_time),
            format_sql_utc(live_before_time));
  set_times(selected, live_old.id, "2000-01-01 00:00:00", "2000-01-01 00:00:00");

  const auto populated_before_reopen = logical_snapshot(selected);
  selected.close();
  selected.open_at(qbrain::util::path_to_utf8(root / "selected.db"));
  const auto reopened_integrity = qbrain::storage::check_schema_integrity(selected.db());
  QB_CHECK(reopened_integrity.ok && reopened_integrity.schema_version == 12);
  QB_CHECK(logical_snapshot(selected) == populated_before_reopen);

  qbrain::Brain decoy("n19-decoy");
  decoy.open_at(qbrain::util::path_to_utf8(root / "decoy.db"));
  QB_CHECK(decoy.ensure_source("team_a"));
  QB_CHECK(decoy.ensure_source("search_src"));
  const auto decoy_default_page =
      put_page(decoy, "default", "DECOY_IDENTITY_SENTINEL", "Decoy", "N19_DECOY_PATH");
  decoy.replace_chunks(decoy_default_page.id,
                       {"decoy default chunk zero", "decoy default chunk one"});
  auto decoy_default_chunks = decoy.get_chunks(decoy_default_page.id);
  QB_CHECK(decoy_default_chunks.size() == 2);
  decoy.update_chunk_embedding(decoy_default_chunks[0].id, {0.375F}, "n19-test");
  const auto decoy_default_deleted = put_page(
      decoy, "default", "DECOY_DEFAULT_DELETED", "Decoy deleted", "deleted body");
  decoy.replace_chunks(decoy_default_deleted.id, {"decoy deleted stored chunk"});
  QB_CHECK(decoy.soft_delete(decoy_default_deleted.slug));
  put_page(decoy, "default", "DECOY_TIMELINE_SENTINEL", "Decoy timeline",
           "DECOY_TIMELINE_BODY", "timeline");
  put_page(decoy, "search_src", "DECOY_SEARCH_SENTINEL", "Neutral", "needle decoy");
  add_link(decoy, "default", "DECOY_IDENTITY_SENTINEL", "DECOY_TIMELINE_SENTINEL");
  const auto decoy_team_page =
      put_page(decoy, "team_a", "DECOY_TEAM_IDENTITY", "Decoy team", "team body");
  decoy.replace_chunks(decoy_team_page.id, {"decoy team active chunk"});
  put_page(decoy, "team_a", "DECOY_TEAM_TARGET", "Decoy team target", "target");
  const auto decoy_team_deleted = put_page(
      decoy, "team_a", "DECOY_TEAM_DELETED", "Decoy team deleted", "deleted body");
  decoy.replace_chunks(decoy_team_deleted.id,
                       {"decoy team deleted chunk zero", "decoy team deleted chunk one"});
  auto decoy_team_deleted_chunks = decoy.get_chunks(decoy_team_deleted.id);
  QB_CHECK(decoy_team_deleted_chunks.size() == 2);
  decoy.update_chunk_embedding(decoy_team_deleted_chunks[0].id, {0.625F}, "n19-test");
  QB_CHECK(decoy.soft_delete(decoy_team_deleted.slug, "team_a"));
  add_link(decoy, "team_a", "DECOY_TEAM_IDENTITY", "DECOY_TEAM_TARGET",
           "decoy-team-manual");
  const auto decoy_integrity = qbrain::storage::check_schema_integrity(decoy.db());
  QB_CHECK(decoy_integrity.ok && decoy_integrity.schema_version == 12);

  qbrain::Brain reverse_fixture("n19-reverse-search");
  reverse_fixture.open_at(qbrain::util::path_to_utf8(root / "reverse-search.db"));
  seed_search_fixture(reverse_fixture, false);

  qbrain::Brain fallback_fixture("n19-fallback-search");
  fallback_fixture.open_at(qbrain::util::path_to_utf8(root / "fallback-search.db"));
  prepare_search_fallback_fixture(fallback_fixture, true);
  qbrain::Brain reverse_fallback_fixture("n19-reverse-fallback-search");
  reverse_fallback_fixture.open_at(
      qbrain::util::path_to_utf8(root / "reverse-fallback-search.db"));
  prepare_search_fallback_fixture(reverse_fallback_fixture, false);

  SnapshotMatrix matrix(selected, decoy);
  SnapshotMatrix reverse_matrix(reverse_fixture, decoy);

  exercise_source_contract(selected, decoy, matrix);
  exercise_identity_contract(selected, decoy, matrix, root);
  exercise_context_contract(selected, decoy, reverse_fixture, matrix, reverse_matrix);
  exercise_context_fallback_contract(fallback_fixture, reverse_fallback_fixture, decoy);
  exercise_timeline_contract(selected, matrix);
  exercise_chronicle_contract(selected, matrix, live_boundary.slug, live_before.slug,
                              live_old.slug);
  exercise_registry_contract(selected, matrix);
  exercise_mcp_contract(selected, matrix);
  exercise_damaged_database(root / "damaged.db", decoy);

  const auto selected_snapshot = logical_snapshot(selected);
  const auto decoy_snapshot = logical_snapshot(decoy);
  QB_CHECK(!g_snapshot_evidence.empty());
  std::cout << "[INFO] n19 schema_v12=pass schema_reopen=pass utc_boundaries=pass "
               "source_matrix=pass strict_arguments=pass identity=pass "
               "identity_exact_matrix=pass identity_matrix_cells=4 path_redaction=pass "
               "context_query=pass context_fail_open=pass context_recent=pass "
               "utf8_bounds=pass timeline=pass "
               "chronicle=pass seven_day_default=pass registry=pass mcp_rpc=pass "
               "ambient_default=pass selected_decoy=pass damaged_database=pass read_only=pass "
               "snapshot_call_count="
            << g_snapshot_evidence.size() << " selected_snapshot_sha256="
            << snapshot_sha256(selected_snapshot) << " decoy_snapshot_sha256="
            << snapshot_sha256(decoy_snapshot) << "\n";
  for (const auto& evidence : g_snapshot_evidence) {
    std::cout << "[INFO] n19 snapshot_call=" << evidence.index << " label=" << evidence.label
              << " selected_before_sha256=" << evidence.selected_before
              << " selected_after_sha256=" << evidence.selected_after
              << " decoy_before_sha256=" << evidence.decoy_before
              << " decoy_after_sha256=" << evidence.decoy_after << "\n";
  }

  reverse_fallback_fixture.close();
  fallback_fixture.close();
  reverse_fixture.close();
  decoy.close();
  selected.close();
  fs::remove_all(root);
}
