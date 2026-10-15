CREATE TABLE IF NOT EXISTS schema_migrations (
    filename text PRIMARY KEY,
    applied_at timestamptz NOT NULL DEFAULT clock_timestamp()
);

CREATE TABLE IF NOT EXISTS cluster_leases (
    lease_name text PRIMARY KEY,
    holder_id text NOT NULL,
    lease_until timestamptz NOT NULL,
    epoch bigint NOT NULL DEFAULT 1,
    updated_at timestamptz NOT NULL DEFAULT clock_timestamp()
);

CREATE TABLE IF NOT EXISTS guild_settings (
    guild_id text PRIMARY KEY,
    logging_enabled boolean NOT NULL DEFAULT false,
    log_channel_id text,
    log_message_edits boolean NOT NULL DEFAULT true,
    log_message_deletes boolean NOT NULL DEFAULT true,
    updated_at timestamptz NOT NULL DEFAULT clock_timestamp()
);

CREATE TABLE IF NOT EXISTS message_cache (
    message_id text PRIMARY KEY,
    guild_id text NOT NULL,
    channel_id text NOT NULL,
    author_id text NOT NULL,
    author_name text,
    content text,
    attachments jsonb NOT NULL DEFAULT '[]'::jsonb,
    message_created_at timestamptz NOT NULL,
    message_updated_at timestamptz,
    expires_at timestamptz NOT NULL,
    cached_at timestamptz NOT NULL DEFAULT clock_timestamp()
);

CREATE INDEX IF NOT EXISTS idx_message_cache_expires
    ON message_cache (expires_at);

CREATE INDEX IF NOT EXISTS idx_message_cache_guild_channel
    ON message_cache (guild_id, channel_id);
