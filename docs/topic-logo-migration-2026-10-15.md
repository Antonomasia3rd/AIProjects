# Existing tile logos and native icon artwork, 2026-10-15

Later user feedback confirmed the topic fallbacks were visible, then requested
transparent plates and configurable branding. The current default appearance
supersedes the colored source-background description below; see
[tile-appearance-2026-10-15.md](tile-appearance-2026-10-15.md). Historical evidence
and the earlier validation boundary are retained here.

## Finding and scope

The user's screenshot shows the six source tiles reverting to the Desktop
monitor logo when their live notifications clear. Generating topic PNGs alone
did not establish that the existing pins had received their new static logos.
Before this change, `CreatePinTile` applied topic logo metadata only through an
explicit Pin/update action. There was no normal-startup migration for already
pinned tiles. This is a confirmed upgrade-path omission; the screenshot's exact
stored Windows metadata and visible shell cache still require local acceptance.

`RequestRefreshExistingSecondaryTileLogos(mainWindow)` is the new normal-startup
UI-thread hook. It is a no-op in inert fixtures and silently skips unpackaged
launches. The host must call it after its normal resident window/profile setup;
it must not be called by configure-only, render-only, helper or smoke entry paths.

The hook asynchronously enumerates existing secondary tiles for the current
package, filters the exact known source/action IDs, and updates their logo
properties in place. It publishes medium, wide, large, small, 44 and 30 logos.
It preserves the existing tile object, ID, name, activation arguments, branding,
background and Start placement. It never requests a new pin, recreates a missing
tile, changes the main Desktop tile or clears/sends a live notification.
Already-current logo metadata is skipped. Updates are serialized and followed
by a fresh asynchronous enumeration. The log records all six existing/read-back
logo URIs, successful updates, missing pins and metadata mismatches separately.
An API success or verified URI is not proof that Explorer renders the right
fallback image; the cleared/offline shell appearance remains an acceptance step.

Pending WinRT objects and results stay on the UI thread. Native completion
callbacks post plain completion records through the existing mailbox. Shutdown
closes the mailbox, cancels operations and releases proxies; a local shared
apartment lease protects operations against synchronous COM message pumping.
Completion processing removes its operation from the pending map before calling
COM accessors, preventing a nested shutdown from invalidating a map reference.

## Artwork and Quick Actions

Quick Actions no longer has a separate Enable pinning switch. Each action offers
its direct Pin/update and Unpin commands. Existing pins are retained. The old
`QuickActions.Enabled` key is accepted only for compatibility and no longer gates
pinning; no current INI needs to be rewritten to remove it.

Media/action/status icons prefer a verified installed Segoe Fluent Icons glyph,
then Segoe MDL2 Assets, then the existing vector fallback. The exact selected
font face and every required glyph are checked before use, so a substituted font
or missing private-use character cannot silently produce a missing-glyph box.
Glyph outlines are rasterized into PNGs; private-use characters are never sent
as tile text, where Windows would choose the font itself. No font or icon-pack
download, new resident process or mandatory DLL is introduced.

The media mappings are Play E768, Pause E769, Previous E892, Next E893,
RepeatAll E8EE, RepeatOne E8ED, Shuffle E8B1, Volume1 E993 and Volume3 E995.
Toggle composes the verified Play and Pause glyphs. Stop uses E71A, unknown uses
Help E897, and disabled repeat/shuffle overlays a slash. Resource icons use
Drop EB42 for resin, Battery10 E83F for stamina and BatteryCharging9 E83E for
charge; these are generic resource indicators, not proprietary game artwork.
The existing six fallback source topics remain distinct. Music/Chat can also use
native glyphs, while RSS and the three game topics retain vector alternatives.

Action/status/resource PNGs are monochrome with transparent backgrounds, replacing
the prior colored circular action badges. Source fallback logos retain their
topic colors. Version-two asset paths include `fluent`, `mdl2` or `vector` and
the topic key, avoiding reuse of the previous artwork URI in the Windows cache.
Valid old files are retained; live or pinned assets are not deleted by migration.
The first generation validates dimensions; later requests reuse the validated
in-process cache while checking that the files still exist.

The existing `DrawTileTopicGlyph` and `EnsureTileTopicAssets` hooks remain available
to native text/image layouts. New render-only topics are stop, unknown,
repeat-off/list/track, shuffle-off/on, resin, stamina and charge. They do not
expand the set of pinnable source/action destinations.

Microsoft references checked on 2026-10-15:

- [Segoe Fluent Icons and supported glyph table](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-fluent-icons-font)
- [Segoe MDL2 Assets, explicit-font requirements and glyph table](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-ui-symbol-font)
- [SecondaryTile.UpdateAsync and the properties it updates](https://learn.microsoft.com/en-us/uwp/api/windows.ui.startscreen.secondarytile.updateasync)
- [SecondaryTileVisualElements and logo properties](https://learn.microsoft.com/en-us/uwp/api/windows.ui.startscreen.secondarytilevisualelements)

## Validation record

Source review and `git diff --check` passed before handoff to the parent build.
This agent did not build/run binaries, query or change real tile metadata, drive
the user's media player, touch the live INI, install packages, or pin/unpin tiles.

Read-only file inspection found both local system font files:
`C:\Windows\Fonts\segmdl2.ttf` and `C:\Windows\Fonts\SegoeIcons.ttf`.
Direct cmap-table inspection found nonzero glyph IDs for E768/E769/E892/E893/
E8EE/E8B1/E993/E995/E90B/E8F2 in both files. This establishes availability of those
codes in those files; actual GDI rendering and older OS fallback are separate
checks. The implementation checks every required code at runtime.

Prepared fixtures:

- `tools/TileVisualAssetsTests.cpp`: platform-neutral topic plans, all logo sizes,
  native-font selection/missing-glyph fallback, status distinctions, source/action
  ID eligibility, exact six-property fake setters, preservation of unrelated
  metadata, stale single-size repair and idempotent migration decisions.
- `tools/TileVisualAssetsRenderTests.cpp`: Windows-only offscreen GDI+/font and
  temporary-PNG checks. It creates a fresh fixture directory, checks PNG sizes
  and transparent margins, verifies visible bounded glyphs, exercises the cache,
  and emits a topic contact sheet plus selected font names. It has no package,
  pinning, notification, provider, profile or playback integration.

Compile, Defender scan, bounded fixture results and actual shell acceptance are
pending the parent validation/install sequence. Do not describe the screenshot's
fallback defect as verified fixed until an existing, cleared/offline source pin
visibly uses its topic logo without manual repinning.
