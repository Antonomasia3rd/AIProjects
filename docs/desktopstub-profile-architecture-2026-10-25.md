# DesktopStub profile identity and runtime isolation

This implementation separates per-tile configuration, profile residents and Windows package identities. It does not use the Windows account name as an ownership boundary. The existing main profile's installed identity remains preserved until the user approves an explicit migration.

## Existing behavior audited

- Registration tiles already used a Windows SID-derived user hash, normalized INI-path hash and tile hash in `ga_tile_registration.inc`. This did not provide independent identities for the main Live Tile host.
- The old main manifest default was `dev.local.<executable base name>` with a product Publisher. `EnsureAppxManifest` wrote to the executable directory, so two INIs launched from one directory would otherwise share their manifest, assets and activation host.
- Single-instance IPC is already scoped by effective INI path. Independent profiles can therefore run in separate residents, but package files also need separate locations.
- Packaged Startup previously supported only the default INI. Profile-list startup is a separate, opt-in coordinator operation; changing that list does not silently create an OS Startup registration.

## Persistent identity model

`dependencies/desktopstub_profiles.h` supplies the portable model and typed settings. A profile identity record stores a cryptographically random 128-bit persistent ID, the Windows SID, the normalized INI identity, package name, Publisher, identity mode and whether a legacy identity was adopted. The native host generates the ID only when no valid record exists, then persists it under the INI mutation lease before package generation.

An automatic package name has the form `aip.ds.<SID-hash-prefix>.<128-bit-id>`, totaling 48 characters. Its Publisher is `CN=DesktopStub.<128-bit-id>`. The full SID and normalized profile key remain in the ownership record; the short name prefix is not a security boundary. A friendly Publisher display name may use the current Windows username. Renaming an account does not regenerate an existing profile's identity.

`PlanIdentity` returns a concrete current/proposed pair. `NeedsPersistentId`, `OwnerMismatch` and `MigrationRequired` are distinct outcomes. Existing identities retain their spelling and Publisher on upgrade. Explicit Static overrides are validated, and native package-family comparison rejects collisions with another profile owned by the current Windows user. Static identities can intentionally share naming conventions across an ISO, but their deployment, certificate Publisher and Windows package-family constraints still apply.

Legacy adoption requires both current-SID file ownership and proof that the same package is registered for the current user at the original executable directory (or that the current packaged process carries that family there). An owned manifest bundled in an unregistered ZIP or fresh VM is a template, not an installed identity to preserve: a fresh automatic identity is generated instead. Unknown package-lookup failures are reported rather than guessed. `GetPackagesByPackageFamily` and `GetPackagePathByFullName` inspect only the current user's registrations. Initial enrollment, saved-identity reuse, staging and migration reject a family registered at an unrelated location, including an app absent from the profile registry. A verified original Main may retain its canonical registered directory; managed profiles must use their derived private runtime generation.

The current Windows package-string constraints include a 3–50-character ASCII name, reserved device-name/prefix restrictions and ordinal case-insensitive comparison. The native adapter also calculates the package family using `PackageFamilyNameFromId`. A Publisher used for a signed package must match its signing certificate subject. See Microsoft's [package identity overview](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/package-identity-overview) and [manifest Identity reference](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-f-identity).

Public identity requests are separate from the internal ownership record:

```ini
[Profile]
IdentityMode=Auto
StaticPackageName=""
StaticPublisher=""
```

`ProfileIdentity.*` is persisted coordinator state. `Profile.ManagedRuntime` is also coordinator-owned and is not a manual setting that bypasses migration. Legacy `Settings.ManifestIdentityName` edits are recognized as requests rather than silently replacing the current identity. A valid existing identity keeps running while a requested change awaits approval.

## Profile files and UI

The per-user coordination file is an actual editable INI:

`%USERPROFILE%\.DesktopStub\Profiles\<installation-hash>\profiles.ini`

This folder is deliberately outside AppData. `FOLDERID_Profile` with `KF_FLAG_NO_PACKAGE_REDIRECTION` supplies the current user's profile root. The flag changes known-folder path retrieval; it does not by itself disable the MSIX file filter that redirects newly created AppData files to a different location for each package. Using another writable part of the user profile gives the packaged Main, ordinary command-line helpers and every independent package identity the same coordination file. This follows Microsoft's [known-folder flag contract](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ne-shlobj_core-known_folder_flag) and [MSIX filesystem virtualization rules](https://learn.microsoft.com/en-us/windows/msix/desktop/flexible-virtualization).

For upgrades from the initial implementation, the coordinator checks the old unredirected LocalAppData profile directory and the private LocalCache directory of this installation's verified main package. It does not enumerate other packages' private data. An owned valid legacy registry can be copied as metadata into an absent shared registry after every referenced saved profile identity is validated. Main's ID, default selection and exact INI path are preserved. Accounts, profile INIs, runtimes and original registry files are not copied, moved or rewritten. Conflicting old lists fail with a clear error; an existing shared registry is never overwritten by legacy discovery. An empty registry does not cause an owned Main's `--exit` command to return early; it continues into normal singleton IPC.

Its `[Profiles]` section contains the owner SID, version, default profile ID, profile list and preferences. Each `[Profile.<id>]` section contains its name, absolute INI path, normalized identity and auto-start selection. All profile preferences are available through the Profiles window and command line. Defaults are separate tray icons, profile-list startup disabled and crash notices disabled.

The editable fields have this structure (the example identifiers are placeholders):

```ini
[Profiles]
Version=1
OwnerSid="SID:S-1-5-21-..."
DefaultProfile="<32 hexadecimal profile ID>"
StartupEnabled=0
UnifiedTray=0
NotifyCrashes=0
Instances="<profile ID>,<another profile ID>"

[Profile.<profile ID>]
Name="Work"
IniPath="D:\\Profiles\\Work.ini"
ProfileKey="D:\\PROFILES\\WORK.INI"
AutoStart=0
```

The registry writer quotes and escapes paths; the owner and normalized path binding are validated before use. Identity/account/source settings remain in each profile's own INI. Registry preferences coordinate profiles and are not copied into every tile or personal profile.

The Profiles window provides Create, Start, Stop, Settings, Auto-start, Make default and Identity actions. Creating a profile uses the compiled factory template; it does not copy another profile's account settings. A duplicate ordinary launch can offer creation of a separate profile through the existing confirmation coordinator. That prompt is controlled by `Settings.AlreadyRunningProfilePrompt`.

Fresh profiles receive `Strings.TrayTip="DesktopStub — <profile name>"`, so their separate tray icons are distinguishable. This remains an ordinary configurable string. Existing main/current profile captions are not rewritten.

The identity action distinguishes a preserved legacy identity from an automatic or Static identity, displays the concrete current and proposed names/Publishers, and explains that Windows requires new pins. Existing pins are not automatically removed.

Only the profile selected by `DefaultProfile` coordinates startup of the other explicitly selected profiles. Nondefault residents do not recursively start the profile list. The coordinator must itself be started, for example through the application's existing Startup option. Unified tray mode is optional and takes effect after profile restart. It hides only managed nondefault profiles explicitly configured with `Settings.NotificationApi=Toast`, without overwriting their `Settings.TrayIcon` preferences. Auto and Balloon profiles keep their own icons because the legacy balloon API requires that icon's identity. Those profiles receive one startup warning explaining how to select Toast before hiding the icon. The default/coordinator and original runtime retain their normal icon preference. No balloon relay or additional background worker is created.

Representative CLI operations are:

```text
DesktopStub.exe --profile-list
DesktopStub.exe --profile-manager
DesktopStub.exe --profile-create "Work"
DesktopStub.exe --profile-create "Work" --profile-path D:\Profiles\Work.ini
DesktopStub.exe --profile <id> --settings
DesktopStub.exe --profile-start <id>
DesktopStub.exe --profile-stop <id>
DesktopStub.exe --profile-default <id>
DesktopStub.exe --profile-autostart <id> on
DesktopStub.exe --profiles-startup on
DesktopStub.exe --profiles-unified-tray off
DesktopStub.exe --profiles-crash-notifications on
DesktopStub.exe --profile <id> --configure-only --set Settings.TrayIcon=0
DesktopStub.exe --profile <id> --exit
DesktopStub.exe --profile <id> --once
```

Profile selection with configure-only, render-only, help, version or exit launches the canonical source executable with the selected absolute INI and propagates the child result after a bounded wait. These commands do not stage a runtime or start a resident. Live one-shot generation (`--once`) and manifest-generation aliases use the profile's owned runtime because they can write package assets. A default-route exit with no registered default returns without creating a new profile. Normal profile start and Settings actions remain asynchronous. Remaining command arguments are preserved individually, including spaced `--set` values; arbitrary `--ini` overrides cannot be smuggled into a registered-profile forwarding request.

## Native ownership and runtime staging

`desktopstub_profiles_native.inc` owns filesystem/process adapters, all injectable for tests. Existing profile INIs and coordination files are checked for the current Windows file-owner SID before reading their contents. Foreign-owned default INIs are not adopted. A normal default launch can route to that user's owned default profile or create an owned fresh Main; explicit `--ini` to another user's profile fails clearly. There is no RunAs or cross-user process control. A file owned by Administrators/System rather than the current user is deliberately not sufficient proof that a personal profile belongs to this user.

Each managed package generation has its own runtime directory:

`%USERPROFILE%\.DesktopStub\Profiles\<installation-hash>\<profile-id>\runtime\<identity-generation>\`

The generation is derived from the validated package name and Publisher. The directory contains the host, Live Tile broker, tile launcher and a SID/install/profile/generation-bound `runtime.owner` marker. Package manifests and generated assets remain inside that runtime. The original main executable directory stays in use for the legacy main until an approved migration changes it. No automatic package registration or pin operation occurs during ordinary profile creation or runtime copying.

A normal resident launched directly from the canonical executable with `--ini <custom path>` first enrolls its persistent identity, releases the original singleton and routes to its owned runtime. It cannot continue using Main's manifest/assets directory. A managed runtime launched by Windows without `--ini` resolves the exact profile INI from its validated owner marker before default-profile routing. The marker stores both the normalized identity and original absolute path, preserving the profile's existing credential-owner binding. Profile selection, managed package activation and direct custom-INI residents therefore converge on the same profile and package directory.

Native writes reject reparse-point traversal, use sibling staging files and atomic commit, refuse new-file overwrites and preserve the existing file's owner/DACL. The first pre-manager backup is not overwritten. Migration backups use a distinct operation ID and preserve source security through file copying. Graceful stop targets a profile-specific window belonging to the same Windows user and session; it does not terminate processes.

## Identity migration lifecycle

`desktopstub_profile_migration.inc` implements preparation, commit, completion and recovery:

1. Revalidate the selected profile's SID/path/current identity and calculate its proposed Windows family. Reject another current-user profile using that family.
2. Stage the proposed identity in a different runtime generation. Preserve the old INI in a non-overwriting operation backup and write an owned migration receipt.
3. After the active resident's workers stop and its window/mutex are released, recheck the complete approved INI revision and package-family collision under the registry/INI leases. Then commit the proposed identity and public mode fields atomically.
4. Launch the new generation with the same INI and the explicit Desktop setup action. A launch failure restores the previous identity and restarts the previous runtime. A rejected stale commit preserves the newer INI and restarts the prior runtime only when its current ownership record still matches.
5. Setup completion marks the receipt complete. Setup failure restores identity/runtime fields while preserving subsequent account/source edits and independently changed pending identity requests. Explicit application Exit does not force an old resident to restart.
6. A pending or rollback receipt survives interruption. The next manual start can recover the old runtime. A packaged relaunch already carrying the exact proposed family is evidence that registration succeeded, so it can complete a receipt left behind by a final-write failure.

Old package identities and pins are retained. The implementation does not automatically delete a successfully registered new package if a later step fails.

## Event-based crash notices

Unified tray presentation is capability-dependent. Auto/Balloon profiles retain
their own icon; only managed nondefault profiles explicitly using Toast can hide.
If the notification adapter later falls back to Balloon, that profile restores
its icon for the remainder of the session before sending. This prevents a failed
Toast or missing notification identity from silently discarding the fallback.

With crash notices enabled, a resident that starts another profile can retain its process handle and use `RegisterWaitForSingleObject`. The child also receives an owned `parent.watch` record containing its coordinator's PID, creation time, executable, SID and installation/profile binding. It validates the live process's creation time, same user/session and controlled source/runtime path before registering a wait. A recycled PID or unrelated process is rejected. There is no process polling timer.

Ordinary exit codes, including a handled failure code 1, are not labeled as crashes. Notices require an unhandled native failure status. Manager-requested graceful stops are explicitly marked expected. Closing the coordinator releases its waits and handles; independent profile residents are not terminated.

## Scope constraints

| Setting | Scope | Reason |
| --- | --- | --- |
| Source/account, refresh, tile typography, delivery/cache policy | Tile | Independent provider/configuration snapshots. |
| Main package name, Publisher and compatibility target | Profile/package | One process has one main package identity. A changed identity needs explicit package/pin migration. |
| PowerShell deployment executable, job timeout/cancel policy | Profile/job | Host deployment policy is captured by the job; it is not an independent text-source setting. |
| Single-instance mutex/IPC, scheduler wakeup, thread priority, COM apartment | Process | All tiles in one resident share these resources. Separate profiles provide separate residents. |
| Startup profile list, unified tray, crash supervision | Current Windows user/coordinator | Coordination spans otherwise independent residents. |

## Validation boundary

The focused tests use in-memory filesystem/process adapters, fabricated SID/INI/account strings and deterministic entropy. They cover persistent identities, ownership, duplicate paths/IDs, initial and saved Static family conflicts, unregistered-template adoption rejection, original-Main reuse, factory creation and distinguishable tray captions, isolated runtime markers, argument forwarding and one-shot exit propagation, transport-aware unified icons, migration revision guards, rollback preserving later edits, receipt validation, PID-reuse rejection and guarded metadata-only adoption into the common registry. The current focused runs passed 79 portable checks and 65 native injected checks with zero failures after both binaries scanned clean in Defender. The only native package API used by the focused migration fixture is identifier calculation; it performs no registration or deployment. `ProfileRoutingHostTests.inc` separately covers the owned-Main/empty-registry exit branch without native IPC.

The startup code emits `Shared profile coordinator: <exact profiles.ini path>; profile=<id>`, but this occurs before file logging is initialized and is not a reliable persisted receipt. Native acceptance instead checks the physical shared registry created by the packaged resident, compares its Main ID with an external `DesktopStub.exe --profile-list`, and verifies that ordinary external `--exit` reaches that resident. A same-process fixture alone cannot prove actual Windows redirection behavior.

No real profiles, AppX packages, Startup entries, processes, user accounts or pins were created or modified while validating this slice. Full host integration and actual desktop visual acceptance remain the parent task's responsibility.

The subsequent combined-host acceptance on October 25 preserved the existing
Main profile ID, found the same ID from the plain CLI, and shut down the packaged
resident through ordinary `--exit` with exit code 0. The existing package was
then reactivated. The corrected Profile Manager caption was verified as
`DesktopStub profiles` with one Main entry. No additional profile, package,
Startup entry or pin was created for these checks. See the final acceptance
record in [the combined delivery report](desktopstub-profiles-mail-ux-2026-10-25.md).
