# N38 Plan — PostgreSQL 存储后端（Phase 3 首节点）

**Status**: done
**Depends on**: N35 done（IStorageBackend 契约 + 契约套件 + docs/10 准入规则）；本机 PostgreSQL 18.4 服务运行中；docs/RESOLUTION-2026-08-15.md N35 条款（"任何可选后端须过同一套迁移+集成套件方可列为 implemented；接口 stub 不得支撑台账行"）
**Plan audit**: PASS round 2 (`N38-PLAN-AUDIT.md`)
**Outcome audit**: PASS (`N38-HARD-AUDIT.md`, Claude Code 2026-08-16, 0 P0/0 P1/1 P2 documentation note)


## DSN 状态（P0-2 批准输入）

DSN 已于 2026-08-16 提供（专用角色 qbrain_test + 独立库 qbrain_n38_test，本机 PG 18；见 n38-evidence/DSN-PROVISIONED.json）→ **范围锁定 full**。集成组证据阶段由控制器以 QBRAIN_PG_TEST_DSN 运行。

## Audit disposition (round 1)

- **P0-1 采纳（选项 a）**：新增 **D0.5 接口扩展先行**——IStorageBackend 增抽象方法 backend_file_path()（SQLite=库文件路径；PG=连接描述符 host/dbname）、backup_to(path)（SQLite=backup API 原语义；PG=pg_dump 子进程，缺席时结构化 COPY 导出并在 G6 等价断言中记录降级）；migrate.cpp 两处 handle() 调用点（sqlite3_db_filename:93 / sqlite3_backup_init:102）重构为经接口；handle() 从接口移除、降为 SqliteBackend 内部（含其余调用点核查清单，全库 grep handle() 零残留为 AA 断言）。接口变更属 N35 契约修订，docs/10 同步（D5）。
- **P0-2 采纳**：DSN 于**计划批准时点**由用户提供并记录为批准输入（QBRAIN_PG_TEST_DSN，建议专用角色 qbrain_test + 独立库）；**预承诺**：若第二轮审计通过时仍无 DSN，本计划自动锁定为 partial 范围（契约级+普查+结构改造清单，集成组不声称，台账标签 partial），不把决定推迟到硬审。硬审据批准时记录的 DSN 验证集成组全绿。
- **P0-3 采纳**：计数分类学**预先承诺**——translatable（INSERT OR IGNORE、datetime()、last_insert_rowid()、json 函数、PRAGMA 剔除）**不算 structural**；structural = 需抽象改造的三类，且**预分类**：FTS5（external-content+触发器+MATCH）→ 由 D0.5 扩展之接口方法 fts_search(...) 承载（SQLite 原路径/PG tsvector+GIN）；backup/filename → D0.5 已解；busy/锁语义 → D1 映射。**分流标准改写为可证伪**：census 列出的每一条 structural 项必须被上述三类接口扩展之一消解（逐条标注消解点）；存在任何无法消解项 → partial，否则 full。FTS 不再是开放裁量。
- **P2-1 采纳**：libpq 发现取 "C:Program FilesPostgreSQL\" 下**最高版本号目录**（文档化；发现失败=预期行为，QBRAIN_WITH_PG 自动 OFF + 警告）。
- **P2-2 采纳**：AA5 增具体负测试：注入 DSN postgresql://user:SECRET123@localhost/db 诱导连接失败，错误/日志输出 grep 断言 SECRET123 不存在且含 host/dbname。
- **P2-3 采纳**：AA1 注记——D3 双语化必须保持 SQLite 行为逐位一致（如 ON CONFLICT 改写不得扰动 rowid/行序语义）；39 项零修改全绿即等价性证明，任何断言变化即 AA1 证伪。

## Goal

实现 **libpq PostgreSQL 后端**并通过 N35 存储契约，使 Qbrain 可通过 `QBRAIN_PG_DSN` 环境变量以 PG 作为实际数据后端运行（SQLite 保持默认，PG 显式 opt-in）。诚实分级：本节点交付**契约级 PG 后端 + 产品级 PG 运行能力**，其边界由 D1 的 SQL 方言普查结果以可证伪的分流标准确定（普查早于全量实现，防止范围谎言）。

## Ledger rows enhanced

| op | notes |
|----|-------|
| （无新 op） | 存储基础设施。台账新增"后端"注记：SQLite（默认，implemented）+ PostgreSQL（N38 契约级+产品级能力，标签按 D1 普查分流结果如实记录：full 或 partial+census 附录）；不声称超出套件证明的 parity |

## Deliverables

0. **D0.5 接口扩展（先于普查，P0-1）**: backend.hpp 增 backend_file_path() / backup_to(path) / fts_search(...) 三抽象（SQLite 实现保持现行为）；migrate.cpp 及一切 handle() 调用点改经接口；接口上 handle() 移除；全库 grep handle() 于 storage 之外零残留（AA 断言）。
1. **D0 SQL 方言普查（先行，分流门；分类学与消解规则见处置 P0-3）**: `docs/nodes/n38-evidence/SQL-CENSUS.json` — 枚举全部经 `storage::Database` 发出的 SQL（schema_sql.hpp、migrate.cpp、brain.cpp、handlers.cpp、jobs/minions.cpp、files/store.cpp、search/*.cpp 等的每条语句），分类：`portable`（PG 可直接执行）/ `translatable`（机械改写：INSERT OR IGNORE→ON CONFLICT、datetime('now')→now()、自增/last_insert_rowid→RETURNING、PRAGMA 剔除、FTS5→tsvector、json 函数等，逐条给出改写式）/ `structural`（需要抽象改造：FTS 触发器、backup、busy 语义、sqlite 专有句法）。**分流标准（P0-3 改写，预承诺）**：census 每条 structural 项须被 D0.5 三接口扩展之一逐条消解（census 记录消解点）；**任何无法消解项存在 → partial（契约级+改造清单+集成组不声称）**；全部消解 → full（产品级 PG 模式）。DSN 缺席于批准时点 → 亦锁定 partial（P0-2 预承诺）。
2. **D1 libpq 后端**: `src/qbrain/storage/pg_backend.cpp` + `include/qbrain/storage/pg_backend.hpp` — 实现 N35 `IStorageBackend` 全接口（open/exec/prepare/bind 族/step/事务/changes/错误分类映射 int 语义→docs/10 错误类表新增 PG 列）；busy 语义映射（PG lock_timeout/serialization_failure → busy 类）；`QBRAIN_PG_DSN` 仅从 env 读取，任何日志/错误脱敏 DSN（host 可见、password 剥离）。libpq 发现次序：`QBRAIN_PG_ROOT` env → 默认 `C:\Program Files\PostgreSQL\<ver>` 扫描 → vswhere 无关；未找到时 PG 后端编译开关关闭（`QBRAIN_WITH_PG` CMake 选项，默认 ON 但发现失败降级 OFF 并警告）——SQLite 主路径永不因 PG 缺席而失败。
3. **D2 PG 规范 schema**: v13 等价 PG DDL（BIGINT GENERATED ALWAYS AS IDENTITY、TEXT/bytea、tsvector+GIN 全文、timestamptz）；幂等创建（IF NOT EXISTS 集 + schema_version 表）；`Brain::open` 在 DSN 存在时走 PG（含迁移版本校验：PG 库版本必须 == 13 才可用，旧库拒绝并指引）。
4. **D3 方言层**: 按普查的 translatable 清单实现改写——优先**源头双语化**（直接改调用点 SQL 为双方言兼容写法，如统一用 ON CONFLICT/标准时间函数/参数化 blob），仅对 FTS/自增等做后端内适配；structural 项按 D0 分流执行或列入 N39。
5. **D4 契约+集成测试**: `tests/test_n38.cpp`（单注册 `n38_pg_backend`）：(a) 无 DSN 时跳过逻辑**不存在**——改为**始终运行**的单元组（DSN 解析/脱敏、SQL 改写器纯函数、PG DDL 生成字符串快照）；(b) `QBRAIN_PG_TEST_DSN` 存在时运行**集成组**：N35 契约 G1-G8 的 PG 等价全过（事务原子性、并发锁分类+重试、prepared rebind、全文+索引、blob 字节一致、备份（pg_dump 等价或 COPY 路径，按 D0 定级）、迁移幂等、错误分类）+ 产品级冒烟子集（put_page/get_page/search/graph/job 提交领取完成于 PG 库）。DSN 缺席时集成组报告 `[SKIP-PG] no QBRAIN_PG_TEST_DSN`（显式可见，不计 PASS）；**outcome 硬审要求集成组全绿**——DSN 由用户提供（实施前门输入）。
6. **D5 文档与治理**: docs/10 增 PG 实现注记+错误映射表+准入结果行；docs/03 增 PG 配置节（DSN 格式、建库建议命令）；master plan v2.1.0 增补：Phase 3 开启（N38 条目+诚实范围标签）；台账后端注记行。
7. **D6 证据**: n38-evidence/（PRE-GATE 基线=本节点批准提交；SQL-CENSUS.json；SQLite 主路径 39/39 两轮×双路径不变证明；PG 集成组全绿输出；DSN 脱敏演示）。

## Tests

- test_n38.cpp 单注册（单元组恒跑 + 集成组 DSN 门控）；SQLite 全套件 **39/39 零修改**（主路径回归证明）+ 40 注册（含 n38）双路径两轮全绿（无 DSN 环境下集成组显式 SKIP 计数）；有 DSN 环境再跑一遍 40/40 且集成组全绿。
- CMake+脚本双路径均含 pg_backend.cpp（当 QBRAIN_WITH_PG=ON）。

## Acceptance assertions (falsifiable)

1. `QBRAIN_PG_DSN` 未设置：一切行为与 N37 完成态一致（39 项零修改全绿；PG 代码路径零激活）。**P2-3 注记：D3 双语化须保持 SQLite 行为逐位一致（rowid/行序等），39 项零修改全绿即等价证明，任何断言漂移即本条证伪。**
2. DSN 设置且库为空 schema：`Brain::open` 自动建 v13 等价 schema，`SELECT MAX(version)`==13，二次打开 no-op。
3. 契约集成组 G1-G8 PG 等价全绿（含并发写锁分类可区分+可重试、prepared rebind、blob sha256 一致、错误三分类映射正确）。
4. 产品级冒烟子集（put/get/search/graph/job 全周期）在 PG 库上通过，数据可查（psql 可见行）。
5. 日志/错误输出不含 DSN password 段——具体负测试：注入 postgresql://user:SECRET123@localhost/db 诱导失败，输出 grep 断言 SECRET123 不存在且 host/dbname 可见（P2-2）。
6. SQL-CENSUS.json 覆盖普查源清单全部文件（每文件语句计数+分类+structural 项消解点标注），分流决定与实际交付范围一致（full 或 partial；台账标签与之相符）；src/qbrain 生产代码（storage 之外）grep handle() 零残留；tests/ 豁免（合法测试钩子：authorizer/serialize/update_hook，P2 采纳措辞）。
7. libpq 缺席机器：配置为 OFF 可编译，SQLite 全绿（构建降级警告存在）。
8. 双路径两轮全绿：SQLite 主路径 40/40（无 DSN：39 旧+1 新，集成组显式 SKIP）；DSN 环境复跑 40/40 集成组全绿（精确值以可执行输出为准，证据指针同 N37 惯例）。

## Rollback

- PG 全部为新增文件/开关（QBRAIN_WITH_PG、QBRAIN_PG_DSN 不设即全禁用）；主路径 SQL 双语化改动以"SQLite 39 项零修改全绿"为等价性证明，整体提交可回退；无 SQLite 库结构变化。

## Security notes

- DSN 仅 env、password 永不入日志/错误/证据（脱敏函数+负测试）；PG 连接限 DSN 指定实例；参数化语句保持（防注入由契约 G 组回归）；生产凭据不入 git（DSN 示例仅占位符）。

## Parallelism notes

- 实施前门（PRE-GATE + D0 普查）串行先行；普查完成分流后子代理切片：A=libpq 后端+PG schema；B=方言层双语化（按 translatable 清单）；C=测试+文档；父代理合并与双环境验证。B 与 A 在 census 交付后并行；共享文件（database.cpp/hpp 接口扩展点）由父代理串行合并（AMD-3）。
- DSN 为实施期用户输入：计划/审计阶段不需要；硬审证据阶段必需（用户提供或授权创建本机 qbrain 角色+库）。
