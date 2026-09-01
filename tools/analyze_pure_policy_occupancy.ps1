[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateCount(1, 16)]
    [string[]] $CorpusPaths
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$reports = foreach ($corpusPath in $CorpusPaths) {
    $resolvedPath = (Resolve-Path -LiteralPath $corpusPath).Path
    $corpus = Get-Content -LiteralPath $resolvedPath -Raw | ConvertFrom-Json
    if ($corpus.schema -ne 'gtosd.real_node_replay.v1') {
        throw "Unsupported corpus schema in $resolvedPath"
    }

    [long] $totalBranches = 0
    [long] $occupiedBranches = 0
    [long] $fullyOccupiedNodes = 0
    [long] $singleActionNodes = 0
    [long] $nonInitialTotalBranches = 0
    [long] $nonInitialOccupiedBranches = 0
    $groups = @{}

    foreach ($sample in $corpus.samples) {
        [int] $actionCount = $sample.action_count
        [int] $handCount = $sample.hand_count
        if ($sample.current_policy.Count -ne $actionCount * $handCount) {
            throw "Malformed current_policy in $resolvedPath"
        }

        $seen = [bool[]]::new($actionCount)
        for ($hand = 0; $hand -lt $handCount; ++$hand) {
            [int] $bestAction = 0
            [double] $bestValue = $sample.current_policy[$hand]
            for ($action = 1; $action -lt $actionCount; ++$action) {
                [double] $value = $sample.current_policy[$action * $handCount + $hand]
                if ($value -gt $bestValue) {
                    $bestAction = $action
                    $bestValue = $value
                }
            }
            $seen[$bestAction] = $true
        }

        [int] $occupied = 0
        foreach ($value in $seen) {
            if ($value) {
                ++$occupied
            }
        }

        $totalBranches += $actionCount
        $occupiedBranches += $occupied
        if ([long] $sample.iteration -gt 1) {
            $nonInitialTotalBranches += $actionCount
            $nonInitialOccupiedBranches += $occupied
        }
        if ($occupied -eq $actionCount) {
            ++$fullyOccupiedNodes
        }
        if ($occupied -eq 1) {
            ++$singleActionNodes
        }

        $groupKey = "iteration=$($sample.iteration);actions=$actionCount"
        if (-not $groups.ContainsKey($groupKey)) {
            $groups[$groupKey] = [ordered]@{
                samples = 0L
                total_branches = 0L
                occupied_branches = 0L
                fully_occupied_nodes = 0L
            }
        }
        $group = $groups[$groupKey]
        ++$group.samples
        $group.total_branches += $actionCount
        $group.occupied_branches += $occupied
        if ($occupied -eq $actionCount) {
            ++$group.fully_occupied_nodes
        }
    }

    $groupReports = foreach ($entry in ($groups.GetEnumerator() | Sort-Object Name)) {
        $group = $entry.Value
        [ordered]@{
            group = $entry.Name
            samples = $group.samples
            occupancy_ratio = $group.occupied_branches / [double] $group.total_branches
            fully_occupied_fraction = $group.fully_occupied_nodes / [double] $group.samples
        }
    }

    [ordered]@{
        schema = 'gtosd.pure_policy_occupancy.v1'
        corpus_path = $resolvedPath
        game_fingerprint = $corpus.game_fingerprint
        samples = $corpus.samples.Count
        occupied_branches = $occupiedBranches
        total_branches = $totalBranches
        occupancy_ratio = $occupiedBranches / [double] $totalBranches
        non_initial_occupied_branches = $nonInitialOccupiedBranches
        non_initial_total_branches = $nonInitialTotalBranches
        non_initial_occupancy_ratio =
            $nonInitialOccupiedBranches / [double] $nonInitialTotalBranches
        fully_occupied_nodes = $fullyOccupiedNodes
        single_action_nodes = $singleActionNodes
        groups = @($groupReports)
    }
}

@($reports) | ConvertTo-Json -Depth 5
