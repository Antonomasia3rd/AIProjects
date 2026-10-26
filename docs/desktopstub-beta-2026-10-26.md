# DesktopStub v27 beta 1 — October 26

Historical beta 1 record. [Beta 2](desktopstub-beta2-2026-10-26.md) is the newer
delivered build and records the latest native validation and fresh reset.

Fresh installations start with one Desktop wallpaper tile using Registration
on the **main package**, optional sources/plugins and login startup off. Run
DesktopStub.exe normally, allow its first image/registration update to finish,
then pin **Desktop** from Start > All apps. The explicit `--setup-desktop`
command is now an optional **Live Tile** host setup, not the ordinary first-run
step. Existing explicit delivery preferences are preserved.

The literal v1–v25 default was Auto; an ordinary unpackaged launch used the
original main Registration path. The new factory makes the requested
Registration choice explicit. Primary `Delivery.PrimaryRegistration=Main`
uses that original entry; Separate preserves the independent package option.
Older typed profiles without this new preference retain Separate semantics.

## Upgrading

Close the old resident and retain a backup of its folder/INI. Use the complete
matching beta payload; preserve the old INI for an upgrade, rather than starting
with a new one. All 25 released factory sets and customized representative
settings are tested. Conversion is staged before adding defaults and keeps an
immutable `.before-tile-instances.bak`. Old Desktop text, branding, cache/assets,
delivery, comments and opaque protected values are retained. Old CLI settings
remain connected to a migrated primary Desktop via an internal marker.

Complex numbered/cycling/multi-source compositions remain on the legacy engine
instead of being flattened. Adding independent tiles to such a profile is
refused with guidance to create a separate profile. This is deliberate data
preservation, not complete conversion of every arbitrary historical INI.
Protected-account acceptance and custom system-wide deployment remain beta
acceptance work. See [the migration audit](desktopstub-legacy-upgrade-2026-10-26.md).

## Windows 10 1809 and icons

Independent Registration manifests formerly required 19041/uap10. The new
launcher obtains validated activation data from its owned package-root binding
instead of uap10 parameters, allowing the 15063 main Desktop Bridge baseline.
Old explicit activation arguments remain supported. No pin/unpin is automatic.
This fixes a confirmed compatibility restriction; it does not prove the exact
cause of every screenshot's missing image. Actual 1809 Start display still
needs native acceptance. Run the supplied read-only image collector on that VM
if needed; review its public paths/metadata before sharing a report.

Per-tile `Artwork.IconFont=Auto|MDL2|Fluent|Vector` is exposed in Appearance;
Gallery fallback controls are in its advanced tile settings. Auto prefers MDL2
on Windows 10 and Fluent on Windows 11, verifies the installed family/glyph,
then falls back safely. A blank cached icon is regenerated. Supplied album art,
HoYo resource artwork and custom EXE/image artwork are not icon-font glyphs.

## Multiple media sources

Now Playing > Source settings can follow **Windows current session** (default)
or a chosen application from the last refreshed SMTC catalog. Manual fields are
`SMTC.SessionMode=Current|Application`, `SMTC.SessionAppId`, and
`SMTC.SessionInstance=0..64`. Zero prefers Windows-current when it belongs to
that app, otherwise the first matching session. A positive number selects that
occurrence within the application's Windows enumeration. Occurrence order is
not a permanent session identity and can change when sessions are removed.

Missing applications/occurrences never fall back to controlling a different
app. Playback controls use the same source preference; system volume actions
remain system-wide. Sources are read on demanded workers, with unused selectors
stopped/released. Tiles with the same selection share one metadata/artwork
snapshot and aggregate artwork/icon demand. Configure the same selection on
an Information tile and an Album art only tile to keep them paired. Legacy
SMTC settings expose the equivalent fields. All choices also work through CLI
and INI; no extra source observer starts just to populate a menu.

## Distribution and remaining checks

Use the clean x64 beta folder/ZIP, not the developer build tree or another PC's
generated INI/profile/manifest/cache. Keep all three matching EXEs for profiles,
independent Registration and broker features. The core primary Desktop route
does not use an independent launcher. Optional embedded-browser features need
their normal Windows WebView2 runtime; a basic Desktop tile does not.

See [current open findings](desktopstub-open-findings-2026-10-25.md) for incomplete
Calendar parity, lyrics, timetable, fitness, Window Capture, Widgets, charging
ETA/Smart Charging, the raster investigation and native acceptance. Performance
is completed for now based on the user's October 26 feedback; do not reopen its
historical investigations without a new regression. New UI/provider requests
are checkpointed in [the latest resume note](desktopstub-next-pass-2026-10-26.md).
Injected/offscreen gates are not proof of real sign-in, hardware, Start image
display or every multi-user/mixed-delivery deployment.

## Delivered validation and reset

The source change is `1950eee`. The shareable ZIP is
`DesktopStub/releases/DesktopStub-v27-beta1-x64.zip`, SHA256
`E48C80FBC8C30013C209DE01CBCF0ED5893F0F526B3E2F2FAF2F9DEB34D96D12`.
All three matching EXEs carry version 27.0.0.1 and scanned clean.

Final gates: 2,040 combined host checks, 2,285 source checks, 180 synthetic/native
SMTC contracts with system media reads disabled, 4,010 historical migration
checks, 69 existing instance checks, 1,644 icon-policy checks, 161 native icon
checks, 774 existing asset checks, 398,520 pixel/render checks, 39 registration
model checks and 60 activation-binding checks. Private compiled CLI configuration
round-tripped the new source/font fields and rejected an invalid occurrence
without writing or assigning a resident identity.

After pushing, the user-authorized reset archived the old project build,
validation build, installation profile coordinators and owned Registration
state at `artifacts/desktopstub-reset-20261026-175309-dad291`. It removed only
matching current-user packages. An ordinary first launch was then tested for
75 seconds with the broker and launcher temporarily absent: it retained
Registration/Main, generated Medium/Wide/Large wallpaper assets and registered
exactly one main package at its own directory, with a successful four-second
generation/registration batch. No explicit setup command was used.

The trial resident was stopped and its package/configuration removed too.
Both helpers were restored. The final project build contains seven release
files, no INI/generated manifest/images, and no running resident. The shared
installation profile directory is absent. The previous developer output is
recoverable in the ignored archive, rather than mixed into the beta payload.
That is the historical end-of-reset snapshot, not a current-state guarantee:
the user has since relaunched the beta and edited preferences. Preserve the
present INI, package and resident state rather than repeating that reset.
The core Desktop copy-and-run path is therefore verified on the host; the full
three-file payload remains recommended for the additional features. Real 1809
Start image display and arbitrary account/custom-INI acceptance remain open.
