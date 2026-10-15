import { db } from "./db.js";
import { config } from "./config.js";
import { withDbRetry } from "./dbRetry.js";

export type LogChannelKind =
  | "everything"
  | "messages"
  | "members"
  | "voice"
  | "moderation"
  | "archive";

export type GuildSettings = {
  guild_id: string;
  logging_enabled: boolean;
  log_channel_id: string | null;
  member_log_channel_id: string | null;
  voice_log_channel_id: string | null;
  moderation_log_channel_id: string | null;
  archive_channel_id: string | null;
  startup_voice_channel_id: string | null;
  startup_log_enabled: boolean;
  log_message_edits: boolean;
  log_message_deletes: boolean;
  message_retention_days: number;
  ignored_message_user_ids: string[];
};

const defaultSettings = (guildId: string): GuildSettings => ({
  guild_id: guildId,
  logging_enabled: false,
  log_channel_id: null,
  member_log_channel_id: null,
  voice_log_channel_id: null,
  moderation_log_channel_id: null,
  archive_channel_id: null,
  startup_voice_channel_id: null,
  startup_log_enabled: true,
  log_message_edits: true,
  log_message_deletes: true,
  message_retention_days: config.MESSAGE_CACHE_RETENTION_DAYS,
  ignored_message_user_ids: []
});

export async function getGuildSettings(guildId: string): Promise<GuildSettings> {
  return withDbRetry("getGuildSettings", async () => {
    const result = await db.query<GuildSettings>(
      `
      SELECT guild_id, logging_enabled, log_channel_id,
             member_log_channel_id, voice_log_channel_id,
             moderation_log_channel_id, archive_channel_id,
             startup_voice_channel_id,
             COALESCE(startup_log_enabled, true) AS startup_log_enabled,
             log_message_edits, log_message_deletes,
             message_retention_days,
             COALESCE(ignored_message_user_ids, ARRAY[]::text[]) AS ignored_message_user_ids
      FROM guild_settings
      WHERE guild_id = $1
      `,
      [guildId]
    );

    return result.rows[0] ?? defaultSettings(guildId);
  });
}

export async function setLoggingChannel(
  guildId: string,
  kind: LogChannelKind,
  channelId: string
): Promise<void> {
  if (kind === "everything") {
    await setAllLoggingChannels(guildId, channelId);
    return;
  }

  const column =
    kind === "messages"
      ? "log_channel_id"
      : kind === "members"
        ? "member_log_channel_id"
        : kind === "voice"
          ? "voice_log_channel_id"
          : kind === "moderation"
            ? "moderation_log_channel_id"
            : "archive_channel_id";

  await db.query(
    `
    INSERT INTO guild_settings (guild_id, ${column})
    VALUES ($1, $2)
    ON CONFLICT (guild_id) DO UPDATE
    SET ${column} = EXCLUDED.${column},
        updated_at = clock_timestamp()
    `,
    [guildId, channelId]
  );
}

export async function setAllLoggingChannels(
  guildId: string,
  channelId: string
): Promise<void> {
  // `log_channel_id` is the canonical default. Category-specific columns are
  // overrides only. Clearing them here means categories added in future builds
  // automatically inherit the Everything destination without a migration.
  await db.query(
    `
    INSERT INTO guild_settings (
      guild_id,
      log_channel_id,
      member_log_channel_id,
      voice_log_channel_id,
      moderation_log_channel_id,
      archive_channel_id
    )
    VALUES ($1, $2, NULL, NULL, NULL, NULL)
    ON CONFLICT (guild_id) DO UPDATE
    SET log_channel_id = EXCLUDED.log_channel_id,
        member_log_channel_id = NULL,
        voice_log_channel_id = NULL,
        moderation_log_channel_id = NULL,
        archive_channel_id = NULL,
        updated_at = clock_timestamp()
    `,
    [guildId, channelId]
  );
}

export async function setStartupVoiceChannel(
  guildId: string,
  channelId: string | null
): Promise<void> {
  await db.query(
    `
    INSERT INTO guild_settings (guild_id, startup_voice_channel_id)
    VALUES ($1, $2)
    ON CONFLICT (guild_id) DO UPDATE
    SET startup_voice_channel_id = EXCLUDED.startup_voice_channel_id,
        updated_at = clock_timestamp()
    `,
    [guildId, channelId]
  );
}

export async function setStartupLogEnabled(
  guildId: string,
  enabled: boolean
): Promise<void> {
  await db.query(
    `
    INSERT INTO guild_settings (guild_id, startup_log_enabled)
    VALUES ($1, $2)
    ON CONFLICT (guild_id) DO UPDATE
    SET startup_log_enabled = EXCLUDED.startup_log_enabled,
        updated_at = clock_timestamp()
    `,
    [guildId, enabled]
  );
}

export async function setLoggingEnabled(
  guildId: string,
  enabled: boolean
): Promise<void> {
  await db.query(
    `
    INSERT INTO guild_settings (guild_id, logging_enabled)
    VALUES ($1, $2)
    ON CONFLICT (guild_id) DO UPDATE
    SET logging_enabled = EXCLUDED.logging_enabled,
        updated_at = clock_timestamp()
    `,
    [guildId, enabled]
  );
}

export async function setMessageRetentionDays(
  guildId: string,
  days: number
): Promise<void> {
  await db.query(
    `
    INSERT INTO guild_settings (guild_id, message_retention_days)
    VALUES ($1, $2)
    ON CONFLICT (guild_id) DO UPDATE
    SET message_retention_days = EXCLUDED.message_retention_days,
        updated_at = clock_timestamp()
    `,
    [guildId, days]
  );

  if (days === 0) {
    await db.query(
      `UPDATE message_cache SET expires_at = NULL WHERE guild_id = $1`,
      [guildId]
    );
  } else {
    await db.query(
      `
      UPDATE message_cache
      SET expires_at = message_created_at + ($2::int * interval '1 day')
      WHERE guild_id = $1
      `,
      [guildId, days]
    );
  }
}


export async function addIgnoredMessageUser(
  guildId: string,
  userId: string
): Promise<void> {
  await db.query(
    `
    INSERT INTO guild_settings (guild_id, ignored_message_user_ids)
    VALUES ($1, ARRAY[$2]::text[])
    ON CONFLICT (guild_id) DO UPDATE
    SET ignored_message_user_ids = CASE
          WHEN $2 = ANY(COALESCE(guild_settings.ignored_message_user_ids, ARRAY[]::text[]))
            THEN COALESCE(guild_settings.ignored_message_user_ids, ARRAY[]::text[])
          ELSE array_append(
            COALESCE(guild_settings.ignored_message_user_ids, ARRAY[]::text[]),
            $2
          )
        END,
        updated_at = clock_timestamp()
    `,
    [guildId, userId]
  );
}

export async function removeIgnoredMessageUser(
  guildId: string,
  userId: string
): Promise<void> {
  await db.query(
    `
    INSERT INTO guild_settings (guild_id, ignored_message_user_ids)
    VALUES ($1, ARRAY[]::text[])
    ON CONFLICT (guild_id) DO UPDATE
    SET ignored_message_user_ids = array_remove(
          COALESCE(guild_settings.ignored_message_user_ids, ARRAY[]::text[]),
          $2
        ),
        updated_at = clock_timestamp()
    `,
    [guildId, userId]
  );
}

export function isIgnoredMessageUser(
  settings: GuildSettings,
  userId: string | null | undefined
): boolean {
  return Boolean(
    userId && settings.ignored_message_user_ids.includes(userId)
  );
}
