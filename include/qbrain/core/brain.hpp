#pragma once
#include "qbrain/core/types.hpp"
#include "qbrain/storage/database.hpp"
#include <cstddef>
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
  const std::string& db_path() const noexcept { return db_path_; }
  storage::Database& db() { return db_; }
  const Config& config() const { return config_; }
  Config& config() { return config_; }

  void load_config();
  void save_config_value(const std::string& key, const std::string& value);
  std::optional<std::string> get_config_value(const std::string& key);

  // N2.5: canonical source identity and read-only registration lookup.
  static std::optional<std::string> canonical_source_id(const std::string& source_id);
  bool source_exists(const std::string& source_id);

  Page put_page(const PageInput& in);
  std::optional<Page> get_page(const std::string& slug,
                               const std::string& source_id = "default",
                               bool include_deleted = false);
  std::vector<Page> list_pages(int limit = 50, const std::string& type = "");
  // N16: active pages for one already-authorized canonical source.
  std::vector<Page> list_pages_for_source(const std::string& source_id, int limit = 50);
  // N19: source/type predicates precede the limit; effective activity is deterministic.
  std::vector<Page> list_pages_for_source(const std::string& source_id, int limit,
                                          const std::string& type);
  bool soft_delete(const std::string& slug, const std::string& source_id = "default");
  bool restore_page(const std::string& slug, const std::string& source_id = "default");
  int purge_deleted(int older_than_hours = 72);
  void create_version(int64_t page_id);
  std::vector<Page> list_versions(const std::string& slug, const std::string& source_id = "default");
  bool revert_version(const std::string& slug, int64_t version_id,
                      const std::string& source_id = "default");
  bool ensure_source(const std::string& source_id);
  std::vector<std::string> list_source_ids();
  // N13: remove an empty source, or explicitly force-delete its dependent data.
  bool remove_source(const std::string& source_id, bool force = false);
  struct SourceStatus {
    std::string id;
    int64_t pages = 0;
    int64_t links = 0;
    std::string last_updated;
  };
  SourceStatus source_status(const std::string& source_id);

  void replace_chunks(int64_t page_id, const std::vector<std::string>& texts);
  std::vector<Chunk> get_chunks(int64_t page_id);
  std::vector<Chunk> list_chunks_missing_embedding(int limit = 100000);
  void update_chunk_embedding(int64_t chunk_id, const std::vector<float>& emb,
                              const std::string& model);

  // N1: enqueue embed job (non-blocking). No-op if embed.auto=0.
  void enqueue_embed_page(int64_t page_id);
  // Test-only deterministic availability seam. It never persists or changes
  // provider/model/key configuration; std::nullopt restores production lookup.
  void set_embedding_available_for_test(std::optional<bool> availability) {
    embedding_available_override_ = availability;
  }
  // Process waiting embed jobs; returns number of chunks embedded.
  int drain_embed_jobs(int max_jobs = 100);

  void add_link(const Link& link);
  void replace_extracted_links(const std::string& source_id,
                               const std::string& from_slug,
                               const std::vector<Link>& links);
  std::vector<Link> get_links_from(const std::string& slug,
                                   const std::string& source_id = "default");
  std::vector<Link> get_links_to(const std::string& slug,
                                 const std::string& source_id = "default");

  // N10 facts (minimal)
  void add_fact(const std::string& entity_slug, const std::string& predicate,
                const std::string& object_text, int64_t page_id = 0);
  std::vector<std::string> list_facts(const std::string& entity_slug, int limit = 50);
  int extract_facts_from_page(const std::string& slug, const std::string& source_id = "default");
  // Soft-deactivate matching active facts (predicate optional filter).
  int forget_fact(const std::string& entity_slug, const std::string& predicate = "");

  void add_tag(const std::string& slug, const std::string& tag,
               const std::string& source_id = "default");
  void remove_tag(const std::string& slug, const std::string& tag,
                  const std::string& source_id = "default");
  std::vector<std::string> get_tags(const std::string& slug,
                                    const std::string& source_id = "default");
  void remove_link(const std::string& from, const std::string& to,
                   const std::string& source_id = "default");
  std::vector<std::string> find_orphans(int limit = 100);
  static std::vector<std::string> list_brains();

  // N15: link_source histogram
  struct LinkSourceCount {
    std::string source_id;
    std::string link_source;
    int64_t count = 0;
  };
  std::vector<LinkSourceCount> list_link_sources(const std::string& source_id = "default");

  // N15: ingest event log (last N retained)
  struct IngestLogEntry {
    int64_t id = 0;
    std::string source_id;
    std::string event_type;
    std::string path;
    std::string detail_json;
    std::string created_at;
  };
  int64_t log_ingest(const std::string& event_type, const std::string& path,
                     const std::string& detail_json = "{}", int keep_last = 100,
                     const std::string& source_id = "default");
  std::vector<IngestLogEntry> get_ingest_log(int limit = 20,
                                             const std::string& source_id = "default");

  // N15: chronicle — pages touched on UTC day or since ISO timestamp
  struct ChronicleHit {
    int64_t id = 0;
    std::string source_id;
    std::string slug;
    std::string title;
    std::string updated_at;
    std::string created_at;
    std::string effective_at;
    std::string type;
  };
  std::vector<ChronicleHit> chronicle_day(const std::string& day_utc, int limit = 100,
                                          const std::string& source_id = "default");
  std::vector<ChronicleHit> chronicle_since(const std::string& since_iso, int limit = 100,
                                            const std::string& source_id = "default");
  // N23: source-scoped Chronicle page-activity/tagging subset.
  struct ChronicleOnThisDayHit {
    int64_t id = 0;
    std::string source_id;
    std::string slug;
    std::string title;
    std::string type;
    std::string created_at;
    std::string updated_at;
    std::string matched_at;
    int years_ago = 0;
  };
  struct ChronicleLastSeen {
    std::string source_id;
    std::string entity;
    std::string last_seen;
  };
  struct ChronicleBackfillResult {
    std::string source_id;
    int scanned = 0;
    int eligible = 0;
    int tagged = 0;
    int already_tagged = 0;
    bool dry_run = false;
  };
  std::vector<ChronicleOnThisDayHit> chronicle_on_this_day(
      const std::string& anchor_date, int limit, const std::string& source_id,
      bool allow_virtual_leap_day = false);
  std::optional<ChronicleLastSeen> chronicle_last_seen(const std::string& entity,
                                                       const std::string& source_id);
  ChronicleBackfillResult chronicle_backfill(
      const std::string& source_id, const std::optional<std::string>& since,
      int limit = 1000, bool dry_run = false);

  // Historical direct-call wrappers. Registered N23 operations use the source-required APIs above.
  std::vector<ChronicleHit> chronicle_on_this_day(const std::string& mmdd, int limit = 100);
  std::string chronicle_last_seen(const std::string& slug = {});
  int chronicle_backfill(int limit = 1000);

  // N21 takes
  struct Take {
    int64_t id = 0;
    std::string entity_slug;
    std::string kind;
    std::string body;
    double score = 0;
    std::string created_at;
  };
  int64_t put_take(const std::string& entity_slug, const std::string& body,
                   const std::string& kind = "fact", double score = 0);
  std::vector<Take> takes_list(const std::string& entity_slug = "", int limit = 50);
  std::vector<Take> takes_search(const std::string& query, int limit = 50);
  int takes_promote_facts(int limit = 100);

  // N27 raw data
  bool put_raw_data(const std::string& key, const std::string& content_text,
                    const std::string& meta_json = "{}");
  std::optional<std::pair<std::string, std::string>> get_raw_data(const std::string& key);
  std::vector<std::pair<std::string, std::string>> list_raw_prefix(const std::string& prefix,
                                                                   int limit = 50);

  BrainStats stats();
  HealthReport health();

  struct SourceIdentitySnapshot {
    std::string source_id;
    int schema_version = 0;
    int64_t pages = 0;
    int64_t chunks = 0;
    int64_t links = 0;
    int64_t embedded_chunks = 0;
  };
  // Read-only counters for one already-resolved canonical source.
  SourceIdentitySnapshot source_identity_snapshot(const std::string& source_id);

  struct StatusSnapshot {
    int schema_version = 0;
    int64_t pages = 0;
    int64_t chunks = 0;
    int64_t links = 0;
    int64_t embedded_chunks = 0;
    int64_t jobs_waiting = 0;
    int64_t jobs_active = 0;
    int64_t jobs_failed = 0;
    int64_t jobs_paused = 0;
  };
  StatusSnapshot status_snapshot();

  struct RemediateReport {
    bool default_source = false;
    int reclaimed = 0;
    int embed_jobs_enqueued = 0;
    bool api_key_present = false;
    std::vector<std::string> notes;
  };
  // Optional availability override is a deterministic test seam. Production
  // callers omit it and use the configured embedding credential lookup.
  RemediateReport remediate(std::optional<bool> embedding_available_override = std::nullopt);

 private:
  std::string brain_id_;
  std::string db_path_;
  storage::Database db_;
  Config config_;
  std::optional<bool> embedding_available_override_;
  Page row_to_page(storage::Database::Statement& st);
};

Config load_file_config();
void save_file_config(const Config& c);
std::string resolve_api_key(const Config& c, bool for_chat);

}  // namespace qbrain
