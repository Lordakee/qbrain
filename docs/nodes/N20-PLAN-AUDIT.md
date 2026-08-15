# N20 PLAN AUDIT

**VERDICT: PASS**  
**Auditor**: Claude Code  
**Plan**: docs/nodes/N20-PLAN.md  
**Plan SHA-256**: d6600297081e983876894d9da893f2ff5769c518e74fb72483687eaae1f02787  
**Date**: 2026-08-04

## Prior Audit Disposition

The 2026-08-04 plan audit of draft SHA-256 `debee8cf8621379be8c8bc6063efbe39c72330199262016112113f40462bef4d` returned **VERDICT: FAIL** with 4 P0 and 3 P1 findings. This fresh audit evaluates the revised draft identified above. The prior FAIL does not authorize implementation; only a PASS on this revised draft permits the metadata-only transition to `approved`.

## Checklist

| Item | Status | Notes |
|------|--------|-------|
| Goal clear and scoped | PASS | Exactly six schema-pack operations with explicit bounded subset claims; no upstream compiler/cache/entity-ontology/full-parity over-claiming |
| Acceptance falsifiable | PASS | 16 specific assertions with observable evidence requirements including pre-corrective gate with exact hash bindings, four-cell selected/decoy statistics verification, and 26 -> 27 test count |
| Tests specified | PASS | 10 test categories covering pack/manifest/id/path/reload/source/registry/MCP/snapshot/evidence with specific predicates; dedicated `test_n20` required post-approval |
| Ledger impact listed | PASS | Exactly six rows with honest subset scoping and explicit non-parity disclaimers; reconciliation means note updates after outcome PASS, not new status transitions |
| Security reviewed | PASS | Confinement, validation, bounds, redaction, isolation, read-no-create, DB-only reload, source authorization, and protected-config preservation contracts detailed with specific limits |
| Dependencies sane | PASS | Nine direct dependencies (N1, N2, N2.5, N7, N8, N11, N15, N18, N19) with 18 recorded SHA-256 hashes; all contracts consumed by N20 are specified |
| Fits Windows-native C++ | PASS | Native paths, MSVC build, no WSL/Docker; Win32 reserved-name and reparse-point awareness; `%LOCALAPPDATA%` layout; PowerShell verifier |
| Prospective gate feasible | PASS | Gate uses existing doctor/schema-integrity path against isolated brain; runs after approval but before corrective edits; captures temporal proof via baseline manifest and Git fingerprints |
| Retrospective baseline explicit | PASS | Frozen noncompliant baseline (create-on-read, path disclosure, missing bounds/confinement/validation) identified in the plan; corrective work scope is post-approval deliverables, not pre-approval prerequisites |

## Findings

### P0 (blocks approval)

None. All four P0 findings from the prior audit are resolved.

### P1 (should block unless mitigated)

None. All three P1 findings from the prior audit are resolved.

### P2 (non-blocking but should be addressed)

#### P2-1: Dependency SHA-256 verification deferred to pre-implementation gate

The plan lists exact SHA-256 hashes for all 18 dependency artifacts (9 plan audits plus 9 outcome audits) in the dependency table. Final verification is deferred to pre-implementation gate step 2. The outcome auditor should independently verify all hashes after approval to confirm no dependency corruption occurred between plan approval and gate execution.

**Mitigation**: The gate explicitly fails and returns the plan to `draft` if any dependency hash mismatch is detected. This is a fail-closed design.

#### P2-2: Ledger reconciliation semantics for already-implemented operations

The ledger section states that reconciliation occurs after and only after a fresh N20 outcome-audit PASS and will retain `implemented` only if proven, replace each terse `N20` note with the honest subset/evidence wording, and replace the historical combined N20-N23 summary with a node-specific refreshed-closure note.

The current ledger shows all six operations already marked `implemented`. The plan should clarify whether "retain implemented only if proven" means the status could change or merely that the evidence must support the existing status.

**Mitigation**: The detailed subset wording in the table makes it clear these are honest capability claims, not status changes. The outcome auditor will verify the claims match evidence.

#### P2-3: Test-count baseline and registration verification

The plan states that the test suite must reach at least 27 registered tests after adding dedicated `n20`. The latest N19 evidence establishes a 26-test baseline. If unrelated approved work raises the baseline further, the effective requirement is current baseline plus one rather than a fixed 27.

**Mitigation**: The outcome auditor will verify the exact registered count from the actual `test_main.cpp` and build output. The "at least 27" phrasing permits a higher count if other work was integrated.

#### P2-4: MCP typed-map verification completeness

The plan requires exact typed-argument maps for all six operations, including empty maps for no-argument operations. Current `server.cpp` has typed maps for other operations but not the six N20 operations. The baseline-defects table acknowledges this gap.

**Mitigation**: This is explicitly a planned post-approval deliverable. The outcome auditor must verify all six operations have correct typed maps and that MCP pre-dispatch type checking works as specified.

## Prior FAIL Remediation Summary

| Prior finding | Resolution in revised plan |
|---------------|----------------------------|
| **P0-1**: Create-on-read `ensure_default_pack()` | Baseline-defects table explicitly identifies this gap; Deliverable 1 requires embedded default and read-only discovery; acceptance assertion 3 requires unchanged filesystem snapshots across reads |
| **P0-2**: Retroactive gate infeasibility | The retrospective baseline and prospective work-boundary section clarifies the gate is prospective, not retroactive; it runs after approval but before new corrective implementation |
| **P0-3**: Missing dedicated test | Baseline-defects table acknowledges absence; Deliverable 4 requires `tests/test_n20.cpp` and registration; acceptance assertion 14 requires 27 or more tests |
| **P0-4**: Missing security checks | Deliverable 1 requires confinement, bounds, reparse detection, and size limits; the normative contract specifies all validation; acceptance assertions 4-5 require rejection of unsafe inputs |
| **P1-1**: Path disclosure | Normative contract forbids paths in listing; baseline-defects table notes current exposure; acceptance assertion 6 requires no path/filename/stat metadata |
| **P1-2**: Missing verifier | Deliverable 5 requires `scripts/n20-verify.ps1` and the evidence directory |
| **P1-3**: `schema_stats` contract verification | Normative contract provides complete source resolution, authorization, selected-source filtering, ordering, limit-plus-one, bounds, and exact response requirements |

All prior findings are resolved by making the baseline/corrective boundary explicit, treating current gaps as planned post-approval work, and specifying complete falsifiable contracts.

## Additional Observations

1. **Ambient source exclusion**: The plan correctly requires excluding all six N20 operations from ambient `QBRAIN_SOURCE` injection. Current `server.cpp` does not exclude the N20 operations. This is properly identified as a baseline gap requiring correction in Deliverable 3.
2. **Six-operation scope is honest**: Each operation includes explicit subset/non-parity language. `reload_schema_pack` does not claim upstream cache invalidation; the ontology operations do not claim entity, temporal, confidence, provenance, or full upstream ontology parity.
3. **Windows safety**: Pack-id validation explicitly handles Win32 reserved device names, trailing dot/space, alternate-data-stream syntax, and reparse points. Confinement requires direct-parent equality before opening files.
4. **Rollback**: The plan provides fail-safe guidance to keep or make the operations unavailable if confinement, validation, isolation, authorization, deterministic output, or write gating cannot be maintained.
5. **Parallelism constraints**: Shared hot files are identified and parent coordination is required for integration, complete tests, and both audits. No subagent may author a PASS audit or mark N20 done.

## Conclusion

The revised N20 plan resolves all P0 and P1 findings from the prior FAIL audit. The retrospective baseline is explicit, the prospective gate is temporally feasible, all missing deliverables have falsifiable post-approval contracts, and the six-operation scope is honest with explicit non-parity disclaimers.

The plan may transition to **Status: approved** via metadata-only change by the parent. After approval, the pre-implementation gate must execute successfully before any new N20 production, test, verifier, or runtime-evidence edit. Implementation may then proceed according to the specified deliverables, followed by a fresh node-specific Claude Code outcome audit. Only that outcome audit's PASS permits marking this plan `done` and reconciling the six N20 ledger rows.
