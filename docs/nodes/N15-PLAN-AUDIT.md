# N15 PLAN AUDIT

**VERDICT: PASS**
**Auditor:** Claude Code
**Plan path:** `docs/nodes/N15-PLAN.md`
**Date:** 2026-07-30
**Scope:** Plan-only. No implementation outcomes audited.

---

## Checklist

| # | Item | Result | Note |
|---|------|--------|------|
| 1 | Bounded goal | ✓ | 4 explicit sub-goals; 10+ explicit exclusions including upstream timeline model, N14/N16/N18 work, LLM config changes, new deps, and N30 |
| 2 | 12 falsifiable assertions | ✓ | Exactly 12, each binary-testable against observable evidence |
| 3 | Full test matrix | ✓ | 7 test categories covering all 6 ops, migration, retention/size, chronicle, MCP security, attribution, and evidence manifest |
| 4 | Six ledger ops | ✓ | `list_link_sources`, `log_ingest`, `get_ingest_log`, `chronicle_day`, `chronicle_since`, `add_timeline_entry` — all 6 match ledger exactly; no extra op claimed for N15 |
| 5 | Security | ✓ | Default-deny before SQLite side effects; N2.5 canonicalization; remote allowlist on writes AND sensitive reads; bound SQL params; strict size/JSON validation; counter-only auto-summaries; temp brains in tests only; no production `%LOCALAPPDATA%\Qbrain` mutation; no LLM config read or change |
| 6 | N1–N13 dependencies | ✓ | Explicitly declared: N1, N2, N2.5, N5, N7, N8, N11, N13; consistent with master-plan DAG and ledger |
| 7 | Windows/MSVC C++20 | ✓ | Tests section: "native Windows PowerShell/MSVC C++20 without WSL or Docker"; evidence manifest requires Windows edition, x64, exact `cl.exe` version, `/std:c++20` |
| 8 | v12/populated-v11 transactional/idempotent/rollback migration | ✓ | Deliverable 2 specifies v12 at plan-draft time, transactional, idempotent on fresh and populated v11; migration matrix covers all four cases: fresh, populated-v11, second-run no-op, injected mid-migration failure with byte-equivalent snapshot proof |
| 9 | Source authorization/isolation | ✓ | Per-source pruning atomicity; remote allowlist required for both writes and reads; brain isolation in attribution tests; no silent fallback to default or cross-source exposure |
| 10 | Retention/size bounds | ✓ | `keep_last` missing→100, clamped 1..1000; `event_type` ≤64 B; `path` ≤4096 B; `detail_json` ≤65536 B valid JSON; log limit 1..50; chronicle limit 1..200; exact boundary and boundary+1 cases required in tests |
| 11 | UTC chronicle validation | ✓ | `chronicle_day` accepts only `YYYY-MM-DD`; `chronicle_since` accepts `YYYY-MM-DD` or `YYYY-MM-DD[T ]HH:MM:SSZ`; offsets, overflow, impossible dates, trailing data, and partial strings all fail closed without DB write; leap-year fixture required |
| 12 | Thin timeline subset | ✓ | Explicit: `add_timeline_entry` creates a `type=timeline` page, not an upstream page-attached timeline row; assertion 11 mandates ledger wording accuracy |
| 13 | Rollback | ✓ | 4 rollback cases stated: keep ops unregistered if verification fails; no DB downgrade, back up before v12 migration; stop auto-logging on provenance issue without weakening source validation; remote writes off by default by omitting allow-write |
| 14 | No contradictions | ✓ | "v12 at plan-draft time" is consistent with historical ledger v7 (old implementation, acknowledged as historical evidence only); retrospective re-plan process note cleanly separates historical evidence from current pending gates |
| 15 | No N30 | ✓ | Explicitly excluded in process note and scope exclusions |

---

## P0 — Blocks Approval

None.

---

## P1 — Must Fix Before Implementation

None.

---

## P2 — Non-blocking; Address Before Outcome Audit

**P2-1 — Schema version drift risk.** The plan binds the migration to "v12 at plan-draft time." If any intermediate work (N14 or another in-flight node) has already consumed version 12, the implementer must confirm the live schema version before writing migration code and the outcome audit must record the actual version applied.

**P2-2 — Ledger note inconsistency.** `docs/OPS-PARITY-LEDGER.md` "N14–N16 notes" records "Claude hard audits: N14/N15/N16 PASS" and all six N15 ops as `implemented`. These reflect the historical 2026-07-26 cycle, not the current retrospective re-plan (status: `draft`, plan audit pending). The plan's process note handles this correctly in-document, but the ledger note does not. After outcome audit PASS, the ledger reconciliation should append a clarifying line distinguishing the historical note from the new gate result.

**P2-3 — Parallel slice file ownership.** The parallelism notes list four slices but do not specify which files each slice owns. Both the migration/core slice and the chronicle/query slice touch `brain.hpp` and `brain.cpp`. The parent agent should assign explicit file ownership per slice before subagent dispatch to prevent merge conflicts.

---

## Conclusion

The N15 plan is complete, internally consistent, and satisfies every mandatory audit criterion. The goal is tightly bounded to exactly six ledger ops with explicit exclusions. All 12 assertions are falsifiable. The test matrix is comprehensive and covers migration, retention, chronicle, security, attribution, and evidence requirements. Windows/MSVC C++20, MCP default-deny, source isolation, size bounds, UTC validation, and rollback are all correctly and specifically stated. No P0 or P1 findings. Three P2 notes are advisory and do not block approval.

**VERDICT: PASS** — Plan status may be advanced to `approved`. Implementation may begin.
