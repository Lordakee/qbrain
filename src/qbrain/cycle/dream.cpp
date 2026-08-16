#include "qbrain/cycle/dream.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/util/time_util.hpp"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string_view>

using json = nlohmann::json;

namespace qbrain::cycle {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kDefaultRetentionHours = 72;
constexpr int kMaxRetentionHours = 8760;

void finish_phase(PhaseResult& pr, Clock::time_point started) {
  pr.duration_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());
}

bool parse_retention_hours(std::string_view raw, int& retention_hours) {
  if (raw.empty()) {
    retention_hours = kDefaultRetentionHours;
    return true;
  }

  int64_t parsed = 0;
  const char* first = raw.data();
  const char* last = first + raw.size();
  auto result = std::from_chars(first, last, parsed, 10);
  if (result.ec != std::errc{} || result.ptr != last) return false;

  if (parsed <= 0)
    retention_hours = 1;
  else if (parsed > kMaxRetentionHours)
    retention_hours = kMaxRetentionHours;
  else
    retention_hours = static_cast<int>(parsed);
  return true;
}

int64_t scalar_count(Brain& brain, std::string_view sql) {
  auto st = brain.db().prepare(sql);
  return st.step() ? st.column_int(0) : 0;
}

int64_t eligible_page_count(Brain& brain, int retention_hours) {
  // n38: datetime('now', '-' || ? || ' hours') -> C++-computed UTC cutoff
  // bound as a parameter (dialect-free SQL; comparison semantics unchanged).
  auto st = brain.db().prepare(
      "SELECT COUNT(*) FROM pages WHERE deleted_at IS NOT NULL AND deleted_at < ?");
  st.bind_text(1, util::utc_now_offset(static_cast<long long>(retention_hours) * -3600));
  return st.step() ? st.column_int(0) : 0;
}

PhaseResult run_orphans(Brain& brain, bool dry) {
  auto t0 = Clock::now();
  PhaseResult pr;
  pr.phase = "orphans";
  auto o = brain.find_orphans(100);
  pr.count = static_cast<int>(o.size());
  pr.status = "ok";
  pr.summary = dry ? ("dry-run orphans=" + std::to_string(pr.count))
                   : ("orphans=" + std::to_string(pr.count));
  if (!dry && !o.empty()) {
    int64_t tagged = 0;
    for (size_t i = 0; i < o.size() && i < 20; ++i) {
      brain.add_tag(o[i], "orphan");
      tagged += brain.db().changes();
    }
    pr.mutations = tagged;
    pr.summary += " tagged=" + std::to_string(tagged);
  }
  finish_phase(pr, t0);
  return pr;
}

PhaseResult run_extract_facts(Brain& brain, bool dry, int page_limit) {
  auto t0 = Clock::now();
  PhaseResult pr;
  pr.phase = "extract_facts";
  auto pages = brain.list_pages(page_limit);
  int total = 0;
  if (dry) {
    pr.count = static_cast<int>(pages.size());
    pr.status = "ok";
    pr.summary = "dry-run candidates=" + std::to_string(pr.count);
  } else {
    for (auto& p : pages) total += brain.extract_facts_from_page(p.slug, p.source_id);
    pr.count = total;
    pr.mutations = total;
    pr.status = "ok";
    pr.summary = "facts_written=" + std::to_string(total) + " pages=" + std::to_string(pages.size());
  }
  finish_phase(pr, t0);
  return pr;
}

PhaseResult run_consolidate(Brain& brain, bool dry, int page_limit) {
  auto t0 = Clock::now();
  PhaseResult pr;
  pr.phase = "consolidate";
  auto pages = brain.list_pages(page_limit);
  int n = 0;
  if (dry) {
    pr.count = static_cast<int>(pages.size());
    pr.status = "ok";
    pr.summary = "dry-run candidates=" + std::to_string(pr.count);
  } else {
    for (auto& p : pages) {
      brain.add_fact(p.slug, "titled", p.title, p.id);
      ++n;
    }
    pr.count = n;
    pr.mutations = n;
    pr.status = "ok";
    pr.summary = "facts_titled=" + std::to_string(n);
  }
  finish_phase(pr, t0);
  return pr;
}

PhaseResult run_embed(Brain& brain, bool dry) {
  auto t0 = Clock::now();
  PhaseResult pr;
  pr.phase = "embed";
  if (dry) {
    auto waiting = jobs::list_jobs(brain, "waiting", 1000);
    int embeds = 0;
    for (auto& j : waiting)
      if (j.type == "embed") ++embeds;
    pr.count = embeds;
    pr.status = "ok";
    pr.summary = "dry-run waiting_embed_jobs=" + std::to_string(embeds);
  } else {
    // Prefer minions claim path; fall back to legacy drain
    int via_minions = jobs::drain_jobs(brain, 50, "dream-embed");
    int via_legacy = brain.drain_embed_jobs(50);
    pr.count = via_minions + via_legacy;
    pr.mutations = pr.count;
    pr.status = "ok";
    pr.summary = "jobs=" + std::to_string(via_minions) + " legacy_chunks=" + std::to_string(via_legacy);
  }
  finish_phase(pr, t0);
  return pr;
}

PhaseResult run_purge(Brain& brain, bool dry, int retention_hours) {
  auto t0 = Clock::now();
  PhaseResult pr;
  pr.phase = "purge";
  if (dry) {
    pr.status = "ok";
    pr.count = eligible_page_count(brain, retention_hours);
    pr.summary = "dry-run eligible_pages=" + std::to_string(pr.count) +
                 " retention_hours=" + std::to_string(retention_hours);
  } else {
    auto& db = brain.db();
    bool in_transaction = false;
    try {
      db.exec("BEGIN IMMEDIATE;");
      in_transaction = true;
      // n38: every reference below drops the SQLite-only "temp." schema
      // qualifier: both SQLite (temp before main) and PostgreSQL (pg_temp
      // first in the implicit search path) resolve the unqualified name to
      // this session's qbrain_dream_purge_targets, and no persistent table
      // of that name exists, so behavior is identical.
      db.exec("CREATE TEMP TABLE IF NOT EXISTS qbrain_dream_purge_targets ("
              "id INTEGER PRIMARY KEY, source_id TEXT NOT NULL, slug TEXT NOT NULL);");
      db.exec("DELETE FROM qbrain_dream_purge_targets;");  // n38: temp. -> unqualified
      {
        // n38: datetime('now', '-' || ? || ' hours') -> C++-computed UTC
        // cutoff bound as a parameter (dialect-free SQL).
        auto targets = db.prepare(
            "INSERT INTO qbrain_dream_purge_targets(id, source_id, slug) "
            "SELECT id, source_id, slug FROM pages "
            "WHERE deleted_at IS NOT NULL AND deleted_at < ?");
        targets.bind_text(1, util::utc_now_offset(
                                  static_cast<long long>(retention_hours) * -3600));
        targets.step_done();
      }

      const int64_t pages = scalar_count(
          brain, "SELECT COUNT(*) FROM qbrain_dream_purge_targets");
      const int64_t chunks = scalar_count(
          brain, "SELECT COUNT(*) FROM content_chunks WHERE page_id IN "
                 "(SELECT id FROM qbrain_dream_purge_targets)");
      const int64_t tags = scalar_count(
          brain, "SELECT COUNT(*) FROM tags WHERE page_id IN "
                 "(SELECT id FROM qbrain_dream_purge_targets)");
      const int64_t versions = scalar_count(
          brain, "SELECT COUNT(*) FROM page_versions WHERE page_id IN "
                 "(SELECT id FROM qbrain_dream_purge_targets)");
      const int64_t facts = scalar_count(
          brain, "SELECT COUNT(*) FROM facts WHERE page_id IN "
                 "(SELECT id FROM qbrain_dream_purge_targets)");
      const int64_t links = scalar_count(
          brain, "SELECT COUNT(*) FROM links l WHERE EXISTS ("
                 "SELECT 1 FROM qbrain_dream_purge_targets t "
                 "WHERE t.source_id=l.source_id AND "
                 "(t.slug=l.from_slug OR t.slug=l.to_slug))");

      db.exec("DELETE FROM facts WHERE page_id IN "
              "(SELECT id FROM qbrain_dream_purge_targets);");
      const int64_t facts_deleted = db.changes();
      db.exec("DELETE FROM links WHERE EXISTS ("
              "SELECT 1 FROM qbrain_dream_purge_targets t "
              "WHERE t.source_id=links.source_id AND "
              "(t.slug=links.from_slug OR t.slug=links.to_slug));");
      const int64_t links_deleted = db.changes();
      db.exec("DELETE FROM pages WHERE id IN "
              "(SELECT id FROM qbrain_dream_purge_targets);");
      const int64_t pages_deleted = db.changes();

      const int64_t remaining_children =
          scalar_count(brain, "SELECT COUNT(*) FROM content_chunks WHERE page_id IN "
                              "(SELECT id FROM qbrain_dream_purge_targets)") +
          scalar_count(brain, "SELECT COUNT(*) FROM tags WHERE page_id IN "
                              "(SELECT id FROM qbrain_dream_purge_targets)") +
          scalar_count(brain, "SELECT COUNT(*) FROM page_versions WHERE page_id IN "
                              "(SELECT id FROM qbrain_dream_purge_targets)");
      if (pages_deleted != pages || facts_deleted != facts || links_deleted != links ||
          remaining_children != 0) {
        throw std::runtime_error("purge row-count mismatch");
      }

      db.exec("DROP TABLE qbrain_dream_purge_targets;");  // n38: temp. -> unqualified
      db.exec("COMMIT;");
      in_transaction = false;

      pr.count = pages_deleted;
      pr.mutations = pages + chunks + tags + versions + facts + links;
      pr.status = "ok";
      pr.summary = "pages=" + std::to_string(pages) + " facts=" + std::to_string(facts) +
                   " links=" + std::to_string(links) + " chunks=" + std::to_string(chunks) +
                   " tags=" + std::to_string(tags) + " versions=" + std::to_string(versions) +
                   " retention_hours=" + std::to_string(retention_hours);
    } catch (...) {
      if (in_transaction) {
        try {
          db.exec("ROLLBACK;");
        } catch (...) {
        }
      }
      try {
        db.exec("DROP TABLE IF EXISTS qbrain_dream_purge_targets;");  // n38: temp. -> unqualified
      } catch (...) {
      }
      pr.status = "fail";
      pr.summary = "purge failed; transaction rolled back";
      pr.count = 0;
      pr.mutations = 0;
    }
  }
  finish_phase(pr, t0);
  return pr;
}

PhaseResult skipped_purge(int retention_hours) {
  PhaseResult pr;
  pr.phase = "purge";
  pr.status = "skipped";
  pr.summary = "explicit phase purge required; retention_hours=" +
               std::to_string(retention_hours);
  return pr;
}

}  // namespace

CycleReport run_dream(Brain& brain, const DreamOpts& opts) {
  auto t0 = Clock::now();
  CycleReport r;
  r.dry_run = opts.dry_run;
  r.timestamp = util::utc_now();

  int retention_hours = kDefaultRetentionHours;
  if (!parse_retention_hours(opts.retention_hours, retention_hours)) {
    PhaseResult pr;
    pr.phase = "purge";
    pr.status = "fail";
    pr.summary = "invalid retention_hours; expected a base-10 integer";
    r.phases.push_back(std::move(pr));
    r.status = "failed";
    r.duration_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
    return r;
  }

  if (opts.phase.empty()) {
    r.phases.push_back(run_orphans(brain, opts.dry_run));
    r.phases.push_back(run_extract_facts(brain, opts.dry_run, opts.page_limit));
    r.phases.push_back(run_consolidate(brain, opts.dry_run, opts.page_limit));
    r.phases.push_back(run_embed(brain, opts.dry_run));
    r.phases.push_back(opts.dry_run ? run_purge(brain, true, retention_hours)
                                    : skipped_purge(retention_hours));
  } else if (opts.phase == "orphans") {
    r.phases.push_back(run_orphans(brain, opts.dry_run));
  } else if (opts.phase == "extract_facts") {
    r.phases.push_back(run_extract_facts(brain, opts.dry_run, opts.page_limit));
  } else if (opts.phase == "consolidate") {
    r.phases.push_back(run_consolidate(brain, opts.dry_run, opts.page_limit));
  } else if (opts.phase == "embed") {
    r.phases.push_back(run_embed(brain, opts.dry_run));
  } else if (opts.phase == "purge") {
    r.phases.push_back(run_purge(brain, opts.dry_run, retention_hours));
  }

  if (r.phases.empty()) {
    PhaseResult pr;
    pr.phase = opts.phase.empty() ? "(none)" : opts.phase;
    pr.status = "fail";
    pr.summary = "unknown phase; use orphans|extract_facts|consolidate|embed|purge";
    r.phases.push_back(pr);
    r.status = "failed";
  } else {
    bool any_fail = false;
    bool any_ok = false;
    for (auto& p : r.phases) {
      if (p.status == "fail") any_fail = true;
      if (p.status == "ok") any_ok = true;
    }
    if (any_fail && any_ok)
      r.status = "partial";
    else if (any_fail)
      r.status = "failed";
    else
      r.status = "ok";
  }

  r.duration_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
  return r;
}

std::string report_to_json(const CycleReport& r) {
  json phases = json::array();
  for (auto& p : r.phases) {
    phases.push_back({{"phase", p.phase},
                      {"status", p.status},
                      {"summary", p.summary},
                      {"count", p.count},
                      {"mutations", p.mutations},
                      {"duration_ms", p.duration_ms}});
  }
  json j = {{"schema_version", "1"},
            {"status", r.status},
            {"timestamp", r.timestamp},
            {"duration_ms", r.duration_ms},
            {"dry_run", r.dry_run},
            {"phases", phases}};
  return j.dump(2);
}

std::string report_to_text(const CycleReport& r) {
  std::ostringstream oss;
  oss << (r.dry_run ? "[dry-run] " : "") << "dream status=" << r.status
      << " duration_ms=" << r.duration_ms << "\n";
  for (auto& p : r.phases) {
    oss << "  " << p.phase << " [" << p.status << "] mutations=" << p.mutations << " "
        << p.summary << " (" << p.duration_ms << "ms)\n";
  }
  return oss.str();
}

}  // namespace qbrain::cycle
