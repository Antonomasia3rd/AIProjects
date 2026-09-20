# SecureDesktopLauncher

Windows secure-desktop tooling made of two executables:

- `SecureDesktopLauncher.exe`: Windows service that launches configured programs as `LocalSystem` on matching sessions/desktops, usually `WinSta0\Winlogon`.
- `SecureDesktopPasswordLauncher.exe`: password-gated launcher that starts one configured target only after password verification and keeps spawned processes in a kill-on-close Job object.

The current code still enforces protected installation paths and password
storage policies. These are existing restrictions, not the repository's new
rule: setup convenience comes first, and extra security is intended to be
opt-in after functional repair. This privileged integration has not completed
that migration, and there is no general switch to disable its path checks.
Writable launch configuration can grant another program `LocalSystem` access;
protect the current installation and its configured programs. Remaining work
is tracked in [the shared audit](../../docs/audit-shared.md).

## Requirements

- Windows.
- Visual Studio Build Tools with the C++ workload.
- Administrator rights to install/start the service.
- A protected install under a Program Files directory. Launch targets may also live under the Windows directory. Both executables refuse user-writable, reparse-point, and alternate-stream paths rather than trying to repair their permissions.

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

## Source layout

The two project-local `.cpp` files are intentionally thin entry-point overlays.
Their product-owned implementations live in
`dependencies\SecureDesktopLauncher\service_app.inc` and
`dependencies\SecureDesktopLauncher\password_app.inc`, where they compose the
root-level shared desktop baseline, release-version, and privileged-path modules.
Shared consumers should use extracted provider/helper interfaces, without
starting another application's service loop or importing its globals.

## Service Install

Do not install the service directly from a source checkout, download folder, temporary directory, or user profile. Build it, then copy the executable and its INI to an administrator-controlled directory under Program Files. From an elevated prompt:

```cmd
mkdir "C:\Program Files\SecureDesktopLauncher"
copy build\SecureDesktopLauncher.exe "C:\Program Files\SecureDesktopLauncher\"
copy build\SecureDesktopPasswordLauncher.exe "C:\Program Files\SecureDesktopLauncher\"
rem Create C:\Program Files\SecureDesktopLauncher\SecureDesktopLauncher.ini from the example below.
"C:\Program Files\SecureDesktopLauncher\SecureDesktopLauncher.exe" install
sc start SecureDesktopLauncher
```

If the service exists, `install` updates the binary path and display name.

Command-line inspection is side-effect free:

```cmd
SecureDesktopLauncher.exe --help
SecureDesktopLauncher.exe --version
SecureDesktopLauncher.exe validate
```

`validate` checks the protected executable/configuration/target paths and parses
the configuration without launching anything or changing SCM state. The legacy
`test` command is now an alias for this dry validation; it no longer reconciles
sessions or launches configured programs. Unknown commands fail instead of
falling through to the service dispatcher.

Remove it with:

```cmd
sc stop SecureDesktopLauncher
"C:\Program Files\SecureDesktopLauncher\SecureDesktopLauncher.exe" uninstall
```

## Service Configuration and Diagnostics

The service reads its configuration from the executable directory using the executable base name:

```text
<exe folder>\<exe name>.ini
```

If the executable is renamed, the INI name follows it. The LocalSystem service does not write a sidecar log: warnings go to the Windows Application event log under source `SecureDesktopLauncher` and to debugger output. This avoids turning a file path beside a privileged executable into a reparse-point or hard-link write target.

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

Because every configured process receives a `LocalSystem` token, the service applies a fail-closed protected-path policy to its own executable and INI, every launch target, and every working directory.

The current validator accepts a path only when it meets these checks:

- be an absolute local path without an alternate data stream;
- already exist under the Windows, Program Files, or Program Files (x86) directory tree;
- contain no reparse-point component;
- be owned by `LocalSystem`, `Administrators`, or `TrustedInstaller`;
- have no applicable allow ACE that grants write-like access to another principal; protected directories also reject untrusted create-file/create-directory rights so a sibling DLL, plugin, or sidecar cannot be planted beside privileged code.

The service opens and retains every component from the volume root through each accepted object, inspects security through those handles, keeps the INI chain pinned while parsing it, and reopens/pins executable and working-directory chains immediately before `CreateProcessAsUserW`. The handles deny write and delete sharing during the privileged operation. Service installation and startup fail if the service executable, configuration, or any enabled program section is unsafe; diagnostics go to the Windows Application event log/debug output. Because the project does not install a registry-backed Event Log message resource, Event Viewer may show the standard unregistered-source wording; the warning insertion text remains visible in the event details and debug output.

This project never changes ACLs, ownership, inheritance, integrity labels, or other access-control state. Move files to a protected install location and configure permissions administratively; the service only inspects and refuses unsafe state.

## Threat Model Notes

This tool intentionally creates `LocalSystem` processes on interactive desktops. Treat the service executable, password launcher, INI files, launched programs, and their parent directories as privileged code.

Install into a location that is already protected. Retaining validated handles blocks ordinary name-based replacement races, but Windows cannot retroactively revoke dangerous rights on a handle an attacker acquired before the deployment was secured.

`CommandLine` only changes the command-line string passed to `CreateProcessAsUserW`; `Path` is still passed as `lpApplicationName`. Keep `CommandLine` empty unless a target truly needs custom `argv[0]` or unusual quoting.

The service cannot infer the security meaning of arguments. Scripts, plug-ins, response files, and other argument-referenced content consumed by a trusted executable must also be administrator-controlled; the path policy only verifies `Path` and `WorkingDirectory`.

## Password Gate Config

The password launcher reads and writes one protected INI beside itself using the executable base name:

```text
<exe folder>\<exe name>.ini
```

It does not write a privileged sidecar log. Diagnostics use debugger output and
the Windows Application event log.

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

`PasswordAttemptCount` and `LockoutUntilUtcFileTime` are internal state written
atomically by the launcher. Do not edit them while it is running.

Set or reset the password from a normal desktop:

```cmd
cd /d C:\Program Files\SecureDesktopLauncher
SecureDesktopPasswordLauncher.exe set-password
```

`set-password` atomically rewrites the launcher-local INI through the shared configuration layer and preserves the current launch/UI policy values. New saves use PBKDF2-SHA256 with a random salt and remove the older salted SHA-256 hash by default. If an older config still has only `PasswordHashHex`, the launcher upgrades it after the next successful password verification. Set `KeepLegacySha256Hash=1` only if rollback to an older binary is required.

At normal launch, the password launcher checks that the target and working directory exist before each `CreateProcessW` call.

## Service Launching The Gate

Example service section:

```ini
[Program:CommandPromptGate]
Enabled=1
Path=C:\Program Files\SecureDesktopLauncher\SecureDesktopPasswordLauncher.exe
WorkingDirectory=C:\Program Files\SecureDesktopLauncher
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
- `MaxAttempts` is a protected, cross-process attempt budget (1 through 100).
  Each issued password prompt reserves a slot before accepting a guess, so
  parallel launchers cannot multiply the budget. When the budget is exhausted,
  the UTC lockout deadline is persisted in the protected INI and therefore
  survives process restarts. `LockoutSeconds` is clamped to 1 through 3600.
- Failure to read, reserve, save, or reset protected attempt state denies access.
- The title bar close button minimizes the gate.
- The password prompt has no `Cancel` button.
- `Lock` hides launched windows and returns to the password prompt.
- `Exit` terminates processes spawned by the password launcher, clears tracked windows, and returns to the password prompt.
- If the service stops and terminates the gate, the gate's Job object should terminate spawned child processes.

## Local Files

The following local files are ignored by git and should not be packaged from a working tree by accident:

- `SecureDesktopLauncher.ini`
- `SecureDesktopPasswordLauncher.ini`
- `build\*`

## Release

Prebuilt binaries: [SecureDesktopLauncher v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/SecureDesktopLauncher-v1).
