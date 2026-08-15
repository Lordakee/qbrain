# N33 HARD AUDIT (outcome)

**Auditor**: Claude Code (claude-opus-5, effort max), watchdog task qbrain-n33-hard-audit-claude-20260815, 2026-08-15/16
**Audit basis**: docs/nodes/N33-PLAN.md (approved)
**Human authorization**: user instruction 2026-08-15 (quoted verbatim in the dispatch log)

---

Now I'll compile the complete audit based on all evidence collected.

---

## N33 OUTCOME HARD AUDIT

**Auditor**: Claude Code (claude-opus-5, effort max), 2026-08-16  
**Audit scope**: N33 real multimodal ingestion/search against approved plan `docs/nodes/N33-PLAN.md`  
**Evidence basis**: Implementation files, test suite, fixture artifacts, verification transcripts (PRE-GATE.json, FINAL-VERIFY-SCRIPT.txt, FINAL-VERIFY-CMAKE.txt)

---

### VERDICT: PASS

---

### Deliverables Verification

**D1 — Minimal image parser** (`src/qbrain/files/image_meta.cpp` + `.hpp`)  
✓ PNG parser: magic + IHDR walk (width/height/bit_depth/color type → components), chunk-count bound 1000, scan bound 64 KiB  
✓ JPEG parser: SOI + marker walk + SOF0/SOF2 (height/width/precision/components), marker-count bound 100  
✓ 32 MiB input cap enforced (image_meta.hpp:14 `kImageMetaMaxInputBytes`)  
✓ Bounded failure: malformed/truncated → format="unknown" + bounded note (<= 80 chars), never throws  
Evidence: image_meta.cpp:1-248, bounds constants at lines 14-17, parse_png/parse_jpeg implementations with explicit chunk/marker counters and early termination

**D2 — MIME-by-content** (image_meta.cpp:195-246)  
✓ Manual magic-byte sniff: PNG signature `\x89PNG\r\n\x1a\n` → image/png, JPEG `\xFF\xD8\xFF` → image/jpeg (zero external dependencies)  
✓ Extension-based fallback for unrecognized content  
✓ Cross-check: `ext_mismatch` flag set when declared extension contradicts content-derived type  
Evidence: content_mime() at image_meta.cpp:195-219, sniff_mime() at 231-246

**D3 — Provider contract** (`src/qbrain/ai/embed.cpp` embed_image, lines 181-237)  
✓ OpenAI-compatible embeddings endpoint via base64 data-URL POST, 30s timeout, 2 MiB response cap  
✓ No credentials → immediate unavailable (no_credentials=true), no network attempt  
✓ Provider failure → degraded unavailable (not error), fail-open contract  
✓ Credential isolation: redact_provider_error (lines 73-98) strips base_url, api_keys, generic URL patterns, truncates to 200 chars  
✓ Mock mode (QBRAIN_EMBED_MOCK=1): deterministic vector from hash of first 4 KiB → 64-bit seed → splitmix64 RNG → 64 dims (lines 102-123)  
Evidence: embed_image implementation with three exit paths (ok, no_credentials unavailable, provider-failure unavailable), redaction wrapper with URL scrubbing + truncation, mock_image_vector with content-hash seed

**D4 — Integration** (`src/qbrain/ops/handlers.cpp`)  
✓ `put_raw_data` (lines 3324-3378): image metadata stored in existing raw_data.meta_json field (merge with user-provided meta), no schema change  
✓ `get_raw_data` (lines 3381-3441): surfaces stored metadata block additively  
✓ `file_upload` (lines 3029-3121): 32 MiB pre-write check (line 3046), metadata in response only (not persisted to file_index), existing file_index schema unchanged  
✓ `search_by_image` (lines 3453-3570): embed_image call → cosine rank when ok; unavailable → `{"results":[],"mode":"unavailable","reason":"..."}` + exit 0 (fail-open)  
✓ 32 MiB bound constant: kN33MaxImageBytes = kImageMetaMaxInputBytes (handlers.cpp:2929-2930)  
Evidence: handlers.cpp N33 D4 comment block at line 2923, four op registrations with image metadata handling, no ALTER TABLE statements, file_index schema untouched

**D5 — Fixtures + tests**  
✓ `tests/fixtures/img/`: 14 fixtures (4 valid PNG/JPEG, 3 spoof, 4 malformed/truncated, 3 bounds-exercising) + 14 ground-truth .json companions  
✓ `tests/fixtures/img/gen_fixtures.py`: provenance script (programmatic construction, no third-party binaries)  
✓ `tests/test_n33.cpp`: 636 lines, 8 test sections (exact match, spoof, malformed, mock, no-creds fail-open, provider injection, 32MiB bound, backward compat)  
Evidence: Glob result lists all 14 fixture pairs, gen_fixtures.py constructs valid CRC-32 PNG chunks + structurally complete JPEG segments, test_n33.cpp covers all acceptance assertions

**D6 — Evidence**  
✓ `docs/nodes/n33-evidence/PRE-GATE.json`: baseline commit 8f2e183 (post-approval, honest late-capture note), plan SHA-256 recorded  
✓ `FINAL-VERIFY-SCRIPT.txt`: clean build + 3 suite runs = 36 tests × 3 = 108 PASS, 0 FAIL  
✓ `FINAL-VERIFY-CMAKE.txt`: fresh CMake + 2 suite runs = 36 tests × 2 = 72 PASS, 0 FAIL  
✓ Total: 36 registered tests (33 baseline + n32_scan_integration + n34 + n33_multimodal), 180 executions (36×5 rounds), 0 failures  
Evidence: PRE-GATE.json lines 3-7, FINAL-VERIFY-SCRIPT grep output shows 3 blocks of 36 [PASS] lines (lines 96-1349, 1352-2605, 2607-3860), FINAL-VERIFY-CMAKE shows 2 blocks (lines 188-1441, 1443-2696)

---

### Acceptance Assertions (8/8 verified)

**#1 — Exact metadata match for valid PNG/JPEG**  
test_n33.cpp:307-340: Constructed PNG (123×456×8-bit RGBA) and JPEG (200×100×8-bit RGB) with correct CRC/SOF headers → parsed metadata matches expected blocks exactly (format, width, height, bit_depth, components). get_raw_data surfaces the stored block.  
✓ PASS (lines 311, 315, 323)

**#2 — Spoof detection (content-based classification + ext_mismatch flag)**  
test_n33.cpp:343-383: PNG content under .jpg extension → mime="image/png", content_based=true, declared_ext_mismatch=true. Text content under .png name (file_upload path) → mime="text/plain", content_based=true, declared_ext_mismatch=true.  
✓ PASS (lines 348-352, 369-371)

**#3 — Malformed/truncated/overlimit bounded (no crash, format=unknown, bounded note)**  
test_n33.cpp:386-416: Truncated PNG IHDR, JPEG SOI+garbage, empty input → no crash, format="unknown" or absent dimensions, bounded failure modes. Parser never throws on untrusted input.  
✓ PASS (lines 388-415)

**#4 — Mock determinism (same image → identical vector; different images differ)**  
test_n33.cpp:419-465: QBRAIN_EMBED_MOCK=1 → same PNG queried twice yields byte-identical vectors (line 426: `e1.vector == e2.vector`), different content (JPEG) yields different vector (line 428: `e3.vector != e1.vector`), 64 dimensions (line 429). search_by_image: same query file → identical query_vector JSON across calls (line 448).  
✓ PASS

**#5 — No credentials → fail-open unavailable + text search intact**  
test_n33.cpp:468-504: All credential env vars cleared + config keys cleared → search_by_image returns `{"results":[],"mode":"unavailable","reason":"no provider credentials"}` with exit_code=0 (lines 479-484). embed_image contract: immediate unavailable, no_credentials=true, error="no provider credentials" (lines 486-489). Text search on same brain unaffected (lines 492-503).  
✓ PASS

**#6 — Provider failure injection → unavailable + no URL/key leak**  
test_n33.cpp:507-557 (Windows-only, Winsock infrastructure): (a) Connection refused to closed loopback port → unavailable, redacted error ≤200 chars, no leak (lines 515-524). (b) Live 401 response with WWW-Authenticate realm + body containing "sk-n33b-SECRETKEY" and "https://api.n33-redact.example" → output contains neither (n33_leaks_secrets returns false, lines 542-547). Oversized input (32MiB+10) → bounded before network (line 555).  
✓ PASS (negative fixture for credential isolation present and verified)

**#7 — Existing files/raw_data tests zero-modification pass**  
test_n33.cpp:596-631: Legacy row (pre-N33 shape, direct Brain API write) → reads fine, no image key (lines 598-605). Plain text through put_raw_data → no image block, user meta passes through (lines 608-619). Non-image file_upload → classic response shape, no image key (lines 622-630). Full suite: 36 tests pass in all 5 verification runs, including all pre-existing tests (FINAL-VERIFY transcripts: 0 FAIL across 180 executions).  
✓ PASS

**#8 — Full suite dual-path two rounds all green (runtime-recorded count per AMD-6)**  
FINAL-VERIFY-SCRIPT.txt: script build path, 3 rounds, 36 tests each, lines 96-3860 show 108 `[PASS]` lines (36×3), 0 `[FAIL]`.  
FINAL-VERIFY-CMAKE.txt: CMake build path, 2 rounds, 36 tests each, lines 188-2696 show 72 `[PASS]` lines (36×2), 0 `[FAIL]`.  
Test count 36 = 33 (pre-N3x baseline per test_main.cpp) + 3 (n32_scan_integration, n34, n33_multimodal), recorded from actual executable output.  
✓ PASS (count exact, dual-path verified, zero failures)

---

### Reconciliation Judgments

**(1) Schema version 12→13 pin updates in 18 pre-existing tests**  
grep output confirms test_n17/n19/n20/n22/n34 contain schema_version assertions. The plan stated N34 mandates v13 bump (jobs.parent_id / jobs.depth columns), requiring mechanical update of test metadata pins from 12 to 13. This is a test-internal assertion update, not a logic change. The tests still verify the same behaviors against the new schema baseline. migrate.cpp:360-389 shows v13 adds job hierarchy columns (N34 D1), and doctor now requires schema_version >= 13 (migrate.cpp:586-589). N33 itself makes no schema change (plan D4: "无任何 schema 迁移、无新列").  
**Judgment**: ACCEPTABLE. Metadata pin updates preserve test intent; the v13 migration is N34's deliverable, not N33's. N33 correctly uses the existing raw_data.meta_json field (no ALTER TABLE).

**(2) N32 mode/degraded_reason as text trailer instead of JSON**  
Not N33's concern; N32 audit covers this.

**(3) N33 file_upload returns image metadata in response only (not persisted)**  
Plan D4 explicitly states this per P0-1 adoption: "file_upload 保持现有 file_index 存储（仅 MIME-by-content 与 size），解析出的元数据仅出现在响应中（临时性，不落库）". handlers.cpp:3046-3121 confirms: metadata extraction attempts only when size <= 32MiB, result appears in the response JSON (line 3088: `res["image"] = image_block`), file_index schema untouched. Acceptance #7 backward-compat section verifies non-image uploads keep classic response shape.  
**Judgment**: CORRECT per plan. This narrowing was the resolution to P0-1 (schema migration contradiction).

**(4) N34 spawn_children bug fix (insert.reset() rebind)**  
Not N33's concern; N34 audit covers this.

**(5) PRE-GATE captured at merge time (baseline 8f2e183) instead of pre-dispatch**  
PRE-GATE.json line 7 honestly documents: "Per-node PRE-GATE was captured by the parent at merge time rather than pre-dispatch: the implementation baseline is the pushed approval commit 8f2e183". The hashes are from that commit (verifiable via `git show 8f2e183`). The final verification transcripts record post-implementation runs. This is a process-timing note, not a defect—the baseline is documented and the final runs are complete.  
**Judgment**: ACCEPTABLE. Honest documentation; final verification is complete and recorded.

---

### Security / Rollback / Parallelism

**Security notes verified**:  
- PNG chunk limit 1000, JPEG marker limit 100 (image_meta.hpp:16-17) ✓  
- No pixel decoding, no execution ✓  
- Credentials never persisted, redaction on all error paths (embed.cpp:73-98) ✓  
- Network only when credentials present, bounded to configured endpoint ✓  
- Backward compat: meta_json consumers can check field existence (acceptance #7 demonstrates) ✓

**Rollback**: No schema migration (v13 is N34's), image_meta + embed_image are new modules, metadata is additive JSON. Entire node can be independently reverted. ✓

**Parallelism**: N32 (codeintel/astlite) / N33 (files/image_meta) / N34 (jobs hierarchy) module implementations are disjoint. handlers.cpp integration serialized by parent per AMD-3 (all three plans state this). No file ownership conflicts. ✓

---

### Findings

**P0**: None.

**P1**: None.

**P2**: None.

---

### Conclusion

N33 delivers all 6 plan artifacts (D1-D6) with complete implementation, comprehensive test coverage (8 sections, 636 lines), and 14 programmatically-constructed fixtures with ground-truth companions. All 8 acceptance assertions are satisfied with concrete evidence: exact metadata match for valid images, spoof detection with content-wins + mismatch flag, bounded handling of malformed/truncated/overlimit input, deterministic mock vectors, no-credentials fail-open (exit 0, results=[], text search intact), provider-failure injection with credential redaction verified via negative fixture (401 + WWW-Authenticate leaks nothing), backward compatibility (legacy rows + plain text pass through unchanged), and dual-path verification (script + CMake, 5 rounds total, 36 tests × 5 = 180 executions, 0 failures). The 32 MiB input bound is enforced at handler entry before disk write (D4 + P1-1 adoption). MIME detection uses manual magic bytes (PNG/JPEG signatures) with zero external dependencies (D2 + P0-5 adoption). The embed_image provider contract implements credential isolation (strip base_url, api_key, generic URLs, truncate to 200 chars) per D3 + P0-4 adoption, with mock mode using content-hash seed (first 4 KiB → 64-bit → deterministic RNG) per P1-2 adoption. Image metadata is stored only in raw_data.meta_json (no ALTER TABLE, no new columns); file_upload metadata is response-only per P0-1 adoption. The schema_version 12→13 pin updates in pre-existing tests are mechanical metadata changes for N34's v13 bump, not logic alterations—N33 itself makes no schema migration. Test count 36 (33 baseline + 3 new N3x tests) is recorded from executable output per AMD-6. All round-1 and round-2 plan audit findings (5 P0, 2 P1, 3 P2) were correctly adopted. No blocking defects found.

**VERDICT: PASS**
