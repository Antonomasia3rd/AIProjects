# DiscordRPC

C# Discord Rich Presence utility with a tray-first configuration UI, Discord IPC mode, optional Gateway mode, Auto mode, and support for dynamic system/window placeholders.

This is a C# replacement for the experimental Python `DiscordRPC.py`.

## Requirements

- Windows with .NET Framework 4.x.
- Discord desktop app for IPC mode.
- A Discord application/client ID from the Discord Developer Portal.
- Gateway mode requires a Discord token and remains the unofficial/user-token path; IPC is the recommended default.

## Build

From this folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

Output:

```text
build\DiscordRPC.exe
```

## Run

```powershell
.\build\DiscordRPC.exe
```

Normal launches start the tray icon unless `[app] show_tray = false` is set. Right-click the tray icon to change most settings directly.

Pass a config path:

```powershell
.\build\DiscordRPC.exe .\config.ini
```

Run without the tray icon:

```powershell
.\build\DiscordRPC.exe --no-tray
```

Validate config parsing and generated activity JSON without connecting to Discord:

```powershell
.\build\DiscordRPC.exe --dry-run
```

Dry-run output redacts sensitive local tokens by default. Use full mode only for private debugging:

```powershell
.\build\DiscordRPC.exe --dry-run-full
```

Run one live update and exit:

```powershell
.\build\DiscordRPC.exe --once
```

Include full IPC/Gateway JSON for diagnostics:

```powershell
.\build\DiscordRPC.exe --once --verbose
```

## Preserved Python Features

- Reads the existing `config.ini` format.
- Creates missing `config.ini` entries on startup while preserving existing user values.
- Connects through local Discord IPC by default.
- Supports optional Gateway fallback and Auto mode.
- Shows the foreground window title in `details` when enabled.
- Applies `full_replace`, `word_replace`, and regex `pattern_replace` censor rules.
- Shows CPU and RAM usage in `state` when enabled.
- Uses system uptime as the activity start timestamp.
- Selects large/small assets from configurable morning, afternoon, evening, night, and default ranges.
- Supports `<time>`, `<percentage>`, `<plug>`, and `<remaining>` placeholders.
- Supports AFK image/text with `<hours>`, `<minutes>`, and `<seconds>`.
- Supports up to two Discord activity buttons.
- Reconnects when Discord IPC is unavailable or restarts.

## Intentional Fixes And Additions

- Night time ranges can wrap across midnight, for example `22` to `5`.
- AFK detection uses the Windows last-input timer.
- Discord text fields are length-limited before sending.
- INI reads/writes preserve common text encodings and insert new keys into the correct section.
- No Python packages or Discord RPC wrapper are required.
- Tray UI includes dropdown or flat categories, disabled current-value rows, checked toggles, edit dialogs, reload, console visibility, granular notifications, recent logs, log-folder access, and exit controls.
- Verbose logs redact foreground-window, username, computer, process ID, and executable-path token values by default.

## Configuration Notes

- On first launch, setup asks for Discord Application ID, preferred transport, and dynamic/static presence mode.
- `[general] transport_mode` accepts `ipc`, `gateway`, or `auto`.
- `[general] client_id` must be a valid Discord application ID.
- `[general] token_env` can point to an environment variable containing the Discord token. `[general] token = env:VARIABLE_NAME` is also accepted.
- Plaintext token values in `config.ini` are ignored and cleared by the setup dialog.
- `[app] show_menu_as_dropdown` switches between dropdown category menus and a flat sectioned menu.
- `[app] single_instance` prevents two persistent instances from fighting over the same Discord presence.
- `[app] file_logging_enabled` and `[app] log_path` control runtime logs.
- `[app] backup_config_on_save` is off by default to avoid duplicating plaintext Gateway tokens into `.bak` files.
- `[ipc] connect_timeout_ms` and `response_timeout_ms` tune Discord IPC waits.

Most tray/menu/dialog labels are configurable in the `[strings]` section, including `ok`, `cancel`, `current_value_format`, `change_menu_format`, and category names.

If Discord reports `Invalid Client ID`, update `[general] client_id` in `config.ini` with an existing application ID.

## Generated Files

- `build\DiscordRPC.exe`
- `config.ini` next to the selected config path/current directory
- optional log file controlled by `[app] log_path`

Generated binaries, runtime configs, and logs should not be committed.

## Release

Prebuilt binary: [DiscordRPC v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/DiscordRPC-v1).
