# Discord tile factory defaults, 23 October 2026

The original standalone DiscordRPC configuration and its current shared engine
agree on these defaults:

```ini
[general]
activity_name=a game
details_template={win_title}
state_template=CPU: {cpu}, RAM: {ram_used}/{ram_total} ({ram_pct})
```

This was verified against the current
`dependencies/DiscordRPC/drpc_config_defaults.inc` and the earlier standalone
`DiscordRPC/src/drpc_config_defaults.inc` at commit `4ecd15e`. The field order is
**Details = foreground title, State = CPU/RAM**, rather than the reverse recalled
in the report. Both templates remain editable; users can swap their contents.

DesktopStub's abbreviated CPU-only State default and DesktopStub activity-name
default now use the same shared constants as standalone DiscordRPC. The model,
legacy fresh-profile defaults, tray editor fallbacks and newly created Discord
instances agree. New instances store their factory templates at creation.
Existing explicit per-tile values, global values and imported advanced profiles
are not upgraded or replaced by this change.

The tile uses the actual in-process presence builder in preview mode. With no
context override, that engine reads the current foreground window and machine
CPU/RAM counters. CPU percentage, RAM used/total and percentage are real context
tokens; the defaults do not contain sample numbers or a DesktopStub setup
message. `SendPresence=0` remains the default, so preview does not create a
Discord transport or require a client ID/token. Sending is a separate explicit
configuration choice.

The regression uses synthetic context rather than reading the user's current
window or running Discord. It verifies that the real builder produces the
expected foreground Details and `CPU: 5%, RAM: 0.8GB/16GB (5%)` State, standalone
editor defaults match tile defaults, explicit per-tile templates survive, and
preview constructs no transport. The existing service lifecycle, cancellation,
fake-send and concurrency tests run in the same fixture.

The final isolated fixture passed **149 checks**, 40,000 concurrent snapshot
reads and 80,000 cancellation-relay reads under `/W4 /WX /O2`; its executable
scanned clean before execution. Compile with `tools/BuildDiscordFactoryTests.cmd`.
The result log is `build/review-validation/discord-factory-defaults-tests-oct23.log`.
