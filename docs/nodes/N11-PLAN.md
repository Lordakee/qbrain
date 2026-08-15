# N11 Plan — Quality gate (doctor, tests, docs, ledger)

**Status**: done (plan PASS + outcome PASS 2026-07-28)  
**Depends on**: all declared v1 nodes N0–N10 (quality + closeout)  
**Plan audit**: PASS (`docs/nodes/N11-PLAN-AUDIT.md`)  
**Outcome audit**: PASS (`docs/nodes/N11-HARD-AUDIT.md`)  

## Goal

Close the v1 parity program with measurable quality and operator tooling:

1. `run_doctor` (CLI/MCP): schema/integrity checks on data root + DB
2. Unit/smoke test suite green on Windows MSVC path (`scripts/build-tests-cl.ps1` / `qbrain_tests.exe`), with at least the current v1 bar of **17/17 PASS** retained
3. Docs aligned: build guide, master plan status, node artifacts present
4. `docs/OPS-PARITY-LEDGER.md` reflects implemented vs deferred ops honestly
5. Migration/release notes sufficient for fresh install + existing DB open
6. No new product features required beyond doctor/tests/docs/ledger hygiene

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| run_doctor | schema/integrity/health checks |
| get_health / get_stats | optional supporting read diagnostics only if already registered; not required for N11 PASS |
| (process) unit suite | fixed corpus + regression bar for v1, minimum **17/17 PASS** |
| (docs) OPS-PARITY-LEDGER | rows match code |
| (docs) 03-BUILD-WINDOWS / master plan | operator path |

## Deliverables

- Doctor reports structured check results with overall `OK`, `WARN`, or `FAIL`: DB open, schema version, critical tables, optional FTS
- Test binary covers core N1–N10 surfaces at the current v1 bar of at least **17/17 PASS**; N11 may raise the count if doctor tests are added
- Ledger: every `implemented` row has a callable CLI/MCP op or explicit process/docs extension note; deferred rows stay marked deferred/out-of-scope
- Docs: how to build, test, run doctor on fresh `%LOCALAPPDATA%\Qbrain` or test CWD
- Final program note: usable D1–D25 gates from `docs/08-MASTER-PLAN-GBRAIN-PARITY.md` vs full 100+ gbrain ops (not 1:1 goal)

## Tests

- Fresh temp DB: doctor exits success with overall `OK`; optional checks may be `WARN` but not critical DB/schema/table checks
- Corrupt/missing schema fixture: doctor fails closed with non-zero or explicit FAIL
- `scripts/build-tests-cl.ps1` builds; `qbrain_tests.exe` all PASS
- Ledger file exists and lists `run_doctor` as implemented when code ships
- Read-only doctor/health without allow-write

## Acceptance assertions (falsifiable)

1. `run_doctor` on a just-migrated empty brain returns overall `OK` with exit code 0; only optional non-critical checks may return `WARN`
2. Unit test suite run via documented Windows script reports **all tests PASS** with count **>=17**
3. `docs/OPS-PARITY-LEDGER.md` exists and marks `run_doctor` **implemented** with matching MCP/CLI registration
4. Build docs path (`docs/03-BUILD-WINDOWS.md` or scripts referenced there) is sufficient to produce `qbrain_tests.exe` on the project’s MSVC recipe
5. MCP `run_doctor` is Read (or local_only diagnostic) and does not require `--allow-write`; any `doctor_remediate` write path is separate and default-deny

## Rollback

- Ship without remediate auto-fix; manual SQL/migrations
- Keep tests optional in CI if environment broken — but node PASS requires local green run evidence

## Security notes

- Doctor default is read-only inspection
- Any remediate/fix path is **Write** or explicit CLI `--remediate` with confirmation semantics
- No secrets in doctor output (redact env keys)
- Tests use temp dirs under user profile/temp — not production data root destruction
- Ledger must not claim network admin or open bind as done; N7 HTTP MCP remains loopback/token by default unless a separately audited node changes it

## Parallelism notes

- After plan audit PASS only, split work across disjoint slices: doctor/ops verification, test/build verification, docs/ledger closeout.
- Parent agent owns merge, full Windows test run, N11 outcome audit, and final status/ledger updates.
