// N36: token-scoped authentication tests (single registration
// `n36_token_scope`). Unit groups: config parse matrix, token shape,
// constant-time compare, hash prefix, authenticate mapping; op-level groups:
// capability propagation through handle_rpc_body (read/write/admin), default
// (unconfigured) behavior identical to N30, registry-level admin semantics
// via an isolated Registry instance.
#include "qbrain/core/brain.hpp"
#include "qbrain/mcp/auth.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;

int n36_failures = 0;
#define N36_CHECK(cond)                                                              \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      std::printf("[FAIL] n36: CHECK failed: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
      ++n36_failures;                                                                \
    }                                                                                \
  } while (0)

struct N36ScopedEnv {
  explicit N36ScopedEnv(const std::string& name) : name_(name) {
#ifdef _WIN32
    if (const char* prev = std::getenv(name_.c_str())) {
      previous_ = prev;
      had_ = true;
    }
#endif
  }
  ~N36ScopedEnv() {
#ifdef _WIN32
    _putenv_s(name_.c_str(), had_ ? previous_.c_str() : "");
#endif
  }

 private:
  std::string name_;
  std::string previous_;
  bool had_ = false;
};

}  // namespace

void test_n36_token_scope() {
  const std::string tok_a = "alphatoken-0123456789abcdef";
  const std::string tok_b = "betatoken-0123456789abcdefgx";
  const std::string tok_c = "gammadtoken-23456789abcde";

  // ---- config parse matrix ----
  const std::string cfg = "alice:" + tok_a + ":read;bob:" + tok_b + ":write,admin;carol:" +
                          tok_c + ":admin;bad:short:read;bad2:" + std::string(300, 'x') +
                          ":read;bad3:token-0123456789abcd:superuser;bad4:oncolonextra:read";
  auto tokens = qbrain::mcp::parse_scoped_tokens(cfg);
  N36_CHECK(tokens.size() == 3);
  if (tokens.size() == 3) {
    N36_CHECK(tokens[0].name == "alice" && tokens[0].token == tok_a);
    N36_CHECK(tokens[0].scopes == static_cast<unsigned>(qbrain::mcp::TokenScope::read));
    N36_CHECK(tokens[1].name == "bob");
    N36_CHECK(tokens[1].scopes ==
              (static_cast<unsigned>(qbrain::mcp::TokenScope::write) |
               static_cast<unsigned>(qbrain::mcp::TokenScope::admin)));
    N36_CHECK(tokens[2].name == "carol");
  }

  // ---- token shape bounds ----
  N36_CHECK(!qbrain::mcp::token_shape_valid(std::string(15, 'a')));
  N36_CHECK(qbrain::mcp::token_shape_valid(std::string(16, 'a')));
  N36_CHECK(qbrain::mcp::token_shape_valid(std::string(256, 'a')));
  N36_CHECK(!qbrain::mcp::token_shape_valid(std::string(257, 'a')));
  N36_CHECK(!qbrain::mcp::token_shape_valid("valid-len-but-\x01-bytes!!"));
  N36_CHECK(!qbrain::mcp::token_shape_valid("valid-len-but space!!"));

  // ---- constant-time compare (behavioral; presence via direct use) ----
  N36_CHECK(qbrain::mcp::constant_time_equal(tok_a, tok_a));
  N36_CHECK(!qbrain::mcp::constant_time_equal(tok_a, tok_b));
  std::string near_a = tok_a;
  near_a[near_a.size() - 1] = near_a[near_a.size() - 1] == 'f' ? 'e' : 'f';
  N36_CHECK(near_a != tok_a);
  N36_CHECK(!qbrain::mcp::constant_time_equal(tok_a, near_a));
  N36_CHECK(!qbrain::mcp::constant_time_equal(tok_a, tok_a.substr(0, tok_a.size() - 1)));

  // ---- hash prefix ----
  const auto p1 = qbrain::mcp::audit_hash_prefix(tok_a);
  N36_CHECK(p1.size() == 16);
  for (char c : p1) N36_CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  N36_CHECK(qbrain::mcp::audit_hash_prefix(tok_a) == p1);
  N36_CHECK(qbrain::mcp::audit_hash_prefix(tok_b) != p1);
  N36_CHECK(qbrain::mcp::audit_hash_prefix(tok_a).find(tok_a) == std::string::npos);

  // ---- authenticate mapping ----
  auto a1 = qbrain::mcp::authenticate_bearer(tokens, tok_a);
  N36_CHECK(a1.has_value() && a1->capability.empty());
  auto a2 = qbrain::mcp::authenticate_bearer(tokens, tok_b);
  N36_CHECK(a2.has_value() && a2->capability == "admin");  // write,admin -> admin
  auto a3 = qbrain::mcp::authenticate_bearer(tokens, tok_c);
  N36_CHECK(a3.has_value() && a3->capability == "admin");
  N36_CHECK(!qbrain::mcp::authenticate_bearer(tokens, near_a).has_value());
  N36_CHECK(!qbrain::mcp::authenticate_bearer(tokens, "").has_value());
  N36_CHECK(!qbrain::mcp::authenticate_bearer({}, tok_a).has_value());
  N36_CHECK(a1->hash_prefix == p1);

  // ---- op-level capability propagation through handle_rpc_body ----
  N36ScopedEnv local_appdata("LOCALAPPDATA");
  const fs::path root =
      fs::temp_directory_path() / ("qbrain_n36_" + std::to_string(::GetCurrentProcessId()));
  fs::create_directories(root);
  qbrain::Brain brain("n36-token-scope");
  brain.open_at((root / "n36.db").string());

  qbrain::mcp::ServeOptions opts;
  const std::string rpc =
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_stats","arguments":{}}})";
  // put_page is local_only by the audited N1 decision; the remote-capability
  // positive probe uses a non-local-only Write op (put_raw_data).
  const std::string put =
      R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"put_raw_data","arguments":{"key":"n36-probe","content":"b"}}})";

  opts.http_transport = true;
  const std::string no_cap;
  auto read_none = qbrain::mcp::handle_rpc_body(brain, opts, rpc, &no_cap);
  N36_CHECK(read_none.find("\"isError\":false") != std::string::npos);
  auto write_none = qbrain::mcp::handle_rpc_body(brain, opts, put, &no_cap);
  N36_CHECK(write_none.find("write_denied") != std::string::npos);

  const std::string read_cap = "";
  auto read_ro = qbrain::mcp::handle_rpc_body(brain, opts, rpc, &read_cap);
  N36_CHECK(read_ro.find("\"isError\":false") != std::string::npos);
  auto write_ro = qbrain::mcp::handle_rpc_body(brain, opts, put, &read_cap);
  N36_CHECK(write_ro.find("write_denied") != std::string::npos);

  const std::string write_cap = "write";
  auto write_ok = qbrain::mcp::handle_rpc_body(brain, opts, put, &write_cap);
  N36_CHECK(write_ok.find("\"isError\":false") != std::string::npos);

  // default (nullptr) == no capability: unconfigured-env N30 behavior
  auto write_default = qbrain::mcp::handle_rpc_body(brain, opts, put);
  N36_CHECK(write_default.find("write_denied") != std::string::npos);

  // stdio path untouched by capabilities: allow_write gate semantics
  qbrain::mcp::ServeOptions stdio_opts;
  auto stdio_no_allow = qbrain::mcp::handle_rpc_body(brain, stdio_opts, put);
  N36_CHECK(stdio_no_allow.find("write_denied") != std::string::npos);
  stdio_opts.allow_write = true;
  auto stdio_allow = qbrain::mcp::handle_rpc_body(brain, stdio_opts, put);
  N36_CHECK(stdio_allow.find("\"isError\":false") != std::string::npos);

  // ---- registry-level admin semantics (isolated registry) ----
  // The only global Admin ops (purge_deleted_pages, code_traversal_cache_clear)
  // remain handler-hardened local-only per the audited N2.5/N22 decisions, so
  // admin scope is verified here at the registry authorization gate itself.
  qbrain::ops::Registry reg;
  qbrain::ops::Operation probe;
  probe.name = "n36_admin_probe";
  probe.scope = qbrain::ops::Scope::Admin;
  probe.handler = [](qbrain::ops::OpContext& ctx) {
    qbrain::ops::OpResult r;
    r.text = "admin-ok";
    return r;
  };
  bool added = reg.add(probe);
  N36_CHECK(added);
  qbrain::ops::OpContext ctx;
  ctx.brain = &brain;
  ctx.remote = true;
  const std::string admin_cap = "admin";
  ctx.authenticated_capability = &admin_cap;
  auto admin_ok = reg.call("n36_admin_probe", ctx);
  N36_CHECK(admin_ok.ok && admin_ok.text == "admin-ok");
  const std::string write_only = "write";
  ctx.authenticated_capability = &write_only;
  auto admin_write = reg.call("n36_admin_probe", ctx);
  N36_CHECK(!admin_write.ok);  // write must not grant admin
  ctx.authenticated_capability = nullptr;
  auto admin_none = reg.call("n36_admin_probe", ctx);
  N36_CHECK(!admin_none.ok);

  if (n36_failures != 0)
    throw std::runtime_error("n36: " + std::to_string(n36_failures) + " check(s) failed");
}
