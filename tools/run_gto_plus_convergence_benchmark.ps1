param(
  [string]$BuildDir = "out/build/windows-release",
  [string]$OutputDir = "out/gto-plus-convergence",
  [string]$Specification = "benchmarks/fixtures/gto_plus_ahkhqh_101.json",
  [int]$Runs = 5,
  [switch]$EnforceGate
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))

function Resolve-RepositoryPath([string]$Path) {
  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }
  return [System.IO.Path]::GetFullPath((Join-Path $repository $Path))
}

if ($Runs -lt 5) {
  throw "The parity protocol requires at least five independent processes."
}

$resolvedBuild = Resolve-RepositoryPath $BuildDir
$resolvedOutput = Resolve-RepositoryPath $OutputDir
$resolvedSpecification = Resolve-RepositoryPath $Specification
$executable = Join-Path $resolvedBuild "apps/gto_cli/gto_cli.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
  throw "Release gto_cli not found at $executable"
}
if (-not (Test-Path -LiteralPath $resolvedSpecification -PathType Leaf)) {
  throw "Benchmark specification not found at $resolvedSpecification"
}
New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null

$specificationData = Get-Content -LiteralPath $resolvedSpecification -Raw | ConvertFrom-Json
if ($specificationData.schema -ne "gtosd.gto_plus_convergence_benchmark.v4" -or
    $specificationData.benchmark_id -notmatch "^GTP-[A-Z0-9]{2,}-[0-9]{3}$") {
  throw "Unexpected benchmark specification."
}
$gtoPlusSolverMemory = $specificationData.gto_plus_reference.solver_memory
if (-not $gtoPlusSolverMemory -or
    $gtoPlusSolverMemory.display_label -ne "Memory needed for solving" -or
    $gtoPlusSolverMemory.display_unit -ne "MB" -or
    $gtoPlusSolverMemory.normalization_rule -ne "decimal_mb_fixture_convention" -or
    $gtoPlusSolverMemory.semantic_class -ne "gto_plus_internal_pre_solve_estimate" -or
    $gtoPlusSolverMemory.comparability_status -ne "unresolved" -or
    [uint64]$gtoPlusSolverMemory.normalized_reference_bytes -eq 0) {
  throw "Invalid GTO+ solver-memory reference."
}

$commit = (git -C $repository rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
  throw "Unable to resolve the repository commit."
}
$statusLines = @(git -C $repository status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0) {
  throw "Unable to inspect the repository worktree."
}
$worktreeClean = $statusLines.Count -eq 0

$runReports = @()
for ($runIndex = 1; $runIndex -le $Runs; $runIndex++) {
  $runPath = Join-Path $resolvedOutput ("run-{0:D2}.json" -f $runIndex)
  & $executable postflop benchmark-gto-plus $resolvedSpecification $runPath
  $runExitCode = $LASTEXITCODE
  if ($runExitCode -ne 0 -and $runExitCode -ne 4) {
    throw "GTO+ convergence run $runIndex failed with exit code $LASTEXITCODE."
  }
  if (-not (Test-Path -LiteralPath $runPath -PathType Leaf)) {
    throw "GTO+ convergence run $runIndex did not produce $runPath"
  }
  $runReport = Get-Content -LiteralPath $runPath -Raw | ConvertFrom-Json
  $runReport | Add-Member -NotePropertyName run_index -NotePropertyValue $runIndex
  $runReports += $runReport
}

$elapsed = @($runReports | ForEach-Object { [double]$_.elapsed_seconds } | Sort-Object)
$middle = [int][Math]::Floor($elapsed.Count / 2)
$median = if (($elapsed.Count % 2) -eq 1) {
  $elapsed[$middle]
} else {
  ($elapsed[$middle - 1] + $elapsed[$middle]) / 2.0
}
$p95Index = [Math]::Max(0, [int][Math]::Ceiling(0.95 * $elapsed.Count) - 1)
$p95 = $elapsed[$p95Index]

$fingerprints = @($runReports | ForEach-Object { $_.game_fingerprint } | Sort-Object -Unique)
$allCorrect = @($runReports | Where-Object { -not $_.correctness_passed }).Count -eq 0
$allEvCorrect = @($runReports | Where-Object { -not $_.ev_correctness_passed }).Count -eq 0
$allActionFrequenciesCorrect =
  @($runReports | Where-Object { -not $_.action_frequency_correctness_passed }).Count -eq 0
$allConverged = @($runReports | Where-Object { -not $_.converged }).Count -eq 0
$allRelease = @($runReports | Where-Object { $_.build.configuration -ne "Release" }).Count -eq 0
$allRunReportsV4 =
  @($runReports | Where-Object { $_.schema -ne "gtosd.gto_plus_convergence_run.v4" }).Count -eq 0
$allMemoryComparisonsNotEvaluated =
  @($runReports | Where-Object {
      $_.memory_comparison.status -ne "not_evaluated" -or
      $null -ne $_.memory_comparison.passed
    }).Count -eq 0
$allBenchmarkRunsUnbudgeted =
  @($runReports | Where-Object {
      $_.solver_state_residency -ne "resident_vectors" -or
      $null -ne $_.resident_working_set_budget
    }).Count -eq 0
$consistentFingerprint = $fingerprints.Count -eq 1
$consistentStateBytes =
  @($runReports | ForEach-Object { [uint64]$_.solver_state_bytes } | Sort-Object -Unique).Count -eq 1
if (-not ($allRelease -and $allRunReportsV4 -and $allMemoryComparisonsNotEvaluated -and
          $allBenchmarkRunsUnbudgeted -and $consistentFingerprint -and $consistentStateBytes)) {
  throw "One or more build or reproducibility checks failed."
}

$referenceSeconds = [double]$specificationData.gto_plus_reference.elapsed_seconds
$gtoPlusSolverMemoryReferenceBytes =
  [double]$gtoPlusSolverMemory.normalized_reference_bytes
$solverStateBytes = [double]$runReports[0].solver_state_bytes
$peakRssBytes = [double](($runReports | ForEach-Object {
      [uint64]$_.process_memory.peak_rss_bytes
    } |
    Measure-Object -Maximum).Maximum)
$speedScore = 100.0 * $referenceSeconds / $median
$speedGateSeconds = $referenceSeconds / 0.90
$speedGatePassed = $median -le $speedGateSeconds
$referenceMetadataComplete = [bool]$specificationData.gto_plus_reference.metadata_complete
# correctness_passed gates on the reference node EV selected by the
# specification (gate_node, default the tree root). The all-node EV and
# action-frequency flags remain published as diagnostics and do not invalidate
# the measurement: per the 2026-08-02 analysis the conditional BTN EV cannot
# prove a different game unless the root posteriors are identical.
$measurementValid = $allCorrect -and $allConverged -and $allRelease -and
                    $consistentFingerprint -and $consistentStateBytes
$scientificComparisonReady = $measurementValid -and $worktreeClean -and
                             $referenceMetadataComplete

$processor = $null
$computer = $null
$operatingSystem = $null
try {
  $processor = Get-CimInstance Win32_Processor | Select-Object -First 1
  $computer = Get-CimInstance Win32_ComputerSystem
  $operatingSystem = Get-CimInstance Win32_OperatingSystem
} catch {
  Write-Warning "Hardware metadata collection failed: $($_.Exception.Message)"
  try {
    $processorRegistry = Get-ItemProperty -LiteralPath `
      "Registry::HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0"
    $processor = [pscustomobject]@{
      Name = $processorRegistry.ProcessorNameString
      NumberOfCores = $null
      NumberOfLogicalProcessors = [Environment]::ProcessorCount
    }
    Add-Type -AssemblyName Microsoft.VisualBasic
    $computerInfo = New-Object Microsoft.VisualBasic.Devices.ComputerInfo
    $computer = [pscustomobject]@{
      TotalPhysicalMemory = $computerInfo.TotalPhysicalMemory
    }
    $operatingSystem = [pscustomobject]@{
      Caption = [Environment]::OSVersion.VersionString
    }
  } catch {
    Write-Warning "Fallback hardware metadata collection failed: $($_.Exception.Message)"
  }
}
$hardwareMetadataComplete = $processor -and $computer -and $operatingSystem -and
                            $processor.Name -and
                            $processor.NumberOfLogicalProcessors -and
                            $computer.TotalPhysicalMemory
$scientificComparisonReady = $measurementValid -and $worktreeClean -and
                             $referenceMetadataComplete -and $hardwareMetadataComplete

$summary = [ordered]@{
  schema = "gtosd.gto_plus_convergence_summary.v4"
  benchmark_id = $specificationData.benchmark_id
  generated_at_utc = [DateTime]::UtcNow.ToString("o")
  repository = [ordered]@{
    commit = $commit
    worktree_clean = $worktreeClean
    dirty_entries = $statusLines
  }
  host = [ordered]@{
    cpu_name = if ($processor) { $processor.Name } else { $null }
    physical_cores = if ($processor) { $processor.NumberOfCores } else { $null }
    logical_processors = if ($processor) { $processor.NumberOfLogicalProcessors } else { $null }
    total_physical_memory_bytes = if ($computer) { $computer.TotalPhysicalMemory } else { $null }
    operating_system = if ($operatingSystem) { $operatingSystem.Caption } else { $null }
    solver_threads = [int]$specificationData.gtosd_run.maximum_solver_threads
  }
  protocol = [ordered]@{
    independent_processes = $Runs
    decision_statistic = "median"
    p95_method = "nearest_rank"
    target_metric = "maximum unilateral best-response gain / initial pot"
    target_dev_percent = [double]$specificationData.gto_plus_reference.target_dev_percent
    timer_scope = $specificationData.gtosd_run.timer_scope
    gto_plus_solver_memory_reference_bytes = [uint64]$gtoPlusSolverMemoryReferenceBytes
    memory_comparability_status = "unresolved"
  }
  reference = $specificationData.gto_plus_reference
  runs = $runReports
  aggregate = [ordered]@{
    elapsed_seconds_sorted = $elapsed
    median_elapsed_seconds = $median
    p95_elapsed_seconds = $p95
    solver_state_bytes = [uint64]$solverStateBytes
    process_memory = [ordered]@{
      peak_rss_bytes = [uint64]$peakRssBytes
      normative_gate = $null
    }
    solver_memory_accounting = [ordered]@{
      schema = "gtosd.solver_memory_accounting.v1"
      state_logical_bytes = [uint64]$solverStateBytes
      managed_payload_peak_bytes = $null
      managed_allocated_peak_bytes = $null
    }
    gto_plus_reference_memory = $gtoPlusSolverMemory
    memory_comparison = [ordered]@{
      status = "not_evaluated"
      passed = $null
      reason = "gto_plus_metric_semantics_unresolved"
    }
    speed_score_percent = $speedScore
    gto_plus_ev_checks = $runReports[0].gto_plus_ev_checks
    gto_plus_action_frequency_checks = $runReports[0].gto_plus_action_frequency_checks
  }
  gates = [ordered]@{
    measurement_valid = $measurementValid
    correctness_gate_passed = $allCorrect
    ev_correctness_passed = $allEvCorrect
    action_frequency_correctness_passed = $allActionFrequenciesCorrect
    hardware_metadata_complete = [bool]$hardwareMetadataComplete
    reference_metadata_complete = $referenceMetadataComplete
    worktree_clean = $worktreeClean
    scientific_comparison_ready = $scientificComparisonReady
    speed_threshold_seconds = $speedGateSeconds
    speed_passed = $speedGatePassed
    memory_comparison = [ordered]@{
      status = "not_evaluated"
      passed = $null
      reason = "gto_plus_metric_semantics_unresolved"
    }
    active_gate_passed = $scientificComparisonReady -and $speedGatePassed
    parity_gate_status = "not_evaluated_memory_comparability_unresolved"
    parity_gate_passed = $null
  }
}

$summaryPath = Join-Path $resolvedOutput "summary.json"
$summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Output $summaryPath

if ($EnforceGate -and -not $summary.gates.active_gate_passed) {
  throw "One or more active correctness/time gates did not pass. See $summaryPath"
}
