param(
    [UInt32]$TrainerProcessId = 6448
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$outputDir = Join-Path $repoRoot 'out\hu40_history7_solve'
$logPath = Join-Path $outputDir 'hu40_history7_t37000_finalizer.log'
$errorPath = Join-Path $outputDir 'hu40_history7_t37000_finalizer.stderr.log'
$viewerLogPath = Join-Path $outputDir 'hu40_history7_t37000_viewer.log'
$viewerErrorPath = Join-Path $outputDir 'hu40_history7_t37000_viewer.stderr.log'

function Invoke-CheckedCommand {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$FailureMessage
    )
    & $Executable @Arguments *>> $viewerLogPath
    if (-not $?) {
        throw "$FailureMessage (exit code $LASTEXITCODE)"
    }
}

try {
    Set-Location $repoRoot
    $trainer = Get-Process -Id $TrainerProcessId -ErrorAction SilentlyContinue
    if ($null -ne $trainer) {
        if ($trainer.ProcessName -ne 'gtosd_preflop_blueprint_train') {
            throw "Process $TrainerProcessId is not the HU40 trainer"
        }
        $trainer.WaitForExit()
        # ExitCode is not guaranteed to be exposed when attaching to an already
        # running Windows process. The required policy/checkpoint checks below
        # remain authoritative in that case.
        try {
            $trainerExitCode = $trainer.ExitCode
        } catch {
            $trainerExitCode = $null
        }
        if ($null -ne $trainerExitCode -and $trainerExitCode -ne 0) {
            throw "HU40 training to 37000 failed with exit code $trainerExitCode"
        }
    }

    $policy = Join-Path $repoRoot 'out\hu40_history7_solve\hu40_history7_t37000_policy.bin'
    $currentPolicy = Join-Path $repoRoot 'out\hu40_history7_solve\hu40_history7_t37000_current_policy.bin'
    $checkpoint = Join-Path $repoRoot 'out\hu40_history7_solve\hu40_history7_checkpoint.bin'
    foreach ($required in @($policy, $currentPolicy, $checkpoint)) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "HU40 training artifact is missing: $required"
        }
    }

    & (Join-Path $PSScriptRoot 'run_hu40_history7_solve.ps1') -FinalIteration 37000 *>> $logPath
    if (-not $?) {
        throw "HU40 37000 finalization failed"
    }

    $statePath = Join-Path $outputDir 'solve_state.json'
    $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    if ($state.status -ne 'COMPLETE' -or [UInt64]$state.final_iteration -ne 37000) {
        throw "HU40 final state is not COMPLETE at 37000"
    }

    $exporter = Join-Path $repoRoot 'out\build\windows-release-main-integration\benchmarks\gtosd_preflop_blueprint_export.exe'
    $python = 'C:\Users\GoryNickel\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
    $viewerRoot = Join-Path $repoRoot 'tools\hu_preflop_chart_viewer'
    $hu40Chart = 'out/viewer/hu40_history7_t37000_chart.json'
    $hu40Tree = 'out/viewer/hu40_postflop_tree.json'
    $certificate = 'out/hu40_history7_solve/hu40_history7_final_physical_certificate.json'
    $abstractBr = 'out/hu40_history7_solve/hu40_history7_t37000_abstract_br_exact.json'
    foreach ($required in @($exporter, $python, (Join-Path $repoRoot $certificate), (Join-Path $repoRoot $abstractBr))) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "HU40 viewer input is missing: $required"
        }
    }

    Remove-Item -LiteralPath $viewerLogPath, $viewerErrorPath -Force -ErrorAction SilentlyContinue
    $commonExportArguments = @(
        '--config', 'benchmarks/fixtures/preflop_blueprint_co40_test_v1.json',
        '--resources-dir', 'out/preflop_blueprint_resources',
        '--buckets-dir', 'out/preflop_blueprint_buckets_200_500_1000',
        '--history-rows', 'out/hierarchy32/history7_rebuilt.bin',
        '--policy', 'out/hu40_history7_solve/hu40_history7_t37000_policy.bin'
    )
    Invoke-CheckedCommand $exporter ($commonExportArguments + @(
            '--certificate', $certificate,
            '--eval-flops', '16',
            '--threads', '4',
            '--iterations', '37000',
            '--algorithm', 'dcfr_alternating_history7_lazy',
            '--output', $hu40Chart
        )) 'HU40 chart export failed'
    Invoke-CheckedCommand $exporter ($commonExportArguments + @(
            '--postflop-tree', $hu40Tree
        )) 'HU40 postflop tree export failed'

    Invoke-CheckedCommand $python @(
        (Join-Path $viewerRoot 'validate_chart_export.py'),
        $hu40Chart,
        'out/viewer/hu30_history7_t32000_chart.json',
        'out/viewer/hu20_history7_t16000_chart.json'
    ) 'HU40 chart validation failed'
    Invoke-CheckedCommand $python @(
        (Join-Path $viewerRoot 'validate_postflop_tree_export.py'),
        $hu40Tree
    ) 'HU40 postflop tree validation failed'
    Invoke-CheckedCommand $python @(
        (Join-Path $viewerRoot 'generate_chart_data.py'),
        '--blueprint', $hu40Chart,
        '--blueprint-label', 'HU40 · history7 · DCFR 37k',
        '--abstract-br', $abstractBr,
        '--blueprint', 'out/viewer/hu30_history7_t32000_chart.json',
        '--blueprint-label', 'HU30 · history7 · DCFR 32k',
        '--abstract-br', 'out/history7_optimized/hu30_history7_lazy_v2_t32000_abstract_br_exact.json',
        '--blueprint', 'out/viewer/hu20_history7_t16000_chart.json',
        '--blueprint-label', 'HU20 · history7 · DCFR 16k',
        '--abstract-br', 'out/history7_optimized/hu20_history7_lazy_v2_t16000_abstract_br_sample8.json',
        '--br-target', '0.03',
        '--postflop-tree', $hu40Tree
    ) 'HU40 viewer payload generation failed'

    $listener = netstat -ano |
        Select-String '^\s*TCP\s+127\.0\.0\.1:4173\s+\S+\s+LISTENING\s+(\d+)\s*$' |
        Select-Object -First 1
    if ($null -ne $listener -and $listener.Matches.Count -eq 1) {
        [UInt32]$listenerProcessId = $listener.Matches[0].Groups[1].Value
        Stop-Process -Id $listenerProcessId -Force -ErrorAction SilentlyContinue
    }
    Get-Process -Name 'gtosd_preflop_blueprint_export' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $exporter } |
        Stop-Process -Force
    Start-Sleep -Seconds 2

    $serverStdout = Join-Path $repoRoot 'out\viewer\server_hu40.stdout.log'
    $serverStderr = Join-Path $repoRoot 'out\viewer\server_hu40.stderr.log'
    Remove-Item -LiteralPath $serverStdout, $serverStderr -Force -ErrorAction SilentlyContinue
    $serverArguments = @(
        (Join-Path $viewerRoot 'serve_viewer.py'),
        '--port', '4173',
        '--backend', 'blueprint',
        '--executable', $exporter,
        '--config', 'benchmarks/fixtures/preflop_blueprint_co40_test_v1.json',
        '--resources-dir', 'out/preflop_blueprint_resources',
        '--buckets-dir', 'out/preflop_blueprint_buckets_200_500_1000',
        '--history-rows', 'out/hierarchy32/history7_rebuilt.bin',
        '--policy', 'out/hu40_history7_solve/hu40_history7_t37000_policy.bin',
        '--iterations', '37000',
        '--threads', '4',
        '--source-label', 'HU40-history7-DCFR-37k'
    )
    $server = Start-Process -FilePath $python -ArgumentList $serverArguments `
        -WorkingDirectory $repoRoot -RedirectStandardOutput $serverStdout `
        -RedirectStandardError $serverStderr -WindowStyle Hidden -PassThru
    $server.Id | Set-Content -LiteralPath (Join-Path $repoRoot 'out\viewer\server.pid') -Encoding ascii
    $healthy = $false
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        Start-Sleep -Seconds 2
        if ($server.HasExited) {
            break
        }
        try {
            $health = Invoke-RestMethod -Uri 'http://127.0.0.1:4173/api/health' -TimeoutSec 2
            if ($health.ok) {
                $healthy = $true
                break
            }
        } catch {
            # The worker can need several seconds to load the HU40 policy.
        }
    }
    if (-not $healthy) {
        throw 'HU40 viewer server did not become healthy on port 4173'
    }
} catch {
    $_ | Out-String | Tee-Object -FilePath $errorPath | Out-File -FilePath $viewerErrorPath -Encoding utf8
    throw
}
