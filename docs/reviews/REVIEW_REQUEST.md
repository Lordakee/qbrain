# Claude Code 审核请求 — Qbrain v0.1

## 审核目标

请对照 `docs/01-ANALYSIS.md` 与 `docs/02-DEVELOPMENT.md`，审核本仓库 **Windows 11 原生 C++ 知识大脑** 的设计与实现。

## 硬性约束（用户）

1. **纯 Windows 11 软件**，不得依赖 WSL2 / Linux 虚拟机 / 强制 Docker  
2. **C++ 开发**（当前 MSVC C++20 + SQLite + WinHTTP）  
3. 复刻 GBrain 的核心能力：页面、混合检索、图谱、think 合成  

## 请检查

- [ ] 模块边界是否清晰（util/storage/core/search/graph/ingest/ai/ops/cli）  
- [ ] 是否存在任何 Linux/WSL 硬依赖  
- [ ] Schema 与迁移是否合理  
- [ ] RRF / FTS / 向量路径是否正确  
- [ ] 安全：路径、密钥日志、remote fail-closed 预留  
- [ ] UTF-8 / 中文路径  
- [ ] 与开发文档不一致之处  
- [ ] 缺失的关键测试  

## 输出

请写入 `docs/reviews/CLAUDE_REVIEW_1.md`，包含：通过项、问题列表（P0/P1/P2）、修改建议。

## 代码入口

- `CMakeLists.txt`
- `include/qbrain/**`
- `src/qbrain/**`
- `schema/001_init.sql`
- `docs/02-DEVELOPMENT.md`
