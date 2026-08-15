#include "qbrain/ai/chat.hpp"
#include "qbrain/ai/http_client.hpp"
#include "qbrain/core/brain.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace qbrain::ai {

ChatResult chat_complete(const Config& cfg, const std::vector<ChatMessage>& messages,
                         double temperature, int timeout_ms) {
  ChatResult r;
  auto key = resolve_api_key(cfg, true);
  if (key.empty()) {
    r.error = "missing chat API key";
    r.failure_kind = ChatFailureKind::configuration;
    return r;
  }
  json body;
  body["model"] = cfg.chat_model;
  body["temperature"] = temperature;
  body["messages"] = json::array();
  for (const auto& m : messages) {
    body["messages"].push_back({{"role", m.role}, {"content", m.content}});
  }
  auto base = cfg.chat_base_url.empty() ? cfg.embedding_base_url : cfg.chat_base_url;
  const int effective_timeout_ms = timeout_ms > 0 ? timeout_ms : 120000;
  const auto started = std::chrono::steady_clock::now();
  auto resp =
      http_post_json(base, "/chat/completions", key, body.dump(), effective_timeout_ms);
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count();
  if (resp.status < 200 || resp.status >= 300) {
    r.error = resp.error.empty() ? resp.body : resp.error;
    if (resp.status == 0) {
      // WinHTTP reports timeout and other transport failures through the same
      // error surface. A request consuming most of its explicit deadline is a
      // timeout; immediate failures remain transport errors.
      const auto timeout_threshold = std::max<int64_t>(1, effective_timeout_ms * 8LL / 10LL);
      r.failure_kind = elapsed_ms >= timeout_threshold ? ChatFailureKind::transport_timeout
                                                      : ChatFailureKind::transport_error;
    } else {
      r.failure_kind = ChatFailureKind::http_status;
    }
    return r;
  }
  try {
    auto j = json::parse(resp.body);
    r.content = j.at("choices").at(0).at("message").at("content").get<std::string>();
    r.ok = true;
  } catch (const std::exception& e) {
    r.error = std::string("parse chat: ") + e.what();
    r.failure_kind = ChatFailureKind::malformed_response;
  }
  return r;
}

}  // namespace qbrain::ai
