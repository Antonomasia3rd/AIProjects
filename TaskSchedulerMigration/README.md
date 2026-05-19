# TaskSchedulerMigration

PowerShell utility for migrating scheduled tasks from an old user SID to a new user/account string.

The script scans all scheduled tasks, finds matching `Principal.UserId` or trigger `UserId` values, exports the task XML, replaces the old SID, and re-registers the task.

## Requirements

- Windows PowerShell with ScheduledTasks module.
- Administrator rights are usually required to read and re-register all tasks.
- A known old SID and target user name/account.

## Run

```powershell
powershell -ExecutionPolicy Bypass -File .\TaskSchedulerMigration.ps1 -OldSID "S-1-5-21-..." -NewUser "DOMAIN\User"
```

For local users, `-NewUser` can be a local account name accepted by Task Scheduler, such as:

```powershell
powershell -ExecutionPolicy Bypass -File .\TaskSchedulerMigration.ps1 -OldSID "S-1-5-21-..." -NewUser ".\User"
```

## Notes

- The script uses `Register-ScheduledTask -Force`, so matching tasks are overwritten with updated XML.
- Review console output after running; failures are printed per task.
- Consider exporting important tasks manually before bulk migration.
