#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <sqlite3.h>

namespace qbrain::storage {

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
  bool is_open() const { return db_ != nullptr; }
  sqlite3* handle() const { return db_; }

  void exec(std::string_view sql);
  int64_t last_insert_rowid() const;
  int changes() const;

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
    sqlite3_stmt* stmt_ = nullptr;
  };

  Statement prepare(std::string_view sql);

 private:
  sqlite3* db_ = nullptr;
  void check(int rc, std::string_view what) const;
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
