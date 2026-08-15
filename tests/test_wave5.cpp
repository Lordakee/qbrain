#include "qbrain/core/brain.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/util/paths.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

namespace {

int facts_count(qbrain::Brain& b, const std::string& entity = {}) {
  std::string sql = "SELECT COUNT(*) FROM facts WHERE active=1";
  if (!entity.empty()) sql += " AND entity_slug=?";
  auto st = b.db().prepare(sql);
  if (!entity.empty()) st.bind_text(1, entity);
  if (st.step()) return static_cast<int>(st.column_int(0));
  return 0;
}

qbrain::Page put_page(qbrain::Brain& b, const std::string& slug, const std::string& title,
                      const std::string& body) {
  qbrain::PageInput in;
  in.slug = slug;
  in.title = title;
  in.body = body;
  return b.put_page(in);
}

}  // namespace

void test_wave5() {
  namespace fs = std::filesystem;
  qbrain::ops::register_builtin_ops();
  auto dir = fs::temp_directory_path() / "qbrain_wave5_test";
  fs::remove_all(dir);
  fs::create_directories(dir);

  qbrain::Brain b("wave5");
  b.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));

  put_page(b, "n10/entity", "N10 Fact Title", "Fact body mentions [[n10/target]].");
  qbrain::Link link;
  link.from_slug = "n10/entity";
  link.to_slug = "n10/target";
  link.link_type = "mentions";
  b.add_link(link);

  int before = facts_count(b, "n10/entity");
  QB_CHECK(b.extract_facts_from_page("n10/entity") >= 1);
  QB_CHECK(facts_count(b, "n10/entity") > before);

  auto facts = b.list_facts("n10/entity", 10);
  QB_CHECK(!facts.empty());
  bool saw_title = false;
  bool saw_target = false;
  for (auto& f : facts) {
    if (f.find("N10 Fact Title") != std::string::npos) saw_title = true;
    if (f.find("n10/target") != std::string::npos) saw_target = true;
  }
  QB_CHECK(saw_title || saw_target);

  QB_CHECK(b.extract_facts_from_page("n10/entity") >= 1);
  QB_CHECK(b.list_facts("n10/entity", 10).size() <= 10);

  qbrain::ops::OpContext ctx;
  ctx.brain = &b;
  ctx.args = {{"entity_slug", "n10/entity"}, {"limit", "10"}};
  auto lf = qbrain::ops::global_registry().call("list_facts", ctx);
  QB_CHECK(lf.ok);
  QB_CHECK(lf.json.find("N10 Fact Title") != std::string::npos ||
           lf.json.find("n10/target") != std::string::npos);

  qbrain::ops::OpContext unk;
  unk.brain = &b;
  unk.args = {{"entity_slug", "missing/entity"}, {"depth", "99"}, {"limit", "500"}};
  auto unknown = qbrain::ops::global_registry().call("find_trajectory", unk);
  QB_CHECK(unknown.ok);
  auto uj = json::parse(unknown.json);
  QB_CHECK(uj.is_array());
  QB_CHECK(uj.empty());

  for (int i = 0; i < 150; ++i) {
    b.add_fact("bulk/entity", "p", "obj" + std::to_string(i));
  }
  qbrain::ops::OpContext traj;
  traj.brain = &b;
  traj.args = {{"entity_slug", "bulk/entity"}, {"depth", "999"}, {"limit", "999"}};
  auto tr = qbrain::ops::global_registry().call("find_trajectory", traj);
  QB_CHECK(tr.ok);
  auto tj = json::parse(tr.json);
  QB_CHECK(tj.is_array());
  QB_CHECK(tj.size() <= 100);
  for (auto& step : tj) {
    QB_CHECK(step.value("depth", 99) <= 4);
  }

  qbrain::mcp::ServeOptions opts;
  opts.allow_write = false;
  auto deny_req =
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"extract_facts","arguments":{"slug":"n10/entity"}}})";
  auto deny_resp = qbrain::mcp::handle_rpc_body(b, opts, deny_req);
  auto dj = json::parse(deny_resp);
  QB_CHECK(dj["result"]["isError"] == true);

  auto read_req =
      R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"find_trajectory","arguments":{"entity_slug":"n10/entity","limit":10}}})";
  auto read_resp = qbrain::mcp::handle_rpc_body(b, opts, read_req);
  auto rj = json::parse(read_resp);
  QB_CHECK(rj["result"]["isError"] == false);

  auto list_req =
      R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"list_facts","arguments":{"entity_slug":"n10/entity","limit":10}}})";
  auto list_resp = qbrain::mcp::handle_rpc_body(b, opts, list_req);
  auto lj = json::parse(list_resp);
  QB_CHECK(lj["result"]["isError"] == false);

  b.close();
  fs::remove_all(dir);
}
