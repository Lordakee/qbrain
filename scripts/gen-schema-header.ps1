$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$sqlPath = Join-Path $Root "schema\001_init.sql"
$outPath = Join-Path $Root "include\qbrain\storage\schema_sql.hpp"
$sql = Get-Content -LiteralPath $sqlPath -Raw
if ($sql -match '\)QBSQL') { throw "SQL contains raw-string terminator )QBSQL" }
$hdr = @"
#pragma once
// AUTO-GENERATED from schema/001_init.sql — do not edit by hand.
// Regenerate: powershell scripts/gen-schema-header.ps1
namespace qbrain::storage {
inline constexpr const char* kCanonicalSchemaSql = R"QBSQL(
$sql
)QBSQL";
}
"@
Set-Content -LiteralPath $outPath -Value $hdr -Encoding UTF8
Write-Host "Wrote $outPath"
