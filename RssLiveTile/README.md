# RssLiveTile

`RssLiveTile.exe` is a native Win32 resident app that updates a Windows Start Live Tile from an RSS or Atom feed while the app is running. It polls periodically, queues up to five recent entries, and opens the corresponding HTTP(S) article when Windows activates a queued tile entry.

It follows the shared repository baseline used by `DesktopStub`: the app writes its INI and log beside the executable, generates local Appx package files under `build`, registers a loose Desktop Bridge package, and relaunches itself through the packaged Start identity so `Windows.UI.Notifications.TileUpdateManager` can update the tile.

## Requirements

- Windows 10 with Live Tiles enabled, or Windows 11 with a third-party Start implementation that still displays Windows Live Tiles. The native Windows 11 Start menu does not display Live Tiles.
- Visual Studio Build Tools with the C++ workload.
- Windows 10/11 SDK with C++/WinRT headers and WinRT metadata.
- Developer Mode or sideloading support may be required for loose Appx registration, depending on Windows policy.

## Build

From the repository root:

```cmd
RssLiveTile\BuildRssLiveTile.cmd
```

Syntax-only check:

```cmd
RssLiveTile\BuildRssLiveTile.cmd check
```

The check target also compiles and runs parser/configuration regression tests for namespaced RSS, namespaced Atom, alternate links, malformed XML, tile activation arguments, and declared feed encodings.

Output:

```text
RssLiveTile\build\RssLiveTile.exe
```

If `build\RssLiveTile.exe` is running, close it before rebuilding so the compiler can overwrite the output.

Run the project-specific binary smoke test after building:

```cmd
RssLiveTile\SmokeRssLiveTile.cmd
```

## Run

```cmd
RssLiveTile\build\RssLiveTile.exe
```

On first launch from `build`, the unpackaged process creates:

- `RssLiveTile.ini`
- `RssLiveTile.log`
- `AppxManifest.xml`
- `Assets\*.png`

It then registers `AppxManifest.xml`, launches the packaged `shell:AppsFolder\...!App` entry, and exits the unpackaged bootstrap process. Live Tile updates happen from the packaged process because unpackaged Win32 processes do not have the package identity required by `TileUpdateManager`.

The tray menu exposes refresh, configuration reload, the latest article, settings/log shortcuts, asynchronous package registration/launch actions, and exit. Package maintenance is started through separate command-line helper processes so the resident window remains responsive. Command-line setting changes reload an existing resident automatically.

## Settings

Settings are stored at:

```text
<exe folder>\<exe name>.ini
```

Default settings:

```ini
[Settings]
"FeedUrl" = "https://blogs.windows.com/windowsexperience/feed/"
"UpdateIntervalSeconds" = "300"
"TileRefreshSeconds" = "900"
"MaxItems" = "5"
"ShowTrayIcon" = "1"
"BootstrapPackageOnLaunch" = "1"
"UserAgent" = "RssLiveTile/1.0"
"HttpTimeoutSeconds" = "30"
"MaxFeedBytes" = "1048576"

[Manifest]
"DisplayName" = "RSS Live Tile"
"Description" = "RSS feed Live Tile updater"
"IdentityName" = "RssLiveTile.App"
"Publisher" = "CN=RssLiveTile"
"Version" = "1.0.0.0"
"BackgroundColor" = "#005A9E"
```

`MaxItems` is capped at 5 because Windows Live Tile notification queues rotate at most five tile notifications per app.

## Command Line

```cmd
RssLiveTile.exe --feed-url https://example.com/feed.xml
RssLiveTile.exe --interval 600
RssLiveTile.exe --set Settings.FeedUrl=https://example.com/feed.xml
RssLiveTile.exe --once
RssLiveTile.exe --register
RssLiveTile.exe --launch-packaged
RssLiveTile.exe --unregister
RssLiveTile.exe --regenerate-manifest
RssLiveTile.exe --exit
```

Supported options:

- `--help`, `-h`, `/?`: show help.
- `--ini <path>`: use an alternate INI file. Alternate INI instances have separate single-instance scope.
- `--set Section.Key=Value`: set and save an INI value.
- `--feed-url <url>`: set and save `[Settings] FeedUrl`.
- `--interval <seconds>`: set and save `[Settings] UpdateIntervalSeconds`.
- `--once`: fetch the feed once, update the tile, and exit. Unless bootstrap is disabled, an unpackaged invocation registers and relaunches itself with package identity automatically.
- `--register`: regenerate package files and register the loose Appx package.
- `--unregister`: remove the registered package.
- `--launch-packaged` / `--launch`: launch the registered packaged entry.
- `--regenerate-manifest`: rewrite `AppxManifest.xml` and default logo assets.
- `--tray` / `--no-tray`: override tray visibility for this invocation.
- `--no-bootstrap`: run directly without registering/relaunching the packaged entry.
- `--allow-multiple`: skip the single-instance guard.
- `--exit` / `--quit`: ask the running instance to exit.

`--open-url <url>` is used by queued tile activation. Only HTTP(S) links are accepted. Packaged tile launches read chaseable-notification arguments through `AppInstance.GetActivatedEventArgs()`; they are not assumed to appear in the normal process command line.

## Notes

This first version targets the Windows 10 Desktop Bridge Live Tile path only. It does not include DesktopStub's Windows 8/8.1 broker/background-task experiments.

Feed parsing supports common RSS 2.0 `<item>` entries and Atom `<entry>` entries. Tile payloads are text-only and rotate through up to five recent entries.

Namespaced RSS/Atom elements are matched by local XML name. Atom `rel="alternate"` links are preferred over feed/self links. UTF-8/UTF-16 BOMs, common XML-declared encodings, and common HTTP `charset` declarations are recognized.

Feed downloads are capped by `MaxFeedBytes`, use bounded WinHTTP timeouts, and are cancelled when the resident process exits. Configuration and log files use the repository shared baseline; alternate INI paths retain their path-scoped resident identity when the app relaunches through the package.
