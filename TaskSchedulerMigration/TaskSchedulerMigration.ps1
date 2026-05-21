[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [Parameter(Mandatory = $true)]
    [string]$OldSID,

    [Parameter(Mandatory = $true)]
    [string]$NewUser,

    [string]$BackupDirectory = ".\TaskSchedulerMigrationBackup",

    [string]$TaskPath,

    [switch]$IncludeCredentialSensitiveTasks
)

Write-Host "=== Task Migration START (SID -> User) ===" -ForegroundColor Cyan
Write-Host "Old SID : $OldSID"
Write-Host "New User: $NewUser"
Write-Host ""

function ConvertTo-SafeFileName {
    param([Parameter(Mandatory = $true)][string]$Name)

    $invalid = [IO.Path]::GetInvalidFileNameChars()
    $chars = foreach ($ch in $Name.ToCharArray()) {
        if ($invalid -contains $ch) { '_' } else { $ch }
    }
    -join $chars
}

function Update-TaskXmlUserId {
    param(
        [Parameter(Mandatory = $true)][string]$Xml,
        [Parameter(Mandatory = $true)][string]$From,
        [Parameter(Mandatory = $true)][string]$To
    )

    $doc = New-Object System.Xml.XmlDocument
    $doc.PreserveWhitespace = $true
    $doc.LoadXml($Xml)

    $changed = $false
    foreach ($node in $doc.GetElementsByTagName('UserId')) {
        if ($node.InnerText -eq $From) {
            $node.InnerText = $To
            $changed = $true
        }
    }

    if (-not $changed) {
        throw "Matched task XML did not contain a UserId node for $From"
    }

    return $doc.OuterXml
}

function Get-TaskLogonType {
    param([Parameter(Mandatory = $true)]$Task)

    if ($Task.Principal -and $null -ne $Task.Principal.LogonType) {
        return [string]$Task.Principal.LogonType
    }

    return ""
}

function Test-CredentialSensitiveLogonType {
    param([string]$LogonType)

    return @("Password", "S4U", "InteractiveOrPassword") -contains $LogonType
}

$Tasks = if ($TaskPath) {
    Get-ScheduledTask -TaskPath $TaskPath
} else {
    Get-ScheduledTask
}
$updatedCount = 0
$skippedCredentialCount = 0
$failedCount = 0

foreach ($task in $Tasks) {
    $fullName = $null
    try {
        $taskName = $task.TaskName
        $taskPath = $task.TaskPath
        $fullName = "$taskPath$taskName"

        $principal = $task.Principal
        $triggers = $task.Triggers

        $needsUpdate = $false

        # Check principal
        if ($principal.UserId -eq $OldSID) {
            Write-Host "[MATCH] Principal: $fullName" -ForegroundColor Yellow
            $needsUpdate = $true
        }

        # Check triggers (just in case)
        foreach ($trigger in $triggers) {
            if ($trigger.UserId -eq $OldSID) {
                Write-Host "[MATCH] Trigger: $fullName" -ForegroundColor Yellow
                $needsUpdate = $true
            }
        }

        if (-not $needsUpdate) {
            continue
        }

        $logonType = Get-TaskLogonType -Task $task
        if (Test-CredentialSensitiveLogonType -LogonType $logonType) {
            $message = "[SKIP] Credential-sensitive task: $fullName (LogonType=$logonType). Re-run with -IncludeCredentialSensitiveTasks only after confirming credentials/logon behavior."
            if (-not $IncludeCredentialSensitiveTasks) {
                Write-Host $message -ForegroundColor Magenta
                Write-Host ""
                $skippedCredentialCount++
                continue
            }

            Write-Host "[WARN] Updating credential-sensitive task: $fullName (LogonType=$logonType)" -ForegroundColor Magenta
        }

        $xml = Export-ScheduledTask -TaskName $taskName -TaskPath $taskPath
        $updatedXml = Update-TaskXmlUserId -Xml $xml -From $OldSID -To $NewUser

        if (-not $PSCmdlet.ShouldProcess($fullName, "Re-register scheduled task with matching UserId values changed to '$NewUser'")) {
            continue
        }

        $backupRoot = if ([IO.Path]::IsPathRooted($BackupDirectory)) {
            $BackupDirectory
        } else {
            Join-Path (Resolve-Path -LiteralPath ".") $BackupDirectory
        }
        New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
        $safeName = ConvertTo-SafeFileName -Name ($fullName.TrimStart('\') -replace '\\', '_')
        $backupPath = Join-Path $backupRoot "$safeName.xml"
        Set-Content -LiteralPath $backupPath -Value $xml -Encoding UTF8
        Write-Host "[BACKUP] $backupPath" -ForegroundColor DarkCyan

        Write-Host "[ACTION] Re-registering: $fullName" -ForegroundColor Cyan

        Register-ScheduledTask `
            -TaskName $taskName `
            -TaskPath $taskPath `
            -Xml $updatedXml `
            -Force | Out-Null

        Write-Host "[SUCCESS] Updated: $fullName" -ForegroundColor Green
        Write-Host ""

        $updatedCount++
    }
    catch {
        $failedCount++
        $displayName = if ($fullName) { $fullName } else { $task.TaskName }
        Write-Host "[ERROR] Failed: $displayName" -ForegroundColor Red
        Write-Host $_.Exception.Message
        Write-Host ""
    }
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Total updated tasks: $updatedCount" -ForegroundColor Green
Write-Host "Credential-sensitive tasks skipped: $skippedCredentialCount" -ForegroundColor Yellow
Write-Host "Failed tasks: $failedCount" -ForegroundColor $(if ($failedCount -eq 0) { "Green" } else { "Red" })
Write-Host "=== Task Migration COMPLETE ==="
