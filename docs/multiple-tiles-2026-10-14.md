# Separate DesktopStub tiles

Six separate current-user apps were prepared and registered on 2026-10-14.
They reuse the same DesktopStub source/binary, with different executable names,
package identities, settings, image folders and resident scopes. There are no
standalone donor programs or new provider DLLs.

| Start app name | Runtime folder below DesktopStub/build/Tiles | Content |
| --- | --- | --- |
| Now playing | smtc | SMTC title, artist and session status |
| BBC Technology | rss | Five headlines rotated by Windows; fetch every 15 minutes |
| Genshin Impact | genshin | Resin summary; account setup pending |
| Honkai Star Rail | hsr | Stamina summary; account setup pending |
| Zenless Zone Zero | zzz | Charge summary; account setup pending |
| Discord preview | discord | Configured presence preview; sending disabled |

Each app has its own named INI next to its executable, for example
`smtc/AipSmtcTile.ini`. The old wallpaper/SMTC profile was backed up to
`DesktopStub/build/BeforeSeparateTiles-20261014-183547.ini`. Its SMTC entry is now
disabled and cycling is off: the original DesktopStub tile shows wallpaper only.
The original resident was not stopped by this setup.

## Pin and use the apps

Open Start, find each name in the app list/search, right-click it and choose
**Pin to Start**. This pins the registered app, not another copy of the original
Desktop tile. Resize Now playing/RSS/game tiles to Wide or Large if more text is
needed. Turn Live Tile on through the tile's Windows context menu if it is off.

All six apps are running and recorded successful Live Tile updates. Pinning is
still a manual step: both browser and Windows computer-use controls failed to
initialize in this session. No pinning dialog was started or left unattended.

The app tray icons have distinct hover names. Right-click the corresponding icon
to edit its sources. Sign-in launch is initially off. Each app exposes the normal
**Application > Run at sign-in (Startup folder)** option; it uses the current
user's shell:startup folder. The separate Windows startup task option remains
available for its packaged default profile.

## Accounts and Discord

Game account setup was deferred at the user's request. The HoYoLAB browser is
signed in according to the user, but the agent browser bridge remained
unavailable; no authenticated cookies were read or copied. Game tiles show an
account-configuration status until their own game UID and the LToken/LTUid cookie
values are entered/imported through Sources and accounts. Genshin, HSR and ZZZ
use different game UIDs; LTUid is the HoYoLAB account identifier, not a game UID.

**Plain cookies are stored in readable INI files. Anyone who can read those
files may be able to use the account.** Keep profiles, cookie exports, backups
and dumps private and out of shared uploads. These runtime folders are ignored
by Git. Existing protected-cookie imports are supported; this setup adds no
mandatory encryption or automatic browser credential extraction.

Discord's tile previews what DesktopStub is configured to send. Set Details and
State under Sources and accounts. Sending is disabled, and no Discord connection
was attempted. The normal IPC transport needs an installed/running Discord
client and an application ClientId before sending can work. Reading another
program's current Rich Presence is not implemented.

The RSS feed is `https://feeds.bbci.co.uk/news/technology/rss.xml`, verified as
RSS with 21 items during setup. The tile selects the first five. BBC's official
[feed information](https://support.bbc.co.uk/platform/feeds/NewsFeeds.htm)
describes its news feeds.

## Multiple tiles versus queued contents

Windows also exposes a secondary-tile API for several tiles under one app.
DesktopStub currently creates a primary tile per registered app. Its five-item
notification queue rotates contents within one tile; it does not create five
separate tiles. The named-host method above uses existing behavior and avoids
introducing a new renderer/router during this setup. A shared process with
secondary tiles remains future work; Microsoft's
[desktop secondary-tile guide](https://learn.microsoft.com/en-us/windows/apps/design/shell/tiles-and-notifications/secondary-tiles-desktop-pinning)
also requires a window owner for the pin-confirmation dialog.

## CapsBlink and wallpaper fallback

The main profile has CapsBlink hardware control enabled, but its contents did not
select CapsBlink. The controller therefore never started. Choose **Caps
indicator pattern / CapsBlink** under a content's Text sources. CapsLock is a
different source that only displays the logical key state. CapsBlink blinks
while logical Caps Lock is off and stays lit while it is on. For continuous
control, use a noncycling content or select it on every active cycle item.
Hardware actions require app-managed rotation. Native keyboard access and
permissions remain untested; no LED/device operation was performed during setup.

Wallpaper selects the ordinary desktop wallpaper without window capture.
LiveWallpaper tries the existing generic live-window capture when enabled;
there is no required Wallpaper Engine installation. If no usable live snapshot
is available, it falls back to ordinary wallpaper after any configured startup
settling. Live capture is off by default. When Windows has no wallpaper image
(for example a solid-color desktop), choose None/Transparent explicitly rather
than expecting an image path.

## Validation and remaining work

The single-content SMTC host now uses media event subscriptions whenever direct
Live Tile delivery is available, independently of queue eligibility. This fixes
an unnecessary polling path for the separate Now playing tile. The injected
host regression suite passes 9,148 checks; normal host/broker and all copied
hosts scanned clean before execution.

Preparation is reproducible with `tools/PrepareDesktopStubTiles.ps1` (offline
only, preserves existing profiles). `tools/RegisterDesktopStubTile.ps1 -Key
smtc` intentionally registers one prepared app for the current user; run it with
Windows PowerShell 5.1. Six package registrations, Start app entries and normal
live update logs were verified. A short passive sample measured individual
residents around 0.47-1.56% of one CPU core and NPSMSvc at zero measured CPU;
this is not sustained stability proof. The user's normal-CPU observation after
17 hours is recorded as user acceptance evidence.

Real HoYoLAB endpoint/account compatibility, native Caps/ASUS hardware behavior,
older-Windows helper queues, exact target-OS presentation and the historical
four-pixel raster variation remain open. Windhawk and the user's untracked
Discord bot work were not changed. No Startup entry or security setting was
changed by this setup.
