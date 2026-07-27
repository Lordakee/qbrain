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

void test_n19() {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / "qbrain_n19_test";
  fs::create_directories(dir);
  auto dbp = dir / "brain.db";
  fs::remove(dbp);

  qbrain::ops::register_builtin_ops();
  qbrain::Brain b("n19");
  b.open_at(qbrain::util::path_to_utf8(dbp));

  qbrain::PageInput in;
  in.slug = "notes/a";
  in.title = "Alpha";
  in.body = "hello world";
  b.put_page(in);
  in.slug = "timeline/t1";
  in.title = "Day one";
  in.body = "event";
  in.type = "timeline";
  b.put_page(in);

  qbrain::ops::OpContext ctx;
  ctx.brain = &b;
  ctx.allow_write = true;

  auto idr = qbrain::ops::global_registry().call("get_brain_identity", ctx);
  QB_CHECK(idr.ok);
  QB_CHECK(idr.json.find("n19") != std::string::npos || idr.json.find("schema_version") != std::string::npos);

  auto vcr = qbrain::ops::global_registry().call("volunteer_context", ctx);
  QB_CHECK(vcr.ok);
  QB_CHECK(vcr.json.find("notes/a") != std::string::npos || vcr.json.find("Alpha") != std::string::npos);

  auto tl = qbrain::ops::global_registry().call("get_timeline", ctx);
  QB_CHECK(tl.ok);
  QB_CHECK(tl.json.find("timeline/t1") != std::string::npos || tl.json.find("Day one") != std::string::npos);

  b.close();
  fs::remove_all(dir);
}
