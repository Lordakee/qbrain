#include "qbrain/ingest/chunker.hpp"
#include <stdexcept>
#include <string>

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

void test_chunker() {
  std::string body;
  for (int i = 0; i < 40; ++i) body += "Paragraph " + std::to_string(i) + " with some text.\n\n";
  auto chunks = qbrain::ingest::chunk_markdown("Title", body, {200, 20});
  QB_CHECK(chunks.size() >= 2);
  QB_CHECK(!chunks[0].empty());
}
