#include "qbrain/ai/http_client.hpp"
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace qbrain::ai {

#ifdef _WIN32
static std::wstring to_wide(std::string_view s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

static bool parse_url(std::string_view base, std::wstring& host, INTERNET_PORT& port, bool& https,
                      std::wstring& path_prefix) {
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  uc.dwSchemeLength = static_cast<DWORD>(-1);
  uc.dwHostNameLength = static_cast<DWORD>(-1);
  uc.dwUrlPathLength = static_cast<DWORD>(-1);
  std::wstring wbase = to_wide(base);
  if (!WinHttpCrackUrl(wbase.c_str(), 0, 0, &uc)) return false;
  https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
  host.assign(uc.lpszHostName, uc.dwHostNameLength);
  port = uc.nPort;
  path_prefix.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
  if (path_prefix.empty()) path_prefix = L"/";
  // strip trailing slash for join
  if (path_prefix.size() > 1 && path_prefix.back() == L'/') path_prefix.pop_back();
  return true;
}
#endif

HttpResponse http_post_json(std::string_view base_url, std::string_view path,
                            std::string_view bearer_token, std::string_view json_body,
                            int timeout_ms) {
  HttpResponse resp;
#ifdef _WIN32
  std::wstring host, path_prefix;
  INTERNET_PORT port = 0;
  bool https = true;
  if (!parse_url(base_url, host, port, https, path_prefix)) {
    resp.error = "invalid base_url";
    return resp;
  }
  std::wstring full_path = path_prefix;
  std::string p(path);
  if (!p.empty() && p[0] != '/') full_path.push_back(L'/');
  full_path += to_wide(p);

  HINTERNET session = WinHttpOpen(L"Qbrain/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    resp.error = "WinHttpOpen failed";
    return resp;
  }
  WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
  HINTERNET conn = WinHttpConnect(session, host.c_str(), port, 0);
  if (!conn) {
    resp.error = "WinHttpConnect failed";
    WinHttpCloseHandle(session);
    return resp;
  }
  DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET req =
      WinHttpOpenRequest(conn, L"POST", full_path.c_str(), nullptr, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!req) {
    resp.error = "WinHttpOpenRequest failed";
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return resp;
  }

  std::wstring headers = L"Content-Type: application/json\r\n";
  if (!bearer_token.empty()) {
    headers += L"Authorization: Bearer ";
    headers += to_wide(bearer_token);
    headers += L"\r\n";
  }

  BOOL ok = WinHttpSendRequest(req, headers.c_str(), static_cast<DWORD>(-1),
                               (LPVOID)json_body.data(), static_cast<DWORD>(json_body.size()),
                               static_cast<DWORD>(json_body.size()), 0);
  if (!ok || !WinHttpReceiveResponse(req, nullptr)) {
    resp.error = "HTTP request failed";
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return resp;
  }

  DWORD status = 0, sz = sizeof(status);
  WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
  resp.status = static_cast<int>(status);

  std::string body;
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req, &avail)) break;
    if (avail == 0) break;
    std::vector<char> buf(avail);
    DWORD read = 0;
    if (!WinHttpReadData(req, buf.data(), avail, &read)) break;
    body.append(buf.data(), read);
  }
  resp.body = std::move(body);
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(conn);
  WinHttpCloseHandle(session);
  if (resp.status < 200 || resp.status >= 300) {
    resp.error = "HTTP " + std::to_string(resp.status) + ": " + resp.body.substr(0, 300);
  }
#else
  (void)base_url;
  (void)path;
  (void)bearer_token;
  (void)json_body;
  (void)timeout_ms;
  resp.error = "HTTP client only implemented on Windows";
#endif
  return resp;
}

}  // namespace qbrain::ai
