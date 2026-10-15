# Nifibot v0.4 — unified logging destination + distributed computer control

## Unified logging destination

`/logging channel kind:everything channel:#nifibot・logs`

sets the current message/member/voice/moderation/archive destinations at once.
`log_channel_id` remains the canonical fallback, so future logging categories should
also fall back there unless explicitly routed elsewhere.

## Computer control

Every Nifibot process now runs a small DB-backed computer agent, including standby
HA nodes. The Discord leader queues a command in Supabase; the selected physical
host claims and executes it. This means laptop B can be controlled even while laptop
A is the active Discord gateway leader.

Commands:

- `/computer action:list`
- `/computer action:status target:A1504VA`
- `/computer action:sleep target:A1504VA reason:kuliah dulu`
- `/computer action:lock target:A1504VA reason:tinggal bentar`
- `/computer action:settings target:A1504VA key:power_mode value:better_battery applies_to:DC reason:laptopnya lagi dicabut`

Computer-changing actions require a reason and are stored in `computer_commands`.
They are also copied to the configured moderation/message log channel.

### Authorization

The slash command defaults to Discord Administrator permission.
Optionally set a second gate in `.env`:

`COMPUTER_CONTROL_USER_IDS=123456789012345678,234567890123456789`

If the variable is blank, Discord Administrator permission is the gate.

### Windows compatibility

Sleep, lock, and status are Windows-compatible on the older hosts too.
Persistent AC/DC power-mode selection in this first version uses Microsoft's
Windows 11 `PowerSetUserConfiguredACPowerMode` / `PowerSetUserConfiguredDCPowerMode`
APIs. Windows 10 hosts will report that this setting adapter is unsupported rather
than writing undocumented registry values.

## Apply

Overlay on top of v0.3.4, then:

```cmd
npm run build
npm run db:migrate
npm run commands:deploy
npm start
```

No existing `.env` is overwritten. The new env settings all have defaults.
