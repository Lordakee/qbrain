# N18 PLAN AUDIT

**VERDICT: PASS**
**Auditor: Claude Code**
**Plan**: `docs/nodes/N18-PLAN.md`
**Date**: 2026-07-30
**Scope**: Plan only. No implementation, build, tests, network calls, or files were modified. The existing analytics implementation, `tests/test_analytics.cpp`, the 2026-07-26 `N18-HARD-AUDIT.md`, and historical combined-wave evidence are not gate artifacts for this refreshed loop and were not used as evidence for this verdict.

## Checklist

| # | Item | Status | Finding |
|---|------|--------|---------|
| 1 | Goal clear and scoped | **PASS** | Three named heuristic-only operations with explicit, enumerated exclusions. No LLM, no graph repair, no schema migration, no orphan analysis, no source creation, no fact provenance backfill, no full gbrain graph-analysis parity. Wave 3 boundary and Wave 4 gate relationship are stated. |
| 2 | Acceptance assertions falsifiable | **PASS** | All 16 assertions are binary and measurable: exact numeric thresholds (>20 outgoing rows, 0-200 clamp, <=512-byte detail), named error codes, byte-identical repeated output, full-snapshot equality before/after every call type, at least 21 tests including `test_n18`, and explicit no-N30 and no-migration assertions. No assertion requires subjective judgment. |
| 3 | Tests specified | **PASS** | Nine test matrices defined with concrete preconditions, fixture structures, and expected outcomes: native MSVC build baseline, anomaly fixture (cross-source, deleted/missing/live, degree thresholds), contradiction fixture (every predicate family, null-page/dangling/other-source exclusion, canonical pair order), expert fixture (live/deleted endpoints, tie-breaking, zero-inbound exclusion), limit/source validation, remote authorization and brain isolation, read-only full-snapshot, registry/MCP security and runtime, and evidence manifest. Each matrix maps directly to one or more acceptance assertions. |
| 4 | Ledger impact listed | **PASS** | Exactly three rows enumerated (`find_anomalies`, `find_contradictions`, `find_experts`) with scope and exact N18 subset documented. Ledger reconciliation explicitly deferred to after a node-specific outcome-audit PASS only; no premature claims. |
| 5 | Security reviewed | **PASS** | Covers: (a) canonical source validation and existence check before analytics SQL; (b) remote allowlist enforcement, fail-closed, without leaking slug/entity existence; (c) `allow_write=true` explicitly does not bypass read authorization; (d) bound SQL parameters for `source_id` and `limit` - no concatenation; (e) null-page legacy fact exclusion with explicit reasoning that assigning them to `default` would create a cross-source disclosure risk; (f) detail rendering bounded and escaped, no body/fact record/secret/token/provider-response output; (g) no model/provider/baseURL/API-key/reasoning/context/compression setting change; (h) test isolation using disposable databases and no production `%LOCALAPPDATA%\Qbrain` access. |
| 6 | Dependencies sane | **PASS** | Declared `N1-N13` with specific contracts named: N1 MCP default-deny / read-operation scope, N2 active-page/soft-delete semantics, N2.5 canonical source ids / remote allowlist rules, N3 link storage semantics, N7 authenticated loopback MCP, N8 selected-brain isolation, N10/N13 page-owned facts, and N13 native test baseline. Direct plan-audit and outcome-audit PASS artifacts verified for N1, N2, N2.5, N3, N7, N8, N10, N12, and N13. N4a, N5, N6, N9, and N11 are confirmed done transitively: N12-PLAN-AUDIT (PASS 2026-07-29) explicitly verifies N1-N11 gate artifacts as a prerequisite, and N13-PLAN-AUDIT (PASS 2026-07-29) confirms N1-N12; ledger records 18/18 PASS at N11 closeout. See P2-1 for a minor documentation note. |
| 7 | Windows-native / C++20 / MSVC fit | **PASS** | All build and test paths use `scripts/build-tests-cl.ps1` and `qbrain_tests.exe`. Assertion 15 requires `/std:c++20`, exact MSVC `cl.exe` version, Windows/x64 markers, exact commands and exit codes, and at least 21 registered tests. `scripts/n18-verify.ps1` runs from a unique temporary `LOCALAPPDATA` path; no WSL, Docker, or new third-party dependency is introduced. |
| 8 | N2.5 source validation and remote allowlist | **PASS** | Fully specified: 1-64 ASCII bytes, `^[A-Za-z0-9_-]+$`, not a case-insensitive Win32 reserved device name, lowercase canonical identity. Read must verify registered source without calling a create-on-read path such as `ensure_source`. Remote callers may read `default`; non-default requires case-insensitive membership in `mcp.allowed_sources`. `allow_write=true` explicitly does not bypass this read authorization. Invalid, unknown, and unauthorized sources fail before analytics SQL with the declared structured-error envelope and without echoing the raw untrusted value. |
| 9 | N8 selected-brain isolation | **PASS** | Test matrix 6 seeds two physical brain databases with identical source ids and slugs but distinct sentinel values (anomaly targets, contradiction object sentinels, expert ranks), invokes every operation on one selected brain, and asserts that no sentinel from the decoy brain appears. Assertion 12 covers this directly. |
| 10 | Page-owned facts and null/dangling ownership | **PASS** | `find_contradictions` requires `facts.active = 1` inner-joined through an active page in the requested source. `page_id IS NULL`, dangling `page_id`, soft-deleted owner, and other-source owner are all explicitly excluded. The plan prohibits assigning null-page facts to `default`, inferring provenance from `entity_slug`, backfilling, deleting, or otherwise mutating them. Assertion 7 and the security notes explain the risk. Test matrix 3 seeds null-page and dangling legacy facts and proves exclusion from every source without mutation. |
| 11 | Deterministic and bounded analytics | **PASS** | Final ordering is fully content-specified for all three operations: kind rank then origin slug bytewise then target slug bytewise then count descending (`find_anomalies`); entity slug bytewise then kind rank then folded predicate tuple then object tuple then fact-id tuple as invisible tie-break (`find_contradictions`); `inbound_count DESC` then slug `ASC` (`find_experts`). SQL row order, insertion encounter order, `unordered_map`, and `unordered_set` iteration are explicitly prohibited from determining output. Detail text is at most 512 bytes after code-point-safe truncation with a truncation marker. Assertion 11 makes repeated-call byte-identity falsifiable. |
| 12 | Read-only snapshot invariants | **PASS** | Test matrix 7 requires a deterministic full logical snapshot (all tables, all columns, all rows including pages, links, facts, sources, jobs, config, logs, sequences) before and after each call type (successful, empty, malformed, unknown source, unauthorized, limit-clamped). Every before/after pair must match. No write, repair, backfill, job enqueue, access-timestamp update, or WAL-visible logical state change is permitted. Assertion 13 covers this. |
| 13 | Rollback | **PASS** | Operations kept unavailable if selected-source filtering, remote authorization, deterministic output, or read-only behavior cannot be maintained. Clearing `mcp.allowed_sources` restores remote access to `default` only. Revert analytics signatures/handlers/tests together. No stored-data transformation is planned, so no database downgrade or provenance rewrite is required. Stop-and-re-plan rule if a migration is discovered as necessary. Verification uses disposable databases; production data is never modified. |
| 14 | Explicit no-N30 rule | **PASS** | Dedicated section: "N30 is not a dependency, coordinator, plan, deliverable, evidence container, audit substitute, or follow-up for N18. No `N30-*` file is created, read as a gate, or required." Assertion 16 requires evidence manifest to include an explicit marker that no N30 artifact or model-configuration change was used. |
| 15 | Node-process compliance | **PASS** | Plan status is `draft` at audit time. Outcome audit is `pending`. Process note correctly states that the 2026-07-26 plan/audit, existing implementation, `test_analytics.cpp`, historical hard audit, and combined wave evidence are context only and cannot satisfy either current gate. Acceptance assertion 1 makes implementation contingent on this plan-audit PASS. Parallelism constraints (subagents, parent ownership of audits, shared hot-file merge review) are documented. |
| 16 | Contradiction / internal consistency | **PASS** | Source-auth contract for reads (allowlist required for non-default remote) is consistent across the Shared contract, individual operation contracts, tests matrix 6, security notes, and acceptance assertions 2-3. The no-`ensure_source` read rule is consistent with rollback (no create-on-read). Null-page exclusion is consistent between the contradiction contract, tests matrix 3, and security notes. The `limit=0` -> empty success rule is consistent between the Shared contract and tests matrix 5. |

## P0 (blocks approval)

None.

## P1

None.

## P2 (non-blocking; outcome auditor should note)

**P2-1 - N4a/N5/N6/N9/N11 plan-audit files not directly read by this plan audit.**
These five nodes are confirmed done transitively: N12-PLAN-AUDIT (Claude Code, PASS 2026-07-29) verifies N1-N11 node-specific gate artifacts before approving N12, and N13-PLAN-AUDIT (Claude Code, PASS 2026-07-29) verifies N1-N12. The `OPS-PARITY-LEDGER.md` records 18/18 PASS at N11 closeout (Wave 6 / N11 quality closeout note). The transitive chain is sufficient for a plan-gate assessment. However, N18's implementation assertion 1 requires individually recording "that N1-N13 have node-specific plan-audit and outcome-audit PASS artifacts" - the outcome auditor should confirm that all five node-specific plan-audit files are present on disk before issuing PASS.

**P2-2 - Concurrent Wave 3 hot-file risk is moot but should be confirmed.**
The plan's parallelism notes identify `src/qbrain/ops/handlers.cpp`, analytics headers, test registration, CMake, and the MSVC script as shared hot files requiring parent-owned merge review with concurrent N14/N15/N16 work. Per the ledger, N14/N15/N16 are already marked implemented and their audits are PASS. The risk of concurrent edits is therefore largely historical. The outcome auditor should confirm no regression in those paths was introduced when integrating N18 work.

**P2-3 - `conflicting_predicates` prefix rule boundary.**
The plan states "exactly one `not_`, `no_`, or `anti_` prefix as opposing its exact base predicate." The exact meaning of "exact base predicate" (i.e., whether `not_foo_bar` opposes `foo_bar` but not `foo`) should be verified by the outcome auditor through the contradiction test matrix. This is a test-coverage question, not a plan defect.

**P2-4 - `parse overflow` definition for `limit`.**
The plan requires "Parse overflow is a structured `invalid_argument` error on field `limit` before querying." The implementation threshold for overflow (e.g., exceeds `UINT32_MAX`, `INT64_MAX`, or a project-defined constant) is left to the implementation. The outcome auditor should verify with the overflow fixture that the chosen threshold is consistent and that values like `99999999999999999999` are rejected via the declared error envelope, not via undefined behavior.

## Conclusion

Claude Code found the N18 plan well-bounded, internally consistent, falsifiable, and fully compliant with Qbrain project governance rules. The plan:

- Defines three heuristic-only read operations with exact numeric thresholds, ordered output contracts, and per-operation exclusion lists that are verifiable without subjectivity.
- Specifies complete source validation, remote allowlist enforcement, selected-brain isolation, page-owned-fact contracts, null/dangling fact exclusion, full-snapshot read-only invariants, and detail bounding - all independently testable.
- Enumerates nine concrete test matrices, sixteen falsifiable acceptance assertions, and a verifiable evidence manifest that explicitly prohibits N30 artifacts and model-configuration changes.
- Documents rollback, a stop-and-re-plan rule for any discovered schema migration, and a no-`ensure_source` read contract.
- Correctly treats the 2026-07-26 plan/audit/implementation/evidence as context only, not as satisfying any current gate.

There are no P0 or P1 findings. The four P2 observations are non-blocking and do not weaken any acceptance assertion.

**The plan's status may be set to `approved` and implementation may begin.**
