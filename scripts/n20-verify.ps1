# N20 evidence verifier. It records factual evidence only and never supplies a
# Claude Code audit verdict, changes node status, or updates the parity ledger.
[CmdletBinding(DefaultParameterSetName = 'Verify')]
param(
  [Parameter(Mandatory = $true, ParameterSetName = 'Prepare')]
  [switch]$Prepare,

  [Parameter(ParameterSetName = 'Verify')]
  [switch]$RunBuilds,

  [Parameter(ParameterSetName = 'Verify')]
  [ValidateRange(60, 7200)]
  [int]$TimeoutSeconds = 3600,

  [Parameter(Mandatory = $true, ParameterSetName = 'ParserSelfTest')]
  [switch]$ParserSelfTest
)

$ErrorActionPreference = 'Stop'
if (Test-Path Variable:PSNativeCommandUseErrorActionPreference) {
  $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Split-Path -Parent $PSScriptRoot
$EvidenceDir = Join-Path $Root 'docs\nodes\n20-evidence'
$GatePath = Join-Path $EvidenceDir 'PRE-IMPLEMENTATION-GATE.json'
$PrebuildManifestPath = Join-Path $EvidenceDir 'PREBUILD-MANIFEST.json'
$EvidenceManifestPath = Join-Path $EvidenceDir 'EVIDENCE-MANIFEST.json'
$ReportPath = Join-Path $EvidenceDir 'VERIFY-REPORT.md'
$ProductionBuildLog = Join-Path $EvidenceDir 'PRODUCTION-BUILD-OUTPUT.txt'
$TestBuildAndFirstSuiteLog = Join-Path $EvidenceDir 'TEST-BUILD-AND-SUITE-RUN-1.txt'
$SecondSuiteLog = Join-Path $EvidenceDir 'FULL-SUITE-RUN-2.txt'
$PlanPath = Join-Path $Root 'docs\nodes\N20-PLAN.md'
$PlanAuditPath = Join-Path $Root 'docs\nodes\N20-PLAN-AUDIT.md'
$Qbrain = Join-Path $Root 'build\cl\qbrain.exe'
$Tests = Join-Path $Root 'build\cl\qbrain_tests.exe'
$BuildScript = Join-Path $Root 'scripts\build-cl.ps1'
$TestBuildScript = Join-Path $Root 'scripts\build-tests-cl.ps1'
$ExpectedGitDirectory = [IO.Path]::GetFullPath((Join-Path $Root '.git'))
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Utf8Strict = New-Object System.Text.UTF8Encoding($false, $true)
$ChildEnvironmentPolicy = 'n20-fail-closed-v1'
$N20EvidenceBuildMutexName = 'Global\Qbrain.N20.Verifier.EvidenceBuild.v1'
$N20ProcessOutputLimitBytes = 32MB
$N20ProcessDrainTimeoutSeconds = 5
$N20SandboxMaxEntries = 4096
$N20SandboxMaxDepth = 32
$N20SandboxMaxFileBytes = 64MB
$N20SandboxMaxPathLength = 32767
$AllowedChildEnvironmentOverrides = @(
  'LOCALAPPDATA',
  'QBRAIN_SOURCE',
  'QBRAIN_MCP_ALLOW_WRITE'
)
$script:N20FinalManifestPublished = $false
$script:N20VerificationStarted = $false
$script:N20EvidenceBuildMutex = $null
$script:N20EvidenceBuildMutexHeld = $false

$GateExpected = [pscustomobject][ordered]@{
  file_sha256 = 'db60b533ea14e99fdcd8971ee7bd13b30d8119c3de3953516c9777641ceecd16'
  approved_plan_sha256 = '147cc7d073d70d31184797cc7180f92cd8fa83d1ac134affd172dc4bbba77e11'
  audited_draft_plan_sha256 = 'd6600297081e983876894d9da893f2ff5769c518e74fb72483687eaae1f02787'
  plan_audit_sha256 = 'c3296b662fdd8e667de1e94bfa5838f141b3a4c249e0e2128e532bed5d506d66'
  qbrain_sha256 = '0d5c63f98713268ba76452105f743c1597c25166fba01ea215924778e15c0bd6'
  baseline_manifest_sha256 = '3bafa9ba27d6271f21f761f1d5639c13662449a6f4edfd64c62a627a2b991c7e'
  baseline_git_fingerprint_sha256 = 'fb5efb1a1da069f9343c02f98cf2949eb0a91f602444f23807eef8b03d93d3b6'
  git_head = '5ced8ccb511672536d0f9767a2bc1777baf561ab'
}

$GateExecutionPath = @(
  'qbrain doctor',
  'Brain::health',
  'storage::check_schema_integrity'
)

$GateBaselinePaths = @(
  'CMakeLists.txt',
  'docs/nodes/N20-HARD-AUDIT.md',
  'docs/OPS-PARITY-LEDGER.md',
  'include/qbrain/core/brain.hpp',
  'include/qbrain/mcp/server.hpp',
  'include/qbrain/ops/registry.hpp',
  'include/qbrain/schema/packs.hpp',
  'include/qbrain/storage/database.hpp',
  'include/qbrain/storage/schema_sql.hpp',
  'include/qbrain/util/paths.hpp',
  'scripts/build-cl.ps1',
  'scripts/build-tests-cl.ps1',
  'src/qbrain/core/brain.cpp',
  'src/qbrain/mcp/server.cpp',
  'src/qbrain/ops/handlers.cpp',
  'src/qbrain/ops/registry.cpp',
  'src/qbrain/schema/packs.cpp',
  'src/qbrain/storage/database.cpp',
  'src/qbrain/storage/migrate.cpp',
  'src/qbrain/util/paths.cpp',
  'tests/test_main.cpp',
  'tests/test_mcp.cpp',
  'tests/test_n20_23.cpp',
  'tests/test_storage.cpp',
  'tests/wave3_test_support.hpp',
  'third_party/nlohmann/json.hpp',
  'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.c',
  'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.h'
)

$ExpectedChangedBaselinePaths = @(
  'CMakeLists.txt',
  'include/qbrain/schema/packs.hpp',
  'scripts/build-tests-cl.ps1',
  'src/qbrain/mcp/server.cpp',
  'src/qbrain/ops/handlers.cpp',
  'src/qbrain/ops/registry.cpp',
  'src/qbrain/schema/packs.cpp',
  'tests/test_main.cpp'
)

# Independent N23 implementation changes to shared baseline inputs are
# recorded separately and are not attributed to N20 deliverables.
$ExpectedConcurrentBaselinePaths = @(
  'include/qbrain/core/brain.hpp',
  'src/qbrain/core/brain.cpp',
  'tests/test_n20_23.cpp'
)

$GateAbsentPlannedPaths = @(
  'docs/nodes/n20-evidence/VERIFY-REPORT.md',
  'scripts/n20-verify.ps1',
  'tests/test_n20.cpp'
)

$ExpectedNewImplementationPaths = @(
  'scripts/n20-verify.ps1',
  'tests/test_n20.cpp'
)

$VerifierOutputPaths = @(
  'docs/nodes/n20-evidence/PREBUILD-MANIFEST.json',
  'docs/nodes/n20-evidence/EVIDENCE-MANIFEST.json',
  'docs/nodes/n20-evidence/VERIFY-REPORT.md',
  'docs/nodes/n20-evidence/PRODUCTION-BUILD-OUTPUT.txt',
  'docs/nodes/n20-evidence/TEST-BUILD-AND-SUITE-RUN-1.txt',
  'docs/nodes/n20-evidence/FULL-SUITE-RUN-2.txt'
)

$N20DeliverablePaths = @(
  'include/qbrain/schema/packs.hpp',
  'src/qbrain/schema/packs.cpp',
  'src/qbrain/ops/handlers.cpp',
  'src/qbrain/mcp/server.cpp',
  'tests/test_n20.cpp',
  'tests/test_main.cpp',
  'CMakeLists.txt',
  'scripts/build-tests-cl.ps1',
  'scripts/n20-verify.ps1',
  'docs/nodes/N20-PLAN.md',
  'docs/nodes/N20-PLAN-AUDIT.md',
  'docs/nodes/n20-evidence/PRE-IMPLEMENTATION-GATE.json'
)

$DependencyContracts = @(
  [pscustomobject]@{ Node='N1';   Plan='9fd6df77ad905463f34e6873c2220849003679a64c869e5fb1eaffba470f95e6'; Outcome='93f112c13d01864aa701683e2a4dbb3726a763d90b7a113c07dc543af4d31141' },
  [pscustomobject]@{ Node='N2';   Plan='c34fede88989a9847dd3cad0bf719b6476c28bbfb124cb094d4afbe24d90fb85'; Outcome='e9dc809dcdb73c0757708f81d53daf2fc89394c12cf953e86c0e9de5923a3413' },
  [pscustomobject]@{ Node='N2.5'; Plan='bd0cf1b5f4dddb9af40168a89d1a87be84d5a4eb2f99872d3389880523617953'; Outcome='dd6e404ab7583af8c6cbecd86179baba3401a1d5ef10f559b2067229a208c8ff' },
  [pscustomobject]@{ Node='N7';   Plan='929970318d8fb3043371f82a9208360db7e38e6dd058e37f0eef515534f26d39'; Outcome='307226705f0dc7495b0aa7aeebf88bd807c0216c19cab059cd23d01dd6835421' },
  [pscustomobject]@{ Node='N8';   Plan='7f16263f786315420ed42a7c79350add553ad84b11ce4cd6dbc21b0fdc320570'; Outcome='7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9' },
  [pscustomobject]@{ Node='N11';  Plan='e157d9f3b6dcbc276b782d960c237d50fed9d4ff5614473678813e27541844a7'; Outcome='bdefcf26d138b658d31df0b8525c46b776aa5e9086796bcd16696d8b783f2012' },
  [pscustomobject]@{ Node='N15';  Plan='01e95a0cc55e4d0580562008a65de2ee941a13a8b37f4fd730389937d5abaef1'; Outcome='9f5f14ab7ed2cf4da50b597f8f861061948d9b65331091a017d677f7b4968c59' },
  [pscustomobject]@{ Node='N18';  Plan='87db9821c255555ab6a42aab8d22cac945a5e0141aeeb3dd02e76a07e743af6d'; Outcome='f09971ecf44ab66129f33ee3b7dad91515aac39d6d330b725916983fcb408053' },
  [pscustomobject]@{ Node='N19';  Plan='e5c603efbfecb5603a0fd068dd2a0b39e7a75abac5fd116634adc397d9b7e470'; Outcome='d4ee4ad14e3768b5470865f092a783ba0d10b9e9155bfb17c4bd5ce594ad4f24' }
)

$CompletedSuiteBaseline = 26
$RequiredMinimumRegisteredTests = $CompletedSuiteBaseline + 1
$ExpectedN20SnapshotLabelCount = 357
$ExpectedN20SnapshotLabelsHash = '3001b8f6f083c884250a4b63f4a50cd14933dd40ffb60017e94386d163cc57d7'

function Require([bool]$Condition, [string]$Message) {
  if (-not $Condition) {
    throw "N20 evidence requirement failed: $Message"
  }
}

function Enter-N20EvidenceBuildCriticalSection {
  if ($script:N20EvidenceBuildMutexHeld) { return }
  $createdNew = $false
  $mutex = $null
  try {
    $mutex = New-Object System.Threading.Mutex($false, $N20EvidenceBuildMutexName, [ref]$createdNew)
    $waitMilliseconds = [Math]::Min(
        [int64]$TimeoutSeconds * 1000,
        [int64][int32]::MaxValue)
    $acquired = $false
    try {
      $acquired = $mutex.WaitOne([TimeSpan]::FromMilliseconds($waitMilliseconds))
    } catch [System.Threading.AbandonedMutexException] {
      # An abandoned verifier still owns the critical section boundary. The
      # mutex has been transferred to this process, so continue fail-closed.
      $acquired = $true
    }
    Require $acquired "named evidence/build mutex was not acquired within $TimeoutSeconds seconds"
    $script:N20EvidenceBuildMutex = $mutex
    $script:N20EvidenceBuildMutexHeld = $true
    Write-Verbose "N20 evidence/build critical section acquired: $N20EvidenceBuildMutexName"
  } catch {
    if ($null -ne $mutex) { $mutex.Dispose() }
    throw
  }
}

function Exit-N20EvidenceBuildCriticalSection {
  $mutex = $script:N20EvidenceBuildMutex
  $script:N20EvidenceBuildMutex = $null
  $held = $script:N20EvidenceBuildMutexHeld
  $script:N20EvidenceBuildMutexHeld = $false
  if ($null -eq $mutex) { return }
  try {
    if ($held) { $mutex.ReleaseMutex() }
  } finally {
    $mutex.Dispose()
  }
}

function Invoke-N20EvidenceBuildCriticalSection([scriptblock]$Action) {
  Require ($null -ne $Action) 'evidence/build critical-section action is missing'
  Enter-N20EvidenceBuildCriticalSection
  try {
    & $Action
  } finally {
    Exit-N20EvidenceBuildCriticalSection
  }
}

function Assert-EvidenceDirectory {
  $directories = @(
    [IO.Path]::GetFullPath($Root),
    [IO.Path]::GetFullPath((Join-Path $Root 'docs')),
    [IO.Path]::GetFullPath((Join-Path $Root 'docs\nodes'))
  )
  foreach ($directory in $directories) {
    Require (Test-Path -LiteralPath $directory -PathType Container) `
        "evidence ancestor is missing: $directory"
    $item = Get-Item -LiteralPath $directory -Force
    Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
        "evidence ancestor is a reparse point: $directory"
  }
  if (-not (Test-Path -LiteralPath $EvidenceDir)) {
    New-Item -ItemType Directory -Path $EvidenceDir | Out-Null
  }
  $evidenceItem = Get-Item -LiteralPath $EvidenceDir -Force
  Require ($evidenceItem.PSIsContainer) 'N20 evidence path is not a directory'
  Require (($evidenceItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
      'N20 evidence directory is a reparse point'
}

function Assert-SafeEvidenceWritePath([string]$Path) {
  $full = [IO.Path]::GetFullPath($Path)
  $evidenceFull = [IO.Path]::GetFullPath($EvidenceDir).TrimEnd('\')
  Require ($full.StartsWith($evidenceFull + '\',
                            [StringComparison]::OrdinalIgnoreCase)) `
      'N20 evidence output is outside the evidence directory'
  Assert-EvidenceDirectory
  Require ((Split-Path -Parent $full).Equals(
      $evidenceFull, [StringComparison]::OrdinalIgnoreCase)) `
      'N20 evidence output is not a direct child of the evidence directory'
  if (Test-Path -LiteralPath $full) {
    $item = Get-Item -LiteralPath $full -Force
    Require (-not $item.PSIsContainer) 'N20 evidence output is a directory'
    Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
        'N20 evidence output is a reparse point'
  }
}

function Write-Utf8Text([string]$Path, [string]$Text) {
  Assert-SafeEvidenceWritePath $Path
  $temporary = Join-Path $EvidenceDir ('.n20-write-' + [Guid]::NewGuid().ToString('N') + '.tmp')
  Assert-SafeEvidenceWritePath $temporary
  try {
    [IO.File]::WriteAllText($temporary, $Text, $Utf8NoBom)
    if (Test-Path -LiteralPath $Path) {
      [IO.File]::Replace($temporary, $Path, $null)
    } else {
      [IO.File]::Move($temporary, $Path)
    }
  } finally {
    if (Test-Path -LiteralPath $temporary) {
      Remove-Item -LiteralPath $temporary -Force
    }
  }
}

function Write-Utf8Lines([string]$Path, [object[]]$Lines) {
  Write-Utf8Text $Path ((@($Lines) -join [Environment]::NewLine) + [Environment]::NewLine)
}

function File-Hash([string]$Path) {
  Assert-PlainPathChain $Path $true
  Require (Test-Path -LiteralPath $Path -PathType Leaf) "missing file: $Path"
  (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
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

function Test-N20NameEqualsAny([string]$Name, [string[]]$Candidates) {
  foreach ($candidate in @($Candidates)) {
    if ($Name.Equals($candidate, [StringComparison]::OrdinalIgnoreCase)) {
      return $true
    }
  }
  $false
}

function Test-BlockedChildEnvironmentName([string]$Name) {
  if ([string]::IsNullOrWhiteSpace($Name)) { return $true }
  if ($Name -imatch '^(?:QBRAIN_|GIT_CONFIG_)') { return $true }
  if ($Name -imatch '^GIT_(?:DIR|WORK_TREE|COMMON_DIR|INDEX_FILE|OBJECT_DIRECTORY|ALTERNATE_OBJECT_DIRECTORIES|CEILING_DIRECTORIES|DISCOVERY_ACROSS_FILESYSTEM|NAMESPACE)$') {
    return $true
  }
  if ($Name -imatch '^(?:OPENAI|ANTHROPIC|AZURE_OPENAI|GOOGLE|GEMINI|MISTRAL|COHERE|AWS|GCP|OLLAMA|OPENROUTER|DEEPSEEK|XAI|GROQ|HUGGINGFACE|HF|LLM)_') {
    return $true
  }
  if ($Name -imatch '(?:^|_)(?:API_?KEY|TOKEN|SECRET|PASSWORD|PASSPHRASE|CREDENTIALS?)(?:_|$)') {
    return $true
  }
  if (Test-N20NameEqualsAny $Name @(
      'HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY', 'NO_PROXY',
      'SSH_AUTH_SOCK', 'GIT_ASKPASS', 'GIT_SSH_COMMAND',
      'MODEL', 'PROVIDER', 'BASE_URL', 'REASONING_EFFORT',
      'CONTEXT_SIZE', 'COMPRESSION_THRESHOLD')) {
    return $true
  }
  $false
}

function Get-N20ProcessEnvironmentName([string]$Name) {
  $environment = [Environment]::GetEnvironmentVariables(
      [EnvironmentVariableTarget]::Process)
  foreach ($candidate in $environment.Keys) {
    if ([string]$candidate -and
        ([string]$candidate).Equals($Name, [StringComparison]::OrdinalIgnoreCase)) {
      return [string]$candidate
    }
  }
  $null
}

function Test-N20EnvironmentStateContains([object]$State, [string]$Name) {
  foreach ($change in $State.changes) {
    if ([string]$change.name -and
        ([string]$change.name).Equals($Name, [StringComparison]::OrdinalIgnoreCase)) {
      return $true
    }
  }
  $false
}

function Add-N20EnvironmentStateChange([object]$State, [string]$Name) {
  if (Test-N20EnvironmentStateContains $State $Name) { return }
  $existingName = Get-N20ProcessEnvironmentName $Name
  $exists = -not [string]::IsNullOrEmpty($existingName)
  $savedName = if ($exists) { $existingName } else { $Name }
  $savedValue = if ($exists) {
    [Environment]::GetEnvironmentVariable(
        $existingName, [EnvironmentVariableTarget]::Process)
  } else {
    $null
  }
  $State.changes.Add([pscustomobject][ordered]@{
      name = $savedName
      existed = $exists
      value = $savedValue
    }) | Out-Null
}

function Exit-FailClosedChildEnvironment([object]$State) {
  if ($null -eq $State -or $State.restored) { return }
  $firstFailure = $null
  for ($index = $State.changes.Count - 1; $index -ge 0; --$index) {
    $change = $State.changes[$index]
    try {
      $value = if ($change.existed) { [string]$change.value } else { $null }
      [Environment]::SetEnvironmentVariable(
          [string]$change.name, $value, [EnvironmentVariableTarget]::Process)
    } catch {
      if ($null -eq $firstFailure) { $firstFailure = $_.Exception }
    }
  }
  $State.restored = $true
  if ($null -ne $firstFailure) {
    throw "N20 evidence requirement failed: child environment restoration failed: $($firstFailure.Message)"
  }
}

function Enter-FailClosedChildEnvironment(
  [hashtable]$EnvironmentOverrides = @{},
  [string[]]$RemoveEnvironmentVariables = @()
) {
  $state = [pscustomobject]@{
    changes = New-Object System.Collections.Generic.List[object]
    removed_blocked_count = 0
    explicit_override_count = 0
    restored = $false
  }
  try {
    $environment = [Environment]::GetEnvironmentVariables(
        [EnvironmentVariableTarget]::Process)
    foreach ($keyValue in $environment.Keys) {
      $name = [string]$keyValue
      if ((Test-BlockedChildEnvironmentName $name) -or
          (Test-N20NameEqualsAny $name $RemoveEnvironmentVariables)) {
        Add-N20EnvironmentStateChange $state $name
        [Environment]::SetEnvironmentVariable(
            $name, $null, [EnvironmentVariableTarget]::Process)
        $state.removed_blocked_count = [int]$state.removed_blocked_count + 1
      }
    }
    foreach ($keyValue in $EnvironmentOverrides.Keys) {
      $name = [string]$keyValue
      Require (Test-N20NameEqualsAny $name $AllowedChildEnvironmentOverrides) `
          'child environment override is not allowlisted'
      Require ($null -ne $EnvironmentOverrides[$keyValue]) `
          'child environment override value is null'
      Add-N20EnvironmentStateChange $state $name
      [Environment]::SetEnvironmentVariable(
          $name, [string]$EnvironmentOverrides[$keyValue],
          [EnvironmentVariableTarget]::Process)
      $state.explicit_override_count = [int]$state.explicit_override_count + 1
    }
    $state
  } catch {
    $entryFailure = $_.Exception
    try {
      Exit-FailClosedChildEnvironment $state
    } catch {
      throw "N20 evidence requirement failed: child environment entry and restoration failed: $($entryFailure.Message); $($_.Exception.Message)"
    }
    throw $entryFailure
  }
}

function Assert-FailClosedChildEnvironment(
  [Collections.Specialized.StringDictionary]$Environment,
  [hashtable]$EnvironmentOverrides
) {
  $overrideNames = [string[]]@(
    $EnvironmentOverrides.Keys | ForEach-Object { [string]$_ }
  )
  foreach ($keyValue in $Environment.Keys) {
    $name = [string]$keyValue
    if ((Test-BlockedChildEnvironmentName $name) -and
        -not (Test-N20NameEqualsAny $name $overrideNames)) {
      throw 'N20 evidence requirement failed: blocked ambient variable reached a child process'
    }
  }
  foreach ($keyValue in $EnvironmentOverrides.Keys) {
    $name = [string]$keyValue
    Require ($Environment.ContainsKey($name)) `
        'child environment is missing an explicit override'
    Require ([string]$Environment[$name] -ceq [string]$EnvironmentOverrides[$keyValue]) `
        'child environment override value differs'
  }
}

function Assert-N20SandboxPath([string]$Path, [string]$Kind) {
  Require ($Kind -cmatch '^(?:stdio|platform|parser|production|testbuild|suite2)$') `
      'disposable sandbox kind is not allowlisted'
  $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
  Require ($full.Length -le $N20SandboxMaxPathLength) `
      'disposable sandbox path exceeds the Windows path-length bound'
  $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
  Require (Test-Path -LiteralPath $temporaryRoot -PathType Container) `
      'Windows temporary root is missing'
  Assert-PlainPathChain $temporaryRoot
  $temporaryRootItem = Get-Item -LiteralPath $temporaryRoot -Force
  Require (($temporaryRootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
      'Windows temporary root is a reparse point'
  $workspaceRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\')
  Require (-not $temporaryRoot.Equals($workspaceRoot, [StringComparison]::OrdinalIgnoreCase) -and
      -not $temporaryRoot.StartsWith($workspaceRoot + '\', [StringComparison]::OrdinalIgnoreCase)) `
      'Windows temporary root overlaps the verifier workspace'
  $ambientLocalAppData = [Environment]::GetEnvironmentVariable(
      'LOCALAPPDATA', [EnvironmentVariableTarget]::Process)
  if (-not [string]::IsNullOrWhiteSpace($ambientLocalAppData)) {
    $productionRoot = [IO.Path]::GetFullPath((Join-Path $ambientLocalAppData 'Qbrain')).TrimEnd('\')
    Require (-not $temporaryRoot.Equals($productionRoot, [StringComparison]::OrdinalIgnoreCase) -and
        -not $temporaryRoot.StartsWith($productionRoot + '\', [StringComparison]::OrdinalIgnoreCase)) `
        'Windows temporary root overlaps the production Qbrain root'
  }
  $parent = [IO.Path]::GetFullPath((Split-Path -Parent $full)).TrimEnd('\')
  Require ($parent.Equals($temporaryRoot, [StringComparison]::OrdinalIgnoreCase)) `
      'disposable sandbox is not a direct child of the Windows temporary root'
  $expectedName = '^qbrain_n20_' + [regex]::Escape($Kind) + '_[0-9a-f]{32}$'
  Require ([IO.Path]::GetFileName($full) -cmatch $expectedName) `
      'disposable sandbox name is not canonical'
  $full
}

function Assert-N20SandboxItemPath([string]$Path, [string]$Sandbox) {
  $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
  Require ($full.Length -le $N20SandboxMaxPathLength) `
      'disposable sandbox item exceeds the Windows path-length bound'
  $root = [IO.Path]::GetFullPath($Sandbox).TrimEnd('\')
  $kindMatch = [regex]::Match([IO.Path]::GetFileName($root), '^qbrain_n20_(stdio|platform|parser|production|testbuild|suite2)_[0-9a-f]{32}$')
  Require $kindMatch.Success 'disposable sandbox root name is not canonical'
  [void](Assert-N20SandboxPath $root $kindMatch.Groups[1].Value)
  $prefix = $root + '\'
  Require ($full.Equals($root, [StringComparison]::OrdinalIgnoreCase) -or
      $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) `
      'disposable sandbox item escapes its root'
  if (-not $full.Equals($root, [StringComparison]::OrdinalIgnoreCase)) {
    $relative = $full.Substring($root.Length).TrimStart('\')
    Require (-not $relative.Contains(':')) `
        'disposable sandbox item uses alternate-data-stream syntax'
  }
  $full
}

function Write-N20SandboxUtf8Text([string]$Path, [string]$Text, [string]$Sandbox) {
  $full = Assert-N20SandboxItemPath $Path $Sandbox
  $parent = Split-Path -Parent $full
  $parentItem = Get-N20SandboxItemNoReparse $parent $Sandbox
  Require ($parentItem.PSIsContainer) 'sandbox fixture parent is not a directory'
  if (Test-Path -LiteralPath $full) {
    $existing = Get-N20SandboxItemNoReparse $full $Sandbox
    Require (-not $existing.PSIsContainer) 'sandbox fixture target is a directory'
  }
  [IO.File]::WriteAllText($full, $Text, $Utf8NoBom)
}

function Get-N20SandboxItemNoReparse([string]$Path, [string]$Sandbox) {
  $full = Assert-N20SandboxItemPath $Path $Sandbox
  $root = [IO.Path]::GetFullPath($Sandbox).TrimEnd('\')
  Require (Test-Path -LiteralPath $root) 'disposable sandbox root is missing'
  $current = $root
  $currentItem = Get-Item -LiteralPath $current -Force
  Require ($currentItem.PSIsContainer) 'disposable sandbox root is not a directory'
  Require (($currentItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
      'disposable sandbox root is a reparse point'
  if ($full.Equals($root, [StringComparison]::OrdinalIgnoreCase)) {
    return $currentItem
  }
  $relative = $full.Substring($root.Length).TrimStart('\')
  foreach ($component in @($relative -split '\\')) {
    Require (-not [string]::IsNullOrWhiteSpace($component)) `
        'disposable sandbox path contains an empty component'
    $current = Join-Path $current $component
    Require (Test-Path -LiteralPath $current) `
        'disposable sandbox path changed during confinement validation'
    $currentItem = Get-Item -LiteralPath $current -Force
    Require (($currentItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
        'disposable sandbox path contains a reparse point'
  }
  $currentItem
}

function Get-N20SandboxInventory([string]$Path, [string]$Kind) {
  $sandbox = Assert-N20SandboxPath $Path $Kind
  Require (Test-Path -LiteralPath $sandbox -PathType Container) `
      'disposable sandbox root is missing'
  $rootItem = Get-N20SandboxItemNoReparse $sandbox $sandbox
  Require ($rootItem.PSIsContainer) 'disposable sandbox root is not a directory'
  Require (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
      'disposable sandbox root is a reparse point'
  $pending = New-Object System.Collections.Generic.Queue[string]
  $pending.Enqueue($sandbox)
  $entries = New-Object System.Collections.Generic.List[object]
  while ($pending.Count -gt 0) {
    $directory = $pending.Dequeue()
    $directoryItem = Get-N20SandboxItemNoReparse $directory $sandbox
    Require ($directoryItem.PSIsContainer) `
        'disposable sandbox traversal target is not a directory'
    foreach ($listed in Get-ChildItem -LiteralPath $directory -Force) {
      Require ($entries.Count -lt $N20SandboxMaxEntries) `
          "disposable sandbox exceeds the $N20SandboxMaxEntries-entry bound"
      $full = Assert-N20SandboxItemPath $listed.FullName $sandbox
      Require ($full.Length -le $N20SandboxMaxPathLength) `
          'disposable sandbox descendant exceeds the Windows path-length bound'
      $item = Get-N20SandboxItemNoReparse $full $sandbox
      Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
          'disposable sandbox descendant is a reparse point'
      $relative = $full.Substring($sandbox.Length).TrimStart('\')
      $depth = @($relative -split '\\').Count
      Require ($depth -le $N20SandboxMaxDepth) `
          "disposable sandbox exceeds the $N20SandboxMaxDepth-level depth bound"
      $length = [int64]0
      $sha256 = 'directory'
      if (-not $item.PSIsContainer) {
        $length = [int64]$item.Length
        Require ($length -le $N20SandboxMaxFileBytes) `
            "disposable sandbox file exceeds the $N20SandboxMaxFileBytes-byte bound"
        $sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $full).Hash.ToLowerInvariant()
        $afterHashItem = Get-N20SandboxItemNoReparse $full $sandbox
        Require (-not $afterHashItem.PSIsContainer -and
            [int64]$afterHashItem.Length -eq $length -and
            $afterHashItem.LastWriteTimeUtc.ToString('o') -ceq $item.LastWriteTimeUtc.ToString('o')) `
            'disposable sandbox file changed during bounded snapshot hashing'
      }
      $entries.Add([pscustomobject][ordered]@{
          path = $full
          relative_path = $relative.Replace('\', '/')
          is_container = [bool]$item.PSIsContainer
          depth = $depth
          bytes = $length
          sha256 = $sha256
          last_write_utc = $item.LastWriteTimeUtc.ToString('o')
        }) | Out-Null
      if ($item.PSIsContainer) { $pending.Enqueue($full) }
    }
  }
  $entries.ToArray()
}

function Get-N20SandboxSnapshot([string]$Path, [string]$Kind) {
  $sandbox = Assert-N20SandboxPath $Path $Kind
  $inventory = @(Get-N20SandboxInventory $sandbox $Kind | Sort-Object relative_path)
  $rows = New-Object System.Collections.Generic.List[string]
  $totalBytes = [int64]0
  foreach ($entry in $inventory) {
    $totalBytes += [int64]$entry.bytes
    $rows.Add("$($entry.relative_path)`t$([bool]$entry.is_container)`t$([int]$entry.depth)`t$([int64]$entry.bytes)`t$($entry.last_write_utc)`t$($entry.sha256)")
  }
  $serialized = ($rows.ToArray() -join "`n") + "`n"
  [pscustomobject][ordered]@{
    sha256 = Text-Hash $serialized
    entry_count = $inventory.Count
    file_count = @($inventory | Where-Object { -not [bool]$_.is_container }).Count
    total_bytes = $totalBytes
  }
}

function Remove-N20SandboxSafely([string]$Path, [string]$Kind) {
  $sandbox = Assert-N20SandboxPath $Path $Kind
  if (-not (Test-Path -LiteralPath $sandbox)) { return }
  $entries = @(Get-N20SandboxInventory $sandbox $Kind | Sort-Object `
      @{ Expression = { $_.depth }; Descending = $true }, `
      @{ Expression = { $_.path }; Descending = $true })
  foreach ($entry in $entries) {
    $full = Assert-N20SandboxItemPath ([string]$entry.path) $sandbox
    Require (Test-Path -LiteralPath $full) `
        'disposable sandbox entry changed before deletion'
    $fresh = Get-N20SandboxItemNoReparse $full $sandbox
    Require (($fresh.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
        'disposable sandbox entry became a reparse point before deletion'
    Require ([bool]$fresh.PSIsContainer -eq [bool]$entry.is_container) `
        'disposable sandbox entry type changed before deletion'
    if ($fresh.PSIsContainer) {
      Require (@(Get-ChildItem -LiteralPath $full -Force).Count -eq 0) `
          'disposable sandbox directory is not empty before deletion'
    }
    Remove-Item -LiteralPath $full -Force
    Require (-not (Test-Path -LiteralPath $full)) `
        'disposable sandbox entry remained after deletion'
  }
  $freshRoot = Get-N20SandboxItemNoReparse $sandbox $sandbox
  Require (($freshRoot.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
      'disposable sandbox root became a reparse point before deletion'
  Require (@(Get-ChildItem -LiteralPath $sandbox -Force).Count -eq 0) `
      'disposable sandbox root is not empty before deletion'
  Remove-Item -LiteralPath $sandbox -Force
  Require (-not (Test-Path -LiteralPath $sandbox)) `
      'disposable sandbox root remained after deletion'
}

function New-N20Sandbox([string]$Kind) {
  $nonce = [Guid]::NewGuid().ToString('N')
  $sandbox = Assert-N20SandboxPath `
      (Join-Path ([IO.Path]::GetTempPath()) "qbrain_n20_${Kind}_$nonce") $Kind
  Require (-not (Test-Path -LiteralPath $sandbox)) `
      'disposable sandbox unexpectedly already exists'
  $created = $false
  try {
    New-Item -ItemType Directory -Path $sandbox | Out-Null
    $created = $true
    [void](Get-N20SandboxInventory $sandbox $Kind)
    $localAppData = Join-Path $sandbox 'localappdata'
    New-Item -ItemType Directory -Path $localAppData | Out-Null
    [void](Get-N20SandboxInventory $sandbox $Kind)
    [pscustomobject][ordered]@{
      path = $sandbox
      localappdata = [IO.Path]::GetFullPath($localAppData)
      id = [IO.Path]::GetFileName($sandbox)
      kind = $Kind
    }
  } catch {
    $creationFailure = $_.Exception
    if ($created -and (Test-Path -LiteralPath $sandbox)) {
      try { Remove-N20SandboxSafely $sandbox $Kind }
      catch {
        throw "N20 evidence requirement failed: sandbox creation and cleanup failed: $($creationFailure.Message); $($_.Exception.Message)"
      }
    }
    throw $creationFailure
  }
}

function Assert-N20IsolatedLocalAppData([string]$Path, [object]$Sandbox) {
  $sandboxPath = Assert-N20SandboxPath ([string]$Sandbox.path) ([string]$Sandbox.kind)
  $expected = [IO.Path]::GetFullPath((Join-Path $sandboxPath 'localappdata'))
  $actual = [IO.Path]::GetFullPath($Path)
  Require ($actual.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) `
      'child LOCALAPPDATA does not equal its isolated sandbox directory'
  Require (Test-Path -LiteralPath $actual -PathType Container) `
      'child LOCALAPPDATA directory is missing'
  $item = Get-N20SandboxItemNoReparse $actual $sandboxPath
  Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
      'child LOCALAPPDATA is a reparse point'
  [void](Get-N20SandboxInventory $sandboxPath ([string]$Sandbox.kind))
}

function ConvertFrom-JsonPreservingDateStrings([string]$Json) {
  $command = Get-Command ConvertFrom-Json
  if ($command.Parameters.ContainsKey('DateKind')) {
    return $Json | ConvertFrom-Json -DateKind String -ErrorAction Stop
  }
  $Json | ConvertFrom-Json -ErrorAction Stop
}

function Skip-JsonWhitespace([string]$Text, [ref]$Index) {
  while ($Index.Value -lt $Text.Length -and
      ($Text[$Index.Value] -eq ' ' -or $Text[$Index.Value] -eq "`t" -or
       $Text[$Index.Value] -eq "`r" -or $Text[$Index.Value] -eq "`n")) {
    $Index.Value = $Index.Value + 1
  }
}

function Read-JsonStringToken([string]$Text, [ref]$Index) {
  Require ($Index.Value -lt $Text.Length -and $Text[$Index.Value] -eq '"') 'strict JSON scanner expected a string'
  $start = $Index.Value
  $Index.Value = $Index.Value + 1
  while ($Index.Value -lt $Text.Length) {
    $character = $Text[$Index.Value]
    if ($character -eq '"') {
      $Index.Value = $Index.Value + 1
      return $Text.Substring($start, $Index.Value - $start)
    }
    if ($character -eq '\') {
      $Index.Value = $Index.Value + 2
    } else {
      $Index.Value = $Index.Value + 1
    }
  }
  throw 'N20 evidence requirement failed: unterminated JSON string'
}

function Read-JsonValueForDuplicateCheck([string]$Text, [ref]$Index) {
  Skip-JsonWhitespace $Text $Index
  Require ($Index.Value -lt $Text.Length) 'strict JSON scanner reached an unexpected end'
  $character = $Text[$Index.Value]
  if ($character -eq '{') {
    $Index.Value = $Index.Value + 1
    $seen = [Collections.Generic.Dictionary[string, bool]]::new([StringComparer]::Ordinal)
    Skip-JsonWhitespace $Text $Index
    if ($Index.Value -lt $Text.Length -and $Text[$Index.Value] -eq '}') {
      $Index.Value = $Index.Value + 1
      return
    }
    while ($true) {
      Skip-JsonWhitespace $Text $Index
      $keyToken = Read-JsonStringToken $Text $Index
      $key = ConvertFrom-JsonPreservingDateStrings $keyToken
      Require ($key -is [string]) 'strict JSON scanner did not decode an object key as a string'
      Require (-not $seen.ContainsKey($key)) "JSON object contains a duplicate key: $key"
      $seen.Add($key, $true)
      Skip-JsonWhitespace $Text $Index
      Require ($Index.Value -lt $Text.Length -and $Text[$Index.Value] -eq ':') 'strict JSON scanner expected a colon'
      $Index.Value = $Index.Value + 1
      Read-JsonValueForDuplicateCheck $Text $Index
      Skip-JsonWhitespace $Text $Index
      Require ($Index.Value -lt $Text.Length) 'strict JSON scanner reached an unterminated object'
      if ($Text[$Index.Value] -eq '}') {
        $Index.Value = $Index.Value + 1
        return
      }
      Require ($Text[$Index.Value] -eq ',') 'strict JSON scanner expected an object comma'
      $Index.Value = $Index.Value + 1
    }
  }
  if ($character -eq '[') {
    $Index.Value = $Index.Value + 1
    Skip-JsonWhitespace $Text $Index
    if ($Index.Value -lt $Text.Length -and $Text[$Index.Value] -eq ']') {
      $Index.Value = $Index.Value + 1
      return
    }
    while ($true) {
      Read-JsonValueForDuplicateCheck $Text $Index
      Skip-JsonWhitespace $Text $Index
      Require ($Index.Value -lt $Text.Length) 'strict JSON scanner reached an unterminated array'
      if ($Text[$Index.Value] -eq ']') {
        $Index.Value = $Index.Value + 1
        return
      }
      Require ($Text[$Index.Value] -eq ',') 'strict JSON scanner expected an array comma'
      $Index.Value = $Index.Value + 1
    }
  }
  if ($character -eq '"') {
    [void](Read-JsonStringToken $Text $Index)
    return
  }
  $start = $Index.Value
  while ($Index.Value -lt $Text.Length -and
      $Text[$Index.Value] -ne ',' -and $Text[$Index.Value] -ne ']' -and
      $Text[$Index.Value] -ne '}' -and $Text[$Index.Value] -ne ' ' -and
      $Text[$Index.Value] -ne "`t" -and $Text[$Index.Value] -ne "`r" -and
      $Text[$Index.Value] -ne "`n") {
    $Index.Value = $Index.Value + 1
  }
  Require ($Index.Value -gt $start) 'strict JSON scanner found an empty scalar'
}

function Read-StrictJson([string]$Path, [string]$Label) {
  Require (Test-Path -LiteralPath $Path -PathType Leaf) "missing $Label"
  $bytes = [IO.File]::ReadAllBytes($Path)
  Require ($bytes.Length -gt 0) "$Label is empty"
  Require (-not ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) "$Label has a UTF-8 BOM"
  try {
    $text = $Utf8Strict.GetString($bytes)
  } catch {
    throw "N20 evidence requirement failed: $Label is not strict UTF-8"
  }
  Require ($text.IndexOf([char]0) -lt 0) "$Label contains a NUL byte"
  try {
    $parsed = ConvertFrom-JsonPreservingDateStrings $text
  } catch {
    throw "N20 evidence requirement failed: $Label is invalid JSON"
  }
  $index = 0
  $reference = [ref]$index
  Read-JsonValueForDuplicateCheck $text $reference
  Skip-JsonWhitespace $text $reference
  Require ($reference.Value -eq $text.Length) "$Label has trailing JSON data"
  [pscustomobject]@{ value=$parsed; text=$text; bytes=[int64]$bytes.Length }
}

function ConvertFrom-StrictJsonText([string]$Text, [string]$Label) {
  Require (-not [string]::IsNullOrWhiteSpace($Text)) "$Label is empty"
  Require (-not $Text.StartsWith([string][char]0xFEFF, [StringComparison]::Ordinal)) "$Label has a Unicode BOM"
  Require ($Text.IndexOf([char]0) -lt 0) "$Label contains a NUL byte"
  try {
    $parsed = ConvertFrom-JsonPreservingDateStrings $Text
  } catch {
    throw "N20 evidence requirement failed: $Label is invalid JSON"
  }
  $index = 0
  $reference = [ref]$index
  Read-JsonValueForDuplicateCheck $Text $reference
  Skip-JsonWhitespace $Text $reference
  Require ($reference.Value -eq $Text.Length) "$Label has trailing JSON data"
  $parsed
}

function Require-ExactJsonPropertyNames([object]$Value, [string[]]$Expected, [string]$Label) {
  Require ($null -ne $Value) "$Label is null"
  $actual = @($Value.PSObject.Properties.Name | Sort-Object)
  $wanted = @($Expected | Sort-Object)
  Require ($actual.Count -eq $wanted.Count -and (($actual -join "`n") -ceq ($wanted -join "`n"))) "$Label property set is not exact"
}

function Has-JsonProperty([object]$Value, [string]$Name) {
  $null -ne $Value -and $null -ne $Value.PSObject.Properties[$Name]
}

function Require-JsonInteger([object]$Value, [string]$Label) {
  $typeCode = if ($null -eq $Value) { [TypeCode]::Empty } else { [Type]::GetTypeCode($Value.GetType()) }
  $integerTypes = @(
    [TypeCode]::SByte, [TypeCode]::Byte, [TypeCode]::Int16, [TypeCode]::UInt16,
    [TypeCode]::Int32, [TypeCode]::UInt32, [TypeCode]::Int64, [TypeCode]::UInt64
  )
  Require ($integerTypes -contains $typeCode) "$Label is not a JSON integer"
}

function Require-JsonIntegerExact([object]$Value, [int64]$Expected, [string]$Label) {
  Require-JsonInteger $Value $Label
  Require ([int64]$Value -eq $Expected) "$Label is not the expected integer"
}

function Require-JsonBooleanExact([object]$Value, [bool]$Expected, [string]$Label) {
  Require ($Value -is [bool] -and $Value -eq $Expected) "$Label is not the expected JSON boolean"
}

function Require-JsonStringExact([object]$Value, [string]$Expected, [string]$Label) {
  Require ($Value -is [string] -and $Value -ceq $Expected) "$Label is not the expected JSON string"
}

function Parse-UtcTimestamp([object]$Value, [string]$Label) {
  Require ($Value -is [string] -and $Value -match '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,7})?(?:Z|\+00:00)$') "$Label is not an explicit UTC timestamp string"
  try {
    $parsed = [DateTimeOffset]::Parse(
      [string]$Value,
      [Globalization.CultureInfo]::InvariantCulture,
      [Globalization.DateTimeStyles]::RoundtripKind)
  } catch {
    throw "N20 evidence requirement failed: $Label is not a valid timestamp"
  }
  Require ($parsed.Offset -eq [TimeSpan]::Zero) "$Label is not UTC"
  $parsed
}

function Resolve-WorkspacePath([string]$Path) {
  $candidate = $Path
  if (-not [IO.Path]::IsPathRooted($candidate)) {
    $candidate = Join-Path $Root $candidate
  }
  $full = [IO.Path]::GetFullPath($candidate)
  $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
  Require ($full.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) 'path is outside the workspace'
  Assert-PlainPathChain $full
  $full
}

function Assert-PlainPathChain([string]$Path, [bool]$RequireLeaf = $false) {
  $full = [IO.Path]::GetFullPath($Path)
  $current = $full
  $leaf = $true
  while (-not [string]::IsNullOrWhiteSpace($current)) {
    if (Test-Path -LiteralPath $current) {
      $item = Get-Item -LiteralPath $current -Force
      Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
          "path chain contains a reparse point: $current"
      if ($leaf -and $RequireLeaf) {
        Require (-not $item.PSIsContainer) "expected a plain file: $current"
      }
      $leaf = $false
    } elseif ($leaf -and $RequireLeaf) {
      throw "N20 evidence requirement failed: missing plain file: $full"
    }
    $parent = [IO.Path]::GetDirectoryName($current)
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -ceq $current) { break }
    $current = $parent
  }
}

function Assert-NoExcludedN20PathToken([string]$Path, [string]$Label) {
  Require ($Path -notmatch '(?i)(^|[^A-Za-z0-9])N30(?=$|[^A-Za-z0-9])') `
      "$Label contains the excluded coordinator token"
  Require ($Path -notmatch '(?i)(^|[\\/])docs[\\/]nodes[\\/](?:N(?:2[1-9]|[3-9][0-9])|n(?:2[1-9]|[3-9][0-9])-evidence)(?:[-\\/]|$)') `
      "$Label contains a later-node governance/evidence artifact"
}

function Relative-Path([string]$Path) {
  $full = Resolve-WorkspacePath $Path
  $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
  $full.Substring($rootPrefix.Length).Replace('\', '/')
}

function Get-FileEntry([string]$Path, [string]$Role) {
  Assert-NoExcludedN20PathToken $Path "$Role path"
  $full = Resolve-WorkspacePath $Path
  Assert-PlainPathChain $full $true
  $item = Get-Item -LiteralPath $full -Force
  [pscustomobject][ordered]@{
    role = $Role
    path = Relative-Path $full
    sha256 = File-Hash $full
    bytes = [int64]$item.Length
    last_write_utc = $item.LastWriteTimeUtc.ToString('o')
  }
}

function Get-N20ExecutableProvenance([string]$FilePath) {
  Require (-not [string]::IsNullOrWhiteSpace($FilePath)) 'child executable is empty'
  $resolved = $null
  if ([IO.Path]::IsPathRooted($FilePath) -or $FilePath.Contains('\') -or $FilePath.Contains('/')) {
    $resolved = [IO.Path]::GetFullPath($FilePath)
  } else {
    $command = Get-Command -Name $FilePath -CommandType Application -ErrorAction Stop
    $resolved = [string]$command.Source
    if ([string]::IsNullOrWhiteSpace($resolved)) { $resolved = [string]$command.Path }
  }
  Require (-not [string]::IsNullOrWhiteSpace($resolved)) 'child executable could not be resolved'
  Assert-PlainPathChain $resolved $true
  $item = Get-Item -LiteralPath $resolved -Force
  [pscustomobject][ordered]@{
    path = [IO.Path]::GetFullPath($resolved)
    name = [IO.Path]::GetFileName($resolved)
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolved).Hash.ToLowerInvariant()
    bytes = [int64]$item.Length
  }
}

function Stop-N20ProcessTree([Diagnostics.Process]$Process) {
  $result = [pscustomobject][ordered]@{
    attempted = $false
    method = 'not-needed'
    exit_code = -1
    process_id = if ($null -ne $Process) { [int]$Process.Id } else { 0 }
  }
  if ($null -eq $Process) { return $result }
  $result.attempted = $true
  $result.method = 'taskkill-tree'
  try {
    $taskkill = Get-Command -Name 'taskkill.exe' -CommandType Application -ErrorAction Stop
    $killInfo = New-Object System.Diagnostics.ProcessStartInfo
    $killInfo.FileName = [string]$taskkill.Source
    if ([string]::IsNullOrWhiteSpace($killInfo.FileName)) { $killInfo.FileName = [string]$taskkill.Path }
    $killInfo.Arguments = "/PID $($Process.Id) /T /F"
    $killInfo.UseShellExecute = $false
    $killInfo.CreateNoWindow = $true
    $killer = New-Object System.Diagnostics.Process
    try {
      $killer.StartInfo = $killInfo
      [void]$killer.Start()
      [void]$killer.WaitForExit(5000)
      $result.exit_code = [int]$killer.ExitCode
    } finally {
      $killer.Dispose()
    }
  } catch {
    $result.method = 'process-kill-fallback'
  }
  try {
    if (-not $Process.HasExited) {
      $Process.Kill()
      $result.method = 'process-kill-fallback'
      [void]$Process.WaitForExit(5000)
    }
  } catch {}
  $result
}

function Invoke-CapturedProcess(
  [string]$FilePath,
  [string]$Arguments,
  [int]$TimeoutSeconds = 60,
  [bool]$SanitizeGitConfiguration = $false,
  [AllowNull()][string]$StandardInput = $null,
  [hashtable]$EnvironmentOverrides = @{},
  [string[]]$RemoveEnvironmentVariables = @()
) {
  $environmentState = $null
  $stateToRestore = $null
  $process = $null
  $processStarted = $false
  $provenance = Get-N20ExecutableProvenance $FilePath
  try {
    $environmentState = Enter-FailClosedChildEnvironment `
        $EnvironmentOverrides $RemoveEnvironmentVariables
    try {
      $startInfo = New-Object System.Diagnostics.ProcessStartInfo
      $startInfo.FileName = [string]$provenance.path
      $startInfo.Arguments = $Arguments
      $startInfo.WorkingDirectory = $Root
      $startInfo.UseShellExecute = $false
      $startInfo.CreateNoWindow = $true
      $startInfo.RedirectStandardOutput = $true
      $startInfo.RedirectStandardError = $true
      $startInfo.RedirectStandardInput = $null -ne $StandardInput
      $childEnvironmentNames = [string[]]@(
        $startInfo.EnvironmentVariables.Keys | ForEach-Object { [string]$_ }
      )
      foreach ($name in $childEnvironmentNames) {
        if ((Test-BlockedChildEnvironmentName $name) -or
            (Test-N20NameEqualsAny $name $RemoveEnvironmentVariables)) {
          [void]$startInfo.EnvironmentVariables.Remove($name)
        }
      }
      foreach ($keyValue in $EnvironmentOverrides.Keys) {
        $startInfo.EnvironmentVariables[[string]$keyValue] =
            [string]$EnvironmentOverrides[$keyValue]
      }
      Assert-FailClosedChildEnvironment `
          $startInfo.EnvironmentVariables $EnvironmentOverrides
      if ($SanitizeGitConfiguration) {
        foreach ($keyValue in $startInfo.EnvironmentVariables.Keys) {
          Require (-not ([string]$keyValue -imatch '^GIT_CONFIG_')) `
              'Git child inherited ambient GIT_CONFIG state'
        }
      }
      $process = New-Object System.Diagnostics.Process
      $process.StartInfo = $startInfo
      $startedUtc = [DateTimeOffset]::UtcNow
      Require ([bool]$process.Start()) 'child process did not start'
      $processStarted = $true
    } finally {
      $stateToRestore = $environmentState
      $environmentState = $null
      Exit-FailClosedChildEnvironment $stateToRestore
    }

    $stdoutState = [pscustomobject]@{
      reader = $process.StandardOutput
      buffer = New-Object char[] 4096
      task = $null
      builder = New-Object System.Text.StringBuilder
      bytes = [int64]0
      done = $false
      limit_exceeded = $false
    }
    $stderrState = [pscustomobject]@{
      reader = $process.StandardError
      buffer = New-Object char[] 4096
      task = $null
      builder = New-Object System.Text.StringBuilder
      bytes = [int64]0
      done = $false
      limit_exceeded = $false
    }
    $stdoutState.task = $stdoutState.reader.ReadAsync(
        $stdoutState.buffer, 0, $stdoutState.buffer.Length)
    $stderrState.task = $stderrState.reader.ReadAsync(
        $stderrState.buffer, 0, $stderrState.buffer.Length)
    if ($null -ne $StandardInput) {
      $process.StandardInput.Write($StandardInput)
      $process.StandardInput.Close()
    }
    $consumeStream = {
      param([object]$State)
      if (-not $State.task.IsCompleted) { return $false }
      $count = $State.task.GetAwaiter().GetResult()
      if ($count -eq 0) {
        $State.done = $true
        return $true
      }
      $State.bytes += [Text.Encoding]::UTF8.GetByteCount(
          $State.buffer, 0, $count)
      if ($State.bytes -gt $N20ProcessOutputLimitBytes) {
        $State.limit_exceeded = $true
        return $true
      }
      [void]$State.builder.Append($State.buffer, 0, $count)
      $State.task = $State.reader.ReadAsync(
          $State.buffer, 0, $State.buffer.Length)
      $true
    }
    $timedOut = $false
    $outputLimitExceeded = $false
    $treeTermination = [pscustomobject][ordered]@{
      attempted = $false; method = 'not-needed'; exit_code = -1; process_id = [int]$process.Id
    }
    while ($true) {
      $progress = [bool](& $consumeStream $stdoutState)
      $progress = ([bool](& $consumeStream $stderrState)) -or $progress
      if ($stdoutState.limit_exceeded -or $stderrState.limit_exceeded) {
        $outputLimitExceeded = $true
        break
      }
      if ($process.HasExited -and $stdoutState.done -and $stderrState.done) { break }
      if (([DateTimeOffset]::UtcNow - $startedUtc).TotalSeconds -ge $TimeoutSeconds) {
        $timedOut = $true
        break
      }
      if (-not $progress) { Start-Sleep -Milliseconds 10 }
    }
    if ($timedOut -or $outputLimitExceeded) {
      $treeTermination = Stop-N20ProcessTree $process
      $drainDeadline = [DateTimeOffset]::UtcNow.AddSeconds($N20ProcessDrainTimeoutSeconds)
      while ((-not $stdoutState.done -or -not $stderrState.done) -and
          [DateTimeOffset]::UtcNow -lt $drainDeadline) {
        [void](& $consumeStream $stdoutState)
        [void](& $consumeStream $stderrState)
        if (-not $stdoutState.done -or -not $stderrState.done) {
          Start-Sleep -Milliseconds 10
        }
      }
      Require ($stdoutState.done -and $stderrState.done) `
          'child output streams did not close after whole-tree termination'
      if ($outputLimitExceeded) {
        throw "N20 evidence requirement failed: child process output exceeds the $N20ProcessOutputLimitBytes-byte limit: $FilePath"
      }
      throw "N20 evidence requirement failed: process and child tree timed out: $FilePath"
    }
    $endedUtc = [DateTimeOffset]::UtcNow
    [pscustomobject][ordered]@{
      stdout = $stdoutState.builder.ToString()
      stderr = $stderrState.builder.ToString()
      stdout_bytes = [int64]$stdoutState.bytes
      stderr_bytes = [int64]$stderrState.bytes
      output_limit_bytes = [int64]$N20ProcessOutputLimitBytes
      exit_code = $process.ExitCode
      started_utc = $startedUtc
      ended_utc = $endedUtc
      child_pid = [int]$process.Id
      child_executable_name = [string]$provenance.name
      child_executable_sha256 = [string]$provenance.sha256
      child_executable_bytes = [int64]$provenance.bytes
      process_timeout_seconds = [int]$TimeoutSeconds
      process_timed_out = $false
      tree_termination_attempted = [bool]$treeTermination.attempted
      tree_termination_method = [string]$treeTermination.method
      tree_termination_exit_code = [int]$treeTermination.exit_code
      child_environment_policy = $ChildEnvironmentPolicy
      blocked_ambient_count = [int]$stateToRestore.removed_blocked_count
      explicit_override_count = [int]$stateToRestore.explicit_override_count
      localappdata_overridden = Test-N20NameEqualsAny `
          'LOCALAPPDATA' ([string[]]@(
              $EnvironmentOverrides.Keys | ForEach-Object { [string]$_ }
            ))
    }
  } catch {
    if ($null -ne $environmentState) {
      try { Exit-FailClosedChildEnvironment $environmentState } catch {}
    }
    if ($processStarted -and $null -ne $process) {
      try {
        if (-not $process.HasExited) { [void](Stop-N20ProcessTree $process) }
      } catch {}
    }
    throw
  } finally {
    if ($null -ne $process) { $process.Dispose() }
  }
}

function Invoke-Git([string]$Arguments) {
  Assert-PlainPathChain $Root
  Assert-PlainPathChain $ExpectedGitDirectory
  Require (Test-Path -LiteralPath $ExpectedGitDirectory -PathType Container) `
      'expected workspace Git directory is missing'
  $quotedGitDirectory = '"' + $ExpectedGitDirectory.Replace('"', '\"') + '"'
  $quotedWorkTree = '"' + ([IO.Path]::GetFullPath($Root)).Replace('"', '\"') + '"'
  $gitArguments = "--git-dir=$quotedGitDirectory --work-tree=$quotedWorkTree -c core.autocrlf=false -c core.safecrlf=false $Arguments"
  $result = Invoke-CapturedProcess 'git.exe' $gitArguments 60 $true
  Require ($result.exit_code -eq 0) "read-only Git command failed: $Arguments"
  Require ([string]::IsNullOrWhiteSpace($result.stderr)) "read-only Git command wrote stderr: $Arguments"
  $result.stdout.TrimEnd("`r", "`n")
}

function Get-GitState {
  $topLevel = [IO.Path]::GetFullPath((Invoke-Git 'rev-parse --show-toplevel').Replace('/', '\'))
  Require ($topLevel.Equals([IO.Path]::GetFullPath($Root), [StringComparison]::OrdinalIgnoreCase)) `
      'Git top-level is not the verifier workspace'
  $head = (Invoke-Git 'rev-parse HEAD').Trim().ToLowerInvariant()
  Require ($head -match '^[0-9a-f]{40}$') 'Git HEAD is not a SHA-1 commit id'
  $gitDirectory = Invoke-Git 'rev-parse --git-dir'
  if (-not [IO.Path]::IsPathRooted($gitDirectory)) {
    $gitDirectory = Join-Path $Root $gitDirectory
  }
  $gitDirectory = [IO.Path]::GetFullPath($gitDirectory)
  Assert-PlainPathChain $gitDirectory
  Require ($gitDirectory.Equals($ExpectedGitDirectory, [StringComparison]::OrdinalIgnoreCase)) `
      'Git directory is not the expected workspace Git directory'
  $commonDirectory = Invoke-Git 'rev-parse --git-common-dir'
  if (-not [IO.Path]::IsPathRooted($commonDirectory)) {
    $commonDirectory = Join-Path $Root $commonDirectory
  }
  $commonDirectory = [IO.Path]::GetFullPath($commonDirectory)
  Assert-PlainPathChain $commonDirectory
  Require ($commonDirectory.Equals($ExpectedGitDirectory, [StringComparison]::OrdinalIgnoreCase)) `
      'Git common directory is not the expected workspace Git directory'
  $rows = New-Object System.Collections.Generic.List[string]
  $rows.Add("HEAD`t$head")
  foreach ($relative in @('logs/HEAD', 'logs/refs/remotes')) {
    $candidate = Join-Path $gitDirectory ($relative.Replace('/', '\'))
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
      $rows.Add("$relative`t$(File-Hash $candidate)")
    } elseif (Test-Path -LiteralPath $candidate -PathType Container) {
      foreach ($file in @(Get-ChildItem -LiteralPath $candidate -Recurse -File | Sort-Object FullName)) {
        $path = $file.FullName.Substring($gitDirectory.Length + 1).Replace('\', '/')
        $rows.Add("$path`t$(File-Hash $file.FullName)")
      }
    } else {
      $rows.Add("$relative`tabsent")
    }
  }
  [pscustomobject][ordered]@{
    head = $head
    reference_log_fingerprint_sha256 = Text-Hash (($rows.ToArray()) -join "`r`n")
  }
}

function Get-SafeTreeLeafPaths([string]$Directory, [string]$Label) {
  Assert-NoExcludedN20PathToken $Directory "$Label root"
  $root = Resolve-WorkspacePath $Directory
  Assert-PlainPathChain $root
  Require (Test-Path -LiteralPath $root -PathType Container) "$Label root is missing"
  $pending = New-Object System.Collections.Generic.Queue[string]
  $pending.Enqueue($root)
  $files = New-Object System.Collections.Generic.List[string]
  while ($pending.Count -gt 0) {
    $current = $pending.Dequeue()
    Assert-PlainPathChain $current
    foreach ($path in @([IO.Directory]::EnumerateFileSystemEntries($current) | Sort-Object)) {
      $relative = Relative-Path $path
      # Directory enumeration yields lexical names. Apply the node gate before
      # metadata, content, or hash access, then reject every reparse entry.
      Assert-NoExcludedN20PathToken $relative "$Label entry"
      $item = Get-Item -LiteralPath $path -Force
      Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
          "$Label entry is a reparse point: $relative"
      if ($item.PSIsContainer) {
        $pending.Enqueue($item.FullName)
      } else {
        $files.Add($item.FullName)
      }
    }
  }
  $files.ToArray()
}

function Get-ProtectedRepoFiles {
  $files = New-Object System.Collections.Generic.List[string]
  foreach ($relative in @('.codex', '.claude', '.opencode')) {
    $directory = Join-Path $Root $relative
    if (Test-Path -LiteralPath $directory -PathType Container) {
      foreach ($file in @(Get-SafeTreeLeafPaths $directory 'protected configuration')) {
        $files.Add($file)
      }
    }
  }
  foreach ($path in @([IO.Directory]::EnumerateFiles($Root, '*', [IO.SearchOption]::TopDirectoryOnly))) {
    $relative = Relative-Path $path
    Assert-NoExcludedN20PathToken $relative 'protected configuration root entry'
    Assert-PlainPathChain $path $true
    if ([IO.Path]::GetFileName($path) -match '(?i)^(?:codex|claude|opencode|model-config|llm-config).*[.](?:json|toml|ya?ml)$') {
      $files.Add($path)
    }
  }
  @($files | Sort-Object -Unique)
}

function Get-ProtectedRepoEntries {
  @((Get-ProtectedRepoFiles) | ForEach-Object { Get-FileEntry $_ 'protected-agent-or-model-config' })
}

function Assert-NoProtectedConfigurationPathChanged {
  $status = Invoke-Git 'status --short --untracked-files=all'
  $changedPaths = @($status -split '\r?\n' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | ForEach-Object {
      if ($_.Length -ge 4) { $_.Substring(3).Trim('"') } else { '' }
    })
  foreach ($path in $changedPaths) {
    Require ($path -notmatch '(?i)(^|/)(?:[.]codex|[.]claude|[.]opencode)(/|$)') "protected agent configuration path is changed: $path"
    Require ($path -notmatch '(?i)(?:model|llm|provider).*(?:config|settings).*[.](?:json|toml|ya?ml)$') "protected model configuration path is changed: $path"
  }
  foreach ($relative in @('scripts/build-cl.ps1', 'scripts/build-tests-cl.ps1', 'scripts/n20-verify.ps1')) {
    $text = Get-Content -Raw -LiteralPath (Join-Path $Root $relative)
    Require ($text -notmatch '(?i)\bgit(?:[.]exe)?\s+(?:commit|push)\b') "$relative contains a forbidden Git mutation command"
  }
  [pscustomobject][ordered]@{
    changed_path_count = $changedPaths.Count
    protected_path_change_count = 0
    protected_setting_change_count = 0
  }
}

function Get-FirstLine([string]$Path, [string]$Pattern) {
  $match = Select-String -LiteralPath $Path -Pattern $Pattern | Select-Object -First 1
  if ($null -eq $match) { return '' }
  $match.Line.Trim()
}

function Assert-N20Governance {
  Require (Test-Path -LiteralPath $PlanPath -PathType Leaf) 'N20 approved plan is missing'
  Require (Test-Path -LiteralPath $PlanAuditPath -PathType Leaf) 'N20 plan audit is missing'
  Require ((File-Hash $PlanPath) -ceq $GateExpected.approved_plan_sha256) 'approved N20 plan hash changed'
  Require ((File-Hash $PlanAuditPath) -ceq $GateExpected.plan_audit_sha256) 'N20 plan-audit hash changed'
  $status = Get-FirstLine $PlanPath '(?i)^\*\*Status\*\*:'
  $planAudit = Get-FirstLine $PlanPath '(?i)^\*\*Plan audit\*\*:'
  $outcome = Get-FirstLine $PlanPath '(?i)^\*\*Outcome audit\*\*:'
  $verdict = Get-FirstLine $PlanAuditPath '(?i)^\*\*VERDICT'
  $auditedHash = Get-FirstLine $PlanAuditPath '(?i)^\*\*Plan SHA-256\*\*:'
  Require ($status -match '(?i)^\*\*Status\*\*:\s*approved\s*$') 'N20 plan status is not approved'
  Require ($planAudit -match '(?i)\bPASS\b' -and $planAudit -notmatch '(?i)\bFAIL\b') 'N20 plan metadata does not record plan-audit PASS'
  Require ($outcome -match '(?i)\bpending\b') 'N20 outcome audit is not pending'
  Require ($verdict -match '(?i)\bPASS\b' -and $verdict -notmatch '(?i)\bFAIL\b') 'N20 plan audit verdict is not PASS'
  Require ((Get-Content -Raw -LiteralPath $PlanAuditPath) -match '(?i)Auditor[^\r\n]*Claude Code') 'N20 plan auditor is not Claude Code'
  Require ($auditedHash -ceq "**Plan SHA-256**: $($GateExpected.audited_draft_plan_sha256)") 'N20 audit is not bound to the audited draft'
  [pscustomobject][ordered]@{
    approved_plan_sha256 = File-Hash $PlanPath
    audited_draft_plan_sha256 = $GateExpected.audited_draft_plan_sha256
    plan_audit_sha256 = File-Hash $PlanAuditPath
    plan_status = 'approved'
    plan_audit_verdict = 'PASS'
    plan_auditor = 'Claude Code'
    outcome_audit = 'pending'
  }
}

function Assert-DependencyContracts([object[]]$GateRows) {
  Require ($GateRows.Count -eq $DependencyContracts.Count * 2) 'gate dependency-audit row count is not exact'
  $facts = New-Object System.Collections.Generic.List[object]
  $rowIndex = 0
  foreach ($dependency in $DependencyContracts) {
    $expectedRows = @(
      [pscustomobject]@{ Path="docs/nodes/$($dependency.Node)-PLAN-AUDIT.md"; Hash=$dependency.Plan; Kind='plan' },
      [pscustomobject]@{ Path="docs/nodes/$($dependency.Node)-HARD-AUDIT.md"; Hash=$dependency.Outcome; Kind='outcome' }
    )
    foreach ($expected in $expectedRows) {
      $row = $GateRows[$rowIndex]
      Require-ExactJsonPropertyNames $row @('path', 'sha256', 'auditor', 'verdict') "gate dependency[$rowIndex]"
      Require-JsonStringExact $row.path $expected.Path "gate dependency[$rowIndex].path"
      Require-JsonStringExact $row.sha256 $expected.Hash "gate dependency[$rowIndex].sha256"
      Require-JsonStringExact $row.auditor 'Claude Code' "gate dependency[$rowIndex].auditor"
      Require-JsonStringExact $row.verdict 'PASS' "gate dependency[$rowIndex].verdict"
      $absolute = Join-Path $Root $expected.Path
      Require ((File-Hash $absolute) -ceq $expected.Hash) "$($dependency.Node) $($expected.Kind) audit hash changed"
      $content = Get-Content -Raw -LiteralPath $absolute
      Require ($content -match '(?i)Auditor[^\r\n]*Claude Code') "$($dependency.Node) $($expected.Kind) auditor is not Claude Code"
      $verdictLine = Get-FirstLine $absolute '(?i)^\*\*VERDICT'
      Require ($verdictLine -match '(?i)\bPASS\b' -and $verdictLine -notmatch '(?i)\bFAIL\b') "$($dependency.Node) $($expected.Kind) audit is not PASS"
      $facts.Add([pscustomobject][ordered]@{
          node = $dependency.Node
          kind = $expected.Kind
          path = $expected.Path
          sha256 = $expected.Hash
          auditor = 'Claude Code'
          verdict = 'PASS'
        })
      $rowIndex++
    }
  }
  $facts.ToArray()
}

function Assert-PreImplementationGate([switch]$RequireCurrentGateBinary) {
  Require ((File-Hash $GatePath) -ceq $GateExpected.file_sha256) 'pre-implementation gate file hash changed'
  $strict = Read-StrictJson $GatePath 'N20 pre-implementation gate'
  $gate = $strict.value
  Require-ExactJsonPropertyNames $gate @(
    'format_version', 'node', 'gate', 'state', 'started_utc', 'doctor_completed_utc',
    'cleanup_completed_utc', 'completed_utc', 'command', 'execution_path', 'governance',
    'dependency_audits', 'qbrain', 'baseline', 'result', 'isolation',
    'protected_model_configuration_changed', 'live_network_or_provider_call',
    'commit_or_push_executed'
  ) 'pre-implementation gate'
  Require-JsonIntegerExact $gate.format_version 1 'gate format_version'
  Require-JsonStringExact $gate.node 'N20' 'gate node'
  Require-JsonStringExact $gate.gate 'pre-implementation-schema-v12' 'gate identity'
  Require-JsonStringExact $gate.state 'passed' 'gate state'

  $started = Parse-UtcTimestamp $gate.started_utc 'gate started_utc'
  $doctorCompleted = Parse-UtcTimestamp $gate.doctor_completed_utc 'gate doctor_completed_utc'
  $cleanupCompleted = Parse-UtcTimestamp $gate.cleanup_completed_utc 'gate cleanup_completed_utc'
  $completed = Parse-UtcTimestamp $gate.completed_utc 'gate completed_utc'
  Require ($started -lt $doctorCompleted -and $doctorCompleted -lt $cleanupCompleted -and $cleanupCompleted -lt $completed) 'gate timestamps are not strictly ordered'
  Require ($completed -le [DateTimeOffset]::UtcNow) 'gate completion is in the future'
  Require ((Get-Item -LiteralPath $GatePath).LastWriteTimeUtc -ge $completed.UtcDateTime) 'gate file timestamp predates gate completion'

  Require ($gate.command -is [string]) 'gate command is not a JSON string'
  $commandMatch = [regex]::Match([string]$gate.command, '^build\\cl\\qbrain[.]exe doctor --brain (n20-gate-[0-9a-f]{32}) --json$')
  Require ($commandMatch.Success) 'gate command is not the isolated canonical doctor command'
  Require ($gate.execution_path -is [Array]) 'gate execution_path is not an array'
  $executionPath = @($gate.execution_path)
  Require ($executionPath.Count -eq $GateExecutionPath.Count) 'gate execution_path count changed'
  for ($index = 0; $index -lt $GateExecutionPath.Count; ++$index) {
    Require-JsonStringExact $executionPath[$index] $GateExecutionPath[$index] "gate execution_path[$index]"
  }

  Require-ExactJsonPropertyNames $gate.governance @(
    'audited_draft_plan_sha256', 'approved_plan_sha256', 'plan_audit_sha256',
    'approval_metadata_only', 'approval_metadata_fields', 'git_head'
  ) 'gate governance'
  foreach ($binding in @(
      @('audited_draft_plan_sha256', $GateExpected.audited_draft_plan_sha256),
      @('approved_plan_sha256', $GateExpected.approved_plan_sha256),
      @('plan_audit_sha256', $GateExpected.plan_audit_sha256),
      @('git_head', $GateExpected.git_head))) {
    Require-JsonStringExact $gate.governance.($binding[0]) $binding[1] "gate governance $($binding[0])"
  }
  Require-JsonBooleanExact $gate.governance.approval_metadata_only $true 'gate metadata-only approval flag'
  Require ($gate.governance.approval_metadata_fields -is [Array]) 'gate approval metadata fields are not an array'
  $approvalFields = @($gate.governance.approval_metadata_fields)
  Require ($approvalFields.Count -eq 2) 'gate approval metadata field count is not two'
  Require-JsonStringExact $approvalFields[0] 'Status' 'gate approval metadata field[0]'
  Require-JsonStringExact $approvalFields[1] 'Plan audit' 'gate approval metadata field[1]'
  [void](Assert-N20Governance)
  $gitHead = (Invoke-Git 'rev-parse HEAD').Trim().ToLowerInvariant()
  Require ($gitHead -ceq $GateExpected.git_head) 'Git HEAD differs from the approved pre-implementation gate'

  Require ($gate.dependency_audits -is [Array]) 'gate dependency_audits is not an array'
  $dependencies = Assert-DependencyContracts @($gate.dependency_audits)

  Require-ExactJsonPropertyNames $gate.qbrain @('sha256', 'bytes', 'last_write_utc') 'gate qbrain binding'
  Require-JsonStringExact $gate.qbrain.sha256 $GateExpected.qbrain_sha256 'gate qbrain SHA-256'
  Require-JsonInteger $gate.qbrain.bytes 'gate qbrain byte count'
  Require ([int64]$gate.qbrain.bytes -gt 0) 'gate qbrain byte count is not positive'
  $gateBinaryWrite = Parse-UtcTimestamp $gate.qbrain.last_write_utc 'gate qbrain last-write timestamp'
  Require ($gateBinaryWrite -lt $started) 'gate binary was not built before gate execution'
  if ($RequireCurrentGateBinary) {
    Require ((File-Hash $Qbrain) -ceq $GateExpected.qbrain_sha256) 'current qbrain.exe is not the binary bound to the gate before official builds'
    $qbrainItem = Get-Item -LiteralPath $Qbrain
    Require ([int64]$qbrainItem.Length -eq [int64]$gate.qbrain.bytes) 'current gate binary byte count changed'
    Require ($qbrainItem.LastWriteTimeUtc.ToString('o') -ceq [string]$gate.qbrain.last_write_utc) 'current gate binary timestamp changed'
  }

  Require-ExactJsonPropertyNames $gate.baseline @(
    'manifest_sha256', 'git_fingerprint_sha256', 'audit_artifact_last_write_utc',
    'input_count', 'inputs', 'absent_planned_paths'
  ) 'gate baseline'
  Require-JsonStringExact $gate.baseline.manifest_sha256 $GateExpected.baseline_manifest_sha256 'gate baseline manifest SHA-256'
  Require-JsonStringExact $gate.baseline.git_fingerprint_sha256 $GateExpected.baseline_git_fingerprint_sha256 'gate baseline Git fingerprint SHA-256'
  $auditWrite = Parse-UtcTimestamp $gate.baseline.audit_artifact_last_write_utc 'gate audit artifact last-write timestamp'
  Require ($auditWrite -lt $started) 'plan audit artifact does not predate the gate'
  Require ((Get-Item -LiteralPath $PlanAuditPath).LastWriteTimeUtc.ToString('o') -ceq [string]$gate.baseline.audit_artifact_last_write_utc) 'plan-audit timestamp changed after gate capture'
  Require-JsonIntegerExact $gate.baseline.input_count $GateBaselinePaths.Count 'gate baseline input_count'
  Require ($gate.baseline.inputs -is [Array]) 'gate baseline inputs is not an array'
  $inputs = @($gate.baseline.inputs)
  Require ($inputs.Count -eq $GateBaselinePaths.Count) 'gate baseline input array count is not exact'
  $manifestRows = New-Object System.Collections.Generic.List[string]
  $gitRows = New-Object System.Collections.Generic.List[string]
  $inputMap = @{}
  for ($index = 0; $index -lt $inputs.Count; ++$index) {
    $entry = $inputs[$index]
    Require-ExactJsonPropertyNames $entry @(
      'path', 'sha256', 'bytes', 'last_write_utc', 'git_status', 'git_diff_sha256'
    ) "gate baseline input[$index]"
    Require-JsonStringExact $entry.path $GateBaselinePaths[$index] "gate baseline input[$index].path"
    Require ($entry.sha256 -is [string] -and [string]$entry.sha256 -match '^[0-9a-f]{64}$') "gate baseline input[$index] has an invalid SHA-256"
    Require-JsonInteger $entry.bytes "gate baseline input[$index] byte count"
    Require ([int64]$entry.bytes -ge 0) "gate baseline input[$index] has a negative byte count"
    $entryWrite = Parse-UtcTimestamp $entry.last_write_utc "gate baseline input[$index] last-write timestamp"
    Require ($entryWrite -lt $started) "gate baseline input[$index] timestamp does not predate gate execution"
    Require ($entry.git_status -is [string] -and [string]$entry.git_status.Length -le 4096) "gate baseline input[$index] has invalid Git status"
    Require ($entry.git_diff_sha256 -is [string] -and [string]$entry.git_diff_sha256 -match '^[0-9a-f]{64}$') "gate baseline input[$index] has invalid Git diff SHA-256"
    Require (-not $inputMap.ContainsKey([string]$entry.path)) "gate baseline path is duplicated: $($entry.path)"
    $inputMap[[string]$entry.path] = $entry
    $manifestRows.Add("$($entry.path)`t$($entry.sha256)`t$([int64]$entry.bytes)`t$($entry.last_write_utc)`t$($entry.git_status)`t$($entry.git_diff_sha256)")
    $gitRows.Add("$($entry.path)`t$($entry.git_status)`t$($entry.git_diff_sha256)")
  }
  Require ((Text-Hash (($manifestRows.ToArray()) -join "`r`n")) -ceq $GateExpected.baseline_manifest_sha256) 'gate baseline manifest digest is invalid'
  Require ((Text-Hash (($gitRows.ToArray()) -join "`r`n")) -ceq $GateExpected.baseline_git_fingerprint_sha256) 'gate baseline Git fingerprint digest is invalid'
  Require ($gate.baseline.absent_planned_paths -is [Array]) 'gate absent_planned_paths is not an array'
  $absent = @($gate.baseline.absent_planned_paths)
  Require ($absent.Count -eq $GateAbsentPlannedPaths.Count) 'gate absent planned-path count is not exact'
  for ($index = 0; $index -lt $absent.Count; ++$index) {
    Require-JsonStringExact $absent[$index] $GateAbsentPlannedPaths[$index] "gate absent planned path[$index]"
  }

  Require-ExactJsonPropertyNames $gate.result @('exit_code', 'ok', 'schema_version', 'stderr_empty', 'stdout_json_sha256') 'gate result'
  Require-JsonIntegerExact $gate.result.exit_code 0 'gate result exit_code'
  Require-JsonBooleanExact $gate.result.ok $true 'gate result ok'
  Require-JsonIntegerExact $gate.result.schema_version 12 'gate result schema_version'
  Require-JsonBooleanExact $gate.result.stderr_empty $true 'gate result stderr_empty'
  Require ($gate.result.stdout_json_sha256 -is [string] -and [string]$gate.result.stdout_json_sha256 -match '^[0-9a-f]{64}$') 'gate result stdout hash is invalid'

  Require-ExactJsonPropertyNames $gate.isolation @(
    'localappdata_overridden', 'temporary_root', 'brain_id', 'temporary_file_count',
    'temporary_files', 'production_localappdata_not_used', 'production_qbrain_root_not_used',
    'config_persisted', 'temporary_root_removed'
  ) 'gate isolation'
  Require-JsonBooleanExact $gate.isolation.localappdata_overridden $true 'gate LOCALAPPDATA override'
  Require-JsonStringExact $gate.isolation.brain_id $commandMatch.Groups[1].Value 'gate isolation brain id'
  Require ($gate.isolation.temporary_root -is [string]) 'gate temporary root is not a string'
  $expectedTemporaryRoot = Join-Path $Root "build\cl\n20-gate-localappdata-$($commandMatch.Groups[1].Value.Substring(9))"
  Require ([IO.Path]::GetFullPath([string]$gate.isolation.temporary_root).Equals([IO.Path]::GetFullPath($expectedTemporaryRoot), [StringComparison]::OrdinalIgnoreCase)) 'gate temporary root is not the canonical isolated path'
  Require-JsonIntegerExact $gate.isolation.temporary_file_count 1 'gate temporary file count'
  Require ($gate.isolation.temporary_files -is [Array]) 'gate temporary_files is not an array'
  $temporaryFiles = @($gate.isolation.temporary_files)
  Require ($temporaryFiles.Count -eq 1) 'gate temporary file list count is not one'
  Require-JsonStringExact $temporaryFiles[0] "Qbrain\brains\$($gate.isolation.brain_id)\brain.db" 'gate temporary database path'
  Require-JsonBooleanExact $gate.isolation.production_localappdata_not_used $true 'gate production LOCALAPPDATA exclusion'
  Require-JsonBooleanExact $gate.isolation.production_qbrain_root_not_used $true 'gate production Qbrain-root exclusion'
  Require-JsonBooleanExact $gate.isolation.config_persisted $false 'gate config persistence flag'
  Require-JsonBooleanExact $gate.isolation.temporary_root_removed $true 'gate temporary cleanup flag'
  Require (-not (Test-Path -LiteralPath ([string]$gate.isolation.temporary_root))) 'gate temporary root still exists'
  Require-JsonBooleanExact $gate.protected_model_configuration_changed $false 'gate protected-model-configuration flag'
  Require-JsonBooleanExact $gate.live_network_or_provider_call $false 'gate live-network/provider flag'
  Require-JsonBooleanExact $gate.commit_or_push_executed $false 'gate commit/push flag'

  [pscustomobject][ordered]@{
    path = Relative-Path $GatePath
    file_sha256 = File-Hash $GatePath
    started_utc = [string]$gate.started_utc
    completed_utc = [string]$gate.completed_utc
    approved_plan_sha256 = [string]$gate.governance.approved_plan_sha256
    audited_draft_plan_sha256 = [string]$gate.governance.audited_draft_plan_sha256
    plan_audit_sha256 = [string]$gate.governance.plan_audit_sha256
    git_head = [string]$gate.governance.git_head
    qbrain_sha256 = [string]$gate.qbrain.sha256
    schema_version = 12
    baseline_manifest_sha256 = [string]$gate.baseline.manifest_sha256
    baseline_git_fingerprint_sha256 = [string]$gate.baseline.git_fingerprint_sha256
    baseline_inputs = $inputs
    baseline_input_map = $inputMap
    dependency_contracts = $dependencies
    production_root_not_used = [bool]$gate.isolation.production_qbrain_root_not_used
    protected_model_configuration_changed = [bool]$gate.protected_model_configuration_changed
    live_network_or_provider_call = [bool]$gate.live_network_or_provider_call
    commit_or_push_executed = [bool]$gate.commit_or_push_executed
  }
}

function Assert-CorrectiveBoundary([object]$Gate) {
  $completed = Parse-UtcTimestamp $Gate.completed_utc 'gate completion for corrective boundary'
  $changed = New-Object System.Collections.Generic.List[object]
  $concurrent = New-Object System.Collections.Generic.List[object]
  $unchanged = New-Object System.Collections.Generic.List[object]
  foreach ($path in $GateBaselinePaths) {
    $recorded = $Gate.baseline_input_map[$path]
    $current = Join-Path $Root $path
    Require (Test-Path -LiteralPath $current -PathType Leaf) "gate baseline input is now missing: $path"
    $item = Get-Item -LiteralPath $current
    $hash = File-Hash $current
    if ($ExpectedChangedBaselinePaths -ccontains $path) {
      Require ($hash -cne [string]$recorded.sha256) "approved N20 corrective input did not change: $path"
      Require ($item.LastWriteTimeUtc -gt $completed.UtcDateTime) "approved N20 corrective input does not postdate the gate: $path"
      $changed.Add([pscustomobject][ordered]@{
          path = $path
          gate_sha256 = [string]$recorded.sha256
          current_sha256 = $hash
          current_bytes = [int64]$item.Length
          current_last_write_utc = $item.LastWriteTimeUtc.ToString('o')
        })
    } elseif ($ExpectedConcurrentBaselinePaths -ccontains $path) {
      Require ($hash -cne [string]$recorded.sha256) "approved concurrent-wave input did not change: $path"
      Require ($item.LastWriteTimeUtc -gt $completed.UtcDateTime) "approved concurrent-wave input does not postdate the gate: $path"
      $concurrent.Add([pscustomobject][ordered]@{
          path = $path
          gate_sha256 = [string]$recorded.sha256
          current_sha256 = $hash
          current_bytes = [int64]$item.Length
          current_last_write_utc = $item.LastWriteTimeUtc.ToString('o')
        })
    } else {
      Require ($hash -ceq [string]$recorded.sha256) "non-N20 gate input changed after the gate: $path"
      Require ([int64]$item.Length -eq [int64]$recorded.bytes) "non-N20 gate input byte count changed: $path"
      Require ($item.LastWriteTimeUtc.ToString('o') -ceq [string]$recorded.last_write_utc) "non-N20 gate input timestamp changed: $path"
      $unchanged.Add([pscustomobject][ordered]@{ path=$path; sha256=$hash })
    }
  }
  Require ((@($changed | ForEach-Object { $_.path }) -join "`n") -ceq ($ExpectedChangedBaselinePaths -join "`n")) 'corrective baseline path set is not exact'
  Require ((@($concurrent | ForEach-Object { $_.path }) -join "`n") -ceq ($ExpectedConcurrentBaselinePaths -join "`n")) 'concurrent-wave baseline path set is not exact'
  $newFiles = New-Object System.Collections.Generic.List[object]
  foreach ($path in $ExpectedNewImplementationPaths) {
    $absolute = Join-Path $Root $path
    Require (Test-Path -LiteralPath $absolute -PathType Leaf) "approved N20 new implementation file is missing: $path"
    $item = Get-Item -LiteralPath $absolute
    Require ($item.LastWriteTimeUtc -gt $completed.UtcDateTime) "approved N20 new implementation file does not postdate the gate: $path"
    $newFiles.Add((Get-FileEntry $absolute 'new-n20-implementation'))
  }
  $correctiveBaselineRows = @($changed.ToArray()) + @($concurrent.ToArray())
  $baselineLateOrEqual = @($correctiveBaselineRows | Where-Object {
      (Parse-UtcTimestamp $_.current_last_write_utc 'corrective boundary baseline timestamp') -le $completed
    }).Count
  $newFileLateOrEqual = @($newFiles | Where-Object {
      (Parse-UtcTimestamp $_.last_write_utc 'corrective boundary new-file timestamp') -le $completed
    }).Count
  $allCorrectiveFilesPostdateGate = ($baselineLateOrEqual -eq 0 -and $newFileLateOrEqual -eq 0)
  $schemaOrMigrationInputChanged = @($correctiveBaselineRows | Where-Object {
      $_.path -in @('include/qbrain/storage/schema_sql.hpp', 'src/qbrain/storage/migrate.cpp')
    }).Count -gt 0
  $ledgerChanged = @($correctiveBaselineRows | Where-Object {
      $_.path -ceq 'docs/OPS-PARITY-LEDGER.md'
    }).Count -gt 0
  $historicalOutcomeAuditChanged = @($correctiveBaselineRows | Where-Object {
      $_.path -ceq 'docs/nodes/N20-HARD-AUDIT.md'
    }).Count -gt 0
  [pscustomobject][ordered]@{
    expected_changed_baseline_paths = $ExpectedChangedBaselinePaths
    changed_baseline_inputs = $changed.ToArray()
    expected_concurrent_baseline_paths = $ExpectedConcurrentBaselinePaths
    concurrent_wave_baseline_inputs = $concurrent.ToArray()
    unchanged_baseline_input_count = $unchanged.Count
    expected_new_implementation_paths = $ExpectedNewImplementationPaths
    new_implementation_files = $newFiles.ToArray()
    gate_completed_before_all_corrective_files = $allCorrectiveFilesPostdateGate
    schema_or_migration_input_changed = $schemaOrMigrationInputChanged
    ledger_changed = $ledgerChanged
    historical_outcome_audit_changed = $historicalOutcomeAuditChanged
  }
}

function Get-RegisteredTests {
  $path = Join-Path $Root 'tests\test_main.cpp'
  $text = Get-Content -Raw -LiteralPath $path
  $matches = [regex]::Matches($text, '\{\s*"([^"]+)"\s*,\s*test_[A-Za-z0-9_]+\s*\}')
  $names = @($matches | ForEach-Object { $_.Groups[1].Value })
  Require ($names.Count -ge $RequiredMinimumRegisteredTests) "registered suite does not contain the completed baseline plus dedicated N20 ($RequiredMinimumRegisteredTests minimum)"
  Require (($names | Sort-Object -Unique).Count -eq $names.Count) 'registered test names are not unique'
  Require (@($names | Where-Object { $_ -ceq 'n20' }).Count -eq 1) 'dedicated n20 test is not registered exactly once'
  Require (@($names | Where-Object { $_ -ceq 'n20_23' }).Count -eq 1) 'retained n20_23 regression is not registered exactly once'
  $names
}

function Assert-DedicatedN20Registration {
  $testMain = Get-Content -Raw -LiteralPath (Join-Path $Root 'tests\test_main.cpp')
  $cmake = Get-Content -Raw -LiteralPath (Join-Path $Root 'CMakeLists.txt')
  $buildTests = Get-Content -Raw -LiteralPath $TestBuildScript
  Require ([regex]::Matches($testMain, '(?m)^\s*void\s+test_n20\s*\(\s*\)\s*;\s*$').Count -eq 1) 'test_main.cpp does not declare test_n20 exactly once'
  Require ([regex]::Matches($testMain, '\{\s*"n20"\s*,\s*test_n20\s*\}').Count -eq 1) 'test_main.cpp does not register dedicated n20 exactly once'
  Require ([regex]::Matches($cmake, '(?m)^\s*tests/test_n20[.]cpp\s*$').Count -eq 1) 'CMakeLists.txt does not include tests/test_n20.cpp exactly once'
  Require ([regex]::Matches($buildTests, '(?m)^\s*"tests\\test_n20[.]cpp",?\s*$').Count -eq 1) 'build-tests-cl.ps1 does not include tests/test_n20.cpp exactly once'
  Require (Test-Path -LiteralPath (Join-Path $Root 'tests\test_n20.cpp') -PathType Leaf) 'dedicated tests/test_n20.cpp is missing'
}

function Get-QuotedSourceArray([string]$Path, [string]$VariableName) {
  Assert-NoExcludedN20PathToken $Path "$VariableName build-script path"
  Assert-PlainPathChain $Path $true
  $text = Get-Content -Raw -LiteralPath $Path
  $pattern = '(?s)\$' + [regex]::Escape($VariableName) + '\s*=\s*@\((.*?)\r?\n\)'
  $match = [regex]::Match($text, $pattern)
  Require ($match.Success) "cannot parse $VariableName from $(Relative-Path $Path)"
  $values = @([regex]::Matches($match.Groups[1].Value, '"([^"]+[.](?:cpp|c))"') | ForEach-Object { $_.Groups[1].Value })
  Require ($values.Count -gt 0) "$VariableName is empty"
  Require (($values | Sort-Object -Unique).Count -eq $values.Count) "$VariableName contains duplicate source paths"
  $values
}

function Get-BuildClosureFiles {
  $files = New-Object System.Collections.Generic.List[object]
  $sourcePaths = @(
    @(Get-QuotedSourceArray $BuildScript 'productionSources') +
    @(Get-QuotedSourceArray $TestBuildScript 'defaultTestSources')
  )
  foreach ($relative in @($sourcePaths | Sort-Object -Unique)) {
    Assert-NoExcludedN20PathToken $relative 'declared native source path'
    $role = if ($relative -match '(?i)^tests[\\/]test_n(?:2[1-9]|[3-9][0-9])(?:[_-]|[.])') {
      'integrated-regression-build-input-not-n20-evidence'
    } else {
      'native-build-input'
    }
    $files.Add([pscustomobject]@{ path=(Resolve-WorkspacePath $relative); role=$role })
  }
  foreach ($directory in @('include', 'src', 'tests')) {
    foreach ($path in @(Get-SafeTreeLeafPaths (Join-Path $Root $directory) 'native header closure')) {
      if ([IO.Path]::GetExtension($path) -in @('.hpp', '.h')) {
        $files.Add([pscustomobject]@{ path=$path; role='native-build-input' })
      }
    }
  }
  foreach ($relative in @(
      'CMakeLists.txt', 'scripts/build-cl.ps1', 'scripts/build-tests-cl.ps1',
      'scripts/n20-verify.ps1', 'third_party/nlohmann/json.hpp',
      'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.c',
      'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.h')) {
    Assert-NoExcludedN20PathToken $relative 'declared build input path'
    $files.Add([pscustomobject]@{ path=(Resolve-WorkspacePath $relative); role='native-build-input' })
  }
  @($files | Sort-Object path -Unique)
}

function Get-InputManifestEntries {
  $entries = New-Object System.Collections.Generic.List[object]
  foreach ($file in @(Get-BuildClosureFiles)) {
    $entries.Add((Get-FileEntry ([string]$file.path) ([string]$file.role)))
  }
  foreach ($relative in @(
      'AGENTS.md', 'docs/nodes/README.md', 'docs/nodes/N20-PLAN.md',
      'docs/nodes/N20-PLAN-AUDIT.md',
      'docs/nodes/n20-evidence/PRE-IMPLEMENTATION-GATE.json')) {
    $entries.Add((Get-FileEntry (Join-Path $Root $relative) 'n20-governance-input'))
  }
  @($entries | Sort-Object path -Unique)
}

function Get-ScopedDiffFacts {
  $paths = @($ExpectedChangedBaselinePaths + $ExpectedNewImplementationPaths)
  $arguments = ($paths | ForEach-Object { $_.Replace('\', '/') }) -join ' '
  $diff = Invoke-Git "diff --no-ext-diff --unified=0 HEAD -- $arguments"
  $names = Invoke-Git "diff --no-ext-diff --name-only HEAD -- $arguments"
  $changedNames = @($names -split '\r?\n' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  foreach ($name in $changedNames) {
    Require ($name -notmatch '(?i)(^|/)docs/nodes/(?:N(?:2[1-9]|[3-9][0-9])|n(?:2[1-9]|[3-9][0-9])-evidence)(?:/|$)') "later-node artifact appears in the N20 scoped diff: $name"
    Require ($name -notmatch '(?i)(^|/)docs/nodes/N30(?:-|/|$)') "N30 artifact appears in the N20 scoped diff: $name"
    Require ($name -notmatch '(?i)(^|/)(?:[.]codex|[.]claude|[.]opencode)(/|$)') "protected configuration path appears in the N20 scoped diff: $name"
  }
  $settingPattern = '(?i)\b(?:base[_-]?url|api[_-]?key|provider|model(?:[_-]?name)?|reasoning(?:[_-]?effort)?|context[_-]?(?:size|window)|compression[_-]?threshold)\b\s*(?:=|:)'
  $protectedAssignmentLines = @($diff -split '\r?\n' | Where-Object {
      ($_ -match '^[+-]') -and ($_ -notmatch '^(?:[+]{3}|[-]{3})') -and ($_ -match $settingPattern)
    })
  Require ($protectedAssignmentLines.Count -eq 0) 'protected model/provider configuration assignment appears in the N20 scoped diff'
  [pscustomobject][ordered]@{
    diff_sha256 = Text-Hash $diff
    tracked_changed_path_count = $changedNames.Count
    protected_path_change_count = 0
    protected_assignment_change_count = 0
    later_node_artifact_count = 0
    n30_artifact_count = 0
  }
}

function Assert-N20SpecificArtifactPaths([string[]]$Paths) {
  foreach ($path in $Paths) {
    Require ($path -notmatch '(?i)^docs/nodes/(?:N(?:2[1-9]|[3-9][0-9])|n(?:2[1-9]|[3-9][0-9])-evidence)(?:-|/|$)') "later-node artifact cannot supply N20-specific evidence: $path"
    Require ($path -notmatch '(?i)^docs/nodes/N30(?:-|/|$)') "N30 artifact cannot supply N20-specific evidence: $path"
  }
}

function New-PrebuildManifest {
  Assert-EvidenceDirectory
  # A diagnostic production rebuild may have replaced the pre-implementation
  # binary before evidence preparation. The immutable gate still binds the
  # original hash; the official build phase below proves the final binary is a
  # different, freshly produced artifact.
  $gate = Assert-PreImplementationGate
  $governance = Assert-N20Governance
  $boundary = Assert-CorrectiveBoundary $gate
  $dependencies = Assert-DependencyContracts @((Read-StrictJson $GatePath 'N20 pre-implementation gate').value.dependency_audits)
  Assert-DedicatedN20Registration
  $registeredTests = @(Get-RegisteredTests)
  $protectedPolicy = Assert-NoProtectedConfigurationPathChanged
  $protectedFiles = @(Get-ProtectedRepoEntries)
  $inputs = @(Get-InputManifestEntries)
  $git = Get-GitState
  Require ($git.head -ceq $GateExpected.git_head) 'Git HEAD changed before preparation'
  $scopedDiff = Get-ScopedDiffFacts
  Assert-N20SpecificArtifactPaths @($N20DeliverablePaths + $VerifierOutputPaths)
  $manifest = [pscustomobject][ordered]@{
    format_version = 2
    node = 'N20'
    state = 'prepared-before-official-builds'
    preparation_nonce = [Guid]::NewGuid().ToString('N')
    prepared_utc = [DateTimeOffset]::UtcNow.ToString('o')
    gate = $gate
    governance = $governance
    dependency_contracts = $dependencies
    corrective_boundary = $boundary
    suite_baseline = [pscustomobject][ordered]@{
      completed_registered_tests = $CompletedSuiteBaseline
      required_minimum_registered_tests = $RequiredMinimumRegisteredTests
      expected_registered_tests = $registeredTests.Count
      registered_test_names = $registeredTests
      dedicated_n20_registration_count = 1
      retained_n20_23_registration_count = 1
    }
    native_build_inputs = $inputs
    protected_repo_files = $protectedFiles
    protected_configuration_policy = $protectedPolicy
    git = $git
    scoped_diff = $scopedDiff
    output_policy = [pscustomobject][ordered]@{
      generated_paths = $VerifierOutputPaths
      audit_verdict_written = $false
      node_or_ledger_status_written = $false
    }
    later_node_specific_evidence_count = 0
    n30_artifact_count = 0
  }
  Write-Utf8Text $PrebuildManifestPath (($manifest | ConvertTo-Json -Depth 12) + [Environment]::NewLine)
  Write-N20PendingState $manifest $true 'prepared; official build and runtime evidence pending'
  Write-Host "N20_PREPARED expected_registered_tests=$($registeredTests.Count) manifest=$(File-Hash $PrebuildManifestPath)"
}

function Write-N20PendingState([object]$Manifest, [bool]$ResetLogs, [string]$Reason) {
  $prebuildHash = File-Hash $PrebuildManifestPath
  if ($ResetLogs) {
    foreach ($entry in @(
        [pscustomobject]@{ Path=$ProductionBuildLog; Stage='production-build' },
        [pscustomobject]@{ Path=$TestBuildAndFirstSuiteLog; Stage='test-build-and-suite-run-1' },
        [pscustomobject]@{ Path=$SecondSuiteLog; Stage='suite-run-2' })) {
      Write-Utf8Lines $entry.Path @(
        'state=pending',
        "stage=$($entry.Stage)",
        "preparation_nonce=$($Manifest.preparation_nonce)",
        "prebuild_manifest_sha256=$prebuildHash"
      )
    }
  }
  Write-Utf8Lines $ReportPath @(
    '# N20 Runtime Verification Report',
    '',
    '**State: PENDING**',
    '',
    'This is factual runtime-evidence scaffolding only. It is not an audit verdict.',
    '',
    "- Reason: $Reason",
    "- Preparation nonce: $($Manifest.preparation_nonce)",
    "- PREBUILD-MANIFEST SHA-256: ``$prebuildHash``",
    '- Required next command: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/n20-verify.ps1 -RunBuilds`'
  )
  $pending = [pscustomobject][ordered]@{
    format_version = 2
    node = 'N20'
    state = 'pending-runtime-evidence'
    preparation_nonce = [string]$Manifest.preparation_nonce
    prebuild_manifest_sha256 = $prebuildHash
    updated_utc = [DateTimeOffset]::UtcNow.ToString('o')
    reason = $Reason
    runtime_evidence_verified = $false
    audit_verdict_written = $false
    node_or_ledger_status_written = $false
  }
  Write-Utf8Text $EvidenceManifestPath (($pending | ConvertTo-Json -Depth 6) + [Environment]::NewLine)
}

function Require-EntrySetsCurrent([object[]]$Recorded, [object[]]$Current, [string]$Label) {
  Require ($Recorded.Count -eq $Current.Count) "$Label entry count changed"
  for ($index = 0; $index -lt $Recorded.Count; ++$index) {
    $old = $Recorded[$index]
    $new = $Current[$index]
    Require-ExactJsonPropertyNames $old @('role', 'path', 'sha256', 'bytes', 'last_write_utc') "$Label recorded entry[$index]"
    Require ([string]$old.role -ceq [string]$new.role) "$Label role changed at entry $index"
    Require ([string]$old.path -ceq [string]$new.path) "$Label path changed at entry $index"
    Require ([string]$old.sha256 -ceq [string]$new.sha256) "$Label hash changed: $($old.path)"
    Require-JsonInteger $old.bytes "$Label byte count: $($old.path)"
    Require ([int64]$old.bytes -eq [int64]$new.bytes) "$Label byte count changed: $($old.path)"
    [void](Parse-UtcTimestamp $old.last_write_utc "$Label timestamp: $($old.path)")
    Require ([string]$old.last_write_utc -ceq [string]$new.last_write_utc) "$Label timestamp changed: $($old.path)"
  }
}

function Read-PrebuildManifest {
  $strict = Read-StrictJson $PrebuildManifestPath 'N20 PREBUILD-MANIFEST.json'
  $manifest = $strict.value
  Require-ExactJsonPropertyNames $manifest @(
    'format_version', 'node', 'state', 'preparation_nonce', 'prepared_utc', 'gate', 'governance',
    'dependency_contracts', 'corrective_boundary', 'suite_baseline',
    'native_build_inputs', 'protected_repo_files', 'protected_configuration_policy',
    'git', 'scoped_diff', 'output_policy', 'later_node_specific_evidence_count',
    'n30_artifact_count'
  ) 'prebuild manifest'
  Require-JsonIntegerExact $manifest.format_version 2 'prebuild format_version'
  Require-JsonStringExact $manifest.node 'N20' 'prebuild node'
  Require-JsonStringExact $manifest.state 'prepared-before-official-builds' 'prebuild state'
  Require ($manifest.preparation_nonce -is [string] -and
      [string]$manifest.preparation_nonce -cmatch '^[0-9a-f]{32}$') `
      'prebuild preparation_nonce is invalid'
  $prepared = Parse-UtcTimestamp $manifest.prepared_utc 'prebuild prepared_utc'
  Require ($prepared -le [DateTimeOffset]::UtcNow) 'prebuild timestamp is in the future'
  Require-JsonIntegerExact $manifest.later_node_specific_evidence_count 0 'prebuild later-node evidence count'
  Require-JsonIntegerExact $manifest.n30_artifact_count 0 'prebuild N30 artifact count'
  Require-ExactJsonPropertyNames $manifest.output_policy @('generated_paths', 'audit_verdict_written', 'node_or_ledger_status_written') 'prebuild output policy'
  Require ($manifest.output_policy.generated_paths -is [Array]) 'prebuild generated_paths is not an array'
  $generatedPaths = @($manifest.output_policy.generated_paths)
  Require ($generatedPaths.Count -eq $VerifierOutputPaths.Count) 'prebuild generated path count changed'
  for ($index = 0; $index -lt $VerifierOutputPaths.Count; ++$index) {
    Require-JsonStringExact $generatedPaths[$index] $VerifierOutputPaths[$index] "prebuild generated path[$index]"
  }
  Require-JsonBooleanExact $manifest.output_policy.audit_verdict_written $false 'prebuild audit-verdict flag'
  Require-JsonBooleanExact $manifest.output_policy.node_or_ledger_status_written $false 'prebuild node/ledger flag'
  $manifest
}

function Assert-N20PendingState([object]$Manifest) {
  $strict = Read-StrictJson $EvidenceManifestPath 'N20 pending EVIDENCE-MANIFEST.json'
  $pending = $strict.value
  Require-ExactJsonPropertyNames $pending @(
    'format_version', 'node', 'state', 'preparation_nonce',
    'prebuild_manifest_sha256', 'updated_utc', 'reason',
    'runtime_evidence_verified', 'audit_verdict_written',
    'node_or_ledger_status_written'
  ) 'pending evidence manifest'
  Require-JsonIntegerExact $pending.format_version 2 'pending format_version'
  Require-JsonStringExact $pending.node 'N20' 'pending node'
  Require-JsonStringExact $pending.state 'pending-runtime-evidence' 'pending state'
  Require-JsonStringExact $pending.preparation_nonce ([string]$Manifest.preparation_nonce) `
      'pending preparation_nonce'
  Require-JsonStringExact $pending.prebuild_manifest_sha256 `
      (File-Hash $PrebuildManifestPath) 'pending prebuild_manifest_sha256'
  [void](Parse-UtcTimestamp $pending.updated_utc 'pending updated_utc')
  Require ($pending.reason -is [string] -and -not [string]::IsNullOrWhiteSpace($pending.reason)) `
      'pending reason is empty'
  Require-JsonBooleanExact $pending.runtime_evidence_verified $false `
      'pending runtime-evidence flag'
  Require-JsonBooleanExact $pending.audit_verdict_written $false `
      'pending audit-verdict flag'
  Require-JsonBooleanExact $pending.node_or_ledger_status_written $false `
      'pending node/ledger flag'
  $pending
}

function Assert-PreparationCurrent([object]$Manifest) {
  $gate = Assert-PreImplementationGate
  $governance = Assert-N20Governance
  $boundary = Assert-CorrectiveBoundary $gate
  Assert-DedicatedN20Registration
  $registeredTests = @(Get-RegisteredTests)
  Require-ExactJsonPropertyNames $Manifest.suite_baseline @(
    'completed_registered_tests', 'required_minimum_registered_tests',
    'expected_registered_tests', 'registered_test_names',
    'dedicated_n20_registration_count', 'retained_n20_23_registration_count'
  ) 'prebuild suite baseline'
  Require-JsonIntegerExact $Manifest.suite_baseline.completed_registered_tests $CompletedSuiteBaseline 'completed suite baseline'
  Require-JsonIntegerExact $Manifest.suite_baseline.required_minimum_registered_tests $RequiredMinimumRegisteredTests 'required suite minimum'
  Require-JsonIntegerExact $Manifest.suite_baseline.expected_registered_tests $registeredTests.Count 'prepared registered count'
  Require ($Manifest.suite_baseline.registered_test_names -is [Array]) 'prepared registered test names are not an array'
  Require ((@($Manifest.suite_baseline.registered_test_names) -join "`n") -ceq ($registeredTests -join "`n")) 'registered test names/order changed after preparation'
  Require-JsonIntegerExact $Manifest.suite_baseline.dedicated_n20_registration_count 1 'prepared dedicated N20 count'
  Require-JsonIntegerExact $Manifest.suite_baseline.retained_n20_23_registration_count 1 'prepared retained regression count'
  Require-EntrySetsCurrent @($Manifest.native_build_inputs) @(Get-InputManifestEntries) 'native build input'
  Require-EntrySetsCurrent @($Manifest.protected_repo_files) @(Get-ProtectedRepoEntries) 'protected repository file'
  $protectedPolicy = Assert-NoProtectedConfigurationPathChanged
  $git = Get-GitState
  Require ($git.head -ceq [string]$Manifest.git.head) 'Git HEAD changed after preparation'
  Require ($git.reference_log_fingerprint_sha256 -ceq [string]$Manifest.git.reference_log_fingerprint_sha256) 'Git reference logs changed after preparation'
  $scopedDiff = Get-ScopedDiffFacts
  Require ($scopedDiff.diff_sha256 -ceq [string]$Manifest.scoped_diff.diff_sha256) 'N20 scoped diff changed after preparation'
  Require ((File-Hash $PlanPath) -ceq [string]$Manifest.governance.approved_plan_sha256) 'approved plan changed after preparation'
  Require ((File-Hash $PlanAuditPath) -ceq [string]$Manifest.governance.plan_audit_sha256) 'plan audit changed after preparation'
  Require ((File-Hash $GatePath) -ceq [string]$Manifest.gate.file_sha256) 'pre-implementation gate changed after preparation'
  [pscustomobject][ordered]@{
    manifest = $Manifest
    gate = $gate
    governance = $governance
    corrective_boundary = $boundary
    registered_tests = $registeredTests
    protected_policy = $protectedPolicy
    git = $git
    scoped_diff = $scopedDiff
  }
}

function Get-EnvelopeValue([string[]]$Lines, [string]$Key, [string]$Label) {
  $matches = @($Lines | Where-Object { $_ -match ('^' + [regex]::Escape($Key) + '=') })
  Require ($matches.Count -eq 1) "$Label must contain exactly one $Key field"
  $matches[0].Substring($Key.Length + 1)
}

function Get-EnvelopeInteger([string[]]$Lines, [string]$Key, [string]$Label) {
  $value = Get-EnvelopeValue $Lines $Key $Label
  Require ($value -match '^(?:0|[1-9][0-9]*)$') "$Label $Key is not a canonical unsigned integer"
  try { [int64]::Parse($value, [Globalization.CultureInfo]::InvariantCulture) }
  catch { throw "N20 evidence requirement failed: $Label $Key is outside Int64" }
}

function Parse-Envelope([string]$Path, [string]$Label) {
  $full = Resolve-WorkspacePath $Path
  Require (-not $full.Equals([IO.Path]::GetFullPath($GatePath), [StringComparison]::OrdinalIgnoreCase)) "$Label overlaps the immutable gate"
  foreach ($output in @($PrebuildManifestPath, $EvidenceManifestPath, $ReportPath)) {
    Require (-not $full.Equals([IO.Path]::GetFullPath($output), [StringComparison]::OrdinalIgnoreCase)) "$Label overlaps a verifier-generated artifact"
  }
  Require (Test-Path -LiteralPath $full -PathType Leaf) "missing $Label"
  $logItem = Get-Item -LiteralPath $full -Force
  Require ([int64]$logItem.Length -le 32MB) "$Label exceeds the 32 MiB evidence limit"
  $text = Get-Content -Raw -LiteralPath $full
  Require (-not [string]::IsNullOrWhiteSpace($text)) "$Label is empty"
  Require ($text -notmatch '(?i)(?<![A-Za-z])(?:WSL|Docker)(?![A-Za-z])') "$Label is not a native-Windows-only record"
  Require ($text -notmatch '(?i)\bgit(?:[.]exe)?\s+(?:commit|push)\b') "$Label contains a forbidden Git mutation command"
  Require ($text -notmatch '(?i)(?<![A-Za-z0-9])N30(?![0-9])') "$Label contains an N30 reference"
  $lines = @($text -split '\r?\n' | Where-Object { $_ -ne '' })
  $started = Parse-UtcTimestamp (Get-EnvelopeValue $lines 'started_utc' $Label) "$Label started_utc"
  $ended = Parse-UtcTimestamp (Get-EnvelopeValue $lines 'ended_utc' $Label) "$Label ended_utc"
  Require ($started -lt $ended) "$Label interval is not positive"
  Require ($ended -le [DateTimeOffset]::UtcNow.AddMinutes(5)) "$Label timestamp is in the future"
  [pscustomobject]@{
    path = $full
    relative_path = Relative-Path $full
    sha256 = File-Hash $full
    bytes = [int64](Get-Item -LiteralPath $full).Length
    text = $text
    lines = $lines
    command = Get-EnvelopeValue $lines 'command' $Label
    started_utc = $started
    ended_utc = $ended
    exit_code = Get-EnvelopeInteger $lines 'exit_code' $Label
  }
}

function Format-N20CapturedLog([string]$Command, [object]$Capture, [string[]]$Metadata) {
  $lines = New-Object System.Collections.Generic.List[string]
  $lines.Add("command=$Command")
  $lines.Add("started_utc=$($Capture.started_utc.ToString('o'))")
  foreach ($line in $Metadata) { $lines.Add($line) }
  $lines.Add('stdout_begin')
  foreach ($line in @($Capture.stdout -split '\r?\n')) {
    if ($line -ne '') { $lines.Add($line) }
  }
  $lines.Add('stdout_end')
  $lines.Add('stderr_begin')
  foreach ($line in @($Capture.stderr -split '\r?\n')) {
    if ($line -ne '') { $lines.Add($line) }
  }
  $lines.Add('stderr_end')
  $lines.Add("ended_utc=$($Capture.ended_utc.ToString('o'))")
  $lines.Add("exit_code=$($Capture.exit_code)")
  $lines.ToArray()
}

function Get-N20IsolatedConfigEvidence([object]$Sandbox, [string]$Label) {
  $inventory = @(Get-N20SandboxInventory ([string]$Sandbox.path) ([string]$Sandbox.kind))
  $canonical = Join-Path ([string]$Sandbox.localappdata) 'Qbrain\config.json'
  $matches = New-Object System.Collections.Generic.List[string]
  foreach ($entry in $inventory) {
    if (-not [bool]$entry.is_container -and
        [IO.Path]::GetFileName([string]$entry.path).Equals(
            'config.json', [StringComparison]::OrdinalIgnoreCase)) {
      $matches.Add([string]$entry.path)
    }
  }
  Require ($matches.Count -le 1) "$Label created more than one config.json"
  if ($matches.Count -eq 0) {
    return [pscustomobject][ordered]@{ count=0; sha256='absent' }
  }
  Require ([IO.Path]::GetFullPath($matches[0]).Equals(
      [IO.Path]::GetFullPath($canonical), [StringComparison]::OrdinalIgnoreCase)) `
      "$Label created config.json outside the disposable canonical path"
  try {
    $config = Get-Content -Raw -LiteralPath $canonical | ConvertFrom-Json -ErrorAction Stop
  } catch {
    throw "N20 evidence requirement failed: $Label config.json is invalid JSON"
  }
  Require-ExactJsonPropertyNames $config @('brain_id', 'embedding', 'chat', 'search') "$Label config"
  Require ($config.brain_id -is [string] -and -not [string]::IsNullOrWhiteSpace($config.brain_id)) `
      "$Label config brain_id is invalid"
  Require-ExactJsonPropertyNames $config.embedding @('provider', 'model', 'base_url', 'dimensions') "$Label embedding config"
  Require ($config.embedding.provider -ceq 'openai' -and
      $config.embedding.model -ceq 'text-embedding-3-small' -and
      $config.embedding.base_url -ceq 'https://api.openai.com/v1') `
      "$Label embedding config differs from canonical defaults"
  Require-JsonIntegerExact $config.embedding.dimensions 1536 "$Label embedding dimensions"
  Require-ExactJsonPropertyNames $config.chat @('model', 'base_url') "$Label chat config"
  Require ($config.chat.model -ceq 'gpt-4o-mini' -and
      $config.chat.base_url -ceq 'https://api.openai.com/v1') `
      "$Label chat config differs from canonical defaults"
  Require-ExactJsonPropertyNames $config.search @('rrf_k', 'default_limit') "$Label search config"
  Require-JsonIntegerExact $config.search.rrf_k 60 "$Label search rrf_k"
  Require-JsonIntegerExact $config.search.default_limit 10 "$Label search default_limit"
  [pscustomobject][ordered]@{ count=1; sha256=File-Hash $canonical }
}

function Open-N20FrozenInputHandles([object[]]$Entries) {
  $handles = New-Object System.Collections.Generic.List[System.IO.FileStream]
  try {
    foreach ($entry in $Entries) {
      $path = Resolve-WorkspacePath ([string]$entry.path)
      Assert-PlainPathChain $path $true
      $handles.Add([IO.File]::Open(
          $path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read))
    }
    $handles.ToArray()
  } catch {
    foreach ($handle in $handles) { $handle.Dispose() }
    throw
  }
}

function Close-N20FrozenInputHandles([object[]]$Handles) {
  foreach ($handle in @($Handles)) {
    if ($null -ne $handle) { $handle.Dispose() }
  }
}

function Invoke-N20OfficialSequence([object]$PreparationState) {
  $prebuildHash = File-Hash $PrebuildManifestPath
  $handles = @(Open-N20FrozenInputHandles @($PreparationState.manifest.native_build_inputs))
  $frozenTestBinaryHash = $null
  try {
    $stages = @(
      [pscustomobject]@{
        kind='production'; path=$ProductionBuildLog
        command='powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cl.ps1'
        executable='powershell.exe'; arguments='-NoProfile -ExecutionPolicy Bypass -File scripts\build-cl.ps1'
        expected_marker='BUILD_OK'; expected_tests=0
      },
      [pscustomobject]@{
        kind='testbuild'; path=$TestBuildAndFirstSuiteLog
        command='powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1 -SkipProductionBuild'
        executable='powershell.exe'; arguments='-NoProfile -ExecutionPolicy Bypass -File scripts\build-tests-cl.ps1 -SkipProductionBuild'
        expected_marker='TESTS_BUILD_OK'; expected_tests=$PreparationState.registered_tests.Count
      },
      [pscustomobject]@{
        kind='suite2'; path=$SecondSuiteLog
        command='build\cl\qbrain_tests.exe'
        executable=$Tests; arguments=''; expected_marker='[PASS] n20'
        expected_tests=$PreparationState.registered_tests.Count
      }
    )
    foreach ($stage in $stages) {
      $sandbox = New-N20Sandbox ([string]$stage.kind)
      try {
        Assert-N20IsolatedLocalAppData ([string]$sandbox.localappdata) $sandbox
        if ($stage.kind -ceq 'suite2') {
          Require (Test-Path -LiteralPath $Tests -PathType Leaf) `
              'test binary is missing before official second suite'
          Require (-not [string]::IsNullOrWhiteSpace($frozenTestBinaryHash)) `
              'first full-suite test-binary hash was not frozen'
          Require ((File-Hash $Tests) -ceq $frozenTestBinaryHash) `
              'test binary changed between the two full-suite runs'
        }
        $capture = Invoke-CapturedProcess ([string]$stage.executable) ([string]$stage.arguments) `
            $TimeoutSeconds $false $null @{ LOCALAPPDATA=[string]$sandbox.localappdata }
        Require ($capture.exit_code -eq 0) "official $($stage.kind) child failed"
        foreach ($stream in @($capture.stdout, $capture.stderr)) {
          Require ($stream -notmatch '(?i)(?:api[_-]?key|secret|authorization|bearer)\s*[=:]') `
              "official $($stage.kind) child output contains a secret-like assignment"
        }
        Require ($capture.stdout -match ('(?m)^' + [regex]::Escape([string]$stage.expected_marker) + '\s*$')) `
            "official $($stage.kind) output lacks its success marker"
        if ($stage.kind -ceq 'production') {
          Require (Test-Path -LiteralPath $Qbrain -PathType Leaf) `
              'official production build did not create qbrain.exe'
        } elseif ($stage.kind -ceq 'testbuild') {
          Require (Test-Path -LiteralPath $Tests -PathType Leaf) `
              'official test build did not create qbrain_tests.exe'
          $frozenTestBinaryHash = File-Hash $Tests
          Require ($frozenTestBinaryHash -match '^[0-9a-f]{64}$') `
              'first full-suite test-binary hash is invalid'
        }
        $config = Get-N20IsolatedConfigEvidence $sandbox "official $($stage.kind)"
        $stageChildEnvironmentFailClosed = $capture.child_environment_policy -ceq $ChildEnvironmentPolicy
        $stageChildLocalAppDataIsolated = [bool]$capture.localappdata_overridden
        Require $stageChildEnvironmentFailClosed "official $($stage.kind) child environment policy differs"
        Require $stageChildLocalAppDataIsolated "official $($stage.kind) child LOCALAPPDATA is not isolated"
        $metadata = New-Object System.Collections.Generic.List[string]
        foreach ($line in @(
            'target_arch=x64', 'language_mode=/std:c++20',
            "isolated_localappdata=$($stageChildLocalAppDataIsolated.ToString().ToLowerInvariant())", "child_environment_policy=$($capture.child_environment_policy)",
            "all_child_environments_fail_closed=$($stageChildEnvironmentFailClosed.ToString().ToLowerInvariant())",
            "all_child_localappdata_isolated=$($stageChildLocalAppDataIsolated.ToString().ToLowerInvariant())",
            'production_data_access_telemetry=not-collected',
            "evidence_build_mutex_name=$N20EvidenceBuildMutexName",
            'evidence_build_mutex_held=true',
            "child_localappdata_id=$($sandbox.id)",
            "child_pid=$($capture.child_pid)",
            "blocked_ambient_count=$($capture.blocked_ambient_count)",
            "explicit_override_count=$($capture.explicit_override_count)",
            "child_executable_name=$($capture.child_executable_name)",
            "child_executable_sha256=$($capture.child_executable_sha256)",
            "child_executable_bytes=$($capture.child_executable_bytes)",
            "process_timeout_seconds=$($capture.process_timeout_seconds)",
            "output_capture_limit_bytes=$($capture.output_limit_bytes)",
            "tree_termination_attempted=$($capture.tree_termination_attempted.ToString().ToLowerInvariant())",
            "tree_termination_method=$($capture.tree_termination_method)",
            "preparation_nonce=$($PreparationState.manifest.preparation_nonce)",
            "prebuild_manifest_sha256=$prebuildHash")) {
          $metadata.Add($line)
        }
        if ([int]$stage.expected_tests -gt 0) {
          $metadata.Add("expected_registered_tests=$($stage.expected_tests)")
          $metadata.Add('isolated_test_config_policy=absent_or_canonical_defaults_only')
          $metadata.Add("isolated_test_config_count=$($config.count)")
          $metadata.Add("isolated_test_config_sha256=$($config.sha256)")
        }
        if ($stage.kind -in @('testbuild', 'suite2')) {
          $currentTestBinaryHash = File-Hash $Tests
          Require ($currentTestBinaryHash -ceq $frozenTestBinaryHash) `
              "official $($stage.kind) did not use the frozen first-suite test binary"
          $metadata.Add("binary_sha256=$currentTestBinaryHash")
        }
        $lines = Format-N20CapturedLog ([string]$stage.command) $capture $metadata.ToArray()
        Require ((($lines -join "`n").Length) -le 32MB) `
            "official $($stage.kind) log exceeds the evidence limit"
        Write-Utf8Lines ([string]$stage.path) $lines
        [void](Assert-PreparationCurrent $PreparationState.manifest)
      } finally {
        if ($null -ne $sandbox -and (Test-Path -LiteralPath ([string]$sandbox.path))) {
          Remove-N20SandboxSafely ([string]$sandbox.path) ([string]$sandbox.kind)
        }
      }
    }
  } finally {
    Close-N20FrozenInputHandles $handles
  }
}

function Assert-CompiledSources([string[]]$Lines, [string[]]$Sources, [string]$Label) {
  foreach ($source in $Sources) {
    $leaf = [IO.Path]::GetFileName($source)
    Require (@($Lines | Where-Object { $_ -ceq $leaf }).Count -eq 1) "$Label did not compile $leaf exactly once"
  }
}

function Require-LabelCount([hashtable]$Counts, [string]$Label, [int]$Minimum, [string]$EvidenceLabel) {
  $actual = 0
  if ($Counts.ContainsKey($Label)) { $actual = [int]$Counts[$Label] }
  Require ($actual -ge $Minimum) "$EvidenceLabel lacks snapshot label $Label (minimum $Minimum, observed $actual)"
}

function Get-ExpectedN20SnapshotLabels {
  $expectedLabels = New-Object System.Collections.Generic.List[string]
  foreach ($label in @(
      'builtin:list:no-create', 'builtin:get-active:no-create',
      'builtin:ontology:no-create', 'builtin:dimensions:no-create',
      'builtin:stats:no-create', 'builtin:reload:no-create',
      'builtin:list:byte-repeat')) {
    $expectedLabels.Add($label)
  }
  for ($index = 0; $index -lt 20; ++$index) { $expectedLabels.Add('id:reject') }
  $expectedLabels.Add('id:mixed-case-canonicalization')
  foreach ($label in @(
      'listing:selected:deterministic', 'listing:decoy:active',
      'shape:get-active:selected', 'shape:get-active:decoy',
      'shape:ontology:named', 'shape:dimensions:named',
      'shape:dimensions:valid-empty', 'shape:canonical-byte-repeat',
      'manifest:maximum-valid')) {
    $expectedLabels.Add($label)
  }
  $invalidManifestLabels = @(
    'malformed', 'trailing', 'non-object', 'duplicate-root-key', 'unknown-key',
    'missing-id', 'missing-name', 'missing-types', 'missing-dimensions',
    'wrong-id-type', 'id-mismatch', 'bom', 'excessive-nesting',
    'wrong-name-type', 'wrong-types-type', 'wrong-dimensions-type',
    'empty-name', 'long-name', 'long-version', 'empty-version', 'wrong-version-type',
    'empty-types', 'empty-type-member', 'long-type-member', 'reserved-type-member',
    'illegal-type-member', 'empty-dimension-member', 'long-dimension-member',
    'reserved-dimension-member', 'illegal-dimension-member',
    'too-many-types', 'too-many-dimensions', 'too-many-phases',
    'duplicate-type', 'duplicate-dimension', 'duplicate-phase',
    'wrong-phases-type', 'empty-phase', 'long-phase', 'non-string-phase',
    'non-string-type', 'non-string-dimension', 'invalid-utf8'
  )
  foreach ($manifestLabel in $invalidManifestLabels) {
    foreach ($operation in @('ontology_get', 'ontology_dimensions', 'reload_schema_pack')) {
      $expectedLabels.Add("manifest:${manifestLabel}:${operation}")
    }
    $expectedLabels.Add("manifest:${manifestLabel}:get-active")
  }
  foreach ($label in @(
      'filesystem:missing', 'filesystem:directory-candidate',
      'filesystem:symlink-candidate', 'filesystem:reparse-pack-root',
      'filesystem:reparse-qbrain-root', 'filesystem:size:1048576',
      'filesystem:size:1048577', 'filesystem:unreadable-locked',
      'filesystem:deterministic-first-error', 'filesystem:deterministic-first-error',
      'filesystem:deterministic-first-error', 'filesystem:case-collision',
      'filesystem:invalid-stem', 'filesystem:malformed-list',
      'filesystem:unrelated-files-ignored',
      'filesystem:noncanonical-named-lookup', 'filesystem:noncanonical-filename',
      'filesystem:noncanonical-extension-named-lookup',
      'filesystem:noncanonical-default:list', 'filesystem:noncanonical-default:active',
      'filesystem:noncanonical-default:ontology-omitted',
      'filesystem:noncanonical-default:ontology-named',
      'filesystem:noncanonical-default:dimensions-omitted',
      'filesystem:noncanonical-default:dimensions-named',
      'filesystem:noncanonical-default:reload',
      'enumeration:pack-count:256', 'enumeration:pack-count:257',
      'enumeration:entry-count:4096', 'enumeration:entry-count:4097',
      'active:missing-no-fallback', 'active:invalid-no-repair',
      'reload:remote-default-deny', 'reload:same-id:no-op',
      'reload:omitted-revalidate-no-op')) {
    $expectedLabels.Add($label)
  }
  for ($index = 0; $index -lt 3; ++$index) { $expectedLabels.Add('reload:failure:no-delta') }
  $expectedLabels.Add('reload:invalid-manifest:no-delta')
  $expectedLabels.Add('reload:oversized:no-delta')
  $expectedLabels.Add('reload:unsafe:no-delta')
  foreach ($label in @(
      'stats:cell:selected:default', 'stats:cell:selected:team',
      'stats:cell:decoy:default', 'stats:cell:decoy:team',
      'stats:source-omitted-limit-omitted', 'stats:local-mixed-case-source')) {
    $expectedLabels.Add($label)
  }
  for ($index = 0; $index -lt 5; ++$index) { $expectedLabels.Add('stats:limit:valid-clamped') }
  for ($index = 0; $index -lt 8; ++$index) { $expectedLabels.Add('stats:limit:reject-before-query') }
  for ($index = 0; $index -lt 5; ++$index) { $expectedLabels.Add('stats:source:reject-before-query') }
  foreach ($label in @(
      'stats:remote-default', 'stats:remote-denied',
      'stats:remote-allow-write-still-denied', 'stats:remote-allowlisted',
      'registry:tools-list')) {
    $expectedLabels.Add($label)
  }
  $readOperations = @(
    'list_schema_packs', 'get_active_schema_pack', 'schema_stats',
    'ontology_get', 'ontology_dimensions'
  )
  foreach ($operation in $readOperations) {
    $expectedLabels.Add("mcp:read-success:${operation}")
  }
  $expectedLabels.Add('mcp:reload-default-deny')
  $expectedLabels.Add('mcp:reload-explicit-allow-noop')
  $mcpOperations = @(
    'list_schema_packs', 'get_active_schema_pack', 'reload_schema_pack',
    'schema_stats', 'ontology_get', 'ontology_dimensions'
  )
  foreach ($operation in $mcpOperations) {
    $expectedLabels.Add("mcp:arguments:non-object:${operation}")
    $expectedLabels.Add("mcp:arguments:unexpected:${operation}")
    $expectedLabels.Add("mcp:arguments:overlong-field:${operation}")
  }
  foreach ($operation in @('reload_schema_pack', 'ontology_get', 'ontology_dimensions')) {
    for ($index = 0; $index -lt 6; ++$index) {
      $expectedLabels.Add("mcp:id-type:${operation}")
    }
  }
  for ($index = 0; $index -lt 6; ++$index) {
    $expectedLabels.Add('mcp:source-type:schema-stats')
  }
  for ($index = 0; $index -lt 7; ++$index) {
    $expectedLabels.Add('mcp:limit-type:schema-stats')
  }
  foreach ($label in @(
      'mcp:id-value:redacted', 'mcp:stats:source-denied',
      'mcp:stats:allow-write-denied', 'mcp:stats:source-allowlisted')) {
    $expectedLabels.Add($label)
  }
  foreach ($operation in $readOperations) {
    $expectedLabels.Add("mcp:ambient-excluded:${operation}")
  }
  $expectedLabels.Add('mcp:ambient-excluded:reload')
  foreach ($label in @(
      'reopen:populated:list', 'reopen:populated:active',
      'reopen:populated:reload', 'reopen:populated:stats',
      'reopen:populated:ontology', 'reopen:populated:dimensions',
      'reopen:populated:decoy-isolation')) {
    $expectedLabels.Add($label)
  }
  foreach ($label in @(
      'stats:damaged-type:utf8', 'stats:damaged-type:limit-plus-one',
      'stats:damaged-type:257',
      'stats:damaged-database:missing-config',
      'stats:damaged-database:missing-pages',
      'stats:damaged-database:missing-schema-version')) {
    $expectedLabels.Add($label)
  }
  $expectedLabels.ToArray()
}

function Get-ExpectedN20Summary([int]$SnapshotCount) {
  $facts = @(
    'schema_v12=pass', 'builtin_no_create=pass', 'pack_id_matrix=pass',
    'listing_shapes=pass', 'path_confinement=pass', 'filesystem_bounds=pass',
    'manifest_matrix=pass', 'exact_shapes=pass', 'selected_decoy=pass',
    'schema_stats=pass', 'source_authorization=pass', 'reload_delta=pass',
    'reload_gate=pass', 'registry=pass', 'mcp_typed=pass', 'mcp_rpc=pass',
    'ambient_excluded=pass', 'error_redaction=pass', 'snapshots=pass',
    'filename_case=pass', 'root_reparse=pass', 'manifest_types=pass',
    'deterministic_listing=pass', 'reload_failure_delta=pass',
    'populated_reopen=pass', 'mcp_single_error_block=pass', 'unique_root=pass'
  )
  '[INFO] n20 ' + ($facts -join ' ') +
      " snapshot_call_count=$SnapshotCount reload_delta_count=1"
}

function Assert-N20RuntimeEvidence([string[]]$Lines, [string]$EvidenceLabel) {
  $summaryLines = @($Lines | Where-Object {
      $_.StartsWith('[INFO] n20 ', [StringComparison]::Ordinal) -and
      -not $_.StartsWith('[INFO] n20 snapshot_call=', [StringComparison]::Ordinal) -and
      -not $_.StartsWith('[INFO] n20 reload_delta ', [StringComparison]::Ordinal)
    })
  Require ($summaryLines.Count -eq 1) "$EvidenceLabel must contain exactly one N20 summary marker"
  $summary = $summaryLines[0]
  $expectedLabels = @(Get-ExpectedN20SnapshotLabels)
  $snapshotCount = $expectedLabels.Count
  Require ($snapshotCount -eq $ExpectedN20SnapshotLabelCount) `
      'internal N20 expected-label sequence count changed'
  Require ((Text-Hash ($expectedLabels -join "`n")) -ceq
      $ExpectedN20SnapshotLabelsHash) `
      'internal N20 expected-label sequence hash changed'
  $expectedSummary = Get-ExpectedN20Summary $snapshotCount
  Require ($summary -ceq $expectedSummary) "$EvidenceLabel N20 summary marker is not exact"

  $snapshotLines = @($Lines | Where-Object { $_.StartsWith('[INFO] n20 snapshot_call=', [StringComparison]::Ordinal) })
  Require ($snapshotLines.Count -eq $snapshotCount) "$EvidenceLabel snapshot row count differs from snapshot_call_count"
  $labelCounts = @{}
  $rows = New-Object System.Collections.Generic.List[object]
  $rowPattern = '^\[INFO\] n20 snapshot_call=([1-9][0-9]*) label=([A-Za-z0-9_.:+-]+) selected_before_sha256=([0-9a-f]{64}) selected_after_sha256=([0-9a-f]{64}) decoy_before_sha256=([0-9a-f]{64}) decoy_after_sha256=([0-9a-f]{64}) filesystem_before_sha256=([0-9a-f]{64}) filesystem_after_sha256=([0-9a-f]{64})$'
  $distinctBrains = $false
  for ($index = 0; $index -lt $snapshotLines.Count; ++$index) {
    $match = [regex]::Match($snapshotLines[$index], $rowPattern)
    Require ($match.Success) "$EvidenceLabel contains a malformed N20 snapshot row"
    Require ([int]$match.Groups[1].Value -eq $index + 1) "$EvidenceLabel snapshot indexes are not contiguous"
    $label = $match.Groups[2].Value
    Require ($label -ceq $expectedLabels[$index]) "$EvidenceLabel snapshot label/order differs at row $($index + 1)"
    Require ($match.Groups[3].Value -ceq $match.Groups[4].Value) "$EvidenceLabel selected snapshot changed during $label"
    Require ($match.Groups[5].Value -ceq $match.Groups[6].Value) "$EvidenceLabel decoy snapshot changed during $label"
    Require ($match.Groups[7].Value -ceq $match.Groups[8].Value) "$EvidenceLabel filesystem snapshot changed during $label"
    if ($match.Groups[3].Value -cne $match.Groups[5].Value) { $distinctBrains = $true }
    if (-not $labelCounts.ContainsKey($label)) { $labelCounts[$label] = 0 }
    $labelCounts[$label] = [int]$labelCounts[$label] + 1
    $rows.Add([pscustomobject][ordered]@{
        index = $index + 1
        label = $label
        selected_sha256 = $match.Groups[4].Value
        decoy_sha256 = $match.Groups[6].Value
        filesystem_sha256 = $match.Groups[8].Value
      })
  }
  Require $distinctBrains "$EvidenceLabel never proves distinct selected and decoy brains"
  Require ($labelCounts.Count -eq (@($expectedLabels | Sort-Object -Unique)).Count) "$EvidenceLabel snapshot label set is not exact"

  $reloadLines = @($Lines | Where-Object { $_.StartsWith('[INFO] n20 reload_delta ', [StringComparison]::Ordinal) })
  Require ($reloadLines.Count -eq 1) "$EvidenceLabel must contain exactly one reload delta row"
  $reloadPattern = '^\[INFO\] n20 reload_delta label=reload:success:remote-allow-db-only selected_before_sha256=([0-9a-f]{64}) selected_after_sha256=([0-9a-f]{64}) selected_without_active_before_sha256=([0-9a-f]{64}) selected_without_active_after_sha256=([0-9a-f]{64}) decoy_before_sha256=([0-9a-f]{64}) decoy_after_sha256=([0-9a-f]{64}) filesystem_before_sha256=([0-9a-f]{64}) filesystem_after_sha256=([0-9a-f]{64}) old_id=alpha new_id=beta$'
  $reload = [regex]::Match($reloadLines[0], $reloadPattern)
  Require ($reload.Success) "$EvidenceLabel contains a malformed reload delta row"
  Require ($reload.Groups[1].Value -cne $reload.Groups[2].Value) "$EvidenceLabel reload did not change the selected active-pack snapshot"
  Require ($reload.Groups[3].Value -ceq $reload.Groups[4].Value) "$EvidenceLabel reload changed selected data outside schema.active_pack"
  Require ($reload.Groups[5].Value -ceq $reload.Groups[6].Value) "$EvidenceLabel reload changed the decoy brain"
  Require ($reload.Groups[7].Value -ceq $reload.Groups[8].Value) "$EvidenceLabel reload changed the filesystem"
  $deniedRows = @($rows | Where-Object { $_.label -ceq 'reload:remote-default-deny' })
  $sameIdRows = @($rows | Where-Object { $_.label -ceq 'reload:same-id:no-op' })
  Require ($deniedRows.Count -eq 1 -and $sameIdRows.Count -eq 1) "$EvidenceLabel lacks the reload boundary snapshot rows"
  Require ($reload.Groups[1].Value -ceq $deniedRows[0].selected_sha256) "$EvidenceLabel reload-before hash is not bound to the preceding denied read"
  Require ($reload.Groups[2].Value -ceq $sameIdRows[0].selected_sha256) "$EvidenceLabel reload-after hash is not bound to the following same-id read"
  Require ($reload.Groups[5].Value -ceq $deniedRows[0].decoy_sha256 -and
      $reload.Groups[6].Value -ceq $sameIdRows[0].decoy_sha256) "$EvidenceLabel reload decoy hashes are not bound to adjacent rows"
  Require ($reload.Groups[7].Value -ceq $deniedRows[0].filesystem_sha256 -and
      $reload.Groups[8].Value -ceq $sameIdRows[0].filesystem_sha256) "$EvidenceLabel reload filesystem hashes are not bound to adjacent rows"

  $populatedReopenLabels = @(
    'reopen:populated:list', 'reopen:populated:active',
    'reopen:populated:reload', 'reopen:populated:stats',
    'reopen:populated:ontology', 'reopen:populated:dimensions',
    'reopen:populated:decoy-isolation'
  )
  $populatedReopenRows = @($rows | Where-Object {
      $populatedReopenLabels -ccontains $_.label
    })
  Require ($populatedReopenRows.Count -eq $populatedReopenLabels.Count) "$EvidenceLabel lacks the exact populated-reopen marker matrix"
  for ($index = 0; $index -lt $populatedReopenLabels.Count; ++$index) {
    Require ($populatedReopenRows[$index].label -ceq $populatedReopenLabels[$index]) "$EvidenceLabel populated-reopen marker order differs at index $index"
  }
  $populatedReopenDigest = Text-Hash (($populatedReopenRows | ForEach-Object {
        "$($_.label)|$($_.selected_sha256)|$($_.decoy_sha256)|$($_.filesystem_sha256)"
      }) -join "`n")
  $schemaReopen = $summary.IndexOf(' populated_reopen=pass ', [StringComparison]::Ordinal) -ge 0 -and
      $populatedReopenRows.Count -eq $populatedReopenLabels.Count
  Require $schemaReopen "$EvidenceLabel does not bind populated reopen to the exact marker matrix"
  $schemaV12 = $summary.StartsWith('[INFO] n20 schema_v12=pass ', [StringComparison]::Ordinal)
  Require $schemaV12 "$EvidenceLabel lacks the schema-v12 marker"

  $finalRow = $rows[$rows.Count - 1]

  $n20Lines = @($summaryLines + $snapshotLines + $reloadLines)
  foreach ($line in $n20Lines) {
    Require ($line -notmatch '(?i)(?:[A-Z]:\\|\\\\|Volume\{)') "$EvidenceLabel N20 evidence leaks a Windows path"
    Require ($line -notmatch '(?i)(?<![A-Za-z0-9])N(?:2[1-9]|[3-9][0-9])(?![0-9])') "$EvidenceLabel uses a later-node marker as N20 evidence"
  }
  [pscustomobject][ordered]@{
    summary = $summary
    snapshot_call_count = $snapshotCount
    reload_delta_count = 1
    selected_snapshot_sha256 = $finalRow.selected_sha256
    decoy_snapshot_sha256 = $finalRow.decoy_sha256
    filesystem_snapshot_sha256 = $finalRow.filesystem_sha256
    labels = @($labelCounts.Keys | Sort-Object)
    snapshot_rows = $rows.ToArray()
    reload_delta_row = $reloadLines[0]
    schema_v12 = $schemaV12
    schema_reopen = $schemaReopen
    populated_reopen_labels = $populatedReopenLabels
    populated_reopen_sha256 = $populatedReopenDigest
    in_process_mcp_contract = $true
    registry_schema = $true
    read_snapshots_unchanged = $true
    changing_reload_selected_only = $true
  }
}

function Assert-TestResults([object]$Envelope, [string[]]$RegisteredTests, [string]$Label) {
  $rows = New-Object System.Collections.Generic.List[object]
  foreach ($line in $Envelope.lines) {
    $pass = [regex]::Match($line, '^\[PASS\]\s+([A-Za-z0-9_.-]+)\s*$')
    if ($pass.Success) {
      $rows.Add([pscustomobject]@{ name=$pass.Groups[1].Value; result='PASS' })
      continue
    }
    $fail = [regex]::Match($line, '^\[FAIL\]\s+([A-Za-z0-9_.-]+)(?::.*)?$')
    if ($fail.Success) { $rows.Add([pscustomobject]@{ name=$fail.Groups[1].Value; result='FAIL' }) }
  }
  Require ($rows.Count -eq $RegisteredTests.Count) "$Label result count is not the exact registered count"
  for ($index = 0; $index -lt $RegisteredTests.Count; ++$index) {
    Require ($rows[$index].name -ceq $RegisteredTests[$index]) "$Label test order/name differs from test_main.cpp at index $index"
  }
  Require ($Envelope.exit_code -eq 0) "$Label exit code is nonzero"
  Require (@($rows | Where-Object { $_.result -ceq 'FAIL' }).Count -eq 0) "$Label contains a failing test"
  Require (@($rows | Where-Object { $_.result -ceq 'PASS' }).Count -eq $RegisteredTests.Count) "$Label is not all PASS"
  Require (@($rows | Where-Object { $_.name -ceq 'n20' -and $_.result -ceq 'PASS' }).Count -eq 1) "$Label lacks exactly one dedicated n20 PASS"
  Require (@($rows | Where-Object { $_.name -ceq 'n20_23' -and $_.result -ceq 'PASS' }).Count -eq 1) "$Label lacks the retained n20_23 regression PASS"
  [pscustomobject][ordered]@{
    registered = $RegisteredTests.Count
    passed = $RegisteredTests.Count
    failed = 0
    n20 = Assert-N20RuntimeEvidence $Envelope.lines $Label
  }
}

function New-N20RpcRequest([int]$Id, [string]$Operation, [object]$Arguments) {
  $json = [ordered]@{
      jsonrpc = '2.0'
      id = $Id
      method = 'tools/call'
      params = [ordered]@{ name = $Operation; arguments = $Arguments }
    } | ConvertTo-Json -Depth 12 -Compress
  if ($Arguments -is [Array] -and @($Arguments).Count -eq 0) {
    Require ($json -cmatch '"arguments":\[\]') 'empty MCP arguments did not serialize as a JSON array'
  }
  $json + "`n"
}

function New-N20ToolsListRequest([int]$Id) {
  ([ordered]@{
      jsonrpc = '2.0'
      id = $Id
      method = 'tools/list'
      params = [ordered]@{}
    } | ConvertTo-Json -Depth 12 -Compress) + "`n"
}

function Get-ExpectedN20RealMcpProbes {
  $expected = New-Object System.Collections.Generic.List[object]
  foreach ($operation in @(
      'list_schema_packs', 'get_active_schema_pack', 'schema_stats',
      'ontology_get', 'ontology_dimensions')) {
    $expected.Add([pscustomobject][ordered]@{
        label = "read-success:$operation"
        operation = $operation
        allow_write = $false
      })
  }
  $expected.Add([pscustomobject][ordered]@{
      label = 'reload:default-deny'
      operation = 'reload_schema_pack'
      allow_write = $false
    })
  $expected.Add([pscustomobject][ordered]@{
      label = 'reload:explicit-allow'
      operation = 'reload_schema_pack'
      allow_write = $true
    })
  $expected.Add([pscustomobject][ordered]@{
      label = 'active:post-reload'
      operation = 'get_active_schema_pack'
      allow_write = $false
    })
  foreach ($operation in @(
      'list_schema_packs', 'get_active_schema_pack', 'reload_schema_pack',
      'schema_stats', 'ontology_get', 'ontology_dimensions')) {
    foreach ($case in @('non-object', 'unknown-field')) {
      $expected.Add([pscustomobject][ordered]@{
          label = "arguments:${case}:$operation"
          operation = $operation
          allow_write = $false
        })
    }
  }
  foreach ($operation in @('reload_schema_pack', 'ontology_get', 'ontology_dimensions')) {
    foreach ($case in @('null', 'integer')) {
      $expected.Add([pscustomobject][ordered]@{
          label = "id-type:${operation}:$case"
          operation = $operation
          allow_write = $false
        })
    }
  }
  foreach ($case in @('null', 'boolean')) {
    $expected.Add([pscustomobject][ordered]@{
        label = "source-type:schema_stats:$case"
        operation = 'schema_stats'
        allow_write = $false
      })
  }
  foreach ($case in @('signed', 'floating')) {
    $expected.Add([pscustomobject][ordered]@{
        label = "limit-type:schema_stats:$case"
        operation = 'schema_stats'
        allow_write = $false
      })
  }
  $expected.ToArray()
}

function Assert-N20RealMcpProbeMatrix([object[]]$Probes, [int]$FirstRequestId) {
  $expected = @(Get-ExpectedN20RealMcpProbes)
  Require ($expected.Count -eq 30) 'internal real stdio tools/call contract is not exactly 30 probes'
  Require ($Probes.Count -eq $expected.Count) 'real stdio tools/call matrix does not contain exactly 30 probes'
  Require (@($expected | ForEach-Object { $_.label } | Sort-Object -Unique).Count -eq $expected.Count) 'internal real stdio probe labels are not unique'
  for ($index = 0; $index -lt $expected.Count; ++$index) {
    $actual = $Probes[$index]
    $contract = $expected[$index]
    Require-JsonIntegerExact $actual.id ($FirstRequestId + $index) "real stdio probe $($index + 1) id"
    Require-JsonStringExact $actual.label $contract.label "real stdio probe $($index + 1) label"
    Require-JsonStringExact $actual.operation $contract.operation "real stdio probe $($index + 1) operation"
    Require-JsonBooleanExact $actual.allow_write $contract.allow_write "real stdio probe $($index + 1) allow_write"
    Require ($actual.snapshot_before_sha256 -is [string] -and
        [string]$actual.snapshot_before_sha256 -match '^[0-9a-f]{64}$') `
        "real stdio probe $($index + 1) lacks a bounded before snapshot hash"
    Require ($actual.snapshot_after_sha256 -is [string] -and
        [string]$actual.snapshot_after_sha256 -match '^[0-9a-f]{64}$') `
        "real stdio probe $($index + 1) lacks a bounded after snapshot hash"
    $expectedMutation = [string]$contract.label -ceq 'reload:explicit-allow'
    Require-JsonBooleanExact $actual.side_effect_unchanged (-not $expectedMutation) `
        "real stdio probe $($index + 1) side-effect snapshot result"
    if ($expectedMutation) {
      Require ([string]$actual.snapshot_before_sha256 -cne [string]$actual.snapshot_after_sha256) `
          'explicit allow-write reload did not change its snapshot hash'
    } else {
      Require ([string]$actual.snapshot_before_sha256 -ceq [string]$actual.snapshot_after_sha256) `
          "real stdio probe $($index + 1) changed an unchanged-side-effect snapshot"
    }
  }
}

function Assert-NoN20IdentityText([string]$Text, [string]$Label) {
  foreach ($identity in @([Environment]::UserName, $env:USERNAME) | Sort-Object -Unique) {
    if (-not [string]::IsNullOrWhiteSpace($identity) -and $identity.Length -ge 3) {
      Require ($Text.IndexOf($identity, [StringComparison]::OrdinalIgnoreCase) -lt 0) "$Label contains the current Windows account name"
    }
  }
}

function Invoke-N20StdioRawRequest(
  [string]$BrainId,
  [bool]$AllowWrite,
  [int]$Id,
  [string]$Label,
  [string]$Operation,
  [string]$RequestBody,
  [string]$IsolatedLocalAppData
) {
  $stdioSandbox = [pscustomobject]@{
    path = Split-Path -Parent $IsolatedLocalAppData
    kind = 'stdio'
  }
  Assert-N20IsolatedLocalAppData $IsolatedLocalAppData $stdioSandbox
  $serveArguments = "serve --brain $BrainId"
  if ($AllowWrite) { $serveArguments += ' --allow-write' }
  $environment = @{
    LOCALAPPDATA = $IsolatedLocalAppData
    QBRAIN_SOURCE = 'n20_ambient_forbidden'
    QBRAIN_MCP_ALLOW_WRITE = '0'
  }
  $result = Invoke-CapturedProcess $Qbrain $serveArguments 60 $false `
      $RequestBody $environment `
      @('QBRAIN_BRAIN', 'QBRAIN_MCP_TOKEN')
  Require ($result.child_environment_policy -ceq $ChildEnvironmentPolicy) `
      "real stdio $Label did not use the fail-closed child environment"
  Require $result.localappdata_overridden `
      "real stdio $Label did not receive isolated LOCALAPPDATA"
  Require ($result.exit_code -eq 0) "real stdio $Label failed"
  Require (-not [string]::IsNullOrWhiteSpace($result.stdout)) "real stdio $Label returned empty stdout"
  $responseLines = @($result.stdout -split '\r?\n' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  Require ($responseLines.Count -eq 1) "real stdio $Label returned more than one response"
  $response = ConvertFrom-StrictJsonText $responseLines[0] "real stdio $Label response"
  Require-ExactJsonPropertyNames $response @('jsonrpc', 'id', 'result') "real stdio $Label JSON-RPC response"
  Require-JsonStringExact $response.jsonrpc '2.0' "real stdio $Label jsonrpc"
  Require-JsonIntegerExact $response.id $Id "real stdio $Label id"
  Require ($result.stderr -match '\[qbrain-serve\] stdio MCP ready brain=') "real stdio $Label lacks server startup evidence"
  Require ($result.stderr -match '\[qbrain-serve\] shutdown: stdin EOF') "real stdio $Label lacks clean EOF shutdown evidence"
  Require (($AllowWrite -and $result.stderr -match 'write=ENABLED') -or
      (-not $AllowWrite -and $result.stderr -match 'write=disabled')) "real stdio $Label write-mode evidence differs"
  Require ($result.stderr -notmatch '(?i)(?:[A-Z]:\\|\\\\|Volume\{|Administrator|api[_-]?key|bearer\s+)') "real stdio $Label stderr leaks sensitive data"
  Assert-NoN20IdentityText $result.stderr "real stdio $Label stderr"
  [pscustomobject][ordered]@{
    id = $Id
    label = $Label
    operation = $Operation
    allow_write = $AllowWrite
    command = "build\cl\qbrain.exe $serveArguments"
    exit_code = $result.exit_code
    child_environment_policy = $result.child_environment_policy
    localappdata_isolated = [bool]$result.localappdata_overridden
    child_pid = [int]$result.child_pid
    child_executable_name = [string]$result.child_executable_name
    child_executable_sha256 = [string]$result.child_executable_sha256
    child_executable_bytes = [int64]$result.child_executable_bytes
    blocked_ambient_count = [int]$result.blocked_ambient_count
    explicit_override_count = [int]$result.explicit_override_count
    process_timeout_seconds = [int]$result.process_timeout_seconds
    output_capture_limit_bytes = [int64]$result.output_limit_bytes
    tree_termination_attempted = [bool]$result.tree_termination_attempted
    response = $response
    response_sha256 = Text-Hash $responseLines[0]
  }
}

function Invoke-N20StdioRawRequestWithSnapshot(
  [string]$BrainId,
  [bool]$AllowWrite,
  [int]$Id,
  [string]$Label,
  [string]$Operation,
  [string]$RequestBody,
  [string]$IsolatedLocalAppData,
  [bool]$ExpectMutation = $false
) {
  $sandbox = Split-Path -Parent $IsolatedLocalAppData
  $before = Get-N20SandboxSnapshot $sandbox 'stdio'
  $probe = Invoke-N20StdioRawRequest $BrainId $AllowWrite $Id $Label $Operation `
      $RequestBody $IsolatedLocalAppData
  $after = Get-N20SandboxSnapshot $sandbox 'stdio'
  $unchanged = $before.sha256 -ceq $after.sha256
  if ($ExpectMutation) {
    Require (-not $unchanged) "real stdio $Label did not produce its expected side-effect snapshot delta"
  } else {
    Require $unchanged "real stdio $Label changed its isolated filesystem snapshot"
  }
  $probe | Add-Member -NotePropertyName snapshot_before_sha256 -NotePropertyValue ([string]$before.sha256)
  $probe | Add-Member -NotePropertyName snapshot_after_sha256 -NotePropertyValue ([string]$after.sha256)
  $probe | Add-Member -NotePropertyName snapshot_before_entry_count -NotePropertyValue ([int]$before.entry_count)
  $probe | Add-Member -NotePropertyName snapshot_after_entry_count -NotePropertyValue ([int]$after.entry_count)
  $probe | Add-Member -NotePropertyName side_effect_unchanged -NotePropertyValue ([bool]$unchanged)
  $probe
}

function Invoke-N20StdioCall(
  [string]$BrainId,
  [bool]$AllowWrite,
  [int]$Id,
  [string]$Operation,
  [object]$Arguments,
  [string]$IsolatedLocalAppData,
  [string]$ProbeLabel = ''
) {
  if ([string]::IsNullOrWhiteSpace($ProbeLabel)) { $ProbeLabel = "tools/call:$Operation" }
  Invoke-N20StdioRawRequest $BrainId $AllowWrite $Id $ProbeLabel $Operation `
      (New-N20RpcRequest $Id $Operation $Arguments) $IsolatedLocalAppData
}

function Invoke-N20StdioCallWithSnapshot(
  [string]$BrainId,
  [bool]$AllowWrite,
  [int]$Id,
  [string]$Operation,
  [object]$Arguments,
  [string]$IsolatedLocalAppData,
  [string]$ProbeLabel,
  [bool]$ExpectMutation = $false
) {
  Invoke-N20StdioRawRequestWithSnapshot $BrainId $AllowWrite $Id $ProbeLabel $Operation `
      (New-N20RpcRequest $Id $Operation $Arguments) $IsolatedLocalAppData $ExpectMutation
}

function Read-N20McpContent([object]$Probe, [bool]$ExpectedError, [string]$ExpectedCode = '', [string]$ExpectedField = '') {
  $label = "real stdio $($Probe.operation)"
  Require-ExactJsonPropertyNames $Probe.response.result @('content', 'isError') "$label result"
  Require-JsonBooleanExact $Probe.response.result.isError $ExpectedError "$label isError"
  Require ($Probe.response.result.content -is [Array]) "$label content is not an array"
  $content = @($Probe.response.result.content)
  Require ($content.Count -eq 1) "$label must contain exactly one text block"
  Require-ExactJsonPropertyNames $content[0] @('type', 'text') "$label content[0]"
  Require-JsonStringExact $content[0].type 'text' "$label content type"
  Require ($content[0].text -is [string] -and $content[0].text.Length -gt 0) "$label text is empty"
  $payload = ConvertFrom-StrictJsonText $content[0].text "$label structured content"
  if ($ExpectedError) {
    Require-ExactJsonPropertyNames $payload @('error') "$label error payload"
    Require-ExactJsonPropertyNames $payload.error @('code', 'field', 'message') "$label structured error"
    Require-JsonStringExact $payload.error.code $ExpectedCode "$label error code"
    Require-JsonStringExact $payload.error.field $ExpectedField "$label error field"
    Require ($payload.error.message -is [string] -and $payload.error.message.Length -in 1..512) "$label error message is not bounded"
  }
  $payload
}

function Get-ExpectedN20ToolContracts {
  @(
    [pscustomobject][ordered]@{ name='list_schema_packs'; properties=[string[]]@() },
    [pscustomobject][ordered]@{ name='get_active_schema_pack'; properties=[string[]]@() },
    [pscustomobject][ordered]@{ name='reload_schema_pack'; properties=[string[]]@('id') },
    [pscustomobject][ordered]@{ name='schema_stats'; properties=[string[]]@('source_id', 'limit') },
    [pscustomobject][ordered]@{ name='ontology_get'; properties=[string[]]@('id') },
    [pscustomobject][ordered]@{ name='ontology_dimensions'; properties=[string[]]@('id') }
  )
}

function Assert-N20IdentifierToolSchema([object]$Schema, [bool]$HasDefault, [string]$Label) {
  $keys = @('type', 'minLength', 'maxLength', 'pattern')
  if ($HasDefault) { $keys += 'default' }
  Require-ExactJsonPropertyNames $Schema $keys $Label
  Require-JsonStringExact $Schema.type 'string' "$Label type"
  Require-JsonIntegerExact $Schema.minLength 1 "$Label minLength"
  Require-JsonIntegerExact $Schema.maxLength 64 "$Label maxLength"
  Require-JsonStringExact $Schema.pattern '^[A-Za-z0-9_-]+$' "$Label pattern"
  if ($HasDefault) {
    Require-JsonStringExact $Schema.default 'default' "$Label default"
  }
}

function New-N20ToolSchemaFixture([string[]]$PropertyNames) {
  $properties = [ordered]@{}
  foreach ($name in @($PropertyNames)) {
    if ($name -ceq 'id') {
      $properties[$name] = [pscustomobject][ordered]@{
        type = 'string'; minLength = 1; maxLength = 64; pattern = '^[A-Za-z0-9_-]+$'
      }
    } elseif ($name -ceq 'source_id') {
      $properties[$name] = [pscustomobject][ordered]@{
        type = 'string'; minLength = 1; maxLength = 64
        pattern = '^[A-Za-z0-9_-]+$'; default = 'default'
      }
    } elseif ($name -ceq 'limit') {
      $properties[$name] = [pscustomobject][ordered]@{
        type = 'integer'; minimum = 0; maximum = 256; default = 100
      }
    } else {
      throw "N20 evidence requirement failed: unknown parser fixture property $name"
    }
  }
  [pscustomobject][ordered]@{
    type = 'object'
    additionalProperties = $false
    properties = [pscustomobject]$properties
  }
}

function Assert-N20RealToolsList([object]$Probe) {
  Require-ExactJsonPropertyNames $Probe.response.result @('tools') 'real stdio tools/list result'
  Require ($Probe.response.result.tools -is [Array]) 'real stdio tools/list tools is not an array'
  $contracts = @(Get-ExpectedN20ToolContracts)
  foreach ($contract in $contracts) {
    $name = [string]$contract.name
    $matches = @($Probe.response.result.tools | Where-Object { $_.name -ceq $name })
    Require ($matches.Count -eq 1) "real stdio tools/list does not contain exactly one $name"
    $tool = $matches[0]
    Require-ExactJsonPropertyNames $tool @('name', 'description', 'inputSchema') "real stdio tools/list $name"
    Require-JsonStringExact $tool.name $name "real stdio tools/list $name name"
    Require ($tool.description -is [string] -and $tool.description.Length -in 1..512) "real stdio tools/list $name description is not bounded"
    $description = $tool.description.ToLowerInvariant()
    Require ($description.Contains('schema') -or $description.Contains('pack') -or
        $description.Contains('ontology')) "real stdio tools/list $name description lacks N20 domain context"
    foreach ($overclaim in @(
        'full parity', 'cache invalidation', 'entity ontology',
        'bi-temporal', 'semantic inference')) {
      Require (-not $description.Contains($overclaim)) "real stdio tools/list $name description overclaims $overclaim"
    }
    $schema = $tool.inputSchema
    Require-ExactJsonPropertyNames $schema @('type', 'additionalProperties', 'properties') "real stdio tools/list $name schema"
    Require-JsonStringExact $schema.type 'object' "real stdio tools/list $name schema type"
    Require-JsonBooleanExact $schema.additionalProperties $false "real stdio tools/list $name additionalProperties"
    $propertyNames = [string[]]@($contract.properties)
    Require-ExactJsonPropertyNames -Value $schema.properties -Expected $propertyNames `
        -Label "real stdio tools/list $name properties"
    if ($propertyNames -contains 'id') {
      Assert-N20IdentifierToolSchema $schema.properties.id $false "real stdio tools/list $name id"
    }
    if ($propertyNames -contains 'source_id') {
      Assert-N20IdentifierToolSchema $schema.properties.source_id $true "real stdio tools/list $name source_id"
    }
    if ($propertyNames -contains 'limit') {
      $limit = $schema.properties.limit
      Require-ExactJsonPropertyNames $limit @('type', 'minimum', 'maximum', 'default') "real stdio tools/list $name limit"
      Require-JsonStringExact $limit.type 'integer' "real stdio tools/list $name limit type"
      Require-JsonIntegerExact $limit.minimum 0 "real stdio tools/list $name limit minimum"
      Require-JsonIntegerExact $limit.maximum 256 "real stdio tools/list $name limit maximum"
      Require-JsonIntegerExact $limit.default 100 "real stdio tools/list $name limit default"
    }
  }
  @($contracts | ForEach-Object { $_.name })
}

function Assert-N20PackPayload([object]$Payload, [string]$ExpectedId, [string]$ExpectedOrigin, [int]$ExpectedDimensionCount, [string]$Label) {
  Require-ExactJsonPropertyNames $Payload @('id', 'origin', 'pack') $Label
  Require-JsonStringExact $Payload.id $ExpectedId "$Label id"
  Require-JsonStringExact $Payload.origin $ExpectedOrigin "$Label origin"
  Require-ExactJsonPropertyNames $Payload.pack @('id', 'name', 'types', 'dimensions', 'phases') "$Label pack"
  Require-JsonStringExact $Payload.pack.id $ExpectedId "$Label pack id"
  Require ($Payload.pack.name -is [string] -and $Payload.pack.name.Length -in 1..256) "$Label pack name is not bounded"
  Require ($Payload.pack.types -is [Array] -and @($Payload.pack.types).Count -in 1..256) "$Label pack types are not bounded"
  Require ($Payload.pack.dimensions -is [Array] -and @($Payload.pack.dimensions).Count -eq $ExpectedDimensionCount) "$Label pack dimensions are not bounded"
  Require ($Payload.pack.phases -is [Array] -and @($Payload.pack.phases).Count -le 64) "$Label pack phases are not bounded"
}

function Invoke-N20IsolatedDoctor(
  [string]$BrainId,
  [string]$IsolatedLocalAppData,
  [string]$Label,
  [bool]$RequireUnchangedSnapshot
) {
  $sandbox = Split-Path -Parent $IsolatedLocalAppData
  $stdioSandbox = [pscustomobject]@{ path=$sandbox; kind='stdio' }
  Assert-N20IsolatedLocalAppData $IsolatedLocalAppData $stdioSandbox
  $before = Get-N20SandboxSnapshot $sandbox 'stdio'
  $doctorCommand = "doctor --brain $BrainId --json"
  $doctorResult = Invoke-CapturedProcess -FilePath $Qbrain -Arguments $doctorCommand `
      -TimeoutSeconds 60 -EnvironmentOverrides @{ LOCALAPPDATA=$IsolatedLocalAppData } `
      -RemoveEnvironmentVariables @('QBRAIN_BRAIN', 'QBRAIN_SOURCE', 'QBRAIN_MCP_ALLOW_WRITE', 'QBRAIN_MCP_TOKEN')
  Require ($doctorResult.child_environment_policy -ceq $ChildEnvironmentPolicy) `
      "$Label did not use the fail-closed child environment"
  Require $doctorResult.localappdata_overridden `
      "$Label did not receive isolated LOCALAPPDATA"
  Require ($doctorResult.exit_code -eq 0) "$Label failed"
  Require ([string]::IsNullOrWhiteSpace($doctorResult.stderr)) "$Label wrote to stderr"
  Require ($doctorResult.stdout -notmatch '(?i)(?:api[_-]?key|secret|authorization|bearer)\s*[=:]') `
      "$Label output contains a secret-like assignment"
  $doctorText = $doctorResult.stdout.Trim()
  Require ($doctorText -notmatch '(?i)(?:[A-Z]:\\|\\\\|Volume\{|Administrator|api[_-]?key\s*[=:]|bearer\s+)') "$Label response leaks sensitive data"
  Assert-NoN20IdentityText $doctorText "$Label response"
  Require ($doctorText -notmatch '(?i)(?<![A-Za-z0-9])N(?:2[1-9]|[3-9][0-9])(?![0-9])') "$Label response contains a later-node marker"
  $doctorJson = ConvertFrom-StrictJsonText $doctorText "$Label response"
  Require (Has-JsonProperty $doctorJson 'ok') "$Label lacks ok"
  Require (Has-JsonProperty $doctorJson 'schema_version') "$Label lacks schema_version"
  Require-JsonBooleanExact $doctorJson.ok $true "$Label ok"
  Require-JsonIntegerExact $doctorJson.schema_version 12 "$Label schema_version"
  $after = Get-N20SandboxSnapshot $sandbox 'stdio'
  $unchanged = $before.sha256 -ceq $after.sha256
  if ($RequireUnchangedSnapshot) {
    Require $unchanged "$Label changed its isolated filesystem snapshot"
  }
  [pscustomobject][ordered]@{
    command = "build\\cl\\qbrain.exe $doctorCommand"
    exit_code = $doctorResult.exit_code
    response_sha256 = Text-Hash $doctorText
    ok = [bool]$doctorJson.ok
    schema_version = [int64]$doctorJson.schema_version
    child_environment_policy = [string]$doctorResult.child_environment_policy
    localappdata_isolated = [bool]$doctorResult.localappdata_overridden
    child_pid = [int]$doctorResult.child_pid
    child_executable_name = [string]$doctorResult.child_executable_name
    child_executable_sha256 = [string]$doctorResult.child_executable_sha256
    child_executable_bytes = [int64]$doctorResult.child_executable_bytes
    blocked_ambient_count = [int]$doctorResult.blocked_ambient_count
    explicit_override_count = [int]$doctorResult.explicit_override_count
    process_timeout_seconds = [int]$doctorResult.process_timeout_seconds
    output_capture_limit_bytes = [int64]$doctorResult.output_limit_bytes
    tree_termination_attempted = [bool]$doctorResult.tree_termination_attempted
    snapshot_before_sha256 = [string]$before.sha256
    snapshot_after_sha256 = [string]$after.sha256
    side_effect_unchanged = [bool]$unchanged
  }
}

function Assert-N20RealMcp([object]$PreparationState, [object]$ProductionBinary) {
  Require ((File-Hash $Qbrain) -ceq [string]$ProductionBinary.sha256) 'real MCP production-binary binding is stale'
  Require ([string]$PreparationState.governance.approved_plan_sha256 -ceq $GateExpected.approved_plan_sha256) 'real MCP approved-plan binding differs'
  $sandboxState = New-N20Sandbox 'stdio'
  $sandbox = [string]$sandboxState.path
  $isolatedLocalAppData = [string]$sandboxState.localappdata
  $nonce = ([string]$sandboxState.id).Substring('qbrain_n20_stdio_'.Length)
  $brainId = "n20mcp$($nonce.Substring(0, 16))"
  $probes = New-Object System.Collections.Generic.List[object]
  $toolsListProbe = $null
  $initializationDoctorEvidence = $null
  $doctorEvidence = $null
  try {
    Assert-N20IsolatedLocalAppData $isolatedLocalAppData $sandboxState
    $initializationDoctorEvidence = Invoke-N20IsolatedDoctor $brainId `
        $isolatedLocalAppData 'initial isolated qbrain doctor --json' $false
    $requestId = 20200
    $toolsListProbe = Invoke-N20StdioRawRequestWithSnapshot $brainId $false $requestId `
        'tools/list' 'tools/list' (New-N20ToolsListRequest $requestId) $isolatedLocalAppData $false
    ++$requestId
    [void](Assert-N20RealToolsList $toolsListProbe)
    foreach ($operation in @(
        'list_schema_packs', 'get_active_schema_pack', 'schema_stats',
        'ontology_get', 'ontology_dimensions')) {
      $probes.Add((Invoke-N20StdioCallWithSnapshot $brainId $false $requestId $operation `
          ([pscustomobject]@{}) $isolatedLocalAppData "read-success:$operation" $false))
      ++$requestId
    }
    $qbrainDirectory = Join-Path $isolatedLocalAppData 'Qbrain'
    Require (Test-Path -LiteralPath $qbrainDirectory -PathType Container) `
        'real MCP isolated Qbrain directory is missing'
    $qbrainItem = Get-N20SandboxItemNoReparse $qbrainDirectory $sandbox
    Require ($qbrainItem.PSIsContainer) 'real MCP isolated Qbrain path is not a directory'
    $packDirectory = Assert-N20SandboxItemPath (Join-Path $qbrainDirectory 'schema-packs') $sandbox
    if (-not (Test-Path -LiteralPath $packDirectory)) {
      New-Item -ItemType Directory -Path $packDirectory | Out-Null
    }
    $packDirectoryItem = Get-N20SandboxItemNoReparse $packDirectory $sandbox
    Require ($packDirectoryItem.PSIsContainer) 'real MCP pack fixture path is not a directory'
    Write-N20SandboxUtf8Text (Join-Path $packDirectory 'alpha.json') `
        '{"id":"alpha","name":"N20 stdio alpha","types":["note"],"dimensions":["topic"],"phases":[]}' `
        $sandbox
    [void](Get-N20SandboxInventory $sandbox 'stdio')
    $probes.Add((Invoke-N20StdioCallWithSnapshot $brainId $false $requestId 'reload_schema_pack' `
        ([pscustomobject]@{ id='alpha' }) $isolatedLocalAppData 'reload:default-deny' $false))
    ++$requestId
    $probes.Add((Invoke-N20StdioCallWithSnapshot $brainId $true $requestId 'reload_schema_pack' `
        ([pscustomobject]@{ id='alpha' }) $isolatedLocalAppData 'reload:explicit-allow' $true))
    ++$requestId
    $probes.Add((Invoke-N20StdioCallWithSnapshot $brainId $false $requestId 'get_active_schema_pack' `
        ([pscustomobject]@{}) $isolatedLocalAppData 'active:post-reload' $false))
    ++$requestId

    $list = Read-N20McpContent $probes[0] $false
    Require-ExactJsonPropertyNames $list @('active_id', 'packs') 'real stdio list_schema_packs payload'
    Require-JsonStringExact $list.active_id 'default' 'real stdio list active_id'
    Require ($list.packs -is [Array] -and @($list.packs).Count -eq 1) 'real stdio list pack count differs'
    Require-ExactJsonPropertyNames $list.packs[0] @('id', 'origin', 'active') 'real stdio list pack row'
    Require-JsonStringExact $list.packs[0].id 'default' 'real stdio list pack id'
    Require-JsonStringExact $list.packs[0].origin 'builtin' 'real stdio list pack origin'
    Require-JsonBooleanExact $list.packs[0].active $true 'real stdio list active flag'

    $active = Read-N20McpContent $probes[1] $false
    Assert-N20PackPayload $active 'default' 'builtin' 3 'real stdio get_active_schema_pack payload'
    $stats = Read-N20McpContent $probes[2] $false
    Require-ExactJsonPropertyNames $stats @(
      'source_id', 'active_pack_id', 'schema_version', 'total_active_pages',
      'type_counts', 'truncated') 'real stdio schema_stats payload'
    Require-JsonStringExact $stats.source_id 'default' 'real stdio schema_stats source_id'
    Require-JsonStringExact $stats.active_pack_id 'default' 'real stdio schema_stats active_pack_id'
    Require-JsonIntegerExact $stats.schema_version 12 'real stdio schema_stats schema_version'
    Require-JsonIntegerExact $stats.total_active_pages 0 'real stdio schema_stats total_active_pages'
    Require ($stats.type_counts -is [Array] -and @($stats.type_counts).Count -eq 0) 'real stdio schema_stats type_counts is not empty'
    Require-JsonBooleanExact $stats.truncated $false 'real stdio schema_stats truncated'

    $ontology = Read-N20McpContent $probes[3] $false
    Assert-N20PackPayload $ontology 'default' 'builtin' 3 'real stdio ontology_get payload'
    $dimensions = Read-N20McpContent $probes[4] $false
    Require-ExactJsonPropertyNames $dimensions @('id', 'dimensions') 'real stdio ontology_dimensions payload'
    Require-JsonStringExact $dimensions.id 'default' 'real stdio ontology_dimensions id'
    Require ($dimensions.dimensions -is [Array] -and @($dimensions.dimensions).Count -eq 3) 'real stdio ontology_dimensions count differs'

    [void](Read-N20McpContent $probes[5] $true 'write_denied' 'operation')
    $allowedReload = Read-N20McpContent $probes[6] $false
    Require-ExactJsonPropertyNames $allowedReload @('id', 'changed') 'real stdio allowed reload payload'
    Require-JsonStringExact $allowedReload.id 'alpha' 'real stdio allowed reload id'
    Require-JsonBooleanExact $allowedReload.changed $true 'real stdio allowed reload changed'
    $activeAfterReload = Read-N20McpContent $probes[7] $false
    Assert-N20PackPayload $activeAfterReload 'alpha' 'installed' 1 'real stdio active-after-reload payload'

    $operations = @(
      'list_schema_packs', 'get_active_schema_pack', 'reload_schema_pack',
      'schema_stats', 'ontology_get', 'ontology_dimensions'
    )
    foreach ($operation in $operations) {
      $probe = Invoke-N20StdioCallWithSnapshot $brainId $false $requestId $operation `
          ([object[]]@()) $isolatedLocalAppData "arguments:non-object:$operation" $false
      $probes.Add($probe)
      ++$requestId
      [void](Read-N20McpContent $probe $true 'invalid_argument' 'arguments')

      $probe = Invoke-N20StdioCallWithSnapshot $brainId $false $requestId $operation `
          ([pscustomobject]@{ unexpected='N20_UNKNOWN_FIELD_VALUE_SENTINEL' }) `
          $isolatedLocalAppData "arguments:unknown-field:$operation" $false
      $probes.Add($probe)
      ++$requestId
      [void](Read-N20McpContent $probe $true 'invalid_argument' 'unexpected')
    }

    foreach ($operation in @('reload_schema_pack', 'ontology_get', 'ontology_dimensions')) {
      foreach ($case in @(
          [pscustomobject]@{ label='null'; value=$null },
          [pscustomobject]@{ label='integer'; value=[int]7 })) {
        $probe = Invoke-N20StdioCallWithSnapshot $brainId $false $requestId $operation `
            ([pscustomobject]@{ id=$case.value }) $isolatedLocalAppData `
            "id-type:${operation}:$($case.label)" $false
        $probes.Add($probe)
        ++$requestId
        [void](Read-N20McpContent $probe $true 'invalid_argument' 'id')
      }
    }

    foreach ($case in @(
        [pscustomobject]@{ label='null'; value=$null },
        [pscustomobject]@{ label='boolean'; value=$true })) {
      $probe = Invoke-N20StdioCallWithSnapshot $brainId $false $requestId 'schema_stats' `
          ([pscustomobject]@{ source_id=$case.value }) $isolatedLocalAppData `
          "source-type:schema_stats:$($case.label)" $false
      $probes.Add($probe)
      ++$requestId
      [void](Read-N20McpContent $probe $true 'invalid_argument' 'source_id')
    }

    foreach ($case in @(
        [pscustomobject]@{ label='signed'; value=[int]-1 },
        [pscustomobject]@{ label='floating'; value=[double]1.5 })) {
      $probe = Invoke-N20StdioCallWithSnapshot $brainId $false $requestId 'schema_stats' `
          ([pscustomobject]@{ limit=$case.value }) $isolatedLocalAppData `
          "limit-type:schema_stats:$($case.label)" $false
      $probes.Add($probe)
      ++$requestId
      [void](Read-N20McpContent $probe $true 'invalid_argument' 'limit')
    }

    foreach ($probe in $probes) {
      $serialized = $probe.response | ConvertTo-Json -Depth 20 -Compress
      Require ($serialized.Length -le 2000000) "real stdio $($probe.operation) response exceeds evidence bound"
      Require ($serialized -notmatch '(?i)(?:[A-Z]:\\|\\\\|Volume\{|Administrator|api[_-]?key|bearer\s+)') "real stdio $($probe.operation) response leaks sensitive data"
      Assert-NoN20IdentityText $serialized "real stdio $($probe.label) response"
      Require ($serialized -notmatch '(?i)n20_ambient_forbidden') "real stdio $($probe.operation) consumed ambient QBRAIN_SOURCE"
    }
    Assert-N20RealMcpProbeMatrix ($probes.ToArray()) 20201
    $toolsSerialized = $toolsListProbe.response | ConvertTo-Json -Depth 20 -Compress
    Require ($toolsSerialized.Length -le 2000000) 'real stdio tools/list response exceeds evidence bound'
    Require ($toolsSerialized -notmatch '(?i)(?:[A-Z]:\\|\\\\|Volume\{|Administrator|api[_-]?key|bearer\s+)') 'real stdio tools/list response leaks sensitive data'
    Assert-NoN20IdentityText $toolsSerialized 'real stdio tools/list response'
    Require (Test-Path -LiteralPath (Join-Path $isolatedLocalAppData 'Qbrain\schema-packs\alpha.json') -PathType Leaf) 'real stdio explicit allow-write fixture disappeared unexpectedly'
    Assert-N20IsolatedLocalAppData $isolatedLocalAppData $sandboxState
    $doctorEvidence = Invoke-N20IsolatedDoctor $brainId $isolatedLocalAppData `
        'final isolated qbrain doctor --json' $true
    Require ((File-Hash $Qbrain) -ceq [string]$ProductionBinary.sha256) 'production binary changed during real stdio MCP probes'
  } finally {
    if (Test-Path -LiteralPath $sandbox) {
      Remove-N20SandboxSafely $sandbox 'stdio'
    }
  }
  Require (-not (Test-Path -LiteralPath $sandbox)) 'real stdio MCP sandbox was not removed'
  Require ($null -ne $toolsListProbe) 'real stdio tools/list evidence was not captured'
  Require ($null -ne $initializationDoctorEvidence) 'initial isolated doctor evidence was not captured'
  Require ($null -ne $doctorEvidence) 'final isolated doctor evidence was not captured'
  Require ([bool]$toolsListProbe.side_effect_unchanged) 'tools/list changed its side-effect snapshot'
  Require ([bool]$doctorEvidence.side_effect_unchanged) 'final isolated doctor changed its side-effect snapshot'
  $childRecords = @($initializationDoctorEvidence, $toolsListProbe) +
      @($probes.ToArray()) + @($doctorEvidence)
  $allChildEnvironmentsFailClosed = @($childRecords | Where-Object {
      $_.child_environment_policy -ceq $ChildEnvironmentPolicy
    }).Count -eq $childRecords.Count
  $allChildLocalAppDataIsolated = @($childRecords | Where-Object {
      [bool]$_.localappdata_isolated
    }).Count -eq $childRecords.Count
  $allChildOutputBoundsObserved = @($childRecords | Where-Object {
      [int64]$_.output_capture_limit_bytes -eq [int64]$N20ProcessOutputLimitBytes -and
      -not [bool]$_.tree_termination_attempted
    }).Count -eq $childRecords.Count
  $allProbeSnapshots = @($toolsListProbe) + @($probes.ToArray())
  $unchangedProbeSnapshots = @($allProbeSnapshots | Where-Object {
      [bool]$_.side_effect_unchanged
    })
  $changedProbeSnapshots = @($allProbeSnapshots | Where-Object {
      -not [bool]$_.side_effect_unchanged
    })
  Require ($allProbeSnapshots.Count -eq $probes.Count + 1) 'real stdio probe snapshot count differs'
  Require ($changedProbeSnapshots.Count -eq 1 -and
      [string]$changedProbeSnapshots[0].label -ceq 'reload:explicit-allow') `
      'real stdio side-effect matrix has an unexpected mutation probe'
  [pscustomobject][ordered]@{
    transport = 'stdio-ndjson'
    brain_id = $brainId
    child_environment_policy = $ChildEnvironmentPolicy
    child_localappdata_id = [string]$sandboxState.id
    child_process_count = $childRecords.Count
    all_child_environments_fail_closed = $allChildEnvironmentsFailClosed
    all_child_localappdata_isolated = $allChildLocalAppDataIsolated
    all_child_output_bounds_observed = $allChildOutputBoundsObserved
    isolated_localappdata = $allChildLocalAppDataIsolated
    sandbox_removed = (-not (Test-Path -LiteralPath $sandbox))
    production_data_access_telemetry = 'not-collected'
    operations = @(@('tools/list') + @($probes | ForEach-Object { $_.operation }))
    probe_labels = @(@('tools/list') + @($probes | ForEach-Object { $_.label }))
    commands = @(@($initializationDoctorEvidence.command) + @($toolsListProbe.command) + @($probes | ForEach-Object { $_.command }) + @($doctorEvidence.command))
    response_sha256 = @(@($toolsListProbe.response_sha256) + @($probes | ForEach-Object { $_.response_sha256 }))
    child_processes = @($childRecords | ForEach-Object {
        [pscustomobject][ordered]@{
          command = $_.command
          child_pid = $_.child_pid
          executable_name = $_.child_executable_name
          executable_sha256 = $_.child_executable_sha256
          executable_bytes = $_.child_executable_bytes
          child_environment_policy = $_.child_environment_policy
          localappdata_isolated = $_.localappdata_isolated
          blocked_ambient_count = $_.blocked_ambient_count
          explicit_override_count = $_.explicit_override_count
          process_timeout_seconds = $_.process_timeout_seconds
          output_capture_limit_bytes = $_.output_capture_limit_bytes
          tree_termination_attempted = $_.tree_termination_attempted
        }
      })
    calls = @(@([pscustomobject][ordered]@{
          id = $toolsListProbe.id
          label = $toolsListProbe.label
          operation = $toolsListProbe.operation
          allow_write = $toolsListProbe.allow_write
          command = $toolsListProbe.command
          exit_code = $toolsListProbe.exit_code
          response_sha256 = $toolsListProbe.response_sha256
          snapshot_before_sha256 = $toolsListProbe.snapshot_before_sha256
          snapshot_after_sha256 = $toolsListProbe.snapshot_after_sha256
          side_effect_unchanged = $toolsListProbe.side_effect_unchanged
          child_pid = $toolsListProbe.child_pid
          blocked_ambient_count = $toolsListProbe.blocked_ambient_count
          explicit_override_count = $toolsListProbe.explicit_override_count
          child_executable_sha256 = $toolsListProbe.child_executable_sha256
          response = $toolsListProbe.response
        }) + @($probes | ForEach-Object {
        [pscustomobject][ordered]@{
          id = $_.id
          label = $_.label
          operation = $_.operation
          allow_write = $_.allow_write
          command = $_.command
          exit_code = $_.exit_code
          response_sha256 = $_.response_sha256
          snapshot_before_sha256 = $_.snapshot_before_sha256
          snapshot_after_sha256 = $_.snapshot_after_sha256
          side_effect_unchanged = $_.side_effect_unchanged
          child_pid = $_.child_pid
          blocked_ambient_count = $_.blocked_ambient_count
          explicit_override_count = $_.explicit_override_count
          child_executable_sha256 = $_.child_executable_sha256
          response = $_.response
        }
      }))
    response_count = $probes.Count + 1
    side_effect_snapshot_pair_count = $allProbeSnapshots.Count
    side_effect_unchanged_pair_count = $unchangedProbeSnapshots.Count
    side_effect_changed_labels = @($changedProbeSnapshots | ForEach-Object { $_.label })
    per_probe_side_effects_recorded = ($unchangedProbeSnapshots.Count + $changedProbeSnapshots.Count -eq $allProbeSnapshots.Count)
    tools_list = $true
    read_success_count = @($probes | Where-Object {
        $_.response.result.isError -eq $false -and
        $_.operation -in @('list_schema_packs', 'get_active_schema_pack', 'schema_stats', 'ontology_get', 'ontology_dimensions')
      }).Count
    reload_default_deny = ($probes[5].response.result.isError -eq $true)
    reload_explicit_allow = ($probes[6].response.result.isError -eq $false)
    structured_error_count = @($probes | Where-Object { $_.response.result.isError -eq $true }).Count
    single_structured_text_block = (@($probes | Where-Object { @($_.response.result.content).Count -ne 1 }).Count -eq 0)
    ambient_source_excluded = (@($probes | Where-Object {
        (($_.response | ConvertTo-Json -Depth 20 -Compress) -match '(?i)n20_ambient_forbidden')
      }).Count -eq 0)
    doctor = $doctorEvidence
    schema_version = $doctorEvidence.schema_version
    binary_sha256 = $ProductionBinary.sha256
  }
}

function Assert-CanonicalBuildEnvelope(
  [object]$Envelope,
  [string]$ExpectedCommand,
  [string]$Label,
  [int]$ExpectedTests,
  [string]$ExpectedLocalAppDataRole
) {
  Require ($Envelope.command -ceq $ExpectedCommand) "$Label command is not canonical"
  Require ((Get-EnvelopeValue $Envelope.lines 'target_arch' $Label) -ceq 'x64') "$Label target is not x64"
  Require ((Get-EnvelopeValue $Envelope.lines 'language_mode' $Label) -ceq '/std:c++20') "$Label language mode is not C++20"
  Require ((Get-EnvelopeValue $Envelope.lines 'isolated_localappdata' $Label) -ceq 'true') "$Label lacks isolated LOCALAPPDATA evidence"
  Require ((Get-EnvelopeValue $Envelope.lines 'child_environment_policy' $Label) -ceq $ChildEnvironmentPolicy) "$Label child environment policy differs"
  Require ((Get-EnvelopeValue $Envelope.lines 'all_child_environments_fail_closed' $Label) -ceq 'true') "$Label has a child outside the fail-closed policy"
  Require ((Get-EnvelopeValue $Envelope.lines 'all_child_localappdata_isolated' $Label) -ceq 'true') "$Label has a child without isolated LOCALAPPDATA"
  Require ((Get-EnvelopeValue $Envelope.lines 'production_data_access_telemetry' $Label) -ceq 'not-collected') `
      "$Label makes an unsupported production-data access claim"
  Require ((Get-EnvelopeValue $Envelope.lines 'evidence_build_mutex_name' $Label) -ceq $N20EvidenceBuildMutexName) `
      "$Label evidence/build mutex name differs"
  Require ((Get-EnvelopeValue $Envelope.lines 'evidence_build_mutex_held' $Label) -ceq 'true') `
      "$Label was not recorded inside the evidence/build critical section"
  $localAppDataId = Get-EnvelopeValue $Envelope.lines 'child_localappdata_id' $Label
  $expectedIdPattern = '^qbrain_n20_' + [regex]::Escape($ExpectedLocalAppDataRole) + '_[0-9a-f]{32}$'
  Require ($localAppDataId -cmatch $expectedIdPattern) "$Label child LOCALAPPDATA id is not unique and canonical"
  Require (@($Envelope.lines | Where-Object { $_ -match "Environment initialized for: 'x64'" }).Count -eq 1) "$Label lacks one x64 vcvars evidence line"
  if ($ExpectedLocalAppDataRole -cne 'suite2') {
    Require (@($Envelope.lines | Where-Object { $_ -match '(?i)Visual Studio [0-9]+' }).Count -ge 1) "$Label lacks a Visual Studio compiler banner"
  }
  Require ($Envelope.exit_code -eq 0) "$Label exit code is nonzero"
  $childPid = Get-EnvelopeInteger $Envelope.lines 'child_pid' $Label
  Require ($childPid -gt 0) "$Label child PID is not positive"
  $blockedAmbientCount = Get-EnvelopeInteger $Envelope.lines 'blocked_ambient_count' $Label
  $explicitOverrideCount = Get-EnvelopeInteger $Envelope.lines 'explicit_override_count' $Label
  $childExecutableName = Get-EnvelopeValue $Envelope.lines 'child_executable_name' $Label
  Require ($childExecutableName -match '^[A-Za-z0-9_.-]+$') "$Label child executable name is not bounded"
  $childExecutableHash = Get-EnvelopeValue $Envelope.lines 'child_executable_sha256' $Label
  Require ($childExecutableHash -match '^[0-9a-f]{64}$') "$Label child executable hash is invalid"
  Require ((Get-EnvelopeInteger $Envelope.lines 'child_executable_bytes' $Label) -gt 0) `
      "$Label child executable byte count is not positive"
  Require ((Get-EnvelopeInteger $Envelope.lines 'process_timeout_seconds' $Label) -gt 0) `
      "$Label child timeout is not positive"
  Require ((Get-EnvelopeInteger $Envelope.lines 'output_capture_limit_bytes' $Label) -eq $N20ProcessOutputLimitBytes) `
      "$Label output capture limit differs"
  Require ((Get-EnvelopeValue $Envelope.lines 'tree_termination_attempted' $Label) -ceq 'false') `
      "$Label unexpectedly required tree termination"
  Require ((Get-EnvelopeValue $Envelope.lines 'tree_termination_method' $Label) -ceq 'not-needed') `
      "$Label tree termination provenance differs"
  if ($ExpectedTests -gt 0) {
    Require ((Get-EnvelopeInteger $Envelope.lines 'expected_registered_tests' $Label) -eq $ExpectedTests) "$Label expected registered-test count differs from preparation"
  }
  [pscustomobject][ordered]@{
    role = $ExpectedLocalAppDataRole
    child_environment_policy = $ChildEnvironmentPolicy
    child_localappdata_id = $localAppDataId
    child_pid = [int64]$childPid
    blocked_ambient_count = [int64]$blockedAmbientCount
    explicit_override_count = [int64]$explicitOverrideCount
    child_executable_name = $childExecutableName
    child_executable_sha256 = $childExecutableHash
    child_executable_bytes = Get-EnvelopeInteger $Envelope.lines 'child_executable_bytes' $Label
    process_timeout_seconds = Get-EnvelopeInteger $Envelope.lines 'process_timeout_seconds' $Label
    output_capture_limit_bytes = Get-EnvelopeInteger $Envelope.lines 'output_capture_limit_bytes' $Label
    localappdata_isolated = ((Get-EnvelopeValue $Envelope.lines 'isolated_localappdata' $Label) -ceq 'true')
    all_child_environments_fail_closed = ((Get-EnvelopeValue $Envelope.lines 'all_child_environments_fail_closed' $Label) -ceq 'true')
    all_child_localappdata_isolated = ((Get-EnvelopeValue $Envelope.lines 'all_child_localappdata_isolated' $Label) -ceq 'true')
    production_data_access_telemetry = 'not-collected'
  }
}

function Assert-OfficialBuildEvidence([object]$PreparationState) {
  $prepared = Parse-UtcTimestamp $PreparationState.manifest.prepared_utc 'prebuild prepared_utc'
  $production = Parse-Envelope $ProductionBuildLog 'official production build log'
  $testBuild = Parse-Envelope $TestBuildAndFirstSuiteLog 'official test-build and first-suite log'
  $secondSuite = Parse-Envelope $SecondSuiteLog 'official second-suite log'
  $expectedPrebuildHash = File-Hash $PrebuildManifestPath
  foreach ($entry in @(
      [pscustomobject]@{ Envelope=$production; Label='official production build' },
      [pscustomobject]@{ Envelope=$testBuild; Label='official test build' },
      [pscustomobject]@{ Envelope=$secondSuite; Label='official second suite' })) {
    Require ((Get-EnvelopeValue $entry.Envelope.lines 'preparation_nonce' $entry.Label) -ceq
        [string]$PreparationState.manifest.preparation_nonce) `
        "$($entry.Label) preparation nonce differs from the pending state"
    Require ((Get-EnvelopeValue $entry.Envelope.lines 'prebuild_manifest_sha256' $entry.Label) -ceq
        $expectedPrebuildHash) `
        "$($entry.Label) prebuild hash differs from the pending state"
  }
  $productionEnvironment = Assert-CanonicalBuildEnvelope $production `
      'powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cl.ps1' `
      'official production build' 0 'production'
  $testBuildEnvironment = Assert-CanonicalBuildEnvelope $testBuild `
      'powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1 -SkipProductionBuild' `
      'official test build' $PreparationState.registered_tests.Count 'testbuild'
  $secondSuiteEnvironment = Assert-CanonicalBuildEnvelope $secondSuite `
      'build\cl\qbrain_tests.exe' 'official second suite' `
      $PreparationState.registered_tests.Count 'suite2'
  $childLocalAppDataIds = @(
    $productionEnvironment.child_localappdata_id,
    $testBuildEnvironment.child_localappdata_id,
    $secondSuiteEnvironment.child_localappdata_id
  )
  Require (@($childLocalAppDataIds | Sort-Object -Unique).Count -eq 3) `
      'official build and suite child LOCALAPPDATA roots are not unique'
  Require (@($production.lines | Where-Object { $_ -ceq 'BUILD_OK' }).Count -eq 1) 'production build log lacks exactly one BUILD_OK'
  Require (@($testBuild.lines | Where-Object { $_ -ceq 'TESTS_BUILD_OK' }).Count -eq 1) 'test-build log lacks exactly one TESTS_BUILD_OK'
  foreach ($envelope in @($testBuild, $secondSuite)) {
    Require ((Get-EnvelopeValue $envelope.lines 'isolated_test_config_policy' 'native test suite') -ceq 'absent_or_canonical_defaults_only') 'native test suite config policy is not fail-closed'
    $configCount = Get-EnvelopeInteger $envelope.lines 'isolated_test_config_count' 'native test suite'
    $configHash = Get-EnvelopeValue $envelope.lines 'isolated_test_config_sha256' 'native test suite'
    Require ($configCount -in @(0, 1)) 'native test suite isolated config count is outside 0..1'
    Require (($configCount -eq 0 -and $configHash -ceq 'absent') -or ($configCount -eq 1 -and $configHash -match '^[0-9a-f]{64}$')) 'native test suite isolated config fingerprint is invalid'
  }
  Require ((Get-EnvelopeValue $testBuild.lines 'isolated_test_config_count' 'official test build') -ceq (Get-EnvelopeValue $secondSuite.lines 'isolated_test_config_count' 'official second suite')) 'the two test runs have different isolated config counts'
  Require ((Get-EnvelopeValue $testBuild.lines 'isolated_test_config_sha256' 'official test build') -ceq (Get-EnvelopeValue $secondSuite.lines 'isolated_test_config_sha256' 'official second suite')) 'the two test runs have different isolated config hashes'
  Require ($production.started_utc -ge $prepared) 'production build started before PREBUILD-MANIFEST.json'
  Require ($testBuild.started_utc -ge $production.ended_utc) 'test build did not start after production build'
  Require ($secondSuite.started_utc -ge $testBuild.ended_utc) 'second suite did not start after the first suite'
  Assert-CompiledSources $production.lines (@(Get-QuotedSourceArray $BuildScript 'productionSources') + @('sqlite3.c')) 'official production build'
  Assert-CompiledSources $testBuild.lines @(Get-QuotedSourceArray $TestBuildScript 'defaultTestSources') 'official test build'
  Require (@($testBuild.lines | Where-Object { $_ -ceq 'test_n20.cpp' }).Count -eq 1) 'official test build did not compile dedicated test_n20.cpp exactly once'
  Require (Test-Path -LiteralPath $Qbrain -PathType Leaf) 'production qbrain.exe is missing after official build'
  Require (Test-Path -LiteralPath $Tests -PathType Leaf) 'qbrain_tests.exe is missing after official build'
  $qbrainItem = Get-Item -LiteralPath $Qbrain
  $testItem = Get-Item -LiteralPath $Tests
  Require ($qbrainItem.LastWriteTimeUtc -ge $production.started_utc.AddMinutes(-2).UtcDateTime -and $qbrainItem.LastWriteTimeUtc -le $production.ended_utc.AddMinutes(2).UtcDateTime) 'production binary timestamp is outside the official production-build interval'
  Require ($testItem.LastWriteTimeUtc -ge $testBuild.started_utc.AddMinutes(-2).UtcDateTime -and $testItem.LastWriteTimeUtc -le $testBuild.ended_utc.AddMinutes(2).UtcDateTime) 'test binary timestamp is outside the official test-build interval'
  Require ((File-Hash $Qbrain) -cne $PreparationState.gate.qbrain_sha256) 'production binary still equals the pre-implementation gate binary'
  $firstResults = Assert-TestResults $testBuild $PreparationState.registered_tests 'official first full suite'
  $secondResults = Assert-TestResults $secondSuite $PreparationState.registered_tests 'official second full suite'
  $firstSuiteBinaryHash = Get-EnvelopeValue $testBuild.lines 'binary_sha256' 'official first full suite'
  $secondSuiteBinaryHash = Get-EnvelopeValue $secondSuite.lines 'binary_sha256' 'official second suite'
  Require ($firstSuiteBinaryHash -match '^[0-9a-f]{64}$') 'first-suite binary_sha256 is invalid'
  Require ($secondSuiteBinaryHash -match '^[0-9a-f]{64}$') 'second-suite binary_sha256 is invalid'
  Require ($firstSuiteBinaryHash -ceq $secondSuiteBinaryHash) 'the two full-suite runs used different qbrain_tests.exe hashes'
  Require ($secondSuiteBinaryHash -ceq (File-Hash $Tests)) 'frozen full-suite binary hash does not bind the current test binary'
  Require ($firstResults.n20.snapshot_call_count -eq $secondResults.n20.snapshot_call_count) 'the two all-pass runs have different N20 snapshot counts'
  Require ($firstResults.n20.labels.Count -eq $secondResults.n20.labels.Count) 'the two all-pass runs have different N20 label counts'
  foreach ($label in $firstResults.n20.labels) {
    Require ($secondResults.n20.labels -contains $label) "second suite is missing N20 label $label"
  }
  Require (($firstResults.n20.populated_reopen_labels -join "`n") -ceq
      ($secondResults.n20.populated_reopen_labels -join "`n")) 'the two all-pass runs have different populated-reopen label contracts'
  Require ($firstResults.n20.schema_reopen -and $secondResults.n20.schema_reopen) 'one full-suite run lacks populated-reopen evidence'
  foreach ($line in @($testBuild.text, $secondSuite.text)) {
    Require ($line -notmatch '(?i)(?:api[_-]?key|secret|authorization|bearer)\s*[=:]') 'native suite log contains a secret-like assignment'
  }
  [pscustomobject][ordered]@{
    production = $production
    test_build = $testBuild
    second_suite = $secondSuite
    first_results = $firstResults
    second_results = $secondResults
    frozen_test_binary_sha256 = $firstSuiteBinaryHash
    production_binary = Get-FileEntry $Qbrain 'production-binary'
    test_binary = Get-FileEntry $Tests 'test-binary'
    first_log = Get-FileEntry $testBuild.path 'official-test-and-first-suite-log'
    second_log = Get-FileEntry $secondSuite.path 'official-second-suite-log'
    production_log = Get-FileEntry $production.path 'official-production-build-log'
    child_environments = @(
      $productionEnvironment,
      $testBuildEnvironment,
      $secondSuiteEnvironment
    )
  }
}

function Get-PlatformEvidence {
  Require ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) 'verifier is not running on native Windows'
  Require ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -eq 'X64') 'Windows OS architecture is not x64'
  Require ([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString() -eq 'X64') 'PowerShell process architecture is not x64'
  $vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
  Require (Test-Path -LiteralPath $vcvars -PathType Leaf) 'MSVC vcvarsall.bat is missing'
  $args = '/d /c "call ^"' + $vcvars + '^" x64 >nul && cl 2>&1"'
  $platformSandbox = New-N20Sandbox 'platform'
  try {
    Assert-N20IsolatedLocalAppData `
        ([string]$platformSandbox.localappdata) $platformSandbox
    $capture = Invoke-CapturedProcess 'cmd.exe' $args 120 $false $null `
        @{ LOCALAPPDATA=[string]$platformSandbox.localappdata } @()
    Require ($capture.child_environment_policy -ceq $ChildEnvironmentPolicy) `
        'compiler query did not use the fail-closed child environment'
    Require $capture.localappdata_overridden `
        'compiler query did not receive isolated LOCALAPPDATA'
  } finally {
    if (Test-Path -LiteralPath ([string]$platformSandbox.path)) {
      Remove-N20SandboxSafely ([string]$platformSandbox.path) 'platform'
    }
  }
  Require ($capture.exit_code -eq 0) 'x64 cl.exe version query failed'
  $compilerLines = @($capture.stdout -split '\r?\n' | Where-Object { $_ -match '(?i)Compiler Version .+ for x64' })
  Require ($compilerLines.Count -eq 1) 'full x64 cl.exe version was not captured'
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
    compiler = $compilerLines[0].Trim()
    compiler_query_child_environment_policy = [string]$capture.child_environment_policy
    compiler_query_child_localappdata_id = [string]$platformSandbox.id
    compiler_query_child_executable_name = [string]$capture.child_executable_name
    compiler_query_child_executable_sha256 = [string]$capture.child_executable_sha256
    compiler_query_child_executable_bytes = [int64]$capture.child_executable_bytes
    compiler_query_child_pid = [int]$capture.child_pid
    compiler_query_blocked_ambient_count = [int]$capture.blocked_ambient_count
    compiler_query_explicit_override_count = [int]$capture.explicit_override_count
    compiler_query_process_timeout_seconds = [int]$capture.process_timeout_seconds
    compiler_query_output_capture_limit_bytes = [int64]$capture.output_limit_bytes
    compiler_query_tree_termination_attempted = [bool]$capture.tree_termination_attempted
    compiler_query_localappdata_isolated = [bool]$capture.localappdata_overridden
    compiler_query_production_data_access_telemetry = 'not-collected'
  }
}

function Assert-OutputPolicy {
  Assert-EvidenceDirectory
  foreach ($relative in $VerifierOutputPaths) {
    $absolute = Resolve-WorkspacePath $relative
    Require ($absolute.StartsWith([IO.Path]::GetFullPath($EvidenceDir).TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) "verifier output escapes the N20 evidence directory: $relative"
    if (Test-Path -LiteralPath $absolute) {
      $item = Get-Item -LiteralPath $absolute -Force
      Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "verifier output is a reparse point: $relative"
    }
    Assert-N20SpecificArtifactPaths @($relative)
  }
  Require (-not ([IO.Path]::GetFullPath($GatePath)).Equals([IO.Path]::GetFullPath($ReportPath), [StringComparison]::OrdinalIgnoreCase)) 'VERIFY-REPORT path overlaps the immutable gate'
}

function New-EvidenceManifest([object]$PreparationState, [object]$Builds, [object]$Platform, [object]$RealMcp) {
  $runtimeFirst = $Builds.first_results.n20
  $runtimeSecond = $Builds.second_results.n20
  $allChildRecords = @($Builds.child_environments) + @($RealMcp.child_processes) + @(
      [pscustomobject][ordered]@{
        child_environment_policy = $Platform.compiler_query_child_environment_policy
        localappdata_isolated = $Platform.compiler_query_localappdata_isolated
        output_capture_limit_bytes = $Platform.compiler_query_output_capture_limit_bytes
        tree_termination_attempted = $Platform.compiler_query_tree_termination_attempted
      }
    )
  $allChildEnvironmentsFailClosed = @($allChildRecords | Where-Object {
      $_.child_environment_policy -ceq $ChildEnvironmentPolicy
    }).Count -eq $allChildRecords.Count
  $allChildLocalAppDataIsolated = @($allChildRecords | Where-Object {
      [bool]$_.localappdata_isolated
    }).Count -eq $allChildRecords.Count
  $allChildOutputBoundsObserved = @($allChildRecords | Where-Object {
      [int64]$_.output_capture_limit_bytes -eq [int64]$N20ProcessOutputLimitBytes -and
      -not [bool]$_.tree_termination_attempted
    }).Count -eq $allChildRecords.Count
  $deliverables = New-Object System.Collections.Generic.List[object]
  foreach ($relative in $N20DeliverablePaths) {
    $deliverables.Add((Get-FileEntry (Join-Path $Root $relative) 'n20-deliverable'))
  }
  $evidenceFiles = @(
    Get-FileEntry $PrebuildManifestPath 'prebuild-manifest'
    $Builds.production_log
    $Builds.first_log
    $Builds.second_log
  )
  Assert-N20SpecificArtifactPaths @($deliverables | ForEach-Object { $_.path })
  Assert-N20SpecificArtifactPaths @($evidenceFiles | ForEach-Object { $_.path })
  [pscustomobject][ordered]@{
    format_version = 2
    node = 'N20'
    state = 'runtime-evidence-verified'
    preparation_nonce = [string]$PreparationState.manifest.preparation_nonce
    generated_utc = [DateTimeOffset]::UtcNow.ToString('o')
    prebuild_manifest_sha256 = File-Hash $PrebuildManifestPath
    gate = $PreparationState.gate
    governance = $PreparationState.governance
    dependency_contracts = $PreparationState.manifest.dependency_contracts
    platform = $Platform
    child_environments = [pscustomobject][ordered]@{
      policy = $ChildEnvironmentPolicy
      official_build_and_suites = $Builds.child_environments
      real_mcp_and_doctor = [pscustomobject][ordered]@{
        child_localappdata_id = $RealMcp.child_localappdata_id
        child_process_count = $RealMcp.child_process_count
        all_child_environments_fail_closed = $RealMcp.all_child_environments_fail_closed
        all_child_localappdata_isolated = $RealMcp.all_child_localappdata_isolated
      }
      compiler_query = [pscustomobject][ordered]@{
        child_localappdata_id = $Platform.compiler_query_child_localappdata_id
        child_environment_policy = $Platform.compiler_query_child_environment_policy
        child_executable_name = $Platform.compiler_query_child_executable_name
        child_executable_sha256 = $Platform.compiler_query_child_executable_sha256
        child_executable_bytes = $Platform.compiler_query_child_executable_bytes
        child_pid = $Platform.compiler_query_child_pid
        blocked_ambient_count = $Platform.compiler_query_blocked_ambient_count
        explicit_override_count = $Platform.compiler_query_explicit_override_count
        process_timeout_seconds = $Platform.compiler_query_process_timeout_seconds
        output_capture_limit_bytes = $Platform.compiler_query_output_capture_limit_bytes
        tree_termination_attempted = $Platform.compiler_query_tree_termination_attempted
      }
      all_child_environments_fail_closed = $allChildEnvironmentsFailClosed
      all_child_localappdata_isolated = $allChildLocalAppDataIsolated
      all_child_output_bounds_observed = $allChildOutputBoundsObserved
      production_data_access_telemetry = 'not-collected'
    }
    commands = [pscustomobject][ordered]@{
      production_build = $Builds.production.command
      production_build_exit_code = $Builds.production.exit_code
      test_build_and_first_suite = $Builds.test_build.command
      test_build_and_first_suite_exit_code = $Builds.test_build.exit_code
      second_full_suite = $Builds.second_suite.command
      second_full_suite_exit_code = $Builds.second_suite.exit_code
    }
    tests = [pscustomobject][ordered]@{
      completed_baseline = $CompletedSuiteBaseline
      registered = $PreparationState.registered_tests.Count
      passed_first_run = $Builds.first_results.passed
      passed_second_run = $Builds.second_results.passed
      failed_first_run = $Builds.first_results.failed
      failed_second_run = $Builds.second_results.failed
      dedicated_n20 = 'PASS'
      frozen_qbrain_tests_sha256 = $Builds.frozen_test_binary_sha256
      n20_snapshot_call_count = $runtimeFirst.snapshot_call_count
      n20_reload_delta_count = $runtimeFirst.reload_delta_count
    }
    n20 = [pscustomobject][ordered]@{
      first_summary = $runtimeFirst.summary
      second_summary = $runtimeSecond.summary
      first_snapshot_rows = $runtimeFirst.snapshot_rows
      second_snapshot_rows = $runtimeSecond.snapshot_rows
      first_reload_delta_row = $runtimeFirst.reload_delta_row
      second_reload_delta_row = $runtimeSecond.reload_delta_row
      selected_snapshot_sha256 = $runtimeFirst.selected_snapshot_sha256
      decoy_snapshot_sha256 = $runtimeFirst.decoy_snapshot_sha256
      filesystem_snapshot_sha256 = $runtimeFirst.filesystem_snapshot_sha256
      schema_v12 = ($runtimeFirst.schema_v12 -and $RealMcp.schema_version -eq 12)
      schema_reopen = $runtimeFirst.schema_reopen
      populated_reopen_labels = $runtimeFirst.populated_reopen_labels
      populated_reopen_first_sha256 = $runtimeFirst.populated_reopen_sha256
      populated_reopen_second_sha256 = $runtimeSecond.populated_reopen_sha256
      registry_schema = $runtimeFirst.registry_schema
      in_process_mcp_contract = $runtimeFirst.in_process_mcp_contract
      real_mcp = $RealMcp
      read_snapshot_pairs_unchanged = $runtimeFirst.read_snapshots_unchanged
      selected_only_reload_delta = $runtimeFirst.changing_reload_selected_only
    }
    binaries = @($Builds.production_binary, $Builds.test_binary)
    deliverables = $deliverables.ToArray()
    evidence_files = $evidenceFiles
    corrective_boundary = $PreparationState.corrective_boundary
    scoped_safety = [pscustomobject][ordered]@{
      expected_changed_baseline_paths = $ExpectedChangedBaselinePaths
      expected_new_paths = $ExpectedNewImplementationPaths
      non_n20_baseline_inputs_unchanged = (
          $PreparationState.corrective_boundary.unchanged_baseline_input_count -eq
          ($GateBaselinePaths.Count - $ExpectedChangedBaselinePaths.Count - $ExpectedConcurrentBaselinePaths.Count))
      schema_or_migration_input_changed = [bool]$PreparationState.corrective_boundary.schema_or_migration_input_changed
      protected_model_configuration_changed = (
          $PreparationState.protected_policy.protected_path_change_count -gt 0 -or
          $PreparationState.protected_policy.protected_setting_change_count -gt 0)
      protected_path_change_count = $PreparationState.protected_policy.protected_path_change_count
      protected_setting_change_count = $PreparationState.protected_policy.protected_setting_change_count
      production_data_access_telemetry = 'not-collected'
      all_child_environments_fail_closed = $allChildEnvironmentsFailClosed
      all_child_localappdata_isolated = ($allChildLocalAppDataIsolated -and $RealMcp.sandbox_removed)
      all_child_output_bounds_observed = $allChildOutputBoundsObserved
      live_network_or_provider_call = 'not-collected'
      secret_persisted = 'not-collected'
      commit_or_push_executed = 'not-collected'
      later_node_specific_evidence_count = 0
      n30_artifact_count = 0
      n21_plus_or_n30_substitution = $false
      audit_verdict_written_by_verifier = $false
      node_or_ledger_status_written_by_verifier = $false
    }
  }
}

function Write-VerificationReport([object]$Evidence) {
  $lines = New-Object System.Collections.Generic.List[string]
  $lines.Add('# N20 Runtime Verification Report')
  $lines.Add('')
  $lines.Add('This file records factual runtime evidence only. It is not a Claude Code plan audit or outcome hard-audit verdict.')
  $lines.Add('')
  $lines.Add("- Generated: $($Evidence.generated_utc)")
  $lines.Add('- Plan status: approved')
  $lines.Add('- Outcome audit: pending')
  $lines.Add("- Platform: $($Evidence.platform.os); process/target X64/x64")
  $lines.Add("- Compiler: $($Evidence.platform.compiler)")
  $lines.Add('- Language mode: `/std:c++20`')
  $lines.Add("- Registered tests: $($Evidence.tests.registered); first run $($Evidence.tests.passed_first_run) PASS / $($Evidence.tests.failed_first_run) FAIL; second run $($Evidence.tests.passed_second_run) PASS / $($Evidence.tests.failed_second_run) FAIL")
  $lines.Add("- Frozen qbrain_tests.exe SHA-256 for both suite runs: ``$($Evidence.tests.frozen_qbrain_tests_sha256)``")
  $lines.Add("- PREBUILD-MANIFEST SHA-256: ``$(File-Hash $PrebuildManifestPath)``")
  $lines.Add('- The companion EVIDENCE-MANIFEST is published atomically after this report and binds this report by SHA-256.')
  $lines.Add('')
  $lines.Add('## Commands And Exit Codes')
  $lines.Add('')
  $lines.Add('| Command | Exit |')
  $lines.Add('|---|---:|')
  $lines.Add("| ``$($Evidence.commands.production_build)`` | $($Evidence.commands.production_build_exit_code) |")
  $lines.Add("| ``$($Evidence.commands.test_build_and_first_suite)`` | $($Evidence.commands.test_build_and_first_suite_exit_code) |")
  $lines.Add("| ``$($Evidence.commands.second_full_suite)`` | $($Evidence.commands.second_full_suite_exit_code) |")
  foreach ($command in @($Evidence.n20.real_mcp.commands | Sort-Object -Unique)) {
    $lines.Add("| ``$command`` | 0 |")
  }
  $lines.Add('')
  $lines.Add('## Governance And Temporal Binding')
  $lines.Add('')
  $lines.Add("- Approved plan SHA-256: ``$($Evidence.governance.approved_plan_sha256)``")
  $lines.Add("- Audited draft SHA-256: ``$($Evidence.governance.audited_draft_plan_sha256)``")
  $lines.Add("- Claude Code plan-audit SHA-256: ``$($Evidence.governance.plan_audit_sha256)``")
  $lines.Add("- Pre-implementation gate SHA-256: ``$($Evidence.gate.file_sha256)``")
  $lines.Add("- Gate interval: $($Evidence.gate.started_utc) to $($Evidence.gate.completed_utc)")
  $lines.Add("- Corrective baseline files: $(@($Evidence.corrective_boundary.expected_changed_baseline_paths) -join ', ')")
  $lines.Add("- New N20 implementation files: $(@($Evidence.corrective_boundary.expected_new_implementation_paths) -join ', ')")
  $lines.Add('- The verifier confirmed the gate precedes every corrective file and that schema/migration inputs, the historical outcome-audit artifact, and the parity ledger remained unchanged.')
  $lines.Add('')
  $lines.Add('## N20 Runtime Marker')
  $lines.Add('')
  $lines.Add('```text')
  $lines.Add($Evidence.n20.first_summary)
  $lines.Add($Evidence.n20.first_reload_delta_row)
  $lines.Add('```')
  $lines.Add('')
  $lines.Add("The dedicated N20 marker contains $($Evidence.tests.n20_snapshot_call_count) contiguous read/rejection snapshot rows. The exact seven-label populated close/reopen matrices are independently bound by SHA-256 ``$($Evidence.n20.populated_reopen_first_sha256)`` and ``$($Evidence.n20.populated_reopen_second_sha256)``. Fresh roots contain runtime timestamps, so cross-run hash equality is not asserted; within each run every selected, decoy, and isolated filesystem before/after pair must match. The single active-pack delta changed only the selected active key, while the selected snapshot with that key excluded, the decoy database, and the pack-library filesystem remained unchanged.")
  $lines.Add('')
  $lines.Add('## Real MCP Stdio')
  $lines.Add('')
  $lines.Add("A real final-binary NDJSON stdio matrix captured $($Evidence.n20.real_mcp.response_count) responses: ``tools/list``; all five N20 read successes with ambient-source exclusion; reload default-deny; the sole explicit ``--allow-write`` reload; a post-reload read; all six operations with non-object arguments and unknown fields; null/integer id types; null/Boolean source types; and signed/floating limits. Every ``tools/call`` response had exactly one structured text block; schema statistics and a final isolated ``doctor --json`` both reported live schema version $($Evidence.n20.real_mcp.schema_version).")
  $lines.Add("Every real-MCP request has a measured before/after bounded filesystem snapshot. $($Evidence.n20.real_mcp.side_effect_unchanged_pair_count) of $($Evidence.n20.real_mcp.side_effect_snapshot_pair_count) probe pairs were unchanged; the exact changed label set is ``$(@($Evidence.n20.real_mcp.side_effect_changed_labels) -join ', ')``.")
  $lines.Add("The probes and doctors used fail-closed child environments rooted at unique disposable LOCALAPPDATA id ``$($Evidence.n20.real_mcp.child_localappdata_id)``. Each child records an executable SHA-256, PID, output cap, and timeout provenance; the sandbox was removed after every root and descendant was rechecked as non-reparse. This proves the configured data route was isolated; direct production-data access telemetry was not collected.")
  $lines.Add('')
  $lines.Add('## Evidence Inputs')
  $lines.Add('')
  $lines.Add('| Role | Path | SHA-256 | Bytes |')
  $lines.Add('|---|---|---|---:|')
  foreach ($entry in @($Evidence.evidence_files)) {
    $lines.Add("| $($entry.role) | ``$($entry.path)`` | ``$($entry.sha256)`` | $($entry.bytes) |")
  }
  $lines.Add('')
  $lines.Add('## Safeguards')
  $lines.Add('')
  $lines.Add('- Every build, suite, MCP, doctor, and compiler-query child record uses a measured fail-closed environment policy with an explicit unique/disposable LOCALAPPDATA root, executable hash, bounded-output cap, and timeout provenance. This is environment-routing evidence; production-data access telemetry was not collected and the report does not claim otherwise.')
  $lines.Add('- Network/provider traffic, secret persistence outside the disposable sandbox, and external Git activity were not instrumented. The verifier records those governance fields as not-collected rather than asserting unobservable negatives; it separately rejects protected configuration edits and does not invoke commit or push commands.')
  $lines.Add('- No N21+ or N30 artifact supplied N20-specific evidence. Later-node tests, if present in the full regression binary, were not used as N20 acceptance evidence.')
  $lines.Add('- Retained workspace outputs are limited to the fixed N20 prebuild/manifest/report and three official build/suite log paths; the real-MCP database/pack fixture lived only in a removed disposable sandbox. The verifier did not write an audit verdict, plan status, ledger row, or outcome-audit file.')
  $lines.Add('')
  $lines.Add('## Result')
  $lines.Add('')
  $lines.Add('All scripted N20 evidence checks completed against two independently executed all-PASS native Windows x64 C++20 suites. A fresh node-specific Claude Code outcome hard audit remains blocking; this report does not mark N20 done.')
  Write-Utf8Lines $ReportPath $lines.ToArray()
}

function Invoke-Verification {
  Assert-OutputPolicy
  $manifest = Read-PrebuildManifest
  [void](Assert-N20PendingState $manifest)
  $prebuildItem = Get-Item -LiteralPath $PrebuildManifestPath
  $gateCompleted = Parse-UtcTimestamp $manifest.gate.completed_utc 'prebuild gate completion'
  Require ($prebuildItem.LastWriteTimeUtc -gt $gateCompleted.UtcDateTime) 'PREBUILD-MANIFEST does not postdate the gate'
  $preparation = Assert-PreparationCurrent $manifest
  Require ([bool]$RunBuilds) 'formal N20 verification requires -RunBuilds in this verifier process'
  Invoke-N20OfficialSequence $preparation
  [void](Assert-N20PendingState $manifest)
  $preparation = Assert-PreparationCurrent $manifest
  $builds = Assert-OfficialBuildEvidence $preparation
  $platform = Get-PlatformEvidence
  # Recheck every frozen input and binary after parsing the complete build interval.
  $preparationAfter = Assert-PreparationCurrent $manifest
  Require ((File-Hash $Qbrain) -ceq [string]$builds.production_binary.sha256) 'production binary changed after log validation'
  Require ((File-Hash $Tests) -ceq [string]$builds.test_binary.sha256) 'test binary changed after log validation'
  $realMcp = Assert-N20RealMcp $preparationAfter $builds.production_binary
  $preparationAfterMcp = Assert-PreparationCurrent $manifest
  Require ((File-Hash $Qbrain) -ceq [string]$builds.production_binary.sha256) 'production binary changed after real MCP validation'
  Require ((File-Hash $Tests) -ceq [string]$builds.test_binary.sha256) 'test binary changed after real MCP validation'
  $evidence = New-EvidenceManifest $preparationAfterMcp $builds $platform $realMcp
  Write-VerificationReport $evidence
  $reportEntry = Get-FileEntry $ReportPath 'verification-report'
  $evidence.evidence_files = @($evidence.evidence_files) + @($reportEntry)
  $evidence | Add-Member -NotePropertyName verification_report_sha256 `
      -NotePropertyValue ([string]$reportEntry.sha256)
  # The final manifest is the last atomic state transition. Until this write
  # succeeds, the existing manifest remains the nonce-bound pending state.
  Write-Utf8Text $EvidenceManifestPath (($evidence | ConvertTo-Json -Depth 20) + [Environment]::NewLine)
  $script:N20FinalManifestPublished = $true
  Write-Host "N20_VERIFY_OK registered=$($evidence.tests.registered) first_pass=$($evidence.tests.passed_first_run) second_pass=$($evidence.tests.passed_second_run) snapshots=$($evidence.tests.n20_snapshot_call_count) real_mcp=$($evidence.n20.real_mcp.response_count)"
}

function Invoke-ParserSelfTest {
  $expectedLabels = @(Get-ExpectedN20SnapshotLabels)
  Require ($expectedLabels.Count -eq $ExpectedN20SnapshotLabelCount) `
      'parser self-test N20 label matrix count changed'
  Require ((Text-Hash ($expectedLabels -join "`n")) -ceq
      $ExpectedN20SnapshotLabelsHash) `
      'parser self-test N20 label matrix hash changed'
  Require ($expectedLabels[0] -ceq 'builtin:list:no-create') 'parser self-test first snapshot label differs'
  Require ($expectedLabels[-1] -ceq 'stats:damaged-database:missing-schema-version') 'parser self-test final snapshot label differs'
  foreach ($required in @(
      'filesystem:reparse-pack-root', 'filesystem:reparse-qbrain-root',
      'filesystem:case-collision', 'filesystem:invalid-stem',
      'filesystem:malformed-list',
      'filesystem:noncanonical-named-lookup',
      'filesystem:noncanonical-extension-named-lookup',
      'filesystem:noncanonical-default:reload',
      'reload:oversized:no-delta', 'reload:unsafe:no-delta',
      'reopen:populated:list', 'reopen:populated:dimensions',
      'mcp:arguments:overlong-field:reload_schema_pack')) {
    Require (@($expectedLabels | Where-Object { $_ -ceq $required }).Count -eq 1) "parser self-test lacks exact label $required"
  }
  $summary = Get-ExpectedN20Summary $expectedLabels.Count
  Require ($summary -match (' snapshot_call_count=' + $expectedLabels.Count + ' reload_delta_count=1$')) 'parser self-test summary count binding differs'
  $syntheticLines = New-Object System.Collections.Generic.List[string]
  $syntheticLines.Add($summary)
  $selectedBeforeReload = 'a' * 64
  $selectedAfterReload = 'b' * 64
  $decoyHash = 'c' * 64
  $filesystemHash = 'd' * 64
  $selectedWithoutActive = 'e' * 64
  $selectedHash = $selectedBeforeReload
  for ($index = 0; $index -lt $expectedLabels.Count; ++$index) {
    if ($expectedLabels[$index] -ceq 'reload:same-id:no-op') {
      $selectedHash = $selectedAfterReload
    }
    $syntheticLines.Add(
        "[INFO] n20 snapshot_call=$($index + 1) label=$($expectedLabels[$index]) selected_before_sha256=$selectedHash selected_after_sha256=$selectedHash decoy_before_sha256=$decoyHash decoy_after_sha256=$decoyHash filesystem_before_sha256=$filesystemHash filesystem_after_sha256=$filesystemHash")
  }
  $syntheticLines.Add(
      "[INFO] n20 reload_delta label=reload:success:remote-allow-db-only selected_before_sha256=$selectedBeforeReload selected_after_sha256=$selectedAfterReload selected_without_active_before_sha256=$selectedWithoutActive selected_without_active_after_sha256=$selectedWithoutActive decoy_before_sha256=$decoyHash decoy_after_sha256=$decoyHash filesystem_before_sha256=$filesystemHash filesystem_after_sha256=$filesystemHash old_id=alpha new_id=beta")
  $runtime = Assert-N20RuntimeEvidence $syntheticLines.ToArray() 'parser self-test runtime marker'
  Require ($runtime.snapshot_call_count -eq $expectedLabels.Count) 'parser self-test runtime count differs'
  Require ($runtime.schema_reopen) 'parser self-test did not bind populated reopen markers'
  $parsed = ConvertFrom-StrictJsonText '{"jsonrpc":"2.0","id":20200,"result":{"content":[{"type":"text","text":"{\"error\":{\"code\":\"invalid_argument\",\"field\":\"limit\",\"message\":\"unsigned integer value required\"}}"}],"isError":true}}' 'parser self-test MCP response'
  $probe = [pscustomobject]@{ operation='schema_stats'; response=$parsed }
  $payload = Read-N20McpContent $probe $true 'invalid_argument' 'limit'
  Require-JsonStringExact $payload.error.code 'invalid_argument' 'parser self-test structured error code'
  $successText = '{"id":"default","origin":"builtin","pack":{"id":"default","name":"Qbrain default","types":["note"],"dimensions":["topic","entity","time"],"phases":["orphans"]}}'
  $successProbe = [pscustomobject]@{
    operation = 'get_active_schema_pack'
    response = [pscustomobject]@{
      result = [pscustomobject]@{
        content = @([pscustomobject]@{ type='text'; text=$successText })
        isError = $false
      }
    }
  }
  $successPayload = Read-N20McpContent $successProbe $false
  Assert-N20PackPayload $successPayload 'default' 'builtin' 3 'parser self-test pack payload'
  $emptyArgumentsRequestText = New-N20RpcRequest 20201 'schema_stats' ([object[]]@())
  Require ($emptyArgumentsRequestText -ceq `
      ('{"jsonrpc":"2.0","id":20201,"method":"tools/call","params":{"name":"schema_stats","arguments":[]}}' + "`n")) `
      'parser self-test empty arguments wire JSON differs'
  $emptyArgumentsRequest = ConvertFrom-StrictJsonText `
      $emptyArgumentsRequestText `
      'parser self-test empty arguments request'
  Require ($emptyArgumentsRequest.params.arguments -is [Array] -and
      @($emptyArgumentsRequest.params.arguments).Count -eq 0) 'parser self-test did not preserve empty non-object arguments'
  $toolsListRequestText = New-N20ToolsListRequest 20202
  Require ($toolsListRequestText -ceq `
      ('{"jsonrpc":"2.0","id":20202,"method":"tools/list","params":{}}' + "`n")) `
      'parser self-test tools/list wire JSON differs'
  $toolsListRequest = ConvertFrom-StrictJsonText $toolsListRequestText `
      'parser self-test tools/list request'
  Require-ExactJsonPropertyNames $toolsListRequest @('jsonrpc', 'id', 'method', 'params') 'parser self-test tools/list request'
  Require-JsonStringExact $toolsListRequest.method 'tools/list' 'parser self-test tools/list method'
  Require-ExactJsonPropertyNames $toolsListRequest.params @() 'parser self-test tools/list params'
  $expectedRealMcpProbes = @(Get-ExpectedN20RealMcpProbes)
  Require ($expectedRealMcpProbes.Count -eq 30) 'parser self-test real MCP probe count differs'
  Require (@($expectedRealMcpProbes | ForEach-Object { $_.label } | Sort-Object -Unique).Count -eq 30) `
      'parser self-test real MCP probe labels are not unique'
  $realMcpProbeFixtures = New-Object System.Collections.Generic.List[object]
  for ($index = 0; $index -lt $expectedRealMcpProbes.Count; ++$index) {
    $contract = $expectedRealMcpProbes[$index]
    $expectedMutation = [string]$contract.label -ceq 'reload:explicit-allow'
    $realMcpProbeFixtures.Add([pscustomobject][ordered]@{
        id = 20201 + $index
        label = $contract.label
        operation = $contract.operation
        allow_write = $contract.allow_write
        snapshot_before_sha256 = 'a' * 64
        snapshot_after_sha256 = if ($expectedMutation) { 'b' * 64 } else { 'a' * 64 }
        side_effect_unchanged = -not $expectedMutation
      })
  }
  Assert-N20RealMcpProbeMatrix ($realMcpProbeFixtures.ToArray()) 20201
  $toolFixtures = New-Object System.Collections.Generic.List[object]
  foreach ($contract in @(Get-ExpectedN20ToolContracts)) {
    $toolFixtures.Add([pscustomobject][ordered]@{
        name = $contract.name
        description = 'Schema pack parser fixture'
        inputSchema = New-N20ToolSchemaFixture ([string[]]@($contract.properties))
      })
  }
  $toolsListProbe = [pscustomobject]@{
    response = [pscustomobject]@{
      result = [pscustomobject]@{ tools = $toolFixtures.ToArray() }
    }
  }
  Require (@(Assert-N20RealToolsList $toolsListProbe).Count -eq 6) 'parser self-test tools/list matrix differs'
  Require (Test-BlockedChildEnvironmentName 'gIt_CoNfIg_Count') `
      'parser self-test mixed-case Git environment name was not blocked'
  Require (Test-BlockedChildEnvironmentName 'qBrAiN_Source') `
      'parser self-test mixed-case Qbrain environment name was not blocked'
  Require (Test-N20NameEqualsAny 'localappdata' $AllowedChildEnvironmentOverrides) `
      'parser self-test child override allowlist is not case-insensitive'
  $environmentSelfTestName = 'qBrAiN_N20_ENV_SELFTEST_' + [Guid]::NewGuid().ToString('N')
  $environmentSelfTestValue = 'n20-environment-restoration-sentinel'
  [Environment]::SetEnvironmentVariable(
      $environmentSelfTestName, $environmentSelfTestValue,
      [EnvironmentVariableTarget]::Process)
  $entryFailureObserved = $false
  try {
    try {
      [void](Enter-FailClosedChildEnvironment `
          @{ N20_NOT_ALLOWLISTED='must-fail' } @())
    } catch {
      $entryFailureObserved = $true
    }
    Require $entryFailureObserved `
        'parser self-test fail-closed environment accepted an unknown override'
    Require ([Environment]::GetEnvironmentVariable(
        $environmentSelfTestName, [EnvironmentVariableTarget]::Process) -ceq
        $environmentSelfTestValue) `
        'parser self-test failed environment entry did not restore ambient state'

    $parserSandbox = New-N20Sandbox 'parser'
    try {
      $parserSnapshotBefore = Get-N20SandboxSnapshot `
          ([string]$parserSandbox.path) 'parser'
      $captureArguments = '/d /c "if defined ' + $environmentSelfTestName +
          ' (exit /b 7) else (more)"'
      $capture = Invoke-CapturedProcess 'cmd.exe' $captureArguments 10 $false `
          "n20-stdio-self-test`n" `
          @{ localappdata=[string]$parserSandbox.localappdata } @()
      $parserSnapshotAfter = Get-N20SandboxSnapshot `
          ([string]$parserSandbox.path) 'parser'
      Require ($capture.child_environment_policy -ceq $ChildEnvironmentPolicy -and
          $capture.localappdata_overridden) `
          'parser self-test stdio child did not use the isolated fail-closed policy'
      Require ($capture.child_executable_name -ceq 'cmd.exe' -and
          [string]$capture.child_executable_sha256 -match '^[0-9a-f]{64}$' -and
          [int64]$capture.child_executable_bytes -gt 0) `
          'parser self-test child executable provenance differs'
      Require ($capture.output_limit_bytes -eq $N20ProcessOutputLimitBytes -and
          -not [bool]$capture.tree_termination_attempted -and
          -not [bool]$capture.process_timed_out) `
          'parser self-test bounded capture provenance differs'
      Require ($parserSnapshotBefore.sha256 -ceq $parserSnapshotAfter.sha256) `
          'parser self-test child changed its disposable sandbox snapshot'
    } finally {
      if (Test-Path -LiteralPath ([string]$parserSandbox.path)) {
        Remove-N20SandboxSafely ([string]$parserSandbox.path) 'parser'
      }
    }
    Require ($capture.exit_code -eq 0 -and
        $capture.stdout.Trim() -ceq 'n20-stdio-self-test') `
        'parser self-test stdio process capture differs'
    Require ([Environment]::GetEnvironmentVariable(
        $environmentSelfTestName, [EnvironmentVariableTarget]::Process) -ceq
        $environmentSelfTestValue) `
        'parser self-test successful child invocation did not restore ambient state'
  } finally {
    [Environment]::SetEnvironmentVariable(
        $environmentSelfTestName, $null,
        [EnvironmentVariableTarget]::Process)
  }
  $duplicateRejected = $false
  try {
    [void](ConvertFrom-StrictJsonText '{"same":1,"same":2}' 'parser self-test duplicate JSON')
  } catch {
    $duplicateRejected = $true
  }
  Require $duplicateRejected 'parser self-test accepted duplicate JSON object keys'
  Require ($N20EvidenceBuildMutexName -ceq 'Global\Qbrain.N20.Verifier.EvidenceBuild.v1') `
      'parser self-test named evidence/build mutex differs'
  Write-Host "N20_PARSER_SELFTEST_OK expected_snapshot_labels=$($expectedLabels.Count)"
}

try {
  if ($ParserSelfTest) {
    Invoke-ParserSelfTest
  } else {
    Invoke-N20EvidenceBuildCriticalSection {
      if ($Prepare) {
        New-PrebuildManifest
      } else {
        $script:N20VerificationStarted = $true
        Invoke-Verification
      }
    }
  }
  exit 0
} catch {
  $verificationFailure = $_
  if (-not $ParserSelfTest -and -not $Prepare -and
      $script:N20VerificationStarted -and
      -not $script:N20FinalManifestPublished -and
      (Test-Path -LiteralPath $PrebuildManifestPath -PathType Leaf)) {
    try {
      Invoke-N20EvidenceBuildCriticalSection {
        $failedManifest = Read-PrebuildManifest
        Write-N20PendingState $failedManifest $false `
            'formal verification failed before final evidence publication; rerun Prepare'
      }
    } catch {
      # Preserve the original verification error below. A failed pending-state
      # rewrite never manufactures a final state because publication is atomic.
    }
  }
  Write-Error $verificationFailure.Exception.Message
  exit 1
}
