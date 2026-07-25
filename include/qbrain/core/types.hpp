#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace qbrain {

struct Page {
  int64_t id = 0;
  std::string source_id = "default";
  std::string slug;
  std::string type = "note";
  std::string title;
  std::string body;
  std::string frontmatter_json = "{}";
  std::string content_hash;
  std::string created_at;
  std::string updated_at;
  std::optional<std::string> deleted_at;
};

struct PageInput {
  std::string source_id = "default";
  std::string slug;
  std::string type = "note";
  std::string title;
  std::string body;
  std::string frontmatter_json = "{}";
};

struct Chunk {
  int64_t id = 0;
  int64_t page_id = 0;
  int chunk_index = 0;
  std::string text;
  std::vector<float> embedding;
  int dim = 0;
  std::string model;
};

struct Link {
  int64_t id = 0;
  std::string source_id = "default";
  std::string from_slug;
  std::string to_slug;
  std::string link_type = "related";
  std::string context;
  std::string link_source = "markdown";
};

struct SearchHit {
  int64_t page_id = 0;
  std::string slug;
  std::string title;
  std::string snippet;
  double score = 0.0;
  double fts_rank = 0.0;
  double vector_rank = 0.0;
  std::string type;
};

struct BrainStats {
  int64_t pages = 0;
  int64_t chunks = 0;
  int64_t links = 0;
  int64_t embedded_chunks = 0;
};

struct HealthReport {
  bool ok = true;
  std::string db_path;
  int schema_version = 0;
  BrainStats stats;
  std::vector<std::string> notes;
};

struct Config {
  std::string brain_id = "default";
  std::string embedding_provider = "openai";
  std::string embedding_model = "text-embedding-3-small";
  std::string embedding_base_url = "https://api.openai.com/v1";
  std::string embedding_api_key;
  int embedding_dimensions = 1536;
  std::string chat_model = "gpt-4o-mini";
  std::string chat_base_url = "https://api.openai.com/v1";
  std::string chat_api_key;
  int search_rrf_k = 60;
  int search_default_limit = 10;
};

}  // namespace qbrain
