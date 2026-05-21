# DiscordRPC

C# Discord Rich Presence utility with a tray-first configuration UI, Discord IPC mode, optional Gateway mode, and support for dynamic system/window placeholders.

This is a C# replacement for the experimental Python `DiscordRPC.py`.

## Existing Python features preserved

- Reads the existing `config.ini` format.
- Creates missing `config.ini` entries on startup while preserving existing user values.
- Connects to Discord Rich Presence through local Discord IPC by default.
- Supports optional Gateway fallback mode, so Discord does not need to be running locally when Gateway is selected.
- Supports auto mode that tries IPC first and falls back to Gateway.
- Shows the foreground window title in `details` when enabled.
- Applies `full_replace`, `word_replace`, and regex `pattern_replace` censor rules.
- Shows CPU and RAM usage in `state` when enabled.
- Uses system uptime as the activity start timestamp.
- Selects large and small assets from configurable morning, afternoon, evening, night, and default ranges.
- Supports `<time>`, `<percentage>`, `<plug>`, and `<remaining>` placeholders.
- Supports AFK image/text with `<hours>`, `<minutes>`, and `<seconds>`.
- Supports up to two Discord activity buttons. Discord only shows buttons to other users, not to the account that owns the activity.
- Reconnects when Discord IPC is unavailable or restarts.
- Adds a tray-first configuration menu with dropdown or flat GenerateAssets-style categories, disabled current-value rows, checked toggles, small edit dialogs, reload, console visibility, granular notifications, recent logs, log-folder access, and exit controls.

## Intentional fixes

- The night time range can wrap across midnight, for example `22` to `5`.
- AFK detection uses the Windows last-input timer instead of treating an unchanged foreground title as idle.
- Discord text fields are length-limited before being sent to reduce rejected updates.
- INI reads and writes preserve common text encodings and insert newly created keys into the correct section.
- No Python packages or Discord RPC wrapper are required.

## Build and run

```powershell
.\build.ps1
.\build\DiscordRPC.exe
```

Normal launches start the tray icon unless `[app] show_tray = false` is set. Right-click the tray icon to change most settings directly. The root menu is grouped into dropdown categories so it should not overflow the screen. A small edit dialog opens only for values that need typing, such as IDs, URLs, asset names, or replacement maps.

On first launch, the setup screen asks for the Discord Application ID, preferred transport, and whether the presence should use active-window/CPU data or static text. The Application ID is required for IPC, Auto, and Gateway because Discord uses it to identify the Rich Presence application and its uploaded assets.

Most tray/menu/dialog labels are configurable in the `[strings]` section of `config.ini`, including `ok`, `cancel`, `current_value_format`, `change_menu_format`, and the category names. Set `[app] hide_disabled_entries = true` if you want disabled dependent actions hidden instead of greyed out.

Use `[general] transport_mode = ipc`, `gateway`, or `auto` to choose how presence is sent. IPC is the standard/default path and requires the Discord desktop app. Gateway is the fallback/surprise path and requires a Discord token. Auto tries IPC first, then Gateway if IPC is unavailable. The same setting is available from the tray menu under General -> Transport mode.

The executable expects `config.ini` in the current directory. You can also pass a config path:

```powershell
.\build\DiscordRPC.exe .\config.ini
```

Validate config parsing and generated activity JSON without connecting to Discord:

```powershell
.\build\DiscordRPC.exe --dry-run
```

By default, dry-run output redacts sensitive local tokens such as foreground window titles, usernames, computer names, process IDs, and executable paths. Use the full mode only when you are keeping the output private:

```powershell
.\build\DiscordRPC.exe --dry-run-full
```

Run one live Discord update, keep it visible briefly for diagnostics, and exit. For a persistent Rich Presence, run `.\build\DiscordRPC.exe` normally so the process stays alive:

```powershell
.\build\DiscordRPC.exe --once
```

Include full IPC/Gateway JSON for debugging:

```powershell
.\build\DiscordRPC.exe --once --verbose
```

Run without the tray icon:

```powershell
.\build\DiscordRPC.exe --no-tray
```

## Configuration notes

- INI keys and values written by the app are quoted, for example `"show_menu_as_dropdown" = "true"`. This keeps `;` and `#` usable as literal text inside values while still allowing inline comments outside quotes.
- `[app] show_menu_as_dropdown` switches between dropdown category menus and a flat sectioned menu similar to GenerateAssets.
- `[app] single_instance` prevents two persistent instances from fighting over the same Discord presence.
- `[app] file_logging_enabled` and `[app] log_path` control the runtime log file. The default log path is next to `config.ini`.
- `[app] verbose_redact_sensitive` is on by default so verbose presence JSON logs redact foreground-window, username, computer, process ID, and executable-path token values. Set it to `false` only for private debugging.
- `[app] backup_config_on_save` is off by default to avoid duplicating plaintext Gateway tokens into `.bak` files.
- `[general] token_env` can point to an environment variable containing the Discord token, avoiding plaintext token storage in `config.ini`.
- `[ipc] connect_timeout_ms` and `response_timeout_ms` tune Discord IPC waits.
- `[gateway]` contains the Discord Gateway client identity and timeout values. Gateway mode is still the unofficial/user-token path; IPC remains the recommended default.

If Discord reports `Invalid Client ID`, update `[general] client_id` in `config.ini` with an existing application ID from the Discord Developer Portal.

## Generated Files

`build\DiscordRPC.exe` and runtime config/log files are generated output and should not be committed.

## Release

Prebuilt binary: [DiscordRPC v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/DiscordRPC-v1).
