#include "qbrain/schema/packs.hpp"

#include "qbrain/storage/database.hpp"
#include "qbrain/util/paths.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

namespace fs = std::filesystem;

namespace qbrain::schema {
namespace {

constexpr std::size_t kMaximumPackIdBytes = 64;
constexpr std::size_t kMaximumPackFileBytes = 1024 * 1024;
constexpr std::size_t kMaximumDirectoryEntries = 4096;
constexpr std::size_t kMaximumPackCount = 256;
constexpr std::size_t kMaximumNameBytes = 256;
constexpr std::size_t kMaximumVersionBytes = 64;
constexpr std::size_t kMaximumIdentifierCount = 256;
constexpr std::size_t kMaximumPhaseCount = 64;
constexpr std::size_t kMaximumPhaseBytes = 64;
constexpr std::size_t kMaximumStoredTypeBytes = 256;

[[noreturn]] void fail(std::string code, std::string field, std::string message) {
  throw PackError(std::move(code), std::move(field), std::move(message));
}

[[noreturn]] void invalid_pack_id() {
  fail("invalid_pack_id", "id", "invalid schema pack id");
}

[[noreturn]] void pack_not_found() {
  fail("pack_not_found", "id", "schema pack not found");
}

[[noreturn]] void pack_invalid() {
  fail("pack_invalid", "pack", "schema pack is invalid");
}

[[noreturn]] void pack_unsafe() {
  fail("pack_unsafe", "pack", "schema pack candidate is unsafe");
}

[[noreturn]] void pack_too_large() {
  fail("pack_too_large", "pack", "schema pack exceeds the size limit");
}

[[noreturn]] void pack_limit_exceeded() {
  fail("pack_limit_exceeded", "pack", "schema pack discovery limit exceeded");
}

[[noreturn]] void filesystem_error() {
  fail("filesystem_error", "pack", "schema pack storage is unavailable");
}

[[noreturn]] void database_error() {
  fail("database_error", "database", "schema pack database operation failed");
}

bool is_utf8_continuation(unsigned char byte) { return (byte & 0xC0u) == 0x80u; }

std::size_t utf8_code_point_length(std::string_view value, std::size_t offset) {
  const auto byte_at = [&](std::size_t index) {
    return static_cast<unsigned char>(value[index]);
  };
  const std::size_t remaining = value.size() - offset;
  const unsigned char first = byte_at(offset);
  if (first <= 0x7Fu) return 1;
  if (first >= 0xC2u && first <= 0xDFu)
    return remaining >= 2 && is_utf8_continuation(byte_at(offset + 1)) ? 2 : 0;
  if (first == 0xE0u)
    return remaining >= 3 && byte_at(offset + 1) >= 0xA0u &&
                   byte_at(offset + 1) <= 0xBFu &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  if ((first >= 0xE1u && first <= 0xECu) ||
      (first >= 0xEEu && first <= 0xEFu))
    return remaining >= 3 && is_utf8_continuation(byte_at(offset + 1)) &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  if (first == 0xEDu)
    return remaining >= 3 && byte_at(offset + 1) >= 0x80u &&
                   byte_at(offset + 1) <= 0x9Fu &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  if (first == 0xF0u)
    return remaining >= 4 && byte_at(offset + 1) >= 0x90u &&
                   byte_at(offset + 1) <= 0xBFu &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  if (first >= 0xF1u && first <= 0xF3u)
    return remaining >= 4 && is_utf8_continuation(byte_at(offset + 1)) &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  if (first == 0xF4u)
    return remaining >= 4 && byte_at(offset + 1) >= 0x80u &&
                   byte_at(offset + 1) <= 0x8Fu &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  return 0;
}

bool is_valid_utf8(std::string_view value) {
  for (std::size_t offset = 0; offset < value.size();) {
    const std::size_t length = utf8_code_point_length(value, offset);
    if (length == 0) return false;
    offset += length;
  }
  return true;
}

bool is_reserved_device_name(std::string_view value) {
  static constexpr std::string_view names[] = {
      "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
      "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
      "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
  return std::find(std::begin(names), std::end(names), value) != std::end(names);
}

std::optional<std::string> canonical_identifier(std::string_view value) {
  if (value.empty() || value.size() > kMaximumPackIdBytes) return std::nullopt;
  std::string canonical;
  canonical.reserve(value.size());
  for (const unsigned char byte : value) {
    if (byte >= 'A' && byte <= 'Z') {
      canonical.push_back(static_cast<char>(byte - 'A' + 'a'));
    } else if ((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
               byte == '_' || byte == '-') {
      canonical.push_back(static_cast<char>(byte));
    } else {
      return std::nullopt;
    }
  }
  if (is_reserved_device_name(canonical)) return std::nullopt;
  return canonical;
}

const PackManifest& builtin_manifest() {
  static const PackManifest manifest{
      "default",
      "Qbrain default",
      std::nullopt,
      {"note", "timeline", "person", "concept"},
      {"topic", "entity", "time"},
      std::vector<std::string>{"orphans", "extract_facts", "consolidate", "embed",
                               "purge"}};
  return manifest;
}

fs::path pack_root_path() {
  try {
    std::error_code ec;
    auto root = fs::absolute(util::qbrain_root() / "schema-packs", ec);
    if (ec) filesystem_error();
    return root.lexically_normal();
  } catch (const PackError&) {
    throw;
  } catch (...) {
    filesystem_error();
  }
}

bool is_not_found_error(const std::error_code& ec) {
  return ec == std::errc::no_such_file_or_directory;
}

#ifdef _WIN32
bool is_not_found_error(DWORD error) {
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
         error == ERROR_NO_MORE_FILES;
}

class UniqueHandle {
 public:
  UniqueHandle() = default;
  explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
  ~UniqueHandle() {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
  }
  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;
  UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = INVALID_HANDLE_VALUE;
  }
  UniqueHandle& operator=(UniqueHandle&& other) noexcept {
    if (this == &other) return *this;
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    handle_ = other.handle_;
    other.handle_ = INVALID_HANDLE_VALUE;
    return *this;
  }
  HANDLE get() const { return handle_; }
  bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

struct FileIdentity {
  DWORD volume_serial_number = 0;
  DWORD file_index_high = 0;
  DWORD file_index_low = 0;
};

bool operator==(const FileIdentity& left, const FileIdentity& right) {
  return left.volume_serial_number == right.volume_serial_number &&
         left.file_index_high == right.file_index_high &&
         left.file_index_low == right.file_index_low;
}

FileIdentity file_identity(HANDLE handle) {
  BY_HANDLE_FILE_INFORMATION information{};
  if (!GetFileInformationByHandle(handle, &information)) filesystem_error();
  return {information.dwVolumeSerialNumber, information.nFileIndexHigh,
          information.nFileIndexLow};
}

fs::path final_handle_path(HANDLE handle) {
  constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
  const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
  if (required == 0) filesystem_error();
  std::wstring value(static_cast<std::size_t>(required), L'\0');
  const DWORD written =
      GetFinalPathNameByHandleW(handle, value.data(), required, flags);
  if (written == 0 || written >= required) filesystem_error();
  value.resize(static_cast<std::size_t>(written));
  return fs::path(std::move(value)).lexically_normal();
}

UniqueHandle open_safe_directory(const fs::path& path, bool& missing) {
  missing = false;
  UniqueHandle directory(CreateFileW(
      path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!directory.valid()) {
    const DWORD error = GetLastError();
    if (is_not_found_error(error)) {
      missing = true;
      return {};
    }
    filesystem_error();
  }

  FILE_ATTRIBUTE_TAG_INFO attributes{};
  if (!GetFileInformationByHandleEx(directory.get(), FileAttributeTagInfo, &attributes,
                                    sizeof(attributes)))
    filesystem_error();
  if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    pack_unsafe();
  return directory;
}
#endif

struct PackRoot {
  fs::path path;
  bool exists = false;
#ifdef _WIN32
  UniqueHandle qbrain_directory;
  UniqueHandle pack_directory;
  FileIdentity qbrain_identity;
  FileIdentity pack_identity;
  fs::path final_qbrain_path;
  fs::path final_pack_path;
#endif
};

PackRoot inspect_pack_root() {
  PackRoot root;
  root.path = pack_root_path();
#ifdef _WIN32
  bool missing = false;
  root.qbrain_directory = open_safe_directory(root.path.parent_path(), missing);
  if (missing) return root;
  root.qbrain_identity = file_identity(root.qbrain_directory.get());
  root.final_qbrain_path = final_handle_path(root.qbrain_directory.get());

  root.pack_directory = open_safe_directory(root.path, missing);
  if (missing) return root;
  root.pack_identity = file_identity(root.pack_directory.get());
  root.final_pack_path = final_handle_path(root.pack_directory.get());
  if (root.final_pack_path.parent_path() != root.final_qbrain_path) pack_unsafe();
  root.exists = true;
#else
  const auto qbrain_path = root.path.parent_path();
  std::error_code ec;
  const auto qbrain_status = fs::symlink_status(qbrain_path, ec);
  if (ec) {
    if (is_not_found_error(ec)) return root;
    filesystem_error();
  }
  if (!fs::exists(qbrain_status)) return root;
  if (fs::is_symlink(qbrain_status) || !fs::is_directory(qbrain_status)) pack_unsafe();

  const auto status = fs::symlink_status(root.path, ec);
  if (ec) {
    if (is_not_found_error(ec)) return root;
    filesystem_error();
  }
  if (!fs::exists(status)) return root;
  if (fs::is_symlink(status) || !fs::is_directory(status)) pack_unsafe();
  root.exists = true;
#endif
  return root;
}

void verify_pack_root_binding(const PackRoot& root) {
  if (!root.exists) pack_unsafe();
#ifdef _WIN32
  bool missing = false;
  auto qbrain_directory = open_safe_directory(root.path.parent_path(), missing);
  if (missing || file_identity(qbrain_directory.get()) != root.qbrain_identity ||
      final_handle_path(qbrain_directory.get()) != root.final_qbrain_path)
    pack_unsafe();

  auto pack_directory = open_safe_directory(root.path, missing);
  if (missing || file_identity(pack_directory.get()) != root.pack_identity)
    pack_unsafe();
  const auto final_pack_path = final_handle_path(pack_directory.get());
  if (final_pack_path != root.final_pack_path ||
      final_pack_path.parent_path() != root.final_qbrain_path)
    pack_unsafe();
#else
  std::error_code ec;
  const auto status = fs::symlink_status(root.path, ec);
  if (ec || fs::is_symlink(status) || !fs::is_directory(status)) pack_unsafe();
#endif
}

fs::path resolve_direct_child(const fs::path& root, std::string_view canonical_id) {
  const fs::path expected(std::string(canonical_id) + ".json");
  const auto child = (root / expected).lexically_normal();
  if (child.parent_path() != root || child.filename() != expected) pack_unsafe();
  return child;
}

void verify_direct_child(const PackRoot& root, const fs::path& candidate,
                         std::string_view canonical_id) {
  const auto expected = resolve_direct_child(root.path, canonical_id);
  if (candidate.lexically_normal() != expected) pack_unsafe();
}

std::optional<std::string> ascii_component(const fs::path& value) {
  std::string result;
#ifdef _WIN32
  const auto& native = value.native();
  result.reserve(native.size());
  for (const wchar_t unit : native) {
    if (unit < 0 || unit > 0x7F) return std::nullopt;
    result.push_back(static_cast<char>(unit));
  }
#else
  const auto& native = value.native();
  result.reserve(native.size());
  for (const unsigned char unit : native) {
    if (unit > 0x7F) return std::nullopt;
    result.push_back(static_cast<char>(unit));
  }
#endif
  return result;
}

std::vector<fs::path> sorted_directory_entries(const PackRoot& root);

std::optional<fs::path> find_exact_pack_candidate(const PackRoot& root,
                                                  std::string_view canonical_id) {
  verify_pack_root_binding(root);
  struct CandidateEntry {
    std::string id;
    fs::path path;
  };

  std::vector<CandidateEntry> candidates;
  std::set<std::string> seen_ids;
  for (const auto& entry_path : sorted_directory_entries(root)) {
    const auto filename = ascii_component(entry_path.filename());
    const auto stem = ascii_component(entry_path.stem());
    const auto extension = ascii_component(entry_path.extension());
    if (!filename || !stem || !extension || extension->size() != 5) continue;
    std::string folded_extension = *extension;
    for (char& byte : folded_extension) {
      if (byte >= 'A' && byte <= 'Z') byte = static_cast<char>(byte - 'A' + 'a');
    }
    if (folded_extension != ".json") continue;
    const auto canonical = canonical_identifier(*stem);
    if (!canonical) {
      if (*extension == ".json") pack_invalid();
      continue;
    }
    if (!seen_ids.insert(*canonical).second) pack_invalid();
    if (*filename != *canonical + ".json") {
      if (*canonical == canonical_id) pack_invalid();
      continue;
    }
    candidates.push_back({*canonical, entry_path});
  }

  for (const auto& candidate : candidates) {
    if (candidate.id == canonical_id) {
      verify_direct_child(root, candidate.path, canonical_id);
      verify_pack_root_binding(root);
      return candidate.path;
    }
  }
  return std::nullopt;
}

std::vector<fs::path> sorted_directory_entries(const PackRoot& root) {
  verify_pack_root_binding(root);
  std::vector<fs::path> entries;
  std::error_code ec;
  fs::directory_iterator iterator(root.path, fs::directory_options::none, ec);
  if (ec) filesystem_error();
  const fs::directory_iterator end;
  while (iterator != end) {
    if (entries.size() == kMaximumDirectoryEntries) pack_limit_exceeded();
    const auto entry_path = iterator->path().lexically_normal();
    if (entry_path.parent_path() != root.path) pack_unsafe();
    entries.push_back(entry_path);
    iterator.increment(ec);
    if (ec) filesystem_error();
  }
  verify_pack_root_binding(root);
  std::sort(entries.begin(), entries.end(), [](const fs::path& left,
                                                const fs::path& right) {
    const auto left_name = left.filename().native();
    const auto right_name = right.filename().native();
    return left_name < right_name;
  });
  return entries;
}

#ifdef _WIN32
void verify_open_candidate(const PackRoot& root, HANDLE file,
                           std::string_view canonical_id) {
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  if (!GetFileInformationByHandleEx(file, FileAttributeTagInfo, &attributes,
                                    sizeof(attributes)))
    filesystem_error();
  if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
      GetFileType(file) != FILE_TYPE_DISK)
    pack_unsafe();

  const auto final_candidate = final_handle_path(file);
  const fs::path expected_filename(std::string(canonical_id) + ".json");
  if (final_candidate.parent_path() != root.final_pack_path ||
      final_candidate.filename() != expected_filename)
    pack_unsafe();
  verify_pack_root_binding(root);
}
#endif

bool candidate_exists_and_is_safe(const PackRoot& root, const fs::path& candidate,
                                  std::string_view canonical_id) {
  verify_direct_child(root, candidate, canonical_id);
  verify_pack_root_binding(root);
#ifdef _WIN32
  UniqueHandle file(CreateFileW(
      candidate.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!file.valid()) {
    if (is_not_found_error(GetLastError())) return false;
    filesystem_error();
  }
  verify_open_candidate(root, file.get(), canonical_id);
  return true;
#else
  std::error_code ec;
  const auto status = fs::symlink_status(candidate, ec);
  if (ec) {
    if (is_not_found_error(ec)) return false;
    filesystem_error();
  }
  if (!fs::exists(status)) return false;
  if (fs::is_symlink(status) || !fs::is_regular_file(status)) pack_unsafe();
  return true;
#endif
}

std::string read_file_bounded(const PackRoot& root, const fs::path& candidate,
                              std::string_view canonical_id) {
  verify_direct_child(root, candidate, canonical_id);
  verify_pack_root_binding(root);
#ifdef _WIN32
  UniqueHandle file(CreateFileW(
      candidate.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!file.valid()) {
    if (is_not_found_error(GetLastError())) pack_not_found();
    filesystem_error();
  }
  verify_open_candidate(root, file.get(), canonical_id);

  FILE_STANDARD_INFO standard_info{};
  if (!GetFileInformationByHandleEx(file.get(), FileStandardInfo, &standard_info,
                                    sizeof(standard_info)))
    filesystem_error();
  if (standard_info.Directory) pack_unsafe();
  if (standard_info.EndOfFile.QuadPart < 0 ||
      static_cast<unsigned long long>(standard_info.EndOfFile.QuadPart) >
          kMaximumPackFileBytes)
    pack_too_large();

  std::string bytes;
  bytes.reserve(static_cast<std::size_t>(standard_info.EndOfFile.QuadPart));
  std::array<char, 64 * 1024> buffer{};
  while (bytes.size() <= kMaximumPackFileBytes) {
    const std::size_t remaining = kMaximumPackFileBytes + 1 - bytes.size();
    const DWORD requested = static_cast<DWORD>(std::min(buffer.size(), remaining));
    DWORD read = 0;
    if (!ReadFile(file.get(), buffer.data(), requested, &read, nullptr)) filesystem_error();
    if (read == 0) break;
    bytes.append(buffer.data(), static_cast<std::size_t>(read));
  }
  if (bytes.size() > kMaximumPackFileBytes) pack_too_large();
  verify_pack_root_binding(root);
  return bytes;
#else
  std::error_code ec;
  const auto status = fs::symlink_status(candidate, ec);
  if (ec) {
    if (is_not_found_error(ec)) pack_not_found();
    filesystem_error();
  }
  if (!fs::exists(status)) pack_not_found();
  if (fs::is_symlink(status) || !fs::is_regular_file(status)) pack_unsafe();
  const auto size = fs::file_size(candidate, ec);
  if (ec) filesystem_error();
  if (size > kMaximumPackFileBytes) pack_too_large();
  std::ifstream input(candidate, std::ios::binary);
  if (!input) filesystem_error();
  std::string bytes;
  bytes.reserve(static_cast<std::size_t>(size));
  std::array<char, 64 * 1024> buffer{};
  while (input && bytes.size() <= kMaximumPackFileBytes) {
    const auto remaining = kMaximumPackFileBytes + 1 - bytes.size();
    const auto requested = static_cast<std::streamsize>(std::min(buffer.size(), remaining));
    input.read(buffer.data(), requested);
    const auto read = input.gcount();
    if (read > 0) bytes.append(buffer.data(), static_cast<std::size_t>(read));
  }
  if (input.bad()) filesystem_error();
  if (bytes.size() > kMaximumPackFileBytes) pack_too_large();
  verify_pack_root_binding(root);
  return bytes;
#endif
}

void reject_excessive_json_nesting(std::string_view bytes) {
  std::size_t depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (const unsigned char byte : bytes) {
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (byte == '\\') {
        escaped = true;
      } else if (byte == '"') {
        in_string = false;
      }
      continue;
    }
    if (byte == '"') {
      in_string = true;
    } else if (byte == '{' || byte == '[') {
      ++depth;
      if (depth > 8) pack_invalid();
    } else if ((byte == '}' || byte == ']') && depth > 0) {
      --depth;
    }
  }
}

std::vector<std::string> validate_identifier_array(const json& value,
                                                   std::size_t minimum,
                                                   std::size_t maximum) {
  if (!value.is_array() || value.size() < minimum || value.size() > maximum) pack_invalid();
  std::set<std::string> seen;
  std::vector<std::string> result;
  result.reserve(value.size());
  for (const auto& member : value) {
    if (!member.is_string()) pack_invalid();
    auto canonical = canonical_identifier(member.get_ref<const std::string&>());
    if (!canonical || !seen.insert(*canonical).second) pack_invalid();
    result.push_back(std::move(*canonical));
  }
  return result;
}

std::optional<std::vector<std::string>> validate_phases(const json& document) {
  if (!document.contains("phases")) return std::nullopt;
  const auto& value = document.at("phases");
  if (!value.is_array() || value.size() > kMaximumPhaseCount) pack_invalid();
  std::set<std::string> seen;
  std::vector<std::string> result;
  result.reserve(value.size());
  for (const auto& member : value) {
    if (!member.is_string()) pack_invalid();
    const auto& phase = member.get_ref<const std::string&>();
    if (phase.empty() || phase.size() > kMaximumPhaseBytes || !is_valid_utf8(phase) ||
        !seen.insert(phase).second)
      pack_invalid();
    result.push_back(phase);
  }
  return result;
}

PackManifest parse_manifest(std::string_view bytes, std::string_view expected_id) {
  if (bytes.size() > kMaximumPackFileBytes) pack_too_large();
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEFu &&
      static_cast<unsigned char>(bytes[1]) == 0xBBu &&
      static_cast<unsigned char>(bytes[2]) == 0xBFu)
    pack_invalid();
  if (!is_valid_utf8(bytes)) pack_invalid();
  reject_excessive_json_nesting(bytes);

  json document;
  bool duplicate_root_key = false;
  std::set<std::string> root_keys;
  try {
    auto callback = [&](int depth, json::parse_event_t event, json& parsed) {
      if (event == json::parse_event_t::key && depth == 1) {
        const auto& key = parsed.get_ref<const std::string&>();
        if (!root_keys.insert(key).second) duplicate_root_key = true;
      }
      return true;
    };
    document = json::parse(bytes.begin(), bytes.end(), callback, true, false);
  } catch (...) {
    pack_invalid();
  }
  if (duplicate_root_key || !document.is_object()) pack_invalid();

  static const std::set<std::string> supported_keys = {
      "id", "name", "version", "types", "dimensions", "phases"};
  for (auto it = document.begin(); it != document.end(); ++it) {
    if (supported_keys.find(it.key()) == supported_keys.end()) pack_invalid();
  }
  if (!document.contains("id") || !document.at("id").is_string() ||
      !document.contains("name") || !document.at("name").is_string() ||
      !document.contains("types") || !document.contains("dimensions"))
    pack_invalid();

  const auto canonical_id =
      canonical_identifier(document.at("id").get_ref<const std::string&>());
  if (!canonical_id || *canonical_id != expected_id) pack_invalid();

  const auto& name = document.at("name").get_ref<const std::string&>();
  if (name.empty() || name.size() > kMaximumNameBytes || !is_valid_utf8(name)) pack_invalid();

  PackManifest manifest;
  manifest.id = *canonical_id;
  manifest.name = name;
  if (document.contains("version")) {
    if (!document.at("version").is_string()) pack_invalid();
    const auto& version = document.at("version").get_ref<const std::string&>();
    if (version.empty() || version.size() > kMaximumVersionBytes || !is_valid_utf8(version))
      pack_invalid();
    manifest.version = version;
  }
  manifest.types = validate_identifier_array(document.at("types"), 1,
                                               kMaximumIdentifierCount);
  manifest.dimensions = validate_identifier_array(document.at("dimensions"), 0,
                                                    kMaximumIdentifierCount);
  manifest.phases = validate_phases(document);
  return manifest;
}

LoadedPack load_installed_pack(const PackRoot& root, const fs::path& candidate,
                               const std::string& canonical_id) {
  if (!candidate_exists_and_is_safe(root, candidate, canonical_id)) pack_not_found();
  auto manifest =
      parse_manifest(read_file_bounded(root, candidate, canonical_id), canonical_id);
  return {canonical_id, "installed", std::move(manifest)};
}

LoadedPack load_named_pack(const std::string& canonical_id) {
  const auto root = inspect_pack_root();
  if (!root.exists) {
    if (canonical_id == "default") return {"default", "builtin", builtin_manifest()};
    pack_not_found();
  }
  const auto candidate = find_exact_pack_candidate(root, canonical_id);
  if (!candidate) {
    if (canonical_id == "default") return {"default", "builtin", builtin_manifest()};
    pack_not_found();
  }
  return load_installed_pack(root, *candidate, canonical_id);
}

std::string read_active_pack_id_unwrapped(Brain& brain) {
  auto statement = brain.db().prepare("SELECT value FROM config WHERE key='schema.active_pack'");
  if (!statement.step()) return "default";
  if (statement.column_is_null(0)) database_error();
  const auto configured = statement.column_text(0);
  if (configured.empty()) return "default";
  auto canonical = canonical_identifier(configured);
  if (!canonical) invalid_pack_id();
  return *canonical;
}

void rollback_noexcept(storage::Database& database) noexcept {
  try {
    database.exec("ROLLBACK;");
  } catch (...) {
  }
}

ordered_json manifest_value(const PackManifest& manifest) {
  ordered_json value = ordered_json::object();
  value["id"] = manifest.id;
  value["name"] = manifest.name;
  if (manifest.version) value["version"] = *manifest.version;
  value["types"] = manifest.types;
  value["dimensions"] = manifest.dimensions;
  if (manifest.phases) value["phases"] = *manifest.phases;
  return value;
}

void create_builtin_default_file(const PackRoot& root, const fs::path& candidate,
                                 std::string_view bytes) {
  verify_direct_child(root, candidate, "default");
  verify_pack_root_binding(root);
#ifdef _WIN32
  UniqueHandle file(CreateFileW(
      candidate.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
      CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!file.valid()) {
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) return;
    filesystem_error();
  }
  verify_open_candidate(root, file.get(), "default");
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
        bytes.size() - offset, std::numeric_limits<DWORD>::max()));
    DWORD written = 0;
    if (!WriteFile(file.get(), bytes.data() + offset, requested, &written, nullptr) ||
        written == 0)
      filesystem_error();
    offset += written;
  }
  verify_pack_root_binding(root);
#else
  std::ofstream output(candidate, std::ios::binary | std::ios::out);
  if (!output) filesystem_error();
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) filesystem_error();
#endif
}

// N30 D9: unique same-directory sibling used as the staged temp file for
// atomic pack replacement. The ".tmp-..." suffix keeps it outside the ".json"
// scan set, and pack writes are bounded by kMaximumPackFileBytes.
fs::path unique_temp_sibling(const fs::path& target) {
  static std::atomic<unsigned long long> sequence{0};
  const auto stamp = static_cast<unsigned long long>(
      std::chrono::steady_clock::now().time_since_epoch().count());
#ifdef _WIN32
  const unsigned long long pid = GetCurrentProcessId();
#else
  const unsigned long long pid = 0;
#endif
  const std::string suffix = ".tmp-" + std::to_string(pid) + "-" + std::to_string(stamp) +
                             "-" + std::to_string(sequence.fetch_add(1));
  return fs::path(target.native() + fs::path(suffix).native());
}

// Atomic same-volume replace: MOVEFILE_REPLACE_EXISTING on Windows (rename
// fallback), std::filesystem::rename (atomic on POSIX) elsewhere.
bool replace_file_atomically(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
  if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0) return true;
#endif
  std::error_code ec;
  fs::rename(from, to, ec);
  return !ec;
}

}  // namespace

PackError::PackError(std::string code, std::string field, std::string message)
    : std::runtime_error(std::move(message)),
      code_(std::move(code)),
      field_(std::move(field)) {}

std::optional<std::string> canonical_pack_id(std::string_view id) {
  return canonical_identifier(id);
}

std::string manifest_json(const PackManifest& manifest) {
  try {
    return manifest_value(manifest).dump();
  } catch (const PackError&) {
    throw;
  } catch (...) {
    pack_invalid();
  }
}

std::string active_pack_id(Brain& brain) {
  try {
    return read_active_pack_id_unwrapped(brain);
  } catch (const PackError&) {
    throw;
  } catch (...) {
    database_error();
  }
}

LoadedPack load_pack(Brain& brain, const std::optional<std::string>& id) {
  const std::string selected = [&] {
    if (!id) return active_pack_id(brain);
    auto canonical = canonical_identifier(*id);
    if (!canonical) invalid_pack_id();
    return *canonical;
  }();
  try {
    return load_named_pack(selected);
  } catch (const PackError&) {
    throw;
  } catch (...) {
    filesystem_error();
  }
}

std::vector<PackInfo> list_packs(Brain& brain) {
  const auto active = active_pack_id(brain);
  const auto root = inspect_pack_root();
  std::map<std::string, PackInfo> discovered;
  discovered.emplace("default", PackInfo{"default", "builtin", false});

  if (root.exists) {
    std::set<std::string> filesystem_ids;
    for (const auto& entry_path : sorted_directory_entries(root)) {
      if (entry_path.extension() == fs::path(".json")) {
        const auto filename = ascii_component(entry_path.filename());
        const auto stem = ascii_component(entry_path.stem());
        if (!filename || !stem) pack_invalid();
        const auto canonical = canonical_identifier(*stem);
        if (!canonical) pack_invalid();
        if (!filesystem_ids.insert(*canonical).second) pack_invalid();
        if (*filename != *canonical + ".json") pack_invalid();
        if (*canonical != "default" && discovered.size() >= kMaximumPackCount)
          pack_limit_exceeded();

        auto loaded = load_installed_pack(root, entry_path, *canonical);
        discovered[*canonical] = PackInfo{*canonical, loaded.origin, false};
        if (discovered.size() > kMaximumPackCount) pack_limit_exceeded();
      }
    }
  }

  auto active_entry = discovered.find(active);
  if (active_entry == discovered.end()) pack_not_found();
  active_entry->second.active = true;

  std::vector<PackInfo> result;
  result.reserve(discovered.size());
  for (auto& [_, pack] : discovered) result.push_back(std::move(pack));
  return result;
}

ReloadPackResult reload_pack(Brain& brain, const std::optional<std::string>& id) {
  const auto loaded = load_pack(brain, id);
  const auto current = active_pack_id(brain);
  if (current == loaded.id) return {loaded.id, false};

  auto& database = brain.db();
  bool in_transaction = false;
  try {
    database.exec("BEGIN IMMEDIATE;");
    in_transaction = true;
    const auto locked_current = read_active_pack_id_unwrapped(brain);
    if (locked_current == loaded.id) {
      database.exec("ROLLBACK;");
      in_transaction = false;
      return {loaded.id, false};
    }

    auto statement = database.prepare(
        "INSERT INTO config(key,value) VALUES('schema.active_pack',?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    statement.bind_text(1, loaded.id);
    statement.step_done();
    database.exec("COMMIT;");
    in_transaction = false;
    return {loaded.id, true};
  } catch (const PackError&) {
    if (in_transaction) rollback_noexcept(database);
    throw;
  } catch (...) {
    if (in_transaction) rollback_noexcept(database);
    database_error();
  }
}

SchemaStatsResult read_schema_stats(Brain& brain, const std::string& source_id, int limit) {
  if (limit < 1 || limit > 256)
    fail("invalid_argument", "limit", "schema statistics limit is out of range");
  const auto canonical_source = Brain::canonical_source_id(source_id);
  if (!canonical_source)
    fail("invalid_source", "source_id", "invalid source_id");

  try {
    if (!brain.source_exists(*canonical_source))
      fail("source_not_found", "source_id", "source_id is not registered");

    const auto active = load_pack(brain);
    const auto integrity = storage::check_schema_integrity(brain.db());
    if (!integrity.ok) database_error();

    SchemaStatsResult result;
    result.source_id = *canonical_source;
    result.active_pack_id = active.id;
    result.schema_version = integrity.schema_version;

    auto total = brain.db().prepare(
        "SELECT COUNT(*) FROM pages WHERE source_id=? AND deleted_at IS NULL");
    total.bind_text(1, *canonical_source);
    if (!total.step()) database_error();
    result.total_active_pages = total.column_int(0);
    if (result.total_active_pages < 0) database_error();

    // n38 (census: typeof guard + COLLATE BINARY): typeof() is SQLite-only;
    // the PG branch uses pg_typeof()::text, which yields the static type name
    // 'text' for a text column (PG columns cannot hold foreign storage
    // classes, so the corruption guard is equivalent). The per-expression
    // COLLATE BINARY moved to column level (pages.type, schema v1); SQLite's
    // default collation is BINARY, so the SQLite branch keeps identical
    // ordering, and the PG branch pins COLLATE "C" (byte order) explicitly.
    const bool pg_mode =
        brain.db().backend_kind() == storage::BackendKind::postgres;
    auto groups = brain.db().prepare(pg_mode
        ? "SELECT type, COUNT(*) AS page_count, pg_typeof(type)::text FROM pages "
          "WHERE source_id=? AND deleted_at IS NULL "
          "GROUP BY type ORDER BY page_count DESC, type COLLATE \"C\" ASC LIMIT ?"
        : "SELECT type, COUNT(*) AS page_count, typeof(type) FROM pages "
          "WHERE source_id=? AND deleted_at IS NULL "
          "GROUP BY type ORDER BY page_count DESC, type ASC LIMIT ?");
    groups.bind_text(1, *canonical_source);
    groups.bind_int(2, static_cast<int64_t>(limit) + 1);
    while (groups.step()) {
      if (groups.column_text(2) != "text") database_error();
      auto type = groups.column_text(0);
      const auto count = groups.column_int(1);
      if (type.size() > kMaximumStoredTypeBytes || !is_valid_utf8(type) || count <= 0)
        database_error();
      if (result.type_counts.size() == static_cast<std::size_t>(limit)) {
        result.truncated = true;
        break;
      }
      result.type_counts.push_back({std::move(type), count});
    }
    return result;
  } catch (const PackError&) {
    throw;
  } catch (...) {
    database_error();
  }
}

bool set_active_pack(Brain& brain, const std::string& id) {
  const std::optional<std::string> requested = id.empty()
                                                   ? std::optional<std::string>("default")
                                                   : std::optional<std::string>(id);
  (void)reload_pack(brain, requested);
  return true;
}

std::string load_pack_json(Brain& brain, const std::string& id) {
  const std::optional<std::string> requested =
      id.empty() ? std::nullopt : std::optional<std::string>(id);
  return manifest_json(load_pack(brain, requested).manifest);
}

void ensure_default_pack() {
  try {
    auto root = inspect_pack_root();
    if (!root.exists) {
      std::error_code ec;
      if (!fs::create_directories(root.path, ec) && ec) filesystem_error();
      root = inspect_pack_root();
      if (!root.exists) filesystem_error();
    }
    const auto candidate = find_exact_pack_candidate(root, "default");
    if (candidate) {
      (void)load_installed_pack(root, *candidate, "default");
      return;
    }
    const auto new_candidate = resolve_direct_child(root.path, "default");
    create_builtin_default_file(root, new_candidate, manifest_json(builtin_manifest()));
    const auto installed = find_exact_pack_candidate(root, "default");
    if (!installed) pack_not_found();
    (void)load_installed_pack(root, *installed, "default");
  } catch (const PackError&) {
    throw;
  } catch (...) {
    filesystem_error();
  }
}

std::string apply_mutations(Brain& brain, const std::string& mutations_json,
                            int* out_applied) {
  if (out_applied) *out_applied = 0;
  try {
    auto loaded = load_pack(brain);
    if (loaded.origin == "builtin") {
      ensure_default_pack();
      loaded = load_pack(brain, std::optional<std::string>(loaded.id));
    }

    json mutations;
    try {
      mutations = json::parse(mutations_json.empty() ? "[]" : mutations_json);
    } catch (...) {
      return "invalid mutations json";
    }
    if (!mutations.is_array()) return "mutations must be array";

    int applied = 0;
    for (const auto& mutation : mutations) {
      if (!mutation.is_object()) continue;
      const auto operation = mutation.value("op", "");
      std::vector<std::string>* target = nullptr;
      std::string raw;
      if (operation == "add_type") {
        if (!mutation.contains("type") || !mutation.at("type").is_string())
          return "invalid type";
        raw = mutation.at("type").get<std::string>();
        target = &loaded.manifest.types;
      } else if (operation == "add_dimension") {
        if (!mutation.contains("dimension") || !mutation.at("dimension").is_string())
          return "invalid dimension";
        raw = mutation.at("dimension").get<std::string>();
        target = &loaded.manifest.dimensions;
      } else {
        return "unsupported mutation op";
      }
      auto canonical = canonical_identifier(raw);
      if (!canonical) return "invalid schema identifier";
      if (std::find(target->begin(), target->end(), *canonical) == target->end()) {
        if (target->size() >= kMaximumIdentifierCount) return "schema pack limit exceeded";
        target->push_back(std::move(*canonical));
        ++applied;
      }
    }

    if (applied == 0) return {};
    const auto bytes = manifest_json(loaded.manifest);
    if (bytes.size() > kMaximumPackFileBytes) return "schema pack limit exceeded";
    const auto root = inspect_pack_root();
    if (!root.exists) return "schema pack storage unavailable";
    const auto candidate = find_exact_pack_candidate(root, loaded.id);
    if (!candidate || !candidate_exists_and_is_safe(root, *candidate, loaded.id))
      return "schema pack not found";
    const auto backup = fs::path(candidate->native() + fs::path(".bak").native());
    std::error_code ec;
    // A stale backup can itself carry the read-only attribute (CopyFileW
    // copies attributes), which would block every future mutation; clear and
    // remove it before taking the fresh backup.
    if (fs::exists(backup, ec)) {
      std::error_code clear_ec;
      fs::permissions(backup, fs::perms::owner_all, clear_ec);
      fs::remove(backup, clear_ec);
    }
    fs::copy_file(*candidate, backup, fs::copy_options::none, ec);
    if (ec) return "schema pack write failed";
    // N30 D9: crash-atomic replacement — write a bounded temp sibling, flush
    // and close it, verify it, then atomically replace the live pack. Every
    // failure path keeps the original file bytes (backup restore when needed),
    // so there is no partial-write window on the active pack.
    const auto temp = unique_temp_sibling(*candidate);
    {
      std::ofstream output(temp, std::ios::binary | std::ios::trunc);
      if (!output) return "schema pack write failed";
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      output.flush();
      if (!output) {
        output.close();
        std::error_code remove_ec;
        fs::remove(temp, remove_ec);
        return "schema pack write failed";
      }
    }
    std::error_code size_ec;
    const auto temp_size = fs::file_size(temp, size_ec);
    if (size_ec || temp_size != bytes.size()) {
      std::error_code remove_ec;
      fs::remove(temp, remove_ec);
      return "schema pack write failed";
    }
    if (!replace_file_atomically(temp, *candidate)) {
      std::error_code remove_ec;
      fs::remove(temp, remove_ec);
      std::error_code restore_ec;
      fs::copy_file(backup, *candidate, fs::copy_options::overwrite_existing, restore_ec);
      return "schema pack write failed";
    }
    std::error_code verify_ec;
    const auto replaced_size = fs::file_size(*candidate, verify_ec);
    if (verify_ec || replaced_size != bytes.size()) {
      std::error_code restore_ec;
      fs::copy_file(backup, *candidate, fs::copy_options::overwrite_existing, restore_ec);
      return "schema pack write failed";
    }
    if (out_applied) *out_applied = applied;
    return {};
  } catch (const PackError& error) {
    return error.code();
  } catch (...) {
    return "schema pack operation failed";
  }
}

}  // namespace qbrain::schema
