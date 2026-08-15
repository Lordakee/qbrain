# N13 CLI runtime evidence. Uses an isolated LOCALAPPDATA and mock embeddings.
$ErrorActionPreference = "Stop"
if (Test-Path Variable:PSNativeCommandUseErrorActionPreference) {
  $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Split-Path -Parent $PSScriptRoot
$Qbrain = Join-Path $Root "build\cl\qbrain.exe"
$EvidenceDir = Join-Path $Root "docs\nodes\n13-evidence"
$LogPath = Join-Path $EvidenceDir "CLI-SMOKE-OUTPUT.txt"
$TempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("qbrain_n13_cli_" + [guid]::NewGuid().ToString("N"))
$Notes = Join-Path $TempRoot "notes"
$IsolatedLocalAppData = Join-Path $TempRoot "localappdata"
$OriginalLocalAppData = $env:LOCALAPPDATA
$OriginalEmbedMock = $env:QBRAIN_EMBED_MOCK
$Log = [System.Collections.Generic.List[string]]::new()

function Require([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw $Message }
}

function Run-Qbrain([string[]]$CommandArgs) {
  $rendered = @($CommandArgs | ForEach-Object {
    if ($_ -match '\s') { '"' + $_ + '"' } else { $_ }
  }) -join ' '
  $Log.Add("> qbrain $rendered")
  $output = @(& $Qbrain @CommandArgs 2>&1 | ForEach-Object { $_.ToString() })
  $exitCode = $LASTEXITCODE
  foreach ($line in $output) { $Log.Add($line) }
  $Log.Add("exit_code=$exitCode")
  $Log.Add("")
  [pscustomobject]@{ Output = ($output -join "`n"); ExitCode = $exitCode }
}

Require (Test-Path -LiteralPath $Qbrain) "missing qbrain.exe: $Qbrain"
New-Item -ItemType Directory -Force -Path $EvidenceDir, $Notes, $IsolatedLocalAppData | Out-Null

try {
  $env:LOCALAPPDATA = $IsolatedLocalAppData
  $env:QBRAIN_EMBED_MOCK = "1"
  [System.IO.File]::WriteAllText((Join-Path $Notes "a.md"), "# A`r`n`r`n[[b]]`r`n")
  [System.IO.File]::WriteAllText((Join-Path $Notes "b.markdown"), "# B`r`n`r`nbody`r`n")

  $first = Run-Qbrain @("sync", $Notes, "--once", "--source", "cli_source", "--brain", "n13-cli-smoke")
  Require ($first.ExitCode -eq 0) "sync --once failed"
  Require ($first.Output -match 'live_sync scanned=2 imported=2 skipped=0 errors=0') "unexpected first sync output: $($first.Output)"

  $second = Run-Qbrain @("sync", $Notes, "--once", "--source", "cli_source", "--brain", "n13-cli-smoke")
  Require ($second.ExitCode -eq 0) "idempotent sync --once failed"
  Require ($second.Output -match 'live_sync scanned=2 imported=0 skipped=2 errors=0') "unexpected second sync output: $($second.Output)"

  [System.IO.File]::WriteAllText((Join-Path $Notes "a.md"), "# A`r`n`r`n[[b]] changed content`r`n")
  $watch = Run-Qbrain @("sync", $Notes, "--watch", "--once", "--interval", "1", "--source", "cli_source", "--brain", "n13-cli-smoke")
  Require ($watch.ExitCode -eq 0) "sync --watch --once failed"
  Require ($watch.Output -match 'live_sync_watch imported_pages_total=1') "unexpected watch output: $($watch.Output)"

  $defaultSync = Run-Qbrain @("sync", $Notes, "--once", "--brain", "n13-cli-smoke")
  Require ($defaultSync.ExitCode -eq 0) "default-source sync failed"
  Require ($defaultSync.Output -match 'imported=2') "unexpected default sync output: $($defaultSync.Output)"

  $graph = Run-Qbrain @("graph", "a", "--depth", "2", "--json", "--brain", "n13-cli-smoke")
  Require ($graph.ExitCode -eq 0) "graph CLI failed"
  Require ($graph.Output -match '"slug"\s*:\s*"b"' -and $graph.Output -match '"direction"\s*:\s*"out"') "graph output did not contain outbound b: $($graph.Output)"

  $Log.Add("N13_CLI_SMOKE_OK sync_once=pass idempotent=pass watch_once=pass source=pass interval=pass graph=pass")
  $Log | Set-Content -LiteralPath $LogPath -Encoding utf8
  Write-Host "N13_CLI_SMOKE_OK"
  Write-Host "WROTE $LogPath"
} finally {
  $env:LOCALAPPDATA = $OriginalLocalAppData
  $env:QBRAIN_EMBED_MOCK = $OriginalEmbedMock
  $resolvedTemp = [System.IO.Path]::GetFullPath($TempRoot)
  $tempBase = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
  if ($resolvedTemp.StartsWith($tempBase, [System.StringComparison]::OrdinalIgnoreCase) -and
      (Split-Path -Leaf $resolvedTemp).StartsWith("qbrain_n13_cli_", [System.StringComparison]::Ordinal)) {
    Remove-Item -LiteralPath $resolvedTemp -Recurse -Force -ErrorAction SilentlyContinue
  }
}

exit 0
