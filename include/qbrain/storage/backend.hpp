#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <sqlite3.h>

namespace qbrain::storage {

// N35 D1 storage contract interface. IStorageBackend is the explicit boundary
// between storage::Database (which delegates its whole public surface through
// this interface) and the concrete backend. The default backend is SQLite
// (SqliteBackend, src/qbrain/storage/database.cpp -- a line-by-line extraction
// of the former Database method bodies, zero logic change; the 36 pre-existing
// tests staying green unmodified is the post-hoc equivalence proof).
//
// Error semantics (adopted P2-4): operations that exist on the legacy
// Database surface report failures exactly as before (std::runtime_error with
// the historical message text, preserved verbatim by the SQLite adapter).
// Error CLASSIFICATION is exposed as int result codes using raw SQLite rc
// values (e.g. SQLITE_CONSTRAINT, SQLITE_ERROR, SQLITE_BUSY); no new enum is
// defined. The code-to-condition mapping table lives in
// docs/10-STORAGE-CONTRACT.md.
//
// Escape hatch (documented): handle() exposes the raw sqlite3* connection.
// Capabilities not modeled as interface methods -- the pre-migration file
// backup in migrations (SQLite online backup API, per adopted P2-3 backup is
// NOT a mandatory interface capability), sqlite3_db_filename, and test
// hooks -- operate through handle().
//
// Future backends: any second backend must pass the same contract suite
// (tests/test_n35.cpp) before it may be listed in docs/OPS-PARITY-LEDGER.md.
class IStorageBackend {
 public:
  // Prepared-statement half of the contract. One IStatement owns one native
  // statement. prepare() on an already-prepared statement finalizes and
  // re-prepares it in place (legacy storage::Database::Statement semantics).
  class IStatement {
   public:
    virtual ~IStatement() = default;

    virtual void prepare(IStorageBackend& db, std::string_view sql) = 0;
    virtual void reset() = 0;
    virtual void clear_bindings() = 0;
    virtual void bind_int(int idx, int64_t v) = 0;
    virtual void bind_double(int idx, double v) = 0;
    virtual void bind_text(int idx, std::string_view v) = 0;
    virtual void bind_blob(int idx, const void* data, int size) = 0;
    virtual void bind_null(int idx) = 0;
    virtual bool step() = 0;
    virtual void step_done() = 0;

    virtual int64_t column_int(int i) const = 0;
    virtual double column_double(int i) const = 0;
    virtual std::string column_text(int i) const = 0;
    virtual std::vector<uint8_t> column_blob(int i) const = 0;
    virtual bool column_is_null(int i) const = 0;
  };

  virtual ~IStorageBackend() = default;

  // Connection lifecycle. open() applies the same PRAGMA sequence as the
  // legacy storage::Database::open (foreign_keys=ON, journal_mode=WAL,
  // synchronous=NORMAL) and closes any previously open connection first.
  virtual void open(const std::string& path) = 0;
  virtual void close() = 0;
  virtual bool is_open() const = 0;
  virtual sqlite3* handle() const = 0;  // documented escape hatch (see above)

  // Direct execution.
  virtual void exec(std::string_view sql) = 0;
  virtual int64_t last_insert_rowid() const = 0;
  virtual int changes() const = 0;

  // Prepared statements.
  virtual std::unique_ptr<IStatement> create_statement() = 0;

  // Transactions. Legacy callers drive transactions through exec("BEGIN;")
  // / exec("BEGIN IMMEDIATE;") / exec("COMMIT;") / exec("ROLLBACK;"); these
  // contract entry points have identical semantics for the SQLite backend
  // (they run through the same exec path with the same statement text).
  virtual void begin_transaction() = 0;
  virtual void begin_immediate_transaction() = 0;
  virtual void commit_transaction() = 0;
  virtual void rollback_transaction() = 0;

  // Busy handling: enable the connection-level busy timeout. The legacy
  // default is no timeout (SQLITE_BUSY surfaces immediately, which the
  // callers' busy-retry loops digest). Returns the sqlite3_busy_timeout
  // result code as int.
  virtual int set_busy_timeout(int ms) = 0;

  // Error classification (adopted P2-4): last connection error code as an
  // int SQLite rc. SQLITE_OK when the connection is not open.
  virtual int last_error_code() const = 0;
};

// Factory for the default SQLite backend (Windows-default per the N35
// resolution). Constructing backends through this factory keeps the concrete
// implementation replaceable, which is the structure requirement the N35
// contract suite (tests/test_n35.cpp) relies on.
std::unique_ptr<IStorageBackend> make_sqlite_backend();

}  // namespace qbrain::storage
