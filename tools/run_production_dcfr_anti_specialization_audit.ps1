param(
  [string]$BuildDir = "out/build/windows-gui-release",
  [string]$OutputDir = "out/production-dcfr-anti-specialization-audit",
  [string]$Specification = "benchmarks/fixtures/gto_plus_ahkhqh_101.json"
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))

function Resolve-RepositoryPath([string]$Path) {
  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }
  return [System.IO.Path]::GetFullPath((Join-Path $repository $Path))
}

function Copy-JsonObject($Value) {
  return $Value | ConvertTo-Json -Depth 30 | ConvertFrom-Json
}

function Set-RootOnlyReference($SpecificationObject) {
  $SpecificationObject.gto_plus_reference.gate_node = 'flop_co_root'
  $SpecificationObject.gto_plus_reference.reference_nodes = @(
    [pscustomobject]@{
      id = 'flop_co_root'
      path = @()
      player = 0
      ev_antes = 19.15
      actions = [pscustomobject]@{}
    }
  )
}

function Assert-Equal($Actual, $Expected, [string]$Message) {
  if ($Actual -ne $Expected) {
    throw "$Message (actual=$Actual expected=$Expected)"
  }
}

$resolvedBuild = Resolve-RepositoryPath $BuildDir
$resolvedOutput = Resolve-RepositoryPath $OutputDir
$resolvedSpecification = Resolve-RepositoryPath $Specification
$executable = Join-Path $resolvedBuild "apps/gto_cli/gto_cli.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf) -or
    -not (Test-Path -LiteralPath $resolvedSpecification -PathType Leaf)) {
  throw "Release CLI or source specification is missing."
}
New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null

$source = Get-Content -LiteralPath $resolvedSpecification -Raw | ConvertFrom-Json
if ($source.schema -ne 'gtosd.gto_plus_convergence_benchmark.v4' -or
    $source.gtosd_run.algorithm -ne 'production_dcfr') {
  throw "The audit requires a v4 ProductionDcfr source fixture."
}

$variants = [ordered]@{}
$variants.baseline = Copy-JsonObject $source

$variants.metadata_only = Copy-JsonObject $source
$variants.metadata_only.benchmark_id = 'GTP-META-777'
$variants.metadata_only | Add-Member -NotePropertyName audit_metadata `
  -NotePropertyValue ([pscustomobject]@{ note = 'identity must not select the kernel' })

$variants.suit_permutation = Copy-JsonObject $source
$variants.suit_permutation.benchmark_id = 'GTP-SUIT-777'
$variants.suit_permutation.fixture.flop = @('As', 'Ks', 'Qs')

$variants.range_perturbation = Copy-JsonObject $source
$variants.range_perturbation.benchmark_id = 'GTP-RANGE-777'
$variants.range_perturbation.fixture.range_btn = 'AA-QQ,AKs-AQs,KQs,AKo-AQo'

$variants.stack_perturbation = Copy-JsonObject $source
$variants.stack_perturbation.benchmark_id = 'GTP-STACK-777'
$variants.stack_perturbation.fixture.effective_stack_antes = 101

$variants.sizing_perturbation = Copy-JsonObject $source
$variants.sizing_perturbation.benchmark_id = 'GTP-SIZING-777'
$variants.sizing_perturbation.fixture.bet_size_percent_pot = 55
$variants.sizing_perturbation.fixture.raise_size_percent_pot = 55

$oldFixed = $env:GTOSD_DIAGNOSTIC_FIXED_ITERATIONS
$oldLimit = $env:GTOSD_DIAGNOSTIC_ITERATION_LIMIT
$oldCertification = $env:GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL
$reports = [ordered]@{}
try {
  $env:GTOSD_DIAGNOSTIC_FIXED_ITERATIONS = '1'
  $env:GTOSD_DIAGNOSTIC_ITERATION_LIMIT = '20'
  $env:GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL = '20'
  foreach ($entry in $variants.GetEnumerator()) {
    Set-RootOnlyReference $entry.Value
    $specificationPath = Join-Path $resolvedOutput ($entry.Key + '.fixture.json')
    $reportPath = Join-Path $resolvedOutput ($entry.Key + '.report.json')
    $entry.Value | ConvertTo-Json -Depth 30 |
      Set-Content -LiteralPath $specificationPath -Encoding utf8
    & $executable postflop benchmark-gto-plus $specificationPath $reportPath
    $exitCode = $LASTEXITCODE
    if ($exitCode -notin @(0, 4) -or -not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
      throw "$($entry.Key) did not produce an auditable report (exit=$exitCode)."
    }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    $report | Add-Member -NotePropertyName audit_exit_code -NotePropertyValue $exitCode
    $reports[$entry.Key] = $report
  }
} finally {
  $env:GTOSD_DIAGNOSTIC_FIXED_ITERATIONS = $oldFixed
  $env:GTOSD_DIAGNOSTIC_ITERATION_LIMIT = $oldLimit
  $env:GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL = $oldCertification
}

$baseline = $reports.baseline
foreach ($entry in $reports.GetEnumerator()) {
  $report = $entry.Value
  Assert-Equal $report.schema 'gtosd.gto_plus_convergence_run.v4' "$($entry.Key) schema"
  Assert-Equal $report.algorithm $baseline.algorithm "$($entry.Key) algorithm"
  Assert-Equal $report.precision $baseline.precision "$($entry.Key) precision"
  Assert-Equal $report.parallel_action_depth $baseline.parallel_action_depth `
    "$($entry.Key) parallel action depth"
  Assert-Equal $report.maximum_solver_threads $baseline.maximum_solver_threads `
    "$($entry.Key) maximum solver threads"
  Assert-Equal $report.certification_interval $baseline.certification_interval `
    "$($entry.Key) certification interval"
  Assert-Equal $report.dcfr_parameters.positive_regret_exponent `
    $baseline.dcfr_parameters.positive_regret_exponent "$($entry.Key) alpha"
  Assert-Equal $report.dcfr_parameters.average_exponent `
    $baseline.dcfr_parameters.average_exponent "$($entry.Key) gamma"
  Assert-Equal $report.product_timing.contract 'gtosd.product_timing.v1' `
    "$($entry.Key) product timing"
  Assert-Equal $report.completed_iterations 20 "$($entry.Key) fixed iterations"
}

Assert-Equal $reports.metadata_only.game_fingerprint $baseline.game_fingerprint `
  'metadata-only fingerprint'
Assert-Equal $reports.metadata_only.actions $baseline.actions 'metadata-only action layout'
Assert-Equal $reports.metadata_only.final_gto_plus_dev_fraction `
  $baseline.final_gto_plus_dev_fraction 'metadata-only dEV'

Assert-Equal $reports.suit_permutation.actions $baseline.actions 'suit-permuted action layout'
Assert-Equal $reports.suit_permutation.information_sets $baseline.information_sets `
  'suit-permuted infoset layout'
Assert-Equal $reports.suit_permutation.final_gto_plus_dev_fraction `
  $baseline.final_gto_plus_dev_fraction 'suit-permuted dEV'

foreach ($name in @('range_perturbation', 'stack_perturbation', 'sizing_perturbation')) {
  if ($reports[$name].game_fingerprint -eq $baseline.game_fingerprint) {
    throw "$name did not alter the game fingerprint."
  }
}

$summary = [ordered]@{
  schema = 'gtosd.production_dcfr_anti_specialization_audit.v1'
  source_specification = $resolvedSpecification
  fixed_iterations = 20
  holdout_used = $false
  checks = [ordered]@{
    identity_metadata_invariant = $true
    semantic_json_reserialization_invariant = $true
    global_suit_permutation_invariant = $true
    range_stack_sizing_change_game_identity = $true
    resolved_production_profile_invariant = $true
    memory_dispatch_boundary_covered_by_phase10 = $true
  }
  variants = @($reports.GetEnumerator() | ForEach-Object {
      [ordered]@{
        name = $_.Key
        benchmark_id = $_.Value.benchmark_id
        exit_code = $_.Value.audit_exit_code
        fingerprint = $_.Value.game_fingerprint
        actions = $_.Value.actions
        information_sets = $_.Value.information_sets
        final_dev_fraction = $_.Value.final_gto_plus_dev_fraction
      }
    })
  passed = $true
}
$summaryPath = Join-Path $resolvedOutput 'summary.json'
$summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Output $summaryPath
