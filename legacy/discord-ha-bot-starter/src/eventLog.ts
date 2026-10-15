import { db } from "./db.js";

export type EventLogInput = {
  guildId: string;
  eventType: string;
  actorId?: string | null;
  targetId?: string | null;
  channelId?: string | null;
  messageId?: string | null;
  data?: unknown;
};

export async function recordEvent(input: EventLogInput): Promise<void> {
  await db.query(
    `
    INSERT INTO event_log (
      guild_id, event_type, actor_id, target_id,
      channel_id, message_id, data
    )
    VALUES ($1, $2, $3, $4, $5, $6, $7::jsonb)
    `,
    [
      input.guildId,
      input.eventType,
      input.actorId ?? null,
      input.targetId ?? null,
      input.channelId ?? null,
      input.messageId ?? null,
      JSON.stringify(input.data ?? {})
    ]
  );
}
