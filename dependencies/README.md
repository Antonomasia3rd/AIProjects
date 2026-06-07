# Shared C++ baseline

New resident desktop projects should compose these modules instead of copying a
product implementation:

- `desktop_app_baseline.h`: stable aggregate entry point that includes the
  common modules below in their supported dependency order.
- `baseline_app.h`: single-instance identity/signaling, path-scoped stable
  hashing, taskbar recreation registration, resident shutdown state, command-line
  help templates, and flat/dropdown tray sections.
- `app_paths.inc`: executable sidecar path discovery for per-product INI and
  log files, including growable current-module path lookup and configured INI
  override handling.
- `logging.inc`: UTF-8 sidecar file logging and bounded recent-log buffering,
  with concurrent appender sharing for helper/broker processes.
- `config_ini.inc`: `IniConfigStore`, synchronized INI mutation, encoding, and
  document parsing.
- `command_line.inc`: option value parsing, INI setting syntax, and boolean
  aliases.
- `tray.inc`: low-level menu construction, popup ownership, and notifications.
- `core.inc`: path, text, JSON, and environment primitives.

Include `desktop_app_baseline.h` from product translation units. That aggregate
header is the supported public entry point for resident desktop apps and owns the
shared include order. Individual `.inc` modules are include-guarded for focused
tests and compatibility, but they are not guaranteed to be standalone unless a
file explicitly says so. Optional facilities such as `dpapi.inc` stay separate.
Product code should keep policy and commands in product modules while using
these shared contracts for lifecycle, sidecar paths, logging, and persistence behavior.

## INI dialect compatibility

The shared INI helpers must stay compatible with DesktopStub's established INI
format. This is now the repository baseline for C++ projects:

- write UTF-8 with BOM;
- write assignments as `"Name" = "Value"`;
- preserve comments, unrelated lines, and ordering where practical;
- preserve whitespace inside quoted values;
- preserve unknown backslash sequences in raw INI values, especially Windows
  paths such as `C:\Users\Amiya\Desktop\file.txt`;
- keep app-level escape decoding separate from raw INI parsing, so templates may
  interpret `\n`, `\r`, and `\t` without making every INI value use those
  escapes.

Do not replace this with `GetPrivateProfileStringW` / `WritePrivateProfileStringW`
or another parser that changes quoting, comments, order, trailing spaces, or path
backslashes.
