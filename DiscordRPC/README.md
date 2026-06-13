# DiscordRPC

C++ Win32 Discord Rich Presence utility with Discord IPC transport, Discord Gateway transport, DPAPI-protected token storage, dynamic system/window placeholders, INI-backed configuration, command-line overrides, and a tray-first control surface.

## Requirements

- Windows 10/11.
- Discord desktop app for the local IPC pipe when using `transport_mode = ipc`.
- A Discord application/client ID from the Discord Developer Portal.
- Visual Studio Build Tools with the C++ workload.

## Build

From this folder:

```cmd
build.cmd
```

Syntax-only check:

```cmd
build.cmd check
```

Output:

```text
build\DiscordRPC.exe
```

## Run

```powershell
.\build\DiscordRPC.exe
```

Normal launches start the tray icon unless `[app] show_tray = false` is set. Right-click the tray icon to refresh, reload, toggle common presence/logging settings, open the config, inspect recent logs, or exit.

Exit follows the same graceful-then-force model as DesktopStub. DiscordRPC first
stops new updates, clears the active presence when possible, and completes IPC
or Gateway cleanup. While cleanup is pending, the tray menu shows `Force
shutdown`; using it exits immediately and records a warning for the next start
because presence clearing or the Gateway close handshake may have been skipped.

Pass a config path:

```powershell
.\build\DiscordRPC.exe .\config.ini
```

Without an explicit config path, the app uses `DiscordRPC.ini` beside `DiscordRPC.exe`. If the executable is renamed, default INI/log names follow the renamed executable.

Useful command-line paths:

```powershell
.\build\DiscordRPC.exe --dry-run
.\build\DiscordRPC.exe --dry-run-full
.\build\DiscordRPC.exe --once --verbose
.\build\DiscordRPC.exe --no-tray
.\build\DiscordRPC.exe --set general.client_id=YOUR_APPLICATION_ID
.\build\DiscordRPC.exe --transport gateway --token YOUR_DISCORD_TOKEN
```

## Configuration Notes

- `[general] client_id` must be a valid Discord application ID.
- `[general] transport_mode` can be `ipc`, `gateway`, or `auto`. `auto` tries IPC first, then falls back to Gateway.
- **Account risk:** Gateway mode automates a normal Discord user token. Discord explicitly forbids self-bots and warns that detected accounts can be terminated. Prefer the local IPC transport; use Gateway only if you accept that risk.
- Gateway tokens are configured only through the INI-backed token fields: DPAPI `[general] token_protected`, or legacy plaintext `[general] token` before migration.
- Plaintext `[general] token` values are migrated to `token_protected=dpapi:<hex>` for the current Windows user and then cleared. If a plaintext token is supplied later, it replaces the existing protected token instead of being discarded.
- `[general] details_template` and `state_template` support tokens such as `{win_title}`, `{cpu}`, `{ram_used}`, `{ram_total}`, `{ram_pct}`, `{uptime}`, `{battery_pct}`, `{time}`, `{date}`, `{username}`, and `{computer}`.
- `[layout]` toggles details/state fields, activity images, and buttons.
- `[large_time_ranges]`, `[small_time_ranges]`, `[large_assets]`, and `[small_assets]` select asset text/image keys by time of day.
- `[afk]` can switch the small asset/text after Windows idle time passes `idle_threshold`.
- `[censor_map]` supports `full_replace`, `word_replace`, and `pattern_replace` rules for the foreground title token.
- `[ipc] connect_timeout_ms` and `response_timeout_ms` tune Discord IPC waits.
- `[gateway]` controls websocket identity metadata, connect/HELLO/READY/send/close timeouts, and public asset-name lookup.
- Most tray/menu/notification labels are configurable in `[strings]`. The tray layout follows `[app] show_menu_as_dropdown`, and `--help` reads `[CommandLineHelp] Template` from an existing INI without creating or repairing the file.

## Source Layout

- `DiscordRPC.cpp`: small translation-unit shell and global app state.
- `..\dependencies\desktop_app_baseline.h`: stable shared baseline entry point for lifecycle, command-line, tray, and UTF-8/BOM-aware INI persistence using DesktopStub's quoted assignment style (`"Name" = "Value"`).
- `src/drpc_core.inc`: DiscordRPC path wrappers, logging, console, JSON helpers, and config access glue.
- `src/drpc_config_defaults.inc`: default INI values and configurable strings.
- `src/drpc_command_line.inc`: command-line parsing and persisted `--set` writes.
- `src/drpc_presence.inc`: template tokens and activity JSON generation.
- `src/drpc_ipc.inc`: Discord named-pipe IPC transport.
- `src/drpc_gateway.inc`: Discord Gateway websocket transport, heartbeat handling, Gateway activity shaping, asset ID lookup, and DPAPI-token use.
- `src/drpc_tray.inc`: tray window, menu, notifications.
- `src/drpc_app.inc`: run loop, single-instance handling, reload/refresh orchestration.

## Generated Files

- `build\DiscordRPC.exe`
- `<exe folder>\<exe name>.ini`
- `<exe folder>\<exe name>.log`
- optional log file controlled by `[app] log_path`; relative log paths resolve beside the executable

Generated binaries, runtime configs, and logs should not be committed.


Logging note: `[app] log_append_lock_wait_ms` controls the bounded wait used when the shared UTF-8 logger appends to the log file. It can be changed through the INI, `--set app.log_append_lock_wait_ms=<ms>`, `--log-lock-wait-ms <ms>`, or the tray Logging menu.
