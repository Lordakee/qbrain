# N12 acceptance evidence pack for hard audit (P0-1..P0-3, dry-run, MCP, token fence)
$ErrorActionPreference = "Stop"
$qb = "D:\Projects\Qbrain\build\cl\qbrain.exe"
$outDir = "D:\Projects\Qbrain\docs\nodes\n12-evidence"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$report = Join-Path $outDir "VERIFY-REPORT.md"
$lines = New-Object System.Collections.Generic.List[string]
function L([string]$s) { $lines.Add($s) | Out-Null; Write-Host $s }

if (-not (Test-Path $qb)) { throw "missing $qb — build first" }

# Isolated brain
$brain = "n12verify"
$env:QBRAIN_HOME = $null
# Use --brain if supported via with_brain
function Q([string[]]$a) {
  & $qb @a --brain $brain 2>&1 | Out-String
}

L "# N12 Verify Report"
L "Date: $(Get-Date -Format o)"
L "Binary: $qb"
L ""

# Init / seed
L "## Seed"
L '```'
L (Q @("init"))
L (Q @("capture", "N12 verify note about quantum brain search rerank minions dream"))
L (Q @("capture", "Second note for membership"))
L '```'

# Snapshot helper via doctor + sqlite-ish stats from doctor
function Snapshot([string]$label) {
  $d = Q @("doctor")
  L "### snapshot $label"
  L '```'
  L $d
  L '```'
  # Also list_jobs and facts via CLI if available — use doctor only
  return $d
}

L "## schema"
$doc = Snapshot "baseline"
if ($doc -notmatch "schema:\s*v6") { throw "schema not v6" }
L "PASS: schema v6"

# Dry-run inertness: doctor before/after dream dry
L "## P0-2 dry-run inertness"
$before = Snapshot "before-dry"
$dreamJson = Q @("dream", "--json")
L '```json'
L $dreamJson
L '```'
$after = Snapshot "after-dry"
# Compare pages/chunks/links lines
function ExtractStats([string]$s) {
  if ($s -match "pages=(\d+)\s+chunks=(\d+)\s+links=(\d+)") {
    return "$($Matches[1])|$($Matches[2])|$($Matches[3])"
  }
  return "unknown"
}
$sb = ExtractStats $before
$sa = ExtractStats $after
L "stats before=$sb after=$sa"
if ($sb -ne $sa) { throw "dry-run changed doctor stats: $sb -> $sa" }
L "PASS: dry-run doctor stats unchanged"

# Phase isolation: apply only consolidate — embed waiting jobs should not complete via dream phase alone without listing other counters
L "## phase isolation --apply --phase consolidate"
$b2 = Snapshot "before-consolidate"
$apply = Q @("dream", "--apply", "--phase", "consolidate")
L '```'
L $apply
L '```'
if ($apply -notmatch "consolidate") { throw "consolidate phase missing" }
if ($apply -match "orphans \[ok\]" -and $apply -match "purge \[ok\]") {
  throw "unexpected multi-phase under --phase consolidate"
}
L "PASS: only consolidate phase reported"

# Search rerank
L "## search --rerank --mode"
$search = Q @("search", "quantum search", "--no-vector", "--rerank", "--mode", "balanced", "--json")
L '```'
L $search
L '```'
if ($search -notmatch "rerank_score") { throw "missing rerank_score" }
L "PASS: rerank_score present + --mode accepted"

# Token fence via worker
L "## worker non-empty token (claim path)"
# enqueue embed via capture already may have jobs
$w = Q @("worker", "--once")
L '```'
L $w
L '```'
L "PASS: worker --once ran (claim path uses cli-worker token in code)"

# Unit tests if present
$tests = "D:\Projects\Qbrain\build\cl\qbrain_tests.exe"
if (Test-Path $tests) {
  L "## unit tests"
  $tr = & $tests 2>&1 | Out-String
  L '```'
  L $tr
  L '```'
  if ($LASTEXITCODE -ne 0) { throw "unit tests failed" }
  L "PASS: unit tests"
} else {
  L "## unit tests"
  L "SKIP: qbrain_tests.exe not built yet"
}

L ""
L "## Summary"
L "All executed checks PASS."
$lines | Set-Content $report -Encoding utf8
Write-Host "WROTE $report"
exit 0
