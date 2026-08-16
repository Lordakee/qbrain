#pragma once
#include "qbrain/core/types.hpp"
#include <functional>
#include <string>
#include <vector>

namespace qbrain::search {

struct RerankerOpts {
  bool enabled = false;
  int top_n_in = 30;
  int top_n_out = 0;  // 0 = no truncate
  bool use_llm = false;
  // Rerank requests may use a shorter deadline but are always capped at 3000 ms.
  int timeout_ms = 3000;
  // N39 test seam: when non-null, apply_reranker copies the effective
  // (rerank-config-merged) Config into it before the LLM call. Production
  // must leave null.
  Config* cfg_capture_for_test = nullptr;
  // Test seam: when set, replaces LLM call. Production must leave null.
  // May throw or return empty/partial — apply_reranker must fail-open.
  std::function<std::vector<SearchHit>(const std::string& query,
                                       const std::vector<SearchHit>& head)>
      llm_fn_for_test;
  // Raw-response seam for exercising the production JSON index parser.
  std::function<std::string(const std::string& query,
                            const std::vector<SearchHit>& head)>
      llm_response_for_test;
};

// Fail-open: never throws; on failure returns results with same membership size
// (when top_n_out==0). Never empties a non-empty input due to rerank failure.
std::vector<SearchHit> apply_reranker(const Config& cfg, const std::string& query,
                                      std::vector<SearchHit> results,
                                      const RerankerOpts& opts);

// Path used by log_rerank_failure (for tests).
std::string rerank_audit_path();

}  // namespace qbrain::search
