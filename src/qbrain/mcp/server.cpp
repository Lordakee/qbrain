#include "qbrain/mcp/server.hpp"
#include "qbrain/mcp/jsonrpc.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/util/log.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string_view>
#include <unordered_map>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#endif

using json = nlohmann::json;

namespace qbrain::mcp {
namespace {

constexpr const char* kProtocolVersion = "2024-11-05";
constexpr const char* kServerVersion = "0.2.0-dev";

// Prefer client's protocolVersion when present (OpenCode may send 2025-03-26).
static std::string negotiate_protocol(const json& params) {
  if (params.is_object() && params.contains("protocolVersion") &&
      params["protocolVersion"].is_string()) {
    auto v = params["protocolVersion"].get<std::string>();
    if (!v.empty()) return v;
  }
  return kProtocolVersion;
}

json tool_defs() {
  json tools = json::array();
  for (auto* op : ops::global_registry().list()) {
    json t;
    t["name"] = op->name;
    t["description"] = op->description.empty() ? op->name : op->description;
    try {
      t["inputSchema"] = json::parse(op->input_schema_json);
    } catch (...) {
      t["inputSchema"] = {{"type", "object"}, {"properties", json::object()}};
    }
    tools.push_back(std::move(t));
  }
  return tools;
}

std::unordered_map<std::string, std::string> args_from_params(const json& params,
                                                              bool boolean_literals = false) {
  std::unordered_map<std::string, std::string> args;
  if (!params.is_object()) return args;
  for (auto it = params.begin(); it != params.end(); ++it) {
    if (it.value().is_string())
      args[it.key()] = it.value().get<std::string>();
    else if (it.value().is_boolean())
      args[it.key()] = it.value().get<bool>() ? (boolean_literals ? "true" : "1")
                                                   : (boolean_literals ? "false" : "0");
    else if (it.value().is_number_unsigned())
      args[it.key()] = std::to_string(it.value().get<uint64_t>());
    else if (it.value().is_number_integer())
      args[it.key()] = std::to_string(it.value().get<int64_t>());
    else if (it.value().is_number())
      args[it.key()] = std::to_string(it.value().get<double>());
    else if (it.value().is_null())
      continue;
    else
      args[it.key()] = it.value().dump();
  }
  return args;
}

bool uses_ambient_source(const std::string& operation_name) {
  return operation_name != "find_anomalies" && operation_name != "find_contradictions" &&
         operation_name != "find_experts" && operation_name != "code_def" &&
         operation_name != "code_refs" && operation_name != "code_callers" &&
         operation_name != "list_link_sources" && operation_name != "log_ingest" &&
         operation_name != "get_ingest_log" && operation_name != "chronicle_day" &&
         operation_name != "chronicle_since" && operation_name != "add_timeline_entry" &&
         operation_name != "replay_job" && operation_name != "send_job_message" &&
         operation_name != "list_job_messages" && operation_name != "get_brain_identity" &&
         operation_name != "volunteer_context" && operation_name != "get_timeline" &&
         operation_name != "volunteer_chronicle" &&
          operation_name != "chronicle_on_this_day" &&
          operation_name != "chronicle_last_seen" && operation_name != "chronicle_backfill" &&
          operation_name != "code_callees" && operation_name != "code_flow" &&
          operation_name != "code_blast" && operation_name != "code_traversal_cache_clear" &&
          operation_name != "list_schema_packs" &&
         operation_name != "get_active_schema_pack" &&
         operation_name != "reload_schema_pack" && operation_name != "schema_stats" &&
         operation_name != "ontology_get" && operation_name != "ontology_dimensions";
}

bool is_analytics_operation(const std::string& operation_name) {
  return operation_name == "find_anomalies" || operation_name == "find_contradictions" ||
         operation_name == "find_experts";
}

json make_error(const json& id, int code, const std::string& message) {
  return json{{"jsonrpc", "2.0"},
              {"id", id},
              {"error", {{"code", code}, {"message", message}}}};
}

json make_result(const json& id, const json& result) {
  return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
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

json make_tool_argument_error(const json& id, const std::string& field,
                              const std::string& message) {
  json payload = {
      {"error", {{"code", "invalid_argument"},
                 {"field", bounded_error_field(field)},
                 {"message", message}}}};
  return make_result(
      id, {{"content", json::array({{{"type", "text"}, {"text", payload.dump()}}})},
           {"isError", true}});
}

json validate_analytics_arguments(const json& id, const json& arguments) {
  if (!arguments.is_object()) {
    return make_tool_argument_error(id, "arguments", "object arguments required");
  }
  for (auto it = arguments.begin(); it != arguments.end(); ++it) {
    if (it.key() == "source_id") {
      if (!it.value().is_string()) {
        return make_tool_argument_error(id, "source_id", "string value required");
      }
    } else if (it.key() == "limit") {
      if (!it.value().is_number_unsigned()) {
        return make_tool_argument_error(id, "limit", "unsigned integer value required");
      }
    } else {
      return make_tool_argument_error(id, it.key(), "unexpected argument");
    }
  }
  return json();
}

enum class ArgumentType { String, UnsignedInteger, Boolean };

json validate_typed_arguments(
    const json& id, const json& arguments,
    const std::unordered_map<std::string, ArgumentType>& allowed_arguments) {
  if (!arguments.is_object()) {
    return make_tool_argument_error(id, "arguments", "object arguments required");
  }
  for (auto it = arguments.begin(); it != arguments.end(); ++it) {
    const auto allowed = allowed_arguments.find(it.key());
    if (allowed == allowed_arguments.end()) {
      return make_tool_argument_error(id, it.key(), "unexpected argument");
    }
    if (allowed->second == ArgumentType::String && !it.value().is_string()) {
      return make_tool_argument_error(id, it.key(), "string value required");
    }
    if (allowed->second == ArgumentType::UnsignedInteger &&
        !it.value().is_number_unsigned()) {
      return make_tool_argument_error(id, it.key(), "unsigned integer value required");
    }
    if (allowed->second == ArgumentType::Boolean && !it.value().is_boolean()) {
      return make_tool_argument_error(id, it.key(), "boolean value required");
    }
  }
  return json();
}

const std::unordered_map<std::string, ArgumentType>* typed_argument_schema(
    const std::string& operation_name) {
  using Type = ArgumentType;
  static const std::unordered_map<std::string, Type> code_arguments = {
      {"symbol", Type::String},       {"name", Type::String},
      {"source_id", Type::String},    {"limit", Type::UnsignedInteger},
      {"page_limit", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> source_only = {
      {"source_id", Type::String}};
  static const std::unordered_map<std::string, Type> ingest_write = {
      {"source_id", Type::String},    {"path", Type::String},
      {"event_type", Type::String},   {"detail_json", Type::String},
      {"keep_last", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> ingest_read = {
      {"source_id", Type::String}, {"limit", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> chronicle_day = {
      {"source_id", Type::String}, {"day", Type::String}, {"date", Type::String},
      {"limit", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> chronicle_since = {
      {"source_id", Type::String}, {"since", Type::String}, {"from", Type::String},
      {"limit", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> timeline_write = {
      {"source_id", Type::String}, {"title", Type::String},
      {"body", Type::String},      {"slug", Type::String}};
  static const std::unordered_map<std::string, Type> replay_job = {
      {"job_id", Type::UnsignedInteger}, {"id", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> send_job_message = {
      {"job_id", Type::UnsignedInteger}, {"id", Type::UnsignedInteger},
      {"sender", Type::String},           {"payload_json", Type::String}};
  static const std::unordered_map<std::string, Type> list_job_messages = {
      {"job_id", Type::UnsignedInteger}, {"id", Type::UnsignedInteger},
      {"limit", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> brain_identity = {
      {"source_id", Type::String}};
  static const std::unordered_map<std::string, Type> volunteer_context = {
      {"source_id", Type::String}, {"query", Type::String},
      {"q", Type::String},         {"limit", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> source_bounded_read = {
      {"source_id", Type::String}, {"limit", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> volunteer_chronicle = {
      {"source_id", Type::String}, {"since", Type::String},
      {"limit", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> chronicle_on_this_day = {
      {"source_id", Type::String}, {"date", Type::String},
      {"mmdd", Type::String},      {"limit", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> chronicle_last_seen = {
      {"source_id", Type::String}, {"entity", Type::String},
      {"slug", Type::String},      {"asof", Type::String}};
  static const std::unordered_map<std::string, Type> chronicle_backfill = {
      {"source_id", Type::String},       {"since", Type::String},
      {"limit", Type::UnsignedInteger}, {"dry_run", Type::Boolean}};
  static const std::unordered_map<std::string, Type> code_callees = {
      {"symbol", Type::String}, {"name", Type::String}, {"source_id", Type::String},
      {"limit", Type::UnsignedInteger}, {"page_limit", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> code_flow = {
      {"entry_point", Type::String}, {"symbol", Type::String}, {"name", Type::String},
      {"source_id", Type::String}, {"depth", Type::UnsignedInteger},
      {"limit", Type::UnsignedInteger}, {"page_limit", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> code_blast = {
      {"symbol", Type::String}, {"name", Type::String}, {"source_id", Type::String},
      {"limit", Type::UnsignedInteger}, {"page_limit", Type::UnsignedInteger}};
  static const std::unordered_map<std::string, Type> no_arguments;
  static const std::unordered_map<std::string, Type> schema_pack_id = {
      {"id", Type::String}};
  static const std::unordered_map<std::string, Type> schema_stats = {
      {"source_id", Type::String}, {"limit", Type::UnsignedInteger}};

  if (operation_name == "code_def" || operation_name == "code_refs" ||
      operation_name == "code_callers")
    return &code_arguments;
  if (operation_name == "list_link_sources") return &source_only;
  if (operation_name == "log_ingest") return &ingest_write;
  if (operation_name == "get_ingest_log") return &ingest_read;
  if (operation_name == "chronicle_day") return &chronicle_day;
  if (operation_name == "chronicle_since") return &chronicle_since;
  if (operation_name == "add_timeline_entry") return &timeline_write;
  if (operation_name == "replay_job") return &replay_job;
  if (operation_name == "send_job_message") return &send_job_message;
  if (operation_name == "list_job_messages") return &list_job_messages;
  if (operation_name == "get_brain_identity") return &brain_identity;
  if (operation_name == "volunteer_context") return &volunteer_context;
  if (operation_name == "get_timeline") return &source_bounded_read;
  if (operation_name == "volunteer_chronicle") return &volunteer_chronicle;
  if (operation_name == "chronicle_on_this_day") return &chronicle_on_this_day;
  if (operation_name == "chronicle_last_seen") return &chronicle_last_seen;
  if (operation_name == "chronicle_backfill") return &chronicle_backfill;
  if (operation_name == "code_callees") return &code_callees;
  if (operation_name == "code_flow") return &code_flow;
  if (operation_name == "code_blast") return &code_blast;
  if (operation_name == "list_schema_packs" ||
      operation_name == "get_active_schema_pack")
    return &no_arguments;
  if (operation_name == "reload_schema_pack" || operation_name == "ontology_get" ||
      operation_name == "ontology_dimensions")
    return &schema_pack_id;
  if (operation_name == "code_traversal_cache_clear") return &no_arguments;
  if (operation_name == "schema_stats") return &schema_stats;
  return nullptr;
}

std::unordered_map<std::string, std::string> analytics_args_from_params(const json& params) {
  std::unordered_map<std::string, std::string> args;
  for (auto it = params.begin(); it != params.end(); ++it) {
    args[it.key()] = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
  }
  return args;
}

json handle_request(Brain& brain, const ServeOptions& opts, const json& req) {
  json id = nullptr;
  if (req.contains("id")) id = req["id"];

  if (!req.contains("method") || !req["method"].is_string()) {
    return make_error(id, -32600, "Invalid Request");
  }
  const auto method = req["method"].get<std::string>();
  json params = req.contains("params") ? req["params"] : json::object();

  // notifications have no id — return empty string sentinel handled by caller
  const bool is_notification = !req.contains("id");

  if (method == "initialize") {
    json result = {
        {"protocolVersion", negotiate_protocol(params)},
        {"capabilities", {{"tools", {{"listChanged", false}}}}},
        {"serverInfo", {{"name", "qbrain"}, {"version", kServerVersion}}},
        {"instructions",
         "Qbrain personal knowledge brain (Windows-native). "
         "Prefer search then get_page. Writes require serve --allow-write."}};
    return make_result(id, result);
  }

  if (method == "notifications/initialized" || method == "initialized") {
    return json();  // no response
  }

  if (method == "ping") {
    if (is_notification) return json();
    return make_result(id, json::object());
  }

  if (method == "tools/list") {
    return make_result(id, json{{"tools", tool_defs()}});
  }

  if (method == "tools/call") {
    if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
      return make_error(id, -32602, "tools/call requires string params.name");
    }
    auto name = params["name"].get<std::string>();
    json arguments = params.contains("arguments") ? params["arguments"] : json::object();
    if (is_analytics_operation(name)) {
      auto argument_error = validate_analytics_arguments(id, arguments);
      if (!argument_error.is_null()) return argument_error;
    } else if (const auto* schema = typed_argument_schema(name)) {
      auto argument_error = validate_typed_arguments(id, arguments, *schema);
      if (!argument_error.is_null()) return argument_error;
    }

    ops::OpContext ctx;
    ctx.brain = &brain;
    // N30 D3: only the HTTP transport counts as network-remote; stdio is a
    // local trusted pipe. Both are MCP transports, so the registry's audited
    // write default-deny (--allow-write opt-in) applies to stdio as well.
    ctx.remote = opts.http_transport;
    ctx.via_mcp = true;
    ctx.allow_write = opts.allow_write;
    ctx.args = is_analytics_operation(name)
                   ? analytics_args_from_params(arguments)
                   : args_from_params(arguments, name == "chronicle_backfill");
    if (uses_ambient_source(name) && ctx.args.find("source_id") == ctx.args.end()) {
      if (const char* s = std::getenv("QBRAIN_SOURCE")) ctx.args["source_id"] = s;
    }

    auto r = ops::global_registry().call(name, ctx);
    std::string text = (!r.ok && !r.json.empty()) ? r.json : r.text;
    if (text.empty() && !r.json.empty()) text = r.json;
    json result = {{"content", json::array({{{"type", "text"}, {"text", text}}})},
                   {"isError", !r.ok}};
    // also attach structured json when present (as second text block for agents)
    if (r.ok && !r.json.empty() && r.json != r.text) {
      result["content"].push_back({{"type", "text"}, {"text", r.json}});
    }
    return make_result(id, result);
  }

  if (is_notification) return json();
  return make_error(id, -32601, "Method not found: " + method);
}

}  // namespace

std::string handle_rpc_body(Brain& brain, const ServeOptions& opts,
                            const std::string& request_json) {
  json req;
  try {
    req = json::parse(request_json);
  } catch (const std::exception&) {
    return make_error(nullptr, -32700, "parse error").dump();
  }
  try {
    if (req.is_array()) return make_error(nullptr, -32600, "batch not supported").dump();
    auto resp = handle_request(brain, opts, req);
    if (resp.is_null() || resp.empty()) return {};
    return resp.dump();
  } catch (const std::exception&) {
    return make_error(nullptr, -32603, "internal error").dump();
  }
}

int run_stdio_server(Brain& brain, const ServeOptions& opts) {
  util::set_log_level(util::Level::Warn);
#ifdef _WIN32
  // MCP framing requires raw bytes; Windows text-mode can mangle \r\n on pipes.
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  std::cerr << "[qbrain-serve] stdio MCP ready brain=" << brain.brain_id()
            << " write=" << (opts.allow_write ? "ENABLED" : "disabled") << "\n";
  if (opts.allow_write) {
    std::cerr << "[qbrain-serve] MCP write tools ENABLED (--allow-write)\n";
  }

  for (;;) {
    auto msg = read_framed_message();
    if (!msg) {
      std::cerr << "[qbrain-serve] shutdown: stdin EOF\n";
      return 0;
    }
    auto body = handle_rpc_body(brain, opts, *msg);
    if (body.empty()) continue;  // notification
    if (!write_framed_message(body)) {
      std::cerr << "[qbrain-serve] stdout write failed\n";
      return 2;
    }
  }
}

}  // namespace qbrain::mcp
