#pragma once
#include "qbrain/core/types.hpp"
#include "qbrain/storage/database.hpp"
#include <optional>
#include <string>
#include <vector>

namespace qbrain {

class Brain {
 public:
  explicit Brain(std::string brain_id = "default");

  void open();
  void open_at(const std::string& db_path);
  void close();
  bool is_open() const;

  const std::string& brain_id() const { return brain_id_; }
  storage::Database& db() { return db_; }
  const Config& config() const { return config_; }
  Config& config() { return config_; }

  void load_config();
  void save_config_value(const std::string& key, const std::string& value);
  std::optional<std::string> get_config_value(const std::string& key);

  Page put_page(const PageInput& in);
  std::optional<Page> get_page(const std::string& slug,
                               const std::string& source_id = "default",
                               bool include_deleted = false);
  std::vector<Page> list_pages(int limit = 50, const std::string& type = "");
  bool soft_delete(const std::string& slug, const std::string& source_id = "default");

  void replace_chunks(int64_t page_id, const std::vector<std::string>& texts);
  std::vector<Chunk> get_chunks(int64_t page_id);
  std::vector<Chunk> list_chunks_missing_embedding(int limit = 100000);
  void update_chunk_embedding(int64_t chunk_id, const std::vector<float>& emb,
                              const std::string& model);

  void add_link(const Link& link);
  void replace_extracted_links(const std::string& source_id,
                               const std::string& from_slug,
                               const std::vector<Link>& links);
  std::vector<Link> get_links_from(const std::string& slug,
                                   const std::string& source_id = "default");
  std::vector<Link> get_links_to(const std::string& slug,
                                 const std::string& source_id = "default");

  BrainStats stats();
  HealthReport health();

 private:
  std::string brain_id_;
  storage::Database db_;
  Config config_;
  Page row_to_page(storage::Database::Statement& st);
};

Config load_file_config();
void save_file_config(const Config& c);
std::string resolve_api_key(const Config& c, bool for_chat);

}  // namespace qbrain
