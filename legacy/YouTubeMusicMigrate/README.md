# YouTubeMusicMigrate

The compiled application provides shared INI, CLI, configuration tray, and
per-user Startup controls for the existing YouTube Music worker. Run
`BuildYouTubeMusicMigrate.cmd`; the executable and required worker scripts are
copied into `build`. Keep that bundle together when moving it. There are no new
plugin DLLs. The existing PowerShell 5.1 API engine remains shared source because
rewriting its resumable account operations is a separate feasibility task.

The cleanup script
previews archiving selected legacy state and top-level reports; `-Apply` moves
those selected files into timestamped subdirectories. It does not remove the
working directory, authentication headers or active caches.

All implementation lives in `dependencies/YouTubeMusicMigrate`. The optional
`youtube_music_tidy.ps1` compatibility entry retains all 47 original parameters
and adds `-IniFile`. It forwards typed, explicitly supplied values to the
compiled frontend. Its no-argument invocation retains the original console
menu/setup behavior. The compiled executable's no-argument invocation opens
configuration only. Custom legacy relative path arguments retain their working
directory meaning; defaults remain in the original data directory.

```powershell
.\build\YouTubeMusicMigrate.exe --help
.\build\YouTubeMusicMigrate.exe --ini .\personal.ini --configure-only --set 'DataRoot=D:\Music Tidy' --set BatchSize=20
.\build\YouTubeMusicMigrate.exe --ini .\personal.ini --show-config
.\build\YouTubeMusicMigrate.exe --ini .\personal.ini --tray
.\build\YouTubeMusicMigrate.exe --ini .\personal.ini --startup
.\build\YouTubeMusicMigrate.exe --ini .\personal.ini --no-startup
.\build\YouTubeMusicMigrate.exe --ini .\personal.ini --menu
.\build\YouTubeMusicMigrate.exe --ini .\personal.ini --run --arg ReportOnly=1
```

All 47 worker parameters are editable in `[Settings]`, through `--set`, and
through the shared tray editor. Arrays use JSON, for example
`AdditionalNonMusicPlaylistIds=["playlist-a","playlist-b"]`. `--arg` overrides
one explicit worker launch without saving it. Effective defaults are kept
separate from explicitly supplied arguments; false switches and empty identity
overrides retain their meaning. `TraceRequests=1` matches the original default
behavior; `TraceRequests=0` now consistently disables tracing. `QuietRequests=1`
takes precedence and suppresses request lines regardless of `TraceRequests`.
`--menu` refuses `NoInteractive=1`; override `NoInteractive=0` for that launch.

The additional settings are `DataRoot`, `RunAtStartup` (default `0`), and
`ImportLegacyIdentity` (default `1`). Relative saved file paths use `DataRoot`;
a relative `DataRoot` uses the INI directory. The default INI is
`YouTubeMusicMigrate.ini` in the original product/data directory in a checkout,
or beside the executable in a flattened release. `--ini` may select another
file, using the working directory for a relative path. Each data/input/output
file must resolve to a distinct path to prevent reports or caches overwriting
authentication, state, or the INI.

Startup uses only the current user's Startup folder and launches
`--tray --ini <selected profile>`. Saving preferences, opening configuration,
help/version/inspection, and sign-in never launch the worker. After a manual
INI Startup edit, open or reload the tray to apply it. Offline
`--configure-only` rejects Startup changes; use the explicit Startup commands
or tray checkbox. Worker execution requires `--run`, `--menu`, or the deliberate
legacy script entry. This utility is not a DesktopStub source.

On the first explicit worker launch, `ImportLegacyIdentity=1` imports missing
account identity preferences from the selected legacy JSON configuration and
then becomes `0`. Passive configuration never reads that account-state file.
Set this preference to `0` to intentionally start with blank/new identity
preferences. The INI then owns editable identity; JSON retains setup timestamps
and runtime state. Confirmed setup changes go back through the frontend's
validated INI writer. Concurrent identity or data/config-path edits are
preserved and reported as a conflict. One-run identity overrides are not saved
merely because authentication timestamps change.

Only one cooperating worker per current user and `DataRoot` runs at a time;
the PowerShell worker owns that lock for its entire lifetime, including if the
frontend closes. Keep separate data directories for independent accounts. Do
not configure different data roots to share writable cache/state files.

Browser-auth headers, tokens, cached library data, generated reports, and
personal playlist state are intentionally ignored. Create those files locally
when running the tool; do not commit or share them.

Authentication headers can grant access to your account. They are currently
stored as plaintext; this tool has no opt-in encryption setting yet. Keep the
folder private, avoid shared/synced locations, and revoke the browser session
if the headers are exposed. Account-changing commands still require their
existing review/confirmation flow.

Repository-root `tools/BuildYouTubeMusicFrontendTests.cmd` compiles an inert
frontend fixture without running it; scan it before bounded local execution.
`tools/TestYouTubeMusicMigrate.cmd` is the CI entry. The PowerShell tests under
this product parse the real worker, substitute inert worker/host stubs, and
exercise the small local metadata writer in a temporary directory. They cover
all 47 parameter types, explicitness, identity writeback, and concurrent-worker
refusal under PowerShell 5.1 and PowerShell 7. CTest retains the portable script
checks. No live account, authentication header, clipboard, or API request is
needed. Actual shell tray/Startup integration and live API behavior remain
environmental acceptance checks.
