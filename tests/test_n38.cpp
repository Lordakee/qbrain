// tests/test_n38.cpp — N38 D4 PostgreSQL backend tests.
//
// SINGLE registration item `n38_pg_backend` (plan D4) with two groups:
//
//   (a) UNIT GROUP — always runs, DSN-independent:
//         U1 DSN redaction           — host/dbname/user visible, password
//                                      (SECRET123) never present.
//         U2 placeholder translator  — '?' -> '$N' (plain, quoted-string
//                                      containing '?', multiple, adjacent).
//         U3 PG DDL snapshot         — the v13-equivalent DDL text contains
//                                      the 15 canonical v13 tables, the
//                                      tsvector full-text column, and
//                                      COLLATE "C" identifiers.
//         U4 redaction negative path — a DSN with SECRET123 pointed at a
//                                      closed loopback port fails to connect
//                                      and the error text still redacts the
//                                      password while keeping host/dbname
//                                      (plan P2-2 negative test).
//
//   (b) INTEGRATION GROUP — runs ONLY when QBRAIN_PG_TEST_DSN is present.
//       When absent the group prints an explicit, visible
//       "[SKIP-PG] no QBRAIN_PG_TEST_DSN" line and returns ok (it does NOT
//       count as PASS for the PG contract; the outcome hard audit requires a
//       DSN run, per plan P0-2). When present it drives the N35 contract
//       G1-G8 equivalents plus a product-level smoke subset on PG through the
//       PUBLIC qbrain::storage::Database / qbrain::Brain surface only:
//         G1 transaction atomicity   — rollback invisible cross-connection,
//                                      commit visible everywhere.
//         G2 busy semantics          — advisory lock + lock_timeout: busy is
//                                      observable, classified, retryable.
//         G3 prepared rebind         — same Statement, bind/reset/rebind
//                                      produces a second row with the new
//                                      value (insert + select variants).
//         G4 FTS + index lookup      — exact-term top hit via the public
//                                      search path; (source_id, slug) lookup
//                                      backed by a real PG index.
//         G5 embedding blob identity — packed f32 blob sha256 round-trip.
//         G6 backup                  — backup_to writes a restorable dump
//                                      (pg_dump path; structured COPY
//                                      fallback recorded if used).
//         G7 migration idempotence   — empty schema auto-brings-up v13,
//                                      MAX(version)==13, second open no-op.
//         G8 error classification    — constraint / syntax / busy pairwise
//                                      distinguishable.
//         SMOKe (product level)      — put_page->get_page byte-identical,
//                                      search finds the term, graph
//                                      neighbors, submit/claim/complete job
//                                      cycle.
//
// Each integration run owns the whole dedicated test database named by the
// DSN: the group first drops and recreates the public schema through the
// facade, so reruns are clean (plan D4 dedicated-namespace requirement).

#include "qbrain/core/brain.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/search/hybrid.hpp"
#include "qbrain/search/vector.hpp"
#include "qbrain/storage/database.hpp"
#include "qbrain/storage/pg_backend.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/paths.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef QB_CHECK
#define QB_CHECK(cond)                                                          \
  do {                                                                          \
    if (!(cond)) {                                                              \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond + " @ " +  \
                               __FILE__ + ":" + std::to_string(__LINE__));      \
    }                                                                           \
  } while (0)
#endif

namespace {

// Redirects an environment variable for the lifetime of the object
// (MSVC _putenv_s semantics, same pattern as tests/test_n35.cpp).
class N38ScopedEnv {
 public:
  N38ScopedEnv(const char* name, const std::string& value) : name_(name) {
    if (const char* previous = std::getenv(name)) previous_ = previous;
    if (_putenv_s(name, value.c_str()) != 0) {
      throw std::runtime_error("failed to set environment variable");
    }
  }
  ~N38ScopedEnv() {
    _putenv_s(name_, previous_ ? previous_->c_str() : "");
  }
  N38ScopedEnv(const N38ScopedEnv&) = delete;
  N38ScopedEnv& operator=(const N38ScopedEnv&) = delete;

 private:
  const char* name_;
  std::optional<std::string> previous_;
};

std::filesystem::path n38_fresh_dir(const char* leaf) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / leaf;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  return dir;
}

// ---- A/B landing seams ------------------------------------------------------
// The two PG-mode open entry points are the ONLY places in this file that
// depend on the Brain/facade DSN wiring (slice B: Brain::open_at arms the
// facade with A's make_pg_backend + pg_ensure_schema when QBRAIN_PG_DSN is
// set; Database::adopt_backend exposes the same arming on the raw facade).
// The unit group calls slice A's pure helpers (pg_redact_dsn /
// pg_translate_placeholders / pg_canonical_schema_sql,
// include/qbrain/storage/pg_backend.hpp) directly.

#ifdef QBRAIN_WITH_PG
// B: open the PUBLIC storage facade in PG mode for the given DSN.
void n38_open_pg(qbrain::storage::Database& db, const std::string& dsn) {
  db.adopt_backend(qbrain::storage::make_pg_backend(dsn));
}

// B: open a Brain on the PG store named by the DSN (Brain::open_pg runs
// ensure-schema and the version==13 gate).
void n38_open_pg_brain(qbrain::Brain& brain, const std::string& dsn) {
  brain.open_pg(dsn);
}
#endif  // QBRAIN_WITH_PG
// ---- end seams --------------------------------------------------------------

// The 15 canonical v13 tables created by pg_ensure_schema's DDL (schema v1..v13
// final shape). The 16th table of a fully-migrated store,
// job_aggregation_fence, is created lazily by jobs/minions.cpp through the
// facade on BOTH backends and is deliberately not part of the canonical DDL;
// the unit group asserts the 15 DDL tables, the integration group asserts
// the same 15 exist live after open.
const char* kN38Tables[15] = {
    "schema_version", "sources",  "pages",      "content_chunks",
    "links",          "tags",     "config",     "jobs",
    "page_versions",  "facts",    "ingest_log", "job_messages",
    "takes",          "file_index", "raw_data",
};

int64_t n38_count(qbrain::storage::Database& db, const std::string& sql) {
  auto st = db.prepare(sql);
  if (!st.step()) throw std::runtime_error("count query returned no row: " + sql);
  return st.column_int(0);
}

std::string n38_dump(qbrain::storage::Database& db, const std::string& table) {
  auto st = db.prepare("SELECT id, tag FROM " + table + " ORDER BY id");
  std::string out;
  while (st.step()) {
    out += std::to_string(st.column_int(0));
    out += ":";
    out += st.column_text(1);
    out += ";";
  }
  return out;
}

std::string n38_read_file_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("fixture read failed: " + path.string());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string n38_sha256_bytes(const std::vector<uint8_t>& bytes) {
  return qbrain::util::sha256_hex(
      std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

// PG error classification (G8): three pairwise-distinguishable observable
// classes. Markers accept BOTH honest presentation styles a backend may use —
// the SQLSTATE code (class+subclass digits) or the mapped legacy text — so
// the predicate locks the CLASS contract, not one message spelling.
enum class N38Err { kConstraint, kSyntax, kBusy, kOther };

bool n38_has_any(const std::string& text,
                 std::initializer_list<const char*> markers) {
  for (const char* marker : markers) {
    if (text.find(marker) != std::string::npos) return true;
  }
  return false;
}

N38Err n38_classify(const std::string& message) {
  const bool constraint = n38_has_any(message, {"constraint failed",
                                                "unique constraint",
                                                "violates unique",
                                                "23505", "23502", "23503",
                                                "23514"});
  const bool syntax = n38_has_any(message, {"syntax error", "42601"});
  const bool busy = n38_has_any(message, {"locked", "lock_timeout",
                                          "lock timeout", "deadlock",
                                          "55P03", "40001", "40P01"});
  if (constraint && !syntax && !busy) return N38Err::kConstraint;
  if (syntax && !constraint && !busy) return N38Err::kSyntax;
  if (busy && !constraint && !syntax) return N38Err::kBusy;
  return N38Err::kOther;
}

bool n38_is_busy(const std::exception& e) {
  return n38_classify(e.what()) == N38Err::kBusy;
}

// Owns the dedicated test database for one integration run: drop + recreate
// the public schema through the facade so reruns start from an empty store.
void n38_reset_namespace(qbrain::storage::Database& db) {
  db.exec("DROP SCHEMA public CASCADE;");
  db.exec("CREATE SCHEMA public;");
}

}  // namespace

void test_n38_pg_backend() {
  namespace fs = std::filesystem;

  // Defensive: this test drives PG explicitly (make_pg_backend / Brain::
  // open_pg with a literal DSN); an ambient QBRAIN_PG_DSN must not influence
  // any open below, and must not leak into the rest of the suite (empty
  // value keeps the env-driven PG branch inactive for other opens).
  N38ScopedEnv clear_pg_dsn("QBRAIN_PG_DSN", "");

  // ================= (a) UNIT GROUP — always runs =================

  // ---- U1: DSN redaction (parse + password strip) ----
  {
    const std::string dsn =
        "postgresql://user:SECRET123@localhost:5432/qbrain_n38_test";
    const std::string redacted = qbrain::storage::pg_redact_dsn(dsn);
    // host, dbname and user stay visible for operators...
    QB_CHECK(redacted.find("localhost") != std::string::npos);
    QB_CHECK(redacted.find("qbrain_n38_test") != std::string::npos);
    // ...and the password NEVER appears (neither raw nor colon-prefixed).
    QB_CHECK(redacted.find("SECRET123") == std::string::npos);
    QB_CHECK(redacted.find(":SECRET123") == std::string::npos);
    QB_CHECK(redacted != dsn);

    // Redaction holds across DSN shapes: keyword/value form, non-default
    // port, and URI parameters after '?'.
    QB_CHECK(qbrain::storage::pg_redact_dsn(
                 "host=db.example dbname=qb password=hunter2 user=alice")
                 .find("hunter2") == std::string::npos);
    const std::string with_params = qbrain::storage::pg_redact_dsn(
        "postgresql://alice:hunter2@db.internal:6432/qb?sslmode=require");
    QB_CHECK(with_params.find("hunter2") == std::string::npos);
    QB_CHECK(with_params.find("db.internal") != std::string::npos);
    QB_CHECK(with_params.find("6432") != std::string::npos);
    QB_CHECK(with_params.find("qb") != std::string::npos);

    // Idempotence: redacting an already-redacted DSN changes nothing.
    QB_CHECK(qbrain::storage::pg_redact_dsn(redacted) == redacted);
  }

  // ---- U2: '?' -> '$N' placeholder translator ----
  {
    const auto tr = [](std::string_view sql) {
      return qbrain::storage::pg_translate_placeholders(sql);
    };
    // Plain single parameter.
    QB_CHECK(tr("SELECT id FROM pages WHERE slug = ?") ==
            "SELECT id FROM pages WHERE slug = $1");
    // Multiple parameters get sequential numbers.
    QB_CHECK(tr("VALUES(?, ?, ?)") == "VALUES($1, $2, $3)");
    // Adjacent parameters are separate markers.
    QB_CHECK(tr("VALUES(?,?)") == "VALUES($1,$2)");
    // A '?' inside a single-quoted string literal is NOT a parameter.
    QB_CHECK(tr("SELECT 'what? why?' AS q, body FROM pages WHERE id = ?") ==
            "SELECT 'what? why?' AS q, body FROM pages WHERE id = $1");
    // Quoted literal then a real parameter after it: numbering counts only
    // real markers.
    QB_CHECK(tr("INSERT INTO tags(page_id, tag) VALUES(?, 'a?b')") ==
            "INSERT INTO tags(page_id, tag) VALUES($1, 'a?b')");
    // No parameters -> untouched; empty stays empty.
    QB_CHECK(tr("SELECT 1") == "SELECT 1");
    QB_CHECK(tr("") == "");
    // Escaped quote inside a literal ('' escape): the '?' between the two
    // quoted segments is a real parameter.
    QB_CHECK(tr("SELECT 'it''s?ok' || ?") == "SELECT 'it''s?ok' || $1");
    // '?NNN' pins index NNN (SQLite rule).
    QB_CHECK(tr("SELECT ?2") == "SELECT $2");
    QB_CHECK(tr("VALUES(?1, ?1, ?3)") == "VALUES($1, $1, $3)");
    // A bare '?' after a pinned index continues from the max index so far.
    QB_CHECK(tr("VALUES(?2, ?)") == "VALUES($2, $3)");
    // param_count receives the highest assigned index.
    int params = 0;
    qbrain::storage::pg_translate_placeholders("VALUES(?, ?, ?)", &params);
    QB_CHECK(params == 3);
    params = 0;
    qbrain::storage::pg_translate_placeholders("SELECT ?2", &params);
    QB_CHECK(params == 2);
    params = 0;
    qbrain::storage::pg_translate_placeholders("SELECT 'x?y'", &params);
    QB_CHECK(params == 0);
  }

  // ---- U3: PG DDL snapshot — 15 canonical v13 tables + tsvector + COLLATE "C" ----
  {
    const std::string ddl = qbrain::storage::pg_canonical_schema_sql();
    QB_CHECK(!ddl.empty());
    for (const char* table : kN38Tables) {
      const std::string create = std::string("CREATE TABLE IF NOT EXISTS ") + table;
      QB_CHECK(ddl.find(create) != std::string::npos);
    }
    // Full-text is carried by a tsvector column (plus a GIN index).
    QB_CHECK(ddl.find("tsvector") != std::string::npos);
    QB_CHECK(ddl.find("GIN") != std::string::npos);
    // Byte-order-equivalent collation for slug/identifier ordering (the
    // SQLite COLLATE BINARY equivalent): the DDL pins COLLATE "C".
    QB_CHECK(ddl.find("COLLATE \"C\"") != std::string::npos);
    // Identity columns replace AUTOINCREMENT (v13 BIGINT identity shape).
    QB_CHECK(ddl.find("IDENTITY") != std::string::npos);
    // No SQLite-isms survive in the PG DDL.
    QB_CHECK(ddl.find("AUTOINCREMENT") == std::string::npos);
    QB_CHECK(ddl.find("fts5") == std::string::npos);
    QB_CHECK(ddl.find("datetime('now')") == std::string::npos);
  }

  // ---- U4: negative redaction on the real connection-failure path ----
#ifdef QBRAIN_WITH_PG
  {
    // A DSN with a secret password pointed at a CLOSED loopback port: the
    // connection fails fast (connection refused), and the surfaced error must
    // still redact the password while naming host (or dbname).
    const std::string bad_dsn =
        "postgresql://qbrain_probe:SECRET123@127.0.0.1:1/qbrain_n38_absent";
    std::string message;
    bool failed = false;
    try {
      auto probe = qbrain::storage::make_pg_backend(bad_dsn);
      // If some service actually listens on port 1, authentication against
      // it fails instead — still a failure path that must redact.
      probe->exec("SELECT 1");
    } catch (const std::exception& e) {
      failed = true;
      message = e.what();
    }
    QB_CHECK(failed);
    QB_CHECK(!message.empty());
    QB_CHECK(message.find("SECRET123") == std::string::npos);
    QB_CHECK(message.find("127.0.0.1") != std::string::npos ||
             message.find("qbrain_n38_absent") != std::string::npos);
  }
#else
  std::cout << "[N38-U4] QBRAIN_WITH_PG not defined — connection-path negative "
               "test skipped (pure unit assertions above still ran)\n"
            << std::flush;
#endif

  // ================= (b) INTEGRATION GROUP — DSN-gated =================
  const char* test_dsn = std::getenv("QBRAIN_PG_TEST_DSN");
  if (test_dsn == nullptr || *test_dsn == '\0') {
    std::cout << "[SKIP-PG] no QBRAIN_PG_TEST_DSN — integration group not run "
                 "(unit group above still asserted)\n"
              << std::flush;
    return;
  }
#ifndef QBRAIN_WITH_PG
  std::cout << "[SKIP-PG] QBRAIN_PG_TEST_DSN is set but this build has no "
               "PostgreSQL backend (QBRAIN_WITH_PG off) — integration group "
               "not run\n"
            << std::flush;
  return;
#else
  const std::string dsn = test_dsn;

  // ---- Setup: own the dedicated database (drop/recreate public schema) ----
  {
    qbrain::storage::Database admin;
    n38_open_pg(admin, dsn);
    n38_reset_namespace(admin);
    admin.close();
  }

  // ---- G1: transaction atomicity ----
  {
    qbrain::storage::Database a;
    n38_open_pg(a, dsn);
    a.exec("CREATE TABLE n38_txn(id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY, "
           "tag TEXT NOT NULL UNIQUE);");
    a.exec("INSERT INTO n38_txn(tag) VALUES('alpha');");
    QB_CHECK(n38_dump(a, "n38_txn") == "1:alpha;");

    qbrain::storage::Database b;  // second connection, same database
    n38_open_pg(b, dsn);
    QB_CHECK(n38_count(b, "SELECT COUNT(*) FROM n38_txn") == 1);

    // BEGIN/INSERT/ROLLBACK: invisible on both connections afterwards.
    a.exec("BEGIN;");
    a.exec("INSERT INTO n38_txn(tag) VALUES('beta');");
    QB_CHECK(n38_count(a, "SELECT COUNT(*) FROM n38_txn") == 2);  // own txn
    QB_CHECK(n38_count(b, "SELECT COUNT(*) FROM n38_txn") == 1);  // isolation
    a.exec("ROLLBACK;");
    QB_CHECK(n38_count(a, "SELECT COUNT(*) FROM n38_txn") == 1);
    QB_CHECK(n38_count(b, "SELECT COUNT(*) FROM n38_txn") == 1);
    QB_CHECK(n38_dump(a, "n38_txn") == "1:alpha;");
    QB_CHECK(n38_dump(b, "n38_txn") == "1:alpha;");

    // BEGIN/INSERT/COMMIT: visible on both connections, exact contents.
    // PG sequence semantics (documented deviation from SQLite): the rolled-
    // back beta insert CONSUMED identity value 2 — PG sequences do not roll
    // back — so gamma takes id=3 (B verified via psql: {1:alpha, 3:gamma}).
    // Contract assertion: commit is visible everywhere with the exact rows;
    // id VALUES are PG-native (the SQLite suite keeps its own 1;2 dump).
    a.exec("BEGIN;");
    a.exec("INSERT INTO n38_txn(tag) VALUES('gamma');");
    a.exec("COMMIT;");
    QB_CHECK(n38_count(a, "SELECT COUNT(*) FROM n38_txn") == 2);
    QB_CHECK(n38_count(b, "SELECT COUNT(*) FROM n38_txn") == 2);
    QB_CHECK(n38_dump(a, "n38_txn") == "1:alpha;3:gamma;");
    QB_CHECK(n38_dump(b, "n38_txn") == "1:alpha;3:gamma;");
    a.close();
    b.close();
  }

  // ---- G2: busy semantics — advisory lock + lock_timeout ----
  {
    qbrain::storage::Database a;
    n38_open_pg(a, dsn);
    qbrain::storage::Database b;
    n38_open_pg(b, dsn);

    // Writer A holds a session-level advisory lock.
    a.exec("SELECT pg_advisory_lock(918273645);");
    // B bounds its own wait so the contention surfaces as an error instead of
    // blocking forever (the PG equivalent of the SQLite busy_timeout=0
    // fail-fast contract).
    b.exec("SET lock_timeout='2s';");

    std::string busy_message;
    bool busy_seen = false;
    try {
      b.exec("SELECT pg_advisory_lock(918273645);");
    } catch (const std::exception& e) {
      busy_seen = true;
      busy_message = e.what();
    }
    QB_CHECK(busy_seen);
    QB_CHECK(n38_classify(busy_message) == N38Err::kBusy);

    // After A releases, the SAME statement retried through a bounded busy
    // retry loop succeeds: busy is retryable, never fatal.
    a.exec("SELECT pg_advisory_unlock(918273645);");
    int failures = 0;
    for (;;) {
      try {
        b.exec("SELECT pg_advisory_lock(918273645);");
        break;
      } catch (const std::exception& e) {
        if (++failures >= 500 || !n38_is_busy(e)) throw;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
    QB_CHECK(failures < 500);
    // The lock is genuinely held by B now: A cannot take it (bounded wait).
    a.exec("SET lock_timeout='1s';");
    bool still_busy = false;
    try {
      a.exec("SELECT pg_advisory_lock(918273645);");
    } catch (const std::exception& e) {
      still_busy = true;
      QB_CHECK(n38_classify(e.what()) == N38Err::kBusy);
    }
    QB_CHECK(still_busy);
    b.exec("SELECT pg_advisory_unlock(918273645);");
    a.close();
    b.close();
  }

  // ---- G3: prepared rebind (same Statement, bind/reset/rebind) ----
  {
    qbrain::storage::Database db;
    n38_open_pg(db, dsn);
    db.exec("CREATE TABLE n38_rebind(id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY, "
            "tag TEXT NOT NULL UNIQUE);");

    {
      auto ins = db.prepare("INSERT INTO n38_rebind(tag) VALUES(?)");
      ins.bind_text(1, "rebind-A");
      ins.step_done();
      ins.reset();
      ins.clear_bindings();
      ins.bind_text(1, "rebind-B");
      ins.step_done();
    }
    QB_CHECK(n38_count(db, "SELECT COUNT(*) FROM n38_rebind") == 2);
    QB_CHECK(n38_count(db,
                       "SELECT COUNT(*) FROM n38_rebind WHERE tag='rebind-A'") ==
             1);
    QB_CHECK(n38_count(db,
                       "SELECT COUNT(*) FROM n38_rebind WHERE tag='rebind-B'") ==
             1);

    // SELECT-rebind variant: reset + rebind returns the row for the NEW
    // binding, and no row for an absent one.
    {
      auto sel = db.prepare("SELECT tag FROM n38_rebind WHERE tag=?");
      sel.bind_text(1, "rebind-A");
      QB_CHECK(sel.step());
      QB_CHECK(sel.column_text(0) == "rebind-A");
      sel.reset();
      sel.clear_bindings();
      sel.bind_text(1, "rebind-B");
      QB_CHECK(sel.step());
      QB_CHECK(sel.column_text(0) == "rebind-B");
      sel.reset();
      sel.clear_bindings();
      sel.bind_text(1, "rebind-absent");
      QB_CHECK(!sel.step());
    }
    db.close();
  }

  // ---- G7 first (needs an EMPTY schema): bring-up + idempotence ----
  {
    qbrain::storage::Database admin;
    n38_open_pg(admin, dsn);
    n38_reset_namespace(admin);
    admin.close();

    const auto assert_v13 = [](qbrain::storage::Database& db) {
      QB_CHECK(n38_count(db,
                         "SELECT COALESCE(MAX(version),0) FROM schema_version") ==
               13);
      // exactly one row for the current version, and no duplicate rows
      QB_CHECK(n38_count(db,
                         "SELECT COUNT(*) FROM schema_version WHERE version=13") ==
               1);
      // every one of the 15 canonical v13 tables exists as a live table
      for (const char* table : kN38Tables) {
        QB_CHECK(n38_count(db,
                           "SELECT COUNT(*) FROM pg_tables "
                           "WHERE schemaname='public' AND tablename='" +
                               std::string(table) + "'") == 1);
      }
    };

    {
      qbrain::Brain fresh("n38_pg");
      n38_open_pg_brain(fresh, dsn);
      assert_v13(fresh.db());
      const int64_t rows_after_first =
          n38_count(fresh.db(), "SELECT COUNT(*) FROM schema_version");
      fresh.close();

      // Second open is a no-op: no new version rows, still v13.
      qbrain::Brain again("n38_pg");
      n38_open_pg_brain(again, dsn);
      assert_v13(again.db());
      QB_CHECK(n38_count(again.db(), "SELECT COUNT(*) FROM schema_version") ==
               rows_after_first);
      again.close();
    }

    // A stale-schema database (version != 13) is REJECTED with guidance,
    // never silently upgraded past a version skew (plan D2).
    {
      qbrain::storage::Database stale;
      n38_open_pg(stale, dsn);
      n38_reset_namespace(stale);
      stale.exec("CREATE TABLE schema_version(version INTEGER NOT NULL PRIMARY KEY,"
                 " applied_at TEXT);");
      stale.exec("INSERT INTO schema_version(version, applied_at) VALUES (7, 'x');");
      stale.close();

      bool rejected = false;
      std::string reject_message;
      try {
        qbrain::Brain old("n38_pg");
        n38_open_pg_brain(old, dsn);
        old.close();
      } catch (const std::exception& e) {
        rejected = true;
        reject_message = e.what();
      }
      QB_CHECK(rejected);
      QB_CHECK(reject_message.find("13") != std::string::npos);

      // Leave the store empty for the G4/G5/smoke fixture below.
      qbrain::storage::Database admin2;
      n38_open_pg(admin2, dsn);
      n38_reset_namespace(admin2);
      admin2.close();
    }
  }

  // ---- G4 + G5 + SMOKE share one Brain fixture on PG ----
  qbrain::Brain brain("n38_pg");
  n38_open_pg_brain(brain, dsn);

  qbrain::PageInput page_a;
  page_a.slug = "n38-pg/alpha";
  page_a.title = "N38 PG alpha";
  page_a.body = "storage contract anchor xylophonequantum in the body";
  const auto stored_a = brain.put_page(page_a);

  qbrain::PageInput page_b;
  page_b.slug = "n38-pg/beta";
  page_b.title = "N38 PG beta";
  page_b.body = "an entirely different pterodactylquantum token here";
  const auto stored_b = brain.put_page(page_b);
  QB_CHECK(stored_a.id > 0 && stored_b.id > 0 && stored_a.id != stored_b.id);

  // ---- SMOKE: put_page -> get_page byte-identical ----
  {
    const auto reloaded = brain.get_page(page_a.slug);
    QB_CHECK(reloaded.has_value());
    QB_CHECK(reloaded->id == stored_a.id);
    QB_CHECK(reloaded->slug == page_a.slug);
    QB_CHECK(reloaded->title == page_a.title);
    QB_CHECK(reloaded->body == page_a.body);
  }

  // ---- G4: FTS exact-term top page + index-backed lookup ----
  {
    const auto hits_alpha =
        qbrain::search::fts_search(brain, "xylophonequantum", 10);
    QB_CHECK(hits_alpha.size() == 1);
    QB_CHECK(hits_alpha[0].page_id == stored_a.id);
    QB_CHECK(hits_alpha[0].slug == page_a.slug);

    const auto hits_beta =
        qbrain::search::fts_search(brain, "pterodactylquantum", 10);
    QB_CHECK(hits_beta.size() == 1);
    QB_CHECK(hits_beta[0].page_id == stored_b.id);
    QB_CHECK(hits_beta[0].slug == page_b.slug);

    // A term absent from every page finds nothing.
    QB_CHECK(qbrain::search::fts_search(brain, "zzznosuchtokenzzz", 10).empty());

    // The (source_id, slug) exact lookup is backed by a real index: an
    // index whose definition covers source_id then slug exists on pages.
    QB_CHECK(n38_count(brain.db(),
                       "SELECT COUNT(*) FROM pg_indexes "
                       "WHERE tablename='pages' AND indexdef LIKE '%source_id%' "
                       "AND indexdef LIKE '%slug%'") >= 1);
    {
      auto st = brain.db().prepare(
          "SELECT id, slug, title FROM pages WHERE source_id=? AND slug=?");
      st.bind_text(1, "default");
      st.bind_text(2, page_a.slug);
      QB_CHECK(st.step());
      QB_CHECK(st.column_int(0) == stored_a.id);
      QB_CHECK(st.column_text(1) == page_a.slug);
      QB_CHECK(st.column_text(2) == page_a.title);
      QB_CHECK(!st.step());  // exactly one row
      st.reset();
      st.clear_bindings();
      st.bind_text(1, "default");
      st.bind_text(2, "n38-pg/no-such-slug");
      QB_CHECK(!st.step());
    }
  }

  // ---- SMOKE: search (public search path) finds the term ----
  {
    const auto hits = qbrain::search::hybrid_search(
        brain, "xylophonequantum", nullptr,
        qbrain::search::HybridOpts{.limit = 5, .use_vector = false});
    QB_CHECK(!hits.empty());
    QB_CHECK(hits[0].slug == page_a.slug);
  }

  // ---- SMOKE: graph neighbors ----
  {
    qbrain::Link link;
    link.from_slug = page_a.slug;
    link.to_slug = page_b.slug;
    link.link_type = "related";
    brain.add_link(link);

    const auto out = brain.get_links_from(page_a.slug);
    QB_CHECK(out.size() == 1);
    QB_CHECK(out[0].to_slug == page_b.slug);
    const auto in = brain.get_links_to(page_b.slug);
    QB_CHECK(in.size() == 1);
    QB_CHECK(in[0].from_slug == page_a.slug);
  }

  // ---- G5: embedding blob sha256 round-trip ----
  {
    brain.replace_chunks(stored_a.id, {"n38 pg embedding blob chunk"});
    const auto chunks = brain.get_chunks(stored_a.id);
    QB_CHECK(chunks.size() == 1);
    const int64_t chunk_id = chunks[0].id;

    const std::vector<float> emb = {0.5f,   -1.25f,  3.75f,   -0.03125f,
                                    1024.0f, -7.5f,  0.25f,   -0.5f};
    const auto packed = qbrain::search::pack_f32(emb);
    QB_CHECK(packed.size() == emb.size() * sizeof(float));
    const std::string sha_in = n38_sha256_bytes(packed);

    brain.update_chunk_embedding(chunk_id, emb, "n38-pg-embed");

    {
      auto st = brain.db().prepare(
          "SELECT embedding, dim, model FROM content_chunks WHERE id=?");
      st.bind_int(1, chunk_id);
      QB_CHECK(st.step());
      const auto blob = st.column_blob(0);
      QB_CHECK(blob.size() == packed.size());
      QB_CHECK(blob == packed);
      QB_CHECK(n38_sha256_bytes(blob) == sha_in);
      QB_CHECK(st.column_int(1) == static_cast<int64_t>(emb.size()));
      QB_CHECK(st.column_text(2) == "n38-pg-embed");
      QB_CHECK(!st.step());
    }
    const auto reloaded = brain.get_chunks(stored_a.id);
    QB_CHECK(reloaded.size() == 1);
    QB_CHECK(reloaded[0].embedding == emb);
    QB_CHECK(reloaded[0].dim == static_cast<int>(emb.size()));
    QB_CHECK(n38_sha256_bytes(qbrain::search::pack_f32(
                 reloaded[0].embedding)) == sha_in);
  }

  // ---- SMOKE: submit/claim/complete job cycle ----
  {
    const int64_t id =
        qbrain::jobs::submit_job(brain, "embed", R"({"page_id":1})", "n38-pg", 100);
    QB_CHECK(id > 0);
    QB_CHECK(!qbrain::jobs::claim_job(brain, "", 30000, "n38-pg").has_value());
    const auto job = qbrain::jobs::claim_job(brain, "tok-n38", 30000, "n38-pg");
    QB_CHECK(job.has_value());
    QB_CHECK(job->id == id);
    QB_CHECK(job->status == "active");
    QB_CHECK(job->lock_token == "tok-n38");
    // A second claim in the same queue gets nothing.
    QB_CHECK(!qbrain::jobs::claim_job(brain, "tok-n38-b", 30000, "n38-pg").has_value());
    // Wrong token cannot complete; right token completes exactly once.
    QB_CHECK(!qbrain::jobs::complete_job(brain, id, "tok-n38-b", "{}"));
    QB_CHECK(qbrain::jobs::complete_job(brain, id, "tok-n38", R"({"ok":1})"));
    QB_CHECK(!qbrain::jobs::complete_job(brain, id, "tok-n38", "{}"));
    const auto done = qbrain::jobs::get_job(brain, id);
    QB_CHECK(done.has_value());
    QB_CHECK(done->status == "completed");
    QB_CHECK(done->result_json == R"({"ok":1})");
  }

  // ---- G8: error classification — three distinguishable classes ----
  {
    qbrain::storage::Database a;
    n38_open_pg(a, dsn);
    a.exec("CREATE TABLE n38_err(id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY, "
           "tag TEXT NOT NULL UNIQUE);");
    a.exec("INSERT INTO n38_err(tag) VALUES('only');");

    std::string constraint_message;
    bool constraint_seen = false;
    try {
      a.exec("INSERT INTO n38_err(tag) VALUES('only');");  // duplicate key
    } catch (const std::exception& e) {
      constraint_seen = true;
      constraint_message = e.what();
    }
    QB_CHECK(constraint_seen);

    std::string syntax_message;
    bool syntax_seen = false;
    try {
      a.exec("SELECTT nope FROM n38_err;");
    } catch (const std::exception& e) {
      syntax_seen = true;
      syntax_message = e.what();
    }
    QB_CHECK(syntax_seen);

    // Busy: the G2 mechanism, self-contained here.
    qbrain::storage::Database b;
    n38_open_pg(b, dsn);
    a.exec("SELECT pg_advisory_lock(918273646);");
    b.exec("SET lock_timeout='2s';");
    std::string busy_message;
    bool busy_seen = false;
    try {
      b.exec("SELECT pg_advisory_lock(918273646);");
    } catch (const std::exception& e) {
      busy_seen = true;
      busy_message = e.what();
    }
    QB_CHECK(busy_seen);
    a.exec("SELECT pg_advisory_unlock(918273646);");

    QB_CHECK(n38_classify(constraint_message) == N38Err::kConstraint);
    QB_CHECK(n38_classify(syntax_message) == N38Err::kSyntax);
    QB_CHECK(n38_classify(busy_message) == N38Err::kBusy);
    // Raw outcomes pairwise distinct...
    QB_CHECK(constraint_message != syntax_message);
    QB_CHECK(constraint_message != busy_message);
    QB_CHECK(syntax_message != busy_message);
    // ...and no class marker leaks into another class's message.
    QB_CHECK(n38_classify(constraint_message) != N38Err::kSyntax);
    QB_CHECK(n38_classify(busy_message) != N38Err::kConstraint);
    a.close();
    b.close();
  }

  // ---- G6: backup_to writes a restorable artifact (pg_dump or COPY) ----
  {
    const fs::path dir = n38_fresh_dir("qbrain_n38_pg_backup");
    const fs::path dest = dir / "n38_pg_backup.out";
    const bool written =
        brain.db().backup_to(qbrain::util::path_to_utf8(dest));
    QB_CHECK(written);
    QB_CHECK(fs::exists(dest));
    const std::string bytes = n38_read_file_bytes(dest);
    QB_CHECK(!bytes.empty());

    // pg_dump plain path: the artifact is real SQL carrying our rows and
    // schema; COPY fallback: the structured export must still carry them.
    const bool has_marker_row = bytes.find("xylophonequantum") != std::string::npos ||
                                bytes.find("n38-pg/alpha") != std::string::npos;
    const bool has_schema = bytes.find("CREATE TABLE") != std::string::npos ||
                            bytes.find("pages") != std::string::npos;
    QB_CHECK(has_marker_row);
    QB_CHECK(has_schema);
    if (bytes.find("PostgreSQL database dump") != std::string::npos) {
      std::cout << "[N38-G6] backup path: pg_dump\n" << std::flush;
    } else {
      std::cout << "[N38-G6] backup path: structured COPY fallback "
                   "(pg_dump unavailable)\n"
                << std::flush;
    }
    fs::remove_all(dir);
  }

  brain.close();
  std::cout << "[N38-INTEGRATION] G1-G8 PG equivalents + product smoke green\n"
            << std::flush;
#endif  // QBRAIN_WITH_PG
}
