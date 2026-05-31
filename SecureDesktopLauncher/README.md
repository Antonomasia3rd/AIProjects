# SecureDesktopLauncher

Windows secure-desktop tooling made of two executables:

- `SecureDesktopLauncher.exe`: Windows service that launches configured programs as `LocalSystem` on matching sessions/desktops, usually `WinSta0\Winlogon`.
- `SecureDesktopPasswordLauncher.exe`: password-gated launcher that starts one configured target only after password verification and keeps spawned processes in a kill-on-close Job object.

## Requirements

- Windows.
- Visual Studio Build Tools with the C++ workload.
- Administrator rights to install/start the service.
- Absolute local paths for the service, config files, launch targets, and working directories.

## Build

```cmd
build_launcher.cmd
build_password_launcher.cmd
```

Syntax-only checks:

```cmd
build_launcher.cmd check
build_password_launcher.cmd check
```

Source checks for launch invariants:

```cmd
TestSecureDesktopLauncherSource.cmd
```

Side-by-side builds when installed files are locked:

```cmd
build_launcher.cmd new
build_password_launcher.cmd new
```

Outputs are written to `build\`.

## Service Install

From an elevated prompt:

```cmd
build\SecureDesktopLauncher.exe install
sc start SecureDesktopLauncher
```

If the service exists, `install` updates the binary path and display name.

Remove it with:

```cmd
sc stop SecureDesktopLauncher
build\SecureDesktopLauncher.exe uninstall
```

## Service Local Files

The service reads its configuration from the executable directory using the executable base name:

```text
<exe folder>\<exe name>.ini
<exe folder>\<exe name>.log
```

For the default build output, those files are `build\SecureDesktopLauncher.ini` and `build\SecureDesktopLauncher.log`. If the executable is renamed, the default INI/log names follow the renamed executable.

## Service Configuration

Example:

```ini
[General]
Desktop=WinSta0\Winlogon
LaunchSpacingMs=3000
MaxProgramsPerSession=0
StopOnServiceStop=1
StartOnServiceStart=1
StartOnConsoleConnect=1
StartOnRemoteConnect=1
StartOnLogon=1
StartOnLock=1
StartOnUnlock=1
LaunchDisconnectedSessions=1
IncludeUsers=
ExcludeUsers=

[Program:ExampleOverlay]
Enabled=1
Path=C:\Program Files\ExampleOverlay\ExampleOverlay.exe
Arguments=--session {SessionId} --user "{Account}"
CommandLine=
WorkingDirectory=C:\Program Files\ExampleOverlay
Desktop=WinSta0\Winlogon
PreventDuplicate=1
StopOnServiceStop=1
LaunchSpacingMs=3000
ShowWindow=4
IncludeUsers=
ExcludeUsers=
```

Add one `[Program:Name]` section per launched program.

General keys:

- `Desktop`: default desktop for program launches.
- `LaunchSpacingMs`: delay between matching program launches.
- `MaxProgramsPerSession`: `0` launches every enabled matching program; positive values cap per-session launches.
- `StopOnServiceStop`: default cleanup behavior for processes launched by the current service instance.
- `StartOnServiceStart`, `StartOnConsoleConnect`, `StartOnRemoteConnect`, `StartOnLogon`, `StartOnLock`, `StartOnUnlock`: session events that trigger launches.
- `LaunchDisconnectedSessions`: whether disconnected sessions can be considered during service-start scans.
- `IncludeUsers`, `ExcludeUsers`: global user filters.

Program keys:

- `Enabled`: set `0` to keep the section but skip it.
- `Path`: local absolute executable path. Required; UNC paths are rejected by path validation.
- `Arguments`: arguments appended after the quoted `Path`.
- `CommandLine`: optional full command line. If present, this replaces the generated `Path + Arguments` command line while `Path` remains the application path passed to `CreateProcessAsUserW`.
- `WorkingDirectory`: local absolute working directory. Defaults to the directory of `Path`.
- `Desktop`: per-program desktop override.
- `PreventDuplicate`: skips launch when the same configured image is already running in the target session.
- `StopOnServiceStop`: whether service stop should terminate matching SYSTEM-owned processes launched by the current service instance.
- `LaunchSpacingMs`: per-program launch delay override.
- `ShowWindow`: `STARTUPINFO.wShowWindow` value.
- `IncludeUsers`, `ExcludeUsers`: per-program filters.

Supported argument tokens:

```text
{ProgramName}
{SessionId}
{UserName}
{Domain}
{Account}
```

## User Filters

`IncludeUsers` and `ExcludeUsers` accept comma or semicolon separated values.

- Empty `IncludeUsers` means all users.
- Plain names match `WTSUserName`.
- `DOMAIN\User` matches the full account string.
- `*` and `?` wildcards are supported.
- Global and per-program filters are both applied.

Examples:

```ini
IncludeUsers=DOMAIN\UserName
ExcludeUsers=DOMAIN\test*
```

## Path Validation

The service requires configured program paths and working directories to be local absolute paths that already exist.

A valid path must:

- be absolute;
- already exist;

Install the service, gate, configs, and launched targets wherever the service account can read and execute them.

This project does not change ACLs on service files, config files, target programs, or their parent directories. If future changes add checks for user-writable or otherwise risky locations, those checks must warn only; they must not modify ACLs, ownership, inheritance, integrity labels, or other access-control state.

## Threat Model Notes

This tool intentionally creates `LocalSystem` processes on interactive desktops. Treat the service executable, password launcher, INI files, launched programs, and their parent directories as privileged code.

`CommandLine` only changes the command-line string passed to `CreateProcessAsUserW`; `Path` is still passed as `lpApplicationName`. Keep `CommandLine` empty unless a target truly needs custom `argv[0]` or unusual quoting.

## Password Gate Config

The password launcher reads and writes local files beside itself using the executable base name:

```text
<exe folder>\<exe name>.ini
<exe folder>\<exe name>.log
```

Example:

```ini
[Launch]
Path=C:\Windows\System32\cmd.exe
Arguments=
WorkingDirectory=C:\Windows\System32
Desktop=WinSta0\Winlogon
ShowWindow=1

[UI]
StartMinimized=1
TopMost=0
AutoLockMinutes=5

[Security]
Kdf=PBKDF2-SHA256
Iterations=210000
SaltHex=
PasswordPbkdf2HashHex=
KeepLegacySha256Hash=0
MaxAttempts=3
LockoutSeconds=30
```

Set or reset the password from a normal desktop:

```cmd
cd /d C:\Program Files\SecureDesktopLauncher
build\SecureDesktopPasswordLauncher.exe set-password
```

`set-password` writes the launcher-local INI and preserves the current launch/UI policy values. New saves use PBKDF2-SHA256 with a random salt and remove the older salted SHA-256 hash by default. If an older config still has only `PasswordHashHex`, the launcher upgrades it after the next successful password verification. Set `KeepLegacySha256Hash=1` only if rollback to an older binary is required.

At normal launch, the password launcher checks that the target and working directory exist before each `CreateProcessW` call.

## Service Launching The Gate

Example service section:

```ini
[Program:CommandPromptGate]
Enabled=1
Path=C:\Program Files\SecureDesktopLauncher\build\SecureDesktopPasswordLauncher.exe
WorkingDirectory=C:\Program Files\SecureDesktopLauncher\build
Desktop=WinSta0\Winlogon
IncludeUsers=DOMAIN\UserName
PreventDuplicate=1
StopOnServiceStop=1
```

Restart the service after changing config:

```cmd
sc stop SecureDesktopLauncher
sc start SecureDesktopLauncher
```

## Password Gate Behavior

- The initial password prompt can start minimized (`StartMinimized=1`) so it does not steal focus from sign-in, UAC, or Ctrl+Alt+Delete.
- `TopMost=1` makes the prompt/control window topmost.
- `AutoLockMinutes=0` disables automatic locking. Positive values lock launched programs after that many minutes of Windows input inactivity.
- `MaxAttempts` controls one prompt batch. Failed batches wait `LockoutSeconds` before another prompt.
- The title bar close button minimizes the gate.
- The password prompt has no `Cancel` button.
- `Lock` hides launched windows and returns to the password prompt.
- `Exit` terminates processes spawned by the password launcher, clears tracked windows, and returns to the password prompt.
- If the service stops and terminates the gate, the gate's Job object should terminate spawned child processes.

## Local Files

The following local files are ignored by git and should not be packaged from a working tree by accident:

- `SecureDesktopLauncher.ini`
- `SecureDesktopLauncher.log`
- `SecureDesktopPasswordLauncher.ini`
- `SecureDesktopPasswordLauncher.log`
- `build\*`

## Release

Prebuilt binaries: [SecureDesktopLauncher v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/SecureDesktopLauncher-v1).
