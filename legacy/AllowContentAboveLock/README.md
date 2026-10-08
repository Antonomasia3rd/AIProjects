# AllowContentAboveLock

Keeps notification entries configured to allow content above the lock screen for the signed-in user.

The default is a current-user tray app. It runs from a normal user folder,
needs no service installation or administrator rights, and does not enumerate
other users' notification hives. The product overlay supplies its policy;
`dependencies/registry_notification_app.cs` owns configuration and the tray,
and the shared registry engine owns watching and typed updates.

Build with `BuildAllowContentAboveLock.cmd`, then run `build\AllowContentAboveLock.exe`.
`--tray` explicitly chooses the same mode. Use **Settings** for every preference:

```ini
[Settings]
Enabled=1
LoggingEnabled=1
RunAtStartup=0
```

`Enabled=0` stops enforcement; it does not restore registry values already
changed. Windows notification settings remain editable while enforcement is
disabled. Logging writes diagnostics to the app console, not the service event
log. The hover text reports activity and errors; **Reload configuration** retries
failed watching or applies manually edited settings. Closing the tray stops
its watcher threads.

```powershell
.\build\AllowContentAboveLock.exe --ini .\personal.ini --configure-only --set Enabled=0
.\build\AllowContentAboveLock.exe --ini .\personal.ini --show-config
.\build\AllowContentAboveLock.exe --ini .\personal.ini --startup
.\build\AllowContentAboveLock.exe --ini .\personal.ini --no-startup
```

Startup uses only this user's Startup folder and starts `--tray --ini` for the
selected profile. `--configure-only`, help/version and configuration inspection
do not start watchers, access notification keys or change Startup. `--enable`
and `--disable` are persistent aliases; combine them with `--configure-only`
to edit without running a tray. An invalid profile remains editable in the
tray; complete replacement batches are validated before saving.

## Optional legacy all-user service

The remainder describes only the explicitly installed, manually started
compatibility service. Its protected-path requirements do not apply to normal
current-user tray mode. Existing installed services are not removed or changed
by the new executable; stop/uninstall the old service deliberately before using
only the current-user host, to avoid two enforcers changing the same keys.
Windows service that watches notification settings under each loaded user hive
and forces `AllowContentAboveLock=1` for notification app entries it can access.

This service runs as `LocalSystem`. The current shared service host rejects
checkout/build copies unless the executable and sibling INI are protected files
under Program Files. This is an existing implementation restriction, not the
repository's current design rule: setup convenience comes first, and additional
security is intended to become opt-in after functional repairs. That migration
is unfinished; there is currently no switch to disable this restriction.
Writable service code or configuration can let another program act as
`LocalSystem`, so keep the current installation protected. See
[the shared audit](../../docs/audit-shared.md).

## Requirements

- Windows with .NET Framework 4.x.
- Administrator rights to provision, install, start, stop, or remove the
  service.
- A dedicated directory below Program Files that keeps the inherited protected
  ACL. Do not grant ordinary users write access to that directory or its files.

## Build

From this folder:

```cmd
mkdir build 2>nul
BuildAllowContentAboveLock.cmd
```

`--help` and `--version` are safe to run directly from the build directory.
`--install` is not: it deliberately rejects an executable outside Program
Files before opening the Service Control Manager.

`AllowContentAboveLock.cs` is a declarative product overlay: it supplies only
the service metadata and the desired `AllowContentAboveLock=1` DWORD policy.
The shared dependency owns service lifecycle, registry watching/enumeration,
typed repair, diagnostics, and protected installation.

## Protected install

Run these commands in an elevated Command Prompt. Provision the files first,
then let the executable register its exact protected path:

```cmd
set "INSTALL_DIR=%ProgramFiles%\AIProjects\AllowContentAboveLock"
mkdir "%INSTALL_DIR%" 2>nul
copy /y "build\AllowContentAboveLock.exe" "%INSTALL_DIR%\AllowContentAboveLock.exe"
(
  echo [Settings]
  echo LoggingEnabled=1
) >"%INSTALL_DIR%\AllowContentAboveLock.ini"
"%INSTALL_DIR%\AllowContentAboveLock.exe" --install
sc.exe start AllowContentAboveLockService
```

Installation fails closed unless both files and every path component are below
a real Program Files known-folder root, are not reparse points, have canonical
handle paths, have a privileged owner, and do not grant dangerous write access
to an unprivileged principal. The executable and INI are pinned while they are
validated, and validation completes before `OpenSCManager`/`CreateService` can
change service registration. The installed account is explicitly
`LocalSystem`.

The service also repeats those checks at every start, requires its current token
to be `LocalSystem`, and retains the component handles for its lifetime. A
missing, moved, oversized, malformed, writable, or redirected INI prevents
startup.

## Commands

```text
--help       Show command help without changing the system.
--version    Show the executable version without changing the system.
--install    Register this protected Program Files copy as a manually started service.
--uninstall  Remove only a same-name LocalSystem service pointing to this exact executable.
```

Unknown commands and extra arguments are rejected. With no command, an
interactive launch is rejected; only the Service Control Manager may enter
service mode.

## Configuration

The only current service-host setting is:

```ini
[Settings]
LoggingEnabled=1
```

`LoggingEnabled=0` suppresses informational events. Warnings and startup
failures are still reported. The file is read through its already validated
handle and is limited to 64 KiB. Stop the service before replacing it, preserve
the protected Program Files ACL, then start it again to load changes.

## Uninstall

Run from an elevated Command Prompt before deleting the files:

```cmd
sc.exe stop AllowContentAboveLockService
"%ProgramFiles%\AIProjects\AllowContentAboveLock\AllowContentAboveLock.exe" --uninstall
```

The uninstaller validates its own protected path and checks that the registered
service still uses this exact executable and the `LocalSystem` account before
calling `DeleteService`.

## Runtime behavior

- Watches `HKEY_USERS` for newly loaded user hives.
- Uses the shared restartable registry-notification service engine under
  `dependencies`.
- Attaches to loaded domain/local `S-1-5-21-*` and Azure AD `S-1-12-1-*` user
  hives.
- Watches
  `HKU\<SID>\Software\Microsoft\Windows\CurrentVersion\Notifications\Settings`
  for child-key and value changes.
- Sets `AllowContentAboveLock` to an exact DWORD `1` on notification setting
  subkeys, repairing malformed or wrong-kind values.
- Sends diagnostics to the Windows Application event log and debugger output.
  It never creates a privileged sibling log or registers an event source in the
  registry.

## Generated files

- `build\AllowContentAboveLock.exe`

The service no longer creates build-directory INI/log sidecars. Generated build
output is ignored by git.

## Release

Prebuilt binary: [AllowContentAboveLock v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/AllowContentAboveLock-v1).
