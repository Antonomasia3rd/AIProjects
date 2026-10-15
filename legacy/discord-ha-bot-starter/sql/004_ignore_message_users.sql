ALTER TABLE guild_settings
  ADD COLUMN IF NOT EXISTS ignored_message_user_ids text[] NOT NULL DEFAULT ARRAY[]::text[];

UPDATE guild_settings
SET ignored_message_user_ids = ARRAY[]::text[]
WHERE ignored_message_user_ids IS NULL;
