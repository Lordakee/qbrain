#include "qbrain/core/brain.hpp"
#include "qbrain/files/store.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/schema/lint.hpp"
#include "qbrain/schema/packs.hpp"
#include "qbrain/util/paths.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

void test_n24_25() {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / "qbrain_n2425";
  fs::create_directories(dir);
  auto dbp = dir / "brain.db";
  auto sample = dir / "sample.txt";
  fs::remove(dbp);
  {
    std::ofstream(sample) << "hello attachment\n";
  }

  qbrain::ops::register_builtin_ops();
  qbrain::Brain b("n2425");
  b.open_at(qbrain::util::path_to_utf8(dbp));

  auto id = qbrain::files::upload(b, qbrain::util::path_to_utf8(sample), "sample.txt");
  QB_CHECK(id > 0);
  auto list = qbrain::files::list_files(b, 10);
  QB_CHECK(!list.empty());
  auto url = qbrain::files::file_url(b, id);
  QB_CHECK(url.find("file:") == 0);
  QB_CHECK(url.find("sample") != std::string::npos);

  qbrain::PageInput in;
  in.slug = "x/empty";
  in.title = "";
  in.body = "";
  in.type = "weird_type";
  b.put_page(in);

  auto lint = qbrain::schema::schema_lint(b, 50);
  QB_CHECK(!lint.empty());
  auto graph = qbrain::schema::schema_graph(b);
  QB_CHECK(!graph.empty());
  auto prop = qbrain::schema::ontology_propose(b, 20);
  QB_CHECK(!prop.empty());  // weird_type
  auto conf = qbrain::schema::ontology_conflicts(b, 20);
  QB_CHECK(!conf.empty());
  auto expl = qbrain::schema::schema_explain_type(b, "note");
  QB_CHECK(!expl.empty());

  auto snap = b.status_snapshot();
  QB_CHECK(snap.schema_version >= 10);

  qbrain::ops::OpContext ctx;
  ctx.brain = &b;
  ctx.allow_write = true;
  auto fl = qbrain::ops::global_registry().call("file_list", ctx);
  QB_CHECK(fl.ok);
  auto sl = qbrain::ops::global_registry().call("schema_lint", ctx);
  QB_CHECK(sl.ok);

  b.close();
  fs::remove_all(dir);
}
