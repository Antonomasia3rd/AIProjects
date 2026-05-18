# DiscordRPC C# build

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
- Adds a tray-first configuration menu with dropdown categories, disabled current-value rows, checked toggles, small edit dialogs, reload, console visibility, granular notifications, recent logs, and exit controls.

## Intentional fixes

- The night time range can wrap across midnight, for example `22` to `5`.
- AFK detection uses the Windows last-input timer instead of treating an unchanged foreground title as idle.
- Discord text fields are length-limited before being sent to reduce rejected updates.
- INI reads and writes preserve common text encodings and insert newly created keys into the correct section.
- No Python packages or Discord RPC wrapper are required.

## Build and run

```powershell
.\build.ps1
.\DiscordRPC.exe
```

Normal launches start the tray icon unless `[app] show_tray = false` is set. Right-click the tray icon to change most settings directly. The root menu is grouped into dropdown categories so it should not overflow the screen. A small edit dialog opens only for values that need typing, such as IDs, URLs, asset names, or replacement maps.

On first launch, the setup screen asks for the Discord Application ID, preferred transport, and whether the presence should use active-window/CPU data or static text. The Application ID is required for IPC, Auto, and Gateway because Discord uses it to identify the Rich Presence application and its uploaded assets.

Most tray/menu/dialog labels are configurable in the `[strings]` section of `config.ini`, including `ok`, `cancel`, `current_value_format`, `change_menu_format`, and the category names. Set `[app] hide_disabled_entries = true` if you want disabled dependent actions hidden instead of greyed out.

Use `[general] transport_mode = ipc`, `gateway`, or `auto` to choose how presence is sent. IPC is the standard/default path and requires the Discord desktop app. Gateway is the fallback/surprise path and requires a Discord token. Auto tries IPC first, then Gateway if IPC is unavailable. The same setting is available from the tray menu under General -> Transport mode.

The executable expects `config.ini` in the current directory. You can also pass a config path:

```powershell
.\DiscordRPC.exe .\config.ini
```

Validate config parsing and generated activity JSON without connecting to Discord:

```powershell
.\DiscordRPC.exe --dry-run
```

Run one live Discord update, keep it visible briefly for diagnostics, and exit. For a persistent Rich Presence, run `.\DiscordRPC.exe` normally so the process stays alive:

```powershell
.\DiscordRPC.exe --once
```

Include full IPC/Gateway JSON for debugging:

```powershell
.\DiscordRPC.exe --once --verbose
```

Run without the tray icon:

```powershell
.\DiscordRPC.exe --no-tray
```

If Discord reports `Invalid Client ID`, update `[general] client_id` in `config.ini` with an existing application ID from the Discord Developer Portal.
