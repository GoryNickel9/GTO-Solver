param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,
    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath,
    [Parameter(Mandatory = $true)]
    [string]$ProgressPath,
    [Parameter(Mandatory = $true)]
    [string]$OriginalProjectPath,
    [Parameter(Mandatory = $true)]
    [string]$ProjectCopyPath,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^GTP-[A-Z0-9]{2,}-[0-9]{3}$')]
    [string]$BenchmarkId,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-zA-Z0-9_.-]+$')]
    [string]$RunId,
    [string]$OutputRoot = '.tmp/gto-plus-black-box/runs',
    [ValidateRange(10, 10000)]
    [int]$SampleIntervalMilliseconds = 50,
    [ValidateRange(1, 86400)]
    [int]$TimeoutSeconds = 900,
    [ValidateRange(0.000001, 100.0)]
    [double]$TargetDevPercent = 1.0,
    [ValidateRange(0.000001, 1000000.0)]
    [double]$InitialPot = 16.0,
    [ValidateRange(1, 120)]
    [int]$ExternalIdleConfirmationSeconds = 8
)

$ErrorActionPreference = 'Stop'
$repository = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
. (Join-Path $PSScriptRoot 'gto_plus_black_box_common.ps1')

function Get-GtoObserverSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    }
    finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

function Get-GtoObserverDataIdentity {
    param([Parameter(Mandatory = $true)][string]$Path)

    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        path = $item.FullName
        sha256 = Get-GtoObserverSha256 -Path $item.FullName
        size_bytes = [uint64]$item.Length
        creation_time_utc = $item.CreationTimeUtc.ToString('o')
        last_write_utc = $item.LastWriteTimeUtc.ToString('o')
    }
}

$resolvedExecutable = Resolve-GtoFullPath -Path $ExecutablePath -BasePath $repository
$resolvedProgress = Resolve-GtoFullPath -Path $ProgressPath -BasePath $repository
$resolvedOriginal = Resolve-GtoFullPath -Path $OriginalProjectPath -BasePath $repository
$resolvedCopy = Resolve-GtoFullPath -Path $ProjectCopyPath -BasePath $repository
$resolvedOutputRoot = Resolve-GtoFullPath -Path $OutputRoot -BasePath $repository
foreach ($path in @($resolvedExecutable, $resolvedOriginal, $resolvedCopy)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required project file not found: $path"
    }
}
if (-not (Test-Path -LiteralPath $resolvedOutputRoot -PathType Container)) {
    [void](New-Item -ItemType Directory -Path $resolvedOutputRoot)
}
$runDirectory = Join-Path $resolvedOutputRoot $RunId
New-GtoImmutableDirectory -Path $runDirectory

$process = Get-Process -Id $ProcessId -ErrorAction Stop
$executableItem = Get-Item -LiteralPath $resolvedExecutable
$executableVersion = $executableItem.VersionInfo
$executableIdentity = [ordered]@{
    path = $executableItem.FullName
    sha256 = Get-GtoObserverSha256 -Path $executableItem.FullName
    size_bytes = [uint64]$executableItem.Length
    last_write_utc = $executableItem.LastWriteTimeUtc.ToString('o')
    file_version = $executableVersion.FileVersion
    product_version = $executableVersion.ProductVersion
    product_name = $executableVersion.ProductName
    company_name = $executableVersion.CompanyName
    identity_source = 'explicit_verified_path_without_runtime_authenticode_reload'
}
$originalIdentity = Get-GtoObserverDataIdentity -Path $resolvedOriginal
$copyIdentity = Get-GtoObserverDataIdentity -Path $resolvedCopy
if ($originalIdentity.sha256 -ne $copyIdentity.sha256) {
    throw 'Original and read-only project copy do not have the same SHA-256.'
}
if (-not (Get-Item -LiteralPath $resolvedCopy).IsReadOnly) {
    throw 'Project copy is not read-only.'
}

$armUtc = [DateTime]::UtcNow
$baselineRaw = if (Test-Path -LiteralPath $resolvedProgress -PathType Leaf) {
    Get-Content -LiteralPath $resolvedProgress -Raw
}
else { '' }
$baselineHash = if ($baselineRaw) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($baselineRaw)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
}
else { $null }

function ConvertFrom-GtoProgressRaw {
    param([AllowEmptyString()][string]$Raw)

    $lines = @($Raw -split "`r?`n" | Where-Object { $_.Trim() })
    if ($lines.Count -eq 0) {
        return [pscustomobject]@{ line_count = 0; last_line = $null; dev_antes = $null; dev_percent = $null }
    }
    $lastLine = $lines[-1]
    $firstField = ($lastLine -split "`t")[0].Trim()
    $value = 0.0
    if (-not [double]::TryParse(
            $firstField,
            [System.Globalization.NumberStyles]::Float,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [ref]$value)) {
        return [pscustomobject]@{ line_count = $lines.Count; last_line = $lastLine; dev_antes = $null; dev_percent = $null }
    }
    return [pscustomobject]@{
        line_count = $lines.Count
        last_line = $lastLine
        dev_antes = $value
        dev_percent = 100.0 * $value / $InitialPot
    }
}

$baselineParsed = ConvertFrom-GtoProgressRaw -Raw $baselineRaw
$manifest = [ordered]@{
    schema = 'gtosd.gto_plus_manual_observer.v1'
    benchmark_id = $BenchmarkId
    run_id = $RunId
    armed_at_utc = $armUtc.ToString('o')
    executable = $executableIdentity
    project_original = $originalIdentity
    project_copy = $copyIdentity
    project_copy_read_only = $true
    process_id = $ProcessId
    protocol = [ordered]@{
        target_dev_percent = $TargetDevPercent
        strict_target = $true
        initial_pot = $InitialPot
        sample_interval_milliseconds = $SampleIntervalMilliseconds
        timeout_seconds = $TimeoutSeconds
        manual_click = $true
        click_timestamp_captured = $false
        official_time_source_pending = 'native_gto_display_or_manual_marker'
    }
    baseline_progress = [ordered]@{
        sha256 = $baselineHash
        line_count = $baselineParsed.line_count
        last_line = $baselineParsed.last_line
    }
}
Write-GtoJsonAtomic -Path (Join-Path $runDirectory 'manifest.json') -Value $manifest -Depth 16

$encoding = New-Object System.Text.UTF8Encoding($false)
$telemetryWriter = New-Object System.IO.StreamWriter((Join-Path $runDirectory 'telemetry.jsonl'), $false, $encoding)
$devWriter = New-Object System.IO.StreamWriter((Join-Path $runDirectory 'dev-trace.jsonl'), $false, $encoding)
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$lastRaw = $baselineRaw
$lastProgressWriteUtc = $null
$generationStart = $null
$firstBelow = $null
$lastAtOrAbove = $null
$externalIdle = $null
$idleSinceSeconds = $null
$timedOut = $false
$observerFailure = $null
$previousCpuSeconds = $process.TotalProcessorTime.TotalSeconds
$previousElapsedSeconds = 0.0
$logicalProcessors = [Environment]::ProcessorCount

Write-Output ("OBSERVER_ARMED|{0}|{1}" -f $RunId, $armUtc.ToString('o'))

try {
    while ($stopwatch.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $sampleUtc = [DateTime]::UtcNow
        $elapsed = $stopwatch.Elapsed.TotalSeconds
        $process.Refresh()
        if ($process.HasExited) {
            $observerFailure = 'process_exited_before_external_idle'
            break
        }

        $cpuSeconds = $process.TotalProcessorTime.TotalSeconds
        $wallDelta = $elapsed - $previousElapsedSeconds
        $cpuDelta = $cpuSeconds - $previousCpuSeconds
        $normalizedCpu = if ($wallDelta -gt 0) { $cpuDelta / $wallDelta / $logicalProcessors } else { 0.0 }
        $progressWriteUtc = $null
        $raw = ''
        if (Test-Path -LiteralPath $resolvedProgress -PathType Leaf) {
            $progressItem = Get-Item -LiteralPath $resolvedProgress
            $progressWriteUtc = $progressItem.LastWriteTimeUtc
            if ($null -eq $lastProgressWriteUtc -or $progressWriteUtc -ne $lastProgressWriteUtc) {
                $raw = Get-Content -LiteralPath $resolvedProgress -Raw
            }
            else {
                $raw = $lastRaw
            }
        }

        if ($raw -ne $lastRaw) {
            $parsed = ConvertFrom-GtoProgressRaw -Raw $raw
            if (-not $generationStart -and $parsed.line_count -gt 0 -and
                ($baselineParsed.line_count -eq 0 -or $parsed.line_count -lt $baselineParsed.line_count)) {
                $generationStart = [ordered]@{
                    timestamp_utc = $sampleUtc.ToString('o')
                    elapsed_seconds = $elapsed
                    source = 'native_progress_new_generation'
                    line_count = $parsed.line_count
                }
                Write-Output ("RUN_GENERATION_OBSERVED|{0}|{1}" -f $RunId, $sampleUtc.ToString('o'))
            }
            if ($generationStart) {
                $state = if ($null -ne $parsed.dev_percent -and $parsed.dev_percent -lt $TargetDevPercent) {
                    'TARGET_FIRST_OBSERVED_BELOW_1'
                }
                else { 'TARGET_NOT_REACHED' }
                $record = [ordered]@{
                    timestamp_utc = $sampleUtc.ToString('o')
                    elapsed_seconds = $elapsed
                    raw_line = $parsed.last_line
                    raw_file = $raw
                    line_count = $parsed.line_count
                    dev_antes = $parsed.dev_antes
                    parsed_dev_percent = $parsed.dev_percent
                    source = 'native_progress_file'
                    state = $state
                }
                $devWriter.WriteLine(($record | ConvertTo-Json -Compress -Depth 8))
                $devWriter.Flush()
                if ($null -ne $parsed.dev_percent -and $parsed.dev_percent -ge $TargetDevPercent) {
                    $lastAtOrAbove = $record
                }
                if (-not $firstBelow -and $null -ne $parsed.dev_percent -and
                    $parsed.dev_percent -lt $TargetDevPercent) {
                    $firstBelow = $record
                    Write-Output ("TARGET_CROSSED|{0}|{1}|{2}" -f $RunId, $sampleUtc.ToString('o'), $parsed.dev_percent)
                }
            }
            $lastRaw = $raw
        }
        $lastProgressWriteUtc = $progressWriteUtc

        $telemetry = [ordered]@{
            timestamp_utc = $sampleUtc.ToString('o')
            elapsed_monotonic_seconds = $elapsed
            main_pid = $ProcessId
            process_cpu_seconds = $cpuSeconds
            cpu_delta_seconds = $cpuDelta
            normalized_cpu_fraction = $normalizedCpu
            working_set_bytes = [uint64]$process.WorkingSet64
            peak_working_set_bytes = [uint64]$process.PeakWorkingSet64
            private_bytes = [uint64]$process.PrivateMemorySize64
            virtual_bytes = [uint64]$process.VirtualMemorySize64
            thread_count = $process.Threads.Count
            handle_count = $process.HandleCount
            responding = $process.Responding
            progress_last_write_utc = if ($progressWriteUtc) { $progressWriteUtc.ToString('o') } else { $null }
            run_generation_observed = [bool]$generationStart
            target_crossed = [bool]$firstBelow
        }
        $telemetryWriter.WriteLine(($telemetry | ConvertTo-Json -Compress -Depth 8))
        if (($elapsed % 1.0) -lt ($SampleIntervalMilliseconds / 1000.0)) {
            $telemetryWriter.Flush()
        }

        if ($firstBelow -and $normalizedCpu -lt 0.02 -and $process.WorkingSet64 -lt 500000000) {
            if ($null -eq $idleSinceSeconds) { $idleSinceSeconds = $elapsed }
            if (($elapsed - $idleSinceSeconds) -ge $ExternalIdleConfirmationSeconds) {
                $externalIdle = [ordered]@{
                    timestamp_utc = $sampleUtc.ToString('o')
                    elapsed_seconds = $elapsed
                    source = 'external_cpu_and_working_set_diagnostic_only'
                }
                Write-Output ("EXTERNAL_IDLE|{0}|{1}" -f $RunId, $sampleUtc.ToString('o'))
                break
            }
        }
        else {
            $idleSinceSeconds = $null
        }

        $previousCpuSeconds = $cpuSeconds
        $previousElapsedSeconds = $elapsed
        Start-Sleep -Milliseconds $SampleIntervalMilliseconds
    }
    if (-not $externalIdle -and -not $observerFailure) { $timedOut = $true }
}
finally {
    $telemetryWriter.Dispose()
    $devWriter.Dispose()
}

$originalFinalHash = Get-GtoObserverSha256 -Path $resolvedOriginal
$copyFinalHash = Get-GtoObserverSha256 -Path $resolvedCopy
$validity = [ordered]@{
    schema = 'gtosd.gto_plus_black_box_validity.v1'
    valid = $false
    pending_manual_completion_evidence = [bool]($generationStart -and $firstBelow -and $externalIdle -and -not $timedOut -and -not $observerFailure)
    manual_start = $true
    click_timestamp_captured = $false
    run_generation = $generationStart
    last_at_or_above_target = $lastAtOrAbove
    first_below_target = $firstBelow
    external_idle = $externalIdle
    solution_consultable = $null
    timed_out = $timedOut
    observer_failure = $observerFailure
    original_project_unchanged = $originalFinalHash -eq $originalIdentity.sha256
    project_copy_unchanged = $copyFinalHash -eq $copyIdentity.sha256
    memory_metric_mapping = 'unresolved'
    limitations = @(
        'External idle is diagnostic and is not accepted as solution completion.',
        'Manual completion evidence and native GTO+ elapsed time are required before validity can be promoted.'
    )
}
Write-GtoJsonAtomic -Path (Join-Path $runDirectory 'validity.json') -Value $validity -Depth 16
Write-Output ("OBSERVER_FINISHED|{0}|{1}" -f $RunId, (Join-Path $runDirectory 'validity.json'))
if (-not $validity.pending_manual_completion_evidence) { exit 4 }
exit 0
