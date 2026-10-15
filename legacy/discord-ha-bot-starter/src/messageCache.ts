import type { Message } from "discord.js";
import { db } from "./db.js";
import { withDbRetry } from "./dbRetry.js";

export type SerializedMessageRecord = {
  message_id: string;
  guild_id: string;
  channel_id: string;
  author_id: string;
  author_name: string | null;
  author_is_bot: boolean;
  webhook_id: string | null;
  content: string | null;
  attachments: Array<Record<string, unknown>>;
  embeds: Array<Record<string, unknown>>;
  stickers: Array<Record<string, unknown>>;
  reply_to_message_id: string | null;
  message_created_at: string;
  message_updated_at: string | null;
};

export type CachedMessage = {
  message_id: string;
  guild_id: string;
  channel_id: string;
  author_id: string;
  author_name: string | null;
  author_is_bot: boolean;
  webhook_id: string | null;
  content: string | null;
  attachments: Array<Record<string, unknown>>;
  embeds: Array<Record<string, unknown>>;
  stickers: Array<Record<string, unknown>>;
  reply_to_message_id: string | null;
  message_created_at: Date;
  message_updated_at: Date | null;
  deleted_at: Date | null;
};

function serializeAttachments(message: Message) {
  return [...message.attachments.values()].map((a) => ({
    id: a.id,
    name: a.name ?? null,
    url: a.url,
    proxyURL: a.proxyURL,
    contentType: a.contentType ?? null,
    description: a.description ?? null,
    size: a.size,
    width: a.width ?? null,
    height: a.height ?? null
  }));
}

function serializeEmbeds(message: Message): Array<Record<string, unknown>> {
  return message.embeds.map(
    (embed) => embed.toJSON() as unknown as Record<string, unknown>
  );
}

function serializeStickers(message: Message) {
  return [...message.stickers.values()].map((sticker) => ({
    id: sticker.id,
    name: sticker.name,
    description: sticker.description ?? null,
    format: sticker.format
  }));
}

export function serializeMessageRecord(message: Message): SerializedMessageRecord {
  if (!message.guildId) {
    throw new Error("Cannot serialize a non-guild message into the guild cache.");
  }

  return {
    message_id: message.id,
    guild_id: message.guildId,
    channel_id: message.channelId,
    author_id: message.author.id,
    author_name: message.author.tag,
    author_is_bot: message.author.bot,
    webhook_id: message.webhookId,
    content: message.content,
    attachments: serializeAttachments(message),
    embeds: serializeEmbeds(message),
    stickers: serializeStickers(message),
    reply_to_message_id: message.reference?.messageId ?? null,
    message_created_at: message.createdAt.toISOString(),
    message_updated_at: message.editedAt?.toISOString() ?? null
  };
}

export async function upsertSerializedMessages(
  records: SerializedMessageRecord[],
  retentionDays: number
): Promise<void> {
  if (records.length === 0) return;

  await withDbRetry("upsertSerializedMessages", () => db.query(
    `
    WITH input AS (
      SELECT *
      FROM jsonb_to_recordset($1::jsonb) AS x(
        message_id text,
        guild_id text,
        channel_id text,
        author_id text,
        author_name text,
        author_is_bot boolean,
        webhook_id text,
        content text,
        attachments jsonb,
        embeds jsonb,
        stickers jsonb,
        reply_to_message_id text,
        message_created_at timestamptz,
        message_updated_at timestamptz
      )
    )
    INSERT INTO message_cache (
      message_id, guild_id, channel_id, author_id, author_name,
      author_is_bot, webhook_id, content, attachments, embeds, stickers,
      reply_to_message_id, message_created_at, message_updated_at, expires_at
    )
    SELECT
      message_id, guild_id, channel_id, author_id, author_name,
      author_is_bot, webhook_id, content,
      COALESCE(attachments, '[]'::jsonb),
      COALESCE(embeds, '[]'::jsonb),
      COALESCE(stickers, '[]'::jsonb),
      reply_to_message_id, message_created_at, message_updated_at,
      CASE
        WHEN $2::int = 0 THEN NULL
        ELSE clock_timestamp() + ($2::int * interval '1 day')
      END
    FROM input
    ON CONFLICT (message_id) DO UPDATE
    SET guild_id = EXCLUDED.guild_id,
        channel_id = EXCLUDED.channel_id,
        author_id = EXCLUDED.author_id,
        author_name = EXCLUDED.author_name,
        author_is_bot = EXCLUDED.author_is_bot,
        webhook_id = EXCLUDED.webhook_id,
        content = EXCLUDED.content,
        attachments = EXCLUDED.attachments,
        embeds = EXCLUDED.embeds,
        stickers = EXCLUDED.stickers,
        reply_to_message_id = EXCLUDED.reply_to_message_id,
        message_created_at = EXCLUDED.message_created_at,
        message_updated_at = EXCLUDED.message_updated_at,
        expires_at = EXCLUDED.expires_at,
        cached_at = clock_timestamp()
    `,
    [JSON.stringify(records), retentionDays]
  ));
}

export async function cacheMessage(
  message: Message,
  retentionDays: number
): Promise<void> {
  if (!message.guildId) return;
  await upsertSerializedMessages([serializeMessageRecord(message)], retentionDays);
}

export async function getCachedMessage(messageId: string): Promise<CachedMessage | null> {
  const result = await withDbRetry("getCachedMessage", () => db.query<CachedMessage>(
    `
    SELECT message_id, guild_id, channel_id, author_id, author_name,
           author_is_bot, webhook_id, content, attachments, embeds, stickers,
           reply_to_message_id, message_created_at, message_updated_at, deleted_at
    FROM message_cache
    WHERE message_id = $1
    `,
    [messageId]
  ));

  return result.rows[0] ?? null;
}

export async function getNewestCachedMessageCursor(
  channelId: string
): Promise<{ messageId: string; createdAt: Date } | null> {
  const result = await withDbRetry("getNewestCachedMessageCursor", () => db.query<{ message_id: string; message_created_at: Date }>(
    `
    SELECT message_id, message_created_at
    FROM message_cache
    WHERE channel_id = $1
    ORDER BY message_created_at DESC, message_id DESC
    LIMIT 1
    `,
    [channelId]
  ));

  const row = result.rows[0];
  return row
    ? { messageId: row.message_id, createdAt: row.message_created_at }
    : null;
}

export async function getMessageCacheStorageBytes(): Promise<number> {
  const result = await withDbRetry("getMessageCacheStorageBytes", () => db.query<{ bytes: string }>(
    `SELECT pg_total_relation_size('message_cache')::bigint::text AS bytes`
  ));
  return Number(result.rows[0]?.bytes ?? 0);
}

export async function markMessageDeleted(messageId: string): Promise<void> {
  await withDbRetry("markMessageDeleted", () => db.query(
    `
    UPDATE message_cache
    SET deleted_at = COALESCE(deleted_at, clock_timestamp()),
        cached_at = clock_timestamp()
    WHERE message_id = $1
    `,
    [messageId]
  ));
}

export async function pruneExpiredMessages(): Promise<number> {
  const result = await db.query(
    `
    DELETE FROM message_cache
    WHERE expires_at IS NOT NULL
      AND expires_at <= clock_timestamp()
    `
  );

  return result.rowCount ?? 0;
}
