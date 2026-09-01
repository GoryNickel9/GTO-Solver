[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateCount(1, 16)]
    [string[]] $CorpusPaths
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Convert-ToSignedCode([uint16] $Code) {
    if ($Code -ge 32768) {
        return [int]$Code - 65536
    }
    return [int]$Code
}

$reports = foreach ($corpusPath in $CorpusPaths) {
    $resolvedPath = (Resolve-Path -LiteralPath $corpusPath).Path
    $corpus = Get-Content -LiteralPath $resolvedPath -Raw | ConvertFrom-Json
    if ($corpus.schema -ne 'gtosd.real_node_replay.v1') {
        throw "Unsupported corpus schema in $resolvedPath"
    }

    $iterationGroups = @{}
    foreach ($sample in $corpus.samples) {
        [int] $actionCount = $sample.action_count
        [int] $handCount = $sample.hand_count
        if ($sample.old_regret_codes.Count -ne $actionCount * $handCount -or
            $sample.action_values.Count -ne $actionCount * $handCount -or
            $sample.opponent_reach.Count -ne $handCount) {
            throw "Malformed replay vectors in $resolvedPath"
        }

        [long] $finitePursuits = 0
        [long] $unitPursuits = 0
        [double] $minimumPhase = [double]::PositiveInfinity
        for ($hand = 0; $hand -lt $handCount; ++$hand) {
            [int] $selected = 0
            [double] $selectedQ =
                (Convert-ToSignedCode ([uint16]$sample.old_regret_codes[$hand])) *
                [double]$sample.old_regret_scale
            for ($action = 1; $action -lt $actionCount; ++$action) {
                [int] $index = $action * $handCount + $hand
                [double] $q =
                    (Convert-ToSignedCode ([uint16]$sample.old_regret_codes[$index])) *
                    [double]$sample.old_regret_scale
                if ($q -gt $selectedQ) {
                    $selected = $action
                    $selectedQ = $q
                }
            }

            [double] $selectedSlope =
                [double]$sample.opponent_reach[$hand] *
                [double]$sample.action_values[$selected * $handCount + $hand]
            for ($action = 0; $action -lt $actionCount; ++$action) {
                if ($action -eq $selected) {
                    continue
                }
                [int] $index = $action * $handCount + $hand
                [double] $q =
                    (Convert-ToSignedCode ([uint16]$sample.old_regret_codes[$index])) *
                    [double]$sample.old_regret_scale
                [double] $slope =
                    [double]$sample.opponent_reach[$hand] *
                    [double]$sample.action_values[$index]
                [double] $pursuitSpeed = $slope - $selectedSlope
                if (-not ($pursuitSpeed -gt 0.0)) {
                    continue
                }
                [double] $gap = $selectedQ - $q
                [double] $phase = if ($gap -le 0.0) {
                    1.0
                } elseif ($action -lt $selected) {
                    [Math]::Ceiling($gap / $pursuitSpeed)
                } else {
                    [Math]::Floor($gap / $pursuitSpeed) + 1.0
                }
                $phase = [Math]::Max(1.0, $phase)
                ++$finitePursuits
                if ($phase -eq 1.0) {
                    ++$unitPursuits
                }
                $minimumPhase = [Math]::Min($minimumPhase, $phase)
            }
        }

        $iterationKey = [string]$sample.iteration
        if (-not $iterationGroups.ContainsKey($iterationKey)) {
            $iterationGroups[$iterationKey] = [ordered]@{
                samples = 0L
                finite_pursuits = 0L
                unit_pursuits = 0L
                minimum_phase = [double]::PositiveInfinity
            }
        }
        $group = $iterationGroups[$iterationKey]
        ++$group.samples
        $group.finite_pursuits += $finitePursuits
        $group.unit_pursuits += $unitPursuits
        $group.minimum_phase = [Math]::Min($group.minimum_phase, $minimumPhase)
    }

    $groups = foreach ($entry in $iterationGroups.GetEnumerator() |
            Sort-Object { [long]$_.Key }) {
        $group = $entry.Value
        [ordered]@{
            iteration = [long]$entry.Key
            samples = $group.samples
            finite_pursuits = $group.finite_pursuits
            unit_pursuits = $group.unit_pursuits
            unit_pursuit_ratio = if ($group.finite_pursuits -eq 0) {
                0.0
            } else {
                $group.unit_pursuits / [double]$group.finite_pursuits
            }
            minimum_phase = if ([double]::IsPositiveInfinity($group.minimum_phase)) {
                $null
            } else {
                $group.minimum_phase
            }
        }
    }

    [ordered]@{
        schema = 'gtosd.sync_pursuit_proxy.v1'
        source = $resolvedPath
        limitation = 'DCFR signed regret is reinterpreted as Q; this is a screening proxy, not a PCFR trajectory.'
        iterations = @($groups)
    }
}

$reports | ConvertTo-Json -Depth 8
