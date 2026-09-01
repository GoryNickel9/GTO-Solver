param(
    [ValidateRange(1, 20)]
    [int]$StartRun = 2,
    [ValidateRange(1, 20)]
    [int]$EndRun = 5,
    [ValidateRange(1.0, 100.0)]
    [double]$MaximumIdleCpuPercent = 15.0,
    [ValidateRange(2000000000, 128000000000)]
    [uint64]$MinimumFreeMemoryBytes = 4000000000,
    [ValidateRange(1, 1000)]
    [int]$MaximumPreflightAttempts = 120,
    [string]$OutputRoot = 'out/production-final-head-20260901'
)

$ErrorActionPreference = 'Stop'
if ($EndRun -lt $StartRun) {
    throw 'EndRun must be greater than or equal to StartRun.'
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$suiteRoot = Join-Path $repoRoot $OutputRoot
[void](New-Item -ItemType Directory -Path $suiteRoot -Force)
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
$preflights = @()

function Wait-ForControlledLoad([string]$RunId) {
    for ($attempt = 1; $attempt -le $MaximumPreflightAttempts; ++$attempt) {
        if (Get-Process -Name ProjectZomboid64 -ErrorAction SilentlyContinue) {
            throw 'ProjectZomboid64 is running; certification comparability is invalid.'
        }

        $cpuSamples = @()
        for ($sample = 0; $sample -lt 5; ++$sample) {
            $cpuSamples += [double](Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor `
                -Filter "Name='_Total'").PercentProcessorTime
            if ($sample -lt 4) { Start-Sleep -Seconds 1 }
        }

        $cpuMean = ($cpuSamples | Measure-Object -Average).Average
        $freeBytes = [uint64](Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory * 1024U
        if ($cpuMean -le $MaximumIdleCpuPercent -and $freeBytes -ge $MinimumFreeMemoryBytes) {
            $preflight = [ordered]@{
                run_id = $RunId
                utc = [DateTime]::UtcNow.ToString('o')
                idle_cpu_samples_percent = $cpuSamples
                idle_cpu_mean_percent = $cpuMean
                free_memory_bytes = $freeBytes
                power_scheme = (powercfg /getactivescheme | Out-String).Trim()
            }
            $script:preflights += $preflight
            [System.IO.File]::WriteAllText(
                (Join-Path $suiteRoot "$RunId.preflight.json"),
                ($preflight | ConvertTo-Json -Depth 10),
                $utf8WithoutBom)
            return
        }
        Start-Sleep -Seconds 5
    }
    throw "Controlled-load preflight did not pass for $RunId"
}

for ($run = $StartRun; $run -le $EndRun; ++$run) {
    $runId = "production-final-head-ci20-r$run"
    Wait-ForControlledLoad $runId
    & (Join-Path $PSScriptRoot 'run_common_target_matrix.ps1') `
        -CandidateId $runId `
        -Algorithm production_dcfr `
        -Gamma 3.0 `
        -CertificationInterval 20 `
        -OutputRoot $OutputRoot
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Output "production_final_head_run_complete run=$run"
}

$preflightPath = Join-Path $suiteRoot "preflight-r$StartRun-r$EndRun.json"
[System.IO.File]::WriteAllText(
    $preflightPath,
    ([ordered]@{
        schema = 'gtosd.production_final_head_preflight.v1'
        start_run = $StartRun
        end_run = $EndRun
        maximum_idle_cpu_percent = $MaximumIdleCpuPercent
        minimum_free_memory_bytes = $MinimumFreeMemoryBytes
        samples = $preflights
    } | ConvertTo-Json -Depth 20),
    $utf8WithoutBom)
Write-Output "production_final_head_certification_complete preflight=$preflightPath"
