#include "qbrain/cli/app.hpp"
#include "qbrain/core/brain.hpp"
#include "qbrain/cycle/dream.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/search/hybrid.hpp"
#include "qbrain/util/paths.hpp"
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

namespace {

void set_env_var(const char* key, const char* value) {
#ifdef _WIN32
  _putenv_s(key, value ? value : "");
#else
  if (value && *value)
    setenv(key, value, 1);
  else
    unsetenv(key);
#endif
}

qbrain::Page put_indexed(qbrain::Brain& b, const std::string& slug, const std::string& title,
                         const std::string& body) {
  qbrain::PageInput in;
  in.slug = slug;
  in.title = title;
  in.body = body;
  auto p = b.put_page(in);
  b.replace_chunks(p.id, {title + "\n" + body});
  return p;
}

int index_of(const std::vector<qbrain::SearchHit>& hits, const std::string& slug) {
  for (size_t i = 0; i < hits.size(); ++i) {
    if (hits[i].slug == slug) return static_cast<int>(i);
  }
  return -1;
}

int fact_count(qbrain::Brain& b) {
  auto st = b.db().prepare("SELECT COUNT(*) FROM facts");
  if (st.step()) return static_cast<int>(st.column_int(0));
  return 0;
}

}  // namespace

void test_wave4() {
  namespace fs = std::filesystem;
  qbrain::ops::register_builtin_ops();

  auto dir = fs::temp_directory_path() / "qbrain_wave4_test";
  fs::remove_all(dir);
  fs::create_directories(dir);

  qbrain::Brain b("wave4");
  b.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));

  // N3: title boost, backlink boost, vector/RRF evidence, modes, autocut, limit/malformed/query alias.
  put_indexed(b, "title-hit", "Alpha Rank", "shared omega body");
  put_indexed(b, "body-hit", "Plain", "alpha rank shared omega body");
  qbrain::search::HybridOpts title_opts;
  title_opts.mode = "conservative";
  title_opts.use_vector = false;
  title_opts.limit = 10;
  auto title_hits = qbrain::search::hybrid_search(b, "alpha rank", nullptr, title_opts);
  QB_CHECK(index_of(title_hits, "title-hit") >= 0);
  QB_CHECK(index_of(title_hits, "body-hit") >= 0);
  QB_CHECK(index_of(title_hits, "title-hit") < index_of(title_hits, "body-hit"));

  put_indexed(b, "back-rich", "Back Target", "backlinkterm same body");
  put_indexed(b, "back-low", "Back Target", "backlinkterm same body");
  for (int i = 0; i < 5; ++i) {
    qbrain::Link l;
    l.from_slug = "ref" + std::to_string(i);
    l.to_slug = "back-rich";
    b.add_link(l);
  }
  auto back_hits = qbrain::search::hybrid_search(b, "backlinkterm", nullptr, title_opts);
  QB_CHECK(index_of(back_hits, "back-rich") >= 0);
  QB_CHECK(index_of(back_hits, "back-low") >= 0);
  QB_CHECK(index_of(back_hits, "back-rich") < index_of(back_hits, "back-low"));

  auto vec_page = put_indexed(b, "vector-only", "Vector Only", "semantic peer with no nevermatchtoken");
  auto chunks = b.get_chunks(vec_page.id);
  QB_CHECK(!chunks.empty());
  b.update_chunk_embedding(chunks[0].id, {1.0f, 0.0f, 0.0f}, "test");
  std::vector<float> qemb = {1.0f, 0.0f, 0.0f};
  qbrain::search::HybridOpts balanced;
  balanced.mode = "balanced";
  balanced.limit = 10;
  auto vec_hits = qbrain::search::hybrid_search(b, "zzznomatch", &qemb, balanced);
  QB_CHECK(index_of(vec_hits, "vector-only") >= 0);
  auto cons_hits = qbrain::search::hybrid_search(b, "zzznomatch", &qemb, title_opts);
  QB_CHECK(index_of(cons_hits, "vector-only") < 0);

  int balanced_budget = 0;
  int tokenmax_budget = 0;
  balanced.limit = 4;
  balanced.candidate_budget_out = &balanced_budget;
  qbrain::search::HybridOpts tokenmax = balanced;
  tokenmax.mode = "tokenmax";
  tokenmax.candidate_budget_out = &tokenmax_budget;
  (void)qbrain::search::hybrid_search(b, "alpha", nullptr, balanced);
  (void)qbrain::search::hybrid_search(b, "alpha", nullptr, tokenmax);
  QB_CHECK(tokenmax_budget > balanced_budget);

  put_indexed(b, "gap-top", "gapterm", "gapterm");
  for (int i = 0; i < 5; ++i) {
    qbrain::Link l;
    l.from_slug = "gap-ref" + std::to_string(i);
    l.to_slug = "gap-top";
    b.add_link(l);
  }
  for (int i = 0; i < 4; ++i) put_indexed(b, "gap-tail" + std::to_string(i), "Tail", "gapterm");
  int pre_cut = 0;
  qbrain::search::HybridOpts cut_opts;
  cut_opts.mode = "conservative";
  cut_opts.use_vector = false;
  cut_opts.limit = 10;
  cut_opts.pre_autocut_count_out = &pre_cut;
  auto cut_hits = qbrain::search::hybrid_search(b, "gapterm", nullptr, cut_opts);
  QB_CHECK(pre_cut >= 3);
  QB_CHECK(static_cast<int>(cut_hits.size()) < pre_cut);

  for (int i = 0; i < 120; ++i) put_indexed(b, "limit" + std::to_string(i), "Limit", "commonlimit");
  qbrain::search::HybridOpts limit_opts;
  limit_opts.mode = "conservative";
  limit_opts.use_vector = false;
  limit_opts.limit = 200;
  auto limit_hits = qbrain::search::hybrid_search(b, "commonlimit", nullptr, limit_opts);
  QB_CHECK(limit_hits.size() <= 100);
  auto malformed = qbrain::search::hybrid_search(b, "\"unterminated", nullptr, limit_opts);
  QB_CHECK(malformed.size() <= 100);

  qbrain::ops::OpContext sctx;
  sctx.brain = &b;
  sctx.args = {{"query", "alpha rank"}, {"mode", "conservative"}, {"limit", "5"}};
  auto sr = qbrain::ops::global_registry().call("search", sctx);
  auto qr = qbrain::ops::global_registry().call("query", sctx);
  QB_CHECK(sr.ok && qr.ok);
  QB_CHECK(sr.json == qr.json);

  qbrain::mcp::ServeOptions mopts;
  mopts.allow_write = false;
  auto search_req =
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"search","arguments":{"query":"alpha","mode":"conservative"}}})";
  auto search_resp = qbrain::mcp::handle_rpc_body(b, mopts, search_req);
  auto search_json = json::parse(search_resp);
  QB_CHECK(search_json["result"]["isError"] == false);

  // N6: embed drain success/failure, dream dry/apply, MCP write deny.
  set_env_var("QBRAIN_EMBED_MOCK", "1");
  auto embed_page = put_indexed(b, "embed-page", "Embed Page", "embed content");
  auto jid = qbrain::jobs::submit_job(b, "embed", std::string(R"({"page_id":)") +
                                                   std::to_string(embed_page.id) + "}");
  QB_CHECK(jid > 0);
  QB_CHECK(b.drain_embed_jobs(10) > 0);
  auto embedded_chunks = b.get_chunks(embed_page.id);
  QB_CHECK(!embedded_chunks.empty());
  QB_CHECK(!embedded_chunks[0].embedding.empty());
  auto done_job = qbrain::jobs::get_job(b, jid);
  QB_CHECK(done_job && done_job->status == "completed");
  QB_CHECK(b.drain_embed_jobs(10) == 0);

  set_env_var("QBRAIN_EMBED_MOCK", "");
  auto fail_page = put_indexed(b, "embed-fail", "Embed Fail", "embed fail content");
  auto fjid = qbrain::jobs::submit_job(b, "embed", std::string(R"({"page_id":)") +
                                                    std::to_string(fail_page.id) + "}");
  QB_CHECK(b.drain_embed_jobs(10) == 0);
  QB_CHECK(b.get_page("embed-fail").has_value());
  auto failed_job = qbrain::jobs::get_job(b, fjid);
  QB_CHECK(failed_job && failed_job->status == "failed");

  auto before_facts = fact_count(b);
  qbrain::cycle::DreamOpts dry;
  dry.dry_run = true;
  dry.phase = "consolidate";
  auto dry_report = qbrain::cycle::run_dream(b, dry);
  QB_CHECK(dry_report.status == "ok");
  QB_CHECK(fact_count(b) == before_facts);
  qbrain::cycle::DreamOpts apply;
  apply.dry_run = false;
  apply.phase = "consolidate";
  auto apply_report = qbrain::cycle::run_dream(b, apply);
  QB_CHECK(apply_report.status == "ok");
  QB_CHECK(fact_count(b) > before_facts);
  qbrain::ops::OpContext dctx;
  dctx.brain = &b;
  dctx.remote = true;
  dctx.allow_write = false;
  dctx.args = {{"apply", "true"}, {"phase", "consolidate"}};
  auto denied = qbrain::ops::global_registry().call("run_dream", dctx);
  QB_CHECK(!denied.ok);
  QB_CHECK(qbrain::jobs::drain_jobs(b, 1, "wave4-empty") == 0);

  // N8: multi-brain id validation, resolution precedence, list/isolation.
  QB_CHECK(qbrain::util::normalize_brain_id("Wave4_B1") == "wave4_b1");
  bool invalid = false;
  try {
    (void)qbrain::util::normalize_brain_id("CON");
  } catch (...) {
    invalid = true;
  }
  QB_CHECK(invalid);
  invalid = false;
  try {
    (void)qbrain::util::normalize_brain_id("../x");
  } catch (...) {
    invalid = true;
  }
  QB_CHECK(invalid);
  invalid = false;
  try {
    (void)qbrain::util::normalize_brain_id(std::string(65, 'x'));
  } catch (...) {
    invalid = true;
  }
  QB_CHECK(invalid);

  auto old_env = std::getenv("QBRAIN_BRAIN") ? std::string(std::getenv("QBRAIN_BRAIN")) : std::string();
  set_env_var("QBRAIN_BRAIN", "wave4_b2");
  QB_CHECK(qbrain::cli::resolve_brain_id({}) == "wave4_b2");
  QB_CHECK(qbrain::cli::resolve_brain_id({"--brain", "wave4_b1"}) == "wave4_b1");
  set_env_var("QBRAIN_BRAIN", old_env.c_str());

  auto b1dir = qbrain::util::brain_dir("wave4_b1");
  auto b2dir = qbrain::util::brain_dir("wave4_b2");
  fs::remove_all(b1dir);
  fs::remove_all(b2dir);
  {
    qbrain::Brain b1("wave4_b1");
    b1.open();
    put_indexed(b1, "only-b1", "Only B1", "brain isolation marker");
    qbrain::Brain b2("wave4_b2");
    b2.open();
    QB_CHECK(!b2.get_page("only-b1").has_value());
    QB_CHECK(qbrain::search::hybrid_search(b2, "isolation", nullptr, title_opts).empty());
  }
  auto brains = qbrain::Brain::list_brains();
  bool saw_b1 = false, saw_b2 = false;
  for (auto& id : brains) {
    if (id == "wave4_b1") saw_b1 = true;
    if (id == "wave4_b2") saw_b2 = true;
  }
  QB_CHECK(saw_b1 && saw_b2);
  fs::remove_all(b1dir);
  fs::remove_all(b2dir);

  b.close();
  fs::remove_all(dir);
}
