#include "qbrain/service/live_sync.hpp"
#include "qbrain/ingest/import.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/log.hpp"
#include "qbrain/util/paths.hpp"
#include "qbrain/util/string_util.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>
#include <unordered_map>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace qbrain::service {
namespace {

fs::path state_path_for(const Brain& brain, const std::string& source_id,
                        const std::string& notes_dir) {
  auto scope = brain.brain_id() + "\n" + source_id + "\n" + notes_dir;
  auto h = util::sha256_hex(scope).substr(0, 16);
  auto dir = util::qbrain_root() / "sync-state";
  util::ensure_dir(dir);
  return dir / (h + ".json");
}

std::unordered_map<std::string, std::string> load_state(const fs::path& p) {
  std::unordered_map<std::string, std::string> m;
  if (!fs::exists(p)) return m;
  try {
    std::ifstream in(p);
    json j;
    in >> j;
    if (j.is_object()) {
      for (auto it = j.begin(); it != j.end(); ++it) {
        m[it.key()] = it.value().get<std::string>();
      }
    }
  } catch (...) {
  }
  return m;
}

void save_state(const fs::path& p, const std::unordered_map<std::string, std::string>& m) {
  json j = json::object();
  for (auto& [k, v] : m) j[k] = v;
  std::ofstream out(p);
  out << j.dump(2);
}

std::string file_sig(const fs::path& f) {
  auto sz = fs::file_size(f);
  auto mt = fs::last_write_time(f).time_since_epoch().count();
  return std::to_string(static_cast<long long>(sz)) + ":" + std::to_string(mt);
}

bool is_note(const fs::path& p) {
  auto ext = util::to_lower(p.extension().string());
  return ext == ".md" || ext == ".txt" || ext == ".markdown";
}

// N5: reject paths that escape notes root via .. or absolute jump
bool path_under_root(const fs::path& file, const fs::path& root) {
  std::error_code ec;
  auto f = fs::weakly_canonical(file, ec);
  if (ec) f = fs::absolute(file).lexically_normal();
  auto r = fs::weakly_canonical(root, ec);
  if (ec) r = fs::absolute(root).lexically_normal();
  auto rel = f.lexically_relative(r);
  if (rel.empty() || rel.is_absolute()) return false;
  for (const auto& part : rel) {
    if (part == "..") return false;
  }
  return true;
}

}  // namespace

LiveSyncResult live_sync_once(Brain& brain, const std::string& notes_dir,
                              const std::string& source_id) {
  LiveSyncResult r;
  auto canon = Brain::canonical_source_id(source_id.empty() ? "default" : source_id);
  if (!canon) {
    util::warn("live_sync: invalid source_id");
    r.errors = 1;
    return r;
  }
  const auto requested_source = *canon;
  if (!brain.ensure_source(requested_source)) {
    util::warn("live_sync: source registration failed");
    r.errors = 1;
    return r;
  }
  fs::path root(notes_dir);
  std::error_code root_ec;
  if (!fs::exists(root, root_ec) || !fs::is_directory(root, root_ec)) {
    util::warn("live_sync: not a directory: " + notes_dir);
    r.errors = 1;
    try {
      brain.log_ingest("live_sync", notes_dir,
                       json({{"imported_pages", 0}, {"scanned", 0},
                             {"skipped", 0}, {"errors", 1}})
                           .dump(),
                       100, requested_source);
    } catch (...) {
    }
    return r;
  }
  auto root_abs = fs::weakly_canonical(root, root_ec);
  if (root_ec) {
    root_abs = fs::absolute(root).lexically_normal();
  }
  auto root_key = util::path_to_utf8(root_abs);
  auto sp = state_path_for(brain, requested_source, root_key);
  auto state = load_state(sp);
  std::unordered_map<std::string, std::string> next = state;

  std::error_code ec;
  for (auto& e : fs::recursive_directory_iterator(
           root_abs, fs::directory_options::skip_permission_denied, ec)) {
    if (!e.is_regular_file()) continue;
    if (!is_note(e.path())) continue;
    if (!path_under_root(e.path(), root_abs)) {
      util::warn("live_sync: skip path outside root: " + util::path_to_utf8(e.path()));
      ++r.errors;
      continue;
    }
    ++r.scanned;
    std::error_code key_ec;
    auto key_path = fs::weakly_canonical(e.path(), key_ec);
    if (key_ec) {
      ++r.errors;
      continue;
    }
    auto key = util::path_to_utf8(key_path);
    std::string sig;
    try {
      sig = file_sig(e.path());
    } catch (...) {
      ++r.errors;
      continue;
    }
    auto it = state.find(key);
    if (it != state.end() && it->second == sig) {
      ++r.skipped;
      next[key] = sig;
      continue;
    }
    try {
      auto ir = ingest::import_path(brain, key, requested_source);
      r.imported_pages += ir.pages;
      r.errors += ir.errors;
      if (ir.errors == 0) next[key] = sig;
    } catch (const std::exception& ex) {
      util::warn(std::string("live_sync import failed: ") + ex.what());
      ++r.errors;
    }
  }
  if (ec) ++r.errors;
  try {
    save_state(sp, next);
  } catch (...) {
    ++r.errors;
  }
  if (r.imported_pages > 0 || r.errors > 0) {
    try {
      brain.log_ingest(
          "live_sync", notes_dir,
          json({{"imported_pages", r.imported_pages},
                {"scanned", r.scanned},
                {"skipped", r.skipped},
                {"errors", r.errors}})
              .dump(),
          100, requested_source);
    } catch (...) {
    }
  }
  return r;
}

int live_sync_watch(Brain& brain, const std::string& notes_dir, int interval_ms, int max_cycles,
                    const std::string& source_id) {
  int total = 0;
  int cycles = 0;
  for (;;) {
    auto r = live_sync_once(brain, notes_dir, source_id);
    total += r.imported_pages;
    ++cycles;
    if (r.imported_pages || r.errors)
      util::info("live_sync cycle pages=" + std::to_string(r.imported_pages) +
                 " scanned=" + std::to_string(r.scanned) + " skip=" + std::to_string(r.skipped));
    if (max_cycles > 0 && cycles >= max_cycles) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(std::max(200, interval_ms)));
  }
  return total;
}

}  // namespace qbrain::service
