#pragma once
#include "qbrain/core/brain.hpp"
#include <string>

namespace qbrain::service {

struct LiveSyncResult {
  int scanned = 0;
  int imported_pages = 0;
  int skipped = 0;
  int errors = 0;
};

// One-shot poll of notes directory: import new/changed .md/.txt/.markdown.
// State file: %LOCALAPPDATA%\Qbrain\sync-state\<scope-hash>.json, scoped by
// canonical brain id, source id, and notes root (mtime+size per path).
LiveSyncResult live_sync_once(Brain& brain, const std::string& notes_dir,
                              const std::string& source_id = "default");

// Continuous poll until max_cycles (0=forever). interval_ms between polls.
// Returns total imported pages across cycles. Stops after max_cycles if >0.
int live_sync_watch(Brain& brain, const std::string& notes_dir, int interval_ms = 2000,
                    int max_cycles = 0, const std::string& source_id = "default");

}  // namespace qbrain::service
