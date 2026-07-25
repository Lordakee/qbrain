#include "qbrain/storage/database.hpp"
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

  // v1: full bootstrap from 001_init.sql (idempotent CREATE IF NOT EXISTS)
  if (ver < 1) {
    std::string sql = read_file(schema_sql_path);
    run_in_txn(db, sql);
    // schema file already inserts version 1; ensure row exists
    db.exec("INSERT OR IGNORE INTO schema_version(version) VALUES (1);");
    ver = 1;
  }

  // v2: additive indexes for embed-missing scans + source filters (upgrade path)
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
}

}  // namespace qbrain::storage
