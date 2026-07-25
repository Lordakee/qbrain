#include "qbrain/mcp/jsonrpc.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/core/brain.hpp"
#include "qbrain/util/paths.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

void test_mcp() {
  // framing (NDJSON: JSON + newline)
  auto framed = qbrain::mcp::make_framed(R"({"jsonrpc":"2.0","id":1})");
  QB_CHECK(!framed.empty() && framed.back() == '\n');
  size_t consumed = 0;
  auto body = qbrain::mcp::parse_framed_buffer(framed, &consumed);
  QB_CHECK(body.has_value());
  QB_CHECK(body->find("jsonrpc") != std::string::npos);

  // temp brain + ops
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / "qbrain_mcp_test";
  fs::create_directories(dir);
  auto dbp = dir / "brain.db";
  fs::remove(dbp);

  qbrain::ops::register_builtin_ops();
  qbrain::Brain b("mcp_test");
  b.open_at(qbrain::util::path_to_utf8(dbp));

  qbrain::PageInput in;
  in.slug = "notes/hello";
  in.title = "Hello";
  in.body = "MCP test note about Alice";
  b.put_page(in);

  qbrain::mcp::ServeOptions opts;
  opts.allow_write = false;

  // initialize
  auto init_req =
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"0"}}})";
  auto init_resp = qbrain::mcp::handle_rpc_body(b, opts, init_req);
  auto ij = json::parse(init_resp);
  QB_CHECK(ij["result"]["serverInfo"]["name"] == "qbrain");
  QB_CHECK(ij["result"].contains("capabilities"));

  // tools/list
  auto list_req = R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})";
  auto list_resp = qbrain::mcp::handle_rpc_body(b, opts, list_req);
  auto lj = json::parse(list_resp);
  QB_CHECK(lj["result"]["tools"].is_array());
  QB_CHECK(lj["result"]["tools"].size() >= 5);

  // search
  auto search_req =
      R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"search","arguments":{"query":"Alice","no_vector":true}}})";
  auto search_resp = qbrain::mcp::handle_rpc_body(b, opts, search_req);
  auto sj = json::parse(search_resp);
  QB_CHECK(sj["result"]["isError"] == false);
  QB_CHECK(sj["result"]["content"][0]["text"].get<std::string>().find("Alice") !=
           std::string::npos);

  // put denied without allow_write
  auto put_req =
      R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"capture","arguments":{"text":"should fail"}}})";
  auto put_resp = qbrain::mcp::handle_rpc_body(b, opts, put_req);
  auto pj = json::parse(put_resp);
  QB_CHECK(pj["result"]["isError"] == true);

  // put allowed with allow_write
  opts.allow_write = true;
  auto put_ok_req =
      R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"capture","arguments":{"text":"mcp write ok"}}})";
  auto put_ok_resp = qbrain::mcp::handle_rpc_body(b, opts, put_ok_req);
  auto poj = json::parse(put_ok_resp);
  QB_CHECK(poj["result"]["isError"] == false);

  // ping
  auto ping_req = R"({"jsonrpc":"2.0","id":6,"method":"ping"})";
  auto ping_resp = qbrain::mcp::handle_rpc_body(b, opts, ping_req);
  auto pingj = json::parse(ping_resp);
  QB_CHECK(pingj.contains("result"));

  b.close();
  fs::remove_all(dir);
}
