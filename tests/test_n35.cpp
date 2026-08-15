// tests/test_n35.cpp — N35 D2 storage contract suite.
//
// SINGLE registration item `n35_contract_suite` (N35 plan P1-2) with 8
// sub-assertion groups locking the storage contract through the PUBLIC
// surface only (qbrain::storage::Database / qbrain::Brain), so a future
// backend swapped in under the same public API must satisfy the identical
// assertions:
//   G1 transaction atomicity  — rollback provably invisible, commit visible,
//                               cross-connection isolation (AA2/P2-2 core).
//   G2 busy semantics         — two connections, exclusive writer, SQLITE_BUSY
//                               observable + distinguishable + retryable
//                               (storage-level concurrency suite, P2-2).
//   G3 prepared rebind        — same Statement, bind/reset/rebind produces a
//                               SECOND row carrying the NEW bound value
//                               (N34 rebind bug class, direct against
//                               Database; AA3 regression lock).
//   G4 FTS + index behavior   — pages_fts MATCH returns the exact expected
//                               row; idx_pages_source_slug exists and backs
//                               an exact (source_id, slug) lookup (P1-3).
//   G5 vector blob persistence— embedding blob written via the public Brain
//                               path is read back byte-identical (sha256)
//                               (P1-1; vector SEARCH contract itself is an
//                               explicit N33-domain deferral).
//   G6 backup byte-identity   — test-side SQLite online backup API (P2-3:
//                               backup is NOT an interface-mandated
//                               capability) to a temp file; sha256 of the DB
//                               payload identical; restored copy answers
//                               queries (AA4).
//   G7 migration suite        — fresh Brain DB at v13 (single v13 row);
//                               second open is a no-op (AA6); v1→v13
//                               full-apply from a canonical-v1 database (P2-1).
//   G8 error classification   — constraint violation / syntax error / busy
//                               are pairwise distinguishable observable
//                               outcomes (AA5, P2-4).
//
// All brains/databases live in temp directories with %LOCALAPPDATA%
// redirected so nothing touches %LOCALAPPDATA%\Qbrain (test_n30/test_n34
// pattern). Database::open installs WAL with busy_timeout=0, so a losing
// writer fails fast with a "database is locked" error (deterministic in a
// single process); the contract being locked is: busy is DISTINGUISHABLE and
// RETRYABLE, not that it never happens.

#include "qbrain/core/brain.hpp"
#include "qbrain/search/hybrid.hpp"
#include "qbrain/search/vector.hpp"
#include "qbrain/storage/database.hpp"
#include "qbrain/storage/schema_sql.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/paths.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sqlite3.h>

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
// (MSVC _putenv_s semantics, same pattern as tests/test_n34.cpp).
class N35ScopedEnv {
 public:
  N35ScopedEnv(const char* name, const std::string& value) : name_(name) {
    if (const char* previous = std::getenv(name)) previous_ = previous;
    if (_putenv_s(name, value.c_str()) != 0) {
      throw std::runtime_error("failed to set environment variable");
    }
  }
  ~N35ScopedEnv() {
    _putenv_s(name_, previous_ ? previous_->c_str() : "");
  }
  N35ScopedEnv(const N35ScopedEnv&) = delete;
  N35ScopedEnv& operator=(const N35ScopedEnv&) = delete;

 private:
  const char* name_;
  std::optional<std::string> previous_;
};

std::filesystem::path n35_fresh_dir(const char* leaf) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / leaf;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  return dir;
}

// Documented raw error classification (P2-4): the interface surfaces the
// SQLite result code semantics through the observable error text. Busy is
// "locked"/"busy", constraint violations carry "constraint failed", parser
// failures carry "syntax error". These markers are the contract; the three
// classes must stay pairwise distinguishable (G8 locks exactly that).
enum class N35Err { kConstraint, kSyntax, kBusy, kOther };

N35Err n35_classify(const std::string& message) {
  if (message.find("constraint failed") != std::string::npos) {
    return N35Err::kConstraint;
  }
  if (message.find("syntax error") != std::string::npos) return N35Err::kSyntax;
  if (message.find("locked") != std::string::npos ||
      message.find("busy") != std::string::npos) {
    return N35Err::kBusy;
  }
  return N35Err::kOther;
}

bool n35_is_busy(const std::exception& e) {
  return n35_classify(e.what()) == N35Err::kBusy;
}

int64_t n35_count(qbrain::storage::Database& db, const std::string& sql) {
  auto st = db.prepare(sql);
  if (!st.step()) throw std::runtime_error("count query returned no row: " + sql);
  return st.column_int(0);
}

// Byte-exact projection of a table's contents for atomicity assertions.
std::string n35_dump(qbrain::storage::Database& db, const std::string& table) {
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

std::string n35_read_file_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("fixture read failed: " + path.string());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string n35_sha256_bytes(const std::vector<uint8_t>& bytes) {
  return qbrain::util::sha256_hex(
      std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

// SQLite's online backup API writes a page-for-page CONTENT copy, but the
// destination commit deliberately rewrites three self-maintained header
// bookkeeping fields that track per-file change history (not content):
// file change counter (bytes 24..27), schema cookie (40..43) and
// version-valid-for number (92..95). "DB payload" for the byte-identity
// contract therefore means: every byte of the file with these 12 volatile
// header bytes masked out.
bool n35_in_volatile_header(size_t offset) {
  return (offset >= 24 && offset < 28) || (offset >= 40 && offset < 44) ||
         (offset >= 92 && offset < 96);
}

std::string n35_payload_sha256(const std::string& file_bytes) {
  std::string masked = file_bytes;
  for (size_t i = 0; i < masked.size(); ++i) {
    if (n35_in_volatile_header(i)) masked[i] = '\0';
  }
  return qbrain::util::sha256_hex(masked);
}

}  // namespace

void test_n35_contract_suite() {
  namespace fs = std::filesystem;
  const fs::path root = n35_fresh_dir("qbrain_n35_contract");
  N35ScopedEnv local_app_data("LOCALAPPDATA", qbrain::util::path_to_utf8(root));

  // ---- G1: transaction atomicity (rollback invisible, commit visible) ----
  {
    const fs::path dir = root / "g1_txn";
    fs::create_directories(dir);
    const std::string path = qbrain::util::path_to_utf8(dir / "txn.db");

    qbrain::storage::Database a;
    a.open(path);
    a.exec("CREATE TABLE n35_txn("
           "id INTEGER PRIMARY KEY AUTOINCREMENT, tag TEXT NOT NULL UNIQUE);");
    a.exec("INSERT INTO n35_txn(tag) VALUES('alpha');");
    const std::string baseline = n35_dump(a, "n35_txn");
    QB_CHECK(baseline == "1:alpha;");

    qbrain::storage::Database b;  // second connection, same file
    b.open(path);
    QB_CHECK(n35_count(b, "SELECT COUNT(*) FROM n35_txn") == 1);

    // BEGIN/INSERT/ROLLBACK: nothing observable changes, on either
    // connection, including a byte-exact dump compare.
    a.exec("BEGIN;");
    a.exec("INSERT INTO n35_txn(tag) VALUES('beta');");
    QB_CHECK(n35_count(a, "SELECT COUNT(*) FROM n35_txn") == 2);  // own txn
    QB_CHECK(n35_count(b, "SELECT COUNT(*) FROM n35_txn") == 1);  // isolation
    a.exec("ROLLBACK;");
    QB_CHECK(n35_count(a, "SELECT COUNT(*) FROM n35_txn") == 1);
    QB_CHECK(n35_count(b, "SELECT COUNT(*) FROM n35_txn") == 1);
    QB_CHECK(n35_dump(a, "n35_txn") == baseline);
    QB_CHECK(n35_dump(b, "n35_txn") == baseline);

    // BEGIN/INSERT/COMMIT: visible on both connections, exact contents.
    a.exec("BEGIN;");
    a.exec("INSERT INTO n35_txn(tag) VALUES('gamma');");
    a.exec("COMMIT;");
    QB_CHECK(n35_count(a, "SELECT COUNT(*) FROM n35_txn") == 2);
    QB_CHECK(n35_count(b, "SELECT COUNT(*) FROM n35_txn") == 2);
    QB_CHECK(n35_dump(a, "n35_txn") == "1:alpha;2:gamma;");
    QB_CHECK(n35_dump(b, "n35_txn") == "1:alpha;2:gamma;");
    a.close();
    b.close();
  }

  // ---- G2: busy semantics — distinguishable + retryable (P2-2) ----
  {
    const fs::path dir = root / "g2_busy";
    fs::create_directories(dir);
    const std::string path = qbrain::util::path_to_utf8(dir / "busy.db");

    qbrain::storage::Database w1;
    w1.open(path);
    w1.exec("CREATE TABLE n35_busy("
            "id INTEGER PRIMARY KEY, tag TEXT NOT NULL UNIQUE);");
    w1.exec("INSERT INTO n35_busy(tag) VALUES('w1-baseline');");

    qbrain::storage::Database w2;  // second writer connection, same file
    w2.open(path);

    // Writer 1 holds an exclusive write transaction.
    w1.exec("BEGIN IMMEDIATE;");
    w1.exec("INSERT INTO n35_busy(tag) VALUES('w1-held');");

    // WAL readers are never blocked: w2 still reads the committed snapshot
    // (and must NOT observe the uncommitted row).
    QB_CHECK(n35_count(w2, "SELECT COUNT(*) FROM n35_busy") == 1);

    // w2's write hits SQLITE_BUSY (Database::open sets busy_timeout=0, so
    // this is deterministic) and the error is classifiable as busy.
    std::string busy_message;
    bool busy_seen = false;
    try {
      w2.exec("INSERT INTO n35_busy(tag) VALUES('w2-write');");
    } catch (const std::exception& e) {
      busy_seen = true;
      busy_message = e.what();
    }
    QB_CHECK(busy_seen);
    QB_CHECK(n35_classify(busy_message) == N35Err::kBusy);

    // After the writer releases, the SAME statement retried through a
    // bounded busy-retry loop commits: busy is retryable, never fatal.
    w1.exec("COMMIT;");
    int failures = 0;
    for (;;) {
      try {
        w2.exec("INSERT INTO n35_busy(tag) VALUES('w2-write');");
        break;
      } catch (const std::exception& e) {
        if (++failures >= 500 || !n35_is_busy(e)) throw;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }
    QB_CHECK(failures < 500);
    QB_CHECK(n35_count(w2, "SELECT COUNT(*) FROM n35_busy") == 3);
    QB_CHECK(n35_dump(w2, "n35_busy") == "1:w1-baseline;2:w1-held;3:w2-write;");
    w1.close();
    w2.close();
  }

  // ---- G3: prepared rebind regression lock (N34 bug class, AA3) ----
  {
    const fs::path dir = root / "g3_rebind";
    fs::create_directories(dir);
    qbrain::storage::Database db;
    db.open(qbrain::util::path_to_utf8(dir / "rebind.db"));
    db.exec("CREATE TABLE n35_rebind("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, tag TEXT NOT NULL UNIQUE);");

    // Same Statement: bind A -> step_done -> reset -> bind B -> step_done
    // must produce TWO rows carrying the two DISTINCT values.
    int64_t id_a = 0;
    int64_t id_b = 0;
    {
      auto ins = db.prepare("INSERT INTO n35_rebind(tag) VALUES(?)");
      ins.bind_text(1, "rebind-A");
      ins.step_done();
      id_a = db.last_insert_rowid();
      ins.reset();
      ins.clear_bindings();
      ins.bind_text(1, "rebind-B");
      ins.step_done();
      id_b = db.last_insert_rowid();
    }  // finalize before close: close() with live statements leaks the handle
    QB_CHECK(id_a > 0);
    QB_CHECK(id_b == id_a + 1);
    QB_CHECK(db.changes() == 1);  // exactly one row changed by the rebind run
    QB_CHECK(n35_count(db, "SELECT COUNT(*) FROM n35_rebind") == 2);
    QB_CHECK(n35_count(db,
                       "SELECT COUNT(*) FROM n35_rebind WHERE tag='rebind-A'") ==
             1);
    QB_CHECK(n35_count(db,
                       "SELECT COUNT(*) FROM n35_rebind WHERE tag='rebind-B'") ==
             1);

    // The same rebind contract on a SELECT: reset + rebind must return the
    // row for the NEW binding (and no row for an absent one).
    {
      auto sel = db.prepare("SELECT id FROM n35_rebind WHERE tag=?");
      sel.bind_text(1, "rebind-A");
      QB_CHECK(sel.step());
      QB_CHECK(sel.column_int(0) == id_a);
      sel.reset();
      sel.clear_bindings();
      sel.bind_text(1, "rebind-B");
      QB_CHECK(sel.step());
      QB_CHECK(sel.column_int(0) == id_b);
      sel.reset();
      sel.clear_bindings();
      sel.bind_text(1, "rebind-absent");
      QB_CHECK(!sel.step());
    }
    db.close();
  }

  // ---- G4 (setup) + G5 + G6 share one Brain fixture ----
  const fs::path brain_dir = root / "g456_brain";
  fs::create_directories(brain_dir);
  const std::string brain_path =
      qbrain::util::path_to_utf8(brain_dir / "brain.db");

  qbrain::Brain brain("n35_contract");
  brain.open_at(brain_path);

  qbrain::PageInput page_a;
  page_a.slug = "n35-contract/fts-alpha";
  page_a.title = "N35 contract alpha";
  page_a.body = "storage contract anchor xylophonequantum in the body";
  const auto stored_a = brain.put_page(page_a);

  qbrain::PageInput page_b;
  page_b.slug = "n35-contract/fts-beta";
  page_b.title = "N35 contract beta";
  page_b.body = "an entirely different pterodactylquantum token here";
  const auto stored_b = brain.put_page(page_b);
  QB_CHECK(stored_a.id > 0 && stored_b.id > 0 && stored_a.id != stored_b.id);

  // ---- G4: FTS availability + idx_pages_source_slug lookup (P1-3) ----
  {
    // FTS through the public search path: exact expected row per query.
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

    // FTS is also reachable through the Database public surface directly.
    {
      auto st = brain.db().prepare(
          "SELECT p.slug FROM pages_fts JOIN pages p ON p.id = pages_fts.rowid "
          "WHERE pages_fts MATCH ? AND p.deleted_at IS NULL");
      st.bind_text(1, "\"xylophonequantum\"");
      QB_CHECK(st.step());
      QB_CHECK(st.column_text(0) == page_a.slug);
      st.reset();
      st.clear_bindings();
      st.bind_text(1, "\"pterodactylquantum\"");
      QB_CHECK(st.step());
      QB_CHECK(st.column_text(0) == page_b.slug);
      st.reset();
      st.clear_bindings();
      st.bind_text(1, "\"zzz-no-such-token-zzz\"");
      QB_CHECK(!st.step());
    }

    // idx_pages_source_slug exists (v2) and backs an exact (source_id, slug)
    // lookup: the exact row, and only that row.
    QB_CHECK(n35_count(brain.db(),
                       "SELECT COUNT(*) FROM sqlite_master WHERE type='index' "
                       "AND name='idx_pages_source_slug'") == 1);
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
      st.bind_text(2, "n35-contract/no-such-slug");
      QB_CHECK(!st.step());
    }
  }

  // ---- G5: embedding blob byte-identity through public paths (P1-1) ----
  {
    brain.replace_chunks(stored_a.id, {"n35 embedding blob chunk"});
    const auto chunks = brain.get_chunks(stored_a.id);
    QB_CHECK(chunks.size() == 1);
    const int64_t chunk_id = chunks[0].id;

    // Exactly-representable float values: any bit difference is a real bug.
    const std::vector<float> emb = {0.5f,   -1.25f,  3.75f,   -0.03125f,
                                    1024.0f, -7.5f,  0.25f,   -0.5f};
    const auto packed = qbrain::search::pack_f32(emb);
    QB_CHECK(packed.size() == emb.size() * sizeof(float));
    const std::string sha_in = n35_sha256_bytes(packed);

    // Public Brain write path (prepare/bind_blob under the hood).
    brain.update_chunk_embedding(chunk_id, emb, "n35-contract-embed");

    // Raw storage read through the Database public surface: byte-identical.
    {
      auto st = brain.db().prepare(
          "SELECT embedding, dim, model FROM content_chunks WHERE id=?");
      st.bind_int(1, chunk_id);
      QB_CHECK(st.step());
      const auto blob = st.column_blob(0);
      QB_CHECK(blob.size() == packed.size());
      QB_CHECK(blob == packed);
      QB_CHECK(n35_sha256_bytes(blob) == sha_in);
      QB_CHECK(st.column_int(1) == static_cast<int64_t>(emb.size()));
      QB_CHECK(st.column_text(2) == "n35-contract-embed");
      QB_CHECK(!st.step());
    }

    // Public Brain read path round-trips the same bytes bit-for-bit.
    const auto reloaded = brain.get_chunks(stored_a.id);
    QB_CHECK(reloaded.size() == 1);
    QB_CHECK(reloaded[0].embedding == emb);
    QB_CHECK(reloaded[0].dim == static_cast<int>(emb.size()));
    QB_CHECK(reloaded[0].model == "n35-contract-embed");
    QB_CHECK(n35_sha256_bytes(qbrain::search::pack_f32(
                 reloaded[0].embedding)) == sha_in);
  }

  // ---- G6: backup byte-identity via the test-side SQLite backup API ----
  {
    // Fold the WAL into the main file so the on-disk .db payload is the
    // complete committed snapshot (backup API reads the logical snapshot).
    brain.db().exec("PRAGMA wal_checkpoint(TRUNCATE);");
    const fs::path source_db = brain_dir / "brain.db";
    const fs::path backup_db = brain_dir / "brain.backup.db";

    // P2-3: the backup runs through the SQLite online backup API directly,
    // test-side — backup is NOT an interface-mandated capability.
    sqlite3* dest = nullptr;
    QB_CHECK(sqlite3_open(qbrain::util::path_to_utf8(backup_db).c_str(), &dest) ==
             SQLITE_OK);
    sqlite3_backup* bak =
        sqlite3_backup_init(dest, "main", brain.db().handle(), "main");
    QB_CHECK(bak != nullptr);
    const int step_rc = sqlite3_backup_step(bak, -1);
    sqlite3_backup_finish(bak);
    QB_CHECK(step_rc == SQLITE_DONE);
    QB_CHECK(sqlite3_errcode(dest) == SQLITE_OK);
    QB_CHECK(sqlite3_close(dest) == SQLITE_OK);

    // Byte-identity of the DB payload: file sizes equal, every differing
    // byte falls inside the documented volatile header fields, and the
    // masked-payload sha256 is identical.
    const std::string source_bytes = n35_read_file_bytes(source_db);
    const std::string backup_bytes = n35_read_file_bytes(backup_db);
    QB_CHECK(source_bytes.size() == backup_bytes.size());
    for (size_t i = 0; i < source_bytes.size(); ++i) {
      if (source_bytes[i] != backup_bytes[i]) {
        QB_CHECK(n35_in_volatile_header(i));
      }
    }
    const std::string sha_source = n35_payload_sha256(source_bytes);
    const std::string sha_backup = n35_payload_sha256(backup_bytes);
    QB_CHECK(sha_source == sha_backup);
    QB_CHECK(!sha_source.empty());

    // Restore-copy: the backup opens as a live database and answers queries
    // (row-exact + FTS still functional on the restored copy).
    qbrain::Brain restored("n35_contract");
    restored.open_at(qbrain::util::path_to_utf8(backup_db));
    const auto page = restored.get_page(page_a.slug);
    QB_CHECK(page.has_value());
    QB_CHECK(page->id == stored_a.id);
    QB_CHECK(page->title == page_a.title);
    QB_CHECK(page->body == page_a.body);
    const auto restored_hits =
        qbrain::search::fts_search(restored, "xylophonequantum", 10);
    QB_CHECK(restored_hits.size() == 1);
    QB_CHECK(restored_hits[0].page_id == stored_a.id);
    QB_CHECK(qbrain::storage::check_schema_integrity(restored.db()).ok);
    restored.close();
  }

  brain.close();

  // ---- G7: migration suite (AA6 + v1→v13 full apply, P2-1) ----
  {
    // Fresh Brain DB: schema v13 with exactly one row per applied version.
    const fs::path dir = root / "g7_migrate";
    fs::create_directories(dir);
    const std::string path = qbrain::util::path_to_utf8(dir / "brain.db");

    const auto assert_v13 = [](qbrain::storage::Database& db) {
      QB_CHECK(n35_count(db, "SELECT COALESCE(MAX(version),0) FROM schema_version") ==
               13);
      QB_CHECK(n35_count(db, "SELECT COUNT(*) FROM schema_version") == 13);
      QB_CHECK(n35_count(db,
                         "SELECT COUNT(*) FROM schema_version WHERE version=13") ==
               1);
      QB_CHECK(qbrain::storage::check_schema_integrity(db).ok);
    };

    {
      qbrain::Brain fresh("n35_contract");
      fresh.open_at(path);
      assert_v13(fresh.db());
      fresh.close();
    }
    {
      // Second open is a no-op: no new version rows, still exactly 13.
      qbrain::Brain again("n35_contract");
      again.open_at(path);
      assert_v13(again.db());
      again.close();
    }

    // v1→v13 full apply: construct the oldest shape the migrator supports
    // (a canonical v1 database: embedded schema + single version row 1),
    // then apply_migrations must run the real v2..v13 DDL to reach v13.
    const std::string v1_path = qbrain::util::path_to_utf8(dir / "v1.db");
    qbrain::storage::Database v1;
    v1.open(v1_path);
    v1.exec("CREATE TABLE IF NOT EXISTS schema_version ("
            "version INTEGER NOT NULL PRIMARY KEY, "
            "applied_at TEXT NOT NULL DEFAULT (datetime('now')));");
    v1.exec("BEGIN;");
    v1.exec(qbrain::storage::kCanonicalSchemaSql);
    v1.exec("COMMIT;");
    v1.exec("INSERT OR IGNORE INTO schema_version(version) VALUES (1);");
    QB_CHECK(n35_count(v1, "SELECT COALESCE(MAX(version),0) FROM schema_version") ==
             1);
    QB_CHECK(n35_count(v1, "SELECT COUNT(*) FROM schema_version") == 1);
    // The v1 shape genuinely predates later migrations (e.g. no v4 columns).
    QB_CHECK(n35_count(v1,
                       "SELECT COUNT(*) FROM pragma_table_info('pages') "
                       "WHERE name='source_kind'") == 0);

    qbrain::storage::apply_migrations(v1);
    assert_v13(v1);

    // Idempotence at storage level: a second open+apply adds nothing.
    v1.close();
    v1.open(v1_path);
    qbrain::storage::apply_migrations(v1);
    assert_v13(v1);
    v1.close();
  }

  // ---- G8: error classification — three distinguishable outcomes ----
  {
    const fs::path dir = root / "g8_errors";
    fs::create_directories(dir);
    const std::string path = qbrain::util::path_to_utf8(dir / "errors.db");

    qbrain::storage::Database a;
    a.open(path);
    a.exec("CREATE TABLE n35_err("
           "id INTEGER PRIMARY KEY, tag TEXT NOT NULL UNIQUE);");
    a.exec("INSERT INTO n35_err(tag) VALUES('only');");

    std::string constraint_message;
    bool constraint_seen = false;
    try {
      a.exec("INSERT INTO n35_err(tag) VALUES('only');");  // duplicate unique key
    } catch (const std::exception& e) {
      constraint_seen = true;
      constraint_message = e.what();
    }
    QB_CHECK(constraint_seen);

    std::string syntax_message;
    bool syntax_seen = false;
    try {
      a.exec("SELECTT nope FROM n35_err;");
    } catch (const std::exception& e) {
      syntax_seen = true;
      syntax_message = e.what();
    }
    QB_CHECK(syntax_seen);

    qbrain::storage::Database b;
    b.open(path);
    a.exec("BEGIN IMMEDIATE;");
    a.exec("INSERT INTO n35_err(tag) VALUES('held');");
    std::string busy_message;
    bool busy_seen = false;
    try {
      b.exec("INSERT INTO n35_err(tag) VALUES('clash');");
    } catch (const std::exception& e) {
      busy_seen = true;
      busy_message = e.what();
    }
    QB_CHECK(busy_seen);
    a.exec("COMMIT;");

    // Each error lands in exactly its documented class...
    QB_CHECK(n35_classify(constraint_message) == N35Err::kConstraint);
    QB_CHECK(n35_classify(syntax_message) == N35Err::kSyntax);
    QB_CHECK(n35_classify(busy_message) == N35Err::kBusy);
    // ...the raw outcomes are pairwise distinct...
    QB_CHECK(constraint_message != syntax_message);
    QB_CHECK(constraint_message != busy_message);
    QB_CHECK(syntax_message != busy_message);
    // ...and no class marker leaks into another class's message.
    QB_CHECK(constraint_message.find("syntax error") == std::string::npos);
    QB_CHECK(constraint_message.find("locked") == std::string::npos);
    QB_CHECK(syntax_message.find("constraint failed") == std::string::npos);
    QB_CHECK(busy_message.find("constraint failed") == std::string::npos);
    QB_CHECK(busy_message.find("syntax error") == std::string::npos);

    a.close();
    b.close();
  }

  fs::remove_all(root);
}
