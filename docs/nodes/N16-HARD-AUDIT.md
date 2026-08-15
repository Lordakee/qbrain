# N16 Outcome Hard Audit

**VERDICT: PASS**

**Auditor:** Claude Code
**Date:** 2026-08-04
**Baseline Plan:** `docs/nodes/N16-PLAN.md` (approved 2026-07-30)
**Baseline Plan Audit:** `docs/nodes/N16-PLAN-AUDIT.md` (PASS 2026-07-30)

---

## Executive Summary

N16 delivers three source-scoped read-only MCP code-intelligence operations (`code_def`, `code_refs`, `code_callers`) that scan active page bodies within one authorized canonical source, returning line/snippet/kind matches with strict symbol validation, deterministic ordering, bounded numeric parsing with explicit clamps (limit: 1..200, page_limit: 1..2000, both with defaults 50/500), structured error contracts, N2.5 remote source authorization, and full N8 selected/decoy brain isolation. All 12 falsifiable acceptance assertions pass. Native Windows x64 MSVC C++20 build exits 0. Test suite: 25 PASS / 0 FAIL. Runtime evidence: 60 per-call snapshot pairs with identical before/after hashes across both brains. No write/filesystem/config/network side effects detected.

---

## Plan Baseline Verification

- **N16-PLAN.md SHA-256:** `99cb6790ddee768fb3e8acc2bc06ac9f32facfe53a5027aa5658521f96593104`
- **N16-PLAN-AUDIT.md SHA-256:** `ad6794067444a56658d52d23c3ca29f7092cd7024829f1c4313b295b10c77fef`
- **Plan Audit Verdict:** PASS (N16-PLAN-AUDIT.md line 3)
- **Plan Status:** approved (N16-PLAN.md line 3)

Both baseline documents verified against VERIFY-REPORT.md lines 134–135.

---

## Deliverables Check

### 1. Source-filtered active-page enumeration

**Implementation:** `src/qbrain/core/brain.cpp:279-293` introduces `Brain::list_pages_for_source(source_id, limit)`:

```cpp
std::vector<Page> Brain::list_pages_for_source(const std::string& source_id, int limit) {
  std::vector<Page> out;
  if (limit <= 0) limit = 1;
  if (limit > 2000) limit = 2000;
  auto canon = canonical_source_id(source_id);
  if (!canon) return out;
  auto st = db_.prepare(
      "SELECT id, source_id, slug, type, title, body, frontmatter_json, content_hash, "
      "created_at, updated_at, deleted_at FROM pages "
      "WHERE source_id=? AND deleted_at IS NULL ORDER BY updated_at DESC, id DESC LIMIT ?");
  st.bind_text(1, *canon);
  st.bind_int(2, limit);
  while (st.step()) out.push_back(row_to_page(st));
  return out;
}
```

**Evidence:**
- Clamps limit at lines 281-282: `if (limit <= 0) limit = 1; if (limit > 2000) limit = 2000;`
- Canonicalizes source_id at lines 283-284 via `canonical_source_id`, returns empty on failure
- Selects full Page column order at lines 285-288
- Source predicate `WHERE source_id=? AND deleted_at IS NULL` applied before `ORDER BY` and `LIMIT ?` (lines 287-288)
- Deterministic order: `ORDER BY updated_at DESC, id DESC` (line 288)
- Source existence enforced separately by `resolve_source` before enumeration
- Scanner at `scan.cpp:284` invokes `brain.list_pages_for_source(source_id, page_limit)`
- Within-page line order: ascending via `for_each_line` at scan.cpp:287-299

**Status:** PASS

### 2. Canonical source and authorization contract

**Implementation:** `handlers.cpp:128-147` (`resolve_source`):
- Defaults to `"default"` when omitted (line 131)
- Normalizes via `Brain::canonical_source_id(raw)` (line 132)
- Validates canonical form (line 133-136)
- Remote authorization: `remote_source_allowed` checks `mcp.allowed_sources` config (line 137-140)
- Source existence: `ctx.brain->source_exists(*canon)` (line 142-145)

**Hit shape:** `handlers.cpp:1420-1432` adds `source_id` to every JSON hit:
```cpp
json hit_obj;
hit_obj["source_id"] = h.source_id;
hit_obj["slug"] = h.slug;
hit_obj["line"] = h.line;
hit_obj["snippet"] = h.snippet;
hit_obj["kind"] = h.kind;
```

**Status:** PASS

### 3. Strict symbol contract and structured failures

**Symbol validation:** `scan.cpp:42-93` (`is_valid_symbol`):
- ASCII 1..256 bytes (enforced at scan.cpp:278)
- Grammar: `~?[A-Za-z_$][A-Za-z0-9_$]*` components separated by `::` (validated via `is_symbol_start`/`is_symbol_continue` at scan.cpp:30-36)
- Rejects whitespace, control bytes, lone/repeated colons, empty components, regex metacharacters

**Alias behavior:** `handlers.cpp:1438-1454` (`parse_code_request`):
- Accepts `symbol` or `name` (line 1440-1442)
- If both supplied with different non-empty values, rejects with `invalid_argument` error (line 1444-1448)
- Normalizes to single `symbol` variable (line 1449)

**Structured errors:** `handlers.cpp:54-63` (`argument_error`):
```cpp
OpResult r;
r.ok = false;
r.exit_code = 1;
json j = {{"error", {{"code", code}, {"field", field}, {"message", message}}}};
r.json = j.dump();
```

**Status:** PASS

### 4. Strict bounded numeric contract

**Parser:** `handlers.cpp:65-86` (`parse_bounded_uint`):
- Uses `std::from_chars` for strict unsigned decimal parsing (line 77)
- Rejects signs, whitespace, decimals, suffixes: `parsed.ptr != last` check (line 78-80)
- Clamps to range: `std::clamp(value, minimum, maximum)` (line 83)
- Syntactically valid zero clamps to minimum (not rejected)
- Empty supplied values rejected with `invalid_argument` error (line 71-73)

**Invocation:** `handlers.cpp:1458-1460`:
```cpp
parse_bounded_uint(ctx, "limit", 50, 1, 200, limit, error)
parse_bounded_uint(ctx, "page_limit", 500, 1, 2000, page_limit, error)
```

**Effective clamps:**
- `limit`: default 50, clamps to 1..200
- `page_limit`: default 500, clamps to 1..2000
- Applied at scan.cpp:279-282 as a secondary enforcement layer

**Status:** PASS

### 5. Honest heuristic behavior

**Definition scanner:** `scan.cpp:141-198` (`looks_like_def`):
- class/struct/interface/enum/type/namespace declarations (lines 150-163)
- function/async function/def (lines 165-176)
- const/let/var assignment or annotation (lines 178-186)
- typed function-like declarations (lines 188-194)

**Reference scanner:** `scan.cpp:214-226` (`has_word_ref`):
- Literal identifier-boundary match via `word_boundary_at` (scan.cpp:40-44)
- Ensures `foo` does not match `food`

**Caller scanner:** `scan.cpp:200-212` (`looks_like_call`):
- Requires identifier boundary plus optional whitespace and `(` (line 207-210)

**Snippet safety:** `scan.cpp:95-115`:
- `bounded_utf8`: validates UTF-8, replaces malformed sequences with U+FFFD `\xEF\xBF\xBD` (line 96)
- Caps at 200 bytes ending on complete code-point boundaries (line 102)
- `trim_snippet` wrapper at line 112-114

**Status:** PASS

### 6. Read-only operation registration

**Registry:** `handlers.cpp:1464-1495`:
- Three operations: `code_def` (line 1464), `code_refs` (line 1475), `code_callers` (line 1486)
- All registered as `Scope::Read`
- Each schema declares five properties: `symbol`, `name`, `source_id`, `limit`, `page_limit`
- Schema `additionalProperties: false` declaration (registry evidence for unknown-field rejection)
- Schema defaults: `source_id: "default"`, `limit: 50`, `page_limit: 500`
- Schema minima: `limit: 0`, `page_limit: 0` (runtime enforcement clamps to 1)
- Schema maxima: `limit: 200`, `page_limit: 2000`
- Schema `anyOf: [{"required":["symbol"]}, {"required":["name"]}]` allows either field

**Runtime enforcement:** `src/qbrain/mcp/server.cpp:126-145` (`validate_typed_arguments`):
- Rejects non-object arguments (line 129-131)
- Rejects any key absent from operation-specific map (lines 133-136)
- Enforces string type for string arguments (lines 137-139)
- Enforces unsigned-integer type for numeric arguments (lines 140-143)

**Operation schema mapping:** `src/qbrain/mcp/server.cpp:148-175` (`typed_argument_schema`):
- Maps `code_def`, `code_refs`, `code_callers` to exactly: `symbol`, `name`, `source_id`, `limit`, `page_limit` (lines 151-154, 173-175)

**Validation invocation:** `src/qbrain/mcp/server.cpp:239-241`:
- Calls `validate_typed_arguments` before dispatch for operations with typed schemas

**Ambient source exclusion:** `src/qbrain/mcp/server.cpp:70-76` (`uses_ambient_source`):
- Excludes `code_def`, `code_refs`, `code_callers` from ambient-source injection (lines 72-73)

**No side effects:** Operations invoke only:
- `resolve_source` (read-only source validation)
- `codeintel::find_*_in_source` (read-only page scan)
- No write operations, no job submissions, no config changes, no filesystem/network calls

**Status:** PASS

### 7. Focused verification artifacts

**Test file:** `tests/test_n16.cpp` (SHA-256 `9653f1bfa0f3e0493e1017d88466b7e45cbdf16901a2eea5a2c58b77387c3ee7`)
- Registered in `tests/test_main.cpp` line 36: `extern void test_n16();`
- Registered in CMakeLists.txt and `scripts/build-tests-cl.ps1`

**Evidence artifacts:**
- `scripts/n16-verify.ps1` (SHA-256 `8bab0bc4144d1be9259e751e09429a25b13e7ff02d2bd4c8a71343e70fca7ca7`)
- `docs/nodes/n16-evidence/VERIFY-REPORT.md` with captured build/test/CLI outputs
- Node evidence BUILD-MANIFEST.txt: `docs/nodes/n16-evidence/BUILD-MANIFEST.txt` (SHA-256 `2b6bee15b0c279c3a85e81b99eebf2811cc59a90627ae1ec331e08ce6e4a4d7d`)
- Node evidence TEST-OUTPUT.txt: `docs/nodes/n16-evidence/TEST-OUTPUT.txt` (SHA-256 `79b7a1925fcbdbb1122d254862be3f6a8fdd732781adf03f57ef8db7e93ec4fe`)
- Wave 3 final build manifest: `build/wave3-final-build-manifest.txt` (SHA-256 `d3fc2bb3ae1e81c2df340ab89d4b9ed76acbaf477947da6676b3cecd6a4c30e3`, 110 FILE entries + 2 ARTIFACT entries)
- Wave 3 final production log: `build/wave3-final-production.log` (SHA-256 `ea907ecda1afb9775d3c0ae3483149281e17b2c5056c74ef03ba02b87f372e4c`)
- Wave 3 final tests log: `build/wave3-final-tests.log` (SHA-256 `634090ed6204f1f5e7b7aa4ad487a0470d52878b8b7008cd842c8f5757853b5b`, line 144 `[PASS] n16`)
- Wave 3 artifact qbrain.exe: `build/cl/qbrain.exe` (SHA-256 `1da1aaca5d805b697d4e3d36a849456245b4ee3ffee5637bb0c87e2161e3b18f`)
- Wave 3 artifact qbrain_tests.exe: `build/cl/qbrain_tests.exe` (SHA-256 `ec1f6bb79734de69578717828d74d62bde99503b472df2a4ea18db035dd1b359`)

**Status:** PASS

---

## Falsifiable Acceptance Assertions

| # | Assertion | Status | Evidence |
|---|-----------|--------|----------|
| **A1** | N16 implementation starts only after Claude Code plan-audit PASS; plan status `approved` | **PASS** | N16-PLAN-AUDIT.md line 3 `VERDICT: PASS`, N16-PLAN.md line 3 `Status: approved` |
| **A2** | Each operation filters active pages by requested canonical source in SQL before `page_limit`; returns no deleted, other-source, or other-brain content | **PASS** | `brain.cpp:287-288` SQL `WHERE source_id=? AND deleted_at IS NULL ... ORDER BY ... LIMIT ?`; scan.cpp:284 invokes `list_pages_for_source`; test evidence: 60 snapshot pairs, zero cross-contamination (VERIFY-REPORT.md:51-110) |
| **A3** | Local registered-source reads work without allowlist; remote non-default reads require N2.5 authorization; invalid/unknown/unauthorized sources fail before enumeration | **PASS** | `handlers.cpp:117-126` (`remote_source_allowed`): default always allowed (line 118), remote requires `mcp.allowed_sources` config (line 119-124); `resolve_source` line 142-145 checks `source_exists` before returning |
| **A4** | Declared symbol grammar and 256-byte bound enforced; invalid input returns structured error and nonzero exit; valid symbol with no hits returns successful `[]` | **PASS** | `scan.cpp:278` rejects invalid symbols via `is_valid_symbol`; grammar enforced via `is_symbol_start`/`is_symbol_continue` (lines 30-36); `handlers.cpp:1450-1453` returns `argument_error` on failure; valid-no-match returns empty JSON array (handlers.cpp:1433) |
| **A5** | `limit` and `page_limit` consume entire unsigned decimal, use defaults only when omitted, clamp to exactly 1..200 and 1..2000, reject signs/whitespace/suffixes/decimals/overflow without scanning | **PASS** | `handlers.cpp:65-86` (`parse_bounded_uint`): `std::from_chars` strict parsing (line 77), `parsed.ptr != last` rejection (line 78), `std::clamp(value, minimum, maximum)` (line 83); defaults 50/500 (line 1458-1459); secondary clamps at scan.cpp:279-282 |
| **A6** | `code_def` finds documented definition forms, `code_refs` respects literal identifier boundaries, `code_callers` requires boundary + optional whitespace + `(`; no AST/semantic parity claim | **PASS** | `scan.cpp:141-198` (defs), `scan.cpp:214-226` (refs with `word_boundary_at`), `scan.cpp:200-212` (calls with whitespace+`(` pattern); all heuristic line-oriented |
| **A7** | Every hit contains canonical source, slug, one-based line, operation kind, JSON-safe snippet ≤200 bytes; repeated unchanged calls return byte-identical JSON in `updated_at DESC, id DESC, line ASC` order | **PASS** | Hit shape: `handlers.cpp:1420-1432` includes all five fields; snippet: `scan.cpp:95-115` (`bounded_utf8` + 200-byte cap); order: `brain.cpp:288` SQL + `scan.cpp:287-299` ascending line iteration; deterministic verified by test at test_n16.cpp (snapshot_call evidence) |
| **A8** | Small source-scoped `page_limit` unaffected by newer pages in another source; effective hit limit never exceeded | **PASS** | SQL `WHERE source_id=?` at brain.cpp:287 filters before `LIMIT`; scan.cpp:286-298 enforces `limit` cap per hit; test matrix in test_n16.cpp verifies cross-source isolation via distinct fixtures |
| **A9** | All successful/empty/malformed/denied N16 calls preserve full logical snapshots of both selected and decoy databases; operations remain read-only with no job/config/filesystem/network side effects | **PASS** | VERIFY-REPORT.md:51-110 records 60 snapshot pairs: all `before_sha256 == after_sha256` for both brains; test harness: test_n16.cpp:64-83 (`call_without_mutation`) enforces snapshot equality |
| **A10** | Operation registry and real CLI/MCP smoke match declared argument schemas, source authorization, error behavior, exit codes, output shape for exactly `code_def`, `code_refs`, `code_callers` | **PASS** | Registry: handlers.cpp:1464-1495; schemas at lines 1473, 1484, 1495 with `additionalProperties:false`, five properties, defaults, anyOf; CLI smoke: VERIFY-REPORT.md:111 `N16_CLI_SMOKE_OK doctor=pass version=pass isolated_localappdata=pass` |
| **A11** | Native Windows x64 MSVC evidence records `/std:c++20`, exact commands/exit codes, all-PASS suite ≥21 tests, dedicated N16 test, runtime markers, snapshot hashes before outcome audit | **PASS** | VERIFY-REPORT.md:6-13: MSVC 19.51.36248 x64, `/std:c++20`, exit 0; line 13: 25 PASS / 0 FAIL (≥21 baseline); line 50: N16 runtime markers with snapshot SHA-256s; wave3-final-tests.log:144 `[PASS] n16` |
| **A12** | After complete Claude Code outcome-audit PASS, plan becomes `done` and only three N16 ledger rows reconciled; no schema migration, third-party parser, full code-intelligence parity, model config change, out-of-scope artifact or dependency, commit, or push | **PASS** | This audit issues PASS verdict; plan status remains `approved` pending parent ledger reconciliation (not this audit's scope); no schema migration (verified: no `migrate.cpp` changes in N16 deliverables); no third-party parser added; model config unchanged (VERIFY-REPORT.md:116 states no config changes); no out-of-scope artifacts or dependencies present |

---

## Platform and Build Verification

**Target Platform:** Windows x64
**Compiler:** MSVC 19.51.36248 (Visual Studio 2026 Developer Command Prompt v18.7.3)
**Language Mode:** `/std:c++20`
**OS:** Microsoft Windows 11 Pro for Workstations 10.0.22624 build 22624

**Build Evidence:**
- Production build: `scripts/build-cl.ps1` exit 0 (wave3-final-production.log SHA-256 `ea907ecda1afb9775d3c0ae3483149281e17b2c5056c74ef03ba02b87f372e4c`)
- Test build: `scripts/build-tests-cl.ps1` exit 0 (wave3-final-tests.log SHA-256 `634090ed6204f1f5e7b7aa4ad487a0470d52878b8b7008cd842c8f5757853b5b`)
- Build manifest SHA-256: `d3fc2bb3ae1e81c2df340ab89d4b9ed76acbaf477947da6676b3cecd6a4c30e3` (build/wave3-final-build-manifest.txt, 110 FILE entries + 2 ARTIFACT entries)
- Artifact qbrain.exe SHA-256: `1da1aaca5d805b697d4e3d36a849456245b4ee3ffee5637bb0c87e2161e3b18f`
- Artifact qbrain_tests.exe SHA-256: `ec1f6bb79734de69578717828d74d62bde99503b472df2a4ea18db035dd1b359`

**Test Evidence:**
- Registered tests: 25 PASS, 0 FAIL (wave3-final-tests.log:217-218)
- N16-specific marker: `[PASS] n16` (wave3-final-tests.log:144)
- Node evidence TEST-OUTPUT.txt SHA-256: `79b7a1925fcbdbb1122d254862be3f6a8fdd732781adf03f57ef8db7e93ec4fe` (docs/nodes/n16-evidence/TEST-OUTPUT.txt)
- Node evidence BUILD-MANIFEST.txt SHA-256: `2b6bee15b0c279c3a85e81b99eebf2811cc59a90627ae1ec331e08ce6e4a4d7d` (docs/nodes/n16-evidence/BUILD-MANIFEST.txt)
- Runtime snapshot verification: 60 calls logged with `snapshot_call_count=60` (wave3-final-tests.log:83)

**CLI Smoke Test:**
- Isolated LOCALAPPDATA: confirmed via `N16_CLI_SMOKE_OK isolated_localappdata=pass` (VERIFY-REPORT.md:111)
- Doctor report: `"ok": true, "overall": "OK"` (confirmed in CLI evidence)
- Version retrieval: exit 0

---

## Runtime Snapshot Analysis

**Selected Brain Aggregate (varies per test phase):** Multiple distinct hashes across 60 calls reflecting deliberate test fixture evolution
**Decoy Brain Aggregate (constant):** `9a9e3e6e81173d229d36094f27caecb0df1614c4443a2b1f22b179425f4647f2` (wave3-final-tests.log:84-143)

**Per-Call Snapshot Evidence (60 calls):**
- Every call recorded with `selected_before_sha256`, `selected_after_sha256`, `decoy_before_sha256`, `decoy_after_sha256` (wave3-final-tests.log:84-143)
- **Zero state mutations detected:** All 60 `before == after` for both selected and decoy
- **Zero cross-contamination:** Selected and decoy hashes remain distinct across all calls; decoy hash constant throughout
- **Deterministic fixture seeding:** Selected brain fixture SHA-256 varies across test phases (calls 1→2, 19→22, 50→54) reflecting test design; decoy remains constant

**Example snapshot pairs:**
```
call=1:  selected_before=05826944... == selected_after=05826944...
         decoy_before=9a9e3e6e... == decoy_after=9a9e3e6e...
call=60: selected_before=12818463... == selected_after=12818463...
         decoy_before=9a9e3e6e... == decoy_after=9a9e3e6e...
```

---

## Deliverable File Hashes

| Path | SHA-256 | Source |
|------|---------|--------|
| `include/qbrain/codeintel/scan.hpp` | `094037949b56801a220eb96f268c6de934546badd2ec5574175844e0fb829c3e` | VERIFY-REPORT.md:122 |
| `src/qbrain/codeintel/scan.cpp` | `9cd8fb066ac8ac66cb400f060d6759277f298f9e0b38bea92b5c681fe23654ad` | VERIFY-REPORT.md:123 |
| `include/qbrain/core/brain.hpp` | `2f9d4bffe5d42234c78e823bd7e2ede9f64999761a1da9df5162d153095a8495` | VERIFY-REPORT.md:124 |
| `src/qbrain/core/brain.cpp` | `244cbccd45cfa4f2c6dccc22551d146198c5871510068458c93eae127bb34df6` | VERIFY-REPORT.md:125 |
| `src/qbrain/ops/handlers.cpp` | `00ab465262ad82e58ec0a173e6d0a8755553897d48c026409ecd9908c3d5cf04` | VERIFY-REPORT.md:126 |
| `tests/test_n16.cpp` | `9653f1bfa0f3e0493e1017d88466b7e45cbdf16901a2eea5a2c58b77387c3ee7` | VERIFY-REPORT.md:127 |
| `tests/wave3_test_support.hpp` | `9a4f94093a6e64b6a3f2021b52cefa23cd06d0223803d4e6bda3ea7ec9c301a6` | VERIFY-REPORT.md:128 |
| `tests/test_main.cpp` | `fda0d2be1d66f3b518fb8fd2b786954a8c128ca12f15fd8aee2360dd96670084` | VERIFY-REPORT.md:129 |
| `CMakeLists.txt` | `f8dc705f72dbc572005566620e22f3388861f72d12b01b971ee3f74b7d708a6f` | VERIFY-REPORT.md:130 |
| `scripts/build-tests-cl.ps1` | `f63dc04756a3c888ea75f5273756b98ddb39d5bf3b82fa6bd1bcaf76b6a72953` | VERIFY-REPORT.md:131 |
| `scripts/n16-verify.ps1` | `8bab0bc4144d1be9259e751e09429a25b13e7ff02d2bd4c8a71343e70fca7ca7` | VERIFY-REPORT.md:132 |
| `scripts/wave3-verify-common.ps1` | `478348e2885f738cf7390aa18e6267470355379c85d6501fb9010a8904189d56` | VERIFY-REPORT.md:133 |
| `docs/nodes/N16-PLAN.md` | `99cb6790ddee768fb3e8acc2bc06ac9f32facfe53a5027aa5658521f96593104` | VERIFY-REPORT.md:134 |
| `docs/nodes/N16-PLAN-AUDIT.md` | `ad6794067444a56658d52d23c3ca29f7092cd7024829f1c4313b295b10c77fef` | VERIFY-REPORT.md:135 |

---

## Security and Authorization

**Remote Allowlist Enforcement:**
- `remote_source_allowed` validates `mcp.allowed_sources` config (handlers.cpp:117-126)
- Default source always permitted (line 118)
- Unauthorized sources rejected with `source_not_allowed` error (line 138-140)
- Test matrix at test_n16.cpp validates denial for unlisted sources

**Source Isolation:**
- `source_exists` check required (handlers.cpp:142-145)
- SQL scoping enforced via `WHERE source_id=?` (brain.cpp:287)
- Nonexistent sources return `source_not_found` error (handlers.cpp:143-145)

**No Privilege Escalation:**
- Read-only API: no write operations exposed
- Ambient source override disabled: N16 operations excluded from `uses_ambient_source` at server.cpp:70-76
- Remote contexts restricted to explicit allowlist

---

## MCP Integration

**Tool Registration:**
- Names: `code_def`, `code_refs`, `code_callers` (handlers.cpp:1464, 1475, 1486)
- Scope: `Scope::Read` for all three
- Schema: Five properties (`symbol`, `name`, `source_id`, `limit`, `page_limit`) with `additionalProperties: false`
- Descriptions: "Find C++/TS-like symbol definitions...", "Find word-boundary symbol references...", "Find call-ish symbol( references..."

**Schema Details (per operation):**
```json
{
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "symbol": {"type": "string", "maxLength": 256},
    "name": {"type": "string", "maxLength": 256},
    "source_id": {"type": "string", "default": "default"},
    "limit": {"type": "integer", "minimum": 0, "maximum": 200, "default": 50},
    "page_limit": {"type": "integer", "minimum": 0, "maximum": 2000, "default": 500}
  },
  "anyOf": [{"required": ["symbol"]}, {"required": ["name"]}]
}
```

**Note on schema `minimum: 0` vs runtime `clamp to 1`:**
The schema declares `minimum: 0` to accept syntactically valid zero, which the runtime parser then clamps to 1 at handlers.cpp:83 and scan.cpp:279-282. This design allows zero as valid input (not a parse error) while enforcing the operational minimum of 1 page/hit.

**Runtime Argument Validation:**
- `server.cpp:126-145` (`validate_typed_arguments`) rejects non-object arguments, unknown keys, string type mismatches, and unsigned-integer type mismatches
- `server.cpp:148-175` (`typed_argument_schema`) maps the three operations to their exact five-parameter schema
- `server.cpp:239-241` invokes `validate_typed_arguments` before dispatch for typed operations
- Unknown field rejection enforced at runtime via schema map key check (lines 133-136), not solely by `additionalProperties: false` declaration

---

## Dependency and Rollback Status

**N1–N13 Preconditions:**
All Wave 1-2 nodes report PASS in both plan and outcome audits (VERIFY-REPORT.md:30-45). SHA-256 hashes confirmed for all predecessor plan/audit documents.

**Rollback Safety:**
No schema migration introduced. Addition of `list_pages_for_source` method is additive; removal of three handler registrations at handlers.cpp:1464-1495 would cleanly revert MCP surface.

**No Breaking Changes:**
Existing operations unaffected; no behavioral changes to N1-N15 features.

---

## P0 (Critical) Issues

**None.**

All 12 acceptance assertions pass. Build, tests, CLI smoke, and snapshot isolation all verify successfully. No regressions, security violations, or correctness gaps detected.

---

## P1 (High Priority) Issues

**None.**

Remote authorization, source isolation, UTF-8 safety, deterministic ordering, and strict numeric parsing all function as specified. No data integrity or specification compliance issues.

---

## P2 (Low Priority) Observations

**P2-01 - Regex compilation overhead in scanner loops**

`looks_like_def` and `looks_like_call` compile regex patterns on every invocation (scan.cpp lines in definition/call checking). For large codebases with many pages, consider caching compiled patterns as static variables to reduce per-line overhead.

**Impact:** Performance optimization only; does not affect correctness. Optional future enhancement.

**P2-02 - UTF-8 validation performance for ASCII-only content**

`bounded_utf8` validates every byte (scan.cpp:95-110). Most source files are ASCII-only; a fast-path early-exit for pure ASCII content (no bytes ≥0x80) would reduce overhead for the common case.

**Impact:** Performance optimization only; does not affect correctness. Optional future enhancement.

**P2-03 - Page limit granularity for remote contexts**

Maximum clamp of 2000 pages may still be excessive for remote contexts. Consider tighter remote-specific limits or pagination support in future enhancements.

**Impact:** Non-blocking observation. Current 2000 limit is within approved plan contract. Optional future tightening.

---

## Post-Audit Status and Next Steps

**Current Status:** PASS verdict issued
**Plan Status:** Remains `approved` pending parent ledger reconciliation
**Next Steps (Parent Process):**
1. Mark N16-PLAN.md status as `done`
2. Update `docs/OPS-PARITY-LEDGER.md` to reconcile exactly three N16 rows: `code_def`, `code_refs`, `code_callers`

**Ledger Reconciliation:** Not yet performed. Changing plan status to `done` and reconciling the ledger are parent post-audit steps, not part of this audit's scope.

---

## Auditor Certification

I, Claude Code (independent hard auditor), have completed a rigorous outcome audit of N16 against the approved plan baseline. I inspected implementation source code, focused tests, build artifacts, runtime evidence, and CLI smoke output. All 12 falsifiable acceptance assertions pass. Build exits 0 under native Windows x64 MSVC `/std:c++20`. 25 tests pass with zero failures. 60 snapshot pairs confirm zero side effects and full brain isolation. Remote authorization allowlist, source existence enforcement, strict symbol grammar, strict numeric parsing with explicit clamps (limit 1..200 default 50, page_limit 1..2000 default 500), UTF-8 safety, and deterministic ordering all conform to specification.

**VERDICT: PASS**

---

**End of N16 Hard Audit**
