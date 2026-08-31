Set-StrictMode -Version Latest

function Resolve-GtoFullPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$BasePath
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function New-GtoImmutableDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (Test-Path -LiteralPath $Path) {
        throw "Output directory already exists: $Path"
    }
    [void](New-Item -ItemType Directory -Path $Path)
}

function Write-GtoJsonAtomic {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [object]$Value,
        [int]$Depth = 32
    )

    if (Test-Path -LiteralPath $Path) {
        throw "Refusing to overwrite JSON artifact: $Path"
    }
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        throw "Missing artifact directory: $parent"
    }
    $temporary = Join-Path $parent (".{0}.{1}.tmp" -f ([System.IO.Path]::GetFileName($Path)), [guid]::NewGuid())
    $encoding = New-Object System.Text.UTF8Encoding($false)
    try {
        [System.IO.File]::WriteAllText($temporary, ($Value | ConvertTo-Json -Depth $Depth), $encoding)
        Move-Item -LiteralPath $temporary -Destination $Path
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Get-GtoPeArchitecture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $stream = [System.IO.File]::OpenRead($Path)
    $reader = $null
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        $stream.Position = 0x3c
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset + 4
        $machine = $reader.ReadUInt16()
        $architecture = switch ($machine) {
            0x014c { 'x86' }
            0x8664 { 'x64' }
            0xAA64 { 'arm64' }
            default { 'unknown-0x{0:X4}' -f $machine }
        }
        return $architecture
    }
    finally {
        if ($reader) {
            $reader.Dispose()
        }
        else {
            $stream.Dispose()
        }
    }
}

function Get-GtoFileIdentity {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $resolved = [System.IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "File not found: $resolved"
    }
    $item = Get-Item -LiteralPath $resolved
    $version = $item.VersionInfo
    $signature = Get-AuthenticodeSignature -LiteralPath $resolved
    return [ordered]@{
        path = $resolved
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolved).Hash
        size_bytes = [uint64]$item.Length
        created_utc = $item.CreationTimeUtc.ToString('o')
        last_write_utc = $item.LastWriteTimeUtc.ToString('o')
        file_version = $version.FileVersion
        product_version = $version.ProductVersion
        company_name = $version.CompanyName
        product_name = $version.ProductName
        original_filename = $version.OriginalFilename
        architecture = Get-GtoPeArchitecture -Path $resolved
        signature = [ordered]@{
            status = [string]$signature.Status
            signer_subject = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { $null }
            signer_thumbprint = if ($signature.SignerCertificate) { $signature.SignerCertificate.Thumbprint } else { $null }
        }
    }
}

function Get-GtoDataFileIdentity {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $resolved = [System.IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "File not found: $resolved"
    }
    $item = Get-Item -LiteralPath $resolved
    return [ordered]@{
        path = $resolved
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolved).Hash
        size_bytes = [uint64]$item.Length
        created_utc = $item.CreationTimeUtc.ToString('o')
        last_write_utc = $item.LastWriteTimeUtc.ToString('o')
        extension = $item.Extension
    }
}

function Get-GtoNearestRankP95 {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [double[]]$Values
    )

    if ($Values.Count -eq 0) {
        return $null
    }
    $sorted = @($Values | Sort-Object)
    $index = [Math]::Max(0, [int][Math]::Ceiling(0.95 * $sorted.Count) - 1)
    return [double]$sorted[$index]
}

function Get-GtoMedian {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [double[]]$Values
    )

    if ($Values.Count -eq 0) {
        return $null
    }
    $sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) {
        return [double]$sorted[$middle]
    }
    return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
}

function Get-GtoOptionalProperty {
    param(
        [AllowNull()]
        [object]$InputObject,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [AllowNull()]
        [object]$DefaultValue = $null
    )

    if ($null -eq $InputObject) {
        return $DefaultValue
    }
    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $DefaultValue
    }
    return $property.Value
}

function ConvertFrom-GtoDevText {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Text
    )

    $match = [regex]::Match($Text, '(?<![0-9])([0-9]+(?:[\.,][0-9]+)?)\s*%')
    if (-not $match.Success) {
        return $null
    }
    $normalized = $match.Groups[1].Value.Replace(',', '.')
    $value = 0.0
    if (-not [double]::TryParse(
            $normalized,
            [System.Globalization.NumberStyles]::Float,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [ref]$value)) {
        return $null
    }
    return $value
}
