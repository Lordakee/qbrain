# Qbrain 开发文档（Windows 11 原生 / C++）

**版本**: 0.1.1-dev  
**状态**: 开发中（代码已落地；本机完整编译依赖完整 VS SDK）  
**审核**: 首轮见 `docs/reviews/CLAUDE_REVIEW_1.md`；P0 路径硬编码 / think slug 已修  

---

## 1. 产品定义

**Qbrain** 是运行在 **纯 Windows 11** 上的个人知识大脑：

- 本地嵌入式数据库（SQLite），无需 Postgres / Docker / WSL  
- 混合检索（FTS5 + 向量 + RRF）  
- 自接线知识图（链接抽取）  
- LLM 合成问答（`think`）+ 缺口提示  
- CLI 优先，后续 MCP  

数据与配置默认：

```
%LOCALAPPDATA%\Qbrain\
  config.json
  brains\<id>\brain.db
  audit\
```

环境变量：

| 变量 | 含义 |
|------|------|
| `QBRAIN_SCHEMA` | `001_init.sql` 路径覆盖 |
| `OPENAI_API_KEY` / `QBRAIN_API_KEY` | Embedding/Chat |
| `LOCALAPPDATA` | 数据根（系统自带） |

---

## 2. 模块地图

```
qbrain/
├── util/          路径、字符串、时间、日志、SHA256 (BCrypt)
├── storage/       SQLite 引擎、schema 迁移
├── core/          Page/Chunk/Link + Brain 门面
├── graph/         链接抽取、邻接遍历
├── search/        FTS、向量、RRF、hybrid
├── ingest/        markdown 导入、capture、分块
├── ai/            Embed + Chat (WinHTTP, OpenAI-compatible)
├── ops/           Operation 注册表（contract-first）
├── mcp/           Phase 2
├── service/       Phase 2
└── cli/           命令行入口
```

CMake 静态库：`qbrain_util` … `qbrain_cli` → 可执行文件 `qbrain`。

---

## 3. 构建

见 [`03-BUILD-WINDOWS.md`](03-BUILD-WINDOWS.md)。

要求：MSVC + **完整 Windows SDK/UCRT**（`malloc.h` 等）。  
备选：MinGW-w64（需自行安装 `g++`）。

---

## 4. Schema v1

`schema/001_init.sql`：

- pages, content_chunks, links, tags, sources, config, jobs  
- pages_fts (FTS5) + AI/AD/AU 触发器  
- schema_version  

无外部 SQL 文件时，`Brain::open_at` 使用内嵌 fallback DDL。

---

## 5. 核心算法

### 5.1 分块

目标 ~600 字符，overlap 80，按空行段落。

### 5.2 向量

OpenAI-compatible `/embeddings`，`float32` BLOB，余弦相似度。

### 5.3 RRF

`score = Σ 1/(k+rank)`，默认 k=60。

### 5.4 链接

`[[wikilink]]`、`[t](rel.md)`（跳过 http/mailto）。

### 5.5 think

hybrid → evidence → chat → `## Gaps`；无 key 则 gather-only。

---

## 6. CLI

```
qbrain init|doctor|config|put|get|list|capture|import|search|think|graph|embed|version
```

退出码：0 成功，1 业务/用法，2 环境/IO。

---

## 7. Operations（子集）

get_health, get_stats, put_page, get_page, list_pages, search, think, capture, get_links  

---

## 8. 里程碑

| M | 状态 |
|---|------|
| M0 文档+骨架 | **完成** |
| M1 FTS 读写导入 | **代码完成**（待编译验证） |
| M2 hybrid+graph | **代码完成** |
| M3 think+doctor | **代码完成** |
| M4 MCP stdio | **完成**（`qbrain serve`，见 docs/06-MCP-USAGE.md） |
| M5 jobs/dream | 未开始 |

---

## 9. 多代理分工（后续并行）

| 节点 | 模块 |
|------|------|
| A | storage 迁移 v2、软删 purge |
| B | search 中文 n-gram、sqlite-vec |
| C | MCP stdio JSON-RPC |
| D | dream/jobs worker |
| E | DPAPI 密钥 |
| F | 测试与 smoke 自动化 |
| G | 文档/评审 |

---

## 10. 审核清单

见 `docs/reviews/`。二次审核在成功 `qbrain.exe` 冒烟后进行。
