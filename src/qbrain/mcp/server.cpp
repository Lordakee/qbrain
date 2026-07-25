#include "qbrain/mcp/server.hpp"
#include "qbrain/mcp/jsonrpc.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/util/log.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
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

std::unordered_map<std::string, std::string> args_from_params(const json& params) {
  std::unordered_map<std::string, std::string> args;
  if (!params.is_object()) return args;
  for (auto it = params.begin(); it != params.end(); ++it) {
    if (it.value().is_string())
      args[it.key()] = it.value().get<std::string>();
    else if (it.value().is_boolean())
      args[it.key()] = it.value().get<bool>() ? "1" : "0";
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

json make_error(const json& id, int code, const std::string& message) {
  return json{{"jsonrpc", "2.0"},
              {"id", id},
              {"error", {{"code", code}, {"message", message}}}};
}

json make_result(const json& id, const json& result) {
  return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
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
    if (!params.is_object() || !params.contains("name")) {
      return make_error(id, -32602, "tools/call requires params.name");
    }
    auto name = params["name"].get<std::string>();
    json arguments = params.contains("arguments") ? params["arguments"] : json::object();

    ops::OpContext ctx;
    ctx.brain = &brain;
    ctx.remote = true;
    ctx.allow_write = opts.allow_write;
    ctx.args = args_from_params(arguments);
    if (ctx.args.find("source_id") == ctx.args.end()) {
      if (const char* s = std::getenv("QBRAIN_SOURCE")) ctx.args["source_id"] = s;
    }

    auto r = ops::global_registry().call(name, ctx);
    std::string text = r.text;
    if (text.empty() && !r.json.empty()) text = r.json;
    json result = {{"content", json::array({{{"type", "text"}, {"text", text}}})},
                   {"isError", !r.ok}};
    // also attach structured json when present (as second text block for agents)
    if (!r.json.empty() && r.json != r.text) {
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
  try {
    auto req = json::parse(request_json);
    // batch not supported
    if (req.is_array()) {
      return make_error(nullptr, -32600, "batch not supported").dump();
    }
    auto resp = handle_request(brain, opts, req);
    if (resp.is_null() || resp.empty()) return {};  // notification
    return resp.dump();
  } catch (const std::exception& e) {
    return make_error(nullptr, -32700, std::string("parse error: ") + e.what()).dump();
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
