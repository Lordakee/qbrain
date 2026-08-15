#pragma once
#include "qbrain/core/brain.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace qbrain::ops {

enum class Scope { Read, Write, Admin };

struct OpContext {
  Brain* brain = nullptr;
  // remote = network-exposed transport (HTTP). The central scope check treats
  // remote Write/Admin as never-writable without an authenticated capability.
  bool remote = false;
  // via_mcp = any MCP transport (stdio or HTTP). The audited N1 decision keeps
  // MCP write default-deny: stdio writes need the explicit --allow-write opt-in.
  bool via_mcp = false;
  bool allow_write = false;  // MCP --allow-write (local convenience switch only)
  // N30: explicit capability that may authorize remote Write/Admin scope.
  // --allow-write is not an identity and never authorizes remote mutation.
  // A transport may point this at a capability string only after verifying the
  // caller's local-machine identity; N36 replaces it with authenticated
  // token scopes ("write" permits Write; "admin" permits Admin and Write).
  const std::string* authenticated_capability = nullptr;
  std::unordered_map<std::string, std::string> args;
};

struct OpResult {
  bool ok = true;
  int exit_code = 0;
  std::string text;
  std::string json;
};

using OpHandler = std::function<OpResult(OpContext&)>;

struct Operation {
  std::string name;
  Scope scope = Scope::Read;
  bool local_only = false;
  std::string description;
  std::string input_schema_json;  // JSON Schema object as string
  OpHandler handler;
};

class Registry {
 public:
  void add(Operation op);
  const Operation* find(const std::string& name) const;
  std::vector<std::string> names() const;
  std::vector<const Operation*> list() const;
  OpResult call(const std::string& name, OpContext& ctx) const;

 private:
  std::unordered_map<std::string, Operation> ops_;
};

Registry& global_registry();
void register_builtin_ops();

}  // namespace qbrain::ops
