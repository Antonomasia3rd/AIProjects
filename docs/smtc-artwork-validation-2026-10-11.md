# SMTC artwork integration, 2026-10-11

DesktopStub now accepts `Background=SMTCArtwork` through its shared background
catalog, typed INI/CLI settings and existing tray background picker. The existing
wallpaper/custom-image/solid backgrounds and text modes retain their behavior.

```ini
[Content]
Enabled=1
Count=1
TextMode=Overlay

[Content.1]
Name=Now playing
Background=SMTCArtwork
TextSources=SMTC
```

An artwork-only entry can leave `TextSources` empty. An absent thumbnail, idle
session or decode failure clears the preceding artwork and uses the existing
solid background with current media/status text as a fallback. Explicit
`TextMode=Off` still suppresses text. Future enabled artwork cycle entries may
prefetch the read-only media source; they do not replace the active entry.

## Shared implementation

- `dependencies/content_sources/smtc.inc` exposes `ReadSmtcMedia` and
  `SmtcMediaSnapshot`. One current session and media-properties object supplies
  title/artist/status and thumbnail under one operation deadline. The original
  `ReadSmtcContent` wrapper remains text-only. `SmtcSessionReader` injects a full
  synthetic session/property read for offline tests.
- `dependencies/content_sources/smtc_artwork.inc` rejects artwork exceeding
  10 MiB encoded, 8192 pixels in either dimension, or 16,777,216 decoded pixels.
  It supports first-frame PNG/JPEG/BMP/GIF, bounds cached output to 1024 pixels
  per edge, and creates an atomic 24-bit BMP. Encoded content uses SHA-256 keys.
- DesktopStub's artwork cache uses a product/profile-scoped temporary directory;
  it does not require a writable executable or package directory. Immutable
  snapshot leases keep source images readable until the final published/render
  consumer releases them. Obsolete files and empty directories are then removed
  on a best-effort basis. A process crash or failed Windows deletion can leave a
  temporary cache directory; this change does not add a global temporary purge.
- `dependencies/DesktopStub/ga_content_runtime.inc` reads the media cache once
  per composed frame, pairing its text and artwork and carrying a background
  lease into the existing immutable render snapshot. Artwork backgrounds can
  request SMTC independently of text-source selection.
- NowPlayingTile's reader now uses the same session/property/thumbnail snapshot
  and bounded decoder. Its persistent executable-directory notification-image
  JPEG format, location and cleanup policy remain for compatibility with its
  existing tile URI/lifetime behavior. Migrating that retained donor's persistent storage and
  notification lifetimes is separate from DesktopStub's new writable cache.

WinRT thumbnail reads use the same deadline as metadata. Synchronous Windows
image codec calls have bounded input/output/dimensions and deadline checks before
and after decode; they cannot be forcibly interrupted inside WIC. This is a
resource bound and cooperative deadline, not a hard real-time guarantee for a
misbehaving codec or filesystem.

## Validation

Fresh October 11 binaries were compiled, custom-scanned with Microsoft Defender,
then run through `build/review-validation/driver.ps1`. Every fixture had a
60-second process deadline. No real SMTC session, playback, package, Startup,
account, driver, disk-activity or Windhawk operation was invoked.

| Gate | Result |
|---|---|
| SMTC media/artwork fixtures | 38 checks, 0 failures; system reads disabled |
| Portable content engine | 282,185 assertions, 100,000 randomized schedule iterations |
| DesktopStub inert runtime/menu | 134 checks, 0 failures |
| Hardware-omitted host | 3 checks, 0 failures |
| Retained donor artwork storage adapter | 7 checks, 0 failures; synthetic image files only |
| NowPlaying source contracts | 78 checks |
| DesktopStub source contracts | 1,747 checks |
| Normal DesktopStub and NowPlaying review products | Fresh native links and clean custom scans; neither product entry point executed |

The media fixtures cover text-only compatibility, one paired session snapshot,
track changes, absent/invalid/oversized thumbnails, idle/cancellation, deadlines,
profile isolation, downscaling, pixel limits, reuse, and lease cleanup. The host
fixtures additionally verify artwork-only fallback, future-cycle prefetch,
immutable old generations and successful decoding of the generated BMP by the
production GDI+ image decoder. The standalone SMTC/portable fixture builds and
source checker builds use warnings as errors. Host/product builds retain existing
SDK GDI+ and unused-service-function warnings; there were no compile failures.

`tools/SmtcSourceTests.cpp` now defaults to injected offline fixtures. Its system
session smoke runs only with explicit `--allow-system-media`; that option was
not used. `tools/TestSmtcSource.cmd` forwards this explicit opt-in.

Build/run evidence is in ignored `build/review-validation/`:
`build-smtc-artwork-oct11.cmd`, `run-smtc-artwork-oct11.ps1`,
`build-smtc-source-contracts-oct11.cmd`, `smtc-artwork-build-oct11.log`,
`smtc-artwork-oct11.log`, `content-engine-artwork-oct11.log`,
`content-runtime-artwork-oct11.log`, `content-no-hardware-artwork-oct11.log`,
`now-playing-artwork-source-oct11.log`, `desktop-artwork-source-oct11.log`,
`now-playing-artwork-adapter-oct11.log`, and
corresponding Defender logs and scan SHA-256 records. Previously submitted
comparison binaries were preserved.

## Still open

NowPlaying donor configuration import/retirement, its persistent image-location
migration, and real shell/target-OS artwork acceptance are not completed by this
slice. The host's native Startup-folder and packaged StartupTask implementations
already exist; no Startup mechanism or package state was changed. Notes data,
actions and migration gaps and Discord/Caps/ASUS retirement remain separate work.
The existing four-pixel renderer intermittency was neither diagnosed nor relaxed.
