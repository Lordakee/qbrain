#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <sqlite3.h>

#include "qbrain/storage/backend.hpp"

namespace qbrain::storage {

// N38-B (SQL census, guarded-branch rewrites): backend discriminator for the
// dialect-guarded call sites (PRAGMA/sqlite_master/typeof introspection in
// migrate.cpp, packs.cpp). sqlite is the default and historical backend;
// postgres is reported once the N38 PG backend is installed through the
// storage facade. Kept here (not backend.hpp) so the discriminator is part
// of the Database vocabulary every caller already includes.
enum class BackendKind { sqlite, postgres };

// N35 D1: Database keeps its exact pre-N35 public API; the native storage
// state now lives behind the IStorageBackend contract (backend.hpp), to which
// every method delegates one-for-one. The SQLite adapter (SqliteBackend,
// src/qbrain/storage/database.cpp) is a line-by-line extraction of the former
// method bodies -- zero logic change.
//
// N38 D0.5 (plan P0-1): backend_file_path / backup_to / fts_search forward
// to the new IStorageBackend capabilities, and handle() is no longer part of
// the contract -- it remains here as a documented sqlite test hook
// (authorizer/serialize/update_hook); production code must not call it.
class Database {
 public:
  Database() = default;
  ~Database();
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  Database(Database&& o) noexcept;
  Database& operator=(Database&& o) noexcept;

  void open(const std::string& path);
  void close();
  bool is_open() const { return backend_ && backend_->is_open(); }

  // sqlite test hook (N38 D0.5): raw connection of the default SQLite
  // backend only; NOT part of the IStorageBackend contract. Tests use it for
  // authorizer/serialize/update_hook seams.
  sqlite3* handle() const;

  void exec(std::string_view sql);
  int64_t last_insert_rowid() const;
  int changes() const;

  // N38-B: which concrete backend this facade holds. sqlite also answers for
  // a moved-from (null) Database -- the default-path answer.
  BackendKind backend_kind() const;

  // N38-B wiring seam (Brain::open_pg / n38 harness): arm this facade with
  // an externally constructed, already-open backend (PG mode), closing any
  // backend it held first. Generic by design -- no PG types cross this
  // boundary, so database.cpp stays libpq-free.
  void adopt_backend(std::unique_ptr<IStorageBackend> backend);

  // N38 D0.5 forwarding shims (see backend.hpp for semantics).
  std::string backend_file_path() const;
  bool backup_to(const std::string& dest);
  std::vector<FtsRow> fts_search(const std::string& query, int limit,
                                 const std::string& source_id);

  class Statement {
   public:
    Statement() = default;
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& o) noexcept;
    Statement& operator=(Statement&& o) noexcept;

    void prepare(Database& db, std::string_view sql);
    void reset();
    void clear_bindings();
    void bind_int(int idx, int64_t v);
    void bind_double(int idx, double v);
    void bind_text(int idx, std::string_view v);
    void bind_blob(int idx, const void* data, int size);
    void bind_null(int idx);
    bool step();
    void step_done();

    int64_t column_int(int i) const;
    double column_double(int i) const;
    std::string column_text(int i) const;
    std::vector<uint8_t> column_blob(int i) const;
    bool column_is_null(int i) const;

   private:
    // N35 D1: the native statement lives behind the contract statement
    // interface. Null exactly while this Statement has never been prepared
    // (the old stmt_ == nullptr state).
    std::unique_ptr<IStorageBackend::IStatement> impl_;
  };

  Statement prepare(std::string_view sql);

 private:
  // N35 D1: the contract owns the native connection. backend_ is null only
  // on a moved-from Database -- the same observable state the old
  // db_ == nullptr represented.
  std::unique_ptr<IStorageBackend> backend_;

  // N35 D1 acceptance assertion 1: compile-time proof that storage::Database
  // uses the storage contract -- the member type IS the interface type.
  static_assert(
      std::is_same_v<decltype(backend_), std::unique_ptr<IStorageBackend>>,
      "storage::Database must hold its storage via IStorageBackend");
};

// Apply migrations using embedded canonical schema (always).
// Optional schema_sql_path is ignored if empty; if set, used only as override for v1 bootstrap.
void apply_migrations(Database& db, const std::string& schema_sql_path = {});

struct SchemaIntegrity {
  bool ok = true;
  int schema_version = 0;
  std::vector<std::string> missing;
};

// Assert expected tables/indexes exist (detects legacy fallback-created DBs).
SchemaIntegrity check_schema_integrity(Database& db);

}  // namespace qbrain::storage
