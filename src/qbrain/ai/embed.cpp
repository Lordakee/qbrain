#include "qbrain/ai/embed.hpp"
#include "qbrain/ai/http_client.hpp"
#include "qbrain/core/brain.hpp"
#include "qbrain/util/hash.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <string_view>

using json = nlohmann::json;

namespace qbrain::ai {

namespace {

// ---- N33 D3: image embedding provider contract ----

constexpr int kImageEmbedTimeoutMs = 30000;           // 30s hard timeout
constexpr size_t kImageMaxResponseBytes = 2u * 1024 * 1024;  // response <= 2 MiB
constexpr size_t kImageMaxInputBytes = 32u * 1024 * 1024;    // 32 MiB input cap
constexpr size_t kImageMockHashPrefix = 4096;         // hash first 4 KiB only
constexpr size_t kImageMockVectorDims = 64;           // deterministic mock width
constexpr size_t kImageMaxErrorChars = 200;           // redacted error cap

std::string base64_encode(std::string_view in) {
  static const char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const unsigned char b0 = static_cast<unsigned char>(in[i]);
    const unsigned char b1 = static_cast<unsigned char>(in[i + 1]);
    const unsigned char b2 = static_cast<unsigned char>(in[i + 2]);
    out.push_back(kTable[b0 >> 2]);
    out.push_back(kTable[((b0 & 0x03) << 4) | (b1 >> 4)]);
    out.push_back(kTable[((b1 & 0x0F) << 2) | (b2 >> 6)]);
    out.push_back(kTable[b2 & 0x3F]);
  }
  if (i + 1 == in.size()) {
    const unsigned char b0 = static_cast<unsigned char>(in[i]);
    out.push_back(kTable[b0 >> 2]);
    out.push_back(kTable[(b0 & 0x03) << 4]);
    out.push_back('=');
    out.push_back('=');
  } else if (i + 2 == in.size()) {
    const unsigned char b0 = static_cast<unsigned char>(in[i]);
    const unsigned char b1 = static_cast<unsigned char>(in[i + 1]);
    out.push_back(kTable[b0 >> 2]);
    out.push_back(kTable[((b0 & 0x03) << 4) | (b1 >> 4)]);
    out.push_back(kTable[(b1 & 0x0F) << 2]);
    out.push_back('=');
  }
  return out;
}

void replace_all(std::string& text, const std::string& needle, const std::string& replacement) {
  if (needle.empty()) return;
  size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    text.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
}

bool is_scheme_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
         c == '+' || c == '-' || c == '.';
}

// N33 D3 credential isolation: strip the configured base URLs, all resolved
// credential material, and any residual URL-looking span, then truncate.
std::string redact_provider_error(std::string message, const Config& cfg,
                                  const std::string& api_key) {
  for (const std::string* secret : {&api_key, &cfg.embedding_api_key, &cfg.chat_api_key}) {
    if (!secret->empty()) replace_all(message, *secret, "[redacted]");
  }
  for (const std::string* url : {&cfg.embedding_base_url, &cfg.chat_base_url}) {
    if (!url->empty()) replace_all(message, *url, "[redacted]");
  }
  // Generic URL scrub: anything scheme://... up to the next delimiter.
  size_t pos = 0;
  while ((pos = message.find("://", pos)) != std::string::npos) {
    size_t start = pos;
    while (start > 0 && is_scheme_char(message[start - 1])) --start;
    size_t end = pos + 3;
    while (end < message.size() && message[end] != ' ' && message[end] != '"' &&
           message[end] != '\'' && message[end] != '<' && message[end] != '>' &&
           message[end] != ')' && message[end] != ']' && message[end] != '}' &&
           message[end] != '\n' && message[end] != '\r' && message[end] != '\t') {
      ++end;
    }
    message.replace(start, end - start, "[redacted]");
    pos = start + std::string("[redacted]").size();
  }
  if (message.size() > kImageMaxErrorChars) message.resize(kImageMaxErrorChars);
  return message;
}

// Deterministic mock vector: hash of the first 4 KiB -> 64-bit seed ->
// splitmix64 stream -> fixed 64 dims. Same content = identical vector.
std::vector<float> mock_image_vector(std::string_view bytes) {
  const size_t n = std::min(bytes.size(), kImageMockHashPrefix);
  const std::string hex = util::sha256_hex(std::string_view(bytes.data(), n));
  uint64_t seed = 0;
  for (size_t i = 0; i < 16 && i < hex.size(); ++i) {
    seed = (seed << 4) | static_cast<uint64_t>(hex[i] <= '9' ? hex[i] - '0'
                                                             : (hex[i] | 0x20) - 'a' + 10);
  }
  std::vector<float> v;
  v.reserve(kImageMockVectorDims);
  uint64_t s = seed;
  for (size_t i = 0; i < kImageMockVectorDims; ++i) {
    s += 0x9E3779B97F4A7C15ULL;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    const double unit = static_cast<double>(z >> 40) / 16777215.0;  // [0,1]
    v.push_back(static_cast<float>(unit * 2.0 - 1.0));              // [-1,1]
  }
  return v;
}

}  // namespace

EmbedResult embed_texts(const Config& cfg, const std::vector<std::string>& texts) {
  EmbedResult r;
  r.model = cfg.embedding_model;
  if (texts.empty()) {
    r.ok = true;
    return r;
  }
  if (const char* mock = std::getenv("QBRAIN_EMBED_MOCK")) {
    if (std::string(mock) == "1" || std::string(mock) == "true") {
      r.ok = true;
      r.model = "mock-embedding";
      r.vectors.reserve(texts.size());
      for (size_t i = 0; i < texts.size(); ++i) {
        float base = static_cast<float>((texts[i].size() % 17) + 1);
        r.vectors.push_back({base, 1.0f, static_cast<float>(i + 1)});
      }
      return r;
    }
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

ImageEmbedResult embed_image(const Config& cfg, std::string_view image_bytes) {
  ImageEmbedResult r;
  r.model = cfg.embedding_model;
  auto degrade = [&](const std::string& message, bool no_credentials) {
    r.unavailable = true;
    r.no_credentials = no_credentials;
    r.error = redact_provider_error(message, cfg, resolve_api_key(cfg, false));
    return r;
  };
  if (image_bytes.empty()) return degrade("empty image input", false);
  if (image_bytes.size() > kImageMaxInputBytes) {
    return degrade("image exceeds size limit", false);
  }
  if (const char* mock = std::getenv("QBRAIN_EMBED_MOCK")) {
    if (std::string(mock) == "1" || std::string(mock) == "true") {
      r.ok = true;
      r.mock = true;
      r.model = "mock-image-embedding";
      r.vector = mock_image_vector(image_bytes);
      return r;
    }
  }
  const std::string key = resolve_api_key(cfg, false);
  if (key.empty()) return degrade("no provider credentials", true);
  // Local magic sniff only to label the data URL; no image_meta dependency.
  std::string mime = "image/png";
  if (image_bytes.size() >= 3 && static_cast<unsigned char>(image_bytes[0]) == 0xFF &&
      static_cast<unsigned char>(image_bytes[1]) == 0xD8 &&
      static_cast<unsigned char>(image_bytes[2]) == 0xFF) {
    mime = "image/jpeg";
  }
  json body;
  body["model"] = cfg.embedding_model;
  body["input"] = json::array({json{{"type", "image_url"},
                                    {"image_url", {{"url", "data:" + mime + ";base64," +
                                                                 base64_encode(image_bytes)}}}}});
  auto resp = http_post_json(cfg.embedding_base_url, "/embeddings", key, body.dump(),
                             kImageEmbedTimeoutMs);
  if (resp.body.size() > kImageMaxResponseBytes) {
    return degrade("embedding response exceeds size limit", false);
  }
  if (resp.status == 0 && !resp.error.empty()) {
    return degrade("embedding provider unavailable: " + resp.error, false);
  }
  if (resp.status < 200 || resp.status >= 300) {
    return degrade(!resp.error.empty() ? resp.error : resp.body, false);
  }
  try {
    auto j = json::parse(resp.body);
    r.vector = j.at("data").at(0).at("embedding").get<std::vector<float>>();
    if (r.vector.empty()) return degrade("embedding provider returned empty vector", false);
    r.ok = true;
  } catch (const std::exception& e) {
    return degrade(std::string("parse embedding response: ") + e.what(), false);
  }
  return r;
}

}  // namespace qbrain::ai
