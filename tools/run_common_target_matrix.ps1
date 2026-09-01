param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-zA-Z0-9_.-]+$')]
    [string]$CandidateId,

    [Parameter(Mandatory = $true)]
    [ValidateSet('dcfr', 'production_dcfr')]
    [string]$Algorithm,

    [double]$Alpha = 1.5,
    [double]$Gamma = 3.0,
    [uint64]$CertificationInterval = 30,
    [string]$OutputRoot = 'out/dcfr-option-loop-20260901'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $repoRoot 'out/build/windows-release-current/apps/gto_cli/gto_cli.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Missing Release executable: $executable"
}

$fixturePaths = [ordered]@{
    ahk = 'benchmarks/fixtures/gto_plus_ahkhqh_101.json'
    th  = 'benchmarks/fixtures/gto_plus_th7d6s_101.json'
    tst = 'benchmarks/fixtures/gto_plus_tstc9d_101.json'
}
$candidateDirectory = Join-Path (Join-Path $repoRoot $OutputRoot) $CandidateId
if (Test-Path -LiteralPath $candidateDirectory) {
    throw "Candidate output already exists: $candidateDirectory"
}
[void](New-Item -ItemType Directory -Path $candidateDirectory)

$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
$manifest = [ordered]@{
    schema = 'gtosd.common_target_matrix.v1'
    candidate_id = $CandidateId
    contract = [ordered]@{
        algorithm = $Algorithm
        alpha = $Alpha
        beta = 0.0
        gamma = $Gamma
        certification_interval = $CertificationInterval
        target_dev_percent = 1.0
        strict_target = $true
        state_precision = 'scaled_uint16_regret_strategy'
        maximum_solver_threads = 8
        fixture_specific_logic = 'none'
    }
    fixtures = @()
}

Remove-Item Env:GTOSD_DIAGNOSTIC_FIXED_ITERATIONS -ErrorAction SilentlyContinue
Remove-Item Env:GTOSD_DIAGNOSTIC_ITERATION_LIMIT -ErrorAction SilentlyContinue
Remove-Item Env:GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL -ErrorAction SilentlyContinue

foreach ($fixtureName in $fixturePaths.Keys) {
    $sourcePath = Join-Path $repoRoot $fixturePaths[$fixtureName]
    $document = Get-Content -LiteralPath $sourcePath -Raw | ConvertFrom-Json
    $document.gtosd_run.algorithm = $Algorithm
    $document.gtosd_run.dcfr_positive_regret_exponent = $Alpha
    $document.gtosd_run.dcfr_average_exponent = $Gamma
    $document.gtosd_run.averaging_delay = 0
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

    & $executable postflop benchmark-gto-plus $generatedFixture $reportPath *> $logPath
    if ($LASTEXITCODE -ne 0 -and
        -not ($LASTEXITCODE -eq 4 -and (Test-Path -LiteralPath $reportPath -PathType Leaf))) {
        throw "Target run failed for $fixtureName with exit code $LASTEXITCODE"
    }

    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if (-not $report.converged -or $report.final_gto_plus_dev_percent -ge 1.0) {
        throw "Target run did not reach strict dEV below 1% for $fixtureName"
    }
    $manifest.fixtures += [ordered]@{
        fixture = $fixtureName
        benchmark_id = $report.benchmark_id
        report = [System.IO.Path]::GetFileName($reportPath)
        completed_iterations = $report.completed_iterations
        final_dev_percent = $report.final_gto_plus_dev_percent
        elapsed_seconds = $report.elapsed_seconds
        wall_elapsed_seconds_including_tree_preparation = $report.wall_elapsed_seconds_including_tree_preparation
        process_cpu_seconds = $report.process_cpu_seconds
        normalized_cpu_utilization_fraction = $report.normalized_cpu_utilization_fraction
        traversal_seconds = $report.phase_seconds.traversal
        certification_seconds = $report.phase_seconds.certification
        root_ev_antes = $report.gto_plus_ev_checks.flop_co_root.measured_antes
        correctness_passed = $report.correctness_passed
        layout_matches_fixture = $report.layout_matches_fixture
        exact_outcomes = $report.exact_outcomes
        solver_state_bytes = $report.solver_state_bytes
        peak_rss_bytes = $report.peak_rss_bytes
        memory_gate_passed = $report.memory_gate.passed
    }
}

$manifestPath = Join-Path $candidateDirectory 'manifest.json'
[System.IO.File]::WriteAllText(
    $manifestPath,
    ($manifest | ConvertTo-Json -Depth 100),
    $utf8WithoutBom)
Write-Output "common_target_matrix_complete manifest=$manifestPath"
