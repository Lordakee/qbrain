# Windows 11 原生构建指南

## 前置条件

1. Windows 11 x64  
2. **完整** Visual Studio 2022/2026 Build Tools，工作负载：  
   - 使用 C++ 的桌面开发 / MSVC v143+  
   - Windows 11 SDK (10.0.22621 或 26100+)  
3. CMake ≥ 3.24（`choco install cmake`）  

检查：

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -all -products * -format json
# isComplete 必须为 true，isLaunchable 必须为 true
```

若 `isComplete: false`，运行：

```powershell
# 管理员 PowerShell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe" `
  modify `
  --installPath "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools" `
  --add Microsoft.VisualStudio.Workload.VCTools `
  --includeRecommended `
  --passive
```

## 构建

```powershell
# 开发者命令提示符 x64
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 或 VS 生成器
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

直接 cl（无 CMake，项目当前验证路径）：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-cl.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build-tests-cl.ps1
```

## 冒烟

```powershell
.\build\cl\qbrain.exe init
.\build\cl\qbrain.exe capture "hello"
.\build\cl\qbrain.exe search hello --no-vector
.\build\cl\qbrain.exe doctor --json
```

## 已验证构建（2026-07-28）

SDK 补齐后可用直接 cl 构建：

```text
产物: D:\Projects\Qbrain\build\cl\qbrain.exe
单元测试: 31/31 (N30, two rounds, both paths) PASS (scripts\build-tests-cl.ps1)
冒烟: init/capture/import/search/doctor OK
```

若 `vcvars` 后仍缺头文件，确认存在：

```text
C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt\malloc.h
C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64
```

若本地 MCP server 正运行在 `build\cl\qbrain.exe`，`scripts\build-cl.ps1` 会在链接前停止同一路径的本地 `qbrain.exe` 进程以释放文件锁。

## Token-scoped HTTP MCP auth (N36)

`QBRAIN_MCP_TOKENS` (env, `;`-separated): `name:token:scope[,scope]` with
scope in {read, write, admin}; tokens are ASCII printable, 16-256 chars.
The HTTP MCP validates `Authorization: Bearer <token>` in constant time and
maps the scope to the central authorization gate (write/admin capabilities);
malformed headers and unknown tokens are rejected with 401. Audit lines log
the token's sha256 prefix (16 hex chars) — never the token itself. The legacy
`QBRAIN_MCP_TOKEN` remains transport auth with no capability (N30 semantics).
Explicitly deferred: TLS (loopback-only boundary), OAuth, dynamic user
stores, token rotation, per-token brain/source restrictions (Phase-3).

## PostgreSQL 后端配置（N38）

SQLite 仍是**默认**存储后端；PostgreSQL 是显式 opt-in（`QBRAIN_PG_DSN`
存在时才激活）。PG 代码路径由 CMake 选项 `QBRAIN_WITH_PG`（默认 `ON`）
控制：构建系统按以下次序发现 libpq —— ① `QBRAIN_PG_ROOT` 环境变量指向
的安装根；② 默认扫描 `C:\Program Files\PostgreSQL\` 下**最高版本号**
目录。发现失败是**预期行为**：`QBRAIN_WITH_PG` 自动降级为 `OFF` 并给出
警告，SQLite 主路径永不因 PG 缺席而失败。

### DSN 格式（`QBRAIN_PG_DSN`）

```text
postgresql://<user>:<password>@<host>:<port>/<dbname>
```

示例（占位符，**勿提交真实口令**）：

```text
postgresql://qbrain_test:<PLACEHOLDER_PASSWORD>@127.0.0.1:5432/qbrain_n38_test
```

- DSN **仅**从环境变量读取，不接受命令行/配置文件传入。
- 任何日志/错误输出中的 DSN 一律脱敏：host、dbname、user 可见，
  password 永不出现（负测试见 `tests/test_n38.cpp` 单元组，注入
  `SECRET123` 诱导连接失败后 grep 断言其不存在）。
- 测试专用 DSN 环境变量为 `QBRAIN_PG_TEST_DSN`（集成组门控）。

### 建库/建角色示例命令（psql，超级用户执行）

```sql
-- 占位符口令仅作示例；生产凭据不入 git
CREATE ROLE qbrain_test LOGIN PASSWORD '<PLACEHOLDER_PASSWORD>';
CREATE DATABASE qbrain_n38_test OWNER qbrain_test;
GRANT ALL ON DATABASE qbrain_n38_test TO qbrain_test;
```

连接要求：PG 库 schema 版本必须**恰好等于** 13 才可用；空库由
`Brain::open` 自动建 v13 等价 schema，旧版本库拒绝并指引升级。
每次测试运行使用专用库/schema（测试自行 drop/recreate），勿指向生产库。

## 数据根（Data Root）解析（N37 D3）

数据根解析实现于 `src/qbrain/util/paths.cpp`（头文件 `include/qbrain/util/paths.hpp`）：

1. **根定位**：优先读取进程环境变量 `LOCALAPPDATA`（显式覆盖，供隔离测试/冒烟使用）；
   未设置时回退 `SHGetKnownFolderPath(FOLDERID_LocalAppData)`。数据根为
   `%LOCALAPPDATA%\Qbrain\`。
2. **目录结构**（按实际实现）：

   ```text
   %LOCALAPPDATA%\Qbrain\
     ├─ brains\<normalized-id>\brain.db   每个 brain 一个 SQLite 库
     ├─ config.json                       配置
     └─ audit\                            审计日志
   ```

   `qbrain_root()` = `<LOCALAPPDATA>\Qbrain`；`brains_root()` = `<root>\brains`；
   `brain_dir(id)` = `brains_root() / normalize_brain_id(id)`；
   `brain_db_path(id)` = `brain_dir(id) / "brain.db"`；
   `config_path()` = `<root>\config.json`；`audit_dir()` = `<root>\audit`。
3. **Brain id 规则**（`normalize_brain_id`，行为为**拒绝**而非静默清洗）：
   - 允许字符集：`a-z`、`0-9`、`_`、`-`（`A-Z` 折叠为小写）；长度 1–64 字节。
   - 其他任何字节（含 `.`、`/`、`\`、`:`、空格等）→ 抛出
     `std::runtime_error("invalid brain id")`，即路径穿越（`..`）、盘符
    （`c:`、`C:\evil`）、分隔符注入均被直接拒绝。
   - Windows 保留设备名（`con`、`prn`、`aux`、`nul`、`com1`-`com9`、
     `lpt1`-`lpt9`，折叠后比较）同样拒绝。
4. 行为由 `tests/test_n37.cpp`（注册项 `n37_packaging`）断言：版本常量、
   隔离 `LOCALAPPDATA` 覆盖下的路径结构、以及上述敌意 id 的拒绝行为。

## Rerank model (N39)

Optional independent `"rerank"` section in config.json (`model`, `base_url`,
`api_key`). Empty fields fall back to the matching `chat` value, so omitting
the section entirely keeps the pre-N39 behavior (rerank uses the chat model).
`rerank.api_key` is DB/env only and is never mirrored to the file plane
(same rule as `embedding.api_key`/`chat.api_key`). Typical use: a cheaper or
faster model for `search --rerank` while `think` uses the main chat model.

## Chat Responses API and reasoning effort (user-directed change)

config.json or `qbrain config set` supports two new chat keys:
- `chat.endpoint`: `"responses"` (default, OpenAI Responses API) or
  `"chat_completions"` (legacy endpoint)
- `chat.reasoning_effort`: `"low"`, `"medium"`, `"high"`, or `"max"`
  (sent as `reasoning.effort` on the Responses endpoint; empty = omit)

Example:
```json
"chat": {
  "model": "gpt-5.6-luna",
  "base_url": "http://your-relay:8080/v1",
  "api_key": "sk-...",
  "endpoint": "responses",
  "reasoning_effort": "max"
}
```

## Native rerank API (rerank.api_type = "native")

Set `rerank.api_type` to `"native"` to use a dedicated rerank API endpoint
(e.g., Zhipu bigmodel `/rerank`) instead of LLM-based rerank. The request
format is `{model, query, documents: [{title, text}...]}` and the response
`{results: [{index, relevance_score}...]}` reorders by score descending.

```json
"rerank": {
  "model": "rerank",
  "base_url": "https://open.bigmodel.cn/api/paas/v4",
  "api_key": "...",
  "api_type": "native"
}
```

Omit `api_type` (or set `"llm"`) to keep the N39 chat-based rerank behavior.
