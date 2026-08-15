#include "qbrain/core/brain.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
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

namespace {

json mcp_call(qbrain::Brain& b, const qbrain::mcp::ServeOptions& opts,
              const std::string& name) {
  auto req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":")" +
             name + R"(","arguments":{}}})";
  return json::parse(qbrain::mcp::handle_rpc_body(b, opts, req));
}

std::string first_content_text(const json& response) {
  return response["result"]["content"][0]["text"].get<std::string>();
}

}  // namespace

void test_doctor() {
  namespace fs = std::filesystem;
  qbrain::ops::register_builtin_ops();

  auto dir = fs::temp_directory_path() / "qbrain_doctor_test";
  fs::remove_all(dir);
  fs::create_directories(dir);

  qbrain::Brain healthy("doctor_test");
  healthy.open_at(qbrain::util::path_to_utf8(dir / "healthy.db"));

  qbrain::ops::OpContext ctx;
  ctx.brain = &healthy;
  ctx.remote = true;
  ctx.allow_write = false;
  auto direct = qbrain::ops::global_registry().call("run_doctor", ctx);
  QB_CHECK(direct.ok);
  auto direct_json = json::parse(direct.json);
  QB_CHECK(direct_json["overall"] == "OK");
  QB_CHECK(direct_json["schema_version"].get<int>() >= 11);
  QB_CHECK(direct_json["checks"].is_array());

  qbrain::mcp::ServeOptions opts;
  opts.allow_write = false;
  auto list_resp = json::parse(qbrain::mcp::handle_rpc_body(
      healthy, opts, R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})"));
  bool saw_run_doctor = false;
  for (auto& tool : list_resp["result"]["tools"]) {
    if (tool.value("name", "") == "run_doctor") saw_run_doctor = true;
  }
  QB_CHECK(saw_run_doctor);

  auto mcp_doctor = mcp_call(healthy, opts, "run_doctor");
  QB_CHECK(mcp_doctor["result"]["isError"] == false);
  auto doctor_json = json::parse(first_content_text(mcp_doctor));
  QB_CHECK(doctor_json["overall"] == "OK");

  auto remediate_deny = mcp_call(healthy, opts, "doctor_remediate");
  QB_CHECK(remediate_deny["result"]["isError"] == true);

  qbrain::Brain broken_table("doctor_test_broken_table");
  broken_table.open_at(qbrain::util::path_to_utf8(dir / "broken_table.db"));
  broken_table.db().exec("DROP TABLE content_chunks;");
  auto broken_health = broken_table.health();
  QB_CHECK(!broken_health.ok);
  bool saw_missing_table = false;
  for (auto& note : broken_health.notes) {
    if (note.find("content_chunks") != std::string::npos ||
        note.find("stats unavailable") != std::string::npos) {
      saw_missing_table = true;
    }
  }
  QB_CHECK(saw_missing_table);

  qbrain::ops::OpContext broken_ctx;
  broken_ctx.brain = &broken_table;
  broken_ctx.remote = true;
  broken_ctx.allow_write = false;
  auto broken_doc = qbrain::ops::global_registry().call("run_doctor", broken_ctx);
  QB_CHECK(!broken_doc.ok);
  QB_CHECK(json::parse(broken_doc.json)["overall"] == "FAIL");

  qbrain::Brain missing_schema("doctor_test_missing_schema");
  missing_schema.open_at(qbrain::util::path_to_utf8(dir / "missing_schema.db"));
  missing_schema.db().exec("DROP TABLE schema_version;");
  auto missing_health = missing_schema.health();
  QB_CHECK(!missing_health.ok);
  bool saw_schema_version = false;
  for (auto& note : missing_health.notes) {
    if (note.find("schema_version") != std::string::npos) saw_schema_version = true;
  }
  QB_CHECK(saw_schema_version);

  healthy.close();
  broken_table.close();
  missing_schema.close();
  fs::remove_all(dir);
}
