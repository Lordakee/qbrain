# N37 D4: five-part release smoke test.
#
#   1. CLI        : init -> put -> search roundtrip (retrieved slug in output)
#   2. STDIO MCP  : NDJSON initialize/tools-list/tools-call(get_stats) session
#                   piped to `qbrain serve` (stdio); "result" object asserted
#   3. HTTP MCP   : `qbrain serve --http --port <free>` with QBRAIN_MCP_TOKEN:
#                   /health with token -> 200 ok; without token -> 401
#                   unauthorized; JSON-RPC tools/call via POST /
#   4. RECOVERY   : corrupt a real brain db (DROP TABLE via python sqlite3,
#                   fallback: truncate), `qbrain doctor` reports FAIL, then
#                   restore the pre-corruption backup (the same file-restore
#                   pattern as the migrate.cpp "<db>.pre-v13.bak" rollback
#                   path) and doctor reports OK again
#   5. REDACTION  : remote get_health over HTTP contains NO drive-letter path
#
# Every child process runs under an ISOLATED data root: LOCALAPPDATA is
# pointed at a system temp dir for the whole script (src/qbrain/util/paths.cpp
# honors the env override first), so the real %LOCALAPPDATA%\Qbrain is never
# touched. Evidence files SMOKE-{CLI,STDIO,HTTP,RECOVERY,REDACT}.txt are
# written to docs/nodes/n37-evidence/ (override with -EvidenceDir).
#
# PowerShell 5.1+ / -NoProfile compatible. Exit 0 only when all five parts
# pass. No secrets: the smoke token below is a throwaway literal.
param(
  [string]$EvidenceDir = ""
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $EvidenceDir) { $EvidenceDir = Join-Path $Root "docs\nodes\n37-evidence" }
New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null

function Write-Evidence([string]$Name, [string[]]$Lines) {
  $path = Join-Path $EvidenceDir $Name
  $Lines | Set-Content -LiteralPath $path -Encoding Ascii
  Write-Host "[smoke] evidence -> $path"
}

function Assert-True([bool]$Cond, [string]$Message) {
  if (-not $Cond) { throw "ASSERT FAILED: $Message" }
}

# --- locate the freshly built qbrain.exe ----------------------------------
$exe = Join-Path $Root "build\cl\qbrain.exe"
if (-not (Test-Path -LiteralPath $exe)) {
  $exe = Get-ChildItem -LiteralPath (Join-Path $Root "build") -Recurse -Filter qbrain.exe |
    Select-Object -First 1 -ExpandProperty FullName
}
if (-not $exe) { throw "qbrain.exe not found; run scripts\build-cl.ps1 first" }
Write-Host "[smoke] exe = $exe"

# --- isolated data root (never the real %LOCALAPPDATA%\Qbrain) ------------
$DataRoot = Join-Path ([IO.Path]::GetTempPath()) "qbrain-n37-smoke"
if (Test-Path -LiteralPath $DataRoot) { Remove-Item -LiteralPath $DataRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null
$SavedLocalAppData = $env:LOCALAPPDATA
$env:LOCALAPPDATA = $DataRoot
Write-Host "[smoke] isolated LOCALAPPDATA = $DataRoot"

$HttpProc = $null
try {
  $StartedAt = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")

  # =====================================================================
  # (1) CLI: init -> put -> search roundtrip
  # =====================================================================
  $out = @()
  $out += "Qbrain smoke CLI - started $StartedAt"
  $out += "exe: $exe"
  $out += "isolated data root: $DataRoot (real %LOCALAPPDATA%\Qbrain untouched)"

  $versionOut = (& $exe --version) -join " "
  Assert-True ($versionOut -match "2\.0\.0") "qbrain --version must report 2.0.0, got: $versionOut"
  $out += "> qbrain --version"
  $out += $versionOut

  $initOut = (& $exe init --brain n37smoke) -join "`n"
  Assert-True ($LASTEXITCODE -eq 0) "qbrain init failed: $initOut"
  Assert-True (Test-Path -LiteralPath (Join-Path $DataRoot "Qbrain\brains\n37smoke\brain.db")) `
    "brain db must live under the isolated data root"
  $out += "> qbrain init --brain n37smoke"
  $out += $initOut

  $putOut = (& $exe put --brain n37smoke --slug people/smoke-alice --title "Smoke Alice" `
      --body "Alice runs engineering at Acme and owns the release pipeline. See [[companies/smoke-acme]].") -join "`n"
  Assert-True ($LASTEXITCODE -eq 0) "qbrain put failed: $putOut"
  $out += "> qbrain put --slug people/smoke-alice ..."
  $out += $putOut

  $searchOut = (& $exe search --brain n37smoke "Alice engineering release" --no-vector) -join "`n"
  Assert-True ($LASTEXITCODE -eq 0) "qbrain search failed: $searchOut"
  Assert-True ($searchOut -like "*people/smoke-alice*") `
    "search output must contain the retrieved slug 'people/smoke-alice'; got: $searchOut"
  $out += "> qbrain search `"Alice engineering release`" --no-vector"
  $out += $searchOut
  $out += ""
  $out += "PASS retrieved slug: people/smoke-alice found via CLI search roundtrip"
  $out += "SMOKE-CLI OK"
  Write-Evidence "SMOKE-CLI.txt" $out
  Write-Host "[smoke] CLI OK"

  # =====================================================================
  # (2) STDIO MCP: NDJSON initialize / tools-list / tools-call session
  # =====================================================================
  $out = @()
  $out += "Qbrain smoke STDIO MCP - started $StartedAt"
  $ndjson = @(
    '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"qbrain-smoke","version":"0"}}}',
    '{"jsonrpc":"2.0","method":"notifications/initialized"}',
    '{"jsonrpc":"2.0","id":2,"method":"tools/list"}',
    '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"get_stats","arguments":{}}}'
  ) -join "`n"
  # Pipe the NDJSON batch to `qbrain serve` (stdio NDJSON transport) and
  # close stdin: the server answers per line, then exits 0 on EOF.
  $responses = ($ndjson + "`n") | & $exe serve --brain n37smoke
  Assert-True ($LASTEXITCODE -eq 0) "qbrain serve (stdio) exited $LASTEXITCODE"
  $responseText = ($responses | Where-Object { $_ }) -join "`n"
  $out += "> 4 NDJSON lines (initialize, notifications/initialized, tools/list, tools/call get_stats) piped to: qbrain serve --brain n37smoke"
  $out += $responseText

  $parsed = @($responses | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json })
  Assert-True ($parsed.Count -eq 3) "expected 3 JSON responses (notification yields none), got $($parsed.Count)"
  foreach ($r in $parsed) {
    Assert-True ($null -ne $r.result) "each response must carry a result object; got: $($r | ConvertTo-Json -Compress)"
    Assert-True ($null -eq $r.error) "no response may carry an error object"
  }
  $init = $parsed | Where-Object { $_.id -eq 1 }
  Assert-True ($init.result.serverInfo.name -eq "qbrain") "initialize serverInfo.name must be qbrain"
  $tools = $parsed | Where-Object { $_.id -eq 2 }
  Assert-True (@($tools.result.tools).Count -gt 0) "tools/list must list at least one tool"
  $stats = $parsed | Where-Object { $_.id -eq 3 }
  Assert-True ($stats.result.isError -eq $false) "tools/call get_stats must not be an error"
  $statsBody = $stats.result.content[0].text | ConvertFrom-Json
  Assert-True ($statsBody.pages -ge 1) "get_stats pages must be >= 1 (CLI put ran first), got $($statsBody.pages)"
  $out += ""
  $out += "PASS result object present in all 3 responses (initialize serverInfo=qbrain, tools/list count=$(@($tools.result.tools).Count), tools/call get_stats pages=$($statsBody.pages))"
  $out += "SMOKE-STDIO OK"
  Write-Evidence "SMOKE-STDIO.txt" $out
  Write-Host "[smoke] STDIO OK"

  # =====================================================================
  # (3) HTTP MCP: token health 200 / no-token 401 / JSON-RPC POST
  # =====================================================================
  $out = @()
  $out += "Qbrain smoke HTTP MCP - started $StartedAt"
  $token = "n37-smoke-token"
  $env:QBRAIN_MCP_TOKEN = $token

  # Pick a free TCP port.
  $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
  $listener.Start()
  $port = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
  $listener.Stop()

  $serveErr = Join-Path $DataRoot "http-serve.err.log"
  $serveOut = Join-Path $DataRoot "http-serve.out.log"
  $HttpProc = Start-Process -FilePath $exe `
    -ArgumentList @("serve", "--http", "--port", "$port", "--brain", "n37smoke") `
    -PassThru -WindowStyle Hidden `
    -RedirectStandardError $serveErr -RedirectStandardOutput $serveOut

  $healthBody = Join-Path $DataRoot "health-body.json"
  $code = ""
  $ready = $false
  # While the server is still starting up, curl fails to connect and writes
  # to stderr; PS5.1 with $ErrorActionPreference=Stop can turn a redirected
  # native stderr write into a terminating error, so poll under Continue.
  $ErrorActionPreference = "Continue"
  try {
    for ($i = 0; $i -lt 40; $i++) {
      if ($HttpProc.HasExited) { throw "qbrain serve --http exited early; stderr: $(Get-Content -LiteralPath $serveErr -Raw)" }
      $code = & curl.exe -s --max-time 3 -o $healthBody -w "%{http_code}" `
        -H "Authorization: Bearer $token" "http://127.0.0.1:$port/health" 2>$null
      if ("$code" -eq "200") { $ready = $true; break }
      Start-Sleep -Milliseconds 500
    }
  } finally {
    $ErrorActionPreference = "Stop"
  }
  Assert-True $ready "HTTP server never became ready on port $port (last code='$code')"

  # Positive: /health with token -> 200 + ok
  $healthWithToken = Get-Content -LiteralPath $healthBody -Raw
  Assert-True ($healthWithToken -match '"ok"\s*:\s*true') "health body with token must be ok; got: $healthWithToken"
  $out += "GET /health (Bearer token) -> HTTP $code"
  $out += $healthWithToken

  # Negative: /health without token -> 401 unauthorized
  $negBody = Join-Path $DataRoot "health-neg.json"
  $negCode = & curl.exe -s --max-time 3 -o $negBody -w "%{http_code}" "http://127.0.0.1:$port/health"
  $negText = Get-Content -LiteralPath $negBody -Raw
  Assert-True ("$negCode" -eq "401") "health without token must be 401, got $negCode"
  Assert-True ($negText -match "unauthorized") "401 body must say unauthorized; got: $negText"
  $out += "GET /health (no token) -> HTTP $negCode"
  $out += $negText

  # JSON-RPC over POST / with token
  $rpcReq = Join-Path $DataRoot "rpc-req.json"
  $rpcBody = Join-Path $DataRoot "rpc-body.json"
  '{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"get_stats","arguments":{}}}' |
    Set-Content -LiteralPath $rpcReq -Encoding Ascii
  $rpcCode = & curl.exe -s --max-time 3 -o $rpcBody -w "%{http_code}" `
    -H "Authorization: Bearer $token" -H "Content-Type: application/json" `
    --data-binary "@$rpcReq" "http://127.0.0.1:$port/"
  Assert-True ("$rpcCode" -eq "200") "JSON-RPC POST with token must be 200, got $rpcCode"
  $rpcText = Get-Content -LiteralPath $rpcBody -Raw
  $rpcJson = $rpcText | ConvertFrom-Json
  Assert-True ($null -ne $rpcJson.result) "JSON-RPC response must carry a result object; got: $rpcText"
  Assert-True ($rpcJson.result.isError -eq $false) "get_stats over HTTP must not error"
  $out += "POST / tools/call get_stats (Bearer token) -> HTTP $rpcCode"
  $out += $rpcText

  $out += ""
  $out += "PASS authorized health=200/ok; unauthorized request rejected HTTP 401 unauthorized; JSON-RPC result object ok"
  $out += "SMOKE-HTTP OK"
  Write-Evidence "SMOKE-HTTP.txt" $out
  Write-Host "[smoke] HTTP OK"

  # =====================================================================
  # (5) REDACTION: remote get_health must contain no drive-letter path
  # (runs while the HTTP server from part 3 is still up)
  # =====================================================================
  $out = @()
  $out += "Qbrain smoke REDACTION - started $StartedAt"
  $redReq = Join-Path $DataRoot "redact-req.json"
  $redBody = Join-Path $DataRoot "redact-body.json"
  $redHeaders = Join-Path $DataRoot "redact-headers.txt"
  '{"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"get_health","arguments":{}}}' |
    Set-Content -LiteralPath $redReq -Encoding Ascii
  $redCode = & curl.exe -s --max-time 3 -o $redBody -D $redHeaders -w "%{http_code}" `
    -H "Authorization: Bearer $token" -H "Content-Type: application/json" `
    --data-binary "@$redReq" "http://127.0.0.1:$port/"
  Assert-True ("$redCode" -eq "200") "get_health over HTTP must be 200, got $redCode"
  $redText = Get-Content -LiteralPath $redBody -Raw
  $redHdrText = Get-Content -LiteralPath $redHeaders -Raw
  $out += "POST / tools/call get_health (Bearer token) -> HTTP $redCode"
  $out += $redText

  $redJson = $redText | ConvertFrom-Json
  Assert-True ($null -ne $redJson.result) "get_health response must carry a result object"
  Assert-True ($redJson.result.isError -eq $false) "get_health must not error"

  # Scan the FULL response (headers + body) for any drive-letter path
  # (C:\, D:\, ...). The N30 D4 redaction contract removes db_path from
  # remote/via_mcp health reports.
  $fullResponse = $redHdrText + "`n" + $redText
  $driveMatches = [regex]::Matches($fullResponse, '[A-Za-z]:\\')
  Assert-True ($driveMatches.Count -eq 0) `
    "remote get_health must not leak drive-letter paths; matched: $($driveMatches | ForEach-Object { $_.Value })"
  $out += ""
  $out += "PASS no-drive-letter: full HTTP response (headers+body) contains zero drive-letter paths (regex [A-Za-z]:\\)"
  $out += "SMOKE-REDACT OK"
  Write-Evidence "SMOKE-REDACT.txt" $out
  Write-Host "[smoke] REDACT OK"

  # Stop the HTTP server before the recovery part.
  if ($HttpProc -and -not $HttpProc.HasExited) { Stop-Process -Id $HttpProc.Id -Force }
  $HttpProc = $null
  Remove-Item Env:\QBRAIN_MCP_TOKEN -ErrorAction SilentlyContinue

  # =====================================================================
  # (4) RECOVERY: corrupt db -> doctor FAIL -> restore backup -> doctor OK
  # =====================================================================
  $out = @()
  $out += "Qbrain smoke RECOVERY - started $StartedAt"
  $db = Join-Path $DataRoot "Qbrain\brains\n37rec\brain.db"

  $initRec = (& $exe init --brain n37rec) -join "`n"
  Assert-True ($LASTEXITCODE -eq 0) "init n37rec failed: $initRec"
  $putRec = (& $exe put --brain n37rec --slug recovery/probe --title "Recovery probe" `
      --body "Page that must survive corruption via the backup restore path.") -join "`n"
  Assert-True ($LASTEXITCODE -eq 0) "put into n37rec failed: $putRec"
  $doctorBefore = (& $exe doctor --brain n37rec --json) -join "`n"
  Assert-True ($LASTEXITCODE -eq 0) "doctor on healthy n37rec failed: $doctorBefore"
  $out += "healthy brain db: $db"
  $out += "> qbrain doctor --brain n37rec --json  (pre-corruption)"
  $out += $doctorBefore

  # Pre-corruption backup, mirroring the migrate.cpp "<db>.pre-v13.bak"
  # rollback artifact: the documented recovery path is a plain file restore
  # (delete -wal/-shm sidecars, copy the backup over brain.db).
  $backup = "$db.n37-smoke.bak"
  Copy-Item -LiteralPath $db -Destination $backup -Force

  # Corrupt deterministically: DROP a required schema table (python's stdlib
  # sqlite3; falls back to truncating the file when no python is available).
  # check_schema_integrity reports the missing object and doctor fails closed
  # (N30 D8).
  $py = Get-Command python -ErrorAction SilentlyContinue
  if ($py) {
    $out += "corruption: python sqlite3 'DROP TABLE links'"
    & python -c "import sqlite3,sys; c=sqlite3.connect(sys.argv[1]); c.execute('DROP TABLE links'); c.commit(); c.close()" $db
    Assert-True ($LASTEXITCODE -eq 0) "python corruption step failed"
  } else {
    $out += "corruption: truncate brain.db to 100 bytes (no python found)"
    $fs = [IO.File]::OpenWrite($db); $fs.SetLength(100); $fs.Close()
  }

  $doctorFail = (& $exe doctor --brain n37rec --json) -join "`n"
  Assert-True ($LASTEXITCODE -ne 0) "doctor on corrupted db must exit nonzero"
  Assert-True ($doctorFail -match '"overall"\s*:\s*"FAIL"') `
    "doctor on corrupted db must report overall FAIL; got: $doctorFail"
  $out += "> qbrain doctor --brain n37rec --json  (after corruption)"
  $out += $doctorFail

  # Recovery: restore the backup file (the .pre-v13.bak pattern).
  foreach ($side in @("$db-wal", "$db-shm")) {
    if (Test-Path -LiteralPath $side) { Remove-Item -LiteralPath $side -Force }
  }
  Copy-Item -LiteralPath $backup -Destination $db -Force
  $out += "recovery: restored $backup over brain.db (same pattern as the migrate.cpp .pre-v13.bak rollback path)"

  $doctorAfter = (& $exe doctor --brain n37rec --json) -join "`n"
  Assert-True ($LASTEXITCODE -eq 0) "doctor after restore must exit 0; got: $doctorAfter"
  Assert-True ($doctorAfter -match '"overall"\s*:\s*"OK"') `
    "doctor after restore must report overall OK; got: $doctorAfter"
  $getBack = (& $exe get --brain n37rec recovery/probe --json) -join "`n"
  Assert-True ($LASTEXITCODE -eq 0 -and $getBack -match "recovery/probe") `
    "restored brain must still serve the pre-corruption page; got: $getBack"
  $out += "> qbrain doctor --brain n37rec --json  (after restore)"
  $out += $doctorAfter
  $out += "> qbrain get recovery/probe (post-restore)"
  $out += $getBack
  $out += ""
  $out += "PASS doctor FAIL on corrupted db, then overall OK after backup restore; recovery/probe page survived"
  $out += "SMOKE-RECOVERY OK"
  Write-Evidence "SMOKE-RECOVERY.txt" $out
  Write-Host "[smoke] RECOVERY OK"

  Write-Host ""
  Write-Host "SMOKE OK (all five parts) - evidence in $EvidenceDir"
  exit 0
} catch {
  Write-Host "[smoke] FAILED: $($_.Exception.Message)"
  throw
} finally {
  if ($HttpProc -and -not $HttpProc.HasExited) { Stop-Process -Id $HttpProc.Id -Force }
  $env:LOCALAPPDATA = $SavedLocalAppData
  Remove-Item Env:\QBRAIN_MCP_TOKEN -ErrorAction SilentlyContinue
}
