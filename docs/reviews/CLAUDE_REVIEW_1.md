# Claude Code / 架构审核报告 #1 — Qbrain v0.1

**日期**: 2026-07-25  
**审核对象**: `D:\Projects\Qbrain`  
**对照文档**: `docs/01-ANALYSIS.md`, `docs/02-DEVELOPMENT.md`  
**状态**: 首轮审核（自动 + 人工清单）

---

## 总评

设计方向正确：**纯 Win11 + C++20 + SQLite/FTS5 + WinHTTP**，不依赖 WSL/Docker/Postgres。模块划分与 GBrain 契约思想对齐，MVP 范围务实。

**当前阻塞**：本机 VS BuildTools 安装不完整（`isComplete=false`，缺 UCRT/`malloc.h`/`rc.exe`），导致无法在本机完成链接验证。代码与文档已就位，待 SDK 补齐后即可编译验证。

---

## 通过项

| # | 项 | 说明 |
|---|----|------|
| 1 | 无 WSL/Docker 硬依赖 | 存储 SQLite amalgamation，HTTP WinHTTP，路径 `%LOCALAPPDATA%` |
| 2 | C++ 技术栈 | CMake + MSVC C++20，模块静态库 |
| 3 | 多模块边界 | util / storage / core / search / graph / ingest / ai / ops / cli |
| 4 | Contract-first 雏形 | `ops::Registry` + scope + localOnly 预留 |
| 5 | 混合检索骨架 | FTS5 + 向量余弦 + RRF(k=60) |
| 6 | 图谱抽取 | wikilink + markdown 相对链接 |
| 7 | think 降级 | 无 API key 时 gather-only |
| 8 | Schema v1 | pages/chunks/links/tags/config/jobs + FTS 触发器 |
| 9 | 单元测试 | rrf/vector/chunker/extract/storage |
| 10 | 文档齐备 | 分析 + 开发 + 审核请求 |

---

## 问题列表

### P0（必须修）

1. **构建环境**：VS BuildTools 残缺，缺 Windows SDK / UCRT 头文件。  
   - 修复：完整安装 `Desktop development with C++` 或 `VCTools` workload + Windows 11 SDK。  
   - 脚本：`scripts/repair-vs-buildtools.ps1`（见下）。

2. **schema 路径硬编码**（`brain.cpp` 含 `D:/Projects/Qbrain/...`）  
   - 应改为：可执行文件旁相对路径 → 环境变量 `QBRAIN_SCHEMA` → 嵌入式 fallback（fallback 已有，硬编码应删除）。

3. **nlohmann JSON 编码**  
   - 通过 ghproxy 下载的 `json.hpp` 可能带 BOM/错误换行；构建前校验 `#include` 可解析。

### P1（应修）

4. **FTS MATCH 注入/语法**：用户 query 直接拼 FTS 表达式，特殊字符可导致查询失败（已有 LIKE fallback，但应规范化 tokenizer）。  

5. **向量搜索全表扫描**：MVP 可接受；文档应标明 10 万 chunk 以上需 sqlite-vec/HNSW。  

6. **config 明文 api_key**：文件落盘明文；后续用 DPAPI（`CryptProtectData`）。  

7. **think --save slug** 对中文 question 的 `slugify` 可能过短；加 hash 后缀。  

8. **缺少 `delete_page` CLI 接线**（ops 层可补）。  

9. **MCP / serve 未实现**（Phase 2，文档已标明）。  

### P2（改进）

10. 中文分词：FTS unicode61 对中文按字/词不理想 → 后期 n-gram。  
11. `jobs` 表已建但无 worker。  
12. 多 brain mount 未实现。  
13. 与上游 operations 命名尚未完全对齐全集。  

---

## 与开发文档一致性

| 文档承诺 | 实现 | 备注 |
|----------|------|------|
| init/doctor/import/search/think | 有 | CLI 已接线 |
| embed | 有 | 需 API key |
| MCP | 无 | Phase 2 OK |
| Windows Service | 无 | Phase 2 OK |
| 多模块 CMake | 有 | NMake/VS 生成器受 SDK 影响 |

---

## 修改建议（已/待执行）

1. 删除 `brain.cpp` 绝对路径候选（**待执行**）  
2. think save slug 加短 hash（**待执行**）  
3. 增加 `scripts/repair-vs-buildtools.ps1` 与 `docs/03-BUILD-WINDOWS.md`（**待执行**）  
4. 补齐 SDK 后跑 `scripts/smoke.ps1`  
5. 索引 codebase-memory  

---

## 结论

- **可行性确认：通过** — 纯 Win11 C++ 复刻 GBrain 核心 **可行**。  
- **MVP 代码：结构通过，编译验证受本机工具链阻塞**。  
- **下一动作**：修 P0 → 编译 → smoke → 二次审核。
