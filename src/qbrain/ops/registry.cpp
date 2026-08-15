#include "qbrain/ops/registry.hpp"
#include <algorithm>

namespace qbrain::ops {

void Registry::add(Operation op) { ops_[op.name] = std::move(op); }

const Operation* Registry::find(const std::string& name) const {
  auto it = ops_.find(name);
  if (it == ops_.end()) return nullptr;
  return &it->second;
}

std::vector<std::string> Registry::names() const {
  std::vector<std::string> n;
  n.reserve(ops_.size());
  for (auto& [k, _] : ops_) n.push_back(k);
  std::sort(n.begin(), n.end());
  return n;
}

std::vector<const Operation*> Registry::list() const {
  std::vector<const Operation*> out;
  out.reserve(ops_.size());
  for (auto& [_, op] : ops_) out.push_back(&op);
  std::sort(out.begin(), out.end(),
            [](const Operation* a, const Operation* b) { return a->name < b->name; });
  return out;
}

namespace {

// N30 D3: minimal capability model. "write" authorizes Scope::Write, "admin"
// authorizes Scope::Admin (and Scope::Write). N36 replaces this with
// authenticated token scopes mapped per operation.
bool capability_permits(const std::string* capability, Scope scope) {
  if (!capability || capability->empty()) return false;
  if (*capability == "admin") return true;
  return *capability == "write" && scope == Scope::Write;
}

OpResult remote_write_denied() {
  OpResult r;
  r.ok = false;
  r.exit_code = 1;
  r.json =
      R"({"error":{"code":"write_denied","field":"operation","message":"remote write or admin operation requires an authenticated capability"}})";
  r.text = r.json;
  return r;
}

OpResult mcp_write_denied() {
  OpResult r;
  r.ok = false;
  r.exit_code = 1;
  r.json =
      R"({"error":{"code":"write_denied","field":"operation","message":"MCP write operations require the --allow-write opt-in"}})";
  r.text = r.json;
  return r;
}

}  // namespace

OpResult Registry::call(const std::string& name, OpContext& ctx) const {
  auto* op = find(name);
  if (!op) {
    OpResult r;
    r.ok = false;
    r.exit_code = 1;
    r.text = "unknown operation";
    r.json =
        R"({"error":{"code":"unknown_operation","field":"name","message":"unknown operation"}})";
    return r;
  }
  // N30 D3: central default-deny. The declared scope is enforced at this
  // single choke point for every caller: remote (network) Write/Admin requires
  // an explicit authenticated capability, and --allow-write never substitutes
  // for one. Read operations stay reachable remotely.
  if ((op->scope == Scope::Write || op->scope == Scope::Admin) && ctx.remote &&
      !capability_permits(ctx.authenticated_capability, op->scope)) {
    return remote_write_denied();
  }
  // Audited N1 decision preserved: every MCP transport (stdio included) keeps
  // write default-deny; --allow-write is the documented operator opt-in.
  if ((op->scope == Scope::Write || op->scope == Scope::Admin) && !ctx.remote &&
      ctx.via_mcp && !ctx.allow_write) {
    return mcp_write_denied();
  }
  // local_only operations never execute for remote callers, regardless of
  // flags or capabilities.
  if (op->local_only && ctx.remote) return remote_write_denied();
  return op->handler(ctx);
}

Registry& global_registry() {
  static Registry r;
  return r;
}

}  // namespace qbrain::ops
