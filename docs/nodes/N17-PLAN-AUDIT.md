# N17 PLAN AUDIT

**VERDICT: PASS**

**Auditor**: Claude Code  
**Plan**: docs/nodes/N17-PLAN.md  
**Audited plan SHA-256**: `a72ec794ee27fce4358ba78b1d1d2a351ced2aad58973cb015e5f5810d90ff1a`  
**Date**: 2026-08-04  
**Scope**: Plan only. No implementation audited. This is the required independent re-audit after the 2026-08-04 FAIL. The prior audit findings are rechecked; resolved P0s are confirmed below.

---

## Checklist

| Item | Status | Notes |
|------|--------|-------|
| Goal clear and scoped | ✓ | Three bounded surfaces: replay terminal jobs, job messages, schema v8 verification. Explicit exclusions: N19+, schema v13, job deletion, lineage, cross-brain queues, model config changes |
| Acceptance falsifiable | ✓ | 16 numbered binary assertions covering state guards, exact deltas, validation boundaries, migration contracts, MCP security, snapshot equality, evidence counts |
| Tests specified | ✓ | 8 test matrices: id/state, message validation, list, migration/integrity, registry/MCP, concurrency, exact-delta, evidence manifest. Each maps to acceptance assertions |
| Ledger impact listed | ✓ | Exactly two upstream ops: `replay_job`, `send_job_message`. Plan explicitly excludes `list_job_messages` from ledger count (lines 46-47) |
| Security reviewed | ✓ | Strict id parsing, terminal-state guards, bounded UTF-8/JSON, no payload echo, MCP default-deny, selected-brain isolation, no model/provider config changes |
| Dependencies sane | ✓ | N8, N12, N13, N14, N15 with plan/outcome PASS artifacts verified. N18 baseline documented. Frozen Wave 3 artifacts with SHA-256 hashes |
| Fits master plan / Windows-native C++ | ✓ | Native PowerShell/MSVC C++20, no WSL/Docker, temporary test isolation, ≥26 tests (Wave 3 baseline 25) |
| **Retrospective closure contract** | ✓ | Lines 7-9 correctly state this is re-verification of existing surfaces; historical evidence does not satisfy current gates; corrective work explicitly listed lines 26-37 |
| **Historical divergence acknowledged** | ✓ | Lines 26-37 enumerate known gaps: any-status replay, permissive id parsing, generic errors, unregistered list helper. These are explicit corrective work, not silent compatibility |
| **Schema v8 verification approach** | ✓ | Lines 119-141 clarify v8 exists in the migration chain; N17 verifies DDL/integrity, does not re-implement or create v13; targeted integrity checks if missing |
| **Corrective breaking changes explicit** | ✓ | Lines 26-30 state the `failed`/`completed`-only replay guard is an intentional breaking correctness fix; no silent compatibility for old waiting/any-status callers |
| Rollback | ✓ | Lines 226-230: disable writes if guarantees fail, no schema downgrade, revert API/handler/test slices together, backup before migration |
| No contradictions | ✓ | Replay breaking change is intentional (lines 29-30); schema v8 verification vs implementation distinction is clear (lines 119-141); no N30 (lines 251-255) |

---

## Prior Audit (2026-08-04 FAIL) — P0 Disposition

The 2026-08-04 plan audit returned FAIL with four P0 findings. This re-audit verifies resolution:

### P0-1: Invalid historical-artifact gate substitution — **RESOLVED**

**Prior finding**: Plan treated N17 as both "not yet approved under current rules" and "approved and done" (as a dependency), which was internally inconsistent.

**Resolution verification**: 
- Lines 7-9 now explicitly state: "This is a retrospective re-verification and correctness closure of existing N17 surfaces. The replay/message code, historical ledger claims, tests, and 2026-07-26 `N17-HARD-AUDIT.md` may already exist or have been used in production, but they were produced under the old combined-wave process. They are background evidence only and do not satisfy the current node-specific plan-audit or outcome-audit gates."
- Line 3: Plan status correctly remains `draft` during this audit.
- The plan no longer claims N17 is "done" — it acknowledges surfaces exist and require current-process governance closure.

**Verdict**: RESOLVED. The plan correctly positions N17 as retrospective closure, not fresh implementation.

---

### P0-2: Conflicting replay contract vs existing implementation — **RESOLVED**

**Prior finding**: Plan required `failed`/`completed`-only replay (lines 52-53 in prior version), but existing code at `minions.cpp:300-305` accepts ANY status with no guard, and historical audit evidenced `waiting` state.

**Resolution verification**:
- Lines 26-30 now explicitly acknowledge: "Historical `replay_job` accepts any existing status, and the historical audit even exercised a `waiting` source row. N17 intentionally changes the final contract to `failed`/`completed` only. Replaying `waiting`, `active`, or other nonterminal rows can duplicate work, bypass an active worker's token fence, or replay an operator-paused/cancelled decision. This is an intentional breaking correctness/security fix to an existing production/ledger surface."
- Line 30: "There is no silent compatibility claim for old waiting/any-status callers. They must use the documented N12-N14 lifecycle or N13 `retry_job` path instead."
- Lines 99-100: Contract explicitly restricts replay to `failed` or `completed`; other statuses rejected.
- Lines 177-179 (test matrix): "Seed `failed`, `completed`, `waiting`, `active`, `paused`, `cancelled`, `dead`, and arbitrary statuses. Exactly `failed` and `completed` replay successfully; every rejection preserves the full logical database snapshot."
- Acceptance assertion 3 (line 210): "Replay's intentional breaking correction is enforced: only `failed` and `completed` sources succeed; `waiting`, `active`, `paused`, `cancelled`, `dead`, and arbitrary statuses are rejected to prevent duplicate live work, with no silent compatibility claim for the historical any-status/waiting behavior."

**Verdict**: RESOLVED. The plan explicitly documents this as corrective breaking change with security justification, not a plan/code conflict.

---

### P0-3: Schema v8 already exists; plan claims to own it but doesn't create v13 — **RESOLVED**

**Prior finding**: Plan stated "N17 does not create schema v13" yet also seemed to claim N17 would "implement" v8, which already exists in the migration chain.

**Resolution verification**:
- Lines 119-141 now clarify the v8 verification approach: "The v8 step already exists at `src/qbrain/storage/migrate.cpp` and current databases have already advanced to schema v12. Its DDL and version marker are verification inputs, not fresh N17 implementation. This plan expects the existing v8 DDL below to remain byte/semantically unchanged and does not authorize schema v13 or any rewrite of an already-current database."
- Lines 135-139: "The known corrective storage gap is narrower: current schema-v12 integrity checks may omit `job_messages`, `idx_job_messages_job`, or their required columns. If inspection confirms that omission, N17 adds detection only. It does not add/alter the table, index DDL, schema marker, defaults, or foreign-key policy."
- Lines 36-37: "If current-v12 integrity checks are missing, adding only those targeted checks is corrective work. Any DDL/version mismatch discovered during implementation requires returning this plan to draft for a separately audited migration change."
- Deliverable 3 (line 163): "Verification evidence for the already-existing v8 migration plus targeted current-v12 integrity checks in `src/qbrain/storage/migrate.cpp` for the known missing table/index/column coverage. The v8 DDL, version marker, current schema version 12, and no-FK policy remain unchanged."
- Acceptance assertion 10 (line 217): "Existing v8 DDL and version-marker source remain unchanged while fresh/populated-v7/current-v12 fixtures prove the exact table/index/no-FK shape, idempotent traversal, injected rollback, and targeted damaged-v12 integrity detection without silent repair."

**Verdict**: RESOLVED. The plan correctly distinguishes v8 verification (inputs already exist) from v8 implementation (not in scope). Only targeted integrity-check additions are permitted, and any DDL mismatch stops the node.

---

### P0-4: Acceptance assertion 1 requires non-existent plan-audit artifact — **CONFIRMED GOVERNANCE**

**Prior finding**: Assertion 1 requires Claude Code plan-audit PASS before implementation, but that audit was issuing FAIL, so the plan couldn't be approved yet. This created correct circular dependency enforcement.

**Current status**:
- Acceptance assertion 1 (line 209): "The delivered node is a retrospective correctness closure: the evidence identifies the historical any-status replay, permissive id parsing, generic failures, and unregistered list helper as pre-fix behavior, and proves each named corrective surface without treating historical PASS text as current conformance."
- This audit is the required plan-audit. If this audit returns PASS, the plan may be approved and implementation may begin.
- If this audit returns FAIL again, the plan must be revised and re-audited per AGENTS.md section 2.2.

**Verdict**: CONFIRMED AS CORRECT GOVERNANCE. This is not a plan defect; it's the proper gate enforcement. This audit's verdict determines whether the plan may be approved.

---

## P0 (blocks approval)

None. All four prior P0 findings are resolved or confirmed as correct governance enforcement.

---

## P1

None.

---

## P2 (non-blocking; outcome auditor should verify)

**P2-1: `list_job_messages` scope and registration gap**

Lines 46-47 correctly state: "`list_job_messages` is a Qbrain read helper required to make the inbox usable and testable. It is not an upstream ledger row and must not be added to, counted in, or represented as an implemented operation in the upstream parity ledger."

However, the plan requires `list_job_messages` to be registered as Scope::Read (line 145), tested (lines 126-128, 183-185), and included in MCP/CLI evidence (lines 174-175, 187-188). The current implementation shows `list_job_messages` exists in `minions.cpp:319-336` but is NOT registered in `handlers.cpp`.

**Resolution**: This is explicit corrective work. Line 35 lists "Its Read registration and real MCP/CLI serialization path are new corrective work." The outcome auditor must verify the registration was added during implementation and the three-field exclusion from ledger count (not from functionality) is honored.

**P2-2: Strict id parsing boundaries not yet proven**

Lines 94-96 specify exhaustive rejection of empty input, zero, signs, whitespace, decimal points, suffixes, embedded NUL/control, overflow, and prefix parsing. The existing code at `handlers.cpp:1675,1698` uses `std::stoll` which permits leading whitespace and `+` signs.

**Resolution**: Lines 33-34 list "Strict full-string job-id parsing is new corrective work; historical `std::stoll` prefix/whitespace behavior is not retained." The outcome auditor must verify that implementation uses a compliant parser (e.g., `std::from_chars` with full-consumption check) and test evidence covers the boundary matrix at lines 177-178.

**P2-3: Concurrent claim/send race schedule ambiguity**

Lines 196-198 state: "Race two connections replaying one terminal job and two connections sending to one job. The test must accept and verify the both-success schedule: two successes mean exactly two distinct rows. It must also verify the one-success/one-structured-busy schedule when observed: exactly one row before retry, no loser delta, then one distinct row after retry. It may not assume contention always produces a loser."

This is a sound concurrency contract, but the exact SQLite busy vs success distribution may vary by hardware/WAL/timing. The outcome auditor should verify test implementation correctly handles both schedules rather than assuming one.

**P2-4: Baseline test count arithmetic**

Acceptance assertion 14 (line 221) requires "at least 26 tests including dedicated N17". The Wave 3 baseline (lines 69-75) records 25 tests. The math is correct (25 + N17 ≥ 26), but if any Wave 3 test was removed or N17 adds no test registration, the count might not advance. Outcome auditor should verify exact registered count and that `[PASS] n17` appears in output.

---

## Conclusion

The revised N17 plan resolves all four prior P0 blocking findings:

1. **P0-1 RESOLVED**: Plan correctly positions N17 as retrospective closure of existing surfaces, not as both "draft" and "done" simultaneously.

2. **P0-2 RESOLVED**: Plan explicitly documents the `failed`/`completed`-only replay guard as an intentional breaking correctness/security fix with no silent compatibility, not as a plan/code conflict.

3. **P0-3 RESOLVED**: Plan clarifies v8 exists, N17 verifies it, and only targeted integrity-check additions (not DDL changes) are corrective work. Any schema mismatch stops the node.

4. **P0-4 CONFIRMED**: Acceptance assertion 1 correctly enforces that implementation may not begin until this audit returns PASS. This is proper governance, not a defect.

The plan is internally consistent, falsifiable, implementable, secure, and compliant with Qbrain project rules. The corrective work is explicitly enumerated (lines 26-37): terminal-state guards, strict id parsing, structured errors, typed MCP validation, `list_job_messages` registration, and targeted integrity checks. The three bounded N17 surfaces (replay, messages, v8 verification) fit within the documented dependency chain, preserve Windows-native C++20/MSVC operation, maintain MCP write default-deny, and introduce no model/provider configuration changes.

Four P2 observations are non-blocking and do not weaken any acceptance contract. Implementation may proceed after this audit is accepted and the plan status is changed to `approved`.

**VERDICT: PASS** — Plan status may be set to `approved` and implementation may begin.
