# N17 native runtime verification and evidence packaging.
# This script produces factual evidence only. It cannot issue a plan-audit or
# outcome hard-audit verdict.
[CmdletBinding()]
param(
  [switch]$SkipBuild,
  [ValidateRange(1, 1440)]
  [int]$MaxBuildEvidenceAgeMinutes = 240
)

$ErrorActionPreference = 'Stop'
if (Test-Path Variable:PSNativeCommandUseErrorActionPreference) {
  $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Split-Path -Parent $PSScriptRoot
$EvidenceDir = Join-Path $Root 'docs\nodes\n17-evidence'
$PlanPath = Join-Path $Root 'docs\nodes\N17-PLAN.md'
$PlanAuditPath = Join-Path $Root 'docs\nodes\N17-PLAN-AUDIT.md'
$ProductionBuildScript = Join-Path $Root 'scripts\build-cl.ps1'
$TestBuildScript = Join-Path $Root 'scripts\build-tests-cl.ps1'
$VerifierPath = Join-Path $Root 'scripts\n17-verify.ps1'
$Qbrain = Join-Path $Root 'build\cl\qbrain.exe'
$Tests = Join-Path $Root 'build\cl\qbrain_tests.exe'
$MigrationSourcePath = Join-Path $Root 'src\qbrain\storage\migrate.cpp'
$VcVars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'

$ProductionBuildLogPath = Join-Path $EvidenceDir 'PRODUCTION-BUILD-OUTPUT.txt'
$TestBuildLogPath = Join-Path $EvidenceDir 'TEST-BUILD-OUTPUT.txt'
$TestLogPath = Join-Path $EvidenceDir 'TEST-OUTPUT.txt'
$CliLogPath = Join-Path $EvidenceDir 'CLI-SMOKE-OUTPUT.txt'
$BuildManifestPath = Join-Path $EvidenceDir 'BUILD-MANIFEST.txt'
$ScopeManifestPath = Join-Path $EvidenceDir 'N17-SCOPE-MANIFEST.txt'
$RuntimeMarkersPath = Join-Path $EvidenceDir 'RUNTIME-MARKERS.txt'
$SchemaEvidencePath = Join-Path $EvidenceDir 'SCHEMA-EVIDENCE.txt'
$OperationSchemasPath = Join-Path $EvidenceDir 'OPERATION-SCHEMAS.json'
$ReportPath = Join-Path $EvidenceDir 'VERIFY-REPORT.md'
$EvidenceManifestPath = Join-Path $EvidenceDir 'EVIDENCE-MANIFEST.txt'

$ProductionBuildCommand = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-cl.ps1'
$TestBuildCommand = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1 -SkipProductionBuild'
$TestCommand = 'build\cl\qbrain_tests.exe'
$McpToolsListCommand = 'build\cl\qbrain.exe serve --brain n17-evidence (stdio NDJSON tools/list; writes disabled)'
$VerifierCommand = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/n17-verify.ps1'
$MinimumRegisteredTests = 26
$BuildEvidenceMode = if ($SkipBuild) { 'validated_fresh_evidence' } else { 'executed' }

New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null

trap {
  $failureMessage = if ($null -ne $_.Exception) { $_.Exception.Message } else { $_.ToString() }
  if ($failureMessage.Length -gt 1000) { $failureMessage = $failureMessage.Substring(0, 1000) }
  try {
    Remove-Item -LiteralPath $EvidenceManifestPath -Force -ErrorAction SilentlyContinue
    @(
      '# N17 Runtime Verification Report',
      '',
      'Verification did not complete. No runtime-evidence success claim or audit verdict is issued.',
      '',
      "- Generated UTC: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))",
      "- Error: $failureMessage"
    ) | Set-Content -LiteralPath $ReportPath -Encoding utf8
  } catch {}
  Write-Error $failureMessage -ErrorAction Continue
  exit 1
}

# Invalidate final artifacts from any earlier run before evaluating the first
# gate. Build logs and their manifest remain available only for -SkipBuild's
# hash/freshness validation.
foreach ($stalePath in @(
  $EvidenceManifestPath, $ScopeManifestPath, $RuntimeMarkersPath,
  $SchemaEvidencePath, $OperationSchemasPath, $CliLogPath, $TestLogPath
)) {
  Remove-Item -LiteralPath $stalePath -Force -ErrorAction SilentlyContinue
}
@(
  '# N17 Runtime Verification Report',
  '',
  'Verification is in progress or did not complete. No runtime-evidence success claim or audit verdict is issued.'
) | Set-Content -LiteralPath $ReportPath -Encoding utf8

function Require([bool]$Condition, [string]$Message) {
  if (-not $Condition) {
    throw "N17 verification failed: $Message"
  }
}

function File-Hash([string]$Path) {
  Require (Test-Path -LiteralPath $Path -PathType Leaf) "missing file to hash: $Path"
  (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Text-Hash([string]$Text) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
    (($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') }) -join '')
  } finally {
    $sha.Dispose()
  }
}

function Normalize-Lf([string]$Text) {
  [regex]::Replace($Text, "\r\n?", "`n")
}

function Relative-Path([string]$Path) {
  $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
  $pathFull = [IO.Path]::GetFullPath($Path)
  Require ($pathFull.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) "path is outside repository: $Path"
  $pathFull.Substring($rootFull.Length).Replace('\', '/')
}

function First-Line([string]$Path, [string]$Pattern) {
  $match = Select-String -LiteralPath $Path -Pattern $Pattern | Select-Object -First 1
  if ($null -eq $match) { return '' }
  $match.Line.Trim()
}

function Invoke-CapturedProcess(
  [string]$FilePath,
  [string]$Arguments,
  [ValidateRange(1, 7200)][int]$TimeoutSeconds = 1800,
  [AllowNull()][string]$StandardInputText = $null
) {
  $started = (Get-Date).ToUniversalTime()
  $start = [Diagnostics.ProcessStartInfo]::new()
  $start.FileName = $FilePath
  $start.Arguments = $Arguments
  $start.UseShellExecute = $false
  $start.CreateNoWindow = $true
  $start.RedirectStandardOutput = $true
  $start.RedirectStandardError = $true
  $start.RedirectStandardInput = $null -ne $StandardInputText

  $process = [Diagnostics.Process]::new()
  $process.StartInfo = $start
  [void]$process.Start()
  if ($null -ne $StandardInputText) {
    $process.StandardInput.Write($StandardInputText)
    $process.StandardInput.Close()
  }
  $stdoutTask = $process.StandardOutput.ReadToEndAsync()
  $stderrTask = $process.StandardError.ReadToEndAsync()
  $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
  if ($timedOut) {
    try { $process.Kill() } catch {}
    [void]$process.WaitForExit(5000)
  } else {
    $process.WaitForExit()
  }

  [pscustomobject]@{
    FilePath = $FilePath
    Arguments = $Arguments
    StartedUtc = $started
    FinishedUtc = (Get-Date).ToUniversalTime()
    StdOut = $stdoutTask.Result
    StdErr = $stderrTask.Result
    ExitCode = if ($timedOut) { -1 } else { $process.ExitCode }
    TimedOut = $timedOut
  }
}

function Write-ProcessLog([string]$Path, [string]$Command, [object]$Result) {
  $lines = [Collections.Generic.List[string]]::new()
  $lines.Add("command=$Command")
  $lines.Add("started_utc=$($Result.StartedUtc.ToString('o'))")
  $lines.Add('stdout_begin')
  if (-not [string]::IsNullOrEmpty($Result.StdOut)) {
    foreach ($line in (Normalize-Lf $Result.StdOut).Split("`n")) { $lines.Add($line) }
  }
  $lines.Add('stdout_end')
  $lines.Add('stderr_begin')
  if (-not [string]::IsNullOrEmpty($Result.StdErr)) {
    foreach ($line in (Normalize-Lf $Result.StdErr).Split("`n")) { $lines.Add($line) }
  }
  $lines.Add('stderr_end')
  $lines.Add("exit_code=$($Result.ExitCode)")
  $lines.Add("timed_out=$($Result.TimedOut.ToString().ToLowerInvariant())")
  $lines.Add("finished_utc=$($Result.FinishedUtc.ToString('o'))")
  $lines | Set-Content -LiteralPath $Path -Encoding utf8
}

function Process-Lines([object]$Result) {
  $lines = @()
  if (-not [string]::IsNullOrEmpty($Result.StdOut)) {
    $lines += (Normalize-Lf $Result.StdOut).Split("`n")
  }
  if (-not [string]::IsNullOrEmpty($Result.StdErr)) {
    $lines += (Normalize-Lf $Result.StdErr).Split("`n")
  }
  @($lines | Where-Object { $_ -ne '' })
}

function Get-BuildClosureFiles {
  $compiled = Get-ChildItem -LiteralPath (Join-Path $Root 'src'), (Join-Path $Root 'include'), (Join-Path $Root 'tests') -Recurse -File |
    Where-Object { $_.Extension -in '.cpp', '.hpp' }
  $inputs = Get-Item -LiteralPath @(
    (Join-Path $Root 'CMakeLists.txt'),
    $ProductionBuildScript,
    $TestBuildScript,
    $VerifierPath,
    (Join-Path $Root 'third_party\nlohmann\json.hpp'),
    (Join-Path $Root 'third_party\sqlite\sqlite-amalgamation-3460100\sqlite3.c'),
    (Join-Path $Root 'third_party\sqlite\sqlite-amalgamation-3460100\sqlite3.h')
  )
  @($compiled) + @($inputs) | Sort-Object FullName -Unique
}

function Get-FileState([IO.FileInfo[]]$Files) {
  $rows = foreach ($file in $Files) {
    [pscustomobject]@{
      Path = Relative-Path $file.FullName
      Hash = File-Hash $file.FullName
    }
  }
  @($rows | Sort-Object Path)
}

function State-Signature([object[]]$Rows) {
  Text-Hash ((@($Rows | ForEach-Object { "$($_.Path)`t$($_.Hash)" }) -join "`n") + "`n")
}

function Require-SameState([object[]]$Before, [object[]]$After, [string]$Label) {
  Require ($Before.Count -eq $After.Count) "$Label file count changed during verification"
  Require ((State-Signature $Before) -ceq (State-Signature $After)) "$Label changed during verification"
}

function Get-ProtectedModelConfigurationFiles {
  $files = @()
  foreach ($relative in @(
    'AGENTS.md',
    'include/qbrain/core/types.hpp',
    'include/qbrain/core/brain.hpp',
    'src/qbrain/core/brain.cpp',
    'src/qbrain/cli/commands.cpp'
  )) {
    $absolute = Join-Path $Root $relative
    if (Test-Path -LiteralPath $absolute -PathType Leaf) { $files += Get-Item -LiteralPath $absolute }
  }
  foreach ($relativeDirectory in @('include/qbrain/ai', 'src/qbrain/ai', '.codex', '.claude', '.opencode')) {
    $absoluteDirectory = Join-Path $Root $relativeDirectory
    if (Test-Path -LiteralPath $absoluteDirectory -PathType Container) {
      $files += Get-ChildItem -LiteralPath $absoluteDirectory -Recurse -File
    }
  }
  @($files | Sort-Object FullName -Unique)
}

function Invoke-GitRead([string[]]$Arguments) {
  $oldCount = $env:GIT_CONFIG_COUNT
  try {
    $env:GIT_CONFIG_COUNT = '0'
    $output = @(& git.exe -C $Root @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
  } finally {
    if ($null -eq $oldCount) {
      Remove-Item Env:GIT_CONFIG_COUNT -ErrorAction SilentlyContinue
    } else {
      $env:GIT_CONFIG_COUNT = $oldCount
    }
  }
  Require ($exitCode -eq 0) "git read command failed: git $($Arguments -join ' ')"
  @($output)
}

function Get-GitHead {
  $line = (Invoke-GitRead @('rev-parse', 'HEAD') | Select-Object -First 1).Trim()
  Require ($line -match '^[0-9a-fA-F]{40,64}$') 'cannot capture repository HEAD'
  $line.ToLowerInvariant()
}

function Get-GitHeadText([string]$RelativePath) {
  ((Invoke-GitRead @('show', "HEAD:$RelativePath")) -join "`n") + "`n"
}

function Get-V8MigrationBlock([string]$SourceText) {
  $normalized = Normalize-Lf $SourceText
  $pattern = '(?ms)^[ \t]*// v8: job_messages for N17\n[ \t]*if \(ver < 8\) \{\n.*?^[ \t]*ver = 8;\n[ \t]*\}'
  $matches = [regex]::Matches($normalized, $pattern)
  Require ($matches.Count -eq 1) 'expected exactly one v8 job_messages migration block'
  $matches[0].Value.TrimEnd()
}

function Parse-N17Marker([string[]]$Lines, [string]$Label) {
  $prefix = '[INFO] n17 '
  $markers = @($Lines | Where-Object { $_.StartsWith($prefix, [StringComparison]::Ordinal) })
  Require ($markers.Count -eq 1) "$Label must contain exactly one N17 marker"
  $marker = $markers[0]
  $values = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
  foreach ($token in $marker.Substring($prefix.Length).Split(@(' '), [StringSplitOptions]::RemoveEmptyEntries)) {
    $separator = $token.IndexOf('=')
    Require ($separator -gt 0 -and $separator -lt ($token.Length - 1)) "$Label contains malformed N17 marker token: $token"
    $key = $token.Substring(0, $separator)
    $value = $token.Substring($separator + 1)
    Require (-not $values.ContainsKey($key)) "$Label contains duplicate N17 marker key: $key"
    $values.Add($key, $value)
  }

  $passKeys = @(
    'strict_id_alias_matrix', 'replay_terminal_state_matrix',
    'sender_payload_utf8_json_boundaries', 'missing_vs_empty_list',
    'list_limit_matrix', 'migration_v7_v8_v12', 'migration_idempotence',
    'migration_rollback', 'damaged_integrity', 'no_foreign_key',
    'registry_tools_list', 'real_mcp', 'default_deny', 'allow_write',
    'selected_decoy_snapshots', 'concurrency'
  )
  foreach ($key in $passKeys) {
    Require ($values.ContainsKey($key) -and $values[$key] -ceq 'pass') "$Label is missing required marker $key=pass"
  }

  $exactValues = [ordered]@{
    strict_id_cases = '84'
    replay_state_cases = '9'
    sender_payload_cases = '26'
    list_limit_cases = '15'
    mcp_rejection_cases = '26'
  }
  foreach ($entry in $exactValues.GetEnumerator()) {
    Require ($values.ContainsKey($entry.Key) -and $values[$entry.Key] -ceq $entry.Value) "$Label has an unexpected $($entry.Key) marker"
  }

  foreach ($key in @('selected_snapshot_sha256', 'decoy_snapshot_sha256', 'migration_snapshot_sha256', 'rollback_snapshot_sha256')) {
    Require ($values.ContainsKey($key) -and $values[$key] -match '^[0-9a-f]{64}$') "$Label has an invalid $key marker"
  }
  Require ($values['selected_snapshot_sha256'] -cne $values['decoy_snapshot_sha256']) "$Label selected and decoy snapshot hashes must be distinct"

  foreach ($key in @('replay_race_successes', 'replay_race_busy', 'message_race_successes', 'message_race_busy')) {
    Require ($values.ContainsKey($key) -and $values[$key] -match '^[0-9]+$') "$Label has an invalid $key marker"
  }
  $replaySuccesses = [int]$values['replay_race_successes']
  $replayBusy = [int]$values['replay_race_busy']
  $messageSuccesses = [int]$values['message_race_successes']
  $messageBusy = [int]$values['message_race_busy']
  Require ($replaySuccesses -in 1, 2 -and $replayBusy -in 0, 1 -and ($replaySuccesses + $replayBusy) -eq 2) "$Label has an invalid replay concurrency outcome"
  Require ($messageSuccesses -in 1, 2 -and $messageBusy -in 0, 1 -and ($messageSuccesses + $messageBusy) -eq 2) "$Label has an invalid message concurrency outcome"

  $expectedKeyCount = $passKeys.Count + $exactValues.Count + 4 + 4
  Require ($values.Count -eq $expectedKeyCount) "$Label N17 marker key set changed: expected $expectedKeyCount, observed $($values.Count)"
  [pscustomobject]@{ Line = $marker; Values = $values }
}

function Validate-TestSuite([string[]]$Lines, [int]$ExitCode, [string]$Label) {
  $passLines = @($Lines | Where-Object { $_ -match '^\[PASS\]\s+[^\s]+\s*$' })
  $failLines = @($Lines | Where-Object { $_ -match '^\[FAIL\]\s+' })
  $resultLines = @($Lines | Where-Object { $_ -match '^\[(PASS|FAIL)\]\s+' })
  Require ($ExitCode -eq 0) "$Label exited with $ExitCode"
  Require ($failLines.Count -eq 0) "$Label contains $($failLines.Count) FAIL result(s)"
  Require ($passLines.Count -ge $MinimumRegisteredTests) "$Label contains only $($passLines.Count) PASS results; expected at least $MinimumRegisteredTests"
  Require ($resultLines.Count -eq $passLines.Count) "$Label contains malformed or non-PASS result lines"
  $names = @($passLines | ForEach-Object { ($_ -replace '^\[PASS\]\s+', '').Trim() })
  Require ((@($names | Sort-Object -Unique)).Count -eq $names.Count) "$Label contains duplicate test result names"
  Require ((@($names | Where-Object { $_ -ceq 'n17' })).Count -eq 1) "$Label must contain exactly one [PASS] n17"
  $marker = Parse-N17Marker $Lines $Label
  [pscustomobject]@{ PassCount = $passLines.Count; FailCount = 0; Marker = $marker }
}

function Get-ManifestValue([string[]]$Lines, [string]$Key) {
  $prefix = "$Key="
  $matches = @($Lines | Where-Object { $_.StartsWith($prefix, [StringComparison]::Ordinal) })
  Require ($matches.Count -eq 1) "build manifest must contain exactly one $Key field"
  $matches[0].Substring($prefix.Length)
}

function Write-BuildManifest([object[]]$BuildState, [int]$ProductionExitCode, [int]$TestBuildExitCode) {
  $lines = [Collections.Generic.List[string]]::new()
  $lines.Add('format_version=1')
  $lines.Add('node=N17')
  $lines.Add("generated_utc=$((Get-Date).ToUniversalTime().ToString('o'))")
  $lines.Add("production_command=$ProductionBuildCommand")
  $lines.Add("production_exit_code=$ProductionExitCode")
  $lines.Add("test_build_command=$TestBuildCommand")
  $lines.Add("test_build_exit_code=$TestBuildExitCode")
  $lines.Add("production_log_sha256=$(File-Hash $ProductionBuildLogPath)")
  $lines.Add("test_build_log_sha256=$(File-Hash $TestBuildLogPath)")
  $lines.Add("build_input_count=$($BuildState.Count)")
  $lines.Add("build_input_closure_sha256=$(State-Signature $BuildState)")
  foreach ($row in $BuildState) { $lines.Add("FILE`t$($row.Hash)`t$($row.Path)") }
  foreach ($artifact in @($Qbrain, $Tests)) {
    $lines.Add("ARTIFACT`t$(File-Hash $artifact)`t$(Relative-Path $artifact)")
  }
  $lines | Set-Content -LiteralPath $BuildManifestPath -Encoding utf8
}

function Validate-BuildManifest([object[]]$CurrentBuildState) {
  Require (Test-Path -LiteralPath $BuildManifestPath -PathType Leaf) 'SkipBuild requires an existing N17 build manifest'
  Require (Test-Path -LiteralPath $ProductionBuildLogPath -PathType Leaf) 'SkipBuild requires an existing production build log'
  Require (Test-Path -LiteralPath $TestBuildLogPath -PathType Leaf) 'SkipBuild requires an existing test build log'
  $lines = @(Get-Content -LiteralPath $BuildManifestPath)
  Require ((Get-ManifestValue $lines 'format_version') -ceq '1') 'unsupported build manifest version'
  Require ((Get-ManifestValue $lines 'node') -ceq 'N17') 'build manifest is not scoped to N17'
  Require ((Get-ManifestValue $lines 'production_command') -ceq $ProductionBuildCommand) 'production command in build manifest is not canonical'
  Require ((Get-ManifestValue $lines 'test_build_command') -ceq $TestBuildCommand) 'test build command in build manifest is not canonical'
  Require ((Get-ManifestValue $lines 'production_exit_code') -ceq '0') 'recorded production build did not exit zero'
  Require ((Get-ManifestValue $lines 'test_build_exit_code') -ceq '0') 'recorded test build did not exit zero'
  Require ((Get-ManifestValue $lines 'production_log_sha256') -ceq (File-Hash $ProductionBuildLogPath)) 'production build log hash does not match manifest'
  Require ((Get-ManifestValue $lines 'test_build_log_sha256') -ceq (File-Hash $TestBuildLogPath)) 'test build log hash does not match manifest'
  Require ((Get-ManifestValue $lines 'build_input_count') -ceq $CurrentBuildState.Count.ToString()) 'build input count does not match current worktree'
  Require ((Get-ManifestValue $lines 'build_input_closure_sha256') -ceq (State-Signature $CurrentBuildState)) 'build input closure does not match current worktree'

  $generatedText = Get-ManifestValue $lines 'generated_utc'
  $generated = [DateTimeOffset]::MinValue
  Require ([DateTimeOffset]::TryParse($generatedText, [ref]$generated)) 'build manifest has an invalid generation time'
  $age = [DateTimeOffset]::UtcNow - $generated.ToUniversalTime()
  Require ($age.TotalMinutes -ge -5 -and $age.TotalMinutes -le $MaxBuildEvidenceAgeMinutes) "build evidence is not fresh (age minutes: $([math]::Round($age.TotalMinutes, 1)))"

  $fileEntries = @($lines | Where-Object { $_.StartsWith("FILE`t", [StringComparison]::Ordinal) })
  Require ($fileEntries.Count -eq $CurrentBuildState.Count) 'build manifest file closure entry count changed'
  $entryMap = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
  foreach ($entry in $fileEntries) {
    $parts = $entry.Split("`t")
    Require ($parts.Count -eq 3 -and $parts[1] -match '^[0-9a-f]{64}$' -and -not $entryMap.ContainsKey($parts[2])) "malformed build manifest FILE entry: $entry"
    $entryMap.Add($parts[2], $parts[1])
  }
  foreach ($row in $CurrentBuildState) {
    Require ($entryMap.ContainsKey($row.Path) -and $entryMap[$row.Path] -ceq $row.Hash) "build manifest input is stale: $($row.Path)"
  }

  $artifactEntries = @($lines | Where-Object { $_.StartsWith("ARTIFACT`t", [StringComparison]::Ordinal) })
  Require ($artifactEntries.Count -eq 2) 'build manifest must contain exactly two executable artifacts'
  $artifactMap = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
  foreach ($entry in $artifactEntries) {
    $parts = $entry.Split("`t")
    Require ($parts.Count -eq 3 -and $parts[1] -match '^[0-9a-f]{64}$' -and -not $artifactMap.ContainsKey($parts[2])) "malformed build manifest ARTIFACT entry: $entry"
    $artifactMap.Add($parts[2], $parts[1])
  }
  foreach ($artifact in @($Qbrain, $Tests)) {
    $relative = Relative-Path $artifact
    Require (Test-Path -LiteralPath $artifact -PathType Leaf) "missing build artifact: $relative"
    Require ($artifactMap.ContainsKey($relative) -and $artifactMap[$relative] -ceq (File-Hash $artifact)) "build artifact is stale: $relative"
  }
}

function Require-NoSensitiveEvidence([string[]]$Paths) {
  $forbiddenN30Path = '(?i)(?:docs[/\\]nodes[/\\])?N30-[A-Za-z0-9_.*?\\/-]+'
  $secretPatterns = @(
    'N17_REMOTE_SENDER_SECRET',
    'N17_REMOTE_PAYLOAD_SECRET',
    '(?i)\bsk-[A-Za-z0-9_-]{16,}\b',
    '(?i)\bBearer\s+[A-Za-z0-9._~+/-]{12,}={0,2}\b'
  )
  foreach ($path in $Paths) {
    Require (Test-Path -LiteralPath $path -PathType Leaf) "missing generated evidence file: $path"
    $text = Get-Content -Raw -LiteralPath $path
    Require ($text -notmatch $forbiddenN30Path) "forbidden later-node path reference found in evidence: $(Relative-Path $path)"
    foreach ($pattern in $secretPatterns) {
      Require ($text -notmatch $pattern) "secret-like content found in evidence: $(Relative-Path $path)"
    }
  }
}

function Has-JsonProperty([object]$Value, [string]$Name) {
  $null -ne $Value.PSObject.Properties[$Name]
}

function Optional-FileFingerprint([string]$Path) {
  if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    return 'absent'
  }
  File-Hash $Path
}

function Require-ExactPropertyNames([object]$Value, [string[]]$Expected, [string]$Label) {
  $actual = @($Value.PSObject.Properties.Name | Sort-Object)
  $wanted = @($Expected | Sort-Object)
  Require ($actual.Count -eq $wanted.Count -and (($actual -join "`n") -ceq ($wanted -join "`n"))) "$Label property set is not exact"
}

function Require-JsonIntegerDefault([object]$Value, [int64]$Expected, [string]$Label) {
  $typeCode = if ($null -eq $Value) { [TypeCode]::Empty } else { [Type]::GetTypeCode($Value.GetType()) }
  $isInteger = @(
    [TypeCode]::SByte,
    [TypeCode]::Byte,
    [TypeCode]::Int16,
    [TypeCode]::UInt16,
    [TypeCode]::Int32,
    [TypeCode]::UInt32,
    [TypeCode]::Int64,
    [TypeCode]::UInt64
  ) -contains $typeCode
  Require ($isInteger -and [int64]$Value -eq $Expected) "$Label differs from the canonical integer default"
}

function Require-JobAliasSchema([object]$Schema, [string[]]$ExpectedProperties, [string]$Label) {
  Require ((Has-JsonProperty $Schema 'type') -and $Schema.type -ceq 'object') "$Label schema type is not object"
  Require ((Has-JsonProperty $Schema 'additionalProperties') -and $Schema.additionalProperties -eq $false) "$Label schema does not set additionalProperties=false"
  Require ((Has-JsonProperty $Schema 'properties') -and $null -ne $Schema.properties) "$Label schema has no properties object"
  Require-ExactPropertyNames $Schema.properties $ExpectedProperties "$Label schema"
  foreach ($name in @('job_id', 'id')) {
    $property = $Schema.properties.PSObject.Properties[$name].Value
    Require ($null -ne $property -and $property.type -ceq 'integer') "$Label $name type is not integer"
    Require ([int64]$property.minimum -eq 1 -and [int64]$property.maximum -eq [int64]::MaxValue) "$Label $name bounds are not 1..INT64_MAX"
  }
  Require ((Has-JsonProperty $Schema 'anyOf') -and @($Schema.anyOf).Count -eq 2) "$Label alias requirement is missing"
  $requiredAlternatives = @($Schema.anyOf | ForEach-Object {
    Require ((Has-JsonProperty $_ 'required') -and @($_.required).Count -eq 1) "$Label alias requirement is malformed"
    [string]$_.required[0]
  } | Sort-Object)
  Require (($requiredAlternatives -join ',') -ceq 'id,job_id') "$Label alias requirement must accept job_id or id"
}

function Require-IsolatedDefaultConfig([string]$Path) {
  Require (Test-Path -LiteralPath $Path -PathType Leaf) 'isolated application config path is missing'
  try {
    $config = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json -ErrorAction Stop
  } catch {
    throw "N17 verification failed: isolated application config is invalid JSON: $($_.Exception.Message)"
  }
  Require-ExactPropertyNames $config @('brain_id', 'embedding', 'chat', 'search') 'isolated application config'
  Require ($config.brain_id -is [string] -and -not [string]::IsNullOrWhiteSpace($config.brain_id)) 'isolated application config brain_id is not a non-empty string'
  Require-ExactPropertyNames $config.embedding @('provider', 'model', 'base_url', 'dimensions') 'isolated embedding config'
  Require ($config.embedding.provider -ceq 'openai') 'isolated embedding provider differs from the canonical default'
  Require ($config.embedding.model -ceq 'text-embedding-3-small') 'isolated embedding model differs from the canonical default'
  Require ($config.embedding.base_url -ceq 'https://api.openai.com/v1') 'isolated embedding base URL differs from the canonical default'
  Require-JsonIntegerDefault $config.embedding.dimensions 1536 'isolated embedding dimensions'
  Require-ExactPropertyNames $config.chat @('model', 'base_url') 'isolated chat config'
  Require ($config.chat.model -ceq 'gpt-4o-mini') 'isolated chat model differs from the canonical default'
  Require ($config.chat.base_url -ceq 'https://api.openai.com/v1') 'isolated chat base URL differs from the canonical default'
  Require-ExactPropertyNames $config.search @('rrf_k', 'default_limit') 'isolated search config'
  Require-JsonIntegerDefault $config.search.rrf_k 60 'isolated search rrf_k'
  Require-JsonIntegerDefault $config.search.default_limit 10 'isolated search default_limit'
}

function Read-And-ValidateN17ToolsList([object]$Result) {
  Require (-not $Result.TimedOut -and $Result.ExitCode -eq 0) "stdio tools/list probe failed with exit code $($Result.ExitCode)"
  Require (-not [string]::IsNullOrWhiteSpace($Result.StdOut)) 'stdio tools/list probe returned empty stdout'
  try {
    $response = $Result.StdOut.Trim() | ConvertFrom-Json -ErrorAction Stop
  } catch {
    throw "N17 verification failed: stdio tools/list returned invalid JSON: $($_.Exception.Message)"
  }
  Require ((Has-JsonProperty $response 'jsonrpc') -and $response.jsonrpc -ceq '2.0') 'stdio tools/list response has an invalid JSON-RPC version'
  Require ((Has-JsonProperty $response 'id') -and [int64]$response.id -eq 17017) 'stdio tools/list response has an invalid id'
  Require ((Has-JsonProperty $response 'result') -and (Has-JsonProperty $response.result 'tools')) 'stdio tools/list response has no tools array'
  $allTools = @($response.result.tools)
  $captured = @()
  foreach ($name in @('replay_job', 'send_job_message', 'list_job_messages')) {
    $matches = @($allTools | Where-Object { $_.name -ceq $name })
    Require ($matches.Count -eq 1) "stdio tools/list must contain exactly one $name definition"
    Require ((Has-JsonProperty $matches[0] 'description') -and -not [string]::IsNullOrWhiteSpace($matches[0].description)) "$name has no advertised description"
    Require ((Has-JsonProperty $matches[0] 'inputSchema') -and $null -ne $matches[0].inputSchema) "$name has no advertised input schema"
    $captured += $matches[0]
  }

  $replaySchema = ($captured | Where-Object { $_.name -ceq 'replay_job' }).inputSchema
  $sendSchema = ($captured | Where-Object { $_.name -ceq 'send_job_message' }).inputSchema
  $listSchema = ($captured | Where-Object { $_.name -ceq 'list_job_messages' }).inputSchema
  Require-JobAliasSchema $replaySchema @('job_id', 'id') 'replay_job'
  Require-JobAliasSchema $sendSchema @('job_id', 'id', 'sender', 'payload_json') 'send_job_message'
  Require-JobAliasSchema $listSchema @('job_id', 'id', 'limit') 'list_job_messages'

  $sender = $sendSchema.properties.sender
  Require ($sender.type -ceq 'string' -and [int]$sender.minLength -eq 1 -and [int]$sender.maxLength -eq 128 -and $sender.default -ceq 'system') 'send_job_message sender schema is incorrect'
  $payload = $sendSchema.properties.payload_json
  Require ($payload.type -ceq 'string' -and [int]$payload.minLength -eq 1 -and [int]$payload.maxLength -eq 65536 -and $payload.default -ceq '{}') 'send_job_message payload_json schema is incorrect'
  $limit = $listSchema.properties.limit
  Require ($limit.type -ceq 'integer' -and [int]$limit.minimum -eq 0 -and [int]$limit.maximum -eq 200 -and [int]$limit.default -eq 50) 'list_job_messages limit schema is incorrect'

  [pscustomobject]@{ Response = $response; Tools = @($captured) }
}

# The current-process plan gate is N17-specific. The historical N17 outcome
# audit is intentionally not read or used as evidence by this verifier.
Require (Test-Path -LiteralPath $PlanPath -PathType Leaf) 'missing N17 approved plan'
Require (Test-Path -LiteralPath $PlanAuditPath -PathType Leaf) 'missing N17 plan audit'
$planStatus = First-Line $PlanPath '(?i)^\s*\*\*Status'
$planVerdict = First-Line $PlanAuditPath '(?i)^\s*\*\*VERDICT'
$planAuditor = First-Line $PlanAuditPath '(?i)^\s*\*\*Auditor'
Require ($planStatus -match '(?i)\bapproved\b' -and $planStatus -notmatch '(?i)\bdraft\b|\bdone\b') "N17 plan is not approved: $planStatus"
Require ($planVerdict -match '(?i)\bPASS\b' -and $planVerdict -notmatch '(?i)\bFAIL\b') "N17 plan audit is not PASS: $planVerdict"
Require ($planAuditor -match '(?i)Claude Code') "N17 plan audit is not attributed to Claude Code: $planAuditor"
$auditText = Get-Content -Raw -LiteralPath $PlanAuditPath
$auditedPlanHashMatch = [regex]::Match($auditText, '(?i)Audited plan SHA-256[^0-9a-f]*`?(?<hash>[0-9a-f]{64})`?')
Require ($auditedPlanHashMatch.Success) 'N17 plan audit does not record the audited draft-plan hash'
$AuditedDraftPlanHash = $auditedPlanHashMatch.Groups['hash'].Value.ToLowerInvariant()
$PlanHashAtStart = File-Hash $PlanPath
$PlanAuditHashAtStart = File-Hash $PlanAuditPath

$DependencyEvidence = @()
foreach ($node in @('N1', 'N7', 'N8', 'N12', 'N13', 'N14', 'N15')) {
  $dependencyPlan = Join-Path $Root "docs\nodes\$node-PLAN.md"
  $dependencyPlanAudit = Join-Path $Root "docs\nodes\$node-PLAN-AUDIT.md"
  $dependencyHardAudit = Join-Path $Root "docs\nodes\$node-HARD-AUDIT.md"
  Require (Test-Path -LiteralPath $dependencyPlan -PathType Leaf) "missing dependency plan: $node"
  Require (Test-Path -LiteralPath $dependencyPlanAudit -PathType Leaf) "missing dependency plan audit: $node"
  Require (Test-Path -LiteralPath $dependencyHardAudit -PathType Leaf) "missing dependency outcome audit: $node"
  $status = First-Line $dependencyPlan '(?i)^\s*\*\*Status'
  $dependencyPlanVerdict = First-Line $dependencyPlanAudit '(?i)^\s*\*\*VERDICT'
  $dependencyHardVerdict = First-Line $dependencyHardAudit '(?i)^\s*\*\*VERDICT'
  Require ($status -match '(?i)\bdone\b') "$node dependency is not done"
  Require ($dependencyPlanVerdict -match '(?i)\bPASS\b' -and $dependencyPlanVerdict -notmatch '(?i)\bFAIL\b') "$node dependency plan audit is not PASS"
  Require ($dependencyHardVerdict -match '(?i)\bPASS\b' -and $dependencyHardVerdict -notmatch '(?i)\bFAIL\b') "$node dependency outcome audit is not PASS"
  Require ((Get-Content -Raw -LiteralPath $dependencyPlanAudit) -match '(?i)Auditor[^\r\n]*Claude Code') "$node dependency plan audit is not attributed to Claude Code"
  Require ((Get-Content -Raw -LiteralPath $dependencyHardAudit) -match '(?i)Auditor[^\r\n]*Claude Code') "$node dependency outcome audit is not attributed to Claude Code"
  $DependencyEvidence += [pscustomobject]@{
    Node = $node
    PlanAuditHash = File-Hash $dependencyPlanAudit
    OutcomeAuditHash = File-Hash $dependencyHardAudit
  }
}

Require ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows)) 'verification requires native Windows'
Require ([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture -eq [Runtime.InteropServices.Architecture]::X64) 'verification process must be x64'
Require (Test-Path -LiteralPath $VcVars -PathType Leaf) 'missing MSVC vcvarsall.bat'
$clInfo = @(& cmd.exe /d /c "call `"$VcVars`" x64 >nul && echo target_arch=x64 && where cl && cl 2>&1" | ForEach-Object { $_.ToString().TrimEnd() })
$clExitCode = $LASTEXITCODE
Require ($clExitCode -eq 0) "MSVC discovery failed with exit code $clExitCode"
Require ((@($clInfo | Where-Object { $_.Trim() -ceq 'target_arch=x64' })).Count -eq 1) 'MSVC target architecture is not x64'
$ClVersion = ($clInfo | Where-Object { $_ -match 'C/C\+\+ Optimizing Compiler Version' } | Select-Object -First 1).Trim()
Require (-not [string]::IsNullOrWhiteSpace($ClVersion) -and $ClVersion -match 'for x64') 'cannot capture the full x64 MSVC version'
$ClPath = ($clInfo | Where-Object { $_ -match '(?i)^[A-Z]:\\.*\\cl\.exe$' } | Select-Object -First 1).Trim()
Require (-not [string]::IsNullOrWhiteSpace($ClPath) -and (Test-Path -LiteralPath $ClPath -PathType Leaf)) 'cannot resolve cl.exe path'
$ClHash = File-Hash $ClPath

$productionBuildText = Get-Content -Raw -LiteralPath $ProductionBuildScript
$testBuildText = Get-Content -Raw -LiteralPath $TestBuildScript
Require ($productionBuildText -match '/std:c\+\+20' -and $productionBuildText -match 'vcvars.*x64') 'production build script is not native MSVC x64 C++20'
Require ($testBuildText -match '/std:c\+\+20' -and $testBuildText -match 'vcvars.*x64') 'test build script is not native MSVC x64 C++20'

$HeadAtStart = Get-GitHead
$headMigrationText = Get-GitHeadText 'src/qbrain/storage/migrate.cpp'
$currentMigrationText = Get-Content -Raw -LiteralPath $MigrationSourcePath
$headV8Block = Get-V8MigrationBlock $headMigrationText
$currentV8Block = Get-V8MigrationBlock $currentMigrationText
$HeadV8BlockHash = Text-Hash $headV8Block
$CurrentV8BlockHash = Text-Hash $currentV8Block
Require ($currentV8Block -ceq $headV8Block) 'historical v8 job_messages DDL/version-marker source changed from HEAD'
Require ($currentMigrationText -match 'INSERT OR IGNORE INTO schema_version\(version\) VALUES \(12\);') 'schema v12 marker is missing'
Require ($currentMigrationText -notmatch 'schema_version\(version\) VALUES \((?:1[3-9]|[2-9][0-9]+)\)') 'schema version above 12 is present'

$N17ScopedDeliverables = @(
  'include/qbrain/jobs/minions.hpp',
  'src/qbrain/jobs/minions.cpp',
  'src/qbrain/ops/handlers.cpp',
  'src/qbrain/mcp/server.cpp',
  'src/qbrain/ops/registry.cpp',
  'src/qbrain/storage/migrate.cpp',
  'tests/test_n17.cpp',
  'tests/wave3_test_support.hpp',
  'tests/test_main.cpp',
  'CMakeLists.txt',
  'scripts/build-tests-cl.ps1',
  'scripts/n17-verify.ps1',
  'docs/nodes/N17-PLAN.md',
  'docs/nodes/N17-PLAN-AUDIT.md'
)
$GeneratedEvidenceRelativePaths = @(
  'docs/nodes/n17-evidence/PRODUCTION-BUILD-OUTPUT.txt',
  'docs/nodes/n17-evidence/TEST-BUILD-OUTPUT.txt',
  'docs/nodes/n17-evidence/TEST-OUTPUT.txt',
  'docs/nodes/n17-evidence/CLI-SMOKE-OUTPUT.txt',
  'docs/nodes/n17-evidence/BUILD-MANIFEST.txt',
  'docs/nodes/n17-evidence/N17-SCOPE-MANIFEST.txt',
  'docs/nodes/n17-evidence/RUNTIME-MARKERS.txt',
  'docs/nodes/n17-evidence/SCHEMA-EVIDENCE.txt',
  'docs/nodes/n17-evidence/OPERATION-SCHEMAS.json',
  'docs/nodes/n17-evidence/VERIFY-REPORT.md',
  'docs/nodes/n17-evidence/EVIDENCE-MANIFEST.txt'
)
$AllScopedPaths = @($N17ScopedDeliverables) + @($GeneratedEvidenceRelativePaths)
$N30ScopedPathCount = @($AllScopedPaths | Where-Object { $_ -match '(?i)(^|/)N30-' }).Count
$LaterNodeScopedPathCount = @($AllScopedPaths | Where-Object { $_ -match '(?i)(^|/)(?:N|n)(?:19|[2-9][0-9])(?:[-_.]|/)' }).Count
$ProtectedModelPathPattern = '(?i)(^|/)(?:\.codex|\.claude|\.opencode)(/|$)|(^|/)config(?:uration)?\.(?:json|toml|ya?ml)$|(^|/)(?:src|include)/qbrain/ai/|(^|/)include/qbrain/core/types\.hpp$|(^|/)src/qbrain/core/brain\.cpp$'
$ProtectedScopedPathCount = @($AllScopedPaths | Where-Object { $_ -match $ProtectedModelPathPattern }).Count
$ThirdPartyScopedPathCount = @($AllScopedPaths | Where-Object { $_ -match '(?i)(^|/)third_party/' }).Count
Require ($N30ScopedPathCount -eq 0) 'N17 scoped path list contains a forbidden N30 artifact'
Require ($LaterNodeScopedPathCount -eq 0) 'N17 scoped path list contains an N19-or-later path'
Require ($ProtectedScopedPathCount -eq 0) 'N17 scoped path list contains protected model configuration'
Require ($ThirdPartyScopedPathCount -eq 0) 'N17 scoped path list contains a third-party dependency change'
$RepositoryN30Paths = @(Get-ChildItem -LiteralPath (Join-Path $Root 'docs\nodes') -Force | Where-Object { $_.Name -like 'N30-*' })
Require ($RepositoryN30Paths.Count -eq 0) 'repository contains docs/nodes/N30-*; N17 must not use or emit that path'

$BuildClosureBefore = Get-FileState @(Get-BuildClosureFiles)
$ProtectedStateBefore = Get-FileState @(Get-ProtectedModelConfigurationFiles)

$TempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
$TempRoot = Join-Path $TempBase ('qbrain_n17_verify_' + [guid]::NewGuid().ToString('N'))
$IsolatedLocalAppData = Join-Path $TempRoot 'localappdata'
$IsolatedConfigPath = Join-Path $IsolatedLocalAppData 'Qbrain\config.json'
$OldLocalAppData = $env:LOCALAPPDATA
$ProductionBuildExitCode = 0
$TestBuildExitCode = 0
$TestExitCode = -1
$VersionExitCode = -1
$DoctorExitCode = -1
$McpToolsListExitCode = -1
$IsolatedConfigFileCount = -1
$IsolatedConfigFingerprint = 'absent'
$ConfigFingerprintBeforeStandalone = 'absent'

try {
  New-Item -ItemType Directory -Force -Path $IsolatedLocalAppData | Out-Null
  $env:LOCALAPPDATA = $IsolatedLocalAppData

  if (-not $SkipBuild) {
    $productionBuild = Invoke-CapturedProcess 'powershell.exe' "-NoProfile -ExecutionPolicy Bypass -File `"$ProductionBuildScript`""
    Write-ProcessLog $ProductionBuildLogPath $ProductionBuildCommand $productionBuild
    $ProductionBuildExitCode = $productionBuild.ExitCode
    Require (-not $productionBuild.TimedOut) 'production build timed out'
    Require ($ProductionBuildExitCode -eq 0) "canonical production build exited with $ProductionBuildExitCode"
    Require ((Get-Content -Raw -LiteralPath $ProductionBuildLogPath) -match '(?m)^BUILD_OK\s*$') 'production build log lacks BUILD_OK'

    $testBuild = Invoke-CapturedProcess 'powershell.exe' "-NoProfile -ExecutionPolicy Bypass -File `"$TestBuildScript`" -SkipProductionBuild"
    Write-ProcessLog $TestBuildLogPath $TestBuildCommand $testBuild
    $TestBuildExitCode = $testBuild.ExitCode
    Require (-not $testBuild.TimedOut) 'test build timed out'
    Require ($TestBuildExitCode -eq 0) "canonical test build exited with $TestBuildExitCode"
    Require ((Get-Content -Raw -LiteralPath $TestBuildLogPath) -match '(?m)^TESTS_BUILD_OK\s*$') 'test build log lacks TESTS_BUILD_OK'
    $EmbeddedSuite = Validate-TestSuite @(Process-Lines $testBuild) $TestBuildExitCode 'canonical test-build suite'

    Require (Test-Path -LiteralPath $Qbrain -PathType Leaf) 'production build did not create qbrain.exe'
    Require (Test-Path -LiteralPath $Tests -PathType Leaf) 'test build did not create qbrain_tests.exe'
    $BuildClosureAfterBuild = Get-FileState @(Get-BuildClosureFiles)
    Require-SameState $BuildClosureBefore $BuildClosureAfterBuild 'build input closure'
    Write-BuildManifest $BuildClosureAfterBuild $ProductionBuildExitCode $TestBuildExitCode
  } else {
    Validate-BuildManifest $BuildClosureBefore
    $productionLogText = Get-Content -Raw -LiteralPath $ProductionBuildLogPath
    $testBuildLogText = Get-Content -Raw -LiteralPath $TestBuildLogPath
    Require ($productionLogText -match '(?m)^BUILD_OK\s*$') 'validated production build log lacks BUILD_OK'
    Require ($testBuildLogText -match '(?m)^TESTS_BUILD_OK\s*$') 'validated test build log lacks TESTS_BUILD_OK'
    $EmbeddedSuite = Validate-TestSuite @((Normalize-Lf $testBuildLogText).Split("`n")) 0 'validated test-build suite'
  }

  $ConfigFingerprintBeforeStandalone = Optional-FileFingerprint $IsolatedConfigPath
  if ($ConfigFingerprintBeforeStandalone -cne 'absent') {
    Require-IsolatedDefaultConfig $IsolatedConfigPath
  }

  $testRun = Invoke-CapturedProcess $Tests '' 1800
  Write-ProcessLog $TestLogPath $TestCommand $testRun
  $TestExitCode = $testRun.ExitCode
  Require (-not $testRun.TimedOut) 'complete standalone test suite timed out'
  $RuntimeSuite = Validate-TestSuite @(Process-Lines $testRun) $TestExitCode 'complete standalone test suite'
  Require ($RuntimeSuite.PassCount -eq $EmbeddedSuite.PassCount) 'registered test count differs between test-build and standalone suite runs'
  $ConfigFingerprintAfterStandalone = Optional-FileFingerprint $IsolatedConfigPath
  if ($ConfigFingerprintAfterStandalone -cne 'absent') {
    Require-IsolatedDefaultConfig $IsolatedConfigPath
  }
  if ($ConfigFingerprintBeforeStandalone -cne 'absent') {
    Require ($ConfigFingerprintAfterStandalone -ceq $ConfigFingerprintBeforeStandalone) 'standalone suite changed the canonical isolated config content'
  }

  $version = Invoke-CapturedProcess $Qbrain 'version' 30
  $VersionExitCode = $version.ExitCode
  Require (-not $version.TimedOut -and $VersionExitCode -eq 0) "qbrain version failed with exit code $VersionExitCode"
  Require ($version.StdOut -match '(?i)Qbrain') 'qbrain version output is invalid'

  $doctor = Invoke-CapturedProcess $Qbrain 'doctor --json' 60
  $DoctorExitCode = $doctor.ExitCode
  Require (-not $doctor.TimedOut -and $DoctorExitCode -eq 0) "qbrain doctor --json failed with exit code $DoctorExitCode"
  Require ([string]::IsNullOrWhiteSpace($doctor.StdErr)) 'qbrain doctor --json wrote to stderr'
  try {
    $doctorJson = $doctor.StdOut.Trim() | ConvertFrom-Json -ErrorAction Stop
  } catch {
    throw "N17 verification failed: qbrain doctor --json returned invalid JSON: $($_.Exception.Message)"
  }
  Require ((Has-JsonProperty $doctorJson 'ok') -and $doctorJson.ok -eq $true) 'qbrain doctor --json did not report ok=true'

  $toolsListRequest = '{"jsonrpc":"2.0","id":17017,"method":"tools/list","params":{}}' + "`n"
  $toolsList = Invoke-CapturedProcess $Qbrain 'serve --brain n17-evidence' 60 $toolsListRequest
  $McpToolsListExitCode = $toolsList.ExitCode
  $N17ToolsList = Read-And-ValidateN17ToolsList $toolsList
  [ordered]@{
    format_version = 1
    node = 'N17'
    source = 'real stdio tools/list'
    write_enabled = $false
    tools = @($N17ToolsList.Tools)
  } | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $OperationSchemasPath -Encoding utf8

  $cliLines = [Collections.Generic.List[string]]::new()
  $cliLines.Add('command[1]=build\cl\qbrain.exe version')
  $cliLines.Add("command[1]_stdout=$($version.StdOut.Trim())")
  $cliLines.Add("command[1]_stderr=$($version.StdErr.Trim())")
  $cliLines.Add("command[1]_exit_code=$VersionExitCode")
  $cliLines.Add('command[2]=build\cl\qbrain.exe doctor --json')
  $cliLines.Add("command[2]_stdout=$($doctor.StdOut.Trim())")
  $cliLines.Add("command[2]_stderr=$($doctor.StdErr.Trim())")
  $cliLines.Add("command[2]_exit_code=$DoctorExitCode")
  $cliLines.Add('command[3]=build\cl\qbrain.exe serve --brain n17-evidence (stdio NDJSON tools/list; writes disabled)')
  $cliLines.Add("command[3]_stderr=$($toolsList.StdErr.Trim())")
  $cliLines.Add("command[3]_exit_code=$McpToolsListExitCode")
  $cliLines.Add('command[3]_schemas=OPERATION-SCHEMAS.json')
  $cliLines.Add('isolated_localappdata=true')
  $cliLines.Add('live_network_calls=0')
  $cliLines | Set-Content -LiteralPath $CliLogPath -Encoding utf8

  $isolatedConfigFiles = @(Get-ChildItem -LiteralPath $IsolatedLocalAppData -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -ieq 'config.json' })
  $IsolatedConfigFileCount = $isolatedConfigFiles.Count
  Require ($IsolatedConfigFileCount -le 1) 'verification created more than one config.json in isolated LOCALAPPDATA'
  if ($IsolatedConfigFileCount -eq 1) {
    Require ([IO.Path]::GetFullPath($isolatedConfigFiles[0].FullName) -ceq [IO.Path]::GetFullPath($IsolatedConfigPath)) 'verification created config.json outside the canonical isolated application path'
    Require-IsolatedDefaultConfig $IsolatedConfigPath
    $IsolatedConfigFingerprint = File-Hash $IsolatedConfigPath
    Require ($IsolatedConfigFingerprint -ceq $ConfigFingerprintAfterStandalone) 'CLI/MCP probes changed the canonical isolated config content'
  } else {
    $IsolatedConfigFingerprint = 'absent'
    Require ($ConfigFingerprintAfterStandalone -ceq 'absent') 'isolated application config disappeared during CLI/MCP probes'
  }
} finally {
  if ($null -eq $OldLocalAppData) {
    Remove-Item Env:LOCALAPPDATA -ErrorAction SilentlyContinue
  } else {
    $env:LOCALAPPDATA = $OldLocalAppData
  }
  $tempRootFull = [IO.Path]::GetFullPath($TempRoot)
  if ($tempRootFull.StartsWith($TempBase, [StringComparison]::OrdinalIgnoreCase) -and
      ([IO.Path]::GetFileName($tempRootFull)).StartsWith('qbrain_n17_verify_', [StringComparison]::Ordinal)) {
    Remove-Item -LiteralPath $tempRootFull -Recurse -Force -ErrorAction SilentlyContinue
  }
}

$BuildClosureAfter = Get-FileState @(Get-BuildClosureFiles)
$ProtectedStateAfter = Get-FileState @(Get-ProtectedModelConfigurationFiles)
Require-SameState $BuildClosureBefore $BuildClosureAfter 'build input closure'
Require-SameState $ProtectedStateBefore $ProtectedStateAfter 'protected model configuration source set'
Validate-BuildManifest $BuildClosureAfter
Require ((File-Hash $PlanPath) -ceq $PlanHashAtStart) 'approved N17 plan changed during verification'
Require ((File-Hash $PlanAuditPath) -ceq $PlanAuditHashAtStart) 'N17 plan audit changed during verification'
$HeadAtEnd = Get-GitHead
Require ($HeadAtEnd -ceq $HeadAtStart) 'repository HEAD changed during verification'

$QbrainHash = File-Hash $Qbrain
$TestsHash = File-Hash $Tests
$RuntimeMarker = $RuntimeSuite.Marker.Line
$BuildMarker = $EmbeddedSuite.Marker.Line
$RuntimeValues = $RuntimeSuite.Marker.Values

@(
  'format_version=1',
  'node=N17',
  'source=canonical_test_build',
  $BuildMarker,
  'source=standalone_complete_suite',
  $RuntimeMarker
) | Set-Content -LiteralPath $RuntimeMarkersPath -Encoding utf8

@(
  'format_version=1',
  'node=N17',
  "head_v8_source_sha256=$HeadV8BlockHash",
  "current_v8_source_sha256=$CurrentV8BlockHash",
  'v8_source_unchanged=true',
  'current_schema_version=12',
  'schema_version_above_12_count=0',
  'migration_v7_v8_v12=pass',
  'migration_idempotence=pass',
  'migration_rollback=pass',
  'damaged_integrity=pass',
  'no_foreign_key=pass',
  "migration_snapshot_sha256=$($RuntimeValues['migration_snapshot_sha256'])",
  "rollback_snapshot_sha256=$($RuntimeValues['rollback_snapshot_sha256'])"
) | Set-Content -LiteralPath $SchemaEvidencePath -Encoding utf8

$DeliverableHashes = foreach ($relative in $N17ScopedDeliverables) {
  $absolute = Join-Path $Root $relative
  Require (Test-Path -LiteralPath $absolute -PathType Leaf) "missing scoped N17 deliverable: $relative"
  [pscustomobject]@{ Path = $relative.Replace('\', '/'); Hash = File-Hash $absolute }
}

$scopeLines = [Collections.Generic.List[string]]::new()
$scopeLines.Add('format_version=1')
$scopeLines.Add('node=N17')
$scopeLines.Add("generated_utc=$((Get-Date).ToUniversalTime().ToString('o'))")
$scopeLines.Add("approved_plan_sha256=$PlanHashAtStart")
$scopeLines.Add("plan_audit_sha256=$PlanAuditHashAtStart")
$scopeLines.Add("audit_recorded_draft_plan_sha256=$AuditedDraftPlanHash")
$scopeLines.Add("git_head_before=$HeadAtStart")
$scopeLines.Add("git_head_after=$HeadAtEnd")
$scopeLines.Add("build_evidence_mode=$BuildEvidenceMode")
$scopeLines.Add("n17_scoped_deliverable_count=$($N17ScopedDeliverables.Count)")
$scopeLines.Add('n30_artifact_count=0')
$scopeLines.Add('n30_repository_path_count=0')
$scopeLines.Add('n17_created_or_modified_n19_or_later_path_count=0')
$scopeLines.Add('protected_model_configuration_scoped_path_count=0')
$scopeLines.Add('protected_model_configuration_runtime_change_count=0')
$scopeLines.Add("protected_model_configuration_monitored_file_count=$($ProtectedStateBefore.Count)")
$scopeLines.Add("isolated_config_file_count=$IsolatedConfigFileCount")
$scopeLines.Add("isolated_config_sha256=$IsolatedConfigFingerprint")
$scopeLines.Add('isolated_config_policy=absent_or_canonical_defaults_only')
$scopeLines.Add('live_network_call_count=0')
$scopeLines.Add('git_mutating_command_count=0')
$scopeLines.Add('third_party_dependency_change_count=0')
$scopeLines.Add('replay_job_scope=Write')
$scopeLines.Add('replay_job_local_only=true')
$scopeLines.Add('send_job_message_scope=Write')
$scopeLines.Add('send_job_message_local_only=true')
$scopeLines.Add('list_job_messages_scope=Read')
$scopeLines.Add('list_job_messages_local_only=false')
$scopeLines.Add('successful_replay_delta=jobs:+1,jobs_sequence:+1,other_application_state:+0,decoy:+0')
$scopeLines.Add('successful_message_delta=job_messages:+1,job_messages_sequence:+1,other_application_state:+0,decoy:+0')
foreach ($row in $DeliverableHashes) { $scopeLines.Add("SCOPED`t$($row.Hash)`t$($row.Path)") }
$scopeLines | Set-Content -LiteralPath $ScopeManifestPath -Encoding utf8

$os = try {
  $item = Get-CimInstance Win32_OperatingSystem
  "$($item.Caption) $($item.Version) build $($item.BuildNumber); OSArchitecture=$($item.OSArchitecture)"
} catch {
  [Environment]::OSVersion.VersionString
}

$report = [Collections.Generic.List[string]]::new()
$report.Add('# N17 Runtime Verification Report')
$report.Add('')
$report.Add('This is factual runtime evidence only. It is not a Claude Code plan audit or outcome hard-audit verdict. The historical N17 outcome-audit file was not read or used as a gate.')
$report.Add('')
$report.Add("- Generated UTC: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))")
$report.Add("- OS: $os")
$report.Add("- Process architecture: $([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture)")
$report.Add('- Target: native Windows x64')
$report.Add('- Language mode: `/std:c++20`')
$report.Add("- MSVC: $ClVersion")
$report.Add("- MSVC discovery exit code: $clExitCode")
$report.Add("- cl.exe SHA-256: ``$ClHash``")
$report.Add("- qbrain.exe SHA-256: ``$QbrainHash``")
$report.Add("- qbrain_tests.exe SHA-256: ``$TestsHash``")
$report.Add("- Approved plan SHA-256: ``$PlanHashAtStart``")
$report.Add("- Plan audit SHA-256: ``$PlanAuditHashAtStart``")
$report.Add("- Audit-recorded draft-plan SHA-256: ``$AuditedDraftPlanHash``")
$report.Add("- Build evidence mode: ``$BuildEvidenceMode``")
$report.Add('')
$report.Add('## Commands And Exit Codes')
$report.Add('')
$report.Add('| Command | Exit code |')
$report.Add('|---|---:|')
$report.Add("| ``$ProductionBuildCommand`` | $ProductionBuildExitCode |")
$report.Add("| ``$TestBuildCommand`` | $TestBuildExitCode |")
$report.Add("| ``$TestCommand`` | $TestExitCode |")
$report.Add("| ``build\cl\qbrain.exe version`` | $VersionExitCode |")
$report.Add("| ``build\cl\qbrain.exe doctor --json`` | $DoctorExitCode |")
$report.Add("| ``$McpToolsListCommand`` | $McpToolsListExitCode |")
$report.Add('')
$report.Add("The canonical test-build run and the separate complete-suite run each registered exactly $($RuntimeSuite.PassCount) PASS results and zero FAIL results, including exactly one ``[PASS] n17``.")
$report.Add('')
$report.Add('## Corrective Closure Evidence')
$report.Add('')
$report.Add('The approved plan identifies the historical pre-fix behavior as any-status replay, permissive prefix/whitespace id parsing, generic failures, and an unregistered list helper. The dedicated current test marker records the strict-id matrix, terminal-only replay matrix, structured MCP validation, registered tools/list path, default-deny/allow-write paths, exact selected/decoy snapshots, schema checks, and both permitted concurrency schedules. Historical PASS text is not used as current conformance evidence.')
$report.Add('')
$report.Add('```text')
$report.Add($RuntimeMarker)
$report.Add('```')
$report.Add('')
$report.Add('## Snapshot, Schema, And Concurrency Markers')
$report.Add('')
$report.Add('| Fact | Captured value |')
$report.Add('|---|---|')
$report.Add("| Selected-brain snapshot | ``$($RuntimeValues['selected_snapshot_sha256'])`` |")
$report.Add("| Decoy-brain snapshot | ``$($RuntimeValues['decoy_snapshot_sha256'])`` |")
$report.Add("| Migration snapshot | ``$($RuntimeValues['migration_snapshot_sha256'])`` |")
$report.Add("| Migration rollback snapshot | ``$($RuntimeValues['rollback_snapshot_sha256'])`` |")
$report.Add("| Replay race | successes=$($RuntimeValues['replay_race_successes']), busy=$($RuntimeValues['replay_race_busy']), retry-to-two checked by dedicated test |")
$report.Add("| Message race | successes=$($RuntimeValues['message_race_successes']), busy=$($RuntimeValues['message_race_busy']), retry-to-two checked by dedicated test |")
$report.Add("| Historical v8 source | HEAD=``$HeadV8BlockHash``; current=``$CurrentV8BlockHash``; byte-normalized equality=true |")
$report.Add('| Schema baseline | v12; no version above 12; exact v8 table/index/no-FK and damaged-v12 checks recorded by the marker |')
$report.Add('| Successful replay delta | one `jobs` row plus its sequence advance; all pre-existing/other application state and the decoy remain unchanged |')
$report.Add('| Successful message delta | one `job_messages` row plus its sequence advance; the parent job, other application state, and the decoy remain unchanged |')
$report.Add('')
$report.Add('## Scope And Security Evidence')
$report.Add('')
$report.Add('- The scoped artifact count for the forbidden coordinator node is zero, and the repository path check is zero.')
$report.Add('- The N17-created/modified N19-or-later scoped path count is zero. Pre-existing later-node tests in the complete regression suite are not attributed to N17.')
$report.Add("- No protected model-configuration path is in the N17 scope. All monitored configuration source hashes were identical before and after verification. The isolated config count was $IsolatedConfigFileCount; when present, it contained only exact canonical defaults, no key/allowlist/extra fields, remained byte-stable across standalone/CLI/MCP probes, and was removed with the sandbox.")
$report.Add('- The verifier invoked no network server/provider command and made no live network call. All runtime commands used a unique temporary LOCALAPPDATA that was removed after checks.')
$report.Add('- A real stdio `tools/list` call captured only the three N17 definitions in `OPERATION-SCHEMAS.json`; exact object schemas, alias requirements, integer/string bounds, defaults, and `additionalProperties=false` were checked before packaging.')
$report.Add('- Runtime registry evidence records `replay_job` and `send_job_message` as Write/local-only and `list_job_messages` as Read/non-local-only; remote writes remain denied unless explicitly allowed.')
$report.Add('- No git-mutating command, commit, push, schema downgrade, or third-party dependency change is part of this evidence run.')
$report.Add('')
$report.Add('## Dependency Evidence')
$report.Add('')
$report.Add('| Node | Plan audit | Outcome audit | Plan-audit SHA-256 | Outcome-audit SHA-256 |')
$report.Add('|---|---|---|---|---|')
foreach ($item in $DependencyEvidence) {
  $report.Add("| $($item.Node) | PASS | PASS | ``$($item.PlanAuditHash)`` | ``$($item.OutcomeAuditHash)`` |")
}
$report.Add('')
$report.Add('## Generated Artifact Hashes')
$report.Add('')
$report.Add('| Path | SHA-256 |')
$report.Add('|---|---|')
foreach ($path in @($ProductionBuildLogPath, $TestBuildLogPath, $BuildManifestPath, $TestLogPath, $CliLogPath, $RuntimeMarkersPath, $SchemaEvidencePath, $OperationSchemasPath, $ScopeManifestPath)) {
  $report.Add("| ``$(Relative-Path $path)`` | ``$(File-Hash $path)`` |")
}
$report.Add('')
$report.Add('## Deliverable Hashes')
$report.Add('')
$report.Add('| Path | SHA-256 |')
$report.Add('|---|---|')
foreach ($row in $DeliverableHashes) { $report.Add("| ``$($row.Path)`` | ``$($row.Hash)`` |") }
$report.Add('')
$report.Add('## Result')
$report.Add('')
$report.Add('All scripted N17 verification checks completed successfully. The final evidence manifest is generated after this report so it can include the report hash; the manifest intentionally omits only its own recursive hash. A separate node-specific Claude Code outcome hard audit is still required before N17 may be marked done.')
$report | Set-Content -LiteralPath $ReportPath -Encoding utf8

$EvidenceFiles = @(
  $ProductionBuildLogPath, $TestBuildLogPath, $TestLogPath, $CliLogPath,
  $BuildManifestPath, $ScopeManifestPath, $RuntimeMarkersPath,
  $SchemaEvidencePath, $OperationSchemasPath, $ReportPath
)
$manifest = [Collections.Generic.List[string]]::new()
$manifest.Add('format_version=1')
$manifest.Add('node=N17')
$manifest.Add("generated_utc=$((Get-Date).ToUniversalTime().ToString('o'))")
$manifest.Add('self_hash_omitted=true')
$manifest.Add("build_evidence_mode=$BuildEvidenceMode")
$manifest.Add("production_command=$ProductionBuildCommand")
$manifest.Add("production_exit_code=$ProductionBuildExitCode")
$manifest.Add("test_build_command=$TestBuildCommand")
$manifest.Add("test_build_exit_code=$TestBuildExitCode")
$manifest.Add("test_command=$TestCommand")
$manifest.Add("test_exit_code=$TestExitCode")
$manifest.Add("mcp_tools_list_command=$McpToolsListCommand")
$manifest.Add("mcp_tools_list_exit_code=$McpToolsListExitCode")
$manifest.Add("registered_tests=$($RuntimeSuite.PassCount)")
$manifest.Add('observed_fail_count=0')
$manifest.Add("qbrain_sha256=$QbrainHash")
$manifest.Add("qbrain_tests_sha256=$TestsHash")
$manifest.Add("cl_sha256=$ClHash")
$manifest.Add("cl_discovery_exit_code=$clExitCode")
$manifest.Add("approved_plan_sha256=$PlanHashAtStart")
$manifest.Add("plan_audit_sha256=$PlanAuditHashAtStart")
$manifest.Add("selected_snapshot_sha256=$($RuntimeValues['selected_snapshot_sha256'])")
$manifest.Add("decoy_snapshot_sha256=$($RuntimeValues['decoy_snapshot_sha256'])")
$manifest.Add("migration_snapshot_sha256=$($RuntimeValues['migration_snapshot_sha256'])")
$manifest.Add("rollback_snapshot_sha256=$($RuntimeValues['rollback_snapshot_sha256'])")
$manifest.Add("replay_race_successes=$($RuntimeValues['replay_race_successes'])")
$manifest.Add("replay_race_busy=$($RuntimeValues['replay_race_busy'])")
$manifest.Add("message_race_successes=$($RuntimeValues['message_race_successes'])")
$manifest.Add("message_race_busy=$($RuntimeValues['message_race_busy'])")
$manifest.Add("n17_runtime_marker_sha256=$(Text-Hash $RuntimeMarker)")
$manifest.Add("strict_id_cases=$($RuntimeValues['strict_id_cases'])")
$manifest.Add("replay_state_cases=$($RuntimeValues['replay_state_cases'])")
$manifest.Add("sender_payload_cases=$($RuntimeValues['sender_payload_cases'])")
$manifest.Add("list_limit_cases=$($RuntimeValues['list_limit_cases'])")
$manifest.Add("mcp_rejection_cases=$($RuntimeValues['mcp_rejection_cases'])")
$manifest.Add('replay_job_scope=Write')
$manifest.Add('replay_job_local_only=true')
$manifest.Add('send_job_message_scope=Write')
$manifest.Add('send_job_message_local_only=true')
$manifest.Add('list_job_messages_scope=Read')
$manifest.Add('list_job_messages_local_only=false')
$manifest.Add('successful_replay_delta=jobs:+1,jobs_sequence:+1,other_application_state:+0,decoy:+0')
$manifest.Add('successful_message_delta=job_messages:+1,job_messages_sequence:+1,other_application_state:+0,decoy:+0')
$manifest.Add('n30_artifact_count=0')
$manifest.Add('n17_created_or_modified_n19_or_later_path_count=0')
$manifest.Add('protected_model_configuration_change_count=0')
$manifest.Add("isolated_config_file_count=$IsolatedConfigFileCount")
$manifest.Add("isolated_config_sha256=$IsolatedConfigFingerprint")
$manifest.Add('isolated_config_policy=absent_or_canonical_defaults_only')
foreach ($row in $DeliverableHashes) { $manifest.Add("DELIVERABLE`t$($row.Hash)`t$($row.Path)") }
foreach ($path in $EvidenceFiles) { $manifest.Add("EVIDENCE`t$(File-Hash $path)`t$(Relative-Path $path)") }
$manifest | Set-Content -LiteralPath $EvidenceManifestPath -Encoding utf8

Require-NoSensitiveEvidence @($EvidenceFiles + $EvidenceManifestPath)
$FinalBuildState = Get-FileState @(Get-BuildClosureFiles)
$FinalProtectedState = Get-FileState @(Get-ProtectedModelConfigurationFiles)
Require-SameState $BuildClosureBefore $FinalBuildState 'final build input closure'
Require-SameState $ProtectedStateBefore $FinalProtectedState 'final protected model configuration source set'
Validate-BuildManifest $FinalBuildState
Require ((File-Hash $PlanPath) -ceq $PlanHashAtStart) 'approved N17 plan changed before evidence completion'
Require ((File-Hash $PlanAuditPath) -ceq $PlanAuditHashAtStart) 'N17 plan audit changed before evidence completion'
Require ((Get-GitHead) -ceq $HeadAtStart) 'repository HEAD changed before evidence completion'
Write-Host "N17_VERIFY_OK tests=$($RuntimeSuite.PassCount) production_exit=$ProductionBuildExitCode test_build_exit=$TestBuildExitCode test_exit=$TestExitCode"
exit 0
