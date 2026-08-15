# N22 native evidence verifier.
# This script writes factual pending/final runtime evidence only. It does not
# author an audit verdict, change node status, reconcile the ledger, or invoke
# a Git mutation command.
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
  [int]$TimeoutSeconds = 3600,

  [Parameter(Mandatory = $true, ParameterSetName = 'ParserSelfTest')]
  [switch]$ParserSelfTest
)

$ErrorActionPreference = 'Stop'
if (Test-Path Variable:PSNativeCommandUseErrorActionPreference) {
  $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Split-Path -Parent $PSScriptRoot
$PlanPath = Join-Path $Root 'docs\nodes\N22-PLAN.md'
$PlanAuditPath = Join-Path $Root 'docs\nodes\N22-PLAN-AUDIT.md'
$EvidenceDir = Join-Path $Root 'docs\nodes\n22-evidence'
$PrebuildManifestPath = Join-Path $EvidenceDir 'PREBUILD-MANIFEST.json'
$EvidenceManifestPath = Join-Path $EvidenceDir 'EVIDENCE-MANIFEST.json'
$ReportPath = Join-Path $EvidenceDir 'VERIFY-REPORT.md'
$ProductionEvidencePath = Join-Path $EvidenceDir 'PRODUCTION-BUILD-OUTPUT.txt'
$TestBuildEvidencePath = Join-Path $EvidenceDir 'TEST-BUILD-OUTPUT.txt'
$FullSuiteEvidencePath = Join-Path $EvidenceDir 'FULL-SUITE-RUN-2.txt'
$FocusedEvidencePath = Join-Path $EvidenceDir 'FOCUSED-RUNTIME-OUTPUT.txt'
$SnapshotEvidencePath = Join-Path $EvidenceDir 'SNAPSHOT-EVIDENCE.txt'
$RegistryMcpEvidencePath = Join-Path $EvidenceDir 'REGISTRY-MCP-EVIDENCE.json'
$RealMcpEvidencePath = Join-Path $EvidenceDir 'REAL-MCP-EVIDENCE.json'
$DoctorEvidencePath = Join-Path $EvidenceDir 'DOCTOR-SCHEMA-EVIDENCE.json'
$SchemaEvidencePath = Join-Path $EvidenceDir 'SCHEMA-EVIDENCE.txt'
$PlatformEvidencePath = Join-Path $EvidenceDir 'PLATFORM-OUTPUT.txt'
$SafetyEvidencePath = Join-Path $EvidenceDir 'SCOPE-SAFETY-EVIDENCE.txt'
$ProductionBuildScript = Join-Path $Root 'scripts\build-cl.ps1'
$TestBuildScript = Join-Path $Root 'scripts\build-tests-cl.ps1'
$VerifierPath = Join-Path $Root 'scripts\n22-verify.ps1'
$Qbrain = Join-Path $Root 'build\cl\qbrain.exe'
$Tests = Join-Path $Root 'build\cl\qbrain_tests.exe'
$ExpectedGitDirectory = [IO.Path]::GetFullPath((Join-Path $Root '.git'))
$VcVars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Utf8Strict = New-Object System.Text.UTF8Encoding($false, $true)
$MinimumRegisteredTests = 26
$ExpectedApprovedPlanHash = '8ae960dfd88ab43f91605c39cc963843e1c5866f61b94691f700393cc984d2b4'
$ExpectedAuditedDraftPlanHash = '696485222d71c86dff805d5d4d20b22d9433e190ff401de90017894e00a06a1c'
$ExpectedPlanAuditHash = 'bac3404aa98a492c6da00fb76b0f55325c7fa26ec1c486c3a1587539b691d986'
$ExpectedN22SnapshotLabelCount = 367
$ExpectedN22SnapshotLabelHash = 'bf97765697495997c2dd512133dec03f49d80cbe618d32b112b506efb55538c1'
$ExpectedN22SourceIdPattern = '^(?!(?:[Cc][Oo][Nn]|[Pp][Rr][Nn]|[Aa][Uu][Xx]|[Nn][Uu][Ll]|[Cc][Oo][Mm][1-9]|[Ll][Pp][Tt][1-9])$)[A-Za-z0-9_-]+$'
$ProductionBuildCommand = 'powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/build-cl.ps1'
$TestBuildCommand = 'powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1 -SkipProductionBuild'
$FullSuiteCommand = 'build\cl\qbrain_tests.exe'
$EvidenceMutexName = 'Local\Qbrain.N22.Verify.SharedEvidence'
$MaximumCapturedOutputBytes = 16777216
$EvidenceInitialized = $false
$ValidatedPrebuildHash = $null
$ValidatedPreparedUtc = $null
$ValidatedPublicationNonce = $null
$N22EvidenceMutex = $null
$N22EvidenceMutexHeld = $false
$FrozenInputHandles = @()
$ChildEnvironmentPolicy = 'remove-qbrain-provider-secret-and-proxy-overrides'
$PersistentTreeFingerprintScope = 'directory-path-and-attributes;file-path-attributes-length-sha256-last-write-utc;directory-last-write-utc-excluded-for-sqlite-wal-read-lifecycle'

if ([string]::IsNullOrWhiteSpace($ProductionBuildLog)) {
  $ProductionBuildLog = $ProductionEvidencePath
}
if ([string]::IsNullOrWhiteSpace($TestBuildLog)) {
  $TestBuildLog = $TestBuildEvidencePath
}

$N22Deliverables = @(
  'include/qbrain/codeintel/scan.hpp',
  'src/qbrain/codeintel/scan.cpp',
  'src/qbrain/ops/handlers.cpp',
  'src/qbrain/mcp/server.cpp',
  'tests/test_n22.cpp',
  'tests/test_main.cpp',
  'CMakeLists.txt',
  'scripts/build-tests-cl.ps1',
  'scripts/n22-verify.ps1',
  'docs/nodes/N22-PLAN.md',
  'docs/nodes/N22-PLAN-AUDIT.md'
)

$N22ScopedDiffPaths = @(
  'include/qbrain/codeintel/scan.hpp',
  'src/qbrain/codeintel/scan.cpp',
  'src/qbrain/ops/handlers.cpp',
  'src/qbrain/mcp/server.cpp',
  'tests/test_n22.cpp',
  'tests/test_main.cpp',
  'CMakeLists.txt',
  'scripts/build-tests-cl.ps1',
  'scripts/n22-verify.ps1'
)

$RelevantSchemaInputs = @(
  'include/qbrain/storage/database.hpp',
  'include/qbrain/storage/schema_sql.hpp',
  'src/qbrain/storage/database.cpp',
  'src/qbrain/storage/migrate.cpp',
  'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.c',
  'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.h'
)

$N19StorageBaseline = [ordered]@{
  'include/qbrain/storage/database.hpp' = '2f0ccf8035aa35f3ce69a12d42afa18015754efb4671d7c528b36bb4285de8fd'
  'include/qbrain/storage/schema_sql.hpp' = '7709912b2d792b6055c0b95d5c79a157b949669925242e7c840a256717c7a024'
  'src/qbrain/storage/database.cpp' = 'bda29c5926936979102fd1078044b8ee76c250ae34707f4f4fda42ab4d32dacb'
  'src/qbrain/storage/migrate.cpp' = 'd775529ff6f89d520f15a3fee30f0e180a2011646516de0a620e1870d538ec45'
}

$N22ProductionSlicePaths = @(
  'include/qbrain/codeintel/scan.hpp',
  'src/qbrain/codeintel/scan.cpp',
  'src/qbrain/ops/handlers.cpp',
  'src/qbrain/mcp/server.cpp'
)

$N22OperationNames = @(
  'code_callees',
  'code_flow',
  'code_blast',
  'code_traversal_cache_clear'
)

$DependencyContracts = @(
  [pscustomobject]@{ Node='N1';   Plan='9fd6df77ad905463f34e6873c2220849003679a64c869e5fb1eaffba470f95e6'; Outcome='93f112c13d01864aa701683e2a4dbb3726a763d90b7a113c07dc543af4d31141' },
  [pscustomobject]@{ Node='N2';   Plan='c34fede88989a9847dd3cad0bf719b6476c28bbfb124cb094d4afbe24d90fb85'; Outcome='e9dc809dcdb73c0757708f81d53daf2fc89394c12cf953e86c0e9de5923a3413' },
  [pscustomobject]@{ Node='N2.5'; Plan='bd0cf1b5f4dddb9af40168a89d1a87be84d5a4eb2f99872d3389880523617953'; Outcome='dd6e404ab7583af8c6cbecd86179baba3401a1d5ef10f559b2067229a208c8ff' },
  [pscustomobject]@{ Node='N7';   Plan='929970318d8fb3043371f82a9208360db7e38e6dd058e37f0eef515534f26d39'; Outcome='307226705f0dc7495b0aa7aeebf88bd807c0216c19cab059cd23d01dd6835421' },
  [pscustomobject]@{ Node='N8';   Plan='7f16263f786315420ed42a7c79350add553ad84b11ce4cd6dbc21b0fdc320570'; Outcome='7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9' },
  [pscustomobject]@{ Node='N11';  Plan='e157d9f3b6dcbc276b782d960c237d50fed9d4ff5614473678813e27541844a7'; Outcome='bdefcf26d138b658d31df0b8525c46b776aa5e9086796bcd16696d8b783f2012' },
  [pscustomobject]@{ Node='N16';  Plan='ad6794067444a56658d52d23c3ca29f7092cd7024829f1c4313b295b10c77fef'; Outcome='591865f6647e175c4aa02ec90abad1075c554eca49e3a15e5f63ad1639c24aba' },
  [pscustomobject]@{ Node='N19';  Plan='e5c603efbfecb5603a0fd068dd2a0b39e7a75abac5fd116634adc397d9b7e470'; Outcome='d4ee4ad14e3768b5470865f092a783ba0d10b9e9155bfb17c4bd5ce594ad4f24' }
)

function Require([bool]$Condition, [string]$Message) {
  if (-not $Condition) {
    throw "N22 evidence requirement failed: $Message"
  }
}

function Enter-N22EvidenceMutex {
  Require ($null -eq $script:N22EvidenceMutex) 'N22 shared evidence mutex was already initialized'
  $mutex = New-Object System.Threading.Mutex($false, $EvidenceMutexName)
  try {
    $acquired = $false
    try {
      # A zero-time wait makes concurrent invocations fail closed instead of
      # racing on the fixed evidence paths or waiting behind an abandoned run.
      $acquired = $mutex.WaitOne(0)
    } catch [System.Threading.AbandonedMutexException] {
      $acquired = $true
    }
    Require $acquired 'another N22 verifier process owns the shared build/evidence paths'
    $script:N22EvidenceMutex = $mutex
    $script:N22EvidenceMutexHeld = $true
  } catch {
    $mutex.Dispose()
    throw
  }
}

function Exit-N22EvidenceMutex {
  if ($script:N22EvidenceMutexHeld -and $null -ne $script:N22EvidenceMutex) {
    try { $script:N22EvidenceMutex.ReleaseMutex() } catch {}
    try { $script:N22EvidenceMutex.Dispose() } catch {}
  }
  $script:N22EvidenceMutex = $null
  $script:N22EvidenceMutexHeld = $false
}

function Assert-Throws([scriptblock]$Action, [string]$Label) {
  $thrown = $false
  try {
    & $Action | Out-Null
  } catch {
    $thrown = $true
  }
  Require $thrown "$Label did not reject the invalid input"
}

function Assert-ThrowsMatching([scriptblock]$Action, [string]$Pattern, [string]$Label) {
  $message = $null
  try {
    & $Action | Out-Null
  } catch {
    $message = [string]$_.Exception.Message
  }
  Require (-not [string]::IsNullOrWhiteSpace($message)) "$Label did not reject the invalid input"
  Require ($message -match $Pattern) "$Label rejected for an unexpected reason"
}

function Assert-NoExcludedCoordinatorToken([string]$Text, [string]$Label) {
  foreach ($match in [regex]::Matches(
      $Text, '(?i)(^|[^A-Za-z0-9])N([0-9]+)(?=$|[^A-Za-z0-9])')) {
    [uint64]$nodeNumber = 0
    Require ([uint64]::TryParse($match.Groups[2].Value, [ref]$nodeNumber)) "$Label contains an unbounded node token"
    Require ($nodeNumber -ne 30) "$Label contains an excluded coordinator token"
  }
}

function Assert-N22ScopedPathTokenPolicy([string]$Path, [string]$Label) {
  $normalized = $Path.Replace('\', '/')
  Assert-NoExcludedCoordinatorToken $normalized $Label
  foreach ($match in [regex]::Matches(
      $normalized, '(?i)(^|[/_.-])N([0-9]+)(?=[/_.-]|$)')) {
    [uint64]$nodeNumber = 0
    Require ([uint64]::TryParse($match.Groups[2].Value, [ref]$nodeNumber)) `
        "$Label contains an unbounded node token"
    Require ($nodeNumber -ne 20 -and $nodeNumber -ne 21 -and $nodeNumber -le 22) `
        "$Label violates the N22 node-token scope policy"
  }
}

function Assert-NoFutureNodeReference([string]$Text, [string]$Label) {
  Assert-NoExcludedCoordinatorToken $Text $Label
  foreach ($match in [regex]::Matches(
      $Text, '(?i)(^|[^A-Za-z0-9])N([0-9]+)(?=$|[^A-Za-z0-9])')) {
    [uint64]$nodeNumber = 0
    Require ([uint64]::TryParse($match.Groups[2].Value, [ref]$nodeNumber)) `
        "$Label contains an unbounded future-node token"
    Require ($nodeNumber -ne 20 -and $nodeNumber -ne 21 -and $nodeNumber -le 22) `
        "$Label contains an out-of-scope node token"
  }
}

function Write-Utf8Text([string]$Path, [string]$Text) {
  Require $script:N22EvidenceMutexHeld 'shared N22 evidence write attempted outside the inter-process mutex'
  $full = [IO.Path]::GetFullPath($Path)
  Assert-PlainPathChain $full
  $directory = [IO.Path]::GetDirectoryName($full)
  Require (Test-Path -LiteralPath $directory -PathType Container) "output directory is missing: $directory"
  $leaf = [IO.Path]::GetFileName($full)
  $temporary = Join-Path $directory ('.' + $leaf + '.' + [guid]::NewGuid().ToString('N') + '.tmp')
  $backup = Join-Path $directory ('.' + $leaf + '.' + [guid]::NewGuid().ToString('N') + '.bak')
  try {
    [IO.File]::WriteAllText($temporary, $Text, $Utf8NoBom)
    Assert-PlainPathChain $temporary $true
    Require ([IO.File]::ReadAllText($temporary, $Utf8Strict) -ceq $Text) "atomic output staging verification failed: $leaf"
    if (Test-Path -LiteralPath $full -PathType Leaf) {
      Assert-PlainPathChain $full $true
      [IO.File]::Replace($temporary, $full, $backup, $true)
      Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
    } else {
      [IO.File]::Move($temporary, $full)
    }
  } finally {
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
  }
}

function Write-Utf8Lines([string]$Path, [object[]]$Lines) {
  Write-Utf8Text $Path ((@($Lines) -join [Environment]::NewLine) + [Environment]::NewLine)
}

function File-Hash([string]$Path) {
  Assert-PlainPathChain $Path $true
  (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-OpenStreamHash([System.IO.FileStream]$Stream) {
  $sha = [Security.Cryptography.SHA256]::Create()
  $position = $Stream.Position
  try {
    $Stream.Position = 0
    ([BitConverter]::ToString($sha.ComputeHash($Stream))).Replace('-', '').ToLowerInvariant()
  } finally {
    $Stream.Position = $position
    $sha.Dispose()
  }
}

function Open-FrozenInputHandles([object[]]$Entries) {
  $handles = New-Object System.Collections.Generic.List[object]
  try {
    foreach ($entry in $Entries) {
      $path = Resolve-WorkspacePath ([string]$entry.path)
      Assert-PlainPathChain $path $true
      Require ($entry.sha256 -is [string] -and $entry.sha256 -match '^[0-9a-f]{64}$') `
          "frozen input has an invalid SHA-256: $($entry.path)"
      $handle = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
                                 [IO.FileShare]::Read)
      try {
        # Hold an exclusive write/delete denial while the child tools consume the
        # input, then bind the already-open handle to the prepared manifest.
        Require ($handle.Length -eq [int64]$entry.bytes) "frozen input length changed before locking: $($entry.path)"
        Require ((Get-OpenStreamHash $handle) -ceq [string]$entry.sha256) `
            "frozen input hash changed before locking: $($entry.path)"
        $handles.Add([pscustomobject]@{ path=$path; handle=$handle })
      } catch {
        $handle.Dispose()
        throw
      }
    }
    return $handles.ToArray()
  } catch {
    foreach ($entry in $handles) { $entry.handle.Dispose() }
    throw
  }
}

function Close-FrozenInputHandles([object[]]$Handles) {
  foreach ($entry in @($Handles)) {
    if ($null -ne $entry -and $null -ne $entry.handle) { $entry.handle.Dispose() }
  }
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

function Get-PlainTreeFiles([string]$Path, [string]$Label, [bool]$RequireDirectory = $false) {
  $root = [IO.Path]::GetFullPath($Path)
  Assert-PlainPathChain $root
  if (-not (Test-Path -LiteralPath $root)) {
    Require (-not $RequireDirectory) "$Label directory is missing: $root"
    return @()
  }
  $rootItem = Get-Item -LiteralPath $root -Force
  Require ($rootItem.PSIsContainer) "$Label root is not a directory: $root"
  Require (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
      "$Label root is a reparse point: $root"
  $files = New-Object System.Collections.Generic.List[string]
  $pending = New-Object 'System.Collections.Generic.Stack[string]'
  $pending.Push($root)
  while ($pending.Count -gt 0) {
    $directory = $pending.Pop()
    foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force | Sort-Object FullName)) {
      Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
          "$Label tree contains a reparse point: $($item.FullName)"
      if ($item.PSIsContainer) {
        $pending.Push($item.FullName)
      } else {
        $files.Add($item.FullName)
      }
    }
  }
  $files.ToArray()
}

function Get-TreeFingerprint([string]$Path, [switch]$PersistentDataTree) {
  if (-not (Test-Path -LiteralPath $Path)) { return 'absent' }
  $root = [IO.Path]::GetFullPath($Path).TrimEnd('\')
  $rows = New-Object System.Collections.Generic.List[string]
  $rootItem = Get-Item -LiteralPath $root -Force
  Require ($rootItem.PSIsContainer) "tree root is not a directory: $root"
  Require (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "tree root is a reparse point: $root"
  # SQLite WAL read lifecycles can update parent directory timestamps without
  # leaving a persistent application-data change. The persistent-data mode
  # retains directory topology/attributes and complete file metadata/content.
  if ($PersistentDataTree) {
    $rows.Add(".|$([int64]$rootItem.Attributes)")
  } else {
    $rows.Add(".|$([int64]$rootItem.Attributes)|$($rootItem.LastWriteTimeUtc.Ticks)")
  }
  $pending = New-Object 'System.Collections.Generic.Stack[string]'
  $pending.Push($root)
  while ($pending.Count -gt 0) {
    $directory = $pending.Pop()
    foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force | Sort-Object FullName)) {
      $relative = $item.FullName.Substring($root.Length).TrimStart('\').Replace('\', '/')
      $attributes = [int64]$item.Attributes
      Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
          "tree contains a reparse point: $($item.FullName)"
      if ($item.PSIsContainer) {
        if ($PersistentDataTree) {
          $rows.Add("D|$relative|$attributes")
        } else {
          $rows.Add("D|$relative|$attributes|$($item.LastWriteTimeUtc.Ticks)")
        }
        $pending.Push($item.FullName)
      } else {
        $rows.Add("F|$relative|$attributes|$([int64]$item.Length)|$(File-Hash $item.FullName)|$($item.LastWriteTimeUtc.Ticks)")
      }
    }
  }
  Text-Hash ((@($rows | Sort-Object) -join "`n"))
}

function Get-DisposableTreeFingerprint([string]$Path, [switch]$PersistentDataTree) {
  $full = [IO.Path]::GetFullPath($Path)
  $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
  Require ($full.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase)) `
      "disposable tree path is outside the system temporary directory: $full"
  Assert-PlainPathChain $full
  Get-TreeFingerprint -Path $full -PersistentDataTree:$PersistentDataTree
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
      $token = $Text.Substring($start, $Index.Value - $start)
      try {
        return ConvertFrom-JsonPreservingDateStrings $token
      } catch {
        throw 'N22 evidence requirement failed: strict JSON scanner found an invalid string'
      }
    }
    if ($character -eq '\') {
      $Index.Value = $Index.Value + 1
      Require ($Index.Value -lt $Text.Length) 'strict JSON scanner found an incomplete escape'
      if ($Text[$Index.Value] -eq 'u') {
        Require ($Index.Value + 4 -lt $Text.Length) 'strict JSON scanner found an incomplete Unicode escape'
        $hex = $Text.Substring($Index.Value + 1, 4)
        Require ($hex -match '^[0-9A-Fa-f]{4}$') 'strict JSON scanner found an invalid Unicode escape'
        $Index.Value = $Index.Value + 5
        continue
      }
    }
    $Index.Value = $Index.Value + 1
  }
  throw 'N22 evidence requirement failed: strict JSON scanner found an unterminated string'
}

function Read-JsonValueForDuplicateCheck([string]$Text, [ref]$Index) {
  Skip-JsonWhitespace $Text $Index
  Require ($Index.Value -lt $Text.Length) 'strict JSON scanner reached unexpected end of input'
  $character = $Text[$Index.Value]
  if ($character -eq '{') {
    $Index.Value = $Index.Value + 1
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    Skip-JsonWhitespace $Text $Index
    if ($Index.Value -lt $Text.Length -and $Text[$Index.Value] -eq '}') {
      $Index.Value = $Index.Value + 1
      return
    }
    while ($true) {
      Skip-JsonWhitespace $Text $Index
      $key = Read-JsonStringToken $Text $Index
      Require ($seen.Add([string]$key)) "JSON object contains a duplicate key: $key"
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

function ConvertFrom-StrictJsonText([string]$Text, [string]$Label) {
  Require (-not [string]::IsNullOrWhiteSpace($Text)) "$Label is empty"
  Require (-not $Text.StartsWith([string][char]0xFEFF, [StringComparison]::Ordinal)) "$Label has a Unicode BOM"
  Require ($Text.IndexOf([char]0) -lt 0) "$Label contains a NUL byte"
  $trimmed = $Text.TrimStart()
  try {
    if ($trimmed.StartsWith('[', [StringComparison]::Ordinal)) {
      # Windows PowerShell 5.1 turns [] into a scalar $null. Preserve the
      # empty-array shape explicitly while retaining [null] as one element.
      if ($trimmed -match '^\[\s*\]$') {
        $parsed = [object[]]@()
      } else {
        # ConvertFrom-Json already returns a native array for a top-level
        # array. Using -InputObject avoids wrapping that array in a second
        # pipeline collection on Windows PowerShell 5.1.
        $command = Get-Command ConvertFrom-Json
        if ($command.Parameters.ContainsKey('DateKind')) {
          $parsed = ConvertFrom-Json -InputObject $Text -DateKind String -ErrorAction Stop
        } else {
          $parsed = ConvertFrom-Json -InputObject $Text -ErrorAction Stop
        }
      }
    } else {
      $parsed = ConvertFrom-JsonPreservingDateStrings $Text
    }
  } catch {
    throw "N22 evidence requirement failed: $Label is invalid JSON"
  }
  $index = 0
  $reference = [ref]$index
  Read-JsonValueForDuplicateCheck $Text $reference
  Skip-JsonWhitespace $Text $reference
  Require ($reference.Value -eq $Text.Length) "$Label has trailing JSON data"
  if ($trimmed.StartsWith('[', [StringComparison]::Ordinal)) {
    return ,$parsed
  }
  $parsed
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
      throw "N22 evidence requirement failed: missing plain file: $full"
    }
    $parent = [IO.Path]::GetDirectoryName($current)
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -ceq $current) { break }
    $current = $parent
  }
}

function Get-CanonicalFilesystemPath([string]$Path) {
  Require (-not [string]::IsNullOrWhiteSpace($Path)) 'path is empty'
  $full = [IO.Path]::GetFullPath($Path)
  $volumeRoot = [IO.Path]::GetPathRoot($full)
  if (-not $full.Equals($volumeRoot, [StringComparison]::OrdinalIgnoreCase)) {
    $full = $full.TrimEnd([char[]]@('\', '/'))
  }
  $full
}

function Resolve-WorkspacePath([string]$Path) {
  Require (-not [string]::IsNullOrWhiteSpace($Path)) 'workspace path is empty'
  $candidate = $Path
  if (-not [IO.Path]::IsPathRooted($candidate)) {
    $candidate = Join-Path $Root $candidate
  }
  $full = Get-CanonicalFilesystemPath $candidate
  $rootFull = Get-CanonicalFilesystemPath $Root
  $rootPrefix = if ($rootFull.EndsWith('\', [StringComparison]::Ordinal)) { $rootFull } else { $rootFull + '\' }
  # Equality is intentional: callers may validate the workspace root itself.
  Require ($full.Equals($rootFull, [StringComparison]::OrdinalIgnoreCase) -or
           $full.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) "path is outside the workspace"
  Assert-PlainPathChain $full
  $full
}

function Relative-Path([string]$Path) {
  $full = Resolve-WorkspacePath $Path
  $rootFull = Get-CanonicalFilesystemPath $Root
  if ($full.Equals($rootFull, [StringComparison]::OrdinalIgnoreCase)) { return '.' }
  $rootPrefix = if ($rootFull.EndsWith('\', [StringComparison]::Ordinal)) { $rootFull } else { $rootFull + '\' }
  $full.Substring($rootPrefix.Length).Replace('\', '/')
}

function Get-FileEntry([string]$Path, [string]$Role) {
  # The lexical coordinator gate intentionally precedes every metadata/content read.
  Assert-NoExcludedCoordinatorToken $Path "$Role path"
  $full = Resolve-WorkspacePath $Path
  $item = Get-Item -LiteralPath $full
  [pscustomobject][ordered]@{
    role = $Role
    path = Relative-Path $full
    sha256 = File-Hash $full
    bytes = [int64]$item.Length
  }
}

function Get-N22ScopedFileEntry([string]$Path, [string]$Role) {
  # This stricter node-scope gate must run before Get-FileEntry reaches Get-Item/File-Hash.
  Assert-N22ScopedPathTokenPolicy $Path "$Role path"
  Get-FileEntry $Path $Role
}

function First-Line([string]$Path, [string]$Pattern) {
  Assert-PlainPathChain $Path $true
  $match = Select-String -LiteralPath $Path -Pattern $Pattern | Select-Object -First 1
  if ($null -eq $match) { return '' }
  $match.Line.Trim()
}

function Has-JsonProperty([object]$Value, [string]$Name) {
  $null -ne $Value -and $null -ne $Value.PSObject.Properties[$Name]
}

function Get-JsonPropertyValue([object]$Value, [string]$Name, [string]$Label) {
  Require (Has-JsonProperty $Value $Name) "$Label is missing property $Name"
  $Value.PSObject.Properties[$Name].Value
}

function Require-ExactJsonPropertyNames([object]$Value, [string[]]$Expected, [string]$Label) {
  Require ($null -ne $Value) "$Label is null"
  $actual = @($Value.PSObject.Properties.Name | Sort-Object)
  $wanted = @($Expected | Sort-Object)
  Require ($actual.Count -eq $wanted.Count -and (($actual -join "`n") -ceq ($wanted -join "`n"))) "$Label property set is not exact"
}

function Require-JsonInteger([object]$Value, [string]$Label) {
  $typeCode = if ($null -eq $Value) { [TypeCode]::Empty } else { [Type]::GetTypeCode($Value.GetType()) }
  $isInteger = @(
    [TypeCode]::SByte, [TypeCode]::Byte, [TypeCode]::Int16, [TypeCode]::UInt16,
    [TypeCode]::Int32, [TypeCode]::UInt32, [TypeCode]::Int64, [TypeCode]::UInt64
  ) -contains $typeCode
  Require $isInteger "$Label is not a JSON integer"
}

function Require-JsonIntegerValue([object]$Value, [int64]$Expected, [string]$Label) {
  Require-JsonInteger $Value $Label
  Require ([int64]$Value -eq $Expected) "$Label is not $Expected"
}

function Require-JsonBooleanValue([object]$Value, [bool]$Expected, [string]$Label) {
  Require ($Value -is [bool] -and $Value -eq $Expected) "$Label is not the expected JSON boolean"
}

function Require-JsonStringValue([object]$Value, [string]$Expected, [string]$Label) {
  Require ($Value -is [string] -and $Value -ceq $Expected) "$Label is not the expected JSON string"
}

function Quote-ProcessArgument([string]$Value) {
  Require ($null -ne $Value) 'process argument is null'
  Require ($Value.IndexOf([char]34) -lt 0) 'process argument contains a quote'
  if ($Value.Length -eq 0 -or $Value -match '\s') {
    return '"' + $Value + '"'
  }
  $Value
}

function Stop-CapturedProcessTree([System.Diagnostics.Process]$Process) {
  if ($null -eq $Process) { return }
  try {
    if ($Process.HasExited) { return }
  } catch {
    return
  }
  try {
    $systemDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::System)
    $taskKill = Join-Path $systemDirectory 'taskkill.exe'
    Require (Test-Path -LiteralPath $taskKill -PathType Leaf) 'taskkill.exe is unavailable for timeout cleanup'
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $taskKill
    $start.Arguments = '/PID ' + $Process.Id + ' /T /F'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $killer = New-Object System.Diagnostics.Process
    $killer.StartInfo = $start
    try {
      [void]$killer.Start()
      [void]$killer.WaitForExit(5000)
    } finally {
      $killer.Dispose()
    }
  } catch {
    try { $Process.Kill() } catch {}
  }
  try { [void]$Process.WaitForExit(5000) } catch {}
}

function Invoke-CapturedProcess(
  [string]$FilePath,
  [string]$Arguments,
  [int]$ProcessTimeoutSeconds,
  [string]$WorkingDirectory = $Root,
  [hashtable]$EnvironmentOverrides = @{},
  [AllowNull()][string]$StandardInputText = $null,
  [bool]$SanitizeGitConfig = $false,
  [string[]]$RemoveEnvironmentVariables = @()
) {
  Require ($ProcessTimeoutSeconds -gt 0) "process timeout must be positive: $FilePath"
  $resolvedWorkingDirectory = Resolve-WorkspacePath $WorkingDirectory
  $start = New-Object System.Diagnostics.ProcessStartInfo
  $start.FileName = $FilePath
  $start.Arguments = $Arguments
  $start.WorkingDirectory = $resolvedWorkingDirectory
  $start.UseShellExecute = $false
  $start.CreateNoWindow = $true
  $start.RedirectStandardOutput = $true
  $start.RedirectStandardError = $true
  $start.RedirectStandardInput = $null -ne $StandardInputText
  foreach ($key in @($start.EnvironmentVariables.Keys)) {
    $name = [string]$key
    $isRuntimeOverride = $name -match '(?i)^(?:QBRAIN|GBRAIN|OPENAI|ANTHROPIC|AZURE(?:_OPENAI)?|GEMINI|GOOGLE_AI|COHERE|MISTRAL|GROQ|DEEPSEEK|OPENROUTER|LLM)_'
    $isSecret = $name -match '(?i)(?:^|_)(?:API_KEY|AUTH_TOKEN|ACCESS_TOKEN|BEARER_TOKEN)$'
    $isProxy = $name -match '(?i)^(?:HTTP|HTTPS|ALL|NO)_PROXY$'
    $isGitRouting = $name -match '(?i)^GIT_(?:CONFIG(?:_.*)?|DIR|WORK_TREE|COMMON_DIR|INDEX_FILE|OBJECT_DIRECTORY|ALTERNATE_OBJECT_DIRECTORIES|CEILING_DIRECTORIES|DISCOVERY_ACROSS_FILESYSTEM|NAMESPACE)$'
    if ($isRuntimeOverride -or $isSecret -or $isProxy -or $isGitRouting) {
      [void]$start.EnvironmentVariables.Remove($name)
    }
  }
  if ($SanitizeGitConfig) {
    foreach ($key in @($start.EnvironmentVariables.Keys)) {
      if ([string]$key -like 'GIT_*') {
        [void]$start.EnvironmentVariables.Remove([string]$key)
      }
    }
  }
  foreach ($name in @($RemoveEnvironmentVariables)) {
    [void]$start.EnvironmentVariables.Remove($name)
  }
  foreach ($key in $EnvironmentOverrides.Keys) {
    $start.EnvironmentVariables[$key] = [string]$EnvironmentOverrides[$key]
  }

  $process = New-Object System.Diagnostics.Process
  $process.StartInfo = $start
  try {
    $started = [DateTimeOffset]::UtcNow
    [void]$process.Start()
    if ($null -ne $StandardInputText) {
      $process.StandardInput.Write($StandardInputText)
      $process.StandardInput.Close()
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($ProcessTimeoutSeconds * 1000)) {
      Stop-CapturedProcessTree $process
      try { [void]$stdoutTask.Wait(5000); [void]$stderrTask.Wait(5000) } catch {}
      throw "process timed out after $ProcessTimeoutSeconds seconds: $FilePath"
    }
    $process.WaitForExit()
    Require ($stdoutTask.Wait(5000) -and $stderrTask.Wait(5000)) `
        "process output capture did not complete: $FilePath"
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    Require ([Text.Encoding]::UTF8.GetByteCount($stdout) -le $MaximumCapturedOutputBytes) `
        "process stdout exceeds the $MaximumCapturedOutputBytes-byte evidence bound: $FilePath"
    Require ([Text.Encoding]::UTF8.GetByteCount($stderr) -le $MaximumCapturedOutputBytes) `
        "process stderr exceeds the $MaximumCapturedOutputBytes-byte evidence bound: $FilePath"
    $ended = [DateTimeOffset]::UtcNow
    [pscustomobject]@{
      stdout = $stdout
      stderr = $stderr
      exit_code = $process.ExitCode
      started_utc = $started
      ended_utc = $ended
    }
  } finally {
    try {
      if (-not $process.HasExited) { Stop-CapturedProcessTree $process }
    } catch {}
    $process.Dispose()
  }
}

function Invoke-GitRead([string]$Arguments) {
  Assert-PlainPathChain $Root
  Assert-PlainPathChain $ExpectedGitDirectory
  Require (Test-Path -LiteralPath $ExpectedGitDirectory -PathType Container) `
      'expected workspace Git directory is missing'
  $quotedGitDirectory = Quote-ProcessArgument $ExpectedGitDirectory
  $quotedWorkTree = Quote-ProcessArgument (Get-CanonicalFilesystemPath $Root)
  $fullArguments = "--no-pager --literal-pathspecs --git-dir=$quotedGitDirectory --work-tree=$quotedWorkTree -c core.autocrlf=false -c core.safecrlf=false -c core.quotePath=false -c diff.external= -c pager.diff=false -c pager.branch=false $Arguments"
  $gitEnvironment = @{
    GIT_CONFIG_NOSYSTEM = '1'
    GIT_CONFIG_SYSTEM = 'NUL'
    GIT_CONFIG_GLOBAL = 'NUL'
    GIT_TERMINAL_PROMPT = '0'
    GIT_ASKPASS = 'NUL'
    GIT_PAGER = 'cat'
    PAGER = 'cat'
    GIT_EDITOR = 'true'
    GIT_SEQUENCE_EDITOR = 'true'
    GIT_OPTIONAL_LOCKS = '0'
    GCM_INTERACTIVE = 'Never'
  }
  $result = Invoke-CapturedProcess 'git.exe' $fullArguments 60 $Root $gitEnvironment $null $true
  Require ($result.exit_code -eq 0) "read-only Git command failed"
  Require ([string]::IsNullOrWhiteSpace($result.stderr)) "read-only Git command wrote stderr"
  $result.stdout.Trim()
}

function Get-GitState {
  $topLevel = [IO.Path]::GetFullPath((Invoke-GitRead 'rev-parse --show-toplevel').Replace('/', '\'))
  Require ($topLevel.Equals([IO.Path]::GetFullPath($Root), [StringComparison]::OrdinalIgnoreCase)) `
      'Git top-level is not the verifier workspace'
  $head = Invoke-GitRead 'rev-parse HEAD'
  Require ($head -match '^[0-9a-fA-F]{40,64}$') "Git HEAD is not a commit id"
  $branch = Invoke-GitRead 'branch --show-current'
  $gitDirText = Invoke-GitRead 'rev-parse --git-dir'
  $gitDir = $gitDirText
  if (-not [IO.Path]::IsPathRooted($gitDir)) {
    $gitDir = Join-Path $Root $gitDir
  }
  $gitDir = [IO.Path]::GetFullPath($gitDir)
  Assert-PlainPathChain $gitDir
  Require ($gitDir.Equals($ExpectedGitDirectory, [StringComparison]::OrdinalIgnoreCase)) `
      'Git directory is not the expected workspace Git directory'
  $commonDirText = Invoke-GitRead 'rev-parse --git-common-dir'
  $commonDir = $commonDirText
  if (-not [IO.Path]::IsPathRooted($commonDir)) {
    $commonDir = Join-Path $Root $commonDir
  }
  $commonDir = [IO.Path]::GetFullPath($commonDir)
  Assert-PlainPathChain $commonDir
  Require ($commonDir.Equals($ExpectedGitDirectory, [StringComparison]::OrdinalIgnoreCase)) `
      'Git common directory is not the expected workspace Git directory'
  $rows = New-Object System.Collections.Generic.List[string]
  $rows.Add("HEAD $($head.ToLowerInvariant())")
  $headLog = Join-Path $gitDir 'logs\HEAD'
  if (Test-Path -LiteralPath $headLog -PathType Leaf) {
    $rows.Add("LOG logs/HEAD $(File-Hash $headLog)")
  } else {
    $rows.Add('LOG logs/HEAD absent')
  }
  $remoteLogs = Join-Path $gitDir 'logs\refs\remotes'
  if (Test-Path -LiteralPath $remoteLogs -PathType Container) {
    foreach ($path in @(Get-PlainTreeFiles $remoteLogs 'Git reference-log' | Sort-Object)) {
      $relative = $path.Substring($gitDir.Length + 1).Replace('\', '/')
      Assert-NoExcludedCoordinatorToken $relative 'Git reference-log path'
      $rows.Add("LOG $relative $(File-Hash $path)")
    }
  }
  [pscustomobject][ordered]@{
    head = $head.ToLowerInvariant()
    branch = $branch
    reference_log_fingerprint_sha256 = Text-Hash (($rows.ToArray()) -join "`n")
  }
}

function Get-ProtectedRepoFiles {
  $paths = New-Object System.Collections.Generic.List[string]
  foreach ($relative in @('.codex', '.claude', '.opencode')) {
    $directory = Join-Path $Root $relative
    if (Test-Path -LiteralPath $directory -PathType Container) {
      foreach ($path in Get-PlainTreeFiles $directory 'protected configuration') {
        $workspaceRelative = Relative-Path $path
        Assert-NoExcludedCoordinatorToken $workspaceRelative 'protected configuration path'
        $paths.Add($path)
      }
    }
  }
  foreach ($path in [IO.Directory]::EnumerateFiles(
      $Root, '*', [IO.SearchOption]::TopDirectoryOnly)) {
    $workspaceRelative = Relative-Path $path
    Assert-NoExcludedCoordinatorToken $workspaceRelative 'protected configuration path'
    if ([IO.Path]::GetFileName($path) -match '(?i)^(?:codex|claude|opencode|model-config|llm-config).*[.](?:json|toml|ya?ml)$') {
      $paths.Add($path)
    }
  }
  @($paths | Sort-Object -Unique)
}

function Get-BuildClosureFiles {
  $paths = New-Object System.Collections.Generic.List[string]
  foreach ($directory in @('src', 'include', 'tests')) {
    $absolute = Join-Path $Root $directory
    foreach ($path in Get-PlainTreeFiles $absolute 'build-closure' $true) {
      $relative = Relative-Path $path
      # Directory enumeration yields only the lexical path. Reject the forbidden
      # token before extension inspection, Get-Item, content reads, or hashing.
      Assert-NoExcludedCoordinatorToken $relative 'build-closure path'
      if ([IO.Path]::GetExtension($path) -in @('.cpp', '.c', '.hpp', '.h')) {
        $paths.Add($relative)
      }
    }
  }
  foreach ($relative in @(
      'CMakeLists.txt',
      'scripts/build-cl.ps1',
      'scripts/build-tests-cl.ps1',
      'scripts/n22-verify.ps1',
      'third_party/nlohmann/json.hpp',
      'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.c',
      'third_party/sqlite/sqlite-amalgamation-3460100/sqlite3.h')) {
    Assert-NoExcludedCoordinatorToken $relative 'build-closure path'
    $paths.Add($relative)
  }
  @($paths | Sort-Object -Unique)
}

function Assert-NoExcludedNodeReference([string]$Text, [string]$Label) {
  Assert-NoExcludedCoordinatorToken $Text $Label
}

function Assert-SafeEvidenceText([string]$Text, [string]$Label) {
  Assert-NoExcludedNodeReference $Text $Label
  Require ($Text -notmatch '(?i)\bsk-[A-Za-z0-9_-]{16,}\b') "$Label contains a secret-like key"
  Require ($Text -notmatch '(?i)\bBearer\s+[A-Za-z0-9._~+/-]{12,}={0,2}\b') "$Label contains a secret-like bearer value"
  Require ($Text -notmatch '(?i)\bgit(?:[.]exe)?\s+(?:commit|push)\b') "$Label contains a prohibited Git mutation command"
}

function Assert-NoExcludedManifestPath([object[]]$Entries) {
  foreach ($entry in @($Entries)) {
    Assert-NoExcludedCoordinatorToken ([string]$entry.path) 'manifest path'
  }
}

function Assert-ScopedPathPolicy {
  foreach ($relative in $N22Deliverables) {
    Assert-N22ScopedPathTokenPolicy $relative 'N22 deliverable scope'
  }
  foreach ($relative in $N22ScopedDiffPaths) {
    Assert-N22ScopedPathTokenPolicy $relative 'N22 diff scope'
  }
}

function Assert-N22BuildClosurePath([string]$Path) {
  # The complete regression suite intentionally contains historical tests from
  # outside N22. It is frozen as regression input, not treated as N22 behavior.
  # The globally forbidden coordinator is still rejected here; the stricter
  # later-node policy applies to every N22 scoped/production-slice input.
  $relative = Relative-Path $Path
  Assert-NoExcludedCoordinatorToken $relative 'N22 build closure'
}

function Assert-NoForbiddenCommandsInVerifierScope {
  foreach ($relative in @('scripts/build-cl.ps1', 'scripts/build-tests-cl.ps1', 'scripts/n22-verify.ps1')) {
    Assert-N22ScopedPathTokenPolicy $relative 'verifier command-scope input'
    $path = Join-Path $Root $relative
    Assert-PlainPathChain $path $true
    $text = Get-Content -Raw -LiteralPath $path
    Require ($text -notmatch '(?i)\bgit(?:[.]exe)?\s+(?:commit|push)\b') "$relative contains a prohibited Git mutation command"
  }
}

function Get-ScopedDiffFacts {
  foreach ($relative in $N22ScopedDiffPaths) {
    Assert-N22ScopedPathTokenPolicy $relative 'N22 scoped diff input'
  }
  $paths = ($N22ScopedDiffPaths | ForEach-Object {
      Quote-ProcessArgument $_.Replace('\', '/')
    }) -join ' '
  $trackedDiff = Invoke-GitRead "diff --no-ext-diff --no-textconv --unified=0 HEAD -- $paths"
  $trackedNamesText = Invoke-GitRead "diff --no-ext-diff --no-textconv --name-only HEAD -- $paths"
  $untrackedNamesText = Invoke-GitRead "ls-files --others --exclude-standard -- $paths"
  $trackedNames = @($trackedNamesText -split '\r?\n' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  $untrackedNames = @($untrackedNamesText -split '\r?\n' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  $changedNames = @($trackedNames + $untrackedNames | Sort-Object -Unique)
  foreach ($name in $changedNames) {
    Require ($name -notmatch '(?i)(^|/)(?:[.]codex|[.]claude|[.]opencode)(/|$)') "protected configuration path appears in the scoped diff"
  }
  $untrackedDiff = New-Object System.Collections.Generic.List[string]
  foreach ($name in $untrackedNames) {
    Assert-N22ScopedPathTokenPolicy $name 'N22 untracked diff input'
    $full = Resolve-WorkspacePath $name
    Require (Test-Path -LiteralPath $full -PathType Leaf) "untracked scoped input disappeared: $name"
    $untrackedDiff.Add("diff --untracked a/$name b/$name")
    foreach ($line in @((Get-Content -Raw -LiteralPath $full) -split '\r?\n')) {
      $untrackedDiff.Add('+' + $line)
    }
  }
  $diff = $trackedDiff
  if ($untrackedDiff.Count -gt 0) {
    $diff += "`n" + ($untrackedDiff -join "`n")
  }
  $settingPattern = '(?i)["'']?\b(?:base[_-]?url|api[_-]?key|provider|model(?:[_-]?name)?|reasoning(?:[_-]?effort)?|context[_-]?(?:size|window)|compression[_-]?threshold)\b["'']?\s*(?:=|:)'
  $changedLines = @($diff -split '\r?\n' | Where-Object {
      ($_ -match '^[+-]') -and ($_ -notmatch '^(?:[+]{3}|[-]{3})')
    })
  $protectedAssignments = @($changedLines | Where-Object { $_ -match $settingPattern })
  Require ($protectedAssignments.Count -eq 0) "protected model/provider setting assignment appears in the N22 scoped diff"
  [pscustomobject][ordered]@{
    diff_sha256 = Text-Hash $diff
    changed_path_count = $changedNames.Count
    protected_path_change_count = 0
    protected_assignment_change_count = 0
  }
}

function Get-QuotedSourceArray([string]$Path, [string]$VariableName) {
  Assert-NoExcludedCoordinatorToken $Path "$VariableName source-list path"
  Assert-PlainPathChain $Path $true
  $text = Get-Content -Raw -LiteralPath $Path
  $pattern = '(?s)\$' + [regex]::Escape($VariableName) + '\s*=\s*@\((.*?)\r?\n\)'
  $match = [regex]::Match($text, $pattern)
  Require ($match.Success) "cannot parse $VariableName from $(Relative-Path $Path)"
  $values = @([regex]::Matches($match.Groups[1].Value, '"([^"]+[.](?:cpp|c))"') | ForEach-Object { $_.Groups[1].Value })
  Require ($values.Count -gt 0) "$VariableName is empty"
  $values
}

function Get-RegisteredTests {
  $relative = 'tests/test_main.cpp'
  Assert-N22ScopedPathTokenPolicy $relative 'registered-test input'
  $path = Join-Path $Root $relative
  Assert-PlainPathChain $path $true
  $text = Get-Content -Raw -LiteralPath $path
  $matches = [regex]::Matches($text, '\{\s*"([^"]+)"\s*,\s*test_[A-Za-z0-9_]+\s*\}')
  $names = @($matches | ForEach-Object { $_.Groups[1].Value })
  Require ($names.Count -ge $MinimumRegisteredTests) "registered suite is below the N19 baseline"
  Require (($names | Sort-Object -Unique).Count -eq $names.Count) "registered test names are not unique"
  Require (@($names | Where-Object { $_ -ceq 'n22' }).Count -eq 1) "dedicated n22 test is not registered exactly once"
  $names
}

function Assert-BuildAndTestWiring {
  foreach ($relative in @(
      'scripts/build-cl.ps1', 'scripts/build-tests-cl.ps1', 'CMakeLists.txt',
      'tests/test_main.cpp', 'tests/test_n22.cpp')) {
    Assert-N22ScopedPathTokenPolicy $relative 'N22 build/test wiring input'
  }
  $productionSources = @(Get-QuotedSourceArray $ProductionBuildScript 'productionSources')
  $testSources = @(Get-QuotedSourceArray $TestBuildScript 'defaultTestSources')
  Require (@($productionSources | Where-Object { $_ -ieq 'src\qbrain\codeintel\scan.cpp' }).Count -eq 1) "production build does not compile scan.cpp exactly once"
  Require (@($productionSources | Where-Object { $_ -ieq 'src\qbrain\ops\handlers.cpp' }).Count -eq 1) "production build does not compile handlers.cpp exactly once"
  Require (@($productionSources | Where-Object { $_ -ieq 'src\qbrain\mcp\server.cpp' }).Count -eq 1) "production build does not compile server.cpp exactly once"
  Require (@($testSources | Where-Object { $_ -ieq 'tests\test_n22.cpp' }).Count -eq 1) "native test build does not compile test_n22.cpp exactly once"

  $cmakePath = Join-Path $Root 'CMakeLists.txt'
  $mainPath = Join-Path $Root 'tests\test_main.cpp'
  $focusedTestPath = Join-Path $Root 'tests\test_n22.cpp'
  foreach ($path in @($cmakePath, $mainPath, $focusedTestPath, $ProductionBuildScript, $TestBuildScript)) {
    Assert-PlainPathChain $path $true
  }
  $cmake = Get-Content -Raw -LiteralPath $cmakePath
  Require ([regex]::Matches($cmake, '(?im)^\s*tests/test_n22[.]cpp\s*$').Count -eq 1) "CMake does not register test_n22.cpp exactly once"
  $main = Get-Content -Raw -LiteralPath $mainPath
  Require ([regex]::Matches($main, '(?m)^void\s+test_n22\s*\(\s*\)\s*;\s*$').Count -eq 1) "test_main.cpp does not declare test_n22 exactly once"
  Require ([regex]::Matches($main, '\{\s*"n22"\s*,\s*test_n22\s*\}').Count -eq 1) "test_main.cpp does not register n22 exactly once"

  $focusedTest = Get-Content -Raw -LiteralPath $focusedTestPath
  $singleErrorBlock = '(?s)json\s+require_mcp_error\s*\(.*?content[.]is_array\(\)\s*&&\s*content[.]size\(\)\s*==\s*1.*?json::parse\s*\(\s*content\[0\]\["text"\]'
  Require ([regex]::Matches($focusedTest, $singleErrorBlock).Count -eq 1) "test_n22.cpp does not enforce one structured MCP error block"
  Require ([regex]::Matches($focusedTest, '"damaged:direct:"\s*\+\s*operation[.]name').Count -eq 1) "test_n22.cpp lacks the direct damaged-database matrix"
  Require ([regex]::Matches($focusedTest, '"damaged:mcp:"\s*\+\s*operation[.]name').Count -eq 1) "test_n22.cpp lacks the MCP damaged-database matrix"

  $productionText = Get-Content -Raw -LiteralPath $ProductionBuildScript
  $testText = Get-Content -Raw -LiteralPath $TestBuildScript
  Require ($productionText -match '/std:c\+\+20' -and $testText -match '/std:c\+\+20') "native build scripts do not both require C++20"
  Require ($productionText -match 'vcvarsall[.]bat' -and $productionText -match '\bx64\b') "production build is not native x64 MSVC"
  Require ($testText -match 'vcvarsall[.]bat' -and $testText -match '\bx64\b') "test build is not native x64 MSVC"

  [pscustomobject][ordered]@{
    production_sources = $productionSources
    test_sources = $testSources
  }
}

function Assert-DependencyContracts {
  $evidence = New-Object System.Collections.Generic.List[object]
  foreach ($dependency in $DependencyContracts) {
    $plan = Join-Path $Root "docs\nodes\$($dependency.Node)-PLAN.md"
    $planAudit = Join-Path $Root "docs\nodes\$($dependency.Node)-PLAN-AUDIT.md"
    $hardAudit = Join-Path $Root "docs\nodes\$($dependency.Node)-HARD-AUDIT.md"
    foreach ($path in @($plan, $planAudit, $hardAudit)) {
      Assert-NoExcludedCoordinatorToken $path 'dependency evidence path'
      Require (Test-Path -LiteralPath $path -PathType Leaf) "missing dependency artifact for $($dependency.Node)"
    }
    Require ((First-Line $plan '(?i)^\*\*Status') -match '(?i)\bdone\b') "$($dependency.Node) is not done"
    $planVerdict = First-Line $planAudit '(?i)^\*\*VERDICT'
    $hardVerdict = First-Line $hardAudit '(?i)^\*\*VERDICT'
    Require ($planVerdict -match '(?i)\bPASS\b' -and $planVerdict -notmatch '(?i)\bFAIL\b') "$($dependency.Node) plan audit is not PASS"
    Require ($hardVerdict -match '(?i)\bPASS\b' -and $hardVerdict -notmatch '(?i)\bFAIL\b') "$($dependency.Node) outcome audit is not PASS"
    Require ((Get-Content -Raw -LiteralPath $planAudit) -match '(?i)Auditor[^\r\n]*Claude Code') "$($dependency.Node) plan auditor is not Claude Code"
    Require ((Get-Content -Raw -LiteralPath $hardAudit) -match '(?i)Auditor[^\r\n]*Claude Code') "$($dependency.Node) outcome auditor is not Claude Code"
    Require ((File-Hash $planAudit) -ceq $dependency.Plan) "$($dependency.Node) plan-audit hash changed"
    Require ((File-Hash $hardAudit) -ceq $dependency.Outcome) "$($dependency.Node) outcome-audit hash changed"
    $evidence.Add([pscustomobject][ordered]@{
        node = $dependency.Node
        plan_audit_sha256 = $dependency.Plan
        outcome_audit_sha256 = $dependency.Outcome
      })
  }
  $evidence.ToArray()
}

function Assert-N22Governance {
  Require (Test-Path -LiteralPath $PlanPath -PathType Leaf) "N22 plan is missing"
  Require (Test-Path -LiteralPath $PlanAuditPath -PathType Leaf) "N22 plan audit is missing"
  Require ((File-Hash $PlanPath) -ceq $ExpectedApprovedPlanHash) "approved N22 plan hash changed"
  Require ((File-Hash $PlanAuditPath) -ceq $ExpectedPlanAuditHash) "N22 plan-audit hash changed"
  $status = First-Line $PlanPath '(?i)^\*\*Status\*\*:'
  $outcome = First-Line $PlanPath '(?i)^\*\*Outcome audit\*\*:'
  $verdict = First-Line $PlanAuditPath '(?i)^\*\*VERDICT'
  $auditor = First-Line $PlanAuditPath '(?i)^\*\*Auditor'
  Require ($status -match '(?i)^\*\*Status\*\*:\s*approved\s*$') "N22 plan is not approved"
  Require ($outcome -match '(?i)\bpending\b') "N22 outcome gate is not pending"
  Require ($verdict -match '(?i)\bPASS\b' -and $verdict -notmatch '(?i)\bFAIL\b') "N22 plan audit is not PASS"
  Require ($auditor -match '(?i)Claude Code') "N22 plan auditor is not Claude Code"
  $auditText = Get-Content -Raw -LiteralPath $PlanAuditPath
  $draftHashMatch = [regex]::Match($auditText, '(?im)^\*\*Plan SHA-256\*\*:\s*`?([0-9a-f]{64})`?\s*$')
  Require ($draftHashMatch.Success -and $draftHashMatch.Groups[1].Value -ceq $ExpectedAuditedDraftPlanHash) "N22 audit is not bound to the recorded draft plan"
  [pscustomobject][ordered]@{
    approved_plan_sha256 = $ExpectedApprovedPlanHash
    audited_draft_plan_sha256 = $ExpectedAuditedDraftPlanHash
    plan_audit_sha256 = $ExpectedPlanAuditHash
    plan_status = 'approved'
    outcome_audit = 'pending'
    plan_audit_verdict = 'PASS'
    plan_auditor = 'Claude Code'
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
  $combined = $capture.stdout + "`n" + $capture.stderr
  $versionLines = @($combined -split '\r?\n' | Where-Object { $_ -match 'Compiler Version .+ for x64' })
  Require ($versionLines.Count -eq 1) "full x64 cl.exe version was not captured exactly once"
  try {
    $value = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
  } catch {
    throw 'N22 evidence requirement failed: Windows 11 build metadata is unavailable'
  }
  Require ([string]$value.Caption -match '(?i)\bWindows 11\b') 'verification host is not Windows 11'
  Require ([string]$value.BuildNumber -match '^[0-9]+$') 'Windows build number is unavailable'
  $os = "$($value.Caption) $($value.Version) build $($value.BuildNumber)"
  [pscustomobject][ordered]@{
    os = $os
    os_architecture = 'X64'
    process_architecture = 'X64'
    target_architecture = 'x64'
    language_mode = '/std:c++20'
    compiler = $versionLines[0].Trim()
  }
}

function New-InputManifestEntries {
  $entries = New-Object System.Collections.Generic.List[object]
  foreach ($relative in @(Get-BuildClosureFiles)) {
    # Get-BuildClosureFiles already applied the coordinator token gate to the
    # lexical path before this metadata/hash entry is created.
    Assert-N22BuildClosurePath (Join-Path $Root $relative)
    $entries.Add((Get-FileEntry $relative 'build-input'))
  }
  foreach ($relative in @('AGENTS.md', 'docs/nodes/README.md', 'docs/nodes/N22-PLAN.md', 'docs/nodes/N22-PLAN-AUDIT.md')) {
    $entries.Add((Get-FileEntry (Join-Path $Root $relative) 'governance-input'))
  }
  foreach ($dependency in $DependencyContracts) {
    foreach ($suffix in @('PLAN.md', 'PLAN-AUDIT.md', 'HARD-AUDIT.md')) {
      $entries.Add((Get-FileEntry (Join-Path $Root "docs\nodes\$($dependency.Node)-$suffix") 'dependency-input'))
    }
  }
  @($entries | Sort-Object path -Unique)
}

function New-ScopedInputEntries {
  $entries = foreach ($relative in @($N22Deliverables + $RelevantSchemaInputs | Sort-Object -Unique)) {
    Assert-N22ScopedPathTokenPolicy $relative 'N22 scoped manifest input'
    $full = Join-Path $Root $relative
    Require (Test-Path -LiteralPath $full -PathType Leaf) "missing N22 scoped input: $relative"
    Get-N22ScopedFileEntry $full 'n22-scoped-input'
  }
  @($entries | Sort-Object path)
}

function New-N19StorageBaselineEvidence {
  $entries = New-Object System.Collections.Generic.List[object]
  foreach ($item in $N19StorageBaseline.GetEnumerator()) {
    $relative = [string]$item.Key
    Assert-N22ScopedPathTokenPolicy $relative 'N19 storage-baseline input'
    $entry = Get-N22ScopedFileEntry (Join-Path $Root $relative) 'n19-schema-v12-storage'
    Require ([string]$entry.sha256 -ceq [string]$item.Value) `
        "N19 schema-v12 storage baseline hash changed: $relative"
    $entries.Add($entry)
  }
  $orderedEntries = @($entries.ToArray() | Sort-Object path)
  $bindingText = @($orderedEntries | ForEach-Object {
      "$($_.path)=$($_.sha256):$($_.bytes)"
    }) -join "`n"
  [pscustomobject][ordered]@{
    kind = 'n19-schema-v12-storage'
    entries = $orderedEntries
    binding_sha256 = Text-Hash $bindingText
  }
}

function Get-N22HandlerSliceText([string]$HandlersText) {
  $sectionMarker = '// N22 code intel extensions'
  $sectionMatches = [regex]::Matches($HandlersText, [regex]::Escape($sectionMarker))
  Require ($sectionMatches.Count -eq 1) 'handlers.cpp lacks one exact N22 section marker'
  $start = $sectionMatches[0].Index
  $cacheToken = '"code_traversal_cache_clear"'
  $cacheIndex = $HandlersText.IndexOf($cacheToken, $start, [StringComparison]::Ordinal)
  Require ($cacheIndex -ge $start) 'handlers.cpp lacks the N22 cache compatibility registration'
  $terminator = ')");'
  $endMarker = $HandlersText.IndexOf($terminator, $cacheIndex, [StringComparison]::Ordinal)
  Require ($endMarker -gt $cacheIndex) 'handlers.cpp N22 section lacks a bounded registration terminator'
  $slice = $HandlersText.Substring($start, $endMarker + $terminator.Length - $start)

  $registered = @([regex]::Matches($slice, 'register_one\s*\(\s*"([^"]+)"') |
      ForEach-Object { $_.Groups[1].Value })
  Require ($registered.Count -eq $N22OperationNames.Count) `
      'handlers.cpp N22 slice does not contain exactly four registrations'
  Require (($registered -join "`n") -ceq ($N22OperationNames -join "`n")) `
      'handlers.cpp N22 registration order/names are not exact'

  foreach ($api in @(
      'find_callees_in_source', 'find_flow_in_source', 'find_blast_in_source')) {
    Require ([regex]::Matches($slice, 'codeintel::' + [regex]::Escape($api) + '\s*\(').Count -eq 1) `
        "handlers.cpp N22 slice does not call $api exactly once"
  }
  Require ($slice -notmatch 'codeintel::find_(?:callees|flow|blast)\s*\(') `
      'handlers.cpp N22 slice references a legacy unscoped scanner API'

  $allRegistered = @([regex]::Matches(
      $HandlersText, 'register_one\s*\(\s*"([^"]+)"') |
      ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
  foreach ($operation in $allRegistered) {
    if ($N22OperationNames -contains $operation) { continue }
    Require ($slice.IndexOf('"' + $operation + '"', [StringComparison]::Ordinal) -lt 0) `
        'handlers.cpp N22 slice references an operation outside its approved slice'
  }
  Assert-NoFutureNodeReference $slice 'handlers.cpp N22 production slice'
  $slice
}

function New-N22ProductionSliceEvidence {
  foreach ($relative in $N22ProductionSlicePaths) {
    Assert-N22ScopedPathTokenPolicy $relative 'N22 production-slice path'
  }

  $headerPath = Join-Path $Root 'include\qbrain\codeintel\scan.hpp'
  $scannerPath = Join-Path $Root 'src\qbrain\codeintel\scan.cpp'
  $handlersPath = Join-Path $Root 'src\qbrain\ops\handlers.cpp'
  $mcpPath = Join-Path $Root 'src\qbrain\mcp\server.cpp'
  foreach ($path in @($headerPath, $scannerPath, $handlersPath, $mcpPath)) {
    Assert-PlainPathChain $path $true
    Require (Test-Path -LiteralPath $path -PathType Leaf) 'N22 production-slice input is missing'
  }

  $headerText = Get-Content -Raw -LiteralPath $headerPath
  $scannerText = Get-Content -Raw -LiteralPath $scannerPath
  $handlersText = Get-Content -Raw -LiteralPath $handlersPath
  $mcpText = Get-Content -Raw -LiteralPath $mcpPath
  Assert-NoFutureNodeReference $headerText 'scan.hpp production slice'
  Assert-NoFutureNodeReference $scannerText 'scan.cpp production slice'
  Assert-NoExcludedCoordinatorToken $mcpText 'mcp/server.cpp production slice'
  $handlerSlice = Get-N22HandlerSliceText $handlersText

  $entries = New-Object System.Collections.Generic.List[object]
  $entries.Add((Get-N22ScopedFileEntry $headerPath 'n22-scanner-header'))
  $entries.Add((Get-N22ScopedFileEntry $scannerPath 'n22-scanner-source'))
  $entries.Add((Get-N22ScopedFileEntry $mcpPath 'n22-mcp-source'))
  $entries.Add([pscustomobject][ordered]@{
      role = 'n22-handler-registration-slice'
      path = 'src/qbrain/ops/handlers.cpp#n22-registration-slice'
      sha256 = Text-Hash $handlerSlice
      bytes = [int64][Text.Encoding]::UTF8.GetByteCount($handlerSlice)
    })
  $orderedEntries = @($entries.ToArray() | Sort-Object path)
  $bindingText = @($orderedEntries | ForEach-Object {
      "$($_.role):$($_.path)=$($_.sha256):$($_.bytes)"
    }) -join "`n"
  [pscustomobject][ordered]@{
    kind = 'n22-production-slice'
    entries = $orderedEntries
    binding_sha256 = Text-Hash $bindingText
  }
}

function Assert-HashBindingCurrent([object]$Recorded, [object]$Current, [string]$Label) {
  Require-ExactJsonPropertyNames $Recorded @('kind', 'entries', 'binding_sha256') $Label
  Require ([string]$Recorded.kind -ceq [string]$Current.kind) "$Label kind changed"
  Require ([string]$Recorded.binding_sha256 -ceq [string]$Current.binding_sha256) `
      "$Label binding hash changed"
  Assert-EntrySetCurrent @($Recorded.entries) @($Current.entries) "$Label entries"
}

function Assert-EntrySetCurrent([object[]]$Recorded, [object[]]$Current, [string]$Label) {
  Require ($Recorded.Count -eq $Current.Count) "$Label file count changed after preparation"
  $recordedMap = @{}
  foreach ($entry in $Recorded) {
    Require-ExactJsonPropertyNames $entry @('role', 'path', 'sha256', 'bytes') "$Label entry"
    Require (-not $recordedMap.ContainsKey([string]$entry.path)) "$Label contains a duplicate path"
    $recordedMap[[string]$entry.path] = $entry
  }
  foreach ($entry in $Current) {
    Require-ExactJsonPropertyNames $entry @('role', 'path', 'sha256', 'bytes') "$Label current entry"
    Require ($recordedMap.ContainsKey([string]$entry.path)) "$Label gained an unprepared path: $($entry.path)"
    $old = $recordedMap[[string]$entry.path]
    Require ([string]$old.role -ceq [string]$entry.role) "$Label role changed: $($entry.path)"
    Require ([string]$old.sha256 -ceq [string]$entry.sha256) "$Label hash changed after preparation: $($entry.path)"
    Require ([int64]$old.bytes -eq [int64]$entry.bytes) "$Label length changed after preparation: $($entry.path)"
  }
}

function Assert-IsolatedTestConfig([string]$Sandbox, [string]$Label) {
  Assert-PlainPathChain $Sandbox
  $canonicalPath = Join-Path $Sandbox 'Qbrain\config.json'
  $configFiles = @(Get-PlainTreeFiles $Sandbox "$Label disposable sandbox" |
      ForEach-Object { Get-Item -LiteralPath $_ -Force } |
      Where-Object { $_.Name -ieq 'config.json' })
  Require ($configFiles.Count -le 1) "$Label created more than one config.json"
  if ($configFiles.Count -eq 0) {
    return [pscustomobject][ordered]@{ count=0; sha256='absent' }
  }
  Require ([IO.Path]::GetFullPath($configFiles[0].FullName).Equals(
      [IO.Path]::GetFullPath($canonicalPath), [StringComparison]::OrdinalIgnoreCase)) "$Label created config.json outside the disposable canonical path"
  try {
    $config = Get-Content -Raw -LiteralPath $canonicalPath | ConvertFrom-Json -ErrorAction Stop
  } catch {
    throw "N22 evidence requirement failed: $Label config.json is invalid JSON"
  }
  Require-ExactJsonPropertyNames $config @('brain_id', 'embedding', 'chat', 'search') "$Label config"
  Require ($config.brain_id -is [string] -and -not [string]::IsNullOrWhiteSpace($config.brain_id)) "$Label config brain_id is not a non-empty string"
  Require-ExactJsonPropertyNames $config.embedding @('provider', 'model', 'base_url', 'dimensions') "$Label embedding config"
  Require ($config.embedding.provider -ceq 'openai') "$Label embedding provider differs from the canonical default"
  Require ($config.embedding.model -ceq 'text-embedding-3-small') "$Label embedding model differs from the canonical default"
  Require ($config.embedding.base_url -ceq 'https://api.openai.com/v1') "$Label embedding base URL differs from the canonical default"
  Require-JsonIntegerValue $config.embedding.dimensions 1536 "$Label embedding dimensions"
  Require-ExactJsonPropertyNames $config.chat @('model', 'base_url') "$Label chat config"
  Require ($config.chat.model -ceq 'gpt-4o-mini') "$Label chat model differs from the canonical default"
  Require ($config.chat.base_url -ceq 'https://api.openai.com/v1') "$Label chat base URL differs from the canonical default"
  Require-ExactJsonPropertyNames $config.search @('rrf_k', 'default_limit') "$Label search config"
  Require-JsonIntegerValue $config.search.rrf_k 60 "$Label search rrf_k"
  Require-JsonIntegerValue $config.search.default_limit 10 "$Label search default_limit"
  [pscustomobject][ordered]@{ count=1; sha256=File-Hash $canonicalPath }
}

function Remove-SafeTemporaryDirectory([string]$Path, [string]$RequiredLeafPrefix) {
  $full = Get-CanonicalFilesystemPath $Path
  $tempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
  $leaf = [IO.Path]::GetFileName($full)
  Require ($full.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) "temporary cleanup target is outside the system temp directory"
  Require ($leaf.StartsWith($RequiredLeafPrefix, [StringComparison]::Ordinal)) "temporary cleanup target has an unexpected name"
  Assert-PlainPathChain $full
  if (Test-Path -LiteralPath $full) {
    $rootItem = Get-Item -LiteralPath $full -Force
    Require ($rootItem.PSIsContainer) 'temporary cleanup target is not a directory'
    Require (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'temporary cleanup root is a reparse point'
    $files = New-Object System.Collections.Generic.List[string]
    $directories = New-Object System.Collections.Generic.List[string]
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $directories.Add($full)
    $pending.Push($full)
    while ($pending.Count -gt 0) {
      $directory = $pending.Pop()
      foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force)) {
        Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'temporary cleanup tree contains a reparse point'
        if ($item.PSIsContainer) {
          $directories.Add($item.FullName)
          $pending.Push($item.FullName)
        } else {
          $files.Add($item.FullName)
        }
      }
    }
    foreach ($file in @($files | Sort-Object Length -Descending)) {
      $item = Get-Item -LiteralPath $file -Force
      Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'temporary file became a reparse point'
      Remove-Item -LiteralPath $file -Force
    }
    foreach ($directory in @($directories | Sort-Object Length -Descending)) {
      $item = Get-Item -LiteralPath $directory -Force
      Require (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'temporary directory became a reparse point'
      Remove-Item -LiteralPath $directory -Force
    }
  }
}

function Assert-OutputPaths {
  $production = Resolve-WorkspacePath $ProductionBuildLog
  $testBuild = Resolve-WorkspacePath $TestBuildLog
  Require ($production.Equals([IO.Path]::GetFullPath($ProductionEvidencePath), [StringComparison]::OrdinalIgnoreCase)) "production build log must use the fixed N22 evidence path"
  Require ($testBuild.Equals([IO.Path]::GetFullPath($TestBuildEvidencePath), [StringComparison]::OrdinalIgnoreCase)) "test-build log must use the fixed N22 evidence path"
  Require (-not $production.Equals($testBuild, [StringComparison]::OrdinalIgnoreCase)) "production and test-build logs must be distinct"
  $evidenceFull = [IO.Path]::GetFullPath($EvidenceDir).TrimEnd('\')
  $evidencePrefix = $evidenceFull + '\'
  foreach ($path in @(
      $PrebuildManifestPath, $EvidenceManifestPath, $ReportPath,
      $ProductionEvidencePath, $TestBuildEvidencePath, $FullSuiteEvidencePath,
      $FocusedEvidencePath, $SnapshotEvidencePath, $RegistryMcpEvidencePath,
      $RealMcpEvidencePath, $DoctorEvidencePath, $SchemaEvidencePath,
      $PlatformEvidencePath, $SafetyEvidencePath)) {
    $full = [IO.Path]::GetFullPath($path)
    Require ($full.StartsWith($evidencePrefix, [StringComparison]::OrdinalIgnoreCase)) "verifier-owned output escapes the fixed N22 evidence directory"
    Assert-PlainPathChain $full
  }
  if (Test-Path -LiteralPath $EvidenceDir -PathType Container) {
    $attributes = (Get-Item -LiteralPath $EvidenceDir -Force).Attributes
    Require (($attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "N22 evidence directory must not be a reparse point"
    [void](Get-PlainTreeFiles $EvidenceDir 'N22 evidence directory')
  }
}

function Write-PendingReport([string]$PreparedUtc, [string]$PrebuildHash, [string]$PublicationNonce) {
  $lines = @(
    '# N22 Runtime Verification Report',
    '',
    '**State: PENDING**',
    '',
    'This is factual runtime-evidence scaffolding only. It is not a plan audit or an outcome hard-audit verdict.',
    '',
    'No final N22 runtime result is recorded. The node remains approved with its outcome audit pending.',
    '',
    "- Frozen input preparation UTC: $PreparedUtc",
    "- Frozen prebuild manifest SHA-256: $PrebuildHash",
    "- Publication nonce: $PublicationNonce",
    '',
    '## Required Commands',
    '',
    '```powershell',
    'powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/n22-verify.ps1 -Prepare',
    'powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/n22-verify.ps1 -RunBuilds',
    '```',
    '',
    'Formal verification requires `-RunBuilds` so the same verifier process observes the native builds and first complete-suite run. A final report is published only after both complete-suite runs, N22 marker parsing, disposable schema/MCP probes, and frozen-input safeguards all succeed.'
  )
  Write-Utf8Lines $ReportPath $lines
}

function Write-PendingEvidence([bool]$IncludeBuildLogs, [string]$PreparedUtc, [string]$PrebuildHash, [string]$PublicationNonce) {
  $paths = @(
    $FullSuiteEvidencePath, $FocusedEvidencePath, $SnapshotEvidencePath,
    $RegistryMcpEvidencePath, $RealMcpEvidencePath, $DoctorEvidencePath,
    $SchemaEvidencePath, $PlatformEvidencePath, $SafetyEvidencePath
  )
  if ($IncludeBuildLogs) {
    $paths = @($ProductionEvidencePath, $TestBuildEvidencePath) + $paths
  }
  foreach ($path in $paths) {
    Write-Utf8Lines $path @(
      'state=pending',
      'reason=current official N22 evidence has not completed',
      "prepared_utc=$PreparedUtc",
      "prebuild_manifest_sha256=$PrebuildHash",
      "publication_nonce=$PublicationNonce"
    )
  }
}

function Write-PendingManifest([string]$Reason, [string]$PrebuildHash, [string]$PublicationNonce) {
  $pending = [pscustomobject][ordered]@{
    format_version = 1
    node = 'N22'
    state = 'pending'
    reason = $Reason
    prebuild_manifest_sha256 = $PrebuildHash
    publication_nonce = $PublicationNonce
    audit_verdict_issued = $false
    node_status_changed = $false
  }
  Write-Utf8Text $EvidenceManifestPath (($pending | ConvertTo-Json -Depth 4) + [Environment]::NewLine)
}

function Assert-PendingPublicationAnchor([string]$ExpectedPrebuildHash, [string]$ExpectedPublicationNonce) {
  Require (Test-Path -LiteralPath $EvidenceManifestPath -PathType Leaf) 'pending publication anchor is missing'
  Assert-PlainPathChain $EvidenceManifestPath $true
  $anchor = ConvertFrom-StrictJsonText ([IO.File]::ReadAllText($EvidenceManifestPath, $Utf8Strict)) 'pending evidence manifest'
  Require-ExactJsonPropertyNames $anchor @(
    'format_version', 'node', 'state', 'reason', 'prebuild_manifest_sha256',
    'publication_nonce', 'audit_verdict_issued', 'node_status_changed'
  ) 'pending evidence manifest'
  Require-JsonIntegerValue $anchor.format_version 1 'pending evidence format_version'
  Require-JsonStringValue $anchor.node 'N22' 'pending evidence node'
  Require-JsonStringValue $anchor.state 'pending' 'pending evidence state'
  Require ($anchor.reason -is [string] -and $anchor.reason.Length -in 1..512) 'pending evidence reason is invalid'
  Require-JsonBooleanValue $anchor.audit_verdict_issued $false 'pending evidence audit_verdict_issued'
  Require-JsonBooleanValue $anchor.node_status_changed $false 'pending evidence node_status_changed'
  Require ($anchor.prebuild_manifest_sha256 -is [string] -and
      $anchor.prebuild_manifest_sha256 -match '^[0-9a-f]{64}$' -and
      $anchor.prebuild_manifest_sha256 -ceq $ExpectedPrebuildHash) `
      'pending evidence does not bind the prepared manifest'
  Require ($anchor.publication_nonce -is [string] -and
      $anchor.publication_nonce -match '^[0-9a-f]{32}$' -and
      $anchor.publication_nonce -ceq $ExpectedPublicationNonce) `
      'pending evidence does not bind the preparation nonce'
  $anchor
}

function Initialize-PendingEvidence([bool]$IncludeBuildLogs, [string]$PreparedUtc, [string]$PrebuildHash, [string]$PublicationNonce) {
  New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
  $script:EvidenceInitialized = $true
  Write-PendingEvidence $IncludeBuildLogs $PreparedUtc $PrebuildHash $PublicationNonce
  Write-PendingReport $PreparedUtc $PrebuildHash $PublicationNonce
  Write-PendingManifest 'verification is pending or did not complete' $PrebuildHash $PublicationNonce
}

function Write-PendingFailure {
  if (-not $script:EvidenceInitialized) { return }
  try {
    $prebuildHash = if ($script:ValidatedPrebuildHash -match '^[0-9a-f]{64}$') {
      [string]$script:ValidatedPrebuildHash
    } else { 'unavailable' }
    $prepared = if (-not [string]::IsNullOrWhiteSpace([string]$script:ValidatedPreparedUtc)) {
      [string]$script:ValidatedPreparedUtc
    } else { 'unavailable' }
    $nonce = if ($script:ValidatedPublicationNonce -match '^[0-9a-f]{32}$') {
      [string]$script:ValidatedPublicationNonce
    } else { 'unavailable' }
    Write-PendingEvidence $false $prepared $prebuildHash $nonce
    Write-PendingReport $prepared $prebuildHash $nonce
    Write-PendingManifest 'verification did not complete; see verifier process error output' $prebuildHash $nonce
  } catch {}
}

function New-Preparation {
  # All preconditions are read-only. Evidence creation starts only after this
  # complete block has succeeded.
  $schemaBaseline = New-N19StorageBaselineEvidence
  $productionSlice = New-N22ProductionSliceEvidence
  Assert-OutputPaths
  Assert-ScopedPathPolicy
  Assert-NoForbiddenCommandsInVerifierScope
  $governance = Assert-N22Governance
  $dependencies = @(Assert-DependencyContracts)
  $wiring = Assert-BuildAndTestWiring
  $registeredTests = @(Get-RegisteredTests)
  $platform = Get-PlatformEvidence
  $inputs = @(New-InputManifestEntries)
  $scopedInputs = @(New-ScopedInputEntries)
  Assert-NoExcludedManifestPath ($inputs + $scopedInputs)
  $protected = @(Get-ProtectedRepoFiles | ForEach-Object { Get-FileEntry $_ 'protected-repo-config' })
  $git = Get-GitState
  $diff = Get-ScopedDiffFacts
  $preparedUtc = [DateTimeOffset]::UtcNow.ToString('o')
  $publicationNonce = [guid]::NewGuid().ToString('N')

  $manifest = [pscustomobject][ordered]@{
    format_version = 1
    node = 'N22'
    state = 'prepared-not-verified'
    prepared_utc = $preparedUtc
    publication_nonce = $publicationNonce
    governance = $governance
    dependency_contracts = $dependencies
    platform = $platform
    expected_registered_tests = $registeredTests.Count
    registered_test_names = $registeredTests
    production_sources = $wiring.production_sources
    test_sources = $wiring.test_sources
    git = $git
    scoped_diff = $diff
    protected_repo_config = $protected
    inputs = $inputs
    scoped_inputs = $scopedInputs
    schema_baseline = $schemaBaseline
    production_slice = $productionSlice
    scope = [pscustomobject][ordered]@{
      dedicated_n22_test_count = 1
      later_node_behavior_dependency_count = 0
      excluded_coordinator_artifact_count = 0
      protected_model_configuration_path_count = 0
    }
  }

  New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
  $script:EvidenceInitialized = $true
  Write-Utf8Text $PrebuildManifestPath (($manifest | ConvertTo-Json -Depth 10) + [Environment]::NewLine)
  $prebuildHash = File-Hash $PrebuildManifestPath
  Initialize-PendingEvidence $true $preparedUtc $prebuildHash $publicationNonce
  Write-Host "N22_PREPARED expected_registered_tests=$($registeredTests.Count) manifest=$prebuildHash"
}

function Read-PrebuildManifest {
  Assert-OutputPaths
  Require (Test-Path -LiteralPath $EvidenceDir -PathType Container) 'N22 evidence directory is missing; run -Prepare first'
  $script:EvidenceInitialized = $true
  Require (Test-Path -LiteralPath $PrebuildManifestPath -PathType Leaf) "missing PREBUILD-MANIFEST.json; run -Prepare first"
  Require (Test-Path -LiteralPath $EvidenceManifestPath -PathType Leaf) 'missing pending EVIDENCE-MANIFEST.json; run -Prepare first'
  $prebuildHash = File-Hash $PrebuildManifestPath

  $manifest = ConvertFrom-StrictJsonText ([IO.File]::ReadAllText($PrebuildManifestPath, $Utf8Strict)) 'PREBUILD-MANIFEST.json'
  Require-ExactJsonPropertyNames $manifest @(
    'format_version', 'node', 'state', 'prepared_utc', 'publication_nonce', 'governance',
    'dependency_contracts', 'platform', 'expected_registered_tests',
    'registered_test_names', 'production_sources', 'test_sources', 'git',
    'scoped_diff', 'protected_repo_config', 'inputs', 'scoped_inputs',
    'schema_baseline', 'production_slice', 'scope'
  ) 'prebuild manifest'
  Require-JsonIntegerValue $manifest.format_version 1 'prebuild format_version'
  Require ($manifest.node -is [string] -and $manifest.node -ceq 'N22') "prebuild manifest node is invalid"
  Require ($manifest.state -is [string] -and $manifest.state -ceq 'prepared-not-verified') "prebuild manifest is not prepared"
  Require ($manifest.publication_nonce -is [string] -and $manifest.publication_nonce -match '^[0-9a-f]{32}$') `
      'prebuild publication nonce is invalid'
  [void](Assert-PendingPublicationAnchor $prebuildHash ([string]$manifest.publication_nonce))
  $script:ValidatedPrebuildHash = $prebuildHash
  $script:ValidatedPreparedUtc = [string]$manifest.prepared_utc
  $script:ValidatedPublicationNonce = [string]$manifest.publication_nonce
  $manifest
}

function Assert-PreparationCurrent([object]$Manifest) {
  Assert-OutputPaths
  Require ($script:ValidatedPrebuildHash -match '^[0-9a-f]{64}$') 'prepared manifest hash was not validated'
  Require ((File-Hash $PrebuildManifestPath) -ceq [string]$script:ValidatedPrebuildHash) `
      'prepared manifest changed after validation'
  Require ($Manifest.publication_nonce -is [string] -and $Manifest.publication_nonce -match '^[0-9a-f]{32}$') `
      'prepared manifest publication nonce is invalid'
  Require ([string]$Manifest.publication_nonce -ceq [string]$script:ValidatedPublicationNonce) `
      'prepared manifest publication nonce changed after validation'
  [void](Assert-PendingPublicationAnchor ([string]$script:ValidatedPrebuildHash) ([string]$script:ValidatedPublicationNonce))
  $schemaBaseline = New-N19StorageBaselineEvidence
  $productionSlice = New-N22ProductionSliceEvidence
  Assert-HashBindingCurrent $Manifest.schema_baseline $schemaBaseline 'N19 storage baseline'
  Assert-HashBindingCurrent $Manifest.production_slice $productionSlice 'N22 production slice'
  $governance = Assert-N22Governance
  Require ([string]$Manifest.governance.approved_plan_sha256 -ceq $governance.approved_plan_sha256) "approved plan changed after preparation"
  Require ([string]$Manifest.governance.plan_audit_sha256 -ceq $governance.plan_audit_sha256) "plan audit changed after preparation"
  $dependencies = @(Assert-DependencyContracts)
  Require ($dependencies.Count -eq @($Manifest.dependency_contracts).Count) "dependency evidence count changed"
  for ($index = 0; $index -lt $dependencies.Count; ++$index) {
    Require ($dependencies[$index].node -ceq [string]$Manifest.dependency_contracts[$index].node) "dependency order changed"
    Require ($dependencies[$index].plan_audit_sha256 -ceq [string]$Manifest.dependency_contracts[$index].plan_audit_sha256) "dependency plan hash changed"
    Require ($dependencies[$index].outcome_audit_sha256 -ceq [string]$Manifest.dependency_contracts[$index].outcome_audit_sha256) "dependency outcome hash changed"
  }

  Assert-ScopedPathPolicy
  Assert-NoForbiddenCommandsInVerifierScope
  $wiring = Assert-BuildAndTestWiring
  $registered = @(Get-RegisteredTests)
  Require ($registered.Count -eq [int]$Manifest.expected_registered_tests) "registered test count changed after preparation"
  Require (($registered -join "`n") -ceq (@($Manifest.registered_test_names) -join "`n")) "registered test order changed after preparation"
  Require (($wiring.production_sources -join "`n") -ceq (@($Manifest.production_sources) -join "`n")) "production source wiring changed after preparation"
  Require (($wiring.test_sources -join "`n") -ceq (@($Manifest.test_sources) -join "`n")) "test source wiring changed after preparation"

  $inputs = @(New-InputManifestEntries)
  $scoped = @(New-ScopedInputEntries)
  Assert-NoExcludedManifestPath ($inputs + $scoped)
  Assert-EntrySetCurrent @($Manifest.inputs) $inputs 'frozen build/governance input closure'
  Assert-EntrySetCurrent @($Manifest.scoped_inputs) $scoped 'frozen N22 scoped input closure'
  $protected = @(Get-ProtectedRepoFiles | ForEach-Object { Get-FileEntry $_ 'protected-repo-config' })
  Assert-EntrySetCurrent @($Manifest.protected_repo_config) $protected 'protected repository configuration'
  $git = Get-GitState
  Require ($git.head -ceq [string]$Manifest.git.head) "Git HEAD changed after preparation"
  Require ($git.reference_log_fingerprint_sha256 -ceq [string]$Manifest.git.reference_log_fingerprint_sha256) "Git reference logs changed after preparation"
  $diff = Get-ScopedDiffFacts
  Require ($diff.diff_sha256 -ceq [string]$Manifest.scoped_diff.diff_sha256) "N22 scoped diff changed after preparation"
  Require ([int]$diff.protected_assignment_change_count -eq 0) "protected setting assignment changed"

  $platform = Get-PlatformEvidence
  Require ($platform.compiler -ceq [string]$Manifest.platform.compiler) "MSVC compiler identity changed after preparation"
  try {
    $preparedUtc = [DateTimeOffset]::Parse([string]$Manifest.prepared_utc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
  } catch {
    throw "N22 evidence requirement failed: prebuild prepared_utc is invalid"
  }
  Require ($preparedUtc.Offset -eq [TimeSpan]::Zero -and $preparedUtc -le [DateTimeOffset]::UtcNow.AddMinutes(5)) "prebuild prepared_utc is not a valid UTC evidence time"

  [pscustomobject]@{
    governance = $governance
    dependencies = $dependencies
    registered_tests = $registered
    production_sources = @($wiring.production_sources)
    test_sources = @($wiring.test_sources)
    inputs = $inputs
    scoped_inputs = $scoped
    schema_baseline = $schemaBaseline
    production_slice = $productionSlice
    protected = $protected
    git = $git
    scoped_diff = $diff
    platform = $platform
    prepared_utc = $preparedUtc
    publication_nonce = [string]$Manifest.publication_nonce
    manifest = $Manifest
  }
}

function Assert-FinalPublicationChain(
  [string]$ExpectedPrebuildHash,
  [string]$ExpectedPublicationNonce,
  [string[]]$ExpectedOutputPaths
) {
  Assert-OutputPaths
  Require ($ExpectedPrebuildHash -match '^[0-9a-f]{64}$') 'final publication has an invalid prepared-manifest hash'
  Require ($ExpectedPublicationNonce -match '^[0-9a-f]{32}$') 'final publication has an invalid nonce'
  Require ((File-Hash $PrebuildManifestPath) -ceq $ExpectedPrebuildHash) `
      'prepared manifest changed before final publication verification'
  Assert-PlainPathChain $EvidenceManifestPath $true
  $published = ConvertFrom-StrictJsonText ([IO.File]::ReadAllText($EvidenceManifestPath, $Utf8Strict)) 'final evidence manifest'
  Require-JsonIntegerValue $published.format_version 1 'final evidence format_version'
  Require-JsonStringValue $published.node 'N22' 'final evidence node'
  Require-JsonStringValue $published.state 'verified-pending-claude-outcome-audit' 'final evidence state'
  Require-JsonStringValue $published.publication_nonce $ExpectedPublicationNonce 'final evidence publication nonce'
  Require-JsonStringValue $published.prebuild_manifest_sha256 $ExpectedPrebuildHash 'final evidence prebuild hash'
  Require ($published.output_files -is [Array]) 'final evidence output file list is not an array'
  $expected = @($ExpectedOutputPaths | Sort-Object -Unique)
  $recorded = @($published.output_files | ForEach-Object { [string]$_.path } | Sort-Object -Unique)
  Require ($recorded.Count -eq @($published.output_files).Count) 'final evidence output list has duplicate paths'
  Require (($recorded -join "`n") -ceq ($expected -join "`n")) 'final evidence output list does not match the published chain'
  foreach ($entry in @($published.output_files)) {
    Require-ExactJsonPropertyNames $entry @('path', 'sha256', 'bytes') 'final evidence output entry'
    Require ($entry.sha256 -is [string] -and $entry.sha256 -match '^[0-9a-f]{64}$') 'final evidence output hash is invalid'
    Require-JsonInteger $entry.bytes 'final evidence output byte count'
    $path = Resolve-WorkspacePath ([string]$entry.path)
    Require ((File-Hash $path) -ceq [string]$entry.sha256) "published output hash changed: $($entry.path)"
    Require ([int64](Get-Item -LiteralPath $path).Length -eq [int64]$entry.bytes) "published output length changed: $($entry.path)"
  }
}

function Format-CapturedLog([string]$Command, [object]$Capture, [string[]]$Metadata) {
  $lines = New-Object System.Collections.Generic.List[string]
  $lines.Add("command=$Command")
  $lines.Add("working_directory=$($Root.Replace('\', '/'))")
  $lines.Add("started_utc=$($Capture.started_utc.ToString('o'))")
  foreach ($line in $Metadata) { $lines.Add($line) }
  $lines.Add('stdout_begin')
  if (-not [string]::IsNullOrEmpty($Capture.stdout)) {
    foreach ($line in @($Capture.stdout -split '\r?\n')) {
      if ($line -ne '') { $lines.Add($line) }
    }
  }
  $lines.Add('stdout_end')
  $lines.Add('stderr_begin')
  if (-not [string]::IsNullOrEmpty($Capture.stderr)) {
    foreach ($line in @($Capture.stderr -split '\r?\n')) {
      if ($line -ne '') { $lines.Add($line) }
    }
  }
  $lines.Add('stderr_end')
  $lines.Add("ended_utc=$($Capture.ended_utc.ToString('o'))")
  $lines.Add("exit_code=$($Capture.exit_code)")
  $lines.ToArray()
}

function Invoke-OfficialBuilds([int]$ExpectedRegisteredTests, [object]$Platform,
                               [object]$PreparationManifest, [string]$PublicationNonce) {
  Require ($null -ne $Platform -and -not [string]::IsNullOrWhiteSpace([string]$Platform.os)) 'official builds lack frozen Windows metadata'
  Require ($PublicationNonce -match '^[0-9a-f]{32}$') 'official builds lack a valid publication nonce'
  Require ([string]$PreparationManifest.publication_nonce -ceq $PublicationNonce) 'official builds do not match the prepared publication nonce'
  $productionPath = Resolve-WorkspacePath $ProductionBuildLog
  $testPath = Resolve-WorkspacePath $TestBuildLog
  $prefix = 'qbrain_n22_build_'
  $sandbox = Join-Path ([IO.Path]::GetTempPath()) ($prefix + [guid]::NewGuid().ToString('N'))
  try {
    Assert-PlainPathChain $sandbox
    New-Item -ItemType Directory -Force -Path $sandbox | Out-Null
    $environment = @{ LOCALAPPDATA=$sandbox }
    $isolatedQbrainTree = Join-Path $sandbox 'Qbrain'

    $buildIsolatedTreeBefore = Get-DisposableTreeFingerprint $isolatedQbrainTree
    $production = Invoke-CapturedProcess 'powershell.exe' '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts\build-cl.ps1' $TimeoutSeconds $Root $environment
    $buildIsolatedTreeAfter = Get-DisposableTreeFingerprint $isolatedQbrainTree
    Require ($production.exit_code -eq 0) "canonical production build failed"
    Require ($production.stdout -match '(?m)^BUILD_OK\s*$') "production build output lacks BUILD_OK"
    Require (Test-Path -LiteralPath $Qbrain -PathType Leaf) "production build did not create qbrain.exe"
    $productionConfig = Assert-IsolatedTestConfig $sandbox 'production build'
    $productionLines = Format-CapturedLog $ProductionBuildCommand $production @(
      'stage=production-build',
      'target_arch=x64',
      'language_mode=/std:c++20',
      "windows_build=$($Platform.os)",
      "child_environment_policy=$ChildEnvironmentPolicy",
      "publication_nonce=$PublicationNonce",
      'isolated_localappdata=true',
      'production_data_access_telemetry=not-collected',
      "isolated_localappdata_tree_before_sha256=$buildIsolatedTreeBefore",
      "isolated_localappdata_tree_after_sha256=$buildIsolatedTreeAfter",
      "isolated_localappdata_tree_changed=$(([string](-not ($buildIsolatedTreeAfter -ceq $buildIsolatedTreeBefore))).ToLowerInvariant())",
      "isolated_test_config_count=$($productionConfig.count)",
      "isolated_test_config_sha256=$($productionConfig.sha256)",
      "artifact_sha256=$(File-Hash $Qbrain)"
    )
    $productionText = $productionLines -join [Environment]::NewLine
    Assert-SafeEvidenceText $productionText 'production build output'
    Write-Utf8Lines $productionPath $productionLines
    [void](Assert-PreparationCurrent $PreparationManifest)

    $testTreeBefore = Get-DisposableTreeFingerprint $isolatedQbrainTree
    $testBuild = Invoke-CapturedProcess 'powershell.exe' '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts\build-tests-cl.ps1 -SkipProductionBuild' $TimeoutSeconds $Root $environment
    $testTreeAfter = Get-DisposableTreeFingerprint $isolatedQbrainTree
    Require ($testBuild.exit_code -eq 0) "canonical test build/full-suite run failed"
    Require ($testBuild.stdout -match '(?m)^TESTS_BUILD_OK\s*$') "test build output lacks TESTS_BUILD_OK"
    Require (Test-Path -LiteralPath $Tests -PathType Leaf) "test build did not create qbrain_tests.exe"
    $testConfig = Assert-IsolatedTestConfig $sandbox 'test build/full-suite run 1'
    $testLines = Format-CapturedLog $TestBuildCommand $testBuild @(
      'stage=test-build-and-suite-run-1',
      'target_arch=x64',
      'language_mode=/std:c++20',
      "windows_build=$($Platform.os)",
      "child_environment_policy=$ChildEnvironmentPolicy",
      "publication_nonce=$PublicationNonce",
      'suite_run_index=1',
      "expected_registered_tests=$ExpectedRegisteredTests",
      'isolated_localappdata=true',
      'production_data_access_telemetry=not-collected',
      "isolated_localappdata_tree_before_sha256=$testTreeBefore",
      "isolated_localappdata_tree_after_sha256=$testTreeAfter",
      "isolated_localappdata_tree_changed=$(([string](-not ($testTreeAfter -ceq $testTreeBefore))).ToLowerInvariant())",
      'isolated_test_config_policy=absent_or_canonical_defaults_only',
      "isolated_test_config_count=$($testConfig.count)",
      "isolated_test_config_sha256=$($testConfig.sha256)",
      "production_binary_sha256=$(File-Hash $Qbrain)",
      "test_binary_sha256=$(File-Hash $Tests)"
    )
    $testText = $testLines -join [Environment]::NewLine
    Assert-SafeEvidenceText $testText 'test build/full-suite run 1 output'
    Write-Utf8Lines $testPath $testLines
    [void](Assert-PreparationCurrent $PreparationManifest)
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
  Assert-PlainPathChain $Path $true
  $text = [IO.File]::ReadAllText($Path, $Utf8Strict)
  Require (-not [string]::IsNullOrWhiteSpace($text)) "$Label log is empty"
  Assert-SafeEvidenceText $text "$Label log"
  Require ($text -notmatch '(?i)(?<![A-Za-z])(?:WSL|Docker)(?![A-Za-z])') "$Label is not a native-Windows-only record"
  $lines = @($text -split '\r?\n' | Where-Object { $_ -ne '' })
  try {
    $started = [DateTimeOffset]::Parse((Get-EnvelopeValue $lines 'started_utc' $Label), [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
    $ended = [DateTimeOffset]::Parse((Get-EnvelopeValue $lines 'ended_utc' $Label), [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
    $exitCode = [int](Get-EnvelopeValue $lines 'exit_code' $Label)
  } catch {
    throw "N22 evidence requirement failed: $Label envelope metadata is invalid"
  }
  Require ($started.Offset -eq [TimeSpan]::Zero -and $ended.Offset -eq [TimeSpan]::Zero) "$Label timestamps are not UTC"
  Require ($ended -ge $started) "$Label ended before it started"
  [pscustomobject]@{
    path = $Path
    text = $text
    lines = $lines
    command = Get-EnvelopeValue $lines 'command' $Label
    working_directory = Get-EnvelopeValue $lines 'working_directory' $Label
    started_utc = $started
    ended_utc = $ended
    exit_code = $exitCode
  }
}

function Assert-CompiledSources([string[]]$Lines, [string[]]$Sources, [string]$Label) {
  foreach ($source in $Sources) {
    $leaf = [IO.Path]::GetFileName($source)
    Require (@($Lines | Where-Object { $_ -ceq $leaf }).Count -eq 1) "$Label did not compile $leaf exactly once"
  }
}

function Require-ExactLabelCount([hashtable]$Counts, [string]$Label, [int]$Expected) {
  $actual = if ($Counts.ContainsKey($Label)) { [int]$Counts[$Label] } else { 0 }
  Require ($actual -eq $Expected) "N22 snapshot evidence count for $Label is not exact"
}

function Get-ExpectedN22SnapshotLabels {
  $labels = New-Object System.Collections.Generic.List[string]
  foreach ($label in @(
      'schema:fresh-v12', 'schema:populated-reopen-v12',
      'scanner:code_callees:body', 'local:code_callees:body',
      'local:code_callees:same-line-close',
      'local:code_callees:declaration-prefix-brace',
      'local:code_callees:control-keywords',
      'local:code_callees:multiple-definitions',
      'local:code_callees:recursive',
      'local:code_callees:brace-window-boundary',
      'local:code_callees:empty:TooFarRoot',
      'local:code_callees:empty:UnbalancedRoot',
      'local:code_callees:empty:RefOnlyRoot',
      'local:code_callees:empty:PrefixRoot',
      'local:code_callees:empty:PrototypeRoot',
      'local:code_flow:prototype-no-body',
      'local:code_callees:utf8-disclosure',
      'local:code_flow:depth-1', 'local:code_flow:depth-2',
      'local:code_flow:depth-3', 'scanner:code_flow:bfs',
      'local:code_flow:depth-max', 'local:code_flow:depth-over-max',
      'local:code_flow:global-limit', 'local:code_flow:deterministic-repeat',
      'scanner:code_blast:priority', 'local:code_blast:priority-dedup',
      'local:code_blast:global-limit-one', 'local:code_blast:empty')) {
    $labels.Add($label)
  }

  $operations = @('code_callees', 'code_flow', 'code_blast')
  foreach ($operation in $operations) {
    foreach ($suffix in @(
        'canonical', 'symbol', 'name', 'all-equal', 'conflict', 'empty',
        'missing', 'unexpected')) {
      $labels.Add("alias:${operation}:${suffix}")
    }
  }
  foreach ($operation in $operations) {
    for ($index = 0; $index -lt 6; ++$index) {
      $labels.Add("symbol:${operation}:valid-no-match:${index}")
    }
  }
  foreach ($operation in $operations) {
    for ($index = 0; $index -lt 13; ++$index) {
      $labels.Add("symbol:${operation}:invalid:${index}")
    }
  }

  foreach ($label in @(
      'numeric:code_callees:default', 'numeric:code_callees:zero',
      'numeric:code_callees:one', 'numeric:code_callees:max',
      'numeric:code_callees:over-max', 'numeric:code_flow:default',
      'numeric:code_flow:limit-zero', 'numeric:code_flow:limit-one',
      'numeric:code_flow:limit-max',
      'numeric:code_flow:limit-over-max', 'numeric:code_flow:depth-zero',
      'numeric:code_flow:depth-one', 'numeric:code_blast:default',
      'numeric:code_blast:zero', 'numeric:code_blast:max',
      'numeric:code_blast:over-max')) {
    $labels.Add($label)
  }
  foreach ($operation in $operations) {
    foreach ($pageLimit in @('0', '1', '2000', '2001')) {
      $labels.Add("numeric:${operation}:page-limit-${pageLimit}")
    }
  }
  $numericFields = [ordered]@{
    code_callees = @('limit', 'page_limit')
    code_flow = @('depth', 'limit', 'page_limit')
    code_blast = @('limit', 'page_limit')
  }
  foreach ($operation in $operations) {
    foreach ($field in $numericFields[$operation]) {
      for ($index = 0; $index -lt 9; ++$index) {
        $labels.Add("numeric:${operation}:invalid:${field}:${index}")
      }
    }
  }

  foreach ($operation in $operations) {
    foreach ($suffix in @(
        'local-mixed-case', 'remote-default', 'remote-denied',
        'write-does-not-authorize')) {
      $labels.Add("source:${operation}:${suffix}")
    }
  }
  foreach ($operation in $operations) {
    for ($index = 0; $index -lt 5; ++$index) {
      $labels.Add("source:${operation}:invalid:${index}")
    }
  }
  $labels.Add('source:code_callees:active-before-page-limit')
  $labels.Add('source:code_flow:active-before-page-limit')
  $labels.Add('source:code_blast:active-before-page-limit')
  foreach ($operation in $operations) {
    $labels.Add("source:${operation}:allowlisted")
  }

  $labels.Add('registry:operations')
  $labels.Add('registry:tools-list')
  foreach ($operation in $operations) {
    foreach ($suffix in @('success', 'empty', 'clamp', 'non-object', 'unknown-field')) {
      $labels.Add("mcp:${operation}:${suffix}")
    }
    for ($index = 0; $index -lt 5; ++$index) {
      $labels.Add("mcp:${operation}:wrong-symbol:${index}")
      $labels.Add("mcp:${operation}:wrong-source:${index}")
    }
    foreach ($field in $numericFields[$operation]) {
      for ($index = 0; $index -lt 7; ++$index) {
        $labels.Add("mcp:${operation}:wrong-number:${field}:${index}")
      }
    }
    foreach ($suffix in @(
        'alias-conflict', 'unknown-source', 'denied-source',
        'write-does-not-authorize')) {
      $labels.Add("mcp:${operation}:${suffix}")
    }
  }
  foreach ($operation in $operations) {
    $labels.Add("mcp:${operation}:ambient-default")
  }

  foreach ($operation in $operations) {
    $labels.Add("damaged:direct:${operation}")
    $labels.Add("damaged:mcp:${operation}")
  }
  foreach ($label in @(
      'cache:local-success', 'cache:unexpected-argument:local',
      'mcp:code_traversal_cache_clear:remote-denied',
      'mcp:code_traversal_cache_clear:remote-allowed',
      'mcp:code_traversal_cache_clear:unexpected-argument',
      'mcp:code_traversal_cache_clear:non-object',
      'page-limit:default-500', 'page-limit:explicit-501',
      'page-limit:cap:2000', 'page-limit:cap:2001',
      'page-limit:cap:999999', 'resource:page-body-budget',
      'resource:source-line-budget', 'resource:slug-length-bound',
      'resource:slug-control-bound', 'schema:final-v12')) {
    $labels.Add($label)
  }
  $labels.ToArray()
}

function Get-ExpectedN22Summary(
  [int]$SnapshotCount,
  [string]$SelectedSnapshotSha256,
  [string]$DecoySnapshotSha256
) {
  $facts = @(
    'schema_v12=pass', 'callee_body_matrix=pass', 'flow_bfs_matrix=pass',
    'blast_matrix=pass', 'symbol_alias_matrix=pass', 'numeric_matrix=pass',
    'source_scope=pass', 'active_page_ordering=pass',
    'remote_authorization=pass', 'ambient_default=pass',
    'selected_decoy=pass', 'registry_schema=pass', 'tools_list=pass',
    'mcp_type_validation=pass', 'cache_clear=pass', 'stateless=pass',
    'disclosure_bounds=pass', 'resource_bounds=pass',
    'deterministic=pass', 'read_only=pass'
  )
  '[INFO] n22 ' + ($facts -join ' ') + " snapshot_call_count=$SnapshotCount" +
      " selected_snapshot_sha256=$SelectedSnapshotSha256" +
      " decoy_snapshot_sha256=$DecoySnapshotSha256"
}

function Assert-N22Evidence([string[]]$Lines, [string]$Label) {
  $summaryLines = @($Lines | Where-Object {
      $_.StartsWith('[INFO] n22 ', [StringComparison]::Ordinal) -and
      -not $_.StartsWith('[INFO] n22 snapshot_call=', [StringComparison]::Ordinal)
    })
  Require ($summaryLines.Count -eq 1) "$Label must contain exactly one N22 summary marker"
  $summary = $summaryLines[0]
  Require ($summary -notmatch '(?i)\b(?:VERDICT|outcome[- ]audit)\b') "$Label N22 marker contains audit language"
  $expectedLabels = @(Get-ExpectedN22SnapshotLabels)
  Require ($expectedLabels.Count -eq $ExpectedN22SnapshotLabelCount) "internal N22 exact label contract count changed"
  Require ((Text-Hash ($expectedLabels -join "`n")) -ceq $ExpectedN22SnapshotLabelHash) "internal N22 exact label contract hash changed"
  $snapshotCount = $expectedLabels.Count
  $selectedMatches = [regex]::Matches($summary, '(?:^|\s)selected_snapshot_sha256=([0-9a-f]{64})(?=\s|$)')
  $decoyMatches = [regex]::Matches($summary, '(?:^|\s)decoy_snapshot_sha256=([0-9a-f]{64})(?=\s|$)')
  Require ($selectedMatches.Count -eq 1 -and $decoyMatches.Count -eq 1) "$Label N22 marker lacks exact selected/decoy hashes"
  $selectedFinal = $selectedMatches[0].Groups[1].Value
  $decoyFinal = $decoyMatches[0].Groups[1].Value
  Require ($selectedFinal -cne $decoyFinal) "$Label selected and decoy final snapshots are not distinct"
  Require ($summary -ceq (Get-ExpectedN22Summary $snapshotCount $selectedFinal $decoyFinal)) "$Label N22 summary marker is not exact"

  $snapshotLines = @($Lines | Where-Object { $_.StartsWith('[INFO] n22 snapshot_call=', [StringComparison]::Ordinal) })
  Require ($snapshotLines.Count -eq $snapshotCount) "$Label snapshot row count differs from snapshot_call_count"
  $labelCounts = @{}
  $rows = New-Object System.Collections.Generic.List[object]
  $distinctBrains = $false
  for ($index = 0; $index -lt $snapshotLines.Count; ++$index) {
    $match = [regex]::Match(
      $snapshotLines[$index],
      '^\[INFO\] n22 snapshot_call=([1-9][0-9]*) label=([A-Za-z0-9_.:+-]+) selected_before_sha256=([0-9a-f]{64}) selected_after_sha256=([0-9a-f]{64}) decoy_before_sha256=([0-9a-f]{64}) decoy_after_sha256=([0-9a-f]{64})$')
    Require ($match.Success) "$Label contains a malformed N22 snapshot row"
    Require ([int]$match.Groups[1].Value -eq ($index + 1)) "$Label snapshot indexes are not contiguous and ordered"
    $snapshotLabel = $match.Groups[2].Value
    Require ($snapshotLabel -ceq $expectedLabels[$index]) "$Label snapshot label/order differs at row $($index + 1)"
    $selectedBefore = $match.Groups[3].Value
    $selectedAfter = $match.Groups[4].Value
    $decoyBefore = $match.Groups[5].Value
    $decoyAfter = $match.Groups[6].Value
    Require ($selectedBefore -ceq $selectedAfter) "$Label selected snapshot changed during $snapshotLabel"
    Require ($decoyBefore -ceq $decoyAfter) "$Label decoy snapshot changed during $snapshotLabel"
    if ($selectedBefore -cne $decoyBefore) { $distinctBrains = $true }
    if (-not $labelCounts.ContainsKey($snapshotLabel)) { $labelCounts[$snapshotLabel] = 0 }
    $labelCounts[$snapshotLabel] = [int]$labelCounts[$snapshotLabel] + 1
    $rows.Add([pscustomobject][ordered]@{
        index = $index + 1
        label = $snapshotLabel
        selected_before_sha256 = $selectedBefore
        selected_after_sha256 = $selectedAfter
        decoy_before_sha256 = $decoyBefore
        decoy_after_sha256 = $decoyAfter
        line = $snapshotLines[$index]
      })
  }
  Require ($rows.Count -gt 0) "$Label contains no N22 snapshot rows"
  Require $distinctBrains "$Label never proves distinct selected and decoy brains"
  Require ($labelCounts.Count -eq (@($expectedLabels | Sort-Object -Unique)).Count) "$Label snapshot label set is not exact"
  Require ($rows[$rows.Count - 1].selected_after_sha256 -ceq $selectedFinal) "$Label final selected hash is not bound to the last snapshot row"
  Require ($rows[$rows.Count - 1].decoy_after_sha256 -ceq $decoyFinal) "$Label final decoy hash is not bound to the last snapshot row"

  foreach ($operation in @('code_callees', 'code_flow', 'code_blast')) {
    Require-ExactLabelCount $labelCounts "mcp:${operation}:success" 1
    Require-ExactLabelCount $labelCounts "mcp:${operation}:empty" 1
    Require-ExactLabelCount $labelCounts "mcp:${operation}:clamp" 1
    Require-ExactLabelCount $labelCounts "damaged:direct:${operation}" 1
    Require-ExactLabelCount $labelCounts "damaged:mcp:${operation}" 1
  }
  foreach ($cacheLabel in @(
      'cache:local-success', 'cache:unexpected-argument:local',
      'mcp:code_traversal_cache_clear:remote-denied',
      'mcp:code_traversal_cache_clear:remote-allowed',
      'mcp:code_traversal_cache_clear:unexpected-argument',
      'mcp:code_traversal_cache_clear:non-object')) {
    Require-ExactLabelCount $labelCounts $cacheLabel 1
  }
  Require-ExactLabelCount $labelCounts 'registry:operations' 1
  Require-ExactLabelCount $labelCounts 'registry:tools-list' 1
  Require-ExactLabelCount $labelCounts 'schema:fresh-v12' 1
  Require-ExactLabelCount $labelCounts 'schema:populated-reopen-v12' 1
  Require-ExactLabelCount $labelCounts 'schema:final-v12' 1

  $normalizedLines = @($summary) + @($snapshotLines)
  $normalizedText = (($normalizedLines -join "`n") -replace '[0-9a-f]{64}', '<sha256>')
  [pscustomobject]@{
    summary = $summary
    snapshot_count = $snapshotCount
    selected_snapshot_sha256 = $selectedFinal
    decoy_snapshot_sha256 = $decoyFinal
    snapshot_lines = $snapshotLines
    rows = $rows.ToArray()
    label_counts = $labelCounts
    exact_label_order = $true
    damaged_database_matrix = $true
    single_structured_error_block_matrix = $true
    fresh_and_populated_schema_v12 = $true
    normalized_text = $normalizedText
    normalized_sha256 = Text-Hash $normalizedText
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
  Require ($resultRows.Count -eq $RegisteredTests.Count) "$Label result count does not equal the registered count"
  for ($index = 0; $index -lt $RegisteredTests.Count; ++$index) {
    Require ($resultRows[$index].name -ceq $RegisteredTests[$index]) "$Label result order/name differs from test_main.cpp"
  }
  $passRows = @($resultRows | Where-Object { $_.result -ceq 'PASS' })
  $failRows = @($resultRows | Where-Object { $_.result -ceq 'FAIL' })
  Require ($Envelope.exit_code -eq 0) "$Label exit code is not zero"
  Require ($failRows.Count -eq 0) "$Label contains failing tests"
  Require ($passRows.Count -eq $RegisteredTests.Count) "$Label is not all passing"
  Require (@($passRows | Where-Object { $_.name -ceq 'n22' }).Count -eq 1) "$Label does not contain exactly one N22 PASS"
  $n22 = Assert-N22Evidence $Envelope.lines $Label
  $resultText = @($resultRows | ForEach-Object { $_.line }) -join "`n"
  [pscustomobject]@{
    registered = $RegisteredTests.Count
    passed = $passRows.Count
    failed = $failRows.Count
    result_lines = @($resultRows | ForEach-Object { $_.line })
    result_text = $resultText
    result_sha256 = Text-Hash $resultText
    n22 = $n22
  }
}

function Assert-BuildLogs([object]$PreparationState) {
  $productionPath = Resolve-WorkspacePath $ProductionBuildLog
  $testPath = Resolve-WorkspacePath $TestBuildLog
  $production = Parse-Envelope $productionPath 'production build'
  $testBuild = Parse-Envelope $testPath 'test build/full-suite run 1'
  $expectedWorkingDirectory = $Root.Replace('\', '/')
  Require ($production.command -ceq $ProductionBuildCommand) "production command is not canonical"
  Require ($testBuild.command -ceq $TestBuildCommand) "test-build command is not canonical"
  Require ($production.working_directory -ceq $expectedWorkingDirectory) "production build working directory is not the repository root"
  Require ($testBuild.working_directory -ceq $expectedWorkingDirectory) "test build working directory is not the repository root"
  Require ((Get-EnvelopeValue $production.lines 'target_arch' 'production build') -ceq 'x64') "production target is not x64"
  Require ((Get-EnvelopeValue $testBuild.lines 'target_arch' 'test build') -ceq 'x64') "test target is not x64"
  Require ((Get-EnvelopeValue $production.lines 'language_mode' 'production build') -ceq '/std:c++20') "production language mode is not C++20"
  Require ((Get-EnvelopeValue $testBuild.lines 'language_mode' 'test build') -ceq '/std:c++20') "test language mode is not C++20"
  Require ((Get-EnvelopeValue $production.lines 'windows_build' 'production build') -ceq [string]$PreparationState.platform.os) "production Windows build differs from the frozen host"
  Require ((Get-EnvelopeValue $testBuild.lines 'windows_build' 'test build') -ceq [string]$PreparationState.platform.os) "test Windows build differs from the frozen host"
  Require ((Get-EnvelopeValue $production.lines 'child_environment_policy' 'production build') -ceq $ChildEnvironmentPolicy) "production build lacks the fail-closed child environment policy"
  Require ((Get-EnvelopeValue $testBuild.lines 'child_environment_policy' 'test build') -ceq $ChildEnvironmentPolicy) "test build lacks the fail-closed child environment policy"
  Require ((Get-EnvelopeValue $production.lines 'publication_nonce' 'production build') -ceq [string]$PreparationState.publication_nonce) "production build is not bound to the preparation nonce"
  Require ((Get-EnvelopeValue $testBuild.lines 'publication_nonce' 'test build') -ceq [string]$PreparationState.publication_nonce) "test build is not bound to the preparation nonce"
  Require ((Get-EnvelopeValue $production.lines 'isolated_localappdata' 'production build') -ceq 'true') "production build lacks disposable LOCALAPPDATA evidence"
  Require ((Get-EnvelopeValue $testBuild.lines 'isolated_localappdata' 'test build') -ceq 'true') "test build lacks disposable LOCALAPPDATA evidence"
  Require ((Get-EnvelopeValue $production.lines 'production_data_access_telemetry' 'production build') -ceq 'not-collected') "production build telemetry claim is invalid"
  Require ((Get-EnvelopeValue $testBuild.lines 'production_data_access_telemetry' 'test build') -ceq 'not-collected') "test build telemetry claim is invalid"
  Require ((Get-EnvelopeValue $testBuild.lines 'isolated_test_config_policy' 'test build') -ceq 'absent_or_canonical_defaults_only') "test-build config policy is not fail-closed"
  Require ((Get-EnvelopeValue $production.lines 'stage' 'production build') -ceq 'production-build') "production build stage marker is not exact"
  Require ((Get-EnvelopeValue $testBuild.lines 'stage' 'test build') -ceq 'test-build-and-suite-run-1') "test build stage marker is not exact"
  Require ([int](Get-EnvelopeValue $testBuild.lines 'suite_run_index' 'test build') -eq 1) "embedded suite is not identified as run 1"
  Require ([int](Get-EnvelopeValue $testBuild.lines 'expected_registered_tests' 'test build') -eq $PreparationState.registered_tests.Count) "test-build expected count differs from frozen test_main.cpp"

  foreach ($entry in @(
      [pscustomobject]@{ Envelope=$production; Label='production build' },
      [pscustomobject]@{ Envelope=$testBuild; Label='test build' })) {
    $treeBefore = Get-EnvelopeValue $entry.Envelope.lines 'isolated_localappdata_tree_before_sha256' $entry.Label
    $treeAfter = Get-EnvelopeValue $entry.Envelope.lines 'isolated_localappdata_tree_after_sha256' $entry.Label
    $treeChanged = Get-EnvelopeValue $entry.Envelope.lines 'isolated_localappdata_tree_changed' $entry.Label
    Require ($treeBefore -match '^(?:absent|[0-9a-f]{64})$' -and $treeAfter -match '^(?:absent|[0-9a-f]{64})$') "$($entry.Label) isolated-tree fingerprint is invalid"
    Require ($treeChanged -in @('true', 'false')) "$($entry.Label) isolated-tree change telemetry is invalid"
    Require ((-not ($treeBefore -ceq $treeAfter)) -eq [bool]::Parse($treeChanged)) "$($entry.Label) isolated-tree change telemetry disagrees with its fingerprints"
    $configCount = [int](Get-EnvelopeValue $entry.Envelope.lines 'isolated_test_config_count' $entry.Label)
    $configHash = Get-EnvelopeValue $entry.Envelope.lines 'isolated_test_config_sha256' $entry.Label
    Require ($configCount -in @(0, 1)) "$($entry.Label) isolated config count is invalid"
    Require (($configCount -eq 0 -and $configHash -ceq 'absent') -or
        ($configCount -eq 1 -and $configHash -match '^[0-9a-f]{64}$')) "$($entry.Label) isolated config fingerprint is invalid"
  }

  Require ($production.exit_code -eq 0 -and $testBuild.exit_code -eq 0) "native build log exit code is nonzero"
  Require ($production.started_utc -ge $PreparationState.prepared_utc) "production build predates the frozen manifest"
  Require ($testBuild.started_utc -ge $production.ended_utc) "test build did not start after production build ended"
  Require ($testBuild.ended_utc -le [DateTimeOffset]::UtcNow.AddMinutes(5)) "test build timestamp is in the future"
  Require (@($production.lines | Where-Object { $_ -ceq 'BUILD_OK' }).Count -eq 1) "production log lacks exactly one BUILD_OK"
  Require (@($testBuild.lines | Where-Object { $_ -ceq 'TESTS_BUILD_OK' }).Count -eq 1) "test-build log lacks exactly one TESTS_BUILD_OK"
  Require (@($production.lines | Where-Object { $_ -match "Environment initialized for: 'x64'" }).Count -eq 1) "production log lacks x64 vcvars evidence"
  Require (@($testBuild.lines | Where-Object { $_ -match "Environment initialized for: 'x64'" }).Count -eq 1) "test log lacks x64 vcvars evidence"
  Assert-CompiledSources $production.lines (@($PreparationState.production_sources) + @('sqlite3.c')) 'production build'
  Assert-CompiledSources $testBuild.lines @($PreparationState.test_sources) 'test build'

  Require (Test-Path -LiteralPath $Qbrain -PathType Leaf) "production binary is missing"
  Require (Test-Path -LiteralPath $Tests -PathType Leaf) "test binary is missing"
  $qbrainHash = File-Hash $Qbrain
  $testsHash = File-Hash $Tests
  Require ((Get-EnvelopeValue $production.lines 'artifact_sha256' 'production build') -ceq $qbrainHash) "production build is not bound to the current qbrain.exe"
  Require ((Get-EnvelopeValue $testBuild.lines 'production_binary_sha256' 'test build') -ceq $qbrainHash) "test build is not bound to the current qbrain.exe"
  Require ((Get-EnvelopeValue $testBuild.lines 'test_binary_sha256' 'test build') -ceq $testsHash) "test build is not bound to the current qbrain_tests.exe"
  $qbrainTime = [DateTimeOffset](Get-Item -LiteralPath $Qbrain).LastWriteTimeUtc
  $testsTime = [DateTimeOffset](Get-Item -LiteralPath $Tests).LastWriteTimeUtc
  Require ($qbrainTime -ge $production.started_utc.AddMinutes(-1) -and $qbrainTime -le $production.ended_utc.AddMinutes(1)) "qbrain.exe timestamp is outside the production-build interval"
  Require ($testsTime -ge $testBuild.started_utc.AddMinutes(-1) -and $testsTime -le $testBuild.ended_utc.AddMinutes(1)) "qbrain_tests.exe timestamp is outside the test-build interval"

  $results = Assert-TestResults $testBuild $PreparationState.registered_tests 'test build/full-suite run 1'
  [pscustomobject]@{
    production = $production
    test_build = $testBuild
    run1 = $results
    production_binary_sha256 = $qbrainHash
    test_binary_sha256 = $testsHash
  }
}

function Invoke-FullSuiteRun2(
  [string[]]$RegisteredTests,
  [string]$ExpectedBinaryHash,
  [string]$ExpectedWindowsBuild,
  [string]$PublicationNonce
) {
  Require ((File-Hash $Tests) -ceq $ExpectedBinaryHash) "test binary changed before full-suite run 2"
  Require ($PublicationNonce -match '^[0-9a-f]{32}$') 'full-suite run 2 lacks a valid publication nonce'
  $prefix = 'qbrain_n22_suite_'
  $sandbox = Join-Path ([IO.Path]::GetTempPath()) ($prefix + [guid]::NewGuid().ToString('N'))
  try {
    Assert-PlainPathChain $sandbox
    New-Item -ItemType Directory -Force -Path $sandbox | Out-Null
    $isolatedQbrainTree = Join-Path $sandbox 'Qbrain'
    $isolatedTreeBefore = Get-DisposableTreeFingerprint $isolatedQbrainTree
    $capture = Invoke-CapturedProcess $Tests '' $TimeoutSeconds $Root @{ LOCALAPPDATA=$sandbox }
    $isolatedTreeAfter = Get-DisposableTreeFingerprint $isolatedQbrainTree
    $config = Assert-IsolatedTestConfig $sandbox 'full-suite run 2'
    $lines = Format-CapturedLog $FullSuiteCommand $capture @(
      'stage=full-suite-run-2',
      'suite_run_index=2',
      "windows_build=$ExpectedWindowsBuild",
      "child_environment_policy=$ChildEnvironmentPolicy",
      "publication_nonce=$PublicationNonce",
      "expected_registered_tests=$($RegisteredTests.Count)",
      "binary_sha256=$ExpectedBinaryHash",
      'isolated_localappdata=true',
      'production_data_access_telemetry=not-collected',
      "isolated_localappdata_tree_before_sha256=$isolatedTreeBefore",
      "isolated_localappdata_tree_after_sha256=$isolatedTreeAfter",
      "isolated_localappdata_tree_changed=$(([string](-not ($isolatedTreeAfter -ceq $isolatedTreeBefore))).ToLowerInvariant())",
      'isolated_test_config_policy=absent_or_canonical_defaults_only',
      "isolated_test_config_count=$($config.count)",
      "isolated_test_config_sha256=$($config.sha256)"
    )
    $text = $lines -join [Environment]::NewLine
    Assert-SafeEvidenceText $text 'full-suite run 2 output'
    Write-Utf8Lines $FullSuiteEvidencePath $lines
  } finally {
    Remove-SafeTemporaryDirectory $sandbox $prefix
  }
  Require ((File-Hash $Tests) -ceq $ExpectedBinaryHash) "test binary changed during full-suite run 2"
  $envelope = Parse-Envelope $FullSuiteEvidencePath 'full-suite run 2'
  Require ($envelope.command -ceq $FullSuiteCommand) "full-suite run 2 command is not canonical"
  Require ($envelope.working_directory -ceq $Root.Replace('\', '/')) "full-suite run 2 working directory is not the repository root"
  Require ([int](Get-EnvelopeValue $envelope.lines 'suite_run_index' 'full-suite run 2') -eq 2) "standalone suite is not identified as run 2"
  Require ([int](Get-EnvelopeValue $envelope.lines 'expected_registered_tests' 'full-suite run 2') -eq $RegisteredTests.Count) "run 2 expected count changed"
  Require ((Get-EnvelopeValue $envelope.lines 'binary_sha256' 'full-suite run 2') -ceq $ExpectedBinaryHash) "run 2 binary hash metadata is wrong"
  Require ((Get-EnvelopeValue $envelope.lines 'windows_build' 'full-suite run 2') -ceq $ExpectedWindowsBuild) "run 2 Windows build differs from the frozen host"
  Require ((Get-EnvelopeValue $envelope.lines 'child_environment_policy' 'full-suite run 2') -ceq $ChildEnvironmentPolicy) "run 2 lacks the fail-closed child environment policy"
  Require ((Get-EnvelopeValue $envelope.lines 'publication_nonce' 'full-suite run 2') -ceq $PublicationNonce) "run 2 is not bound to the preparation nonce"
  Require ((Get-EnvelopeValue $envelope.lines 'isolated_localappdata' 'full-suite run 2') -ceq 'true') "run 2 lacks disposable LOCALAPPDATA evidence"
  Require ((Get-EnvelopeValue $envelope.lines 'production_data_access_telemetry' 'full-suite run 2') -ceq 'not-collected') "run 2 telemetry claim is invalid"
  Require ((Get-EnvelopeValue $envelope.lines 'stage' 'full-suite run 2') -ceq 'full-suite-run-2') "run 2 stage marker is not exact"
  $run2TreeBefore = Get-EnvelopeValue $envelope.lines 'isolated_localappdata_tree_before_sha256' 'full-suite run 2'
  $run2TreeAfter = Get-EnvelopeValue $envelope.lines 'isolated_localappdata_tree_after_sha256' 'full-suite run 2'
  $run2TreeChanged = Get-EnvelopeValue $envelope.lines 'isolated_localappdata_tree_changed' 'full-suite run 2'
  Require ($run2TreeBefore -match '^(?:absent|[0-9a-f]{64})$' -and $run2TreeAfter -match '^(?:absent|[0-9a-f]{64})$') 'run 2 isolated-tree fingerprint is invalid'
  Require ($run2TreeChanged -in @('true', 'false')) 'run 2 isolated-tree change telemetry is invalid'
  Require ((-not ($run2TreeBefore -ceq $run2TreeAfter)) -eq [bool]::Parse($run2TreeChanged)) 'run 2 isolated-tree change telemetry disagrees with its fingerprints'
  Require ((Get-EnvelopeValue $envelope.lines 'isolated_test_config_policy' 'full-suite run 2') -ceq 'absent_or_canonical_defaults_only') "run 2 config policy is not fail-closed"
  $configCount = [int](Get-EnvelopeValue $envelope.lines 'isolated_test_config_count' 'full-suite run 2')
  $configHash = Get-EnvelopeValue $envelope.lines 'isolated_test_config_sha256' 'full-suite run 2'
  Require ($configCount -in @(0, 1)) "run 2 isolated config count is invalid"
  Require (($configCount -eq 0 -and $configHash -ceq 'absent') -or
      ($configCount -eq 1 -and $configHash -match '^[0-9a-f]{64}$')) "run 2 isolated config fingerprint is invalid"
  $results = Assert-TestResults $envelope $RegisteredTests 'full-suite run 2'
  [pscustomobject]@{
    envelope = $envelope
    results = $results
    config_count = $configCount
    config_sha256 = $configHash
  }
}

function Assert-TwoRunEquivalence([object]$Run1, [object]$Run2) {
  Require ($Run1.registered -eq $Run2.registered) "full-suite registered counts differ"
  Require ($Run1.passed -eq $Run2.passed -and $Run1.failed -eq $Run2.failed) "full-suite result counts differ"
  Require ($Run1.result_text -ceq $Run2.result_text) "ordered PASS/FAIL result streams are not byte-identical"
  Require ($Run1.result_sha256 -ceq $Run2.result_sha256) "ordered result-stream hashes differ"
  Require ($Run1.n22.snapshot_count -eq $Run2.n22.snapshot_count) "N22 snapshot counts differ between runs"
  Require ($Run1.n22.normalized_text -ceq $Run2.n22.normalized_text) "N22 marker matrices differ after factual snapshot hashes are normalized"
  Require ($Run1.n22.normalized_sha256 -ceq $Run2.n22.normalized_sha256) "normalized N22 marker hashes differ"
  [pscustomobject][ordered]@{
    result_stream_byte_identical = $true
    result_stream_sha256 = $Run1.result_sha256
    n22_matrix_shape_identical = $true
    n22_normalized_sha256 = $Run1.n22.normalized_sha256
  }
}

function New-N22RpcRequest([int]$Id, [string]$Operation, [object]$Arguments, [string]$Label) {
  $request = [ordered]@{
    jsonrpc = '2.0'
    id = $Id
    method = 'tools/call'
    params = [ordered]@{ name = $Operation; arguments = $Arguments }
  }
  [pscustomobject][ordered]@{
    id = $Id
    operation = $Operation
    label = $Label
    body = (($request | ConvertTo-Json -Depth 30 -Compress) + "`n")
  }
}

function New-N22ToolsListRequest([int]$Id, [string]$Label) {
  $request = [ordered]@{
    jsonrpc = '2.0'
    id = $Id
    method = 'tools/list'
    params = [ordered]@{}
  }
  [pscustomobject][ordered]@{
    id = $Id
    operation = 'tools/list'
    label = $Label
    body = (($request | ConvertTo-Json -Depth 12 -Compress) + "`n")
  }
}

function Invoke-N22StdioSession(
  [string]$BrainId,
  [bool]$AllowWrite,
  [object[]]$Requests,
  [string]$IsolatedLocalAppData,
  [string]$Label
) {
  Require (@($Requests).Count -gt 0) "$Label has no stdio requests"
  $serveArguments = "serve --brain $BrainId"
  if ($AllowWrite) { $serveArguments += ' --allow-write' }
  $inputText = (@($Requests | ForEach-Object { $_.body }) -join '')
  $environment = @{
    LOCALAPPDATA = $IsolatedLocalAppData
    QBRAIN_SOURCE = 'n22_ambient_forbidden'
    QBRAIN_MCP_ALLOW_WRITE = '0'
  }
  $capture = Invoke-CapturedProcess -FilePath $Qbrain -Arguments $serveArguments `
      -ProcessTimeoutSeconds 240 -WorkingDirectory $Root -EnvironmentOverrides $environment `
      -StandardInputText $inputText -RemoveEnvironmentVariables @('QBRAIN_BRAIN', 'QBRAIN_MCP_TOKEN')
  Require ($capture.exit_code -eq 0) "$Label stdio server exited nonzero"
  Require ($capture.stderr -match '(?m)^\[qbrain-serve\] stdio MCP ready brain=') "$Label lacks stdio startup evidence"
  Require ($capture.stderr -match '(?m)^\[qbrain-serve\] shutdown: stdin EOF\s*$') "$Label lacks clean EOF shutdown evidence"
  Require (@([regex]::Matches($capture.stderr, '(?m)^\[qbrain-serve\] stdio MCP ready brain=' )).Count -eq 1) "$Label has duplicate startup evidence"
  Require (@([regex]::Matches($capture.stderr, '(?m)^\[qbrain-serve\] shutdown: stdin EOF\s*$')).Count -eq 1) "$Label has duplicate shutdown evidence"
  if ($AllowWrite) {
    Require ($capture.stderr -match '(?m)^\[qbrain-serve\] stdio MCP ready brain=.* write=ENABLED\r?$') "$Label lacks argv write-enable evidence"
  } else {
    Require ($capture.stderr -match '(?m)^\[qbrain-serve\] stdio MCP ready brain=.* write=disabled\r?$') "$Label is not write-disabled"
  }
  $responseLines = @($capture.stdout -split '\r?\n' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  Require ($responseLines.Count -eq @($Requests).Count) "$Label response count differs from request count"
  $responses = New-Object System.Collections.Generic.List[object]
  for ($index = 0; $index -lt @($Requests).Count; ++$index) {
    $request = @($Requests)[$index]
    $response = ConvertFrom-StrictJsonText $responseLines[$index] "$Label response $($request.label)"
    Require-ExactJsonPropertyNames $response @('jsonrpc', 'id', 'result') "$Label response $($request.label) envelope"
    Require-JsonStringValue $response.jsonrpc '2.0' "$Label response $($request.label) jsonrpc"
    Require-JsonIntegerValue $response.id $request.id "$Label response $($request.label) id"
    $responses.Add([pscustomobject][ordered]@{
        request = $request
        response = $response
        response_text = $responseLines[$index]
      })
  }
  [pscustomobject][ordered]@{
    label = $Label
    brain_id = $BrainId
    allow_write = $AllowWrite
    command = "build\cl\qbrain.exe $serveArguments"
    request_count = @($Requests).Count
    requests = @($Requests)
    responses = $responses.ToArray()
    exit_code = $capture.exit_code
    stdout_sha256 = Text-Hash $capture.stdout
    stderr_sha256 = Text-Hash $capture.stderr
    startup_marker = $true
    shutdown_marker = $true
    write_mode_marker = $true
  }
}

function Get-N22StructuredMcpPayload(
  [object]$Probe,
  [bool]$ExpectedError,
  [string]$ExpectedCode = '',
  [string]$ExpectedField = ''
) {
  $label = "real stdio $($Probe.request.label)"
  $result = $Probe.response.result
  Require-ExactJsonPropertyNames $result @('content', 'isError') "$label result"
  Require-JsonBooleanValue $result.isError $ExpectedError "$label isError"
  Require ($result.content -is [Array]) "$label content is not an array"
  $content = @($result.content)
  if ($ExpectedError) {
    Require ($content.Count -eq 1) "$label error must contain exactly one content block"
  } else {
    Require ($content.Count -in @(1, 2)) "$label success content block count is not 1 or 2"
  }
  $structured = New-Object System.Collections.Generic.List[object]
  for ($index = 0; $index -lt $content.Count; ++$index) {
    Require-ExactJsonPropertyNames $content[$index] @('type', 'text') "$label content[$index]"
    Require-JsonStringValue $content[$index].type 'text' "$label content[$index] type"
    Require ($content[$index].text -is [string] -and $content[$index].text.Length -gt 0) "$label content[$index] text is empty"
    $text = [string]$content[$index].text
    if ($text.TrimStart().StartsWith('{', [StringComparison]::Ordinal) -or
        $text.TrimStart().StartsWith('[', [StringComparison]::Ordinal)) {
      try {
        $payload = ConvertFrom-StrictJsonText $text "$label content[$index] structured payload"
        $structured.Add([pscustomobject]@{ index=$index; payload=$payload })
      } catch {
        if ($ExpectedError) { throw }
      }
    }
  }
  Require ($structured.Count -eq 1) "$label must contain exactly one structured JSON text block"
  $payload = $structured[0].payload
  if ($ExpectedError) {
    Require-ExactJsonPropertyNames $payload @('error') "$label error payload"
    Require-ExactJsonPropertyNames $payload.error @('code', 'field', 'message') "$label structured error"
    Require-JsonStringValue $payload.error.code $ExpectedCode "$label error code"
    Require-JsonStringValue $payload.error.field $ExpectedField "$label error field"
    Require ($payload.error.message -is [string] -and $payload.error.message.Length -in 1..512) "$label error message is not bounded"
  }
  [pscustomobject][ordered]@{
    payload = $payload
    content_count = $content.Count
    structured_block_index = $structured[0].index
    single_structured_error_block = $ExpectedError -and $content.Count -eq 1
  }
}

function Assert-N22HitRows([object]$Payload, [string]$SourceId, [string]$Label, [int]$ExpectedCount = -1) {
  Require ($Payload -is [Array]) "$Label payload is not an array"
  $rows = @($Payload)
  if ($ExpectedCount -ge 0) { Require ($rows.Count -eq $ExpectedCount) "$Label row count is not $ExpectedCount" }
  foreach ($row in $rows) {
    Require-ExactJsonPropertyNames $row @('source_id', 'slug', 'line', 'snippet', 'kind') "$Label row"
    Require-JsonStringValue $row.source_id $SourceId "$Label row source_id"
    Require ($row.slug -is [string] -and $row.slug.Length -gt 0 -and $row.slug.Length -le 512) "$Label row slug is not bounded"
    Require-JsonInteger $row.line "$Label row line"
    Require ([int64]$row.line -ge 1) "$Label row line is not one-based"
    Require ($row.snippet -is [string]) "$Label row snippet is not a string"
    Require ([Text.Encoding]::UTF8.GetByteCount([string]$row.snippet) -le 200) "$Label row snippet exceeds 200 UTF-8 bytes"
    Require ($row.kind -is [string] -and $row.kind.Length -gt 0 -and $row.kind.Length -le 512) "$Label row kind is not bounded"
  }
  Write-Output -NoEnumerate $rows
}

function Assert-N22DescriptionContract([string]$Name, [string]$Description, [string]$Label) {
  Require ($Description.Length -in 1..1024) "$Label is not bounded"
  Require ($Description -notmatch '[\x00-\x08\x0B\x0C\x0E-\x1F]') "$Label contains a control byte"
  $requiredPatterns = New-Object System.Collections.Generic.List[object]
  $affirmativeUnsupportedPatterns = New-Object System.Collections.Generic.List[string]

  $noAst = '(?i)\b(?:not\s+(?:an?\s+)?ast|no\s+ast|without\s+(?:an?\s+)?ast)\b'
  $noCompilerIndex = '(?i)(?:\b(?:not|no|without)\b[^.;]{0,80}\bcompiler index\b)'
  $noResolution = '(?i)\b(?:no|without)\s+(?:type/overload|overload/type|type(?:\s+or\s+overload)?|overload(?:\s+or\s+type)?)\s+resolution\b'
  $noPersistedEdges = '(?i)(?:\b(?:no|without)\b[^.;]{0,80}\b(?:persisted\s+)?call edges?\b|\bdoes not (?:store|persist|maintain)\b[^.;]{0,40}\bcall edges?\b)'
  foreach ($pattern in @(
      '(?i)\b(?:is|acts as)\s+(?:an?\s+)?ast\b',
      '(?i)\b(?:uses?|using|implements?|builds?|provides?)\s+(?:an?\s+)?(?:ast|tree-sitter|compiler index)\b',
      '(?i)\b(?:provides?|performs?|supports?)\s+(?:exact\s+)?(?:type|overload|type/overload|overload/type)\s+resolution\b',
      '(?i)(?<!not )(?<!never )\b(?:stores?|persists?|maintains?)\s+(?:a\s+)?(?:persisted\s+)?(?:call edges?|call graph)\b')) {
    $affirmativeUnsupportedPatterns.Add($pattern)
  }

  switch ($Name) {
    'code_callees' {
      foreach ($requirement in @(
          @{ label='source-scoped contract'; pattern='(?i)\bsource[- ]scoped\b' },
          @{ label='bounded brace-body contract'; pattern='(?i)(?:\bbounded\b[^.;]{0,80}\bbrace[- ]body\b|\bbrace[- ]body\b[^.;]{0,80}\bbounded\b)' },
          @{ label='lexical/heuristic contract'; pattern='(?i)\b(?:lexical|heuristic)\b' },
          @{ label='direct-callee contract'; pattern='(?i)\b(?:direct[- ]callee|one[- ]hop)\b' },
          @{ label='AST limitation'; pattern=$noAst },
          @{ label='compiler-index limitation'; pattern=$noCompilerIndex },
          @{ label='type/overload limitation'; pattern=$noResolution },
          @{ label='persisted-edge limitation'; pattern=$noPersistedEdges })) {
        $requiredPatterns.Add($requirement)
      }
      $affirmativeUnsupportedPatterns.Add(
        '(?i)\b(?:supports?|provides?|performs?)\s+(?:recursive|transitive)[^.;]{0,48}\b(?:upstream|analysis|parity)\b')
      break
    }
    'code_flow' {
      foreach ($requirement in @(
          @{ label='stateless contract'; pattern='(?i)\bstateless\b' },
          @{ label='deterministic contract'; pattern='(?i)\bdeterministic\b' },
          @{ label='bounded contract'; pattern='(?i)\bbounded\b' },
          @{ label='breadth-first contract'; pattern='(?i)\bbreadth[- ]first\b' },
          @{ label='source-scoped contract'; pattern='(?i)\bsource[- ]scoped\b' },
          @{ label='heuristic contract'; pattern='(?i)\bheuristic\b' },
          @{ label='callee-relation contract'; pattern='(?i)\bcallees?\b' },
          @{ label='AST limitation'; pattern=$noAst },
          @{ label='compiler-index limitation'; pattern=$noCompilerIndex },
          @{ label='type/overload limitation'; pattern=$noResolution },
          @{ label='persisted-edge limitation'; pattern=$noPersistedEdges },
          @{ label='terminal/sink limitation'; pattern='(?i)\b(?:no|without)\s+(?:terminal(?:-node)?(?:/sink)?|sink)\s+classification\b' })) {
        $requiredPatterns.Add($requirement)
      }
      $affirmativeUnsupportedPatterns.Add(
        '(?i)\b(?:provides?|performs?|supports?)\s+(?:terminal(?:-node)?(?:/sink)?|sink)\s+classification\b')
      break
    }
    'code_blast' {
      foreach ($requirement in @(
          @{ label='one-hop heuristic subset contract'; pattern='(?i)\bone[- ]hop\b[^.;]{0,100}\bheuristic\b[^.;]{0,100}\bsubset\b' },
          @{ label='def/ref/caller/callee contract'; pattern='(?i)\bdef/ref/caller/callee\b' },
          @{ label='source-scoped contract'; pattern='(?i)\bsource[- ]scoped\b' },
          @{ label='AST limitation'; pattern=$noAst },
          @{ label='compiler-index limitation'; pattern=$noCompilerIndex },
          @{ label='type/overload limitation'; pattern=$noResolution },
          @{ label='persisted-edge limitation'; pattern=$noPersistedEdges },
          @{ label='recursive/transitive parity limitation'; pattern='(?i)\b(?:no|not|without|does not provide)\b[^.;]{0,80}\b(?:recursive|transitive)[^.;]{0,80}\b(?:upstream|blast|parity)\b' })) {
        $requiredPatterns.Add($requirement)
      }
      $affirmativeUnsupportedPatterns.Add(
        '(?i)\b(?:supports?|provides?|performs?)\s+(?:recursive|transitive)[^.;]{0,48}\b(?:upstream|blast|parity)\b')
      $affirmativeUnsupportedPatterns.Add(
        '(?i)\b(?:returns?|provides?|includes?|emits?|supports?)\s+(?:depth_groups|confidence(?:\s+scores?)?)\b')
      break
    }
    'code_traversal_cache_clear' {
      foreach ($requirement in @(
          @{ label='guarded stateless compatibility contract'; pattern='(?i)\bguarded\b[^.;]{0,80}\bstateless\b[^.;]{0,80}\bcompatibility\s+no-op\b' },
          @{ label='zero-row contract'; pattern='(?i)\b(?:clears?|returns?)\s+zero\s+(?:cached\s+)?rows?\b' },
          @{ label='no persisted cache contract'; pattern='(?i)\b(?:no|without)\s+(?:persisted\s+)?(?:traversal\s+)?cache\b' },
          @{ label='no table contract'; pattern='(?i)\b(?:no|without)\s+(?:cache\s+)?table\b' },
          @{ label='no migration contract'; pattern='(?i)\b(?:no|without)\s+(?:schema\s+)?migration\b' })) {
        $requiredPatterns.Add($requirement)
      }
      foreach ($pattern in @(
          '(?i)\b(?:has|uses?|persists?|stores?|maintains?|creates?)\s+(?:a\s+)?(?:real\s+)?(?:persisted\s+)?(?:traversal\s+)?cache\b',
          '(?i)\b(?:creates?|uses?|requires?|adds?)\s+(?:a\s+)?(?:cache\s+)?(?:table|migration)\b')) {
        $affirmativeUnsupportedPatterns.Add($pattern)
      }
      break
    }
    default { throw "$Label has unknown operation name: $Name" }
  }
  foreach ($requirement in $requiredPatterns) {
    Require ($Description -match [string]$requirement.pattern) "$Label omits $($requirement.label)"
  }
  foreach ($pattern in $affirmativeUnsupportedPatterns) {
    Require ($Description -notmatch $pattern) "$Label makes an affirmative unsupported-capability claim"
  }
}

function Assert-N22ToolsList([object]$Response, [string]$Label) {
  Require-ExactJsonPropertyNames $Response @('jsonrpc', 'id', 'result') "$Label envelope"
  Require-JsonStringValue $Response.jsonrpc '2.0' "$Label jsonrpc"
  Require-ExactJsonPropertyNames $Response.result @('tools') "$Label result"
  Require ($Response.result.tools -is [Array]) "$Label tools is not an array"
  $wanted = @('code_callees', 'code_flow', 'code_blast', 'code_traversal_cache_clear')
  $tools = @($Response.result.tools | Where-Object { $wanted -contains [string]$_.name })
  Require ($tools.Count -eq 4) "$Label does not expose exactly four N22 tools"
  foreach ($name in $wanted) {
    $matches = @($tools | Where-Object { $_.name -ceq $name })
    Require ($matches.Count -eq 1) "$Label contains an unexpected duplicate $name definition"
    Require-ExactJsonPropertyNames $matches[0] @('name', 'description', 'inputSchema') "$Label $name definition"
    Require ($matches[0].description -is [string] -and $matches[0].description.Length -in 1..1024) "$Label $name description is not bounded"
    $description = [string]$matches[0].description
    $descriptionLower = $description.ToLowerInvariant()
    Assert-N22DescriptionContract $name $description "$Label $name description"
    $schema = $matches[0].inputSchema
    Require ($schema.type -ceq 'object') "$Label $name schema is not object"
    Require-JsonBooleanValue $schema.additionalProperties $false "$Label $name additionalProperties"

    if ($name -eq 'code_traversal_cache_clear') {
      Require-ExactJsonPropertyNames $schema @('type', 'additionalProperties', 'properties') "$Label $name schema shape"
      Require-ExactJsonPropertyNames $schema.properties @() "$Label $name properties"
      continue
    }
    $expectedProperties = if ($name -eq 'code_flow') {
      @('entry_point', 'symbol', 'name', 'source_id', 'depth', 'limit', 'page_limit')
    } else {
      @('symbol', 'name', 'source_id', 'limit', 'page_limit')
    }
    Require-ExactJsonPropertyNames $schema @('type', 'additionalProperties', 'properties', 'anyOf') "$Label $name schema shape"
    Require-ExactJsonPropertyNames $schema.properties $expectedProperties "$Label $name properties"
    foreach ($field in @('symbol', 'name')) {
      $fieldSchema = Get-JsonPropertyValue $schema.properties $field "$Label $name properties"
      Require ($fieldSchema.type -ceq 'string') "$Label $name $field type"
      Require-JsonIntegerValue $fieldSchema.maxLength 256 "$Label $name $field maxLength"
      Require-JsonIntegerValue $fieldSchema.minLength 1 "$Label $name $field minLength"
    }
    if ($name -eq 'code_flow') {
      Require ($schema.properties.entry_point.type -ceq 'string') "$Label flow entry_point type"
      Require-JsonIntegerValue $schema.properties.entry_point.minLength 1 "$Label flow entry_point minLength"
      Require-JsonIntegerValue $schema.properties.entry_point.maxLength 256 "$Label flow entry_point maxLength"
      Require-JsonIntegerValue $schema.properties.depth.minimum 0 "$Label flow depth minimum"
      Require-JsonIntegerValue $schema.properties.depth.maximum 8 "$Label flow depth maximum"
      Require-JsonIntegerValue $schema.properties.depth.default 2 "$Label flow depth default"
      Require ($descriptionLower.Contains('breadth')) "$Label flow description omits breadth-first contract"
      Require ($descriptionLower.Contains('heuristic')) "$Label flow description omits heuristic contract"
    } else {
      $defaultLimit = if ($name -eq 'code_blast') { 80 } else { 50 }
      Require-JsonIntegerValue $schema.properties.limit.default $defaultLimit "$Label $name limit default"
      Require ($descriptionLower.Contains('heuristic')) "$Label $name description omits heuristic contract"
    }
    if ($name -eq 'code_callees') {
      Require ($description -match '(?i)\bbrace[- ]body\b') "$Label callees description omits brace-body boundary"
    }
    if ($name -eq 'code_blast') {
      Require-JsonIntegerValue $schema.properties.limit.default 80 "$Label blast limit default"
      Require ($descriptionLower.Contains('one-hop') -or $descriptionLower.Contains('one hop')) "$Label blast description omits one-hop contract"
    }
    if ($name -ne 'code_blast' -and $name -ne 'code_flow') {
      Require-JsonIntegerValue $schema.properties.limit.default 50 "$Label callees limit default"
    }
    foreach ($field in @('limit', 'page_limit')) {
      $fieldSchema = Get-JsonPropertyValue $schema.properties $field "$Label $name properties"
      Require ($fieldSchema.type -ceq 'integer') "$Label $name $field type"
      Require-JsonIntegerValue $fieldSchema.minimum 0 "$Label $name $field minimum"
      $maximum = if ($field -eq 'limit') { 200 } else { 2000 }
      Require-JsonIntegerValue $fieldSchema.maximum $maximum "$Label $name $field maximum"
    }
    Require-JsonIntegerValue $schema.properties.page_limit.default 500 "$Label $name page_limit default"
    if ($name -eq 'code_flow') {
      Require-JsonIntegerValue $schema.properties.limit.default 50 "$Label flow limit default"
    }
    Require ($schema.properties.source_id.type -ceq 'string') "$Label $name source type"
    Require-JsonStringValue $schema.properties.source_id.default 'default' "$Label $name source default"
    Require-JsonIntegerValue $schema.properties.source_id.minLength 1 "$Label $name source minLength"
    Require-JsonIntegerValue $schema.properties.source_id.maxLength 64 "$Label $name source maxLength"
    Require-JsonStringValue $schema.properties.source_id.pattern $ExpectedN22SourceIdPattern "$Label $name source pattern"
    Require ($schema.anyOf -is [Array]) "$Label $name anyOf is not an array"
    $required = @($schema.anyOf | ForEach-Object { (@($_.required) -join ',') })
    $expectedRequired = if ($name -eq 'code_flow') { @('entry_point', 'symbol', 'name') } else { @('symbol', 'name') }
    $actualRequired = (($required | Sort-Object) -join '|')
    $wantedRequired = (($expectedRequired | Sort-Object) -join '|')
    Require ($actualRequired -ceq $wantedRequired) "$Label $name alias requirements differ"
  }
  [pscustomobject][ordered]@{ tools = $wanted; exact = $true }
}

function Assert-N22DoctorPayload([object]$Payload, [string]$Label) {
  Require-ExactJsonPropertyNames $Payload @('ok', 'overall', 'schema_version', 'stats', 'checks', 'notes') "$Label payload"
  Require-JsonBooleanValue $Payload.ok $true "$Label ok"
  Require-JsonStringValue $Payload.overall 'OK' "$Label overall"
  Require-JsonIntegerValue $Payload.schema_version 12 "$Label schema_version"
  Require-ExactJsonPropertyNames $Payload.stats @('pages', 'chunks', 'links', 'embedded_chunks') "$Label stats"
  foreach ($field in @('pages', 'chunks', 'links', 'embedded_chunks')) {
    $fieldValue = Get-JsonPropertyValue $Payload.stats $field "$Label stats"
    Require-JsonInteger $fieldValue "$Label stats.$field"
    Require ([int64]$fieldValue -ge 0) "$Label stats.$field is negative"
  }
  Require ($Payload.checks -is [Array] -and @($Payload.checks).Count -eq 4) "$Label checks shape"
  foreach ($check in @($Payload.checks)) {
    Require-ExactJsonPropertyNames $check @('name', 'status') "$Label check"
    Require ($check.name -is [string] -and $check.status -is [string]) "$Label check fields are not strings"
  }
  Require ($Payload.notes -is [Array]) "$Label notes is not an array"
  Require (-not (Has-JsonProperty $Payload 'db_path')) "$Label leaks a database path"
  $true
}

function Add-N22SeedRequest([System.Collections.Generic.List[object]]$Requests, [int]$Id, [string]$Slug, [string]$SourceId, [string]$Body) {
  $arguments = [ordered]@{
    slug = $Slug
    title = $Slug
    body = $Body
    type = 'note'
    source_id = $SourceId
  }
  $Requests.Add((New-N22RpcRequest $Id 'put_page' $arguments "seed:${SourceId}:${Slug}"))
}

function Invoke-N22RealMcpAndSchema([object]$PreparationState, [string]$ProductionBinaryHash,
                                    [string]$PublicationNonce) {
  Require ($null -ne $PreparationState) 'real MCP probe is missing preparation state'
  Require ($PreparationState.registered_tests.Count -ge $MinimumRegisteredTests) 'real MCP probe has an incomplete frozen test registration'
  Require ((File-Hash $Qbrain) -ceq $ProductionBinaryHash) 'real MCP probe started with an unexpected production binary'
  Require ($PublicationNonce -match '^[0-9a-f]{32}$') 'real MCP probe lacks a valid publication nonce'
  Require ([string]$PreparationState.publication_nonce -ceq $PublicationNonce) 'real MCP probe does not match the prepared publication nonce'
  $sessionNonce = [guid]::NewGuid().ToString('N')
  $sandbox = Join-Path ([IO.Path]::GetTempPath()) ('qbrain_n22_stdio_' + $sessionNonce)
  $isolatedLocalAppData = Join-Path $sandbox 'localappdata'
  $isolatedQbrainTree = Join-Path $isolatedLocalAppData 'Qbrain'
  $brainId = 'n22stdio' + $sessionNonce.Substring(0, 16)
  $failure = $null
  $mcpEvidence = $null
  $doctorEvidence = $null
  try {
    Assert-PlainPathChain $sandbox
    New-Item -ItemType Directory -Force -Path $isolatedLocalAppData | Out-Null
    $isolatedTreeBefore = Get-DisposableTreeFingerprint $isolatedQbrainTree -PersistentDataTree
    $config = Invoke-CapturedProcess -FilePath $Qbrain -Arguments "config set mcp.allowed_sources team_a,ghost --brain $brainId" `
        -ProcessTimeoutSeconds 60 -WorkingDirectory $Root -EnvironmentOverrides @{ LOCALAPPDATA=$isolatedLocalAppData; QBRAIN_SOURCE='n22_ambient_forbidden' } `
        -RemoveEnvironmentVariables @('QBRAIN_BRAIN', 'QBRAIN_MCP_TOKEN')
    Require ($config.exit_code -eq 0) 'real MCP fixture config command failed'
    Require ($config.stdout -match '(?m)^set mcp[.]allowed_sources\s*$') 'real MCP fixture config command output is not exact'
    Assert-SafeEvidenceText ($config.stdout + "`n" + $config.stderr) 'real MCP fixture config output'

    $seedRequests = New-Object System.Collections.Generic.List[object]
    $seedId = 21900
    Add-N22SeedRequest $seedRequests $seedId 'stdio/body' 'team_a' "void BodyRoot() {`n  Alpha();`n  Beta();`n}`n"; ++$seedId
    Add-N22SeedRequest $seedRequests $seedId 'stdio/flow-root' 'team_a' "void FlowRoot() { Alpha(); Beta(); }`n"; ++$seedId
    Add-N22SeedRequest $seedRequests $seedId 'stdio/flow-alpha' 'team_a' "void Alpha() { Gamma(); }`n"; ++$seedId
    Add-N22SeedRequest $seedRequests $seedId 'stdio/flow-beta' 'team_a' "void Beta() { Gamma(); FlowRoot(); }`n"; ++$seedId
    Add-N22SeedRequest $seedRequests $seedId 'stdio/flow-gamma' 'team_a' "void Gamma() { Delta(); }`n"; ++$seedId
    Add-N22SeedRequest $seedRequests $seedId 'stdio/flow-delta' 'team_a' "void Delta() {}`n"; ++$seedId
    Add-N22SeedRequest $seedRequests $seedId 'stdio/blast-def' 'team_a' "void BlastRoot() {`n  BlastCallee();`n}`n"; ++$seedId
    Add-N22SeedRequest $seedRequests $seedId 'stdio/blast-ref' 'team_a' "auto reference = BlastRoot;`n"; ++$seedId
    Add-N22SeedRequest $seedRequests $seedId 'stdio/blast-call' 'team_a' "BlastRoot();`n"; ++$seedId
    Add-N22SeedRequest $seedRequests $seedId 'stdio/ambient' 'default' "void AmbientRoot() { DefaultAmbient(); }`n"; ++$seedId
    $fanBody = "void FanRoot() {`n"
    for ($index = 0; $index -lt 205; ++$index) { $fanBody += ('  Fan{0:D3}();' -f $index) + "`n" }
    $fanBody += "}`n"
    Add-N22SeedRequest $seedRequests $seedId 'stdio/fan' 'team_a' $fanBody; ++$seedId
    $seedSession = Invoke-N22StdioSession $brainId $true $seedRequests $isolatedLocalAppData 'fixture-seed'
    foreach ($seedProbe in @($seedSession.responses)) {
      $seedPayload = Get-N22StructuredMcpPayload $seedProbe $false
      Require ($seedPayload.payload -isnot [Array]) "real MCP fixture seed $($seedProbe.request.label) returned an array"
    }
    Require ((File-Hash $Qbrain) -ceq $ProductionBinaryHash) 'production binary changed during real MCP fixture setup'

    $fixtureTree = Get-DisposableTreeFingerprint $isolatedQbrainTree -PersistentDataTree
    $readRequests = New-Object System.Collections.Generic.List[object]
    $requestId = 22000
    $readRequests.Add((New-N22ToolsListRequest $requestId 'registry:tools-list:real')); ++$requestId
    foreach ($operation in @(
        [pscustomobject]@{ name='code_callees'; canonical='symbol'; symbol='BodyRoot'; numeric=@('limit','page_limit') },
        [pscustomobject]@{ name='code_flow'; canonical='entry_point'; symbol='FlowRoot'; numeric=@('depth','limit','page_limit') },
        [pscustomobject]@{ name='code_blast'; canonical='symbol'; symbol='BlastRoot'; numeric=@('limit','page_limit') })) {
      $successArguments = [ordered]@{ source_id='Team_A'; $operation.canonical=$operation.symbol }
      if ($operation.name -eq 'code_flow') { $successArguments.depth = 3 }
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name $successArguments "mcp:real:$($operation.name):success")); ++$requestId
      $emptyArguments = [ordered]@{ source_id='Team_A'; $operation.canonical='NoSuchMcpSymbol' }
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name $emptyArguments "mcp:real:$($operation.name):empty")); ++$requestId
      $clampArguments = [ordered]@{ source_id='Team_A'; $operation.canonical='FanRoot'; limit=999; page_limit=9999 }
      if ($operation.name -eq 'code_flow') { $clampArguments.depth = 99 }
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name $clampArguments "mcp:real:$($operation.name):clamp")); ++$requestId
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name ([object[]]@()) "mcp:real:$($operation.name):non-object")); ++$requestId
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name ([ordered]@{ $operation.canonical=$operation.symbol; unexpected='x' }) "mcp:real:$($operation.name):unknown-field")); ++$requestId
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name ([ordered]@{ $operation.canonical=7 }) "mcp:real:$($operation.name):wrong-symbol")); ++$requestId
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name ([ordered]@{ $operation.canonical=$operation.symbol; source_id=$null }) "mcp:real:$($operation.name):null-source")); ++$requestId
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name ([ordered]@{ $operation.canonical=$operation.symbol; limit=-1 }) "mcp:real:$($operation.name):signed-limit")); ++$requestId
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name ([ordered]@{ $operation.canonical=$operation.symbol; limit=1.5 }) "mcp:real:$($operation.name):floating-limit")); ++$requestId
      $aliasArguments = [ordered]@{ $operation.canonical=$operation.symbol; name='DifferentMcpSymbol'; source_id='Team_A' }
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name $aliasArguments "mcp:real:$($operation.name):alias-conflict")); ++$requestId
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name ([ordered]@{ $operation.canonical=$operation.symbol; source_id='ghost' }) "mcp:real:$($operation.name):unknown-source")); ++$requestId
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name ([ordered]@{ $operation.canonical=$operation.symbol; source_id='team_b' }) "mcp:real:$($operation.name):denied-source")); ++$requestId
      $readRequests.Add((New-N22RpcRequest $requestId $operation.name ([ordered]@{ $operation.canonical='AmbientRoot' }) "mcp:real:$($operation.name):ambient-default")); ++$requestId
    }
    $readRequests.Add((New-N22RpcRequest $requestId 'code_traversal_cache_clear' ([ordered]@{}) 'mcp:real:cache:remote-denied')); ++$requestId
    $readSession = Invoke-N22StdioSession $brainId $false $readRequests $isolatedLocalAppData 'read-matrix'

    $allowRequests = New-Object System.Collections.Generic.List[object]
    $allowId = 23000
    foreach ($operation in @('code_callees', 'code_flow', 'code_blast')) {
      $canonical = if ($operation -eq 'code_flow') { 'entry_point' } else { 'symbol' }
      $allowRequests.Add((New-N22RpcRequest $allowId $operation ([ordered]@{ $canonical='BodyRoot'; source_id='team_b' }) "mcp:real:${operation}:write-does-not-authorize")); ++$allowId
    }
    $allowRequests.Add((New-N22RpcRequest $allowId 'code_traversal_cache_clear' ([ordered]@{}) 'mcp:real:cache:remote-allowed')); ++$allowId
    $allowRequests.Add((New-N22RpcRequest $allowId 'code_traversal_cache_clear' ([ordered]@{ unexpected='x' }) 'mcp:real:cache:unexpected')); ++$allowId
    $allowRequests.Add((New-N22RpcRequest $allowId 'code_traversal_cache_clear' ([object[]]@()) 'mcp:real:cache:non-object')); ++$allowId
    $allowSession = Invoke-N22StdioSession $brainId $true $allowRequests $isolatedLocalAppData 'write-gate-matrix'

    $allProbes = @($readSession.responses + $allowSession.responses)
    $evidenceRows = New-Object System.Collections.Generic.List[object]
    $successByOperation = @{}
    $clampByOperation = @{}
    $emptyByOperation = @{}
    $errorCount = 0
    $singleErrorCount = 0
    foreach ($probe in $allProbes) {
      $serialized = $probe.response_text
      Assert-SafeEvidenceText $serialized "real MCP $($probe.request.label) response"
      Require ($serialized.Length -le 2000000) "real MCP $($probe.request.label) response exceeds evidence bound"
      $sensitiveScanText = $serialized
      if ($probe.request.operation -eq 'tools/list') {
        $n22Tools = @($probe.response.result.tools | Where-Object {
            @('code_callees', 'code_flow', 'code_blast', 'code_traversal_cache_clear') -contains [string]$_.name
          })
        $sensitiveScanText = (($n22Tools | ConvertTo-Json -Depth 30 -Compress) + "`n")
      }
      Require ($sensitiveScanText -notmatch '(?i)(?:[A-Z]:\\|\\\\|Volume\{|Administrator|api[_-]?key|bearer\s+)') "real MCP $($probe.request.label) response leaks sensitive data"
      Require ($serialized -notmatch '(?i)n22_ambient_forbidden') "real MCP $($probe.request.label) consumed ambient source"
      $expectedError = $probe.request.label -match '(?:non-object|unknown-field|wrong-symbol|null-source|signed-limit|floating-limit|alias-conflict|unknown-source|denied-source|write-does-not-authorize|remote-denied|unexpected|non-object)$'
      $expectedCode = ''
      $expectedField = ''
      if ($probe.request.label -match 'unknown-field|unexpected') { $expectedCode='invalid_argument'; $expectedField='unexpected' }
      elseif ($probe.request.label -match 'wrong-symbol') { $expectedCode='invalid_argument'; $expectedField=if ($probe.request.operation -eq 'code_flow') { 'entry_point' } else { 'symbol' } }
      elseif ($probe.request.label -match 'null-source') { $expectedCode='invalid_argument'; $expectedField='source_id' }
      elseif ($probe.request.label -match 'signed-limit|floating-limit') { $expectedCode='invalid_argument'; $expectedField='limit' }
      elseif ($probe.request.label -match 'alias-conflict') { $expectedCode='invalid_argument'; $expectedField=if ($probe.request.operation -eq 'code_flow') { 'entry_point' } else { 'symbol' } }
      elseif ($probe.request.label -match 'unknown-source') { $expectedCode='source_not_found'; $expectedField='source_id' }
      elseif ($probe.request.label -match 'denied-source|write-does-not-authorize') { $expectedCode='source_not_allowed'; $expectedField='source_id' }
      elseif ($probe.request.label -match 'remote-denied') { $expectedCode='write_denied'; $expectedField='operation' }
      elseif ($probe.request.label -match 'non-object') { $expectedCode='invalid_argument'; $expectedField='arguments' }
      if ($probe.request.operation -eq 'tools/list') {
        Require (-not $expectedError) 'real tools/list was unexpectedly classified as an error'
        [void](Assert-N22ToolsList $probe.response "real $($probe.request.label)")
        $parsed = [pscustomobject]@{
          payload = $probe.response.result
          content_count = 0
          structured_block_index = -1
          single_structured_error_block = $false
        }
      } elseif ($expectedError) {
        $parsed = Get-N22StructuredMcpPayload $probe $true $expectedCode $expectedField
        ++$errorCount
        if ($parsed.single_structured_error_block) { ++$singleErrorCount }
      } else {
        $parsed = Get-N22StructuredMcpPayload $probe $false
        if ($probe.request.label -match ':success$') {
          $rows = Assert-N22HitRows $parsed.payload 'team_a' "real $($probe.request.label)"
          Require ($rows.Count -gt 0) "real $($probe.request.label) returned no rows"
          if ($probe.request.operation -eq 'code_callees') {
            Require (($rows | ForEach-Object { $_.kind }) -contains 'callee:Alpha') "real callees success lacks Alpha"
            Require (($rows | ForEach-Object { $_.kind }) -contains 'callee:Beta') "real callees success lacks Beta"
          } elseif ($probe.request.operation -eq 'code_flow') {
            foreach ($kind in @('flow:d1:Alpha', 'flow:d1:Beta', 'flow:d2:Gamma')) { Require (($rows | ForEach-Object { $_.kind }) -contains $kind) "real flow success lacks $kind" }
          } else {
            Require (($rows | ForEach-Object { $_.kind }) -contains 'def') "real blast success lacks a definition hit"
          }
          $successByOperation[$probe.request.operation] = $rows.Count
        } elseif ($probe.request.label -match ':empty$') {
          [void](Assert-N22HitRows $parsed.payload 'team_a' "real $($probe.request.label)" 0)
          $emptyByOperation[$probe.request.operation] = 0
        } elseif ($probe.request.label -match ':clamp$') {
          [void](Assert-N22HitRows $parsed.payload 'team_a' "real $($probe.request.label)" 200)
          $clampByOperation[$probe.request.operation] = 200
        } elseif ($probe.request.label -match ':ambient-default$') {
          $rows = Assert-N22HitRows $parsed.payload 'default' "real $($probe.request.label)"
          Require ($rows.Count -gt 0) "real $($probe.request.label) returned no rows"
        } elseif ($probe.request.label -ceq 'mcp:real:cache:remote-allowed') {
          Require-ExactJsonPropertyNames $parsed.payload @('cleared', 'stateless') 'real cache allow-write payload'
          Require-JsonIntegerValue $parsed.payload.cleared 0 'real cache allow-write cleared'
          Require-JsonBooleanValue $parsed.payload.stateless $true 'real cache allow-write stateless'
        }
      }
      $evidenceRows.Add([pscustomobject][ordered]@{
          label = $probe.request.label
          operation = $probe.request.operation
          id = $probe.request.id
          allow_write = [bool]($probe.request.label -match 'write-does-not-authorize|cache:remote-allowed|cache:unexpected|cache:non-object')
          request_sha256 = Text-Hash $probe.request.body
          response_sha256 = Text-Hash $probe.response_text
          content_count = $parsed.content_count
          structured_block_index = $parsed.structured_block_index
          is_error = $expectedError
          payload = $parsed.payload
        })
    }
    foreach ($operation in @('code_callees', 'code_flow', 'code_blast')) {
      Require ($successByOperation.ContainsKey($operation)) "real MCP lacks $operation success evidence"
      Require ($emptyByOperation.ContainsKey($operation)) "real MCP lacks $operation empty evidence"
      Require ($clampByOperation.ContainsKey($operation)) "real MCP lacks $operation clamp evidence"
    }
    Require ($errorCount -ge 30) 'real MCP error matrix is too small'
    Require ($singleErrorCount -eq $errorCount) 'real MCP error responses are not single structured blocks'
    Require ((File-Hash $Qbrain) -ceq $ProductionBinaryHash) 'production binary changed during real MCP probes'
    $afterProbeTree = Get-DisposableTreeFingerprint $isolatedQbrainTree -PersistentDataTree
    Require ($afterProbeTree -ceq $fixtureTree) 'real N22 MCP probes changed the persistent isolated Qbrain data-tree fingerprint'

    $doctorCapture = Invoke-CapturedProcess -FilePath $Qbrain -Arguments "doctor --brain $brainId --json" `
        -ProcessTimeoutSeconds 120 -WorkingDirectory $Root -EnvironmentOverrides @{ LOCALAPPDATA=$isolatedLocalAppData; QBRAIN_SOURCE='n22_ambient_forbidden'; QBRAIN_MCP_ALLOW_WRITE='0' } `
        -RemoveEnvironmentVariables @('QBRAIN_BRAIN', 'QBRAIN_MCP_TOKEN')
    Require ($doctorCapture.exit_code -eq 0) 'real final-binary doctor failed'
    Require ([string]::IsNullOrWhiteSpace($doctorCapture.stderr)) 'real final-binary doctor wrote stderr'
    $doctorRaw = $doctorCapture.stdout.Trim()
    Assert-SafeEvidenceText $doctorRaw 'real final-binary doctor output'
    $doctorPayload = ConvertFrom-StrictJsonText $doctorRaw 'real final-binary doctor JSON'
    [void](Assert-N22DoctorPayload $doctorPayload 'real final-binary doctor')
    Require ((File-Hash $Qbrain) -ceq $ProductionBinaryHash) 'production binary changed during real doctor'
    $doctorTree = Get-DisposableTreeFingerprint $isolatedQbrainTree -PersistentDataTree
    Require ($doctorTree -ceq $fixtureTree) 'real final-binary doctor changed the persistent isolated Qbrain data-tree fingerprint'
    $doctorEvidence = [pscustomobject][ordered]@{
      command = "build\cl\qbrain.exe doctor --brain $brainId --json"
      working_directory = $Root.Replace('\', '/')
      exit_code = $doctorCapture.exit_code
      stderr_empty = [string]::IsNullOrWhiteSpace($doctorCapture.stderr)
      child_environment_policy = $ChildEnvironmentPolicy
      publication_nonce = $PublicationNonce
      response_sha256 = Text-Hash $doctorRaw
      schema_version = [int]$doctorPayload.schema_version
      ok = [bool]$doctorPayload.ok
      payload = $doctorPayload
      persistent_tree_fingerprint_scope = $PersistentTreeFingerprintScope
      isolated_tree_fixture_sha256 = $fixtureTree
      isolated_tree_after_probes_sha256 = $afterProbeTree
      isolated_tree_after_doctor_sha256 = $doctorTree
      isolated_tree_after_cleanup_sha256 = ''
      binary_sha256 = $ProductionBinaryHash
    }
    $mcpEvidence = [pscustomobject][ordered]@{
      transport = 'stdio-ndjson'
      brain_id = $brainId
      isolated_localappdata = $true
      child_environment_policy = $ChildEnvironmentPolicy
      publication_nonce = $PublicationNonce
      persistent_tree_fingerprint_scope = $PersistentTreeFingerprintScope
      fixture_tree_sha256 = $fixtureTree
      tree_after_probe_sha256 = $afterProbeTree
      request_count = $evidenceRows.Count
      error_count = $errorCount
      single_structured_error_block_count = $singleErrorCount
      read_success_operations = @($successByOperation.Keys | Sort-Object)
      empty_operations = @($emptyByOperation.Keys | Sort-Object)
      clamp_operations = @($clampByOperation.Keys | Sort-Object)
      cache_remote_default_denied = $true
      cache_explicit_allow_success = $true
      read_write_does_not_authorize = $true
      tools_list_exact = $true
      ambient_source_excluded = $true
      binary_sha256 = $ProductionBinaryHash
      sessions = @(
        [pscustomobject]@{ label=$readSession.label; command=$readSession.command; request_count=$readSession.request_count; stdout_sha256=$readSession.stdout_sha256; stderr_sha256=$readSession.stderr_sha256; startup_marker=$readSession.startup_marker; shutdown_marker=$readSession.shutdown_marker; write_mode_marker=$readSession.write_mode_marker },
        [pscustomobject]@{ label=$allowSession.label; command=$allowSession.command; request_count=$allowSession.request_count; stdout_sha256=$allowSession.stdout_sha256; stderr_sha256=$allowSession.stderr_sha256; startup_marker=$allowSession.startup_marker; shutdown_marker=$allowSession.shutdown_marker; write_mode_marker=$allowSession.write_mode_marker }
      )
      probes = $evidenceRows.ToArray()
    }
  } catch {
    $failure = $_
  } finally {
    if (Test-Path -LiteralPath $sandbox) {
      Remove-SafeTemporaryDirectory $sandbox 'qbrain_n22_stdio_'
    }
  }
  $isolatedTreeAfterCleanup = Get-DisposableTreeFingerprint $isolatedQbrainTree -PersistentDataTree
  if ($null -ne $failure) { throw $failure }
  Require (-not (Test-Path -LiteralPath $sandbox)) 'real N22 MCP sandbox was not removed'
  Require ($isolatedTreeAfterCleanup -ceq 'absent') 'real N22 MCP sandbox tree remained after cleanup'
  $doctorEvidence.isolated_tree_after_cleanup_sha256 = $isolatedTreeAfterCleanup
  [pscustomobject][ordered]@{
    mcp = $mcpEvidence
    doctor = $doctorEvidence
    publication_nonce = $PublicationNonce
    persistent_tree_fingerprint_scope = $PersistentTreeFingerprintScope
    isolated_localappdata_tree_before_sha256 = $isolatedTreeBefore
    isolated_localappdata_tree_after_cleanup_sha256 = $isolatedTreeAfterCleanup
    sandbox_removed = $true
    schema_version = [int]$doctorEvidence.schema_version
  }
}

function Invoke-N22ParserSelfTest {
  Require ((Text-Hash 'n22-parser-self-test') -match '^[0-9a-f]{64}$') 'hash helper failed'
  Assert-SafeEvidenceText 'native Windows C++20 parser self-test' 'parser self-test'
  Require ((Resolve-WorkspacePath $Root).Equals((Get-CanonicalFilesystemPath $Root), [StringComparison]::OrdinalIgnoreCase)) `
      'workspace root path is not accepted by the workspace resolver'
  Require ((Relative-Path $Root) -ceq '.') 'workspace root relative path is not canonical'
  $baselineFixture = New-N19StorageBaselineEvidence
  Require (@($baselineFixture.entries).Count -eq 4) 'N19 storage baseline entry count changed'
  Require ([string]$baselineFixture.binding_sha256 -match '^[0-9a-f]{64}$') 'N19 storage baseline binding is invalid'
  $productionSliceFixture = New-N22ProductionSliceEvidence
  Require (@($productionSliceFixture.entries).Count -eq 4) 'N22 production-slice entry count changed'
  Require ([string]$productionSliceFixture.binding_sha256 -match '^[0-9a-f]{64}$') 'N22 production-slice binding is invalid'

  $fingerprintSandbox = Join-Path ([IO.Path]::GetTempPath()) ('qbrain_n22_fingerprint_' + [guid]::NewGuid().ToString('N'))
  $fingerprintNested = Join-Path $fingerprintSandbox 'nested'
  $fingerprintFile = Join-Path $fingerprintSandbox 'binding.txt'
  try {
    New-Item -ItemType Directory -Force -Path $fingerprintSandbox | Out-Null
    New-Item -ItemType Directory -Force -Path $fingerprintNested | Out-Null
    [IO.File]::WriteAllText($fingerprintFile, 'AAAA', $Utf8NoBom)
    $fingerprintBaseline = Get-DisposableTreeFingerprint $fingerprintSandbox -PersistentDataTree

    $rootInitialTime = (Get-Item -LiteralPath $fingerprintSandbox -Force).LastWriteTimeUtc
    [IO.Directory]::SetLastWriteTimeUtc($fingerprintSandbox, $rootInitialTime.AddHours(1))
    $rootObservedTime = (Get-Item -LiteralPath $fingerprintSandbox -Force).LastWriteTimeUtc
    Require ($rootObservedTime.Ticks -ne $rootInitialTime.Ticks) 'fingerprint self-test could not change root directory time'
    Require ((Get-DisposableTreeFingerprint $fingerprintSandbox -PersistentDataTree) -ceq $fingerprintBaseline) `
        'persistent fingerprint changed for root directory time only'

    $nestedInitialTime = (Get-Item -LiteralPath $fingerprintNested -Force).LastWriteTimeUtc
    [IO.Directory]::SetLastWriteTimeUtc($fingerprintNested, $nestedInitialTime.AddHours(1))
    $nestedObservedTime = (Get-Item -LiteralPath $fingerprintNested -Force).LastWriteTimeUtc
    Require ($nestedObservedTime.Ticks -ne $nestedInitialTime.Ticks) 'fingerprint self-test could not change nested directory time'
    Require ((Get-DisposableTreeFingerprint $fingerprintSandbox -PersistentDataTree) -ceq $fingerprintBaseline) `
        'persistent fingerprint changed for nested directory time only'

    $nestedItem = Get-Item -LiteralPath $fingerprintNested -Force
    $nestedAttributes = $nestedItem.Attributes
    Require (($nestedAttributes -band [IO.FileAttributes]::Hidden) -eq 0) 'fingerprint self-test nested directory is unexpectedly hidden'
    $nestedItem.Attributes = $nestedAttributes -bor [IO.FileAttributes]::Hidden
    Require (((Get-Item -LiteralPath $fingerprintNested -Force).Attributes -band [IO.FileAttributes]::Hidden) -ne 0) `
        'fingerprint self-test could not change nested directory attributes'
    Require ((Get-DisposableTreeFingerprint $fingerprintSandbox -PersistentDataTree) -cne $fingerprintBaseline) `
        'persistent fingerprint missed a directory attribute change'
    (Get-Item -LiteralPath $fingerprintNested -Force).Attributes = $nestedAttributes
    Require ((Get-DisposableTreeFingerprint $fingerprintSandbox -PersistentDataTree) -ceq $fingerprintBaseline) `
        'persistent fingerprint did not restore after directory attribute reset'

    $fileInitialTime = (Get-Item -LiteralPath $fingerprintFile -Force).LastWriteTimeUtc
    [IO.File]::SetLastWriteTimeUtc($fingerprintFile, $fileInitialTime.AddHours(1))
    $fileObservedTime = (Get-Item -LiteralPath $fingerprintFile -Force).LastWriteTimeUtc
    Require ($fileObservedTime.Ticks -ne $fileInitialTime.Ticks) 'fingerprint self-test could not change file time'
    Require ((Get-DisposableTreeFingerprint $fingerprintSandbox -PersistentDataTree) -cne $fingerprintBaseline) `
        'persistent fingerprint missed a file time change'
    [IO.File]::SetLastWriteTimeUtc($fingerprintFile, $fileInitialTime)
    Require ((Get-DisposableTreeFingerprint $fingerprintSandbox -PersistentDataTree) -ceq $fingerprintBaseline) `
        'persistent fingerprint did not restore after file time reset'

    $fileItem = Get-Item -LiteralPath $fingerprintFile -Force
    $fileAttributes = $fileItem.Attributes
    Require (($fileAttributes -band [IO.FileAttributes]::Hidden) -eq 0) 'fingerprint self-test file is unexpectedly hidden'
    $fileItem.Attributes = $fileAttributes -bor [IO.FileAttributes]::Hidden
    Require (((Get-Item -LiteralPath $fingerprintFile -Force).Attributes -band [IO.FileAttributes]::Hidden) -ne 0) `
        'fingerprint self-test could not change file attributes'
    Require ((Get-DisposableTreeFingerprint $fingerprintSandbox -PersistentDataTree) -cne $fingerprintBaseline) `
        'persistent fingerprint missed a file attribute change'
    (Get-Item -LiteralPath $fingerprintFile -Force).Attributes = $fileAttributes
    Require ((Get-DisposableTreeFingerprint $fingerprintSandbox -PersistentDataTree) -ceq $fingerprintBaseline) `
        'persistent fingerprint did not restore after file attribute reset'

    [IO.File]::WriteAllText($fingerprintFile, 'BBBB', $Utf8NoBom)
    [IO.File]::SetLastWriteTimeUtc($fingerprintFile, $fileInitialTime)
    Require ([int64](Get-Item -LiteralPath $fingerprintFile -Force).Length -eq 4) 'fingerprint self-test content length changed unexpectedly'
    $contentFingerprint = Get-DisposableTreeFingerprint $fingerprintSandbox -PersistentDataTree
    Require ($contentFingerprint -cne $fingerprintBaseline) 'persistent fingerprint missed same-length file content change'

    $extraFile = Join-Path $fingerprintSandbox 'extra.txt'
    [IO.File]::WriteAllText($extraFile, 'CCCC', $Utf8NoBom)
    Require ((Get-DisposableTreeFingerprint $fingerprintSandbox -PersistentDataTree) -cne $contentFingerprint) `
        'persistent fingerprint missed file inventory addition'
    Remove-Item -LiteralPath $extraFile -Force
    Require ((Get-DisposableTreeFingerprint $fingerprintSandbox -PersistentDataTree) -ceq $contentFingerprint) `
        'persistent fingerprint did not restore after file inventory removal'
  } finally {
    if (Test-Path -LiteralPath $fingerprintSandbox) {
      Remove-SafeTemporaryDirectory $fingerprintSandbox 'qbrain_n22_fingerprint_'
    }
  }

  $emptyArray = ConvertFrom-StrictJsonText '[]' 'parser empty array'
  Require ($emptyArray -is [Array] -and @($emptyArray).Count -eq 0) 'empty JSON arrays are not preserved'
  $singleArray = ConvertFrom-StrictJsonText '[{"ok":true}]' 'parser single array'
  Require ($singleArray -is [Array] -and @($singleArray).Count -eq 1) 'single-element JSON arrays are not preserved'
  Require ($singleArray[0] -is [pscustomobject] -and
      @($singleArray[0].PSObject.Properties.Name) -ceq @('ok') -and
      $singleArray[0].ok -eq $true) 'single-element JSON array rows are nested or malformed'
  Assert-Throws { ConvertFrom-StrictJsonText '{"x":1,"x":2}' 'parser duplicate key' } 'duplicate JSON keys'
  Assert-Throws { ConvertFrom-StrictJsonText '{"x":1} trailing' 'parser trailing data' } 'trailing JSON data'
  Assert-Throws { ConvertFrom-StrictJsonText ([string][char]0xFEFF + '{}') 'parser BOM' } 'JSON BOM'
  Assert-Throws { ConvertFrom-StrictJsonText ([string][char]0 + '{}') 'parser NUL' } 'JSON NUL'

  $descriptionFixtures = [ordered]@{
    code_callees = 'Stateless bounded source-text heuristic: one-hop source-scoped brace-body callee scan with exact lexical identifier matching; no AST, tree-sitter, or compiler index; no overload/type resolution; no persisted call edges/cache; not recursive/transitive upstream parity'
    code_flow = 'Stateless bounded source-text heuristic: deterministic breadth-first traversal over source-scoped brace-body callees with exact lexical identifier matching; no AST, tree-sitter, or compiler index; no overload/type resolution; no persisted call edges/cache; not recursive/transitive upstream parity; no terminal/sink classification'
    code_blast = 'Stateless bounded source-text heuristic: bounded one-hop source-scoped def/ref/caller/callee heuristic subset using brace-body callees and exact lexical identifier matching; no AST, tree-sitter, or compiler index; no overload/type resolution; no persisted call edges/cache; not recursive/transitive upstream parity'
    code_traversal_cache_clear = 'Guarded stateless compatibility no-op; clears zero rows; no persisted traversal cache, no cache table, and no schema migration'
  }
  foreach ($descriptionName in $descriptionFixtures.Keys) {
    Assert-N22DescriptionContract $descriptionName ([string]$descriptionFixtures[$descriptionName]) "parser description valid $descriptionName"
  }
  Assert-N22DescriptionContract 'code_callees' $descriptionFixtures.code_callees 'parser description accepts not an AST'
  Assert-N22BuildClosurePath (Join-Path $Root 'tests\test_n22.cpp')
  $syntheticCoordinatorPath = Join-Path $Root ('tests\test_' + 'n30.cpp')
  $syntheticLaterPath = Join-Path $Root 'tests\test_n23.cpp'
  $syntheticLargeNodePath = Join-Path $Root 'src\qbrain\future-N100.cpp'
  Assert-ThrowsMatching {
    Get-N22ScopedFileEntry $syntheticCoordinatorPath 'parser synthetic scoped input'
  } 'excluded coordinator token' 'coordinator path guard before file access'
  Assert-ThrowsMatching {
    Assert-N22BuildClosurePath $syntheticCoordinatorPath
  } 'excluded coordinator token' 'coordinator build-closure path guard before file access'
  Assert-ThrowsMatching {
    Assert-NoExcludedManifestPath @([pscustomobject]@{ path='tests/test_n030.cpp' })
  } 'excluded coordinator token' 'zero-padded coordinator manifest path guard'
  Assert-ThrowsMatching {
    Get-N22ScopedFileEntry $syntheticLaterPath 'parser synthetic scoped input'
  } 'node-token scope policy' 'later-node underscore path guard before file access'
  Assert-ThrowsMatching {
    Get-N22ScopedFileEntry $syntheticLargeNodePath 'parser synthetic scoped input'
  } 'node-token scope policy' 'large later-node path guard before file access'
  Assert-ThrowsMatching {
    Assert-NoFutureNodeReference ('synthetic future token ' + 'N30') 'parser synthetic content'
  } 'excluded coordinator token' 'future-node in-memory content guard'
  Require ('team_a' -match $ExpectedN22SourceIdPattern) 'source-id schema pattern rejects a valid source'
  foreach ($reservedSource in @('CON', 'prn', 'Aux', 'nul', 'COM1', 'lpt9')) {
    Require ($reservedSource -notmatch $ExpectedN22SourceIdPattern) "source-id schema pattern accepts $reservedSource"
  }
  $affirmativeDescriptionFixtures = @(
    @{ name = 'code_callees'; description = "$($descriptionFixtures.code_callees); this is an AST" },
    @{ name = 'code_callees'; description = "$($descriptionFixtures.code_callees); stores persisted call edges" },
    @{ name = 'code_flow'; description = "$($descriptionFixtures.code_flow); uses tree-sitter/compiler index" },
    @{ name = 'code_flow'; description = "$($descriptionFixtures.code_flow); provides terminal-node classification" },
    @{ name = 'code_blast'; description = "$($descriptionFixtures.code_blast); supports recursive upstream parity" },
    @{ name = 'code_traversal_cache_clear'; description = "$($descriptionFixtures.code_traversal_cache_clear); persists a real traversal cache" }
  )
  foreach ($fixture in $affirmativeDescriptionFixtures) {
    Assert-Throws {
      Assert-N22DescriptionContract $fixture.name $fixture.description "parser description affirmative $($fixture.name)"
    } "affirmative unsupported description $($fixture.name)"
  }

  $emptyArgumentsRequest = New-N22RpcRequest 1 'code_callees' ([object[]]@()) 'parser:empty-arguments'
  $emptyArgumentsJson = ConvertFrom-StrictJsonText $emptyArgumentsRequest.body 'parser empty arguments request'
  Require ($emptyArgumentsJson.params.arguments -is [Array] -and
      @($emptyArgumentsJson.params.arguments).Count -eq 0) 'empty MCP argument arrays are not serialized as []'
  $singleArgumentsRequest = New-N22RpcRequest 2 'code_callees' ([object[]]@('one')) 'parser:single-arguments'
  $singleArgumentsJson = ConvertFrom-StrictJsonText $singleArgumentsRequest.body 'parser single arguments request'
  Require ($singleArgumentsJson.params.arguments -is [Array] -and
      @($singleArgumentsJson.params.arguments).Count -eq 1) 'single-element MCP argument arrays are not preserved'

  $errorProbe = [pscustomobject][ordered]@{
    request = [pscustomobject][ordered]@{ label = 'parser:structured-error' }
    response = [pscustomobject][ordered]@{
      result = [pscustomobject][ordered]@{
        content = [object[]]@([pscustomobject][ordered]@{
            type = 'text'
            text = '{"error":{"code":"invalid_argument","field":"symbol","message":"bounded"}}'
          })
        isError = $true
      }
    }
  }
  $errorPayload = Get-N22StructuredMcpPayload $errorProbe $true 'invalid_argument' 'symbol'
  Require $errorPayload.single_structured_error_block 'single structured error block parser failed'

  $doctorPayload = [pscustomobject][ordered]@{
    ok = $true
    overall = 'OK'
    schema_version = 12
    stats = [pscustomobject][ordered]@{ pages = 0; chunks = 0; links = 0; embedded_chunks = 0 }
    checks = [object[]]@(
      [pscustomobject][ordered]@{ name = 'schema'; status = 'PASS' },
      [pscustomobject][ordered]@{ name = 'pages'; status = 'PASS' },
      [pscustomobject][ordered]@{ name = 'chunks'; status = 'PASS' },
      [pscustomobject][ordered]@{ name = 'links'; status = 'PASS' }
    )
    notes = [object[]]@()
  }
  Require (Assert-N22DoctorPayload $doctorPayload 'parser doctor') 'doctor schema parser failed'

  $labels = @(Get-ExpectedN22SnapshotLabels)
  Require ($labels.Count -eq $ExpectedN22SnapshotLabelCount) 'parser self-test label count changed'
  Require (@($labels | Sort-Object -Unique).Count -eq $ExpectedN22SnapshotLabelCount) 'parser self-test labels are not unique'
  Require ((Text-Hash ($labels -join "`n")) -ceq $ExpectedN22SnapshotLabelHash) 'parser self-test label hash changed'
  $selectedHash = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
  $decoyHash = 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
  $matrix = New-Object System.Collections.Generic.List[string]
  [void]$matrix.Add((Get-ExpectedN22Summary $labels.Count $selectedHash $decoyHash))
  for ($index = 0; $index -lt $labels.Count; ++$index) {
    [void]$matrix.Add(("[INFO] n22 snapshot_call={0} label={1} selected_before_sha256={2} selected_after_sha256={2} decoy_before_sha256={3} decoy_after_sha256={3}" -f ($index + 1), $labels[$index], $selectedHash, $decoyHash))
  }
  $parsed = Assert-N22Evidence $matrix.ToArray() 'parser exact marker matrix'
  Require ($parsed.snapshot_count -eq $ExpectedN22SnapshotLabelCount -and $parsed.exact_label_order) 'exact marker parser did not preserve the reviewed matrix'
  $malformed = [string[]]$matrix.ToArray()
  $malformed[2] = $malformed[2] + ' trailing'
  Assert-Throws { Assert-N22Evidence $malformed 'parser malformed marker matrix' } 'malformed N22 evidence'
  Write-Host "N22_PARSER_SELFTEST_OK labels=$($labels.Count)"
}

try {
  if ($ParserSelfTest) {
    Invoke-N22ParserSelfTest
    exit 0
  }

  Enter-N22EvidenceMutex

  if ($Prepare) {
    New-Preparation
    exit 0
  }

  $manifest = Read-PrebuildManifest
  $preparation = Assert-PreparationCurrent $manifest
  Require ([bool]$RunBuilds) 'formal verification requires -RunBuilds in this verifier process'
  # Keep every frozen input read-only through both suite runs, the disposable
  # MCP/doctor probes, and the final publication-chain revalidation.
  $script:FrozenInputHandles = Open-FrozenInputHandles $preparation.inputs
  Invoke-OfficialBuilds $preparation.registered_tests.Count $preparation.platform `
      $manifest $preparation.publication_nonce
  $build = Assert-BuildLogs $preparation
  $run2 = Invoke-FullSuiteRun2 $preparation.registered_tests $build.test_binary_sha256 `
      ([string]$preparation.platform.os) $preparation.publication_nonce
  $equivalence = Assert-TwoRunEquivalence $build.run1 $run2.results
  $real = Invoke-N22RealMcpAndSchema $preparation $build.production_binary_sha256 $preparation.publication_nonce
  # Re-bind every frozen input after the disposable runtime probes and before
  # publishing final evidence. The probes must not alter repository inputs.
  $finalPreparation = Assert-PreparationCurrent $manifest
  $schemaBaseline = $finalPreparation.schema_baseline
  $productionSlice = $finalPreparation.production_slice

  $n22 = $build.run1.n22
  $realMcp = $real.mcp
  $doctor = $real.doctor
  $schemaVersion = [int]$real.schema_version
  $isolatedTreeBefore = [string]$real.isolated_localappdata_tree_before_sha256
  $isolatedTreeAfterCleanup = [string]$real.isolated_localappdata_tree_after_cleanup_sha256
  Require ($schemaVersion -eq 12) 'real doctor schema version is not v12'
  Require ([string]$real.publication_nonce -ceq [string]$preparation.publication_nonce) 'real MCP evidence is not bound to the preparation nonce'
  Require ([string]$real.persistent_tree_fingerprint_scope -ceq $PersistentTreeFingerprintScope) `
      'real MCP evidence persistent-tree fingerprint scope is invalid'
  Require ([string]$doctor.persistent_tree_fingerprint_scope -ceq $PersistentTreeFingerprintScope) `
      'doctor evidence persistent-tree fingerprint scope is invalid'
  Require ($isolatedTreeBefore -match '^(?:absent|[0-9a-f]{64})$') 'real MCP disposable-tree before fingerprint is invalid'
  Require ($isolatedTreeAfterCleanup -ceq 'absent') 'real MCP disposable tree was not removed after cleanup'
  $runtimeSchemaLabels = @(
    'schema:fresh-v12',
    'schema:populated-reopen-v12',
    'schema:final-v12'
  )
  foreach ($runtimeSchemaLabel in $runtimeSchemaLabels) {
    Require ([int]$n22.label_counts[$runtimeSchemaLabel] -eq 1) "runtime schema snapshot is not exact: $runtimeSchemaLabel"
  }
  $realMcpJson = ($realMcp | ConvertTo-Json -Depth 30) + [Environment]::NewLine
  $doctorJson = ($doctor | ConvertTo-Json -Depth 20) + [Environment]::NewLine
  Assert-SafeEvidenceText $realMcpJson 'real MCP evidence JSON'
  Assert-SafeEvidenceText $doctorJson 'doctor evidence JSON'
  Write-Utf8Text $RealMcpEvidencePath $realMcpJson
  Write-Utf8Text $DoctorEvidencePath $doctorJson
  Write-Utf8Lines $FocusedEvidencePath @(
    'node=N22',
    "publication_nonce=$($preparation.publication_nonce)",
    "summary=$($n22.summary)",
    "snapshot_call_count=$($n22.snapshot_count)",
    "selected_snapshot_sha256=$($n22.selected_snapshot_sha256)",
    "decoy_snapshot_sha256=$($n22.decoy_snapshot_sha256)",
    "normalized_marker_sha256=$($n22.normalized_sha256)"
  )
  Write-Utf8Lines $SnapshotEvidencePath $n22.snapshot_lines
  Write-Utf8Text $RegistryMcpEvidencePath (($n22.label_counts | ConvertTo-Json -Depth 6) + [Environment]::NewLine)
  $schemaEvidenceLines = New-Object System.Collections.Generic.List[string]
  $schemaEvidenceLines.Add("publication_nonce=$($preparation.publication_nonce)")
  $schemaEvidenceLines.Add("schema_version=$schemaVersion")
  $schemaEvidenceLines.Add("doctor_ok=$(([string]$doctor.ok).ToLowerInvariant())")
  $schemaEvidenceLines.Add("n19_storage_baseline_binding_sha256=$($schemaBaseline.binding_sha256)")
  $schemaEvidenceLines.Add("n19_storage_baseline_file_count=$(@($schemaBaseline.entries).Count)")
  for ($baselineIndex = 0; $baselineIndex -lt @($schemaBaseline.entries).Count; ++$baselineIndex) {
    $entryNumber = $baselineIndex + 1
    $baselineEntry = @($schemaBaseline.entries)[$baselineIndex]
    $schemaEvidenceLines.Add("n19_storage_baseline_file_$($entryNumber)_path=$($baselineEntry.path)")
    $schemaEvidenceLines.Add("n19_storage_baseline_file_$($entryNumber)_sha256=$($baselineEntry.sha256)")
  }
  $schemaEvidenceLines.Add("runtime_schema_snapshot_count=$($runtimeSchemaLabels.Count)")
  $schemaEvidenceLines.Add("runtime_schema_snapshot_labels=$($runtimeSchemaLabels -join ',')")
  $schemaEvidenceLines.Add('schema_integrity_basis=exact_n19_storage_hash_baseline_plus_runtime_schema_v12_snapshots')
  $schemaEvidenceLines.Add("persistent_tree_fingerprint_scope=$PersistentTreeFingerprintScope")
  $schemaEvidenceLines.Add("isolated_localappdata_tree_before_sha256=$isolatedTreeBefore")
  $schemaEvidenceLines.Add("isolated_localappdata_tree_after_cleanup_sha256=$isolatedTreeAfterCleanup")
  $schemaEvidenceLines.Add("isolated_tree_fixture_sha256=$($doctor.isolated_tree_fixture_sha256)")
  $schemaEvidenceLines.Add("isolated_tree_after_probes_sha256=$($doctor.isolated_tree_after_probes_sha256)")
  $schemaEvidenceLines.Add("isolated_tree_after_doctor_sha256=$($doctor.isolated_tree_after_doctor_sha256)")
  Assert-SafeEvidenceText ($schemaEvidenceLines.ToArray() -join "`n") 'schema evidence'
  Write-Utf8Lines $SchemaEvidencePath $schemaEvidenceLines.ToArray()
  Write-Utf8Lines $PlatformEvidencePath @(
    "os=$($preparation.platform.os)",
    "windows_build=$($preparation.platform.os)",
    'architecture=x64',
    'language_mode=/std:c++20',
    "child_environment_policy=$ChildEnvironmentPolicy",
    "publication_nonce=$($preparation.publication_nonce)",
    "compiler=$($preparation.platform.compiler)"
  )
  Write-Utf8Lines $SafetyEvidencePath @(
    "publication_nonce=$($preparation.publication_nonce)",
    "isolated_localappdata_tree_before_sha256=$isolatedTreeBefore",
    "isolated_localappdata_tree_after_cleanup_sha256=$isolatedTreeAfterCleanup",
    "persistent_tree_fingerprint_scope=$PersistentTreeFingerprintScope",
    'disposable_runtime_tree_integrity=collected',
    "n19_storage_baseline_binding_sha256=$($schemaBaseline.binding_sha256)",
    "n22_production_slice_binding_sha256=$($productionSlice.binding_sha256)",
    'protected_repo_config_rechecked=true',
    'git_repository_facts_rechecked=true',
    'production_data_access_telemetry=not-collected'
  )

  $report = @(
    '# N22 Verification Report',
    '',
    'This report records native evidence only; it is not a Claude Code audit verdict.',
    '',
    "- Registered tests: $($build.run1.registered); first run $($build.run1.passed) PASS / $($build.run1.failed) FAIL; second run $($run2.results.passed) PASS / $($run2.results.failed) FAIL.",
    "- Windows host: $($preparation.platform.os); compiler: $($preparation.platform.compiler).",
    "- Production binary SHA-256: ``$($build.production_binary_sha256)``.",
    "- Test binary SHA-256: ``$($build.test_binary_sha256)``.",
    "- N22 snapshot rows: $($n22.snapshot_count); normalized marker SHA-256: ``$($n22.normalized_sha256)``.",
    "- Two-run result stream SHA-256: ``$($equivalence.result_stream_sha256)``.",
    "- Publication nonce: ``$($preparation.publication_nonce)``.",
    "- Real final-binary doctor reported schema v$schemaVersion and ok=$($doctor.ok).",
    "- Exact N19 schema-v12 storage inputs remained byte-identical across $(@($schemaBaseline.entries).Count) files; binding SHA-256 $($schemaBaseline.binding_sha256).",
    '- Runtime schema evidence contains exactly one fresh-v12, populated-reopen-v12, and final-v12 snapshot marker.',
    "- The N22 production slice remained closed and byte-identical after runtime probes; binding SHA-256 $($productionSlice.binding_sha256).",
    "- Real stdio MCP probes covered $($realMcp.request_count) requests, $($realMcp.error_count) structured errors, and $($realMcp.single_structured_error_block_count) single-error blocks.",
    "- Disposable Qbrain persistent-data tree SHA-256 before setup / after cleanup: ``$isolatedTreeBefore`` / ``$isolatedTreeAfterCleanup``; fixture before/after probes: ``$($realMcp.fixture_tree_sha256)`` / ``$($realMcp.tree_after_probe_sha256)``; doctor after-tree ``$($doctor.isolated_tree_after_doctor_sha256)``.",
    '- That persistent-data fingerprint binds directory paths/attributes and every file path, attributes, length, SHA-256 content, and LastWriteTimeUtc. It intentionally excludes directory LastWriteTimeUtc because SQLite WAL read lifecycles can change that OS metadata without a surviving Qbrain data or logical-database change.',
    '- Selected/decoy logical snapshots and the scoped persistent-data tree fingerprint matched across N22 paths; the stateless cache-clear compatibility path reported zero rows.',
    '- Protected repository configuration hashes and Git repository facts were rechecked against the frozen preparation. The verifier did not traverse production LOCALAPPDATA; it collected no production filesystem-access telemetry and does not claim global push telemetry. It did not write node status, ledger state, or an audit verdict.',
    '- A fresh node-specific Claude Code outcome hard audit remains blocking before status or ledger reconciliation.'
  )
  Assert-SafeEvidenceText ($report -join "`n") 'final verification report'
  Write-Utf8Lines $ReportPath $report

  # Re-check the still-pending anchor and every frozen input immediately before
  # turning the evidence manifest into the final publication record.
  $publicationPreparation = Assert-PreparationCurrent $manifest
  Require ([string]$publicationPreparation.publication_nonce -ceq [string]$preparation.publication_nonce) `
      'publication preparation nonce changed before final evidence'

  $outputPaths = @(
    $ProductionBuildLog, $TestBuildLog, $FullSuiteEvidencePath, $FocusedEvidencePath,
    $SnapshotEvidencePath, $RegistryMcpEvidencePath, $RealMcpEvidencePath,
    $DoctorEvidencePath, $SchemaEvidencePath, $PlatformEvidencePath,
    $SafetyEvidencePath, $ReportPath, $PrebuildManifestPath
  ) | ForEach-Object { Relative-Path (Resolve-WorkspacePath $_) }
  Assert-NoExcludedManifestPath @($outputPaths | ForEach-Object {
      [pscustomobject]@{ path = $_ }
    })

  $evidence = [pscustomobject][ordered]@{
    format_version = 1
    node = 'N22'
    state = 'verified-pending-claude-outcome-audit'
    verified_utc = [DateTimeOffset]::UtcNow.ToString('o')
    publication_nonce = [string]$publicationPreparation.publication_nonce
    approved_plan_sha256 = [string]$preparation.governance.approved_plan_sha256
    plan_audit_sha256 = [string]$preparation.governance.plan_audit_sha256
    prebuild_manifest_sha256 = [string]$script:ValidatedPrebuildHash
    registered_tests = $build.run1.registered
    passed_first_run = $build.run1.passed
    passed_second_run = $run2.results.passed
    failed_first_run = $build.run1.failed
    failed_second_run = $run2.results.failed
    production_binary_sha256 = $build.production_binary_sha256
    test_binary_sha256 = $build.test_binary_sha256
    schema_version = $schemaVersion
    schema_baseline = $schemaBaseline
    schema_runtime = [pscustomobject][ordered]@{
      snapshot_labels = $runtimeSchemaLabels
      snapshot_count = $runtimeSchemaLabels.Count
      doctor_schema_version = $schemaVersion
      doctor_ok = [bool]$doctor.ok
    }
    production_slice = $productionSlice
    persistent_tree_fingerprint_scope = $PersistentTreeFingerprintScope
    isolated_localappdata_tree_before_sha256 = $isolatedTreeBefore
    isolated_localappdata_tree_after_cleanup_sha256 = $isolatedTreeAfterCleanup
    disposable_runtime_tree_integrity = 'collected'
    real_mcp = $realMcp
    doctor = $doctor
    n22 = [pscustomobject][ordered]@{
      summary = $n22.summary
      snapshot_count = $n22.snapshot_count
      normalized_marker_sha256 = $n22.normalized_sha256
      selected_snapshot_sha256 = $n22.selected_snapshot_sha256
      decoy_snapshot_sha256 = $n22.decoy_snapshot_sha256
      label_counts = $n22.label_counts
    }
    two_run_equivalence = $equivalence
    output_files = @($outputPaths | ForEach-Object {
        $absolute = Join-Path $Root $_
        [pscustomobject][ordered]@{
          path = $_
          sha256 = File-Hash $absolute
          bytes = [int64](Get-Item -LiteralPath $absolute).Length
        }
      })
    protected_repo_config_rechecked = $true
    git_repository_facts_rechecked = $true
    production_data_access_telemetry = 'not-collected'
  }
  # The final manifest is written last so its hashes bind every other output.
  $evidenceJson = ($evidence | ConvertTo-Json -Depth 30) + [Environment]::NewLine
  Assert-SafeEvidenceText $evidenceJson 'final evidence manifest'
  Write-Utf8Text $EvidenceManifestPath $evidenceJson
  Assert-FinalPublicationChain ([string]$script:ValidatedPrebuildHash) `
      ([string]$publicationPreparation.publication_nonce) $outputPaths
  Write-Host "N22_VERIFY_OK registered=$($build.run1.registered) first_pass=$($build.run1.passed) second_pass=$($run2.results.passed) snapshots=$($n22.snapshot_count)"
  exit 0
} catch {
  Write-PendingFailure
  Write-Error $_.Exception.Message
  exit 1
} finally {
  Close-FrozenInputHandles $script:FrozenInputHandles
  $script:FrozenInputHandles = @()
  Exit-N22EvidenceMutex
}
