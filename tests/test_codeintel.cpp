#include "qbrain/codeintel/scan.hpp"
#include "qbrain/core/brain.hpp"
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

void test_codeintel() {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / "qbrain_test_codeintel";
  fs::create_directories(dir);
  auto dbp = dir / "brain.db";
  fs::remove(dbp);

  qbrain::Brain b("codeintel_test");
  b.open_at(qbrain::util::path_to_utf8(dbp));

  qbrain::PageInput def_page;
  def_page.slug = "code/foo_def";
  def_page.title = "foo def";
  def_page.body =
      "# snippet\n"
      "void foo() {\n"
      "  return;\n"
      "}\n"
      "class Bar {};\n";
  b.put_page(def_page);

  qbrain::PageInput use_page;
  use_page.slug = "code/foo_use";
  use_page.title = "foo use";
  use_page.body =
      "int main() {\n"
      "  foo();\n"
      "  auto x = food;\n"
      "  return 0;\n"
      "}\n";
  b.put_page(use_page);

  auto defs = qbrain::codeintel::find_defs(b, "foo", 50, 100);
  QB_CHECK(!defs.empty());
  bool def_slug = false;
  for (auto& h : defs) {
    if (h.slug == "code/foo_def" && h.snippet.find("void foo()") != std::string::npos) {
      def_slug = true;
      QB_CHECK(h.kind == "def");
      QB_CHECK(h.line == 2);
    }
  }
  QB_CHECK(def_slug);

  auto refs = qbrain::codeintel::find_refs(b, "foo", 50, 100);
  QB_CHECK(refs.size() >= 2);
  bool has_def_ref = false, has_use_ref = false, has_food = false;
  for (auto& h : refs) {
    if (h.slug == "code/foo_def") has_def_ref = true;
    if (h.slug == "code/foo_use") has_use_ref = true;
    if (h.snippet.find("food") != std::string::npos) has_food = true;
  }
  QB_CHECK(has_def_ref);
  QB_CHECK(has_use_ref);
  QB_CHECK(!has_food);

  auto calls = qbrain::codeintel::find_callers(b, "foo", 50, 100);
  QB_CHECK(!calls.empty());
  bool call_use = false;
  for (auto& h : calls) {
    if (h.slug == "code/foo_use" && h.snippet.find("foo()") != std::string::npos) {
      call_use = true;
      QB_CHECK(h.kind == "call");
    }
  }
  QB_CHECK(call_use);

  auto bar_defs = qbrain::codeintel::find_defs(b, "Bar", 10, 100);
  QB_CHECK(!bar_defs.empty());
  QB_CHECK(bar_defs[0].snippet.find("class Bar") != std::string::npos);

  b.close();
  fs::remove_all(dir);
}
