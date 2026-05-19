param(
    [string]$DllPath
)

$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path $PSScriptRoot).Path
$Dll = if ($DllPath) { (Resolve-Path $DllPath).Path } else { Join-Path $Root 'build\RealTimeNotesDeskband.dll' }

if (-not (Test-Path $Dll)) {
    throw "Deskband DLL was not found. Build it first with: .\BuildDeskband.cmd"
}

function Test-ConfigDir {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path $Path)) {
        return $false
    }

    foreach ($name in @('genshin_cookie.json', 'hsr_cookie.json', 'zzz_cookie.json')) {
        $file = Join-Path $Path $name
        try {
            $stream = [System.IO.File]::Open($file, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
            $length = $stream.Length
            $stream.Dispose()
        } catch {
            continue
        }

        if ($length -gt 0) {
            return $true
        }
    }
    return $false
}

function Test-AssetDir {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path $Path)) {
        return $false
    }

    foreach ($name in @(
        'genshin\resin_not_full.ico',
        'hsr\stamina_not_full.ico',
        'zzz\charge_not_full.ico'
    )) {
        if (Test-Path (Join-Path $Path $name)) {
            return $true
        }
    }
    return $false
}

function Test-AccountConfig {
    $base = 'HKCU:\Software\RealTimeNotesDeskband\Accounts'
    foreach ($name in @('resin', 'stamina', 'charge')) {
        $props = Get-ItemProperty -Path (Join-Path $base $name) -ErrorAction SilentlyContinue
        if ((-not [string]::IsNullOrWhiteSpace($props.UID)) -and
            (-not [string]::IsNullOrWhiteSpace($props.LTokenV2)) -and
            (-not [string]::IsNullOrWhiteSpace($props.LTuidV2))) {
            return $true
        }
    }
    return $false
}

function Get-ExistingSetting {
    param([string]$Name)
    $key = 'HKCU:\Software\RealTimeNotesDeskband'
    if (-not (Test-Path $key)) {
        return $null
    }
    return (Get-ItemProperty -Path $key -Name $Name -ErrorAction SilentlyContinue).$Name
}

function Ensure-RegistryKey {
    param([string]$Path)
    if (-not (Test-Path -Path $Path)) {
        New-Item -Path $Path -Force | Out-Null
    }
}
$ExistingConfigDir = Get-ExistingSetting 'ConfigDir'
if (Test-AccountConfig) {
    $ConfigDir = if ($ExistingConfigDir) { $ExistingConfigDir } else { $Root }
} else {
    $ConfigCandidates = @(
        $ExistingConfigDir,
        $Root,
        (Join-Path $Root 'references\genshin-real-time-notes-0.0.8'),
        (Resolve-Path (Join-Path $Root '..\..\..\Apps\Real-Time Notes') -ErrorAction SilentlyContinue)
    )
    $ConfigDir = ($ConfigCandidates | Where-Object { Test-ConfigDir "$_" } | Select-Object -First 1)
    if (-not $ConfigDir) {
        $ConfigDir = $Root
    }
}

$AssetCandidates = @(
    (Get-ExistingSetting 'AssetDir'),
    (Join-Path $Root 'references\genshin-real-time-notes-0.0.8\embedded\assets')
)
$AssetDir = ($AssetCandidates | Where-Object { Test-AssetDir "$_" } | Select-Object -First 1)
if (-not $AssetDir) {
    $AssetDir = Join-Path $Root 'references\genshin-real-time-notes-0.0.8\embedded\assets'
}

$ExistingResource = Get-ExistingSetting 'Resource'

$RegSvr32 = Join-Path $env:WINDIR 'System32\regsvr32.exe'
$Process = Start-Process -FilePath $RegSvr32 -ArgumentList @('/s', $Dll) -Wait -PassThru -WindowStyle Hidden
if ($Process.ExitCode -ne 0) {
    throw "regsvr32 failed with exit code $($Process.ExitCode)"
}

$SettingsKey = 'HKCU:\Software\RealTimeNotesDeskband'
Ensure-RegistryKey $SettingsKey
New-ItemProperty -Path $SettingsKey -Name 'InstallDir' -Value (Split-Path $Dll -Parent) -PropertyType String -Force | Out-Null
New-ItemProperty -Path $SettingsKey -Name 'ConfigDir' -Value "$ConfigDir" -PropertyType String -Force | Out-Null
New-ItemProperty -Path $SettingsKey -Name 'AssetDir' -Value "$AssetDir" -PropertyType String -Force | Out-Null
if ($ExistingResource) {
    New-ItemProperty -Path $SettingsKey -Name 'Resource' -Value "$ExistingResource" -PropertyType String -Force | Out-Null
} else {
    New-ItemProperty -Path $SettingsKey -Name 'Resource' -Value 'auto' -PropertyType String -Force | Out-Null
}

Write-Host "Registered Real Time Notes Deskband."
Write-Host "DLL      : $Dll"
Write-Host "ConfigDir: $ConfigDir"
Write-Host "AssetDir : $AssetDir"
Write-Host "Enable it from taskbar Toolbars > Real Time Notes."
