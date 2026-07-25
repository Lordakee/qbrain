#include "qbrain/ops/registry.hpp"
#include "qbrain/ai/chat.hpp"
#include "qbrain/ai/embed.hpp"
#include "qbrain/graph/extract.hpp"
#include "qbrain/graph/traverse.hpp"
#include "qbrain/ingest/chunker.hpp"
#include "qbrain/ingest/import.hpp"
#include "qbrain/search/hybrid.hpp"
#include "qbrain/util/string_util.hpp"
#include "qbrain/util/time_util.hpp"
#include "qbrain/util/hash.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace qbrain::ops {
namespace {

std::string arg(OpContext& ctx, const std::string& k, const std::string& def = {}) {
  auto it = ctx.args.find(k);
  return it == ctx.args.end() ? def : it->second;
}

int arg_int(OpContext& ctx, const std::string& k, int def) {
  auto s = arg(ctx, k);
  if (s.empty()) return def;
  try {
    return std::stoi(s);
  } catch (...) {
    return def;
  }
}

void register_one(const char* name, Scope scope, OpHandler h, bool local_only = false,
                  const char* description = "", const char* input_schema = "") {
  Operation op;
  op.name = name;
  op.scope = scope;
  op.local_only = local_only;
  op.description = description ? description : "";
  op.input_schema_json =
      (input_schema && *input_schema) ? input_schema
                                      : R"({"type":"object","properties":{}})";
  op.handler = std::move(h);
  global_registry().add(std::move(op));
}

}  // namespace

void register_builtin_ops() {
  register_one(
      "get_health", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto h = ctx.brain->health();
    json j;
    j["ok"] = h.ok;
    j["db_path"] = h.db_path;
    j["schema_version"] = h.schema_version;
    j["stats"] = {{"pages", h.stats.pages},
                  {"chunks", h.stats.chunks},
                  {"links", h.stats.links},
                  {"embedded_chunks", h.stats.embedded_chunks}};
    j["notes"] = h.notes;
    r.json = j.dump(2);
    std::ostringstream oss;
    oss << "Qbrain doctor: " << (h.ok ? "OK" : "DEGRADED") << "\n"
        << "  db: " << h.db_path << "\n"
        << "  schema: v" << h.schema_version << "\n"
        << "  pages=" << h.stats.pages << " chunks=" << h.stats.chunks
        << " links=" << h.stats.links << " embedded=" << h.stats.embedded_chunks << "\n";
    for (auto& n : h.notes) oss << "  - " << n << "\n";
    r.text = oss.str();
    r.ok = h.ok;
    return r;
  }, false, "Brain health / doctor report", R"({"type":"object","properties":{}})");

  register_one(
      "get_stats", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto s = ctx.brain->stats();
    json j = {{"pages", s.pages},
              {"chunks", s.chunks},
              {"links", s.links},
              {"embedded_chunks", s.embedded_chunks}};
    r.json = j.dump(2);
    r.text = r.json;
    return r;
  }, false, "Page/chunk/link statistics", R"({"type":"object","properties":{}})");

  register_one(
      "put_page", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    PageInput in;
    in.slug = arg(ctx, "slug");
    in.title = arg(ctx, "title", in.slug);
    in.body = arg(ctx, "body");
    in.type = arg(ctx, "type", "note");
    in.source_id = arg(ctx, "source_id", "default");
    // N2.5: remote may only write default source unless allow-list config
    if (ctx.remote && in.source_id != "default") {
      auto allow = ctx.brain->get_config_value("mcp.allowed_sources");
      bool ok = false;
      if (allow) {
        for (auto& p : util::split(*allow, ',')) {
          if (util::trim(p) == in.source_id) ok = true;
        }
      }
      if (!ok) {
        OpResult r;
        r.ok = false;
        r.text = "remote source_id not allowed (only default, or mcp.allowed_sources)";
        return r;
      }
    }
    if (!ctx.brain->ensure_source(in.source_id)) {
      OpResult r;
      r.ok = false;
      r.text = "invalid source_id";
      return r;
    }
    if (in.slug.empty()) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "slug required";
      return r;
    }
    // Provenance: remote MCP stamps mcp:put_page (gbrain-like)
    if (ctx.remote) {
      in.source_kind = "mcp:put_page";
      in.ingested_via = "mcp";
    } else {
      in.source_kind = in.source_kind.empty() ? "put_page" : in.source_kind;
      in.ingested_via = in.ingested_via.empty() ? "cli" : in.ingested_via;
    }
    auto page = ctx.brain->put_page(in);
    auto chunks = ingest::chunk_markdown(page.title, page.body);
    ctx.brain->replace_chunks(page.id, chunks);
    // Remote callers: skip auto-link (gbrain mitigation vs backlink poisoning)
    size_t nlinks = 0;
    if (!ctx.remote) {
      auto links = graph::extract_links(page.source_id, page.slug, page.body);
      ctx.brain->replace_extracted_links(page.source_id, page.slug, links);
      nlinks = links.size();
    }
    ctx.brain->enqueue_embed_page(page.id);
    r.text = "put " + page.slug + " id=" + std::to_string(page.id);
    json j = {{"id", page.id},
              {"slug", page.slug},
              {"chunks", chunks.size()},
              {"links", nlinks},
              {"embed_enqueued", true}};
    r.json = j.dump(2);
    return r;
  }, true, "Create or update a page (localOnly unless MCP --allow-write)",
      R"({"type":"object","properties":{"slug":{"type":"string"},"title":{"type":"string"},"body":{"type":"string"},"type":{"type":"string"},"source_id":{"type":"string"}},"required":["slug"]})");

  register_one(
      "get_page", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "slug");
    auto page = ctx.brain->get_page(slug, arg(ctx, "source_id", "default"));
    if (!page) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "not found: " + slug;
      return r;
    }
    json j = {{"id", page->id},
              {"slug", page->slug},
              {"type", page->type},
              {"title", page->title},
              {"body", page->body},
              {"updated_at", page->updated_at}};
    r.json = j.dump(2);
    r.text = "# " + page->title + "\n\n" + page->body + "\n";
    return r;
  }, false, "Get a page by slug",
      R"({"type":"object","properties":{"slug":{"type":"string"},"source_id":{"type":"string"}},"required":["slug"]})");

  register_one(
      "list_pages", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    int limit = arg_int(ctx, "limit", 50);
    auto type = arg(ctx, "type");
    auto pages = ctx.brain->list_pages(limit, type);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& p : pages) {
      arr.push_back({{"slug", p.slug}, {"type", p.type}, {"title", p.title}, {"updated_at", p.updated_at}});
      oss << p.slug << "\t" << p.type << "\t" << p.title << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str();
    return r;
  }, false, "List pages",
      R"({"type":"object","properties":{"limit":{"type":"integer"},"type":{"type":"string"}}})");

  register_one(
      "search", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto q = arg(ctx, "query");
    if (q.empty()) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "query required";
      return r;
    }
    search::HybridOpts opts;
    opts.limit = arg_int(ctx, "limit", ctx.brain->config().search_default_limit);
    opts.rrf_k = ctx.brain->config().search_rrf_k;
    opts.source_id = arg(ctx, "source_id");
    opts.mode = arg(ctx, "mode", "balanced");
    std::vector<float> emb;
    std::vector<float>* pemb = nullptr;
    // no_vector accepts "1"/"true" from CLI and MCP bool mapping
    auto nv = arg(ctx, "no_vector");
    if (nv != "1" && nv != "true" && opts.mode != "conservative") {
      auto er = ai::embed_texts(ctx.brain->config(), {q});
      if (er.ok && !er.vectors.empty()) {
        emb = er.vectors[0];
        pemb = &emb;
      }
    }
    auto hits = search::hybrid_search(*ctx.brain, q, pemb, opts);
    json arr = json::array();
    std::ostringstream oss;
    int i = 1;
    for (auto& h : hits) {
      arr.push_back({{"rank", i},
                     {"slug", h.slug},
                     {"title", h.title},
                     {"score", h.score},
                     {"snippet", h.snippet}});
      oss << i << ". " << h.slug << "  (" << h.score << ")\n   " << h.title << "\n   " << h.snippet
          << "\n";
      ++i;
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "(no results)\n" : oss.str();
    return r;
  }, false, "Hybrid search (FTS + vector + RRF)",
      R"({"type":"object","properties":{"query":{"type":"string"},"limit":{"type":"integer"},"no_vector":{"type":"boolean"},"source_id":{"type":"string"}},"required":["query"]})");

  register_one(
      "think", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto q = arg(ctx, "question");
    if (q.empty()) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "question required";
      return r;
    }
    search::HybridOpts opts;
    opts.limit = arg_int(ctx, "limit", 8);
    opts.rrf_k = ctx.brain->config().search_rrf_k;
    opts.source_id = arg(ctx, "source_id");
    std::vector<float> emb;
    std::vector<float>* pemb = nullptr;
    auto er = ai::embed_texts(ctx.brain->config(), {q});
    if (er.ok && !er.vectors.empty()) {
      emb = er.vectors[0];
      pemb = &emb;
    }
    auto hits = search::hybrid_search(*ctx.brain, q, pemb, opts);
    std::ostringstream evidence;
    int i = 1;
    for (auto& h : hits) {
      auto page = ctx.brain->get_page(h.slug);
      evidence << "[" << i << "] " << h.slug << " — " << h.title << "\n";
      if (page) {
        auto body = page->body;
        if (body.size() > 1200) body = body.substr(0, 1200) + "…";
        evidence << body << "\n\n";
      }
      ++i;
    }
    std::string system =
        "You are Qbrain, a personal knowledge brain. Answer using ONLY the evidence. "
        "Cite sources as [n]. End with a section '## Gaps' listing what is unknown or stale.";
    std::string user = "Question: " + q + "\n\nEvidence:\n" + evidence.str();
    auto cr = ai::chat_complete(ctx.brain->config(),
                                {{"system", system}, {"user", user}});
    json j;
    j["question"] = q;
    j["hits"] = hits.size();
    if (!cr.ok) {
      j["degraded"] = true;
      j["error"] = cr.error;
      j["evidence"] = evidence.str();
      r.json = j.dump(2);
      r.text = "[gather-only; no LLM] " + cr.error + "\n\n" + evidence.str();
      r.ok = true;  // graceful
      return r;
    }
    j["answer"] = cr.content;
    r.json = j.dump(2);
    r.text = cr.content + "\n";
    // save is a write side-effect: only when not remote, or allow_write
    if (arg(ctx, "save") == "1" && (!ctx.remote || ctx.allow_write)) {
      PageInput in;
      auto h = util::sha256_hex(q);
      if (h.size() > 8) h = h.substr(0, 8);
      in.slug = "synthesis/" + util::slugify(q.substr(0, 40)) + "-" + util::utc_date() + "-" + h;
      in.title = "Synthesis: " + q;
      in.body = cr.content;
      in.type = "synthesis";
      auto page = ctx.brain->put_page(in);
      auto chunks = ingest::chunk_markdown(page.title, page.body);
      ctx.brain->replace_chunks(page.id, chunks);
      auto links = graph::extract_links(page.source_id, page.slug, page.body);
      ctx.brain->replace_extracted_links(page.source_id, page.slug, links);
      r.text += "\n[saved " + page.slug + "]\n";
    } else if (arg(ctx, "save") == "1" && ctx.remote && !ctx.allow_write) {
      r.text += "\n[save ignored: MCP write disabled; use --allow-write]\n";
    }
    return r;
  }, false, "Synthesize an answer with citations and gaps",
      R"({"type":"object","properties":{"question":{"type":"string"},"limit":{"type":"integer"},"save":{"type":"boolean"},"source_id":{"type":"string"}},"required":["question"]})");

  register_one(
      "capture", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto text = arg(ctx, "text");
    if (text.empty()) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "text required";
      return r;
    }
    auto page = ingest::capture_text(*ctx.brain, text, arg(ctx, "type", "note"));
    // stamp provenance via re-put lightweight fields already set at capture; enqueue embed
    ctx.brain->enqueue_embed_page(page.id);
    r.text = page.slug;
    r.json = json({{"slug", page.slug}, {"id", page.id}, {"embed_enqueued", true}}).dump(2);
    return r;
  }, true, "Quick-capture text into inbox/ (CLI always; MCP needs --allow-write). gbrain capture is CLI-only — Qbrain extension.",
      R"({"type":"object","properties":{"text":{"type":"string"},"type":{"type":"string"}},"required":["text"]})");

  register_one(
      "get_links", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "slug");
    auto depth = arg_int(ctx, "depth", 1);
    auto ns = graph::neighbors(*ctx.brain, slug, depth);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& n : ns) {
      arr.push_back({{"slug", n.slug}, {"link_type", n.link_type}, {"direction", n.direction}, {"depth", n.depth}});
      oss << n.direction << "\t" << n.link_type << "\t" << n.slug << "\td=" << n.depth << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str();
    return r;
  }, false, "Graph neighbors for a slug",
      R"({"type":"object","properties":{"slug":{"type":"string"},"depth":{"type":"integer"}},"required":["slug"]})");

  register_one(
      "get_backlinks", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "slug");
    auto sid = arg(ctx, "source_id", "default");
    auto links = ctx.brain->get_links_to(slug, sid);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& l : links) {
      arr.push_back({{"from", l.from_slug}, {"type", l.link_type}});
      oss << l.from_slug << "\t" << l.link_type << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str();
    return r;
  }, false, "Inbound links to a slug",
      R"({"type":"object","properties":{"slug":{"type":"string"},"source_id":{"type":"string"}},"required":["slug"]})");

  register_one(
      "delete_page", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "slug");
    auto sid = arg(ctx, "source_id", "default");
    if (!ctx.brain->soft_delete(slug, sid)) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "not found or already deleted";
      return r;
    }
    r.text = "deleted " + slug;
    r.json = json({{"slug", slug}, {"status", "soft_deleted"}}).dump(2);
    return r;
  }, true, "Soft-delete a page",
      R"({"type":"object","properties":{"slug":{"type":"string"},"source_id":{"type":"string"}},"required":["slug"]})");

  register_one(
      "restore_page", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "slug");
    if (!ctx.brain->restore_page(slug, arg(ctx, "source_id", "default"))) {
      r.ok = false;
      r.text = "not restored";
      return r;
    }
    r.text = "restored " + slug;
    return r;
  }, true, "Restore soft-deleted page",
      R"({"type":"object","properties":{"slug":{"type":"string"}},"required":["slug"]})");

  register_one(
      "purge_deleted_pages", Scope::Admin, [](OpContext& ctx) {
    OpResult r;
    if (ctx.remote) {
      r.ok = false;
      r.text = "purge is localOnly";
      return r;
    }
    int hours = arg_int(ctx, "older_than_hours", 72);
    int n = ctx.brain->purge_deleted(hours);
    r.text = "purged " + std::to_string(n);
    r.json = json({{"count", n}}).dump(2);
    return r;
  }, true, "Hard-delete soft-deleted pages older than N hours (localOnly)",
      R"({"type":"object","properties":{"older_than_hours":{"type":"integer"}}})");

  register_one(
      "get_versions", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto vers = ctx.brain->list_versions(arg(ctx, "slug"), arg(ctx, "source_id", "default"));
    json arr = json::array();
    for (auto& v : vers) arr.push_back({{"id", v.id}, {"title", v.title}, {"at", v.created_at}});
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List page versions",
      R"({"type":"object","properties":{"slug":{"type":"string"}},"required":["slug"]})");

  register_one(
      "sources_list", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto ids = ctx.brain->list_source_ids();
    r.json = json(ids).dump(2);
    std::ostringstream oss;
    for (auto& id : ids) oss << id << "\n";
    r.text = oss.str();
    return r;
  }, false, "List source ids", R"({"type":"object","properties":{}})");

  register_one(
      "sources_add", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto id = arg(ctx, "id");
    if (!ctx.brain->ensure_source(id)) {
      r.ok = false;
      r.text = "invalid source id";
      return r;
    }
    r.text = "ok " + id;
    return r;
  }, true, "Ensure a source id exists",
      R"({"type":"object","properties":{"id":{"type":"string"}},"required":["id"]})");

  register_one(
      "find_trajectory", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto facts = ctx.brain->list_facts(arg(ctx, "entity_slug"), arg_int(ctx, "limit", 50));
    json arr = json::array();
    for (auto& f : facts) arr.push_back(f);
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List facts for an entity slug (minimal trajectory)",
      R"({"type":"object","properties":{"entity_slug":{"type":"string"}},"required":["entity_slug"]})");

  register_one(
      "list_skills", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    (void)ctx;
    // N9: scan skills dir next to exe / project
    json arr = json::array();
    namespace fs = std::filesystem;
    std::vector<fs::path> roots = {fs::path("skills"), fs::path("D:/Projects/Qbrain/skills")};
    for (auto& root : roots) {
      if (!fs::exists(root)) continue;
      for (auto& e : fs::directory_iterator(root)) {
        if (!e.is_directory()) continue;
        auto skill = e.path() / "SKILL.md";
        if (fs::exists(skill)) arr.push_back({{"name", e.path().filename().string()}});
      }
    }
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List markdown skills", R"({"type":"object","properties":{}})");

  register_one(
      "get_skill", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto name = arg(ctx, "name");
    if (name.find("..") != std::string::npos || name.find('/') != std::string::npos) {
      r.ok = false;
      r.text = "invalid skill name";
      return r;
    }
    namespace fs = std::filesystem;
    fs::path p = fs::path("skills") / name / "SKILL.md";
    if (!fs::exists(p)) p = fs::path("D:/Projects/Qbrain/skills") / name / "SKILL.md";
    if (!fs::exists(p)) {
      r.ok = false;
      r.text = "not found";
      return r;
    }
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    r.text = ss.str();
    r.json = json({{"name", name}, {"body", r.text}}).dump(2);
    return r;
  }, false, "Read a skill SKILL.md",
      R"({"type":"object","properties":{"name":{"type":"string"}},"required":["name"]})");

  register_one(
      "run_doctor", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto h = ctx.brain->health();
    json j;
    j["ok"] = h.ok;
    j["schema_version"] = h.schema_version;
    j["stats"] = {{"pages", h.stats.pages},
                  {"chunks", h.stats.chunks},
                  {"links", h.stats.links},
                  {"embedded_chunks", h.stats.embedded_chunks}};
    j["notes"] = h.notes;
    r.json = j.dump(2);
    r.text = r.json;
    r.ok = h.ok;
    return r;
  }, false, "Doctor report (gbrain name parity)", R"({"type":"object","properties":{}})");

  // --- gbrain name aliases / remaining write ops ---
  register_one(
      "query", Scope::Read, [](OpContext& ctx) {
    // alias search
    if (ctx.args.count("query") == 0 && ctx.args.count("q")) ctx.args["query"] = ctx.args["q"];
    OpContext c2 = ctx;
    auto* op = global_registry().find("search");
    return op ? op->handler(c2) : OpResult{false, 1, "search missing", ""};
  }, false, "Alias of search (gbrain query)",
      R"({"type":"object","properties":{"query":{"type":"string"}},"required":["query"]})");

  register_one(
      "add_link", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    Link l;
    l.from_slug = arg(ctx, "from");
    l.to_slug = arg(ctx, "to");
    l.link_type = arg(ctx, "link_type", "related");
    l.link_source = "manual";
    l.source_id = arg(ctx, "source_id", "default");
    if (l.from_slug.empty() || l.to_slug.empty()) {
      r.ok = false;
      r.text = "from and to required";
      return r;
    }
    ctx.brain->add_link(l);
    r.text = "ok";
    return r;
  }, true, "Add a manual link",
      R"({"type":"object","properties":{"from":{"type":"string"},"to":{"type":"string"},"link_type":{"type":"string"}},"required":["from","to"]})");

  register_one(
      "remove_link", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    ctx.brain->remove_link(arg(ctx, "from"), arg(ctx, "to"), arg(ctx, "source_id", "default"));
    r.text = "ok";
    return r;
  }, true, "Remove a link",
      R"({"type":"object","properties":{"from":{"type":"string"},"to":{"type":"string"}},"required":["from","to"]})");

  register_one(
      "add_tag", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    try {
      ctx.brain->add_tag(arg(ctx, "slug"), arg(ctx, "tag"), arg(ctx, "source_id", "default"));
      r.text = "ok";
    } catch (const std::exception& e) {
      r.ok = false;
      r.text = e.what();
    }
    return r;
  }, true, "Add tag to page",
      R"({"type":"object","properties":{"slug":{"type":"string"},"tag":{"type":"string"}},"required":["slug","tag"]})");

  register_one(
      "remove_tag", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    ctx.brain->remove_tag(arg(ctx, "slug"), arg(ctx, "tag"), arg(ctx, "source_id", "default"));
    r.text = "ok";
    return r;
  }, true, "Remove tag",
      R"({"type":"object","properties":{"slug":{"type":"string"},"tag":{"type":"string"}},"required":["slug","tag"]})");

  register_one(
      "get_tags", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto tags = ctx.brain->get_tags(arg(ctx, "slug"), arg(ctx, "source_id", "default"));
    r.json = json(tags).dump(2);
    r.text = r.json;
    return r;
  }, false, "List tags on a page",
      R"({"type":"object","properties":{"slug":{"type":"string"}},"required":["slug"]})");

  register_one(
      "find_orphans", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto o = ctx.brain->find_orphans(arg_int(ctx, "limit", 100));
    r.json = json(o).dump(2);
    r.text = r.json;
    return r;
  }, false, "Pages with no inbound or outbound links",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "extract_facts", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int n = ctx.brain->extract_facts_from_page(arg(ctx, "slug"), arg(ctx, "source_id", "default"));
    r.text = "facts=" + std::to_string(n);
    r.json = json({{"count", n}}).dump(2);
    return r;
  }, true, "Heuristic fact extraction from page links/title",
      R"({"type":"object","properties":{"slug":{"type":"string"}},"required":["slug"]})");

  register_one(
      "list_brains", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    (void)ctx;
    auto ids = Brain::list_brains();
    r.json = json(ids).dump(2);
    r.text = r.json;
    return r;
  }, false, "List brain ids under %LOCALAPPDATA%\\Qbrain\\brains",
      R"({"type":"object","properties":{}})");

  register_one(
      "whoami", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    r.json = json({{"remote", ctx.remote},
                   {"allow_write", ctx.allow_write},
                   {"brain", ctx.brain ? ctx.brain->brain_id() : ""},
                   {"transport", ctx.remote ? "mcp" : "cli"}})
                 .dump(2);
    r.text = r.json;
    return r;
  }, false, "Caller context", R"({"type":"object","properties":{}})");

  register_one(
      "get_chunks", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto page = ctx.brain->get_page(arg(ctx, "slug"), arg(ctx, "source_id", "default"));
    if (!page) {
      r.ok = false;
      r.text = "not found";
      return r;
    }
    auto chunks = ctx.brain->get_chunks(page->id);
    json arr = json::array();
    for (auto& c : chunks) {
      arr.push_back({{"index", c.chunk_index},
                     {"text", c.text.substr(0, 500)},
                     {"embedded", !c.embedding.empty()},
                     {"dim", c.dim}});
    }
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List chunks for a page",
      R"({"type":"object","properties":{"slug":{"type":"string"}},"required":["slug"]})");

  register_one(
      "revert_version", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int64_t vid = 0;
    try {
      vid = std::stoll(arg(ctx, "version_id"));
    } catch (...) {
      r.ok = false;
      r.text = "version_id required";
      return r;
    }
    if (!ctx.brain->revert_version(arg(ctx, "slug"), vid, arg(ctx, "source_id", "default"))) {
      r.ok = false;
      r.text = "revert failed";
      return r;
    }
    r.text = "reverted";
    return r;
  }, true, "Revert page to a version id",
      R"({"type":"object","properties":{"slug":{"type":"string"},"version_id":{"type":"integer"}},"required":["slug","version_id"]})");
}

}  // namespace qbrain::ops
