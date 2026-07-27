#pragma once
#include "qbrain/core/brain.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace qbrain::files {

struct FileEntry {
  int64_t id = 0;
  std::string name;
  std::string path;
  int64_t size = 0;
  std::string mime;
  std::string created_at;
};

// Copy local path into brain files dir; register in file_index. Returns id or 0.
int64_t upload(Brain& brain, const std::string& src_path, const std::string& name = {});
std::vector<FileEntry> list_files(Brain& brain, int limit = 100);
std::string file_url(Brain& brain, int64_t id);
std::string file_url_by_name(Brain& brain, const std::string& name);

}  // namespace qbrain::files
