# Nifibot Logging v0.3.4 — Ignore Users/Bots

Adds persistent per-server ignore controls for noisy message/reaction sources.

Commands:

- `/logging ignore-user user:@Lofi Radio`
- `/logging unignore-user user:@Lofi Radio`
- `/logging status` shows the ignored list.

Semantics:

- ignored users/bots are still cached in `message_cache`
- history scans/imports still preserve their messages
- edit logs authored by them are suppressed
- delete logs authored by them are suppressed
- bulk-delete previews omit their messages
- reactions from them are suppressed
- reactions on their messages are suppressed

This intentionally affects only the Messages + reactions logging category.
Member/moderation/voice events remain independent.

Apply over v0.3.3:

```cmd
npm run build
npm run db:migrate
npm run commands:deploy
npm start
```
