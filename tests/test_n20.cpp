#include "qbrain/core/brain.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/schema/packs.hpp"
#include "qbrain/storage/database.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/paths.hpp"
#include "wave3_test_support.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

#define QB_CHECK(cond)                                                               \
  do {                                                                               \
    if (!(cond))                                                                     \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond + " @ " +      \
                               __FILE__ + ":" + std::to_string(__LINE__));            \
  } while (0)

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;
using qbrain::test_support::logical_snapshot;
using qbrain::test_support::snapshot_sha256;

constexpr std::size_t kPackMaximumBytes = 1'048'576;
constexpr int kMaximumPacks = 256;
constexpr int kMaximumDirectoryEntries = 4096;

#ifdef _WIN32
// A mount-point reparse point is a directory junction and does not require
// the symbolic-link privilege or Developer Mode on a writable NTFS temp root.
struct N20JunctionReparseData {
  DWORD reparse_tag;
  WORD reparse_data_length;
  WORD reserved;
  WORD substitute_name_offset;
  WORD substitute_name_length;
  WORD print_name_offset;
  WORD print_name_length;
  WCHAR path_buffer[1];
};
static_assert(offsetof(N20JunctionReparseData, path_buffer) == 16);

struct N20FileCaseSensitiveInfo {
  ULONG flags;
};
static_assert(sizeof(N20FileCaseSensitiveInfo) == sizeof(ULONG));

constexpr FILE_INFO_BY_HANDLE_CLASS kN20FileCaseSensitiveInfoClass =
    static_cast<FILE_INFO_BY_HANDLE_CLASS>(23);
constexpr ULONG kN20FileCsFlagCaseSensitiveDir = 1;
#endif

bool create_directory_junction(const fs::path& junction, const fs::path& target,
                               std::error_code& error) {
#ifdef _WIN32
  error.clear();
  std::error_code target_error;
  if (!fs::is_directory(target, target_error)) {
    error = target_error ? target_error
                         : std::make_error_code(std::errc::not_a_directory);
    return false;
  }
  const auto target_absolute = fs::absolute(target, target_error).lexically_normal();
  if (target_error) {
    error = target_error;
    return false;
  }

  const std::wstring print_name = target_absolute.wstring();
  const std::wstring substitute_name = L"\\??\\" + print_name;
  constexpr std::size_t kPathBufferOffset = 16;
  const std::size_t substitute_bytes = substitute_name.size() * sizeof(wchar_t);
  const std::size_t print_bytes = print_name.size() * sizeof(wchar_t);
  const std::size_t print_offset = substitute_bytes + sizeof(wchar_t);
  const std::size_t total_bytes =
      kPathBufferOffset + print_offset + print_bytes + sizeof(wchar_t);
  if (total_bytes > std::numeric_limits<WORD>::max() + 8u) {
    error = std::make_error_code(std::errc::filename_too_long);
    return false;
  }

  std::vector<unsigned char> buffer(total_bytes, 0);
  auto* data = reinterpret_cast<N20JunctionReparseData*>(buffer.data());
  data->reparse_tag = IO_REPARSE_TAG_MOUNT_POINT;
  data->reparse_data_length = static_cast<WORD>(total_bytes - 8u);
  data->substitute_name_offset = 0;
  data->substitute_name_length = static_cast<WORD>(substitute_bytes);
  data->print_name_offset = static_cast<WORD>(print_offset);
  data->print_name_length = static_cast<WORD>(print_bytes);
  auto* path_buffer = reinterpret_cast<unsigned char*>(data->path_buffer);
  std::memcpy(path_buffer, substitute_name.data(), substitute_bytes);
  std::memcpy(path_buffer + print_offset, print_name.data(), print_bytes);

  if (!CreateDirectoryW(junction.c_str(), nullptr)) {
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
  }
  bool created = true;
  HANDLE handle = INVALID_HANDLE_VALUE;
  auto fail = [&](DWORD code) {
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    if (created) RemoveDirectoryW(junction.c_str());
    error = std::error_code(static_cast<int>(code), std::system_category());
    return false;
  };

  handle = CreateFileW(junction.c_str(), GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING,
                       FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return fail(GetLastError());

  DWORD returned = 0;
  if (!DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
                       static_cast<DWORD>(total_bytes), nullptr, 0, &returned, nullptr))
    return fail(GetLastError());
  CloseHandle(handle);
  handle = INVALID_HANDLE_VALUE;
  const DWORD attributes = GetFileAttributesW(junction.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) return fail(GetLastError());
  if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 ||
      (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    return fail(ERROR_INVALID_REPARSE_DATA);
  created = false;
  return true;
#else
  (void)junction;
  (void)target;
  error = std::make_error_code(std::errc::operation_not_supported);
  return false;
#endif
}

bool enable_directory_case_sensitivity(const fs::path& directory,
                                       std::error_code& error) {
#ifdef _WIN32
  error.clear();
  HANDLE handle = CreateFileW(
      directory.c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
  }

  const auto fail = [&](DWORD code) {
    CloseHandle(handle);
    error = std::error_code(static_cast<int>(code), std::system_category());
    return false;
  };
  N20FileCaseSensitiveInfo requested{kN20FileCsFlagCaseSensitiveDir};
  if (!SetFileInformationByHandle(handle, kN20FileCaseSensitiveInfoClass, &requested,
                                  sizeof(requested))) {
    return fail(GetLastError());
  }

  N20FileCaseSensitiveInfo observed{};
  if (!GetFileInformationByHandleEx(handle, kN20FileCaseSensitiveInfoClass, &observed,
                                    sizeof(observed))) {
    return fail(GetLastError());
  }
  if (!CloseHandle(handle)) {
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
  }
  if ((observed.flags & kN20FileCsFlagCaseSensitiveDir) == 0) {
    error = std::error_code(ERROR_INVALID_DATA, std::system_category());
    return false;
  }
  return true;
#else
  (void)directory;
  error = std::make_error_code(std::errc::operation_not_supported);
  return false;
#endif
}

class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(std::string name, const std::string& value)
      : name_(std::move(name)) {
    if (const char* previous = std::getenv(name_.c_str())) previous_ = previous;
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

class ScopedTestRoot {
 public:
  ScopedTestRoot() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto base = fs::temp_directory_path();
    for (int attempt = 0; attempt < 128; ++attempt) {
      path_ = base / ("qbrain_n20_test_" + std::to_string(stamp) + "_" +
                      std::to_string(attempt));
      std::error_code error;
      if (fs::create_directory(path_, error)) return;
      if (error) throw std::runtime_error("failed to create unique N20 test root");
    }
    throw std::runtime_error("failed to allocate unique N20 test root");
  }

  ~ScopedTestRoot() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  ScopedTestRoot(const ScopedTestRoot&) = delete;
  ScopedTestRoot& operator=(const ScopedTestRoot&) = delete;

  const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

void require_keys(const json& object, std::initializer_list<std::string_view> expected) {
  QB_CHECK(object.is_object());
  std::set<std::string> actual;
  for (auto it = object.begin(); it != object.end(); ++it) actual.insert(it.key());
  std::set<std::string> wanted;
  for (const auto key : expected) wanted.emplace(key);
  QB_CHECK(actual == wanted);
}

std::string read_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("fixture read failed");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_file(const fs::path& path, std::string_view bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("fixture write failed");
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) throw std::runtime_error("fixture write incomplete");
}

fs::path pack_root() { return qbrain::util::qbrain_root() / "schema-packs"; }

json make_manifest(const std::string& id, std::vector<std::string> types = {"note"},
                   std::vector<std::string> dimensions = {"topic"}) {
  return {{"id", id},
          {"name", "N20 " + id},
          {"types", std::move(types)},
          {"dimensions", std::move(dimensions)}};
}

void write_pack(const std::string& id, const json& manifest) {
  write_file(pack_root() / (id + ".json"), manifest.dump());
}

void write_pack_bytes(const std::string& id, std::string_view bytes) {
  write_file(pack_root() / (id + ".json"), bytes);
}

void set_config(qbrain::Brain& brain, const std::string& key, const std::string& value) {
  auto statement = brain.db().prepare(
      "INSERT INTO config(key,value) VALUES(?,?) "
      "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  statement.bind_text(1, key);
  statement.bind_text(2, value);
  statement.step_done();
}

void erase_config(qbrain::Brain& brain, const std::string& key) {
  auto statement = brain.db().prepare("DELETE FROM config WHERE key=?");
  statement.bind_text(1, key);
  statement.step_done();
}

std::optional<std::string> get_config(qbrain::Brain& brain, const std::string& key) {
  auto statement = brain.db().prepare("SELECT value FROM config WHERE key=?");
  statement.bind_text(1, key);
  if (!statement.step()) return std::nullopt;
  return statement.column_text(0);
}

std::string lowercase(std::string value) {
  for (char& c : value) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return value;
}

std::string filesystem_snapshot(const fs::path& root) {
  std::error_code error;
  const auto status = fs::symlink_status(root, error);
  if (error || !fs::exists(status)) return "absent\n";

  std::vector<fs::path> paths;
  fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied,
                                             error);
  const fs::recursive_directory_iterator end;
  while (!error && iterator != end) {
    paths.push_back(iterator->path());
    iterator.increment(error);
  }
  if (error) throw std::runtime_error("filesystem snapshot enumeration failed");
  std::sort(paths.begin(), paths.end(), [&](const fs::path& left, const fs::path& right) {
    return qbrain::util::path_to_utf8(left.lexically_relative(root)) <
           qbrain::util::path_to_utf8(right.lexically_relative(root));
  });

  std::ostringstream snapshot;
  for (const auto& path : paths) {
    const auto relative = qbrain::util::path_to_utf8(path.lexically_relative(root));
    const auto item_status = fs::symlink_status(path, error);
    if (error) throw std::runtime_error("filesystem snapshot status failed");
    snapshot << relative << '|';
    if (fs::is_symlink(item_status)) {
      snapshot << "symlink|" << qbrain::util::path_to_utf8(fs::read_symlink(path, error));
      if (error) throw std::runtime_error("filesystem snapshot symlink failed");
    } else if (fs::is_directory(item_status)) {
      snapshot << "directory";
    } else if (fs::is_regular_file(item_status)) {
      const auto size = fs::file_size(path, error);
      if (error) throw std::runtime_error("filesystem snapshot size failed");
      const auto bytes = size == 0 ? std::string{} : read_file(path);
      snapshot << "file|" << size << '|' << qbrain::util::sha256_hex(bytes);
    } else {
      snapshot << "other";
    }
    const auto write_time = fs::last_write_time(path, error);
    if (!error) snapshot << '|' << write_time.time_since_epoch().count();
    error.clear();
    snapshot << '\n';
  }
  return snapshot.str();
}

std::string logical_snapshot_without_active_pack(qbrain::Brain& brain) {
  sqlite3* database = brain.db().handle();
  sqlite3_stmt* schema_statement = nullptr;
  const char* schema_sql =
      "SELECT type,name,COALESCE(tbl_name,''),COALESCE(sql,'') FROM sqlite_master "
      "WHERE name NOT LIKE 'sqlite_%' OR name='sqlite_sequence' ORDER BY type,name";
  QB_CHECK(sqlite3_prepare_v2(database, schema_sql, -1, &schema_statement, nullptr) ==
           SQLITE_OK);
  std::string snapshot = "schema\n";
  while (sqlite3_step(schema_statement) == SQLITE_ROW) {
    for (int column = 0; column < 4; ++column) {
      if (column) snapshot.push_back('|');
      snapshot += qbrain::test_support::snapshot_cell(schema_statement, column);
    }
    snapshot.push_back('\n');
  }
  sqlite3_finalize(schema_statement);

  sqlite3_stmt* tables_statement = nullptr;
  const char* tables_sql =
      "SELECT name FROM sqlite_master WHERE type='table' AND "
      "(name NOT LIKE 'sqlite_%' OR name='sqlite_sequence') ORDER BY name";
  QB_CHECK(sqlite3_prepare_v2(database, tables_sql, -1, &tables_statement, nullptr) ==
           SQLITE_OK);
  std::vector<std::string> tables;
  while (sqlite3_step(tables_statement) == SQLITE_ROW) {
    const auto* name = sqlite3_column_text(tables_statement, 0);
    if (name) tables.emplace_back(reinterpret_cast<const char*>(name));
  }
  sqlite3_finalize(tables_statement);

  for (const auto& table : tables) {
    const std::string sql = table == "config"
                                ? "SELECT * FROM config WHERE key<>'schema.active_pack'"
                                : "SELECT * FROM \"" + table + "\"";
    sqlite3_stmt* rows_statement = nullptr;
    QB_CHECK(sqlite3_prepare_v2(database, sql.c_str(), -1, &rows_statement, nullptr) ==
             SQLITE_OK);
    std::vector<std::string> rows;
    const int columns = sqlite3_column_count(rows_statement);
    int result = SQLITE_OK;
    while ((result = sqlite3_step(rows_statement)) == SQLITE_ROW) {
      std::string row;
      for (int column = 0; column < columns; ++column) {
        if (column) row.push_back('|');
        row += qbrain::test_support::snapshot_cell(rows_statement, column);
      }
      rows.push_back(std::move(row));
    }
    sqlite3_finalize(rows_statement);
    QB_CHECK(result == SQLITE_DONE);
    std::sort(rows.begin(), rows.end());
    snapshot += table + "#" + std::to_string(columns) + "\n";
    for (const auto& row : rows) snapshot += row + "\n";
  }
  return snapshot;
}

std::string serialized_database_snapshot(qbrain::Brain& brain) {
  sqlite3_int64 bytes = 0;
  unsigned char* image = sqlite3_serialize(brain.db().handle(), "main", &bytes, 0);
  if (!image || bytes <= 0) {
    if (image) sqlite3_free(image);
    throw std::runtime_error("database serialization failed");
  }
  std::string snapshot(reinterpret_cast<const char*>(image),
                       static_cast<std::size_t>(bytes));
  sqlite3_free(image);
  return snapshot;
}

struct SnapshotEvidence {
  std::size_t index = 0;
  std::string label;
  std::string selected_before;
  std::string selected_after;
  std::string decoy_before;
  std::string decoy_after;
  std::string filesystem_before;
  std::string filesystem_after;
};

std::vector<SnapshotEvidence> g_snapshot_evidence;

struct ReloadEvidence {
  std::string label;
  std::string selected_before;
  std::string selected_after;
  std::string selected_without_active_before;
  std::string selected_without_active_after;
  std::string decoy_before;
  std::string decoy_after;
  std::string filesystem_before;
  std::string filesystem_after;
  std::string old_id;
  std::string new_id;
};

std::vector<ReloadEvidence> g_reload_evidence;

class SnapshotMatrix {
 public:
  SnapshotMatrix(qbrain::Brain& selected, qbrain::Brain& decoy, fs::path filesystem_root)
      : selected_(selected), decoy_(decoy), filesystem_root_(std::move(filesystem_root)) {}

  template <typename Fn>
  auto run_read(const std::string& label, Fn&& fn) {
    return run_read_with_selected_snapshot(
        label, [](qbrain::Brain& brain) { return logical_snapshot(brain); },
        std::forward<Fn>(fn));
  }

  template <typename SnapshotFn, typename Fn>
  auto run_read_with_selected_snapshot(const std::string& label,
                                       SnapshotFn&& selected_snapshot, Fn&& fn) {
    const auto selected_before = selected_snapshot(selected_);
    const auto decoy_before = logical_snapshot(decoy_);
    const auto files_before = filesystem_snapshot(filesystem_root_);
    auto result = fn();
    const auto selected_after = selected_snapshot(selected_);
    const auto decoy_after = logical_snapshot(decoy_);
    const auto files_after = filesystem_snapshot(filesystem_root_);
    if (selected_before != selected_after || decoy_before != decoy_after ||
        files_before != files_after) {
      throw std::runtime_error("N20 read/rejection mutated state: " + label);
    }
    g_snapshot_evidence.push_back(
        {g_snapshot_evidence.size() + 1, label, snapshot_sha256(selected_before),
         snapshot_sha256(selected_after), snapshot_sha256(decoy_before),
         snapshot_sha256(decoy_after), snapshot_sha256(files_before),
         snapshot_sha256(files_after)});
    return result;
  }

 private:
  qbrain::Brain& selected_;
  qbrain::Brain& decoy_;
  fs::path filesystem_root_;
};

class DatabaseReadObserver {
 public:
  explicit DatabaseReadObserver(qbrain::Brain& brain) : database_(brain.db().handle()) {
    QB_CHECK(sqlite3_set_authorizer(database_, &DatabaseReadObserver::authorize, this) ==
             SQLITE_OK);
  }
  ~DatabaseReadObserver() { sqlite3_set_authorizer(database_, nullptr, nullptr); }

  int application_reads() const { return application_reads_; }
  int page_reads() const { return page_reads_; }

 private:
  static int authorize(void* context, int action, const char* table, const char*,
                       const char*, const char*) {
    if (action == SQLITE_READ && table && !std::string_view(table).starts_with("sqlite_")) {
      auto* observer = static_cast<DatabaseReadObserver*>(context);
      ++observer->application_reads_;
      if (std::string_view(table) == "pages") ++observer->page_reads_;
    }
    return SQLITE_OK;
  }

  sqlite3* database_ = nullptr;
  int application_reads_ = 0;
  int page_reads_ = 0;
};

qbrain::ops::OpResult call_op(qbrain::Brain& brain, const std::string& name,
                              std::unordered_map<std::string, std::string> args = {},
                              bool remote = false, bool allow_write = false) {
  qbrain::ops::OpContext context;
  context.brain = &brain;
  context.remote = remote;
  context.allow_write = allow_write;
  context.args = std::move(args);
  return qbrain::ops::global_registry().call(name, context);
}

json require_success(const qbrain::ops::OpResult& result) {
  QB_CHECK(result.ok);
  QB_CHECK(result.exit_code == 0);
  QB_CHECK(!result.json.empty());
  QB_CHECK(result.text == result.json);
  return json::parse(result.json);
}

json require_operation_error(const qbrain::ops::OpResult& result,
                             const std::string& code, const std::string& field,
                             std::initializer_list<std::string_view> forbidden = {}) {
  QB_CHECK(!result.ok);
  QB_CHECK(result.exit_code != 0);
  QB_CHECK(!result.json.empty() && result.json.size() <= 2048);
  QB_CHECK(result.text.size() <= 2048);
  QB_CHECK(result.text == result.json);
  const auto payload = json::parse(result.json);
  require_keys(payload, {"error"});
  require_keys(payload["error"], {"code", "field", "message"});
  QB_CHECK(payload["error"]["code"] == code);
  QB_CHECK(payload["error"]["field"] == field);
  QB_CHECK(payload["error"]["message"].is_string());
  QB_CHECK(payload["error"]["message"].get<std::string>().size() <= 512);
  for (const auto value : forbidden) {
    if (value.empty()) continue;
    QB_CHECK(result.json.find(value) == std::string::npos);
    QB_CHECK(result.text.find(value) == std::string::npos);
  }
  return payload["error"];
}

template <typename Fn>
void require_pack_error(Fn&& fn, const std::string& code,
                        const std::string& field) {
  try {
    fn();
  } catch (const qbrain::schema::PackError& error) {
    QB_CHECK(error.code() == code);
    QB_CHECK(error.field() == field);
    QB_CHECK(std::string(error.what()).size() <= 512);
    return;
  }
  throw std::runtime_error("expected PackError");
}

json mcp_call(qbrain::Brain& brain, const qbrain::mcp::ServeOptions& options,
              const std::string& operation, const json& arguments, int request_id) {
  const json body = {{"jsonrpc", "2.0"},
                     {"id", request_id},
                     {"method", "tools/call"},
                     {"params", {{"name", operation}, {"arguments", arguments}}}};
  const auto response = qbrain::mcp::handle_rpc_body(brain, options, body.dump());
  QB_CHECK(!response.empty() && response.size() <= 2'000'000);
  return json::parse(response);
}

json structured_mcp_content(const json& response) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(response["result"].contains("content"));
  const auto& content = response["result"]["content"];
  for (auto it = content.rbegin(); it != content.rend(); ++it) {
    if (!it->is_object() || it->value("type", "") != "text") continue;
    try {
      return json::parse(it->value("text", ""));
    } catch (...) {
    }
  }
  throw std::runtime_error("MCP response lacks structured JSON content");
}

json require_mcp_success(const json& response) {
  QB_CHECK(response["result"]["isError"] == false);
  return structured_mcp_content(response);
}

json require_mcp_error(const json& response, const std::string& code,
                       const std::string& field,
                       std::initializer_list<std::string_view> forbidden = {}) {
  QB_CHECK(response["result"]["isError"] == true);
  const auto& content = response["result"]["content"];
  QB_CHECK(content.is_array());
  QB_CHECK(content.size() == 1);
  std::size_t text_blocks = 0;
  std::size_t structured_blocks = 0;
  json payload;
  for (const auto& item : content) {
    if (!item.is_object() || item.value("type", "") != "text") continue;
    ++text_blocks;
    const auto text = item.value("text", "");
    try {
      const auto parsed = json::parse(text);
      if (!parsed.is_object() || !parsed.contains("error")) continue;
      ++structured_blocks;
      payload = parsed;
    } catch (...) {
    }
  }
  QB_CHECK(text_blocks == 1);
  QB_CHECK(structured_blocks == 1);
  require_keys(payload, {"error"});
  require_keys(payload["error"], {"code", "field", "message"});
  QB_CHECK(payload["error"]["code"] == code);
  QB_CHECK(payload["error"]["field"] == field);
  QB_CHECK(payload["error"]["message"].is_string());
  QB_CHECK(payload["error"]["message"].get<std::string>().size() <= 512);
  const auto serialized = response.dump();
  QB_CHECK(serialized.size() <= 4096);
  for (const auto value : forbidden) {
    if (!value.empty()) QB_CHECK(serialized.find(value) == std::string::npos);
  }
  return payload["error"];
}

const json& find_tool(const json& tools, const std::string& name) {
  for (const auto& tool : tools) {
    if (tool.value("name", "") == name) return tool;
  }
  throw std::runtime_error("missing MCP tool: " + name);
}

void require_manifest(const json& manifest, const std::string& id,
                      const std::vector<std::string>& dimensions) {
  QB_CHECK(manifest.is_object());
  std::set<std::string> allowed = {"id", "name", "version", "types", "dimensions",
                                   "phases"};
  for (auto it = manifest.begin(); it != manifest.end(); ++it) {
    QB_CHECK(allowed.contains(it.key()));
  }
  QB_CHECK(manifest["id"] == id);
  QB_CHECK(manifest["name"].is_string());
  QB_CHECK(manifest["types"].is_array() && !manifest["types"].empty());
  QB_CHECK(manifest["dimensions"] == dimensions);
}

void require_pack_shape(const json& payload, const std::string& id,
                        const std::string& origin,
                        const std::vector<std::string>& dimensions) {
  require_keys(payload, {"id", "origin", "pack"});
  QB_CHECK(payload["id"] == id);
  QB_CHECK(payload["origin"] == origin);
  require_manifest(payload["pack"], id, dimensions);
}

qbrain::Page seed_page(qbrain::Brain& brain, const std::string& source_id,
                       const std::string& slug, const std::string& type,
                       bool deleted = false) {
  qbrain::PageInput input;
  input.source_id = source_id;
  input.slug = slug;
  input.title = slug;
  input.body = "N20 fixture body";
  input.type = type;
  auto page = brain.put_page(input);
  if (deleted) QB_CHECK(brain.soft_delete(slug, source_id));
  return page;
}

struct ExpectedStats {
  std::string source_id;
  std::string active_pack_id;
  int schema_version = 0;
  int64_t total_active_pages = 0;
  std::vector<std::pair<std::string, int64_t>> type_counts;
  bool truncated = false;
};

ExpectedStats direct_stats(qbrain::Brain& brain, const std::string& source_id,
                           int limit) {
  const auto integrity = qbrain::storage::check_schema_integrity(brain.db());
  QB_CHECK(integrity.ok);
  ExpectedStats expected;
  expected.source_id = source_id;
  expected.active_pack_id = get_config(brain, "schema.active_pack").value_or("default");
  expected.schema_version = integrity.schema_version;
  {
    auto total = brain.db().prepare(
        "SELECT COUNT(*) FROM pages WHERE source_id=? AND deleted_at IS NULL");
    total.bind_text(1, source_id);
    QB_CHECK(total.step());
    expected.total_active_pages = total.column_int(0);
  }
  auto counts = brain.db().prepare(
      "SELECT type,COUNT(*) FROM pages WHERE source_id=? AND deleted_at IS NULL "
      "GROUP BY type ORDER BY COUNT(*) DESC,type COLLATE BINARY ASC LIMIT ?");
  counts.bind_text(1, source_id);
  counts.bind_int(2, static_cast<int64_t>(limit) + 1);
  while (counts.step()) {
    expected.type_counts.emplace_back(counts.column_text(0), counts.column_int(1));
  }
  if (static_cast<int>(expected.type_counts.size()) > limit) {
    expected.truncated = true;
    expected.type_counts.resize(static_cast<std::size_t>(limit));
  }
  return expected;
}

void require_stats_shape(const json& payload, const ExpectedStats& expected) {
  require_keys(payload, {"source_id", "active_pack_id", "schema_version",
                         "total_active_pages", "type_counts", "truncated"});
  QB_CHECK(payload["source_id"] == expected.source_id);
  QB_CHECK(payload["active_pack_id"] == expected.active_pack_id);
  QB_CHECK(payload["schema_version"] == expected.schema_version);
  QB_CHECK(payload["total_active_pages"] == expected.total_active_pages);
  QB_CHECK(payload["truncated"] == expected.truncated);
  QB_CHECK(payload["type_counts"].is_array());
  QB_CHECK(payload["type_counts"].size() == expected.type_counts.size());
  for (std::size_t index = 0; index < expected.type_counts.size(); ++index) {
    const auto& row = payload["type_counts"][index];
    require_keys(row, {"type", "count"});
    QB_CHECK(row["type"] == expected.type_counts[index].first);
    QB_CHECK(row["count"] == expected.type_counts[index].second);
  }
}

void exercise_pack_id_contract(qbrain::Brain& selected, qbrain::Brain& decoy,
                               SnapshotMatrix& matrix) {
  const auto decoy_before = logical_snapshot(decoy);
  const auto one = qbrain::schema::canonical_pack_id("a");
  QB_CHECK(one && *one == "a");
  const auto maximum = qbrain::schema::canonical_pack_id(std::string(64, 'Z'));
  QB_CHECK(maximum && *maximum == std::string(64, 'z'));
  const auto mixed = qbrain::schema::canonical_pack_id("MiXeD_9-");
  QB_CHECK(mixed && *mixed == "mixed_9-");

  std::string embedded_nul("ok\0bad", 6);
  const std::vector<std::string> invalid = {
      "",          ".",          "..",          "bad/name",      "bad\\name",
      "C:pack",    "name:stream", "C:\\pack",    "\\\\host\\share", "\\\\?\\Volume{n20}",
      "CON",       "con",        "PrN",         "COM1",          "lpt9",
      "trailing.", "trailing ",  std::string(65, 'x'),
      std::string("unicode-") + "\xE2\x84\xAA", embedded_nul};
  for (const auto& value : invalid) {
    QB_CHECK(!qbrain::schema::canonical_pack_id(value));
    auto rejected = matrix.run_read("id:reject", [&] {
      return call_op(selected, "ontology_get", {{"id", value}});
    });
    require_operation_error(rejected, "invalid_pack_id", "id",
                            {value, qbrain::util::path_to_utf8(pack_root())});
  }

  write_pack("mixed_9-", make_manifest("mixed_9-", {"note"}, {"topic"}));
  auto canonicalized = matrix.run_read("id:mixed-case-canonicalization", [&] {
    return call_op(selected, "ontology_get", {{"id", "MiXeD_9-"}});
  });
  require_pack_shape(require_success(canonicalized), "mixed_9-", "installed", {"topic"});
  QB_CHECK(logical_snapshot(decoy) == decoy_before);
}

void exercise_builtin_no_create(qbrain::Brain& selected, qbrain::Brain& decoy,
                                SnapshotMatrix& matrix, const fs::path& isolated_root) {
  const auto decoy_before = logical_snapshot(decoy);
  fs::remove_all(isolated_root / "Qbrain");
  erase_config(selected, "schema.active_pack");
  QB_CHECK(!fs::exists(pack_root()));

  auto listed = matrix.run_read("builtin:list:no-create", [&] {
    return call_op(selected, "list_schema_packs");
  });
  const auto list_payload = require_success(listed);
  require_keys(list_payload, {"active_id", "packs"});
  QB_CHECK(list_payload["active_id"] == "default");
  QB_CHECK(list_payload["packs"].size() == 1);
  require_keys(list_payload["packs"][0], {"id", "origin", "active"});
  QB_CHECK(list_payload["packs"][0] ==
           json({{"id", "default"}, {"origin", "builtin"}, {"active", true}}));

  auto active = matrix.run_read("builtin:get-active:no-create", [&] {
    return call_op(selected, "get_active_schema_pack");
  });
  const auto active_payload = require_success(active);
  require_pack_shape(active_payload, "default", "builtin", {"topic", "entity", "time"});

  auto ontology = matrix.run_read("builtin:ontology:no-create", [&] {
    return call_op(selected, "ontology_get");
  });
  QB_CHECK(require_success(ontology) == active_payload);

  auto dimensions = matrix.run_read("builtin:dimensions:no-create", [&] {
    return call_op(selected, "ontology_dimensions");
  });
  const auto dimension_payload = require_success(dimensions);
  require_keys(dimension_payload, {"id", "dimensions"});
  QB_CHECK(dimension_payload["id"] == "default");
  QB_CHECK(dimension_payload["dimensions"] == json({"topic", "entity", "time"}));

  auto stats = matrix.run_read("builtin:stats:no-create", [&] {
    return call_op(selected, "schema_stats");
  });
  require_stats_shape(require_success(stats), direct_stats(selected, "default", 100));

  auto reload = matrix.run_read("builtin:reload:no-create", [&] {
    return call_op(selected, "reload_schema_pack", {}, false, true);
  });
  QB_CHECK(require_success(reload) ==
           json({{"id", "default"}, {"changed", false}}));

  auto listed_repeat = matrix.run_read("builtin:list:byte-repeat", [&] {
    return call_op(selected, "list_schema_packs");
  });
  QB_CHECK(listed_repeat.json == listed.json);
  QB_CHECK(!fs::exists(pack_root()));
  QB_CHECK(!fs::exists(qbrain::util::config_path()));
  QB_CHECK(logical_snapshot(decoy) == decoy_before);
}

void exercise_listing_and_exact_shapes(qbrain::Brain& selected, qbrain::Brain& decoy,
                                       SnapshotMatrix& matrix) {
  fs::remove_all(pack_root());
  write_pack("zeta", make_manifest("zeta", {"note", "person"}, {"zdim"}));
  write_pack("alpha", make_manifest("alpha", {"note"}, {}));
  write_pack("default", make_manifest("default", {"custom"}, {"override"}));
  set_config(selected, "schema.active_pack", "alpha");
  set_config(decoy, "schema.active_pack", "zeta");

  auto list_selected = matrix.run_read("listing:selected:deterministic", [&] {
    return call_op(selected, "list_schema_packs");
  });
  const auto selected_payload = require_success(list_selected);
  require_keys(selected_payload, {"active_id", "packs"});
  QB_CHECK(selected_payload["active_id"] == "alpha");
  QB_CHECK(selected_payload["packs"] ==
           json({{{"id", "alpha"}, {"origin", "installed"}, {"active", true}},
                 {{"id", "default"}, {"origin", "installed"}, {"active", false}},
                 {{"id", "zeta"}, {"origin", "installed"}, {"active", false}}}));
  int selected_active_count = 0;
  for (const auto& row : selected_payload["packs"])
    if (row["active"] == true) ++selected_active_count;
  QB_CHECK(selected_active_count == 1);

  const auto serialized = list_selected.json;
  const std::vector<std::string> forbidden_listing = {
      qbrain::util::path_to_utf8(pack_root()), ".json", "schema-packs", "last_write",
      "file_size"};
  for (const auto& forbidden : forbidden_listing) {
    QB_CHECK(serialized.find(forbidden) == std::string::npos);
  }

  auto list_decoy = matrix.run_read("listing:decoy:active", [&] {
    return call_op(decoy, "list_schema_packs");
  });
  const auto decoy_payload = require_success(list_decoy);
  QB_CHECK(decoy_payload["active_id"] == "zeta");
  QB_CHECK(decoy_payload["packs"][0]["active"] == false);
  QB_CHECK(decoy_payload["packs"][2]["active"] == true);

  auto selected_active = matrix.run_read("shape:get-active:selected", [&] {
    return call_op(selected, "get_active_schema_pack");
  });
  require_pack_shape(require_success(selected_active), "alpha", "installed", {});
  auto decoy_active = matrix.run_read("shape:get-active:decoy", [&] {
    return call_op(decoy, "get_active_schema_pack");
  });
  require_pack_shape(require_success(decoy_active), "zeta", "installed", {"zdim"});

  auto named = matrix.run_read("shape:ontology:named", [&] {
    return call_op(selected, "ontology_get", {{"id", "zeta"}});
  });
  require_pack_shape(require_success(named), "zeta", "installed", {"zdim"});
  QB_CHECK(get_config(selected, "schema.active_pack") == "alpha");

  auto named_dimensions = matrix.run_read("shape:dimensions:named", [&] {
    return call_op(selected, "ontology_dimensions", {{"id", "zeta"}});
  });
  const auto dimensions = require_success(named_dimensions);
  require_keys(dimensions, {"id", "dimensions"});
  QB_CHECK(dimensions == json({{"id", "zeta"}, {"dimensions", {"zdim"}}}));

  auto empty_dimensions = matrix.run_read("shape:dimensions:valid-empty", [&] {
    return call_op(selected, "ontology_dimensions", {{"id", "alpha"}});
  });
  const auto empty = require_success(empty_dimensions);
  QB_CHECK(empty == json({{"id", "alpha"}, {"dimensions", json::array()}}));

  auto repeat = matrix.run_read("shape:canonical-byte-repeat", [&] {
    return call_op(selected, "get_active_schema_pack");
  });
  QB_CHECK(repeat.json == selected_active.json);
}

std::string fixed_identifier(const std::string& prefix, int index) {
  std::ostringstream value;
  value << prefix << std::setw(3) << std::setfill('0') << index;
  auto result = value.str();
  result.append(64 - result.size(), 'x');
  return result;
}

void exercise_manifest_contract(qbrain::Brain& selected, qbrain::Brain& decoy,
                                SnapshotMatrix& matrix) {
  const auto decoy_before = logical_snapshot(decoy);
  fs::remove_all(pack_root());
  set_config(selected, "schema.active_pack", "default");

  const std::string maximum_id(64, 'm');
  json maximum_manifest = {{"id", maximum_id},
                           {"name", std::string(256, 'n')},
                           {"version", std::string(64, 'v')},
                           {"types", json::array()},
                           {"dimensions", json::array()},
                           {"phases", json::array()}};
  for (int index = 0; index < 256; ++index) {
    maximum_manifest["types"].push_back(fixed_identifier("t", index));
    maximum_manifest["dimensions"].push_back(fixed_identifier("d", index));
  }
  for (int index = 0; index < 64; ++index) {
    maximum_manifest["phases"].push_back(fixed_identifier("p", index));
  }
  write_pack(maximum_id, maximum_manifest);
  auto maximum = matrix.run_read("manifest:maximum-valid", [&] {
    return call_op(selected, "ontology_get", {{"id", maximum_id}});
  });
  const auto maximum_payload = require_success(maximum);
  require_pack_shape(maximum_payload, maximum_id, "installed",
                     maximum_manifest["dimensions"].get<std::vector<std::string>>());
  QB_CHECK(maximum_payload["pack"]["name"].get<std::string>().size() == 256);
  QB_CHECK(maximum_payload["pack"]["version"].get<std::string>().size() == 64);
  QB_CHECK(maximum_payload["pack"]["types"].size() == 256);
  QB_CHECK(maximum_payload["pack"]["dimensions"].size() == 256);
  QB_CHECK(maximum_payload["pack"]["phases"].size() == 64);

  std::string invalid_utf8 =
      R"({"id":"bad","name":"bad-)";
  invalid_utf8.push_back(static_cast<char>(0xff));
  invalid_utf8 += R"(","types":["note"],"dimensions":[]})";
  const std::string bom_manifest = std::string("\xEF\xBB\xBF") + make_manifest("bad").dump();
  const std::string excessive_nesting =
      R"({"id":"bad","name":"Bad","types":["note"],"dimensions":[],"extra":[[[[[[[[[]]]]]]]]]})";

  json too_many_types = make_manifest("bad");
  too_many_types["types"] = json::array();
  for (int index = 0; index < 257; ++index)
    too_many_types["types"].push_back("t" + std::to_string(index));
  json too_many_dimensions = make_manifest("bad");
  too_many_dimensions["dimensions"] = json::array();
  for (int index = 0; index < 257; ++index)
    too_many_dimensions["dimensions"].push_back("d" + std::to_string(index));
  json too_many_phases = make_manifest("bad");
  too_many_phases["phases"] = json::array();
  for (int index = 0; index < 65; ++index)
    too_many_phases["phases"].push_back("phase" + std::to_string(index));

  const std::vector<std::pair<std::string, std::string>> invalid_manifests = {
      {"malformed", "{"},
      {"trailing", make_manifest("bad").dump() + " trailing"},
      {"non-object", R"(["bad"])"},
      {"duplicate-root-key",
       R"({"id":"bad","id":"bad","name":"Bad","types":["note"],"dimensions":[]})"},
      {"unknown-key",
       json({{"id", "bad"}, {"name", "Bad"}, {"types", {"note"}},
             {"dimensions", json::array()}, {"unknown", true}}).dump()},
      {"missing-id", json({{"name", "Bad"}, {"types", {"note"}},
                            {"dimensions", json::array()}}).dump()},
      {"missing-name", json({{"id", "bad"}, {"types", {"note"}},
                              {"dimensions", json::array()}}).dump()},
      {"missing-types", json({{"id", "bad"}, {"name", "Bad"},
                               {"dimensions", json::array()}}).dump()},
      {"missing-dimensions",
       json({{"id", "bad"}, {"name", "Bad"}, {"types", {"note"}}}).dump()},
      {"wrong-id-type", json({{"id", 7}, {"name", "Bad"}, {"types", {"note"}},
                                {"dimensions", json::array()}}).dump()},
      {"id-mismatch", make_manifest("different").dump()},
      {"bom", bom_manifest},
      {"excessive-nesting", excessive_nesting},
      {"wrong-name-type", json({{"id", "bad"}, {"name", 7}, {"types", {"note"}},
                                  {"dimensions", json::array()}}).dump()},
      {"wrong-types-type", json({{"id", "bad"}, {"name", "Bad"}, {"types", 7},
                                  {"dimensions", json::array()}}).dump()},
      {"wrong-dimensions-type",
       json({{"id", "bad"}, {"name", "Bad"}, {"types", {"note"}},
             {"dimensions", 7}}).dump()},
      {"empty-name", json({{"id", "bad"}, {"name", ""}, {"types", {"note"}},
                            {"dimensions", json::array()}}).dump()},
      {"long-name", json({{"id", "bad"}, {"name", std::string(257, 'n')},
                            {"types", {"note"}}, {"dimensions", json::array()}}).dump()},
      {"long-version", json({{"id", "bad"}, {"name", "Bad"},
                               {"version", std::string(65, 'v')}, {"types", {"note"}},
                               {"dimensions", json::array()}}).dump()},
      {"empty-version", json({{"id", "bad"}, {"name", "Bad"}, {"version", ""},
                                {"types", {"note"}}, {"dimensions", json::array()}}).dump()},
      {"wrong-version-type", json({{"id", "bad"}, {"name", "Bad"}, {"version", 7},
                                     {"types", {"note"}},
                                     {"dimensions", json::array()}}).dump()},
      {"empty-types", json({{"id", "bad"}, {"name", "Bad"},
                              {"types", json::array()},
                              {"dimensions", json::array()}}).dump()},
      {"empty-type-member", json({{"id", "bad"}, {"name", "Bad"}, {"types", {""}},
                                    {"dimensions", json::array()}}).dump()},
      {"long-type-member", json({{"id", "bad"}, {"name", "Bad"},
                                   {"types", {std::string(65, 't')}},
                                   {"dimensions", json::array()}}).dump()},
      {"reserved-type-member", json({{"id", "bad"}, {"name", "Bad"},
                                       {"types", {"CON"}},
                                       {"dimensions", json::array()}}).dump()},
      {"illegal-type-member", json({{"id", "bad"}, {"name", "Bad"},
                                      {"types", {"bad/type"}},
                                      {"dimensions", json::array()}}).dump()},
      {"empty-dimension-member", json({{"id", "bad"}, {"name", "Bad"},
                                         {"types", {"note"}}, {"dimensions", {""}}}).dump()},
      {"long-dimension-member", json({{"id", "bad"}, {"name", "Bad"},
                                        {"types", {"note"}},
                                        {"dimensions", {std::string(65, 'd')}}}).dump()},
      {"reserved-dimension-member", json({{"id", "bad"}, {"name", "Bad"},
                                            {"types", {"note"}},
                                            {"dimensions", {"NUL"}}}).dump()},
      {"illegal-dimension-member", json({{"id", "bad"}, {"name", "Bad"},
                                           {"types", {"note"}},
                                           {"dimensions", {"bad.dimension"}}}).dump()},
      {"too-many-types", too_many_types.dump()},
      {"too-many-dimensions", too_many_dimensions.dump()},
      {"too-many-phases", too_many_phases.dump()},
      {"duplicate-type",
       json({{"id", "bad"}, {"name", "Bad"}, {"types", {"Note", "note"}},
             {"dimensions", json::array()}}).dump()},
      {"duplicate-dimension",
       json({{"id", "bad"}, {"name", "Bad"}, {"types", {"note"}},
             {"dimensions", {"Topic", "topic"}}}).dump()},
      {"duplicate-phase",
       json({{"id", "bad"}, {"name", "Bad"}, {"types", {"note"}},
             {"dimensions", json::array()}, {"phases", {"same", "same"}}}).dump()},
      {"wrong-phases-type", json({{"id", "bad"}, {"name", "Bad"},
                                    {"types", {"note"}},
                                    {"dimensions", json::array()}, {"phases", 7}}).dump()},
      {"empty-phase", json({{"id", "bad"}, {"name", "Bad"}, {"types", {"note"}},
                             {"dimensions", json::array()}, {"phases", {""}}}).dump()},
      {"long-phase", json({{"id", "bad"}, {"name", "Bad"}, {"types", {"note"}},
                            {"dimensions", json::array()},
                            {"phases", {std::string(65, 'p')}}}).dump()},
      {"non-string-phase", json({{"id", "bad"}, {"name", "Bad"},
                                   {"types", {"note"}},
                                   {"dimensions", json::array()}, {"phases", {1}}}).dump()},
      {"non-string-type",
       json({{"id", "bad"}, {"name", "Bad"}, {"types", {1}},
             {"dimensions", json::array()}}).dump()},
      {"non-string-dimension",
       json({{"id", "bad"}, {"name", "Bad"}, {"types", {"note"}},
             {"dimensions", {json::object()}}}).dump()},
      {"invalid-utf8", invalid_utf8},
  };

  for (const auto& [label, bytes] : invalid_manifests) {
    write_pack_bytes("bad", bytes);
    for (const auto& operation : {"ontology_get", "ontology_dimensions",
                                  "reload_schema_pack"}) {
      auto rejected = matrix.run_read("manifest:" + label + ":" + operation, [&] {
        return call_op(selected, operation, {{"id", "bad"}}, false, true);
      });
      require_operation_error(rejected, "pack_invalid", "pack",
                              {"bad", "N20_RAW_MANIFEST_SENTINEL",
                               qbrain::util::path_to_utf8(pack_root())});
      QB_CHECK(get_config(selected, "schema.active_pack") == "default");
    }

    set_config(selected, "schema.active_pack", "bad");
    auto active_rejected = matrix.run_read("manifest:" + label + ":get-active", [&] {
      return call_op(selected, "get_active_schema_pack");
    });
    require_operation_error(active_rejected, "pack_invalid", "pack",
                            {"bad", qbrain::util::path_to_utf8(pack_root())});
    QB_CHECK(get_config(selected, "schema.active_pack") == "bad");
    set_config(selected, "schema.active_pack", "default");
  }

  QB_CHECK(logical_snapshot(decoy) == decoy_before);
}

void exercise_filesystem_safety(qbrain::Brain& selected, qbrain::Brain& decoy,
                                SnapshotMatrix& matrix, const fs::path& test_root) {
  const auto decoy_before = logical_snapshot(decoy);
  fs::remove_all(pack_root());
  set_config(selected, "schema.active_pack", "default");

  auto missing = matrix.run_read("filesystem:missing", [&] {
    return call_op(selected, "ontology_get", {{"id", "missing"}});
  });
  require_operation_error(missing, "pack_not_found", "id", {"missing", "schema-packs"});

  fs::create_directories(pack_root() / "directory.json");
  auto directory = matrix.run_read("filesystem:directory-candidate", [&] {
    return call_op(selected, "ontology_get", {{"id", "directory"}});
  });
  require_operation_error(directory, "pack_unsafe", "pack", {"directory", "schema-packs"});
  fs::remove_all(pack_root() / "directory.json");

  const auto outside = test_root / "N20_OUTSIDE_SENTINEL";
  const auto outside_file = outside / "link.json";
  const std::string outside_bytes =
      R"({"id":"link","name":"N20_OUTSIDE_RAW_SENTINEL","types":["note"],"dimensions":[]})";
  write_file(outside_file, outside_bytes);
  const auto outside_hash_before = qbrain::util::sha256_hex(read_file(outside_file));
  const auto outside_time_before = fs::last_write_time(outside_file);
  std::error_code symlink_error;
  QB_CHECK(create_directory_junction(pack_root() / "link.json", outside, symlink_error));
  QB_CHECK(!symlink_error);
  auto symlink = matrix.run_read("filesystem:symlink-candidate", [&] {
    return call_op(selected, "ontology_get", {{"id", "link"}});
  });
  require_operation_error(symlink, "pack_unsafe", "pack",
                          {"link", "N20_OUTSIDE_RAW_SENTINEL",
                           qbrain::util::path_to_utf8(outside),
                           qbrain::util::path_to_utf8(outside_file)});
  QB_CHECK(read_file(outside_file) == outside_bytes);
  QB_CHECK(fs::remove(pack_root() / "link.json"));
  QB_CHECK(qbrain::util::sha256_hex(read_file(outside_file)) == outside_hash_before);
  QB_CHECK(fs::last_write_time(outside_file) == outside_time_before);

  const auto qbrain_root = qbrain::util::qbrain_root();
  const auto outside_pack_root = test_root / "N20_OUTSIDE_PACK_ROOT";
  fs::create_directories(outside_pack_root);
  const auto outside_pack = outside_pack_root / "escape.json";
  const std::string outside_pack_bytes =
      R"({"id":"escape","name":"N20_OUTSIDE_ROOT_RAW_SENTINEL","types":["note"],"dimensions":[]})";
  write_file(outside_pack, outside_pack_bytes);
  const auto outside_pack_hash = qbrain::util::sha256_hex(read_file(outside_pack));
  const auto outside_pack_time = fs::last_write_time(outside_pack);
  fs::remove_all(pack_root());
  fs::create_directories(qbrain_root);
  std::error_code pack_root_link_error;
  QB_CHECK(create_directory_junction(pack_root(), outside_pack_root,
                                     pack_root_link_error));
  QB_CHECK(!pack_root_link_error);
  auto reparse_root = matrix.run_read("filesystem:reparse-pack-root", [&] {
    return call_op(selected, "ontology_get", {{"id", "escape"}});
  });
  require_operation_error(reparse_root, "pack_unsafe", "pack",
                          {"escape", "N20_OUTSIDE_PACK_ROOT",
                           "N20_OUTSIDE_ROOT_RAW_SENTINEL",
                           qbrain::util::path_to_utf8(outside_pack_root)});
  QB_CHECK(qbrain::util::sha256_hex(read_file(outside_pack)) == outside_pack_hash);
  QB_CHECK(fs::last_write_time(outside_pack) == outside_pack_time);
  QB_CHECK(fs::remove(pack_root()));

  const auto outside_qbrain_root = test_root / "N20_OUTSIDE_QBRAIN_ROOT";
  fs::create_directories(outside_qbrain_root / "schema-packs");
  const auto outside_ancestor_pack =
      outside_qbrain_root / "schema-packs" / "escape.json";
  const std::string outside_ancestor_bytes =
      R"({"id":"escape","name":"N20_OUTSIDE_ANCESTOR_RAW_SENTINEL","types":["note"],"dimensions":[]})";
  write_file(outside_ancestor_pack, outside_ancestor_bytes);
  const auto outside_ancestor_hash =
      qbrain::util::sha256_hex(read_file(outside_ancestor_pack));
  const auto outside_ancestor_time = fs::last_write_time(outside_ancestor_pack);
  fs::remove_all(qbrain_root);
  fs::create_directories(qbrain_root.parent_path());
  std::error_code qbrain_root_link_error;
  QB_CHECK(create_directory_junction(qbrain_root, outside_qbrain_root,
                                     qbrain_root_link_error));
  QB_CHECK(!qbrain_root_link_error);
  auto reparse_ancestor = matrix.run_read("filesystem:reparse-qbrain-root", [&] {
    return call_op(selected, "ontology_get", {{"id", "escape"}});
  });
  require_operation_error(reparse_ancestor, "pack_unsafe", "pack",
                          {"escape", "N20_OUTSIDE_QBRAIN_ROOT",
                           "N20_OUTSIDE_ANCESTOR_RAW_SENTINEL",
                           qbrain::util::path_to_utf8(outside_qbrain_root)});
  QB_CHECK(qbrain::util::sha256_hex(read_file(outside_ancestor_pack)) ==
           outside_ancestor_hash);
  QB_CHECK(fs::last_write_time(outside_ancestor_pack) == outside_ancestor_time);
  QB_CHECK(fs::remove(qbrain_root));
  fs::create_directories(qbrain_root);

  auto exact_manifest = make_manifest("exact").dump();
  QB_CHECK(exact_manifest.size() < kPackMaximumBytes);
  exact_manifest.append(kPackMaximumBytes - exact_manifest.size(), ' ');
  write_pack_bytes("exact", exact_manifest);
  auto exact_size = matrix.run_read("filesystem:size:1048576", [&] {
    return call_op(selected, "ontology_get", {{"id", "exact"}});
  });
  const auto exact_payload = require_success(exact_size);
  require_pack_shape(exact_payload, "exact", "installed", {"topic"});

  write_pack_bytes("oversized", std::string(kPackMaximumBytes + 1, ' '));
  auto oversized = matrix.run_read("filesystem:size:1048577", [&] {
    return call_op(selected, "ontology_get", {{"id", "oversized"}});
  });
  require_operation_error(oversized, "pack_too_large", "pack",
                          {"oversized", qbrain::util::path_to_utf8(pack_root())});

#ifdef _WIN32
  // N30: an exclusive share-mode handle is not a deterministic read blocker on
  // every Windows build (verified on 22624 where the open succeeds anyway); a
  // directory at the pack path is deterministically unreadable instead.
  fs::create_directory(pack_root() / "locked.json");
  auto locked = matrix.run_read("filesystem:unreadable-locked", [&] {
    return call_op(selected, "ontology_get", {{"id", "locked"}});
  });
  require_operation_error(locked, "pack_unsafe", "pack", {"locked", "schema-packs"});
#endif

  fs::remove_all(pack_root());
  fs::create_directories(pack_root());
  write_pack_bytes("zinvalid", "{");
  write_pack_bytes("aoversized", std::string(kPackMaximumBytes + 1, ' '));
  std::string deterministic_error;
  for (int attempt = 0; attempt < 3; ++attempt) {
    auto rejected = matrix.run_read("filesystem:deterministic-first-error", [&] {
      return call_op(selected, "list_schema_packs");
    });
    require_operation_error(rejected, "pack_too_large", "pack",
                            {"aoversized", "zinvalid",
                             qbrain::util::path_to_utf8(pack_root())});
    if (deterministic_error.empty())
      deterministic_error = rejected.json;
    else
      QB_CHECK(rejected.json == deterministic_error);
  }

  fs::remove_all(pack_root());
  fs::create_directories(pack_root());
  std::error_code case_sensitivity_error;
  QB_CHECK(enable_directory_case_sensitivity(pack_root(), case_sensitivity_error));
  QB_CHECK(!case_sensitivity_error);
  write_file(pack_root() / "foo.json", make_manifest("foo").dump());
  write_file(pack_root() / "FOO.json", make_manifest("foo").dump());
  std::set<std::string> case_collision_names;
  for (const auto& entry : fs::directory_iterator(pack_root())) {
    case_collision_names.insert(qbrain::util::path_to_utf8(entry.path().filename()));
  }
  QB_CHECK(case_collision_names == std::set<std::string>({"FOO.json", "foo.json"}));
  set_config(selected, "schema.active_pack", "foo");
  auto case_collision = matrix.run_read("filesystem:case-collision", [&] {
    const auto listed = call_op(selected, "list_schema_packs");
    const auto named = call_op(selected, "ontology_get", {{"id", "foo"}});
    const auto active = call_op(selected, "get_active_schema_pack");
    const auto reloaded = call_op(selected, "reload_schema_pack", {{"id", "foo"}}, false,
                                  true);
    for (const auto* result : {&listed, &named, &active, &reloaded}) {
      require_operation_error(*result, "pack_invalid", "pack",
                              {"FOO.json", "foo.json", qbrain::util::path_to_utf8(pack_root())});
    }
    QB_CHECK(listed.json == named.json);
    QB_CHECK(named.json == active.json);
    QB_CHECK(active.json == reloaded.json);
    return listed;
  });
  QB_CHECK(!case_collision.ok);
  QB_CHECK(get_config(selected, "schema.active_pack") == "foo");
  set_config(selected, "schema.active_pack", "default");

  fs::remove_all(pack_root());
  fs::create_directories(pack_root());
  write_file(pack_root() / "bad!.json", make_manifest("bad").dump());
  auto invalid_stem = matrix.run_read("filesystem:invalid-stem", [&] {
    return call_op(selected, "list_schema_packs");
  });
  require_operation_error(invalid_stem, "pack_invalid", "pack",
                          {"bad!.json", qbrain::util::path_to_utf8(pack_root())});

  fs::remove_all(pack_root());
  fs::create_directories(pack_root());
  write_pack_bytes("malformed", "{");
  auto malformed_list = matrix.run_read("filesystem:malformed-list", [&] {
    return call_op(selected, "list_schema_packs");
  });
  require_operation_error(malformed_list, "pack_invalid", "pack",
                          {"malformed", qbrain::util::path_to_utf8(pack_root())});

  fs::remove_all(pack_root());
  fs::create_directories(pack_root());
  write_file(pack_root() / "ignored.JSON", "N20_IGNORED_SENTINEL");
  write_file(pack_root() / "unrelated.txt", "N20_UNRELATED_SENTINEL");
  auto unrelated = matrix.run_read("filesystem:unrelated-files-ignored", [&] {
    return call_op(selected, "list_schema_packs");
  });
  QB_CHECK(require_success(unrelated)["active_id"] == "default");

  auto uppercase_manifest = make_manifest("upper");
  uppercase_manifest["name"] = "N20_UPPER_LOOKUP_SENTINEL";
  write_pack("Upper", uppercase_manifest);
  auto uppercase_named_lookup = matrix.run_read("filesystem:noncanonical-named-lookup", [&] {
    return call_op(selected, "ontology_get", {{"id", "upper"}});
  });
  require_operation_error(uppercase_named_lookup, "pack_invalid", "pack",
                          {"Upper", "N20_UPPER_LOOKUP_SENTINEL",
                           qbrain::util::path_to_utf8(pack_root())});
  auto noncanonical_name = matrix.run_read("filesystem:noncanonical-filename", [&] {
    return call_op(selected, "list_schema_packs");
  });
  require_operation_error(noncanonical_name, "pack_invalid", "pack",
                          {"Upper", qbrain::util::path_to_utf8(pack_root())});
  fs::remove(pack_root() / "Upper.json");

  fs::remove_all(pack_root());
  fs::create_directories(pack_root());
  write_file(pack_root() / "upper.JSON", make_manifest("upper").dump());
  auto uppercase_extension_lookup = matrix.run_read(
      "filesystem:noncanonical-extension-named-lookup", [&] {
        return call_op(selected, "ontology_get", {{"id", "upper"}});
  });
  require_operation_error(uppercase_extension_lookup, "pack_invalid", "pack",
                          {"upper.JSON", qbrain::util::path_to_utf8(pack_root())});
  fs::remove(pack_root() / "upper.JSON");

  write_pack_bytes("Default", make_manifest("default").dump());
  const auto require_default_case_error = [&](const std::string& label,
                                               auto&& operation) {
    auto result = matrix.run_read(label, std::forward<decltype(operation)>(operation));
    require_operation_error(result, "pack_invalid", "pack",
                            {"Default", qbrain::util::path_to_utf8(pack_root())});
  };
  require_default_case_error("filesystem:noncanonical-default:list", [&] {
    return call_op(selected, "list_schema_packs");
  });
  require_default_case_error("filesystem:noncanonical-default:active", [&] {
    return call_op(selected, "get_active_schema_pack");
  });
  require_default_case_error("filesystem:noncanonical-default:ontology-omitted", [&] {
    return call_op(selected, "ontology_get");
  });
  require_default_case_error("filesystem:noncanonical-default:ontology-named", [&] {
    return call_op(selected, "ontology_get", {{"id", "default"}});
  });
  require_default_case_error("filesystem:noncanonical-default:dimensions-omitted", [&] {
    return call_op(selected, "ontology_dimensions");
  });
  require_default_case_error("filesystem:noncanonical-default:dimensions-named", [&] {
    return call_op(selected, "ontology_dimensions", {{"id", "default"}});
  });
  require_default_case_error("filesystem:noncanonical-default:reload", [&] {
    return call_op(selected, "reload_schema_pack", {{"id", "default"}}, false, true);
  });
  fs::remove(pack_root() / "Default.json");

  QB_CHECK(logical_snapshot(decoy) == decoy_before);
}

void exercise_enumeration_bounds(qbrain::Brain& selected, qbrain::Brain& decoy,
                                 const fs::path& test_root) {
  const auto limit_localappdata = test_root / "limit-localappdata";
  fs::remove_all(limit_localappdata);
  ScopedEnvironmentVariable environment(
      "LOCALAPPDATA", qbrain::util::path_to_utf8(limit_localappdata));
  SnapshotMatrix matrix(selected, decoy, limit_localappdata);
  set_config(selected, "schema.active_pack", "default");

  for (int index = 0; index < kMaximumPacks - 1; ++index) {
    std::ostringstream id;
    id << 'p' << std::setw(3) << std::setfill('0') << index;
    write_pack(id.str(), make_manifest(id.str()));
  }
  auto exact_count = matrix.run_read("enumeration:pack-count:256", [&] {
    return call_op(selected, "list_schema_packs");
  });
  const auto exact_payload = require_success(exact_count);
  QB_CHECK(exact_payload["packs"].size() == kMaximumPacks);
  QB_CHECK(exact_payload["packs"].front()["id"] == "default");
  QB_CHECK(exact_payload["packs"].back()["id"] == "p254");

  write_pack("p255", make_manifest("p255"));
  auto too_many_packs = matrix.run_read("enumeration:pack-count:257", [&] {
    return call_op(selected, "list_schema_packs");
  });
  require_operation_error(too_many_packs, "pack_limit_exceeded", "pack",
                          {qbrain::util::path_to_utf8(pack_root())});

  fs::remove_all(pack_root());
  fs::create_directories(pack_root());
  for (int index = 0; index < kMaximumDirectoryEntries; ++index) {
    write_file(pack_root() / ("entry-" + std::to_string(index) + ".txt"), "");
  }
  auto exact_entries = matrix.run_read("enumeration:entry-count:4096", [&] {
    return call_op(selected, "list_schema_packs");
  });
  QB_CHECK(require_success(exact_entries)["packs"].size() == 1);

  write_file(pack_root() / "entry-4096.txt", "");
  auto too_many_entries = matrix.run_read("enumeration:entry-count:4097", [&] {
    return call_op(selected, "list_schema_packs");
  });
  require_operation_error(too_many_entries, "pack_limit_exceeded", "pack",
                          {qbrain::util::path_to_utf8(pack_root())});

  fs::remove_all(limit_localappdata);
}

void exercise_invalid_active_state(qbrain::Brain& selected, qbrain::Brain& decoy,
                                   SnapshotMatrix& matrix) {
  const auto decoy_before = logical_snapshot(decoy);
  fs::remove_all(pack_root());
  write_pack("alpha", make_manifest("alpha"));

  set_config(selected, "schema.active_pack", "missing_active");
  auto missing = matrix.run_read("active:missing-no-fallback", [&] {
    return call_op(selected, "list_schema_packs");
  });
  require_operation_error(missing, "pack_not_found", "id", {"missing_active"});
  QB_CHECK(get_config(selected, "schema.active_pack") == "missing_active");

  set_config(selected, "schema.active_pack", "../N20_ACTIVE_PATH_SENTINEL");
  auto invalid = matrix.run_read("active:invalid-no-repair", [&] {
    return call_op(selected, "get_active_schema_pack");
  });
  require_operation_error(invalid, "invalid_pack_id", "id",
                          {"N20_ACTIVE_PATH_SENTINEL", ".."});
  QB_CHECK(get_config(selected, "schema.active_pack") ==
           "../N20_ACTIVE_PATH_SENTINEL");

  set_config(selected, "schema.active_pack", "default");
  QB_CHECK(logical_snapshot(decoy) == decoy_before);
}

void exercise_reload_contract(qbrain::Brain& selected, qbrain::Brain& decoy,
                              SnapshotMatrix& matrix, const fs::path& filesystem_root) {
  fs::remove_all(pack_root());
  write_pack("alpha", make_manifest("alpha", {"note"}, {"alpha_dim"}));
  write_pack("beta", make_manifest("beta", {"note"}, {"beta_dim"}));
  set_config(selected, "schema.active_pack", "alpha");
  set_config(decoy, "schema.active_pack", "alpha");
  set_config(selected, "embedding.model", "N20_MODEL_SENTINEL");
  set_config(selected, "embedding.base_url", "N20_BASE_URL_SENTINEL");
  set_config(selected, "chat.model", "N20_CHAT_MODEL_SENTINEL");
  set_config(selected, "n20.other_config", "N20_OTHER_CONFIG_SENTINEL");

  const std::string global_config =
      R"({"embedding":{"provider":"N20_PROVIDER_SENTINEL","model":"N20_FILE_MODEL_SENTINEL","base_url":"N20_FILE_BASE_SENTINEL"},"chat":{"model":"N20_FILE_CHAT_SENTINEL"},"n20":"N20_GLOBAL_CONFIG_SENTINEL"})";
  write_file(qbrain::util::config_path(), global_config);

  auto denied = matrix.run_read("reload:remote-default-deny", [&] {
    return call_op(selected, "reload_schema_pack",
                   {{"id", "N20_DENIED_PACK_SENTINEL"}}, true, false);
  });
  require_operation_error(denied, "write_denied", "operation",
                          {"N20_DENIED_PACK_SENTINEL",
                           qbrain::util::path_to_utf8(pack_root())});
  QB_CHECK(get_config(selected, "schema.active_pack") == "alpha");

  const auto selected_before = logical_snapshot(selected);
  const auto selected_without_before = logical_snapshot_without_active_pack(selected);
  const auto decoy_before = logical_snapshot(decoy);
  const auto files_before = filesystem_snapshot(filesystem_root);
  // N30 D3: remote + allow-write no longer authorizes writes; the local
  // operator path (remote=false) is the documented reload workflow and the
  // remote deny is covered by reload:remote-default-deny above.
  auto changed = call_op(selected, "reload_schema_pack", {{"id", "BeTa"}}, false, false);
  const auto changed_payload = require_success(changed);
  require_keys(changed_payload, {"id", "changed"});
  QB_CHECK(changed_payload == json({{"id", "beta"}, {"changed", true}}));
  const auto selected_after = logical_snapshot(selected);
  const auto selected_without_after = logical_snapshot_without_active_pack(selected);
  const auto decoy_after = logical_snapshot(decoy);
  const auto files_after = filesystem_snapshot(filesystem_root);

  QB_CHECK(selected_before != selected_after);
  QB_CHECK(selected_without_before == selected_without_after);
  QB_CHECK(decoy_before == decoy_after);
  QB_CHECK(files_before == files_after);
  QB_CHECK(get_config(selected, "schema.active_pack") == "beta");
  QB_CHECK(get_config(decoy, "schema.active_pack") == "alpha");
  QB_CHECK(get_config(selected, "embedding.model") == "N20_MODEL_SENTINEL");
  QB_CHECK(get_config(selected, "embedding.base_url") == "N20_BASE_URL_SENTINEL");
  QB_CHECK(get_config(selected, "chat.model") == "N20_CHAT_MODEL_SENTINEL");
  QB_CHECK(get_config(selected, "n20.other_config") == "N20_OTHER_CONFIG_SENTINEL");
  QB_CHECK(read_file(qbrain::util::config_path()) == global_config);
  g_reload_evidence.push_back(
      {"reload:success:remote-allow-db-only", snapshot_sha256(selected_before),
       snapshot_sha256(selected_after), snapshot_sha256(selected_without_before),
       snapshot_sha256(selected_without_after), snapshot_sha256(decoy_before),
       snapshot_sha256(decoy_after), snapshot_sha256(files_before),
       snapshot_sha256(files_after), "alpha", "beta"});

  auto unchanged = matrix.run_read("reload:same-id:no-op", [&] {
    return call_op(selected, "reload_schema_pack", {{"id", "BETA"}}, false, true);
  });
  QB_CHECK(require_success(unchanged) ==
           json({{"id", "beta"}, {"changed", false}}));

  auto omitted = matrix.run_read("reload:omitted-revalidate-no-op", [&] {
    return call_op(selected, "reload_schema_pack", {}, false, true);
  });
  QB_CHECK(require_success(omitted) ==
           json({{"id", "beta"}, {"changed", false}}));

  const std::vector<std::pair<std::string, std::string>> invalid = {
      {"", "invalid_pack_id"}, {"../outside", "invalid_pack_id"},
      {"missing", "pack_not_found"}};
  for (const auto& [id, code] : invalid) {
    auto rejected = matrix.run_read("reload:failure:no-delta", [&] {
      return call_op(selected, "reload_schema_pack", {{"id", id}}, false, true);
    });
    require_operation_error(rejected, code, "id",
                            {id, qbrain::util::path_to_utf8(pack_root())});
    QB_CHECK(get_config(selected, "schema.active_pack") == "beta");
  }

  write_pack_bytes("broken", "N20_RAW_MANIFEST_SENTINEL");
  auto broken = matrix.run_read("reload:invalid-manifest:no-delta", [&] {
    return call_op(selected, "reload_schema_pack", {{"id", "broken"}}, false, true);
  });
  require_operation_error(broken, "pack_invalid", "pack",
                          {"broken", "N20_RAW_MANIFEST_SENTINEL"});
  QB_CHECK(get_config(selected, "schema.active_pack") == "beta");

  write_pack_bytes("oversized_reload", std::string(kPackMaximumBytes + 1, ' '));
  auto oversized = matrix.run_read("reload:oversized:no-delta", [&] {
    return call_op(selected, "reload_schema_pack", {{"id", "oversized_reload"}}, false, true);
  });
  require_operation_error(oversized, "pack_too_large", "pack",
                          {"oversized_reload", qbrain::util::path_to_utf8(pack_root())});
  QB_CHECK(get_config(selected, "schema.active_pack") == "beta");

  const auto reload_outside = filesystem_root / "N20_RELOAD_OUTSIDE_SENTINEL";
  const auto reload_outside_file = reload_outside / "unsafe_reload.json";
  const std::string reload_outside_bytes =
      R"({"id":"unsafe_reload","name":"N20_RELOAD_OUTSIDE_RAW_SENTINEL","types":["note"],"dimensions":[]})";
  write_file(reload_outside_file, reload_outside_bytes);
  const auto reload_outside_hash =
      qbrain::util::sha256_hex(read_file(reload_outside_file));
  const auto reload_outside_time = fs::last_write_time(reload_outside_file);
  std::error_code reload_link_error;
  QB_CHECK(create_directory_junction(pack_root() / "unsafe_reload.json", reload_outside,
                                     reload_link_error));
  QB_CHECK(!reload_link_error);
  auto unsafe = matrix.run_read("reload:unsafe:no-delta", [&] {
    return call_op(selected, "reload_schema_pack", {{"id", "unsafe_reload"}}, false, true);
  });
  require_operation_error(unsafe, "pack_unsafe", "pack",
                          {"unsafe_reload", "N20_RELOAD_OUTSIDE_RAW_SENTINEL",
                           qbrain::util::path_to_utf8(reload_outside),
                           qbrain::util::path_to_utf8(reload_outside_file)});
  QB_CHECK(get_config(selected, "schema.active_pack") == "beta");
  QB_CHECK(qbrain::util::sha256_hex(read_file(reload_outside_file)) ==
           reload_outside_hash);
  QB_CHECK(fs::last_write_time(reload_outside_file) == reload_outside_time);
  QB_CHECK(fs::remove(pack_root() / "unsafe_reload.json"));
  fs::remove(pack_root() / "broken.json");
  fs::remove(pack_root() / "oversized_reload.json");
}

void seed_stats_fixture(qbrain::Brain& selected, qbrain::Brain& decoy) {
  QB_CHECK(selected.ensure_source("team_a"));
  QB_CHECK(selected.ensure_source("not_allowed"));
  QB_CHECK(decoy.ensure_source("team_a"));

  seed_page(selected, "default", "default/note-a", "note");
  seed_page(selected, "default", "default/note-b", "note");
  seed_page(selected, "default", "default/note-c", "note");
  seed_page(selected, "default", "default/Alpha-a", "Alpha");
  seed_page(selected, "default", "default/Alpha-b", "Alpha");
  seed_page(selected, "default", "default/alpha-a", "alpha");
  seed_page(selected, "default", "default/alpha-b", "alpha");
  for (int index = 0; index < 8; ++index) {
    seed_page(selected, "default", "default/deleted-" + std::to_string(index),
              "deleted_stronger", true);
  }
  for (int index = 0; index < 12; ++index) {
    seed_page(selected, "team_a", "team/major-" + std::to_string(index), "major");
  }
  for (int index = 0; index < 258; ++index) {
    std::ostringstream type;
    type << "type" << std::setw(3) << std::setfill('0') << index;
    seed_page(selected, "team_a", "team/" + type.str(), type.str());
  }
  for (int index = 0; index < 20; ++index) {
    seed_page(selected, "not_allowed", "denied/" + std::to_string(index),
              "N20_DENIED_TYPE_SENTINEL");
  }

  for (int index = 0; index < 30; ++index) {
    seed_page(decoy, "default", "decoy/default-" + std::to_string(index),
              "N20_DECOY_DEFAULT_SENTINEL");
  }
  for (int index = 0; index < 40; ++index) {
    seed_page(decoy, "team_a", "decoy/team-" + std::to_string(index),
              "N20_DECOY_TEAM_SENTINEL");
  }
  seed_page(decoy, "team_a", "decoy/deleted", "N20_DECOY_DELETED_SENTINEL", true);
}

void exercise_stats_exact_matrix(qbrain::Brain& selected, qbrain::Brain& decoy,
                                 SnapshotMatrix& matrix) {
  const struct Cell {
    qbrain::Brain* brain;
    std::string label;
    std::string source;
    int limit;
  } cells[] = {
      {&selected, "stats:cell:selected:default", "default", 100},
      {&selected, "stats:cell:selected:team", "team_a", 256},
      {&decoy, "stats:cell:decoy:default", "default", 100},
      {&decoy, "stats:cell:decoy:team", "team_a", 100},
  };

  for (const auto& cell : cells) {
    const auto expected = direct_stats(*cell.brain, cell.source, cell.limit);
    std::unordered_map<std::string, std::string> arguments = {
        {"source_id", cell.source}, {"limit", std::to_string(cell.limit)}};
    auto result = matrix.run_read(cell.label, [&] {
      return call_op(*cell.brain, "schema_stats", arguments);
    });
    require_stats_shape(require_success(result), expected);
  }

  auto omitted = matrix.run_read("stats:source-omitted-limit-omitted", [&] {
    return call_op(selected, "schema_stats");
  });
  require_stats_shape(require_success(omitted), direct_stats(selected, "default", 100));

  auto mixed_case = matrix.run_read("stats:local-mixed-case-source", [&] {
    return call_op(selected, "schema_stats", {{"source_id", "TeAm_A"}, {"limit", "1"}});
  });
  require_stats_shape(require_success(mixed_case), direct_stats(selected, "team_a", 1));

  for (const auto& [text, effective] :
       std::vector<std::pair<std::string, int>>{{"0", 1}, {"1", 1}, {"100", 100},
                                                {"256", 256}, {"999", 256}}) {
    auto result = matrix.run_read("stats:limit:valid-clamped", [&] {
      return call_op(selected, "schema_stats", {{"source_id", "team_a"},
                                                  {"limit", text}});
    });
    require_stats_shape(require_success(result), direct_stats(selected, "team_a", effective));
  }

  const std::vector<std::string> invalid_limits = {
      "", "+1", "-1", " 1", "1 ", "1.0", "1x", "18446744073709551616"};
  for (const auto& limit : invalid_limits) {
    auto result = matrix.run_read("stats:limit:reject-before-query", [&] {
      DatabaseReadObserver observer(selected);
      auto rejected = call_op(selected, "schema_stats", {{"limit", limit}});
      QB_CHECK(observer.page_reads() == 0);
      return rejected;
    });
    require_operation_error(result, "invalid_argument", "limit", {limit});
  }

  const std::vector<std::pair<std::string, std::string>> invalid_sources = {
      {"", "invalid_source"},
      {"bad/source", "invalid_source"},
      {"CON", "invalid_source"},
      {std::string(65, 's'), "invalid_source"},
      {"unknown_n20", "source_not_found"},
  };
  for (const auto& [source, code] : invalid_sources) {
    auto result = matrix.run_read("stats:source:reject-before-query", [&] {
      DatabaseReadObserver observer(selected);
      auto rejected = call_op(selected, "schema_stats", {{"source_id", source}});
      QB_CHECK(observer.page_reads() == 0);
      return rejected;
    });
    require_operation_error(result, code, "source_id", {source});
  }

  auto remote_default = matrix.run_read("stats:remote-default", [&] {
    return call_op(selected, "schema_stats", {}, true, false);
  });
  require_stats_shape(require_success(remote_default), direct_stats(selected, "default", 100));

  auto remote_denied = matrix.run_read("stats:remote-denied", [&] {
    DatabaseReadObserver observer(selected);
    auto result = call_op(selected, "schema_stats", {{"source_id", "team_a"}}, true, false);
    QB_CHECK(observer.page_reads() == 0);
    return result;
  });
  require_operation_error(remote_denied, "source_not_allowed", "source_id");

  auto allow_write_does_not_authorize =
      matrix.run_read("stats:remote-allow-write-still-denied", [&] {
        DatabaseReadObserver observer(selected);
        auto result = call_op(selected, "schema_stats", {{"source_id", "not_allowed"}},
                              true, true);
        QB_CHECK(observer.page_reads() == 0);
        return result;
      });
  require_operation_error(allow_write_does_not_authorize, "source_not_allowed",
                          "source_id", {"N20_DENIED_TYPE_SENTINEL"});

  set_config(selected, "mcp.allowed_sources", "TeAm_A");
  auto remote_allowed = matrix.run_read("stats:remote-allowlisted", [&] {
    return call_op(selected, "schema_stats", {{"source_id", "TEAM_A"}, {"limit", "256"}},
                   true, false);
  });
  require_stats_shape(require_success(remote_allowed), direct_stats(selected, "team_a", 256));

  QB_CHECK(qbrain::test_support::scalar(
               selected, "SELECT COUNT(*) FROM sources WHERE id='unknown_n20'") == 0);
}

void exercise_damaged_stats(const fs::path& database_path, qbrain::Brain& decoy,
                            const fs::path& filesystem_root) {
  qbrain::Brain damaged("n20-damaged");
  damaged.open_at(qbrain::util::path_to_utf8(database_path));
  set_config(damaged, "schema.active_pack", "beta");
  std::string invalid_type = "bad-";
  invalid_type.push_back(static_cast<char>(0xff));
  seed_page(damaged, "default", "damaged/utf8", invalid_type);
  SnapshotMatrix invalid_utf8_matrix(damaged, decoy, filesystem_root);
  auto invalid_utf8_result = invalid_utf8_matrix.run_read("stats:damaged-type:utf8", [&] {
    return call_op(damaged, "schema_stats");
  });
  require_operation_error(invalid_utf8_result, "database_error", "database",
                          {invalid_type, damaged.db_path()});
  damaged.close();

  qbrain::Brain malformed_sentinel("n20-malformed-stats-sentinel");
  malformed_sentinel.open_at(
      qbrain::util::path_to_utf8(database_path.string() + ".malformed-sentinel"));
  set_config(malformed_sentinel, "schema.active_pack", "beta");
  seed_page(malformed_sentinel, "default", "sentinel/valid-a", "valid");
  seed_page(malformed_sentinel, "default", "sentinel/valid-b", "valid");
  seed_page(malformed_sentinel, "default", "sentinel/malformed", invalid_type);
  {
    auto groups = malformed_sentinel.db().prepare(
        "SELECT type, COUNT(*) AS page_count, typeof(type) FROM pages "
        "WHERE source_id='default' AND deleted_at IS NULL "
        "GROUP BY type ORDER BY page_count DESC, type COLLATE BINARY ASC LIMIT 2");
    QB_CHECK(groups.step());
    QB_CHECK(groups.column_text(0) == "valid");
    QB_CHECK(groups.column_int(1) == 2);
    QB_CHECK(groups.column_text(2) == "text");
    QB_CHECK(groups.step());
    QB_CHECK(groups.column_text(0) == invalid_type);
    QB_CHECK(groups.column_int(1) == 1);
    QB_CHECK(groups.column_text(2) == "text");
    QB_CHECK(!groups.step());
  }
  SnapshotMatrix malformed_sentinel_matrix(malformed_sentinel, decoy,
                                            filesystem_root);
  auto malformed_sentinel_result = malformed_sentinel_matrix.run_read(
      "stats:damaged-type:limit-plus-one", [&] {
        return call_op(malformed_sentinel, "schema_stats", {{"limit", "1"}});
      });
  require_operation_error(malformed_sentinel_result, "database_error", "database",
                          {invalid_type, malformed_sentinel.db_path()});
  malformed_sentinel.close();

  qbrain::Brain oversized("n20-oversized-type");
  oversized.open_at(qbrain::util::path_to_utf8(database_path.string() + ".oversized"));
  set_config(oversized, "schema.active_pack", "beta");
  seed_page(oversized, "default", "damaged/oversized", std::string(257, 't'));
  SnapshotMatrix oversized_matrix(oversized, decoy, filesystem_root);
  auto oversized_result = oversized_matrix.run_read("stats:damaged-type:257", [&] {
    return call_op(oversized, "schema_stats");
  });
  require_operation_error(oversized_result, "database_error", "database",
                          {std::string(257, 't'), oversized.db_path()});
  oversized.close();

  qbrain::Brain missing_table("n20-missing-config");
  missing_table.open_at(qbrain::util::path_to_utf8(database_path.string() + ".missing"));
  set_config(missing_table, "schema.active_pack", "beta");
  missing_table.db().exec("DROP TABLE config");
  SnapshotMatrix missing_matrix(missing_table, decoy, filesystem_root);
  auto missing_result = missing_matrix.run_read("stats:damaged-database:missing-config", [&] {
    return call_op(missing_table, "schema_stats");
  });
  require_operation_error(missing_result, "database_error", "database",
                          {missing_table.db_path(), "missing-config"});
  QB_CHECK(qbrain::test_support::scalar(
               missing_table,
               "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='config'") == 0);
  missing_table.close();

  qbrain::Brain missing_pages("n20-missing-pages");
  missing_pages.open_at(qbrain::util::path_to_utf8(database_path.string() + ".missing-pages"));
  set_config(missing_pages, "schema.active_pack", "beta");
  missing_pages.db().exec("PRAGMA foreign_keys=OFF; DROP TABLE pages; PRAGMA foreign_keys=ON;");
  SnapshotMatrix missing_pages_matrix(missing_pages, decoy, filesystem_root);
  auto missing_pages_result = missing_pages_matrix.run_read_with_selected_snapshot(
      "stats:damaged-database:missing-pages", serialized_database_snapshot,
      [&] { return call_op(missing_pages, "schema_stats"); });
  require_operation_error(missing_pages_result, "database_error", "database",
                          {missing_pages.db_path(), "missing-pages"});
  QB_CHECK(qbrain::test_support::scalar(
               missing_pages,
               "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='pages'") == 0);
  missing_pages.close();

  qbrain::Brain missing_schema("n20-missing-schema-version");
  missing_schema.open_at(
      qbrain::util::path_to_utf8(database_path.string() + ".missing-schema-version"));
  set_config(missing_schema, "schema.active_pack", "beta");
  missing_schema.db().exec("DROP TABLE schema_version");
  SnapshotMatrix missing_schema_matrix(missing_schema, decoy, filesystem_root);
  auto missing_schema_result = missing_schema_matrix.run_read(
      "stats:damaged-database:missing-schema-version", [&] {
        return call_op(missing_schema, "schema_stats");
      });
  require_operation_error(missing_schema_result, "database_error", "database",
                          {missing_schema.db_path(), "missing-schema-version"});
  QB_CHECK(qbrain::test_support::scalar(
               missing_schema,
               "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='schema_version'") == 0);
  missing_schema.close();
}

void require_identifier_schema(const json& schema, bool has_default) {
  QB_CHECK(schema["type"] == "string");
  QB_CHECK(schema["minLength"] == 1);
  QB_CHECK(schema["maxLength"] == 64);
  QB_CHECK(schema["pattern"] == "^[A-Za-z0-9_-]+$");
  if (has_default)
    QB_CHECK(schema["default"] == "default");
  else
    QB_CHECK(!schema.contains("default"));
}

void exercise_registry_contract(qbrain::Brain& selected, qbrain::Brain& decoy,
                                SnapshotMatrix& matrix) {
  const auto decoy_before = logical_snapshot(decoy);
  struct ExpectedOperation {
    std::string name;
    qbrain::ops::Scope scope;
    bool local_only;
    std::set<std::string> properties;
  };
  const std::vector<ExpectedOperation> expected = {
      {"list_schema_packs", qbrain::ops::Scope::Read, false, {}},
      {"get_active_schema_pack", qbrain::ops::Scope::Read, false, {}},
      {"reload_schema_pack", qbrain::ops::Scope::Write, true, {"id"}},
      {"schema_stats", qbrain::ops::Scope::Read, false, {"source_id", "limit"}},
      {"ontology_get", qbrain::ops::Scope::Read, false, {"id"}},
      {"ontology_dimensions", qbrain::ops::Scope::Read, false, {"id"}},
  };

  auto tools_response = matrix.run_read("registry:tools-list", [&] {
    auto response = json::parse(qbrain::mcp::handle_rpc_body(
        selected, {}, R"({"jsonrpc":"2.0","id":2000,"method":"tools/list","params":{}})"));
    for (const auto& item : expected) {
      auto rejected = call_op(selected, item.name,
                              {{"N20_LOCAL_UNEXPECTED_SENTINEL", "value"}}, false, true);
      require_operation_error(rejected, "invalid_argument",
                              "N20_LOCAL_UNEXPECTED_SENTINEL", {"value"});

      const std::string hostile_field =
          "N20_LOCAL_HOSTILE_FIELD_SENTINEL_" + std::string(1024, 'x');
      auto hostile = call_op(selected, item.name,
                             {{hostile_field, "N20_LOCAL_HOSTILE_VALUE_SENTINEL"}},
                             false, true);
      require_operation_error(hostile, "invalid_argument", "argument",
                              {hostile_field, "N20_LOCAL_HOSTILE_FIELD_SENTINEL",
                               "N20_LOCAL_HOSTILE_VALUE_SENTINEL"});
    }
    return response;
  });
  const auto& tools = tools_response["result"]["tools"];

  for (const auto& item : expected) {
    const auto* operation = qbrain::ops::global_registry().find(item.name);
    QB_CHECK(operation != nullptr);
    QB_CHECK(operation->scope == item.scope);
    QB_CHECK(operation->local_only == item.local_only);
    QB_CHECK(!operation->description.empty() && operation->description.size() <= 512);
    const auto lower_description = lowercase(operation->description);
    QB_CHECK(lower_description.find("schema") != std::string::npos ||
             lower_description.find("pack") != std::string::npos ||
             lower_description.find("ontology") != std::string::npos);
    for (const auto& overclaim : {"full parity", "cache invalidation", "entity ontology",
                                  "bi-temporal", "semantic inference"}) {
      QB_CHECK(lower_description.find(overclaim) == std::string::npos);
    }

    const auto schema = json::parse(operation->input_schema_json);
    require_keys(schema, {"type", "additionalProperties", "properties"});
    QB_CHECK(schema["type"] == "object");
    QB_CHECK(schema["additionalProperties"] == false);
    QB_CHECK(schema["properties"].is_object());
    std::set<std::string> properties;
    for (auto it = schema["properties"].begin(); it != schema["properties"].end(); ++it)
      properties.insert(it.key());
    QB_CHECK(properties == item.properties);

    if (properties.contains("id")) require_identifier_schema(schema["properties"]["id"], false);
    if (properties.contains("source_id"))
      require_identifier_schema(schema["properties"]["source_id"], true);
    if (properties.contains("limit")) {
      const auto& limit = schema["properties"]["limit"];
      require_keys(limit, {"type", "minimum", "maximum", "default"});
      QB_CHECK(limit["type"] == "integer");
      QB_CHECK(limit["minimum"] == 0);
      QB_CHECK(limit["maximum"] == 256);
      QB_CHECK(limit["default"] == 100);
    }

    const auto& tool = find_tool(tools, item.name);
    QB_CHECK(tool["description"] == operation->description);
    QB_CHECK(tool["inputSchema"] == schema);
  }
  QB_CHECK(logical_snapshot(decoy) == decoy_before);
}

void require_no_sensitive_output(const std::string& output,
                                 const fs::path& filesystem_root) {
  std::vector<std::string> forbidden = {
      qbrain::util::path_to_utf8(filesystem_root),
      qbrain::util::path_to_utf8(pack_root()),
      "N20_MODEL_SENTINEL",
      "N20_BASE_URL_SENTINEL",
      "N20_CHAT_MODEL_SENTINEL",
      "N20_PROVIDER_SENTINEL",
      "N20_GLOBAL_CONFIG_SENTINEL",
      "N20_RAW_MANIFEST_SENTINEL",
      "N20_DECOY_DEFAULT_SENTINEL",
      "N20_DECOY_TEAM_SENTINEL",
      "N20_DENIED_TYPE_SENTINEL",
      "Administrator",
      "\\\\?\\Volume",
  };
  if (const char* username = std::getenv("USERNAME"); username && std::strlen(username) >= 3)
    forbidden.emplace_back(username);
  for (const auto& value : forbidden) {
    if (!value.empty()) QB_CHECK(output.find(value) == std::string::npos);
  }
}

void exercise_mcp_contract(qbrain::Brain& selected, qbrain::Brain& decoy,
                           SnapshotMatrix& matrix, const fs::path& filesystem_root) {
  const auto decoy_before = logical_snapshot(decoy);
  qbrain::mcp::ServeOptions no_writes;
  no_writes.allow_write = false;
  qbrain::mcp::ServeOptions writes;
  writes.allow_write = true;
  int request_id = 2100;

  const std::vector<std::pair<std::string, json>> read_calls = {
      {"list_schema_packs", json::object()},
      {"get_active_schema_pack", json::object()},
      {"schema_stats", json::object()},
      {"ontology_get", json::object()},
      {"ontology_dimensions", json::object()},
  };
  for (const auto& [operation, arguments] : read_calls) {
    auto responses = matrix.run_read("mcp:read-success:" + operation, [&] {
      auto mcp = mcp_call(selected, no_writes, operation, arguments, request_id++);
      auto local = call_op(selected, operation);
      return std::pair<json, qbrain::ops::OpResult>{std::move(mcp), std::move(local)};
    });
    const auto& response = responses.first;
    const auto payload = require_mcp_success(response);
    const auto local = require_success(responses.second);
    QB_CHECK(payload == local);
    QB_CHECK(response["result"]["content"].size() == 1);
    require_no_sensitive_output(response.dump(), filesystem_root);
  }

  auto denied_reload = matrix.run_read("mcp:reload-default-deny", [&] {
    return mcp_call(selected, no_writes, "reload_schema_pack",
                    json{{"id", "alpha"}}, request_id++);
  });
  require_mcp_error(denied_reload, "write_denied", "operation",
                    {"alpha", qbrain::util::path_to_utf8(pack_root())});

  auto allowed_noop = matrix.run_read("mcp:reload-explicit-allow-noop", [&] {
    return mcp_call(selected, writes, "reload_schema_pack", json{{"id", "BETA"}},
                    request_id++);
  });
  QB_CHECK(require_mcp_success(allowed_noop) ==
           json({{"id", "beta"}, {"changed", false}}));

  const std::array<std::string, 6> operations = {
      "list_schema_packs", "get_active_schema_pack", "reload_schema_pack",
      "schema_stats", "ontology_get", "ontology_dimensions"};
  for (const auto& operation : operations) {
    auto non_object = matrix.run_read("mcp:arguments:non-object:" + operation, [&] {
      DatabaseReadObserver observer(selected);
      auto result = mcp_call(selected, no_writes, operation, json::array(), request_id++);
      QB_CHECK(observer.application_reads() == 0);
      return result;
    });
    require_mcp_error(non_object, "invalid_argument", "arguments");

    auto unexpected = matrix.run_read("mcp:arguments:unexpected:" + operation, [&] {
      DatabaseReadObserver observer(selected);
      auto result = mcp_call(selected, no_writes, operation,
                             json{{"N20_UNEXPECTED_FIELD_SENTINEL", "value"}}, request_id++);
      QB_CHECK(observer.application_reads() == 0);
      return result;
    });
    require_mcp_error(unexpected, "invalid_argument", "N20_UNEXPECTED_FIELD_SENTINEL",
                      {"value"});

    const std::string hostile_field =
        "N20_MCP_HOSTILE_FIELD_SENTINEL_" + std::string(1024, 'x');
    auto hostile = matrix.run_read("mcp:arguments:overlong-field:" + operation, [&] {
      DatabaseReadObserver observer(selected);
      auto result = mcp_call(selected, no_writes, operation,
                             json{{hostile_field, "N20_HOSTILE_VALUE_SENTINEL"}},
                             request_id++);
      QB_CHECK(observer.application_reads() == 0);
      return result;
    });
    require_mcp_error(hostile, "invalid_argument", "argument",
                      {hostile_field, "N20_MCP_HOSTILE_FIELD_SENTINEL",
                       "N20_HOSTILE_VALUE_SENTINEL"});
  }

  const std::vector<json> invalid_strings = {
      nullptr, true, 7, 1.5, json::array(), json::object()};
  for (const auto& operation : {"reload_schema_pack", "ontology_get",
                                "ontology_dimensions"}) {
    for (const auto& value : invalid_strings) {
      auto rejected = matrix.run_read(std::string("mcp:id-type:") + operation, [&] {
        DatabaseReadObserver observer(selected);
        auto result = mcp_call(selected, no_writes, operation, json{{"id", value}}, request_id++);
        QB_CHECK(observer.application_reads() == 0);
        return result;
      });
      require_mcp_error(rejected, "invalid_argument", "id");
    }
  }

  for (const auto& value : invalid_strings) {
    auto rejected = matrix.run_read("mcp:source-type:schema-stats", [&] {
      DatabaseReadObserver observer(selected);
      auto result = mcp_call(selected, no_writes, "schema_stats",
                             json{{"source_id", value}}, request_id++);
      QB_CHECK(observer.application_reads() == 0);
      return result;
    });
    require_mcp_error(rejected, "invalid_argument", "source_id");
  }

  const std::vector<json> invalid_mcp_limits = {
      -1, 1.5, true, nullptr, "1", json::array(), json::object()};
  for (const auto& value : invalid_mcp_limits) {
    auto rejected = matrix.run_read("mcp:limit-type:schema-stats", [&] {
      DatabaseReadObserver observer(selected);
      auto result = mcp_call(selected, no_writes, "schema_stats", json{{"limit", value}},
                             request_id++);
      QB_CHECK(observer.application_reads() == 0);
      return result;
    });
    require_mcp_error(rejected, "invalid_argument", "limit");
  }

  auto invalid_id = matrix.run_read("mcp:id-value:redacted", [&] {
    return mcp_call(selected, no_writes, "ontology_get",
                    json{{"id", "../N20_MCP_PATH_SENTINEL"}}, request_id++);
  });
  require_mcp_error(invalid_id, "invalid_pack_id", "id",
                    {"N20_MCP_PATH_SENTINEL", qbrain::util::path_to_utf8(pack_root())});

  set_config(selected, "mcp.allowed_sources", "");
  auto denied_source = matrix.run_read("mcp:stats:source-denied", [&] {
    return mcp_call(selected, no_writes, "schema_stats",
                    json{{"source_id", "team_a"}}, request_id++);
  });
  require_mcp_error(denied_source, "source_not_allowed", "source_id",
                    {"N20_DECOY_TEAM_SENTINEL"});

  auto write_does_not_authorize = matrix.run_read("mcp:stats:allow-write-denied", [&] {
    return mcp_call(selected, writes, "schema_stats",
                    json{{"source_id", "not_allowed"}}, request_id++);
  });
  require_mcp_error(write_does_not_authorize, "source_not_allowed", "source_id",
                    {"N20_DENIED_TYPE_SENTINEL"});

  set_config(selected, "mcp.allowed_sources", "TEAM_A");
  auto allowed_source = matrix.run_read("mcp:stats:source-allowlisted", [&] {
    return mcp_call(selected, no_writes, "schema_stats",
                    json{{"source_id", "TeAm_A"}, {"limit", 1}}, request_id++);
  });
  require_stats_shape(require_mcp_success(allowed_source), direct_stats(selected, "team_a", 1));

  {
    ScopedEnvironmentVariable ambient("QBRAIN_SOURCE", "team_a");
    for (const auto& [operation, arguments] : read_calls) {
      auto response = matrix.run_read("mcp:ambient-excluded:" + operation, [&] {
        return mcp_call(selected, no_writes, operation, arguments, request_id++);
      });
      const auto payload = require_mcp_success(response);
      if (operation == "schema_stats") QB_CHECK(payload["source_id"] == "default");
    }
    auto reload = matrix.run_read("mcp:ambient-excluded:reload", [&] {
      return mcp_call(selected, writes, "reload_schema_pack", json::object(), request_id++);
    });
    QB_CHECK(require_mcp_success(reload) ==
             json({{"id", "beta"}, {"changed", false}}));
  }

  QB_CHECK(logical_snapshot(decoy) == decoy_before);
}

void exercise_populated_reopen(qbrain::Brain& selected, qbrain::Brain& decoy,
                               SnapshotMatrix& matrix) {
  const auto selected_path = selected.db_path();
  const auto decoy_path = decoy.db_path();
  const auto selected_before = logical_snapshot(selected);
  const auto decoy_before = logical_snapshot(decoy);
  const auto selected_integrity_before = qbrain::storage::check_schema_integrity(selected.db());
  const auto decoy_integrity_before = qbrain::storage::check_schema_integrity(decoy.db());
  QB_CHECK(selected_integrity_before.ok && selected_integrity_before.schema_version == 12);
  QB_CHECK(decoy_integrity_before.ok && decoy_integrity_before.schema_version == 12);

  const auto list_before = require_success(call_op(selected, "list_schema_packs"));
  const auto active_before = require_success(call_op(selected, "get_active_schema_pack"));
  const auto reload_before =
      require_success(call_op(selected, "reload_schema_pack", {}, false, true));
  const auto stats_before = require_success(call_op(selected, "schema_stats"));
  const auto ontology_before =
      require_success(call_op(selected, "ontology_get", {{"id", "beta"}}));
  const auto dimensions_before =
      require_success(call_op(selected, "ontology_dimensions", {{"id", "beta"}}));
  QB_CHECK(list_before["active_id"] == "beta");
  QB_CHECK(active_before["id"] == "beta");
  QB_CHECK(reload_before == json({{"id", "beta"}, {"changed", false}}));

  selected.close();
  decoy.close();
  selected.open_at(selected_path);
  decoy.open_at(decoy_path);

  const auto selected_integrity_after = qbrain::storage::check_schema_integrity(selected.db());
  const auto decoy_integrity_after = qbrain::storage::check_schema_integrity(decoy.db());
  QB_CHECK(selected_integrity_after.ok && selected_integrity_after.schema_version == 12);
  QB_CHECK(decoy_integrity_after.ok && decoy_integrity_after.schema_version == 12);
  QB_CHECK(selected_integrity_after.schema_version == selected_integrity_before.schema_version);
  QB_CHECK(decoy_integrity_after.schema_version == decoy_integrity_before.schema_version);
  QB_CHECK(logical_snapshot(selected) == selected_before);
  QB_CHECK(logical_snapshot(decoy) == decoy_before);

  auto list_after = matrix.run_read("reopen:populated:list", [&] {
    return call_op(selected, "list_schema_packs");
  });
  QB_CHECK(require_success(list_after) == list_before);
  auto active_after = matrix.run_read("reopen:populated:active", [&] {
    return call_op(selected, "get_active_schema_pack");
  });
  QB_CHECK(require_success(active_after) == active_before);
  auto reload_after = matrix.run_read("reopen:populated:reload", [&] {
    return call_op(selected, "reload_schema_pack", {}, false, true);
  });
  QB_CHECK(require_success(reload_after) == reload_before);
  auto stats_after = matrix.run_read("reopen:populated:stats", [&] {
    return call_op(selected, "schema_stats");
  });
  QB_CHECK(require_success(stats_after) == stats_before);
  auto ontology_after = matrix.run_read("reopen:populated:ontology", [&] {
    return call_op(selected, "ontology_get", {{"id", "beta"}});
  });
  QB_CHECK(require_success(ontology_after) == ontology_before);
  auto dimensions_after = matrix.run_read("reopen:populated:dimensions", [&] {
    return call_op(selected, "ontology_dimensions", {{"id", "beta"}});
  });
  QB_CHECK(require_success(dimensions_after) == dimensions_before);

  auto decoy_active = matrix.run_read("reopen:populated:decoy-isolation", [&] {
    return call_op(decoy, "get_active_schema_pack");
  });
  QB_CHECK(require_success(decoy_active)["id"] == "alpha");
}

void run_test_n20_impl() {
  ScopedTestRoot scoped_test_root;
  const auto& test_root = scoped_test_root.path();
  const auto isolated_localappdata = test_root / "localappdata";
  ScopedEnvironmentVariable local_app_data(
      "LOCALAPPDATA", qbrain::util::path_to_utf8(isolated_localappdata));

  g_snapshot_evidence.clear();
  g_reload_evidence.clear();
  qbrain::ops::register_builtin_ops();

  qbrain::Brain selected("n20-selected");
  selected.open_at(qbrain::util::path_to_utf8(test_root / "selected.db"));
  qbrain::Brain decoy("n20-decoy");
  decoy.open_at(qbrain::util::path_to_utf8(test_root / "decoy.db"));
  const auto selected_integrity = qbrain::storage::check_schema_integrity(selected.db());
  const auto decoy_integrity = qbrain::storage::check_schema_integrity(decoy.db());
  QB_CHECK(selected_integrity.ok && selected_integrity.schema_version == 12);
  QB_CHECK(decoy_integrity.ok && decoy_integrity.schema_version == 12);

  SnapshotMatrix matrix(selected, decoy, isolated_localappdata);
  exercise_builtin_no_create(selected, decoy, matrix, isolated_localappdata);
  exercise_pack_id_contract(selected, decoy, matrix);
  exercise_listing_and_exact_shapes(selected, decoy, matrix);
  exercise_manifest_contract(selected, decoy, matrix);
  exercise_filesystem_safety(selected, decoy, matrix, test_root);
  exercise_enumeration_bounds(selected, decoy, test_root);
  exercise_invalid_active_state(selected, decoy, matrix);
  exercise_reload_contract(selected, decoy, matrix, isolated_localappdata);

  seed_stats_fixture(selected, decoy);
  exercise_stats_exact_matrix(selected, decoy, matrix);
  exercise_registry_contract(selected, decoy, matrix);
  exercise_mcp_contract(selected, decoy, matrix, isolated_localappdata);
  exercise_populated_reopen(selected, decoy, matrix);
  exercise_damaged_stats(test_root / "damaged.db", decoy, isolated_localappdata);

  QB_CHECK(!g_snapshot_evidence.empty());
  QB_CHECK(g_reload_evidence.size() == 1);
  std::cout << "[INFO] n20 schema_v12=pass builtin_no_create=pass pack_id_matrix=pass "
               "listing_shapes=pass path_confinement=pass filesystem_bounds=pass "
               "manifest_matrix=pass exact_shapes=pass selected_decoy=pass "
               "schema_stats=pass source_authorization=pass reload_delta=pass "
               "reload_gate=pass registry=pass mcp_typed=pass mcp_rpc=pass "
               "ambient_excluded=pass error_redaction=pass snapshots=pass "
               "filename_case=pass root_reparse=pass manifest_types=pass "
               "deterministic_listing=pass "
               "reload_failure_delta=pass populated_reopen=pass "
               "mcp_single_error_block=pass unique_root=pass "
               "snapshot_call_count="
            << g_snapshot_evidence.size() << " reload_delta_count=" << g_reload_evidence.size()
            << "\n";
  for (const auto& evidence : g_snapshot_evidence) {
    std::cout << "[INFO] n20 snapshot_call=" << evidence.index << " label=" << evidence.label
              << " selected_before_sha256=" << evidence.selected_before
              << " selected_after_sha256=" << evidence.selected_after
              << " decoy_before_sha256=" << evidence.decoy_before
              << " decoy_after_sha256=" << evidence.decoy_after
              << " filesystem_before_sha256=" << evidence.filesystem_before
              << " filesystem_after_sha256=" << evidence.filesystem_after << "\n";
  }
  for (const auto& evidence : g_reload_evidence) {
    std::cout << "[INFO] n20 reload_delta label=" << evidence.label
              << " selected_before_sha256=" << evidence.selected_before
              << " selected_after_sha256=" << evidence.selected_after
              << " selected_without_active_before_sha256=" << evidence.selected_without_active_before
              << " selected_without_active_after_sha256=" << evidence.selected_without_active_after
              << " decoy_before_sha256=" << evidence.decoy_before
              << " decoy_after_sha256=" << evidence.decoy_after
              << " filesystem_before_sha256=" << evidence.filesystem_before
              << " filesystem_after_sha256=" << evidence.filesystem_after
              << " old_id=" << evidence.old_id << " new_id=" << evidence.new_id << "\n";
  }

  decoy.close();
  selected.close();
}

}  // namespace

void run_test_n20() { run_test_n20_impl(); }

void test_n20() { run_test_n20(); }
