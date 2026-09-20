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

Normal launches start the tray icon unless `[app] show_tray = false` is set. The
GUI-subsystem process does not create and then hide a console during startup. A
console is allocated only when `[app] show_console = true`, `--show-console`,
or the tray setting enables it. Right-click the tray icon to refresh, reload,
toggle common presence/logging settings, open the config, inspect recent logs,
enable the per-profile Startup shortcut, or exit. The tray root follows the DesktopStub baseline order and displays the
tag-derived build version. `--version` reports the same release tag and four-part
version stored in the executable's Win32 version resource.

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
.\build\DiscordRPC.exe --startup
.\build\DiscordRPC.exe --set general.client_id=YOUR_APPLICATION_ID
.\build\DiscordRPC.exe --transport gateway --token YOUR_DISCORD_TOKEN
```

## Configuration Notes

- `[general] client_id` must be a valid Discord application ID.
- `[general] transport_mode` can be `ipc`, `gateway`, or `auto`. `auto` tries IPC first, then falls back to Gateway.
- **Account risk:** Gateway mode automates a normal Discord user token. Discord explicitly forbids self-bots and warns that detected accounts can be terminated. Prefer the local IPC transport; use Gateway only if you accept that risk.
- Gateway tokens are configured only through the INI-backed token fields: DPAPI `[general] token_protected`, or legacy plaintext `[general] token` before migration.
- Plaintext `[general] token` values are migrated to `token_protected=dpapi:v1:utf8:<hex>` for the current Windows user and then cleared. Existing unversioned `dpapi:<hex>` values from older DiscordRPC builds remain readable as UTF-8. If a plaintext token is supplied later, it replaces the existing protected token instead of being discarded.

The automatic DPAPI migration above is current code behavior, not the new
repository policy. Making protection opt-in while retaining existing encrypted
profiles is unfinished work; there is currently no portable plaintext mode.
Setup convenience takes priority over new security work during consolidation.
Anyone who can read a plaintext Discord token can use that session, so keep
tokens and INI files private and out of Git. DPAPI restricts decryption to the
Windows user but does not stop other code running as that user. See
[the shared audit](../docs/audit-shared.md).
- `[general] details_template` and `state_template` support tokens such as `{win_title}`, `{cpu}`, `{ram_used}`, `{ram_total}`, `{ram_pct}`, `{uptime}`, `{battery_pct}`, `{time}`, `{date}`, `{username}`, and `{computer}`.
- `[layout]` toggles details/state fields, activity images, and buttons.
- `[app] run_at_startup`, `--startup`/`--no-startup`, `--set`/`--bool`, and the tray toggle all control the same per-profile shortcut in the current user's `shell:startup` folder. No Run-registry or scheduled-task startup entry is used. Alternate `--ini` profiles receive separate stable shortcut names and launch with their absolute INI path.
- Command-line setting batches are committed to the INI together. Known boolean values are validated and normalized to `true` or `false`. Startup and INI changes hold both shared locks: enable installs before saving `true`, disable saves `false` before removal, ordinary failures roll back safely, and an interrupted change self-reconciles on the next launch.
- `[large_time_ranges]`, `[small_time_ranges]`, `[large_assets]`, and `[small_assets]` select asset text/image keys by time of day.
- `[afk]` can switch the small asset/text after Windows idle time passes `idle_threshold`.
- `[censor_map]` supports `full_replace`, `word_replace`, and `pattern_replace` rules for the foreground title token.
- `[ipc] connect_timeout_ms` and `response_timeout_ms` tune Discord IPC waits.
- `[gateway]` controls websocket identity metadata, connect/HELLO/READY/send/close timeouts, and public asset-name lookup.
- Most tray/menu/notification labels are configurable in `[strings]`. The tray layout follows `[app] show_menu_as_dropdown`, and `--help` reads `[CommandLineHelp] Template` from an existing INI without creating or repairing the file.

## Source Layout

- `DiscordRPC.cpp`: small translation-unit shell and global app state.
- `..\dependencies\DiscordRPC\drpc_environment.inc`: the common build environment and standalone/engine state declarations.
- `..\dependencies\DiscordRPC\service.h` / `service.cpp`: reusable in-process presence service used by the standalone loop and available to DesktopStub. It compiles the existing presence builder and IPC/Gateway code, reads an explicit immutable profile snapshot, and defaults to preview with sending disabled. Embedded use does not run tray, console, Startup, INI creation or token-migration code. `Stop(timeout)` reports pending cleanup instead of blocking the host indefinitely; the module must remain loaded while cleanup is pending.
- `..\dependencies\desktop_app_baseline.h`: stable shared baseline entry point for lifecycle, command-line, tray, and UTF-8/BOM-aware INI persistence using DesktopStub's quoted assignment style (`"Name" = "Value"`).
- `..\dependencies\startup_shortcut.inc`: shared, ownership-checked `shell:startup` shortcut lifecycle used by native unpackaged apps.
- Implementation code lives under `..\dependencies\DiscordRPC`. Other hosts use the public service API and link its translation unit; standalone UI/lifecycle fragments remain product-owned:
  - `drpc_core.inc`: DiscordRPC path wrappers, logging, console, JSON helpers, and config access glue.
  - `drpc_config_defaults.inc`: default INI values and configurable strings.
  - `drpc_command_line.inc`: command-line parsing and persisted `--set` writes.
  - `drpc_presence.inc`: template tokens and activity JSON generation.
  - `drpc_ipc.inc`: Discord named-pipe IPC transport.
  - `drpc_gateway.inc`: Discord Gateway websocket transport, heartbeat handling, Gateway activity shaping, asset ID lookup, and DPAPI-token use.
  - `drpc_tray.inc`: tray window, menu, notifications.
  - `drpc_app.inc`: run loop, single-instance handling, reload/refresh orchestration.

## Generated Files

- `build\DiscordRPC.exe`
- `<exe folder>\<exe name>.ini`
- `<exe folder>\<exe name>.log`
- optional log file controlled by `[app] log_path`; relative log paths resolve beside the executable
- optional profile-scoped `.lnk` in the current user's `shell:startup` folder

Generated binaries, runtime configs, and logs should not be committed.


Logging note: `[app] log_append_lock_wait_ms` controls the bounded wait used when the shared UTF-8 logger appends to the log file. It can be changed through the INI, `--set app.log_append_lock_wait_ms=<ms>`, `--log-lock-wait-ms <ms>`, or the tray Logging menu.
