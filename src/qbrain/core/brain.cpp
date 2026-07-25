#include "qbrain/core/brain.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/paths.hpp"
#include "qbrain/util/time_util.hpp"
#include "qbrain/search/vector.hpp"
#include <nlohmann/json.hpp>
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
  // locate schema relative to cwd or executable-adjacent
  std::vector<std::string> candidates;
  if (const char* env = std::getenv("QBRAIN_SCHEMA")) candidates.emplace_back(env);
  candidates.push_back("schema/001_init.sql");
  candidates.push_back("../schema/001_init.sql");
  candidates.push_back("../../schema/001_init.sql");
  // next to executable (best-effort via current path variants)
  candidates.push_back("./schema/001_init.sql");
  std::string schema_path;
  for (const auto& c : candidates) {
    std::ifstream t(c);
    if (t.good()) {
      schema_path = c;
      break;
    }
  }
  if (schema_path.empty()) {
    // embed minimal schema fallback
    db_.exec(R"SQL(
CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY, applied_at TEXT DEFAULT (datetime('now')));
CREATE TABLE IF NOT EXISTS sources (id TEXT PRIMARY KEY, name TEXT, local_path TEXT, config_json TEXT DEFAULT '{}', created_at TEXT DEFAULT (datetime('now')), last_sync_at TEXT);
CREATE TABLE IF NOT EXISTS pages (id INTEGER PRIMARY KEY AUTOINCREMENT, source_id TEXT NOT NULL DEFAULT 'default', slug TEXT NOT NULL, type TEXT NOT NULL DEFAULT 'note', title TEXT NOT NULL DEFAULT '', body TEXT NOT NULL DEFAULT '', frontmatter_json TEXT NOT NULL DEFAULT '{}', content_hash TEXT, created_at TEXT DEFAULT (datetime('now')), updated_at TEXT DEFAULT (datetime('now')), deleted_at TEXT, UNIQUE(source_id, slug));
CREATE TABLE IF NOT EXISTS content_chunks (id INTEGER PRIMARY KEY AUTOINCREMENT, page_id INTEGER NOT NULL, chunk_index INTEGER NOT NULL, text TEXT NOT NULL, embedding BLOB, dim INTEGER, model TEXT, UNIQUE(page_id, chunk_index));
CREATE TABLE IF NOT EXISTS links (id INTEGER PRIMARY KEY AUTOINCREMENT, source_id TEXT NOT NULL DEFAULT 'default', from_slug TEXT NOT NULL, to_slug TEXT NOT NULL, link_type TEXT NOT NULL DEFAULT 'related', context TEXT NOT NULL DEFAULT '', link_source TEXT NOT NULL DEFAULT 'markdown', created_at TEXT DEFAULT (datetime('now')), UNIQUE(source_id, from_slug, to_slug, link_type, link_source));
CREATE TABLE IF NOT EXISTS tags (page_id INTEGER NOT NULL, tag TEXT NOT NULL, PRIMARY KEY(page_id, tag));
CREATE TABLE IF NOT EXISTS config (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS jobs (id INTEGER PRIMARY KEY AUTOINCREMENT, queue TEXT NOT NULL DEFAULT 'default', type TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'waiting', payload_json TEXT NOT NULL DEFAULT '{}', result_json TEXT, priority INTEGER NOT NULL DEFAULT 100, attempts INTEGER NOT NULL DEFAULT 0, created_at TEXT DEFAULT (datetime('now')), updated_at TEXT DEFAULT (datetime('now')), lock_until TEXT);
CREATE VIRTUAL TABLE IF NOT EXISTS pages_fts USING fts5(slug, title, body, content='pages', content_rowid='id', tokenize='unicode61');
CREATE TRIGGER IF NOT EXISTS pages_ai AFTER INSERT ON pages BEGIN INSERT INTO pages_fts(rowid, slug, title, body) VALUES (new.id, new.slug, new.title, new.body); END;
CREATE TRIGGER IF NOT EXISTS pages_ad AFTER DELETE ON pages BEGIN INSERT INTO pages_fts(pages_fts, rowid, slug, title, body) VALUES ('delete', old.id, old.slug, old.title, old.body); END;
CREATE TRIGGER IF NOT EXISTS pages_au AFTER UPDATE ON pages BEGIN INSERT INTO pages_fts(pages_fts, rowid, slug, title, body) VALUES ('delete', old.id, old.slug, old.title, old.body); INSERT INTO pages_fts(rowid, slug, title, body) VALUES (new.id, new.slug, new.title, new.body); END;
INSERT OR IGNORE INTO sources(id, name) VALUES ('default', 'Default Source');
INSERT OR IGNORE INTO schema_version(version) VALUES (1);
)SQL");
  } else {
    storage::apply_migrations(db_, schema_path);
  }
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
  auto hash = util::content_hash(in.title, in.body);
  auto now = util::utc_now();
  auto st = db_.prepare(R"SQL(
INSERT INTO pages(source_id, slug, type, title, body, frontmatter_json, content_hash, updated_at, deleted_at)
VALUES(?,?,?,?,?,?,?,?,NULL)
ON CONFLICT(source_id, slug) DO UPDATE SET
  type=excluded.type,
  title=excluded.title,
  body=excluded.body,
  frontmatter_json=excluded.frontmatter_json,
  content_hash=excluded.content_hash,
  updated_at=excluded.updated_at,
  deleted_at=NULL
)SQL");
  st.bind_text(1, in.source_id);
  st.bind_text(2, in.slug);
  st.bind_text(3, in.type);
  st.bind_text(4, in.title);
  st.bind_text(5, in.body);
  st.bind_text(6, in.frontmatter_json.empty() ? "{}" : in.frontmatter_json);
  st.bind_text(7, hash);
  st.bind_text(8, now);
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

bool Brain::soft_delete(const std::string& slug, const std::string& source_id) {
  auto st = db_.prepare("UPDATE pages SET deleted_at=?, updated_at=? WHERE source_id=? AND slug=? AND deleted_at IS NULL");
  auto now = util::utc_now();
  st.bind_text(1, now);
  st.bind_text(2, now);
  st.bind_text(3, source_id);
  st.bind_text(4, slug);
  st.step_done();
  return db_.changes() > 0;
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
  auto st = db_.prepare("SELECT COALESCE(MAX(version),0) FROM schema_version");
  if (st.step()) h.schema_version = static_cast<int>(st.column_int(0));
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
