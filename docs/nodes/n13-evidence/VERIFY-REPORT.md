# N13 Runtime Verification Report

This report is runtime evidence only. It is not a plan audit or outcome hard-audit verdict.

- Generated: 2026-07-30T10:40:35Z
- OS: Microsoft Windows 11 专业工作站版 10.0.22624 build 22624
- Process architecture: X64
- MSVC: target_arch=x64  Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36248 for x64 版权所有(C) Microsoft Corporation。保留所有权利。  用法: cl [ 选项... ] 文件名... [ /link 链接选项... ]
- Target: x64, native Windows
- Language mode: `/std:c++20`
- Canonical build exit code: 0
- Test runtime: 23774 ms
- Registered tests: 21 PASS, 0 FAIL
- Build output SHA-256: e00a8bd8dd7f54244f9264b6427b84affe429f3d4d809f0ed98aca1d090b13e8
- Test output SHA-256: e13556167e08f6b4304219627b3d3b9ed26bc740289aaa1c62c730acaa9512f6
- CLI smoke output SHA-256: a392248ff0eac91eea6a4c4aeb529ef3bcbb7ad6a533d5d80b909e0949743368

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1
build\cl\qbrain_tests.exe
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/n13-verify.ps1 -SkipBuild
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/n13-cli-smoke.ps1
```

## N1-N12 Preconditions

| Node | Status | Plan audit | Outcome audit | Plan audit SHA-256 | Outcome audit SHA-256 |
|---|---|---|---|---|---|
| N1 | done | PASS | PASS | `9fd6df77ad905463f34e6873c2220849003679a64c869e5fb1eaffba470f95e6` | `93f112c13d01864aa701683e2a4dbb3726a763d90b7a113c07dc543af4d31141` |
| N2 | done | PASS | PASS | `c34fede88989a9847dd3cad0bf719b6476c28bbfb124cb094d4afbe24d90fb85` | `e9dc809dcdb73c0757708f81d53daf2fc89394c12cf953e86c0e9de5923a3413` |
| N2.5 | done | PASS | PASS | `bd0cf1b5f4dddb9af40168a89d1a87be84d5a4eb2f99872d3389880523617953` | `dd6e404ab7583af8c6cbecd86179baba3401a1d5ef10f559b2067229a208c8ff` |
| N3 | done | PASS | PASS | `ab99d7c2d0553575f16124fba807067a8039839321fb9f3469c949dbdc8a4994` | `2ca977089b0564f7ff60752c8cff4b968e1f528163239ef4c19e1bd22c094d24` |
| N4 | done | PASS | PASS | `ee7e14ab9347edac185661fddb0cee1a3d0a5205f6932560aab49fb99509f0bb` | `fe422f63e3cf686b3ebe7b85d8e04c4717df855d590a19aae59b4a9d96dc2065` |
| N5 | done | PASS | PASS | `481f77b815b0d0623e37010ab9dbcd9879d9d275d4b5e9e601e74c2d781bee28` | `d179a16ed21a3c7b67f28fde061c403b0414e6e783e45319bb99e316ee57eb18` |
| N6 | done | PASS | PASS | `8de656eae1a26d244cf83ea2e06e9f2b8b264f3102b27f32c96ad1a63dddbb12` | `637636d4ae57aa9087abba66abe1864f766c167d9a78e7e5954913a9dae5e9c9` |
| N7 | done | PASS | PASS | `929970318d8fb3043371f82a9208360db7e38e6dd058e37f0eef515534f26d39` | `307226705f0dc7495b0aa7aeebf88bd807c0216c19cab059cd23d01dd6835421` |
| N8 | done | PASS | PASS | `7f16263f786315420ed42a7c79350add553ad84b11ce4cd6dbc21b0fdc320570` | `7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9` |
| N9 | done | PASS | PASS | `8939a95a619e398a1605e13b807771726ad27574a3fd6f9a7b0803d1f593ebfa` | `1cf2ee1bfb671e8503a0eb400fd81ec24870b3254a5bd6335c1201d4ea2430c5` |
| N10 | done | PASS | PASS | `912e1953d6bf2a871fca1ca7b6d50986154e4ec28830d65927be81ae00c45c2a` | `27c4c3d389fbeadf2bf04c5d73827135baed7e09d912afd71cf85219b6e4d244` |
| N11 | done | PASS | PASS | `e157d9f3b6dcbc276b782d960c237d50fed9d4ff5614473678813e27541844a7` | `bdefcf26d138b658d31df0b8525c46b776aa5e9086796bcd16696d8b783f2012` |
| N12 | done | PASS | PASS | `4275b645cb9b08b9f118863ea8f56ca9137a0217d75d1db4440d37e5e1308970` | `66709f852022c205e9651a89dc92373e57f278a3a13a52b01b0f2071d6dac2c2` |

## Runtime Markers

```text
[INFO] n13_live_sync first_pages=2 second_skipped=2 changed_pages=1 watch_once=1 source_isolation=pass
[INFO] n13_source_cleanup rollback_unchanged=pass force_cleanup=pass
[INFO] n13_graph neighbors_depth2=8 cycle_nodes=8 source_isolation=pass
[INFO] n13_mcp_deny_snapshot_sha256=6645496a43a4f39f90bcd1fe8232a849f42ab90c802b99b17c17e2979b99fe5e unchanged=pass allow_write=pass read_ops=pass
N13_CLI_SMOKE_OK sync_once=pass idempotent=pass watch_once=pass source=pass interval=pass graph=pass
```

The test matrix covers live-sync idempotence/change/watch, brain/source isolation, source status/removal and rollback, bounded bidirectional BFS, retry/fact state transitions, and remote write denial with a full snapshot hash.

No provider/model/baseURL/API-key/reasoning/context/compression configuration is changed by N13.

## Deliverable Hashes

| Path | SHA-256 |
|---|---|
| `include/qbrain/core/brain.hpp` | `bbbe9547891bcb68d4189efaa0d55b83570d9881eaddfdab528a1a62d68ae4b3` |
| `include/qbrain/graph/traverse.hpp` | `b157649a23642e4932689d2e105040eb9754848faa0ca7d9afed2ed537f1a0f2` |
| `include/qbrain/ingest/import.hpp` | `71c2bd96b3a9d3958814e2594434454883b4cde1d407ab6f78e6e5ef2f39de1d` |
| `include/qbrain/service/live_sync.hpp` | `9cc6c61e26565aaca5294e263f6ef3b36b2040ed7b9d3ed8ebabf0dc35f7d4ba` |
| `src/qbrain/core/brain.cpp` | `46e54205ec76c8726ad415e71d8ffd907bbc542891eca28ea90c297dbdafdf9d` |
| `src/qbrain/graph/traverse.cpp` | `9b9eeecff60a720f480f79c4d010369e7ed6984589e9210af1da4e935de4f5e4` |
| `src/qbrain/ingest/import.cpp` | `ea1a7b9f450f69f1351c4163256df683e2ec06012af8b93a9688d0e8a55ad48d` |
| `src/qbrain/ops/handlers.cpp` | `d4d419a3179d2e01cb02175d303418b777c9d4240fef923b5891303e767710fc` |
| `src/qbrain/service/live_sync.cpp` | `cf6fe62830adf123e5904b58310330f2812f4fe5b261b20a44c27c6190831698` |
| `tests/test_n13.cpp` | `34ae2de83bc54b1fcd9611d7982ada6704617e3c02ce132be5bf807171425dec` |
| `tests/test_main.cpp` | `62f4ea80a70d5ac73551cabf586cc33718f71dd7277cef69cf85b1515e6f5a20` |
| `CMakeLists.txt` | `426365f70fe10be2076f147cd7f48a1247d102d711da1c0c6da91f745a95db8c` |
| `scripts/build-tests-cl.ps1` | `0a1d358fc41cedb256d9eeb6c33b644d21e3feea7384693fab5612ba41db45bf` |
| `scripts/n13-cli-smoke.ps1` | `f322b5e082d7a58c6861dd2a74adaef3804b5b384754ee70bc65b38ad2c22124` |
| `scripts/n13-verify.ps1` | `c4b85a83ea01e6753d775982002592e2d5cc59338d420b4eba2e9cf425441c9e` |

## Result

All scripted N13 runtime checks passed. A separate Claude Code outcome hard audit is still required before N13 can be marked done.
