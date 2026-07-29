param(
  [string]$BuildDir = "C:\tmp\gtosd-gui-build",
  [string]$OutputDir = "out/phase9"
)

$ErrorActionPreference = "Stop"
$repository = Split-Path -Parent $PSScriptRoot
$resolvedBuild = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
  [System.IO.Path]::GetFullPath($BuildDir)
} else {
  Join-Path $repository $BuildDir
}
$resolvedOutput = Join-Path $repository $OutputDir
New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null

$vcpkgBin = Join-Path $resolvedBuild "vcpkg_installed/x64-windows/bin"
$qtPlugins = Join-Path $resolvedBuild "vcpkg_installed/x64-windows/Qt6/plugins"
$env:PATH = "$vcpkgBin;$env:PATH"
$env:QT_PLUGIN_PATH = $qtPlugins

$qt = Join-Path $resolvedBuild "apps/gui_qt_prototype/gto_gui_qt_prototype.exe"
$imgui = Join-Path $resolvedBuild "apps/gui_imgui_prototype/gto_gui_imgui_prototype.exe"
$qtReport = Join-Path $resolvedOutput "qt6_widgets_benchmark.json"
$imguiHardwareReport = Join-Path $resolvedOutput "imgui_dx11_benchmark.json"
$imguiWarpReport = Join-Path $resolvedOutput "imgui_warp_benchmark.json"

function Invoke-PrototypeBenchmark([string]$Executable, [string]$ReportPath) {
  $process = Start-Process -FilePath $Executable `
    -ArgumentList @("--benchmark", "`"$ReportPath`"") `
    -WindowStyle Hidden `
    -Wait `
    -PassThru
  if ($process.ExitCode -ne 0) {
    throw "$Executable benchmark failed with exit code $($process.ExitCode)"
  }
  if (-not (Test-Path -LiteralPath $ReportPath -PathType Leaf)) {
    throw "$Executable did not produce $ReportPath"
  }
}

Remove-Item Env:GTOSD_FORCE_WARP -ErrorAction SilentlyContinue
$env:QT_QPA_PLATFORM = "minimal"
Invoke-PrototypeBenchmark $qt $qtReport
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
Invoke-PrototypeBenchmark $imgui $imguiHardwareReport
$env:GTOSD_FORCE_WARP = "1"
Invoke-PrototypeBenchmark $imgui $imguiWarpReport
Remove-Item Env:GTOSD_FORCE_WARP -ErrorAction SilentlyContinue

$qtMetrics = Get-Content $qtReport -Raw | ConvertFrom-Json
$imguiHardwareMetrics = Get-Content $imguiHardwareReport -Raw | ConvertFrom-Json
$imguiWarpMetrics = Get-Content $imguiWarpReport -Raw | ConvertFrom-Json

function Test-FrameGate($metrics) {
  return $metrics.fps -ge 60.0 -and $metrics.p95_frame_ms -le (1000.0 / 60.0)
}

$summary = [ordered]@{
  schema = "gtosd.phase9.benchmark.v1"
  logical_nodes = 100000
  matrix_cells = 81
  processor_affinity_limit = 4
  qt6_widgets = $qtMetrics
  dear_imgui_dx11 = $imguiHardwareMetrics
  dear_imgui_warp = $imguiWarpMetrics
  gates = [ordered]@{
    qt_raster_60_fps = Test-FrameGate $qtMetrics
    imgui_dx11_60_fps = Test-FrameGate $imguiHardwareMetrics
    imgui_warp_60_fps = Test-FrameGate $imguiWarpMetrics
  }
}
$summary.all_frame_gates_passed =
  $summary.gates.qt_raster_60_fps -and
  $summary.gates.imgui_dx11_60_fps -and
  $summary.gates.imgui_warp_60_fps

$summaryPath = Join-Path $resolvedOutput "phase9_benchmark_summary.json"
$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $summaryPath -Encoding utf8
if (-not $summary.all_frame_gates_passed) {
  throw "One or more F9 frame-time gates failed. See $summaryPath"
}
Write-Output $summaryPath
