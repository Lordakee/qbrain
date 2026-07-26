#include "qbrain/search/rerank.hpp"
#include "qbrain/ai/chat.hpp"
#include "qbrain/core/brain.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/string_util.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_set>

using json = nlohmann::json;

namespace qbrain::search {
namespace {

void log_rerank_failure(const std::string& reason, const std::string& query,
                        int doc_count, const std::string& err) noexcept {
  try {
    auto path = rerank_audit_path();
    if (path.empty()) return;
    auto parent = std::filesystem::path(path).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    std::ofstream out(path, std::ios::app);
    if (!out) return;
    std::string summary = err;
    if (summary.size() > 200) summary.resize(200);
    json j = {{"reason", reason},
              {"query_hash", util::sha256_hex(query).substr(0, 8)},
              {"doc_count", doc_count},
              {"error_summary", summary}};
    out << j.dump() << "\n";
  } catch (...) {
    // Audit must never break search.
  }
}

double local_relevance(const std::string& query, const SearchHit& h) {
  auto ql = util::to_lower(query);
  auto text = util::to_lower(h.title + " " + h.snippet);
  auto tokens = util::split(ql, ' ');
  if (tokens.empty()) return 0.0;
  int hits = 0;
  int weight = 0;
  for (auto& t : tokens) {
    t = util::trim(t);
    if (t.size() < 2) continue;
    ++weight;
    if (text.find(t) != std::string::npos) ++hits;
    if (util::to_lower(h.title).find(t) != std::string::npos) hits += 1;
  }
  if (weight == 0) return 0.0;
  return static_cast<double>(hits) / static_cast<double>(weight * 2);
}

std::vector<SearchHit> local_reorder(const std::string& query, std::vector<SearchHit> head) {
  for (auto& h : head) {
    double rel = local_relevance(query, h);
    h.rerank_score = rel;
    h.score = h.score * (1.0 + 0.5 * rel) + rel;
  }
  std::stable_sort(head.begin(), head.end(),
                   [](const SearchHit& a, const SearchHit& b) {
                     if (a.rerank_score != b.rerank_score) return a.rerank_score > b.rerank_score;
                     return a.score > b.score;
                   });
  return head;
}

std::vector<int> parse_index_order(const std::string& content, int n) {
  std::vector<int> order;
  try {
    auto j = json::parse(content);
    if (j.is_array()) {
      for (auto& el : j) {
        int idx = el.is_number_integer() ? el.get<int>()
                  : el.is_object() && el.contains("index") ? el["index"].get<int>()
                                                           : -1;
        if (idx >= 0 && idx < n) order.push_back(idx);
      }
      return order;
    }
  } catch (...) {
  }
  std::string digits;
  for (char c : content) {
    if (std::isdigit(static_cast<unsigned char>(c))) {
      digits.push_back(c);
    } else if (!digits.empty()) {
      try {
        int idx = std::stoi(digits);
        if (idx >= 0 && idx < n) order.push_back(idx);
      } catch (...) {
      }
      digits.clear();
    }
  }
  if (!digits.empty()) {
    try {
      int idx = std::stoi(digits);
      if (idx >= 0 && idx < n) order.push_back(idx);
    } catch (...) {
    }
  }
  return order;
}

std::vector<SearchHit> llm_reorder(const Config& cfg, const std::string& query,
                                   const std::vector<SearchHit>& head) {
  if (head.empty()) return head;
  std::ostringstream docs;
  for (size_t i = 0; i < head.size(); ++i) {
    auto snip = head[i].snippet;
    if (snip.size() > 240) snip = snip.substr(0, 240);
    docs << i << ". title=" << head[i].title << " | " << snip << "\n";
  }
  std::string system =
      "You are a search reranker. Reply with ONLY a JSON array of document indices "
      "sorted by relevance to the query (most relevant first). Example: [2,0,1]";
  std::string user = "Query: " + query + "\nDocuments:\n" + docs.str();
  auto cr = ai::chat_complete(cfg, {{"system", system}, {"user", user}}, 0.0);
  if (!cr.ok) {
    log_rerank_failure("llm_error", query, static_cast<int>(head.size()), cr.error);
    return head;
  }
  auto order = parse_index_order(cr.content, static_cast<int>(head.size()));
  if (order.empty()) {
    log_rerank_failure("bad_shape", query, static_cast<int>(head.size()),
                       cr.content.substr(0, 120));
    return head;
  }
  std::unordered_set<int> seen;
  std::vector<SearchHit> out;
  out.reserve(head.size());
  int pos = 0;
  for (int idx : order) {
    if (seen.count(idx)) continue;
    seen.insert(idx);
    auto item = head[static_cast<size_t>(idx)];
    item.rerank_score = 1.0 - (pos * 0.01);
    item.reranker_delta = idx - pos;
    out.push_back(std::move(item));
    ++pos;
  }
  for (int i = 0; i < static_cast<int>(head.size()); ++i) {
    if (!seen.count(i)) out.push_back(head[static_cast<size_t>(i)]);
  }
  return out;
}

bool same_membership(const std::vector<SearchHit>& a, const std::vector<SearchHit>& b) {
  if (a.size() != b.size()) return false;
  std::unordered_set<std::string> sa, sb;
  for (auto& h : a) sa.insert(h.slug.empty() ? std::to_string(h.page_id) : h.slug);
  for (auto& h : b) sb.insert(h.slug.empty() ? std::to_string(h.page_id) : h.slug);
  return sa == sb;
}

}  // namespace

std::string rerank_audit_path() {
  auto home = std::getenv("LOCALAPPDATA");
  if (!home) return {};
  return std::string(home) + "\\Qbrain\\audit\\rerank-failures.jsonl";
}

std::vector<SearchHit> apply_reranker(const Config& cfg, const std::string& query,
                                      std::vector<SearchHit> results,
                                      const RerankerOpts& opts) {
  if (!opts.enabled || results.empty() || opts.top_n_in <= 0) return results;
  const auto original = results;
  try {
    size_t n = std::min(results.size(), static_cast<size_t>(opts.top_n_in));
    std::vector<SearchHit> head(results.begin(), results.begin() + static_cast<std::ptrdiff_t>(n));
    std::vector<SearchHit> tail(results.begin() + static_cast<std::ptrdiff_t>(n), results.end());
    const auto head_backup = head;

    // Local lexical reorder (also fail-open: restore on throw)
    try {
      head = local_reorder(query, head);
    } catch (...) {
      log_rerank_failure("local_exception", query, static_cast<int>(n), "local_reorder");
      head = head_backup;
    }
    if (head.size() != head_backup.size() || !same_membership(head, head_backup)) {
      log_rerank_failure("local_membership", query, static_cast<int>(n), "size/membership mismatch");
      head = head_backup;
    }

    // Optional LLM pass (fail-open; never move-from head into fallible call)
    if (opts.use_llm || opts.llm_fn_for_test) {
      try {
        std::vector<SearchHit> reordered;
        if (opts.llm_fn_for_test) {
          reordered = opts.llm_fn_for_test(query, head);
        } else {
          reordered = llm_reorder(cfg, query, head);
        }
        if (reordered.empty() || reordered.size() != head.size() ||
            !same_membership(reordered, head)) {
          log_rerank_failure("bad_shape", query, static_cast<int>(n),
                             "empty_or_membership_mismatch");
          // keep head (local order)
        } else {
          head = std::move(reordered);
        }
      } catch (const std::exception& e) {
        log_rerank_failure("exception", query, static_cast<int>(n), e.what());
        // head still local order
      } catch (...) {
        log_rerank_failure("exception", query, static_cast<int>(n), "unknown");
      }
    }

    std::vector<SearchHit> combined;
    combined.reserve(head.size() + tail.size());
    for (auto& h : head) combined.push_back(std::move(h));
    for (auto& h : tail) combined.push_back(std::move(h));

    if (opts.top_n_out > 0 && static_cast<int>(combined.size()) > opts.top_n_out)
      combined.resize(static_cast<size_t>(opts.top_n_out));

    // Hard fail-open: never empty a non-empty input when not truncating
    if (combined.empty() && !original.empty()) {
      log_rerank_failure("empty_guard", query, static_cast<int>(original.size()), "restored");
      return original;
    }
    if (opts.top_n_out == 0 && combined.size() != original.size()) {
      log_rerank_failure("size_guard", query, static_cast<int>(original.size()), "restored");
      return original;
    }
    return combined;
  } catch (const std::exception& e) {
    log_rerank_failure("exception", query, static_cast<int>(original.size()), e.what());
    return original;
  } catch (...) {
    log_rerank_failure("exception", query, static_cast<int>(original.size()), "unknown");
    return original;
  }
}

}  // namespace qbrain::search
