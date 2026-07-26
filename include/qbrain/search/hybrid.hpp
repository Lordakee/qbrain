#pragma once
#include "qbrain/core/brain.hpp"
#include "qbrain/core/types.hpp"
#include <string>
#include <vector>

namespace qbrain::search {

struct HybridOpts {
  int limit = 10;
  int rrf_k = 60;
  bool use_vector = true;
  std::string source_id;  // empty = all
  std::string mode;       // conservative | balanced | tokenmax (N3)
  bool rerank = false;    // explicit override; tokenmax enables by default
  bool rerank_llm = false;
  const Config* config = nullptr;  // needed for LLM rerank; optional
};

std::vector<SearchHit> fts_search(Brain& brain, const std::string& query, int limit,
                                  const std::string& source_id = {});
std::vector<SearchHit> vector_search(Brain& brain, const std::vector<float>& qemb, int limit,
                                     const std::string& source_id = {});
std::vector<SearchHit> hybrid_search(Brain& brain, const std::string& query,
                                     const std::vector<float>* qemb,
                                     const HybridOpts& opts);

}  // namespace qbrain::search
