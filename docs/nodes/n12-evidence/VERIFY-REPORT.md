# N12 Runtime Verification Report

This report is runtime evidence only. It is not a plan audit or outcome hard-audit verdict.

- Generated: 2026-07-29T20:52:24Z
- OS: Microsoft Windows 11 专业工作站版 10.0.22624 build 22624
- Process architecture: X64
- MSVC: target_arch=x64  Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36248 for x64 版权所有(C) Microsoft Corporation。保留所有权利。  用法: cl [ 选项... ] 文件名... [ /link 链接选项... ]
- Target: x64, native Windows
- Language mode: `/std:c++20`
- Test runtime: 14068 ms
- Registered tests: 20 PASS, 0 FAIL
- Test output SHA-256: 1ef178b8ea34ee036c57e297b8822487d3dbbdddcf1f7f226e44440a561e6706

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1
build\cl\qbrain_tests.exe
```

## N1-N11 Preconditions

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

## Runtime Markers

```text
[INFO] silent_provider_elapsed_ms=3961
[INFO] rerank_audit_sample={"doc_count":3,"failure_reason":"transport_timeout","fallback_taken":true,"query_hash":"abadadc5b647126edb5c309a536952857ea008c0ec22babd6e0c74dd9791408e","timestamp":"2026-07-29T20:52:13Z"}
[INFO] minions_concurrent_claim winner=tok-race-A loser=sqlite_busy error=step: database is locked
[INFO] migration_v6_snapshot_sha256=692927dc1b80165584578f7256eac3ff956e4dd468552e96fb35038d9849bda4 populated_v5_before_after=identical
[INFO] migration_v6 populated_v5=preserved idempotent=noop rollback_after_first_ddl=clean rollback_after_marker=clean migrated_job_fence=pass fresh_shape=pass
[INFO] dream_dry_run_snapshot_sha256=447b07f81e145d5f74dffac396df1faace0399121b7f05bbdd811c052c509cf0 phases=orphans,extract_facts,consolidate,embed,purge unchanged=pass
[INFO] dream_purge_snapshot_before_sha256=3a45db1ae4fa98811c9f843db9e993c7af58c952f723b725b9ca63c8d88c4f23 after_sha256=f7fe43f2134531f8b1f083f008b46c051d9ca3195034964131cd4b3bbc625a36 mutations=7
[INFO] mcp_write_deny_snapshot_sha256=e650e3892a8736511efda93fe2b33d8a8b69752416f89d7d54a6803ca8c5a574 ops=submit_job,cancel_job,run_dream unchanged=pass
```

The rerank sample contains only the five approved fields. Its query hash uses a process-local random salt; no prompt, response body, exception body, or key is present. Audit rotation is tested at 1 MiB and preserves the prior generation in `.1`.

The concurrent claim marker records the exact loser outcome observed in this run. The dream markers are stable SHA-256 hashes over every non-SQLite user table, not page/chunk/link counters.

Schema v6 is additive. Downgrade and pre-v6 writer compatibility with a v6+ database are unsupported; restore a pre-migration backup instead of attempting schema rollback.

## Deliverable Hashes

| Path | SHA-256 |
|---|---|
| `include/qbrain/ai/chat.hpp` | `581b40d0b931c2d3876584d120936d5c4e92655a17084804f08ead59f9280bc7` |
| `include/qbrain/cycle/dream.hpp` | `6003aa8383cef1a4f3c8cd8e8779b1c9f995bc94c9e78ca8649826daec5a5ebe` |
| `include/qbrain/jobs/minions.hpp` | `b5a3fe4ff162e06d8712f08f21aaa8d7965b0ee9f23628562fc497674015c7a3` |
| `include/qbrain/search/rerank.hpp` | `7486917838473af720043e9cb6b71752ac950e3693de26f6aeae69b0ceb33148` |
| `src/qbrain/ai/chat.cpp` | `d140568bb6977668c5912c39a32e5399ecf47ad843047fce777d1ec1be397191` |
| `src/qbrain/cycle/dream.cpp` | `e7c6d107e3d9c64526a6e9c428ebff27d5ce89a7545321412bd23ab3ed0e3a21` |
| `src/qbrain/jobs/minions.cpp` | `3873351b51da3047bca0003ebed81d10c4200c54b5825d4c1d5e869fe526ed3c` |
| `src/qbrain/search/rerank.cpp` | `67d87412d90d14c79e1f44b19262917105d7433c66264e170f910c97a8dbb505` |
| `src/qbrain/storage/migrate.cpp` | `c2e752a059119a0d1bae6b96cca9b063425f6c05c3baca9f965e1143f6614247` |
| `tests/test_rerank.cpp` | `64a91574e2a56433392b39e52a74a101a09487ff1e6c772cb432da1a3e90ff06` |
| `tests/test_minions.cpp` | `910f5e2b511b258c4aa4e940baf12d47e495680c8c46c5d627ca9b41e615a4ad` |
| `tests/test_migration_v6.cpp` | `8b0c7e8fd1cd716ae2a7ae1a921098a2b48e99a5ad3029efc24f89697b908880` |
| `tests/test_n12_dream.cpp` | `23350c06a9ded7ca637410770f342a1488ffc98143dc4bd04a3e30fb03e4b329` |
| `scripts/build-tests-cl.ps1` | `0de120c72e59eaddeb17e10dddf3ff927a9c2631e755865af432c6912cb31bca` |
| `scripts/n12-verify.ps1` | `3dbad7765580f282f56d446d72053ab610010b458b19b8d398da3991153441d9` |

## Result

All scripted N12 runtime checks passed. A separate Claude Code outcome hard audit is still required before N12 can be marked done.
