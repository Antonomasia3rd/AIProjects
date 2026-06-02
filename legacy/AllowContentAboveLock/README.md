# AllowContentAboveLock

Windows service that watches notification settings under each loaded user hive and forces `AllowContentAboveLock=1` for notification app entries it can access.

This is useful when Windows or app updates reset lock-screen notification visibility and you want those entries restored automatically.

## Requirements

- Windows with .NET Framework 4.x.
- Administrator rights to install/start the service.

## Build

From this folder:

```cmd
mkdir build 2>nul
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /target:exe /optimize+ /out:build\AllowContentAboveLock.exe /r:System.ServiceProcess.dll AllowContentAboveLock.cs
```

## Install

Run from an elevated command prompt:

```cmd
sc.exe create AllowContentAboveLockService binPath= "%CD%\build\AllowContentAboveLock.exe" start= auto
sc.exe start AllowContentAboveLockService
```

## Uninstall

Run from an elevated command prompt:

```cmd
sc.exe stop AllowContentAboveLockService
sc.exe delete AllowContentAboveLockService
```

## Runtime Behavior

- Watches `HKEY_USERS` for newly loaded user hives.
- Attaches to loaded domain/local `S-1-5-21-*` and Azure AD `S-1-12-1-*` user hives.
- Watches `HKU\<SID>\Software\Microsoft\Windows\CurrentVersion\Notifications\Settings` for child-key and value changes.
- Sets `AllowContentAboveLock` to DWORD `1` on notification setting subkeys.
- Creates `AllowContentAboveLock.ini` beside the executable if missing.
- Logs to `AllowContentAboveLock.log` beside the executable by default. Set `[Settings] LoggingEnabled=0` in the local INI to disable file logging.

## Generated Files

- `build\AllowContentAboveLock.exe`
- `build\AllowContentAboveLock.ini`
- `build\AllowContentAboveLock.log`

Generated build output is ignored by git.

## Release

Prebuilt binary: [AllowContentAboveLock v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/AllowContentAboveLock-v1).
