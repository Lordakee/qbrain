#pragma once
#include "qbrain/core/brain.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace qbrain::graph {

struct Anomaly {
  std::string kind;
  std::string slug;
  std::string detail;
};

struct Contradiction {
  std::string kind;
  std::string slug;  // entity_slug
  std::string detail;
};

struct Expert {
  std::string slug;
  int64_t inbound_count = 0;
};

// Graph heuristics (read-only). limit caps total results returned.
std::vector<Anomaly> find_anomalies(Brain& brain, int limit = 100);
std::vector<Contradiction> find_contradictions(Brain& brain, int limit = 100);
std::vector<Expert> find_experts(Brain& brain, int limit = 50);

}  // namespace qbrain::graph
