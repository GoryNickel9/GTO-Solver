param(
    [string]$OutputRoot = '.tmp/gto-plus-black-box/01-install-discovery',
    [string[]]$AdditionalSearchRoot = @()
)

$ErrorActionPreference = 'Stop'
$repository = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
. (Join-Path $PSScriptRoot 'gto_plus_black_box_common.ps1')

$output = Resolve-GtoFullPath -Path $OutputRoot -BasePath $repository
New-GtoImmutableDirectory -Path $output

$registryEvidence = New-Object System.Collections.Generic.List[object]
$candidatePaths = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
$registryRoots = @(
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths',
    'Registry::HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths',
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
    'Registry::HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
)

foreach ($root in $registryRoots) {
    if (-not (Test-Path -LiteralPath $root)) {
        continue
    }
    foreach ($key in Get-ChildItem -LiteralPath $root -ErrorAction SilentlyContinue) {
        $properties = Get-ItemProperty -LiteralPath $key.PSPath -ErrorAction SilentlyContinue
        if (-not $properties) {
            continue
        }
        $displayName = Get-GtoOptionalProperty -InputObject $properties -Name 'DisplayName'
        $installLocation = Get-GtoOptionalProperty -InputObject $properties -Name 'InstallLocation'
        $defaultValue = Get-GtoOptionalProperty -InputObject $properties -Name '(default)'
        $uninstallString = Get-GtoOptionalProperty -InputObject $properties -Name 'UninstallString'
        $text = @(
            $key.PSChildName,
            $displayName,
            $installLocation,
            $defaultValue,
            $uninstallString
        ) -join ' '
        if ($text -notmatch '(?i)(^|[^a-z])GTO\+?|StoxEV') {
            continue
        }
        $record = [ordered]@{
            root = $root
            key = $key.PSChildName
            display_name = $displayName
            install_location = $installLocation
            display_version = Get-GtoOptionalProperty -InputObject $properties -Name 'DisplayVersion'
            publisher = Get-GtoOptionalProperty -InputObject $properties -Name 'Publisher'
            default_value = $defaultValue
            uninstall_string = $uninstallString
        }
        $registryEvidence.Add($record)
        foreach ($value in @($defaultValue, $installLocation)) {
            if (-not $value) {
                continue
            }
            $expanded = [Environment]::ExpandEnvironmentVariables([string]$value).Trim('"')
            if (Test-Path -LiteralPath $expanded -PathType Leaf) {
                [void]$candidatePaths.Add([System.IO.Path]::GetFullPath($expanded))
            }
            elseif (Test-Path -LiteralPath $expanded -PathType Container) {
                foreach ($file in Get-ChildItem -LiteralPath $expanded -Filter '*.exe' -File -ErrorAction SilentlyContinue) {
                    if ($file.Name -match '(?i)GTO') {
                        [void]$candidatePaths.Add($file.FullName)
                    }
                }
            }
        }
    }
}

$association = [ordered]@{
    extension = '.gto'
    prog_id = $null
    open_command = $null
    print_command = $null
}
$extensionKey = 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\.gto'
if (Test-Path -LiteralPath $extensionKey) {
    $association.prog_id = (Get-Item -LiteralPath $extensionKey).GetValue('')
}
if ($association.prog_id) {
    $progIdRoot = "Registry::HKEY_LOCAL_MACHINE\Software\Classes\$($association.prog_id)"
    $openKey = Join-Path $progIdRoot 'shell\open\command'
    $printKey = Join-Path $progIdRoot 'shell\print\command'
    if (Test-Path -LiteralPath $openKey) {
        $association.open_command = (Get-Item -LiteralPath $openKey).GetValue('')
    }
    if (Test-Path -LiteralPath $printKey) {
        $association.print_command = (Get-Item -LiteralPath $printKey).GetValue('')
    }
    if ($association.open_command) {
        $executableMatch = [regex]::Match($association.open_command, '^\s*"?([^"\r\n]+?\.exe)"?(?:\s|$)')
        if ($executableMatch.Success -and (Test-Path -LiteralPath $executableMatch.Groups[1].Value -PathType Leaf)) {
            [void]$candidatePaths.Add([System.IO.Path]::GetFullPath($executableMatch.Groups[1].Value))
        }
    }
}

$shortcutRoots = @(
    (Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs'),
    (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'),
    (Join-Path $env:USERPROFILE 'Desktop'),
    (Join-Path $env:PUBLIC 'Desktop')
)
$shortcutEvidence = New-Object System.Collections.Generic.List[object]
$shell = New-Object -ComObject WScript.Shell
foreach ($root in $shortcutRoots) {
    if (-not (Test-Path -LiteralPath $root)) {
        continue
    }
    foreach ($link in Get-ChildItem -LiteralPath $root -Filter '*.lnk' -File -Recurse -ErrorAction SilentlyContinue) {
        if ($link.Name -notmatch '(?i)GTO|StoxEV') {
            continue
        }
        $shortcut = $shell.CreateShortcut($link.FullName)
        $shortcutEvidence.Add([ordered]@{
            link = $link.FullName
            target = $shortcut.TargetPath
            arguments = $shortcut.Arguments
            working_directory = $shortcut.WorkingDirectory
            icon = $shortcut.IconLocation
            last_write_utc = $link.LastWriteTimeUtc.ToString('o')
        })
        if ($shortcut.TargetPath -and (Test-Path -LiteralPath $shortcut.TargetPath -PathType Leaf)) {
            [void]$candidatePaths.Add([System.IO.Path]::GetFullPath($shortcut.TargetPath))
        }
    }
}

foreach ($root in $AdditionalSearchRoot) {
    $resolvedRoot = Resolve-GtoFullPath -Path $root -BasePath $repository
    if (-not (Test-Path -LiteralPath $resolvedRoot -PathType Container)) {
        continue
    }
    foreach ($file in Get-ChildItem -LiteralPath $resolvedRoot -Filter '*.exe' -File -Recurse -ErrorAction SilentlyContinue) {
        if ($file.Name -match '(?i)^GTO(?:Plus)?\.exe$') {
            [void]$candidatePaths.Add($file.FullName)
        }
    }
}

$activeProcesses = @(
    Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -match '(?i)^GTO(?:Plus)?$' } |
        ForEach-Object {
            $path = $null
            $startTimeUtc = $null
            try { $path = $_.Path } catch { $path = $null }
            try { $startTimeUtc = $_.StartTime.ToUniversalTime().ToString('o') } catch { $startTimeUtc = $null }
            [ordered]@{
                pid = $_.Id
                name = $_.ProcessName
                path = $path
                main_window_title = $_.MainWindowTitle
                responding = $_.Responding
                start_time_utc = $startTimeUtc
            }
        }
)
foreach ($process in $activeProcesses) {
    if ($process.path -and (Test-Path -LiteralPath $process.path -PathType Leaf)) {
        [void]$candidatePaths.Add([System.IO.Path]::GetFullPath($process.path))
    }
}

$candidates = @(
    foreach ($path in $candidatePaths) {
        $identity = Get-GtoFileIdentity -Path $path
        $confidence = 0
        $evidence = New-Object System.Collections.Generic.List[string]
        if ($association.open_command -and $association.open_command -like "*$path*") {
            $confidence += 45
            $evidence.Add('registered .gto shell open executable')
        }
        if ($identity.original_filename -match '(?i)^GTO\.exe$') {
            $confidence += 15
            $evidence.Add('OriginalFilename=GTO.exe')
        }
        if ($identity.signature.status -eq 'Valid' -and $identity.signature.signer_subject -match '(?i)StoxEV') {
            $confidence += 35
            $evidence.Add('valid StoxEV Authenticode signature')
        }
        if ($identity.file_version) {
            $confidence += 5
            $evidence.Add('version resource present')
        }
        [ordered]@{
            path = $identity.path
            confidence = [Math]::Min(100, $confidence)
            evidence = @($evidence | ForEach-Object { $_ })
            sha256 = $identity.sha256
            version = $identity.file_version
            product_version = $identity.product_version
            architecture = $identity.architecture
            signature = $identity.signature
            size_bytes = $identity.size_bytes
            last_write_utc = $identity.last_write_utc
            launch_method = if ($association.open_command -like "*$path*") { 'shell_open_or_direct' } else { 'direct' }
            associated_extensions = if ($association.open_command -like "*$path*") { @('.gto') } else { @() }
        }
    }
)

$document = [ordered]@{
    schema = 'gtosd.gto_plus_install_discovery.v1'
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    bounded_search_roots = @($AdditionalSearchRoot)
    active_processes = $activeProcesses
    registry = @($registryEvidence | ForEach-Object { $_ })
    file_association = $association
    shortcuts = @($shortcutEvidence | ForEach-Object { $_ })
    candidates = @($candidates | Sort-Object confidence -Descending)
}
Write-GtoJsonAtomic -Path (Join-Path $output 'candidates.json') -Value $document
Write-Output (Join-Path $output 'candidates.json')
