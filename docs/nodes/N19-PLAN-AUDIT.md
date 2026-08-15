# N19 PLAN AUDIT

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N19-PLAN.md
**Plan SHA-256**: f1cffc57a0e3d1c7447eb8c0cacfa130a3afeb05f85cdceaba26406fc69f958e
**Date**: 2026-08-04

## Executive Summary

The revised N19 plan successfully addresses both P1 findings from the 2026-08-04 outcome audit (FAIL) and is approved for corrective implementation. The plan introduces a truthful pre-corrective-work schema gate bound to approved-plan/audit/binary/pre-edit hashes with temporal ordering proof, and expands the identity test matrix to require exact SQL-derived counter and schema-version comparisons for all four cells (selected/default, selected/team, decoy/default, decoy/team). The gate mechanism is feasible using the existing `doctor -> Brain::health -> storage::check_schema_integrity` path, artifact fields are sufficient for temporal verification, and all contracts remain coherent with Windows-native MSVC C++20 constraints.

## Checklist

| Item | Status | Notes |
|------|--------|-------|
| Goal clear and scoped | ✓ | Four read operations; corrective scope limited to P1-1 gate evidence and P1-2 identity matrix completion |
| Acceptance falsifiable | ✓ | 15 assertions including new pre-corrective gate with exact hash/temporal bindings (assertion #1) and four-cell exact counter/schema comparisons (assertion #4) |
| Tests specified | ✓ | 10-category matrix with expanded identity test requiring exact SQL comparisons for all counters/schema in selected/default, selected/team, decoy/default, decoy/team |
| Ledger impact listed | ✓ | Exactly four N19 ops; no change from prior plan |
| Security reviewed | ✓ | Source authorization before queries, path redaction, read-only snapshots, bound SQL; no new attack surface |
| Dependencies complete | ✓ | All twenty artifacts for ten dependencies (ten plan audits and ten outcome audits) verified with PASS/SHA-256 |
| Fits Windows-native C++ | ✓ | Uses existing `storage::check_schema_integrity`, native MSVC build/test, no WSL/Docker |
| No N30 / protected config | ✓ | N30 explicitly excluded; no model/provider/LLM configuration changes permitted |

## Findings

### P0 (blocks approval)

None.

### P1 (blocks implementation)

None. Both P1 findings from the 2026-08-04 outcome audit have been resolved in this revised plan:

#### Resolution of P1-1: Pre-implementation schema gate has no contemporaneous temporal evidence

**Original finding**: The first attempt had no contemporaneous before-implementation evidence that the schema v12 gate was checked and passed before implementation began. All schema checks occurred post-implementation (SCHEMA-SMOKE-OUTPUT.txt, test runtime, verifier scripts).

**Plan resolution** (lines 9, 112, 151):
- **New pre-corrective-work gate** after this plan-audit PASS/approval but **before any corrective production/test/verifier/runtime-evidence edit**.
- Uses existing `build\cl\qbrain.exe doctor --json` → `Brain::health` → `storage::check_schema_integrity` path (verified: migrate.cpp:307 exists and returns `SchemaIntegrity` with `schema_version` field).
- Captures `docs/nodes/n19-evidence/PRE-CORRECTIVE-SCHEMA-GATE.json` with:
  - UTC start/end timestamps (temporal ordering proof)
  - Exact command and exit code
  - Approved-plan SHA-256 (binds to this plan after approval)
  - Plan-audit SHA-256 (binds to the PASS audit authorizing corrective work)
  - Binary SHA-256 (proves which qbrain.exe ran the gate)
  - Sorted SHA-256 manifest of every scoped N19 production/test/verifier input **before corrective edits** (enables temporal ordering: gate predates all corrective changes)
  - Parsed `ok` and `schema_version` result
  - Proof that production `%LOCALAPPDATA%\Qbrain` was not touched (isolated test only)
  - Proof that temporary root was removed (cleanup verification)

**Feasibility assessment**:
- Binary exists: `build\cl\qbrain.exe` (verified present)
- `doctor` command already implemented and working (used in first-attempt verification)
- `Brain::health` calls `storage::check_schema_integrity` (migrate.cpp:307-314)
- No pre-approval implementation required: gate runs **after** plan-audit PASS using **already-built** binary
- Temporal ordering provable: pre-edit input manifest captured before corrective edits begin, compared to gate timestamp
- Verifier (line 99) required to "ingest and fail closed on the approved-plan-bound `PRE-CORRECTIVE-SCHEMA-GATE.json`, prove it predates the corrective test/verifier input hashes, and include its hash and parsed result in final evidence"

**Blocking behavior** (line 9): "If the gate fails or reports another version, return this plan to `draft`, revise it to the actual schema/constraints, and obtain another Claude Code plan-audit PASS; do not silently migrate or continue."

**Verdict**: ✓ Resolved. The gate is implementable without authorization loop, uses existing runtime paths, captures sufficient temporal/hash bindings, and has explicit blocking behavior on failure.

---

#### Resolution of P1-2: Identity evidence matrix incomplete for team-source and decoy-brain exact counter comparisons

**Original finding**: The first attempt's identity test (test_n19.cpp:426-462) provided exact SQL-derived counter comparisons only for selected-brain × default-source. The selected-brain × team-source case checked only `pages` exactly and used broad inequality for other counters; the decoy-brain × default-source case used only whole-object inequality without any exact counter assertions. The plan required "Compare **every** returned counter and schema version to direct SQL/integrity evidence" for "two sources and two physical brains."

**Plan resolution** (lines 121-124, 154):

Test matrix #4 (lines 121-124) now explicitly states:
> "Seed exact source-specific active/deleted page, stored chunk/embedded chunk, and link counts in two sources and two physical brains. For selected/default, selected/team, decoy/default, and decoy/team independently, compare `pages`, `chunks`, `embedded_chunks`, `links`, and `schema_version` to direct bound SQL and `storage::check_schema_integrity` results. **Whole-object inequality or comparison with another cell is not a substitute for any exact assertion.**"

Acceptance assertion #4 (line 154) now states:
> "`get_brain_identity` returns the exact declared source-scoped counters and actual schema version from only the selected brain. **Selected/default, selected/team, decoy/default, and decoy/team each compare all four counters and schema version to direct bound SQL/integrity evidence, never broad inequality.** Local output includes the actual opened database path; remote JSON/text contains no `db_path` key or local/decoy path-derived value."

**Required corrections** (outcome audit lines 189-228 provided exact code examples):
1. **Team-source case** (test_n19.cpp ~line 426): Add exact SQL-derived expected values for `chunks`, `embedded_chunks`, and `links` (in addition to existing `pages`); add exact equality assertions for all four counters; add `schema_version == 12` assertion.
2. **Decoy-brain case** (test_n19.cpp ~line 456): Add exact SQL-derived expected values for all four counters via `scalar_for_source(decoy, ...)`; add exact equality assertions for `pages`, `chunks`, `embedded_chunks`, `links`; add `schema_version == 12` assertion.

**Implementability**: The outcome audit provided concrete code examples using the existing `scalar_for_source` test helper pattern already demonstrated in the selected/default case (lines 395-407). No new test infrastructure required.

**Verdict**: ✓ Resolved. The plan now requires exact four-cell SQL-derived comparisons for all counters and schema version, explicitly rejects broad inequality as a substitute, and provides an implementable test pattern.

---

### P2 (advisory, non-blocking)

**P2-1: Gate artifact structure and verifier obligations**

The proposed `PRE-CORRECTIVE-SCHEMA-GATE.json` artifact includes:
- UTC start/end (temporal proof)
- Exact command and exit code
- Approved-plan SHA-256, plan-audit SHA-256, binary SHA-256 (binding proof)
- Sorted SHA-256 manifest of every scoped N19 production/test/verifier input before corrective edits (ordering proof)
- Parsed `ok` and `schema_version` result
- Proof of no production-data access and successful cleanup

The verifier is required to (line 99): "ingest and fail closed on the approved-plan-bound `PRE-CORRECTIVE-SCHEMA-GATE.json`, prove it predates the corrective test/verifier input hashes, and include its hash and parsed result in final evidence."

**Assessment**: The artifact fields are sufficient to prove:
1. The gate ran (timestamps, command, exit code)
2. It passed (parsed `ok == true`, `schema_version == 12`)
3. It was bound to the approved plan/audit/binary (SHA-256 hashes)
4. It predates corrective work (pre-edit input manifest compared to gate timestamp)
5. It was isolated and clean (no production-data access, cleanup verified)

The verifier obligations are clear and testable: ingest gate JSON, verify its hashes match approved artifacts, confirm gate timestamp predates corrective input changes, fail if any check fails.

**Resolution**: No blocking ambiguity. The gate structure and verifier contract are internally coherent and sufficient for temporal verification.

---

**P2-2: Process note clarifies governance without creating implementation authorization**

Plan line 8 (process note) states: "No further N19 production, test, verifier, or runtime-evidence change may begin until this revision receives a fresh node-specific Claude Code plan-audit PASS and is changed to `approved`."

This is a governance constraint, not an acceptance assertion. It does not create a circular dependency because:
1. The plan-audit (this document) is authored independently by Claude Code
2. The audit verdict determines whether the plan may be approved
3. Approval is a status change to the plan document, not an acceptance criterion the implementation must satisfy
4. After approval, the pre-corrective gate (acceptance assertion #1) runs before implementation

**Resolution**: No circular dependency. The process note correctly describes the approval gate without making it an acceptance assertion.

---

**P2-3: Four-cell identity matrix verbose but unambiguous**

The plan specifies exact comparisons for four cells: selected/default, selected/team, decoy/default, decoy/team. Each cell must independently compare all four counters (`pages`, `chunks`, `embedded_chunks`, `links`) and `schema_version` to direct SQL/integrity evidence.

This is verbose (20 exact assertions across four cells) but unambiguous and testable. The outcome audit provided concrete code examples demonstrating the required pattern.

**Resolution**: Verbosity does not block approval. The matrix is complete and implementable.

## Acceptance Assertions Review

All 15 acceptance assertions (lines 149-166) reviewed for falsifiability:

| # | Assertion | Falsifiable | Evidence |
|---|-----------|-------------|----------|
| 1 | Pre-corrective schema gate with exact hashes/temporal proof; v12 compatibility; no migration needed | ✓ | `PRE-CORRECTIVE-SCHEMA-GATE.json`, test suite snapshots, no schema edit |
| 2 | Source defaults to `default`, ignores ambient, verifies existence, rejects invalid/unknown/unauthorized before queries | ✓ | Test matrix #3, MCP typed validation, source resolution/auth code |
| 3 | Local reads need no allowlist; remote non-default requires N2.5 authorization; `allow_write` cannot bypass | ✓ | Test matrix #3, N2.5 auth integration, MCP writes-disabled success |
| 4 | Four-cell exact counter/schema comparisons; local includes path; remote omits it | ✓ | Test matrix #4 with SQL-derived expected values for all cells |
| 5 | `volunteer_context` enforces query/alias and 4096-byte UTF-8 contract; modes use correct helpers | ✓ | Test matrix #5, handler validation, conservative FTS/source-filtered calls |

| 6 | Query/recent modes have stable membership/order with deterministic tie-breaks | ✓ | Test matrix #5 with reverse fixtures and repeated calls |
| 7 | `get_timeline` returns only active source `type=timeline` pages with predicates before limit; ordered correctly | ✓ | Test matrix #6 with source/type/active predicates, ordering verification |
| 8 | `volunteer_chronicle` delegates to N15 `chronicle_since`; seven-day bounded default; no unbounded fallback | ✓ | Test matrix #7 with UTC boundary seam, empty-window no-fallback proof |
| 9 | Limits consume complete unsigned decimal; defaults only when omitted; clamp correctly; reject invalid | ✓ | Test matrix #3 and #9 with strict parsing cases |
| 10 | Validation rejects non-object, wrong types, null, unknown fields, conflicting aliases, invalid dates | ✓ | Test matrix #3 with MCP/local validation matrix |
| 11 | Every row contains canonical source, exact fields, valid UTF-8 bounded display; no bodies/paths except local identity | ✓ | Test matrix #4, #5, #6, #7 with field-set verification, UTF-8 truncation |
| 12 | Full logical snapshots identical before/after for selected and decoy databases across all calls | ✓ | Test matrix #8 with comprehensive snapshot coverage |
| 13 | Two physical brains with overlapping source ids prove selected-brain and source isolation | ✓ | Test matrix #8 with selected/decoy distinct results |
| 14 | Native Windows x64 MSVC evidence records `/std:c++20`, full details, 26 tests including N19, no N30 references | ✓ | Test matrix #1 and #10 with build/suite/manifest requirements |
| 15 | Only after fresh Claude Code outcome-audit PASS may plan become `done`; no N17/N20+/migration/N30/config/commit/push | ✓ | Governance requirement with explicit exclusions |

**Verdict**: All 15 assertions are falsifiable with deterministic evidence requirements. No circular dependencies, no vague qualifiers, no untestable claims.

## Dependencies Verification

All twenty artifacts for the ten dependencies were verified against the plan table (lines 195-206):

**Plan audits** (all PASS):
- N1: SHA-256 `9fd6df77ad905463f34e6873c2220849003679a64c869e5fb1eaffba470f95e6` ✓
- N2: SHA-256 `c34fede88989a9847dd3cad0bf719b6476c28bbfb124cb094d4afbe24d90fb85` ✓
- N2.5: SHA-256 `bd0cf1b5f4dddb9af40168a89d1a87be84d5a4eb2f99872d3389880523617953` ✓
- N3: SHA-256 `ab99d7c2d0553575f16124fba807067a8039839321fb9f3469c949dbdc8a4994` ✓
- N7: SHA-256 `929970318d8fb3043371f82a9208360db7e38e6dd058e37f0eef515534f26d39` ✓
- N8: SHA-256 `7f16263f786315420ed42a7c79350add553ad84b11ce4cd6dbc21b0fdc320570` ✓
- N11: SHA-256 `e157d9f3b6dcbc276b782d960c237d50fed9d4ff5614473678813e27541844a7` ✓
- N15: SHA-256 `01e95a0cc55e4d0580562008a65de2ee941a13a8b37f4fd730389937d5abaef1` ✓
- N16: SHA-256 `ad6794067444a56658d52d23c3ca29f7092cd7024829f1c4313b295b10c77fef` ✓
- N18: SHA-256 `87db9821c255555ab6a42aab8d22cac945a5e0141aeeb3dd02e76a07e743af6d` ✓

**Outcome audits** (all PASS):
- N1: SHA-256 `93f112c13d01864aa701683e2a4dbb3726a763d90b7a113c07dc543af4d31141` ✓
- N2: SHA-256 `e9dc809dcdb73c0757708f81d53daf2fc89394c12cf953e86c0e9de5923a3413` ✓
- N2.5: SHA-256 `dd6e404ab7583af8c6cbecd86179baba3401a1d5ef10f559b2067229a208c8ff` ✓
- N3: SHA-256 `2ca977089b0564f7ff60752c8cff4b968e1f528163239ef4c19e1bd22c094d24` ✓
- N7: SHA-256 `307226705f0dc7495b0aa7aeebf88bd807c0216c19cab059cd23d01dd6835421` ✓
- N8: SHA-256 `7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9` ✓
- N11: SHA-256 `bdefcf26d138b658d31df0b8525c46b776aa5e9086796bcd16696d8b783f2012` ✓
- N15: SHA-256 `9f5f14ab7ed2cf4da50b597f8f861061948d9b65331091a017d677f7b4968c59` ✓
- N16: SHA-256 `591865f6647e175c4aa02ec90abad1075c554eca49e3a15e5f63ad1639c24aba` ✓
- N18: SHA-256 `f09971ecf44ab66129f33ee3b7dad91515aac39d6d330b725916983fcb408053` ✓

**Pre-implementation gate requirement** (line 208): "At the pre-implementation gate, verify that every listed file still exists, still states PASS, and still has the recorded SHA-256."

**Verdict**: All 20 dependency artifacts (10 plan audits + 10 outcome audits) exist with PASS verdicts and verified SHA-256 hashes. The dependency chain is complete and stable.

## Security Review

Security contracts (lines 183-190) reviewed:

1. **Source authorization before queries**: ✓ Acceptance assertions #2-#4 require authorization/existence/validation before data enumeration
2. **Path redaction**: ✓ Assertion #4 requires remote responses omit `db_path` and all path-derived values from JSON and text
3. **Bound SQL inputs**: ✓ Plan line 186 states "Bind all SQL inputs. Query text is length/UTF-8 validated and goes through the established conservative FTS quoting path"
4. **Row limits and UTF-8 truncation**: ✓ Assertion #11 requires "valid UTF-8 bounded display text" with explicit truncation
5. **No error echo of untrusted values**: ✓ Plan line 43 states "Error text does not echo raw untrusted input, local paths, configuration values, tokens, provider/model information, page bodies, or cross-source identifiers"
6. **Full-snapshot evidence**: ✓ Test matrix #8 and assertion #12 require complete before/after schema+data snapshots for selected and decoy databases
7. **No protected configuration changes**: ✓ Plan line 190 and assertion #15 explicitly prohibit model/provider/LLM configuration changes

**No new attack surface**: The corrective work adds only:
- Pre-corrective schema gate artifact (isolated test, no production access)
- Expanded identity test assertions (test-only code)

No handler logic, SQL query structure, or authorization path changes.

**Verdict**: Security contracts are sound and unchanged from the prior plan audit. The corrective scope introduces no new vulnerabilities.

## Scope and Exclusions

**Corrective scope** (line 8): The revision addresses exactly two P1 findings:
1. Missing contemporaneous pre-implementation schema gate evidence → new pre-corrective-work gate
2. Incomplete identity test matrix → expanded four-cell exact counter/schema comparisons

**Unchanged scope**: All other N19 contracts remain identical to the prior plan:
- Four read operations with exact bounded subset claims
- Source-scoped counters, path redaction, conservative search, thin timeline pages, strict Chronicle delegation
- No LLM, embedding, rerank, provider, network, filesystem-write, or database-write work
- Schema v12 with no migration planned
- Native Windows x64 MSVC evidence

**No N30 rule** (lines 167-172): Plan explicitly excludes N30 as a dependency, coordinator, plan, deliverable, evidence container, audit substitute, follow-up, or prerequisite. No `N30-*` file is created, read as a gate, or required. N19 closes through its own node-specific plan and outcome audit loop.

**No protected configuration changes** (lines 190, 165): Planning, implementation, verification, and auditing must not modify any LLM/agent/application base URL, API key, provider, model, reasoning effort, context size, compression threshold, or related protected configuration.

**Verdict**: Scope is clear, bounded to P1 corrections only, and excludes N30/protected-config changes.

## Feasibility Assessment

**Pre-corrective schema gate**:
- Binary exists: `build\cl\qbrain.exe` verified present
- `doctor` command already implemented and working (used in first-attempt verification)
- `Brain::health` → `storage::check_schema_integrity` path verified (migrate.cpp:307)
- No pre-approval implementation required: runs **after** plan-audit PASS using **already-built** binary
- Temporal ordering provable: pre-edit input manifest captured before corrective edits, compared to gate timestamp
- Blocking behavior explicit: failure returns plan to `draft` (line 9)

**Identity matrix expansion**:
- Test helper pattern already exists: `scalar_for_source` demonstrated in selected/default case (test_n19.cpp:395-407)
- Outcome audit provided exact code examples for team-source and decoy-brain corrections (lines 189-228)
- No new test infrastructure required
- Implementable as assertion expansion in existing test matrix

**Verifier obligations**:
- Ingest and parse `PRE-CORRECTIVE-SCHEMA-GATE.json`
- Verify hashes match approved-plan/plan-audit/binary
- Prove gate timestamp predates corrective input changes
- Fail closed if any check fails
- Include gate hash and parsed result in final evidence manifest

**Verdict**: All corrective work is feasible using existing runtime paths, test patterns, and verifier capabilities. No circular dependencies, no authorization loops, no unimplementable requirements.

## Windows-Native MSVC C++20 Fit

**Platform requirements** (lines 105-111, 164):
- Native Windows 11 PowerShell with MSVC x64 and C++20
- WSL and Docker are not part of the build, test, verifier, or runtime path
- Commands: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cl.ps1` and `build-tests-cl.ps1`
- Evidence must record Windows edition/architecture, full `cl.exe` version, `/std:c++20`

**Corrective work compatibility**:
- Pre-corrective gate uses existing `qbrain.exe doctor` (already Windows-native)
- Identity test expansion uses existing C++20 test patterns
- Verifier PowerShell script ingests JSON artifact (Windows-native)
- No new Linux-only dependencies, WSL requirements, or Docker containers

**Verdict**: All corrective work fits Windows-native MSVC C++20 constraints.

## Test Matrix Adequacy

10-category test matrix (lines 104-148) reviewed for P1-correction coverage:

**Category #2 (Schema v12 compatibility)**: Now includes:
- After plan-audit PASS/approval, capture `PRE-CORRECTIVE-SCHEMA-GATE.json` before corrective edits
- Require exit zero, `ok == true`, `schema_version == 12`
- Require exact approved-plan/plan-audit/binary/pre-edit input hashes
- Require isolated `LOCALAPPDATA`, no persisted config, cleanup verification
- Block corrective work on failure or different version

**Category #4 (Identity and path-redaction matrix)**: Now includes:
> "For selected/default, selected/team, decoy/default, and decoy/team independently, compare `pages`, `chunks`, `embedded_chunks`, `links`, and `schema_version` to direct bound SQL and `storage::check_schema_integrity` results. Whole-object inequality or comparison with another cell is not a substitute for any exact assertion."

**Other categories**: Unchanged from prior plan audit (all were complete and adequate).

**Verdict**: Test matrix adequately covers both P1 corrections with concrete, falsifiable requirements.

## Deliverables Review

7 deliverables (lines 85-101) reviewed for P1-correction impact:

1. **Core brain.hpp/cpp** (line 85-90): Unchanged scope (source-scoped identity, db path accessor, source/type enumeration, UTC helper, Chronicle reuse)
2. **Search hybrid.cpp** (line 91): Unchanged scope (deterministic FTS tie-break)
3. **Handlers.cpp** (line 92-96): Unchanged scope (N19 validation, four exact handlers, `list_pages_for_source` call, registrations)
4. **MCP server.cpp** (line 97): Unchanged scope (typed-argument maps, ambient-source exclusion)
5. **tests/test_n19.cpp** (line 98): **Expanded** - "The selected/decoy by default/team identity matrix must compare every returned counter and live schema version to direct SQL/integrity results in all four cells; broad object inequality is not acceptance evidence."
6. **Verifier** (line 99): **Expanded** - "The verifier must ingest and fail closed on the approved-plan-bound `PRE-CORRECTIVE-SCHEMA-GATE.json`, prove it predates the corrective test/verifier input hashes, and include its hash and parsed result in final evidence. It must not claim to retroactively manufacture the failed first attempt's missing gate."
7. **No schema migration** (line 100-101): **Expanded gate requirement** - "The pre-corrective-work gate above must report success and `schema_version == 12` after this revised plan is approved and before corrective edits."

**Verdict**: Deliverables clearly scope P1 corrections (test expansion, verifier gate handling) without changing production code contracts.

## Ledger Impact

Ledger rows (lines 22-33) unchanged from prior plan:

| op | scope and locality | exact N19 subset |
|----|--------------------|------------------|
| `get_brain_identity` | Read, remote-capable subject to source authorization | Selected brain id, authorized canonical source id, live schema version, source-scoped page/chunk/link counters, and local-only actual database path |
| `volunteer_context` | Read, remote-capable subject to source authorization | Zero-LLM conservative FTS pointers for a query, or recent active-page pointers when the query is omitted/blank, from one source |
| `get_timeline` | Read, remote-capable subject to source authorization | Active `type=timeline` pages from one source; the N15 thin timeline-page model, not upstream page-attached timeline rows |
| `volunteer_chronicle` | Read, remote-capable subject to source authorization | N15-style inclusive UTC page activity from one source, with an explicit `since` or a bounded seven-UTC-calendar-day default |

**No N17 job operation, N20+ schema/ontology/takes/code/chronicle operation, or other ledger row belongs to N19** (line 33).

**Verdict**: Ledger impact is clear, bounded to exactly four N19 read operations, and unchanged by P1 corrections.

## Rollback

Rollback section (lines 175-181) reviewed:

- Keep or make the four N19 operations unavailable if contracts cannot be maintained
- No silent fallback to historical unscoped/path-leaking behavior
- Clear `mcp.allowed_sources` to restore remote access to `default` only
- Revert N19 core/path/enumeration API, handlers, MCP typed maps, tests, and verifier together
- No schema downgrade or data rewrite needed (N19 plans no migration)
- Verification touches only disposable databases and temporary `LOCALAPPDATA`

**Corrective-work rollback**: If the pre-corrective gate fails or reports a version other than 12, the plan returns to `draft` for revision and fresh plan-audit (line 9). This is a blocking gate, not a rollback scenario.

**Verdict**: Rollback section is adequate and includes gate-failure blocking behavior.

## Outcome Audit Requirements

Plan line 101 and acceptance assertion #15 require: "After implementation and evidence, obtain a fresh node-specific Claude Code outcome audit against the approved plan. Only after its PASS may the parent set this plan to `done` and reconcile exactly the four N19 ledger rows."

**Outcome audit scope**:
- Must audit implementation vs this approved plan (SHA-256 `f1cffc57a0e3d1c7447eb8c0cacfa130a3afeb05f85cdceaba26406fc69f958e`)
- Must verify all 15 acceptance assertions with deterministic evidence
- Must confirm `PRE-CORRECTIVE-SCHEMA-GATE.json` exists, predates corrective edits, passed, and is bound to correct hashes
- Must verify expanded four-cell identity matrix with exact SQL-derived counter/schema comparisons
- Must confirm no N30 artifacts, no protected configuration changes, no commit/push unless separately requested

**Verdict**: Outcome audit requirements are explicit and complete.

## Comparison to 2026-08-04 Outcome Audit Findings

The 2026-08-04 outcome audit (N19-HARD-AUDIT.md) found VERDICT FAIL with two P1 findings:

### P1-1: Pre-implementation schema gate has no contemporaneous temporal evidence

**Outcome audit finding summary**:
- First attempt had no contemporaneous before-implementation evidence
- All schema v12 checks occurred post-implementation
- Verifier explicitly acknowledged it "does not retroactively manufacture" the gate
- Evidence investigation showed SCHEMA-SMOKE-OUTPUT.txt, test runtime checks, and PREBUILD-MANIFEST.json all post-implementation

**Plan resolution verification**:
- ✓ New pre-corrective-work gate runs **after** plan-audit PASS but **before** corrective edits (line 9)
- ✓ Uses existing `doctor -> Brain::health -> storage::check_schema_integrity` path (feasible without pre-approval implementation)
- ✓ Captures `PRE-CORRECTIVE-SCHEMA-GATE.json` with UTC timestamps, exact hashes, pre-edit input manifest (temporal ordering proof)
- ✓ Verifier required to ingest gate, verify temporal ordering, fail closed on anomalies (line 99)
- ✓ Blocking behavior: failure returns plan to `draft` (line 9)
- ✓ Acceptance assertion #1 requires gate success before corrective work (line 151)

**Verdict**: ✓ RESOLVED. The plan introduces a truthful, implementable, temporally-provable pre-corrective gate that addresses the outcome audit's finding.

---

### P1-2: Identity evidence matrix incomplete for team-source and decoy-brain exact counter comparisons

**Outcome audit finding summary**:
- Plan required "Compare **every** returned counter and schema version to direct SQL/integrity evidence" for "two sources and two physical brains"
- First attempt provided exact comparisons only for selected/default
- Team-source case: only `pages` exact, used broad inequality for others, omitted `schema_version`
- Decoy-brain case: only whole-object inequality, no exact counter/schema comparisons
- Outcome audit provided exact remediation code examples (lines 189-228)

**Plan resolution verification**:
- ✓ Test matrix #4 now explicitly states: "For selected/default, selected/team, decoy/default, and decoy/team independently, compare `pages`, `chunks`, `embedded_chunks`, `links`, and `schema_version` to direct bound SQL and `storage::check_schema_integrity` results" (lines 121-122)
- ✓ Adds: "Whole-object inequality or comparison with another cell is not a substitute for any exact assertion" (line 123)
- ✓ Acceptance assertion #4 now states: "Selected/default, selected/team, decoy/default, and decoy/team each compare all four counters and schema version to direct bound SQL/integrity evidence, never broad inequality" (line 154)
- ✓ Deliverable #5 reinforces: "The selected/decoy by default/team identity matrix must compare every returned counter and live schema version to direct SQL/integrity results in all four cells" (line 98)

**Verdict**: ✓ RESOLVED. The plan now requires exact four-cell SQL-derived comparisons for all counters and schema version, explicitly rejects broad inequality as a substitute.

## Conclusion

The revised N19 plan successfully addresses both P1 findings from the 2026-08-04 outcome audit (FAIL) and is **approved for corrective implementation**.

**Key resolutions**:

1. **Pre-implementation schema gate** (P1-1): The plan introduces a truthful pre-corrective-work gate that runs after plan-audit PASS/approval but before any corrective edits, uses the existing `doctor -> Brain::health -> storage::check_schema_integrity` path, captures `PRE-CORRECTIVE-SCHEMA-GATE.json` with exact hash/temporal bindings, and has explicit blocking behavior on failure. The gate is implementable without authorization loop and provides contemporaneous temporal evidence that was missing from the first attempt.

2. **Identity test matrix** (P1-2): The plan now requires exact SQL-derived counter and schema-version comparisons for all four cells (selected/default, selected/team, decoy/default, decoy/team), explicitly rejects broad inequality as a substitute for exact assertions, and provides an implementable test pattern based on the existing `scalar_for_source` helper.

**No blocking findings remain**:
- Zero P0 findings (approval not blocked)
- Zero P1 findings (both prior P1 findings resolved)
- Three P2 findings (all advisory, non-blocking)

**Approval criteria satisfied**:
- Goal clear and scoped to P1 corrections only ✓
- Acceptance assertions falsifiable with deterministic evidence requirements ✓
- Test matrix comprehensive with P1-correction coverage ✓
- Ledger impact unchanged (four N19 read operations) ✓
- Security contracts sound with no new attack surface ✓
- Dependencies complete (20 artifacts for 10 dependencies with PASS/SHA-256 verified) ✓
- Fits Windows-native MSVC C++20 constraints ✓
- No N30 artifacts or protected configuration changes ✓

**The plan may now be marked `Status: approved` and corrective implementation may begin.**

**Required sequence**:
1. Update N19-PLAN.md from `Status: draft` to `Status: approved`
2. Run pre-corrective gate: `build\cl\qbrain.exe doctor --json` on isolated selected brain, capture `PRE-CORRECTIVE-SCHEMA-GATE.json` with all required fields
3. If gate passes (exit 0, `ok == true`, `schema_version == 12`), proceed to corrective edits
4. If gate fails or reports different version, return plan to `draft`, revise, obtain fresh plan-audit PASS
5. Implement corrective deliverables (expanded identity test, verifier gate handling)
6. Run native Windows x64 MSVC production build + full test suite
7. Generate verifier evidence with gate ingestion and temporal verification
8. Obtain fresh Claude Code outcome audit against approved plan SHA-256 `f1cffc57a0e3d1c7447eb8c0cacfa130a3afeb05f85cdceaba26406fc69f958e`
9. After outcome audit PASS only, mark N19-PLAN.md `Status: done` and reconcile four ledger rows

**VERDICT: PASS**

