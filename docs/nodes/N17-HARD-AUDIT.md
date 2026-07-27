# N17 HARD AUDIT

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N17-PLAN.md
**Date**: 2026-07-26

## Scope

N17 delivers job replay and job-scoped messaging on the minions subsystem, with a schema bump to v8 to carry the `job_messages` table.

| Area | Surface |
| --- | --- |
| Replay | Clone a waiting job into a fresh queued job, preserving payload and lineage |
| Messages | Send and list messages attached to a job id |
| Storage | Migration to schema v8 adding `job_messages` |
| Tests | `tests/test_minions.cpp` extended, full unit suite green |

## Acceptance

| # | Assertion | Evidence | Status |
| --- | --- | --- | --- |
| 1 | replay clones waiting job | test_minions replay PASS | PASS |
| 2 | send/list job messages | test_minions messages PASS | PASS |
| 3 | schema v8 job_messages | migrate v8 | PASS |
| 4 | unit suite | 12/12 including minions | PASS |

All four plan assertions are satisfied with direct test or migration evidence. No assertion was accepted on inspection alone.

## Evidence Detail

**Assertion 1 鈥?replay.** The replay path selects a job in `waiting` state, copies its payload, and enqueues a new job. The test asserts a distinct job id is produced and the source job is left intact, which is the behavior the plan specified.

**Assertion 2 鈥?messages.** Send followed by list round-trips message bodies against the originating job id. Ordering is stable on insert sequence.

**Assertion 3 鈥?schema v8.** `src/qbrain/storage/migrate.cpp` carries the v8 step creating `job_messages`. The migration is additive; no existing table is rewritten or dropped, so the upgrade is safe on populated databases.

**Assertion 4 鈥?suite.** 12 of 12 unit tests pass, including the extended minions coverage. No skips, no expected failures.

## Findings

### P0
None.

### P1
None blocking.

### P2
Further gbrain parity still deferred. The replay surface covers `waiting` jobs only; replay of terminal states (failed, cancelled) is not in N17 scope and remains open for a later node. Message retention and pruning are likewise unaddressed, so `job_messages` grows without bound in long-lived deployments. Neither gap contradicts the plan.

## Risk Notes

The schema change is forward-only. There is no down-migration for v8, which matches the existing convention in this repo but means a rollback to a v7 binary against a v8 database is untested. Worth confirming before any release that ships alongside older nodes.

## Conclusion

N17 job replay and messages meet plan. Acceptance is complete, findings are non-blocking, and the schema bump is additive and safe. Recommend proceeding to N18.
