# GBrain 分析报告 & Qbrain 可行性评估

**日期**: 2026-07-25  
**上游**: https://github.com/garrytan/gbrain (v0.42.x, Bun/TypeScript)  
**目标**: 纯 Windows 11 原生软件，C++ 实现，**零 WSL2 / 零 Linux 虚拟机 / 零 Docker 依赖**

---

## 1. 上游 GBrain 是什么

GBrain 是 Garry Tan（YC CEO）开源的 **个人/团队知识大脑**：

| 能力 | 说明 |
|------|------|
| 知识页 (Pages) | Markdown 为中心，slug 唯一键（多 source 下为 `(source_id, slug)`） |
| 混合检索 | 向量 + 关键词 + RRF 融合 + 图信号 + 重排 + autocut |
| 知识图谱 | 写页时零 LLM 抽实体边（`works_at` / `invested_in` 等） |
| 合成层 `think` | 检索后 LLM 生成带引用答案 + **缺口分析 (gap analysis)** |
| 引擎 | PGLite（默认，WASM 嵌入式 Postgres）或 Postgres+pgvector |
| 契约 | `operations.ts` ~90–130 个共享操作，CLI 与 MCP 同源生成 |
| 后台 | Minions 任务队列、dream 夜间巩固、cron、ingestion |
| 集成 | MCP stdio/HTTP、OpenClaw/Hermes skills、admin SPA |

生产规模参考：~14.6 万页、2.4 万人、5k 公司、66 个 cron。

---

## 2. 架构要点（复刻时必须保留）

### 2.1 双轴模型

1. **Brain** = 哪一个数据库实例  
2. **Source** = 库内哪一个知识源/仓库  

查询路由必须同时尊重两轴，否则会静默串数据。

### 2.2 Contract-first 操作层

- 所有读写走统一 `Operation` 表（name / scope / handler）  
- CLI 与 MCP 只是适配器  
- `OperationContext.remote`：`false`=本地信任，`true`=远程 fail-closed  

### 2.3 检索管线（核心差异化）

```
query
  → embed(query)
  → keyword arm (FTS/BM25)
  → vector arm (cosine/HNSW)
  → RRF fusion (k=60)
  → post: backlink / salience / recency / title / graph-signals / alias-hop
  → optional rerank
  → autocut / adaptive-return
  → results
```

`think` = 上述检索 + LLM 合成 + 引用 + gaps。

### 2.4 数据模型（精简核心表）

- `pages` — slug, type, title, compiled_truth, frontmatter, source_id, dates  
- `content_chunks` — page_id, chunk_index, text, embedding BLOB  
- `links` — from_slug, to_slug, link_type, link_source  
- `tags`, `timeline`, `sources`, `config`, `jobs`（后期）  

---

## 3. 上游技术债 / 与 Windows 的冲突

| 上游依赖 | Windows 纯原生问题 | Qbrain 对策 |
|----------|-------------------|-------------|
| Bun runtime | 可用但非 C++ 目标 | 放弃 Bun，全 C++ |
| PGLite (WASM Postgres) | 依赖 Node/Bun | **SQLite + FTS5** 嵌入式 |
| Postgres + pgvector | 常靠 Docker/WSL | 可选后期；MVP 不需要 |
| bash scripts / CI Docker | 非原生 | PowerShell + MSVC |
| tree-sitter wasms | 可移植 | 后期可选；MVP 规则分块 |
| Express admin SPA | 可选 | Phase 3+ |

**结论：原样跑 GBrain 在“无 WSL 的纯 Win11”上勉强可用（Bun+PGLite），但用户要求 C++ 且彻底原生 → 必须重写。**

---

## 4. C++ 复刻可行性：**可行**

### 4.1 推荐技术栈（已定，按用户“一律按推荐”）

| 层 | 选择 | 理由 |
|----|------|------|
| 语言 | C++20 (MSVC 14.4x BuildTools) | 本机已装 VS BuildTools 18 |
| 构建 | CMake 3.24+ + Ninja 或 MSBuild | 标准 Windows 原生 |
| 主存储 | **SQLite 3 amalgamation** | 单文件、零服务、FTS5、WAL |
| JSON | nlohmann/json (header-only) | 已 vendored |
| HTTP 客户端 | WinHTTP | 系统自带，调 Embedding/LLM API |
| 向量 | `std::vector<float>` BLOB + 暴力/IVF 余弦 | MVP 够用到数万 chunk；后期 sqlite-vec |
| CLI | 自研 argv 解析 → `qbrain.exe` | 对标 `gbrain` 子命令 |
| MCP | Phase 2：stdio JSON-RPC | 对接 Claude Code / Cursor |
| 服务 | Windows Service 可选 + 前台 daemon | 对标 dream/minions |
| 配置 | `%LOCALAPPDATA%\Qbrain\config.json` | 纯 Windows 路径 |
| 数据 | `%LOCALAPPDATA%\Qbrain\brains\<id>\brain.db` | 多 brain 目录 |

### 4.2 范围裁剪（务实 MVP → 完整）

**Phase 0–1（当前冲刺）**  
- init / doctor / import / put / get / list  
- capture / search (hybrid FTS+vector)  
- extract links（正则 wikilink + markdown link）  
- graph neighbors  
- config + embed（OpenAI 兼容 HTTP）  
- think（可选 LLM，无 key 时 gather-only）  

**Phase 2**  
- MCP stdio  
- jobs 队列（minions 精简版）  
- dream 夜间巩固骨架  
- RRF 全后处理（recency/salience/backlink）  

**Phase 3**  
- HTTP MCP + 简易 admin  
- 多 source / multi-brain mount  
- 重排 / autocut  
- 完整 operations 对齐上游子集  

**明确不做（v1）**  
- 100% 操作 1:1 兼容上游 TypeScript API  
- PGLite/Postgres 引擎  
- OpenClaw skill 全量 300+  
- 浏览器 SPA admin（可后补）  

### 4.3 风险与缓解

| 风险 | 等级 | 缓解 |
|------|------|------|
| 上游体量极大（单文件 100KB–290KB TS） | 高 | 契约对齐 + 分模块渐进，不追求位级兼容 |
| 向量性能 | 中 | 分阶段：暴力 → 量化 → sqlite-vec/HNSW |
| LLM/Embedding 网络 | 中 | 可插拔 provider；离线 FTS 仍可用 |
| 中文分词 FTS | 中 | FTS5 unicode61 + 简单 n-gram 辅助 |
| 无现成 CMake | 低 | 本机装 CMake 或用 VS 生成器 |

---

## 5. 与上游的产品对齐（用户可见）

| 上游命令 | Qbrain |
|----------|--------|
| `gbrain init --pglite` | `qbrain init` |
| `gbrain doctor` | `qbrain doctor` |
| `gbrain import` | `qbrain import` |
| `gbrain capture` | `qbrain capture` |
| `gbrain search` | `qbrain search` |
| `gbrain think` | `qbrain think` |
| `gbrain serve` | `qbrain serve` (Phase 2) |
| `gbrain query` | `qbrain search` / `qbrain think` |

品牌名 **Qbrain** = Windows-native reimplementation，不 fork 上游许可证冲突代码；思想与数据模型对齐，实现独立（MIT 友好依赖）。

---

## 6. 可行性总结

| 问题 | 答案 |
|------|------|
| 能否纯 Win11、无 WSL2？ | **能** — SQLite + WinHTTP + 原生可执行文件 |
| 能否 C++？ | **能** — MSVC C++20 |
| 能否达到 GBrain 核心体验？ | **能（分阶段）** — 检索+图谱+合成是核心；dream/skills 后置 |
| 工期预期 | MVP 核心 1–2 周等价工作量（多代理并行加速） |

**推荐决策：立即按本文档进入开发准备与实现。**
