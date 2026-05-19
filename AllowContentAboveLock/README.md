# AllowContentAboveLock

Windows service that watches notification settings under each loaded user hive and forces `AllowContentAboveLock=1` for every notification app entry it can access.

This is useful when Windows or app updates reset notification visibility on the lock screen and you want those entries restored automatically.

## Requirements

- Windows with .NET Framework 4.x.
- Administrator rights to install/start the service and create the Event Log source.

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

## Notes

- Logs are written to the Windows Application Event Log using source `AllowContentAboveLockService`.
- The service watches `HKU\<SID>\Software\Microsoft\Windows\CurrentVersion\Notifications\Settings` for loaded user hives.
- The release build is available at [AllowContentAboveLock v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/AllowContentAboveLock-v1).
