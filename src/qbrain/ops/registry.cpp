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

OpResult Registry::call(const std::string& name, OpContext& ctx) const {
  auto* op = find(name);
  if (!op) {
    OpResult r;
    r.ok = false;
    r.exit_code = 1;
    r.text = "unknown operation: " + name;
    return r;
  }
  if (op->local_only && ctx.remote && !ctx.allow_write) {
    OpResult r;
    r.ok = false;
    r.exit_code = 1;
    r.text =
        "operation is localOnly: " + name +
        " (MCP write denied; pass --allow-write or set QBRAIN_MCP_ALLOW_WRITE=1, or use CLI)";
    return r;
  }
  return op->handler(ctx);
}

Registry& global_registry() {
  static Registry r;
  return r;
}

}  // namespace qbrain::ops
