#include "qbrain/graph/analytics.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/util/paths.hpp"
#include "wave3_test_support.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#define QB_CHECK(cond)                                                               \
  do {                                                                               \
    if (!(cond)) throw std::runtime_error(std::string("CHECK failed: ") + #cond); \
  } while (0)

namespace {

using qbrain::test_support::logical_snapshot;
using qbrain::test_support::snapshot_sha256;

class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(std::string name, const std::string& value)
      : name_(std::move(name)) {
    if (const char* previous = std::getenv(name_.c_str())) previous_ = previous;
    if (_putenv_s(name_.c_str(), value.c_str()) != 0) {
      throw std::runtime_error("failed to set test environment variable");
    }
  }

  ~ScopedEnvironmentVariable() {
    _putenv_s(name_.c_str(), previous_ ? previous_->c_str() : "");
  }

  ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
  ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

 private:
  std::string name_;
  std::optional<std::string> previous_;
};

class AnalyticsReadObserver {
 public:
  explicit AnalyticsReadObserver(qbrain::Brain& brain) : database_(brain.db().handle()) {
    QB_CHECK(sqlite3_set_authorizer(database_, &AnalyticsReadObserver::authorize, this) ==
             SQLITE_OK);
  }

  ~AnalyticsReadObserver() { sqlite3_set_authorizer(database_, nullptr, nullptr); }

  AnalyticsReadObserver(const AnalyticsReadObserver&) = delete;
  AnalyticsReadObserver& operator=(const AnalyticsReadObserver&) = delete;

  int reads() const { return reads_; }
  int user_reads() const { return user_reads_; }

 private:
  static int authorize(void* context, int action, const char* argument1, const char*,
                       const char*, const char*) {
    if (action == SQLITE_READ && argument1) {
      const std::string_view table(argument1);
      if (!table.starts_with("sqlite_")) {
        ++static_cast<AnalyticsReadObserver*>(context)->user_reads_;
      }
      if (table == "pages" || table == "links" || table == "facts") {
        ++static_cast<AnalyticsReadObserver*>(context)->reads_;
      }
    }
    return SQLITE_OK;
  }

  sqlite3* database_ = nullptr;
  int reads_ = 0;
  int user_reads_ = 0;
};

void add_link(qbrain::Brain& brain, const std::string& source_id, const std::string& from,
              const std::string& to, const std::string& link_type = "wiki",
              const std::string& link_source = "manual") {
  qbrain::Link link;
  link.source_id = source_id;
  link.from_slug = from;
  link.to_slug = to;
  link.link_type = link_type;
  link.link_source = link_source;
  brain.add_link(link);
}

bool has_row(const nlohmann::json& rows, const std::string& kind, const std::string& slug) {
  return std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
    return row.value("kind", "") == kind && row.value("slug", "") == slug;
  });
}

void require_bounded_utf8_details(const nlohmann::json& rows) {
  for (const auto& row : rows) {
    const auto detail = row.value("detail", "");
    QB_CHECK(detail.size() <= 512);
    // nlohmann validates UTF-8 while serializing this string.
    QB_CHECK(!nlohmann::json({{"detail", detail}}).dump().empty());
  }
}

nlohmann::json require_mcp_structured_error(const nlohmann::json& response) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(response["result"].value("isError", false));
  for (const auto& content : response["result"]["content"]) {
    if (content.value("type", "") != "text" || !content.contains("text") ||
        !content["text"].is_string()) {
      continue;
    }
    try {
      auto parsed = nlohmann::json::parse(content["text"].get<std::string>());
      if (parsed.contains("error") && parsed["error"].is_object()) return parsed["error"];
    } catch (const nlohmann::json::parse_error&) {
    }
  }
  throw std::runtime_error("MCP response did not contain a structured operation error");
}

nlohmann::json require_operation_error(const qbrain::ops::OpResult& result,
                                       const std::string& code,
                                       const std::string& field) {
  QB_CHECK(!result.ok);
  QB_CHECK(result.exit_code != 0);
  auto error = nlohmann::json::parse(result.json)["error"];
  QB_CHECK(error["code"] == code);
  QB_CHECK(error["field"] == field);
  return error;
}

void require_source_rows(const qbrain::ops::OpResult& result, const std::string& source_id,
                         size_t expected_size) {
  QB_CHECK(result.ok);
  auto rows = nlohmann::json::parse(result.json);
  QB_CHECK(rows.size() == expected_size);
  QB_CHECK(std::all_of(rows.begin(), rows.end(), [&](const auto& row) {
    return row.value("source_id", "") == source_id;
  }));
}

struct SnapshotCallEvidence {
  size_t index = 0;
  std::string selected_before;
  std::string selected_after;
  std::string decoy_before;
  std::string decoy_after;
};

std::vector<SnapshotCallEvidence> g_snapshot_call_evidence;

template <typename Callable>
auto call_without_mutation(qbrain::Brain& selected, qbrain::Brain& decoy,
                           Callable&& callable) {
  const auto selected_before = logical_snapshot(selected);
  const auto decoy_before = logical_snapshot(decoy);
  const auto selected_before_hash = snapshot_sha256(selected_before);
  const auto decoy_before_hash = snapshot_sha256(decoy_before);
  auto result = callable();
  const auto selected_after = logical_snapshot(selected);
  const auto decoy_after = logical_snapshot(decoy);
  const auto selected_after_hash = snapshot_sha256(selected_after);
  const auto decoy_after_hash = snapshot_sha256(decoy_after);
  QB_CHECK(selected_after == selected_before);
  QB_CHECK(decoy_after == decoy_before);
  QB_CHECK(selected_after_hash == selected_before_hash);
  QB_CHECK(decoy_after_hash == decoy_before_hash);
  g_snapshot_call_evidence.push_back(
      {g_snapshot_call_evidence.size() + 1, selected_before_hash, selected_after_hash,
       decoy_before_hash, decoy_after_hash});
  return result;
}

nlohmann::json mcp_tool_call(qbrain::Brain& brain,
                             const qbrain::mcp::ServeOptions& options, int id,
                             const std::string& name, const nlohmann::json& arguments) {
  nlohmann::json request = {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"method", "tools/call"},
      {"params", {{"name", name}, {"arguments", arguments}}}};
  return nlohmann::json::parse(
      qbrain::mcp::handle_rpc_body(brain, options, request.dump()));
}

nlohmann::json require_mcp_success_rows(const nlohmann::json& response) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(!response["result"].value("isError", true));
  for (auto it = response["result"]["content"].rbegin();
       it != response["result"]["content"].rend(); ++it) {
    if (it->value("type", "") != "text" || !it->contains("text") ||
        !(*it)["text"].is_string()) {
      continue;
    }
    try {
      auto parsed = nlohmann::json::parse((*it)["text"].get<std::string>());
      if (parsed.is_array()) return parsed;
    } catch (const nlohmann::json::parse_error&) {
    }
  }
  throw std::runtime_error("MCP success response did not contain a JSON row array");
}

void exercise_limit_and_source_contract(const std::filesystem::path& database_path) {
  qbrain::Brain brain("n18-limits");
  brain.open_at(qbrain::util::path_to_utf8(database_path));
  QB_CHECK(brain.ensure_source("team_a"));
  QB_CHECK(brain.ensure_source("team_b"));

  brain.db().exec("BEGIN;");
  try {
    qbrain::test_support::put_page(brain, "default", "limit/anomaly-origin", "origin");
    for (int index = 0; index < 205; ++index) {
      add_link(brain, "default", "limit/anomaly-origin",
               "limit/missing-" + std::to_string(index), "wiki",
               "limit-" + std::to_string(index));
    }

    auto fact_owner =
        qbrain::test_support::put_page(brain, "default", "limit/fact-owner", "facts");
    for (int index = 0; index < 205; ++index) {
      const auto entity = "limit/entity-" + std::to_string(index);
      brain.add_fact(entity, "titled", "first", fact_owner.id);
      brain.add_fact(entity, "titled", "second", fact_owner.id);
    }

    qbrain::test_support::put_page(brain, "default", "limit/expert-origin", "origin");
    for (int index = 0; index < 205; ++index) {
      const auto target = "limit/expert-" + std::to_string(index);
      qbrain::test_support::put_page(brain, "default", target, "target");
      add_link(brain, "default", "limit/expert-origin", target, "expert",
               "limit-" + std::to_string(index));
    }

    auto team_owner =
        qbrain::test_support::put_page(brain, "team_a", "limit/team-owner", "team");
    brain.add_fact("limit/team-entity", "titled", "first", team_owner.id);
    brain.add_fact("limit/team-entity", "titled", "second", team_owner.id);
    add_link(brain, "team_a", "limit/team-owner", "limit/team-missing");
    qbrain::test_support::put_page(brain, "team_a", "limit/team-expert", "expert");
    add_link(brain, "team_a", "limit/team-owner", "limit/team-expert", "expert");
    brain.db().exec("COMMIT;");
  } catch (...) {
    brain.db().exec("ROLLBACK;");
    throw;
  }

  struct OperationLimit {
    const char* name;
    size_t default_size;
  };
  const std::vector<OperationLimit> operations = {
      {"find_anomalies", 100},
      {"find_contradictions", 100},
      {"find_experts", 50},
  };

  for (const auto& operation : operations) {
    const auto default_before = logical_snapshot(brain);
    require_source_rows(qbrain::test_support::call_op(brain, operation.name), "default",
                        operation.default_size);
    QB_CHECK(logical_snapshot(brain) == default_before);

    for (const auto& [limit, expected_size] :
         std::vector<std::pair<std::string, size_t>>{{"1", 1},
                                                     {"200", 200},
                                                     {"201", 200},
                                                     {"999", 200},
                                                     {"18446744073709551615", 200}}) {
      const auto before = logical_snapshot(brain);
      require_source_rows(
          qbrain::test_support::call_op(brain, operation.name, {{"limit", limit}}),
          "default", expected_size);
      QB_CHECK(logical_snapshot(brain) == before);
    }

    {
      const auto before = logical_snapshot(brain);
      qbrain::ops::OpResult result;
      int analytics_reads = -1;
      {
        AnalyticsReadObserver observer(brain);
        result = qbrain::test_support::call_op(brain, operation.name, {{"limit", "0"}});
        analytics_reads = observer.reads();
      }
      require_source_rows(result, "default", 0);
      QB_CHECK(analytics_reads == 0);
      QB_CHECK(logical_snapshot(brain) == before);
    }

    for (const auto& malformed : {"", "+1", "-1", " 1", "1 ", "1.0", "1junk",
                                  "18446744073709551616"}) {
      const auto before = logical_snapshot(brain);
      qbrain::ops::OpResult result;
      int analytics_reads = -1;
      int user_reads = -1;
      {
        AnalyticsReadObserver observer(brain);
        result = qbrain::test_support::call_op(brain, operation.name,
                                               {{"limit", malformed}});
        analytics_reads = observer.reads();
        user_reads = observer.user_reads();
      }
      require_operation_error(result, "invalid_argument", "limit");
      QB_CHECK(analytics_reads == 0);
      QB_CHECK(user_reads == 0);
      QB_CHECK(logical_snapshot(brain) == before);
    }

    const auto team_before = logical_snapshot(brain);
    auto team_result = qbrain::test_support::call_op(
        brain, operation.name, {{"source_id", "Team_A"}, {"limit", "1"}});
    require_source_rows(team_result, "team_a", 1);
    QB_CHECK(logical_snapshot(brain) == team_before);
  }

  const std::vector<std::pair<std::string, std::string>> invalid_sources = {
      {"", "invalid_source"},
      {"bad/slash", "invalid_source"},
      {"CON", "invalid_source"},
      {"NUL", "invalid_source"},
      {"PRN", "invalid_source"},
      {std::string(65, 'a'), "invalid_source"},
      {"valid_but_unknown", "source_not_found"},
  };
  const auto source_count =
      qbrain::test_support::scalar(brain, "SELECT COUNT(*) FROM sources");
  for (const auto& operation : operations) {
    for (const auto& [source_id, code] : invalid_sources) {
      const auto before = logical_snapshot(brain);
      qbrain::ops::OpResult result;
      int analytics_reads = -1;
      {
        AnalyticsReadObserver observer(brain);
        result = qbrain::test_support::call_op(brain, operation.name,
                                               {{"source_id", source_id}});
        analytics_reads = observer.reads();
      }
      require_operation_error(result, code, "source_id");
      QB_CHECK(result.text.find(source_id) == std::string::npos || source_id.empty());
      QB_CHECK(result.json.find(source_id) == std::string::npos || source_id.empty());
      QB_CHECK(analytics_reads == 0);
      QB_CHECK(logical_snapshot(brain) == before);
      QB_CHECK(qbrain::test_support::scalar(brain, "SELECT COUNT(*) FROM sources") ==
               source_count);
    }
  }

  const auto remote_default_before = logical_snapshot(brain);
  require_source_rows(qbrain::test_support::call_op(
                          brain, "find_anomalies", {{"limit", "1"}}, true, false),
                      "default", 1);
  QB_CHECK(logical_snapshot(brain) == remote_default_before);

  for (bool allow_write : {false, true}) {
    const auto before = logical_snapshot(brain);
    auto denied = qbrain::test_support::call_op(
        brain, "find_experts", {{"source_id", "team_a"}}, true, allow_write);
    require_operation_error(denied, "source_not_allowed", "source_id");
    QB_CHECK(logical_snapshot(brain) == before);
  }

  brain.save_config_value("mcp.allowed_sources", "Team_A");
  const auto allowlisted_before = logical_snapshot(brain);
  require_source_rows(qbrain::test_support::call_op(
                          brain, "find_experts", {{"source_id", "TEAM_A"}, {"limit", "1"}},
                          true, false),
                      "team_a", 1);
  auto second_source_denied = qbrain::test_support::call_op(
      brain, "find_anomalies", {{"source_id", "team_b"}}, true, true);
  require_operation_error(second_source_denied, "source_not_allowed", "source_id");
  QB_CHECK(logical_snapshot(brain) == allowlisted_before);

  brain.close();
}

int count_rows(const nlohmann::json& rows, const std::string& kind,
               const std::string& slug) {
  return static_cast<int>(std::count_if(rows.begin(), rows.end(), [&](const auto& row) {
    return row.value("kind", "") == kind && row.value("slug", "") == slug;
  }));
}

std::pair<std::string, std::string> contradiction_rule_output(
    const std::filesystem::path& database_path, bool reverse_insertion) {
  qbrain::Brain brain("n18-contradiction-rules");
  brain.open_at(qbrain::util::path_to_utf8(database_path));
  QB_CHECK(brain.ensure_source("team_a"));

  const auto owner =
      qbrain::test_support::put_page(brain, "default", "rules/default-owner", "facts");
  const auto team_owner =
      qbrain::test_support::put_page(brain, "team_a", "rules/team-owner", "facts");
  const auto deleted_owner =
      qbrain::test_support::put_page(brain, "default", "rules/deleted-owner", "facts");

  struct FactInput {
    std::string entity;
    std::string predicate;
    std::string object;
    int64_t page_id;
  };
  std::vector<FactInput> facts;
  const std::array<std::pair<const char*, const char*>, 14> opposing_pairs = {{
      {"is", "is_not"},
      {"is", "isnt"},
      {"is", "isn't"},
      {"supports", "opposes"},
      {"likes", "dislikes"},
      {"has", "lacks"},
      {"titled", "not_titled"},
      {"titled", "untitled"},
      {"true", "false"},
      {"yes", "no"},
      {"works_at", "left"},
      {"employed_by", "former_employee_of"},
      {"located_in", "not_located_in"},
      {"member_of", "not_member_of"},
  }};
  std::vector<std::string> positive_entities;
  for (size_t index = 0; index < opposing_pairs.size(); ++index) {
    const std::string entity =
        "rules/pair-" + std::string(index < 10 ? "0" : "") + std::to_string(index);
    positive_entities.push_back(entity);
    facts.push_back({entity, opposing_pairs[index].first, "left", owner.id});
    facts.push_back({entity, opposing_pairs[index].second, "right", owner.id});
  }
  const std::array<const char*, 3> prefixes = {"not_", "no_", "anti_"};
  for (size_t index = 0; index < prefixes.size(); ++index) {
    const std::string entity = "rules/prefix-" + std::to_string(index);
    positive_entities.push_back(entity);
    facts.push_back({entity, std::string(prefixes[index]) + "exact_base", "negated", owner.id});
    facts.push_back({entity, "exact_base", "base", owner.id});
  }

  facts.push_back({"rules/same-predicate", "TITLED", "alpha", owner.id});
  facts.push_back({"rules/same-predicate", "titled", "beta", owner.id});
  facts.push_back({"rules/both-kinds", "titled", "alpha", owner.id});
  facts.push_back({"rules/both-kinds", "TITLED", "beta", owner.id});
  facts.push_back({"rules/both-kinds", "not_titled", "gamma", owner.id});

  for (int duplicate = 0; duplicate < 2; ++duplicate) {
    facts.push_back({"rules/duplicate", "titled", "alpha", owner.id});
    facts.push_back({"rules/duplicate", "titled", "beta", owner.id});
  }

  facts.push_back({"rules/identical", "titled", "same", owner.id});
  facts.push_back({"rules/identical", "TITLED", "same", owner.id});
  facts.push_back({"rules/unsupported", "supports", "left", owner.id});
  facts.push_back({"rules/unsupported", "likes", "right", owner.id});
  facts.push_back({"rules/double-prefix", "not_not_x", "left", owner.id});
  facts.push_back({"rules/double-prefix", "not_x", "right", owner.id});
  facts.push_back({"rules/mixed-prefix", "not_x", "left", owner.id});
  facts.push_back({"rules/mixed-prefix", "no_x", "right", owner.id});
  facts.push_back({"rules/nonexact-base", "not_exact_base", "left", owner.id});
  facts.push_back({"rules/nonexact-base", "exact", "right", owner.id});

  const std::string escaped_object = "quote\" slash\\ tab\t line\n first";
  facts.push_back({"rules/escaping", "titled", escaped_object, owner.id});
  facts.push_back({"rules/escaping", "titled", "second", owner.id});
  std::string multibyte_object;
  for (int index = 0; index < 200; ++index) multibyte_object += "\xE4\xB8\xAD";
  facts.push_back({"rules/truncation", "titled", multibyte_object, owner.id});
  facts.push_back({"rules/truncation", "titled", "short", owner.id});

  facts.push_back({"rules/inactive", "titled", "left", owner.id});
  facts.push_back({"rules/inactive", "titled", "right", owner.id});
  facts.push_back({"rules/deleted-owner", "titled", "left", deleted_owner.id});
  facts.push_back({"rules/deleted-owner", "titled", "right", deleted_owner.id});
  facts.push_back({"rules/team-owner", "titled", "left", team_owner.id});
  facts.push_back({"rules/team-owner", "titled", "right", team_owner.id});
  facts.push_back({"rules/cross-source", "titled", "default", owner.id});
  facts.push_back({"rules/cross-source", "titled", "team", team_owner.id});
  facts.push_back({"rules/null-owner", "titled", "left", 0});
  facts.push_back({"rules/null-owner", "titled", "right", 0});
  facts.push_back({"rules/dangling-owner", "titled", "left", 999999});
  facts.push_back({"rules/dangling-owner", "titled", "right", 999999});

  if (reverse_insertion) std::reverse(facts.begin(), facts.end());
  brain.db().exec("BEGIN;");
  try {
    for (const auto& fact : facts) {
      brain.add_fact(fact.entity, fact.predicate, fact.object, fact.page_id);
    }
    brain.db().exec("UPDATE facts SET active=0 WHERE entity_slug='rules/inactive'");
    brain.db().exec("COMMIT;");
  } catch (...) {
    brain.db().exec("ROLLBACK;");
    throw;
  }
  QB_CHECK(brain.soft_delete(deleted_owner.slug));

  const auto before = logical_snapshot(brain);
  auto result = qbrain::test_support::call_op(
      brain, "find_contradictions", {{"source_id", "default"}, {"limit", "200"}});
  QB_CHECK(result.ok);
  QB_CHECK(logical_snapshot(brain) == before);
  auto repeated = qbrain::test_support::call_op(
      brain, "find_contradictions", {{"source_id", "default"}, {"limit", "200"}});
  QB_CHECK(repeated.ok && repeated.json == result.json && repeated.text == result.text);
  QB_CHECK(logical_snapshot(brain) == before);

  const auto rows = nlohmann::json::parse(result.json);
  for (const auto& entity : positive_entities) {
    QB_CHECK(count_rows(rows, "conflicting_predicates", entity) == 1);
  }
  QB_CHECK(count_rows(rows, "same_predicate_different_object", "rules/same-predicate") == 1);
  QB_CHECK(count_rows(rows, "same_predicate_different_object", "rules/duplicate") == 1);
  for (const auto& excluded : {"rules/identical", "rules/unsupported", "rules/double-prefix",
                               "rules/mixed-prefix", "rules/nonexact-base", "rules/inactive",
                               "rules/deleted-owner", "rules/team-owner", "rules/cross-source",
                               "rules/null-owner", "rules/dangling-owner"}) {
    QB_CHECK(std::none_of(rows.begin(), rows.end(), [&](const auto& row) {
      return row.value("slug", "") == excluded;
    }));
  }

  std::string previous_slug;
  bool conflict_seen_for_slug = false;
  for (const auto& row : rows) {
    QB_CHECK(row.is_object() && row.size() == 4);
    QB_CHECK(row.contains("source_id") && row.contains("kind") && row.contains("slug") &&
             row.contains("detail"));
    QB_CHECK(row["source_id"] == "default");
    const auto slug = row["slug"].get<std::string>();
    QB_CHECK(previous_slug.empty() || previous_slug <= slug);
    if (slug != previous_slug) conflict_seen_for_slug = false;
    if (row["kind"] == "conflicting_predicates") conflict_seen_for_slug = true;
    if (row["kind"] == "same_predicate_different_object") {
      QB_CHECK(!conflict_seen_for_slug);
    }
    previous_slug = slug;
  }

  const auto escaping = std::find_if(rows.begin(), rows.end(), [](const auto& row) {
    return row.value("slug", "") == "rules/escaping";
  });
  QB_CHECK(escaping != rows.end());
  const auto escaped_detail = escaping->value("detail", "");
  QB_CHECK(escaped_detail.find('"') != std::string::npos);
  QB_CHECK(escaped_detail.find('\\') != std::string::npos);
  QB_CHECK(escaped_detail.find('\t') != std::string::npos);
  QB_CHECK(escaped_detail.find('\n') != std::string::npos);
  QB_CHECK(result.json.find("\\\\") != std::string::npos);
  QB_CHECK(result.json.find("\\t") != std::string::npos);
  QB_CHECK(result.json.find("\\n") != std::string::npos);

  const auto truncation = std::find_if(rows.begin(), rows.end(), [](const auto& row) {
    return row.value("slug", "") == "rules/truncation";
  });
  QB_CHECK(truncation != rows.end());
  const auto truncated_detail = truncation->value("detail", "");
  QB_CHECK(truncated_detail.size() <= 512);
  QB_CHECK(truncated_detail.size() >= 14);
  QB_CHECK(truncated_detail.substr(truncated_detail.size() - 14) == "...[truncated]");

  auto team_result = qbrain::test_support::call_op(
      brain, "find_contradictions", {{"source_id", "team_a"}, {"limit", "200"}});
  QB_CHECK(team_result.ok);
  auto team_rows = nlohmann::json::parse(team_result.json);
  QB_CHECK(count_rows(team_rows, "same_predicate_different_object", "rules/team-owner") == 1);
  for (const auto& excluded : {"rules/null-owner", "rules/dangling-owner", "rules/cross-source"}) {
    QB_CHECK(std::none_of(team_rows.begin(), team_rows.end(), [&](const auto& row) {
      return row.value("slug", "") == excluded;
    }));
  }
  QB_CHECK(logical_snapshot(brain) == before);

  const auto output = std::make_pair(result.json, result.text);
  brain.close();
  return output;
}

void exercise_contradiction_rule_contract(const std::filesystem::path& forward_path,
                                          const std::filesystem::path& reverse_path) {
  const auto forward = contradiction_rule_output(forward_path, false);
  const auto reverse = contradiction_rule_output(reverse_path, true);
  QB_CHECK(forward.first == reverse.first);
  QB_CHECK(forward.second == reverse.second);
}

std::pair<std::string, std::string> anomaly_contract_output(
    const std::filesystem::path& database_path, bool reverse_insertion) {
  qbrain::Brain brain("n18-anomaly-contract");
  brain.open_at(qbrain::util::path_to_utf8(database_path));
  QB_CHECK(brain.ensure_source("team_a"));

  for (const auto& slug : {"anomaly/A-origin", "anomaly/a-origin", "anomaly/b-origin",
                           "anomaly/c-origin", "anomaly/live-origin",
                           "anomaly/deleted-origin", "anomaly/degree20",
                           "anomaly/degree21", "anomaly/live-target",
                           "anomaly/deleted-target", "anomaly/degree-target"}) {
    qbrain::test_support::put_page(brain, "default", slug, "default");
  }
  QB_CHECK(brain.soft_delete("anomaly/deleted-origin"));
  QB_CHECK(brain.soft_delete("anomaly/deleted-target"));
  for (const auto& slug : {"anomaly/team-live-target", "anomaly/team-deleted-target",
                           "anomaly/degree20", "anomaly/degree-target"}) {
    qbrain::test_support::put_page(brain, "team_a", slug, "team");
  }
  QB_CHECK(brain.soft_delete("anomaly/team-deleted-target", "team_a"));

  struct LinkInput {
    std::string source;
    std::string from;
    std::string to;
    std::string type;
    std::string provenance;
  };
  std::vector<LinkInput> links = {
      {"default", "anomaly/A-origin", "anomaly/deleted-target", "deleted", "one"},
      {"default", "anomaly/a-origin", "anomaly/team-deleted-target", "missing", "one"},
      {"default", "anomaly/b-origin", "anomaly/team-live-target", "missing", "one"},
      {"default", "anomaly/c-origin", "anomaly/duplicate-target", "wiki", "one"},
      {"default", "anomaly/c-origin", "anomaly/duplicate-target", "reference", "two"},
      {"default", "anomaly/live-origin", "anomaly/live-target", "live", "one"},
      {"default", "anomaly/missing-origin", "anomaly/invisible-missing", "ignored", "one"},
      {"default", "anomaly/deleted-origin", "anomaly/invisible-deleted", "ignored", "one"},
  };
  for (int index = 0; index < 20; ++index) {
    links.push_back({"default", "anomaly/degree20", "anomaly/degree-target",
                     "degree-" + std::to_string(index), "default"});
  }
  for (int index = 0; index < 21; ++index) {
    links.push_back({"default", "anomaly/degree21", "anomaly/degree-target",
                     "degree-" + std::to_string(index), "default"});
  }
  for (int index = 0; index < 50; ++index) {
    links.push_back({"team_a", "anomaly/degree20", "anomaly/degree-target",
                     "degree-" + std::to_string(index), "team"});
  }
  if (reverse_insertion) std::reverse(links.begin(), links.end());
  brain.db().exec("BEGIN;");
  try {
    for (const auto& link : links) {
      add_link(brain, link.source, link.from, link.to, link.type, link.provenance);
    }
    brain.db().exec("COMMIT;");
  } catch (...) {
    brain.db().exec("ROLLBACK;");
    throw;
  }

  const auto before = logical_snapshot(brain);
  auto result = qbrain::test_support::call_op(
      brain, "find_anomalies", {{"source_id", "default"}, {"limit", "200"}});
  QB_CHECK(result.ok);
  QB_CHECK(logical_snapshot(brain) == before);
  auto repeated = qbrain::test_support::call_op(
      brain, "find_anomalies", {{"source_id", "default"}, {"limit", "200"}});
  QB_CHECK(repeated.ok && repeated.json == result.json && repeated.text == result.text);
  QB_CHECK(logical_snapshot(brain) == before);

  const nlohmann::json expected = nlohmann::json::array({
      {{"source_id", "default"},
       {"kind", "link_to_deleted_page"},
       {"slug", "anomaly/A-origin"},
       {"detail", "link target soft-deleted: anomaly/deleted-target"}},
      {{"source_id", "default"},
       {"kind", "link_to_missing_page"},
       {"slug", "anomaly/a-origin"},
       {"detail", "link target missing: anomaly/team-deleted-target"}},
      {{"source_id", "default"},
       {"kind", "link_to_missing_page"},
       {"slug", "anomaly/b-origin"},
       {"detail", "link target missing: anomaly/team-live-target"}},
      {{"source_id", "default"},
       {"kind", "link_to_missing_page"},
       {"slug", "anomaly/c-origin"},
       {"detail", "link target missing: anomaly/duplicate-target"}},
      {{"source_id", "default"},
       {"kind", "high_out_degree"},
       {"slug", "anomaly/degree21"},
       {"detail", "out_degree=21 (>20)"}},
  });
  const auto rows = nlohmann::json::parse(result.json);
  QB_CHECK(rows == expected);
  for (const auto& row : rows) QB_CHECK(row.is_object() && row.size() == 4);
  QB_CHECK(result.json.find("anomaly/missing-origin") == std::string::npos);
  QB_CHECK(result.json.find("anomaly/deleted-origin") == std::string::npos);
  QB_CHECK(result.json.find("anomaly/live-origin") == std::string::npos);
  QB_CHECK(result.json.find("anomaly/degree20") == std::string::npos);

  auto team = qbrain::test_support::call_op(
      brain, "find_anomalies", {{"source_id", "team_a"}, {"limit", "200"}});
  QB_CHECK(team.ok);
  auto team_rows = nlohmann::json::parse(team.json);
  QB_CHECK(count_rows(team_rows, "high_out_degree", "anomaly/degree20") == 1);
  QB_CHECK(logical_snapshot(brain) == before);

  const auto output = std::make_pair(result.json, result.text);
  brain.close();
  return output;
}

void exercise_anomaly_contract(const std::filesystem::path& forward_path,
                               const std::filesystem::path& reverse_path) {
  const auto forward = anomaly_contract_output(forward_path, false);
  const auto reverse = anomaly_contract_output(reverse_path, true);
  QB_CHECK(forward.first == reverse.first);
  QB_CHECK(forward.second == reverse.second);
}

std::pair<std::string, std::string> expert_contract_output(
    const std::filesystem::path& database_path, bool reverse_insertion) {
  qbrain::Brain brain("n18-expert-contract");
  brain.open_at(qbrain::util::path_to_utf8(database_path));
  QB_CHECK(brain.ensure_source("team_a"));

  for (const auto& slug : {"expert/live-origin", "expert/second-origin",
                           "expert/deleted-origin", "expert/leader", "expert/A-tie",
                           "expert/a-tie", "expert/zero-inbound", "expert/deleted-target"}) {
    qbrain::test_support::put_page(brain, "default", slug, "default");
  }
  QB_CHECK(brain.soft_delete("expert/deleted-origin"));
  QB_CHECK(brain.soft_delete("expert/deleted-target"));
  for (const auto& slug : {"expert/team-origin", "expert/leader", "expert/team-strong"}) {
    qbrain::test_support::put_page(brain, "team_a", slug, "team");
  }

  struct LinkInput {
    std::string source;
    std::string from;
    std::string to;
    std::string type;
    std::string provenance;
  };
  std::vector<LinkInput> links = {
      {"default", "expert/live-origin", "expert/leader", "wiki", "one"},
      {"default", "expert/live-origin", "expert/leader", "reference", "two"},
      {"default", "expert/second-origin", "expert/leader", "wiki", "three"},
      {"default", "expert/live-origin", "expert/A-tie", "wiki", "one"},
      {"default", "expert/second-origin", "expert/A-tie", "reference", "two"},
      {"default", "expert/live-origin", "expert/a-tie", "wiki", "one"},
      {"default", "expert/second-origin", "expert/a-tie", "reference", "two"},
      {"default", "expert/missing-origin", "expert/leader", "ignored", "missing-origin"},
      {"default", "expert/deleted-origin", "expert/leader", "ignored", "deleted-origin"},
      {"default", "expert/live-origin", "expert/missing-target", "ignored", "missing-target"},
      {"default", "expert/live-origin", "expert/deleted-target", "ignored", "deleted-target"},
  };
  for (int index = 0; index < 10; ++index) {
    links.push_back({"team_a", "expert/team-origin", "expert/team-strong",
                     "team-" + std::to_string(index), "team"});
  }
  for (int index = 0; index < 8; ++index) {
    links.push_back({"team_a", "expert/team-origin", "expert/leader",
                     "leader-" + std::to_string(index), "team"});
  }
  if (reverse_insertion) std::reverse(links.begin(), links.end());
  brain.db().exec("BEGIN;");
  try {
    for (const auto& link : links) {
      add_link(brain, link.source, link.from, link.to, link.type, link.provenance);
    }
    brain.db().exec("COMMIT;");
  } catch (...) {
    brain.db().exec("ROLLBACK;");
    throw;
  }

  const auto before = logical_snapshot(brain);
  auto result = qbrain::test_support::call_op(
      brain, "find_experts", {{"source_id", "default"}, {"limit", "200"}});
  QB_CHECK(result.ok);
  QB_CHECK(logical_snapshot(brain) == before);
  auto repeated = qbrain::test_support::call_op(
      brain, "find_experts", {{"source_id", "default"}, {"limit", "200"}});
  QB_CHECK(repeated.ok && repeated.json == result.json && repeated.text == result.text);
  QB_CHECK(logical_snapshot(brain) == before);

  const nlohmann::json expected = nlohmann::json::array({
      {{"source_id", "default"}, {"slug", "expert/leader"}, {"inbound_count", 3}},
      {{"source_id", "default"}, {"slug", "expert/A-tie"}, {"inbound_count", 2}},
      {{"source_id", "default"}, {"slug", "expert/a-tie"}, {"inbound_count", 2}},
  });
  const auto rows = nlohmann::json::parse(result.json);
  QB_CHECK(rows == expected);
  for (const auto& row : rows) QB_CHECK(row.is_object() && row.size() == 3);
  for (const auto& excluded : {"expert/zero-inbound", "expert/missing-target",
                               "expert/deleted-target", "expert/team-strong"}) {
    QB_CHECK(result.json.find(excluded) == std::string::npos);
    QB_CHECK(result.text.find(excluded) == std::string::npos);
  }

  auto limited = qbrain::test_support::call_op(
      brain, "find_experts", {{"source_id", "default"}, {"limit", "1"}});
  QB_CHECK(limited.ok);
  QB_CHECK(nlohmann::json::parse(limited.json) == nlohmann::json::array({expected[0]}));
  QB_CHECK(logical_snapshot(brain) == before);

  auto team = qbrain::test_support::call_op(
      brain, "find_experts", {{"source_id", "team_a"}, {"limit", "1"}});
  QB_CHECK(team.ok);
  const auto team_rows = nlohmann::json::parse(team.json);
  QB_CHECK(team_rows.size() == 1);
  QB_CHECK(team_rows[0]["source_id"] == "team_a");
  QB_CHECK(team_rows[0]["slug"] == "expert/team-strong");
  QB_CHECK(team_rows[0]["inbound_count"] == 10);
  QB_CHECK(logical_snapshot(brain) == before);

  const auto output = std::make_pair(result.json, result.text);
  brain.close();
  return output;
}

void exercise_expert_contract(const std::filesystem::path& forward_path,
                              const std::filesystem::path& reverse_path) {
  const auto forward = expert_contract_output(forward_path, false);
  const auto reverse = expert_contract_output(reverse_path, true);
  QB_CHECK(forward.first == reverse.first);
  QB_CHECK(forward.second == reverse.second);
}

void add_link_with_context(qbrain::Brain& brain, const std::string& source_id,
                           const std::string& from, const std::string& to,
                           const std::string& context, const std::string& link_type,
                           const std::string& link_source) {
  qbrain::Link link;
  link.source_id = source_id;
  link.from_slug = from;
  link.to_slug = to;
  link.context = context;
  link.link_type = link_type;
  link.link_source = link_source;
  brain.add_link(link);
}

void set_database_config(qbrain::Brain& brain, const std::string& key,
                         const std::string& value) {
  auto statement = brain.db().prepare(
      "INSERT INTO config(key,value) VALUES(?,?) "
      "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  statement.bind_text(1, key);
  statement.bind_text(2, value);
  statement.step_done();
}

void seed_isolation_brain(qbrain::Brain& brain, const std::string& anomaly_sentinel,
                          const std::string& contradiction_sentinel,
                          int expert_a_count, int expert_b_count,
                          const std::string& internal_prefix) {
  QB_CHECK(brain.ensure_source("team_a"));
  QB_CHECK(brain.ensure_source("team_b"));
  for (const auto& source : {"default", "team_a"}) {
    qbrain::test_support::put_page(brain, source, "isolation/origin",
                                   internal_prefix + "_BODY_SECRET");
    qbrain::test_support::put_page(brain, source, "isolation/expert-a", "expert a");
    qbrain::test_support::put_page(brain, source, "isolation/expert-b", "expert b");
    auto owner = qbrain::test_support::put_page(brain, source, "isolation/facts", "facts");
    brain.add_fact("isolation/entity", "titled", "common", owner.id);
    brain.add_fact("isolation/entity", "titled", contradiction_sentinel + "_" + source,
                   owner.id);
    add_link_with_context(brain, source, "isolation/origin",
                          anomaly_sentinel + "_" + source,
                          internal_prefix + "_LINK_CONTEXT_SECRET", "anomaly", "fixture");
    for (int index = 0; index < expert_a_count; ++index) {
      add_link(brain, source, "isolation/origin", "isolation/expert-a",
               "a-" + std::to_string(index), "fixture");
    }
    for (int index = 0; index < expert_b_count; ++index) {
      add_link(brain, source, "isolation/origin", "isolation/expert-b",
               "b-" + std::to_string(index), "fixture");
    }
  }
  set_database_config(brain, "n18.fixture_secret", internal_prefix + "_CONFIG_SECRET");
  brain.log_ingest("n18_fixture", "internal",
                   nlohmann::json({{"canary", internal_prefix + "_LOG_SECRET"}}).dump(),
                   100, "default");
}

void require_no_forbidden_text(const std::string& text,
                               const std::vector<std::string>& forbidden) {
  for (const auto& value : forbidden) QB_CHECK(text.find(value) == std::string::npos);
}

void require_result_rows_match_text(const qbrain::ops::OpResult& result) {
  const auto rows = nlohmann::json::parse(result.json);
  std::ostringstream expected;
  for (const auto& row : rows) {
    QB_CHECK(row.contains("source_id") && row["source_id"].is_string());
    QB_CHECK(row.contains("slug") && row["slug"].is_string());
    if (row.contains("kind")) {
      QB_CHECK(row.size() == 4);
      QB_CHECK(row["kind"].is_string() && row["detail"].is_string());
      expected << row["source_id"].get<std::string>() << "\t"
               << row["kind"].get<std::string>() << "\t"
               << row["slug"].get<std::string>() << "\t"
               << row["detail"].get<std::string>() << "\n";
    } else {
      QB_CHECK(row.size() == 3);
      QB_CHECK(row["inbound_count"].is_number_integer());
      expected << row["source_id"].get<std::string>() << "\t"
               << row["slug"].get<std::string>() << "\t"
               << row["inbound_count"].get<int64_t>() << "\n";
    }
  }
  if (rows.empty()) expected << "[]\n";
  QB_CHECK(result.text == expected.str());
}

void exercise_registry_mcp_snapshot_contract(const std::filesystem::path& selected_path,
                                             const std::filesystem::path& decoy_path) {
  qbrain::Brain selected("n18-selected-matrix");
  selected.open_at(qbrain::util::path_to_utf8(selected_path));
  qbrain::Brain decoy("n18-decoy-matrix");
  decoy.open_at(qbrain::util::path_to_utf8(decoy_path));
  seed_isolation_brain(selected, "SELECTED_ANOMALY_SENTINEL",
                       "SELECTED_CONTRADICTION_SENTINEL", 3, 1, "SELECTED_INTERNAL");
  seed_isolation_brain(decoy, "DECOY_ANOMALY_SENTINEL",
                       "DECOY_CONTRADICTION_SENTINEL", 1, 4, "DECOY_INTERNAL");

  const std::vector<std::string> selected_forbidden = {
      "DECOY_ANOMALY_SENTINEL",       "DECOY_CONTRADICTION_SENTINEL",
      "SELECTED_INTERNAL_BODY_SECRET", "SELECTED_INTERNAL_LINK_CONTEXT_SECRET",
      "SELECTED_INTERNAL_CONFIG_SECRET", "SELECTED_INTERNAL_LOG_SECRET",
      "DECOY_INTERNAL_BODY_SECRET",     "DECOY_INTERNAL_LINK_CONTEXT_SECRET",
      "DECOY_INTERNAL_CONFIG_SECRET",   "DECOY_INTERNAL_LOG_SECRET",
  };
  const std::vector<std::string> decoy_forbidden = {
      "SELECTED_ANOMALY_SENTINEL",       "SELECTED_CONTRADICTION_SENTINEL",
      "SELECTED_INTERNAL_BODY_SECRET",   "SELECTED_INTERNAL_LINK_CONTEXT_SECRET",
      "SELECTED_INTERNAL_CONFIG_SECRET", "SELECTED_INTERNAL_LOG_SECRET",
      "DECOY_INTERNAL_BODY_SECRET",      "DECOY_INTERNAL_LINK_CONTEXT_SECRET",
      "DECOY_INTERNAL_CONFIG_SECRET",    "DECOY_INTERNAL_LOG_SECRET",
  };
  const std::array<const char*, 3> operations = {
      "find_anomalies", "find_contradictions", "find_experts"};

  qbrain::mcp::ServeOptions mcp_options;
  auto tools_list = call_without_mutation(selected, decoy, [&] {
    return nlohmann::json::parse(qbrain::mcp::handle_rpc_body(
        selected, mcp_options,
        R"({"jsonrpc":"2.0","id":200,"method":"tools/list","params":{}})"));
  });
  for (const auto& name : operations) {
    const auto* operation = qbrain::ops::global_registry().find(name);
    QB_CHECK(operation && operation->scope == qbrain::ops::Scope::Read &&
             !operation->local_only);
    const auto expected_schema = nlohmann::json::parse(operation->input_schema_json);
    std::vector<nlohmann::json> matching_tools;
    for (const auto& tool : tools_list["result"]["tools"]) {
      if (tool.value("name", "") == name) matching_tools.push_back(tool);
    }
    QB_CHECK(matching_tools.size() == 1);
    QB_CHECK(matching_tools[0]["inputSchema"] == expected_schema);
  }

  for (const auto& name : operations) {
    auto success = call_without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(selected, name, {{"limit", "200"}});
    });
    QB_CHECK(success.ok);
    QB_CHECK(!nlohmann::json::parse(success.json).empty());
    require_result_rows_match_text(success);
    require_no_forbidden_text(success.json, selected_forbidden);
    require_no_forbidden_text(success.text, selected_forbidden);

    auto empty = call_without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(selected, name, {{"limit", "0"}});
    });
    QB_CHECK(empty.ok && nlohmann::json::parse(empty.json).empty());

    auto clamped = call_without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(selected, name, {{"limit", "999"}});
    });
    QB_CHECK(clamped.ok && nlohmann::json::parse(clamped.json).size() <= 200);

    for (const auto& [args, code, field] :
         std::vector<std::tuple<std::unordered_map<std::string, std::string>,
                                std::string, std::string>>{
             {{{"limit", "1junk"}}, "invalid_argument", "limit"},
             {{{"source_id", "unknown_source"}}, "source_not_found", "source_id"},
             {{{"unexpected", "value"}}, "invalid_argument", "unexpected"},
         }) {
      auto error = call_without_mutation(selected, decoy, [&] {
        return qbrain::test_support::call_op(selected, name, args);
      });
      require_operation_error(error, code, field);
    }

    for (bool allow_write : {false, true}) {
      auto denied = call_without_mutation(selected, decoy, [&] {
        return qbrain::test_support::call_op(
            selected, name, {{"source_id", "team_a"}}, true, allow_write);
      });
      require_operation_error(denied, "source_not_allowed", "source_id");
    }

    auto remote_default = call_without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(selected, name, {{"limit", "1"}}, true, false);
    });
    require_source_rows(remote_default, "default", 1);
  }

  set_database_config(selected, "mcp.allowed_sources", "team_a");
  for (const auto& name : operations) {
    auto team_success = call_without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(
          selected, name, {{"source_id", "TEAM_A"}, {"limit", "1"}}, true, false);
    });
    require_source_rows(team_success, "team_a", 1);
    auto team_b_denied = call_without_mutation(selected, decoy, [&] {
      return qbrain::test_support::call_op(
          selected, name, {{"source_id", "team_b"}}, true, true);
    });
    require_operation_error(team_b_denied, "source_not_allowed", "source_id");
  }

  int request_id = 210;
  for (const auto& name : operations) {
    auto success = call_without_mutation(selected, decoy, [&] {
      return mcp_tool_call(selected, mcp_options, request_id++, name,
                           nlohmann::json{{"limit", 200}});
    });
    auto success_rows = require_mcp_success_rows(success);
    QB_CHECK(!success_rows.empty());
    require_no_forbidden_text(success.dump(), selected_forbidden);

    auto empty = call_without_mutation(selected, decoy, [&] {
      return mcp_tool_call(selected, mcp_options, request_id++, name,
                           nlohmann::json{{"limit", 0}});
    });
    QB_CHECK(require_mcp_success_rows(empty).empty());

    auto clamped = call_without_mutation(selected, decoy, [&] {
      return mcp_tool_call(selected, mcp_options, request_id++, name,
                           nlohmann::json{{"limit", 999}});
    });
    QB_CHECK(require_mcp_success_rows(clamped).size() <= 200);

    for (const auto& [arguments, code, field] :
         std::vector<std::tuple<nlohmann::json, std::string, std::string>>{
             {nlohmann::json{{"limit", "1junk"}}, "invalid_argument", "limit"},
             {nlohmann::json{{"source_id", "unknown_source"}}, "source_not_allowed",
              "source_id"},
             {nlohmann::json{{"unexpected", "value"}}, "invalid_argument", "unexpected"},
         }) {
      auto response = call_without_mutation(selected, decoy, [&] {
        return mcp_tool_call(selected, mcp_options, request_id++, name, arguments);
      });
      auto error = require_mcp_structured_error(response);
      QB_CHECK(error["code"] == code && error["field"] == field);
    }

    auto team_success = call_without_mutation(selected, decoy, [&] {
      return mcp_tool_call(selected, mcp_options, request_id++, name,
                           nlohmann::json{{"source_id", "TEAM_A"}, {"limit", 1}});
    });
    auto team_rows = require_mcp_success_rows(team_success);
    QB_CHECK(team_rows.size() == 1 && team_rows[0]["source_id"] == "team_a");

    for (bool allow_write : {false, true}) {
      qbrain::mcp::ServeOptions denied_options;
      denied_options.allow_write = allow_write;
      auto denied = call_without_mutation(selected, decoy, [&] {
        return mcp_tool_call(selected, denied_options, request_id++, name,
                             nlohmann::json{{"source_id", "team_b"}});
      });
      auto error = require_mcp_structured_error(denied);
      QB_CHECK(error["code"] == "source_not_allowed" && error["field"] == "source_id");
    }
  }

  const std::string unknown_name = "unknown_DUAL_SNAPSHOT_SECRET";
  auto local_unknown = call_without_mutation(selected, decoy, [&] {
    return qbrain::test_support::call_op(selected, unknown_name);
  });
  require_operation_error(local_unknown, "unknown_operation", "name");
  require_no_forbidden_text(local_unknown.text, {unknown_name});
  require_no_forbidden_text(local_unknown.json, {unknown_name});
  auto mcp_unknown = call_without_mutation(selected, decoy, [&] {
    return mcp_tool_call(selected, mcp_options, request_id++, unknown_name,
                         nlohmann::json::object());
  });
  auto mcp_unknown_error = require_mcp_structured_error(mcp_unknown);
  QB_CHECK(mcp_unknown_error["code"] == "unknown_operation" &&
           mcp_unknown_error["field"] == "name");
  require_no_forbidden_text(mcp_unknown.dump(), {unknown_name});

  for (const auto& invalid_name : {nlohmann::json(true), nlohmann::json(nullptr)}) {
    auto invalid_name_response = call_without_mutation(selected, decoy, [&] {
      nlohmann::json request = {
          {"jsonrpc", "2.0"},
          {"id", request_id++},
          {"method", "tools/call"},
          {"params", {{"name", invalid_name}, {"arguments", nlohmann::json::object()}}}};
      return nlohmann::json::parse(
          qbrain::mcp::handle_rpc_body(selected, mcp_options, request.dump()));
    });
    QB_CHECK(invalid_name_response["error"]["code"] == -32602);
  }

  for (const auto& name : operations) {
    auto decoy_result = call_without_mutation(decoy, selected, [&] {
      return qbrain::test_support::call_op(decoy, name, {{"limit", "200"}});
    });
    QB_CHECK(decoy_result.ok);
    require_no_forbidden_text(decoy_result.json, decoy_forbidden);
    require_no_forbidden_text(decoy_result.text, decoy_forbidden);
  }
  auto selected_experts = call_without_mutation(selected, decoy, [&] {
    return qbrain::test_support::call_op(selected, "find_experts", {{"limit", "2"}});
  });
  auto selected_expert_rows = nlohmann::json::parse(selected_experts.json);
  QB_CHECK(selected_expert_rows[0]["slug"] == "isolation/expert-a");
  QB_CHECK(selected_expert_rows[0]["inbound_count"] == 3);
  auto decoy_experts = call_without_mutation(decoy, selected, [&] {
    return qbrain::test_support::call_op(decoy, "find_experts", {{"limit", "2"}});
  });
  auto decoy_expert_rows = nlohmann::json::parse(decoy_experts.json);
  QB_CHECK(decoy_expert_rows[0]["slug"] == "isolation/expert-b");
  QB_CHECK(decoy_expert_rows[0]["inbound_count"] == 4);

  decoy.close();
  selected.close();
}

}  // namespace

void test_n18() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "qbrain_n18_test";
  fs::remove_all(root);
  fs::create_directories(root);

  qbrain::ops::register_builtin_ops();
  exercise_limit_and_source_contract(root / "limits.db");
  exercise_contradiction_rule_contract(root / "contradictions-forward.db",
                                       root / "contradictions-reverse.db");
  exercise_anomaly_contract(root / "anomalies-forward.db", root / "anomalies-reverse.db");
  exercise_expert_contract(root / "experts-forward.db", root / "experts-reverse.db");
  exercise_registry_mcp_snapshot_contract(root / "selected-matrix.db",
                                          root / "decoy-matrix.db");
  qbrain::Brain brain("n18-primary");
  brain.open_at(qbrain::util::path_to_utf8(root / "primary.db"));
  brain.ensure_source("team_a");

  // Same-source origin/target rules and degree threshold.
  qbrain::test_support::put_page(brain, "default", "n18/origin", "origin");
  qbrain::test_support::put_page(brain, "default", "n18/deleted", "deleted");
  QB_CHECK(brain.soft_delete("n18/deleted"));
  qbrain::test_support::put_page(brain, "team_a", "n18/shared", "team target");
  add_link(brain, "default", "n18/origin", "n18/shared");
  add_link(brain, "default", "n18/origin", "n18/deleted");
  add_link(brain, "default", "n18/origin", "n18/duplicate", "wiki", "manual");
  add_link(brain, "default", "n18/origin", "n18/duplicate", "reference", "manual-2");
  qbrain::test_support::put_page(brain, "default", "n18/degree20", "degree 20");
  qbrain::test_support::put_page(brain, "default", "n18/degree21", "degree 21");
  for (int index = 0; index < 20; ++index) {
    add_link(brain, "default", "n18/degree20", "n18/missing20-" + std::to_string(index));
  }
  for (int index = 0; index < 21; ++index) {
    add_link(brain, "default", "n18/degree21", "n18/missing21-" + std::to_string(index));
  }
  auto dead_origin = qbrain::test_support::put_page(brain, "default", "n18/dead-origin", "dead");
  QB_CHECK(brain.soft_delete(dead_origin.slug));
  add_link(brain, "default", "n18/dead-origin", "n18/should-not-appear");

  // Facts are included only through a live, source-owned page.
  auto facts_default = qbrain::test_support::put_page(brain, "default", "n18/facts-default", "facts");
  auto facts_team = qbrain::test_support::put_page(brain, "team_a", "n18/facts-team", "facts");
  auto facts_deleted = qbrain::test_support::put_page(brain, "default", "n18/facts-deleted", "facts");
  brain.add_fact("entity/default", "TITLED", "CEO", facts_default.id);
  brain.add_fact("entity/default", "titled", "CTO", facts_default.id);
  brain.add_fact("entity/default", "supports", "plan", facts_default.id);
  brain.add_fact("entity/default", "opposes", "plan", facts_default.id);
  brain.add_fact("entity/team", "titled", "team-a", facts_team.id);
  brain.add_fact("entity/team", "titled", "team-b", facts_team.id);
  brain.add_fact("entity/deleted", "titled", "old-a", facts_deleted.id);
  brain.add_fact("entity/deleted", "titled", "old-b", facts_deleted.id);
  QB_CHECK(brain.soft_delete(facts_deleted.slug));
  brain.db().exec(
      "INSERT INTO facts(page_id,entity_slug,predicate,object_text,active) "
      "VALUES(NULL,'entity/null','titled','a',1),(999999,'entity/dangling','titled','b',1)");
  std::string invalid_utf8_object(400, 'x');
  invalid_utf8_object.push_back(static_cast<char>(0xff));
  invalid_utf8_object.append(200, 'y');
  brain.add_fact("entity/utf8", "titled", invalid_utf8_object, facts_default.id);
  brain.add_fact("entity/utf8", "titled", "short", facts_default.id);
  brain.add_fact("entity/prefix", "not_not_x", "one", facts_default.id);
  brain.add_fact("entity/prefix", "not_x", "two", facts_default.id);
  const std::string nul_object_one("same\0one", 8);
  const std::string nul_object_two("same\0two", 8);
  brain.add_fact("entity/nul-object", "titled", nul_object_one, facts_default.id);
  brain.add_fact("entity/nul-object", "titled", nul_object_two, facts_default.id);

  // Expert ranking counts only live endpoints in the selected source.
  qbrain::test_support::put_page(brain, "default", "n18/expert-alpha", "alpha");
  qbrain::test_support::put_page(brain, "default", "n18/expert-beta", "beta");
  for (const auto& slug : {"n18/ref-a", "n18/ref-b", "n18/ref-c"}) {
    qbrain::test_support::put_page(brain, "default", slug, "ref");
    add_link(brain, "default", slug, "n18/expert-alpha");
  }
  add_link(brain, "default", "n18/ref-a", "n18/expert-beta");
  qbrain::test_support::put_page(brain, "team_a", "n18/expert-alpha", "team alpha");
  qbrain::test_support::put_page(brain, "team_a", "n18/team-ref", "ref");
  for (int index = 0; index < 6; ++index) add_link(brain, "team_a", "n18/team-ref", "n18/expert-alpha", "t" + std::to_string(index));
  add_link(brain, "team_a", "n18/team-ref", "n18/ambient-team-missing");

  const auto read_before = logical_snapshot(brain);
  auto anomalies = qbrain::test_support::call_op(brain, "find_anomalies", {{"limit", "200"}});
  QB_CHECK(anomalies.ok);
  auto anomaly_rows = nlohmann::json::parse(anomalies.json);
  QB_CHECK(has_row(anomaly_rows, "link_to_missing_page", "n18/origin"));
  QB_CHECK(has_row(anomaly_rows, "link_to_deleted_page", "n18/origin"));
  QB_CHECK(has_row(anomaly_rows, "high_out_degree", "n18/degree21"));
  QB_CHECK(!has_row(anomaly_rows, "high_out_degree", "n18/degree20"));
  QB_CHECK(!has_row(anomaly_rows, "link_to_missing_page", "n18/dead-origin"));
  int duplicate_count = 0;
  for (const auto& row : anomaly_rows) {
    QB_CHECK(row["source_id"] == "default");
    if (row.value("slug", "") == "n18/origin" && row.value("detail", "").find("duplicate") != std::string::npos) {
      ++duplicate_count;
    }
  }
  QB_CHECK(duplicate_count == 1);
  require_bounded_utf8_details(anomaly_rows);
  auto anomalies_repeat = qbrain::test_support::call_op(brain, "find_anomalies", {{"limit", "200"}});
  QB_CHECK(anomalies_repeat.ok && anomalies_repeat.json == anomalies.json);

  auto contradictions = qbrain::test_support::call_op(brain, "find_contradictions", {{"limit", "200"}});
  QB_CHECK(contradictions.ok);
  auto contradiction_rows = nlohmann::json::parse(contradictions.json);
  QB_CHECK(has_row(contradiction_rows, "same_predicate_different_object", "entity/default"));
  QB_CHECK(has_row(contradiction_rows, "conflicting_predicates", "entity/default"));
  QB_CHECK(has_row(contradiction_rows, "same_predicate_different_object", "entity/utf8"));
  QB_CHECK(has_row(contradiction_rows, "same_predicate_different_object", "entity/nul-object"));
  for (const auto& row : contradiction_rows) {
    QB_CHECK(row["source_id"] == "default");
    QB_CHECK(row.value("slug", "") != "entity/team");
    QB_CHECK(row.value("slug", "") != "entity/deleted");
    QB_CHECK(row.value("slug", "") != "entity/null");
    QB_CHECK(row.value("slug", "") != "entity/dangling");
    QB_CHECK(row.value("slug", "") != "entity/prefix");
  }
  require_bounded_utf8_details(contradiction_rows);
  bool found_sanitized_utf8_detail = false;
  for (const auto& row : contradiction_rows) {
    if (row.value("slug", "") != "entity/utf8") continue;
    const auto detail = row.value("detail", "");
    QB_CHECK(detail.find("\xEF\xBF\xBD") != std::string::npos);
    QB_CHECK(detail.ends_with("...[truncated]"));
    QB_CHECK(detail.size() <= 512);
    found_sanitized_utf8_detail = true;
  }
  QB_CHECK(found_sanitized_utf8_detail);

  auto experts = qbrain::test_support::call_op(brain, "find_experts", {{"limit", "200"}});
  QB_CHECK(experts.ok);
  auto expert_rows = nlohmann::json::parse(experts.json);
  QB_CHECK(expert_rows.size() >= 2);
  QB_CHECK(expert_rows[0]["slug"] == "n18/expert-alpha");
  QB_CHECK(expert_rows[0]["inbound_count"].get<int>() == 3);
  QB_CHECK(expert_rows[1]["slug"] == "n18/expert-beta");
  QB_CHECK(expert_rows[1]["inbound_count"].get<int>() == 1);
  QB_CHECK(logical_snapshot(brain) == read_before);

  for (const auto& name : {"find_anomalies", "find_contradictions", "find_experts"}) {
    const auto* operation = qbrain::ops::global_registry().find(name);
    QB_CHECK(operation && operation->scope == qbrain::ops::Scope::Read && !operation->local_only);
    const auto schema = nlohmann::json::parse(operation->input_schema_json);
    QB_CHECK(schema["type"] == "object");
    QB_CHECK(schema.contains("additionalProperties"));
    QB_CHECK(schema["additionalProperties"] == false);
    QB_CHECK(schema["properties"].size() == 2);
    QB_CHECK(schema["properties"]["source_id"]["type"] == "string");
    QB_CHECK(schema["properties"]["source_id"].contains("default"));
    QB_CHECK(schema["properties"]["source_id"]["default"] == "default");
    QB_CHECK(schema["properties"]["limit"]["type"] == "integer");
    QB_CHECK(schema["properties"]["limit"]["minimum"] == 0);
    QB_CHECK(schema["properties"]["limit"]["maximum"] == 200);
  }
  QB_CHECK(nlohmann::json::parse(
               qbrain::ops::global_registry().find("find_anomalies")->input_schema_json)
               ["properties"]["limit"]["default"] == 100);
  QB_CHECK(nlohmann::json::parse(
               qbrain::ops::global_registry().find("find_contradictions")->input_schema_json)
               ["properties"]["limit"]["default"] == 100);
  QB_CHECK(nlohmann::json::parse(
               qbrain::ops::global_registry().find("find_experts")->input_schema_json)
               ["properties"]["limit"]["default"] == 50);

  auto unexpected_argument =
      qbrain::test_support::call_op(brain, "find_anomalies", {{"unexpected", "value"}});
  QB_CHECK(!unexpected_argument.ok && unexpected_argument.exit_code != 0);
  auto unexpected_error = nlohmann::json::parse(unexpected_argument.json)["error"];
  QB_CHECK(unexpected_error["code"] == "invalid_argument");
  QB_CHECK(unexpected_error["field"] == "unexpected");
  const auto invalid_before = logical_snapshot(brain);
  auto zero = qbrain::test_support::call_op(brain, "find_anomalies", {{"limit", "0"}});
  auto malformed = qbrain::test_support::call_op(brain, "find_experts", {{"limit", "1junk"}});
  auto empty_source = qbrain::test_support::call_op(brain, "find_contradictions", {{"source_id", ""}});
  auto unknown_source = qbrain::test_support::call_op(brain, "find_anomalies", {{"source_id", "unknown"}});
  QB_CHECK(zero.ok && nlohmann::json::parse(zero.json).empty());
  QB_CHECK(!malformed.ok && !empty_source.ok && !unknown_source.ok);
  QB_CHECK(nlohmann::json::parse(malformed.json)["error"]["field"] == "limit");
  QB_CHECK(logical_snapshot(brain) == invalid_before);

  const auto remote_before = logical_snapshot(brain);
  auto remote_denied = qbrain::test_support::call_op(
      brain, "find_experts", {{"source_id", "team_a"}}, true, false);
  QB_CHECK(!remote_denied.ok);
  QB_CHECK(nlohmann::json::parse(remote_denied.json)["error"]["code"] == "source_not_allowed");
  QB_CHECK(logical_snapshot(brain) == remote_before);
  brain.save_config_value("mcp.allowed_sources", "Team_A");
  const auto allowed_before = logical_snapshot(brain);
  auto remote_team = qbrain::test_support::call_op(
      brain, "find_experts", {{"source_id", "TEAM_A"}}, true, false);
  QB_CHECK(remote_team.ok && remote_team.json.find("team_a") != std::string::npos);
  auto remote_other = qbrain::test_support::call_op(
      brain, "find_anomalies", {{"source_id", "other"}}, true, true);
  QB_CHECK(!remote_other.ok);
  QB_CHECK(logical_snapshot(brain) == allowed_before);

  qbrain::mcp::ServeOptions mcp_options;
  auto mcp_list = nlohmann::json::parse(qbrain::mcp::handle_rpc_body(
      brain, mcp_options, R"({"jsonrpc":"2.0","id":90,"method":"tools/list","params":{}})"));
  QB_CHECK(std::any_of(mcp_list["result"]["tools"].begin(), mcp_list["result"]["tools"].end(),
                       [](const auto& tool) { return tool.value("name", "") == "find_experts"; }));
  auto mcp_read = nlohmann::json::parse(qbrain::mcp::handle_rpc_body(
      brain, mcp_options,
      R"({"jsonrpc":"2.0","id":91,"method":"tools/call","params":{"name":"find_experts","arguments":{"source_id":"TEAM_A"}}})"));
  QB_CHECK(mcp_read["result"]["isError"] == false);

  {
    ScopedEnvironmentVariable ambient_source("QBRAIN_SOURCE", "team_a");
    int request_id = 92;
    for (const auto& name : {"find_anomalies", "find_contradictions", "find_experts"}) {
      nlohmann::json request = {
          {"jsonrpc", "2.0"},
          {"id", request_id++},
          {"method", "tools/call"},
          {"params", {{"name", name}, {"arguments", nlohmann::json::object()}}}};
      auto response = nlohmann::json::parse(
          qbrain::mcp::handle_rpc_body(brain, mcp_options, request.dump()));
      QB_CHECK(response["result"]["isError"] == false);
      auto rows = nlohmann::json::parse(
          response["result"]["content"].back()["text"].get<std::string>());
      QB_CHECK(!rows.empty());
      QB_CHECK(std::all_of(rows.begin(), rows.end(), [](const auto& row) {
        return row.value("source_id", "") == "default";
      }));
    }
  }

  const std::vector<std::pair<nlohmann::json, std::string>> invalid_mcp_arguments = {
      {nlohmann::json{{"limit", true}}, "limit"},
      {nlohmann::json{{"limit", nullptr}}, "limit"},
      {nlohmann::json{{"limit", 1.5}}, "limit"},
      {nlohmann::json{{"source_id", true}}, "source_id"},
      {nlohmann::json{{"source_id", nullptr}}, "source_id"},
      {nlohmann::json::array(), "arguments"},
  };
  int invalid_request_id = 100;
  for (const auto& [arguments, field] : invalid_mcp_arguments) {
    const auto before = logical_snapshot(brain);
    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"id", invalid_request_id++},
        {"method", "tools/call"},
        {"params", {{"name", "find_anomalies"}, {"arguments", arguments}}}};
    auto response = nlohmann::json::parse(
        qbrain::mcp::handle_rpc_body(brain, mcp_options, request.dump()));
    auto error = require_mcp_structured_error(response);
    QB_CHECK(error["code"] == "invalid_argument");
    QB_CHECK(error["field"] == field);
    QB_CHECK(logical_snapshot(brain) == before);
  }

  {
    const auto before = logical_snapshot(brain);
    const auto response = nlohmann::json::parse(qbrain::mcp::handle_rpc_body(
        brain, mcp_options,
        R"({"jsonrpc":"2.0","id":109,"method":"tools/call","params":{"name":"find_anomalies","arguments":{"limit":-0}}})"));
    const auto error = require_mcp_structured_error(response);
    QB_CHECK(error["code"] == "invalid_argument");
    QB_CHECK(error["field"] == "limit");
    QB_CHECK(logical_snapshot(brain) == before);
  }

  const std::string unknown_operation = "missing_N18_SECRET_SENTINEL";
  const auto unknown_before = logical_snapshot(brain);
  auto local_unknown =
      qbrain::test_support::call_op(brain, unknown_operation, {}, false, false);
  auto local_unknown_error =
      require_operation_error(local_unknown, "unknown_operation", "name");
  QB_CHECK(local_unknown.text.find(unknown_operation) == std::string::npos);
  QB_CHECK(local_unknown.json.find(unknown_operation) == std::string::npos);
  QB_CHECK(logical_snapshot(brain) == unknown_before);

  nlohmann::json unknown_request = {
      {"jsonrpc", "2.0"},
      {"id", 110},
      {"method", "tools/call"},
      {"params", {{"name", unknown_operation}, {"arguments", nlohmann::json::object()}}}};
  auto mcp_unknown = nlohmann::json::parse(
      qbrain::mcp::handle_rpc_body(brain, mcp_options, unknown_request.dump()));
  auto mcp_unknown_error = require_mcp_structured_error(mcp_unknown);
  QB_CHECK(mcp_unknown_error["code"] == "unknown_operation");
  QB_CHECK(mcp_unknown_error["field"] == "name");
  QB_CHECK(mcp_unknown.dump().find(unknown_operation) == std::string::npos);
  QB_CHECK(logical_snapshot(brain) == unknown_before);

  for (const auto& invalid_name : {nlohmann::json(true), nlohmann::json(nullptr)}) {
    const auto before = logical_snapshot(brain);
    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"id", 111},
        {"method", "tools/call"},
        {"params", {{"name", invalid_name}, {"arguments", nlohmann::json::object()}}}};
    auto response = nlohmann::json::parse(
        qbrain::mcp::handle_rpc_body(brain, mcp_options, request.dump()));
    QB_CHECK(response["error"]["code"] == -32602);
    QB_CHECK(response["error"]["message"] == "tools/call requires string params.name");
    QB_CHECK(logical_snapshot(brain) == before);
  }

  qbrain::Brain decoy("n18-decoy");
  decoy.open_at(qbrain::util::path_to_utf8(root / "decoy.db"));
  qbrain::test_support::put_page(decoy, "default", "n18/decoy-origin", "body");
  add_link(decoy, "default", "n18/decoy-origin", "DECOY_ANALYTICS_SENTINEL");
  auto decoy_owner = qbrain::test_support::put_page(decoy, "default", "n18/decoy-facts", "body");
  decoy.add_fact("DECOY_ENTITY", "titled", "one", decoy_owner.id);
  decoy.add_fact("DECOY_ENTITY", "titled", "two", decoy_owner.id);
  const auto selected_snapshot = logical_snapshot(brain);
  const auto decoy_snapshot = logical_snapshot(decoy);
  auto selected_anomalies = qbrain::test_support::call_op(brain, "find_anomalies");
  auto selected_contradictions = qbrain::test_support::call_op(brain, "find_contradictions");
  auto selected_experts = qbrain::test_support::call_op(brain, "find_experts");
  QB_CHECK(selected_anomalies.ok && selected_contradictions.ok && selected_experts.ok);
  QB_CHECK(selected_anomalies.json.find("DECOY_ANALYTICS_SENTINEL") == std::string::npos);
  QB_CHECK(selected_contradictions.json.find("DECOY_ENTITY") == std::string::npos);
  QB_CHECK(logical_snapshot(brain) == selected_snapshot);
  QB_CHECK(logical_snapshot(decoy) == decoy_snapshot);

  QB_CHECK(!g_snapshot_call_evidence.empty());
  std::cout << "[INFO] n18 snapshot_schema=pass snapshot_matrix=pass anomalies=" << anomaly_rows.size()
            << " contradictions=" << contradiction_rows.size()
            << " experts=" << expert_rows.size()
            << " source_scope=pass limit_source_matrix=pass contradiction_rule_matrix=pass "
            << "anomaly_matrix=pass expert_matrix=pass registry_mcp_snapshot_matrix=pass "
            << "mcp_type_validation=pass nul_text=pass deterministic=pass utf8_bounds=pass "
            << "text_json_equivalence=pass per_call_snapshot_hashes=pass snapshot_call_count="
            << g_snapshot_call_evidence.size()
            << " mcp_rpc=pass remote_authorization=pass selected_snapshot_sha256="
            << snapshot_sha256(selected_snapshot) << " decoy_snapshot_sha256="
            << snapshot_sha256(decoy_snapshot) << " read_only=pass\n";
  for (const auto& evidence : g_snapshot_call_evidence) {
    std::cout << "[INFO] n18 snapshot_call=" << evidence.index
              << " selected_before_sha256=" << evidence.selected_before
              << " selected_after_sha256=" << evidence.selected_after
              << " decoy_before_sha256=" << evidence.decoy_before
              << " decoy_after_sha256=" << evidence.decoy_after << "\n";
  }

  decoy.close();
  brain.close();
  fs::remove_all(root);
}
