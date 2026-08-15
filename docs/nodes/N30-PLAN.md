# N30 Plan — 基线调和与安全/构建闭合（Phase 2 首节点）

**Status**: done
**Depends on**: docs/08-MASTER-PLAN-GBRAIN-PARITY.md v2.0.0；docs/RESOLUTION-2026-08-15.md（三方决议，AMD-1/2/4/6/7/8/9/10 直接约束本节点）
**Plan audit**: PASS (`N30-PLAN-AUDIT.md`, Claude Code 2026-08-15；0 P0 / 3 P1 / 3 P2)
**Outcome audit**: PASS (`N30-HARD-AUDIT.md`, Claude Code 2026-08-15, 0 P0/0 P1/0 P2)

## Audit disposition (controller decisions on P1/P2)

- P1-1 采纳：D3 增加强制执行算法与负矩阵 op 枚举。
- P1-2 采纳：D2/验收2 定义"全标准审计"可观察结构与 deferral 记录字段。
- P1-3 采纳：PRE-GATE.json 移至独立"实施前门"节，明确时序（approved 之后、任何 D1-D11 编辑之前）。
- P2-1 采纳：D7 增加 MSVC 发现次序（vswhere.exe → 已知 BuildTools 路径 → 明确报错）。
- P2-2 维持原案：B/C 共享文件按函数块所有权 + 父代理串行合并（计划原文已有）。
- P2-3 采纳：D1 明确 rejected 文件处置（保留于工作树不提交，理由入清单，不入任何 N30 提交）。

## 实施前门（Pre-implementation Gate，串行，先于一切实现编辑）

在 plan-audit PASS 且状态置 approved 之后、任何 D1-D11 实现编辑之前，捕获 `docs/nodes/n30-evidence/PRE-GATE.json` 建立冻结基线与 N30 纠正工作之间的时间边界：
- 本计划与审计文件的 sha256；
- 范围文件清单（D1-D11 涉及的全部路径 + 当前哈希/缺席标记）；
- 交付物缺席证明（n30-evidence/ 与 tests/test_n30.cpp 尚不存在）；
- 构建身份（双路径干净构建的命令、时间戳、产出二进制 sha256）；
- 隔离数据根证明（测试根位于系统临时目录，绝不触碰 `%LOCALAPPDATA%\Qbrain`）。

## Goal

建立唯一可信基线：冻结并处置未提交变更集（93 修改 + 94 未跟踪），调和 N1-N29 节点门矛盾（含 N20/N21/N23 显式处置、N24-N28 stub 审计纠正），闭合决议确认的全部 P0 安全缺陷与 P1 构建/存储完整性缺陷，使 CMake 与直连 MSVC 两条构建路径干净可复现。本节点是 Phase 2 一切后续节点（N31-N37）的阻塞前置。

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| （无新增 op） | 本节点为安全/治理/构建闭合；不新增台账行。既有 104 行在治理调和后其 notes 与证据状态对齐（Tier 标注）。 |

## Deliverables

1. **D1 变更集处置**: `docs/nodes/n30-evidence/CHANGESET-MANIFEST.json`（每文件 path/size/sha256/分类/处置）；密钥扫描记录；处置分类：accepted-product / accepted-test-evidence / doc-correction / duplicate-stale / rejected（不删除用户工作；**rejected 文件保留于工作树、不进入任何 N30 提交**，拒绝理由入清单）。
2. **D2 节点调和矩阵**: `docs/nodes/n30-evidence/NODE-RECONCILIATION-MATRIX.json` — N1-N29 每节点：当前 plan 哈希、状态、plan-audit 哈希+结论、outcome-audit 哈希+结论、Tier 定级（1/2/3）与处置（accept / corrective-closure / supersede / defer + 理由）。N20/N21/N23 必须各有控制器处置记录（AMD-1）；N24-N28 各获全新全标准审计或显式 deferral（AMD-7）。**"全标准审计"可观察结构（P1-2 采纳）**: ≥50 行；显式断言表（计划交付项 ↔ 证据逐行对照）；findings 节（可为空）；结论引用具体测试输出哈希；证据文件引用。**deferral 记录字段**: 理由（如"回溯基线，不足以重审"）、受影响 ledger 行、未来闭合 owner。
3. **D3 中央授权策略**: `src/qbrain/ops/registry.cpp`（及 `include/qbrain/ops/registry.hpp`）— scope（Read/Write/Admin）由中央策略点强制执行。**执行算法（P1-1 采纳）**: `registry.cpp::call` 检查 `op->scope`；若 scope ∈ {Write, Admin} 且 `ctx.remote == true`，除非 `ctx.authenticated_capability` 显式许可该操作，否则拒绝；`--allow-write` 仅作为本地能力（远程永不出示）。负矩阵覆盖全部 Write/Admin 注册 op（至少：put_page、delete_page、restore_page、purge_deleted_pages、file_upload、put_raw_data、submit_agent、doctor_remediate、schema_apply_mutations、takes_calibration、chronicle_backfill、log_ingest、add_timeline_entry、reload_schema_pack）。
4. **D4 路径披露闭合**: `get_health`/`file_list`/`file_url`（`src/qbrain/ops/handlers.cpp`）对远程调用者脱敏（不返回本地绝对路径/db 路径/`file:///` URL）。
5. **D5 HTTP 精确路由**: `src/qbrain/mcp/http_server.cpp` — 精确请求行解析（method+path 精确匹配，替代 `req.find("POST /ingest")` 子串判定）；负测试覆盖 `/ingestx`、畸形方法、重复头、非法 Content-Length、不支持路径、超大请求体拒绝。
6. **D6 CLI 参数安全**: `src/qbrain/cli/commands.cpp` — `--port` 等数值选项解析与范围检查，无未捕获异常。
7. **D7 构建对齐**: `CMakeLists.txt` 纳入 `src/qbrain/files/store.cpp`；`scripts/build-cl.ps1`/`build-tests-cl.ps1` 路径由 `$PSScriptRoot` 推导、**MSVC 发现次序（P2-1 采纳）：vswhere.exe 优先 → 已知 BuildTools 路径回退 → 未找到则明确报错**、系统临时目录、尊重 `-TestSources`、仅链接本次生成的对象（无陈旧对象）。
8. **D8 schema 健康闭合**: `src/qbrain/storage/migrate.cpp` — 覆盖当前 schema v12 全部必需表/索引/列/FTS 对象（含 page_versions、facts、takes、file_index、raw_data 等）；损坏 fixture（逐一移除每个必需对象）验证 doctor 闭环失败。
9. **D9 原子 pack 写入**: `src/qbrain/schema/packs.cpp` — 同目录有界临时文件写全→flush→close→原子替换；一切失败路径保留原文件（失败后目标哈希不变）。
10. **D10 文档调和**: `docs/09-PROJECT-COMPLETION.md`、`docs/03-BUILD-WINDOWS.md`、`docs/OPS-PARITY-LEDGER.md` 从调和矩阵单一来源同步；全部测试计数改为生成值（AMD-6）。
11. **D11 测试**: 新增/扩展 `tests/test_n30.cpp`（授权负矩阵、HTTP 负路径、CLI 参数、schema 损坏 fixture、pack 原子性、脱敏）；接入 `tests/test_main.cpp`、`CMakeLists.txt`、`scripts/build-tests-cl.ps1`。
12. **D12 证据**: `docs/nodes/n30-evidence/`（实施前门见上方"实施前门"节；本交付物为实施期证据：两轮全绿测试输出、双路径干净构建输出、验收断言逐条对照记录）。

## Tests

- 全套注册套件（当前基线 29 → 本节点后 ≥30）两轮通过（隔离临时数据根）。
- MCP 授权负矩阵（每个 Write/Admin op：远程无授权 → 拒绝）。
- HTTP 负路径套件（`/ingestx`、畸形方法、重复头、非法长度、不支持路径、超大 body）。
- CLI 参数测试（`--port=abc`、越界 → 受控错误、exit code 非 0 但无崩溃）。
- schema 损坏 fixture：逐对象移除 → doctor 报 FAIL（闭环）。
- pack 原子性：注入写失败 → 目标文件哈希不变。
- 脱敏测试：远程 get_health/file_list/file_url 响应无本地绝对路径模式。
- 干净构建：删除 build 输出目录后 CMake 与直连 MSVC 双路径全绿。

## Acceptance assertions (falsifiable)

1. `CHANGESET-MANIFEST.json` 覆盖 `git status` 全部 93+94（及实施期新增）文件，每条含 sha256 与分类处置；密钥扫描 0 命中（或显式豁免记录）。
2. `NODE-RECONCILIATION-MATRIX.json` 覆盖 N1-N29 全部节点；其中 N20、N21、N23 各有非空处置记录字段（处置类型+理由+受影响文件/ledger 行）；N24-N28 每个的 outcome-audit 引用为新的全标准审计文件（≥ N21 回溯级证据结构：断言表+发现+结论）或显式 deferral 记录。
3. 对任意 Write/Admin 注册 op，经 HTTP 远程调用且无显式授权时返回拒绝（负矩阵测试全绿；不存在仅靠 `local_only=false` 注册即放行的路径）。
4. `POST /ingestx` 不被当作 `/ingest` 处理（返回 unsupported path）；重复头/非法 Content-Length/超大 body 均受控拒绝。
5. `qbrain serve --port=notanumber` 与越界端口返回受控错误消息且进程不崩溃（无未捕获异常）。
6. 远程（经 HTTP）`get_health`/`file_list`/`file_url` 响应中不出现 `%LOCALAPPDATA%`、盘符绝对路径、`file:///` 模式。
7. 从 schema v12 必需对象清单逐一移除任一对象，`doctor` 均报 FAIL（新表 page_versions/facts/takes/file_index/raw_data 在清单内）。
8. pack 写入失败注入后，活动 pack 文件 sha256 与写入前一致（备份恢复路径）。
9. 删除构建输出目录后，CMake 与 `scripts/build-cl.ps1`/`build-tests-cl.ps1` 双路径均成功产出生产+测试二进制；测试套件 ≥30 注册、两轮全绿。
10. `docs/09`/`docs/03`/台账中的测试计数与测试二进制实际输出一致（生成值）；无 18/18 旧散文计数残留。
11. N30 实施期产生的所有提交为独立可逆切片（文档调和与源修复分离）。

## Rollback

- 每个交付物独立提交；文档调和（D10）与源修复（D3-D9）分离提交，可分别回退。
- 无破坏性迁移；schema 检查扩展不影响既有库内容（只读检测）。
- pack 原子写失败路径保留原文件；如替换后异常，从已验证备份恢复。
- 整个 N30 提交集在基线无法调和时整体回退至 `5ced8cc` + 处置清单（保留证据文档）。

## Security notes

- 中央默认拒绝；`--allow-write` 不是身份，仅本地能力开关；远程 Write/Admin 一律显式授权能力。
- 路径脱敏不泄露 `%LOCALAPPDATA%`/db 路径；负矩阵覆盖所有 Write/Admin。
- 请求体大小限制；测试根使用系统临时目录，绝不触碰 `%LOCALAPPDATA%\Qbrain`。
- 变更集密钥扫描在提交前完成；证据文件不含密钥。

## Parallelism notes (optional)

- 实施前门（时间基线+双构建）必须先行完成（串行）。
- 批准后子代理切片（文件所有权不相交）：
  - 子代理 A（治理/文档）: D1/D2/D10 — 仅 `docs/**`、`n30-evidence/**`
  - 子代理 B（授权+披露）: D3/D4/D6 — `ops/registry.*`、`ops/handlers.cpp`（授权相关行）、`cli/commands.cpp`、`mcp/http_server.cpp` 授权边界、`tests/test_n30.cpp` 授权与脱敏部分
  - 子代理 C（构建+存储）: D5/D7/D8/D9 — `mcp/http_server.cpp` 路由解析、`CMakeLists.txt`、`scripts/*.ps1`、`storage/migrate.cpp`、`schema/packs.cpp`、tests 对应部分
  - 注意 B/C 共享 `http_server.cpp` 与 `test_n30.cpp`：所有权矩阵须按函数块划分（B: 授权与脱敏路径；C: 路由解析器与负路径），冲突块由父代理串行合并（AMD-3）。
- 父代理拥有：两个审计门、合并、全量构建与测试运行、最终台账调和与（授权后的）推送。
