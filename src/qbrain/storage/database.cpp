#include "qbrain/storage/database.hpp"
#include "qbrain/util/string_util.hpp"
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace qbrain::storage {

namespace {

// N38 D0.5 (P0-3): moved verbatim from search/hybrid.cpp (fts_quote).
// Join tokens with spaces; lowercase to neutralize FTS5 reserved words
// (AND/OR/NOT). Quoting the raw user query is SQLite/FTS5-specific
// normalization, so it belongs to the backend, not the search layer.
static std::string fts_quote(const std::string& q) {
  auto parts = util::split(q, ' ');
  std::string out;
  for (auto& p : parts) {
    p = util::trim(p);
    if (p.empty()) continue;
    p = util::replace_all(p, "\"", "");
    p = util::to_lower(p);
    if (p.empty()) continue;
    if (!out.empty()) out += " ";
    out += "\"" + p + "\"";
  }
  return out.empty() ? "\"\"" : out;
}

// N35 D1: SqliteBackend is a line-by-line extraction of the former
// storage::Database / Database::Statement method bodies (pre-N35
// src/qbrain/storage/database.cpp). Every extracted body below is copied
// verbatim -- same sqlite3_* calls, same arguments, same error handling,
// same order of operations; only the enclosing class changed. Zero logic
// change is the N35 equivalence contract (adopted P1-4): the 36 pre-existing
// tests must stay green without modification.
class SqliteBackend final : public IStorageBackend {
 public:
  // ---- extracted from the former Database bodies (verbatim) ----

  void open(const std::string& path) override {
    close();
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
      std::string err = db_ ? sqlite3_errmsg(db_) : "open failed";
      if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
      }
      throw std::runtime_error("sqlite open: " + err + " path=" + path);
    }
    exec("PRAGMA foreign_keys = ON;");
    exec("PRAGMA journal_mode = WAL;");
    exec("PRAGMA synchronous = NORMAL;");
  }

  void close() override {
    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
  }

  bool is_open() const override { return db_ != nullptr; }

  // N38 D0.5 (P0-1): no longer an IStorageBackend override. The raw
  // connection stays reachable on this concrete class (statement wiring
  // below, storage::Database::handle sqlite test hook).
  sqlite3* handle() const { return db_; }

  void exec(std::string_view sql) override {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      std::string m = err ? err : "exec failed";
      sqlite3_free(err);
      throw std::runtime_error(m);
    }
  }

  int64_t last_insert_rowid() const override { return sqlite3_last_insert_rowid(db_); }
  int changes() const override { return sqlite3_changes(db_); }

  // Preserved verbatim from the former private Database::check (it had no
  // callers at extraction time; it keeps its original behavior here).
  void check(int rc, std::string_view what) const {
    if (rc == SQLITE_OK || rc == SQLITE_ROW || rc == SQLITE_DONE) return;
    std::string msg = std::string(what) + ": ";
    msg += db_ ? sqlite3_errmsg(db_) : "no db";
    throw std::runtime_error(msg);
  }

  // ---- N35 contract capabilities (no legacy Database equivalent) ----
  // Transactions and busy/error-classification hooks run through the same
  // exec()/sqlite3 paths as the legacy callers' equivalent statements.

  void begin_transaction() override { exec("BEGIN;"); }
  void begin_immediate_transaction() override { exec("BEGIN IMMEDIATE;"); }
  void commit_transaction() override { exec("COMMIT;"); }
  void rollback_transaction() override { exec("ROLLBACK;"); }

  int set_busy_timeout(int ms) override { return sqlite3_busy_timeout(db_, ms); }

  int last_error_code() const override { return db_ ? sqlite3_errcode(db_) : SQLITE_OK; }

  // ---- N38 D0.5 interface extensions (plan disposition P0-1 / P0-3) ----

  // sqlite3_db_filename never returns nullptr for "main" (it returns "" for
  // :memory: / temporary stores); the null guard preserves the historical
  // call-site check exactly.
  std::string backend_file_path() const override {
    const char* main_file = sqlite3_db_filename(db_, "main");
    return main_file ? std::string(main_file) : std::string();
  }

  // N38 D0.5: the pre-migration backup body moved verbatim from
  // migrate.cpp:backup_db_file_before_migration (same sqlite3_backup_*
  // sequence, same WAL-checkpoint-to-delete-journal handling on the
  // destination, same fatal error texts). Returns true when the backup
  // file was written; failures throw, exactly as before.
  bool backup_to(const std::string& dest) override {
    sqlite3* to = nullptr;
    if (sqlite3_open(dest.c_str(), &to) != SQLITE_OK) {
      if (to) sqlite3_close(to);
      throw std::runtime_error("pre-migration backup open failed: " + dest);
    }
    bool ok = true;
    sqlite3_backup* bak = sqlite3_backup_init(to, "main", db_, "main");
    if (bak) {
      const int rc = sqlite3_backup_step(bak, -1);
      sqlite3_backup_finish(bak);
      ok = rc == SQLITE_DONE && sqlite3_errcode(to) == SQLITE_OK;
    } else {
      ok = false;
    }
    // The backup image inherits the source WAL mode; checkpoint it back to a
    // rollback journal so the .bak is one self-contained file (no -wal/-shm
    // sidecars to lose) and the rollback path is a plain file restore.
    if (ok) {
      char* err = nullptr;
      const int rc = sqlite3_exec(to, "PRAGMA journal_mode=DELETE;", nullptr, nullptr, &err);
      if (err) sqlite3_free(err);
      ok = rc == SQLITE_OK;
    }
    const int close_rc = sqlite3_close(to);
    if (!ok || close_rc != SQLITE_OK) {
      throw std::runtime_error("pre-migration backup failed: " + dest);
    }
    return true;
  }

  // N38 D0.5 (P0-3): the FTS5 external-content MATCH statement that lived in
  // search/hybrid.cpp, moved verbatim (same text, same bind order, same
  // optional source_id filter, same ORDER BY). Runs through the same
  // IStatement path as every legacy prepare, so error texts are identical.
  std::vector<FtsRow> fts_search(const std::string& query, int limit,
                                 const std::string& source_id) override {
    std::vector<FtsRow> out;
    auto mq = fts_quote(query);
    std::string sql = R"SQL(
SELECT p.id, p.slug, p.title, p.type, snippet(pages_fts, 2, '', '', '…', 16),
       bm25(pages_fts) AS rank
FROM pages_fts
JOIN pages p ON p.id = pages_fts.rowid
WHERE pages_fts MATCH ? AND p.deleted_at IS NULL
)SQL";
    if (!source_id.empty()) sql += " AND p.source_id = ?";
    sql += " ORDER BY rank ASC, p.slug COLLATE BINARY ASC LIMIT ?";
    auto st = create_statement();
    st->prepare(*this, sql);
    int idx = 1;
    st->bind_text(idx++, mq);
    if (!source_id.empty()) st->bind_text(idx++, source_id);
    st->bind_int(idx, limit);
    while (st->step()) {
      FtsRow r;
      r.page_id = st->column_int(0);
      r.slug = st->column_text(1);
      r.title = st->column_text(2);
      r.type = st->column_text(3);
      r.snippet = st->column_text(4);
      r.rank = st->column_double(5);
      out.push_back(std::move(r));
    }
    return out;
  }

  // ---- statements: extracted from the former Database::Statement bodies ----

  class SqliteStatement final : public IStatement {
   public:
    ~SqliteStatement() override {
      if (stmt_) sqlite3_finalize(stmt_);
    }

    void prepare(IStorageBackend& db, std::string_view sql) override {
      if (stmt_) {
        sqlite3_finalize(stmt_);
        stmt_ = nullptr;
      }
      // N38 D0.5: handle() is SqliteBackend-internal now; the statement is
      // only ever prepared against the backend that created it.
      auto& sqlite_db = static_cast<SqliteBackend&>(db);
      int rc = sqlite3_prepare_v2(sqlite_db.handle(), std::string(sql).c_str(), -1, &stmt_, nullptr);
      if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string("prepare: ") + sqlite3_errmsg(sqlite_db.handle()));
      }
    }
    void reset() override { sqlite3_reset(stmt_); }
    void clear_bindings() override { sqlite3_clear_bindings(stmt_); }

    void bind_int(int idx, int64_t v) override {
      sqlite3_bind_int64(stmt_, idx, v);
    }
    void bind_double(int idx, double v) override {
      sqlite3_bind_double(stmt_, idx, v);
    }
    void bind_text(int idx, std::string_view v) override {
      sqlite3_bind_text(stmt_, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    }
    void bind_blob(int idx, const void* data, int size) override {
      sqlite3_bind_blob(stmt_, idx, data, size, SQLITE_TRANSIENT);
    }
    void bind_null(int idx) override { sqlite3_bind_null(stmt_, idx); }

    bool step() override {
      int rc = sqlite3_step(stmt_);
      if (rc == SQLITE_ROW) return true;
      if (rc == SQLITE_DONE) return false;
      throw std::runtime_error(std::string("step: ") + sqlite3_errstr(rc));
    }

    void step_done() override {
      if (step()) {
        // Write statements should not return rows; drain extras (audit P1-6).
        // Avoid silent single-row ignore without consuming the rest of the result.
        int extra = 1;
        while (step()) ++extra;
        (void)extra;
      }
    }

    int64_t column_int(int i) const override { return sqlite3_column_int64(stmt_, i); }
    double column_double(int i) const override { return sqlite3_column_double(stmt_, i); }

    std::string column_text(int i) const override {
      const unsigned char* p = sqlite3_column_text(stmt_, i);
      if (!p) return {};
      const int size = sqlite3_column_bytes(stmt_, i);
      return std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(size));
    }

    std::vector<uint8_t> column_blob(int i) const override {
      const void* p = sqlite3_column_blob(stmt_, i);
      int n = sqlite3_column_bytes(stmt_, i);
      if (!p || n <= 0) return {};
      const auto* b = static_cast<const uint8_t*>(p);
      return std::vector<uint8_t>(b, b + n);
    }

    bool column_is_null(int i) const override {
      return sqlite3_column_type(stmt_, i) == SQLITE_NULL;
    }

   private:
    sqlite3_stmt* stmt_ = nullptr;
  };

  std::unique_ptr<IStatement> create_statement() override {
    return std::make_unique<SqliteStatement>();
  }

 private:
  sqlite3* db_ = nullptr;
};

// N35 D1: compile-time proof that the concrete backend implements the
// storage contract.
static_assert(std::is_base_of_v<IStorageBackend, SqliteBackend>,
              "SqliteBackend must implement the N35 IStorageBackend contract");

}  // namespace

std::unique_ptr<IStorageBackend> make_sqlite_backend() {
  return std::make_unique<SqliteBackend>();
}

// ---------------------------------------------------------------------------
// storage::Database -- forwarding shim over IStorageBackend (N35 D1).
// Public API and observable behavior are unchanged. The null-backend_ /
// null-impl_ branches below reproduce what the old direct sqlite3_* calls did
// with a null handle (verified against the vendored amalgamation 3.46.1, no
// SQLITE_ENABLE_API_ARMOR), so the extraction cannot introduce latent UB
// where there was none:
//   - exec with no connection: sqlite3_exec(nullptr,..) returned SQLITE_MISUSE
//     with a null errmsg -> exactly this "exec failed" throw.
//   - last_insert_rowid/changes with no connection: undefined (null deref in
//     sqlite); guarded to 0. No caller exercises this state.
//   - step on a never-prepared statement: sqlite3_step(nullptr) returned
//     SQLITE_MISUSE -> the same "step: ..." throw (sqlite3_errstr of the rc).
//   - reset/clear_bindings/bind_* on a never-prepared statement: SQLITE_OK or
//     unchecked SQLITE_MISUSE -> silent no-ops.
//   - column_* on a never-prepared statement: neutral values (0 / 0.0 / "" /
//     {} / NULL) from sqlite's null statement-value guard.
// ---------------------------------------------------------------------------

Database::~Database() { close(); }

Database::Database(Database&& o) noexcept : backend_(std::move(o.backend_)) {
  // o.backend_ is left null -- the same moved-from state as the old
  // "o.db_ = nullptr".
}

Database& Database::operator=(Database&& o) noexcept {
  if (this != &o) {
    close();
    backend_ = std::move(o.backend_);
  }
  return *this;
}

void Database::open(const std::string& path) {
  if (!backend_) backend_ = make_sqlite_backend();  // re-arm after move-from
  backend_->open(path);
}

void Database::close() {
  if (backend_) backend_->close();
}

void Database::exec(std::string_view sql) {
  if (!backend_) throw std::runtime_error("exec failed");
  backend_->exec(sql);
}

int64_t Database::last_insert_rowid() const {
  return backend_ ? backend_->last_insert_rowid() : 0;
}
int Database::changes() const { return backend_ ? backend_->changes() : 0; }

// N38-B: concrete-type discrimination. SqliteBackend is defined in this
// translation unit, and dynamic_cast is available in every supported build
// path (MSVC default RTTI, verified against the scripts/build-cl.ps1 flag
// set), so any backend installed through the facade answers correctly
// without extending the IStorageBackend contract (backend.hpp is N38-A's).
// Only consulted by dialect-guarded branches.
BackendKind Database::backend_kind() const {
  return backend_ && dynamic_cast<const SqliteBackend*>(backend_.get()) != nullptr
             ? BackendKind::sqlite
             : (backend_ ? BackendKind::postgres : BackendKind::sqlite);
}

void Database::adopt_backend(std::unique_ptr<IStorageBackend> backend) {
  // N38-B wiring seam: mirror open()'s "close previous connection first"
  // semantics, then hold the caller's backend. Null disarms the facade
  // (same observable state as a moved-from Database).
  if (backend_) backend_->close();
  backend_ = std::move(backend);
}

sqlite3* Database::handle() const {
  // N38 D0.5 (P0-1): sqlite test hook. handle() is no longer part of the
  // IStorageBackend contract; the raw connection remains reachable on the
  // SQLite default path only (tests use authorizer/serialize/update_hook
  // through it). open() arms backend_ exclusively via make_sqlite_backend()
  // and moves preserve the same object, so the downcast is exact. Returns
  // nullptr for a moved-from Database -- the pre-N38 inline accessor did the
  // same via its null-backend_ branch.
  // N38-A coordination item: a non-SQLite backend (PG mode) must never be
  // downcast -- doing so hands tests a sqlite3*-typed PGconn and corrupts it.
  // Return nullptr instead; callers are test hooks that no-op on null.
  if (!backend_ || backend_kind() != BackendKind::sqlite) return nullptr;
  return static_cast<SqliteBackend*>(backend_.get())->handle();
}

std::string Database::backend_file_path() const {
  return backend_ ? backend_->backend_file_path() : std::string();
}

bool Database::backup_to(const std::string& dest) { return backend_->backup_to(dest); }

std::vector<FtsRow> Database::fts_search(const std::string& query, int limit,
                                         const std::string& source_id) {
  if (!backend_) {
    // Moved-from Database: the legacy raw-SQL path failed inside Statement
    // prepare ("prepare: no db"); surface the same failure here.
    throw std::runtime_error("prepare: no db");
  }
  return backend_->fts_search(query, limit, source_id);
}

Database::Statement::~Statement() = default;

Database::Statement::Statement(Statement&& o) noexcept : impl_(std::move(o.impl_)) {}

Database::Statement& Database::Statement::operator=(Statement&& o) noexcept {
  if (this != &o) {
    impl_ = std::move(o.impl_);
  }
  return *this;
}

void Database::Statement::prepare(Database& db, std::string_view sql) {
  if (!db.backend_) {
    // Moved-from Database (old db_ == nullptr): the old code reached
    // sqlite3_prepare_v2(nullptr, ...), which is undefined in sqlite; fail
    // closed with the legacy message shape instead of UB.
    throw std::runtime_error("prepare: no db");
  }
  if (!impl_) impl_ = db.backend_->create_statement();
  impl_->prepare(*db.backend_, sql);
}

void Database::Statement::reset() {
  if (impl_) impl_->reset();
}
void Database::Statement::clear_bindings() {
  if (impl_) impl_->clear_bindings();
}

void Database::Statement::bind_int(int idx, int64_t v) {
  if (impl_) impl_->bind_int(idx, v);
}
void Database::Statement::bind_double(int idx, double v) {
  if (impl_) impl_->bind_double(idx, v);
}
void Database::Statement::bind_text(int idx, std::string_view v) {
  if (impl_) impl_->bind_text(idx, v);
}
void Database::Statement::bind_blob(int idx, const void* data, int size) {
  if (impl_) impl_->bind_blob(idx, data, size);
}
void Database::Statement::bind_null(int idx) {
  if (impl_) impl_->bind_null(idx);
}

bool Database::Statement::step() {
  if (!impl_) {
    // Never-prepared statement: sqlite3_step(nullptr) returned SQLITE_MISUSE
    // and took the generic branch below with that rc.
    throw std::runtime_error(std::string("step: ") + sqlite3_errstr(SQLITE_MISUSE));
  }
  return impl_->step();
}

void Database::Statement::step_done() {
  if (!impl_) {
    step();  // old path: step() on the null statement threw here as well
    return;
  }
  impl_->step_done();
}

int64_t Database::Statement::column_int(int i) const {
  return impl_ ? impl_->column_int(i) : 0;
}
double Database::Statement::column_double(int i) const {
  return impl_ ? impl_->column_double(i) : 0.0;
}

std::string Database::Statement::column_text(int i) const {
  if (!impl_) return {};
  return impl_->column_text(i);
}

std::vector<uint8_t> Database::Statement::column_blob(int i) const {
  if (!impl_) return {};
  return impl_->column_blob(i);
}

bool Database::Statement::column_is_null(int i) const {
  return !impl_ || impl_->column_is_null(i);
}

Database::Statement Database::prepare(std::string_view sql) {
  Statement st;
  st.prepare(*this, sql);
  return st;
}

}  // namespace qbrain::storage
