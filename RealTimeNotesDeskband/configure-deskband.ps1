param(
    [ValidateSet('resin', 'stamina', 'charge', 'all')]
    [string]$Resource = 'resin',

    [string]$UID,
    [string]$LTokenV2,
    [string]$LTuidV2,

    [string]$ImportFromJson,
    [string]$ImportFromDir,

    [int]$RefreshIntervalSeconds = 0,
    [switch]$NoSelect
)

$ErrorActionPreference = 'Stop'

$SettingsKey = 'HKCU:\Software\RealTimeNotesDeskband'
$AccountsKey = Join-Path $SettingsKey 'Accounts'
$KnownResources = @('resin', 'stamina', 'charge')
$CookieFiles = @{
    resin = 'genshin_cookie.json'
    stamina = 'hsr_cookie.json'
    charge = 'zzz_cookie.json'
}

function Get-JsonString {
    param(
        [object]$Object,
        [string]$Name
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return $null
    }
    return [string]$property.Value
}

function Get-JsonInt {
    param(
        [object]$Object,
        [string]$Name
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return 0
    }
    return [int]$property.Value
}

function Read-SecretText {
    param([string]$Prompt)

    $secure = Read-Host -Prompt $Prompt -AsSecureString
    $ptr = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try {
        return [System.Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
    } finally {
        [System.Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
    }
}

function Ensure-RegistryKey {
    param([string]$Path)
    if (-not (Test-Path -Path $Path)) {
        New-Item -Path $Path -Force | Out-Null
    }
}

function Set-ResourceAccount {
    param(
        [string]$ResourceName,
        [string]$AccountUID,
        [string]$AccountLTokenV2,
        [string]$AccountLTuidV2,
        [int]$AccountRefreshIntervalSeconds
    )

    if ([string]::IsNullOrWhiteSpace($AccountUID)) {
        throw "Missing UID for $ResourceName."
    }
    if ([string]::IsNullOrWhiteSpace($AccountLTokenV2)) {
        throw "Missing ltoken_v2 for $ResourceName."
    }
    if ([string]::IsNullOrWhiteSpace($AccountLTuidV2)) {
        throw "Missing ltuid_v2 for $ResourceName."
    }

    $key = Join-Path $AccountsKey $ResourceName
    Ensure-RegistryKey $key
    New-ItemProperty -Path $key -Name 'UID' -Value $AccountUID -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $key -Name 'LTokenV2' -Value $AccountLTokenV2 -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $key -Name 'LTuidV2' -Value $AccountLTuidV2 -PropertyType String -Force | Out-Null

    if ($AccountRefreshIntervalSeconds -gt 0) {
        New-ItemProperty -Path $key -Name 'RefreshIntervalSeconds' -Value $AccountRefreshIntervalSeconds -PropertyType DWord -Force | Out-Null
    }

    Write-Host "Configured $ResourceName account credentials."
}

function Import-ResourceAccount {
    param(
        [string]$ResourceName,
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Cookie JSON was not found: $Path"
    }

    $json = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $refresh = $RefreshIntervalSeconds
    if ($refresh -le 0) {
        $refresh = Get-JsonInt $json 'refresh_interval'
    }

    Set-ResourceAccount `
        -ResourceName $ResourceName `
        -AccountUID (Get-JsonString $json 'uid') `
        -AccountLTokenV2 (Get-JsonString $json 'ltoken_v2') `
        -AccountLTuidV2 (Get-JsonString $json 'ltuid_v2') `
        -AccountRefreshIntervalSeconds $refresh
}

if ($ImportFromJson -and $ImportFromDir) {
    throw 'Use either -ImportFromJson or -ImportFromDir, not both.'
}

Ensure-RegistryKey $SettingsKey
Ensure-RegistryKey $AccountsKey

if ($ImportFromDir) {
    $resourcesToImport = if ($Resource -eq 'all') { $KnownResources } else { @($Resource) }
    foreach ($name in $resourcesToImport) {
        $path = Join-Path $ImportFromDir $CookieFiles[$name]
        if (Test-Path -LiteralPath $path) {
            Import-ResourceAccount -ResourceName $name -Path $path
        } else {
            Write-Warning "Skipped $name; cookie JSON was not found at $path"
        }
    }
} elseif ($ImportFromJson) {
    if ($Resource -eq 'all') {
        throw '-ImportFromJson requires a single -Resource value.'
    }
    Import-ResourceAccount -ResourceName $Resource -Path $ImportFromJson
} else {
    if ($Resource -eq 'all') {
        throw 'Manual configuration requires a single -Resource value.'
    }

    if ([string]::IsNullOrWhiteSpace($UID)) {
        $UID = Read-Host -Prompt 'UID'
    }
    if ([string]::IsNullOrWhiteSpace($LTokenV2)) {
        $LTokenV2 = Read-SecretText 'ltoken_v2'
    }
    if ([string]::IsNullOrWhiteSpace($LTuidV2)) {
        $LTuidV2 = Read-SecretText 'ltuid_v2'
    }

    Set-ResourceAccount `
        -ResourceName $Resource `
        -AccountUID $UID `
        -AccountLTokenV2 $LTokenV2 `
        -AccountLTuidV2 $LTuidV2 `
        -AccountRefreshIntervalSeconds $RefreshIntervalSeconds
}

if (-not $NoSelect -and $Resource -ne 'all') {
    New-ItemProperty -Path $SettingsKey -Name 'Resource' -Value $Resource -PropertyType String -Force | Out-Null
}

Write-Host "Credentials are stored under HKCU:\Software\RealTimeNotesDeskband\Accounts."
Write-Host 'Secret values were not printed.'
