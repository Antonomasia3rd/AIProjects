# Shared C++ baseline

New resident desktop projects should compose these modules instead of copying a
product implementation:

- `baseline_app.h`: single-instance identity/signaling, taskbar recreation
  registration, command-line help templates, and flat/dropdown tray sections.
- `config_ini.inc`: `IniConfigStore`, synchronized INI mutation, encoding, and
  document parsing.
- `command_line.inc`: option value parsing, INI setting syntax, and boolean
  aliases.
- `tray.inc`: low-level menu construction, popup ownership, and notifications.
- `core.inc`: path, text, JSON, and environment primitives.

`baseline_app.h` is self-contained. The `.inc` modules currently retain the
existing include order for compatibility with DesktopStub and DiscordRPC.
Product code should keep policy and commands in product modules while using
these shared contracts for lifecycle and persistence behavior.
