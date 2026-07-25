#include "qbrain/core/brain.hpp"
#include "qbrain/ai/embed.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/paths.hpp"
#include "qbrain/util/time_util.hpp"
#include "qbrain/search/vector.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace qbrain {

Brain::Brain(std::string brain_id) : brain_id_(std::move(brain_id)) {}

void Brain::open() {
  util::ensure_dir(util::brain_dir(brain_id_));
  util::ensure_dir(util::qbrain_root());
  open_at(util::path_to_utf8(util::brain_db_path(brain_id_)));
}

void Brain::open_at(const std::string& db_path) {
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
}

void Brain::close() { db_.close(); }
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
  if (key != "embedding.api_key" && key != "chat.api_key") {
    save_file_config(config_);
  }
}

std::optional<std::string> Brain::get_config_value(const std::string& key) {
  auto st = db_.prepare("SELECT value FROM config WHERE key=?");
  st.bind_text(1, key);
  if (st.step()) return st.column_text(0);
  return std::nullopt;
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
  if (!ensure_source(in.source_id.empty() ? "default" : in.source_id)) {
    throw std::runtime_error("invalid source_id");
  }
  // version snapshot on update
  if (auto prev = get_page(in.slug, in.source_id.empty() ? "default" : in.source_id, true)) {
    create_version(prev->id);
  }
  auto hash = util::content_hash(in.title, in.body);
  auto now = util::utc_now();
  auto sk = in.source_kind.empty() ? "put_page" : in.source_kind;
  auto via = in.ingested_via.empty() ? "cli" : in.ingested_via;
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
  st.bind_text(1, in.source_id);
  st.bind_text(2, in.slug);
  st.bind_text(3, in.type);
  st.bind_text(4, in.title);
  st.bind_text(5, in.body);
  st.bind_text(6, in.frontmatter_json.empty() ? "{}" : in.frontmatter_json);
  st.bind_text(7, hash);
  st.bind_text(8, now);
  st.bind_text(9, sk);
  st.bind_text(10, via);
  st.bind_text(11, now);
  st.step_done();
  auto got = get_page(in.slug, in.source_id, true);
  if (!got) throw std::runtime_error("put_page failed to read back");
  return *got;
}

std::optional<Page> Brain::get_page(const std::string& slug, const std::string& source_id,
                                    bool include_deleted) {
  std::string sql =
      "SELECT id, source_id, slug, type, title, body, frontmatter_json, content_hash, "
      "created_at, updated_at, deleted_at FROM pages WHERE source_id=? AND slug=?";
  if (!include_deleted) sql += " AND deleted_at IS NULL";
  auto st = db_.prepare(sql);
  st.bind_text(1, source_id);
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

void Brain::enqueue_embed_page(int64_t page_id) {
  auto off = get_config_value("embed.auto");
  if (off && (*off == "0" || *off == "false")) return;
  if (resolve_api_key(config_, false).empty()) return;
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
  // SQLite datetime: deleted_at older than now - hours
  auto st = db_.prepare(
      "DELETE FROM pages WHERE deleted_at IS NOT NULL AND "
      "deleted_at < datetime('now', ?)");
  std::string mod = "-" + std::to_string(older_than_hours) + " hours";
  st.bind_text(1, mod);
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
  if (source_id.empty()) return false;
  // reject path traversal-ish
  if (source_id.find("..") != std::string::npos || source_id.find('/') != std::string::npos ||
      source_id.find('\\') != std::string::npos)
    return false;
  auto st = db_.prepare("INSERT OR IGNORE INTO sources(id, name) VALUES(?,?)");
  st.bind_text(1, source_id);
  st.bind_text(2, source_id);
  st.step_done();
  return true;
}

std::vector<std::string> Brain::list_source_ids() {
  std::vector<std::string> out;
  auto st = db_.prepare("SELECT id FROM sources ORDER BY id");
  while (st.step()) out.push_back(st.column_text(0));
  return out;
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

void Brain::add_tag(const std::string& slug, const std::string& tag, const std::string& source_id) {
  auto page = get_page(slug, source_id);
  if (!page) throw std::runtime_error("page not found");
  auto st = db_.prepare("INSERT OR IGNORE INTO tags(page_id, tag) VALUES(?,?)");
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
    if (e.is_directory()) out.push_back(e.path().filename().string());
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
  auto st = db_.prepare(R"SQL(
INSERT INTO links(source_id, from_slug, to_slug, link_type, context, link_source)
VALUES(?,?,?,?,?,?)
ON CONFLICT(source_id, from_slug, to_slug, link_type, link_source) DO UPDATE SET
  context=excluded.context
)SQL");
  st.bind_text(1, link.source_id);
  st.bind_text(2, link.from_slug);
  st.bind_text(3, link.to_slug);
  st.bind_text(4, link.link_type);
  st.bind_text(5, link.context);
  st.bind_text(6, link.link_source);
  st.step_done();
}

void Brain::replace_extracted_links(const std::string& source_id, const std::string& from_slug,
                                    const std::vector<Link>& links) {
  db_.exec("BEGIN;");
  try {
    {
      auto d = db_.prepare(
          "DELETE FROM links WHERE source_id=? AND from_slug=? AND link_source IN "
          "('markdown','wikilink')");
      d.bind_text(1, source_id);
      d.bind_text(2, from_slug);
      d.step_done();
    }
    for (const auto& l : links) add_link(l);
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
  auto st = db_.prepare(
      "SELECT id, source_id, from_slug, to_slug, link_type, context, link_source FROM links "
      "WHERE source_id=? AND from_slug=?");
  st.bind_text(1, source_id);
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
  auto st = db_.prepare(
      "SELECT id, source_id, from_slug, to_slug, link_type, context, link_source FROM links "
      "WHERE source_id=? AND to_slug=?");
  st.bind_text(1, source_id);
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

HealthReport Brain::health() {
  HealthReport h;
  h.db_path = util::path_to_utf8(util::brain_db_path(brain_id_));
  h.stats = stats();
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

}  // namespace qbrain
