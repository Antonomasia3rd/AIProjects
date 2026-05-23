$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$servicePath = Join-Path $root 'SecureDesktopLauncherService.cpp'
$passwordPath = Join-Path $root 'SecureDesktopPasswordLauncher.cpp'

function Read-Source {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing source file: $Path"
    }
    return Get-Content -LiteralPath $Path -Raw
}

function Assert-SourceMatches {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Pattern
    )

    if ($Source -notmatch $Pattern) {
        throw "SecureDesktopLauncher source guardrail failed: $Name"
    }
    Write-Host "ok - $Name"
}

$service = Read-Source -Path $servicePath
$password = Read-Source -Path $passwordPath

Write-Host 'Running SecureDesktopLauncher source guardrails...'
Write-Host 'These checks validate launch/trust invariants only; they do not install or start services.'

Assert-SourceMatches `
    -Name 'service requires trusted executable and config path before loading programs' `
    -Source $service `
    -Pattern '(?s)TrustedExistingFilePath\(exePath\).*?TrustedExistingFilePath\(config\.configPath\)'

Assert-SourceMatches `
    -Name 'service validates each configured program path and working directory' `
    -Source $service `
    -Pattern '(?s)TrustedExistingFilePath\(program\.path\).*?\(program\.workingDirectory\.empty\(\) \|\| TrustedExistingDirectoryPath\(program\.workingDirectory\)\).*?config\.programs\.push_back\(program\)'

Assert-SourceMatches `
    -Name 'service keeps lpApplicationName pinned to trusted Path even when CommandLine is configured' `
    -Source $service `
    -Pattern '(?s)CreateProcessAsUserW\(\s*primaryToken,\s*program\.path\.c_str\(\),\s*commandLine\.empty\(\) \? nullptr : &commandLine\[0\]'

Assert-SourceMatches `
    -Name 'service retargets the duplicated token to the selected session before launch' `
    -Source $service `
    -Pattern '(?s)SetTokenInformation\(primaryToken,\s*TokenSessionId,\s*&tokenSessionId,\s*sizeof\(tokenSessionId\)\).*?CreateProcessAsUserW'

Assert-SourceMatches `
    -Name 'password launcher enforces trusted executable and config before loading launch policy' `
    -Source $password `
    -Pattern '(?s)TrustedExistingFilePath\(CurrentExePath\(\), trustError, L"Password launcher executable"\).*?TrustedExistingFilePath\(config\.configPath, trustError, L"Password launcher config"\)'

Assert-SourceMatches `
    -Name 'password launcher revalidates target and working directory immediately before CreateProcessW' `
    -Source $password `
    -Pattern '(?s)TrustedExistingFilePath\(state->config\.launchPath, trustError, L"Launch target"\).*?TrustedExistingDirectoryPath\(state->config\.workingDirectory, trustError, L"Launch working directory"\).*?CreateProcessW'

Assert-SourceMatches `
    -Name 'password launcher writes PBKDF2 hash and keeps legacy hash opt-in' `
    -Source $password `
    -Pattern '(?s)PasswordPbkdf2HashHex.*?KeepLegacySha256Hash.*?keepLegacySha256Hash'

Write-Host 'SecureDesktopLauncher source guardrails passed.'
