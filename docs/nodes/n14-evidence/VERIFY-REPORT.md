# N14 Runtime Verification Report

This is runtime evidence only. It is not a Claude Code plan audit or outcome hard-audit verdict.

- Generated: 2026-08-03T18:38:12Z
- OS: Microsoft Windows 11 专业工作站版 10.0.22624 build 22624
- Process architecture: X64
- MSVC: target_arch=x64  Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36248 for x64 版权所有(C) Microsoft Corporation。保留所有权利。  用法: cl [ 选项... ] 文件名... [ /link 链接选项... ]
- Target: native Windows x64
- Language mode: `/std:c++20`
- Canonical build command exit code: 0
- Test command exit code: 0
- Registered tests: 25 PASS, 0 FAIL
- Build output SHA-256: 03c213b1a785e2597da5ea354c4273c2925178018d4adb4b1af0045353ad5f7c
- Build manifest SHA-256: 2b6bee15b0c279c3a85e81b99eebf2811cc59a90627ae1ec331e08ce6e4a4d7d
- Test output SHA-256: 36296c327d21be482e44ea962c4c9c69d045e3f07f7245af606c4359438ed7dd
- CLI output SHA-256: 253be9a380d13b4ca2926791bdeff92044c22cd774722cfedf76a2025c95281f

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1 -SkipProductionBuild
build\cl\qbrain_tests.exe
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/n14-verify.ps1 -SkipBuild
```

## N1-N13 Preconditions

| Node | Plan audit | Outcome audit | Plan SHA-256 | Outcome SHA-256 |
|---|---|---|---|---|
| N1 | PASS | PASS | `9fd6df77ad905463f34e6873c2220849003679a64c869e5fb1eaffba470f95e6` | `93f112c13d01864aa701683e2a4dbb3726a763d90b7a113c07dc543af4d31141` |
| N2 | PASS | PASS | `c34fede88989a9847dd3cad0bf719b6476c28bbfb124cb094d4afbe24d90fb85` | `e9dc809dcdb73c0757708f81d53daf2fc89394c12cf953e86c0e9de5923a3413` |
| N2.5 | PASS | PASS | `bd0cf1b5f4dddb9af40168a89d1a87be84d5a4eb2f99872d3389880523617953` | `dd6e404ab7583af8c6cbecd86179baba3401a1d5ef10f559b2067229a208c8ff` |
| N3 | PASS | PASS | `ab99d7c2d0553575f16124fba807067a8039839321fb9f3469c949dbdc8a4994` | `2ca977089b0564f7ff60752c8cff4b968e1f528163239ef4c19e1bd22c094d24` |
| N4 | PASS | PASS | `ee7e14ab9347edac185661fddb0cee1a3d0a5205f6932560aab49fb99509f0bb` | `fe422f63e3cf686b3ebe7b85d8e04c4717df855d590a19aae59b4a9d96dc2065` |
| N5 | PASS | PASS | `481f77b815b0d0623e37010ab9dbcd9879d9d275d4b5e9e601e74c2d781bee28` | `d179a16ed21a3c7b67f28fde061c403b0414e6e783e45319bb99e316ee57eb18` |
| N6 | PASS | PASS | `8de656eae1a26d244cf83ea2e06e9f2b8b264f3102b27f32c96ad1a63dddbb12` | `637636d4ae57aa9087abba66abe1864f766c167d9a78e7e5954913a9dae5e9c9` |
| N7 | PASS | PASS | `929970318d8fb3043371f82a9208360db7e38e6dd058e37f0eef515534f26d39` | `307226705f0dc7495b0aa7aeebf88bd807c0216c19cab059cd23d01dd6835421` |
| N8 | PASS | PASS | `7f16263f786315420ed42a7c79350add553ad84b11ce4cd6dbc21b0fdc320570` | `7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9` |
| N9 | PASS | PASS | `8939a95a619e398a1605e13b807771726ad27574a3fd6f9a7b0803d1f593ebfa` | `1cf2ee1bfb671e8503a0eb400fd81ec24870b3254a5bd6335c1201d4ea2430c5` |
| N10 | PASS | PASS | `912e1953d6bf2a871fca1ca7b6d50986154e4ec28830d65927be81ae00c45c2a` | `27c4c3d389fbeadf2bf04c5d73827135baed7e09d912afd71cf85219b6e4d244` |
| N11 | PASS | PASS | `e157d9f3b6dcbc276b782d960c237d50fed9d4ff5614473678813e27541844a7` | `bdefcf26d138b658d31df0b8525c46b776aa5e9086796bcd16696d8b783f2012` |
| N12 | PASS | PASS | `4275b645cb9b08b9f118863ea8f56ca9137a0217d75d1db4440d37e5e1308970` | `66709f852022c205e9651a89dc92373e57f278a3a13a52b01b0f2071d6dac2c2` |
| N13 | PASS | PASS | `632d8c5c062536555c307280badcf8cb3b41a0c33c91be1e4350e7b635ea0d9f` | `49a73387886dfa34fe7226b944d2db0ea94f3a3414735d6378889e7afda02744` |

## Runtime Markers

```text
[INFO] n14 job_fence=pass progress_redaction=pass status_snapshot=pass snapshot_schema=pass snapshot_matrix=pass state_matrix=pass progress_matrix=pass selected_brain=pass remediation_lease_matrix=pass remediation_embed_matrix=pass allowed_remote_writes=pass job_matrix_snapshot_sha256=7fbaf2b6949d7fd76e8d0a65ac7cf64eee2fd11cebba7e9ccf4bfc185fa76d56 status_matrix_snapshot_sha256=ef5d5ac621cb50805f4d25e913f78622a0416dfca8a7dfeeaffc5f4c273503f3 remediation_snapshot_sha256=7eb4af5a435d1d1df92cae90d3fa40d5273cbbdd993be57b1469a5582733b08d remediation_matrix_rollback_sha256=b52e0533902e32688c2ca0f396648fdeed94d261ea097f7b1eea64611263c538 concurrent_claim_winners=1 remediation_embed_delta=5 mcp_deny_snapshot_sha256=8d2a6e900099478b8f0fc911762030eb0882187c0af9952f1c163350256cc719 mcp_rpc=pass page_id_exact_dedup=pass remediation_idempotent=pass rollback_snapshot_sha256=43e91142175ec111dfbb21e411e8bf910b68269bb8e8663290051d04aa829c55 concurrent_pending=1 damaged_status=database_error
N14_CLI_SMOKE_OK commands=3 json_parse=pass remediation_envelope=pass post_health=pass timeout=30s isolated_localappdata=pass config_unchanged=pass
```

The marker records token-fence invalidation, bounded redaction, selected-brain snapshot behavior, transactional/idempotent remediation, concurrency deduplication, structured damaged-database failure, and full-snapshot MCP denial.

No model/provider/base URL/API key, reasoning, context, or compression configuration was changed.

## Deliverable Hashes

| Path | SHA-256 |
|---|---|
| `include/qbrain/jobs/minions.hpp` | `b5a3fe4ff162e06d8712f08f21aaa8d7965b0ee9f23628562fc497674015c7a3` |
| `src/qbrain/jobs/minions.cpp` | `64076eac93daebb3ebcfd67fb539682ac77b36edcb854272db923875769abc52` |
| `include/qbrain/core/brain.hpp` | `2f9d4bffe5d42234c78e823bd7e2ede9f64999761a1da9df5162d153095a8495` |
| `src/qbrain/core/brain.cpp` | `244cbccd45cfa4f2c6dccc22551d146198c5871510068458c93eae127bb34df6` |
| `src/qbrain/ops/handlers.cpp` | `00ab465262ad82e58ec0a173e6d0a8755553897d48c026409ecd9908c3d5cf04` |
| `tests/test_n14.cpp` | `3b4a69f3b4c2d282069ca81d8be6421d3a09a48a8ce61d71cf761f296eb97785` |
| `tests/wave3_test_support.hpp` | `9a4f94093a6e64b6a3f2021b52cefa23cd06d0223803d4e6bda3ea7ec9c301a6` |
| `tests/test_main.cpp` | `fda0d2be1d66f3b518fb8fd2b786954a8c128ca12f15fd8aee2360dd96670084` |
| `CMakeLists.txt` | `f8dc705f72dbc572005566620e22f3388861f72d12b01b971ee3f74b7d708a6f` |
| `scripts/build-tests-cl.ps1` | `f63dc04756a3c888ea75f5273756b98ddb39d5bf3b82fa6bd1bcaf76b6a72953` |
| `scripts/n14-verify.ps1` | `8f840f6414d6827fc3f766bd64630f66d18803e3a1041c46ea4b671f70808532` |
| `docs/nodes/N14-PLAN.md` | `add6143785031cf1928b06d6132c61aeffeddebc7243470f6e5f08299f0ffe36` |
| `docs/nodes/N14-PLAN-AUDIT.md` | `e28365fc38b79bc4340defba8524350629f6966490532251445a6f84d6ce1385` |

## Result

All scripted N14 runtime checks passed. A separate Claude Code outcome hard audit is still required before N14 may be marked done.
