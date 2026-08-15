# Shared implementation for the four node-specific Wave 3 verification entry
# points. Each invocation writes only its own evidence directory and report.
[CmdletBinding()]
param(
  [ValidateSet('N14','N15','N16','N18')]
  [string]$Node,
  [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
if (Test-Path Variable:PSNativeCommandUseErrorActionPreference) {
  $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $Root 'scripts\build-tests-cl.ps1'
$Tests = Join-Path $Root 'build\cl\qbrain_tests.exe'
$Qbrain = Join-Path $Root 'build\cl\qbrain.exe'
$EvidenceDir = Join-Path $Root "docs\nodes\$($Node.ToLowerInvariant())-evidence"
$ReportPath = Join-Path $EvidenceDir 'VERIFY-REPORT.md'
$BuildLogPath = Join-Path $EvidenceDir 'BUILD-OUTPUT.txt'
$ManifestEvidencePath = Join-Path $EvidenceDir 'BUILD-MANIFEST.txt'
$TestLogPath = Join-Path $EvidenceDir 'TEST-OUTPUT.txt'
$CliLogPath = Join-Path $EvidenceDir 'CLI-SMOKE-OUTPUT.txt'
$VcVars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
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

function First-Line([string]$Path, [string]$Pattern) {
  $match = Select-String -LiteralPath $Path -Pattern $Pattern | Select-Object -First 1
  if ($null -eq $match) { return '' }
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
  $stdout = $process.StandardOutput.ReadToEndAsync()
  $stderr = $process.StandardError.ReadToEndAsync()
  $process.WaitForExit()
  $lines = @()
  if ($stdout.Result) { $lines += $stdout.Result -split '\r?\n' }
  if ($stderr.Result) { $lines += $stderr.Result -split '\r?\n' }
  [pscustomobject]@{ Lines=@($lines | Where-Object { $_ -ne '' }); ExitCode=$process.ExitCode }
}

function Run-Cli([string[]]$Arguments) {
  $output = @(& $Qbrain @Arguments 2>&1 | ForEach-Object { $_.ToString() })
  [pscustomobject]@{ Output=($output -join "`n"); ExitCode=$LASTEXITCODE }
}

switch ($Node) {
  'N14' {
    $MarkerPrefix = '[INFO] n14 '
    $NodePlanName = 'N14-PLAN.md'
    $NodePlanAuditName = 'N14-PLAN-AUDIT.md'
    $CliRemediate = $true
    $Deliverables = @(
      'include/qbrain/jobs/minions.hpp','src/qbrain/jobs/minions.cpp','include/qbrain/core/brain.hpp',
      'src/qbrain/core/brain.cpp','src/qbrain/ops/handlers.cpp','tests/test_n14.cpp',
      'tests/wave3_test_support.hpp','tests/test_main.cpp','CMakeLists.txt','scripts/build-tests-cl.ps1',
      'scripts/n14-verify.ps1','scripts/wave3-verify-common.ps1','docs/nodes/N14-PLAN.md','docs/nodes/N14-PLAN-AUDIT.md'
    )
  }
  'N15' {
    $MarkerPrefix = '[INFO] n15 '
    $NodePlanName = 'N15-PLAN.md'
    $NodePlanAuditName = 'N15-PLAN-AUDIT.md'
    $CliRemediate = $false
    $Deliverables = @(
      'include/qbrain/core/brain.hpp','src/qbrain/core/brain.cpp','src/qbrain/storage/migrate.cpp',
      'src/qbrain/ingest/import.cpp','src/qbrain/service/live_sync.cpp','src/qbrain/ops/handlers.cpp',
      'src/qbrain/util/paths.cpp','tests/test_n15.cpp','tests/wave3_test_support.hpp','tests/test_main.cpp',
      'CMakeLists.txt','scripts/build-tests-cl.ps1','scripts/n15-verify.ps1','scripts/wave3-verify-common.ps1',
      'docs/nodes/N15-PLAN.md','docs/nodes/N15-PLAN-AUDIT.md'
    )
  }
  'N16' {
    $MarkerPrefix = '[INFO] n16 '
    $NodePlanName = 'N16-PLAN.md'
    $NodePlanAuditName = 'N16-PLAN-AUDIT.md'
    $CliRemediate = $false
    $Deliverables = @(
      'include/qbrain/codeintel/scan.hpp','src/qbrain/codeintel/scan.cpp','include/qbrain/core/brain.hpp',
      'src/qbrain/core/brain.cpp','src/qbrain/ops/handlers.cpp','tests/test_n16.cpp',
      'tests/wave3_test_support.hpp','tests/test_main.cpp','CMakeLists.txt','scripts/build-tests-cl.ps1',
      'scripts/n16-verify.ps1','scripts/wave3-verify-common.ps1','docs/nodes/N16-PLAN.md','docs/nodes/N16-PLAN-AUDIT.md'
    )
  }
  'N18' {
    $MarkerPrefix = '[INFO] n18 '
    $NodePlanName = 'N18-PLAN.md'
    $NodePlanAuditName = 'N18-PLAN-AUDIT.md'
    $CliRemediate = $false
    $Deliverables = @(
      'include/qbrain/graph/analytics.hpp','src/qbrain/graph/analytics.cpp','src/qbrain/ops/handlers.cpp',
      'src/qbrain/mcp/server.cpp','src/qbrain/ops/registry.cpp','src/qbrain/storage/database.cpp',
      'tests/test_analytics.cpp','tests/test_n18.cpp','tests/wave3_test_support.hpp','tests/test_main.cpp',
      'CMakeLists.txt','scripts/build-tests-cl.ps1','scripts/n18-verify.ps1','scripts/wave3-verify-common.ps1',
      'docs/nodes/N18-PLAN.md','docs/nodes/N18-PLAN-AUDIT.md'
    )
  }
}

$DependencyEvidence = @()
foreach ($DependencyNode in @('N1','N2','N2.5','N3','N4','N5','N6','N7','N8','N9','N10','N11','N12','N13')) {
  $plan = Join-Path $Root "docs\nodes\$DependencyNode-PLAN.md"
  $planAudit = Join-Path $Root "docs\nodes\$DependencyNode-PLAN-AUDIT.md"
  $hardAudit = Join-Path $Root "docs\nodes\$DependencyNode-HARD-AUDIT.md"
  Require (Test-Path -LiteralPath $plan) "missing dependency plan: $DependencyNode"
  Require (Test-Path -LiteralPath $planAudit) "missing dependency plan audit: $DependencyNode"
  Require (Test-Path -LiteralPath $hardAudit) "missing dependency outcome audit: $DependencyNode"
  $status = First-Line $plan '(?i)^\*\*Status|^Status'
  $planVerdict = First-Line $planAudit '(?i)^\*\*VERDICT|^VERDICT'
  $hardVerdict = First-Line $hardAudit '(?i)^\*\*VERDICT|^VERDICT'
  Require ($status -match '(?i)\bdone\b') "$DependencyNode is not done"
  Require ($planVerdict -match '(?i)\bPASS\b' -and $planVerdict -notmatch '(?i)\bFAIL\b') "$DependencyNode plan audit is not PASS"
  Require ($hardVerdict -match '(?i)\bPASS\b' -and $hardVerdict -notmatch '(?i)\bFAIL\b') "$DependencyNode outcome audit is not PASS"
  Require ((Get-Content -Raw -LiteralPath $planAudit) -match '(?i)Auditor[^\r\n]*Claude Code') "$DependencyNode plan audit is not Claude Code"
  Require ((Get-Content -Raw -LiteralPath $hardAudit) -match '(?i)Auditor[^\r\n]*Claude Code') "$DependencyNode outcome audit is not Claude Code"
  $DependencyEvidence += [pscustomobject]@{ Node=$DependencyNode; PlanHash=(File-Hash $planAudit); OutcomeHash=(File-Hash $hardAudit) }
}

$PlanPath = Join-Path $Root "docs\nodes\$NodePlanName"
$PlanAuditPath = Join-Path $Root "docs\nodes\$NodePlanAuditName"
$PlanStatus = First-Line $PlanPath '(?i)^\*\*Status|^Status'
$PlanVerdict = First-Line $PlanAuditPath '(?i)^\*\*VERDICT|^VERDICT'
Require ($PlanStatus -match '(?i)\bapproved\b') "$Node plan is not approved: $PlanStatus"
Require ($PlanVerdict -match '(?i)\bPASS\b' -and $PlanVerdict -notmatch '(?i)\bFAIL\b') "$Node plan audit is not PASS"
Require ((Get-Content -Raw -LiteralPath $PlanAuditPath) -match '(?i)Auditor[^\r\n]*Claude Code') "$Node plan audit is not Claude Code"
Require (Test-Path -LiteralPath $VcVars) 'missing MSVC environment script'
$clInfo = @(& cmd.exe /d /c "call `"$VcVars`" x64 >nul && echo target_arch=x64 && cl 2>&1" | ForEach-Object { $_.ToString() })
Require ($clInfo.Count -gt 0) 'cannot capture cl.exe version'
$ClVersion = ($clInfo -join ' ').Trim()
$BuildText = Get-Content -Raw -LiteralPath $BuildScript
Require ($BuildText -match '/std:c\+\+20' -and $BuildText -match 'vcvars.*x64') 'build script is not native C++20 x64'

if (-not $SkipBuild) {
  $build = Invoke-CapturedProcess 'powershell.exe' "-NoProfile -ExecutionPolicy Bypass -File `"$BuildScript`""
  $build.Lines | Set-Content -LiteralPath $BuildLogPath -Encoding utf8
  $BuildExitCode = $build.ExitCode
  Require ($BuildExitCode -eq 0) "canonical build/test command failed: $BuildExitCode"
  Write-BuildManifest $ManifestEvidencePath $BuildLogPath $BuildLogPath
} else {
  $productionLog = Join-Path $Root 'build\wave3-final-production.log'
  $testBuildLog = Join-Path $Root 'build\wave3-final-tests.log'
  Require (Test-Path -LiteralPath $productionLog) 'missing captured full production build log'
  Require (Test-Path -LiteralPath $testBuildLog) 'missing captured final test build log'
  Require ((Get-Content $productionLog -Raw) -match 'BUILD_OK') 'production log lacks BUILD_OK'
  Require ((Get-Content $testBuildLog -Raw) -match 'TESTS_BUILD_OK') 'test build log lacks TESTS_BUILD_OK'
  Validate-BuildManifest (Join-Path $Root 'build\wave3-final-build-manifest.txt') $productionLog $testBuildLog
  @('=== production: scripts/build-cl.ps1 ===') + @(Get-Content $productionLog) +
    @('=== tests: scripts/build-tests-cl.ps1 -SkipProductionBuild ===') + @(Get-Content $testBuildLog) |
    Set-Content -LiteralPath $BuildLogPath -Encoding utf8
  $BuildExitCode = 0
}

Require (Test-Path -LiteralPath $Tests) 'missing qbrain_tests.exe'
$testResult = Invoke-CapturedProcess $Tests ''
$testLines = $testResult.Lines
$TestExitCode = $testResult.ExitCode
$testLines | Set-Content -LiteralPath $TestLogPath -Encoding utf8
$passLines = @($testLines | Where-Object { $_ -match '^\[PASS\]\s+' })
$failLines = @($testLines | Where-Object { $_ -match '^\[FAIL\]\s+' })
Require ($TestExitCode -eq 0) "qbrain_tests.exe failed: $TestExitCode"
Require ($failLines.Count -eq 0 -and $passLines.Count -eq $ExpectedTests) "expected $ExpectedTests all-PASS tests"
$Marker = $testLines | Where-Object { $_.StartsWith($MarkerPrefix, [StringComparison]::Ordinal) } | Select-Object -First 1
Require (-not [string]::IsNullOrWhiteSpace($Marker)) "missing $Node runtime marker"
$SnapshotCallLines = @()

switch ($Node) {
  'N14' {
    Require ($Marker -match 'job_fence=pass' -and $Marker -match 'progress_redaction=pass' -and $Marker -match 'status_snapshot=pass' -and $Marker -match 'snapshot_schema=pass' -and $Marker -match 'snapshot_matrix=pass') 'incomplete N14 marker'
    Require ($Marker -match 'mcp_rpc=pass' -and $Marker -match 'page_id_exact_dedup=pass' -and $Marker -match 'remediation_idempotent=pass' -and $Marker -match 'damaged_status=database_error') 'incomplete N14 security/remediation marker'
    Require ($Marker -match 'state_matrix=pass' -and $Marker -match 'progress_matrix=pass' -and $Marker -match 'selected_brain=pass') 'incomplete N14 state/progress/status matrix marker'
    Require ($Marker -match 'remediation_lease_matrix=pass' -and $Marker -match 'remediation_embed_matrix=pass' -and $Marker -match 'allowed_remote_writes=pass') 'incomplete N14 remediation/write matrix marker'
    Require ($Marker -match 'job_matrix_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'status_matrix_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'remediation_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'remediation_matrix_rollback_sha256=[0-9a-f]{64}') 'missing N14 matrix snapshot hashes'
    Require ($Marker -match 'mcp_deny_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'rollback_snapshot_sha256=[0-9a-f]{64}') 'missing N14 denial/rollback snapshot hashes'
    Require ($Marker -match 'concurrent_claim_winners=1' -and $Marker -match 'remediation_embed_delta=5') 'incorrect N14 observed concurrency/remediation deltas'
    Require ($Marker -match 'concurrent_pending=[01]') 'invalid N14 concurrency marker'
  }
  'N15' {
    Require ($Marker -match 'migration_matrix=pass' -and $Marker -match 'migration_v12=pass' -and $Marker -match 'migration_legacy_rows=2' -and $Marker -match 'migration_fk_cascade=pass') 'incomplete N15 migration matrix marker'
    Require ($Marker -match 'link_source_matrix=pass' -and $Marker -match 'link_source_ordering=pass' -and $Marker -match 'link_brain_isolation=pass') 'incomplete N15 link-source matrix marker'
    Require ($Marker -match 'retention_matrix=pass' -and $Marker -match 'retention_default=100' -and $Marker -match 'retention_team_max=1000' -and $Marker -match 'get_log_limit_matrix=pass') 'incomplete N15 retention/read-limit matrix marker'
    Require ($Marker -match 'payload_boundary_matrix=pass' -and $Marker -match 'event_boundary_bytes=64' -and $Marker -match 'path_boundary_bytes=4096' -and $Marker -match 'detail_boundary_bytes=65536' -and $Marker -match 'payload_rejected=8') 'incomplete N15 payload-boundary matrix marker'
    Require ($Marker -match 'source_validation_matrix=pass' -and $Marker -match 'source_read_rejected=8' -and $Marker -match 'mcp_type_rejection_matrix=pass' -and $Marker -match 'mcp_type_rejected=19') 'incomplete N15 source/MCP validation marker'
    Require ($Marker -match 'import_live_sync_matrix=pass' -and $Marker -match 'import_counter_json=pass' -and $Marker -match 'second_brain_isolation=pass') 'incomplete N15 import/live-sync matrix marker'
    Require ($Marker -match 'chronicle_boundary_matrix=pass' -and $Marker -match 'chronicle_limit_matrix=pass' -and $Marker -match 'chronicle_soft_delete=pass' -and $Marker -match 'chronicle_tie_ordering=pass') 'incomplete N15 chronicle behavior marker'
    Require ($Marker -match 'chronicle_invalid_day=11' -and $Marker -match 'chronicle_invalid_since=14') 'incorrect N15 chronicle rejection counts'
    Require ($Marker -match 'registry_metadata_matrix=pass' -and $Marker -match 'registry_operation_count=6' -and $Marker -match 'registry_schema=pass' -and $Marker -match 'registry_strict_arguments=pass' -and $Marker -match 'registry_unknown_fields_rejected=2') 'incomplete N15 registry/schema marker'
    Require ($Marker -match 'timeline_write_matrix=pass' -and $Marker -match 'timeline_provenance=pass' -and $Marker -match 'timeline_chunks=pass' -and $Marker -match 'timeline_embed_once=pass' -and $Marker -match 'remote_no_link_extraction=pass') 'incomplete N15 timeline write marker'
    Require ($Marker -match 'timeline_same_second_attempts=([1-9][0-9]*)') 'missing N15 same-second collision evidence'
    Require ($Marker -match 'remote_write_matrix=pass' -and $Marker -match 'remote_read_matrix=pass' -and $Marker -match 'remote_read_count=4') 'incomplete N15 remote authorization marker'
    Require ($Marker -match 'ambient_source_ignored=pass' -and $Marker -match 'ambient_operation_count=6' -and $Marker -match 'full_logical_snapshots=pass' -and $Marker -match 'localappdata_isolation=pass') 'incomplete N15 isolation/snapshot marker'
    Require ($Marker -match 'migration_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'migration_rollback_sha256=[0-9a-f]{64}' -and $Marker -match 'migration_cleanup_sha256=[0-9a-f]{64}') 'missing N15 migration hashes'
    Require ($Marker -match 'link_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'retention_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'payload_snapshot_sha256=[0-9a-f]{64}') 'missing N15 source/payload hashes'
    Require ($Marker -match 'import_primary_sha256=[0-9a-f]{64}' -and $Marker -match 'import_second_sha256=[0-9a-f]{64}' -and $Marker -match 'chronicle_snapshot_sha256=[0-9a-f]{64}') 'missing N15 import/chronicle hashes'
    Require ($Marker -match 'registry_metadata_sha256=[0-9a-f]{64}' -and $Marker -match 'timeline_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'remote_deny_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'remote_read_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'ambient_snapshot_sha256=[0-9a-f]{64}') 'missing N15 registry/security hashes'
  }
  'N16' {
    Require ($Marker -match 'snapshot_schema=pass' -and $Marker -match 'snapshot_matrix=pass' -and $Marker -match 'source_scope=pass' -and $Marker -match 'symbol_grammar=pass' -and $Marker -match 'deterministic=pass' -and $Marker -match 'mcp_rpc=pass') 'incomplete N16 marker'
    Require ($Marker -match 'definition_matrix=pass' -and $Marker -match 'reference_caller_matrix=pass' -and $Marker -match 'limit_matrix=pass' -and $Marker -match 'page_limit_matrix=pass' -and $Marker -match 'ordering_matrix=pass') 'incomplete N16 behavior matrix marker'
    Require ($Marker -match 'utf8_bounds=pass' -and $Marker -match 'registry_schema=pass' -and $Marker -match 'mcp_type_validation=pass' -and $Marker -match 'ambient_default=pass') 'incomplete N16 schema/serialization marker'
    Require ($Marker -match 'remote_authorization=pass' -and $Marker -match 'read_only=pass') 'incomplete N16 security marker'
    Require ($Marker -match 'selected_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'decoy_snapshot_sha256=[0-9a-f]{64}') 'missing N16 hashes'
    Require ($Marker -match 'snapshot_call_count=([1-9][0-9]*)') 'missing N16 per-call snapshot count'
    $SnapshotCallCount = [int]$Matches[1]
    $SnapshotCallLines = @($testLines | Where-Object { $_ -match '^\[INFO\] n16 snapshot_call=[1-9][0-9]* ' })
    Require ($SnapshotCallLines.Count -eq $SnapshotCallCount) 'N16 snapshot call count does not match the emitted evidence rows'
    foreach ($line in $SnapshotCallLines) {
      Require ($line -match 'selected_before_sha256=([0-9a-f]{64}) selected_after_sha256=([0-9a-f]{64}) decoy_before_sha256=([0-9a-f]{64}) decoy_after_sha256=([0-9a-f]{64})$') 'malformed N16 per-call snapshot hash row'
      $selectedBefore = $Matches[1]; $selectedAfter = $Matches[2]
      $decoyBefore = $Matches[3]; $decoyAfter = $Matches[4]
      Require ($selectedBefore -eq $selectedAfter -and $decoyBefore -eq $decoyAfter) 'N16 per-call snapshot hash changed'
    }
  }
  'N18' {
    Require ($Marker -match 'snapshot_schema=pass' -and $Marker -match 'snapshot_matrix=pass' -and $Marker -match 'source_scope=pass' -and $Marker -match 'deterministic=pass' -and $Marker -match 'utf8_bounds=pass' -and $Marker -match 'mcp_rpc=pass') 'incomplete N18 marker'
    Require ($Marker -match 'limit_source_matrix=pass' -and $Marker -match 'contradiction_rule_matrix=pass' -and $Marker -match 'anomaly_matrix=pass' -and $Marker -match 'expert_matrix=pass') 'incomplete N18 analytics matrix marker'
    Require ($Marker -match 'registry_mcp_snapshot_matrix=pass' -and $Marker -match 'mcp_type_validation=pass' -and $Marker -match 'nul_text=pass') 'incomplete N18 MCP/storage matrix marker'
    Require ($Marker -match 'remote_authorization=pass' -and $Marker -match 'read_only=pass') 'incomplete N18 security marker'
    Require ($Marker -match 'selected_snapshot_sha256=[0-9a-f]{64}' -and $Marker -match 'decoy_snapshot_sha256=[0-9a-f]{64}') 'missing N18 hashes'
    Require ($Marker -match 'text_json_equivalence=pass' -and $Marker -match 'per_call_snapshot_hashes=pass' -and $Marker -match 'snapshot_call_count=([1-9][0-9]*)') 'missing N18 equivalence/per-call snapshot evidence'
    $SnapshotCallCount = [int]$Matches[1]
    $SnapshotCallLines = @($testLines | Where-Object { $_ -match '^\[INFO\] n18 snapshot_call=[1-9][0-9]* ' })
    Require ($SnapshotCallLines.Count -eq $SnapshotCallCount) 'N18 snapshot call count does not match the emitted evidence rows'
    foreach ($line in $SnapshotCallLines) {
      Require ($line -match 'selected_before_sha256=([0-9a-f]{64}) selected_after_sha256=([0-9a-f]{64}) decoy_before_sha256=([0-9a-f]{64}) decoy_after_sha256=([0-9a-f]{64})$') 'malformed N18 per-call snapshot hash row'
      $selectedBefore = $Matches[1]; $selectedAfter = $Matches[2]
      $decoyBefore = $Matches[3]; $decoyAfter = $Matches[4]
      Require ($selectedBefore -eq $selectedAfter -and $decoyBefore -eq $decoyAfter) 'N18 per-call snapshot hash changed'
    }
  }
}

Require (Test-Path -LiteralPath $Qbrain) 'missing qbrain.exe for CLI smoke'
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("qbrain_$($Node.ToLowerInvariant())_cli_" + [guid]::NewGuid().ToString('N'))
$oldLocalAppData = $env:LOCALAPPDATA
$cliLines = [System.Collections.Generic.List[string]]::new()
try {
  New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
  $env:LOCALAPPDATA = $tempRoot
  $version = Run-Cli @('version')
  $doctor = Run-Cli @('doctor', '--json')
  $cliLines.Add("version=$($version.Output)"); $cliLines.Add("version_exit=$($version.ExitCode)")
  $cliLines.Add($doctor.Output); $cliLines.Add("doctor_exit=$($doctor.ExitCode)")
  Require ($version.ExitCode -eq 0 -and $version.Output -match 'Qbrain') "$Node CLI version failed"
  Require ($doctor.ExitCode -eq 0 -and $doctor.Output -match '"ok"\s*:\s*true') "$Node CLI doctor failed"
  if ($CliRemediate) {
    $remediate = Run-Cli @('doctor', '--remediate', '--json')
    $cliLines.Add($remediate.Output); $cliLines.Add("remediate_exit=$($remediate.ExitCode)")
    Require ($remediate.ExitCode -eq 0 -and $remediate.Output -match '"default_source"\s*:\s*true') 'N14 CLI remediation failed'
    $CliMarker = "${Node}_CLI_SMOKE_OK doctor=pass remediation=pass version=pass isolated_localappdata=pass"
  } else {
    $CliMarker = "${Node}_CLI_SMOKE_OK doctor=pass version=pass isolated_localappdata=pass"
  }
  $cliLines.Add($CliMarker)
} finally {
  $env:LOCALAPPDATA = $oldLocalAppData
  if (([IO.Path]::GetFullPath($tempRoot)).StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()), [StringComparison]::OrdinalIgnoreCase)) {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
  }
}
$cliLines | Set-Content -LiteralPath $CliLogPath -Encoding utf8

$Hashes = foreach ($relative in $Deliverables) {
  $absolute = Join-Path $Root $relative
  Require (Test-Path -LiteralPath $absolute) "missing $Node deliverable: $relative"
  [pscustomobject]@{ Path=$relative.Replace('\','/'); Hash=(File-Hash $absolute) }
}
$os = try { $x=Get-CimInstance Win32_OperatingSystem; "$($x.Caption) $($x.Version) build $($x.BuildNumber)" } catch { [Environment]::OSVersion.VersionString }
$report = [System.Collections.Generic.List[string]]::new()
$report.Add("# $Node Runtime Verification Report"); $report.Add('')
$report.Add('This is runtime evidence only. It is not a Claude Code plan audit or outcome hard-audit verdict.'); $report.Add('')
$report.Add("- Generated: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))")
$report.Add("- OS: $os"); $report.Add("- Process architecture: $([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture)")
$report.Add("- MSVC: $ClVersion"); $report.Add('- Target: native Windows x64'); $report.Add('- Language mode: `/std:c++20`')
$report.Add("- Canonical build command exit code: $BuildExitCode"); $report.Add("- Test command exit code: $TestExitCode")
$report.Add("- Registered tests: $($passLines.Count) PASS, 0 FAIL"); $report.Add("- Build output SHA-256: $(File-Hash $BuildLogPath)"); $report.Add("- Build manifest SHA-256: $(File-Hash $ManifestEvidencePath)"); $report.Add("- Test output SHA-256: $(File-Hash $TestLogPath)"); $report.Add("- CLI output SHA-256: $(File-Hash $CliLogPath)"); $report.Add('')
$report.Add('## Commands'); $report.Add(''); $report.Add('```powershell')
$report.Add('powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1')
$report.Add('powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1 -SkipProductionBuild')
$report.Add('build\cl\qbrain_tests.exe'); $report.Add("powershell -NoProfile -ExecutionPolicy Bypass -File scripts/$($Node.ToLowerInvariant())-verify.ps1 -SkipBuild")
$report.Add('```'); $report.Add('')
$report.Add('## N1-N13 Preconditions'); $report.Add(''); $report.Add('| Node | Plan audit | Outcome audit | Plan SHA-256 | Outcome SHA-256 |'); $report.Add('|---|---|---|---|---|')
foreach ($item in $DependencyEvidence) { $report.Add("| $($item.Node) | PASS | PASS | ``$($item.PlanHash)`` | ``$($item.OutcomeHash)`` |") }
$report.Add(''); $report.Add('## Runtime Markers'); $report.Add(''); $report.Add('```text'); $report.Add($Marker)
foreach ($line in $SnapshotCallLines) { $report.Add($line) }
$report.Add($CliMarker); $report.Add('```'); $report.Add('')
$report.Add("The $Node marker is emitted by the dedicated test and records the node-specific acceptance checks, full logical snapshot hashes, and MCP serialization path. The CLI marker uses a unique temporary LOCALAPPDATA and does not touch production data."); $report.Add('')
$report.Add('No model/provider/base URL/API key, reasoning, context, or compression configuration was changed.'); $report.Add('')
$report.Add('## Deliverable Hashes'); $report.Add(''); $report.Add('| Path | SHA-256 |'); $report.Add('|---|---|')
foreach ($item in $Hashes) { $report.Add("| ``$($item.Path)`` | ``$($item.Hash)`` |") }
$report.Add(''); $report.Add('## Result'); $report.Add(''); $report.Add("All scripted $Node runtime checks passed. A separate Claude Code outcome hard audit is still required before $Node may be marked done.")
$report | Set-Content -LiteralPath $ReportPath -Encoding utf8
Write-Host "$($Node.ToUpperInvariant())_VERIFY_OK tests=$($passLines.Count) build_exit=$BuildExitCode test_exit=$TestExitCode"
exit 0
