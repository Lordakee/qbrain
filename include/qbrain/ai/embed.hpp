#pragma once
#include "qbrain/core/types.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace qbrain::ai {

struct EmbedResult {
  bool ok = false;
  std::string error;
  std::vector<std::vector<float>> vectors;
  std::string model;
};

EmbedResult embed_texts(const Config& cfg, const std::vector<std::string>& texts);

// N33 D3: optional multimodal image-embedding provider contract.
// - Credentials follow the existing chat/embed env convention
//   (Config embedding_api_key -> OPENAI_API_KEY -> QBRAIN_API_KEY).
// - With credentials: POST base64 (data-URL) to the configured
//   OpenAI-compatible /embeddings endpoint, 30s timeout, response body
//   capped at 2 MiB; any failure degrades to `unavailable`.
// - Without credentials: immediate `unavailable`, no network request.
// - QBRAIN_EMBED_MOCK=1|true: deterministic local vector (hash of the
//   first 4 KiB -> 64-bit seed -> deterministic RNG); no network.
// - Error redaction (credential isolation): the returned error never
//   contains the base_url, any credential material, or any URL; it is
//   truncated to 200 characters.
struct ImageEmbedResult {
  bool ok = false;
  // Degraded outcome: no credentials or provider failure. Callers fail
  // open (structured unavailable) instead of erroring out.
  bool unavailable = false;
  // Unavailable specifically because no provider credentials resolved.
  bool no_credentials = false;
  bool mock = false;
  std::string error;  // redacted, at most 200 chars
  std::vector<float> vector;
  std::string model;
};

ImageEmbedResult embed_image(const Config& cfg, std::string_view image_bytes);

}  // namespace qbrain::ai
