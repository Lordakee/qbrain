#include "qbrain/ingest/import.hpp"
#include "qbrain/ingest/chunker.hpp"
#include "qbrain/ingest/markdown.hpp"
#include "qbrain/graph/extract.hpp"
#include "qbrain/util/paths.hpp"
#include "qbrain/util/string_util.hpp"
#include "qbrain/util/time_util.hpp"
#include "qbrain/util/hash.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace qbrain::ingest {
namespace fs = std::filesystem;

static std::string read_all(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read " + util::path_to_utf8(p));
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static void index_page(Brain& brain, Page& page) {
  auto chunks = chunk_markdown(page.title, page.body);
  brain.replace_chunks(page.id, chunks);
  auto links = graph::extract_links(page.source_id, page.slug, page.body);
  brain.replace_extracted_links(page.source_id, page.slug, links);
  brain.enqueue_embed_page(page.id);
}

static Page put_file(Brain& brain, const fs::path& file, const fs::path& root) {
  auto rel = fs::relative(file, root);
  auto text = read_all(file);
  auto [fm, body] = split_frontmatter(text);
  PageInput in;
  in.source_id = "default";
  in.slug = slug_from_path(util::path_to_utf8(rel));
  in.title = title_from_body(body, in.slug);
  in.body = body;
  in.frontmatter_json = fm;
  in.type = "note";
  in.source_kind = "import";
  in.ingested_via = "fs";
  try {
    auto j = nlohmann::json::parse(fm);
    if (j.contains("type")) in.type = j["type"].get<std::string>();
    if (j.contains("title")) in.title = j["title"].get<std::string>();
  } catch (...) {
  }
  auto page = brain.put_page(in);
  index_page(brain, page);
  return page;
}

ImportResult import_path(Brain& brain, const std::string& path) {
  ImportResult r;
  fs::path p = util::utf8_to_path(path);
  if (!fs::exists(p)) {
    ++r.errors;
    try {
      brain.log_ingest("import", path,
                       nlohmann::json({{"pages", 0}, {"errors", 1}, {"reason", "missing"}}).dump());
    } catch (...) {
    }
    return r;
  }
  if (fs::is_regular_file(p)) {
    try {
      auto page = put_file(brain, p, p.parent_path());
      ++r.files;
      ++r.pages;
      r.links += static_cast<int>(brain.get_links_from(page.slug).size());
    } catch (...) {
      ++r.errors;
    }
    try {
      brain.log_ingest(
          "import", path,
          nlohmann::json({{"pages", r.pages}, {"files", r.files}, {"links", r.links},
                          {"errors", r.errors}})
              .dump());
    } catch (...) {
    }
    return r;
  }
  for (auto it = fs::recursive_directory_iterator(p); it != fs::recursive_directory_iterator();
       ++it) {
    if (!it->is_regular_file()) continue;
    auto ext = util::to_lower(util::path_to_utf8(it->path().extension()));
    if (ext != ".md" && ext != ".markdown" && ext != ".txt") continue;
    try {
      auto page = put_file(brain, it->path(), p);
      ++r.files;
      ++r.pages;
      r.links += static_cast<int>(brain.get_links_from(page.slug).size());
    } catch (...) {
      ++r.errors;
    }
  }
  try {
    brain.log_ingest(
        "import", path,
        nlohmann::json({{"pages", r.pages}, {"files", r.files}, {"links", r.links},
                        {"errors", r.errors}})
            .dump());
  } catch (...) {
  }
  return r;
}

Page capture_text(Brain& brain, const std::string& text, const std::string& type) {
  auto h = util::sha256_hex(text);
  if (h.size() > 8) h = h.substr(0, 8);
  PageInput in;
  in.slug = "inbox/" + util::utc_date() + "-" + h;
  in.title = title_from_body(text, "capture");
  in.body = text;
  in.type = type;
  in.source_kind = "capture";
  in.ingested_via = "cli";
  auto page = brain.put_page(in);
  index_page(brain, page);
  return page;
}

}  // namespace qbrain::ingest
