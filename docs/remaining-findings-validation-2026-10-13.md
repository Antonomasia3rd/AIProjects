# Remaining-findings continuation, 2026-10-13

This checkpoint follows `37d9bde` on the review branch. The user continued
testing their normal DesktopStub instance while separately named builds and
bounded inert fixtures were prepared. The original repository task remains
incomplete; the open items below are not erased by these fixes.

## Corrections

- Manual tile updates, resident CLI generation, template/branding changes,
  manifest refresh and mode re-registration now resolve the published content
  background rather than requiring a desktop wallpaper. Initial startup primes
  a content frame before mode reconciliation, without waiting for asynchronous
  providers or queuing that initial frame prematurely.
- An invalid content edit clears the published frame. Generation also refreshes
  the configuration under its existing path lock and validates it before
  accepting an older frame, including external edits inside the normal INI cache
  refresh interval. Missing frames cannot silently become wallpaper generation.
- `--no-monitor` suppresses ongoing wallpaper/fit/DPI observation while retaining
  the source/profile scheduler. Enabling or editing contents later therefore
  remains effective. An explicit return from contents to wallpaper can still
  restore wallpaper once.
- Four display enums and 58 existing host boolean keys share typed CLI/profile
  validation and direct-INI boolean interpretation. Boolean words no longer
  change behavior depending on the surface used. Integer settings remain
  integers. Clearing primary text preserves enabled secondary/badge text.
- Text size gates are under **Tile display > Text on tile sizes** for both
  contents and default text. The default-text notice also recognizes the legacy
  RSS composition path. The PowerShell picker now supplies its complete
  double-NUL-terminated file filter rather than a truncated `wstring`.
- The Windows 10 Adaptive bitmap fallback retains its background and every
  selected text field. Auto fallback and Overlay use the same selected layout;
  Overlay suppresses duplicate native text. Explicit Windows81Preset and older
  targets retain their existing renderer. An adaptive policy marker is included
  in cache identity. See [adaptive behavior](adaptive-static-text-gap-2026-10-13.md).
- Failed live-wallpaper captures observe the configured retry interval, and
  overlapping captures are excluded while the state mutex is released. Startup
  bursts retain their separate cadence and capture-once can recover from initial
  failure. See [capture scheduling](live-wallpaper-capture-validation-2026-10-13.md).
- Composition already prevented starting sibling-wallpaper pre-cache jobs. The
  additional guard stops an older job between candidates after contents become
  enabled; no initial composed-precache defect is claimed.
- DNSAutoUpdate's configuration-only path preserves Startup without consulting
  its platform. `--ini`, `--show-config` and `--print-config` now share the selected
  profile with tray, resident identities and custom Startup arguments. See
  [DNS validation](dns-passive-configuration-2026-10-13.md).

The reusable implementations remain under `dependencies`; no plugin DLL split,
mandatory credential protection or new service installation was introduced.
Windhawk and the user's untracked Discord bot remain outside this checkpoint.

## Coordinated validation

The fresh normal DesktopStub host/broker and DNS product builds compile and
custom-scan clean. Before execution, each fixture and the copied-binary smoke
runner is scanned. Private INIs, synthetic data, fake providers and offscreen
controls are used; default automated tests do not exercise real Startup,
registration, accounts, hardware, media sessions or live wallpaper capture.

| Fixture | Passing checks |
| --- | ---: |
| Managed utilities, including DNS passive/profile paths | 253 |
| Composed host, configuration and menu behavior | 8,912 |
| Portable adaptive layout | 539 |
| Adaptive renderer/host, including exact Auto/Overlay parity | 5,522 |
| Transparent backgrounds/templates | 331 |
| Portable capture scheduling and concurrent exclusion | 1,014 |

Copied normal-binary content and full smoke both pass, including the new
adaptive assertions and the 12 concurrent INI writers. DesktopStub and shared
source gates pass 1,764 and 277 checks; project-map/selector validation passes
for 11 products plus All. The existing
preset unsupported-secondary assertion still requires zero changed pixels; it
is explicitly pinned to Windows81Preset and never relaxed. The adaptive smoke
independently checks every field, retained borders and exact Auto/Overlay parity.
Portable targets are scheduled through CMake/CI; no Linux execution is claimed
on this laptop.

The first new copied-binary adaptive control was accidentally manifest-only
because it set a target without an explicit wallpaper; its visibility assertion
correctly failed. The corrected rendering control then changed the custom
manifest identity by unnecessarily setting that target again. Final fixture
arguments use the existing Windows10 smoke profile and an explicit synthetic
image, preserving the custom identity; both complete phases pass. These setup
failures are preserved in ignored logs, not counted as renderer passes.

The renamed generation test seam also required the source gate to recognize
`DESKTOPSTUB_CONTENT_QUEUE_GENERATE`. The source check uses ordered text needles,
not general regex groups; its final exact needle is unchanged in scope. The
runtime/menu fixture independently exercises those requests through the inert
queue.

The normal host and broker are installed and custom-scan clean. Host SHA-256:
`3F06C3F88E96FC586DF2756DA921F761F56C1F63D7AD9487D011BA4D00621C4C`;
broker SHA-256:
`B40845BA45A4DD207C7699693F8F803D90B693DB1A07E5F3E2D4F0A459A2B638`.
The normal instance was closed through its own graceful CLI and relaunched
within a guarded installation script. Its INI bytes were unchanged during the
copy, and its prior **Live Tile** preference remains selected. The previous
normal binaries/profile are retained in
`DesktopStub/build/ReviewBackupOct13B-20261013-165728`. Relaunched PID 392 was
observed at 16:57:41 local time; this is a checkpoint observation, not a permanent
process identity or sustained stability proof.

## Remaining findings and acceptance

1. The historical intermittent four-pixel opaque preset glyph variation remains
   unexplained. A passing new run alone does not establish its cause or repair.
2. Media-service CPU causality and sustained real-provider behavior still need
   observation. A ten-second passive sample during the user's test recorded
   DesktopStub at 2.13% of one core and the NPSMSvc process at zero measured CPU;
   this did not capture a high-CPU episode and is not a service-wide guarantee.
3. `PrintWindow` is synchronous. Retry/overlap scheduling does not make a hung
   foreign window's individual capture cancellable. A future bounded capture
   process requires separate implementation and acceptance.
4. Native target-Windows appearance, branding, RTL/DPI, visible tray/dialog
   usability, actual Startup-folder/StartupTask and account/hardware behavior
   remain acceptance stages. Adaptive fields/styles now agree, while exact shell
   margins and rasterization remain documented approximations.
5. New opt-in credential protection remains deferred under the user's
   security-last rule. Existing plain credentials, backups and full dumps still
   require private storage. The deferred Microsoft case was not reopened.

The supplied dumps and earlier renderer artifacts are preserved. No dump was
uploaded, service changed or Defender protection bypassed.
