# TaskSchedulerMigration

PowerShell utility for migrating scheduled tasks from an old user SID to a new user/account string.

The script scans scheduled tasks, finds matching `Principal.UserId` or trigger `UserId` values, exports the task XML, replaces the old SID, and re-registers the task.

## Requirements

- Windows PowerShell with the ScheduledTasks module.
- Administrator rights are usually required to read and re-register all tasks.
- A known old SID and target user/account string accepted by Task Scheduler.

## Run

```powershell
powershell -ExecutionPolicy Bypass -File .\TaskSchedulerMigration.ps1 -OldSID "S-1-5-21-..." -NewUser "DOMAIN\User"
```

For local users, `-NewUser` can be a local account name accepted by Task Scheduler:

```powershell
powershell -ExecutionPolicy Bypass -File .\TaskSchedulerMigration.ps1 -OldSID "S-1-5-21-..." -NewUser ".\User"
```

Limit the scan to one scheduled-task folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\TaskSchedulerMigration.ps1 -OldSID "S-1-5-21-..." -NewUser "DOMAIN\User" -TaskPath "\SomePath\"
```

Preview changes:

```powershell
powershell -ExecutionPolicy Bypass -File .\TaskSchedulerMigration.ps1 -OldSID "S-1-5-21-..." -NewUser "DOMAIN\User" -WhatIf
```

## Parameters

- `-OldSID`: SID text to replace. Required.
- `-NewUser`: replacement user/account value. Required.
- `-BackupDirectory`: folder for exported task XML backups. Default: `.\TaskSchedulerMigrationBackup`.
- `-TaskPath`: optional scheduled-task folder filter.
- `-WhatIf`: preview re-registration without changing tasks.
- `-Confirm`: prompt before each matching task is re-registered.

## Safety Notes

- Matching tasks are exported to the backup directory before they are changed.
- The script uses `Register-ScheduledTask -Force`, so matching tasks are overwritten with updated XML.
- Only exact old-SID text matches in task XML are replaced.
- Review console output after running; failures are printed per task.
- Consider exporting important tasks manually before bulk migration.

## Generated Files

- XML backups under `.\TaskSchedulerMigrationBackup` unless `-BackupDirectory` is changed.
