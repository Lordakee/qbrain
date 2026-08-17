#include "qbrain/core/brain.hpp"
#include "qbrain/ai/embed.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/paths.hpp"
#include "qbrain/util/time_util.hpp"
#include "qbrain/search/vector.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string_view>

using json = nlohmann::json;

#if defined(QBRAIN_WITH_PG)
// N38-A seam (libpq backend slice): the PG backend factory and the canonical
// PG schema bootstrap live in storage/pg_backend.*; this include is compiled
// only when the PG backend is linked in (QBRAIN_WITH_PG).
#include "qbrain/storage/pg_backend.hpp"
#endif

namespace qbrain {

namespace {

bool is_utf8_continuation_byte(unsigned char byte) { return (byte & 0xC0u) == 0x80u; }

size_t utf8_sequence_length(std::string_view value, size_t offset) {
  const auto byte_at = [&](size_t index) {
    return static_cast<unsigned char>(value[index]);
  };
  const size_t remaining = value.size() - offset;
  const unsigned char first = byte_at(offset);
  if (first <= 0x7Fu) return 1;
  if (first >= 0xC2u && first <= 0xDFu)
    return remaining >= 2 && is_utf8_continuation_byte(byte_at(offset + 1)) ? 2 : 0;
  if (first == 0xE0u)
    return remaining >= 3 && byte_at(offset + 1) >= 0xA0u &&
                   byte_at(offset + 1) <= 0xBFu &&
                   is_utf8_continuation_byte(byte_at(offset + 2))
               ? 3
               : 0;
  if ((first >= 0xE1u && first <= 0xECu) ||
      (first >= 0xEEu && first <= 0xEFu))
    return remaining >= 3 && is_utf8_continuation_byte(byte_at(offset + 1)) &&
                   is_utf8_continuation_byte(byte_at(offset + 2))
               ? 3
               : 0;
  if (first == 0xEDu)
    return remaining >= 3 && byte_at(offset + 1) >= 0x80u &&
                   byte_at(offset + 1) <= 0x9Fu &&
                   is_utf8_continuation_byte(byte_at(offset + 2))
               ? 3
               : 0;
  if (first == 0xF0u)
    return remaining >= 4 && byte_at(offset + 1) >= 0x90u &&
                   byte_at(offset + 1) <= 0xBFu &&
                   is_utf8_continuation_byte(byte_at(offset + 2)) &&
                   is_utf8_continuation_byte(byte_at(offset + 3))
               ? 4
               : 0;
  if (first >= 0xF1u && first <= 0xF3u)
    return remaining >= 4 && is_utf8_continuation_byte(byte_at(offset + 1)) &&
                   is_utf8_continuation_byte(byte_at(offset + 2)) &&
                   is_utf8_continuation_byte(byte_at(offset + 3))
               ? 4
               : 0;
  if (first == 0xF4u)
    return remaining >= 4 && byte_at(offset + 1) >= 0x80u &&
                   byte_at(offset + 1) <= 0x8Fu &&
                   is_utf8_continuation_byte(byte_at(offset + 2)) &&
                   is_utf8_continuation_byte(byte_at(offset + 3))
               ? 4
               : 0;
  return 0;
}

bool is_valid_utf8_text(std::string_view value) {
  for (size_t offset = 0; offset < value.size();) {
    const size_t length = utf8_sequence_length(value, offset);
    if (length == 0) return false;
    offset += length;
  }
  return true;
}

std::optional<std::string> canonical_source_id_impl(const std::string& source_id) {
  if (source_id.empty() || source_id.size() > 64) return std::nullopt;
  std::string canon;
  canon.reserve(source_id.size());
  for (unsigned char c : source_id) {
    if (c >= 'A' && c <= 'Z') {
      canon.push_back(static_cast<char>(c - 'A' + 'a'));
    } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
      canon.push_back(static_cast<char>(c));
    } else {
      return std::nullopt;
    }
  }
  static const char* kReserved[] = {"con",  "prn",  "aux",  "nul",  "com1", "com2", "com3",
                                    "com4", "com5", "com6", "com7", "com8", "com9", "lpt1",
                                    "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8",
                                    "lpt9"};
  for (auto* reserved : kReserved) {
    if (canon == reserved) return std::nullopt;
  }
  return canon;
}

}  // namespace

Brain::Brain(std::string brain_id) : brain_id_(util::normalize_brain_id(std::move(brain_id))) {}

std::optional<std::string> Brain::canonical_source_id(const std::string& source_id) {
  return canonical_source_id_impl(source_id);
}

void Brain::open() {
  util::ensure_dir(util::brain_dir(brain_id_));
  util::ensure_dir(util::qbrain_root());
  open_at(util::path_to_utf8(util::brain_db_path(brain_id_)));
}

void Brain::open_at(const std::string& db_path) {
  db_path_.clear();
  // ---- N38 PG wiring (plan D2): explicit opt-in backend selection --------
  // When QBRAIN_PG_DSN is non-empty, the PG backend replaces SQLite for this
  // Brain (open_pg). Every later Brain call flows through the same
  // storage::Database facade; no Brain method branches on the backend except
  // through the dialect-guarded sites marked `n38:` per the SQL census.
  //
  // Wiring seam — dialect-clean verification (N38 census, brain.cpp slice):
  //   put_page / get_page / list_pages* / soft_delete / restore_page /
  //   create_version / list_versions / revert_version / ensure_source /
  //   remove_source / source_status / add_fact / list_facts / forget_fact /
  //   add_tag / remove_tag / get_tags / remove_link / find_orphans /
  //   replace_chunks / get_chunks / list_chunks_missing_embedding /
  //   update_chunk_embedding / add_link / replace_extracted_links /
  //   get_links_* / log_ingest / get_ingest_log / chronicle_* (substr/CAST
  //   bodies are portable per census) / put_take / takes_* / put_raw_data /
  //   get_raw_data / list_raw_prefix / stats / enqueue_embed_page /
  //   drain_embed_jobs / save_config_value: portable SQL only (census
  //   brain.cpp = 95 statements, 84 portable). The former translatables were
  //   rewritten here (INSERT OR IGNORE x3, purge_deleted datetime, COLLATE
  //   BINARY list_link_sources) or are guarded downstream (migrate.cpp
  //   introspection); structural BEGIN IMMEDIATE x4 maps to PG busy
  //   semantics inside the backend (N38-A D1).
  //   NOT dialect-clean in PG mode: FTS paths route through the
  //   IStorageBackend::fts_search seam (SQLite FTS5 / PG tsvector+GIN).
  if (const char* pg_dsn_env = std::getenv("QBRAIN_PG_DSN"); pg_dsn_env && *pg_dsn_env) {
    open_pg(pg_dsn_env);  // n38: PG opt-in (fail-loud in a no-PG build)
    return;
  }
  db_.open(db_path);
  // Always migrate from embedded canonical schema (no CWD-dependent fallback DDL).
  // Optional QBRAIN_SCHEMA path overrides only the v1 SQL text if the file exists.
  std::string override_path;
  if (const char* env = std::getenv("QBRAIN_SCHEMA")) {
    std::ifstream t(env);
    if (t.good()) override_path = env;
  }
  storage::apply_migrations(db_, override_path);
  load_config();
  db_path_ = db_path;
}

void Brain::open_pg(const std::string& dsn) {
#if defined(QBRAIN_WITH_PG)
  // N38 PG open path (plan D2), shared by the QBRAIN_PG_DSN branch of
  // open_at() and the explicit-DSN test/harness seam n38_open_pg_brain():
  // A's make_pg_backend constructs+opens the connection, pg_ensure_schema
  // applies the canonical v13-equivalent PG DDL idempotently (rejecting a
  // pre-existing 0 < version < 13 store with guidance), the facade is armed
  // through the generic adopt_backend seam, and the version gate below
  // enforces exactly v13 (an older PG database is refused, never silently
  // upgraded -- plan D2).
  db_path_.clear();
  auto backend = storage::make_pg_backend(dsn);
  storage::pg_ensure_schema(storage::pg_conn_of(*backend));
  db_.adopt_backend(std::move(backend));
  int64_t ver = 0;
  {
    auto st = db_.prepare("SELECT COALESCE(MAX(version),0) FROM schema_version");
    if (st.step()) ver = st.column_int(0);
  }
  if (ver != 13) {
    throw std::runtime_error(
        "QBRAIN_PG_DSN database is at schema_version " + std::to_string(ver) +
        ", not the required 13; refusing to open (drop/recreate the PG database or "
        "restore it to v13 before using it as a Qbrain backend)");
  }
  load_config();
  // Storage identity for reporting paths (sanitized host/dbname descriptor
  // per the D0.5 backend_file_path contract; never the DSN itself).
  db_path_ = db_.backend_file_path();
#else
  // Fail loud, never silently fall back to SQLite when PG was requested:
  // an opt-in backend must not degrade into the default one (plan goal).
  (void)dsn;
  throw std::runtime_error(
      "PostgreSQL backend requested (QBRAIN_PG_DSN / Brain::open_pg) but this "
      "build was compiled without it (QBRAIN_WITH_PG off); unset QBRAIN_PG_DSN "
      "or rebuild with PG support");
#endif
}

void Brain::close() {
  db_.close();
  db_path_.clear();
}
bool Brain::is_open() const { return db_.is_open(); }

Config load_file_config() {
  Config c;
  auto path = util::config_path();
  std::ifstream in(path);
  if (!in) return c;
  try {
    json j = json::parse(in);
    if (j.contains("brain_id")) c.brain_id = j["brain_id"].get<std::string>();
    if (j.contains("embedding")) {
      auto& e = j["embedding"];
      if (e.contains("provider")) c.embedding_provider = e["provider"];
      if (e.contains("model")) c.embedding_model = e["model"];
      if (e.contains("base_url")) c.embedding_base_url = e["base_url"];
      if (e.contains("api_key")) c.embedding_api_key = e["api_key"];
      if (e.contains("dimensions")) c.embedding_dimensions = e["dimensions"];
    }
    if (j.contains("chat")) {
      auto& ch = j["chat"];
      if (ch.contains("model")) c.chat_model = ch["model"];
      if (ch.contains("base_url")) c.chat_base_url = ch["base_url"];
      if (ch.contains("api_key")) c.chat_api_key = ch["api_key"];
      if (ch.contains("reasoning_effort")) c.chat_reasoning_effort = ch["reasoning_effort"];
      if (ch.contains("endpoint")) c.chat_endpoint = ch["endpoint"];
    }
    if (j.contains("rerank")) {  // N39: optional; empty fields fall back to chat
      auto& rr = j["rerank"];
      if (rr.contains("model")) c.rerank_model = rr["model"];
      if (rr.contains("base_url")) c.rerank_base_url = rr["base_url"];
      if (rr.contains("api_key")) c.rerank_api_key = rr["api_key"];
      if (rr.contains("api_type")) c.rerank_api_type = rr["api_type"];
    }
    if (j.contains("search")) {
      auto& s = j["search"];
      if (s.contains("rrf_k")) c.search_rrf_k = s["rrf_k"];
      if (s.contains("default_limit")) c.search_default_limit = s["default_limit"];
    }
  } catch (...) {
  }
  return c;
}

void save_file_config(const Config& c) {
  util::ensure_dir(util::qbrain_root());
  json j;
  j["brain_id"] = c.brain_id;
  // Never persist API keys to the file plane (audit P1-1). Keys live in DB config
  // table and/or environment variables only.
  j["embedding"] = {{"provider", c.embedding_provider},
                    {"model", c.embedding_model},
                    {"base_url", c.embedding_base_url},
                    {"dimensions", c.embedding_dimensions}};
  j["chat"] = {{"model", c.chat_model}, {"base_url", c.chat_base_url}};
  j["search"] = {{"rrf_k", c.search_rrf_k}, {"default_limit", c.search_default_limit}};
  std::ofstream out(util::config_path());
  out << j.dump(2);
}

Config rerank_config(const Config& c) {
  // N39 (plan Option A): map rerank fields into the chat slots of a copy so
  // chat_complete consumes them unchanged; empty rerank fields keep the chat
  // values (current behavior when no rerank section is configured).
  Config copy = c;
  if (!c.rerank_model.empty()) copy.chat_model = c.rerank_model;
  if (!c.rerank_base_url.empty()) copy.chat_base_url = c.rerank_base_url;
  if (!c.rerank_api_key.empty()) copy.chat_api_key = c.rerank_api_key;
  copy.rerank_model = c.rerank_model;
  copy.rerank_base_url = c.rerank_base_url;
  copy.rerank_api_key = c.rerank_api_key;
  copy.rerank_api_type = c.rerank_api_type;
  return copy;
}

std::string resolve_api_key(const Config& c, bool for_chat) {
  if (for_chat) {
    if (!c.chat_api_key.empty()) return c.chat_api_key;
    if (const char* e = std::getenv("OPENAI_API_KEY")) return e;
    if (const char* e = std::getenv("QBRAIN_API_KEY")) return e;
    return c.embedding_api_key;
  }
  if (!c.embedding_api_key.empty()) return c.embedding_api_key;
  if (const char* e = std::getenv("OPENAI_API_KEY")) return e;
  if (const char* e = std::getenv("QBRAIN_API_KEY")) return e;
  return c.chat_api_key;
}

void Brain::load_config() {
  config_ = load_file_config();
  config_.brain_id = brain_id_;
  // overlay DB config keys
  try {
    auto st = db_.prepare("SELECT key, value FROM config");
    while (st.step()) {
      auto k = st.column_text(0);
      auto v = st.column_text(1);
      if (k == "embedding.model") config_.embedding_model = v;
      else if (k == "embedding.base_url") config_.embedding_base_url = v;
      else if (k == "embedding.api_key") config_.embedding_api_key = v;
      else if (k == "embedding.dimensions") config_.embedding_dimensions = std::stoi(v);
      else if (k == "chat.model") config_.chat_model = v;
      else if (k == "chat.base_url") config_.chat_base_url = v;
      else if (k == "chat.api_key") config_.chat_api_key = v;
      else if (k == "chat.reasoning_effort") config_.chat_reasoning_effort = v;
      else if (k == "chat.endpoint") config_.chat_endpoint = v;
      else if (k == "rerank.model") config_.rerank_model = v;  // N39
      else if (k == "rerank.base_url") config_.rerank_base_url = v;
      else if (k == "rerank.api_key") config_.rerank_api_key = v;
      else if (k == "rerank.api_type") config_.rerank_api_type = v;  // N40
      else if (k == "search.rrf_k") config_.search_rrf_k = std::stoi(v);
      else if (k == "search.default_limit") config_.search_default_limit = std::stoi(v);
    }
  } catch (...) {
  }
}

void Brain::save_config_value(const std::string& key, const std::string& value) {
  auto st = db_.prepare("INSERT INTO config(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  st.bind_text(1, key);
  st.bind_text(2, value);
  st.step_done();
  load_config();
  // API keys stay DB/env only; still mirror non-secret keys to file plane.
  if (key != "embedding.api_key" && key != "chat.api_key" &&  // N39
      key != "rerank.api_key") {
    save_file_config(config_);
  }
}

std::optional<std::string> Brain::get_config_value(const std::string& key) {
  auto st = db_.prepare("SELECT value FROM config WHERE key=?");
  st.bind_text(1, key);
  if (st.step()) return st.column_text(0);
  return std::nullopt;
}

bool Brain::source_exists(const std::string& source_id) {
  auto canon = canonical_source_id(source_id);
  if (!canon) return false;
  auto st = db_.prepare("SELECT 1 FROM sources WHERE id=? LIMIT 1");
  st.bind_text(1, *canon);
  return st.step();
}

Page Brain::row_to_page(storage::Database::Statement& st) {
  Page p;
  p.id = st.column_int(0);
  p.source_id = st.column_text(1);
  p.slug = st.column_text(2);
  p.type = st.column_text(3);
  p.title = st.column_text(4);
  p.body = st.column_text(5);
  p.frontmatter_json = st.column_text(6);
  p.content_hash = st.column_text(7);
  p.created_at = st.column_text(8);
  p.updated_at = st.column_text(9);
  if (!st.column_is_null(10)) p.deleted_at = st.column_text(10);
  return p;
}

Page Brain::put_page(const PageInput& in) {
  auto sid = canonical_source_id(in.source_id.empty() ? "default" : in.source_id);
  if (!sid || !ensure_source(*sid)) {
    throw std::runtime_error("invalid source_id");
  }
  PageInput normalized = in;
  normalized.source_id = *sid;
  // version snapshot on update
  if (auto prev = get_page(normalized.slug, normalized.source_id, true)) {
    create_version(prev->id);
  }
  auto hash = util::content_hash(normalized.title, normalized.body);
  auto now = util::utc_now();
  auto sk = normalized.source_kind.empty() ? "put_page" : normalized.source_kind;
  auto via = normalized.ingested_via.empty() ? "cli" : normalized.ingested_via;
  auto st = db_.prepare(R"SQL(
INSERT INTO pages(source_id, slug, type, title, body, frontmatter_json, content_hash, updated_at, deleted_at,
  source_kind, ingested_via, ingested_at)
VALUES(?,?,?,?,?,?,?,?,NULL,?,?,?)
ON CONFLICT(source_id, slug) DO UPDATE SET
  type=excluded.type,
  title=excluded.title,
  body=excluded.body,
  frontmatter_json=excluded.frontmatter_json,
  content_hash=excluded.content_hash,
  updated_at=excluded.updated_at,
  deleted_at=NULL,
  source_kind=COALESCE(excluded.source_kind, pages.source_kind),
  ingested_via=COALESCE(excluded.ingested_via, pages.ingested_via)
)SQL");
  st.bind_text(1, normalized.source_id);
  st.bind_text(2, normalized.slug);
  st.bind_text(3, normalized.type);
  st.bind_text(4, normalized.title);
  st.bind_text(5, normalized.body);
  st.bind_text(6, normalized.frontmatter_json.empty() ? "{}" : normalized.frontmatter_json);
  st.bind_text(7, hash);
  st.bind_text(8, now);
  st.bind_text(9, sk);
  st.bind_text(10, via);
  st.bind_text(11, now);
  st.step_done();
  auto got = get_page(normalized.slug, normalized.source_id, true);
  if (!got) throw std::runtime_error("put_page failed to read back");
  return *got;
}

std::optional<Page> Brain::get_page(const std::string& slug, const std::string& source_id,
                                     bool include_deleted) {
  auto canon = canonical_source_id(source_id.empty() ? "default" : source_id);
  if (!canon) return std::nullopt;
  std::string sql =
      "SELECT id, source_id, slug, type, title, body, frontmatter_json, content_hash, "
      "created_at, updated_at, deleted_at FROM pages WHERE source_id=? AND slug=?";
  if (!include_deleted) sql += " AND deleted_at IS NULL";
  auto st = db_.prepare(sql);
  st.bind_text(1, *canon);
  st.bind_text(2, slug);
  if (!st.step()) return std::nullopt;
  return row_to_page(st);
}

std::vector<Page> Brain::list_pages(int limit, const std::string& type) {
  std::vector<Page> out;
  std::string sql =
      "SELECT id, source_id, slug, type, title, body, frontmatter_json, content_hash, "
      "created_at, updated_at, deleted_at FROM pages WHERE deleted_at IS NULL";
  if (!type.empty()) sql += " AND type=?";
  sql += " ORDER BY updated_at DESC LIMIT ?";
  auto st = db_.prepare(sql);
  int idx = 1;
  if (!type.empty()) st.bind_text(idx++, type);
  st.bind_int(idx, limit);
  while (st.step()) out.push_back(row_to_page(st));
  return out;
}

std::vector<Page> Brain::list_pages_for_source(const std::string& source_id, int limit) {
  std::vector<Page> out;
  if (limit <= 0) limit = 1;
  if (limit > 2000) limit = 2000;
  auto canon = canonical_source_id(source_id);
  if (!canon) return out;
  auto st = db_.prepare(
      "SELECT id, source_id, slug, type, title, body, frontmatter_json, content_hash, "
      "created_at, updated_at, deleted_at FROM pages "
      "WHERE source_id=? AND deleted_at IS NULL ORDER BY updated_at DESC, id DESC LIMIT ?");
  st.bind_text(1, *canon);
  st.bind_int(2, limit);
  while (st.step()) out.push_back(row_to_page(st));
  return out;
}

std::vector<Page> Brain::list_pages_for_source(const std::string& source_id, int limit,
                                               const std::string& type) {
  std::vector<Page> out;
  if (limit <= 0) limit = 1;
  if (limit > 2000) limit = 2000;
  auto canon = canonical_source_id(source_id);
  if (!canon) return out;
  auto st = db_.prepare(
      "SELECT id, source_id, slug, type, title, body, frontmatter_json, content_hash, "
      "created_at, updated_at, deleted_at FROM pages "
      "WHERE source_id=? AND type=? AND deleted_at IS NULL "
      "ORDER BY CASE WHEN updated_at >= created_at THEN updated_at ELSE created_at END DESC, "
      "id DESC LIMIT ?");
  st.bind_text(1, *canon);
  st.bind_text(2, type);
  st.bind_int(3, limit);
  while (st.step()) out.push_back(row_to_page(st));
  return out;
}

void Brain::enqueue_embed_page(int64_t page_id) {
  auto off = get_config_value("embed.auto");
  if (off && (*off == "0" || *off == "false")) return;
  const bool embedding_available =
      embedding_available_override_.value_or(!resolve_api_key(config_, false).empty());
  if (!embedding_available) return;
  json payload = {{"page_id", page_id}};
  auto st = db_.prepare(
      "INSERT INTO jobs(queue, type, status, payload_json, priority) VALUES('default','embed','waiting',?,50)");
  st.bind_text(1, payload.dump());
  st.step_done();
}

int Brain::drain_embed_jobs(int max_jobs) {
  int done_chunks = 0;
  for (int n = 0; n < max_jobs; ++n) {
    auto st = db_.prepare(
        "SELECT id, payload_json FROM jobs WHERE type='embed' AND status='waiting' "
        "ORDER BY priority ASC, id ASC LIMIT 1");
    if (!st.step()) break;
    int64_t job_id = st.column_int(0);
    auto payload = st.column_text(1);
    int64_t page_id = 0;
    try {
      auto j = json::parse(payload);
      page_id = j.at("page_id").get<int64_t>();
    } catch (...) {
      auto u = db_.prepare("UPDATE jobs SET status='failed', updated_at=? WHERE id=?");
      u.bind_text(1, util::utc_now());
      u.bind_int(2, job_id);
      u.step_done();
      continue;
    }
    {
      auto u = db_.prepare("UPDATE jobs SET status='active', updated_at=? WHERE id=?");
      u.bind_text(1, util::utc_now());
      u.bind_int(2, job_id);
      u.step_done();
    }
    auto chunks = get_chunks(page_id);
    std::vector<Chunk> missing;
    for (auto& c : chunks)
      if (c.embedding.empty()) missing.push_back(c);
    if (!missing.empty()) {
      std::vector<std::string> texts;
      for (auto& c : missing) texts.push_back(c.text);
      auto er = ai::embed_texts(config_, texts);
      if (!er.ok) {
        auto u = db_.prepare(
            "UPDATE jobs SET status='failed', result_json=?, updated_at=? WHERE id=?");
        u.bind_text(1, json({{"error", er.error}}).dump());
        u.bind_text(2, util::utc_now());
        u.bind_int(3, job_id);
        u.step_done();
        continue;
      }
      for (size_t i = 0; i < missing.size() && i < er.vectors.size(); ++i) {
        update_chunk_embedding(missing[i].id, er.vectors[i], er.model);
        ++done_chunks;
      }
    }
    auto u = db_.prepare(
        "UPDATE jobs SET status='completed', result_json=?, updated_at=? WHERE id=?");
    u.bind_text(1, json({{"chunks", done_chunks}}).dump());
    u.bind_text(2, util::utc_now());
    u.bind_int(3, job_id);
    u.step_done();
  }
  return done_chunks;
}

bool Brain::soft_delete(const std::string& slug, const std::string& source_id) {
  auto page = get_page(slug, source_id, true);
  if (page) create_version(page->id);
  auto st = db_.prepare("UPDATE pages SET deleted_at=?, updated_at=? WHERE source_id=? AND slug=? AND deleted_at IS NULL");
  auto now = util::utc_now();
  st.bind_text(1, now);
  st.bind_text(2, now);
  st.bind_text(3, source_id);
  st.bind_text(4, slug);
  st.step_done();
  return db_.changes() > 0;
}

bool Brain::restore_page(const std::string& slug, const std::string& source_id) {
  auto st = db_.prepare(
      "UPDATE pages SET deleted_at=NULL, updated_at=? WHERE source_id=? AND slug=? AND deleted_at IS NOT NULL");
  st.bind_text(1, util::utc_now());
  st.bind_text(2, source_id);
  st.bind_text(3, slug);
  st.step_done();
  return db_.changes() > 0;
}

int Brain::purge_deleted(int older_than_hours) {
  // n38: datetime('now', ?) -> C++-computed UTC cutoff bound as a parameter
  // (dialect-free SQL; stored/compared TEXT format unchanged).
  auto st = db_.prepare(
      "DELETE FROM pages WHERE deleted_at IS NOT NULL AND deleted_at < ?");
  st.bind_text(1, util::utc_now_offset(static_cast<long long>(older_than_hours) * -3600));
  st.step_done();
  return db_.changes();
}

void Brain::create_version(int64_t page_id) {
  auto st = db_.prepare(
      "INSERT INTO page_versions(page_id, source_id, slug, title, body, frontmatter_json) "
      "SELECT id, source_id, slug, title, body, frontmatter_json FROM pages WHERE id=?");
  st.bind_int(1, page_id);
  st.step_done();
}

std::vector<Page> Brain::list_versions(const std::string& slug, const std::string& source_id) {
  std::vector<Page> out;
  auto st = db_.prepare(
      "SELECT id, source_id, slug, '' AS type, title, body, frontmatter_json, '' AS ch, "
      "created_at, created_at, NULL FROM page_versions WHERE slug=? AND source_id=? "
      "ORDER BY id DESC LIMIT 50");
  st.bind_text(1, slug);
  st.bind_text(2, source_id);
  while (st.step()) {
    Page p;
    p.id = st.column_int(0);
    p.source_id = st.column_text(1);
    p.slug = st.column_text(2);
    p.title = st.column_text(4);
    p.body = st.column_text(5);
    p.frontmatter_json = st.column_text(6);
    p.created_at = st.column_text(8);
    p.updated_at = st.column_text(9);
    out.push_back(std::move(p));
  }
  return out;
}

bool Brain::revert_version(const std::string& slug, int64_t version_id,
                           const std::string& source_id) {
  auto st = db_.prepare(
      "SELECT title, body, frontmatter_json FROM page_versions WHERE id=? AND slug=? AND source_id=?");
  st.bind_int(1, version_id);
  st.bind_text(2, slug);
  st.bind_text(3, source_id);
  if (!st.step()) return false;
  PageInput in;
  in.source_id = source_id;
  in.slug = slug;
  in.title = st.column_text(0);
  in.body = st.column_text(1);
  in.frontmatter_json = st.column_text(2);
  in.source_kind = "revert_version";
  put_page(in);
  return true;
}

bool Brain::ensure_source(const std::string& source_id) {
  auto canon = canonical_source_id(source_id);
  if (!canon) return false;
  auto st = db_.prepare(
      "INSERT INTO sources(id, name) VALUES(?,?) ON CONFLICT DO NOTHING");  // n38: INSERT OR IGNORE -> ON CONFLICT DO NOTHING
  st.bind_text(1, *canon);
  st.bind_text(2, *canon);
  st.step_done();
  return true;
}

std::vector<std::string> Brain::list_source_ids() {
  std::vector<std::string> out;
  auto st = db_.prepare("SELECT id FROM sources ORDER BY id");
  while (st.step()) out.push_back(st.column_text(0));
  return out;
}

bool Brain::remove_source(const std::string& source_id, bool force) {
  auto canon = canonical_source_id(source_id);
  if (!canon || *canon == "default") return false;
  auto exists = db_.prepare("SELECT 1 FROM sources WHERE id=?");
  exists.bind_text(1, *canon);
  if (!exists.step()) return false;

  auto stc = db_.prepare("SELECT COUNT(*) FROM pages WHERE source_id=?");
  stc.bind_text(1, *canon);
  int64_t pages = 0;
  if (stc.step()) pages = stc.column_int(0);
  if (pages > 0 && !force) return false;

  try {
    db_.exec("BEGIN IMMEDIATE");
    auto links = db_.prepare("DELETE FROM links WHERE source_id=?");
    links.bind_text(1, *canon);
    links.step_done();
    if (force) {
      auto facts = db_.prepare(
          "DELETE FROM facts WHERE page_id IN (SELECT id FROM pages WHERE source_id=?)");
      facts.bind_text(1, *canon);
      facts.step_done();
      auto pages_delete = db_.prepare("DELETE FROM pages WHERE source_id=?");
      pages_delete.bind_text(1, *canon);
      pages_delete.step_done();
    }
    auto source = db_.prepare("DELETE FROM sources WHERE id=?");
    source.bind_text(1, *canon);
    source.step_done();
    const bool removed = db_.changes() > 0;
    db_.exec("COMMIT");
    return removed;
  } catch (...) {
    try {
      db_.exec("ROLLBACK");
    } catch (...) {
    }
    return false;
  }
}

Brain::SourceStatus Brain::source_status(const std::string& source_id) {
  SourceStatus s;
  auto canon = canonical_source_id(source_id);
  if (!canon) return s;
  s.id = *canon;
  {
    auto st = db_.prepare(
        "SELECT COUNT(*), COALESCE(MAX(updated_at),'') FROM pages WHERE source_id=? AND deleted_at IS NULL");
    st.bind_text(1, *canon);
    if (st.step()) {
      s.pages = st.column_int(0);
      s.last_updated = st.column_text(1);
    }
  }
  {
    auto st = db_.prepare("SELECT COUNT(*) FROM links WHERE source_id=?");
    st.bind_text(1, *canon);
    if (st.step()) s.links = st.column_int(0);
  }
  return s;
}

void Brain::add_fact(const std::string& entity_slug, const std::string& predicate,
                     const std::string& object_text, int64_t page_id) {
  auto st = db_.prepare(
      "INSERT INTO facts(page_id, entity_slug, predicate, object_text) VALUES(?,?,?,?)");
  if (page_id > 0)
    st.bind_int(1, page_id);
  else
    st.bind_null(1);
  st.bind_text(2, entity_slug);
  st.bind_text(3, predicate);
  st.bind_text(4, object_text);
  st.step_done();
}

std::vector<std::string> Brain::list_facts(const std::string& entity_slug, int limit) {
  std::vector<std::string> out;
  limit = std::clamp(limit, 0, 100);
  auto st = db_.prepare(
      "SELECT predicate || ': ' || object_text FROM facts WHERE entity_slug=? AND active=1 "
      "ORDER BY id DESC LIMIT ?");
  st.bind_text(1, entity_slug);
  st.bind_int(2, limit);
  while (st.step()) out.push_back(st.column_text(0));
  return out;
}

int Brain::extract_facts_from_page(const std::string& slug, const std::string& source_id) {
  auto page = get_page(slug, source_id);
  if (!page) return 0;
  int n = 0;
  // Heuristic: each outbound link becomes a mentions fact
  for (auto& l : get_links_from(slug, source_id)) {
    add_fact(slug, l.link_type.empty() ? "mentions" : l.link_type, l.to_slug, page->id);
    ++n;
  }
  if (!page->title.empty()) {
    add_fact(slug, "titled", page->title, page->id);
    ++n;
  }
  return n;
}

int Brain::forget_fact(const std::string& entity_slug, const std::string& predicate) {
  if (entity_slug.empty()) return 0;
  if (predicate.empty()) {
    auto st = db_.prepare("UPDATE facts SET active=0 WHERE entity_slug=? AND active=1");
    st.bind_text(1, entity_slug);
    st.step_done();
  } else {
    auto st = db_.prepare(
        "UPDATE facts SET active=0 WHERE entity_slug=? AND predicate=? AND active=1");
    st.bind_text(1, entity_slug);
    st.bind_text(2, predicate);
    st.step_done();
  }
  return db_.changes();
}

void Brain::add_tag(const std::string& slug, const std::string& tag, const std::string& source_id) {
  auto page = get_page(slug, source_id);
  if (!page) throw std::runtime_error("page not found");
  auto st = db_.prepare(
      "INSERT INTO tags(page_id, tag) VALUES(?,?) ON CONFLICT DO NOTHING");  // n38: INSERT OR IGNORE -> ON CONFLICT DO NOTHING
  st.bind_int(1, page->id);
  st.bind_text(2, tag);
  st.step_done();
}

void Brain::remove_tag(const std::string& slug, const std::string& tag, const std::string& source_id) {
  auto page = get_page(slug, source_id);
  if (!page) return;
  auto st = db_.prepare("DELETE FROM tags WHERE page_id=? AND tag=?");
  st.bind_int(1, page->id);
  st.bind_text(2, tag);
  st.step_done();
}

std::vector<std::string> Brain::get_tags(const std::string& slug, const std::string& source_id) {
  std::vector<std::string> out;
  auto page = get_page(slug, source_id);
  if (!page) return out;
  auto st = db_.prepare("SELECT tag FROM tags WHERE page_id=? ORDER BY tag");
  st.bind_int(1, page->id);
  while (st.step()) out.push_back(st.column_text(0));
  return out;
}

void Brain::remove_link(const std::string& from, const std::string& to, const std::string& source_id) {
  auto st = db_.prepare("DELETE FROM links WHERE source_id=? AND from_slug=? AND to_slug=?");
  st.bind_text(1, source_id);
  st.bind_text(2, from);
  st.bind_text(3, to);
  st.step_done();
}

std::vector<std::string> Brain::find_orphans(int limit) {
  std::vector<std::string> out;
  auto st = db_.prepare(R"SQL(
SELECT p.slug FROM pages p
WHERE p.deleted_at IS NULL
AND NOT EXISTS (SELECT 1 FROM links l WHERE l.to_slug=p.slug)
AND NOT EXISTS (SELECT 1 FROM links l2 WHERE l2.from_slug=p.slug)
ORDER BY p.updated_at DESC LIMIT ?
)SQL");
  st.bind_int(1, limit);
  while (st.step()) out.push_back(st.column_text(0));
  return out;
}

std::vector<std::string> Brain::list_brains() {
  std::vector<std::string> out;
  namespace fs = std::filesystem;
  auto root = util::brains_root();
  if (!fs::exists(root)) return out;
  for (auto& e : fs::directory_iterator(root)) {
    if (!e.is_directory()) continue;
    try {
      out.push_back(util::normalize_brain_id(e.path().filename().string()));
    } catch (...) {
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

void Brain::replace_chunks(int64_t page_id, const std::vector<std::string>& texts) {
  db_.exec("BEGIN;");
  try {
    {
      auto d = db_.prepare("DELETE FROM content_chunks WHERE page_id=?");
      d.bind_int(1, page_id);
      d.step_done();
    }
    auto ins = db_.prepare(
        "INSERT INTO content_chunks(page_id, chunk_index, text) VALUES(?,?,?)");
    for (size_t i = 0; i < texts.size(); ++i) {
      ins.reset();
      ins.clear_bindings();
      ins.bind_int(1, page_id);
      ins.bind_int(2, static_cast<int64_t>(i));
      ins.bind_text(3, texts[i]);
      ins.step_done();
    }
    db_.exec("COMMIT;");
  } catch (...) {
    try {
      db_.exec("ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

std::vector<Chunk> Brain::get_chunks(int64_t page_id) {
  std::vector<Chunk> out;
  auto st = db_.prepare(
      "SELECT id, page_id, chunk_index, text, embedding, dim, model FROM content_chunks "
      "WHERE page_id=? ORDER BY chunk_index");
  st.bind_int(1, page_id);
  while (st.step()) {
    Chunk c;
    c.id = st.column_int(0);
    c.page_id = st.column_int(1);
    c.chunk_index = static_cast<int>(st.column_int(2));
    c.text = st.column_text(3);
    if (!st.column_is_null(4)) c.embedding = search::unpack_f32(st.column_blob(4));
    if (!st.column_is_null(5)) c.dim = static_cast<int>(st.column_int(5));
    if (!st.column_is_null(6)) c.model = st.column_text(6);
    out.push_back(std::move(c));
  }
  return out;
}

std::vector<Chunk> Brain::list_chunks_missing_embedding(int limit) {
  std::vector<Chunk> out;
  auto st = db_.prepare(
      "SELECT c.id, c.page_id, c.chunk_index, c.text, c.embedding, c.dim, c.model "
      "FROM content_chunks c "
      "JOIN pages p ON p.id = c.page_id "
      "WHERE p.deleted_at IS NULL AND c.embedding IS NULL "
      "ORDER BY c.id LIMIT ?");
  st.bind_int(1, limit);
  while (st.step()) {
    Chunk c;
    c.id = st.column_int(0);
    c.page_id = st.column_int(1);
    c.chunk_index = static_cast<int>(st.column_int(2));
    c.text = st.column_text(3);
    if (!st.column_is_null(4)) c.embedding = search::unpack_f32(st.column_blob(4));
    if (!st.column_is_null(5)) c.dim = static_cast<int>(st.column_int(5));
    if (!st.column_is_null(6)) c.model = st.column_text(6);
    out.push_back(std::move(c));
  }
  return out;
}

void Brain::update_chunk_embedding(int64_t chunk_id, const std::vector<float>& emb,
                                   const std::string& model) {
  auto blob = search::pack_f32(emb);
  auto st = db_.prepare("UPDATE content_chunks SET embedding=?, dim=?, model=? WHERE id=?");
  st.bind_blob(1, blob.data(), static_cast<int>(blob.size()));
  st.bind_int(2, static_cast<int64_t>(emb.size()));
  st.bind_text(3, model);
  st.bind_int(4, chunk_id);
  st.step_done();
}

void Brain::add_link(const Link& link) {
  auto canon = canonical_source_id(link.source_id.empty() ? "default" : link.source_id);
  if (!canon) throw std::runtime_error("invalid source_id");
  Link normalized = link;
  normalized.source_id = *canon;
  auto st = db_.prepare(R"SQL(
INSERT INTO links(source_id, from_slug, to_slug, link_type, context, link_source)
VALUES(?,?,?,?,?,?)
ON CONFLICT(source_id, from_slug, to_slug, link_type, link_source) DO UPDATE SET
  context=excluded.context
)SQL");
  st.bind_text(1, normalized.source_id);
  st.bind_text(2, normalized.from_slug);
  st.bind_text(3, normalized.to_slug);
  st.bind_text(4, normalized.link_type);
  st.bind_text(5, normalized.context);
  st.bind_text(6, normalized.link_source);
  st.step_done();
}

void Brain::replace_extracted_links(const std::string& source_id, const std::string& from_slug,
                                    const std::vector<Link>& links) {
  auto canon = canonical_source_id(source_id.empty() ? "default" : source_id);
  if (!canon) throw std::runtime_error("invalid source_id");
  db_.exec("BEGIN;");
  try {
    {
      auto d = db_.prepare(
          "DELETE FROM links WHERE source_id=? AND from_slug=? AND link_source IN "
          "('markdown','wikilink')");
      d.bind_text(1, *canon);
      d.bind_text(2, from_slug);
      d.step_done();
    }
    for (const auto& l : links) {
      auto normalized = l;
      normalized.source_id = *canon;
      add_link(normalized);
    }
    db_.exec("COMMIT;");
  } catch (...) {
    try {
      db_.exec("ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

std::vector<Link> Brain::get_links_from(const std::string& slug, const std::string& source_id) {
  std::vector<Link> out;
  auto canon = canonical_source_id(source_id.empty() ? "default" : source_id);
  if (!canon) return out;
  auto st = db_.prepare(
      "SELECT id, source_id, from_slug, to_slug, link_type, context, link_source FROM links "
      "WHERE source_id=? AND from_slug=?");
  st.bind_text(1, *canon);
  st.bind_text(2, slug);
  while (st.step()) {
    Link l;
    l.id = st.column_int(0);
    l.source_id = st.column_text(1);
    l.from_slug = st.column_text(2);
    l.to_slug = st.column_text(3);
    l.link_type = st.column_text(4);
    l.context = st.column_text(5);
    l.link_source = st.column_text(6);
    out.push_back(std::move(l));
  }
  return out;
}

std::vector<Link> Brain::get_links_to(const std::string& slug, const std::string& source_id) {
  std::vector<Link> out;
  auto canon = canonical_source_id(source_id.empty() ? "default" : source_id);
  if (!canon) return out;
  auto st = db_.prepare(
      "SELECT id, source_id, from_slug, to_slug, link_type, context, link_source FROM links "
      "WHERE source_id=? AND to_slug=?");
  st.bind_text(1, *canon);
  st.bind_text(2, slug);
  while (st.step()) {
    Link l;
    l.id = st.column_int(0);
    l.source_id = st.column_text(1);
    l.from_slug = st.column_text(2);
    l.to_slug = st.column_text(3);
    l.link_type = st.column_text(4);
    l.context = st.column_text(5);
    l.link_source = st.column_text(6);
    out.push_back(std::move(l));
  }
  return out;
}

std::vector<Brain::LinkSourceCount> Brain::list_link_sources(const std::string& source_id) {
  std::vector<LinkSourceCount> out;
  auto canon = canonical_source_id(source_id.empty() ? "default" : source_id);
  if (!canon) return out;
  auto st = db_.prepare(
      "SELECT COALESCE(link_source,''), COUNT(*) FROM links "
      "WHERE source_id=? GROUP BY link_source "
      "ORDER BY COUNT(*) DESC, link_source ASC");  // n38: per-expression COLLATE BINARY -> column-level (links.link_source, schema v1); SQLite default collation is BINARY, ordering identical
  st.bind_text(1, *canon);
  while (st.step()) {
    LinkSourceCount c;
    c.source_id = *canon;
    c.link_source = st.column_text(0);
    c.count = st.column_int(1);
    out.push_back(std::move(c));
  }
  return out;
}

int64_t Brain::log_ingest(const std::string& event_type, const std::string& path,
                          const std::string& detail_json, int keep_last,
                          const std::string& source_id) {
  const auto canon = canonical_source_id(source_id.empty() ? "default" : source_id);
  if (!canon) throw std::invalid_argument("invalid source_id");
  const std::string event = event_type.empty() ? "import" : event_type;
  const std::string detail = detail_json.empty() ? "{}" : detail_json;
  if (event.size() > 64) throw std::invalid_argument("event_type too long");
  if (path.size() > 4096) throw std::invalid_argument("path too long");
  if (detail.size() > 65536) throw std::invalid_argument("detail_json too long");
  try {
    (void)json::parse(detail);
  } catch (...) {
    throw std::invalid_argument("detail_json must be valid JSON");
  }
  keep_last = std::clamp(keep_last, 1, 1000);

  db_.exec("BEGIN IMMEDIATE;");
  try {
    auto source = db_.prepare(
        "INSERT INTO sources(id,name) VALUES(?,?) ON CONFLICT DO NOTHING");  // n38: INSERT OR IGNORE -> ON CONFLICT DO NOTHING
    source.bind_text(1, *canon);
    source.bind_text(2, *canon);
    source.step_done();

    auto st = db_.prepare(
        "INSERT INTO ingest_log(source_id,event_type,path,detail_json,created_at) "
        "VALUES(?,?,?,?,?)");
    st.bind_text(1, *canon);
    st.bind_text(2, event);
    st.bind_text(3, path);
    st.bind_text(4, detail);
    st.bind_text(5, util::utc_now());
    st.step_done();
    // n38: last_insert_rowid() kept (census RETURNING rule deferred to the
    // backend layer): ingest_log.id is INTEGER PRIMARY KEY AUTOINCREMENT, so
    // SQLite semantics are unchanged; PG-mode correctness relies on N38-A's
    // PgBackend last-RETURNING tracking (audit-listed site).
    int64_t id = db_.last_insert_rowid();

    auto prune = db_.prepare(
        "DELETE FROM ingest_log WHERE source_id=? AND id NOT IN ("
        "SELECT id FROM ingest_log WHERE source_id=? "
        "ORDER BY created_at DESC, id DESC LIMIT ?)");
    prune.bind_text(1, *canon);
    prune.bind_text(2, *canon);
    prune.bind_int(3, keep_last);
    prune.step_done();
    db_.exec("COMMIT;");
    return id;
  } catch (...) {
    try {
      db_.exec("ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

std::vector<Brain::IngestLogEntry> Brain::get_ingest_log(int limit,
                                                         const std::string& source_id) {
  std::vector<IngestLogEntry> out;
  auto canon = canonical_source_id(source_id.empty() ? "default" : source_id);
  if (!canon) return out;
  limit = std::clamp(limit, 1, 50);
  auto st = db_.prepare(
      "SELECT id, source_id, event_type, path, detail_json, created_at FROM ingest_log "
      "WHERE source_id=? ORDER BY created_at DESC, id DESC LIMIT ?");
  st.bind_text(1, *canon);
  st.bind_int(2, limit);
  while (st.step()) {
    IngestLogEntry e;
    e.id = st.column_int(0);
    e.source_id = st.column_text(1);
    e.event_type = st.column_text(2);
    e.path = st.column_text(3);
    e.detail_json = st.column_text(4);
    e.created_at = st.column_text(5);
    out.push_back(std::move(e));
  }
  return out;
}

namespace {

bool all_digits_at(const std::string& s, size_t start, size_t count) {
  if (start + count > s.size()) return false;
  for (size_t i = start; i < start + count; ++i)
    if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

int digits_value(const std::string& s, size_t start, size_t count) {
  int n = 0;
  for (size_t i = start; i < start + count; ++i) n = n * 10 + (s[i] - '0');
  return n;
}

bool leap_year(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month(int year, int month) {
  static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) return leap_year(year) ? 29 : 28;
  return (month >= 1 && month <= 12) ? days[month] : 0;
}

std::optional<std::string> normalize_utc_boundary(const std::string& input,
                                                  bool allow_date_only) {
  if (input.size() == 10) {
    if (!allow_date_only || input[4] != '-' || input[7] != '-' ||
        !all_digits_at(input, 0, 4) || !all_digits_at(input, 5, 2) ||
        !all_digits_at(input, 8, 2))
      return std::nullopt;
    int year = digits_value(input, 0, 4);
    int month = digits_value(input, 5, 2);
    int day = digits_value(input, 8, 2);
    if (year < 1 || days_in_month(year, month) == 0 || day < 1 || day > days_in_month(year, month))
      return std::nullopt;
    return input + " 00:00:00";
  }
  if (input.size() != 20 || input[4] != '-' || input[7] != '-' ||
      (input[10] != 'T' && input[10] != ' ') || input[13] != ':' || input[16] != ':' ||
      input[19] != 'Z' || !all_digits_at(input, 0, 4) || !all_digits_at(input, 5, 2) ||
      !all_digits_at(input, 8, 2) || !all_digits_at(input, 11, 2) ||
      !all_digits_at(input, 14, 2) || !all_digits_at(input, 17, 2))
    return std::nullopt;
  int year = digits_value(input, 0, 4);
  int month = digits_value(input, 5, 2);
  int day = digits_value(input, 8, 2);
  int hour = digits_value(input, 11, 2);
  int minute = digits_value(input, 14, 2);
  int second = digits_value(input, 17, 2);
  if (year < 1 || days_in_month(year, month) == 0 || day < 1 || day > days_in_month(year, month) ||
      hour > 23 || minute > 59 || second > 59)
    return std::nullopt;
  std::string out = input;
  out[10] = ' ';
  out.pop_back();
  return out;
}

std::string next_utc_day(std::string day_start) {
  int year = digits_value(day_start, 0, 4);
  int month = digits_value(day_start, 5, 2);
  int day = digits_value(day_start, 8, 2);
  ++day;
  if (day > days_in_month(year, month)) {
    day = 1;
    ++month;
    if (month > 12) {
      month = 1;
      ++year;
    }
  }
  char buf[32]{};
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d 00:00:00", year, month, day);
  return buf;
}

}  // namespace

static void fill_chronicle_hit(Brain::ChronicleHit& h, storage::Database::Statement& st) {
  h.id = st.column_int(0);
  h.source_id = st.column_text(1);
  h.slug = st.column_text(2);
  h.title = st.column_text(3);
  h.updated_at = st.column_text(4);
  h.created_at = st.column_text(5);
  h.effective_at = st.column_text(6);
  h.type = st.column_text(7);
}

std::vector<Brain::ChronicleHit> Brain::chronicle_day(const std::string& day_utc, int limit,
                                                      const std::string& source_id) {
  std::vector<ChronicleHit> out;
  auto canon = canonical_source_id(source_id.empty() ? "default" : source_id);
  if (day_utc.size() != 10) return out;
  auto start = normalize_utc_boundary(day_utc, true);
  if (!canon || !start) return out;
  limit = std::clamp(limit, 1, 200);
  auto end = next_utc_day(*start);
  auto st = db_.prepare(
      "SELECT id, source_id, slug, title, updated_at, created_at, "
      "CASE WHEN updated_at >= created_at THEN updated_at ELSE created_at END AS effective_at, type "
      "FROM pages WHERE source_id=? AND deleted_at IS NULL AND "
      "((created_at>=? AND created_at<?) OR (updated_at>=? AND updated_at<?)) "
      "ORDER BY effective_at DESC, id DESC LIMIT ?");
  st.bind_text(1, *canon);
  st.bind_text(2, *start);
  st.bind_text(3, end);
  st.bind_text(4, *start);
  st.bind_text(5, end);
  st.bind_int(6, limit);
  while (st.step()) {
    ChronicleHit h;
    fill_chronicle_hit(h, st);
    out.push_back(std::move(h));
  }
  return out;
}

std::vector<Brain::ChronicleHit> Brain::chronicle_since(const std::string& since_iso, int limit,
                                                        const std::string& source_id) {
  std::vector<ChronicleHit> out;
  auto canon = canonical_source_id(source_id.empty() ? "default" : source_id);
  auto since = normalize_utc_boundary(since_iso, true);
  if (!canon || !since) return out;
  limit = std::clamp(limit, 1, 200);
  auto st = db_.prepare(
      "SELECT id, source_id, slug, title, updated_at, created_at, "
      "CASE WHEN updated_at >= created_at THEN updated_at ELSE created_at END AS effective_at, type "
      "FROM pages WHERE source_id=? AND deleted_at IS NULL AND (updated_at>=? OR created_at>=?) "
      "ORDER BY effective_at DESC, id DESC LIMIT ?");
  st.bind_text(1, *canon);
  st.bind_text(2, *since);
  st.bind_text(3, *since);
  st.bind_int(4, limit);
  while (st.step()) {
    ChronicleHit h;
    fill_chronicle_hit(h, st);
    out.push_back(std::move(h));
  }
  return out;
}

std::vector<Brain::ChronicleOnThisDayHit> Brain::chronicle_on_this_day(
    const std::string& anchor_date, int limit, const std::string& source_id,
    bool allow_virtual_leap_day) {
  const auto canon = canonical_source_id(source_id);
  const auto anchor = normalize_utc_boundary(anchor_date, true);
  if (!canon) throw std::invalid_argument("invalid source_id");
  if (!source_exists(*canon))
    throw std::invalid_argument("source_id is not registered");
  const bool virtual_leap_day =
      allow_virtual_leap_day && anchor_date.size() == 10 && anchor_date[4] == '-' &&
      anchor_date[7] == '-' && all_digits_at(anchor_date, 0, 4) &&
      anchor_date.substr(5, 5) == "02-29" && digits_value(anchor_date, 0, 4) >= 1;
  if (anchor_date.size() != 10 || (!anchor && !virtual_leap_day))
    throw std::invalid_argument("anchor_date must be a real UTC YYYY-MM-DD");

  const std::string month_day = anchor_date.substr(5, 5);
  const int anchor_year = digits_value(anchor_date, 0, 4);
  limit = std::clamp(limit, 1, 200);

  auto st = db_.prepare(
      "WITH matches AS ("
      " SELECT id, source_id, slug, title, type, created_at, updated_at,"
      // n38: CAST(... AS TEXT) is identity on SQLite's TEXT timestamps and
      // normalizes PG timestamptz to 'YYYY-MM-DD ...' so the fixed-position
      // substr extraction of MM-DD / YYYY holds on both backends.
      " CASE"
      "  WHEN substr(CAST(updated_at AS TEXT),6,5)=? AND CAST(substr(CAST(updated_at AS TEXT),1,4) AS INTEGER)<?"
      "   AND NOT (substr(CAST(created_at AS TEXT),6,5)=? AND CAST(substr(CAST(created_at AS TEXT),1,4) AS INTEGER)<?"
      "            AND created_at>updated_at) THEN updated_at"
      "  WHEN substr(CAST(created_at AS TEXT),6,5)=? AND CAST(substr(CAST(created_at AS TEXT),1,4) AS INTEGER)<?"
      "   THEN created_at ELSE NULL END AS matched_at"
      " FROM pages WHERE source_id=? AND deleted_at IS NULL"
      ")"
      " SELECT id, source_id, slug, title, type, created_at, updated_at, matched_at"
      " FROM matches WHERE matched_at IS NOT NULL"
      " ORDER BY matched_at DESC, id DESC LIMIT ?");
  st.bind_text(1, month_day);
  st.bind_int(2, anchor_year);
  st.bind_text(3, month_day);
  st.bind_int(4, anchor_year);
  st.bind_text(5, month_day);
  st.bind_int(6, anchor_year);
  st.bind_text(7, *canon);
  st.bind_int(8, limit);

  std::vector<ChronicleOnThisDayHit> out;
  while (st.step()) {
    ChronicleOnThisDayHit hit;
    hit.id = st.column_int(0);
    hit.source_id = st.column_text(1);
    hit.slug = st.column_text(2);
    hit.title = st.column_text(3);
    hit.type = st.column_text(4);
    hit.created_at = st.column_text(5);
    hit.updated_at = st.column_text(6);
    hit.matched_at = st.column_text(7);
    if (hit.matched_at.size() < 4 || !all_digits_at(hit.matched_at, 0, 4))
      throw std::runtime_error("invalid Chronicle timestamp in storage");
    hit.years_ago = anchor_year - digits_value(hit.matched_at, 0, 4);
    out.push_back(std::move(hit));
  }
  return out;
}

std::optional<Brain::ChronicleLastSeen> Brain::chronicle_last_seen(
    const std::string& entity, const std::string& source_id) {
  const auto canon = canonical_source_id(source_id);
  if (!canon) throw std::invalid_argument("invalid source_id");
  if (!source_exists(*canon))
    throw std::invalid_argument("source_id is not registered");
  if (entity.empty()) throw std::invalid_argument("entity is required");
  if (entity.size() > 4096 || !is_valid_utf8_text(entity))
    throw std::invalid_argument("entity must be valid UTF-8 within 4096 bytes");

  auto st = db_.prepare(
      "SELECT CASE WHEN updated_at>=created_at THEN updated_at ELSE created_at END"
      " FROM pages WHERE source_id=? AND slug=? AND deleted_at IS NULL LIMIT 1");
  st.bind_text(1, *canon);
  st.bind_text(2, entity);
  if (!st.step()) return std::nullopt;
  ChronicleLastSeen result;
  result.source_id = *canon;
  result.entity = entity;
  result.last_seen = st.column_text(0);
  return result;
}

Brain::ChronicleBackfillResult Brain::chronicle_backfill(
    const std::string& source_id, const std::optional<std::string>& since,
    int limit, bool dry_run) {
  const auto canon = canonical_source_id(source_id);
  if (!canon) throw std::invalid_argument("invalid source_id");
  if (!source_exists(*canon))
    throw std::invalid_argument("source_id is not registered");

  std::optional<std::string> normalized_since;
  if (since) {
    normalized_since = normalize_utc_boundary(*since, true);
    if (!normalized_since) throw std::invalid_argument("since must be a UTC date or timestamp");
  }
  limit = std::clamp(limit, 1, 1000);

  ChronicleBackfillResult result;
  result.source_id = *canon;
  result.dry_run = dry_run;

  const auto select_eligible = [&]() {
    storage::Database::Statement st;
    if (normalized_since) {
      st = db_.prepare(
          "SELECT p.id, EXISTS(SELECT 1 FROM tags t WHERE t.page_id=p.id AND t.tag='chronicle')"
          " FROM pages p WHERE p.source_id=? AND p.deleted_at IS NULL"
          " AND p.type IN ('meeting','conversation','calendar-event')"
          " AND (CASE WHEN p.updated_at>=p.created_at THEN p.updated_at ELSE p.created_at END)>=?"
          " ORDER BY CASE WHEN p.updated_at>=p.created_at THEN p.updated_at ELSE p.created_at END DESC,"
          " p.id DESC LIMIT ?");
      st.bind_text(1, *canon);
      st.bind_text(2, *normalized_since);
      st.bind_int(3, limit);
    } else {
      st = db_.prepare(
          "SELECT p.id, EXISTS(SELECT 1 FROM tags t WHERE t.page_id=p.id AND t.tag='chronicle')"
          " FROM pages p WHERE p.source_id=? AND p.deleted_at IS NULL"
          " AND p.type IN ('meeting','conversation','calendar-event')"
          " ORDER BY CASE WHEN p.updated_at>=p.created_at THEN p.updated_at ELSE p.created_at END DESC,"
          " p.id DESC LIMIT ?");
      st.bind_text(1, *canon);
      st.bind_int(2, limit);
    }
    std::vector<std::pair<int64_t, bool>> rows;
    while (st.step()) rows.emplace_back(st.column_int(0), st.column_int(1) != 0);
    return rows;
  };

  const auto apply_rows = [&](const std::vector<std::pair<int64_t, bool>>& selected) {
    for (const auto& [page_id, already_tagged] : selected) {
      ++result.scanned;
      ++result.eligible;
      if (already_tagged) {
        ++result.already_tagged;
        continue;
      }
      if (!dry_run) {
        auto insert = db_.prepare("INSERT INTO tags(page_id,tag) VALUES(?,'chronicle')");
        insert.bind_int(1, page_id);
        insert.step_done();
        if (db_.changes() != 1)
          throw std::runtime_error("chronicle tag insert produced no change");
        ++result.tagged;
      }
    }
  };

  if (dry_run) {
    apply_rows(select_eligible());
    return result;
  }

  db_.exec("BEGIN IMMEDIATE;");
  try {
    apply_rows(select_eligible());
    db_.exec("COMMIT;");
    return result;
  } catch (...) {
    try {
      db_.exec("ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

std::vector<Brain::ChronicleHit> Brain::chronicle_on_this_day(const std::string& mmdd, int limit) {
  std::string anchor = mmdd;
  if (anchor.empty()) anchor = util::utc_date();
  if (anchor.size() == 5) anchor = util::utc_date().substr(0, 5) + anchor;
  std::vector<ChronicleHit> out;
  const bool virtual_leap_day = mmdd.size() == 5 && mmdd == "02-29";
  for (const auto& detailed : chronicle_on_this_day(anchor, limit, "default",
                                                    virtual_leap_day)) {
    ChronicleHit h;
    h.id = detailed.id;
    h.source_id = detailed.source_id;
    h.slug = detailed.slug;
    h.title = detailed.title;
    h.updated_at = detailed.updated_at;
    h.created_at = detailed.created_at;
    h.effective_at = detailed.matched_at;
    h.type = detailed.type;
    out.push_back(std::move(h));
  }
  return out;
}

std::string Brain::chronicle_last_seen(const std::string& slug) {
  if (slug.empty()) return {};
  auto result = chronicle_last_seen(slug, "default");
  return result ? result->last_seen : std::string{};
}

int Brain::chronicle_backfill(int limit) {
  return chronicle_backfill("default", std::nullopt, limit, false).tagged;
}

int64_t Brain::put_take(const std::string& entity_slug, const std::string& body,
                        const std::string& kind, double score) {
  if (entity_slug.empty() || body.empty()) return 0;
  auto st = db_.prepare(
      "INSERT INTO takes(entity_slug, kind, body, score, created_at) VALUES(?,?,?,?,?)");
  st.bind_text(1, entity_slug);
  st.bind_text(2, kind.empty() ? "fact" : kind);
  st.bind_text(3, body);
  st.bind_double(4, score);
  st.bind_text(5, util::utc_now());
  st.step_done();
  // n38: last_insert_rowid() kept (census RETURNING rule deferred to the
  // backend layer): takes.id is INTEGER PRIMARY KEY AUTOINCREMENT, so SQLite
  // semantics are unchanged; PG-mode correctness relies on N38-A's PgBackend
  // last-RETURNING tracking (audit-listed site).
  return db_.last_insert_rowid();
}

std::vector<Brain::Take> Brain::takes_list(const std::string& entity_slug, int limit) {
  std::vector<Take> out;
  if (limit <= 0) limit = 50;
  if (entity_slug.empty()) {
    auto st = db_.prepare(
        "SELECT id, entity_slug, kind, body, score, created_at FROM takes "
        "WHERE active=1 ORDER BY id DESC LIMIT ?");
    st.bind_int(1, limit);
    while (st.step()) {
      Take t;
      t.id = st.column_int(0);
      t.entity_slug = st.column_text(1);
      t.kind = st.column_text(2);
      t.body = st.column_text(3);
      t.score = st.column_double(4);
      t.created_at = st.column_text(5);
      out.push_back(std::move(t));
    }
  } else {
    auto st = db_.prepare(
        "SELECT id, entity_slug, kind, body, score, created_at FROM takes "
        "WHERE active=1 AND entity_slug=? ORDER BY id DESC LIMIT ?");
    st.bind_text(1, entity_slug);
    st.bind_int(2, limit);
    while (st.step()) {
      Take t;
      t.id = st.column_int(0);
      t.entity_slug = st.column_text(1);
      t.kind = st.column_text(2);
      t.body = st.column_text(3);
      t.score = st.column_double(4);
      t.created_at = st.column_text(5);
      out.push_back(std::move(t));
    }
  }
  return out;
}

std::vector<Brain::Take> Brain::takes_search(const std::string& query, int limit) {
  std::vector<Take> out;
  if (query.empty()) return takes_list("", limit);
  if (limit <= 0) limit = 50;
  std::string like = "%" + query + "%";
  auto st = db_.prepare(
      "SELECT id, entity_slug, kind, body, score, created_at FROM takes "
      "WHERE active=1 AND (body LIKE ? OR entity_slug LIKE ?) ORDER BY id DESC LIMIT ?");
  st.bind_text(1, like);
  st.bind_text(2, like);
  st.bind_int(3, limit);
  while (st.step()) {
    Take t;
    t.id = st.column_int(0);
    t.entity_slug = st.column_text(1);
    t.kind = st.column_text(2);
    t.body = st.column_text(3);
    t.score = st.column_double(4);
    t.created_at = st.column_text(5);
    out.push_back(std::move(t));
  }
  return out;
}

int Brain::takes_promote_facts(int limit) {
  if (limit <= 0) limit = 100;
  auto st = db_.prepare(
      "SELECT entity_slug, predicate || ': ' || object_text FROM facts WHERE active=1 "
      "ORDER BY id DESC LIMIT ?");
  st.bind_int(1, limit);
  int n = 0;
  while (st.step()) {
    auto slug = st.column_text(0);
    auto body = st.column_text(1);
    if (put_take(slug, body, "fact", 0.0) > 0) ++n;
  }
  return n;
}

bool Brain::put_raw_data(const std::string& key, const std::string& content_text,
                         const std::string& meta_json) {
  if (key.empty()) return false;
  auto st = db_.prepare(
      "INSERT INTO raw_data(key, content_text, meta_json, created_at) VALUES(?,?,?,?) "
      "ON CONFLICT(key) DO UPDATE SET content_text=excluded.content_text, "
      "meta_json=excluded.meta_json, created_at=excluded.created_at");
  st.bind_text(1, key);
  st.bind_text(2, content_text);
  st.bind_text(3, meta_json.empty() ? "{}" : meta_json);
  st.bind_text(4, util::utc_now());
  st.step_done();
  return true;
}

std::optional<std::pair<std::string, std::string>> Brain::get_raw_data(const std::string& key) {
  auto st = db_.prepare("SELECT content_text, meta_json FROM raw_data WHERE key=?");
  st.bind_text(1, key);
  if (!st.step()) return std::nullopt;
  return std::make_pair(st.column_text(0), st.column_text(1));
}

std::vector<std::pair<std::string, std::string>> Brain::list_raw_prefix(const std::string& prefix,
                                                                        int limit) {
  std::vector<std::pair<std::string, std::string>> out;
  if (limit <= 0) limit = 50;
  auto st = db_.prepare(
      "SELECT key, content_text FROM raw_data WHERE key LIKE ? ORDER BY id DESC LIMIT ?");
  st.bind_text(1, prefix + "%");
  st.bind_int(2, limit);
  while (st.step()) out.emplace_back(st.column_text(0), st.column_text(1));
  return out;
}

BrainStats Brain::stats() {
  BrainStats s;
  auto q = [&](const char* sql) {
    auto st = db_.prepare(sql);
    if (st.step()) return st.column_int(0);
    return int64_t{0};
  };
  s.pages = q("SELECT COUNT(*) FROM pages WHERE deleted_at IS NULL");
  s.chunks = q("SELECT COUNT(*) FROM content_chunks");
  s.links = q("SELECT COUNT(*) FROM links");
  s.embedded_chunks = q("SELECT COUNT(*) FROM content_chunks WHERE embedding IS NOT NULL");
  return s;
}

Brain::SourceIdentitySnapshot Brain::source_identity_snapshot(const std::string& source_id) {
  const auto canon = canonical_source_id(source_id);
  if (!canon) throw std::invalid_argument("invalid source_id");

  const auto integrity = storage::check_schema_integrity(db_);
  if (!integrity.ok) throw std::runtime_error("schema integrity check failed");

  auto st = db_.prepare(
      "SELECT "
      "(SELECT COUNT(*) FROM pages WHERE source_id=?1 AND deleted_at IS NULL), "
      "(SELECT COUNT(*) FROM content_chunks AS c "
      " JOIN pages AS p ON p.id=c.page_id WHERE p.source_id=?1), "
      "(SELECT COUNT(*) FROM links WHERE source_id=?1), "
      "(SELECT COUNT(*) FROM content_chunks AS c "
      " JOIN pages AS p ON p.id=c.page_id "
      " WHERE p.source_id=?1 AND c.embedding IS NOT NULL)");
  st.bind_text(1, *canon);
  if (!st.step()) throw std::runtime_error("source identity query failed");

  SourceIdentitySnapshot snapshot;
  snapshot.source_id = *canon;
  snapshot.schema_version = integrity.schema_version;
  snapshot.pages = st.column_int(0);
  snapshot.chunks = st.column_int(1);
  snapshot.links = st.column_int(2);
  snapshot.embedded_chunks = st.column_int(3);
  return snapshot;
}

HealthReport Brain::health() {
  HealthReport h;
  h.db_path = util::path_to_utf8(util::brain_db_path(brain_id_));
  auto integ = storage::check_schema_integrity(db_);
  h.schema_version = integ.schema_version;
  if (!integ.ok) {
    h.ok = false;
    h.notes.push_back("schema DEGRADED (missing objects)");
    for (auto& m : integ.missing) h.notes.push_back("  missing " + m);
    h.notes.push_back("repair: reopen after upgrade (migration v3) or re-init brain");
  }
  if (h.schema_version < 1) {
    h.ok = false;
    h.notes.push_back("schema not migrated");
  }
  try {
    h.stats = stats();
  } catch (const std::exception& e) {
    h.ok = false;
    h.notes.push_back(std::string("stats unavailable: ") + e.what());
  }
  if (h.stats.pages == 0) h.notes.push_back("empty brain: import or capture notes");
  if (h.stats.chunks > 0 && h.stats.embedded_chunks == 0)
    h.notes.push_back("no embeddings yet (FTS-only search works; run: qbrain embed --all)");
  if (h.stats.embedded_chunks > 5000)
    h.notes.push_back(
        "vector search scans all embedded chunks in memory; >5k chunks may be slow "
        "(use FTS --no-vector or Phase-2 ANN index)");
  auto key = resolve_api_key(config_, false);
  if (key.empty()) h.notes.push_back("no embedding API key (set OPENAI_API_KEY or qbrain config set embedding.api_key)");
  return h;
}

Brain::StatusSnapshot Brain::status_snapshot() {
  StatusSnapshot s;
  auto integ = storage::check_schema_integrity(db_);
  if (!integ.ok) {
    std::string message = "schema integrity check failed";
    if (!integ.missing.empty()) message += ": " + integ.missing.front();
    throw std::runtime_error(message);
  }
  auto st_stats = stats();
  s.pages = st_stats.pages;
  s.chunks = st_stats.chunks;
  s.links = st_stats.links;
  s.embedded_chunks = st_stats.embedded_chunks;
  s.schema_version = integ.schema_version;
  auto jc = jobs::count_jobs(*this);
  s.jobs_waiting = jc.waiting;
  s.jobs_active = jc.active;
  s.jobs_failed = jc.failed;
  s.jobs_paused = jc.paused;
  return s;
}

Brain::RemediateReport Brain::remediate(std::optional<bool> embedding_available_override) {
  RemediateReport r;
  const auto effective_override =
      embedding_available_override ? embedding_available_override : embedding_available_override_;
  r.api_key_present = effective_override.value_or(!resolve_api_key(config_, false).empty());
  db_.exec("BEGIN IMMEDIATE;");
  try {
    r.default_source = ensure_source("default");
    if (!r.default_source) throw std::runtime_error("ensure default source failed");
    r.reclaimed = jobs::reclaim_stalled(*this, "default");

    std::set<int64_t> missing_page_ids;
    auto missing = db_.prepare(
        "SELECT DISTINCT p.id FROM pages p "
        "JOIN content_chunks c ON c.page_id=p.id "
        "WHERE p.deleted_at IS NULL AND c.embedding IS NULL ORDER BY p.id");
    while (missing.step()) missing_page_ids.insert(missing.column_int(0));

    if (!r.api_key_present) {
      r.notes.push_back("embedding unavailable; skipped embed re-enqueue");
    } else {
      std::set<int64_t> pending_page_ids;
      auto pending = db_.prepare(
          "SELECT payload_json FROM jobs WHERE type='embed' "
          "AND status IN ('waiting','active','paused')");
      while (pending.step()) {
        try {
          auto payload = json::parse(pending.column_text(0));
          if (!payload.is_object() || !payload.contains("page_id") ||
              !payload["page_id"].is_number_integer())
            continue;
          auto page_id = payload["page_id"].get<int64_t>();
          if (page_id > 0) pending_page_ids.insert(page_id);
        } catch (...) {
        }
      }

      for (auto page_id : missing_page_ids) {
        if (pending_page_ids.count(page_id) != 0) continue;
        auto insert = db_.prepare(
            "INSERT INTO jobs(queue,type,status,payload_json,priority) "
            "VALUES('default','embed','waiting',?,50)");
        insert.bind_text(1, json({{"page_id", page_id}}).dump());
        insert.step_done();
        pending_page_ids.insert(page_id);
        ++r.embed_jobs_enqueued;
      }
    }

    if (missing_page_ids.empty())
      r.notes.push_back("no chunks missing embeddings");
    else if (r.api_key_present && r.embed_jobs_enqueued == 0)
      r.notes.push_back("missing embeddings already have pending embed jobs");
    db_.exec("COMMIT;");
    return r;
  } catch (...) {
    try {
      db_.exec("ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

}  // namespace qbrain

// ---- N38 PG-mode open seams (tests/test_n38.cpp A/B landing seams) --------
// Declared by the n38 suite and defined here (slice B): the only non-test
// entry points that construct the storage facade on the PG backend. In a
// build without the PG backend they fail loudly instead of degrading to the
// SQLite default (an opt-in backend must never silently fall back).
void n38_open_pg(qbrain::storage::Database& db, const std::string& dsn) {
#if defined(QBRAIN_WITH_PG)
  auto backend = qbrain::storage::make_pg_backend(dsn);
  qbrain::storage::pg_ensure_schema(qbrain::storage::pg_conn_of(*backend));
  db.adopt_backend(std::move(backend));
#else
  (void)db;
  (void)dsn;
  throw std::runtime_error(
      "n38_open_pg: this build was compiled without the PostgreSQL backend "
      "(QBRAIN_WITH_PG off)");
#endif
}

void n38_open_pg_brain(qbrain::Brain& brain, const std::string& dsn) {
  brain.open_pg(dsn);
}
