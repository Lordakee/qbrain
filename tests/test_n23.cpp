#include "qbrain/core/brain.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/util/paths.hpp"
#include "qbrain/util/time_util.hpp"
#include "wave3_test_support.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
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
    if (_putenv_s(name_.c_str(), value.c_str()) != 0)
      throw std::runtime_error("failed to set test environment variable");
  }
  ~ScopedEnvironmentVariable() { _putenv_s(name_.c_str(), previous_ ? previous_->c_str() : ""); }

 private:
  std::string name_;
  std::optional<std::string> previous_;
};

class ScopedTestDirectory {
 public:
  explicit ScopedTestDirectory(std::string_view prefix) {
    const auto temporary_root = std::filesystem::temp_directory_path();
    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt) {
      const auto nonce = std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                         std::to_string(random()) + "_" + std::to_string(attempt);
      auto candidate = temporary_root / (std::string(prefix) + nonce);
      std::error_code error;
      if (std::filesystem::create_directory(candidate, error)) {
        path_ = std::move(candidate);
        return;
      }
      if (error && error != std::errc::file_exists)
        throw std::runtime_error("failed to create isolated N23 test directory");
    }
    throw std::runtime_error("failed to reserve a unique N23 test directory");
  }

  ~ScopedTestDirectory() {
    if (path_.empty()) return;
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

struct SnapshotRow {
  std::string label;
  std::string selected_before;
  std::string selected_after;
  std::string decoy_before;
  std::string decoy_after;
};

std::vector<SnapshotRow> g_snapshots;

class SnapshotMatrix {
 public:
  SnapshotMatrix(qbrain::Brain& selected, qbrain::Brain& decoy)
      : selected_(selected), decoy_(decoy) {}

  template <typename Fn>
  auto read(const std::string& label, Fn&& fn) {
    const auto selected_before = logical_snapshot(selected_);
    const auto decoy_before = logical_snapshot(decoy_);
    auto result = fn();
    const auto selected_after = logical_snapshot(selected_);
    const auto decoy_after = logical_snapshot(decoy_);
    if (selected_before != selected_after || decoy_before != decoy_after)
      throw std::runtime_error("N23 read/rejection mutated state: " + label);
    g_snapshots.push_back({label, snapshot_sha256(selected_before),
                           snapshot_sha256(selected_after), snapshot_sha256(decoy_before),
                           snapshot_sha256(decoy_after)});
    return result;
  }

 private:
  qbrain::Brain& selected_;
  qbrain::Brain& decoy_;
};

qbrain::Page put_page(qbrain::Brain& brain, const std::string& source_id,
                      const std::string& slug, const std::string& title,
                      const std::string& type, const std::string& body = "body") {
  qbrain::PageInput input;
  input.source_id = source_id;
  input.slug = slug;
  input.title = title;
  input.type = type;
  input.body = body;
  return brain.put_page(input);
}

void set_times(qbrain::Brain& brain, int64_t id, const std::string& created,
               const std::string& updated) {
  auto statement = brain.db().prepare("UPDATE pages SET created_at=?,updated_at=? WHERE id=?");
  statement.bind_text(1, created);
  statement.bind_text(2, updated);
  statement.bind_int(3, id);
  statement.step_done();
  QB_CHECK(brain.db().changes() == 1);
}

void delete_page(qbrain::Brain& brain, int64_t id) {
  auto statement = brain.db().prepare("UPDATE pages SET deleted_at='2024-06-01 00:00:00' WHERE id=?");
  statement.bind_int(1, id);
  statement.step_done();
  QB_CHECK(brain.db().changes() == 1);
}

int tag_count(qbrain::Brain& brain, const std::string& source_id,
              const std::string& slug, const std::string& tag = "chronicle") {
  auto statement = brain.db().prepare(
      "SELECT COUNT(*) FROM tags t JOIN pages p ON p.id=t.page_id "
      "WHERE p.source_id=? AND p.slug=? AND t.tag=?");
  statement.bind_text(1, source_id);
  statement.bind_text(2, slug);
  statement.bind_text(3, tag);
  return statement.step() ? static_cast<int>(statement.column_int(0)) : 0;
}

qbrain::ops::OpResult call_op(qbrain::Brain& brain, const std::string& name,
                              std::unordered_map<std::string, std::string> args = {},
                              bool remote = false, bool allow_write = false) {
  qbrain::ops::OpContext context;
  context.brain = &brain;
  context.args = std::move(args);
  context.remote = remote;
  context.allow_write = allow_write;
  return qbrain::ops::global_registry().call(name, context);
}

json parse_result(const qbrain::ops::OpResult& result) {
  QB_CHECK(!result.json.empty());
  return json::parse(result.json);
}

void require_error(const qbrain::ops::OpResult& result, const std::string& code,
                   const std::string& field) {
  QB_CHECK(!result.ok);
  QB_CHECK(result.exit_code != 0);
  const auto parsed = parse_result(result);
  QB_CHECK(parsed.is_object() && parsed.size() == 1 && parsed.contains("error"));
  const auto& error = parsed.at("error");
  QB_CHECK(error.is_object() && error.size() == 3 && error.contains("code") &&
           error.contains("field") && error.contains("message"));
  QB_CHECK(error.at("code") == code);
  QB_CHECK(error.at("field") == field);
  QB_CHECK(error.at("message").is_string());
  const auto serialized = parsed.dump();
  QB_CHECK(serialized.size() <= 512);
  for (const std::string_view forbidden : {"BODY_SECRET", "TEAM_", "DECOY_", "mcp.allowed_sources",
                                           "api_key", "provider", "model", "D:\\"}) {
    QB_CHECK(serialized.find(forbidden) == std::string::npos);
  }
}

template <typename Fn>
std::string require_invalid_argument(Fn&& fn) {
  try {
    fn();
  } catch (const std::invalid_argument& error) {
    return error.what();
  }
  throw std::runtime_error("expected std::invalid_argument");
}

json mcp_call(qbrain::Brain& brain, const std::string& name, const json& arguments,
              bool allow_write = false) {
  qbrain::mcp::ServeOptions options;
  options.allow_write = allow_write;
  const json request = {{"jsonrpc", "2.0"},
                        {"id", 23},
                        {"method", "tools/call"},
                        {"params", {{"name", name}, {"arguments", arguments}}}};
  return json::parse(qbrain::mcp::handle_rpc_body(brain, options, request.dump()));
}

json mcp_payload_json(const json& response) {
  const auto& content = response.at("result").at("content");
  for (auto it = content.rbegin(); it != content.rend(); ++it) {
    if (!it->contains("text") || !it->at("text").is_string()) continue;
    try {
      return json::parse(it->at("text").get<std::string>());
    } catch (...) {
    }
  }
  throw std::runtime_error("MCP response has no JSON content");
}

json mcp_tools(qbrain::Brain& brain) {
  qbrain::mcp::ServeOptions options;
  const json request = {{"jsonrpc", "2.0"},
                        {"id", 24},
                        {"method", "tools/list"},
                        {"params", json::object()}};
  return json::parse(qbrain::mcp::handle_rpc_body(brain, options, request.dump()))
      .at("result")
      .at("tools");
}

const json& tool_by_name(const json& tools, const std::string& name) {
  for (const auto& tool : tools)
    if (tool.at("name") == name) return tool;
  throw std::runtime_error("missing MCP tool: " + name);
}

void require_keys(const json& value, std::initializer_list<std::string_view> expected) {
  QB_CHECK(value.is_object());
  std::set<std::string> actual;
  for (auto it = value.begin(); it != value.end(); ++it) actual.insert(it.key());
  std::set<std::string> wanted;
  for (auto key : expected) wanted.emplace(key);
  QB_CHECK(actual == wanted);
}

void require_mcp_error(const json& response, const std::string& code,
                       const std::string& field) {
  QB_CHECK(response.at("result").at("isError") == true);
  const auto payload = mcp_payload_json(response);
  require_keys(payload, {"error"});
  require_keys(payload.at("error"), {"code", "field", "message"});
  QB_CHECK(payload.at("error").at("code") == code);
  QB_CHECK(payload.at("error").at("field") == field);
  QB_CHECK(payload.dump().size() <= 512);
}

std::string year_with_month_day(int year, std::string_view month_day) {
  char prefix[8]{};
  std::snprintf(prefix, sizeof(prefix), "%04d-", year);
  return std::string(prefix) + std::string(month_day);
}

std::string hex_encode(std::string_view value) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string encoded;
  encoded.reserve(value.size() * 2);
  for (const unsigned char byte : value) {
    encoded.push_back(kHex[byte >> 4]);
    encoded.push_back(kHex[byte & 0x0f]);
  }
  return encoded;
}

std::string quote_identifier(std::string_view value) {
  std::string quoted = "\"";
  for (const char c : value) {
    quoted.push_back(c);
    if (c == '\"') quoted.push_back('\"');
  }
  quoted.push_back('\"');
  return quoted;
}

struct StatementFinalizer {
  void operator()(sqlite3_stmt* statement) const noexcept { sqlite3_finalize(statement); }
};

using StatementPtr = std::unique_ptr<sqlite3_stmt, StatementFinalizer>;

void count_tag_inserts(void* context, int operation, const char*, const char* table,
                       sqlite3_int64) noexcept {
  if (operation == SQLITE_INSERT && table && std::string_view(table) == "tags")
    ++*static_cast<int*>(context);
}

std::string query_snapshot(qbrain::Brain& brain, const std::string& sql) {
  sqlite3_stmt* raw_statement = nullptr;
  if (sqlite3_prepare_v2(brain.db().handle(), sql.c_str(), -1, &raw_statement, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error("N23 snapshot query preparation failed");
  }
  StatementPtr statement(raw_statement);
  const int columns = sqlite3_column_count(statement.get());
  std::vector<std::string> rows;
  int result = SQLITE_OK;
  while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
    std::string row;
    for (int column = 0; column < columns; ++column) {
      if (column) row.push_back('|');
      row += qbrain::test_support::snapshot_cell(statement.get(), column);
    }
    rows.push_back(std::move(row));
  }
  if (result != SQLITE_DONE) throw std::runtime_error("N23 snapshot query failed");
  std::sort(rows.begin(), rows.end());
  std::string snapshot;
  for (const auto& row : rows) {
    snapshot += row;
    snapshot.push_back('\n');
  }
  return snapshot;
}

std::vector<std::string> table_names(qbrain::Brain& brain) {
  sqlite3_stmt* raw_statement = nullptr;
  const char* sql =
      "SELECT name FROM sqlite_master WHERE type='table' AND "
      "(name NOT LIKE 'sqlite_%' OR name='sqlite_sequence') ORDER BY name";
  if (sqlite3_prepare_v2(brain.db().handle(), sql, -1, &raw_statement, nullptr) != SQLITE_OK)
    throw std::runtime_error("N23 table discovery failed");
  StatementPtr statement(raw_statement);
  std::vector<std::string> names;
  int result = SQLITE_OK;
  while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
    const auto* name = sqlite3_column_text(statement.get(), 0);
    if (name) names.emplace_back(reinterpret_cast<const char*>(name));
  }
  if (result != SQLITE_DONE) throw std::runtime_error("N23 table discovery failed");
  return names;
}

std::string schema_snapshot(qbrain::Brain& brain) {
  return query_snapshot(
      brain,
      "SELECT type,name,COALESCE(tbl_name,''),COALESCE(sql,'') FROM sqlite_master "
      "WHERE name NOT LIKE 'sqlite_%' OR name='sqlite_sequence' ORDER BY type,name");
}

json schema_objects(qbrain::Brain& brain) {
  sqlite3_stmt* raw_statement = nullptr;
  const char* sql =
      "SELECT type,name,COALESCE(tbl_name,''),COALESCE(sql,'') FROM sqlite_master "
      "WHERE name NOT LIKE 'sqlite_%' OR name='sqlite_sequence' ORDER BY type,name";
  if (sqlite3_prepare_v2(brain.db().handle(), sql, -1, &raw_statement, nullptr) != SQLITE_OK)
    throw std::runtime_error("N23 schema discovery failed");
  StatementPtr statement(raw_statement);
  json rows = json::array();
  int result = SQLITE_OK;
  while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
    const std::string type =
        reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
    const std::string name =
        reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 1));
    const std::string table =
        reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 2));
    const std::string ddl =
        reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 3));
    rows.push_back({{"type", type},
                    {"name", name},
                     {"tbl_name", table},
                     {"sql_sha256", snapshot_sha256(ddl)}});
  }
  if (result != SQLITE_DONE) throw std::runtime_error("N23 schema discovery failed");
  return rows;
}

json tag_rows(qbrain::Brain& brain) {
  sqlite3_stmt* raw_statement = nullptr;
  const char* sql =
      "SELECT t.page_id,p.source_id,p.slug,t.tag FROM tags t JOIN pages p ON p.id=t.page_id "
      "ORDER BY t.page_id,t.tag";
  if (sqlite3_prepare_v2(brain.db().handle(), sql, -1, &raw_statement, nullptr) != SQLITE_OK)
    throw std::runtime_error("N23 tag snapshot preparation failed");
  StatementPtr statement(raw_statement);
  json rows = json::array();
  int result = SQLITE_OK;
  while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
    rows.push_back(
        {{"page_id", sqlite3_column_int64(statement.get(), 0)},
         {"source_id", reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 1))},
          {"slug", reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 2))},
          {"tag", reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 3))}});
  }
  if (result != SQLITE_DONE) throw std::runtime_error("N23 tag snapshot failed");
  return rows;
}

json sqlite_sequence_rows(qbrain::Brain& brain) {
  bool present = false;
  for (const auto& table : table_names(brain)) {
    if (table == "sqlite_sequence") {
      present = true;
      break;
    }
  }
  if (!present) return json::array();
  sqlite3_stmt* raw_statement = nullptr;
  if (sqlite3_prepare_v2(brain.db().handle(),
                         "SELECT name,seq FROM sqlite_sequence ORDER BY name", -1,
                         &raw_statement, nullptr) != SQLITE_OK)
    throw std::runtime_error("N23 sqlite_sequence preparation failed");
  StatementPtr statement(raw_statement);
  json rows = json::array();
  int result = SQLITE_OK;
  while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
    rows.push_back(
        {{"name", reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0))},
         {"seq", sqlite3_column_int64(statement.get(), 1)}});
  }
  if (result != SQLITE_DONE) throw std::runtime_error("N23 sqlite_sequence query failed");
  return rows;
}

json changed_rows(const json& before, const json& after) {
  json added = json::array();
  for (const auto& row : after) {
    if (std::find(before.begin(), before.end(), row) == before.end()) added.push_back(row);
  }
  return added;
}

json removed_rows(const json& before, const json& after) {
  json removed = json::array();
  for (const auto& row : before) {
    if (std::find(after.begin(), after.end(), row) == after.end()) removed.push_back(row);
  }
  return removed;
}

json delta_table_evidence(qbrain::Brain& brain) {
  json rows = json::array();
  for (const auto& table : table_names(brain)) {
    if (table == "tags" || table == "sqlite_sequence") continue;
    const auto snapshot = query_snapshot(brain, "SELECT * FROM " + quote_identifier(table));
    size_t count = 0;
    for (size_t offset = 0; offset < snapshot.size();) {
      ++count;
      const auto end = snapshot.find('\n', offset);
      if (end == std::string::npos) break;
      offset = end + 1;
    }
    rows.push_back({{"name", table}, {"sha256", snapshot_sha256(snapshot)}, {"rows", count}});
  }
  return rows;
}

json make_allowed_delta(const std::string& selected_before, const std::string& selected_after,
                        const std::string& decoy_before, const std::string& decoy_after,
                        const std::string& schema_before, const std::string& schema_after,
                        const json& schema_before_objects, const json& schema_after_objects,
                        const json& non_tag_before, const json& non_tag_after,
                        const json& tags_before, const json& tags_after,
                        const json& sequence_before, const json& sequence_after,
                        int tagged, int already_tagged) {
  json non_tag = json::array();
  QB_CHECK(non_tag_before.size() == non_tag_after.size());
  for (size_t index = 0; index < non_tag_before.size(); ++index) {
    const auto& before = non_tag_before[index];
    const auto& after = non_tag_after[index];
    QB_CHECK(before.at("name") == after.at("name"));
    non_tag.push_back({{"name", before.at("name")},
                       {"before_sha256", before.at("sha256")},
                       {"after_sha256", after.at("sha256")},
                       {"before_rows", before.at("rows")},
                       {"after_rows", after.at("rows")}});
  }
  json schema = json::array();
  QB_CHECK(schema_before_objects.size() == schema_after_objects.size());
  for (size_t index = 0; index < schema_before_objects.size(); ++index) {
    const auto& before = schema_before_objects[index];
    const auto& after = schema_after_objects[index];
    QB_CHECK(before.at("type") == after.at("type"));
    QB_CHECK(before.at("name") == after.at("name"));
    QB_CHECK(before.at("tbl_name") == after.at("tbl_name"));
    schema.push_back({{"type", before.at("type")},
                      {"name", before.at("name")},
                      {"tbl_name", before.at("tbl_name")},
                      {"before_sql_sha256", before.at("sql_sha256")},
                      {"after_sql_sha256", after.at("sql_sha256")}});
  }
  const auto tag_added = changed_rows(tags_before, tags_after);
  const auto tag_removed = removed_rows(tags_before, tags_after);
  return {{"format_version", 1},
          {"selected_full",
           {{"before_sha256", snapshot_sha256(selected_before)},
            {"after_sha256", snapshot_sha256(selected_after)},
            {"before_bytes", selected_before.size()},
            {"after_bytes", selected_after.size()}}},
          {"decoy_full",
           {{"before_sha256", snapshot_sha256(decoy_before)},
            {"after_sha256", snapshot_sha256(decoy_after)},
            {"before_bytes", decoy_before.size()},
            {"after_bytes", decoy_after.size()}}},
          {"schema",
           {{"before_sha256", snapshot_sha256(schema_before)},
            {"after_sha256", snapshot_sha256(schema_after)},
            {"objects", schema}}},
          {"non_tag_tables", non_tag},
          {"tags",
           {{"before_rows", tags_before},
            {"after_rows", tags_after},
            {"added_rows", tag_added},
            {"removed_rows", tag_removed},
            {"before_sha256", snapshot_sha256(tags_before.dump())},
            {"after_sha256", snapshot_sha256(tags_after.dump())}}},
          {"sqlite_sequence",
           {{"before_rows", sequence_before},
            {"after_rows", sequence_after},
            {"delta", json::array()},
            {"before_sha256", snapshot_sha256(sequence_before.dump())},
            {"after_sha256", snapshot_sha256(sequence_after.dump())}}},
          {"result", {{"tagged", tagged}, {"already_tagged", already_tagged}}}};
}

}  // namespace

void test_n23() {
  namespace fs = std::filesystem;
  using namespace qbrain;

  g_snapshots.clear();
  ScopedTestDirectory test_directory("qbrain_n23_");
  const auto& root = test_directory.path();
  const auto isolated_localappdata = root / "localappdata";
  fs::create_directory(isolated_localappdata);
  ScopedEnvironmentVariable localappdata(
      "LOCALAPPDATA", qbrain::util::path_to_utf8(isolated_localappdata));
  const auto selected_path = root / "selected.db";
  const auto decoy_path = root / "decoy.db";

  Brain selected("n23-selected");
  Brain decoy("n23-decoy");
  selected.open_at(util::path_to_utf8(selected_path));
  decoy.open_at(util::path_to_utf8(decoy_path));
  QB_CHECK(selected.ensure_source("team_a"));
  QB_CHECK(selected.ensure_source("other"));
  QB_CHECK(decoy.ensure_source("team_a"));
  ops::register_builtin_ops();
  SnapshotMatrix matrix(selected, decoy);

  const auto created = put_page(selected, "default", "history-created", "created", "note",
                                "BODY_SECRET_CREATED");
  set_times(selected, created.id, "2020-03-01 08:00:00", "2021-04-01 08:00:00");
  const auto updated = put_page(selected, "default", "history-updated", "updated", "note");
  set_times(selected, updated.id, "2019-02-01 08:00:00", "2023-03-01 09:00:00");
  const auto both = put_page(selected, "default", "history-both", "both", "note");
  set_times(selected, both.id, "2022-03-01 10:00:00", "2023-03-01 10:00:00");
  const auto tie_first = put_page(selected, "default", "history-tie-first", "tie-first", "note");
  const auto tie_second = put_page(selected, "default", "history-tie-second", "tie-second", "note");
  set_times(selected, tie_first.id, "2018-01-01 00:00:00", "2023-03-01 11:00:00");
  set_times(selected, tie_second.id, "2018-01-01 00:00:00", "2023-03-01 11:00:00");
  std::string long_title(505, 'T');
  long_title += "\xF0\x9F\x98\x80\xF0\x9F\x98\x80";
  const auto title_page = put_page(selected, "default", "history-long-title", long_title,
                                   "note", "BODY_SECRET_LONG_TITLE");
  set_times(selected, title_page.id, "2017-03-01 00:00:00", "2017-03-01 00:00:00");
  const auto anchor_year = put_page(selected, "default", "history-anchor", "anchor", "note");
  set_times(selected, anchor_year.id, "2024-03-01 00:00:00", "2024-03-01 01:00:00");
  const auto future = put_page(selected, "default", "history-future", "future", "note");
  set_times(selected, future.id, "2025-03-01 00:00:00", "2025-03-01 01:00:00");
  const auto other_day = put_page(selected, "default", "history-other-day", "other-day", "note");
  set_times(selected, other_day.id, "2020-03-02 00:00:00", "2023-03-02 01:00:00");
  const auto deleted = put_page(selected, "default", "history-deleted", "deleted", "note");
  set_times(selected, deleted.id, "2020-03-01 00:00:00", "2023-03-01 12:00:00");
  delete_page(selected, deleted.id);
  const auto team_history = put_page(selected, "team_a", "TEAM_HISTORY_SENTINEL",
                                     "team", "note");
  set_times(selected, team_history.id, "2020-03-01 00:00:00", "2023-03-01 23:00:00");
  const auto decoy_history = put_page(decoy, "default", "DECOY_HISTORY_SENTINEL",
                                       "decoy", "note");
  set_times(decoy, decoy_history.id, "2020-03-01 00:00:00", "2023-03-01 23:59:00");
  const auto team_limit = put_page(selected, "team_a", "history-limit-sentinel",
                                   "TEAM_LIMIT_SENTINEL", "note");
  set_times(selected, team_limit.id, "2020-01-01 00:00:00", "2023-01-01 23:00:00");
  const auto decoy_limit = put_page(decoy, "default", "history-limit-sentinel",
                                    "DECOY_LIMIT_SENTINEL", "note");
  set_times(decoy, decoy_limit.id, "2020-01-01 00:00:00", "2023-01-01 23:59:00");

  const auto history = matrix.read("on-this-day:direct", [&] {
    const auto canonical_team_history =
        selected.chronicle_on_this_day("2024-03-01", 50, "TEAM_A");
    QB_CHECK(canonical_team_history.size() == 1);
    QB_CHECK(canonical_team_history[0].source_id == "team_a");
    QB_CHECK(canonical_team_history[0].slug == "TEAM_HISTORY_SENTINEL");
    return selected.chronicle_on_this_day("2024-03-01", 200, "default");
  });
  std::vector<std::string> history_slugs;
  for (const auto& hit : history) {
    history_slugs.push_back(hit.slug);
    QB_CHECK(hit.source_id == "default");
    QB_CHECK(hit.matched_at.substr(5, 5) == "03-01");
    QB_CHECK(hit.matched_at.substr(0, 4) < "2024");
    QB_CHECK(hit.years_ago > 0);
  }
  QB_CHECK(std::find(history_slugs.begin(), history_slugs.end(), "history-created") !=
           history_slugs.end());
  QB_CHECK(std::find(history_slugs.begin(), history_slugs.end(), "history-updated") !=
           history_slugs.end());
  QB_CHECK(std::find(history_slugs.begin(), history_slugs.end(), "history-both") !=
           history_slugs.end());
  QB_CHECK(std::find(history_slugs.begin(), history_slugs.end(), "history-anchor") ==
           history_slugs.end());
  QB_CHECK(std::find(history_slugs.begin(), history_slugs.end(), "history-future") ==
           history_slugs.end());
  QB_CHECK(std::find(history_slugs.begin(), history_slugs.end(), "history-other-day") ==
           history_slugs.end());
  QB_CHECK(std::find(history_slugs.begin(), history_slugs.end(), "history-deleted") ==
           history_slugs.end());
  QB_CHECK(std::find(history_slugs.begin(), history_slugs.end(), "TEAM_HISTORY_SENTINEL") ==
           history_slugs.end());
  QB_CHECK(history_slugs[0] == "history-tie-second");
  QB_CHECK(history_slugs[1] == "history-tie-first");
  const auto both_it = std::find_if(history.begin(), history.end(), [](const auto& hit) {
    return hit.slug == "history-both";
  });
  QB_CHECK(both_it != history.end());
  QB_CHECK(both_it->matched_at == "2023-03-01 10:00:00");
  QB_CHECK(both_it->years_ago == 1);

  auto year_boundary = matrix.read("on-this-day:year-boundary-limit", [&] {
    return call_op(selected, "chronicle_on_this_day",
                   {{"date", "2024-01-01"}, {"limit", "1"}});
  });
  QB_CHECK(year_boundary.ok);
  const auto year_boundary_rows = parse_result(year_boundary);
  QB_CHECK(year_boundary_rows.size() == 1);
  QB_CHECK(year_boundary_rows[0].at("slug") == "history-tie-second");

  const auto leap = put_page(selected, "default", "history-leap", "leap", "note");
  set_times(selected, leap.id, "2000-02-29 01:00:00", "2020-02-29 02:00:00");
  auto leap_hits = matrix.read("on-this-day:leap", [&] {
    return selected.chronicle_on_this_day("2024-02-29", 20, "default");
  });
  QB_CHECK(leap_hits.size() == 1);
  QB_CHECK(leap_hits[0].slug == "history-leap");
  QB_CHECK(leap_hits[0].years_ago == 4);

  const auto century_leap = put_page(selected, "default", "history-century-leap",
                                     "century-leap", "note");
  set_times(selected, century_leap.id, "1600-02-29 01:00:00", "1600-02-29 02:00:00");
  auto century_hits = matrix.read("on-this-day:century-leap", [&] {
    return selected.chronicle_on_this_day("2000-02-29", 20, "default");
  });
  QB_CHECK(century_hits.size() == 1);
  QB_CHECK(century_hits[0].slug == "history-century-leap");
  QB_CHECK(century_hits[0].years_ago == 400);

  auto recurring_leap = matrix.read("on-this-day:recurring-leap-day", [&] {
    return call_op(selected, "chronicle_on_this_day", {{"mmdd", "02-29"}});
  });
  QB_CHECK(recurring_leap.ok);
  const auto recurring_rows = parse_result(recurring_leap);
  QB_CHECK(std::any_of(recurring_rows.begin(), recurring_rows.end(), [](const auto& row) {
    return row.at("slug") == "history-leap";
  }));

  const std::vector<std::string> bad_dates = {"", "2024-2-29", "2023-02-29", "1900-02-29",
                                               "2024-04-31", "2024-03-01x", " 2024-03-01"};
  for (size_t index = 0; index < bad_dates.size(); ++index) {
    auto rejected = matrix.read("on-this-day:bad-date:" + std::to_string(index), [&] {
      return call_op(selected, "chronicle_on_this_day", {{"date", bad_dates[index]}});
    });
    require_error(rejected, "invalid_argument", "date");
  }
  const std::vector<std::string> bad_mmdd = {"", "2-29", "02-30", "03/01", "03-01x"};
  for (size_t index = 0; index < bad_mmdd.size(); ++index) {
    auto rejected = matrix.read("on-this-day:bad-mmdd:" + std::to_string(index), [&] {
      return call_op(selected, "chronicle_on_this_day", {{"mmdd", bad_mmdd[index]}});
    });
    require_error(rejected, "invalid_argument", "mmdd");
  }
  auto conflict = matrix.read("on-this-day:alias-conflict", [&] {
    return call_op(selected, "chronicle_on_this_day",
                   {{"date", "2024-03-01"}, {"mmdd", "04-01"}});
  });
  require_error(conflict, "invalid_argument", "date");
  auto matching_alias = matrix.read("on-this-day:alias-match", [&] {
    return call_op(selected, "chronicle_on_this_day",
                   {{"date", "2024-03-01"}, {"mmdd", "03-01"}, {"limit", "200"}});
  });
  QB_CHECK(matching_alias.ok);
  const auto matching_rows = parse_result(matching_alias);
  QB_CHECK(matching_rows.size() == history.size());
  for (const auto& row : matching_rows)
    require_keys(row, {"source_id", "slug", "title", "type", "created_at", "updated_at",
                       "matched_at", "years_ago"});
  auto repeat_alias = matrix.read("on-this-day:repeat-byte-stability", [&] {
    return call_op(selected, "chronicle_on_this_day",
                   {{"date", "2024-03-01"}, {"mmdd", "03-01"}, {"limit", "200"}});
  });
  QB_CHECK(repeat_alias.ok);
  QB_CHECK(repeat_alias.json == matching_alias.json);
  const auto long_row = std::find_if(matching_rows.begin(), matching_rows.end(), [](const auto& row) {
    return row.at("slug") == "history-long-title";
  });
  QB_CHECK(long_row != matching_rows.end());
  const auto rendered_title = long_row->at("title").get<std::string>();
  QB_CHECK(rendered_title.size() <= 512);
  QB_CHECK(rendered_title.ends_with("[truncated]"));
  QB_CHECK(matching_alias.json.find("BODY_SECRET_LONG_TITLE") == std::string::npos);

  auto invalid_source = matrix.read("source:invalid", [&] {
    return call_op(selected, "chronicle_on_this_day",
                   {{"date", "2024-03-01"}, {"source_id", "CON"}});
  });
  require_error(invalid_source, "invalid_source", "source_id");
  auto unknown_source = matrix.read("source:unknown", [&] {
    return call_op(selected, "chronicle_on_this_day",
                   {{"date", "2024-03-01"}, {"source_id", "ghost"}});
  });
  require_error(unknown_source, "source_not_found", "source_id");
  auto denied_source = matrix.read("source:remote-denied", [&] {
    return call_op(selected, "chronicle_on_this_day",
                   {{"date", "2024-03-01"}, {"source_id", "team_a"}}, true, true);
  });
  require_error(denied_source, "source_not_allowed", "source_id");
  auto local_team = matrix.read("source:local-team", [&] {
    return call_op(selected, "chronicle_on_this_day",
                   {{"date", "2024-03-01"}, {"source_id", "TEAM_A"}});
  });
  QB_CHECK(local_team.ok);
  const auto local_team_rows = parse_result(local_team);
  QB_CHECK(local_team_rows.size() == 1);
  QB_CHECK(local_team_rows[0].at("slug") == "TEAM_HISTORY_SENTINEL");
  QB_CHECK(local_team_rows[0].at("source_id") == "team_a");

  const std::vector<std::string> bad_numbers = {"", "-1", "+1", " 1", "1 ", "1.0", "1x",
                                                 "18446744073709551616"};
  for (size_t index = 0; index < bad_numbers.size(); ++index) {
    auto rejected = matrix.read("on-this-day:bad-limit:" + std::to_string(index), [&] {
      return call_op(selected, "chronicle_on_this_day",
                     {{"date", "2024-03-01"}, {"limit", bad_numbers[index]}});
    });
    require_error(rejected, "invalid_argument", "limit");
  }
  auto clamped_zero = matrix.read("on-this-day:limit-zero", [&] {
    return call_op(selected, "chronicle_on_this_day",
                   {{"date", "2024-03-01"}, {"limit", "0"}});
  });
  QB_CHECK(parse_result(clamped_zero).size() == 1);
  QB_CHECK(parse_result(clamped_zero)[0].at("slug") == "history-tie-second");
  auto limit_one = matrix.read("on-this-day:limit-one", [&] {
    return call_op(selected, "chronicle_on_this_day",
                   {{"date", "2024-03-01"}, {"limit", "1"}});
  });
  QB_CHECK(parse_result(limit_one).size() == 1);
  QB_CHECK(parse_result(limit_one)[0].at("slug") == "history-tie-second");
  auto clamped_high = matrix.read("on-this-day:limit-high", [&] {
    return call_op(selected, "chronicle_on_this_day",
                   {{"date", "2024-03-01"}, {"limit", "999"}});
  });
  QB_CHECK(parse_result(clamped_high).size() == history.size());

  const auto today = util::utc_date();
  const auto dynamic_page = put_page(selected, "default", "history-dynamic", "dynamic", "note");
  set_times(selected, dynamic_page.id, year_with_month_day(2000, today.substr(5, 5)) + " 00:00:00",
            "2001-01-02 00:00:00");
  auto omitted_date = matrix.read("on-this-day:omitted-date", [&] {
    return call_op(selected, "chronicle_on_this_day");
  });
  QB_CHECK(omitted_date.ok);
  const auto omitted_rows = parse_result(omitted_date);
  QB_CHECK(std::any_of(omitted_rows.begin(), omitted_rows.end(), [](const auto& row) {
    return row.at("slug") == "history-dynamic";
  }));

  const auto entity = put_page(selected, "default", "entity/exact", "entity", "note");
  set_times(selected, entity.id, "2024-03-02 12:00:00", "2024-03-01 12:00:00");
  const auto updated_entity = put_page(selected, "default", "entity/updated", "updated", "note");
  set_times(selected, updated_entity.id, "2024-03-01 12:00:00", "2024-03-03 12:00:00");
  const auto equal_entity = put_page(selected, "default", "entity/equal", "equal", "note");
  set_times(selected, equal_entity.id, "2024-03-04 12:00:00", "2024-03-04 12:00:00");
  const auto deleted_entity = put_page(selected, "default", "entity/deleted", "deleted", "note");
  set_times(selected, deleted_entity.id, "2024-03-05 12:00:00", "2024-03-06 12:00:00");
  delete_page(selected, deleted_entity.id);
  std::string max_entity(4093, 'e');
  max_entity += "\xE4\xB8\xAD";
  QB_CHECK(max_entity.size() == 4096);
  std::string oversized_multibyte_entity;
  for (size_t index = 0; index < 2049; ++index) oversized_multibyte_entity += "\xC2\xA2";
  QB_CHECK(oversized_multibyte_entity.size() == 4098);
  const auto max_entity_page = put_page(selected, "default", max_entity, "max-entity", "note");
  set_times(selected, max_entity_page.id, "2024-03-05 12:00:00", "2024-03-05 12:00:00");
  const auto today_entity = put_page(selected, "default", "entity/today", "today", "note");
  set_times(selected, today_entity.id, today + " 00:00:00", today + " 12:00:00");
  const auto team_entity = put_page(selected, "team_a", "entity/exact", "TEAM_ENTITY_SENTINEL", "note");
  set_times(selected, team_entity.id, "2024-04-01 00:00:00", "2024-04-02 00:00:00");
  const auto team_only = put_page(selected, "team_a", "entity/team-only",
                                  "TEAM_ONLY_ENTITY_SENTINEL", "note");
  set_times(selected, team_only.id, "2024-04-03 00:00:00", "2024-04-04 00:00:00");
  const auto decoy_entity = put_page(decoy, "default", "entity/exact", "DECOY_ENTITY_SENTINEL", "note");
  set_times(decoy, decoy_entity.id, "2024-05-01 00:00:00", "2024-05-02 00:00:00");
  const auto decoy_team_entity = put_page(decoy, "team_a", "entity/exact",
                                          "DECOY_TEAM_ENTITY_SENTINEL", "note");
  set_times(decoy, decoy_team_entity.id, "2024-06-01 00:00:00", "2024-06-02 00:00:00");

  auto direct_last_seen = matrix.read("last-seen:direct", [&] {
    const auto canonical_team_entity =
        selected.chronicle_last_seen("entity/exact", "TEAM_A");
    QB_CHECK(canonical_team_entity.has_value());
    QB_CHECK(canonical_team_entity->source_id == "team_a");
    QB_CHECK(canonical_team_entity->last_seen == "2024-04-02 00:00:00");
    return selected.chronicle_last_seen("entity/exact", "default");
  });
  QB_CHECK(direct_last_seen.has_value());
  QB_CHECK(direct_last_seen->source_id == "default");
  QB_CHECK(direct_last_seen->last_seen == "2024-03-02 12:00:00");

  auto last_seen = matrix.read("last-seen:exact", [&] {
    return call_op(selected, "chronicle_last_seen",
                   {{"entity", "entity/exact"}, {"asof", "2024-03-01"}});
  });
  QB_CHECK(last_seen.ok);
  const auto last_seen_json = parse_result(last_seen);
  require_keys(last_seen_json, {"source_id", "entity", "last_seen", "days_ago"});
  QB_CHECK(last_seen_json.at("source_id") == "default");
  QB_CHECK(last_seen_json.at("entity") == "entity/exact");
  QB_CHECK(last_seen_json.at("last_seen") == "2024-03-02 12:00:00");
  QB_CHECK(last_seen_json.at("days_ago") == -1);
  auto updated_last_seen = matrix.read("last-seen:updated-newer-asof-equal", [&] {
    return call_op(selected, "chronicle_last_seen",
                   {{"entity", "entity/updated"}, {"asof", "2024-03-03"}});
  });
  QB_CHECK(updated_last_seen.ok);
  QB_CHECK(parse_result(updated_last_seen).at("last_seen") == "2024-03-03 12:00:00");
  QB_CHECK(parse_result(updated_last_seen).at("days_ago") == 0);
  auto equal_last_seen = matrix.read("last-seen:equal-timestamps", [&] {
    return call_op(selected, "chronicle_last_seen",
                   {{"entity", "entity/equal"}, {"asof", "2024-03-04"}});
  });
  QB_CHECK(equal_last_seen.ok);
  QB_CHECK(parse_result(equal_last_seen).at("last_seen") == "2024-03-04 12:00:00");
  QB_CHECK(parse_result(equal_last_seen).at("days_ago") == 0);
  auto omitted_asof = matrix.read("last-seen:omitted-asof", [&] {
    return call_op(selected, "chronicle_last_seen", {{"entity", "entity/today"}});
  });
  QB_CHECK(omitted_asof.ok);
  QB_CHECK(parse_result(omitted_asof).at("days_ago") == 0);
  auto max_entity_result = matrix.read("last-seen:utf8-4096-byte-entity", [&] {
    const auto direct = selected.chronicle_last_seen(max_entity, "default");
    QB_CHECK(direct.has_value());
    QB_CHECK(direct->entity == max_entity);
    return call_op(selected, "chronicle_last_seen",
                   {{"entity", max_entity}, {"asof", "2024-03-05"}});
  });
  QB_CHECK(max_entity_result.ok);
  QB_CHECK(parse_result(max_entity_result).at("entity") == max_entity);
  QB_CHECK(parse_result(max_entity_result).at("days_ago") == 0);
  auto slug_alias = matrix.read("last-seen:slug-alias", [&] {
    return call_op(selected, "chronicle_last_seen",
                   {{"entity", "entity/exact"}, {"slug", "entity/exact"},
                    {"asof", "2024-03-03"}});
  });
  QB_CHECK(parse_result(slug_alias).at("days_ago") == 1);
  auto entity_conflict = matrix.read("last-seen:alias-conflict", [&] {
    return call_op(selected, "chronicle_last_seen",
                   {{"entity", "entity/exact"}, {"slug", "entity/other"}});
  });
  require_error(entity_conflict, "invalid_argument", "entity");
  auto missing_entity = matrix.read("last-seen:missing", [&] {
    return call_op(selected, "chronicle_last_seen", {{"entity", "missing"}});
  });
  require_error(missing_entity, "not_found", "entity");
  QB_CHECK(missing_entity.json.find("TEAM_ENTITY_SENTINEL") == std::string::npos);
  QB_CHECK(missing_entity.json.find("DECOY_ENTITY_SENTINEL") == std::string::npos);
  auto cross_source_missing = matrix.read("last-seen:cross-source-not-found", [&] {
    return call_op(selected, "chronicle_last_seen", {{"entity", "entity/team-only"}});
  });
  require_error(cross_source_missing, "not_found", "entity");
  auto deleted_missing = matrix.read("last-seen:deleted-not-found", [&] {
    return call_op(selected, "chronicle_last_seen", {{"entity", "entity/deleted"}});
  });
  require_error(deleted_missing, "not_found", "entity");
  auto no_entity = matrix.read("last-seen:required", [&] {
    return call_op(selected, "chronicle_last_seen");
  });
  require_error(no_entity, "invalid_argument", "entity");
  auto empty_entity = matrix.read("last-seen:empty", [&] {
    return call_op(selected, "chronicle_last_seen", {{"entity", ""}});
  });
  require_error(empty_entity, "invalid_argument", "entity");
  auto bad_asof = matrix.read("last-seen:bad-asof", [&] {
    return call_op(selected, "chronicle_last_seen",
                   {{"entity", "entity/exact"}, {"asof", "2024-02-30"}});
  });
  require_error(bad_asof, "invalid_argument", "asof");
  std::string malformed_entity(1, static_cast<char>(0xC3));
  auto malformed = matrix.read("last-seen:malformed-utf8", [&] {
    return call_op(selected, "chronicle_last_seen", {{"entity", malformed_entity}});
  });
  require_error(malformed, "invalid_argument", "entity");
  auto oversized = matrix.read("last-seen:oversized", [&] {
    const auto ascii = call_op(selected, "chronicle_last_seen", {{"entity", std::string(4097, 'e')}});
    require_error(ascii, "invalid_argument", "entity");
    return call_op(selected, "chronicle_last_seen",
                   {{"entity", oversized_multibyte_entity}});
  });
  require_error(oversized, "invalid_argument", "entity");
  const auto direct_malformed = matrix.read("last-seen:direct-malformed-utf8", [&] {
    return require_invalid_argument([&] {
      (void)selected.chronicle_last_seen(malformed_entity, "default");
    });
  });
  QB_CHECK(direct_malformed == "entity must be valid UTF-8 within 4096 bytes");
  const auto direct_oversized = matrix.read("last-seen:direct-oversized", [&] {
    const auto ascii = require_invalid_argument([&] {
      (void)selected.chronicle_last_seen(std::string(4097, 'e'), "default");
    });
    const auto multibyte = require_invalid_argument([&] {
      (void)selected.chronicle_last_seen(oversized_multibyte_entity, "default");
    });
    QB_CHECK(ascii == multibyte);
    return multibyte;
  });
  QB_CHECK(direct_oversized == "entity must be valid UTF-8 within 4096 bytes");

  const auto bf_new = put_page(selected, "default", "bf-new", "new", "meeting");
  const auto bf_existing = put_page(selected, "default", "bf-existing", "existing", "conversation");
  const auto bf_at = put_page(selected, "default", "bf-at", "at", "calendar-event");
  const auto bf_before = put_page(selected, "default", "bf-before", "before", "meeting");
  const auto bf_note = put_page(selected, "default", "bf-note", "note", "note");
  const auto bf_deleted = put_page(selected, "default", "bf-deleted", "deleted", "meeting");
  const auto bf_team = put_page(selected, "team_a", "BF_TEAM_SENTINEL", "team", "meeting");
  const auto bf_decoy = put_page(decoy, "default", "BF_DECOY_SENTINEL", "decoy", "meeting");
  set_times(selected, bf_new.id, "2024-05-01 00:00:00", "2024-05-02 00:00:00");
  set_times(selected, bf_existing.id, "2024-04-01 00:00:00", "2024-04-02 00:00:00");
  set_times(selected, bf_at.id, "2024-01-01 00:00:00", "2024-01-01 00:00:00");
  set_times(selected, bf_before.id, "2023-12-31 23:59:59", "2023-12-31 23:59:59");
  set_times(selected, bf_note.id, "2024-06-01 00:00:00", "2024-06-02 00:00:00");
  set_times(selected, bf_deleted.id, "2024-06-01 00:00:00", "2024-06-02 00:00:00");
  set_times(selected, bf_team.id, "2024-07-01 00:00:00", "2024-07-02 00:00:00");
  set_times(decoy, bf_decoy.id, "2024-08-01 00:00:00", "2024-08-02 00:00:00");
  delete_page(selected, bf_deleted.id);
  selected.add_tag("bf-existing", "chronicle", "default");

  struct DirectSourceRejection {
    const char* label;
    const char* source_id;
    const char* message;
  };
  for (const auto& rejection : {
           DirectSourceRejection{"empty", "", "invalid source_id"},
           DirectSourceRejection{"unknown", "ghost", "source_id is not registered"},
       }) {
    const auto on_this_day_message = matrix.read(
        "direct-source:" + std::string(rejection.label) + ":on-this-day", [&] {
          return require_invalid_argument([&] {
            (void)selected.chronicle_on_this_day("2024-03-01", 50,
                                                  rejection.source_id);
          });
        });
    QB_CHECK(on_this_day_message == rejection.message);

    const auto last_seen_message = matrix.read(
        "direct-source:" + std::string(rejection.label) + ":last-seen", [&] {
          return require_invalid_argument([&] {
            (void)selected.chronicle_last_seen("entity/exact", rejection.source_id);
          });
        });
    QB_CHECK(last_seen_message == rejection.message);

    const auto backfill_message = matrix.read(
        "direct-source:" + std::string(rejection.label) + ":backfill", [&] {
          return require_invalid_argument([&] {
            (void)selected.chronicle_backfill(rejection.source_id, std::nullopt,
                                               1000, false);
          });
        });
    QB_CHECK(backfill_message == rejection.message);
  }

  auto dry_run = matrix.read("backfill:dry-run", [&] {
    return call_op(selected, "chronicle_backfill",
                   {{"since", "2024-01-01"}, {"limit", "1000"}, {"dry_run", "true"}});
  });
  QB_CHECK(dry_run.ok);
  const auto dry_json = parse_result(dry_run);
  require_keys(dry_json, {"source_id", "scanned", "eligible", "tagged", "already_tagged",
                          "dry_run"});
  QB_CHECK(dry_json.at("source_id") == "default");
  QB_CHECK(dry_json.at("scanned") == 3);
  QB_CHECK(dry_json.at("eligible") == 3);
  QB_CHECK(dry_json.at("tagged") == 0);
  QB_CHECK(dry_json.at("already_tagged") == 1);
  QB_CHECK(dry_json.at("dry_run") == true);
  auto direct_dry_run = matrix.read("backfill:direct-dry-run", [&] {
    const auto canonical_team_dry_run = selected.chronicle_backfill(
        "TEAM_A", std::optional<std::string>{"2024-01-01"}, 1000, true);
    QB_CHECK(canonical_team_dry_run.source_id == "team_a");
    QB_CHECK(canonical_team_dry_run.scanned == 1);
    QB_CHECK(canonical_team_dry_run.eligible == 1);
    QB_CHECK(canonical_team_dry_run.tagged == 0);
    QB_CHECK(canonical_team_dry_run.already_tagged == 0);
    QB_CHECK(canonical_team_dry_run.dry_run);
    return selected.chronicle_backfill("default", std::optional<std::string>{"2024-01-01"},
                                       1000, true);
  });
  QB_CHECK(direct_dry_run.scanned == 3 && direct_dry_run.eligible == 3);
  QB_CHECK(direct_dry_run.tagged == 0 && direct_dry_run.already_tagged == 1);
  auto omitted_limit = matrix.read("backfill:omitted-limit", [&] {
    return call_op(selected, "chronicle_backfill",
                   {{"since", "2024-01-01"}, {"dry_run", "true"}});
  });
  QB_CHECK(parse_result(omitted_limit).at("scanned") == 3);
  auto timestamp_t_inclusive = matrix.read("backfill:since-timestamp-t-inclusive", [&] {
    return call_op(selected, "chronicle_backfill",
                   {{"since", "2024-01-01T00:00:00Z"}, {"dry_run", "true"}});
  });
  QB_CHECK(timestamp_t_inclusive.ok);
  QB_CHECK(parse_result(timestamp_t_inclusive).at("scanned") == 3);
  auto timestamp_space_inclusive =
      matrix.read("backfill:since-timestamp-space-inclusive", [&] {
        return call_op(selected, "chronicle_backfill",
                       {{"since", "2024-01-01 00:00:00Z"}, {"dry_run", "true"}});
      });
  QB_CHECK(timestamp_space_inclusive.ok);
  QB_CHECK(parse_result(timestamp_space_inclusive).at("scanned") == 3);
  auto zero_limit = matrix.read("backfill:limit-zero", [&] {
    return call_op(selected, "chronicle_backfill",
                   {{"since", "2024-01-01"}, {"limit", "0"}, {"dry_run", "true"}});
  });
  QB_CHECK(parse_result(zero_limit).at("scanned") == 1);
  auto above_limit = matrix.read("backfill:limit-above-maximum", [&] {
    return call_op(selected, "chronicle_backfill",
                   {{"since", "2024-01-01"}, {"limit", "1001"}, {"dry_run", "true"}});
  });
  QB_CHECK(parse_result(above_limit).at("scanned") == 3);

  const auto selected_before_real = logical_snapshot(selected);
  const auto decoy_before_real = logical_snapshot(decoy);
  const auto schema_before_real = schema_snapshot(selected);
  const auto schema_objects_before_real = schema_objects(selected);
  const auto non_tag_before_real = delta_table_evidence(selected);
  const auto tags_before_real = tag_rows(selected);
  const auto sequence_before_real = sqlite_sequence_rows(selected);
  auto real_run = call_op(selected, "chronicle_backfill",
                           {{"since", "2024-01-01"}, {"limit", "1000"}});
  QB_CHECK(real_run.ok);
  const auto real_json = parse_result(real_run);
  QB_CHECK(real_json.at("scanned") == 3);
  QB_CHECK(real_json.at("eligible") == 3);
  QB_CHECK(real_json.at("tagged") == 2);
  QB_CHECK(real_json.at("already_tagged") == 1);
  QB_CHECK(real_json.at("dry_run") == false);
  QB_CHECK(tag_count(selected, "default", "bf-new") == 1);
  QB_CHECK(tag_count(selected, "default", "bf-existing") == 1);
  QB_CHECK(tag_count(selected, "default", "bf-at") == 1);
  QB_CHECK(tag_count(selected, "default", "bf-before") == 0);
  QB_CHECK(tag_count(selected, "default", "bf-note") == 0);
  QB_CHECK(tag_count(selected, "default", "bf-deleted") == 0);
  QB_CHECK(tag_count(selected, "team_a", "BF_TEAM_SENTINEL") == 0);
  const auto selected_after_real = logical_snapshot(selected);
  const auto decoy_after_real = logical_snapshot(decoy);
  const auto schema_after_real = schema_snapshot(selected);
  const auto schema_objects_after_real = schema_objects(selected);
  const auto non_tag_after_real = delta_table_evidence(selected);
  const auto tags_after_real = tag_rows(selected);
  const auto sequence_after_real = sqlite_sequence_rows(selected);
  const auto added_tags = changed_rows(tags_before_real, tags_after_real);
  const auto removed_tags = removed_rows(tags_before_real, tags_after_real);
  const json expected_added_tags = json::array(
      {{{"page_id", bf_new.id}, {"source_id", "default"}, {"slug", "bf-new"},
        {"tag", "chronicle"}},
       {{"page_id", bf_at.id}, {"source_id", "default"}, {"slug", "bf-at"},
        {"tag", "chronicle"}}});
  QB_CHECK(selected_before_real != selected_after_real);
  QB_CHECK(decoy_before_real == decoy_after_real);
  QB_CHECK(schema_before_real == schema_after_real);
  QB_CHECK(schema_objects_before_real == schema_objects_after_real);
  QB_CHECK(non_tag_before_real == non_tag_after_real);
  QB_CHECK(sequence_before_real == sequence_after_real);
  QB_CHECK(added_tags == expected_added_tags);
  QB_CHECK(removed_tags.empty());
  const auto allowed_delta = make_allowed_delta(
      selected_before_real, selected_after_real, decoy_before_real, decoy_after_real,
      schema_before_real, schema_after_real, schema_objects_before_real,
      schema_objects_after_real, non_tag_before_real, non_tag_after_real, tags_before_real,
      tags_after_real, sequence_before_real, sequence_after_real,
      real_json.at("tagged").get<int>(), real_json.at("already_tagged").get<int>());
  std::cout << "N23_ALLOWED_DELTA_JSON=" << allowed_delta.dump() << "\n";

  auto idempotent = matrix.read("backfill:idempotent", [&] {
    return call_op(selected, "chronicle_backfill",
                   {{"since", "2024-01-01"}, {"limit", "1000"}});
  });
  QB_CHECK(idempotent.ok);
  QB_CHECK(parse_result(idempotent).at("tagged") == 0);
  QB_CHECK(parse_result(idempotent).at("already_tagged") == 3);
  auto explicit_false = matrix.read("backfill:explicit-false-idempotent", [&] {
    return call_op(selected, "chronicle_backfill",
                   {{"since", "2024-01-01"}, {"limit", "1000"}, {"dry_run", "false"}});
  });
  QB_CHECK(explicit_false.ok);
  QB_CHECK(parse_result(explicit_false).at("dry_run") == false);
  QB_CHECK(parse_result(explicit_false).at("tagged") == 0);

  const auto limit_old = put_page(selected, "team_a", "bf-limit-old", "old", "meeting");
  const auto limit_new = put_page(selected, "team_a", "bf-limit-new", "new", "meeting");
  set_times(selected, limit_old.id, "2024-08-01 00:00:00", "2024-08-01 00:00:00");
  set_times(selected, limit_new.id, "2024-09-01 00:00:00", "2024-09-01 00:00:00");
  const auto limited_selected_before = logical_snapshot(selected);
  const auto limited_decoy_before = logical_snapshot(decoy);
  const auto limited_schema_before = schema_snapshot(selected);
  const auto limited_non_tag_before = delta_table_evidence(selected);
  const auto limited_tags_before = tag_rows(selected);
  const auto limited_sequence_before = sqlite_sequence_rows(selected);
  auto limited = call_op(selected, "chronicle_backfill",
                          {{"source_id", "team_a"}, {"limit", "1"}});
  QB_CHECK(limited.ok);
  QB_CHECK(parse_result(limited).at("tagged") == 1);
  QB_CHECK(tag_count(selected, "team_a", "bf-limit-new") == 1);
  QB_CHECK(tag_count(selected, "team_a", "bf-limit-old") == 0);
  const auto limited_selected_after = logical_snapshot(selected);
  const auto limited_tags_after = tag_rows(selected);
  QB_CHECK(limited_selected_before != limited_selected_after);
  QB_CHECK(limited_decoy_before == logical_snapshot(decoy));
  QB_CHECK(limited_schema_before == schema_snapshot(selected));
  QB_CHECK(limited_non_tag_before == delta_table_evidence(selected));
  QB_CHECK(limited_sequence_before == sqlite_sequence_rows(selected));
  const json expected_limited_tag = json::array(
      {{{"page_id", limit_new.id}, {"source_id", "team_a"}, {"slug", "bf-limit-new"},
        {"tag", "chronicle"}}});
  QB_CHECK(changed_rows(limited_tags_before, limited_tags_after) == expected_limited_tag);
  QB_CHECK(removed_rows(limited_tags_before, limited_tags_after).empty());

  const auto rollback_first = put_page(selected, "default", "bf-rollback-first",
                                       "rollback-first", "meeting");
  const auto rollback_fail = put_page(selected, "default", "bf-rollback-fail",
                                      "rollback-fail", "conversation");
  set_times(selected, rollback_first.id, "2024-12-02 00:00:00", "2024-12-02 00:00:00");
  set_times(selected, rollback_fail.id, "2024-12-01 00:00:00", "2024-12-01 00:00:00");
  selected.db().exec(
      "CREATE TRIGGER n23_force_tag_failure BEFORE INSERT ON tags WHEN NEW.page_id=" +
      std::to_string(rollback_fail.id) +
      " AND NEW.tag='chronicle' BEGIN SELECT RAISE(IGNORE); END;");
  const auto rollback_before = logical_snapshot(selected);
  const auto rollback_decoy_before = logical_snapshot(decoy);
  const auto rollback_schema_before = schema_snapshot(selected);
  const auto rollback_tags_before = tag_rows(selected);
  const auto rollback_sequence_before = sqlite_sequence_rows(selected);
  int attempted_tag_inserts = 0;
  sqlite3_update_hook(selected.db().handle(), count_tag_inserts, &attempted_tag_inserts);
  auto rolled_back = call_op(selected, "chronicle_backfill",
                             {{"since", "2024-12-01"}, {"limit", "2"}});
  sqlite3_update_hook(selected.db().handle(), nullptr, nullptr);
  require_error(rolled_back, "database_error", "database");
  QB_CHECK(attempted_tag_inserts == 1);
  QB_CHECK(logical_snapshot(selected) == rollback_before);
  QB_CHECK(logical_snapshot(decoy) == rollback_decoy_before);
  QB_CHECK(schema_snapshot(selected) == rollback_schema_before);
  QB_CHECK(tag_rows(selected) == rollback_tags_before);
  QB_CHECK(sqlite_sequence_rows(selected) == rollback_sequence_before);
  QB_CHECK(tag_count(selected, "default", "bf-rollback-first") == 0);
  QB_CHECK(tag_count(selected, "default", "bf-rollback-fail") == 0);
  g_snapshots.push_back({"backfill:mid-transaction-rollback", snapshot_sha256(rollback_before),
                         snapshot_sha256(logical_snapshot(selected)),
                         snapshot_sha256(rollback_decoy_before),
                         snapshot_sha256(logical_snapshot(decoy))});
  selected.db().exec("DROP TRIGGER n23_force_tag_failure;");

  const auto busy_page = put_page(selected, "default", "bf-busy", "busy", "meeting");
  set_times(selected, busy_page.id, "2024-10-01 00:00:00", "2024-10-01 00:00:00");
  const auto busy_before = logical_snapshot(selected);
  const auto busy_decoy_before = logical_snapshot(decoy);
  Brain locker("n23-locker");
  locker.open_at(util::path_to_utf8(selected_path));
  locker.db().exec("BEGIN IMMEDIATE;");
  auto busy = call_op(selected, "chronicle_backfill",
                      {{"since", "2024-10-01"}, {"limit", "1"}});
  require_error(busy, "database_busy", "database");
  locker.db().exec("ROLLBACK;");
  locker.close();
  QB_CHECK(logical_snapshot(selected) == busy_before);
  QB_CHECK(logical_snapshot(decoy) == busy_decoy_before);
  QB_CHECK(tag_count(selected, "default", "bf-busy") == 0);
  g_snapshots.push_back({"backfill:busy", snapshot_sha256(busy_before),
                         snapshot_sha256(logical_snapshot(selected)),
                         snapshot_sha256(busy_decoy_before),
                         snapshot_sha256(logical_snapshot(decoy))});

  for (const auto& value : bad_numbers) {
    auto rejected = matrix.read("backfill:bad-limit:" + value, [&] {
      return call_op(selected, "chronicle_backfill", {{"limit", value}});
    });
    require_error(rejected, "invalid_argument", "limit");
  }
  const std::vector<std::string> bad_since = {"", "2024-1-01", "2024-02-30",
                                               "2024-01-01T00:00:00", "2024-01-01Z",
                                               "2024-01-01T24:00:00Z", "2024-01-01T00:00:00+00:00"};
  for (size_t index = 0; index < bad_since.size(); ++index) {
    auto rejected = matrix.read("backfill:bad-since:" + std::to_string(index), [&] {
      return call_op(selected, "chronicle_backfill", {{"since", bad_since[index]}});
    });
    require_error(rejected, "invalid_argument", "since");
  }
  auto bad_bool = matrix.read("backfill:bad-bool", [&] {
    for (const std::string value : {"0", "TRUE", "False", "false "}) {
      const auto rejected = call_op(selected, "chronicle_backfill", {{"dry_run", value}});
      require_error(rejected, "invalid_argument", "dry_run");
    }
    return call_op(selected, "chronicle_backfill", {{"dry_run", "1"}});
  });
  require_error(bad_bool, "invalid_argument", "dry_run");

  const auto* on_this_day_op = ops::global_registry().find("chronicle_on_this_day");
  const auto* last_seen_op = ops::global_registry().find("chronicle_last_seen");
  const auto* backfill_op = ops::global_registry().find("chronicle_backfill");
  QB_CHECK(on_this_day_op && last_seen_op && backfill_op);
  QB_CHECK(on_this_day_op->scope == ops::Scope::Read && !on_this_day_op->local_only);
  QB_CHECK(last_seen_op->scope == ops::Scope::Read && !last_seen_op->local_only);
  QB_CHECK(backfill_op->scope == ops::Scope::Write && backfill_op->local_only);
  for (const auto* operation : {on_this_day_op, last_seen_op, backfill_op}) {
    QB_CHECK(operation->description.find("Qbrain Chronicle subset") != std::string::npos);
    QB_CHECK(operation->description.find("authorized canonical source") != std::string::npos);
    QB_CHECK(operation->description.find("no timeline-event storage") != std::string::npos);
    QB_CHECK(operation->description.find("extraction jobs") != std::string::npos);
    QB_CHECK(operation->description.find("narrative generation") != std::string::npos);
    QB_CHECK(operation->description.find("full Chronicle parity") != std::string::npos);
  }
  const auto on_schema = json::parse(on_this_day_op->input_schema_json);
  const auto last_schema = json::parse(last_seen_op->input_schema_json);
  const auto backfill_schema = json::parse(backfill_op->input_schema_json);
  const json source_schema = {{"type", "string"},
                              {"minLength", 1},
                              {"maxLength", 64},
                              {"x-maxUtf8Bytes", 64},
                              {"pattern", "^(?!(?:[Cc][Oo][Nn]|[Pp][Rr][Nn]|[Aa][Uu][Xx]|[Nn][Uu][Ll]|[Cc][Oo][Mm][1-9]|[Ll][Pp][Tt][1-9])$)[A-Za-z0-9_-]+$"},
                              {"description", "1-64 ASCII bytes; canonicalized to lowercase; Windows reserved device names rejected."},
                              {"default", "default"}};
  const json expected_on_schema = {
      {"type", "object"},
      {"additionalProperties", false},
      {"properties",
       {{"source_id", source_schema},
        {"date", {{"type", "string"}, {"minLength", 10}, {"maxLength", 10},
                  {"pattern", "^[0-9]{4}-[0-9]{2}-[0-9]{2}$"}}},
        {"mmdd", {{"type", "string"}, {"minLength", 5}, {"maxLength", 5},
                  {"pattern", "^[0-9]{2}-[0-9]{2}$"}}},
        {"limit", {{"type", "integer"}, {"minimum", 0}, {"maximum", 200},
                   {"default", 50}}}}}};
  const json expected_last_schema = {
      {"type", "object"},
      {"additionalProperties", false},
      {"properties",
        {{"source_id", source_schema},
        {"entity", {{"type", "string"}, {"minLength", 1}, {"x-maxUtf8Bytes", 4096},
                    {"description", "Valid UTF-8 entity identifier; at most 4096 UTF-8 bytes."}}},
        {"slug", {{"type", "string"}, {"minLength", 1}, {"x-maxUtf8Bytes", 4096},
                  {"description", "Legacy entity alias; valid UTF-8 and at most 4096 UTF-8 bytes."}}},
        {"asof", {{"type", "string"}, {"minLength", 10}, {"maxLength", 10},
                  {"pattern", "^[0-9]{4}-[0-9]{2}-[0-9]{2}$"}}}}},
      {"anyOf", json::array({json{{"required", json::array({"entity"})}},
                              json{{"required", json::array({"slug"})}}})}};
  const json expected_backfill_schema = {
      {"type", "object"},
      {"additionalProperties", false},
      {"properties",
       {{"source_id", source_schema},
        {"since", {{"type", "string"}, {"minLength", 10}, {"maxLength", 20},
                   {"pattern", "^[0-9]{4}-[0-9]{2}-[0-9]{2}([T ][0-9]{2}:[0-9]{2}:[0-9]{2}Z)?$"}}},
        {"limit", {{"type", "integer"}, {"minimum", 0}, {"maximum", 1000},
                   {"default", 1000}}},
        {"dry_run", {{"type", "boolean"}, {"default", false}}}}}};
  QB_CHECK(on_schema == expected_on_schema);
  QB_CHECK(last_schema == expected_last_schema);
  QB_CHECK(backfill_schema == expected_backfill_schema);
  const auto tools = mcp_tools(selected);
  QB_CHECK(tool_by_name(tools, "chronicle_on_this_day").at("inputSchema") == on_schema);
  QB_CHECK(tool_by_name(tools, "chronicle_last_seen").at("inputSchema") == last_schema);
  QB_CHECK(tool_by_name(tools, "chronicle_backfill").at("inputSchema") == backfill_schema);

  ScopedEnvironmentVariable ambient("QBRAIN_SOURCE", "team_a");
  auto typed_limit = matrix.read("mcp:typed-unsigned-limit", [&] {
    return mcp_call(selected, "chronicle_on_this_day",
                    json{{"date", "2024-03-01"}, {"limit", json::number_unsigned_t{1}}});
  });
  QB_CHECK(typed_limit.at("result").at("isError") == false);
  QB_CHECK(mcp_payload_json(typed_limit).size() == 1);
  auto typed_false = matrix.read("mcp:typed-false", [&] {
    return mcp_call(selected, "chronicle_backfill",
                    json{{"since", "9999-12-31"},
                         {"limit", json::number_unsigned_t{1000}},
                         {"dry_run", false}},
                    true);
  });
  QB_CHECK(typed_false.at("result").at("isError") == false);
  QB_CHECK(mcp_payload_json(typed_false).at("dry_run") == false);
  auto ambient_default = matrix.read("mcp:ambient-default", [&] {
    return mcp_call(selected, "chronicle_on_this_day", {{"date", "2024-03-01"}});
  });
  QB_CHECK(ambient_default.at("result").at("isError") == false);
  const auto ambient_rows = mcp_payload_json(ambient_default);
  QB_CHECK(std::none_of(ambient_rows.begin(), ambient_rows.end(), [](const auto& row) {
    return row.at("slug") == "TEAM_HISTORY_SENTINEL";
  }));

  auto mcp_last = matrix.read("mcp:last-seen", [&] {
    return mcp_call(selected, "chronicle_last_seen",
                    {{"entity", "entity/exact"}, {"asof", "2024-03-03"}});
  });
  QB_CHECK(mcp_last.at("result").at("isError") == false);
  QB_CHECK(mcp_payload_json(mcp_last).at("days_ago") == 1);
  auto denied_write = matrix.read("mcp:write-denied", [&] {
    return mcp_call(selected, "chronicle_backfill", {{"dry_run", true}}, false);
  });
  require_mcp_error(denied_write, "write_denied", "operation");
  auto allowed_dry = matrix.read("mcp:write-enabled-dry-run", [&] {
    return mcp_call(selected, "chronicle_backfill", {{"dry_run", true}}, true);
  });
  QB_CHECK(allowed_dry.at("result").at("isError") == false);
  QB_CHECK(mcp_payload_json(allowed_dry).at("dry_run") == true);

  auto unauthorized = matrix.read("mcp:source-not-allowed", [&] {
    return mcp_call(selected, "chronicle_backfill",
                    {{"source_id", "other"}, {"dry_run", false}}, true);
  });
  require_mcp_error(unauthorized, "source_not_allowed", "source_id");
  selected.save_config_value("mcp.allowed_sources", "TEAM_A");
  auto authorized = matrix.read("mcp:authorized-source-dry-run", [&] {
    return mcp_call(selected, "chronicle_backfill",
                    {{"source_id", "team_a"}, {"dry_run", true}}, true);
  });
  QB_CHECK(authorized.at("result").at("isError") == false);
  QB_CHECK(mcp_payload_json(authorized).at("source_id") == "team_a");

  struct McpRejection {
    std::string label;
    std::string operation;
    std::string field;
    json arguments;
  };
  const std::vector<McpRejection> wrong_types = {
      {"on-date-bool", "chronicle_on_this_day", "date", {{"date", true}}},
      {"on-date-null", "chronicle_on_this_day", "date", {{"date", nullptr}}},
      {"on-mmdd-object", "chronicle_on_this_day", "mmdd", {{"mmdd", json::object()}}},
      {"on-limit-array", "chronicle_on_this_day", "limit", {{"limit", json::array()}}},
      {"on-limit-signed", "chronicle_on_this_day", "limit", {{"limit", -1}}},
      {"on-source-object", "chronicle_on_this_day", "source_id",
       {{"source_id", json::object()}}},
      {"last-entity-number", "chronicle_last_seen", "entity", {{"entity", 23}}},
      {"last-entity-null", "chronicle_last_seen", "entity", {{"entity", nullptr}}},
      {"last-entity-object", "chronicle_last_seen", "entity",
       {{"entity", json::object()}}},
      {"last-entity-array", "chronicle_last_seen", "entity", {{"entity", json::array()}}},
      {"last-asof-bool", "chronicle_last_seen", "asof", {{"asof", true}}},
      {"backfill-since-null", "chronicle_backfill", "since", {{"since", nullptr}}},
      {"backfill-since-bool", "chronicle_backfill", "since", {{"since", true}}},
      {"backfill-since-object", "chronicle_backfill", "since",
       {{"since", json::object()}}},
      {"backfill-since-array", "chronicle_backfill", "since",
       {{"since", json::array()}}},
      {"backfill-limit-float", "chronicle_backfill", "limit", {{"limit", 1.5}}},
      {"backfill-limit-string", "chronicle_backfill", "limit", {{"limit", "1"}}},
      {"backfill-dry-string", "chronicle_backfill", "dry_run", {{"dry_run", "true"}}},
      {"backfill-dry-null", "chronicle_backfill", "dry_run", {{"dry_run", nullptr}}},
      {"backfill-dry-array", "chronicle_backfill", "dry_run",
       {{"dry_run", json::array()}}},
  };
  for (const auto& item : wrong_types) {
    auto rejected = matrix.read("mcp:wrong-type:" + item.label, [&] {
      return mcp_call(selected, item.operation, item.arguments, true);
    });
    require_mcp_error(rejected, "invalid_argument", item.field);
  }
  for (const auto& [operation, arguments] :
       std::vector<std::pair<std::string, json>>{{"chronicle_on_this_day", json::array()},
                                                  {"chronicle_last_seen", nullptr},
                                                  {"chronicle_backfill", true}}) {
    auto non_object = matrix.read("mcp:non-object:" + operation, [&] {
      return mcp_call(selected, operation, arguments, true);
    });
    require_mcp_error(non_object, "invalid_argument", "arguments");
  }
  for (const std::string operation : {"chronicle_on_this_day", "chronicle_last_seen",
                                      "chronicle_backfill"}) {
    auto mcp_unknown = matrix.read("mcp:unknown-field:" + operation, [&] {
      return mcp_call(selected, operation, {{"unknown", "x"}}, true);
    });
    require_mcp_error(mcp_unknown, "invalid_argument", "unknown");
    auto local_unknown = matrix.read("local:unknown-field:" + operation, [&] {
      return call_op(selected, operation, {{"unknown", "x"}});
    });
    require_error(local_unknown, "invalid_argument", "unknown");
  }

  for (size_t index = 0; index < g_snapshots.size(); ++index) {
    const auto& row = g_snapshots[index];
    QB_CHECK(row.selected_before == row.selected_after);
    QB_CHECK(row.decoy_before == row.decoy_after);
    std::cout << "N23_SNAPSHOT " << (index + 1) << " " << hex_encode(row.label) << " "
              << row.selected_before << " " << row.selected_after << " "
              << row.decoy_before << " " << row.decoy_after << "\n";
  }
  std::cout << "N23_ON_THIS_DAY_MATRIX=pass\n";
  std::cout << "N23_LAST_SEEN_MATRIX=pass\n";
  std::cout << "N23_BACKFILL_MATRIX=pass\n";
  std::cout << "N23_REGISTRY_MCP_MATRIX=pass\n";
  std::cout << "N23_SOURCE_BRAIN_ISOLATION=pass\n";
  std::cout << "N23_SNAPSHOT_COUNT=" << g_snapshots.size() << "\n";

  selected.close();
  decoy.close();
}
