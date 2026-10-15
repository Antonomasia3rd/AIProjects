# Source and Quick Action tile appearance, 2026-10-15

The user confirmed that topic fallback logos now appear on the existing pins,
then requested removing the colored plates and unwanted corner branding. This
revision changes secondary defaults to transparent artwork and name-only
branding. The primary Desktop tile keeps its existing settings and renderer.

## Configuration and tray controls

Each existing source section (`Tile.smtc`, `Tile.rss`, `Tile.genshin`, `Tile.hsr`,
`Tile.zzz`, `Tile.discord`) now supports:

| Key | Values | Default |
| --- | --- | --- |
| `Background` | `Transparent`, `Color` | `Transparent` |
| `BackgroundColor` | `Auto`, `#RRGGBB` | `Auto` |
| `Branding` | `Auto`, `None`, `Logo`, `Name`, `NameAndLogo` | `Name` |

`Auto` background color uses that topic's palette only when `Background=Color`.
Color values are case-insensitive, accept an optional leading `#`, and are saved
in canonical `#RRGGBB` form. Transparency is selected through `Background`, so
the color does not use a separate alpha channel. All appearance fields use the
same typed normalizer from INI, generic `--set` and tray changes.
Quote `#RRGGBB` values when editing INI text: `#` otherwise starts an inline
comment in the shared dialect. The tray/CLI writer quotes these values for you.

The same three keys under `MediaActions` control the nine static Quick Action
tiles together. Volume-step and click-action settings retain their behavior.

For example:

```ini
[Tile.smtc]
Background=Transparent
Branding=Name
BackgroundColor=Auto

[Tile.rss]
Background=Color
BackgroundColor="#204060"
Branding=None

[MediaActions]
Background=Transparent
Branding=Name
```

The tray exposes `Tiles > <source> > Appearance` and `Tiles > Quick Actions >
Appearance`. The custom-color action enables Color background explicitly; the
topic-color action changes the stored color preference. Selecting Transparent
keeps that color preference available for later use.

Branding controls the native name/corner-logo treatment; it does not remove the
central topic glyph used by the static fallback. Source content images remain
controlled by their numbered content settings. A wallpaper/image content can
still cover the tile canvas independently of the fallback background choice.

## Artwork and existing pins

Default source, action, status and resource artwork is now a white glyph on a
transparent PNG, without gradients, halos, colored plates or circular badges.
Color is an explicit solid RGB option. Light selected colors use a dark glyph
and native foreground text for readability. Branding icons retain transparent
margins for either background choice.

Assets use `Assets/TopicTiles/v3/<font>/<transparent-or-color>/<topic>/`, keeping
appearance and font variants separate from older cached artwork. Existing
referenced versions are retained. The main Desktop tile's assets are unchanged.

Pin/update and the existing-pin refresh publish every logo size, explicit native
ARGB background, foreground selection and all three name-display flags. The
normal startup migration applies the new defaults to known existing pins without
recreating them or changing their names, activation arguments, accounts or Start
placement. Appearance menu saves request a silent refresh through the main
window. Account/sign-in saves do not trigger this refresh. Concurrent appearance
changes coalesce a subsequent batch; manual pin/unpin and migration are
serialized to avoid publishing stale pin metadata.

The existing poll loop also checks one canonical fingerprint of the six source
appearance sections plus MediaActions. Changed CLI settings/resident reloads and
raw INI edits therefore reach the same native metadata refresh on the next poll,
including `--no-monitor`. The poller reads a coherent cached document and posts
one coalesced main-window notice; it never enumerates pins or calls WinRT.
Startup records its scheduled key, so an unchanged first poll does not repeat
the startup migration. Account, name and ordinary content-text changes are not
part of this key. Failed message posting remains retryable on a later poll;
native metadata errors do not cause continual same-key enumeration. An explicit
forced refresh, including reselecting an appearance option, can retry that state
without changing the fingerprint. Invalid partial INI edits are not published
as a new appearance key.

The native API exposes background colors as a Windows color and inherits the
parent color when the property is not set. The implementation explicitly sets
ARGB zero for Transparent and an opaque RGB value for Color. How a particular
Windows shell/theme renders a transparent tile canvas remains an OS behavior;
the generated glyph PNG itself has no painted background. See Microsoft's
[background-color property](https://learn.microsoft.com/en-us/uwp/api/windows.ui.startscreen.secondarytilevisualelements.backgroundcolor?view=winrt-28000).

The default live `Name` branding hides the corner logo through native XML while
retaining the tile name. Primary branding stays under `Settings.LiveTileBranding`.
The parent integration owns the live XML selection and delivery fingerprint.

## Validation and installation

The parent completed compilation, clean Defender custom scans, bounded fixtures
and the full copied-binary offline smoke. Final host/menu/configuration coverage
passed 11,714 checks in 17.7 seconds; the existing 45-second fixture deadline was
unchanged. An earlier concurrent scan/smoke run reached that deadline; its log is
retained, and the isolated run passed without relaxing the gate. Pure secondary
appearance/notice checks passed 78 assertions, asset plans passed 598, adaptive
renderer/host checks passed 5,630, Notes rows passed 16 and the no-hardware host
passed three. The offscreen artwork fixture checked every topic and explicit
dark/light plates; its 230,962 raw checks mostly include per-pixel API returns.
The exact Color boundary test found partially transparent antialiased edges;
painting the plate with Graphics::Clear corrected them. A tightened Notes-row
assertion also exposed stale synthetic error text in the fixture; its Ok-state
input now resets the resource text explicitly.

The reviewed host and broker replaced the canonical binaries after graceful
close, backups and unchanged-INI copy verification. Both installed paths scanned
clean and DesktopStub relaunched. Windows read back matching v3 transparent
logos, ARGB/foreground and name flags for all six source pins plus Previous
Track. Native shell visual acceptance remains separate; no computer input or
Start opening was performed.

- Host SHA256: 8C07419BF3EDCF51EBEEAEFAC7B288D5E4FC9C81C240C44B3E7B0CE0350DF8A6
- Broker SHA256: 6C18138443211A2F396DF0EF83EC29EC530C0F99BC4EDC0C0CCBFF738073761F
- Backup: DesktopStub/build/ReviewBackupOct15Appearance-20261015-185702.
- Relaunched canonical resident observed as PID 14872 at this checkpoint.

Accounts, hardware, player controls and actual Startup were not exercised by
the inert fixtures. No new tile, package, account or gallery setup was created.

- `SecondaryTileTests`: old-profile defaults, independent source settings,
  canonicalization, error preservation, MediaActions parity, five branding
  choices and fake native background/foreground/name-flag publication.
- The host fixture also rejects raw-INI invalid MediaActions appearance through
  startup/full-profile, tray-candidate and preset-candidate validation; a valid
  typed override repairs the field without validation mutating the saved file.
- Pure notice tests and the injected host message path cover startup suppression,
  source/action changes, unrelated-field exclusion, coalescing and failed-post
  retry. The test calls no actual window or pin APIs; only the main-window
  handler invokes the injected refresh callback after polling posts its notice.
- `TileVisualAssetsTests`: all default assets transparent, separate Color/font
  paths, solid-plate eligibility, transparent branding margins and readable ink.
- `TileVisualAssetsRenderTests`: actual offscreen PNG corner/margin checks for
  every default topic plus explicit dark/light Color variants. No shell, profile,
  pinning, provider or playback integration is invoked by that fixture.

Shared source addition: `dependencies/tile_appearance.h`. Existing tile mapping,
action normalization, artwork helpers, API migration and tray menus consume it.
