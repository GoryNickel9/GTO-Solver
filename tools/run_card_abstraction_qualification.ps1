[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GtoCli,
    [Parameter(Mandatory = $true)]
    [string]$Specification,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 4294967295)]
    [uint32]$Buckets,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$Iterations,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$RamBytes,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$DiskBytes,
    [ValidateRange(1, 99)]
    [int]$Repetitions = 5,
    [string]$FixedTurn = "",
    [switch]$SkipSerialOracle
)

$ErrorActionPreference = "Stop"
$cli = (Resolve-Path -LiteralPath $GtoCli).Path
$fixture = (Resolve-Path -LiteralPath $Specification).Path
$destination = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($destination) | Out-Null

$street = if ([string]::IsNullOrWhiteSpace($FixedTurn)) { "flop" } else { "turn-$FixedTurn" }
$stem = "card-abstraction-$street-k$Buckets"
$preflight = Join-Path $destination "$stem-preflight.json"
$cache = Join-Path $destination "$stem.features"
$cacheLog = Join-Path $destination "$stem-cache.log"
$summaryPath = Join-Path $destination "$stem-summary.json"

$preflightArguments = @(
    "postflop", "preflight-bucketing-gto-plus", $fixture, $preflight,
    [string]$Buckets, [string]$RamBytes, [string]$DiskBytes
)
if (-not [string]::IsNullOrWhiteSpace($FixedTurn)) {
    $preflightArguments += $FixedTurn
}
& $cli @preflightArguments
if ($LASTEXITCODE -ne 0) {
    throw "Bucketing preflight failed with exit code $LASTEXITCODE"
}

Remove-Item -LiteralPath $cache -Force -ErrorAction SilentlyContinue
$cacheArguments = @("postflop", "build-feature-cache-gto-plus", $fixture, $cache)
if (-not [string]::IsNullOrWhiteSpace($FixedTurn)) {
    $cacheArguments += $FixedTurn
}
$cacheOutput = & $cli @cacheArguments 2>&1
$cacheOutput | Set-Content -LiteralPath $cacheLog -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    throw "Feature-cache build failed with exit code $LASTEXITCODE. See $cacheLog"
}

$reports = @()
for ($run = 1; $run -le $Repetitions; ++$run) {
    $report = Join-Path $destination ("$stem-run-{0:D2}.json" -f $run)
    $log = Join-Path $destination ("$stem-run-{0:D2}.log" -f $run)
    Remove-Item -LiteralPath $report -Force -ErrorAction SilentlyContinue
    $qualificationArguments = @(
        "postflop", "qualify-bucketing-gto-plus", $fixture, $cache, $report,
        [string]$Buckets, [string]$Iterations, [string]$RamBytes, [string]$DiskBytes
    )
    if (-not [string]::IsNullOrWhiteSpace($FixedTurn)) {
        $qualificationArguments += $FixedTurn
    }
    $qualificationOutput = & $cli @qualificationArguments 2>&1
    $qualificationOutput | Set-Content -LiteralPath $log -Encoding utf8
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $report)) {
        throw "Qualification run $run failed. See $log"
    }
    $reports += Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
}

$serialOracleTolerance = 0.0001
$serialOracle = $null
$serialOracleReport = Join-Path $destination "$stem-serial-oracle.json"
if (-not $SkipSerialOracle) {
    $serialArguments = @(
        "postflop", "qualify-bucketing-gto-plus", $fixture, $cache, $serialOracleReport,
        [string]$Buckets, [string]$Iterations, [string]$RamBytes, [string]$DiskBytes
    )
    if (-not [string]::IsNullOrWhiteSpace($FixedTurn)) {
        $serialArguments += $FixedTurn
    }
    $previousDiagnosticThreads = $env:GTOSD_CARD_ABSTRACTION_DIAGNOSTIC_SOLVER_THREADS
    try {
        $env:GTOSD_CARD_ABSTRACTION_DIAGNOSTIC_SOLVER_THREADS = "1"
        $serialOutput = & $cli @serialArguments 2>&1
        $serialOutput | Set-Content -LiteralPath (
            Join-Path $destination "$stem-serial-oracle.log") -Encoding utf8
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $serialOracleReport)) {
            throw "Serial oracle failed. See $stem-serial-oracle.log"
        }
    } finally {
        if ($null -eq $previousDiagnosticThreads) {
            Remove-Item Env:GTOSD_CARD_ABSTRACTION_DIAGNOSTIC_SOLVER_THREADS `
                -ErrorAction SilentlyContinue
        } else {
            $env:GTOSD_CARD_ABSTRACTION_DIAGNOSTIC_SOLVER_THREADS = $previousDiagnosticThreads
        }
    }
    $serialOracle = Get-Content -LiteralPath $serialOracleReport -Raw | ConvertFrom-Json
}

function Get-Median([double[]]$Values) {
    $ordered = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($ordered.Count / 2)
    if (($ordered.Count % 2) -eq 1) {
        return [double]$ordered[$middle]
    }
    return ([double]$ordered[$middle - 1] + [double]$ordered[$middle]) / 2.0
}

$fingerprints = @($reports | ForEach-Object { $_.feature_cache_fingerprint } | Select-Object -Unique)
$allGatesPass = @($reports | Where-Object { -not $_.gates.pass }).Count -eq 0
$deterministicMetrics =
    @($reports | ForEach-Object { [string]$_.normalized_nash_conv } | Select-Object -Unique).Count -eq 1
$allFingerprintsMatch = $fingerprints.Count -eq 1
$allRunsUseEightThreads =
    @($reports | Where-Object {
        [int]$_.solver_threads -ne 8 -or [int]$_.parallel_action_workers -ne 7
    }).Count -eq 0
$parallelMetric = [double]$reports[0].normalized_nash_conv
$serialOracleDifference = if ($null -eq $serialOracle) {
    $null
} else {
    [Math]::Abs($parallelMetric - [double]$serialOracle.normalized_nash_conv)
}
$serialOracleVerified =
    $null -ne $serialOracle -and [int]$serialOracle.solver_threads -eq 1 -and
    [bool]$serialOracle.gates.pass -and
    $serialOracle.feature_cache_fingerprint -eq $fingerprints[0] -and
    $serialOracleDifference -le $serialOracleTolerance
$summary = [ordered]@{
    schema = "gtosd.card_abstraction_qualification_suite.v1"
    source_specification = $fixture
    starting_street = if ([string]::IsNullOrWhiteSpace($FixedTurn)) { "flop" } else { "turn" }
    fixed_turn = if ([string]::IsNullOrWhiteSpace($FixedTurn)) { $null } else { $FixedTurn }
    buckets_per_partition = $Buckets
    iterations = $Iterations
    repetitions = $Repetitions
    independent_solver_processes = $true
    shared_exact_feature_cache = $true
    solver_threads = 8
    all_runs_use_eight_threads = $allRunsUseEightThreads
    feature_cache_fingerprint = if ($allFingerprintsMatch) { $fingerprints[0] } else { $null }
    all_fingerprints_match = $allFingerprintsMatch
    deterministic_metrics = $deterministicMetrics
    all_run_gates_pass = $allGatesPass
    parallel_vs_serial_oracle = [ordered]@{
        required = $true
        verified = $serialOracleVerified
        serial_report = if ($null -eq $serialOracle) { $null } else {
            [System.IO.Path]::GetFileName($serialOracleReport)
        }
        absolute_nash_conv_tolerance = $serialOracleTolerance
        absolute_nash_conv_difference = $serialOracleDifference
    }
    normalized_nash_conv = [ordered]@{
        minimum = [double](($reports.normalized_nash_conv | Measure-Object -Minimum).Minimum)
        median = Get-Median ([double[]]$reports.normalized_nash_conv)
        maximum = [double](($reports.normalized_nash_conv | Measure-Object -Maximum).Maximum)
    }
    process_peak_rss_bytes = [ordered]@{
        minimum = [long](($reports.measured.process_peak_rss_bytes | Measure-Object -Minimum).Minimum)
        median = [long](Get-Median ([double[]]$reports.measured.process_peak_rss_bytes))
        maximum = [long](($reports.measured.process_peak_rss_bytes | Measure-Object -Maximum).Maximum)
    }
    wall_seconds = [ordered]@{
        minimum = [double](($reports.measured.wall_seconds | Measure-Object -Minimum).Minimum)
        median = Get-Median ([double[]]$reports.measured.wall_seconds)
        maximum = [double](($reports.measured.wall_seconds | Measure-Object -Maximum).Maximum)
    }
    reports = @(
        1..$Repetitions | ForEach-Object { "$stem-run-{0:D2}.json" -f $_ }
    )
    status = if ($allGatesPass -and $allFingerprintsMatch -and $deterministicMetrics -and
                  $allRunsUseEightThreads -and $serialOracleVerified) {
        "QUALIFIED_FOR_THIS_FIXTURE_AND_K"
    } elseif ($SkipSerialOracle) {
        "INCOMPLETE_SERIAL_ORACLE_NOT_RUN"
    } else {
        "REJECTED_FOR_THIS_FIXTURE_AND_K"
    }
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding utf8
$summary | ConvertTo-Json -Depth 8
if ($summary.status -ne "QUALIFIED_FOR_THIS_FIXTURE_AND_K") {
    exit 4
}
