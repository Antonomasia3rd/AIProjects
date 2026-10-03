# YouTubeMusicMigrate

Local PowerShell tooling for inspecting and tidying a YouTube Music library and
playlist layout. `youtube_music_tidy.ps1` is the main entry point. The cleanup script
previews archiving selected legacy state and top-level reports; `-Apply` moves
those selected files into timestamped subdirectories. It does not remove the
working directory, authentication headers or active caches.

Both entries load their implementation from `dependencies/YouTubeMusicMigrate`
at the repository root. Keep that folder when copying the tool. The main entry
declares the existing command-line options and passes its data directory and
explicit arguments to the shared engine; default auth/config/cache/report
locations remain beside this entry point. Existing custom path arguments keep
their meaning. The shared engine is not a standalone launcher.

This is an intermediate consolidation step. Configuration is still through
PowerShell parameters and the existing JSON files; INI, tray and Startup controls
have not been implemented for this product. It is not a DesktopStub source.

Browser-auth headers, tokens, cached library data, generated reports, and
personal playlist state are intentionally ignored. Create those files locally
when running the tool; do not commit or share them.

Authentication headers can grant access to your account. They are currently
stored as plaintext; this tool has no opt-in encryption setting yet. Keep the
folder private, avoid shared/synced locations, and revoke the browser session
if the headers are exposed. Account-changing commands still require their
existing review/confirmation flow.

`tools/YouTubeMusicMigrateWrapperTests.ps1` runs 50 inert assertions in a
temporary directory, replacing both engines with stubs. It checks default and
custom paths, explicit empty/false options, script scope and error propagation.
The real engines are only parsed. It requires PowerShell 5.1 or PowerShell 7;
CMake/CTest registers it when PowerShell is available. These checks do not
validate live YouTube API behavior.
