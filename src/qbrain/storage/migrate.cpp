#include "qbrain/storage/database.hpp"
#include "qbrain/storage/schema_sql.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
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
    auto try_exec = [&](const char* sql) {
      try {
        db.exec(sql);
      } catch (...) {
      }
    };
    try_exec("ALTER TABLE jobs ADD COLUMN lock_token TEXT;");
    try_exec("ALTER TABLE jobs ADD COLUMN error_text TEXT;");
    try_exec("CREATE INDEX IF NOT EXISTS idx_jobs_status ON jobs(status, type);");
    db.exec("INSERT OR IGNORE INTO schema_version(version) VALUES (6);");
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
}

SchemaIntegrity check_schema_integrity(Database& db) {
  SchemaIntegrity r;
  {
    auto st = db.prepare("SELECT COALESCE(MAX(version),0) FROM schema_version");
    if (st.step()) r.schema_version = static_cast<int>(st.column_int(0));
  }

  auto has_table = [&](const char* name) {
    auto st = db.prepare(
        "SELECT 1 FROM sqlite_master WHERE type IN ('table','view') AND name=? LIMIT 1");
    st.bind_text(1, name);
    return st.step();
  };
  auto has_index = [&](const char* name) {
    auto st = db.prepare(
        "SELECT 1 FROM sqlite_master WHERE type='index' AND name=? LIMIT 1");
    st.bind_text(1, name);
    return st.step();
  };

  const char* tables[] = {"schema_version", "sources", "pages", "content_chunks",
                          "links",          "tags",    "config", "jobs", "pages_fts"};
  for (auto* t : tables) {
    if (!has_table(t)) {
      r.ok = false;
      r.missing.push_back(std::string("table:") + t);
    }
  }
  const char* indexes[] = {"idx_pages_type",  "idx_pages_updated", "idx_pages_source",
                           "idx_chunks_page", "idx_links_from",    "idx_links_to",
                           "idx_jobs_claim",  "idx_pages_source_slug"};
  for (auto* i : indexes) {
    if (!has_index(i)) {
      r.ok = false;
      r.missing.push_back(std::string("index:") + i);
    }
  }
  if (r.schema_version < 3) {
    r.ok = false;
    r.missing.push_back("schema_version<3");
  }
  return r;
}

}  // namespace qbrain::storage
