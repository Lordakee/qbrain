# N37 Plan — 打包、CI 与项目终审（Phase 2 收官节点）

**Status**: done
**Depends on**: N36 done（全 Phase-2 特性节点完成）；docs/08 v2.0.0 §4 N37 与 §6 完成定义（退出标准 1-11）
**Plan audit**: PASS round 2 (`N37-PLAN-AUDIT.md`)
**Outcome audit**: PASS (`N37-HARD-AUDIT.md`, Claude Code 2026-08-16, 0 P0/0 P1/0 P2)；节点 PASS 后另行执行**项目级终审**


## Audit disposition (round 1)

- **P0-1 采纳**：新增"退出标准映射"（见 Goal 后）——11 条逐一映射到责任节点或 N37 AA；N37 冒烟是**验证性子集**而非重交付（8/9 由 N30 负矩阵与 N30+N35 存储完整性承载）。
- **P0-2 采纳**：版本单一事实源 = CMakeLists.txt `project(... VERSION 2.0.0 ...)`（当前 0.1.0 → 本节点更新为 2.0.0）；version.hpp 常量与之同步，AA4 一致性检查无条件化。
- **P0-3 采纳**：可复现判据精确化——两次打包的 MANIFEST.json **文件内容完全一致**（逐字段）+ zip 内文件清单（路径+sha256 对）一致；zip 整体 sha256 一致为更强判据；时间戳限制（如发生）记录于 REPRODUCIBILITY-NOTE.md。
- **P0-4 采纳**：D6 表述修正——master plan 头部状态更新为 "Phase 2 COMPLETE (2026-08-16)"；§3 DAG 注记 N30-N37 done；docs/09 补 Phase-2 收官叙述。
- **P0-5 采纳**：CI workflow 精确命令（windows-latest；cmake -G "Visual Studio 17 2022" -A x64 → build --config Release → ctest -C Release --output-on-failure；产物上传）；首次运行证据 = Actions 触发/排队状态记录，不伪造。
- **P2-1..4 全采纳**：数据根测试具体化（brain 路径结构 + id 字节受控断言）；冒烟证据文件化（SMOKE-*.txt 含预期关键字）；secrets 扫描工具化（gitleaks 或等价 + SECRETS-SCAN.txt 记录逐条评估）；测试计数证据指针（FINAL-VERIFY-*-RUN{1,2}.txt）。

## 退出标准映射（P0-1；master plan §6 的 11 条）

| # | 标准 | 责任 |
|---|------|------|
| 1 | 干净工作树连贯提交 | N30 已达成（后续各节点均切片提交推送） |
| 2 | master plan 覆盖全部活跃节点与 deferral | D6/AA6 |
| 3 | 每节点自身 plan+审计+证据+测试齐全 | D6/AA5（GOVERNANCE-INDEX 全覆盖核对） |
| 4 | 无 pending/BLOCKED/过期审计残留 | D6/AA5 |
| 5 | registry/清单/测试/台账精确一致 | N31 已达成（n31_a 四方断言持续生效） |
| 6 | 双路径干净构建无机器特有路径 | D1/AA1（N30 D7 已消绝对路径，AA1 复验） |
| 7 | 注册套件重复通过、计数生成 | AA1/AA8 |
| 8 | MCP 矩阵（远程默认拒绝/隔离/畸形/脱敏/回滚/无密钥） | **N30 负矩阵承载**；D4 冒烟为集成后验证子集 |
| 9 | schema 健康全对象/原子 pack/恢复演示 | **N30 D8/D9 + N35 契约套件承载**；D4 冒烟验证恢复路径 |
| 10 | 每个 gbrain 缺口实现+测试或显式 deferral | D6/AA6（台账 Tier 注记 + docs/10 deferral 记录） |
| 11 | 最终 Claude Code 项目级硬审 PASS | 节点 PASS 后的独立项目级终审门（非本节点 AA） |

## Goal

产出可复现的 Windows 发布物并闭合项目：干净 checkout 双路径构建、确定性打包（zip 内容+版本清单+哈希）、数据根行为文档化+测试、全套件重复通过、五类冒烟（CLI/stdio MCP/HTTP MCP/存储恢复/授权与脱敏）、全部文档一致性调和（master plan/节点计划/审计/台账/完成文档零矛盾）、secrets 终扫，然后 Claude Code **项目级终审** PASS。

## Ledger rows enhanced

| op | notes |
|----|-------|
| （无新 op） | 打包/CI/文档基础设施；台账仅做一致性终调和 Tier-1 状态核对 |

## Deliverables

1. **D1 打包脚本**: `scripts/package.ps1` — 干净构建→收集产物（qbrain.exe、qbrain_tests.exe、LICENSE、README 类、docs 子集）→ `dist/qbrain-<version>-win-x64.zip` + `MANIFEST.json`（每文件 sha256/size；**version 单一事实源 = CMakeLists project VERSION（本节点更新 0.1.0 → 2.0.0），version.hpp 常量同步**）；可复现判据（P0-3）：两次执行的 MANIFEST.json 文件内容完全一致（逐字段）且 zip 内文件清单（路径+sha256 对）一致；zip 整体 sha256 一致为更强判据；时间戳限制（如发生）记录于 n37-evidence/REPRODUCIBILITY-NOTE.md。
2. **D2 版本常量**: CMakeLists.txt `project(qbrain VERSION 2.0.0 ...)`（0.1.0 → 2.0.0 更新）+ `include/qbrain/version.hpp`（QBRAIN_VERSION_MAJOR/MINOR/PATCH = 2.0.0，与 CMake 一致性由 AA4 断言）+ `qbrain --version` CLI 输出。
3. **D3 数据根文档+测试**: docs/03 补数据根解析说明（%LOCALAPPDATA%\Qbrain\brains\<id> 结构）；test 断言 qbrain_root()/brain 路径解析规则（既有 util 测试核对，缺口补）。
4. **D4 冒烟脚本**: `scripts/smoke.ps1` — 隔离数据根下：CLI put/search 往返；stdio MCP initialize/tools-list/tools-call NDJSON 会话；HTTP MCP 带 token 的 health+JSON-RPC 与无 token 401；存储恢复（损坏库 doctor FAIL→修复路径/备份恢复说明）；远程脱敏（get_health 无本地路径）。输出记录至 n37-evidence/SMOKE-{CLI,STDIO,HTTP,RECOVERY,REDACT}.txt（各含预期关键字：CLI 含 retrieved slug；stdio 含 result 对象；HTTP 401 含 unauthorized；doctor 含 FAIL 判据；health 响应不含盘符路径；P2-2）。
5. **D5 CI 骨架**: `.github/workflows/ci.yml` — windows-latest：`cmake -B build -G "Visual Studio 17 2022" -A x64` → `cmake --build build --config Release` → `ctest --test-dir build -C Release --output-on-failure`；上传 qbrain.exe/qbrain_tests.exe 与打包 zip 为 artifact（P0-5）。首次运行证据 = push 后 Actions 触发/排队状态记录（不伪造结果）。
6. **D6 文档一致性终调（P0-4）**: master plan 头部状态更新为 "Phase 2 COMPLETE (2026-08-16)"；§3 Phase 2 DAG 注记 N30-N37 done；docs/09 补 Phase-2 收官叙述（保留历史波次记录）；台账 Tier 注记最终核对；全部节点 PLAN 状态=done 与审计 PASS 文件一一对应表（n37-evidence/GOVERNANCE-INDEX.md）。
7. **D7 secrets 终扫（P2-3）**: gitleaks 或等价工具/模式集（API key/token/password/private key 正则）扫描全仓与 dist/；零真实正例（命中逐条评估记录）；结果存 n37-evidence/SECRETS-SCAN.txt。
8. **D8 证据**: n37-evidence/（PRE-GATE；双路径两轮全绿 = 38+新注册；打包双跑记录；冒烟输出；GOVERNANCE-INDEX）。

## Tests

- 新增 `tests/test_n37.cpp`：版本常量断言 + 数据根解析断言（单注册项 `n37_packaging`）；全套件 = 39，双路径两轮全绿。
- smoke.ps1 全部五类通过（证据落盘）。

## Acceptance assertions (falsifiable)

1. 干净 checkout（git clean 等价：rm build 目录后）双路径构建 exit 0；套件 39/39 两轮×两路径全绿。
2. `scripts/package.ps1` 产出 zip + MANIFEST.json；两次执行的 MANIFEST.json 文件内容完全一致（逐字段）且 zip 内文件清单（路径+sha256 对）一致；zip 整体 sha256 一致为更强判据（时间戳限制如发生则记录 REPRODUCIBILITY-NOTE.md）；zip 含 qbrain.exe 且 `--version` 输出 2.0.0。
3. smoke.ps1 五类冒烟全部 exit 0：CLI 往返、stdio NDJSON 会话、HTTP token 正/负例、doctor 损坏库 FAIL 闭环、远程脱敏断言；SMOKE-*.txt 五文件非空且各含预期关键字（P2-2）。
4. `qbrain --version` 输出含 2.0.0；version.hpp 常量与 CMakeLists.txt VERSION 均为 2.0.0（一致性断言）。
5. GOVERNANCE-INDEX.md 覆盖 N1-N37 全部节点：每行 {node, plan 状态 done, plan-audit 文件+结论, hard-audit 文件+结论}，无缺失行；与各文件实际状态一致（抽查 5 节点由硬审核对）。
6. master plan v2.0.0 §7/状态与 docs/09/台账三者对 Phase-2 完成态叙述一致（无"pending/计划中"残留矛盾）。
7. secrets 终扫（含 dist、docs、scripts）零真实命中（模式命中逐条评估记录）。
8. 全套件 39/39 双路径两轮全绿（证据指针：n37-evidence/FINAL-VERIFY-SCRIPT-RUN{1,2}.txt 与 FINAL-VERIFY-CMAKE-RUN{1,2}.txt，P2-4）。

## Rollback

- 打包/CI/冒烟脚本均为新增文件可独立移除；版本头与 --version 为附加能力；文档调和为独立提交可回退。无产品代码逻辑变化（除版本输出接线）。

## Security notes

- dist 不含任何数据根内容/密钥；zip 内容清单化便于审计；CI secrets 不需要（公共仓库无密钥依赖）；冒烟在系统临时数据根运行不触碰真实 %LOCALAPPDATA%\Qbrain。

## Parallelism notes

- 顺序执行（收官节点）；子代理可选：A=打包+CI+冒烟；B=文档终调+GOVERNANCE-INDEX；父代理拥有项目级终审门。
