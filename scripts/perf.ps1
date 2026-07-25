[CmdletBinding()]
param(
    [string]$BuildDir = "",
    [ValidateSet("RelWithDebInfo", "Release", "Debug")]
    [string]$Configuration = "RelWithDebInfo",
    [ValidateRange(1, 36000)]
    [int]$SmokeFrames = 720,
    [int]$WarmupFrames = 180,
    [ValidateRange(1, 10)]
    [int]$Repetitions = 3,
    [ValidateRange(640, 7680)]
    [int]$Width = 1920,
    [ValidateRange(360, 4320)]
    [int]$Height = 1080,
    [string[]]$Scenarios = @("baseline", "world_stress", "no_shadows", "no_post_process"),
    [switch]$Trace,
    [switch]$AdaptiveQuality,
    [switch]$EnforceThresholds,
    [string]$BaselinePath = "",
    [ValidateRange(0.0, 100.0)]
    [double]$MaxRegressionPercent = 8.0,
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

        $pathSeparator = [System.IO.Path]::PathSeparator
        $pathEntries = $env:PATH -split [Regex]::Escape([string]$pathSeparator)
        if ($pathEntries -notcontains $entry) {
            $env:PATH = "$entry$pathSeparator$env:PATH"
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
        [int]$WarmupFrames,

        [Parameter(Mandatory)]
        [int]$Width,

        [Parameter(Mandatory)]
        [int]$Height,

        [Parameter(Mandatory)]
        [string]$OutputPath,

        [Parameter(Mandatory)]
        [string]$AuditRoot,

        [switch]$AdaptiveQuality,

        [switch]$Trace
    )

    $totalFrames = $SmokeFrames + $WarmupFrames
    $arguments = @(
        "--smoke-test",
        "--smoke-frames=$totalFrames",
        "--perf-warmup-frames=$WarmupFrames",
        "--window-width=$Width",
        "--window-height=$Height",
        "--hidden-window",
        "--freeze-time",
        "--perf-report",
        "--perf-json=$OutputPath",
        "--perf-scenario=$Name",
        "--audit-dir=$AuditRoot",
        "--audit-mode=measure"
    )

    switch ($Name) {
    "baseline" {
        $arguments += "--stream-radius=5"
    }
    "world_stress" {
        $arguments += "--stream-radius=8"
    }
    "no_shadows" {
        $arguments += "--stream-radius=5"
        $arguments += "--disable-shadows"
    }
    "no_post_process" {
        $arguments += "--stream-radius=5"
        $arguments += "--disable-post-process"
    }
    "sea_tempest" {
        # Je fige une Tempest maritime hors flash pour mesurer durablement le
        # coût des vagues, des gouttes et des impacts au niveau de qualité haut.
        $arguments += "--stream-radius=5"
        $arguments += "--smoke-session=sea-new"
        $arguments += "--smoke-ship-view=deck"
        $arguments += "--initial-weather-time=2760"
    }
    default {
        throw "Unknown perf scenario '$Name'."
    }
    }

    if (-not $AdaptiveQuality) {
        $arguments += "--fixed-render-quality"
    }

    if ($Trace) {
        $arguments += "--perf-trace"
    }

    return @{
        Name = $Name
        Arguments = $arguments
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

function Find-LatestScenarioRun {
    param(
        [Parameter(Mandatory)]
        [string]$AuditRoot,

        [Parameter(Mandatory)]
        [string]$ScenarioName
    )

    $runsRoot = Join-Path $AuditRoot "runs"
    if (-not (Test-Path $runsRoot)) {
        return $null
    }

    $sanitizedLabel = Get-SanitizedAuditLabel -RawLabel $ScenarioName
    return Get-ChildItem -Path $runsRoot -Directory -Filter "*-measure-$sanitizedLabel" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

function Assert-ScenarioReport {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$Report,

        [Parameter(Mandatory)]
        [string]$ScenarioName,

        [Parameter(Mandatory)]
        [int]$SmokeFrames,

        [Parameter(Mandatory)]
        [int]$WarmupFrames,

        [Parameter(Mandatory)]
        [int]$Width,

        [Parameter(Mandatory)]
        [int]$Height,

        [Parameter(Mandatory)]
        [string]$Configuration,

        [switch]$AdaptiveQuality,

        [switch]$Trace
    )

    if ($Report.schema_version -ne 2) {
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
    if ([int]$Report.metadata.warmup_frames -ne $WarmupFrames) {
        throw "Perf scenario '$ScenarioName' reported warmup_frames=$($Report.metadata.warmup_frames), expected $WarmupFrames."
    }
    if ([int]$Report.metadata.viewport_width -ne $Width -or [int]$Report.metadata.viewport_height -ne $Height) {
        throw "Perf scenario '$ScenarioName' reported an unexpected viewport."
    }
    if ([string]$Report.metadata.platform -eq "unknown" -or
        [string]$Report.metadata.build_type -ne $Configuration) {
        throw "Perf scenario '$ScenarioName' used incompatible platform/build metadata."
    }
    $expectedQuality = if ($AdaptiveQuality) { "adaptive" } else { "fixed_high" }
    if ([string]$Report.metadata.quality_profile -ne $expectedQuality) {
        throw "Perf scenario '$ScenarioName' reported quality '$($Report.metadata.quality_profile)', expected '$expectedQuality'."
    }
    if ($Trace -and @($Report.frames).Count -ne $SmokeFrames) {
        throw "Perf scenario '$ScenarioName' did not emit the expected frame trace."
    }
    if ($null -eq $Report.worst_frames -or $null -eq $Report.spike_windows) {
        throw "Perf scenario '$ScenarioName' is missing worst_frames or spike_windows."
    }
    if ([int]$Report.summary.gpu_timing_samples -le 0 -or
        [double]$Report.summary.gpu_frame_ms.p95 -le 0.0) {
        throw "Perf scenario '$ScenarioName' did not produce valid GPU timing samples."
    }
    if ([double]$Report.summary.process_private_bytes.max -le 0.0 -or
        [double]$Report.summary.process_working_set_bytes.max -le 0.0 -or
        [double]$Report.summary.gpu_buffer_bytes.max -le 0.0 -or
        [int64]$Report.summary.max_draw_calls -le 0 -or
        [int64]$Report.summary.max_triangles -le 0) {
        throw "Perf scenario '$ScenarioName' produced incomplete render or memory counters."
    }
}

function Assert-AuditRun {
    param(
        [Parameter(Mandatory)]
        [System.IO.DirectoryInfo]$RunDirectory,

        [Parameter(Mandatory)]
        [string]$ScenarioName
    )

    $requiredFiles = @(
        "manifest.json",
        "summary.json",
        "summary.txt",
        "events.jsonl",
        "seconds.jsonl",
        "frames.jsonl",
        "spikes.json"
    )

    foreach ($fileName in $requiredFiles) {
        $path = Join-Path $RunDirectory.FullName $fileName
        if (-not (Test-Path $path)) {
            throw "Perf scenario '$ScenarioName' is missing audit artifact '$path'."
        }
    }

    $manifest = Get-Content -Path (Join-Path $RunDirectory.FullName "manifest.json") -Raw | ConvertFrom-Json
    if ([string]$manifest.status -ne "completed") {
        throw "Perf scenario '$ScenarioName' produced audit status '$($manifest.status)'."
    }
}

function Get-Median {
    param([double[]]$Values)

    if (@($Values).Count -eq 0) {
        return 0.0
    }

    $sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) {
        return [double]$sorted[$middle]
    }
    return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
}

function Get-ScenarioThreshold {
    param([string]$ScenarioName)

    switch ($ScenarioName) {
    "world_stress" {
        return @{ P95 = 25.0; Max = 100.0; GpuP95 = 16.7; PrivateBytes = 1610612736.0 }
    }
    default {
        return @{ P95 = 16.7; Max = 75.0; GpuP95 = 16.7; PrivateBytes = 1073741824.0 }
    }
    }
}

function Get-MedianAbsoluteDeviation {
    param([double[]]$Values)

    if (@($Values).Count -eq 0) {
        return 0.0
    }
    $median = Get-Median -Values $Values
    return Get-Median -Values @($Values | ForEach-Object { [Math]::Abs([double]$_ - $median) })
}

function Assert-ScenarioThreshold {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$Summary
    )

    $threshold = Get-ScenarioThreshold -ScenarioName $Summary.scenario
    if ([double]$Summary.frame_p95 -gt [double]$threshold.P95) {
        throw "Perf scenario '$($Summary.scenario)' exceeded p95 threshold: $($Summary.frame_p95) ms > $($threshold.P95) ms."
    }
    if ([double]$Summary.frame_max -gt [double]$threshold.Max) {
        throw "Perf scenario '$($Summary.scenario)' exceeded max-frame threshold: $($Summary.frame_max) ms > $($threshold.Max) ms."
    }
    if ([double]$Summary.gpu_frame_p95 -gt [double]$threshold.GpuP95) {
        throw "Perf scenario '$($Summary.scenario)' exceeded GPU p95 threshold."
    }
    $maximumLongLagFrames = [Math]::Max(2, [Math]::Ceiling([double]$SmokeFrames * 0.01))
    if ([int]$Summary.lag_frames_33_3 -gt $maximumLongLagFrames) {
        throw "Perf scenario '$($Summary.scenario)' produced too many frames above 33.3 ms."
    }
    if ([double]$Summary.private_bytes_peak -gt [double]$threshold.PrivateBytes) {
        throw "Perf scenario '$($Summary.scenario)' exceeded private-memory threshold."
    }
}

function Assert-NoBaselineRegression {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$Current,

        [Parameter(Mandatory)]
        [pscustomobject]$Baseline,

        [Parameter(Mandatory)]
        [double]$AllowedPercent
    )

    foreach ($metric in @("frame_avg", "frame_p95", "gpu_frame_p95", "private_bytes_peak")) {
        $baselineValue = [double]$Baseline.$metric
        if ($baselineValue -le 0.0) {
            continue
        }
        $limit = $baselineValue * (1.0 + ($AllowedPercent / 100.0))
        if ([double]$Current.$metric -gt $limit) {
            throw "Perf scenario '$($Current.scenario)' regressed on '$metric': $($Current.$metric) > $limit."
        }
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "cmake-build-perf"
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

if ([string]::IsNullOrWhiteSpace($ArtifactDir)) {
    $runId = Get-Date -Format "yyyyMMdd-HHmmss"
    $ArtifactDir = Join-Path $repoRoot ("performancesaudit\perf-suite\" + $runId)
}
$ArtifactDir = [System.IO.Path]::GetFullPath($ArtifactDir)

$clionBinDir = Get-LatestClionBinDir
$cmakeFallback = if ($clionBinDir) { Join-Path $clionBinDir "cmake\win\x64\bin\cmake.exe" } else { $null }
$mingwBinDir = if ($clionBinDir) { Join-Path $clionBinDir "mingw\bin" } else { $null }
$ninjaDir = if ($clionBinDir) { Join-Path $clionBinDir "ninja\win\x64" } else { $null }
$cmakeFallbackDir = if ($cmakeFallback) { Split-Path -Parent $cmakeFallback } else { $null }

Add-ToPath -Entries @($mingwBinDir, $ninjaDir, $cmakeFallbackDir)
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

$isWindows = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
$gameExecutableName = if ($isWindows) { "ValCraft.exe" } else { "ValCraft" }
$gameExe = Join-Path (Join-Path $BuildDir "bin") $gameExecutableName
if (-not (Test-Path $gameExe)) {
    throw "ValCraft executable not found at '$gameExe'."
}

if (Test-Path $ArtifactDir) {
    Remove-Item -Path $ArtifactDir -Recurse -Force
}
New-Item -ItemType Directory -Path $ArtifactDir | Out-Null

$scenarioSummaries = @()
foreach ($scenarioName in $Scenarios) {
    $runs = @()
    for ($repetition = 1; $repetition -le $Repetitions; ++$repetition) {
        $outputPath = Join-Path $ArtifactDir ("{0}-run-{1}.json" -f $scenarioName, $repetition)
        $definition = Get-ScenarioDefinition `
            -Name $scenarioName `
            -SmokeFrames $SmokeFrames `
            -WarmupFrames $WarmupFrames `
            -Width $Width `
            -Height $Height `
            -OutputPath $outputPath `
            -AuditRoot $ArtifactDir `
            -AdaptiveQuality:$AdaptiveQuality `
            -Trace:$Trace
        Write-Host ("==> Running perf scenario '{0}' ({1}/{2})" -f $scenarioName, $repetition, $Repetitions)
        Invoke-External -FilePath $gameExe -Arguments $definition.Arguments -WorkingDirectory $BuildDir

        if (-not (Test-Path $outputPath)) {
            throw "Perf scenario '$scenarioName' did not produce '$outputPath'."
        }

        $report = Get-Content -Path $outputPath -Raw | ConvertFrom-Json
        Assert-ScenarioReport `
            -Report $report `
            -ScenarioName $scenarioName `
            -SmokeFrames $SmokeFrames `
            -WarmupFrames $WarmupFrames `
            -Width $Width `
            -Height $Height `
            -Configuration $Configuration `
            -AdaptiveQuality:$AdaptiveQuality `
            -Trace:$Trace
        $runDirectory = Find-LatestScenarioRun -AuditRoot $ArtifactDir -ScenarioName $scenarioName
        if ($null -eq $runDirectory) {
            throw "Perf scenario '$scenarioName' did not create an audit run directory."
        }
        Assert-AuditRun -RunDirectory $runDirectory -ScenarioName $scenarioName

        $runs += [PSCustomObject]@{
            repetition = $repetition
            frame_avg = [double]$report.summary.frame_total_ms.avg
            frame_p95 = [double]$report.summary.frame_total_ms.p95
            frame_p99 = [double]$report.summary.frame_total_ms.p99
            frame_max = [double]$report.summary.frame_total_ms.max
            gpu_frame_p95 = [double]$report.summary.gpu_frame_ms.p95
            private_bytes_peak = [double]$report.summary.process_private_bytes.max
            working_set_bytes_peak = [double]$report.summary.process_working_set_bytes.max
            lag_frames_16_7 = [int]$report.summary.lag_buckets.over_16_7_ms
            lag_frames_33_3 = [int]$report.summary.lag_buckets.over_33_3_ms
            lag_frames_50_0 = [int]$report.summary.lag_buckets.over_50_0_ms
            worst_frame_stage = [string]$report.hotspots.worst_frame_stage
            platform = [string]$report.metadata.platform
            build_type = [string]$report.metadata.build_type
            vsync_mode = [string]$report.metadata.vsync_mode
            spike_windows = @($report.spike_windows).Count
            run_directory = $runDirectory.FullName
            output = $outputPath
        }
    }

    $summary = [PSCustomObject]@{
        scenario = $scenarioName
        frame_avg = Get-Median -Values @($runs.frame_avg)
        frame_p95 = Get-Median -Values @($runs.frame_p95)
        frame_p99 = Get-Median -Values @($runs.frame_p99)
        frame_max = [double]($runs.frame_max | Measure-Object -Maximum).Maximum
        gpu_frame_p95 = Get-Median -Values @($runs.gpu_frame_p95)
        private_bytes_peak = [double]($runs.private_bytes_peak | Measure-Object -Maximum).Maximum
        working_set_bytes_peak = [double]($runs.working_set_bytes_peak | Measure-Object -Maximum).Maximum
        lag_frames_16_7 = [int]($runs.lag_frames_16_7 | Measure-Object -Maximum).Maximum
        lag_frames_33_3 = [int]($runs.lag_frames_33_3 | Measure-Object -Maximum).Maximum
        lag_frames_50_0 = [int]($runs.lag_frames_50_0 | Measure-Object -Maximum).Maximum
        frame_avg_mad = Get-MedianAbsoluteDeviation -Values @($runs.frame_avg)
        frame_p95_mad = Get-MedianAbsoluteDeviation -Values @($runs.frame_p95)
        platform = [string]$runs[0].platform
        build_type = [string]$runs[0].build_type
        vsync_mode = [string]$runs[0].vsync_mode
        repetitions = $runs
    }
    $scenarioSummaries += $summary
}

if ($EnforceThresholds) {
    foreach ($summary in $scenarioSummaries) {
        Assert-ScenarioThreshold -Summary $summary
    }
}

if (-not [string]::IsNullOrWhiteSpace($BaselinePath)) {
    $resolvedBaselinePath = [System.IO.Path]::GetFullPath($BaselinePath)
    if (-not (Test-Path $resolvedBaselinePath)) {
        throw "Baseline suite not found at '$resolvedBaselinePath'."
    }
    $baselineSuite = Get-Content -Path $resolvedBaselinePath -Raw | ConvertFrom-Json
    if ([int]$baselineSuite.schema_version -ne 2 -or
        [string]$baselineSuite.configuration -ne $Configuration -or
        [int]$baselineSuite.measured_frames -ne $SmokeFrames -or
        [int]$baselineSuite.warmup_frames -ne $WarmupFrames -or
        [int]$baselineSuite.viewport_width -ne $Width -or
        [int]$baselineSuite.viewport_height -ne $Height -or
        [string]$baselineSuite.platform -ne [string]$scenarioSummaries[0].platform -or
        [string]$baselineSuite.build_type -ne [string]$scenarioSummaries[0].build_type -or
        [bool]$baselineSuite.adaptive_quality -ne [bool]$AdaptiveQuality) {
        throw "Baseline suite metadata is incompatible with the current performance run."
    }
    foreach ($summary in $scenarioSummaries) {
        $baseline = @($baselineSuite.scenarios | Where-Object { $_.scenario -eq $summary.scenario }) | Select-Object -First 1
        if ($null -eq $baseline) {
            throw "Baseline suite does not contain scenario '$($summary.scenario)'."
        }
        Assert-NoBaselineRegression -Current $summary -Baseline $baseline -AllowedPercent $MaxRegressionPercent
    }
}

Write-Host "==> Performance suite summary"
$scenarioSummaries |
    Sort-Object scenario |
    Format-Table scenario, frame_avg, frame_p95, frame_p99, frame_max, gpu_frame_p95, private_bytes_peak, lag_frames_16_7, lag_frames_33_3 -AutoSize |
    Out-String |
    Write-Host

$suiteSummary = [PSCustomObject]@{
    schema_version = 2
    configuration = $Configuration
    measured_frames = $SmokeFrames
    warmup_frames = $WarmupFrames
    repetitions = $Repetitions
    viewport_width = $Width
    viewport_height = $Height
    trace_enabled = [bool]$Trace
    adaptive_quality = [bool]$AdaptiveQuality
    platform = if ($scenarioSummaries.Count -gt 0) { [string]$scenarioSummaries[0].platform } else { "unknown" }
    build_type = if ($scenarioSummaries.Count -gt 0) { [string]$scenarioSummaries[0].build_type } else { "unknown" }
    thresholds_enforced = [bool]$EnforceThresholds
    baseline_path = $BaselinePath
    max_regression_percent = $MaxRegressionPercent
    build_dir = $BuildDir
    artifact_dir = $ArtifactDir
    scenarios = $scenarioSummaries
}

$suiteSummaryPath = Join-Path $ArtifactDir "suite-summary.json"
$suiteSummary | ConvertTo-Json -Depth 8 | Set-Content -Path $suiteSummaryPath -Encoding UTF8
Write-Host ("==> Suite summary JSON: {0}" -f $suiteSummaryPath)
