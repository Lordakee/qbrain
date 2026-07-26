#pragma once
#include "qbrain/core/brain.hpp"
#include <string>
#include <vector>

namespace qbrain::codeintel {

struct Hit {
  std::string slug;
  int line = 0;
  std::string snippet;
  std::string kind;  // def | ref | call
};

// Heuristic C++/TS-like definition patterns (no tree-sitter).
std::vector<Hit> find_defs(Brain& brain, const std::string& symbol, int limit = 50,
                           int page_limit = 500);

// Word-boundary symbol references across page bodies.
std::vector<Hit> find_refs(Brain& brain, const std::string& symbol, int limit = 50,
                           int page_limit = 500);

// Call-ish pattern: symbol( — reuses ref scan with stricter match.
std::vector<Hit> find_callers(Brain& brain, const std::string& symbol, int limit = 50,
                              int page_limit = 500);

}  // namespace qbrain::codeintel
