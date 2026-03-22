[CmdletBinding()]
param(
    [string]$BuildDir = "",
    [ValidateSet("RelWithDebInfo", "Release", "Debug")]
    [string]$Configuration = "RelWithDebInfo",
    [int]$SmokeFrames = 720,
    [string[]]$Scenarios = @("baseline", "world_stress", "no_shadows", "no_post_process"),
    [switch]$Trace,
    [switch]$NoConfigure,
    [switch]$NoBuild,
    [string]$ArtifactDir = ""
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

    throw "Unable to resolve required tool '$CommandName'."
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
        throw "Command failed with exit code ${exitCode}: $FilePath"
    }
}

function Get-ScenarioDefinition {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [int]$SmokeFrames,

        [Parameter(Mandatory)]
        [string]$OutputPath,

        [switch]$Trace
    )

    $arguments = @(
        "--smoke-test",
        "--smoke-frames=$SmokeFrames",
        "--hidden-window",
        "--freeze-time",
        "--perf-report",
        "--perf-json=$OutputPath",
        "--perf-scenario=$Name"
    )

    switch ($Name) {
    "baseline" {
        $arguments += "--stream-radius=10"
    }
    "world_stress" {
        $arguments += "--stream-radius=14"
    }
    "no_shadows" {
        $arguments += "--stream-radius=10"
        $arguments += "--disable-shadows"
    }
    "no_post_process" {
        $arguments += "--stream-radius=10"
        $arguments += "--disable-post-process"
    }
    default {
        throw "Unknown perf scenario '$Name'."
    }
    }

    if ($Trace) {
        $arguments += "--perf-trace"
    }

    return @{
        Name = $Name
        Arguments = $arguments
    }
}

function Assert-ScenarioReport {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$Report,

        [Parameter(Mandatory)]
        [string]$ScenarioName,

        [Parameter(Mandatory)]
        [int]$SmokeFrames,

        [switch]$Trace
    )

    if ($Report.schema_version -ne 1) {
        throw "Perf scenario '$ScenarioName' returned unexpected schema_version '$($Report.schema_version)'."
    }
    if ($null -eq $Report.metadata -or $null -eq $Report.summary -or $null -eq $Report.hotspots) {
        throw "Perf scenario '$ScenarioName' is missing required sections."
    }
    if ($Report.metadata.scenario -ne $ScenarioName) {
        throw "Perf scenario '$ScenarioName' wrote a mismatched metadata.scenario '$($Report.metadata.scenario)'."
    }
    if ([int]$Report.summary.frame_count -ne $SmokeFrames) {
        throw "Perf scenario '$ScenarioName' reported frame_count=$($Report.summary.frame_count), expected $SmokeFrames."
    }
    if ($Trace -and @($Report.frames).Count -ne $SmokeFrames) {
        throw "Perf scenario '$ScenarioName' did not emit the expected frame trace."
    }
    if ($null -eq $Report.worst_frames -or $null -eq $Report.spike_windows) {
        throw "Perf scenario '$ScenarioName' is missing worst_frames or spike_windows."
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "cmake-build-perf"
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

if ([string]::IsNullOrWhiteSpace($ArtifactDir)) {
    $runId = Get-Date -Format "yyyyMMdd-HHmmss"
    $ArtifactDir = Join-Path $BuildDir ("perf-artifacts\" + $runId)
}
$ArtifactDir = [System.IO.Path]::GetFullPath($ArtifactDir)

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
    throw "ValCraft executable not found at '$gameExe'."
}

if (Test-Path $ArtifactDir) {
    Remove-Item -Path $ArtifactDir -Recurse -Force
}
New-Item -ItemType Directory -Path $ArtifactDir | Out-Null

$scenarioSummaries = @()
foreach ($scenarioName in $Scenarios) {
    $outputPath = Join-Path $ArtifactDir ($scenarioName + ".json")
    $definition = Get-ScenarioDefinition -Name $scenarioName -SmokeFrames $SmokeFrames -OutputPath $outputPath -Trace:$Trace
    Write-Host ("==> Running perf scenario '{0}'" -f $scenarioName)
    Invoke-External -FilePath $gameExe -Arguments $definition.Arguments -WorkingDirectory $BuildDir

    if (-not (Test-Path $outputPath)) {
        throw "Perf scenario '$scenarioName' did not produce '$outputPath'."
    }

    $report = Get-Content -Path $outputPath -Raw | ConvertFrom-Json
    Assert-ScenarioReport -Report $report -ScenarioName $scenarioName -SmokeFrames $SmokeFrames -Trace:$Trace

    $summary = [PSCustomObject]@{
        scenario = $scenarioName
        frame_avg = [double]$report.summary.frame_total_ms.avg
        frame_p95 = [double]$report.summary.frame_total_ms.p95
        frame_max = [double]$report.summary.frame_total_ms.max
        lag_frames_16_7 = [int]$report.summary.lag_buckets.over_16_7_ms
        lag_frames_33_3 = [int]$report.summary.lag_buckets.over_33_3_ms
        lag_frames_50_0 = [int]$report.summary.lag_buckets.over_50_0_ms
        worst_frame_stage = [string]$report.hotspots.worst_frame_stage
        spike_windows = @($report.spike_windows).Count
        output = $outputPath
    }
    $scenarioSummaries += $summary
}

Write-Host "==> Performance suite summary"
$scenarioSummaries |
    Sort-Object scenario |
    Format-Table scenario, frame_avg, frame_p95, frame_max, lag_frames_16_7, lag_frames_33_3, lag_frames_50_0, worst_frame_stage, spike_windows -AutoSize |
    Out-String |
    Write-Host

$suiteSummary = [PSCustomObject]@{
    schema_version = 1
    configuration = $Configuration
    smoke_frames = $SmokeFrames
    trace_enabled = [bool]$Trace
    build_dir = $BuildDir
    artifact_dir = $ArtifactDir
    scenarios = $scenarioSummaries
}

$suiteSummaryPath = Join-Path $ArtifactDir "suite-summary.json"
$suiteSummary | ConvertTo-Json -Depth 8 | Set-Content -Path $suiteSummaryPath -Encoding UTF8
Write-Host ("==> Suite summary JSON: {0}" -f $suiteSummaryPath)
