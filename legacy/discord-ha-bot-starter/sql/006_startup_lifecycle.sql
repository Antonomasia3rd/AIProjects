ALTER TABLE guild_settings
  ADD COLUMN IF NOT EXISTS startup_voice_channel_id TEXT;

ALTER TABLE guild_settings
  ADD COLUMN IF NOT EXISTS startup_log_enabled BOOLEAN NOT NULL DEFAULT TRUE;
