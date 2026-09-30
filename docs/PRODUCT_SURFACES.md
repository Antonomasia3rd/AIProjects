# Product surface inventory

This inventory records the actual host and configuration surfaces in the repository. It is a migration plan, not an exception to the project goal: users should eventually get consistent configuration, command-line, tray, and appropriate per-user startup behavior wherever an interactive app is meaningful.

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
| PhotoCollage | One-shot image utility | CLI and shared logging | A profile/configuration surface can be added directly. A tray/startup surface requires a resident host and should not automatically run image jobs at sign-in. |
| TaskSchedulerMigration | One-shot Task Scheduler utility | CLI and local validation | A profile/configuration surface can be added directly. A tray/startup surface requires a resident host; automatically rerunning a migration at sign-in would be unsafe and confusing. |
| ChromeProfileCounter | One-shot PowerShell script | Script parameters | Needs a product wrapper or companion before it can share the common desktop surface. |
| YouTubeMusicMigrate | One-shot PowerShell utility | Script parameters and local files | Needs a product wrapper or companion before it can share the common desktop surface. |
| WindhawkMods and LockScreenWin10 | Windhawk host modules | Windhawk settings and lifecycle | Windhawk owns loading and startup. A separate interactive companion or DesktopStub source would be needed for the common surface. |

The rows above are open migration work. They do not authorize registry Run keys, scheduled tasks, per-machine startup, or hidden destructive login actions. Any new resident companion must use the existing per-user `shell:startup` helper, and packaged companions may expose the separately named StartupTask control.

## Consolidation order

1. Keep product algorithms and data access under `dependencies`, with thin product overlays for resources, identity, and host wiring.
2. Add or improve DesktopStub source adapters when a product’s useful outcome is tile content: NowPlaying metadata/artwork, deskband Notes data, Discord status, and hardware status/control.
3. Retire a standalone product only after its user-visible behavior, configuration, errors, and necessary actions have parity in the source host.
4. Introduce an interactive companion only where a service, one-shot tool, or host module must expose tray/startup controls. Its own lifecycle must stay separate from the noninteractive operation.

See [the persistent rework audit](REWORK_AUDIT.md) for validated work, open feature parity, and the Defender-related restriction on executable validation.
