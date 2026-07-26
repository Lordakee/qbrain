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

  // N12 MCP round-trip: submit_job → list_jobs → get_job → cancel_job → run_dream
  opts.allow_write = true;
  auto submit_req =
      R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"submit_job","arguments":{"type":"embed","payload_json":"{\"page_id\":1}"}}})";
  auto submit_resp = qbrain::mcp::handle_rpc_body(b, opts, submit_req);
  auto subj = json::parse(submit_resp);
  QB_CHECK(subj["result"]["isError"] == false);
  auto sub_text = subj["result"]["content"][0]["text"].get<std::string>();
  QB_CHECK(sub_text.find("job") != std::string::npos);

  auto list_jobs_req =
      R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"list_jobs","arguments":{"status":"waiting"}}})";
  auto list_jobs_resp = qbrain::mcp::handle_rpc_body(b, opts, list_jobs_req);
  auto ljj = json::parse(list_jobs_resp);
  QB_CHECK(ljj["result"]["isError"] == false);
  auto list_text = ljj["result"]["content"][0]["text"].get<std::string>();
  QB_CHECK(list_text.find("embed") != std::string::npos || list_text.find("waiting") != std::string::npos ||
           list_text.find("\"id\"") != std::string::npos);

  // parse job id from submit json if present in text
  int64_t jid = 0;
  try {
    // tools/call returns text; submit handler puts json in text field too when set
    auto pos = sub_text.find_first_of("0123456789");
    if (pos != std::string::npos) jid = std::stoll(sub_text.substr(pos));
  } catch (...) {
  }
  if (jid > 0) {
    auto get_req = std::string(
                       R"({"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"get_job","arguments":{"id":")") +
                   std::to_string(jid) + R"("}}})";
    auto get_resp = qbrain::mcp::handle_rpc_body(b, opts, get_req);
    auto gj = json::parse(get_resp);
    QB_CHECK(gj["result"]["isError"] == false);

    auto cancel_req = std::string(
                          R"({"jsonrpc":"2.0","id":13,"method":"tools/call","params":{"name":"cancel_job","arguments":{"id":")") +
                      std::to_string(jid) + R"("}}})";
    auto cancel_resp = qbrain::mcp::handle_rpc_body(b, opts, cancel_req);
    auto cj = json::parse(cancel_resp);
    QB_CHECK(cj["result"]["isError"] == false);
  }

  auto dream_req =
      R"({"jsonrpc":"2.0","id":14,"method":"tools/call","params":{"name":"run_dream","arguments":{"apply":false}}})";
  auto dream_resp = qbrain::mcp::handle_rpc_body(b, opts, dream_req);
  auto dj = json::parse(dream_resp);
  QB_CHECK(dj["result"]["isError"] == false);
  auto dtext = dj["result"]["content"][0]["text"].get<std::string>();
  QB_CHECK(dtext.find("orphans") != std::string::npos || dtext.find("dry") != std::string::npos ||
           dtext.find("phase") != std::string::npos);

  b.close();
  fs::remove_all(dir);
}
