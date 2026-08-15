#pragma once
#include "qbrain/core/brain.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace qbrain::cycle {

struct PhaseResult {
  std::string phase;
  std::string status;  // ok | skipped | warn | fail
  std::string summary;
  int64_t count = 0;
  int64_t mutations = 0;
  int duration_ms = 0;
};

struct CycleReport {
  std::string status = "ok";  // ok | partial | failed
  std::string timestamp;
  int duration_ms = 0;
  bool dry_run = true;
  std::vector<PhaseResult> phases;
};

struct DreamOpts {
  bool dry_run = true;
  std::string phase;  // empty = all phases when dry; non-destructive phases when applying
  int page_limit = 50;
  std::string retention_hours;  // empty = 72; parsed strictly by run_dream
};

// Multi-phase dream cycle. Purge is applied only when phase is explicitly "purge".
CycleReport run_dream(Brain& brain, const DreamOpts& opts);

std::string report_to_json(const CycleReport& r);
std::string report_to_text(const CycleReport& r);

}  // namespace qbrain::cycle
