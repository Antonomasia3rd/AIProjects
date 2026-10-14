# Secondary tile management and source setup

The new `Tiles` tray category manages six stable source tiles from the same
resident/profile: Now playing, RSS, Genshin Impact, Honkai: Star Rail, Zenless
Zone Zero, and Discord. The catalog comes from `dependencies/secondary_tiles.h`;
the tray does not maintain a second ID or source list. Windows IDs are
`AipSource_` plus each catalog key.

Use **Set up and enable the six source tiles** to append missing mappings and
content fields. This explicit action enables Content and Tiles, turns cycling
off, enables mapped contents, and grows Count only when required. Content 1 stays
the primary content. Existing content fields, mappings, names, source settings,
and stored account credentials are preserved. Empty RSS/account configuration
remains a source-setup state; the initializer does not fetch or invent accounts.

Each source tile exposes update enablement, pin/update, unpin, status, source
setup, rename, and its numbered content mapping. Pin/update explicitly enables
source tile mode and the selected tile; if updates were off, cycling was on, or
the tile was paused, the UI describes those changes before saving. Windows then
handles the separate pin confirmation. An accepted asynchronous API request is
not reported as a completed pin. Repeating pin/update can update an existing
tile through the shared API. Pin availability/status is separate from source
configuration and requires the registered app; configuration remains accessible.

For Notes games, Source and sign-in settings opens a small setup menu with the
official HoYoLAB sign-in website, a manual account editor, existing cookie JSON
import, individual masked fields, refresh, and clear actions. The manual editor
stages UID and both optional replacement cookie fields together, validates and
saves once, and cancels without writing. Leaving both credential fields blank
keeps stored credentials; replacement requires both fields and clears old plain/
protected aliases through the existing account-family helper. It does not
decrypt stored credentials, automate browser sign-in, inspect browser cookies,
or copy data through the clipboard. Plain replacement inputs use masked controls
and are explicitly described as stored in this INI. Existing protected-field
editing/import routes remain available.

RSS setup reuses the existing URL/refresh/headline handlers. Discord setup reuses
the existing basic/profile creation/typed advanced editor, including private
replacement policy. Now-playing setup explains the Windows media-session source.
The root activation route can call
`OpenSecondaryTileSetup(HWND, const std::wstring& catalogKey)` for a clicked tile.
No source setup UI is shown during menu construction or offline validation.

Implementation is `dependencies/DesktopStub/ga_secondary_tile_tray.inc` with
minimal include/menu/dispatch hooks in `ga_tray.inc`, `ga_tray_menu.inc`, and
`ga_tray_dispatch.inc`. The pure staging entry is
`StageSecondaryTileDefaultLayout(std::wstring&, bool activate, std::wstring&)`.
The saved batch uses the existing native fresh INI mutation and validates both
the content model and secondary mappings before publication. Menu status reads
already prepared snapshots; it does not refresh providers to populate a menu.

The integrated build and inert tests passed on 2026-10-14. It is installed on
the user's laptop, with the seven-content profile migrated into the main INI
and the six copied-host packages retired. Actual Windows secondary pin/unpin
confirmation and visible source sign-in acceptance are still pending user
testing. See multiple-tiles-2026-10-14.md for the setup steps and
one-host-tiles-validation-2026-10-14.md for exact validation scope.