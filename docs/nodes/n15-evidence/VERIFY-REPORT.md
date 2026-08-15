# N15 Runtime Verification Report

This is runtime evidence only. It is not a Claude Code plan audit or outcome hard-audit verdict.

- Generated: 2026-08-03T18:40:36Z
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
- Test output SHA-256: 8744d31f1579cd199e29af9c2bf02d412d76ff34c35efb5c374f41b3382d5fff
- CLI output SHA-256: 2f5702719e984a6a4a020741d5acdf29eba9df0c74eeb183ef61659db1013ab2

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1 -SkipProductionBuild
build\cl\qbrain_tests.exe
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/n15-verify.ps1 -SkipBuild
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
[INFO] n15 migration_matrix=pass migration_v12=pass migration_snapshot_sha256=6a72f46dbba6f534d0543685039e7f70554dafc26af34df0b6ef3ec136b8a0c0 migration_rollback_sha256=8065b4e091442358de89549b0ad60a501bf2634386ed3c4be47943a9cb495d73 migration_cleanup_sha256=23bb6429e5faa69d647ce545a628c483da4b27ae947dddaee79cf31909e6d058 migration_legacy_rows=2 migration_fk_cascade=pass link_source_matrix=pass link_source_ordering=pass link_brain_isolation=pass link_snapshot_sha256=a00e68ad5468fb43e10b3ee63172cfbba801848e7016039744dbd13cf42d627b retention_matrix=pass retention_default=100 retention_team_max=1000 get_log_limit_matrix=pass retention_snapshot_sha256=150d3089aac52d1e5a9067aa1edfa671feece1afa4ec9de3a68682eb76bddfad payload_boundary_matrix=pass event_boundary_bytes=64 path_boundary_bytes=4096 detail_boundary_bytes=65536 payload_rejected=8 source_validation_matrix=pass source_read_rejected=8 mcp_type_rejection_matrix=pass mcp_type_rejected=19 payload_snapshot_sha256=d5a1e1cfef92b3d89f6c808e33f8e51994a18c67a999b5642e6dc898507a7541 import_live_sync_matrix=pass import_counter_json=pass second_brain_isolation=pass import_primary_sha256=03d78a28838ec751e5aecf0b4c40b7b26379c6339e233d29d97b0aa0feea5038 import_second_sha256=b3243133bbad104dbfd88f78b408339bffafc7f0edaff1c2b83ef094df2db72f chronicle_boundary_matrix=pass chronicle_limit_matrix=pass chronicle_soft_delete=pass chronicle_tie_ordering=pass chronicle_invalid_day=11 chronicle_invalid_since=14 chronicle_snapshot_sha256=c9fa6555646f33002684f1bce91778ce51bc2cea9eafe66bbb88bb1939275321 registry_metadata_matrix=pass registry_operation_count=6 registry_schema=pass registry_strict_arguments=pass registry_unknown_fields_rejected=2 registry_metadata_sha256=be2d0f40ca6b015f16b1c9da37f9bf2b0db350888d973d1474dbfafd64619950 timeline_write_matrix=pass timeline_provenance=pass timeline_chunks=pass timeline_embed_once=pass remote_no_link_extraction=pass timeline_same_second_attempts=1 timeline_snapshot_sha256=543761c8870583d5233195911a306e86501486d0863a4068dc46de032a62de27 remote_write_matrix=pass remote_deny_snapshot_sha256=32a8d94a859f7191cdf19f1b805678e852388b502997123e3319ffb030108f7e remote_read_matrix=pass remote_read_count=4 remote_read_snapshot_sha256=ed2bd942ef2c5117819e5b019ed0098422af2f40cc2ebc9e3762d0bc76db3704 ambient_source_ignored=pass ambient_operation_count=6 ambient_snapshot_sha256=543761c8870583d5233195911a306e86501486d0863a4068dc46de032a62de27 full_logical_snapshots=pass localappdata_isolation=pass
N15_CLI_SMOKE_OK doctor=pass version=pass isolated_localappdata=pass
```

The N15 marker is emitted by the dedicated test and records the node-specific acceptance checks, full logical snapshot hashes, and MCP serialization path. The CLI marker uses a unique temporary LOCALAPPDATA and does not touch production data.

No model/provider/base URL/API key, reasoning, context, or compression configuration was changed.

## Deliverable Hashes

| Path | SHA-256 |
|---|---|
| `include/qbrain/core/brain.hpp` | `2f9d4bffe5d42234c78e823bd7e2ede9f64999761a1da9df5162d153095a8495` |
| `src/qbrain/core/brain.cpp` | `244cbccd45cfa4f2c6dccc22551d146198c5871510068458c93eae127bb34df6` |
| `src/qbrain/storage/migrate.cpp` | `4c7092a0c189f6f527db5641efc36031d6eb7533a50ab1a6b9cb928138cac900` |
| `src/qbrain/ingest/import.cpp` | `a51304a9dbbfe234d3f8b6cd39174c59a5a36f747644af03a22852bbc8f61819` |
| `src/qbrain/service/live_sync.cpp` | `35302192d0c1fdfb0515115945fc2419cf26f8ec450af9e7bb929bddc3cf7c4f` |
| `src/qbrain/ops/handlers.cpp` | `00ab465262ad82e58ec0a173e6d0a8755553897d48c026409ecd9908c3d5cf04` |
| `src/qbrain/util/paths.cpp` | `f7c3522070c601635b385f8d68bfee898e6b70cad2e2acc5a25d9e2f90b2488f` |
| `tests/test_n15.cpp` | `ad709351520f1f48f16fcbd6392ee8a93f886c10e92f70054b24f6b231cc9078` |
| `tests/wave3_test_support.hpp` | `9a4f94093a6e64b6a3f2021b52cefa23cd06d0223803d4e6bda3ea7ec9c301a6` |
| `tests/test_main.cpp` | `fda0d2be1d66f3b518fb8fd2b786954a8c128ca12f15fd8aee2360dd96670084` |
| `CMakeLists.txt` | `f8dc705f72dbc572005566620e22f3388861f72d12b01b971ee3f74b7d708a6f` |
| `scripts/build-tests-cl.ps1` | `f63dc04756a3c888ea75f5273756b98ddb39d5bf3b82fa6bd1bcaf76b6a72953` |
| `scripts/n15-verify.ps1` | `05e84a9222589b82f4c3788faff1154d608eca00eb2184f087b5b0e10bc356f7` |
| `scripts/wave3-verify-common.ps1` | `478348e2885f738cf7390aa18e6267470355379c85d6501fb9010a8904189d56` |
| `docs/nodes/N15-PLAN.md` | `c712cad4de7e8e11f3c274ccdb9c9e2b1acacc780802a654a125b14040b31d4e` |
| `docs/nodes/N15-PLAN-AUDIT.md` | `01e95a0cc55e4d0580562008a65de2ee941a13a8b37f4fd730389937d5abaef1` |

## Result

All scripted N15 runtime checks passed. A separate Claude Code outcome hard audit is still required before N15 may be marked done.
