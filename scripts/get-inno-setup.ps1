[CmdletBinding()]
param([string]$ToolsDirectory = (Join-Path $PSScriptRoot '../build/tools'))
$ErrorActionPreference = 'Stop'
$tools = [IO.Path]::GetFullPath($ToolsDirectory)
$compilerDirectory = Join-Path $tools 'InnoSetup-6.7.3'
$compiler = Join-Path $compilerDirectory 'ISCC.exe'
if (-not (Test-Path -LiteralPath $compiler)) {
    New-Item -ItemType Directory -Path $tools -Force | Out-Null
    $download = Join-Path $tools 'innosetup-6.7.3.exe'
    $url = 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe'
    $expected = '9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732'
    Write-Host 'Downloading verified Inno Setup 6.7.3 compiler.'
    Invoke-WebRequest -Uri $url -OutFile $download
    if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash.ToLowerInvariant() -cne $expected) {
        throw 'Inno Setup download checksum does not match the pinned official release.'
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $download
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '(^|,\s*)CN=Pyrsys B\.V\.(,|$)') {
        throw 'Inno Setup download does not have a valid signature from Pyrsys B.V.'
    }
    $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /CURRENTUSER /PORTABLE=1 /NOICONS /TASKS="" /DIR="{0}"' -f $compilerDirectory
    $process = Start-Process -FilePath $download -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) { throw "Inno Setup compiler provisioning failed: $($process.ExitCode)." }
}
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Inno Setup compiler was not provisioned.' }
$signature = Get-AuthenticodeSignature -LiteralPath $compiler
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '(^|,\s*)CN=Pyrsys B\.V\.(,|$)') {
    throw 'Cached Inno Setup compiler signature is invalid.'
}
Write-Output $compiler
