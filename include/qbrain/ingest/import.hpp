#pragma once
#include "qbrain/core/brain.hpp"
#include <string>

namespace qbrain::ingest {

struct ImportResult {
  int files = 0;
  int pages = 0;
  int links = 0;
  int errors = 0;
};

ImportResult import_path(Brain& brain, const std::string& path,
                         const std::string& source_id = "default");
Page capture_text(Brain& brain, const std::string& text, const std::string& type = "note",
                  const std::string& source_id = "default");

}  // namespace qbrain::ingest
