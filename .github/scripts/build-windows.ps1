param(
    [string[]]$SkipProjects = @()
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$ArtifactsDir = Join-Path $RepoRoot 'artifacts'
$StageRoot = Join-Path $RepoRoot 'build\github-artifacts'

function Write-Section([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message"
}

function Test-SkipProject([string]$Name) {
    foreach ($skipValue in $SkipProjects) {
        foreach ($skip in (($skipValue -as [string]) -split ',')) {
            $trimmed = $skip.Trim()
            if ($trimmed -and $trimmed.Equals($Name, [StringComparison]::OrdinalIgnoreCase)) {
                Write-Host "Skipping $Name because it was passed in -SkipProjects."
                return $true
            }
        }
    }

    return $false
}

function Invoke-External([string]$FilePath, [string[]]$Arguments, [string]$WorkingDirectory) {
    Write-Host "> $FilePath $($Arguments -join ' ')"
    Push-Location $WorkingDirectory
    try {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$FilePath exited with code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}

function Import-VsDevEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        return
    }

    $candidates = @()
    if ($env:VCINSTALLDIR) {
        $candidates += Join-Path $env:VCINSTALLDIR 'Auxiliary\Build\vcvars64.bat'
    }
    if ($env:VSINSTALLDIR) {
        $candidates += Join-Path $env:VSINSTALLDIR 'VC\Auxiliary\Build\vcvars64.bat'
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($install) {
            $candidates += Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
        }
    }

    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, 'D:\Program Files', 'D:\Program Files (x86)')) {
        if (-not $base) { continue }
        foreach ($year in @('2022', '2019')) {
            foreach ($edition in @('BuildTools', 'Community', 'Professional', 'Enterprise')) {
                $candidates += Join-Path $base "Microsoft Visual Studio\$year\$edition\VC\Auxiliary\Build\vcvars64.bat"
            }
        }
    }

    $vcvars = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $vcvars) {
        throw 'vcvars64.bat not found. Install Visual Studio Build Tools with the C++ workload.'
    }

    Write-Host "Using $vcvars"
    $envLines = cmd.exe /c "`"$vcvars`" amd64 >nul && set"
    foreach ($line in $envLines) {
        if ($line -match '^(.*?)=(.*)$') {
            Set-Item -LiteralPath "Env:$($matches[1])" -Value $matches[2]
        }
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw 'cl.exe was not found after importing the Visual Studio developer environment.'
    }
}

function Get-CscPath {
    $csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    if (-not (Test-Path $csc)) {
        throw "C# compiler not found at $csc"
    }
    return $csc
}

function Ensure-BuildDir([string]$Project) {
    $dir = Join-Path $RepoRoot (Join-Path $Project 'build')
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    return $dir
}

function Build-CSharpProject {
    param(
        [Parameter(Mandatory=$true)][string]$Project,
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$OutputName,
        [string]$Target = 'exe',
        [string[]]$References = @()
    )

    Write-Section "Build $Project"
    $csc = Get-CscPath
    $projectDir = Join-Path $RepoRoot $Project
    $outDir = Ensure-BuildDir $Project
    $out = Join-Path $outDir $OutputName
    $args = @('/nologo', '/optimize+', "/target:$Target", "/out:$out")
    foreach ($ref in $References) {
        $args += "/r:$ref"
    }
    $args += (Join-Path $projectDir $Source)
    Invoke-External $csc $args $projectDir
}

function Build-MsvcProject {
    param(
        [Parameter(Mandatory=$true)][string]$Project,
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$OutputName,
        [string[]]$CompileArgs = @(),
        [string[]]$LinkArgs = @()
    )

    Write-Section "Build $Project"
    Import-VsDevEnvironment
    $projectDir = Join-Path $RepoRoot $Project
    $outDir = Ensure-BuildDir $Project
    $objDir = Join-Path $outDir 'obj'
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null
    $obj = Join-Path $objDir ([IO.Path]::GetFileNameWithoutExtension($Source) + '.obj')
    $out = Join-Path $outDir $OutputName
    $args = @('/nologo') + $CompileArgs + @("/Fo$obj", "/Fe$out", (Join-Path $projectDir $Source))
    if ($LinkArgs.Count -gt 0) {
        $args += '/link'
        $args += $LinkArgs
    }
    Invoke-External 'cl.exe' $args $projectDir
}

function Build-CmdProject {
    param(
        [Parameter(Mandatory=$true)][string]$Project,
        [Parameter(Mandatory=$true)][string]$Command
    )

    Write-Section "Build $Project"
    $projectDir = Join-Path $RepoRoot $Project
    Invoke-External 'cmd.exe' @('/c', $Command) $projectDir
}

function Copy-IfExists([string]$Source, [string]$Destination) {
    if (Test-Path $Source) {
        Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
    }
}

function New-BuildInfo([string]$Destination) {
    $sha = if ($env:GITHUB_SHA) { $env:GITHUB_SHA } else { (& git -C $RepoRoot rev-parse HEAD) }
    $ref = if ($env:GITHUB_REF) { $env:GITHUB_REF } else { (& git -C $RepoRoot branch --show-current) }
    $repo = if ($env:GITHUB_REPOSITORY) { $env:GITHUB_REPOSITORY } else { 'local' }
    @(
        "Repository: $repo",
        "Ref: $ref",
        "Commit: $sha",
        "BuiltOnUtc: $([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "Builder: GitHub Actions workflow when downloaded from a GitHub run or release."
    ) | Set-Content -LiteralPath (Join-Path $Destination 'BUILD_INFO.txt') -Encoding UTF8
}

function New-Package {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][string]$Project,
        [Parameter(Mandatory=$true)][string[]]$Paths,
        [string[]]$ExtraPaths = @()
    )

    Write-Section "Package $Name"
    $packageRoot = Join-Path $StageRoot $Name
    Remove-Item -LiteralPath $packageRoot -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null

    $projectDir = Join-Path $RepoRoot $Project
    foreach ($relative in $Paths) {
        $source = Join-Path $projectDir $relative
        if (-not (Test-Path $source)) {
            throw "Package input missing: $source"
        }
        Copy-Item -LiteralPath $source -Destination $packageRoot -Recurse -Force
    }

    Copy-IfExists (Join-Path $projectDir 'README.md') $packageRoot
    foreach ($relative in $ExtraPaths) {
        Copy-IfExists (Join-Path $projectDir $relative) $packageRoot
    }

    New-BuildInfo $packageRoot

    $zip = Join-Path $ArtifactsDir "$Name-windows-x64.zip"
    Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
    Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $zip -Force
    Write-Host "Created $zip"
}

Remove-Item -LiteralPath $ArtifactsDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $StageRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $ArtifactsDir, $StageRoot | Out-Null

if (-not (Test-SkipProject 'AllowContentAboveLock')) {
    Build-CSharpProject -Project 'AllowContentAboveLock' -Source 'AllowContentAboveLock.cs' -OutputName 'AllowContentAboveLock.exe' -References @('System.ServiceProcess.dll')
    New-Package -Name 'AllowContentAboveLock' -Project 'AllowContentAboveLock' -Paths @('build\AllowContentAboveLock.exe')
}
if (-not (Test-SkipProject 'asusblink')) {
    Build-CSharpProject -Project 'asusblink' -Source 'asusblink.cs' -OutputName 'asusblink.exe' -Target 'winexe' -References @('System.Core.dll', 'System.Windows.Forms.dll', 'System.Drawing.dll', 'System.Management.dll', 'System.Runtime.Serialization.dll')
    New-Package -Name 'asusblink' -Project 'asusblink' -Paths @('build\asusblink.exe')
}
if (-not (Test-SkipProject 'capsblink')) {
    Build-CSharpProject -Project 'capsblink' -Source 'capsblink.cs' -OutputName 'capsblink.exe'
    New-Package -Name 'capsblink' -Project 'capsblink' -Paths @('build\capsblink.exe')
}
if (-not (Test-SkipProject 'YourPhoneHideBanner')) {
    Build-CSharpProject -Project 'YourPhoneHideBanner' -Source 'YourPhoneHideBanner.cs' -OutputName 'YourPhoneHideBanner.exe' -References @('System.ServiceProcess.dll')
    New-Package -Name 'YourPhoneHideBanner' -Project 'YourPhoneHideBanner' -Paths @('build\YourPhoneHideBanner.exe')
}
if (-not (Test-SkipProject 'DiscordRPC')) {
    Write-Section 'Build DiscordRPC'
    Invoke-External 'powershell.exe' @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'build.ps1') (Join-Path $RepoRoot 'DiscordRPC')
    New-Package -Name 'DiscordRPC' -Project 'DiscordRPC' -Paths @('build\DiscordRPC.exe')
}
if (-not (Test-SkipProject 'NowPlayingTile')) {
    Write-Section 'Build NowPlayingTile'
    Invoke-External 'powershell.exe' @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'build.ps1') (Join-Path $RepoRoot 'NowPlayingTile')
    New-Package -Name 'NowPlayingTile' -Project 'NowPlayingTile' -Paths @('build\NowPlayingTile.exe') -ExtraPaths @('register-dev-package.ps1', 'unregister-dev-package.ps1', 'launch-packaged.ps1', 'launch-widget.ps1', 'install-startup.ps1', 'uninstall-startup.ps1', 'open-settings.ps1', 'package')
}
if (-not (Test-SkipProject 'GenerateAssets')) {
    Build-CmdProject -Project 'DesktopStub' -Command 'BuildGenerateAssets.cmd'
    New-Package -Name 'GenerateAssets' -Project 'DesktopStub' -Paths @('build\GenerateAssets.exe')
}
if (-not (Test-SkipProject 'CharmTray')) {
    Build-MsvcProject -Project 'CharmTray' -Source 'CharmTray.cpp' -OutputName 'CharmTray.exe' -CompileArgs @('/std:c++17', '/EHsc', '/O2', '/W3', '/MT', '/D_UNICODE', '/DUNICODE', '/D_WIN32_WINNT=0x0602') -LinkArgs @('user32.lib', 'ole32.lib', 'shell32.lib', '/SUBSYSTEM:WINDOWS')
    New-Package -Name 'CharmTray' -Project 'CharmTray' -Paths @('build\CharmTray.exe')
}
if (-not (Test-SkipProject 'SecureDesktopLauncher')) {
    Build-MsvcProject -Project 'SecureDesktopLauncher' -Source 'SecureDesktopLauncherService.cpp' -OutputName 'SecureDesktopLauncherService.exe' -CompileArgs @('/EHsc', '/W4') -LinkArgs @('advapi32.lib', 'wtsapi32.lib', 'userenv.lib')
    Build-MsvcProject -Project 'SecureDesktopLauncher' -Source 'SecureDesktopPasswordLauncher.cpp' -OutputName 'SecureDesktopPasswordLauncher.exe' -CompileArgs @('/EHsc', '/W4') -LinkArgs @('bcrypt.lib', 'shell32.lib', 'user32.lib', 'gdi32.lib', 'comctl32.lib', 'version.lib', '/SUBSYSTEM:WINDOWS')
    New-Package -Name 'SecureDesktopLauncher' -Project 'SecureDesktopLauncher' -Paths @('build\SecureDesktopLauncherService.exe', 'build\SecureDesktopPasswordLauncher.exe') -ExtraPaths @('SecureDesktopLauncher.README.md')
}
if (-not (Test-SkipProject 'RealTimeNotesDeskband')) {
    if (-not (Get-Command g++.exe -ErrorAction SilentlyContinue)) {
        throw 'g++.exe is required for RealTimeNotesDeskband. The GitHub workflow installs MinGW-w64 before running this script.'
    }
    Build-CmdProject -Project 'RealTimeNotesDeskband' -Command 'BuildDeskband.cmd'
    New-Package -Name 'RealTimeNotesDeskband' -Project 'RealTimeNotesDeskband' -Paths @('build\RealTimeNotesDeskband.dll') -ExtraPaths @('RegisterDeskband.cmd', 'UnregisterDeskband.cmd', 'ConfigureDeskband.cmd')
}

Write-Section 'Artifacts'
Get-ChildItem -LiteralPath $ArtifactsDir -Filter '*.zip' | Sort-Object Name | ForEach-Object { Write-Host $_.FullName }
