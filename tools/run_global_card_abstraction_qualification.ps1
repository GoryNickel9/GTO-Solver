[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GtoCli,
    [Parameter(Mandatory = $true)]
    [ValidateCount(2, 32)]
    [string[]]$Specifications,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [ValidateCount(1, 16)]
    [uint32[]]$Buckets = @(16, 32),
    [ValidateRange(1, [long]::MaxValue)]
    [long]$Iterations = 800,
    [ValidateRange(1, [long]::MaxValue)]
    [long]$RamBytes = 12000000000,
    [ValidateRange(1, [long]::MaxValue)]
    [long]$DiskBytes = 10737418240,
    [ValidateRange(1, 99)]
    [int]$Repetitions = 5,
    [switch]$SkipSerialOracle,
    [switch]$ContinueAfterReject
)

$ErrorActionPreference = 'Stop'
$repository = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$cli = (Resolve-Path -LiteralPath $GtoCli).Path
$destination = [System.IO.Path]::GetFullPath($OutputDirectory)
$summaryPath = Join-Path $destination 'global-card-abstraction-summary.json'

if ($env:GTOSD_CARD_ABSTRACTION_DIAGNOSTIC_SOLVER_THREADS) {
    throw 'The global gate requires a clean eight-thread environment.'
}
if (@($Buckets | Select-Object -Unique).Count -ne $Buckets.Count) {
    throw 'Bucket candidates must be unique.'
}
if (@($Specifications | Select-Object -Unique).Count -ne $Specifications.Count) {
    throw 'Specifications must be unique.'
}
if (Test-Path -LiteralPath $destination) {
    if (@(Get-ChildItem -LiteralPath $destination -Force).Count -ne 0) {
        throw "Output directory must be empty: $destination"
    }
} else {
    [System.IO.Directory]::CreateDirectory($destination) | Out-Null
}

$sourceCommit = (& git -C $repository rev-parse HEAD 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($sourceCommit)) {
    throw 'Unable to resolve the source commit.'
}
$trackedStatus = (& git -C $repository status --short --untracked-files=no 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect the tracked worktree.'
}
if (-not [string]::IsNullOrWhiteSpace($trackedStatus)) {
    throw "The global gate requires a clean tracked worktree: $trackedStatus"
}

function Write-JsonAtomically([object]$Value, [string]$Path) {
    $temporary = "$Path.tmp"
    $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $temporary -Encoding utf8
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Invoke-LoggedCli([string[]]$Arguments, [string]$LogPath) {
    $output = & $cli @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $output | Set-Content -LiteralPath $LogPath -Encoding utf8
    if ($exitCode -ne 0) {
        throw "CLI failed with exit code $exitCode. See $LogPath"
    }
}

function Assert-QualificationContract([object]$Report, [uint32]$K, [int]$Threads) {
    if ($Report.schema -ne 'gtosd.card_abstraction_qualification.v1' -or
        [uint32]$Report.buckets_per_partition -ne $K -or
        [long]$Report.iterations -ne $Iterations -or
        $Report.algorithm -ne 'cfr_plus' -or
        $Report.state_precision -ne 'float64' -or
        [long]$Report.averaging_delay -ne 0 -or
        [long]$Report.certification_interval -ne [Math]::Min(100, $Iterations) -or
        -not [bool]$Report.exact_outcomes -or
        -not [bool]$Report.exact_combo_best_response -or
        -not [bool]$Report.feature_cache_reused -or
        [int]$Report.solver_threads -ne $Threads -or
        [int]$Report.parallel_action_workers -ne ($Threads - 1)) {
        throw "Qualification contract mismatch for $($Report.benchmark_id), K=$K."
    }
}

function Invoke-Qualification(
    [object]$Fixture,
    [uint32]$K,
    [string]$Stem,
    [int]$Threads
) {
    $reportPath = Join-Path $Fixture.output_directory "$Stem.json"
    $logPath = Join-Path $Fixture.output_directory "$Stem.log"
    $arguments = @(
        'postflop', 'qualify-bucketing-gto-plus', $Fixture.specification,
        $Fixture.cache, $reportPath, [string]$K, [string]$Iterations,
        [string]$RamBytes, [string]$DiskBytes
    )
    $previousThreads = $env:GTOSD_CARD_ABSTRACTION_DIAGNOSTIC_SOLVER_THREADS
    try {
        if ($Threads -eq 1) {
            $env:GTOSD_CARD_ABSTRACTION_DIAGNOSTIC_SOLVER_THREADS = '1'
        }
        Invoke-LoggedCli $arguments $logPath
    } finally {
        if ($null -eq $previousThreads) {
            Remove-Item Env:GTOSD_CARD_ABSTRACTION_DIAGNOSTIC_SOLVER_THREADS `
                -ErrorAction SilentlyContinue
        } else {
            $env:GTOSD_CARD_ABSTRACTION_DIAGNOSTIC_SOLVER_THREADS = $previousThreads
        }
    }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    Assert-QualificationContract $report $K $Threads
    return [pscustomobject]@{
        report = $report
        report_path = [System.IO.Path]::GetRelativePath($destination, $reportPath)
    }
}

$fixtureRecords = @()
foreach ($specification in $Specifications) {
    $fixturePath = (Resolve-Path -LiteralPath $specification).Path
    $fixtureDocument = Get-Content -LiteralPath $fixturePath -Raw | ConvertFrom-Json
    $benchmarkId = [string]$fixtureDocument.benchmark_id
    if ([string]::IsNullOrWhiteSpace($benchmarkId)) {
        throw "Fixture does not declare benchmark_id: $fixturePath"
    }
    $safeId = $benchmarkId -replace '[^A-Za-z0-9_.-]', '_'
    $fixtureDirectory = Join-Path $destination $safeId
    [System.IO.Directory]::CreateDirectory($fixtureDirectory) | Out-Null
    $preflightPath = Join-Path $fixtureDirectory 'preflight.json'
    $preflightLog = Join-Path $fixtureDirectory 'preflight.log'
    $cachePath = Join-Path $fixtureDirectory 'exact.features'
    $cacheLog = Join-Path $fixtureDirectory 'feature-cache.log'
    $bucketCsv = ($Buckets | ForEach-Object { [string]$_ }) -join ','
    Invoke-LoggedCli @(
        'postflop', 'preflight-bucketing-gto-plus', $fixturePath, $preflightPath,
        $bucketCsv, [string]$RamBytes, [string]$DiskBytes
    ) $preflightLog
    $preflight = Get-Content -LiteralPath $preflightPath -Raw | ConvertFrom-Json
    if ($preflight.schema -ne 'gtosd.card_abstraction_preflight.v1' -or
        [int]$preflight.memory_model.solver_threads -ne 8 -or
        -not [bool]$preflight.feature_cache.format_limit_ok) {
        throw "Preflight contract mismatch for $benchmarkId."
    }
    foreach ($candidate in $preflight.candidates) {
        if (-not [bool]$candidate.in_ram_meets_requested_budget -or
            -not [bool]$candidate.out_of_core_meets_requested_budgets) {
            throw "K=$($candidate.buckets_per_partition) exceeds the declared product envelope for $benchmarkId."
        }
    }
    Invoke-LoggedCli @(
        'postflop', 'build-feature-cache-gto-plus', $fixturePath, $cachePath
    ) $cacheLog
    $fixtureRecords += [pscustomobject]@{
        benchmark_id = $benchmarkId
        specification = $fixturePath
        specification_sha256 = (Get-FileHash -LiteralPath $fixturePath -Algorithm SHA256).Hash
        output_directory = $fixtureDirectory
        preflight = [System.IO.Path]::GetRelativePath($destination, $preflightPath)
        cache = $cachePath
    }
}
if (@($fixtureRecords.benchmark_id | Select-Object -Unique).Count -ne $fixtureRecords.Count) {
    throw 'Every specification must declare a unique benchmark_id.'
}

$summary = [ordered]@{
    schema = 'gtosd.global_card_abstraction_qualification.v1'
    selection_policy = 'one_k_for_every_fixture_worst_case_gate'
    source_commit = $sourceCommit
    tracked_worktree_clean = $true
    executable = $cli
    executable_sha256 = (Get-FileHash -LiteralPath $cli -Algorithm SHA256).Hash
    solver_threads = 8
    algorithm = 'cfr_plus'
    state_precision = 'float64'
    exact_outcomes = $true
    exact_combo_best_response = $true
    iterations = $Iterations
    certification_interval = [Math]::Min(100, $Iterations)
    normalized_nash_conv_limit = 0.01
    requested_ram_bytes = $RamBytes
    requested_disk_bytes = $DiskBytes
    repetitions = $Repetitions
    fixtures = @($fixtureRecords | ForEach-Object {
        [ordered]@{
            benchmark_id = $_.benchmark_id
            specification = $_.specification
            specification_sha256 = $_.specification_sha256
            preflight = $_.preflight
        }
    })
    candidates = @()
    status = 'RUNNING'
}
Write-JsonAtomically $summary $summaryPath

foreach ($k in $Buckets) {
    $candidate = [ordered]@{
        buckets_per_partition = $k
        fixture_results = @()
        all_pre_gates_pass = $false
        repeated_gate_complete = $false
        status = 'RUNNING_PRE_GATE'
    }
    $preGatePass = $true
    foreach ($fixture in $fixtureRecords) {
        $result = Invoke-Qualification $fixture $k "k$k-pre-gate" 8
        $pass = [bool]$result.report.gates.pass
        $candidate.fixture_results += [ordered]@{
            benchmark_id = $fixture.benchmark_id
            pre_gate_report = $result.report_path
            normalized_nash_conv = [double]$result.report.normalized_nash_conv
            process_peak_rss_bytes = [long]$result.report.measured.process_peak_rss_bytes
            wall_seconds = [double]$result.report.measured.wall_seconds
            pass = $pass
            repeated_reports = @()
            serial_oracle_report = $null
            serial_oracle_difference = $null
        }
        if (-not $pass) {
            $preGatePass = $false
            if (-not $ContinueAfterReject) {
                break
            }
        }
    }
    $candidate.all_pre_gates_pass = $preGatePass -and
        $candidate.fixture_results.Count -eq $fixtureRecords.Count
    if (-not $candidate.all_pre_gates_pass) {
        $candidate.status = 'REJECTED_GLOBAL'
        $summary.candidates += $candidate
        Write-JsonAtomically $summary $summaryPath
        continue
    }

    $candidate.status = 'RUNNING_REPEATED_GATE'
    Write-JsonAtomically $summary $summaryPath
    $repeatedPass = $true
    for ($fixtureIndex = 0; $fixtureIndex -lt $fixtureRecords.Count; ++$fixtureIndex) {
        $fixture = $fixtureRecords[$fixtureIndex]
        for ($run = 2; $run -le $Repetitions; ++$run) {
            $result = Invoke-Qualification $fixture $k ("k$k-run-{0:D2}" -f $run) 8
            $candidate.fixture_results[$fixtureIndex].repeated_reports += $result.report_path
            $repeatedPass = $repeatedPass -and [bool]$result.report.gates.pass
        }
        if (-not $SkipSerialOracle) {
            $serial = Invoke-Qualification $fixture $k "k$k-serial-oracle" 1
            $difference = [Math]::Abs(
                [double]$candidate.fixture_results[$fixtureIndex].normalized_nash_conv -
                [double]$serial.report.normalized_nash_conv)
            $candidate.fixture_results[$fixtureIndex].serial_oracle_report = $serial.report_path
            $candidate.fixture_results[$fixtureIndex].serial_oracle_difference = $difference
            $repeatedPass = $repeatedPass -and [bool]$serial.report.gates.pass -and
                $difference -le 0.0001
        }
    }
    $candidate.repeated_gate_complete = $true
    $candidate.status = if ($repeatedPass) { 'QUALIFIED_GLOBAL' } else { 'REJECTED_GLOBAL' }
    $summary.candidates += $candidate
    Write-JsonAtomically $summary $summaryPath
}

$qualified = @($summary.candidates | Where-Object { $_.status -eq 'QUALIFIED_GLOBAL' })
$summary.status = if ($qualified.Count -eq 1) {
    'ONE_GLOBAL_K_QUALIFIED'
} elseif ($qualified.Count -gt 1) {
    'MULTIPLE_GLOBAL_K_CANDIDATES_QUALIFIED'
} else {
    'NO_GLOBAL_K_QUALIFIED'
}
Write-JsonAtomically $summary $summaryPath
$summary | ConvertTo-Json -Depth 12
if ($qualified.Count -eq 0) {
    exit 4
}
