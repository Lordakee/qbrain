#pragma once
#include "qbrain/core/brain.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace qbrain::graph {

struct Anomaly {
  std::string source_id;
  std::string kind;
  std::string slug;
  std::string detail;
};

struct Contradiction {
  std::string source_id;
  std::string kind;
  std::string slug;  // entity_slug
  std::string detail;
};

struct Expert {
  std::string source_id;
  std::string slug;
  int64_t inbound_count = 0;
};

// Graph heuristics are read-only. Callers pass a canonical, authorized source
// explicitly so a query cannot accidentally widen to every registered source.
std::vector<Anomaly> find_anomalies(Brain& brain, const std::string& source_id,
                                    int limit = 100);
std::vector<Contradiction> find_contradictions(Brain& brain, const std::string& source_id,
                                                int limit = 100);
std::vector<Expert> find_experts(Brain& brain, const std::string& source_id,
                                 int limit = 50);

}  // namespace qbrain::graph
