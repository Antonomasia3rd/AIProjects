-- v0.5: durable audit catch-up/reconciliation state + canonical "Everything" routing.

CREATE TABLE IF NOT EXISTS processed_audit_entries (
  guild_id text NOT NULL,
  audit_entry_id text NOT NULL,
  action_type integer NOT NULL,
  executor_id text,
  target_id text,
  occurred_at timestamptz NOT NULL,
  processed_at timestamptz NOT NULL DEFAULT clock_timestamp(),
  source text NOT NULL DEFAULT 'live',
  PRIMARY KEY (guild_id, audit_entry_id)
);

CREATE INDEX IF NOT EXISTS processed_audit_entries_guild_time_idx
  ON processed_audit_entries (guild_id, occurred_at DESC);

CREATE TABLE IF NOT EXISTS guild_reconcile_state (
  guild_id text PRIMARY KEY,
  last_audit_entry_id text,
  last_reconciled_at timestamptz,
  snapshot jsonb NOT NULL DEFAULT '{}'::jsonb,
  updated_at timestamptz NOT NULL DEFAULT clock_timestamp()
);

-- v0.4 wrote the same channel into every override when "Everything" was used.
-- v0.5 makes log_channel_id the canonical default and keeps the other columns
-- only as explicit overrides. This makes future categories inherit Everything.
UPDATE guild_settings
SET member_log_channel_id = NULL,
    voice_log_channel_id = NULL,
    moderation_log_channel_id = NULL,
    archive_channel_id = NULL,
    updated_at = clock_timestamp()
WHERE log_channel_id IS NOT NULL
  AND member_log_channel_id = log_channel_id
  AND voice_log_channel_id = log_channel_id
  AND moderation_log_channel_id = log_channel_id
  AND archive_channel_id = log_channel_id;
