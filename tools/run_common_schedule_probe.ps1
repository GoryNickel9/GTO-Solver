param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-zA-Z0-9_.-]+$')]
    [string]$CandidateId,

    [ValidateSet('dcfr', 'dcfr_plus', 'hs_dcfr_30')]
    [string]$Algorithm = 'dcfr',

    [double]$Alpha = 1.5,
    [double]$Gamma = 2.0,
    [uint64]$AveragingDelay = 0,
    [uint64]$Iterations = 20,
    [uint64]$CertificationInterval = 20,
    [switch]$Quiet,
    [string]$OutputRoot = '.tmp/common-convergence-loop'
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

$contract = [ordered]@{
    algorithm = $Algorithm
    dcfr_positive_regret_exponent = $Alpha
    dcfr_negative_regret_exponent = 0.0
    dcfr_average_exponent = $Gamma
    averaging_delay = $AveragingDelay
    state_precision = 'scaled_uint16_regret_strategy'
    maximum_solver_threads = 8
    certification_interval = $CertificationInterval
    diagnostic_fixed_iterations = $true
    diagnostic_iteration_limit = $Iterations
    fixture_specific_logic = 'none'
}

$candidateDirectory = Join-Path (Join-Path $repoRoot $OutputRoot) $CandidateId
if (Test-Path -LiteralPath $candidateDirectory) {
    throw "Candidate output already exists: $candidateDirectory"
}
[void](New-Item -ItemType Directory -Path $candidateDirectory)

$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
$manifest = [ordered]@{
    schema = 'gtosd.common_schedule_probe.v1'
    candidate_id = $CandidateId
    contract = $contract
    fixtures = @()
}

foreach ($fixtureName in $fixturePaths.Keys) {
    $sourcePath = Join-Path $repoRoot $fixturePaths[$fixtureName]
    $document = Get-Content -LiteralPath $sourcePath -Raw | ConvertFrom-Json
    $document.gtosd_run.algorithm = $Algorithm
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

    $env:GTOSD_DIAGNOSTIC_FIXED_ITERATIONS = '1'
    $env:GTOSD_DIAGNOSTIC_ITERATION_LIMIT = [string]$Iterations
    $env:GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL = [string]$CertificationInterval
    try {
        if ($Quiet) {
            & $executable postflop benchmark-gto-plus $generatedFixture $reportPath *> $logPath
        }
        else {
            & $executable postflop benchmark-gto-plus $generatedFixture $reportPath 2>&1 |
                Tee-Object -FilePath $logPath
        }
        # Exit 4 means that a structurally valid fixed-iteration diagnostic did
        # not yet satisfy the production target. The report is still the
        # authoritative curve sample. All other non-zero exits are failures.
        if ($LASTEXITCODE -ne 0 -and
            -not ($LASTEXITCODE -eq 4 -and (Test-Path -LiteralPath $reportPath -PathType Leaf))) {
            throw "Probe failed for $fixtureName with exit code $LASTEXITCODE"
        }
    }
    finally {
        Remove-Item Env:GTOSD_DIAGNOSTIC_FIXED_ITERATIONS -ErrorAction SilentlyContinue
        Remove-Item Env:GTOSD_DIAGNOSTIC_ITERATION_LIMIT -ErrorAction SilentlyContinue
        Remove-Item Env:GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL -ErrorAction SilentlyContinue
    }

    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    $lastCertification = $report.convergence[-1]
    $manifest.fixtures += [ordered]@{
        fixture = $fixtureName
        benchmark_id = $report.benchmark_id
        report = [System.IO.Path]::GetFileName($reportPath)
        completed_iterations = $report.completed_iterations
        final_dev_percent = $report.final_gto_plus_dev_percent
        final_root_ev_antes = $report.gto_plus_ev_checks.flop_co_root.measured_antes
        elapsed_seconds = $report.elapsed_seconds
        traversal_seconds = $lastCertification.traversal_elapsed_seconds
        certification_seconds = $lastCertification.certification_elapsed_seconds
        normalized_nash_conv = $lastCertification.normalized_nash_conv
        expected_payoff_sum_antes = $lastCertification.expected_payoff_sum_antes
        maximum_normalization_error = $report.maximum_normalization_error
        layout_matches_fixture = $report.layout_matches_fixture
        exact_outcomes = $report.exact_outcomes
    }
}

$manifestPath = Join-Path $candidateDirectory 'manifest.json'
[System.IO.File]::WriteAllText(
    $manifestPath,
    ($manifest | ConvertTo-Json -Depth 100),
    $utf8WithoutBom)
Write-Output "common_schedule_probe_complete manifest=$manifestPath"
exit 0
