[CmdletBinding()]
param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 with Desktop development with C++ and CMake tools.' }
    $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakePath = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $cmakePath)) { throw 'Install the Visual Studio C++ CMake tools component, or put CMake on PATH.' }
}
& $cmakePath --preset windows-x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmakePath --build --preset release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if (-not $SkipTests) {
    $ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
    & $ctestPath --preset release
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
& $cmakePath --install build --config Release --prefix dist
if ($LASTEXITCODE -ne 0) { throw 'Portable package creation failed.' }
Write-Host 'Portable app: dist\ScreenshotTool.exe'
