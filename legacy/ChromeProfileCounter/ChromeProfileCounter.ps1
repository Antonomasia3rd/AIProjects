$ErrorActionPreference = "Stop"

$UserData = Join-Path $env:LOCALAPPDATA "Google\Chrome\User Data"
$LocalState = Join-Path $UserData "Local State"
$BackupDir = Join-Path $UserData "Profile Counter Backups"

function Pause-Menu {
    Write-Host
    Read-Host "Press Enter to continue"
}

function Get-StateText {
    if (!(Test-Path $LocalState)) {
        throw "Local State not found:`n$LocalState"
    }
    return [IO.File]::ReadAllText($LocalState)
}

function Get-Counter {
    $text = Get-StateText
    $m = [regex]::Match(
        $text,
        '"profiles_created"\s*:\s*(\d+)'
    )

    if (!$m.Success) {
        throw "profile.profiles_created was not found in Local State."
    }

    return [int]$m.Groups[1].Value
}

function Get-DiskProfiles {
    $result = @{}

    Get-ChildItem $UserData -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^Profile (\d+)$' } |
        ForEach-Object {
            $n = [int]$Matches[1]
            $result[$n] = $_.FullName
        }

    return $result
}

function Get-RegisteredProfiles {
    $result = @{}

    try {
        $obj = (Get-StateText) | ConvertFrom-Json

        if ($null -ne $obj.profile.info_cache) {
            foreach ($prop in $obj.profile.info_cache.PSObject.Properties) {
                if ($prop.Name -match '^Profile (\d+)$') {
                    $result[[int]$Matches[1]] = $prop.Name
                }
            }
        }
    }
    catch {
        Write-Host "WARNING: Could not parse profile.info_cache." -ForegroundColor Yellow
    }

    return $result
}

function Get-FirstSafeNumber {
    $disk = Get-DiskProfiles
    $registered = Get-RegisteredProfiles

    $n = 1

    while ($disk.ContainsKey($n) -or $registered.ContainsKey($n)) {
        $n++
    }

    return $n
}

function Test-ChromeRunning {
    return $null -ne (Get-Process chrome -ErrorAction SilentlyContinue)
}

function Backup-LocalState {
    if (!(Test-Path $BackupDir)) {
        New-Item -ItemType Directory -Path $BackupDir | Out-Null
    }

    $stamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
    $destination = Join-Path $BackupDir "Local State.$stamp.backup"

    Copy-Item $LocalState $destination

    return $destination
}

function Set-Counter([int]$NewValue) {
    if ($NewValue -lt 1) {
        throw "Counter must be 1 or greater."
    }

    if (Test-ChromeRunning) {
        Write-Host
        Write-Host "STOP: Chrome is currently running." -ForegroundColor Red
        Write-Host "Close ALL Chrome windows and run this option again."
        Write-Host
        return
    }

    $disk = Get-DiskProfiles
    $registered = Get-RegisteredProfiles

    if ($disk.ContainsKey($NewValue)) {
        Write-Host
        Write-Host "REFUSED: Profile $NewValue already exists on disk." -ForegroundColor Red
        return
    }

    if ($registered.ContainsKey($NewValue)) {
        Write-Host
        Write-Host "REFUSED: Profile $NewValue is still registered in Chrome." -ForegroundColor Red
        return
    }

    $oldValue = Get-Counter

    Write-Host
    Write-Host "Current counter : $oldValue"
    Write-Host "New counter     : $NewValue" -ForegroundColor Cyan
    Write-Host "Next profile    : Profile $NewValue" -ForegroundColor Green
    Write-Host

    $confirmation = Read-Host "Type YES to apply"

    if ($confirmation -cne "YES") {
        Write-Host "Cancelled." -ForegroundColor Yellow
        return
    }

    $backup = Backup-LocalState

    $text = Get-StateText

    $newText = [regex]::Replace(
        $text,
        '("profiles_created"\s*:\s*)\d+',
        '${1}' + $NewValue,
        1
    )

    [IO.File]::WriteAllText(
        $LocalState,
        $newText,
        (New-Object Text.UTF8Encoding($false))
    )

    $verify = Get-Counter

    Write-Host
    if ($verify -eq $NewValue) {
        Write-Host "SUCCESS." -ForegroundColor Green
        Write-Host "profiles_created = $verify"
        Write-Host "Backup:"
        Write-Host $backup
    }
    else {
        Write-Host "Verification failed!" -ForegroundColor Red
        Write-Host "Expected $NewValue but found $verify."
    }
}

function Show-Status {
    $counter = Get-Counter
    $disk = Get-DiskProfiles
    $registered = Get-RegisteredProfiles
    $safe = Get-FirstSafeNumber

    Write-Host
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host " Chrome Profile Counter Status"
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host
    Write-Host "User Data:"
    Write-Host $UserData
    Write-Host
    Write-Host "Current Chrome counter : $counter"
    Write-Host "Chrome would request   : Profile $counter"
    Write-Host "First safe free number : Profile $safe" -ForegroundColor Green
    Write-Host

    $allNumbers = @(
        @($disk.Keys)
        @($registered.Keys)
    ) | Sort-Object -Unique

    if ($allNumbers.Count -eq 0) {
        Write-Host "No numbered profiles found."
        return
    }

    Write-Host ("{0,-12} {1,-10} {2,-12}" -f "PROFILE", "ON DISK", "REGISTERED")
    Write-Host ("{0,-12} {1,-10} {2,-12}" -f "-------", "-------", "----------")

    foreach ($n in $allNumbers) {
        $onDisk = if ($disk.ContainsKey($n)) { "Yes" } else { "-" }
        $reg = if ($registered.ContainsKey($n)) { "Yes" } else { "-" }

        Write-Host ("{0,-12} {1,-10} {2,-12}" -f "Profile $n", $onDisk, $reg)
    }
}

while ($true) {
    Clear-Host

    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host "       CHROME PROFILE COUNTER TOOL"
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host
    Write-Host "  [1] Show profile status"
    Write-Host "  [2] Auto-fix counter to first free number"
    Write-Host "  [3] Set counter manually"
    Write-Host "  [4] Open Chrome User Data folder"
    Write-Host "  [Q] Quit"
    Write-Host

    try {
        $counter = Get-Counter
        $safe = Get-FirstSafeNumber

        Write-Host "Current counter : $counter"
        Write-Host "First free      : $safe" -ForegroundColor Green

        if (Test-ChromeRunning) {
            Write-Host "Chrome status   : RUNNING - edits disabled" -ForegroundColor Red
        }
        else {
            Write-Host "Chrome status   : Closed" -ForegroundColor Green
        }
    }
    catch {
        Write-Host $_.Exception.Message -ForegroundColor Red
    }

    Write-Host
    $choice = Read-Host "Choose"

    switch ($choice.ToUpper()) {
        "1" {
            Clear-Host
            Show-Status
            Pause-Menu
        }

        "2" {
            Clear-Host
            Show-Status

            $safe = Get-FirstSafeNumber

            Write-Host
            Write-Host "AUTO FIX" -ForegroundColor Cyan
            Write-Host "This will set Chrome's counter to $safe."
            Write-Host "The next profile should therefore be Profile $safe."
            Write-Host

            Set-Counter $safe
            Pause-Menu
        }

        "3" {
            Clear-Host
            Show-Status
            Write-Host

            $inputValue = Read-Host "Enter desired next Profile number"

            if ($inputValue -match '^\d+$') {
                Set-Counter ([int]$inputValue)
            }
            else {
                Write-Host "Invalid number." -ForegroundColor Red
            }

            Pause-Menu
        }

        "4" {
            Start-Process explorer.exe $UserData
        }

        "Q" {
            break
        }

        default {
            Write-Host "Unknown option." -ForegroundColor Yellow
            Start-Sleep -Milliseconds 700
        }
    }
}