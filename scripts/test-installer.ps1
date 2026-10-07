[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InstallerPath,
    [Parameter(Mandatory)][string]$SourceDir,
    [Parameter(Mandatory)][string]$InstallerScript,
    [Parameter(Mandatory)][string]$IsccPath,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -cne 'true' -or $env:RUNNER_OS -cne 'Windows' -or -not $env:RUNNER_TEMP) {
    throw 'Installer smoke tests may run only in a disposable Windows GitHub Actions runner account.'
}
if ([Environment]::OSVersion.Version.Build -lt 22000 -or
    [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64') {
    throw 'Installer smoke tests require the production Windows 11 build floor and x64 OS.'
}
foreach ($filePath in @($InstallerPath, $InstallerScript, $IsccPath, (Join-Path $SourceDir 'ScreenshotTool.exe'))) {
    if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) { throw "Required smoke-test file is missing: $filePath" }
}
$InstallerPath = (Resolve-Path -LiteralPath $InstallerPath).Path
$InstallerScript = (Resolve-Path -LiteralPath $InstallerScript).Path
$IsccPath = (Resolve-Path -LiteralPath $IsccPath).Path
$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path

$installDir = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\ScreenshotTool'
$installedExe = Join-Path $installDir 'ScreenshotTool.exe'
$uninstaller = Join-Path $installDir 'unins000.exe'
$startShortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'ScreenshotTool.lnk'
$desktopShortcut = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'ScreenshotTool.lnk'
$uninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\bustedbunny.ScreenshotTool_is1'
$runKey = 'Software\Microsoft\Windows\CurrentVersion\Run'
$registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey('CurrentUser', 'Registry64')
$machineRegistry = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', 'Registry64')
$script:checks = 0
$script:invocations = 0

function Assert-Check([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

function Read-RunValue {
    $key = $registry.OpenSubKey($runKey)
    if (-not $key) { return [pscustomobject]@{ Exists = $false; Kind = $null; Value = $null } }
    try {
        $exists = $key.GetValueNames() -contains 'ScreenshotTool'
        if (-not $exists) { return [pscustomobject]@{ Exists = $false; Kind = $null; Value = $null } }
        return [pscustomobject]@{
            Exists = $true
            Kind = $key.GetValueKind('ScreenshotTool')
            Value = $key.GetValue('ScreenshotTool', $null, 'DoNotExpandEnvironmentNames')
        }
    } finally { $key.Dispose() }
}

function Write-RunValue($Value, [Microsoft.Win32.RegistryValueKind]$Kind = 'String') {
    $key = $registry.CreateSubKey($runKey)
    try { $key.SetValue('ScreenshotTool', $Value, $Kind) } finally { $key.Dispose() }
}

function Remove-RunValue {
    $key = $registry.OpenSubKey($runKey, $true)
    if ($key) { try { $key.DeleteValue('ScreenshotTool', $false) } finally { $key.Dispose() } }
}

function Invoke-Installer([string]$Path, [string]$Label, [string[]]$ExtraArguments = @(), [switch]$AllowFailure) {
    $script:invocations++
    $logPath = Join-Path $testDir ("{0:D2}-{1}.log" -f $script:invocations, $Label)
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/LOG=`"$logPath`"") + $ExtraArguments
    $process = Start-Process -FilePath $Path -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    $exitCode = $process.ExitCode
    $process.Dispose()
    if (-not $AllowFailure -and $exitCode -ne 0) { throw "$Label failed with exit code $exitCode; see $logPath" }
    return $exitCode
}

function Assert-NoLaunch {
    Assert-Check (@(Get-Process -Name ScreenshotTool -ErrorAction SilentlyContinue).Count -eq 0) 'Silent installation unexpectedly launched ScreenshotTool.'
}

function Assert-Shortcut([string]$Path, [string]$Label) {
    Assert-Check (Test-Path -LiteralPath $Path -PathType Leaf) "$Label shortcut is missing."
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $null
    try {
        $shortcut = $shell.CreateShortcut($Path)
        Assert-Check ($shortcut.TargetPath -ieq $installedExe) "$Label shortcut does not target the installed executable."
        Assert-Check ($shortcut.WorkingDirectory -ieq $installDir) "$Label shortcut working directory is incorrect."
    } finally {
        if ($shortcut) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut) }
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
    }
}

function Assert-Installed([string]$ExpectedVersion, [string]$ExpectedPayload) {
    Assert-Check (Test-Path -LiteralPath $installedExe -PathType Leaf) 'The executable was not installed into the persistent per-user app directory.'
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($installedExe)
    $actualVersion = '{0}.{1}.{2}' -f $info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart
    Assert-Check ($actualVersion -ceq $ExpectedVersion) "Installed PE version is $actualVersion, expected $ExpectedVersion."
    foreach ($sourceFile in Get-ChildItem -LiteralPath $ExpectedPayload -Recurse -File) {
        $relative = [IO.Path]::GetRelativePath($ExpectedPayload, $sourceFile.FullName)
        $destination = Join-Path $installDir $relative
        Assert-Check (Test-Path -LiteralPath $destination -PathType Leaf) "Installed payload is missing $relative."
        Assert-Check ((Get-FileHash -LiteralPath $destination).Hash -ceq (Get-FileHash -LiteralPath $sourceFile.FullName).Hash) "Installed payload differs: $relative."
    }
    $key = $registry.OpenSubKey($uninstallKey)
    Assert-Check ($null -ne $key) 'Per-user Apps uninstall registration is missing.'
    try {
        Assert-Check ($key.GetValue('DisplayName') -ceq 'ScreenshotTool') 'Apps display name does not match.'
        Assert-Check ($key.GetValue('DisplayVersion') -ceq $ExpectedVersion) 'Apps display version does not match.'
        Assert-Check ([string]$key.GetValue('InstallLocation').TrimEnd('\') -ieq $installDir) 'Apps installation location does not match.'
        Assert-Check ([string]$key.GetValue('UninstallString') -like "*$uninstaller*") 'Apps uninstaller path does not match.'
        Assert-Check ([string]$key.GetValue('QuietUninstallString') -like '*/SILENT*') 'Apps silent uninstaller registration is missing.'
    } finally { $key.Dispose() }
    $machineKey = $machineRegistry.OpenSubKey($uninstallKey)
    try {
        Assert-Check ($null -eq $machineKey) 'A per-user install unexpectedly registered for all users.'
    } finally { if ($machineKey) { $machineKey.Dispose() } }
    Assert-Shortcut $startShortcut 'Start menu'
    Assert-NoLaunch
}

function Assert-Removed {
    Assert-Check (-not (Test-Path -LiteralPath $installedExe)) 'Uninstall left the installed executable behind.'
    Assert-Check (-not (Test-Path -LiteralPath $startShortcut)) 'Uninstall left the Start menu shortcut behind.'
    Assert-Check (-not (Test-Path -LiteralPath $desktopShortcut)) 'Uninstall left the desktop shortcut behind.'
    $key = $registry.OpenSubKey($uninstallKey)
    try { Assert-Check ($null -eq $key) 'Uninstall left its Apps registration behind.' } finally { if ($key) { $key.Dispose() } }
}

# Fail before touching account state unless this runner is clean and disposable.
if (Test-Path -LiteralPath $installDir) { throw 'The smoke-test account already has an installation directory.' }
foreach ($shortcutPath in @($startShortcut, $desktopShortcut)) {
    if (Test-Path -LiteralPath $shortcutPath) { throw "The smoke-test account already has a shortcut: $shortcutPath" }
}
if ((Read-RunValue).Exists) { throw 'The smoke-test account already has a ScreenshotTool sign-in entry.' }
foreach ($baseKey in @($registry, $machineRegistry)) {
    $key = $baseKey.OpenSubKey($uninstallKey)
    if ($key) { $key.Dispose(); throw 'The smoke-test account already has ScreenshotTool uninstall registration.' }
}
Assert-NoLaunch
$settingsPath = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'ScreenshotTool\settings.ini'
if (Test-Path -LiteralPath $settingsPath) { throw 'The smoke-test account already has ScreenshotTool settings.' }
$testDir = Join-Path $env:RUNNER_TEMP ('ScreenshotTool-installer-smoke-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir | Out-Null
$screenshotsDir = Join-Path ([Environment]::GetFolderPath('MyPictures')) 'ScreenshotTool'
$screenshotPath = Join-Path $screenshotsDir ('installer-smoke-' + [Guid]::NewGuid().ToString('N') + '.txt')
$mutex = $null
$smokePassed = $false
$logOutputDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\build\installer-validation'))

try {
    # Create an older real PE resource fixture without rebuilding or launching an app.
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class InstallerVersionFixture {
    private delegate bool LanguageCallback(IntPtr module, IntPtr type, IntPtr name, ushort language, IntPtr parameter);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool EnumResourceLanguages(IntPtr module, IntPtr type, IntPtr name, LanguageCallback callback, IntPtr parameter);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern IntPtr BeginUpdateResource(string path, bool deleteExisting);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool UpdateResource(IntPtr update, IntPtr type, IntPtr name, ushort language, byte[] data, uint length);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool EndUpdateResource(IntPtr update, bool discard);
    public static void LowerVersion(string path) {
        IntPtr module = LoadLibraryEx(path, IntPtr.Zero, 2);
        if (module == IntPtr.Zero) throw new Win32Exception();
        byte[] data;
        ushort language = 0;
        try {
            IntPtr resource = FindResource(module, new IntPtr(1), new IntPtr(16));
            if (resource == IntPtr.Zero) throw new Win32Exception();
            data = new byte[SizeofResource(module, resource)];
            Marshal.Copy(LockResource(LoadResource(module, resource)), data, 0, data.Length);
            bool foundLanguage = false;
            LanguageCallback callback = (m, t, n, l, p) => { language=l; foundLanguage=true; return false; };
            EnumResourceLanguages(module, new IntPtr(16), new IntPtr(1), callback, IntPtr.Zero);
            if (!foundLanguage) throw new InvalidOperationException("Version resource language was not found.");
        } finally { FreeLibrary(module); }
        int fixedOffset = -1;
        for (int i=0; i+52<=data.Length; i+=4) {
            if (BitConverter.ToUInt32(data, i) == 0xfeef04bd) { fixedOffset=i; break; }
        }
        if (fixedOffset < 0) throw new InvalidOperationException("Fixed version resource was not found.");
        // dwFileVersionMS/LS and dwProductVersionMS/LS become 0.0.0.0.
        Array.Clear(data, fixedOffset+8, 16);
        IntPtr update = BeginUpdateResource(path, false);
        if (update == IntPtr.Zero) throw new Win32Exception();
        bool written = false;
        try {
            written = UpdateResource(update, new IntPtr(16), new IntPtr(1), language, data, (uint)data.Length);
            if (!written) throw new Win32Exception();
        } finally {
            if (!EndUpdateResource(update, !written) && written) throw new Win32Exception();
        }
    }
}
'@
    $olderPayload = Join-Path $testDir 'older-payload'
    New-Item -ItemType Directory -Path $olderPayload | Out-Null
    Get-ChildItem -LiteralPath $SourceDir | Copy-Item -Destination $olderPayload -Recurse
    [InstallerVersionFixture]::LowerVersion((Join-Path $olderPayload 'ScreenshotTool.exe'))
    & $IsccPath '/DAppVersion=0.0.0' "/DSourceDir=$olderPayload" "/O$testDir" $InstallerScript
    if ($LASTEXITCODE -ne 0) { throw 'Older installer fixture compilation failed.' }
    $olderInstaller = Join-Path $testDir 'ScreenshotTool-0.0.0-windows-x64-setup.exe'
    Assert-Check (Test-Path -LiteralPath $olderInstaller) 'The older installer fixture is missing.'

    New-Item -ItemType Directory -Path (Split-Path -Parent $settingsPath), $screenshotsDir -Force | Out-Null
    Set-Content -LiteralPath $settingsPath -Value "updates 0`nlanguage en`nstroke 4" -Encoding utf8
    Set-Content -LiteralPath $screenshotPath -Value 'Existing screenshot sentinel' -Encoding utf8
    $settingsHash = (Get-FileHash -LiteralPath $settingsPath).Hash
    $screenshotHash = (Get-FileHash -LiteralPath $screenshotPath).Hash

    [void](Invoke-Installer $olderInstaller 'fresh')
    Assert-Installed '0.0.0' $olderPayload
    Assert-Check (-not (Read-RunValue).Exists) 'A fresh install enabled launch at sign-in.'
    Assert-Check (-not (Test-Path -LiteralPath $desktopShortcut)) 'A fresh install enabled the optional desktop shortcut.'

    $portableDir = Join-Path $testDir 'portable location'
    New-Item -ItemType Directory -Path $portableDir | Out-Null
    $portableExe = Join-Path $portableDir 'ScreenshotTool.exe'
    Copy-Item -LiteralPath (Join-Path $SourceDir 'ScreenshotTool.exe') -Destination $portableExe
    $portableCommand = '"' + $portableExe + '"'
    $installedCommand = '"' + $installedExe + '"'
    Write-RunValue $portableCommand
    [void](Invoke-Installer $InstallerPath 'upgrade' @('/TASKS=desktopicon'))
    Assert-Installed $Version $SourceDir
    Assert-Check ((Read-RunValue).Value -ceq $installedCommand) 'Upgrade did not preserve and migrate enabled sign-in to the installed path.'
    Assert-Shortcut $desktopShortcut 'Desktop'

    Set-Content -LiteralPath (Join-Path $installDir 'README.md') -Value 'Repair fixture' -Encoding utf8
    [void](Invoke-Installer $InstallerPath 'repair')
    Assert-Installed $Version $SourceDir
    Assert-Check ((Read-RunValue).Value -ceq $installedCommand) 'Repair changed enabled sign-in unexpectedly.'

    $beforeDowngrade = (Get-FileHash -LiteralPath $installedExe).Hash
    $code = Invoke-Installer $olderInstaller 'downgrade-blocked' -AllowFailure
    Assert-Check ($code -eq 7) "Downgrade should fail before installation with exit code 7, received $code."
    Assert-Check ((Get-FileHash -LiteralPath $installedExe).Hash -ceq $beforeDowngrade) 'Downgrade changed the newer executable.'
    Assert-Installed $Version $SourceDir

    foreach ($mutexName in @('Local\ScreenshotTool-1C6860C8-243D-4F14-B85C-26E74508D8AA', 'Local\ScreenshotTool.UpdateOperation')) {
        $mutex = [Threading.Mutex]::new($false, $mutexName)
        try {
            Assert-Check ((Invoke-Installer $InstallerPath 'install-mutex-blocked' -AllowFailure) -ne 0) "Setup ignored the running operation mutex $mutexName."
            Assert-Check ((Invoke-Installer $uninstaller 'uninstall-mutex-blocked' -AllowFailure) -ne 0) "Uninstall ignored the running operation mutex $mutexName."
            Assert-Installed $Version $SourceDir
            Assert-Check ((Read-RunValue).Value -ceq $installedCommand) 'A blocked operation changed sign-in.'
        } finally { $mutex.Dispose(); $mutex = $null }
    }
    $mutex = [Threading.Mutex]::new($false, 'Local\ScreenshotTool.Setup')
    try {
        Assert-Check ((Invoke-Installer $InstallerPath 'setup-mutex-blocked' -AllowFailure) -ne 0) 'Setup ignored another setup operation.'
        Assert-Check ((Invoke-Installer $uninstaller 'uninstall-setup-blocked' -AllowFailure) -ne 0) 'Uninstall ignored another setup operation.'
    } finally { $mutex.Dispose(); $mutex = $null }

    $unrelatedCommand = '"C:\unrelated\other.exe" --example'
    Write-RunValue $unrelatedCommand
    [void](Invoke-Installer $InstallerPath 'unrelated-sign-in')
    Assert-Check ((Read-RunValue).Value -ceq $unrelatedCommand) 'Setup overwrote an unrelated command in the named Run value.'
    $expandCommand = '%LOCALAPPDATA%\portable\ScreenshotTool.exe'
    Write-RunValue $expandCommand 'ExpandString'
    [void](Invoke-Installer $InstallerPath 'expand-sign-in')
    $run = Read-RunValue
    Assert-Check ($run.Kind -eq 'ExpandString' -and $run.Value -ceq $expandCommand) 'Setup changed an unsupported REG_EXPAND_SZ sign-in entry.'
    Write-RunValue ([byte[]]@(1, 2, 3)) 'Binary'
    [void](Invoke-Installer $InstallerPath 'binary-sign-in')
    $run = Read-RunValue
    Assert-Check ($run.Kind -eq 'Binary' -and ($run.Value -join ',') -ceq '1,2,3') 'Setup changed an unsupported binary sign-in entry.'

    Write-RunValue $portableCommand
    [void](Invoke-Installer $uninstaller 'uninstall-preserve-other-path')
    Assert-Removed
    Assert-Check ((Read-RunValue).Value -ceq $portableCommand) 'Uninstall removed sign-in for a different portable executable.'
    Assert-Check ((Get-FileHash -LiteralPath $settingsPath).Hash -ceq $settingsHash) 'Install or uninstall changed existing settings.'
    Assert-Check ((Get-FileHash -LiteralPath $screenshotPath).Hash -ceq $screenshotHash) 'Install or uninstall changed an existing screenshot.'

    [void](Invoke-Installer $InstallerPath 'reinstall-preserve-opt-in')
    Assert-Installed $Version $SourceDir
    Assert-Check ((Read-RunValue).Value -ceq $installedCommand) 'Reinstall did not preserve enabled sign-in.'
    [void](Invoke-Installer $uninstaller 'uninstall-current-path')
    Assert-Removed
    Assert-Check (-not (Read-RunValue).Exists) 'Uninstall left this installation''s sign-in entry behind.'
    Assert-Check ((Get-FileHash -LiteralPath $settingsPath).Hash -ceq $settingsHash) 'Final uninstall changed settings.'
    Assert-Check ((Get-FileHash -LiteralPath $screenshotPath).Hash -ceq $screenshotHash) 'Final uninstall changed screenshots.'
    Assert-NoLaunch
    $smokePassed = $true
    Write-Host "Installer smoke tests: $script:checks checks passed. Logs: $logOutputDir"
} finally {
    if ($mutex) { $mutex.Dispose() }
    if (Test-Path -LiteralPath $uninstaller) {
        try { [void](Invoke-Installer $uninstaller 'cleanup' -AllowFailure) } catch { Write-Warning $_ }
    }
    Remove-RunValue
    foreach ($ownedFile in @($settingsPath, $screenshotPath)) {
        if (Test-Path -LiteralPath $ownedFile) { Remove-Item -LiteralPath $ownedFile }
    }
    $registry.Dispose()
    $machineRegistry.Dispose()
    New-Item -ItemType Directory -Path $logOutputDir -Force | Out-Null
    Get-ChildItem -LiteralPath $testDir -Filter '*.log' -File | Copy-Item -Destination $logOutputDir
    [ordered]@{ passed = $smokePassed; version = $Version; checksPassed = $script:checks; installerInvocations = $script:invocations } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logOutputDir 'result.json') -Encoding utf8
}
