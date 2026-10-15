# Nifibot v0.5.0 — reconciliation + broad audit logging

This release is based on the consolidated v0.4.2 project snapshot.

## Main changes

### Startup recovery beyond message history

After the normal message catch-up, the active Discord gateway node now:

1. replays Discord Audit Log entries that arrived while every gateway node was offline;
2. emits recovered audit events into the configured logging destination;
3. stores a current-state snapshot (members/channels/roles, current VC occupants,
   visible embedded Activities, and active timeouts visible in cache);
4. posts a startup reconciliation summary.

The first v0.5 startup creates a baseline at the newest Discord Audit Log entry so
installing the release does not flood the server with old administrative history.
Subsequent downtime is replayed automatically.

Transient events that both start and finish while all nodes are offline cannot be
reconstructed from Discord (for example join->leave VC, camera on->off, or an
Activity that starts and ends entirely during the outage). The current state is
snapshotted instead.

### Broader live server audit logging

The real-time Discord Audit Log stream now produces generic rich logs for server
configuration/admin events that were previously missing, including channel and
permission-overwrite changes, roles, invites, webhooks, emojis, stickers,
integrations, threads, scheduled events, stage instances, soundboard sounds,
AutoMod rules/actions, onboarding/server-guide changes, application-command
permission changes, and bot additions.

Existing richer loggers (timeouts, bans, kicks, member roles/nicknames, voice,
message content) remain in place and are not replaced by generic duplicate cards.

### Durable audit deduplication

`processed_audit_entries` prevents the same Discord Audit Log entry from being
replayed after a failover/restart. `guild_reconcile_state` stores the audit cursor
and latest state snapshot.

### Better HA status

`/status` now shows the active gateway lease, independent voice lease, all known
Nifibot host heartbeats, and the last reconciliation time in addition to local
runtime/memory information.

### `Everything` is now the canonical future default

`/logging channel kind:everything` now stores the channel in `log_channel_id` and
clears category-specific overrides. Members/voice/moderation/archive only store a
channel when explicitly overridden. New logging categories therefore inherit the
Everything destination automatically.

Migration 007 automatically normalizes old v0.4 configurations where every
category was set to the same channel.

## Upgrade

Stop Nifibot, overlay the release, then run:

```cmd
npm run build
npm run db:migrate
npm run commands:deploy
npm start
```

Expected new migration:

```text
007_reconciliation_and_default_logging.sql  Migration applied
```

No new npm dependencies are introduced in v0.5.
