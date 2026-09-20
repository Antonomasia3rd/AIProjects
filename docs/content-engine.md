# DesktopStub content engine

DesktopStub can compose text and backgrounds through one renderer and delivery path. Enable **Content and sources > Use layered content** in the tray, or set `[Content] Enabled=1`. Existing wallpaper profiles keep their behavior until enabled. The old RSS preset now uses the same composition pipeline and places headlines over a wallpaper background; no separate RSS worker publishes competing notifications.

## Configuration

All keys below work in the INI, through `--set Section.Key=Value`, and through the **Content and sources** tray menu. Text delivery and backgrounds have choice submenus; text providers have checkboxes with an optional source-order editor. **Choose background image** saves the file and background type together. **Add content** adds an entry, and **Move earlier/later in cycle** changes its position while preserving its INI fields/comments. Reducing the number of contents preserves unused sections. Selecting a legacy Wallpaper/RSS preset turns off the numbered profile; its checkmark is suppressed while layered content is active.

| `[Content]` key | Default | Meaning |
|---|---|---|
| `Enabled` | `0` | Use numbered content entries. Boolean names such as `true` work too. |
| `Count` | `1` | Read `Content.1` through `Content.N`, from 1 to 32. |
| `CycleEnabled` | `0` | Rotate enabled entries; otherwise show the first enabled entry. |
| `CycleSeconds` | `10` | Time between entries, 1–86400 seconds. |
| `RefreshSeconds` | `2` | Background/SMTC refresh, 1–86400 seconds. RSS has its own fetch interval. |
| `TextMode` | `Auto` | `Auto`: native Windows text where supported, bitmap fallback otherwise. `Overlay`: bake text over the image in all delivery modes. `Off`: omit composed text. |

Each `[Content.N]` has `Enabled`, `Name`, `Background`, `ImagePath`, `TextSources`, `Text`, `SecondaryText`, `BadgeText`, and `RssItem`. `Background` is `Wallpaper`, `LiveWallpaper`, `Image`, or `None`. Wallpaper excludes live-window capture. LiveWallpaper uses the existing capture settings and falls back to ordinary wallpaper. None supplies a solid manifest-color background. Image paths are absolute or relative to the INI directory. Choosing an unconfigured Image background opens the file picker and saves both settings atomically.

`TextSources` is an ordered comma-separated list: `CustomText`, `RssFeed`, `SMTC`, `CapsLock`, `Notes`, `DiscordRPC`, `CapsBlink`, `AsusBlink`. CustomText reads the entry's three text fields. Sources contribute corresponding text fields in order, joined by newlines and bounded to 4096 characters. Display regions still wrap/clip to their supported size. An entry's image remains its background; another content entry is a separate cycle item. Empty TextSources means image-only content.

SMTC supplies title/artist/session status on supported Windows 10 1809+ systems. It does not control playback or replace NowPlayingTile's separate artwork/widget features. CapsLock reports the logical toggle state; it does not blink a hardware LED or replace capsblink. `RssItem` chooses headline 1–20 for a numbered RSS entry, default 1; the legacy RSS preset cycles parsed headlines. A missing selected headline or feed failure appears as status text. Background errors produce diagnostics instead of silently replacing a custom image.

Timing warnings appear when enabling or accelerating risky cycling. Editing text/names does not ask to approve the same unchanged timing again. A tray edit refuses to overwrite another writer's simultaneous configuration change; reopen the menu and retry if that occurs.

## Example

This shows wallpaper, RSS over wallpaper, and custom text over a custom image:

```ini
[Content]
Enabled=1
Count=3
CycleEnabled=1
CycleSeconds=15
RefreshSeconds=2
TextMode=Overlay

[Content.1]
Name=Desktop
Background=Wallpaper
TextSources=

[Content.2]
Name=News
Background=Wallpaper
TextSources=RssFeed

[Content.3]
Name=Reminder
Background=Image
ImagePath=img1.jpg
TextSources=CustomText
Text=Remember the meeting
SecondaryText=Bring the draft

[RssFeed]
FeedUrl=https://example.com/feed.xml
UpdateIntervalSeconds=900
```

Replace the example feed URL and image path with real values. The same configuration can be edited without leaving the tray. CLI example:

```cmd
DesktopStub.exe --set Content.Count=1 --set Content.1.Background=Wallpaper --set Content.1.TextSources=CustomText --set Content.1.Text=Hello --set Content.TextMode=Overlay --set Content.Enabled=1
```

Fetch cadence and content cycling are separate. Slow network requests run off the composition/UI thread. Generation takes an immutable snapshot, so a refresh cannot mix one entry's text with another entry's image. Caches include the composed text and delivery mode. Generic wallpaper precaching is suspended while composition is active; each rendered frame can still use the generated-asset cache.

## Timing and rendering limits

Before a tray change enables cycling in registration mode, DesktopStub warns about CPU/disk/registration cost. CLI and externally edited INI settings produce a runtime warning when applied. For live delivery, cycles below ten seconds produce an advisory warning about skipped display cycles. Ten seconds is not a proven OS minimum: [Microsoft documents a five-notification queue and shell-controlled timing](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.tileupdater.enablenotificationqueue), without a fixed cadence guarantee. This engine submits current frames through the existing single-update pipeline; it does not rely on that queue for exact timing. Test visible timing on the target Start screen.

Auto uses the selected native/preset layout; some preset templates replace the background or omit a secondary field. Choose Overlay when the image must stay behind the text. Windows 8/8.1 simulation is an approximation; native branding, RTL mirroring and Windows-version pixel equivalence still need OS testing. Renderer tests validate glyph visibility, clipping, line limits and background retention, not exact shell appearance.

## Migration status

This is a built-in source host, not yet full consolidation of every program. Discord transport/preview and Notes data use shared native implementations also consumed by the standalone programs. Native ASUS/Caps engines and host controls are implemented, with fixture coverage; actual driver behavior and full standalone parity still need verification. Complete standalone UI/configuration parity and NowPlayingTile widget/artwork parity remain open. Standalone apps remain until that migration is complete. Dynamic external plugin loading is not implemented. See [the audit](REWORK_AUDIT.md) for current validation status.

## Real-Time Notes

Enable the Notes source on an entry and select `NotesResource=Resin`, `Stamina`, or `Charge`. The tray offers the corresponding game and account settings. Each game has its own `[Notes.Resin]`, `[Notes.Stamina]`, or `[Notes.Charge]` section with `UID`, `LToken`, and `LTUid`. Use cookie **values**, not an entire Cookie header. Empty credentials produce a configuration status on the tile without sending a request. `[Notes] RefreshSeconds=300`, `HttpTimeoutSeconds=15`, and `MaximumBytes=1048576` control fetching independently of content cycling.

**Cookies are stored in plaintext in the selected INI. Anyone who can read it can obtain them.** Cookie entry is masked and values are hidden in menus/logs, but that does not encrypt the file. Keep the INI private and do not share it in bug reports. This source adds no mandatory protection or automatic import; the old deskband's DPAPI account storage remains separate. Further opt-in storage protection is deferred until functional consolidation, as requested. Live HoYoLAB endpoint/account compatibility has not been verified; tests use synthetic responses.

## Discord Rich Presence

Enable `DiscordRPC` in an entry's text sources. The default is preview-only: `[Discord] SendPresence=0`. Set `Name`, `Details` and `State` (templates use the existing DiscordRPC placeholders). Set a valid `ClientId` and explicitly enable `SendPresence` to send using Discord IPC. `RefreshSeconds=5`, `OperationTimeoutMs=5000`, and `ClearOnStop=1` control the service. The tile shows details/state plus preview, sending, stopped or error status.

For existing advanced configuration, set `[Discord] ProfilePath` to a DiscordRPC INI, absolute or relative to DesktopStub's INI. That profile supplies the complete existing presence/transport settings, including Gateway configuration; the simple Name/Details/State/ClientId fields are used only when ProfilePath is empty. The service reads the profile without creating or migrating it. It uses the shared transport code inside DesktopStub, without launching DiscordRPC.exe. Existing advanced credential rules are not silently changed. Tests inject fake transport and never contact Discord.

## Caps indicator

`CapsLock` only displays the logical toggle state. `CapsBlink` exposes the legacy physical-indicator pattern and its status. Select CapsBlink on an enabled content entry, then use **Caps indicator settings** in the tray. Selecting the source alone does not access a keyboard device.

| `[CapsBlink]` key | Default | Meaning |
|---|---|---|
| `HardwareEnabled` | `0` | Explicitly enable physical indicator control. |
| `KeyboardTargetPath` | `\Device\KeyboardClass0` | Numbered keyboard class device. |
| `BlinkIntervalMs` | `500` | Interval from 50 through 86400000 milliseconds. |

When logical Caps Lock is on, the Caps indicator is held on. When it is off, the Caps indicator toggles; other indicator flags are preserved. Disabling restores the Caps bit to the current logical state before releasing the device. The status distinguishes restoration still pending from completed cleanup. A driver that ignores cancellation can delay shutdown; the engine retains its buffers and ownership rather than claiming a late write cannot happen. The native mutex matches the old program's device identity to avoid competing instances. **Restart indicator pattern** retries after a stopped/error state.

Tests use a fake backend and never query or modify real keyboard indicators. Device permissions and driver behavior remain target-machine checks; errors are surfaced on the tile. The standalone C# program remains during migration.

## ASUS indicator patterns

Select `AsusBlink` to display pattern progress. **ASUS indicator settings** provides the same keys as `[AsusBlink]` and generic CLI `--set`. `HardwareEnabled=0` and `ReadDiskActivity=0` are independent defaults: preview neither opens the ASUS driver nor samples disk counters. `Paused` pauses active pattern time; **Restart patterns** starts the configured sequence again.

Basic pattern keys are `MicState`, `MicInterval`, `MicDuration`, `KeyboardState`, `KeyboardInterval`, `KeyboardDuration`, `ErrorRetry`, and `ErrorAction`. State defaults to `off`; intervals default to `0`; duration defaults to `once`. Mic states are 0/1, keyboard states 0–255; comma-separated states and interval lists are supported. Timing accepts milliseconds or units such as `500ms` and `2s`. Duration `0` repeats indefinitely. Retry defaults to3; `ErrorAction` defaults to `continue`. `exit`/`crash` stop/fault this source, never terminate DesktopStub.

Example preview configuration:

```ini
[AsusBlink]
HardwareEnabled=0
ReadDiskActivity=0
MicState=0,1
MicInterval=500ms
MicDuration=0
KeyboardState=off
```

The mic setting controls its **indicator LED**, not audio mute. The tile distinguishes requested pattern states from hardware control/error status. ASUS patterns retain the last applied state when stopped, matching the existing program; the engine does not invent a restoration value.

An optional `ProfilePath` reads an existing asusblink INI's `[Settings]` section, including numbered mic/keyboard/HDD events. Paths are absolute or relative to DesktopStub's INI. Host-only Startup/tray/log fields in that profile are recognized but not applied. Inline basic pattern fields apply only when ProfilePath is empty. Explicitly enable disk sampling for HDD patterns, and hardware control for actual writes. Existing firmware/driver support is unverified by the fixture tests; the standalone C# program is retained until parity is confirmed.
