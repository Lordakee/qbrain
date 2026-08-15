#include "qbrain/storage/database.hpp"
#include <stdexcept>
#include <utility>

namespace qbrain::storage {

Database::~Database() { close(); }

Database::Database(Database&& o) noexcept : db_(o.db_) { o.db_ = nullptr; }

Database& Database::operator=(Database&& o) noexcept {
  if (this != &o) {
    close();
    db_ = o.db_;
    o.db_ = nullptr;
  }
  return *this;
}

void Database::check(int rc, std::string_view what) const {
  if (rc == SQLITE_OK || rc == SQLITE_ROW || rc == SQLITE_DONE) return;
  std::string msg = std::string(what) + ": ";
  msg += db_ ? sqlite3_errmsg(db_) : "no db";
  throw std::runtime_error(msg);
}

void Database::open(const std::string& path) {
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

void Database::close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void Database::exec(std::string_view sql) {
  char* err = nullptr;
  int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::string m = err ? err : "exec failed";
    sqlite3_free(err);
    throw std::runtime_error(m);
  }
}

int64_t Database::last_insert_rowid() const { return sqlite3_last_insert_rowid(db_); }
int Database::changes() const { return sqlite3_changes(db_); }

Database::Statement::~Statement() {
  if (stmt_) sqlite3_finalize(stmt_);
}

Database::Statement::Statement(Statement&& o) noexcept : stmt_(o.stmt_) { o.stmt_ = nullptr; }

Database::Statement& Database::Statement::operator=(Statement&& o) noexcept {
  if (this != &o) {
    if (stmt_) sqlite3_finalize(stmt_);
    stmt_ = o.stmt_;
    o.stmt_ = nullptr;
  }
  return *this;
}

void Database::Statement::prepare(Database& db, std::string_view sql) {
  if (stmt_) {
    sqlite3_finalize(stmt_);
    stmt_ = nullptr;
  }
  int rc = sqlite3_prepare_v2(db.handle(), std::string(sql).c_str(), -1, &stmt_, nullptr);
  if (rc != SQLITE_OK) {
    throw std::runtime_error(std::string("prepare: ") + sqlite3_errmsg(db.handle()));
  }
}

void Database::Statement::reset() { sqlite3_reset(stmt_); }
void Database::Statement::clear_bindings() { sqlite3_clear_bindings(stmt_); }

void Database::Statement::bind_int(int idx, int64_t v) {
  sqlite3_bind_int64(stmt_, idx, v);
}
void Database::Statement::bind_double(int idx, double v) {
  sqlite3_bind_double(stmt_, idx, v);
}
void Database::Statement::bind_text(int idx, std::string_view v) {
  sqlite3_bind_text(stmt_, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
}
void Database::Statement::bind_blob(int idx, const void* data, int size) {
  sqlite3_bind_blob(stmt_, idx, data, size, SQLITE_TRANSIENT);
}
void Database::Statement::bind_null(int idx) { sqlite3_bind_null(stmt_, idx); }

bool Database::Statement::step() {
  int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) return true;
  if (rc == SQLITE_DONE) return false;
  throw std::runtime_error(std::string("step: ") + sqlite3_errstr(rc));
}

void Database::Statement::step_done() {
  if (step()) {
    // Write statements should not return rows; drain extras (audit P1-6).
    // Avoid silent single-row ignore without consuming the rest of the result.
    int extra = 1;
    while (step()) ++extra;
    (void)extra;
  }
}

int64_t Database::Statement::column_int(int i) const { return sqlite3_column_int64(stmt_, i); }
double Database::Statement::column_double(int i) const { return sqlite3_column_double(stmt_, i); }

std::string Database::Statement::column_text(int i) const {
  const unsigned char* p = sqlite3_column_text(stmt_, i);
  if (!p) return {};
  const int size = sqlite3_column_bytes(stmt_, i);
  return std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(size));
}

std::vector<uint8_t> Database::Statement::column_blob(int i) const {
  const void* p = sqlite3_column_blob(stmt_, i);
  int n = sqlite3_column_bytes(stmt_, i);
  if (!p || n <= 0) return {};
  const auto* b = static_cast<const uint8_t*>(p);
  return std::vector<uint8_t>(b, b + n);
}

bool Database::Statement::column_is_null(int i) const {
  return sqlite3_column_type(stmt_, i) == SQLITE_NULL;
}

Database::Statement Database::prepare(std::string_view sql) {
  Statement st;
  st.prepare(*this, sql);
  return st;
}

}  // namespace qbrain::storage
