param(
    [ValidateRange(1, 20)]
    [int]$Rounds = 5,
    [ValidateRange(1.0, 100.0)]
    [double]$MaximumIdleCpuPercent = 15.0,
    [ValidateRange(2000000000, 128000000000)]
    [uint64]$MinimumFreeMemoryBytes = 4000000000,
    [ValidateRange(1, 1000)]
    [int]$MaximumPreflightAttempts = 120,
    [string]$OutputRoot = 'out/dcfr-interleaved-certification-20260901'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$suiteRoot = Join-Path $repoRoot $OutputRoot
if (Test-Path -LiteralPath $suiteRoot) {
    throw "Interleaved output already exists: $suiteRoot"
}
[void](New-Item -ItemType Directory -Path $suiteRoot)
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
$preflights = @()

function Wait-ForControlledLoad([string]$ArmId) {
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
                arm_id = $ArmId
                utc = [DateTime]::UtcNow.ToString('o')
                idle_cpu_samples_percent = $cpuSamples
                idle_cpu_mean_percent = $cpuMean
                free_memory_bytes = $freeBytes
                power_scheme = (powercfg /getactivescheme | Out-String).Trim()
            }
            $script:preflights += $preflight
            [System.IO.File]::WriteAllText(
                (Join-Path $suiteRoot "$ArmId.preflight.json"),
                ($preflight | ConvertTo-Json -Depth 10),
                $utf8WithoutBom)
            return
        }
        Start-Sleep -Seconds 5
    }
    throw "Controlled-load preflight did not pass for $ArmId"
}

$arms = [ordered]@{
    release = [ordered]@{ algorithm = 'dcfr'; gamma = 2.0 }
    leader = [ordered]@{
        algorithm = 'production_dcfr'
        gamma = 3.0
    }
}

for ($round = 1; $round -le $Rounds; ++$round) {
    $order = if (($round % 2) -eq 1) { @('release', 'leader') } else { @('leader', 'release') }
    foreach ($arm in $order) {
        $armId = 'interleaved-r{0:d2}-{1}' -f $round, $arm
        Wait-ForControlledLoad $armId
        & (Join-Path $PSScriptRoot 'run_common_target_matrix.ps1') `
            -CandidateId $armId `
            -Algorithm $arms[$arm].algorithm `
            -Gamma $arms[$arm].gamma `
            -CertificationInterval 30 `
            -OutputRoot $OutputRoot
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Write-Output "interleaved_arm_complete round=$round arm=$arm"
    }
}

$preflightPath = Join-Path $suiteRoot 'preflight.json'
[System.IO.File]::WriteAllText(
    $preflightPath,
    ([ordered]@{
        schema = 'gtosd.interleaved_preflight.v1'
        rounds = $Rounds
        maximum_idle_cpu_percent = $MaximumIdleCpuPercent
        minimum_free_memory_bytes = $MinimumFreeMemoryBytes
        samples = $preflights
    } | ConvertTo-Json -Depth 20),
    $utf8WithoutBom)
Write-Output "interleaved_certification_complete preflight=$preflightPath"
