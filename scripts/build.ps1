# Build Qbrain with VS BuildTools
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path $vcvars)) {
  $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
}
if (-not (Test-Path $vcvars)) {
  throw "vcvarsall.bat not found. Install VS C++ Build Tools."
}

# Ensure cmake
$cmake = $null
foreach ($c in @(
  "cmake",
  "C:\Program Files\CMake\bin\cmake.exe",
  "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)) {
  if ($c -eq "cmake") {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { $cmake = $cmd.Source; break }
  } elseif (Test-Path $c) { $cmake = $c; break }
}

if (-not $cmake) {
  Write-Host "Installing CMake via chocolatey..."
  choco install cmake -y --installargs 'ADD_CMAKE_TO_PATH=System'
  $cmake = "C:\Program Files\CMake\bin\cmake.exe"
}

Write-Host "Using cmake: $cmake"
Write-Host "Using vcvars: $vcvars"

$buildDir = Join-Path $Root "build"
$cmd = @"
call "$vcvars" x64
"$cmake" -S "$Root" -B "$buildDir" -G "Ninja" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
  "$cmake" -S "$Root" -B "$buildDir" -G "Visual Studio 18 2026" -A x64
  "$cmake" --build "$buildDir" --config Release
) else (
  "$cmake" --build "$buildDir"
)
"@

$bat = Join-Path $env:TEMP "qbrain-build.bat"
Set-Content -Path $bat -Value $cmd -Encoding ASCII
cmd /c $bat
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Build OK"
Get-ChildItem $buildDir -Recurse -Filter qbrain.exe | Select-Object -First 5 FullName
