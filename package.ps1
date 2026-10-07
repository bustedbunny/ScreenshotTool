[CmdletBinding()]
param([string]$InnoCompilerPath, [string]$BuildDirectory = 'build/release-packaging', [string]$ConfigurePreset = 'windows-x64')
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) { [IO.Path]::GetFullPath($BuildDirectory) } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $BuildDirectory)) }
& (Join-Path $PSScriptRoot 'build.ps1') -BuildDirectory $buildPath -ConfigurePreset $ConfigurePreset
$cache = Get-Content -LiteralPath (Join-Path $buildPath 'CMakeCache.txt')
$cmakeEntry = @($cache | Where-Object { $_ -match '^CMAKE_COMMAND:INTERNAL=' })
if ($cmakeEntry.Count -ne 1) { throw 'Configured CMake executable was not found.' }
$cmake = $cmakeEntry[0].Substring('CMAKE_COMMAND:INTERNAL='.Length)
$cpack = Join-Path (Split-Path -Parent $cmake) 'cpack.exe'
$versionHeader = Get-Content -LiteralPath (Join-Path $buildPath 'generated/version.hpp') -Raw
if ($versionHeader -notmatch 'AppVersion = L"([0-9]+\.[0-9]+\.[0-9]+)"') { throw 'Configured app version was not found.' }
$version = $Matches[1]
$dist = Join-Path $PSScriptRoot 'dist'
$assets = Join-Path $PSScriptRoot 'release-assets'
$script = Join-Path $buildPath 'generated/ScreenshotTool.iss'
New-Item -ItemType Directory -Path $assets -Force | Out-Null
if (-not $InnoCompilerPath) { $InnoCompilerPath = & (Join-Path $PSScriptRoot 'scripts/get-inno-setup.ps1') }
$compiler = (Get-Item -LiteralPath $InnoCompilerPath).FullName
$signature = Get-AuthenticodeSignature -LiteralPath $compiler
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '(^|,\s*)CN=Pyrsys B\.V\.(,|$)') {
    throw 'Packaging requires an authentic Inno Setup compiler.'
}
& $cpack --config (Join-Path $buildPath 'CPackConfig.cmake') -C Release -B $assets
if ($LASTEXITCODE -ne 0) { throw 'Portable ZIP packaging failed.' }
& $compiler "/DSourceDir=$dist" "/O$assets" $script
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
$exe = Join-Path $assets 'ScreenshotTool.exe'
Copy-Item -LiteralPath (Join-Path $dist 'ScreenshotTool.exe') -Destination $exe -Force
$setup = Join-Path $assets "ScreenshotTool-$version-windows-x64-setup.exe"
$zip = Join-Path $assets "ScreenshotTool-$version-windows-x64.zip"
foreach ($path in @($exe, $setup, $zip)) {
    if ((Get-Item -LiteralPath $path).Length -le 0) { throw "Release asset is empty: $path" }
}
foreach ($path in @($exe, $setup)) {
    $info = (Get-Item -LiteralPath $path).VersionInfo
    if ("$($info.FileMajorPart).$($info.FileMinorPart).$($info.FileBuildPart)" -cne $version -or $info.FilePrivatePart -ne 0) {
        throw "Release asset version mismatch: $path"
    }
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try {
    foreach ($relative in @('ScreenshotTool.exe', 'README.md', 'docs/VALIDATION.md', 'docs/ARCHITECTURE.md')) {
        $entries = @($archive.Entries | Where-Object { $_.FullName -ceq $relative -or $_.FullName.EndsWith("/$relative", [StringComparison]::Ordinal) })
        if ($entries.Count -ne 1) { throw "Portable ZIP must contain exactly one $relative." }
        $stream = $entries[0].Open()
        $hash = [Security.Cryptography.SHA256]::Create()
        try { $digest = [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
        finally { $hash.Dispose(); $stream.Dispose() }
        $expected = (Get-FileHash -LiteralPath (Join-Path $dist $relative) -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($digest -cne $expected) { throw "Portable ZIP payload mismatch: $relative" }
    }
} finally { $archive.Dispose() }
Write-Host "Verified release assets in $assets"
