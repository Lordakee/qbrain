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
  "src\qbrain\search\vector.cpp",
  "src\qbrain\search\rrf.cpp",
  "src\qbrain\search\hybrid.cpp",
  "src\qbrain\ingest\chunker.cpp",
  "src\qbrain\ingest\markdown.cpp",
  "src\qbrain\ingest\import.cpp",
  "src\qbrain\ai\http_client.cpp",
  "src\qbrain\ai\embed.cpp",
  "src\qbrain\ai\chat.cpp",
  "src\qbrain\ops\registry.cpp",
  "src\qbrain\ops\handlers.cpp",
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
link /nologo /OUT:qbrain.exe /MANIFEST:NO *.obj winhttp.lib bcrypt.lib shell32.lib ole32.lib advapi32.lib
if errorlevel 1 exit /b 1
echo BUILD_OK
dir qbrain.exe
"@

$batPath = "C:\Users\ADMINI~1\AppData\Local\Temp\opencode\qbrain-cl.bat"
# Write without breaking paths - use a simpler approach via cmd
Set-Content -Path $batPath -Value $bat -Encoding ASCII
cmd /c $batPath
exit $LASTEXITCODE
