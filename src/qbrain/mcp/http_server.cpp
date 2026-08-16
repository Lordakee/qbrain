#include "qbrain/mcp/auth.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/ingest/import.hpp"
#include "qbrain/util/log.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <atomic>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

using json = nlohmann::json;

namespace qbrain::mcp {

// Minimal loopback HTTP JSON-RPC for tools/list and tools/call (N7).
// Auth: Authorization: Bearer <token> required. Bind 127.0.0.1 only.

#ifdef _WIN32
namespace {

constexpr size_t kMaxHttpRequestBytes = 16 * 1024 * 1024;

bool ascii_iequals(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); ++i) {
    auto lower = [](unsigned char c) {
      return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
    };
    if (lower(static_cast<unsigned char>(left[i])) != lower(static_cast<unsigned char>(right[i]))) {
      return false;
    }
  }
  return true;
}

std::string_view trim_ows(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
  return value;
}

enum class HeaderStatus { absent, present, invalid };

HeaderStatus find_single_header(std::string_view headers, std::string_view wanted,
                                std::string_view* value) {
  bool found = false;
  size_t line_start = 0;
  while (line_start <= headers.size()) {
    auto line_end = headers.find("\r\n", line_start);
    if (line_end == std::string_view::npos) line_end = headers.size();
    auto line = headers.substr(line_start, line_end - line_start);
    auto colon = line.find(':');
    if (colon != std::string_view::npos &&
        ascii_iequals(line.substr(0, colon), wanted)) {
      if (found) return HeaderStatus::invalid;
      found = true;
      if (value) *value = trim_ows(line.substr(colon + 1));
    }
    if (line_end == headers.size()) break;
    line_start = line_end + 2;
  }
  return found ? HeaderStatus::present : HeaderStatus::absent;
}

bool parse_size(std::string_view value, size_t* result) {
  if (value.empty() || result == nullptr) return false;
  size_t parsed = 0;
  for (char c : value) {
    if (c < '0' || c > '9') return false;
    const auto digit = static_cast<size_t>(c - '0');
    if (parsed > (std::numeric_limits<size_t>::max() - digit) / 10) return false;
    parsed = parsed * 10 + digit;
  }
  *result = parsed;
  return true;
}

bool send_all(SOCKET client, std::string_view data) {
  size_t sent = 0;
  while (sent < data.size()) {
    const auto remaining = data.size() - sent;
    const auto chunk = static_cast<int>(std::min<size_t>(remaining, std::numeric_limits<int>::max()));
    const int n = ::send(client, data.data() + sent, chunk, 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

}  // namespace
#endif

static bool check_auth(const std::string& headers, const std::string& token) {
  if (token.empty()) return false;

#ifdef _WIN32
  std::string_view value;
  if (find_single_header(headers, "Authorization", &value) != HeaderStatus::present) return false;
  value = trim_ows(value);
  constexpr std::string_view kScheme = "Bearer";
  if (value.size() <= kScheme.size() || !ascii_iequals(value.substr(0, kScheme.size()), kScheme)) {
    return false;
  }
  value.remove_prefix(kScheme.size());
  if (value.empty() || (value.front() != ' ' && value.front() != '\t')) return false;
  value = trim_ows(value);
  if (value.empty() || value.size() != token.size()) return false;

  // Constant-time-ish comparison after the header name and Bearer scheme have
  // both been matched at their protocol boundaries.
  volatile unsigned char diff = 0;
  for (size_t i = 0; i < value.size(); ++i) {
    diff |= static_cast<unsigned char>(value[i] ^ token[i]);
  }
  return diff == 0;
#else
  (void)headers;
  return false;
#endif
}

static bool is_tchar(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '*' ||
         c == '+' || c == '-' || c == '.' || c == '^' || c == '_' || c == '`' || c == '|' ||
         c == '~';
}

bool parse_http_request_line(std::string_view line, HttpRequestLine& out) {
  const auto sp1 = line.find(' ');
  if (sp1 == std::string_view::npos || sp1 == 0) return false;
  const auto method = line.substr(0, sp1);
  for (const char c : method) {
    if (!is_tchar(static_cast<unsigned char>(c))) return false;
  }

  const auto rest = line.substr(sp1 + 1);
  const auto sp2 = rest.find(' ');
  if (sp2 == std::string_view::npos || sp2 == 0) return false;
  const auto target = rest.substr(0, sp2);
  const auto version = rest.substr(sp2 + 1);

  // HTTP-version: exactly "HTTP/" DIGIT "." DIGIT
  if (version.size() != 8 || version.substr(0, 5) != "HTTP/" ||
      version[5] < '0' || version[5] > '9' || version[6] != '.' ||
      version[7] < '0' || version[7] > '9') {
    return false;
  }

  // request-target: origin-form only ("/" origin path [ "?" query ]).
  if (target.empty() || target.front() != '/') return false;
  for (const unsigned char c : target) {
    if (c <= 0x20 || c >= 0x7F || c == '#') return false;
  }

  const auto query = target.find('?');
  out.method.assign(method);
  out.path.assign(target.substr(0, query));
  if (query == std::string_view::npos) {
    out.query.clear();
  } else {
    out.query.assign(target.substr(query + 1));
  }
  out.version.assign(version);
  return true;
}

HttpRoute route_http_request(const HttpRequestLine& request) {
  const bool get = request.method == "GET";
  const bool post = request.method == "POST";
  if (request.path == "/ingest") return post ? HttpRoute::Ingest : HttpRoute::MethodNotAllowed;
  if (request.path == "/") return post ? HttpRoute::JsonRpc : HttpRoute::MethodNotAllowed;
  if (request.path == "/health") return get ? HttpRoute::Health : HttpRoute::MethodNotAllowed;
  if (request.path == "/admin") return get ? HttpRoute::Admin : HttpRoute::MethodNotAllowed;
  return HttpRoute::NotFound;
}

int run_http_server(Brain& brain, const ServeOptions& opts, const std::string& token, int port) {
#ifdef _WIN32
  // N36: scoped tokens from QBRAIN_MCP_TOKENS (name:token:scope[,scope]; ';'-separated).
  // Legacy QBRAIN_MCP_TOKEN stays valid transport auth with no capability (N30 semantics).
  std::vector<mcp::ScopedToken> scoped_tokens;
  if (const char* scoped_cfg = std::getenv("QBRAIN_MCP_TOKENS")) {
    scoped_tokens = mcp::parse_scoped_tokens(scoped_cfg);
    std::cerr << "[qbrain-http] " << scoped_tokens.size() << " scoped token(s) loaded\n";
  }
  if (token.empty() && scoped_tokens.empty()) {
    std::cerr << "[qbrain-http] token required (set QBRAIN_MCP_TOKEN or QBRAIN_MCP_TOKENS env, not argv)\n";
    return 2;
  }
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 2;
  SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_sock == INVALID_SOCKET) {
    WSACleanup();
    return 2;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(port));
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  int opt = 1;
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
  if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
    std::cerr << "[qbrain-http] bind failed\n";
    closesocket(listen_sock);
    WSACleanup();
    return 2;
  }
  listen(listen_sock, 8);
  std::cerr << "[qbrain-http] listening 127.0.0.1:" << port << " (Bearer required)\n";

  for (;;) {
    SOCKET client = accept(listen_sock, nullptr, nullptr);
    if (client == INVALID_SOCKET) break;
    auto send_http_response = [&](int status, std::string_view content_type,
                                  std::string_view response_body) {
      const char* reason = "Error";
      switch (status) {
        case 200: reason = "OK"; break;
        case 400: reason = "Bad Request"; break;
        case 401: reason = "Unauthorized"; break;
        case 403: reason = "Forbidden"; break;
        case 404: reason = "Not Found"; break;
        case 405: reason = "Method Not Allowed"; break;
        default: break;
      }
      std::string resp = "HTTP/1.1 " + std::to_string(status) + " " + reason +
                         "\r\nContent-Type: " + std::string(content_type) +
                         "\r\nContent-Length: " + std::to_string(response_body.size()) +
                         "\r\nConnection: close\r\n\r\n" + std::string(response_body);
      send_all(client, resp);
    };

    std::string req;
    req.reserve(8192);
    size_t hdr_end = std::string::npos;
    bool request_read_error = false;
    char buf[8192];
    while (hdr_end == std::string::npos) {
      const int n = recv(client, buf, static_cast<int>(sizeof(buf)), 0);
      if (n <= 0) {
        request_read_error = true;
        break;
      }
      if (req.size() > kMaxHttpRequestBytes - static_cast<size_t>(n)) {
        request_read_error = true;
        break;
      }
      req.append(buf, static_cast<size_t>(n));
      hdr_end = req.find("\r\n\r\n");
    }
    if (request_read_error || hdr_end == std::string::npos) {
      send_http_response(400, "application/json", R"({"error":"bad request"})");
      closesocket(client);
      continue;
    }

    const std::string headers = req.substr(0, hdr_end);
    const size_t body_offset = hdr_end + 4;

    // N30 D5: exact request-line parsing before any routing decision.
    const auto first_line_end = headers.find("\r\n");
    const std::string_view request_line(
        headers.data(), first_line_end == std::string::npos ? headers.size() : first_line_end);
    HttpRequestLine parsed;
    if (!parse_http_request_line(request_line, parsed)) {
      send_http_response(400, "application/json", R"({"error":"malformed request line"})");
      closesocket(client);
      continue;
    }
    const HttpRoute route = route_http_request(parsed);

    if (body_offset > kMaxHttpRequestBytes) {
      send_http_response(400, "application/json", R"({"error":"request too large"})");
      closesocket(client);
      continue;
    }
    std::string body;
    std::string_view content_length_value;
    const auto content_length_status =
        find_single_header(headers, "Content-Length", &content_length_value);
    size_t content_length = 0;
    if (content_length_status == HeaderStatus::invalid ||
        (content_length_status == HeaderStatus::present &&
         !parse_size(content_length_value, &content_length))) {
      send_http_response(400, "application/json", R"({"error":"invalid content-length"})");
      closesocket(client);
      continue;
    }

    if (content_length_status == HeaderStatus::present) {
      if (content_length > kMaxHttpRequestBytes - body_offset) {
        send_http_response(400, "application/json", R"({"error":"invalid content-length"})");
        closesocket(client);
        continue;
      }
      while (req.size() - body_offset < content_length) {
        const int n = recv(client, buf, static_cast<int>(sizeof(buf)), 0);
        if (n <= 0 || req.size() > kMaxHttpRequestBytes - static_cast<size_t>(n)) {
          request_read_error = true;
          break;
        }
        req.append(buf, static_cast<size_t>(n));
      }
      if (request_read_error || req.size() - body_offset < content_length) {
        send_http_response(400, "application/json", R"({"error":"incomplete request body"})");
        closesocket(client);
        continue;
      }
      body.assign(req.data() + body_offset, content_length);
    } else {
      body.assign(req.data() + body_offset, req.size() - body_offset);
    }

    std::string response_body;
    int status = 200;
    // N36: scoped-token authentication feeds the N30 central authorization
    // gate; malformed headers and unknown tokens both compare as mismatch.
    std::string request_capability;
    bool authenticated = false;
    std::string audit_identity = "anonymous";
    if (!token.empty() && check_auth(headers, token)) {
      authenticated = true;
      audit_identity = mcp::audit_hash_prefix(token) + "/legacy";
    } else if (!scoped_tokens.empty()) {
      std::string_view value;
      if (find_single_header(headers, "Authorization", &value) == HeaderStatus::present) {
        value = trim_ows(value);
        constexpr std::string_view kScheme = "Bearer";
        if (value.size() > kScheme.size() &&
            ascii_iequals(value.substr(0, kScheme.size()), kScheme)) {
          value = trim_ows(value.substr(kScheme.size()));
          if (!value.empty() && value.size() <= 256) {
            if (auto auth = mcp::authenticate_bearer(scoped_tokens, value)) {
              authenticated = true;
              request_capability = auth->capability;
              audit_identity = auth->hash_prefix + "/" + auth->name;
            }
          }
        }
      }
    }
    if (!authenticated) {
      status = 401;
      response_body = R"({"error":"unauthorized"})";
      std::cerr << "[qbrain-http] audit method=" << (route == HttpRoute::JsonRpc ? "POST" : "GET")
                << " result=401 identity=" << audit_identity << "\n";
    } else {
      switch (route) {
        case HttpRoute::Ingest: {
          // N5/N7: write path — require --allow-write (token alone is not enough)
          if (!opts.allow_write) {
            status = 403;
            response_body = json({{"ok", false}, {"error", "write denied (need --allow-write)"}}).dump();
          } else if (body.empty()) {
            status = 400;
            response_body = json({{"ok", false}, {"error", "empty body"}}).dump();
          } else {
            try {
              auto page = qbrain::ingest::capture_text(brain, body, "note");
              brain.enqueue_embed_page(page.id);
              response_body = json({{"ok", true}, {"slug", page.slug}, {"id", page.id}}).dump();
            } catch (const std::exception& e) {
              status = 400;
              response_body = json({{"ok", false}, {"error", e.what()}}).dump();
            }
          }
          break;
        }
        case HttpRoute::JsonRpc: {
          // N7/N30: Write default-deny — HTTP is a remote transport; the
          // registry centrally denies Write/Admin with no capability (allow_write
          // alone never authorizes remote mutation).
          ServeOptions http_opts = opts;
          http_opts.http_transport = true;
          response_body = handle_rpc_body(brain, http_opts, body,
                                          request_capability.empty()
                                              ? nullptr
                                              : &request_capability);
          if (response_body.empty()) response_body = R"({"jsonrpc":"2.0","result":{}})";
          break;
        }
        case HttpRoute::Health:
          response_body = R"({"ok":true,"service":"qbrain-http"})";
          break;
        case HttpRoute::Admin: {
          auto st = brain.stats();
          response_body =
              "<!doctype html><html><body><h1>Qbrain</h1><pre>pages=" + std::to_string(st.pages) +
              " chunks=" + std::to_string(st.chunks) + " links=" + std::to_string(st.links) +
              " embedded=" + std::to_string(st.embedded_chunks) + "</pre></body></html>";
          break;
        }
        case HttpRoute::MethodNotAllowed:
          status = 405;
          response_body = R"({"error":"method not allowed"})";
          break;
        case HttpRoute::NotFound:
        default:
          status = 404;
          response_body = R"({"error":"not found"})";
          break;
      }
    }
    const std::string ctype = (route == HttpRoute::Admin) ? "text/html; charset=utf-8" : "application/json";
    if (authenticated)
      std::cerr << "[qbrain-http] audit result=" << status << " identity=" << audit_identity << "\n";
    send_http_response(status, ctype, response_body);
    closesocket(client);
  }
  closesocket(listen_sock);
  WSACleanup();
  return 0;
#else
  (void)brain;
  (void)opts;
  (void)token;
  (void)port;
  return 2;
#endif
}

}  // namespace qbrain::mcp
