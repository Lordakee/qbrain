#pragma once
#include <string>
#include <vector>

namespace qbrain::ingest {

struct ChunkerOpts {
  int target_chars = 600;
  int overlap = 80;
};

std::vector<std::string> chunk_markdown(const std::string& title, const std::string& body,
                                        const ChunkerOpts& opts = {});

}  // namespace qbrain::ingest
