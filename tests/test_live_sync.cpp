#include "qbrain/core/brain.hpp"
#include "qbrain/service/live_sync.hpp"
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

void test_live_sync() {
  namespace fs = std::filesystem;
  auto root = fs::temp_directory_path() / "qbrain_live_sync_test";
  auto notes = root / "notes";
  auto dbp = root / "brain.db";
  fs::remove_all(root);
  fs::create_directories(notes);

  qbrain::Brain b("live_sync_test");
  b.open_at(qbrain::util::path_to_utf8(dbp));

  {
    std::ofstream(notes / "a.md") << "# Hello\n\nLive sync note about alpha.\n";
  }
  auto r1 = qbrain::service::live_sync_once(b, qbrain::util::path_to_utf8(notes));
  QB_CHECK(r1.imported_pages >= 1);
  QB_CHECK(r1.errors == 0);

  auto r2 = qbrain::service::live_sync_once(b, qbrain::util::path_to_utf8(notes));
  QB_CHECK(r2.imported_pages == 0);
  QB_CHECK(r2.skipped >= 1);

  {
    std::ofstream(notes / "b.md") << "# Beta\n\nSecond note.\n";
  }
  auto r3 = qbrain::service::live_sync_once(b, qbrain::util::path_to_utf8(notes));
  QB_CHECK(r3.imported_pages >= 1);

  b.close();
  fs::remove_all(root);
}
