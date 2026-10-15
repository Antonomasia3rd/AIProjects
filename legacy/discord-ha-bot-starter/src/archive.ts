import fs from "node:fs/promises";
import { createWriteStream } from "node:fs";
import path from "node:path";
import crypto from "node:crypto";
import { db } from "./db.js";
import { archiveJsonLines, cursorRows, publishArchive } from "./runtime/archiveCore.js";

function parseMonth(month: string): { start: Date; end: Date } {
  if (!/^\d{4}-\d{2}$/.test(month)) throw new Error("Month must use YYYY-MM, for example 2026-10.");
  const [yearText, monthText] = month.split("-"), year = Number(yearText), index = Number(monthText) - 1;
  if (index < 0 || index > 11) throw new Error("Month must be between 01 and 12.");
  const start = new Date(0); start.setUTCFullYear(year, index, 1); start.setUTCHours(0, 0, 0, 0);
  const end = new Date(start); end.setUTCMonth(index + 1);
  return { start, end };
}
export async function createGuildMonthArchive(guildId: string, month: string): Promise<string> {
  const { start, end } = parseMonth(month);
  const client = await db.connect();
  let transaction = false;
  try {
    await client.query("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY"); transaction = true;
    const parameters = [guildId, start, end];
    const counts = await client.query<{ events: string; messages: string }>(`
      SELECT (SELECT count(*) FROM event_log WHERE guild_id = $1 AND occurred_at >= $2 AND occurred_at < $3)::text AS events,
        (SELECT count(*) FROM message_cache WHERE guild_id = $1 AND message_created_at >= $2 AND message_created_at < $3)::text AS messages`, parameters);
    await client.query(`DECLARE archive_events NO SCROLL CURSOR FOR
      SELECT id, guild_id, event_type, actor_id, target_id, channel_id, message_id, occurred_at, data
      FROM event_log WHERE guild_id = $1 AND occurred_at >= $2 AND occurred_at < $3 ORDER BY occurred_at, id`, parameters);
    await client.query(`DECLARE archive_messages NO SCROLL CURSOR FOR
      SELECT message_id, guild_id, channel_id, author_id, author_name, author_is_bot, webhook_id,
        content, attachments, embeds, stickers, reply_to_message_id, message_created_at,
        message_updated_at, deleted_at, cached_at
      FROM message_cache WHERE guild_id = $1 AND message_created_at >= $2 AND message_created_at < $3
      ORDER BY message_created_at, message_id`, parameters);
    const count = (value: string) => { const number = Number(value); return Number.isSafeInteger(number) ? number : value; };
    const lines = archiveJsonLines({ recordType: "archive_metadata", formatVersion: 1, guildId, month,
      createdAt: new Date().toISOString(), eventCount: count(counts.rows[0].events), messageCount: count(counts.rows[0].messages) },
      cursorRows(sql => client.query(sql), "archive_events"), cursorRows(sql => client.query(sql), "archive_messages"));
    const directory = path.join(process.cwd(), "archives", guildId);
    await fs.mkdir(directory, { recursive: true });
    const target = path.join(directory, `${month}.jsonl.gz`), temporary = target + ".tmp-" + crypto.randomUUID();
    await publishArchive(lines, temporary, target, {
      create: file => createWriteStream(file, { flags: "wx", highWaterMark: 64 * 1024 }),
      async commitSnapshot() { await client.query("COMMIT"); transaction = false; },
      rename: (from, to) => fs.rename(from, to),
      remove: file => fs.rm(file, { force: true })
    });
    return target;
  } finally {
    let discard: Error | undefined;
    if (transaction) try { await client.query("ROLLBACK"); }
    catch (error) { discard = error instanceof Error ? error : new Error("Archive transaction rollback failed."); }
    // A connection with an uncertain transaction state must not be reused.
    client.release(discard);
  }
}
