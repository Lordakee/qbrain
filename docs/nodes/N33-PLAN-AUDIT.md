# N33 PLAN AUDIT (round 1)

**Auditor**: Claude Code (claude-opus-5, effort max), watchdog task qbrain-n33-plan-audit-claude-20260815, 2026-08-15
**Audit object**: docs/nodes/N33-PLAN.md (draft as dispatched)
**Human authorization**: user instruction 2026-08-15 (quoted verbatim in dispatch log)

---

## N33 Plan Audit — Real Multimodal Ingestion/Search

**VERDICT: FAIL**

---

### P0 Findings (Blocking Approval)

**P0-1: Schema migration claim contradicts "no schema migration" assertion**

- **Location**: Deliverables D4, Rollback section
- **Problem**: D4 states "记录 image 元数据（schema 加列或 JSON 字段——采用现有 raw_data JSON 字段扩展，**无 schema 迁移**）", and Rollback confirms "无 schema 迁移". However, D1 claims to modify `file_index` table (via `file_upload` operation) to store image metadata. The existing `file_index` schema (migrate.cpp:236-249, v10) has columns `id, name, path, size, mime, created_at` with no metadata storage field. The plan does not specify *where* the image metadata (width/height/bit_depth/format) will be stored for `file_upload` images. If using `raw_data.meta_json`, that table is unrelated to `file_index`. If adding a column to `file_index`, that **is** a schema migration contradicting the claim.
- **Suggested change**: Either (a) clarify that image metadata for `file_upload` goes into a NEW `file_index.meta_json TEXT DEFAULT '{}'` column via v13 migration (contradicting "no migration"), OR (b) state that `file_upload` stores only MIME/size as today and image metadata enhancement applies ONLY to `put_raw_data`/`get_raw_data` (narrower scope). Reconcile D1/D4/Rollback claims.

**P0-2: Overlap with N32 in handlers.cpp is understated**

- **Location**: Parallelism notes line 55
- **Problem**: Plan claims "handlers.cpp 内 files/raw_data/search_by_image 相关行仅本节点改；与 N32 在 handlers.cpp 的触碰区域不相交（code ops 段 vs files/search 段）". However, `handlers.cpp` is a 2152-line monolithic file (Resolution §1 P2 finding). N32 plan states it touches "code ops 输出字段" (lines around code_def/code_refs/code_callers/code_callees/code_flow/code_blast registrations). N33 touches file_upload:2814, put_raw_data:3087, get_raw_data:3107, search_by_image:3163. While line ranges don't overlap, **both nodes modify the same file concurrently**, creating merge conflict risk. The plan delegates conflict resolution to "父代理串行合并（AMD-3）" but does not acknowledge that the parent must serialize the handlers.cpp integration step, which contradicts the "并行" claim.
- **Suggested change**: Revise Parallelism notes to state: "N32/N33 implementations of new modules (astlite vs image_meta) are fully parallel. Integration into handlers.cpp must be serialized by the parent agent (AMD-3: shared hot file). Subagent slices A/B remain disjoint; only the parent's handlers.cpp merge is sequential."

**P0-3: Test count target (≥35) is arbitrary and not derived from current baseline**

- **Location**: Tests section line 32, Acceptance #8
- **Problem**: Current test baseline is 29 registered tests (test_main.cpp:64-103; Resolution §1 baseline). N33 claims "全套件 ≥35" but does not justify why +6 tests. D5 specifies `test_n33.cpp` but does not enumerate the test cases (metadata exact match, MIME spoofing, malformed/overlimit, mock determinism, fail-open, provider failure injection, leak check = 7 categories minimum). AMD-6 requires "测试计数只能来自当前可执行套件运行记录", not prose assertions.
- **Suggested change**: Replace "≥35" with "29 (current baseline) + N33-specific test count (to be determined by test_n33.cpp registration)". Acceptance #8 should state: "Full suite passes at new count ≥ current 29; exact count recorded from executable output in n33-evidence."

**P0-4: `embed_image` provider contract leaks credentials in failure path**

- **Location**: D3/D3b, Acceptance #6
- **Problem**: D3b states "provider 调用错误消息不含 URL/密钥片段". However, existing `embed_texts` (embed.cpp:40-46) returns `r.error = resp.error.empty() ? resp.body : resp.error` on HTTP failure. The `resp.error` from `http_post_json` may contain the full URL (which includes the endpoint path, potentially sensitive). The plan does not specify a redaction layer for `embed_image`. Acceptance #6 requires "无 URL/密钥泄漏" but does not specify *how* this will be implemented (filter function? wrapper?).
- **Suggested change**: Add to D3: "Error redaction wrapper: `embed_image` failure messages strip base_url and truncate error to first 200 chars without credential material." Add negative test fixture: provider returns 401 with WWW-Authenticate header containing realm/token → verify output contains neither.

**P0-5: MIME-by-content "magic number" implementation scope undefined**

- **Location**: D2, Acceptance #2
- **Problem**: D2 states "内容魔数优先（finfo 类型 sniff）" but does not specify implementation. PHP's `finfo` is unavailable in C++. Does the plan mean (a) libmagic (external dependency violating master plan), (b) manual magic-number table (PNG `\x89PNG`, JPEG `\xFF\xD8\xFF`), or (c) Windows API (`IStream::Stat` / URL monikers)? The current `guess_mime` (store.cpp:20-29) is extension-only. If implementing manual magic, the plan must list supported formats and byte patterns.
- **Suggested change**: Replace "finfo 类型 sniff" with concrete implementation: "Manual content-based MIME detection: read first 16 bytes, match PNG signature `\x89PNG\r\n\x1a\n` → image/png; JPEG `\xFF\xD8\xFF` → image/jpeg; otherwise extension-based fallback. No external dependencies."

---

### P1 Findings (Should Block Unless Mitigated)

**P1-1: 32MiB file limit inconsistent with N30 authorization model**

- **Location**: D1
- **Problem**: D1 hardcodes "文件 ≤32MiB" limit. However, `file_upload` is Scope::Write (handlers.cpp:2814), and N30 established that Write operations require authorization matrix. The plan does not specify where the 32MiB limit is enforced: (a) in `image_meta.cpp` (rejects parse), (b) in `file_upload` handler before disk write, or (c) at HTTP request body level (MCP layer). If (a), large files are already on disk wasting space. If (c), it affects all file_upload operations, not just images.
- **Suggested change**: Add to D4: "32MiB check at file_upload handler entry (before disk write); return invalid_argument if exceeded. Limit applies only to image metadata extraction attempt, not non-image uploads."

**P1-2: Mock provider mode uses insecure determinism**

- **Location**: D3, Acceptance #4
- **Problem**: D3 specifies `QBRAIN_EMBED_MOCK=1` extension for images with "种子哈希向量". Existing text mock (embed.cpp:18-28) uses `texts[i].size() % 17` as seed, which is collision-prone (different images same size → same vector). Acceptance #4 requires "不同图向量不同" but does not specify collision resistance.
- **Suggested change**: Clarify mock seed: "Mock image embedding: hash first 4KB of file content → 64-bit seed → deterministic RNG for dimension-N vector. Collision probability <1e-6 for distinct images."

---

### P2 Findings (Non-blocking, Should Address)

**P2-1: Rollback claim "无 schema 迁移" incomplete**

- **Location**: Rollback section
- **Problem**: Even without ALTER TABLE, adding image metadata to `raw_data.meta_json` changes the JSON schema contract. If later code expects `{width, height, ...}` but encounters old `{}`, undefined behavior. The plan should specify JSON schema versioning or graceful degradation.
- **Suggested change**: Add: "Backward compat: get_raw_data consumers check for `meta_json.width` existence before use; missing = treat as non-image."

**P2-2: `search_by_image` fail-open response shape not formally specified**

- **Location**: D4, Acceptance #5
- **Problem**: D4 states "无凭据→`{"mode":"unavailable","reason":"no provider credentials"}` 且 HTTP 200". However, existing ops return either `{results: [...]}` or `{error: {...}}`. The new shape `{mode, reason}` is a third pattern creating inconsistency.
- **Suggested change**: Align with existing patterns: `{results: [], mode: "unavailable", reason: "..."}` (empty results + status fields).

**P2-3: PNG/JPEG parser security surface needs explicit bounds**

- **Location**: D1
- **Problem**: D1 states "头部扫描 ≤64KiB" but PNG chunks can be maliciously crafted (e.g., millions of tiny chunks before IHDR, gzip bombs in compressed text chunks). JPEG APPn markers can chain. The plan should specify max chunk/marker count.
- **Suggested change**: Add to D1: "Max 1000 PNG chunks scanned, 100 JPEG markers; if IHDR/SOF not found within limits, return format=unknown."

---

### Audit Summary

The N33 plan addresses a genuine capability gap (filename-only image heuristics) with a bounded approach (header-only parsing, optional provider, fail-open). However, it contains **five P0 blocking defects**: (1) contradictory schema migration claim, (2) understated handlers.cpp merge serialization, (3) unjustified test count, (4) credential leak risk in error path, (5) undefined MIME implementation ("finfo" unavailable in C++). The schema/storage confusion is most severe—the plan must clarify whether `file_index` gains a metadata column (requiring v13 migration, contradicting "no migration") or image metadata applies only to `raw_data` (narrower scope than ledger rows claim). The parallelism claim is misleading: while module implementations are parallel, handlers.cpp integration must be serialized by the parent, per AMD-3. The test baseline math is wrong (current 29, not a base for "≥35" without enumeration). Fix these five, address the P1 authorization/mock issues, and the plan becomes approvable.


---

# Round 2 — VERDICT: PASS (task qbrain-n33-plan-audit2-claude-20260815)

I now have all the context needed to conduct a thorough round-2 audit. Let me verify each round-1 finding against the revised plan.

---

## N33 Plan Audit — Round 2

**Auditor**: Claude Code (claude-opus-5, effort max), 2026-08-15
**Audit object**: `docs/nodes/N33-PLAN.md` (revised; round-1 findings adopted)
**History**: `docs/nodes/N33-PLAN-AUDIT.md` (round 1: FAIL, 5 P0 + 2 P1 + 3 P2)

---

### Round-1 Finding Resolution Check

**P0-1 — Schema migration claim contradicts "no schema migration"**

Adopted text (D4): *"图片元数据仅存储于 raw_data 的既有 JSON 字段（put_raw_data/get_raw_data 路径）；file_upload 保持现有 file_index 存储（仅 MIME-by-content 与 size），解析出的元数据仅出现在响应中（临时性，不落库）；无任何 schema 迁移、无新列"*

Resolution: **RESOLVED.** The plan now cleanly limits metadata persistence to `raw_data.meta_json` (no ALTER TABLE), and explicitly states file_upload keeps existing file_index schema unchanged. The Rollback section also reconfirms "无 schema 迁移". The contradiction is gone.

---

**P0-2 — Overlap with N32 in handlers.cpp understated**

Adopted text (Parallelism notes): *"新模块（astlite vs image_meta）完全并行；handlers.cpp 的集成步骤由父代理串行合并（AMD-3 共享热文件；P0-2 采纳）"*

Cross-check: N32 Parallelism notes states *"handlers.cpp 仅触碰 code ops 输出字段段——N33 触碰 files/raw_data/search_by_image 段、N34 触碰 jobs 段，三段结构不相交；handlers.cpp 的合并由父代理串行执行（AMD-3）"*. N34 Parallelism notes: *"handlers.cpp 仅 jobs 段——与 N32/N33 的触碰区不相交，父代理串行合并"*. All three plans now consistently state AMD-3 serial merge for handlers.cpp while calling module implementations parallel.

Resolution: **RESOLVED.** The parallelism claim is now accurately qualified with serial merge for the shared hot file, consistent across all three sibling node plans.

---

**P0-3 — Test count target (≥35) arbitrary and not derived from current baseline**

Adopted text (Tests): *"全套件为当前注册数（33）+ test_n33 注册项，精确计数以可执行输出为准记录于 n33-evidence（AMD-6；P0-3 采纳）"*. Acceptance #8: *"全套件双路径两轮全绿（= 33 + test_n33 注册数，精确值来自可执行输出并记录）"*

Note: The round-1 audit cited current baseline as 29. The revised plan states 33. N34's plan also uses 33 as the baseline. This could be a discrepancy if the true current test count from `test_main.cpp` is still 29 (as cited in round-1 and in the RESOLUTION document: *"tests/test_main.cpp currently registers 29 tests"*). However, 4 tests may have been added by N31 (which is listed as "done" in the N33 plan header). The RESOLUTION document was written before N31 was complete, and N31's completion could plausibly add test registrations. Critically, the plan now bases the target on the *actual executable output* rather than a prose-only number, which is exactly what AMD-6 demands. The 33 figure is used as the stated baseline but the acceptance assertion is correctly keyed to the runtime-reported count, not the prose number. This is the right structure.

Resolution: **RESOLVED** (the mechanism is correct; the prose number "33" is a best-estimate that is subordinated to the AMD-6 runtime recording requirement).

---

**P0-4 — `embed_image` provider contract leaks credentials in failure path**

Adopted text (D3): *"错误脱敏包装（P0-4 采纳）：embed_image 失败消息剥离 base_url 与一切凭据材料并截断至 200 字符；负 fixture：provider 返回 401+WWW-Authenticate（含 realm/token）→ 输出不含 URL/凭据"*. Acceptance #6: *"provider 失败注入（mock 端点拒绝）→ unavailable + 无 URL/密钥泄漏"*

Resolution: **RESOLVED.** The plan now specifies the redaction mechanism (strip base_url, truncate to 200 chars) and a concrete negative fixture for the 401+WWW-Authenticate case, making acceptance #6 falsifiable.

---

**P0-5 — MIME-by-content "magic number" implementation scope undefined**

Adopted text (D2): *"手工内容魔数检测（P0-5 采纳，零外部依赖）：读前 16 字节，PNG 签名 `PNG\r\n\x1a\n` → image/png；JPEG `\xFF\xD8\xFF` → image/jpeg；其余回退扩展名提示"* (Note: the plan text shows the raw bytes rather than escape sequences due to encoding, but the intent and byte patterns are clearly specified.)

Resolution: **RESOLVED.** Zero external dependencies, manual magic bytes specified for both supported formats, extension-based fallback for others.

---

**P1-1 — 32MiB file limit inconsistent with N30 authorization model**

Adopted text (D4): *"32MiB 上限执行点（P1-1 采纳）：file_upload 与 put_raw_data 的 handler 入口、磁盘写入之前检查；仅作用于图片元数据提取尝试的输入，非图片上传不受影响"*

Resolution: **RESOLVED.** Enforcement point is now specified (handler entry, before disk write) and scope is clarified (image metadata extraction only, non-image uploads unaffected).

---

**P1-2 — Mock provider mode uses insecure determinism**

Adopted text (D3): *"图像内容前 4KiB 哈希 → 64 位种子 → 确定性 RNG 生成 N 维向量（P1-2 采纳）"*. Acceptance #4: *"QBRAIN_EMBED_MOCK=1 下同图两次 search_by_image 向量 byte-identical；不同图向量不同"*

Resolution: **RESOLVED.** Content-hash seed with 4KB input vastly reduces collision probability vs. the file-size-mod approach, and the acceptance assertion is now falsifiable.

---

**P2-1 — Rollback claim "无 schema 迁移" incomplete**

Adopted text (Security notes): *"向后兼容（P2-1 采纳）：meta_json 消费者检查字段存在性，缺失按非图片处理"*

Resolution: **RESOLVED.** Graceful degradation for existing rows without metadata is now specified.

---

**P2-2 — `search_by_image` fail-open response shape inconsistent**

Adopted text (D4): *"无凭据→`{"results":[],"mode":"unavailable","reason":"no provider credentials"}` 且 exit 0（fail-open；P2-2 采纳）"*. Acceptance #5 also uses `mode=unavailable`.

Resolution: **RESOLVED.** Shape now aligns with the existing `{results: [...]}` envelope by including `results: []`.

---

**P2-3 — PNG/JPEG parser security surface needs explicit bounds**

Adopted text (Security notes): *"PNG chunk ≤1000、JPEG marker ≤100，未在限内找到 IHDR/SOF → format=unknown，P2-3 采纳"*

Resolution: **RESOLVED.** Concrete per-chunk and per-marker limits specified.

---

### New Issues Check

**New P0-candidate: D4 does not address what `get_raw_data` returns for metadata fields already in `raw_data.meta_json`**

D4 says metadata is stored in `raw_data`'s existing JSON field for `put_raw_data/get_raw_data` path. Acceptance #7 says "既有 files/raw_data 测试零修改通过（附加字段不破坏）". The plan specifies that put_raw_data writes metadata *into* `meta_json`, and `get_raw_data` reads it back. The scope of change to `get_raw_data` is implicit (it must now surface the metadata fields). The plan says "附加 JSON 字段" and "不破坏", which is consistent and falsifiable via acceptance #7. This is adequately handled as an additive change.

**New minor note: Acceptance #8 baseline "33" may be off if N31 added fewer than 4 tests**

As noted under P0-3, the prose figure "33" is a claimed baseline. If N31 is done but added e.g. 2 tests (baseline 29+2=31), the prose baseline is wrong by 2. However, because acceptance #8 explicitly ties the gate to the *runtime-recorded executable count*, this is a documentation accuracy issue, not a blocking defect. The mechanism (AMD-6 runtime count) is correct and overrides the prose.

**Parallelism cross-check: all three node plans (N32/N33/N34) are internally consistent**

- N32 owns: `codeintel/**`, `tests/test_n32*`, `fixtures/astlite`, handlers.cpp code ops segment
- N33 owns: `files/image_meta.*`, `ai/embed` image extension, `tests/test_n33*`, `fixtures/img`, handlers.cpp files/raw_data/search_by_image segment
- N34 owns: `storage/migrate.cpp` v13, `jobs/**`, `tests/test_n34*`, handlers.cpp jobs segment

No file overlap across the three plans' ownership declarations. All three call out AMD-3 serial merge for handlers.cpp. ✓

**`QBRAIN_EMBED_MOCK=1` interaction with N31 registry contract**

N31 introduced registry/MCP contract closure. The mock mode uses an env var (`QBRAIN_EMBED_MOCK=1`) that extends the existing text-embed mock pattern (`embed.cpp:18-28`). This is an additive extension to an existing mechanism, consistent with N31's contract (not a new operation, not a registry change). No conflict.

**Acceptance assertions falsifiability re-check**

All 8 assertions are now falsifiable:
1. Exact value match vs ground-truth fixture ✓
2. Content classification + flag ✓
3. No crash + bounded failure mode ✓
4. Byte-identical vectors, distinct images distinct ✓
5. Env-cleared → mode=unavailable + exit 0 + text search unaffected ✓
6. Mock endpoint reject → unavailable + no URL/key in output ✓
7. Existing tests pass unmodified ✓
8. Runtime-recorded count, dual-path, two rounds ✓

---

### VERDICT: PASS

All five P0 findings from round 1 are correctly and completely resolved. Both P1 findings are resolved. All three P2 findings are resolved. No new blocking issues were introduced. The schema/storage confusion (P0-1) is cleanly resolved by narrowing scope to raw_data only for persistence, with file_upload returning transient metadata in the response. The parallelism claim (P0-2) is now accurately stated and consistent across all three sibling node plans (N32/N33/N34), all invoking AMD-3 for handlers.cpp serial merge. The test count (P0-3) correctly subordinates the prose baseline to the AMD-6 runtime-recording requirement. The credential redaction (P0-4) specifies both the mechanism and a concrete negative fixture. MIME detection (P0-5) is now a portable, zero-dependency manual magic-byte implementation. The plan is now internally consistent, externally consistent with sibling node plans, and all acceptance assertions are falsifiable.

One non-blocking observation: the prose baseline "33" in the Tests section and Acceptance #8 may be slightly off if N31 added a different number of tests than assumed; but because the gate is the runtime-recorded count, this does not block approval.

**VERDICT: PASS**
