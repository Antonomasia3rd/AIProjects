# Historical 1809 Desktop and lock-screen provider feasibility

This is read-only investigation, not a native reproduction or a new feature.
The user confirms the main Desktop image problem also occurs with v24 and
earlier. The repaired 19041/uap10 independent-Registration restriction therefore
cannot be its complete historical explanation. Wallpaper, static source icons,
live corner logos and lock-screen eligibility are separate observations.

## Historical source and binary evidence

The reviewed release anchors are `DesktopStub-v1`
(`241b71d2e4a016e07fb20a0a56ee2e2e19588807`) and `DesktopStub-v24`
(`d80f5498b7b991591c7054203510d5052e85deba`), using `git show` without changing
the checkout. v24 source is under `DesktopStub/src/`; its factory defaults use
`ExperimentalLiveTileUpdate=Auto`, wallpaper and enabled Medium/Wide/Large
assets. v1 contains the same identity-dependent Auto selection.

The read-only scan of archived canonical `DesktopStub.exe` resources identified
no executable reporting a v1–v24 file version. Existing development archives
must not be called a verified original v24 binary merely because they are old.
No archived executable was run. The actual affected VM binary/hash, configuration,
manifest, registration and log remain required evidence.

Confirmed v24 branches:

| Source | Observed contract |
| --- | --- |
| `ga_runtime_helpers.inc::EffectiveLiveTileUpdateMode` | For the Windows10 manifest target, Auto chooses LiveTile if the calling process has any package identity and Registration otherwise. Fixed Registration/LiveTile modes request the corresponding runtime; Auto does not. |
| `ga_generation.inc::Generate` | Registration renders the configured static package assets and registers the manifest. LiveTile renders separate versioned images and sends notification XML. LiveTile static sizes normally use the Desktop fallback icon. Thus seeing that icon does not identify the failed step. |
| `ga_manifest.inc` | The Windows10 main app is one `Windows.FullTrustApplication`, default PRAID `App`, with minimum/tested version 15063. Windows8/8.1 target experiments have separate broker/activation contracts. |
| `ga_live_tile.inc::Appx_Update_LiveTile` | Obtains/logs current package full name, builds XML and calls the no-argument primary application updater. The old direct sender does not independently compare that package/PRAID/root with the manifest it used for image generation. |
| `ga_live_tile.inc::AppendLiveTileImageBinding` | Adaptive Medium/Wide/Large packaged image URI, background placement and explicit zero overlay. This basic XML contract exists before the later independent-tile framework. |
| `ga_app.inc::MaybeClearLiveTileOnShutdown` | Normal shutdown can clear a live primary tile, with mode-switch exceptions. A successful update followed by a clear/relaunch must be distinguished from an image never loading. |

The no-argument [primary updater](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.tileupdatemanager.createtileupdaterforapplication)
targets the **calling application**. The explicit-ID overload targets another
PRAID within the same package; it is not a cross-package repair. In particular,
"has package identity" alone does not prove the caller is the Desktop pin's
own application. This old source condition makes package/AUMID/origin evidence
important, but does not prove that a foreign identity caused the user's report.

Microsoft documents [adaptive image-only tiles](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/create-adaptive-tiles)
and [local primary/secondary notifications](https://learn.microsoft.com/en-us/windows/uwp/controls-and-patterns/tiles-and-notifications-sending-a-local-tile-notification).
Desktop Bridge secondary pinning begins at 16299 and secondary updates at
17134.81, both before 17763. The [desktop secondary documentation](https://learn.microsoft.com/en-us/windows/apps/design/shell/tiles-and-notifications/secondary-tiles-desktop-pinning)
does not establish identical behavior for every UWP/Win32 primary sender.

One version hypothesis can be excluded: v24's `ForceUpdateFromAnyVersion` flag
is documented as introduced in **1809/17763**, not 1903. It cannot be declared
unsupported merely because this VM is 1809. Older-than-1809 support is a separate
compatibility question. [DeploymentOptions version history](https://learn.microsoft.com/en-us/uwp/api/windows.management.deployment.deploymentoptions)

## Next controlled evidence

Before a product workaround, collect the affected VM's exact executable hash,
selected/effective mode and manifest target, current caller package full name
and AUMID/PRAID, registered package install root, and the actual pinned AUMID.
A public `Export-StartLayout` export can distinguish the intended AppX entry
from a stale/different application or ordinary shortcut; keep unrelated app
inventory private. Compare the caller and pin instead of relying on "Desktop"
as a display name.

Use the existing read-only image collector for manifest paths, minimum versions,
file dimensions/hashes and ACLs. Add the generation/publication result and
whether a later shutdown cleared it. A process's successful read/write or a
successful notification submission does not prove Start can load the image.

The following experiments belong in a coordinated disposable 17763 VM, not an
automatic test on the user's laptop:

1. Run a known original v24 and the candidate with separate clean profiles;
   compare explicit Registration, explicit LiveTile and Auto.
2. Keep the exact same generated image and compare UWP primary, Desktop Bridge
   primary and Desktop Bridge secondary notification senders with verified IDs.
3. Compare a simple legacy image binding with an adaptive background image;
   compare text-only and image-plus-text. This separates parsing/rendering from
   target and image-access failures.
4. Compare static generated assets before pinning, changed static assets after
   re-registration, and unique live image names. Inspect actual package-root
   image access/ACLs and caching; do not repeatedly clear Start's databases.

These are an isolation matrix, not a claim that any listed hypothesis is true.
The existing strict four-pixel renderer discrepancy is not modified or reopened
as a performance issue by this research.

## Classic lock-screen quick/detailed status

There is a documented feature path on Windows 8/8.1 and Windows 10. The
[Windows10 manifest declaration](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-uap-lockscreen)
uses `uap:LockScreen Notification="badge"` or `"badgeAndTileText"`, a dedicated
monochrome badge logo, and a Wide tile for detailed text. The documentation also
requires background-task registration. The [UWP lock-screen sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-universal-samples/lockscreenapps/)
demonstrates badge updates and text-only Wide status; a wallpaper image does not
become detailed status automatically.

Secondary tiles also have documented
[`LockScreenBadgeLogo`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.startscreen.secondarytile.lockscreenbadgelogo)
and [`LockScreenDisplayBadgeAndTileText`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.startscreen.secondarytile.lockscreendisplaybadgeandtiletext)
eligibility properties, including the 17763 contract. Eligibility does not
select a slot: the user must choose the provider in Settings. Microsoft's
[secondary tile overview](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/secondary-tiles)
explicitly excludes programmatically selecting a secondary lock-screen tile.

For a DesktopStub implementation, first prove picker eligibility and status
updates for the **actual full-trust application shape** on the target OS. A
schema/API being present does not establish that adding those fields to this
Win32 manifest is sufficient. Desktop Bridge's
[resource validation](https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/windows-desktop-bridge-app-tests)
does describe lock-screen badge resources, so there is also no basis here for a
blanket claim that Desktop Bridge can never participate. An optional UWP
companion/provider is a possible prototype if direct eligibility fails; it is
not yet a necessary or accepted design.

Candidate feature policy should keep lock-screen use opt-in, independently
configure badge/detailed content and sensitive-text redaction, clear/update
the correct destination, and expose the same settings through CLI/INI/GUI.
Quick status is a Windows badge/number/glyph surface, not arbitrary tile text.
Keep a dedicated status layout rather than silently replacing Desktop's Start
wallpaper notification. Reuse source snapshots; eligibility alone is not proof
of selection, background execution or guaranteed update timing.

## Modern Windows11 lock-screen widgets

Do not promise the historical seven quick-status slots on every Windows11 build.
Microsoft's current [lock-screen support guidance](https://support.microsoft.com/en-us/windows/experience/personalization/customize-the-lock-screen-in-windows)
distinguishes classic Windows10 status selection from Windows11's widget catalog,
where supported small-size widgets can be selected. The
[Insider rollout announcement](https://blogs.windows.com/windows-insider/2025/11/04/announcing-windows-11-insider-preview-build-27982-canary-channel/)
also makes build/rollout dependence explicit.

This modern path is tied to a widget provider/host, not merely another Live Tile
manifest field. DesktopStub would need a separate widget implementation and
native host-eligibility checks on the actual Windows/Web Experience versions.
Neither legacy lock-screen status nor modern widget support is implemented by
this investigation. No package, VM, account, lock-screen preference or system
state was changed; no lock/CPU/RAM experiment was performed.
