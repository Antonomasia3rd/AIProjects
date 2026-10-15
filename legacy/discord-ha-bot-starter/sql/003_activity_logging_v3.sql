ALTER TABLE guild_settings
  ADD COLUMN IF NOT EXISTS voice_log_channel_id text,
  ADD COLUMN IF NOT EXISTS moderation_log_channel_id text;
