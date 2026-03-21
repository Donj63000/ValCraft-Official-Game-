Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
& (Join-Path $repoRoot "scripts\check.ps1")
exit $LASTEXITCODE
