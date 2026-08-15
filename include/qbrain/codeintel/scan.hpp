#pragma once
#include "qbrain/core/brain.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace qbrain::codeintel {

struct Hit {
  std::string slug;
  int line = 0;
  std::string snippet;
  // def | ref | call | callee:<target> | flow:d<N>:<target>
  std::string kind;
  std::string source_id;
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
// Depth-limited definition-to-callee traversal with depth encoded in each hit kind.
std::vector<Hit> find_flow(Brain& brain, const std::string& symbol, int depth = 2,
                           int limit = 50, int page_limit = 500);
// Union of def+refs+callers+callees.
std::vector<Hit> find_blast(Brain& brain, const std::string& symbol, int limit = 80,
                            int page_limit = 500);
void clear_traversal_cache();

// N16: source-scoped heuristic scans. Legacy unscoped APIs above remain for N22 compatibility.
bool is_valid_symbol(std::string_view symbol);
std::vector<Hit> find_defs_in_source(Brain& brain, const std::string& source_id,
                                     const std::string& symbol, int limit = 50,
                                     int page_limit = 500);
std::vector<Hit> find_refs_in_source(Brain& brain, const std::string& source_id,
                                     const std::string& symbol, int limit = 50,
                                     int page_limit = 500);
std::vector<Hit> find_callers_in_source(Brain& brain, const std::string& source_id,
                                        const std::string& symbol, int limit = 50,
                                        int page_limit = 500);

// N22: source-scoped, brace-body heuristic traversal. The unscoped APIs above remain for
// compatibility, but registered N22 operations use these source-required entry points.
std::vector<Hit> find_callees_in_source(Brain& brain, const std::string& source_id,
                                        const std::string& symbol, int limit = 50,
                                        int page_limit = 500);
std::vector<Hit> find_flow_in_source(Brain& brain, const std::string& source_id,
                                     const std::string& symbol, int depth = 2,
                                     int limit = 50, int page_limit = 500);
std::vector<Hit> find_blast_in_source(Brain& brain, const std::string& source_id,
                                      const std::string& symbol, int limit = 80,
                                      int page_limit = 500);

}  // namespace qbrain::codeintel
