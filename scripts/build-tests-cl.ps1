# Build qbrain_tests.exe with MSVC (links production objs + test sources)
$ErrorActionPreference = "Stop"
$Root = "D:\Projects\Qbrain"
$Out = Join-Path $Root "build\cl"
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$sqlite = Join-Path $Root "third_party\sqlite\sqlite-amalgamation-3460100"
$inc = Join-Path $Root "include"
$third = Join-Path $Root "third_party"

# Ensure production objs exist (rerank/minions/dream etc.)
& (Join-Path $Root "scripts\build-cl.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$tests = @(
  "tests\test_main.cpp",
  "tests\test_rrf.cpp",
  "tests\test_vector.cpp",
  "tests\test_chunker.cpp",
  "tests\test_extract.cpp",
  "tests\test_storage.cpp",
  "tests\test_mcp.cpp",
  "tests\test_rerank.cpp",
  "tests\test_minions.cpp",
  "tests\test_live_sync.cpp",
  "tests\test_codeintel.cpp"
) | ForEach-Object { Join-Path $Root $_ }

$testList = ($tests | ForEach-Object { "`"$_`"" }) -join " "
$prodObjs = @(
  "paths","hash","log","string_util","time_util","database","migrate","types","brain",
  "extract","traverse","scan","vector","rrf","hybrid","rerank","minions","dream",
  "chunker","markdown","import","http_client","embed","chat","registry","handlers",
  "inbox_watch","live_sync","jsonrpc","server","http_server","sqlite3"
) | ForEach-Object { "$_.obj" }
$objList = $prodObjs -join " "

$bat = @"
@echo off
call "$vcvars" x64
cd /d "$Out"
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /I"$inc" /I"$third" /I"$sqlite" /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSQLITE_ENABLE_FTS5 /c $testList
if errorlevel 1 exit /b 1
link /nologo /OUT:qbrain_tests.exe /MANIFEST:NO $objList test_main.obj test_rrf.obj test_vector.obj test_chunker.obj test_extract.obj test_storage.obj test_mcp.obj test_rerank.obj test_minions.obj test_live_sync.obj test_codeintel.obj winhttp.lib bcrypt.lib shell32.lib ole32.lib advapi32.lib ws2_32.lib
if errorlevel 1 exit /b 1
echo TESTS_BUILD_OK
qbrain_tests.exe
exit /b %ERRORLEVEL%
"@
$batPath = "C:\Users\ADMINI~1\AppData\Local\Temp\opencode\qbrain-tests-cl.bat"
Set-Content -Path $batPath -Value $bat -Encoding ASCII
cmd /c $batPath
exit $LASTEXITCODE
