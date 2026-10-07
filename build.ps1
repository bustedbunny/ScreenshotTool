[CmdletBinding()]
param([switch]$SkipTests, [string]$BuildDirectory = 'build', [string]$ConfigurePreset = 'windows-x64')
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
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) { [IO.Path]::GetFullPath($BuildDirectory) } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $BuildDirectory)) }
& $cmakePath --preset $ConfigurePreset -B $buildPath
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmakePath --build $buildPath --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if (-not $SkipTests) {
    $ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
    & $ctestPath --test-dir $buildPath -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
& $cmakePath --install $buildPath --config Release --prefix dist
if ($LASTEXITCODE -ne 0) { throw 'Portable package creation failed.' }
Write-Host 'Portable app: dist\ScreenshotTool.exe'
