# 二次审核（文档+代码静态）— Qbrain v0.1.1

**前提**: 本机尚无法产出 `qbrain.exe`（VS BuildTools incomplete）。本轮为**静态代码/文档审核**，非运行时冒烟。

## 相对 REVIEW_1 的修复

| P0/P1 | 状态 |
|-------|------|
| 绝对路径 schema | **已修** → `QBRAIN_SCHEMA` + 相对路径 + 内嵌 fallback |
| think save slug | **已修** → 日期 + sha256 前 8 位 |
| 构建文档 | **已补** `docs/03-BUILD-WINDOWS.md` |
| codebase-memory 索引 | **已完成** project=`Qbrain` (3465 nodes) |

## 仍待

| 项 | 级别 |
|----|------|
| 安装完整 Windows SDK + UCRT，编译通过 | P0 环境 |
| smoke.ps1 实测 | P0 验证 |
| FTS query 规范化 | P1 |
| DPAPI 密钥 | P1 |
| MCP Phase 2 | 计划内 |

## 模块完整性（静态）

- [x] CMakeLists 定义全部库与测试目标  
- [x] include/src 成对  
- [x] schema SQL + 内嵌 fallback  
- [x] CLI 覆盖 init…embed  
- [x] 单元测试 5 套  
- [x] 无 WSL/Docker 引用  
- [x] sample_notes 示例语料  

## 结论

**开发文档与实现一致**。在工具链修复后应立即：

```text
cmake 配置 → 编译 → ctest → smoke.ps1 → REVIEW_3 运行时审核
```

产品决策不变：**纯 Win11 C++ 复刻可行且为正确路径**。
