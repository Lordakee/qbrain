#include "qbrain/ops/registry.hpp"
#include "qbrain/ai/chat.hpp"
#include "qbrain/ai/embed.hpp"
#include "qbrain/codeintel/scan.hpp"
#include "qbrain/cycle/dream.hpp"
#include "qbrain/graph/analytics.hpp"
#include "qbrain/graph/extract.hpp"
#include "qbrain/graph/traverse.hpp"
#include "qbrain/ingest/chunker.hpp"
#include "qbrain/ingest/import.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/search/hybrid.hpp"
#include "qbrain/service/live_sync.hpp"
#include "qbrain/util/string_util.hpp"
#include "qbrain/util/time_util.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/paths.hpp"
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
    opts.config = &ctx.brain->config();
    auto rr = arg(ctx, "rerank");
    if (rr == "1" || rr == "true") opts.rerank = true;
    auto rrl = arg(ctx, "rerank_llm");
    if (rrl == "1" || rrl == "true") opts.rerank_llm = true;
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
                     {"rerank_score", h.rerank_score},
                     {"snippet", h.snippet}});
      oss << i << ". " << h.slug << "  (" << h.score << ")\n   " << h.title << "\n   " << h.snippet
          << "\n";
      ++i;
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "(no results)\n" : oss.str();
    return r;
  }, false, "Hybrid search (FTS + vector + RRF + optional rerank)",
      R"({"type":"object","properties":{"query":{"type":"string"},"limit":{"type":"integer"},"no_vector":{"type":"boolean"},"source_id":{"type":"string"},"mode":{"type":"string"},"rerank":{"type":"boolean"},"rerank_llm":{"type":"boolean"}},"required":["query"]})");

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
      "sources_remove", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto id = arg(ctx, "id");
    bool force = arg(ctx, "force") == "1" || arg(ctx, "force") == "true";
    if (!ctx.brain->remove_source(id, force)) {
      r.ok = false;
      r.text = "remove failed (nonempty? use force=true; cannot remove default)";
      return r;
    }
    r.text = "removed " + id;
    return r;
  }, true, "Remove a source (blocks if pages unless force)",
      R"({"type":"object","properties":{"id":{"type":"string"},"force":{"type":"boolean"}},"required":["id"]})");

  register_one(
      "sources_status", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto id = arg(ctx, "id", "default");
    auto s = ctx.brain->source_status(id);
    r.json = json({{"id", s.id},
                   {"pages", s.pages},
                   {"links", s.links},
                   {"last_updated", s.last_updated}})
                 .dump(2);
    r.text = r.json;
    return r;
  }, false, "Source page/link counts",
      R"({"type":"object","properties":{"id":{"type":"string"}}})");

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
      "find_anomalies", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto rows = graph::find_anomalies(*ctx.brain, arg_int(ctx, "limit", 100));
    json arr = json::array();
    std::ostringstream oss;
    for (auto& a : rows) {
      arr.push_back({{"kind", a.kind}, {"slug", a.slug}, {"detail", a.detail}});
      oss << a.kind << "\t" << a.slug << "\t" << a.detail << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "[]\n" : oss.str();
    return r;
  }, false, "Graph anomalies: missing/deleted link targets, high out-degree",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "find_contradictions", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto rows = graph::find_contradictions(*ctx.brain, arg_int(ctx, "limit", 100));
    json arr = json::array();
    std::ostringstream oss;
    for (auto& c : rows) {
      arr.push_back({{"kind", c.kind}, {"slug", c.slug}, {"detail", c.detail}});
      oss << c.kind << "\t" << c.slug << "\t" << c.detail << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "[]\n" : oss.str();
    return r;
  }, false, "Heuristic fact contradictions (conflicting predicates / dual objects)",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "find_experts", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto rows = graph::find_experts(*ctx.brain, arg_int(ctx, "limit", 50));
    json arr = json::array();
    std::ostringstream oss;
    for (auto& e : rows) {
      arr.push_back({{"slug", e.slug}, {"inbound_count", e.inbound_count}});
      oss << e.slug << "\t" << e.inbound_count << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "[]\n" : oss.str();
    return r;
  }, false, "Pages ranked by inbound link count (expertise heuristic)",
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

  // N12 minions / jobs
  register_one(
      "submit_job", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto type = arg(ctx, "type");
    if (type.empty()) type = arg(ctx, "name");
    if (type.empty()) {
      r.ok = false;
      r.text = "type required";
      return r;
    }
    auto payload = arg(ctx, "payload_json", "{}");
    auto queue = arg(ctx, "queue", "default");
    int pri = arg_int(ctx, "priority", 100);
    auto id = jobs::submit_job(*ctx.brain, type, payload, queue, pri);
    r.json = json({{"id", id}, {"type", type}, {"status", "waiting"}}).dump(2);
    r.text = "job " + std::to_string(id);
    return r;
  }, false, "Submit a minion job",
      R"({"type":"object","properties":{"type":{"type":"string"},"name":{"type":"string"},"payload_json":{"type":"string"},"queue":{"type":"string"},"priority":{"type":"integer"}},"required":["type"]})");

  register_one(
      "list_jobs", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto status = arg(ctx, "status");
    int limit = arg_int(ctx, "limit", 50);
    auto list = jobs::list_jobs(*ctx.brain, status, limit);
    json arr = json::array();
    for (auto& j : list) {
      arr.push_back({{"id", j.id},
                     {"type", j.type},
                     {"status", j.status},
                     {"priority", j.priority},
                     {"attempts", j.attempts},
                     {"queue", j.queue}});
    }
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List jobs",
      R"({"type":"object","properties":{"status":{"type":"string"},"limit":{"type":"integer"}}})");

  register_one(
      "get_job", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    int64_t id = 0;
    try {
      id = std::stoll(arg(ctx, "id"));
    } catch (...) {
      r.ok = false;
      r.text = "id required";
      return r;
    }
    auto j = jobs::get_job(*ctx.brain, id);
    if (!j) {
      r.ok = false;
      r.text = "not found";
      return r;
    }
    r.json = json({{"id", j->id},
                   {"type", j->type},
                   {"status", j->status},
                   {"payload_json", j->payload_json},
                   {"result_json", j->result_json},
                   {"error_text", j->error_text},
                   {"attempts", j->attempts},
                   {"priority", j->priority}})
                 .dump(2);
    r.text = r.json;
    return r;
  }, false, "Get job by id",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "cancel_job", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int64_t id = 0;
    try {
      id = std::stoll(arg(ctx, "id"));
    } catch (...) {
      r.ok = false;
      r.text = "id required";
      return r;
    }
    if (!jobs::cancel_job(*ctx.brain, id)) {
      r.ok = false;
      r.text = "cancel failed";
      return r;
    }
    r.text = "cancelled";
    return r;
  }, false, "Cancel a waiting/active job",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "run_dream", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    cycle::DreamOpts opts;
    opts.dry_run = arg(ctx, "apply") != "1" && arg(ctx, "apply") != "true";
    opts.phase = arg(ctx, "phase");
    opts.page_limit = arg_int(ctx, "limit", 50);
    auto report = cycle::run_dream(*ctx.brain, opts);
    r.json = cycle::report_to_json(report);
    r.text = cycle::report_to_text(report);
    if (report.status == "failed") {
      r.ok = false;
      r.exit_code = 1;
    }
    return r;
  }, true, "Run multi-phase dream cycle",
      R"({"type":"object","properties":{"apply":{"type":"boolean"},"phase":{"type":"string"},"limit":{"type":"integer"}}})");

  // N13
  register_one(
      "sync_brain", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto path = arg(ctx, "path");
    if (path.empty()) path = arg(ctx, "dir");
    if (path.empty()) {
      r.ok = false;
      r.text = "path required";
      return r;
    }
    auto source = arg(ctx, "source_id", "default");
    auto once = service::live_sync_once(*ctx.brain, path, source);
    r.json = json({{"scanned", once.scanned},
                   {"imported_pages", once.imported_pages},
                   {"skipped", once.skipped},
                   {"errors", once.errors}})
                 .dump(2);
    r.text = r.json;
    if (once.errors && !once.imported_pages) {
      r.ok = false;
      r.exit_code = 1;
    }
    return r;
  }, true, "Live-sync a notes directory (mtime state)",
      R"({"type":"object","properties":{"path":{"type":"string"},"dir":{"type":"string"},"source_id":{"type":"string"}},"required":["path"]})");

  register_one(
      "traverse_graph", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "slug");
    if (slug.empty()) {
      r.ok = false;
      r.text = "slug required";
      return r;
    }
    int depth = arg_int(ctx, "depth", 1);
    auto ns = graph::neighbors(*ctx.brain, slug, depth);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& n : ns) {
      arr.push_back({{"slug", n.slug},
                     {"link_type", n.link_type},
                     {"direction", n.direction},
                     {"depth", n.depth}});
      oss << n.direction << " " << n.slug << " (" << n.link_type << ") d=" << n.depth << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "(no neighbors)\n" : oss.str();
    return r;
  }, false, "BFS graph neighbors",
      R"({"type":"object","properties":{"slug":{"type":"string"},"depth":{"type":"integer"}},"required":["slug"]})");

  register_one(
      "retry_job", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int64_t id = 0;
    try {
      id = std::stoll(arg(ctx, "id"));
    } catch (...) {
      r.ok = false;
      r.text = "id required";
      return r;
    }
    if (!jobs::retry_job(*ctx.brain, id)) {
      r.ok = false;
      r.text = "retry failed";
      return r;
    }
    r.text = "retried " + std::to_string(id);
    return r;
  }, false, "Requeue failed/cancelled job to waiting",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "pause_job", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int64_t id = 0;
    try {
      id = std::stoll(arg(ctx, "id"));
    } catch (...) {
      r.ok = false;
      r.text = "id required";
      return r;
    }
    if (!jobs::pause_job(*ctx.brain, id)) {
      r.ok = false;
      r.text = "pause failed (need waiting/active)";
      return r;
    }
    auto j = jobs::get_job(*ctx.brain, id);
    r.json = json({{"id", id}, {"status", j ? j->status : "paused"}}).dump(2);
    r.text = "paused " + std::to_string(id);
    return r;
  }, false, "Pause waiting/active job",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "resume_job", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int64_t id = 0;
    try {
      id = std::stoll(arg(ctx, "id"));
    } catch (...) {
      r.ok = false;
      r.text = "id required";
      return r;
    }
    if (!jobs::resume_job(*ctx.brain, id)) {
      r.ok = false;
      r.text = "resume failed (need paused)";
      return r;
    }
    r.json = json({{"id", id}, {"status", "waiting"}}).dump(2);
    r.text = "resumed " + std::to_string(id);
    return r;
  }, false, "Resume paused job to waiting",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "get_job_progress", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    int64_t id = 0;
    try {
      id = std::stoll(arg(ctx, "id"));
    } catch (...) {
      r.ok = false;
      r.text = "id required";
      return r;
    }
    auto p = jobs::get_job_progress(*ctx.brain, id);
    if (!p) {
      r.ok = false;
      r.text = "not found";
      return r;
    }
    r.json = json({{"id", p->id},
                   {"type", p->type},
                   {"status", p->status},
                   {"attempts", p->attempts},
                   {"lock_until", p->lock_until},
                   {"error_text", p->error_text}})
                 .dump(2);
    r.text = r.json;
    return r;
  }, false, "Job progress (status/attempts/lock/error)",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "get_status_snapshot", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto s = ctx.brain->status_snapshot();
    json j = {{"schema_version", s.schema_version},
              {"pages", s.pages},
              {"chunks", s.chunks},
              {"links", s.links},
              {"embedded_chunks", s.embedded_chunks},
              {"jobs",
               {{"waiting", s.jobs_waiting},
                {"active", s.jobs_active},
                {"failed", s.jobs_failed},
                {"paused", s.jobs_paused}}}};
    r.json = j.dump(2);
    r.text = r.json;
    return r;
  }, false, "Pages/chunks/links/jobs counts + schema version",
      R"({"type":"object","properties":{}})");

  register_one(
      "doctor_remediate", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto rep = ctx.brain->remediate();
    json j = {{"default_source", rep.default_source},
              {"reclaimed", rep.reclaimed},
              {"embed_jobs_enqueued", rep.embed_jobs_enqueued},
              {"api_key_present", rep.api_key_present},
              {"notes", rep.notes}};
    r.json = j.dump(2);
    std::ostringstream oss;
    oss << "remediate: source=" << (rep.default_source ? "ok" : "fail")
        << " reclaimed=" << rep.reclaimed
        << " embed_enqueued=" << rep.embed_jobs_enqueued << "\n";
    for (auto& n : rep.notes) oss << "  - " << n << "\n";
    r.text = oss.str();
    r.ok = rep.default_source;
    return r;
  }, true, "Doctor remediate: source, reclaim stalled, re-enqueue embeds",
      R"({"type":"object","properties":{}})");

  register_one(
      "forget_fact", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "entity_slug");
    if (slug.empty()) slug = arg(ctx, "slug");
    if (slug.empty()) {
      r.ok = false;
      r.text = "entity_slug required";
      return r;
    }
    int n = ctx.brain->forget_fact(slug, arg(ctx, "predicate"));
    r.json = json({{"deactivated", n}}).dump(2);
    r.text = "deactivated " + std::to_string(n);
    return r;
  }, false, "Soft-deactivate facts for entity",
      R"({"type":"object","properties":{"entity_slug":{"type":"string"},"slug":{"type":"string"},"predicate":{"type":"string"}}})");

  register_one(
      "resolve_slugs", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    // comma-separated slugs → existing pages
    auto raw = arg(ctx, "slugs");
    if (raw.empty()) raw = arg(ctx, "slug");
    auto parts = util::split(raw, ',');
    json arr = json::array();
    std::ostringstream oss;
    for (auto p : parts) {
      p = util::trim(p);
      if (p.empty()) continue;
      auto page = ctx.brain->get_page(p, arg(ctx, "source_id", "default"));
      json item = {{"slug", p}, {"exists", page.has_value()}};
      if (page) {
        item["title"] = page->title;
        item["type"] = page->type;
      }
      arr.push_back(item);
      oss << p << "\t" << (page ? "ok" : "missing") << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str();
    return r;
  }, false, "Resolve slug existence",
      R"({"type":"object","properties":{"slugs":{"type":"string"},"slug":{"type":"string"}}})");

  register_one(
      "recall", Scope::Read, [](OpContext& ctx) {
    // Alias of search with conservative mode default (gbrain recall-ish)
    OpContext c2 = ctx;
    if (c2.args.find("mode") == c2.args.end()) c2.args["mode"] = "conservative";
    if (c2.args.find("query") == c2.args.end() && c2.args.count("q"))
      c2.args["query"] = c2.args["q"];
    auto* op = global_registry().find("search");
    return op ? op->handler(c2) : OpResult{false, 1, "search missing", ""};
  }, false, "Recall (conservative search alias)",
      R"({"type":"object","properties":{"query":{"type":"string"},"q":{"type":"string"},"limit":{"type":"integer"}},"required":["query"]})");

  // N16 code-intel (regex/heuristic, no tree-sitter)
  auto hits_to_result = [](const std::vector<codeintel::Hit>& hits) {
    OpResult r;
    json arr = json::array();
    std::ostringstream oss;
    for (auto& h : hits) {
      arr.push_back({{"slug", h.slug},
                     {"line", h.line},
                     {"snippet", h.snippet},
                     {"kind", h.kind}});
      oss << h.slug << ":" << h.line << " [" << h.kind << "] " << h.snippet << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "(no matches)\n" : oss.str();
    return r;
  };

  register_one(
      "code_def", Scope::Read, [hits_to_result](OpContext& ctx) {
    auto symbol = arg(ctx, "symbol");
    if (symbol.empty()) symbol = arg(ctx, "name");
    if (symbol.empty()) {
      OpResult r;
      r.ok = false;
      r.text = "symbol required";
      return r;
    }
    int limit = arg_int(ctx, "limit", 50);
    int page_limit = arg_int(ctx, "page_limit", 500);
    auto hits = codeintel::find_defs(*ctx.brain, symbol, limit, page_limit);
    return hits_to_result(hits);
  }, false, "Find C++/TS-like symbol definitions in page bodies",
      R"({"type":"object","properties":{"symbol":{"type":"string"},"name":{"type":"string"},"limit":{"type":"integer"},"page_limit":{"type":"integer"}},"required":["symbol"]})");

  register_one(
      "code_refs", Scope::Read, [hits_to_result](OpContext& ctx) {
    auto symbol = arg(ctx, "symbol");
    if (symbol.empty()) symbol = arg(ctx, "name");
    if (symbol.empty()) {
      OpResult r;
      r.ok = false;
      r.text = "symbol required";
      return r;
    }
    int limit = arg_int(ctx, "limit", 50);
    int page_limit = arg_int(ctx, "page_limit", 500);
    auto hits = codeintel::find_refs(*ctx.brain, symbol, limit, page_limit);
    return hits_to_result(hits);
  }, false, "Find word-boundary symbol references in page bodies",
      R"({"type":"object","properties":{"symbol":{"type":"string"},"name":{"type":"string"},"limit":{"type":"integer"},"page_limit":{"type":"integer"}},"required":["symbol"]})");

  register_one(
      "code_callers", Scope::Read, [hits_to_result](OpContext& ctx) {
    auto symbol = arg(ctx, "symbol");
    if (symbol.empty()) symbol = arg(ctx, "name");
    if (symbol.empty()) {
      OpResult r;
      r.ok = false;
      r.text = "symbol required";
      return r;
    }
    int limit = arg_int(ctx, "limit", 50);
    int page_limit = arg_int(ctx, "page_limit", 500);
    auto hits = codeintel::find_callers(*ctx.brain, symbol, limit, page_limit);
    return hits_to_result(hits);
  }, false, "Find call-ish symbol( references in page bodies",
      R"({"type":"object","properties":{"symbol":{"type":"string"},"name":{"type":"string"},"limit":{"type":"integer"},"page_limit":{"type":"integer"}},"required":["symbol"]})");

  // N15: link sources, ingest log, chronicle, timeline
  register_one(
      "list_link_sources", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto rows = ctx.brain->list_link_sources();
    json arr = json::array();
    std::ostringstream oss;
    for (auto& row : rows) {
      arr.push_back({{"link_source", row.link_source}, {"count", row.count}});
      oss << row.link_source << "\t" << row.count << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str();
    return r;
  }, false, "Distinct link_source values with counts",
      R"({"type":"object","properties":{}})");

  register_one(
      "log_ingest", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto path = arg(ctx, "path");
    auto et = arg(ctx, "event_type", "import");
    auto detail = arg(ctx, "detail_json", "{}");
    int keep = arg_int(ctx, "keep_last", 100);
    auto id = ctx.brain->log_ingest(et, path, detail, keep);
    r.json = json({{"id", id}}).dump(2);
    r.text = "logged " + std::to_string(id);
    return r;
  }, false, "Append ingest log event (keeps last N)",
      R"({"type":"object","properties":{"path":{"type":"string"},"event_type":{"type":"string"},"detail_json":{"type":"string"},"keep_last":{"type":"integer"}}})");

  register_one(
      "get_ingest_log", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto rows = ctx.brain->get_ingest_log(arg_int(ctx, "limit", 50));
    json arr = json::array();
    for (auto& e : rows) {
      arr.push_back({{"id", e.id},
                     {"event_type", e.event_type},
                     {"path", e.path},
                     {"detail_json", e.detail_json},
                     {"created_at", e.created_at}});
    }
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "Recent ingest log events",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "chronicle_day", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto day = arg(ctx, "day");
    if (day.empty()) day = arg(ctx, "date");
    if (day.empty()) day = util::utc_date();
    auto hits = ctx.brain->chronicle_day(day, arg_int(ctx, "limit", 100));
    json arr = json::array();
    std::ostringstream oss;
    for (auto& h : hits) {
      arr.push_back({{"slug", h.slug},
                     {"title", h.title},
                     {"updated_at", h.updated_at},
                     {"created_at", h.created_at},
                     {"type", h.type}});
      oss << h.slug << "\t" << h.title << "\t" << h.updated_at << "\n";
    }
    r.json = json({{"day", day.substr(0, 10)}, {"pages", arr}}).dump(2);
    r.text = oss.str();
    return r;
  }, false, "Pages created/updated on a UTC day",
      R"({"type":"object","properties":{"day":{"type":"string"},"date":{"type":"string"},"limit":{"type":"integer"}}})");

  register_one(
      "chronicle_since", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto since = arg(ctx, "since");
    if (since.empty()) since = arg(ctx, "from");
    if (since.empty()) {
      r.ok = false;
      r.text = "since required (ISO date/time)";
      return r;
    }
    auto hits = ctx.brain->chronicle_since(since, arg_int(ctx, "limit", 100));
    json arr = json::array();
    std::ostringstream oss;
    for (auto& h : hits) {
      arr.push_back({{"slug", h.slug},
                     {"title", h.title},
                     {"updated_at", h.updated_at},
                     {"created_at", h.created_at},
                     {"type", h.type}});
      oss << h.slug << "\t" << h.title << "\t" << h.updated_at << "\n";
    }
    r.json = json({{"since", since}, {"pages", arr}}).dump(2);
    r.text = oss.str();
    return r;
  }, false, "Pages created/updated since ISO timestamp",
      R"({"type":"object","properties":{"since":{"type":"string"},"from":{"type":"string"},"limit":{"type":"integer"}},"required":["since"]})");

  register_one(
      "add_timeline_entry", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    PageInput in;
    in.slug = arg(ctx, "slug");
    if (in.slug.empty()) {
      auto h = util::sha256_hex(arg(ctx, "body") + util::utc_now());
      if (h.size() > 8) h = h.substr(0, 8);
      in.slug = "timeline/" + util::utc_date() + "-" + h;
    }
    in.title = arg(ctx, "title");
    in.body = arg(ctx, "body");
    if (in.title.empty() && !in.body.empty()) {
      auto nl = in.body.find('\n');
      in.title = nl == std::string::npos ? in.body : in.body.substr(0, nl);
      if (in.title.size() > 80) in.title = in.title.substr(0, 80);
    }
    if (in.body.empty() && in.title.empty()) {
      r.ok = false;
      r.text = "title or body required";
      return r;
    }
    in.type = "timeline";
    in.source_id = arg(ctx, "source_id", "default");
    in.source_kind = "timeline";
    in.ingested_via = "mcp";
    auto page = ctx.brain->put_page(in);
    auto chunks = ingest::chunk_markdown(page.title, page.body);
    ctx.brain->replace_chunks(page.id, chunks);
    ctx.brain->enqueue_embed_page(page.id);
    r.json = json({{"slug", page.slug}, {"id", page.id}, {"type", page.type}}).dump(2);
    r.text = "timeline " + page.slug;
    return r;
  }, false, "Create a type=timeline page (thin put_page)",
      R"({"type":"object","properties":{"title":{"type":"string"},"body":{"type":"string"},"slug":{"type":"string"},"source_id":{"type":"string"}}})");

  // N17 job replay + messages
  register_one(
      "replay_job", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int64_t id = 0;
    try {
      id = std::stoll(arg(ctx, "id"));
    } catch (...) {
      r.ok = false;
      r.text = "id required";
      return r;
    }
    auto nid = jobs::replay_job(*ctx.brain, id);
    if (nid <= 0) {
      r.ok = false;
      r.text = "replay failed";
      return r;
    }
    r.json = json({{"original_id", id}, {"new_id", nid}, {"status", "waiting"}}).dump(2);
    r.text = "replayed " + std::to_string(id) + " -> " + std::to_string(nid);
    return r;
  }, false, "Clone job to a new waiting job",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "send_job_message", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int64_t id = 0;
    try {
      id = std::stoll(arg(ctx, "id"));
    } catch (...) {
      try {
        id = std::stoll(arg(ctx, "job_id"));
      } catch (...) {
        r.ok = false;
        r.text = "id required";
        return r;
      }
    }
    auto mid = jobs::send_job_message(*ctx.brain, id, arg(ctx, "sender", "system"),
                                      arg(ctx, "payload_json", "{}"));
    if (mid <= 0) {
      r.ok = false;
      r.text = "send failed";
      return r;
    }
    r.json = json({{"message_id", mid}, {"job_id", id}}).dump(2);
    r.text = "message " + std::to_string(mid);
    return r;
  }, false, "Append a message to a job inbox",
      R"({"type":"object","properties":{"id":{"type":"integer"},"job_id":{"type":"integer"},"sender":{"type":"string"},"payload_json":{"type":"string"}}})");

  // N19 identity / context / timeline
  register_one(
      "get_brain_identity", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto snap = ctx.brain->status_snapshot();
    auto st = ctx.brain->stats();
    r.json = json({{"brain_id", ctx.brain->brain_id()},
                   {"db_path", util::path_to_utf8(util::brain_db_path(ctx.brain->brain_id()))},
                   {"schema_version", snap.schema_version},
                   {"pages", st.pages},
                   {"chunks", st.chunks},
                   {"links", st.links},
                   {"embedded_chunks", st.embedded_chunks}})
                 .dump(2);
    r.text = r.json;
    return r;
  }, false, "Brain identity and stats", R"({"type":"object","properties":{}})");

  register_one(
      "volunteer_context", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto q = arg(ctx, "query");
    if (q.empty()) q = arg(ctx, "q");
    int limit = arg_int(ctx, "limit", 8);
    json arr = json::array();
    if (!q.empty()) {
      search::HybridOpts opts;
      opts.limit = limit;
      opts.use_vector = false;
      opts.mode = "conservative";
      opts.config = &ctx.brain->config();
      auto hits = search::hybrid_search(*ctx.brain, q, nullptr, opts);
      for (auto& h : hits) {
        arr.push_back({{"slug", h.slug},
                       {"title", h.title},
                       {"snippet", h.snippet},
                       {"score", h.score}});
      }
    } else {
      auto pages = ctx.brain->list_pages(limit);
      for (auto& p : pages) {
        arr.push_back({{"slug", p.slug},
                       {"title", p.title},
                       {"type", p.type},
                       {"updated_at", p.updated_at}});
      }
    }
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "Volunteer recent or search context",
      R"({"type":"object","properties":{"query":{"type":"string"},"q":{"type":"string"},"limit":{"type":"integer"}}})");

  register_one(
      "get_timeline", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    int limit = arg_int(ctx, "limit", 50);
    auto pages = ctx.brain->list_pages(limit, "timeline");
    json arr = json::array();
    for (auto& p : pages) {
      arr.push_back({{"slug", p.slug},
                     {"title", p.title},
                     {"updated_at", p.updated_at},
                     {"created_at", p.created_at}});
    }
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List timeline-type pages",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "volunteer_chronicle", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    int limit = arg_int(ctx, "limit", 50);
    auto since = arg(ctx, "since");
    std::vector<Brain::ChronicleHit> hits;
    if (!since.empty()) {
      hits = ctx.brain->chronicle_since(since, limit);
    } else {
      auto day = util::utc_date();
      hits = ctx.brain->chronicle_day(day, limit);
      if (hits.empty()) hits = ctx.brain->chronicle_since("2000-01-01", limit);
    }
    json arr = json::array();
    for (auto& h : hits) {
      arr.push_back({{"slug", h.slug},
                     {"title", h.title},
                     {"updated_at", h.updated_at},
                     {"type", h.type}});
    }
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "Volunteer recent chronicle pages",
      R"({"type":"object","properties":{"since":{"type":"string"},"limit":{"type":"integer"}}})");
}

}  // namespace qbrain::ops
