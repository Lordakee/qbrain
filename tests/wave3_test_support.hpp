#pragma once

#include "qbrain/core/brain.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/util/hash.hpp"
#include <sqlite3.h>

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace qbrain::test_support {

inline std::string snapshot_cell(sqlite3_stmt* statement, int column) {
  std::string out = std::to_string(sqlite3_column_type(statement, column)) + ":";
  switch (sqlite3_column_type(statement, column)) {
    case SQLITE_NULL:
      break;
    case SQLITE_INTEGER:
      out += std::to_string(sqlite3_column_int64(statement, column));
      break;
    case SQLITE_FLOAT: {
      std::ostringstream value;
      value << std::setprecision(std::numeric_limits<double>::max_digits10)
            << sqlite3_column_double(statement, column);
      out += value.str();
      break;
    }
    case SQLITE_TEXT: {
      const auto* data = sqlite3_column_text(statement, column);
      const int size = sqlite3_column_bytes(statement, column);
      out += std::to_string(size) + ":";
      if (data && size > 0) out.append(reinterpret_cast<const char*>(data), size);
      break;
    }
    case SQLITE_BLOB: {
      const auto* data = static_cast<const unsigned char*>(sqlite3_column_blob(statement, column));
      const int size = sqlite3_column_bytes(statement, column);
      static constexpr char kHex[] = "0123456789abcdef";
      out += std::to_string(size) + ":";
      for (int index = 0; index < size; ++index) {
        out.push_back(kHex[data[index] >> 4]);
        out.push_back(kHex[data[index] & 0x0f]);
      }
      break;
    }
  }
  return out;
}

inline std::string logical_snapshot(Brain& brain) {
  sqlite3* db = brain.db().handle();
  sqlite3_stmt* schema_statement = nullptr;
  const char* schema_sql =
      "SELECT type,name,COALESCE(tbl_name,''),COALESCE(sql,'') FROM sqlite_master "
      "WHERE name NOT LIKE 'sqlite_%' OR name='sqlite_sequence' ORDER BY type,name";
  if (sqlite3_prepare_v2(db, schema_sql, -1, &schema_statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("snapshot schema discovery failed");
  }
  std::string snapshot = "schema\n";
  while (sqlite3_step(schema_statement) == SQLITE_ROW) {
    for (int column = 0; column < 4; ++column) {
      if (column) snapshot.push_back('|');
      snapshot += snapshot_cell(schema_statement, column);
    }
    snapshot.push_back('\n');
  }
  sqlite3_finalize(schema_statement);

  sqlite3_stmt* tables_statement = nullptr;
  const char* tables_sql =
      "SELECT name FROM sqlite_master WHERE type='table' AND "
      "(name NOT LIKE 'sqlite_%' OR name='sqlite_sequence') "
      "ORDER BY name";
  if (sqlite3_prepare_v2(db, tables_sql, -1, &tables_statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("snapshot table discovery failed");
  }
  std::vector<std::string> tables;
  while (sqlite3_step(tables_statement) == SQLITE_ROW) {
    const auto* name = sqlite3_column_text(tables_statement, 0);
    if (name) tables.emplace_back(reinterpret_cast<const char*>(name));
  }
  sqlite3_finalize(tables_statement);

  for (const auto& table : tables) {
    sqlite3_stmt* rows_statement = nullptr;
    const std::string sql = "SELECT * FROM \"" + table + "\"";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &rows_statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("snapshot row read failed: " + table);
    }
    std::vector<std::string> rows;
    const int columns = sqlite3_column_count(rows_statement);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(rows_statement)) == SQLITE_ROW) {
      std::string row;
      for (int column = 0; column < columns; ++column) {
        if (column) row.push_back('|');
        row += snapshot_cell(rows_statement, column);
      }
      rows.push_back(std::move(row));
    }
    sqlite3_finalize(rows_statement);
    if (result != SQLITE_DONE) throw std::runtime_error("snapshot scan failed: " + table);
    std::sort(rows.begin(), rows.end());
    snapshot += table + "#" + std::to_string(columns) + "\n";
    for (const auto& row : rows) snapshot += row + "\n";
  }
  return snapshot;
}

inline std::string snapshot_sha256(std::string_view snapshot) {
  return qbrain::util::sha256_hex(snapshot);
}

inline int64_t scalar(Brain& brain, const std::string& sql) {
  auto statement = brain.db().prepare(sql);
  return statement.step() ? statement.column_int(0) : 0;
}

inline Page put_page(Brain& brain, const std::string& source_id, const std::string& slug,
                     const std::string& body, const std::string& type = "note") {
  PageInput input;
  input.source_id = source_id;
  input.slug = slug;
  input.title = slug;
  input.body = body;
  input.type = type;
  return brain.put_page(input);
}

inline ops::OpResult call_op(Brain& brain, const std::string& name,
                             std::unordered_map<std::string, std::string> args = {},
                             bool remote = false, bool allow_write = false) {
  ops::OpContext context;
  context.brain = &brain;
  // N30: historical tests used remote=1 to mean an MCP client; the transport
  // distinction is now remote(HTTP) vs via_mcp(any MCP). Map the old meaning.
  context.via_mcp = remote;
  context.remote = false;
  context.allow_write = allow_write;
  context.args = std::move(args);
  return ops::global_registry().call(name, context);
}

}  // namespace qbrain::test_support
