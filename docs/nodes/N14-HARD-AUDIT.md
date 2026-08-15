# N14 OUTCOME HARD AUDIT

**VERDICT: PASS**

**Auditor: Claude Code**

**Plan:** docs/nodes/N14-PLAN.md

**Date:** 2026-08-04

**Scope:** Outcome audit for N14 — job control (pause/resume), progress, status snapshot, and doctor remediation.

---

## Evidence timestamps and hashes

- Evidence generated: 2026-08-03T18:38:12Z (VERIFY-REPORT.md)
- N14 plan SHA-256: `add6143785031cf1928b06d6132c61aeffeddebc7243470f6e5f08299f0ffe36`
- N14 plan audit SHA-256: `e28365fc38b79bc4340defba8524350629f6966490532251445a6f84d6ce1385` (PASS 2026-07-30)
- Evidence-directory manifest (`docs/nodes/n14-evidence/BUILD-MANIFEST.txt`) SHA-256: `2b6bee15b0c279c3a85e81b99eebf2811cc59a90627ae1ec331e08ce6e4a4d7d` (99 FILE entries)
- Current build manifest (`build/wave3-final-build-manifest.txt`) SHA-256: `2b6bee15b0c279c3a85e81b99eebf2811cc59a90627ae1ec331e08ce6e4a4d7d` (identical)
- Build output SHA-256: `03c213b1a785e2597da5ea354c4273c2925178018d4adb4b1af0045353ad5f7c`
- Test output SHA-256: `36296c327d21be482e44ea962c4c9c69d045e3f07f7245af606c4359438ed7dd`
- CLI smoke SHA-256: `253be9a380d13b4ca2926791bdeff92044c22cd774722cfedf76a2025c95281f`

---

## Acceptance table

| # | Assertion | Evidence | Status |
|---|-----------|----------|--------|
| 1 | Implementation begins only after Claude Code plan audit PASS and plan marked approved | N14-PLAN.md line 3: `Status: approved`; N14-PLAN-AUDIT.md verdict PASS 2026-07-30 | ✓ |
| 2 | `pause_job` changes only waiting/active to paused, clears fence, preserves non-state data, makes old token unusable | `src/qbrain/jobs/minions.cpp:338-347` SQL `WHERE id=? AND status IN ('waiting','active')` sets `lock_token=NULL, lock_until=NULL`; test_n14.cpp:178-186 proves stale token rejected | ✓ |
| 3 | `resume_job` changes only paused to waiting; unclaimed until N12 claim assigns token; concurrent claim has one winner | `src/qbrain/jobs/minions.cpp:349-358` SQL `WHERE id=? AND status='paused'`; test_n14.cpp:161-195 exercises pause→resume→claim sequence with fresh token; runtime marker `concurrent_claim_winners=1` | ✓ |
| 4 | Wrong-state, unknown, malformed, non-positive, overflow inputs fail without changing database snapshot | test_n14.cpp:225-246 exercises unknown id `9223372036854775807`, invalid inputs `["", "0", "-1", "1junk", "9223372036854775808"]`; snapshot equality proven before/after rejections | ✓ |
| 5 | Operation parser consumes entire decimal id string; `1junk` cannot operate on job 1 | handlers.cpp:97-109 `std::from_chars` with `parsed.ptr != last` check rejects trailing data; test_n14.cpp:237-246 proves `1junk` rejected with `invalid_argument` | ✓ |
| 6 | `get_job_progress` returns only documented fields, redacts credentials, never returns payload/result/lock_token, no mutation | minions.cpp:392-412 reads only 6 columns `id,type,status,attempts,lock_until,error_text`; minions.cpp:109-127 `safe_progress_error` redacts 14 credential patterns; test_n14.cpp:77-107 proves 6-field JSON with forbidden fields absent; handlers.cpp:1297-1302 serializes exactly 6 fields | ✓ |
| 7 | `get_status_snapshot` matches SQL for selected brain, correct count semantics, only 4 job counters, no cross-brain leak | brain.cpp:1310-1330 implements StatusSnapshot with live-page SQL `deleted_at IS NULL`, schema integrity check, and 4 job counters from `count_jobs`; handlers.cpp:1313-1334 returns 8-field JSON; test evidence runtime marker `status_snapshot=pass snapshot_schema=pass selected_brain=pass` | ✓ |
| 8 | Malformed snapshot fails structured without repair; default `run_doctor` read-only unchanged | Test evidence runtime marker `damaged_status=database_error`; handlers.cpp:1330-1332 catches exception and returns `database_error`; N11 run_doctor remains separate read-only path | ✓ |
| 9 | Remediation restores canonical default, reclaims only expired active default-queue jobs, clears fences, increments attempts once, idempotent | brain.cpp:1332-1394 `remediate()` transaction with `ensure_source("default")` at line 1339, `reclaim_stalled(*this, "default")` at line 1341; minions.cpp:360-369 reclaim SQL `WHERE queue=? AND status='active' AND lock_until<datetime('now')` increments attempts once; test evidence runtime marker `remediation_idempotent=pass` | ✓ |
| 10 | Embedding unavailable → zero jobs, no request; deterministic injected availability → exactly one job per eligible page, exact count reported | brain.cpp:1332-1336 optional override seam `embedding_available_override`; brain.cpp:1350-1351 checks `api_key_present` and skips enqueue; brain.cpp:1369-1378 enqueues exactly one job per missing page; test evidence runtime marker `remediation_embed_delta=5` proves exact count | ✓ |
| 11 | Embed dedup uses parsed integer identity: page 1 vs 10 no collision, multiple chunks → one job, pending suppresses duplicates, terminal permits retry, malformed safe | brain.cpp:1358-1366 parses `payload["page_id"].get<int64_t>()` as integer, continues on parse failure; brain.cpp:1370 checks `pending_page_ids.count(page_id)` to suppress duplicates; test evidence runtime marker `page_id_exact_dedup=pass` | ✓ |
| 12 | Concurrent remediation ≤1 job per page; injected failure rolls back complete mutation set | brain.cpp:1337 `db_.exec("BEGIN IMMEDIATE;")`, exception handler at 1387-1393 rolls back; test evidence runtime markers `concurrent_pending=1 rollback_snapshot_sha256=43e91142175ec111dfbb21e411e8bf910b68269bb8e8663290051d04aa829c55` | ✓ |
| 13 | Remote N14 Write calls with write disabled rejected before handler, byte-identical snapshot preserved; allowed valid calls mutate only declared surface | Test evidence runtime marker `mcp_deny_snapshot_sha256=8d2a6e900099478b8f0fc911762030eb0882187c0af9952f1c163350256cc719 mcp_rpc=pass allowed_remote_writes=pass`; handlers.cpp registrations at lines 1244,1269,1337 specify `Scope::Write` with `local_only=true` for pause/resume/doctor_remediate | ✓ |
| 14 | Progress/status Read ops work with write disabled, emit valid JSON, expose no secrets, leave database unchanged | handlers.cpp:1288-1310 `get_job_progress` and 1313-1334 `get_status_snapshot` registered `Scope::Read` with `local_only=false`; test evidence runtime marker `progress_redaction=pass status_snapshot=pass` | ✓ |
| 15 | Native Windows MSVC evidence: x64, compiler version, /std:c++20, exact commands/exit codes, ≥21 PASS, CLI markers, row deltas, snapshot hashes; no config changed | VERIFY-REPORT.md: MSVC 19.51.36248 x64, `/std:c++20`, exit 0, 25 PASS/0 FAIL; BUILD-OUTPUT.txt lines 4-10 confirm Visual Studio 2026 x64 with `/std:c++20`; CLI-SMOKE-OUTPUT.txt line 51 `N14_CLI_SMOKE_OK commands=3 json_parse=pass remediation_envelope=pass post_health=pass timeout=30s isolated_localappdata=pass config_unchanged=pass`; no model/provider/baseURL/key/reasoning/context/compression modified | ✓ |

All 15 acceptance assertions proven with concrete source, test, and runtime evidence.

---

## Deliverables check

| Deliverable | Status | Evidence |
|-------------|--------|----------|
| 1. `include/qbrain/jobs/minions.hpp` + `src/qbrain/jobs/minions.cpp` pause/resume contracts | ✓ | minions.hpp:56-58 declarations; minions.cpp:338-358 implementations with N12 fence preservation (clears `lock_token` and `lock_until`) |
| 2. `include/qbrain/core/brain.hpp` + `src/qbrain/core/brain.cpp` status snapshot + idempotent remediation with parsed page_id | ✓ | brain.hpp:170-192 StatusSnapshot + RemediateReport structs; brain.cpp:1310-1330 status_snapshot, 1332-1394 remediate transaction with integer page_id parsing at line 1363 |
| 3. `src/qbrain/ops/handlers.cpp` strict positive int64 parsing, correct scope/local_only, safe JSON, structured failures | ✓ | handlers.cpp:97-109 `parse_positive_i64` with full-string consumption (`parsed.ptr != last` check); handlers.cpp:1244-1260, 1269-1285, 1288-1310, 1313-1334, 1337-1362 all 5 N14 ops with correct scope metadata |
| 4. `src/qbrain/cli/commands.cpp` doctor read-only default, explicit `--remediate` flag | ✓ | N11 `doctor` command unchanged; CLI-SMOKE-OUTPUT.txt lines 36-82 shows `doctor --remediate --json` invokes N14 write path with remediation envelope |
| 5. Dedicated `tests/test_n14.cpp` registered in test_main/CMakeLists/build script | ✓ | test_n14.cpp present with N14 state/progress/snapshot/remediation matrices; TEST-OUTPUT.txt line 36 `[PASS] n14` with runtime markers |
| 6. `scripts/n14-verify.ps1` + `docs/nodes/n14-evidence/VERIFY-REPORT.md` with build/test/CLI output, snapshot hashes, row deltas | ✓ | n14-verify.ps1 SHA-256 `8f840f6414d6827fc3f766bd64630f66d18803e3a1041c46ea4b671f70808532`; VERIFY-REPORT.md with 8 evidence artifacts and runtime markers |
| 7. No schema migration | ✓ | No schema.sql modification in N14 deliverable hashes; N14 plan line 40 explicitly states no schema migration planned |
| 8. Node-specific Claude Code outcome audit → ledger update if PASS | This document | PASS verdict; ready for ledger update |

All 8 deliverables present and complete.

---

## Build and test verification

### Native Windows MSVC x64 build

- Compiler: Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36248 for x64
- Architecture: x64
- Language mode: `/std:c++20`
- Build command: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1`
- Exit code: 0 (production), 0 (tests)
- Test executable: `build\cl\qbrain_tests.exe`
- Production binary: `build\cl\qbrain.exe`

### Test results

- Registered tests: 25
- PASS: 25
- FAIL: 0
- Baseline: N13 required ≥21, N14 delivers 25 ✓
- Test output runtime markers:
  ```
  [PASS] n14
  [INFO] n14 job_fence=pass progress_redaction=pass status_snapshot=pass
         snapshot_schema=pass snapshot_matrix=pass state_matrix=pass
         progress_matrix=pass selected_brain=pass remediation_lease_matrix=pass
         remediation_embed_matrix=pass allowed_remote_writes=pass
         job_matrix_snapshot_sha256=7fbaf2b6949d7fd76e8d0a65ac7cf64eee2fd11cebba7e9ccf4bfc185fa76d56
         status_matrix_snapshot_sha256=ef5d5ac621cb50805f4d25e913f78622a0416dfca8a7dfeeaffc5f4c273503f3
         remediation_snapshot_sha256=7eb4af5a435d1d1df92cae90d3fa40d5273cbbdd993be57b1469a5582733b08d
         remediation_matrix_rollback_sha256=b52e0533902e32688c2ca0f396648fdeed94d261ea097f7b1eea64611263c538
         concurrent_claim_winners=1 remediation_embed_delta=5
         mcp_deny_snapshot_sha256=8d2a6e900099478b8f0fc911762030eb0882187c0af9952f1c163350256cc719
         mcp_rpc=pass page_id_exact_dedup=pass remediation_idempotent=pass
         rollback_snapshot_sha256=43e91142175ec111dfbb21e411e8bf910b68269bb8e8663290051d04aa829c55
         concurrent_pending=1 damaged_status=database_error
  ```

### CLI smoke test

- Isolated LOCALAPPDATA: temporary sandbox per N14 plan security requirement ✓
- `qbrain doctor --json` exit code: 0
- `qbrain doctor --remediate --json` exit code: 0
- Post-remediation `qbrain doctor --json` exit code: 0
- Canonical default source: restored ✓
- No model/provider configuration change ✓
- No live network call ✓
- Output marker: `N14_CLI_SMOKE_OK commands=3 json_parse=pass remediation_envelope=pass post_health=pass timeout=30s isolated_localappdata=pass config_unchanged=pass`

---

## N1-N13 dependency verification

All dependency nodes have plan audit PASS and outcome audit PASS per VERIFY-REPORT.md table:

| Node | Plan audit | Outcome audit | Verified |
|------|------------|---------------|----------|
| N1 | PASS | PASS | ✓ |
| N2 | PASS | PASS | ✓ |
| N2.5 | PASS | PASS | ✓ |
| N3 | PASS | PASS | ✓ |
| N4 | PASS | PASS | ✓ |
| N5 | PASS | PASS | ✓ |
| N6 | PASS | PASS | ✓ |
| N7 | PASS | PASS | ✓ |
| N8 | PASS | PASS | ✓ |
| N9 | PASS | PASS | ✓ |
| N10 | PASS | PASS | ✓ |
| N11 | PASS | PASS | ✓ |
| N12 | PASS | PASS | ✓ |
| N13 | PASS | PASS | ✓ |

---

## Security verification

### MCP default-deny (N1, N13)

- `pause_job`, `resume_job`, `doctor_remediate`: registered `Scope::Write` + `local_only=true` ✓ (handlers.cpp:1244,1269,1337)
- `get_job_progress`, `get_status_snapshot`: registered `Scope::Read` + `local_only=false` ✓ (handlers.cpp:1288,1313)
- Remote write-disabled denial: test runtime marker `mcp_deny_snapshot_sha256=8d2a6e900099478b8f0fc911762030eb0882187c0af9952f1c163350256cc719` proves byte-identical snapshot after all three write rejections
- MCP RPC verified: runtime marker `mcp_rpc=pass allowed_remote_writes=pass`

### N12 token fence preservation

- Pause clears `lock_token` and `lock_until` immediately (minions.cpp:341-342) ✓
- Stale worker completion/failure with old token rejected (test_n14.cpp:185-186 proves `complete_job` and `fail_job` return false with stale token) ✓
- Resume transitions to `waiting` only; new token assigned exclusively through N12 claim path (minions.cpp:349-358) ✓
- Test proves: pause active job → attempt completion with old token → fails; fresh claim gets new token at line 192-195 ✓
- Runtime marker: `job_fence=pass`

### Integer ID parsing strictness

- `parse_positive_i64` (handlers.cpp:97-109) uses `std::from_chars` with full-string consumption check `parsed.ptr != last` ✓
- Zero rejected: line 108 `value == 0` ✓
- Overflow (>INT64_MAX) rejected: line 109 `value > std::numeric_limits<int64_t>::max()` ✓
- Trailing junk (`1junk`) rejected: line 108 `parsed.ptr != last` ✓
- Test matrix (test_n14.cpp:237-246) proves rejection leaves snapshot unchanged ✓

### Progress output redaction and bounding

- `safe_progress_error` (minions.cpp:109-127) redacts 14 credential patterns including `api_key`, `bearer`, `authorization`, `sk-`, `password`, `token`, `secret`, `credential`, `passphrase`, `privatekey`, `cert`, `oauth`, `session`, `cookie` ✓
- Max 500 bytes bounded via `bounded_utf8` (minions.cpp:76-94) ✓
- Progress JSON (handlers.cpp:1297-1302) contains exactly 6 fields ✓
- `payload_json`, `result_json`, `lock_token`, API keys, model names explicitly absent ✓
- Runtime marker: `progress_redaction=pass`

### N8 selected-brain isolation

- Status snapshot uses `ctx.brain` connection, no cross-brain aggregate (handlers.cpp:1316) ✓
- Remediation operates only on selected database (brain.cpp:1332-1394) ✓
- Runtime marker: `selected_brain=pass`

### Test and CLI isolation

- Tests use temporary directory fixtures ✓
- CLI smoke uses isolated temporary LOCALAPPDATA sandbox ✓
- No production `%LOCALAPPDATA%\Qbrain` touched ✓
- No live network request ✓
- Runtime marker: `isolated_localappdata=pass`

---

## Transaction and idempotence verification

### Remediation transaction rollback

- brain.cpp:1337 `db_.exec("BEGIN IMMEDIATE;")` wraps all remediation mutations ✓
- Exception handler (brain.cpp:1387-1393) catches and rolls back ✓
- Test evidence runtime marker `rollback_snapshot_sha256=43e91142175ec111dfbb21e411e8bf910b68269bb8e8663290051d04aa829c55` proves rollback to pre-injection snapshot ✓

### Idempotence

- First remediation: `embed_jobs_enqueued=5` for eligible pages ✓
- Second remediation: returns 0 due to pending suppression (brain.cpp:1370 `pending_page_ids.count(page_id)`) ✓
- Runtime marker: `remediation_idempotent=pass`

### Concurrency deduplication

- Runtime marker: `concurrent_pending=1` proves at most one pending job per page ✓
- SQLite busy loser permitted (test_n14.cpp:45-53 `is_sqlite_busy` check documents expected behavior) ✓

---

## Page ID deduplication verification

### Integer identity prevents prefix collision

- brain.cpp:1363 parses `payload["page_id"].get<int64_t>()` as integer, not string ✓
- No substring/prefix matching that would collide page 1 with page 10 ✓
- Runtime marker: `page_id_exact_dedup=pass` ✓

---

## P0

None.

---

## P1

None.

---

## P2

None.

---

## Conclusion

All 15 acceptance assertions are proven with concrete evidence from implementation, tests, and runtime verification. All 8 deliverables are present and complete. Native Windows MSVC x64 C++20 build succeeds with 25 PASS / 0 FAIL tests (exceeds N13 baseline of 21). CLI smoke test passes with isolated LOCALAPPDATA and no configuration changes. MCP default-deny security verified with byte-identical snapshot after denial. N12 token fence preserved: stale completion rejected after pause. Progress output redacted and bounded (14 credential patterns, 500-byte UTF-8 boundary). Status snapshot isolated to selected brain with live-page semantics. Remediation is transactional (rollback proven with injected failure), idempotent (second call returns 0 enqueued), and uses parsed integer page_id to prevent collision (page 1 distinct from page 10). Concurrent remediation produces ≤1 pending job per page. All N1-N13 dependencies have PASS audits. No schema migration. No model/provider/baseURL/API key/reasoning/context/compression configuration changed.

N14 is complete and ready to be marked done.
