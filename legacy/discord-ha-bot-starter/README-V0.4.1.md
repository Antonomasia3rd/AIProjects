# Nifibot v0.4.1 — resilient DB + startup lifecycle

This package supersedes the earlier v0.4 package. If you have **not** installed v0.4 yet, apply this package directly on top of your current v0.3.4/v0.3.3 install.

Includes everything from v0.4:
- `/logging channel kind:everything`
- distributed `/computer` control worker/queue
- migration `005_computer_control.sql`

Plus v0.4.1:
- transient Supabase retry for live message cache/settings reads
- TCP keepalive on PostgreSQL pool connections
- migration `006_startup_lifecycle.sql`
- `/logging startup-voice enabled:true channel:#...`
- `/logging startup-log enabled:true|false`
- active HA leader auto-joins the configured VC
- leader-online startup/failover message in the normal log channel
- startup voice failures do **not** crash Discord; they are reported in the startup log

## Required extra package

Install the official discord.js voice library locally in this project:

```cmd
npm install @discordjs/voice@0.19.2
```

No FFmpeg is needed just to maintain an idle voice connection.

## Apply

```cmd
npm install @discordjs/voice@0.19.2
npm run build
npm run db:migrate
npm run commands:deploy
npm start
```

Expected new migrations on an install currently at migration 004:
- `005_computer_control.sql`
- `006_startup_lifecycle.sql`

Then configure, for example:

```text
/logging channel kind:everything channel:#nifibot﹒logs
/logging startup-log enabled:true
/logging startup-voice enabled:true channel:#AFK
```

Only the active Discord leader joins the VC. Standby HA agents remain disconnected from Discord/voice until they acquire leadership.
