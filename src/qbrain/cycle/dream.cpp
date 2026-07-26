#include "qbrain/cycle/dream.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/util/time_util.hpp"
#include <chrono>
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace qbrain::cycle {
namespace {

using Clock = std::chrono::steady_clock;

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
    // Tag first batch for operator visibility (idempotent-ish)
    int tagged = 0;
    for (size_t i = 0; i < o.size() && i < 20; ++i) {
      brain.add_tag(o[i], "orphan");
      ++tagged;
    }
    pr.summary += " tagged=" + std::to_string(tagged);
  }
  pr.duration_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
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
    pr.status = "ok";
    pr.summary = "facts_written=" + std::to_string(total) + " pages=" + std::to_string(pages.size());
  }
  pr.duration_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
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
    pr.status = "ok";
    pr.summary = "facts_titled=" + std::to_string(n);
  }
  pr.duration_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
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
    pr.status = "ok";
    pr.summary = "jobs=" + std::to_string(via_minions) + " legacy_chunks=" + std::to_string(via_legacy);
  }
  pr.duration_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
  return pr;
}

PhaseResult run_purge(Brain& brain, bool dry) {
  auto t0 = Clock::now();
  PhaseResult pr;
  pr.phase = "purge";
  if (dry) {
    pr.status = "ok";
    pr.summary = "dry-run purge older_than_hours=72";
    pr.count = 0;
  } else {
    pr.count = brain.purge_deleted(72);
    pr.status = "ok";
    pr.summary = "purged=" + std::to_string(pr.count);
  }
  pr.duration_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
  return pr;
}

bool want_phase(const DreamOpts& opts, const char* name) {
  return opts.phase.empty() || opts.phase == name;
}

}  // namespace

CycleReport run_dream(Brain& brain, const DreamOpts& opts) {
  auto t0 = Clock::now();
  CycleReport r;
  r.dry_run = opts.dry_run;
  r.timestamp = util::utc_now();

  if (want_phase(opts, "orphans")) r.phases.push_back(run_orphans(brain, opts.dry_run));
  if (want_phase(opts, "extract_facts"))
    r.phases.push_back(run_extract_facts(brain, opts.dry_run, opts.page_limit));
  if (want_phase(opts, "consolidate"))
    r.phases.push_back(run_consolidate(brain, opts.dry_run, opts.page_limit));
  if (want_phase(opts, "embed")) r.phases.push_back(run_embed(brain, opts.dry_run));
  if (want_phase(opts, "purge")) r.phases.push_back(run_purge(brain, opts.dry_run));

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
    oss << "  " << p.phase << " [" << p.status << "] " << p.summary << " (" << p.duration_ms
        << "ms)\n";
  }
  return oss.str();
}

}  // namespace qbrain::cycle
