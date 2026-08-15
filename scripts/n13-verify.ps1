# N13 reproducible evidence harness. Runtime evidence does not replace the
# required Claude Code outcome hard audit.
[CmdletBinding()]
param([switch]$SkipBuild)

$ErrorActionPreference = "Stop"
if (Test-Path Variable:PSNativeCommandUseErrorActionPreference) {
  $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $Root "scripts\build-tests-cl.ps1"
$Tests = Join-Path $Root "build\cl\qbrain_tests.exe"
$CliSmokeScript = Join-Path $Root "scripts\n13-cli-smoke.ps1"
$EvidenceDir = Join-Path $Root "docs\nodes\n13-evidence"
$ReportPath = Join-Path $EvidenceDir "VERIFY-REPORT.md"
$BuildLogPath = Join-Path $EvidenceDir "BUILD-OUTPUT.txt"
$TestLogPath = Join-Path $EvidenceDir "TEST-OUTPUT.txt"
$CliLogPath = Join-Path $EvidenceDir "CLI-SMOKE-OUTPUT.txt"
$VcVars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$ExpectedTests = 21

New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null

function Require([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw $Message }
}

function File-Hash([string]$Path) {
  (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function First-Line([string]$Path, [string]$Pattern) {
  $match = Select-String -LiteralPath $Path -Pattern $Pattern | Select-Object -First 1
  if ($null -eq $match) { return "" }
  $match.Line.Trim()
}

function Invoke-CapturedProcess([string]$FilePath, [string]$Arguments) {
  $start = [System.Diagnostics.ProcessStartInfo]::new()
  $start.FileName = $FilePath
  $start.Arguments = $Arguments
  $start.UseShellExecute = $false
  $start.CreateNoWindow = $true
  $start.RedirectStandardOutput = $true
  $start.RedirectStandardError = $true
  $process = [System.Diagnostics.Process]::new()
  $process.StartInfo = $start
  [void]$process.Start()
  $stdoutTask = $process.StandardOutput.ReadToEndAsync()
  $stderrTask = $process.StandardError.ReadToEndAsync()
  $process.WaitForExit()
  $lines = @()
  if ($stdoutTask.Result) { $lines += $stdoutTask.Result -split '\r?\n' }
  if ($stderrTask.Result) { $lines += $stderrTask.Result -split '\r?\n' }
  [pscustomobject]@{
    Lines = @($lines | Where-Object { $_ -ne '' })
    ExitCode = $process.ExitCode
  }
}

function Output-Line([string[]]$Lines, [string]$Prefix) {
  $Lines | Where-Object {
    $_.StartsWith($Prefix, [System.StringComparison]::Ordinal)
  } | Select-Object -First 1
}

# N13 depends on the completed N1-N12 audit chain. N2.5 is an explicit
# source-id dependency in the ordered node history.
$DependencyNodes = @("N1", "N2", "N2.5", "N3", "N4", "N5", "N6", "N7", "N8", "N9", "N10", "N11", "N12")
$DependencyEvidence = @()
foreach ($Node in $DependencyNodes) {
  $plan = Join-Path $Root "docs\nodes\$Node-PLAN.md"
  $planAudit = Join-Path $Root "docs\nodes\$Node-PLAN-AUDIT.md"
  $hardAudit = Join-Path $Root "docs\nodes\$Node-HARD-AUDIT.md"
  Require (Test-Path -LiteralPath $plan) "missing dependency plan: $plan"
  Require (Test-Path -LiteralPath $planAudit) "missing dependency plan audit: $planAudit"
  Require (Test-Path -LiteralPath $hardAudit) "missing dependency outcome audit: $hardAudit"
  $status = First-Line $plan '(?i)^\*\*Status|^Status'
  $planVerdict = First-Line $planAudit '(?i)^\*\*VERDICT|^VERDICT'
  $hardVerdict = First-Line $hardAudit '(?i)^\*\*VERDICT|^VERDICT'
  Require ($status -match '(?i)\bdone\b') "$Node plan is not done: $status"
  Require ($planVerdict -match '(?i)\bPASS\b' -and $planVerdict -notmatch '(?i)\bFAIL\b') "$Node plan audit is not PASS: $planVerdict"
  Require ($hardVerdict -match '(?i)\bPASS\b' -and $hardVerdict -notmatch '(?i)\bFAIL\b') "$Node outcome audit is not PASS: $hardVerdict"
  $DependencyEvidence += [pscustomobject]@{
    Node = $Node; Status = "done"; PlanAudit = "PASS"; OutcomeAudit = "PASS"
    PlanAuditSha256 = File-Hash $planAudit; OutcomeAuditSha256 = File-Hash $hardAudit
  }
}

$N13Plan = Join-Path $Root "docs\nodes\N13-PLAN.md"
$N13PlanAudit = Join-Path $Root "docs\nodes\N13-PLAN-AUDIT.md"
Require (Test-Path -LiteralPath $N13Plan) "missing N13 plan"
Require (Test-Path -LiteralPath $N13PlanAudit) "missing N13 plan audit"
$N13Status = First-Line $N13Plan '(?i)^\*\*Status|^Status'
$N13PlanVerdict = First-Line $N13PlanAudit '(?i)^\*\*VERDICT|^VERDICT'
Require ($N13Status -match '(?i)\bapproved\b') "N13 plan is not approved: $N13Status"
Require ($N13PlanVerdict -match '(?i)\bPASS\b' -and $N13PlanVerdict -notmatch '(?i)\bFAIL\b') "N13 plan audit is not PASS: $N13PlanVerdict"

Require (Test-Path -LiteralPath $VcVars) "missing MSVC environment script: $VcVars"
$clCommand = "call `"$VcVars`" x64 >nul && echo target_arch=x64 && cl 2>&1"
$ClInfo = @(& cmd.exe /d /c $clCommand | ForEach-Object { $_.ToString() })
Require ($ClInfo.Count -gt 0) "failed to capture cl.exe version"
$ClVersion = ($ClInfo -join " ").Trim()
$BuildScriptText = Get-Content -Raw -LiteralPath $BuildScript
Require ($BuildScriptText -match '/std:c\+\+20') "canonical test build does not select /std:c++20"
Require ($BuildScriptText -match 'vcvars[^\r\n]*x64') "canonical test build does not select x64"

if (-not $SkipBuild) {
  $BuildResult = Invoke-CapturedProcess "powershell.exe" "-NoProfile -ExecutionPolicy Bypass -File `"$BuildScript`""
  $BuildLines = $BuildResult.Lines
  $BuildExitCode = $BuildResult.ExitCode
  $BuildLines | Set-Content -LiteralPath $BuildLogPath -Encoding utf8
  Require ($BuildExitCode -eq 0) "canonical MSVC build/test failed: $BuildExitCode"
} else {
  Require (Test-Path -LiteralPath $BuildLogPath) "-SkipBuild requires a captured canonical build log"
  $BuildLines = @(Get-Content -LiteralPath $BuildLogPath)
  Require (@($BuildLines | Where-Object { $_ -match 'BUILD_OK' }).Count -gt 0) "captured build log does not contain BUILD_OK"
  Require (@($BuildLines | Where-Object { $_ -match 'TESTS_BUILD_OK' }).Count -gt 0) "captured build log does not contain TESTS_BUILD_OK"
  $BuildExitCode = 0
}

Require (Test-Path -LiteralPath $Tests) "missing test binary after build: $Tests"
$Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$TestResult = Invoke-CapturedProcess $Tests ""
$Stopwatch.Stop()
$TestLines = $TestResult.Lines
$TestExitCode = $TestResult.ExitCode
$TestLines | Set-Content -LiteralPath $TestLogPath -Encoding utf8
$PassLines = @($TestLines | Where-Object { $_ -match '^\[PASS\]\s+' })
$FailLines = @($TestLines | Where-Object { $_ -match '^\[FAIL\]\s+' })
Require ($TestExitCode -eq 0) "qbrain_tests.exe failed: $TestExitCode"
Require ($FailLines.Count -eq 0) "qbrain_tests.exe emitted failing tests"
Require ($PassLines.Count -eq $ExpectedTests) "expected exactly $ExpectedTests tests, got $($PassLines.Count)"

$LiveLine = Output-Line $TestLines '[INFO] n13_live_sync '
$SourceLine = Output-Line $TestLines '[INFO] n13_source_cleanup '
$GraphLine = Output-Line $TestLines '[INFO] n13_graph '
$McpLine = Output-Line $TestLines '[INFO] n13_mcp_deny_snapshot_sha256='
foreach ($line in @($LiveLine, $SourceLine, $GraphLine, $McpLine)) {
  Require (-not [string]::IsNullOrWhiteSpace($line)) "missing an N13 runtime evidence marker"
}
Require ($LiveLine -match 'first_pages=2' -and $LiveLine -match 'second_skipped=2' -and $LiveLine -match 'changed_pages=1' -and $LiveLine -match 'watch_once=1' -and $LiveLine -match 'source_isolation=pass') "incomplete live-sync marker: $LiveLine"
Require ($SourceLine -match 'rollback_unchanged=pass' -and $SourceLine -match 'force_cleanup=pass') "incomplete source marker: $SourceLine"
Require ($GraphLine -match 'source_isolation=pass' -and $GraphLine -match 'cycle_nodes=') "incomplete graph marker: $GraphLine"
Require ($McpLine -match 'n13_mcp_deny_snapshot_sha256=[0-9a-f]{64}' -and $McpLine -match 'unchanged=pass' -and $McpLine -match 'allow_write=pass' -and $McpLine -match 'read_ops=pass') "incomplete MCP marker: $McpLine"

$CliResult = Invoke-CapturedProcess "powershell.exe" "-NoProfile -ExecutionPolicy Bypass -File `"$CliSmokeScript`""
Require ($CliResult.ExitCode -eq 0) "N13 CLI smoke failed: $($CliResult.ExitCode)"
Require (Test-Path -LiteralPath $CliLogPath) "N13 CLI smoke did not write its evidence log"
$CliLines = @(Get-Content -LiteralPath $CliLogPath)
$CliLine = Output-Line $CliLines 'N13_CLI_SMOKE_OK '
Require (-not [string]::IsNullOrWhiteSpace($CliLine)) "missing N13 CLI runtime marker"
Require ($CliLine -match 'sync_once=pass' -and $CliLine -match 'idempotent=pass' -and $CliLine -match 'watch_once=pass' -and $CliLine -match 'source=pass' -and $CliLine -match 'interval=pass' -and $CliLine -match 'graph=pass') "incomplete CLI marker: $CliLine"

$SourceFiles = @(
  'include/qbrain/core/brain.hpp', 'include/qbrain/graph/traverse.hpp', 'include/qbrain/ingest/import.hpp', 'include/qbrain/service/live_sync.hpp',
  'src/qbrain/core/brain.cpp', 'src/qbrain/graph/traverse.cpp', 'src/qbrain/ingest/import.cpp', 'src/qbrain/ops/handlers.cpp', 'src/qbrain/service/live_sync.cpp',
  'tests/test_n13.cpp', 'tests/test_main.cpp', 'CMakeLists.txt', 'scripts/build-tests-cl.ps1', 'scripts/n13-cli-smoke.ps1', 'scripts/n13-verify.ps1'
)
$SourceHashes = foreach ($Relative in $SourceFiles) {
  $absolute = Join-Path $Root $Relative
  Require (Test-Path -LiteralPath $absolute) "missing N13 deliverable: $Relative"
  [pscustomobject]@{ Path = $Relative.Replace('\', '/'); Sha256 = File-Hash $absolute }
}
try {
  $Os = Get-CimInstance Win32_OperatingSystem
  $OsText = "$($Os.Caption) $($Os.Version) build $($Os.BuildNumber)"
} catch { $OsText = [System.Environment]::OSVersion.VersionString }

$Report = [System.Collections.Generic.List[string]]::new()
$Report.Add('# N13 Runtime Verification Report'); $Report.Add('')
$Report.Add('This report is runtime evidence only. It is not a plan audit or outcome hard-audit verdict.'); $Report.Add('')
$Report.Add("- Generated: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))")
$Report.Add("- OS: $OsText")
$Report.Add("- Process architecture: $([System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture)")
$Report.Add("- MSVC: $ClVersion")
$Report.Add('- Target: x64, native Windows'); $Report.Add('- Language mode: `/std:c++20`')
$Report.Add("- Canonical build exit code: $BuildExitCode"); $Report.Add("- Test runtime: $($Stopwatch.ElapsedMilliseconds) ms")
$Report.Add("- Registered tests: $($PassLines.Count) PASS, 0 FAIL")
$Report.Add("- Build output SHA-256: $(File-Hash $BuildLogPath)"); $Report.Add("- Test output SHA-256: $(File-Hash $TestLogPath)"); $Report.Add("- CLI smoke output SHA-256: $(File-Hash $CliLogPath)"); $Report.Add('')
$Report.Add('## Commands'); $Report.Add(''); $Report.Add('```powershell')
$Report.Add('powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1')
$Report.Add('build\cl\qbrain_tests.exe')
$Report.Add('powershell -NoProfile -ExecutionPolicy Bypass -File scripts/n13-verify.ps1 -SkipBuild')
$Report.Add('powershell -NoProfile -ExecutionPolicy Bypass -File scripts/n13-cli-smoke.ps1')
$Report.Add('```'); $Report.Add('')
$Report.Add('## N1-N12 Preconditions'); $Report.Add('')
$Report.Add('| Node | Status | Plan audit | Outcome audit | Plan audit SHA-256 | Outcome audit SHA-256 |'); $Report.Add('|---|---|---|---|---|---|')
foreach ($Item in $DependencyEvidence) {
  $Report.Add("| $($Item.Node) | $($Item.Status) | $($Item.PlanAudit) | $($Item.OutcomeAudit) | ``$($Item.PlanAuditSha256)`` | ``$($Item.OutcomeAuditSha256)`` |")
}
$Report.Add(''); $Report.Add('## Runtime Markers'); $Report.Add(''); $Report.Add('```text')
foreach ($line in @($LiveLine, $SourceLine, $GraphLine, $McpLine, $CliLine)) { $Report.Add($line) }
$Report.Add('```'); $Report.Add('')
$Report.Add('The test matrix covers live-sync idempotence/change/watch, brain/source isolation, source status/removal and rollback, bounded bidirectional BFS, retry/fact state transitions, and remote write denial with a full snapshot hash.')
$Report.Add(''); $Report.Add('No provider/model/baseURL/API-key/reasoning/context/compression configuration is changed by N13.'); $Report.Add('')
$Report.Add('## Deliverable Hashes'); $Report.Add(''); $Report.Add('| Path | SHA-256 |'); $Report.Add('|---|---|')
foreach ($Item in $SourceHashes) { $Report.Add("| ``$($Item.Path)`` | ``$($Item.Sha256)`` |") }
$Report.Add(''); $Report.Add('## Result'); $Report.Add('')
$Report.Add('All scripted N13 runtime checks passed. A separate Claude Code outcome hard audit is still required before N13 can be marked done.')
$Report | Set-Content -LiteralPath $ReportPath -Encoding utf8
Write-Host "N13_VERIFY_OK tests=$($PassLines.Count) build_exit=$BuildExitCode"
Write-Host "WROTE $ReportPath"
exit 0
