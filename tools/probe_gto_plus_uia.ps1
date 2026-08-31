param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,
    [ValidatePattern('^[a-z0-9_.-]+$')]
    [string]$SnapshotName = 'snapshot',
    [string]$OutputRoot = '.tmp/gto-plus-black-box/03-uia-probe',
    [ValidateRange(1, 64)]
    [int]$MaximumDepth = 24,
    [ValidateRange(1, 20000)]
    [int]$MaximumElements = 10000
)

$ErrorActionPreference = 'Stop'
$repository = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
. (Join-Path $PSScriptRoot 'gto_plus_black_box_common.ps1')

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$output = Resolve-GtoFullPath -Path $OutputRoot -BasePath $repository
if (-not (Test-Path -LiteralPath $output -PathType Container)) {
    [void](New-Item -ItemType Directory -Path $output)
}

$process = Get-Process -Id $ProcessId -ErrorAction Stop
$executablePath = $null
try { $executablePath = $process.Path } catch { $executablePath = $null }
if (-not $executablePath) {
    throw "Unable to resolve executable path for process $ProcessId"
}
$identity = Get-GtoFileIdentity -Path $executablePath

$condition = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty,
    $ProcessId)
$topWindows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
    [System.Windows.Automation.TreeScope]::Children,
    $condition)

$patternMap = [ordered]@{
    InvokePattern = [System.Windows.Automation.InvokePattern]::Pattern
    ValuePattern = [System.Windows.Automation.ValuePattern]::Pattern
    TextPattern = [System.Windows.Automation.TextPattern]::Pattern
    SelectionPattern = [System.Windows.Automation.SelectionPattern]::Pattern
    TogglePattern = [System.Windows.Automation.TogglePattern]::Pattern
    ExpandCollapsePattern = [System.Windows.Automation.ExpandCollapsePattern]::Pattern
    RangeValuePattern = [System.Windows.Automation.RangeValuePattern]::Pattern
}

function Get-ElementPatterns([System.Windows.Automation.AutomationElement]$Element) {
    $names = New-Object System.Collections.Generic.List[string]
    foreach ($entry in $patternMap.GetEnumerator()) {
        $instance = $null
        if ($Element.TryGetCurrentPattern($entry.Value, [ref]$instance)) {
            $names.Add($entry.Key)
        }
    }
    return @($names | ForEach-Object { $_ })
}

function Convert-Element([System.Windows.Automation.AutomationElement]$Element, [int]$Index, [int]$Depth) {
    $current = $Element.Current
    $rectangle = $current.BoundingRectangle
    return [ordered]@{
        index = $Index
        depth = $Depth
        name = $current.Name
        automation_id = $current.AutomationId
        class_name = $current.ClassName
        control_type = $current.ControlType.ProgrammaticName
        framework_id = $current.FrameworkId
        process_id = $current.ProcessId
        is_enabled = $current.IsEnabled
        is_offscreen = $current.IsOffscreen
        is_keyboard_focusable = $current.IsKeyboardFocusable
        has_keyboard_focus = $current.HasKeyboardFocus
        bounding_rectangle = [ordered]@{
            x = $rectangle.X
            y = $rectangle.Y
            width = $rectangle.Width
            height = $rectangle.Height
        }
        supported_patterns = @(Get-ElementPatterns -Element $Element)
    }
}

$elements = New-Object System.Collections.Generic.List[object]
$selectorCandidates = New-Object System.Collections.Generic.List[object]
$walker = [System.Windows.Automation.TreeWalker]::RawViewWalker
$keywords = '(?i)run solver|stop|pause|dEV|target|thread|memory|status|progress|board|pot|stack|rake|range|bet|raise|solution|save|close|completed'

function Visit-Element(
    [System.Windows.Automation.AutomationElement]$Element,
    [int]$Depth) {
    if ($elements.Count -ge $MaximumElements -or $Depth -gt $MaximumDepth) {
        return
    }
    $record = Convert-Element -Element $Element -Index $elements.Count -Depth $Depth
    $elements.Add($record)
    if (($record.name -match $keywords) -or ($record.automation_id -match $keywords)) {
        $selectorCandidates.Add([ordered]@{
            index = $record.index
            name = $record.name
            automation_id = $record.automation_id
            class_name = $record.class_name
            control_type = $record.control_type
            supported_patterns = $record.supported_patterns
            robust_attribute_count = @(
                $record.name,
                $record.automation_id,
                $record.class_name,
                $record.control_type
            ).Where({ $_ }).Count
        })
    }
    $child = $walker.GetFirstChild($Element)
    while ($child -and $elements.Count -lt $MaximumElements) {
        Visit-Element -Element $child -Depth ($Depth + 1)
        $child = $walker.GetNextSibling($child)
    }
}

foreach ($window in $topWindows) {
    Visit-Element -Element $window -Depth 0
}

$runCandidates = @($selectorCandidates | Where-Object {
        $_.name -match '(?i)^run solver$' -and $_.supported_patterns -contains 'InvokePattern'
    })
$devCandidates = @($selectorCandidates | Where-Object {
        $_.name -match '(?i)dEV' -and
        (($_.supported_patterns -contains 'ValuePattern') -or
         ($_.supported_patterns -contains 'TextPattern') -or $_.name)
    })
$completionCandidates = @($selectorCandidates | Where-Object {
        $_.name -match '(?i)completed|solution' -or
        ($_.name -match '(?i)^run solver$' -and $_.supported_patterns -contains 'InvokePattern')
    })
$classification = if ($runCandidates.Count -gt 0 -and $devCandidates.Count -gt 0 -and $completionCandidates.Count -gt 0) {
    'UIA_FULL'
}
elseif ($selectorCandidates.Count -gt 0) {
    'UIA_PARTIAL'
}
else {
    'UIA_NONE'
}

$snapshot = [ordered]@{
    schema = 'gtosd.gto_plus_uia_snapshot.v1'
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    snapshot_name = $SnapshotName
    executable = $identity
    process = [ordered]@{
        pid = $ProcessId
        main_window_title = $process.MainWindowTitle
        responding = $process.Responding
    }
    limits = [ordered]@{
        maximum_depth = $MaximumDepth
        maximum_elements = $MaximumElements
        truncated = $elements.Count -ge $MaximumElements
    }
    windows = $topWindows.Count
    elements = @($elements | ForEach-Object { $_ })
    selector_candidates = @($selectorCandidates | ForEach-Object { $_ })
}

$capability = [ordered]@{
    schema = 'gtosd.gto_plus_uia_capability.v1'
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    classification = $classification
    executable_sha256 = $identity.sha256
    product_version = $identity.product_version
    can_launch = $true
    can_open_project = $false
    can_verify_project_loaded = $false
    can_read_configuration = $false
    can_detect_pre_solve = $false
    can_invoke_run = $runCandidates.Count -eq 1
    can_read_dev = $devCandidates.Count -ge 1
    can_detect_target_crossing = $devCandidates.Count -ge 1
    can_detect_completion = $completionCandidates.Count -ge 1
    can_close_cleanly = $false
    requires_coordinates = $runCandidates.Count -eq 0
    requires_ocr = $devCandidates.Count -eq 0
    requires_user = $runCandidates.Count -eq 0 -or $devCandidates.Count -eq 0 -or $completionCandidates.Count -eq 0
    probe_is_read_only = $true
}

$snapshotPath = Join-Path $output ("uia-{0}.json" -f $SnapshotName)
if (Test-Path -LiteralPath $snapshotPath) {
    throw "Snapshot already exists and will not be overwritten: $snapshotPath"
}
Write-GtoJsonAtomic -Path $snapshotPath -Value $snapshot -Depth 64
if (-not (Test-Path -LiteralPath (Join-Path $output 'executable.json'))) {
    Write-GtoJsonAtomic -Path (Join-Path $output 'executable.json') -Value $identity
}
if (-not (Test-Path -LiteralPath (Join-Path $output 'windows.json'))) {
    Write-GtoJsonAtomic -Path (Join-Path $output 'windows.json') -Value ([ordered]@{
            generated_at_utc = [DateTime]::UtcNow.ToString('o')
            process_id = $ProcessId
            top_level_window_count = $topWindows.Count
            main_window_title = $process.MainWindowTitle
        })
}
if (-not (Test-Path -LiteralPath (Join-Path $output 'selector-candidates.json'))) {
    Write-GtoJsonAtomic -Path (Join-Path $output 'selector-candidates.json') -Value @($selectorCandidates | ForEach-Object { $_ })
}
if (-not (Test-Path -LiteralPath (Join-Path $output 'capability.json'))) {
    Write-GtoJsonAtomic -Path (Join-Path $output 'capability.json') -Value $capability
}
Write-Output $snapshotPath
