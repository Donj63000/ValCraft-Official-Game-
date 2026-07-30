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
    [string[]]$Scenarios = @(
        "baseline",
        "world_stress",
        "sea-open",
        "sea_tempest",
        "terrain_edit_stress",
        "port_dense"
    ),
    [switch]$Trace,
    [switch]$AdaptiveQuality,
    [switch]$EnforceThresholds,
    [string]$BaselinePath = "",
    [ValidateRange(0.0, 100.0)]
    [double]$MaxRegressionPercent = 8.0,
    [switch]$NoConfigure,
    [switch]$NoBuild,
    [string]$ArtifactDir = "",
    [switch]$SelfTest
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
        "--visual-pipeline=modern",
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
    "sea-open" {
        # Je mesure une vraie session V3 déjà en route, assez loin du port pour
        # isoler l'eau, l'horizon et le sillage sans chargement de quai.
        $arguments += "--stream-radius=5"
        $arguments += "--smoke-session=sea-open"
        $arguments += "--smoke-ship-view=stern"
        $arguments += "--initial-time=10.5"
        $arguments += "--initial-weather-time=0"
    }
    "terrain_edit_stress" {
        # Le jeu reconnait ce libelle et rejoue une suite deterministe de
        # poses/casses aux frontieres afin de mesurer le remeshing organique.
        $arguments += "--stream-radius=5"
    }
    "port_dense" {
        # Je place la camera sur le pont pour conserver simultanement le port,
        # le navire, ses structures et son equipage dans le budget graphique.
        $arguments += "--stream-radius=6"
        $arguments += "--smoke-session=sea-new"
        $arguments += "--smoke-ship-view=deck"
        $arguments += "--initial-time=16.5"
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

function Test-MaterialPackChecksum {
    param([string]$Checksum)

    return $Checksum -match '^0x[0-9A-Fa-f]{16}$' -and
           $Checksum -ne "0x0000000000000000"
}

function Get-WaterSurfaceP95 {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$Report
    )

    if ($null -eq $Report.summary) {
        return 0.0
    }

    # Je prefere la passe de surface moderne. Je ne replie sur l'ancien
    # agregat que lorsque le champ append-only est absent du rapport.
    $surfaceProperty =
        $Report.summary.PSObject.Properties["gpu_water_surface_ms"]
    if ($null -ne $surfaceProperty -and
        $null -ne $surfaceProperty.Value) {
        $p95Property =
            $surfaceProperty.Value.PSObject.Properties["p95"]
        if ($null -ne $p95Property -and
            $null -ne $p95Property.Value) {
            return [double]$p95Property.Value
        }
    }

    $legacyProperty =
        $Report.summary.PSObject.Properties["gpu_water_ms"]
    if ($null -eq $legacyProperty -or
        $null -eq $legacyProperty.Value) {
        return 0.0
    }
    $legacyP95 =
        $legacyProperty.Value.PSObject.Properties["p95"]
    if ($null -eq $legacyP95 -or
        $null -eq $legacyP95.Value) {
        return 0.0
    }
    return [double]$legacyP95.Value
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

    if ([int]$Report.schema_version -ne 3) {
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
    if ([string]$Report.metadata.visual_pipeline -ne "modern") {
        throw "Perf scenario '$ScenarioName' did not execute the required modern visual pipeline."
    }
    if ([int64]$Report.metadata.material_pack_version -le 0) {
        throw "Perf scenario '$ScenarioName' did not report a valid material pack version."
    }
    $materialPackChecksum =
        [string]$Report.metadata.material_pack_checksum
    if (-not (Test-MaterialPackChecksum -Checksum $materialPackChecksum)) {
        throw "Perf scenario '$ScenarioName' did not report a valid material pack checksum."
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
    if ($ScenarioName -in @("sea-open", "sea_tempest", "port_dense") -and
        (Get-WaterSurfaceP95 -Report $Report) -le 0.0) {
        throw "Perf scenario '$ScenarioName' did not produce a valid water GPU timing."
    }
    if ([double]$Report.summary.process_private_bytes.max -le 0.0 -or
        [double]$Report.summary.process_working_set_bytes.max -le 0.0 -or
        [double]$Report.summary.gpu_buffer_bytes.max -le 0.0 -or
        [int64]$Report.summary.max_draw_calls -le 0 -or
        [int64]$Report.summary.max_triangles -le 0) {
        throw "Perf scenario '$ScenarioName' produced incomplete render or memory counters."
    }
    if ($ScenarioName -eq "terrain_edit_stress" -and
        ($null -eq $Report.event_summary -or
         [int64]$Report.event_summary.block_breaks -le 0 -or
         [int64]$Report.event_summary.block_places -le 0 -or
         [int64]$Report.event_summary.total -lt
             ([int64]$Report.event_summary.block_breaks +
              [int64]$Report.event_summary.block_places))) {
        throw "Perf scenario '$ScenarioName' did not execute deterministic block edits."
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
        return @{
            P95 = 16.7
            GpuP95 = 9.0
            PrivateBytes = 1610612736.0
            TextureBytes = 134217728.0
            BufferBytes = 201326592.0
            DrawCalls = 380
            Triangles = 1500000
            MaximumLongLagFrames = [Math]::Max(2, [Math]::Ceiling([double]$SmokeFrames * 0.01))
        }
    }
    "terrain_edit_stress" {
        return @{
            P95 = 16.7
            GpuP95 = 9.0
            PrivateBytes = 1610612736.0
            TextureBytes = 134217728.0
            BufferBytes = 201326592.0
            DrawCalls = 380
            Triangles = 1500000
            MaximumLongLagFrames = [Math]::Max(2, [Math]::Ceiling([double]$SmokeFrames * 0.01))
        }
    }
    "sea_tempest" {
        return @{
            P95 = 13.0
            GpuP95 = 9.0
            WaterP95 = 2.0
            PrivateBytes = 671088640.0
            TextureBytes = 134217728.0
            BufferBytes = 33554432.0
            DrawCalls = 100
            Triangles = 350000
            MaximumLongLagFrames = 2
        }
    }
    "port_dense" {
        return @{
            P95 = 13.0
            GpuP95 = 9.0
            WaterP95 = 2.0
            PrivateBytes = 671088640.0
            TextureBytes = 134217728.0
            BufferBytes = 33554432.0
            DrawCalls = 100
            Triangles = 350000
            MaximumLongLagFrames = 2
        }
    }
    "sea-open" {
        return @{
            P95 = 13.0
            GpuP95 = 9.0
            WaterP95 = 2.0
            PrivateBytes = 671088640.0
            TextureBytes = 134217728.0
            BufferBytes = 33554432.0
            DrawCalls = 100
            Triangles = 350000
            MaximumLongLagFrames = 2
        }
    }
    default {
        return @{
            P95 = 8.0
            GpuP95 = 5.0
            PrivateBytes = 536870912.0
            TextureBytes = 134217728.0
            BufferBytes = 33554432.0
            DrawCalls = 80
            Triangles = 300000
            MaximumLongLagFrames = 2
        }
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
    if ([double]$Summary.gpu_frame_p95 -gt [double]$threshold.GpuP95) {
        throw "Perf scenario '$($Summary.scenario)' exceeded GPU p95 threshold: $($Summary.gpu_frame_p95) ms > $($threshold.GpuP95) ms."
    }
    if ($threshold.ContainsKey("WaterP95") -and
        [double]$Summary.gpu_water_p95 -gt
            [double]$threshold.WaterP95) {
        throw "Perf scenario '$($Summary.scenario)' exceeded water GPU p95 threshold: $($Summary.gpu_water_p95) ms > $($threshold.WaterP95) ms."
    }
    if ([int]$Summary.lag_frames_33_3 -gt [int]$threshold.MaximumLongLagFrames) {
        throw "Perf scenario '$($Summary.scenario)' produced too many frames above 33.3 ms."
    }
    if ([int]$Summary.dropped_fixed_updates -ne 0) {
        throw "Perf scenario '$($Summary.scenario)' lost $($Summary.dropped_fixed_updates) fixed updates."
    }
    if ([double]$Summary.private_bytes_peak -gt [double]$threshold.PrivateBytes) {
        throw "Perf scenario '$($Summary.scenario)' exceeded private-memory threshold."
    }
    if ([double]$Summary.gpu_texture_bytes_peak -gt [double]$threshold.TextureBytes) {
        throw "Perf scenario '$($Summary.scenario)' exceeded GPU texture-memory threshold."
    }
    if ([double]$Summary.gpu_buffer_bytes_peak -gt [double]$threshold.BufferBytes) {
        throw "Perf scenario '$($Summary.scenario)' exceeded GPU buffer-memory threshold."
    }
    if ([int64]$Summary.draw_calls_peak -gt [int64]$threshold.DrawCalls) {
        throw "Perf scenario '$($Summary.scenario)' exceeded draw-call threshold."
    }
    if ([int64]$Summary.triangles_peak -gt [int64]$threshold.Triangles) {
        throw "Perf scenario '$($Summary.scenario)' exceeded triangle threshold."
    }
}

function Get-OptionalDoubleProperty {
    param(
        [Parameter(Mandatory)]
        [psobject]$Object,

        [Parameter(Mandatory)]
        [string]$Name
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return 0.0
    }
    return [double]$property.Value
}

function Assert-NoBaselineRegression {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$Current,

        [Parameter(Mandatory)]
        [pscustomobject]$Baseline,

        [Parameter(Mandatory)]
        [double]$FallbackAllowedPercent
    )

    $policies = @(
        @{ Metric = "frame_avg"; Mad = "frame_avg_mad"; Percent = 3.0 },
        @{ Metric = "frame_p95"; Mad = "frame_p95_mad"; Percent = 3.0 },
        @{ Metric = "gpu_frame_p95"; Mad = "gpu_frame_p95_mad"; Percent = 3.0 },
        @{ Metric = "gpu_water_p95"; Mad = "gpu_water_p95_mad"; Percent = 8.0 },
        @{ Metric = "frame_p99"; Mad = "frame_p99_mad"; Percent = 5.0 },
        @{ Metric = "meshing_p95"; Mad = "meshing_p95_mad"; Percent = 5.0 },
        @{ Metric = "upload_p95"; Mad = "upload_p95_mad"; Percent = 5.0 }
    )
    foreach ($policy in $policies) {
        $metric = [string]$policy.Metric
        $baselineValue =
            Get-OptionalDoubleProperty `
                -Object $Baseline `
                -Name $metric
        if ($baselineValue -le 0.0) {
            continue
        }
        $percent = [Math]::Min([double]$FallbackAllowedPercent, [double]$policy.Percent)
        $percentAllowance = $baselineValue * ($percent / 100.0)
        $madProperty = [string]$policy.Mad
        $baselineMad =
            Get-OptionalDoubleProperty `
                -Object $Baseline `
                -Name $madProperty
        $currentMad =
            Get-OptionalDoubleProperty `
                -Object $Current `
                -Name $madProperty
        $noiseAllowance = 3.0 * [Math]::Max($baselineMad, $currentMad)
        $limit = $baselineValue + [Math]::Max($percentAllowance, $noiseAllowance)
        $currentValue =
            Get-OptionalDoubleProperty `
                -Object $Current `
                -Name $metric
        if ($currentValue -gt $limit) {
            throw "Perf scenario '$($Current.scenario)' regressed on '$metric': $currentValue > $limit."
        }
    }

    if ([int]$Current.dropped_fixed_updates -ne 0) {
        throw "Perf scenario '$($Current.scenario)' lost fixed updates compared with the baseline."
    }
}

function Test-CompatibleBaselineSuiteMetadata {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$BaselineSuite,

        [Parameter(Mandatory)]
        [pscustomobject]$CurrentSummary,

        [Parameter(Mandatory)]
        [string]$Configuration,

        [Parameter(Mandatory)]
        [int]$MeasuredFrames,

        [Parameter(Mandatory)]
        [int]$WarmupFrames,

        [Parameter(Mandatory)]
        [int]$Width,

        [Parameter(Mandatory)]
        [int]$Height,

        [Parameter(Mandatory)]
        [bool]$AdaptiveQuality
    )

    # Je compare uniquement l'environnement de mesure. L'identite artistique
    # legacy/modern et son pack restent tracees dans chaque suite sans bloquer
    # la reference historique verrouillee.
    return [int]$BaselineSuite.schema_version -in @(2, 3) -and
           [string]$BaselineSuite.configuration -eq $Configuration -and
           [int]$BaselineSuite.measured_frames -eq $MeasuredFrames -and
           [int]$BaselineSuite.warmup_frames -eq $WarmupFrames -and
           [int]$BaselineSuite.viewport_width -eq $Width -and
           [int]$BaselineSuite.viewport_height -eq $Height -and
           [string]$BaselineSuite.platform -eq
               [string]$CurrentSummary.platform -and
           [string]$BaselineSuite.build_type -eq
               [string]$CurrentSummary.build_type -and
           [bool]$BaselineSuite.adaptive_quality -eq $AdaptiveQuality
}

function Invoke-PerfScriptSelfTest {
    $definition =
        Get-ScenarioDefinition `
            -Name "terrain_edit_stress" `
            -SmokeFrames 24 `
            -WarmupFrames 8 `
            -Width 1920 `
            -Height 1080 `
            -OutputPath "perf-self-test.json" `
            -AuditRoot "perf-self-test"
    if (@($definition.Arguments) -notcontains "--visual-pipeline=modern") {
        throw "Perf self-test: modern visual pipeline argument is missing."
    }
    $openSeaDefinition =
        Get-ScenarioDefinition `
            -Name "sea-open" `
            -SmokeFrames 24 `
            -WarmupFrames 8 `
            -Width 1920 `
            -Height 1080 `
            -OutputPath "perf-self-test-sea-open.json" `
            -AuditRoot "perf-self-test"
    if (@($openSeaDefinition.Arguments) -notcontains
            "--smoke-session=sea-open" -or
        @($openSeaDefinition.Arguments) -notcontains
            "--smoke-ship-view=stern") {
        throw "Perf self-test: sea-open is not an underway stern fixture."
    }
    $openSeaThreshold =
        Get-ScenarioThreshold `
            -ScenarioName "sea-open"
    if ([double]$openSeaThreshold.WaterP95 -ne 2.0) {
        throw "Perf self-test: sea-open water p95 target is not 2.0 ms."
    }
    if ((Get-MedianAbsoluteDeviation -Values @(1.0, 1.5, 2.0)) -ne 0.5) {
        throw "Perf self-test: median absolute deviation is inconsistent."
    }
    if (-not (Test-MaterialPackChecksum -Checksum "0x0123456789abcdef") -or
        (Test-MaterialPackChecksum -Checksum "0x0000000000000000") -or
        (Test-MaterialPackChecksum -Checksum "invalid")) {
        throw "Perf self-test: material pack checksum validation is inconsistent."
    }
    $splitWaterReport = [PSCustomObject]@{
        summary = [PSCustomObject]@{
            gpu_water_surface_ms = [PSCustomObject]@{ p95 = 1.25 }
            gpu_water_ms = [PSCustomObject]@{ p95 = 4.50 }
        }
    }
    if ((Get-WaterSurfaceP95 -Report $splitWaterReport) -ne 1.25) {
        throw "Perf self-test: split water surface timing was not preferred."
    }
    $legacyWaterReport = [PSCustomObject]@{
        summary = [PSCustomObject]@{
            gpu_water_ms = [PSCustomObject]@{ p95 = 1.75 }
        }
    }
    if ((Get-WaterSurfaceP95 -Report $legacyWaterReport) -ne 1.75) {
        throw "Perf self-test: legacy water timing fallback is inconsistent."
    }

    $current = [PSCustomObject]@{
        scenario = "baseline"
        frame_avg = 10.1
        frame_p95 = 11.1
        gpu_frame_p95 = 4.1
        gpu_water_p95 = 1.05
        gpu_water_p95_mad = 0.01
        frame_p99 = 12.1
        meshing_p95 = 1.0
        upload_p95 = 1.0
        dropped_fixed_updates = 0
        platform = "self-test"
        build_type = "Release"
    }
    $schema2Scenario = [PSCustomObject]@{
        scenario = "baseline"
        frame_avg = 10.0
        frame_p95 = 11.0
        gpu_frame_p95 = 4.0
        frame_p99 = 12.0
    }
    # Ce scenario v2 omet volontairement MAD, remeshing et uploads : leur
    # normalisation a zero doit garder la reference e182e40 exploitable.
    Assert-NoBaselineRegression `
        -Current $current `
        -Baseline $schema2Scenario `
        -FallbackAllowedPercent 8.0
    $waterBaseline = [PSCustomObject]@{
        scenario = "baseline"
        gpu_water_p95 = 1.0
        gpu_water_p95_mad = 0.01
    }
    Assert-NoBaselineRegression `
        -Current $current `
        -Baseline $waterBaseline `
        -FallbackAllowedPercent 8.0

    $schema2Suite = [PSCustomObject]@{
        schema_version = 2
        configuration = "Release"
        measured_frames = 720
        warmup_frames = 180
        viewport_width = 1920
        viewport_height = 1080
        platform = "self-test"
        build_type = "Release"
        adaptive_quality = $false
    }
    if (-not (Test-CompatibleBaselineSuiteMetadata `
            -BaselineSuite $schema2Suite `
            -CurrentSummary $current `
            -Configuration "Release" `
            -MeasuredFrames 720 `
            -WarmupFrames 180 `
            -Width 1920 `
            -Height 1080 `
            -AdaptiveQuality $false)) {
        throw "Perf self-test: a compatible schema v2 baseline was rejected."
    }
    $schema2Suite.schema_version = 1
    if (Test-CompatibleBaselineSuiteMetadata `
            -BaselineSuite $schema2Suite `
            -CurrentSummary $current `
            -Configuration "Release" `
            -MeasuredFrames 720 `
            -WarmupFrames 180 `
            -Width 1920 `
            -Height 1080 `
            -AdaptiveQuality $false) {
        throw "Perf self-test: an unsupported baseline schema was accepted."
    }

    Write-Host "perf.ps1 self-test: OK"
}

if ($SelfTest) {
    Invoke-PerfScriptSelfTest
    return
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
            gpu_water_p95 = Get-WaterSurfaceP95 -Report $report
            meshing_p95 = [double]$report.summary.meshing_ms.p95
            upload_p95 = [double]$report.summary.upload_ms.p95
            private_bytes_peak = [double]$report.summary.process_private_bytes.max
            working_set_bytes_peak = [double]$report.summary.process_working_set_bytes.max
            gpu_buffer_bytes_peak = [double]$report.summary.gpu_buffer_bytes.max
            gpu_texture_bytes_peak = [double]$report.summary.gpu_texture_bytes.max
            draw_calls_peak = [int64]$report.summary.max_draw_calls
            triangles_peak = [int64]$report.summary.max_triangles
            dropped_fixed_updates = [int64]$report.summary.total_dropped_fixed_updates
            lag_frames_16_7 = [int]$report.summary.lag_buckets.over_16_7_ms
            lag_frames_33_3 = [int]$report.summary.lag_buckets.over_33_3_ms
            lag_frames_50_0 = [int]$report.summary.lag_buckets.over_50_0_ms
            worst_frame_stage = [string]$report.hotspots.worst_frame_stage
            platform = [string]$report.metadata.platform
            build_type = [string]$report.metadata.build_type
            vsync_mode = [string]$report.metadata.vsync_mode
            visual_pipeline = [string]$report.metadata.visual_pipeline
            material_pack_version = [int64]$report.metadata.material_pack_version
            material_pack_checksum = [string]$report.metadata.material_pack_checksum
            spike_windows = @($report.spike_windows).Count
            run_directory = $runDirectory.FullName
            output = $outputPath
        }
    }

    $runVisualPipelines =
        @($runs | ForEach-Object { $_.visual_pipeline } | Sort-Object -Unique)
    $runMaterialPackVersions =
        @($runs | ForEach-Object { $_.material_pack_version } | Sort-Object -Unique)
    $runMaterialPackChecksums =
        @($runs | ForEach-Object { $_.material_pack_checksum } | Sort-Object -Unique)
    if ($runVisualPipelines.Count -ne 1 -or
        $runMaterialPackVersions.Count -ne 1 -or
        $runMaterialPackChecksums.Count -ne 1) {
        throw "Perf scenario '$scenarioName' changed visual material metadata between repetitions."
    }

    $summary = [PSCustomObject]@{
        scenario = $scenarioName
        frame_avg = Get-Median -Values @($runs.frame_avg)
        frame_p95 = Get-Median -Values @($runs.frame_p95)
        frame_p99 = Get-Median -Values @($runs.frame_p99)
        frame_max = [double]($runs.frame_max | Measure-Object -Maximum).Maximum
        gpu_frame_p95 = Get-Median -Values @($runs.gpu_frame_p95)
        gpu_water_p95 = Get-Median -Values @($runs.gpu_water_p95)
        meshing_p95 = Get-Median -Values @($runs.meshing_p95)
        upload_p95 = Get-Median -Values @($runs.upload_p95)
        private_bytes_peak = [double]($runs.private_bytes_peak | Measure-Object -Maximum).Maximum
        working_set_bytes_peak = [double]($runs.working_set_bytes_peak | Measure-Object -Maximum).Maximum
        gpu_buffer_bytes_peak = [double]($runs.gpu_buffer_bytes_peak | Measure-Object -Maximum).Maximum
        gpu_texture_bytes_peak = [double]($runs.gpu_texture_bytes_peak | Measure-Object -Maximum).Maximum
        draw_calls_peak = [int64]($runs.draw_calls_peak | Measure-Object -Maximum).Maximum
        triangles_peak = [int64]($runs.triangles_peak | Measure-Object -Maximum).Maximum
        dropped_fixed_updates = [int64]($runs.dropped_fixed_updates | Measure-Object -Maximum).Maximum
        lag_frames_16_7 = [int]($runs.lag_frames_16_7 | Measure-Object -Maximum).Maximum
        lag_frames_33_3 = [int]($runs.lag_frames_33_3 | Measure-Object -Maximum).Maximum
        lag_frames_50_0 = [int]($runs.lag_frames_50_0 | Measure-Object -Maximum).Maximum
        frame_avg_mad = Get-MedianAbsoluteDeviation -Values @($runs.frame_avg)
        frame_p95_mad = Get-MedianAbsoluteDeviation -Values @($runs.frame_p95)
        frame_p99_mad = Get-MedianAbsoluteDeviation -Values @($runs.frame_p99)
        gpu_frame_p95_mad = Get-MedianAbsoluteDeviation -Values @($runs.gpu_frame_p95)
        gpu_water_p95_mad = Get-MedianAbsoluteDeviation -Values @($runs.gpu_water_p95)
        meshing_p95_mad = Get-MedianAbsoluteDeviation -Values @($runs.meshing_p95)
        upload_p95_mad = Get-MedianAbsoluteDeviation -Values @($runs.upload_p95)
        platform = [string]$runs[0].platform
        build_type = [string]$runs[0].build_type
        vsync_mode = [string]$runs[0].vsync_mode
        visual_pipeline = [string]$runs[0].visual_pipeline
        material_pack_version = [int64]$runs[0].material_pack_version
        material_pack_checksum = [string]$runs[0].material_pack_checksum
        repetitions = $runs
    }
    $scenarioSummaries += $summary
}

$suiteVisualPipelines =
    @($scenarioSummaries | ForEach-Object { $_.visual_pipeline } | Sort-Object -Unique)
$suiteMaterialPackVersions =
    @($scenarioSummaries | ForEach-Object { $_.material_pack_version } | Sort-Object -Unique)
$suiteMaterialPackChecksums =
    @($scenarioSummaries | ForEach-Object { $_.material_pack_checksum } | Sort-Object -Unique)
if ($suiteVisualPipelines.Count -ne 1 -or
    $suiteMaterialPackVersions.Count -ne 1 -or
    $suiteMaterialPackChecksums.Count -ne 1) {
    throw "Performance scenarios did not use one stable modern material pack."
}

if ($EnforceThresholds) {
    foreach ($summary in $scenarioSummaries) {
        Assert-ScenarioThreshold -Summary $summary
    }
}

$baselineMetadata = $null
if (-not [string]::IsNullOrWhiteSpace($BaselinePath)) {
    $resolvedBaselinePath = [System.IO.Path]::GetFullPath($BaselinePath)
    if (-not (Test-Path $resolvedBaselinePath)) {
        throw "Baseline suite not found at '$resolvedBaselinePath'."
    }
    $baselineSuite = Get-Content -Path $resolvedBaselinePath -Raw | ConvertFrom-Json
    $baselineVisualPipeline =
        $baselineSuite.PSObject.Properties["visual_pipeline"]
    $baselineMaterialPackVersion =
        $baselineSuite.PSObject.Properties["material_pack_version"]
    $baselineMaterialPackChecksum =
        $baselineSuite.PSObject.Properties["material_pack_checksum"]
    $baselineMetadata = [PSCustomObject]@{
        schema_version = [int]$baselineSuite.schema_version
        visual_pipeline =
            if ($null -ne $baselineVisualPipeline) {
                [string]$baselineVisualPipeline.Value
            } else {
                "unreported"
            }
        material_pack_version =
            if ($null -ne $baselineMaterialPackVersion) {
                [int64]$baselineMaterialPackVersion.Value
            } else {
                0
            }
        material_pack_checksum =
            if ($null -ne $baselineMaterialPackChecksum) {
                [string]$baselineMaterialPackChecksum.Value
            } else {
                "unreported"
            }
    }
    if (-not (Test-CompatibleBaselineSuiteMetadata `
            -BaselineSuite $baselineSuite `
            -CurrentSummary $scenarioSummaries[0] `
            -Configuration $Configuration `
            -MeasuredFrames $SmokeFrames `
            -WarmupFrames $WarmupFrames `
            -Width $Width `
            -Height $Height `
            -AdaptiveQuality ([bool]$AdaptiveQuality))) {
        throw "Baseline suite metadata is incompatible with the current performance run."
    }
    foreach ($summary in $scenarioSummaries) {
        $baseline = @($baselineSuite.scenarios | Where-Object { $_.scenario -eq $summary.scenario }) | Select-Object -First 1
        if ($null -eq $baseline) {
            throw "Baseline suite does not contain scenario '$($summary.scenario)'."
        }
        Assert-NoBaselineRegression -Current $summary -Baseline $baseline -FallbackAllowedPercent $MaxRegressionPercent
    }
}

Write-Host "==> Performance suite summary"
$scenarioSummaries |
    Sort-Object scenario |
    Format-Table scenario, frame_avg, frame_p95, frame_p99, gpu_frame_p95, gpu_water_p95, meshing_p95, upload_p95, private_bytes_peak, lag_frames_33_3 -AutoSize |
    Out-String |
    Write-Host

$suiteSummary = [PSCustomObject]@{
    schema_version = 3
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
    visual_pipeline = [string]$scenarioSummaries[0].visual_pipeline
    material_pack_version = [int64]$scenarioSummaries[0].material_pack_version
    material_pack_checksum = [string]$scenarioSummaries[0].material_pack_checksum
    thresholds_enforced = [bool]$EnforceThresholds
    baseline_path = $BaselinePath
    baseline_visual_metadata = $baselineMetadata
    max_regression_percent = $MaxRegressionPercent
    build_dir = $BuildDir
    artifact_dir = $ArtifactDir
    scenarios = $scenarioSummaries
}

$suiteSummaryPath = Join-Path $ArtifactDir "suite-summary.json"
$suiteSummary | ConvertTo-Json -Depth 8 | Set-Content -Path $suiteSummaryPath -Encoding UTF8
Write-Host ("==> Suite summary JSON: {0}" -f $suiteSummaryPath)
