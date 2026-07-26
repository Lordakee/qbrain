#include "qbrain/service/live_sync.hpp"
#include "qbrain/ingest/import.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/log.hpp"
#include "qbrain/util/paths.hpp"
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

fs::path state_path_for(const std::string& notes_dir) {
  auto h = util::sha256_hex(notes_dir).substr(0, 16);
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
  auto ext = p.extension().string();
  return ext == ".md" || ext == ".txt" || ext == ".markdown";
}

}  // namespace

LiveSyncResult live_sync_once(Brain& brain, const std::string& notes_dir,
                              const std::string& source_id) {
  LiveSyncResult r;
  fs::path root(notes_dir);
  if (!fs::exists(root) || !fs::is_directory(root)) {
    util::warn("live_sync: not a directory: " + notes_dir);
    r.errors = 1;
    return r;
  }
  brain.ensure_source(source_id.empty() ? "default" : source_id);
  auto sp = state_path_for(util::path_to_utf8(fs::absolute(root)));
  auto state = load_state(sp);
  std::unordered_map<std::string, std::string> next = state;

  for (auto& e : fs::recursive_directory_iterator(root)) {
    if (!e.is_regular_file()) continue;
    if (!is_note(e.path())) continue;
    ++r.scanned;
    auto key = util::path_to_utf8(fs::absolute(e.path()));
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
      auto ir = ingest::import_path(brain, key);
      r.imported_pages += ir.pages;
      r.errors += ir.errors;
      next[key] = sig;
    } catch (const std::exception& ex) {
      util::warn(std::string("live_sync import failed: ") + ex.what());
      ++r.errors;
    }
  }
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
                {"errors", r.errors},
                {"source_id", source_id.empty() ? "default" : source_id}})
              .dump());
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
