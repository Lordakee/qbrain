#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/ingest/import.hpp"
#include "qbrain/util/log.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
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

static bool check_auth(const std::string& headers, const std::string& token) {
  if (token.empty()) return false;
  auto pos = headers.find("Authorization:");
  if (pos == std::string::npos) pos = headers.find("authorization:");
  if (pos == std::string::npos) return false;
  auto line_end = headers.find("\r\n", pos);
  auto line = headers.substr(pos, line_end == std::string::npos ? std::string::npos : line_end - pos);
  auto bearer = line.find("Bearer ");
  if (bearer == std::string::npos) bearer = line.find("bearer ");
  if (bearer == std::string::npos) return false;
  auto got = line.substr(bearer + 7);
  while (!got.empty() && (got.back() == ' ' || got.back() == '\r')) got.pop_back();
  // constant-time-ish compare
  if (got.size() != token.size()) return false;
  volatile unsigned char diff = 0;
  for (size_t i = 0; i < got.size(); ++i) diff |= (unsigned char)(got[i] ^ token[i]);
  return diff == 0;
}

int run_http_server(Brain& brain, const ServeOptions& opts, const std::string& token, int port) {
#ifdef _WIN32
  if (token.empty()) {
    std::cerr << "[qbrain-http] token required (set QBRAIN_MCP_TOKEN env, not argv)\n";
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
    char buf[65536];
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
      closesocket(client);
      continue;
    }
    buf[n] = 0;
    std::string req(buf, n);
    auto hdr_end = req.find("\r\n\r\n");
    std::string headers = hdr_end == std::string::npos ? req : req.substr(0, hdr_end);
    std::string body = hdr_end == std::string::npos ? "" : req.substr(hdr_end + 4);
    std::string response_body;
    int status = 200;
    if (!check_auth(headers, token)) {
      status = 401;
      response_body = R"({"error":"unauthorized"})";
    } else if (req.find("POST /ingest") == 0 || req.find("POST /ingest ") != std::string::npos ||
               (req.find("POST") == 0 && headers.find("/ingest") != std::string::npos) ||
               (req.find("POST /ingest") != std::string::npos)) {
      // Markdown ingest webhook (gbrain-like): body is text/markdown or raw text
      if (!opts.allow_write) {
        // allow ingest when token present (auth already checked) — treat as write path
      }
      try {
        auto page = qbrain::ingest::capture_text(brain, body, "note");
        brain.enqueue_embed_page(page.id);
        response_body = json({{"ok", true}, {"slug", page.slug}, {"id", page.id}}).dump();
      } catch (const std::exception& e) {
        status = 400;
        response_body = json({{"ok", false}, {"error", e.what()}}).dump();
      }
    } else if (req.find("POST") == 0) {
      // Force allow_write for authenticated HTTP token holders on write tools
      ServeOptions o2 = opts;
      o2.allow_write = true;
      response_body = handle_rpc_body(brain, o2, body);
      if (response_body.empty()) response_body = R"({"jsonrpc":"2.0","result":{}})";
    } else if (req.find("GET /health") == 0) {
      response_body = R"({"ok":true,"service":"qbrain-http"})";
    } else if (req.find("GET /admin") == 0) {
      auto st = brain.stats();
      response_body =
          "<!doctype html><html><body><h1>Qbrain</h1><pre>pages=" + std::to_string(st.pages) +
          " chunks=" + std::to_string(st.chunks) + " links=" + std::to_string(st.links) +
          " embedded=" + std::to_string(st.embedded_chunks) + "</pre></body></html>";
    } else {
      status = 404;
      response_body = R"({"error":"not found"})";
    }
    std::string ctype = (req.find("GET /admin") == 0) ? "text/html; charset=utf-8" : "application/json";
    std::string resp = "HTTP/1.1 " + std::to_string(status) +
                       (status == 200 ? " OK" : status == 401 ? " Unauthorized" : " Error") +
                       "\r\nContent-Type: " + ctype + "\r\nContent-Length: " +
                       std::to_string(response_body.size()) + "\r\nConnection: close\r\n\r\n" +
                       response_body;
    send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
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
