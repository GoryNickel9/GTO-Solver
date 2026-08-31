param(
    [string]$RunsRoot = '.tmp/gto-plus-black-box/runs',
    [string]$SummaryPath = '.tmp/gto-plus-black-box/summary.json',
    [double]$GtosdFreshSolverSeconds = 234.437726,
    [long]$GtosdFreshPeakRssBytes = 1971036160
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

function Get-GtoTelemetryMaximum {
    param(
        [object[]]$Items,
        [string]$PropertyName
    )

    $values = @($Items | ForEach-Object {
            Get-GtoOptionalProperty -InputObject $_ -Name $PropertyName
        } | Where-Object { $null -ne $_ })
    if ($values.Count -eq 0) {
        return $null
    }
    return [uint64](($values | Measure-Object -Maximum).Maximum)
}

$runs = New-Object System.Collections.Generic.List[object]
foreach ($validityPath in Get-ChildItem -LiteralPath $resolvedRunsRoot -Filter 'validity.json' -File -Recurse) {
    $runDirectory = Split-Path -Parent $validityPath.FullName
    $validity = Get-Content -LiteralPath $validityPath.FullName -Raw | ConvertFrom-Json
    $manifestPath = Join-Path $runDirectory 'manifest.json'
    $manifest = if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
        Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    }
    else {
        $null
    }
    $telemetryPath = Join-Path $runDirectory 'telemetry.jsonl'
    $telemetry = if (Test-Path -LiteralPath $telemetryPath -PathType Leaf) {
        @(Get-Content -LiteralPath $telemetryPath | ForEach-Object { $_ | ConvertFrom-Json })
    }
    else {
        @()
    }
    $telemetry = @($telemetry)
    $peakWorkingSet = Get-GtoTelemetryMaximum -Items $telemetry -PropertyName 'peak_working_set_bytes'
    $peakPrivate = Get-GtoTelemetryMaximum -Items $telemetry -PropertyName 'private_bytes'
    $peakTreeWorkingSet = Get-GtoTelemetryMaximum -Items $telemetry -PropertyName 'process_tree_working_set_bytes'
    $firstBelowTarget = Get-GtoOptionalProperty -InputObject $validity -Name 'first_below_target'
    $solutionConsultable = Get-GtoOptionalProperty -InputObject $validity -Name 'solution_consultable'
    $runs.Add([pscustomobject][ordered]@{
        run_id = if ($manifest) { Get-GtoOptionalProperty -InputObject $manifest -Name 'run_id' -DefaultValue (Split-Path -Leaf $runDirectory) } else { Split-Path -Leaf $runDirectory }
        benchmark_id = if ($manifest) { Get-GtoOptionalProperty -InputObject $manifest -Name 'benchmark_id' } else { $null }
        directory = $runDirectory
        valid = [bool]$validity.valid
        manual_start = [bool](Get-GtoOptionalProperty -InputObject $validity -Name 'manual_start' -DefaultValue $false)
        replacement_run_id = Get-GtoOptionalProperty -InputObject $validity -Name 'replacement_run_id'
        first_below_target_seconds = if ($firstBelowTarget) {
            [double]$firstBelowTarget.elapsed_seconds
        }
        else { $null }
        solution_consultable_seconds = if ($solutionConsultable) {
            [double]$solutionConsultable.elapsed_seconds
        }
        else { $null }
        peak_working_set_bytes = $peakWorkingSet
        peak_private_bytes = $peakPrivate
        peak_process_tree_working_set_bytes = $peakTreeWorkingSet
        memory_metric_mapping = Get-GtoOptionalProperty -InputObject $validity -Name 'memory_metric_mapping' -DefaultValue 'unresolved'
        invalid_reason = Get-GtoOptionalProperty -InputObject $validity -Name 'observer_failure'
    })
}

$validRuns = @($runs | Where-Object { $_.valid })
$invalidRuns = @($runs | Where-Object { -not $_.valid })
$targetTimes = [double[]]@($validRuns | Where-Object { $null -ne $_.first_below_target_seconds } |
    ForEach-Object { $_.first_below_target_seconds })
$consultableTimes = [double[]]@($validRuns | Where-Object { $null -ne $_.solution_consultable_seconds } |
    ForEach-Object { $_.solution_consultable_seconds })
$manualCount = @($validRuns | Where-Object { $_.manual_start }).Count
$memoryMappings = @($validRuns | ForEach-Object { $_.memory_metric_mapping } | Sort-Object -Unique)
$memoryMappings = @($memoryMappings)
$automationClassification = if ($validRuns.Count -ge 5 -and $manualCount -eq 0) {
    'B. AUTOMATABLE AFTER ONE-TIME DISCOVERY'
}
elseif ($validRuns.Count -ge 5 -and $manualCount -eq $validRuns.Count) {
    'C. PARTIALLY AUTOMATABLE'
}
else {
    'E. UNSAFE OR INCONCLUSIVE'
}
$characterizationValid = $validRuns.Count -ge 5 -and $targetTimes.Count -ge 5 -and $consultableTimes.Count -ge 5
$medianConsultable = Get-GtoMedian -Values $consultableTimes
$memoryMetricMappingValue = 'unresolved_or_mixed'
if ($memoryMappings.Count -eq 1) {
    $memoryMetricMappingValue = [string]($memoryMappings | Select-Object -First 1)
}
$runArray = @()
foreach ($run in $runs) {
    $runArray += $run
}

$summary = [ordered]@{
    schema = 'gtosd.gto_plus_black_box_summary.v1'
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    automation_classification = $automationClassification
    characterization_valid = $characterizationValid
    installation_identity = $null
    project_identity = $null
    configuration_identity = $null
    memory_metric_mapping = $memoryMetricMappingValue
    runs = $runArray
    valid_run_count = $validRuns.Count
    invalid_run_count = $invalidRuns.Count
    replacement_run_count = @($runs | Where-Object { $_.replacement_run_id }).Count
    time_to_target_statistics = [ordered]@{
        count = $targetTimes.Count
        minimum = if ($targetTimes.Count) { ($targetTimes | Measure-Object -Minimum).Minimum } else { $null }
        median = Get-GtoMedian -Values $targetTimes
        maximum = if ($targetTimes.Count) { ($targetTimes | Measure-Object -Maximum).Maximum } else { $null }
        p95_nearest_rank = Get-GtoNearestRankP95 -Values $targetTimes
    }
    time_to_consultable_statistics = [ordered]@{
        count = $consultableTimes.Count
        minimum = if ($consultableTimes.Count) { ($consultableTimes | Measure-Object -Minimum).Minimum } else { $null }
        median = $medianConsultable
        maximum = if ($consultableTimes.Count) { ($consultableTimes | Measure-Object -Maximum).Maximum } else { $null }
        p95_nearest_rank = Get-GtoNearestRankP95 -Values $consultableTimes
    }
    memory_statistics = [ordered]@{
        maximum_working_set_bytes = if ($validRuns.Count) { ($validRuns | Measure-Object peak_working_set_bytes -Maximum).Maximum } else { $null }
        maximum_private_bytes = if ($validRuns.Count) { ($validRuns | Measure-Object peak_private_bytes -Maximum).Maximum } else { $null }
        maximum_process_tree_working_set_bytes = if ($validRuns.Count) { ($validRuns | Measure-Object peak_process_tree_working_set_bytes -Maximum).Maximum } else { $null }
    }
    cpu_statistics = $null
    io_statistics = $null
    dev_trace_statistics = [ordered]@{
        strict_less_than_target = $true
        target_percent = 1.0
    }
    thread_scaling = $null
    size_scaling = $null
    comparison_with_gtosd = [ordered]@{
        gtosd_fresh_solver_seconds = $GtosdFreshSolverSeconds
        gtosd_fresh_peak_rss_bytes = $GtosdFreshPeakRssBytes
        gto_plus_median_consultable_seconds = $medianConsultable
        gtosd_to_gto_plus_time_ratio = if ($medianConsultable) { $GtosdFreshSolverSeconds / $medianConsultable } else { $null }
    }
    limitations = @(
        'No claim about GTO+ internal algorithm, iteration count, precision, layout, codec, or BR cadence.',
        'Characterization is invalid until five runs have both strict target and consultable completion evidence.'
    )
    final_outcome = if ($characterizationValid) { $automationClassification } else { 'E. UNSAFE OR INCONCLUSIVE' }
}

Write-GtoJsonAtomic -Path $resolvedSummary -Value $summary -Depth 32
Write-Output $resolvedSummary
