# Shared C++ baseline

New resident desktop projects should compose these modules instead of copying a
product implementation:

- `desktop_app_baseline.h`: stable aggregate entry point that includes the
  common modules below in their supported dependency order.
- `baseline_app.h`: single-instance identity/signaling, taskbar recreation
  registration, resident shutdown state, command-line help templates, and
  flat/dropdown tray sections.
- `config_ini.inc`: `IniConfigStore`, synchronized INI mutation, encoding, and
  document parsing.
- `command_line.inc`: option value parsing, INI setting syntax, and boolean
  aliases.
- `tray.inc`: low-level menu construction, popup ownership, and notifications.
- `core.inc`: path, text, JSON, and environment primitives.

Include `desktop_app_baseline.h` from product translation units. Individual
modules remain independently guarded for focused tests and compatibility, but
consumers no longer need to reproduce their include order. Optional facilities
such as `dpapi.inc` stay separate. Product code should keep policy and commands
in product modules while using these shared contracts for lifecycle and
persistence behavior.
