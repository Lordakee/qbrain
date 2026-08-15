#include "qbrain/jobs/minions.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/storage/database.hpp"
#include "qbrain/util/paths.hpp"
#include "wave3_test_support.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#define QB_CHECK(cond)                                                               \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond + " @ " +    \
                               __FILE__ + ":" + std::to_string(__LINE__));          \
    }                                                                                \
  } while (0)

namespace {

using json = nlohmann::json;
using qbrain::storage::Database;
using qbrain::test_support::logical_snapshot;
using qbrain::test_support::snapshot_sha256;

struct Evidence {
  int strict_id_cases = 0;
  int replay_state_cases = 0;
  int sender_payload_cases = 0;
  int list_limit_cases = 0;
  int mcp_rejection_cases = 0;
  int replay_race_successes = 0;
  int replay_race_busy = 0;
  int message_race_successes = 0;
  int message_race_busy = 0;
  std::string selected_hash;
  std::string decoy_hash;
  std::string migration_hash;
  std::string rollback_hash;
};

Evidence g_evidence;

std::string quote_identifier(std::string_view identifier) {
  std::string quoted = "\"";
  for (const char ch : identifier) {
    quoted.push_back(ch);
    if (ch == '"') quoted.push_back('"');
  }
  quoted.push_back('"');
  return quoted;
}

std::vector<std::string> snapshot_query(sqlite3* database, const std::string& sql) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("snapshot query prepare failed");
  }
  std::vector<std::string> rows;
  int result = SQLITE_OK;
  while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
    std::string row;
    for (int column = 0; column < sqlite3_column_count(statement); ++column) {
      if (column) row.push_back('|');
      row += qbrain::test_support::snapshot_cell(statement, column);
    }
    rows.push_back(std::move(row));
  }
  sqlite3_finalize(statement);
  if (result != SQLITE_DONE) throw std::runtime_error("snapshot query scan failed");
  std::sort(rows.begin(), rows.end());
  return rows;
}

std::string rowset_snapshot(Database& database, const std::string& sql) {
  std::string snapshot;
  for (const auto& row : snapshot_query(database.handle(), sql)) snapshot += row + "\n";
  return snapshot;
}

std::string schema_snapshot(Database& database) {
  return rowset_snapshot(
      database,
      "SELECT type,name,COALESCE(tbl_name,''),COALESCE(sql,'') FROM sqlite_master "
      "WHERE name NOT LIKE 'sqlite_%' OR name='sqlite_sequence' ORDER BY type,name");
}

std::string database_snapshot(Database& database) {
  std::string snapshot = "schema\n" + schema_snapshot(database);
  sqlite3_stmt* statement = nullptr;
  const char* sql =
      "SELECT name FROM sqlite_master WHERE type='table' AND "
      "(name NOT LIKE 'sqlite_%' OR name='sqlite_sequence') ORDER BY name";
  if (sqlite3_prepare_v2(database.handle(), sql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("snapshot table discovery failed");
  }
  std::vector<std::string> tables;
  int result = SQLITE_OK;
  while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
    const auto* name = sqlite3_column_text(statement, 0);
    if (name) tables.emplace_back(reinterpret_cast<const char*>(name));
  }
  sqlite3_finalize(statement);
  if (result != SQLITE_DONE) throw std::runtime_error("snapshot table discovery scan failed");
  for (const auto& table : tables) {
    snapshot += "table:" + table + "\n";
    snapshot += rowset_snapshot(database, "SELECT * FROM " + quote_identifier(table));
  }
  return snapshot;
}

std::string rows_except(Database& database, const std::set<std::string>& excluded) {
  sqlite3_stmt* statement = nullptr;
  const char* sql =
      "SELECT name FROM sqlite_master WHERE type='table' AND "
      "(name NOT LIKE 'sqlite_%' OR name='sqlite_sequence') ORDER BY name";
  if (sqlite3_prepare_v2(database.handle(), sql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("row snapshot discovery failed");
  }
  std::vector<std::string> tables;
  int result = SQLITE_OK;
  while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
    const auto* name = sqlite3_column_text(statement, 0);
    if (name) tables.emplace_back(reinterpret_cast<const char*>(name));
  }
  sqlite3_finalize(statement);
  if (result != SQLITE_DONE) throw std::runtime_error("row snapshot discovery scan failed");

  std::string snapshot;
  for (const auto& table : tables) {
    if (excluded.contains(table)) continue;
    snapshot += "table:" + table + "\n";
    snapshot += rowset_snapshot(database, "SELECT * FROM " + quote_identifier(table));
  }
  return snapshot;
}

int64_t scalar(Database& database, const std::string& sql) {
  auto statement = database.prepare(sql);
  return statement.step() ? statement.column_int(0) : 0;
}

int64_t sequence_value(Database& database, const std::string& table) {
  auto statement = database.prepare("SELECT seq FROM sqlite_sequence WHERE name=?");
  statement.bind_text(1, table);
  return statement.step() ? statement.column_int(0) : 0;
}

bool has_table(Database& database, const std::string& name) {
  auto statement = database.prepare(
      "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1");
  statement.bind_text(1, name);
  return statement.step();
}

bool has_index(Database& database, const std::string& name) {
  auto statement = database.prepare(
      "SELECT 1 FROM sqlite_master WHERE type='index' AND name=? LIMIT 1");
  statement.bind_text(1, name);
  return statement.step();
}

int schema_version(Database& database) {
  return static_cast<int>(
      scalar(database, "SELECT COALESCE(MAX(version),0) FROM schema_version"));
}

bool contains_text(const std::vector<std::string>& values, std::string_view needle) {
  return std::any_of(values.begin(), values.end(), [&](const std::string& value) {
    return value.find(needle) != std::string::npos;
  });
}

template <typename Callable>
auto without_mutation(qbrain::Brain& selected, qbrain::Brain& decoy, Callable&& callable) {
  const auto selected_before = logical_snapshot(selected);
  const auto decoy_before = logical_snapshot(decoy);
  auto result = callable();
  QB_CHECK(logical_snapshot(selected) == selected_before);
  QB_CHECK(logical_snapshot(decoy) == decoy_before);
  return result;
}

json require_operation_error(const qbrain::ops::OpResult& result,
                             const std::string& code,
                             const std::string& field) {
  QB_CHECK(!result.ok);
  QB_CHECK(result.exit_code != 0);
  QB_CHECK(result.text.size() <= 1024);
  QB_CHECK(result.json.size() <= 2048);
  const auto payload = json::parse(result.json);
  QB_CHECK(payload.is_object() && payload.size() == 1);
  QB_CHECK(payload.contains("error") && payload["error"].is_object());
  const auto& error = payload["error"];
  QB_CHECK(error["code"] == code);
  QB_CHECK(error["field"] == field);
  QB_CHECK(error["message"].is_string());
  QB_CHECK(error["message"].get<std::string>().size() <= 512);
  return error;
}

json require_mcp_error(const json& response, const std::string& code,
                       const std::optional<std::string>& field = std::nullopt) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(response["result"].value("isError", false));
  QB_CHECK(response.dump().size() <= 4096);
  for (const auto& block : response["result"]["content"]) {
    if (!block.is_object() || block.value("type", "") != "text" ||
        !block.contains("text") || !block["text"].is_string()) {
      continue;
    }
    const auto parsed = json::parse(block["text"].get<std::string>(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("error")) continue;
    const auto error = parsed["error"];
    QB_CHECK(error["code"] == code);
    if (field) QB_CHECK(error["field"] == *field);
    QB_CHECK(error["message"].is_string());
    QB_CHECK(error["message"].get<std::string>().size() <= 512);
    return error;
  }
  throw std::runtime_error("MCP response did not contain a structured operation error");
}

json require_mcp_success_json(const json& response) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(!response["result"].value("isError", true));
  const auto& content = response["result"]["content"];
  for (auto it = content.rbegin(); it != content.rend(); ++it) {
    if (!it->is_object() || it->value("type", "") != "text" ||
        !it->contains("text") || !(*it)["text"].is_string()) {
      continue;
    }
    const auto parsed = json::parse((*it)["text"].get<std::string>(), nullptr, false);
    if (!parsed.is_discarded()) return parsed;
  }
  throw std::runtime_error("MCP response did not contain JSON success content");
}

json rpc_call(qbrain::Brain& brain, const qbrain::mcp::ServeOptions& options,
              const std::string& operation, json arguments) {
  static int64_t request_id = 17000;
  json request = {
      {"jsonrpc", "2.0"},
      {"id", request_id++},
      {"method", "tools/call"},
      {"params", {{"name", operation}, {"arguments", std::move(arguments)}}}};
  return json::parse(qbrain::mcp::handle_rpc_body(brain, options, request.dump()));
}

class DatabaseAccessObserver {
 public:
  explicit DatabaseAccessObserver(qbrain::Brain& brain) : database_(brain.db().handle()) {
    QB_CHECK(sqlite3_set_authorizer(database_, &DatabaseAccessObserver::authorize, this) ==
             SQLITE_OK);
  }

  ~DatabaseAccessObserver() { sqlite3_set_authorizer(database_, nullptr, nullptr); }

  DatabaseAccessObserver(const DatabaseAccessObserver&) = delete;
  DatabaseAccessObserver& operator=(const DatabaseAccessObserver&) = delete;

  int reads() const { return reads_; }
  int writes() const { return writes_; }

 private:
  static int authorize(void* context, int action, const char* argument1, const char*,
                       const char*, const char*) {
    auto* self = static_cast<DatabaseAccessObserver*>(context);
    const std::string_view object = argument1 ? std::string_view(argument1) : std::string_view();
    if (action == SQLITE_READ && !object.starts_with("sqlite_")) ++self->reads_;
    if ((action == SQLITE_INSERT || action == SQLITE_UPDATE || action == SQLITE_DELETE) &&
        !object.starts_with("sqlite_")) {
      ++self->writes_;
    }
    return SQLITE_OK;
  }

  sqlite3* database_ = nullptr;
  int reads_ = 0;
  int writes_ = 0;
};

class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(std::string name, const std::string& value)
      : name_(std::move(name)) {
    char* previous = nullptr;
    size_t previous_size = 0;
    if (_dupenv_s(&previous, &previous_size, name_.c_str()) == 0 && previous) {
      previous_ = previous;
      std::free(previous);
    }
    if (_putenv_s(name_.c_str(), value.c_str()) != 0) {
      throw std::runtime_error("failed to set test environment variable");
    }
  }

  ~ScopedEnvironmentVariable() {
    _putenv_s(name_.c_str(), previous_ ? previous_->c_str() : "");
  }

  ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
  ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

 private:
  std::string name_;
  std::optional<std::string> previous_;
};

int64_t insert_job(qbrain::Brain& brain, const std::string& status,
                   const std::string& marker,
                   const std::string& payload = R"({"seed":true})") {
  auto statement = brain.db().prepare(
      "INSERT INTO jobs(queue,type,status,payload_json,result_json,priority,attempts,"
      "created_at,updated_at,lock_until,lock_token,error_text) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
  statement.bind_text(1, "queue-" + marker);
  statement.bind_text(2, "type-" + marker);
  statement.bind_text(3, status);
  statement.bind_text(4, payload);
  statement.bind_text(5, "result-" + marker);
  statement.bind_int(6, 701);
  statement.bind_int(7, 9);
  statement.bind_text(8, "2001-02-03 04:05:06");
  statement.bind_text(9, "2002-03-04 05:06:07");
  statement.bind_text(10, "2099-12-31 23:59:59");
  statement.bind_text(11, "lock-" + marker);
  statement.bind_text(12, "error-" + marker);
  statement.step_done();
  return brain.db().last_insert_rowid();
}

std::string job_row(qbrain::Brain& brain, int64_t id) {
  return rowset_snapshot(brain.db(),
                         "SELECT * FROM jobs WHERE id=" + std::to_string(id));
}

std::string message_rows_except(qbrain::Brain& brain, int64_t excluded_id) {
  return rowset_snapshot(brain.db(),
                         "SELECT * FROM job_messages WHERE id<>" +
                             std::to_string(excluded_id));
}

int64_t require_replay_delta(qbrain::Brain& selected, qbrain::Brain& decoy,
                             int64_t source_id,
                             std::unordered_map<std::string, std::string> args) {
  const auto source_before = job_row(selected, source_id);
  const auto jobs_before = rowset_snapshot(selected.db(), "SELECT * FROM jobs");
  const auto schema_before = schema_snapshot(selected.db());
  const auto other_before = rows_except(selected.db(), {"jobs", "sqlite_sequence"});
  const auto decoy_before = logical_snapshot(decoy);
  const auto jobs_sequence_before = sequence_value(selected.db(), "jobs");
  const auto messages_sequence_before = sequence_value(selected.db(), "job_messages");
  const auto job_count_before = scalar(selected.db(), "SELECT COUNT(*) FROM jobs");

  const auto result = qbrain::test_support::call_op(selected, "replay_job", std::move(args));
  QB_CHECK(result.ok && result.exit_code == 0);
  const auto payload = json::parse(result.json);
  QB_CHECK(payload.is_object() && payload.size() == 3);
  QB_CHECK(payload["original_id"] == source_id);
  QB_CHECK(payload["status"] == "waiting");
  const int64_t new_id = payload["new_id"].get<int64_t>();
  QB_CHECK(new_id > 0 && new_id != source_id);

  QB_CHECK(schema_snapshot(selected.db()) == schema_before);
  QB_CHECK(rows_except(selected.db(), {"jobs", "sqlite_sequence"}) == other_before);
  QB_CHECK(logical_snapshot(decoy) == decoy_before);
  QB_CHECK(scalar(selected.db(), "SELECT COUNT(*) FROM jobs") == job_count_before + 1);
  QB_CHECK(sequence_value(selected.db(), "jobs") == jobs_sequence_before + 1);
  QB_CHECK(sequence_value(selected.db(), "job_messages") == messages_sequence_before);
  QB_CHECK(job_row(selected, source_id) == source_before);
  QB_CHECK(rowset_snapshot(selected.db(),
                           "SELECT * FROM jobs WHERE id<>" + std::to_string(new_id)) ==
           jobs_before);

  auto statement = selected.db().prepare(
      "SELECT queue,type,status,payload_json,result_json,priority,attempts,created_at,"
      "updated_at,lock_until,lock_token,error_text FROM jobs WHERE id=?");
  statement.bind_int(1, new_id);
  QB_CHECK(statement.step());
  auto source = selected.db().prepare(
      "SELECT queue,type,payload_json,priority,created_at,updated_at FROM jobs WHERE id=?");
  source.bind_int(1, source_id);
  QB_CHECK(source.step());
  QB_CHECK(statement.column_text(0) == source.column_text(0));
  QB_CHECK(statement.column_text(1) == source.column_text(1));
  QB_CHECK(statement.column_text(2) == "waiting");
  QB_CHECK(statement.column_text(3) == source.column_text(2));
  QB_CHECK(statement.column_is_null(4));
  QB_CHECK(statement.column_int(5) == source.column_int(3));
  QB_CHECK(statement.column_int(6) == 0);
  QB_CHECK(!statement.column_text(7).empty());
  QB_CHECK(!statement.column_text(8).empty());
  QB_CHECK(statement.column_text(7) != source.column_text(4));
  QB_CHECK(statement.column_text(8) != source.column_text(5));
  QB_CHECK(statement.column_is_null(9));
  QB_CHECK(statement.column_is_null(10));
  QB_CHECK(statement.column_is_null(11));
  QB_CHECK(scalar(selected.db(), "SELECT COUNT(*) FROM job_messages WHERE job_id=" +
                                      std::to_string(new_id)) == 0);
  return new_id;
}

int64_t require_message_delta(qbrain::Brain& selected, qbrain::Brain& decoy,
                              int64_t job_id,
                              std::unordered_map<std::string, std::string> args,
                              const std::string& expected_sender,
                              const std::string& expected_payload) {
  const auto messages_before = rowset_snapshot(selected.db(), "SELECT * FROM job_messages");
  const auto schema_before = schema_snapshot(selected.db());
  const auto other_before = rows_except(selected.db(), {"job_messages", "sqlite_sequence"});
  const auto decoy_before = logical_snapshot(decoy);
  const auto job_sequence_before = sequence_value(selected.db(), "jobs");
  const auto message_sequence_before = sequence_value(selected.db(), "job_messages");
  const auto message_count_before =
      scalar(selected.db(), "SELECT COUNT(*) FROM job_messages");

  const auto result =
      qbrain::test_support::call_op(selected, "send_job_message", std::move(args));
  QB_CHECK(result.ok && result.exit_code == 0);
  const auto payload = json::parse(result.json);
  QB_CHECK(payload.is_object() && payload.size() == 2);
  QB_CHECK(payload["job_id"] == job_id);
  const int64_t message_id = payload["message_id"].get<int64_t>();
  QB_CHECK(message_id > 0);

  QB_CHECK(schema_snapshot(selected.db()) == schema_before);
  QB_CHECK(rows_except(selected.db(), {"job_messages", "sqlite_sequence"}) == other_before);
  QB_CHECK(logical_snapshot(decoy) == decoy_before);
  QB_CHECK(scalar(selected.db(), "SELECT COUNT(*) FROM job_messages") ==
           message_count_before + 1);
  QB_CHECK(sequence_value(selected.db(), "jobs") == job_sequence_before);
  QB_CHECK(sequence_value(selected.db(), "job_messages") == message_sequence_before + 1);
  QB_CHECK(message_rows_except(selected, message_id) == messages_before);

  auto statement = selected.db().prepare(
      "SELECT job_id,sender,payload_json,created_at FROM job_messages WHERE id=?");
  statement.bind_int(1, message_id);
  QB_CHECK(statement.step());
  QB_CHECK(statement.column_int(0) == job_id);
  QB_CHECK(statement.column_text(1) == expected_sender);
  QB_CHECK(statement.column_text(2) == expected_payload);
  QB_CHECK(!statement.column_text(3).empty());
  return message_id;
}

void exercise_strict_ids_and_replay(qbrain::Brain& selected, qbrain::Brain& decoy) {
  const std::vector<std::string> invalid_ids = {
      "",          "0",      "-1",      "+1",      " 1",      "1 ",
      "1.0",       "1junk",  std::string("1\0", 2), std::string("1\x1f", 2),
      "9223372036854775808", "18446744073709551616"};
  for (const auto& invalid : invalid_ids) {
    for (const auto* operation : {"replay_job", "send_job_message", "list_job_messages"}) {
      auto result = without_mutation(selected, decoy, [&] {
        return qbrain::test_support::call_op(selected, operation, {{"job_id", invalid}});
      });
      require_operation_error(result, "invalid_argument", "job_id");
      ++g_evidence.strict_id_cases;

      auto legacy = without_mutation(selected, decoy, [&] {
        return qbrain::test_support::call_op(selected, operation, {{"id", invalid}});
      });
      require_operation_error(legacy, "invalid_argument", "job_id");
      ++g_evidence.strict_id_cases;
    }
  }

  // Exercise all unknown-id paths before jobs has ever advanced its sequence.
  for (const auto* operation : {"replay_job", "send_job_message", "list_job_messages"}) {
    auto unknown = without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(
          selected, operation, {{"job_id", "9223372036854775807"}});
    });
    require_operation_error(unknown, "not_found", "job_id");
    ++g_evidence.strict_id_cases;
  }

  const auto ordinary_id = insert_job(selected, "waiting", "strict-id");
  for (const auto* operation : {"replay_job", "send_job_message", "list_job_messages"}) {
    auto missing = without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(selected, operation);
    });
    require_operation_error(missing, "invalid_argument", "job_id");
    auto conflict = without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(
          selected, operation,
          {{"job_id", std::to_string(ordinary_id)}, {"id", std::to_string(ordinary_id + 1)}});
    });
    require_operation_error(conflict, "invalid_argument", "job_id");
    auto unexpected = without_mutation(selected, decoy, [&] {
      DatabaseAccessObserver observer(selected);
      auto value = qbrain::test_support::call_op(
          selected, operation,
          {{"job_id", std::to_string(ordinary_id)}, {"unexpected", "N17_SECRET"}});
      QB_CHECK(observer.reads() == 0 && observer.writes() == 0);
      return value;
    });
    require_operation_error(unexpected, "invalid_argument", "unexpected");
    g_evidence.strict_id_cases += 3;
  }

  auto equal_alias_read = without_mutation(selected, decoy, [&] {
    return qbrain::test_support::call_op(
        selected, "list_job_messages",
        {{"job_id", std::to_string(ordinary_id)}, {"id", std::to_string(ordinary_id)}});
  });
  QB_CHECK(equal_alias_read.ok && json::parse(equal_alias_read.json).empty());

  std::string opaque_payload = " { \"opaque\" : [3,2,1] }";
  opaque_payload.push_back('\0');
  opaque_payload += "audit";
  const auto failed_id = insert_job(selected, "failed", "failed", opaque_payload);
  auto seeded_message = qbrain::jobs::send_job_message_checked(
      selected, failed_id, "seed", R"({"kept":"original-only"})");
  QB_CHECK(seeded_message.status == qbrain::jobs::JobOperationStatus::success);
  (void)require_replay_delta(selected, decoy, failed_id,
                             {{"job_id", std::to_string(failed_id)}});
  ++g_evidence.replay_state_cases;

  const auto completed_id = insert_job(selected, "completed", "completed");
  (void)require_replay_delta(selected, decoy, completed_id,
                             {{"id", std::to_string(completed_id)}});
  ++g_evidence.replay_state_cases;

  const auto equal_id = insert_job(selected, "failed", "equal-alias");
  (void)require_replay_delta(
      selected, decoy, equal_id,
      {{"job_id", std::to_string(equal_id)}, {"id", std::to_string(equal_id)}});
  ++g_evidence.replay_state_cases;

  for (const auto& status : {"waiting", "active", "paused", "cancelled", "dead",
                             "operator_custom"}) {
    const auto id = insert_job(selected, status, std::string("state-") + status);
    const auto before = job_row(selected, id);
    auto result = without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(
          selected, "replay_job", {{"job_id", std::to_string(id)}});
    });
    require_operation_error(result, "invalid_state", "job_id");
    QB_CHECK(job_row(selected, id) == before);
    ++g_evidence.replay_state_cases;
  }
}

void exercise_message_boundaries(qbrain::Brain& selected, qbrain::Brain& decoy) {
  const auto job_id = insert_job(selected, "waiting", "message-boundaries");
  (void)require_message_delta(selected, decoy, job_id,
                              {{"job_id", std::to_string(job_id)}}, "system", "{}");
  ++g_evidence.sender_payload_cases;

  (void)require_message_delta(
      selected, decoy, job_id,
      {{"id", std::to_string(job_id)}, {"sender", "x"}, {"payload_json", " { \"b\" : 2, \"a\" : [1, true] } "}},
      "x", R"({"a":[1,true],"b":2})");
  ++g_evidence.sender_payload_cases;

  const std::string sender_128(128, 's');
  (void)require_message_delta(
      selected, decoy, job_id,
      {{"job_id", std::to_string(job_id)}, {"sender", sender_128}, {"payload_json", "[]"}},
      sender_128, "[]");
  ++g_evidence.sender_payload_cases;

  std::string multibyte_128;
  for (int index = 0; index < 64; ++index) multibyte_128 += "\xC3\xA9";
  QB_CHECK(multibyte_128.size() == 128);
  (void)require_message_delta(
      selected, decoy, job_id,
      {{"job_id", std::to_string(job_id)}, {"sender", multibyte_128}, {"payload_json", "true"}},
      multibyte_128, "true");
  ++g_evidence.sender_payload_cases;

  std::string exact_payload(65534, 'p');
  exact_payload.insert(exact_payload.begin(), '"');
  exact_payload.push_back('"');
  QB_CHECK(exact_payload.size() == qbrain::jobs::kJobMessagePayloadMaxBytes);
  (void)require_message_delta(
      selected, decoy, job_id,
      {{"job_id", std::to_string(job_id)}, {"sender", "boundary"}, {"payload_json", exact_payload}},
      "boundary", exact_payload);
  ++g_evidence.sender_payload_cases;

  std::string boundary_plus_one(65535, 'q');
  boundary_plus_one.insert(boundary_plus_one.begin(), '"');
  boundary_plus_one.push_back('"');
  QB_CHECK(boundary_plus_one.size() == qbrain::jobs::kJobMessagePayloadMaxBytes + 1);

  std::string canonical_expansion = "[";
  for (int index = 0; index < 12000; ++index) {
    if (index) canonical_expansion.push_back(',');
    canonical_expansion += "1e20";
  }
  canonical_expansion.push_back(']');
  QB_CHECK(canonical_expansion.size() <= qbrain::jobs::kJobMessagePayloadMaxBytes);
  QB_CHECK(json::parse(canonical_expansion).dump().size() >
           qbrain::jobs::kJobMessagePayloadMaxBytes);

  std::string malformed_utf8 = "bad-";
  malformed_utf8.push_back(static_cast<char>(0xc3));
  malformed_utf8.push_back('(');
  std::string sender_nul("a\0b", 3);
  std::string sender_control = "a";
  sender_control.push_back(static_cast<char>(0x1f));
  std::string sender_del = "a";
  sender_del.push_back(static_cast<char>(0x7f));
  std::string payload_nul = "{\"x\":\"a";
  payload_nul.push_back('\0');
  payload_nul += "b\"}";
  std::string payload_bad_utf8 = "{\"x\":\"";
  payload_bad_utf8.push_back(static_cast<char>(0xff));
  payload_bad_utf8 += "\"}";

  const std::vector<std::pair<std::string, std::string>> invalid_senders = {
      {"", "{}"}, {std::string(129, 's'), "{}"}, {multibyte_128 + "\xC3\xA9", "{}"},
      {malformed_utf8, "{}"}, {sender_nul, "{}"}, {sender_control, "{}"},
      {sender_del, "{}"}};
  for (const auto& [sender, payload] : invalid_senders) {
    auto result = without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(
          selected, "send_job_message",
          {{"job_id", std::to_string(job_id)}, {"sender", sender}, {"payload_json", payload}});
    });
    require_operation_error(result, "invalid_argument", "sender");
    QB_CHECK(result.text.find(sender) == std::string::npos || sender.empty());
    ++g_evidence.sender_payload_cases;
  }

  const std::vector<std::string> invalid_payloads = {
      "", "{", boundary_plus_one, canonical_expansion, payload_nul, payload_bad_utf8};
  for (const auto& invalid : invalid_payloads) {
    auto result = without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(
          selected, "send_job_message",
          {{"job_id", std::to_string(job_id)}, {"sender", "safe-sender"},
           {"payload_json", invalid}});
    });
    require_operation_error(result, "invalid_argument", "payload_json");
    QB_CHECK(result.text.find("safe-sender") == std::string::npos);
    ++g_evidence.sender_payload_cases;
  }

  auto missing = without_mutation(selected, decoy, [&] {
    return qbrain::test_support::call_op(
        selected, "send_job_message",
        {{"job_id", "9223372036854775807"}, {"sender", "safe"}, {"payload_json", "{}"}});
  });
  require_operation_error(missing, "not_found", "job_id");

  for (const auto& status : {"waiting", "active", "paused", "failed", "completed",
                             "cancelled", "dead", "custom"}) {
    const auto status_id = insert_job(selected, status, std::string("message-") + status);
    (void)require_message_delta(
        selected, decoy, status_id,
        {{"job_id", std::to_string(status_id)}, {"sender", "status"},
         {"payload_json", json({{"status", status}}).dump()}},
        "status", json({{"status", status}}).dump());
    ++g_evidence.sender_payload_cases;
  }
}

void seed_list_messages(qbrain::Brain& brain, int64_t job_id, int count) {
  brain.db().exec("BEGIN;");
  try {
    auto statement = brain.db().prepare(
        "INSERT INTO job_messages(job_id,sender,payload_json,created_at) "
        "VALUES(?,?,?,'2025-01-02 03:04:05')");
    for (int index = 0; index < count; ++index) {
      statement.reset();
      statement.clear_bindings();
      statement.bind_int(1, job_id);
      statement.bind_text(2, "sender-" + std::to_string(index));
      statement.bind_text(3, json({{"index", index}}).dump());
      statement.step_done();
    }
    brain.db().exec("COMMIT;");
  } catch (...) {
    try {
      brain.db().exec("ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

void require_list_shape(const json& rows, int64_t job_id, size_t expected_size) {
  QB_CHECK(rows.is_array() && rows.size() == expected_size);
  int64_t previous_id = std::numeric_limits<int64_t>::max();
  const std::set<std::string> expected_keys = {
      "id", "job_id", "sender", "payload_json", "created_at"};
  for (const auto& row : rows) {
    QB_CHECK(row.is_object() && row.size() == expected_keys.size());
    std::set<std::string> actual_keys;
    for (auto it = row.begin(); it != row.end(); ++it) actual_keys.insert(it.key());
    QB_CHECK(actual_keys == expected_keys);
    QB_CHECK(row["job_id"] == job_id);
    const int64_t id = row["id"].get<int64_t>();
    QB_CHECK(id < previous_id);
    previous_id = id;
    QB_CHECK(row["sender"].is_string() && !row["sender"].get<std::string>().empty());
    QB_CHECK(row["created_at"].is_string() && !row["created_at"].get<std::string>().empty());
    const auto stored_payload =
        json::parse(row["payload_json"].get<std::string>(), nullptr, false);
    QB_CHECK(!stored_payload.is_discarded());
  }
}

void exercise_message_lists(qbrain::Brain& selected, qbrain::Brain& decoy) {
  const auto empty_id = insert_job(selected, "waiting", "empty-inbox");
  auto empty = without_mutation(selected, decoy, [&] {
    return qbrain::test_support::call_op(
        selected, "list_job_messages", {{"job_id", std::to_string(empty_id)}});
  });
  QB_CHECK(empty.ok && empty.exit_code == 0 && json::parse(empty.json).empty());

  auto missing = without_mutation(selected, decoy, [&] {
    return qbrain::test_support::call_op(
        selected, "list_job_messages", {{"job_id", "9223372036854775807"}});
  });
  require_operation_error(missing, "not_found", "job_id");

  const auto list_id = insert_job(selected, "completed", "list-matrix");
  seed_list_messages(selected, list_id, 205);
  const std::vector<std::tuple<std::optional<std::string>, size_t>> limits = {
      {std::nullopt, 50}, {"0", 1}, {"1", 1}, {"50", 50},
      {"200", 200},      {"201", 200}, {"999999", 200}};
  std::string default_output;
  for (const auto& [limit, expected] : limits) {
    std::unordered_map<std::string, std::string> args = {
        {"job_id", std::to_string(list_id)}};
    if (limit) args["limit"] = *limit;
    auto result = without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(selected, "list_job_messages", args);
    });
    QB_CHECK(result.ok);
    require_list_shape(json::parse(result.json), list_id, expected);
    if (!limit) default_output = result.json;
    ++g_evidence.list_limit_cases;
  }
  auto repeat = without_mutation(selected, decoy, [&] {
    return qbrain::test_support::call_op(
        selected, "list_job_messages", {{"job_id", std::to_string(list_id)}});
  });
  QB_CHECK(repeat.ok && repeat.json == default_output && repeat.text == default_output);

  for (const auto& invalid : {"", "-1", "+1", " 1", "1 ", "1.0", "1junk",
                              "18446744073709551616"}) {
    auto result = without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(
          selected, "list_job_messages",
          {{"job_id", std::to_string(list_id)}, {"limit", invalid}});
    });
    require_operation_error(result, "invalid_argument", "limit");
    ++g_evidence.list_limit_cases;
  }

  const auto checked_zero = without_mutation(selected, decoy, [&] {
    return qbrain::jobs::list_job_messages_checked(selected, list_id, 0);
  });
  QB_CHECK(checked_zero.status == qbrain::jobs::JobOperationStatus::success &&
           checked_zero.messages.size() == 1);
  const auto checked_high = without_mutation(selected, decoy, [&] {
    return qbrain::jobs::list_job_messages_checked(selected, list_id, 999);
  });
  QB_CHECK(checked_high.status == qbrain::jobs::JobOperationStatus::success &&
           checked_high.messages.size() == 200);

  const auto selected_text = default_output;
  QB_CHECK(selected_text.find("DECOY_N17_MESSAGE_SENTINEL") == std::string::npos);
}

void require_job_messages_shape(Database& database) {
  struct Column {
    std::string name;
    std::string type;
    int64_t not_null = 0;
    std::string default_value;
    int64_t primary_key = 0;
  };
  std::vector<Column> columns;
  auto table = database.prepare("PRAGMA table_info(job_messages)");
  while (table.step()) {
    columns.push_back({table.column_text(1), table.column_text(2), table.column_int(3),
                       table.column_text(4), table.column_int(5)});
  }
  QB_CHECK(columns.size() == 5);
  QB_CHECK(columns[0].name == "id" && columns[0].type == "INTEGER" &&
           columns[0].primary_key == 1);
  QB_CHECK(columns[1].name == "job_id" && columns[1].type == "INTEGER" &&
           columns[1].not_null == 1 && columns[1].default_value.empty());
  QB_CHECK(columns[2].name == "sender" && columns[2].type == "TEXT" &&
           columns[2].not_null == 1 && columns[2].default_value == "'system'");
  QB_CHECK(columns[3].name == "payload_json" && columns[3].type == "TEXT" &&
           columns[3].not_null == 1 && columns[3].default_value == "'{}'");
  QB_CHECK(columns[4].name == "created_at" && columns[4].type == "TEXT" &&
           columns[4].not_null == 1 &&
           columns[4].default_value.find("datetime('now')") != std::string::npos);

  auto master = database.prepare(
      "SELECT sql FROM sqlite_master WHERE type='table' AND name='job_messages'");
  QB_CHECK(master.step());
  const auto table_sql = master.column_text(0);
  QB_CHECK(table_sql.find("AUTOINCREMENT") != std::string::npos);

  std::vector<std::pair<std::string, int64_t>> index_columns;
  auto index = database.prepare("PRAGMA index_xinfo(idx_job_messages_job)");
  while (index.step()) {
    if (index.column_int(5) == 1 && !index.column_is_null(2)) {
      index_columns.emplace_back(index.column_text(2), index.column_int(3));
    }
  }
  QB_CHECK(index_columns.size() == 2);
  QB_CHECK(index_columns[0] == std::make_pair(std::string("job_id"), int64_t{0}));
  QB_CHECK(index_columns[1] == std::make_pair(std::string("id"), int64_t{0}));
  auto foreign_keys = database.prepare("PRAGMA foreign_key_list(job_messages)");
  QB_CHECK(!foreign_keys.step());
}

void create_v7_fixture(const std::filesystem::path& path) {
  qbrain::Brain brain("n17-v7-fixture");
  brain.open_at(qbrain::util::path_to_utf8(path));
  (void)insert_job(brain, "failed", "legacy-v7", R"({ "legacy" : true })");
  brain.db().exec(
      "INSERT OR REPLACE INTO config(key,value) VALUES('n17.fixture','preserved-config');"
      "INSERT INTO ingest_log(source_id,event_type,path,detail_json,created_at) "
      "VALUES('default','legacy','v7-path','{\"legacy\":7}','2024-05-06 07:08:09');");
  brain.db().exec("PRAGMA wal_checkpoint(TRUNCATE);");
  brain.close();

  Database database;
  database.open(qbrain::util::path_to_utf8(path));
  database.exec(R"SQL(
BEGIN IMMEDIATE;
DROP INDEX IF EXISTS idx_ingest_log_source_created;
ALTER TABLE ingest_log RENAME TO ingest_log_v12_old;
CREATE TABLE ingest_log (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  event_type TEXT NOT NULL DEFAULT 'import',
  path TEXT NOT NULL DEFAULT '',
  detail_json TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
INSERT INTO ingest_log(id,event_type,path,detail_json,created_at)
SELECT id,event_type,path,detail_json,created_at FROM ingest_log_v12_old;
DROP TABLE ingest_log_v12_old;
CREATE INDEX idx_ingest_log_created ON ingest_log(created_at DESC);
DROP TABLE IF EXISTS job_messages;
DROP TABLE IF EXISTS takes;
DROP TABLE IF EXISTS file_index;
DROP TABLE IF EXISTS raw_data;
DELETE FROM schema_version WHERE version > 7;
COMMIT;
)SQL");
  database.exec("PRAGMA wal_checkpoint(TRUNCATE);");
}

std::string v7_payload_snapshot(Database& database) {
  std::string snapshot;
  snapshot += rowset_snapshot(database, "SELECT * FROM jobs ORDER BY id");
  snapshot += rowset_snapshot(
      database, "SELECT key,value FROM config WHERE key='n17.fixture'");
  snapshot += rowset_snapshot(
      database,
      "SELECT id,event_type,path,detail_json,created_at FROM ingest_log ORDER BY id");
  return snapshot;
}

void require_integrity_damage(qbrain::Brain& brain,
                              const std::vector<std::string>& expected_terms) {
  const auto before = logical_snapshot(brain);
  const auto integrity = qbrain::storage::check_schema_integrity(brain.db());
  QB_CHECK(!integrity.ok);
  std::string reasons;
  for (const auto& reason : integrity.missing) {
    QB_CHECK(reason.size() <= 512);
    reasons += reason + "\n";
  }
  for (const auto& term : expected_terms) QB_CHECK(reasons.find(term) != std::string::npos);
  QB_CHECK(logical_snapshot(brain) == before);
}

void exercise_migrations(const std::filesystem::path& root) {
  const auto fresh_path = root / "migration-fresh.db";
  qbrain::Brain fresh("n17-migration-fresh");
  fresh.open_at(qbrain::util::path_to_utf8(fresh_path));
  QB_CHECK(schema_version(fresh.db()) == 12);
  QB_CHECK(qbrain::storage::check_schema_integrity(fresh.db()).ok);
  require_job_messages_shape(fresh.db());
  const auto fresh_before = logical_snapshot(fresh);
  qbrain::storage::apply_migrations(fresh.db());
  QB_CHECK(logical_snapshot(fresh) == fresh_before);
  fresh.close();

  const auto checkpoint_path = root / "migration-v8-checkpoint.db";
  create_v7_fixture(checkpoint_path);
  {
    Database database;
    database.open(qbrain::util::path_to_utf8(checkpoint_path));
    QB_CHECK(schema_version(database) == 7);
    const auto payload_before = v7_payload_snapshot(database);
    database.exec(R"SQL(
CREATE TRIGGER n17_reject_v9 BEFORE INSERT ON schema_version
WHEN NEW.version=9 BEGIN SELECT RAISE(ABORT, 'injected v9 marker failure'); END;
)SQL");
    bool failed = false;
    try {
      qbrain::storage::apply_migrations(database);
    } catch (...) {
      failed = true;
    }
    QB_CHECK(failed);
    QB_CHECK(schema_version(database) == 8);
    QB_CHECK(has_table(database, "job_messages"));
    QB_CHECK(has_index(database, "idx_job_messages_job"));
    QB_CHECK(!has_table(database, "takes"));
    require_job_messages_shape(database);
    QB_CHECK(v7_payload_snapshot(database) == payload_before);
    database.exec("DROP TRIGGER n17_reject_v9;");
    qbrain::storage::apply_migrations(database);
    QB_CHECK(schema_version(database) == 12);
    QB_CHECK(v7_payload_snapshot(database) == payload_before);
    QB_CHECK(qbrain::storage::check_schema_integrity(database).ok);
    const auto migrated = database_snapshot(database);
    g_evidence.migration_hash = snapshot_sha256(migrated);
    qbrain::storage::apply_migrations(database);
    QB_CHECK(database_snapshot(database) == migrated);
  }

  const auto rollback_path = root / "migration-v8-rollback.db";
  create_v7_fixture(rollback_path);
  {
    Database database;
    database.open(qbrain::util::path_to_utf8(rollback_path));
    database.exec(R"SQL(
CREATE TRIGGER n17_reject_v8 BEFORE INSERT ON schema_version
WHEN NEW.version=8 BEGIN SELECT RAISE(ABORT, 'injected v8 marker failure'); END;
)SQL");
    const auto before = database_snapshot(database);
    bool failed = false;
    try {
      qbrain::storage::apply_migrations(database);
    } catch (...) {
      failed = true;
    }
    QB_CHECK(failed);
    QB_CHECK(schema_version(database) == 7);
    QB_CHECK(!has_table(database, "job_messages"));
    QB_CHECK(!has_index(database, "idx_job_messages_job"));
    QB_CHECK(database_snapshot(database) == before);
    g_evidence.rollback_hash = snapshot_sha256(before);
    database.exec("DROP TRIGGER n17_reject_v8;");
    qbrain::storage::apply_migrations(database);
    QB_CHECK(schema_version(database) == 12);
    require_job_messages_shape(database);
    QB_CHECK(qbrain::storage::check_schema_integrity(database).ok);
  }

  qbrain::Brain missing_table("n17-damaged-table");
  missing_table.open_at(qbrain::util::path_to_utf8(root / "damaged-table.db"));
  missing_table.db().exec("DROP TABLE job_messages;");
  require_integrity_damage(missing_table, {"job_messages"});
  missing_table.close();

  qbrain::Brain missing_index("n17-damaged-index");
  missing_index.open_at(qbrain::util::path_to_utf8(root / "damaged-index.db"));
  missing_index.db().exec("DROP INDEX idx_job_messages_job;");
  require_integrity_damage(missing_index, {"idx_job_messages_job"});
  missing_index.close();

  qbrain::Brain missing_column("n17-damaged-column");
  missing_column.open_at(qbrain::util::path_to_utf8(root / "damaged-column.db"));
  missing_column.db().exec("ALTER TABLE job_messages DROP COLUMN sender;");
  require_integrity_damage(missing_column, {"job_messages", "sender"});
  missing_column.close();

  qbrain::Brain wrong_shape("n17-damaged-shape");
  wrong_shape.open_at(qbrain::util::path_to_utf8(root / "damaged-shape.db"));
  wrong_shape.db().exec(R"SQL(
DROP INDEX idx_job_messages_job;
ALTER TABLE job_messages RENAME TO job_messages_old;
CREATE TABLE job_messages (
  id INTEGER PRIMARY KEY,
  job_id INTEGER,
  sender INTEGER DEFAULT 7,
  payload_json BLOB,
  created_at INTEGER
);
DROP TABLE job_messages_old;
CREATE INDEX idx_job_messages_job ON job_messages(job_id, id);
)SQL");
  require_integrity_damage(wrong_shape, {"job_messages", "sender"});
  wrong_shape.close();
}

const json* find_tool(const json& tools, const std::string& name) {
  for (const auto& tool : tools) {
    if (tool.value("name", "") == name) return &tool;
  }
  return nullptr;
}

void require_alias_schema(const json& schema) {
  QB_CHECK(schema["type"] == "object");
  QB_CHECK(schema.contains("additionalProperties") &&
           schema["additionalProperties"] == false);
  QB_CHECK(schema["properties"]["job_id"]["type"] == "integer");
  QB_CHECK(schema["properties"]["job_id"]["minimum"] == 1);
  QB_CHECK(schema["properties"]["job_id"]["maximum"] ==
           std::numeric_limits<int64_t>::max());
  QB_CHECK(schema["properties"]["id"]["type"] == "integer");
  QB_CHECK(schema["properties"]["id"]["minimum"] == 1);
  QB_CHECK(schema["properties"]["id"]["maximum"] ==
           std::numeric_limits<int64_t>::max());
  QB_CHECK(schema.contains("anyOf") && schema["anyOf"].size() == 2);
  std::set<std::string> required;
  for (const auto& branch : schema["anyOf"]) {
    QB_CHECK(branch["required"].is_array() && branch["required"].size() == 1);
    required.insert(branch["required"][0].get<std::string>());
  }
  QB_CHECK(required == std::set<std::string>({"id", "job_id"}));
}

void exercise_registry_and_mcp(qbrain::Brain& selected, qbrain::Brain& decoy) {
  const auto* replay = qbrain::ops::global_registry().find("replay_job");
  const auto* send = qbrain::ops::global_registry().find("send_job_message");
  const auto* list = qbrain::ops::global_registry().find("list_job_messages");
  QB_CHECK(replay && send && list);
  QB_CHECK(replay->scope == qbrain::ops::Scope::Write && replay->local_only);
  QB_CHECK(send->scope == qbrain::ops::Scope::Write && send->local_only);
  QB_CHECK(list->scope == qbrain::ops::Scope::Read && !list->local_only);

  const auto replay_schema = json::parse(replay->input_schema_json);
  const auto send_schema = json::parse(send->input_schema_json);
  const auto list_schema = json::parse(list->input_schema_json);
  require_alias_schema(replay_schema);
  require_alias_schema(send_schema);
  require_alias_schema(list_schema);
  QB_CHECK(replay_schema["properties"].size() == 2);
  QB_CHECK(send_schema["properties"].size() == 4);
  QB_CHECK(send_schema["properties"]["sender"]["type"] == "string");
  QB_CHECK(send_schema["properties"]["sender"]["minLength"] == 1);
  QB_CHECK(send_schema["properties"]["sender"]["maxLength"] == 128);
  QB_CHECK(send_schema["properties"]["sender"]["default"] == "system");
  QB_CHECK(send_schema["properties"]["payload_json"]["type"] == "string");
  QB_CHECK(send_schema["properties"]["payload_json"]["minLength"] == 1);
  QB_CHECK(send_schema["properties"]["payload_json"]["maxLength"] == 65536);
  QB_CHECK(send_schema["properties"]["payload_json"]["default"] == "{}");
  QB_CHECK(list_schema["properties"].size() == 3);
  QB_CHECK(list_schema["properties"]["limit"]["type"] == "integer");
  QB_CHECK(list_schema["properties"]["limit"]["minimum"] == 0);
  QB_CHECK(list_schema["properties"]["limit"]["maximum"] == 200);
  QB_CHECK(list_schema["properties"]["limit"]["default"] == 50);

  qbrain::mcp::ServeOptions denied_options;
  const auto tools_response = without_mutation(selected, decoy, [&] {
    return json::parse(qbrain::mcp::handle_rpc_body(
        selected, denied_options,
        R"({"jsonrpc":"2.0","id":17001,"method":"tools/list","params":{}})"));
  });
  const auto& tools = tools_response["result"]["tools"];
  for (const auto& [name, schema] :
       std::vector<std::pair<std::string, json>>{{"replay_job", replay_schema},
                                                 {"send_job_message", send_schema},
                                                 {"list_job_messages", list_schema}}) {
    const auto* tool = find_tool(tools, name);
    QB_CHECK(tool && (*tool)["inputSchema"] == schema);
  }

  const auto terminal_id = insert_job(selected, "failed", "mcp-replay");
  const auto inbox_id = insert_job(selected, "waiting", "mcp-send");
  qbrain::mcp::ServeOptions allowed_options;
  allowed_options.allow_write = true;
  const auto selected_before_denial = logical_snapshot(selected);
  const auto decoy_before_denial = logical_snapshot(decoy);
  for (const auto& [operation, arguments] :
       std::vector<std::pair<std::string, json>>{
           {"replay_job", json{{"job_id", terminal_id}}},
           {"send_job_message",
            json{{"job_id", inbox_id}, {"sender", "N17_REMOTE_SENDER_SECRET"},
                 {"payload_json", R"({"secret":"N17_REMOTE_PAYLOAD_SECRET"})"}}}}) {
    json response;
    {
      DatabaseAccessObserver observer(selected);
      response = rpc_call(selected, denied_options, operation, arguments);
      QB_CHECK(observer.reads() == 0 && observer.writes() == 0);
    }
    require_mcp_error(response, "write_denied");
    QB_CHECK(response.dump().find("N17_REMOTE_SENDER_SECRET") == std::string::npos);
    QB_CHECK(response.dump().find("N17_REMOTE_PAYLOAD_SECRET") == std::string::npos);
  }
  QB_CHECK(logical_snapshot(selected) == selected_before_denial);
  QB_CHECK(logical_snapshot(decoy) == decoy_before_denial);

  const std::vector<std::tuple<std::string, json, std::string, bool>> invalid_calls = {
      {"replay_job", json::array(), "arguments", false},
      {"send_job_message", nullptr, "arguments", false},
      {"list_job_messages", true, "arguments", false},
      {"replay_job", json{{"job_id", "1"}}, "job_id", false},
      {"send_job_message", json{{"job_id", "1"}}, "job_id", false},
      {"list_job_messages", json{{"job_id", "1"}}, "job_id", false},
      {"replay_job", json{{"job_id", 1.5}}, "job_id", false},
      {"replay_job", json{{"job_id", -1}}, "job_id", false},
      {"replay_job", json{{"job_id", true}}, "job_id", false},
      {"replay_job", json{{"job_id", json::object()}}, "job_id", false},
      {"replay_job", json{{"job_id", json::array()}}, "job_id", false},
      {"replay_job", json{{"job_id", nullptr}}, "job_id", false},
      {"replay_job", json{{"job_id", std::numeric_limits<uint64_t>::max()}}, "job_id",
       true},
      {"replay_job", json{{"job_id", 0}}, "job_id", true},
      {"replay_job", json{{"unexpected", 1}}, "unexpected", false},
      {"replay_job", json{{"job_id", terminal_id}, {"id", terminal_id + 1}}, "job_id",
       true},
      {"replay_job", json::object(), "job_id", true},
      {"send_job_message", json{{"job_id", inbox_id}, {"sender", 7}}, "sender", false},
      {"send_job_message", json{{"job_id", inbox_id}, {"payload_json", false}},
       "payload_json", false},
      {"send_job_message", json{{"job_id", inbox_id}, {"sender", ""}}, "sender", true},
      {"send_job_message", json{{"job_id", inbox_id}, {"payload_json", "{"}},
       "payload_json", true},
      {"list_job_messages", json{{"job_id", inbox_id}, {"limit", "1"}}, "limit", false},
      {"list_job_messages", json{{"job_id", inbox_id}, {"limit", 1.5}}, "limit", false},
      {"list_job_messages", json{{"job_id", inbox_id}, {"limit", -1}}, "limit", false},
      {"list_job_messages", json{{"job_id", inbox_id}, {"limit", true}}, "limit", false},
      {"list_job_messages", json{{"job_id", inbox_id}, {"limit", nullptr}}, "limit", false},
  };
  for (const auto& [operation, arguments, field, allow_write] : invalid_calls) {
    auto response = without_mutation(selected, decoy, [&] {
      DatabaseAccessObserver observer(selected);
      auto value = rpc_call(selected, allow_write ? allowed_options : denied_options,
                            operation, arguments);
      QB_CHECK(observer.reads() == 0 && observer.writes() == 0);
      return value;
    });
    require_mcp_error(response, "invalid_argument", field);
    ++g_evidence.mcp_rejection_cases;
  }

  const auto decoy_before_allowed = logical_snapshot(decoy);
  const auto unrelated_before =
      rows_except(selected.db(), {"jobs", "job_messages", "sqlite_sequence"});
  {
    ScopedEnvironmentVariable ambient_source("QBRAIN_SOURCE", "ambient_n17_forbidden");
    auto mcp_read = without_mutation(selected, decoy, [&] {
      DatabaseAccessObserver observer(selected);
      auto value = rpc_call(selected, denied_options, "list_job_messages",
                            json{{"job_id", inbox_id}});
      QB_CHECK(observer.reads() > 0 && observer.writes() == 0);
      return value;
    });
    QB_CHECK(require_mcp_success_json(mcp_read).empty());

    auto replay_response = rpc_call(selected, allowed_options, "replay_job",
                                    json{{"job_id", terminal_id}});
    const auto replay_payload = require_mcp_success_json(replay_response);
    QB_CHECK(replay_payload["original_id"] == terminal_id);
    QB_CHECK(replay_payload["new_id"].get<int64_t>() > 0);
    auto send_response = rpc_call(
        selected, allowed_options, "send_job_message",
        json{{"job_id", inbox_id}, {"sender", "allowed"},
             {"payload_json", " { \"allowed\" : true } "}});
    const auto send_payload = require_mcp_success_json(send_response);
    QB_CHECK(send_payload["job_id"] == inbox_id);
  }
  auto stored = qbrain::jobs::list_job_messages_checked(selected, inbox_id, 10);
  QB_CHECK(stored.status == qbrain::jobs::JobOperationStatus::success &&
           stored.messages.size() == 1 && stored.messages[0].sender == "allowed" &&
           stored.messages[0].payload_json == R"({"allowed":true})");
  QB_CHECK(rows_except(selected.db(), {"jobs", "job_messages", "sqlite_sequence"}) ==
           unrelated_before);
  QB_CHECK(logical_snapshot(decoy) == decoy_before_allowed);
}

std::string operation_error_code(const qbrain::ops::OpResult& result) {
  if (result.ok) return {};
  const auto payload = json::parse(result.json, nullptr, false);
  if (payload.is_discarded() || !payload.contains("error")) return "unstructured";
  return payload["error"].value("code", "missing");
}

void race_replays(const std::filesystem::path& path, qbrain::Brain& decoy) {
  qbrain::Brain first("n17-race-replay-a");
  qbrain::Brain second("n17-race-replay-b");
  first.open_at(qbrain::util::path_to_utf8(path));
  second.open_at(qbrain::util::path_to_utf8(path));
  const auto source_id = insert_job(first, "completed", "race-replay");
  const auto source_before = job_row(first, source_id);
  const auto other_before = rows_except(first.db(), {"jobs", "sqlite_sequence"});
  const auto schema_before = schema_snapshot(first.db());
  const auto sequence_before = sequence_value(first.db(), "jobs");
  const auto count_before = scalar(first.db(), "SELECT COUNT(*) FROM jobs");
  const auto decoy_before = logical_snapshot(decoy);

  std::atomic<int> ready{0};
  std::atomic<bool> start{false};
  qbrain::ops::OpResult first_result;
  qbrain::ops::OpResult second_result;
  auto invoke = [&](qbrain::Brain& brain, qbrain::ops::OpResult& result) {
    ready.fetch_add(1, std::memory_order_release);
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    result = qbrain::test_support::call_op(
        brain, "replay_job", {{"job_id", std::to_string(source_id)}});
  };
  std::thread a(invoke, std::ref(first), std::ref(first_result));
  std::thread b(invoke, std::ref(second), std::ref(second_result));
  while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
  start.store(true, std::memory_order_release);
  a.join();
  b.join();

  std::vector<int64_t> new_ids;
  qbrain::Brain* loser = nullptr;
  for (auto pair : {std::make_pair(&first_result, &first),
                    std::make_pair(&second_result, &second)}) {
    if (pair.first->ok) {
      new_ids.push_back(json::parse(pair.first->json)["new_id"].get<int64_t>());
    } else {
      QB_CHECK(operation_error_code(*pair.first) == "database_busy");
      loser = pair.second;
    }
  }
  const int successes = static_cast<int>(new_ids.size());
  const int busy = 2 - successes;
  QB_CHECK(successes == 1 || successes == 2);
  QB_CHECK(busy == 0 || busy == 1);
  QB_CHECK(scalar(first.db(), "SELECT COUNT(*) FROM jobs") == count_before + successes);
  QB_CHECK(sequence_value(first.db(), "jobs") == sequence_before + successes);
  QB_CHECK(job_row(first, source_id) == source_before);
  QB_CHECK(rows_except(first.db(), {"jobs", "sqlite_sequence"}) == other_before);
  QB_CHECK(schema_snapshot(first.db()) == schema_before);
  QB_CHECK(logical_snapshot(decoy) == decoy_before);
  if (new_ids.size() == 2) QB_CHECK(new_ids[0] != new_ids[1]);

  if (loser) {
    auto retry = qbrain::test_support::call_op(
        *loser, "replay_job", {{"job_id", std::to_string(source_id)}});
    QB_CHECK(retry.ok);
    const auto retry_id = json::parse(retry.json)["new_id"].get<int64_t>();
    QB_CHECK(std::find(new_ids.begin(), new_ids.end(), retry_id) == new_ids.end());
    new_ids.push_back(retry_id);
  }
  QB_CHECK(new_ids.size() == 2);
  QB_CHECK(scalar(first.db(), "SELECT COUNT(*) FROM jobs") == count_before + 2);
  QB_CHECK(sequence_value(first.db(), "jobs") == sequence_before + 2);
  QB_CHECK(logical_snapshot(decoy) == decoy_before);
  g_evidence.replay_race_successes = successes;
  g_evidence.replay_race_busy = busy;
  second.close();
  first.close();
}

void race_messages(const std::filesystem::path& path, qbrain::Brain& decoy) {
  qbrain::Brain first("n17-race-message-a");
  qbrain::Brain second("n17-race-message-b");
  first.open_at(qbrain::util::path_to_utf8(path));
  second.open_at(qbrain::util::path_to_utf8(path));
  const auto job_id = insert_job(first, "active", "race-message");
  const auto jobs_before = rowset_snapshot(first.db(), "SELECT * FROM jobs");
  const auto other_before = rows_except(first.db(), {"job_messages", "sqlite_sequence"});
  const auto schema_before = schema_snapshot(first.db());
  const auto sequence_before = sequence_value(first.db(), "job_messages");
  const auto count_before = scalar(first.db(), "SELECT COUNT(*) FROM job_messages");
  const auto decoy_before = logical_snapshot(decoy);

  std::atomic<int> ready{0};
  std::atomic<bool> start{false};
  qbrain::ops::OpResult first_result;
  qbrain::ops::OpResult second_result;
  auto invoke = [&](qbrain::Brain& brain, const char* sender,
                    qbrain::ops::OpResult& result) {
    ready.fetch_add(1, std::memory_order_release);
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    result = qbrain::test_support::call_op(
        brain, "send_job_message",
        {{"job_id", std::to_string(job_id)}, {"sender", sender},
         {"payload_json", json({{"sender", sender}}).dump()}});
  };
  std::thread a(invoke, std::ref(first), "race-a", std::ref(first_result));
  std::thread b(invoke, std::ref(second), "race-b", std::ref(second_result));
  while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
  start.store(true, std::memory_order_release);
  a.join();
  b.join();

  std::vector<int64_t> message_ids;
  qbrain::Brain* loser = nullptr;
  for (auto pair : {std::make_pair(&first_result, &first),
                    std::make_pair(&second_result, &second)}) {
    if (pair.first->ok) {
      message_ids.push_back(
          json::parse(pair.first->json)["message_id"].get<int64_t>());
    } else {
      QB_CHECK(operation_error_code(*pair.first) == "database_busy");
      loser = pair.second;
    }
  }
  const int successes = static_cast<int>(message_ids.size());
  const int busy = 2 - successes;
  QB_CHECK(successes == 1 || successes == 2);
  QB_CHECK(scalar(first.db(), "SELECT COUNT(*) FROM job_messages") ==
           count_before + successes);
  QB_CHECK(sequence_value(first.db(), "job_messages") == sequence_before + successes);
  QB_CHECK(rowset_snapshot(first.db(), "SELECT * FROM jobs") == jobs_before);
  QB_CHECK(rows_except(first.db(), {"job_messages", "sqlite_sequence"}) == other_before);
  QB_CHECK(schema_snapshot(first.db()) == schema_before);
  QB_CHECK(logical_snapshot(decoy) == decoy_before);
  if (message_ids.size() == 2) QB_CHECK(message_ids[0] != message_ids[1]);

  if (loser) {
    auto retry = qbrain::test_support::call_op(
        *loser, "send_job_message",
        {{"job_id", std::to_string(job_id)}, {"sender", "race-retry"},
         {"payload_json", "{}"}});
    QB_CHECK(retry.ok);
    const auto retry_id = json::parse(retry.json)["message_id"].get<int64_t>();
    QB_CHECK(std::find(message_ids.begin(), message_ids.end(), retry_id) ==
             message_ids.end());
    message_ids.push_back(retry_id);
  }
  QB_CHECK(message_ids.size() == 2);
  QB_CHECK(scalar(first.db(), "SELECT COUNT(*) FROM job_messages") == count_before + 2);
  QB_CHECK(sequence_value(first.db(), "job_messages") == sequence_before + 2);
  auto listed = qbrain::jobs::list_job_messages_checked(first, job_id, 10);
  QB_CHECK(listed.status == qbrain::jobs::JobOperationStatus::success &&
           listed.messages.size() == 2 && listed.messages[0].id > listed.messages[1].id);
  QB_CHECK(logical_snapshot(decoy) == decoy_before);
  g_evidence.message_race_successes = successes;
  g_evidence.message_race_busy = busy;
  second.close();
  first.close();
}

void exercise_deterministic_busy(const std::filesystem::path& path,
                                 qbrain::Brain& decoy) {
  qbrain::Brain locker("n17-busy-locker");
  qbrain::Brain contender("n17-busy-contender");
  locker.open_at(qbrain::util::path_to_utf8(path));
  contender.open_at(qbrain::util::path_to_utf8(path));
  const auto replay_id = insert_job(locker, "failed", "busy-replay");
  const auto message_job_id = insert_job(locker, "waiting", "busy-message");
  const auto before = logical_snapshot(locker);
  const auto decoy_before = logical_snapshot(decoy);

  locker.db().exec("BEGIN IMMEDIATE;");
  auto replay_busy = qbrain::test_support::call_op(
      contender, "replay_job", {{"job_id", std::to_string(replay_id)}});
  require_operation_error(replay_busy, "database_busy", "database");
  QB_CHECK(logical_snapshot(locker) == before);
  locker.db().exec("ROLLBACK;");
  QB_CHECK(qbrain::test_support::call_op(
               contender, "replay_job", {{"job_id", std::to_string(replay_id)}})
               .ok);

  const auto after_replay = logical_snapshot(locker);
  locker.db().exec("BEGIN IMMEDIATE;");
  auto message_busy = qbrain::test_support::call_op(
      contender, "send_job_message",
      {{"job_id", std::to_string(message_job_id)}, {"sender", "busy"},
       {"payload_json", "{}"}});
  require_operation_error(message_busy, "database_busy", "database");
  QB_CHECK(logical_snapshot(locker) == after_replay);
  locker.db().exec("ROLLBACK;");
  QB_CHECK(qbrain::test_support::call_op(
               contender, "send_job_message",
               {{"job_id", std::to_string(message_job_id)}, {"sender", "retry"},
                {"payload_json", "{}"}})
               .ok);
  QB_CHECK(logical_snapshot(decoy) == decoy_before);
  contender.close();
  locker.close();
}

}  // namespace

void test_n17() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "qbrain_n17_test";
  fs::remove_all(root);
  fs::create_directories(root);

  qbrain::ops::register_builtin_ops();
  exercise_migrations(root);

  qbrain::Brain selected("n17-selected");
  qbrain::Brain decoy("n17-decoy");
  selected.open_at(qbrain::util::path_to_utf8(root / "selected.db"));
  decoy.open_at(qbrain::util::path_to_utf8(root / "decoy.db"));
  const auto decoy_job = insert_job(decoy, "failed", "DECOY_N17_JOB_SENTINEL");
  const auto decoy_message = qbrain::jobs::send_job_message_checked(
      decoy, decoy_job, "decoy", R"({"value":"DECOY_N17_MESSAGE_SENTINEL"})");
  QB_CHECK(decoy_message.status == qbrain::jobs::JobOperationStatus::success);

  exercise_strict_ids_and_replay(selected, decoy);
  exercise_message_boundaries(selected, decoy);
  exercise_message_lists(selected, decoy);
  exercise_registry_and_mcp(selected, decoy);
  g_evidence.selected_hash = snapshot_sha256(logical_snapshot(selected));
  g_evidence.decoy_hash = snapshot_sha256(logical_snapshot(decoy));

  exercise_deterministic_busy(root / "deterministic-busy.db", decoy);
  race_replays(root / "race-replay.db", decoy);
  race_messages(root / "race-message.db", decoy);

  std::cout << "[INFO] n17 strict_id_alias_matrix=pass strict_id_cases="
            << g_evidence.strict_id_cases
            << " replay_terminal_state_matrix=pass replay_state_cases="
            << g_evidence.replay_state_cases
            << " sender_payload_utf8_json_boundaries=pass sender_payload_cases="
            << g_evidence.sender_payload_cases
            << " missing_vs_empty_list=pass list_limit_matrix=pass list_limit_cases="
            << g_evidence.list_limit_cases
            << " migration_v7_v8_v12=pass migration_idempotence=pass "
               "migration_rollback=pass damaged_integrity=pass no_foreign_key=pass "
               "registry_tools_list=pass real_mcp=pass default_deny=pass "
               "allow_write=pass mcp_rejection_cases="
            << g_evidence.mcp_rejection_cases
            << " selected_decoy_snapshots=pass concurrency=pass replay_race_successes="
            << g_evidence.replay_race_successes
            << " replay_race_busy=" << g_evidence.replay_race_busy
            << " message_race_successes=" << g_evidence.message_race_successes
            << " message_race_busy=" << g_evidence.message_race_busy
            << " selected_snapshot_sha256=" << g_evidence.selected_hash
            << " decoy_snapshot_sha256=" << g_evidence.decoy_hash
            << " migration_snapshot_sha256=" << g_evidence.migration_hash
            << " rollback_snapshot_sha256=" << g_evidence.rollback_hash << "\n";

  decoy.close();
  selected.close();
  fs::remove_all(root);
}
