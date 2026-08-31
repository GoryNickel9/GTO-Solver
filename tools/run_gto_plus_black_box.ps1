param(
    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath,
    [Parameter(Mandatory = $true)]
    [string]$ProjectPath,
    [Parameter(Mandatory = $true)]
    [string]$SelectorProfile,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^GTP-[A-Z0-9]{2,}-[0-9]{3}$')]
    [string]$BenchmarkId,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-zA-Z0-9_.-]+$')]
    [string]$RunId,
    [string]$OutputRoot = '.tmp/gto-plus-black-box/runs',
    [ValidateRange(1, 10000)]
    [int]$SampleIntervalMilliseconds = 50,
    [ValidateRange(1, 86400)]
    [int]$TimeoutSeconds = 900,
    [ValidateRange(0.000001, 100.0)]
    [double]$TargetDevPercent = 1.0,
    [ValidateRange(1, 8)]
    [int]$MaximumSolverThreads = 8,
    [long]$SolverMemoryLimitBytes = 2000000000,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$repository = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
. (Join-Path $PSScriptRoot 'gto_plus_black_box_common.ps1')

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$resolvedExecutable = Resolve-GtoFullPath -Path $ExecutablePath -BasePath $repository
$resolvedProject = Resolve-GtoFullPath -Path $ProjectPath -BasePath $repository
$resolvedProfile = Resolve-GtoFullPath -Path $SelectorProfile -BasePath $repository
$resolvedOutputRoot = Resolve-GtoFullPath -Path $OutputRoot -BasePath $repository
foreach ($path in @($resolvedExecutable, $resolvedProject, $resolvedProfile)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required input not found: $path"
    }
}
if ($SolverMemoryLimitBytes -lt 1) {
    throw 'SolverMemoryLimitBytes must be positive.'
}
if (-not (Test-Path -LiteralPath $resolvedOutputRoot -PathType Container)) {
    [void](New-Item -ItemType Directory -Path $resolvedOutputRoot)
}
$runDirectory = Join-Path $resolvedOutputRoot $RunId
New-GtoImmutableDirectory -Path $runDirectory

$executableIdentity = Get-GtoFileIdentity -Path $resolvedExecutable
$projectIdentity = Get-GtoDataFileIdentity -Path $resolvedProject
$profile = Get-Content -LiteralPath $resolvedProfile -Raw | ConvertFrom-Json
if ($profile.schema -ne 'gtosd.gto_plus_selector_profile.v1') {
    throw 'Unexpected selector profile schema.'
}
if ($profile.executable_sha256 -ne $executableIdentity.sha256 -or
    $profile.product_version -ne $executableIdentity.product_version) {
    throw 'Selector profile does not match the executable identity.'
}
if (-not $profile.selectors.run_solver -or -not $profile.selectors.dev -or
    -not $profile.selectors.completion) {
    throw 'Selector profile lacks run_solver, dev, or completion authority.'
}
if ($profile.requires_coordinates -or $profile.requires_ocr) {
    throw 'Unsafe selector profile: coordinate-only or OCR authority is prohibited.'
}

$projectCopy = Join-Path $runDirectory ("project-copy{0}" -f ([System.IO.Path]::GetExtension($resolvedProject)))
Copy-Item -LiteralPath $resolvedProject -Destination $projectCopy
$projectCopyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $projectCopy).Hash
if ($projectCopyHash -ne $projectIdentity.sha256) {
    throw 'Project copy hash does not match the original.'
}
[System.IO.File]::SetAttributes($projectCopy, [System.IO.FileAttributes]::ReadOnly)

function Find-GtoElement {
    param(
        [Parameter(Mandatory = $true)]
        [System.Windows.Automation.AutomationElement]$Root,
        [Parameter(Mandatory = $true)]
        [object]$Selector
    )

    $all = $Root.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.Condition]::TrueCondition)
    $matches = New-Object System.Collections.Generic.List[object]
    foreach ($element in $all) {
        $current = $element.Current
        if ($Selector.name -and $current.Name -ne $Selector.name) { continue }
        if ($Selector.automation_id -and $current.AutomationId -ne $Selector.automation_id) { continue }
        if ($Selector.class_name -and $current.ClassName -ne $Selector.class_name) { continue }
        if ($Selector.control_type -and $current.ControlType.ProgrammaticName -ne $Selector.control_type) { continue }
        $matches.Add($element)
    }
    if ($matches.Count -ne 1) {
        throw "Selector matched $($matches.Count) elements; exactly one is required."
    }
    return $matches[0]
}

function Read-GtoElementText {
    param(
        [Parameter(Mandatory = $true)]
        [System.Windows.Automation.AutomationElement]$Element
    )

    $instance = $null
    if ($Element.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$instance)) {
        return [string]$instance.Current.Value
    }
    if ($Element.TryGetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern, [ref]$instance)) {
        return [string]$instance.DocumentRange.GetText(-1)
    }
    return [string]$Element.Current.Name
}

function Get-GtoTopWindow([int]$ProcessIdentifier) {
    $condition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ProcessIdProperty,
        $ProcessIdentifier)
    $windows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
        [System.Windows.Automation.TreeScope]::Children,
        $condition)
    if ($windows.Count -ne 1) {
        throw "Expected exactly one top-level GTO+ window; found $($windows.Count)."
    }
    return $windows[0]
}

function Get-GtoProcessTreeIds([int]$RootPid) {
    $known = New-Object System.Collections.Generic.HashSet[int]
    [void]$known.Add($RootPid)
    try {
        $all = @(Get-CimInstance Win32_Process -ErrorAction Stop | Select-Object ProcessId, ParentProcessId)
        $changed = $true
        while ($changed) {
            $changed = $false
            foreach ($item in $all) {
                if ($known.Contains([int]$item.ParentProcessId) -and -not $known.Contains([int]$item.ProcessId)) {
                    [void]$known.Add([int]$item.ProcessId)
                    $changed = $true
                }
            }
        }
    }
    catch {
        # Main-process metrics remain valid; the manifest records the fallback.
    }
    return @($known | Sort-Object)
}

function Test-GtoCompletion {
    param(
        [Parameter(Mandatory = $true)]
        [System.Windows.Automation.AutomationElement]$Root,
        [Parameter(Mandatory = $true)]
        [object]$Selector
    )

    $element = Find-GtoElement -Root $Root -Selector $Selector
    if ($Selector.expected_text_regex) {
        return (Read-GtoElementText -Element $element) -match $Selector.expected_text_regex
    }
    if ($null -ne $Selector.expected_enabled) {
        return $element.Current.IsEnabled -eq [bool]$Selector.expected_enabled
    }
    throw 'Completion selector has no expected state.'
}

$preexisting = @(Get-Process -Name ([System.IO.Path]::GetFileNameWithoutExtension($resolvedExecutable)) -ErrorAction SilentlyContinue)
if ($preexisting.Count -gt 0) {
    throw 'A GTO+ process is already running; fresh-process protocol refused.'
}

$manifest = [ordered]@{
    schema = 'gtosd.gto_plus_black_box_run.v1'
    benchmark_id = $BenchmarkId
    run_id = $RunId
    created_at_utc = [DateTime]::UtcNow.ToString('o')
    executable = $executableIdentity
    project_original = $projectIdentity
    project_copy_sha256 = $projectCopyHash
    selector_profile_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolvedProfile).Hash
    protocol = [ordered]@{
        target_dev_percent = $TargetDevPercent
        strict_target = $true
        maximum_solver_threads = $MaximumSolverThreads
        solver_memory_limit_bytes = $SolverMemoryLimitBytes
        sample_interval_milliseconds = $SampleIntervalMilliseconds
        timeout_seconds = $TimeoutSeconds
        dry_run = [bool]$DryRun
        clock = 'System.Diagnostics.Stopwatch'
        coordinate_authority = $false
        ocr_authority = $false
    }
}
Write-GtoJsonAtomic -Path (Join-Path $runDirectory 'manifest.json') -Value $manifest

$process = Start-Process -FilePath $resolvedExecutable -ArgumentList @($projectCopy) -PassThru
$launchDeadline = [DateTime]::UtcNow.AddSeconds([Math]::Min(120, $TimeoutSeconds))
$root = $null
while ([DateTime]::UtcNow -lt $launchDeadline) {
    $process.Refresh()
    if ($process.HasExited) {
        throw "GTO+ exited during launch with code $($process.ExitCode)."
    }
    try {
        $root = Get-GtoTopWindow -ProcessIdentifier $process.Id
        break
    }
    catch {
        Start-Sleep -Milliseconds 100
    }
}
if (-not $root) {
    throw 'GTO+ main window did not become uniquely observable.'
}

$runElement = Find-GtoElement -Root $root -Selector $profile.selectors.run_solver
$devElement = Find-GtoElement -Root $root -Selector $profile.selectors.dev
$completionElement = Find-GtoElement -Root $root -Selector $profile.selectors.completion
$invoke = $null
if (-not $runElement.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$invoke)) {
    throw 'Run Solver selector does not expose InvokePattern.'
}

$preflight = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    process_id = $process.Id
    window_title = $root.Current.Name
    run_solver_enabled = $runElement.Current.IsEnabled
    stale_dev_text = Read-GtoElementText -Element $devElement
    completion_initial = Test-GtoCompletion -Root $root -Selector $profile.selectors.completion
    configuration_verified = $false
    configuration = @()
}
foreach ($check in @($profile.configuration_checks)) {
    $element = Find-GtoElement -Root $root -Selector $check.selector
    $raw = Read-GtoElementText -Element $element
    $passed = if ($check.expected_regex) { $raw -match $check.expected_regex } else { $raw -eq $check.expected }
    $preflight.configuration += [ordered]@{
        id = $check.id
        raw = $raw
        expected = $check.expected
        expected_regex = $check.expected_regex
        passed = $passed
    }
}
$preflight.configuration_verified = @($preflight.configuration | Where-Object { -not $_.passed }).Count -eq 0
Write-GtoJsonAtomic -Path (Join-Path $runDirectory 'preflight.json') -Value $preflight
if (-not $preflight.run_solver_enabled -or -not $preflight.configuration_verified) {
    throw 'Pre-solve gate failed; Run Solver was not invoked.'
}

if ($DryRun) {
    $closed = $process.CloseMainWindow()
    [void]$process.WaitForExit(15000)
    $validity = [ordered]@{
        schema = 'gtosd.gto_plus_black_box_validity.v1'
        valid = -not $preflight.completion_initial
        dry_run = $true
        run_invoked = $false
        close_requested = $closed
        process_exited = $process.HasExited
        reasons = @()
    }
    Write-GtoJsonAtomic -Path (Join-Path $runDirectory 'validity.json') -Value $validity
    Write-Output (Join-Path $runDirectory 'validity.json')
    exit 0
}

$telemetryPath = Join-Path $runDirectory 'telemetry.jsonl'
$devTracePath = Join-Path $runDirectory 'dev-trace.jsonl'
$encoding = New-Object System.Text.UTF8Encoding($false)
$telemetryWriter = New-Object System.IO.StreamWriter($telemetryPath, $false, $encoding)
$devWriter = New-Object System.IO.StreamWriter($devTracePath, $false, $encoding)
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$startUtc = [DateTime]::UtcNow
$firstBelow = $null
$completion = $null
$lastDevRaw = $null
$staleDevRaw = $preflight.stale_dev_text
$runActivityObserved = $false
$timedOut = $false
$observerFailure = $null
$initialProjectHash = $projectCopyHash

try {
    $invoke.Invoke()
    while ($stopwatch.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $sampleUtc = [DateTime]::UtcNow
        $process.Refresh()
        if ($process.HasExited) {
            $observerFailure = 'process_exited_before_observed_completion'
            break
        }
        $treeIds = @(Get-GtoProcessTreeIds -RootPid $process.Id)
        $treeProcesses = @($treeIds | ForEach-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue })
        $treeWorkingSet = [uint64](($treeProcesses | Measure-Object WorkingSet64 -Sum).Sum)
        $treePrivate = [uint64](($treeProcesses | Measure-Object PrivateMemorySize64 -Sum).Sum)
        $devRaw = Read-GtoElementText -Element (Find-GtoElement -Root $root -Selector $profile.selectors.dev)
        $devPercent = ConvertFrom-GtoDevText -Text $devRaw
        $completionNow = Test-GtoCompletion -Root $root -Selector $profile.selectors.completion
        if ($devRaw -ne $staleDevRaw -or -not $completionNow) {
            $runActivityObserved = $true
        }
        $telemetry = [ordered]@{
            timestamp_utc = $sampleUtc.ToString('o')
            elapsed_monotonic_seconds = $stopwatch.Elapsed.TotalSeconds
            main_pid = $process.Id
            process_tree_pids = $treeIds
            process_count = $treeProcesses.Count
            process_cpu_seconds = $process.TotalProcessorTime.TotalSeconds
            working_set_bytes = [uint64]$process.WorkingSet64
            peak_working_set_bytes = [uint64]$process.PeakWorkingSet64
            private_bytes = [uint64]$process.PrivateMemorySize64
            virtual_bytes = [uint64]$process.VirtualMemorySize64
            process_tree_working_set_bytes = $treeWorkingSet
            process_tree_private_bytes = $treePrivate
            thread_count = $process.Threads.Count
            handle_count = $process.HandleCount
            responding = $process.Responding
            main_window_title = $root.Current.Name
            raw_dev_text = $devRaw
            parsed_dev_percent = $devPercent
        }
        $telemetryWriter.WriteLine(($telemetry | ConvertTo-Json -Compress -Depth 8))
        if ($devRaw -ne $lastDevRaw) {
            $state = if ($null -ne $devPercent -and $devPercent -lt $TargetDevPercent) {
                'TARGET_FIRST_OBSERVED_BELOW_1'
            }
            else {
                'TARGET_NOT_REACHED'
            }
            $devRecord = [ordered]@{
                timestamp_utc = $sampleUtc.ToString('o')
                elapsed_seconds = $stopwatch.Elapsed.TotalSeconds
                raw_text = $devRaw
                parsed_percent = $devPercent
                source = 'uia_value_or_text_pattern'
                state = $state
            }
            $devWriter.WriteLine(($devRecord | ConvertTo-Json -Compress))
            $devWriter.Flush()
            $lastDevRaw = $devRaw
            if ($runActivityObserved -and -not $firstBelow -and $null -ne $devPercent -and
                $devPercent -lt $TargetDevPercent) {
                $firstBelow = $devRecord
            }
        }
        if ($firstBelow -and $runActivityObserved -and $completionNow) {
            $completion = [ordered]@{
                timestamp_utc = $sampleUtc.ToString('o')
                elapsed_seconds = $stopwatch.Elapsed.TotalSeconds
                source = 'uia_completion_selector'
            }
            break
        }
        Start-Sleep -Milliseconds $SampleIntervalMilliseconds
    }
    if (-not $completion -and -not $observerFailure) {
        $timedOut = $true
    }
}
finally {
    $telemetryWriter.Dispose()
    $devWriter.Dispose()
}

$finalProjectHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $projectCopy).Hash
$closeRequested = $false
$processExited = $false
if ($completion) {
    $closeRequested = $process.CloseMainWindow()
    [void]$process.WaitForExit(15000)
    $processExited = $process.HasExited
}
$validity = [ordered]@{
    schema = 'gtosd.gto_plus_black_box_validity.v1'
    valid = [bool]($firstBelow -and $completion -and -not $timedOut -and
        -not $observerFailure -and $initialProjectHash -eq $finalProjectHash -and $processExited)
    dry_run = $false
    run_invoked = $true
    run_activity_observed = $runActivityObserved
    start_timestamp_utc = $startUtc.ToString('o')
    first_below_target = $firstBelow
    solution_consultable = $completion
    timed_out = $timedOut
    observer_failure = $observerFailure
    original_project_unchanged = ((Get-FileHash -Algorithm SHA256 -LiteralPath $resolvedProject).Hash -eq $projectIdentity.sha256)
    project_copy_unchanged = $initialProjectHash -eq $finalProjectHash
    close_requested = $closeRequested
    process_exited = $processExited
    memory_metric_mapping = 'unresolved'
    os_peak_is_not_treated_as_displayed_solver_memory = $true
    audit_screenshots_available = $false
    limitations = @('Screenshot capture is not implemented by the UIA-only runner.')
}
Write-GtoJsonAtomic -Path (Join-Path $runDirectory 'validity.json') -Value $validity -Depth 16
Write-Output (Join-Path $runDirectory 'validity.json')
if (-not $validity.valid) {
    exit 4
}
exit 0
