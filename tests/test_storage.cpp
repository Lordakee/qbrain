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

  b.close();
  fs::remove_all(dir);
}
