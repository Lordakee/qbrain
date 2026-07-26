#include "qbrain/core/brain.hpp"
#include "qbrain/storage/database.hpp"
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

void test_storage() {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / "qbrain_test_brain";
  fs::create_directories(dir);
  auto dbp = dir / "brain.db";
  fs::remove(dbp);

  // Open from unrelated CWD simulation: only absolute db path, no schema file nearby.
  qbrain::Brain b("test");
  b.open_at(qbrain::util::path_to_utf8(dbp));

  auto integ = qbrain::storage::check_schema_integrity(b.db());
  QB_CHECK(integ.ok);
  QB_CHECK(integ.schema_version >= 3);

  qbrain::PageInput in;
  in.slug = "notes/hello";
  in.title = "Hello";
  in.body = "World [[other]]";
  auto p = b.put_page(in);
  QB_CHECK(p.id > 0);

  auto got = b.get_page("notes/hello");
  QB_CHECK(got.has_value());
  QB_CHECK(got->title == "Hello");

  auto st = b.stats();
  QB_CHECK(st.pages == 1);

  auto h = b.health();
  QB_CHECK(h.schema_version >= 3);
  QB_CHECK(h.schema_version >= 7);

  // N15: link sources
  qbrain::Link link;
  link.from_slug = "notes/hello";
  link.to_slug = "other";
  link.link_source = "markdown";
  b.add_link(link);
  link.to_slug = "manual-target";
  link.link_source = "manual";
  b.add_link(link);
  auto sources = b.list_link_sources();
  QB_CHECK(sources.size() >= 2);
  bool saw_md = false, saw_manual = false;
  for (auto& s : sources) {
    if (s.link_source == "markdown" && s.count >= 1) saw_md = true;
    if (s.link_source == "manual" && s.count >= 1) saw_manual = true;
  }
  QB_CHECK(saw_md && saw_manual);

  // N15: ingest log
  auto lid = b.log_ingest("import", "/tmp/notes", R"({"pages":1,"errors":0})", 50);
  QB_CHECK(lid > 0);
  auto log = b.get_ingest_log(10);
  QB_CHECK(!log.empty());
  QB_CHECK(log[0].path == "/tmp/notes");

  // N15: chronicle
  auto day = p.updated_at.substr(0, 10);
  auto day_hits = b.chronicle_day(day, 50);
  QB_CHECK(!day_hits.empty());
  bool found = false;
  for (auto& ch : day_hits) {
    if (ch.slug == "notes/hello") found = true;
  }
  QB_CHECK(found);
  auto since_hits = b.chronicle_since(day, 50);
  QB_CHECK(!since_hits.empty());

  b.close();
  fs::remove_all(dir);
}
