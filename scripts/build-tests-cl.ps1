param(
  [switch]$SkipProductionBuild,
  [string[]]$TestSources
)

# Build qbrain_tests.exe with MSVC (links production objs + test sources).
# N30 D7: paths derived from $PSScriptRoot, MSVC discovered via vswhere.exe ->
# known BuildTools fallbacks, per-run temp object dir in the system temp folder
# (no stale objects can enter the link), outputs still land in build\cl.
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Out = Join-Path $Root "build\cl"
$ObjDir = Join-Path $Out "obj"

function Find-VcvarsAll {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path $vswhere) {
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($install) {
      $candidate = Join-Path $install "VC\Auxiliary\Build\vcvarsall.bat"
      if (Test-Path $candidate) { return $candidate }
    }
  }
  $known = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat"
  )
  foreach ($path in $known) {
    if (Test-Path $path) { return $path }
  }
  throw "MSVC not found: vswhere.exe reported no VC tools and no known BuildTools vcvarsall.bat exists. Install Visual Studio Build Tools with the 'Desktop development with C++' workload."
}

$vcvars = Find-VcvarsAll
$sqlite = Join-Path $Root "third_party\sqlite\sqlite-amalgamation-3460100"
$inc = Join-Path $Root "include"
$third = Join-Path $Root "third_party"

# Ensure production objs exist unless the caller has just completed the same
# native production build and only needs to rebuild test objects. The obj dir
# is wiped by every build-cl.ps1 run, so only current-invocation objects can
# be linked.
if (-not $SkipProductionBuild) {
  & (Join-Path $Root "scripts\build-cl.ps1")
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if (-not (Test-Path (Join-Path $ObjDir "commands.obj"))) {
  throw "Production objects missing in '$ObjDir'. Run scripts\build-cl.ps1 (or drop -SkipProductionBuild)."
}

$defaultTestSources = @(
  "tests\test_main.cpp",
  "tests\test_rrf.cpp",
  "tests\test_vector.cpp",
  "tests\test_chunker.cpp",
  "tests\test_extract.cpp",
  "tests\test_storage.cpp",
  "tests\test_mcp.cpp",
  "tests\test_rerank.cpp",
  "tests\test_minions.cpp",
  "tests\test_migration_v6.cpp",
  "tests\test_n12_dream.cpp",
  "tests\test_live_sync.cpp",
  "tests\test_n13.cpp",
  "tests\test_codeintel.cpp",
  "tests\test_analytics.cpp",
  "tests\test_n19.cpp",
  "tests\test_n20.cpp",
  "tests\test_n22.cpp",
  "tests\test_n23.cpp",
  "tests\test_n20_23.cpp",
  "tests\test_n24_25.cpp",
  "tests\test_n26_27.cpp",
  "tests\test_wave4.cpp",
  "tests\test_wave5.cpp",
  "tests\test_doctor.cpp",
  "tests\test_n14.cpp",
  "tests\test_n15.cpp",
  "tests\test_n16.cpp",
  "tests\test_n17.cpp",
  "tests\test_n18.cpp",
  "tests\test_n30.cpp",
  "tests\test_n31.cpp",
  "tests\test_n32.cpp",
  "tests\test_n34.cpp",
  "tests\test_n33.cpp",
  "tests\test_n35.cpp",
  "tests\test_n36.cpp",
  "tests\test_n37.cpp"
)
# test_main.cpp statically references the complete suite, so the canonical
# closure is always compiled and linked (a focused invocation can never
# consume stale objects or fail because the output directory was clean).
# -TestSources IS honored: extra sources are appended to (not replacing) the
# canonical closure, deduplicated, e.g. during incremental node development.
$selected = New-Object System.Collections.Generic.List[string]
foreach ($s in $defaultTestSources) { $selected.Add($s) }
if ($TestSources) {
  foreach ($s in $TestSources) {
    $rel = if ([IO.Path]::IsPathRooted($s)) { $s.Substring($Root.Length + 1) } else { $s }
    if (-not $selected.Contains($rel)) { $selected.Add($rel) }
  }
}
$tests = $selected | ForEach-Object { Join-Path $Root $_ }
$testList = ($tests | ForEach-Object { "`"$_`"" }) -join " "
$testObjList = ($selected | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) + ".obj" }) -join " "
$prodObjs = @(
  "paths","hash","log","string_util","time_util","database","migrate","types","brain",
  "extract","traverse","analytics","scan","astlite","packs","lint","store","image_meta","vector","rrf","hybrid","rerank","minions","dream",
  "chunker","markdown","import","http_client","embed","chat","registry","handlers",
  "inbox_watch","live_sync","jsonrpc","server","auth","http_server","commands","sqlite3"
) | ForEach-Object { "`"$ObjDir\$_.obj`"" }
$prodObjList = $prodObjs -join " "

# Unique per-run object directory in the system temp tree: only objects
# generated by this invocation participate in the test link.
$RunDir = Join-Path ([IO.Path]::GetTempPath()) "qbrain-tests-cl-$PID-$([DateTime]::UtcNow.Ticks)"
New-Item -ItemType Directory -Force -Path $RunDir | Out-Null

$bat = @"
@echo off
call "$vcvars" x64
if errorlevel 1 exit /b 1
cd /d "$RunDir"
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /I"$inc" /I"$third" /I"$sqlite" /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSQLITE_ENABLE_FTS5 /c $testList
if errorlevel 1 exit /b 1
link /nologo /OUT:qbrain_tests.exe /MANIFEST:NO $prodObjList $testObjList winhttp.lib bcrypt.lib shell32.lib ole32.lib advapi32.lib ws2_32.lib
if errorlevel 1 exit /b 1
copy /y qbrain_tests.exe "$Out\qbrain_tests.exe" >nul
if errorlevel 1 exit /b 1
echo TESTS_BUILD_OK
cd /d "$Out"
qbrain_tests.exe
exit /b %ERRORLEVEL%
"@

$batPath = Join-Path ([IO.Path]::GetTempPath()) "qbrain-tests-cl-$PID.bat"
Set-Content -Path $batPath -Value $bat -Encoding ASCII
cmd /d /s /c $batPath
$code = $LASTEXITCODE
Remove-Item -LiteralPath $batPath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $RunDir -Recurse -Force -ErrorAction SilentlyContinue
exit $code
