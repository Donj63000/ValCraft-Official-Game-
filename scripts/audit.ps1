[CmdletBinding()]
param(
    [string]$BuildDir = "",
    [ValidateSet("RelWithDebInfo", "Release", "Debug")]
    [string]$Configuration = "Debug",
    [ValidateSet("measure", "forensic")]
    [string]$Mode = "measure",
    [string]$Label = "interactive",
    [string]$AuditDir = "",
    [int]$SmokeFrames = 0,
    [switch]$TraceFrames,
    [switch]$HiddenWindow,
    [switch]$FreezeTime,
    [int]$StreamRadius = -1,
    [switch]$DisableShadows,
    [switch]$DisablePostProcess,
    [switch]$NoConfigure,
    [switch]$NoBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Get-LatestClionBinDir {
    $jetbrainsRoot = "C:\Program Files\JetBrains"
    if (-not (Test-Path $jetbrainsRoot)) {
        return $null
    }

    $candidates = Get-ChildItem -Path $jetbrainsRoot -Directory -Filter "CLion *" |
        Sort-Object Name -Descending
    if (@($candidates).Count -eq 0) {
        return $null
    }

    return (Join-Path $candidates[0].FullName "bin")
}

function Add-ToPath {
    param([string[]]$Entries)

    foreach ($entry in $Entries) {
        if ([string]::IsNullOrWhiteSpace($entry) -or -not (Test-Path $entry)) {
            continue
        }

        $pathEntries = $env:PATH -split ';'
        if ($pathEntries -notcontains $entry) {
            $env:PATH = "$entry;$env:PATH"
        }
    }
}

function Resolve-ToolPath {
    param(
        [Parameter(Mandatory)]
        [string]$CommandName,

        [string[]]$FallbackPaths = @()
    )

    try {
        return (Get-Command $CommandName -ErrorAction Stop).Source
    } catch {
        foreach ($fallback in $FallbackPaths) {
            if (-not [string]::IsNullOrWhiteSpace($fallback) -and (Test-Path $fallback)) {
                return $fallback
            }
        }
    }

    throw "Impossible de resoudre l'outil requis '$CommandName'."
}

function Invoke-External {
    param(
        [Parameter(Mandatory)]
        [string]$FilePath,

        [Parameter(Mandatory)]
        [string[]]$Arguments,

        [string]$WorkingDirectory
    )

    Write-Host ("> {0} {1}" -f $FilePath, ($Arguments -join ' '))
    $exitCode = 0
    if ($WorkingDirectory) {
        Push-Location $WorkingDirectory
    }

    try {
        & $FilePath @Arguments
        $exitCode = $LASTEXITCODE
    } finally {
        if ($WorkingDirectory) {
            Pop-Location
        }
    }

    if ($exitCode -ne 0) {
        throw "La commande a echoue avec le code ${exitCode}: $FilePath"
    }
}

function Get-SanitizedAuditLabel {
    param([string]$RawLabel)

    $builder = New-Object System.Text.StringBuilder
    foreach ($character in $RawLabel.ToCharArray()) {
        if (($character -ge 'a' -and $character -le 'z') -or
            ($character -ge 'A' -and $character -le 'Z') -or
            ($character -ge '0' -and $character -le '9') -or
            $character -eq '-' -or
            $character -eq '_') {
            [void]$builder.Append($character)
            continue
        }

        if ($character -eq ' ' -or $character -eq '/' -or $character -eq '\' -or $character -eq '.') {
            [void]$builder.Append('-')
        }
    }

    $sanitized = $builder.ToString().Trim('-')
    if ([string]::IsNullOrWhiteSpace($sanitized)) {
        return "interactive"
    }
    return $sanitized
}

function Get-LatestAuditRun {
    param(
        [Parameter(Mandatory)]
        [string]$AuditRoot,

        [Parameter(Mandatory)]
        [string]$Mode,

        [Parameter(Mandatory)]
        [string]$Label
    )

    $runsRoot = Join-Path $AuditRoot "runs"
    if (-not (Test-Path $runsRoot)) {
        return $null
    }

    $sanitizedLabel = Get-SanitizedAuditLabel -RawLabel $Label
    $pattern = "*-$Mode-$sanitizedLabel"
    return Get-ChildItem -Path $runsRoot -Directory -Filter $pattern |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "cmake-build-debug"
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

if ([string]::IsNullOrWhiteSpace($AuditDir)) {
    $AuditDir = Join-Path $repoRoot "performancesaudit"
}
$AuditDir = [System.IO.Path]::GetFullPath($AuditDir)

$clionBinDir = Get-LatestClionBinDir
$cmakeFallback = if ($clionBinDir) { Join-Path $clionBinDir "cmake\win\x64\bin\cmake.exe" } else { $null }
$mingwBinDir = if ($clionBinDir) { Join-Path $clionBinDir "mingw\bin" } else { $null }
$ninjaDir = if ($clionBinDir) { Join-Path $clionBinDir "ninja\win\x64" } else { $null }

Add-ToPath -Entries @($mingwBinDir, $ninjaDir, (Split-Path -Parent $cmakeFallback))
$cmakeExe = Resolve-ToolPath -CommandName "cmake" -FallbackPaths @($cmakeFallback)

if (-not $NoConfigure) {
    Invoke-External -FilePath $cmakeExe -Arguments @(
        "-S", $repoRoot,
        "-B", $BuildDir,
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=$Configuration",
        "-DVALCRAFT_STRICT_WARNINGS=ON",
        "-DVALCRAFT_ENABLE_COVERAGE=OFF"
    )
}

if (-not $NoBuild) {
    Invoke-External -FilePath $cmakeExe -Arguments @("--build", $BuildDir, "--target", "ValCraft", "--parallel")
}

$gameExe = Join-Path $BuildDir "bin\ValCraft.exe"
if (-not (Test-Path $gameExe)) {
    throw "L'executable ValCraft est introuvable a '$gameExe'."
}

$arguments = @(
    "--audit",
    "--audit-mode=$Mode",
    "--audit-dir=$AuditDir",
    "--audit-label=$Label"
)

if ($TraceFrames) {
    $arguments += "--audit-trace-frames"
}
if ($SmokeFrames -gt 0) {
    $arguments += "--smoke-test"
    $arguments += "--smoke-frames=$SmokeFrames"
    $arguments += "--hidden-window"
}
if ($HiddenWindow -and $SmokeFrames -le 0) {
    $arguments += "--hidden-window"
}
if ($FreezeTime) {
    $arguments += "--freeze-time"
}
if ($StreamRadius -ge 0) {
    $arguments += "--stream-radius=$StreamRadius"
}
if ($DisableShadows) {
    $arguments += "--disable-shadows"
}
if ($DisablePostProcess) {
    $arguments += "--disable-post-process"
}

Write-Host ("==> Lancement audit mode={0} label={1}" -f $Mode, $Label)
Invoke-External -FilePath $gameExe -Arguments $arguments -WorkingDirectory $BuildDir

$latestRun = Get-LatestAuditRun -AuditRoot $AuditDir -Mode $Mode -Label $Label
if ($null -eq $latestRun) {
    throw "Aucun run d'audit n'a ete trouve dans '$AuditDir'."
}

$summaryPath = Join-Path $latestRun.FullName "summary.txt"
$manifestPath = Join-Path $latestRun.FullName "manifest.json"
Write-Host ("==> Run audit: {0}" -f $latestRun.FullName)
Write-Host ("==> Manifest: {0}" -f $manifestPath)
if (Test-Path $summaryPath) {
    Write-Host ("==> Summary: {0}" -f $summaryPath)
    Write-Host ""
    Get-Content -Path $summaryPath
}
