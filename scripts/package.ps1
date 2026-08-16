# N37 D1: deterministic release packaging.
#
# Pipeline: clean build via the canonical scripts (build-cl.ps1 then
# build-tests-cl.ps1; the tests script also runs the full unit suite and
# fails this script on any red test) -> stage the dist payload -> zip via
# Compress-Archive -> MANIFEST.json with a sorted file list and per-file
# sha256/size.
#
# Version single source of truth: project(qbrain VERSION x.y.z) in
# CMakeLists.txt (N37 D2). The MANIFEST carries that version verbatim.
#
# Reproducibility criterion (N37 plan AA2 / P0-3): two runs of this script
# must produce byte-identical MANIFEST.json content (field by field) and an
# identical zip file list (path + sha256 pairs). MSVC link.exe would embed a
# wall-clock PE TimeDateStamp (the ONLY byte divergence between two clean
# rebuilds — measured: 4 bytes per exe), so the staged executables are
# normalized to a content-addressed stamp (see below) and staged file mtimes
# are pinned before zipping. Any residual limitation (e.g. zip container
# variance) is recorded in docs/nodes/n37-evidence/REPRODUCIBILITY-NOTE.md.
# The MANIFEST therefore contains NO timestamps and NO absolute paths.
#
# PowerShell 5.1+ / -NoProfile compatible. No secrets; dist/ holds only the
# payload listed below.
param(
  # Optional directory to receive a copy of MANIFEST.json (evidence capture).
  [string]$EvidenceDir = ""
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

# --- Version from the single source of truth (CMakeLists.txt) -------------
$cmakeLists = Join-Path $Root "CMakeLists.txt"
$cmakeText = Get-Content -LiteralPath $cmakeLists -Raw
if ($cmakeText -notmatch 'project\(qbrain\s+VERSION\s+(\d+)\.(\d+)\.(\d+)') {
  throw "cannot parse project(qbrain VERSION ...) from $cmakeLists"
}
$Version = "{0}.{1}.{2}" -f $Matches[1], $Matches[2], $Matches[3]
$PackageName = "qbrain-$Version-win-x64"
Write-Host "[package] version=$Version (from CMakeLists.txt project VERSION)"

# --- 1) Clean build via the canonical scripts -----------------------------
# build-cl.ps1 wipes the shared obj dir first, so every run links only
# objects produced by this invocation. build-tests-cl.ps1 -SkipProductionBuild
# links the test binary against those objects and RUNS the full suite; a red
# suite exits nonzero and fails packaging.
Write-Host "[package] clean production build (scripts/build-cl.ps1)"
& (Join-Path $Root "scripts\build-cl.ps1")
if ($LASTEXITCODE -ne 0) { throw "build-cl.ps1 failed with exit $LASTEXITCODE" }

Write-Host "[package] test build + full suite (scripts/build-tests-cl.ps1)"
& (Join-Path $Root "scripts\build-tests-cl.ps1") -SkipProductionBuild
if ($LASTEXITCODE -ne 0) { throw "build-tests-cl.ps1 (suite) failed with exit $LASTEXITCODE" }

$qbrainExe = Join-Path $Root "build\cl\qbrain.exe"
$qbrainTestsExe = Join-Path $Root "build\cl\qbrain_tests.exe"
foreach ($f in @($qbrainExe, $qbrainTestsExe)) {
  if (-not (Test-Path -LiteralPath $f)) { throw "expected build output missing: $f" }
}

# --- 2) Stage the dist payload --------------------------------------------
$Dist = Join-Path $Root "dist"
$Stage = Join-Path $Dist "stage\$PackageName"
# Start from a clean dist/ every run so stale files can never enter the zip.
if (Test-Path -LiteralPath $Dist) { Remove-Item -LiteralPath $Dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Stage, (Join-Path $Stage "docs") | Out-Null

# Payload (exactly as specified by the approved N37 plan D1):
#   qbrain.exe, qbrain_tests.exe, LICENSE(.md), README(.md),
#   docs/10-STORAGE-CONTRACT.md
Copy-Item -LiteralPath $qbrainExe -Destination (Join-Path $Stage "qbrain.exe")
Copy-Item -LiteralPath $qbrainTestsExe -Destination (Join-Path $Stage "qbrain_tests.exe")
$license = Join-Path $Root "LICENSE.md"
if (-not (Test-Path -LiteralPath $license)) { $license = Join-Path $Root "LICENSE" }
if (Test-Path -LiteralPath $license) { Copy-Item -LiteralPath $license -Destination (Join-Path $Stage (Split-Path -Leaf $license)) }
$readme = Join-Path $Root "README.md"
if (-not (Test-Path -LiteralPath $readme)) { $readme = Join-Path $Root "README" }
if (Test-Path -LiteralPath $readme) { Copy-Item -LiteralPath $readme -Destination (Join-Path $Stage (Split-Path -Leaf $readme)) }
$storageContract = Join-Path $Root "docs\10-STORAGE-CONTRACT.md"
if (Test-Path -LiteralPath $storageContract) {
  Copy-Item -LiteralPath $storageContract -Destination (Join-Path $Stage "docs\10-STORAGE-CONTRACT.md")
} else {
  throw "docs\10-STORAGE-CONTRACT.md is part of the approved dist payload but is missing"
}

# --- Determinism post-processing -------------------------------------------
# MSVC link.exe embeds the wall-clock time into the PE COFF TimeDateStamp
# (verified: two clean rebuilds of this project diverge in EXACTLY those
# 4 bytes per exe, duplicated at 2 sites — the header and one interior copy
# of the same value; every other byte is identical). build-cl.ps1 does not
# pass /Brepro, so this script applies the equivalent normalization itself:
# zero every occurrence of the link timestamp, then write the first 4 bytes
# of the zeroed image's sha256 into those sites (content-addressed stamp,
# the same semantics as /Brepro). The staged exe is then bit-for-bit
# reproducible across clean rebuilds. The TimeDateStamp field is advisory
# metadata; the Windows loader ignores it, and the --version gate below
# executes the NORMALIZED binary to prove it still runs.
function Find-AllBytePattern([byte[]]$Haystack, [byte[]]$Needle, [int]$Limit = 8) {
  $found = New-Object System.Collections.Generic.List[int]
  $i = 0
  while ($found.Count -lt $Limit) {
    $j = [Array]::IndexOf($Haystack, $Needle[0], $i)
    if ($j -lt 0 -or $j + $Needle.Length -gt $Haystack.Length) { break }
    $match = $true
    for ($k = 1; $k -lt $Needle.Length; $k++) {
      if ($Haystack[$j + $k] -ne $Needle[$k]) { $match = $false; break }
    }
    if ($match) { $found.Add($j) }
    $i = $j + 1
  }
  return $found
}

function Normalize-LinkTimestamp([string]$Path) {
  $bytes = [IO.File]::ReadAllBytes($Path)
  if ($bytes.Length -lt 0x40) { throw "not a PE image: $Path" }
  $peOff = [BitConverter]::ToInt32($bytes, 0x3C)
  if ($peOff -le 0 -or $peOff + 8 + 4 -gt $bytes.Length) { throw "bad PE layout: $Path" }
  $needle = New-Object byte[] 4
  [Array]::Copy($bytes, $peOff + 8, $needle, 0, 4)
  $sites = Find-AllBytePattern $bytes $needle
  if ($sites.Count -lt 1) { throw "PE TimeDateStamp not found: $Path" }
  if ($sites.Count -gt 4) { throw "unexpected TimeDateStamp site count $($sites.Count) in $Path (refusing to patch)" }
  foreach ($off in $sites) {
    $bytes[$off] = 0; $bytes[$off + 1] = 0; $bytes[$off + 2] = 0; $bytes[$off + 3] = 0
  }
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try { $digest = $sha.ComputeHash($bytes) } finally { $sha.Dispose() }
  foreach ($off in $sites) {
    $bytes[$off] = $digest[0]; $bytes[$off + 1] = $digest[1]
    $bytes[$off + 2] = $digest[2]; $bytes[$off + 3] = $digest[3]
  }
  [IO.File]::WriteAllBytes($Path, $bytes)
  Write-Host ("[package] normalized link TimeDateStamp in {0} ({1} site(s))" -f (Split-Path -Leaf $Path), $sites.Count)
}

Normalize-LinkTimestamp (Join-Path $Stage "qbrain.exe")
Normalize-LinkTimestamp (Join-Path $Stage "qbrain_tests.exe")

# Pin staged file mtimes so Compress-Archive embeds a fixed timestamp into
# every zip entry; combined with byte-normalized payloads this aims for
# whole-zip sha256 equality, the stronger reproducibility criterion.
$FixedStamp = [DateTime]::new(2020, 1, 1, 0, 0, 0, [DateTimeKind]::Local)
Get-ChildItem -LiteralPath $Stage -Recurse -File | ForEach-Object {
  $_.LastWriteTime = $FixedStamp
}

# AA2 gate: the packaged qbrain.exe must report the manifest version.
$versionOut = & (Join-Path $Stage "qbrain.exe") --version
if ($LASTEXITCODE -ne 0 -or -not (($versionOut -join "`n") -match [regex]::Escape($Version))) {
  throw "staged qbrain.exe --version did not report $Version (got: $versionOut)"
}
Write-Host "[package] staged qbrain.exe --version -> $($versionOut -join ' ')"

# --- 3) MANIFEST.json (deterministic: sorted, no timestamps, no abs paths) --
$stageFiles = Get-ChildItem -LiteralPath $Stage -Recurse -File |
  Sort-Object { $_.FullName.Substring($Stage.Length + 1).Replace('\', '/') }
$manifestFiles = New-Object System.Collections.Generic.List[object]
foreach ($f in $stageFiles) {
  $rel = $f.FullName.Substring($Stage.Length + 1).Replace('\', '/')
  $hash = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  $manifestFiles.Add([ordered]@{ path = "$PackageName/$rel"; sha256 = $hash; size = $f.Length })
}
$manifest = [ordered]@{
  name    = $PackageName
  version = $Version
  files   = $manifestFiles
}
# Out-String joins the pipeline lines into ONE string (PS5.1 returns an
# array of lines) so WriteAllText receives a proper single string.
$manifestJson = ($manifest | ConvertTo-Json -Depth 5) | Out-String
# UTF-8 without BOM; content has no timestamps/abs paths, so identical
# payloads yield identical bytes.
$manifestPath = Join-Path $Dist "MANIFEST.json"
[IO.File]::WriteAllText($manifestPath, $manifestJson, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "[package] MANIFEST.json -> $manifestPath ($($manifestFiles.Count) files)"

# --- 4) Zip ----------------------------------------------------------------
$zipPath = Join-Path $Dist "$PackageName.zip"
# Compress-Archive with the stage DIRECTORY (not its contents) keeps the
# top-level $PackageName/ folder inside the archive, matching the manifest
# paths exactly.
Compress-Archive -Path $Stage -DestinationPath $zipPath -Force
Write-Host "[package] zip -> $zipPath"

# Zip file-list equality gate (AA2/P0-3): archive entries must match the
# manifest paths one-for-one (directory entries excluded). Entry names are
# normalized to '/' because Windows PowerShell 5.1's Compress-Archive writes
# '\' separators into entry FullName (a known quirk; the zip spec mandates
# '/', and Expand-Archive/.NET accept both).
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
  $entryNames = $zip.Entries | Where-Object { $_.Name -ne "" } |
    ForEach-Object { $_.FullName.Replace('\', '/') } | Sort-Object
  $manifestPaths = $manifestFiles | ForEach-Object { $_.path } | Sort-Object
  $diff = Compare-Object -ReferenceObject $manifestPaths -DifferenceObject $entryNames
  if ($diff) {
    throw "zip file list does not match MANIFEST paths: $($diff | Out-String)"
  }
} finally {
  $zip.Dispose()
}

$zipSha = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "[package] zip sha256 = $zipSha"
Write-Host "[package] DONE version=$Version files=$($manifestFiles.Count)"

if ($EvidenceDir) {
  New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
  Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $EvidenceDir "MANIFEST.json") -Force
}
exit 0
