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
  bool remote = false;
  bool allow_write = false;  // MCP --allow-write
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
