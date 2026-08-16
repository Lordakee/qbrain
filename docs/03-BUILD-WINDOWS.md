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
