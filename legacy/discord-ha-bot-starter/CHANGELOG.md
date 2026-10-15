## 0.5.3

- Add updater-aware local watchdog with crash restart/backoff.
- Make `npm start` supervised; add `npm run start:direct`.
- Make Windows Startup-folder launchers start the watchdog.
- Make self-update restart through the watchdog and wait for the old watchdog to exit.
- Preserve v0.5.2-hotfix.2 voice PostgreSQL idle-pool resilience.

# Changelog

## 0.5.0 — 2026-10-13

- Added Discord Audit Log startup replay after downtime.
- Added current-state reconciliation snapshots at gateway startup.
- Added broad generic logging for previously uncovered server/admin audit actions.
- Added persistent audit-entry deduplication and reconciliation cursor tables.
- Expanded `/status` with gateway/voice lease ownership and node heartbeats.
- Changed `Everything` logging to a canonical default with per-category overrides.
- Fixed history/startup backfill recursion boundaries to include voice/moderation outputs.
- Updated the startup-voice command wording to match independent Voice HA semantics.
- Bumped package version to 0.5.0.

## 0.5.1

- Added live autocomplete for `/computer target:`.
- Added `/cluster-update` with Supabase-backed ZIP transport and rolling node restarts.
- Added SHA-256 and ZIP path safety checks; `.env` is never accepted from an update archive.
- Added staged compile/migration/command validation before live replacement.
- Serialized migrations with a PostgreSQL advisory lock for concurrent cluster staging.
- Added node version reporting to heartbeats and `/status`.
- Suppressed bot/webhook embed bookkeeping-only `messageUpdate` noise.
- Voice joins now log a full state snapshot including unchanged flags.
