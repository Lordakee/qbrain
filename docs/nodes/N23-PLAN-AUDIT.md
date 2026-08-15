# N23 PLAN AUDIT

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N23-PLAN.md
**Plan SHA-256**: `9601d6bc7665b52ac5871073d900c6b61763d1c5c440d88df68c8fbd6397a940`
**Date**: 2026-08-08
**Audit type**: Fresh revised-plan hard audit retry

---

## Checklist

### 1. Gate Ordering and Status

| # | Check | Result |
|---|-------|--------|
| 1.1 | Plan is `draft`; plan-audit is `pending`; no corrective work permitted while draft or audit missing/FAIL | PASS |
| 1.2 | All seven direct dependencies require plan-audit PASS and outcome-audit PASS before N23 approval | PASS |
| 1.3 | Pre-corrective schema gate must exist, bind approved plan/audit/binary/HEAD/scoped-input facts, prove isolation and cleanup, and remain byte-identical; outcome audit is the final gate before ledger reconciliation | PASS |
| 1.4 | Post-gate shared-input revalidation sequence is ordered: (a) build-script safety edit -> (b) registry.cpp facts bound, no edit -> (c) sidecar created exactly once -> (d) verifier updated to validate sidecar -> before any verifier or pending-evidence edit | PASS |
| 1.5 | Status `done` and ledger note reconciliation are gated exclusively behind outcome-audit PASS | PASS |

### 2. Immutable Gate Preservation

| # | Check | Result |
|---|-------|--------|
| 2.1 | `PRE-CORRECTIVE-SCHEMA-GATE.json`, `N23-PLAN-APPROVED-BASELINE.md`, `N23-PLAN-AUDIT-BASELINE.md`, and `N23-HARD-AUDIT-BASELINE.md` are named as immutable historical facts; the plan states they are never rewritten | PASS |
| 2.2 | Acceptance assertion 1 requires the final verifier to validate the gate's byte-identity and include its hash/facts in the final manifest before accepting native evidence | PASS |
| 2.3 | Rollback clause explicitly states immutable gate and archived governance are retained regardless of build, test, or evidence failure | PASS |
| 2.4 | The "if the gate fails" conditional in the corrective scope section treats the gate as a validation target (already captured), not a re-capture target; contextual markers ("already-built current `qbrain.exe`", "The current gate check found two finite post-gate shared-input drifts") confirm the gate is pre-existing | PASS |

### 3. Unsafe build-cl Process-Management Removal

| # | Check | Result |
|---|-------|--------|
| 3.1 | Exact scope of allowed change defined: remove `Win32_Process` querying and `qbrain.exe` stop/kill/restart; no change to source list, compiler flags, output path, or other process-control behavior | PASS |
| 3.2 | Replacement behavior stated: let MSVC/link fail normally when target executable is locked | PASS |
| 3.3 | Required sidecar evidence for this edit: resulting file hash, byte count, mtime, and exact bounded diff hash | PASS |
| 3.4 | Native regression evidence gate confirms: "the build script contains no process enumeration or termination" before accepting native evidence | PASS |
| 3.5 | Parallelism note designates `scripts/build-cl.ps1` as a serialized shared input; no agent may change it after sidecar capture; a further change returns plan to draft | PASS |

### 4. registry.cpp Binding

| # | Check | Result |
|---|-------|--------|
| 4.1 | `src/qbrain/ops/registry.cpp` is explicitly excluded from N23 deliverables and from corrective edits | PASS |
| 4.2 | Sidecar must bind its observed SHA-256, byte count, creation time, attributes, and last-write time | PASS |
| 4.3 | Verifier must fail closed if any of those values change before or during the native run | PASS |
| 4.4 | Parallelism note designates registry.cpp as a serialized shared input; further change invalidates sidecar and returns plan to draft | PASS |

### 5. Post-Gate Shared-Input Revalidation Contract (Finite and Complete)

| # | Check | Result |
|---|-------|--------|
| 5.1 | Exactly two drifts identified with full before/after SHA-256, byte count, mtime facts; allowlist is finite and closed - a third drift is a hard failure | PASS |
| 5.2 | Sidecar (`POST-GATE-SHARED-INPUT-REVALIDATION-v1.json`) is created exactly once; a pre-existing v1, changed v1, second sidecar, or N30-component path is a hard failure | PASS |
| 5.3 | Sidecar content is fully specified: strict UTF-8 JSON, no duplicate/unknown fields, immutable gate hash + completion time, archived + current approved plan/audit hashes, both paths' before/after facts, exact bounded delta hash | PASS |
| 5.4 | Sidecar hash must appear in `PREBUILD-MANIFEST.json`, `EVIDENCE-MANIFEST.json`, and `VERIFY-REPORT.md` | PASS |
| 5.5 | Sidecar is evidence only; it is not a replacement pre-corrective gate and carries no audit/verdict authority | PASS |
| 5.6 | If either shared input changes again after sidecar capture: retain v1, return plan to draft, revise finite contract, obtain new plan-audit PASS before further verifier/evidence work | PASS |

### 6. Current-vs-Historical Governance Split

| # | Check | Result |
|---|-------|--------|
| 6.1 | Verifier is required to split frozen historical governance (first approved plan/audit archives) from current approved governance (this revised plan/audit) | PASS |
| 6.2 | Acceptance assertion 1 distinguishes "archived historical governance," "current approved governance," and "append-only two-path sidecar" as three separate validation targets | PASS |
| 6.3 | The historical outcome artifact (`N23-HARD-AUDIT-BASELINE.md`) and its peer archives are immutable and do not satisfy the fresh outcome gate for this revision | PASS |
| 6.4 | Verifier has no audit/status/ledger authority; it may not write a verdict or reconcile ledger rows | PASS |

### 7. Exact Evidence Ordering

| # | Check | Result |
|---|-------|--------|
| 7.1 | Sidecar is created before any verifier or pending-evidence edit | PASS |
| 7.2 | Pre-corrective gate must be validated before pending-evidence write | PASS |
| 7.3 | Verifier re-hashes every bound file at each stage boundary | PASS |
| 7.4 | Two frozen-binary full-suite runs are required; count and per-run identity must match; the N19 baseline (26/26) is named; final count must be >=27, include exactly one N23 PASS, and be identical across both runs | PASS |
| 7.5 | Evidence manifests bind plan, plan audit, immutable gate, sidecar, HEAD, scoped inputs, commands, platform, binaries, logs, counts, and hashes | PASS |

### 8. Falsifiable Acceptance

| # | Check | Result |
|---|-------|--------|
| 8.1 | 15 acceptance assertions; each is independently verifiable and not tautological | PASS |
| 8.2 | Specific SHA-256 values, byte counts, file paths, op names, type names, field names, limit bounds, UTC formats, error codes, row shapes, and test counts are named throughout | PASS |
| 8.3 | Snapshot/disclosure matrix requires full logical before/after snapshots for every read, dry-run, rejection, denial, and failure - exact tag-row delta for successful backfill | PASS |
| 8.4 | Test matrix covers all seven dimensions with named edge cases; each matrix item maps to a named acceptance assertion | PASS |
| 8.5 | Acceptance assertion 15 gates ledger reconciliation exclusively behind outcome-audit PASS | PASS |

### 9. Finite Evidence Boundary

| # | Check | Result |
|---|-------|--------|
| 9.1 | Evidence directory is fully enumerated in deliverables: immutable gate, sidecar, prebuild manifest, production build log, test build log, two full-suite logs, focused N23 markers, selected/decoy snapshots, platform/compiler evidence, final manifest, `VERIFY-REPORT.md` | PASS |
| 9.2 | No open-ended "additional files as needed" language | PASS |
| 9.3 | Evidence scope is bounded to N23-owned files; shared input changes are bounded to two paths with closed allowlist | PASS |

### 10. Security

| # | Check | Result |
|---|-------|--------|
| 10.1 | Source existence and authorization validated before any data enumeration or mutation | PASS |
| 10.2 | `not_found` response does not disclose whether another source contains the slug | PASS |
| 10.3 | Backfill is `Scope::Write`, `local_only=true`; remote default-deny enforced; allow-write cannot bypass source authorization | PASS |
| 10.4 | One explicit transaction for backfill real run; busy/locked -> `database_busy`/`database_error`; no per-row exception swallowed; state unchanged on failure | PASS |
| 10.5 | No response or error exposes bodies, frontmatter, paths, allowlists, secrets, provider/model data, or cross-source identifiers | PASS |
| 10.6 | All SQL inputs are bound; source/type/active/date predicates applied before limits | PASS |
| 10.7 | Tests use unique temporary roots, sentinel-only data; no access to `%LOCALAPPDATA%\Qbrain`, real accounts, credentials, or network | PASS |
| 10.8 | Build script may not enumerate, stop, kill, or restart `qbrain.exe`; locked output is a normal build failure | PASS |
| 10.9 | Protected agent/LLM configuration (model name, provider, base URL, API key, reasoning effort, context size, compression threshold) unchanged by this node | PASS |
| 10.10 | Security test matrix: reads with MCP writes disabled; remote non-default unauthorized reads fail before enumeration; ambient `QBRAIN_SOURCE` injection excluded from all three N23 operations | PASS |

### 11. Windows-Native C++20 Fit

| # | Check | Result |
|---|-------|--------|
| 11.1 | N11 dependency explicitly provides Windows x64 MSVC C++20 build/test evidence contract | PASS |
| 11.2 | Build scripts are `build-cl.ps1` and `build-tests-cl.ps1` (MSVC `cl.exe` pipeline) | PASS |
| 11.3 | Acceptance assertion 12: "Native Windows x64 MSVC C++20 production and test builds exit 0" | PASS |
| 11.4 | No POSIX-only APIs, Unix paths, or non-MSVC build assumptions in deliverables | PASS |

### 12. Operation Contract Completeness

| # | Check | Result |
|---|-------|--------|
| 12.1 | `chronicle_on_this_day`: declared-fields-only, strict date/alias forms, alias equality check, prior-year exclusion, one result per page, `matched_at` = later qualifying timestamp, `years_ago` non-negative, predicates-before-limit, `matched_at DESC, id DESC` ordering, 1..200 limit | PASS |
| 12.2 | February 29 as MM-DD alias is valid for recurring leap-day queries; not an error for non-leap-year anchors | PASS |
| 12.3 | `chronicle_last_seen`: required entity, alias equality/conflict, 4096-byte UTF-8 bound, no global fallback, no deleted/other-source/other-brain match, `last_seen` = max(created_at, updated_at), exact signed UTC calendar `days_ago`, structured `not_found` | PASS |
| 12.4 | `chronicle_backfill`: strict since forms per N15, inclusive, 1..1000 limit after predicates, eligible types exactly `meeting`/`conversation`/`calendar-event`, effective-activity-desc/id-desc ordering, no extraction/embedding, dry-run read-only, real-run idempotent one-transaction, exact count invariants | PASS |
| 12.5 | `scanned == eligible` for SQL-filtered subset is explicitly stated as a plan-level assertion; test matrix verifies the invariant | PASS |
| 12.6 | Numeric validation: zero clamps to minimum, above-maximum clamps to maximum; signs, whitespace, decimals, suffixes, overflow are invalid | PASS |
| 12.7 | MCP argument maps use exact types including real Boolean for `dry_run`; `additionalProperties=false` required in registered schema | PASS |
| 12.8 | All three operations default omitted `source_id` to literal `default`, independent of ambient `QBRAIN_SOURCE` | PASS |

### 13. Scope Exclusions

| # | Check | Result |
|---|-------|--------|
| 13.1 | No N20-N22, N24+ operations, schema migration, DDL, or new ledger rows in N23 | PASS |
| 13.2 | No LLM, embedding, rerank, network, filesystem, provider, or API-key work in the three operations | PASS |
| 13.3 | No N30 artifact or reference anywhere in the plan | PASS |
| 13.4 | No claim to upstream page-attached timeline-event storage, `who` projections, Chronicle extraction jobs, narrative generation, diary ingestion, salience/confidence, or full gbrain Life Chronicle parity | PASS |
| 13.5 | Ledger reconciliation updates only three historical row notes; implemented-operation total is unchanged | PASS |

### 14. Rollback Adequacy

| # | Check | Result |
|---|-------|--------|
| 14.1 | Rollback scoped to N23-owned corrective hunks and focused test/verifier/evidence artifacts; user-owned and other-node work preserved | PASS |
| 14.2 | No schema rollback needed (no migration added) | PASS |
| 14.3 | Product rollback for added tags is a plan change requiring re-audit; backfill idempotency does not imply silent reversal | PASS |

---

## Findings

### P0 (Plan-blocking) - None

### P1 (Significant gap) - None

### P2 (Clarity - non-blocking)

**P2-1 - Gate-capture language ambiguity**

The paragraph beginning "After a fresh plan-audit PASS and status `approved`, but before any N23 production...the parent must run the already-built current `qbrain.exe doctor --json`...and capture `docs/nodes/n23-evidence/PRE-CORRECTIVE-SCHEMA-GATE.json`" uses imperative future tense. Combined with the immutability claim in the Process note and the past-tense "The current gate check found two finite post-gate shared-input drifts," the intent is clear: the gate was captured under the first approved baseline and is now a fixed historical artifact. However, the imperative phrasing could be read by an implementer as requiring a fresh capture on this revision's approval, which would violate immutability. A future revision could add a parenthetical such as "(this was performed under the first approved baseline; the gate file is now immutable and must only be verified, not re-created)".

**P2-2 - `effective activity` undefined inline**

The plan delegates "effective activity" to N15 without an inline reminder. Where the phrase appears in backfill ordering and last-seen semantics, a parenthetical "(max(created_at, updated_at) per N15)" would reduce ambiguity for implementers reading the plan in isolation.

**P2-3 - `scanned == eligible` rationale not stated**

The plan asserts the equality but does not explain why both fields are present in the response. A brief inline note (e.g., "both fields are retained for API symmetry; implementations must not relax the SQL predicate to create a distinction") would prevent a well-intentioned implementer from adding a pre-filter scan count.

---

## Conclusion

The plan is complete, internally consistent, and correctly structured. Gate ordering is valid and unambiguous in net effect. The post-gate shared-input revalidation contract names exactly two finite paths, specifies exact before/after facts and a bounded diff hash, places the sidecar before any verifier or evidence edit, and closes against third drifts, second sidecars, and N30 references. The unsafe process-management removal is precisely scoped. The registry.cpp binding is correctly specified as bind-only with fail-closed enforcement. The historical/current governance split is explicitly required of the verifier with no audit authority granted. Acceptance assertions are independently falsifiable and map to specific test matrix items. The security model enforces authorization before data access, write default-deny, transaction atomicity, no per-row exception swallowing, and no cross-source disclosure. All three operation contracts are complete with deterministic ordering, strict type validation, and exact output shapes. The Windows-native C++20 build pipeline is correctly referenced throughout. No P0 or P1 findings were identified. The three P2 items are documentation clarity suggestions and do not affect correctness or implementability.

**VERDICT: PASS**
