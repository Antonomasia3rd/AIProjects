# TaskSchedulerMigration

C# utility for migrating scheduled tasks from an old user SID to a new user/account string.

The utility scans scheduled tasks through the Task Scheduler COM API, finds matching `Principal.UserId` or trigger `UserId` values, exports the task XML, replaces the old SID, and re-registers the task.

## Requirements

- Windows Task Scheduler service.
- Administrator rights are usually required to read and re-register all tasks.
- A known old SID and target user/account string accepted by Task Scheduler.

## Run

```cmd
TaskSchedulerMigration.cmd -OldSID "S-1-5-21-..." -NewUser "DOMAIN\User"
```

For local users, `-NewUser` can be a local account name accepted by Task Scheduler:

```cmd
TaskSchedulerMigration.cmd -OldSID "S-1-5-21-..." -NewUser ".\User"
```

Limit the scan to one scheduled-task folder:

```cmd
TaskSchedulerMigration.cmd -OldSID "S-1-5-21-..." -NewUser "DOMAIN\User" -TaskPath "\SomePath\"
```

Preview changes:

```cmd
TaskSchedulerMigration.cmd -OldSID "S-1-5-21-..." -NewUser "DOMAIN\User" -WhatIf
```

## Parameters

- `-OldSID`: SID text to replace. Required.
- `-NewUser`: replacement user/account value. Required.
- `-BackupDirectory`: folder for exported task XML backups. Default: `TaskSchedulerMigrationBackup` beside the compiled helper executable. Relative paths resolve from the helper directory.
- `-TaskPath`: optional scheduled-task folder filter.
- `-IncludeCredentialSensitiveTasks`: also attempt tasks with `Password`, `S4U`, or `InteractiveOrPassword` logon types. These are skipped by default because XML-only re-registration can require credentials or change logon behavior.
- `-WhatIf`: preview re-registration without changing tasks.
- `-Confirm`: prompt before each matching task is re-registered.

## Safety Notes

- Matching tasks are exported to the backup directory before they are changed.
- The utility uses Task Scheduler COM registration, so matching tasks are overwritten with updated XML.
- Tasks with credential-sensitive logon types are reported and skipped unless `-IncludeCredentialSensitiveTasks` is supplied.
- Only exact old-SID text matches in task XML are replaced.
- Review console output after running; failures are printed per task.
- Consider exporting important tasks manually before bulk migration.

## Generated Files

- XML backups under `TaskSchedulerMigrationBackup` beside the compiled helper executable unless `-BackupDirectory` is changed.
