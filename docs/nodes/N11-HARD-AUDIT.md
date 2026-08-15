# N11 HARD AUDIT

**VERDICT**: PASS  
**Auditor**: Claude Code  
**Plan**: `docs/nodes/N11-PLAN.md`  
**Plan audit**: `docs/nodes/N11-PLAN-AUDIT.md` PASS  
**Evidence**: `docs/nodes/N11-OUTCOME-EVIDENCE.md`  
**Date**: 2026-07-28

## Acceptance Table

| # | Assertion | Evidence | Result |
|---|-----------|----------|--------|
| 1 | `run_doctor` on just-migrated empty brain returns `overall: OK`, exit 0; only optional checks WARN | CLI smoke created `n11_doctor_smoke`, doctor JSON returned `ok:true`, `overall:"OK"`, schema v11, critical checks OK, optional WARN only for empty brain/missing embed key, exit 0 | PASS |
| 2 | Documented Windows script reports all tests PASS with count >=17 | `scripts\build-tests-cl.ps1` produced `BUILD_OK`, `TESTS_BUILD_OK`, and **18/18 PASS** including `[PASS] doctor` | PASS |
| 3 | Ledger marks `run_doctor` implemented with matching MCP/CLI registration | `docs/OPS-PARITY-LEDGER.md` records `run_doctor` implemented as N11 read-only doctor; CLI `doctor` routes through `run_doctor` | PASS |
| 4 | Build docs/scripts sufficient to produce `qbrain_tests.exe` on MSVC recipe | `docs/03-BUILD-WINDOWS.md` documents `scripts\build-cl.ps1` and `scripts\build-tests-cl.ps1`; verified on Windows MSVC path | PASS |
| 5 | MCP `run_doctor` is Read/no allow-write; `doctor_remediate` is separate Write/default-deny | `handlers.cpp` registers `run_doctor` as `Scope::Read`, `doctor_remediate` as `Scope::Write, local_only=true`; `test_doctor.cpp` covers read call and write denial | PASS |

## Deliverables Check

| Deliverable | Evidence | Result |
|-------------|----------|--------|
| Structured doctor checks | `run_doctor` JSON includes `ok`, `overall`, `schema_version`, `stats`, `checks`, `notes` | PASS |
| Fail-closed damaged schema behavior | `check_schema_integrity()` catches missing schema/table/index queries; `Brain::health()` catches stats exceptions; tests drop `content_chunks` and `schema_version` | PASS |
| Windows test suite green | `qbrain_tests.exe`: 18/18 PASS | PASS |
| Docs aligned | Build guide updated; stale shared audit marked superseded; master/completion docs updated | PASS |
| Ledger honest | `run_doctor` and `doctor_remediate` notes match Read vs Write behavior | PASS |

## P0

None.

## P1

None.

## P2

1. Plan still has minor cosmetic structure issues noted by the plan audit, but delivered behavior and evidence are sufficient.
2. `N0` dependency wording remains historical shorthand; N11 outcome evidence covers N1-N10 closeout.

## Rationale

Claude Code found all five N11 acceptance assertions satisfied by runtime evidence dated 2026-07-28. The implementation keeps doctor inspection read-only, preserves remediation as a separate default-denied write path, raises the suite to 18/18 PASS with doctor-specific coverage, and aligns docs/ledger with the shipped behavior.

**Conclusion**: N11 is complete.
