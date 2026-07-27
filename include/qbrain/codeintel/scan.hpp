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

// N22: symbols called inside definition bodies of `symbol`.
std::vector<Hit> find_callees(Brain& brain, const std::string& symbol, int limit = 50,
                              int page_limit = 500);
// Depth-limited def → callee names as hits (kind=flow).
std::vector<Hit> find_flow(Brain& brain, const std::string& symbol, int depth = 2,
                           int limit = 50, int page_limit = 500);
// Union of def+refs+callers+callees.
std::vector<Hit> find_blast(Brain& brain, const std::string& symbol, int limit = 80,
                            int page_limit = 500);
void clear_traversal_cache();

}  // namespace qbrain::codeintel
