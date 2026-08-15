#include "qbrain/storage/database.hpp"
#include "qbrain/storage/schema_sql.hpp"
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace qbrain::storage {

static std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read schema: " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static void run_in_txn(Database& db, const std::string& sql) {
  db.exec("BEGIN;");
  try {
    db.exec(sql);
    db.exec("COMMIT;");
  } catch (...) {
    try {
      db.exec("ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

static bool jobs_has_column(Database& db, const std::string& column_name) {
  auto st = db.prepare("PRAGMA table_info(jobs)");
  while (st.step()) {
    if (st.column_text(1) == column_name) return true;
  }
  return false;
}

static bool table_has_column(Database& db, const std::string& table,
                              const std::string& column_name) {
  auto st = db.prepare("PRAGMA table_info(" + table + ")");
  while (st.step()) {
    if (st.column_text(1) == column_name) return true;
  }
  return false;
}

static std::string ascii_upper(std::string value) {
  for (char& c : value) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return value;
}

static void apply_v6_minion_migration(Database& db) {
  db.exec("BEGIN;");
  try {
    if (!jobs_has_column(db, "lock_token"))
      db.exec("ALTER TABLE jobs ADD COLUMN lock_token TEXT DEFAULT NULL;");
    if (!jobs_has_column(db, "error_text"))
      db.exec("ALTER TABLE jobs ADD COLUMN error_text TEXT DEFAULT NULL;");
    db.exec("CREATE INDEX IF NOT EXISTS idx_jobs_status ON jobs(status, type);");
    db.exec("INSERT OR IGNORE INTO schema_version(version) VALUES (6);");
    db.exec("COMMIT;");
  } catch (...) {
    try {
      db.exec("ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

void apply_migrations(Database& db, const std::string& schema_sql_path) {
  db.exec(R"SQL(
    CREATE TABLE IF NOT EXISTS schema_version (
      version INTEGER NOT NULL PRIMARY KEY,
      applied_at TEXT NOT NULL DEFAULT (datetime('now'))
    );
  )SQL");

  int ver = 0;
  {
    auto st = db.prepare("SELECT COALESCE(MAX(version),0) FROM schema_version");
    if (st.step()) ver = static_cast<int>(st.column_int(0));
  }

  // v1: always use embedded canonical schema (single source of truth).
  // Optional path override only if env/file explicitly provided and non-empty.
  if (ver < 1) {
    std::string sql;
    if (!schema_sql_path.empty()) {
      try {
        sql = read_file(schema_sql_path);
      } catch (...) {
        sql = kCanonicalSchemaSql;
      }
    } else {
      sql = kCanonicalSchemaSql;
    }
    run_in_txn(db, sql);
    db.exec("INSERT OR IGNORE INTO schema_version(version) VALUES (1);");
    ver = 1;
  }

  // v2: additive indexes
  if (ver < 2) {
    run_in_txn(db, R"SQL(
CREATE INDEX IF NOT EXISTS idx_chunks_missing_emb
  ON content_chunks(page_id) WHERE embedding IS NULL;
CREATE INDEX IF NOT EXISTS idx_pages_source_slug
  ON pages(source_id, slug);
INSERT OR IGNORE INTO schema_version(version) VALUES (2);
)SQL");
    ver = 2;
  }

  // v3: repair indexes/FKs that may be missing on legacy fallback-created DBs.
  // SQLite cannot ADD CONSTRAINT FK easily; we ensure indexes exist.
  if (ver < 3) {
    run_in_txn(db, R"SQL(
CREATE INDEX IF NOT EXISTS idx_pages_type ON pages(type);
CREATE INDEX IF NOT EXISTS idx_pages_updated ON pages(updated_at DESC);
CREATE INDEX IF NOT EXISTS idx_pages_source ON pages(source_id);
CREATE INDEX IF NOT EXISTS idx_chunks_page ON content_chunks(page_id);
CREATE INDEX IF NOT EXISTS idx_links_from ON links(source_id, from_slug);
CREATE INDEX IF NOT EXISTS idx_links_to ON links(source_id, to_slug);
CREATE INDEX IF NOT EXISTS idx_jobs_claim ON jobs(queue, status, priority, created_at);
INSERT OR IGNORE INTO schema_version(version) VALUES (3);
)SQL");
    ver = 3;
  }

  // v4: provenance columns for write path (N1)
  if (ver < 4) {
    run_in_txn(db, R"SQL(
ALTER TABLE pages ADD COLUMN source_kind TEXT;
ALTER TABLE pages ADD COLUMN ingested_via TEXT;
ALTER TABLE pages ADD COLUMN ingested_at TEXT;
INSERT OR IGNORE INTO schema_version(version) VALUES (4);
)SQL");
    ver = 4;
  }

  // v5: page versions + facts (N2/N10)
  if (ver < 5) {
    run_in_txn(db, R"SQL(
CREATE TABLE IF NOT EXISTS page_versions (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  page_id INTEGER NOT NULL,
  source_id TEXT NOT NULL DEFAULT 'default',
  slug TEXT NOT NULL,
  title TEXT NOT NULL DEFAULT '',
  body TEXT NOT NULL DEFAULT '',
  frontmatter_json TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  FOREIGN KEY(page_id) REFERENCES pages(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_page_versions_page ON page_versions(page_id);
CREATE TABLE IF NOT EXISTS facts (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  page_id INTEGER,
  entity_slug TEXT NOT NULL,
  predicate TEXT NOT NULL DEFAULT 'mentions',
  object_text TEXT NOT NULL DEFAULT '',
  active INTEGER NOT NULL DEFAULT 1,
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_facts_entity ON facts(entity_slug);
INSERT OR IGNORE INTO schema_version(version) VALUES (5);
)SQL");
    ver = 5;
  }

  // v6: minions claim fields (N12). Columns may already exist on fresh v1 schema.
  if (ver < 6) {
    apply_v6_minion_migration(db);
    ver = 6;
  }

  // v7: ingest_log for N15 chronicle/provenance (last-N events)
  if (ver < 7) {
    run_in_txn(db, R"SQL(
CREATE TABLE IF NOT EXISTS ingest_log (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  event_type TEXT NOT NULL DEFAULT 'import',
  path TEXT NOT NULL DEFAULT '',
  detail_json TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_ingest_log_created ON ingest_log(created_at DESC);
INSERT OR IGNORE INTO schema_version(version) VALUES (7);
)SQL");
    ver = 7;
  }

  // v8: job_messages for N17
  if (ver < 8) {
    run_in_txn(db, R"SQL(
CREATE TABLE IF NOT EXISTS job_messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  job_id INTEGER NOT NULL,
  sender TEXT NOT NULL DEFAULT 'system',
  payload_json TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_job_messages_job ON job_messages(job_id, id);
INSERT OR IGNORE INTO schema_version(version) VALUES (8);
)SQL");
    ver = 8;
  }

  // v9: takes (N21)
  if (ver < 9) {
    run_in_txn(db, R"SQL(
CREATE TABLE IF NOT EXISTS takes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  entity_slug TEXT NOT NULL,
  kind TEXT NOT NULL DEFAULT 'fact',
  body TEXT NOT NULL DEFAULT '',
  score REAL NOT NULL DEFAULT 0,
  active INTEGER NOT NULL DEFAULT 1,
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_takes_entity ON takes(entity_slug, active);
CREATE INDEX IF NOT EXISTS idx_takes_body ON takes(body);
INSERT OR IGNORE INTO schema_version(version) VALUES (9);
)SQL");
    ver = 9;
  }

  // v10: file_index (N24)
  if (ver < 10) {
    run_in_txn(db, R"SQL(
CREATE TABLE IF NOT EXISTS file_index (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  path TEXT NOT NULL,
  size INTEGER NOT NULL DEFAULT 0,
  mime TEXT NOT NULL DEFAULT 'application/octet-stream',
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_file_index_name ON file_index(name);
INSERT OR IGNORE INTO schema_version(version) VALUES (10);
)SQL");
    ver = 10;
  }

  // v11: raw_data (N27)
  if (ver < 11) {
    run_in_txn(db, R"SQL(
CREATE TABLE IF NOT EXISTS raw_data (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  key TEXT NOT NULL UNIQUE,
  content_text TEXT NOT NULL DEFAULT '',
  meta_json TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_raw_data_key ON raw_data(key);
INSERT OR IGNORE INTO schema_version(version) VALUES (11);
)SQL");
    ver = 11;
  }

  // v12: source-attributed ingest log. Legacy rows cannot be attributed
  // honestly, so they are preserved and assigned to canonical default.
  if (ver < 12) {
    db.exec("BEGIN IMMEDIATE;");
    try {
      db.exec(
          "CREATE TABLE IF NOT EXISTS sources ("
          "id TEXT PRIMARY KEY,"
          "name TEXT NOT NULL DEFAULT ''"
          ");"
          "INSERT OR IGNORE INTO sources(id,name) VALUES('default','default');"
          "DROP TABLE IF EXISTS ingest_log_v12;"
          "CREATE TABLE ingest_log_v12 ("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "source_id TEXT NOT NULL,"
          "event_type TEXT NOT NULL DEFAULT 'import',"
          "path TEXT NOT NULL DEFAULT '',"
          "detail_json TEXT NOT NULL DEFAULT '{}',"
          "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
          "FOREIGN KEY(source_id) REFERENCES sources(id) ON DELETE CASCADE"
          ");"
          "INSERT INTO ingest_log_v12(id,source_id,event_type,path,detail_json,created_at) "
          "SELECT id,'default',event_type,path,detail_json,created_at FROM ingest_log;"
          "DROP TABLE ingest_log;"
          "ALTER TABLE ingest_log_v12 RENAME TO ingest_log;"
          "CREATE INDEX idx_ingest_log_source_created "
          "ON ingest_log(source_id,created_at DESC,id DESC);"
          "INSERT OR IGNORE INTO schema_version(version) VALUES (12);");
      db.exec("COMMIT;");
    } catch (...) {
      try {
        db.exec("ROLLBACK;");
      } catch (...) {
      }
      throw;
    }
    ver = 12;
  }
}

SchemaIntegrity check_schema_integrity(Database& db) {
  SchemaIntegrity r;
  {
    try {
      auto st = db.prepare("SELECT COALESCE(MAX(version),0) FROM schema_version");
      if (st.step()) r.schema_version = static_cast<int>(st.column_int(0));
    } catch (const std::exception& e) {
      r.ok = false;
      r.missing.push_back(std::string("schema_version query failed: ") + e.what());
    }
  }

  auto has_table = [&](const char* name) {
    try {
      auto st = db.prepare(
          "SELECT 1 FROM sqlite_master WHERE type IN ('table','view') AND name=? LIMIT 1");
      st.bind_text(1, name);
      return st.step();
    } catch (const std::exception& e) {
      r.ok = false;
      r.missing.push_back(std::string("table check failed:") + name + ": " + e.what());
      return false;
    }
  };
  auto has_index = [&](const char* name) {
    try {
      auto st = db.prepare(
          "SELECT 1 FROM sqlite_master WHERE type='index' AND name=? LIMIT 1");
      st.bind_text(1, name);
      return st.step();
    } catch (const std::exception& e) {
      r.ok = false;
      r.missing.push_back(std::string("index check failed:") + name + ": " + e.what());
      return false;
    }
  };

  // N30 D8: complete v12 required-object set. Every table/index created by the
  // canonical v1 schema (include/qbrain/storage/schema_sql.hpp) plus every
  // additive migration v2..v12 must be present; doctor fails closed otherwise.
  const char* tables[] = {"schema_version", "sources",
                          "pages",         "content_chunks",
                          "links",         "tags",
                          "config",        "jobs",
                          "pages_fts",     "ingest_log",
                          "job_messages",  "page_versions",
                          "facts",         "takes",
                          "file_index",    "raw_data"};
  for (auto* t : tables) {
    if (!has_table(t)) {
      r.ok = false;
      r.missing.push_back(std::string("table:") + t);
    }
  }
  const char* indexes[] = {"idx_pages_type",
                           "idx_pages_updated",
                           "idx_pages_source",
                           "idx_pages_source_slug",
                           "idx_chunks_page",
                           "idx_chunks_missing_emb",
                           "idx_links_from",
                           "idx_links_to",
                           "idx_jobs_claim",
                           "idx_jobs_status",
                           "idx_ingest_log_source_created",
                           "idx_job_messages_job",
                           "idx_page_versions_page",
                           "idx_facts_entity",
                           "idx_takes_entity",
                           "idx_takes_body",
                           "idx_file_index_name",
                           "idx_raw_data_key"};
  for (auto* i : indexes) {
    if (!has_index(i)) {
      r.ok = false;
      r.missing.push_back(std::string("index:") + i);
    }
  }
  try {
    for (const char* column : {"lock_token", "error_text"}) {
      if (!jobs_has_column(db, column)) {
        r.ok = false;
        r.missing.push_back(std::string("column:jobs.") + column);
      }
    }
  } catch (const std::exception& e) {
    r.ok = false;
    r.missing.push_back(std::string("jobs column check failed: ") + e.what());
  }
  // N30 D8: required columns on v4 (pages provenance) and v5/v9/v10/v11 tables.
  const std::pair<const char*, const char*> required_columns[] = {
      {"pages", "source_kind"},   {"pages", "ingested_via"},
      {"pages", "ingested_at"},   {"ingest_log", "source_id"},
      {"page_versions", "page_id"}, {"facts", "entity_slug"},
      {"takes", "entity_slug"},   {"file_index", "path"},
      {"raw_data", "key"}};
  for (const auto& [table, column] : required_columns) {
    try {
      if (!table_has_column(db, table, column)) {
        r.ok = false;
        r.missing.push_back(std::string("column:") + table + "." + column);
      }
    } catch (const std::exception& e) {
      r.ok = false;
      r.missing.push_back(std::string("column check failed:") + table + "." + column + ": " +
                          e.what());
    }
  }
  try {
    struct ColumnShape {
      const char* name;
      const char* type;
      int not_null;
      const char* default_value;
      int primary_key;
      bool default_contains;
    };
    const ColumnShape expected[] = {
        {"id", "INTEGER", 0, "", 1, false},
        {"job_id", "INTEGER", 1, "", 0, false},
        {"sender", "TEXT", 1, "'system'", 0, false},
        {"payload_json", "TEXT", 1, "'{}'", 0, false},
        {"created_at", "TEXT", 1, "datetime('now')", 0, true},
    };
    size_t index = 0;
    auto columns = db.prepare("PRAGMA table_info(job_messages)");
    while (columns.step()) {
      if (index >= std::size(expected)) {
        r.ok = false;
        r.missing.push_back("table:job_messages unexpected column shape");
        ++index;
        continue;
      }
      const auto& want = expected[index];
      const std::string name = columns.column_text(1);
      const std::string type = ascii_upper(columns.column_text(2));
      const int not_null = static_cast<int>(columns.column_int(3));
      const std::string default_value = columns.column_text(4);
      const int primary_key = static_cast<int>(columns.column_int(5));
      const bool default_ok =
          want.default_contains
              ? default_value.find(want.default_value) != std::string::npos
              : default_value == want.default_value;
      if (name != want.name || type != want.type || not_null != want.not_null ||
          !default_ok || primary_key != want.primary_key) {
        r.ok = false;
        r.missing.push_back(std::string("column:job_messages.") + want.name + " shape");
      }
      ++index;
    }
    if (index != std::size(expected)) {
      r.ok = false;
      r.missing.push_back("table:job_messages column count/shape");
    }

    auto table_sql = db.prepare(
        "SELECT sql FROM sqlite_master WHERE type='table' AND name='job_messages'");
    if (!table_sql.step() ||
        ascii_upper(table_sql.column_text(0)).find("AUTOINCREMENT") == std::string::npos) {
      r.ok = false;
      r.missing.push_back("table:job_messages AUTOINCREMENT shape");
    }

    std::vector<std::string> index_columns;
    auto index_info = db.prepare("PRAGMA index_xinfo(idx_job_messages_job)");
    while (index_info.step()) {
      if (index_info.column_int(5) == 1 && !index_info.column_is_null(2)) {
        if (index_info.column_int(3) != 0) {
          r.ok = false;
          r.missing.push_back("index:idx_job_messages_job ordering shape");
        }
        index_columns.push_back(index_info.column_text(2));
      }
    }
    if (index_columns != std::vector<std::string>({"job_id", "id"})) {
      r.ok = false;
      r.missing.push_back("index:idx_job_messages_job column shape");
    }
  } catch (const std::exception& e) {
    r.ok = false;
    r.missing.push_back(std::string("job_messages column check failed: ") + e.what());
  }
  if (r.schema_version < 12) {
    r.ok = false;
    r.missing.push_back("schema_version<12");
  }
  return r;
}

}  // namespace qbrain::storage
