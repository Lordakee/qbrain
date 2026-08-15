#include "qbrain/core/brain.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/storage/database.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/paths.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

namespace {

using qbrain::storage::Database;

struct FixtureSnapshot {
  int64_t pages = 0;
  int64_t chunks = 0;
  int64_t links = 0;
  int64_t jobs = 0;
  std::string content_hash;
};

struct ColumnInfo {
  bool found = false;
  std::string type;
  bool not_null = false;
  bool default_is_null = false;
};

void remove_database(const std::filesystem::path& path) {
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + "-wal");
  std::filesystem::remove(path.string() + "-shm");
}

int64_t row_count(Database& db, const char* table) {
  auto st = db.prepare(std::string("SELECT COUNT(*) FROM ") + table);
  QB_CHECK(st.step());
  return st.column_int(0);
}

void append_rows(Database& db, const char* sql, int columns, std::string& out) {
  auto st = db.prepare(sql);
  while (st.step()) {
    for (int i = 0; i < columns; ++i) {
      if (st.column_is_null(i)) {
        out += "N;";
      } else {
        const auto value = st.column_text(i);
        out += std::to_string(value.size()) + ":" + value + ";";
      }
    }
    out += "\n";
  }
}

FixtureSnapshot snapshot_fixture(Database& db) {
  FixtureSnapshot snapshot;
  snapshot.pages = row_count(db, "pages");
  snapshot.chunks = row_count(db, "content_chunks");
  snapshot.links = row_count(db, "links");
  snapshot.jobs = row_count(db, "jobs");

  std::string serialized;
  append_rows(db,
              "SELECT id,source_id,slug,type,title,body,frontmatter_json,content_hash,"
              "created_at,updated_at,deleted_at,source_kind,ingested_via,ingested_at "
              "FROM pages ORDER BY id",
              14, serialized);
  append_rows(db,
              "SELECT id,page_id,chunk_index,text,hex(embedding),embedding_model,created_at "
              "FROM content_chunks ORDER BY id",
              7, serialized);
  append_rows(db,
              "SELECT id,source_id,from_slug,to_slug,kind,created_at FROM links ORDER BY id",
              6, serialized);
  append_rows(db,
              "SELECT id,queue,type,status,payload_json,result_json,priority,attempts,"
              "created_at,updated_at,lock_until FROM jobs ORDER BY id",
              11, serialized);
  snapshot.content_hash = qbrain::util::sha256_hex(serialized);
  QB_CHECK(!snapshot.content_hash.empty());
  return snapshot;
}

void check_same_fixture(const FixtureSnapshot& expected, const FixtureSnapshot& actual) {
  QB_CHECK(actual.pages == expected.pages);
  QB_CHECK(actual.chunks == expected.chunks);
  QB_CHECK(actual.links == expected.links);
  QB_CHECK(actual.jobs == expected.jobs);
  QB_CHECK(actual.content_hash == expected.content_hash);
}

int schema_version(Database& db) {
  auto st = db.prepare("SELECT COALESCE(MAX(version),0) FROM schema_version");
  QB_CHECK(st.step());
  return static_cast<int>(st.column_int(0));
}

ColumnInfo column_info(Database& db, const std::string& name) {
  ColumnInfo info;
  auto st = db.prepare("PRAGMA table_info(jobs)");
  while (st.step()) {
    if (st.column_text(1) != name) continue;
    info.found = true;
    info.type = st.column_text(2);
    info.not_null = st.column_int(3) != 0;
    info.default_is_null = st.column_is_null(4) || st.column_text(4) == "NULL";
    break;
  }
  return info;
}

bool has_index(Database& db, const std::string& name) {
  auto st = db.prepare(
      "SELECT 1 FROM sqlite_master WHERE type='index' AND name=? LIMIT 1");
  st.bind_text(1, name);
  return st.step();
}

void check_v6_shape(Database& db) {
  const auto lock_token = column_info(db, "lock_token");
  const auto error_text = column_info(db, "error_text");
  QB_CHECK(lock_token.found);
  QB_CHECK(lock_token.type == "TEXT");
  QB_CHECK(!lock_token.not_null);
  QB_CHECK(lock_token.default_is_null);
  QB_CHECK(error_text.found);
  QB_CHECK(error_text.type == "TEXT");
  QB_CHECK(!error_text.not_null);
  QB_CHECK(error_text.default_is_null);
  QB_CHECK(has_index(db, "idx_jobs_status"));
}

void create_v5_fixture(const std::filesystem::path& path) {
  remove_database(path);
  Database db;
  db.open(qbrain::util::path_to_utf8(path));
  db.exec(R"SQL(
CREATE TABLE schema_version (
  version INTEGER NOT NULL PRIMARY KEY,
  applied_at TEXT NOT NULL DEFAULT (datetime('now'))
);
INSERT INTO schema_version(version) VALUES (1),(2),(3),(4),(5);

CREATE TABLE pages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  source_id TEXT NOT NULL DEFAULT 'default',
  slug TEXT NOT NULL,
  type TEXT NOT NULL DEFAULT 'note',
  title TEXT NOT NULL DEFAULT '',
  body TEXT NOT NULL DEFAULT '',
  frontmatter_json TEXT NOT NULL DEFAULT '{}',
  content_hash TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now')),
  deleted_at TEXT,
  source_kind TEXT,
  ingested_via TEXT,
  ingested_at TEXT
);
CREATE TABLE content_chunks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  page_id INTEGER NOT NULL,
  chunk_index INTEGER NOT NULL,
  text TEXT NOT NULL,
  embedding BLOB,
  embedding_model TEXT,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  FOREIGN KEY(page_id) REFERENCES pages(id) ON DELETE CASCADE
);
CREATE TABLE links (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  source_id TEXT NOT NULL DEFAULT 'default',
  from_slug TEXT NOT NULL,
  to_slug TEXT NOT NULL,
  kind TEXT NOT NULL DEFAULT 'wiki',
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE TABLE jobs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  queue TEXT NOT NULL DEFAULT 'default',
  type TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'waiting',
  payload_json TEXT NOT NULL DEFAULT '{}',
  result_json TEXT,
  priority INTEGER NOT NULL DEFAULT 100,
  attempts INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now')),
  lock_until TEXT
);

INSERT INTO pages(id,source_id,slug,type,title,body,frontmatter_json,content_hash,
                  source_kind,ingested_via,ingested_at)
VALUES(7,'legacy','legacy-page','note','Legacy','body-v5','{"k":1}','hash-v5',
       'filesystem','import','2026-01-01T00:00:00Z');
INSERT INTO content_chunks(id,page_id,chunk_index,text,embedding,embedding_model)
VALUES(9,7,0,'legacy chunk',X'010203','legacy-embed');
INSERT INTO links(id,source_id,from_slug,to_slug,kind)
VALUES(11,'legacy','legacy-page','target','wiki');
INSERT INTO jobs(id,queue,type,status,payload_json,result_json,priority,attempts,lock_until)
VALUES(41,'migrated-complete','embed','waiting','{"page_id":7}',NULL,20,2,NULL),
      (42,'migrated-fail','extract_facts','waiting','{"slug":"legacy-page"}',NULL,30,1,NULL);
)SQL");
}

struct AlterFailureState {
  int calls = 0;
};

int fail_second_alter(void* context, int action, const char*, const char*, const char*,
                      const char*) {
  if (action != SQLITE_ALTER_TABLE) return SQLITE_OK;
  auto& state = *static_cast<AlterFailureState*>(context);
  ++state.calls;
  return state.calls == 2 ? SQLITE_DENY : SQLITE_OK;
}

void check_failed_v6_is_rolled_back(Database& db, const FixtureSnapshot& before) {
  QB_CHECK(schema_version(db) == 5);
  QB_CHECK(!column_info(db, "lock_token").found);
  QB_CHECK(!column_info(db, "error_text").found);
  QB_CHECK(!has_index(db, "idx_jobs_status"));
  QB_CHECK(sqlite3_get_autocommit(db.handle()) != 0);
  check_same_fixture(before, snapshot_fixture(db));
}

}  // namespace

void test_migration_v6() {
  namespace fs = std::filesystem;
  const auto dir = fs::temp_directory_path() / "qbrain_migration_v6_test";
  fs::create_directories(dir);

  const auto migrated_path = dir / "populated-v5.db";
  create_v5_fixture(migrated_path);
  {
    Database db;
    db.open(qbrain::util::path_to_utf8(migrated_path));
    QB_CHECK(schema_version(db) == 5);
    const auto before = snapshot_fixture(db);

    qbrain::storage::apply_migrations(db);
    QB_CHECK(schema_version(db) >= 6);
    check_v6_shape(db);
    check_same_fixture(before, snapshot_fixture(db));
    auto nulls = db.prepare(
        "SELECT lock_token IS NULL,error_text IS NULL FROM jobs ORDER BY id");
    int nullable_rows = 0;
    while (nulls.step()) {
      QB_CHECK(nulls.column_int(0) == 1);
      QB_CHECK(nulls.column_int(1) == 1);
      ++nullable_rows;
    }
    QB_CHECK(nullable_rows == 2);

    const int first_version = schema_version(db);
    qbrain::storage::apply_migrations(db);
    QB_CHECK(schema_version(db) == first_version);
    check_v6_shape(db);
    check_same_fixture(before, snapshot_fixture(db));
    std::cout << "[INFO] migration_v6_snapshot_sha256=" << before.content_hash
              << " populated_v5_before_after=identical\n"
              << std::flush;
  }

  // A job created under v5 keeps the same token fence through reclaim and completion.
  {
    qbrain::Brain brain("migration_v6_lifecycle");
    brain.open_at(qbrain::util::path_to_utf8(migrated_path));
    QB_CHECK(!qbrain::jobs::claim_job(brain, "", 30000, "migrated-complete"));
    auto migrated =
        qbrain::jobs::claim_job(brain, "v5-old-token", 30000, "migrated-complete");
    QB_CHECK(migrated.has_value());
    QB_CHECK(migrated->id == 41);
    QB_CHECK(migrated->attempts == 3);
    QB_CHECK(!qbrain::jobs::complete_job(brain, 41, "wrong-token", "{}"));
    QB_CHECK(!qbrain::jobs::fail_job(brain, 41, "wrong-token", "wrong"));
    auto expire = brain.db().prepare(
        "UPDATE jobs SET lock_until=datetime('now','-5 seconds') WHERE id=41");
    expire.step_done();
    QB_CHECK(qbrain::jobs::reclaim_stalled(brain, "migrated-complete") == 1);
    auto reclaimed = qbrain::jobs::get_job(brain, 41);
    QB_CHECK(reclaimed->status == "waiting");
    QB_CHECK(reclaimed->attempts == 4);
    QB_CHECK(reclaimed->lock_token.empty());
    QB_CHECK(!qbrain::jobs::complete_job(brain, 41, "v5-old-token", "{}"));
    QB_CHECK(!qbrain::jobs::fail_job(brain, 41, "v5-old-token", "stale"));
    auto reclaimed_claim =
        qbrain::jobs::claim_job(brain, "v5-new-token", 30000, "migrated-complete");
    QB_CHECK(reclaimed_claim.has_value());
    QB_CHECK(reclaimed_claim->attempts == 5);
    QB_CHECK(qbrain::jobs::complete_job(brain, 41, "v5-new-token", "{}"));

    auto migrated_fail =
        qbrain::jobs::claim_job(brain, "v5-fail-token", 30000, "migrated-fail");
    QB_CHECK(migrated_fail.has_value());
    QB_CHECK(migrated_fail->id == 42);
    QB_CHECK(!qbrain::jobs::fail_job(brain, 42, "stale-token", "wrong"));
    QB_CHECK(qbrain::jobs::fail_job(brain, 42, "v5-fail-token", "bounded failure"));
    auto failed = qbrain::jobs::get_job(brain, 42);
    QB_CHECK(failed->status == "failed");
    QB_CHECK(failed->error_text == "bounded failure");
  }

  // Reject the second ALTER: the first column and the version marker must roll back.
  const auto first_ddl_failure_path = dir / "fail-after-first-ddl.db";
  create_v5_fixture(first_ddl_failure_path);
  {
    Database db;
    db.open(qbrain::util::path_to_utf8(first_ddl_failure_path));
    const auto before = snapshot_fixture(db);
    AlterFailureState state;
    QB_CHECK(sqlite3_set_authorizer(db.handle(), fail_second_alter, &state) == SQLITE_OK);
    bool failed = false;
    try {
      qbrain::storage::apply_migrations(db);
    } catch (...) {
      failed = true;
    }
    QB_CHECK(sqlite3_set_authorizer(db.handle(), nullptr, nullptr) == SQLITE_OK);
    QB_CHECK(failed);
    QB_CHECK(state.calls == 2);
    check_failed_v6_is_rolled_back(db, before);
  }

  // Reject the v6 marker after all DDL to prove the new index rolls back too.
  const auto marker_failure_path = dir / "fail-at-version-marker.db";
  create_v5_fixture(marker_failure_path);
  {
    Database db;
    db.open(qbrain::util::path_to_utf8(marker_failure_path));
    db.exec(R"SQL(
CREATE TRIGGER reject_v6_marker BEFORE INSERT ON schema_version
WHEN NEW.version=6
BEGIN
  SELECT RAISE(ABORT, 'injected v6 marker failure');
END;
)SQL");
    const auto before = snapshot_fixture(db);
    bool failed = false;
    try {
      qbrain::storage::apply_migrations(db);
    } catch (...) {
      failed = true;
    }
    QB_CHECK(failed);
    check_failed_v6_is_rolled_back(db, before);
  }

  // Fresh bootstrap has the same nullable-column and index contract.
  const auto fresh_path = dir / "fresh.db";
  remove_database(fresh_path);
  qbrain::Brain fresh("migration_v6_fresh");
  fresh.open_at(qbrain::util::path_to_utf8(fresh_path));
  QB_CHECK(fresh.status_snapshot().schema_version >= 6);
  check_v6_shape(fresh.db());
  QB_CHECK(qbrain::storage::check_schema_integrity(fresh.db()).ok);
  std::cout << "[INFO] migration_v6 populated_v5=preserved idempotent=noop "
               "rollback_after_first_ddl=clean rollback_after_marker=clean "
               "migrated_job_fence=pass fresh_shape=pass\n"
            << std::flush;
}
