# N23 evidence verifier. This script records factual native evidence only. It
# never writes an audit verdict, changes a node status, or updates the ledger.
[CmdletBinding(DefaultParameterSetName = 'Verify')]
param(
  [Parameter(Mandatory = $true, ParameterSetName = 'Prepare')]
  [switch]$Prepare,

  [Parameter(Mandatory = $true, ParameterSetName = 'ParserSelfTest')]
  [switch]$ParserSelfTest,

  [Parameter(Mandatory = $true, ParameterSetName = 'Verify')]
  [switch]$RunBuilds
)

$ErrorActionPreference = 'Stop'
if (Test-Path Variable:PSNativeCommandUseErrorActionPreference) {
  $PSNativeCommandUseErrorActionPreference = $false
}
foreach ($item in @(Get-ChildItem Env: | Where-Object { $_.Name -match '^GIT_' })) {
  Remove-Item -LiteralPath ('Env:' + $item.Name) -ErrorAction SilentlyContinue
}

$Root = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$EvidenceDir = Join-Path $Root 'docs\nodes\n23-evidence'
$GatePath = Join-Path $EvidenceDir 'PRE-CORRECTIVE-SCHEMA-GATE.json'
$PostGateSidecarPath = Join-Path $EvidenceDir 'POST-GATE-SHARED-INPUT-REVALIDATION-v1.json'
$HistoricalPlanPath = Join-Path $EvidenceDir 'N23-PLAN-APPROVED-BASELINE.md'
$HistoricalPlanAuditPath = Join-Path $EvidenceDir 'N23-PLAN-AUDIT-BASELINE.md'
$HistoricalOutcomeAuditPath = Join-Path $EvidenceDir 'N23-HARD-AUDIT-BASELINE.md'
$PrebuildPath = Join-Path $EvidenceDir 'PREBUILD-MANIFEST.json'
$ManifestPath = Join-Path $EvidenceDir 'EVIDENCE-MANIFEST.json'
$ReportPath = Join-Path $EvidenceDir 'VERIFY-REPORT.md'
$FocusedPath = Join-Path $EvidenceDir 'FOCUSED-RUNTIME-OUTPUT.txt'
$SnapshotPath = Join-Path $EvidenceDir 'SNAPSHOT-EVIDENCE.txt'
$SchemaPath = Join-Path $EvidenceDir 'SCHEMA-SMOKE-OUTPUT.txt'
$PlatformPath = Join-Path $EvidenceDir 'PLATFORM-OUTPUT.txt'
$ProductionTreePath = Join-Path $EvidenceDir 'PRODUCTION-TREE-OUTPUT.txt'
$DefaultProductionLog = Join-Path $EvidenceDir 'PRODUCTION-BUILD-OUTPUT.txt'
$DefaultTestLog = Join-Path $EvidenceDir 'TEST-BUILD-AND-SUITE-RUN-1.txt'
$DefaultSecondLog = Join-Path $EvidenceDir 'FULL-SUITE-RUN-2.txt'
$PlanPath = Join-Path $Root 'docs\nodes\N23-PLAN.md'
$PlanAuditPath = Join-Path $Root 'docs\nodes\N23-PLAN-AUDIT.md'
$BuildScript = Join-Path $Root 'scripts\build-cl.ps1'
$TestBuildScript = Join-Path $Root 'scripts\build-tests-cl.ps1'
$VerifierPath = Join-Path $Root 'scripts\n23-verify.ps1'
$Qbrain = Join-Path $Root 'build\cl\qbrain.exe'
$Tests = Join-Path $Root 'build\cl\qbrain_tests.exe'
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$StrictUtf8 = New-Object System.Text.UTF8Encoding($false, $true)
$MaxCapturedStreamBytes = 32MB
$MaxImportedLogBytes = 33MB
$MaxJsonBytes = 32MB
$MaxSidecarJsonDepth = 32
$MaxHashBytes = 512MB
$MaxTreeEntries = 100000
$MaxTreeDepth = 128
$MaxTreePathCharacters = 32768
$MaxOutputLineCount = 2000000
$ProductionBuildTimeoutSeconds = 1800
$TestBuildTimeoutSeconds = 2400
$SuiteTimeoutSeconds = 1200
$DoctorTimeoutSeconds = 120
$ToolDiscoveryTimeoutSeconds = 120
$MaximumProcessTimeoutSeconds = 3600
$MinimumRegisteredTestCount = 27
$EvidenceBuildMutexName = 'Local\Qbrain.N23.EvidenceBuild'
$EvidenceBuildMutexWaitMilliseconds = 0
$EvidenceInitialized = $false
$PendingPreparedUtc = $null
$PendingPrebuildHash = $null
$PendingEvidenceNonce = $null
$PendingPostGateSidecarHash = $null
$VerificationRunNonce = $null
$OfficialRunsCompleted = $false
$script:StageBindingRecords = $null
$script:EvidenceBuildMutex = $null
$script:EvidenceBuildMutexHeld = $false
$script:EvidenceBuildMutexAbandoned = $false
$ActiveLogRoot = $EvidenceDir
$ProductionBuildLog = $DefaultProductionLog
$TestBuildLog = $DefaultTestLog
$SecondSuiteLog = $DefaultSecondLog

$ProductionCommand = 'WindowsPowerShell -NoProfile -ExecutionPolicy Bypass -File scripts\build-cl.ps1'
$TestBuildCommand = 'WindowsPowerShell -NoProfile -ExecutionPolicy Bypass -File scripts\build-tests-cl.ps1 -SkipProductionBuild'
$SecondSuiteCommand = 'build\cl\qbrain_tests.exe'
$ExpectedN23SnapshotLabelCount = 124
$ExpectedN23SnapshotLabelsHash = '6a951bcc75cbd4fc8afbe56b2b736c2154ec5ce786ce2a983240123f7c7cbbf9'
$ExpectedGateHash = '06c1d9fb4527993267534afe3780ce5ac5c2ead5e12f6e2018c471aad2839122'
$ExpectedHistoricalApprovedPlanHash = '4301f77a8a7b427a3d27bd883ce21d0316a94a5261a10541f9b0a2a3f6942a17'
$ExpectedHistoricalAuditedDraftHash = '138d68f638bea13f1bc8198339d6e1d0422fa11a85185c7a49698fde87bb5398'
$ExpectedHistoricalPlanAuditHash = '0bff70920652adbe9929d0cd76697fadaa9912a9014ae6b41ddccb648c869aef'
$ExpectedHistoricalOutcomeAuditHash = '0ab5517a77331de181550b4365244a86a8d0c4c9a9883b267837df017a579ebb'
$ExpectedCurrentApprovedPlanHash = '72186f2fdcc2289eda49b69e1cb56367537bc27691fdec7ea9377884afcad2fa'
$ExpectedCurrentAuditedDraftHash = '9601d6bc7665b52ac5871073d900c6b61763d1c5c440d88df68c8fbd6397a940'
$ExpectedCurrentPlanAuditHash = '8da7e7af958e4d776885970c4cfddc581376582e94c3797b9309a43a5297b714'
$ExpectedPostGateSidecarHash = 'c2dc2a34fc9928c253456210dce94f4a6fb239933906d8b896a1146bdec37149'
$ExpectedGateManifestHash = 'acb0ece63c81e933b5c579d9bbb6ecc5b5f221daa2673059c2126c0d423d5eac'
$ExpectedGitHead = '5ced8ccb511672536d0f9767a2bc1777baf561ab'
$ExpectedChangedGateInputs = @(
  'CMakeLists.txt',
  'include/qbrain/core/brain.hpp',
  'scripts/build-tests-cl.ps1',
  'src/qbrain/core/brain.cpp',
  'src/qbrain/mcp/server.cpp',
  'src/qbrain/ops/handlers.cpp',
  'tests/test_main.cpp',
  'tests/test_n20_23.cpp'
)
$ExpectedSharedInputDriftPaths = @(
  'scripts/build-cl.ps1',
  'src/qbrain/ops/registry.cpp'
)
$TrustedRemovedBuildProcessLine = 'powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-CimInstance Win32_Process -Filter ''name=''''qbrain.exe'''''' | Where-Object { `$_.ExecutablePath -eq ''$Out\qbrain.exe'' } | ForEach-Object { Stop-Process -Id `$_.ProcessId -Force }"'
$TrustedBuildDeltaAnchor = 'link /nologo /OUT:qbrain.exe'
$ScopedPaths = @(
  'include/qbrain/core/brain.hpp',
  'src/qbrain/core/brain.cpp',
  'src/qbrain/ops/handlers.cpp',
  'src/qbrain/mcp/server.cpp',
  'tests/test_n23.cpp',
  'tests/test_main.cpp',
  'CMakeLists.txt',
  'scripts/build-tests-cl.ps1',
  'scripts/n23-verify.ps1'
)
function Require([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw "N23 evidence requirement failed: $Message" }
}

function Enter-EvidenceBuildMutex {
  Require ($null -eq $script:EvidenceBuildMutex -and
           -not $script:EvidenceBuildMutexHeld) `
      'evidence/build mutex was already initialized in this process'
  $created = $false
  $mutex = $null
  try {
    $mutex = New-Object System.Threading.Mutex($false, $EvidenceBuildMutexName,
                                               [ref]$created)
    $acquired = $false
    try {
      $acquired = $mutex.WaitOne($EvidenceBuildMutexWaitMilliseconds)
    } catch [System.Threading.AbandonedMutexException] {
      # Ownership is transferred to this process when an abandoned mutex is
      # observed. Continue only after recording that recovery fact.
      $acquired = $true
      $script:EvidenceBuildMutexAbandoned = $true
    }
    Require ($acquired) 'another N23 evidence/build process holds the named mutex'
    $script:EvidenceBuildMutex = $mutex
    $script:EvidenceBuildMutexHeld = $true
  } catch {
    if ($null -ne $mutex) { $mutex.Dispose() }
    throw
  }
}

function Exit-EvidenceBuildMutex {
  $mutex = $script:EvidenceBuildMutex
  $script:EvidenceBuildMutex = $null
  if ($null -eq $mutex) {
    $script:EvidenceBuildMutexHeld = $false
    return
  }
  try {
    if ($script:EvidenceBuildMutexHeld) { $mutex.ReleaseMutex() }
  } finally {
    $script:EvidenceBuildMutexHeld = $false
    $mutex.Dispose()
  }
}

function Assert-EvidenceBuildMutexHeld {
  Require ($script:EvidenceBuildMutexHeld -and
           $null -ne $script:EvidenceBuildMutex) `
      'formal N23 work is not holding the process-wide evidence/build mutex'
}

function Test-N30ArtifactPath([string]$Path) {
  if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
  # This is intentionally broader than the textual N30-reference check. Any
  # path component containing the excluded node token is rejected before a
  # directory is enumerated or a file is opened.
  foreach ($component in @($Path.Replace('/', '\') -split '\\')) {
    if ($component -match '(?i)n30') { return $true }
  }
  $false
}

function Assert-NoN30ArtifactPath([string]$Path, [string]$Label) {
  Require (-not (Test-N30ArtifactPath $Path)) `
      "$Label references an excluded N30 artifact path"
  $Path
}

function Require-ExactProperties([object]$Value, [string[]]$Expected, [string]$Label) {
  Require ($null -ne $Value) "$Label is null"
  $actual = @($Value.PSObject.Properties | ForEach-Object { $_.Name } | Sort-Object)
  $wanted = @($Expected | Sort-Object)
  Require (($actual -join "`n") -ceq ($wanted -join "`n")) "$Label properties are not exact"
}

function Get-NormalizedFullPath([string]$Path, [string]$Label) {
  Require (-not [string]::IsNullOrWhiteSpace($Path)) "$Label path is empty"
  try {
    $full = [IO.Path]::GetFullPath($Path)
  } catch {
    throw "N23 evidence requirement failed: $Label path is invalid"
  }
  Require ($full -notmatch '^[\\]{2}[?.][\\]') "$Label uses a device path"
  $rootPart = [IO.Path]::GetPathRoot($full)
  Require (-not [string]::IsNullOrWhiteSpace($rootPart)) "$Label path is not rooted"
  $tail = $full.Substring($rootPart.Length)
  Require ($tail -notmatch ':') "$Label path uses an alternate data stream"
  $full.TrimEnd([char[]]@('\', '/'))
}

function Test-PathWithinRoot([string]$Path, [string]$AllowedRoot,
                             [bool]$AllowRoot = $true) {
  $full = Get-NormalizedFullPath $Path 'candidate'
  $rootFull = Get-NormalizedFullPath $AllowedRoot 'allowed root'
  if ($full.Equals($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
    return $AllowRoot
  }
  $full.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Assert-ConfinedPath([string]$Path, [string]$AllowedRoot,
                             [string]$Label, [bool]$AllowRoot = $true) {
  $full = Get-NormalizedFullPath $Path $Label
  $rootFull = Get-NormalizedFullPath $AllowedRoot "$Label allowed root"
  Require (Test-PathWithinRoot $full $rootFull $AllowRoot) `
      "$Label escaped its allowed root"
  $full
}

function Get-LiteralItemOrNull([string]$Path) {
  try {
    Get-Item -LiteralPath $Path -Force -ErrorAction Stop
  } catch [System.Management.Automation.ItemNotFoundException] {
    $null
  }
}

function Assert-PlainPathChain(
    [string]$Path, [string]$AllowedRoot, [string]$Label,
    [ValidateSet('Any', 'File', 'Directory', 'Missing')][string]$LeafKind = 'Any',
    [bool]$AllowMissingLeaf = $false) {
  $full = Assert-ConfinedPath $Path $AllowedRoot $Label
  $volumeRoot = [IO.Path]::GetPathRoot($full)
  $relative = $full.Substring($volumeRoot.Length).TrimStart([char[]]@('\', '/'))
  $components = @(if ($relative.Length -gt 0) { $relative -split '[\\/]' })
  $current = $volumeRoot
  $lastItem = Get-LiteralItemOrNull $current
  Require ($null -ne $lastItem -and $lastItem.PSIsContainer) `
      "$Label volume root is unavailable"
  Require (($lastItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
      "$Label volume root is a reparse point"
  for ($index = 0; $index -lt $components.Count; ++$index) {
    $component = [string]$components[$index]
    Require ($component.Length -gt 0 -and $component -notin @('.', '..')) `
        "$Label contains an invalid path component"
    $current = Join-Path $current $component
    $item = Get-LiteralItemOrNull $current
    $isLeaf = $index -eq ($components.Count - 1)
    if ($null -eq $item) {
      Require ($isLeaf -and $AllowMissingLeaf) `
          "$Label path component is missing: $current"
      $lastItem = $null
      continue
    }
    Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
        "$Label path chain contains a reparse point"
    if (-not $isLeaf) {
      Require ($item.PSIsContainer) "$Label ancestor is not a directory"
    }
    $lastItem = $item
  }
  switch ($LeafKind) {
    'File' {
      Require ($null -ne $lastItem -and -not $lastItem.PSIsContainer) `
          "$Label is not a plain file"
    }
    'Directory' {
      Require ($null -ne $lastItem -and $lastItem.PSIsContainer) `
          "$Label is not a plain directory"
    }
    'Missing' {
      Require ($null -eq $lastItem) "$Label already exists"
    }
  }
  $full
}

function Ensure-PlainDirectory([string]$Path, [string]$AllowedRoot,
                               [string]$Label) {
  $full = Assert-ConfinedPath $Path $AllowedRoot $Label
  $item = Get-LiteralItemOrNull $full
  if ($null -eq $item) {
    $parent = Split-Path -Parent $full
    [void](Assert-PlainPathChain $parent $AllowedRoot "$Label parent" 'Directory')
    [void](Assert-PlainPathChain $full $AllowedRoot $Label 'Missing' $true)
    New-Item -ItemType Directory -Path $full -ErrorAction Stop | Out-Null
  }
  Assert-PlainPathChain $full $AllowedRoot $Label 'Directory'
}

function Get-DefaultAllowedRoot([string]$Path) {
  $full = Get-NormalizedFullPath $Path 'file'
  if ((Get-LiteralItemOrNull $EvidenceDir) -and
      (Test-PathWithinRoot $full $EvidenceDir $true)) {
    [void](Assert-PlainPathChain $EvidenceDir $Root 'N23 evidence root' 'Directory')
    return [IO.Path]::GetFullPath($EvidenceDir)
  }
  if (Test-PathWithinRoot $full $Root $true) {
    [void](Assert-PlainPathChain $Root $Root 'repository root' 'Directory')
    return [IO.Path]::GetFullPath($Root)
  }
  throw 'N23 evidence requirement failed: file path has no approved repository/evidence root'
}

function Get-SafeFileItem([string]$Path, [string]$AllowedRoot,
                          [int64]$MaximumBytes, [string]$Label) {
  $full = Assert-PlainPathChain $Path $AllowedRoot $Label 'File'
  $item = Get-Item -LiteralPath $full -Force -ErrorAction Stop
  Require ([int64]$item.Length -le $MaximumBytes) "$Label exceeds its byte limit"
  $item
}

function Read-BoundedUtf8Text([string]$Path, [string]$AllowedRoot,
                              [int64]$MaximumBytes, [string]$Label) {
  $before = Get-SafeFileItem $Path $AllowedRoot $MaximumBytes $Label
  $bytes = [IO.File]::ReadAllBytes($before.FullName)
  Require ([int64]$bytes.Length -eq [int64]$before.Length) `
      "$Label changed while it was read"
  $after = Get-SafeFileItem $before.FullName $AllowedRoot $MaximumBytes $Label
  Require ([int64]$after.Length -eq [int64]$before.Length -and
           $after.LastWriteTimeUtc.Ticks -eq $before.LastWriteTimeUtc.Ticks -and
           $after.CreationTimeUtc.Ticks -eq $before.CreationTimeUtc.Ticks -and
           [int]$after.Attributes -eq [int]$before.Attributes) `
      "$Label changed while it was read"
  try {
    $StrictUtf8.GetString($bytes)
  } catch {
    throw "N23 evidence requirement failed: $Label is not valid UTF-8"
  }
}

function Read-BoundedUtf8Lines([string]$Path, [string]$AllowedRoot,
                               [int64]$MaximumBytes, [string]$Label) {
  $text = Read-BoundedUtf8Text $Path $AllowedRoot $MaximumBytes $Label
  if ($text.Length -eq 0) { return @() }
  $lines = @([regex]::Split($text, '\r\n|\n|\r'))
  if ($lines.Count -gt 0 -and $lines[$lines.Count - 1] -ceq '') {
    if ($lines.Count -eq 1) { return @() }
    $lines = @($lines[0..($lines.Count - 2)])
  }
  Require ($lines.Count -le $MaxOutputLineCount) `
      "$Label exceeds its line-count limit"
  $lines
}

function File-Hash([string]$Path, [string]$AllowedRoot = '',
                   [int64]$MaximumBytes = $MaxHashBytes) {
  if ([string]::IsNullOrWhiteSpace($AllowedRoot)) {
    $AllowedRoot = Get-DefaultAllowedRoot $Path
  }
  $before = Get-SafeFileItem $Path $AllowedRoot $MaximumBytes 'hash input'
  $sha = [Security.Cryptography.SHA256]::Create()
  $stream = $null
  try {
    $stream = New-Object IO.FileStream(
        $before.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    $hash = $sha.ComputeHash($stream)
  } finally {
    if ($null -ne $stream) { $stream.Dispose() }
    $sha.Dispose()
  }
  $after = Get-SafeFileItem $before.FullName $AllowedRoot $MaximumBytes 'hash input'
  Require ([int64]$after.Length -eq [int64]$before.Length -and
           $after.LastWriteTimeUtc.Ticks -eq $before.LastWriteTimeUtc.Ticks -and
           $after.CreationTimeUtc.Ticks -eq $before.CreationTimeUtc.Ticks -and
           [int]$after.Attributes -eq [int]$before.Attributes) `
      'hash input changed while it was hashed'
  ([BitConverter]::ToString($hash)).Replace('-', '').ToLowerInvariant()
}

function Remove-SafeFile([string]$Path, [string]$AllowedRoot,
                         [string]$Label) {
  $full = Assert-ConfinedPath $Path $AllowedRoot $Label
  $item = Get-LiteralItemOrNull $full
  if ($null -eq $item) { return }
  [void](Assert-PlainPathChain $full $AllowedRoot $Label 'File')
  [IO.File]::Delete($full)
  Require ($null -eq (Get-LiteralItemOrNull $full)) "$Label deletion failed"
}

function Remove-PreexistingBuildArtifact([string]$Path, [string]$Label) {
  $full = Assert-ConfinedPath $Path $Root $Label $false
  $existing = Get-LiteralItemOrNull $full
  if ($null -eq $existing) {
    [void](Assert-PlainPathChain $full $Root $Label 'Missing' $true)
    return
  }
  [void](Assert-PlainPathChain $full $Root $Label 'File')
  Remove-SafeFile $full $Root $Label
  [void](Assert-PlainPathChain $full $Root $Label 'Missing' $true)
}

function Get-FreshBuildArtifact([string]$Path, [DateTimeOffset]$StageStarted,
                                [string]$Label) {
  $item = Get-SafeFileItem $Path $Root $MaxHashBytes $Label
  $created = [DateTimeOffset]$item.CreationTimeUtc
  $written = [DateTimeOffset]$item.LastWriteTimeUtc
  Require ($created.Offset -eq [TimeSpan]::Zero -and
           $written.Offset -eq [TimeSpan]::Zero) "$Label timestamps are not UTC"
  Require ($created -ge $StageStarted) "$Label predates the fresh build stage"
  Require ($written -ge $StageStarted) "$Label was not written during the fresh build stage"
  [pscustomobject][ordered]@{
    sha256 = File-Hash $item.FullName $Root
    bytes = [int64]$item.Length
    created_utc = $created.ToString('o')
    last_write_utc = $written.ToString('o')
  }
}

function Write-AtomicUtf8Text([string]$Path, [string]$Text,
                              [string]$AllowedRoot = $EvidenceDir,
                              [int64]$MaximumBytes = $MaxImportedLogBytes) {
  $full = Assert-ConfinedPath $Path $AllowedRoot 'atomic-write target' $false
  $directory = Split-Path -Parent $full
  [void](Assert-PlainPathChain $directory $AllowedRoot `
      'atomic-write directory' 'Directory')
  $existing = Get-LiteralItemOrNull $full
  if ($null -ne $existing) {
    [void](Assert-PlainPathChain $full $AllowedRoot `
        'atomic-write target' 'File')
  }
  try {
    $bytes = $StrictUtf8.GetBytes($Text)
  } catch {
    throw 'N23 evidence requirement failed: atomic-write text is not valid UTF-8'
  }
  Require ([int64]$bytes.Length -le $MaximumBytes) `
      'atomic-write text exceeds its byte limit'
  $temporary = Join-Path $directory ('.n23-atomic-' +
                                      [guid]::NewGuid().ToString('N') + '.tmp')
  $backup = Join-Path $directory ('.n23-atomic-' +
                                   [guid]::NewGuid().ToString('N') + '.bak')
  [void](Assert-PlainPathChain $temporary $AllowedRoot `
      'atomic-write temporary' 'Missing' $true)
  [void](Assert-PlainPathChain $backup $AllowedRoot `
      'atomic-write backup' 'Missing' $true)
  try {
    [IO.File]::WriteAllBytes($temporary, $bytes)
    [void](Assert-PlainPathChain $temporary $AllowedRoot `
        'atomic-write temporary' 'File')
    [void](Assert-PlainPathChain $directory $AllowedRoot `
        'atomic-write directory' 'Directory')
    $destination = Get-LiteralItemOrNull $full
    if ($null -ne $destination) {
      [void](Assert-PlainPathChain $full $AllowedRoot `
          'atomic-write target' 'File')
      [IO.File]::Replace($temporary, $full, $backup, $true)
      [void](Assert-PlainPathChain $backup $AllowedRoot `
          'atomic-write backup' 'File')
      Remove-SafeFile $backup $AllowedRoot 'atomic-write backup'
    } else {
      [void](Assert-PlainPathChain $full $AllowedRoot `
          'atomic-write target' 'Missing' $true)
      [IO.File]::Move($temporary, $full)
    }
    [void](Assert-PlainPathChain $full $AllowedRoot `
        'atomic-write published target' 'File')
  } finally {
    if ($null -ne (Get-LiteralItemOrNull $temporary)) {
      Remove-SafeFile $temporary $AllowedRoot 'atomic-write temporary'
    }
    if ($null -ne (Get-LiteralItemOrNull $backup)) {
      Remove-SafeFile $backup $AllowedRoot 'atomic-write backup'
    }
  }
}

function Write-AtomicUtf8Lines([string]$Path, [object[]]$Lines,
                               [string]$AllowedRoot = $EvidenceDir,
                               [int64]$MaximumBytes = $MaxImportedLogBytes) {
  Write-AtomicUtf8Text $Path ((@($Lines) -join [Environment]::NewLine) +
                              [Environment]::NewLine) $AllowedRoot $MaximumBytes
}

function Text-Hash([string]$Text) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
    ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
  } finally {
    $sha.Dispose()
  }
}

function ConvertFrom-JsonText([string]$Text) {
  $command = Get-Command ConvertFrom-Json
  if ($command.Parameters.ContainsKey('DateKind')) {
    return $Text | ConvertFrom-Json -DateKind String -ErrorAction Stop
  }
  return $Text | ConvertFrom-Json -ErrorAction Stop
}

function Assert-StrictJsonSyntaxAndNoDuplicateProperties([string]$Text,
                                                          [string]$Label) {
  Require (-not $Text.StartsWith([string][char]0xfeff, [StringComparison]::Ordinal)) `
      "$Label has a UTF-8 BOM"
  $state = [pscustomobject]@{ index = 0; length = $Text.Length }
  $skipWhitespace = {
    while ($state.index -lt $state.length -and
           ([string]$Text[$state.index] -in @(' ', "`t", "`r", "`n"))) {
      ++$state.index
    }
  }
  $parseString = {
    Require ($state.index -lt $state.length -and $Text[$state.index] -eq '"') `
        "$Label JSON string is malformed"
    ++$state.index
    $builder = New-Object System.Text.StringBuilder
    while ($state.index -lt $state.length) {
      $character = [char]$Text[$state.index]
      ++$state.index
      if ($character -eq '"') { return $builder.ToString() }
      Require ([int][char]$character -ge 0x20) "$Label JSON string has a control character"
      if ($character -ne '\') {
        [void]$builder.Append($character)
        continue
      }
      Require ($state.index -lt $state.length) "$Label JSON escape is truncated"
      $escape = [char]$Text[$state.index]
      ++$state.index
      switch ($escape) {
        '"' { [void]$builder.Append('"'); break }
        '\' { [void]$builder.Append('\'); break }
        '/' { [void]$builder.Append('/'); break }
        'b' { [void]$builder.Append([char]0x08); break }
        'f' { [void]$builder.Append([char]0x0c); break }
        'n' { [void]$builder.Append([char]0x0a); break }
        'r' { [void]$builder.Append([char]0x0d); break }
        't' { [void]$builder.Append([char]0x09); break }
        'u' {
          Require (($state.index + 4) -le $state.length) "$Label JSON unicode escape is truncated"
          $hex = $Text.Substring($state.index, 4)
          Require ($hex -cmatch '^[0-9A-Fa-f]{4}$') "$Label JSON unicode escape is malformed"
          [void]$builder.Append([char][Convert]::ToInt32($hex, 16))
          $state.index += 4
          break
        }
        default { throw "N23 evidence requirement failed: $Label JSON escape is invalid" }
      }
    }
    throw "N23 evidence requirement failed: $Label JSON string is unterminated"
  }
  $parseLiteral = {
    param([string]$Literal)
    Require (($state.index + $Literal.Length) -le $state.length -and
             $Text.Substring($state.index, $Literal.Length) -ceq $Literal) `
        "$Label JSON literal is malformed"
    $state.index += $Literal.Length
  }
  $parseValue = $null
  $parseArray = $null
  $parseObject = $null
  $parseArray = {
    param([int]$Depth)
    Require ($Text[$state.index] -eq '[') "$Label JSON array is malformed"
    ++$state.index
    & $skipWhitespace
    if ($state.index -lt $state.length -and $Text[$state.index] -eq ']') {
      ++$state.index
      return
    }
    while ($true) {
      & $parseValue ($Depth + 1)
      & $skipWhitespace
      Require ($state.index -lt $state.length) "$Label JSON array is unterminated"
      if ($Text[$state.index] -eq ']') {
        ++$state.index
        return
      }
      Require ($Text[$state.index] -eq ',') "$Label JSON array is malformed"
      ++$state.index
      & $skipWhitespace
      Require ($state.index -lt $state.length -and $Text[$state.index] -ne ']') `
          "$Label JSON array has a trailing comma"
    }
  }
  $parseObject = {
    param([int]$Depth)
    Require ($Text[$state.index] -eq '{') "$Label JSON object is malformed"
    ++$state.index
    & $skipWhitespace
    $names = New-Object System.Collections.Generic.List[string]
    if ($state.index -lt $state.length -and $Text[$state.index] -eq '}') {
      ++$state.index
      return
    }
    while ($true) {
      & $skipWhitespace
      $name = & $parseString
      Require (-not $names.Contains($name)) "$Label JSON object contains duplicate property '$name'"
      $names.Add($name)
      & $skipWhitespace
      Require ($state.index -lt $state.length -and $Text[$state.index] -eq ':') `
          "$Label JSON object is missing a property separator"
      ++$state.index
      & $skipWhitespace
      & $parseValue ($Depth + 1)
      & $skipWhitespace
      Require ($state.index -lt $state.length) "$Label JSON object is unterminated"
      if ($Text[$state.index] -eq '}') {
        ++$state.index
        return
      }
      Require ($Text[$state.index] -eq ',') "$Label JSON object is malformed"
      ++$state.index
      & $skipWhitespace
      Require ($state.index -lt $state.length -and $Text[$state.index] -ne '}') `
          "$Label JSON object has a trailing comma"
    }
  }
  $parseValue = {
    param([int]$Depth)
    Require ($Depth -le $MaxSidecarJsonDepth) "$Label JSON nesting exceeds its bound"
    & $skipWhitespace
    Require ($state.index -lt $state.length) "$Label JSON value is missing"
    $character = [char]$Text[$state.index]
    if ($character -eq '{') { & $parseObject $Depth; return }
    if ($character -eq '[') { & $parseArray $Depth; return }
    if ($character -eq '"') { [void](& $parseString); return }
    if ($character -eq 't') { & $parseLiteral 'true'; return }
    if ($character -eq 'f') { & $parseLiteral 'false'; return }
    if ($character -eq 'n') { & $parseLiteral 'null'; return }
    $remaining = $Text.Substring($state.index)
    $number = [regex]::Match($remaining,
      '^-?(?:0|[1-9][0-9]*)(?:[.][0-9]+)?(?:[eE][+-]?[0-9]+)?')
    Require ($number.Success) "$Label JSON value is malformed"
    $state.index += $number.Length
  }
  & $skipWhitespace
  Require ($state.index -lt $state.length) "$Label is empty"
  & $parseValue 0
  & $skipWhitespace
  Require ($state.index -eq $state.length) "$Label has trailing non-JSON content"
}

function Read-StrictSidecarJson([string]$Path) {
  $text = Read-BoundedUtf8Text $Path $EvidenceDir $MaxJsonBytes `
      'post-gate shared-input sidecar'
  Assert-StrictJsonSyntaxAndNoDuplicateProperties $text 'post-gate shared-input sidecar'
  try {
    ConvertFrom-JsonText $text
  } catch {
    throw 'N23 evidence requirement failed: post-gate shared-input sidecar is not one JSON document'
  }
}

function Require-JsonString([object]$Value, [string]$Label) {
  Require ($Value -is [string]) "$Label is not a JSON string"
  Require (-not [string]::IsNullOrWhiteSpace([string]$Value)) "$Label is empty"
  [string]$Value
}

function Require-JsonInteger([object]$Value, [string]$Label) {
  Require ($Value -is [byte] -or $Value -is [sbyte] -or
           $Value -is [int16] -or $Value -is [uint16] -or
           $Value -is [int32] -or $Value -is [uint32] -or
           $Value -is [int64] -or $Value -is [uint64]) "$Label is not an integer"
  [int64]$Value
}

function Require-JsonStringArray([object]$Value, [string]$Label) {
  Require ($null -ne $Value -and -not ($Value -is [string]) -and
           $Value -is [System.Collections.IEnumerable]) "$Label is not an array"
  $items = @($Value)
  foreach ($item in $items) { [void](Require-JsonString $item "$Label item") }
  $items
}

function Assert-SidecarUtcTimestamp([object]$Value, [string]$Label) {
  $text = Require-JsonString $Value $Label
  Require ($text -cmatch '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[.]\d{7}(?:Z|[+]00:00)$') `
      "$Label is not canonical UTC"
  $normalized = if ($text.EndsWith('Z', [StringComparison]::Ordinal)) {
    $text.Substring(0, $text.Length - 1) + '+00:00'
  } else {
    $text
  }
  try {
    $timestamp = [DateTimeOffset]::ParseExact($normalized,
      'yyyy-MM-ddTHH:mm:ss.fffffffzzz',
      [Globalization.CultureInfo]::InvariantCulture,
      [Globalization.DateTimeStyles]::None)
  } catch {
    throw "N23 evidence requirement failed: $Label is not a real UTC timestamp"
  }
  Require ($timestamp.Offset -eq [TimeSpan]::Zero) "$Label is not UTC"
  $timestamp
}

function Get-ExpectedN23SnapshotLabels {
  $json = @'
["on-this-day:direct","on-this-day:year-boundary-limit","on-this-day:leap","on-this-day:century-leap","on-this-day:recurring-leap-day","on-this-day:bad-date:0","on-this-day:bad-date:1","on-this-day:bad-date:2","on-this-day:bad-date:3","on-this-day:bad-date:4","on-this-day:bad-date:5","on-this-day:bad-date:6","on-this-day:bad-mmdd:0","on-this-day:bad-mmdd:1","on-this-day:bad-mmdd:2","on-this-day:bad-mmdd:3","on-this-day:bad-mmdd:4","on-this-day:alias-conflict","on-this-day:alias-match","on-this-day:repeat-byte-stability","source:invalid","source:unknown","source:remote-denied","source:local-team","on-this-day:bad-limit:0","on-this-day:bad-limit:1","on-this-day:bad-limit:2","on-this-day:bad-limit:3","on-this-day:bad-limit:4","on-this-day:bad-limit:5","on-this-day:bad-limit:6","on-this-day:bad-limit:7","on-this-day:limit-zero","on-this-day:limit-one","on-this-day:limit-high","on-this-day:omitted-date","last-seen:direct","last-seen:exact","last-seen:updated-newer-asof-equal","last-seen:equal-timestamps","last-seen:omitted-asof","last-seen:utf8-4096-byte-entity","last-seen:slug-alias","last-seen:alias-conflict","last-seen:missing","last-seen:cross-source-not-found","last-seen:deleted-not-found","last-seen:required","last-seen:empty","last-seen:bad-asof","last-seen:malformed-utf8","last-seen:oversized","direct-source:empty:on-this-day","direct-source:empty:last-seen","direct-source:empty:backfill","direct-source:unknown:on-this-day","direct-source:unknown:last-seen","direct-source:unknown:backfill","backfill:dry-run","backfill:direct-dry-run","backfill:omitted-limit","backfill:since-timestamp-t-inclusive","backfill:since-timestamp-space-inclusive","backfill:limit-zero","backfill:limit-above-maximum","backfill:idempotent","backfill:explicit-false-idempotent","backfill:mid-transaction-rollback","backfill:busy","backfill:bad-limit:","backfill:bad-limit:-1","backfill:bad-limit:+1","backfill:bad-limit: 1","backfill:bad-limit:1 ","backfill:bad-limit:1.0","backfill:bad-limit:1x","backfill:bad-limit:18446744073709551616","backfill:bad-since:0","backfill:bad-since:1","backfill:bad-since:2","backfill:bad-since:3","backfill:bad-since:4","backfill:bad-since:5","backfill:bad-since:6","backfill:bad-bool","mcp:typed-unsigned-limit","mcp:typed-false","mcp:ambient-default","mcp:last-seen","mcp:write-denied","mcp:write-enabled-dry-run","mcp:source-not-allowed","mcp:authorized-source-dry-run","mcp:wrong-type:on-date-bool","mcp:wrong-type:on-date-null","mcp:wrong-type:on-mmdd-object","mcp:wrong-type:on-limit-array","mcp:wrong-type:on-limit-signed","mcp:wrong-type:on-source-object","mcp:wrong-type:last-entity-number","mcp:wrong-type:last-entity-null","mcp:wrong-type:last-entity-object","mcp:wrong-type:last-entity-array","mcp:wrong-type:last-asof-bool","mcp:wrong-type:backfill-since-null","mcp:wrong-type:backfill-since-bool","mcp:wrong-type:backfill-since-object","mcp:wrong-type:backfill-since-array","mcp:wrong-type:backfill-limit-float","mcp:wrong-type:backfill-limit-string","mcp:wrong-type:backfill-dry-string","mcp:wrong-type:backfill-dry-null","mcp:wrong-type:backfill-dry-array","mcp:non-object:chronicle_on_this_day","mcp:non-object:chronicle_last_seen","mcp:non-object:chronicle_backfill","mcp:unknown-field:chronicle_on_this_day","local:unknown-field:chronicle_on_this_day","mcp:unknown-field:chronicle_last_seen","local:unknown-field:chronicle_last_seen","mcp:unknown-field:chronicle_backfill","local:unknown-field:chronicle_backfill"]
'@
  $baseLabels = @((ConvertFrom-JsonText $json))
  $insertAfter = [Array]::IndexOf($baseLabels, 'last-seen:oversized')
  Require ($insertAfter -ge 0) 'embedded N23 snapshot label insertion point is missing'
  $labels = @(
    $baseLabels[0..$insertAfter]
    'last-seen:direct-malformed-utf8'
    'last-seen:direct-oversized'
    $baseLabels[($insertAfter + 1)..($baseLabels.Count - 1)]
  )
  Require ($labels.Count -eq $ExpectedN23SnapshotLabelCount) 'embedded N23 snapshot label count is invalid'
  Require ((Text-Hash ($labels -join "`n")) -ceq
           $ExpectedN23SnapshotLabelsHash) 'embedded N23 snapshot label hash is invalid'
  $labels
}

function Assert-N23SnapshotLabelContract([string[]]$Labels, [string]$Label) {
  $expected = @(Get-ExpectedN23SnapshotLabels)
  Require ($Labels.Count -eq $expected.Count) "$Label snapshot label count is not exact"
  Require (($Labels | Sort-Object -Unique).Count -eq $Labels.Count) `
      "$Label snapshot labels are not unique"
  for ($index = 0; $index -lt $expected.Count; ++$index) {
    Require ([string]$Labels[$index] -ceq [string]$expected[$index]) `
        "$Label snapshot label order differs at index $($index + 1)"
  }
  Require ((Text-Hash ($Labels -join "`n")) -ceq
           $ExpectedN23SnapshotLabelsHash) "$Label snapshot label hash is invalid"
}

function Read-Json([string]$Path, [string]$AllowedRoot = '',
                   [int64]$MaximumBytes = $MaxJsonBytes) {
  if ([string]::IsNullOrWhiteSpace($AllowedRoot)) {
    $AllowedRoot = Get-DefaultAllowedRoot $Path
  }
  ConvertFrom-JsonText (Read-BoundedUtf8Text $Path $AllowedRoot $MaximumBytes `
      'JSON input')
}

function Relative-Path([string]$Path) {
  $rootFull = (Get-NormalizedFullPath $Root 'repository root') + '\'
  $pathFull = Assert-ConfinedPath $Path $Root 'repository path' $false
  $pathFull.Substring($rootFull.Length).Replace('\', '/')
}

function Resolve-RepositoryRelativePath([string]$Relative, [string]$Label) {
  Require (-not [string]::IsNullOrWhiteSpace($Relative)) "$Label is empty"
  Require (-not [IO.Path]::IsPathRooted($Relative)) "$Label is rooted"
  [void](Assert-NoN30ArtifactPath $Relative $Label)
  $components = @($Relative.Replace('/', '\') -split '\\')
  Require ($components.Count -gt 0) "$Label has no components"
  foreach ($component in $components) {
    Require ($component.Length -gt 0 -and $component -notin @('.', '..')) `
        "$Label contains an invalid component"
  }
  Assert-ConfinedPath (Join-Path $Root ($components -join '\')) $Root $Label $false
}

function File-Entry([string]$Path, [string]$Role) {
  [void](Assert-NoN30ArtifactPath $Path 'manifest input')
  $allowedRoot = Get-DefaultAllowedRoot $Path
  $item = Get-SafeFileItem $Path $allowedRoot $MaxHashBytes 'manifest input'
  [pscustomobject][ordered]@{
    path = Relative-Path $item.FullName
    role = $Role
    sha256 = File-Hash $item.FullName $allowedRoot
    bytes = [int64]$item.Length
  }
}

function Get-SafeTreeInventory([string]$Path, [string]$AllowedRoot,
                               [string]$Label) {
  [void](Assert-NoN30ArtifactPath $Path $Label)
  $rootPath = Assert-PlainPathChain $Path $AllowedRoot $Label 'Directory'
  $queue = New-Object 'System.Collections.Generic.Queue[string]'
  $queue.Enqueue($rootPath)
  $entries = New-Object System.Collections.Generic.List[object]
  while ($queue.Count -gt 0) {
    $directory = $queue.Dequeue()
    [void](Assert-PlainPathChain $directory $AllowedRoot `
        "$Label directory" 'Directory')
    $remainingEntries = $MaxTreeEntries - $entries.Count
    Require ($remainingEntries -ge 0) "$Label tree exceeds its entry limit"
    $candidates = New-Object System.Collections.Generic.List[string]
    foreach ($candidate in [IO.Directory]::EnumerateFileSystemEntries($directory)) {
      Require ($candidates.Count -lt $remainingEntries) `
          "$Label tree exceeds its entry limit"
      $candidates.Add([string]$candidate)
    }
    foreach ($candidate in @($candidates.ToArray() | Sort-Object)) {
      [void](Assert-NoN30ArtifactPath $candidate "$Label descendant")
      $full = Assert-ConfinedPath $candidate $rootPath "$Label descendant" $false
      $item = Get-LiteralItemOrNull $full
      Require ($null -ne $item) "$Label descendant disappeared during traversal"
      Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
          "$Label tree contains a reparse point"
      $relative = $full.Substring($rootPath.Length).TrimStart('\')
      Require ($relative.Length -le $MaxTreePathCharacters) `
          "$Label descendant path exceeds its character limit"
      $depth = @($relative -split '\\').Count
      Require ($depth -le $MaxTreeDepth) "$Label tree exceeds its depth limit"
      Require ($entries.Count -lt $MaxTreeEntries) `
          "$Label tree exceeds its entry limit"
      $entries.Add([pscustomobject][ordered]@{
          path = $full
          is_directory = [bool]$item.PSIsContainer
          depth = $depth
        }) | Out-Null
      if ($item.PSIsContainer) { $queue.Enqueue($full) }
    }
  }
  $entries.ToArray()
}

function Get-SafeTreeLeafPaths([string]$Path, [string]$AllowedRoot,
                               [string]$Label) {
  @((Get-SafeTreeInventory $Path $AllowedRoot $Label) | Where-Object {
      -not [bool]$_.is_directory
    } | ForEach-Object { [string]$_.path })
}

function Remove-SafeTree([string]$Path, [string]$AllowedRoot,
                         [string]$Label) {
  [void](Assert-NoN30ArtifactPath $Path $Label)
  $full = Assert-ConfinedPath $Path $AllowedRoot $Label $false
  if ($null -eq (Get-LiteralItemOrNull $full)) { return }
  $entries = @(Get-SafeTreeInventory $full $AllowedRoot $Label | Sort-Object `
      @{ Expression = { [int]$_.depth }; Descending = $true }, `
      @{ Expression = { [string]$_.path }; Descending = $true })
  foreach ($entry in $entries) {
    $kind = if ([bool]$entry.is_directory) { 'Directory' } else { 'File' }
    $freshPath = Assert-PlainPathChain ([string]$entry.path) $full `
        "$Label cleanup entry" $kind
    $fresh = Get-Item -LiteralPath $freshPath -Force -ErrorAction Stop
    if ($fresh.PSIsContainer) {
      Require (@([IO.Directory]::EnumerateFileSystemEntries($freshPath)).Count -eq 0) `
          "$Label cleanup directory is not empty"
      [IO.Directory]::Delete($freshPath, $false)
    } else {
      [IO.File]::Delete($freshPath)
    }
    Require ($null -eq (Get-LiteralItemOrNull $freshPath)) `
        "$Label cleanup entry deletion failed"
  }
  [void](Assert-PlainPathChain $full $AllowedRoot $Label 'Directory')
  Require (@([IO.Directory]::EnumerateFileSystemEntries($full)).Count -eq 0) `
      "$Label cleanup root is not empty"
  [IO.Directory]::Delete($full, $false)
  Require ($null -eq (Get-LiteralItemOrNull $full)) "$Label cleanup root deletion failed"
}

function Assert-Sha256([string]$Value, [string]$Label) {
  Require ($Value -cmatch '^[0-9a-f]{64}$') "$Label is not a lowercase SHA-256"
}

function Assert-SafeText([string]$Text, [string]$Label) {
  Require ($Text -notmatch '(?i)n30') "$Label contains an excluded artifact reference"
  Require ($Text -notmatch '(?i)\bsk-[A-Za-z0-9_-]{16,}\b') "$Label contains a secret-like key"
  Require ($Text -notmatch '(?i)\bBearer\s+[A-Za-z0-9._~+/-]{12,}={0,2}\b') "$Label contains a bearer-like secret"
  Require ($Text -notmatch '(?i)\bgit(?:[.]exe)?\s+(?:commit|push)\b') "$Label contains a prohibited Git mutation command"
}

$ChildEnvironmentPolicy = 'fail-closed-v2: explicit allowlist; pinned PATH; isolated LOCALAPPDATA/TEMP/TMP/APPDATA/USERPROFILE; no ambient Git/Qbrain/provider/credential/proxy variables'
$script:BaseToolFacts = $null

function Test-BlockedChildEnvironmentName([string]$Name) {
  if ([string]::IsNullOrWhiteSpace($Name)) { return $true }
  if ($Name -match '^GIT_') { return $true }
  if ($Name -match '^QBRAIN_') { return $true }
  if ($Name -match '^(?:OPENAI|ANTHROPIC|AZURE|GOOGLE|GEMINI|OLLAMA|COHERE|MISTRAL|DEEPSEEK|AWS|GCP|OPENROUTER|XAI|GROQ|HUGGINGFACE|HF)_') { return $true }
  if ($Name -match '^(?:HTTP|HTTPS|ALL|NO)_PROXY$') { return $true }
  if ($Name -match '^(?:LLM|AI)_' -or
      $Name -match '^(?:MODEL|PROVIDER|BASE_URL|REASONING_EFFORT|CONTEXT_SIZE|COMPRESSION_THRESHOLD)$') { return $true }
  if ($Name -match '(?:^|_)(?:API_?KEY|TOKEN|SECRET|PASSWORD|PASSPHRASE|CREDENTIALS?)(?:_|$)') { return $true }
  if ($Name -match '^(?:SSH_AUTH_SOCK|SSH_AGENT_PID)$') { return $true }
  return $false
}

function Get-ExecutableFact([string]$Path, [string]$Name) {
  $full = Get-NormalizedFullPath $Path "$Name executable"
  $parent = Split-Path -Parent $full
  [void](Assert-PlainPathChain $full $parent "$Name executable" 'File')
  [pscustomobject][ordered]@{
    name = $Name
    path = $full.Replace('\', '/')
    sha256 = File-Hash $full $parent
  }
}

function Resolve-ApplicationPath([string]$Name) {
  $commands = @(Get-Command $Name -CommandType Application -ErrorAction Stop)
  Require ($commands.Count -ge 1) "$Name executable is unavailable"
  $paths = @($commands | ForEach-Object { [IO.Path]::GetFullPath($_.Source) } |
      Sort-Object -Unique)
  Require ($paths.Count -ge 1) "$Name executable resolution is empty"
  $paths[0]
}

function Get-BaseToolFacts([switch]$Refresh) {
  if (-not $Refresh -and $null -ne $script:BaseToolFacts) {
    return $script:BaseToolFacts
  }
  $systemRoot = Get-NormalizedFullPath ([Environment]::GetEnvironmentVariable(
      'SystemRoot', [EnvironmentVariableTarget]::Process)) 'SystemRoot'
  [void](Assert-PlainPathChain $systemRoot $systemRoot 'SystemRoot' 'Directory')
  $windowsPowerShell = Join-Path $systemRoot `
      'System32\WindowsPowerShell\v1.0\powershell.exe'
  $cmd = Join-Path $systemRoot 'System32\cmd.exe'
  $where = Join-Path $systemRoot 'System32\where.exe'
  $hostPath = (Get-Process -Id $PID -ErrorAction Stop).Path
  $script:BaseToolFacts = [pscustomobject][ordered]@{
    verifier_host = Get-ExecutableFact $hostPath 'verifier-host'
    windows_powershell = Get-ExecutableFact $windowsPowerShell 'WindowsPowerShell'
    cmd = Get-ExecutableFact $cmd 'cmd'
    where = Get-ExecutableFact $where 'where'
    git = Get-ExecutableFact (Resolve-ApplicationPath 'git.exe') 'git'
  }
  $script:BaseToolFacts
}

function Get-ToolPath([object]$Fact) {
  ([string]$Fact.path).Replace('/', '\')
}

function New-ClosedChildEnvironment([object]$Sandbox, [object]$Tools) {
  foreach ($property in @('root', 'localappdata', 'temp', 'appdata', 'userprofile')) {
    Require (-not [string]::IsNullOrWhiteSpace([string]$Sandbox.$property)) `
        "child sandbox lacks $property"
    [void](Assert-PlainPathChain ([string]$Sandbox.$property) ([string]$Sandbox.root) `
        "child sandbox $property" 'Directory')
  }
  $systemRoot = Get-NormalizedFullPath ([Environment]::GetEnvironmentVariable(
      'SystemRoot', [EnvironmentVariableTarget]::Process)) 'SystemRoot'
  $system32 = Join-Path $systemRoot 'System32'
  [void](Assert-PlainPathChain $system32 $systemRoot 'System32' 'Directory')
  $windowsPowerShellPath = Get-ToolPath $Tools.windows_powershell
  $windowsPowerShellDirectory = Split-Path -Parent $windowsPowerShellPath
  $processorCount = [Environment]::ProcessorCount
  Require ($processorCount -ge 1) 'processor count is invalid'
  $environment = [ordered]@{
    APPDATA = [string]$Sandbox.appdata
    COMSPEC = Get-ToolPath $Tools.cmd
    LOCALAPPDATA = [string]$Sandbox.localappdata
    NUMBER_OF_PROCESSORS = [string]$processorCount
    OS = 'Windows_NT'
    PATH = ($windowsPowerShellDirectory, $system32, $systemRoot -join ';')
    PATHEXT = '.COM;.EXE;.BAT;.CMD'
    PROCESSOR_ARCHITECTURE = 'AMD64'
    PSMODULEPATH = ((Join-Path $systemRoot `
        'System32\WindowsPowerShell\v1.0\Modules'),
        (Join-Path $systemRoot 'System32\WindowsPowerShell\v1.0') -join ';')
    SYSTEMDRIVE = [IO.Path]::GetPathRoot($systemRoot).TrimEnd('\')
    SYSTEMROOT = $systemRoot
    TEMP = [string]$Sandbox.temp
    TMP = [string]$Sandbox.temp
    USERPROFILE = [string]$Sandbox.userprofile
    WINDIR = $systemRoot
  }
  foreach ($name in $environment.Keys) {
    Require (-not (Test-BlockedChildEnvironmentName ([string]$name))) `
        'closed child environment contains a blocked name'
    Require ([string]$environment[$name] -notmatch '[\x00\r\n]') `
        'closed child environment contains an unsafe value'
  }
  $environment
}

function Set-ClosedProcessEnvironment(
    [System.Diagnostics.ProcessStartInfo]$Info,
    [System.Collections.IDictionary]$Environment) {
  $Info.EnvironmentVariables.Clear()
  foreach ($keyObject in $Environment.Keys) {
    $Info.EnvironmentVariables[[string]$keyObject] = [string]$Environment[$keyObject]
  }
  $actualNames = @($Info.EnvironmentVariables.Keys | ForEach-Object {
      [string]$_
    } | Sort-Object)
  $expectedNames = @($Environment.Keys | ForEach-Object { [string]$_ } | Sort-Object)
  Require (($actualNames -join "`n") -ceq ($expectedNames -join "`n")) `
      'ProcessStartInfo environment is not the exact allowlist'
  foreach ($name in $actualNames) {
    Require (-not (Test-BlockedChildEnvironmentName $name)) `
        'blocked ProcessStartInfo environment variable survived closure'
    Require ([string]$Info.EnvironmentVariables[$name] -ceq
             [string]$Environment[$name]) `
        'ProcessStartInfo environment value changed'
  }
}

function Get-EnvironmentFingerprint([System.Collections.IDictionary]$Environment) {
  $rows = @($Environment.Keys | ForEach-Object {
      $name = [string]$_
      "$name=$([string]$Environment[$_])"
    } | Sort-Object)
  Text-Hash ($rows -join "`n")
}

function Encode-HexUtf8([string]$Value) {
  ([BitConverter]::ToString($StrictUtf8.GetBytes($Value))).Replace('-', '').ToLowerInvariant()
}

function Decode-HexUtf8([string]$Hex, [string]$Label) {
  Require ($Hex -cmatch '^(?:[0-9a-f]{2})+$') "$Label is not nonempty lowercase byte hex"
  $bytes = New-Object byte[] ($Hex.Length / 2)
  for ($index = 0; $index -lt $bytes.Length; ++$index) {
    $bytes[$index] = [Convert]::ToByte($Hex.Substring($index * 2, 2), 16)
  }
  try {
    $decoded = $StrictUtf8.GetString($bytes)
  } catch {
    throw "N23 evidence requirement failed: $Label is not valid UTF-8"
  }
  Require ((Encode-HexUtf8 $decoded) -ceq $Hex) "$Label is not canonical UTF-8 hex"
  Require ($decoded.Length -gt 0 -and $decoded -notmatch '[\x00-\x1f\x7f]') "$Label contains a control character"
  $decoded
}

function Assert-EvidenceDirectory {
  [void](Assert-PlainPathChain $Root $Root 'repository root' 'Directory')
  foreach ($relative in @('docs', 'docs\nodes')) {
    [void](Assert-PlainPathChain (Join-Path $Root $relative) $Root `
        'evidence ancestor' 'Directory')
  }
  [void](Ensure-PlainDirectory $EvidenceDir $Root 'N23 evidence directory')
}

function Assert-FixedLogPaths {
  Assert-EvidenceDirectory
  $pairs = @(
    @('ProductionBuildLog', $ProductionBuildLog, $DefaultProductionLog),
    @('TestBuildLog', $TestBuildLog, $DefaultTestLog),
    @('SecondSuiteLog', $SecondSuiteLog, $DefaultSecondLog)
  )
  $resolved = New-Object System.Collections.Generic.List[string]
  foreach ($pair in $pairs) {
    $actual = Assert-ConfinedPath ([string]$pair[1]) $EvidenceDir `
        ([string]$pair[0]) $false
    $expected = Assert-ConfinedPath ([string]$pair[2]) $EvidenceDir `
        ([string]$pair[0] + ' expected path') $false
    Require ($actual.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) "$($pair[0]) must use its fixed N23 evidence filename"
    Require ((Split-Path -Parent $actual).Equals([IO.Path]::GetFullPath($EvidenceDir),
                                                 [StringComparison]::OrdinalIgnoreCase)) "$($pair[0]) escaped the N23 evidence directory"
    if ($null -ne (Get-LiteralItemOrNull $actual)) {
      [void](Assert-PlainPathChain $actual $EvidenceDir ([string]$pair[0]) 'File')
    } else {
      [void](Assert-PlainPathChain $actual $EvidenceDir ([string]$pair[0]) `
          'Missing' $true)
    }
    $resolved.Add($actual)
  }
  Require (($resolved | Sort-Object -Unique).Count -eq 3) 'native log paths alias one another'
  $script:ProductionBuildLog = $resolved[0]
  $script:TestBuildLog = $resolved[1]
  $script:SecondSuiteLog = $resolved[2]
}

function Assert-HistoricalGovernance {
  foreach ($archive in @(
      @($HistoricalPlanPath, $ExpectedHistoricalApprovedPlanHash, 25416,
        'historical approved-plan archive'),
      @($HistoricalPlanAuditPath, $ExpectedHistoricalPlanAuditHash, 14220,
        'historical plan-audit archive'),
      @($HistoricalOutcomeAuditPath, $ExpectedHistoricalOutcomeAuditHash, 1722,
        'historical outcome-audit archive')
    )) {
    [void](Assert-PlainPathChain ([string]$archive[0]) $EvidenceDir
        ([string]$archive[3]) 'File')
    $item = Get-SafeFileItem ([string]$archive[0]) $EvidenceDir $MaxJsonBytes `
        ([string]$archive[3])
    Require ((File-Hash ([string]$archive[0]) $EvidenceDir $MaxJsonBytes) -ceq
             [string]$archive[1]) "$($archive[3]) hash changed"
    Require ([int64]$item.Length -eq [int64]$archive[2]) "$($archive[3]) size changed"
  }
  $plan = Read-BoundedUtf8Text $HistoricalPlanPath $EvidenceDir $MaxJsonBytes `
      'historical approved plan'
  $audit = Read-BoundedUtf8Text $HistoricalPlanAuditPath $EvidenceDir $MaxJsonBytes `
      'historical plan audit'
  Require ($plan -match '(?m)^\*\*Status\*\*:\s*approved\s*$')
      'historical approved plan is not approved'
  $verdict = [regex]::Match($audit,
      '(?im)^\*\*VERDICT:\s*([^*\r\n]+)').Groups[1].Value.Trim()
  Require ($verdict -ceq 'PASS') 'historical plan-audit verdict is not PASS'
  [pscustomobject][ordered]@{
    approved_plan_archive = [pscustomobject][ordered]@{
      path = Relative-Path $HistoricalPlanPath
      sha256 = $ExpectedHistoricalApprovedPlanHash
      bytes = 25416
    }
    plan_audit_archive = [pscustomobject][ordered]@{
      path = Relative-Path $HistoricalPlanAuditPath
      sha256 = $ExpectedHistoricalPlanAuditHash
      bytes = 14220
    }
    outcome_audit_archive = [pscustomobject][ordered]@{
      path = Relative-Path $HistoricalOutcomeAuditPath
      sha256 = $ExpectedHistoricalOutcomeAuditHash
      bytes = 1722
    }
    audited_draft_sha256 = $ExpectedHistoricalAuditedDraftHash
  }
}

function Assert-CurrentGovernance {
  Require ((File-Hash $PlanPath $Root) -ceq $ExpectedCurrentApprovedPlanHash)
      'current approved plan hash changed'
  Require ((File-Hash $PlanAuditPath $Root) -ceq $ExpectedCurrentPlanAuditHash)
      'current plan-audit hash changed'
  $plan = Read-BoundedUtf8Text $PlanPath $Root $MaxJsonBytes 'current approved plan'
  $audit = Read-BoundedUtf8Text $PlanAuditPath $Root $MaxJsonBytes 'current plan audit'
  Require ($plan -match '(?m)^\*\*Status\*\*:\s*approved\s*$')
      'current plan is not approved'
  Require ($plan -match '(?m)^\*\*Outcome audit\*\*:\s*pending')
      'current outcome gate is not pending'
  Require ($plan -cmatch [regex]::Escape($ExpectedCurrentAuditedDraftHash))
      'current approved plan does not bind its audited draft'
  Require ($audit -cmatch ('(?im)^\*\*Auditor\*\*:\s*Claude Code\s*$'))
      'current plan audit is not attributed to Claude Code'
  $auditedPlanHash = [regex]::Match($audit,
      '(?im)^\*\*Plan SHA-256\*\*:\s*`([0-9a-f]{64})`').Groups[1].Value
  Require ($auditedPlanHash -ceq $ExpectedCurrentAuditedDraftHash)
      'current plan audit does not bind the audited draft'
  $verdict = [regex]::Match($audit,
      '(?im)^\*\*VERDICT:\s*([^*\r\n]+)').Groups[1].Value.Trim()
  Require ($verdict -ceq 'PASS') 'current plan-audit verdict is not PASS'
  [pscustomobject][ordered]@{
    plan_path = Relative-Path $PlanPath
    plan_sha256 = $ExpectedCurrentApprovedPlanHash
    audited_draft_sha256 = $ExpectedCurrentAuditedDraftHash
    plan_audit_path = Relative-Path $PlanAuditPath
    plan_audit_sha256 = $ExpectedCurrentPlanAuditHash
    status = 'approved'
    auditor = 'Claude Code'
    audit_verdict = 'PASS'
  }
}

function Assert-Governance {
  [pscustomobject][ordered]@{
    historical = Assert-HistoricalGovernance
    current = Assert-CurrentGovernance
  }
}

function Assert-Gate {
  Require ((File-Hash $GatePath $EvidenceDir) -ceq $ExpectedGateHash) 'pre-corrective gate hash changed'
  $gate = Read-Json $GatePath $EvidenceDir
  Require ([int]$gate.format_version -eq 1 -and [string]$gate.node -ceq 'N23') 'gate identity is invalid'
  Require ([string]$gate.gate -ceq 'pre-corrective-schema-v12' -and
           [string]$gate.state -ceq 'passed') 'gate state is invalid'
  Require ([string]$gate.approved_plan_sha256 -ceq $ExpectedHistoricalApprovedPlanHash) 'gate historical approved-plan binding is invalid'
  Require ([string]$gate.audited_draft_plan_sha256 -ceq $ExpectedHistoricalAuditedDraftHash) 'gate historical audited-draft binding is invalid'
  Require ([string]$gate.plan_audit_sha256 -ceq $ExpectedHistoricalPlanAuditHash) 'gate historical plan-audit binding is invalid'
  Require ([string]$gate.git_head -ceq $ExpectedGitHead) 'gate Git binding is invalid'
  Require ([string]$gate.pre_corrective_input_manifest_sha256 -ceq $ExpectedGateManifestHash) 'gate manifest binding is invalid'
  Require ([int]$gate.result.exit_code -eq 0 -and [bool]$gate.result.ok) 'gate doctor result failed'
  Require ([int]$gate.result.schema_version -eq 12 -and
           [string]$gate.result.overall -ceq 'OK') 'gate schema is not v12 OK'
  Require ([bool]$gate.result.stderr_empty) 'gate doctor stderr was not empty'
  Require ([bool]$gate.isolation.localappdata_overridden -and
           [bool]$gate.isolation.production_localappdata_not_used) 'gate isolation is invalid'
  Require ([bool]$gate.isolation.production_tree_unchanged -and
           [bool]$gate.isolation.temporary_root_removed) 'gate production-tree or cleanup proof failed'
  $gateTemporaryRoot = Resolve-RepositoryRelativePath `
      ([string]$gate.isolation.isolated_localappdata) 'gate temporary root'
  [void](Assert-PlainPathChain $gateTemporaryRoot $Root 'gate temporary root' `
      'Missing' $true)

  $rows = New-Object System.Collections.Generic.List[string]
  $changed = New-Object System.Collections.Generic.List[object]
  $unchanged = New-Object System.Collections.Generic.List[object]
  $shared = New-Object System.Collections.Generic.List[object]
  $completed = [DateTimeOffset]::Parse([string]$gate.completed_utc)
  foreach ($entry in @($gate.pre_corrective_inputs)) {
    [void](Assert-NoN30ArtifactPath ([string]$entry.path) 'gate manifest input')
    Assert-Sha256 ([string]$entry.sha256) "gate input hash"
    $rows.Add("$($entry.path)`t$($entry.sha256)`t$([int64]$entry.bytes)")
    $current = Resolve-RepositoryRelativePath ([string]$entry.path) 'gate input path'
    $currentItem = Get-SafeFileItem $current $Root $MaxHashBytes 'gate input'
    $hash = File-Hash $current $Root
    if ($ExpectedChangedGateInputs -ccontains [string]$entry.path) {
      Require ($hash -cne [string]$entry.sha256) "expected corrective input did not change"
      Require ($currentItem.LastWriteTimeUtc -gt $completed.UtcDateTime) 'corrective input does not postdate gate'
      $changed.Add([pscustomobject][ordered]@{
        path = [string]$entry.path
        gate_sha256 = [string]$entry.sha256
        current_sha256 = $hash
      })
    } elseif ($ExpectedSharedInputDriftPaths -ccontains [string]$entry.path) {
      $shared.Add([pscustomobject][ordered]@{
        path = [string]$entry.path
        gate_sha256 = [string]$entry.sha256
        gate_bytes = [int64]$entry.bytes
      })
    } else {
      Require ($hash -ceq [string]$entry.sha256) "non-N23 gate input changed"
      Require ([int64]$currentItem.Length -eq [int64]$entry.bytes) `
          'non-N23 gate input size changed'
      $unchanged.Add([pscustomobject][ordered]@{
        path = [string]$entry.path
        sha256 = $hash
      })
    }
  }
  Require ((Text-Hash (($rows.ToArray()) -join "`r`n")) -ceq
           $ExpectedGateManifestHash) 'gate input manifest digest is invalid'
  Require ((@($changed | ForEach-Object { $_.path }) -join "`n") -ceq
           ($ExpectedChangedGateInputs -join "`n")) 'changed gate input set is not exact'
  Require ((@($shared | ForEach-Object { $_.path }) -join "`n") -ceq
           ($ExpectedSharedInputDriftPaths -join "`n")) 'shared gate drift set is not exact'
  [pscustomobject][ordered]@{
    path = Relative-Path $GatePath
    sha256 = $ExpectedGateHash
    completed_utc = [string]$gate.completed_utc
    schema_version = 12
    input_count = @($gate.pre_corrective_inputs).Count
    changed_inputs = $changed.ToArray()
    shared_input_gate_facts = $shared.ToArray()
    unchanged_input_count = $unchanged.Count
    production_tree_unchanged = $true
    temporary_root_removed = $true
  }
}

function Require-JsonArray([object]$Value, [string]$Label) {
  Require ($null -ne $Value -and -not ($Value -is [string]) -and
           $Value -is [System.Collections.IEnumerable]) "$Label is not an array"
  @($Value)
}

function Get-CanonicalUtcZ([datetime]$Value) {
  $Value.ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffffffZ',
    [Globalization.CultureInfo]::InvariantCulture)
}

function Get-ObservedFileFact([string]$Path, [string]$AllowedRoot,
                              [int64]$MaximumBytes, [string]$Label) {
  $before = Get-SafeFileItem $Path $AllowedRoot $MaximumBytes $Label
  $hash = File-Hash $before.FullName $AllowedRoot $MaximumBytes
  $after = Get-SafeFileItem $before.FullName $AllowedRoot $MaximumBytes $Label
  Require ([int64]$after.Length -eq [int64]$before.Length -and
           $after.CreationTimeUtc.Ticks -eq $before.CreationTimeUtc.Ticks -and
           $after.LastWriteTimeUtc.Ticks -eq $before.LastWriteTimeUtc.Ticks -and
           [int]$after.Attributes -eq [int]$before.Attributes) `
      "$Label changed while its facts were collected"
  [pscustomobject][ordered]@{
    sha256 = $hash
    bytes = [int64]$after.Length
    creation_utc = Get-CanonicalUtcZ $after.CreationTimeUtc
    last_write_utc = Get-CanonicalUtcZ $after.LastWriteTimeUtc
    attributes = [string]$after.Attributes
  }
}

function Assert-SidecarCurrentFileFact([object]$Fact,
                                       [string[]]$ExpectedProperties,
                                       [string]$Path, [string]$Label) {
  Require ($Fact -is [pscustomobject]) "$Label is not an object"
  Require-ExactProperties $Fact $ExpectedProperties $Label
  $expectedHash = Require-JsonString $Fact.sha256 "$Label sha256"
  Assert-Sha256 $expectedHash "$Label sha256"
  $expectedBytes = Require-JsonInteger $Fact.bytes "$Label bytes"
  Require ($expectedBytes -ge 0) "$Label bytes is negative"
  if ($ExpectedProperties -contains 'creation_utc') {
    [void](Assert-SidecarUtcTimestamp $Fact.creation_utc "$Label creation time")
  }
  if ($ExpectedProperties -contains 'last_write_utc') {
    [void](Assert-SidecarUtcTimestamp $Fact.last_write_utc "$Label write time")
  }
  if ($ExpectedProperties -contains 'attributes') {
    [void](Require-JsonString $Fact.attributes "$Label attributes")
  }
  $actual = Get-ObservedFileFact $Path $Root $MaxHashBytes $Label
  Require ([string]$actual.sha256 -ceq $expectedHash) "$Label hash changed"
  Require ([int64]$actual.bytes -eq $expectedBytes) "$Label byte count changed"
  if ($ExpectedProperties -contains 'creation_utc') {
    Require ([string]$actual.creation_utc -ceq [string]$Fact.creation_utc) `
        "$Label creation time changed"
  }
  if ($ExpectedProperties -contains 'last_write_utc') {
    Require ([string]$actual.last_write_utc -ceq [string]$Fact.last_write_utc) `
        "$Label write time changed"
  }
  if ($ExpectedProperties -contains 'attributes') {
    Require ([string]$actual.attributes -ceq [string]$Fact.attributes) `
        "$Label attributes changed"
  }
  $actual
}

function Get-GateSharedInputFact([object]$Gate, [string]$Path) {
  $matches = @($Gate.shared_input_gate_facts | Where-Object {
      [string]$_.path -ceq $Path
    })
  Require ($matches.Count -eq 1) "gate lacks exactly one shared-input fact for $Path"
  $matches[0]
}

function Assert-BuildScriptSafety([object]$BuildChange) {
  Require-ExactProperties $BuildChange @(
    'path', 'classification', 'gate_baseline', 'pre_safe_edit',
    'after_safe_edit', 'delta'
  ) 'build-script sidecar entry'
  Require ([string]$BuildChange.path -ceq 'scripts/build-cl.ps1')
      'build-script sidecar path is invalid'
  Require ([string]$BuildChange.classification -ceq
      'independent-shared-build-safety-hardening')
      'build-script sidecar classification is invalid'
  $after = Assert-SidecarCurrentFileFact $BuildChange.after_safe_edit @(
      'sha256', 'bytes', 'creation_utc', 'last_write_utc', 'attributes'
    ) $BuildScript 'post-gate build script'
  Require-ExactProperties $BuildChange.pre_safe_edit @(
    'sha256', 'bytes', 'last_write_utc'
  ) 'pre-safe build-script fact'
  $preSafeHash = Require-JsonString $BuildChange.pre_safe_edit.sha256
      'pre-safe build-script hash'
  Assert-Sha256 $preSafeHash 'pre-safe build-script hash'
  $preSafeBytes = Require-JsonInteger $BuildChange.pre_safe_edit.bytes
      'pre-safe build-script bytes'
  [void](Assert-SidecarUtcTimestamp $BuildChange.pre_safe_edit.last_write_utc
      'pre-safe build-script write time')
  Require-ExactProperties $BuildChange.delta @(
    'format', 'anchor', 'reconstructed_pre_safe_sha256', 'removed_line_sha256',
    'canonical_utf8_sha256', 'canonical_utf8_bytes', 'forbidden_tokens',
    'required_tokens'
  ) 'build-script delta'
  Require ([string]$BuildChange.delta.format -ceq
      'reconstructed-single-line-removal-v1') 'build-script delta format is invalid'
  Require ([string]$BuildChange.delta.anchor -ceq $TrustedBuildDeltaAnchor)
      'build-script delta anchor is invalid'
  Require ([string]$BuildChange.delta.reconstructed_pre_safe_sha256 -ceq
      $preSafeHash) 'build-script reconstructed pre-safe hash is inconsistent'
  Assert-Sha256 ([string]$BuildChange.delta.removed_line_sha256)
      'build-script removed-line hash'
  Assert-Sha256 ([string]$BuildChange.delta.canonical_utf8_sha256)
      'build-script canonical delta hash'
  $canonicalBytes = Require-JsonInteger $BuildChange.delta.canonical_utf8_bytes
      'build-script canonical delta bytes'
  Require ($canonicalBytes -ge 0) 'build-script canonical delta bytes is negative'
  $forbidden = @(Require-JsonStringArray $BuildChange.delta.forbidden_tokens
      'build-script forbidden tokens')
  $required = @(Require-JsonStringArray $BuildChange.delta.required_tokens
      'build-script required tokens')
  Require (($forbidden -join "`n") -ceq
      (@('Get-CimInstance', 'Win32_Process', 'Stop-Process') -join "`n"))
      'build-script forbidden token set is invalid'
  Require (($required -join "`n") -ceq @(
      '[string[]]$SourceFiles', '$productionSources', '/std:c++20',
      '/OUT:qbrain.exe', 'cmd /d /s /c $batPath', 'BUILD_OK'
    ) -join "`n") 'build-script required token set is invalid'
  Require ((Text-Hash $TrustedRemovedBuildProcessLine) -ceq
      [string]$BuildChange.delta.removed_line_sha256)
      'trusted removed build-process line hash is invalid'
  $canonical = "format=$($BuildChange.delta.format)`nanchor=$($BuildChange.delta.anchor)`nremoved_line=$TrustedRemovedBuildProcessLine"
  Require ([int64]$StrictUtf8.GetByteCount($canonical) -eq $canonicalBytes)
      'build-script canonical delta byte count is invalid'
  Require ((Text-Hash $canonical) -ceq
      [string]$BuildChange.delta.canonical_utf8_sha256)
      'build-script canonical delta hash is invalid'

  $text = Read-BoundedUtf8Text $BuildScript $Root $MaxJsonBytes
      'post-gate build script'
  Require (-not $text.StartsWith([string][char]0xfeff, [StringComparison]::Ordinal))
      'post-gate build script has a UTF-8 BOM'
  foreach ($token in $forbidden) {
    Require ($text.IndexOf([string]$token,
        [StringComparison]::OrdinalIgnoreCase) -lt 0)
        "post-gate build script still contains forbidden process token $token"
  }
  foreach ($token in $required) {
    Require ($text.IndexOf([string]$token, [StringComparison]::Ordinal) -ge 0)
        "post-gate build script lost required token $token"
  }
  $anchorIndex = $text.IndexOf($TrustedBuildDeltaAnchor,
      [StringComparison]::Ordinal)
  Require ($anchorIndex -gt 0 -and $text.IndexOf($TrustedBuildDeltaAnchor,
      $anchorIndex + 1, [StringComparison]::Ordinal) -lt 0)
      'post-gate build-script anchor is not unique'
  Require ($text[$anchorIndex - 1] -eq [char]0x0a)
      'post-gate build-script anchor lacks a preceding newline'
  $newline = if ($anchorIndex -ge 2 -and
                 $text[$anchorIndex - 2] -eq [char]0x0d) { "`r`n" } else { "`n" }
  $reconstructed = $text.Insert($anchorIndex,
      $TrustedRemovedBuildProcessLine + $newline)
  Require ((Text-Hash $reconstructed) -ceq $preSafeHash)
      'post-gate build script does not reconstruct the captured pre-safe bytes'
  Require ([int64]$StrictUtf8.GetByteCount($reconstructed) -eq $preSafeBytes)
      'reconstructed pre-safe build script size is invalid'
  Require (($preSafeBytes - [int64]$after.bytes) -eq
      [int64]$StrictUtf8.GetByteCount($TrustedRemovedBuildProcessLine + $newline))
      'build-script delta is not exactly one removed process-management line'
  $tokens = $null
  $parseErrors = $null
  $ast = [Management.Automation.Language.Parser]::ParseInput(
      $text, [ref]$tokens, [ref]$parseErrors)
  Require ($parseErrors.Count -eq 0) 'post-gate build script has parser errors'
  $blockedCommands = @(
    'Get-CimInstance', 'Get-WmiObject', 'Get-Process', 'Stop-Process',
    'taskkill', 'taskkill.exe', 'wmic', 'Start-Process', 'Restart-Computer',
    'Restart-Service', 'Stop-Service'
  )
  foreach ($command in @($ast.FindAll({
        param($node)
        $node -is [Management.Automation.Language.CommandAst]
      }, $true))) {
    $name = $command.GetCommandName()
    if (-not [string]::IsNullOrWhiteSpace([string]$name)) {
      Require ($blockedCommands -cnotcontains [string]$name)
          "post-gate build script contains blocked process command $name"
    }
  }
  Require ($text -notmatch '(?i)[.]\s*(?:Kill|Terminate|CloseMainWindow)\s*\(')
      'post-gate build script contains a process-termination member call'
  $after
}

function Assert-PostGateSharedInputRevalidation([object]$Governance,
                                                [object]$Gate) {
  [void](Assert-PlainPathChain $PostGateSidecarPath $EvidenceDir
      'post-gate shared-input sidecar' 'File')
  Require ((File-Hash $PostGateSidecarPath $EvidenceDir $MaxJsonBytes) -ceq
      $ExpectedPostGateSidecarHash) 'post-gate shared-input sidecar hash changed'
  $sidecar = Read-StrictSidecarJson $PostGateSidecarPath
  Require ($sidecar -is [pscustomobject]) 'post-gate shared-input sidecar root is not an object'
  Require-ExactProperties $sidecar @(
    'format_version', 'node', 'artifact_type', 'state', 'artifact_id',
    'captured_started_utc', 'captured_completed_utc', 'immutable_gate',
    'historical_governance', 'current_governance', 'original_corrective_inputs',
    'allowed_change_set', 'safeguards'
  ) 'post-gate shared-input sidecar'
  Require ((Require-JsonInteger $sidecar.format_version 'sidecar format version') -eq 1)
      'post-gate sidecar format version is invalid'
  Require ([string]$sidecar.node -ceq 'N23' -and
           [string]$sidecar.artifact_type -ceq 'post-gate-shared-input-revalidation' -and
           [string]$sidecar.state -ceq 'captured')
      'post-gate sidecar identity is invalid'
  Require ([string]$sidecar.artifact_id -cmatch
      '^n23-shared-input-v1-[0-9a-f]{32}$') 'post-gate sidecar artifact id is invalid'
  $capturedStarted = Assert-SidecarUtcTimestamp $sidecar.captured_started_utc
      'post-gate sidecar start time'
  $capturedCompleted = Assert-SidecarUtcTimestamp $sidecar.captured_completed_utc
      'post-gate sidecar completion time'
  Require ($capturedCompleted -ge $capturedStarted)
      'post-gate sidecar completion precedes capture start'

  Require ($sidecar.immutable_gate -is [pscustomobject]) 'sidecar immutable gate is not an object'
  Require-ExactProperties $sidecar.immutable_gate @(
    'path', 'sha256', 'completed_utc', 'input_manifest_sha256', 'git_head'
  ) 'sidecar immutable gate'
  Require ([string]$sidecar.immutable_gate.path -ceq (Relative-Path $GatePath))
      'sidecar immutable-gate path is invalid'
  Require ([string]$sidecar.immutable_gate.sha256 -ceq $ExpectedGateHash -and
           [string]$sidecar.immutable_gate.input_manifest_sha256 -ceq
               $ExpectedGateManifestHash -and
           [string]$sidecar.immutable_gate.git_head -ceq $ExpectedGitHead)
      'sidecar immutable-gate binding is invalid'
  [void](Assert-SidecarUtcTimestamp $sidecar.immutable_gate.completed_utc
      'sidecar immutable-gate completion time')
  Require ([string]$sidecar.immutable_gate.completed_utc -ceq
      [string]$Gate.completed_utc) 'sidecar immutable-gate completion binding is invalid'

  Require ($sidecar.historical_governance -is [pscustomobject])
      'sidecar historical governance is not an object'
  Require-ExactProperties $sidecar.historical_governance @(
    'approved_plan_archive', 'plan_audit_archive', 'outcome_audit_archive'
  ) 'sidecar historical governance'
  foreach ($binding in @(
      @('approved_plan_archive', $Governance.historical.approved_plan_archive),
      @('plan_audit_archive', $Governance.historical.plan_audit_archive),
      @('outcome_audit_archive', $Governance.historical.outcome_audit_archive)
    )) {
    $actual = $sidecar.historical_governance.([string]$binding[0])
    Require ($actual -is [pscustomobject]) "sidecar historical $($binding[0]) is not an object"
    Require-ExactProperties $actual @('path', 'sha256', 'bytes')
        "sidecar historical $($binding[0])"
    Require ([string]$actual.path -ceq [string]$binding[1].path -and
             [string]$actual.sha256 -ceq [string]$binding[1].sha256 -and
             (Require-JsonInteger $actual.bytes "sidecar historical $($binding[0]) bytes") -eq
                 [int64]$binding[1].bytes)
        "sidecar historical $($binding[0]) binding is invalid"
    [void](Assert-NoN30ArtifactPath ([string]$actual.path)
        "sidecar historical $($binding[0]) path")
  }

  Require ($sidecar.current_governance -is [pscustomobject])
      'sidecar current governance is not an object'
  Require-ExactProperties $sidecar.current_governance @(
    'plan_path', 'plan_sha256', 'audited_draft_sha256', 'plan_audit_path',
    'plan_audit_sha256', 'status', 'auditor', 'audit_verdict'
  ) 'sidecar current governance'
  foreach ($property in @(
      'plan_path', 'plan_sha256', 'audited_draft_sha256', 'plan_audit_path',
      'plan_audit_sha256', 'status', 'auditor', 'audit_verdict'
    )) {
    Require ([string]$sidecar.current_governance.$property -ceq
             [string]$Governance.current.$property)
        "sidecar current governance $property binding is invalid"
  }
  foreach ($property in @('plan_path', 'plan_audit_path')) {
    [void](Assert-NoN30ArtifactPath ([string]$sidecar.current_governance.$property)
        "sidecar current governance $property")
  }

  $originalInputs = @(Require-JsonArray $sidecar.original_corrective_inputs
      'sidecar original corrective inputs')
  foreach ($item in $originalInputs) {
    [void](Require-JsonString $item 'sidecar original corrective input')
    [void](Assert-NoN30ArtifactPath ([string]$item)
        'sidecar original corrective input')
  }
  Require (($originalInputs -join "`n") -ceq
      ($ExpectedChangedGateInputs -join "`n"))
      'sidecar changed corrective input set is not exact'

  $changes = @(Require-JsonArray $sidecar.allowed_change_set
      'sidecar allowed change set')
  Require ($changes.Count -eq 2) 'sidecar allowed change set is not exactly two paths'
  $buildChange = $changes[0]
  $registryChange = $changes[1]
  Require ($buildChange -is [pscustomobject] -and $registryChange -is [pscustomobject])
      'sidecar allowed change set contains a non-object'
  $buildGate = Get-GateSharedInputFact $Gate 'scripts/build-cl.ps1'
  $registryGate = Get-GateSharedInputFact $Gate 'src/qbrain/ops/registry.cpp'
  Require-ExactProperties $buildChange.gate_baseline @('sha256', 'bytes')
      'build-script gate baseline'
  Require ([string]$buildChange.gate_baseline.sha256 -ceq
      [string]$buildGate.gate_sha256 -and
      (Require-JsonInteger $buildChange.gate_baseline.bytes
        'build-script gate baseline bytes') -eq [int64]$buildGate.gate_bytes)
      'build-script gate baseline is invalid'
  $buildFact = Assert-BuildScriptSafety $buildChange
  Require-ExactProperties $registryChange @(
    'path', 'classification', 'gate_baseline', 'after', 'n23_edit_allowed'
  ) 'registry sidecar entry'
  Require ([string]$registryChange.path -ceq 'src/qbrain/ops/registry.cpp' -and
           [string]$registryChange.classification -ceq
               'independent-shared-registry-boundary')
      'registry sidecar identity is invalid'
  Require-ExactProperties $registryChange.gate_baseline @('sha256', 'bytes')
      'registry gate baseline'
  Require ([string]$registryChange.gate_baseline.sha256 -ceq
      [string]$registryGate.gate_sha256 -and
      (Require-JsonInteger $registryChange.gate_baseline.bytes
        'registry gate baseline bytes') -eq [int64]$registryGate.gate_bytes)
      'registry gate baseline is invalid'
  Require ($registryChange.n23_edit_allowed -is [bool] -and
           -not [bool]$registryChange.n23_edit_allowed)
      'registry sidecar permits an N23 edit'
  [void](Assert-NoN30ArtifactPath ([string]$registryChange.path)
      'registry sidecar path')
  $registryFact = Assert-SidecarCurrentFileFact $registryChange.after @(
      'sha256', 'bytes', 'creation_utc', 'last_write_utc', 'attributes'
    ) (Join-Path $Root 'src\qbrain\ops\registry.cpp') 'post-gate registry source'

  Require ($sidecar.safeguards -is [pscustomobject]) 'sidecar safeguards are not an object'
  Require-ExactProperties $sidecar.safeguards @(
    'build_script_process_termination_removed', 'registry_n23_edit_allowed',
    'sidecar_overwrite_forbidden', 'n30_path_component_forbidden'
  ) 'sidecar safeguards'
  foreach ($property in @(
      'build_script_process_termination_removed', 'sidecar_overwrite_forbidden',
      'n30_path_component_forbidden'
    )) {
    Require ($sidecar.safeguards.$property -is [bool] -and
             [bool]$sidecar.safeguards.$property)
        "sidecar safeguard $property is not true"
  }
  Require ($sidecar.safeguards.registry_n23_edit_allowed -is [bool] -and
           -not [bool]$sidecar.safeguards.registry_n23_edit_allowed)
      'sidecar safeguard permits registry N23 edits'
  [pscustomobject][ordered]@{
    path = Relative-Path $PostGateSidecarPath
    sha256 = $ExpectedPostGateSidecarHash
    artifact_id = [string]$sidecar.artifact_id
    captured_started_utc = [string]$sidecar.captured_started_utc
    captured_completed_utc = [string]$sidecar.captured_completed_utc
    build_script = $buildFact
    registry = $registryFact
  }
}

function Get-LiteralArrayAssignment([string]$ScriptPath, [string]$VariableName) {
  $text = Read-BoundedUtf8Text $ScriptPath $Root $MaxJsonBytes `
      "$VariableName build-script input"
  $tokens = $null
  $parseErrors = $null
  $ast = [Management.Automation.Language.Parser]::ParseInput(
      $text, [ref]$tokens, [ref]$parseErrors)
  Require ($parseErrors.Count -eq 0) "$VariableName build script has parser errors"
  $assignments = @($ast.FindAll({
        param($node)
        $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left -is [Management.Automation.Language.VariableExpressionAst] -and
        $node.Left.VariablePath.UserPath -ceq $VariableName -and
        $node.Right.Extent.Text.TrimStart().StartsWith('@(')
      }, $true))
  Require ($assignments.Count -eq 1) `
      "$VariableName is not assigned one literal array"
  $arrays = @($assignments[0].Right.FindAll({
        param($node)
        $node -is [Management.Automation.Language.ArrayExpressionAst]
      }, $true))
  Require ($arrays.Count -eq 1) "$VariableName literal array shape is invalid"
  $statements = @($arrays[0].SubExpression.Statements)
  Require ($statements.Count -eq 1 -and
           $statements[0] -is [Management.Automation.Language.PipelineAst] -and
           $statements[0].PipelineElements.Count -eq 1 -and
           $statements[0].PipelineElements[0] -is
               [Management.Automation.Language.CommandExpressionAst]) `
      "$VariableName literal array statement shape is invalid"
  $literal = $statements[0].PipelineElements[0].Expression
  Require ($literal -is [Management.Automation.Language.ArrayLiteralAst]) `
      "$VariableName is not an array of constants"
  $values = New-Object System.Collections.Generic.List[string]
  foreach ($element in @($literal.Elements)) {
    Require ($element -is [Management.Automation.Language.StringConstantExpressionAst]) `
        "$VariableName contains a nonliteral element"
    $value = [string]$element.Value
    Require (-not [string]::IsNullOrWhiteSpace($value) -and
             -not [IO.Path]::IsPathRooted($value)) `
        "$VariableName contains an invalid relative path"
    $full = Resolve-RepositoryRelativePath $value "$VariableName source"
    [void](Assert-PlainPathChain $full $Root "$VariableName source" 'File')
    $values.Add((Relative-Path $full))
  }
  Require ($values.Count -gt 0 -and
           (@($values | Sort-Object -Unique)).Count -eq $values.Count) `
      "$VariableName source array is empty or contains duplicates"
  $values.ToArray()
}

function Get-LiteralScalarAssignment([string]$ScriptPath,
                                     [string]$VariableName) {
  $text = Read-BoundedUtf8Text $ScriptPath $Root $MaxJsonBytes `
      "$VariableName build-script input"
  $tokens = $null
  $parseErrors = $null
  $ast = [Management.Automation.Language.Parser]::ParseInput(
      $text, [ref]$tokens, [ref]$parseErrors)
  Require ($parseErrors.Count -eq 0) "$VariableName build script has parser errors"
  $assignments = @($ast.FindAll({
        param($node)
        $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left -is [Management.Automation.Language.VariableExpressionAst] -and
        $node.Left.VariablePath.UserPath -ceq $VariableName -and
        $node.Right -is [Management.Automation.Language.CommandExpressionAst] -and
        $node.Right.Expression -is
            [Management.Automation.Language.StringConstantExpressionAst]
      }, $true))
  Require ($assignments.Count -eq 1) `
      "$VariableName is not assigned one literal string"
  [string]$assignments[0].Right.Expression.Value
}

function Get-BuildSourceClosure {
  $production = @(Get-LiteralArrayAssignment $BuildScript 'productionSources')
  $tests = @(Get-LiteralArrayAssignment $TestBuildScript 'defaultTestSources')
  $focused = @($tests | Where-Object { [string]$_ -ceq 'tests/test_n23.cpp' })
  Require ($focused.Count -eq 1) 'actual test build source array lacks exactly one N23 focused source'
  $integrated = @($tests | Where-Object { [string]$_ -cne 'tests/test_n23.cpp' })
  Require ($integrated.Count -gt 0) 'actual test build source array lacks integrated regressions'
  [pscustomobject][ordered]@{
    production_sources = $production
    production_sources_sha256 = Text-Hash ($production -join "`n")
    test_sources = $tests
    test_sources_sha256 = Text-Hash ($tests -join "`n")
    n23_focused_test_sources = $focused
    integrated_regression_test_sources = $integrated
    integrated_regression_test_count = $integrated.Count
  }
}

function Get-RegisteredTests {
  $mainPath = Join-Path $Root 'tests\test_main.cpp'
  $text = Read-BoundedUtf8Text $mainPath $Root $MaxJsonBytes 'test registry'
  $matches = [regex]::Matches($text, '\{\s*"([^"]+)"\s*,\s*test_[A-Za-z0-9_]+\s*\}')
  $names = @($matches | ForEach-Object { $_.Groups[1].Value })
  Require ($names.Count -ge $MinimumRegisteredTestCount) `
      'registered suite count is below the approved minimum closure'
  Require (($names | Sort-Object -Unique).Count -eq $names.Count) 'registered tests are not unique'
  Require (@($names | Where-Object { $_ -ceq 'n23' }).Count -eq 1) 'dedicated n23 test is not registered exactly once'
  $names
}

function Assert-Wiring([object]$BuildClosure) {
  $main = Read-BoundedUtf8Text (Join-Path $Root 'tests\test_main.cpp') $Root `
      $MaxJsonBytes 'test registry'
  $cmake = Read-BoundedUtf8Text (Join-Path $Root 'CMakeLists.txt') $Root `
      $MaxJsonBytes 'CMake wiring'
  $productionScript = Read-BoundedUtf8Text $BuildScript $Root $MaxJsonBytes `
      'production build script'
  $testsScript = Read-BoundedUtf8Text $TestBuildScript $Root $MaxJsonBytes `
      'test build script'
  Require ([regex]::Matches($main, '(?m)^void\s+test_n23\s*\(\s*\)\s*;\s*$').Count -eq 1) 'test_main declaration is not exact'
  Require ([regex]::Matches($main, '\{\s*"n23"\s*,\s*test_n23\s*\}').Count -eq 1) 'test_main registration is not exact'
  Require ([regex]::Matches($cmake, '(?m)^\s*tests/test_n23[.]cpp\s*$').Count -eq 1) 'CMake registration is not exact'
  Require (@($BuildClosure.n23_focused_test_sources).Count -eq 1) `
      'MSVC test source registration is not exact'
  $expectedRoot = Get-NormalizedFullPath $Root 'repository root'
  foreach ($scriptPath in @($BuildScript, $TestBuildScript)) {
    $scriptRoot = Get-NormalizedFullPath `
        (Get-LiteralScalarAssignment $scriptPath 'Root') 'build-script Root'
    Require ($scriptRoot.Equals($expectedRoot,
            [StringComparison]::OrdinalIgnoreCase)) `
        'build script is not rooted at the verifier workspace'
  }
  Require ([regex]::Matches($productionScript, '(?m)^\s*call\s+"[$]vcvars"\s+x64\s*$').Count -eq 1) 'production build does not initialize x64 MSVC exactly once'
  Require ([regex]::Matches($testsScript, '(?m)^\s*call\s+"[$]vcvars"\s+x64\s*$').Count -eq 1) 'test build does not initialize x64 MSVC exactly once'
  Require ([regex]::Matches($productionScript, '(?m)^\s*cl\s+/nologo\s+/std:c\+\+20\b').Count -eq 2) 'production build C++20 compile commands are not exact'
  Require ([regex]::Matches($testsScript, '(?m)^\s*cl\s+/nologo\s+/std:c\+\+20\b').Count -eq 1) 'test build C++20 compile command is not exact'
  Require ([regex]::Matches($productionScript, '(?m)^\s*echo\s+BUILD_OK\s*$').Count -eq 1) 'production success marker is not exact'
  Require ([regex]::Matches($testsScript, '(?m)^\s*echo\s+TESTS_BUILD_OK\s*$').Count -eq 1) 'test success marker is not exact'
  Require ([regex]::Matches($testsScript, '(?m)^\s*qbrain_tests[.]exe\s*$').Count -eq 1) 'test build does not run the canonical suite exactly once'
}

function Get-InputEntries([object]$BuildClosure) {
  $roles = @{}
  foreach ($relative in @($BuildClosure.production_sources)) {
    [void](Assert-NoN30ArtifactPath ([string]$relative) 'production build source')
    $roles[[string]$relative] = 'production-build-source'
  }
  foreach ($relative in @($BuildClosure.test_sources)) {
    [void](Assert-NoN30ArtifactPath ([string]$relative) 'test build source')
    $roles[[string]$relative] = if ([string]$relative -ceq 'tests/test_n23.cpp') {
      'n23-focused-test-evidence'
    } else {
      'integrated-regression-test-source'
    }
  }
  $includeRoot = Join-Path $Root 'include'
  [void](Assert-NoN30ArtifactPath $includeRoot 'compile header closure')
  foreach ($header in @(Get-SafeTreeLeafPaths $includeRoot $Root `
        'compile header closure')) {
    if ([IO.Path]::GetExtension($header) -in @('.h', '.hpp')) {
      $roles[(Relative-Path $header)] = 'compile-header'
    }
  }
  $staticRoles = [ordered]@{
    'CMakeLists.txt' = 'wiring-governance'
    'scripts/build-cl.ps1' = 'production-build-driver'
    'scripts/build-tests-cl.ps1' = 'test-build-driver'
    'scripts/n23-verify.ps1' = 'n23-verifier'
    'schema/001_init.sql' = 'schema-contract-input'
    'AGENTS.md' = 'governance-input'
    'docs/nodes/README.md' = 'governance-input'
    'docs/nodes/N23-PLAN.md' = 'approved-plan-input'
    'docs/nodes/N23-PLAN-AUDIT.md' = 'approved-plan-audit-input'
    'docs/nodes/n23-evidence/PRE-CORRECTIVE-SCHEMA-GATE.json' = 'immutable-pre-corrective-gate'
    'docs/nodes/n23-evidence/N23-PLAN-APPROVED-BASELINE.md' = 'immutable-historical-approved-plan'
    'docs/nodes/n23-evidence/N23-PLAN-AUDIT-BASELINE.md' = 'immutable-historical-plan-audit'
    'docs/nodes/n23-evidence/N23-HARD-AUDIT-BASELINE.md' = 'immutable-historical-outcome-audit'
    'docs/nodes/n23-evidence/POST-GATE-SHARED-INPUT-REVALIDATION-v1.json' = 'immutable-post-gate-sidecar'
    'third_party/nlohmann/json.hpp' = 'third-party-compile-input'
    'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.c' = 'production-build-source'
    'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.h' = 'third-party-compile-input'
  }
  foreach ($relative in $staticRoles.Keys) {
    [void](Assert-NoN30ArtifactPath ([string]$relative) 'static build input')
    if ($roles.ContainsKey([string]$relative)) {
      Require ([string]$roles[[string]$relative] -ceq
               [string]$staticRoles[$relative]) `
          'build input role assignment conflicts'
    } else {
      $roles[[string]$relative] = [string]$staticRoles[$relative]
    }
  }
  $entries = New-Object System.Collections.Generic.List[object]
  foreach ($relative in @($roles.Keys | Sort-Object)) {
    [void](Assert-NoN30ArtifactPath ([string]$relative) 'build input path')
    $path = Resolve-RepositoryRelativePath ([string]$relative) 'build input path'
    [void](Assert-PlainPathChain $path $Root 'build input' 'File')
    $entries.Add((File-Entry $path ([string]$roles[$relative])))
  }
  $entries.ToArray()
}

function Assert-FrozenInputEntries([object[]]$Current, [object[]]$Frozen,
                                   [string]$Label) {
  Require ($Current.Count -eq $Frozen.Count) "$Label input count changed"
  for ($index = 0; $index -lt $Current.Count; ++$index) {
    Require ([string]$Current[$index].path -ceq [string]$Frozen[$index].path)
        "$Label input path changed"
    Require ([string]$Current[$index].sha256 -ceq [string]$Frozen[$index].sha256)
        "$Label input hash changed"
    Require ([string]$Current[$index].role -ceq [string]$Frozen[$index].role)
        "$Label input role changed"
    Require ([int64]$Current[$index].bytes -eq [int64]$Frozen[$index].bytes)
        "$Label input size changed"
  }
}

function Assert-StageBindings([object]$Preparation, [string]$Stage) {
  Require ($Stage -cin @(
      'before-production-build', 'after-production-build',
      'after-test-build-suite-run-1', 'after-full-suite-run-2',
      'before-final-doctor', 'after-final-doctor',
      'before-evidence-publication', 'before-final-manifest-publication'
    )) 'unknown post-gate stage binding'
  $governance = Assert-Governance
  $gate = Assert-Gate
  $sidecar = Assert-PostGateSharedInputRevalidation $governance $gate
  Require ([string]$sidecar.sha256 -ceq
      [string]$Preparation.manifest.post_gate_shared_input_revalidation.sha256)
      'post-gate sidecar changed after preparation'
  Require (($governance | ConvertTo-Json -Depth 8 -Compress) -ceq
      ($Preparation.manifest.governance | ConvertTo-Json -Depth 8 -Compress))
      'governance changed after preparation'
  Require ([string]$gate.sha256 -ceq
      [string]$Preparation.manifest.pre_corrective_gate.sha256)
      'pre-corrective gate changed after preparation'
  $buildClosure = Get-BuildSourceClosure
  Assert-Wiring $buildClosure
  $currentInputs = @(Get-InputEntries $buildClosure)
  Assert-FrozenInputEntries $currentInputs @($Preparation.manifest.inputs)
      "stage $Stage"
  [pscustomobject][ordered]@{
    stage = $Stage
    checked_utc = [DateTimeOffset]::UtcNow.ToString('o')
    post_gate_sidecar_sha256 = [string]$sidecar.sha256
    build_script_sha256 = [string]$sidecar.build_script.sha256
    registry_sha256 = [string]$sidecar.registry.sha256
    pre_corrective_gate_sha256 = [string]$gate.sha256
    historical_approved_plan_sha256 =
        [string]$governance.historical.approved_plan_archive.sha256
    current_approved_plan_sha256 = [string]$governance.current.plan_sha256
    current_plan_audit_sha256 = [string]$governance.current.plan_audit_sha256
  }
}

function Record-StageBinding([object]$Preparation, [string]$Stage) {
  Require ($null -ne $script:StageBindingRecords)
      'stage-binding record collection is not initialized'
  $record = Assert-StageBindings $Preparation $Stage
  $script:StageBindingRecords.Add($record)
  $record
}

function Get-SystemTempRoot {
  $candidate = Get-NormalizedFullPath ([IO.Path]::GetTempPath()) `
      'Windows temporary root'
  $item = Get-LiteralItemOrNull $candidate
  Require ($null -ne $item -and $item.PSIsContainer) `
      'Windows temporary root is missing'
  $canonical = Get-NormalizedFullPath $item.FullName 'Windows temporary root'
  [void](Assert-PlainPathChain $canonical $canonical `
      'Windows temporary root' 'Directory')
  $canonical
}

function Get-BuildBatchPaths {
  $tempRoot = Get-SystemTempRoot
  $facts = New-Object System.Collections.Generic.List[object]
  foreach ($pair in @(
      @('production', $BuildScript),
      @('test', $TestBuildScript))) {
    $literal = Get-LiteralScalarAssignment ([string]$pair[1]) 'batPath'
    Require ([IO.Path]::IsPathRooted($literal)) `
        'build-script batch path is not rooted'
    $literalParent = Split-Path -Parent $literal
    $parentItem = Get-LiteralItemOrNull $literalParent
    Require ($null -ne $parentItem -and $parentItem.PSIsContainer) `
        'build-script batch parent is missing'
    $canonicalParent = Get-NormalizedFullPath $parentItem.FullName `
        'build-script batch parent'
    [void](Assert-PlainPathChain $canonicalParent $tempRoot `
        'build-script batch parent' 'Directory')
    $canonical = Assert-ConfinedPath `
        (Join-Path $canonicalParent ([IO.Path]::GetFileName($literal))) `
        $tempRoot 'build-script batch path' $false
    if ($null -ne (Get-LiteralItemOrNull $canonical)) {
      [void](Assert-PlainPathChain $canonical $tempRoot `
          'build-script batch file' 'File')
    } else {
      [void](Assert-PlainPathChain $canonical $tempRoot `
          'build-script batch file' 'Missing' $true)
    }
    $facts.Add([pscustomobject][ordered]@{
        stage = [string]$pair[0]
        path = $canonical.Replace('\', '/')
      })
  }
  Require (($facts | ForEach-Object { [string]$_.path } |
        Sort-Object -Unique).Count -eq 2) `
      'build-script batch paths alias one another'
  $facts.ToArray()
}

function Get-ToolchainFacts {
  $productionVcvars = Get-LiteralScalarAssignment $BuildScript 'vcvars'
  $testVcvars = Get-LiteralScalarAssignment $TestBuildScript 'vcvars'
  Require ($productionVcvars -ceq $testVcvars) `
      'build scripts resolve different vcvarsall inputs'
  $vcvarsFact = Get-ExecutableFact $productionVcvars 'vcvarsall'
  $tools = Get-BaseToolFacts
  $sandbox = $null
  try {
    $sandbox = New-Sandbox 'n23-tool-discovery-'
    $batchPath = Join-Path ([string]$sandbox.root) 'discover-msvc.bat'
    $batchLines = @(
      '@echo off',
      ('call "' + (Get-ToolPath $vcvarsFact) + '" x64 >nul'),
      'if errorlevel 1 exit /b 1',
      'where.exe cl.exe',
      'if errorlevel 1 exit /b 1',
      'echo N23_TOOLCHAIN_SPLIT',
      'where.exe link.exe',
      'if errorlevel 1 exit /b 1'
    )
    Write-AtomicUtf8Lines $batchPath $batchLines ([string]$sandbox.root) 64KB
    $capture = Invoke-SeparateCapture (Get-ToolPath $tools.cmd) @(
        '/d', '/s', '/c', $batchPath) $sandbox $ToolDiscoveryTimeoutSeconds 4MB
    Require ($capture.exit_code -eq 0) 'MSVC toolchain discovery failed'
    Require ([string]::IsNullOrWhiteSpace([string]$capture.stderr)) `
        'MSVC toolchain discovery wrote stderr'
    $lines = @(Convert-CapturedTextToLines $capture.stdout | Where-Object {
        -not [string]::IsNullOrWhiteSpace([string]$_)
      })
    $splitIndexes = New-Object System.Collections.Generic.List[int]
    for ($index = 0; $index -lt $lines.Count; ++$index) {
      if ([string]$lines[$index] -ceq 'N23_TOOLCHAIN_SPLIT') {
        $splitIndexes.Add($index)
      }
    }
    Require ($splitIndexes.Count -eq 1 -and $splitIndexes[0] -gt 0 -and
             $splitIndexes[0] -lt ($lines.Count - 1)) `
        'MSVC toolchain discovery framing is invalid'
    $clPath = [string]$lines[0]
    $linkPath = [string]$lines[$splitIndexes[0] + 1]
    Require ([IO.Path]::IsPathRooted($clPath) -and
             [IO.Path]::IsPathRooted($linkPath)) `
        'MSVC toolchain discovery returned a non-rooted path'
    [pscustomobject][ordered]@{
      vcvarsall = $vcvarsFact
      cl = Get-ExecutableFact $clPath 'cl'
      link = Get-ExecutableFact $linkPath 'link'
      discovery_stdout_sha256 = Text-Hash ([string]$capture.stdout)
    }
  } finally {
    Remove-Sandboxes @($sandbox)
  }
}

function Get-AllToolFacts {
  $base = Get-BaseToolFacts -Refresh
  [pscustomobject][ordered]@{
    verifier_host = $base.verifier_host
    windows_powershell = $base.windows_powershell
    cmd = $base.cmd
    where = $base.where
    git = $base.git
    msvc = Get-ToolchainFacts
    build_batch_paths = @(Get-BuildBatchPaths)
  }
}

function Assert-ToolFactsBinding([object]$Current, [object]$Frozen) {
  $currentJson = $Current | ConvertTo-Json -Depth 8 -Compress
  $frozenJson = $Frozen | ConvertTo-Json -Depth 8 -Compress
  Require ($currentJson -ceq $frozenJson) `
      'executable/toolchain paths or hashes changed after preparation'
}

function Get-GitRepositoryBinding {
  $dotGit = Join-Path $Root '.git'
  $item = Get-LiteralItemOrNull $dotGit
  Require ($null -ne $item) 'workspace .git entry is missing'
  [void](Assert-PlainPathChain $dotGit $Root 'workspace .git entry' `
      $(if ($item.PSIsContainer) { 'Directory' } else { 'File' }))
  if ($item.PSIsContainer) {
    $gitDirectory = $item.FullName
  } else {
    $gitFile = Read-BoundedUtf8Text $dotGit $Root 4096 'workspace .git file'
    $match = [regex]::Match($gitFile.Trim(), '^gitdir:\s*(.+)$')
    Require ($match.Success -and $match.Groups[1].Value -notmatch '[\x00\r\n]') `
        'workspace .git file is malformed'
    $candidate = $match.Groups[1].Value
    if (-not [IO.Path]::IsPathRooted($candidate)) {
      $candidate = Join-Path $Root $candidate
    }
    $gitDirectory = Get-NormalizedFullPath $candidate 'Git directory'
    [void](Assert-PlainPathChain $gitDirectory $gitDirectory `
        'Git directory' 'Directory')
  }
  $commonFile = Join-Path $gitDirectory 'commondir'
  if ($null -ne (Get-LiteralItemOrNull $commonFile)) {
    $commonText = (Read-BoundedUtf8Text $commonFile $gitDirectory 4096 `
        'Git commondir file').Trim()
    Require ($commonText.Length -gt 0 -and $commonText -notmatch '[\x00\r\n]') `
        'Git commondir file is malformed'
    $commonDirectory = if ([IO.Path]::IsPathRooted($commonText)) {
      $commonText
    } else {
      Join-Path $gitDirectory $commonText
    }
    $commonDirectory = Get-NormalizedFullPath $commonDirectory `
        'Git common directory'
  } else {
    $commonDirectory = Get-NormalizedFullPath $gitDirectory 'Git common directory'
  }
  [void](Assert-PlainPathChain $commonDirectory $commonDirectory `
      'Git common directory' 'Directory')
  [pscustomobject][ordered]@{
    work_tree = Get-NormalizedFullPath $Root 'Git work tree'
    git_dir = Get-NormalizedFullPath $gitDirectory 'Git directory'
    common_dir = $commonDirectory
  }
}

function Invoke-GitRead([object]$Binding, [object]$Sandbox,
                        [string[]]$Arguments) {
  Require ($Arguments.Count -ge 1 -and
           [string]$Arguments[0] -in @('rev-parse', 'diff', 'ls-files')) `
      'Git read command is not allowlisted'
  $tools = Get-BaseToolFacts
  $gitArguments = @(
    "--git-dir=$([string]$Binding.git_dir)"
    "--work-tree=$([string]$Binding.work_tree)"
    '-c'
    'core.autocrlf=false'
    '-c'
    'core.safecrlf=false'
  ) + @($Arguments)
  $capture = Invoke-SeparateCapture (Get-ToolPath $tools.git) $gitArguments `
      $Sandbox 60 32MB
  Require ($capture.exit_code -eq 0) `
      "read-only Git command failed with exit $($capture.exit_code): $(([string]$capture.stderr).Trim())"
  Require ([string]::IsNullOrWhiteSpace([string]$capture.stderr)) `
      'read-only Git command wrote stderr'
  [string]$capture.stdout
}

function Get-GitFacts {
  $binding = Get-GitRepositoryBinding
  $sandbox = $null
  try {
    $sandbox = New-Sandbox 'n23-git-read-'
    $topLevelText = (Invoke-GitRead $binding $sandbox @(
        'rev-parse', '--show-toplevel')).Trim()
    $topLevel = Get-NormalizedFullPath $topLevelText 'Git top-level output'
    Require ($topLevel.Equals([string]$binding.work_tree,
            [StringComparison]::OrdinalIgnoreCase)) `
        'Git top-level is not the bound work tree'
    $gitDirectoryText = (Invoke-GitRead $binding $sandbox @(
        'rev-parse', '--absolute-git-dir')).Trim()
    $gitDirectory = Get-NormalizedFullPath $gitDirectoryText `
        'Git directory output'
    Require ($gitDirectory.Equals([string]$binding.git_dir,
            [StringComparison]::OrdinalIgnoreCase)) `
        'Git directory differs from the explicit binding'
    $commonDirectoryText = (Invoke-GitRead $binding $sandbox @(
        'rev-parse', '--path-format=absolute', '--git-common-dir')).Trim()
    $commonDirectory = Get-NormalizedFullPath $commonDirectoryText `
        'Git common-directory output'
    Require ($commonDirectory.Equals([string]$binding.common_dir,
            [StringComparison]::OrdinalIgnoreCase)) `
        'Git common directory differs from the explicit binding'
    $head = (Invoke-GitRead $binding $sandbox @(
        'rev-parse', '--verify', 'HEAD')).Trim().ToLowerInvariant()
    Require ($head -ceq $ExpectedGitHead) 'Git HEAD changed'

    $diffText = Invoke-GitRead $binding $sandbox (@(
        'diff', '--no-ext-diff', '--no-textconv', '--unified=0', 'HEAD', '--') + $ScopedPaths)
    $untrackedText = Invoke-GitRead $binding $sandbox (@(
        'ls-files', '--others', '--exclude-standard', '--') + $ScopedPaths)
    $untrackedScoped = @(Convert-CapturedTextToLines $untrackedText |
        Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) } |
        Sort-Object)
    $diffRows = New-Object System.Collections.Generic.List[string]
    foreach ($line in @(Convert-CapturedTextToLines $diffText)) {
      $diffRows.Add([string]$line)
    }
    foreach ($relative in $untrackedScoped) {
      [void](Assert-NoN30ArtifactPath ([string]$relative) 'scoped untracked input')
      $path = Resolve-RepositoryRelativePath ([string]$relative) `
          'scoped untracked input'
      $item = Get-SafeFileItem $path $Root $MaxHashBytes `
          'scoped untracked input'
      $diffRows.Add("UNTRACKED`t$relative`t$(File-Hash $path $Root)`t$([int64]$item.Length)")
    }
    $scopedDiffText = $diffRows.ToArray() -join "`n"
    [pscustomobject][ordered]@{
      head = $head
      work_tree = $topLevel.Replace('\', '/')
      git_dir = $gitDirectory.Replace('\', '/')
      common_dir = $commonDirectory.Replace('\', '/')
      scoped_diff_sha256 = Text-Hash $scopedDiffText
      scoped_untracked_input_count = $untrackedScoped.Count
    }
  } finally {
    Remove-Sandboxes @($sandbox)
  }
}

function Assert-GitFactsBinding([object]$Current, [object]$Frozen) {
  foreach ($property in @('head', 'work_tree', 'git_dir', 'common_dir',
                           'scoped_diff_sha256')) {
    Require ([string]$Current.$property -ceq [string]$Frozen.$property) `
        "Git $property differs from the frozen manifest"
  }
  Require ([int]$Current.scoped_untracked_input_count -eq
           [int]$Frozen.scoped_untracked_input_count) 'scoped untracked input count changed'
}

function Write-Pending([string]$PreparedUtc, [string]$PrebuildHash,
                       [string]$EvidenceNonce, [string]$PostGateSidecarHash,
                       [string]$PendingReportPath = $ReportPath,
                       [string]$PendingManifestPath = $ManifestPath,
                       [string]$AllowedRoot = $EvidenceDir) {
  Assert-Sha256 $PrebuildHash 'pending prebuild manifest hash'
  Assert-Sha256 $PostGateSidecarHash 'pending post-gate sidecar hash'
  Require ($EvidenceNonce -cmatch '^[0-9a-f]{32}$') 'pending evidence nonce is invalid'
  [void](Assert-NoN30ArtifactPath $PendingReportPath 'pending evidence report')
  [void](Assert-NoN30ArtifactPath $PendingManifestPath 'pending evidence manifest')
  $reportFull = Assert-ConfinedPath $PendingReportPath $AllowedRoot `
      'pending evidence report' $false
  $manifestFull = Assert-ConfinedPath $PendingManifestPath $AllowedRoot `
      'pending evidence manifest' $false
  Require (-not $reportFull.Equals($manifestFull,
           [StringComparison]::OrdinalIgnoreCase)) `
      'pending report and manifest paths alias one another'
  $report = @(
    '# N23 Verification Report',
    '',
    'State: pending native evidence and fresh Claude Code outcome audit.',
    "Prepared UTC: $PreparedUtc",
    "Prebuild manifest SHA-256: $PrebuildHash",
    "Post-gate sidecar SHA-256: $PostGateSidecarHash",
    "Evidence nonce: $EvidenceNonce",
    'Production-data, Git-mutation, and protected-configuration telemetry: not-collected.'
  )
  Write-AtomicUtf8Lines $reportFull $report $AllowedRoot
  $reportHash = File-Hash $reportFull $AllowedRoot $MaxImportedLogBytes
  $pending = [pscustomobject][ordered]@{
    format_version = 3
    node = 'N23'
    state = 'pending-native-evidence-and-claude-outcome-audit'
    prepared_utc = $PreparedUtc
    prebuild_manifest_sha256 = $PrebuildHash
    post_gate_shared_input_revalidation_sha256 = $PostGateSidecarHash
    pending_report_sha256 = $reportHash
    evidence_nonce = $EvidenceNonce
    audit_verdict_written = $false
    node_or_ledger_status_written = $false
  }
  Write-AtomicUtf8Text $manifestFull `
      (($pending | ConvertTo-Json -Depth 4) + [Environment]::NewLine) `
      $AllowedRoot
}

function Read-PendingManifest([string]$PendingPath, [string]$FrozenPrebuildPath,
                               [string]$ExpectedPreparedUtc,
                               [string]$ExpectedEvidenceNonce,
                               [string]$ExpectedPostGateSidecarHash,
                               [string]$AllowedRoot = $EvidenceDir) {
  [void](Assert-NoN30ArtifactPath $PendingPath 'pending evidence manifest')
  [void](Assert-NoN30ArtifactPath $FrozenPrebuildPath 'frozen prebuild manifest')
  [void](Assert-PlainPathChain $PendingPath $AllowedRoot `
      'pending evidence manifest' 'File')
  [void](Assert-PlainPathChain $FrozenPrebuildPath $AllowedRoot `
      'frozen prebuild manifest' 'File')
  $pending = Read-Json $PendingPath $AllowedRoot
  Require-ExactProperties $pending @(
    'format_version', 'node', 'state', 'prepared_utc',
    'prebuild_manifest_sha256', 'post_gate_shared_input_revalidation_sha256',
    'pending_report_sha256', 'evidence_nonce',
    'audit_verdict_written', 'node_or_ledger_status_written'
  ) 'pending evidence manifest'
  Require ([int]$pending.format_version -eq 3 -and
           [string]$pending.node -ceq 'N23') 'pending manifest identity is invalid'
  Require ([string]$pending.state -ceq
           'pending-native-evidence-and-claude-outcome-audit') 'pending manifest state is invalid'
  Require ([string]$pending.prepared_utc -ceq $ExpectedPreparedUtc) 'pending manifest prepared time is not frozen'
  Require ([string]$pending.evidence_nonce -ceq $ExpectedEvidenceNonce) 'pending manifest nonce is not frozen'
  Require ([string]$pending.post_gate_shared_input_revalidation_sha256 -ceq
           $ExpectedPostGateSidecarHash) 'pending manifest sidecar binding is not frozen'
  Require ([string]$pending.prebuild_manifest_sha256 -ceq
           (File-Hash $FrozenPrebuildPath $AllowedRoot)) `
      'pending manifest is not bound to the frozen prebuild manifest'
  Assert-Sha256 ([string]$pending.pending_report_sha256) 'pending report hash'
  $pendingReportPath = Join-Path (Split-Path -Parent $PendingPath) 'VERIFY-REPORT.md'
  [void](Assert-PlainPathChain $pendingReportPath $AllowedRoot `
      'pending evidence report' 'File')
  Require ([string]$pending.pending_report_sha256 -ceq
           (File-Hash $pendingReportPath $AllowedRoot $MaxImportedLogBytes)) `
      'pending manifest is not bound to the pending report'
  Require (-not [bool]$pending.audit_verdict_written -and
           -not [bool]$pending.node_or_ledger_status_written) 'pending manifest claims forbidden authority'
  $pending
}

function New-Preparation {
  Assert-EvidenceBuildMutexHeld
  Assert-EvidenceDirectory
  $governance = Assert-Governance
  $gate = Assert-Gate
  $sidecar = Assert-PostGateSharedInputRevalidation $governance $gate
  $buildClosure = Get-BuildSourceClosure
  Assert-Wiring $buildClosure
  $tests = @(Get-RegisteredTests)
  $focusedTests = @($tests | Where-Object { [string]$_ -ceq 'n23' })
  $integratedTests = @($tests | Where-Object { [string]$_ -cne 'n23' })
  Require ($focusedTests.Count -eq 1 -and
           $integratedTests.Count -eq ($tests.Count - 1)) `
      'focused/integrated registered-test classification is invalid'
  $inputs = @(Get-InputEntries $buildClosure)
  foreach ($entry in $inputs) {
    [void](Assert-NoN30ArtifactPath ([string]$entry.path) 'frozen manifest input')
  }
  $excludedN30Inputs = @($inputs | Where-Object {
      Test-N30ArtifactPath ([string]$_.path)
  })
  Require ($excludedN30Inputs.Count -eq 0) `
      'frozen input closure contains an excluded N30 artifact'
  $tools = Get-AllToolFacts
  $git = Get-GitFacts
  $prepared = [DateTimeOffset]::UtcNow.ToString('o')
  $nonce = [guid]::NewGuid().ToString('N')
  $manifest = [pscustomobject][ordered]@{
    format_version = 3
    node = 'N23'
    state = 'prepared-before-official-builds'
    prepared_utc = $prepared
    evidence_nonce = $nonce
    governance = $governance
    pre_corrective_gate = $gate
    post_gate_shared_input_revalidation = $sidecar
    expected_registered_tests = $tests.Count
    registered_test_names = $tests
    n23_focused_registered_tests = $focusedTests
    integrated_regression_registered_tests = $integratedTests
    build_source_closure = $buildClosure
    inputs = $inputs
    git = $git
    tools = $tools
    evidence_build_mutex = [pscustomobject][ordered]@{
      name = $EvidenceBuildMutexName
      acquired = $true
      abandoned_recovery = [bool]$script:EvidenceBuildMutexAbandoned
    }
    schema_version = 12
    git_binding_frozen = $true
    excluded_n30_input_count = $excludedN30Inputs.Count
    telemetry = [pscustomobject][ordered]@{
      production_data_access = 'not-collected'
      git_commit_or_push = 'not-collected'
      protected_configuration_changes = 'not-collected'
    }
  }
  Write-AtomicUtf8Text $PrebuildPath (($manifest | ConvertTo-Json -Depth 12) +
                                      [Environment]::NewLine) $EvidenceDir
  $hash = File-Hash $PrebuildPath $EvidenceDir
  $script:EvidenceInitialized = $true
  $script:PendingPreparedUtc = $prepared
  $script:PendingPrebuildHash = $hash
  $script:PendingEvidenceNonce = $nonce
  $script:PendingPostGateSidecarHash = [string]$sidecar.sha256
  Write-Pending $prepared $hash $nonce ([string]$sidecar.sha256)
  Write-Host "N23_PREPARED expected_registered_tests=$($tests.Count) manifest=$hash gate=$ExpectedGateHash sidecar=$($sidecar.sha256)"
}

function Read-Preparation {
  Assert-EvidenceBuildMutexHeld
  [void](Assert-PlainPathChain $PrebuildPath $EvidenceDir `
      'prebuild manifest' 'File')
  $manifest = Read-Json $PrebuildPath $EvidenceDir
  Require-ExactProperties $manifest @(
    'format_version', 'node', 'state', 'prepared_utc', 'evidence_nonce',
    'governance', 'pre_corrective_gate', 'expected_registered_tests',
    'post_gate_shared_input_revalidation',
    'registered_test_names', 'n23_focused_registered_tests',
    'integrated_regression_registered_tests', 'build_source_closure',
    'inputs', 'git', 'tools', 'evidence_build_mutex', 'schema_version', 'git_binding_frozen',
    'excluded_n30_input_count', 'telemetry'
  ) 'prebuild manifest'
  Require ([int]$manifest.format_version -eq 3 -and
           [string]$manifest.node -ceq 'N23') 'prebuild manifest identity is invalid'
  Require ([string]$manifest.state -ceq 'prepared-before-official-builds') 'prebuild manifest state is invalid'
  Require ([string]$manifest.evidence_nonce -cmatch '^[0-9a-f]{32}$') 'prebuild evidence nonce is invalid'
  Require-ExactProperties $manifest.evidence_build_mutex @(
    'name', 'acquired', 'abandoned_recovery'
  ) 'prebuild evidence/build mutex'
  Require ([string]$manifest.evidence_build_mutex.name -ceq $EvidenceBuildMutexName -and
           [bool]$manifest.evidence_build_mutex.acquired) `
      'prebuild evidence/build mutex binding is invalid'
  Require ([int]$manifest.schema_version -eq 12) 'prebuild schema contract is not v12'
  Require ([bool]$manifest.git_binding_frozen -and
           [int]$manifest.excluded_n30_input_count -eq 0) `
      'prebuild governance flags are invalid'
  Require-ExactProperties $manifest.telemetry @(
    'production_data_access', 'git_commit_or_push',
    'protected_configuration_changes'
  ) 'prebuild telemetry'
  foreach ($property in @('production_data_access', 'git_commit_or_push',
                           'protected_configuration_changes')) {
    Require ([string]$manifest.telemetry.$property -ceq 'not-collected') `
        'prebuild telemetry makes an unmeasured claim'
  }
  Require ([int]$manifest.expected_registered_tests -ge
           $MinimumRegisteredTestCount) 'prebuild registered count is too small'
  Require (@($manifest.n23_focused_registered_tests).Count -eq 1 -and
           [string]$manifest.n23_focused_registered_tests[0] -ceq 'n23') `
      'prebuild N23 focused test classification is invalid'
  Require (@($manifest.integrated_regression_registered_tests).Count -eq
           ([int]$manifest.expected_registered_tests - 1)) `
      'prebuild integrated-regression classification is invalid'
  $prebuildHash = File-Hash $PrebuildPath $EvidenceDir
  $pending = Read-PendingManifest $ManifestPath $PrebuildPath `
      ([string]$manifest.prepared_utc) ([string]$manifest.evidence_nonce) `
      ([string]$manifest.post_gate_shared_input_revalidation.sha256) `
      $EvidenceDir

  $governance = Assert-Governance
  $gate = Assert-Gate
  $sidecar = Assert-PostGateSharedInputRevalidation $governance $gate
  Require (($governance | ConvertTo-Json -Depth 8 -Compress) -ceq
           ($manifest.governance | ConvertTo-Json -Depth 8 -Compress)) `
      'governance changed after preparation'
  Require ([string]$gate.sha256 -ceq
           [string]$manifest.pre_corrective_gate.sha256) `
      'pre-corrective gate changed after preparation'
  Require (($sidecar | ConvertTo-Json -Depth 8 -Compress) -ceq
           ($manifest.post_gate_shared_input_revalidation | ConvertTo-Json -Depth 8 -Compress)) `
      'post-gate shared-input sidecar changed after preparation'
  $buildClosure = Get-BuildSourceClosure
  Assert-Wiring $buildClosure
  Require (($buildClosure | ConvertTo-Json -Depth 8 -Compress) -ceq
           ($manifest.build_source_closure | ConvertTo-Json -Depth 8 -Compress)) `
      'actual build-script source closure changed after preparation'
  $tests = @(Get-RegisteredTests)
  Require ($tests.Count -eq [int]$manifest.expected_registered_tests) 'registered count changed after preparation'
  Require (($tests -join "`n") -ceq
           (@($manifest.registered_test_names) -join "`n")) 'registered test order changed after preparation'
  $current = @(Get-InputEntries $buildClosure)
  Assert-FrozenInputEntries $current @($manifest.inputs) 'preparation'
  $tools = Get-AllToolFacts
  Assert-ToolFactsBinding $tools $manifest.tools
  $git = Get-GitFacts
  Assert-GitFactsBinding $git $manifest.git
  $preparedUtc = [DateTimeOffset]::Parse([string]$manifest.prepared_utc)
  Require ($preparedUtc.Offset -eq [TimeSpan]::Zero) 'preparation time is not UTC'

  $script:EvidenceInitialized = $true
  $script:PendingPreparedUtc = [string]$manifest.prepared_utc
  $script:PendingPrebuildHash = $prebuildHash
  $script:PendingEvidenceNonce = [string]$manifest.evidence_nonce
  $script:PendingPostGateSidecarHash = [string]$sidecar.sha256
  [pscustomobject][ordered]@{
    manifest = $manifest
    pending = $pending
    pending_manifest_sha256 = File-Hash $ManifestPath $EvidenceDir
    governance = $governance
    gate = $gate
    post_gate_sidecar = $sidecar
    tests = $tests
    inputs = $current
    build_source_closure = $buildClosure
    git = $git
    tools = $tools
    prepared_utc = $preparedUtc
    prebuild_sha256 = $prebuildHash
    evidence_nonce = [string]$manifest.evidence_nonce
  }
}

function New-Sandbox([string]$Prefix) {
  Require ($Prefix -cmatch '^n23-[a-z0-9-]+-$') 'sandbox prefix is not allowlisted'
  [void](Assert-PlainPathChain $Root $Root 'workspace root' 'Directory')
  $build = Join-Path $Root 'build'
  [void](Ensure-PlainDirectory $build $Root 'build directory')
  $parent = Join-Path $Root 'build\cl'
  [void](Ensure-PlainDirectory $parent $Root 'build/cl directory')
  $path = Join-Path $parent ($Prefix + [guid]::NewGuid().ToString('N'))
  [void](Assert-SandboxPath $path $Prefix)
  try {
    [void](Assert-PlainPathChain $path $Root 'sandbox root' 'Missing' $true)
    New-Item -ItemType Directory -Path $path -ErrorAction Stop | Out-Null
    [void](Assert-PlainPathChain $path $Root 'sandbox root' 'Directory')
    $properties = [ordered]@{ root = $path; prefix = $Prefix }
    foreach ($name in @('localappdata', 'temp', 'appdata', 'userprofile')) {
      $child = Join-Path $path $name
      [void](Ensure-PlainDirectory $child $path "sandbox $name")
      $properties[$name] = $child
    }
    [pscustomobject]$properties
  } catch {
    $failure = $_
    if ($null -ne (Get-LiteralItemOrNull $path)) {
      Remove-SafeTree $path $Root 'incomplete sandbox'
    }
    throw $failure
  }
}

function Assert-SandboxPath([string]$Path, [string]$Prefix) {
  Require ($Prefix -cmatch '^n23-[a-z0-9-]+-$') 'sandbox prefix is not allowlisted'
  $full = Assert-ConfinedPath $Path $Root 'sandbox path' $false
  $parent = Get-NormalizedFullPath (Join-Path $Root 'build\cl') 'sandbox parent'
  $actualParent = Get-NormalizedFullPath (Split-Path -Parent $full) `
      'sandbox actual parent'
  Require ($actualParent.Equals($parent, [StringComparison]::OrdinalIgnoreCase)) `
      'sandbox is not a direct child of build/cl'
  $expectedName = '^' + [regex]::Escape($Prefix) + '[0-9a-f]{32}$'
  Require ([IO.Path]::GetFileName($full) -cmatch $expectedName) `
      'sandbox name is not the exact prefix plus lowercase GUID form'
  $full
}

function Remove-Sandbox([object]$Sandbox) {
  if ($null -eq $Sandbox -or
      [string]::IsNullOrWhiteSpace([string]$Sandbox.root)) { return }
  $full = Assert-SandboxPath ([string]$Sandbox.root) ([string]$Sandbox.prefix)
  Remove-SafeTree $full $Root 'sandbox'
}

function Remove-Sandboxes([object[]]$Items) {
  $failures = New-Object System.Collections.Generic.List[string]
  foreach ($item in @($Items)) {
    if ($null -eq $item -or [string]::IsNullOrWhiteSpace([string]$item.root)) { continue }
    try {
      Remove-Sandbox $item
    } catch {
      $failures.Add($_.Exception.Message)
    }
  }
  if ($failures.Count -gt 0) {
    throw "N23 evidence requirement failed: sandbox cleanup failed for $($failures.Count) isolated roots"
  }
}

function Initialize-N23ProcessRunner {
  if ($null -ne ('QbrainN23BoundedProcessRunnerV2' -as [type])) { return }
  $source = @'
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

public sealed class QbrainN23ProcessResultV2
{
    public byte[] StdoutBytes;
    public byte[] StderrBytes;
    public int ExitCode;
    public bool TimedOut;
    public bool OutputLimitExceeded;
    public bool JobAssigned;
    public bool TreeTerminationAttempted;
    public bool TreeTerminationSucceeded;
}

public static class QbrainN23BoundedProcessRunnerV2
{
    private const UInt32 JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000;
    private const Int32 JobObjectExtendedLimitInformation = 9;

    [StructLayout(LayoutKind.Sequential)]
    private struct JOBOBJECT_BASIC_LIMIT_INFORMATION
    {
        public Int64 PerProcessUserTimeLimit;
        public Int64 PerJobUserTimeLimit;
        public UInt32 LimitFlags;
        public UIntPtr MinimumWorkingSetSize;
        public UIntPtr MaximumWorkingSetSize;
        public UInt32 ActiveProcessLimit;
        public IntPtr Affinity;
        public UInt32 PriorityClass;
        public UInt32 SchedulingClass;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct IO_COUNTERS
    {
        public UInt64 ReadOperationCount;
        public UInt64 WriteOperationCount;
        public UInt64 OtherOperationCount;
        public UInt64 ReadTransferCount;
        public UInt64 WriteTransferCount;
        public UInt64 OtherTransferCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION
    {
        public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
        public IO_COUNTERS IoInfo;
        public UIntPtr ProcessMemoryLimit;
        public UIntPtr JobMemoryLimit;
        public UIntPtr PeakProcessMemoryUsed;
        public UIntPtr PeakJobMemoryUsed;
    }

    private sealed class PumpState
    {
        public volatile bool LimitExceeded;
        public Exception Failure;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateJobObject(IntPtr securityAttributes, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetInformationJobObject(
        IntPtr job, Int32 infoClass, IntPtr info, UInt32 length);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool TerminateJobObject(IntPtr job, UInt32 exitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    private static IntPtr CreateKillOnCloseJob()
    {
        IntPtr job = CreateJobObject(IntPtr.Zero, null);
        if (job == IntPtr.Zero)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateJobObject failed");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits =
            new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        Int32 length = Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
        IntPtr buffer = Marshal.AllocHGlobal(length);
        try
        {
            Marshal.StructureToPtr(limits, buffer, false);
            if (!SetInformationJobObject(
                    job, JobObjectExtendedLimitInformation, buffer, (UInt32)length))
            {
                Int32 error = Marshal.GetLastWin32Error();
                CloseHandle(job);
                throw new Win32Exception(error, "SetInformationJobObject failed");
            }
            return job;
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    private static void Pump(Stream input, MemoryStream output, Int32 maximumBytes,
                             PumpState state)
    {
        try
        {
            byte[] buffer = new byte[8192];
            while (true)
            {
                Int32 read = input.Read(buffer, 0, buffer.Length);
                if (read == 0) return;
                if (output.Length + read > maximumBytes)
                {
                    state.LimitExceeded = true;
                    return;
                }
                output.Write(buffer, 0, read);
            }
        }
        catch (Exception ex)
        {
            state.Failure = ex;
        }
    }

    public static QbrainN23ProcessResultV2 Run(
        ProcessStartInfo startInfo, Int32 timeoutMilliseconds, Int32 maximumStreamBytes)
    {
        if (timeoutMilliseconds < 1) throw new ArgumentOutOfRangeException("timeoutMilliseconds");
        if (maximumStreamBytes < 1) throw new ArgumentOutOfRangeException("maximumStreamBytes");
        Process process = new Process();
        process.StartInfo = startInfo;
        IntPtr job = IntPtr.Zero;
        bool started = false;
        MemoryStream stdout = new MemoryStream();
        MemoryStream stderr = new MemoryStream();
        PumpState stdoutState = new PumpState();
        PumpState stderrState = new PumpState();
        Task stdoutTask = null;
        Task stderrTask = null;
        QbrainN23ProcessResultV2 result = new QbrainN23ProcessResultV2();
        try
        {
            if (!process.Start()) throw new InvalidOperationException("process did not start");
            started = true;
            job = CreateKillOnCloseJob();
            if (!AssignProcessToJobObject(job, process.Handle))
            {
                Int32 error = Marshal.GetLastWin32Error();
                try { process.Kill(); } catch { }
                try { process.WaitForExit(10000); } catch { }
                throw new Win32Exception(error, "AssignProcessToJobObject failed");
            }
            result.JobAssigned = true;
            stdoutTask = Task.Factory.StartNew(
                delegate { Pump(process.StandardOutput.BaseStream, stdout,
                                 maximumStreamBytes, stdoutState); },
                TaskCreationOptions.LongRunning);
            stderrTask = Task.Factory.StartNew(
                delegate { Pump(process.StandardError.BaseStream, stderr,
                                 maximumStreamBytes, stderrState); },
                TaskCreationOptions.LongRunning);
            Stopwatch elapsed = Stopwatch.StartNew();
            bool exited = false;
            while (!(exited = process.WaitForExit(50)))
            {
                if (stdoutState.LimitExceeded || stderrState.LimitExceeded) break;
                if (stdoutState.Failure != null || stderrState.Failure != null) break;
                if (elapsed.ElapsedMilliseconds >= timeoutMilliseconds)
                {
                    result.TimedOut = true;
                    break;
                }
            }
            result.OutputLimitExceeded =
                stdoutState.LimitExceeded || stderrState.LimitExceeded;
            if (!exited || result.OutputLimitExceeded || stdoutState.Failure != null ||
                stderrState.Failure != null)
            {
                result.TreeTerminationAttempted = true;
                result.TreeTerminationSucceeded = TerminateJobObject(job, 124);
                try { process.WaitForExit(10000); } catch { }
            }
            CloseHandle(job);
            job = IntPtr.Zero;
            if (stdoutTask != null && stderrTask != null &&
                !Task.WaitAll(new Task[] { stdoutTask, stderrTask }, 10000))
                throw new TimeoutException("redirected streams did not close after process termination");
            if (stdoutState.Failure != null) throw stdoutState.Failure;
            if (stderrState.Failure != null) throw stderrState.Failure;
            // A fast-exiting child may overflow while the asynchronous pumps
            // are draining after Process.WaitForExit reports completion.
            result.OutputLimitExceeded =
                result.OutputLimitExceeded || stdoutState.LimitExceeded || stderrState.LimitExceeded;
            if (!process.HasExited)
                throw new TimeoutException("process remained alive after tree termination");
            result.ExitCode = process.ExitCode;
            result.StdoutBytes = stdout.ToArray();
            result.StderrBytes = stderr.ToArray();
            return result;
        }
        finally
        {
            if (job != IntPtr.Zero)
            {
                try { TerminateJobObject(job, 125); } catch { }
                CloseHandle(job);
            }
            if (started && !process.HasExited)
            {
                try { process.Kill(); } catch { }
                try { process.WaitForExit(5000); } catch { }
            }
            process.Dispose();
            stdout.Dispose();
            stderr.Dispose();
        }
    }
}
'@
  Add-Type -TypeDefinition $source -Language CSharp -ErrorAction Stop
}

function ConvertTo-WindowsArgument([string]$Value) {
  if ($null -eq $Value) { $Value = '' }
  if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
  $builder = New-Object Text.StringBuilder
  [void]$builder.Append('"')
  $backslashes = 0
  foreach ($character in $Value.ToCharArray()) {
    if ($character -eq '\') {
      ++$backslashes
      continue
    }
    if ($character -eq '"') {
      [void]$builder.Append(('\' * ($backslashes * 2 + 1)))
      [void]$builder.Append('"')
    } else {
      if ($backslashes -gt 0) { [void]$builder.Append(('\' * $backslashes)) }
      [void]$builder.Append($character)
    }
    $backslashes = 0
  }
  if ($backslashes -gt 0) { [void]$builder.Append(('\' * ($backslashes * 2))) }
  [void]$builder.Append('"')
  $builder.ToString()
}

function Join-WindowsArguments([string[]]$Arguments) {
  (@($Arguments | ForEach-Object { ConvertTo-WindowsArgument ([string]$_) }) -join ' ')
}

function Convert-CapturedBytesToText([byte[]]$Bytes, [string]$Label) {
  try {
    $StrictUtf8.GetString($Bytes)
  } catch {
    throw "N23 evidence requirement failed: $Label is not valid UTF-8"
  }
}

function Convert-CapturedTextToLines([string]$Text) {
  if ($Text.Length -eq 0) { return @() }
  $lines = @([regex]::Split($Text, '\r\n|\n|\r'))
  if ($lines.Count -gt 0 -and $lines[$lines.Count - 1] -ceq '') {
    if ($lines.Count -eq 1) { return @() }
    $lines = @($lines[0..($lines.Count - 2)])
  }
  $lines
}

function Invoke-BoundedProcess(
    [string]$File, [string[]]$Arguments, [object]$Sandbox,
    [int]$TimeoutSeconds, [int]$MaximumStreamBytes = $MaxCapturedStreamBytes) {
  Require ($TimeoutSeconds -ge 1 -and
           $TimeoutSeconds -le $MaximumProcessTimeoutSeconds) `
      'process timeout is invalid'
  Require ($MaximumStreamBytes -ge 1 -and
           $MaximumStreamBytes -le $MaxCapturedStreamBytes) `
      'process stream limit is invalid'
  $filePath = Get-NormalizedFullPath $File 'child executable'
  $fileParent = Split-Path -Parent $filePath
  [void](Assert-PlainPathChain $filePath $fileParent 'child executable' 'File')
  [void](Assert-SandboxPath ([string]$Sandbox.root) ([string]$Sandbox.prefix))
  $tools = Get-BaseToolFacts
  $environment = New-ClosedChildEnvironment $Sandbox $tools
  $info = New-Object System.Diagnostics.ProcessStartInfo
  $info.FileName = $filePath
  $info.Arguments = Join-WindowsArguments $Arguments
  $info.WorkingDirectory = Assert-PlainPathChain $Root $Root `
      'child working directory' 'Directory'
  $info.UseShellExecute = $false
  $info.CreateNoWindow = $true
  $info.RedirectStandardOutput = $true
  $info.RedirectStandardError = $true
  Set-ClosedProcessEnvironment $info $environment
  Initialize-N23ProcessRunner
  $started = [DateTimeOffset]::UtcNow
  $result = [QbrainN23BoundedProcessRunnerV2]::Run(
      $info, ($TimeoutSeconds * 1000), $MaximumStreamBytes)
  $ended = [DateTimeOffset]::UtcNow
  Require ([bool]$result.JobAssigned) 'child process was not assigned to its kill-on-close job'
  if ([bool]$result.TimedOut) {
    Require ([bool]$result.TreeTerminationAttempted -and
             [bool]$result.TreeTerminationSucceeded) `
        'timed-out child process tree termination failed'
    throw "N23 evidence requirement failed: child process timed out after $TimeoutSeconds seconds"
  }
  if ([bool]$result.OutputLimitExceeded) {
    Require ([bool]$result.TreeTerminationAttempted -and
             [bool]$result.TreeTerminationSucceeded) `
        'over-limit child process tree termination failed'
    throw 'N23 evidence requirement failed: child process exceeded the per-stream byte limit'
  }
  $stdout = Convert-CapturedBytesToText $result.StdoutBytes 'child stdout'
  $stderr = Convert-CapturedBytesToText $result.StderrBytes 'child stderr'
  [pscustomobject][ordered]@{
    started = $started
    ended = $ended
    exit_code = [int]$result.ExitCode
    stdout = $stdout
    stderr = $stderr
    stdout_bytes = [int64]$result.StdoutBytes.Length
    stderr_bytes = [int64]$result.StderrBytes.Length
    stdout_sha256 = Text-Hash $stdout
    stderr_sha256 = Text-Hash $stderr
    environment_sha256 = Get-EnvironmentFingerprint $environment
    environment_names = @($environment.Keys | ForEach-Object { [string]$_ } |
        Sort-Object)
    child_environment_closed = $true
    process_job_assigned = $true
    timeout_seconds = $TimeoutSeconds
    per_stream_byte_limit = $MaximumStreamBytes
    production_data_access_telemetry = 'not-collected'
    git_mutation_telemetry = 'not-collected'
    protected_configuration_telemetry = 'not-collected'
  }
}

function Invoke-Captured([string]$File, [string[]]$Arguments,
                         [object]$Sandbox, [int]$TimeoutSeconds,
                         [int]$MaximumStreamBytes = $MaxCapturedStreamBytes) {
  $capture = Invoke-BoundedProcess $File $Arguments $Sandbox $TimeoutSeconds `
      $MaximumStreamBytes
  $stdoutLines = @(Convert-CapturedTextToLines $capture.stdout)
  $stderrLines = @(Convert-CapturedTextToLines $capture.stderr)
  $capture | Add-Member -NotePropertyName stdout_lines -NotePropertyValue $stdoutLines
  $capture | Add-Member -NotePropertyName stderr_lines -NotePropertyValue $stderrLines
  $capture | Add-Member -NotePropertyName lines -NotePropertyValue `
      @($stdoutLines + $stderrLines)
  $capture
}

function Invoke-SeparateCapture([string]$File, [string[]]$Arguments,
                                [object]$Sandbox, [int]$TimeoutSeconds,
                                [int]$MaximumStreamBytes = $MaxCapturedStreamBytes) {
  Invoke-BoundedProcess $File $Arguments $Sandbox $TimeoutSeconds `
      $MaximumStreamBytes
}

function Get-ExpectedLogPath([string]$Stage) {
  switch ($Stage) {
    'production-build' { return [IO.Path]::GetFullPath($DefaultProductionLog) }
    'test-build-suite-run-1' { return [IO.Path]::GetFullPath($DefaultTestLog) }
    'full-suite-run-2' { return [IO.Path]::GetFullPath($DefaultSecondLog) }
    default { throw "N23 evidence requirement failed: unknown native log stage" }
  }
}

function Get-StageMarker([string]$Stage) {
  switch ($Stage) {
    'production-build' { return 'N23_PRODUCTION_BUILD' }
    'test-build-suite-run-1' { return 'N23_TEST_BUILD_SUITE_RUN_1' }
    'full-suite-run-2' { return 'N23_FULL_SUITE_RUN_2' }
    default { throw "N23 evidence requirement failed: unknown native stage" }
  }
}

function Write-Capture([string]$Path, [string]$Stage, [string]$Command,
                       [object]$Capture, [System.Collections.IDictionary]$Metadata,
                       [object]$Preparation) {
  $actual = Assert-ConfinedPath $Path $script:ActiveLogRoot `
      'native capture path' $false
  Require ($actual.Equals((Get-ExpectedLogPath $Stage),
                          [StringComparison]::OrdinalIgnoreCase)) 'capture path is not the fixed path for its stage'
  $output = @($Capture.lines | ForEach-Object { [string]$_ })
  Require ($output.Count -le $MaxOutputLineCount) `
      "$Stage capture exceeds its line-count limit"
  Require ([bool]$Capture.child_environment_closed -and
           [bool]$Capture.process_job_assigned -and
           [string]$Capture.production_data_access_telemetry -ceq 'not-collected' -and
           [string]$Capture.git_mutation_telemetry -ceq 'not-collected' -and
           [string]$Capture.protected_configuration_telemetry -ceq 'not-collected') `
      "$Stage child isolation proof is invalid"
  Require ($script:VerificationRunNonce -cmatch '^[0-9a-f]{32}$') `
      'verification-run nonce is not initialized'
  $headers = [ordered]@{
    capture_format = 'n23-native-log-v3'
    node = 'N23'
    stage = $Stage
    stage_marker = Get-StageMarker $Stage
    command = $Command
    working_directory = $Root.Replace('\', '/')
    child_environment_policy = $ChildEnvironmentPolicy
    prebuild_manifest_sha256 = [string]$Preparation.prebuild_sha256
    evidence_nonce = [string]$Preparation.evidence_nonce
    verification_run_nonce = [string]$script:VerificationRunNonce
    started_utc = $Capture.started.ToString('o')
    ended_utc = $Capture.ended.ToString('o')
    exit_code = [string][int]$Capture.exit_code
    output_line_count = [string]$output.Count
    output_sha256 = Text-Hash ($output -join "`n")
    stdout_bytes = [string][int64]$Capture.stdout_bytes
    stderr_bytes = [string][int64]$Capture.stderr_bytes
    stdout_sha256 = [string]$Capture.stdout_sha256
    stderr_sha256 = [string]$Capture.stderr_sha256
    child_environment_sha256 = [string]$Capture.environment_sha256
    child_environment_names_sha256 = Text-Hash `
        (@($Capture.environment_names) -join "`n")
    child_environment_closed = 'true'
    process_job_assigned = 'true'
    timeout_seconds = [string][int]$Capture.timeout_seconds
    per_stream_byte_limit = [string][int]$Capture.per_stream_byte_limit
    production_data_access_telemetry = 'not-collected'
    git_mutation_telemetry = 'not-collected'
    protected_configuration_telemetry = 'not-collected'
  }
  foreach ($keyObject in $Metadata.Keys) {
    $key = [string]$keyObject
    Require ($key -cmatch '^[a-z][a-z0-9_]*$') 'capture metadata key is invalid'
    Require (-not $headers.Contains($key)) 'capture metadata duplicates a reserved header'
    $value = [string]$Metadata[$keyObject]
    Require ($value -notmatch '[\r\n]') 'capture metadata value contains a newline'
    $headers[$key] = $value
  }
  $lines = New-Object System.Collections.Generic.List[string]
  foreach ($key in $headers.Keys) { $lines.Add("$key=$($headers[$key])") }
  $lines.Add('output_begin=N23_CAPTURE_OUTPUT_BEGIN')
  foreach ($line in $output) { $lines.Add($line) }
  $lines.Add('output_end=N23_CAPTURE_OUTPUT_END')
  $text = $lines.ToArray() -join [Environment]::NewLine
  Assert-SafeText $text "native $Stage capture"
  Write-AtomicUtf8Lines $actual $lines.ToArray() $script:ActiveLogRoot `
      $MaxImportedLogBytes
}

function Parse-Log([string]$Path) {
  $full = Assert-ConfinedPath $Path $script:ActiveLogRoot `
      'native evidence log' $false
  [void](Get-SafeFileItem $full $script:ActiveLogRoot $MaxImportedLogBytes `
      'native evidence log')
  $lines = @(Read-BoundedUtf8Lines $full $script:ActiveLogRoot `
      $MaxImportedLogBytes `
      'native evidence log')
  $beginIndexes = New-Object System.Collections.Generic.List[int]
  $endIndexes = New-Object System.Collections.Generic.List[int]
  for ($index = 0; $index -lt $lines.Count; ++$index) {
    if ([string]$lines[$index] -ceq 'output_begin=N23_CAPTURE_OUTPUT_BEGIN') {
      $beginIndexes.Add($index)
    }
    if ([string]$lines[$index] -ceq 'output_end=N23_CAPTURE_OUTPUT_END') {
      $endIndexes.Add($index)
    }
  }
  Require ($beginIndexes.Count -eq 1 -and $endIndexes.Count -eq 1) 'native log output boundaries are not exact'
  $begin = $beginIndexes[0]
  $end = $endIndexes[0]
  Require ($begin -gt 0 -and $end -eq ($lines.Count - 1) -and
           $end -gt $begin) 'native log output boundary positions are invalid'
  $fields = @{}
  for ($index = 0; $index -lt $begin; ++$index) {
    $match = [regex]::Match([string]$lines[$index], '^([a-z][a-z0-9_]*)=(.*)$')
    Require ($match.Success) 'native log header line is malformed'
    $key = $match.Groups[1].Value
    Require (-not $fields.ContainsKey($key)) 'native log contains a duplicate header'
    $fields[$key] = $match.Groups[2].Value
  }
  $output = New-Object System.Collections.Generic.List[string]
  for ($index = $begin + 1; $index -lt $end; ++$index) {
    $output.Add([string]$lines[$index])
  }
  foreach ($field in @('started_utc', 'ended_utc', 'exit_code',
                        'output_line_count', 'output_sha256', 'stdout_bytes',
                        'stderr_bytes', 'stdout_sha256', 'stderr_sha256',
                        'verification_run_nonce')) {
    Require ($fields.ContainsKey($field)) 'native log lacks a required core header'
  }
  $started = [DateTimeOffset]::Parse([string]$fields.started_utc)
  $ended = [DateTimeOffset]::Parse([string]$fields.ended_utc)
  Require ($started.Offset -eq [TimeSpan]::Zero -and
           $ended.Offset -eq [TimeSpan]::Zero) 'native log timestamps are not UTC'
  Require ($started -le $ended) 'native log timestamps are reversed'
  Require ([string]$fields.started_utc -ceq $started.ToString('o') -and
           [string]$fields.ended_utc -ceq $ended.ToString('o')) 'native log timestamps are not canonical'
  Require ([string]$fields.exit_code -cmatch '^-?[0-9]+$') 'native log exit code is malformed'
  Require ([string]$fields.output_line_count -cmatch '^(?:0|[1-9][0-9]*)$') 'native log output count is malformed'
  Require ([int]$fields.output_line_count -eq $output.Count) 'native log output count differs from framing'
  Assert-Sha256 ([string]$fields.output_sha256) 'native log output hash'
  Assert-Sha256 ([string]$fields.stdout_sha256) 'native log stdout hash'
  Assert-Sha256 ([string]$fields.stderr_sha256) 'native log stderr hash'
  Require ([string]$fields.stdout_bytes -cmatch '^(?:0|[1-9][0-9]*)$' -and
           [string]$fields.stderr_bytes -cmatch '^(?:0|[1-9][0-9]*)$') `
      'native log stream byte counts are malformed'
  Require ([int64]$fields.stdout_bytes -le $MaxCapturedStreamBytes -and
           [int64]$fields.stderr_bytes -le $MaxCapturedStreamBytes) `
      'native log records an over-limit stream'
  Require ((Text-Hash ($output.ToArray() -join "`n")) -ceq
           [string]$fields.output_sha256) 'native log output hash is invalid'
  Assert-SafeText ($lines -join "`n") 'native evidence log'
  [pscustomobject][ordered]@{
    path = $full
    lines = $lines
    output = $output.ToArray()
    fields = $fields
    started = $started
    ended = $ended
    exit_code = [int]$fields.exit_code
  }
}

function Assert-HeaderFields([object]$Log, [string[]]$Expected) {
  $actual = @($Log.fields.Keys | Sort-Object)
  $wanted = @($Expected | Sort-Object)
  Require (($actual -join "`n") -ceq ($wanted -join "`n")) 'native log headers are not exact for the stage'
}

function Get-CompilerBanner([string[]]$Lines, [string]$Label) {
  $banners = @($Lines | Where-Object {
      [string]$_ -cmatch '^\*\* Visual Studio [0-9]+ Developer Command Prompt v[0-9]+(?:[.][0-9]+)+$'
    })
  Require ($banners.Count -eq 1) "$Label lacks exactly one nonempty Visual Studio compiler banner"
  $arch = @($Lines | Where-Object {
      [string]$_ -ceq "[vcvarsall.bat] Environment initialized for: 'x64'"
    })
  Require ($arch.Count -eq 1) "$Label lacks exactly one x64 vcvars marker"
  [string]$banners[0]
}

function Assert-StageArtifactTimestamp([string]$Value, [object]$Log,
                                       [string]$Label) {
  $timestamp = [DateTimeOffset]::Parse($Value)
  Require ($timestamp.Offset -eq [TimeSpan]::Zero -and
           $Value -ceq $timestamp.ToString('o')) "$Label is not canonical UTC"
  Require ($timestamp -ge $Log.started -and $timestamp -le $Log.ended) `
      "$Label is outside the native stage interval"
}

function Assert-NativeLog([object]$Log, [string]$Stage,
                          [object]$Preparation) {
  $common = @(
    'capture_format', 'node', 'stage', 'stage_marker', 'command',
    'working_directory', 'child_environment_policy',
    'prebuild_manifest_sha256', 'evidence_nonce', 'verification_run_nonce',
    'started_utc', 'ended_utc', 'exit_code', 'output_line_count',
    'output_sha256', 'stdout_bytes', 'stderr_bytes', 'stdout_sha256',
    'stderr_sha256', 'child_environment_sha256',
    'child_environment_names_sha256', 'child_environment_closed',
    'process_job_assigned', 'timeout_seconds', 'per_stream_byte_limit',
    'production_data_access_telemetry', 'git_mutation_telemetry',
    'protected_configuration_telemetry'
  )
  $stageFields = @(
    'target_arch', 'language_mode', 'toolchain', 'isolated_localappdata',
    'localappdata_sandbox_kind'
  )
  switch ($Stage) {
    'production-build' {
      $expectedPath = $DefaultProductionLog
      $expectedCommand = $ProductionCommand
      $expectedTimeout = $ProductionBuildTimeoutSeconds
      $stageFields += @(
        'artifact_sha256', 'artifact_preexisting', 'artifact_created_utc',
        'artifact_last_write_utc', 'compiler_banner_sha256',
        'launcher_executable_path', 'launcher_executable_sha256',
        'vcvarsall_path', 'vcvarsall_sha256', 'cl_path', 'cl_sha256',
        'link_path', 'link_sha256', 'build_batch_path', 'build_batch_sha256'
      )
    }
    'test-build-suite-run-1' {
      $expectedPath = $DefaultTestLog
      $expectedCommand = $TestBuildCommand
      $expectedTimeout = $TestBuildTimeoutSeconds
      $stageFields += @(
        'suite_run_index', 'expected_registered_tests',
        'n23_focused_registered_tests', 'integrated_regression_tests',
        'production_binary_sha256', 'test_binary_sha256',
        'test_binary_preexisting', 'test_binary_created_utc',
        'test_binary_last_write_utc',
        'compiler_banner_sha256', 'launcher_executable_path',
        'launcher_executable_sha256', 'vcvarsall_path', 'vcvarsall_sha256',
        'cl_path', 'cl_sha256', 'link_path', 'link_sha256',
        'build_batch_path', 'build_batch_sha256'
      )
    }
    'full-suite-run-2' {
      $expectedPath = $DefaultSecondLog
      $expectedCommand = $SecondSuiteCommand
      $expectedTimeout = $SuiteTimeoutSeconds
      $stageFields += @(
        'suite_run_index', 'expected_registered_tests',
        'n23_focused_registered_tests', 'integrated_regression_tests',
        'test_binary_sha256', 'launcher_executable_path',
        'launcher_executable_sha256'
      )
    }
    default { throw 'N23 evidence requirement failed: unknown log stage' }
  }
  Assert-HeaderFields $Log ($common + $stageFields)
  Require ([IO.Path]::GetFullPath($Log.path).Equals(
           [IO.Path]::GetFullPath($expectedPath),
           [StringComparison]::OrdinalIgnoreCase)) 'native log is not at its fixed stage path'
  Require ([string]$Log.fields.capture_format -ceq 'n23-native-log-v3') 'native log format is invalid'
  Require ([string]$Log.fields.node -ceq 'N23') 'native log node is invalid'
  Require ([string]$Log.fields.stage -ceq $Stage -and
           [string]$Log.fields.stage_marker -ceq
           (Get-StageMarker $Stage)) 'native log stage marker is invalid'
  Require ([string]$Log.fields.command -ceq $expectedCommand) 'native log command is not exact'
  Require ([string]$Log.fields.working_directory -ceq
           $Root.Replace('\', '/')) 'native log working directory is not exact'
  Require ([string]$Log.fields.child_environment_policy -ceq
           $ChildEnvironmentPolicy) 'native log child-environment policy is not exact'
  Require ([string]$Log.fields.prebuild_manifest_sha256 -ceq
           [string]$Preparation.prebuild_sha256) 'native log is not bound to the frozen prebuild manifest'
  Require ([string]$Log.fields.evidence_nonce -ceq
           [string]$Preparation.evidence_nonce) 'native log evidence nonce is invalid'
  Require ([string]$Log.fields.verification_run_nonce -ceq
           [string]$script:VerificationRunNonce) `
      'native log was not produced by the current Verify process'
  Require ($Log.exit_code -eq 0) 'native log exit code is nonzero'
  Require ([string]$Log.fields.target_arch -ceq 'x64') 'native log target architecture is not x64'
  Require ([string]$Log.fields.language_mode -ceq '/std:c++20') 'native log language mode is not C++20'
  Require ([string]$Log.fields.toolchain -ceq 'MSVC') 'native log toolchain is not MSVC'
  Require ([string]$Log.fields.isolated_localappdata -ceq 'true' -and
           [string]$Log.fields.production_data_access_telemetry -ceq 'not-collected') 'native log isolation facts are invalid'
  Require ([string]$Log.fields.localappdata_sandbox_kind -ceq
           ('n23-' + $Stage)) 'native log sandbox kind is invalid'
  Require ([string]$Log.fields.git_mutation_telemetry -ceq 'not-collected' -and
           [string]$Log.fields.protected_configuration_telemetry -ceq
               'not-collected') 'native log telemetry makes an unmeasured claim'
  Require ([string]$Log.fields.child_environment_closed -ceq 'true' -and
           [string]$Log.fields.process_job_assigned -ceq 'true') `
      'native log process isolation facts are invalid'
  foreach ($field in @('child_environment_sha256',
                         'child_environment_names_sha256')) {
    Assert-Sha256 ([string]$Log.fields.$field) "native log $field"
  }
  Require ([string]$Log.fields.timeout_seconds -ceq [string]$expectedTimeout) `
      'native log timeout is not the stage timeout'
  Require ([string]$Log.fields.per_stream_byte_limit -ceq
           [string]$MaxCapturedStreamBytes) `
      'native log stream limit is not 32 MiB'
  if ($Stage -in @('production-build', 'test-build-suite-run-1')) {
    foreach ($binding in @(
        @('vcvarsall_path', 'vcvarsall_sha256', $Preparation.tools.msvc.vcvarsall),
        @('cl_path', 'cl_sha256', $Preparation.tools.msvc.cl),
        @('link_path', 'link_sha256', $Preparation.tools.msvc.link))) {
      Require ([string]$Log.fields[[string]$binding[0]] -ceq
               [string]$binding[2].path -and
               [string]$Log.fields[[string]$binding[1]] -ceq
               [string]$binding[2].sha256) `
          "native log tool binding $($binding[0]) is invalid"
    }
    $batchStage = if ($Stage -eq 'production-build') { 'production' } else { 'test' }
    $batchFacts = @($Preparation.tools.build_batch_paths | Where-Object {
        [string]$_.stage -ceq $batchStage
      })
    Require ($batchFacts.Count -eq 1 -and
             [string]$Log.fields.build_batch_path -ceq
             [string]$batchFacts[0].path) `
        'native log build-batch path binding is invalid'
  }

  if ($Stage -eq 'production-build') {
    Require (@($Log.output | Where-Object { [string]$_ -ceq 'BUILD_OK' }).Count -eq 1) 'production log lacks exactly one BUILD_OK'
    Require (@($Log.output | Where-Object { [string]$_ -ceq 'TESTS_BUILD_OK' }).Count -eq 0) 'production log contains a test-build marker'
    Require (@($Log.output | Where-Object { [string]$_ -match '^\[(?:PASS|FAIL)\]' }).Count -eq 0) 'production log contains test results'
    $banner = Get-CompilerBanner $Log.output 'production build'
    Assert-Sha256 ([string]$Log.fields.artifact_sha256) 'production artifact hash'
    Require ([string]$Log.fields.artifact_preexisting -ceq 'false') `
        'production output was not absent before its build stage'
    Assert-StageArtifactTimestamp ([string]$Log.fields.artifact_created_utc) $Log `
        'production artifact creation time'
    Assert-StageArtifactTimestamp ([string]$Log.fields.artifact_last_write_utc) $Log `
        'production artifact write time'
    Assert-Sha256 ([string]$Log.fields.compiler_banner_sha256) 'production compiler banner hash'
    Require ((Text-Hash $banner) -ceq [string]$Log.fields.compiler_banner_sha256) 'production compiler banner hash is invalid'
    foreach ($field in @('launcher_executable_sha256', 'vcvarsall_sha256',
                          'cl_sha256', 'link_sha256', 'build_batch_sha256')) {
      Assert-Sha256 ([string]$Log.fields.$field) "production $field"
    }
    Require ([string]$Log.fields.launcher_executable_path -ceq
             [string]$Preparation.tools.windows_powershell.path -and
             [string]$Log.fields.launcher_executable_sha256 -ceq
             [string]$Preparation.tools.windows_powershell.sha256) `
        'production launcher binding is invalid'
    return $banner
  }
  if ($Stage -eq 'test-build-suite-run-1') {
    Require (@($Log.output | Where-Object { [string]$_ -ceq 'TESTS_BUILD_OK' }).Count -eq 1) 'test-build log lacks exactly one TESTS_BUILD_OK'
    Require (@($Log.output | Where-Object { [string]$_ -ceq 'BUILD_OK' }).Count -eq 0) 'test-build log contains a production-build marker'
    Require ([string]$Log.fields.suite_run_index -ceq '1') 'test-build suite index is not one'
    Require ([string]$Log.fields.expected_registered_tests -ceq
             [string]$Preparation.tests.Count) 'test-build expected count is not frozen'
    Require ([string]$Log.fields.n23_focused_registered_tests -ceq '1' -and
             [string]$Log.fields.integrated_regression_tests -ceq
             [string]($Preparation.tests.Count - 1)) `
        'test-build focused/integrated classification is invalid'
    Assert-Sha256 ([string]$Log.fields.production_binary_sha256) 'test-build production binary hash'
    Assert-Sha256 ([string]$Log.fields.test_binary_sha256) 'test-build test binary hash'
    Require ([string]$Log.fields.test_binary_preexisting -ceq 'false') `
        'test output was not absent before its build stage'
    Assert-StageArtifactTimestamp ([string]$Log.fields.test_binary_created_utc) $Log `
        'test artifact creation time'
    Assert-StageArtifactTimestamp ([string]$Log.fields.test_binary_last_write_utc) $Log `
        'test artifact write time'
    $banner = Get-CompilerBanner $Log.output 'test build'
    Assert-Sha256 ([string]$Log.fields.compiler_banner_sha256) 'test-build compiler banner hash'
    Require ((Text-Hash $banner) -ceq [string]$Log.fields.compiler_banner_sha256) 'test-build compiler banner hash is invalid'
    Require ([string]$Log.fields.launcher_executable_path -ceq
             [string]$Preparation.tools.windows_powershell.path -and
             [string]$Log.fields.launcher_executable_sha256 -ceq
             [string]$Preparation.tools.windows_powershell.sha256) `
        'test-build launcher binding is invalid'
    return $banner
  }
  Require (@($Log.output | Where-Object {
      [string]$_ -in @('BUILD_OK', 'TESTS_BUILD_OK')
    }).Count -eq 0) 'second suite log contains a build marker'
  Require ([string]$Log.fields.suite_run_index -ceq '2') 'second suite index is not two'
  Require ([string]$Log.fields.expected_registered_tests -ceq
           [string]$Preparation.tests.Count) 'second suite expected count is not frozen'
  Require ([string]$Log.fields.n23_focused_registered_tests -ceq '1' -and
           [string]$Log.fields.integrated_regression_tests -ceq
           [string]($Preparation.tests.Count - 1)) `
      'second suite focused/integrated classification is invalid'
  Assert-Sha256 ([string]$Log.fields.test_binary_sha256) 'second suite test binary hash'
  Require ([string]$Log.fields.launcher_executable_path -ceq
           (Relative-Path $Tests) -and
           [string]$Log.fields.launcher_executable_sha256 -ceq
           [string]$Log.fields.test_binary_sha256) `
      'second suite launcher binding is invalid'
  return ''
}

function Parse-SnapshotLine([string]$Line, [int]$ExpectedIndex,
                            [string]$Label) {
  $match = [regex]::Match(
      $Line,
      '^N23_SNAPSHOT ([1-9][0-9]*) ((?:[0-9a-f]{2})+) ([0-9a-f]{64}) ([0-9a-f]{64}) ([0-9a-f]{64}) ([0-9a-f]{64})$')
  Require ($match.Success) "$Label contains a malformed snapshot row"
  Require ([int]$match.Groups[1].Value -eq $ExpectedIndex) "$Label snapshot indexes are not contiguous"
  $decoded = Decode-HexUtf8 $match.Groups[2].Value "$Label snapshot label"
  Require ($match.Groups[3].Value -ceq $match.Groups[4].Value) "$Label selected snapshot changed"
  Require ($match.Groups[5].Value -ceq $match.Groups[6].Value) "$Label decoy snapshot changed"
  Require ($match.Groups[3].Value -cne $match.Groups[5].Value) "$Label selected and decoy snapshots are not distinct"
  [pscustomobject][ordered]@{
    index = $ExpectedIndex
    label = $decoded
    label_hex = $match.Groups[2].Value
    selected_sha256 = $match.Groups[3].Value
    decoy_sha256 = $match.Groups[5].Value
  }
}

function Get-TagRowKey([object]$Row, [string]$Label) {
  Require-ExactProperties $Row @('page_id', 'source_id', 'slug', 'tag') $Label
  Require ([int64]$Row.page_id -gt 0) "$Label page id is not positive"
  foreach ($name in @('source_id', 'slug', 'tag')) {
    $value = [string]$Row.$name
    Require ($value -cmatch '^[A-Za-z0-9_.:/-]+$') "$Label contains an unsafe textual value"
  }
  "$([int64]$Row.page_id)`t$([string]$Row.source_id)`t$([string]$Row.slug)`t$([string]$Row.tag)"
}

function Get-SequenceRowKey([object]$Row, [string]$Label) {
  Require-ExactProperties $Row @('name', 'seq') $Label
  Require ([string]$Row.name -cmatch '^[A-Za-z_][A-Za-z0-9_]*$') "$Label sequence name is invalid"
  Require ([int64]$Row.seq -ge 0) "$Label sequence value is negative"
  "$([string]$Row.name)`t$([int64]$Row.seq)"
}

function Parse-AllowedDeltaLine([string]$Line, [string]$Label) {
  $match = [regex]::Match($Line, '^N23_ALLOWED_DELTA_JSON=(\{.*\})$')
  Require ($match.Success) "$Label allowed-delta row is malformed"
  try {
    $delta = ConvertFrom-JsonText $match.Groups[1].Value
  } catch {
    throw "N23 evidence requirement failed: $Label allowed-delta JSON is invalid"
  }
  Require-ExactProperties $delta @(
    'format_version', 'selected_full', 'decoy_full', 'schema',
    'non_tag_tables', 'tags', 'sqlite_sequence', 'result'
  ) "$Label allowed delta"
  Require ([int]$delta.format_version -eq 1) "$Label allowed-delta version is invalid"

  foreach ($pair in @(
      @('selected_full', $delta.selected_full),
      @('decoy_full', $delta.decoy_full))) {
    Require-ExactProperties $pair[1] @(
      'before_sha256', 'after_sha256', 'before_bytes', 'after_bytes'
    ) "$Label $($pair[0])"
    Assert-Sha256 ([string]$pair[1].before_sha256) "$Label $($pair[0]) before hash"
    Assert-Sha256 ([string]$pair[1].after_sha256) "$Label $($pair[0]) after hash"
    Require ([int64]$pair[1].before_bytes -gt 0 -and
             [int64]$pair[1].after_bytes -gt 0) "$Label $($pair[0]) byte counts are not positive"
  }
  Require ([string]$delta.selected_full.before_sha256 -cne
           [string]$delta.selected_full.after_sha256) "$Label selected full snapshot did not change"
  Require ([string]$delta.decoy_full.before_sha256 -ceq
           [string]$delta.decoy_full.after_sha256) "$Label decoy full snapshot changed"
  Require ([int64]$delta.decoy_full.before_bytes -eq
           [int64]$delta.decoy_full.after_bytes) "$Label decoy snapshot byte count changed"
  Require ([string]$delta.selected_full.before_sha256 -cne
           [string]$delta.decoy_full.before_sha256) "$Label selected and decoy full snapshots are not distinct"

  Require-ExactProperties $delta.schema @(
    'before_sha256', 'after_sha256', 'objects'
  ) "$Label schema delta"
  Assert-Sha256 ([string]$delta.schema.before_sha256) "$Label schema before hash"
  Assert-Sha256 ([string]$delta.schema.after_sha256) "$Label schema after hash"
  Require ([string]$delta.schema.before_sha256 -ceq
           [string]$delta.schema.after_sha256) "$Label schema changed"
  $schemaObjects = @($delta.schema.objects)
  Require ($schemaObjects.Count -gt 0) "$Label schema object set is empty"
  $schemaKeys = New-Object System.Collections.Generic.List[string]
  $schemaTableNames = New-Object System.Collections.Generic.List[string]
  foreach ($object in $schemaObjects) {
    Require-ExactProperties $object @(
      'type', 'name', 'tbl_name', 'before_sql_sha256', 'after_sql_sha256'
    ) "$Label schema object"
    Require ([string]$object.type -in @('index', 'table', 'trigger', 'view')) "$Label schema object type is invalid"
    foreach ($name in @('name', 'tbl_name')) {
      $value = [string]$object.$name
      Require ($value.Length -gt 0 -and $value -notmatch '[\x00-\x1f\x7f]') "$Label schema object name is invalid"
    }
    Assert-Sha256 ([string]$object.before_sql_sha256) "$Label schema SQL before hash"
    Assert-Sha256 ([string]$object.after_sql_sha256) "$Label schema SQL after hash"
    Require ([string]$object.before_sql_sha256 -ceq
             [string]$object.after_sql_sha256) "$Label schema object DDL changed"
    $schemaKeys.Add("$([string]$object.type)`t$([string]$object.name)")
    if ([string]$object.type -ceq 'table') {
      $schemaTableNames.Add([string]$object.name)
    }
  }
  Require (($schemaKeys | Sort-Object -Unique).Count -eq
           $schemaKeys.Count) "$Label schema objects are not unique"

  $nonTag = @($delta.non_tag_tables)
  Require ($nonTag.Count -gt 0) "$Label non-tag table evidence is empty"
  $nonTagNames = New-Object System.Collections.Generic.List[string]
  foreach ($table in $nonTag) {
    Require-ExactProperties $table @(
      'name', 'before_sha256', 'after_sha256', 'before_rows', 'after_rows'
    ) "$Label non-tag table"
    Require ([string]$table.name -cmatch '^[A-Za-z_][A-Za-z0-9_]*$') "$Label non-tag table name is invalid"
    Require ([string]$table.name -cnotin @('tags', 'sqlite_sequence')) "$Label non-tag table set contains an excluded table"
    Assert-Sha256 ([string]$table.before_sha256) "$Label non-tag before hash"
    Assert-Sha256 ([string]$table.after_sha256) "$Label non-tag after hash"
    Require ([string]$table.before_sha256 -ceq
             [string]$table.after_sha256) "$Label non-tag table changed"
    Require ([int64]$table.before_rows -ge 0 -and
             [int64]$table.before_rows -eq
             [int64]$table.after_rows) "$Label non-tag row count changed"
    $nonTagNames.Add([string]$table.name)
  }
  Require (($nonTagNames | Sort-Object -Unique).Count -eq
           $nonTagNames.Count) "$Label non-tag table names are not unique"
  $expectedNonTagNames = @($schemaTableNames | Where-Object {
      [string]$_ -cnotin @('tags', 'sqlite_sequence')
    } | Sort-Object)
  Require ((@($nonTagNames | Sort-Object) -join "`n") -ceq
           ($expectedNonTagNames -join "`n")) "$Label non-tag table set does not cover the schema"

  Require-ExactProperties $delta.tags @(
    'before_rows', 'after_rows', 'added_rows', 'removed_rows',
    'before_sha256', 'after_sha256'
  ) "$Label tag delta"
  Assert-Sha256 ([string]$delta.tags.before_sha256) "$Label tags before hash"
  Assert-Sha256 ([string]$delta.tags.after_sha256) "$Label tags after hash"
  Require ([string]$delta.tags.before_sha256 -cne
           [string]$delta.tags.after_sha256) "$Label tags did not change"
  $tagBefore = @($delta.tags.before_rows)
  $tagAfter = @($delta.tags.after_rows)
  $tagAdded = @($delta.tags.added_rows)
  $tagRemoved = @($delta.tags.removed_rows)
  Require ($tagBefore.Count -eq 1 -and $tagAfter.Count -eq 3) "$Label complete tag row sets are not exact"
  $beforeKeys = @($tagBefore | ForEach-Object { Get-TagRowKey $_ "$Label tag before row" })
  $afterKeys = @($tagAfter | ForEach-Object { Get-TagRowKey $_ "$Label tag after row" })
  $addedKeys = @($tagAdded | ForEach-Object { Get-TagRowKey $_ "$Label emitted added tag row" })
  $removedKeys = @($tagRemoved | ForEach-Object { Get-TagRowKey $_ "$Label emitted removed tag row" })
  Require (($beforeKeys | Sort-Object -Unique).Count -eq $beforeKeys.Count -and
           ($afterKeys | Sort-Object -Unique).Count -eq $afterKeys.Count) "$Label tag rows are not unique"
  $computedAdded = @($afterKeys | Where-Object { $beforeKeys -cnotcontains $_ } | Sort-Object)
  $computedRemoved = @($beforeKeys | Where-Object { $afterKeys -cnotcontains $_ } | Sort-Object)
  Require (($computedAdded -join "`n") -ceq
           (@($addedKeys | Sort-Object) -join "`n")) "$Label emitted added tags do not match full row sets"
  Require (($computedRemoved -join "`n") -ceq
           (@($removedKeys | Sort-Object) -join "`n")) "$Label emitted removed tags do not match full row sets"
  Require ($addedKeys.Count -eq 2 -and $removedKeys.Count -eq 0) "$Label tag delta is not exactly two additions"
  $existing = $tagBefore[0]
  Require ([string]$existing.source_id -ceq 'default' -and
           [string]$existing.slug -ceq 'bf-existing' -and
           [string]$existing.tag -ceq 'chronicle') "$Label preexisting Chronicle tag is not exact"
  $addedSlugs = @($tagAdded | ForEach-Object {
      Require ([string]$_.source_id -ceq 'default' -and
               [string]$_.tag -ceq 'chronicle') "$Label added a non-Chronicle or non-default tag"
      [string]$_.slug
    } | Sort-Object)
  Require (($addedSlugs -join "`n") -ceq
           (@('bf-at', 'bf-new') -join "`n")) "$Label added Chronicle slugs are not exact"

  Require-ExactProperties $delta.sqlite_sequence @(
    'before_rows', 'after_rows', 'delta', 'before_sha256', 'after_sha256'
  ) "$Label sqlite_sequence delta"
  Assert-Sha256 ([string]$delta.sqlite_sequence.before_sha256) "$Label sqlite_sequence before hash"
  Assert-Sha256 ([string]$delta.sqlite_sequence.after_sha256) "$Label sqlite_sequence after hash"
  Require ([string]$delta.sqlite_sequence.before_sha256 -ceq
           [string]$delta.sqlite_sequence.after_sha256) "$Label sqlite_sequence hash changed"
  $sequenceBefore = @($delta.sqlite_sequence.before_rows)
  $sequenceAfter = @($delta.sqlite_sequence.after_rows)
  $sequenceDelta = @($delta.sqlite_sequence.delta)
  $sequenceBeforeKeys = @($sequenceBefore | ForEach-Object {
      Get-SequenceRowKey $_ "$Label sqlite_sequence before row"
    })
  $sequenceAfterKeys = @($sequenceAfter | ForEach-Object {
      Get-SequenceRowKey $_ "$Label sqlite_sequence after row"
    })
  Require (($sequenceBeforeKeys -join "`n") -ceq
           ($sequenceAfterKeys -join "`n")) "$Label sqlite_sequence rows changed"
  Require ($sequenceDelta.Count -eq 0) "$Label sqlite_sequence delta is not explicitly empty"

  Require-ExactProperties $delta.result @('tagged', 'already_tagged') "$Label result counts"
  Require ([int]$delta.result.tagged -eq 2 -and
           [int]$delta.result.already_tagged -eq 1) "$Label backfill result counts are not exact"
  $semantic = @(
    'format=1',
    'tagged=2',
    'already_tagged=1',
    'added=default/bf-at/chronicle,default/bf-new/chronicle',
    'removed=0',
    "schema_objects=$($schemaObjects.Count)",
    "non_tag_tables=$((@($nonTagNames | Sort-Object) -join ','))",
    "sqlite_sequence_rows=$($sequenceBeforeKeys.Count)",
    'selected_full_changed=true',
    'decoy_full_unchanged=true'
  ) -join "`n"
  [pscustomobject][ordered]@{
    value = $delta
    semantic_sha256 = Text-Hash $semantic
    tagged = 2
    already_tagged = 1
    added_slugs = $addedSlugs
    removed_count = 0
    schema_object_count = $schemaObjects.Count
    non_tag_table_count = $nonTag.Count
    sqlite_sequence_row_count = $sequenceBeforeKeys.Count
    selected_before_sha256 = [string]$delta.selected_full.before_sha256
    selected_after_sha256 = [string]$delta.selected_full.after_sha256
    decoy_sha256 = [string]$delta.decoy_full.before_sha256
  }
}

function Assert-TestRun([object]$Log, [string[]]$Expected,
                        [string]$Label) {
  $results = New-Object System.Collections.Generic.List[object]
  foreach ($line in $Log.output) {
    if ([string]$line -match '^\[PASS\]\s+([A-Za-z0-9_.-]+)\s*$') {
      $results.Add([pscustomobject][ordered]@{
        name = $Matches[1]
        result = 'PASS'
        line = [string]$line
      })
    } elseif ([string]$line -match '^\[FAIL\]\s+([A-Za-z0-9_.-]+)(?::.*)?$') {
      $results.Add([pscustomobject][ordered]@{
        name = $Matches[1]
        result = 'FAIL'
        line = [string]$line
      })
    }
  }
  Require ($Expected.Count -ge $MinimumRegisteredTestCount) `
      "$Label expected test closure is below the approved minimum"
  Require ($results.Count -eq $Expected.Count) "$Label result count is not exact"
  for ($index = 0; $index -lt $Expected.Count; ++$index) {
    Require ([string]$results[$index].name -ceq
             $Expected[$index]) "$Label result order changed"
    Require ([string]$results[$index].result -ceq 'PASS') "$Label contains a failed test"
  }
  Require ($Log.exit_code -eq 0) "$Label exit code is nonzero"
  Require (@($results | Where-Object {
      $_.name -ceq 'n23'
    }).Count -eq 1) "$Label lacks exactly one N23 PASS"

  $markers = @(
    'N23_ON_THIS_DAY_MATRIX',
    'N23_LAST_SEEN_MATRIX',
    'N23_BACKFILL_MATRIX',
    'N23_REGISTRY_MCP_MATRIX',
    'N23_SOURCE_BRAIN_ISOLATION'
  )
  foreach ($name in $markers) {
    $found = @($Log.output | Where-Object { [string]$_ -ceq ($name + '=pass') })
    Require ($found.Count -eq 1) "$Label lacks exactly one $name marker"
  }
  $countLines = @($Log.output | Where-Object {
      [string]$_ -match '^N23_SNAPSHOT_COUNT=([1-9][0-9]*)$'
    })
  Require ($countLines.Count -eq 1) "$Label lacks one positive N23 snapshot count"
  $countMatch = [regex]::Match([string]$countLines[0],
                              '^N23_SNAPSHOT_COUNT=([1-9][0-9]*)$')
  $snapshotCount = [int]$countMatch.Groups[1].Value
  $snapshotLines = @($Log.output | Where-Object {
      [string]$_ -cmatch '^N23_SNAPSHOT '
    })
  Require ($snapshotLines.Count -eq $snapshotCount) "$Label snapshot count differs from marker"
  $snapshots = New-Object System.Collections.Generic.List[object]
  for ($index = 0; $index -lt $snapshotLines.Count; ++$index) {
    $parsedSnapshot = Parse-SnapshotLine ([string]$snapshotLines[$index]) `
                                         ($index + 1) $Label
    $snapshots.Add($parsedSnapshot)
  }
  $labels = @($snapshots | ForEach-Object { $_.label })
  Require (($labels | Sort-Object -Unique).Count -eq
           $labels.Count) "$Label snapshot labels are not unique"
  Assert-N23SnapshotLabelContract $labels $Label
  $deltaLines = @($Log.output | Where-Object {
      [string]$_ -cmatch '^N23_ALLOWED_DELTA_JSON='
    })
  Require ($deltaLines.Count -eq 1) "$Label lacks exactly one structured allowed-delta row"
  $delta = Parse-AllowedDeltaLine ([string]$deltaLines[0]) $Label
  $resultText = @($results | ForEach-Object { $_.line }) -join "`n"
  $semanticLines = @($markers | Sort-Object | ForEach-Object { "$_=pass" })
  $semanticLines += @($snapshots | ForEach-Object {
      "snapshot:$($_.index):$($_.label)"
    })
  $semanticLines += "allowed_delta=$($delta.semantic_sha256)"
  $markerLines = @($Log.output | Where-Object {
      [string]$_ -match '^N23_(?:ON_THIS_DAY|LAST_SEEN|BACKFILL|REGISTRY_MCP|SOURCE_BRAIN|SNAPSHOT|ALLOWED_DELTA_JSON)'
    })
  [pscustomobject][ordered]@{
    registered = $Expected.Count
    passed = $results.Count
    failed = 0
    result_text = $resultText
    result_sha256 = Text-Hash $resultText
    snapshot_count = $snapshotCount
    snapshot_lines = $snapshotLines
    snapshot_labels = $labels
    normalized_marker_sha256 = Text-Hash ($semanticLines -join "`n")
    marker_lines = $markerLines
    allowed_delta_line = [string]$deltaLines[0]
    allowed_delta = $delta
  }
}

function Invoke-OfficialRuns([object]$Preparation) {
  Assert-EvidenceBuildMutexHeld
  Require ($RunBuilds.IsPresent) 'official Verify requires -RunBuilds'
  Require (-not $script:OfficialRunsCompleted) `
      'official native runs were already attempted in this process'
  Require ($script:VerificationRunNonce -cmatch '^[0-9a-f]{32}$') `
      'official native runs lack a current-process nonce'
  foreach ($logPath in @($ProductionBuildLog, $TestBuildLog, $SecondSuiteLog)) {
    Remove-SafeFile $logPath $EvidenceDir 'stale native evidence log'
  }
  $tempRoot = Get-SystemTempRoot
  $batchPaths = @(Get-BuildBatchPaths)
  Require (($batchPaths | ConvertTo-Json -Depth 4 -Compress) -ceq
           ($Preparation.tools.build_batch_paths |
             ConvertTo-Json -Depth 4 -Compress)) `
      'build batch paths changed after preparation'
  foreach ($batch in $batchPaths) {
    Remove-SafeFile (([string]$batch.path).Replace('/', '\')) $tempRoot `
        'stale build batch file'
  }
  # A success marker alone cannot prove that the linker produced the binary
  # used for evidence. Remove both fixed outputs while the cross-process mutex
  # is held, then require a newly created, stage-dated artifact after each
  # relevant build stage.
  Remove-PreexistingBuildArtifact $Qbrain 'preexisting production binary'
  Remove-PreexistingBuildArtifact $Tests 'preexisting test binary'
  $buildSandbox = $null
  $run2Sandbox = $null
  try {
    $buildSandbox = New-Sandbox 'n23-build-localappdata-'
    $run2Sandbox = New-Sandbox 'n23-run2-localappdata-'
    $windowsPowerShell = Get-ToolPath $Preparation.tools.windows_powershell
    $production = Invoke-Captured $windowsPowerShell @(
      '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
      'scripts\build-cl.ps1'
    ) $buildSandbox $ProductionBuildTimeoutSeconds
    Require ($production.exit_code -eq 0) 'production build failed'
    Require (@($production.lines | Where-Object {
        [string]$_ -ceq 'BUILD_OK'
      }).Count -eq 1) 'production build did not emit exactly one BUILD_OK'
    $productionArtifact = Get-FreshBuildArtifact $Qbrain $production.started `
        'production binary after build'
    $productionHash = [string]$productionArtifact.sha256
    $productionBatch = @($batchPaths | Where-Object {
        [string]$_.stage -ceq 'production'
      })[0]
    $productionBatchPath = ([string]$productionBatch.path).Replace('/', '\')
    $productionBatchHash = File-Hash $productionBatchPath $tempRoot 4MB
    $productionBanner = Get-CompilerBanner $production.lines 'production build capture'
    Write-Capture $ProductionBuildLog 'production-build' $ProductionCommand `
        $production ([ordered]@{
          target_arch = 'x64'
          language_mode = '/std:c++20'
          toolchain = 'MSVC'
          isolated_localappdata = 'true'
          localappdata_sandbox_kind = 'n23-production-build'
          artifact_sha256 = $productionHash
          artifact_preexisting = 'false'
          artifact_created_utc = [string]$productionArtifact.created_utc
          artifact_last_write_utc = [string]$productionArtifact.last_write_utc
          compiler_banner_sha256 = Text-Hash $productionBanner
          launcher_executable_path = [string]$Preparation.tools.windows_powershell.path
          launcher_executable_sha256 = [string]$Preparation.tools.windows_powershell.sha256
          vcvarsall_path = [string]$Preparation.tools.msvc.vcvarsall.path
          vcvarsall_sha256 = [string]$Preparation.tools.msvc.vcvarsall.sha256
          cl_path = [string]$Preparation.tools.msvc.cl.path
          cl_sha256 = [string]$Preparation.tools.msvc.cl.sha256
          link_path = [string]$Preparation.tools.msvc.link.path
          link_sha256 = [string]$Preparation.tools.msvc.link.sha256
          build_batch_path = [string]$productionBatch.path
          build_batch_sha256 = $productionBatchHash
        }) $Preparation
    Remove-SafeFile $productionBatchPath $tempRoot 'production build batch file'

    Remove-PreexistingBuildArtifact $Tests 'preexisting test binary before test build'
    $testBuild = Invoke-Captured $windowsPowerShell @(
      '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
      'scripts\build-tests-cl.ps1', '-SkipProductionBuild'
    ) $buildSandbox $TestBuildTimeoutSeconds
    Require ($testBuild.exit_code -eq 0) 'test build or suite run one failed'
    Require (@($testBuild.lines | Where-Object {
        [string]$_ -ceq 'TESTS_BUILD_OK'
      }).Count -eq 1) 'test build did not emit exactly one TESTS_BUILD_OK'
    $testArtifact = Get-FreshBuildArtifact $Tests $testBuild.started `
        'test binary after build'
    Require ((File-Hash $Qbrain $Root) -ceq $productionHash) `
        'production binary changed during test build'
    $testHash = [string]$testArtifact.sha256
    $testBatch = @($batchPaths | Where-Object {
        [string]$_.stage -ceq 'test'
      })[0]
    $testBatchPath = ([string]$testBatch.path).Replace('/', '\')
    $testBatchHash = File-Hash $testBatchPath $tempRoot 4MB
    $testBanner = Get-CompilerBanner $testBuild.lines 'test build capture'
    Write-Capture $TestBuildLog 'test-build-suite-run-1' $TestBuildCommand `
        $testBuild ([ordered]@{
          target_arch = 'x64'
          language_mode = '/std:c++20'
          toolchain = 'MSVC'
          isolated_localappdata = 'true'
          localappdata_sandbox_kind = 'n23-test-build-suite-run-1'
          suite_run_index = '1'
          expected_registered_tests = [string]$Preparation.tests.Count
          n23_focused_registered_tests = '1'
          integrated_regression_tests = [string]($Preparation.tests.Count - 1)
          production_binary_sha256 = $productionHash
          test_binary_sha256 = $testHash
          test_binary_preexisting = 'false'
          test_binary_created_utc = [string]$testArtifact.created_utc
          test_binary_last_write_utc = [string]$testArtifact.last_write_utc
          compiler_banner_sha256 = Text-Hash $testBanner
          launcher_executable_path = [string]$Preparation.tools.windows_powershell.path
          launcher_executable_sha256 = [string]$Preparation.tools.windows_powershell.sha256
          vcvarsall_path = [string]$Preparation.tools.msvc.vcvarsall.path
          vcvarsall_sha256 = [string]$Preparation.tools.msvc.vcvarsall.sha256
          cl_path = [string]$Preparation.tools.msvc.cl.path
          cl_sha256 = [string]$Preparation.tools.msvc.cl.sha256
          link_path = [string]$Preparation.tools.msvc.link.path
          link_sha256 = [string]$Preparation.tools.msvc.link.sha256
          build_batch_path = [string]$testBatch.path
          build_batch_sha256 = $testBatchHash
        }) $Preparation
    Remove-SafeFile $testBatchPath $tempRoot 'test build batch file'

    $second = Invoke-Captured $Tests @() $run2Sandbox $SuiteTimeoutSeconds
    Require ($second.exit_code -eq 0) 'full suite run two failed'
    Require ((File-Hash $Tests $Root) -ceq $testHash) `
        'test binary changed during run two'
    Write-Capture $SecondSuiteLog 'full-suite-run-2' $SecondSuiteCommand `
        $second ([ordered]@{
          target_arch = 'x64'
          language_mode = '/std:c++20'
          toolchain = 'MSVC'
          isolated_localappdata = 'true'
          localappdata_sandbox_kind = 'n23-full-suite-run-2'
          suite_run_index = '2'
          expected_registered_tests = [string]$Preparation.tests.Count
          n23_focused_registered_tests = '1'
          integrated_regression_tests = [string]($Preparation.tests.Count - 1)
          test_binary_sha256 = $testHash
          launcher_executable_path = Relative-Path $Tests
          launcher_executable_sha256 = $testHash
        }) $Preparation
    $script:OfficialRunsCompleted = $true
  } finally {
    foreach ($batch in $batchPaths) {
      Remove-SafeFile (([string]$batch.path).Replace('/', '\')) $tempRoot `
          'build batch file cleanup'
    }
    Remove-Sandboxes @($run2Sandbox, $buildSandbox)
  }
}

function Parse-DoctorCapture([object]$Capture, [string]$Label) {
  Require ([int]$Capture.exit_code -eq 0) "$Label exit code is nonzero"
  Require ([string]::IsNullOrWhiteSpace([string]$Capture.stderr)) "$Label stderr is not empty"
  Require ([bool]$Capture.child_environment_closed -and
           [bool]$Capture.process_job_assigned) `
      "$Label child execution was not closed and job-bound"
  foreach ($property in @('production_data_access_telemetry',
                           'git_mutation_telemetry',
                           'protected_configuration_telemetry')) {
    Require ([string]$Capture.$property -ceq 'not-collected') `
        "$Label telemetry makes an unmeasured claim"
  }
  $stdout = ([string]$Capture.stdout).Trim()
  Require ($stdout.Length -gt 0) "$Label stdout is empty"
  Assert-SafeText $stdout "$Label stdout"
  try {
    $doctor = ConvertFrom-JsonText $stdout
  } catch {
    throw "N23 evidence requirement failed: $Label stdout is not one JSON document"
  }
  Require-ExactProperties $doctor @(
    'checks', 'notes', 'ok', 'overall', 'schema_version', 'stats'
  ) "$Label result"
  Require ([bool]$doctor.ok -and
           [string]$doctor.overall -ceq 'OK' -and
           [int]$doctor.schema_version -eq 12) "$Label did not prove schema v12 OK"
  Require (-not ($doctor.PSObject.Properties.Name -contains 'db_path')) "$Label leaked a database path"
  $checks = @($doctor.checks)
  Require ($checks.Count -eq 4) "$Label health check count is not exact"
  foreach ($name in @('database', 'schema', 'critical_tables')) {
    $matching = @($checks | Where-Object { [string]$_.name -ceq $name })
    Require ($matching.Count -eq 1 -and
             [string]$matching[0].status -ceq 'OK') "$Label $name check is not exactly OK"
  }
  [pscustomobject][ordered]@{
    started = $Capture.started
    ended = $Capture.ended
    exit_code = 0
    ok = $true
    overall = 'OK'
    schema_version = 12
    stderr_empty = $true
    stdout_sha256 = Text-Hash $stdout
    child_environment_closed = $true
    process_job_assigned = $true
    child_environment_sha256 = [string]$Capture.environment_sha256
    production_data_access_telemetry = 'not-collected'
    git_mutation_telemetry = 'not-collected'
    protected_configuration_telemetry = 'not-collected'
  }
}

function Invoke-FinalDoctor([object]$Preparation) {
  $sandbox = $null
  $capture = $null
  $parsed = $null
  $brain = 'n23-final-schema-' + [guid]::NewGuid().ToString('N')
  $sandboxRoot = $null
  try {
    $sandbox = New-Sandbox 'n23-doctor-localappdata-'
    $sandboxRoot = [string]$sandbox.root
    $capture = Invoke-SeparateCapture $Qbrain @(
      'doctor', '--brain', $brain, '--json'
    ) $sandbox $DoctorTimeoutSeconds
    $parsed = Parse-DoctorCapture $capture 'final schema doctor'
  } finally {
    Remove-Sandboxes @($sandbox)
  }
  Require ($null -ne $parsed) 'final schema doctor did not produce parsed evidence'
  Require ($null -eq (Get-LiteralItemOrNull $sandboxRoot)) `
      'final doctor sandbox still exists'
  [pscustomobject][ordered]@{
    command = "build\cl\qbrain.exe doctor --brain $brain --json"
    brain_id = $brain
    started = $parsed.started
    ended = $parsed.ended
    exit_code = 0
    ok = $true
    overall = 'OK'
    schema_version = 12
    stderr_empty = $true
    stdout_sha256 = $parsed.stdout_sha256
    child_environment_closed = $true
    process_job_assigned = $true
    child_environment_sha256 = [string]$parsed.child_environment_sha256
    production_data_access_telemetry = 'not-collected'
    git_mutation_telemetry = 'not-collected'
    protected_configuration_telemetry = 'not-collected'
    isolated_localappdata = $true
    temporary_root_removed = $true
    executable_path = Relative-Path $Qbrain
    qbrain_sha256 = File-Hash $Qbrain $Root
    prebuild_manifest_sha256 = [string]$Preparation.prebuild_sha256
  }
}

function Get-SchemaContractHash([object[]]$Inputs) {
  $required = @(
    'include/qbrain/storage/schema_sql.hpp',
    'schema/001_init.sql',
    'src/qbrain/storage/migrate.cpp'
  )
  $rows = New-Object System.Collections.Generic.List[string]
  foreach ($path in $required) {
    $matching = @($Inputs | Where-Object { [string]$_.path -ceq $path })
    Require ($matching.Count -eq 1) 'schema contract input is not frozen exactly once'
    $rows.Add("$path`t$([string]$matching[0].sha256)`t$([int64]$matching[0].bytes)")
  }
  Text-Hash ($rows.ToArray() -join "`r`n")
}

function Assert-PublishedEvidenceBinding([object]$Expected,
                                         [object]$Preparation,
                                         [string]$ExpectedText) {
  $beforeManifestHash = File-Hash $ManifestPath $EvidenceDir $MaxJsonBytes
  $publishedText = Read-BoundedUtf8Text $ManifestPath $EvidenceDir `
      $MaxJsonBytes 'final evidence manifest'
  $afterManifestHash = File-Hash $ManifestPath $EvidenceDir $MaxJsonBytes
  Require ($beforeManifestHash -ceq $afterManifestHash) `
      'final evidence manifest changed while it was re-read'
  Require ((Text-Hash $publishedText) -ceq (Text-Hash $ExpectedText)) `
      'final evidence manifest bytes differ from the published value'
  $published = ConvertFrom-JsonText $publishedText
  Require-ExactProperties $published @(
    $Expected.PSObject.Properties | ForEach-Object { $_.Name }
  ) 'final evidence manifest'
  foreach ($property in @(
      'format_version', 'node', 'state', 'approved_plan_sha256',
      'plan_audit_sha256', 'pre_corrective_gate_sha256',
      'prebuild_manifest_sha256', 'pending_manifest_sha256', 'evidence_nonce',
      'verification_run_nonce', 'schema_version', 'schema_contract_sha256',
      'production_binary_sha256', 'test_binary_sha256',
      'git_binding_rechecked', 'excluded_n30_input_count',
      'audit_verdict_written', 'node_or_ledger_status_written')) {
    Require ([string]$published.$property -ceq [string]$Expected.$property) `
        "final evidence manifest $property binding is invalid"
  }
  Require ([string]$published.pending_manifest_sha256 -ceq
           [string]$Preparation.pending_manifest_sha256) `
      'final evidence manifest is not bound to the pending manifest'
  Require ([bool]$published.git_binding_rechecked -and
           -not [bool]$published.audit_verdict_written -and
           -not [bool]$published.node_or_ledger_status_written) `
      'final evidence manifest claims forbidden authority'
  Require-ExactProperties $published.evidence_build_mutex @(
    'name', 'held', 'abandoned_recovery'
  ) 'final evidence/build mutex'
  Require ([string]$published.evidence_build_mutex.name -ceq
           $EvidenceBuildMutexName -and
           [bool]$published.evidence_build_mutex.held -eq $true -and
           [bool]$published.evidence_build_mutex.abandoned_recovery -eq
               [bool]$script:EvidenceBuildMutexAbandoned) `
      'final evidence/build mutex binding is invalid'
  $expectedEntries = @($Expected.evidence_files)
  $actualEntries = @($published.evidence_files)
  Require ($actualEntries.Count -eq $expectedEntries.Count) `
      'final evidence file count changed after publication'
  for ($index = 0; $index -lt $actualEntries.Count; ++$index) {
    $entry = $actualEntries[$index]
    $expectedEntry = $expectedEntries[$index]
    Require-ExactProperties $entry @('path', 'role', 'sha256', 'bytes') `
        'final evidence file entry'
    Require ([string]$entry.path -ceq [string]$expectedEntry.path -and
             [string]$entry.role -ceq [string]$expectedEntry.role -and
             [string]$entry.sha256 -ceq [string]$expectedEntry.sha256 -and
             [int64]$entry.bytes -eq [int64]$expectedEntry.bytes) `
        'final evidence file entry binding changed after publication'
    $path = Resolve-RepositoryRelativePath ([string]$entry.path) `
        'final evidence file path'
    $item = Get-SafeFileItem $path $Root $MaxImportedLogBytes `
        'final evidence file'
    Require ([int64]$item.Length -eq [int64]$entry.bytes -and
             (File-Hash $path $Root $MaxImportedLogBytes) -ceq
                 [string]$entry.sha256) `
        'final evidence file changed after publication'
  }
  $published
}

function Complete-Verification {
  Assert-EvidenceBuildMutexHeld
  Require ($RunBuilds.IsPresent) `
      'formal Verify must be invoked with -RunBuilds in this process'
  $script:VerificationRunNonce = [guid]::NewGuid().ToString('N')
  $script:OfficialRunsCompleted = $false
  Assert-FixedLogPaths
  $preparation = Read-Preparation
  Invoke-OfficialRuns $preparation
  Require ($script:OfficialRunsCompleted) `
      'official native runs did not complete in this Verify process'

  $production = Parse-Log $ProductionBuildLog
  $run1Log = Parse-Log $TestBuildLog
  $run2Log = Parse-Log $SecondSuiteLog
  $productionBanner = Assert-NativeLog $production 'production-build' $preparation
  $testBanner = Assert-NativeLog $run1Log 'test-build-suite-run-1' $preparation
  [void](Assert-NativeLog $run2Log 'full-suite-run-2' $preparation)
  Require (-not [string]::IsNullOrWhiteSpace([string]$productionBanner)) 'production compiler evidence is empty'
  Require (-not [string]::IsNullOrWhiteSpace([string]$testBanner)) 'test compiler evidence is empty'
  Require ($production.started -ge $preparation.prepared_utc) 'production build predates the prebuild freeze'
  Require ($run1Log.started -ge $production.ended) 'test build did not follow the production build'
  Require ($run2Log.started -ge $run1Log.ended) 'second suite did not follow suite run one'

  $productionHash = File-Hash $Qbrain $Root
  $testHash = File-Hash $Tests $Root
  Require ($productionHash -ceq
           [string]$production.fields.artifact_sha256) 'production log is not bound to qbrain.exe'
  Require ($productionHash -ceq
           [string]$run1Log.fields.production_binary_sha256) 'test-build log is not bound to qbrain.exe'
  Require ($testHash -ceq
           [string]$run1Log.fields.test_binary_sha256) 'suite run one is not bound to qbrain_tests.exe'
  Require ($testHash -ceq
           [string]$run2Log.fields.test_binary_sha256) 'suite run two is not bound to qbrain_tests.exe'

  $run1 = Assert-TestRun $run1Log $preparation.tests 'full-suite run one'
  $run2 = Assert-TestRun $run2Log $preparation.tests 'full-suite run two'
  Require ($run1.result_text -ceq $run2.result_text) 'PASS result streams are not byte-identical'
  Require ($run1.normalized_marker_sha256 -ceq
           $run2.normalized_marker_sha256) 'N23 marker semantics differ between runs'
  Require ($run1.allowed_delta.semantic_sha256 -ceq
           $run2.allowed_delta.semantic_sha256) 'N23 allowed-delta proofs differ between runs'

  $doctor = Invoke-FinalDoctor $preparation
  Require ($doctor.started -ge $run2Log.ended) 'final schema doctor did not follow suite run two'
  Require ([string]$doctor.qbrain_sha256 -ceq $productionHash) 'final doctor used a different production binary'
  $post = Read-Preparation
  Require ([string]$post.prebuild_sha256 -ceq
           [string]$preparation.prebuild_sha256) 'prebuild binding changed during verification'
  Require ((File-Hash $Tests $Root) -ceq $testHash) `
      'test binary changed after verification'
  Require ((File-Hash $Qbrain $Root) -ceq $productionHash) `
      'production binary changed after verification'
  Require ([bool]$doctor.child_environment_closed -and
           [bool]$doctor.process_job_assigned -and
           [string]$doctor.production_data_access_telemetry -ceq 'not-collected' -and
           [string]$doctor.git_mutation_telemetry -ceq 'not-collected' -and
           [string]$doctor.protected_configuration_telemetry -ceq 'not-collected') `
      'final doctor process or telemetry facts are invalid'

  $schemaContractHash = Get-SchemaContractHash $preparation.inputs
  Write-AtomicUtf8Lines $FocusedPath $run1.marker_lines $EvidenceDir
  Write-AtomicUtf8Lines $SnapshotPath $run1.snapshot_lines $EvidenceDir
  Write-AtomicUtf8Lines $SchemaPath @(
    'format=n23-final-schema-doctor-v3',
    "command=$($doctor.command)",
    "started_utc=$($doctor.started.ToString('o'))",
    "ended_utc=$($doctor.ended.ToString('o'))",
    'exit_code=0',
    'ok=true',
    'overall=OK',
    'schema_version=12',
    'stderr_empty=true',
    'isolated_localappdata=true',
    "child_environment_policy=$ChildEnvironmentPolicy",
    'child_environment_closed=true',
    'process_job_assigned=true',
    'temporary_root_removed=true',
    'n23_schema_contract_changed=false',
    "schema_contract_sha256=$schemaContractHash",
    "qbrain_sha256=$productionHash",
    "stdout_sha256=$($doctor.stdout_sha256)",
    'production_data_access_telemetry=not-collected',
    'git_commit_or_push_telemetry=not-collected',
    'protected_configuration_change_telemetry=not-collected'
  ) $EvidenceDir
  Write-AtomicUtf8Lines $PlatformPath @(
    'os=Windows-native',
    'architecture=x64',
    'language_mode=/std:c++20',
    'toolchain=MSVC',
    "production_compiler=$productionBanner",
    "test_compiler=$testBanner",
    "production_compiler_sha256=$(Text-Hash $productionBanner)",
    "test_compiler_sha256=$(Text-Hash $testBanner)",
    "windows_powershell_path=$($preparation.tools.windows_powershell.path)",
    "windows_powershell_sha256=$($preparation.tools.windows_powershell.sha256)",
    "git_path=$($preparation.tools.git.path)",
    "git_sha256=$($preparation.tools.git.sha256)",
    "vcvarsall_path=$($preparation.tools.msvc.vcvarsall.path)",
    "vcvarsall_sha256=$($preparation.tools.msvc.vcvarsall.sha256)",
    "cl_path=$($preparation.tools.msvc.cl.path)",
    "cl_sha256=$($preparation.tools.msvc.cl.sha256)",
    "link_path=$($preparation.tools.msvc.link.path)",
    "link_sha256=$($preparation.tools.msvc.link.sha256)"
  ) $EvidenceDir
  Write-AtomicUtf8Lines $ProductionTreePath @(
    'format=n23-telemetry-v2',
    'production_data_access_telemetry=not-collected',
    'git_commit_or_push_telemetry=not-collected',
    'protected_configuration_change_telemetry=not-collected',
    'child_localappdata_temp_tmp_closed=true',
    "child_environment_policy=$ChildEnvironmentPolicy",
    "verification_run_nonce=$($script:VerificationRunNonce)"
  ) $EvidenceDir

  $reportLines = @(
    '# N23 Verification Report',
    '',
    'This report records factual native evidence only; it is not a Claude Code audit verdict.',
    '',
    "- Immutable pre-corrective gate SHA-256: $ExpectedGateHash.",
    "- Frozen prebuild manifest SHA-256: $($preparation.prebuild_sha256).",
    "- Registered tests: $($run1.registered); both frozen-binary runs: $($run1.passed) PASS / 0 FAIL.",
    '- N23 focused evidence: exactly one registered `n23` PASS plus the N23 marker, snapshot, and allowed-delta contracts.',
    "- Integrated regression only: $($run1.registered - 1) non-N23 registered tests; these results are not attributed to N23.",
    "- Production binary SHA-256: $productionHash.",
    "- Test binary SHA-256: $testHash.",
    '- Fresh-output provenance: each fixed evidence binary was absent immediately before its build stage, then its creation and last-write timestamps were bound inside that stage log.',
    "- N23 snapshot rows: $($run1.snapshot_count); semantic marker SHA-256: $($run1.normalized_marker_sha256).",
    "- Allowed backfill delta: tagged=2, already_tagged=1, exactly bf-at and bf-new gained Chronicle tags; all non-tag tables, schema objects, sqlite_sequence, and the decoy remained unchanged.",
    "- Final isolated doctor: ok=true, overall=OK, schema_version=12; schema contract SHA-256: $schemaContractHash.",
    '- Native commands and final doctor used closed child environments with isolated LOCALAPPDATA/TEMP/TMP roots, pinned PATH values, 32 MiB per-stream limits, stage timeouts, and kill-on-close process jobs.',
    '- Production-data access telemetry: not-collected. Git commit/push telemetry: not-collected. Protected-configuration change telemetry: not-collected.',
    '- Git top-level, explicit git-dir/common-dir/work-tree binding, HEAD, and scoped worktree facts were frozen and rechecked; this is not Git mutation telemetry.',
    '- The verifier did not write an audit verdict, node status, or ledger state.',
    '- A fresh node-specific Claude Code outcome hard audit remains blocking before status or ledger reconciliation.'
  )
  # The report is the penultimate publication. The final manifest below is the
  # sole last-published completion marker; the top-level catch restores pending.
  Write-AtomicUtf8Lines $ReportPath $reportLines $EvidenceDir

  $outputs = @(
    $ProductionBuildLog,
    $TestBuildLog,
    $SecondSuiteLog,
    $FocusedPath,
    $SnapshotPath,
    $SchemaPath,
    $PlatformPath,
    $ProductionTreePath,
    $PrebuildPath,
    $GatePath,
    $ReportPath
  )
  $entries = @($outputs | ForEach-Object { File-Entry $_ 'evidence' })
  $evidence = [pscustomobject][ordered]@{
    format_version = 2
    node = 'N23'
    state = 'verified-pending-claude-outcome-audit'
    verified_utc = [DateTimeOffset]::UtcNow.ToString('o')
    approved_plan_sha256 = $ExpectedApprovedPlanHash
    plan_audit_sha256 = $ExpectedPlanAuditHash
    pre_corrective_gate_sha256 = $ExpectedGateHash
    prebuild_manifest_sha256 = [string]$preparation.prebuild_sha256
    pending_manifest_sha256 = [string]$preparation.pending_manifest_sha256
    evidence_nonce = [string]$preparation.evidence_nonce
    verification_run_nonce = [string]$script:VerificationRunNonce
    schema_version = 12
    schema_contract_sha256 = $schemaContractHash
    final_doctor = [pscustomobject][ordered]@{
      command = [string]$doctor.command
      exit_code = 0
      ok = $true
      overall = 'OK'
      schema_version = 12
      stderr_empty = $true
      isolated_localappdata = $true
      temporary_root_removed = $true
      child_environment_policy = $ChildEnvironmentPolicy
      child_environment_closed = $true
      process_job_assigned = $true
      child_environment_sha256 = [string]$doctor.child_environment_sha256
      production_data_access_telemetry = 'not-collected'
      git_mutation_telemetry = 'not-collected'
      protected_configuration_telemetry = 'not-collected'
      stdout_sha256 = [string]$doctor.stdout_sha256
      executable_path = [string]$doctor.executable_path
      qbrain_sha256 = $productionHash
    }
    native_commands = @(
      [pscustomobject][ordered]@{
        stage = 'production-build'
        command = $ProductionCommand
        exit_code = 0
        artifact_preexisting = $false
        artifact_created_utc = [string]$production.fields.artifact_created_utc
        artifact_last_write_utc = [string]$production.fields.artifact_last_write_utc
        launcher = $preparation.tools.windows_powershell
        log_sha256 = File-Hash $ProductionBuildLog $EvidenceDir $MaxImportedLogBytes
      },
      [pscustomobject][ordered]@{
        stage = 'test-build-suite-run-1'
        command = $TestBuildCommand
        exit_code = 0
        suite_run_index = 1
        registered_tests = $run1.registered
        n23_focused_tests = 1
        integrated_regression_tests = $run1.registered - 1
        test_binary_preexisting = $false
        test_binary_created_utc = [string]$run1Log.fields.test_binary_created_utc
        test_binary_last_write_utc = [string]$run1Log.fields.test_binary_last_write_utc
        launcher = $preparation.tools.windows_powershell
        log_sha256 = File-Hash $TestBuildLog $EvidenceDir $MaxImportedLogBytes
      },
      [pscustomobject][ordered]@{
        stage = 'full-suite-run-2'
        command = $SecondSuiteCommand
        exit_code = 0
        suite_run_index = 2
        registered_tests = $run2.registered
        n23_focused_tests = 1
        integrated_regression_tests = $run2.registered - 1
        launcher_path = Relative-Path $Tests
        launcher_sha256 = $testHash
        log_sha256 = File-Hash $SecondSuiteLog $EvidenceDir $MaxImportedLogBytes
      }
    )
    registered_tests = $run1.registered
    n23_focused_registered_tests = @('n23')
    integrated_regression_registered_tests = @(
      $preparation.manifest.integrated_regression_registered_tests
    )
    passed_first_run = $run1.passed
    passed_second_run = $run2.passed
    failed_first_run = 0
    failed_second_run = 0
    n23_snapshot_count = $run1.snapshot_count
    n23_normalized_marker_sha256 = $run1.normalized_marker_sha256
    result_stream_sha256 = $run1.result_sha256
    allowed_delta = [pscustomobject][ordered]@{
      tagged = 2
      already_tagged = 1
      added_slugs = @('bf-at', 'bf-new')
      removed_tag_rows = 0
      non_tag_table_count = $run1.allowed_delta.non_tag_table_count
      schema_object_count = $run1.allowed_delta.schema_object_count
      sqlite_sequence_row_count = $run1.allowed_delta.sqlite_sequence_row_count
      semantic_sha256 = $run1.allowed_delta.semantic_sha256
      selected_before_sha256 = $run1.allowed_delta.selected_before_sha256
      selected_after_sha256 = $run1.allowed_delta.selected_after_sha256
      decoy_sha256 = $run1.allowed_delta.decoy_sha256
    }
    production_binary_sha256 = $productionHash
    test_binary_sha256 = $testHash
    build_source_closure = $preparation.build_source_closure
    tools = $preparation.tools
    evidence_build_mutex = [pscustomobject][ordered]@{
      name = $EvidenceBuildMutexName
      held = $true
      abandoned_recovery = [bool]$script:EvidenceBuildMutexAbandoned
    }
    platform = [pscustomobject][ordered]@{
      os = 'Windows-native'
      architecture = 'x64'
      language_mode = '/std:c++20'
      toolchain = 'MSVC'
      production_compiler_sha256 = Text-Hash $productionBanner
      test_compiler_sha256 = Text-Hash $testBanner
    }
    child_environment_policy = $ChildEnvironmentPolicy
    telemetry = [pscustomobject][ordered]@{
      production_data_access = 'not-collected'
      git_commit_or_push = 'not-collected'
      protected_configuration_changes = 'not-collected'
    }
    evidence_files = $entries
    git_binding_rechecked = $true
    excluded_n30_input_count = [int]$preparation.manifest.excluded_n30_input_count
    audit_verdict_written = $false
    node_or_ledger_status_written = $false
  }
  # Deliberately last: any exception before/during this atomic publication
  # leaves EvidenceInitialized set, so the top-level catch restores v2 pending.
  $evidenceText = ($evidence | ConvertTo-Json -Depth 12) +
                  [Environment]::NewLine
  Write-AtomicUtf8Text $ManifestPath $evidenceText $EvidenceDir
  [void](Assert-PublishedEvidenceBinding $evidence $preparation $evidenceText)
  $script:EvidenceInitialized = $false
  Write-Host "N23_VERIFY_OK registered=$($run1.registered) first_pass=$($run1.passed) second_pass=$($run2.passed) snapshots=$($run1.snapshot_count) schema=12 gate=$ExpectedGateHash"
}

function Assert-Throws([scriptblock]$Action, [string]$Label,
                       [string]$MessagePattern = '') {
  $threw = $false
  $message = ''
  try {
    & $Action
  } catch {
    $threw = $true
    $message = [string]$_.Exception.Message
  }
  Require ($threw) "$Label did not fail closed"
  if (-not [string]::IsNullOrWhiteSpace($MessagePattern)) {
    Require ($message -match $MessagePattern) `
        "$Label failed for the wrong reason"
  }
}

function New-SyntheticCapture([string[]]$Lines, [int]$TimeoutSeconds) {
  $started = [DateTimeOffset]::UtcNow
  $stdout = (@($Lines) -join [Environment]::NewLine) + [Environment]::NewLine
  [pscustomobject][ordered]@{
    started = $started
    ended = $started.AddSeconds(1)
    exit_code = 0
    lines = $Lines
    stdout_bytes = [int64]$StrictUtf8.GetByteCount($stdout)
    stderr_bytes = 0
    stdout_sha256 = Text-Hash $stdout
    stderr_sha256 = Text-Hash ''
    environment_sha256 = 'e' * 64
    environment_names = @('LOCALAPPDATA', 'PATH', 'TEMP', 'TMP')
    child_environment_closed = $true
    process_job_assigned = $true
    timeout_seconds = $TimeoutSeconds
    per_stream_byte_limit = $MaxCapturedStreamBytes
    production_data_access_telemetry = 'not-collected'
    git_mutation_telemetry = 'not-collected'
    protected_configuration_telemetry = 'not-collected'
  }
}

function New-SelfTestAllowedDelta {
  $hashA = 'a' * 64
  $hashB = 'b' * 64
  $hashC = 'c' * 64
  $hashD = 'd' * 64
  $hashE = 'e' * 64
  $existing = [pscustomobject][ordered]@{
    page_id = 2
    source_id = 'default'
    slug = 'bf-existing'
    tag = 'chronicle'
  }
  $new = [pscustomobject][ordered]@{
    page_id = 1
    source_id = 'default'
    slug = 'bf-new'
    tag = 'chronicle'
  }
  $at = [pscustomobject][ordered]@{
    page_id = 3
    source_id = 'default'
    slug = 'bf-at'
    tag = 'chronicle'
  }
  [pscustomobject][ordered]@{
    format_version = 1
    selected_full = [pscustomobject][ordered]@{
      before_sha256 = $hashA
      after_sha256 = $hashB
      before_bytes = 100
      after_bytes = 120
    }
    decoy_full = [pscustomobject][ordered]@{
      before_sha256 = $hashC
      after_sha256 = $hashC
      before_bytes = 80
      after_bytes = 80
    }
    schema = [pscustomobject][ordered]@{
      before_sha256 = $hashD
      after_sha256 = $hashD
      objects = @(
        [pscustomobject][ordered]@{
          type = 'table'
          name = 'pages'
          tbl_name = 'pages'
          before_sql_sha256 = $hashA
          after_sql_sha256 = $hashA
        },
        [pscustomobject][ordered]@{
          type = 'table'
          name = 'sqlite_sequence'
          tbl_name = 'sqlite_sequence'
          before_sql_sha256 = $hashB
          after_sql_sha256 = $hashB
        },
        [pscustomobject][ordered]@{
          type = 'table'
          name = 'tags'
          tbl_name = 'tags'
          before_sql_sha256 = $hashC
          after_sql_sha256 = $hashC
        }
      )
    }
    non_tag_tables = @(
      [pscustomobject][ordered]@{
        name = 'pages'
        before_sha256 = $hashE
        after_sha256 = $hashE
        before_rows = 3
        after_rows = 3
      }
    )
    tags = [pscustomobject][ordered]@{
      before_rows = @($existing)
      after_rows = @($new, $existing, $at)
      added_rows = @($new, $at)
      removed_rows = @()
      before_sha256 = $hashA
      after_sha256 = $hashB
    }
    sqlite_sequence = [pscustomobject][ordered]@{
      before_rows = @(
        [pscustomobject][ordered]@{ name = 'pages'; seq = 3 }
      )
      after_rows = @(
        [pscustomobject][ordered]@{ name = 'pages'; seq = 3 }
      )
      delta = @()
      before_sha256 = $hashC
      after_sha256 = $hashC
    }
    result = [pscustomobject][ordered]@{
      tagged = 2
      already_tagged = 1
    }
  }
}

function Invoke-ParserSelfTest {
  Require ($PSVersionTable.PSVersion.Major -ge 5) 'PowerShell host is below the supported parser baseline'
  Assert-EvidenceBuildMutexHeld
  Require ($EvidenceBuildMutexName -ceq 'Local\Qbrain.N23.EvidenceBuild') `
      'evidence/build mutex name is not stable'
  Require ((Text-Hash 'n23-parser-self-test') -cmatch '^[0-9a-f]{64}$') 'hash self-test failed'
  Assert-SafeText 'native Windows C++20 parser self-test' 'parser self-test'
  Require (-not (Test-N30ArtifactPath 'tests/test_n23.cpp')) `
      'N23 input was misclassified as an excluded N30 artifact'
  Assert-Throws {
    [void](Assert-NoN30ArtifactPath 'tests/N30-shadow.cpp' `
        'N30 classifier self-test')
  } 'conservative N30 path classifier'
  $verifierText = Read-BoundedUtf8Text $VerifierPath $Root $MaxJsonBytes `
      'verifier self-test input'
  Require ($verifierText -notmatch '\[string\]\$ProductionBuildLog' -and
           $verifierText -notmatch '\[string\]\$TestBuildLog' -and
           $verifierText -notmatch '\[string\]\$SecondSuiteLog') `
      'formal Verify still accepts caller-supplied evidence logs'
  Require ($verifierText -match
      "(?s)\[Parameter\(Mandatory = [`$]true, ParameterSetName = 'Verify'\)\]\s*\[switch\][`$]RunBuilds") `
      'formal Verify does not require -RunBuilds'
  $completeText = [regex]::Match($verifierText,
      '(?s)function Complete-Verification\s*\{(.*?)\n\}').Groups[1].Value
  Require ($completeText -notmatch 'if\s*\(\$RunBuilds\)' -and
           $completeText -match '(?m)^\s*Invoke-OfficialRuns\s+\$preparation\s*$') `
      'formal Verify can bypass its same-process official runs'
  foreach ($blockedName in @(
      'GIT_CONFIG_KEY_X', 'GIT_CONFIG_VALUE_bad', 'GIT_DIR', 'GIT_WORK_TREE',
      'GIT_COMMON_DIR', 'GIT_INDEX_FILE', 'QBRAIN_SOURCE', 'QBRAIN_TOKEN',
      'OPENAI_API_KEY', 'ANTHROPIC_TOKEN', 'HTTP_PROXY', 'MODEL')) {
    Require (Test-BlockedChildEnvironmentName $blockedName) `
        "blocked child environment name escaped: $blockedName"
  }
  Require (-not (Test-BlockedChildEnvironmentName 'SYSTEMROOT')) `
      'safe child environment name was blocked'

  $buildClosure = Get-BuildSourceClosure
  Require (@($buildClosure.n23_focused_test_sources).Count -eq 1 -and
           [string]$buildClosure.n23_focused_test_sources[0] -ceq
               'tests/test_n23.cpp') `
      'build source closure does not isolate the N23 focused source'
  Require (@($buildClosure.integrated_regression_test_sources) -ccontains
           'tests/test_n24_25.cpp') `
      'later-wave test is not classified as integrated regression'
  Require (@($buildClosure.n23_focused_test_sources) -cnotcontains
           'tests/test_n24_25.cpp') `
      'later-wave test is misclassified as N23 evidence'
  $inputEntries = @(Get-InputEntries $buildClosure)
  $focusedEntries = @($inputEntries | Where-Object {
      [string]$_.role -ceq 'n23-focused-test-evidence'
    })
  Require ($focusedEntries.Count -eq 1 -and
           [string]$focusedEntries[0].path -ceq 'tests/test_n23.cpp') `
      'input manifest N23 evidence role is invalid'
  $laterWaveEntry = @($inputEntries | Where-Object {
      [string]$_.path -ceq 'tests/test_n24_25.cpp'
    })
  Require ($laterWaveEntry.Count -eq 1 -and
           [string]$laterWaveEntry[0].role -ceq
               'integrated-regression-test-source') `
      'input manifest later-wave role is invalid'

  $systemTempRoot = Get-SystemTempRoot
  $temporaryRoot = Join-Path $systemTempRoot (
     'qbrain-n23-parser-selftest-' + [guid]::NewGuid().ToString('N')
  )
  $outsideRoot = Join-Path $systemTempRoot (
    'qbrain-n23-parser-outside-' + [guid]::NewGuid().ToString('N')
  )
  $oldProductionLog = $DefaultProductionLog
  $oldTestLog = $DefaultTestLog
  $oldSecondLog = $DefaultSecondLog
  $oldActiveLogRoot = $script:ActiveLogRoot
  $oldVerificationRunNonce = $script:VerificationRunNonce
  $junctionPath = $null
  $processSandbox = $null
  try {
    [void](Assert-PlainPathChain $temporaryRoot $systemTempRoot `
        'self-test root' 'Missing' $true)
    New-Item -ItemType Directory -Path $temporaryRoot -ErrorAction Stop | Out-Null
    [void](Assert-PlainPathChain $temporaryRoot $systemTempRoot `
        'self-test root' 'Directory')
    [void](Assert-PlainPathChain $outsideRoot $systemTempRoot `
        'self-test outside root' 'Missing' $true)
    New-Item -ItemType Directory -Path $outsideRoot -ErrorAction Stop | Out-Null
    [void](Assert-PlainPathChain $outsideRoot $systemTempRoot `
        'self-test outside root' 'Directory')
    $script:DefaultProductionLog = Join-Path $temporaryRoot 'PRODUCTION-BUILD-OUTPUT.txt'
    $script:DefaultTestLog = Join-Path $temporaryRoot 'TEST-BUILD-AND-SUITE-RUN-1.txt'
    $script:DefaultSecondLog = Join-Path $temporaryRoot 'FULL-SUITE-RUN-2.txt'
    $script:ActiveLogRoot = $temporaryRoot
    $script:VerificationRunNonce = 'f' * 32

    $prebuild = Join-Path $temporaryRoot 'PREBUILD-MANIFEST.json'
    Write-AtomicUtf8Text $prebuild '{"selftest":true}' $temporaryRoot
    $prebuildHash = File-Hash $prebuild $temporaryRoot
    $prepared = [DateTimeOffset]::UtcNow.ToString('o')
    $nonce = '1' * 32
    $pendingPath = Join-Path $temporaryRoot 'EVIDENCE-MANIFEST.json'
    $pendingReport = Join-Path $temporaryRoot 'VERIFY-REPORT.md'
    Write-Pending $prepared $prebuildHash $nonce $pendingReport $pendingPath `
        $temporaryRoot
    $pending = Read-PendingManifest $pendingPath $prebuild $prepared $nonce `
        $temporaryRoot
    Require ([int]$pending.format_version -eq 2) `
        'pending self-test did not produce format v2'
    Write-AtomicUtf8Lines $pendingReport @('# tampered pending report') `
        $temporaryRoot
    Assert-Throws {
      [void](Read-PendingManifest $pendingPath $prebuild $prepared $nonce `
          $temporaryRoot)
    } 'pending-report hash binding'
    Write-Pending $prepared $prebuildHash $nonce $pendingReport $pendingPath `
        $temporaryRoot
    $pending = Read-PendingManifest $pendingPath $prebuild $prepared $nonce `
        $temporaryRoot

    $gitFacts = [pscustomobject][ordered]@{
      head = $ExpectedGitHead
      work_tree = 'D:/Projects/Qbrain'
      git_dir = 'D:/Projects/Qbrain/.git'
      common_dir = 'D:/Projects/Qbrain/.git'
      scoped_diff_sha256 = '2' * 64
      scoped_untracked_input_count = 2
    }
    $frozenGitFacts = [pscustomobject][ordered]@{
      head = $ExpectedGitHead
      work_tree = 'D:/Projects/Qbrain'
      git_dir = 'D:/Projects/Qbrain/.git'
      common_dir = 'D:/Projects/Qbrain/.git'
      scoped_diff_sha256 = '2' * 64
      scoped_untracked_input_count = 2
    }
    Assert-GitFactsBinding $gitFacts $frozenGitFacts
    $frozenGitFacts.scoped_untracked_input_count = 1
    Assert-Throws {
      Assert-GitFactsBinding $gitFacts $frozenGitFacts
    } 'scoped Git-fact binding'
    $actualGitFacts = Get-GitFacts
    Require ([string]$actualGitFacts.head -ceq $ExpectedGitHead -and
             [string]$actualGitFacts.work_tree -ceq $Root.Replace('\', '/')) `
        'actual explicit Git binding self-test failed'
    $actualToolchain = Get-ToolchainFacts
    foreach ($fact in @($actualToolchain.vcvarsall, $actualToolchain.cl,
                         $actualToolchain.link)) {
      Assert-Sha256 ([string]$fact.sha256) `
          'actual toolchain executable hash'
    }

    $badPendingPath = Join-Path $temporaryRoot 'BAD-PENDING.json'
    $pending.prebuild_manifest_sha256 = '0' * 64
    Write-AtomicUtf8Text $badPendingPath (($pending | ConvertTo-Json -Depth 4) +
                                          [Environment]::NewLine) $temporaryRoot
    Assert-Throws {
      [void](Read-PendingManifest $badPendingPath $prebuild $prepared $nonce `
          $temporaryRoot)
    } 'pending-manifest binding parser'
    $pending.prebuild_manifest_sha256 = $prebuildHash
    $badNoncePath = Join-Path $temporaryRoot 'BAD-NONCE.json'
    $pending.evidence_nonce = '9' * 32
    Write-AtomicUtf8Text $badNoncePath (($pending | ConvertTo-Json -Depth 4) +
                                        [Environment]::NewLine) $temporaryRoot
    Assert-Throws {
      [void](Read-PendingManifest $badNoncePath $prebuild $prepared $nonce `
          $temporaryRoot)
    } 'pending-manifest nonce parser'
    $pending.evidence_nonce = $nonce

    $boundedPath = Join-Path $temporaryRoot 'BOUNDED.txt'
    Write-AtomicUtf8Text $boundedPath ('x' * 128) $temporaryRoot
    Assert-Throws {
      [void](Read-BoundedUtf8Text $boundedPath $temporaryRoot 16 `
          'over-limit self-test input')
    } 'bounded file reader'
    $outsideFile = Join-Path $outsideRoot 'outside.txt'
    Write-AtomicUtf8Text $outsideFile 'outside' $outsideRoot
    Assert-Throws {
      [void](File-Hash $outsideFile $temporaryRoot)
    } 'hash root confinement'
    Assert-Throws {
      Write-AtomicUtf8Text $outsideFile 'escape' $temporaryRoot
    } 'atomic writer root confinement'

    $targetDirectory = Join-Path $outsideRoot 'junction-target'
    [void](Ensure-PlainDirectory $targetDirectory $outsideRoot `
        'junction target')
    $targetFile = Join-Path $targetDirectory 'target.txt'
    Write-AtomicUtf8Text $targetFile 'target-stays' $outsideRoot
    $junctionPath = Join-Path $temporaryRoot 'junction-ancestor'
    [void](Assert-PlainPathChain $junctionPath $temporaryRoot `
        'self-test junction' 'Missing' $true)
    New-Item -ItemType Junction -Path $junctionPath -Target $targetDirectory `
        -ErrorAction Stop | Out-Null
    $junctionItem = Get-Item -LiteralPath $junctionPath -Force
    Require (($junctionItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) `
        'self-test could not create a junction'
    $throughJunction = Join-Path $junctionPath 'target.txt'
    Assert-Throws {
      [void](File-Hash $throughJunction $temporaryRoot)
    } 'hash ancestor-reparse confinement'
    Assert-Throws {
      Write-AtomicUtf8Text (Join-Path $junctionPath 'new.txt') 'blocked' `
          $temporaryRoot
    } 'atomic writer ancestor-reparse confinement'
    Assert-Throws {
      Remove-SafeTree $temporaryRoot $systemTempRoot `
          'self-test reparse cleanup refusal'
    } 'cleanup ancestor-reparse confinement'
    [IO.Directory]::Delete($junctionPath, $false)
    $junctionPath = $null
    Require ((Read-BoundedUtf8Text $targetFile $outsideRoot 1024 `
        'junction target survivor') -ceq 'target-stays') `
        'reparse refusal damaged the junction target'

    $processSandbox = New-Sandbox 'n23-selftest-process-'
    $baseTools = Get-BaseToolFacts
    $closedEnvironment = New-ClosedChildEnvironment $processSandbox $baseTools
    foreach ($name in $closedEnvironment.Keys) {
      Require (-not (Test-BlockedChildEnvironmentName ([string]$name))) `
          'closed environment self-test retained a blocked name'
    }
    Require ([string]$closedEnvironment.PATH -notmatch
             [regex]::Escape([string]$env:PATH)) `
        'closed environment reused the ambient PATH'
    $windowsPowerShell = Get-ToolPath $baseTools.windows_powershell
    Assert-Throws {
      [void](Invoke-Captured $windowsPowerShell @(
          '-NoProfile', '-Command',
          '$child = Start-Process -FilePath $PSHOME\powershell.exe -ArgumentList ''-NoProfile -Command Start-Sleep -Seconds 30'' -PassThru; $child.WaitForExit()'
        ) $processSandbox 1 1MB)
    } 'process timeout and tree termination' 'timed out after 1 seconds'
    Assert-Throws {
      [void](Invoke-Captured $windowsPowerShell @(
          '-NoProfile', '-Command', "[Console]::Out.Write(('x' * 4096)); Start-Sleep -Seconds 30"
        ) $processSandbox 10 1024)
    } 'process output-limit termination' 'per-stream byte limit'
    Assert-Throws {
      [void](Invoke-Captured $windowsPowerShell @(
          '-NoProfile', '-Command', "[Console]::Out.Write(('x' * 4096))"
        ) $processSandbox 10 1024)
    } 'fast-exit process output-limit termination' 'per-stream byte limit'
    Remove-Sandbox $processSandbox
    $processSandbox = $null

    $expected = New-Object System.Collections.Generic.List[string]
    for ($index = 1; $index -le 28; ++$index) {
      $expected.Add(('self{0:D2}' -f $index))
    }
    $expected.Add('n23')
    $fakePowerShell = [pscustomobject][ordered]@{
      name = 'WindowsPowerShell'; path = 'C:/selftest/powershell.exe'; sha256 = '3' * 64
    }
    $fakeVcvars = [pscustomobject][ordered]@{
      name = 'vcvarsall'; path = 'C:/selftest/vcvarsall.bat'; sha256 = '4' * 64
    }
    $fakeCl = [pscustomobject][ordered]@{
      name = 'cl'; path = 'C:/selftest/cl.exe'; sha256 = '5' * 64
    }
    $fakeLink = [pscustomobject][ordered]@{
      name = 'link'; path = 'C:/selftest/link.exe'; sha256 = '6' * 64
    }
    $preparation = [pscustomobject][ordered]@{
      prebuild_sha256 = $prebuildHash
      evidence_nonce = $nonce
      tests = $expected.ToArray()
      tools = [pscustomobject][ordered]@{
        windows_powershell = $fakePowerShell
        msvc = [pscustomobject][ordered]@{
          vcvarsall = $fakeVcvars
          cl = $fakeCl
          link = $fakeLink
        }
        build_batch_paths = @(
          [pscustomobject][ordered]@{ stage = 'production'; path = 'C:/selftest/production.bat' },
          [pscustomobject][ordered]@{ stage = 'test'; path = 'C:/selftest/test.bat' }
        )
      }
    }

    $banner = '** Visual Studio 2026 Developer Command Prompt v18.7.3'
    $arch = "[vcvarsall.bat] Environment initialized for: 'x64'"
    $productionCapture = New-SyntheticCapture @(
      $banner, $arch, 'BUILD_OK'
    ) $ProductionBuildTimeoutSeconds
    Write-Capture $DefaultProductionLog 'production-build' $ProductionCommand `
        $productionCapture ([ordered]@{
          target_arch = 'x64'
          language_mode = '/std:c++20'
          toolchain = 'MSVC'
          isolated_localappdata = 'true'
          localappdata_sandbox_kind = 'n23-production-build'
          artifact_sha256 = 'a' * 64
          artifact_preexisting = 'false'
          artifact_created_utc = $productionCapture.started.ToString('o')
          artifact_last_write_utc = $productionCapture.ended.ToString('o')
          compiler_banner_sha256 = Text-Hash $banner
          launcher_executable_path = $fakePowerShell.path
          launcher_executable_sha256 = $fakePowerShell.sha256
          vcvarsall_path = $fakeVcvars.path
          vcvarsall_sha256 = $fakeVcvars.sha256
          cl_path = $fakeCl.path
          cl_sha256 = $fakeCl.sha256
          link_path = $fakeLink.path
          link_sha256 = $fakeLink.sha256
          build_batch_path = 'C:/selftest/production.bat'
          build_batch_sha256 = '7' * 64
        }) $preparation
    $parsedProduction = Parse-Log $DefaultProductionLog
    [void](Assert-NativeLog $parsedProduction 'production-build' $preparation)

    $deltaObject = New-SelfTestAllowedDelta
    $deltaJson = $deltaObject | ConvertTo-Json -Depth 12 -Compress
    $deltaLine = 'N23_ALLOWED_DELTA_JSON=' + $deltaJson
    $expectedSnapshotLabels = @(Get-ExpectedN23SnapshotLabels)
    $testLines = New-Object System.Collections.Generic.List[string]
    $testLines.Add($banner)
    $testLines.Add($arch)
    $testLines.Add('TESTS_BUILD_OK')
    foreach ($name in $expected) { $testLines.Add("[PASS] $name") }
    foreach ($marker in @(
        'N23_ON_THIS_DAY_MATRIX=pass',
        'N23_LAST_SEEN_MATRIX=pass',
        'N23_BACKFILL_MATRIX=pass',
        'N23_REGISTRY_MCP_MATRIX=pass',
        'N23_SOURCE_BRAIN_ISOLATION=pass')) {
      $testLines.Add($marker)
    }
    for ($index = 0; $index -lt $expectedSnapshotLabels.Count; ++$index) {
      $testLines.Add(
        "N23_SNAPSHOT $($index + 1) $(Encode-HexUtf8 $expectedSnapshotLabels[$index]) $('1' * 64) $('1' * 64) $('2' * 64) $('2' * 64)"
      )
    }
    $testLines.Add($deltaLine)
    $testLines.Add("N23_SNAPSHOT_COUNT=$($expectedSnapshotLabels.Count)")
    $testCapture = New-SyntheticCapture $testLines.ToArray() `
        $TestBuildTimeoutSeconds
    Write-Capture $DefaultTestLog 'test-build-suite-run-1' $TestBuildCommand `
        $testCapture ([ordered]@{
          target_arch = 'x64'
          language_mode = '/std:c++20'
          toolchain = 'MSVC'
          isolated_localappdata = 'true'
          localappdata_sandbox_kind = 'n23-test-build-suite-run-1'
          suite_run_index = '1'
          expected_registered_tests = '29'
          n23_focused_registered_tests = '1'
          integrated_regression_tests = '28'
          production_binary_sha256 = 'a' * 64
          test_binary_sha256 = 'b' * 64
          test_binary_preexisting = 'false'
          test_binary_created_utc = $testCapture.started.ToString('o')
          test_binary_last_write_utc = $testCapture.ended.ToString('o')
          compiler_banner_sha256 = Text-Hash $banner
          launcher_executable_path = $fakePowerShell.path
          launcher_executable_sha256 = $fakePowerShell.sha256
          vcvarsall_path = $fakeVcvars.path
          vcvarsall_sha256 = $fakeVcvars.sha256
          cl_path = $fakeCl.path
          cl_sha256 = $fakeCl.sha256
          link_path = $fakeLink.path
          link_sha256 = $fakeLink.sha256
          build_batch_path = 'C:/selftest/test.bat'
          build_batch_sha256 = '8' * 64
        }) $preparation
    $parsedRun1 = Parse-Log $DefaultTestLog
    [void](Assert-NativeLog $parsedRun1 'test-build-suite-run-1' $preparation)
    $run1 = Assert-TestRun $parsedRun1 $expected.ToArray() 'self-test suite run one'

    $run2Lines = @($testLines | Where-Object {
        [string]$_ -cne $banner -and [string]$_ -cne $arch -and
        [string]$_ -cne 'TESTS_BUILD_OK'
      })
    $run2Capture = New-SyntheticCapture $run2Lines $SuiteTimeoutSeconds
    Write-Capture $DefaultSecondLog 'full-suite-run-2' $SecondSuiteCommand `
        $run2Capture ([ordered]@{
          target_arch = 'x64'
          language_mode = '/std:c++20'
          toolchain = 'MSVC'
          isolated_localappdata = 'true'
          localappdata_sandbox_kind = 'n23-full-suite-run-2'
          suite_run_index = '2'
          expected_registered_tests = '29'
          n23_focused_registered_tests = '1'
          integrated_regression_tests = '28'
          test_binary_sha256 = 'b' * 64
          launcher_executable_path = Relative-Path $Tests
          launcher_executable_sha256 = 'b' * 64
        }) $preparation
    $parsedRun2 = Parse-Log $DefaultSecondLog
    [void](Assert-NativeLog $parsedRun2 'full-suite-run-2' $preparation)
    $run2 = Assert-TestRun $parsedRun2 $expected.ToArray() 'self-test suite run two'
    Require ($run1.normalized_marker_sha256 -ceq
             $run2.normalized_marker_sha256) 'self-test suite marker semantics differ'

    $doctorJson = [pscustomobject][ordered]@{
      ok = $true
      overall = 'OK'
      schema_version = 12
      stats = [pscustomobject][ordered]@{
        pages = 0
        chunks = 0
        links = 0
        embedded_chunks = 0
      }
      checks = @(
        [pscustomobject]@{ name = 'database'; status = 'OK' },
        [pscustomobject]@{ name = 'schema'; status = 'OK' },
        [pscustomobject]@{ name = 'critical_tables'; status = 'OK' },
        [pscustomobject]@{ name = 'optional'; status = 'OK' }
      )
      notes = @()
    } | ConvertTo-Json -Depth 8 -Compress
    $doctorCapture = [pscustomobject][ordered]@{
      started = [DateTimeOffset]::UtcNow
      ended = [DateTimeOffset]::UtcNow.AddSeconds(1)
      exit_code = 0
      stdout = $doctorJson
      stderr = ''
      child_environment_closed = $true
      process_job_assigned = $true
      environment_sha256 = 'e' * 64
      production_data_access_telemetry = 'not-collected'
      git_mutation_telemetry = 'not-collected'
      protected_configuration_telemetry = 'not-collected'
    }
    [void](Parse-DoctorCapture $doctorCapture 'self-test doctor')

    $badLog = Join-Path $temporaryRoot 'BAD-LOG.txt'
    $validLines = @(Read-BoundedUtf8Lines $DefaultProductionLog $temporaryRoot `
        $MaxImportedLogBytes 'self-test valid log')
    Write-AtomicUtf8Lines $badLog (@($validLines[0], $validLines[0]) +
                                   @($validLines | Select-Object -Skip 1)) `
        $temporaryRoot
    Assert-Throws { [void](Parse-Log $badLog) } 'duplicate log-header parser'
    $badRunNonceLog = Join-Path $temporaryRoot 'BAD-RUN-NONCE.txt'
    $tamperedLines = @($validLines | ForEach-Object {
        if ([string]$_ -match '^verification_run_nonce=') {
          'verification_run_nonce=' + ('0' * 32)
        } else {
          [string]$_
        }
      })
    Write-AtomicUtf8Lines $badRunNonceLog $tamperedLines $temporaryRoot
    $parsedBadRunNonce = Parse-Log $badRunNonceLog
    Assert-Throws {
      [void](Assert-NativeLog $parsedBadRunNonce 'production-build' $preparation)
    } 'current Verify nonce binding'
    Assert-Throws {
      [void](Parse-SnapshotLine (
        "N23_SNAPSHOT 1 0 $('1' * 64) $('1' * 64) $('2' * 64) $('2' * 64)"
      ) 1 'bad snapshot')
    } 'snapshot-label parser'
    $badDeltaLine = $deltaLine.Replace('"tagged":2', '"tagged":1')
    Require ($badDeltaLine -cne $deltaLine) 'self-test could not form a bad delta'
    Assert-Throws {
      [void](Parse-AllowedDeltaLine $badDeltaLine 'bad delta')
    } 'allowed-delta parser'

    $missingLabels = @($expectedSnapshotLabels | Select-Object -First (
        $expectedSnapshotLabels.Count - 1
      ))
    Assert-Throws {
      Assert-N23SnapshotLabelContract $missingLabels 'missing-label self-test'
    } 'missing snapshot label contract'

    $extraLabels = @($expectedSnapshotLabels + 'self-test:extra')
    Assert-Throws {
      Assert-N23SnapshotLabelContract $extraLabels 'extra-label self-test'
    } 'extra snapshot label contract'

    $duplicateLabels = @($expectedSnapshotLabels)
    $duplicateLabels[1] = $duplicateLabels[0]
    Assert-Throws {
      Assert-N23SnapshotLabelContract $duplicateLabels 'duplicate-label self-test'
    } 'duplicate snapshot label contract'

    $reorderedLabels = @($expectedSnapshotLabels)
    $firstLabel = $reorderedLabels[0]
    $reorderedLabels[0] = $reorderedLabels[1]
    $reorderedLabels[1] = $firstLabel
    Assert-Throws {
      Assert-N23SnapshotLabelContract $reorderedLabels 'reordered-label self-test'
    } 'reordered snapshot label contract'

    $renamedLabels = @($expectedSnapshotLabels)
    $renamedLabels[0] = $renamedLabels[0] + ':renamed'
    Assert-Throws {
      Assert-N23SnapshotLabelContract $renamedLabels 'renamed-label self-test'
    } 'renamed snapshot label contract'

    Write-AtomicUtf8Lines $pendingReport @('# final self-test report') `
        $temporaryRoot
    Assert-Throws {
      Write-AtomicUtf8Text $outsideFile '{"state":"final"}' $temporaryRoot
    } 'final manifest publication confinement'
    Write-Pending $prepared $prebuildHash $nonce $pendingReport $pendingPath `
        $temporaryRoot
    [void](Read-PendingManifest $pendingPath $prebuild $prepared $nonce `
        $temporaryRoot)
    $recoveredReport = Read-BoundedUtf8Text $pendingReport $temporaryRoot `
        64KB 'recovered pending report'
    Require ($recoveredReport -match
             'State: pending native evidence and fresh Claude Code outcome audit[.]') `
        'failed final publication did not restore the pending report'
  } finally {
    $script:DefaultProductionLog = $oldProductionLog
    $script:DefaultTestLog = $oldTestLog
    $script:DefaultSecondLog = $oldSecondLog
    $script:ActiveLogRoot = $oldActiveLogRoot
    $script:VerificationRunNonce = $oldVerificationRunNonce
    if ($null -ne $processSandbox) { Remove-Sandbox $processSandbox }
    if (-not [string]::IsNullOrWhiteSpace([string]$junctionPath)) {
      $junction = Get-LiteralItemOrNull $junctionPath
      if ($null -ne $junction) {
        Require (($junction.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) `
            'self-test cleanup refused a non-reparse junction path'
        [IO.Directory]::Delete($junctionPath, $false)
      }
    }
    Remove-SafeTree $temporaryRoot $systemTempRoot 'self-test root'
    Remove-SafeTree $outsideRoot $systemTempRoot 'self-test outside root'
  }
  Write-Host "N23_PARSER_SELFTEST_OK host_major=$($PSVersionTable.PSVersion.Major)"
}

try {
  Enter-EvidenceBuildMutex
  if ($ParserSelfTest) {
    Invoke-ParserSelfTest
    exit 0
  }
  if ($Prepare) {
    New-Preparation
    exit 0
  }
  Complete-Verification
  exit 0
} catch {
  $message = $_.Exception.Message
  if (-not [string]::IsNullOrWhiteSpace([string]$_.ScriptStackTrace)) {
    $message += "; stack: $($_.ScriptStackTrace)"
  }
  if ($script:EvidenceInitialized -and
      -not [string]::IsNullOrWhiteSpace([string]$script:PendingPreparedUtc) -and
      -not [string]::IsNullOrWhiteSpace([string]$script:PendingPrebuildHash) -and
      -not [string]::IsNullOrWhiteSpace([string]$script:PendingEvidenceNonce)) {
    try {
      Write-Pending ([string]$script:PendingPreparedUtc) `
                    ([string]$script:PendingPrebuildHash) `
                    ([string]$script:PendingEvidenceNonce)
    } catch {
      $message += "; pending-state recovery failed: $($_.Exception.Message)"
    }
  }
  Write-Error $message
  exit 1
} finally {
  Exit-EvidenceBuildMutex
}
