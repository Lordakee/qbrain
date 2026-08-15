# N16 Plan Audit - Source-scoped Heuristic Code Intelligence

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: `docs/nodes/N16-PLAN.md`
**Date**: 2026-07-30
**Scope**: Plan-only. No implementation, build, tests, or outcome evidence was audited. Historical N16 plan/audit artifacts and shared wave evidence are not accepted as substitutes for this gate.

## Audit basis

Files read during this audit:

- `AGENTS.md` (hard-gate rules and precedence)
- `docs/nodes/README.md` (ordered loop and forbidden list)
- `docs/nodes/_TEMPLATE-PLAN-AUDIT.md` (required audit shape)
- `docs/nodes/N16-PLAN.md` (subject of this audit)
- `docs/08-MASTER-PLAN-GBRAIN-PARITY.md` (master plan and capability domains)
- `docs/OPS-PARITY-LEDGER.md` (ledger ownership verification)
- `docs/nodes/N1-HARD-AUDIT.md`, `N2-PLAN.md` (N1/N2 contract status)
- `docs/nodes/N2.5-HARD-AUDIT.md` (source-id/allowlist contract)
- `docs/nodes/N7-HARD-AUDIT.md` (authenticated loopback MCP)
- `docs/nodes/N8-HARD-AUDIT.md` (selected-brain isolation)
- `docs/nodes/N12-HARD-AUDIT.md`, `N13-HARD-AUDIT.md`, `N13-PLAN.md` (recent baselines)
- `include/qbrain/codeintel/scan.hpp`, `src/qbrain/codeintel/scan.cpp` (current scanner)
- `include/qbrain/core/brain.hpp` (Brain API surface)
- `src/qbrain/ops/handlers.cpp` (current N16 handler registrations)
- `tests/test_codeintel.cpp`, `tests/test_main.cpp` (existing test registrations)
- `include/qbrain/ops/registry.hpp` (Scope enum and OpContext)

## Checklist

| # | Item | Status | Notes |
|---|------|--------|-------|
| 1 | Goal clear and bounded | PASS | Single wave: re-verify/correct three ops only; no AST, no migration, no new deps |
| 2 | Process note explicit | PASS | Draft status; implementation blocked until plan-audit PASS; historical audit not reused |
| 3 | Risks identified and mapped to deliverables | PASS | Six identified risks each addressed by a numbered deliverable |
| 4 | Acceptance assertions falsifiable | PASS | 12 numbered assertions; each is independently testable and specific |
| 5 | Tests fully specified | PASS | 8 test groups; every acceptance assertion covered; matrix cases explicit |
| 6 | Native Windows 11 / MSVC x64 / C++20 required | PASS | Stated at top of Tests section; no WSL or Docker allowed |
| 7 | Test baseline declared | PASS | N13 baseline = 21 tests; new suite must be all-PASS at >=21 |
| 8 | Dedicated N16 test file required | PASS | `tests/test_n16.cpp` + registration in `test_main.cpp`, CMake, and `build-tests-cl.ps1` |
| 9 | Ledger rows explicit and bounded | PASS | Exactly `code_def`, `code_refs`, `code_callers`; no other rows touched |
| 10 | Ledger reconciliation gate | PASS | Only after both plan-audit and outcome-audit PASS |
| 11 | N2.5 source contract enforced | PASS | Normalize, validate, exist-check, remote allowlist - all before page enumeration |
| 12 | N8 selected-brain isolation enforced | PASS | Two physical databases, sentinel snippets, full logical snapshot hashes |
| 13 | Source predicate in SQL (not post-filter) | PASS | Deliverable 1 explicitly requires `pages.source_id = ?` in bound SQL |
| 14 | Deterministic and bounded read | PASS | `updated_at DESC, id DESC, line ASC`; byte-stable JSON declared; page_limit applied after source predicate |
| 15 | Strict symbol grammar | PASS | 1..256 ASCII; `~?[A-Za-z_$][A-Za-z0-9_$]*` components separated by `::`; explicit reject list |
| 16 | Structured error contract | PASS | `{"error":{"code":"invalid_argument","field":"symbol","message":"..."}}` + nonzero exit; no untrusted echo |
| 17 | Strict numeric parser required | PASS | Entire unsigned base-10 decimal; clamp to 1..200 / 1..2000; signs/whitespace/suffixes rejected |
| 18 | `arg_int` isolation rule | PASS | N16 must use its own or an already-audited strict parser; must not alter permissive behavior for other ops |
| 19 | `source_id` in every JSON hit | PASS | `{source_id, slug, line, snippet, kind}` shape declared |
| 20 | Read-only: no write, no job, no config, no network | PASS | Scope::Read; failure paths verified as read-only; snapshot hash proves no mutation |
| 21 | Security section present and adequate | PASS | Source-sensitive content, SQL binding, escaping, snippet caps, structured errors, temp databases |
| 22 | No LLM/model/config changes | PASS | Explicitly forbidden in Security notes and acceptance assertion 12 |
| 23 | Rollback plan present | PASS | Ops made unavailable if contract cannot be maintained; no schema downgrade needed; no production data touched |
| 24 | No N30 rule enforced | PASS | Stated in Dependencies, acceptance assertion 12, and test evidence assertion 8 |
| 25 | Subagent PASS prohibition | PASS | Plan states no subagent may author a PASS audit or mark the node done |
| 26 | Dependencies verified as done | PASS | N1/N2/N2.5/N7/N8/N11/N12/N13 all carry outcome-audit PASS artifacts (confirmed by reading hard audits) |
| 27 | No dependency on N14/N15/N18/N30 | PASS | Explicitly stated; hot-file coordination is process-only, not a plan gate dependency |
| 28 | Fits master plan (D20 code-intel) | PASS | D20 scoped as heuristic/line-scanner; plan is consistent with master plan |
| 29 | Ledger current N16 entries status | PASS | Plan correctly flags historical entries as unconfirmed until new loop completes |
| 30 | Evidence artifacts specified | PASS | `scripts/n16-verify.ps1`, `docs/nodes/n16-evidence/VERIFY-REPORT.md`, manifest, snapshot hashes |

## Findings

### P0 - Blocks approval

None.

### P1 - Must fix before implementation proceeds

None.

### P2 - Non-blocking; note for implementation phase

**P2-01 - Hot-file coordination with N18 requires explicit sequencing by parent agent.**
`src/qbrain/core/brain.cpp`, `src/qbrain/ops/handlers.cpp`, `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1` are listed as shared hot files with the N14/N15/N16/N18 wave. N14 and N15 are already done. If N18 is still in flight when N16 implementation begins, concurrent edits to these files risk merge conflicts. The plan correctly assigns sequencing, conflict resolution, and integration review to the parent agent, but the parent should verify N18 plan status before launching N16 implementation subagents. No plan revision required.

**P2-02 - Source-aware refactoring scope within `scan.cpp`.**
The current internal anonymous-namespace `scan()` function is shared by all six codeintel functions, including the N22-owned `find_callees`, `find_flow`, and `find_blast`. The plan correctly instructs the implementer to add a source-aware enumeration path in `Brain` and to not alter the existing two-argument `list_pages` contract for unrelated callers. During implementation, care is needed to ensure the N16 deliverable introduces a distinct Brain method or clearly overloaded path that only the three N16 scanner functions invoke, leaving the N22 call paths untouched. No plan revision required; this is an implementation-phase caution.

**P2-03 - Test count increment must be explicitly tracked.**
The plan states the full suite must remain at or above the N13 baseline of 21 tests. `test_main.cpp` currently registers 21 entries including `test_codeintel`. Adding `test_n16` will bring the count to 22 minimum. The plan does not state the expected post-N16 count explicitly (only >=21), which is technically correct. Evidence must record the actual count reached. No plan revision required.

## Conclusion

The N16 plan is complete, self-consistent, and satisfies every mandatory project gate requirement. All six identified risks map directly to specific, falsifiable deliverables and acceptance assertions. The security section correctly addresses source-sensitive snippet exposure, SQL-bound predicates, symbol injection prevention, structured error output, and read-only isolation with full logical snapshot verification for both selected and decoy databases. Dependencies are verified as done. The no-N30 rule, no-LLM-config rule, no-WSL rule, and subagent PASS prohibition are all explicitly honored. Three non-blocking P2 notes are recorded for the implementation phase.

**Set `N16-PLAN.md` status to `approved`. Implementation may begin.**
