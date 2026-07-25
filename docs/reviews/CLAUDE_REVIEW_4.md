# 终审 — 通过/失败硬判 — Qbrain v0.1

**日期**: 2026-07-25  
**依据**: 全量源码阅读（include/ + src/ + schema/ + tests/ + CMakeLists.txt）+ REVIEW_3 运行时冒烟 + REVIEW_4 静态发现  
**方法**: 逐文件静态审核，对照 REVIEW_REQUEST.md 八项检查清单，所有前轮 P0/P1 逐条核实

---

## 最终裁定

```
PASS  (两项 P1 bug 需下一提交修复，不阻塞 MVP)
```

无遗留 P0。REVIEW_1/2 的 P0 全部修复并由 REVIEW_3 运行时确认。  
本轮新发现两项 P1 数据一致性 bug（N2、N3），修复量各约 5 行，不影响现有已通过的冒烟路径。

---

## 八项清单逐项裁定

### 1. 模块边界 ✅ PASS

CMakeLists 依赖链严格单向：

```
util → storage → core → {graph, search, ingest, ai} → ops → cli → main
```

无循环依赖。include/ 与 src/ 一一对应。`ops/registry.cpp` 是唯一汇聚点，`cli/commands.cpp` 是唯一 CLI 入口。`app.cpp` 为 TUI 预留，当前仅有占位符。

### 2. Linux/WSL 硬依赖 ✅ PASS

| 检查点 | 结论 |
|--------|------|
| 存储 | SQLite amalgamation，本地静态链接 |
| HTTP | WinHTTP (`winhttp.lib`)，`#ifdef _WIN32` 包裹，非 Windows 返回 stub error |
| 路径 | `SHGetKnownFolderPath(FOLDERID_LocalAppData)` + `MultiByteToWideChar/CP_UTF8` |
| 编译 | MSVC `/utf-8 /W3 /EHsc`，链接 `bcrypt.lib`，`if(WIN32)` 守卫所有平台库 |

零 Linux/WSL/Docker 依赖。

### 3. Schema 与迁移 ✅ PASS（含一项 P2 缺口）

- FTS5 触发器三件套（`pages_ai` / `pages_au` / `pages_ad`）语法正确，`'delete'` 命令符合 FTS5 content-table 协议。
- `content_chunks` 和 `tags` 有 `ON DELETE CASCADE`。
- `UNIQUE(source_id, slug)` 与 links 五列联合唯一约束，防重插安全。
- `brain.cpp::open_at()` 优先级：`QBRAIN_SCHEMA` env → 相对路径候选 → 内嵌 inline fallback。无绝对路径。
- `schema_version` 与 `sources('default')` 末尾 `INSERT OR IGNORE` 保证幂等。

**P2 缺口 N7**: 内嵌 fallback SQL（`brain.cpp:42-57`）省略了所有外键声明（`FOREIGN KEY … ON DELETE CASCADE`），与 `001_init.sql` 不同步。schema 文件缺失时初始化的数据库无 FK 约束，孤儿 chunk/link 不会被级联清理。修复：补全 FK 或打包 schema 文件到安装目录。

### 4. RRF / FTS / 向量路径 ✅ PASS（含一项 P1 保留）

**FTS 注入防护** — `fts_quote()` 将每个 token 转小写（消除 FTS5 保留词 AND/OR/NOT）、去除双引号、以 `"token"` 包裹后拼接。已修，无注入风险。

**LIKE fallback** — `bind_text` 绑定参数，无 SQL 注入；但用户 query 中的 `%`/`_` 会被 LIKE 当通配符处理（P2 N4，语义错误而非安全漏洞）。修复：在 query 拼入 LIKE 模板前对 `%`/`_`/`\` 转义。

**RRF 公式** — `score += 1.0 / (k + rank + 1)`，k=60 默认，按 slug 去重后取最大 arm rank。算法正确。

**向量路径** — `cosine_similarity()` 有零向量守卫，`pack_f32`/`unpack_f32` 用 `memcpy`，字节序一致。`vector_search()` 按 slug 取最高相似度 chunk 去重后截断到 limit。

**hybrid_search()** — `fts_search(limit*3)` + 可选 `vector_search(limit*3)` → `rrf_fusion()` → 截断 limit。过采样倍数合理。

**P1 保留 N1**: `vector_search` 加载所有已嵌入 chunk blob 到内存后排序，无预筛 LIMIT。对 embedded_chunks > 5 万时会有明显内存与延迟压力。`doctor` 输出中需加对应警告，或添加可配置的预筛上界（建议 ≤ 50000）。

### 5. 安全：路径、密钥日志、远程 fail-closed ✅ PASS（含一项 P1 保留）

| 项 | 结论 |
|----|------|
| API key 日志 | `save_file_config()` 注释明确不落盘 api_key；`log.cpp` 纯字符串输出，无 key 插值；`http_client.cpp` 设置 Bearer header 但从不调用 log() |
| `resolve_api_key()` | 内存 Config → `OPENAI_API_KEY` env → `QBRAIN_API_KEY` env，优先级明确 |
| 路径遍历 | `import_path()` 只处理 .md/.markdown/.txt；slug 基于 `fs::relative()`，不构造任意路径 |
| 远程 fail-closed | `Registry::call()` 中 `op->local_only && ctx.remote → exit_code=1`，正确拒绝 |
| WinHTTP handle 泄漏 | 所有提前返回路径均关闭 session/conn/req，无泄漏 |

**P1 保留**: `config` 表中 `embedding.api_key` / `chat.api_key` 明文存于 SQLite。`%LOCALAPPDATA%` 默认仅当前用户可读，风险已缓解。后续 Phase 2 应用 DPAPI `CryptProtectData` 加密。

### 6. UTF-8 / 中文路径 ✅ PASS

`paths.cpp` 全程 `CP_UTF8 ↔ wstring` 转换。`/utf-8` 编译标志确保源码与执行字符集均为 UTF-8。`UNICODE _UNICODE` 宏确保 Win32 API 走 W 后缀变体。`slug_from_path` 保留 UTF-8 多字节字符原样（≥ 0x80 字节不做 ASCII 替换）。中文文件名与 slug 均可正常处理。

FTS5 `tokenize='unicode61'` 对中文逐字符索引，MVP 可用（P2 升级为 n-gram）。

### 7. 与开发文档一致性 ✅ PASS（含一项 P1 bug）

| 文档承诺 | 实现状态 |
|----------|----------|
| init / doctor / import / capture / put / search / graph / embed / think | CLI 已接线，REVIEW_3 实测 OK |
| `--no-vector` flag | 存在 |
| `qbrain config set/get` | 存在 |
| MCP / serve / Windows Service / 多 brain | Phase 2，文档已标明 ✓ |

**P1 bug N3**: `think --save` 分支保存合成结果时调用了 `put_page` + `replace_chunks`，但缺少 `replace_extracted_links`。合成文本中的 `[[wikilink]]` 不被索引到图谱，与 `put` 命令的行为不一致。修复：在 save 分支补两行（extract_links + replace_extracted_links）。

**P1 保留 R1-P1-8**: `soft_delete()` 已在 `Brain` 层实现，但 CLI 未暴露 `qbrain delete <slug>` 命令。

### 8. 关键测试覆盖 ✅ PASS（含 P2 缺口）

| 测试套件 | 覆盖内容 | 状态 |
|----------|----------|------|
| test_rrf | RRF fusion, dedup, 排序 | PASS |
| test_vector | cosine_similarity, pack/unpack | PASS |
| test_chunker | 分块数量、非空 | PASS |
| test_extract | wikilink + markdown link, HTTP URL 跳过 | PASS |
| test_storage | Brain open/put/get/stats/close | PASS |

REVIEW_3 运行时：5/5 单元测试 PASS，smoke.ps1 全通过。

**P2 缺口**:
- 无 `hybrid_search` 集成测试（FTS+vector 融合路径）
- 无 `put_page` upsert 幂等性测试
- 无 LIKE fallback 路径测试（需触发 FTS 异常）
- 无 `replace_extracted_links` 事务回滚测试

---

## 新发现问题汇总（本轮）

### N2 — P1：`replace_extracted_links` 缺事务

**位置**: `src/qbrain/core/brain.cpp:352-373`

DELETE 之后、INSERT 循环结束之前进程崩溃，该页将失去所有抽取链接。`replace_chunks`（同文件第 254 行）已正确使用 BEGIN/COMMIT；本函数遗漏。

**修复**（~5 行，参考 `replace_chunks`）:
```cpp
void Brain::replace_extracted_links(...) {
  db_.exec("BEGIN;");
  try {
    { auto d = ...; d.step_done(); }
    for (const auto& l : links) add_link(l);
    db_.exec("COMMIT;");
  } catch (...) {
    try { db_.exec("ROLLBACK;"); } catch (...) {}
    throw;
  }
}
```

### N3 — P1：`think --save` 缺 `replace_extracted_links`

**位置**: `src/qbrain/ops/handlers.cpp`（think op 的 save 分支）

`put` 命令保存页面时完整执行 put_page + replace_chunks + replace_extracted_links；think save 分支只做了前两步。合成文本中的 `[[wikilink]]` 不会出现在图谱查询中。

**修复**（~2 行，在 save 分支 replace_chunks 之后追加）:
```cpp
auto links = graph::extract_links(page.source_id, page.slug, page.body);
ctx.brain->replace_extracted_links(page.source_id, page.slug, links);
```

### N4 — P2：LIKE fallback 不转义 `%`/`_`

**位置**: `src/qbrain/search/hybrid.cpp`（LIKE 拼接处）

LIKE 路径通过 `bind_text` 绑定，无 SQL 注入，但用户 query 中 `%` 或 `_` 会被 LIKE 当通配符，导致搜索语义错误。修复：对 query 先做 `replace_all(%→\%)` / `replace_all(_→\_)` / `replace_all(\→\\)` 再拼入 `like` 字符串（SQL 已有 `ESCAPE '\'`）。

### N1 — P1*：向量搜索全表扫描无上界

**位置**: `src/qbrain/search/hybrid.cpp:97-101`

无 LIMIT 子句，全量 embedding blob 读入内存。`doctor` 需在 embedded_chunks > 50000 时加提示；或在 `HybridOpts` 中增加 `max_vector_scan`（默认 50000）并在 SQL 加 `LIMIT ?`。

---

## 全部问题优先级汇总

| 编号 | 优先级 | 描述 | 修复量 |
|------|--------|------|--------|
| N2 | **P1** | `replace_extracted_links` 加事务 | ~5 行 |
| N3 | **P1** | `think --save` 补 `replace_extracted_links` | ~2 行 |
| N1 | **P1\*** | 向量全扫描加文档警告或可配置上界 | 小 |
| R1-P1-8 | **P1** | `qbrain delete <slug>` CLI 接线 | 小 |
| R1-P1-6 | P1\* | DPAPI API key 加密（Phase 2） | 中 |
| N4 | P2 | LIKE fallback 转义 `%` `_` | 小 |
| N5 | P2 | frontmatter `\n---\n` 精确匹配 | 小 |
| N7 | P2 | 内嵌 fallback schema 补 FK | 中 |
| N8 | P2 | v2 索引改为 `(id) WHERE embedding IS NULL` | 小 |
| N6 | P2 | WinHTTP session 复用（Phase 2） | 中 |

\* = MVP 范围内可接受，需文档说明或配置上界

---

## 结论

**Qbrain v0.1 终审：PASS**

纯 Win11 C++20 + SQLite + WinHTTP 复刻 GBrain 核心能力的目标已实现并经运行时验证。

两项 P1 数据一致性 bug（N2 事务、N3 think 图谱缺失）需在下一提交中修复，修复量合计约 7 行。其余问题为 P2 改进项，不阻塞 MVP 内部使用。

**立即行动（按优先级）**:
1. 修 N2 — `replace_extracted_links` 加 BEGIN/COMMIT/ROLLBACK
2. 修 N3 — `think --save` 补 `replace_extracted_links`
3. 接线 `qbrain delete <slug>` CLI 命令
4. `doctor` 输出在 embedded_chunks > 50000 时加向量扫描警告
5. Phase 2: DPAPI 密钥加密、MCP、WinHTTP session pool
