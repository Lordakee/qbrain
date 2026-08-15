#include "qbrain/graph/extract.hpp"
#include <stdexcept>
#include <string>

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

void test_extract() {
  std::string body = "See [[people/alice]] and [Bob](people/bob.md). Ignore [x](https://ex.com).";
  auto links = qbrain::graph::extract_links("default", "notes/x", body);
  QB_CHECK(links.size() >= 2);
  bool has_alice = false, has_bob = false;
  bool related_alice = false, related_bob = false;
  for (auto& l : links) {
    if (l.to_slug == "people/alice") {
      has_alice = true;
      if (l.link_type == "related") related_alice = true;
    }
    if (l.to_slug == "people/bob") {
      has_bob = true;
      if (l.link_type == "related") related_bob = true;
    }
  }
  QB_CHECK(has_alice);
  QB_CHECK(has_bob);
  QB_CHECK(related_alice);
  QB_CHECK(related_bob);
}
