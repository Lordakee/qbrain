# N12 HARD AUDIT

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N12-PLAN.md
**Date**: 2026-07-26
**Re-audit**: post-P0-fix

---

## Acceptance Table

| # | Criterion | Evidence | Result |
|---|---|---|---|
| 1 | Rerank fail-open never throws/empties on LLM failure | `test_rerank` PASS in unit suite; injected throw / empty / partial responses 鈫?result set size preserved, audit JSONL grows. Code: `apply_reranker` wraps the LLM call in an outer `try`, guards membership before reordering, never moves-from the head candidate into a fallible LLM call, and restores the original ordering on empty or size-mismatch responses. | **PASS** |
| 2 | claim/complete token-fenced | `test_minions` PASS. Empty claim rejected (`claim_job` returns `nullopt` when `lock_token.empty()`); `complete` with wrong token fails (`WHERE lock_token = ?`); correct token completes; cancel works. `process_one` supplies a non-empty token. Live: `worker --once` exercised the claim path with the `cli-worker` token. | **PASS** |
| 3 | dream dry-run writes nothing; `--apply` only selected phase | Dry-run JSON reports all 5 phases with `dry_run: true` and zero mutations. `doctor` snapshots bracketing the run: `pages\|chunks\|links` before `2\|2\|0`, after `2\|2\|0` 鈥?byte-identical. `dream --apply --phase consolidate` reported **only** `consolidate [ok] facts_titled=2 (102ms)`; no other phase appeared. | **PASS** |
| 4 | schema >= 6 | `doctor` baseline: `schema: v6` on a freshly-initialized `n12verify` brain. 6 鈮?6. | **PASS** |

**4 / 4 criteria satisfied.**

---

## Deliverables

| Deliverable | State |
|---|---|
| Unit suite 8/8 | `rrf`, `vector`, `chunker`, `extract`, `storage`, `mcp`, `rerank`, `minions` 鈥?all `[PASS]` |
| `tests/test_rerank.cpp` | New. Fault-injection: throw / empty / partial LLM responses; asserts size preservation + audit-trail growth |
| `tests/test_minions.cpp` | New. Token-fence matrix: empty claim, wrong-token complete, correct-token complete, cancel |
| `tests/test_mcp.cpp` | Extended. Job lifecycle `submit_job 鈫?list_jobs 鈫?get_job 鈫?cancel_job 鈫?run_dream` |
| `src/qbrain/search/rerank.cpp` + `include/qbrain/search/rerank.hpp` | New. Fail-open reranker |
| `src/qbrain/cycle/`, `include/qbrain/cycle/` | New. Dream cycle with dry-run and phase selection |
| `src/qbrain/jobs/`, `include/qbrain/jobs/` | New. Minion job queue with token-fenced claim/complete |
| `scripts/n12-verify.ps1` | New. Reproducible end-to-end verify harness |
| `scripts/build-tests-cl.ps1` | New. MSVC test build |
| `docs/nodes/N12-PLAN.md`, `docs/nodes/n12-evidence/` | New. Plan + captured evidence |
| Schema migration to v6 | `src/qbrain/storage/migrate.cpp`, `include/qbrain/storage/schema_sql.hpp` |
| CLI surface | `search --rerank --mode` accepted, emits `rerank_score`; `dream --apply --phase`; `worker --once` |

---

## Findings

### P0 鈥?Blocking
**None.** The two P0 defects that failed the prior audit are closed and independently re-verified:

- **P0-1 (rerank fault tolerance)** 鈥?closed. Outer `try` + membership guards + no move-from-head-into-fallible-call + original-order restore on empty/size-mismatch. Covered by `test_rerank`.
- **P0-2 (dream dry-run inertness)** 鈥?closed. Confirmed at the DB level, not merely by the reporter's own claim: independent `doctor` snapshots before and after the dry-run are identical (`2|2|0`).

### P1 鈥?Should fix
1. **Dry-run inertness is verified only on page/chunk/link counts.** The `doctor` triple is a coarse fingerprint. A dry-run that mutated `facts`, `tags`, `versions`, or job rows without changing page/chunk/link cardinality would pass this check. *Recommendation:* extend the verify harness to a full-table row-count or content hash of `brain.db`.
2. **Phase isolation tested on one phase only.** Only `--phase consolidate` was exercised. `orphans`, `extract_facts`, `embed`, and `purge` have no live `--apply` isolation evidence; `purge` in particular is destructive. *Recommendation:* loop the verify script over all five phases.

### P2 鈥?Nice to have
1. **Rerank evidence is single-shot.** The `search --rerank` sample returned exactly one row with `rerank_score: 1.0` 鈥?a degenerate case where reordering is unobservable. A multi-row corpus would demonstrate that reordering actually occurs on the success path, not just that the field is emitted.
2. **Worker token evidence is indirect.** The `worker --once` invocation produced empty stdout; the report attributes the `cli-worker` token to code inspection rather than an observed artifact. A log line or `list_jobs` snapshot showing the held token would make this directly observable.
3. **Embed phase untested end-to-end.** The verify brain has `embedded=0` and no API key, so the `embed` phase reports `waiting_embed_jobs=0` and its real path is unexercised.
4. **`dream` JSON carries `"schema_version": "1"`** as a string while the DB schema is v6. Unrelated namespaces, but the naming invites confusion in logs.

---

## Conclusion

All four acceptance criteria are satisfied by the supplied evidence, with the two previously-failing P0 items now backed by independent DB-level observation rather than self-reported status. The unit suite is green at 8/8, both new test files exercise the specific failure modes the criteria name, and the CLI surface behaves as specified in live runs.

Remaining findings are P1/P2 breadth-of-coverage gaps 鈥?the correctness of what was tested is not in question; the concern is what was *not* tested (four of five dream phases under `--apply`, dry-run inertness beyond three counters, multi-row rerank ordering). None of these block N12.

**VERDICT: PASS** 鈥?proceed to N13. Fold P1-1 and P1-2 into the N13 verify harness.
