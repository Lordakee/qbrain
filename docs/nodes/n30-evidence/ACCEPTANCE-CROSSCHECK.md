# N30 Acceptance Cross-Check (controller, 2026-08-15)

Maps each N30-PLAN.md acceptance assertion to concrete evidence. Suite status: **31/31 registered, two rounds green, both build paths** (FINAL-VERIFY-SCRIPT.txt: 93 PASS / 0 FAIL across build-run + 2 rounds; FINAL-VERIFY-CMAKE.txt: 62 PASS / 0 FAIL across 2 rounds + qbrain.exe doctor smoke OK).

| # | Assertion | Evidence | State |
|---|---|---|---|
| 1 | CHANGESET-MANIFEST covers all changed/untracked files with sha256+classification; secrets scan 0 | CHANGESET-MANIFEST.json (270 files: 41 product / 40 test / 188 docs / 1 stale / 0 rejected; 99 pattern hits all assessed non-secret, per-file exemptions embedded) | MET |
| 2 | NODE-RECONCILIATION-MATRIX N1-N29; N20/N21/N23 dispositions; N24-N28 full-standard audit or deferral | NODE-RECONCILIATION-MATRIX.json (30 rows; N20/N23 corrective-closure with fresh audits dispatched in N30 outcome phase; N21 supersede; N24-N28 AMD-7 deferrals with owner+rows) | MET (audits pending → see outcome phase) |
| 3 | Every Write/Admin op denied remotely without capability (negative matrix) | tests/test_n30.cpp n30_b section: programmatic enumeration — 36 mutating ops (34 Write + 2 Admin), all denied remote; the 5 P0-bypass ops (takes_calibration, file_upload, submit_agent, put_raw_data, schema_apply_mutations) centrally denied; PASS in both paths | MET |
| 4 | /ingestx not routed as /ingest; dup/invalid/oversized Content-Length rejected | tests/test_n30.cpp n30_c parse/route unit tests + loopback e2e negatives (404/405/400); exact request-line parser in mcp/server.hpp; PASS | MET |
| 5 | --port=notanumber/out-of-range controlled error, no crash | cli parse_port_value (from_chars, 1-65535, exit 2 controlled); n30_b unit tests incl. 16 malformed inputs; PASS | MET |
| 6 | Remote get_health/file_list/file_url expose no local paths | handlers redaction (remote∨via_mcp); n30_b asserts no db_path/drive-letter/file:///%LOCALAPPDATA% patterns remotely; local retains full info; PASS | MET |
| 7 | Removing any required v12 object → doctor FAIL | migrate.cpp now checks 16 tables + 18 indexes + 6 column sets incl. page_versions/facts/takes/file_index/raw_data; n30_c 11 corruption fixtures each FAIL doctor; PASS | MET |
| 8 | Failed pack mutation leaves target sha256 unchanged | packs.cpp temp+atomic-replace (MoveFileExW) with verified .bak restore (also fixed read-only .bak brick bug); n30_c read-only injection test: sha256 unchanged, no temp leftovers, retry succeeds; PASS | MET |
| 9 | Clean-dir dual builds produce working binaries; ≥30 registered, two rounds green | FINAL-VERIFY-SCRIPT.txt (rm -rf build/cl; build exit 0; rounds exit 0), FINAL-VERIFY-CMAKE.txt (fresh build/cmake-final; qbrain.exe+qbrain_tests.exe; rounds exit 0); 31 registered | MET |
| 10 | Test counts in docs are generated values; no stale 18/18 | docs/09 + docs/03 updated to 31/31 generated; ledger header carries generated current-status line; historical prose counts relabeled historical | MET |
| 11 | Commits are reversible slices (docs vs source separated) | Commit plan (parent): slice 1 source/build+tests, slice 2 docs/governance — executed after audits PASS | PENDING (post-audit) |

## Additional deliverable notes

- Pre-implementation gate: PRE-GATE.json (18 scoped-file hashes, deliverables-absence proof, build identity incl. baseline 28+1(n20)/CMake-red evidence).
- Resolution amendments honored: AMD-1 (matrix dispositions), AMD-2 (tiers), AMD-4/9 (manifest before commit; only accepted slices committed), AMD-6 (generated counts), AMD-7 (N24-N28 deferral records; N20/N23 fresh audits dispatched), AMD-8 (N30 audited only against its own plan), AMD-10 (security findings as mandatory gates, all closed).
- Security model refinement during merge (controller decisions, all test-verified): ctx.remote = HTTP transport only; ctx.via_mcp = any MCP transport; MCP stdio keeps audited N1 write default-deny via --allow-write opt-in; HTTP Write/Admin centrally denied regardless of flags; source allow-list + provenance stamping apply to any non-CLI caller (remote ∨ via_mcp); disclosure redaction (db paths, file URLs) applies to remote ∨ via_mcp (CLI-only visibility).
- test_n20.cpp corrective closure: the previously failing expected-deny check now passes via central enforcement; the non-deterministic Windows share-mode lock fixture was replaced by a deterministic directory-at-path fixture mapping to pack_unsafe (matching the file's own existing directory/symlink conventions at lines 1139/1155).
