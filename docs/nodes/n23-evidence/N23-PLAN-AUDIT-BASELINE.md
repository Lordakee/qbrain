# N23 PLAN AUDIT

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N23-PLAN.md
**Plan SHA-256**: `138d68f638bea13f1bc8198339d6e1d0422fa11a85185c7a49698fde87bb5398`
**Date**: 2026-08-04
**Audit type**: Fresh node-specific plan hard audit under current governance loop

## Checklist

| Item | Status | Notes |
|------|--------|-------|
| Goal clear and scoped | **PASS** | Re-verify and correct exactly three Chronicle operations: `chronicle_on_this_day`, `chronicle_last_seen`, and `chronicle_backfill`. Explicit Qbrain page-activity/tagging subset boundaries stated; no upstream extraction/narrative/diary claim. |
| Acceptance falsifiable | **PASS** | 15 numbered assertions covering immutable pre-corrective gate, source/brain isolation, prior-year semantics, exact entity matching, eligible-type filtering, idempotent transactions, default-deny, structured errors, deterministic output, snapshot proofs, and native evidence with frozen test counts. |
| Tests specified | **PASS** | 8-category matrix: API/source isolation, on-this-day prior-year/anchors/tie-break, last-seen calendar arithmetic/not-found, backfill dry/real/idempotent/transactional, limit/type validation, security default-deny/authorization, snapshot/disclosure proofs, and native regression with registered count ≥27. |
| Ledger impact listed | **PASS** | Explicitly reconciles three historical N23 ledger rows without increasing the implemented-operation total. No N20-N22 or N24+ rows belong to N23. |
| Security reviewed | **PASS** | Activity pattern disclosure noted; source validation before queries; no cross-source entity disclosure in not_found; backfill MCP write-default-deny with explicit allow-write gate; bound predicates before limits; no bodies/paths/secrets in output; tests use temp roots only; no provider/network/credential access; no protected config modification. |
| Dependencies sane | **PASS** | N1 (scope/write-deny), N2 (active pages/tags), N2.5 (source auth), N8 (brain routing), N11 (Windows evidence), N15 (UTC chronicle), N19 (strict patterns). All dependency plan/plan-audit/hard-audit SHA-256 values verified and match. Planning baseline Git commit declared. |
| Fits master plan / Windows-native C++ | **PASS** | Source-scoped SQL queries with bound parameters, strict UTC date parsing (`YYYY-MM-DD`), prior-year filtering via substring and year arithmetic, signed calendar-day difference calculation, explicit transactional tagging with SQLite IMMEDIATE, UTF-8 validation, deterministic ordering with id tie-breaks - all native Windows x64 MSVC C++20. No WSL, Docker, or platform-specific unix dependencies. |

## Findings

### P0 (blocks approval)

None.

### P1

None.

### P2 (non-blocking; documented for implementation awareness)

1. **Pre-corrective schema gate complexity**: The immutable gate requirement (lines 60-61) binds approved plan/audit/binary SHA-256, scoped manifest, isolated temporary root, schema v12 validation, and cleanup proof before any corrective edit. This is a defensive correctness measure but adds process overhead. The plan acknowledges gate failure returns the plan to `draft` for revision, which is appropriate handling.

2. **Plan length and detail density**: 198 lines with extensive normative contracts, 8-category test matrix, 15 acceptance assertions, and detailed error/output specifications. This exceeds typical plan brevity but is justified for a correctness closure over three existing non-conformant operations. The detail reduces implementation ambiguity.

3. **Ambient source injection exclusion**: Lines 72-83 require excluding N23 from `QBRAIN_SOURCE` injection such that omitted `source_id` becomes literal `"default"` independent of environment. Current `src/qbrain/mcp/server.cpp:72-83` does not exclude N23 operations, confirming the baseline gap. Implementation must add all three operations to the exclusion list.

4. **Test count derivation**: Acceptance #12 requires "at or above 27 with every test PASS and one N23 PASS" without pre-calculating the exact final count. The verifier must derive this from actual registration. The N19 baseline was 26/26; adding one focused N23 test yields 27 minimum, but shared hot-file merges could affect registration order.

5. **Ledger reconciliation timing**: The plan defers ledger note updates until after outcome PASS (line 7 of Deliverables, acceptance #15). This is correct gate ordering but means the ledger will temporarily show stale notes during the approved-but-not-done window.

6. **Shared hot files**: `src/qbrain/ops/handlers.cpp`, `src/qbrain/mcp/server.cpp`, `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1` are identified as Wave 5 hot files (lines 195-196). Parallelism notes assign merge serialization and review to the parent. This is prudent for correctness but may serialize N23 corrective work with other Wave 5 nodes if they are also active.

## Detailed Analysis

### Baseline Accuracy

The plan's "Current baseline and corrective scope" (lines 48-59) accurately describes observed non-conformance:

- **Source-scoping**: Verified absent in `include/qbrain/core/brain.hpp:145-147` (no `source_id` parameters) and `src/qbrain/core/brain.cpp:1098,1133,1148` (no canonical source resolution).
- **on_this_day gaps**: Confirmed in `brain.cpp:1098-1131`: accepts partial dates (lines 1105-1110), no prior-year filter (SQL lines 1113-1120 include current year), no `matched_at`/`years_ago`, no id tie-break (ORDER BY `updated_at DESC` only, line 1117), limit has floor but no upper clamp.
- **last_seen gaps**: Confirmed in `brain.cpp:1133-1146`: global fallback when slug empty (lines 1142-1145), returns only `updated_at` (line 1139), no effective activity calculation, no `days_ago`, no structured `not_found`.
- **backfill gaps**: Confirmed in `brain.cpp:1148-1161`: uses arbitrary `list_pages(limit)` with no type filtering (line 1151), catches/discards errors (lines 1153-1159 empty catch), counts even on existing tags (line 1156 unconditional increment), no dry-run, no transactional boundary, no since.
- **Handler gaps**: Confirmed in `handlers.cpp:2316-2348`: `arg_int` uses permissive `stoi` (line 49-50 of handlers with try/stoi), loose schemas without `additionalProperties=false` (lines 2328, 2338, 2348), no structured errors, no source resolver calls.
- **MCP ambient injection**: Confirmed in `server.cpp:72-83`: `uses_ambient_source()` does NOT exclude `chronicle_on_this_day`, `chronicle_last_seen`, or `chronicle_backfill`, so lines 281-283 inject `QBRAIN_SOURCE` into them.
- **Test coverage**: Confirmed in `tests/test_n20_23.cpp:91-113`: only 23 lines covering basic shape checks; no prior-year, source isolation, strict parsing, idempotency, authorization, dry-run, or transaction rollback tests.

The baseline assessment is factually accurate, not a hypothetical strawman.

### Contract Completeness

The "Normative shared contract" (lines 63-72) and three operation contracts (lines 74-103) provide:
- Allowed fields and defaults (explicit, no silent acceptance)
- Canonical source resolution and authorization order
- Local vs remote allow-write vs source authorization interaction
- Strict numeric parsing (unsigned base-10, no signs/whitespace/decimal/overflow)
- Structured error shape with bounded fields
- SQL predicate-before-limit ordering with deterministic tie-breaks
- UTF-8 and byte-boundary title truncation
- Explicit no-LLM/no-network/no-provider contract

These are testable, security-conscious, and appropriate for a re-verification node.

### Deliverables and Evidence

Lines 104-123 specify:
- Focused file changes (brain.hpp/cpp, optional time_util, handlers.cpp, server.cpp)
- Focused test registration
- Verifier script with immutable pre-corrective gate, manifests, native build logs, snapshots, and VERIFY-REPORT.md
- Explicit no-verdict authority for verifier
- Ledger reconciliation only after outcome PASS

The evidence design supports the acceptance assertions without creating a forged audit pathway.

### Test Matrix Exhaustiveness

The 8-category matrix (lines 125-158) is exceptionally detailed:
1. **Source matrix**: selected/decoy brains and sources with overlap/sentinels - proves isolation
2. **On-this-day matrix**: anchors, leap rules, prior-year exclusion, tie ordering, byte stability, alias/conflict/invalid forms - proves correct semantics
3. **Last-seen matrix**: timestamp priority, deleted/unknown/cross-source, as-of arithmetic, alias equality, UTF-8/4096 boundaries - proves exact calculation and no-disclosure
4. **Backfill matrix**: eligible types, active/deleted, since boundaries, multi-source/brain, existing tags, limit bounds, dry vs real, idempotency, transaction failure - proves safe bulk mutation
5. **Limit/type matrix**: omitted/zero/max/overflow, wrong types, unknown fields, malformed UTF-8 - proves strict validation
6. **Security matrix**: default-deny, authorization order, ambient source non-injection - proves isolation
7. **Snapshot matrix**: before/after full logical state, exact deltas, no cross-contamination - proves nonmutating reads and controlled writes
8. **Native regression**: Windows MSVC, frozen binaries, two identical runs, count ≥27, focused markers - proves platform fit

This level of detail is rare but appropriate for a correctness closure that must prove existing implementations were inadequate.

### Acceptance Assertions

The 15 numbered assertions (lines 159-176) map directly to test matrix categories and provide clear pass/fail gates. They avoid "best effort" or "reasonable" language. Assertion #1 (pre-corrective gate), #11 (snapshot proofs), #12 (native evidence), and #13 (manifest binding) are verifier-enforced. Assertions #2-#10 and #14-#15 are outcome-audit-enforced.

### Rollback and Security

Rollback (lines 177-183) is scoped to N23 artifacts only, preserving user and other-node work. No schema rollback needed since N23 adds no migration. Security notes (lines 185-191) cover disclosure risk, bulk mutation validation, bounds enforcement, test isolation, and no-protected-config modification.

### Parallelism and Hot Files

Parallelism notes (lines 193-198) allow parallel slices but assign audit gates and shared-file serialization to the parent. This prevents audit forgery and merge conflicts.

### Dependency Verification

All declared dependency plan SHA-256 values match on-disk files:
- N1: ✓ `7fc14a6921984f850fdf82b77e8042827de1bd8e97505df5938ce55d4d6a614a`
- N2: ✓ `9a4f99cfd9385e8e245d9828d74ac4ac54bf15320877cb6da0fab7060e149b72`
- N2.5: ✓ `5b7a56bee110eefec199bf2c3b5d0fbc70171ad634923a9a97dc85150d4b2983`
- N8: ✓ `84438fa09559aef0c5471f4de339cad5f710b774e0f61ba7253d285ba89e9449`
- N11: ✓ `1fb0898b37306d56b92b89eef40f824acf26a7a45d9626fbdecfd28fa8ea9b6d`
- N15: ✓ `1ea4a30a2c82b54adf1a1c2722168c9d82e187165214de27606b9a82f6fc88c8`
- N19: ✓ `61b1553403b7afcb1411b6e62ce9788e73981d0c2d00b94135704ed5a32bebb3`

All plan-audit and hard-audit SHA-256 values are also listed in the plan (table lines 36-44) and can be verified at outcome-audit time.

### Master Plan and Ledger Alignment

`docs/08-MASTER-PLAN-GBRAIN-PARITY.md` lists D15 Chronicle (lines 51, 62) as covering `chronicle_*` operations. `docs/OPS-PARITY-LEDGER.md` lines 13-16 show `chronicle_backfill`, `chronicle_last_seen`, and `chronicle_on_this_day` as already `implemented` with N23 attribution and notes "N20–N23" and "tests 13/13; Claude PASS" (lines 150-151). This plan correctly identifies them as historical claims requiring fresh verification (lines 8, 22-23).

### No N30 / No Protected Config

Acceptance #14 explicitly excludes N30 artifacts and protected model/provider configuration. Security notes (lines 191) repeat the no-protected-config constraint. This satisfies `AGENTS.md` section 1 (Iron Laws).

### Windows Native Fit

All specified work is SQLite SQL queries, C++ string processing, UTC date parsing/arithmetic, and transactional logic - no platform-specific barriers. The plan requires native MSVC C++20 builds (acceptance #12, dependency N11), which is the project baseline.

### Honest Qbrain Subset Claims

The plan explicitly states (lines 17-18): "deliberately implements a Qbrain page-activity/tagging subset. It does not claim upstream page-attached timeline-event storage, `who` projections, Chronicle extraction jobs, narrative generation, diary ingestion, salience/confidence, or full gbrain Life Chronicle parity."

Operation contract for `chronicle_backfill` (lines 98-99) repeats: "This Qbrain subset adds the exact tag `chronicle`; it does not enqueue `chronicle_extract`, synthesize timeline rows, inspect bodies/frontmatter, or claim upstream extraction parity."

These are honest scope boundaries, not false parity claims.

### Plan Audit vs Outcome Audit Separation

The plan clearly states (lines 6-8): "No N23 production, test, verifier, evidence, or ledger change may begin while this plan is `draft` or while its fresh plan audit is missing or FAIL." Line 7 of Deliverables: "After outcome PASS only: this plan status and exactly the three N23 ledger row notes." This enforces the gate separation required by `AGENTS.md` section 2.2 and `docs/nodes/README.md`.

## Conclusion

This is a **refreshed correctness closure plan** that accurately identifies non-conformance in existing Chronicle implementations and specifies detailed corrective contracts, exhaustive test matrices, falsifiable acceptance assertions, and strict evidence gates. All dependencies have verified PASS audits, the ledger reconciliation is honest (no new count), the Qbrain subset boundaries are explicit, security requirements are comprehensive, and Windows native C++20 fit is clear.

The plan contains no P0 or P1 blocking issues. Six P2 observations are documented above; all are either justified complexity (pre-corrective gate, test detail, manifest binding) or acknowledged coordination points (hot files, ambient source exclusion, test count derivation, ledger timing).

**VERDICT: PASS**. The plan may be set to `Status: approved`. Implementation, focused tests, verifier, native evidence, and fresh outcome audit by Claude Code are required before marking N23 done or reconciling ledger notes.
