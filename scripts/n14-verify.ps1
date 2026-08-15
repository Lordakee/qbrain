# N14 runtime verification. This report is factual evidence only; it is not a
# Claude Code plan audit or outcome hard-audit verdict.
[CmdletBinding()]
param([switch]$SkipBuild)

$ErrorActionPreference = "Stop"
if (Test-Path Variable:PSNativeCommandUseErrorActionPreference) {
  $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $Root "scripts\build-tests-cl.ps1"
$Tests = Join-Path $Root "build\cl\qbrain_tests.exe"
$Qbrain = Join-Path $Root "build\cl\qbrain.exe"
$EvidenceDir = Join-Path $Root "docs\nodes\n14-evidence"
$ReportPath = Join-Path $EvidenceDir "VERIFY-REPORT.md"
$BuildLogPath = Join-Path $EvidenceDir "BUILD-OUTPUT.txt"
$ManifestEvidencePath = Join-Path $EvidenceDir "BUILD-MANIFEST.txt"
$TestLogPath = Join-Path $EvidenceDir "TEST-OUTPUT.txt"
$CliLogPath = Join-Path $EvidenceDir "CLI-SMOKE-OUTPUT.txt"
$VcVars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$ExpectedTests = 25

New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null

function Require([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw $Message }
}

function File-Hash([string]$Path) {
  (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-BuildClosureFiles {
  $compiled = Get-ChildItem (Join-Path $Root 'src'),(Join-Path $Root 'include'),(Join-Path $Root 'tests') -Recurse -File |
    Where-Object { $_.Extension -in '.cpp','.hpp' }
  $buildInputs = Get-Item -LiteralPath @(
    (Join-Path $Root 'CMakeLists.txt'),
    (Join-Path $Root 'scripts\build-cl.ps1'),
    (Join-Path $Root 'scripts\build-tests-cl.ps1'),
    (Join-Path $Root 'scripts\n14-verify.ps1'),
    (Join-Path $Root 'scripts\n15-verify.ps1'),
    (Join-Path $Root 'scripts\n16-verify.ps1'),
    (Join-Path $Root 'scripts\n18-verify.ps1'),
    (Join-Path $Root 'scripts\wave3-verify-common.ps1'),
    (Join-Path $Root 'third_party\nlohmann\json.hpp'),
    (Join-Path $Root 'third_party\sqlite\sqlite-amalgamation-3460100\sqlite3.c'),
    (Join-Path $Root 'third_party\sqlite\sqlite-amalgamation-3460100\sqlite3.h')
  )
  @($compiled) + @($buildInputs) | Sort-Object FullName -Unique
}

function Relative-Path([string]$Path) {
  ([IO.Path]::GetFullPath($Path)).Substring($Root.Length + 1).Replace('\','/')
}

function Write-BuildManifest([string]$Path, [string]$ProductionLog, [string]$TestLog) {
  $lines = @(
    "generated_utc=$((Get-Date).ToUniversalTime().ToString('o'))",
    "production_log_sha256=$(File-Hash $ProductionLog)",
    "test_log_sha256=$(File-Hash $TestLog)"
  )
  foreach ($file in @(Get-BuildClosureFiles)) {
    $relative = Relative-Path $file.FullName
    $lines += "FILE $relative $(File-Hash $file.FullName)"
  }
  foreach ($artifact in @($Qbrain, $Tests)) {
    Require (Test-Path -LiteralPath $artifact) "missing build artifact for manifest: $artifact"
    $lines += "ARTIFACT $(Relative-Path $artifact) $(File-Hash $artifact)"
  }
  $lines | Set-Content -LiteralPath $Path -Encoding utf8
}

function Validate-BuildManifest([string]$Path, [string]$ProductionLog, [string]$TestLog) {
  Require (Test-Path -LiteralPath $Path) "missing current build manifest: $Path"
  $lines = @(Get-Content -LiteralPath $Path)
  $productionHash = ($lines | Where-Object { $_.StartsWith('production_log_sha256=') } | Select-Object -First 1) -replace '^production_log_sha256=', ''
  $testHash = ($lines | Where-Object { $_.StartsWith('test_log_sha256=') } | Select-Object -First 1) -replace '^test_log_sha256=', ''
  Require ($productionHash -eq (File-Hash $ProductionLog)) 'production build log does not match manifest'
  Require ($testHash -eq (File-Hash $TestLog)) 'test build log does not match manifest'
  $entries = @($lines | Where-Object { $_.StartsWith('FILE ') })
  $expectedFiles = @(Get-BuildClosureFiles)
  Require ($entries.Count -eq $expectedFiles.Count) 'build manifest file closure differs from the current worktree'
  $entryHashes = @{}
  foreach ($entry in $entries) {
    $parts = $entry.Split(' ', 3)
    Require ($parts.Count -eq 3 -and -not $entryHashes.ContainsKey($parts[1])) "malformed or duplicate manifest entry: $entry"
    $entryHashes[$parts[1]] = $parts[2]
  }
  foreach ($file in $expectedFiles) {
    $relative = Relative-Path $file.FullName
    Require ($entryHashes.ContainsKey($relative)) "current build input is absent from manifest: $relative"
    Require ($entryHashes[$relative] -eq (File-Hash $file.FullName)) "manifest build input changed: $relative"
  }
  $artifactEntries = @($lines | Where-Object { $_.StartsWith('ARTIFACT ') })
  Require ($artifactEntries.Count -eq 2) 'build manifest must contain exactly two binary artifacts'
  $artifactHashes = @{}
  foreach ($entry in $artifactEntries) {
    $parts = $entry.Split(' ', 3)
    Require ($parts.Count -eq 3 -and -not $artifactHashes.ContainsKey($parts[1])) "malformed or duplicate artifact entry: $entry"
    $artifactHashes[$parts[1]] = $parts[2]
  }
  foreach ($artifact in @($Qbrain, $Tests)) {
    $relative = Relative-Path $artifact
    Require (Test-Path -LiteralPath $artifact) "manifest artifact missing: $relative"
    Require ($artifactHashes.ContainsKey($relative)) "current build artifact is absent from manifest: $relative"
    Require ($artifactHashes[$relative] -eq (File-Hash $artifact)) "manifest artifact changed: $relative"
  }
  $lines | Set-Content -LiteralPath $ManifestEvidencePath -Encoding utf8
}

function Invoke-CapturedProcess(
  [string]$FilePath,
  [string]$Arguments,
  [ValidateRange(1, 3600)][int]$TimeoutSeconds = 1800
) {
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
  $stdout = $process.StandardOutput.ReadToEndAsync()
  $stderr = $process.StandardError.ReadToEndAsync()
  if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    try { $process.Kill() } catch {}
    [void]$process.WaitForExit(5000)
    throw "process timed out after $TimeoutSeconds seconds: $FilePath $Arguments"
  }
  $process.WaitForExit()
  $stdoutText = $stdout.Result
  $stderrText = $stderr.Result
  $lines = @()
  if ($stdoutText) { $lines += $stdoutText -split '\r?\n' }
  if ($stderrText) { $lines += $stderrText -split '\r?\n' }
  [pscustomobject]@{
    Lines = @($lines | Where-Object { $_ -ne '' })
    StdOut = $stdoutText
    StdErr = $stderrText
    ExitCode = $process.ExitCode
  }
}

function First-Line([string]$Path, [string]$Pattern) {
  $match = Select-String -LiteralPath $Path -Pattern $Pattern | Select-Object -First 1
  if ($null -eq $match) { return "" }
  $match.Line.Trim()
}

function Run-Cli([string[]]$Arguments) {
  $captured = Invoke-CapturedProcess $Qbrain ($Arguments -join ' ') 30
  [pscustomobject]@{
    Output = $captured.StdOut.Trim()
    Error = $captured.StdErr.Trim()
    ExitCode = $captured.ExitCode
  }
}

function Parse-CliJson([string]$Label, [object]$Result) {
  Require (-not [string]::IsNullOrWhiteSpace($Result.Output)) "$Label returned empty stdout"
  Require ([string]::IsNullOrWhiteSpace($Result.Error)) "$Label wrote to stderr: $($Result.Error)"
  $trimmed = $Result.Output.Trim()
  Require ($trimmed.StartsWith('{') -and $trimmed.EndsWith('}')) "$Label did not return one JSON object"
  try {
    return $trimmed | ConvertFrom-Json -ErrorAction Stop
  } catch {
    throw "$Label returned invalid or multiple JSON values: $($_.Exception.Message)"
  }
}

function Has-Property([object]$Value, [string]$Name) {
  $null -ne $Value.PSObject.Properties[$Name]
}

$DependencyEvidence = @()
foreach ($Node in @("N1", "N2", "N2.5", "N3", "N4", "N5", "N6", "N7", "N8", "N9", "N10", "N11", "N12", "N13")) {
  $plan = Join-Path $Root "docs\nodes\$Node-PLAN.md"
  $planAudit = Join-Path $Root "docs\nodes\$Node-PLAN-AUDIT.md"
  $hardAudit = Join-Path $Root "docs\nodes\$Node-HARD-AUDIT.md"
  Require (Test-Path -LiteralPath $plan) "missing dependency plan: $Node"
  Require (Test-Path -LiteralPath $planAudit) "missing dependency plan audit: $Node"
  Require (Test-Path -LiteralPath $hardAudit) "missing dependency outcome audit: $Node"
  $status = First-Line $plan '(?i)^\*\*Status|^Status'
  $planVerdict = First-Line $planAudit '(?i)^\*\*VERDICT|^VERDICT'
  $hardVerdict = First-Line $hardAudit '(?i)^\*\*VERDICT|^VERDICT'
  Require ($status -match '(?i)\bdone\b') "$Node is not done: $status"
  Require ($planVerdict -match '(?i)\bPASS\b' -and $planVerdict -notmatch '(?i)\bFAIL\b') "$Node plan audit is not PASS"
  Require ($hardVerdict -match '(?i)\bPASS\b' -and $hardVerdict -notmatch '(?i)\bFAIL\b') "$Node outcome audit is not PASS"
  Require ((Get-Content -Raw -LiteralPath $planAudit) -match '(?i)Auditor[^\r\n]*Claude Code') "$Node plan audit is not Claude Code"
  Require ((Get-Content -Raw -LiteralPath $hardAudit) -match '(?i)Auditor[^\r\n]*Claude Code') "$Node outcome audit is not Claude Code"
  $DependencyEvidence += [pscustomobject]@{ Node=$Node; PlanAudit="PASS"; OutcomeAudit="PASS"; PlanHash=(File-Hash $planAudit); OutcomeHash=(File-Hash $hardAudit) }
}

$PlanPath = Join-Path $Root "docs\nodes\N14-PLAN.md"
$PlanAuditPath = Join-Path $Root "docs\nodes\N14-PLAN-AUDIT.md"
$PlanStatus = First-Line $PlanPath '(?i)^\*\*Status|^Status'
$PlanVerdict = First-Line $PlanAuditPath '(?i)^\*\*VERDICT|^VERDICT'
Require ($PlanStatus -match '(?i)\bapproved\b') "N14 plan is not approved: $PlanStatus"
Require ($PlanVerdict -match '(?i)\bPASS\b' -and $PlanVerdict -notmatch '(?i)\bFAIL\b') "N14 plan audit is not PASS"
Require ((Get-Content -Raw -LiteralPath $PlanAuditPath) -match '(?i)Auditor[^\r\n]*Claude Code') "N14 plan audit is not Claude Code"
Require (Test-Path -LiteralPath $VcVars) "missing MSVC environment script"
$clInfo = @(& cmd.exe /d /c "call `"$VcVars`" x64 >nul && echo target_arch=x64 && cl 2>&1" | ForEach-Object { $_.ToString() })
Require ($clInfo.Count -gt 0) "cannot capture cl.exe version"
$ClVersion = ($clInfo -join " ").Trim()
$BuildText = Get-Content -Raw -LiteralPath $BuildScript
Require ($BuildText -match '/std:c\+\+20' -and $BuildText -match 'vcvars.*x64') "build script is not native C++20 x64"

if (-not $SkipBuild) {
  $build = Invoke-CapturedProcess "powershell.exe" "-NoProfile -ExecutionPolicy Bypass -File `"$BuildScript`""
  $build.Lines | Set-Content -LiteralPath $BuildLogPath -Encoding utf8
  $BuildExitCode = $build.ExitCode
  Require ($BuildExitCode -eq 0) "canonical build/test command failed: $BuildExitCode"
  Write-BuildManifest $ManifestEvidencePath $BuildLogPath $BuildLogPath
} else {
  $productionLog = Join-Path $Root "build\wave3-final-production.log"
  $testBuildLog = Join-Path $Root "build\wave3-final-tests.log"
  Require (Test-Path -LiteralPath $productionLog) "missing captured full production build log"
  Require (Test-Path -LiteralPath $testBuildLog) "missing captured final test build log"
  Require ((Get-Content $productionLog -Raw) -match 'BUILD_OK') "production build log lacks BUILD_OK"
  Require ((Get-Content $testBuildLog -Raw) -match 'TESTS_BUILD_OK') "test build log lacks TESTS_BUILD_OK"
  Validate-BuildManifest (Join-Path $Root 'build\wave3-final-build-manifest.txt') $productionLog $testBuildLog
  @("=== production: scripts/build-cl.ps1 ===") + @(Get-Content $productionLog) +
    @("=== tests: scripts/build-tests-cl.ps1 -SkipProductionBuild ===") + @(Get-Content $testBuildLog) |
    Set-Content -LiteralPath $BuildLogPath -Encoding utf8
  $BuildExitCode = 0
}

Require (Test-Path -LiteralPath $Tests) "missing qbrain_tests.exe"
$testResult = Invoke-CapturedProcess $Tests ''
$testLines = $testResult.Lines
$TestExitCode = $testResult.ExitCode
$testLines | Set-Content -LiteralPath $TestLogPath -Encoding utf8
$passLines = @($testLines | Where-Object { $_ -match '^\[PASS\]\s+' })
$failLines = @($testLines | Where-Object { $_ -match '^\[FAIL\]\s+' })
Require ($TestExitCode -eq 0) "qbrain_tests.exe failed: $TestExitCode"
Require ($failLines.Count -eq 0 -and $passLines.Count -eq $ExpectedTests) "expected $ExpectedTests all-PASS tests"
$Marker = $testLines | Where-Object { $_.StartsWith('[INFO] n14 ', [StringComparison]::Ordinal) } | Select-Object -First 1
Require (-not [string]::IsNullOrWhiteSpace($Marker)) "missing N14 runtime marker"
Require ($Marker -match 'job_fence=pass' -and $Marker -match 'progress_redaction=pass' -and $Marker -match 'status_snapshot=pass' -and $Marker -match 'snapshot_schema=pass' -and $Marker -match 'snapshot_matrix=pass') "incomplete N14 job marker"
Require ($Marker -match 'mcp_rpc=pass' -and $Marker -match 'page_id_exact_dedup=pass' -and $Marker -match 'remediation_idempotent=pass' -and $Marker -match 'damaged_status=database_error') "incomplete N14 remediation marker"
Require ($Marker -match 'state_matrix=pass' -and $Marker -match 'progress_matrix=pass' -and $Marker -match 'selected_brain=pass') "incomplete N14 state/progress/status matrix marker"
Require ($Marker -match 'remediation_lease_matrix=pass' -and $Marker -match 'remediation_embed_matrix=pass' -and $Marker -match 'allowed_remote_writes=pass') "incomplete N14 remediation/write matrix marker"
Require ($Marker -match 'job_matrix_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'status_matrix_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'remediation_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'remediation_matrix_rollback_sha256=[0-9a-f]{64}') "missing N14 matrix snapshot hashes"
Require ($Marker -match 'mcp_deny_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'rollback_snapshot_sha256=[0-9a-f]{64}') "missing N14 denial/rollback snapshot hashes"
Require ($Marker -match 'concurrent_claim_winners=1' -and $Marker -match 'remediation_embed_delta=5') "incorrect N14 observed concurrency/remediation deltas"
Require ($Marker -match 'concurrent_pending=[01]') "invalid N14 concurrency marker"

Require (Test-Path -LiteralPath $Qbrain) "missing qbrain.exe for CLI smoke"
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("qbrain_n14_cli_" + [guid]::NewGuid().ToString('N'))
$oldLocalAppData = $env:LOCALAPPDATA
$cliLines = [System.Collections.Generic.List[string]]::new()
try {
  New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
  $env:LOCALAPPDATA = $tempRoot
  $doctorBefore = Run-Cli @('doctor', '--json')
  $remediate = Run-Cli @('doctor', '--remediate', '--json')
  $doctorAfter = Run-Cli @('doctor', '--json')

  $doctorBeforeJson = Parse-CliJson 'doctor before remediation' $doctorBefore
  $remediateJson = Parse-CliJson 'doctor remediation' $remediate
  $doctorAfterJson = Parse-CliJson 'doctor after remediation' $doctorAfter

  Require ($doctorBefore.ExitCode -eq 0 -and $doctorBeforeJson.ok -eq $true) "N14 CLI pre-remediation doctor failed"
  Require ($remediate.ExitCode -eq 0 -and $remediateJson.ok -eq $true) "N14 CLI remediation failed"
  Require ($doctorAfter.ExitCode -eq 0 -and $doctorAfterJson.ok -eq $true) "N14 CLI post-remediation doctor failed"
  Require (-not (Has-Property $doctorBeforeJson 'remediation') -and -not (Has-Property $doctorBeforeJson 'health')) "doctor --json contract changed"
  Require (-not (Has-Property $doctorAfterJson 'remediation') -and -not (Has-Property $doctorAfterJson 'health')) "post-remediation doctor --json contract changed"
  Require ((Has-Property $remediateJson 'remediation') -and (Has-Property $remediateJson 'health')) "remediation JSON envelope is incomplete"
  Require ($remediateJson.remediation.default_source -eq $true) "remediation did not report the canonical default source"
  Require ($remediateJson.remediation.reclaimed -eq 0) "fresh CLI sandbox unexpectedly reclaimed jobs"
  Require ($remediateJson.remediation.embed_jobs_enqueued -eq 0) "fresh CLI sandbox unexpectedly enqueued embed jobs"
  Require ($remediateJson.health.ok -eq $true -and $remediateJson.health.overall -eq 'OK') "remediation envelope lacks healthy post-remediation status"
  Require ($remediateJson.health.schema_version -eq $doctorAfterJson.schema_version) "envelope health schema does not match the post-remediation doctor"
  Require ($doctorBefore.Output -ceq $doctorAfter.Output) "doctor JSON was not stable across idempotent remediation"
  Require (-not (Test-Path -LiteralPath (Join-Path $tempRoot 'Qbrain\config.json'))) "CLI smoke unexpectedly persisted application configuration"

  $cliLines.Add('command[1]=qbrain doctor --json')
  $cliLines.Add($doctorBefore.Output)
  $cliLines.Add("command[1]_exit=$($doctorBefore.ExitCode)")
  $cliLines.Add('command[2]=qbrain doctor --remediate --json')
  $cliLines.Add($remediate.Output)
  $cliLines.Add("command[2]_exit=$($remediate.ExitCode)")
  $cliLines.Add('command[3]=qbrain doctor --json')
  $cliLines.Add($doctorAfter.Output)
  $cliLines.Add("command[3]_exit=$($doctorAfter.ExitCode)")
  $CliMarker = 'N14_CLI_SMOKE_OK commands=3 json_parse=pass remediation_envelope=pass post_health=pass timeout=30s isolated_localappdata=pass config_unchanged=pass'
  $cliLines.Add($CliMarker)
} finally {
  $env:LOCALAPPDATA = $oldLocalAppData
  if (([IO.Path]::GetFullPath($tempRoot)).StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()), [StringComparison]::OrdinalIgnoreCase)) {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
  }
}
$cliLines | Set-Content -LiteralPath $CliLogPath -Encoding utf8

$Deliverables = @(
  'include/qbrain/jobs/minions.hpp','src/qbrain/jobs/minions.cpp','include/qbrain/core/brain.hpp',
  'src/qbrain/core/brain.cpp','src/qbrain/ops/handlers.cpp','tests/test_n14.cpp',
  'tests/wave3_test_support.hpp','tests/test_main.cpp','CMakeLists.txt','scripts/build-tests-cl.ps1',
  'scripts/n14-verify.ps1','docs/nodes/N14-PLAN.md','docs/nodes/N14-PLAN-AUDIT.md'
)
$Hashes = foreach ($relative in $Deliverables) {
  $absolute = Join-Path $Root $relative
  Require (Test-Path -LiteralPath $absolute) "missing N14 deliverable: $relative"
  [pscustomobject]@{ Path=$relative.Replace('\','/'); Hash=(File-Hash $absolute) }
}
$os = try { $x=Get-CimInstance Win32_OperatingSystem; "$($x.Caption) $($x.Version) build $($x.BuildNumber)" } catch { [Environment]::OSVersion.VersionString }
$report = [System.Collections.Generic.List[string]]::new()
$report.Add('# N14 Runtime Verification Report'); $report.Add('')
$report.Add('This is runtime evidence only. It is not a Claude Code plan audit or outcome hard-audit verdict.'); $report.Add('')
$report.Add("- Generated: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))")
$report.Add("- OS: $os"); $report.Add("- Process architecture: $([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture)")
$report.Add("- MSVC: $ClVersion"); $report.Add('- Target: native Windows x64'); $report.Add('- Language mode: `/std:c++20`')
$report.Add("- Canonical build command exit code: $BuildExitCode"); $report.Add("- Test command exit code: $TestExitCode")
$report.Add("- Registered tests: $($passLines.Count) PASS, 0 FAIL"); $report.Add("- Build output SHA-256: $(File-Hash $BuildLogPath)"); $report.Add("- Build manifest SHA-256: $(File-Hash $ManifestEvidencePath)"); $report.Add("- Test output SHA-256: $(File-Hash $TestLogPath)"); $report.Add("- CLI output SHA-256: $(File-Hash $CliLogPath)"); $report.Add('')
$report.Add('## Commands'); $report.Add(''); $report.Add('```powershell')
$report.Add('powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1')
$report.Add('powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1 -SkipProductionBuild')
$report.Add('build\cl\qbrain_tests.exe'); $report.Add('powershell -NoProfile -ExecutionPolicy Bypass -File scripts/n14-verify.ps1 -SkipBuild')
$report.Add('```'); $report.Add('')
$report.Add('## N1-N13 Preconditions'); $report.Add(''); $report.Add('| Node | Plan audit | Outcome audit | Plan SHA-256 | Outcome SHA-256 |'); $report.Add('|---|---|---|---|---|')
foreach ($item in $DependencyEvidence) { $report.Add("| $($item.Node) | PASS | PASS | ``$($item.PlanHash)`` | ``$($item.OutcomeHash)`` |") }
$report.Add(''); $report.Add('## Runtime Markers'); $report.Add(''); $report.Add('```text'); $report.Add($Marker); $report.Add($CliMarker); $report.Add('```'); $report.Add('')
$report.Add('The marker records token-fence invalidation, bounded redaction, selected-brain snapshot behavior, transactional/idempotent remediation, concurrency deduplication, structured damaged-database failure, and full-snapshot MCP denial.'); $report.Add('')
$report.Add('No model/provider/base URL/API key, reasoning, context, or compression configuration was changed.'); $report.Add('')
$report.Add('## Deliverable Hashes'); $report.Add(''); $report.Add('| Path | SHA-256 |'); $report.Add('|---|---|')
foreach ($item in $Hashes) { $report.Add("| ``$($item.Path)`` | ``$($item.Hash)`` |") }
$report.Add(''); $report.Add('## Result'); $report.Add(''); $report.Add('All scripted N14 runtime checks passed. A separate Claude Code outcome hard audit is still required before N14 may be marked done.')
$report | Set-Content -LiteralPath $ReportPath -Encoding utf8
Write-Host "N14_VERIFY_OK tests=$($passLines.Count) build_exit=$BuildExitCode test_exit=$TestExitCode"
exit 0
