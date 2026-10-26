# Profiles in Properties, October 26

Profiles now uses the existing DesktopStub Properties presenter. **Profiles...**
in the tray and **--profile-manager** select the **Profiles** page in the same
singleton window used by --settings; there is no separate Profile Manager
window, listbox/button layout, dialog message loop or independent palette.
The page inherits the Properties theme, contrast, pointer/keyboard feedback,
caption choice, DPI behavior and explicit sidebar expansion.

## Page and action parity

The Profiles page offers Create plus coordinator startup, unified tray and
crash-notice preferences. Each profile has a stable-ID subpage containing its
INI path, running status, Start, Stop, Open settings, per-profile automatic
startup, Make default and Identity/migration. Default and startup suffixes are
shown in the profile heading. The same profile remains selected when its
default/startup caption changes; selection is keyed by its ID, not its name.
Stopped profiles have no enabled Stop action and the current default does not
offer a redundant Make default action.

The shared backend remains responsible for creation, ownership, private
runtimes, process start/stop, confirmation continuations and identity migration.
The page does not directly terminate a process, deploy a package, pin a tile,
copy another profile's accounts, change Windows Startup or use RunAs. Starting
an already running profile retains the existing confirmation to open its
settings. Identity/migration still uses its existing concrete pair and
confirmation safeguards. Common input/information dialogs are contextual
actions, not a second settings interface.

Separate tray icons remain the default. Unified tray is opt-in and applies
after profiles restart, retaining the existing Toast/Balloon fallback behavior.
Coordinator automatic startup and crash notices also remain opt-in. Profiles
start with independent factory settings and no borrowed credentials.

## Existing configuration surfaces

This presentation adds no competing settings schema. Preferences continue to
use the owned user coordinator profiles.ini and existing CLI operations:

| Properties control | Existing command |
| --- | --- |
| Create | --profile-create NAME |
| Start / Stop | --profile-start ID / --profile-stop ID |
| Open settings | --profile-settings ID |
| Per-profile automatic startup | --profile-autostart ID on/off |
| Make default | --profile-default ID |
| Start selected profiles with the main resident | --profiles-startup on/off |
| Unified tray | --profiles-unified-tray on/off |
| Crash notices | --profiles-crash-notifications on/off |

The CLI's advanced explicit destination-path option remains available when
creating a profile. Moving an existing profile is not achieved by editing the
displayed path. Identity request fields and migration are described in
[the profile architecture](desktopstub-profile-architecture-2026-10-25.md).

## Revision and ownership safeguards

The view reads a validated coordinator snapshot. Action rows share one immutable
serialized revision, avoiding a copy of the whole list for every control.
Before dispatch, the backend rereads the owned coordinator and verifies that
revision and the captured profile ID/path key. Preference mutations recheck
the revision under the coordinator lock. Stale rows, another owner's list,
failed writes and concurrent changes fail without overwriting existing
preferences. Current-profile INI revision checks from Properties remain active.

The UI creates no polling worker or timer. Running status is an on-demand
controller inventory; F5 refreshes external changes. Wording uses named
Strings fallbacks: SettingsWindowProfiles, ProfilesCreate, ProfilesStartup,
ProfilesUnifiedTray, ProfilesCrashNotices, ProfilesExplanation, heading/status
suffixes and per-profile action labels.

## Validation scope

BuildProfilePropertiesUiTests.cmd builds the portable model fixture with
/W4 /WX /O2. It passed **39 checks** after a clean Defender scan, covering all
coordinator/profile actions, independent preferences, separate-tray defaults,
stable navigation IDs, shared revisions and custom wording.

The existing TestProfileActivationHost() includes injected backend checks:
read-only inventory, selected-profile Settings routing, mismatched path refusal,
preference transactions, stale replay/concurrent edits, ownership-before-read,
write failure, Stop controller signaling and routing the tray action to
Properties. No live profile or process is used. Parent combined-host and actual
visual results are recorded by the parent after those gates; this document does
not treat model fixtures as native visual acceptance.

No active user profile, account, package, Startup preference or resident was
changed during the implementation fixtures.
