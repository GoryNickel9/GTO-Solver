param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-zA-Z0-9_.-]+$')]
    [string]$CandidateId,

    [double]$Alpha = 1.5,
    [double]$Gamma = 2.0,
    [uint64]$AveragingDelay = 0,
    [uint64]$CertificationInterval = 20,
    [string]$BuildDir = 'out/build/windows-release-current',
    [string]$OutputRoot = '.tmp/s6-production-qualification',
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))

function Resolve-RepositoryPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Require-Equal($Actual, $Expected, [string]$Label) {
    if ($Actual -ne $Expected) {
        throw "$Label drifted: expected '$Expected', observed '$Actual'"
    }
}

function Is-Finite([double]$Value) {
    return -not ([double]::IsNaN($Value) -or [double]::IsInfinity($Value))
}

$buildPath = Resolve-RepositoryPath $BuildDir
$outputPath = Resolve-RepositoryPath $OutputRoot
$executable = Join-Path $buildPath 'apps/gto_cli/gto_cli.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Missing Release executable: $executable"
}

$ambientGtosdVariables = @(Get-ChildItem Env: | Where-Object { $_.Name -like 'GTOSD_*' })
if ($ambientGtosdVariables.Count -ne 0) {
    $names = ($ambientGtosdVariables | ForEach-Object { $_.Name }) -join ', '
    throw "Target-driven qualification requires a clean GTOSD environment; found: $names"
}

$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($commit)) {
    throw 'Unable to resolve committed HEAD.'
}

$fixturePaths = [ordered]@{
    ahk = 'benchmarks/fixtures/gto_plus_ahkhqh_101.json'
    th  = 'benchmarks/fixtures/gto_plus_th7d6s_101.json'
    tst = 'benchmarks/fixtures/gto_plus_tstc9d_101.json'
}

$candidateDirectory = Join-Path $outputPath $CandidateId
if (Test-Path -LiteralPath $candidateDirectory) {
    throw "Candidate output already exists: $candidateDirectory"
}
[void](New-Item -ItemType Directory -Path $candidateDirectory)

$contract = [ordered]@{
    algorithm = 'exact_alternating_signed_dcfr'
    dcfr_positive_regret_exponent = $Alpha
    dcfr_negative_regret_exponent = 0.0
    dcfr_average_exponent = $Gamma
    averaging_delay = $AveragingDelay
    state_precision = 'scaled_uint16_regret_strategy'
    maximum_solver_threads = 8
    certification_interval = $CertificationInterval
    strict_target_percent = 1.0
    strict_target_comparison = 'less_than'
    best_response = 'exact'
    fixture_specific_logic = 'none'
    same_contract_all_fixtures = $true
}

$manifest = [ordered]@{
    schema = 'gtosd.common_schedule_target.v1'
    candidate_id = $CandidateId
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    source = [ordered]@{
        repository_commit = $commit
        fixture_materialization = 'git_show_head'
        executable = $executable
    }
    fixture_specific_logic = 'none'
    same_contract_all_fixtures = $true
    contract = $contract
    fixtures = @()
}

$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
foreach ($fixtureName in $fixturePaths.Keys) {
    $relativeFixture = $fixturePaths[$fixtureName]
    $committedText = (& git -C $repoRoot show "${commit}:$relativeFixture" | Out-String)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($committedText)) {
        throw "Unable to materialize committed fixture: $relativeFixture"
    }
    $document = $committedText | ConvertFrom-Json

    Require-Equal $document.gto_plus_reference.target_dev_percent 1.0 "$fixtureName target"
    Require-Equal $document.gtosd_run.parallel_action_depth 7 "$fixtureName parallel depth"
    $document.gtosd_run.algorithm = 'dcfr'
    $document.gtosd_run.dcfr_positive_regret_exponent = $Alpha
    $document.gtosd_run.dcfr_average_exponent = $Gamma
    $document.gtosd_run.averaging_delay = $AveragingDelay
    $document.gtosd_run.state_precision = 'scaled_uint16_regret_strategy'
    $document.gtosd_run.maximum_solver_threads = 8
    $document.gtosd_run.certification_interval = $CertificationInterval

    $generatedFixture = Join-Path $candidateDirectory "$fixtureName.fixture.json"
    $reportPath = Join-Path $candidateDirectory "$fixtureName.report.json"
    $logPath = Join-Path $candidateDirectory "$fixtureName.log"
    [System.IO.File]::WriteAllText(
        $generatedFixture,
        ($document | ConvertTo-Json -Depth 100),
        $utf8WithoutBom)

    if ($Quiet) {
        & $executable postflop benchmark-gto-plus $generatedFixture $reportPath *> $logPath
    }
    else {
        & $executable postflop benchmark-gto-plus $generatedFixture $reportPath 2>&1 |
            Tee-Object -FilePath $logPath
    }
    $runExitCode = $LASTEXITCODE
    if (($runExitCode -ne 0 -and $runExitCode -ne 4) -or
        -not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        throw "Target run failed for $fixtureName with exit code $runExitCode"
    }

    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    Require-Equal $report.schema 'gtosd.gto_plus_convergence_run.v1' "$fixtureName report schema"
    Require-Equal $report.build.configuration 'Release' "$fixtureName build configuration"
    Require-Equal $report.algorithm 'exact_dcfr' "$fixtureName algorithm"
    Require-Equal $report.update_mode 'alternating' "$fixtureName update mode"
    Require-Equal $report.dcfr_parameters.alpha $Alpha "$fixtureName alpha"
    Require-Equal $report.dcfr_parameters.beta 0.0 "$fixtureName beta"
    Require-Equal $report.dcfr_parameters.gamma $Gamma "$fixtureName gamma"
    Require-Equal $report.averaging_delay $AveragingDelay "$fixtureName averaging delay"
    Require-Equal $report.certification_interval $CertificationInterval "$fixtureName certification interval"
    Require-Equal $report.maximum_solver_threads 8 "$fixtureName thread limit"
    Require-Equal $report.precision `
        'action_major_scaled_uint16_regret_strategy_float32_compute' "$fixtureName precision"
    Require-Equal $report.exact_outcomes $true "$fixtureName exact outcomes"
    Require-Equal $report.sampling $false "$fixtureName sampling"
    Require-Equal $report.fixed_iteration_diagnostic $false "$fixtureName target mode"
    Require-Equal $report.converged $true "$fixtureName convergence"
    if ([double]$report.final_gto_plus_dev_percent -ge 1.0) {
        throw "$fixtureName did not satisfy strict dEV < 1%"
    }
    if (-not (Is-Finite ([double]$report.final_normalized_nash_conv))) {
        throw "$fixtureName produced non-finite NashConv"
    }

    $lastCertification = $report.convergence[-1]
    $solverSeconds = [double]$report.elapsed_seconds
    $wallSeconds = [double]$report.wall_elapsed_seconds_including_tree_preparation
    $traversalSeconds = [double]$lastCertification.traversal_elapsed_seconds
    $certificationSeconds = [double]$lastCertification.certification_elapsed_seconds
    $rootCheck = $report.gto_plus_ev_checks.flop_co_root
    $manifest.fixtures += [ordered]@{
        fixture = $fixtureName
        benchmark_id = $report.benchmark_id
        source_fixture = $relativeFixture
        generated_fixture = [System.IO.Path]::GetFileName($generatedFixture)
        report = [System.IO.Path]::GetFileName($reportPath)
        log = [System.IO.Path]::GetFileName($logPath)
        process_exit_code = $runExitCode
        completed_iterations = [uint64]$report.completed_iterations
        final_dev_percent = [double]$report.final_gto_plus_dev_percent
        root_ev_antes = [double]$rootCheck.measured_antes
        root_delta_antes = [double]$rootCheck.delta_antes
        root_passed = [bool]$rootCheck.passed
        payoff_sum_antes = [double]$lastCertification.expected_payoff_sum_antes
        payoff_sum_passed = [Math]::Abs([double]$lastCertification.expected_payoff_sum_antes) -le 1.0e-11
        maximum_normalization_error = [double]$report.maximum_normalization_error
        normalization_passed = [double]$report.maximum_normalization_error -le 1.0e-11
        normalized_nash_conv = [double]$report.final_normalized_nash_conv
        exact_outcomes = [bool]$report.exact_outcomes
        exact_best_response = $true
        layout_matches_fixture = [bool]$report.layout_matches_fixture
        game_fingerprint = $report.game_fingerprint
        decision_node_scales = [uint64]$report.decision_node_scales
        information_sets = [uint64]$report.information_sets
        actions = [uint64]$report.actions
        solver_state_bytes = [uint64]$report.solver_state_bytes
        solver_state_passed = [bool]$report.solver_state_gate.passed
        peak_rss_bytes = [uint64]$report.peak_rss_bytes
        desktop_ram_passed = [uint64]$report.peak_rss_bytes -lt 2000000000
        normalized_cpu_utilization_fraction = [double]$report.normalized_cpu_utilization_fraction
        tree_preparation_seconds = [Math]::Max(0.0, $wallSeconds - $solverSeconds)
        traversal_seconds = $traversalSeconds
        certification_seconds = $certificationSeconds
        solver_other_seconds = [Math]::Max(0.0, $solverSeconds - $traversalSeconds - $certificationSeconds)
        solver_seconds = $solverSeconds
        process_wall_seconds = $wallSeconds
        correctness_passed = [bool]$report.correctness_passed
        time_reference_seconds = [double]$report.time_gate.reference_seconds
        time_passed = [bool]$report.time_gate.passed
    }
}

$manifestPath = Join-Path $candidateDirectory 'manifest.json'
[System.IO.File]::WriteAllText(
    $manifestPath,
    ($manifest | ConvertTo-Json -Depth 100),
    $utf8WithoutBom)
Write-Output "common_schedule_target_complete manifest=$manifestPath"
exit 0
