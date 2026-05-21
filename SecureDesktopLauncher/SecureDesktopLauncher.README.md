# Secure Desktop Launcher

Generic Windows service for launching configured programs as `LocalSystem` on each matching session's secure desktop, normally `WinSta0\Winlogon`.

## Configuration

The service reads:

```text
<exe folder>\SecureDesktopLauncher.ini
<exe parent folder>\SecureDesktopLauncher.ini
```

This lets `build\SecureDesktopLauncherService.exe` use the config in the project root.

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
Path=D:\Tools\ExampleOverlay.exe
Arguments=--session {SessionId} --user "{Account}"
WorkingDirectory=D:\Tools
Desktop=WinSta0\Winlogon
PreventDuplicate=1
StopOnServiceStop=1
LaunchSpacingMs=3000
ShowWindow=4
IncludeUsers=
ExcludeUsers=
```

Add programs with additional `[Program:Name]` sections.

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

Rules:

- Empty `IncludeUsers` means all users.
- Plain names match `WTSUserName`.
- `DOMAIN\User` matches the full account.
- `*` and `?` wildcards are supported.
- Global and per-program filters are both applied.

Examples:

```ini
; only one account
IncludeUsers=DOMAIN\UserName

; everyone except one user
ExcludeUsers=DOMAIN\ExcludedUser

; all domain users except test accounts
IncludeUsers=DOMAIN\*
ExcludeUsers=DOMAIN\test*
```

## Program Count

`MaxProgramsPerSession=0` starts every enabled matching program.

Set a positive value to cap how many matching program sections are started per session:

```ini
MaxProgramsPerSession=2
```

## Build

```cmd
build_launcher.cmd
```

Syntax-only check:

```cmd
build_launcher.cmd check
```

Side-by-side build when the installed EXE is locked:

```cmd
build_launcher.cmd new
```

## Install Or Update

From an elevated prompt:

```cmd
build\SecureDesktopLauncherService.exe install
sc start SecureDesktopLauncher
```

If the service already exists, `install` updates the binary path and display name.

## Remove

```cmd
sc stop SecureDesktopLauncher
build\SecureDesktopLauncherService.exe uninstall
```

## Notes

Processes are launched as `LocalSystem`, not as the interactive user. Make sure paths and config files are readable by SYSTEM.

On service stop, the service terminates configured SYSTEM-owned processes whose executable paths match enabled program sections with `StopOnServiceStop=1`. Normal user-owned processes are not targeted.
## Password-Gated Command Prompt

`SecureDesktopPasswordLauncher.exe` is a small gate program for launching a configured target only after a password is accepted.

Configured target:

```ini
[Launch]
Path=C:\Windows\System32\cmd.exe
Arguments=
WorkingDirectory=C:\Windows\System32
Desktop=WinSta0\Winlogon
ShowWindow=1
```

The main launcher config starts the gate only for `DOMAIN\UserName`:

```ini
[Program:CommandPromptGate]
Enabled=1
Path=C:\Tools\SecureDesktopLauncher\build\SecureDesktopPasswordLauncher.exe
WorkingDirectory=C:\Tools\SecureDesktopLauncher\build
IncludeUsers=DOMAIN\UserName
PreventDuplicate=1
StopOnServiceStop=1
```

Set or reset the gate password from your normal desktop:

```cmd
cd /d C:\Tools\SecureDesktopLauncher
build\SecureDesktopPasswordLauncher.exe set-password
```

The password is not stored as plaintext. The config stores `Kdf=PBKDF2-SHA256`, an iteration count, a random salt, and the derived hash in `SecureDesktopPasswordLauncher.ini`. Older salted SHA-256-only configs are still accepted so existing installs can be unlocked and reset. New password saves remove the older `PasswordHashHex` rollback hash by default; set `[Security] KeepLegacySha256Hash=1` before running `set-password` only if you need rollback to an older binary.

After setting the password, restart the service to launch the gate immediately:

```cmd
sc stop SecureDesktopLauncher
sc start SecureDesktopLauncher
```

The gate keeps the spawned target process in a Windows Job object with kill-on-close. If the service stops and terminates the gate, the child process should also be terminated.
Password gate UI behavior:

```ini
[UI]
StartMinimized=1
TopMost=0
AutoLockMinutes=5
```

Password retry policy:

```ini
[Security]
MaxAttempts=3
LockoutSeconds=30
```

`MaxAttempts` limits one password prompt batch. When the already-running gate is locked and a batch fails, `LockoutSeconds` delays the next prompt instead of immediately reopening it.

With this default, the password window is created as a normal Alt+Tab window and shown minimized without activation. It should not steal focus from sign-in, UAC, or the Ctrl+Alt+Delete screen. Use Alt+Tab on the secure desktop to bring it forward.
`AutoLockMinutes=0` disables automatic locking. Positive values lock the launched programs after that many minutes of Windows input inactivity.
Close/minimize behavior:

- The title bar minimize button minimizes the password gate.
- The title bar close button also minimizes the password gate.
- The password prompt has no `Cancel` button.
- In the unlocked control window, `Lock` hides launched windows and returns to the password prompt.
- `Exit` terminates processes spawned by the password launcher, clears tracked windows, and returns to the password prompt instead of exiting the launcher process.

