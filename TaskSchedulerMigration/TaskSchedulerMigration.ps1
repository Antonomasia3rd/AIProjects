$OldSID = "S-1-5-21-4166441615-362121406-1305389529-1001"
$NewUser = "ADDS\Amiya"

Write-Host "=== Task Migration START (SID → User) ===" -ForegroundColor Cyan
Write-Host "Old SID : $OldSID"
Write-Host "New User: $NewUser"
Write-Host ""

$Tasks = Get-ScheduledTask
$updatedCount = 0

foreach ($task in $Tasks) {
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

        # Export XML
        $xml = Export-ScheduledTask -TaskName $taskName -TaskPath $taskPath

        # Replace SID with new user
        $updatedXml = $xml -replace [regex]::Escape($OldSID), $NewUser

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
        Write-Host "[ERROR] Failed: $($task.TaskName)" -ForegroundColor Red
        Write-Host $_.Exception.Message
        Write-Host ""
    }
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Total updated tasks: $updatedCount" -ForegroundColor Green
Write-Host "=== Task Migration COMPLETE ==="