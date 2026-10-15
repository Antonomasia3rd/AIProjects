import type {
  AuditLogEvent,
  Guild,
  GuildAuditLogsEntry
} from "discord.js";

export type AuditActor = {
  id: string;
  tag: string;
  reason: string | null;
  entryId: string;
} | null;

export async function findRecentAuditActor(
  guild: Guild,
  type: AuditLogEvent,
  targetId: string | null,
  maxAgeMs = 15_000
): Promise<AuditActor> {
  const logs = await guild.fetchAuditLogs({ type, limit: 8 }).catch(() => null);
  if (!logs) return null;

  const now = Date.now();

  const candidates = [...logs.entries.values()]
    .filter((entry: GuildAuditLogsEntry) => {
      const age = now - entry.createdTimestamp;
      if (age < 0 || age > maxAgeMs) return false;

      if (!targetId) return true;

      const target = entry.target as { id?: string } | null;
      return target?.id === targetId;
    })
    .sort(
      (a: GuildAuditLogsEntry, b: GuildAuditLogsEntry) =>
        b.createdTimestamp - a.createdTimestamp
    );

  const entry = candidates[0];
  if (!entry?.executor) return null;

  return {
    id: entry.executor.id,
    tag: entry.executor.tag ?? entry.executor.username ?? entry.executor.id,
    reason: entry.reason ?? null,
    entryId: entry.id
  };
}
