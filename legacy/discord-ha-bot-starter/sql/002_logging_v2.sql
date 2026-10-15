ALTER TABLE guild_settings
  ADD COLUMN IF NOT EXISTS member_log_channel_id text,
  ADD COLUMN IF NOT EXISTS archive_channel_id text,
  ADD COLUMN IF NOT EXISTS message_retention_days integer NOT NULL DEFAULT 0;

ALTER TABLE message_cache
  ALTER COLUMN expires_at DROP NOT NULL;

ALTER TABLE message_cache
  ADD COLUMN IF NOT EXISTS author_is_bot boolean NOT NULL DEFAULT false,
  ADD COLUMN IF NOT EXISTS webhook_id text,
  ADD COLUMN IF NOT EXISTS embeds jsonb NOT NULL DEFAULT '[]'::jsonb,
  ADD COLUMN IF NOT EXISTS stickers jsonb NOT NULL DEFAULT '[]'::jsonb,
  ADD COLUMN IF NOT EXISTS reply_to_message_id text,
  ADD COLUMN IF NOT EXISTS deleted_at timestamptz;

-- User requested forever retention. Preserve existing cached test rows indefinitely.
UPDATE guild_settings
SET message_retention_days = 0;

UPDATE message_cache
SET expires_at = NULL;

CREATE TABLE IF NOT EXISTS event_log (
    id bigserial PRIMARY KEY,
    guild_id text NOT NULL,
    event_type text NOT NULL,
    actor_id text,
    target_id text,
    channel_id text,
    message_id text,
    occurred_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    data jsonb NOT NULL DEFAULT '{}'::jsonb
);

CREATE INDEX IF NOT EXISTS idx_event_log_guild_time
    ON event_log (guild_id, occurred_at);

CREATE INDEX IF NOT EXISTS idx_event_log_type_time
    ON event_log (event_type, occurred_at);

CREATE INDEX IF NOT EXISTS idx_message_cache_created
    ON message_cache (guild_id, message_created_at);

CREATE INDEX IF NOT EXISTS idx_message_cache_deleted
    ON message_cache (guild_id, deleted_at)
    WHERE deleted_at IS NOT NULL;
