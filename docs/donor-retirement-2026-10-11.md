# Native source donor retirement — 2026-10-11

The tracked standalone source/build wrappers for NowPlayingTile,
RealTimeNotesDeskband, capsblink and asusblink have been removed after the native
DesktopStub integration checkpoint `e6e7b0f`. Shared data providers, native
hardware engines, profile importers, host configuration/actions and their inert
fixtures remain. The optional NowPlaying widget and Explorer deskband toolbar
are outside the requested replacement surface.

This is a repository source retirement. It does not uninstall an existing EXE,
DLL, registered deskband, AppX package, StartupTask or Startup-folder shortcut.
No unregister, package, Startup, device or account operation was performed.
Existing installed/running programs may still use their previous configuration.

## Personal files and history

Only the reviewed, explicitly enumerated 32 tracked files listed below were
passed to `git rm`. Each resolved source path was checked to be a file inside
the checkout first. No folder purge or ignored-file cleanup was used.

The 32 ignored personal/generated files present within the four donor folders
were checked before and after removal for existence, length and write time;
those values remained unchanged. Their contents were not read for this check.
This includes the repository's preservation rules for personal `*.ini`, every
`build/` directory, deskband `*.dll` and `*_cookie.json` files. Removing tracked
wrappers does not remove those personal profiles or compiled binaries.

The donor source remains recoverable from Git history, including `e6e7b0f`.
Build/CI/catalog references are coordinated with this retirement; historical
audit names and compatibility metadata do not imply a remaining standalone
build target.

## Migration to DesktopStub

Use a separate target DesktopStub INI when importing an old profile. A donor
source cannot be the destination INI. Relative source paths resolve against the
target INI's directory. These commands default to configuration-only and save
local settings without starting a resident, querying media, contacting an
account, accessing a hardware device or registering a package:

```powershell
DesktopStub.exe --ini DesktopStub.ini --now-playing-import-profile OldNowPlaying.ini
DesktopStub.exe --ini DesktopStub.ini --notes-import-profile OldNotes.ini
DesktopStub.exe --ini DesktopStub.ini --notes-import-cookie Resin=GenshinCookie.json
DesktopStub.exe --ini DesktopStub.ini --caps-import-profile OldCapsBlink.ini
DesktopStub.exe --ini DesktopStub.ini --asus-import-profile OldAsusBlink.ini
```

Equivalent import actions are available from DesktopStub's content/source menu.
Explicit `--set` choices are folded after imported settings. Imported files are
read without repair or mutation; malformed source data returns an error before
publishing a partial imported batch.

NowPlaying profiles preserve Text, Artwork, Combined or Cycle layouts through
`Content` / `Content.N`. SMTC metadata and artwork share one immutable media
snapshot. Missing artwork falls back to text/status over the existing solid
background. The source polling interval maps to `Content.RefreshSeconds`.
The old `TileRefreshSeconds` preference described notification republishing and
has no exact host equivalent; it is retained in import metadata and diagnosed,
not converted into source refresh or `Content.CycleSeconds`. Cycle uses the
host's existing cycle interval. Its global text mode is reported when a donor
cycle cannot reproduce per-variant native/overlay switching exactly.

NowPlaying tray/menu preferences map where an exact host preference exists.
The donor `RunAtStartup` value is visible in import metadata and diagnostics,
but is deferred. Import preserves the current DesktopStub Startup preference.
Apply a Startup choice explicitly through DesktopStub's Run at sign-in control
or a separate `--startup` / `--no-startup` command. Unsupported widget font,
color, image and geometry fields are diagnosed instead of assigned invented
host keys.

Notes retains the shared resource/detail data model, account/profile and cookie
imports, credential clear and existing-resident refresh actions. The account
resources remain Resin, Stamina and Charge. Saved protected credentials retain
the existing legacy UTF-16LE DPAPI decoding support. Explicit plain/protected
CLI credential choices replace the alternate aliases in their own family; the
imports do not decrypt or test credentials. Starting an enabled configured Notes
source or choosing Refresh is a separate data-fetch action.

Caps imports preserve keyboard target and blink interval. ASUS imports preserve
the native pattern fields, numbered mic/keyboard/HDD events and supported legacy
aliases. ASUS import removes obsolete recognized inline events and selects the
imported inline configuration atomically. Hardware activation, disk sampling,
pause and unrelated host settings remain independently controlled. The native
hardware backends remain available; their actions default off. Imports perform
no device writes or firmware calls.

## Surviving shared implementation

The following remain in the source tree:

- `dependencies/content_sources/smtc.inc`, `smtc_artwork.inc` and
  `now_playing_profile.inc`.
- `dependencies/content_sources/notes.inc`, `notes_model.h` and
  `notes_profile.inc`, plus `dependencies/http_fetch.inc` and `dpapi.inc`.
- `dependencies/content_engine.h` and the native DesktopStub content/runtime,
  CLI, tray and offline configuration fragments.
- `dependencies/hardware/hardware_profile_import.inc`, the native Caps engine
  and Windows backend, and `dependencies/asusblink/pattern.h`, `protocol.h`,
  `service.h`, `service.cpp` and `native_backend.cpp`.
- Shared native/managed baseline infrastructure used by the remaining products,
  all promoted native source/profile tests, and the host runtime/menu fixtures.

The retired donor-only artwork fixture tested executable-directory JPEG
notification artifacts and cleanup in the old NowPlaying adapter. Shared
bounded artwork decoding, temporary snapshot leases, fallback and host
composition remain covered by the SMTC and native host fixtures.

## Validation boundary

Before source removal, the frozen native checkpoint passed 111 inert NowPlaying
profile checks, 208 host runtime/menu checks with zero failures, and 18 passive
NowPlaying CLI cases. Each executed binary received a clean Defender custom scan.
The importer compiled with `/W4 /WX`; the host used its existing `/W4` build.
The host fixture retained its 45-second self-deadline and runner stages were
bounded to 60 seconds. See
[the NowPlaying validation record](../tools/NowPlayingProfileImportValidation-2026-10-11.md)
for the exact hashes and logs.

Those fixtures used synthetic profiles/accounts/media and inert hardware inputs.
They do not establish real driver/firmware behavior, real account access,
visible Live Tile timing, or installation/removal of an existing system
integration. Post-retirement aggregate build/catalog checks belong to the
coordinated retirement checkpoint.

The managed legacy utility fixture no longer compiles or inspects the retired
Caps/ASUS app bodies. DNS resident settings transactions, managed INI dialect and
publication, logging, named-object scoping, tooltip behavior, shared Startup
policy/rollback, PhotoCollage and TaskSchedulerMigration checks remain. The
shared tooltip and Startup-folder policy assertions formerly grouped with the
ASUS app checks were retained independently. Native hardware engine and host
fixtures remain separate and unchanged.

After the managed/native common-lock baseline froze,
`tools/TestLegacyUtilities.cmd --compile-only` compiled the reduced fixture with
the existing C# `/warn:4` setting and executed nothing. A fresh Defender custom
scan reported no threats, and the resulting binary passed a 60-second-bounded
run with only the repository-root argument. Exact result:
`Legacy utility tests passed: 216 scoped checks; real Startup integration disabled.`
Injected Startup transaction tests remained enabled; the real Startup-folder
branch was explicitly skipped. The deliberate persistence/rollback/corrupt-image
fixtures produced their expected diagnostics and the process returned zero.

The scanned `build/LegacyUtilitiesTests.exe` SHA-256 was
`A01B605DC4FA057CB478DB8F5CA655A672F9CB64B1745A8B6E23098BF5CDD83E`.
Evidence is in `build/review-validation/retired-legacy-utilities-build.log`,
`retired-legacy-utilities-defender.log`,
`retired-legacy-utilities-scan.sha256` and
`retired-legacy-utilities-tests.log`. The recipe's default behavior still runs
its managed utility and configuration suites; `--compile-only` provides the
explicit build-only path used for this scan-gated review.

## Exact tracked source removal

```text
dependencies/NowPlayingTile/npt_app.inc
dependencies/NowPlayingTile/npt_command_line.inc
dependencies/NowPlayingTile/npt_config_defaults.inc
dependencies/NowPlayingTile/npt_core.inc
dependencies/NowPlayingTile/npt_live_tile.inc
dependencies/NowPlayingTile/npt_manifest.inc
dependencies/NowPlayingTile/npt_media.inc
dependencies/NowPlayingTile/npt_tray.inc
dependencies/NowPlayingTile/npt_widget.inc
dependencies/RealTimeNotesDeskband/deskband_app.inc
dependencies/asusblink/asusblink_app.cs
dependencies/capsblink/capsblink_app.cs
legacy/NowPlayingTile/BuildNowPlayingTile.cmd
legacy/NowPlayingTile/NowPlayingTile.cpp
legacy/NowPlayingTile/README.md
legacy/NowPlayingTile/TestNowPlayingTileSource.cmd
legacy/NowPlayingTile/tools/NowPlayingArtworkTests.cpp
legacy/NowPlayingTile/tools/NowPlayingTileSourceCheck.cpp
legacy/RealTimeNotesDeskband/BuildDeskband.cmd
legacy/RealTimeNotesDeskband/ConfigureDeskband.cmd
legacy/RealTimeNotesDeskband/README.md
legacy/RealTimeNotesDeskband/RealTimeNotesDeskband.cpp
legacy/RealTimeNotesDeskband/RegisterDeskband.cmd
legacy/RealTimeNotesDeskband/TestRealTimeNotesDeskbandSource.cmd
legacy/RealTimeNotesDeskband/UnregisterDeskband.cmd
legacy/RealTimeNotesDeskband/tools/RealTimeNotesDeskbandSourceCheck.cpp
legacy/asusblink/BuildAsusBlink.cmd
legacy/asusblink/README.md
legacy/asusblink/asusblink.cs
legacy/capsblink/BuildCapsBlink.cmd
legacy/capsblink/README.md
legacy/capsblink/capsblink.cs
```
