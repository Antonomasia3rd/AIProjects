# Nifibot v0.5.1 — Cluster Update + Logging Quality

Built on the v0.5.0 reconciliation/audit baseline.

## Changes

### `/computer target:` autocomplete
`/computer` now queries `computer_nodes` live and autocompletes hostname/NODE_ID,
including online/offline state and current Nifibot version.

### `/cluster-update`
One-time bootstrap each machine manually, then future ZIP updates can be rolled
out from Discord:

```
/cluster-update package:<zip> reason:<reason> [target:<node>]
```

Leave `target` blank to update every registered node. Offline nodes keep a pending update command for up to 30 days and apply it when their agent comes back online. The release ZIP is downloaded
immediately by the Discord leader and stored temporarily in Supabase Postgres so
other nodes do not need access to the original Discord attachment URL.

Each target:
1. verifies SHA-256;
2. rejects unsafe ZIP paths and any `.env` file;
3. copies the current source tree into a staging directory;
4. overlays the ZIP, so both full releases and normal overlay patches work;
5. reuses existing `node_modules` when `package-lock.json` is unchanged, or runs
   `npm ci` if dependencies changed;
6. runs `npm run build`;
7. runs database migrations and guild command deployment;
8. completes the remote command;
9. waits for its rolling restart slot;
10. shuts down gracefully, applies staged files while preserving `.env`, local
    logs/history/archives, then starts Nifibot again.

Standby/offline nodes are ordered before current Discord/voice owners. Live restarts are staggered by 45 seconds.

The database retains only the 10 newest update ZIPs.

### Migration serialization
`npm run db:migrate` now uses a PostgreSQL advisory lock. Multiple cluster nodes
may stage the same release simultaneously without racing `schema_migrations`.

### Bot no-op edit suppression
Bot/webhook `messageUpdate` events now receive an additional semantic comparison
that ignores bookkeeping-only embed changes. This is intended to suppress cases
such as Dyno refreshing an otherwise identical log embed while preserving real
text/field/image/link changes.

### Voice join snapshots
A fresh VC join always logs the complete state snapshot, even where the value did
not change:

- server mute
- server deaf
- self mute
- self deaf
- streaming
- camera

So a join log can contain `off → off` just like Nifibot's own voice join did.

### Version reporting
Computer heartbeats and `/status` now include the running Nifibot version.

## First install of v0.5.1

This release itself still has to be installed normally, because older builds do
not contain the self-updater yet.

```
npm run build
npm run db:migrate
npm run commands:deploy
npm start
```

Expected new migration:

```
008_cluster_self_update.sql
```

After every node has v0.5.1, use `/cluster-update` for future ZIPs.
