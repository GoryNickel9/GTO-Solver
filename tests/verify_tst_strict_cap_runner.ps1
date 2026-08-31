$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'tools/run_tst_strict_cap.ps1'
$null = [ScriptBlock]::Create((Get-Content -LiteralPath $scriptPath -Raw))
$text = Get-Content -LiteralPath $scriptPath -Raw
$required = @(
    'run.json',
    'report.json',
    'stdout.log',
    'stderr.log',
    'environment.json',
    'process.json',
    'validity.json',
    'PeakWorkingSet64',
    '-ge $MemoryCapBytes',
    'Stop-Process',
    "'strict_less_than'",
    "'git_show_head'"
)
foreach ($needle in $required) {
    if (-not $text.Contains($needle)) {
        throw "strict-cap runner is missing required token: $needle"
    }
}
Write-Output 'verify_tst_strict_cap_runner=PASS'
