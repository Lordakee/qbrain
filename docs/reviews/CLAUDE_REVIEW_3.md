# 运行时审核 #3 — 编译与冒烟通过

**日期**: 2026-07-25  
**产物**: `D:\Projects\Qbrain\build\cl\qbrain.exe` (约 2.1 MB)

## 环境修复

- 补齐 Windows 11 SDK 10.0.26100（UCRT 头/库、um 库）
- MSVC 14.51 + `vcvarsall x64` 直接 `cl`/`link` 构建（`/MANIFEST:NO`）

## 验证结果

| 项 | 结果 |
|----|------|
| `qbrain version` | OK |
| `qbrain init` | OK → `%LOCALAPPDATA%\Qbrain\brains\default\brain.db` |
| `qbrain doctor` | OK schema v1 |
| `capture` / `put` / `import` | OK |
| `search ... --no-vector` | OK（命中 people/alice） |
| `graph people/alice` | OK（双向 related） |
| 单元测试 5/5 | PASS |

## 结论

**纯 Win11 C++ MVP 可运行。** 无 WSL/Docker。后续：MCP、embed 联调、中文 FTS 增强。
