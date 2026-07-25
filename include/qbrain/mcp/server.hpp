#pragma once
#include "qbrain/core/brain.hpp"
#include <string>

namespace qbrain::mcp {

struct ServeOptions {
  std::string brain_id = "default";
  bool allow_write = false;
};

// Blocks until stdin EOF / client disconnect. Returns process exit code.
int run_stdio_server(Brain& brain, const ServeOptions& opts);

// Single-request handler for tests (JSON-RPC request object as string → response body)
std::string handle_rpc_body(Brain& brain, const ServeOptions& opts, const std::string& request_json);

}  // namespace qbrain::mcp
