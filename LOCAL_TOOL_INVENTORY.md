# Local Tool Inventory

Scanned on 2026-05-12 from `D:\Users\Amiya\AppData\Local\Programs\Scripts\AIProjects`.

This is a practical inventory for future Codex work. It focuses on developer tools, compilers, package managers, media utilities, industrial/automation software, and command-line programs that are installed or discoverable. It does not list every Windows `System32` utility individually.

## Machine

- Computer: `A1504VA`
- OS: Windows 10 Enterprise 22H2, build `22621.6060`
- Architecture: `AMD64`
- Logical processors: `12`
- PowerShell: `7.6.0`
- Registry installed-program entries found: `444`
- Application commands discoverable from current PATH: `1103`
- Unique application command names discoverable from current PATH: `1094`

## High-Value PATH Directories

- `C:\Windows\system32` - 703 commands
- `D:\ProgramData\Strawberry\perl\bin` - 105 commands
- `C:\Program Files\WinGet\Links` - 77 commands, mainly Sysinternals links
- `D:\ProgramData\Strawberry\c\bin` - 74 commands
- `C:\Program Files (x86)\Common Files\Siemens\ACE\Bin` - 16 commands
- `D:\Program Files\Git\cmd` - 10 commands
- `D:\Program Files\ImageMagick-6.9.13-Q16-HDRI` - 9 commands
- `D:\Windows Kits\10\Windows Performance Toolkit` - 9 commands
- `C:\Windows\System32\OpenSSH` - 7 commands
- `%userprofile%\AppData\Local\OpenAI\Codex\bin` - Codex `node`, `rg`, and related commands
- `D:\ProgramData\PHP` - PHP command directory
- `D:\Program Files\MATLAB\R2021a\bin` - MATLAB command directory
- `C:\Program Files\dotnet` - .NET host

## Compilers And Build Tools

### MSVC / Visual Studio

- Visual Studio Community 2019: `16.11.54`
  - Install path: `D:\Program Files (x86)\Microsoft Visual Studio\2019\Community`
  - IDE: `D:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\devenv.exe`
- Visual Studio Build Tools 2019: `16.11.54`
  - Install path: `D:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools`
- MSVC C/C++ compiler:
  - `cl.exe` version `19.29.30159`
  - Main x64 path: `D:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\bin\Hostx64\x64\cl.exe`
- Linker/library tools:
  - `link.exe` version `14.29.30159`
  - `lib.exe` version `14.29.30159`
  - `nmake.exe` version `14.29.30159`
- MSBuild:
  - `D:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe`
  - Version `16.11.6`
- Developer environment setup scripts:
  - `D:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat`
  - `D:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat`
  - `D:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat`
- Note: MSVC tools are installed but `cl.exe` is not on the default PATH. Use a Visual Studio developer shell or call `VsDevCmd.bat` before compiling.

### Windows SDK / ADK

- Windows SDK: `10.1.22621.5040`
- Windows ADK: `10.1.22621.5337`
- Tools found:
  - `rc.exe`
  - `midl.exe`
  - `signtool.exe`
  - `makeappx.exe`
- Main SDK tool root: `D:\Windows Kits\10`

### MinGW / Strawberry C Toolchain

Installed under `D:\ProgramData\Strawberry\c\bin` and available on PATH.

- `gcc` / `g++`: MinGW-W64 `13.2.0`
- `gfortran`: MinGW-W64 `13.2.0`
- `gdb`
- `ar`, `as`, `ld`, `objdump`, `strip`, `windres`
- `mingw32-make`
- `cmake`: `3.29.2`
- `ninja`: `1.12.0`
- Other useful tools in the same directory: `ctags`, `cppcheck`, `doxygen`, `ctest`, `cpack`, `dos2unix`

## Runtimes And Package Managers

- Git: `2.54.0.windows.1`
  - Path: `D:\Program Files\Git\cmd\git.exe`
- PowerShell Core: `7.6.0`
  - Path: `C:\Program Files\PowerShell\7\pwsh.exe`
- Node.js:
  - PATH Node: `v24.14.0`
  - Codex bundled Node: `%userprofile%\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe`
  - OpenAI Codex Node: `%userprofile%\AppData\Local\OpenAI\Codex\bin\node.exe`
  - `npm`, `npx`, `pnpm`, `yarn`, `bun`, and `deno` were not found on PATH.
- Python:
  - Codex bundled Python: `Python 3.12.13`
  - Path: `%userprofile%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`
  - No regular system `python`, `py`, or `pip` command was found on PATH.
- .NET:
  - Host: `10.0.5`
  - Runtimes installed: `3.1.32`, `6.0.36`, `8.0.17`, `8.0.19`, `8.0.21`, `8.0.24`, `9.0.13`, `10.0.5`
  - ASP.NET Core runtimes: `8.0.19`, `8.0.21`, `8.0.24`
  - Windows Desktop runtimes: `6.0.36`, `8.0.17`, `8.0.24`, `9.0.13`, `10.0.5`
  - No .NET SDK was listed by `dotnet --list-sdks`.
- PHP:
  - PHP CLI: `8.2.30`
  - Path: `D:\ProgramData\PHP\php.exe`
  - Composer: `2.9.5`
- Perl:
  - Strawberry Perl: `5.42.0`
  - Path: `D:\ProgramData\Strawberry\perl\bin\perl.exe`
  - `cpan` and `cpanm` available.
- MATLAB:
  - MATLAB R2021a, version `9.10`
  - Path: `D:\Program Files\MATLAB\R2021a`

Not found on PATH in this scan: Java/JDK, Go, Rust/Cargo, Ruby, R, Julia, Docker, Kubernetes tools, Terraform, AWS CLI, Azure CLI, Google Cloud CLI, `uv`, Conda/Mamba, Pandoc, Quarto, FFmpeg.

## Database And Data Tools

- Microsoft SQL Server 2022 installed.
- SQL command-line tools on PATH:
  - `sqlcmd` version `16.0.1000.6`
  - `bcp` version `16.0.1000.6`
  - `dtexec` / `dtutil` version `16.0.1000.6`
  - `osql` version `16.0.1000.6`
- SQL Server folders:
  - `C:\Program Files\Microsoft SQL Server\160`
  - `C:\Program Files\Microsoft SQL Server\Client SDK\ODBC\170\Tools\Binn`
- MariaDB 10.11 installed:
  - Install path: `D:\Program Files\MariaDB 10.11`
  - Tools include `mariadb.exe`, `mariadbd.exe`, `mariadb-dump.exe`, `mariadb-admin.exe`
  - MariaDB `bin` is not on the current PATH.

## Android / Mobile

- Android SDK Platform-Tools package installed: `37.0.0`
  - Package root: `C:\Program Files\WinGet\Packages\Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe`
- scrcpy package installed:
  - Package root: `C:\Program Files\WinGet\Packages\Genymobile.scrcpy_Microsoft.Winget.Source_8wekyb3d8bbwe`
- Caveat: the sandbox could see the WinGet package roots but could not access `adb.exe`, `fastboot.exe`, or `scrcpy.exe` directly. Treat these as installed but not verified/invokable from the current shell.

## Graphics, Media, And Documents

- ImageMagick: `6.9.13-40 Q16-HDRI`
  - Path: `D:\Program Files\ImageMagick-6.9.13-Q16-HDRI`
- Inkscape: `1.4.2`
  - Path: `D:\Program Files\Inkscape\bin\inkscape.exe`
- 7-Zip ZS: `25.01 ZS v1.5.7 R4`
  - Path: `D:\Program Files\7-Zip-Zstandard\7z.exe`
- Adobe Media Encoder 2026: `26.0`
  - Path: `D:\Program Files\Adobe\Adobe Media Encoder 2026`
- Audacity: `3.7.7`
  - Path: `D:\Program Files\Audacity`
- Microsoft 365 Apps for enterprise: `16.0.19929.20136`
- Google Drive: `125.0.0.0`
- Google Chrome: `148.0.7778.97`
- Microsoft Edge WebView2 Runtime: `147.0.3912.98`

## Industrial / Automation Software

- Siemens TIA Portal V21 appears installed.
  - Main portal path: `D:\Program Files\Siemens\Automation\Portal V21\Bin\Siemens.Automation.Portal.exe`
- Siemens PLCSIM V21 installed.
  - `D:\Program Files\Siemens\Automation\PLCSIM_V21\S7PLCSIMV21.exe`
- Siemens PLCSIM Advanced installed.
- WinCC Runtime Advanced V17 installed.
- WinCC Runtime Advanced Simulator installed.
- Factory I/O: `2.5.8`
- GMWIN 4: `1.00.11`
- WinSPS-S7: `5.036`
- OPC Foundation components are installed.

## Admin / System Utilities

- Sysinternals Suite installed through WinGet.
  - Link directory: `C:\Program Files\WinGet\Links`
  - Examples found: `accesschk`, `Autoruns`, `autorunsc`, `Bginfo`, `Coreinfo`, `du`, `handle`, `junction`, `Listdlls`, `Procmon` likely available through the same suite.
- PowerToys installed.
  - Path: `D:\Program Files\PowerToys`
- usbipd-win: `5.3.0`
  - Path: `C:\Program Files\usbipd-win\usbipd.exe`
- OpenSSH client tools:
  - `ssh`, `scp`, `sftp`, `ssh-keygen`
- Sunshine installed:
  - Path: `D:\Program Files\Sunshine\sunshine.exe`

## Codex-Specific Tools Available In This Session

- PowerShell shell execution
- Node REPL MCP
- Browser plugin / browser-use skill for local browser automation
- Documents plugin for DOCX creation/editing/render verification
- Presentations plugin for PPTX creation/editing/render verification
- Spreadsheets plugin for XLSX/CSV creation/editing/analysis/render verification
- Image generation skill
- OpenAI docs skill
- Bundled runtime paths:
  - Node: `%userprofile%\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe`
  - Node packages: `%userprofile%\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\node_modules`
  - Python: `%userprofile%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`
  - Python packages: `%userprofile%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python`

## Practical Notes

- For C/C++ builds, prefer MinGW tools when a project expects GCC/Make/CMake/Ninja, because they are already on PATH.
- For MSVC builds, initialize the developer environment first. Example:

```bat
call "D:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64
cl
```

- For Python automation inside Codex, use the bundled Python path above unless a project provides its own virtual environment.
- For Node scripts inside Codex, use the bundled Node path above. Package manager commands were not found on PATH, so installing npm dependencies may require a separate package manager setup.
- For .NET projects, runtimes are available but no SDK was detected. Building SDK-style projects may require installing or locating a .NET SDK.
- MariaDB tools are installed but not on PATH; call them by full path or add `D:\Program Files\MariaDB 10.11\bin` temporarily.
- WinGet package roots for Android Platform Tools and scrcpy exist, but the current sandbox could not verify the contained executables.
