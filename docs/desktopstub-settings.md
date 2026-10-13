# DesktopStub settings

Right-click the tray icon. **Update tile now** refreshes the tile immediately.
Keep **Group settings into submenus** checked for the shorter category view;
uncheck it for the existing flat view. Both views expose the same controls.

| Category | Use it for |
| --- | --- |
| **Tile contents** | Enable composed contents, add/name/edit entries, change their order, or apply a preset. |
| **Sources and accounts** | Wallpaper detection/fitting/capture, RSS feed setup, Notes accounts, Discord presence, Caps indicators and ASUS patterns. |
| **Tile display** | Live Tile versus static delivery, native template/branding, text delivery, default text, generated asset sizes and DPI scales. |
| **Updates and cycling** | Rotate enabled contents, set time per content, check source updates, follow wallpaper changes and control initial generation. |
| **Application** | Launch at sign-in through the user Startup folder or the separate Windows startup task; tray/console visibility and notifications. |
| **Diagnostics** | Enable/configure logging, inspect the log location and PowerShell status. |
| **Advanced** | Package registration/Windows compatibility, manifest fields, cache/pre-cache, timing, memory, shutdown, PowerShell and file-save behavior. |

## Build a tile content

Open **Tile contents > Content list**, then select an entry. Its controls are:

- **Use this content** and **Name**: enable it and give it a recognizable name.
- **Background**: choose wallpaper, an image, media artwork, a solid background,
  or **Nothing / transparent**. The last removes DesktopStub's background while
  Windows controls the tile plate; see [transparency and delivery](transparent-tile-background-2026-10-13.md).
- **Text sources**: choose providers; **Edit source order** determines how their
  fields are combined.
- **Custom text**: primary, secondary and badge fields contributed by CustomText.
- **RSS headline and Notes detail**: choose the feed item or game/resource/detail.
- **Move earlier/later in cycle**: change this entry's position.

The content list shows disabled entries explicitly. **Add content** adds another
slot; the list-size control remains available at the bottom. Shrinking the count
does not erase the unused INI sections.

Sources are ingredients within an entry. Entries are separate items in the
rotation. Wallpaper plus RSS text in one entry keeps the wallpaper underneath
the text. A second entry can independently show Discord or media information.

## Text and delivery

Under **Tile display > Live Tile**, **Live Tile updates** selects Windows
notifications, **Static tile updates** selects regenerated tile assets, and
Automatic retains the existing target-dependent behavior. The mode switch still
uses the existing configured relaunch behavior.

**Text delivery** offers automatic text (native when available, with bitmap
fallback), forced Overlay inside the tile image, or off. Turning native Live Tile
delivery off can still leave bitmap text enabled. **Default text (without tile
contents)** is the older standalone text configuration; its menu explains when
the enabled content entries supply the text instead.

For the Windows 10 Adaptive template, automatic bitmap fallback now keeps the
selected background and all applicable primary, secondary and badge fields.
The medium secondary field stays visible when native Live Tile updates are
disabled. Overlay forces bitmap delivery using the selected template, so it
also retains every adaptive field. Explicit Windows 8.1 presets and older
targets retain their established geometry. The adaptive fallback follows native field styles and
grouping but approximates shell margins/rasterization; see the
[adaptive fallback guide](adaptive-static-text-gap-2026-10-13.md).

**Tile display > Text on tile sizes** controls Medium, Wide and Large text for
both composed sources and default text. These controls keep the existing
`TileText.ApplyTo*` INI keys but no longer sit inside the default-text-only group.
Display enum and boolean values use the same validation through `--set`, dedicated
flags and INI profile checks. The explicit host catalog covers four display
enums and 58 existing boolean keys, including logging, tray/console visibility,
notifications, Startup preferences, text sizes, asset switches, and capture/cache
options. Integer timing/limit fields remain integers. Invalid modes are rejected
rather than silently changing to Automatic. Boolean words such as `true`,
`false`, `yes` and `off` are accepted for these typed switches. `--tile-text ""` clears only
the primary field, like `--tile-text-clear` and the tray editor; it preserves a
still-enabled secondary or badge. Use `--tile-text-disable` to hide all default
text explicitly.

**Updates and cycling > Who rotates contents** chooses the driver:

- **DesktopStub timer** keeps the configured interval and preserves existing
  profiles' behavior.
- **Windows when available, otherwise DesktopStub** uses Windows' Live Tile queue
  when the layout is eligible, with the app timer as fallback. New built-in
  presets select this automatic driver.
- **Windows Live Tile queue** requests Windows rotation. Up to five enabled
  items can be queued; Windows chooses their display order and duration. Static
  delivery and layouts with visibility-dependent physical indicator or sending
  Discord actions fall back to the app timer with an explanation.

The **App / fallback cycle interval** applies when DesktopStub rotates entries.
It cannot set Windows' native queue duration. Source refresh and rotation remain
separate preferences. Preserved inactive slots do not count against the five
enabled-item queue limit. The same driver is `Content.CycleDriver` in the INI and
`--cycle-driver DesktopStub|Auto|Windows` on the CLI.

Each provider keeps its own fetch interval where applicable. **Updates and
cycling > Check for source updates** is the host's scheduler cadence; it does
not replace RSS/Notes/Discord's individual refresh settings. Existing warnings
for expensive registration cycles and potentially skipped native notifications
still apply.

The `--no-monitor` command-line option disables ongoing wallpaper, fit and DPI
observation. It keeps content source refresh/cycling and later profile edits
responsive. Switching explicitly from contents to wallpaper can restore the
wallpaper once.

## Profiles and compatibility

**Tile contents > Presets and imports** separates three different actions.
The old “Quick layouts” label did not mean saved named layouts or plugins.

| Action | What it does |
| --- | --- |
| **Wallpaper only** | Adds one active wallpaper content with no text source. |
| **Wallpaper with RSS headlines** | Adds wallpaper-backed RSS entries up to the configured feed item limit and cycles them. Set the feed URL under Sources and accounts first. |
| **Wallpaper and now playing (two-item cycle)** | Adds two separate items: wallpaper with your existing custom text, then transparent SMTC text. It copies the current wallpaper caption; no personal text is built into the preset. |
| **Compatibility modes** | Retains the older Wallpaper/RSS source switches. These turn off numbered tile contents but keep their saved sections for later re-enabling. |
| **Import old NowPlayingTile profile (replace contents)** | Converts a donor profile and replaces every saved Content.* section, including unused entries, after an explicit replacement summary and confirmation. |

Built-in presets append their entries after the highest saved slot, make those
new entries active, and leave earlier entries saved but inactive. Re-enable
earlier entries in **Content list** when needed. Reapplying an unchanged active
preset is a no-op. Edited entries are not silently reused. If there are not
enough free slots within the 32-entry limit, the preset is rejected without
overwriting saved contents. Startup/account settings and cycle/source intervals
stay configured; preset changes to cycling/text delivery are shown before
application.

The compatibility Wallpaper mode changes only `Settings.ContentSource=Wallpaper`
and `Content.Enabled=0`. Its RSS counterpart changes the source to `RssFeed` and
also sets `Content.Enabled=0`; the host creates wallpaper/RSS cycle entries in
memory using the existing feed, timing and text preferences. Neither rewrites
the saved Content list. Preset templates can still affect whether a wallpaper
is visible behind text; choose Adaptive or explicit bitmap Overlay as appropriate.

NowPlayingTile import has different semantics: Text, Artwork, Combined or Cycle
creates one or three media entries, updates source refresh/text delivery and the
donor's tray/menu preferences, and removes the old numbered sections. Its
Startup preference is deferred, and the host's cycle interval is retained.
The GUI asks before this replacement. The explicit CLI import performs the same
full replacement without an interactive prompt; save the current INI before
using it if the existing layout must be kept. Notes, Caps and ASUS imports stay
beside their respective source settings.

The equivalent CLI presets are `--preset Wallpaper`, `--preset WallpaperRSS`,
and `--preset WallpaperSMTC`. Presets are expanded from the fresh INI inside
the mutation lock; explicit `--set` and other setting flags apply afterward,
regardless of argument order. The last `--preset` selects the requested bundle.
Donor imports and advanced Discord profile edits require a separate command
because they have different replacement semantics. Configuration-only examples:

```cmd
DesktopStub.exe --ini DesktopStub.ini --configure-only --preset WallpaperSMTC
DesktopStub.exe --ini DesktopStub.ini --configure-only --preset WallpaperSMTC --cycle-driver DesktopStub
```

The first command chooses automatic Windows/app rotation. The second overrides
that preset choice with the existing app timer. Both preserve saved content
sections and can initialize a new INI file. They configure without fetching
providers or changing Startup. Offline rendering selects from the exact
committed preset snapshot, rather than an earlier preview.

Discord shows whether it is preview-only or sending. Its advanced editor groups
fields by activity/connection, text layout, images, filters, buttons, away status
and transports. Dormant inline fields remain editable so they can be repaired
before returning from an advanced profile.

Manifest editing now groups identity/publisher, name/appearance,
application/permissions, Windows compatibility, image assets and Live Tile
helpers. These controls are under Advanced rather than ordinary tile appearance.

The category organization preserves existing INI keys, command IDs and Startup
policy. The new preset and cycle-driver options extend the configuration as
described above. Old stock UI labels upgrade automatically; customized
labels stay intact. New category names are configurable in `[Strings]` through
the normal UI-string defaults. Account values stay masked in menus; existing
plaintext-storage risks and setup guidance still apply.

Masking a menu value does not protect its INI storage. Account tokens are readable
by default, and someone who can read those files may be able to use the account.
Keep profiles, backups and crash dumps private. Existing protected-credential
imports remain supported where available; the settings reorganization adds no
automatic encryption or credential migration.
