[CmdletBinding()]
param(
    [int]$MinimumTests = 20,
    [double]$CriticalCoverageThreshold = 80.0,
    [double]$MarlowBranchCoverageThreshold = 70.0,
    [int]$SmokeFrames = 60
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

function Invoke-ExternalCapture {
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
        $output = & $FilePath @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        if ($WorkingDirectory) {
            Pop-Location
        }
    }

    if ($exitCode -ne 0) {
        $joinedOutput = ($output -join [Environment]::NewLine)
        throw "Command failed with exit code ${exitCode}: $FilePath`n$joinedOutput"
    }

    return @($output)
}

function Get-DiscoveredTestCount {
    param(
        [Parameter(Mandatory)]
        [string]$CTestExe,

        [Parameter(Mandatory)]
        [string]$BuildDir
    )

    $output = Invoke-ExternalCapture -FilePath $CTestExe -Arguments @("--test-dir", $BuildDir, "-N")
    $joinedOutput = $output -join [Environment]::NewLine
    $match = [regex]::Match($joinedOutput, "Total Tests:\s*(\d+)")
    if (-not $match.Success) {
        throw "Unable to determine discovered test count from ctest output."
    }

    return [int]$match.Groups[1].Value
}

function Get-GcovSummary {
    param(
        [Parameter(Mandatory)]
        [string]$GcovExe,

        [Parameter(Mandatory)]
        [string]$CoverageWorkingDirectory,

        [Parameter(Mandatory)]
        [string]$ObjectDirectory,

        [Parameter(Mandatory)]
        [string]$CoverageInputPath,

        [Parameter(Mandatory)]
        [string]$SourcePath
    )

    $output = Invoke-ExternalCapture -FilePath $GcovExe -Arguments @("-b", "-c", "-o", $ObjectDirectory, $CoverageInputPath) -WorkingDirectory $CoverageWorkingDirectory
    $text = ($output -join [Environment]::NewLine).Replace('\', '/')
    $normalizedSource = $SourcePath.Replace('\', '/')

    $options = [System.Text.RegularExpressions.RegexOptions]::Singleline
    $pattern = "File '$([regex]::Escape($normalizedSource))'(?<section>.*?)(?=File '|$)"
    $match = [regex]::Match($text, $pattern, $options)

    if (-not $match.Success) {
        $fallbackPattern = "File '.*$([regex]::Escape([System.IO.Path]::GetFileName($SourcePath)))'(?<section>.*?)(?=File '|$)"
        $match = [regex]::Match($text, $fallbackPattern, $options)
    }

    if (-not $match.Success) {
        throw "Unable to extract gcov summary for '$SourcePath'."
    }

    $section = $match.Groups["section"].Value
    $lineMatch = [regex]::Match(
        $section,
        "Lines executed:(?<percent>[0-9.]+)% of (?<lines>\d+)")
    if (-not $lineMatch.Success) {
        throw "Unable to extract gcov line summary for '$SourcePath'."
    }
    $branchMatch = [regex]::Match(
        $section,
        "Taken at least once:(?<percent>[0-9.]+)% of (?<branches>\d+)")

    $lines = [int]$lineMatch.Groups["lines"].Value
    $percent = [double]::Parse(
        $lineMatch.Groups["percent"].Value,
        [System.Globalization.CultureInfo]::InvariantCulture)
    $branches = if ($branchMatch.Success) {
        [int]$branchMatch.Groups["branches"].Value
    } else {
        0
    }
    $branchPercent = if ($branchMatch.Success) {
        [double]::Parse(
            $branchMatch.Groups["percent"].Value,
            [System.Globalization.CultureInfo]::InvariantCulture)
    } else {
        0.0
    }

    return [PSCustomObject]@{
        Source = $SourcePath
        Lines = $lines
        Executed = ($percent / 100.0) * $lines
        Branches = $branches
        TakenBranches = ($branchPercent / 100.0) * $branches
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$runId = Get-Date -Format "yyyyMMddHHmmss"
$strictBuildDir = Join-Path $repoRoot "cmake-build-strict-$runId"
$coverageBuildDir = Join-Path $repoRoot "cmake-build-coverage-$runId"
$coverageReportDir = Join-Path $coverageBuildDir "coverage-report"

$clionBinDir = Get-LatestClionBinDir
$cmakeFallback = if ($clionBinDir) { Join-Path $clionBinDir "cmake\win\x64\bin\cmake.exe" } else { $null }
$ctestFallback = if ($clionBinDir) { Join-Path $clionBinDir "cmake\win\x64\bin\ctest.exe" } else { $null }
$mingwBinDir = if ($clionBinDir) { Join-Path $clionBinDir "mingw\bin" } else { $null }
$ninjaDir = if ($clionBinDir) { Join-Path $clionBinDir "ninja\win\x64" } else { $null }
$gcovFallback = if ($mingwBinDir) { Join-Path $mingwBinDir "gcov.exe" } else { $null }

Add-ToPath -Entries @($mingwBinDir, $ninjaDir, (Split-Path -Parent $cmakeFallback))

$cmakeExe = Resolve-ToolPath -CommandName "cmake" -FallbackPaths @($cmakeFallback)
$ctestExe = Resolve-ToolPath -CommandName "ctest" -FallbackPaths @($ctestFallback)
$gcovExe = Resolve-ToolPath -CommandName "gcov" -FallbackPaths @($gcovFallback)

Write-Host "==> Preparing fresh strict and coverage build directories"
foreach ($buildDir in @($strictBuildDir, $coverageBuildDir)) {
    if (Test-Path $buildDir) {
        Remove-Item -Path $buildDir -Recurse -Force
    }
}

Write-Host "==> Configuring strict build"
Invoke-External -FilePath $cmakeExe -Arguments @(
    "-S", $repoRoot,
    "-B", $strictBuildDir,
    "-G", "Ninja",
    "-DVALCRAFT_STRICT_WARNINGS=ON",
    "-DVALCRAFT_ENABLE_COVERAGE=OFF"
)

Write-Host "==> Building strict gate targets"
Invoke-External -FilePath $cmakeExe -Arguments @("--build", $strictBuildDir, "--target", "valcraft_check_build", "--parallel")

Write-Host "==> Running discovered tests"
Invoke-External -FilePath $cmakeExe -Arguments @("--build", $strictBuildDir, "--target", "valcraft_check_tests", "--parallel")

Write-Host "==> Replaying Marlow tests in deterministic random orders"
Invoke-External -FilePath $cmakeExe -Arguments @("--build", $strictBuildDir, "--target", "valcraft_check_marlow", "--parallel")

$testCount = Get-DiscoveredTestCount -CTestExe $ctestExe -BuildDir $strictBuildDir
Write-Host ("==> Discovered tests: {0}" -f $testCount)
if ($testCount -lt $MinimumTests) {
    throw "Strict gate failed: expected at least $MinimumTests tests, found $testCount."
}

Write-Host "==> Running smoke mode across day-cycle checkpoints"
$smokeScenarios = @(
    @{ Name = "legacy-day"; Arguments = @("--smoke-test", "--smoke-frames=$SmokeFrames", "--hidden-window", "--visual-pipeline=legacy", "--initial-time=8") },
    @{ Name = "modern-day"; Arguments = @("--smoke-test", "--smoke-frames=$SmokeFrames", "--hidden-window", "--visual-pipeline=modern", "--initial-time=8") },
    @{ Name = "modern-dusk"; Arguments = @("--smoke-test", "--smoke-frames=$SmokeFrames", "--hidden-window", "--visual-pipeline=modern", "--initial-time=18.5") },
    @{ Name = "modern-night"; Arguments = @("--smoke-test", "--smoke-frames=$SmokeFrames", "--hidden-window", "--visual-pipeline=modern", "--initial-time=0") },
    @{ Name = "marlow-peek"; Arguments = @("--smoke-test", "--smoke-session=backrooms", "--smoke-backrooms-level=-2", "--smoke-backrooms-marlow=peek", "--smoke-frames=2", "--hidden-window") },
    @{ Name = "marlow-chase"; Arguments = @("--smoke-test", "--smoke-session=backrooms", "--smoke-backrooms-level=-2", "--smoke-backrooms-marlow=chase", "--smoke-frames=2", "--hidden-window") },
    @{ Name = "marlow-capture-blocked"; Arguments = @("--smoke-test", "--smoke-session=backrooms", "--smoke-backrooms-level=-2", "--smoke-backrooms-marlow=capture-blocked", "--smoke-frames=2", "--hidden-window") },
    @{ Name = "marlow-screamer"; Arguments = @("--smoke-test", "--smoke-session=backrooms", "--smoke-backrooms-level=-2", "--smoke-backrooms-marlow=screamer", "--smoke-frames=2", "--hidden-window") }
)
foreach ($scenario in $smokeScenarios) {
    Write-Host ("   -> smoke checkpoint '{0}'" -f $scenario.Name)
    Invoke-External -FilePath (Join-Path $strictBuildDir "bin\ValCraft.exe") -Arguments $scenario.Arguments -WorkingDirectory $strictBuildDir
}

Write-Host "==> Configuring coverage build"
Invoke-External -FilePath $cmakeExe -Arguments @(
    "-S", $repoRoot,
    "-B", $coverageBuildDir,
    "-G", "Ninja",
    "-DVALCRAFT_STRICT_WARNINGS=ON",
    "-DVALCRAFT_ENABLE_COVERAGE=ON"
)

Write-Host "==> Building coverage gate targets"
Invoke-External -FilePath $cmakeExe -Arguments @("--build", $coverageBuildDir, "--target", "valcraft_check_build", "--parallel")

Write-Host "==> Executing coverage tests"
Invoke-External -FilePath $cmakeExe -Arguments @("--build", $coverageBuildDir, "--target", "valcraft_check_coverage", "--parallel")

if (Test-Path $coverageReportDir) {
    Remove-Item -Path $coverageReportDir -Recurse -Force
}
New-Item -ItemType Directory -Path $coverageReportDir | Out-Null

$criticalSources = @(
    Get-ChildItem -Path (Join-Path $repoRoot "src\world") -Filter "*.cpp" | Sort-Object Name
    Get-ChildItem -Path (Join-Path $repoRoot "src\gameplay") -Filter "*.cpp" | Sort-Object Name
) | ForEach-Object { $_.FullName }

$coveredLineTotal = 0.0
$instrumentedLineTotal = 0

Write-Host "==> Computing critical coverage"
foreach ($sourcePath in $criticalSources) {
    $relativeSource = $sourcePath.Substring($repoRoot.Length + 1)
    $objectDirectory = Join-Path $coverageBuildDir (Join-Path "CMakeFiles\valcraft_core.dir" (Split-Path $relativeSource -Parent))
    $coverageInputPath = Join-Path $objectDirectory ((Split-Path $sourcePath -Leaf) + ".gcno")
    if (-not (Test-Path $coverageInputPath)) {
        throw "Strict gate failed: gcov notes file not found for '$sourcePath' at '$coverageInputPath'."
    }

    $summary = Get-GcovSummary -GcovExe $gcovExe -CoverageWorkingDirectory $coverageReportDir -ObjectDirectory $objectDirectory -CoverageInputPath $coverageInputPath -SourcePath $sourcePath
    $coveredLineTotal += $summary.Executed
    $instrumentedLineTotal += $summary.Lines

    $sourcePercent = if ($summary.Lines -gt 0) { ($summary.Executed / $summary.Lines) * 100.0 } else { 0.0 }
    Write-Host ("   {0}: {1:N2}% ({2:N2}/{3})" -f (Split-Path $sourcePath -Leaf), $sourcePercent, $summary.Executed, $summary.Lines)
}

if ($instrumentedLineTotal -le 0) {
    throw "Strict gate failed: no instrumented lines were found for critical coverage."
}

$aggregateCoverage = ($coveredLineTotal / $instrumentedLineTotal) * 100.0
Write-Host ("==> Aggregate critical coverage: {0:N2}% ({1:N2}/{2})" -f $aggregateCoverage, $coveredLineTotal, $instrumentedLineTotal)

if ($aggregateCoverage -lt $CriticalCoverageThreshold) {
    throw "Strict gate failed: critical coverage is $([math]::Round($aggregateCoverage, 2))%, below the required $CriticalCoverageThreshold%."
}

# Je mesure separement le coeur Marlow, son rendu procedural et l'adaptateur
# World : l'agregat global ne doit pas pouvoir masquer une branche non testee.
$marlowSources = @(
    Join-Path $repoRoot "src\gameplay\BackroomsMarlow.cpp"
    Join-Path $repoRoot "src\gameplay\BackroomsMarlowWorld.cpp"
    Join-Path $repoRoot "src\render\BackroomsMarlowVisual.cpp"
)
$marlowCoveredLines = 0.0
$marlowInstrumentedLines = 0
$marlowTakenBranches = 0.0
$marlowInstrumentedBranches = 0

Write-Host "==> Computing Marlow coverage gate"
foreach ($sourcePath in $marlowSources) {
    if (-not (Test-Path $sourcePath)) {
        throw "Strict gate failed: Marlow source not found at '$sourcePath'."
    }
    $relativeSource = $sourcePath.Substring($repoRoot.Length + 1)
    $objectDirectory = Join-Path $coverageBuildDir (Join-Path "CMakeFiles\valcraft_core.dir" (Split-Path $relativeSource -Parent))
    $coverageInputPath = Join-Path $objectDirectory ((Split-Path $sourcePath -Leaf) + ".gcno")
    if (-not (Test-Path $coverageInputPath)) {
        throw "Strict gate failed: gcov notes file not found for '$sourcePath' at '$coverageInputPath'."
    }

    $summary = Get-GcovSummary -GcovExe $gcovExe -CoverageWorkingDirectory $coverageReportDir -ObjectDirectory $objectDirectory -CoverageInputPath $coverageInputPath -SourcePath $sourcePath
    $marlowCoveredLines += $summary.Executed
    $marlowInstrumentedLines += $summary.Lines
    $marlowTakenBranches += $summary.TakenBranches
    $marlowInstrumentedBranches += $summary.Branches
}

if ($marlowInstrumentedLines -le 0 -or $marlowInstrumentedBranches -le 0) {
    throw "Strict gate failed: Marlow line or branch instrumentation is empty."
}
$marlowLineCoverage = ($marlowCoveredLines / $marlowInstrumentedLines) * 100.0
$marlowBranchCoverage = ($marlowTakenBranches / $marlowInstrumentedBranches) * 100.0
Write-Host ("==> Marlow coverage: lines {0:N2}%, branches {1:N2}%" -f $marlowLineCoverage, $marlowBranchCoverage)
if ($marlowLineCoverage -lt $CriticalCoverageThreshold) {
    throw "Strict gate failed: Marlow line coverage is $([math]::Round($marlowLineCoverage, 2))%, below the required $CriticalCoverageThreshold%."
}
if ($marlowBranchCoverage -lt $MarlowBranchCoverageThreshold) {
    throw "Strict gate failed: Marlow branch coverage is $([math]::Round($marlowBranchCoverage, 2))%, below the required $MarlowBranchCoverageThreshold%."
}

Write-Host "==> Strict gate passed successfully."
