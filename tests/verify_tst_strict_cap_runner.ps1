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
    'GTOSD_PROFILE_HOTPATH_LIGHTWEIGHT',
    '-ge $ProcessMemoryBudgetBytes',
    'Stop-Process',
    "'strict_less_than'",
    "'git_show_head'",
    "'gtosd.gto_plus_convergence_run.v4'",
    "'gtosd.user_configured_process_memory_budget_run.v1'",
    "'user_configured_experiment'",
    "'production_dcfr'",
    "'exact_production_dcfr'"
)
foreach ($needle in $required) {
    if (-not $text.Contains($needle)) {
        throw "strict-cap runner is missing required token: $needle"
    }
}
$forbidden = @(
    '[uint64]$MemoryCapBytes = 2000000000',
    'gtosd.tst_strict_cap_run.v1',
    'gtosd.tst_strict_cap_process.v1',
    'gtosd.tst_strict_cap_validity.v1',
    '$report.solver_state_gate.passed'
)
foreach ($needle in $forbidden) {
    if ($text.Contains($needle)) {
        throw "user-configured budget runner retains forbidden legacy token: $needle"
    }
}
Write-Output 'verify_user_configured_process_memory_budget_runner=PASS'
