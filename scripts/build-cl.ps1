param(
  [string[]]$SourceFiles
)

# Direct MSVC cl build without CMake rc/mt dependency (N30 D7: paths derived
# from $PSScriptRoot, MSVC discovered via vswhere.exe -> known BuildTools
# fallbacks, temp files under the system temp dir, objects isolated per run).
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Out = Join-Path $Root "build\cl"
$ObjDir = Join-Path $Out "obj"
New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null
# Only objects produced by this invocation participate in the link: wipe the
# shared object directory so a changed source list can never mix in stale objs.
Get-ChildItem -LiteralPath $ObjDir -Filter "*.obj" -File -ErrorAction SilentlyContinue |
  Remove-Item -Force

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

$productionSources = @(
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
  "src\qbrain\schema\lint.cpp",
  "src\qbrain\files\store.cpp",
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
)
if ($SourceFiles -and $SourceFiles.Count -gt 0) {
  $productionSources = $SourceFiles
}
$sources = $productionSources | ForEach-Object { Join-Path $Root $_ }

$srcList = ($sources | ForEach-Object { "`"$_`"" }) -join " "
$sqliteC = Join-Path $sqlite "sqlite3.c"
$prodObjNames = @(
  "paths","hash","log","string_util","time_util","database","migrate","types","brain",
  "extract","traverse","analytics","scan","packs","lint","store","vector","rrf","hybrid","rerank","minions","dream",
  "chunker","markdown","import","http_client","embed","chat","registry","handlers",
  "inbox_watch","live_sync","jsonrpc","server","http_server","app","commands","main"
)
$prodObjList = ($prodObjNames | ForEach-Object { "$_.obj" }) -join " "

$bat = @"
@echo off
call "$vcvars" x64
if errorlevel 1 exit /b 1
cd /d "$ObjDir"
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /I"$inc" /I"$third" /I"$sqlite" /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSQLITE_ENABLE_FTS5 /DSQLITE_THREADSAFE=1 /DSQLITE_OMIT_LOAD_EXTENSION /c "$sqliteC"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /I"$inc" /I"$third" /I"$sqlite" /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSQLITE_ENABLE_FTS5 /c $srcList
if errorlevel 1 exit /b 1
rem Link exactly the object set produced by this invocation.
link /nologo /OUT:qbrain.exe /MANIFEST:NO $prodObjList sqlite3.obj winhttp.lib bcrypt.lib shell32.lib ole32.lib advapi32.lib ws2_32.lib
if errorlevel 1 exit /b 1
copy /y qbrain.exe "$Out\qbrain.exe" >nul
if errorlevel 1 exit /b 1
echo BUILD_OK
"@

$batPath = Join-Path ([IO.Path]::GetTempPath()) "qbrain-cl-$PID.bat"
Set-Content -Path $batPath -Value $bat -Encoding ASCII
cmd /d /s /c $batPath
$code = $LASTEXITCODE
Remove-Item -LiteralPath $batPath -Force -ErrorAction SilentlyContinue
exit $code
