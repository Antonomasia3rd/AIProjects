# DesktopStub released-profile upgrade evidence, October 26

This pass compares actual released `g_defaults` entries from **DesktopStub-v1
through DesktopStub-v25**, rather than assuming the current factory INI is a
representative old profile. It fixes demonstrated conversion losses; it is not
a claim that every native package, account, third-party edited INI or shell
integration is now accepted on every Windows release.

## Historical source

The 25 release references contain three distinct literal default sets. Versions
v1–v22 contain 147 literal rows; v23–v25 contain 128. The exact release commit
identities and deduplicated rows are in
[`DesktopStubLegacyDefaults.h`](../tools/fixtures/DesktopStubLegacyDefaults.h).
These are factory values from this repository, never personal profiles or tokens.

For v1–v24, the relevant source is
`DesktopStub/src/ga_config_defaults.inc`; v25 uses
`dependencies/DesktopStub/ga_config_defaults.inc`. In those releases the main
Desktop used wallpaper and optional global `[TileText]`. They did not contain
the later numbered-content/SMTC/Notes tile schema. The literal delivery default
was **Auto**, whose runtime behavior depended on package identity; it was not
literally the current `Registration` preference. The user's requested new
Registration-first factory default is a separate, deliberate policy change.

The comparison also found all 114 literal long CLI flags in v25 still present
in the current parser. Retaining flag spelling alone does not prove its effects
remain correct after configuration is projected into independent tiles.

## Repairs

Previously, defaults were filled before migration inspected the file. An old
Desktop-only INI therefore appeared to contain modern provider sections. The
converter then created eight source entries and did not project the old global
TileText into the visible Desktop. Editing the old global fields could succeed
while leaving the new per-tile snapshot unchanged.

`EnsureIniDefaults` now inspects and stages the **original saved schema** under
its existing mutation lease, before new defaults are inserted. The prospective
typed document is validated, an immutable
`<profile>.before-tile-instances.bak` is created, then defaults and conversion
are committed together. Failed validation/backup/commit does not write a
partially converted document. Existing backup files must be ordinary readable
files and are never overwritten. Encoding normalization can precede this backup;
the receipt is the original pre-default document at the conversion boundary.

A pre-source Desktop profile converts to **one primary Wallpaper tile**. It
preserves its display name, global text and Enabled state, explicit native
branding, Auto/boolean delivery choice, image/scale/cache/fitting policies and
original main-package Registration route. Text retains the old trimming
semantics. Original global fields, comments, unknown settings, opaque protected
values, account references and gallery ownership fields remain in place.
Migration does not decode, copy or fetch account credentials.

`State.LegacyDesktopSettingsMigrated=1` records that compatibility projection.
For marked profiles only, the shared setting writer mirrors old global Desktop
assignments into the current primary Wallpaper tile:

- `TileText.Enabled` controls its `TextMode`; text/secondary/badge values control
  its `Content.*` fields. Legacy `TextPreset=Inherit` becomes `Default` locally.
- Global delivery, cache, asset, scale, fitting and PNG fields represented by
  `TilePolicyFields` update the corresponding per-tile field.
- `Settings.LiveTileBranding` updates the primary tile's branding.
- App-wide Startup/scheduling/instance settings and account fields are not
  redirected. Existing identity-change approval rules remain separate.

The original global assignment is retained for old-script readback and rollback.
Explicit `Tile.*` edits remain explicit per-tile edits; they do not rewrite
unrelated global creation defaults. Batch order is preserved: the last
assignment wins, including mixed old/new syntax. A marked profile without an
unambiguous primary Wallpaper fails clearly instead of saving an ineffective
global-only change. Modern profiles without the marker retain modern semantics.

Some later numbered-content layouts cannot be represented faithfully as one
independent Wallpaper: numbered multi-entry/slideshow configuration, a primary
RSS/SMTC source or a multi-source text composition. The plan returns an explicit
successful **legacyModeRequired** decision, leaves that document unchanged and
keeps the existing legacy engine. It does not flatten a playlist, discard text
sources or turn a successful old startup into a conversion failure. A separate
profile or a future explicit reviewed conversion is required to add independent
tiles to a deferred complex profile; adding one must not silently replace its
legacy primary.

## Validation

`BuildLegacySettingsMigrationTests.cmd` builds a `/W4 /WX /O2` fixture using the
production in-memory INI parser/writer and migration model. The historical and
customized matrix passed **4,010 checks** after a clean Defender scan. The
existing tile-instance regression passed **69 checks**, also after a clean scan.
Logs are `build/review-validation/legacy-release-migration-agent-oct26-*` and
`legacy-instance-regression-agent-oct26-*`.

`LegacySettingsMigrationHostTests.inc` supplies the parent's combined-host gate:
actual defaults initialization for every distinct historical set plus customized
v25, exact pre-default backup, the real setting adapter, mixed assignment order,
missing-primary refusal and deferred complex contents. Its entry point is
`TestLegacySettingsMigrationHost()`. Combined native-host results are recorded by
the parent after the gate runs; source/model results alone do not establish it.

No active INI, account, photo ledger, package registration, Startup setting,
hardware provider or user resident was changed by this audit. Native old-package
adoption/re-pinning, old protected-account acceptance, Windows version behavior,
rollback through an older executable and arbitrary customized old INIs remain
separate acceptance work.
