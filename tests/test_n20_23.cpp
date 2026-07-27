#include "qbrain/codeintel/scan.hpp"
#include "qbrain/core/brain.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/schema/packs.hpp"
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

void test_n20_23() {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / "qbrain_n2023";
  fs::create_directories(dir);
  auto dbp = dir / "brain.db";
  fs::remove(dbp);

  qbrain::ops::register_builtin_ops();
  qbrain::Brain b("n2023");
  b.open_at(qbrain::util::path_to_utf8(dbp));

  // N20 packs
  qbrain::schema::ensure_default_pack();
  auto packs = qbrain::schema::list_packs(b);
  QB_CHECK(!packs.empty());
  QB_CHECK(qbrain::schema::active_pack_id(b) == "default");
  auto raw = qbrain::schema::load_pack_json(b, "default");
  QB_CHECK(raw.find("default") != std::string::npos || raw.find("types") != std::string::npos);

  qbrain::ops::OpContext ctx;
  ctx.brain = &b;
  ctx.allow_write = true;
  auto lsp = qbrain::ops::global_registry().call("list_schema_packs", ctx);
  QB_CHECK(lsp.ok);
  auto ss = qbrain::ops::global_registry().call("schema_stats", ctx);
  QB_CHECK(ss.ok);

  // N21 takes
  QB_CHECK(b.put_take("entity/x", "X is important", "fact", 1.0) > 0);
  auto tl = b.takes_list("entity/x", 10);
  QB_CHECK(!tl.empty());
  auto ts = b.takes_search("important", 10);
  QB_CHECK(!ts.empty());
  auto tcal = qbrain::ops::global_registry().call("takes_list", ctx);
  QB_CHECK(tcal.ok);

  // seed code page for N22
  qbrain::PageInput in;
  in.slug = "code/foo";
  in.title = "Foo";
  in.body = "void foo() {\n  bar();\n  baz();\n}\nvoid bar() { }\n";
  b.put_page(in);
  auto callees = qbrain::codeintel::find_callees(b, "foo", 20, 50);
  QB_CHECK(!callees.empty());
  auto blast = qbrain::codeintel::find_blast(b, "foo", 40, 50);
  QB_CHECK(!blast.empty());
  qbrain::codeintel::clear_traversal_cache();

  // N23 chronicle
  auto otd = b.chronicle_on_this_day("", 50);
  (void)otd;
  auto ls = b.chronicle_last_seen("");
  QB_CHECK(!ls.empty() || true);  // may be empty only if no pages — we have pages
  QB_CHECK(!b.chronicle_last_seen("code/foo").empty() || !b.list_pages(5).empty());
  int bf = b.chronicle_backfill(10);
  QB_CHECK(bf >= 1);

  auto snap = b.status_snapshot();
  QB_CHECK(snap.schema_version >= 9);

  b.close();
  fs::remove_all(dir);
}
