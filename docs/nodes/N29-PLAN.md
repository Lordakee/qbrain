# N29 Plan - Strict Audit Artifact Reconciliation

**Status**: done (outcome audit PASS 2026-07-29)  
**Depends on**: N11 outcome PASS; existing N1, N21, N22, N23 implementation artifacts  
**Plan audit**: PASS (`docs/nodes/N29-PLAN-AUDIT.md`)  
**Outcome audit**: PASS (`docs/nodes/N29-HARD-AUDIT.md`)  

## Goal

Repair historical node-gate artifacts that do not satisfy the current `AGENTS.md` / `docs/nodes/README.md` hard requirements, without changing product behavior:

1. N1 has a plan audit PASS and outcome evidence, but `N1-PLAN.md` still says `approved` and `N1-HARD-AUDIT.md` lacks an explicit `Auditor: Claude Code` field.
2. N21, N22, and N23 are marked done, but their plan audit files are missing and their hard audit files are placeholder text rather than node-specific Claude Code outcome audits.
3. Re-establish node-specific audit files for the affected nodes, then mark only audited PASS nodes as done.

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| none | Governance/artifact reconciliation only; no new runtime ops. |

## Deliverables

- `docs/nodes/N1-HARD-AUDIT.md` contains a real Claude Code outcome audit with `VERDICT`, `Auditor`, acceptance evidence, deliverables check, and P0/P1/P2.
- `docs/nodes/N1-PLAN.md` status is `done` only after the N1 re-audit PASS.
- `docs/nodes/N21-PLAN-AUDIT.md`, `N22-PLAN-AUDIT.md`, and `N23-PLAN-AUDIT.md` exist and contain Claude Code plan audit verdicts.
- `docs/nodes/N21-HARD-AUDIT.md`, `N22-HARD-AUDIT.md`, and `N23-HARD-AUDIT.md` are replaced with real Claude Code outcome audits, not placeholders.
- `docs/nodes/N29-HARD-AUDIT.md` records the reconciliation outcome and evidence.
- `docs/OPS-PARITY-LEDGER.md` gets a short N29 note only after all affected node audits PASS.

## Tests

- `scripts\build-tests-cl.ps1` or `build\cl\qbrain_tests.exe` shows all tests PASS; current expected count is **18/18 PASS**.
- N21-N23 outcome evidence references `tests/test_n20_23.cpp` and relevant code paths for takes, code-intel traversal, and chronicle ops.
- N1 outcome evidence references existing write/default-deny/embed/provenance coverage and current green unit suite.

## Acceptance assertions (falsifiable)

1. N1, N21, N22, and N23 each have node-specific hard audit files with `Auditor: Claude Code`, `VERDICT: PASS`, acceptance evidence, and no P0/P1.
2. N21, N22, and N23 each have node-specific plan audit files with `Auditor: Claude Code`, `VERDICT: PASS`, and no P0/P1.
3. Affected plan statuses are consistent with audit state: no node is marked `done` unless both its plan and outcome gates PASS.
4. No product code changes are introduced by N29; any edits are docs/audit metadata only unless a Claude audit explicitly fails and demands code remediation.
5. N29 outcome audit PASS exists before N29 is marked done.

## Rollback

- If any node audit FAILs or Claude Code is unavailable, stop with that node not reconciled; do not mark it done.
- Keep old placeholder text out of final PASS artifacts; if historical context is useful, move it under a clearly marked historical note after the real audit result.

## Security notes

- Audits are read-only; no secrets should be included in audit files.
- No MCP write behavior changes are in scope.
- No model or provider configuration changes are in scope.
- Do not weaken prior default-deny or loopback/token claims while editing docs.

## Parallelism notes

- After N29 plan audit PASS only, N21, N22, and N23 plan/outcome audit requests may run in parallel because their scopes are disjoint.
- Parent agent owns writing final audit files, test evidence, N29 outcome audit, and status/ledger updates.
