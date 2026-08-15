# N15 HARD AUDIT

**VERDICT: PASS**

**Auditor:** Claude Code
**Date:** 2026-08-04
**Plan:** `docs/nodes/N15-PLAN.md` (Status: approved, Plan audit: PASS 2026-07-30)
**Plan audit:** `docs/nodes/N15-PLAN-AUDIT.md` (PASS, Claude Code, 2026-07-30)

---

## Baseline

| Artifact | Status | SHA-256 |
|----------|--------|---------|
| Approved plan | PASS (2026-07-30) | `c712cad4de7e8e11f3c274ccdb9c9e2b1acacc780802a654a125b14040b31d4e` |
| Plan audit | PASS (2026-07-30) | `01e95a0cc55e4d0580562008a65de2ee941a13a8b37f4fd730389937d5abaef1` |
| N1-N13 dependencies | All PASS | Verified in evidence report |

---

## Implementation vs Approved Plan

### Deliverables (§ Deliverables, plan lines 42-75)

| # | Deliverable | Evidence | Result |
|---|-------------|----------|--------|
| 1 | Source-scoped link provenance with `list_link_sources` | `brain.cpp:832-849`, `handlers.cpp:1499-1515`; canonical source validation; deterministic ordering `COUNT(*) DESC, link_source ASC`; test matrix proves default/team_a isolation, operation registry metadata hash `be2d0f40ca6b015f16b1c9da37f9bf2b0db350888d973d1474dbfafd64619950` | ✓ |
| 2 | v12 migration with source_id, FK, index | `migrate.cpp:259-296`; transactional BEGIN IMMEDIATE/COMMIT/ROLLBACK; legacy rows backfilled to `default`; FK `ON DELETE CASCADE`; index `(source_id, created_at DESC, id DESC)`; test proves idempotence, rollback on injected failure, and FK cleanup | ✓ |
| 3 | Bounded ingest writes/reads with retention | `brain.cpp:851-927`; `log_ingest` validates source, clamps keep_last 1..1000, atomically inserts+prunes per source; event≤64B, path≤4096B, detail≤65536B valid JSON; `get_ingest_log` clamps limit 1..50, newest-first with id tie-breaker, source-isolated | ✓ |
| 4 | Strict UTC chronicle reads | `brain.cpp:1020-1071`; `chronicle_day` accepts `YYYY-MM-DD`, `chronicle_since` accepts `YYYY-MM-DD` or `YYYY-MM-DD[T ]HH:MM:SSZ`; validation rejects offsets, trailing data, invalid dates, overflow; inclusive boundaries, source-isolated, deterministic ordering, limit clamped 1..200 | ✓ |
| 5 | Thin timeline write | `handlers.cpp:1625-1667`; `add_timeline_entry` creates `type=timeline` page via `put_page`, validates source, requires title or body, auto-generates unique slug, replaces chunks, enqueues embed once; local calls extract links, remote calls do not; test proves same-second uniqueness | ✓ |
| 6 | Operation security and registration | `handlers.cpp:1518,1625` local_only=true for writes; `handlers.cpp:1499,1544,1567,1595` reads are Scope::Read; all six ops validate canonical source; remote non-default requires allowlist; test matrix proves denial before mutation, byte-identical snapshots | ✓ |
| 7 | Focused verification artifacts | `tests/test_n15.cpp` 1503 lines, registered in CMakeLists.txt line 4, test_main.cpp line 95, build-tests-cl.ps1; `scripts/n15-verify.ps1` present; `docs/nodes/n15-evidence/VERIFY-REPORT.md` records Windows x64 MSVC `/std:c++20`, exact commands, 25 PASS/0 FAIL, hashes | ✓ |

All seven deliverables present and match the approved plan specification.

---

## Acceptance Assertions (§ Acceptance assertions, plan lines 113-126)

| # | Assertion | Evidence | Result |
|---|-----------|----------|--------|
| 1 | v11→v12 transactional, idempotent, failure-safe | `test_n15.cpp:401-488`; populated v11 fixture with 2 legacy rows migrates to v12, ids/text/timestamps preserved, both backfilled to `default`; second migration is no-op (identical snapshot hash `6a72f46dbba6f534d0543685039e7f70554dafc26af34df0b6ef3ec136b8a0c0`); injected mid-v12 failure leaves v11 schema/data/indexes/version unchanged (snapshot `8065b4e091442358de89549b0ad60a501bf2634386ed3c4be47943a9cb495d73`) | **PASS** |
| 2 | Source-attributed ingest with brain isolation | `test_n15.cpp:801-888`; import/live-sync write `team_a` logs with counter-only JSON, no default-source cross-write; second brain with same source/root has isolated ingest rows (snapshot `b3243133bbad104dbfd88f78b408339bffafc7f0edaff1c2b83ef094df2db72f` ≠ primary `03d78a28838ec751e5aecf0b4c40b7b26379c6339e233d29d97b0aa0feea5038`) | **PASS** |
| 3 | Per-source atomic retention, 1..1000 boundary | `test_n15.cpp:565-648`; default 105→100 after write, team_a unchanged; keep_last 0→1, 1→1, 50→50, 1000→1000, 1001→1000; pruning one source never affects the other; retention snapshot `150d3089aac52d1e5a9067aa1edfa671feece1afa4ec9de3a68682eb76bddfad` | **PASS** |
| 4 | `get_ingest_log` limit 1..50, newest-first, source-only; rejected malformed | `test_n15.cpp:612-643,697-763`; limit 0→1, 1→1, 20→20, 50→50, 51→50; deterministic ordering proven by `require_newest_order`; 8 rejected invalid payloads (event>64B, path>4096B, detail>65536B, malformed JSON, bad source) leave snapshot unchanged | **PASS** |
| 5 | `list_link_sources` exact provenance counts, brain/source isolation, open strings | `test_n15.cpp:496-562`; default has wiki=3/manual=1/vendor=1, team_a has wiki=2/manual=1/vendor=1; each excludes the other; second brain team_a has second-only=1, primary unchanged; link snapshot `a00e68ad5468fb43e10b3ee63172cfbba801848e7016039744dbd13cf42d627b` | **PASS** |
| 6 | `chronicle_day` strict YYYY-MM-DD, active only, source-isolated, deterministic | `test_n15.cpp:897-963`; leap-day 2024-02-29 accepts day-before excluded, day-at/day-end/created/updated/both included, soft-deleted excluded, team_a page excluded, tie ordering by id DESC; 11 invalid day cases rejected without mutation | **PASS** |
| 7 | `chronicle_since` normalized UTC forms, inclusive lower bound, strict validation | `test_n15.cpp:965-1035`; date-only normalized to `T00:00:00Z`, space separator accepted, inclusive boundary; before excluded, at/after included, soft-deleted excluded, source-isolated; 14 invalid since cases (offset, overflow, trailing, partial) rejected without mutation | **PASS** |
| 8 | `add_timeline_entry` type=timeline page, correct provenance/chunks/embed, same-second uniqueness | `test_n15.cpp:1209-1273`; local creates timeline/cli provenance, chunks present, 1 embed job; custom slug overwrites with version; two identical calls in same second produce distinct slugs (found within 1 attempt); timeline snapshot `543761c8870583d5233195911a306e86501486d0863a4068dc46de032a62de27` | **PASS** |
| 9 | Remote writes denied before mutation without allow-write; authorized source only | `test_n15.cpp:1301-1344`; remote log_ingest/add_timeline_entry without allow-write denied, snapshot `32a8d94a859f7191cdf19f1b805678e852388b502997123e3319ffb030108f7e` unchanged; allow-write with allowlist `team_a` succeeds; non-allowlisted `other` rejected | **PASS** |
| 10 | Remote reads require authorized source, return no cross-source row, preserve snapshot | `test_n15.cpp:1349-1384`; 4 authorized read ops (list_link_sources, get_ingest_log, chronicle_day, chronicle_since) for team_a return only team_a rows; unauthorized `other` rejected; all leave snapshot unchanged; read snapshot `ed2bd942ef2c5117819e5b019ed0098422af2f40cc2ebc9e3762d0bc76db3704` | **PASS** |
| 11 | Ledger/descriptions call it thin timeline-page, not upstream page-attached Chronicle | `docs/OPS-PARITY-LEDGER.md:10` "N15 thin put_page type=timeline"; plan line 64 "one add_timeline_entry call creates or updates a type=timeline page, rather than pretending to append an upstream page-attached timeline row"; handler description line 1666 "Create a type=timeline page (thin put_page subset)"; no claim of upstream event table, week/narrative/kind, or full Chronicle parity | **PASS** |
| 12 | Native Windows x64 MSVC /std:c++20 evidence, exact counts, all tests pass, N1-N13 PASS deps | `docs/nodes/n15-evidence/VERIFY-REPORT.md`; OS Windows 11 Pro Workstation 10.0.22624 X64; MSVC 19.51.36248 x64; language `/std:c++20`; build exit 0, test exit 0; registered tests 25 PASS/0 FAIL (≥N13 baseline 21+N15); N1-N13 plan/outcome audits all PASS with SHA-256 hashes; no model/provider/config change | **PASS** |

All 12 acceptance assertions satisfied with explicit evidence.

---

## Tests (§ Tests, plan lines 78-111)

| # | Test category | Coverage | Result |
|---|---------------|----------|--------|
| 1 | Build and regression suite | `build-tests-cl.ps1` exit 0, `qbrain_tests.exe` exit 0, 25 registered tests PASS (≥21 N13 baseline + N15) | ✓ |
| 2 | Migration matrix | Fresh v12 contract, populated v11→v12 preserves 2 rows as default, idempotent no-op, injected failure rollback, FK cleanup on force-remove | ✓ |
| 3 | Source and retention matrix | Default/team_a link isolation, per-source pruning atomicity, limit/keep_last 0/1/50/1000/over-max, source validation, counter-only size boundaries, malformed JSON/overflow rejection | ✓ |
| 4 | Import/live-sync attribution matrix | `team_a` logs with bounded JSON, brain isolation, invalid source/missing path error contract | ✓ |
| 5 | Chronicle matrix | Day/since boundaries, leap-day fixture, soft-delete exclusion, source isolation, limit clamping, invalid date/time/offset/trailing rejection | ✓ |
| 6 | Timeline and MCP matrix | Local/remote provenance, chunks/embed, auto-slug uniqueness, empty payload rejection, invalid source, default-deny before mutation, allow-write authorized path, remote no link extraction | ✓ |
| 7 | Evidence manifest | Windows edition/x64/MSVC version/`/std:c++20`, exact commands/counts, runtime markers, migration/schema/snapshot hashes, N1-N13 PASS refs, no config change | ✓ |

Full test matrix executed and passed.

---

## Security (§ Security notes, plan lines 134-142)

| Check | Evidence | Result |
|-------|----------|--------|
| MCP default-deny before SQLite/FS mutation | `handlers.cpp:1518,1625` local_only=true; remote calls without allow-write rejected before handler mutation; test snapshots prove unchanged DB | ✓ |
| Source canonicalization and remote authorization | `brain.cpp:22-43,49-50` N2.5 Windows-safe canonicalization; `handlers.cpp:1403-1425` resolve_source checks allowlist for remote non-default; no silent fallback | ✓ |
| Bound SQL parameters, strict size/JSON validation, counter-only summaries | `brain.cpp:858-865,876-893` bound parameters; size checks before mutation; JSON parse before insert; `test_n15.cpp:369-393` counter-only keys, no body/token/secret | ✓ |
| Temporary brains only, no production LOCALAPPDATA mutation | `test_n15.cpp:1434-1436` EnvironmentGuard overrides LOCALAPPDATA to temp; production `%LOCALAPPDATA%\Qbrain` never touched | ✓ |
| Transactional migration, FK verification, scoped cleanup | `migrate.cpp:262-294` BEGIN IMMEDIATE/COMMIT/ROLLBACK; FK CASCADE; `test_n15.cpp:482` `PRAGMA foreign_key_check` empty after force-remove | ✓ |
| No secrets committed, no LLM config read/changed | Evidence report line 56 "No model/provider/base URL/API key, reasoning, context, or compression configuration was changed"; git history clean | ✓ |

All six security requirements satisfied.

---

## Build and Runtime Evidence

| Metric | Value | Source |
|--------|-------|--------|
| OS | Windows 11 Pro for Workstations 10.0.22624 X64 | VERIFY-REPORT.md line 6 |
| Compiler | MSVC 19.51.36248 for x64 | VERIFY-REPORT.md line 8 |
| Language mode | `/std:c++20` | VERIFY-REPORT.md line 10 |
| Build command exit | 0 | VERIFY-REPORT.md line 11 |
| Test command exit | 0 | VERIFY-REPORT.md line 12 |
| Registered test count | 25 PASS, 0 FAIL | VERIFY-REPORT.md line 13 |
| CLI smoke | PASS (doctor=pass, version=pass, isolated_localappdata=pass) | VERIFY-REPORT.md line 51 |
| Build manifest hash | `2b6bee15b0c279c3a85e81b99eebf2811cc59a90627ae1ec331e08ce6e4a4d7d` | VERIFY-REPORT.md line 15, matches `build/wave3-final-build-manifest.txt` |
| Deliverable hashes | All 14 N15 deliverables recorded with SHA-256 | VERIFY-REPORT.md lines 59-77 |

Native Windows x64 MSVC C++20 verification complete.

---

## Ledger Reconciliation

The approved plan (lines 23-33) authorizes reconciliation of exactly six N15 ledger rows:

| Operation | Ledger status | Evidence |
|-----------|---------------|----------|
| `list_link_sources` | **implemented** | Line 59, N15 attribution |
| `log_ingest` | **implemented** | Line 63, N15 attribution |
| `get_ingest_log` | **implemented** | Line 42, N15 attribution |
| `chronicle_day` | **implemented** | Line 14, N15 attribution |
| `chronicle_since` | **implemented** | Line 17, N15 attribution |
| `add_timeline_entry` | **implemented** | Line 10, "N15 thin put_page type=timeline" |

Current ledger state: `docs/OPS-PARITY-LEDGER.md` line 123 still contains historical schema v7 reference, line 132 contains a historical short note, and line 135 contains the old 10-test-era hard-audit claim. No fresh 2026-08-04 N15 ledger reconciliation row has been written yet. Ledger reconciliation is a required post-audit process step after PASS, to be performed by the parent under the node rules; the audit itself does not edit the ledger. No other operation may be newly attributed to N15.

---

## P0 — Blocks Done

None.

---

## P1 — Must Fix Before Marking Done

None.

---

## P2 — Advisory; Document Before Close

**P2-1 — Schema version resolved.** Plan P2-1 noted "v12 at plan-draft time" drift risk. Implementation confirms v12 as the live schema version. No intermediate N14 or other node consumed version 12. Recorded: `migrate.cpp:261` conditional `if (ver < 12)` and evidence report confirms schema_version=12.

**P2-2 — Ledger reconciliation pending.** Plan P2-2 noted ledger historical-note inconsistency. Current `docs/OPS-PARITY-LEDGER.md` line 123 still references historical schema v7, line 132 contains a historical short note, and line 135 contains the old 10-test-era hard-audit claim. Fresh 2026-08-04 N15 ledger reconciliation must be performed by the parent as a required post-audit process step; the audit does not edit the ledger. Required before marking N15 done.

**P2-3 — Parallel slice ownership not exercised.** Plan P2-3 advised explicit file ownership if using parallel slices. N15 was implemented without multi-subagent parallelism. Not applicable. No conflict observed.

---

## Rollback Readiness (§ Rollback, plan lines 127-133)

| Scenario | Mitigation | Status |
|----------|------------|--------|
| Verification fails | Keep ops unregistered or disabled; existing page/search/N13 sync remain usable | All six ops registered and passing; rollback unnecessary |
| v12 migration issue | Do not downgrade; restore pre-v12 backup | Migration passed fresh/populated/idempotent/rollback tests; no field issue |
| Provenance logging causes operational issue | Stop automatic logging; do not weaken source validation or MCP denial | No operational issue observed; counter-only summaries safe |
| Remote write abuse | Keep allow-write omitted by default; revert handler/API if needed | Remote writes default-deny; authorized allowlist tested and safe |

All four rollback scenarios addressed and tested. No rollback required.

---

## Exclusions Honored (§ Scope and exclusions, plan lines 36-40)

| Exclusion | Verification |
|-----------|--------------|
| No new timeline/event table | `migrate.cpp` contains no v12 event/timeline table; only `ingest_log` schema update |
| No out-of-scope work claimed | Ledger attributes only six N15 operations; no work claimed from later nodes |
| No upstream PostgreSQL/JSONB chronology | Chronicle implemented as SQLite page-activity queries, not PG JSONB |
| No week/narrative/kind projections | `chronicle_day/since` return page rows only; no week/narrative/kind fields |
| No schema downgrade tool | No downgrade code present; plan explicitly unsupported |
| No arbitrary remote filesystem access | Import/sync validate source; no MCP filesystem-enumeration route |
| No model/provider configuration change | Evidence report line 56 confirms no change |

All seven exclusions verified.

---

## Dependencies (§ Depends on, plan line 4)

| Dependency | Status | Evidence |
|------------|--------|----------|
| N1 (write provenance, MCP default-deny) | done, PASS | VERIFY-REPORT.md line 31 |
| N2 (page/version/link contracts) | done, PASS | VERIFY-REPORT.md line 32 |
| N2.5 (canonical source ids, remote authorization) | done, PASS | VERIFY-REPORT.md line 33 |
| N5 (import hooks) | done, PASS | VERIFY-REPORT.md line 36 |
| N7 (authenticated loopback MCP) | done, PASS | VERIFY-REPORT.md line 38 |
| N8 (brain isolation) | done, PASS | VERIFY-REPORT.md line 39 |
| N11 (Windows quality evidence) | done, PASS | VERIFY-REPORT.md line 42 |
| N13 (source-scoped live-sync) | done, PASS | VERIFY-REPORT.md line 45 |

All eight dependencies satisfied with PASS audits before this outcome audit.

---

## Conclusion

The N15 implementation fully satisfies the approved plan (2026-07-30 PASS). All seven deliverables are present and correct. All 12 acceptance assertions pass with explicit evidence. The full test matrix (7 categories) executed and passed. Native Windows x64 MSVC `/std:c++20` verification recorded 25 PASS/0 FAIL tests (≥N13 baseline 21 + N15). Security requirements (MCP default-deny before mutation, source canonicalization/authorization, bound SQL, size/JSON validation, counter-only summaries, temp-brain-only tests, transactional migration, FK verification, no secrets, no config change) are satisfied. Build exit 0, test exit 0, CLI smoke PASS. Exactly six operations are within scope. All exclusions honored. All dependencies met. No P0 or P1 findings. P2-1 and P2-3 resolved or not applicable; P2-2 requires parent ledger reconciliation as a post-audit process step. Rollback readiness confirmed and unnecessary.

**VERDICT: PASS** — N15 plan status may be advanced to `done` after the parent completes the required plan-status update and ledger reconciliation per node rules. Implementation is complete and correct against the approved plan baseline.
