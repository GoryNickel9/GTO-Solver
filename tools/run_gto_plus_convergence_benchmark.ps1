param(
  [string]$BuildDir = "out/build/windows-release",
  [string]$OutputDir = "out/gto-plus-convergence",
  [string]$Specification = "benchmarks/fixtures/gto_plus_ahkhqh_003.json",
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
if ($specificationData.schema -ne "gtosd.gto_plus_convergence_benchmark.v1" -or
    $specificationData.benchmark_id -ne "GTP-AHKHQH-003") {
  throw "Unexpected benchmark specification."
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
$consistentFingerprint = $fingerprints.Count -eq 1
$consistentStateBytes =
  @($runReports | ForEach-Object { [uint64]$_.solver_state_bytes } | Sort-Object -Unique).Count -eq 1
if (-not ($allRelease -and $consistentFingerprint -and $consistentStateBytes)) {
  throw "One or more build or reproducibility checks failed."
}

$referenceSeconds = [double]$specificationData.gto_plus_reference.elapsed_seconds
$referenceMemory = [double]$specificationData.gto_plus_reference.solver_memory_bytes
$solverStateBytes = [double]$runReports[0].solver_state_bytes
$speedScore = 100.0 * $referenceSeconds / $median
$memoryScore = 100.0 * $referenceMemory / $solverStateBytes
$speedGateSeconds = $referenceSeconds / 0.90
$memoryGateBytes = $referenceMemory / 0.90
$speedGatePassed = $median -le $speedGateSeconds
$memoryGatePassed = $solverStateBytes -le $memoryGateBytes
$referenceMetadataComplete = [bool]$specificationData.gto_plus_reference.metadata_complete
$measurementValid = $allCorrect -and $allEvCorrect -and $allActionFrequenciesCorrect -and
                    $allConverged -and $allRelease -and $consistentFingerprint -and
                    $consistentStateBytes
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
  schema = "gtosd.gto_plus_convergence_summary.v1"
  benchmark_id = "GTP-AHKHQH-003"
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
  }
  reference = $specificationData.gto_plus_reference
  runs = $runReports
  aggregate = [ordered]@{
    elapsed_seconds_sorted = $elapsed
    median_elapsed_seconds = $median
    p95_elapsed_seconds = $p95
    solver_state_bytes = [uint64]$solverStateBytes
    speed_score_percent = $speedScore
    memory_score_percent = $memoryScore
    gto_plus_ev_checks = $runReports[0].gto_plus_ev_checks
    gto_plus_action_frequency_checks = $runReports[0].gto_plus_action_frequency_checks
  }
  gates = [ordered]@{
    measurement_valid = $measurementValid
    ev_correctness_passed = $allEvCorrect
    action_frequency_correctness_passed = $allActionFrequenciesCorrect
    hardware_metadata_complete = [bool]$hardwareMetadataComplete
    reference_metadata_complete = $referenceMetadataComplete
    worktree_clean = $worktreeClean
    scientific_comparison_ready = $scientificComparisonReady
    speed_threshold_seconds = $speedGateSeconds
    speed_passed = $speedGatePassed
    memory_threshold_bytes = $memoryGateBytes
    memory_passed = $memoryGatePassed
    parity_gate_passed = $scientificComparisonReady -and $speedGatePassed -and $memoryGatePassed
  }
}

$summaryPath = Join-Path $resolvedOutput "summary.json"
$summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Output $summaryPath

if ($EnforceGate -and -not $summary.gates.parity_gate_passed) {
  throw "The GTO+ parity gate did not pass. See $summaryPath"
}
