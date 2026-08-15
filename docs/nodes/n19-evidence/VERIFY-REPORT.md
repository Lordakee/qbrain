# N19 Runtime Verification Report

This is factual runtime evidence only. It is not a Claude Code plan audit or outcome hard-audit verdict.

- Generated: 2026-08-04T09:51:14.7275983+00:00
- Plan status: approved
- Outcome audit: pending
- OS: Microsoft Windows 11 专业工作站版 10.0.22624 build 22624
- Process/target: X64/x64
- Compiler: Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36248 for x64
- Language mode: `/std:c++20`
- Registered tests: 26
- Results: 26 PASS, 0 FAIL
- N19 snapshot calls: 204
- Evidence manifest SHA-256: fa17ce28a96e5ba62bd0a107d2f28c715678de71e2b645fbd2c1c2cc16c84b94

## Commands And Exit Codes

| Command | Exit |
|---|---:|
| `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cl.ps1` | 0 |
| `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1 -SkipProductionBuild` | 0 |
| `build\cl\qbrain_tests.exe` | 0 |
| `build\cl\qbrain.exe doctor --json` | 0 |

## Governance

- Approved plan SHA-256: `d03201c31a6a4051b2cc6d5b82e5ced7ae8c4b0982bc65223e251870d01c1d3a`
- Claude Code plan-audit SHA-256: `e5c603efbfecb5603a0fd068dd2a0b39e7a75abac5fd116634adc397d9b7e470`
- The exact direct dependency audit hashes were rechecked against the approved plan and are recorded in `EVIDENCE-MANIFEST.json`.
- The historical outcome audit is not used. A fresh Claude Code outcome audit remains blocking.

## Pre-Corrective Schema Gate

- Artifact: `docs/nodes/n19-evidence/PRE-CORRECTIVE-SCHEMA-GATE.json`
- Artifact SHA-256: `2638f4c02a501cf295f8b2ab575e894d051159479b8f6a8beadca28d6544ea3a`
- Interval: 2026-08-04T08:50:12.4331908+00:00 to 2026-08-04T08:50:13.0139314+00:00
- Command: `build\cl\qbrain.exe doctor --brain n19-pre-corrective-gate --json`
- Execution path: qbrain doctor -> Brain::health -> storage::check_schema_integrity
- Result: exit_code=0, ok=true, schema_version=12, stderr_empty=true
- Gate binary SHA-256: `04460d5ae88d4e1c285cb4f7b4a05df548bfd830772a053b9555f2d45990e7b4`
- Pre-corrective input manifest: 20 files, SHA-256 `dbe67c9bfbb31baaa4ae2c19dbac40bb11c9e82c662cd2c73ed930f8e5528cfa`
- Ordering proof: 18 inputs remain byte-for-byte unchanged; exactly 2 corrective inputs have different hashes and timestamps strictly after gate completion.
- Isolation proof: temporary `LOCALAPPDATA` was used, production data was not used, no config persisted, and the temporary root was removed.
- The gate records no protected model-configuration change and no commit or push.

## Focused Runtime

```text
[INFO] n19 schema_v12=pass schema_reopen=pass utc_boundaries=pass source_matrix=pass strict_arguments=pass identity=pass identity_exact_matrix=pass identity_matrix_cells=4 path_redaction=pass context_query=pass context_fail_open=pass context_recent=pass utf8_bounds=pass timeline=pass chronicle=pass seven_day_default=pass registry=pass mcp_rpc=pass ambient_default=pass selected_decoy=pass damaged_database=pass read_only=pass snapshot_call_count=204 selected_snapshot_sha256=192e0efd7f46b10c6ab2c65b3ac33481832f05097698318c3f4ee286730bc8f8 decoy_snapshot_sha256=0d74cc5de575761d7c6573cd8392ded34c77e242d3d977a3fb3fee0d66e40da1
[PASS] n19
```

Every one of the 204 emitted snapshot rows was parsed, indexed contiguously, and required identical selected-before/after and decoy-before/after SHA-256 values. The selected and decoy final hashes are distinct. The damaged-database call has its own unchanged snapshot row and structured-database-error marker.

The marker and required snapshot-label matrix cover schema v12/reopen, the exact four-cell selected/decoy by default/team identity matrix, strict source and numeric validation, local/remote path redaction, conservative context query/recent/fail-open determinism, timeline, seven-day Chronicle boundaries, tools/list schema inspection, real tools/call framing with writes disabled, ambient-source exclusion, authorization, selected/decoy isolation, and read-only failures.

## Evidence Files

| Role | Path | SHA-256 |
|---|---|---|
| pre-corrective-schema-gate | `docs/nodes/n19-evidence/PRE-CORRECTIVE-SCHEMA-GATE.json` | `2638f4c02a501cf295f8b2ab575e894d051159479b8f6a8beadca28d6544ea3a` |
| prebuild-manifest | `docs/nodes/n19-evidence/PREBUILD-MANIFEST.json` | `e7b556cb957f0e2eeecd8bfeda8d385727ca6f94f32d4fcb9294e4658aa5aa27` |
| production-build-log | `docs/nodes/n19-evidence/PRODUCTION-BUILD-OUTPUT.txt` | `b5fa020d00e9abaa90df2bd3719670b749b9841c6144ab21fb427fb21dcd4531` |
| test-build-log | `docs/nodes/n19-evidence/TEST-BUILD-OUTPUT.txt` | `b1f79f7924b55faf36440ef8a1168ccb8236976fe2d313c773c158feb9306082` |
| full-suite-log | `docs/nodes/n19-evidence/FULL-SUITE-OUTPUT.txt` | `95ca314165d4586d68bbbe95dc774db4e2b2bf382b54fdffb765e1ccaf62a3ca` |
| focused-runtime-log | `docs/nodes/n19-evidence/FOCUSED-RUNTIME-OUTPUT.txt` | `f4de3fb1f98332caacaae51a6ff5cb1f1520d198f78edda369ec8118a03982a7` |
| snapshot-evidence | `docs/nodes/n19-evidence/SNAPSHOT-EVIDENCE.txt` | `bd8b8c8d065e24d530ac733e51e8cb93f3b798bbcbcc95921ae25ca71b63e7f3` |
| mcp-schema-evidence | `docs/nodes/n19-evidence/MCP-SCHEMA-EVIDENCE.txt` | `d1d8c903947ba7b6754c01fa2a9e2e072e2338326bce84e2ba6a78f470e0af44` |
| schema-smoke-evidence | `docs/nodes/n19-evidence/SCHEMA-SMOKE-OUTPUT.txt` | `615902fc0751394500bdd8bb3eb80cbf08b69b9ceaf74206a2313cbc56207cbd` |
| platform-evidence | `docs/nodes/n19-evidence/PLATFORM-OUTPUT.txt` | `6af1d454dc6de519b9517889d59760bdfa361cbdaf2d014313a18d517605e275` |

## Deliverable Hashes

| Path | SHA-256 |
|---|---|
| `include/qbrain/core/brain.hpp` | `1aa7da46f32c3514320c9c878c0d57aae62721c59698fcfeb58c68ab98256229` |
| `include/qbrain/search/hybrid.hpp` | `45072d707a65485f355b22fa64ed662d9a1439703224224faad03fd9a5f00034` |
| `include/qbrain/util/time_util.hpp` | `78390f81b3a583fd7e3b0ce3b62e7f4405ef3e0947b266a0e5fb4066b68b8cb3` |
| `src/qbrain/core/brain.cpp` | `24bcd78978dc3aefb8bf7057def006f1fa049a8d63276402403f3b9a3740235b` |
| `src/qbrain/search/hybrid.cpp` | `2a74917efc8e6ceffd8800ab2548fd4ce3c1065671d53dc174bc4bd190b74508` |
| `src/qbrain/ops/handlers.cpp` | `e02c76b55410f4ac02bb973d6e83691ffe1b97856f3a4234cb3677cca3c7ad7e` |
| `src/qbrain/mcp/server.cpp` | `7515830e9d40c18580723567149569296b55d43a5e93615d7e5e35385564c4fd` |
| `src/qbrain/util/time_util.cpp` | `cb315dc6c50958c6a69270b130a9757881a9030cd27b8b730626d6e296ffe217` |
| `tests/test_n19.cpp` | `27023d52d3c637b081747d1e66c2a86c84a21c0628fb413d6301af8e00e6d8e9` |
| `tests/test_main.cpp` | `e47ab53280eb82f29ba189d022e174ee8300a683eef2134accb14758dfd12600` |
| `CMakeLists.txt` | `24a4668591feb3b4a9ae1dba584c82dffe9f0d7c5f796fc6f0c31b631eced395` |
| `scripts/build-cl.ps1` | `553a1989952d346fce8f836146377aec6d0db29e143893c3ac7fd28869b15070` |
| `scripts/build-tests-cl.ps1` | `a682bb81ba366e7dc45eeee2825255f76b27c6db66b490791e4a1467c88f458e` |
| `scripts/n19-verify.ps1` | `cd6f37cfcb5b4e40ed4af2bec46776814d868905b5d246dc1f511d5fd4ec2e8a` |
| `docs/nodes/N19-PLAN.md` | `d03201c31a6a4051b2cc6d5b82e5ced7ae8c4b0982bc65223e251870d01c1d3a` |
| `docs/nodes/N19-PLAN-AUDIT.md` | `e5c603efbfecb5603a0fd068dd2a0b39e7a75abac5fd116634adc397d9b7e470` |

## Scoped Safeguards

- Build, test, and runtime logs contain zero N30 references, and the scoped manifest contains no N30 artifact path.
- N19 scoped diff checks found no protected model/provider/base URL/API key/reasoning/context/compression setting assignment or protected configuration path.
- Protected repository configuration hashes, Git HEAD, and Git reference-log fingerprints remained unchanged from preparation through verification.
- The verifier executed no commit or push command. Its exact allowed commands are listed above.
- Both complete-suite sandboxes produced the same isolated config result (count 1, SHA-256 `441063381f57fbee120246cbf69050c6ae40ab554ccbebad812ac901cb5eae92`). When present, the file had only exact canonical defaults, no key/allowlist/extra fields, and was deleted with its sandbox.
- Schema smoke used a unique temporary LOCALAPPDATA, persisted no application configuration, and did not touch production `%LOCALAPPDATA%\Qbrain` data.
- No live LLM/provider request is part of the N19 focused marker matrix.

## Result

All scripted N19 evidence checks completed against the prepared, current native build closure. This does not mark N19 done. A fresh node-specific Claude Code outcome hard audit against the approved plan and these evidence files is still required.
