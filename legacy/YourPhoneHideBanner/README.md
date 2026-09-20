# YourPhoneHideBanner

Windows service that watches Phone Link notification registry entries for
loaded users and suppresses matching notification banners and sounds.

It targets notification setting keys whose names start with:

```text
Microsoft.YourPhone_8wekyb3d8bbwe!YourPhoneNotifications_
```

For matching keys it sets `ShowBanner` to DWORD `0` and `SoundFile` to an empty
REG_SZ string.

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
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /warn:4 /warnaserror+ /target:exe /optimize+ /out:build\YourPhoneHideBanner.exe /r:System.ServiceProcess.dll ..\..\dependencies\registry_notification_service.cs YourPhoneHideBanner.cs
```

The installed Windows service name is `YourPhoneHideBannerService`.
`--help` and `--version` are safe to run from the build directory. `--install`
deliberately rejects an executable outside Program Files before opening the
Service Control Manager.

`YourPhoneHideBanner.cs` is a declarative product overlay: it supplies only the
service metadata, matching subkey prefix, and desired typed registry values.
The shared dependency owns service lifecycle, registry watching/enumeration,
typed repair, independent per-value failures, diagnostics, and protected
installation.

## Protected install

Run these commands in an elevated Command Prompt. Provision the files first,
then let the executable register its exact protected path:

```cmd
set "INSTALL_DIR=%ProgramFiles%\AIProjects\YourPhoneHideBanner"
mkdir "%INSTALL_DIR%" 2>nul
copy /y "build\YourPhoneHideBanner.exe" "%INSTALL_DIR%\YourPhoneHideBanner.exe"
(
  echo [Settings]
  echo LoggingEnabled=1
) >"%INSTALL_DIR%\YourPhoneHideBanner.ini"
"%INSTALL_DIR%\YourPhoneHideBanner.exe" --install
sc.exe start YourPhoneHideBannerService
```

Installation fails closed unless both files and every path component are below
a real Program Files known-folder root, are not reparse points, have canonical
handle paths, have a privileged owner, and do not grant dangerous write access
to an unprivileged principal. The executable and INI are pinned while they are
validated, and validation completes before `OpenSCManager`/`CreateService` can
change service registration. The installed account is explicitly
`LocalSystem`.

The service repeats those checks at every start, requires its current token to
be `LocalSystem`, and retains the component handles for its lifetime. A missing,
moved, oversized, malformed, writable, or redirected INI prevents startup.

## Commands

```text
--help       Show command help without changing the system.
--version    Show the executable version without changing the system.
--install    Register this protected Program Files copy as an automatic service.
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
sc.exe stop YourPhoneHideBannerService
"%ProgramFiles%\AIProjects\YourPhoneHideBanner\YourPhoneHideBanner.exe" --uninstall
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
- Reapplies exact DWORD/REG_SZ banner and sound values when matching keys are
  created, changed, malformed, or stored under the wrong registry value kind.
  Banner and sound repairs are independently contained so one failed value
  operation does not suppress the other repair attempt.
- Sends diagnostics to the Windows Application event log and debugger output.
  It never creates a privileged sibling log or registers an event source in the
  registry.

## Generated files

- `build\YourPhoneHideBanner.exe`

The service no longer creates build-directory INI/log sidecars. Generated build
output is ignored by git.

## Release

Prebuilt binary: [YourPhoneHideBanner v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/YourPhoneHideBanner-v1).
