# DesktopStub content engine

DesktopStub can compose text and backgrounds through one renderer and delivery path. Enable **Content and sources > Use layered content** in the tray, or set `[Content] Enabled=1`. Existing wallpaper profiles keep their behavior until enabled. The old RSS preset now uses the same composition pipeline and places headlines over a wallpaper background; no separate RSS worker publishes competing notifications.

## Configuration

All keys below work in the INI, through `--set Section.Key=Value`, and through the **Content and sources** tray menu. Text delivery and backgrounds have choice submenus; text providers have checkboxes with an optional source-order editor. **Choose background image** saves the file and background type together. **Add content** adds an entry, and **Move earlier/later in cycle** changes its position while preserving its INI fields/comments. Reducing the number of contents preserves unused sections. Selecting a **Legacy preset** asks before turning off the numbered profile, keeps its entries saved for later re-enabling, and suppresses the preset checkmark while layered content is active. The RSS legacy preset renders headlines over wallpaper; it does not replace the wallpaper with feed data.

| `[Content]` key | Default | Meaning |
|---|---|---|
| `Enabled` | `0` | Use numbered content entries. Boolean names such as `true` work too. |
| `Count` | `1` | Read `Content.1` through `Content.N`, from 1 to 32. |
| `CycleEnabled` | `0` | Rotate enabled entries; otherwise show the first enabled entry. |
| `CycleSeconds` | `10` | Time between entries, 1–86400 seconds. |
| `RefreshSeconds` | `2` | Background/SMTC refresh, 1–86400 seconds. RSS has its own fetch interval. |
| `TextMode` | `Auto` | `Auto`: native Windows text where supported, bitmap fallback otherwise. `Overlay`: bake text over the image in all delivery modes. `Off`: omit composed text. |

Each `[Content.N]` has `Enabled`, `Name`, `Background`, `ImagePath`, `TextSources`, `Text`, `SecondaryText`, `BadgeText`, and `RssItem`. The shared background catalog owns the canonical `Wallpaper`, `LiveWallpaper`, `Image`, `None`, and `SMTCArtwork` values, which are case-insensitive when read and saved canonically. Wallpaper excludes live-window capture. LiveWallpaper uses the existing capture settings and falls back to ordinary wallpaper. If neither wallpaper path is available, DesktopStub reports the unavailable background instead of silently using a solid color. None supplies a solid manifest-color background through a product-scoped `%TEMP%` BMP, so it does not need write access beside the executable. Image paths are absolute or relative to the INI directory. Choosing an unconfigured Image background opens the file picker and saves both settings atomically.

`TextSources` is an ordered comma-separated list: `CustomText`, `RssFeed`, `CapsLock`, `SMTC`, `Notes`, `DiscordRPC`, `CapsBlink`, `AsusBlink`. The shared source catalog owns these canonical names, their display labels, normalization, tray order, and host dispatch. Names are case-insensitive when read and are saved in the canonical form above. CustomText reads the entry's three text fields. Sources contribute corresponding text fields in order, joined by newlines and bounded to 4096 characters. Display regions still wrap/clip to their supported size. An entry's image remains its background; another content entry is a separate cycle item. Empty TextSources means image-only content.

`RssFeed`, `SMTC`, and `Notes` can refresh while an enabled entry waits for its next cycle slot, so a cycle can display cached data promptly. `DiscordRPC`, `CapsBlink`, and `AsusBlink` run only while the selected entry uses them; changing away from that entry cancels the provider. This prevents a background cycle entry from continuing to send presence or control indicator hardware.

SMTC supplies title/artist/session status on supported Windows 10 1809+ systems. `Background=SMTCArtwork` adds the current media artwork from the same immutable session snapshot; select `TextSources=SMTC` for a combined entry or leave it empty for artwork only. Missing, idle or invalid artwork clears the prior track's image and uses the normal solid/text fallback. The host stores bounded artwork in a writable profile-scoped temporary cache. It does not control playback. CapsLock reports the logical toggle state; `CapsBlink` supplies the separate opt-in indicator controller. `RssItem` chooses headline 1–20 for a numbered RSS entry, default 1; the legacy RSS preset cycles parsed headlines. A missing selected headline or feed failure appears as status text. Background errors produce diagnostics instead of silently replacing a custom image.

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

Before a tray change enables cycling in registration mode, DesktopStub warns about CPU/disk/registration cost. CLI and externally edited INI settings produce a runtime warning when applied. For live delivery, cycles below ten seconds produce an advisory warning about skipped display cycles. Ten seconds is not a proven OS minimum. [Microsoft's Windows 8-era notification queue documentation](https://learn.microsoft.com/en-us/previous-versions/windows/apps/hh781199(v=win.10)) says the shell determines each notification's display duration and order, and applications cannot control either. Thus even a ten-second cycle is not a guarantee that every source will be visibly displayed. This engine submits current frames through the existing single-update pipeline; it does not rely on that queue for exact timing. Measure visible timing on the target Windows 8/8.1/10 Start screen if seeing every source matters.

The [periodic notification API](https://learn.microsoft.com/en-us/windows/apps/develop/notifications/periodic-notification-overview) polls remote tile URLs on a separate schedule. Its polling limits do not establish a minimum for this engine's local tile updates or source retrieval. `RefreshSeconds` controls data retrieval, `CycleSeconds` selects the composed source, and Windows controls presentation of delivered live tiles. Research refreshed on 2026-10-11; no primary source establishing a fixed ten-second local-update minimum was found.

Auto uses the selected native/preset layout; some preset templates replace the background or omit a secondary field. Choose Overlay when the image must stay behind the text. Windows 8/8.1 simulation is an approximation; native branding, RTL mirroring and Windows-version pixel equivalence still need OS testing. Renderer tests validate glyph visibility, clipping, line limits and background retention, not exact shell appearance.

## Migration status

This is a built-in source host. Shared providers now cover Notes data/details/actions, SMTC metadata/artwork, Discord presence/preview, and ASUS/Caps patterns. Profile import and advanced configuration are being completed before removing the donor build entries. The old Explorer deskband and media widget are not part of the tile source engine. Dynamic external plugin loading is not implemented. Real device, account and older Windows acceptance remain separate from the inert integration tests; see [the audit](REWORK_AUDIT.md) for current validation status.

## Real-Time Notes

Enable the Notes source on an entry and select `NotesResource=Resin`, `Stamina`, or `Charge`. `NotesDetail=Summary` shows the usual summary; the tray detail picker also offers the metrics available for that game. Separate entries can cycle through those metrics. The tray offers corresponding game/account settings, profile/cookie import, clear and refresh actions. Each game has its own `[Notes.Resin]`, `[Notes.Stamina]`, or `[Notes.Charge]` section with `UID`, `LToken`, and `LTUid`. Use cookie **values**, not an entire Cookie header. Empty credentials produce a configuration status on the tile without sending a request. `[Notes] RefreshSeconds=300`, `HttpTimeoutSeconds=15`, and `MaximumBytes=1048576` control fetching independently of content cycling. Per-account `RefreshSeconds=0` inherits the global interval.

**Cookies entered as plain values are stored in plaintext in the selected INI. Anyone who can read it can obtain them.** Cookie entry is masked and values are hidden in menus/logs, but that does not encrypt the file. Keep the INI private and do not share it in bug reports. Existing protected values can be explicitly imported into `LTokenProtected`/`LTUidProtected`; the old `LTokenV2Protected`/`LTuidV2Protected` aliases are also readable. Import copies the opaque values without decoding or rewriting the source. The fetch worker decodes protected values only when that account is used, so a copied protected profile may be unusable under another Windows user or machine. There is no mandatory encryption or automatic registry discovery. Creating new opt-in protected values remains deferred until functional consolidation. Live HoYoLAB endpoint/account compatibility has not been verified; tests use synthetic responses.

These import/clear commands only configure the selected INI. Explicit `--set` overrides imported values, including choosing plain versus protected credentials, and rejected batches leave the target unchanged:

```cmd
DesktopStub.exe --ini DesktopStub.ini --notes-import-profile OldNotes.ini
DesktopStub.exe --ini DesktopStub.ini --notes-import-cookie Resin=Cookies.json
DesktopStub.exe --ini DesktopStub.ini --notes-clear Resin
```

`--notes-refresh Resin` (or `Stamina`, `Charge`, `All`) requests the existing resident to refresh; it fails when no resident is running. Clearing credentials invalidates pending old-account results. Importing a profile never adds a Startup entry or accesses a live account.

## Discord Rich Presence

Enable `DiscordRPC` in an entry's text sources. The default is preview-only: `[Discord] SendPresence=0`. Set `Name`, `Details` and `State` (templates use the existing DiscordRPC placeholders). Set a valid `ClientId` and explicitly enable `SendPresence` to send using Discord IPC. `RefreshSeconds=5`, `OperationTimeoutMs=5000`, and `ClearOnStop=1` control the service. The tile shows details/state plus preview, sending, stopped or error status.

For existing advanced configuration, set `[Discord] ProfilePath` to a DiscordRPC INI, absolute or relative to DesktopStub's INI. That profile supplies the complete existing presence/transport settings, including Gateway configuration; the simple Name/Details/State/ClientId fields are used only when ProfilePath is empty. The service reads the profile without creating or migrating it. It uses the shared transport code inside DesktopStub, without launching DiscordRPC.exe. Existing advanced credential rules are not silently changed. Tests inject fake transport and never contact Discord.

## Caps indicator

`CapsLock` only displays the logical toggle state. `CapsBlink` exposes the legacy physical-indicator pattern and its status. Select CapsBlink on an enabled content entry, then use **Caps indicator settings** in the tray. Selecting the source alone does not access a keyboard device.

The normal DesktopStub build includes the CapsBlink and AsusBlink providers. A build made with `DESKTOPSTUB_ENABLE_HARDWARE_SOURCES=0` removes their raw device backends rather than merely changing `HardwareEnabled`; selecting either source then shows that the provider was omitted. Rebuild with the default value `1` when those sources are required.

| `[CapsBlink]` key | Default | Meaning |
|---|---|---|
| `HardwareEnabled` | `0` | Explicitly enable physical indicator control. |
| `KeyboardTargetPath` | `\Device\KeyboardClass0` | Numbered keyboard class device. |
| `BlinkIntervalMs` | `500` | Interval from 50 through 86400000 milliseconds. |

When logical Caps Lock is on, the Caps indicator is held on. When it is off, the Caps indicator toggles; other indicator flags are preserved. Disabling restores the Caps bit to the current logical state before releasing the device. The status distinguishes restoration still pending from completed cleanup. A driver that ignores cancellation can delay shutdown; the engine retains its buffers and ownership rather than claiming a late write cannot happen. The native mutex matches the old program's device identity to avoid competing instances. **Restart indicator pattern** retries after a stopped/error state.

Tests use a fake backend and never query or modify real keyboard indicators. Device permissions and driver behavior remain target-machine checks; errors are surfaced on the tile. `--caps-import-profile OldCaps.ini` imports the pattern into the selected host INI, with the same tray action. It preserves `HardwareEnabled` and ignores donor Startup/tray preferences. An import does not activate a device.

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

An optional `ProfilePath` reads an existing asusblink INI's `[Settings]` section, with `[Options]` as the older fallback, including numbered mic/keyboard/HDD events. Paths are absolute or relative to DesktopStub's INI. Host-only Startup/tray/log fields in that profile are recognized but not applied. Inline pattern/event fields apply only when ProfilePath is empty. The tray offers import and numbered-event editing; `--asus-import-profile OldAsus.ini` replaces the inline pattern, removes old recognized events and clears ProfilePath in one save. It preserves hardware activation, disk sampling, pause and unrelated host fields. Explicit `--set AsusBlink.Event001HddState=140` and other pattern overrides are applied after import. Explicitly enable disk sampling for HDD patterns, and hardware control for actual writes. Existing firmware/driver support is unverified by the fixture tests.

## Media metadata and artwork

An entry with `Background=SMTCArtwork` uses artwork from the current Windows media session. Add `TextSources=SMTC` for title/artist/status and choose `TextMode=Overlay` to retain artwork behind text. Artwork-only entries can leave `TextSources` empty. Missing artwork, an idle session or a decoding error clears the old image and falls back to the existing solid background/status. One immutable media snapshot supplies text and artwork for a composed frame.

`--now-playing-import-profile OldNowPlaying.ini` and the corresponding tray action import Text, Artwork, Combined or Cycle layouts. Existing `Content.*` entries are replaced atomically; explicit `--set` overrides apply last. Source files, hardware activation and existing Startup configuration are preserved. The donor Startup preference is diagnosed and deferred to DesktopStub's explicit Startup controls. Its tile republish interval has no exact host equivalent and is not used as `CycleSeconds`. The Cycle layout uses the existing host cycle interval; its shared global text mode cannot reproduce per-frame native/overlay switching exactly. Imports default to configuration-only and never query a media session.
