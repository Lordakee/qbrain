# Ops Parity Ledger

Updated: 2026-08-15
Qbrain implemented MCP/CLI ops: **104**

**Ops inventory (generated, N31 2026-08-15)**: docs/nodes/n31-evidence/OPS-INVENTORY.json — 108 registered ops (104 upstream + 4 extensions incl. list_job_messages), op→test mapping complete (0 gaps), deterministic (regenerated after list_job_messages reconciliation; sha256 fa772160…b3268b); enforced by test n31_a_counts_mapping.
**Current suite status (generated, N30 2026-08-15)**: 31/31 registered tests PASS, two rounds, both CMake and direct-MSVC build paths (docs/nodes/n30-evidence/FINAL-VERIFY-*.txt). Historical per-wave prose counts below are retained as history only.
**Audit tiers (master plan v2.0.0 §1)**: N20/N23 corrective closure PASS in N30 (fresh audits 2026-08-15); N21 superseded (retrospective Tier-2, documentation-only); N24-N28 stub audits deferred to Phase-2 closure (AMD-7 deferral records in docs/nodes/n30-evidence/NODE-RECONCILIATION-MATRIX.json).

| upstream_op | status | notes |
|-------------|--------|-------|
| add_link | **implemented** | |
| add_tag | **implemented** | |
| add_timeline_entry | **implemented** | N15: source-scoped thin `type=timeline` page write; fresh hard-audit PASS 2026-08-04 |
| advisor | **implemented** | N26-N27 |
| cancel_job | **implemented** | N12 waiting/active cancellation; MCP write default-deny |
| chronicle_backfill | **implemented** | N23; fresh N23 outcome audit PASS via N30 corrective closure (2026-08-15, docs/nodes/N23-HARD-AUDIT.md) |
| chronicle_day | **implemented** | N15: strict UTC day over active pages in one authorized canonical source; fresh PASS 2026-08-04 |
| chronicle_last_seen | **implemented** | N23 |
| chronicle_on_this_day | **implemented** | N23 |
| chronicle_since | **implemented** | N15: strict inclusive UTC lower bound over active pages in one authorized canonical source; fresh PASS 2026-08-04 |
| code_blast | **implemented** | N22: source-scoped deterministic one-hop def/ref/call/callee heuristic subset; no AST, type resolution, or transitive upstream-parity claim; fresh native evidence and Claude Code outcome PASS 2026-08-07 |
| code_callees | **implemented** | N22: source-scoped bounded brace-body call-token heuristic; no AST, compiler index, overload/type resolution, or persisted call-edge claim; fresh native evidence and Claude Code outcome PASS 2026-08-07 |
| code_callers | **implemented** | N16: source-scoped call-like identifier matches, strict bounds, read-only; fresh PASS 2026-08-04 |
| code_def | **implemented** | N16: source-scoped line-oriented definition-like matches, strict bounds, read-only; fresh PASS 2026-08-04 |
| code_flow | **implemented** | N22: source-scoped deterministic bounded breadth-first brace-body heuristic traversal; no AST, compiler index, overload/type resolution, or transitive upstream-parity claim; fresh native evidence and Claude Code outcome PASS 2026-08-07 |
| code_refs | **implemented** | N16: source-scoped literal identifier-boundary matches, strict bounds, read-only; fresh PASS 2026-08-04 |
| code_traversal_cache_clear | **implemented** | N22: guarded Admin compatibility no-op, remote-default-deny, returns zero because Qbrain has no persisted traversal cache; fresh native evidence and Claude Code outcome PASS 2026-08-07 |
| delete_page | **implemented** | |
| extract_facts | **implemented** | |
| file_list | **implemented** | N24-N25 |
| file_upload | **implemented** | N24-N25 |
| file_url | **implemented** | N24-N25 |
| find_anomalies | **implemented** | N18: selected-source active-origin missing/deleted targets and `high_out_degree > 20`; fresh PASS 2026-08-04 |
| find_contradictions | **implemented** | N18: selected-source active page-owned syntactic fact-pair heuristics only; fresh PASS 2026-08-04 |
| find_experts | **implemented** | N18: selected-source live-endpoint inbound stored-link rank; fresh PASS 2026-08-04 |
| find_orphans | **implemented** | |
| find_trajectory | **implemented** | |
| forget_fact | **implemented** | N13 |
| get_active_schema_pack | **implemented** | N20; fresh N20 outcome audit PASS via N30 corrective closure (2026-08-15, docs/nodes/N20-HARD-AUDIT.md) |
| get_backlinks | **implemented** | |
| get_brain_identity | **implemented** | N19: selected-brain/source identity with live schema version and exact source-scoped counters; local-only database path; fresh PASS 2026-08-04 |
| get_calibration_profile | **implemented** | N21 |
| get_chunks | **implemented** | |
| get_health | **implemented** | |
| get_ingest_log | **implemented** | N15: newest-first bounded ingest events from one authorized canonical source; schema v12; fresh PASS 2026-08-04 |
| get_job | **implemented** | N12 audited minion lifecycle inspection |
| get_job_progress | **implemented** | N14: bounded/redacted read-only job fields with structured id errors; fresh PASS 2026-08-04 |
| get_links | **implemented** | |
| get_page | **implemented** | |
| get_raw_data | **implemented** | N26-N27 |
| get_recent_salience | **implemented** | N26-N27 |
| get_recent_transcripts | **implemented** | N26-N27 |
| get_skill | **implemented** | |
| get_stats | **implemented** | |
| get_status_snapshot | **implemented** | N14: selected-brain schema v12, storage stats, and all-queue job counts; fresh PASS 2026-08-04 |
| get_tags | **implemented** | |
| get_timeline | **implemented** | N19: deterministic bounded active `type=timeline` pages from one authorized canonical source; fresh PASS 2026-08-04 |
| get_versions | **implemented** | |
| list_brain_skillpack | **implemented** | N26-N27 |
| list_facts | **implemented** | N10 facts read helper |
| list_jobs | **implemented** | N12 audited minion lifecycle inspection |
| list_link_sources | **implemented** | N15: source-scoped histogram of open `link_source` provenance values; fresh PASS 2026-08-04 |
| list_pages | **implemented** | |
| list_schema_packs | **implemented** | N20 |
| list_skills | **implemented** | |
| log_ingest | **implemented** | N15: validated source-attributed retention-bounded ingest event write; schema v12; fresh PASS 2026-08-04 |
| ontology_conflicts | **implemented** | N24-N25 |
| ontology_dimensions | **implemented** | N20; fresh N20 outcome audit PASS via N30 corrective closure (2026-08-15) |
| ontology_get | **implemented** | N20 |
| ontology_propose | **implemented** | N24-N25 |
| pause_job | **implemented** | N14: token-fenced `waiting`/`active` to `paused`, local-only Write; fresh PASS 2026-08-04 |
| purge_deleted_pages | **implemented** | |
| put_page | **implemented** | |
| put_raw_data | **implemented** | N26-N27 |
| query | **implemented** | |
| recall | **implemented** | N13 conservative search alias |
| reload_schema_pack | **implemented** | N20 |
| remove_link | **implemented** | |
| remove_tag | **implemented** | |
| replay_job | **implemented** | N17: strict positive id, terminal `failed`/`completed` source only, atomic fresh `waiting` clone; fresh PASS 2026-08-04 |
| resolve_slugs | **implemented** | N13 |
| restore_page | **implemented** | |
| resume_job | **implemented** | N14: `paused` to `waiting` with a fresh claim fence, local-only Write; fresh PASS 2026-08-04 |
| retry_job | **implemented** | N13 |
| revert_version | **implemented** | |
| run_doctor | **implemented** | N11 read-only doctor; MCP Read and CLI `doctor` route |
| doctor_remediate | **implemented** | N14: explicit CLI remediation plus MCP local-only Write/default-deny; fresh PASS 2026-08-04 |
| run_onboard | **implemented** | N26-N27 |
| run_skillopt | **implemented** | N26-N27 |
| schema_apply_mutations | **implemented** | N28 |
| schema_explain_type | **implemented** | N24-N25 |
| schema_graph | **implemented** | N24-N25 |
| schema_lint | **implemented** | N24-N25 |
| schema_review_orphans | **implemented** | N24-N25 |
| schema_stats | **implemented** | N20 |
| search | **implemented** | N12 audited fail-open rerank |
| search_by_image | **implemented** | N26-N27 |
| send_job_message | **implemented** | N17: existing-job-only local Write with bounded UTF-8 sender and canonical JSON payload; fresh PASS 2026-08-04 |
| sources_add | **implemented** | |
| sources_list | **implemented** | |
| sources_remove | **implemented** | N13 |
| sources_status | **implemented** | N13 |
| submit_agent | **implemented** | N26-N27 |
| submit_job | **implemented** | N12 token-fenced minions; MCP write default-deny |
| sync_brain | **implemented** | N13 live-sync |
| takes_calibration | **implemented** | N21 (Tier-2 retrospective; scope re-verified centrally by N30 D3 deny matrix) |
| takes_list | **implemented** | N21 |
| takes_scorecard | **implemented** | N21 |
| takes_search | **implemented** | N21 |
| think | **implemented** | |
| traverse_graph | **implemented** | N13 |
| volunteer_chronicle | **implemented** | N19: N15 `chronicle_since` delegation with strict source isolation and bounded seven-UTC-calendar-day default; fresh PASS 2026-08-04 |
| volunteer_context | **implemented** | N19: deterministic zero-LLM source-scoped conservative-search or recent-page pointers; fresh PASS 2026-08-04 |
| whoami | **implemented** | |

## Qbrain extensions (not in gbrain ops list)
| op | status |
|----|--------|
| capture | implemented |
| list_brains | implemented |
| run_dream | implemented (N12 five phases, purge opt-in, MCP write default-deny) |
| list_job_messages | implemented | N17 helper (Qbrain extension; read-only job message inbox; reconciled into the ledger by N31 ops inventory — registry count 108 = upstream 104 + extensions 3 + this helper) |

## N12–N15 notes
- N12: retrospective plan audit PASS and outcome hard audit PASS; rerank fail-open/timeout/rotation, token-fenced minions and populated-v5 migration, five-phase dream/full-table isolation/explicit purge, and MCP write default-deny verified by native MSVC 20/20 tests.
- N13: retrospective plan audit PASS and outcome hard audit PASS; source-scoped live-sync/state isolation, transactional source removal/status, bounded graph traversal, retry/fact operations, resolve_slugs/recall, MCP write default-deny, native MSVC 21/21 tests, and CLI sync/watch/graph smoke verified.
- N15: source-scoped link provenance, retention-bounded ingest log on schema v12, strict UTC chronicle reads, and a thin timeline page write; fresh Claude Code hard-audit PASS 2026-08-04.
- Depth note: many ops are usable/heuristic stubs (not full gbrain PG/LLM/tree-sitter parity).

## Project completion criterion (usable parity)
- D1–D25 master plan usable gates: **met** (N0–N11 + N12–N13 extensions)
- Full 100+ gbrain ops 1:1: **not goal**; ledger marks deferred intentionally

## N14–N16 notes
- N14: token-fenced pause/resume, bounded progress, selected-brain status snapshot, and explicit doctor remediation; fresh Claude Code hard-audit PASS 2026-08-04.
- N15: source-scoped provenance/ingest/chronicle/timeline subset on schema v12; fresh Claude Code hard-audit PASS 2026-08-04.
- N16: source-scoped bounded heuristic `code_def`/`code_refs`/`code_callers` reads (no tree-sitter/AST parity claim); fresh Claude Code hard-audit PASS 2026-08-04.
- Integrated native Windows x64 MSVC C++20 evidence: production and test builds exit 0, 25/25 tests PASS, and the frozen manifest covers 110 files plus 2 binaries.
- Fresh hard-audit SHA-256: N14 `15c9b5a0886e6b9406b6bbe6ed2cd12e12ffa1b732768193a4e281355b1f3c2b`; N15 `9f5f14ab7ed2cf4da50b597f8f861061948d9b65331091a017d677f7b4968c59`; N16 `591865f6647e175c4aa02ec90abad1075c554eca49e3a15e5f63ad1639c24aba`.

## N18 notes
- `find_anomalies`: selected-source active-origin missing/deleted targets plus stored-row `high_out_degree > 20`, with deterministic bounded output.
- `find_contradictions`: selected-source active page-owned facts, exact same-predicate/opposing-predicate/prefix syntactic rules, deterministic bounded detail.
- `find_experts`: selected-source stored inbound-link counts between live endpoints, ranked by count then bytewise slug.
- Fresh evidence: 25/25 native tests PASS, 70/70 selected/decoy before-after snapshot pairs match, and Claude Code hard-audit PASS 2026-08-04 (`f09971ecf44ab66129f33ee3b7dad91515aac39d6d330b725916983fcb408053`).

## N17–N19
- N17: `replay_job` and `send_job_message` refreshed under the current node loop; strict identifiers, terminal-only replay, bounded canonical message inbox, existing schema-v8/no-FK shape verified inside schema v12; fresh Claude Code outcome PASS 2026-08-04. `list_job_messages` remains an uncounted Qbrain helper.
- N18: find_anomalies/contradictions/experts
- N19: refreshed node closure PASS for `get_brain_identity`, `volunteer_context`, `get_timeline`, and `volunteer_chronicle`; native Windows x64 MSVC C++20 evidence has 26/26 tests PASS and 204 read-only snapshot rows.
- Unit tests 12/12; Claude audits PASS

## N20–N23
- schema packs, takes v9, code callees/flow/blast, chronicle remaining
- tests 13/13; Claude PASS

## N24–N25
- files v10; schema lint/graph; ontology propose/conflicts; tests 14/14

## N28
- schema_apply_mutations (add_type/add_dimension only)
- out-of-scope table rows: **0**
- unit tests 15/15

## Wave 4 retrospective notes
- N3: search/query plan+outcome PASS; title/backlink/vector evidence, tokenmax budget, autocut 0.35, limit clamp 100 covered by `test_wave4`.
- N6: embed drain/dream MVP plan+outcome PASS; deterministic `QBRAIN_EMBED_MOCK` test path, failed embed job handling, dream dry/apply, and MCP write-deny covered.
- N8: multi-brain routing plan+outcome PASS; path-safe brain ids, `--brain` > `QBRAIN_BRAIN` > default resolution, list/isolation/search scoping covered.
- Unit tests: **16/16 PASS** after Wave 4.

## Wave 5 retrospective notes
- N10: facts/trajectory plan+outcome PASS; `extract_facts`, `list_facts`, `find_trajectory`, and facts schema are implemented and covered by `test_wave5`.
- N10 explicit deferrals: code-intel/tree-sitter, multimodal facts/search, and full PG graph parity are **not** delivered by N10; later-node rows must carry their own audits.
- Unit tests: **17/17 PASS** after Wave 5 implementation evidence.

## N29 governance reconciliation
- Re-established node-specific Claude Code plan and outcome audit artifacts for N1, N21, N22, and N23.
- All affected node plans are marked done only with both plan-audit and hard-audit PASS; N29 added no product-code changes.
- Fresh Windows MSVC test run: **18/18 PASS** (one non-failing live_sync warning).

## Wave 6 / N11 quality closeout notes
- N11 plan audit PASS; outcome audit PASS.
- `run_doctor` returns structured `OK`/`FAIL` checks, is MCP Read, and does not require allow-write; `doctor_remediate` remains the separate local-only Write path.
- Windows direct MSVC path verified with `scripts\build-tests-cl.ps1`: **18/18 PASS** after adding doctor-specific tests.


