#include "qbrain/ingest/import.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/service/live_sync.hpp"
#include "qbrain/storage/database.hpp"
#include "qbrain/util/paths.hpp"
#include "wave3_test_support.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#define QB_CHECK(cond)                                                               \
  do {                                                                               \
    if (!(cond)) throw std::runtime_error(std::string("CHECK failed: ") + #cond);    \
  } while (0)

namespace {

using nlohmann::json;
using qbrain::storage::Database;
using qbrain::test_support::logical_snapshot;
using qbrain::test_support::scalar;
using qbrain::test_support::snapshot_sha256;

class EnvironmentGuard {
 public:
  EnvironmentGuard(std::string name, const std::string& value) : name_(std::move(name)) {
    if (const char* existing = std::getenv(name_.c_str())) original_ = existing;
    QB_CHECK(_putenv_s(name_.c_str(), value.c_str()) == 0);
  }

  ~EnvironmentGuard() {
    (void)_putenv_s(name_.c_str(), original_ ? original_->c_str() : "");
  }

 private:
  std::string name_;
  std::optional<std::string> original_;
};

class TemporaryRoot {
 public:
  explicit TemporaryRoot(std::filesystem::path path) : path_(std::move(path)) {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }

  ~TemporaryRoot() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

void remove_database(const std::filesystem::path& path) {
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + "-wal");
  std::filesystem::remove(path.string() + "-shm");
}

std::string quote_identifier(const std::string& identifier) {
  std::string out = "\"";
  for (char ch : identifier) {
    out.push_back(ch);
    if (ch == '"') out.push_back('"');
  }
  out.push_back('"');
  return out;
}

std::vector<std::string> snapshot_query(sqlite3* database, const std::string& sql) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("snapshot query prepare failed: " + sql);
  }
  std::vector<std::string> rows;
  int result = SQLITE_OK;
  while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
    std::string row;
    for (int column = 0; column < sqlite3_column_count(statement); ++column) {
      if (column) row.push_back('|');
      row += qbrain::test_support::snapshot_cell(statement, column);
    }
    rows.push_back(std::move(row));
  }
  sqlite3_finalize(statement);
  if (result != SQLITE_DONE) throw std::runtime_error("snapshot query scan failed: " + sql);
  std::sort(rows.begin(), rows.end());
  return rows;
}

std::string database_snapshot(Database& database) {
  sqlite3* handle = database.handle();
  std::string out = "schema\n";
  const auto schema_rows = snapshot_query(
      handle,
      "SELECT type,name,COALESCE(tbl_name,''),COALESCE(sql,'') FROM sqlite_master "
      "WHERE name NOT LIKE 'sqlite_%' OR name='sqlite_sequence'");
  for (const auto& row : schema_rows) out += row + "\n";

  sqlite3_stmt* tables_statement = nullptr;
  const char* tables_sql =
      "SELECT name FROM sqlite_master WHERE type='table' AND "
      "(name NOT LIKE 'sqlite_%' OR name='sqlite_sequence') ORDER BY name";
  if (sqlite3_prepare_v2(handle, tables_sql, -1, &tables_statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("snapshot table discovery failed");
  }
  std::vector<std::string> tables;
  int result = SQLITE_OK;
  while ((result = sqlite3_step(tables_statement)) == SQLITE_ROW) {
    const auto* name = sqlite3_column_text(tables_statement, 0);
    if (name) tables.emplace_back(reinterpret_cast<const char*>(name));
  }
  sqlite3_finalize(tables_statement);
  if (result != SQLITE_DONE) throw std::runtime_error("snapshot table discovery scan failed");

  for (const auto& table : tables) {
    out += "table:" + table + "\n";
    for (const auto& row : snapshot_query(handle, "SELECT * FROM " + quote_identifier(table))) {
      out += row + "\n";
    }
  }
  return out;
}

void create_v11_ingest_fixture(const std::filesystem::path& path) {
  remove_database(path);
  Database database;
  database.open(qbrain::util::path_to_utf8(path));
  database.exec(R"SQL(
CREATE TABLE schema_version (
  version INTEGER NOT NULL PRIMARY KEY,
  applied_at TEXT NOT NULL DEFAULT (datetime('now'))
);
INSERT INTO schema_version(version) VALUES (11);
CREATE TABLE sources (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL DEFAULT ''
);
INSERT INTO sources(id,name) VALUES ('default','default'),('team_a','team_a');
CREATE TABLE ingest_log (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  event_type TEXT NOT NULL DEFAULT 'import',
  path TEXT NOT NULL DEFAULT '',
  detail_json TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX idx_ingest_log_created ON ingest_log(created_at DESC);
INSERT INTO ingest_log(id,event_type,path,detail_json,created_at)
VALUES (41,'import','legacy-a','{"pages":1}','2024-02-29 01:02:03'),
       (42,'sync','legacy-b','{"pages":2}','2024-02-29 01:02:04');
)SQL");
}

int schema_version(Database& database) {
  auto statement = database.prepare("SELECT COALESCE(MAX(version),0) FROM schema_version");
  return statement.step() ? static_cast<int>(statement.column_int(0)) : 0;
}

bool has_source_column(Database& database) {
  auto statement = database.prepare("PRAGMA table_info(ingest_log)");
  while (statement.step()) {
    if (statement.column_text(1) == "source_id") return true;
  }
  return false;
}

bool has_index(Database& database, const std::string& name) {
  auto statement = database.prepare(
      "SELECT 1 FROM sqlite_master WHERE type='index' AND name=? LIMIT 1");
  statement.bind_text(1, name);
  return statement.step();
}

void require_ingest_fk_contract(Database& database) {
  auto statement = database.prepare("PRAGMA foreign_key_list(ingest_log)");
  bool found = false;
  while (statement.step()) {
    if (statement.column_text(2) == "sources" && statement.column_text(3) == "source_id" &&
        statement.column_text(4) == "id" && statement.column_text(6) == "CASCADE") {
      found = true;
    }
  }
  QB_CHECK(found);
}

void require_ingest_index_contract(Database& database) {
  auto statement = database.prepare("PRAGMA index_xinfo(idx_ingest_log_source_created)");
  std::vector<std::pair<std::string, int64_t>> columns;
  while (statement.step()) {
    if (statement.column_int(5) == 1 && !statement.column_is_null(2)) {
      columns.emplace_back(statement.column_text(2), statement.column_int(3));
    }
  }
  QB_CHECK(columns.size() == 3);
  QB_CHECK(columns[0] == std::make_pair(std::string("source_id"), int64_t{0}));
  QB_CHECK(columns[1] == std::make_pair(std::string("created_at"), int64_t{1}));
  QB_CHECK(columns[2] == std::make_pair(std::string("id"), int64_t{1}));
}

int64_t count_for(qbrain::Brain& brain, const std::string& sql, const std::string& value) {
  auto statement = brain.db().prepare(sql);
  statement.bind_text(1, value);
  return statement.step() ? statement.column_int(0) : 0;
}

int64_t ingest_count(qbrain::Brain& brain, const std::string& source_id) {
  return count_for(brain, "SELECT COUNT(*) FROM ingest_log WHERE source_id=?", source_id);
}

int64_t ingest_path_count(qbrain::Brain& brain, const std::string& path) {
  return count_for(brain, "SELECT COUNT(*) FROM ingest_log WHERE path=?", path);
}

int64_t source_count(qbrain::Brain& brain, const std::string& source_id) {
  return count_for(brain, "SELECT COUNT(*) FROM sources WHERE id=?", source_id);
}

void bulk_seed_ingest(qbrain::Brain& brain, const std::string& source_id, int count,
                      const std::string& prefix) {
  QB_CHECK(brain.ensure_source(source_id));
  brain.db().exec("BEGIN;");
  try {
    auto statement = brain.db().prepare(
        "INSERT INTO ingest_log(source_id,event_type,path,detail_json,created_at) "
        "VALUES(?,'seed',?,?,'2024-01-01 00:00:00')");
    for (int index = 0; index < count; ++index) {
      statement.reset();
      statement.clear_bindings();
      statement.bind_text(1, source_id);
      statement.bind_text(2, prefix + std::to_string(index));
      statement.bind_text(3, json({{"seed", index}}).dump());
      statement.step_done();
    }
    brain.db().exec("COMMIT;");
  } catch (...) {
    try {
      brain.db().exec("ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

void require_newest_order(const std::vector<qbrain::Brain::IngestLogEntry>& rows) {
  for (size_t index = 1; index < rows.size(); ++index) {
    const auto& previous = rows[index - 1];
    const auto& current = rows[index];
    QB_CHECK(previous.created_at > current.created_at ||
             (previous.created_at == current.created_at && previous.id > current.id));
  }
}

template <typename Function>
auto read_without_mutation(qbrain::Brain& brain, Function&& function) {
  const auto before = logical_snapshot(brain);
  auto result = function();
  QB_CHECK(logical_snapshot(brain) == before);
  return result;
}

template <typename Function>
void require_no_mutation(qbrain::Brain& brain, Function&& function) {
  const auto before = logical_snapshot(brain);
  function();
  QB_CHECK(logical_snapshot(brain) == before);
}

json rpc_call(qbrain::Brain& brain, const qbrain::mcp::ServeOptions& options,
              const std::string& operation, json arguments) {
  static int64_t next_id = 1500;
  json request = {{"jsonrpc", "2.0"},
                  {"id", next_id++},
                  {"method", "tools/call"},
                  {"params", {{"name", operation}, {"arguments", std::move(arguments)}}}};
  return json::parse(qbrain::mcp::handle_rpc_body(brain, options, request.dump()));
}

bool rpc_is_error(const json& response) {
  return response.contains("error") || !response.contains("result") ||
         response["result"].value("isError", true);
}

json rpc_structured_payload(const json& response) {
  QB_CHECK(!rpc_is_error(response));
  const auto& content = response.at("result").at("content");
  for (size_t offset = 0; offset < content.size(); ++offset) {
    const auto& block = content[content.size() - 1 - offset];
    if (!block.is_object() || !block.contains("text") || !block["text"].is_string()) continue;
    try {
      auto parsed = json::parse(block["text"].get<std::string>());
      if (parsed.is_object()) return parsed;
    } catch (...) {
    }
  }
  throw std::runtime_error("MCP success did not include a structured JSON payload");
}

std::pair<std::string, std::string> page_provenance(qbrain::Brain& brain, int64_t page_id) {
  auto statement = brain.db().prepare(
      "SELECT COALESCE(source_kind,''),COALESCE(ingested_via,'') FROM pages WHERE id=?");
  statement.bind_int(1, page_id);
  QB_CHECK(statement.step());
  return {statement.column_text(0), statement.column_text(1)};
}

int64_t embed_jobs_for_page(qbrain::Brain& brain, int64_t page_id) {
  auto statement = brain.db().prepare("SELECT payload_json FROM jobs WHERE type='embed'");
  int64_t count = 0;
  while (statement.step()) {
    try {
      const auto payload = json::parse(statement.column_text(0));
      if (payload.value("page_id", int64_t{0}) == page_id) ++count;
    } catch (...) {
    }
  }
  return count;
}

qbrain::Page seed_page(qbrain::Brain& brain, const std::string& source_id,
                       const std::string& slug, const std::string& created_at,
                       const std::string& updated_at) {
  auto page = qbrain::test_support::put_page(brain, source_id, slug, "body");
  auto statement = brain.db().prepare("UPDATE pages SET created_at=?,updated_at=? WHERE id=?");
  statement.bind_text(1, created_at);
  statement.bind_text(2, updated_at);
  statement.bind_int(3, page.id);
  statement.step_done();
  page.created_at = created_at;
  page.updated_at = updated_at;
  return page;
}

bool contains_slug(const std::vector<qbrain::Brain::ChronicleHit>& rows,
                   const std::string& slug) {
  return std::any_of(rows.begin(), rows.end(), [&](const auto& row) { return row.slug == slug; });
}

void require_chronicle_order(const std::vector<qbrain::Brain::ChronicleHit>& rows) {
  for (size_t index = 1; index < rows.size(); ++index) {
    const auto& previous = rows[index - 1];
    const auto& current = rows[index];
    QB_CHECK(previous.effective_at > current.effective_at ||
             (previous.effective_at == current.effective_at && previous.id > current.id));
  }
}

void require_counter_only_ingest(const std::vector<qbrain::Brain::IngestLogEntry>& rows,
                                 const std::string& forbidden_text) {
  const std::set<std::string> import_keys = {"errors", "files", "links", "pages"};
  const std::set<std::string> sync_keys = {"errors", "imported_pages", "scanned", "skipped"};
  for (const auto& row : rows) {
    QB_CHECK(row.source_id == "team_a");
    QB_CHECK(row.detail_json.size() <= 65536);
    QB_CHECK(row.detail_json.find(forbidden_text) == std::string::npos);
    const auto detail = json::parse(row.detail_json);
    QB_CHECK(detail.is_object());
    std::set<std::string> keys;
    for (auto it = detail.begin(); it != detail.end(); ++it) {
      keys.insert(it.key());
      QB_CHECK(it.value().is_number_integer());
      QB_CHECK(it.value().get<int64_t>() >= 0);
    }
    if (row.event_type == "import") {
      QB_CHECK(keys == import_keys);
    } else if (row.event_type == "live_sync") {
      QB_CHECK(keys == sync_keys);
    } else {
      QB_CHECK(false);
    }
  }
}

struct MigrationEvidence {
  std::string migrated_hash;
  std::string rollback_hash;
  std::string cleanup_hash;
};

MigrationEvidence test_migration_matrix(const std::filesystem::path& root) {
  MigrationEvidence evidence;

  qbrain::Brain fresh("n15-migration-fresh");
  fresh.open_at(qbrain::util::path_to_utf8(root / "migration-fresh.db"));
  QB_CHECK(fresh.status_snapshot().schema_version >= 12);
  QB_CHECK(qbrain::storage::check_schema_integrity(fresh.db()).ok);
  QB_CHECK(has_source_column(fresh.db()));
  QB_CHECK(has_index(fresh.db(), "idx_ingest_log_source_created"));
  require_ingest_fk_contract(fresh.db());
  require_ingest_index_contract(fresh.db());
  fresh.close();

  const auto legacy_path = root / "legacy-v11.db";
  create_v11_ingest_fixture(legacy_path);
  {
    Database database;
    database.open(qbrain::util::path_to_utf8(legacy_path));
    QB_CHECK(schema_version(database) == 11);
    qbrain::storage::apply_migrations(database);
    QB_CHECK(schema_version(database) >= 12);
    QB_CHECK(has_source_column(database));
    QB_CHECK(!has_index(database, "idx_ingest_log_created"));
    QB_CHECK(has_index(database, "idx_ingest_log_source_created"));
    require_ingest_fk_contract(database);
    require_ingest_index_contract(database);

    auto rows = database.prepare(
        "SELECT id,source_id,event_type,path,detail_json,created_at FROM ingest_log ORDER BY id");
    QB_CHECK(rows.step());
    QB_CHECK(rows.column_int(0) == 41);
    QB_CHECK(rows.column_text(1) == "default");
    QB_CHECK(rows.column_text(2) == "import");
    QB_CHECK(rows.column_text(3) == "legacy-a");
    QB_CHECK(rows.column_text(4) == R"({"pages":1})");
    QB_CHECK(rows.column_text(5) == "2024-02-29 01:02:03");
    QB_CHECK(rows.step());
    QB_CHECK(rows.column_int(0) == 42);
    QB_CHECK(rows.column_text(1) == "default");
    QB_CHECK(rows.column_text(2) == "sync");
    QB_CHECK(rows.column_text(3) == "legacy-b");
    QB_CHECK(rows.column_text(4) == R"({"pages":2})");
    QB_CHECK(rows.column_text(5) == "2024-02-29 01:02:04");
    QB_CHECK(!rows.step());

    const auto after_first = database_snapshot(database);
    evidence.migrated_hash = snapshot_sha256(after_first);
    qbrain::storage::apply_migrations(database);
    QB_CHECK(database_snapshot(database) == after_first);
  }

  const auto failing_path = root / "legacy-v11-fail.db";
  create_v11_ingest_fixture(failing_path);
  {
    Database database;
    database.open(qbrain::util::path_to_utf8(failing_path));
    database.exec(R"SQL(
CREATE TRIGGER reject_v12_marker BEFORE INSERT ON schema_version
WHEN NEW.version=12 BEGIN SELECT RAISE(ABORT, 'injected v12 marker failure'); END;
)SQL");
    const auto before = database_snapshot(database);
    bool failed = false;
    try {
      qbrain::storage::apply_migrations(database);
    } catch (...) {
      failed = true;
    }
    QB_CHECK(failed);
    QB_CHECK(schema_version(database) == 11);
    QB_CHECK(!has_source_column(database));
    QB_CHECK(has_index(database, "idx_ingest_log_created"));
    QB_CHECK(!has_index(database, "idx_ingest_log_source_created"));
    QB_CHECK(database_snapshot(database) == before);
    evidence.rollback_hash = snapshot_sha256(before);
  }

  qbrain::Brain cleanup("n15-migration-cleanup");
  cleanup.open_at(qbrain::util::path_to_utf8(root / "migration-cleanup.db"));
  cleanup.log_ingest("cleanup", "remove-me", "{}", 100, "remove_me");
  cleanup.log_ingest("cleanup", "survivor", "{}", 100, "survivor");
  const auto survivor_before = ingest_count(cleanup, "survivor");
  QB_CHECK(cleanup.remove_source("remove_me", true));
  QB_CHECK(ingest_count(cleanup, "remove_me") == 0);
  QB_CHECK(ingest_count(cleanup, "survivor") == survivor_before);
  QB_CHECK(scalar(cleanup, "SELECT COUNT(*) FROM pragma_foreign_key_check") == 0);
  evidence.cleanup_hash = snapshot_sha256(logical_snapshot(cleanup));
  cleanup.close();
  return evidence;
}

struct SourceRetentionEvidence {
  std::string link_hash;
  std::string retention_hash;
};

SourceRetentionEvidence test_source_and_retention_matrix(const std::filesystem::path& root) {
  SourceRetentionEvidence evidence;
  qbrain::Brain links("n15-links-primary");
  links.open_at(qbrain::util::path_to_utf8(root / "links-primary.db"));
  QB_CHECK(links.ensure_source("Team_A"));
  QB_CHECK(source_count(links, "team_a") == 1);
  QB_CHECK(links.ensure_source("TEAM_A"));
  QB_CHECK(source_count(links, "team_a") == 1);

  const auto default_page =
      qbrain::test_support::put_page(links, "default", "n15/default-links", "body");
  const auto team_page =
      qbrain::test_support::put_page(links, "team_a", "n15/team-links", "body");
  for (int index = 0; index < 3; ++index) {
    links.add_link({0, "default", default_page.slug, "default-wiki-" + std::to_string(index),
                    "wiki", "", "wiki"});
  }
  links.add_link({0, "default", default_page.slug, "default-manual", "wiki", "", "manual"});
  links.add_link(
      {0, "default", default_page.slug, "default-open", "wiki", "", "vendor/custom:v1"});
  for (int index = 0; index < 2; ++index) {
    links.add_link({0, "team_a", team_page.slug, "team-wiki-" + std::to_string(index),
                    "wiki", "", "wiki"});
  }
  links.add_link({0, "team_a", team_page.slug, "team-manual", "wiki", "", "manual"});
  links.add_link(
      {0, "team_a", team_page.slug, "team-open", "wiki", "", "vendor/custom:v1"});

  const auto default_counts =
      read_without_mutation(links, [&] { return links.list_link_sources("default"); });
  const auto team_counts =
      read_without_mutation(links, [&] { return links.list_link_sources("TEAM_A"); });
  QB_CHECK(default_counts.size() == 3);
  QB_CHECK(default_counts[0].source_id == "default" && default_counts[0].link_source == "wiki" &&
           default_counts[0].count == 3);
  QB_CHECK(default_counts[1].link_source == "manual" && default_counts[1].count == 1);
  QB_CHECK(default_counts[2].link_source == "vendor/custom:v1" &&
           default_counts[2].count == 1);
  QB_CHECK(team_counts.size() == 3);
  QB_CHECK(team_counts[0].source_id == "team_a" && team_counts[0].link_source == "wiki" &&
           team_counts[0].count == 2);
  QB_CHECK(team_counts[1].link_source == "manual" && team_counts[1].count == 1);
  QB_CHECK(team_counts[2].link_source == "vendor/custom:v1" && team_counts[2].count == 1);

  const auto op_counts = read_without_mutation(links, [&] {
    return qbrain::test_support::call_op(
        links, "list_link_sources", {{"source_id", "Team_A"}});
  });
  QB_CHECK(op_counts.ok);
  const auto op_counts_json = json::parse(op_counts.json);
  QB_CHECK(op_counts_json["source_id"] == "team_a");
  QB_CHECK(op_counts_json["link_sources"].size() == 3);

  qbrain::Brain second_links("n15-links-second");
  second_links.open_at(qbrain::util::path_to_utf8(root / "links-second.db"));
  const auto primary_before_second = logical_snapshot(links);
  const auto second_page =
      qbrain::test_support::put_page(second_links, "team_a", "n15/second-links", "body");
  second_links.add_link(
      {0, "team_a", second_page.slug, "second-only", "wiki", "", "second-only"});
  const auto second_counts = second_links.list_link_sources("team_a");
  QB_CHECK(second_counts.size() == 1 && second_counts[0].link_source == "second-only" &&
           second_counts[0].count == 1);
  QB_CHECK(logical_snapshot(links) == primary_before_second);
  QB_CHECK(links.list_link_sources("team_a").size() == 3);
  evidence.link_hash = snapshot_sha256(primary_before_second);
  second_links.close();
  links.close();

  qbrain::Brain retention("n15-retention");
  retention.open_at(qbrain::util::path_to_utf8(root / "retention.db"));
  bulk_seed_ingest(retention, "default", 105, "default-seed-");
  bulk_seed_ingest(retention, "team_a", 7, "team-seed-");
  const auto team_before_default_prune = ingest_count(retention, "team_a");
  auto default_write = qbrain::test_support::call_op(
      retention, "log_ingest", {{"path", "default-new"}});
  QB_CHECK(default_write.ok);
  QB_CHECK(json::parse(default_write.json)["source_id"] == "default");
  QB_CHECK(ingest_count(retention, "default") == 100);
  QB_CHECK(ingest_count(retention, "team_a") == team_before_default_prune);
  QB_CHECK(retention.get_ingest_log(1, "default")[0].path == "default-new");

  const auto default_before_team_prune = ingest_count(retention, "default");
  auto keep_zero = qbrain::test_support::call_op(
      retention, "log_ingest",
      {{"source_id", "Team_A"}, {"path", "team-keep-zero"}, {"keep_last", "0"}});
  QB_CHECK(keep_zero.ok);
  QB_CHECK(ingest_count(retention, "team_a") == 1);
  QB_CHECK(ingest_count(retention, "default") == default_before_team_prune);
  QB_CHECK(retention.get_ingest_log(1, "team_a")[0].path == "team-keep-zero");

  auto keep_one = qbrain::test_support::call_op(
      retention, "log_ingest",
      {{"source_id", "team_a"}, {"path", "team-keep-one"}, {"keep_last", "1"}});
  QB_CHECK(keep_one.ok && ingest_count(retention, "team_a") == 1);
  QB_CHECK(retention.get_ingest_log(1, "team_a")[0].path == "team-keep-one");

  bulk_seed_ingest(retention, "team_a", 55, "team-fifty-seed-");
  auto keep_fifty = qbrain::test_support::call_op(
      retention, "log_ingest",
      {{"source_id", "team_a"}, {"path", "team-keep-fifty"}, {"keep_last", "50"}});
  QB_CHECK(keep_fifty.ok && ingest_count(retention, "team_a") == 50);
  QB_CHECK(retention.get_ingest_log(1, "team_a")[0].path == "team-keep-fifty");

  bulk_seed_ingest(retention, "team_a", 951, "team-max-seed-");
  auto keep_max = qbrain::test_support::call_op(
      retention, "log_ingest",
      {{"source_id", "team_a"}, {"path", "team-keep-max"}, {"keep_last", "1000"}});
  QB_CHECK(keep_max.ok && ingest_count(retention, "team_a") == 1000);
  auto keep_above_max = qbrain::test_support::call_op(
      retention, "log_ingest",
      {{"source_id", "team_a"}, {"path", "team-keep-above-max"},
       {"keep_last", "1001"}});
  QB_CHECK(keep_above_max.ok && ingest_count(retention, "team_a") == 1000);
  QB_CHECK(ingest_count(retention, "default") == default_before_team_prune);

  const auto default_limit = read_without_mutation(
      retention, [&] { return retention.get_ingest_log(20, "team_a"); });
  const auto zero_limit = read_without_mutation(
      retention, [&] { return retention.get_ingest_log(0, "team_a"); });
  const auto one_limit = read_without_mutation(
      retention, [&] { return retention.get_ingest_log(1, "team_a"); });
  const auto max_limit = read_without_mutation(
      retention, [&] { return retention.get_ingest_log(50, "team_a"); });
  const auto above_limit = read_without_mutation(
      retention, [&] { return retention.get_ingest_log(51, "team_a"); });
  QB_CHECK(default_limit.size() == 20);
  QB_CHECK(zero_limit.size() == 1 && one_limit.size() == 1);
  QB_CHECK(max_limit.size() == 50 && above_limit.size() == 50);
  require_newest_order(max_limit);
  for (const auto& row : max_limit) {
    QB_CHECK(row.source_id == "team_a");
    QB_CHECK(row.path.find("default-") == std::string::npos);
  }

  for (const auto& [limit_text, expected_size] :
       std::vector<std::pair<std::string, size_t>>{{"", 20}, {"0", 1}, {"1", 1},
                                                   {"50", 50}, {"51", 50}}) {
    std::unordered_map<std::string, std::string> arguments = {{"source_id", "team_a"}};
    if (!limit_text.empty()) arguments["limit"] = limit_text;
    const auto result = read_without_mutation(retention, [&] {
      return qbrain::test_support::call_op(retention, "get_ingest_log", arguments);
    });
    QB_CHECK(result.ok);
    const auto payload = json::parse(result.json);
    QB_CHECK(payload["source_id"] == "team_a");
    QB_CHECK(payload["events"].size() == expected_size);
  }

  evidence.retention_hash = snapshot_sha256(logical_snapshot(retention));
  retention.close();
  return evidence;
}

struct PayloadEvidence {
  std::string boundary_hash;
  int rejected_payloads = 0;
  int rejected_mcp_types = 0;
  int rejected_source_reads = 0;
};

PayloadEvidence test_payload_and_mcp_validation_matrix(const std::filesystem::path& root) {
  PayloadEvidence evidence;
  qbrain::Brain brain("n15-payload");
  brain.open_at(qbrain::util::path_to_utf8(root / "payload.db"));
  QB_CHECK(brain.ensure_source("bounds"));
  QB_CHECK(brain.ensure_source("Team_A"));
  QB_CHECK(source_count(brain, "team_a") == 1);

  const std::string event_at_limit(64, 'e');
  const std::string path_at_limit(4096, 'p');
  const std::string detail_at_limit = "{\"x\":\"" + std::string(65528, 'd') + "\"}";
  QB_CHECK(detail_at_limit.size() == 65536);

  auto event_ok = qbrain::test_support::call_op(
      brain, "log_ingest",
      {{"source_id", "bounds"}, {"event_type", event_at_limit}, {"path", "event-boundary"},
       {"detail_json", "{}"}, {"keep_last", "1000"}});
  auto path_ok = qbrain::test_support::call_op(
      brain, "log_ingest",
      {{"source_id", "bounds"}, {"event_type", "path-boundary"}, {"path", path_at_limit},
       {"detail_json", "{}"}, {"keep_last", "1000"}});
  auto detail_ok = qbrain::test_support::call_op(
      brain, "log_ingest",
      {{"source_id", "bounds"}, {"event_type", "detail-boundary"},
       {"path", "detail-boundary"}, {"detail_json", detail_at_limit},
       {"keep_last", "1000"}});
  QB_CHECK(event_ok.ok && path_ok.ok && detail_ok.ok);

  auto rows = brain.get_ingest_log(50, "bounds");
  QB_CHECK(rows.size() == 3);
  QB_CHECK(std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
    return row.event_type == event_at_limit && row.path == "event-boundary";
  }));
  QB_CHECK(std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
    return row.event_type == "path-boundary" && row.path.size() == 4096;
  }));
  QB_CHECK(std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
    return row.event_type == "detail-boundary" && row.detail_json == detail_at_limit;
  }));

  const auto rejected = [&](std::unordered_map<std::string, std::string> arguments) {
    require_no_mutation(brain, [&] {
      const auto result = qbrain::test_support::call_op(brain, "log_ingest", arguments);
      QB_CHECK(!result.ok);
    });
    ++evidence.rejected_payloads;
  };
  rejected({{"source_id", "bounds"}, {"event_type", std::string(65, 'e')}, {"path", "bad"}});
  rejected({{"source_id", "bounds"}, {"event_type", "bad"}, {"path", std::string(4097, 'p')}});
  rejected({{"source_id", "bounds"},
            {"event_type", "bad"},
            {"path", "bad"},
            {"detail_json", "{\"x\":\"" + std::string(65529, 'd') + "\"}"}});
  rejected({{"source_id", "bounds"}, {"path", "bad-json"}, {"detail_json", "{"}});
  rejected({{"source_id", "bad/source"}, {"path", "bad-source"}});
  rejected({{"source_id", "CON"}, {"path", "reserved-source"}});
  rejected({{"source_id", std::string(65, 'a')}, {"path", "long-source"}});
  rejected({{"source_id", ""}, {"path", "empty-source"}});
  QB_CHECK(source_count(brain, "team_a") == 1);
  QB_CHECK(source_count(brain, "con") == 0);

  const std::vector<std::pair<std::string, std::unordered_map<std::string, std::string>>>
      invalid_source_reads = {
          {"list_link_sources", {{"source_id", "bad/source"}}},
          {"list_link_sources", {{"source_id", "CON"}}},
          {"get_ingest_log", {{"source_id", "bad/source"}}},
          {"get_ingest_log", {{"source_id", "CON"}}},
          {"chronicle_day", {{"source_id", "bad/source"}, {"day", "2024-02-29"}}},
          {"chronicle_day", {{"source_id", "CON"}, {"day", "2024-02-29"}}},
          {"chronicle_since", {{"source_id", "bad/source"}, {"since", "2024-02-29"}}},
          {"chronicle_since", {{"source_id", "CON"}, {"since", "2024-02-29"}}}};
  for (const auto& [operation, arguments] : invalid_source_reads) {
    require_no_mutation(brain, [&] {
      auto result = qbrain::test_support::call_op(brain, operation, arguments);
      QB_CHECK(!result.ok);
    });
    ++evidence.rejected_source_reads;
  }

  qbrain::mcp::ServeOptions allowed;
  allowed.allow_write = true;
  brain.save_config_value("mcp.allowed_sources", "bounds");
  const std::vector<json> malformed_keep_values = {
      "not-a-number", "18446744073709551616", -1, true, nullptr, 1.5, json::object()};
  for (const auto& value : malformed_keep_values) {
    require_no_mutation(brain, [&] {
      auto response = rpc_call(brain, allowed, "log_ingest",
                               {{"source_id", "bounds"},
                                {"path", "mcp-invalid-keep"},
                                {"keep_last", value}});
      QB_CHECK(rpc_is_error(response));
    });
    ++evidence.rejected_mcp_types;
  }

  for (const auto& arguments :
       std::vector<json>{{{"source_id", "bounds"}, {"path", nullptr}},
                         {{"source_id", "bounds"}, {"path", "bad-type"},
                          {"event_type", true}},
                         {{"source_id", "bounds"}, {"path", "bad-detail-type"},
                          {"detail_json", json::object()}}}) {
    require_no_mutation(brain, [&] {
      auto response = rpc_call(brain, allowed, "log_ingest", arguments);
      QB_CHECK(rpc_is_error(response));
    });
    ++evidence.rejected_mcp_types;
  }

  const std::vector<std::pair<std::string, json>> bad_read_limits = {
      {"get_ingest_log", {{"source_id", "bounds"}, {"limit", true}}},
      {"get_ingest_log", {{"source_id", "bounds"}, {"limit", nullptr}}},
      {"get_ingest_log", {{"source_id", "bounds"},
                           {"limit", "18446744073709551616"}}},
      {"chronicle_day", {{"source_id", "bounds"}, {"day", "2024-02-29"},
                          {"limit", true}}},
      {"chronicle_day", {{"source_id", "bounds"}, {"day", "2024-02-29"},
                          {"limit", nullptr}}},
      {"chronicle_day", {{"source_id", "bounds"}, {"day", "2024-02-29"},
                          {"limit", "18446744073709551616"}}},
      {"chronicle_since", {{"source_id", "bounds"}, {"since", "2024-02-29"},
                            {"limit", true}}},
      {"chronicle_since", {{"source_id", "bounds"}, {"since", "2024-02-29"},
                            {"limit", nullptr}}},
      {"chronicle_since", {{"source_id", "bounds"}, {"since", "2024-02-29"},
                            {"limit", "18446744073709551616"}}}};
  qbrain::mcp::ServeOptions read_options;
  for (const auto& [operation, arguments] : bad_read_limits) {
    require_no_mutation(brain, [&] {
      auto response = rpc_call(brain, read_options, operation, arguments);
      QB_CHECK(rpc_is_error(response));
    });
    ++evidence.rejected_mcp_types;
  }

  evidence.boundary_hash = snapshot_sha256(logical_snapshot(brain));
  brain.close();
  return evidence;
}

struct ImportEvidence {
  std::string primary_hash;
  std::string second_hash;
};

ImportEvidence test_import_live_sync_matrix(const std::filesystem::path& root) {
  ImportEvidence evidence;
  const auto notes = root / "notes";
  std::filesystem::create_directories(notes);
  const std::string body_marker = "N15_PRIVATE_BODY_MARKER";
  {
    std::ofstream output(notes / "team.md", std::ios::binary);
    output << "# Team\n\n" << body_marker << " [[target]]\n";
  }

  qbrain::Brain primary("n15-import-primary");
  primary.open_at(qbrain::util::path_to_utf8(root / "import-primary.db"));
  auto imported = qbrain::ingest::import_path(
      primary, qbrain::util::path_to_utf8(notes / "team.md"), "TEAM_A");
  QB_CHECK(imported.pages == 1 && imported.files == 1 && imported.errors == 0);
  QB_CHECK(primary.get_page("team", "team_a").has_value());
  QB_CHECK(!primary.get_page("team", "default").has_value());

  auto synced = qbrain::service::live_sync_once(
      primary, qbrain::util::path_to_utf8(notes), "Team_A");
  QB_CHECK(synced.scanned == 1 && synced.imported_pages == 1 && synced.errors == 0);
  const auto emitted_primary = read_without_mutation(
      primary, [&] { return primary.get_ingest_log(50, "team_a"); });
  QB_CHECK(emitted_primary.size() == 3);
  QB_CHECK(ingest_count(primary, "default") == 0);
  require_counter_only_ingest(emitted_primary, body_marker);

  require_no_mutation(primary, [&] {
    auto invalid_import = qbrain::ingest::import_path(
        primary, qbrain::util::path_to_utf8(notes), "bad/source");
    QB_CHECK(invalid_import.errors == 1 && invalid_import.pages == 0);
  });
  require_no_mutation(primary, [&] {
    auto invalid_sync = qbrain::service::live_sync_once(
        primary, qbrain::util::path_to_utf8(notes), "CON");
    QB_CHECK(invalid_sync.errors == 1 && invalid_sync.imported_pages == 0);
  });

  const auto missing_file = root / "missing.md";
  const auto team_before_missing_file = ingest_count(primary, "team_a");
  const auto default_before_missing_file = ingest_count(primary, "default");
  auto missing_import = qbrain::ingest::import_path(
      primary, qbrain::util::path_to_utf8(missing_file), "team_a");
  QB_CHECK(missing_import.errors == 1 && missing_import.pages == 0);
  QB_CHECK(ingest_count(primary, "team_a") == team_before_missing_file + 1);
  QB_CHECK(ingest_count(primary, "default") == default_before_missing_file);
  auto latest = primary.get_ingest_log(1, "team_a");
  QB_CHECK(latest.size() == 1 && latest[0].event_type == "import" &&
           latest[0].path == qbrain::util::path_to_utf8(missing_file));
  require_counter_only_ingest(latest, body_marker);

  const auto missing_directory = root / "missing-directory";
  const auto team_before_missing_sync = ingest_count(primary, "team_a");
  auto missing_sync = qbrain::service::live_sync_once(
      primary, qbrain::util::path_to_utf8(missing_directory), "team_a");
  QB_CHECK(missing_sync.errors == 1 && missing_sync.imported_pages == 0);
  QB_CHECK(ingest_count(primary, "team_a") == team_before_missing_sync + 1);
  QB_CHECK(ingest_count(primary, "default") == default_before_missing_file);
  latest = primary.get_ingest_log(1, "team_a");
  QB_CHECK(latest.size() == 1 && latest[0].event_type == "live_sync" &&
           latest[0].path == qbrain::util::path_to_utf8(missing_directory));
  require_counter_only_ingest(latest, body_marker);

  const auto primary_before_second = logical_snapshot(primary);
  qbrain::Brain second("n15-import-second");
  second.open_at(qbrain::util::path_to_utf8(root / "import-second.db"));
  auto second_import = qbrain::ingest::import_path(
      second, qbrain::util::path_to_utf8(notes / "team.md"), "team_a");
  QB_CHECK(second_import.pages == 1 && second_import.errors == 0);
  auto second_sync = qbrain::service::live_sync_once(
      second, qbrain::util::path_to_utf8(notes), "team_a");
  QB_CHECK(second_sync.scanned == 1 && second_sync.imported_pages == 1 &&
           second_sync.errors == 0);
  const auto emitted_second = second.get_ingest_log(50, "team_a");
  QB_CHECK(emitted_second.size() == 3);
  QB_CHECK(ingest_count(second, "default") == 0);
  require_counter_only_ingest(emitted_second, body_marker);
  second.log_ingest("import", "second-brain-only", "{\"pages\":0,\"files\":0,\"links\":0,\"errors\":0}",
                    100, "team_a");
  QB_CHECK(ingest_path_count(second, "second-brain-only") == 1);
  QB_CHECK(ingest_path_count(primary, "second-brain-only") == 0);
  QB_CHECK(logical_snapshot(primary) == primary_before_second);

  evidence.primary_hash = snapshot_sha256(primary_before_second);
  evidence.second_hash = snapshot_sha256(logical_snapshot(second));
  second.close();
  primary.close();
  return evidence;
}

struct ChronicleEvidence {
  std::string snapshot_hash;
  int invalid_day_cases = 0;
  int invalid_since_cases = 0;
};

ChronicleEvidence test_chronicle_matrix(const std::filesystem::path& root) {
  ChronicleEvidence evidence;
  qbrain::Brain brain("n15-chronicle");
  brain.open_at(qbrain::util::path_to_utf8(root / "chronicle.db"));
  QB_CHECK(brain.ensure_source("team_a"));

  seed_page(brain, "default", "day-before", "2024-02-28 23:59:59",
            "2024-02-28 23:59:59");
  const auto day_at = seed_page(brain, "default", "day-at", "2024-02-29 00:00:00",
                                "2024-02-29 00:00:00");
  seed_page(brain, "default", "day-end", "2024-02-29 23:59:59",
            "2024-02-29 23:59:59");
  seed_page(brain, "default", "day-after", "2024-03-01 00:00:00",
            "2024-03-01 00:00:00");
  seed_page(brain, "default", "day-created-match", "2024-02-29 12:00:00",
            "2024-03-01 00:00:01");
  seed_page(brain, "default", "day-updated-match", "2024-02-28 12:00:00",
            "2024-02-29 12:00:00");
  seed_page(brain, "default", "day-both-match", "2024-02-29 08:00:00",
            "2024-02-29 09:00:00");
  const auto tie_low = seed_page(brain, "default", "day-tie-low", "2024-02-29 10:00:00",
                                 "2024-02-29 10:00:00");
  const auto tie_high = seed_page(brain, "default", "day-tie-high", "2024-02-29 10:00:00",
                                  "2024-02-29 10:00:00");
  seed_page(brain, "team_a", "day-team", "2024-02-29 11:00:00",
            "2024-02-29 11:00:00");
  const auto deleted = seed_page(brain, "default", "day-deleted", "2024-02-29 13:00:00",
                                 "2024-02-29 13:00:00");
  QB_CHECK(brain.soft_delete(deleted.slug, "default"));

  const auto day_rows = read_without_mutation(
      brain, [&] { return brain.chronicle_day("2024-02-29", 200, "default"); });
  QB_CHECK(day_rows.size() == 7);
  require_chronicle_order(day_rows);
  const std::set<std::string> expected_day = {
      "day-at",          "day-end",       "day-created-match", "day-updated-match",
      "day-both-match",  "day-tie-low",   "day-tie-high"};
  std::set<std::string> actual_day;
  std::set<int64_t> actual_day_ids;
  for (const auto& row : day_rows) {
    QB_CHECK(row.source_id == "default");
    actual_day.insert(row.slug);
    actual_day_ids.insert(row.id);
  }
  QB_CHECK(actual_day == expected_day);
  QB_CHECK(actual_day_ids.size() == day_rows.size());
  QB_CHECK(actual_day_ids.count(day_at.id) == 1);
  QB_CHECK(!contains_slug(day_rows, "day-before"));
  QB_CHECK(!contains_slug(day_rows, "day-after"));
  QB_CHECK(!contains_slug(day_rows, "day-team"));
  QB_CHECK(!contains_slug(day_rows, "day-deleted"));
  const auto low_position = std::find_if(day_rows.begin(), day_rows.end(),
                                         [](const auto& row) { return row.slug == "day-tie-low"; });
  const auto high_position = std::find_if(day_rows.begin(), day_rows.end(),
                                          [](const auto& row) { return row.slug == "day-tie-high"; });
  QB_CHECK(low_position != day_rows.end() && high_position != day_rows.end());
  QB_CHECK(tie_high.id > tie_low.id && high_position < low_position);

  const auto leap_op = read_without_mutation(brain, [&] {
    return qbrain::test_support::call_op(
        brain, "chronicle_day", {{"source_id", "default"}, {"day", "2024-02-29"},
                                  {"limit", "200"}});
  });
  QB_CHECK(leap_op.ok);
  const auto leap_json = json::parse(leap_op.json);
  QB_CHECK(leap_json["source_id"] == "default" && leap_json["day"] == "2024-02-29");
  QB_CHECK(leap_json["pages"].size() == 7);

  seed_page(brain, "default", "since-before", "2024-12-31 23:59:59",
            "2024-12-31 23:59:59");
  seed_page(brain, "default", "since-created-at", "2025-01-01 00:00:00",
            "2024-12-31 23:59:59");
  seed_page(brain, "default", "since-updated-at", "2024-12-31 23:59:59",
            "2025-01-01 00:00:00");
  seed_page(brain, "default", "since-after", "2025-01-01 00:00:01",
            "2025-01-01 00:00:01");
  seed_page(brain, "team_a", "since-team", "2025-01-01 00:00:01",
            "2025-01-01 00:00:01");
  const auto since_deleted = seed_page(brain, "default", "since-deleted",
                                       "2025-01-01 00:00:02", "2025-01-01 00:00:02");
  QB_CHECK(brain.soft_delete(since_deleted.slug, "default"));

  const auto since_rows = read_without_mutation(brain, [&] {
    return brain.chronicle_since("2025-01-01T00:00:00Z", 200, "default");
  });
  QB_CHECK(since_rows.size() == 3);
  require_chronicle_order(since_rows);
  QB_CHECK(contains_slug(since_rows, "since-created-at"));
  QB_CHECK(contains_slug(since_rows, "since-updated-at"));
  QB_CHECK(contains_slug(since_rows, "since-after"));
  QB_CHECK(!contains_slug(since_rows, "since-before"));
  QB_CHECK(!contains_slug(since_rows, "since-team"));
  QB_CHECK(!contains_slug(since_rows, "since-deleted"));
  for (const auto& row : since_rows) QB_CHECK(row.source_id == "default");

  const auto since_date = read_without_mutation(brain, [&] {
    return qbrain::test_support::call_op(
        brain, "chronicle_since", {{"source_id", "default"}, {"since", "2025-01-01"},
                                    {"limit", "200"}});
  });
  QB_CHECK(since_date.ok);
  auto since_date_json = json::parse(since_date.json);
  QB_CHECK(since_date_json["since"] == "2025-01-01T00:00:00Z");
  QB_CHECK(since_date_json["pages"].size() == 3);
  const auto since_space = read_without_mutation(brain, [&] {
    return qbrain::test_support::call_op(
        brain, "chronicle_since",
        {{"source_id", "default"}, {"since", "2025-01-01 00:00:00Z"},
         {"limit", "200"}});
  });
  QB_CHECK(since_space.ok && json::parse(since_space.json)["since"] ==
                                  "2025-01-01T00:00:00Z");

  const std::vector<std::string> invalid_days = {
      "2025-02-29", "2024-00-01", "2024-13-01", "2024-01-00",
      "2024-04-31", "0000-01-01", "2024-2-29",  "2024-02-2",
      "2024-02-29T00:00:00Z", "2024-02-29x", "99999-01-01"};
  for (const auto& invalid : invalid_days) {
    require_no_mutation(brain, [&] {
      auto result = qbrain::test_support::call_op(
          brain, "chronicle_day", {{"source_id", "default"}, {"day", invalid}});
      QB_CHECK(!result.ok);
    });
    ++evidence.invalid_day_cases;
  }
  const std::vector<std::string> invalid_since = {
      "2025-02-29",          "2025-01",              "2025-01-01T24:00:00Z",
      "2025-01-01T00:60:00Z", "2025-01-01T00:00:60Z", "2025-01-01T00:00:00+00:00",
      "2025-01-01T00:00:00",  "2025-01-01T00:00:00z", "2025-01-01T00:00:00.0Z",
      "2025-01-01T00:00:00Zx", "99999-01-01T00:00:00Z", "",
      "2025-01-01 00:00:00",  "2025-01-01T00:00Z"};
  for (const auto& invalid : invalid_since) {
    require_no_mutation(brain, [&] {
      auto result = qbrain::test_support::call_op(
          brain, "chronicle_since", {{"source_id", "default"}, {"since", invalid}});
      QB_CHECK(!result.ok);
    });
    ++evidence.invalid_since_cases;
  }

  QB_CHECK(brain.ensure_source("day_limit"));
  for (int index = 0; index < 205; ++index) {
    seed_page(brain, "day_limit", "day-limit-" + std::to_string(index),
              "2024-06-15 12:00:00", "2024-06-15 12:00:00");
  }
  const auto day_limit_zero = read_without_mutation(
      brain, [&] { return brain.chronicle_day("2024-06-15", 0, "day_limit"); });
  const auto day_limit_one = read_without_mutation(
      brain, [&] { return brain.chronicle_day("2024-06-15", 1, "day_limit"); });
  const auto day_limit_max = read_without_mutation(
      brain, [&] { return brain.chronicle_day("2024-06-15", 200, "day_limit"); });
  const auto day_limit_above = read_without_mutation(
      brain, [&] { return brain.chronicle_day("2024-06-15", 201, "day_limit"); });
  QB_CHECK(day_limit_zero.size() == 1 && day_limit_one.size() == 1);
  QB_CHECK(day_limit_max.size() == 200 && day_limit_above.size() == 200);
  require_chronicle_order(day_limit_max);
  for (const auto& [limit_text, expected] :
       std::vector<std::pair<std::string, size_t>>{{"", 100}, {"0", 1}, {"1", 1},
                                                   {"200", 200}, {"201", 200}}) {
    std::unordered_map<std::string, std::string> arguments = {
        {"source_id", "day_limit"}, {"day", "2024-06-15"}};
    if (!limit_text.empty()) arguments["limit"] = limit_text;
    const auto result = read_without_mutation(brain, [&] {
      return qbrain::test_support::call_op(brain, "chronicle_day", arguments);
    });
    QB_CHECK(result.ok && json::parse(result.json)["pages"].size() == expected);
  }

  QB_CHECK(brain.ensure_source("since_limit"));
  for (int index = 0; index < 205; ++index) {
    seed_page(brain, "since_limit", "since-limit-" + std::to_string(index),
              "2026-01-01 00:00:00", "2026-01-01 00:00:00");
  }
  const auto since_limit_zero = read_without_mutation(
      brain, [&] { return brain.chronicle_since("2026-01-01", 0, "since_limit"); });
  const auto since_limit_max = read_without_mutation(
      brain, [&] { return brain.chronicle_since("2026-01-01", 200, "since_limit"); });
  const auto since_limit_above = read_without_mutation(
      brain, [&] { return brain.chronicle_since("2026-01-01", 201, "since_limit"); });
  QB_CHECK(since_limit_zero.size() == 1);
  QB_CHECK(since_limit_max.size() == 200 && since_limit_above.size() == 200);
  require_chronicle_order(since_limit_max);
  for (const auto& [limit_text, expected] :
       std::vector<std::pair<std::string, size_t>>{{"", 100}, {"0", 1}, {"1", 1},
                                                   {"200", 200}, {"201", 200}}) {
    std::unordered_map<std::string, std::string> arguments = {
        {"source_id", "since_limit"}, {"since", "2026-01-01"}};
    if (!limit_text.empty()) arguments["limit"] = limit_text;
    const auto result = read_without_mutation(brain, [&] {
      return qbrain::test_support::call_op(brain, "chronicle_since", arguments);
    });
    QB_CHECK(result.ok && json::parse(result.json)["pages"].size() == expected);
  }

  evidence.snapshot_hash = snapshot_sha256(logical_snapshot(brain));
  brain.close();
  return evidence;
}

struct RegistryEvidence {
  std::string metadata_hash;
  int rejected_unknown_fields = 0;
};

RegistryEvidence test_registry_matrix(qbrain::Brain& brain) {
  RegistryEvidence evidence;
  struct ExpectedOperation {
    qbrain::ops::Scope scope;
    bool local_only;
    std::string description;
    json schema;
  };
  const std::map<std::string, ExpectedOperation> expected = {
      {"list_link_sources",
       {qbrain::ops::Scope::Read, false, "Distinct link_source values with counts",
        json::parse(
            R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"}}})")}},
      {"log_ingest",
       {qbrain::ops::Scope::Write, true,
        "Append source-attributed ingest log event (keeps last N per source)",
        json::parse(
            R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"path":{"type":"string","maxLength":4096},"event_type":{"type":"string","maxLength":64,"default":"import"},"detail_json":{"type":"string","maxLength":65536,"default":"{}"},"keep_last":{"type":"integer","minimum":0,"maximum":1000,"default":100}}})")}},
      {"get_ingest_log",
       {qbrain::ops::Scope::Read, false, "Recent ingest log events",
        json::parse(
            R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"limit":{"type":"integer","minimum":0,"maximum":50,"default":20}}})")}},
      {"chronicle_day",
       {qbrain::ops::Scope::Read, false, "Pages created/updated on a UTC day",
        json::parse(
            R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"day":{"type":"string"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":100}},"required":["day"]})")}},
      {"chronicle_since",
       {qbrain::ops::Scope::Read, false, "Pages created/updated since ISO timestamp",
        json::parse(
            R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"since":{"type":"string"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":100}},"required":["since"]})")}},
      {"add_timeline_entry",
       {qbrain::ops::Scope::Write, true,
        "Create a type=timeline page (thin put_page subset)",
        json::parse(
            R"({"type":"object","additionalProperties":false,"properties":{"title":{"type":"string"},"body":{"type":"string"},"slug":{"type":"string"},"source_id":{"type":"string","default":"default"}},"anyOf":[{"required":["title"]},{"required":["body"]}]})")}}};

  std::string serialized;
  for (const auto& [name, contract] : expected) {
    const auto* operation = qbrain::ops::global_registry().find(name);
    QB_CHECK(operation != nullptr);
    QB_CHECK(operation->name == name);
    QB_CHECK(operation->scope == contract.scope);
    QB_CHECK(operation->local_only == contract.local_only);
    QB_CHECK(operation->description == contract.description);
    const auto schema = json::parse(operation->input_schema_json);
    QB_CHECK(schema == contract.schema);
    serialized += name + "|" + std::to_string(static_cast<int>(operation->scope)) + "|" +
                  (operation->local_only ? "1" : "0") + "|" + operation->description + "|" +
                  schema.dump() + "\n";
  }

  json list_request = {
      {"jsonrpc", "2.0"}, {"id", 1499}, {"method", "tools/list"}, {"params", json::object()}};
  qbrain::mcp::ServeOptions options;
  const auto before = logical_snapshot(brain);
  const auto response = json::parse(
      qbrain::mcp::handle_rpc_body(brain, options, list_request.dump()));
  QB_CHECK(logical_snapshot(brain) == before);
  QB_CHECK(response.contains("result") && response["result"]["tools"].is_array());
  for (const auto& [name, contract] : expected) {
    const auto found = std::find_if(response["result"]["tools"].begin(),
                                    response["result"]["tools"].end(),
                                    [&](const auto& tool) { return tool.value("name", "") == name; });
    QB_CHECK(found != response["result"]["tools"].end());
    QB_CHECK((*found)["description"] == contract.description);
    QB_CHECK((*found)["inputSchema"] == contract.schema);
  }

  const auto day_unknown_field = read_without_mutation(brain, [&] {
    return rpc_call(brain, options, "chronicle_day",
                    {{"date", "2024-02-29"}, {"limit", 1}});
  });
  QB_CHECK(rpc_is_error(day_unknown_field));
  ++evidence.rejected_unknown_fields;
  const auto since_unknown_field = read_without_mutation(brain, [&] {
    return rpc_call(brain, options, "chronicle_since",
                    {{"from", "2024-02-29"}, {"limit", 1}});
  });
  QB_CHECK(rpc_is_error(since_unknown_field));
  ++evidence.rejected_unknown_fields;
  evidence.metadata_hash = snapshot_sha256(serialized);
  return evidence;
}

struct TimelineEvidence {
  std::string timeline_hash;
  std::string deny_hash;
  std::string read_hash;
  std::string ambient_hash;
  int same_second_attempts = 0;
};

void require_timeline_page(qbrain::Brain& brain, const json& payload,
                           const std::string& expected_source,
                           const std::pair<std::string, std::string>& expected_provenance) {
  QB_CHECK(payload["source_id"] == expected_source);
  QB_CHECK(payload["type"] == "timeline");
  const auto page_id = payload["id"].get<int64_t>();
  const auto slug = payload["slug"].get<std::string>();
  const auto page = brain.get_page(slug, expected_source);
  QB_CHECK(page.has_value() && page->id == page_id && page->type == "timeline");
  QB_CHECK(page_provenance(brain, page_id) == expected_provenance);
  QB_CHECK(!brain.get_chunks(page_id).empty());
  QB_CHECK(embed_jobs_for_page(brain, page_id) == 1);
}

TimelineEvidence test_timeline_remote_and_ambient_matrix(const std::filesystem::path& root,
                                                         RegistryEvidence& registry_evidence) {
  TimelineEvidence evidence;
  qbrain::Brain brain("n15-timeline");
  brain.open_at(qbrain::util::path_to_utf8(root / "timeline.db"));
  brain.set_embedding_available_for_test(true);
  QB_CHECK(brain.ensure_source("team_a"));
  QB_CHECK(brain.ensure_source("other"));
  registry_evidence = test_registry_matrix(brain);

  auto local = qbrain::test_support::call_op(
      brain, "add_timeline_entry",
      {{"source_id", "Team_A"}, {"title", "Local Entry"},
       {"body", "Body [[local-target]]"}});
  QB_CHECK(local.ok);
  const auto local_payload = json::parse(local.json);
  require_timeline_page(brain, local_payload, "team_a", {"timeline", "cli"});
  const auto local_id = local_payload["id"].get<int64_t>();
  const auto local_chunks = brain.get_chunks(local_id);
  QB_CHECK(local_chunks.size() == 1);
  QB_CHECK(local_chunks[0].text == "# Local Entry\n\nBody [[local-target]]");
  QB_CHECK(brain.get_links_from(local_payload["slug"].get<std::string>(), "team_a").size() ==
           1);

  auto custom_first = qbrain::test_support::call_op(
      brain, "add_timeline_entry",
      {{"source_id", "team_a"}, {"slug", "timeline/custom"}, {"title", "Version One"},
       {"body", "first"}});
  auto custom_second = qbrain::test_support::call_op(
      brain, "add_timeline_entry",
      {{"source_id", "team_a"}, {"slug", "timeline/custom"}, {"title", "Version Two"},
       {"body", "second"}});
  QB_CHECK(custom_first.ok && custom_second.ok);
  const auto custom_one = json::parse(custom_first.json);
  const auto custom_two = json::parse(custom_second.json);
  QB_CHECK(custom_one["id"] == custom_two["id"]);
  const auto custom_id = custom_two["id"].get<int64_t>();
  const auto custom_page = brain.get_page("timeline/custom", "team_a");
  QB_CHECK(custom_page && custom_page->title == "Version Two" && custom_page->body == "second");
  auto version_count = brain.db().prepare("SELECT COUNT(*) FROM page_versions WHERE page_id=?");
  version_count.bind_int(1, custom_id);
  QB_CHECK(version_count.step() && version_count.column_int(0) == 1);
  QB_CHECK(embed_jobs_for_page(brain, custom_id) == 2);
  QB_CHECK(brain.get_chunks(custom_id).size() == 1 &&
           brain.get_chunks(custom_id)[0].text == "# Version Two\n\nsecond");

  bool found_same_second_pair = false;
  for (int attempt = 0; attempt < 5 && !found_same_second_pair; ++attempt) {
    ++evidence.same_second_attempts;
    auto first = qbrain::test_support::call_op(
        brain, "add_timeline_entry",
        {{"source_id", "team_a"}, {"title", "Same Entry"}, {"body", "same body"}});
    auto second = qbrain::test_support::call_op(
        brain, "add_timeline_entry",
        {{"source_id", "team_a"}, {"title", "Same Entry"}, {"body", "same body"}});
    QB_CHECK(first.ok && second.ok);
    const auto first_payload = json::parse(first.json);
    const auto second_payload = json::parse(second.json);
    QB_CHECK(first_payload["slug"] != second_payload["slug"]);
    require_timeline_page(brain, first_payload, "team_a", {"timeline", "cli"});
    require_timeline_page(brain, second_payload, "team_a", {"timeline", "cli"});
    const auto first_page = brain.get_page(first_payload["slug"].get<std::string>(), "team_a");
    const auto second_page = brain.get_page(second_payload["slug"].get<std::string>(), "team_a");
    QB_CHECK(first_page && second_page);
    found_same_second_pair = first_page->created_at == second_page->created_at;
  }
  QB_CHECK(found_same_second_pair);

  require_no_mutation(brain, [&] {
    auto empty = qbrain::test_support::call_op(
        brain, "add_timeline_entry", {{"source_id", "team_a"}});
    QB_CHECK(!empty.ok);
  });
  require_no_mutation(brain, [&] {
    auto invalid = qbrain::test_support::call_op(
        brain, "add_timeline_entry", {{"source_id", "bad/source"}, {"title", "bad"}});
    QB_CHECK(!invalid.ok);
  });
  require_no_mutation(brain, [&] {
    auto reserved = qbrain::test_support::call_op(
        brain, "add_timeline_entry", {{"source_id", "NUL"}, {"title", "bad"}});
    QB_CHECK(!reserved.ok);
  });

  brain.log_ingest("fixture", "default-log", "{}", 100, "default");
  brain.log_ingest("fixture", "team-log", "{}", 100, "team_a");
  brain.log_ingest("fixture", "other-log", "{}", 100, "other");
  const auto default_page =
      qbrain::test_support::put_page(brain, "default", "timeline/default-read", "body");
  const auto other_page =
      qbrain::test_support::put_page(brain, "other", "timeline/other-read", "body");
  brain.add_link({0, "default", default_page.slug, "default-target", "wiki", "", "default-only"});
  brain.add_link({0, "other", other_page.slug, "other-target", "wiki", "", "other-only"});

  qbrain::mcp::ServeOptions denied;
  const auto deny_before = logical_snapshot(brain);
  require_no_mutation(brain, [&] {
    auto response = rpc_call(brain, denied, "log_ingest",
                             {{"source_id", "team_a"}, {"path", "remote-denied"}});
    QB_CHECK(rpc_is_error(response));
  });
  require_no_mutation(brain, [&] {
    auto response = rpc_call(brain, denied, "add_timeline_entry",
                             {{"source_id", "team_a"}, {"title", "remote denied"}});
    QB_CHECK(rpc_is_error(response));
  });
  evidence.deny_hash = snapshot_sha256(deny_before);

  brain.save_config_value("mcp.allowed_sources", " Team_A ");
  qbrain::mcp::ServeOptions allowed;
  allowed.allow_write = true;
  const auto team_logs_before_allowed = ingest_count(brain, "team_a");
  auto allowed_log = rpc_call(brain, allowed, "log_ingest",
                              {{"source_id", "Team_A"}, {"event_type", "remote"},
                               {"path", "remote-allowed"}, {"detail_json", "{}"}});
  QB_CHECK(!rpc_is_error(allowed_log));
  QB_CHECK(rpc_structured_payload(allowed_log)["source_id"] == "team_a");
  QB_CHECK(ingest_count(brain, "team_a") == team_logs_before_allowed + 1);
  QB_CHECK(ingest_path_count(brain, "remote-allowed") == 1);

  auto allowed_timeline = rpc_call(
      brain, allowed, "add_timeline_entry",
      {{"source_id", "team_a"}, {"title", "Remote Entry"},
       {"body", "Remote body [[must-not-extract]]"}});
  QB_CHECK(!rpc_is_error(allowed_timeline));
  const auto remote_payload = rpc_structured_payload(allowed_timeline);
  require_timeline_page(brain, remote_payload, "team_a", {"mcp:add_timeline_entry", "mcp"});
  QB_CHECK(brain.get_links_from(remote_payload["slug"].get<std::string>(), "team_a").empty());

  for (const auto& [operation, arguments] :
       std::vector<std::pair<std::string, json>>{
           {"log_ingest", {{"source_id", "other"}, {"path", "not-allowed"}}},
           {"add_timeline_entry", {{"source_id", "other"}, {"title", "not allowed"}}}}) {
    require_no_mutation(brain, [&] {
      auto response = rpc_call(brain, allowed, operation, arguments);
      QB_CHECK(rpc_is_error(response));
    });
  }

  const auto local_page = brain.get_page(local_payload["slug"].get<std::string>(), "team_a");
  QB_CHECK(local_page && local_page->created_at.size() >= 10);
  const auto current_day = local_page->created_at.substr(0, 10);
  const std::vector<std::pair<std::string, json>> authorized_reads = {
      {"list_link_sources", {{"source_id", "Team_A"}}},
      {"get_ingest_log", {{"source_id", "team_a"}, {"limit", 50}}},
      {"chronicle_day", {{"source_id", "team_a"}, {"day", current_day}, {"limit", 200}}},
      {"chronicle_since", {{"source_id", "team_a"}, {"since", current_day},
                            {"limit", 200}}}};
  for (const auto& [operation, arguments] : authorized_reads) {
    const auto before = logical_snapshot(brain);
    auto response = rpc_call(brain, denied, operation, arguments);
    QB_CHECK(!rpc_is_error(response));
    const auto payload = rpc_structured_payload(response);
    QB_CHECK(payload["source_id"] == "team_a");
    if (operation == "list_link_sources") {
      for (const auto& row : payload["link_sources"]) QB_CHECK(row["source_id"] == "team_a");
    } else if (operation == "get_ingest_log") {
      QB_CHECK(!payload["events"].empty());
      for (const auto& row : payload["events"]) QB_CHECK(row["source_id"] == "team_a");
    } else {
      QB_CHECK(!payload["pages"].empty());
      for (const auto& row : payload["pages"]) QB_CHECK(row["source_id"] == "team_a");
    }
    QB_CHECK(logical_snapshot(brain) == before);
  }
  evidence.read_hash = snapshot_sha256(logical_snapshot(brain));

  const std::vector<std::pair<std::string, json>> unauthorized_reads = {
      {"list_link_sources", {{"source_id", "other"}}},
      {"get_ingest_log", {{"source_id", "other"}}},
      {"chronicle_day", {{"source_id", "other"}, {"day", current_day}}},
      {"chronicle_since", {{"source_id", "other"}, {"since", current_day}}}};
  for (const auto& [operation, arguments] : unauthorized_reads) {
    require_no_mutation(brain, [&] {
      auto response = rpc_call(brain, denied, operation, arguments);
      QB_CHECK(rpc_is_error(response));
    });
  }

  {
    EnvironmentGuard ambient_source("QBRAIN_SOURCE", "team_a");
    const std::vector<std::pair<std::string, json>> omitted_source_reads = {
        {"list_link_sources", json::object()},
        {"get_ingest_log", {{"limit", 50}}},
        {"chronicle_day", {{"day", current_day}, {"limit", 200}}},
        {"chronicle_since", {{"since", current_day}, {"limit", 200}}}};
    for (const auto& [operation, arguments] : omitted_source_reads) {
      const auto before = logical_snapshot(brain);
      auto response = rpc_call(brain, denied, operation, arguments);
      QB_CHECK(!rpc_is_error(response));
      const auto payload = rpc_structured_payload(response);
      QB_CHECK(payload["source_id"] == "default");
      if (operation == "list_link_sources") {
        for (const auto& row : payload["link_sources"]) QB_CHECK(row["source_id"] == "default");
      } else if (operation == "get_ingest_log") {
        for (const auto& row : payload["events"]) QB_CHECK(row["source_id"] == "default");
      } else {
        for (const auto& row : payload["pages"]) QB_CHECK(row["source_id"] == "default");
      }
      QB_CHECK(logical_snapshot(brain) == before);
    }

    auto ambient_log = rpc_call(brain, allowed, "log_ingest",
                                {{"event_type", "ambient"}, {"path", "ambient-default"}});
    QB_CHECK(!rpc_is_error(ambient_log));
    QB_CHECK(rpc_structured_payload(ambient_log)["source_id"] == "default");
    QB_CHECK(ingest_path_count(brain, "ambient-default") == 1);
    auto ambient_timeline = rpc_call(
        brain, allowed, "add_timeline_entry",
        {{"title", "Ambient Default"}, {"body", "default despite ambient source"}});
    QB_CHECK(!rpc_is_error(ambient_timeline));
    const auto ambient_payload = rpc_structured_payload(ambient_timeline);
    require_timeline_page(brain, ambient_payload, "default",
                          {"mcp:add_timeline_entry", "mcp"});
  }

  evidence.ambient_hash = snapshot_sha256(logical_snapshot(brain));
  evidence.timeline_hash = evidence.ambient_hash;
  brain.close();
  return evidence;
}

}  // namespace

void test_n15() {
  const auto root_path = std::filesystem::temp_directory_path() / "qbrain_n15_test";
  TemporaryRoot temporary_root(root_path);
  std::filesystem::create_directories(root_path / "localappdata");
  EnvironmentGuard local_app_data(
      "LOCALAPPDATA", qbrain::util::path_to_utf8(root_path / "localappdata"));

  qbrain::ops::register_builtin_ops();
  const auto migration = test_migration_matrix(root_path);
  const auto source_retention = test_source_and_retention_matrix(root_path);
  const auto payload = test_payload_and_mcp_validation_matrix(root_path);
  const auto import_sync = test_import_live_sync_matrix(root_path);
  const auto chronicle = test_chronicle_matrix(root_path);
  RegistryEvidence registry;
  const auto timeline = test_timeline_remote_and_ambient_matrix(root_path, registry);

  QB_CHECK(migration.migrated_hash.size() == 64 && migration.rollback_hash.size() == 64 &&
           migration.cleanup_hash.size() == 64);
  QB_CHECK(source_retention.link_hash.size() == 64 &&
           source_retention.retention_hash.size() == 64);
  QB_CHECK(payload.boundary_hash.size() == 64 && payload.rejected_payloads == 8 &&
           payload.rejected_mcp_types == 19 && payload.rejected_source_reads == 8);
  QB_CHECK(import_sync.primary_hash.size() == 64 && import_sync.second_hash.size() == 64);
  QB_CHECK(chronicle.snapshot_hash.size() == 64 && chronicle.invalid_day_cases == 11 &&
           chronicle.invalid_since_cases == 14);
  QB_CHECK(registry.metadata_hash.size() == 64 && registry.rejected_unknown_fields == 2 &&
           timeline.timeline_hash.size() == 64 &&
           timeline.deny_hash.size() == 64 && timeline.read_hash.size() == 64 &&
           timeline.ambient_hash.size() == 64);

  std::cout
      << "[INFO] n15 migration_matrix=pass migration_v12=pass migration_snapshot_sha256="
      << migration.migrated_hash << " migration_rollback_sha256=" << migration.rollback_hash
      << " migration_cleanup_sha256=" << migration.cleanup_hash
      << " migration_legacy_rows=2 migration_fk_cascade=pass "
      << "link_source_matrix=pass link_source_ordering=pass link_brain_isolation=pass "
         "link_snapshot_sha256="
      << source_retention.link_hash
      << " retention_matrix=pass retention_default=100 retention_team_max=1000 "
         "get_log_limit_matrix=pass retention_snapshot_sha256="
      << source_retention.retention_hash
      << " payload_boundary_matrix=pass event_boundary_bytes=64 path_boundary_bytes=4096 "
         "detail_boundary_bytes=65536 payload_rejected="
      << payload.rejected_payloads
      << " source_validation_matrix=pass source_read_rejected="
      << payload.rejected_source_reads
      << " mcp_type_rejection_matrix=pass mcp_type_rejected=" << payload.rejected_mcp_types
      << " payload_snapshot_sha256=" << payload.boundary_hash
      << " import_live_sync_matrix=pass import_counter_json=pass second_brain_isolation=pass "
         "import_primary_sha256="
      << import_sync.primary_hash << " import_second_sha256=" << import_sync.second_hash
      << " chronicle_boundary_matrix=pass chronicle_limit_matrix=pass "
         "chronicle_soft_delete=pass chronicle_tie_ordering=pass chronicle_invalid_day="
      << chronicle.invalid_day_cases << " chronicle_invalid_since="
      << chronicle.invalid_since_cases << " chronicle_snapshot_sha256="
      << chronicle.snapshot_hash
      << " registry_metadata_matrix=pass registry_operation_count=6 "
         "registry_schema=pass registry_strict_arguments=pass "
         "registry_unknown_fields_rejected="
      << registry.rejected_unknown_fields << " registry_metadata_sha256="
      << registry.metadata_hash
      << " timeline_write_matrix=pass timeline_provenance=pass timeline_chunks=pass "
         "timeline_embed_once=pass remote_no_link_extraction=pass "
         "timeline_same_second_attempts="
      << timeline.same_second_attempts << " timeline_snapshot_sha256=" << timeline.timeline_hash
      << " remote_write_matrix=pass remote_deny_snapshot_sha256=" << timeline.deny_hash
      << " remote_read_matrix=pass remote_read_count=4 remote_read_snapshot_sha256="
      << timeline.read_hash
      << " ambient_source_ignored=pass ambient_operation_count=6 ambient_snapshot_sha256="
      << timeline.ambient_hash
      << " full_logical_snapshots=pass localappdata_isolation=pass\n";
}
