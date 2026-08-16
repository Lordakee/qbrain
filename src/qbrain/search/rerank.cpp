#include "qbrain/search/rerank.hpp"
#include "qbrain/ai/chat.hpp"
#include "qbrain/core/brain.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/string_util.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string_view>
#include <vector>

using json = nlohmann::json;

namespace qbrain::search {
namespace {

constexpr std::uintmax_t kMaxAuditBytes = 1024U * 1024U;
constexpr int kAuditRotationCount = 5;
constexpr size_t kMaxAuditDocCount = 1000;
constexpr int kMaxRerankTimeoutMs = 3000;

enum class FailureReason {
  local_exception,
  transport_error,
  transport_timeout,
  empty_response,
  malformed_response,
  membership_mismatch,
  empty_guard,
  size_guard,
};

const char* failure_reason_name(FailureReason reason) noexcept {
  switch (reason) {
    case FailureReason::local_exception:
      return "local_exception";
    case FailureReason::transport_error:
      return "transport_error";
    case FailureReason::transport_timeout:
      return "transport_timeout";
    case FailureReason::empty_response:
      return "empty_response";
    case FailureReason::malformed_response:
      return "malformed_response";
    case FailureReason::membership_mismatch:
      return "membership_mismatch";
    case FailureReason::empty_guard:
      return "empty_guard";
    case FailureReason::size_guard:
      return "size_guard";
  }
  return "local_exception";
}

std::string utc_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t value = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &value);
#else
  gmtime_r(&value, &utc);
#endif
  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

const std::string& process_query_salt() {
  static const std::string salt = [] {
    std::random_device random;
    std::string bytes(32, '\0');
    for (char& byte : bytes) byte = static_cast<char>(random() & 0xffU);
    return bytes;
  }();
  return salt;
}

std::string salted_query_hash(std::string_view query) {
  std::string input;
  const auto& salt = process_query_salt();
  input.reserve(salt.size() + query.size());
  input.append(salt);
  input.append(query);
  return util::sha256_hex(input);
}

std::filesystem::path rotated_audit_path(const std::filesystem::path& path, int generation) {
  return std::filesystem::path(path.string() + "." + std::to_string(generation));
}

bool rename_replacing(const std::filesystem::path& from,
                      const std::filesystem::path& to) noexcept {
  std::error_code ec;
  if (!std::filesystem::exists(from, ec) || ec) return !ec;
  std::filesystem::remove(to, ec);
  ec.clear();
  std::filesystem::rename(from, to, ec);
  return !ec;
}

bool rotate_audit_if_needed(const std::filesystem::path& path,
                            std::uintmax_t next_record_bytes) noexcept {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return !ec;
  const auto current_bytes = std::filesystem::file_size(path, ec);
  if (ec) return false;
  if (current_bytes <= kMaxAuditBytes &&
      next_record_bytes <= kMaxAuditBytes - current_bytes) {
    return true;
  }

  std::filesystem::remove(rotated_audit_path(path, kAuditRotationCount), ec);
  ec.clear();
  for (int generation = kAuditRotationCount - 1; generation >= 1; --generation) {
    if (!rename_replacing(rotated_audit_path(path, generation),
                          rotated_audit_path(path, generation + 1))) {
      return false;
    }
  }
  return rename_replacing(path, rotated_audit_path(path, 1));
}

void log_rerank_failure(FailureReason reason, const std::string& query,
                        size_t doc_count) noexcept {
  try {
    const auto raw_path = rerank_audit_path();
    if (raw_path.empty()) return;

    json record = {{"timestamp", utc_timestamp()},
                   {"query_hash", salted_query_hash(query)},
                   {"failure_reason", failure_reason_name(reason)},
                   {"fallback_taken", true},
                   {"doc_count", std::min(doc_count, kMaxAuditDocCount)}};
    std::string line = record.dump();
    line.push_back('\n');
    if (line.size() > kMaxAuditBytes) return;

    static std::mutex audit_mutex;
    const std::lock_guard<std::mutex> lock(audit_mutex);
    const std::filesystem::path path(raw_path);
    const auto parent = path.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec || !rotate_audit_if_needed(path, line.size())) return;

    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) return;
    out.write(line.data(), static_cast<std::streamsize>(line.size()));
  } catch (...) {
    // Search must remain available even if the audit sink is unavailable.
  }
}

double local_relevance(const std::string& query, const SearchHit& hit) {
  const auto query_lower = util::to_lower(query);
  const auto title_lower = util::to_lower(hit.title);
  const auto text_lower = title_lower + " " + util::to_lower(hit.snippet);
  auto tokens = util::split(query_lower, ' ');
  size_t token_count = 0;
  size_t match_points = 0;
  for (auto& token : tokens) {
    token = util::trim(token);
    if (token.size() < 2) continue;
    ++token_count;
    if (text_lower.find(token) != std::string::npos) ++match_points;
    if (title_lower.find(token) != std::string::npos) ++match_points;
  }
  if (token_count == 0) return 0.0;
  const double relevance = static_cast<double>(match_points) /
                           static_cast<double>(token_count * 2U);
  return std::isfinite(relevance) ? std::clamp(relevance, 0.0, 1.0) : 0.0;
}

std::vector<SearchHit> local_baseline(const std::string& query,
                                      std::vector<SearchHit> results,
                                      size_t rerank_count) {
  for (auto& hit : results) {
    hit.rerank_score = local_relevance(query, hit);
    hit.reranker_delta = 0;
  }
  rerank_count = std::min(rerank_count, results.size());
  std::stable_sort(results.begin(),
                   results.begin() + static_cast<std::ptrdiff_t>(rerank_count),
                   [](const SearchHit& left, const SearchHit& right) {
                     return left.rerank_score > right.rerank_score;
                   });
  return results;
}

void sanitize_fallback_scores(std::vector<SearchHit>& results) noexcept {
  for (auto& hit : results) {
    if (!std::isfinite(hit.rerank_score)) hit.rerank_score = 0.0;
    hit.rerank_score = std::clamp(hit.rerank_score, 0.0, 1.0);
    hit.reranker_delta = 0;
  }
}

bool same_identity(const SearchHit& left, const SearchHit& right) noexcept {
  return left.page_id == right.page_id && left.slug == right.slug;
}

bool canonicalize_callback_order(const std::vector<SearchHit>& proposed,
                                 const std::vector<SearchHit>& baseline,
                                 std::vector<SearchHit>& canonical) {
  if (proposed.size() != baseline.size()) return false;
  std::vector<bool> used(baseline.size(), false);
  canonical.clear();
  canonical.reserve(baseline.size());
  for (size_t position = 0; position < proposed.size(); ++position) {
    size_t source_index = baseline.size();
    for (size_t index = 0; index < baseline.size(); ++index) {
      if (!used[index] && same_identity(proposed[position], baseline[index])) {
        source_index = index;
        break;
      }
    }
    if (source_index == baseline.size()) return false;
    used[source_index] = true;
    auto hit = baseline[source_index];
    hit.reranker_delta = static_cast<int>(source_index) - static_cast<int>(position);
    canonical.push_back(std::move(hit));
  }
  return true;
}

void assign_llm_scores(std::vector<SearchHit>& results) noexcept {
  if (results.empty()) return;
  const double denominator = results.size() > 1 ? static_cast<double>(results.size() - 1U) : 1.0;
  for (size_t position = 0; position < results.size(); ++position) {
    results[position].rerank_score =
        results.size() == 1 ? 1.0 : 1.0 - static_cast<double>(position) / denominator;
  }
}

struct LlmAttempt {
  bool ok = false;
  std::vector<SearchHit> results;
  FailureReason failure_reason = FailureReason::transport_error;
};

LlmAttempt reorder_from_callback(const std::vector<SearchHit>& proposed,
                                 const std::vector<SearchHit>& baseline) {
  if (proposed.empty()) return {false, {}, FailureReason::empty_response};
  std::vector<SearchHit> canonical;
  if (!canonicalize_callback_order(proposed, baseline, canonical)) {
    return {false, {}, FailureReason::membership_mismatch};
  }
  assign_llm_scores(canonical);
  return {true, std::move(canonical), FailureReason::transport_error};
}

LlmAttempt reorder_from_content(const std::string& content,
                                const std::vector<SearchHit>& baseline) {
  if (util::trim(content).empty()) return {false, {}, FailureReason::empty_response};

  json parsed;
  try {
    parsed = json::parse(content);
  } catch (...) {
    return {false, {}, FailureReason::malformed_response};
  }
  if (!parsed.is_array()) return {false, {}, FailureReason::malformed_response};
  if (parsed.empty()) return {false, {}, FailureReason::empty_response};

  std::vector<int> order;
  order.reserve(parsed.size());
  for (const auto& element : parsed) {
    if (!element.is_number_integer()) {
      return {false, {}, FailureReason::malformed_response};
    }
    const auto index = element.get<int64_t>();
    if (index < 0 || index >= static_cast<int64_t>(baseline.size())) {
      return {false, {}, FailureReason::membership_mismatch};
    }
    order.push_back(static_cast<int>(index));
  }
  if (order.size() != baseline.size()) {
    return {false, {}, FailureReason::membership_mismatch};
  }
  std::vector<bool> seen(baseline.size(), false);
  std::vector<SearchHit> reordered;
  reordered.reserve(baseline.size());
  for (size_t position = 0; position < order.size(); ++position) {
    const size_t index = static_cast<size_t>(order[position]);
    if (seen[index]) return {false, {}, FailureReason::membership_mismatch};
    seen[index] = true;
    auto hit = baseline[index];
    hit.reranker_delta = static_cast<int>(index) - static_cast<int>(position);
    reordered.push_back(std::move(hit));
  }
  assign_llm_scores(reordered);
  return {true, std::move(reordered), FailureReason::transport_error};
}

LlmAttempt request_llm_reorder(const Config& cfg, const std::string& query,
                               const std::vector<SearchHit>& baseline,
                               int timeout_ms) {
  std::ostringstream documents;
  for (size_t index = 0; index < baseline.size(); ++index) {
    auto snippet = baseline[index].snippet;
    if (snippet.size() > 240) snippet.resize(240);
    documents << index << ". title=" << baseline[index].title << " | " << snippet << "\n";
  }
  const std::string system =
      "You are a search reranker. Reply with ONLY a JSON array containing every "
      "document index exactly once, sorted by relevance. Example: [2,0,1]";
  const std::string user = "Query: " + query + "\nDocuments:\n" + documents.str();
  const auto chat =
      ai::chat_complete(cfg, {{"system", system}, {"user", user}}, 0.0, timeout_ms);
  if (!chat.ok) {
    if (chat.failure_kind == ai::ChatFailureKind::transport_timeout) {
      return {false, {}, FailureReason::transport_timeout};
    }
    if (chat.failure_kind == ai::ChatFailureKind::malformed_response) {
      return {false, {}, FailureReason::malformed_response};
    }
    return {false, {}, FailureReason::transport_error};
  }
  return reorder_from_content(chat.content, baseline);
}

void apply_positive_truncation(std::vector<SearchHit>& results, int top_n_out) {
  if (top_n_out > 0 && results.size() > static_cast<size_t>(top_n_out)) {
    results.resize(static_cast<size_t>(top_n_out));
  }
}

}  // namespace

std::string rerank_audit_path() {
  const auto* local_app_data = std::getenv("LOCALAPPDATA");
  if (!local_app_data) return {};
  return std::string(local_app_data) + "\\Qbrain\\audit\\rerank-failures.jsonl";
}

std::vector<SearchHit> apply_reranker(const Config& cfg, const std::string& query,
                                      std::vector<SearchHit> results,
                                      const RerankerOpts& opts) {
  if (!opts.enabled || results.empty() || opts.top_n_in <= 0) return results;

  // N39: the rerank LLM call uses the independent rerank configuration
  // (falling back to chat per rerank_config); capture it for tests.
  const Config effective = rerank_config(cfg);
  if (opts.cfg_capture_for_test) *opts.cfg_capture_for_test = effective;

  const auto original = results;
  try {
    const size_t rerank_count =
        std::min(results.size(), static_cast<size_t>(opts.top_n_in));
    auto baseline = local_baseline(query, std::move(results), rerank_count);

    if (opts.use_llm || opts.llm_fn_for_test || opts.llm_response_for_test) {
      std::vector<SearchHit> head(baseline.begin(),
                                  baseline.begin() + static_cast<std::ptrdiff_t>(rerank_count));
      LlmAttempt attempt;
      try {
        if (opts.llm_fn_for_test) {
          attempt = reorder_from_callback(opts.llm_fn_for_test(query, head), head);
        } else if (opts.llm_response_for_test) {
          attempt = reorder_from_content(opts.llm_response_for_test(query, head), head);
        } else {
          const int timeout_ms = opts.timeout_ms > 0
                                     ? std::min(opts.timeout_ms, kMaxRerankTimeoutMs)
                                     : kMaxRerankTimeoutMs;
          attempt = request_llm_reorder(effective, query, head, timeout_ms);
        }
      } catch (...) {
        attempt = {false, {}, FailureReason::local_exception};
      }

      if (!attempt.ok) {
        log_rerank_failure(attempt.failure_reason, query, original.size());
      } else {
        std::move(attempt.results.begin(), attempt.results.end(), baseline.begin());
      }
    }

    if (baseline.empty()) {
      auto fallback = original;
      sanitize_fallback_scores(fallback);
      log_rerank_failure(FailureReason::empty_guard, query, original.size());
      apply_positive_truncation(fallback, opts.top_n_out);
      return fallback;
    }
    if (baseline.size() != original.size()) {
      auto fallback = original;
      sanitize_fallback_scores(fallback);
      log_rerank_failure(FailureReason::size_guard, query, original.size());
      apply_positive_truncation(fallback, opts.top_n_out);
      return fallback;
    }

    apply_positive_truncation(baseline, opts.top_n_out);
    return baseline;
  } catch (...) {
    auto fallback = original;
    sanitize_fallback_scores(fallback);
    log_rerank_failure(FailureReason::local_exception, query, original.size());
    apply_positive_truncation(fallback, opts.top_n_out);
    return fallback;
  }
}

}  // namespace qbrain::search
