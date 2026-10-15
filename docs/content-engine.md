# DesktopStub content engine

DesktopStub can compose text and backgrounds through one renderer and delivery path. Enable **Tile contents > Use tile contents** in the tray, or set `[Content] Enabled=1`. Existing wallpaper profiles keep their behavior until enabled. **Tile contents > Presets and imports** adds wallpaper, wallpaper/RSS, or a two-item wallpaper/SMTC cycle while retaining previous saved entries as inactive contents. RSS headlines can sit over the wallpaper; selecting RSS does not inherently remove it.

## Configuration

All keys below work in the INI and through `--set Section.Key=Value`. The tray separates **Tile contents** (what appears together), **Sources and accounts** (provider setup), **Tile display** (text and delivery), and **Updates and cycling** (when it changes). Each entry under **Tile contents > Content list** groups its background, text sources/order, custom text, and RSS/Notes selection. **Choose background image** saves the file and background type together. **Add content** adds an entry, and **Move earlier/later in cycle** changes its position while preserving its INI fields/comments. Reducing the content-slot count preserves unused sections. Built-in presets append new active entries and make earlier slots inactive, with a confirmation preview; repeating an unchanged active preset does not append again. **Compatibility modes** retains the older source switches, which disable numbered contents without removing their sections. **Import old NowPlayingTile profile** is explicitly a complete replacement and asks before removing saved Content.* sections. See the [settings guide](desktopstub-settings.md) for the exact differences.

For an RSS overlay on wallpaper, configure the feed under **Sources and accounts > RSS / Atom settings**, then select Wallpaper as the entry's background and RSS as one of its text sources. For bitmap text, choose Overlay under **Tile display > Text delivery**. The separate **Default text (without tile contents)** menu edits the older standalone text fields; while tile contents are enabled, text comes from their entries instead. See the [settings guide](desktopstub-settings.md) for the complete menu map.

| `[Content]` key | Default | Meaning |
|---|---|---|
| `Enabled` | `0` | Use numbered content entries. Boolean names such as `true` work too. |
| `Count` | `1` | Read `Content.1` through `Content.N`, from 1 to 32. |
| `CycleEnabled` | `0` | Rotate enabled entries; otherwise show the first enabled entry. |
| `CycleDriver` | `DesktopStub` | Existing profiles use the app timer. `Auto` chooses Windows' queue when eligible; `Windows` requests it. Both use the app timer when native queuing is unavailable for the layout. New presets choose Auto. |
| `CycleSeconds` | `10` | App/fallback interval between entries, 1–86400 seconds. Windows owns the native queue's visible timing. |
| `RefreshSeconds` | `2` | Background/media polling interval, 1–86400 seconds. With supported SMTC events in Windows rotation, fallback polling is at least 30 seconds. RSS has its own fetch interval. |
| `TextMode` | `Auto` | `Auto`: native Windows text where supported, bitmap fallback otherwise. `Overlay`: bake text over the image in all delivery modes. `Off`: omit composed text. |

Each `[Content.N]` has `Enabled`, `Name`, `Background`, `ImagePath`, `TextSources`, `Text`, `SecondaryText`, `BadgeText`, and `RssItem`. The shared background catalog owns the canonical `Wallpaper`, `LiveWallpaper`, `Image`, `None`, `SMTCArtwork`, and `Transparent` values, which are case-insensitive when read and saved canonically. Wallpaper excludes live-window capture. LiveWallpaper uses the existing capture settings and falls back to ordinary wallpaper. If neither wallpaper path is available, DesktopStub reports the unavailable background instead of silently using a solid color. None supplies a solid manifest-color background through a product-scoped `%TEMP%` BMP, so it does not need write access beside the executable. Transparent supplies no app background: adaptive automatic text omits the image; bitmap delivery retains PNG alpha, while Windows controls the final tile plate. See [transparent background behavior](transparent-tile-background-2026-10-13.md). Image paths are absolute or relative to the INI directory. Choosing an unconfigured Image background opens the file picker and saves both settings atomically.

`TextSources` is an ordered comma-separated list: `CustomText`, `RssFeed`, `CapsLock`, `SMTC`, `Notes`, `DiscordRPC`, `CapsBlink`, `AsusBlink`. The shared source catalog owns these canonical names, their display labels, normalization, tray order, and host dispatch. Names are case-insensitive when read and are saved in the canonical form above. CustomText reads the entry's three text fields. Sources contribute corresponding text fields in order, joined by newlines and bounded to 4096 characters. Display regions still wrap/clip to their supported size. An entry's image remains its background; another content entry is a separate cycle item. Empty TextSources means image-only content.

`RssFeed`, `SMTC`, and `Notes` can refresh while an enabled entry waits for its next cycle slot, so a cycle can display cached data promptly. In app-managed rotation, Discord sending and ASUS actions follow the selected source. CapsBlink hardware is independent of tile text and stays enabled according to its own HardwareEnabled setting; selecting its text source only adds status to the tile. Windows rotation can refresh their read-only previews across enabled entries. Turning on presence sending or hardware actions requires app-managed rotation, since Windows does not report which queued item is visible.

SMTC supplies title/artist/session status on supported Windows 10 1809+ systems. `Background=SMTCArtwork` adds the current media artwork from the same immutable session snapshot; select `TextSources=SMTC` for a combined entry or leave it empty for artwork only. Missing, idle or invalid artwork clears the prior track's image and uses the normal solid/text fallback. The host stores bounded artwork in a writable profile-scoped temporary cache. Explicit tile-click and Quick Actions controls can request playback, repeat/shuffle changes and default-output volume changes; unsupported player controls report their limits. CapsLock reports the logical toggle state; `CapsBlink` supplies the separate opt-in indicator controller. `RssItem` chooses headline 1–20 for a numbered RSS entry, default 1; the legacy RSS preset cycles parsed headlines. A missing selected headline or feed failure appears as status text. Background errors produce diagnostics instead of silently replacing a custom image.

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

Fetch cadence and content cycling are separate. Slow network requests run off the composition/UI thread. Generation takes an immutable snapshot, so a refresh cannot mix one entry's text with another entry's image. Bitmap cache keys include text when it is rendered into the image; native XML text changes reuse the unchanged background image. Delivery keys still include the complete displayed text and link. Generic wallpaper precaching is suspended while composition is active; each rendered frame can still use the generated-asset cache. See [Windows-managed content rotation](windows-content-rotation.md) for queue behavior and media events.

## Timing and rendering limits

Before a tray change enables cycling in registration mode, DesktopStub warns about CPU/disk/registration cost. CLI and externally edited INI settings produce a runtime warning when applied. `CycleDriver=DesktopStub` keeps the existing timer: for live delivery, intervals below ten seconds produce an advisory warning about skipped display cycles. Ten seconds is not a proven OS minimum. `Auto` or `Windows` uses a native notification queue for eligible layouts with two through five enabled entries. Windows controls queue display duration and order, while DesktopStub refreshes the queued source data. Static delivery, more than five enabled entries, or visibility-dependent hardware/presence actions use the app timer instead. [Microsoft's notification queue documentation](https://learn.microsoft.com/en-us/previous-versions/windows/apps/hh781199(v=win.10)) explains that applications cannot control native queue timing/order. Thus a configured ten-second fallback interval does not guarantee ten-second Windows rotation. Measure visible behavior on the target Windows 8/8.1/10 Start screen if seeing every source matters.

The [periodic notification API](https://learn.microsoft.com/en-us/windows/apps/develop/notifications/periodic-notification-overview) polls remote tile URLs on a separate schedule. Its polling limits do not establish a minimum for this engine's local tile updates or source retrieval. `RefreshSeconds` controls data retrieval, `CycleSeconds` selects the composed source, and Windows controls presentation of delivered live tiles. Research refreshed on 2026-10-11; no primary source establishing a fixed ten-second local-update minimum was found.

Auto uses the selected native/preset layout. The Windows 10 Adaptive bitmap path retains the image and all selected fields using the same caption/secondary/badge grouping, both for automatic fallback and forced Overlay. Explicit Windows 8.1 preset templates may replace the background or omit a secondary field, as their native catalog specifies. Overlay forces bitmap delivery over an image in every delivery mode; it does not select a different template. Both simulations approximate shell appearance; native branding, RTL mirroring and Windows-version pixel equivalence still need OS testing. Renderer tests validate glyph visibility, clipping, line limits and background retention, not exact shell appearance. See the [adaptive fallback](adaptive-static-text-gap-2026-10-13.md).

## Migration status

This is a built-in source host. Shared providers cover Notes data/details/actions, SMTC metadata/artwork, Discord presence/preview, and ASUS/Caps patterns. All five donor app hosts/build entries are retired; explicit profile imports and the Discord advanced editor remain supported. The old Explorer deskband and media widget are not part of the tile source engine. Dynamic external plugin loading is not implemented. Real device, account and older Windows acceptance remain separate from inert integration tests; see [migration](donor-retirement-2026-10-11.md) and [the audit](REWORK_AUDIT.md).

## Real-Time Notes

Enable the Notes source on an entry and select `NotesResource=Resin`, `Stamina`, or `Charge`. `NotesDetail=Summary` shows a stacked resource/activity overview when Notes is the only text source; the tray detail picker also offers the metrics available for that game. Separate entries can cycle through those metrics. The tray offers corresponding game/account settings, profile/cookie import, clear and refresh actions. Each game has its own `[Notes.Resin]`, `[Notes.Stamina]`, or `[Notes.Charge]` section with `UID`, `LToken`, and `LTUid`. Use cookie **values**, not an entire Cookie header. Empty credentials produce a configuration status on the tile without sending a request. `[Notes] RefreshSeconds=300`, `HttpTimeoutSeconds=15`, and `MaximumBytes=1048576` control fetching independently of content cycling. Per-account `RefreshSeconds=0` inherits the global interval.

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

The tray's advanced editor and CLI use the same typed option catalog, including
censoring, layouts, time ranges, assets, buttons, AFK and transport options. Basic
Name/Details/State/ClientId values remain editable for use after leaving an
advanced profile. The menu identifies the active profile and actual host refresh.
Explicit advanced cadence changes update `Discord.RefreshSeconds`; contradictory
host/profile cadence requests in one command are rejected.

```cmd
DesktopStub.exe --ini DesktopStub.ini --discord-create-profile DiscordSource.ini
DesktopStub.exe --ini DesktopStub.ini --discord-set general.details_template=Working
DesktopStub.exe --ini DesktopStub.ini --discord-set general.update_interval=8
```

These commands are configuration-only. Creation uses shared defaults, binds the
new profile and never replaces an existing file. Editing requires a separate
existing profile; reads never create one. Both files validate before publication,
and ordinary save failures restore their exact previous bytes. Each publication
is atomic; a power loss between the two is not a crash-safe two-file transaction.
After an external/manual profile edit, the host's `Discord.RefreshSeconds` remains
the actual scheduler preference. Stale tray editors ask you to reopen before saving.

**Plain Discord tokens are readable in the profile and command-line tokens may
remain in shell history.** Secret prompts are blank and masked. Intentional CLI
credential replacement requires `--discord-private-input`; values are not echoed.
An explicit plain/protected replacement clears the opposite representation,
unrelated edits preserve credentials, and clearing credentials does not reactivate
an old hidden value. Existing protected tokens are still readable; a configured
protected value or decode failure takes precedence and never falls back to plain
text. No token is encrypted/migrated during editor or preview operations. Keep
profiles private, avoid sharing them, and use an existing protected value when
that Windows-user-bound storage suits your setup. Actual sends require the
separate `SendPresence` choice.

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

### Media presentation and game account setup

[SMTC] Indicator=Status|AppIcon|None selects the displayed indicator;
ShowRepeatShuffle=0|1 adds known repeat/shuffle states. These settings are
under the Now playing tile's Source and sign-in settings > Now playing appearance
menu and accept generic CLI --set. Text stays top-aligned. Status icons reflect
current metadata, not the click action. AppIcon uses a bounded source-worker cache
and falls back to status when the app/Windows cannot provide a logo.

Each game's Notes.Resin, Notes.Stamina or Notes.Charge section can store
an explicit Server, chosen automatically with a discovered account. Empty Server
preserves the older UID-prefix inference. Changing a UID through the tray/CLI
clears a stale Server unless the same CLI batch supplies a replacement.

Embedded sign-in is game-specific. Choose Find game accounts after signing in,
select a UID/server when multiple roles exist, then Save selected account.
Manual UID entry remains a fallback with a clear game heading.
Notes.SignIn.Resin, Notes.SignIn.Stamina and Notes.SignIn.Charge sections accept
RetainSession=0|1 through INI/CLI and the window's Remember sign-in checkbox.
Default 0 uses a fresh private session; remembered profiles are separate per game.
Local browser-data cleanup never calls server Logout or clears imported INI
credentials. It waits for a blank page before clearing and reports success only
after the browser confirms completion. Runtime failure, explicit cancellation,
forced shutdown or power loss can still interrupt erasure.

Plain saved tokens, exports and backups remain readable unless opt-in protection
is selected. Protect these files and browser data from shared uploads.