[CmdletBinding()]
param(
    [ValidateSet('ahk', 'th', 'tst')]
    [string] $Fixture = 'ahk',

    [ValidateRange(1, 1000000)]
    [uint64] $Iterations = 20,

    [ValidateRange(1, 1000000)]
    [uint64] $PhaseCap = 1000000,

    [ValidateRange(0, 7)]
    [byte] $ParallelActionDepth = 7,

    [switch] $Simultaneous,

    [string] $OutputRoot = '.tmp/sync-pcfr-trajectory-probe',

    [string] $BuildDirectory = 'out/build/windows-release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $repoRoot "$BuildDirectory/apps/gto_cli/gto_cli.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Missing Release executable: $executable"
}

$fixturePaths = @{
    ahk = 'benchmarks/fixtures/gto_plus_ahkhqh_101.json'
    th = 'benchmarks/fixtures/gto_plus_th7d6s_101.json'
    tst = 'benchmarks/fixtures/gto_plus_tstc9d_101.json'
}
$mode = if ($Simultaneous) { 'simultaneous' } else { 'alternating' }
$outputDirectory =
    Join-Path (Join-Path $repoRoot $OutputRoot) "$Fixture-i$Iterations-p$PhaseCap-$mode"
if (Test-Path -LiteralPath $outputDirectory) {
    throw "Probe output already exists: $outputDirectory"
}
[void](New-Item -ItemType Directory -Path $outputDirectory)

$source = Join-Path $repoRoot $fixturePaths[$Fixture]
$document = Get-Content -LiteralPath $source -Raw | ConvertFrom-Json
$document.gtosd_run.algorithm = 'dcfr'
$document.gtosd_run.dcfr_positive_regret_exponent = 1.5
$document.gtosd_run.dcfr_average_exponent = 2.0
$document.gtosd_run.averaging_delay = 0
$document.gtosd_run.state_precision = 'scaled_uint16_regret_strategy'
$document.gtosd_run.parallel_action_depth = $ParallelActionDepth
$document.gtosd_run.maximum_solver_threads =
    [uint64]$ParallelActionDepth + [uint64]1
$document.gtosd_run.certification_interval = $Iterations

$fixturePath = Join-Path $outputDirectory 'fixture.json'
$reportPath = Join-Path $outputDirectory 'report.json'
$logPath = Join-Path $outputDirectory 'run.log'
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText(
    $fixturePath,
    ($document | ConvertTo-Json -Depth 100),
    $utf8WithoutBom)

$env:GTOSD_DIAGNOSTIC_FIXED_ITERATIONS = '1'
$env:GTOSD_DIAGNOSTIC_ITERATION_LIMIT = [string]$Iterations
$env:GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL = [string]$Iterations
$env:GTOSD_DIAGNOSTIC_PURE_CFR_TRAJECTORY = '1'
$env:GTOSD_DIAGNOSTIC_PURE_CFR_PHASE_CAP = [string]$PhaseCap
if ($Simultaneous) {
    $env:GTOSD_DIAGNOSTIC_SIMULTANEOUS = '1'
}
try {
    # Windows PowerShell 5.1 wraps redirected native stderr in a
    # NativeCommandError. Capture both streams through Process instead so the
    # structured progress log remains raw and failure follows the exit code.
    $processInfo = New-Object System.Diagnostics.ProcessStartInfo
    $processInfo.FileName = $executable
    $processInfo.Arguments = 'postflop benchmark-gto-plus "{0}" "{1}"' -f `
        $fixturePath.Replace('"', '\"'), $reportPath.Replace('"', '\"')
    $processInfo.UseShellExecute = $false
    $processInfo.CreateNoWindow = $true
    $processInfo.RedirectStandardOutput = $true
    $processInfo.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $processInfo
    if (-not $process.Start()) {
        throw 'Failed to start trajectory probe process'
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    $exitCode = $process.ExitCode
    [System.IO.File]::WriteAllText(
        $logPath,
        "STDOUT`r`n$stdout`r`nSTDERR`r`n$stderr",
        $utf8WithoutBom)
    if ($stderr.Length -gt 0) {
        Write-Output $stderr.TrimEnd()
    }
    if ($stdout.Length -gt 0) {
        Write-Output $stdout.TrimEnd()
    }
    if ($exitCode -ne 0 -and
        -not ($exitCode -eq 4 -and (Test-Path -LiteralPath $reportPath -PathType Leaf))) {
        throw "Trajectory probe failed with exit code $exitCode"
    }
}
finally {
    Remove-Item Env:GTOSD_DIAGNOSTIC_FIXED_ITERATIONS -ErrorAction SilentlyContinue
    Remove-Item Env:GTOSD_DIAGNOSTIC_ITERATION_LIMIT -ErrorAction SilentlyContinue
    Remove-Item Env:GTOSD_DIAGNOSTIC_CERTIFICATION_INTERVAL -ErrorAction SilentlyContinue
    Remove-Item Env:GTOSD_DIAGNOSTIC_PURE_CFR_TRAJECTORY -ErrorAction SilentlyContinue
    Remove-Item Env:GTOSD_DIAGNOSTIC_PURE_CFR_PHASE_CAP -ErrorAction SilentlyContinue
    Remove-Item Env:GTOSD_DIAGNOSTIC_SIMULTANEOUS -ErrorAction SilentlyContinue
}

$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
$points = @($report.pure_cfr_trajectory.points)
$compressible = @($points | Where-Object { $_.minimum_phase -gt 1 }).Count
$minimum = ($points | Measure-Object -Property minimum_phase -Minimum).Minimum
$maximum = ($points | Measure-Object -Property minimum_phase -Maximum).Maximum
$scanSeconds = ($points | Measure-Object -Property pursuit_scan_seconds -Sum).Sum
[pscustomobject]@{
    schema = 'gtosd.sync_pcfr_trajectory_probe.v1'
    fixture = $Fixture
    update_mode = $mode
    report = $reportPath
    completed_iterations = $report.completed_iterations
    final_dev_percent = $report.final_gto_plus_dev_percent
    normalized_nash_conv = $report.final_normalized_nash_conv
    traversal_seconds = $report.phase_seconds.traversal
    peak_rss_bytes = $report.peak_rss_bytes
    solver_state_bytes = $report.solver_state_bytes
    compressible_iterations = $compressible
    compressible_ratio = if ($points.Count -eq 0) { 0.0 } else { $compressible / [double]$points.Count }
    minimum_phase = $minimum
    maximum_phase = $maximum
    pursuit_scan_seconds = $scanSeconds
} | ConvertTo-Json -Depth 5
