#pragma once
#include "qbrain/core/brain.hpp"
#include <string>
#include <string_view>

namespace qbrain::mcp {

struct ServeOptions {
  std::string brain_id = "default";
  bool allow_write = false;
  // N30 D3: transport discriminator. stdio is a local trusted pipe (allow_write
  // honored, ctx.remote=false); HTTP is network-exposed (ctx.remote=true, so the
  // registry centrally denies Write/Admin regardless of allow_write).
  bool http_transport = false;
};

// N30 D5: exact HTTP request-line parsing (unit-testable). A request line is
// "method SP request-target SP HTTP-version" with single-space separators.
struct HttpRequestLine {
  std::string method;  // e.g. "POST" (case-sensitive token)
  std::string path;    // target without the query part (e.g. "/ingest")
  std::string query;   // without the leading '?' (empty when absent)
  std::string version; // e.g. "HTTP/1.1"
};

// Parse one request line. Returns false on any malformed input (bad token,
// wrong separator count, non origin-form target, bad version, control bytes).
bool parse_http_request_line(std::string_view line, HttpRequestLine& out);

enum class HttpRoute {
  Ingest,           // POST /ingest
  JsonRpc,          // POST /
  Health,           // GET /health
  Admin,            // GET /admin
  MethodNotAllowed, // known path, unsupported method
  NotFound,         // unknown path
};

// Exact-path routing decision; no substring matching anywhere.
HttpRoute route_http_request(const HttpRequestLine& request);

// Blocks until stdin EOF / client disconnect. Returns process exit code.
int run_stdio_server(Brain& brain, const ServeOptions& opts);

// Loopback HTTP JSON-RPC (Bearer token required). Port default 7420.
int run_http_server(Brain& brain, const ServeOptions& opts, const std::string& token, int port = 7420);

// Single-request handler for tests (JSON-RPC request object as string → response body)
std::string handle_rpc_body(Brain& brain, const ServeOptions& opts, const std::string& request_json);

}  // namespace qbrain::mcp
