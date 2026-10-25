# Separate Now Playing information and album artwork

Two independent **Now playing** tiles can show one Windows media session side by
side. Keep the first on **Source settings > Presentation presets > Information**
and resize it Wide. Add a second Now playing tile, give it an artwork name, choose
**Album art only**, pin it, and resize it Medium. Arrange them manually in Start.

These are presets over existing editable settings, not a new provider/type:

| Preset | `Content.Background` | `TextMode` | `Branding` |
| --- | --- | --- | --- |
| Information | `Transparent` | `Auto` | `NameAndLogo` |
| Album art only | `SMTCArtwork` | `Off` | `Name` |

Each preset saves those three fields atomically and retains name, click action,
refresh interval, source controls, custom fonts and delivery preferences. The
current tile type is checked again before committing. Both tray and Properties
expose the presets and an independent Image source selector. Appearance and
text controls can customize the resulting fields afterward; choose Branding
None for an image without the tile-name footer.

The same fields work in `[Tile.<id>]` and through CLI, for example:

```powershell
DesktopStub.exe --set 'Tile.<id>.Content.Background=SMTCArtwork' `
  --set 'Tile.<id>.TextMode=Off' --set 'Tile.<id>.Branding=Name'
```

Replace `<id>` with the actual artwork tile ID. This edits an existing tile;
creating the second tile and the native pin confirmation remain explicit actions.

Both demanded tiles share the existing SMTC reader and current-session snapshot.
Text Off does not disable artwork demand. The artwork frame holds a source-file
lease until rendering finishes. If the current application supplies no usable
thumbnail, the previous image is cleared to the existing blank background rather
than showing stale cover art or fetching a guessed cover from the Internet.
Artwork availability and Start's display timing remain controlled by the source
application and Windows.
