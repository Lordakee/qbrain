#include "qbrain/mcp/jsonrpc.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/core/brain.hpp"
#include "qbrain/util/paths.hpp"
#include <filesystem>
#include <fstream>
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

  // N9 fixture skill under brain-local skills path
  auto skill_dir = qbrain::util::brain_dir("mcp_test") / "skills" / "n9_fixture";
  fs::create_directories(skill_dir);
  {
    std::ofstream sk(skill_dir / "SKILL.md", std::ios::binary);
    sk << "# n9_fixture\nN9_FIXTURE_MARKER_OK\n";
  }

  qbrain::PageInput in;
  in.slug = "notes/hello";
  in.title = "Hello";
  in.body = "MCP test note about Alice [[other-page]]";
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

  // N2: delete_page denied without allow_write
  auto del_deny =
      R"({"jsonrpc":"2.0","id":40,"method":"tools/call","params":{"name":"delete_page","arguments":{"slug":"notes/hello"}}})";
  auto del_deny_resp = qbrain::mcp::handle_rpc_body(b, opts, del_deny);
  auto ddj = json::parse(del_deny_resp);
  QB_CHECK(ddj["result"]["isError"] == true);

  // N2: get_backlinks / get_versions under write deny
  auto bl_req =
      R"({"jsonrpc":"2.0","id":41,"method":"tools/call","params":{"name":"get_backlinks","arguments":{"slug":"notes/hello"}}})";
  auto bl_resp = qbrain::mcp::handle_rpc_body(b, opts, bl_req);
  auto blj = json::parse(bl_resp);
  QB_CHECK(blj["result"]["isError"] == false);

  auto gv_req =
      R"({"jsonrpc":"2.0","id":42,"method":"tools/call","params":{"name":"get_versions","arguments":{"slug":"notes/hello"}}})";
  auto gv_resp = qbrain::mcp::handle_rpc_body(b, opts, gv_req);
  auto gvj = json::parse(gv_resp);
  QB_CHECK(gvj["result"]["isError"] == false);

  // N2: purge remote/localOnly always denied when remote MCP
  auto purge_req =
      R"({"jsonrpc":"2.0","id":43,"method":"tools/call","params":{"name":"purge_deleted_pages","arguments":{}}})";
  auto purge_resp = qbrain::mcp::handle_rpc_body(b, opts, purge_req);
  auto prj = json::parse(purge_resp);
  QB_CHECK(prj["result"]["isError"] == true);

  // N9: list_skills / get_skill under write deny; path traversal rejected
  auto ls_req =
      R"({"jsonrpc":"2.0","id":44,"method":"tools/call","params":{"name":"list_skills","arguments":{}}})";
  auto ls_resp = qbrain::mcp::handle_rpc_body(b, opts, ls_req);
  auto lsj = json::parse(ls_resp);
  QB_CHECK(lsj["result"]["isError"] == false);
  auto ls_text = lsj["result"]["content"][0]["text"].get<std::string>();
  QB_CHECK(ls_text.find("n9_fixture") != std::string::npos);

  auto gs_req =
      R"({"jsonrpc":"2.0","id":440,"method":"tools/call","params":{"name":"get_skill","arguments":{"name":"n9_fixture"}}})";
  auto gs_resp = qbrain::mcp::handle_rpc_body(b, opts, gs_req);
  auto gsj = json::parse(gs_resp);
  QB_CHECK(gsj["result"]["isError"] == false);
  auto gs_text = gsj["result"]["content"][0]["text"].get<std::string>();
  QB_CHECK(gs_text.find("N9_FIXTURE_MARKER_OK") != std::string::npos);

  auto bad_skill =
      R"({"jsonrpc":"2.0","id":45,"method":"tools/call","params":{"name":"get_skill","arguments":{"name":"../etc"}}})";
  auto bad_skill_resp = qbrain::mcp::handle_rpc_body(b, opts, bad_skill);
  auto bsj = json::parse(bad_skill_resp);
  QB_CHECK(bsj["result"]["isError"] == true);

  // N4: think save without allow_write does not create pages
  auto pages_before = b.stats().pages;
  auto think_req =
      R"({"jsonrpc":"2.0","id":46,"method":"tools/call","params":{"name":"think","arguments":{"question":"n4 test","save":"1"}}})";
  auto think_resp = qbrain::mcp::handle_rpc_body(b, opts, think_req);
  auto thj = json::parse(think_resp);
  QB_CHECK(thj.contains("result"));
  QB_CHECK(b.stats().pages == pages_before);

  // N2.5: remote sources_add always denied (even with allow_write)
  opts.allow_write = true;
  auto sa_req =
      R"({"jsonrpc":"2.0","id":50,"method":"tools/call","params":{"name":"sources_add","arguments":{"id":"evil"}}})";
  auto sa_resp = qbrain::mcp::handle_rpc_body(b, opts, sa_req);
  auto saj = json::parse(sa_resp);
  QB_CHECK(saj["result"]["isError"] == true);

  // N2.5: remote put_page non-default source without allowlist denied
  auto put_bad =
      R"({"jsonrpc":"2.0","id":51,"method":"tools/call","params":{"name":"put_page","arguments":{"slug":"x","body":"y","source_id":"other"}}})";
  auto put_bad_resp = qbrain::mcp::handle_rpc_body(b, opts, put_bad);
  auto pbj = json::parse(put_bad_resp);
  QB_CHECK(pbj["result"]["isError"] == true);

  // N2.5: capture non-default without allowlist denied
  auto cap_bad =
      R"({"jsonrpc":"2.0","id":52,"method":"tools/call","params":{"name":"capture","arguments":{"text":"hi","source_id":"other"}}})";
  auto cap_bad_resp = qbrain::mcp::handle_rpc_body(b, opts, cap_bad);
  auto cbj = json::parse(cap_bad_resp);
  QB_CHECK(cbj["result"]["isError"] == true);

  // N5: empty capture text rejected
  auto cap_empty =
      R"({"jsonrpc":"2.0","id":53,"method":"tools/call","params":{"name":"capture","arguments":{"text":""}}})";
  auto cap_empty_resp = qbrain::mcp::handle_rpc_body(b, opts, cap_empty);
  auto cej = json::parse(cap_empty_resp);
  QB_CHECK(cej["result"]["isError"] == true);

  opts.allow_write = false;

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
