param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-zA-Z0-9_.-]+$')]
    [string]$RunId,

    [ValidateSet('ahk', 'th', 'tst')]
    [string]$Fixture = 'tst',

    [ValidateRange(1, 1000000)]
    [uint64]$Iterations = 20,

    [switch]$TargetDriven,

    [string]$BuildDir = 'out/build/windows-release',
    [string]$OutputRoot = '.tmp/tst-strict-2gb-bottleneck-loop/03-baseline',

    [ValidateRange(1, [uint64]::MaxValue)]
    [uint64]$MemoryCapBytes = 2000000000
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)

function Resolve-RepositoryPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Write-Json([string]$Path, $Value) {
    [System.IO.File]::WriteAllText(
        $Path,
        (($Value | ConvertTo-Json -Depth 100) + [Environment]::NewLine),
        $utf8WithoutBom)
}

function Require-Equal($Actual, $Expected, [string]$Label) {
    if ($Actual -ne $Expected) {
        throw "$Label drifted: expected '$Expected', observed '$Actual'"
    }
}

function Is-Finite([double]$Value) {
    return -not ([double]::IsNaN($Value) -or [double]::IsInfinity($Value))
}

$fixturePaths = [ordered]@{
    ahk = 'benchmarks/fixtures/gto_plus_ahkhqh_101.json'
    th  = 'benchmarks/fixtures/gto_plus_th7d6s_101.json'
    tst = 'benchmarks/fixtures/gto_plus_tstc9d_101.json'
}
$timeLimits = [ordered]@{
    ahk = 1.900000
    th  = 19.622222
    tst = 128.988889
}

$ambientGtosdVariables = @(Get-ChildItem Env: | Where-Object { $_.Name -like 'GTOSD_*' })
if ($ambientGtosdVariables.Count -ne 0) {
    $names = ($ambientGtosdVariables | ForEach-Object { $_.Name }) -join ', '
    throw "Strict-cap run requires a clean GTOSD environment; found: $names"
}

$buildPath = Resolve-RepositoryPath $BuildDir
$outputPath = Resolve-RepositoryPath $OutputRoot
$executable = Join-Path $buildPath 'apps/gto_cli/gto_cli.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Missing Release executable: $executable"
}

$runDirectory = Join-Path $outputPath $RunId
if (Test-Path -LiteralPath $runDirectory) {
    throw "Immutable run already exists: $runDirectory"
}
[void](New-Item -ItemType Directory -Path $runDirectory)

$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($commit)) {
    throw 'Unable to resolve committed HEAD.'
}
$branch = (& git -C $repoRoot branch --show-current).Trim()
$relativeFixture = $fixturePaths[$Fixture]
$committedText = (& git -C $repoRoot show "${commit}:$relativeFixture" | Out-String)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($committedText)) {
    throw "Unable to materialize committed fixture: $relativeFixture"
}
$document = $committedText | ConvertFrom-Json

Require-Equal $document.gtosd_run.algorithm 'dcfr' "$Fixture algorithm"
Require-Equal $document.gtosd_run.dcfr_positive_regret_exponent 1.5 "$Fixture alpha"
Require-Equal $document.gtosd_run.dcfr_average_exponent 2.0 "$Fixture gamma"
Require-Equal $document.gtosd_run.averaging_delay 0 "$Fixture averaging delay"
Require-Equal $document.gtosd_run.state_precision 'scaled_uint16_regret_strategy' "$Fixture precision"
Require-Equal $document.gtosd_run.parallel_action_depth 7 "$Fixture parallel depth"
Require-Equal $document.gtosd_run.maximum_solver_threads 8 "$Fixture threads"
Require-Equal $document.gtosd_run.certification_interval 20 "$Fixture certification interval"
Require-Equal $document.gto_plus_reference.target_dev_percent 1.0 "$Fixture target"

$fixturePath = Join-Path $runDirectory 'fixture.json'
$reportPath = Join-Path $runDirectory 'report.json'
$stdoutPath = Join-Path $runDirectory 'stdout.log'
$stderrPath = Join-Path $runDirectory 'stderr.log'
$environmentPath = Join-Path $runDirectory 'environment.json'
$processPath = Join-Path $runDirectory 'process.json'
$validityPath = Join-Path $runDirectory 'validity.json'
$runPath = Join-Path $runDirectory 'run.json'
Write-Json $fixturePath $document

$contract = [ordered]@{
    algorithm = 'exact_alternating_signed_dcfr'
    alpha = 1.5
    beta = 0.0
    gamma = 2.0
    averaging_delay = 0
    state_precision = 'scaled_uint16_regret_strategy'
    parallel_action_depth = 7
    maximum_solver_threads = 8
    certification_interval = 20
    strict_target_percent = 1.0
    strict_target_comparison = 'less_than'
    exact_best_response = $true
    exact_outcomes = $true
    sampling = $false
    bucketing = $false
    fixture_specific_logic = 'none'
    same_contract_all_fixtures = $true
    memory_cap_bytes = $MemoryCapBytes
    memory_comparison = 'strict_less_than'
}
$contractText = $contract | ConvertTo-Json -Compress -Depth 100
$contractHashBytes = [System.Security.Cryptography.SHA256]::HashData(
    [System.Text.Encoding]::UTF8.GetBytes($contractText))
$contractHash = [Convert]::ToHexString($contractHashBytes).ToLowerInvariant()

$binaryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $executable).Hash.ToLowerInvariant()
$fixtureHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $fixturePath).Hash.ToLowerInvariant()
$runDocument = [ordered]@{
    schema = 'gtosd.tst_strict_cap_run.v1'
    run_id = $RunId
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    repository = [ordered]@{ root = $repoRoot; branch = $branch; commit = $commit }
    fixture = [ordered]@{
        name = $Fixture
        source = $relativeFixture
        materialization = 'git_show_head'
        sha256 = $fixtureHash
    }
    binary = [ordered]@{ path = $executable; sha256 = $binaryHash }
    mode = if ($TargetDriven) { 'target_driven' } else { 'fixed_iteration_diagnostic' }
    requested_iterations = if ($TargetDriven) { $null } else { $Iterations }
    contract = $contract
    contract_sha256 = $contractHash
}
Write-Json $runPath $runDocument

$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
$computer = Get-CimInstance Win32_ComputerSystem
$operatingSystem = Get-CimInstance Win32_OperatingSystem
$powerPlan = (& powercfg /GETACTIVESCHEME | Out-String).Trim()
$environmentDocument = [ordered]@{
    schema = 'gtosd.tst_strict_cap_environment.v1'
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    cpu_name = $processor.Name
    physical_cores = $processor.NumberOfCores
    logical_processors = $processor.NumberOfLogicalProcessors
    total_physical_memory_bytes = [uint64]$computer.TotalPhysicalMemory
    operating_system = $operatingSystem.Caption
    operating_system_version = $operatingSystem.Version
    power_plan = $powerPlan
    ambient_gtosd_variables = @()
    processes = @(Get-Process | Sort-Object CPU -Descending | Select-Object -First 32 `
        Id, ProcessName, CPU, WorkingSet64)
}
Write-Json $environmentPath $environmentDocument

$oldFixed = $env:GTOSD_DIAGNOSTIC_FIXED_ITERATIONS
$oldLimit = $env:GTOSD_DIAGNOSTIC_ITERATION_LIMIT
$oldInterval = $env:GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL
$process = $null
$observedPeak = [uint64]0
$capReached = $false
$startedAt = [DateTime]::UtcNow
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
try {
    if (-not $TargetDriven) {
        $env:GTOSD_DIAGNOSTIC_FIXED_ITERATIONS = '1'
        $env:GTOSD_DIAGNOSTIC_ITERATION_LIMIT = [string]$Iterations
        $env:GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL = '20'
    }
    $process = Start-Process -FilePath $executable `
        -ArgumentList @('postflop', 'benchmark-gto-plus', $fixturePath, $reportPath) `
        -WorkingDirectory (Split-Path -Parent $executable) `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -WindowStyle Hidden `
        -PassThru

    while (-not $process.HasExited) {
        $process.Refresh()
        $sample = [uint64]$process.PeakWorkingSet64
        if ($sample -gt $observedPeak) {
            $observedPeak = $sample
        }
        if ($sample -ge $MemoryCapBytes) {
            $capReached = $true
            Stop-Process -Id $process.Id -Force
            break
        }
        Start-Sleep -Milliseconds 50
    }
    $process.WaitForExit()
    $process.Refresh()
    $finalSample = [uint64]$process.PeakWorkingSet64
    if ($finalSample -gt $observedPeak) {
        $observedPeak = $finalSample
    }
}
finally {
    $stopwatch.Stop()
    if ($null -eq $oldFixed) { Remove-Item Env:GTOSD_DIAGNOSTIC_FIXED_ITERATIONS -ErrorAction SilentlyContinue }
    else { $env:GTOSD_DIAGNOSTIC_FIXED_ITERATIONS = $oldFixed }
    if ($null -eq $oldLimit) { Remove-Item Env:GTOSD_DIAGNOSTIC_ITERATION_LIMIT -ErrorAction SilentlyContinue }
    else { $env:GTOSD_DIAGNOSTIC_ITERATION_LIMIT = $oldLimit }
    if ($null -eq $oldInterval) { Remove-Item Env:GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL -ErrorAction SilentlyContinue }
    else { $env:GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL = $oldInterval }
}

$exitCode = if ($process) { $process.ExitCode } else { -1 }
$reportExists = Test-Path -LiteralPath $reportPath -PathType Leaf
$report = if ($reportExists) { Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json } else { $null }
$internalPeak = if ($report) { [uint64]$report.peak_rss_bytes } else { [uint64]0 }
$effectivePeak = [Math]::Max($observedPeak, $internalPeak)

$processDocument = [ordered]@{
    schema = 'gtosd.tst_strict_cap_process.v1'
    started_at_utc = $startedAt.ToString('o')
    ended_at_utc = [DateTime]::UtcNow.ToString('o')
    process_id = if ($process) { $process.Id } else { $null }
    exit_code = $exitCode
    wall_seconds = $stopwatch.Elapsed.TotalSeconds
    external_observed_peak_working_set_bytes = $observedPeak
    internal_reported_peak_rss_bytes = $internalPeak
    effective_peak_rss_bytes = $effectivePeak
    memory_cap_bytes = $MemoryCapBytes
    cap_reached = $capReached
    killed_on_cap = $capReached
}
Write-Json $processPath $processDocument

$schemaValid = $report -and $report.schema -eq 'gtosd.gto_plus_convergence_run.v1'
$releaseValid = $schemaValid -and $report.build.configuration -eq 'Release'
$contractValid = $schemaValid -and
    $report.algorithm -eq 'exact_dcfr' -and
    $report.update_mode -eq 'alternating' -and
    [double]$report.dcfr_parameters.alpha -eq 1.5 -and
    [double]$report.dcfr_parameters.beta -eq 0.0 -and
    [double]$report.dcfr_parameters.gamma -eq 2.0 -and
    [uint64]$report.averaging_delay -eq 0 -and
    [uint64]$report.certification_interval -eq 20 -and
    [uint64]$report.maximum_solver_threads -eq 8 -and
    $report.precision -eq 'action_major_scaled_uint16_regret_strategy_float32_compute' -and
    [bool]$report.exact_outcomes -and
    -not [bool]$report.sampling
$modeValid = $schemaValid -and ((-not $TargetDriven -and [bool]$report.fixed_iteration_diagnostic -and
        [uint64]$report.completed_iterations -eq $Iterations) -or
    ($TargetDriven -and -not [bool]$report.fixed_iteration_diagnostic))
$layoutValid = $schemaValid -and [bool]$report.layout_matches_fixture -and
    $report.game_fingerprint -eq $document.expected_layout.game_fingerprint
$stateValid = $schemaValid -and [bool]$report.solver_state_gate.passed -and
    [uint64]$report.solver_state_bytes -eq [uint64]$document.expected_layout.solver_state_bytes
$memoryValid = -not $capReached -and $effectivePeak -gt 0 -and $effectivePeak -lt $MemoryCapBytes
$runCompleted = -not $capReached -and ($exitCode -eq 0 -or (-not $TargetDriven -and $exitCode -eq 4))
$lastCertification = if ($schemaValid -and $report.convergence.Count -gt 0) { $report.convergence[-1] } else { $null }
$payoffSumValid = $lastCertification -and
    [Math]::Abs([double]$lastCertification.expected_payoff_sum_antes) -le 1.0e-11
$normalizationValid = $schemaValid -and [double]$report.maximum_normalization_error -le 1.0e-11
$nashConvValid = $schemaValid -and (Is-Finite ([double]$report.final_normalized_nash_conv))
$rootValid = $schemaValid -and [bool]$report.gto_plus_ev_checks.flop_co_root.passed
$correctnessValid = if ($TargetDriven) {
    $schemaValid -and $exitCode -eq 0 -and [bool]$report.correctness_passed -and
        [bool]$report.converged -and [double]$report.final_gto_plus_dev_percent -lt 1.0 -and
        $rootValid -and $payoffSumValid -and $normalizationValid -and $nashConvValid
} else {
    $schemaValid -and $payoffSumValid -and $normalizationValid -and $nashConvValid
}
$timePassed = if ($TargetDriven -and $schemaValid) {
    [double]$report.elapsed_seconds -lt [double]$timeLimits[$Fixture]
} else { $null }
$overallPassed = $runCompleted -and $schemaValid -and $releaseValid -and $contractValid -and
    $modeValid -and $layoutValid -and $stateValid -and $memoryValid -and $correctnessValid -and
    (-not $TargetDriven -or $timePassed)

$validityDocument = [ordered]@{
    schema = 'gtosd.tst_strict_cap_validity.v1'
    run_completed = $runCompleted
    report_structurally_valid = $schemaValid
    release_valid = $releaseValid
    contract_valid = $contractValid
    mode_valid = $modeValid
    layout_fingerprint_valid = $layoutValid
    solver_state_valid = $stateValid
    payoff_sum_valid = $payoffSumValid
    normalization_valid = $normalizationValid
    nash_conv_finite = $nashConvValid
    root_passed = $rootValid
    correctness_passed = $correctnessValid
    time_passed = $timePassed
    memory_passed = $memoryValid
    overall_passed = $overallPassed
    process_exit_code = $exitCode
    cap_reached = $capReached
    effective_peak_rss_bytes = $effectivePeak
    memory_headroom_bytes = if ($effectivePeak -lt $MemoryCapBytes) {
        $MemoryCapBytes - $effectivePeak
    } else { 0 }
}
Write-Json $validityPath $validityDocument

if ($capReached) {
    throw "Memory cap reached: observed $observedPeak B, cap $MemoryCapBytes B"
}
if (-not $overallPassed) {
    throw "Strict-cap validity failed; see $validityPath"
}
Write-Output "strict_cap_run_complete run=$runDirectory peak_rss_bytes=$effectivePeak"
