param(
    [string]$RunsRoot = '.tmp/gto-plus-black-box/runs',
    [string]$SummaryPath = '.tmp/gto-plus-black-box/summary.json',
    [double]$GtoPlusReferenceSeconds = 116.09,
    [double]$GtosdTaskLimitSeconds = 128.988889,
    [double]$GtosdFreshSolverSeconds = 234.437726,
    [long]$GtosdFreshPeakRssBytes = 1971036160,
    [string]$MemoryMetricMapping = 'unresolved'
)

$ErrorActionPreference = 'Stop'
$repository = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
. (Join-Path $PSScriptRoot 'gto_plus_black_box_common.ps1')

$resolvedRunsRoot = Resolve-GtoFullPath -Path $RunsRoot -BasePath $repository
$resolvedSummary = Resolve-GtoFullPath -Path $SummaryPath -BasePath $repository
if (-not (Test-Path -LiteralPath $resolvedRunsRoot -PathType Container)) {
    throw "Runs root not found: $resolvedRunsRoot"
}
$summaryParent = Split-Path -Parent $resolvedSummary
if (-not (Test-Path -LiteralPath $summaryParent -PathType Container)) {
    [void](New-Item -ItemType Directory -Path $summaryParent)
}

function Read-GtoJsonIfPresent {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Read-GtoJsonLines {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return @()
    }
    $items = @()
    foreach ($line in Get-Content -LiteralPath $Path) {
        if (-not $line.Trim()) {
            continue
        }
        try {
            $items += ($line | ConvertFrom-Json)
        }
        catch {
            # A concurrently interrupted observer can leave one partial final line.
        }
    }
    return $items
}

function Get-GtoNestedProperty {
    param(
        [AllowNull()][object]$InputObject,
        [Parameter(Mandatory = $true)][string[]]$Path,
        [AllowNull()][object]$DefaultValue = $null
    )

    $current = $InputObject
    foreach ($name in $Path) {
        $current = Get-GtoOptionalProperty -InputObject $current -Name $name -DefaultValue $null
        if ($null -eq $current) {
            return $DefaultValue
        }
    }
    return $current
}

function Get-GtoStatistics {
    param([Parameter(Mandatory = $true)][AllowEmptyCollection()][double[]]$Values)

    if ($Values.Count -eq 0) {
        return [ordered]@{
            count = 0
            minimum = $null
            median = $null
            maximum = $null
            p95_nearest_rank = $null
            mean_diagnostic = $null
            sample_standard_deviation_diagnostic = $null
        }
    }
    $mean = [double](($Values | Measure-Object -Average).Average)
    $sampleStandardDeviation = $null
    if ($Values.Count -gt 1) {
        $sumSquares = 0.0
        foreach ($value in $Values) {
            $sumSquares += [Math]::Pow(([double]$value - $mean), 2.0)
        }
        $sampleStandardDeviation = [Math]::Sqrt($sumSquares / ($Values.Count - 1))
    }
    return [ordered]@{
        count = $Values.Count
        minimum = [double](($Values | Measure-Object -Minimum).Minimum)
        median = Get-GtoMedian -Values $Values
        maximum = [double](($Values | Measure-Object -Maximum).Maximum)
        p95_nearest_rank = Get-GtoNearestRankP95 -Values $Values
        mean_diagnostic = $mean
        sample_standard_deviation_diagnostic = $sampleStandardDeviation
    }
}

function Get-GtoTelemetryMaximum {
    param([object[]]$Items, [string]$PropertyName)

    $values = @($Items | ForEach-Object {
            Get-GtoOptionalProperty -InputObject $_ -Name $PropertyName
        } | Where-Object { $null -ne $_ })
    if ($values.Count -eq 0) {
        return $null
    }
    return [uint64](($values | Measure-Object -Maximum).Maximum)
}

function ConvertTo-GtoUtcDateTime {
    param([AllowNull()][object]$Value)

    if ($null -eq $Value) {
        return $null
    }
    return ([DateTime]$Value).ToUniversalTime()
}

$runs = New-Object System.Collections.Generic.List[object]
foreach ($runDirectoryItem in Get-ChildItem -LiteralPath $resolvedRunsRoot -Directory | Sort-Object Name) {
    $runDirectory = $runDirectoryItem.FullName
    $validity = Read-GtoJsonIfPresent -Path (Join-Path $runDirectory 'validity.json')
    $adjudication = Read-GtoJsonIfPresent -Path (Join-Path $runDirectory 'adjudication.json')
    if ($null -eq $validity -and $null -eq $adjudication) {
        continue
    }
    $manifest = Read-GtoJsonIfPresent -Path (Join-Path $runDirectory 'manifest.json')
    $telemetry = @(Read-GtoJsonLines -Path (Join-Path $runDirectory 'telemetry.jsonl'))
    $devTrace = @(Read-GtoJsonLines -Path (Join-Path $runDirectory 'dev-trace.jsonl'))

    $adjudicatedValid = Get-GtoOptionalProperty -InputObject $adjudication -Name 'valid' -DefaultValue $null
    $valid = if ($null -ne $adjudicatedValid) {
        [bool]$adjudicatedValid
    }
    else {
        [bool](Get-GtoOptionalProperty -InputObject $validity -Name 'valid' -DefaultValue $false)
    }
    $manualStart = [bool](Get-GtoOptionalProperty -InputObject $validity -Name 'manual_start' -DefaultValue $false)
    if (Get-GtoOptionalProperty -InputObject $adjudication -Name 'manual_action') {
        $manualStart = $true
    }

    $generationUtc = ConvertTo-GtoUtcDateTime -Value (Get-GtoNestedProperty -InputObject $validity -Path @('run_generation', 'timestamp_utc'))
    $firstBelowUtc = ConvertTo-GtoUtcDateTime -Value (Get-GtoNestedProperty -InputObject $validity -Path @('first_below_target', 'timestamp_utc'))
    $traceWindowTelemetry = @($telemetry | Where-Object {
            $timestamp = ConvertTo-GtoUtcDateTime -Value (Get-GtoOptionalProperty -InputObject $_ -Name 'timestamp_utc')
            $null -ne $timestamp -and $null -ne $generationUtc -and $null -ne $firstBelowUtc -and
            $timestamp -ge $generationUtc -and $timestamp -le $firstBelowUtc
        })
    $traceCpu = [double[]]@($traceWindowTelemetry | ForEach-Object {
            Get-GtoOptionalProperty -InputObject $_ -Name 'normalized_cpu_fraction'
        } | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
    $traceThreads = [double[]]@($traceWindowTelemetry | ForEach-Object {
            Get-GtoOptionalProperty -InputObject $_ -Name 'thread_count'
        } | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })

    $cadenceSeconds = @()
    for ($index = 1; $index -lt $devTrace.Count; ++$index) {
        $previous = ConvertTo-GtoUtcDateTime -Value (Get-GtoOptionalProperty -InputObject $devTrace[$index - 1] -Name 'timestamp_utc')
        $current = ConvertTo-GtoUtcDateTime -Value (Get-GtoOptionalProperty -InputObject $devTrace[$index] -Name 'timestamp_utc')
        if ($null -ne $previous -and $null -ne $current) {
            $cadenceSeconds += ($current - $previous).TotalSeconds
        }
    }
    $lastAtOrAboveUtc = ConvertTo-GtoUtcDateTime -Value (Get-GtoNestedProperty -InputObject $validity -Path @('last_at_or_above_target', 'timestamp_utc'))
    $crossingUncertaintySeconds = if ($null -ne $lastAtOrAboveUtc -and $null -ne $firstBelowUtc) {
        ($firstBelowUtc - $lastAtOrAboveUtc).TotalSeconds
    }
    else { $null }

    $officialSeconds = Get-GtoNestedProperty -InputObject $adjudication -Path @('timing', 'official_seconds')
    if ($null -eq $officialSeconds) {
        $officialSeconds = Get-GtoNestedProperty -InputObject $validity -Path @('solution_consultable', 'elapsed_seconds')
    }
    $targetSeconds = Get-GtoNestedProperty -InputObject $adjudication -Path @('timing', 'observer_generation_to_first_below_target_seconds')
    if ($null -eq $targetSeconds -and $null -ne $generationUtc -and $null -ne $firstBelowUtc) {
        $targetSeconds = ($firstBelowUtc - $generationUtc).TotalSeconds
    }
    if ($null -eq $targetSeconds) {
        $targetSeconds = Get-GtoNestedProperty -InputObject $validity -Path @('first_below_target', 'elapsed_seconds')
    }

    $peakWorkingSet = Get-GtoNestedProperty -InputObject $adjudication -Path @('memory', 'peak_working_set_bytes')
    if ($null -eq $peakWorkingSet) {
        $peakWorkingSet = Get-GtoTelemetryMaximum -Items $telemetry -PropertyName 'peak_working_set_bytes'
    }
    $peakPrivate = Get-GtoNestedProperty -InputObject $adjudication -Path @('memory', 'peak_private_bytes')
    if ($null -eq $peakPrivate) {
        $peakPrivate = Get-GtoTelemetryMaximum -Items $telemetry -PropertyName 'private_bytes'
    }
    $peakTreeWorkingSet = Get-GtoTelemetryMaximum -Items $telemetry -PropertyName 'process_tree_working_set_bytes'
    $replacementRunId = Get-GtoOptionalProperty -InputObject $adjudication -Name 'replacement_run_id'
    if ($null -eq $replacementRunId) {
        $replacementRunId = Get-GtoOptionalProperty -InputObject $validity -Name 'replacement_run_id'
    }
    $invalidReason = Get-GtoOptionalProperty -InputObject $adjudication -Name 'reason'
    if ($null -eq $invalidReason) {
        $invalidReason = Get-GtoOptionalProperty -InputObject $validity -Name 'observer_failure'
    }

    $runs.Add([pscustomobject][ordered]@{
        run_id = if ($manifest) { Get-GtoOptionalProperty -InputObject $manifest -Name 'run_id' -DefaultValue $runDirectoryItem.Name } else { $runDirectoryItem.Name }
        benchmark_id = if ($manifest) { Get-GtoOptionalProperty -InputObject $manifest -Name 'benchmark_id' } else { $null }
        directory = $runDirectory
        valid = $valid
        manual_start = $manualStart
        replacement_run_id = $replacementRunId
        invalid_reason = $invalidReason
        official_consultable_seconds = if ($null -ne $officialSeconds) { [double]$officialSeconds } else { $null }
        observed_generation_to_target_seconds = if ($null -ne $targetSeconds) { [double]$targetSeconds } else { $null }
        crossing_uncertainty_seconds = $crossingUncertaintySeconds
        dev_update_count = $devTrace.Count
        dev_update_cadence_statistics = Get-GtoStatistics -Values ([double[]]@($cadenceSeconds))
        peak_working_set_bytes = if ($null -ne $peakWorkingSet) { [uint64]$peakWorkingSet } else { $null }
        peak_private_bytes = if ($null -ne $peakPrivate) { [uint64]$peakPrivate } else { $null }
        peak_process_tree_working_set_bytes = $peakTreeWorkingSet
        displayed_memory_text = Get-GtoNestedProperty -InputObject $adjudication -Path @('completion', 'displayed_memory_text')
        trace_window_cpu_statistics = Get-GtoStatistics -Values $traceCpu
        trace_window_thread_count_statistics = Get-GtoStatistics -Values $traceThreads
        executable_sha256 = Get-GtoNestedProperty -InputObject $manifest -Path @('executable', 'sha256')
        executable_version = Get-GtoNestedProperty -InputObject $manifest -Path @('executable', 'product_version')
        project_sha256 = Get-GtoNestedProperty -InputObject $manifest -Path @('project_original', 'sha256')
        project_copy_sha256 = Get-GtoNestedProperty -InputObject $manifest -Path @('project_copy', 'sha256')
        original_project_unchanged = Get-GtoOptionalProperty -InputObject $validity -Name 'original_project_unchanged'
        project_copy_unchanged = Get-GtoOptionalProperty -InputObject $validity -Name 'project_copy_unchanged'
    })
}

$validRuns = @($runs | Where-Object { $_.valid })
$invalidRuns = @($runs | Where-Object { -not $_.valid })
$targetTimes = [double[]]@($validRuns | Where-Object { $null -ne $_.observed_generation_to_target_seconds } |
    ForEach-Object { [double]$_.observed_generation_to_target_seconds })
$consultableTimes = [double[]]@($validRuns | Where-Object { $null -ne $_.official_consultable_seconds } |
    ForEach-Object { [double]$_.official_consultable_seconds })
$peakWorkingSets = [double[]]@($validRuns | Where-Object { $null -ne $_.peak_working_set_bytes } |
    ForEach-Object { [double]$_.peak_working_set_bytes })
$peakPrivateBytes = [double[]]@($validRuns | Where-Object { $null -ne $_.peak_private_bytes } |
    ForEach-Object { [double]$_.peak_private_bytes })
$crossingUncertainties = [double[]]@($validRuns | Where-Object { $null -ne $_.crossing_uncertainty_seconds } |
    ForEach-Object { [double]$_.crossing_uncertainty_seconds })
$allTraceCpu = [double[]]@($validRuns | ForEach-Object {
        if ($_.trace_window_cpu_statistics.count -gt 0) { [double]$_.trace_window_cpu_statistics.mean_diagnostic }
    })
$manualCount = @($validRuns | Where-Object { $_.manual_start }).Count

$executableHashes = @($validRuns | ForEach-Object { $_.executable_sha256 } | Where-Object { $_ } | Sort-Object -Unique)
$projectHashes = @($validRuns | ForEach-Object { $_.project_sha256 } | Where-Object { $_ } | Sort-Object -Unique)
$benchmarkIds = @($validRuns | ForEach-Object { $_.benchmark_id } | Where-Object { $_ } | Sort-Object -Unique)
$identityConsistent = $executableHashes.Count -eq 1 -and $projectHashes.Count -eq 1 -and $benchmarkIds.Count -eq 1
$integrityPass = @($validRuns | Where-Object {
        $_.original_project_unchanged -eq $false -or $_.project_copy_unchanged -eq $false
    }).Count -eq 0
$characterizationValid = $validRuns.Count -ge 5 -and $targetTimes.Count -ge 5 -and
    $consultableTimes.Count -ge 5 -and $identityConsistent -and $integrityPass
$automationClassification = if ($characterizationValid -and $manualCount -eq 0) {
    'B. AUTOMATABLE AFTER ONE-TIME DISCOVERY — CHARACTERIZATION VALID'
}
elseif ($characterizationValid -and $manualCount -eq $validRuns.Count) {
    'C. PARTIALLY AUTOMATABLE — CHARACTERIZATION VALID WITH MANUAL MARKER'
}
else {
    'E. UNSAFE OR INCONCLUSIVE'
}

$medianConsultable = Get-GtoMedian -Values $consultableTimes
$firstValidManifest = $null
foreach ($run in $validRuns) {
    $candidate = Read-GtoJsonIfPresent -Path (Join-Path $run.directory 'manifest.json')
    if ($candidate) {
        $firstValidManifest = $candidate
        break
    }
}
$runArray = @()
foreach ($run in $runs) {
    $runArray += $run
}

$summary = [ordered]@{
    schema = 'gtosd.gto_plus_black_box_summary.v2'
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    automation_classification = $automationClassification
    characterization_valid = $characterizationValid
    installation_identity = if ($firstValidManifest) { Get-GtoOptionalProperty -InputObject $firstValidManifest -Name 'executable' } else { $null }
    project_identity = if ($firstValidManifest) { Get-GtoOptionalProperty -InputObject $firstValidManifest -Name 'project_original' } else { $null }
    configuration_identity = [ordered]@{
        benchmark_id = if ($benchmarkIds.Count -eq 1) { $benchmarkIds | Select-Object -First 1 } else { $null }
        target_dev_percent = 1.0
        strict_target = $true
        initial_pot_antes = 16.0
        displayed_solver_threads = 8
        identity_consistent_across_valid_runs = $identityConsistent
    }
    memory_metric_mapping = $MemoryMetricMapping
    runs = $runArray
    valid_run_count = $validRuns.Count
    invalid_run_count = $invalidRuns.Count
    replacement_run_count = @($runs | Where-Object { $_.replacement_run_id }).Count
    time_to_target_statistics = Get-GtoStatistics -Values $targetTimes
    time_to_target_source = 'observer first native progress generation to first observed strict dEV below 1%; click timestamp unavailable'
    time_to_consultable_statistics = Get-GtoStatistics -Values $consultableTimes
    time_to_consultable_source = 'native GTO+ Time field; Run Solver click to consultable solution'
    memory_statistics = [ordered]@{
        peak_working_set_bytes = Get-GtoStatistics -Values $peakWorkingSets
        peak_private_bytes = Get-GtoStatistics -Values $peakPrivateBytes
        maximum_process_tree_working_set_bytes = if ($validRuns.Count) { ($validRuns | Measure-Object peak_process_tree_working_set_bytes -Maximum).Maximum } else { $null }
        displayed_values = @($validRuns | ForEach-Object { $_.displayed_memory_text } | Where-Object { $_ })
    }
    cpu_statistics = [ordered]@{
        source = 'normalized process CPU fraction during native progress-generation-to-target window'
        per_run_mean_statistics = Get-GtoStatistics -Values $allTraceCpu
    }
    io_statistics = [ordered]@{
        measured = $false
        reason = 'The manual observer did not capture process I/O counters; no retroactive estimate is produced.'
    }
    dev_trace_statistics = [ordered]@{
        strict_less_than_target = $true
        target_percent = 1.0
        update_count_statistics = Get-GtoStatistics -Values ([double[]]@($validRuns | ForEach-Object { [double]$_.dev_update_count }))
        crossing_uncertainty_seconds = Get-GtoStatistics -Values $crossingUncertainties
    }
    thread_scaling = [ordered]@{
        performed = $false
        reason = 'Secondary gate closed: UIA exposes no semantic thread control, so changing 1/2/4/8 threads would require additional unverified manual configuration mutations.'
    }
    size_scaling = [ordered]@{
        performed = $false
        reason = 'Secondary gate closed: only TST received the full solve preflight and five-run validity protocol; cross-size configuration identity is not independently verified.'
    }
    comparison_with_gtosd = [ordered]@{
        gto_plus_reference_seconds = $GtoPlusReferenceSeconds
        gto_plus_median_consultable_seconds = $medianConsultable
        gto_plus_median_vs_reference_ratio = if ($medianConsultable) { $medianConsultable / $GtoPlusReferenceSeconds } else { $null }
        gto_plus_median_vs_reference_delta_percent = if ($medianConsultable) { 100.0 * ($medianConsultable / $GtoPlusReferenceSeconds - 1.0) } else { $null }
        gtosd_task_limit_seconds = $GtosdTaskLimitSeconds
        gto_plus_median_vs_gtosd_task_limit_ratio = if ($medianConsultable) { $medianConsultable / $GtosdTaskLimitSeconds } else { $null }
        gtosd_fresh_solver_seconds = $GtosdFreshSolverSeconds
        gtosd_fresh_peak_rss_bytes = $GtosdFreshPeakRssBytes
        gtosd_to_gto_plus_time_ratio = if ($medianConsultable) { $GtosdFreshSolverSeconds / $medianConsultable } else { $null }
        gto_plus_max_peak_working_set_to_gtosd_peak_rss_ratio = if ($peakWorkingSets.Count) { ($peakWorkingSets | Measure-Object -Maximum).Maximum / $GtosdFreshPeakRssBytes } else { $null }
    }
    limitations = @(
        'No claim about GTO+ internal algorithm, iteration count, numerical precision, layout, codec, or best-response cadence.',
        'Manual Run Solver actions are explicit markers; native GTO+ Time is authoritative because click timestamps were not captured by the observer.',
        'The UI memory readout is not treated as process memory unless independently mapped; OS working set and private bytes remain separate metrics.',
        'Thread and size scaling were closed by their secondary verification gates.',
        'Process I/O was not measured.'
    )
    final_outcome = if ($characterizationValid) { $automationClassification } else { 'E. UNSAFE OR INCONCLUSIVE' }
}

Write-GtoJsonAtomic -Path $resolvedSummary -Value $summary -Depth 32
Write-Output $resolvedSummary
