# Product surface inventory

This inventory records actual host and configuration surfaces. Every project
remains in scope for the requested INI/CLI/tray/Startup contract except Windhawk
mods, which the user explicitly excluded on 2026-10-08. Services and other hosts
may need an interactive companion. See [the requirements review](REVIEW_2026-09-30.md).

## Resident desktop apps

| Product | Shared implementation | INI / CLI / tray | Startup | Status |
| --- | --- | --- | --- | --- |
| DesktopStub | `dependencies/DesktopStub` and the native baseline | Yes | Startup-folder shortcut, plus separately named packaged StartupTask | Reference source host |
| DiscordRPC | `dependencies/DiscordRPC` and the native baseline | Yes | Startup-folder shortcut | Resident app |
| ADBController | `dependencies/ADBController` and the native baseline | Yes | Startup-folder shortcut | Resident app |
| CharmTray | `dependencies/CharmTray` and the native baseline | Yes | Startup-folder shortcut | Resident app |
| NowPlayingTile | `dependencies/NowPlayingTile` and the native baseline | Yes | Packaged StartupTask | Resident app with optional widget |
| asusblink | `dependencies/asusblink` and managed baseline helpers | Yes | Startup-folder shortcut | Hardware source remains until DesktopStub parity |
| capsblink | `dependencies/capsblink` and managed baseline helpers | Yes | Startup-folder shortcut | Hardware source remains until DesktopStub parity |
| DNSAutoUpdate | `dependencies/DNSAutoUpdate` and managed baseline helpers | Yes | Startup-folder shortcut | Resident app |

All of these use the shared tooltip policy. Native apps call `RegisterTrayIcon` and `ModifyTrayIconTooltip`; managed apps call `ManagedTrayBaseline`. Version-4 native icons retain normal hover text through `NIF_SHOWTIP`.

## Host-specific and one-shot products

| Product | Actual host | Current surface | Work still required for the common interactive contract |
| --- | --- | --- | --- |
| RealTimeNotesDeskband | Explorer COM deskband | Explorer-loaded toolbar and its configuration commands | Its shared Notes provider is available to DesktopStub. Full deskband UI/parity must be represented as a source or a companion before retirement. |
| SecureDesktopLauncher | Windows service plus password launcher | Service/launcher configuration and CLI | A service cannot own a user notification area. An interactive companion would be needed for tray and Startup-folder controls. |
| AllowContentAboveLock | Windows service | Service install/control arguments | Requires an interactive companion to expose user tray/startup settings without moving service work into a user process. |
| YourPhoneHideBanner | Windows service | Service install/control arguments | Requires an interactive companion for the common user surface. |
| PhotoCollage | Image utility with optional configuration tray | CLI, shared logging/INI profile/editor and per-user Startup preference | Startup wiring has injected tests; real-folder and visible tray acceptance remain open. Sign-in starts configuration only, never an image job. |
| TaskSchedulerMigration | Task Scheduler utility with optional configuration tray | CLI, shared INI profile/editor and per-user Startup preference | Startup wiring has injected tests; real-folder and visible tray acceptance remain open. Sign-in never invokes a migration. |
| ChromeProfileCounter | One-shot PowerShell script | Dependency-backed legacy wrapper and interactive menu | Needs an explicit profile and a companion before it can share the common desktop surface; it must not edit Chrome data at sign-in. |
| YouTubeMusicMigrate | One-shot PowerShell utility | Dependency engines with compatibility entries; script parameters and product-root auth/config/cache files | Source extraction preserves the explicit data root and original argument set; inert wrapper tests pass in PowerShell 5.1 and 7. INI/tray/Startup, language parity and live-account behavior validation remain open. |
| WindhawkMods, including LockScreenWin10 | Windhawk host modules | Windhawk settings and lifecycle | Explicit user-approved exception: retain under `legacy/WindhawkMods` as-is. No dependency extraction or separate INI/tray/Startup companion is required. |

Except for the Windhawk exception, unfinished rows above are open migration work. They do not authorize registry Run keys, scheduled tasks, per-machine startup, or hidden destructive login actions. Any new resident companion must use the existing per-user `shell:startup` helper, and packaged companions may expose the separately named StartupTask control.

Registry-notification and SecureDesktopLauncher installers now select manual
SCM startup. Existing installations are not changed by a source update.
Interactive controls and per-user Startup-folder companions remain open work;
manual service installation is not a replacement for the common user surface.
No general service exception has been approved.

## Consolidation order

1. Keep product algorithms and data access under `dependencies`, with thin product overlays for resources, identity, and host wiring.
2. Add or improve DesktopStub source adapters when a product’s useful outcome is tile content: NowPlaying metadata/artwork, deskband Notes data, Discord status, and hardware status/control.
3. Retire a standalone product only after its user-visible behavior, configuration, errors, and necessary actions have parity in the source host.
4. Introduce an interactive companion only where a service, one-shot tool, or host module must expose tray/startup controls. Its own lifecycle must stay separate from the noninteractive operation.

See [the persistent rework audit](REWORK_AUDIT.md) for validated work, open feature parity, and the Defender-related restriction on executable validation.
