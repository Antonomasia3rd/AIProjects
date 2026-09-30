# TaskSchedulerMigration

C# utility for migrating scheduled tasks from an old user SID to a new user/account string.

The utility scans scheduled tasks through the Task Scheduler COM API, finds matching `Principal.UserId` or trigger `UserId` values, exports the task XML, replaces the old SID, and re-registers the task while preserving its supported logon type. Registration triggers are suppressed during that re-registration.

## Requirements

- Windows Task Scheduler service.
- Administrator rights are usually required to read and re-register all tasks.
- A known old SID and target user/account string accepted by Task Scheduler.

## Build

From this folder:

```cmd
BuildTaskSchedulerMigration.cmd
```

Output:

```text
build\TaskSchedulerMigration.exe
```

`TaskSchedulerMigration.cs` is intentionally only the product assembly-metadata
overlay. The product-owned implementation is compiled from
`dependencies\TaskSchedulerMigration\task_scheduler_migration_app.cs`; the
project-local test file continues to exercise that implementation directly. It
uses the shared managed INI and named-object dependencies for profile storage.

The build also compiles and runs `TaskSchedulerMigrationLocalTests.cs`. To run those source and unit guardrails directly:

```cmd
TestTaskSchedulerMigration.cmd
```

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

## Persistent Profile

Direct commands retain the existing behavior: supplying `-OldSID` and
`-NewUser` runs the requested migration unless `-WhatIf` is supplied. A profile
is explicit through `--ini`; its first generated settings default to
`WhatIf=1` and `Confirm=1`, so reviewing a saved migration does not immediately
re-register tasks.

Create or update a profile without opening Task Scheduler:

```cmd
TaskSchedulerMigration.cmd --ini "D:\Profiles\account-move.ini" --set Settings.OldSID="S-1-5-21-..." --set Settings.NewUser="DOMAIN\User" --configure-only
TaskSchedulerMigration.cmd --ini "D:\Profiles\account-move.ini" --show-config
```

After reviewing the output, run the profile in preview mode with `--ini`. To
apply it for one run, add `--apply`; `--no-confirm` skips per-task prompts only
for that invocation. To make those choices persistent, set `Settings.WhatIf=0`
or `Settings.Confirm=0` with `--configure-only`.

`--set` is intentionally rejected unless `--configure-only` is also present.
Changing a profile therefore cannot also begin a migration.

## Parameters

- `-OldSID`: SID text to replace. Required.
- `-NewUser`: replacement user/account value. Required.
- `-BackupDirectory`: folder for exported task XML backups. Default: `TaskSchedulerMigrationBackup` beside the compiled helper executable. Relative paths resolve from the helper directory.
- `-TaskPath`: optional scheduled-task folder filter.
- `-IncludeCredentialSensitiveTasks`: opt in to S4U tasks after reviewing their restricted-token behavior. `Password` and `InteractiveOrPassword` tasks remain blocked because Task Scheduler does not expose the stored password, so this utility cannot preserve their credentials safely.
- `-WhatIf`: preview re-registration without changing tasks.
- `-Confirm`: prompt before each matching task is re-registered.
- `--apply` / `--no-what-if`: allow task registration for the current invocation.
- `--no-confirm`: skip per-task prompts for the current invocation.
- `--ini`: use a persistent profile. Relative paths resolve from the helper executable; paths stored inside the profile resolve from the profile directory.
- `--set Settings.Key=Value`: persist `OldSID`, `NewUser`, `BackupDirectory`, `TaskPath`, `IncludeCredentialSensitiveTasks`, `WhatIf`, or `Confirm`. Requires `--configure-only`.
- `--configure-only`: create, validate, and optionally update a profile without Task Scheduler access.
- `--show-config`: print effective profile values without Task Scheduler access.
- `--help`: print usage and exit before strict parsing, filesystem access, or Task Scheduler access.
- `--version`: print the executable version and exit before strict parsing, filesystem access, or Task Scheduler access.

## Safety Notes

- Matching tasks are exported to the backup directory before they are changed.
  Each backup is written to a same-directory temporary file and atomically
  renamed into a distinct filename; an existing backup is never overwritten.
- The utility uses Task Scheduler COM registration, so matching tasks are overwritten with updated XML.
- The existing task logon type is passed back to Task Scheduler; it is never replaced with `TASK_LOGON_NONE` as a generic fallback.
- Task registration uses `TASK_IGNORE_REGISTRATION_TRIGGERS`, so registration triggers do not run merely because the migration updates a task.
- `Password` and `InteractiveOrPassword` tasks are always reported and skipped because their stored password cannot be recovered. S4U tasks require `-IncludeCredentialSensitiveTasks`; other known non-password logon types are preserved automatically.
- Tasks whose logon type cannot be inspected are skipped instead of being re-registered with an assumed logon mode.
- Only exact old-SID text matches in task XML are replaced.
- Task XML is parsed with DTDs and external entity resolution disabled.
- Inaccessible folders/items are reported while the scan continues through other folders. Any partial enumeration, uninspectable task, or failed update returns exit code `3`.
- Review console output after running; failures are printed per task or folder.
- Consider exporting important tasks manually before bulk migration.

## Generated Files

- XML backups under `TaskSchedulerMigrationBackup` beside the compiled helper executable unless `-BackupDirectory` is changed.
- `TaskSchedulerMigration.ini` beside the compiled helper executable when using the default profile path, or the file supplied through `--ini`.
