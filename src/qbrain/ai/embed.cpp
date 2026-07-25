#include "qbrain/ai/embed.hpp"
#include "qbrain/ai/http_client.hpp"
#include "qbrain/core/brain.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace qbrain::ai {

EmbedResult embed_texts(const Config& cfg, const std::vector<std::string>& texts) {
  EmbedResult r;
  r.model = cfg.embedding_model;
  if (texts.empty()) {
    r.ok = true;
    return r;
  }
  auto key = resolve_api_key(cfg, false);
  if (key.empty()) {
    r.error = "missing embedding API key";
    return r;
  }
  json body;
  body["model"] = cfg.embedding_model;
  body["input"] = texts;
  if (cfg.embedding_dimensions > 0) body["dimensions"] = cfg.embedding_dimensions;

  auto resp = http_post_json(cfg.embedding_base_url, "/embeddings", key, body.dump());
  if (!resp.error.empty() && resp.status == 0) {
    r.error = resp.error;
    return r;
  }
  if (resp.status < 200 || resp.status >= 300) {
    r.error = resp.error.empty() ? resp.body : resp.error;
    return r;
  }
  try {
    auto j = json::parse(resp.body);
    auto& data = j.at("data");
    r.vectors.resize(data.size());
    for (auto& item : data) {
      size_t idx = item.at("index").get<size_t>();
      if (idx >= r.vectors.size()) r.vectors.resize(idx + 1);
      r.vectors[idx] = item.at("embedding").get<std::vector<float>>();
    }
    r.ok = true;
  } catch (const std::exception& e) {
    r.error = std::string("parse embeddings: ") + e.what();
  }
  return r;
}

}  // namespace qbrain::ai
