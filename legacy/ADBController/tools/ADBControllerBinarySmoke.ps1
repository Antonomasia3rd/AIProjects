param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'
$executablePath = [IO.Path]::GetFullPath($Executable)
if (-not [IO.File]::Exists($executablePath)) {
    throw "ADBController binary not found: $executablePath"
}

$temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = Join-Path $temporaryBase ("AIProjects.ADBControllerSmoke." + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
$resolvedRoot = [IO.Path]::GetFullPath($testRoot)
if (-not $resolvedRoot.StartsWith($temporaryBase, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use a smoke-test directory outside the temporary directory."
}

$script:invocation = 0
function Invoke-Controller {
    param(
        [string]$Name,
        [string[]]$Arguments,
        [int]$ExpectedExitCode
    )

    $script:invocation++
    $stdout = Join-Path $testRoot ("stdout-" + $script:invocation + ".txt")
    $stderr = Join-Path $testRoot ("stderr-" + $script:invocation + ".txt")
    $process = Start-Process `
        -FilePath $executablePath `
        -ArgumentList $Arguments `
        -Wait `
        -PassThru `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr
    if ($process.ExitCode -ne $ExpectedExitCode) {
        $outText = if (Test-Path -LiteralPath $stdout) { Get-Content -Raw -LiteralPath $stdout } else { '' }
        $errText = if (Test-Path -LiteralPath $stderr) { Get-Content -Raw -LiteralPath $stderr } else { '' }
        throw "$Name returned $($process.ExitCode), expected $ExpectedExitCode.`nstdout: $outText`nstderr: $errText"
    }
    return @{
        Stdout = if (Test-Path -LiteralPath $stdout) { Get-Content -Raw -LiteralPath $stdout } else { '' }
        Stderr = if (Test-Path -LiteralPath $stderr) { Get-Content -Raw -LiteralPath $stderr } else { '' }
    }
}

try {
    $sideEffectIni = Join-Path $testRoot 'help-side-effect.ini'
    $help = Invoke-Controller 'help' @('--definitely-invalid', "--ini=$sideEffectIni", '--help') 0
    if ($help.Stdout -notmatch 'Usage: ADBController.exe') {
        throw 'Help output did not contain the usage line.'
    }
    if (Test-Path -LiteralPath $sideEffectIni) {
        throw 'Help created the requested INI file.'
    }
    if (Test-Path -LiteralPath ([IO.Path]::ChangeExtension($sideEffectIni, '.log'))) {
        throw 'Help created a log file.'
    }

    $version = Invoke-Controller 'version' @('--definitely-invalid', '--version') 0
    if ($version.Stdout -notmatch 'ADB TV Controller 1') {
        throw 'Version output did not contain the product version.'
    }

    $invalidIni = Join-Path $testRoot 'invalid.ini'
    Invoke-Controller 'invalid typed setting' @("--ini=$invalidIni", '--theme=sepia', '--configure-only') 2 | Out-Null
    if (Test-Path -LiteralPath $invalidIni) {
        throw 'Invalid typed input created an INI file.'
    }

    $profile = Join-Path $testRoot 'profile.ini'
    Invoke-Controller 'atomic configuration' @(
        "--ini=$profile",
        '--theme=dark',
        '--device=SmokeTV=127.0.0.1:5557',
        '--configure-only'
    ) 0 | Out-Null
    $profileText = Get-Content -Raw -LiteralPath $profile
    if ($profileText -notmatch '"Theme"\s*=\s*"Dark"' -or
        $profileText -notmatch '"SmokeTV"\s*=\s*"127\.0\.0\.1:5557"') {
        throw 'Atomic configuration did not persist canonical typed values.'
    }
    if (Test-Path -LiteralPath ([IO.Path]::ChangeExtension($profile, '.log'))) {
        throw 'Configure-only unexpectedly created a log file.'
    }

    $configured = Invoke-Controller 'list configured' @("--ini=$profile", '--list-configured') 0
    if ($configured.Stdout -notmatch 'SmokeTV\s*=\s*127\.0\.0\.1:5557') {
        throw 'Configured-device listing did not expose the saved device.'
    }

    Invoke-Controller 'reboot confirmation gate' @(
        "--ini=$profile",
        '--target=SmokeTV',
        '--reboot'
    ) 2 | Out-Null
    Invoke-Controller 'single action gate' @(
        "--ini=$profile",
        '--target=SmokeTV',
        '--home',
        '--power'
    ) 2 | Out-Null

    $versionInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($executablePath)
    if ($versionInfo.FileVersion -ne '1.0.0.0' -or $versionInfo.ProductVersion -ne '1.0.0.0') {
        throw 'The built binary is missing the expected version resource.'
    }

    Write-Host 'ADBController binary smoke passed: 8 scenarios'
}
finally {
    if ([IO.Directory]::Exists($resolvedRoot) -and
        $resolvedRoot.StartsWith($temporaryBase, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
    }
}
