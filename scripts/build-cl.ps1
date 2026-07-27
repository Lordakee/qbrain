# Direct MSVC cl build without CMake rc/mt dependency
$ErrorActionPreference = "Stop"
$Root = "D:\Projects\Qbrain"
$Out = Join-Path $Root "build\cl"
New-Item -ItemType Directory -Force -Path $Out | Out-Null

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$sqlite = Join-Path $Root "third_party\sqlite\sqlite-amalgamation-3460100"
$inc = Join-Path $Root "include"
$third = Join-Path $Root "third_party"

$sources = @(
  "src\qbrain\util\paths.cpp",
  "src\qbrain\util\hash.cpp",
  "src\qbrain\util\log.cpp",
  "src\qbrain\util\string_util.cpp",
  "src\qbrain\util\time_util.cpp",
  "src\qbrain\storage\database.cpp",
  "src\qbrain\storage\migrate.cpp",
  "src\qbrain\core\types.cpp",
  "src\qbrain\core\brain.cpp",
  "src\qbrain\graph\extract.cpp",
  "src\qbrain\graph\traverse.cpp",
  "src\qbrain\graph\analytics.cpp",
  "src\qbrain\codeintel\scan.cpp",
  "src\qbrain\schema\packs.cpp",
  "src\qbrain\search\vector.cpp",
  "src\qbrain\search\rrf.cpp",
  "src\qbrain\search\hybrid.cpp",
  "src\qbrain\search\rerank.cpp",
  "src\qbrain\jobs\minions.cpp",
  "src\qbrain\cycle\dream.cpp",
  "src\qbrain\ingest\chunker.cpp",
  "src\qbrain\ingest\markdown.cpp",
  "src\qbrain\ingest\import.cpp",
  "src\qbrain\ai\http_client.cpp",
  "src\qbrain\ai\embed.cpp",
  "src\qbrain\ai\chat.cpp",
  "src\qbrain\ops\registry.cpp",
  "src\qbrain\ops\handlers.cpp",
  "src\qbrain\service\inbox_watch.cpp",
  "src\qbrain\service\live_sync.cpp",
  "src\qbrain\mcp\jsonrpc.cpp",
  "src\qbrain\mcp\server.cpp",
  "src\qbrain\mcp\http_server.cpp",
  "src\qbrain\cli\app.cpp",
  "src\qbrain\cli\commands.cpp",
  "src\qbrain\main.cpp"
) | ForEach-Object { Join-Path $Root $_ }

$srcList = ($sources | ForEach-Object { "`"$_`"" }) -join " "
$sqliteC = Join-Path $sqlite "sqlite3.c"

$bat = @"
@echo off
call "$vcvars" x64
cd /d "$Out"
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /I"$inc" /I"$third" /I"$sqlite" /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSQLITE_ENABLE_FTS5 /DSQLITE_THREADSAFE=1 /DSQLITE_OMIT_LOAD_EXTENSION /c "$sqliteC"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /I"$inc" /I"$third" /I"$sqlite" /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSQLITE_ENABLE_FTS5 /c $srcList
if errorlevel 1 exit /b 1
rem Exclude test_*.obj so unit-test objects do not collide with main.
link /nologo /OUT:qbrain.exe /MANIFEST:NO paths.obj hash.obj log.obj string_util.obj time_util.obj database.obj migrate.obj types.obj brain.obj extract.obj traverse.obj analytics.obj scan.obj packs.obj vector.obj rrf.obj hybrid.obj rerank.obj minions.obj dream.obj chunker.obj markdown.obj import.obj http_client.obj embed.obj chat.obj registry.obj handlers.obj inbox_watch.obj live_sync.obj jsonrpc.obj server.obj http_server.obj app.obj commands.obj main.obj sqlite3.obj winhttp.lib bcrypt.lib shell32.lib ole32.lib advapi32.lib ws2_32.lib
if errorlevel 1 exit /b 1
echo BUILD_OK
dir qbrain.exe
"@

$batPath = "C:\Users\ADMINI~1\AppData\Local\Temp\opencode\qbrain-cl.bat"
Set-Content -Path $batPath -Value $bat -Encoding ASCII
cmd /c $batPath
exit $LASTEXITCODE
