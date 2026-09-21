param(
    [UInt64]$MemoryLimitBytes = 12GB,
    [UInt64]$MinimumFreeDiskBytes = 30GB,
    [UInt64]$FinalIteration = 37000
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$outputDir = Join-Path $repoRoot 'out\hu40_history7_solve'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$buildDir = Join-Path $repoRoot 'out\build\windows-release-main-integration\benchmarks'
$trainer = Join-Path $buildDir 'gtosd_preflop_blueprint_train.exe'
$abstractBr = Join-Path $buildDir 'gtosd_preflop_blueprint_abstract_br.exe'
$certifier = Join-Path $buildDir 'gtosd_preflop_blueprint_certify.exe'
$config = 'benchmarks/fixtures/preflop_blueprint_co40_test_v1.json'
$resources = 'out/preflop_blueprint_resources'
$buckets = 'out/preflop_blueprint_buckets_200_500_1000'
$historyRows = 'out/hierarchy32/history7_rebuilt.bin'
$checkpoint = 'out/hu40_history7_solve/hu40_history7_checkpoint.bin'
$statePath = Join-Path $outputDir 'solve_state.json'
$targetAnte = 0.03

foreach ($required in @($trainer, $abstractBr, $certifier,
        (Join-Path $repoRoot $config), (Join-Path $repoRoot $resources),
        (Join-Path $repoRoot $buckets), (Join-Path $repoRoot $historyRows))) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required input is missing: $required"
    }
}

function Write-SolveEvent {
    param([string]$Event, [System.Collections.IDictionary]$Fields = @{})
    $record = [ordered]@{
        event = $Event
        utc = [DateTime]::UtcNow.ToString('o')
    }
    foreach ($key in $Fields.Keys) {
        $record[$key] = $Fields[$key]
    }
    [Console]::Out.WriteLine(($record | ConvertTo-Json -Compress))
}

function Save-SolveState {
    param([System.Collections.IDictionary]$State)
    $temporary = "$statePath.tmp"
    $State | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary -Encoding utf8
    Move-Item -LiteralPath $temporary -Destination $statePath -Force
}

function Assert-FreeDisk {
    $drive = Get-PSDrive -Name ([System.IO.Path]::GetPathRoot($repoRoot).Substring(0, 1))
    if ([UInt64]$drive.Free -lt $MinimumFreeDiskBytes) {
        throw "RESOURCE_LIMIT free disk $($drive.Free) is below $MinimumFreeDiskBytes"
    }
}

function Invoke-MonitoredProcess {
    param(
        [string]$Phase,
        [string]$Executable,
        [string[]]$Arguments,
        [string]$StdoutPath,
        [string]$StderrPath
    )
    Assert-FreeDisk
    Remove-Item -LiteralPath $StdoutPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $StderrPath -Force -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments `
        -WorkingDirectory $repoRoot -RedirectStandardOutput $StdoutPath `
        -RedirectStandardError $StderrPath -WindowStyle Hidden -PassThru
    $started = Get-Date
    [UInt64]$peak = 0
    $lastReport = -30
    $killed = $false
    while (-not $process.HasExited) {
        Start-Sleep -Seconds 2
        $process.Refresh()
        [UInt64]$sample = $process.WorkingSet64
        if ($sample -gt $peak) {
            $peak = $sample
        }
        $elapsed = [int]((Get-Date) - $started).TotalSeconds
        if ($elapsed - $lastReport -ge 30) {
            Write-SolveEvent 'phase_progress' @{
                phase = $Phase
                elapsed_seconds = $elapsed
                working_set_bytes = $sample
                peak_bytes = $peak
            }
            $lastReport = $elapsed
        }
        if ($peak -gt $MemoryLimitBytes) {
            Stop-Process -Id $process.Id -Force
            $killed = $true
            break
        }
    }
    $process.WaitForExit()
    $process.Refresh()
    $elapsedTotal = ((Get-Date) - $started).TotalSeconds
    $result = [ordered]@{
        phase = $Phase
        exit_code = $process.ExitCode
        killed_on_memory_limit = $killed
        elapsed_seconds = $elapsedTotal
        peak_bytes = $peak
        stdout = $StdoutPath
        stderr = $StderrPath
    }
    Write-SolveEvent 'phase_end' $result
    if ($killed) {
        throw "RESOURCE_LIMIT $Phase exceeded $MemoryLimitBytes bytes"
    }
    if ($process.ExitCode -ne 0) {
        $errorTail = if (Test-Path -LiteralPath $StderrPath) {
            (Get-Content -LiteralPath $StderrPath -Tail 20) -join [Environment]::NewLine
        } else {
            'stderr log missing'
        }
        throw "$Phase failed with exit code $($process.ExitCode): $errorTail"
    }
    return [pscustomobject]$result
}

$state = [ordered]@{
    schema = 'gtosd.research.hu40_history7_solve.v1'
    status = 'RUNNING'
    current_iteration = 0
    target_ante = $targetAnte
    memory_limit_bytes = $MemoryLimitBytes
    checkpoint = $checkpoint
    last_policy = ''
    last_abstract_br = ''
    last_abstract_max_gain = $null
    runs = @()
}
if (Test-Path -LiteralPath $statePath) {
    $loaded = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json -AsHashtable
    if ($loaded.schema -ne $state.schema -or $loaded.checkpoint -ne $checkpoint) {
        throw 'Existing solve state has an incompatible identity'
    }
    $state = $loaded
    $state.status = 'RUNNING'
}
Save-SolveState $state

try {
    while ($true) {
        [UInt64]$current = $state.current_iteration
        [UInt64]$target = if ($current -lt 16000) {
            16000
        } elseif ($current -lt 32000) {
            32000
        } else {
            [Math]::Min($current + 5000, $FinalIteration)
        }
        if ($current -ge $FinalIteration) {
            break
        }
        $tag = "t$target"
        $averagePolicy = "out/hu40_history7_solve/hu40_history7_${tag}_policy.bin"
        $currentPolicy = "out/hu40_history7_solve/hu40_history7_${tag}_current_policy.bin"
        $trainStdout = Join-Path $outputDir "hu40_history7_${tag}_train.jsonl"
        $trainStderr = Join-Path $outputDir "hu40_history7_${tag}_train.stderr.log"
        $trainArguments = @(
            '--config', $config,
            '--resources-dir', $resources,
            '--buckets-dir', $buckets,
            '--history-rows', $historyRows,
            '--checkpoint', $checkpoint,
            '--policy-out', $averagePolicy,
            '--current-policy-out', $currentPolicy,
            '--iterations', "$target",
            '--batch', '32',
            '--threads', '8',
            '--partition-target', '64',
            '--scheme', 'dcfr',
            '--update', 'alternating',
            '--batch-policy-refresh',
            '--lazy-discount',
            '--progress-every', '500',
            '--eval-every', '0'
        )
        if ($current -gt 0) {
            $trainArguments += '--resume'
        }
        Write-SolveEvent 'training_start' @{ from_iteration = $current; target_iteration = $target }
        $trainRun = Invoke-MonitoredProcess "train_$tag" $trainer $trainArguments `
            $trainStdout $trainStderr
        $state.current_iteration = $target
        $state.last_policy = $averagePolicy
        $state.runs += @($trainRun)
        Save-SolveState $state

        $abstractOutput = "out/hu40_history7_solve/hu40_history7_${tag}_abstract_br_exact.json"
        $abstractStdout = Join-Path $outputDir "hu40_history7_${tag}_abstract_br_exact.log"
        $abstractStderr = Join-Path $outputDir "hu40_history7_${tag}_abstract_br_exact.stderr.log"
        $abstractArguments = @(
            '--config', $config,
            '--resources-dir', $resources,
            '--buckets-dir', $buckets,
            '--history-rows', $historyRows,
            '--policy', $averagePolicy,
            '--output', $abstractOutput,
            '--threads', '8'
        )
        Write-SolveEvent 'abstract_br_start' @{ iteration = $target }
        $abstractRun = Invoke-MonitoredProcess "abstract_br_$tag" $abstractBr `
            $abstractArguments $abstractStdout $abstractStderr
        $abstractReport = Get-Content -LiteralPath (Join-Path $repoRoot $abstractOutput) -Raw |
            ConvertFrom-Json
        [double]$maxGain = $abstractReport.max_gain
        $state.last_abstract_br = $abstractOutput
        $state.last_abstract_max_gain = $maxGain
        $state.runs += @($abstractRun)
        Save-SolveState $state
        Write-SolveEvent 'abstract_br_result' @{
            iteration = $target
            max_gain = $maxGain
            target_ante = $targetAnte
            passes = ($maxGain -le $targetAnte)
        }
        if ($maxGain -le $targetAnte -or $target -ge $FinalIteration) {
            break
        }
    }

    [UInt64]$finalIteration = $state.current_iteration
    $finalTag = "t$finalIteration"
    $finalPolicy = $state.last_policy
    $certificate = 'out/hu40_history7_solve/hu40_history7_final_physical_certificate.json'
    $certificateState = 'out/hu40_history7_solve/hu40_history7_final_physical_br_state.bin'
    $certificateStdout = Join-Path $outputDir 'hu40_history7_final_physical_certificate.log'
    $certificateStderr = Join-Path $outputDir 'hu40_history7_final_physical_certificate.stderr.log'
    $certificateArguments = @(
        '--config', $config,
        '--resources-dir', $resources,
        '--buckets-dir', $buckets,
        '--history-rows', $historyRows,
        '--policy', $finalPolicy,
        '--output', $certificate,
        '--state', $certificateState,
        '--threads', '8',
        '--chunk', '16',
        '--target-pot-percent', '1'
    )
    Write-SolveEvent 'physical_br_start' @{ iteration = $finalIteration }
    $physicalRun = Invoke-MonitoredProcess 'physical_br_final' $certifier `
        $certificateArguments $certificateStdout $certificateStderr
    $state.runs += @($physicalRun)

    $coverage = 'out/hu40_history7_solve/hu40_history7_final_coverage.json'
    $coverageStdout = Join-Path $outputDir 'hu40_history7_final_coverage.log'
    $coverageStderr = Join-Path $outputDir 'hu40_history7_final_coverage.stderr.log'
    $coverageArguments = @(
        '--config', $config,
        '--resources-dir', $resources,
        '--buckets-dir', $buckets,
        '--history-rows', $historyRows,
        '--checkpoint', $checkpoint,
        '--resume',
        '--iterations', "$finalIteration",
        '--batch', '32',
        '--threads', '8',
        '--partition-target', '64',
        '--scheme', 'dcfr',
        '--update', 'alternating',
        '--batch-policy-refresh',
        '--lazy-discount',
        '--eval-every', '0',
        '--coverage-out', $coverage
    )
    Write-SolveEvent 'coverage_start' @{ iteration = $finalIteration }
    $coverageRun = Invoke-MonitoredProcess 'coverage_final' $trainer `
        $coverageArguments $coverageStdout $coverageStderr
    $state.runs += @($coverageRun)
    $physicalReport = Get-Content -LiteralPath (Join-Path $repoRoot $certificate) -Raw |
        ConvertFrom-Json
    $state.status = 'COMPLETE'
    $state.final_iteration = $finalIteration
    $state.physical_certificate = $certificate
    $state.physical_max_gain = [double]$physicalReport.max_gain
    $state.physical_passes = [bool]$physicalReport.passes_target
    $state.coverage = $coverage
    Save-SolveState $state
    Write-SolveEvent 'solve_complete' @{
        iteration = $finalIteration
        abstract_max_gain = $state.last_abstract_max_gain
        physical_max_gain = $state.physical_max_gain
        physical_passes = $state.physical_passes
    }
} catch {
    $state.status = 'FAILED'
    $state.error = $_.Exception.Message
    Save-SolveState $state
    Write-SolveEvent 'solve_failed' @{ error = $_.Exception.Message }
    throw
}
