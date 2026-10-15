# Nifibot Logging v0.2

Major changes:
- 0-day retention means forever.
- All guild messages are cached, including bot/webhook messages.
- Embed JSON, stickers, attachment metadata and reply target are cached.
- Deleted messages remain in the DB with `deleted_at` instead of being removed.
- Persistent structured event history in `event_log`.
- Reactions add/remove.
- Bulk delete logs.
- Member join/leave.
- Nickname changes.
- Role changes.
- Best-effort audit-log actor attribution.
- Manual compressed month archives (`.jsonl.gz`).
- Optional archive upload to Discord.
- Archive directory is suitable for Syncthing.

Required Discord privileged intent:
- Message Content Intent: ON
- Server Members Intent: ON
- Presence Intent: not required

For existing installs:
1. Extract over project root.
2. Set `MESSAGE_CACHE_RETENTION_DAYS=0` in `.env`.
3. Run:
   npm run build
   npm run db:migrate
   npm run commands:deploy
4. Start the bot.

Recommended configuration after command redeploy:
- `/logging channel kind:messages channel:#nifibot-logs`
- `/logging channel kind:members channel:#server-logs`
- `/logging channel kind:archive channel:#nifibot-archives`
- `/logging retention days:0`
- `/logging status`

Create an archive:
- `/logging archive month:2026-10 upload:false`
- `/logging archive month:2026-10 upload:true`

Archive files:
`archives/<guild-id>/<YYYY-MM>.jsonl.gz`
