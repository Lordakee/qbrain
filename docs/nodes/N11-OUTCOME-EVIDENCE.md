# N11 Outcome Evidence Packet

**Node**: N11 quality gate (doctor, tests, docs, ledger)  
**Plan**: `docs/nodes/N11-PLAN.md`  
**Plan audit**: `docs/nodes/N11-PLAN-AUDIT.md` PASS  
**Outcome audit**: pending `docs/nodes/N11-HARD-AUDIT.md`

## Approved Acceptance

1. `run_doctor` on a just-migrated empty brain returns overall `OK` with exit code 0; only optional non-critical checks may return `WARN`.
2. Unit test suite run via documented Windows script reports all tests PASS with count `>=17`.
3. `docs/OPS-PARITY-LEDGER.md` marks `run_doctor` implemented with matching MCP/CLI registration.
4. Build docs path or referenced scripts are sufficient to produce `qbrain_tests.exe` on the MSVC recipe.
5. MCP `run_doctor` is Read/local-only diagnostic and does not require `--allow-write`; `doctor_remediate` is separate Write/default-deny.

## Implementation Files

- `src/qbrain/ops/handlers.cpp`: `run_doctor` is registered as `Scope::Read`, `local_only=false`, emits structured `ok`, `overall`, `schema_version`, `stats`, `checks`, and `notes`. `doctor_remediate` is registered as `Scope::Write`, `local_only=true`.
- `src/qbrain/cli/commands.cpp`: CLI `doctor` calls the `run_doctor` registry op; `doctor --remediate` calls `doctor_remediate` first, then `run_doctor`.
- `src/qbrain/ops/registry.cpp`: remote local-only operations are denied when `ctx.remote && !ctx.allow_write`.
- `src/qbrain/storage/migrate.cpp`: schema integrity checks catch missing/damaged `schema_version`, table, and index queries and return `ok=false` instead of throwing.
- `src/qbrain/core/brain.cpp`: `Brain::health()` runs schema integrity before stats and catches stats exceptions, so damaged schemas return a degraded/failed report.
- `tests/test_doctor.cpp`: covers direct `run_doctor`, MCP `tools/list`, MCP `run_doctor` without allow-write, MCP `doctor_remediate` denial without allow-write, dropped `content_chunks`, and dropped `schema_version`.
- `scripts/build-cl.ps1`: before linking `qbrain.exe`, stops only processes whose executable path is `build\cl\qbrain.exe` to release a local MCP binary lock.
- `scripts/build-tests-cl.ps1`, `tests/test_main.cpp`, and `CMakeLists.txt`: include `tests/test_doctor.cpp`.

## Runtime Evidence

Command:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-tests-cl.ps1
```

Result observed 2026-07-28:

```text
BUILD_OK
TESTS_BUILD_OK
[PASS] rrf
[PASS] vector
[PASS] chunker
[PASS] extract
[PASS] storage
[PASS] mcp
[PASS] rerank
[PASS] minions
[PASS] live_sync
[PASS] codeintel
[PASS] analytics
[PASS] n19
[PASS] n20_23
[PASS] n24_25
[PASS] n26_27
[PASS] wave4
[PASS] wave5
[PASS] doctor
```

Count: **18/18 PASS**.

Fresh doctor smoke:

```powershell
.\build\cl\qbrain.exe init --brain n11_doctor_smoke
.\build\cl\qbrain.exe doctor --brain n11_doctor_smoke --json
```

Observed result:

```json
{
  "ok": true,
  "overall": "OK",
  "schema_version": 11,
  "checks": [
    {"name": "database", "status": "OK"},
    {"name": "schema", "status": "OK"},
    {"name": "critical_tables", "status": "OK"},
    {"name": "optional", "status": "WARN"}
  ]
}
```

Exit code: `0`. Optional WARN notes were empty brain and missing embedding API key.

## Docs/Ledger Evidence

- `docs/03-BUILD-WINDOWS.md`: direct CL path now documents `scripts\build-cl.ps1` and `scripts\build-tests-cl.ps1`, smoke uses `build\cl\qbrain.exe doctor --json`, verified result is 18/18 PASS.
- `docs/OPS-PARITY-LEDGER.md`: `run_doctor` is implemented with N11 read-only doctor note; `doctor_remediate` notes separate local-only Write/default-deny path; Wave 6 note records 18/18 PASS.
- `docs/nodes/N8-N11-HARD-AUDIT.md`: marked superseded/historical only, not valid for N11 outcome.
- `docs/08-MASTER-PLAN-GBRAIN-PARITY.md` and `docs/09-PROJECT-COMPLETION.md`: no longer claim final N11 PASS; both say N11 outcome is pending.
