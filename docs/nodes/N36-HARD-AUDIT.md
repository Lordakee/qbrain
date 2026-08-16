# N36 HARD AUDIT (outcome)

**Auditor**: Claude Code (claude-opus-5, effort max), task qbrain-n36-hard-audit-claude-20260816, 2026-08-16
**Audit basis**: docs/nodes/N36-PLAN.md (approved round-2 PASS)
**Human authorization**: user instruction 2026-08-15 (verbatim in dispatch log)

---

Based on my systematic review of the N36 implementation against the approved plan, here is my audit conclusion:

---

**VERDICT: PASS**

## Findings

**No P0 (blocking), P1, or P2 findings.** All acceptance assertions AA1-AA8 verified against implementation and test evidence.

### Verification Summary by Acceptance Assertion

**AA1 (scope mapping matrix)**: `test_n36.cpp:115-158` demonstrates alice (read) → read ops succeed + write denied; bob (write,admin→admin) → write/admin succeed; carol (admin) → admin succeeds. Capability propagation through `handle_rpc_body` verified with `put_raw_data` (non-local-only Write op, documented rationale at line 126-127).

**AA2 (negative matrix + 3-way privilege escalation)**: Missing/malformed/forged tokens → 401 (`http_server.cpp:357-361`); three-way escalation matrix verified via isolated Registry instance (`test_n36.cpp:159-188`): admin capability grants admin scope, write capability does NOT grant admin (line 182-184), no capability does NOT grant admin (line 186-187). Malformed Authorization headers (missing Bearer, no space, token>256, non-ASCII) all fail authentication → 401.

**AA3 (constant-time compare)**: `auth.cpp:36-42` implements `constant_time_equal` with XOR accumulation; `authenticate_bearer` (line 88) calls it; test behavioral verification at `test_n36.cpp:85-92`.

**AA4 (unconfigured = N30 behavior)**: `test_n36.cpp:148-149` verifies nullptr capability → write_denied; evidence shows all 37 pre-existing tests passed (FINAL-VERIFY-SCRIPT.txt lines preceding 3898; FINAL-VERIFY-CMAKE.txt lines preceding 2724).

**AA5 (audit lines)**: `http_server.cpp:338,351,361,421` log audit lines with hash_prefix (sha256 first 16 hex chars, `auth.cpp:80-82`); `test_n36.cpp:94-100` asserts 16-char hex format + no token material.

**AA6 (token <16 bytes skipped)**: `auth.cpp:71,74-75` skips invalid entries with stderr warning; evidence shows 4 "skipping invalid QBRAIN_MCP_TOKENS entry" warnings (FINAL-VERIFY-SCRIPT.txt:3894-3897, FINAL-VERIFY-CMAKE.txt:2720-2723) matching test config's 4 malformed entries.

**AA7 (stdio unchanged)**: `test_n36.cpp:151-157` verifies stdio path respects allow_write gate with no capability concept; all pre-N36 tests green.

**AA8 (full suite 38×3 + 38×2 green)**: `test_main.cpp:74-113` registers 38 tests (37 + n36_token_scope); evidence confirms **3 rounds script-path** (FINAL-VERIFY-SCRIPT.txt line 3898 final PASS) + **2 rounds cmake-path** (FINAL-VERIFY-CMAKE.txt line 2724 final PASS), all green, 0 failures.

### Deliverables Confirmed

- **D1-D3** (auth core + HTTP wiring + audit): `auth.{hpp,cpp}`, `http_server.cpp:193-199,329-361,420-421`
- **D4** (tests): `test_n36.cpp` single registration `n36_token_scope` with unit + op-level + registry-level sub-assertions
- **D5** (docs): `docs/03-BUILD-WINDOWS.md:77-87` token configuration section with explicit TLS/OAuth/dynamic-user deferrals
- **D6** (evidence): `docs/nodes/n36-evidence/FINAL-VERIFY-{SCRIPT,CMAKE}.txt` with exact counts

### Known Items Disposition

1. Remote write probe uses `put_raw_data` (not `put_page`): **Correct** — `put_page` is local-only per audited N1 decision; test comment documents rationale (`test_n36.cpp:125-127`).
2. Admin scope verified at registry level: **Correct** — isolated Registry instance (`test_n36.cpp:163-188`) because global Admin ops remain handler-hardened local-only per audited N2.5/N22 decisions.
3. Audit-line format asserted at component level: **Correct** — hash prefix composition verified (`test_n36.cpp:94-100`) rather than stderr capture.
4. Socket-level 401s for missing/forged tokens: **Correct** — covered by pre-existing `n30_c` loopback negatives + unit-level `authenticate_bearer` negatives (`test_n36.cpp:109-111`).

## Conclusion

N36 delivers bounded token-scoped authentication for the loopback HTTP MCP as specified. Implementation matches the approved plan's scope (read/write/admin ranges, constant-time comparison, audit-prefix logging, startup config parsing with warning-on-skip), respects audited boundaries (local-only ops remain handler-hardened, stdio path unchanged, legacy token preserved), and achieves the evidence target (38 tests × 3 rounds script-path + × 2 rounds cmake-path, all green). The test suite systematically covers positive capability propagation, three-way privilege escalation denial, malformed-header rejection, unconfigured-env N30 parity, and audit-line format. Explicit deferrals (TLS, OAuth, dynamic users, per-token brain/source restrictions) documented per plan. **No blocking or material findings; outcome PASS.**
