# Claude Code 硬审核任务

你是严格的架构审核官。请对比：

1. **上游参考**：`D:\Projects\Qbrain\gbrain-upstream\`（garrytan/gbrain 核心源码镜像）
2. **被审项目**：`D:\Projects\Qbrain\` 中除 `gbrain-upstream` 外的 Qbrain C++ 实现
3. **开发文档**：`docs/01-ANALYSIS.md`、`docs/02-DEVELOPMENT.md`
4. **可执行文件**：`build\cl\qbrain.exe`（应可运行）

## 硬性产品约束（任一违反 = FAIL）

- 纯 Windows 11 原生，无 WSL2 / 强制 Docker / 虚拟 Linux 依赖
- 使用 C++ 开发（非 Bun/TS 运行时）
- 具备 GBrain 核心能力子集：页面 CRUD、混合检索、图谱链接、think 合成路径

## 审核方法

1. 阅读 gbrain-upstream 的 README/CLAUDE/operations/engine/hybrid/think 关键
2. 阅读 Qbrain include + src 全部模块
3. 对照开发文档一致性
4. 如可能，运行 `build\cl\qbrain.exe doctor` / search 做运行时抽查

## 输出格式（必须）

写入文件：`D:\Projects\Qbrain\docs\reviews\CLAUDE_HARD_AUDIT.md`

```markdown
# VERDICT: PASS | FAIL

## Summary
...

## Hard Requirements
| ID | Requirement | Status | Evidence |
|----|-------------|--------|----------|

## Findings
### P0 (blockers)
- ...

### P1
- ...

### P2
- ...

## Required Fixes (ordered checklist)
1. ...

## Pass Criteria for next review
- [ ] all P0 fixed
- [ ] ...
```

若 FAIL：列出可执行的修改清单（文件路径级）。若 PASS：说明仍存在的已知限制（非 blocker）。
