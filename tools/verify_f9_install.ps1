param(
  [string]$BuildDir = "C:\tmp\gtosd-gui-build",
  [string]$InstallDir = "C:\tmp\gtosd-gui-install"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath([string]$PathValue) {
  if ([System.IO.Path]::IsPathRooted($PathValue)) {
    return [System.IO.Path]::GetFullPath($PathValue)
  }
  return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $PathValue))
}

$resolvedBuild = Resolve-AbsolutePath $BuildDir
$resolvedInstall = Resolve-AbsolutePath $InstallDir

& cmake --install $resolvedBuild --config Release --prefix $resolvedInstall
if ($LASTEXITCODE -ne 0) {
  throw "F9 install failed with exit code $LASTEXITCODE"
}

$requiredArtifacts = @(
  "bin\gto_gui_qt_prototype.exe",
  "bin\gto_gui_imgui_prototype.exe",
  "bin\Qt6Core.dll",
  "bin\Qt6Gui.dll",
  "bin\Qt6Network.dll",
  "bin\Qt6Widgets.dll",
  "bin\double-conversion.dll",
  "bin\libsodium.dll",
  "bin\md4c.dll",
  "bin\pcre2-16.dll",
  "bin\sqlite3.dll",
  "bin\z.dll",
  "bin\zstd.dll",
  "Qt6\plugins\platforms\qminimal.dll",
  "Qt6\plugins\platforms\qwindows.dll",
  "share\gtosd\licenses\imgui\LICENSE",
  "share\gtosd\licenses\qtbase\LICENSE",
  "share\gtosd\licenses\double-conversion\LICENSE",
  "share\gtosd\licenses\md4c\LICENSE",
  "share\gtosd\licenses\pcre2\LICENSE",
  "share\gtosd\licenses\zlib\LICENSE"
)

$missing = @()
foreach ($relativePath in $requiredArtifacts) {
  $candidate = Join-Path $resolvedInstall $relativePath
  if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
    $missing += $relativePath
  }
}

if ($missing.Count -ne 0) {
  throw "F9 install is incomplete. Missing: $($missing -join ', ')"
}

[pscustomobject]@{
  schema = "gtosd.phase9.install.v1"
  install_root = $resolvedInstall
  checked_artifacts = $requiredArtifacts.Count
  qt_dynamic_linking = $true
  license_notices_present = $true
} | ConvertTo-Json
