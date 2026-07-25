#include "qbrain/core/brain.hpp"
#include "qbrain/ingest/import.hpp"
#include "qbrain/util/paths.hpp"
#include "qbrain/util/log.hpp"
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_set>
#include <fstream>

namespace qbrain::service {
namespace fs = std::filesystem;

fs::path inbox_dir() {
  return util::qbrain_root() / "inbox";
}

// Poll inbox for new files; import and move to inbox/processed.
// max_cycles=0 means forever (use with care).
int watch_inbox_once(Brain& brain) {
  util::ensure_dir(inbox_dir());
  util::ensure_dir(inbox_dir() / "processed");
  int n = 0;
  for (auto& e : fs::directory_iterator(inbox_dir())) {
    if (!e.is_regular_file()) continue;
    auto ext = e.path().extension().string();
    if (ext != ".md" && ext != ".txt" && ext != ".markdown") continue;
    try {
      auto r = ingest::import_path(brain, util::path_to_utf8(e.path()));
      n += r.pages;
      auto dest = inbox_dir() / "processed" / e.path().filename();
      fs::rename(e.path(), dest);
    } catch (const std::exception& ex) {
      util::warn(std::string("inbox import failed: ") + ex.what());
    }
  }
  return n;
}

}  // namespace qbrain::service
