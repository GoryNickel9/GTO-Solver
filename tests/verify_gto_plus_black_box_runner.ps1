param()

$ErrorActionPreference = 'Stop'
$repository = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$tools = Join-Path $repository 'tools'
$requiredScripts = @(
    'gto_plus_black_box_common.ps1',
    'probe_gto_plus_installation.ps1',
    'probe_gto_plus_projects.ps1',
    'probe_gto_plus_uia.ps1',
    'run_gto_plus_black_box.ps1',
    'evaluate_gto_plus_black_box.ps1'
)

foreach ($name in $requiredScripts) {
    $path = Join-Path $tools $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing required tooling script: $path"
    }
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
    if ($errors.Count -gt 0) {
        throw "PowerShell parse failure in $name`: $($errors[0].Message)"
    }
}

$runnerPath = Join-Path $tools 'run_gto_plus_black_box.ps1'
$runner = Get-Content -LiteralPath $runnerPath -Raw
$runnerTokens = @(
    'New-GtoImmutableDirectory',
    'Get-FileHash -Algorithm SHA256',
    'System.Diagnostics.Stopwatch',
    'process_tree_pids',
    'parsed_dev_percent',
    '-lt $TargetDevPercent',
    'configuration_verified',
    'requires_coordinates',
    'requires_ocr',
    'TimeoutSeconds',
    'validity.json',
    'project_copy_unchanged',
    'run_activity_observed'
)
foreach ($token in $runnerTokens) {
    if (-not $runner.Contains($token)) {
        throw "Runner contract token is missing: $token"
    }
}
if ($runner -match '(?i)ocr_image|process memory scraping|ReadProcessMemory|WriteProcessMemory') {
    throw 'Runner contains a prohibited authority or process-memory operation.'
}

. (Join-Path $tools 'gto_plus_black_box_common.ps1')
if ((ConvertFrom-GtoDevText -Text 'dEV: 1.0%') -ne 1.0) {
    throw 'dEV parser failed decimal-point input.'
}
if ((ConvertFrom-GtoDevText -Text 'dEV: 0,91 %') -ne 0.91) {
    throw 'dEV parser failed decimal-comma input.'
}
if ($null -ne (ConvertFrom-GtoDevText -Text 'no value')) {
    throw 'dEV parser accepted an invalid value.'
}
$strictCrossing = (ConvertFrom-GtoDevText -Text '1.0%') -lt 1.0
if ($strictCrossing) {
    throw 'Strict crossing incorrectly accepted exactly 1.0%.'
}

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("gtosd-black-box-test-{0}" -f [guid]::NewGuid())
$runsRoot = Join-Path $testRoot 'runs'
[void](New-Item -ItemType Directory -Path $runsRoot)
try {
    $encoding = New-Object System.Text.UTF8Encoding($false)
    foreach ($index in 1..5) {
        $run = Join-Path $runsRoot ("run-{0:D2}" -f $index)
        [void](New-Item -ItemType Directory -Path $run)
        $manifest = [ordered]@{
            run_id = "run-{0:D2}" -f $index
            benchmark_id = 'GTP-TSTC9D-101'
        }
        $validity = [ordered]@{
            valid = $true
            manual_start = $false
            first_below_target = [ordered]@{ elapsed_seconds = 110.0 + $index }
            solution_consultable = [ordered]@{ elapsed_seconds = 112.0 + $index }
            memory_metric_mapping = 'unresolved'
        }
        $telemetry = [ordered]@{
            peak_working_set_bytes = 1900000000 + $index
            private_bytes = 1800000000 + $index
            process_tree_working_set_bytes = 1900000000 + $index
        }
        [System.IO.File]::WriteAllText((Join-Path $run 'manifest.json'), ($manifest | ConvertTo-Json), $encoding)
        [System.IO.File]::WriteAllText((Join-Path $run 'validity.json'), ($validity | ConvertTo-Json -Depth 8), $encoding)
        [System.IO.File]::WriteAllText((Join-Path $run 'telemetry.jsonl'), ($telemetry | ConvertTo-Json -Compress), $encoding)
    }
    $summaryPath = Join-Path $testRoot 'summary.json'
    & (Join-Path $tools 'evaluate_gto_plus_black_box.ps1') -RunsRoot $runsRoot -SummaryPath $summaryPath | Out-Null
    $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
    if (-not $summary.characterization_valid -or $summary.valid_run_count -ne 5) {
        throw 'Synthetic five-run aggregation did not become valid.'
    }
    if ($summary.time_to_target_statistics.median -ne 113.0 -or
        $summary.time_to_target_statistics.p95_nearest_rank -ne 115.0) {
        throw 'Synthetic median or nearest-rank p95 is incorrect.'
    }
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}

Write-Output 'GTO_PLUS_BLACK_BOX_TOOLING_TEST=PASS'
