# N19 evidence verifier. This script produces factual runtime evidence only.
# It never supplies a Claude Code audit verdict or changes node/ledger status.
[CmdletBinding(DefaultParameterSetName = 'Verify')]
param(
  [Parameter(Mandatory = $true, ParameterSetName = 'Prepare')]
  [switch]$Prepare,

  [Parameter(ParameterSetName = 'Verify')]
  [switch]$RunBuilds,

  [Parameter(ParameterSetName = 'Verify')]
  [string]$ProductionBuildLog,

  [Parameter(ParameterSetName = 'Verify')]
  [string]$TestBuildLog,

  [Parameter(ParameterSetName = 'Verify')]
  [ValidateRange(60, 7200)]
  [int]$TimeoutSeconds = 3600
)

$ErrorActionPreference = 'Stop'
if (Test-Path Variable:PSNativeCommandUseErrorActionPreference) {
  $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Split-Path -Parent $PSScriptRoot
$EvidenceDir = Join-Path $Root 'docs\nodes\n19-evidence'
$PreCorrectiveGatePath = Join-Path $EvidenceDir 'PRE-CORRECTIVE-SCHEMA-GATE.json'
$PrebuildManifestPath = Join-Path $EvidenceDir 'PREBUILD-MANIFEST.json'
$EvidenceManifestPath = Join-Path $EvidenceDir 'EVIDENCE-MANIFEST.json'
$ReportPath = Join-Path $EvidenceDir 'VERIFY-REPORT.md'
$ProductionEvidencePath = Join-Path $EvidenceDir 'PRODUCTION-BUILD-OUTPUT.txt'
$TestBuildEvidencePath = Join-Path $EvidenceDir 'TEST-BUILD-OUTPUT.txt'
$FullSuiteEvidencePath = Join-Path $EvidenceDir 'FULL-SUITE-OUTPUT.txt'
$FocusedEvidencePath = Join-Path $EvidenceDir 'FOCUSED-RUNTIME-OUTPUT.txt'
$SnapshotEvidencePath = Join-Path $EvidenceDir 'SNAPSHOT-EVIDENCE.txt'
$McpSchemaEvidencePath = Join-Path $EvidenceDir 'MCP-SCHEMA-EVIDENCE.txt'
$SchemaSmokeEvidencePath = Join-Path $EvidenceDir 'SCHEMA-SMOKE-OUTPUT.txt'
$PlatformEvidencePath = Join-Path $EvidenceDir 'PLATFORM-OUTPUT.txt'
$BuildScript = Join-Path $Root 'scripts\build-cl.ps1'
$TestBuildScript = Join-Path $Root 'scripts\build-tests-cl.ps1'
$Qbrain = Join-Path $Root 'build\cl\qbrain.exe'
$Tests = Join-Path $Root 'build\cl\qbrain_tests.exe'
$VcVars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$PreCorrectiveGateExpected = [pscustomobject][ordered]@{
  file_sha256 = '2638f4c02a501cf295f8b2ab575e894d051159479b8f6a8beadca28d6544ea3a'
  started_utc = '2026-08-04T08:50:12.4331908+00:00'
  completed_utc = '2026-08-04T08:50:13.0139314+00:00'
  command = 'build\cl\qbrain.exe doctor --brain n19-pre-corrective-gate --json'
  approved_plan_sha256 = 'd03201c31a6a4051b2cc6d5b82e5ced7ae8c4b0982bc65223e251870d01c1d3a'
  audited_draft_plan_sha256 = 'f1cffc57a0e3d1c7447eb8c0cacfa130a3afeb05f85cdceaba26406fc69f958e'
  plan_audit_sha256 = 'e5c603efbfecb5603a0fd068dd2a0b39e7a75abac5fd116634adc397d9b7e470'
  qbrain_sha256 = '04460d5ae88d4e1c285cb4f7b4a05df548bfd830772a053b9555f2d45990e7b4'
  git_head = '5ced8ccb511672536d0f9767a2bc1777baf561ab'
  input_manifest_sha256 = 'dbe67c9bfbb31baaa4ae2c19dbac40bb11c9e82c662cd2c73ed930f8e5528cfa'
}

$PreCorrectiveExecutionPath = @(
  'qbrain doctor',
  'Brain::health',
  'storage::check_schema_integrity'
)

$PreCorrectiveInputPaths = @(
  'CMakeLists.txt',
  'include/qbrain/core/brain.hpp',
  'include/qbrain/search/hybrid.hpp',
  'include/qbrain/storage/database.hpp',
  'include/qbrain/storage/schema_sql.hpp',
  'include/qbrain/util/time_util.hpp',
  'scripts/build-cl.ps1',
  'scripts/build-tests-cl.ps1',
  'scripts/n19-verify.ps1',
  'src/qbrain/core/brain.cpp',
  'src/qbrain/mcp/server.cpp',
  'src/qbrain/ops/handlers.cpp',
  'src/qbrain/search/hybrid.cpp',
  'src/qbrain/storage/database.cpp',
  'src/qbrain/storage/migrate.cpp',
  'src/qbrain/util/time_util.cpp',
  'tests/test_main.cpp',
  'tests/test_n19.cpp',
  'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.c',
  'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.h'
)

$PreCorrectiveChangedInputPaths = @(
  'scripts/n19-verify.ps1',
  'tests/test_n19.cpp'
)

if ([string]::IsNullOrWhiteSpace($ProductionBuildLog)) {
  $ProductionBuildLog = $ProductionEvidencePath
}
if ([string]::IsNullOrWhiteSpace($TestBuildLog)) {
  $TestBuildLog = $TestBuildEvidencePath
}

$N19Deliverables = @(
  'include/qbrain/core/brain.hpp',
  'include/qbrain/search/hybrid.hpp',
  'include/qbrain/util/time_util.hpp',
  'src/qbrain/core/brain.cpp',
  'src/qbrain/search/hybrid.cpp',
  'src/qbrain/ops/handlers.cpp',
  'src/qbrain/mcp/server.cpp',
  'src/qbrain/util/time_util.cpp',
  'tests/test_n19.cpp',
  'tests/test_main.cpp',
  'CMakeLists.txt',
  'scripts/build-cl.ps1',
  'scripts/build-tests-cl.ps1',
  'scripts/n19-verify.ps1',
  'docs/nodes/N19-PLAN.md',
  'docs/nodes/N19-PLAN-AUDIT.md'
)

$RelevantSchemaInputs = @(
  'include/qbrain/storage/database.hpp',
  'include/qbrain/storage/schema_sql.hpp',
  'src/qbrain/storage/database.cpp',
  'src/qbrain/storage/migrate.cpp',
  'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.c',
  'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.h'
)

$N19ScopedDiffPaths = @(
  'include/qbrain/core/brain.hpp',
  'include/qbrain/search/hybrid.hpp',
  'include/qbrain/util/time_util.hpp',
  'src/qbrain/core/brain.cpp',
  'src/qbrain/search/hybrid.cpp',
  'src/qbrain/ops/handlers.cpp',
  'src/qbrain/mcp/server.cpp',
  'src/qbrain/util/time_util.cpp',
  'tests/test_n19.cpp',
  'tests/test_main.cpp',
  'CMakeLists.txt',
  'scripts/build-tests-cl.ps1',
  'scripts/n19-verify.ps1'
)

$DependencyContracts = @(
  [pscustomobject]@{ Node='N1';   Plan='9fd6df77ad905463f34e6873c2220849003679a64c869e5fb1eaffba470f95e6'; Outcome='93f112c13d01864aa701683e2a4dbb3726a763d90b7a113c07dc543af4d31141' },
  [pscustomobject]@{ Node='N2';   Plan='c34fede88989a9847dd3cad0bf719b6476c28bbfb124cb094d4afbe24d90fb85'; Outcome='e9dc809dcdb73c0757708f81d53daf2fc89394c12cf953e86c0e9de5923a3413' },
  [pscustomobject]@{ Node='N2.5'; Plan='bd0cf1b5f4dddb9af40168a89d1a87be84d5a4eb2f99872d3389880523617953'; Outcome='dd6e404ab7583af8c6cbecd86179baba3401a1d5ef10f559b2067229a208c8ff' },
  [pscustomobject]@{ Node='N3';   Plan='ab99d7c2d0553575f16124fba807067a8039839321fb9f3469c949dbdc8a4994'; Outcome='2ca977089b0564f7ff60752c8cff4b968e1f528163239ef4c19e1bd22c094d24' },
  [pscustomobject]@{ Node='N7';   Plan='929970318d8fb3043371f82a9208360db7e38e6dd058e37f0eef515534f26d39'; Outcome='307226705f0dc7495b0aa7aeebf88bd807c0216c19cab059cd23d01dd6835421' },
  [pscustomobject]@{ Node='N8';   Plan='7f16263f786315420ed42a7c79350add553ad84b11ce4cd6dbc21b0fdc320570'; Outcome='7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9' },
  [pscustomobject]@{ Node='N11';  Plan='e157d9f3b6dcbc276b782d960c237d50fed9d4ff5614473678813e27541844a7'; Outcome='bdefcf26d138b658d31df0b8525c46b776aa5e9086796bcd16696d8b783f2012' },
  [pscustomobject]@{ Node='N15';  Plan='01e95a0cc55e4d0580562008a65de2ee941a13a8b37f4fd730389937d5abaef1'; Outcome='9f5f14ab7ed2cf4da50b597f8f861061948d9b65331091a017d677f7b4968c59' },
  [pscustomobject]@{ Node='N16';  Plan='ad6794067444a56658d52d23c3ca29f7092cd7024829f1c4313b295b10c77fef'; Outcome='591865f6647e175c4aa02ec90abad1075c554eca49e3a15e5f63ad1639c24aba' },
  [pscustomobject]@{ Node='N18';  Plan='87db9821c255555ab6a42aab8d22cac945a5e0141aeeb3dd02e76a07e743af6d'; Outcome='f09971ecf44ab66129f33ee3b7dad91515aac39d6d330b725916983fcb408053' }
)

function Require([bool]$Condition, [string]$Message) {
  if (-not $Condition) {
    throw "N19 evidence requirement failed: $Message"
  }
}

function Write-Utf8Text([string]$Path, [string]$Text) {
  [IO.File]::WriteAllText($Path, $Text, $Utf8NoBom)
}

function Write-Utf8Lines([string]$Path, [object[]]$Lines) {
  $text = (@($Lines) -join [Environment]::NewLine) + [Environment]::NewLine
  Write-Utf8Text $Path $text
}

function File-Hash([string]$Path) {
  Require (Test-Path -LiteralPath $Path -PathType Leaf) "missing file: $Path"
  (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Text-Hash([string]$Text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
  } finally {
    $sha.Dispose()
  }
}

function Require-ExactJsonPropertyNames([object]$Value, [string[]]$Expected, [string]$Label) {
  $actual = @($Value.PSObject.Properties.Name | Sort-Object)
  $wanted = @($Expected | Sort-Object)
  Require ($actual.Count -eq $wanted.Count -and (($actual -join "`n") -ceq ($wanted -join "`n"))) "$Label property set is not exact"
}

function Require-JsonInteger([object]$Value, [string]$Label) {
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
  Require $isInteger "$Label is not a JSON integer"
}

function Require-JsonIntegerDefault([object]$Value, [int64]$Expected, [string]$Label) {
  Require-JsonInteger $Value $Label
  Require ([int64]$Value -eq $Expected) "$Label differs from the canonical integer default"
}

function Require-JsonStringExact([object]$Value, [string]$Expected, [string]$Label) {
  Require ($Value -is [string] -and $Value -ceq $Expected) "$Label is not the exact expected JSON string"
}

function Require-JsonBooleanExact([object]$Value, [bool]$Expected, [string]$Label) {
  Require ($Value -is [bool] -and $Value -eq $Expected) "$Label is not the exact expected JSON boolean"
}

function ConvertFrom-JsonPreservingDateStrings([string]$Json) {
  $command = Get-Command ConvertFrom-Json
  if ($command.Parameters.ContainsKey('DateKind')) {
    return $Json | ConvertFrom-Json -DateKind String -ErrorAction Stop
  }
  $Json | ConvertFrom-Json -ErrorAction Stop
}

function Assert-IsolatedTestConfig([string]$Sandbox, [string]$Label) {
  $canonicalPath = Join-Path $Sandbox 'Qbrain\config.json'
  $configFiles = @(Get-ChildItem -LiteralPath $Sandbox -Recurse -File -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -ieq 'config.json' })
  Require ($configFiles.Count -le 1) "$Label created more than one config.json"
  if ($configFiles.Count -eq 0) {
    return [pscustomobject][ordered]@{ count=0; sha256='absent' }
  }
  Require ([IO.Path]::GetFullPath($configFiles[0].FullName).Equals(
      [IO.Path]::GetFullPath($canonicalPath), [StringComparison]::OrdinalIgnoreCase)) "$Label created config.json outside the canonical sandbox path"
  try {
    $config = Get-Content -Raw -LiteralPath $canonicalPath | ConvertFrom-Json -ErrorAction Stop
  } catch {
    throw "N19 evidence requirement failed: $Label config.json is invalid JSON"
  }
  Require-ExactJsonPropertyNames $config @('brain_id', 'embedding', 'chat', 'search') "$Label config"
  Require ($config.brain_id -is [string] -and -not [string]::IsNullOrWhiteSpace($config.brain_id)) "$Label config brain_id is not a non-empty string"
  Require-ExactJsonPropertyNames $config.embedding @('provider', 'model', 'base_url', 'dimensions') "$Label embedding config"
  Require ($config.embedding.provider -ceq 'openai') "$Label embedding provider differs from the canonical default"
  Require ($config.embedding.model -ceq 'text-embedding-3-small') "$Label embedding model differs from the canonical default"
  Require ($config.embedding.base_url -ceq 'https://api.openai.com/v1') "$Label embedding base URL differs from the canonical default"
  Require-JsonIntegerDefault $config.embedding.dimensions 1536 "$Label embedding dimensions"
  Require-ExactJsonPropertyNames $config.chat @('model', 'base_url') "$Label chat config"
  Require ($config.chat.model -ceq 'gpt-4o-mini') "$Label chat model differs from the canonical default"
  Require ($config.chat.base_url -ceq 'https://api.openai.com/v1') "$Label chat base URL differs from the canonical default"
  Require-ExactJsonPropertyNames $config.search @('rrf_k', 'default_limit') "$Label search config"
  Require-JsonIntegerDefault $config.search.rrf_k 60 "$Label search rrf_k"
  Require-JsonIntegerDefault $config.search.default_limit 10 "$Label search default_limit"
  [pscustomobject][ordered]@{ count=1; sha256=File-Hash $canonicalPath }
}

function Resolve-WorkspacePath([string]$Path) {
  $candidate = $Path
  if (-not [IO.Path]::IsPathRooted($candidate)) {
    $candidate = Join-Path $Root $candidate
  }
  $full = [IO.Path]::GetFullPath($candidate)
  $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
  Require ($full.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) "path is outside the workspace"
  $full
}

function Relative-Path([string]$Path) {
  $full = Resolve-WorkspacePath $Path
  $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
  $full.Substring($rootPrefix.Length).Replace('\', '/')
}

function Get-FileEntry([string]$Path, [string]$Role) {
  $full = Resolve-WorkspacePath $Path
  $item = Get-Item -LiteralPath $full
  [pscustomobject][ordered]@{
    role = $Role
    path = Relative-Path $full
    sha256 = File-Hash $full
    bytes = [int64]$item.Length
  }
}

function Get-BuildClosureFiles {
  $files = New-Object System.Collections.Generic.List[System.IO.FileInfo]
  foreach ($directory in @('src', 'include', 'tests')) {
    $absolute = Join-Path $Root $directory
    foreach ($file in @(Get-ChildItem -LiteralPath $absolute -Recurse -File)) {
      if ($file.Extension -in @('.cpp', '.c', '.hpp', '.h')) {
        $files.Add($file)
      }
    }
  }
  foreach ($relative in @(
      'CMakeLists.txt',
      'scripts/build-cl.ps1',
      'scripts/build-tests-cl.ps1',
      'scripts/n19-verify.ps1',
      'third_party/nlohmann/json.hpp',
      'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.c',
      'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.h')) {
    $files.Add((Get-Item -LiteralPath (Join-Path $Root $relative)))
  }
  @($files | Sort-Object FullName -Unique)
}

function Get-ProtectedRepoFiles {
  $files = New-Object System.Collections.Generic.List[System.IO.FileInfo]
  foreach ($relative in @('.codex', '.claude', '.opencode')) {
    $directory = Join-Path $Root $relative
    if (Test-Path -LiteralPath $directory -PathType Container) {
      foreach ($file in @(Get-ChildItem -LiteralPath $directory -Recurse -File)) {
        $files.Add($file)
      }
    }
  }
  foreach ($file in @(Get-ChildItem -LiteralPath $Root -File)) {
    if ($file.Name -match '(?i)^(?:codex|claude|opencode|model-config|llm-config).*[.](?:json|toml|ya?ml)$') {
      $files.Add($file)
    }
  }
  @($files | Sort-Object FullName -Unique)
}

function Invoke-CapturedProcess(
  [string]$FilePath,
  [string]$Arguments,
  [int]$ProcessTimeoutSeconds,
  [string]$WorkingDirectory = $Root,
  [hashtable]$EnvironmentOverrides = @{},
  [bool]$SanitizeGitConfig = $false
) {
  $startInfo = New-Object System.Diagnostics.ProcessStartInfo
  $startInfo.FileName = $FilePath
  $startInfo.Arguments = $Arguments
  $startInfo.WorkingDirectory = $WorkingDirectory
  $startInfo.UseShellExecute = $false
  $startInfo.CreateNoWindow = $true
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true
  if ($SanitizeGitConfig) {
    foreach ($key in @($startInfo.EnvironmentVariables.Keys)) {
      if ($key -like 'GIT_CONFIG_*') {
        [void]$startInfo.EnvironmentVariables.Remove($key)
      }
    }
  }
  foreach ($key in $EnvironmentOverrides.Keys) {
    $startInfo.EnvironmentVariables[$key] = [string]$EnvironmentOverrides[$key]
  }

  $process = New-Object System.Diagnostics.Process
  $process.StartInfo = $startInfo
  $started = [DateTimeOffset]::UtcNow
  [void]$process.Start()
  $stdoutTask = $process.StandardOutput.ReadToEndAsync()
  $stderrTask = $process.StandardError.ReadToEndAsync()
  if (-not $process.WaitForExit($ProcessTimeoutSeconds * 1000)) {
    try { $process.Kill() } catch {}
    [void]$process.WaitForExit(5000)
    throw "process timed out after $ProcessTimeoutSeconds seconds: $FilePath"
  }
  $process.WaitForExit()
  $ended = [DateTimeOffset]::UtcNow
  [pscustomobject]@{
    stdout = $stdoutTask.Result
    stderr = $stderrTask.Result
    exit_code = $process.ExitCode
    started_utc = $started
    ended_utc = $ended
  }
}

function Invoke-Git([string]$Arguments) {
  $readOnlyArguments = "-c core.autocrlf=false -c core.safecrlf=false $Arguments"
  $result = Invoke-CapturedProcess 'git.exe' $readOnlyArguments 60 $Root @{} $true
  Require ($result.exit_code -eq 0) "read-only Git command failed"
  Require ([string]::IsNullOrWhiteSpace($result.stderr)) "read-only Git command wrote stderr"
  $result.stdout.Trim()
}

function Get-GitState {
  $head = Invoke-Git 'rev-parse HEAD'
  Require ($head -match '^[0-9a-fA-F]{40,64}$') "Git HEAD is not a commit id"
  $branch = Invoke-Git 'branch --show-current'
  $gitDirText = Invoke-Git 'rev-parse --git-dir'
  $gitDir = $gitDirText
  if (-not [IO.Path]::IsPathRooted($gitDir)) {
    $gitDir = Join-Path $Root $gitDir
  }
  $gitDir = [IO.Path]::GetFullPath($gitDir)
  $stateLines = New-Object System.Collections.Generic.List[string]
  $stateLines.Add("HEAD $($head.ToLowerInvariant())")
  $headLog = Join-Path $gitDir 'logs\HEAD'
  if (Test-Path -LiteralPath $headLog -PathType Leaf) {
    $stateLines.Add("LOG logs/HEAD $(File-Hash $headLog)")
  } else {
    $stateLines.Add('LOG logs/HEAD absent')
  }
  $remoteLogs = Join-Path $gitDir 'logs\refs\remotes'
  if (Test-Path -LiteralPath $remoteLogs -PathType Container) {
    foreach ($file in @(Get-ChildItem -LiteralPath $remoteLogs -Recurse -File | Sort-Object FullName)) {
      $relative = $file.FullName.Substring($gitDir.Length + 1).Replace('\', '/')
      $stateLines.Add("LOG $relative $(File-Hash $file.FullName)")
    }
  }
  [pscustomobject][ordered]@{
    head = $head.ToLowerInvariant()
    branch = $branch
    reference_log_fingerprint_sha256 = Text-Hash (($stateLines.ToArray()) -join "`n")
  }
}

function Get-ScopedDiffFacts {
  $pathArguments = ($N19ScopedDiffPaths | ForEach-Object { $_.Replace('\', '/') }) -join ' '
  $diff = Invoke-Git "diff --no-ext-diff --unified=0 HEAD -- $pathArguments"
  $nameOutput = Invoke-Git "diff --no-ext-diff --name-only HEAD -- $pathArguments"
  $changedNames = @($nameOutput -split '\r?\n' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  foreach ($name in $changedNames) {
    Require ($name -notmatch '(?i)(^|/)(?:[.]codex|[.]claude|[.]opencode)(/|$)') "protected configuration path appears in the N19 scoped diff"
    Require ($name -notmatch '(?i)(?:model|llm|provider).*(?:config|settings).*[.](?:json|toml|ya?ml)$') "protected configuration file appears in the N19 scoped diff"
  }
  $settingPattern = '(?i)\b(?:base[_-]?url|api[_-]?key|provider|model(?:[_-]?name)?|reasoning(?:[_-]?effort)?|context[_-]?(?:size|window)|compression[_-]?threshold)\b\s*(?:=|:)'
  $changedLines = @($diff -split '\r?\n' | Where-Object {
      ($_ -match '^[+-]') -and ($_ -notmatch '^(?:[+]{3}|[-]{3})')
    })
  $protectedAssignmentLines = @($changedLines | Where-Object { $_ -match $settingPattern })
  Require ($protectedAssignmentLines.Count -eq 0) "protected model/provider configuration assignment appears in the N19 scoped diff"
  [pscustomobject][ordered]@{
    diff_sha256 = Text-Hash $diff
    changed_path_count = $changedNames.Count
    protected_path_change_count = 0
    protected_assignment_change_count = 0
  }
}

function Get-FirstLine([string]$Path, [string]$Pattern) {
  $match = Select-String -LiteralPath $Path -Pattern $Pattern | Select-Object -First 1
  if ($null -eq $match) { return '' }
  $match.Line.Trim()
}

function Assert-DependencyContracts {
  $evidence = New-Object System.Collections.Generic.List[object]
  foreach ($dependency in $DependencyContracts) {
    $plan = Join-Path $Root "docs\nodes\$($dependency.Node)-PLAN.md"
    $planAudit = Join-Path $Root "docs\nodes\$($dependency.Node)-PLAN-AUDIT.md"
    $hardAudit = Join-Path $Root "docs\nodes\$($dependency.Node)-HARD-AUDIT.md"
    foreach ($path in @($plan, $planAudit, $hardAudit)) {
      Require (Test-Path -LiteralPath $path -PathType Leaf) "missing dependency artifact for $($dependency.Node)"
    }
    Require ((Get-FirstLine $plan '(?i)^\*\*Status\*\*:') -match '(?i)\bdone\b') "$($dependency.Node) is not done"
    $planVerdict = Get-FirstLine $planAudit '(?i)^\*\*VERDICT'
    $hardVerdict = Get-FirstLine $hardAudit '(?i)^\*\*VERDICT'
    Require ($planVerdict -match '(?i)\bPASS\b' -and $planVerdict -notmatch '(?i)\bFAIL\b') "$($dependency.Node) plan audit is not PASS"
    Require ($hardVerdict -match '(?i)\bPASS\b' -and $hardVerdict -notmatch '(?i)\bFAIL\b') "$($dependency.Node) outcome audit is not PASS"
    Require ((Get-Content -Raw -LiteralPath $planAudit) -match '(?i)Auditor[^\r\n]*Claude Code') "$($dependency.Node) plan auditor is not Claude Code"
    Require ((Get-Content -Raw -LiteralPath $hardAudit) -match '(?i)Auditor[^\r\n]*Claude Code') "$($dependency.Node) outcome auditor is not Claude Code"
    Require ((File-Hash $planAudit) -eq $dependency.Plan) "$($dependency.Node) plan-audit hash changed"
    Require ((File-Hash $hardAudit) -eq $dependency.Outcome) "$($dependency.Node) outcome-audit hash changed"
    $evidence.Add([pscustomobject][ordered]@{
        node = $dependency.Node
        plan_audit_sha256 = $dependency.Plan
        outcome_audit_sha256 = $dependency.Outcome
      })
  }
  $evidence.ToArray()
}

function Assert-N19Governance {
  $plan = Join-Path $Root 'docs\nodes\N19-PLAN.md'
  $audit = Join-Path $Root 'docs\nodes\N19-PLAN-AUDIT.md'
  Require (Test-Path -LiteralPath $plan -PathType Leaf) "N19 plan is missing"
  Require (Test-Path -LiteralPath $audit -PathType Leaf) "N19 plan audit is missing"
  $status = Get-FirstLine $plan '(?i)^\*\*Status\*\*:'
  $outcome = Get-FirstLine $plan '(?i)^\*\*Outcome audit\*\*:'
  $verdict = Get-FirstLine $audit '(?i)^\*\*VERDICT'
  Require ($status -match '(?i)^\*\*Status\*\*:\s*approved\s*$') "N19 plan is not approved"
  Require ($outcome -match '(?i)\bpending\b') "N19 outcome gate is no longer pending"
  Require ($verdict -match '(?i)\bPASS\b' -and $verdict -notmatch '(?i)\bFAIL\b') "N19 plan audit is not PASS"
  Require ((Get-Content -Raw -LiteralPath $audit) -match '(?i)Auditor[^\r\n]*Claude Code') "N19 plan auditor is not Claude Code"
  [pscustomobject][ordered]@{
    plan_sha256 = File-Hash $plan
    plan_audit_sha256 = File-Hash $audit
    plan_status = 'approved'
    outcome_audit = 'pending'
    plan_audit_verdict = 'PASS'
    plan_auditor = 'Claude Code'
  }
}

function Assert-PreCorrectiveSchemaGate([switch]$RequireCurrentGateBinary) {
  Require (Test-Path -LiteralPath $PreCorrectiveGatePath -PathType Leaf) "pre-corrective schema gate is missing"
  $gateFileHash = File-Hash $PreCorrectiveGatePath
  Require ($gateFileHash -ceq $PreCorrectiveGateExpected.file_sha256) "pre-corrective schema gate file hash changed"

  try {
    $gate = ConvertFrom-JsonPreservingDateStrings (Get-Content -Raw -LiteralPath $PreCorrectiveGatePath)
  } catch {
    throw "N19 evidence requirement failed: pre-corrective schema gate is invalid JSON"
  }
  Require-ExactJsonPropertyNames $gate @(
    'format_version', 'node', 'gate', 'state', 'started_utc', 'completed_utc',
    'command', 'execution_path', 'approved_plan_sha256', 'audited_draft_plan_sha256',
    'plan_audit_sha256', 'qbrain_sha256', 'git_head',
    'pre_corrective_input_manifest_sha256', 'pre_corrective_inputs', 'result',
    'isolation', 'protected_model_configuration_changed', 'commit_or_push_executed'
  ) 'pre-corrective schema gate'
  Require-JsonIntegerDefault $gate.format_version 1 'pre-corrective gate format_version'
  Require-JsonStringExact $gate.node 'N19' 'pre-corrective gate node'
  Require-JsonStringExact $gate.gate 'pre-corrective-schema-v12' 'pre-corrective gate identity'
  Require-JsonStringExact $gate.state 'passed' 'pre-corrective gate state'
  Require-JsonStringExact $gate.started_utc $PreCorrectiveGateExpected.started_utc 'pre-corrective gate started_utc'
  Require-JsonStringExact $gate.completed_utc $PreCorrectiveGateExpected.completed_utc 'pre-corrective gate completed_utc'
  Require-JsonStringExact $gate.command $PreCorrectiveGateExpected.command 'pre-corrective gate command'

  try {
    $startedUtc = [DateTimeOffset]::Parse(
      [string]$gate.started_utc,
      [Globalization.CultureInfo]::InvariantCulture,
      [Globalization.DateTimeStyles]::RoundtripKind)
    $completedUtc = [DateTimeOffset]::Parse(
      [string]$gate.completed_utc,
      [Globalization.CultureInfo]::InvariantCulture,
      [Globalization.DateTimeStyles]::RoundtripKind)
  } catch {
    throw "N19 evidence requirement failed: pre-corrective gate timestamps are invalid"
  }
  Require ($startedUtc -lt $completedUtc) "pre-corrective gate interval is not positive"
  Require ($completedUtc -le [DateTimeOffset]::UtcNow) "pre-corrective gate completion is in the future"
  $gateItem = Get-Item -LiteralPath $PreCorrectiveGatePath
  Require ($gateItem.LastWriteTimeUtc -ge $completedUtc.UtcDateTime) "pre-corrective gate file timestamp predates gate completion"

  Require ($gate.execution_path -is [Array]) "pre-corrective gate execution_path is not a JSON array"
  $executionPath = @($gate.execution_path)
  Require ($executionPath.Count -eq $PreCorrectiveExecutionPath.Count) "pre-corrective gate execution path length changed"
  for ($index = 0; $index -lt $PreCorrectiveExecutionPath.Count; ++$index) {
    Require-JsonStringExact $executionPath[$index] $PreCorrectiveExecutionPath[$index] "pre-corrective gate execution_path[$index]"
  }

  $expectedHashes = [ordered]@{
    approved_plan_sha256 = $PreCorrectiveGateExpected.approved_plan_sha256
    audited_draft_plan_sha256 = $PreCorrectiveGateExpected.audited_draft_plan_sha256
    plan_audit_sha256 = $PreCorrectiveGateExpected.plan_audit_sha256
    qbrain_sha256 = $PreCorrectiveGateExpected.qbrain_sha256
    git_head = $PreCorrectiveGateExpected.git_head
    pre_corrective_input_manifest_sha256 = $PreCorrectiveGateExpected.input_manifest_sha256
  }
  foreach ($property in $expectedHashes.Keys) {
    $value = $gate.$property
    Require ($value -is [string] -and $value -match '^[0-9a-f]+$') "pre-corrective gate $property is not lowercase hexadecimal"
    Require ([string]$value -ceq [string]$expectedHashes[$property]) "pre-corrective gate $property changed"
  }
  Require ([string]$gate.git_head -match '^[0-9a-f]{40}$') "pre-corrective gate git_head is not SHA-1"
  foreach ($property in @(
      'approved_plan_sha256', 'audited_draft_plan_sha256', 'plan_audit_sha256',
      'qbrain_sha256', 'pre_corrective_input_manifest_sha256')) {
    Require ([string]$gate.$property -match '^[0-9a-f]{64}$') "pre-corrective gate $property is not SHA-256"
  }

  $planPath = Join-Path $Root 'docs\nodes\N19-PLAN.md'
  $planAuditPath = Join-Path $Root 'docs\nodes\N19-PLAN-AUDIT.md'
  Require ((File-Hash $planPath) -ceq [string]$gate.approved_plan_sha256) "approved N19 plan is not the plan bound to the pre-corrective gate"
  Require ((File-Hash $planAuditPath) -ceq [string]$gate.plan_audit_sha256) "N19 plan audit is not the audit bound to the pre-corrective gate"
  $auditedPlanLine = Get-FirstLine $planAuditPath '(?i)^\*\*Plan SHA-256\*\*:'
  $auditedPlanMatch = [regex]::Match($auditedPlanLine, '^\*\*Plan SHA-256\*\*:\s*([0-9a-f]{64})\s*$')
  Require ($auditedPlanMatch.Success -and $auditedPlanMatch.Groups[1].Value -ceq [string]$gate.audited_draft_plan_sha256) "pre-corrective gate audited-draft hash is not bound to the Claude Code plan audit"
  $currentHead = (Invoke-Git 'rev-parse HEAD').ToLowerInvariant()
  Require ($currentHead -ceq [string]$gate.git_head) "Git HEAD differs from the pre-corrective gate"
  if ($RequireCurrentGateBinary) {
    Require (Test-Path -LiteralPath $Qbrain -PathType Leaf) "pre-corrective gate binary is missing before the official build"
    Require ((File-Hash $Qbrain) -ceq [string]$gate.qbrain_sha256) "current qbrain.exe is not the binary used by the pre-corrective gate"
  }

  Require ($gate.pre_corrective_inputs -is [Array]) "pre-corrective gate inputs are not a JSON array"
  $inputs = @($gate.pre_corrective_inputs)
  Require ($inputs.Count -eq $PreCorrectiveInputPaths.Count) "pre-corrective gate input count is not exact"
  $seenPaths = @{}
  $manifestRows = New-Object System.Collections.Generic.List[string]
  for ($index = 0; $index -lt $inputs.Count; ++$index) {
    $entry = $inputs[$index]
    Require-ExactJsonPropertyNames $entry @('path', 'sha256', 'bytes') "pre-corrective gate input[$index]"
    Require-JsonStringExact $entry.path $PreCorrectiveInputPaths[$index] "pre-corrective gate input[$index].path"
    Require (-not $seenPaths.ContainsKey([string]$entry.path)) "pre-corrective gate input path is duplicated: $($entry.path)"
    $seenPaths[[string]$entry.path] = $true
    Require ($entry.sha256 -is [string] -and [string]$entry.sha256 -match '^[0-9a-f]{64}$') "pre-corrective gate input hash is not lowercase SHA-256: $($entry.path)"
    Require-JsonInteger $entry.bytes "pre-corrective gate input byte count: $($entry.path)"
    Require ([int64]$entry.bytes -ge 0) "pre-corrective gate input byte count is negative: $($entry.path)"
    $manifestRows.Add("$($entry.path)`t$($entry.sha256)`t$([int64]$entry.bytes)")
  }
  # The captured gate canonicalized ordered rows with CRLF and no trailing separator.
  $manifestDigest = Text-Hash (($manifestRows.ToArray()) -join "`r`n")
  Require ($manifestDigest -ceq [string]$gate.pre_corrective_input_manifest_sha256) "pre-corrective gate input manifest digest is invalid"

  Require-ExactJsonPropertyNames $gate.result @('exit_code', 'ok', 'schema_version', 'stderr_empty') 'pre-corrective gate result'
  Require-JsonIntegerDefault $gate.result.exit_code 0 'pre-corrective gate exit_code'
  Require-JsonBooleanExact $gate.result.ok $true 'pre-corrective gate ok'
  Require-JsonIntegerDefault $gate.result.schema_version 12 'pre-corrective gate schema_version'
  Require-JsonBooleanExact $gate.result.stderr_empty $true 'pre-corrective gate stderr_empty'
  Require-ExactJsonPropertyNames $gate.isolation @(
    'localappdata_overridden', 'production_localappdata_not_used',
    'config_persisted', 'temporary_root_removed'
  ) 'pre-corrective gate isolation'
  Require-JsonBooleanExact $gate.isolation.localappdata_overridden $true 'pre-corrective gate LOCALAPPDATA override'
  Require-JsonBooleanExact $gate.isolation.production_localappdata_not_used $true 'pre-corrective gate production LOCALAPPDATA exclusion'
  Require-JsonBooleanExact $gate.isolation.config_persisted $false 'pre-corrective gate config persistence'
  Require-JsonBooleanExact $gate.isolation.temporary_root_removed $true 'pre-corrective gate temporary cleanup'
  Require-JsonBooleanExact $gate.protected_model_configuration_changed $false 'pre-corrective gate protected configuration flag'
  Require-JsonBooleanExact $gate.commit_or_push_executed $false 'pre-corrective gate Git mutation flag'

  $inputMap = @{}
  foreach ($entry in $inputs) { $inputMap[[string]$entry.path] = $entry }
  $changedPaths = New-Object System.Collections.Generic.List[string]
  $correctiveFacts = New-Object System.Collections.Generic.List[object]
  foreach ($path in $PreCorrectiveInputPaths) {
    $recorded = $inputMap[$path]
    $currentPath = Join-Path $Root $path
    Require (Test-Path -LiteralPath $currentPath -PathType Leaf) "pre-corrective gate input is now missing: $path"
    $currentItem = Get-Item -LiteralPath $currentPath
    $currentHash = File-Hash $currentPath
    if ($PreCorrectiveChangedInputPaths -ccontains $path) {
      Require ($currentHash -cne [string]$recorded.sha256) "corrective input did not change after the pre-corrective gate: $path"
      Require ($currentItem.LastWriteTimeUtc -gt $completedUtc.UtcDateTime) "corrective input timestamp does not follow the pre-corrective gate: $path"
      $changedPaths.Add($path)
      $correctiveFacts.Add([pscustomobject][ordered]@{
          path = $path
          gate_sha256 = [string]$recorded.sha256
          current_sha256 = $currentHash
          current_bytes = [int64]$currentItem.Length
          current_last_write_utc = $currentItem.LastWriteTimeUtc.ToString('o')
        })
    } else {
      Require ($currentHash -ceq [string]$recorded.sha256) "non-corrective gate input hash changed: $path"
      Require ([int64]$currentItem.Length -eq [int64]$recorded.bytes) "non-corrective gate input length changed: $path"
    }
  }
  Require (($changedPaths.ToArray() -join "`n") -ceq ($PreCorrectiveChangedInputPaths -join "`n")) "pre-corrective gate does not identify exactly the two corrective inputs"

  [pscustomobject][ordered]@{
    path = Relative-Path $PreCorrectiveGatePath
    gate_file_sha256 = $gateFileHash
    gate = 'pre-corrective-schema-v12'
    state = 'passed'
    started_utc = [string]$gate.started_utc
    completed_utc = [string]$gate.completed_utc
    command = [string]$gate.command
    execution_path = @($executionPath)
    approved_plan_sha256 = [string]$gate.approved_plan_sha256
    audited_draft_plan_sha256 = [string]$gate.audited_draft_plan_sha256
    plan_audit_sha256 = [string]$gate.plan_audit_sha256
    qbrain_sha256 = [string]$gate.qbrain_sha256
    git_head = [string]$gate.git_head
    pre_corrective_input_manifest_sha256 = [string]$gate.pre_corrective_input_manifest_sha256
    pre_corrective_input_count = $inputs.Count
    result = [pscustomobject][ordered]@{
      exit_code = 0
      ok = $true
      schema_version = 12
      stderr_empty = $true
    }
    isolation = [pscustomobject][ordered]@{
      localappdata_overridden = $true
      production_localappdata_not_used = $true
      config_persisted = $false
      temporary_root_removed = $true
    }
    ordering = [pscustomobject][ordered]@{
      unchanged_input_count = $inputs.Count - $correctiveFacts.Count
      corrective_input_count = $correctiveFacts.Count
      corrective_inputs = $correctiveFacts.ToArray()
      gate_completed_before_all_corrective_inputs = $true
    }
    protected_model_configuration_changed = $false
    commit_or_push_executed = $false
  }
}

function Assert-PreCorrectiveGateOutputIsolation {
  $gateFull = [IO.Path]::GetFullPath($PreCorrectiveGatePath)
  foreach ($path in @(
      $PrebuildManifestPath, $EvidenceManifestPath, $ReportPath,
      $ProductionEvidencePath, $TestBuildEvidencePath, $FullSuiteEvidencePath,
      $FocusedEvidencePath, $SnapshotEvidencePath, $McpSchemaEvidencePath,
      $SchemaSmokeEvidencePath, $PlatformEvidencePath,
      $ProductionBuildLog, $TestBuildLog)) {
    $candidate = Resolve-WorkspacePath $path
    Require (-not $candidate.Equals($gateFull, [StringComparison]::OrdinalIgnoreCase)) "verifier output path overlaps the pre-corrective schema gate"
  }
}

function Get-RegisteredTests {
  $path = Join-Path $Root 'tests\test_main.cpp'
  $text = Get-Content -Raw -LiteralPath $path
  $matches = [regex]::Matches($text, '\{\s*"([^"]+)"\s*,\s*test_[A-Za-z0-9_]+\s*\}')
  $names = @($matches | ForEach-Object { $_.Groups[1].Value })
  Require ($names.Count -ge 25) "registered suite is below the completed Wave 3 baseline"
  Require (($names | Sort-Object -Unique).Count -eq $names.Count) "registered test names are not unique"
  Require (@($names | Where-Object { $_ -ceq 'n19' }).Count -eq 1) "dedicated n19 test is not registered exactly once"
  $names
}

function Assert-NoExcludedNodeReference([string]$Text, [string]$Label) {
  Require ($Text -notmatch '(?i)(?<![A-Za-z0-9])N30(?![0-9])') "$Label contains an excluded-node reference"
}

function Assert-NoGitMutationCommandsInVerifierScope {
  foreach ($relative in @('scripts/build-cl.ps1', 'scripts/build-tests-cl.ps1', 'scripts/n19-verify.ps1')) {
    $text = Get-Content -Raw -LiteralPath (Join-Path $Root $relative)
    Require ($text -notmatch '(?i)\bgit(?:[.]exe)?\s+(?:commit|push)\b') "$relative contains a forbidden Git mutation command"
  }
}

function Get-QuotedSourceArray([string]$Path, [string]$VariableName) {
  $text = Get-Content -Raw -LiteralPath $Path
  $pattern = '(?s)\$' + [regex]::Escape($VariableName) + '\s*=\s*@\((.*?)\r?\n\)'
  $match = [regex]::Match($text, $pattern)
  Require ($match.Success) "cannot parse $VariableName from $(Relative-Path $Path)"
  $values = @([regex]::Matches($match.Groups[1].Value, '"([^"]+[.](?:cpp|c))"') | ForEach-Object { $_.Groups[1].Value })
  Require ($values.Count -gt 0) "$VariableName is empty"
  $values
}

function New-InputManifestEntries {
  $entries = New-Object System.Collections.Generic.List[object]
  foreach ($file in @(Get-BuildClosureFiles)) {
    $entries.Add((Get-FileEntry $file.FullName 'build-input'))
  }
  foreach ($relative in @('AGENTS.md', 'docs/nodes/README.md', 'docs/nodes/N19-PLAN.md', 'docs/nodes/N19-PLAN-AUDIT.md')) {
    $entries.Add((Get-FileEntry (Join-Path $Root $relative) 'governance-input'))
  }
  @($entries | Sort-Object path -Unique)
}

function Assert-NoExcludedManifestPath([object[]]$Entries) {
  foreach ($entry in @($Entries)) {
    Require ([string]$entry.path -notmatch '(?i)(?<![A-Za-z0-9])N30(?![0-9])') "manifest includes an excluded-node artifact"
  }
}

function Write-PendingReport([string]$PreparedUtc = '') {
  Require (-not [IO.Path]::GetFullPath($ReportPath).Equals(
      [IO.Path]::GetFullPath($PreCorrectiveGatePath), [StringComparison]::OrdinalIgnoreCase)) "pending report path overlaps the pre-corrective schema gate"
  $lines = @(
    '# N19 Runtime Verification Report',
    '',
    '**State: PENDING**',
    '',
    'This file is factual runtime-evidence scaffolding only. It is not a Claude Code plan audit or outcome hard-audit verdict.',
    '',
    'No official N19 verification result is recorded here. Diagnostic, partial, stale, or missing build output is not accepted.',
    ''
  )
  if (-not [string]::IsNullOrWhiteSpace($PreparedUtc)) {
    $lines += "- Pre-build manifest prepared: $PreparedUtc"
    $lines += "- Pre-build manifest SHA-256: $(File-Hash $PrebuildManifestPath)"
    $lines += ''
  }
  $lines += @(
    '## Required Sequence',
    '',
    '```powershell',
    'powershell -NoProfile -ExecutionPolicy Bypass -File scripts/n19-verify.ps1 -Prepare',
    'powershell -NoProfile -ExecutionPolicy Bypass -File scripts/n19-verify.ps1 -RunBuilds',
    '```',
    '',
    'The second command may instead consume separately captured metadata-wrapped logs through `-ProductionBuildLog` and `-TestBuildLog`.',
    '',
    'A successful verifier run replaces this pending template with exact commands, counts, hashes, N19 markers, snapshot facts, platform facts, and remaining outcome-audit requirements. Until then, N19 has no verifier success claim.'
  )
  Write-Utf8Lines $ReportPath $lines
}

function Write-PendingEvidenceFiles([bool]$IncludeBuildLogs) {
  $paths = @(
    $FullSuiteEvidencePath, $FocusedEvidencePath, $SnapshotEvidencePath,
    $McpSchemaEvidencePath, $SchemaSmokeEvidencePath, $PlatformEvidencePath
  )
  if ($IncludeBuildLogs) {
    $paths = @($ProductionEvidencePath, $TestBuildEvidencePath) + $paths
  }
  foreach ($path in $paths) {
    Require (-not [IO.Path]::GetFullPath($path).Equals(
        [IO.Path]::GetFullPath($PreCorrectiveGatePath), [StringComparison]::OrdinalIgnoreCase)) "pending evidence path overlaps the pre-corrective schema gate"
    Write-Utf8Lines $path @(
      'state=pending',
      'reason=current official N19 evidence has not completed'
    )
  }
}

function New-Preparation {
  Require ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) "preparation must run on native Windows"
  Require ([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString() -eq 'X64') "preparation process is not x64"
  $preCorrectiveGate = Assert-PreCorrectiveSchemaGate -RequireCurrentGateBinary
  Assert-PreCorrectiveGateOutputIsolation
  New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
  Write-PendingReport
  Write-PendingEvidenceFiles $true
  $preparingManifest = [pscustomobject][ordered]@{
    format_version = 1
    node = 'N19'
    state = 'pending'
    reason = 'pre-build preparation is in progress or did not complete'
  }
  Write-Utf8Text $EvidenceManifestPath (($preparingManifest | ConvertTo-Json -Depth 4) + [Environment]::NewLine)
  $governance = Assert-N19Governance
  $dependencies = @(Assert-DependencyContracts)
  Assert-NoGitMutationCommandsInVerifierScope
  $registeredTests = @(Get-RegisteredTests)
  $inputEntries = @(New-InputManifestEntries)
  Assert-NoExcludedManifestPath $inputEntries
  foreach ($relative in $N19Deliverables + $RelevantSchemaInputs) {
    Require (Test-Path -LiteralPath (Join-Path $Root $relative) -PathType Leaf) "required N19 file is missing: $relative"
  }
  $protectedEntries = @(
    Get-ProtectedRepoFiles | ForEach-Object { Get-FileEntry $_.FullName 'protected-repo-config' }
  )
  $git = Get-GitState
  $diff = Get-ScopedDiffFacts
  $prepared = [DateTimeOffset]::UtcNow.ToString('o')
  $manifest = [pscustomobject][ordered]@{
    format_version = 1
    node = 'N19'
    state = 'prepared-not-verified'
    prepared_utc = $prepared
    pre_corrective_schema_gate = $preCorrectiveGate
    governance = $governance
    dependency_contracts = $dependencies
    expected_registered_tests = $registeredTests.Count
    registered_test_names = $registeredTests
    git = $git
    scoped_diff = $diff
    protected_repo_config = $protectedEntries
    inputs = $inputEntries
  }
  Write-Utf8Text $PrebuildManifestPath (($manifest | ConvertTo-Json -Depth 8) + [Environment]::NewLine)
  $pendingManifest = [pscustomobject][ordered]@{
    format_version = 1
    node = 'N19'
    state = 'pending'
    reason = 'official native build and runtime evidence has not been verified'
    pre_corrective_schema_gate_sha256 = $preCorrectiveGate.gate_file_sha256
    prebuild_manifest_sha256 = File-Hash $PrebuildManifestPath
  }
  Write-Utf8Text $EvidenceManifestPath (($pendingManifest | ConvertTo-Json -Depth 4) + [Environment]::NewLine)
  Write-PendingReport $prepared
  Write-PendingEvidenceFiles $true
  Write-Host "N19_PREPARED expected_registered_tests=$($registeredTests.Count) manifest=$(File-Hash $PrebuildManifestPath)"
}

function Read-PrebuildManifest {
  Require (Test-Path -LiteralPath $PrebuildManifestPath -PathType Leaf) "missing PREBUILD-MANIFEST.json; run -Prepare before the official builds"
  try {
    $manifest = ConvertFrom-JsonPreservingDateStrings (Get-Content -Raw -LiteralPath $PrebuildManifestPath)
  } catch {
    throw "N19 evidence requirement failed: PREBUILD-MANIFEST.json is invalid JSON"
  }
  Require ($manifest.format_version -eq 1 -and $manifest.node -ceq 'N19') "pre-build manifest identity is invalid"
  Require ($manifest.state -ceq 'prepared-not-verified') "pre-build manifest is not in prepared state"
  $manifest
}

function Assert-EntrySetCurrent([object[]]$Recorded, [object[]]$Current, [string]$Label) {
  Require ($Recorded.Count -eq $Current.Count) "$Label file count changed after preparation"
  $recordedMap = @{}
  foreach ($entry in $Recorded) {
    Require (-not $recordedMap.ContainsKey([string]$entry.path)) "$Label contains a duplicate path"
    $recordedMap[[string]$entry.path] = $entry
  }
  foreach ($entry in $Current) {
    Require ($recordedMap.ContainsKey([string]$entry.path)) "$Label gained an unprepared path: $($entry.path)"
    $old = $recordedMap[[string]$entry.path]
    Require ([string]$old.sha256 -ceq [string]$entry.sha256) "$Label hash changed after preparation: $($entry.path)"
    Require ([int64]$old.bytes -eq [int64]$entry.bytes) "$Label length changed after preparation: $($entry.path)"
  }
}

function Assert-PreparationCurrent([object]$Manifest) {
  $preCorrectiveGate = Assert-PreCorrectiveSchemaGate
  Require ($null -ne $Manifest.pre_corrective_schema_gate) "pre-build manifest lacks the pre-corrective schema gate"
  $recordedGateJson = $Manifest.pre_corrective_schema_gate | ConvertTo-Json -Depth 8 -Compress
  $currentGateJson = $preCorrectiveGate | ConvertTo-Json -Depth 8 -Compress
  Require ($recordedGateJson -ceq $currentGateJson) "pre-corrective schema gate facts changed after preparation"
  $governance = Assert-N19Governance
  Assert-NoGitMutationCommandsInVerifierScope
  Require ($governance.plan_sha256 -ceq [string]$Manifest.governance.plan_sha256) "approved N19 plan changed after preparation"
  Require ($governance.plan_audit_sha256 -ceq [string]$Manifest.governance.plan_audit_sha256) "N19 plan audit changed after preparation"
  $dependencies = @(Assert-DependencyContracts)
  Require ($dependencies.Count -eq @($Manifest.dependency_contracts).Count) "dependency evidence count changed"
  $registered = @(Get-RegisteredTests)
  Require ($registered.Count -eq [int]$Manifest.expected_registered_tests) "registered test count changed after preparation"
  Require (($registered -join "`n") -ceq (@($Manifest.registered_test_names) -join "`n")) "registered test order changed after preparation"
  $currentInputs = @(New-InputManifestEntries)
  Assert-NoExcludedManifestPath $currentInputs
  Assert-EntrySetCurrent @($Manifest.inputs) $currentInputs 'build/governance input manifest'
  $currentProtected = @(Get-ProtectedRepoFiles | ForEach-Object { Get-FileEntry $_.FullName 'protected-repo-config' })
  Assert-EntrySetCurrent @($Manifest.protected_repo_config) $currentProtected 'protected repository configuration'
  $git = Get-GitState
  Require ($git.head -ceq [string]$Manifest.git.head) "Git HEAD changed after preparation; commit-free evidence interval is broken"
  Require ($git.reference_log_fingerprint_sha256 -ceq [string]$Manifest.git.reference_log_fingerprint_sha256) "Git reference logs changed after preparation"
  $diff = Get-ScopedDiffFacts
  Require ($diff.diff_sha256 -ceq [string]$Manifest.scoped_diff.diff_sha256) "N19 scoped diff changed after preparation"
  [pscustomobject]@{
    pre_corrective_schema_gate = $preCorrectiveGate
    governance = $governance
    dependencies = $dependencies
    registered_tests = $registered
    git = $git
    scoped_diff = $diff
    current_inputs = $currentInputs
  }
}

function Format-CapturedLog([string]$Command, [object]$Capture, [string[]]$Metadata) {
  $lines = New-Object System.Collections.Generic.List[string]
  $lines.Add("command=$Command")
  $lines.Add("started_utc=$($Capture.started_utc.ToString('o'))")
  foreach ($line in $Metadata) { $lines.Add($line) }
  if (-not [string]::IsNullOrEmpty($Capture.stdout)) {
    foreach ($line in @($Capture.stdout -split '\r?\n')) {
      if ($line -ne '') { $lines.Add($line) }
    }
  }
  if (-not [string]::IsNullOrEmpty($Capture.stderr)) {
    foreach ($line in @($Capture.stderr -split '\r?\n')) {
      if ($line -ne '') { $lines.Add($line) }
    }
  }
  $lines.Add("ended_utc=$($Capture.ended_utc.ToString('o'))")
  $lines.Add("exit_code=$($Capture.exit_code)")
  $lines.ToArray()
}

function Invoke-OfficialBuilds([int]$ExpectedRegisteredTests) {
  Assert-PreCorrectiveGateOutputIsolation
  $productionPath = Resolve-WorkspacePath $ProductionBuildLog
  $testPath = Resolve-WorkspacePath $TestBuildLog
  $prefix = 'qbrain_n19_build_'
  $sandbox = Join-Path ([IO.Path]::GetTempPath()) ($prefix + [guid]::NewGuid().ToString('N'))
  try {
    New-Item -ItemType Directory -Force -Path $sandbox | Out-Null
    $environment = @{ LOCALAPPDATA=$sandbox }
    $productionCommand = 'powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cl.ps1'
    $production = Invoke-CapturedProcess 'powershell.exe' '-NoProfile -ExecutionPolicy Bypass -File scripts\build-cl.ps1' $TimeoutSeconds $Root $environment
    Write-Utf8Lines $productionPath (Format-CapturedLog $productionCommand $production @(
        'target_arch=x64', 'language_mode=/std:c++20',
        'isolated_localappdata=true', 'production_qbrain_data_touched=false'))
    Require ($production.exit_code -eq 0) "official production build failed"
    Require (-not (Test-Path -LiteralPath (Join-Path $sandbox 'Qbrain\config.json'))) "official production build persisted application configuration"

    $testCommand = 'powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1 -SkipProductionBuild'
    $testBuild = Invoke-CapturedProcess 'powershell.exe' '-NoProfile -ExecutionPolicy Bypass -File scripts\build-tests-cl.ps1 -SkipProductionBuild' $TimeoutSeconds $Root $environment
    $testConfig = Assert-IsolatedTestConfig $sandbox 'official test build/full suite'
    Write-Utf8Lines $testPath (Format-CapturedLog $testCommand $testBuild @(
        'target_arch=x64', 'language_mode=/std:c++20',
        "expected_registered_tests=$ExpectedRegisteredTests",
        'isolated_localappdata=true', 'production_qbrain_data_touched=false',
        'isolated_test_config_policy=absent_or_canonical_defaults_only',
        "isolated_test_config_count=$($testConfig.count)",
        "isolated_test_config_sha256=$($testConfig.sha256)"))
    Require ($testBuild.exit_code -eq 0) "official test build/full suite failed"
  } finally {
    Remove-SafeTemporaryDirectory $sandbox $prefix
  }
}

function Get-EnvelopeValue([string[]]$Lines, [string]$Key, [string]$Label) {
  $matches = @($Lines | Where-Object { $_ -match ('^' + [regex]::Escape($Key) + '=') })
  Require ($matches.Count -eq 1) "$Label must contain exactly one $Key field"
  $matches[0].Substring($Key.Length + 1)
}

function Parse-Envelope([string]$Path, [string]$Label) {
  Require (Test-Path -LiteralPath $Path -PathType Leaf) "missing $Label log"
  $text = Get-Content -Raw -LiteralPath $Path
  Require (-not [string]::IsNullOrWhiteSpace($text)) "$Label log is empty"
  Assert-NoExcludedNodeReference $text "$Label log"
  Require ($text -notmatch '(?i)(?<![A-Za-z])(?:WSL|Docker)(?![A-Za-z])') "$Label log is not a native-Windows-only record"
  Require ($text -notmatch '(?i)\bgit(?:[.]exe)?\s+(?:commit|push)\b') "$Label log contains a forbidden Git mutation command"
  $lines = @($text -split '\r?\n' | Where-Object { $_ -ne '' })
  $startedText = Get-EnvelopeValue $lines 'started_utc' $Label
  $endedText = Get-EnvelopeValue $lines 'ended_utc' $Label
  try {
    $started = [DateTimeOffset]::Parse($startedText, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
    $ended = [DateTimeOffset]::Parse($endedText, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
  } catch {
    throw "N19 evidence requirement failed: $Label has an invalid UTC timestamp"
  }
  Require ($started.Offset -eq [TimeSpan]::Zero -and $ended.Offset -eq [TimeSpan]::Zero) "$Label timestamps are not UTC"
  Require ($ended -ge $started) "$Label ended before it started"
  [pscustomobject]@{
    path = $Path
    text = $text
    lines = $lines
    command = Get-EnvelopeValue $lines 'command' $Label
    started_utc = $started
    ended_utc = $ended
    exit_code = [int](Get-EnvelopeValue $lines 'exit_code' $Label)
  }
}

function Assert-CompiledSources([string[]]$Lines, [string[]]$Sources, [string]$Label) {
  foreach ($source in $Sources) {
    $leaf = [IO.Path]::GetFileName($source)
    Require (@($Lines | Where-Object { $_ -ceq $leaf }).Count -eq 1) "$Label did not compile $leaf exactly once"
  }
}

function Require-LabelCount([hashtable]$Counts, [string]$Label, [int]$Minimum) {
  $actual = 0
  if ($Counts.ContainsKey($Label)) { $actual = [int]$Counts[$Label] }
  Require ($actual -ge $Minimum) "N19 snapshot evidence lacks $Label (minimum $Minimum, observed $actual)"
}

function Require-ExactLabelCount([hashtable]$Counts, [string]$Label, [int]$Expected) {
  $actual = 0
  if ($Counts.ContainsKey($Label)) { $actual = [int]$Counts[$Label] }
  Require ($actual -eq $Expected) "N19 snapshot evidence count for $Label is not exact (expected $Expected, observed $actual)"
}

function Assert-N19Evidence([string[]]$Lines, [string]$Label) {
  $summaryLines = @($Lines | Where-Object {
      $_.StartsWith('[INFO] n19 ', [StringComparison]::Ordinal) -and
      -not $_.StartsWith('[INFO] n19 snapshot_call=', [StringComparison]::Ordinal)
    })
  Require ($summaryLines.Count -eq 1) "$Label must contain exactly one N19 summary marker"
  $summary = $summaryLines[0]
  foreach ($token in @(
      'schema_v12=pass', 'schema_reopen=pass', 'utc_boundaries=pass',
      'source_matrix=pass', 'strict_arguments=pass', 'identity=pass',
      'identity_exact_matrix=pass',
      'path_redaction=pass', 'context_query=pass', 'context_recent=pass',
      'context_fail_open=pass', 'utf8_bounds=pass', 'timeline=pass', 'chronicle=pass',
      'seven_day_default=pass', 'registry=pass', 'mcp_rpc=pass',
      'ambient_default=pass', 'selected_decoy=pass',
      'damaged_database=pass', 'read_only=pass')) {
    $tokenMatches = [regex]::Matches($summary, '(?:^|\s)' + [regex]::Escape($token) + '(?=\s|$)')
    Require ($tokenMatches.Count -eq 1) "$Label N19 marker does not contain exactly one $token"
  }
  $identityCellMatches = [regex]::Matches($summary, '(?:^|\s)identity_matrix_cells=([0-9]+)(?=\s|$)')
  Require ($identityCellMatches.Count -eq 1) "$Label N19 marker does not contain exactly one identity_matrix_cells integer"
  Require ($identityCellMatches[0].Groups[1].Value -ceq '4') "$Label N19 identity_matrix_cells is not exactly 4"
  $countMatch = [regex]::Match($summary, '(?:^|\s)snapshot_call_count=([1-9][0-9]*)(?:\s|$)')
  Require ($countMatch.Success) "$Label N19 marker lacks an exact positive snapshot count"
  $snapshotCount = [int]$countMatch.Groups[1].Value
  $selectedMatch = [regex]::Match($summary, '(?:^|\s)selected_snapshot_sha256=([0-9a-f]{64})(?:\s|$)')
  $decoyMatch = [regex]::Match($summary, '(?:^|\s)decoy_snapshot_sha256=([0-9a-f]{64})(?:\s|$)')
  Require ($selectedMatch.Success -and $decoyMatch.Success) "$Label N19 marker lacks final selected/decoy hashes"
  Require ($selectedMatch.Groups[1].Value -cne $decoyMatch.Groups[1].Value) "$Label selected and decoy snapshots are not distinct"

  $snapshotLines = @($Lines | Where-Object { $_.StartsWith('[INFO] n19 snapshot_call=', [StringComparison]::Ordinal) })
  Require ($snapshotLines.Count -eq $snapshotCount) "$Label snapshot row count does not match snapshot_call_count"
  $labelCounts = @{}
  $rows = New-Object System.Collections.Generic.List[object]
  $distinctBrainsObserved = $false
  for ($index = 0; $index -lt $snapshotLines.Count; ++$index) {
    $match = [regex]::Match(
      $snapshotLines[$index],
      '^\[INFO\] n19 snapshot_call=([1-9][0-9]*) label=([A-Za-z0-9_.:+-]+) selected_before_sha256=([0-9a-f]{64}) selected_after_sha256=([0-9a-f]{64}) decoy_before_sha256=([0-9a-f]{64}) decoy_after_sha256=([0-9a-f]{64})$')
    Require ($match.Success) "$Label contains a malformed N19 snapshot row"
    Require ([int]$match.Groups[1].Value -eq ($index + 1)) "$Label N19 snapshot indexes are not contiguous and ordered"
    $snapshotLabel = $match.Groups[2].Value
    $selectedBefore = $match.Groups[3].Value
    $selectedAfter = $match.Groups[4].Value
    $decoyBefore = $match.Groups[5].Value
    $decoyAfter = $match.Groups[6].Value
    Require ($selectedBefore -ceq $selectedAfter) "$Label selected snapshot changed during $snapshotLabel"
    Require ($decoyBefore -ceq $decoyAfter) "$Label decoy snapshot changed during $snapshotLabel"
    if ($selectedBefore -cne $decoyBefore) { $distinctBrainsObserved = $true }
    if (-not $labelCounts.ContainsKey($snapshotLabel)) { $labelCounts[$snapshotLabel] = 0 }
    $labelCounts[$snapshotLabel] = [int]$labelCounts[$snapshotLabel] + 1
    $rows.Add([pscustomobject]@{
        index = $index + 1
        label = $snapshotLabel
        line = $snapshotLines[$index]
      })
  }
  Require ($distinctBrainsObserved) "$Label never observed distinct selected and decoy snapshots"

  foreach ($required in @(
      'identity:remote-redaction',
      'context:query', 'context:query-repeat', 'context:reverse-fixture', 'context:recent',
      'context:query-4096', 'context:query-4097', 'context:query-malformed-utf8',
      'timeline:default', 'timeline:repeat', 'timeline:team',
      'chronicle:date-only', 'chronicle:timestamp-t', 'chronicle:timestamp-space',
      'chronicle:default-seven-days', 'chronicle:empty-window', 'chronicle:since-malformed-utf8',
      'registry:tools-list', 'mcp:empty-result', 'mcp:alias-conflict',
      'identity:damaged-database')) {
    Require-LabelCount $labelCounts $required 1
  }
  foreach ($identityLabel in @(
      'identity:matrix:selected:default', 'identity:matrix:selected:team',
      'identity:matrix:decoy:default', 'identity:matrix:decoy:team')) {
    Require-ExactLabelCount $labelCounts $identityLabel 1
  }
  foreach ($fallbackLabel in @('context:fallback', 'context:fallback-repeat', 'context:fallback-reverse')) {
    Require-ExactLabelCount $labelCounts $fallbackLabel 1
  }

  $operations = @('get_brain_identity', 'volunteer_context', 'get_timeline', 'volunteer_chronicle')
  foreach ($operation in $operations) {
    foreach ($suffix in @(
        'source:omitted', 'source:mixed-case', 'argument:unexpected',
        'source:remote-default', 'source:remote-denied',
        'source:allow-write-denied', 'source:remote-allowed')) {
      Require-LabelCount $labelCounts "$operation`:$suffix" 1
    }
    Require-LabelCount $labelCounts "$operation`:source:reject" 5
    foreach ($prefix in @(
        'mcp:authorized:', 'mcp:arguments-type:', 'mcp:unknown-field:',
        'mcp:unknown-source:', 'mcp:denied-source:', 'mcp:allowed-source:',
        'mcp:ambient-default:')) {
      Require-LabelCount $labelCounts "$prefix$operation" 1
    }
    Require-LabelCount $labelCounts "mcp:source-type:$operation" 5
  }
  foreach ($operation in @('volunteer_context', 'get_timeline', 'volunteer_chronicle')) {
    Require-LabelCount $labelCounts "$operation`:limit:reject" 8
    foreach ($suffix in @('limit:zero', 'limit:one', 'limit:max', 'limit:over-max')) {
      Require-LabelCount $labelCounts "$operation`:$suffix" 1
    }
    Require-LabelCount $labelCounts "mcp:limit-type:$operation" 7
  }

  [pscustomobject]@{
    summary = $summary
    identity_exact_matrix = $true
    identity_matrix_cells = 4
    snapshot_count = $snapshotCount
    selected_snapshot_sha256 = $selectedMatch.Groups[1].Value
    decoy_snapshot_sha256 = $decoyMatch.Groups[1].Value
    snapshot_lines = $snapshotLines
    rows = $rows.ToArray()
    label_counts = $labelCounts
  }
}

function Assert-TestResults([object]$Envelope, [string[]]$RegisteredTests, [string]$Label) {
  $resultRows = New-Object System.Collections.Generic.List[object]
  foreach ($line in $Envelope.lines) {
    $passMatch = [regex]::Match($line, '^\[PASS\]\s+([A-Za-z0-9_.-]+)\s*$')
    if ($passMatch.Success) {
      $resultRows.Add([pscustomobject]@{ name=$passMatch.Groups[1].Value; result='PASS'; line=$line })
      continue
    }
    $failMatch = [regex]::Match($line, '^\[FAIL\]\s+([A-Za-z0-9_.-]+)(?::.*)?$')
    if ($failMatch.Success) {
      $resultRows.Add([pscustomobject]@{ name=$failMatch.Groups[1].Value; result='FAIL'; line=$line })
    }
  }
  Require ($resultRows.Count -eq $RegisteredTests.Count) "$Label result count does not equal the exact registered count"
  for ($index = 0; $index -lt $RegisteredTests.Count; ++$index) {
    Require ($resultRows[$index].name -ceq $RegisteredTests[$index]) "$Label result order/name differs from test_main.cpp"
  }
  $passRows = @($resultRows | Where-Object { $_.result -ceq 'PASS' })
  $failRows = @($resultRows | Where-Object { $_.result -ceq 'FAIL' })
  Require ($Envelope.exit_code -eq 0) "$Label exit code is not zero"
  Require ($failRows.Count -eq 0) "$Label contains failing tests"
  Require ($passRows.Count -eq $RegisteredTests.Count) "$Label is not all passing"
  Require (@($passRows | Where-Object { $_.name -ceq 'n19' }).Count -eq 1) "$Label lacks exactly one passing n19 result"
  $n19 = Assert-N19Evidence $Envelope.lines $Label
  [pscustomobject]@{
    registered = $RegisteredTests.Count
    passed = $passRows.Count
    failed = $failRows.Count
    n19 = $n19
  }
}

function Assert-BuildLogs([object]$PreparationState) {
  $manifest = $PreparationState.manifest
  $preparedUtc = [DateTimeOffset]::Parse([string]$manifest.prepared_utc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
  $productionPath = Resolve-WorkspacePath $ProductionBuildLog
  $testPath = Resolve-WorkspacePath $TestBuildLog
  Require (-not $productionPath.Equals($testPath, [StringComparison]::OrdinalIgnoreCase)) "production and test-build logs must be distinct files"
  $reservedRuntimePaths = @(
    $FullSuiteEvidencePath, $FocusedEvidencePath, $SnapshotEvidencePath,
    $McpSchemaEvidencePath, $SchemaSmokeEvidencePath, $PlatformEvidencePath,
    $PreCorrectiveGatePath, $PrebuildManifestPath, $EvidenceManifestPath, $ReportPath
  ) | ForEach-Object { [IO.Path]::GetFullPath($_) }
  foreach ($reserved in $reservedRuntimePaths) {
    Require (-not $productionPath.Equals($reserved, [StringComparison]::OrdinalIgnoreCase)) "production log overlaps a verifier-generated runtime file"
    Require (-not $testPath.Equals($reserved, [StringComparison]::OrdinalIgnoreCase)) "test-build log overlaps a verifier-generated runtime file"
  }
  $production = Parse-Envelope $productionPath 'production build'
  $testBuild = Parse-Envelope $testPath 'test build/full suite'
  Require ($production.command -ceq 'powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cl.ps1') "production command is not canonical"
  Require ($testBuild.command -ceq 'powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1 -SkipProductionBuild') "test-build command is not canonical"
  Require ((Get-EnvelopeValue $production.lines 'target_arch' 'production build') -ceq 'x64') "production target is not x64"
  Require ((Get-EnvelopeValue $testBuild.lines 'target_arch' 'test build') -ceq 'x64') "test target is not x64"
  Require ((Get-EnvelopeValue $production.lines 'language_mode' 'production build') -ceq '/std:c++20') "production language mode is not C++20"
  Require ((Get-EnvelopeValue $testBuild.lines 'language_mode' 'test build') -ceq '/std:c++20') "test language mode is not C++20"
  Require ((Get-EnvelopeValue $production.lines 'isolated_localappdata' 'production build') -ceq 'true') "production build lacks LOCALAPPDATA isolation evidence"
  Require ((Get-EnvelopeValue $testBuild.lines 'isolated_localappdata' 'test build') -ceq 'true') "test build lacks LOCALAPPDATA isolation evidence"
  Require ((Get-EnvelopeValue $production.lines 'production_qbrain_data_touched' 'production build') -ceq 'false') "production build reports a production Qbrain data touch"
  Require ((Get-EnvelopeValue $testBuild.lines 'production_qbrain_data_touched' 'test build') -ceq 'false') "test build reports a production Qbrain data touch"
  Require ((Get-EnvelopeValue $testBuild.lines 'isolated_test_config_policy' 'test build') -ceq 'absent_or_canonical_defaults_only') "test-build config policy is not fail-closed"
  $testConfigCount = [int](Get-EnvelopeValue $testBuild.lines 'isolated_test_config_count' 'test build')
  $testConfigHash = Get-EnvelopeValue $testBuild.lines 'isolated_test_config_sha256' 'test build'
  Require ($testConfigCount -in @(0, 1)) "test-build isolated config count is invalid"
  Require (($testConfigCount -eq 0 -and $testConfigHash -ceq 'absent') -or
      ($testConfigCount -eq 1 -and $testConfigHash -match '^[0-9a-f]{64}$')) "test-build isolated config fingerprint is invalid"
  $recordedExpected = [int](Get-EnvelopeValue $testBuild.lines 'expected_registered_tests' 'test build')
  Require ($recordedExpected -eq $PreparationState.registered_tests.Count) "test-build expected count differs from prepared test_main.cpp"
  Require ($production.exit_code -eq 0) "production build exit code is not zero"
  Require ($testBuild.exit_code -eq 0) "test build/full-suite exit code is not zero"
  Require ($production.started_utc -ge $preparedUtc) "production build predates the pre-build manifest"
  Require ($testBuild.started_utc -ge $production.ended_utc) "test build did not start after the production build ended"
  Require ($testBuild.ended_utc -le [DateTimeOffset]::UtcNow.AddMinutes(5)) "test build timestamp is in the future"
  Require (@($production.lines | Where-Object { $_ -ceq 'BUILD_OK' }).Count -eq 1) "production log lacks exactly one BUILD_OK"
  Require (@($testBuild.lines | Where-Object { $_ -ceq 'TESTS_BUILD_OK' }).Count -eq 1) "test-build log lacks exactly one TESTS_BUILD_OK"
  Require (@($production.lines | Where-Object { $_ -match "Environment initialized for: 'x64'" }).Count -eq 1) "production log lacks x64 vcvars evidence"
  Require (@($testBuild.lines | Where-Object { $_ -match "Environment initialized for: 'x64'" }).Count -eq 1) "test log lacks x64 vcvars evidence"
  $productionSources = @(Get-QuotedSourceArray $BuildScript 'productionSources')
  $testSources = @(Get-QuotedSourceArray $TestBuildScript 'defaultTestSources')
  Assert-CompiledSources $production.lines ($productionSources + @('sqlite3.c')) 'production build'
  Assert-CompiledSources $testBuild.lines $testSources 'test build'
  Require (Test-Path -LiteralPath $Qbrain -PathType Leaf) "production binary is missing"
  Require (Test-Path -LiteralPath $Tests -PathType Leaf) "test binary is missing"
  $productionBinaryTime = [DateTimeOffset](Get-Item -LiteralPath $Qbrain).LastWriteTimeUtc
  $testBinaryTime = [DateTimeOffset](Get-Item -LiteralPath $Tests).LastWriteTimeUtc
  Require ($productionBinaryTime -ge $production.started_utc.AddMinutes(-1) -and $productionBinaryTime -le $production.ended_utc.AddMinutes(1)) "production binary timestamp is outside the production-build interval"
  Require ($testBinaryTime -ge $testBuild.started_utc.AddMinutes(-1) -and $testBinaryTime -le $testBuild.ended_utc.AddMinutes(1)) "test binary timestamp is outside the test-build interval"
  $testResults = Assert-TestResults $testBuild $PreparationState.registered_tests 'test build/full suite'
  [pscustomobject]@{
    production = $production
    test_build = $testBuild
    test_results = $testResults
    test_config_count = $testConfigCount
    test_config_sha256 = $testConfigHash
    production_binary_sha256 = File-Hash $Qbrain
    test_binary_sha256 = File-Hash $Tests
  }
}

function Get-PlatformEvidence {
  Require ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) "verification must run on native Windows"
  Require ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -eq 'X64') "Windows OS architecture is not x64"
  Require ([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString() -eq 'X64') "verification process architecture is not x64"
  Require (Test-Path -LiteralPath $VcVars -PathType Leaf) "MSVC vcvarsall.bat is missing"
  $arguments = '/d /c "call ^"' + $VcVars + '^" x64 >nul && echo target_arch=x64 && cl 2>&1"'
  $capture = Invoke-CapturedProcess 'cmd.exe' $arguments 120
  Require ($capture.exit_code -eq 0) "cannot invoke the x64 MSVC compiler"
  $combined = ($capture.stdout + "`n" + $capture.stderr)
  $versionLine = @($combined -split '\r?\n' | Where-Object { $_ -match 'Compiler Version .+ for x64' } | Select-Object -First 1)
  Require ($versionLine.Count -eq 1) "full x64 cl.exe version was not captured"
  $os = try {
    $value = Get-CimInstance Win32_OperatingSystem
    "$($value.Caption) $($value.Version) build $($value.BuildNumber)"
  } catch {
    [Environment]::OSVersion.VersionString
  }
  [pscustomobject][ordered]@{
    os = $os
    os_architecture = 'X64'
    process_architecture = 'X64'
    target_architecture = 'x64'
    language_mode = '/std:c++20'
    compiler = $versionLine[0].Trim()
  }
}

function Invoke-FullSuite([string[]]$RegisteredTests) {
  $binaryHashBefore = File-Hash $Tests
  $prefix = 'qbrain_n19_suite_'
  $sandbox = Join-Path ([IO.Path]::GetTempPath()) ($prefix + [guid]::NewGuid().ToString('N'))
  try {
    New-Item -ItemType Directory -Force -Path $sandbox | Out-Null
    $capture = Invoke-CapturedProcess $Tests '' $TimeoutSeconds $Root @{ LOCALAPPDATA=$sandbox }
    $testConfig = Assert-IsolatedTestConfig $sandbox 'verifier full suite'
    $metadata = @(
      "expected_registered_tests=$($RegisteredTests.Count)",
      "binary_sha256=$binaryHashBefore",
      'isolated_localappdata=true',
      'production_qbrain_data_touched=false',
      'isolated_test_config_policy=absent_or_canonical_defaults_only',
      "isolated_test_config_count=$($testConfig.count)",
      "isolated_test_config_sha256=$($testConfig.sha256)"
    )
    $lines = Format-CapturedLog 'build\cl\qbrain_tests.exe' $capture $metadata
    Write-Utf8Lines $FullSuiteEvidencePath $lines
  } finally {
    Remove-SafeTemporaryDirectory $sandbox $prefix
  }
  $binaryHashAfter = File-Hash $Tests
  Require ($binaryHashBefore -ceq $binaryHashAfter) "test binary changed during the full-suite run"
  $envelope = Parse-Envelope $FullSuiteEvidencePath 'verifier full suite'
  Require ($envelope.command -ceq 'build\cl\qbrain_tests.exe') "full-suite command is not canonical"
  Require ([int](Get-EnvelopeValue $envelope.lines 'expected_registered_tests' 'verifier full suite') -eq $RegisteredTests.Count) "full-suite expected count changed"
  Require ((Get-EnvelopeValue $envelope.lines 'binary_sha256' 'verifier full suite') -ceq $binaryHashBefore) "full-suite binary hash metadata is wrong"
  Require ((Get-EnvelopeValue $envelope.lines 'isolated_localappdata' 'verifier full suite') -ceq 'true') "full suite lacks LOCALAPPDATA isolation evidence"
  Require ((Get-EnvelopeValue $envelope.lines 'production_qbrain_data_touched' 'verifier full suite') -ceq 'false') "full suite reports a production Qbrain data touch"
  Require ((Get-EnvelopeValue $envelope.lines 'isolated_test_config_policy' 'verifier full suite') -ceq 'absent_or_canonical_defaults_only') "full-suite config policy is not fail-closed"
  $configCount = [int](Get-EnvelopeValue $envelope.lines 'isolated_test_config_count' 'verifier full suite')
  $configHash = Get-EnvelopeValue $envelope.lines 'isolated_test_config_sha256' 'verifier full suite'
  Require ($configCount -in @(0, 1)) "full-suite isolated config count is invalid"
  Require (($configCount -eq 0 -and $configHash -ceq 'absent') -or
      ($configCount -eq 1 -and $configHash -match '^[0-9a-f]{64}$')) "full-suite isolated config fingerprint is invalid"
  $results = Assert-TestResults $envelope $RegisteredTests 'verifier full suite'
  [pscustomobject]@{
    envelope=$envelope
    results=$results
    binary_sha256=$binaryHashBefore
    test_config_count=$configCount
    test_config_sha256=$configHash
  }
}

function Remove-SafeTemporaryDirectory([string]$Path, [string]$RequiredLeafPrefix) {
  $full = [IO.Path]::GetFullPath($Path)
  $tempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
  $leaf = [IO.Path]::GetFileName($full)
  Require ($full.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) "temporary cleanup target is outside the system temp directory"
  Require ($leaf.StartsWith($RequiredLeafPrefix, [StringComparison]::Ordinal)) "temporary cleanup target has an unexpected name"
  if (Test-Path -LiteralPath $full) {
    Remove-Item -LiteralPath $full -Recurse -Force
  }
}

function Invoke-SchemaSmoke {
  $prefix = 'qbrain_n19_verify_'
  $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ($prefix + [guid]::NewGuid().ToString('N'))
  $capture = $null
  $responseHash = ''
  try {
    New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
    $capture = Invoke-CapturedProcess $Qbrain 'doctor --json' 120 $Root @{ LOCALAPPDATA=$tempRoot }
    Require ($capture.exit_code -eq 0) "isolated schema smoke failed"
    Require ([string]::IsNullOrWhiteSpace($capture.stderr)) "isolated schema smoke wrote stderr"
    $raw = $capture.stdout.Trim()
    Assert-NoExcludedNodeReference $raw 'schema smoke response'
    try {
      $json = $raw | ConvertFrom-Json -ErrorAction Stop
    } catch {
      throw "N19 evidence requirement failed: isolated schema smoke did not return valid JSON"
    }
    Require ($json.ok -eq $true) "isolated schema integrity did not report ok=true"
    Require ([int]$json.schema_version -eq 12) "isolated schema version is not 12"
    Require (-not (Test-Path -LiteralPath (Join-Path $tempRoot 'Qbrain\config.json'))) "schema smoke persisted application configuration"
    $responseHash = Text-Hash $raw
  } finally {
    Remove-SafeTemporaryDirectory $tempRoot $prefix
  }
  [pscustomobject][ordered]@{
    command = 'build\cl\qbrain.exe doctor --json'
    exit_code = $capture.exit_code
    ok = $true
    schema_version = 12
    response_sha256 = $responseHash
    isolated_localappdata = $true
    application_config_persisted = $false
    production_qbrain_data_touched = $false
  }
}

function Copy-ValidatedLog([string]$Source, [string]$Destination) {
  $sourceFull = [IO.Path]::GetFullPath($Source)
  $destinationFull = [IO.Path]::GetFullPath($Destination)
  if (-not $sourceFull.Equals($destinationFull, [StringComparison]::OrdinalIgnoreCase)) {
    Copy-Item -LiteralPath $sourceFull -Destination $destinationFull -Force
  }
  Require ((File-Hash $sourceFull) -ceq (File-Hash $destinationFull)) "evidence log copy hash mismatch"
}

function Write-DerivedEvidence([object]$Runtime, [object]$Platform, [object]$SchemaSmoke) {
  $n19 = $Runtime.results.n19
  Write-Utf8Lines $FocusedEvidencePath @(
    "source_log_sha256=$(File-Hash $FullSuiteEvidencePath)",
    $n19.summary,
    '[PASS] n19'
  )
  Write-Utf8Lines $SnapshotEvidencePath (@(
    "source_log_sha256=$(File-Hash $FullSuiteEvidencePath)",
    "snapshot_call_count=$($n19.snapshot_count)",
    "selected_snapshot_sha256=$($n19.selected_snapshot_sha256)",
    "decoy_snapshot_sha256=$($n19.decoy_snapshot_sha256)"
  ) + @($n19.snapshot_lines))

  $mcpRows = @($n19.rows | Where-Object {
      $_.label -ceq 'registry:tools-list' -or $_.label.StartsWith('mcp:', [StringComparison]::Ordinal)
    } | ForEach-Object { $_.line })
  Write-Utf8Lines $McpSchemaEvidencePath (@(
      "source_log_sha256=$(File-Hash $FullSuiteEvidencePath)",
      'schema_v12=pass',
      'schema_reopen=pass',
      'registry_tools_list=pass',
      'real_mcp_tools_call_framing=pass',
      'mcp_writes_disabled=pass',
      'ambient_source_exclusion=pass',
      'schema get_brain_identity scope=Read local_only=false additionalProperties=false fields=source_id:string',
      'schema volunteer_context scope=Read local_only=false additionalProperties=false fields=source_id:string,query:string,q:string,limit:integer limit_default=8 limit_minimum=0 limit_maximum=50',
      'schema get_timeline scope=Read local_only=false additionalProperties=false fields=source_id:string,limit:integer limit_default=50 limit_minimum=0 limit_maximum=200',
      'schema volunteer_chronicle scope=Read local_only=false additionalProperties=false fields=source_id:string,since:string,limit:integer limit_default=50 limit_minimum=0 limit_maximum=200'
    ) + $mcpRows)

  Write-Utf8Lines $PlatformEvidencePath @(
    "os=$($Platform.os)",
    "os_architecture=$($Platform.os_architecture)",
    "process_architecture=$($Platform.process_architecture)",
    "target_architecture=$($Platform.target_architecture)",
    "language_mode=$($Platform.language_mode)",
    "compiler=$($Platform.compiler)"
  )
  Write-Utf8Lines $SchemaSmokeEvidencePath @(
    "command=$($SchemaSmoke.command)",
    "exit_code=$($SchemaSmoke.exit_code)",
    "ok=$($SchemaSmoke.ok.ToString().ToLowerInvariant())",
    "schema_version=$($SchemaSmoke.schema_version)",
    "response_sha256=$($SchemaSmoke.response_sha256)",
    'isolated_localappdata=true',
    'application_config_persisted=false',
    'production_qbrain_data_touched=false'
  )
}

function New-FinalEvidenceManifest(
  [object]$Preparation,
  [object]$Builds,
  [object]$Runtime,
  [object]$Platform,
  [object]$SchemaSmoke
) {
  $deliverables = @($N19Deliverables | ForEach-Object { Get-FileEntry (Join-Path $Root $_) 'n19-deliverable' })
  $schemaInputs = @($RelevantSchemaInputs | ForEach-Object { Get-FileEntry (Join-Path $Root $_) 'schema-or-sqlite-input' })
  Assert-NoExcludedManifestPath ($deliverables + $schemaInputs)
  $logs = @(
    Get-FileEntry $PreCorrectiveGatePath 'pre-corrective-schema-gate'
    Get-FileEntry $PrebuildManifestPath 'prebuild-manifest'
    Get-FileEntry $ProductionEvidencePath 'production-build-log'
    Get-FileEntry $TestBuildEvidencePath 'test-build-log'
    Get-FileEntry $FullSuiteEvidencePath 'full-suite-log'
    Get-FileEntry $FocusedEvidencePath 'focused-runtime-log'
    Get-FileEntry $SnapshotEvidencePath 'snapshot-evidence'
    Get-FileEntry $McpSchemaEvidencePath 'mcp-schema-evidence'
    Get-FileEntry $SchemaSmokeEvidencePath 'schema-smoke-evidence'
    Get-FileEntry $PlatformEvidencePath 'platform-evidence'
  )
  $manifest = [pscustomobject][ordered]@{
    format_version = 1
    node = 'N19'
    state = 'runtime-evidence-verified'
    generated_utc = [DateTimeOffset]::UtcNow.ToString('o')
    prebuild_manifest_sha256 = File-Hash $PrebuildManifestPath
    pre_corrective_schema_gate = $Preparation.pre_corrective_schema_gate
    governance = $Preparation.governance
    dependency_contracts = $Preparation.dependencies
    platform = $Platform
    commands = [pscustomobject][ordered]@{
      production_build = $Builds.production.command
      production_exit_code = $Builds.production.exit_code
      test_build_and_suite = $Builds.test_build.command
      test_build_exit_code = $Builds.test_build.exit_code
      verifier_full_suite = $Runtime.envelope.command
      verifier_full_suite_exit_code = $Runtime.envelope.exit_code
      isolated_schema_smoke = $SchemaSmoke.command
      isolated_schema_smoke_exit_code = $SchemaSmoke.exit_code
    }
    tests = [pscustomobject][ordered]@{
      registered = $Runtime.results.registered
      passed = $Runtime.results.passed
      failed = $Runtime.results.failed
      dedicated_n19 = 'PASS'
      n19_snapshot_call_count = $Runtime.results.n19.snapshot_count
      identity_matrix_cells = $Runtime.results.n19.identity_matrix_cells
    }
    n19 = [pscustomobject][ordered]@{
      marker = $Runtime.results.n19.summary
      identity_exact_matrix = $Runtime.results.n19.identity_exact_matrix
      identity_matrix_cells = $Runtime.results.n19.identity_matrix_cells
      selected_snapshot_sha256 = $Runtime.results.n19.selected_snapshot_sha256
      decoy_snapshot_sha256 = $Runtime.results.n19.decoy_snapshot_sha256
      damaged_fixture_snapshot_label_count = [int]$Runtime.results.n19.label_counts['identity:damaged-database']
      schema_version = $SchemaSmoke.schema_version
      real_mcp_framing = $true
      registry_schema = $true
      read_only_selected_and_decoy = $true
    }
    binaries = @(
      Get-FileEntry $Qbrain 'production-binary'
      Get-FileEntry $Tests 'test-binary'
    )
    deliverables = $deliverables
    relevant_schema_inputs = $schemaInputs
    evidence_files = $logs
    scoped_safety = [pscustomobject][ordered]@{
      excluded_node_artifact_count = 0
      excluded_node_build_test_runtime_reference_count = 0
      protected_repo_config_changed_during_evidence_interval = $false
      protected_setting_assignment_change_count = 0
      isolated_test_config_policy = 'absent_or_canonical_defaults_only'
      isolated_test_config_count = $Builds.test_config_count
      isolated_test_config_sha256 = $Builds.test_config_sha256
      git_head_changed_during_evidence_interval = $false
      git_reference_log_changed_during_evidence_interval = $false
      commit_or_push_command_executed_by_verifier = $false
      production_qbrain_data_touched = $false
      llm_or_provider_request_executed_by_n19 = $false
    }
  }
  Assert-NoExcludedManifestPath (@($manifest.deliverables) + @($manifest.relevant_schema_inputs) + @($manifest.evidence_files) + @($manifest.binaries))
  Write-Utf8Text $EvidenceManifestPath (($manifest | ConvertTo-Json -Depth 10) + [Environment]::NewLine)
  $manifest
}

function Write-FinalReport([object]$Manifest, [object]$Builds, [object]$Runtime) {
  $lines = New-Object System.Collections.Generic.List[string]
  $lines.Add('# N19 Runtime Verification Report')
  $lines.Add('')
  $lines.Add('This is factual runtime evidence only. It is not a Claude Code plan audit or outcome hard-audit verdict.')
  $lines.Add('')
  $lines.Add("- Generated: $($Manifest.generated_utc)")
  $lines.Add("- Plan status: approved")
  $lines.Add("- Outcome audit: pending")
  $lines.Add("- OS: $($Manifest.platform.os)")
  $lines.Add("- Process/target: X64/x64")
  $lines.Add("- Compiler: $($Manifest.platform.compiler)")
  $lines.Add('- Language mode: `/std:c++20`')
  $lines.Add("- Registered tests: $($Manifest.tests.registered)")
  $lines.Add("- Results: $($Manifest.tests.passed) PASS, $($Manifest.tests.failed) FAIL")
  $lines.Add("- N19 snapshot calls: $($Manifest.tests.n19_snapshot_call_count)")
  $lines.Add("- Evidence manifest SHA-256: $(File-Hash $EvidenceManifestPath)")
  $lines.Add('')
  $lines.Add('## Commands And Exit Codes')
  $lines.Add('')
  $lines.Add('| Command | Exit |')
  $lines.Add('|---|---:|')
  $lines.Add("| ``$($Manifest.commands.production_build)`` | $($Manifest.commands.production_exit_code) |")
  $lines.Add("| ``$($Manifest.commands.test_build_and_suite)`` | $($Manifest.commands.test_build_exit_code) |")
  $lines.Add("| ``$($Manifest.commands.verifier_full_suite)`` | $($Manifest.commands.verifier_full_suite_exit_code) |")
  $lines.Add("| ``$($Manifest.commands.isolated_schema_smoke)`` | $($Manifest.commands.isolated_schema_smoke_exit_code) |")
  $lines.Add('')
  $lines.Add('## Governance')
  $lines.Add('')
  $lines.Add("- Approved plan SHA-256: ``$($Manifest.governance.plan_sha256)``")
  $lines.Add("- Claude Code plan-audit SHA-256: ``$($Manifest.governance.plan_audit_sha256)``")
  $lines.Add('- The exact direct dependency audit hashes were rechecked against the approved plan and are recorded in `EVIDENCE-MANIFEST.json`.')
  $lines.Add('- The historical outcome audit is not used. A fresh Claude Code outcome audit remains blocking.')
  $lines.Add('')
  $lines.Add('## Pre-Corrective Schema Gate')
  $lines.Add('')
  $gate = $Manifest.pre_corrective_schema_gate
  $lines.Add("- Artifact: ``$($gate.path)``")
  $lines.Add("- Artifact SHA-256: ``$($gate.gate_file_sha256)``")
  $lines.Add("- Interval: $($gate.started_utc) to $($gate.completed_utc)")
  $lines.Add("- Command: ``$($gate.command)``")
  $lines.Add("- Execution path: $(@($gate.execution_path) -join ' -> ')")
  $lines.Add("- Result: exit_code=$($gate.result.exit_code), ok=true, schema_version=$($gate.result.schema_version), stderr_empty=true")
  $lines.Add("- Gate binary SHA-256: ``$($gate.qbrain_sha256)``")
  $lines.Add("- Pre-corrective input manifest: $($gate.pre_corrective_input_count) files, SHA-256 ``$($gate.pre_corrective_input_manifest_sha256)``")
  $lines.Add("- Ordering proof: $($gate.ordering.unchanged_input_count) inputs remain byte-for-byte unchanged; exactly $($gate.ordering.corrective_input_count) corrective inputs have different hashes and timestamps strictly after gate completion.")
  $lines.Add('- Isolation proof: temporary `LOCALAPPDATA` was used, production data was not used, no config persisted, and the temporary root was removed.')
  $lines.Add('- The gate records no protected model-configuration change and no commit or push.')
  $lines.Add('')
  $lines.Add('## Focused Runtime')
  $lines.Add('')
  $lines.Add('```text')
  $lines.Add($Runtime.results.n19.summary)
  $lines.Add('[PASS] n19')
  $lines.Add('```')
  $lines.Add('')
  $lines.Add("Every one of the $($Runtime.results.n19.snapshot_count) emitted snapshot rows was parsed, indexed contiguously, and required identical selected-before/after and decoy-before/after SHA-256 values. The selected and decoy final hashes are distinct. The damaged-database call has its own unchanged snapshot row and structured-database-error marker.")
  $lines.Add('')
  $lines.Add('The marker and required snapshot-label matrix cover schema v12/reopen, the exact four-cell selected/decoy by default/team identity matrix, strict source and numeric validation, local/remote path redaction, conservative context query/recent/fail-open determinism, timeline, seven-day Chronicle boundaries, tools/list schema inspection, real tools/call framing with writes disabled, ambient-source exclusion, authorization, selected/decoy isolation, and read-only failures.')
  $lines.Add('')
  $lines.Add('## Evidence Files')
  $lines.Add('')
  $lines.Add('| Role | Path | SHA-256 |')
  $lines.Add('|---|---|---|')
  foreach ($entry in @($Manifest.evidence_files)) {
    $lines.Add("| $($entry.role) | ``$($entry.path)`` | ``$($entry.sha256)`` |")
  }
  $lines.Add('')
  $lines.Add('## Deliverable Hashes')
  $lines.Add('')
  $lines.Add('| Path | SHA-256 |')
  $lines.Add('|---|---|')
  foreach ($entry in @($Manifest.deliverables)) {
    $lines.Add("| ``$($entry.path)`` | ``$($entry.sha256)`` |")
  }
  $lines.Add('')
  $lines.Add('## Scoped Safeguards')
  $lines.Add('')
  $lines.Add('- Build, test, and runtime logs contain zero N30 references, and the scoped manifest contains no N30 artifact path.')
  $lines.Add('- N19 scoped diff checks found no protected model/provider/base URL/API key/reasoning/context/compression setting assignment or protected configuration path.')
  $lines.Add('- Protected repository configuration hashes, Git HEAD, and Git reference-log fingerprints remained unchanged from preparation through verification.')
  $lines.Add('- The verifier executed no commit or push command. Its exact allowed commands are listed above.')
  $lines.Add("- Both complete-suite sandboxes produced the same isolated config result (count $($Manifest.scoped_safety.isolated_test_config_count), SHA-256 ``$($Manifest.scoped_safety.isolated_test_config_sha256)``). When present, the file had only exact canonical defaults, no key/allowlist/extra fields, and was deleted with its sandbox.")
  $lines.Add('- Schema smoke used a unique temporary LOCALAPPDATA, persisted no application configuration, and did not touch production `%LOCALAPPDATA%\Qbrain` data.')
  $lines.Add('- No live LLM/provider request is part of the N19 focused marker matrix.')
  $lines.Add('')
  $lines.Add('## Result')
  $lines.Add('')
  $lines.Add('All scripted N19 evidence checks completed against the prepared, current native build closure. This does not mark N19 done. A fresh node-specific Claude Code outcome hard audit against the approved plan and these evidence files is still required.')
  Write-Utf8Lines $ReportPath $lines.ToArray()
}

function Invoke-Verification {
  $preCorrectiveGate = Assert-PreCorrectiveSchemaGate
  Assert-PreCorrectiveGateOutputIsolation
  New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
  Write-PendingReport
  Write-PendingEvidenceFiles $false
  $startingManifest = [pscustomobject][ordered]@{
    format_version = 1
    node = 'N19'
    state = 'pending'
    reason = 'verification was requested but has not completed'
    pre_corrective_schema_gate_sha256 = $preCorrectiveGate.gate_file_sha256
  }
  Write-Utf8Text $EvidenceManifestPath (($startingManifest | ConvertTo-Json -Depth 4) + [Environment]::NewLine)
  $manifest = Read-PrebuildManifest
  $current = Assert-PreparationCurrent $manifest
  $current | Add-Member -NotePropertyName manifest -NotePropertyValue $manifest
  Write-PendingReport ([string]$manifest.prepared_utc)
  $pendingManifest = [pscustomobject][ordered]@{
    format_version = 1
    node = 'N19'
    state = 'pending'
    reason = 'verification is in progress or did not complete'
    pre_corrective_schema_gate_sha256 = $preCorrectiveGate.gate_file_sha256
    prebuild_manifest_sha256 = File-Hash $PrebuildManifestPath
  }
  Write-Utf8Text $EvidenceManifestPath (($pendingManifest | ConvertTo-Json -Depth 4) + [Environment]::NewLine)
  Write-PendingEvidenceFiles $false

  if ($RunBuilds) {
    $gateBeforeBuild = Assert-PreCorrectiveSchemaGate -RequireCurrentGateBinary
    Require ($gateBeforeBuild.gate_file_sha256 -ceq $preCorrectiveGate.gate_file_sha256) "pre-corrective schema gate changed before the official builds"
    Invoke-OfficialBuilds $current.registered_tests.Count
  }
  $builds = Assert-BuildLogs $current
  $platform = Get-PlatformEvidence
  $runtime = Invoke-FullSuite $current.registered_tests
  Require ($runtime.test_config_count -eq $builds.test_config_count) "complete-suite isolated config counts differ"
  Require ($runtime.test_config_sha256 -ceq $builds.test_config_sha256) "complete-suite isolated config fingerprints differ"
  $schemaSmoke = Invoke-SchemaSmoke

  # Detect any source, governance, protected-config, or Git change that raced the
  # official build/runtime interval before publishing final evidence.
  $finalCurrent = Assert-PreparationCurrent $manifest
  Require ((File-Hash $Qbrain) -ceq $builds.production_binary_sha256) "production binary changed after build-log validation"
  Require ((File-Hash $Tests) -ceq $builds.test_binary_sha256) "test binary changed after build-log validation"

  Copy-ValidatedLog $builds.production.path $ProductionEvidencePath
  Copy-ValidatedLog $builds.test_build.path $TestBuildEvidencePath
  Write-DerivedEvidence $runtime $platform $schemaSmoke

  foreach ($path in @(
      $ProductionEvidencePath, $TestBuildEvidencePath, $FullSuiteEvidencePath,
      $FocusedEvidencePath, $SnapshotEvidencePath, $McpSchemaEvidencePath,
      $SchemaSmokeEvidencePath, $PlatformEvidencePath)) {
    Assert-NoExcludedNodeReference (Get-Content -Raw -LiteralPath $path) (Relative-Path $path)
  }

  $finalManifest = New-FinalEvidenceManifest $finalCurrent $builds $runtime $platform $schemaSmoke
  $gitAfterEvidence = Get-GitState
  Require ($gitAfterEvidence.head -ceq [string]$manifest.git.head) "Git HEAD changed while evidence files were written"
  Require ($gitAfterEvidence.reference_log_fingerprint_sha256 -ceq [string]$manifest.git.reference_log_fingerprint_sha256) "Git reference logs changed while evidence files were written"
  Write-FinalReport $finalManifest $builds $runtime
  Write-Host "N19_VERIFY_OK registered=$($runtime.results.registered) pass=$($runtime.results.passed) fail=$($runtime.results.failed) snapshots=$($runtime.results.n19.snapshot_count)"
}

try {
  if ($Prepare) {
    New-Preparation
  } else {
    Invoke-Verification
  }
  exit 0
} catch {
  Write-Error $_.Exception.Message
  exit 1
}
