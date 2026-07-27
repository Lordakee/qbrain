#include "qbrain/core/brain.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/util/paths.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

void test_n26_27() {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / "qbrain_n2627";
  fs::create_directories(dir);
  auto dbp = dir / "brain.db";
  fs::remove(dbp);

  qbrain::ops::register_builtin_ops();
  qbrain::Brain b("n2627");
  b.open_at(qbrain::util::path_to_utf8(dbp));

  qbrain::ops::OpContext ctx;
  ctx.brain = &b;
  ctx.allow_write = true;

  ctx.args = {{"prompt", "summarize my notes"}};
  auto ag = qbrain::ops::global_registry().call("submit_agent", ctx);
  QB_CHECK(ag.ok);
  QB_CHECK(ag.json.find("waiting") != std::string::npos || ag.text.find("agent") != std::string::npos);

  auto ob = qbrain::ops::global_registry().call("run_onboard", ctx);
  QB_CHECK(ob.ok);

  QB_CHECK(b.put_raw_data("transcript/t1", "hello meeting", R"({"src":"test"})"));
  auto raw = b.get_raw_data("transcript/t1");
  QB_CHECK(raw.has_value());
  QB_CHECK(raw->first.find("hello") != std::string::npos);

  ctx.args = {{"key", "transcript/t1"}};
  auto gr = qbrain::ops::global_registry().call("get_raw_data", ctx);
  QB_CHECK(gr.ok);

  ctx.args = {{"limit", "10"}};
  auto tr = qbrain::ops::global_registry().call("get_recent_transcripts", ctx);
  QB_CHECK(tr.ok);

  ctx.args = {{"limit", "5"}};
  auto sal = qbrain::ops::global_registry().call("get_recent_salience", ctx);
  QB_CHECK(sal.ok);

  ctx.args = {{"name", "photo"}};
  auto img = qbrain::ops::global_registry().call("search_by_image", ctx);
  QB_CHECK(img.ok);

  auto snap = b.status_snapshot();
  QB_CHECK(snap.schema_version >= 11);

  // N28 schema_apply_mutations
  ctx.args = {{"mutations", R"([{"op":"add_type","type":"n28_type"}])"}};
  auto mut = qbrain::ops::global_registry().call("schema_apply_mutations", ctx);
  QB_CHECK(mut.ok);
  auto pack = qbrain::ops::global_registry().call("ontology_get", ctx);
  QB_CHECK(pack.json.find("n28_type") != std::string::npos);

  b.close();
  fs::remove_all(dir);
}
