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
    std::vector<float> emb;
    std::vector<float>* pemb = nullptr;
    // no_vector accepts "1"/"true" from CLI and MCP bool mapping
    auto nv = arg(ctx, "no_vector");
    if (nv != "1" && nv != "true") {
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
}

}  // namespace qbrain::ops
