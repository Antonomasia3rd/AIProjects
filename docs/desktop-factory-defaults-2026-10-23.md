# Desktop fallback and fresh defaults, 23 October 2026

The independent tile metadata incorrectly mapped both Wallpaper and Gallery to
the `gallery` artwork topic. Consequently the primary Desktop's disabled/static
assets could contain the Photo icon. Wallpaper now maps to `desktop`, while
Gallery keeps `gallery`.

The main Desktop identity uses its original embedded Desktop bitmap and original
size/vertical positioning through `RenderDesktopIconAsset`. The topic catalog
also has a distinct Desktop glyph for independent wallpaper/topic artwork:
Microsoft PC1, U+E977, selected through the existing verified Segoe Fluent Icons
then Segoe MDL2 Assets fallback. If those fonts/glyphs are unavailable, an explicit
monitor/desktop vector is drawn. The Photo glyph remains U+E91B. No system font
files were copied or bundled.

`Assets.GenerateIconWhenDisabled` (legacy
`Settings.GenerateDesktopIconForDisabledEntries`) continues to control fallback
visibility. Disabled fallback icons remain transparent. Asset-size and scaling
policies are preserved. The topic change contributes to the generated asset key,
so the old Photo rendering cannot be reused for Desktop. Windows may still need
the main package's static assets/manifest refreshed to release its cached logo;
that is separate from refreshing secondary-pin metadata.

Only a genuinely new INI template gets this initial tile collection:

```ini
[Tiles]
SchemaVersion=1
Instances=desktop

[Tile.desktop]
Type=Wallpaper
Name=Desktop
Primary=1
Content.Background=Wallpaper
Delivery.Mode=LiveTile
Branding=NameAndLogo
```

No optional source is enabled or pinned automatically. Other tile types remain
available through Add tile. Both sign-in mechanisms remain off until the user
chooses one. The initial collection is deliberately not in the missing-key
defaults array: upgrading an existing profile must not replace its source list,
accounts, gallery history or preferences. Existing legacy-profile migration is
unchanged.

The v24 tag was checked at `DesktopStub/src/ga_config_defaults.inc` and
`ga_tray_menu.inc`. It used `ExperimentalLiveTileUpdate=Auto`, with choices Auto,
Registration and LiveTile; there was no separate `RegistrationMode` setting.
`Disabled` was an old Live Tile mode spelling for static/Registration delivery.
The fresh profile's explicit LiveTile preference expresses the request to stop
using repeated static registration for wallpaper updates. It does not remove
Windows' one-time package identity prerequisite for native Live Tile APIs.

## First installation on a VM

Use Windows 10's Start menu/screen, or a restored legacy Start interface on
Windows 11 that implements Live Tiles. The standard Windows 11 Start menu
[does not show Live Tiles](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/chaseable-tile-notifications). Keep the normal Windows10 manifest target for this setup;
the Windows8/Windows81 targets are separate compatibility experiments.

Open a terminal in the permanent, writable folder containing the extracted
build/release files and run:

```powershell
.\DesktopStub.exe --setup-desktop
```

This explicit action prepares the main Desktop assets and registers its
full-trust app identity in the background. On success, DesktopStub restarts with
that identity. An ordinary first EXE launch alone does not install it. If the
EXE is already running unpackaged, use **Tiles > Set up Desktop tile...**; the
command and menu perform the same setup. Windows Developer Mode or sideloading
support may be required by the VM's policy. If setup fails, its notification/log
reports the error and the menu can retry; saved settings and existing pins are
retained.

After successful setup, find **Desktop** in Start's **All apps** list and pin it
to Start. The executable is named `DesktopStub.exe`, but the default registered
app display name is **Desktop** (`Settings.ManifestDisplayName`); an existing
custom manifest can provide another name. Choose Medium, Wide or Large, keep
Live Tile enabled, and leave DesktopStub running to follow wallpaper changes.
The setup action does not automatically pin every optional source.

Normal later launches can use:

```powershell
.\DesktopStub.exe
```

The fresh Desktop's `Delivery.Mode=LiveTile` means ongoing wallpaper changes use
native notifications rather than repeated static Registration updates. It still
requires the one-time main identity registration above. Both **Run at sign-in
(Startup folder)** and **Run at sign-in (Windows startup task)** default to off;
enable one explicitly under **Application > Launch at sign-in** when desired.

Setup is a package operation and cannot be combined with `--render-only`,
`--configure-only`, settings batches or Startup switches. No real package or
Startup setting was changed by the implementation's offline validation.

Regression coverage adds factory collection/mode/branding/startup assertions,
distinct Desktop/Photo metadata and glyph mappings, byte-exact primary bitmap
comparison against the old renderer for Medium/Wide/Large, and hidden-icon
transparency. Legacy fixture templates explicitly remove the new initial typed
collection so their existing numbered-content compatibility assertions remain
about that model, rather than accidentally testing modern defaults.

The portable topic gate passed 697 checks. The bounded offscreen font/asset gate
passed 286,390 checks, selected Segoe Fluent Icons for the Desktop topic, and
generated transparent, correctly sized artifacts. Both executables were scanned
before execution. The new primary/default host assertions run in the Core host
fixture; their final result belongs to the parent integration gate.

The copied-binary RepoTools smoke now creates a genuine existing legacy INI only
for cases intentionally exercising legacy Content/TileText pixel contracts.
No pixel assertions or tolerances changed. The external-provider rejection case
also verifies its exact preseed bytes remain unchanged and its error identifies
the external-provider guard. A separate new configure-only profile asserts one
Desktop/Wallpaper primary, explicit LiveTile delivery, NameAndLogo and both
startup mechanisms off, without creating a manifest/log/gallery ledger.

Auditing that distinction found a real fresh `--preset` issue: the old typed
profile guard ran before preview created the new factory document, so a missing
INI could pass the guard, write ignored global numbered contents, and exit with
success. The CLI owner added a typed-document guard at preset application itself,
covering both preview and atomic commit. The new smoke asserts actionable
rejection before creating a missing profile and exact-byte preservation of an
existing factory profile. `RepoToolsDesktopFactoryOct23.exe` was compiled from
the current source and scanned; the parent runs it against the final product.

Primary glyph reference: [Microsoft Segoe MDL2 icon table](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-ui-symbol-font).
