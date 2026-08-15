# N17 Runtime Verification Report

This is factual runtime evidence only. It is not a Claude Code plan audit or outcome hard-audit verdict. The historical N17 outcome-audit file was not read or used as a gate.

- Generated UTC: 2026-08-04T07:03:39Z
- OS: Microsoft Windows 11 专业工作站版 10.0.22624 build 22624; OSArchitecture=64 位
- Process architecture: X64
- Target: native Windows x64
- Language mode: `/std:c++20`
- MSVC: Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36248 for x64
- MSVC discovery exit code: 0
- cl.exe SHA-256: `dc8426b8760d92cf757df3d10b9f0244a95b454ff43194a58161568a0ec70d53`
- qbrain.exe SHA-256: `dcb8d0c2ae39ca15d862ecc486bcc96a68e2e7cf1e0ee1b41bac1d52ab247ce4`
- qbrain_tests.exe SHA-256: `a9bd5a42c51c4f54ab3af91475198072e8d811d57f8e82481adc0006fe76ea52`
- Approved plan SHA-256: `3954237b5707348963821bd72a5eccec5f805e437478b9fc02f135761582bf77`
- Plan audit SHA-256: `46cded0d8537f432617e0fed4d59b1d9e842ce255ee6851ce224b4cea177433b`
- Audit-recorded draft-plan SHA-256: `a72ec794ee27fce4358ba78b1d1d2a351ced2aad58973cb015e5f5810d90ff1a`
- Build evidence mode: `executed`

## Commands And Exit Codes

| Command | Exit code |
|---|---:|
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-cl.ps1` | 0 |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1 -SkipProductionBuild` | 0 |
| `build\cl\qbrain_tests.exe` | 0 |
| `build\cl\qbrain.exe version` | 0 |
| `build\cl\qbrain.exe doctor --json` | 0 |
| `build\cl\qbrain.exe serve --brain n17-evidence (stdio NDJSON tools/list; writes disabled)` | 0 |

The canonical test-build run and the separate complete-suite run each registered exactly 26 PASS results and zero FAIL results, including exactly one `[PASS] n17`.

## Corrective Closure Evidence

The approved plan identifies the historical pre-fix behavior as any-status replay, permissive prefix/whitespace id parsing, generic failures, and an unregistered list helper. The dedicated current test marker records the strict-id matrix, terminal-only replay matrix, structured MCP validation, registered tools/list path, default-deny/allow-write paths, exact selected/decoy snapshots, schema checks, and both permitted concurrency schedules. Historical PASS text is not used as current conformance evidence.

```text
[INFO] n17 strict_id_alias_matrix=pass strict_id_cases=84 replay_terminal_state_matrix=pass replay_state_cases=9 sender_payload_utf8_json_boundaries=pass sender_payload_cases=26 missing_vs_empty_list=pass list_limit_matrix=pass list_limit_cases=15 migration_v7_v8_v12=pass migration_idempotence=pass migration_rollback=pass damaged_integrity=pass no_foreign_key=pass registry_tools_list=pass real_mcp=pass default_deny=pass allow_write=pass mcp_rejection_cases=26 selected_decoy_snapshots=pass concurrency=pass replay_race_successes=1 replay_race_busy=1 message_race_successes=1 message_race_busy=1 selected_snapshot_sha256=bc270828371e106e2a77959dc1b93e8fbc2fc24883b2bb98344983e5724d03b4 decoy_snapshot_sha256=fd4401542097bf5d0a4eb337e55b7afe5566428bd189d60294c6c2c5630cd0d6 migration_snapshot_sha256=b37d7576d83abc5a84855ac9546477ac1567bdd46d5eea103381a313a67199ca rollback_snapshot_sha256=b0dd9c48506d100aeeae8d21c07543818be1e2c4aa20525af5b81283ddd61995
```

## Snapshot, Schema, And Concurrency Markers

| Fact | Captured value |
|---|---|
| Selected-brain snapshot | `bc270828371e106e2a77959dc1b93e8fbc2fc24883b2bb98344983e5724d03b4` |
| Decoy-brain snapshot | `fd4401542097bf5d0a4eb337e55b7afe5566428bd189d60294c6c2c5630cd0d6` |
| Migration snapshot | `b37d7576d83abc5a84855ac9546477ac1567bdd46d5eea103381a313a67199ca` |
| Migration rollback snapshot | `b0dd9c48506d100aeeae8d21c07543818be1e2c4aa20525af5b81283ddd61995` |
| Replay race | successes=1, busy=1, retry-to-two checked by dedicated test |
| Message race | successes=1, busy=1, retry-to-two checked by dedicated test |
| Historical v8 source | HEAD=`0b0cf624d8c4b36152866f2c37389367e2f01399174f94cf5935f4fb59ae3980`; current=`0b0cf624d8c4b36152866f2c37389367e2f01399174f94cf5935f4fb59ae3980`; byte-normalized equality=true |
| Schema baseline | v12; no version above 12; exact v8 table/index/no-FK and damaged-v12 checks recorded by the marker |
| Successful replay delta | one `jobs` row plus its sequence advance; all pre-existing/other application state and the decoy remain unchanged |
| Successful message delta | one `job_messages` row plus its sequence advance; the parent job, other application state, and the decoy remain unchanged |

## Scope And Security Evidence

- The scoped artifact count for the forbidden coordinator node is zero, and the repository path check is zero.
- The N17-created/modified N19-or-later scoped path count is zero. Pre-existing later-node tests in the complete regression suite are not attributed to N17.
- No protected model-configuration path is in the N17 scope. All monitored configuration source hashes were identical before and after verification. The isolated config count was 1; when present, it contained only exact canonical defaults, no key/allowlist/extra fields, remained byte-stable across standalone/CLI/MCP probes, and was removed with the sandbox.
- The verifier invoked no network server/provider command and made no live network call. All runtime commands used a unique temporary LOCALAPPDATA that was removed after checks.
- A real stdio `tools/list` call captured only the three N17 definitions in `OPERATION-SCHEMAS.json`; exact object schemas, alias requirements, integer/string bounds, defaults, and `additionalProperties=false` were checked before packaging.
- Runtime registry evidence records `replay_job` and `send_job_message` as Write/local-only and `list_job_messages` as Read/non-local-only; remote writes remain denied unless explicitly allowed.
- No git-mutating command, commit, push, schema downgrade, or third-party dependency change is part of this evidence run.

## Dependency Evidence

| Node | Plan audit | Outcome audit | Plan-audit SHA-256 | Outcome-audit SHA-256 |
|---|---|---|---|---|
| N1 | PASS | PASS | `9fd6df77ad905463f34e6873c2220849003679a64c869e5fb1eaffba470f95e6` | `93f112c13d01864aa701683e2a4dbb3726a763d90b7a113c07dc543af4d31141` |
| N7 | PASS | PASS | `929970318d8fb3043371f82a9208360db7e38e6dd058e37f0eef515534f26d39` | `307226705f0dc7495b0aa7aeebf88bd807c0216c19cab059cd23d01dd6835421` |
| N8 | PASS | PASS | `7f16263f786315420ed42a7c79350add553ad84b11ce4cd6dbc21b0fdc320570` | `7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9` |
| N12 | PASS | PASS | `4275b645cb9b08b9f118863ea8f56ca9137a0217d75d1db4440d37e5e1308970` | `66709f852022c205e9651a89dc92373e57f278a3a13a52b01b0f2071d6dac2c2` |
| N13 | PASS | PASS | `632d8c5c062536555c307280badcf8cb3b41a0c33c91be1e4350e7b635ea0d9f` | `49a73387886dfa34fe7226b944d2db0ea94f3a3414735d6378889e7afda02744` |
| N14 | PASS | PASS | `e28365fc38b79bc4340defba8524350629f6966490532251445a6f84d6ce1385` | `15c9b5a0886e6b9406b6bbe6ed2cd12e12ffa1b732768193a4e281355b1f3c2b` |
| N15 | PASS | PASS | `01e95a0cc55e4d0580562008a65de2ee941a13a8b37f4fd730389937d5abaef1` | `9f5f14ab7ed2cf4da50b597f8f861061948d9b65331091a017d677f7b4968c59` |

## Generated Artifact Hashes

| Path | SHA-256 |
|---|---|
| `docs/nodes/n17-evidence/PRODUCTION-BUILD-OUTPUT.txt` | `fe5cb3021725e29b837f129d6209be42a3366084e5795808769b1827d0bf9024` |
| `docs/nodes/n17-evidence/TEST-BUILD-OUTPUT.txt` | `ab7b70f412b54fc167eeb674fb06f024dcc48279710b62e7952f5fdaea1d2201` |
| `docs/nodes/n17-evidence/BUILD-MANIFEST.txt` | `d24f6fed6faf8fa3e2ff4af7c16c906870847995affde52e5065d8c4b153070b` |
| `docs/nodes/n17-evidence/TEST-OUTPUT.txt` | `ff4d563530439b4d1c1fe8c50429dea05efb017a72a91ae83ca6585ceb0671c6` |
| `docs/nodes/n17-evidence/CLI-SMOKE-OUTPUT.txt` | `72c6e75bb43d550234be35ca71865544b39f0b998ef6c70ab8ce612b7c13a68c` |
| `docs/nodes/n17-evidence/RUNTIME-MARKERS.txt` | `d40f25be8fe041b8b7c622ff5c5ef8fe9efde9582dec3627a9b08220a06ecbdc` |
| `docs/nodes/n17-evidence/SCHEMA-EVIDENCE.txt` | `7dac37dad1eaf9e1854f3a40f452e7a427048d3d7b184a835c387ec1ab85b5eb` |
| `docs/nodes/n17-evidence/OPERATION-SCHEMAS.json` | `f1a0d24ca5d54d5cd14f5696d259b95f89e776ecd87aff91b545cb7ed2883fca` |
| `docs/nodes/n17-evidence/N17-SCOPE-MANIFEST.txt` | `ab53f1ce9a230d8350cdb5452973d2474b30c8f8be6c8c753a039789cc09bcc3` |

## Deliverable Hashes

| Path | SHA-256 |
|---|---|
| `include/qbrain/jobs/minions.hpp` | `00da6811c7e0c41ac93b6b50e8e126b8d2dbf15e19b4f149a1116c108b52abe4` |
| `src/qbrain/jobs/minions.cpp` | `245b7bae5589947b3aaf03698f12c54959c62535439f781c8daa782bf0123806` |
| `src/qbrain/ops/handlers.cpp` | `e02c76b55410f4ac02bb973d6e83691ffe1b97856f3a4234cb3677cca3c7ad7e` |
| `src/qbrain/mcp/server.cpp` | `7515830e9d40c18580723567149569296b55d43a5e93615d7e5e35385564c4fd` |
| `src/qbrain/ops/registry.cpp` | `e3fb8bad77a88677d6ae23a2eba626e98d47ab41c0659cbac1d724688b134a18` |
| `src/qbrain/storage/migrate.cpp` | `d775529ff6f89d520f15a3fee30f0e180a2011646516de0a620e1870d538ec45` |
| `tests/test_n17.cpp` | `e4fe3eaa144209e8a7e64b6f5251bf863514a05c1d5e6ae5109b118b112bcdac` |
| `tests/wave3_test_support.hpp` | `9a4f94093a6e64b6a3f2021b52cefa23cd06d0223803d4e6bda3ea7ec9c301a6` |
| `tests/test_main.cpp` | `e47ab53280eb82f29ba189d022e174ee8300a683eef2134accb14758dfd12600` |
| `CMakeLists.txt` | `24a4668591feb3b4a9ae1dba584c82dffe9f0d7c5f796fc6f0c31b631eced395` |
| `scripts/build-tests-cl.ps1` | `a682bb81ba366e7dc45eeee2825255f76b27c6db66b490791e4a1467c88f458e` |
| `scripts/n17-verify.ps1` | `1c8991fd1ee4ef06cc17b2c506ce35fdfac240b35ca0be21bc7ec92034159c00` |
| `docs/nodes/N17-PLAN.md` | `3954237b5707348963821bd72a5eccec5f805e437478b9fc02f135761582bf77` |
| `docs/nodes/N17-PLAN-AUDIT.md` | `46cded0d8537f432617e0fed4d59b1d9e842ce255ee6851ce224b4cea177433b` |

## Result

All scripted N17 verification checks completed successfully. The final evidence manifest is generated after this report so it can include the report hash; the manifest intentionally omits only its own recursive hash. A separate node-specific Claude Code outcome hard audit is still required before N17 may be marked done.
