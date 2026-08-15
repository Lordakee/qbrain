# N32 Plan — AST 代码智能（C++/TS 有界解析器）

**Status**: approved (round-2 plan audit PASS; all findings adopted)
**Depends on**: N31 done（registry 权威化）；docs/08 v2.0.0 §4 N32；docs/RESOLUTION-2026-08-15.md（N32 定义含可证伪验收）
**Plan audit**: PASS round 2 (`N32-PLAN-AUDIT.md`)
**Outcome audit**: pending (`N32-HARD-AUDIT.md`)

## Goal

以**自研有界解析器**（不引入 tree-sitter 外部依赖，Windows 原生零新依赖）替代 code_def/code_refs/code_callers/code_callees/code_flow/code_blast 的纯 regex 启发式：对 C++ 与 TypeScript 两种语言提供"括号平衡+作用域感知"的轻量结构分析（定义识别=声明语法模式+上下文、调用识别=调用点+被调名解析到同文件/工作区内定义、流/爆炸=现有遍历换用新索引源）；regex 路径保留为显式 fallback 并在输出中标注 `mode: "heuristic"|"structured"`。所有上游 parity 声明继续限定为 source-scoped 有界子集（不做完整编译器级类型解析）。

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| code_def / code_refs / code_callers / code_callees / code_flow / code_blast | N32: structured 模式（C++/TS）+ 显式 heuristic fallback 标注 |

## Deliverables

1. **D1 解析器核心**: `src/qbrain/codeintel/astlite.cpp` + `include/qbrain/codeintel/astlite.hpp` — **API（P1-2 采纳）：`parse_content(std::string_view body, Language lang) -> SymbolTable`，内容入参、无文件系统访问、无路径参数**；Language ∈ {Cpp, TypeScript}。有界解析：行/块扫描、字符串/注释/字符字面量跳过、括号深度跟踪、C++（namespace/class/struct/function 声明模式）与 TS（function/class/const arrow/export 声明模式）定义提取、调用点提取（identifier 后随 `(` 且非关键字/声明位）、单文件符号表。硬上限：文件 ≤2MiB、嵌套深度 ≤64、单文件符号 ≤10000、总时间预算 ≤50ms/内容块（P2-1 采纳：`std::chrono::steady_clock` 每 1000 行采样一次，超限即返回 partial 结果 + `degraded_reason: "timeout"` 并降级 heuristic）。
2. **D2 扫描器集成**: `src/qbrain/codeintel/scan.cpp` — 新增结构化路径：语言判定（.cpp/.hpp/.cc/.h vs .ts/.tsx）；ops 六个的 handler 输出增加 `mode` 字段（handlers.cpp 仅追加字段，不改契约）。**D2b 构建接线（P1-3 采纳）**：`astlite.cpp` 加入 CMakeLists.txt 生产目标与 scripts/build-cl.ps1；`tests/test_n32.cpp` 加入 CMake 测试目标与 scripts/build-tests-cl.ps1；集成前先双路径干净构建通过（生产+测试二进制均链接成功）。
3. **D3 fixtures**: `tests/fixtures/astlite/` — C++ 与 TS 各 ≥3 个金标准文件（含边界：嵌套类、模板、overload、注释陷阱、字符串含代码、深度嵌套超限、**timeout 触发 fixture（合成耗时结构 >50ms → 降级 timeout 原因；round-2 P2 残留采纳）**）+ 预期 JSON。
4. **D4 测试**: `tests/test_n32.cpp` — 金标准对照（每符号 def/ref/call 精确匹配）、畸形/超限降级、确定性（两次运行 byte-identical）、路径隔离（仅授权工作区根）、fallback 标注正确、既有 6 op 回归不破坏（现测试继续通过）。
5. **D5 证据**: n32-evidence/（PRE-GATE；双路径两轮全绿；fixtures 运行输出）。

## Tests

- test_n32.cpp（注册入 test_main + 双构建源列表）；全套件 ≥34 双路径两轮全绿。
- 确定性：同一 fixture 两轮解析输出 byte-identical。

## Acceptance assertions (falsifiable)

1. 金标准 fixture 全符号匹配：structured 模式下 def/ref/caller/callee 集合与预期 JSON 精确相等（无遗漏无多余）。
2. 六个 code ops 的输出均含 `mode` 字段；structured 适用且成功时为 `structured`，语言不支持/超限/解析失败时为 `heuristic`。
3. 超限 fixture（>64 嵌套、>2MiB、>10000 符号构造）→ 受控降级 heuristic + 输出含降级原因，不崩溃不挂起。
4. 畸形输入（截断、二进制垃圾、无效 UTF-8）→ 有界诊断或 heuristic，进程存活。
5. 两次运行输出 byte-identical（确定性）。
6. 路径隔离由 scan.cpp/handlers.cpp 层强制（source 根检查先于页内容获取；越界 source → 拒绝 fixture 在集成层测试）；astlite 单元层仅验证内容串解析正确（P1-2 采纳后的 AA6 语义）。
7. 既有 code 相关测试（test_codeintel.cpp、test_n16/22 段）零修改全部通过。
8. 全套件双路径两轮全绿（≥34 注册）。

## Rollback

- D2 集成点为单一开关（语言判定失败即走旧 regex 路径）；astlite 为新增文件，整体可移除。
- ops 输出新增 `mode` 字段不破坏既有消费者（附加字段）。

## Security notes

- 源码视为不可信输入：无执行、无路径外访问、一切上限强制（尺寸/深度/时间/符号数）；**astlite 路径完全不使用正则；regex fallback 受 N22 既有 16 KiB/page 上限约束，输入有界**（P2-2 采纳）。
- 不在输出泄露绝对路径（延续 source-scoped 相对输出）。

## Parallelism notes

- 与 N33/N34 并行（文件所有权：codeintel/** + tests/test_n32* + fixtures 独占；handlers.cpp 仅触碰 code ops 输出字段段——**N33 触碰 files/raw_data/search_by_image 段、N34 触碰 jobs 段，三段结构不相交**（P1-1 采纳）；handlers.cpp 的合并由父代理串行执行（AMD-3 共享热文件），各子代理仅拥有其声明段）。
- 子代理切片（批准后）：A=astlite 核心+fixtures；B=scan/handlers 集成+test_n32；父代理合并。
