// tests/test_n30.cpp — N30 security/build/storage closure tests.
// Sections are owned by parallel subagents; keep them separated:
//   // --- n30-c: routing/build/storage ---  (subagent C: D5/D7/D8/D9)
//   // --- n30-b: auth/redaction ---        (subagent B: D3/D4/D6; appends below)

#include "qbrain/core/brain.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/schema/packs.hpp"
#include "qbrain/storage/database.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/paths.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

using json = nlohmann::json;

#define QB_CHECK(cond)                                                          \
  do {                                                                          \
    if (!(cond)) {                                                              \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond + " @ " +  \
                               __FILE__ + ":" + std::to_string(__LINE__));      \
    }                                                                           \
  } while (0)

// --- n30-c: routing/build/storage ---

namespace {

qbrain::mcp::HttpRequestLine parse_ok(const std::string& line) {
  qbrain::mcp::HttpRequestLine parsed;
  if (!qbrain::mcp::parse_http_request_line(line, parsed)) {
    throw std::runtime_error("expected parse success: " + line);
  }
  return parsed;
}

bool parse_fails(const std::string& line) {
  qbrain::mcp::HttpRequestLine parsed;
  return !qbrain::mcp::parse_http_request_line(line, parsed);
}

#ifdef _WIN32

class WinsockLifetime {
 public:
  WinsockLifetime() {
    WSADATA data{};
    started_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
  ~WinsockLifetime() {
    if (started_) WSACleanup();
  }
  bool started() const { return started_; }

 private:
  bool started_ = false;
};

unsigned short find_free_loopback_port() {
  SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (probe == INVALID_SOCKET) throw std::runtime_error("probe socket failed");
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(probe, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    closesocket(probe);
    throw std::runtime_error("probe bind failed");
  }
  int size = sizeof(addr);
  if (getsockname(probe, reinterpret_cast<sockaddr*>(&addr), &size) == SOCKET_ERROR) {
    closesocket(probe);
    throw std::runtime_error("getsockname failed");
  }
  const unsigned short port = ntohs(addr.sin_port);
  closesocket(probe);
  return port;
}

std::string http_roundtrip(const std::string& peer, unsigned short port,
                           const std::string& raw_request) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) throw std::runtime_error("client socket failed");
  DWORD timeout = 5000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, peer.c_str(), &addr.sin_addr);
  if (connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    closesocket(s);
    throw std::runtime_error("connect failed");
  }
  size_t sent = 0;
  while (sent < raw_request.size()) {
    const int n = ::send(s, raw_request.data() + sent,
                         static_cast<int>(raw_request.size() - sent), 0);
    if (n <= 0) {
      closesocket(s);
      throw std::runtime_error("send failed");
    }
    sent += static_cast<size_t>(n);
  }
  std::string response;
  char buf[4096];
  for (;;) {
    const int n = recv(s, buf, sizeof(buf), 0);
    if (n <= 0) break;
    response.append(buf, static_cast<size_t>(n));
  }
  closesocket(s);
  return response;
}

bool wait_for_listener(unsigned short port) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (probe == INVALID_SOCKET) return false;
    DWORD timeout = 200;
    setsockopt(probe, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    const int rc = connect(probe, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    closesocket(probe);
    if (rc == 0) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

#endif  // _WIN32

class ScopedLocalAppData {
 public:
  explicit ScopedLocalAppData(const std::string& value) : name_("LOCALAPPDATA") {
    if (const char* previous = std::getenv(name_.c_str())) previous_ = previous;
    if (_putenv_s(name_.c_str(), value.c_str()) != 0) {
      throw std::runtime_error("failed to set LOCALAPPDATA");
    }
  }
  ~ScopedLocalAppData() {
    _putenv_s(name_.c_str(), previous_ ? previous_->c_str() : "");
  }
  ScopedLocalAppData(const ScopedLocalAppData&) = delete;
  ScopedLocalAppData& operator=(const ScopedLocalAppData&) = delete;

 private:
  std::string name_;
  std::optional<std::string> previous_;
};

std::string read_file_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("fixture read failed: " + path.string());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

void test_n30_c_routing_storage() {
  namespace fs = std::filesystem;
  qbrain::ops::register_builtin_ops();

  // ---- D5 unit: exact request-line parsing ----
  {
    auto r = parse_ok("POST /ingest HTTP/1.1");
    QB_CHECK(r.method == "POST" && r.path == "/ingest" && r.query.empty() &&
             r.version == "HTTP/1.1");
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::Ingest);

    r = parse_ok("POST /ingest?a=b&c=d HTTP/1.1");
    QB_CHECK(r.path == "/ingest" && r.query == "a=b&c=d");
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::Ingest);

    r = parse_ok("GET /health HTTP/1.1");
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::Health);

    r = parse_ok("GET /admin HTTP/1.0");
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::Admin);

    r = parse_ok("POST / HTTP/1.1");
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::JsonRpc);

    // Prefix/suffix confusables must NOT route to /ingest.
    r = parse_ok("POST /ingestx HTTP/1.1");
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::NotFound);
    r = parse_ok("POST /ingest/ HTTP/1.1");
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::NotFound);
    r = parse_ok("POST /xingest HTTP/1.1");
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::NotFound);
    r = parse_ok("GET /healthy HTTP/1.1");
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::NotFound);

    // Known path, unsupported method.
    r = parse_ok("GET /ingest HTTP/1.1");
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::MethodNotAllowed);
    r = parse_ok("POST /health HTTP/1.1");
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::MethodNotAllowed);
    r = parse_ok("get /health HTTP/1.1");  // methods are case-sensitive
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::MethodNotAllowed);
    r = parse_ok("DELETE /admin HTTP/1.1");
    QB_CHECK(qbrain::mcp::route_http_request(r) == qbrain::mcp::HttpRoute::MethodNotAllowed);

    // Malformed request lines.
    QB_CHECK(parse_fails(""));
    QB_CHECK(parse_fails("GET"));
    QB_CHECK(parse_fails("GET "));
    QB_CHECK(parse_fails("GET /health"));
    QB_CHECK(parse_fails("GET  /health HTTP/1.1"));         // double space
    QB_CHECK(parse_fails("GET /health  HTTP/1.1"));         // double space
    QB_CHECK(parse_fails("GET\t/health HTTP/1.1"));         // tab separator
    QB_CHECK(parse_fails("P@ST / HTTP/1.1"));               // invalid method token
    QB_CHECK(parse_fails("GET /hea lth HTTP/1.1"));         // space inside target
    QB_CHECK(parse_fails("GET /health HTTP/1.11"));         // bad version
    QB_CHECK(parse_fails("GET /health HTTP/x.1"));          // bad version
    QB_CHECK(parse_fails("GET /health HTTP/1"));            // bad version
    QB_CHECK(parse_fails("POST /ingest#frag HTTP/1.1"));    // fragment in target
    QB_CHECK(parse_fails("CONNECT peer.example HTTP/1.1")); // authority form
    QB_CHECK(parse_fails("GET /\x01 HTTP/1.1"));            // control byte
  }

#ifdef _WIN32
  // ---- D5 end-to-end: loopback negatives over the real socket path ----
  {
    WinsockLifetime winsock;
    QB_CHECK(winsock.started());

    const fs::path dir = fs::temp_directory_path() / "qbrain_n30_c_http";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // The server loop only exits when accept() fails, so the brain/options
    // are intentionally leaked to keep the detached thread valid for the
    // remainder of the test process lifetime.
    auto* server_brain = new qbrain::Brain("n30_c_http");
    server_brain->open_at(qbrain::util::path_to_utf8(dir / "brain.db"));
    auto* server_opts = new qbrain::mcp::ServeOptions();
    server_opts->allow_write = false;
    auto* server_token = new std::string("n30-c-test-token");
    const unsigned short port = find_free_loopback_port();
    std::thread([](qbrain::Brain* b, qbrain::mcp::ServeOptions* o, std::string* t,
                   unsigned short p) {
      (void)qbrain::mcp::run_http_server(*b, *o, *t, static_cast<int>(p));
    }, server_brain, server_opts, server_token, port).detach();
    QB_CHECK(wait_for_listener(port));

    const std::string auth = "Authorization: Bearer " + *server_token + "\r\n";

    // /ingestx is NOT routed as /ingest.
    auto resp = http_roundtrip("127.0.0.1", port,
                               "POST /ingestx HTTP/1.1\r\nHost: x\r\n" + auth +
                                   "Content-Length: 5\r\n\r\nhello");
    QB_CHECK(resp.find(" 404 ") != std::string::npos);
    QB_CHECK(resp.find("not found") != std::string::npos);
    QB_CHECK(resp.find("\"ok\":true") == std::string::npos);
    QB_CHECK(server_brain->stats().pages == 0);

    // Unsupported method on a known path.
    resp = http_roundtrip("127.0.0.1", port,
                          "get /health HTTP/1.1\r\nHost: x\r\n" + auth + "\r\n");
    QB_CHECK(resp.find(" 405 ") != std::string::npos);
    QB_CHECK(resp.find("method not allowed") != std::string::npos);

    // Unknown GET path.
    resp = http_roundtrip("127.0.0.1", port,
                          "GET /healthy HTTP/1.1\r\nHost: x\r\n" + auth + "\r\n");
    QB_CHECK(resp.find(" 404 ") != std::string::npos);

    // Malformed request line (invalid method token).
    resp = http_roundtrip("127.0.0.1", port,
                          "P@ST / HTTP/1.1\r\nHost: x\r\n" + auth +
                              "Content-Length: 2\r\n\r\n{}");
    QB_CHECK(resp.find(" 400 ") != std::string::npos);
    QB_CHECK(resp.find("malformed request line") != std::string::npos);

    // Duplicate Content-Length.
    resp = http_roundtrip("127.0.0.1", port,
                          "POST / HTTP/1.1\r\nHost: x\r\n" + auth +
                              "Content-Length: 2\r\nContent-Length: 3\r\n\r\n{}");
    QB_CHECK(resp.find(" 400 ") != std::string::npos);
    QB_CHECK(resp.find("invalid content-length") != std::string::npos);

    // Invalid Content-Length.
    resp = http_roundtrip("127.0.0.1", port,
                          "POST / HTTP/1.1\r\nHost: x\r\n" + auth +
                              "Content-Length: 12abc\r\n\r\n{}");
    QB_CHECK(resp.find(" 400 ") != std::string::npos);
    QB_CHECK(resp.find("invalid content-length") != std::string::npos);

    // Oversized declared body (over the 16 MiB request cap).
    resp = http_roundtrip("127.0.0.1", port,
                          "POST / HTTP/1.1\r\nHost: x\r\n" + auth +
                              "Content-Length: 99999999\r\n\r\n");
    QB_CHECK(resp.find(" 400 ") != std::string::npos);

    // Sanity: exact routing keeps the supported paths working.
    resp = http_roundtrip("127.0.0.1", port,
                          "GET /health HTTP/1.1\r\nHost: x\r\n" + auth + "\r\n");
    QB_CHECK(resp.find(" 200 ") != std::string::npos);
    QB_CHECK(resp.find("qbrain-http") != std::string::npos);

    resp = http_roundtrip("127.0.0.1", port,
                          "POST / HTTP/1.1\r\nHost: x\r\n" + auth +
                              "Content-Length: 39\r\n\r\n"
                          R"({"jsonrpc":"2.0","id":7,"method":"ping"})");
    QB_CHECK(resp.find(" 200 ") != std::string::npos);
    QB_CHECK(resp.find("jsonrpc") != std::string::npos);

    // server_brain intentionally not closed (detached server thread owns it).
  }
#endif  // _WIN32

  // ---- D8: schema corruption fixtures — doctor fails closed per object ----
  {
    const fs::path dir = fs::temp_directory_path() / "qbrain_n30_c_schema";
    fs::remove_all(dir);
    fs::create_directories(dir);

    struct Corruption {
      const char* label;
      const char* sql;
      const char* expect_missing;
    };
    // Representative set covering every post-v4 table (page_versions, facts,
    // takes, file_index, raw_data), the FTS virtual table, indexes, and
    // required columns.
    const Corruption cases[] = {
        {"drop_page_versions", "DROP TABLE page_versions;", "page_versions"},
        {"drop_facts", "DROP TABLE facts;", "facts"},
        {"drop_takes", "DROP TABLE takes;", "takes"},
        {"drop_file_index", "DROP TABLE file_index;", "file_index"},
        {"drop_raw_data", "DROP TABLE raw_data;", "raw_data"},
        {"drop_fts", "DROP TABLE pages_fts;", "pages_fts"},
        {"drop_idx_facts_entity", "DROP INDEX idx_facts_entity;", "idx_facts_entity"},
        {"drop_idx_raw_data_key", "DROP INDEX idx_raw_data_key;", "idx_raw_data_key"},
        {"drop_idx_takes_body", "DROP INDEX idx_takes_body;", "idx_takes_body"},
        {"drop_col_pages_source_kind", "ALTER TABLE pages DROP COLUMN source_kind;",
         "pages.source_kind"},
        {"drop_col_file_index_path", "ALTER TABLE file_index DROP COLUMN path;",
         "file_index.path"},
    };

    int index = 0;
    for (const auto& corruption : cases) {
      qbrain::Brain brain("n30_c_schema");
      brain.open_at(qbrain::util::path_to_utf8(
          dir / (std::string(corruption.label) + ".db")));
      QB_CHECK(qbrain::storage::check_schema_integrity(brain.db()).ok);
      brain.db().exec(corruption.sql);

      const auto integrity = qbrain::storage::check_schema_integrity(brain.db());
      QB_CHECK(!integrity.ok);
      bool saw_object = false;
      for (const auto& note : integrity.missing) {
        if (note.find(corruption.expect_missing) != std::string::npos) saw_object = true;
      }
      QB_CHECK(saw_object);

      qbrain::ops::OpContext ctx;
      ctx.brain = &brain;
      ctx.remote = true;
      ctx.allow_write = false;
      auto doctor = qbrain::ops::global_registry().call("run_doctor", ctx);
      QB_CHECK(!doctor.ok);
      QB_CHECK(json::parse(doctor.json)["overall"] == "FAIL");

      brain.close();
      ++index;
    }
    QB_CHECK(index == 11);
    fs::remove_all(dir);
  }

  // ---- D9: pack atomicity — failed mutation leaves the pack untouched ----
  {
    const fs::path root = fs::temp_directory_path() / "qbrain_n30_c_packs";
    fs::remove_all(root);
    fs::create_directories(root);
    ScopedLocalAppData local_app_data(qbrain::util::path_to_utf8(root));

    const fs::path db_dir = root / "db";
    fs::create_directories(db_dir);
    qbrain::Brain brain("n30_c_packs");
    brain.open_at(qbrain::util::path_to_utf8(db_dir / "brain.db"));

    qbrain::schema::ensure_default_pack();
    const fs::path pack_path =
        qbrain::util::qbrain_root() / "schema-packs" / "default.json";
    QB_CHECK(fs::exists(pack_path));

    // Successful mutation writes through the atomic path.
    int applied = 0;
    auto err = qbrain::schema::apply_mutations(
        brain, R"([{"op":"add_type","type":"n30_probe"}])", &applied);
    QB_CHECK(err.empty());
    QB_CHECK(applied == 1);
    QB_CHECK(read_file_bytes(pack_path).find("n30_probe") != std::string::npos);

    // Inject a write failure: a read-only target cannot be replaced, so the
    // atomic rename fails and the original bytes must survive byte-for-byte.
    const auto before_hash =
        qbrain::util::sha256_hex(read_file_bytes(pack_path));
    const auto before_bytes = read_file_bytes(pack_path);
    fs::permissions(pack_path, fs::perms::owner_read | fs::perms::group_read |
                                   fs::perms::others_read);
    applied = 0;
    err = qbrain::schema::apply_mutations(
        brain, R"([{"op":"add_type","type":"n30_probe2"}])", &applied);
    QB_CHECK(!err.empty());
    QB_CHECK(applied == 0);
    QB_CHECK(qbrain::util::sha256_hex(read_file_bytes(pack_path)) == before_hash);
    QB_CHECK(read_file_bytes(pack_path) == before_bytes);
    QB_CHECK(read_file_bytes(pack_path).find("n30_probe2") == std::string::npos);

    // No staged temp sibling may survive the failed mutation.
    for (const auto& entry : fs::directory_iterator(pack_path.parent_path())) {
      const auto name = entry.path().filename().string();
      QB_CHECK(name.find(".tmp-") == std::string::npos);
    }

    // Restore writability; a retry must now succeed and update the pack.
    fs::permissions(pack_path, fs::perms::owner_all);
    applied = 0;
    err = qbrain::schema::apply_mutations(
        brain, R"([{"op":"add_type","type":"n30_probe2"}])", &applied);
    QB_CHECK(err.empty());
    QB_CHECK(applied == 1);
    QB_CHECK(read_file_bytes(pack_path).find("n30_probe2") != std::string::npos);
    QB_CHECK(qbrain::util::sha256_hex(read_file_bytes(pack_path)) != before_hash);

    brain.close();
    fs::remove_all(root);
  }
}

// (subagent B appends n30-b auth/redaction sections below this line)

// --- n30-b: auth/redaction ---

#include "qbrain/files/store.hpp"
#include <algorithm>
#include <optional>
#include <set>
#include <vector>

namespace qbrain::cli {
// N30 D6 (subagent B): defined in src/qbrain/cli/commands.cpp. Declared here
// so the unit suite can exercise the CLI port parser directly; the CLI helper
// intentionally has no public header.
bool parse_port_value(const std::string& text, int& out);
}

namespace {

class N30bScopedEnv {
 public:
  N30bScopedEnv(const char* name, const std::string& value) : name_(name) {
    if (const char* previous = std::getenv(name)) previous_ = previous;
    if (_putenv_s(name, value.c_str()) != 0) {
      throw std::runtime_error("failed to set environment variable");
    }
  }
  ~N30bScopedEnv() {
    _putenv_s(name_, previous_ ? previous_->c_str() : "");
  }
  N30bScopedEnv(const N30bScopedEnv&) = delete;
  N30bScopedEnv& operator=(const N30bScopedEnv&) = delete;

 private:
  const char* name_;
  std::optional<std::string> previous_;
};

// A remote-facing response must not disclose local absolute paths, the
// redirected %LOCALAPPDATA% root, or file:/// URLs.
bool n30b_discloses_local_paths(const std::string& text, const std::string& local_root,
                                const std::string& db_path) {
  if (text.find("file:///") != std::string::npos) return true;
  if (text.find("%LOCALAPPDATA%") != std::string::npos) return true;
  if (!local_root.empty() && text.find(local_root) != std::string::npos) return true;
  if (!db_path.empty() && text.find(db_path) != std::string::npos) return true;
  for (char c = 'A'; c <= 'Z'; ++c) {
    const std::string drive_back = std::string(1, c) + ":\\";
    const std::string drive_fwd = std::string(1, c) + ":/";
    if (text.find(drive_back) != std::string::npos) return true;
    if (text.find(drive_fwd) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

void test_n30_b_auth_redaction() {
  namespace fs = std::filesystem;
  qbrain::ops::register_builtin_ops();

  const fs::path dir = fs::temp_directory_path() / "qbrain_n30_b";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
  N30bScopedEnv local_app_data("LOCALAPPDATA", local_root);

  qbrain::Brain brain("n30_b_auth");
  brain.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));
  const std::string db_path = brain.db_path();

  // ---- D3: enumerate every registered Write/Admin operation ----
  std::vector<std::string> mutating;
  for (const auto* op : qbrain::ops::global_registry().list()) {
    if (op->scope == qbrain::ops::Scope::Write ||
        op->scope == qbrain::ops::Scope::Admin) {
      mutating.push_back(op->name);
    }
  }
  std::sort(mutating.begin(), mutating.end());
  QB_CHECK(!mutating.empty());

  // The plan's minimum negative-matrix coverage set must be registered and
  // classified as Write/Admin (checked programmatically, not hardcoded-only).
  const std::set<std::string> required = {
      "put_page",           "delete_page",        "restore_page",
      "purge_deleted_pages", "file_upload",       "put_raw_data",
      "submit_agent",       "doctor_remediate",   "schema_apply_mutations",
      "takes_calibration",  "chronicle_backfill", "log_ingest",
      "add_timeline_entry", "reload_schema_pack",
  };
  for (const auto& name : required) {
    QB_CHECK(std::find(mutating.begin(), mutating.end(), name) != mutating.end());
  }

  // ---- D3: negative matrix — remote without capability is denied, and
  // --allow-write never authorizes remote Write/Admin ----
  int denied_count = 0;
  for (const auto& name : mutating) {
    for (bool allow_write : {false, true}) {
      qbrain::ops::OpContext ctx;
      ctx.brain = &brain;
      ctx.remote = true;
      ctx.allow_write = allow_write;
      auto r = qbrain::ops::global_registry().call(name, ctx);
      QB_CHECK(!r.ok);
      QB_CHECK(r.exit_code != 0);
      const auto payload = json::parse(r.json);
      QB_CHECK(payload.contains("error"));
      QB_CHECK(payload["error"]["code"] == "write_denied");
      QB_CHECK(payload["error"]["field"] == "operation");
      QB_CHECK(payload["error"]["message"].is_string());
      QB_CHECK(!n30b_discloses_local_paths(r.json + r.text, local_root, db_path));
      ++denied_count;
    }
  }
  QB_CHECK(denied_count == 2 * static_cast<int>(mutating.size()));

  // ---- D3: capability model ----
  const std::string capability_write = "write";
  const std::string capability_admin = "admin";
  {
    // "write" permits a non-local-only Write op to reach its handler remotely.
    qbrain::ops::OpContext ctx;
    ctx.brain = &brain;
    ctx.remote = true;
    ctx.authenticated_capability = &capability_write;
    ctx.args = {{"prompt", "n30 capability probe"}};
    auto r = qbrain::ops::global_registry().call("submit_agent", ctx);
    QB_CHECK(r.ok);
  }
  {
    // local_only operations stay local-only even with a capability, and
    // --allow-write adds nothing on top of that.
    qbrain::ops::OpContext ctx;
    ctx.brain = &brain;
    ctx.remote = true;
    ctx.allow_write = true;
    ctx.authenticated_capability = &capability_write;
    auto r = qbrain::ops::global_registry().call("put_page", ctx);
    QB_CHECK(!r.ok);
    r = qbrain::ops::global_registry().call("purge_deleted_pages", ctx);
    QB_CHECK(!r.ok);
    ctx.authenticated_capability = &capability_admin;
    r = qbrain::ops::global_registry().call("purge_deleted_pages", ctx);
    QB_CHECK(!r.ok);
  }
  {
    // Local callers are unaffected by the remote default-deny.
    qbrain::ops::OpContext ctx;
    ctx.brain = &brain;
    ctx.args = {{"slug", "n30-b-local"}, {"body", "local write still works"}};
    auto r = qbrain::ops::global_registry().call("put_page", ctx);
    QB_CHECK(r.ok);
  }

  // ---- D4: remote path disclosure redaction ----
  const fs::path src = dir / "n30b_note.txt";
  {
    std::ofstream out(src, std::ios::binary);
    out << "n30b attachment body\n";
  }
  const auto file_id = qbrain::files::upload(
      brain, qbrain::util::path_to_utf8(src), "n30b_note.txt");
  QB_CHECK(file_id > 0);

  {
    // get_health: local callers keep the db path, remote callers do not.
    // Note: health() reports the brain's canonical data-root path, not the
    // open_at() override used by this fixture.
    const std::string health_db_path =
        qbrain::util::path_to_utf8(qbrain::util::brain_db_path("n30_b_auth"));
    qbrain::ops::OpContext ctx;
    ctx.brain = &brain;
    auto local = qbrain::ops::global_registry().call("get_health", ctx);
    QB_CHECK(local.ok);
    const auto local_payload = json::parse(local.json);
    QB_CHECK(local_payload.contains("db_path"));
    QB_CHECK(local_payload["db_path"] == health_db_path);
    QB_CHECK(local.text.find(health_db_path) != std::string::npos);

    ctx.remote = true;
    auto remote = qbrain::ops::global_registry().call("get_health", ctx);
    QB_CHECK(remote.ok);
    const auto remote_payload = json::parse(remote.json);
    QB_CHECK(!remote_payload.contains("db_path"));
    QB_CHECK(!n30b_discloses_local_paths(remote.json + remote.text, local_root,
                                         health_db_path));
  }
  {
    // file_list: remote rows carry the stored name as a relative identifier.
    qbrain::ops::OpContext ctx;
    ctx.brain = &brain;
    auto local = qbrain::ops::global_registry().call("file_list", ctx);
    QB_CHECK(local.ok);
    const auto local_rows = json::parse(local.json);
    QB_CHECK(local_rows.is_array() && local_rows.size() == 1);
    QB_CHECK(local_rows[0]["path"].get<std::string>().find("n30b_note.txt") !=
             std::string::npos);
    QB_CHECK(local_rows[0]["path"].get<std::string>().find(":") != std::string::npos);

    ctx.remote = true;
    auto remote = qbrain::ops::global_registry().call("file_list", ctx);
    QB_CHECK(remote.ok);
    const auto remote_rows = json::parse(remote.json);
    QB_CHECK(remote_rows.is_array() && remote_rows.size() == 1);
    QB_CHECK(remote_rows[0]["path"] == remote_rows[0]["name"]);
    QB_CHECK(remote_rows[0]["name"] == "n30b_note.txt");
    QB_CHECK(!n30b_discloses_local_paths(remote.json + remote.text, local_root, db_path));
  }
  {
    // file_url: remote callers get identifiers, never a file:/// URL.
    qbrain::ops::OpContext ctx;
    ctx.brain = &brain;
    ctx.args = {{"id", std::to_string(file_id)}};
    auto local = qbrain::ops::global_registry().call("file_url", ctx);
    QB_CHECK(local.ok);
    QB_CHECK(local.text.find("file:///") != std::string::npos);
    QB_CHECK(local.text.find("n30b_note.txt") != std::string::npos);

    ctx.remote = true;
    auto remote = qbrain::ops::global_registry().call("file_url", ctx);
    QB_CHECK(remote.ok);
    const auto remote_payload = json::parse(remote.json);
    QB_CHECK(!remote_payload.contains("url"));
    QB_CHECK(remote_payload.contains("id"));
    QB_CHECK(remote_payload["id"] == file_id);
    QB_CHECK(!n30b_discloses_local_paths(remote.json + remote.text, local_root, db_path));
  }

  brain.close();
  fs::remove_all(dir);

  // ---- D6: CLI port option parsing (helper in src/qbrain/cli/commands.cpp) ----
  {
    int port = 0;
    QB_CHECK(qbrain::cli::parse_port_value("1", port) && port == 1);
    QB_CHECK(qbrain::cli::parse_port_value("80", port) && port == 80);
    QB_CHECK(qbrain::cli::parse_port_value("7420", port) && port == 7420);
    QB_CHECK(qbrain::cli::parse_port_value("65535", port) && port == 65535);
    QB_CHECK(qbrain::cli::parse_port_value("0080", port) && port == 80);
    for (const std::string bad :
         {"", "abc", "99999", "-1", "0", "65536", "1.5", " 80", "80 ", "+80",
          "0x50", "80,", "2147483648", "--1"}) {
      QB_CHECK(!qbrain::cli::parse_port_value(bad, port));
    }
  }
}
