#include "qbrain/files/store.hpp"
#include "qbrain/util/paths.hpp"
#include "qbrain/util/string_util.hpp"
#include "qbrain/util/time_util.hpp"
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace qbrain::files {
namespace {

fs::path files_root(Brain& brain) {
  auto d = util::brain_dir(brain.brain_id()) / "files";
  util::ensure_dir(d);
  return d;
}

std::string guess_mime(const std::string& name) {
  auto lower = util::to_lower(name);
  if (util::ends_with(lower, ".md") || util::ends_with(lower, ".markdown")) return "text/markdown";
  if (util::ends_with(lower, ".txt")) return "text/plain";
  if (util::ends_with(lower, ".json")) return "application/json";
  if (util::ends_with(lower, ".png")) return "image/png";
  if (util::ends_with(lower, ".jpg") || util::ends_with(lower, ".jpeg")) return "image/jpeg";
  if (util::ends_with(lower, ".pdf")) return "application/pdf";
  return "application/octet-stream";
}

std::string safe_name(const std::string& name) {
  std::string out;
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')
      out.push_back(c);
    else if (c == ' ')
      out.push_back('_');
  }
  if (out.empty()) out = "file";
  if (out == "." || out == "..") out = "file";
  return out;
}

}  // namespace

int64_t upload(Brain& brain, const std::string& src_path, const std::string& name) {
  fs::path src = util::utf8_to_path(src_path);
  if (!fs::exists(src) || !fs::is_regular_file(src)) return 0;
  auto base = name.empty() ? util::path_to_utf8(src.filename()) : name;
  base = safe_name(base);
  auto dest_dir = files_root(brain);
  auto dest = dest_dir / base;
  // uniquify
  if (fs::exists(dest)) {
    auto stem = dest.stem().string();
    auto ext = dest.extension().string();
    for (int i = 1; i < 1000; ++i) {
      dest = dest_dir / (stem + "_" + std::to_string(i) + ext);
      if (!fs::exists(dest)) break;
    }
  }
  fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
  auto sz = static_cast<int64_t>(fs::file_size(dest));
  auto path = util::path_to_utf8(dest);
  auto mime = guess_mime(util::path_to_utf8(dest.filename()));
  auto st = brain.db().prepare(
      "INSERT INTO file_index(name, path, size, mime, created_at) VALUES(?,?,?,?,?)");
  st.bind_text(1, util::path_to_utf8(dest.filename()));
  st.bind_text(2, path);
  st.bind_int(3, sz);
  st.bind_text(4, mime);
  st.bind_text(5, util::utc_now());
  st.step_done();
  return brain.db().last_insert_rowid();
}

std::vector<FileEntry> list_files(Brain& brain, int limit) {
  std::vector<FileEntry> out;
  if (limit <= 0) limit = 100;
  auto st = brain.db().prepare(
      "SELECT id, name, path, size, mime, created_at FROM file_index ORDER BY id DESC LIMIT ?");
  st.bind_int(1, limit);
  while (st.step()) {
    FileEntry e;
    e.id = st.column_int(0);
    e.name = st.column_text(1);
    e.path = st.column_text(2);
    e.size = st.column_int(3);
    e.mime = st.column_text(4);
    e.created_at = st.column_text(5);
    out.push_back(std::move(e));
  }
  return out;
}

std::string file_url(Brain& brain, int64_t id) {
  auto st = brain.db().prepare("SELECT path FROM file_index WHERE id=?");
  st.bind_int(1, id);
  if (!st.step()) return {};
  auto path = st.column_text(0);
  if (path.empty()) return {};
  // Windows file URL
  std::string p = path;
  for (auto& c : p)
    if (c == '\\') c = '/';
  if (p.size() >= 2 && p[1] == ':') return "file:///" + p;
  return "file://" + p;
}

std::string file_url_by_name(Brain& brain, const std::string& name) {
  auto st = brain.db().prepare("SELECT id FROM file_index WHERE name=? ORDER BY id DESC LIMIT 1");
  st.bind_text(1, name);
  if (!st.step()) return {};
  return file_url(brain, st.column_int(0));
}

}  // namespace qbrain::files
