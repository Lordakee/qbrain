#include "qbrain/search/rerank.hpp"
#include "qbrain/util/hash.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

using json = nlohmann::json;

#define QB_CHECK(cond)                                                          \
  do {                                                                          \
    if (!(cond)) {                                                              \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond + " @ " + \
                               __FILE__ + ":" + std::to_string(__LINE__));      \
    }                                                                           \
  } while (0)

namespace {

constexpr std::uintmax_t kMaxAuditBytes = 1024U * 1024U;

#ifdef _WIN32
class SilentLoopbackServer {
 public:
  SilentLoopbackServer() {
    WSADATA startup{};
    if (WSAStartup(MAKEWORD(2, 2), &startup) != 0) {
      throw std::runtime_error("WSAStartup failed");
    }
    winsock_started_ = true;
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == INVALID_SOCKET) fail("socket failed");

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
        SOCKET_ERROR) {
      fail("bind failed");
    }
    if (listen(listener_, 1) == SOCKET_ERROR) fail("listen failed");
    int address_size = sizeof(address);
    if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &address_size) ==
        SOCKET_ERROR) {
      fail("getsockname failed");
    }
    port_ = ntohs(address.sin_port);
    const SOCKET listener = listener_;
    worker_ = std::thread([this, listener] { serve(listener); });
  }

  ~SilentLoopbackServer() { stop(); }

  SilentLoopbackServer(const SilentLoopbackServer&) = delete;
  SilentLoopbackServer& operator=(const SilentLoopbackServer&) = delete;

  int port() const noexcept { return port_; }

  void wait() {
    if (worker_.joinable()) worker_.join();
  }

 private:
  [[noreturn]] void fail(const char* message) {
    if (listener_ != INVALID_SOCKET) closesocket(listener_);
    listener_ = INVALID_SOCKET;
    if (winsock_started_) WSACleanup();
    winsock_started_ = false;
    throw std::runtime_error(message);
  }

  void serve(SOCKET listener) noexcept {
    const SOCKET accepted = accept(listener, nullptr, nullptr);
    if (accepted == INVALID_SOCKET) return;
    {
      const std::lock_guard<std::mutex> lock(socket_mutex_);
      if (stopping_) {
        closesocket(accepted);
        return;
      }
      accepted_ = accepted;
    }

    const DWORD receive_timeout_ms = 7000;
    setsockopt(accepted, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&receive_timeout_ms),
               sizeof(receive_timeout_ms));
    std::array<char, 2048> buffer{};
    while (recv(accepted, buffer.data(), static_cast<int>(buffer.size()), 0) > 0) {
      // Intentionally consume the request without ever sending an HTTP response.
    }

    bool close_here = false;
    {
      const std::lock_guard<std::mutex> lock(socket_mutex_);
      if (accepted_ == accepted) {
        accepted_ = INVALID_SOCKET;
        close_here = true;
      }
    }
    if (close_here) closesocket(accepted);
  }

  void stop() noexcept {
    SOCKET accepted = INVALID_SOCKET;
    {
      const std::lock_guard<std::mutex> lock(socket_mutex_);
      stopping_ = true;
      accepted = accepted_;
      accepted_ = INVALID_SOCKET;
    }
    if (listener_ != INVALID_SOCKET) {
      closesocket(listener_);
      listener_ = INVALID_SOCKET;
    }
    if (accepted != INVALID_SOCKET) {
      shutdown(accepted, SD_BOTH);
      closesocket(accepted);
    }
    if (worker_.joinable()) worker_.join();
    if (winsock_started_) WSACleanup();
    winsock_started_ = false;
  }

  bool winsock_started_ = false;
  SOCKET listener_ = INVALID_SOCKET;
  SOCKET accepted_ = INVALID_SOCKET;
  int port_ = 0;
  bool stopping_ = false;
  std::mutex socket_mutex_;
  std::thread worker_;
};
#endif

class ScopedAuditRoot {
 public:
  ScopedAuditRoot() {
    if (const auto* current = std::getenv("LOCALAPPDATA")) {
      had_original_ = true;
      original_ = current;
    }
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
            ("qbrain-rerank-test-" + std::to_string(unique));
    std::filesystem::create_directories(root_);
#ifdef _WIN32
    if (_putenv_s("LOCALAPPDATA", root_.string().c_str()) != 0) {
      throw std::runtime_error("failed to set temporary LOCALAPPDATA");
    }
#else
    if (setenv("LOCALAPPDATA", root_.string().c_str(), 1) != 0) {
      throw std::runtime_error("failed to set temporary LOCALAPPDATA");
    }
#endif
  }

  ~ScopedAuditRoot() {
#ifdef _WIN32
    _putenv_s("LOCALAPPDATA", had_original_ ? original_.c_str() : "");
#else
    if (had_original_)
      setenv("LOCALAPPDATA", original_.c_str(), 1);
    else
      unsetenv("LOCALAPPDATA");
#endif
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

 private:
  std::filesystem::path root_;
  bool had_original_ = false;
  std::string original_;
};

std::vector<qbrain::SearchHit> sample_hits() {
  using qbrain::SearchHit;
  return {
      SearchHit{3, "c", "Gamma note", "body c", 0.8},
      SearchHit{2, "b", "Beta search", "body b", 0.9},
      SearchHit{1, "a", "Alpha quantum", "body a", 1.0},
  };
}

std::vector<json> read_audit_records(const std::filesystem::path& path) {
  std::vector<json> records;
  std::ifstream input(path, std::ios::binary);
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) records.push_back(json::parse(line));
  }
  return records;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

std::string serialize_triples(const std::vector<qbrain::SearchHit>& hits) {
  json triples = json::array();
  for (size_t rank = 0; rank < hits.size(); ++rank) {
    triples.push_back({hits[rank].page_id, hits[rank].slug, rank,
                       hits[rank].rerank_score});
  }
  return triples.dump();
}

void check_safe_audit_record(const json& record) {
  static const std::set<std::string> allowed_reasons = {
      "local_exception",    "transport_error",   "transport_timeout",
      "empty_response",     "malformed_response", "membership_mismatch",
      "empty_guard",        "size_guard",
  };
  QB_CHECK(record.is_object());
  QB_CHECK(record.size() == 5);
  QB_CHECK(record.contains("timestamp"));
  QB_CHECK(record.contains("query_hash"));
  QB_CHECK(record.contains("failure_reason"));
  QB_CHECK(record.contains("fallback_taken"));
  QB_CHECK(record.contains("doc_count"));
  const auto timestamp = record.at("timestamp").get<std::string>();
  QB_CHECK(timestamp.size() == 20);
  QB_CHECK(timestamp[4] == '-' && timestamp[7] == '-' && timestamp[10] == 'T');
  QB_CHECK(timestamp.back() == 'Z');
  const auto query_hash = record.at("query_hash").get<std::string>();
  QB_CHECK(query_hash.size() == 64);
  QB_CHECK(std::all_of(query_hash.begin(), query_hash.end(), [](unsigned char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
  }));
  QB_CHECK(allowed_reasons.count(record.at("failure_reason").get<std::string>()) == 1);
  QB_CHECK(record.at("fallback_taken").get<bool>());
  const auto doc_count = record.at("doc_count").get<size_t>();
  QB_CHECK(doc_count <= 1000);
}

void check_fallback_equals(const std::vector<qbrain::SearchHit>& actual,
                           const std::vector<qbrain::SearchHit>& expected) {
  QB_CHECK(serialize_triples(actual) == serialize_triples(expected));
  QB_CHECK(actual.size() == expected.size());
  for (const auto& hit : actual) {
    QB_CHECK(std::isfinite(hit.rerank_score));
    QB_CHECK(hit.rerank_score >= 0.0);
    QB_CHECK(hit.rerank_score <= 1.0);
  }
}

}  // namespace

void test_rerank() {
  using qbrain::Config;
  using qbrain::search::RerankerOpts;
  using qbrain::search::apply_reranker;

  ScopedAuditRoot audit_root;
  const std::filesystem::path audit_path(qbrain::search::rerank_audit_path());
  Config cfg;
  cfg.chat_api_key = "DUMMY_API_KEY_SENTINEL";
  const auto hits = sample_hits();

  RerankerOpts local_opts;
  local_opts.enabled = true;
  local_opts.top_n_in = 30;
  const auto baseline = apply_reranker(cfg, "quantum", hits, local_opts);
  const auto baseline_repeat = apply_reranker(cfg, "quantum", hits, local_opts);
  QB_CHECK(serialize_triples(baseline) == serialize_triples(baseline_repeat));
  QB_CHECK(baseline.size() == 3);
  QB_CHECK(baseline[0].slug == "a");
  QB_CHECK(baseline[1].slug == "c");
  QB_CHECK(baseline[2].slug == "b");
  QB_CHECK(baseline[0].rerank_score == 1.0);
  QB_CHECK(baseline[1].rerank_score == 0.0);
  QB_CHECK(baseline[2].rerank_score == 0.0);
  QB_CHECK(baseline[0].score == 1.0);
  QB_CHECK(baseline[1].score == 0.8);
  QB_CHECK(baseline[2].score == 0.9);

  const std::string sensitive_query = "SENSITIVE_QUERY_SENTINEL";
  const auto sensitive_baseline = apply_reranker(cfg, sensitive_query, hits, local_opts);

  RerankerOpts failure_opts = local_opts;
  failure_opts.use_llm = true;
  failure_opts.llm_fn_for_test =
      [](const std::string&,
         const std::vector<qbrain::SearchHit>&) -> std::vector<qbrain::SearchHit> {
    throw std::runtime_error("SENSITIVE_ERROR_BODY_SENTINEL");
  };
  auto output = apply_reranker(cfg, sensitive_query, hits, failure_opts);
  check_fallback_equals(output, sensitive_baseline);
  auto records = read_audit_records(audit_path);
  QB_CHECK(records.size() == 1);
  QB_CHECK(records.back().at("failure_reason") == "local_exception");
  const auto first_query_hash = records.back().at("query_hash").get<std::string>();

  failure_opts.llm_fn_for_test =
      [](const std::string&,
         const std::vector<qbrain::SearchHit>&) -> std::vector<qbrain::SearchHit> { return {}; };
  output = apply_reranker(cfg, sensitive_query, hits, failure_opts);
  check_fallback_equals(output, sensitive_baseline);
  records = read_audit_records(audit_path);
  QB_CHECK(records.size() == 2);
  QB_CHECK(records.back().at("failure_reason") == "empty_response");
  QB_CHECK(records.back().at("query_hash").get<std::string>() == first_query_hash);
  QB_CHECK(first_query_hash != qbrain::util::sha256_hex(sensitive_query));

  failure_opts.llm_fn_for_test = [](const std::string&,
                                    const std::vector<qbrain::SearchHit>& head) {
    return std::vector<qbrain::SearchHit>{head[0]};
  };
  output = apply_reranker(cfg, "quantum", hits, failure_opts);
  check_fallback_equals(output, baseline);
  records = read_audit_records(audit_path);
  QB_CHECK(records.back().at("failure_reason") == "membership_mismatch");

  failure_opts.llm_fn_for_test = [](const std::string&,
                                    const std::vector<qbrain::SearchHit>& head) {
    return std::vector<qbrain::SearchHit>{head[0], head[0], head[2]};
  };
  output = apply_reranker(cfg, "quantum", hits, failure_opts);
  check_fallback_equals(output, baseline);
  records = read_audit_records(audit_path);
  QB_CHECK(records.back().at("failure_reason") == "membership_mismatch");

  failure_opts.llm_fn_for_test = [](const std::string&,
                                    const std::vector<qbrain::SearchHit>& head) {
    auto foreign = head;
    foreign[1].page_id = 999;
    foreign[1].slug = "foreign";
    return foreign;
  };
  output = apply_reranker(cfg, "quantum", hits, failure_opts);
  check_fallback_equals(output, baseline);
  records = read_audit_records(audit_path);
  QB_CHECK(records.back().at("failure_reason") == "membership_mismatch");

  failure_opts.llm_fn_for_test = {};
  failure_opts.llm_response_for_test =
      [](const std::string&, const std::vector<qbrain::SearchHit>&) {
        return std::string("SENSITIVE_RESPONSE_BODY_SENTINEL");
      };
  output = apply_reranker(cfg, "malformed query", hits, failure_opts);
  const auto malformed_baseline = apply_reranker(cfg, "malformed query", hits, local_opts);
  check_fallback_equals(output, malformed_baseline);
  records = read_audit_records(audit_path);
  QB_CHECK(records.back().at("failure_reason") == "malformed_response");
  QB_CHECK(records.back().at("query_hash").get<std::string>() != first_query_hash);

  const auto audit_text = read_file(audit_path);
  QB_CHECK(audit_text.find(sensitive_query) == std::string::npos);
  QB_CHECK(audit_text.find("DUMMY_API_KEY_SENTINEL") == std::string::npos);
  QB_CHECK(audit_text.find("SENSITIVE_ERROR_BODY_SENTINEL") == std::string::npos);
  QB_CHECK(audit_text.find("SENSITIVE_RESPONSE_BODY_SENTINEL") == std::string::npos);
  for (const auto& record : records) {
    check_safe_audit_record(record);
    QB_CHECK(record.at("doc_count").get<size_t>() == hits.size());
  }

#ifdef _WIN32
  {
    SilentLoopbackServer silent_provider;
    Config timeout_cfg = cfg;
    timeout_cfg.chat_base_url =
        "http://127.0.0.1:" + std::to_string(silent_provider.port()) + "/v1";
    RerankerOpts timeout_opts = local_opts;
    timeout_opts.use_llm = true;
    timeout_opts.timeout_ms = 3000;
    const auto started = std::chrono::steady_clock::now();
    output = apply_reranker(timeout_cfg, "silent provider query", hits, timeout_opts);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    silent_provider.wait();
    std::cout << "[INFO] silent_provider_elapsed_ms=" << elapsed.count() << "\n";

    const auto timeout_baseline =
        apply_reranker(timeout_cfg, "silent provider query", hits, local_opts);
    check_fallback_equals(output, timeout_baseline);
    QB_CHECK(elapsed.count() < 4500);
    records = read_audit_records(audit_path);
    QB_CHECK(records.back().at("failure_reason") == "transport_timeout");
    check_safe_audit_record(records.back());
    QB_CHECK(read_file(audit_path).find("DUMMY_API_KEY_SENTINEL") == std::string::npos);
    std::cout << "[INFO] rerank_audit_sample=" << records.back().dump() << "\n"
              << std::flush;
  }
#endif

  RerankerOpts success_opts = local_opts;
  success_opts.use_llm = true;
  success_opts.llm_response_for_test =
      [](const std::string&, const std::vector<qbrain::SearchHit>&) {
        return std::string("[2,0,1]");
      };
  const auto llm_output = apply_reranker(cfg, "quantum", hits, success_opts);
  QB_CHECK(llm_output.size() == 3);
  QB_CHECK(llm_output[0].slug == "b");
  QB_CHECK(llm_output[1].slug == "a");
  QB_CHECK(llm_output[2].slug == "c");
  QB_CHECK(llm_output[0].rerank_score == 1.0);
  QB_CHECK(llm_output[1].rerank_score == 0.5);
  QB_CHECK(llm_output[2].rerank_score == 0.0);
  QB_CHECK(read_audit_records(audit_path).size() == records.size());

  const std::string rotation_marker = "ROTATION_CONTENT_SENTINEL";
  {
    std::ofstream seed(audit_path, std::ios::binary | std::ios::trunc);
    seed.write(rotation_marker.data(), static_cast<std::streamsize>(rotation_marker.size()));
    seed.seekp(static_cast<std::streamoff>(kMaxAuditBytes - 1U));
    seed.put('X');
  }
  RerankerOpts rotation_opts = local_opts;
  rotation_opts.llm_fn_for_test =
      [](const std::string&,
         const std::vector<qbrain::SearchHit>&) -> std::vector<qbrain::SearchHit> {
    throw std::runtime_error("rotation trigger");
  };
  output = apply_reranker(cfg, "rotation query", hits, rotation_opts);
  const auto rotation_baseline = apply_reranker(cfg, "rotation query", hits, local_opts);
  check_fallback_equals(output, rotation_baseline);

  const std::filesystem::path rotated_path(audit_path.string() + ".1");
  QB_CHECK(std::filesystem::exists(rotated_path));
  QB_CHECK(std::filesystem::file_size(rotated_path) == kMaxAuditBytes);
  QB_CHECK(read_file(rotated_path).substr(0, rotation_marker.size()) == rotation_marker);
  QB_CHECK(std::filesystem::exists(audit_path));
  QB_CHECK(std::filesystem::file_size(audit_path) <= kMaxAuditBytes);
  const auto post_rotation_records = read_audit_records(audit_path);
  QB_CHECK(post_rotation_records.size() == 1);
  QB_CHECK(post_rotation_records[0].at("failure_reason") == "local_exception");
  check_safe_audit_record(post_rotation_records[0]);

  RerankerOpts disabled;
  disabled.enabled = false;
  const auto disabled_output = apply_reranker(cfg, "quantum", hits, disabled);
  QB_CHECK(disabled_output.size() == hits.size());
  QB_CHECK(disabled_output[0].slug == hits[0].slug);
  QB_CHECK(disabled_output[0].score == hits[0].score);
}
