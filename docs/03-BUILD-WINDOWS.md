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
