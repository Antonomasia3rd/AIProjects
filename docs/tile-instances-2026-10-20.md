# DesktopStub independent tile instances

DesktopStub configures tiles by type. Each tile has a stable identity and its own
source, update interval, text layout, appearance and click action. Several RSS
tiles can therefore use different feeds and fetch intervals in the same resident
process. The tray icon and Startup entry belong to the application.

## Tray workflow

- **Tiles > Add tile...** creates a Wallpaper, RSS, Now Playing, Prayer Times,
  Genshin Impact, Honkai: Star Rail, Zenless Zone Zero, Discord or Quick Action
  instance. Photo gallery redirects to the existing gallery setup workflow.
- **Tiles > your tile > Source settings** contains only that type's source
  options. RSS has its own URL and fetch interval; Prayer Times has its own city
  and calculation options; a game tile selects its own saved account.
- **Appearance** controls transparent/color backgrounds and name/corner-logo
  branding. Transparent backgrounds and name-only branding are the defaults.
- **Text size, wrapping and layout** controls native named text styles, line
  limits, alignment, vertical position and gaps for the selected tile.
- **Click action** chooses settings, an RSS article or a supported media action.
- **Pin tile...** appears for an unpinned tile; **Unpin from Start...** appears
  for a pinned tile. Unknown membership exposes **Check pin status...** instead.
- **Tile contents...** is scoped to the selected tile. **All tile contents...**
  is the application-wide overview. Long content/status output has pages.
- **Remove tile settings** is available after a secondary tile is confirmed
  unpinned. Removing its settings does not delete source photographs or accounts.
- **Plugins** contains independent CapsBlink and ASUS indicator settings. These
  utilities do not add indicator text to the Desktop tile.
- Each tile has **Delivery and exit behavior**, **Image cache**, **Generated
  assets and scales** and **Text on tile sizes** settings. Image sources also
  expose fitting and decode options; Wallpaper exposes desktop observation and
  capture options. A gallery row opens these through **Rendering, cache and
  delivery...**. Application Startup, tray, console and diagnostics remain
  application settings.

Adding an ordinary tile saves its configuration. Its pin command remains an
explicit action, with Windows providing the confirmation. Gallery setup retains
its existing queued sequence of pin confirmations and **Pin all...** commands.
The tile menu displays 16 instances per page; **Previous tiles...** and
**Next tiles...** reach the rest without consuming one fixed block of command
IDs per configured tile.

## Schema and identities

The presence of `Tiles.Instances`, including an explicitly empty value, selects
the instance schema. It is an ordered, comma-separated list of unique IDs. IDs
use lowercase ASCII letters, digits, hyphens and underscores, with a maximum of
54 characters so the native `AipSource_<id>` identity fits Windows' 64-character
limit. There is no fixed seven/eight-source or 32-content-slot ceiling on this
list. Memory, menu practicality and Windows' own behavior still impose practical
limits.

```ini
[Tiles]
SchemaVersion=1
Instances=desktop,music,releases,prayers

[Tile.desktop]
Type=Wallpaper
Name=Desktop
Primary=1
Content.Background=Wallpaper
RefreshSeconds=2

[Tile.music]
Type=NowPlaying
Name=Now playing
SMTC.Indicator=Status
SMTC.ShowRepeatShuffle=1
ClickAction=toggle

[Tile.releases]
Type=RSS
Name=Windhawk releases
RssFeed.FeedUrl=https://mods.windhawk.net/releases.atom
RssFeed.UpdateIntervalSeconds=900
ClickAction=source

[Tile.prayers]
Type=PrayerTimes
Name=Prayer times
PrayerTimes.City=
PrayerTimes.Country=
```

Each `Tile.<id>` section requires `Type`. The canonical type values are
`Wallpaper`, `RSS`, `NowPlaying`, `PrayerTimes`, `Genshin`, `HSR`, `ZZZ`,
`Discord` and `QuickAction`. `Gallery` is reserved by the portable model; the
current user-facing gallery workflow uses its ownership-aware gallery engine,
described below.

Only one Wallpaper instance can have `Primary=1`. It updates the existing
primary Desktop identity; other ordinary instances use `AipSource_<id>`.
Renaming a tile changes its name, not its ID. Keep IDs stable to preserve pins.
An empty list creates no ordinary tile demand; independent plugins and configured
gallery pins retain their own controls.

Common instance fields are:

| Key | Purpose |
| --- | --- |
| `Type`, `Name` | Type and visible name. |
| `Primary` | Primary Desktop identity; only one Wallpaper instance. |
| `Background` | `Transparent` or `Color` static topic background. |
| `BackgroundColor` | `Auto` or `#RRGGBB`. |
| `Branding` | `Auto`, `None`, `Logo`, `Name` or `NameAndLogo`. |
| `TextMode` | `Auto`, `Overlay` or `Off`. |
| `RefreshSeconds` | Local source/check cadence, 1–86,400 seconds; source-specific network intervals remain separate. |
| `ClickAction` | `setup`, RSS-only `source`, or a supported media action. |
| `VolumeStepPercent` | Volume action step, 1–100 percentage points. |

Media action names are `toggle`, `play`, `pause`, `previous`, `next`, `repeat`,
`shuffle`, `volume-down` and `volume-up`. A QuickAction tile stores its chosen
action in `Action`; it can have its own `VolumeStepPercent`.

Source fields use explicit prefixes within the instance section. For example,
`RssFeed.FeedUrl` is one key in `[Tile.releases]`, and `PrayerTimes.City` is one
key in `[Tile.prayers]`. These are not global source sections. The adapter reuses
the existing provider validators instead of creating separate tray/INI/CLI
interpretations of each value.

## Five independent RSS tiles

The first feed below is the requested Windhawk release feed. The other four
URLs are placeholders; replace them with actual RSS/Atom URLs.

```ini
[Tiles]
SchemaVersion=1
Instances=releases,news,projects,articles,updates

[Tile.releases]
Type=RSS
Name=Windhawk releases
RssFeed.FeedUrl=https://mods.windhawk.net/releases.atom
RssFeed.UpdateIntervalSeconds=600

[Tile.news]
Type=RSS
Name=News
RssFeed.FeedUrl=https://example.invalid/news.atom
RssFeed.UpdateIntervalSeconds=900

[Tile.projects]
Type=RSS
Name=Projects
RssFeed.FeedUrl=https://example.invalid/projects.atom
RssFeed.UpdateIntervalSeconds=1800

[Tile.articles]
Type=RSS
Name=Articles
RssFeed.FeedUrl=https://example.invalid/articles.atom
RssFeed.UpdateIntervalSeconds=3600

[Tile.updates]
Type=RSS
Name=Updates
RssFeed.FeedUrl=https://example.invalid/updates.atom
RssFeed.UpdateIntervalSeconds=7200
```

These tiles do not share a feed URL, refresh deadline or selected layout. One
tile's successful fetch cannot replace another tile's snapshot. A blank feed URL
is a local setup state and does not start HTTP polling.

| RSS field | Default | Range/meaning |
| --- | --- | --- |
| `RssFeed.FeedUrl` | Empty | HTTP(S) feed URL. |
| `RssFeed.UpdateIntervalSeconds` | `900` | 60–86,400 seconds. |
| `RssFeed.MaxItems` | `5` | Keep 1–20 parsed entries. |
| `RssFeed.HttpTimeoutSeconds` | `20` | 5–120 seconds. |
| `RssFeed.MaxFeedBytes` | `2097152` | 65,536–16,777,216 bytes. |
| `RssFeed.UserAgent` | DesktopStub RSS user agent | Bounded single-line HTTP user agent. |
| `Content.RssItem` | `1` | Featured item, 1–20. |

The default RSS click action is `source`, which opens the featured article when
a valid link is available. Choose `setup` to open the tile's settings instead.

For a larger, wrapped headline, select **Text size, wrapping and layout >
Layout > Headlines with summaries**. This writes a complete named layout:
`TextPreset=RssHeadlines`, native `TextMode=Auto`, a semibold 15-pixel `base` headline,
wrapping and bounded line/gap settings. It replaces stale individual text
settings from the previous layout, so selecting it produces an observable
change. Medium shows one rich item, Wide two and Large three, with line limits
adjusted to the available tile height. RSS also uses this readable layout when
its preset is Default.

The equivalent explicit layout is:

```ini
[Tile.releases]
TextPreset=RssHeadlines
TextMode=Auto
TextStyle=base
TextSecondaryStyle=captionSubtle
TextBadgeStyle=captionSubtle
TextWrap=1
TextMaxLines=3
TextSecondaryMaxLines=2
TextBadgeMaxLines=1
TextGapLines=0
TextAlign=left
TextStacking=top
```

## INI and command-line parity

The command-line syntax understands source prefixes inside `Tile.<id>` keys.
This example changes an existing `releases` tile without replacing the tile list:

```cmd
DesktopStub.exe --configure-only --set "Tile.releases.RssFeed.FeedUrl=https://mods.windhawk.net/releases.atom" --set "Tile.releases.RssFeed.UpdateIntervalSeconds=900"
```

To create a separate configuration example without altering the normal profile:

```cmd
DesktopStub.exe --ini "D:\Temp\DesktopStub-rss-example.ini" --configure-only --set "Tiles.SchemaVersion=1" --set "Tiles.Instances=releases" --set "Tile.releases.Type=RSS" --set "Tile.releases.Name=Windhawk releases" --set "Tile.releases.RssFeed.FeedUrl=https://mods.windhawk.net/releases.atom"
```

`--configure-only` validates and saves the complete batch without starting
providers, registering packages, modifying Startup, pinning tiles or signaling
the ordinary resident instance. A regular persistent `--set` invocation uses
the same validation and notifies the resident as usual.

`Tiles.Instances` is the complete list, not an append command. Keep existing IDs
when editing it manually. The tray's Add workflow guards against stale menus
dropping a tile that was added in another configuration change.

## Per-tile rendering, cache and delivery

These keys live alongside the source settings in `[Tile.<id>]`, or in
`[Gallery.Tile.N]` for a gallery row. The tray, INI and dotted `--set` keys use
the same validation. For example:

```cmd
DesktopStub.exe --configure-only --set "Tile.releases.Cache.MaxEntries=7" --set "Tile.releases.Delivery.Template=Adaptive" --set "Gallery.Tile.1.Cache.MaxEntries=12"
```

| Key family | Settings |
| --- | --- |
| `Delivery` | `Mode` (`Auto`, `LiveTile`, `Registration`), `Template` (`Adaptive`, `Windows81Preset`), `ClearOnShutdown`, and `ManifestTarget`. |
| `Delivery` registration backend | `UsePowerShell`, `ComUseHelper`, `ComTimeoutMs`, `ComCancelWaitMs`, `PowerShellExe`, `PowerShellTimeoutMs`, `PowerShellPollMs`, `PowerShellTerminateWaitMs`, `PowerShellOutputLimitBytes`, `UnresolvedBlockMs`. |
| `Cache` | `Enabled`, `MaxEntries` (0–4096), `Directory` (blank uses the application cache parent). Each tile has a separate cache partition and budget under that parent. |
| `Assets` | `ScaleAuto`, `Scale100`, `Scale125`, `Scale150`, `Scale200`, `Scale400`, `StoreLogo`, `MediumTile`, `Square44x44Logo`, `Square30x30Logo`, `SmallTile`, `WideTile`, `LargeTile`, `SplashScreen`, `GenerateIconWhenDisabled`, `DeleteDisabled`. |
| `Text` | `ApplyToMediumTile`, `ApplyToWideTile`, `ApplyToLargeTile`. |
| `Wallpaper` fitting and decode | `Fit` (`Auto`, `Fill`, `Fit`, `Stretch`, `Center`, `Tile`, `Span`), `DisableFitting`, `DecodeLowMemory`, `DecodeMaxLongEdge` (0 means automatic). Gallery fitting supports `Auto`, `Fill` and `Fit`, with Auto using its gallery group's fit. |
| `Wallpaper` desktop source | `Listen`, `ListenFit`, `DetectionMethod`, `DetectionFallbackOnInvalid`, `Capture`, `CaptureDelayMs`, `CaptureRefreshMs`, `CaptureStartupRefreshMs`, `CaptureStartupRefreshDurationMs`, `CaptureScreenFallback`. |

Conversion captures missing values from the existing application settings once,
including profiles already using SchemaVersion 1. Adding a tile captures its
creation defaults once too. Subsequent application-level edits do not change
existing tiles' saved policies. Missing hand-written keys use documented policy
defaults rather than a live global override. Gallery creation retains its own
group fitting through `Wallpaper.Fit=Auto` and `Wallpaper.DisableFitting=0`.

`Wallpaper.Listen=0` retains the selected background until explicit refresh or a
relevant configuration change. `Wallpaper.ListenFit=0` similarly retains the
captured desktop fitting mode; explicit refresh updates it. When enabled, fit
observation runs at that tile's `RefreshSeconds` cadence. Auto fit on an RSS,
transparent or other non-desktop source resolves Fill without querying desktop
fitting. Each frame carries the fitting mode used by its generation key and
renderer. The old slideshow precache options remain accepted for legacy
configuration, but are not exposed as active controls in instance mode.

Native Live Tile delivery keeps the existing primary or `AipSource_...` pin.
Registration delivery needs a separate app identity and rendered static tile
images. Changing this through the tile menu offers preparation while retaining
the old pin; after the first successful update, manually pin the new app from
Start's All apps list and remove the old pin when satisfied. Windows cannot
convert either identity's existing pin in place. Separate Registration packages
currently require `Delivery.ManifestTarget=Windows10`. Auto follows the available
runtime and requires Registration preparation when running unpackaged. Merely
saving a Registration mode using `--configure-only` does not deploy a package or
pin anything; finish preparation through that tile's setup command.

## Text customization

`TextPreset=Default` uses the type's normal layout. `Custom` enables individual
style/geometry choices; `RssHeadlines` is the RSS rich-headline layout. New
instances have their own values rather than inheriting a global `[TileText]`
preset.

Named Windows styles include `caption` (12), `body`/`base` (15), `subtitle` (20),
`title` (24), `subheader` (34) and `header` (46), with the supported subtle and
numeral variants. They express native style/weight choices rather than an
arbitrary font-family or pixel-size selector. Larger text leaves less room for
content, and Windows remains responsible for native text rendering.

The other fields are `TextSecondaryStyle`, `TextBadgeStyle`, `TextWrap`,
`TextMaxLines`, `TextSecondaryMaxLines`, `TextBadgeMaxLines`, `TextAlign`,
`TextStacking` and `TextGapLines`. Line limits are 1–8; blank-line gaps are 0–2.
Changing an individual text property through the tray activates Custom layout,
except that RSS Default and Headlines with summaries retain the readable RSS
layout with the edited property. An explicitly selected Custom layout stays
Custom.

## Prayer Times clock layout

A PrayerTimes instance owns `PrayerTimes.City`, `Country`, `State`, `TimeZone`,
`Method`, `School`, `LatitudeAdjustmentMethod`, local minute adjustments and
network/display options. Location starts unset. Existing configured locations
survive conversion. Choose the method and adjustments used by the local prayer
timetable; the source supplies calculated prayer times, not mosque Iqamah times.

The default presentation puts the next prayer time in large text, the prayer
name below it and the location underneath. Wide/Large layouts place a minutes
label and large countdown at the right; compact layouts put the countdown below
the main text. This column is inside the notification's content, not an OS shell
badge whose position the application controls.

`PrayerTimes.TwentyFourHour=0` selects AM/PM times; its default is `1` for 24-hour
times. `PrayerTimes.ShowTimetable=1` retains the five-prayer timetable on Large
tiles; set it to `0` for the clock-focused layout without the timetable.
`PrayerTimes.ShowSunrise=1` includes available sunrise information. These
display changes use cached schedule data instead of fetching a schedule on
every countdown update.

## Games and saved accounts

Each game tile has an explicit `Account` reference: `Notes.Resin`, `Notes.Stamina`
or `Notes.Charge` for a converted game account, or `Account.<id>` for a named
saved account. Its `Notes.UID` and `Notes.Server` can override the selected
account for that tile. An explicit blank server requests automatic detection
from the tile's UID. The `Notes.Region` alias is also understood.

Use the tile's **Sign in to HoYoLAB inside this app...** command for embedded
sign-in and game-account discovery. Newly added game tiles can start without an
account reference; canceling the window does not replace their selection.
Choosing/saving an account is explicit. Local browser cleanup keeps the imported
account credentials and does not invoke website Logout.

In instance mode, **Remember sign-in** stores the `RetainSession` preference in
the selected account section. Fresh sessions are the default (`0`); retaining
one account's browser login does not silently enable retention for another
account. Tiles that deliberately select the same saved account share that
account's retention preference.

Game fetch options live under the tile's `Notes.RefreshSeconds`,
`Notes.HttpTimeoutSeconds` and `Notes.MaximumBytes` keys. `Content.NotesDetail`
selects a game-supported featured detail; the available choices depend on its
game type. A saved account can be selected deliberately by more than one tile,
while their local game UID/server and update intervals remain independent.

## Photo galleries and shutdown

The existing gallery coordinator continues to own photo reservations, folder
groups, queue windows and the `.gallery-ledger` sidecar. Gallery setup is reached
from Add tile and from **Tiles > Photo galleries**. Keep the profile and ledger
together. The migration does not rebuild ownership, change gallery group/tile
IDs or create duplicate photo pools.

Gallery membership and ordinary tile demand follow the known Start pins. Old
global `Content.Enabled` and `Tiles.Enabled` switches do not disable instance-mode
gallery updates. Configured gallery appearance still participates in logo
refresh detection. Separate folder groups retain the gallery engine's existing
queue/order/ownership rules; they are not ordinary `Tile.<id>` RSS-style runtime
objects in this implementation.

Each tile's `Delivery.ClearOnShutdown` controls cleanup of its owned native
notification content on a graceful exit, including gallery rows. It does not
unpin Start entries, delete source photographs, remove settings or erase
ownership history. Historical gallery owners absent from current configuration
retain the existing application's shutdown preference.
Graceful shutdown and forced termination remain distinct: code cannot guarantee
cleanup after the process is forcibly killed or the operating system stops it.

## Conversion, idle work and validation scope

Before conversion, DesktopStub preserves the original text in
`<profile>.before-tile-instances.bak` when that backup does not already exist.
It stages and validates the complete new document before the normal atomic INI
commit. Source IDs such as `smtc`, `rss`, `genshin`, `hsr`, `zzz`, `discord` and
`prayer` retain their `AipSource_...` identities. Account credentials remain
referenced in their existing account sections rather than copied into every
tile. Invalid legacy mappings refuse conversion instead of silently replacing
the selected content.

Configured but unpinned ordinary secondary tiles do not start their providers.
No active wallpaper demand means no wallpaper observation/capture work for an
unused Wallpaper type. No active RSS demand means no RSS polling. SMTC resources
are shared across media consumers; other provider state and refresh deadlines
remain isolated by instance. Explicitly enabled indicator plugins continue
independently of tile demand. These choices reduce unnecessary background work;
they do not promise a fixed historical RAM number or zero CPU during an actual
fetch, image decode or native notification submission.

Private tests cover parsing, large instance lists, independent provider settings,
conversion failure/idempotency, menu pagination/dispatch, named RSS layouts,
repairing invalid fields and gallery metadata capture. Those tests do not pin
real tiles, sign in to accounts, exercise hardware or prove pixel placement in
the user's native Start renderer. Start's native layout, Windows pin dialogs,
Battery Saver deferral and actual lock/resume behavior still need ordinary
Windows acceptance testing.

Legacy composition presets and NowPlaying donor-composition imports are refused
with setup guidance when `Tiles.Instances` selects the new model. Use Add tile
or instance-prefixed `--set` fields rather than writing an unused global content
list. Legacy profiles without the instance marker retain their prior behavior.
