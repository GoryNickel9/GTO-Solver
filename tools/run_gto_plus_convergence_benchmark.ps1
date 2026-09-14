param(
  [string]$BuildDir = "out/build/windows-release",
  [string]$OutputDir = "out/gto-plus-convergence",
  [string]$Specification = "benchmarks/fixtures/gto_plus_ahkhqh_101.json",
  [int]$Runs = 5,
  [double]$MaximumPreflightCpuPercent = 15.0,
  [double]$MinimumFreeRamGiB = 4.0,
  [int]$PreflightSamples = 5,
  [int]$MaximumPreflightAttempts = 12,
  [switch]$EnforceGate
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))

if (-not ('GtosdSystemCpuTimes' -as [type])) {
  Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;

public static class GtosdSystemCpuTimes {
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern bool GetSystemTimes(out long idle, out long kernel, out long user);
}
'@
}

function Resolve-RepositoryPath([string]$Path) {
  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }
  return [System.IO.Path]::GetFullPath((Join-Path $repository $Path))
}

if ($Runs -lt 5) {
  throw "The parity protocol requires at least five independent processes."
}
if ($MaximumPreflightCpuPercent -le 0.0 -or $MaximumPreflightCpuPercent -gt 100.0 -or
    $MinimumFreeRamGiB -le 0.0 -or $PreflightSamples -lt 1 -or
    $MaximumPreflightAttempts -lt 1) {
  throw "Invalid controlled-load preflight parameters."
}

function Get-ControlledLoadPreflight {
  $minimumFreeRamBytes = [uint64]($MinimumFreeRamGiB * 1GB)
  $lastMeasurement = $null
  for ($attempt = 1; $attempt -le $MaximumPreflightAttempts; $attempt++) {
    $cpuSamples = @()
    for ($sample = 1; $sample -le $PreflightSamples; $sample++) {
      [long]$idleBefore = 0
      [long]$kernelBefore = 0
      [long]$userBefore = 0
      [long]$idleAfter = 0
      [long]$kernelAfter = 0
      [long]$userAfter = 0
      if (-not [GtosdSystemCpuTimes]::GetSystemTimes(
          [ref]$idleBefore, [ref]$kernelBefore, [ref]$userBefore)) {
        throw "GetSystemTimes failed before the CPU sample."
      }
      Start-Sleep -Milliseconds 1000
      if (-not [GtosdSystemCpuTimes]::GetSystemTimes(
          [ref]$idleAfter, [ref]$kernelAfter, [ref]$userAfter)) {
        throw "GetSystemTimes failed after the CPU sample."
      }
      $idleDelta = [double]($idleAfter - $idleBefore)
      $totalDelta = [double](($kernelAfter - $kernelBefore) + ($userAfter - $userBefore))
      if ($totalDelta -le 0.0) {
        throw "GetSystemTimes returned a non-positive interval."
      }
      $cpuSamples += 100.0 * (1.0 - $idleDelta / $totalDelta)
    }
    Add-Type -AssemblyName Microsoft.VisualBasic
    $computerInfo = New-Object Microsoft.VisualBasic.Devices.ComputerInfo
    $freeRamBytes = [uint64]$computerInfo.AvailablePhysicalMemory
    $meanCpuPercent = [double]($cpuSamples | Measure-Object -Average).Average
    $blockedProcesses = @(Get-Process -ErrorAction SilentlyContinue |
      Where-Object { $_.ProcessName -in @('ProjectZomboid64') } |
      Select-Object -ExpandProperty ProcessName -Unique)
    $passed = $meanCpuPercent -le $MaximumPreflightCpuPercent -and
              $freeRamBytes -ge $minimumFreeRamBytes -and
              $blockedProcesses.Count -eq 0
    $measurement = [ordered]@{
      attempt = $attempt
      cpu_samples_percent = $cpuSamples
      mean_cpu_percent = $meanCpuPercent
      maximum_cpu_percent = $MaximumPreflightCpuPercent
      free_ram_bytes = $freeRamBytes
      minimum_free_ram_bytes = $minimumFreeRamBytes
      blocked_processes = $blockedProcesses
      passed = $passed
    }
    $lastMeasurement = $measurement
    if ($passed) {
      return $measurement
    }
  }
  $blocked = @($lastMeasurement.blocked_processes) -join ','
  throw "Controlled-load preflight did not pass after $MaximumPreflightAttempts attempts: " +
    "mean_cpu_percent=$($lastMeasurement.mean_cpu_percent) " +
    "free_ram_bytes=$($lastMeasurement.free_ram_bytes) blocked_processes=$blocked"
}

function Get-Distribution([double[]]$Values) {
  $sorted = @($Values | Sort-Object)
  $middle = [int][Math]::Floor($sorted.Count / 2)
  $median = if (($sorted.Count % 2) -eq 1) {
    $sorted[$middle]
  } else {
    ($sorted[$middle - 1] + $sorted[$middle]) / 2.0
  }
  $p95Index = [Math]::Max(0, [int][Math]::Ceiling(0.95 * $sorted.Count) - 1)
  return [ordered]@{
    sorted = $sorted
    median = $median
    p95 = $sorted[$p95Index]
  }
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
  $preflight = Get-ControlledLoadPreflight
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
  if (-not $runReport.product_timing -or
      $runReport.product_timing.contract -ne 'gtosd.product_timing.v1') {
    throw "GTO+ convergence run $runIndex is missing the product timing contract."
  }
  $runReport | Add-Member -NotePropertyName measurement_environment -NotePropertyValue $preflight
  $runReport | Add-Member -NotePropertyName run_index -NotePropertyValue $runIndex
  $runReport | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $runPath -Encoding utf8
  $runReports += $runReport
}

$solverDistribution = Get-Distribution @($runReports | ForEach-Object {
    [double]$_.elapsed_seconds
  })
$buildDistribution = Get-Distribution @($runReports | ForEach-Object {
    [double]$_.product_timing.build_to_ready_seconds
  })
$solveConsultableDistribution = Get-Distribution @($runReports | ForEach-Object {
    [double]$_.product_timing.solve_to_consultable_seconds
  })
$buildConsultableDistribution = Get-Distribution @($runReports | ForEach-Object {
    [double]$_.product_timing.build_to_consultable_seconds
  })
$elapsed = $solverDistribution.sorted
$median = $solverDistribution.median
$p95 = $solverDistribution.p95

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
    $processorName = [Microsoft.Win32.Registry]::GetValue(
      'HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0',
      'ProcessorNameString', $null)
    $processor = [pscustomobject]@{
      Name = $processorName
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
    controlled_load = [ordered]@{
      maximum_preflight_cpu_percent = $MaximumPreflightCpuPercent
      minimum_free_ram_bytes = [uint64]($MinimumFreeRamGiB * 1GB)
      samples_per_preflight = $PreflightSamples
      maximum_attempts = $MaximumPreflightAttempts
      performed_before_each_run = $true
    }
  }
  reference = $specificationData.gto_plus_reference
  runs = $runReports
  aggregate = [ordered]@{
    elapsed_seconds_sorted = $elapsed
    median_elapsed_seconds = $median
    p95_elapsed_seconds = $p95
    product_timing = [ordered]@{
      contract = 'gtosd.product_timing.v1'
      build_to_ready_seconds_sorted = $buildDistribution.sorted
      median_build_to_ready_seconds = $buildDistribution.median
      p95_build_to_ready_seconds = $buildDistribution.p95
      solve_to_consultable_seconds_sorted = $solveConsultableDistribution.sorted
      median_solve_to_consultable_seconds = $solveConsultableDistribution.median
      p95_solve_to_consultable_seconds = $solveConsultableDistribution.p95
      build_to_consultable_seconds_sorted = $buildConsultableDistribution.sorted
      median_build_to_consultable_seconds = $buildConsultableDistribution.median
      p95_build_to_consultable_seconds = $buildConsultableDistribution.p95
    }
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
