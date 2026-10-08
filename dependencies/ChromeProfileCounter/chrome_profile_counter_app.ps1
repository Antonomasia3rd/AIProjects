$ErrorActionPreference = "Stop"

$UserData = Join-Path $env:LOCALAPPDATA "Google\Chrome\User Data"
$LocalState = Join-Path $UserData "Local State"
$BackupDir = Join-Path $UserData "Profile Counter Backups"

function Pause-Menu {
    Write-Host
    Read-Host "Press Enter to continue"
}

function Get-StateText {
    if (!(Test-Path -LiteralPath $LocalState -PathType Leaf)) {
        throw "Local State not found:`n$LocalState"
    }
    return [IO.File]::ReadAllText($LocalState)
}

function Get-CounterMatch([string]$Text) {
    $state = $Text | ConvertFrom-Json -ErrorAction Stop
    $matches = [regex]::Matches($Text, '"profiles_created"\s*:\s*(\d+)(?=\s*[,}])')
    $counter = 0
    if ($null -eq $state.profile -or $null -eq $state.profile.profiles_created -or
        $matches.Count -ne 1 -or
        -not [int]::TryParse($matches[0].Groups[1].Value, [ref]$counter) -or
        $state.profile.profiles_created -ne $counter) {
        throw "Local State must contain one unambiguous integer profile.profiles_created value."
    }
    # A same-valued field elsewhere can satisfy the comparison above when the
    # real property name uses JSON escapes. Prove that this exact digit span
    # changes the parsed profile counter before offering it to the writer.
    $digits = $matches[0].Groups[1]
    $probeText = $Text.Substring(0, $digits.Index) + '-1' +
        $Text.Substring($digits.Index + $digits.Length)
    $probe = $probeText | ConvertFrom-Json -ErrorAction Stop
    if ($null -eq $probe.profile -or $probe.profile.profiles_created -ne -1) {
        throw "The matched counter does not belong to profile.profiles_created."
    }
    return $matches[0]
}

function Get-Counter {
    return [int](Get-CounterMatch (Get-StateText)).Groups[1].Value
}

function Set-CounterInText([string]$Text, [int]$NewValue) {
    if ($NewValue -lt 1) { throw "Counter must be 1 or greater." }
    $digits = (Get-CounterMatch $Text).Groups[1]
    return $Text.Substring(0, $digits.Index) +
        $NewValue.ToString([Globalization.CultureInfo]::InvariantCulture) +
        $Text.Substring($digits.Index + $digits.Length)
}

function Get-DiskProfiles {
    $result = @{}

    foreach ($directory in Get-ChildItem -LiteralPath $UserData -Directory -ErrorAction Stop) {
        $match = [regex]::Match($directory.Name, '^Profile (\d+)$')
        $number = 0
        if ($match.Success -and [int]::TryParse($match.Groups[1].Value, [ref]$number)) {
            $result[$number] = $directory.FullName
        }
    }

    return $result
}

function Get-RegisteredProfiles([string]$Text = (Get-StateText)) {
    $result = @{}

    $obj = $Text | ConvertFrom-Json -ErrorAction Stop
    if ($null -eq $obj.profile) { throw "Local State has no readable profile object." }

    if ($null -ne $obj.profile.info_cache) {
        foreach ($prop in $obj.profile.info_cache.PSObject.Properties) {
            if ($prop.Name -match '^Profile (\d+)$') {
                $result[[int]$Matches[1]] = $prop.Name
            }
        }
    }

    return $result
}

function Get-FirstSafeNumber {
    $disk = Get-DiskProfiles
    $registered = Get-RegisteredProfiles

    $n = 1

    while ($disk.ContainsKey($n) -or $registered.ContainsKey($n)) {
        if ($n -eq [int]::MaxValue) { throw "No available numbered profile fits the counter range." }
        $n++
    }

    return $n
}

function Test-ChromeRunning {
    return $null -ne (Get-Process chrome -ErrorAction SilentlyContinue)
}

function New-LocalStateBackupPath {
    [IO.Directory]::CreateDirectory($BackupDir) | Out-Null
    $stamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss-fff"
    $nonce = [Guid]::NewGuid().ToString("N")
    $destination = Join-Path $BackupDir "Local State.$stamp.$nonce.backup"
    if (Test-Path -LiteralPath $destination) { throw "Backup filename already exists; retry the change." }
    return $destination
}

function Write-LocalStateAtomically([string]$Text, [string]$ExpectedText) {
    if (Test-ChromeRunning) {
        throw "Chrome started while the counter change was being prepared. Local State was left unchanged."
    }

    $directory = [IO.Path]::GetDirectoryName($LocalState)
    $temporary = Join-Path $directory (".Local State." + [Guid]::NewGuid().ToString("N") + ".tmp")
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $identity = [BitConverter]::ToString($sha.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes([IO.Path]::GetFullPath($LocalState).ToUpperInvariant()))).Replace('-', '')
    } finally { $sha.Dispose() }
    # Serialize this tool's own writers; Chrome does not participate in this lock.
    $mutex = New-Object Threading.Mutex($false, ("Global\AIProjects.ChromeProfileCounter." + $identity))
    $locked = $false
    try {
        try { $locked = $mutex.WaitOne(5000) }
        catch [Threading.AbandonedMutexException] { $locked = $true }
        if (-not $locked) { throw "Another counter edit is still running. Try again later." }
        [IO.File]::WriteAllText(
            $temporary,
            $Text,
            (New-Object Text.UTF8Encoding($false, $true))
        )
        if (Test-ChromeRunning) {
            throw "Chrome started before Local State could be replaced. Local State was left unchanged."
        }
        if ((Get-StateText) -cne $ExpectedText) {
            throw "Local State changed after review. Nothing was replaced; review the new state and retry."
        }
        $newValue = [int](Get-CounterMatch $Text).Groups[1].Value
        if ((Get-DiskProfiles).ContainsKey($newValue) -or (Get-RegisteredProfiles $ExpectedText).ContainsKey($newValue)) {
            throw "The selected profile number is now in use. Review the current state and retry."
        }
        $backup = New-LocalStateBackupPath
        # Capture the exact replaced file, not an earlier independently read copy.
        [IO.File]::Replace($temporary, $LocalState, $backup)
        return $backup
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
        }
        if ($locked) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
    }
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

    $text = Get-StateText
    $disk = Get-DiskProfiles
    $registered = Get-RegisteredProfiles $text

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

    $oldValue = [int](Get-CounterMatch $text).Groups[1].Value

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

    $newText = Set-CounterInText $text $NewValue
    $backup = Write-LocalStateAtomically $newText $text

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

:ChromeMenu while ($true) {
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

            $manualValue = 0
            if ([int]::TryParse($inputValue, [ref]$manualValue) -and $manualValue -ge 1) {
                Set-Counter $manualValue
            }
            else {
                Write-Host "Invalid number." -ForegroundColor Red
            }

            Pause-Menu
        }

        "4" {
            Start-Process explorer.exe -ArgumentList ('"{0}"' -f $UserData)
        }

        "Q" {
            break ChromeMenu
        }

        default {
            Write-Host "Unknown option." -ForegroundColor Yellow
            Start-Sleep -Milliseconds 700
        }
    }
}
