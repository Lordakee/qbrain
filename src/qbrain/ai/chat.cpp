#include "qbrain/ai/chat.hpp"
#include "qbrain/ai/http_client.hpp"
#include "qbrain/core/brain.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace qbrain::ai {

ChatResult chat_complete(const Config& cfg, const std::vector<ChatMessage>& messages,
                         double temperature) {
  ChatResult r;
  auto key = resolve_api_key(cfg, true);
  if (key.empty()) {
    r.error = "missing chat API key";
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
  auto resp = http_post_json(base, "/chat/completions", key, body.dump(), 120000);
  if (resp.status < 200 || resp.status >= 300) {
    r.error = resp.error.empty() ? resp.body : resp.error;
    return r;
  }
  try {
    auto j = json::parse(resp.body);
    r.content = j.at("choices").at(0).at("message").at("content").get<std::string>();
    r.ok = true;
  } catch (const std::exception& e) {
    r.error = std::string("parse chat: ") + e.what();
  }
  return r;
}

}  // namespace qbrain::ai
