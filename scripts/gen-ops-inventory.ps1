param(
  # Path to a runtime registry export produced by qbrain_tests.exe with
  # QBRAIN_N31_EXPORT_REGISTRY set (test_n31.cpp n31-a env-gated export).
  # When empty, the script runs the test suite once to produce a fresh export.
  [string]$RegistryExport = "",
  # Output path; default docs/nodes/n31-evidence/OPS-INVENTORY.json.
  [string]$OutFile = "",
  # Run the full generation pipeline twice and require byte-identical output.
  [switch]$VerifyDeterminism
)

# N31 D1: generate docs/nodes/n31-evidence/OPS-INVENTORY.json from exactly two
# code-derived sources, cross-checked against the ops parity ledger:
#   (1) runtime registry export (name/scope/local_only/description/schema),
#   (2) static extraction of op->test mappings from tests/*.cpp sources
#       (registry-proxy helper calls + direct global_registry().call + MCP
#       tools/call request literals),
#   (3) docs/OPS-PARITY-LEDGER.md upstream/extension tables (row mapping).
# Deterministic: two consecutive runs are byte-identical (no timestamps, no
# local paths, fixed key order, LF newlines, no BOM).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\gen-ops-inventory.ps1

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$EvidenceDir = Join-Path $Root "docs\nodes\n31-evidence"
if (-not $OutFile) { $OutFile = Join-Path $EvidenceDir "OPS-INVENTORY.json" }
$LedgerPath = Join-Path $Root "docs\OPS-PARITY-LEDGER.md"

# Frozen at the N31 pre-gate (docs/nodes/n31-evidence/PRE-GATE.json).
$FrozenN = 108
$FrozenLedgerUpstream = 104
$FrozenLedgerExtension = 4  # N31 merge: list_job_messages reconciled into extensions table

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Read-Text([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path)) {
    throw "required input missing: $Path"
  }
  return ([IO.File]::ReadAllText($Path) -replace "`r`n", "`n")
}

function Get-RuntimeExport([string]$Path) {
  if (-not $Path) {
    $exe = Join-Path $Root "build\cl\qbrain_tests.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
      throw "qbrain_tests.exe not found at build\cl\qbrain_tests.exe; build first"
    }
    $Path = Join-Path ([IO.Path]::GetTempPath()) "qbrain-n31-registry-export.json"
    $env:QBRAIN_N31_EXPORT_REGISTRY = $Path
    $workDir = Split-Path -Parent $exe
    Push-Location $workDir
    try {
      # The n31-a test writes the export BEFORE its inventory assertions, so
      # the export exists even on a first-generation run where the inventory
      # does not exist yet and the suite reports a failure.
      # Scope EAP down: the suite legitimately writes warnings to stderr, and
      # under ErrorActionPreference=Stop PowerShell 5 turns the first native
      # stderr line into a terminating NativeCommandError.
      $previousEap = $ErrorActionPreference
      $ErrorActionPreference = "Continue"
      try {
        $null = & $exe 2>&1
        $code = $LASTEXITCODE
      } finally {
        $ErrorActionPreference = $previousEap
      }
      if ($code -ne 0) {
        Write-Warning "qbrain_tests.exe exit code $code (expected during first generation, before OPS-INVENTORY.json exists)"
      }
    } finally {
      Pop-Location
    }
  }
  if (-not (Test-Path -LiteralPath $Path)) {
    throw "registry export not found: $Path"
  }
  return $Path
}

# JSON string escaper (deterministic; control chars as \uXXXX).
function ConvertTo-JsonStringLiteral([string]$Value) {
  $sb = New-Object System.Text.StringBuilder
  foreach ($ch in $Value.ToCharArray()) {
    $code = [int]$ch
    if ($ch -eq '"') { [void]$sb.Append('\"') }
    elseif ($ch -eq '\') { [void]$sb.Append('\\') }
    elseif ($code -lt 0x20) { [void]$sb.Append('\u' + $code.ToString('x4')) }
    else { [void]$sb.Append($ch) }
  }
  return $sb.ToString()
}

# ---------------------------------------------------------------------------
# Ledger table parsing (same state machine as the n31-a C++ assertion)
# ---------------------------------------------------------------------------

function Get-LedgerOps([string]$Markdown, [bool]$Extensions) {
  $names = New-Object 'System.Collections.Generic.HashSet[string]'
  $inSection = $false
  $inExtensions = $false
  foreach ($line in ($Markdown -split "`n", -1)) {
    if ($line.StartsWith("## ")) { $inSection = $false }
    if ($line.StartsWith("| upstream_op |")) {
      $inSection = $true; $inExtensions = $false
    } elseif ($line.StartsWith("## Qbrain extensions")) {
      $inSection = $true; $inExtensions = $true
    }
    if ($inSection -and ($inExtensions -eq $Extensions) -and $line.StartsWith("|")) {
      $cells = $line.Substring(1).Split('|')
      if ($cells.Count -ge 2) {
        $id = $cells[0].Trim()
        $status = $cells[1].Trim()
        $isToken = ($id -ne "" -and $id -notmatch '[\s\*\-]' -and
                    $id -ne "upstream_op" -and $id -ne "op")
        $isImplemented = ($status.StartsWith("**implemented**") -or
                          $status.StartsWith("implemented"))
        if ($isToken -and $isImplemented) { [void]$names.Add($id) }
      }
    }
  }
  return $names
}

# ---------------------------------------------------------------------------
# Static op->test extraction from tests/*.cpp
# ---------------------------------------------------------------------------

# Scan one file's line stream for column-0 function definitions and brace
# depth, producing the per-line enclosing function names and the set of
# "registry proxy" helper names (functions whose bodies contain a direct
# registry call or an MCP RPC dispatch).
function Get-FileScan([string]$Text) {
  $lines = $Text -split "`n", -1
  $lineFn = New-Object 'System.Collections.Generic.List[string]'
  $helpers = New-Object 'System.Collections.Generic.HashSet[string]'
  $isProxy = {
    param([string]$Body)
    return ($Body.Contains('global_registry().call(') -or
            $Body.Contains('handle_rpc_body('))
  }
  $depth = 0
  $current = ""
  $entryDepth = -1
  $body = New-Object System.Text.StringBuilder
  foreach ($line in $lines) {
    $lineFn.Add($current)
    $isDef = $false
    if ($line -match '^[A-Za-z_]' -and $line -match '\(' -and
        $line -notmatch ';\s*$') {
      $name = [regex]::Match($line, '([A-Za-z_]\w*)\s*\(').Groups[1].Value
      if ($name -notin @('if', 'for', 'while', 'switch', 'catch', 'return',
                         'else', 'do', 'new', 'delete', 'sizeof', 'throw')) {
        # Close any still-tracked function before opening this one.
        if ($current -ne "" -and (& $isProxy $body.ToString())) {
          [void]$helpers.Add($current)
        }
        $current = $name
        $entryDepth = $depth
        $body = New-Object System.Text.StringBuilder
        $isDef = $true
      }
    }
    [void]$body.Append($line)
    [void]$body.Append("`n")
    $opens = [regex]::Matches($line, '\{').Count
    $closes = [regex]::Matches($line, '\}').Count
    $depth += $opens - $closes
    if ($current -ne "" -and -not $isDef -and $depth -le $entryDepth) {
      if (& $isProxy $body.ToString()) { [void]$helpers.Add($current) }
      $current = ""
      $entryDepth = -1
      $body = New-Object System.Text.StringBuilder
    }
  }
  $result = New-Object PSObject -Property @{
    LineFunction = $lineFn
    Helpers = $helpers
  }
  return $result
}

function Get-StaticMappings([hashtable]$OpsIndex) {
  # op name -> HashSet of "file|case"
  $mappings = @{}
  foreach ($name in $OpsIndex.Keys) {
    $mappings[$name] = New-Object 'System.Collections.Generic.HashSet[string]'
  }

  $testDir = Join-Path $Root "tests"
  $files = Get-ChildItem -LiteralPath $testDir -Filter '*.cpp' |
      Sort-Object -Property Name
  foreach ($file in $files) {
    $text = Read-Text $file.FullName
    $scan = Get-FileScan $text
    $fileName = $file.Name

    # Registry-proxy helper names: header-defined known set + auto-detected
    # functions whose bodies call the registry or the MCP RPC handler.
    $helperNames = New-Object 'System.Collections.Generic.HashSet[string]'
    @('call_op', 'call_remote', 'mcp_call') | ForEach-Object { [void]$helperNames.Add($_) }
    foreach ($h in $scan.Helpers) { [void]$helperNames.Add($h) }

    $escaped = @(($helperNames | Sort-Object) | ForEach-Object { [regex]::Escape($_) })
    $helperPattern = '\b(?:' + ($escaped -join '|') + ')\s*\('
    $directPattern = 'global_registry\(\)\.call\s*\('

    # Line-start offsets for enclosing-function lookup.
    $lineStarts = New-Object 'System.Collections.Generic.List[int]'
    $pos = 0
    foreach ($line in ($text -split "`n", -1)) {
      $lineStarts.Add($pos)
      $pos += $line.Length + 1
    }
    $lookup = {
      param([int]$Offset)
      $lo = 0; $hi = $lineStarts.Count - 1
      while ($lo -lt $hi) {
        $mid = [int](($lo + $hi + 1) / 2)
        if ($lineStarts[$mid] -le $Offset) { $lo = $mid } else { $hi = $mid - 1 }
      }
      return $lo
    }

    foreach ($pattern in @($helperPattern, $directPattern)) {
      foreach ($m in [regex]::Matches($text, $pattern)) {
        $windowLen = [Math]::Min(240, $text.Length - $m.Index - $m.Length)
        if ($windowLen -le 0) { continue }
        $window = $text.Substring($m.Index + $m.Length, $windowLen)
        $nameMatch = [regex]::Match($window, '^[\sA-Za-z0-9_:,]*?"([a-z_0-9]+)"')
        if ($nameMatch.Success) {
          $op = $nameMatch.Groups[1].Value
          if ($OpsIndex.ContainsKey($op)) {
            $lineIndex = & $lookup $m.Index
            $fn = $scan.LineFunction[$lineIndex]
            if ([string]::IsNullOrEmpty($fn)) { $fn = ($fileName -replace '\.cpp$', '') }
            [void]$mappings[$op].Add("$fileName|$fn")
          }
        }
      }
    }

    # MCP JSON-RPC tools/call request literals: "tools/call" ... "name":"op"
    foreach ($m in [regex]::Matches($text, 'tools/call')) {
      $windowLen = [Math]::Min(300, $text.Length - $m.Index - $m.Length)
      if ($windowLen -le 0) { continue }
      $window = $text.Substring($m.Index + $m.Length, $windowLen)
      $nameMatch = [regex]::Match($window, '"name":"([a-z_0-9]+)"')
      if ($nameMatch.Success) {
        $op = $nameMatch.Groups[1].Value
        if ($OpsIndex.ContainsKey($op)) {
          $lineIndex = & $lookup $m.Index
          $fn = $scan.LineFunction[$lineIndex]
          if ([string]::IsNullOrEmpty($fn)) { $fn = ($fileName -replace '\.cpp$', '') }
          [void]$mappings[$op].Add("$fileName|$fn")
        }
      }
    }
  }
  return $mappings
}

# ---------------------------------------------------------------------------
# Deterministic serializer
# ---------------------------------------------------------------------------

$N31Reasons = @{
  'list_job_messages' = ('registered op absent from both ledger tables; N17 helper op, ' +
      'listed as an "uncounted Qbrain helper" in the ledger N17 notes; requires an ' +
      'explicit ledger disposition before it can be counted')
}
$N31DefaultReason = ('registered op with no row in either ledger table (upstream or ' +
    'extension); surfaced by the N31 four-way reconciliation')

function New-InventoryJson([string]$ExportPath) {
  $exportText = Read-Text $ExportPath
  $export = $exportText | ConvertFrom-Json
  if (-not $export.ops) { throw "registry export has no ops array: $ExportPath" }

  $ops = @($export.ops | Sort-Object -Property name)
  if ($ops.Count -ne $FrozenN) {
    throw "registry export op count $($ops.Count) != frozen N $FrozenN"
  }

  $ledgerText = Read-Text $LedgerPath
  $ledgerUpstream = Get-LedgerOps $ledgerText $false
  $ledgerExtension = Get-LedgerOps $ledgerText $true
  if ($ledgerUpstream.Count -ne $FrozenLedgerUpstream) {
    throw "ledger upstream implemented rows $($ledgerUpstream.Count) != frozen $FrozenLedgerUpstream"
  }
  if ($ledgerExtension.Count -ne $FrozenLedgerExtension) {
    throw "ledger extension rows $($ledgerExtension.Count) != frozen $FrozenLedgerExtension"
  }

  # Every ledger row must map to a registered op (hence to an inventory row).
  $opsIndex = @{}
  foreach ($op in $ops) { $opsIndex[$op.name] = $op }
  foreach ($name in $ledgerUpstream) {
    if (-not $opsIndex.ContainsKey($name)) {
      throw "ledger upstream op '$name' is not registered; it cannot map to an inventory row"
    }
  }
  foreach ($name in $ledgerExtension) {
    if (-not $opsIndex.ContainsKey($name)) {
      throw "ledger extension op '$name' is not registered; it cannot map to an inventory row"
    }
  }

  $mappings = Get-StaticMappings $opsIndex

  $defaultSchema = '{"type":"object","properties":{}}'
  $withTests = 0
  foreach ($name in $opsIndex.Keys) {
    if ($mappings[$name].Count -gt 0) { $withTests++ }
  }
  $diffOps = @($opsIndex.Keys | Where-Object {
    -not $ledgerUpstream.Contains($_) -and -not $ledgerExtension.Contains($_)
  } | Sort-Object)

  $sb = New-Object System.Text.StringBuilder
  [void]$sb.Append("{`n")
  [void]$sb.Append('  "generated_by": "scripts/gen-ops-inventory.ps1",' + "`n")
  [void]$sb.Append('  "node": "N31",' + "`n")
  [void]$sb.Append('  "frozen_registry_count": ' + $FrozenN + ",`n")
  [void]$sb.Append('  "counts": {' + "`n")
  [void]$sb.Append('    "registry_ops": ' + $ops.Count + ",`n")
  [void]$sb.Append('    "inventory_rows": ' + $ops.Count + ",`n")
  [void]$sb.Append('    "ledger_upstream": ' + $ledgerUpstream.Count + ",`n")
  [void]$sb.Append('    "ledger_extension": ' + $ledgerExtension.Count + ",`n")
  [void]$sb.Append('    "extensions_or_diff": ' + $diffOps.Count + ",`n")
  [void]$sb.Append('    "ops_with_tests": ' + $withTests + ",`n")
  [void]$sb.Append('    "ops_without_tests": ' + ($ops.Count - $withTests) + "`n")
  [void]$sb.Append('  },' + "`n")
  [void]$sb.Append('  "ops": [' + "`n")
  for ($i = 0; $i -lt $ops.Count; $i++) {
    $op = $ops[$i]
    if ($ledgerUpstream.Contains($op.name)) { $ledger = 'upstream' }
    elseif ($ledgerExtension.Contains($op.name)) { $ledger = 'extension' }
    else { $ledger = 'no-ledger-diff' }
    $schema = [string]$op.input_schema_json
    $hasSchema = ($schema -ne '' -and $schema -ne $defaultSchema)
    $localOnly = if ($op.local_only) { 'true' } else { 'false' }
    [void]$sb.Append('    {' + "`n")
    [void]$sb.Append('      "name": "' + (ConvertTo-JsonStringLiteral ([string]$op.name)) + '",' + "`n")
    [void]$sb.Append('      "scope": "' + ([string]$op.scope) + '",' + "`n")
    [void]$sb.Append('      "local_only": ' + $localOnly + ",`n")
    [void]$sb.Append('      "description_summary": "' + (ConvertTo-JsonStringLiteral ([string]$op.description)) + '",' + "`n")
    [void]$sb.Append('      "has_input_schema": ' + $(if ($hasSchema) { 'true' } else { 'false' }) + ",`n")
    [void]$sb.Append('      "ledger": "' + $ledger + '",' + "`n")
    $tests = @($mappings[$op.name] | Sort-Object)
    [void]$sb.Append('      "tests": [')
    for ($t = 0; $t -lt $tests.Count; $t++) {
      $parts = $tests[$t].Split('|', 2)
      if ($t -gt 0) { [void]$sb.Append(' ') }
      [void]$sb.Append('{"file": "' + $parts[0] + '", "case": "' + $parts[1] + '"}')
      if ($t -lt $tests.Count - 1) { [void]$sb.Append(',') }
    }
    [void]$sb.Append(']' + "`n")
    [void]$sb.Append('    }')
    if ($i -lt $ops.Count - 1) { [void]$sb.Append(',') }
    [void]$sb.Append("`n")
  }
  [void]$sb.Append('  ],' + "`n")
  [void]$sb.Append('  "extensions_or_diff": [' + "`n")
  for ($d = 0; $d -lt $diffOps.Count; $d++) {
    $reason = $N31DefaultReason
    if ($N31Reasons.ContainsKey($diffOps[$d])) { $reason = $N31Reasons[$diffOps[$d]] }
    [void]$sb.Append('    {' + "`n")
    [void]$sb.Append('      "name": "' + (ConvertTo-JsonStringLiteral $diffOps[$d]) + '",' + "`n")
    [void]$sb.Append('      "reason": "' + (ConvertTo-JsonStringLiteral $reason) + '"' + "`n")
    [void]$sb.Append('    }')
    if ($d -lt $diffOps.Count - 1) { [void]$sb.Append(',') }
    [void]$sb.Append("`n")
  }
  [void]$sb.Append('  ]' + "`n")
  [void]$sb.Append("}`n")
  return $sb.ToString()
}

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

$exportPath = Get-RuntimeExport $RegistryExport
Write-Host "runtime registry export: $exportPath"

if ($VerifyDeterminism) {
  $run1 = New-InventoryJson $exportPath
  $run2 = New-InventoryJson $exportPath
  $sha = [System.Security.Cryptography.SHA256]::Create()
  $h1 = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($run1))).Replace('-', '').ToLowerInvariant()
  $h2 = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($run2))).Replace('-', '').ToLowerInvariant()
  if ($h1 -ne $h2) {
    throw "determinism check FAILED: run1 sha256=$h1 run2 sha256=$h2"
  }
  Write-Host "DETERMINISM_OK sha256=$h1 (two consecutive pipeline runs byte-identical)"
  $content = $run1
} else {
  $content = New-InventoryJson $exportPath
}

$outDir = Split-Path -Parent $OutFile
if (-not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText($OutFile, $content, $utf8NoBom)
Write-Host "WROTE $OutFile ($($content.Length) bytes)"
exit 0
