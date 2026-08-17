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
  const bool use_responses =
      cfg.chat_endpoint.empty() || cfg.chat_endpoint == "responses";
  json body;
  body["model"] = cfg.chat_model;
  const int effective_timeout_ms = timeout_ms > 0 ? timeout_ms : 120000;

  if (use_responses) {
    // OpenAI Responses API: POST /responses with input items, reasoning, and
    // an output_text extraction convenience on the server side.
    json input = json::array();
    for (const auto& m : messages) {
      input.push_back({{"role", m.role},
                       {"content", json::array({{{"type", "input_text"}, {"text", m.content}}})}});
    }
    body["input"] = input;
    body["temperature"] = temperature;
    if (!cfg.chat_reasoning_effort.empty())
      body["reasoning"] = {{"effort", cfg.chat_reasoning_effort}};
  } else {
    // Legacy OpenAI Chat Completions: POST /chat/completions with messages.
    body["temperature"] = temperature;
    body["messages"] = json::array();
    for (const auto& m : messages) {
      body["messages"].push_back({{"role", m.role}, {"content", m.content}});
    }
  }

  auto base = cfg.chat_base_url.empty() ? cfg.embedding_base_url : cfg.chat_base_url;
  const char* path = use_responses ? "/responses" : "/chat/completions";
  const auto started = std::chrono::steady_clock::now();
  auto resp = http_post_json(base, path, key, body.dump(), effective_timeout_ms);
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count();
  if (resp.status < 200 || resp.status >= 300) {
    r.error = resp.error.empty() ? resp.body : resp.error;
    if (resp.status == 0) {
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
    if (use_responses) {
      // Responses API returns output items; concatenate message texts.
      std::string text;
      if (j.contains("output_text") && j["output_text"].is_string()) {
        text = j["output_text"].get<std::string>();
      } else if (j.contains("output") && j["output"].is_array()) {
        for (const auto& item : j["output"]) {
          if (item.value("type", "") == "message" && item.contains("content") &&
              item["content"].is_array()) {
            for (const auto& c : item["content"]) {
              if (c.value("type", "") == "output_text" && c.contains("text"))
                text += c["text"].get<std::string>();
            }
          }
        }
      }
      if (text.empty()) {
        r.error = "parse chat: no output_text in responses payload";
        r.failure_kind = ChatFailureKind::malformed_response;
        return r;
      }
      r.content = std::move(text);
    } else {
      r.content = j.at("choices").at(0).at("message").at("content").get<std::string>();
    }
    r.ok = true;
  } catch (const std::exception& e) {
    r.error = std::string("parse chat: ") + e.what();
    r.failure_kind = ChatFailureKind::malformed_response;
  }
  return r;
}

}  // namespace qbrain::ai
