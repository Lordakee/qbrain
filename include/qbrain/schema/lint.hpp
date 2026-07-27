#pragma once
#include "qbrain/core/brain.hpp"
#include <string>
#include <vector>

namespace qbrain::schema {

struct LintIssue {
  std::string code;
  std::string slug;
  std::string detail;
};

struct GraphNode {
  std::string id;
  std::string kind;  // type | pack_type
  int64_t count = 0;
};

std::vector<LintIssue> schema_lint(Brain& brain, int limit = 100);
std::vector<GraphNode> schema_graph(Brain& brain);
std::string schema_explain_type(Brain& brain, const std::string& type);
std::vector<std::string> ontology_propose(Brain& brain, int limit = 20);
std::vector<LintIssue> ontology_conflicts(Brain& brain, int limit = 50);

}  // namespace qbrain::schema
