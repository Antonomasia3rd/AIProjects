# SecureDesktopLauncher

Windows secure-desktop tooling made of two executables:

- `SecureDesktopLauncherService.exe`: Windows service that launches configured programs as `LocalSystem` on matching sessions/desktops, usually `WinSta0\Winlogon`.
- `SecureDesktopPasswordLauncher.exe`: password-gated launcher for starting a configured target only after a password is accepted.

Detailed configuration and behavior documentation is kept in [SecureDesktopLauncher.README.md](SecureDesktopLauncher.README.md).

## Requirements

- Windows.
- Visual Studio Build Tools with the C++ workload.
- Administrator rights to install/start the service.
- Careful configuration: launched processes run as `LocalSystem`.

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

Side-by-side builds for locked installed binaries:

```cmd
build_launcher.cmd new
build_password_launcher.cmd new
```

Outputs are written to `build\`.

## Install Service

From an elevated command prompt:

```cmd
build\SecureDesktopLauncherService.exe install
sc start SecureDesktopLauncher
```

## Remove Service

```cmd
sc stop SecureDesktopLauncher
build\SecureDesktopLauncherService.exe uninstall
```

## Configuration

Private local configuration files are intentionally ignored by git:

- `SecureDesktopLauncher.ini`
- `SecureDesktopPasswordLauncher.ini`

See [SecureDesktopLauncher.README.md](SecureDesktopLauncher.README.md) for example sections and supported options.

## Release

Prebuilt binaries: [SecureDesktopLauncher v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/SecureDesktopLauncher-v1).
