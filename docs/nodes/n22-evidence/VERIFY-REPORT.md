# N22 Verification Report

This report records native evidence only; it is not a Claude Code audit verdict.

- Registered tests: 29; first run 29 PASS / 0 FAIL; second run 29 PASS / 0 FAIL.
- Windows host: Microsoft Windows 11 专业工作站版 10.0.22624 build 22624; compiler: Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36248 for x64.
- Production binary SHA-256: `84c3427a349fa5bb16819639229694fadc4f80011ee0cff03a2ca5ec5096f71a`.
- Test binary SHA-256: `a1491acd844f6e5146752afd8f80ecc39c886c2cca197c4317d5eab142514ac0`.
- N22 snapshot rows: 367; normalized marker SHA-256: `20e0899ba5d1d73ecf4448beaba481850a610ee4b41b2d8b84e3fc187f515410`.
- Two-run result stream SHA-256: `c9f845e68ca12ab14cdfa863c9ec7db09fef31a3045d31badc86a382dd2b75fb`.
- Publication nonce: `0a52857e556345e0bbdd8b83e0f467c4`.
- Real final-binary doctor reported schema v12 and ok=True.
- Exact N19 schema-v12 storage inputs remained byte-identical across 4 files; binding SHA-256 2d662c1b165bc51380644b95955378ca67887a21b70e6260279ead20d67e550b.
- Runtime schema evidence contains exactly one fresh-v12, populated-reopen-v12, and final-v12 snapshot marker.
- The N22 production slice remained closed and byte-identical after runtime probes; binding SHA-256 aac89e1d0436814d177cbbf750723f3865230d52f965baeb034db9ffd59920c0.
- Real stdio MCP probes covered 47 requests, 33 structured errors, and 33 single-error blocks.
- Disposable Qbrain persistent-data tree SHA-256 before setup / after cleanup: `absent` / `absent`; fixture before/after probes: `70461e4f4b8b330412d1056f64b87aba17c00a95899bdc80566481d7a70630c5` / `70461e4f4b8b330412d1056f64b87aba17c00a95899bdc80566481d7a70630c5`; doctor after-tree `70461e4f4b8b330412d1056f64b87aba17c00a95899bdc80566481d7a70630c5`.
- That persistent-data fingerprint binds directory paths/attributes and every file path, attributes, length, SHA-256 content, and LastWriteTimeUtc. It intentionally excludes directory LastWriteTimeUtc because SQLite WAL read lifecycles can change that OS metadata without a surviving Qbrain data or logical-database change.
- Selected/decoy logical snapshots and the scoped persistent-data tree fingerprint matched across N22 paths; the stateless cache-clear compatibility path reported zero rows.
- Protected repository configuration hashes and Git repository facts were rechecked against the frozen preparation. The verifier did not traverse production LOCALAPPDATA; it collected no production filesystem-access telemetry and does not claim global push telemetry. It did not write node status, ledger state, or an audit verdict.
- A fresh node-specific Claude Code outcome hard audit remains blocking before status or ledger reconciliation.
