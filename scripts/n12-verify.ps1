# N12 reproducible evidence harness. This produces runtime evidence only; it is
# not a substitute for the required Claude Code outcome hard audit.
[CmdletBinding()]
param(
  [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
if (Test-Path Variable:PSNativeCommandUseErrorActionPreference) {
  $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $Root "scripts\build-tests-cl.ps1"
$Tests = Join-Path $Root "build\cl\qbrain_tests.exe"
$EvidenceDir = Join-Path $Root "docs\nodes\n12-evidence"
$ReportPath = Join-Path $EvidenceDir "VERIFY-REPORT.md"
$BuildLogPath = Join-Path $EvidenceDir "BUILD-OUTPUT.txt"
$TestLogPath = Join-Path $EvidenceDir "TEST-OUTPUT.txt"
$VcVars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"

New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null

function Require([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw $Message }
}

function First-MatchingLine([string]$Path, [string]$Pattern) {
  $match = Select-String -Path $Path -Pattern $Pattern | Select-Object -First 1
  if ($null -eq $match) { return "" }
  return $match.Line.Trim()
}

function File-Hash([string]$Path) {
  return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Test-OutputLine([string[]]$Lines, [string]$Prefix) {
  return $Lines | Where-Object { $_.StartsWith($Prefix, [System.StringComparison]::Ordinal) } |
      Select-Object -First 1
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
  return [pscustomobject]@{ Lines = @($lines | Where-Object { $_ -ne '' }); ExitCode = $process.ExitCode }
}

# The approved N12 plan requires the completed N1-N11 audit chain as a
# precondition. N2.5 is included because it is an actual dependency node in the
# repository's ordered history.
$DependencyNodes = @("N1", "N2", "N2.5", "N3", "N4", "N5", "N6", "N7", "N8", "N9", "N10", "N11")
$DependencyEvidence = @()
foreach ($Node in $DependencyNodes) {
  $plan = Join-Path $Root "docs\nodes\$Node-PLAN.md"
  $planAudit = Join-Path $Root "docs\nodes\$Node-PLAN-AUDIT.md"
  $hardAudit = Join-Path $Root "docs\nodes\$Node-HARD-AUDIT.md"
  Require (Test-Path -LiteralPath $plan) "missing dependency plan: $plan"
  Require (Test-Path -LiteralPath $planAudit) "missing dependency plan audit: $planAudit"
  Require (Test-Path -LiteralPath $hardAudit) "missing dependency outcome audit: $hardAudit"

  $status = First-MatchingLine $plan '(?i)status'
  $planVerdict = First-MatchingLine $planAudit '(?i)verdict'
  $hardVerdict = First-MatchingLine $hardAudit '(?i)verdict'
  Require ($status -match '(?i)\bdone\b') "$Node plan is not done: $status"
  Require ($planVerdict -match '(?i)\bPASS\b' -and $planVerdict -notmatch '(?i)\bFAIL\b') `
      "$Node plan audit is not PASS: $planVerdict"
  Require ($hardVerdict -match '(?i)\bPASS\b' -and $hardVerdict -notmatch '(?i)\bFAIL\b') `
      "$Node outcome audit is not PASS: $hardVerdict"

  $DependencyEvidence += [pscustomobject]@{
    Node = $Node
    Status = "done"
    PlanAudit = "PASS"
    OutcomeAudit = "PASS"
    PlanAuditSha256 = File-Hash $planAudit
    OutcomeAuditSha256 = File-Hash $hardAudit
  }
}

Require (Test-Path -LiteralPath $VcVars) "missing MSVC environment script: $VcVars"
$clCommand = "call `"$VcVars`" x64 >nul && echo target_arch=x64 && cl 2>&1"
$ClInfo = @(& cmd.exe /d /c $clCommand | ForEach-Object { $_.ToString() })
Require ($ClInfo.Count -gt 0) "failed to capture cl.exe version"
$ClVersion = ($ClInfo -join " ").Trim()

$BuildScriptText = Get-Content -Raw -LiteralPath $BuildScript
Require ($BuildScriptText -match '/std:c\+\+20') "canonical test build does not select /std:c++20"
Require ($BuildScriptText -match 'vcvars[^\r\n]*x64') "canonical test build does not select x64"

if (-not $SkipBuild) {
  $BuildResult = Invoke-CapturedProcess "powershell.exe" `
      "-NoProfile -ExecutionPolicy Bypass -File `"$BuildScript`""
  $BuildLines = $BuildResult.Lines
  $BuildExitCode = $BuildResult.ExitCode
  $BuildLines | Set-Content -LiteralPath $BuildLogPath -Encoding utf8
  Require ($BuildExitCode -eq 0) "canonical MSVC build/test command failed with exit code $BuildExitCode"
} else {
  Require (Test-Path -LiteralPath $Tests) "-SkipBuild requested but test binary is missing: $Tests"
  if (-not (Test-Path -LiteralPath $BuildLogPath)) {
    @( "Build intentionally skipped by explicit -SkipBuild." ) |
        Set-Content -LiteralPath $BuildLogPath -Encoding utf8
  }
}

Require (Test-Path -LiteralPath $Tests) "missing test binary after build: $Tests"
$Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$TestResult = Invoke-CapturedProcess $Tests ""
$TestLines = $TestResult.Lines
$TestExitCode = $TestResult.ExitCode
$Stopwatch.Stop()
$TestLines | Set-Content -LiteralPath $TestLogPath -Encoding utf8

$PassLines = @($TestLines | Where-Object { $_ -match '^\[PASS\]\s+' })
$FailLines = @($TestLines | Where-Object { $_ -match '^\[FAIL\]\s+' })
Require ($TestExitCode -eq 0) "qbrain_tests.exe failed with exit code $TestExitCode"
Require ($FailLines.Count -eq 0) "qbrain_tests.exe emitted failing tests"
Require ($PassLines.Count -ge 18) "registered test count regressed below 18: $($PassLines.Count)"

$SilentLine = Test-OutputLine $TestLines '[INFO] silent_provider_elapsed_ms='
$AuditLine = Test-OutputLine $TestLines '[INFO] rerank_audit_sample='
$ConcurrentLine = Test-OutputLine $TestLines '[INFO] minions_concurrent_claim '
$MigrationHashLine = Test-OutputLine $TestLines '[INFO] migration_v6_snapshot_sha256='
$MigrationLine = Test-OutputLine $TestLines '[INFO] migration_v6 populated_v5='
$DryHashLine = Test-OutputLine $TestLines '[INFO] dream_dry_run_snapshot_sha256='
$PurgeHashLine = Test-OutputLine $TestLines '[INFO] dream_purge_snapshot_before_sha256='
$DenyHashLine = Test-OutputLine $TestLines '[INFO] mcp_write_deny_snapshot_sha256='

foreach ($RequiredLine in @($SilentLine, $AuditLine, $ConcurrentLine, $MigrationHashLine,
                             $MigrationLine, $DryHashLine, $PurgeHashLine, $DenyHashLine)) {
  Require (-not [string]::IsNullOrWhiteSpace($RequiredLine)) "missing an N12 runtime evidence marker"
}

$SilentMs = [int64]($SilentLine -replace '^\[INFO\] silent_provider_elapsed_ms=', '')
Require ($SilentMs -lt 10000) "silent provider fallback exceeded 10 seconds: ${SilentMs}ms"
Require ($ConcurrentLine -match 'loser=(no_job|sqlite_busy)') `
    "concurrent claim loser did not have a documented result: $ConcurrentLine"
Require ($MigrationLine -match 'populated_v5=preserved' -and
         $MigrationLine -match 'idempotent=noop' -and
         $MigrationLine -match 'rollback_after_first_ddl=clean' -and
         $MigrationLine -match 'rollback_after_marker=clean' -and
         $MigrationLine -match 'migrated_job_fence=pass') "incomplete migration evidence: $MigrationLine"
Require ($DenyHashLine -match 'ops=submit_job,cancel_job,run_dream unchanged=pass') `
    "incomplete MCP deny evidence: $DenyHashLine"

$AuditJsonText = $AuditLine.Substring('[INFO] rerank_audit_sample='.Length)
$AuditJson = $AuditJsonText | ConvertFrom-Json
$AuditNames = @($AuditJson.PSObject.Properties.Name | Sort-Object)
$ExpectedAuditNames = @('doc_count', 'failure_reason', 'fallback_taken', 'query_hash', 'timestamp')
Require (($AuditNames -join ',') -eq ($ExpectedAuditNames -join ',')) `
    "rerank audit sample contains unexpected fields: $($AuditNames -join ',')"
Require ($AuditJson.fallback_taken -eq $true) "rerank audit sample does not record fallback"
Require ($AuditJson.failure_reason -eq 'transport_timeout') `
    "silent-provider audit reason is not transport_timeout"
Require ($AuditJson.query_hash -match '^[0-9a-f]{64}$') "rerank query hash is not a SHA-256 hex string"
Require ($AuditJsonText -notmatch 'DUMMY_API_KEY_SENTINEL|SENSITIVE_QUERY_SENTINEL|SENSITIVE_RESPONSE_BODY_SENTINEL') `
    "rerank audit sample leaked a test secret or payload"

$SourceFiles = @(
  'include/qbrain/ai/chat.hpp',
  'include/qbrain/cycle/dream.hpp',
  'include/qbrain/jobs/minions.hpp',
  'include/qbrain/search/rerank.hpp',
  'src/qbrain/ai/chat.cpp',
  'src/qbrain/cycle/dream.cpp',
  'src/qbrain/jobs/minions.cpp',
  'src/qbrain/search/rerank.cpp',
  'src/qbrain/storage/migrate.cpp',
  'tests/test_rerank.cpp',
  'tests/test_minions.cpp',
  'tests/test_migration_v6.cpp',
  'tests/test_n12_dream.cpp',
  'scripts/build-tests-cl.ps1',
  'scripts/n12-verify.ps1'
)
$SourceHashes = foreach ($Relative in $SourceFiles) {
  $absolute = Join-Path $Root $Relative
  Require (Test-Path -LiteralPath $absolute) "missing N12 deliverable: $Relative"
  [pscustomobject]@{ Path = $Relative.Replace('\', '/'); Sha256 = File-Hash $absolute }
}

try {
  $Os = Get-CimInstance Win32_OperatingSystem
  $OsText = "$($Os.Caption) $($Os.Version) build $($Os.BuildNumber)"
} catch {
  $OsText = [System.Environment]::OSVersion.VersionString
}

$Report = [System.Collections.Generic.List[string]]::new()
$Report.Add('# N12 Runtime Verification Report')
$Report.Add('')
$Report.Add('This report is runtime evidence only. It is not a plan audit or outcome hard-audit verdict.')
$Report.Add('')
$Report.Add("- Generated: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))")
$Report.Add("- OS: $OsText")
$Report.Add("- Process architecture: $([System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture)")
$Report.Add("- MSVC: $ClVersion")
$Report.Add('- Target: x64, native Windows')
$Report.Add('- Language mode: `/std:c++20`')
$Report.Add("- Test runtime: $($Stopwatch.ElapsedMilliseconds) ms")
$Report.Add("- Registered tests: $($PassLines.Count) PASS, 0 FAIL")
$Report.Add("- Test output SHA-256: $(File-Hash $TestLogPath)")
$Report.Add('')
$Report.Add('## Commands')
$Report.Add('')
$Report.Add('```powershell')
$Report.Add('powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1')
$Report.Add('build\cl\qbrain_tests.exe')
$Report.Add('```')
$Report.Add('')
$Report.Add('## N1-N11 Preconditions')
$Report.Add('')
$Report.Add('| Node | Status | Plan audit | Outcome audit | Plan audit SHA-256 | Outcome audit SHA-256 |')
$Report.Add('|---|---|---|---|---|---|')
foreach ($Item in $DependencyEvidence) {
  $Report.Add("| $($Item.Node) | $($Item.Status) | $($Item.PlanAudit) | $($Item.OutcomeAudit) | ``$($Item.PlanAuditSha256)`` | ``$($Item.OutcomeAuditSha256)`` |")
}
$Report.Add('')
$Report.Add('## Runtime Markers')
$Report.Add('')
$Report.Add('```text')
foreach ($Line in @($SilentLine, $AuditLine, $ConcurrentLine, $MigrationHashLine,
                     $MigrationLine, $DryHashLine, $PurgeHashLine, $DenyHashLine)) {
  $Report.Add($Line)
}
$Report.Add('```')
$Report.Add('')
$Report.Add('The rerank sample contains only the five approved fields. Its query hash uses a process-local random salt; no prompt, response body, exception body, or key is present. Audit rotation is tested at 1 MiB and preserves the prior generation in `.1`.')
$Report.Add('')
$Report.Add('The concurrent claim marker records the exact loser outcome observed in this run. The dream markers are stable SHA-256 hashes over every non-SQLite user table, not page/chunk/link counters.')
$Report.Add('')
$Report.Add('Schema v6 is additive. Downgrade and pre-v6 writer compatibility with a v6+ database are unsupported; restore a pre-migration backup instead of attempting schema rollback.')
$Report.Add('')
$Report.Add('## Deliverable Hashes')
$Report.Add('')
$Report.Add('| Path | SHA-256 |')
$Report.Add('|---|---|')
foreach ($Item in $SourceHashes) {
  $Report.Add("| ``$($Item.Path)`` | ``$($Item.Sha256)`` |")
}
$Report.Add('')
$Report.Add('## Result')
$Report.Add('')
$Report.Add('All scripted N12 runtime checks passed. A separate Claude Code outcome hard audit is still required before N12 can be marked done.')
$Report | Set-Content -LiteralPath $ReportPath -Encoding utf8

Write-Host "N12_VERIFY_OK tests=$($PassLines.Count) silent_ms=$SilentMs"
Write-Host "WROTE $ReportPath"
exit 0
