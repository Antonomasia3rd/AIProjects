# ChromeProfileCounter

Inspect and repair Chrome's local `profile.profiles_created` counter. The C#
application and repair engine live in `dependencies/ChromeProfileCounter`.
This product folder supplies metadata, an example profile, a build recipe,
and the compatibility PowerShell launcher.

Run `BuildChromeProfileCounter.cmd` to build `build\ChromeProfileCounter.exe`,
or use the compiled release. The build uses the installed .NET Framework
compiler. No downloaded libraries or plugin DLLs are required; the executable
runs independently of the repository and its source dependency folder.

```powershell
.\build\ChromeProfileCounter.exe --help
.\build\ChromeProfileCounter.exe --status
.\build\ChromeProfileCounter.exe --set-counter 8
.\build\ChromeProfileCounter.exe --auto-fix
```

Without an action, the application opens the familiar status, auto-fix, manual
counter, open-folder, and quit menu. `ChromeProfileCounter.ps1` forwards to the
same executable, preferring a release executable beside it, then `build\`.
The optional wrapper also needs `powershell_native_launcher.ps1` beside it or
under `build\`; the build recipe copies that shared helper for packaging.
The helper preserves Windows PowerShell argument quoting, including empty
arguments and spaced paths ending in a backslash. Direct EXE use needs neither
PowerShell file.
The old PowerShell runtime engine has been removed.

## Configuration and Startup

All saved settings are available through the INI, `--set`, and the shared
`--tray` settings editor. The default INI is `ChromeProfileCounter.ini` beside
the executable. `--ini` selects another profile; a relative `--ini` path uses
the current working directory. Help, version, and `--show-config` never read
Chrome data or create a profile.

```powershell
.\build\ChromeProfileCounter.exe --ini .\personal.ini --configure-only --set 'UserDataDirectory=D:\Browsers\Chrome' --set 'BackupDirectory=Counter Backups'
.\build\ChromeProfileCounter.exe --ini .\personal.ini --show-config
.\build\ChromeProfileCounter.exe --ini .\personal.ini --tray
.\build\ChromeProfileCounter.exe --ini .\personal.ini --startup
.\build\ChromeProfileCounter.exe --ini .\personal.ini --no-startup
```

| Setting | Default | Meaning |
| --- | --- | --- |
| `UserDataDirectory` | `%LOCALAPPDATA%\Google\Chrome\User Data` | Chrome profile root; a relative saved value uses the INI folder. |
| `BackupDirectory` | `Profile Counter Backups` | Exact replaced-file backups; a relative saved value uses `UserDataDirectory`. |
| `RunAtStartup` | `0` | Open the configuration tray at sign-in. |

Environment variables are expanded in configured paths. Transient `--user-data`
and `--backup-directory` path overrides use the working directory for relative
values and never change the saved profile. `--first-free` prints the first
available number without modifying Chrome state.

Startup uses only the current user's Startup folder. Its shortcut runs
`--tray --ini <selected profile>`, never a counter action. The tray's Startup
checkbox and CLI Startup commands share rollback-aware INI/shortcut updates.
After manually editing `RunAtStartup`, open or reload the tray to reconcile
the shortcut. Offline `--configure-only` deliberately rejects Startup edits;
use `--startup`, `--no-startup`, or the tray checkbox instead.

Opening the tray, signing in, and saving settings cannot repair Chrome data.
The tray is a configuration surface; run a deliberate CLI/menu action for
status or repair. Counter choices and confirmations are never saved in the INI.

## Repair behavior and limits

Close Chrome before repairing and keep it closed throughout. A repair displays
the current and proposed counters, rejects occupied on-disk or registered
numbers, and requires exact typed `YES`. An explicit `--yes` confirms only a
one-shot `--set-counter` or `--auto-fix` command; it cannot be combined with
Startup, tray, or configuration commands.

The engine changes only the selected counter's digit span. It checks strict
JSON syntax and the decoded property path, rejecting ambiguous or escaped
duplicate target keys. Invalid encodings and unreadable profile registrations
are rejected instead of becoming apparently safe free numbers.

The replacement uses a temporary file beside `Local State`, then `File.Replace`
to preserve the exact replaced file as a backup, including its encoding and
BOM. The new state is UTF-8 without BOM, matching the old utility's output.
UTF-8 and BOM-marked UTF-16/UTF-32 input are supported. A verification failure
after publication returns an error and retains the printed backup path.

This operation can damage Chrome profile selection if used with the wrong data
directory or while Chrome is changing it. Cooperating tool writers share a
mutex; Chrome does not. Final browser/file checks reduce the race but cannot
eliminate it. Atomic replacement also does not promise power-loss durability.
Keep an independent backup when the data matters. Stored settings and backups
are ordinary files; no encryption or additional opt-in security controls are
implemented by this utility. Protect their folders with your Windows account
permissions, and do not share backups containing personal browser state.

## Validation

Repository-root `tools\TestChromeProfileCounter.cmd` is the CI entry for
synthetic engine and adapter tests. For local staged validation, use
`tools\BuildChromeProfileCounterEngineTests.cmd` and
`tools\BuildChromeProfileCounterAppTests.cmd`, scan each compiled test first,
then execute it with an independent timeout. These recipes only compile.

Engine fixtures use generated files and injected browser observations. App
fixtures additionally substitute Startup, tray, folder launch, and interactive
input; they test configuration and actual repairs without touching a real
browser. The PowerShell checks under this folder's `tools` remain portable:
they parse/check source contracts and substitute inert process/helper objects
for argument quoting and wrapper forwarding tests. Real shell tray rendering, actual Startup-folder behavior,
and repair against a manually prepared disposable browser profile remain
environmental acceptance checks.
