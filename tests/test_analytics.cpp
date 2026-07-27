#include "qbrain/core/brain.hpp"
#include "qbrain/graph/analytics.hpp"
#include "qbrain/util/paths.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

void test_analytics() {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / "qbrain_test_analytics";
  fs::create_directories(dir);
  auto dbp = dir / "brain.db";
  fs::remove(dbp);

  qbrain::Brain b("test_analytics");
  b.open_at(qbrain::util::path_to_utf8(dbp));

  // --- find_anomalies: broken link + high out-degree ---
  {
    qbrain::PageInput hub;
    hub.slug = "hub/page";
    hub.title = "Hub";
    hub.body = "hub";
    b.put_page(hub);

    qbrain::Link broken;
    broken.from_slug = "hub/page";
    broken.to_slug = "missing/nowhere";
    broken.link_source = "manual";
    b.add_link(broken);

    // soft-deleted target
    qbrain::PageInput gone;
    gone.slug = "was/here";
    gone.title = "Gone";
    gone.body = "x";
    b.put_page(gone);
    b.soft_delete("was/here");
    qbrain::Link to_del;
    to_del.from_slug = "hub/page";
    to_del.to_slug = "was/here";
    to_del.link_source = "manual";
    b.add_link(to_del);

    // high out-degree: 21 outbound links from hub
    for (int i = 0; i < 21; ++i) {
      qbrain::Link l;
      l.from_slug = "hub/page";
      l.to_slug = "targets/t" + std::to_string(i);
      l.link_source = "manual";
      b.add_link(l);
    }

    auto anom = qbrain::graph::find_anomalies(b, 200);
    bool saw_missing = false, saw_deleted = false, saw_high = false;
    for (auto& a : anom) {
      if (a.kind == "link_to_missing_page" && a.slug == "hub/page") saw_missing = true;
      if (a.kind == "link_to_deleted_page" && a.slug == "hub/page") saw_deleted = true;
      if (a.kind == "high_out_degree" && a.slug == "hub/page") saw_high = true;
    }
    QB_CHECK(saw_missing);
    QB_CHECK(saw_deleted);
    QB_CHECK(saw_high);
  }

  // --- find_contradictions ---
  {
    b.add_fact("people/alice", "titled", "CEO");
    b.add_fact("people/alice", "titled", "CTO");  // same predicate different object
    b.add_fact("people/bob", "supports", "plan-a");
    b.add_fact("people/bob", "opposes", "plan-a");  // conflicting pair

    auto cons = qbrain::graph::find_contradictions(b, 50);
    bool same_pred = false, pair = false;
    for (auto& c : cons) {
      if (c.slug == "people/alice" && c.kind == "same_predicate_different_object") same_pred = true;
      if (c.slug == "people/bob" && c.kind == "conflicting_predicates") pair = true;
    }
    QB_CHECK(same_pred);
    QB_CHECK(pair);
  }

  // --- find_experts: rank by inbound ---
  {
    qbrain::PageInput e1;
    e1.slug = "experts/alpha";
    e1.title = "Alpha";
    e1.body = "a";
    b.put_page(e1);
    qbrain::PageInput e2;
    e2.slug = "experts/beta";
    e2.title = "Beta";
    e2.body = "b";
    b.put_page(e2);
    qbrain::PageInput ref;
    ref.slug = "refs/r1";
    ref.title = "R1";
    ref.body = "r";
    b.put_page(ref);
    qbrain::PageInput ref2;
    ref2.slug = "refs/r2";
    ref2.title = "R2";
    ref2.body = "r";
    b.put_page(ref2);
    qbrain::PageInput ref3;
    ref3.slug = "refs/r3";
    ref3.title = "R3";
    ref3.body = "r";
    b.put_page(ref3);

    // alpha: 3 inbound, beta: 1 inbound
    for (auto from : {"refs/r1", "refs/r2", "refs/r3"}) {
      qbrain::Link l;
      l.from_slug = from;
      l.to_slug = "experts/alpha";
      l.link_source = "manual";
      b.add_link(l);
    }
    {
      qbrain::Link l;
      l.from_slug = "refs/r1";
      l.to_slug = "experts/beta";
      l.link_source = "manual";
      b.add_link(l);
    }

    auto experts = qbrain::graph::find_experts(b, 10);
    QB_CHECK(experts.size() >= 2);
    // alpha should rank at or above beta
    int alpha_rank = -1, beta_rank = -1;
    for (int i = 0; i < static_cast<int>(experts.size()); ++i) {
      if (experts[i].slug == "experts/alpha") {
        alpha_rank = i;
        QB_CHECK(experts[i].inbound_count >= 3);
      }
      if (experts[i].slug == "experts/beta") {
        beta_rank = i;
        QB_CHECK(experts[i].inbound_count >= 1);
      }
    }
    QB_CHECK(alpha_rank >= 0 && beta_rank >= 0);
    QB_CHECK(alpha_rank < beta_rank);
  }

  b.close();
  fs::remove_all(dir);
}
