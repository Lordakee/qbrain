#include "qbrain/search/rerank.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

static std::vector<qbrain::SearchHit> sample_hits() {
  using qbrain::SearchHit;
  return {
      SearchHit{1, "a", "Alpha quantum", "body a", 1.0},
      SearchHit{2, "b", "Beta search", "body b", 0.9},
      SearchHit{3, "c", "Gamma note", "body c", 0.8},
  };
}

static size_t count_audit_lines() {
  auto p = qbrain::search::rerank_audit_path();
  if (p.empty() || !std::filesystem::exists(p)) return 0;
  std::ifstream in(p);
  size_t n = 0;
  std::string line;
  while (std::getline(in, line))
    if (!line.empty()) ++n;
  return n;
}

void test_rerank() {
  using qbrain::Config;
  using qbrain::search::RerankerOpts;
  using qbrain::search::apply_reranker;

  Config cfg;
  auto hits = sample_hits();

  // 1) LLM throws → fail-open same size + membership + audit line
  {
    size_t before = count_audit_lines();
    RerankerOpts opts;
    opts.enabled = true;
    opts.top_n_in = 30;
    opts.use_llm = true;
    opts.llm_fn_for_test =
        [](const std::string&,
           const std::vector<qbrain::SearchHit>&) -> std::vector<qbrain::SearchHit> {
      throw std::runtime_error("injected llm throw");
    };
    auto out = apply_reranker(cfg, "quantum search", hits, opts);
    QB_CHECK(out.size() == hits.size());
    QB_CHECK(out.size() == 3);
    // membership by slug
    int found = 0;
    for (auto& h : out)
      if (h.slug == "a" || h.slug == "b" || h.slug == "c") ++found;
    QB_CHECK(found == 3);
    size_t after = count_audit_lines();
    QB_CHECK(after > before);
  }

  // 2) LLM returns empty → keep local (or original) membership, audit line
  {
    size_t before = count_audit_lines();
    RerankerOpts opts;
    opts.enabled = true;
    opts.top_n_in = 30;
    opts.use_llm = true;
    opts.llm_fn_for_test =
        [](const std::string&,
           const std::vector<qbrain::SearchHit>&) -> std::vector<qbrain::SearchHit> {
      return {};
    };
    auto out = apply_reranker(cfg, "quantum", hits, opts);
    QB_CHECK(out.size() == hits.size());
    QB_CHECK(count_audit_lines() > before);
  }

  // 3) LLM returns partial membership → reject, keep size
  {
    size_t before = count_audit_lines();
    RerankerOpts opts;
    opts.enabled = true;
    opts.top_n_in = 30;
    opts.use_llm = true;
    opts.llm_fn_for_test =
        [](const std::string&,
           const std::vector<qbrain::SearchHit>& head) -> std::vector<qbrain::SearchHit> {
      return {head[0]};  // drop 2
    };
    auto out = apply_reranker(cfg, "quantum", hits, opts);
    QB_CHECK(out.size() == hits.size());
    QB_CHECK(count_audit_lines() > before);
  }

  // 4) disabled → identity
  {
    RerankerOpts opts;
    opts.enabled = false;
    auto out = apply_reranker(cfg, "q", hits, opts);
    QB_CHECK(out.size() == hits.size());
    QB_CHECK(out[0].slug == hits[0].slug);
  }

  // 5) local-only (no llm) reorders but preserves membership
  {
    RerankerOpts opts;
    opts.enabled = true;
    opts.use_llm = false;
    opts.top_n_in = 30;
    auto out = apply_reranker(cfg, "quantum", hits, opts);
    QB_CHECK(out.size() == 3);
  }
}
