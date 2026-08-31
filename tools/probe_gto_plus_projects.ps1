param(
    [string]$OutputRoot = '.tmp/gto-plus-black-box/02-project-discovery',
    [string[]]$AdditionalSearchRoot = @(),
    [string]$ExecutablePath
)

$ErrorActionPreference = 'Stop'
$repository = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
. (Join-Path $PSScriptRoot 'gto_plus_black_box_common.ps1')

$output = Resolve-GtoFullPath -Path $OutputRoot -BasePath $repository
New-GtoImmutableDirectory -Path $output

$roots = New-Object System.Collections.Generic.List[string]
$documents = Join-Path $env:USERPROFILE 'Documents'
if (Test-Path -LiteralPath $documents -PathType Container) {
    $roots.Add([System.IO.Path]::GetFullPath($documents))
}
if ($ExecutablePath) {
    $resolvedExecutable = Resolve-GtoFullPath -Path $ExecutablePath -BasePath $repository
    if (Test-Path -LiteralPath $resolvedExecutable -PathType Leaf) {
        $roots.Add((Split-Path -Parent $resolvedExecutable))
    }
}
foreach ($root in $AdditionalSearchRoot) {
    $resolvedRoot = Resolve-GtoFullPath -Path $root -BasePath $repository
    if (Test-Path -LiteralPath $resolvedRoot -PathType Container) {
        $roots.Add($resolvedRoot)
    }
}

$recentRoot = Join-Path $env:APPDATA 'Microsoft\Windows\Recent'
$recentLinks = New-Object System.Collections.Generic.List[object]
$recentTargets = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
$shell = New-Object -ComObject WScript.Shell
if (Test-Path -LiteralPath $recentRoot -PathType Container) {
    foreach ($link in Get-ChildItem -LiteralPath $recentRoot -Filter '*.lnk' -File -ErrorAction SilentlyContinue) {
        $shortcut = $shell.CreateShortcut($link.FullName)
        if (-not $shortcut.TargetPath -or [System.IO.Path]::GetExtension($shortcut.TargetPath) -ne '.gto') {
            continue
        }
        $recentLinks.Add([ordered]@{
            link = $link.FullName
            target = $shortcut.TargetPath
            last_write_utc = $link.LastWriteTimeUtc.ToString('o')
        })
        if (Test-Path -LiteralPath $shortcut.TargetPath -PathType Leaf) {
            [void]$recentTargets.Add([System.IO.Path]::GetFullPath($shortcut.TargetPath))
        }
    }
}

$candidatePaths = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
foreach ($target in $recentTargets) {
    [void]$candidatePaths.Add($target)
}
foreach ($root in ($roots | Sort-Object -Unique)) {
    foreach ($file in Get-ChildItem -LiteralPath $root -Filter '*.gto' -File -Recurse -ErrorAction SilentlyContinue) {
        [void]$candidatePaths.Add($file.FullName)
    }
}

$projects = @(
    $index = 0
    foreach ($path in $candidatePaths) {
        $index += 1
        $item = Get-Item -LiteralPath $path
        [ordered]@{
            candidate_id = 'project-{0:D3}' -f $index
            path = $item.FullName
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $item.FullName).Hash
            extension = $item.Extension
            size_bytes = [uint64]$item.Length
            creation_time_utc = $item.CreationTimeUtc.ToString('o')
            last_write_time_utc = $item.LastWriteTimeUtc.ToString('o')
            source = if ($recentTargets.Contains($item.FullName)) { 'recent_items' } else { 'bounded_extension_search' }
            likely_role = 'unknown_requires_read_only_open'
            safe_copy_available = $item.Length -ge 0
            auto_save_risk = 'unresolved'
            tst_match_evidence = @()
        }
    }
)

$document = [ordered]@{
    schema = 'gtosd.gto_plus_project_discovery.v1'
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    associated_extension = '.gto'
    bounded_search_roots = @($roots | ForEach-Object { $_ } | Sort-Object -Unique)
    recent_links = @($recentLinks | ForEach-Object { $_ })
    projects = $projects
    no_project_found = $projects.Count -eq 0
}
Write-GtoJsonAtomic -Path (Join-Path $output 'projects.json') -Value $document
Write-Output (Join-Path $output 'projects.json')
