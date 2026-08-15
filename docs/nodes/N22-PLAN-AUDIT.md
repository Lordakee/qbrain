# N22 PLAN AUDIT

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N22-PLAN.md
**Plan SHA-256**: `696485222d71c86dff805d5d4d20b22d9433e190ff401de90017894e00a06a1c`
**Date**: 2026-08-04
**Scope**: Plan-only audit. No implementation, build, tests, or outcome evidence was audited.

## Audit basis

Files read during this audit:

- `AGENTS.md` (mandatory hard-gate rules and precedence)
- `docs/nodes/README.md` (ordered loop, no reordering, forbidden actions)
- `docs/nodes/_TEMPLATE-PLAN-AUDIT.md` (required audit artifact shape)
- `docs/nodes/N22-PLAN.md` (subject of this audit; verified SHA-256 match)
- `docs/08-MASTER-PLAN-GBRAIN-PARITY.md` (master plan D20 code-intel domain)
- `docs/OPS-PARITY-LEDGER.md` (ledger ownership and current N22 entries)
- `docs/nodes/N7-HARD-AUDIT.md` (independently verified PASS and hash)
- `docs/nodes/N8-HARD-AUDIT.md` (independently verified PASS and hash)
- `include/qbrain/codeintel/scan.hpp` (current N22 scanner API declaration)
- `src/qbrain/codeintel/scan.cpp` (current `find_callees`, `find_flow`, `find_blast` implementation)
- `src/qbrain/ops/handlers.cpp` (current N22 handler registrations)
- `src/qbrain/mcp/server.cpp` (MCP typed-argument gate and ambient-source exclusion)

Glob confirmed zero `docs/nodes/N30-*.md` files exist.

## Checklist

| # | Item | Status | Notes |
|---|------|--------|-------|
| 1 | Goal clear and bounded | PASS | Four ops; source-scoped; honest heuristic; no AST/tree-sitter |
| 2 | Corrective intent explicit | PASS | Plan states current handlers are non-compliant and will be replaced |
| 3 | Risks identified and mapped | PASS | Six current-risk bullets each addressed by deliverables |
| 4 | Acceptance assertions falsifiable | PASS | 13 numbered independently testable assertions |
| 5 | Tests fully specified | PASS | 9 test groups with exact matrix coverage |
| 6 | Native Windows 11 / MSVC x64 / C++20 | PASS | Required at Tests section top; no WSL/Docker |
| 7 | Test baseline declared | PASS | N19 baseline = 26 tests; new suite must be all-PASS at ≥26 |
| 8 | Dedicated N22 test file required | PASS | `tests/test_n22.cpp` + build wiring |
| 9 | Ledger rows explicit and bounded | PASS | Exactly four: `code_callees`, `code_flow`, `code_blast`, `code_traversal_cache_clear` |
| 10 | Ledger reconciliation gate | PASS | Only after fresh outcome-audit PASS |
| 11 | Dependencies verified | PASS | All audit hashes independently confirmed (see below) |
| 12 | Fits master plan (D20 code-intel) | PASS | D20 scoped as heuristic; plan consistent |
| 13 | Security section present | PASS | Source-sensitive content, SQL binding, input validation, structured errors |
| 14 | No LLM/model/config changes | PASS | Explicitly forbidden in Security and acceptance 11 |
| 15 | Rollback plan present | PASS | Ops unavailable if contract fails; no schema change |
| 16 | No N30 rule enforced | PASS | Dependencies, acceptance 12, test evidence 9 |
| 17 | Subagent PASS prohibition | PASS | Plan states no subagent may mark approved/done or forge PASS |
| 18 | Source-scoped APIs additive intent | PASS | Deliverable 1 clarifies APIs must be added; current code non-compliant |
| 19 | N16 contract reuse specified | PASS | Plan requires N16 enumeration path; current handlers will be replaced |
| 20 | Callee body bounding specified | PASS | Scan ≤10 lines for first `{`, track depth, stop at matching close |
| 21 | Deterministic traversal specified | PASS | Breadth-first, first-discovery, seen set, depth/limit clamps |
| 22 | Blast composition specified | PASS | Category order def/ref/call/callee; first-category-wins deduplication |
| 23 | Symbol/alias grammar specified | PASS | Canonical `symbol`, compatibility `name`/`entry_point`, conflict detection |
| 24 | Strict numeric parsing required | PASS | Full unsigned decimal; clamp to ranges; reject malformed |
| 25 | Source authorization specified | PASS | Omitted→default; local no-allowlist; remote N2.5 auth |
| 26 | MCP typed-argument gate specified | PASS | Plan requires adding N22 ops to typed map |
| 27 | MCP ambient-source exclusion specified | PASS | Plan requires adding N22 reads to exclusion list |
| 28 | Cache-clear local_only correction | PASS | Plan states current `false` will be corrected to `true` |
| 29 | Cache-clear structured JSON specified | PASS | `{"cleared":0,"stateless":true}` shape declared |
| 30 | Read-only evidence specified | PASS | Before/after snapshots; schema v12 unchanged |
| 31 | Evidence manifest specified | PASS | Plan/audit hashes, inputs, binaries, commands, snapshots |
| 32 | Scope exclusions explicit | PASS | No schema change, no N20/N21/N23, no new dependency |

## Findings

### P0 - None (all prior P0 findings resolved)

The previous plan-audit FAIL identified six P0 blockers. All have been resolved in the revised plan:

1. **N7/N8 dependency verification**: This audit independently verified `docs/nodes/N7-HARD-AUDIT.md` exists, contains `VERDICT: PASS`, and matches SHA-256 `307226705f0dc7495b0aa7aeebf88bd807c0216c19cab059cd23d01dd6835421`. Similarly verified `docs/nodes/N8-HARD-AUDIT.md` as `VERDICT: PASS` with SHA-256 `7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9`. **RESOLVED**.

2. **Source-scoped API framing**: The revised plan's Goal section (lines 12-13) explicitly states "The current N22 handlers are known to be non-compliant: they call legacy unscoped scanner APIs... The approved implementation phase will replace/correct those paths." Deliverable 1 (lines 55-57) clarifies "These APIs do not exist in the current header and must be newly declared/implemented. Replace all three current N22 handler calls... the current unscoped calls are explicitly non-compliant." **RESOLVED**.

3. **Handler contract violation acknowledgment**: Same as item 2 above. The plan no longer implies the current code is partially compliant. **RESOLVED**.

4. **MCP typed-argument gate clarity**: Deliverable 6 (line 81) states "Add the currently missing MCP typed-argument maps for all three reads." The plan explicitly acknowledges these maps do not exist. **RESOLVED**.

5. **MCP ambient-source exclusion clarity**: Deliverable 6 (line 82) states "Add the reads to the currently missing ambient-source exclusion policy." The plan explicitly acknowledges this policy does not cover N22 ops. **RESOLVED**.

6. **Cache-clear local_only correction**: Deliverable 7 (line 85) states "Correct the current `code_traversal_cache_clear` registration from `local_only=false` to `local_only=true`." The plan explicitly acknowledges the current value is wrong and will be corrected. **RESOLVED**.

### P1 - None (all prior P1 findings resolved)

The previous plan-audit FAIL identified three P1 issues. All have been resolved:

1. **Callee body-boundary algorithm**: Deliverable 2 (lines 61-62) now specifies: "Starting on the matched declaration line, scan that line and at most the next 10 physical lines for the first `{` byte; whitespace-only lines may be skipped, but the scan never proceeds beyond those 10 lines. If no `{` is found, emit no callee for that definition." The algorithm is now fully specified. **RESOLVED**.

2. **Blast deduplication tie-breaking**: Deliverable 4 (lines 72-73) now specifies: "De-duplicate blast rows by `(source_id, slug, line)` across categories, retaining only the first occurrence under that priority (for example, a line classified as both `def` and `ref` emits only `def`)." The first-category-wins rule is explicit. **RESOLVED**.

3. **N30-absence evidence clarification**: Test evidence assertion 9 (line 126) now states: "Record that... no N30-prefixed file was created, read, or required; the manifest and all build/test/runtime logs contain no N30 file, operation, or coordination reference; no later-node artifact supplied N22 behavior." The requirement is now clear. **RESOLVED**.

### P2 - Non-blocking observations

**P2-01 - Hot-file coordination note for parent agent.**

The plan's Dependencies section (line 176) states: "N20, N21, and N23 may be developed in parallel only if their own plan audits pass and the parent assigns exclusive ownership of shared hot files." The ledger shows N20/N21/N23 are marked `implemented` with `tests 13/13`, suggesting those nodes are complete. If implementation begins before verifying N20/N21/N23 closure status, the parent must confirm no active parallel work exists on `scan.cpp`, `handlers.cpp`, `server.cpp`, `test_main.cpp`, `CMakeLists.txt`, or `build-tests-cl.ps1`.

No plan revision required. This is a process note for the parent agent.

**P2-02 - Source-scoped API naming consistency recommendation.**

The N16 source-scoped APIs use the `_in_source` suffix pattern with `source_id` as the second parameter. For consistency, the N22 implementation should adopt `find_callees_in_source(Brain&, source_id, symbol, limit, page_limit)` and similar signatures. Deliverable 1 allows "equivalently explicit source-required APIs," so this is a non-blocking implementation suggestion.

No plan revision required.

## Disposition of previous FAIL findings

All six P0 blockers and three P1 required actions from the 2026-08-04 FAIL audit have been resolved through explicit plan revisions. The revised plan:

- Acknowledges the current N22 handlers are non-compliant and will be replaced (not "re-verified")
- Clarifies that source-scoped N22 APIs must be added (they do not exist)
- States the current `local_only=false` will be corrected to `true`
- Explicitly notes MCP typed-argument maps and ambient-source exclusions are "currently missing" and will be added
- Specifies the exact callee body-boundary algorithm (scan ≤10 lines for `{`, track depth)
- Specifies first-category-wins deduplication for blast
- Clarifies N30-absence evidence requirements

The plan's internal consistency is excellent: every current-risk bullet (lines 23-30) maps to a specific deliverable, and every deliverable maps to falsifiable acceptance assertions and test coverage.

## Conclusion

The N22 plan is comprehensive, well-scoped, and implementable. The plan correctly identifies all non-compliance gaps in the current N22 code and prescribes explicit corrective deliverables. The corrective/additive intent is clear throughout.

**Strengths**:
- Explicit acknowledgment that current handlers call legacy unscoped APIs and will be replaced
- Source-scoped N22 APIs framed as new additions, not assumptions
- Exact callee body-boundary algorithm with bounded search (≤10 lines)
- First-category-wins blast deduplication fully specified
- MCP gaps ("currently missing") explicitly called out with additive intent
- `local_only=false` correction explicitly stated
- All six current-risk bullets map to numbered deliverables
- 13 falsifiable acceptance assertions with 9 comprehensive test groups
- N7/N8 dependency hashes independently verified as PASS
- No N30 artifact required (verified zero N30 files exist)
- Ledger reconciliation gated on fresh outcome-audit PASS
- Security, rollback, evidence, and scope boundaries are clear

**No blocking issues remain.** All P0 and P1 findings from the previous FAIL audit have been resolved. The two P2 observations are non-blocking process notes for the implementation phase.

**VERDICT: PASS**

The plan may be marked `Status: approved`. Implementation may proceed once the parent confirms no active hot-file conflicts with N20/N21/N23 work (P2-01 note). After implementation, a fresh node-specific Claude Code outcome audit against this approved plan is required before marking the node `done` or reconciling ledger entries.
