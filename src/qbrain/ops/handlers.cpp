#include "qbrain/ops/registry.hpp"
#include "qbrain/ai/chat.hpp"
#include "qbrain/ai/embed.hpp"
#include "qbrain/codeintel/scan.hpp"
#include "qbrain/cycle/dream.hpp"
#include "qbrain/graph/analytics.hpp"
#include "qbrain/schema/packs.hpp"
#include "qbrain/schema/lint.hpp"
#include "qbrain/files/store.hpp"
#include "qbrain/graph/extract.hpp"
#include "qbrain/graph/traverse.hpp"
#include "qbrain/ingest/chunker.hpp"
#include "qbrain/ingest/import.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/search/hybrid.hpp"
#include "qbrain/service/live_sync.hpp"
#include "qbrain/util/string_util.hpp"
#include "qbrain/util/time_util.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/paths.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

namespace qbrain::ops {
namespace {

std::string arg(OpContext& ctx, const std::string& k, const std::string& def = {}) {
  auto it = ctx.args.find(k);
  return it == ctx.args.end() ? def : it->second;
}

int arg_int(OpContext& ctx, const std::string& k, int def) {
  auto s = arg(ctx, k);
  if (s.empty()) return def;
  try {
    return std::stoi(s);
  } catch (...) {
    return def;
  }
}

OpResult argument_error(const std::string& code, const std::string& field,
                         const std::string& message) {
  OpResult r;
  r.ok = false;
  r.exit_code = 1;
  json j = {{"error", {{"code", code}, {"field", field}, {"message", message}}}};
  r.json = j.dump();
  r.text = message;
  return r;
}

OpResult n20_error(const std::string& code, const std::string& field,
                   const std::string& message) {
  OpResult result = argument_error(code, field, message);
  result.text = result.json;
  return result;
}

OpResult normalize_n20_error(OpResult result) {
  if (!result.json.empty()) {
    result.text = result.json;
    if (result.exit_code == 0) result.exit_code = 1;
    result.ok = false;
    return result;
  }
  return n20_error("database_error", "database", "schema operation failed");
}

OpResult n20_pack_error(const schema::PackError& error) {
  const auto& code = error.code();
  if (code == "invalid_pack_id")
    return n20_error(code, "id", "invalid schema pack id");
  if (code == "pack_not_found")
    return n20_error(code, "id", "schema pack not found");
  if (code == "pack_invalid")
    return n20_error(code, "pack", "schema pack is invalid");
  if (code == "pack_unsafe")
    return n20_error(code, "pack", "schema pack candidate is unsafe");
  if (code == "pack_too_large")
    return n20_error(code, "pack", "schema pack exceeds the size limit");
  if (code == "pack_limit_exceeded")
    return n20_error(code, "pack", "schema pack discovery limit exceeded");
  if (code == "filesystem_error")
    return n20_error(code, "pack", "schema pack storage is unavailable");
  if (code == "database_error")
    return n20_error(code, "database", "schema pack database operation failed");
  if (code == "invalid_argument")
    return n20_error(code, "limit", "schema statistics limit is out of range");
  if (code == "invalid_source")
    return n20_error(code, "source_id", "invalid source_id");
  if (code == "source_not_found")
    return n20_error(code, "source_id", "source_id is not registered");
  return n20_error("database_error", "database", "schema operation failed");
}

std::optional<std::string> optional_n20_pack_id(OpContext& ctx) {
  const auto requested = ctx.args.find("id");
  if (requested == ctx.args.end()) return std::nullopt;
  return requested->second;
}

json n20_pack_payload(const schema::LoadedPack& loaded) {
  return {{"id", loaded.id},
          {"origin", loaded.origin},
          {"pack", json::parse(schema::manifest_json(loaded.manifest))}};
}

bool parse_bounded_uint(OpContext& ctx, const std::string& field, uint64_t default_value,
                         uint64_t minimum, uint64_t maximum, int& out, OpResult& error) {
  auto it = ctx.args.find(field);
  uint64_t value = default_value;
  if (it != ctx.args.end()) {
    const auto& text = it->second;
    if (text.empty()) {
      error = argument_error("invalid_argument", field, "unsigned decimal value required");
      return false;
    }
    const char* first = text.data();
    const char* last = first + text.size();
    auto parsed = std::from_chars(first, last, value, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
      error = argument_error("invalid_argument", field, "unsigned decimal value required");
      return false;
    }
  }
  value = std::clamp(value, minimum, maximum);
  out = static_cast<int>(value);
  return true;
}

std::string bounded_error_field(std::string_view field) {
  if (field.empty() || field.size() > 64) return "argument";
  for (const unsigned char c : field) {
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    if (!allowed) return "argument";
  }
  return std::string(field);
}

bool validate_analytics_args(OpContext& ctx, OpResult& error) {
  for (const auto& [field, _] : ctx.args) {
    if (field == "source_id" || field == "limit") continue;
    error = argument_error("invalid_argument", bounded_error_field(field),
                           "unexpected argument");
    return false;
  }
  return true;
}

bool parse_positive_i64(OpContext& ctx, const std::string& field, int64_t& out,
                        OpResult& error) {
  auto it = ctx.args.find(field);
  if (it == ctx.args.end() || it->second.empty()) {
    error = argument_error("invalid_argument", field, "positive decimal job id required");
    return false;
  }
  uint64_t value = 0;
  const char* first = it->second.data();
  const char* last = first + it->second.size();
  auto parsed = std::from_chars(first, last, value, 10);
  if (parsed.ec != std::errc{} || parsed.ptr != last || value == 0 ||
      value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    error = argument_error("invalid_argument", field, "positive decimal job id required");
    return false;
  }
  out = static_cast<int64_t>(value);
  return true;
}

bool validate_allowed_args(OpContext& ctx,
                           std::initializer_list<std::string_view> allowed,
                           OpResult& error) {
  std::string unexpected;
  for (const auto& [field, _] : ctx.args) {
    const bool is_allowed =
        std::find(allowed.begin(), allowed.end(), std::string_view(field)) != allowed.end();
    if (!is_allowed && (unexpected.empty() || field < unexpected)) unexpected = field;
  }
  if (unexpected.empty()) return true;
  error = argument_error("invalid_argument", bounded_error_field(unexpected),
                         "unexpected argument");
  return false;
}

bool parse_positive_i64_text(const std::string& text, int64_t& out) {
  if (text.empty()) return false;
  uint64_t value = 0;
  const char* first = text.data();
  const char* last = first + text.size();
  const auto parsed = std::from_chars(first, last, value, 10);
  if (parsed.ec != std::errc{} || parsed.ptr != last || value == 0 ||
      value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    return false;
  out = static_cast<int64_t>(value);
  return true;
}

bool parse_job_id_alias(OpContext& ctx, int64_t& out, OpResult& error) {
  const auto canonical = ctx.args.find("job_id");
  const auto legacy = ctx.args.find("id");
  if (canonical == ctx.args.end() && legacy == ctx.args.end()) {
    error = argument_error("invalid_argument", "job_id", "positive decimal job id required");
    return false;
  }

  int64_t canonical_id = 0;
  int64_t legacy_id = 0;
  if (canonical != ctx.args.end() && !parse_positive_i64_text(canonical->second, canonical_id)) {
    error = argument_error("invalid_argument", "job_id", "positive decimal job id required");
    return false;
  }
  if (legacy != ctx.args.end() && !parse_positive_i64_text(legacy->second, legacy_id)) {
    error = argument_error("invalid_argument", "job_id", "positive decimal job id required");
    return false;
  }
  if (canonical != ctx.args.end() && legacy != ctx.args.end() && canonical_id != legacy_id) {
    error = argument_error("invalid_argument", "job_id", "job_id and id must match");
    return false;
  }
  out = canonical != ctx.args.end() ? canonical_id : legacy_id;
  return true;
}

const char* job_input_field_name(jobs::JobInputField field) {
  switch (field) {
    case jobs::JobInputField::job_id:
      return "job_id";
    case jobs::JobInputField::sender:
      return "sender";
    case jobs::JobInputField::payload_json:
      return "payload_json";
    case jobs::JobInputField::none:
      return "job_id";
  }
  return "job_id";
}

bool is_database_busy_error(const std::exception& error) {
  std::string message = error.what();
  for (char& c : message) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return message.find("busy") != std::string::npos ||
         message.find("locked") != std::string::npos;
}

bool is_utf8_continuation(unsigned char byte) { return (byte & 0xC0u) == 0x80u; }

size_t utf8_code_point_length(std::string_view value, size_t offset) {
  const auto byte_at = [&](size_t index) {
    return static_cast<unsigned char>(value[index]);
  };
  const size_t remaining = value.size() - offset;
  const unsigned char first = byte_at(offset);
  if (first <= 0x7Fu) return 1;
  if (first >= 0xC2u && first <= 0xDFu)
    return remaining >= 2 && is_utf8_continuation(byte_at(offset + 1)) ? 2 : 0;
  if (first == 0xE0u)
    return remaining >= 3 && byte_at(offset + 1) >= 0xA0u &&
                   byte_at(offset + 1) <= 0xBFu &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  if ((first >= 0xE1u && first <= 0xECu) ||
      (first >= 0xEEu && first <= 0xEFu))
    return remaining >= 3 && is_utf8_continuation(byte_at(offset + 1)) &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  if (first == 0xEDu)
    return remaining >= 3 && byte_at(offset + 1) >= 0x80u &&
                   byte_at(offset + 1) <= 0x9Fu &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  if (first == 0xF0u)
    return remaining >= 4 && byte_at(offset + 1) >= 0x90u &&
                   byte_at(offset + 1) <= 0xBFu &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  if (first >= 0xF1u && first <= 0xF3u)
    return remaining >= 4 && is_utf8_continuation(byte_at(offset + 1)) &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  if (first == 0xF4u)
    return remaining >= 4 && byte_at(offset + 1) >= 0x80u &&
                   byte_at(offset + 1) <= 0x8Fu &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  return 0;
}

bool is_valid_utf8(std::string_view value) {
  for (size_t offset = 0; offset < value.size();) {
    const size_t length = utf8_code_point_length(value, offset);
    if (length == 0) return false;
    offset += length;
  }
  return true;
}

std::string bounded_display_utf8(std::string_view value, size_t maximum,
                                 bool mark_truncated) {
  constexpr std::string_view replacement = "\xEF\xBF\xBD";
  constexpr std::string_view marker = "[truncated]";
  std::string output;
  output.reserve(std::min(value.size(), maximum));
  size_t offset = 0;
  while (offset < value.size()) {
    const size_t length = utf8_code_point_length(value, offset);
    const size_t unit_size = length == 0 ? replacement.size() : length;
    if (unit_size > maximum - output.size()) break;
    if (length == 0)
      output.append(replacement);
    else
      output.append(value.data() + offset, length);
    offset += length == 0 ? 1 : length;
  }
  if (mark_truncated && offset < value.size() && marker.size() <= maximum) {
    while (!output.empty() && output.size() + marker.size() > maximum) {
      size_t start = output.size() - 1;
      while (start > 0 &&
             is_utf8_continuation(static_cast<unsigned char>(output[start])))
        --start;
      output.resize(start);
    }
    output.append(marker);
  }
  return output;
}

bool is_ascii_whitespace_only(std::string_view value) {
  for (const unsigned char c : value) {
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\f' && c != '\v')
      return false;
  }
  return true;
}

bool remote_source_allowed(OpContext& ctx, const std::string& source_id) {
  // N20/N30: any non-CLI caller (network remote or MCP transport) is subject
  // to the configured source allow-list.
  if (!(ctx.remote || ctx.via_mcp) || source_id == "default") return true;
  auto configured = ctx.brain->get_config_value("mcp.allowed_sources");
  if (!configured) return false;
  for (auto item : util::split(*configured, ',')) {
    auto canon = Brain::canonical_source_id(util::trim(item));
    if (canon && *canon == source_id) return true;
  }
  return false;
}

std::optional<std::string> resolve_source(OpContext& ctx, bool require_existing,
                                          OpResult& error) {
  auto it = ctx.args.find("source_id");
  std::string raw = it == ctx.args.end() ? "default" : it->second;
  auto canon = Brain::canonical_source_id(raw);
  if (!canon) {
    error = argument_error("invalid_source", "source_id", "invalid source_id");
    return std::nullopt;
  }
  if (!remote_source_allowed(ctx, *canon)) {
    error = argument_error("source_not_allowed", "source_id",
                           "source_id is not authorized for remote access");
    return std::nullopt;
  }
  if (require_existing && !ctx.brain->source_exists(*canon)) {
    error = argument_error("source_not_found", "source_id", "source_id is not registered");
    return std::nullopt;
  }
  return canon;
}

bool date_parts_valid(const std::string& value) {
  if (value.size() < 10 || value[4] != '-' || value[7] != '-') return false;
  constexpr size_t digit_positions[] = {0, 1, 2, 3, 5, 6, 8, 9};
  for (size_t i : digit_positions)
    if (value[i] < '0' || value[i] > '9') return false;
  auto number = [&value](size_t start, size_t count) {
    int n = 0;
    for (size_t i = start; i < start + count; ++i) n = n * 10 + (value[i] - '0');
    return n;
  };
  int year = number(0, 4);
  int month = number(5, 2);
  int day = number(8, 2);
  static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year < 1 || month < 1 || month > 12) return false;
  int max_day = days[month];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) max_day = 29;
  return day >= 1 && day <= max_day;
}

bool valid_utc_day(const std::string& value) {
  return value.size() == 10 && date_parts_valid(value);
}

bool valid_utc_since(const std::string& value) {
  if (value.size() == 10) return date_parts_valid(value);
  if (value.size() != 20 || !date_parts_valid(value) ||
      (value[10] != 'T' && value[10] != ' ') || value[13] != ':' || value[16] != ':' ||
      value[19] != 'Z')
    return false;
  constexpr size_t digit_positions[] = {11, 12, 14, 15, 17, 18};
  for (size_t i : digit_positions)
    if (value[i] < '0' || value[i] > '9') return false;
  auto two = [&value](size_t pos) { return (value[pos] - '0') * 10 + value[pos + 1] - '0'; };
  return two(11) <= 23 && two(14) <= 59 && two(17) <= 59;
}

bool valid_month_day(const std::string& value) {
  return value.size() == 5 && value[2] == '-' && valid_utc_day("2000-" + value);
}

std::optional<std::chrono::sys_days> parse_utc_sys_day(const std::string& value) {
  if (!valid_utc_day(value)) return std::nullopt;
  auto component = [&value](size_t start, size_t count) {
    int result = 0;
    for (size_t i = start; i < start + count; ++i)
      result = result * 10 + (value[i] - '0');
    return result;
  };
  const std::chrono::year_month_day date{
      std::chrono::year{component(0, 4)},
      std::chrono::month{static_cast<unsigned>(component(5, 2))},
      std::chrono::day{static_cast<unsigned>(component(8, 2))}};
  if (!date.ok()) return std::nullopt;
  return std::chrono::sys_days{date};
}

bool parse_boolean(OpContext& ctx, const std::string& field, bool default_value,
                   bool& out, OpResult& error) {
  const auto it = ctx.args.find(field);
  if (it == ctx.args.end()) {
    out = default_value;
    return true;
  }
  if (it->second == "true") {
    out = true;
    return true;
  }
  if (it->second == "false") {
    out = false;
    return true;
  }
  error = argument_error("invalid_argument", field, "boolean value required");
  return false;
}

json health_report_json(const qbrain::HealthReport& h, bool include_db_path) {
  const char* overall = h.ok ? "OK" : "FAIL";
  json j;
  j["ok"] = h.ok;
  j["overall"] = overall;
  if (include_db_path) j["db_path"] = h.db_path;
  j["schema_version"] = h.schema_version;
  j["stats"] = {{"pages", h.stats.pages},
                  {"chunks", h.stats.chunks},
                  {"links", h.stats.links},
                  {"embedded_chunks", h.stats.embedded_chunks}};
  j["checks"] = json::array({
      {{"name", "database"}, {"status", "OK"}},
      {{"name", "schema"}, {"status", overall}},
      {{"name", "critical_tables"}, {"status", overall}},
      {{"name", "optional"}, {"status", h.notes.empty() ? "OK" : "WARN"}},
  });
  j["notes"] = h.notes;
  return j;
}

void register_one(const char* name, Scope scope, OpHandler h, bool local_only = false,
                  const char* description = "", const char* input_schema = "") {
  Operation op;
  op.name = name;
  op.scope = scope;
  op.local_only = local_only;
  op.description = description ? description : "";
  op.input_schema_json =
      (input_schema && *input_schema) ? input_schema
                                      : R"({"type":"object","properties":{}})";
  op.handler = std::move(h);
  global_registry().add(std::move(op));
}

}  // namespace

void register_builtin_ops() {
  register_one(
      "get_health", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto h = ctx.brain->health();
    // N30 D4: the local database path is only disclosed to local callers.
    json j = health_report_json(h, !(ctx.remote || ctx.via_mcp));
    r.json = j.dump(2);
    std::ostringstream oss;
    oss << "Qbrain doctor: " << j["overall"].get<std::string>() << "\n";
    if (!(ctx.remote || ctx.via_mcp)) oss << "  db: " << h.db_path << "\n";
    oss << "  schema: v" << h.schema_version << "\n"
        << "  pages=" << h.stats.pages << " chunks=" << h.stats.chunks
        << " links=" << h.stats.links << " embedded=" << h.stats.embedded_chunks << "\n";
    for (auto& n : h.notes) oss << "  - " << n << "\n";
    r.text = oss.str();
    r.ok = h.ok;
    return r;
  }, false, "Brain health / doctor report", R"({"type":"object","properties":{}})");

  register_one(
      "get_stats", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto s = ctx.brain->stats();
    json j = {{"pages", s.pages},
              {"chunks", s.chunks},
              {"links", s.links},
              {"embedded_chunks", s.embedded_chunks}};
    r.json = j.dump(2);
    r.text = r.json;
    return r;
  }, false, "Page/chunk/link statistics", R"({"type":"object","properties":{}})");

  register_one(
      "put_page", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    PageInput in;
    in.slug = arg(ctx, "slug");
    in.title = arg(ctx, "title", in.slug);
    in.body = arg(ctx, "body");
    in.type = arg(ctx, "type", "note");
    in.source_id = arg(ctx, "source_id", "default");
    // N2.5: canonicalize + remote allowlist (case-insensitive); always on when remote
    {
      std::string sid = in.source_id;
      for (char& c : sid) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
      }
      in.source_id = sid;
    }
    if ((ctx.remote || ctx.via_mcp) && in.source_id != "default") {
      auto allow = ctx.brain->get_config_value("mcp.allowed_sources");
      bool ok = false;
      if (allow) {
        for (auto& p : util::split(*allow, ',')) {
          auto t = util::trim(p);
          for (char& c : t) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
          }
          if (t == in.source_id) ok = true;
        }
      }
      if (!ok) {
        OpResult r;
        r.ok = false;
        r.text = "remote source_id not allowed (only default, or mcp.allowed_sources)";
        return r;
      }
    }
    if (!ctx.brain->ensure_source(in.source_id)) {
      OpResult r;
      r.ok = false;
      r.text = "invalid source_id";
      return r;
    }
    if (in.slug.empty()) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "slug required";
      return r;
    }
    // Provenance: remote MCP stamps mcp:put_page (gbrain-like)
    if (ctx.via_mcp) {
      in.source_kind = "mcp:put_page";
      in.ingested_via = "mcp";
    } else {
      in.source_kind = in.source_kind.empty() ? "put_page" : in.source_kind;
      in.ingested_via = in.ingested_via.empty() ? "cli" : in.ingested_via;
    }
    auto page = ctx.brain->put_page(in);
    auto chunks = ingest::chunk_markdown(page.title, page.body);
    ctx.brain->replace_chunks(page.id, chunks);
    // Remote callers: skip auto-link (gbrain mitigation vs backlink poisoning)
    size_t nlinks = 0;
    if (!ctx.via_mcp) {
      auto links = graph::extract_links(page.source_id, page.slug, page.body);
      ctx.brain->replace_extracted_links(page.source_id, page.slug, links);
      nlinks = links.size();
    }
    ctx.brain->enqueue_embed_page(page.id);
    r.text = "put " + page.slug + " id=" + std::to_string(page.id);
    json j = {{"id", page.id},
              {"slug", page.slug},
              {"chunks", chunks.size()},
              {"links", nlinks},
              {"embed_enqueued", true}};
    r.json = j.dump(2);
    return r;
  }, true, "Create or update a page (localOnly unless MCP --allow-write)",
      R"({"type":"object","properties":{"slug":{"type":"string"},"title":{"type":"string"},"body":{"type":"string"},"type":{"type":"string"},"source_id":{"type":"string"}},"required":["slug"]})");

  register_one(
      "get_page", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "slug");
    auto page = ctx.brain->get_page(slug, arg(ctx, "source_id", "default"));
    if (!page) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "not found: " + slug;
      return r;
    }
    json j = {{"id", page->id},
              {"slug", page->slug},
              {"type", page->type},
              {"title", page->title},
              {"body", page->body},
              {"updated_at", page->updated_at}};
    r.json = j.dump(2);
    r.text = "# " + page->title + "\n\n" + page->body + "\n";
    return r;
  }, false, "Get a page by slug",
      R"({"type":"object","properties":{"slug":{"type":"string"},"source_id":{"type":"string"}},"required":["slug"]})");

  register_one(
      "list_pages", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    int limit = arg_int(ctx, "limit", 50);
    auto type = arg(ctx, "type");
    auto pages = ctx.brain->list_pages(limit, type);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& p : pages) {
      arr.push_back({{"slug", p.slug}, {"type", p.type}, {"title", p.title}, {"updated_at", p.updated_at}});
      oss << p.slug << "\t" << p.type << "\t" << p.title << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str();
    return r;
  }, false, "List pages",
      R"({"type":"object","properties":{"limit":{"type":"integer"},"type":{"type":"string"}}})");

  register_one(
      "search", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto q = arg(ctx, "query");
    if (q.empty()) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "query required";
      return r;
    }
    search::HybridOpts opts;
    opts.limit = arg_int(ctx, "limit", ctx.brain->config().search_default_limit);
    opts.rrf_k = ctx.brain->config().search_rrf_k;
    opts.source_id = arg(ctx, "source_id");
    opts.mode = arg(ctx, "mode", "balanced");
    opts.config = &ctx.brain->config();
    auto rr = arg(ctx, "rerank");
    if (rr == "1" || rr == "true") opts.rerank = true;
    auto rrl = arg(ctx, "rerank_llm");
    if (rrl == "1" || rrl == "true") opts.rerank_llm = true;
    std::vector<float> emb;
    std::vector<float>* pemb = nullptr;
    // no_vector accepts "1"/"true" from CLI and MCP bool mapping
    auto nv = arg(ctx, "no_vector");
    if (nv != "1" && nv != "true" && opts.mode != "conservative") {
      auto er = ai::embed_texts(ctx.brain->config(), {q});
      if (er.ok && !er.vectors.empty()) {
        emb = er.vectors[0];
        pemb = &emb;
      }
    }
    auto hits = search::hybrid_search(*ctx.brain, q, pemb, opts);
    json arr = json::array();
    std::ostringstream oss;
    int i = 1;
    for (auto& h : hits) {
      arr.push_back({{"rank", i},
                     {"slug", h.slug},
                     {"title", h.title},
                     {"score", h.score},
                     {"rerank_score", h.rerank_score},
                     {"snippet", h.snippet}});
      oss << i << ". " << h.slug << "  (" << h.score << ")\n   " << h.title << "\n   " << h.snippet
          << "\n";
      ++i;
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "(no results)\n" : oss.str();
    return r;
  }, false, "Hybrid search (FTS + vector + RRF + optional rerank)",
      R"({"type":"object","properties":{"query":{"type":"string"},"limit":{"type":"integer"},"no_vector":{"type":"boolean"},"source_id":{"type":"string"},"mode":{"type":"string"},"rerank":{"type":"boolean"},"rerank_llm":{"type":"boolean"}},"required":["query"]})");

  register_one(
      "think", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto q = arg(ctx, "question");
    if (q.empty()) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "question required";
      return r;
    }
    search::HybridOpts opts;
    opts.limit = arg_int(ctx, "limit", 8);
    opts.rrf_k = ctx.brain->config().search_rrf_k;
    opts.source_id = arg(ctx, "source_id");
    std::vector<float> emb;
    std::vector<float>* pemb = nullptr;
    auto er = ai::embed_texts(ctx.brain->config(), {q});
    if (er.ok && !er.vectors.empty()) {
      emb = er.vectors[0];
      pemb = &emb;
    }
    auto hits = search::hybrid_search(*ctx.brain, q, pemb, opts);
    std::ostringstream evidence;
    int i = 1;
    for (auto& h : hits) {
      auto page = ctx.brain->get_page(h.slug);
      evidence << "[" << i << "] " << h.slug << " — " << h.title << "\n";
      if (page) {
        auto body = page->body;
        if (body.size() > 1200) body = body.substr(0, 1200) + "…";
        evidence << body << "\n\n";
      }
      ++i;
    }
    std::string system =
        "You are Qbrain, a personal knowledge brain. Answer using ONLY the evidence. "
        "Cite sources as [n]. End with a section '## Gaps' listing what is unknown or stale.";
    std::string user = "Question: " + q + "\n\nEvidence:\n" + evidence.str();
    auto cr = ai::chat_complete(ctx.brain->config(),
                                {{"system", system}, {"user", user}});
    json j;
    j["question"] = q;
    j["hits"] = hits.size();
    if (!cr.ok) {
      j["degraded"] = true;
      j["error"] = cr.error;
      j["evidence"] = evidence.str();
      r.json = j.dump(2);
      r.text = "[gather-only; no LLM] " + cr.error + "\n\n" + evidence.str();
      r.ok = true;  // graceful
      return r;
    }
    j["answer"] = cr.content;
    r.json = j.dump(2);
    r.text = cr.content + "\n";
    // save is a write side-effect: only when not remote, or allow_write
    if (arg(ctx, "save") == "1" && (!ctx.via_mcp || ctx.allow_write)) {
      PageInput in;
      auto h = util::sha256_hex(q);
      if (h.size() > 8) h = h.substr(0, 8);
      in.slug = "synthesis/" + util::slugify(q.substr(0, 40)) + "-" + util::utc_date() + "-" + h;
      in.title = "Synthesis: " + q;
      in.body = cr.content;
      in.type = "synthesis";
      auto page = ctx.brain->put_page(in);
      auto chunks = ingest::chunk_markdown(page.title, page.body);
      ctx.brain->replace_chunks(page.id, chunks);
      auto links = graph::extract_links(page.source_id, page.slug, page.body);
      ctx.brain->replace_extracted_links(page.source_id, page.slug, links);
      r.text += "\n[saved " + page.slug + "]\n";
    } else if (arg(ctx, "save") == "1" && ctx.via_mcp && !ctx.allow_write) {
      r.text += "\n[save ignored: MCP write disabled; use --allow-write]\n";
    }
    return r;
  }, false, "Synthesize an answer with citations and gaps",
      R"({"type":"object","properties":{"question":{"type":"string"},"limit":{"type":"integer"},"save":{"type":"boolean"},"source_id":{"type":"string"}},"required":["question"]})");

  register_one(
      "capture", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto text = arg(ctx, "text");
    if (text.empty()) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "text required";
      return r;
    }
    // N2.5: optional source_id on capture; remote allowlist always enforced
    auto sid = arg(ctx, "source_id", "default");
    for (char& c : sid) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if ((ctx.remote || ctx.via_mcp) && sid != "default") {
      auto allow = ctx.brain->get_config_value("mcp.allowed_sources");
      bool ok = false;
      if (allow) {
        for (auto& p : util::split(*allow, ',')) {
          auto t = util::trim(p);
          for (char& c : t) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
          }
          if (t == sid) ok = true;
        }
      }
      if (!ok) {
        r.ok = false;
        r.text = "remote source_id not allowed (only default, or mcp.allowed_sources)";
        return r;
      }
    }
    if (!ctx.brain->ensure_source(sid)) {
      r.ok = false;
      r.text = "invalid source_id";
      return r;
    }
    auto page = ingest::capture_text(*ctx.brain, text, arg(ctx, "type", "note"), sid);
    ctx.brain->enqueue_embed_page(page.id);
    r.text = page.slug;
    r.json = json({{"slug", page.slug}, {"id", page.id}, {"embed_enqueued", true},
                   {"source_id", sid}})
                 .dump(2);
    return r;
  }, true, "Quick-capture text into inbox/ (CLI always; MCP needs --allow-write). gbrain capture is CLI-only — Qbrain extension.",
      R"({"type":"object","properties":{"text":{"type":"string"},"type":{"type":"string"},"source_id":{"type":"string"}},"required":["text"]})");

  register_one(
      "get_links", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "slug");
    auto depth = arg_int(ctx, "depth", 1);
    auto ns = graph::neighbors(*ctx.brain, slug, depth);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& n : ns) {
      arr.push_back({{"slug", n.slug}, {"link_type", n.link_type}, {"direction", n.direction}, {"depth", n.depth}});
      oss << n.direction << "\t" << n.link_type << "\t" << n.slug << "\td=" << n.depth << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str();
    return r;
  }, false, "Graph neighbors for a slug",
      R"({"type":"object","properties":{"slug":{"type":"string"},"depth":{"type":"integer"}},"required":["slug"]})");

  register_one(
      "get_backlinks", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "slug");
    auto sid = arg(ctx, "source_id", "default");
    auto links = ctx.brain->get_links_to(slug, sid);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& l : links) {
      arr.push_back({{"from", l.from_slug}, {"type", l.link_type}});
      oss << l.from_slug << "\t" << l.link_type << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str();
    return r;
  }, false, "Inbound links to a slug",
      R"({"type":"object","properties":{"slug":{"type":"string"},"source_id":{"type":"string"}},"required":["slug"]})");

  register_one(
      "delete_page", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "slug");
    auto sid = arg(ctx, "source_id", "default");
    if (!ctx.brain->soft_delete(slug, sid)) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "not found or already deleted";
      return r;
    }
    r.text = "deleted " + slug;
    r.json = json({{"slug", slug}, {"status", "soft_deleted"}}).dump(2);
    return r;
  }, true, "Soft-delete a page",
      R"({"type":"object","properties":{"slug":{"type":"string"},"source_id":{"type":"string"}},"required":["slug"]})");

  register_one(
      "restore_page", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "slug");
    if (!ctx.brain->restore_page(slug, arg(ctx, "source_id", "default"))) {
      r.ok = false;
      r.text = "not restored";
      return r;
    }
    r.text = "restored " + slug;
    return r;
  }, true, "Restore soft-deleted page",
      R"({"type":"object","properties":{"slug":{"type":"string"}},"required":["slug"]})");

  register_one(
      "purge_deleted_pages", Scope::Admin, [](OpContext& ctx) {
    OpResult r;
    if (ctx.via_mcp) {
      r.ok = false;
      r.text = "purge is localOnly";
      return r;
    }
    int hours = arg_int(ctx, "older_than_hours", 72);
    int n = ctx.brain->purge_deleted(hours);
    r.text = "purged " + std::to_string(n);
    r.json = json({{"count", n}}).dump(2);
    return r;
  }, true, "Hard-delete soft-deleted pages older than N hours (localOnly)",
      R"({"type":"object","properties":{"older_than_hours":{"type":"integer"}}})");

  register_one(
      "get_versions", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto vers = ctx.brain->list_versions(arg(ctx, "slug"), arg(ctx, "source_id", "default"));
    json arr = json::array();
    for (auto& v : vers) arr.push_back({{"id", v.id}, {"title", v.title}, {"at", v.created_at}});
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List page versions",
      R"({"type":"object","properties":{"slug":{"type":"string"}},"required":["slug"]})");

  register_one(
      "sources_list", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto ids = ctx.brain->list_source_ids();
    r.json = json(ids).dump(2);
    std::ostringstream oss;
    for (auto& id : ids) oss << id << "\n";
    r.text = oss.str();
    return r;
  }, false, "List source ids", R"({"type":"object","properties":{}})");

  register_one(
      "sources_add", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    // N2.5: true localOnly — remote MCP never registers sources (even with allow-write)
    if (ctx.via_mcp) {
      r.ok = false;
      r.text = "sources_add is localOnly (use CLI)";
      return r;
    }
    auto id = arg(ctx, "id");
    if (!ctx.brain->ensure_source(id)) {
      r.ok = false;
      r.text = "invalid source id";
      return r;
    }
    // ensure_source stores canonical lowercase
    std::string canon = id;
    for (char& c : canon) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    r.text = "ok " + canon;
    return r;
  }, true, "Ensure a source id exists (localOnly; remote always denied)",
      R"({"type":"object","properties":{"id":{"type":"string"}},"required":["id"]})");

  register_one(
      "sources_remove", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto id = arg(ctx, "id");
    bool force = arg(ctx, "force") == "1" || arg(ctx, "force") == "true";
    if (!ctx.brain->remove_source(id, force)) {
      r.ok = false;
      r.text = "remove failed (nonempty? use force=true; cannot remove default)";
      return r;
    }
    r.text = "removed " + id;
    return r;
  }, true, "Remove a source (blocks if pages unless force)",
      R"({"type":"object","properties":{"id":{"type":"string"},"force":{"type":"boolean"}},"required":["id"]})");

  register_one(
      "sources_status", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto id = arg(ctx, "id", "default");
    auto s = ctx.brain->source_status(id);
    if (s.id.empty()) {
      r.ok = false;
      r.exit_code = 1;
      r.text = "invalid source_id";
      return r;
    }
    r.json = json({{"id", s.id},
                   {"pages", s.pages},
                   {"links", s.links},
                   {"last_updated", s.last_updated}})
                 .dump(2);
    r.text = r.json;
    return r;
  }, false, "Source page/link counts",
      R"({"type":"object","properties":{"id":{"type":"string"}}})");

  register_one(
      "list_facts", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto entity = arg(ctx, "entity_slug");
    if (entity.empty()) entity = arg(ctx, "entity");
    int limit = std::clamp(arg_int(ctx, "limit", 50), 0, 100);
    auto facts = ctx.brain->list_facts(entity, limit);
    json arr = json::array();
    for (auto& f : facts) arr.push_back(f);
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List active facts for an entity slug",
      R"({"type":"object","properties":{"entity_slug":{"type":"string"},"entity":{"type":"string"},"limit":{"type":"integer"}}})");

  register_one(
      "find_trajectory", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto entity = arg(ctx, "entity_slug");
    if (entity.empty()) entity = arg(ctx, "entity");
    if (entity.empty()) entity = arg(ctx, "query");
    int depth = std::clamp(arg_int(ctx, "depth", 2), 0, 4);
    int limit = std::clamp(arg_int(ctx, "limit", 50), 0, 100);
    auto facts = ctx.brain->list_facts(entity, limit);
    json arr = json::array();
    int i = 0;
    for (auto& f : facts) {
      if (i >= limit) break;
      arr.push_back({{"kind", "fact"}, {"entity_slug", entity}, {"depth", 0}, {"text", f}});
      ++i;
    }
    r.json = arr.dump(2);
    r.text = r.json;
    (void)depth;  // facts are direct steps today; input is still clamped for future traversal.
    return r;
  }, false, "Bounded facts/links trajectory for an entity slug",
      R"({"type":"object","properties":{"entity_slug":{"type":"string"},"entity":{"type":"string"},"query":{"type":"string"},"depth":{"type":"integer"},"limit":{"type":"integer"}}})");

  register_one(
      "list_skills", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    // N9: brain-local skills first, then cwd/project pack (brain ids override packs)
    json arr = json::array();
    namespace fs = std::filesystem;
    std::unordered_map<std::string, bool> seen;
    auto scan = [&](const fs::path& root) {
      if (!fs::exists(root)) return;
      std::error_code ec;
      for (auto& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory()) continue;
        auto name = e.path().filename().string();
        if (name.empty() || name.find("..") != std::string::npos) continue;
        auto skill = e.path() / "SKILL.md";
        if (!fs::exists(skill)) continue;
        if (seen[name]) continue;
        seen[name] = true;
        arr.push_back({{"name", name}});
      }
    };
    if (ctx.brain) scan(util::brain_dir(ctx.brain->brain_id()) / "skills");
    scan(fs::path("skills"));
    scan(fs::path("D:/Projects/Qbrain/skills"));
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List markdown skills", R"({"type":"object","properties":{}})");

  register_one(
      "get_skill", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto name = arg(ctx, "name");
    if (name.empty() || name.find("..") != std::string::npos || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
      r.ok = false;
      r.text = "invalid skill name";
      return r;
    }
    namespace fs = std::filesystem;
    // brain-first override
    std::vector<fs::path> candidates;
    if (ctx.brain) candidates.push_back(util::brain_dir(ctx.brain->brain_id()) / "skills" / name / "SKILL.md");
    candidates.push_back(fs::path("skills") / name / "SKILL.md");
    candidates.push_back(fs::path("D:/Projects/Qbrain/skills") / name / "SKILL.md");
    fs::path p;
    bool found = false;
    for (auto& c : candidates) {
      if (fs::exists(c)) {
        p = c;
        found = true;
        break;
      }
    }
    if (!found) {
      r.ok = false;
      r.text = "not found";
      return r;
    }
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    r.text = ss.str();
    r.json = json({{"name", name}, {"body", r.text}}).dump(2);
    return r;
  }, false, "Read a skill SKILL.md",
      R"({"type":"object","properties":{"name":{"type":"string"}},"required":["name"]})");

  register_one(
      "run_doctor", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto h = ctx.brain->health();
    json j = health_report_json(h, false);
    r.json = j.dump(2);
    r.text = r.json;
    r.ok = h.ok;
    return r;
  }, false, "Doctor report (gbrain name parity)", R"({"type":"object","properties":{}})");

  // --- gbrain name aliases / remaining write ops ---
  register_one(
      "query", Scope::Read, [](OpContext& ctx) {
    // alias search
    if (ctx.args.count("query") == 0 && ctx.args.count("q")) ctx.args["query"] = ctx.args["q"];
    OpContext c2 = ctx;
    auto* op = global_registry().find("search");
    return op ? op->handler(c2) : OpResult{false, 1, "search missing", ""};
  }, false, "Alias of search (gbrain query)",
      R"({"type":"object","properties":{"query":{"type":"string"}},"required":["query"]})");

  register_one(
      "add_link", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    Link l;
    l.from_slug = arg(ctx, "from");
    l.to_slug = arg(ctx, "to");
    l.link_type = arg(ctx, "link_type", "related");
    l.link_source = "manual";
    l.source_id = arg(ctx, "source_id", "default");
    if (l.from_slug.empty() || l.to_slug.empty()) {
      r.ok = false;
      r.text = "from and to required";
      return r;
    }
    ctx.brain->add_link(l);
    r.text = "ok";
    return r;
  }, true, "Add a manual link",
      R"({"type":"object","properties":{"from":{"type":"string"},"to":{"type":"string"},"link_type":{"type":"string"}},"required":["from","to"]})");

  register_one(
      "remove_link", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    ctx.brain->remove_link(arg(ctx, "from"), arg(ctx, "to"), arg(ctx, "source_id", "default"));
    r.text = "ok";
    return r;
  }, true, "Remove a link",
      R"({"type":"object","properties":{"from":{"type":"string"},"to":{"type":"string"}},"required":["from","to"]})");

  register_one(
      "add_tag", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    try {
      ctx.brain->add_tag(arg(ctx, "slug"), arg(ctx, "tag"), arg(ctx, "source_id", "default"));
      r.text = "ok";
    } catch (const std::exception& e) {
      r.ok = false;
      r.text = e.what();
    }
    return r;
  }, true, "Add tag to page",
      R"({"type":"object","properties":{"slug":{"type":"string"},"tag":{"type":"string"}},"required":["slug","tag"]})");

  register_one(
      "remove_tag", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    ctx.brain->remove_tag(arg(ctx, "slug"), arg(ctx, "tag"), arg(ctx, "source_id", "default"));
    r.text = "ok";
    return r;
  }, true, "Remove tag",
      R"({"type":"object","properties":{"slug":{"type":"string"},"tag":{"type":"string"}},"required":["slug","tag"]})");

  register_one(
      "get_tags", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto tags = ctx.brain->get_tags(arg(ctx, "slug"), arg(ctx, "source_id", "default"));
    r.json = json(tags).dump(2);
    r.text = r.json;
    return r;
  }, false, "List tags on a page",
      R"({"type":"object","properties":{"slug":{"type":"string"}},"required":["slug"]})");

  register_one(
      "find_orphans", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto o = ctx.brain->find_orphans(arg_int(ctx, "limit", 100));
    r.json = json(o).dump(2);
    r.text = r.json;
    return r;
  }, false, "Pages with no inbound or outbound links",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "find_anomalies", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_analytics_args(ctx, error)) return error;
    int limit = 100;
    if (!parse_bounded_uint(ctx, "limit", 100, 0, 200, limit, error)) return error;
    auto source = resolve_source(ctx, true, error);
    if (!source) return error;
    auto rows = graph::find_anomalies(*ctx.brain, *source, limit);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& a : rows) {
      arr.push_back({{"source_id", a.source_id}, {"kind", a.kind},
                     {"slug", a.slug}, {"detail", a.detail}});
      oss << a.source_id << "\t" << a.kind << "\t" << a.slug << "\t" << a.detail << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "[]\n" : oss.str();
    return r;
  }, false, "Graph anomalies: missing/deleted link targets, high out-degree",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":100}}})");

  register_one(
      "find_contradictions", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_analytics_args(ctx, error)) return error;
    int limit = 100;
    if (!parse_bounded_uint(ctx, "limit", 100, 0, 200, limit, error)) return error;
    auto source = resolve_source(ctx, true, error);
    if (!source) return error;
    auto rows = graph::find_contradictions(*ctx.brain, *source, limit);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& c : rows) {
      arr.push_back({{"source_id", c.source_id}, {"kind", c.kind},
                     {"slug", c.slug}, {"detail", c.detail}});
      oss << c.source_id << "\t" << c.kind << "\t" << c.slug << "\t" << c.detail << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "[]\n" : oss.str();
    return r;
  }, false, "Heuristic fact contradictions (conflicting predicates / dual objects)",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":100}}})");

  register_one(
      "find_experts", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_analytics_args(ctx, error)) return error;
    int limit = 50;
    if (!parse_bounded_uint(ctx, "limit", 50, 0, 200, limit, error)) return error;
    auto source = resolve_source(ctx, true, error);
    if (!source) return error;
    auto rows = graph::find_experts(*ctx.brain, *source, limit);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& e : rows) {
      arr.push_back({{"source_id", e.source_id}, {"slug", e.slug},
                     {"inbound_count", e.inbound_count}});
      oss << e.source_id << "\t" << e.slug << "\t" << e.inbound_count << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "[]\n" : oss.str();
    return r;
  }, false, "Pages ranked by inbound link count (expertise heuristic)",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":50}}})");

  register_one(
      "extract_facts", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int n = ctx.brain->extract_facts_from_page(arg(ctx, "slug"), arg(ctx, "source_id", "default"));
    r.text = "facts=" + std::to_string(n);
    r.json = json({{"count", n}}).dump(2);
    return r;
  }, true, "Heuristic fact extraction from page links/title",
      R"({"type":"object","properties":{"slug":{"type":"string"}},"required":["slug"]})");

  register_one(
      "list_brains", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    (void)ctx;
    auto ids = Brain::list_brains();
    r.json = json(ids).dump(2);
    r.text = r.json;
    return r;
  }, false, "List brain ids under %LOCALAPPDATA%\\Qbrain\\brains",
      R"({"type":"object","properties":{}})");

  register_one(
      "whoami", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    r.json = json({{"remote", ctx.remote},
                   {"allow_write", ctx.allow_write},
                   {"brain", ctx.brain ? ctx.brain->brain_id() : ""},
                   {"transport", ctx.via_mcp ? "mcp" : "cli"}})
                 .dump(2);
    r.text = r.json;
    return r;
  }, false, "Caller context", R"({"type":"object","properties":{}})");

  register_one(
      "get_chunks", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto page = ctx.brain->get_page(arg(ctx, "slug"), arg(ctx, "source_id", "default"));
    if (!page) {
      r.ok = false;
      r.text = "not found";
      return r;
    }
    auto chunks = ctx.brain->get_chunks(page->id);
    json arr = json::array();
    for (auto& c : chunks) {
      arr.push_back({{"index", c.chunk_index},
                     {"text", c.text.substr(0, 500)},
                     {"embedded", !c.embedding.empty()},
                     {"dim", c.dim}});
    }
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List chunks for a page",
      R"({"type":"object","properties":{"slug":{"type":"string"}},"required":["slug"]})");

  register_one(
      "revert_version", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int64_t vid = 0;
    try {
      vid = std::stoll(arg(ctx, "version_id"));
    } catch (...) {
      r.ok = false;
      r.text = "version_id required";
      return r;
    }
    if (!ctx.brain->revert_version(arg(ctx, "slug"), vid, arg(ctx, "source_id", "default"))) {
      r.ok = false;
      r.text = "revert failed";
      return r;
    }
    r.text = "reverted";
    return r;
  }, true, "Revert page to a version id",
      R"({"type":"object","properties":{"slug":{"type":"string"},"version_id":{"type":"integer"}},"required":["slug","version_id"]})");

  // N12 minions / jobs
  register_one(
      "submit_job", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto type = arg(ctx, "type");
    if (type.empty()) type = arg(ctx, "name");
    if (type.empty()) {
      r.ok = false;
      r.text = "type required";
      return r;
    }
    auto payload = arg(ctx, "payload_json", "{}");
    auto queue = arg(ctx, "queue", "default");
    int pri = arg_int(ctx, "priority", 100);
    auto id = jobs::submit_job(*ctx.brain, type, payload, queue, pri);
    r.json = json({{"id", id}, {"type", type}, {"status", "waiting"}}).dump(2);
    r.text = "job " + std::to_string(id);
    return r;
  }, true, "Submit a minion job (MCP requires --allow-write)",
      R"({"type":"object","properties":{"type":{"type":"string"},"name":{"type":"string"},"payload_json":{"type":"string"},"queue":{"type":"string"},"priority":{"type":"integer"}},"required":["type"]})");

  register_one(
      "list_jobs", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto status = arg(ctx, "status");
    int limit = arg_int(ctx, "limit", 50);
    auto list = jobs::list_jobs(*ctx.brain, status, limit);
    json arr = json::array();
    for (auto& j : list) {
      arr.push_back({{"id", j.id},
                     {"type", j.type},
                     {"status", j.status},
                     {"priority", j.priority},
                     {"attempts", j.attempts},
                     {"queue", j.queue}});
    }
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List jobs",
      R"({"type":"object","properties":{"status":{"type":"string"},"limit":{"type":"integer"}}})");

  register_one(
      "get_job", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    int64_t id = 0;
    try {
      id = std::stoll(arg(ctx, "id"));
    } catch (...) {
      r.ok = false;
      r.text = "id required";
      return r;
    }
    auto j = jobs::get_job(*ctx.brain, id);
    if (!j) {
      r.ok = false;
      r.text = "not found";
      return r;
    }
    r.json = json({{"id", j->id},
                   {"type", j->type},
                   {"status", j->status},
                   {"payload_json", j->payload_json},
                   {"result_json", j->result_json},
                   {"error_text", j->error_text},
                   {"attempts", j->attempts},
                   {"priority", j->priority}})
                 .dump(2);
    r.text = r.json;
    return r;
  }, false, "Get job by id",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "cancel_job", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int64_t id = 0;
    try {
      id = std::stoll(arg(ctx, "id"));
    } catch (...) {
      r.ok = false;
      r.text = "id required";
      return r;
    }
    if (!jobs::cancel_job(*ctx.brain, id)) {
      r.ok = false;
      r.text = "cancel failed";
      return r;
    }
    r.text = "cancelled";
    return r;
  }, true, "Cancel a waiting/active job (MCP requires --allow-write)",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "run_dream", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    cycle::DreamOpts opts;
    opts.dry_run = arg(ctx, "apply") != "1" && arg(ctx, "apply") != "true";
    opts.phase = arg(ctx, "phase");
    opts.page_limit = arg_int(ctx, "limit", 50);
    opts.retention_hours = arg(ctx, "retention_hours");
    auto report = cycle::run_dream(*ctx.brain, opts);
    r.json = cycle::report_to_json(report);
    r.text = cycle::report_to_text(report);
    if (report.status == "failed") {
      r.ok = false;
      r.exit_code = 1;
    }
    return r;
  }, true, "Run multi-phase dream cycle",
      R"({"type":"object","properties":{"apply":{"type":"boolean"},"phase":{"type":"string","enum":["orphans","extract_facts","consolidate","embed","purge"]},"limit":{"type":"integer"},"retention_hours":{"type":"integer","description":"Purge retention; default 72, clamped to 1..8760"}}})");

  // N13
  register_one(
      "sync_brain", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto path = arg(ctx, "path");
    if (path.empty()) path = arg(ctx, "dir");
    if (path.empty()) {
      r.ok = false;
      r.text = "path required";
      return r;
    }
    auto source = arg(ctx, "source_id", "default");
    auto once = service::live_sync_once(*ctx.brain, path, source);
    r.json = json({{"scanned", once.scanned},
                   {"imported_pages", once.imported_pages},
                   {"skipped", once.skipped},
                   {"errors", once.errors}})
                 .dump(2);
    r.text = r.json;
    if (once.errors && !once.imported_pages) {
      r.ok = false;
      r.exit_code = 1;
    }
    return r;
  }, true, "Live-sync a notes directory (mtime state)",
      R"({"type":"object","properties":{"path":{"type":"string"},"dir":{"type":"string"},"source_id":{"type":"string"}},"required":["path"]})");

  register_one(
      "traverse_graph", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "slug");
    if (slug.empty()) {
      r.ok = false;
      r.text = "slug required";
      return r;
    }
    int depth = std::clamp(arg_int(ctx, "depth", 1), 0, 100);
    auto ns = graph::neighbors(*ctx.brain, slug, depth, arg(ctx, "source_id", "default"));
    json arr = json::array();
    std::ostringstream oss;
    for (auto& n : ns) {
      arr.push_back({{"slug", n.slug},
                     {"link_type", n.link_type},
                     {"direction", n.direction},
                     {"depth", n.depth}});
      oss << n.direction << " " << n.slug << " (" << n.link_type << ") d=" << n.depth << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "(no neighbors)\n" : oss.str();
    return r;
  }, false, "BFS graph neighbors",
      R"({"type":"object","properties":{"slug":{"type":"string"},"depth":{"type":"integer"}},"required":["slug"]})");

  register_one(
      "retry_job", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int64_t id = 0;
    try {
      id = std::stoll(arg(ctx, "id"));
    } catch (...) {
      r.ok = false;
      r.text = "id required";
      return r;
    }
    if (!jobs::retry_job(*ctx.brain, id)) {
      r.ok = false;
      r.text = "retry failed";
      return r;
    }
    r.text = "retried " + std::to_string(id);
    return r;
  }, true, "Requeue failed/cancelled job to waiting",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "pause_job", Scope::Write, [](OpContext& ctx) {
    OpResult r, error;
    int64_t id = 0;
    if (!parse_positive_i64(ctx, "id", id, error)) return error;
    try {
      if (!jobs::pause_job(*ctx.brain, id)) {
        return argument_error("invalid_state", "id",
                              "job is missing or cannot transition to paused");
      }
      auto j = jobs::get_job(*ctx.brain, id);
      r.json = json({{"id", id}, {"status", j ? j->status : "paused"}}).dump(2);
      r.text = "paused " + std::to_string(id);
      return r;
    } catch (...) {
      return argument_error("database_error", "database", "job pause unavailable");
    }
  }, true, "Pause waiting/active job",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "resume_job", Scope::Write, [](OpContext& ctx) {
    OpResult r, error;
    int64_t id = 0;
    if (!parse_positive_i64(ctx, "id", id, error)) return error;
    try {
      if (!jobs::resume_job(*ctx.brain, id)) {
        return argument_error("invalid_state", "id",
                              "job is missing or cannot transition to waiting");
      }
      r.json = json({{"id", id}, {"status", "waiting"}}).dump(2);
      r.text = "resumed " + std::to_string(id);
      return r;
    } catch (...) {
      return argument_error("database_error", "database", "job resume unavailable");
    }
  }, true, "Resume paused job to waiting",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "get_job_progress", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    int64_t id = 0;
    if (!parse_positive_i64(ctx, "id", id, error)) return error;
    try {
      auto p = jobs::get_job_progress(*ctx.brain, id);
      if (!p) {
        return argument_error("not_found", "id", "job not found");
      }
      r.json = json({{"id", p->id},
                     {"type", p->type},
                     {"status", p->status},
                     {"attempts", p->attempts},
                     {"lock_until", p->lock_until},
                     {"error_text", p->error_text}})
                   .dump(2);
      r.text = r.json;
      return r;
    } catch (...) {
      return argument_error("database_error", "database", "job progress unavailable");
    }
  }, false, "Job progress (status/attempts/lock/error)",
      R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})");

  register_one(
      "get_status_snapshot", Scope::Read, [](OpContext& ctx) {
    try {
      OpResult r;
      auto s = ctx.brain->status_snapshot();
      json j = {{"schema_version", s.schema_version},
                {"pages", s.pages},
                {"chunks", s.chunks},
                {"links", s.links},
                {"embedded_chunks", s.embedded_chunks},
                {"jobs",
                 {{"waiting", s.jobs_waiting},
                  {"active", s.jobs_active},
                  {"failed", s.jobs_failed},
                  {"paused", s.jobs_paused}}}};
      r.json = j.dump(2);
      r.text = r.json;
      return r;
    } catch (...) {
      return argument_error("database_error", "database", "status snapshot unavailable");
    }
  }, false, "Pages/chunks/links/jobs counts + schema version",
      R"({"type":"object","properties":{}})");

  register_one(
      "doctor_remediate", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    Brain::RemediateReport rep;
    try {
      rep = ctx.brain->remediate();
    } catch (...) {
      return argument_error("remediation_failed", "database",
                            "remediation rolled back");
    }
    json j = {{"default_source", rep.default_source},
              {"reclaimed", rep.reclaimed},
              {"embed_jobs_enqueued", rep.embed_jobs_enqueued},
              {"api_key_present", rep.api_key_present},
              {"notes", rep.notes}};
    r.json = j.dump(2);
    std::ostringstream oss;
    oss << "remediate: source=" << (rep.default_source ? "ok" : "fail")
        << " reclaimed=" << rep.reclaimed
        << " embed_enqueued=" << rep.embed_jobs_enqueued << "\n";
    for (auto& n : rep.notes) oss << "  - " << n << "\n";
    r.text = oss.str();
    r.ok = rep.default_source;
    return r;
  }, true, "Doctor remediate: source, reclaim stalled, re-enqueue embeds",
      R"({"type":"object","properties":{}})");

  register_one(
      "forget_fact", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto slug = arg(ctx, "entity_slug");
    if (slug.empty()) slug = arg(ctx, "slug");
    if (slug.empty()) {
      r.ok = false;
      r.text = "entity_slug required";
      return r;
    }
    int n = ctx.brain->forget_fact(slug, arg(ctx, "predicate"));
    r.json = json({{"deactivated", n}}).dump(2);
    r.text = "deactivated " + std::to_string(n);
    return r;
  }, true, "Soft-deactivate facts for entity",
      R"({"type":"object","properties":{"entity_slug":{"type":"string"},"slug":{"type":"string"},"predicate":{"type":"string"}}})");

  register_one(
      "resolve_slugs", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    // comma-separated slugs → existing pages
    auto raw = arg(ctx, "slugs");
    if (raw.empty()) raw = arg(ctx, "slug");
    auto parts = util::split(raw, ',');
    json arr = json::array();
    std::ostringstream oss;
    for (auto p : parts) {
      p = util::trim(p);
      if (p.empty()) continue;
      auto page = ctx.brain->get_page(p, arg(ctx, "source_id", "default"));
      json item = {{"slug", p}, {"exists", page.has_value()}};
      if (page) {
        item["title"] = page->title;
        item["type"] = page->type;
      }
      arr.push_back(item);
      oss << p << "\t" << (page ? "ok" : "missing") << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str();
    return r;
  }, false, "Resolve slug existence",
      R"({"type":"object","properties":{"slugs":{"type":"string"},"slug":{"type":"string"}}})");

  register_one(
      "recall", Scope::Read, [](OpContext& ctx) {
    // Alias of search with conservative mode default (gbrain recall-ish)
    OpContext c2 = ctx;
    if (c2.args.find("mode") == c2.args.end()) c2.args["mode"] = "conservative";
    if (c2.args.find("query") == c2.args.end() && c2.args.count("q"))
      c2.args["query"] = c2.args["q"];
    auto* op = global_registry().find("search");
    return op ? op->handler(c2) : OpResult{false, 1, "search missing", ""};
  }, false, "Recall (conservative search alias)",
      R"({"type":"object","properties":{"query":{"type":"string"},"q":{"type":"string"},"limit":{"type":"integer"}},"required":["query"]})");

  // N16 code-intel (regex/heuristic, no tree-sitter)
  auto hits_to_result = [](const std::vector<codeintel::Hit>& hits) {
    OpResult r;
    json arr = json::array();
    std::ostringstream oss;
    for (auto& h : hits) {
      arr.push_back({{"source_id", h.source_id},
                     {"slug", h.slug},
                     {"line", h.line},
                     {"snippet", h.snippet},
                     {"kind", h.kind}});
      oss << h.source_id << "\t" << h.slug << ":" << h.line << " [" << h.kind
          << "] " << h.snippet << "\n";
    }
    r.json = arr.dump(2);
    r.text = oss.str().empty() ? "(no matches)\n" : oss.str();
    return r;
  };

  auto parse_code_request = [](OpContext& ctx, std::string& symbol, std::string& source_id,
                               int& limit, int& page_limit, OpResult& error) {
    auto symbol_it = ctx.args.find("symbol");
    auto name_it = ctx.args.find("name");
    const bool has_symbol = symbol_it != ctx.args.end() && !symbol_it->second.empty();
    const bool has_name = name_it != ctx.args.end() && !name_it->second.empty();
    if (has_symbol && has_name && symbol_it->second != name_it->second) {
      error = argument_error("invalid_argument", "symbol",
                             "symbol and name must identify the same value");
      return false;
    }
    symbol = has_symbol ? symbol_it->second : (has_name ? name_it->second : std::string{});
    if (!codeintel::is_valid_symbol(symbol)) {
      error = argument_error("invalid_argument", "symbol",
                             "symbol must be a qualified ASCII identifier");
      return false;
    }
    auto source = resolve_source(ctx, true, error);
    if (!source) return false;
    source_id = *source;
    if (!parse_bounded_uint(ctx, "limit", 50, 1, 200, limit, error) ||
        !parse_bounded_uint(ctx, "page_limit", 500, 1, 2000, page_limit, error))
      return false;
    return true;
  };

  register_one(
      "code_def", Scope::Read, [hits_to_result, parse_code_request](OpContext& ctx) {
    std::string symbol, source;
    int limit = 50, page_limit = 500;
    OpResult error;
    if (!parse_code_request(ctx, symbol, source, limit, page_limit, error)) return error;
    auto hits = codeintel::find_defs_in_source(*ctx.brain, source, symbol, limit, page_limit);
    return hits_to_result(hits);
  }, false, "Find C++/TS-like symbol definitions in page bodies",
      R"({"type":"object","additionalProperties":false,"properties":{"symbol":{"type":"string","maxLength":256},"name":{"type":"string","maxLength":256},"source_id":{"type":"string","default":"default"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":50},"page_limit":{"type":"integer","minimum":0,"maximum":2000,"default":500}},"anyOf":[{"required":["symbol"]},{"required":["name"]}]})");

  register_one(
      "code_refs", Scope::Read, [hits_to_result, parse_code_request](OpContext& ctx) {
    std::string symbol, source;
    int limit = 50, page_limit = 500;
    OpResult error;
    if (!parse_code_request(ctx, symbol, source, limit, page_limit, error)) return error;
    auto hits = codeintel::find_refs_in_source(*ctx.brain, source, symbol, limit, page_limit);
    return hits_to_result(hits);
  }, false, "Find word-boundary symbol references in page bodies",
      R"({"type":"object","additionalProperties":false,"properties":{"symbol":{"type":"string","maxLength":256},"name":{"type":"string","maxLength":256},"source_id":{"type":"string","default":"default"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":50},"page_limit":{"type":"integer","minimum":0,"maximum":2000,"default":500}},"anyOf":[{"required":["symbol"]},{"required":["name"]}]})");

  register_one(
      "code_callers", Scope::Read, [hits_to_result, parse_code_request](OpContext& ctx) {
    std::string symbol, source;
    int limit = 50, page_limit = 500;
    OpResult error;
    if (!parse_code_request(ctx, symbol, source, limit, page_limit, error)) return error;
    auto hits = codeintel::find_callers_in_source(*ctx.brain, source, symbol, limit, page_limit);
    return hits_to_result(hits);
  }, false, "Find call-ish symbol( references in page bodies",
      R"({"type":"object","additionalProperties":false,"properties":{"symbol":{"type":"string","maxLength":256},"name":{"type":"string","maxLength":256},"source_id":{"type":"string","default":"default"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":50},"page_limit":{"type":"integer","minimum":0,"maximum":2000,"default":500}},"anyOf":[{"required":["symbol"]},{"required":["name"]}]})");

  // N15: link sources, ingest log, chronicle, timeline
  register_one(
      "list_link_sources", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    auto source = resolve_source(ctx, true, error);
    if (!source) return error;
    auto rows = ctx.brain->list_link_sources(*source);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& row : rows) {
      arr.push_back({{"source_id", row.source_id}, {"link_source", row.link_source},
                     {"count", row.count}});
      oss << row.source_id << "\t" << row.link_source << "\t" << row.count << "\n";
    }
    r.json = json({{"source_id", *source}, {"link_sources", arr}}).dump(2);
    r.text = oss.str();
    return r;
  }, false, "Distinct link_source values with counts",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"}}})");

  register_one(
      "log_ingest", Scope::Write, [](OpContext& ctx) {
    OpResult r, error;
    auto source = resolve_source(ctx, false, error);
    if (!source) return error;
    auto path = arg(ctx, "path");
    auto et = arg(ctx, "event_type", "import");
    auto detail = arg(ctx, "detail_json", "{}");
    int keep = 100;
    if (!parse_bounded_uint(ctx, "keep_last", 100, 1, 1000, keep, error)) return error;
    if (et.size() > 64 || path.size() > 4096 || detail.size() > 65536)
      return argument_error("invalid_argument", "payload", "ingest payload exceeds size limit");
    try {
      (void)json::parse(detail);
      auto id = ctx.brain->log_ingest(et, path, detail, keep, *source);
      r.json = json({{"id", id}, {"source_id", *source}}).dump(2);
      r.text = "logged " + std::to_string(id);
    } catch (const std::invalid_argument&) {
      return argument_error("invalid_argument", "detail_json", "valid bounded JSON required");
    } catch (...) {
      return argument_error("database_error", "database", "ingest write failed");
    }
    return r;
  }, true, "Append source-attributed ingest log event (keeps last N per source)",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"path":{"type":"string","maxLength":4096},"event_type":{"type":"string","maxLength":64,"default":"import"},"detail_json":{"type":"string","maxLength":65536,"default":"{}"},"keep_last":{"type":"integer","minimum":0,"maximum":1000,"default":100}}})");

  register_one(
      "get_ingest_log", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    auto source = resolve_source(ctx, true, error);
    if (!source) return error;
    int limit = 20;
    if (!parse_bounded_uint(ctx, "limit", 20, 1, 50, limit, error)) return error;
    auto rows = ctx.brain->get_ingest_log(limit, *source);
    json arr = json::array();
    for (auto& e : rows) {
      arr.push_back({{"id", e.id},
                     {"source_id", e.source_id},
                     {"event_type", e.event_type},
                     {"path", e.path},
                     {"detail_json", e.detail_json},
                     {"created_at", e.created_at}});
    }
    r.json = json({{"source_id", *source}, {"events", arr}}).dump(2);
    r.text = r.json;
    return r;
  }, false, "Recent ingest log events",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"limit":{"type":"integer","minimum":0,"maximum":50,"default":20}}})");

  register_one(
      "chronicle_day", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    auto source = resolve_source(ctx, true, error);
    if (!source) return error;
    auto day = arg(ctx, "day");
    if (!valid_utc_day(day))
      return argument_error("invalid_argument", "day", "real UTC date YYYY-MM-DD required");
    int limit = 100;
    if (!parse_bounded_uint(ctx, "limit", 100, 1, 200, limit, error)) return error;
    auto hits = ctx.brain->chronicle_day(day, limit, *source);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& h : hits) {
      arr.push_back({{"id", h.id}, {"source_id", h.source_id}, {"slug", h.slug},
                     {"title", h.title},
                     {"updated_at", h.updated_at},
                     {"created_at", h.created_at},
                     {"effective_at", h.effective_at},
                     {"type", h.type}});
      oss << h.slug << "\t" << h.title << "\t" << h.updated_at << "\n";
    }
    r.json = json({{"source_id", *source}, {"day", day}, {"pages", arr}}).dump(2);
    r.text = oss.str();
    return r;
  }, false, "Pages created/updated on a UTC day",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"day":{"type":"string"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":100}},"required":["day"]})");

  register_one(
      "chronicle_since", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    auto source = resolve_source(ctx, true, error);
    if (!source) return error;
    auto since = arg(ctx, "since");
    if (!valid_utc_since(since))
      return argument_error("invalid_argument", "since", "strict UTC date or timestamp required");
    int limit = 100;
    if (!parse_bounded_uint(ctx, "limit", 100, 1, 200, limit, error)) return error;
    auto hits = ctx.brain->chronicle_since(since, limit, *source);
    json arr = json::array();
    std::ostringstream oss;
    for (auto& h : hits) {
      arr.push_back({{"id", h.id}, {"source_id", h.source_id}, {"slug", h.slug},
                     {"title", h.title},
                     {"updated_at", h.updated_at},
                     {"created_at", h.created_at},
                     {"effective_at", h.effective_at},
                     {"type", h.type}});
      oss << h.slug << "\t" << h.title << "\t" << h.updated_at << "\n";
    }
    std::string normalized = since.size() == 10 ? since + "T00:00:00Z" : since;
    if (normalized.size() == 20 && normalized[10] == ' ') normalized[10] = 'T';
    r.json = json({{"source_id", *source}, {"since", normalized}, {"pages", arr}}).dump(2);
    r.text = oss.str();
    return r;
  }, false, "Pages created/updated since ISO timestamp",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"since":{"type":"string"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":100}},"required":["since"]})");

  register_one(
      "add_timeline_entry", Scope::Write, [](OpContext& ctx) {
    OpResult r, error;
    auto source = resolve_source(ctx, false, error);
    if (!source) return error;
    PageInput in;
    in.slug = arg(ctx, "slug");
    if (in.slug.empty()) {
      auto h = util::sha256_hex(arg(ctx, "title") + "\n" + arg(ctx, "body") + util::utc_now());
      if (h.size() > 8) h = h.substr(0, 8);
      auto base = "timeline/" + util::utc_date() + "-" + h;
      in.slug = base;
      int suffix = 2;
      while (ctx.brain->get_page(in.slug, *source, true))
        in.slug = base + "-" + std::to_string(suffix++);
    }
    in.title = arg(ctx, "title");
    in.body = arg(ctx, "body");
    if (in.title.empty() && !in.body.empty()) {
      auto nl = in.body.find('\n');
      in.title = nl == std::string::npos ? in.body : in.body.substr(0, nl);
      if (in.title.size() > 80) in.title = in.title.substr(0, 80);
    }
    if (in.body.empty() && in.title.empty()) {
      return argument_error("invalid_argument", "body", "title or body required");
    }
    in.type = "timeline";
    in.source_id = *source;
    in.source_kind = ctx.via_mcp ? "mcp:add_timeline_entry" : "timeline";
    in.ingested_via = ctx.via_mcp ? "mcp" : "cli";
    auto page = ctx.brain->put_page(in);
    auto chunks = ingest::chunk_markdown(page.title, page.body);
    ctx.brain->replace_chunks(page.id, chunks);
    if (!ctx.via_mcp) {
      auto links = graph::extract_links(page.source_id, page.slug, page.body);
      ctx.brain->replace_extracted_links(page.source_id, page.slug, links);
    }
    ctx.brain->enqueue_embed_page(page.id);
    r.json = json({{"source_id", page.source_id}, {"slug", page.slug},
                   {"id", page.id}, {"type", page.type}}).dump(2);
    r.text = "timeline " + page.slug;
    return r;
  }, true, "Create a type=timeline page (thin put_page subset)",
      R"({"type":"object","additionalProperties":false,"properties":{"title":{"type":"string"},"body":{"type":"string"},"slug":{"type":"string"},"source_id":{"type":"string","default":"default"}},"anyOf":[{"required":["title"]},{"required":["body"]}]})");

  // N17 job replay + messages
  register_one(
      "replay_job", Scope::Write, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"job_id", "id"}, error)) return error;
    int64_t id = 0;
    if (!parse_job_id_alias(ctx, id, error)) return error;
    try {
      const auto replay = jobs::replay_job_checked(*ctx.brain, id);
      if (replay.status == jobs::JobOperationStatus::not_found)
        return argument_error("not_found", "job_id", "job not found");
      if (replay.status == jobs::JobOperationStatus::invalid_state)
        return argument_error("invalid_state", "job_id", "job is not replayable");
      if (replay.status != jobs::JobOperationStatus::success)
        return argument_error("invalid_argument", job_input_field_name(replay.field),
                              "invalid replay argument");
      r.json = json({{"original_id", replay.original_id},
                     {"new_id", replay.new_id},
                     {"status", "waiting"}})
                   .dump(2);
      r.text = "replayed " + std::to_string(replay.original_id) + " -> " +
               std::to_string(replay.new_id);
      return r;
    } catch (const std::exception& e) {
      if (is_database_busy_error(e))
        return argument_error("database_busy", "database", "job replay temporarily busy");
      return argument_error("database_error", "database", "job replay failed");
    }
  }, true, "Replay a failed/completed job as a fresh waiting job",
      R"({"type":"object","additionalProperties":false,"properties":{"job_id":{"type":"integer","minimum":1,"maximum":9223372036854775807},"id":{"type":"integer","minimum":1,"maximum":9223372036854775807}},"anyOf":[{"required":["job_id"]},{"required":["id"]}]})");

  register_one(
      "send_job_message", Scope::Write, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"job_id", "id", "sender", "payload_json"}, error))
      return error;
    int64_t id = 0;
    if (!parse_job_id_alias(ctx, id, error)) return error;
    const auto sender_it = ctx.args.find("sender");
    const auto payload_it = ctx.args.find("payload_json");
    const std::string sender = sender_it == ctx.args.end() ? "system" : sender_it->second;
    const std::string payload = payload_it == ctx.args.end() ? "{}" : payload_it->second;
    try {
      const auto sent = jobs::send_job_message_checked(*ctx.brain, id, sender, payload);
      if (sent.status == jobs::JobOperationStatus::not_found)
        return argument_error("not_found", "job_id", "job not found");
      if (sent.status != jobs::JobOperationStatus::success)
        return argument_error("invalid_argument", job_input_field_name(sent.field),
                              sent.field == jobs::JobInputField::sender
                                  ? "valid bounded sender required"
                                  : "valid bounded JSON payload required");
      r.json = json({{"message_id", sent.message_id}, {"job_id", id}}).dump(2);
      r.text = "message " + std::to_string(sent.message_id);
      return r;
    } catch (const std::exception& e) {
      if (is_database_busy_error(e))
        return argument_error("database_busy", "database", "message write temporarily busy");
      return argument_error("database_error", "database", "message write failed");
    }
  }, true, "Append a validated JSON message to a job inbox",
      R"({"type":"object","additionalProperties":false,"properties":{"job_id":{"type":"integer","minimum":1,"maximum":9223372036854775807},"id":{"type":"integer","minimum":1,"maximum":9223372036854775807},"sender":{"type":"string","minLength":1,"maxLength":128,"default":"system"},"payload_json":{"type":"string","minLength":1,"maxLength":65536,"default":"{}"}},"anyOf":[{"required":["job_id"]},{"required":["id"]}]})");

  register_one(
      "list_job_messages", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"job_id", "id", "limit"}, error)) return error;
    int64_t id = 0;
    if (!parse_job_id_alias(ctx, id, error)) return error;
    int limit = jobs::kJobMessageDefaultLimit;
    if (!parse_bounded_uint(ctx, "limit", jobs::kJobMessageDefaultLimit, 1,
                            jobs::kJobMessageMaxLimit, limit, error))
      return error;
    try {
      auto listed = jobs::list_job_messages_checked(*ctx.brain, id, limit);
      if (listed.status == jobs::JobOperationStatus::not_found)
        return argument_error("not_found", "job_id", "job not found");
      if (listed.status != jobs::JobOperationStatus::success)
        return argument_error("invalid_argument", job_input_field_name(listed.field),
                              "invalid message-list argument");
      json messages = json::array();
      for (const auto& message : listed.messages) {
        messages.push_back({{"id", message.id},
                            {"job_id", message.job_id},
                            {"sender", message.sender},
                            {"payload_json", message.payload_json},
                            {"created_at", message.created_at}});
      }
      r.json = messages.dump(2);
      r.text = r.json;
      return r;
    } catch (const std::exception& e) {
      if (is_database_busy_error(e))
        return argument_error("database_busy", "database", "message list temporarily busy");
      return argument_error("database_error", "database", "message list failed");
    }
  }, false, "List a job inbox newest first",
      R"({"type":"object","additionalProperties":false,"properties":{"job_id":{"type":"integer","minimum":1,"maximum":9223372036854775807},"id":{"type":"integer","minimum":1,"maximum":9223372036854775807},"limit":{"type":"integer","minimum":0,"maximum":200,"default":50}},"anyOf":[{"required":["job_id"]},{"required":["id"]}]})");

  // N19 identity / context / timeline / Chronicle reads
  register_one(
      "get_brain_identity", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"source_id"}, error)) return error;
    try {
      auto source = resolve_source(ctx, true, error);
      if (!source) return error;
      const auto snapshot = ctx.brain->source_identity_snapshot(*source);
      json result = {{"brain_id", ctx.brain->brain_id()},
                     {"source_id", snapshot.source_id},
                     {"schema_version", snapshot.schema_version},
                     {"pages", snapshot.pages},
                     {"chunks", snapshot.chunks},
                     {"links", snapshot.links},
                     {"embedded_chunks", snapshot.embedded_chunks}};
      if (!(ctx.remote || ctx.via_mcp)) result["db_path"] = ctx.brain->db_path();
      r.json = result.dump(2);
      r.text = r.json;
      return r;
    } catch (const std::exception&) {
      return argument_error("database_error", "database", "brain identity read failed");
    }
  }, false, "Read source-scoped identity counters for the selected brain",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"}}})");

  register_one(
      "volunteer_context", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"source_id", "query", "q", "limit"}, error))
      return error;
    const auto query_it = ctx.args.find("query");
    const auto alias_it = ctx.args.find("q");
    if (query_it != ctx.args.end() && alias_it != ctx.args.end() &&
        !query_it->second.empty() && !alias_it->second.empty() &&
        query_it->second != alias_it->second) {
      return argument_error("invalid_argument", "query", "query and q must match");
    }
    std::string query;
    if (query_it != ctx.args.end() && !query_it->second.empty())
      query = query_it->second;
    else if (alias_it != ctx.args.end())
      query = alias_it->second;
    if (query.size() > 4096 || !is_valid_utf8(query))
      return argument_error("invalid_argument", "query", "valid bounded UTF-8 query required");
    int limit = 8;
    if (!parse_bounded_uint(ctx, "limit", 8, 1, 50, limit, error)) return error;
    try {
      auto source = resolve_source(ctx, true, error);
      if (!source) return error;
      json rows = json::array();
      if (!query.empty() && !is_ascii_whitespace_only(query)) {
        search::HybridOpts opts;
        opts.limit = limit;
        opts.use_vector = false;
        opts.source_id = *source;
        opts.mode = "conservative";
        opts.rerank = false;
        opts.rerank_llm = false;
        auto hits = search::hybrid_search(*ctx.brain, query, nullptr, opts);
        for (const auto& hit : hits) {
          rows.push_back({{"source_id", *source},
                          {"slug", hit.slug},
                          {"title", bounded_display_utf8(hit.title, 512, true)},
                          {"snippet", bounded_display_utf8(hit.snippet, 512, true)},
                          {"score", hit.score}});
        }
      } else {
        auto pages = ctx.brain->list_pages_for_source(*source, limit);
        for (const auto& page : pages) {
          rows.push_back({{"source_id", *source},
                          {"slug", page.slug},
                          {"title", bounded_display_utf8(page.title, 512, true)},
                          {"type", page.type},
                          {"updated_at", page.updated_at}});
        }
      }
      r.json = rows.dump(2);
      r.text = r.json;
      return r;
    } catch (const std::exception&) {
      return argument_error("database_error", "database", "context read failed");
    }
  }, false, "Read bounded source-scoped conservative search or recent-page context",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"query":{"type":"string","maxLength":4096},"q":{"type":"string","maxLength":4096},"limit":{"type":"integer","minimum":0,"maximum":50,"default":8}}})");

  register_one(
      "get_timeline", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"source_id", "limit"}, error)) return error;
    int limit = 50;
    if (!parse_bounded_uint(ctx, "limit", 50, 1, 200, limit, error)) return error;
    try {
      auto source = resolve_source(ctx, true, error);
      if (!source) return error;
      auto pages = ctx.brain->list_pages_for_source(*source, limit, "timeline");
      json rows = json::array();
      for (const auto& page : pages) {
        const auto effective_at =
            page.updated_at >= page.created_at ? page.updated_at : page.created_at;
        rows.push_back({{"source_id", *source},
                        {"slug", page.slug},
                        {"type", page.type},
                        {"title", bounded_display_utf8(page.title, 512, true)},
                        {"created_at", page.created_at},
                        {"updated_at", page.updated_at},
                        {"effective_at", effective_at}});
      }
      r.json = rows.dump(2);
      r.text = r.json;
      return r;
    } catch (const std::exception&) {
      return argument_error("database_error", "database", "timeline read failed");
    }
  }, false, "Read the bounded source-scoped thin timeline-page subset",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":50}}})");

  register_one(
      "volunteer_chronicle", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"source_id", "since", "limit"}, error)) return error;
    const auto since_it = ctx.args.find("since");
    std::string since = since_it == ctx.args.end() ? util::utc_seven_day_boundary()
                                                   : since_it->second;
    if (since_it != ctx.args.end() && (since.empty() || !valid_utc_since(since)))
      return argument_error("invalid_argument", "since", "valid UTC date or timestamp required");
    int limit = 50;
    if (!parse_bounded_uint(ctx, "limit", 50, 1, 200, limit, error)) return error;
    try {
      auto source = resolve_source(ctx, true, error);
      if (!source) return error;
      auto hits = ctx.brain->chronicle_since(since, limit, *source);
      json rows = json::array();
      for (const auto& hit : hits) {
        rows.push_back({{"source_id", *source},
                        {"slug", hit.slug},
                        {"title", bounded_display_utf8(hit.title, 512, true)},
                        {"created_at", hit.created_at},
                        {"updated_at", hit.updated_at},
                        {"effective_at", hit.effective_at},
                        {"type", hit.type}});
      }
      r.json = rows.dump(2);
      r.text = r.json;
      return r;
    } catch (const std::exception&) {
      return argument_error("database_error", "database", "Chronicle read failed");
    }
  }, false, "Read bounded source-scoped Chronicle page activity",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","default":"default"},"since":{"type":"string"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":50}}})");

  // N20 schema packs
  register_one(
      "list_schema_packs", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {}, error)) return normalize_n20_error(std::move(error));
    try {
      const auto packs = schema::list_packs(*ctx.brain);
      json rows = json::array();
      std::string active_id;
      for (const auto& pack : packs) {
        rows.push_back(
            {{"id", pack.id}, {"origin", pack.origin}, {"active", pack.active}});
        if (pack.active) active_id = pack.id;
      }
      json payload = {{"active_id", active_id}, {"packs", std::move(rows)}};
      r.json = payload.dump();
      r.text = r.json;
      return r;
    } catch (const schema::PackError& pack_error) {
      return n20_pack_error(pack_error);
    } catch (const std::exception&) {
      return n20_error("filesystem_error", "pack", "schema pack storage is unavailable");
    }
  }, false, "List validated schema pack identities",
      R"({"type":"object","additionalProperties":false,"properties":{}})");

  register_one(
      "get_active_schema_pack", Scope::Read, [](OpContext& ctx) {
    try {
      OpResult r, error;
      if (!validate_allowed_args(ctx, {}, error))
        return normalize_n20_error(std::move(error));
      r.json = n20_pack_payload(schema::load_pack(*ctx.brain)).dump();
      r.text = r.json;
      return r;
    } catch (const schema::PackError& pack_error) {
      return n20_pack_error(pack_error);
    } catch (const std::exception&) {
      return n20_error("filesystem_error", "pack", "schema pack storage is unavailable");
    }
  }, false, "Read the active validated schema pack",
      R"({"type":"object","additionalProperties":false,"properties":{}})");

  register_one(
      "reload_schema_pack", Scope::Write, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"id"}, error))
      return normalize_n20_error(std::move(error));
    try {
      const auto reloaded = schema::reload_pack(*ctx.brain, optional_n20_pack_id(ctx));
      r.json = json({{"id", reloaded.id}, {"changed", reloaded.changed}}).dump();
      r.text = r.json;
      return r;
    } catch (const schema::PackError& pack_error) {
      return n20_pack_error(pack_error);
    } catch (const std::exception&) {
      return n20_error("database_error", "database", "schema pack reload failed");
    }
  }, true, "Validate and select an installed schema pack",
      R"({"type":"object","additionalProperties":false,"properties":{"id":{"type":"string","minLength":1,"maxLength":64,"pattern":"^[A-Za-z0-9_-]+$"}}})");

  register_one(
      "schema_stats", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"source_id", "limit"}, error))
      return normalize_n20_error(std::move(error));
    int limit = 100;
    if (!parse_bounded_uint(ctx, "limit", 100, 1, 256, limit, error))
      return normalize_n20_error(std::move(error));
    try {
      auto source = resolve_source(ctx, true, error);
      if (!source) return normalize_n20_error(std::move(error));
      const auto stats = schema::read_schema_stats(*ctx.brain, *source, limit);
      json type_counts = json::array();
      for (const auto& row : stats.type_counts)
        type_counts.push_back({{"type", row.type}, {"count", row.count}});
      json payload = {{"source_id", stats.source_id},
                      {"active_pack_id", stats.active_pack_id},
                      {"schema_version", stats.schema_version},
                      {"total_active_pages", stats.total_active_pages},
                      {"type_counts", std::move(type_counts)},
                      {"truncated", stats.truncated}};
      r.json = payload.dump();
      r.text = r.json;
      return r;
    } catch (const schema::PackError& pack_error) {
      return n20_pack_error(pack_error);
    } catch (const std::exception& exception) {
      if (is_database_busy_error(exception))
        return n20_error("database_busy", "database", "schema statistics database is busy");
      return n20_error("database_error", "database", "schema statistics read failed");
    }
  }, false, "Read bounded source-scoped schema statistics",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","minLength":1,"maxLength":64,"pattern":"^[A-Za-z0-9_-]+$","default":"default"},"limit":{"type":"integer","minimum":0,"maximum":256,"default":100}}})");

  register_one(
      "ontology_get", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"id"}, error))
      return normalize_n20_error(std::move(error));
    try {
      r.json = n20_pack_payload(
                   schema::load_pack(*ctx.brain, optional_n20_pack_id(ctx)))
                   .dump();
      r.text = r.json;
      return r;
    } catch (const schema::PackError& pack_error) {
      return n20_pack_error(pack_error);
    } catch (const std::exception&) {
      return n20_error("filesystem_error", "pack", "schema pack storage is unavailable");
    }
  }, false, "Read a validated schema pack ontology declaration",
      R"({"type":"object","additionalProperties":false,"properties":{"id":{"type":"string","minLength":1,"maxLength":64,"pattern":"^[A-Za-z0-9_-]+$"}}})");

  register_one(
      "ontology_dimensions", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"id"}, error))
      return normalize_n20_error(std::move(error));
    try {
      const auto loaded = schema::load_pack(*ctx.brain, optional_n20_pack_id(ctx));
      r.json = json({{"id", loaded.id}, {"dimensions", loaded.manifest.dimensions}}).dump();
      r.text = r.json;
      return r;
    } catch (const schema::PackError& pack_error) {
      return n20_pack_error(pack_error);
    } catch (const std::exception&) {
      return n20_error("filesystem_error", "pack", "schema pack storage is unavailable");
    }
  }, false, "Read dimensions declared by a validated ontology schema pack",
      R"({"type":"object","additionalProperties":false,"properties":{"id":{"type":"string","minLength":1,"maxLength":64,"pattern":"^[A-Za-z0-9_-]+$"}}})");

  // N21 takes
  register_one(
      "takes_list", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto rows = ctx.brain->takes_list(arg(ctx, "entity_slug"), arg_int(ctx, "limit", 50));
    json arr = json::array();
    for (auto& t : rows)
      arr.push_back({{"id", t.id},
                     {"entity_slug", t.entity_slug},
                     {"kind", t.kind},
                     {"body", t.body},
                     {"score", t.score}});
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List takes",
      R"({"type":"object","properties":{"entity_slug":{"type":"string"},"limit":{"type":"integer"}}})");

  register_one(
      "takes_search", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto q = arg(ctx, "query");
    if (q.empty()) q = arg(ctx, "q");
    auto rows = ctx.brain->takes_search(q, arg_int(ctx, "limit", 50));
    json arr = json::array();
    for (auto& t : rows)
      arr.push_back({{"id", t.id}, {"entity_slug", t.entity_slug}, {"body", t.body}});
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "Search takes by body/slug",
      R"({"type":"object","properties":{"query":{"type":"string"},"limit":{"type":"integer"}}})");

  register_one(
      "takes_scorecard", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto st = ctx.brain->db().prepare(
        "SELECT kind, COUNT(*), COALESCE(AVG(score),0) FROM takes WHERE active=1 GROUP BY kind");
    json arr = json::array();
    while (st.step())
      arr.push_back({{"kind", st.column_text(0)},
                     {"count", st.column_int(1)},
                     {"avg_score", st.column_double(2)}});
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "Takes aggregate scorecard", R"({"type":"object","properties":{}})");

  register_one(
      "takes_calibration", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    int n = ctx.brain->takes_promote_facts(arg_int(ctx, "limit", 50));
    r.json = json({{"promoted_from_facts", n}}).dump(2);
    r.text = r.json;
    return r;
  }, false, "Promote facts into takes (stub calibration)",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "get_calibration_profile", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    r.json = json({{"version", 1},
                   {"note", "stub profile"},
                   {"active_pack", schema::active_pack_id(*ctx.brain)}})
                 .dump(2);
    r.text = r.json;
    return r;
  }, false, "Calibration profile stub", R"({"type":"object","properties":{}})");

  // N22 code intel extensions
  auto parse_n22_symbol = [](OpContext& ctx,
                             std::initializer_list<std::string_view> aliases,
                             std::string_view canonical, std::string& symbol,
                             OpResult& error) {
    const std::string* selected = nullptr;
    for (const auto alias : aliases) {
      const auto it = ctx.args.find(std::string(alias));
      if (it == ctx.args.end()) continue;
      if (it->second.empty()) {
        error = argument_error("invalid_argument", std::string(canonical),
                               "qualified ASCII identifier required");
        return false;
      }
      if (!selected) {
        selected = &it->second;
      } else if (*selected != it->second) {
        error = argument_error("invalid_argument", std::string(canonical),
                               "symbol aliases must match");
        return false;
      }
    }
    if (!selected || !codeintel::is_valid_symbol(*selected)) {
      error = argument_error("invalid_argument", std::string(canonical),
                             "qualified ASCII identifier required");
      return false;
    }
    symbol = *selected;
    return true;
  };

  auto n22_hits_to_result = [](const std::vector<codeintel::Hit>& hits) {
    OpResult r;
    json arr = json::array();
    std::ostringstream text;
    for (const auto& hit : hits) {
      arr.push_back({{"source_id", hit.source_id},
                     {"slug", hit.slug},
                     {"line", hit.line},
                     {"snippet", hit.snippet},
                     {"kind", hit.kind}});
      text << hit.source_id << "\t" << hit.slug << ":" << hit.line << " ["
           << hit.kind << "] " << hit.snippet << "\n";
    }
    r.json = arr.dump(2);
    r.text = text.str().empty() ? "(no matches)\n" : text.str();
    return r;
  };

  auto parse_n22_source_request = [parse_n22_symbol](
      OpContext& ctx, std::initializer_list<std::string_view> allowed,
      std::initializer_list<std::string_view> aliases, std::string_view canonical,
      std::string& symbol, std::string& source, int& limit, int& page_limit,
      OpResult& error) {
    if (!validate_allowed_args(ctx, allowed, error)) return false;
    if (!parse_n22_symbol(ctx, aliases, canonical, symbol, error)) return false;
    if (!parse_bounded_uint(ctx, "limit", 50, 1, 200, limit, error) ||
        !parse_bounded_uint(ctx, "page_limit", 500, 1, 2000, page_limit, error))
      return false;
    auto resolved = resolve_source(ctx, true, error);
    if (!resolved) return false;
    source = *resolved;
    return true;
  };

  register_one(
      "code_callees", Scope::Read, [parse_n22_source_request, n22_hits_to_result](OpContext& ctx) {
    OpResult error;
    std::string symbol, source;
    int limit = 50, page_limit = 500;
    try {
      if (!parse_n22_source_request(
              ctx, {"symbol", "name", "source_id", "limit", "page_limit"},
              {"symbol", "name"}, "symbol", symbol, source, limit, page_limit, error))
        return error;
      return n22_hits_to_result(
          codeintel::find_callees_in_source(*ctx.brain, source, symbol, limit, page_limit));
    } catch (const std::length_error&) {
      return argument_error("resource_limit", "source_id", "source text exceeds scan budget");
    } catch (const std::exception& exception) {
      if (is_database_busy_error(exception))
        return argument_error("database_busy", "database", "code scan database is busy");
      return argument_error("database_error", "database", "code scan failed");
    }
  }, false,
      "Stateless bounded source-text heuristic (16 KiB/page, 8 MiB and 16384 lines/corpus): "
      "one-hop source-scoped bounded brace-body "
      "callee scan with exact lexical identifier matching; no AST, tree-sitter, "
      "or compiler index; no overload/type resolution; no persisted call "
      "edges/cache; not recursive/transitive upstream parity",
      R"({"type":"object","additionalProperties":false,"properties":{"symbol":{"type":"string","minLength":1,"maxLength":256},"name":{"type":"string","minLength":1,"maxLength":256},"source_id":{"type":"string","minLength":1,"maxLength":64,"pattern":"^(?!(?:[Cc][Oo][Nn]|[Pp][Rr][Nn]|[Aa][Uu][Xx]|[Nn][Uu][Ll]|[Cc][Oo][Mm][1-9]|[Ll][Pp][Tt][1-9])$)[A-Za-z0-9_-]+$","default":"default"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":50},"page_limit":{"type":"integer","minimum":0,"maximum":2000,"default":500}},"anyOf":[{"required":["symbol"]},{"required":["name"]}]})");

  register_one(
      "code_flow", Scope::Read,
      [parse_n22_symbol, n22_hits_to_result](OpContext& ctx) {
    OpResult error;
    try {
      if (!validate_allowed_args(ctx, {"entry_point", "symbol", "name", "source_id",
                                       "depth", "limit", "page_limit"}, error))
        return error;
      std::string symbol;
      if (!parse_n22_symbol(ctx, {"entry_point", "symbol", "name"}, "entry_point",
                             symbol, error))
        return error;
      int depth = 2, limit = 50, page_limit = 500;
      if (!parse_bounded_uint(ctx, "depth", 2, 1, 8, depth, error) ||
          !parse_bounded_uint(ctx, "limit", 50, 1, 200, limit, error) ||
          !parse_bounded_uint(ctx, "page_limit", 500, 1, 2000, page_limit, error))
        return error;
      auto source = resolve_source(ctx, true, error);
      if (!source) return error;
      return n22_hits_to_result(codeintel::find_flow_in_source(
          *ctx.brain, *source, symbol, depth, limit, page_limit));
    } catch (const std::length_error&) {
      return argument_error("resource_limit", "source_id", "source text exceeds scan budget");
    } catch (const std::exception& exception) {
      if (is_database_busy_error(exception))
        return argument_error("database_busy", "database", "code flow database is busy");
      return argument_error("database_error", "database", "code flow scan failed");
    }
  }, false,
      "Stateless bounded source-text heuristic (16 KiB/page, 8 MiB and 16384 lines/corpus): "
      "deterministic breadth-first "
      "traversal over source-scoped brace-body callees with exact lexical "
      "identifier matching; no AST, tree-sitter, or compiler index; no "
      "overload/type resolution; no persisted call edges/cache; not "
      "recursive/transitive upstream parity; no terminal/sink classification",
      R"({"type":"object","additionalProperties":false,"properties":{"entry_point":{"type":"string","minLength":1,"maxLength":256},"symbol":{"type":"string","minLength":1,"maxLength":256},"name":{"type":"string","minLength":1,"maxLength":256},"source_id":{"type":"string","minLength":1,"maxLength":64,"pattern":"^(?!(?:[Cc][Oo][Nn]|[Pp][Rr][Nn]|[Aa][Uu][Xx]|[Nn][Uu][Ll]|[Cc][Oo][Mm][1-9]|[Ll][Pp][Tt][1-9])$)[A-Za-z0-9_-]+$","default":"default"},"depth":{"type":"integer","minimum":0,"maximum":8,"default":2},"limit":{"type":"integer","minimum":0,"maximum":200,"default":50},"page_limit":{"type":"integer","minimum":0,"maximum":2000,"default":500}},"anyOf":[{"required":["entry_point"]},{"required":["symbol"]},{"required":["name"]}]})");

  register_one(
      "code_blast", Scope::Read,
      [parse_n22_symbol, n22_hits_to_result](OpContext& ctx) {
    OpResult error;
    try {
      std::string symbol, source;
      int limit = 80, page_limit = 500;
      if (!validate_allowed_args(ctx,
                                 {"symbol", "name", "source_id", "limit", "page_limit"},
                                 error))
        return error;
      if (!parse_n22_symbol(ctx, {"symbol", "name"}, "symbol", symbol, error))
        return error;
      if (!parse_bounded_uint(ctx, "limit", 80, 1, 200, limit, error) ||
          !parse_bounded_uint(ctx, "page_limit", 500, 1, 2000, page_limit, error))
        return error;
      auto resolved = resolve_source(ctx, true, error);
      if (!resolved) return error;
      source = *resolved;
      return n22_hits_to_result(
          codeintel::find_blast_in_source(*ctx.brain, source, symbol, limit, page_limit));
    } catch (const std::length_error&) {
      return argument_error("resource_limit", "source_id", "source text exceeds scan budget");
    } catch (const std::exception& exception) {
      if (is_database_busy_error(exception))
        return argument_error("database_busy", "database", "code blast database is busy");
      return argument_error("database_error", "database", "code blast scan failed");
    }
  }, false,
      "Stateless bounded source-text heuristic (16 KiB/page, 8 MiB and 16384 lines/corpus): "
      "bounded one-hop source-scoped "
      "def/ref/caller/callee heuristic subset using brace-body callees and exact "
      "lexical identifier matching; no AST, tree-sitter, or compiler index; no "
      "overload/type resolution; no persisted call edges/cache; not "
      "recursive/transitive upstream parity",
      R"({"type":"object","additionalProperties":false,"properties":{"symbol":{"type":"string","minLength":1,"maxLength":256},"name":{"type":"string","minLength":1,"maxLength":256},"source_id":{"type":"string","minLength":1,"maxLength":64,"pattern":"^(?!(?:[Cc][Oo][Nn]|[Pp][Rr][Nn]|[Aa][Uu][Xx]|[Nn][Uu][Ll]|[Cc][Oo][Mm][1-9]|[Ll][Pp][Tt][1-9])$)[A-Za-z0-9_-]+$","default":"default"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":80},"page_limit":{"type":"integer","minimum":0,"maximum":2000,"default":500}},"anyOf":[{"required":["symbol"]},{"required":["name"]}]})");

  register_one(
      "code_traversal_cache_clear", Scope::Admin, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {}, error)) return error;
    codeintel::clear_traversal_cache();
    const json payload = {{"cleared", 0}, {"stateless", true}};
    r.json = payload.dump();
    r.text = r.json;
    return r;
  }, true,
      "Guarded stateless compatibility no-op; clears zero rows; no persisted "
      "traversal cache, no cache table, and no schema migration",
      R"({"type":"object","additionalProperties":false,"properties":{}})");

  // N23 source-scoped Chronicle page-activity/tagging subset
  register_one(
      "chronicle_on_this_day", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"source_id", "date", "mmdd", "limit"}, error))
      return error;

    const auto date_it = ctx.args.find("date");
    const auto mmdd_it = ctx.args.find("mmdd");
    if (date_it != ctx.args.end() && !valid_utc_day(date_it->second))
      return argument_error("invalid_argument", "date", "real UTC YYYY-MM-DD required");
    if (mmdd_it != ctx.args.end() && !valid_month_day(mmdd_it->second))
      return argument_error("invalid_argument", "mmdd", "real MM-DD required");

    std::string anchor = date_it == ctx.args.end() ? util::utc_date() : date_it->second;
    if (mmdd_it != ctx.args.end()) {
      if (date_it != ctx.args.end() && anchor.substr(5, 5) != mmdd_it->second)
        return argument_error("invalid_argument", "date", "date and mmdd must match");
      if (date_it == ctx.args.end()) anchor = util::utc_date().substr(0, 5) + mmdd_it->second;
    }

    int limit = 50;
    if (!parse_bounded_uint(ctx, "limit", 50, 1, 200, limit, error)) return error;
    try {
      auto source = resolve_source(ctx, true, error);
      if (!source) return error;
      const bool virtual_leap_day = date_it == ctx.args.end() && mmdd_it != ctx.args.end() &&
                                    mmdd_it->second == "02-29";
      const auto hits = ctx.brain->chronicle_on_this_day(anchor, limit, *source,
                                                          virtual_leap_day);
      json rows = json::array();
      for (const auto& hit : hits) {
        rows.push_back({{"source_id", hit.source_id},
                        {"slug", hit.slug},
                        {"title", bounded_display_utf8(hit.title, 512, true)},
                        {"type", hit.type},
                        {"created_at", hit.created_at},
                        {"updated_at", hit.updated_at},
                        {"matched_at", hit.matched_at},
                        {"years_ago", hit.years_ago}});
      }
      r.json = rows.dump(2);
      r.text = r.json;
      return r;
    } catch (const std::exception&) {
      return argument_error("database_error", "database", "Chronicle history read failed");
    }
  }, false,
      "Qbrain Chronicle subset: read prior-year same-UTC-day page activity in one authorized "
      "canonical source; "
      "no timeline-event storage, extraction jobs, narrative generation, or full Chronicle parity",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","minLength":1,"maxLength":64,"x-maxUtf8Bytes":64,"pattern":"^(?!(?:[Cc][Oo][Nn]|[Pp][Rr][Nn]|[Aa][Uu][Xx]|[Nn][Uu][Ll]|[Cc][Oo][Mm][1-9]|[Ll][Pp][Tt][1-9])$)[A-Za-z0-9_-]+$","description":"1-64 ASCII bytes; canonicalized to lowercase; Windows reserved device names rejected.","default":"default"},"date":{"type":"string","minLength":10,"maxLength":10,"pattern":"^[0-9]{4}-[0-9]{2}-[0-9]{2}$"},"mmdd":{"type":"string","minLength":5,"maxLength":5,"pattern":"^[0-9]{2}-[0-9]{2}$"},"limit":{"type":"integer","minimum":0,"maximum":200,"default":50}}})");

  register_one(
      "chronicle_last_seen", Scope::Read, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"source_id", "entity", "slug", "asof"}, error))
      return error;

    const auto entity_it = ctx.args.find("entity");
    const auto slug_it = ctx.args.find("slug");
    if (entity_it == ctx.args.end() && slug_it == ctx.args.end())
      return argument_error("invalid_argument", "entity", "entity is required");
    if (entity_it != ctx.args.end() && slug_it != ctx.args.end() &&
        entity_it->second != slug_it->second)
      return argument_error("invalid_argument", "entity", "entity and slug must match");
    const std::string entity = entity_it != ctx.args.end() ? entity_it->second : slug_it->second;
    if (entity.empty() || entity.size() > 4096 || !is_valid_utf8(entity))
      return argument_error("invalid_argument", "entity", "valid bounded UTF-8 entity required");

    const auto asof_it = ctx.args.find("asof");
    const std::string asof = asof_it == ctx.args.end() ? util::utc_date() : asof_it->second;
    const auto asof_day = parse_utc_sys_day(asof);
    if (!asof_day)
      return argument_error("invalid_argument", "asof", "real UTC YYYY-MM-DD required");

    try {
      auto source = resolve_source(ctx, true, error);
      if (!source) return error;
      auto found = ctx.brain->chronicle_last_seen(entity, *source);
      if (!found)
        return argument_error("not_found", "entity", "entity was not found in the source");
      if (found->last_seen.size() < 10)
        return argument_error("database_error", "database", "invalid Chronicle timestamp");
      const auto last_seen_day = parse_utc_sys_day(found->last_seen.substr(0, 10));
      if (!last_seen_day)
        return argument_error("database_error", "database", "invalid Chronicle timestamp");
      const auto days_ago = (*asof_day - *last_seen_day).count();
      json result = {{"source_id", found->source_id},
                     {"entity", found->entity},
                     {"last_seen", found->last_seen},
                     {"days_ago", days_ago}};
      r.json = result.dump(2);
      r.text = r.json;
      return r;
    } catch (const std::exception&) {
      return argument_error("database_error", "database", "Chronicle last-seen read failed");
    }
  }, false,
      "Qbrain Chronicle subset: read one entity page's source-scoped effective activity in "
      "one authorized canonical source; "
      "no timeline-event storage, extraction jobs, narrative generation, or full Chronicle parity",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","minLength":1,"maxLength":64,"x-maxUtf8Bytes":64,"pattern":"^(?!(?:[Cc][Oo][Nn]|[Pp][Rr][Nn]|[Aa][Uu][Xx]|[Nn][Uu][Ll]|[Cc][Oo][Mm][1-9]|[Ll][Pp][Tt][1-9])$)[A-Za-z0-9_-]+$","description":"1-64 ASCII bytes; canonicalized to lowercase; Windows reserved device names rejected.","default":"default"},"entity":{"type":"string","minLength":1,"x-maxUtf8Bytes":4096,"description":"Valid UTF-8 entity identifier; at most 4096 UTF-8 bytes."},"slug":{"type":"string","minLength":1,"x-maxUtf8Bytes":4096,"description":"Legacy entity alias; valid UTF-8 and at most 4096 UTF-8 bytes."},"asof":{"type":"string","minLength":10,"maxLength":10,"pattern":"^[0-9]{4}-[0-9]{2}-[0-9]{2}$"}},"anyOf":[{"required":["entity"]},{"required":["slug"]}]})");

  register_one(
      "chronicle_backfill", Scope::Write, [](OpContext& ctx) {
    OpResult r, error;
    if (!validate_allowed_args(ctx, {"source_id", "since", "limit", "dry_run"}, error))
      return error;
    const auto since_it = ctx.args.find("since");
    std::optional<std::string> since;
    if (since_it != ctx.args.end()) {
      if (since_it->second.empty() || !valid_utc_since(since_it->second))
        return argument_error("invalid_argument", "since", "valid UTC date or timestamp required");
      since = since_it->second;
    }
    int limit = 1000;
    if (!parse_bounded_uint(ctx, "limit", 1000, 1, 1000, limit, error)) return error;
    bool dry_run = false;
    if (!parse_boolean(ctx, "dry_run", false, dry_run, error)) return error;

    try {
      auto source = resolve_source(ctx, true, error);
      if (!source) return error;
      const auto result = ctx.brain->chronicle_backfill(*source, since, limit, dry_run);
      json payload = {{"source_id", result.source_id},
                      {"scanned", result.scanned},
                      {"eligible", result.eligible},
                      {"tagged", result.tagged},
                      {"already_tagged", result.already_tagged},
                      {"dry_run", result.dry_run}};
      r.json = payload.dump(2);
      r.text = r.json;
      return r;
    } catch (const std::exception& exception) {
      if (is_database_busy_error(exception))
        return argument_error("database_busy", "database", "Chronicle backfill database is busy");
      return argument_error("database_error", "database", "Chronicle backfill failed");
    }
  }, true,
      "Qbrain Chronicle subset: tag eligible pages in one authorized canonical source "
      "idempotently; no timeline-event "
      "storage, extraction jobs, narrative generation, or full Chronicle parity",
      R"({"type":"object","additionalProperties":false,"properties":{"source_id":{"type":"string","minLength":1,"maxLength":64,"x-maxUtf8Bytes":64,"pattern":"^(?!(?:[Cc][Oo][Nn]|[Pp][Rr][Nn]|[Aa][Uu][Xx]|[Nn][Uu][Ll]|[Cc][Oo][Mm][1-9]|[Ll][Pp][Tt][1-9])$)[A-Za-z0-9_-]+$","description":"1-64 ASCII bytes; canonicalized to lowercase; Windows reserved device names rejected.","default":"default"},"since":{"type":"string","minLength":10,"maxLength":20,"pattern":"^[0-9]{4}-[0-9]{2}-[0-9]{2}([T ][0-9]{2}:[0-9]{2}:[0-9]{2}Z)?$"},"limit":{"type":"integer","minimum":0,"maximum":1000,"default":1000},"dry_run":{"type":"boolean","default":false}}})");

  // N24 files
  register_one(
      "file_upload", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto path = arg(ctx, "path");
    if (path.empty()) path = arg(ctx, "src");
    if (path.empty()) {
      r.ok = false;
      r.text = "path required";
      return r;
    }
    auto id = files::upload(*ctx.brain, path, arg(ctx, "name"));
    if (id <= 0) {
      r.ok = false;
      r.text = "upload failed";
      return r;
    }
    r.json = json({{"id", id}, {"url", files::file_url(*ctx.brain, id)}}).dump(2);
    r.text = r.json;
    return r;
  }, false, "Upload local file into brain files dir",
      R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"}},"required":["path"]})");

  register_one(
      "file_list", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto rows = files::list_files(*ctx.brain, arg_int(ctx, "limit", 100));
    json arr = json::array();
    for (auto& e : rows)
      // N30 D4: remote callers get the stored file name as a relative
      // identifier; the absolute storage path stays local-only.
      arr.push_back({{"id", e.id},
                     {"name", e.name},
                   {"path", (ctx.remote || ctx.via_mcp) ? e.name : e.path},
                     {"size", e.size},
                     {"mime", e.mime}});
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "List attached files",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "file_url", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    std::string url;
    auto id_s = arg(ctx, "id");
    if (!id_s.empty()) {
      try {
        url = files::file_url(*ctx.brain, std::stoll(id_s));
      } catch (...) {
      }
    }
    if (url.empty()) url = files::file_url_by_name(*ctx.brain, arg(ctx, "name"));
    if (url.empty()) {
      r.ok = false;
      r.text = "not found";
      return r;
    }
    if (ctx.remote || ctx.via_mcp) {
      // N30 D4: file:/// URLs disclose the local filesystem layout; remote
      // callers receive identifiers only.
      json j = json::object();
      if (!id_s.empty()) {
        try {
          j["id"] = std::stoll(id_s);
        } catch (...) {
          j["id"] = id_s;
        }
      }
      auto name = arg(ctx, "name");
      if (!name.empty()) j["name"] = name;
      r.json = j.dump(2);
      r.text = r.json;
      return r;
    }
    r.json = json({{"url", url}}).dump(2);
    r.text = url;
    return r;
  }, false, "file:// URL for attachment (file URL disclosed to local callers only)",
      R"({"type":"object","properties":{"id":{"type":"integer"},"name":{"type":"string"}}})");

  // N25 schema/ontology deep
  register_one(
      "schema_lint", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto rows = schema::schema_lint(*ctx.brain, arg_int(ctx, "limit", 100));
    json arr = json::array();
    for (auto& i : rows)
      arr.push_back({{"code", i.code}, {"slug", i.slug}, {"detail", i.detail}});
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "Lint pages for schema issues",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "schema_graph", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto nodes = schema::schema_graph(*ctx.brain);
    json arr = json::array();
    for (auto& n : nodes)
      arr.push_back({{"id", n.id}, {"kind", n.kind}, {"count", n.count}});
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "Type graph nodes", R"({"type":"object","properties":{}})");

  register_one(
      "schema_explain_type", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto t = arg(ctx, "type");
    if (t.empty()) {
      r.ok = false;
      r.text = "type required";
      return r;
    }
    r.text = schema::schema_explain_type(*ctx.brain, t);
    r.json = json({{"type", t}, {"explain", r.text}}).dump(2);
    return r;
  }, false, "Explain a page type",
      R"({"type":"object","properties":{"type":{"type":"string"}},"required":["type"]})");

  register_one(
      "schema_review_orphans", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto o = ctx.brain->find_orphans(arg_int(ctx, "limit", 100));
    r.json = json(o).dump(2);
    r.text = r.json;
    return r;
  }, false, "Orphan pages (schema review)",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "ontology_propose", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto p = schema::ontology_propose(*ctx.brain, arg_int(ctx, "limit", 20));
    r.json = json(p).dump(2);
    r.text = r.json;
    return r;
  }, false, "Propose types missing from pack",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "ontology_conflicts", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto rows = schema::ontology_conflicts(*ctx.brain, arg_int(ctx, "limit", 50));
    json arr = json::array();
    for (auto& i : rows)
      arr.push_back({{"code", i.code}, {"slug", i.slug}, {"detail", i.detail}});
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "Types used but not in pack",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  // N26 agent / advisor / onboard / skillopt
  register_one(
      "submit_agent", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto prompt = arg(ctx, "prompt");
    if (prompt.empty()) prompt = arg(ctx, "task");
    if (prompt.empty()) {
      r.ok = false;
      r.text = "prompt required";
      return r;
    }
    json payload = {{"prompt", prompt},
                    {"model", arg(ctx, "model")},
                    {"source", arg(ctx, "source_id", "default")}};
    auto id = jobs::submit_job(*ctx.brain, "agent", payload.dump(), "default",
                               arg_int(ctx, "priority", 50));
    r.json = json({{"id", id}, {"type", "agent"}, {"status", "waiting"}}).dump(2);
    r.text = "agent job " + std::to_string(id);
    return r;
  }, false, "Enqueue agent job",
      R"({"type":"object","properties":{"prompt":{"type":"string"},"task":{"type":"string"},"priority":{"type":"integer"}}})");

  register_one(
      "advisor", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto q = arg(ctx, "question");
    if (q.empty()) q = arg(ctx, "query");
    if (q.empty()) {
      r.ok = false;
      r.text = "question required";
      return r;
    }
    search::HybridOpts opts;
    opts.limit = arg_int(ctx, "limit", 5);
    opts.use_vector = false;
    opts.mode = "conservative";
    opts.config = &ctx.brain->config();
    auto hits = search::hybrid_search(*ctx.brain, q, nullptr, opts);
    json evidence = json::array();
    std::ostringstream ctx_txt;
    for (auto& h : hits) {
      evidence.push_back({{"slug", h.slug}, {"title", h.title}, {"snippet", h.snippet}});
      ctx_txt << "- " << h.slug << ": " << h.title << "\n";
    }
    std::string advice = "Based on " + std::to_string(hits.size()) + " notes:\n" + ctx_txt.str();
    // optional chat enrichment fail-open
    auto cr = ai::chat_complete(ctx.brain->config(),
                                {{"system", "You are a brief advisor using only provided notes."},
                                 {"user", "Question: " + q + "\nNotes:\n" + ctx_txt.str()}},
                                0.2);
    if (cr.ok && !cr.content.empty()) advice = cr.content;
    r.json = json({{"advice", advice}, {"evidence", evidence}, {"llm", cr.ok}}).dump(2);
    r.text = advice;
    return r;
  }, false, "Advise from search (+ optional LLM)",
      R"({"type":"object","properties":{"question":{"type":"string"},"query":{"type":"string"},"limit":{"type":"integer"}}})");

  register_one(
      "run_onboard", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    schema::ensure_default_pack();
    ctx.brain->ensure_source("default");
    auto rem = ctx.brain->remediate();
    PageInput in;
    in.slug = "meta/welcome";
    in.title = "Welcome to Qbrain";
    in.body = "# Welcome\n\nYour brain is ready. Use capture, search, think, dream.\n";
    in.type = "note";
    in.source_kind = "onboard";
    auto page = ctx.brain->put_page(in);
    r.json = json({{"welcome_slug", page.slug},
                   {"remediate", {{"default_source", rem.default_source},
                                  {"reclaimed", rem.reclaimed}}}})
                 .dump(2);
    r.text = "onboarded " + page.slug;
    return r;
  }, true, "Initialize pack/source/welcome page", R"({"type":"object","properties":{}})");

  register_one(
      "run_skillopt", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    (void)ctx;
    // Report-only: list skills and note no mutation
    json arr = json::array();
    namespace fs = std::filesystem;
    for (auto& root : {fs::path("skills"), fs::path("D:/Projects/Qbrain/skills")}) {
      if (!fs::exists(root)) continue;
      for (auto& e : fs::directory_iterator(root)) {
        if (!e.is_directory()) continue;
        if (fs::exists(e.path() / "SKILL.md"))
          arr.push_back({{"name", e.path().filename().string()}, {"mutate", false}});
      }
    }
    r.json = json({{"skills", arr}, {"mode", "no-mutate"}, {"note", "review only"}}).dump(2);
    r.text = r.json;
    return r;
  }, true, "SkillOpt report-only stub", R"({"type":"object","properties":{}})");

  register_one(
      "list_brain_skillpack", Scope::Read, [](OpContext& ctx) {
    OpContext c2 = ctx;
    auto* op = global_registry().find("list_skills");
    return op ? op->handler(c2) : OpResult{false, 1, "list_skills missing", ""};
  }, false, "Alias list_skills", R"({"type":"object","properties":{}})");

  // N27 raw / transcripts / salience / image
  register_one(
      "put_raw_data", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto key = arg(ctx, "key");
    if (key.empty()) {
      r.ok = false;
      r.text = "key required";
      return r;
    }
    if (!ctx.brain->put_raw_data(key, arg(ctx, "content"), arg(ctx, "meta_json", "{}"))) {
      r.ok = false;
      r.text = "put failed";
      return r;
    }
    r.text = "ok " + key;
    r.json = json({{"key", key}}).dump(2);
    return r;
  }, false, "Store raw key/value text",
      R"({"type":"object","properties":{"key":{"type":"string"},"content":{"type":"string"},"meta_json":{"type":"string"}},"required":["key"]})");

  register_one(
      "get_raw_data", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto key = arg(ctx, "key");
    auto v = ctx.brain->get_raw_data(key);
    if (!v) {
      r.ok = false;
      r.text = "not found";
      return r;
    }
    r.json = json({{"key", key}, {"content", v->first}, {"meta_json", v->second}}).dump(2);
    r.text = v->first;
    return r;
  }, false, "Get raw data by key",
      R"({"type":"object","properties":{"key":{"type":"string"}},"required":["key"]})");

  register_one(
      "get_recent_transcripts", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    int limit = arg_int(ctx, "limit", 20);
    json arr = json::array();
    auto pages = ctx.brain->list_pages(limit, "transcript");
    for (auto& p : pages)
      arr.push_back({{"slug", p.slug}, {"title", p.title}, {"updated_at", p.updated_at}});
    // also raw keys transcript/
    for (auto& kv : ctx.brain->list_raw_prefix("transcript/", limit))
      arr.push_back({{"key", kv.first}, {"preview", kv.second.substr(0, 200)}});
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "Recent transcript pages/raw keys",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "get_recent_salience", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    int limit = arg_int(ctx, "limit", 20);
    // score = inbound links approx via SQL
    auto st = ctx.brain->db().prepare(
        "SELECT p.slug, p.title, p.updated_at, "
        "(SELECT COUNT(*) FROM links l WHERE l.to_slug=p.slug) AS inbound "
        "FROM pages p WHERE p.deleted_at IS NULL "
        "ORDER BY inbound DESC, p.updated_at DESC LIMIT ?");
    st.bind_int(1, limit);
    json arr = json::array();
    while (st.step())
      arr.push_back({{"slug", st.column_text(0)},
                     {"title", st.column_text(1)},
                     {"updated_at", st.column_text(2)},
                     {"salience", st.column_int(3)}});
    r.json = arr.dump(2);
    r.text = r.json;
    return r;
  }, false, "Pages by inbound-link salience",
      R"({"type":"object","properties":{"limit":{"type":"integer"}}})");

  register_one(
      "search_by_image", Scope::Read, [](OpContext& ctx) {
    OpResult r;
    auto path = arg(ctx, "path");
    auto name = arg(ctx, "name");
    if (path.empty() && name.empty()) {
      r.ok = false;
      r.text = "path or name required";
      return r;
    }
    // Heuristic: upload optional, match file_index by basename stem in page titles/slugs
    std::string stem = name;
    if (!path.empty()) {
      namespace fs = std::filesystem;
      stem = util::path_to_utf8(fs::path(path).stem());
      // best-effort index
      files::upload(*ctx.brain, path, name);
    }
    search::HybridOpts opts;
    opts.limit = arg_int(ctx, "limit", 10);
    opts.use_vector = false;
    opts.config = &ctx.brain->config();
    auto hits = search::hybrid_search(*ctx.brain, stem, nullptr, opts);
    json arr = json::array();
    for (auto& h : hits)
      arr.push_back({{"slug", h.slug}, {"title", h.title}, {"score", h.score}});
    r.json = json({{"query_stem", stem}, {"results", arr}, {"note", "filename heuristic, no vision model"}})
                 .dump(2);
    r.text = r.json;
    return r;
  }, false, "Image search stub via filename stem",
      R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"},"limit":{"type":"integer"}}})");

  // N28 schema_apply_mutations
  register_one(
      "schema_apply_mutations", Scope::Write, [](OpContext& ctx) {
    OpResult r;
    auto raw = arg(ctx, "mutations");
    if (raw.empty()) raw = arg(ctx, "mutations_json");
    if (raw.empty()) raw = "[]";
    int applied = 0;
    auto err = schema::apply_mutations(*ctx.brain, raw, &applied);
    if (!err.empty()) {
      r.ok = false;
      r.text = err;
      return r;
    }
    r.json = json({{"applied", applied},
                   {"active_pack", schema::active_pack_id(*ctx.brain)}})
                 .dump(2);
    r.text = r.json;
    return r;
  }, false, "Apply safe pack mutations (add_type/add_dimension)",
      R"({"type":"object","properties":{"mutations":{"type":"string"},"mutations_json":{"type":"string"}}})");
}

}  // namespace qbrain::ops
