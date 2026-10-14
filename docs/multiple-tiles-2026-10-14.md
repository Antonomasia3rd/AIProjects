# Multiple tiles from one DesktopStub

DesktopStub manages its primary wallpaper tile and six source tiles from one
resident process, one tray icon, and one INI. Each source uses a Windows secondary
tile under the existing DesktopStub package. The six copied-host packages from
the initial 2026-10-14 setup have been retired on this laptop. Their private INIs
and backups remain under DesktopStub/build/Tiles for recovery.

## Set up and pin

1. Register/run DesktopStub in Live Tile mode. Open its tray menu and choose
   **Tiles > Set up and enable the six source tiles**.
2. Open **Tiles > Now playing > Pin**, confirm the Windows prompt, and repeat for
   RSS, Genshin Impact, Honkai: Star Rail, Zenless Zone Zero, and Discord.
   The action may be labelled pin/update for an existing tile.
3. Configure each source through its submenu. Resize RSS to Wide or Large for
   more text. A Large RSS tile displays up to five headline/summary rows; Medium
   and Wide display up to three headlines. Windows decides the final visual
   sizing and truncation.
4. Use DesktopStub's single **Application** Startup setting for all tiles.
   Legacy Startup uses the current user's shell:startup folder; packaged Startup
   uses the existing Windows startup-task option.

Content 1 remains the primary tile. The source tiles default to Content 2–7.
Enabling this layout turns primary cycling off. Source tiles update when their
composed content changes; unpinned destinations are skipped. The existing Live
Tile broker is an occasional isolated helper, not another resident tray app.

The initializer fills missing fields and preserves existing source settings,
content fields and credentials. Review mappings if your profile already uses
Content 2–7 for other layouts. The main INI owns all source/account settings.

## Source setup and clicks

Now playing uses Windows media sessions. It needs a player that publishes SMTC.
RSS was configured here with BBC Technology:
https://feeds.bbci.co.uk/news/technology/rss.xml, refreshed every 15 minutes.
A configured RSS tile opens the first article. A missing feed opens source setup.

Game tiles display setup status until the appropriate game UID and HoYoLAB
credentials are configured. Clicking a game configuration-error tile opens embedded HoYoLAB sign-in. Use its **Source and sign-in settings** tray
submenu to enter/import the resulting account settings. Use the embedded window's Use signed-in account button after entering your game UID. External browser sign-in alone does not import credentials. Genshin, HSR and ZZZ have separate
game UIDs; LTUid identifies the HoYoLAB account. Account setup on this laptop
remains deferred. No browser cookies were extracted.

**Plain credentials are readable in the INI. Anyone who can read it may be able
to use the account.** Keep profiles, cookie exports, backups and crash dumps
private. Existing opt-in protected-cookie routes remain available; encryption
is not mandatory. Masked input prevents casual display, not file access.

Discord displays DesktopStub's configured presence preview. Sending remains off
until explicitly enabled and requires a Discord client and application ClientId.
Reading an unrelated application's current Rich Presence is not implemented.

## Windows support and recovery

Windows controls the pin-confirmation prompt. Pinning requires an explicit
action for each source; the setup action does not silently create Start pins.
The native desktop secondary-tile path targets supported Windows 10/11 builds.
Its availability and Start presentation depend on Windows and any installed
Start replacement; this session has not yet verified an actual secondary pin.
Microsoft documents the
[desktop secondary-tile APIs](https://learn.microsoft.com/en-us/windows/apps/design/shell/tiles-and-notifications/secondary-tiles-desktop-pinning).

The old PrepareDesktopStubTiles.ps1 and RegisterDesktopStubTile.ps1 scripts are
compatibility tools requiring an explicit LegacyMultipleHosts switch. New setup
uses the Tiles menu. Do not use those scripts for one-process operation.

The installed binary and INI backup from this migration is
DesktopStub/build/ReviewBackupOct14OneHost-20261014-194634.
Six old source INIs were also backed up under
DesktopStub/build/Tiles/BeforeOneHost-20261014-190621.
These folders are private, ignored runtime files; they are not committed.

## Caps indicator and wallpaper fallback

CapsBlink HardwareEnabled now runs independently of tile text. Select CapsBlink as a text source only if you want its status on a tile. Auto targets available detected keyboard endpoints; explicit class targets remain supported. CapsLock only displays logical state. CapsBlink blinks while Caps Lock is off and stays lit while it is on.
Native LED/device behavior remains untested here.

Wallpaper uses the ordinary desktop image. LiveWallpaper attempts the existing
generic live-window capture when enabled; it does not require Wallpaper Engine.
If no usable live capture is available, it falls back to ordinary wallpaper after
configured startup settling. For a solid-color desktop without an image path,
choose None/Transparent explicitly.
Now playing clicks toggle playback by default. Tiles > Quick Actions configures the click action, volume step, and optional individual action tiles. Use Pin/update on existing source tiles to install their topic logos. See tile-actions-validation-2026-10-14.md for behavior and acceptance limits.
