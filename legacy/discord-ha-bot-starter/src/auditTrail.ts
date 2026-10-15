import {
  AuditLogEvent,
  Client,
  EmbedBuilder,
  type Guild,
  type GuildAuditLogsEntry
} from "discord.js";
import { db } from "./db.js";
import { getGuildSettings } from "./guildSettings.js";
import { log } from "./logger.js";
import { recordEvent } from "./eventLog.js";

export type AuditSource = "live" | "startup-replay";

type AuditChange = {
  key: string;
  old?: unknown;
  new?: unknown;
};

const actionNames = new Map<number, string>([
  [1, "Server settings updated"],
  [10, "Channel created"],
  [11, "Channel updated"],
  [12, "Channel deleted"],
  [13, "Channel permission overwrite created"],
  [14, "Channel permission overwrite updated"],
  [15, "Channel permission overwrite deleted"],
  [20, "Member kicked"],
  [21, "Members pruned"],
  [22, "Member banned"],
  [23, "Member unbanned"],
  [24, "Member updated"],
  [25, "Member roles updated"],
  [26, "Member moved"],
  [27, "Member disconnected from voice"],
  [28, "Bot added"],
  [30, "Role created"],
  [31, "Role updated"],
  [32, "Role deleted"],
  [40, "Invite created"],
  [41, "Invite updated"],
  [42, "Invite deleted"],
  [50, "Webhook created"],
  [51, "Webhook updated"],
  [52, "Webhook deleted"],
  [60, "Emoji created"],
  [61, "Emoji updated"],
  [62, "Emoji deleted"],
  [72, "Message deleted by moderator"],
  [73, "Messages bulk-deleted"],
  [74, "Message pinned"],
  [75, "Message unpinned"],
  [80, "Integration created"],
  [81, "Integration updated"],
  [82, "Integration deleted"],
  [83, "Stage instance created"],
  [84, "Stage instance updated"],
  [85, "Stage instance deleted"],
  [90, "Sticker created"],
  [91, "Sticker updated"],
  [92, "Sticker deleted"],
  [100, "Scheduled event created"],
  [101, "Scheduled event updated"],
  [102, "Scheduled event deleted"],
  [110, "Thread created"],
  [111, "Thread updated"],
  [112, "Thread deleted"],
  [121, "Application command permissions updated"],
  [130, "Soundboard sound created"],
  [131, "Soundboard sound updated"],
  [132, "Soundboard sound deleted"],
  [140, "AutoMod rule created"],
  [141, "AutoMod rule updated"],
  [142, "AutoMod rule deleted"],
  [143, "AutoMod blocked a message"],
  [144, "AutoMod flagged a message"],
  [145, "AutoMod timed out a member"],
  [163, "Onboarding prompt created"],
  [164, "Onboarding prompt updated"],
  [165, "Onboarding prompt deleted"],
  [166, "Onboarding created"],
  [167, "Onboarding updated"],
  [190, "Server guide/home settings created"],
  [191, "Server guide/home settings updated"]
]);

// These already have richer live Gateway loggers in discordClient.ts. We still
// claim them so startup replay knows the event was observed while online.
const detailedLiveActions = new Set<number>([
  AuditLogEvent.MemberKick,
  AuditLogEvent.MemberBanAdd,
  AuditLogEvent.MemberBanRemove,
  AuditLogEvent.MemberRoleUpdate,
  AuditLogEvent.MemberMove,
  AuditLogEvent.MemberDisconnect,
  AuditLogEvent.MessageDelete,
  AuditLogEvent.MessageBulkDelete
]);

function trunc(value: string, max = 1000): string {
  return value.length <= max ? value : `${value.slice(0, max - 1)}…`;
}

function discordTimestamp(ms: number): string {
  const seconds = Math.floor(ms / 1000);
  return `<t:${seconds}:F>\n<t:${seconds}:R>`;
}

function safeJson(value: unknown): string {
  try {
    return JSON.stringify(value);
  } catch {
    return String(value);
  }
}

function formatValue(value: unknown): string {
  if (value === undefined) return "*(not present)*";
  if (value === null) return "`null`";
  if (typeof value === "string") return trunc(value, 350);
  if (typeof value === "number" || typeof value === "boolean") {
    return `\`${String(value)}\``;
  }
  return `\`${trunc(safeJson(value), 330)}\``;
}

function formatChanges(changes: readonly AuditChange[]): string {
  if (!changes.length) return "*No change payload supplied by Discord.*";

  const lines = changes.slice(0, 12).map((change) => {
    const oldValue = formatValue(change.old);
    const newValue = formatValue(change.new);
    return `**${change.key}**\n${oldValue} → ${newValue}`;
  });

  if (changes.length > 12) {
    lines.push(`*…and ${changes.length - 12} more change(s).*`);
  }

  return trunc(lines.join("\n"), 1024);
}


function targetLabel(action: number, targetId: string | null): string {
  if (!targetId) return "*No target ID supplied*";

  if ([10, 11, 12, 13, 14, 15, 40, 41, 42, 50, 51, 52, 110, 111, 112].includes(action)) {
    return `<#${targetId}> (\`${targetId}\`)`;
  }

  if ([30, 31, 32].includes(action)) {
    return `<@&${targetId}> (\`${targetId}\`)`;
  }

  if ([20, 22, 23, 24, 25, 26, 27, 28, 145].includes(action)) {
    return `<@${targetId}> (\`${targetId}\`)`;
  }

  return `\`${targetId}\``;
}

function categoryForAction(action: number): "moderation" | "system" {
  if (
    [20, 21, 22, 23, 24, 25, 26, 27, 72, 73, 74, 75, 140, 141, 142, 143, 144, 145].includes(
      action
    )
  ) {
    return "moderation";
  }
  return "system";
}

async function sendAuditEmbed(
  client: Client,
  guild: Guild,
  category: "moderation" | "system",
  embed: EmbedBuilder
): Promise<void> {
  const settings = await getGuildSettings(guild.id);
  if (!settings.logging_enabled) return;

  const channelId =
    category === "moderation"
      ? settings.moderation_log_channel_id ??
        settings.member_log_channel_id ??
        settings.log_channel_id
      : settings.log_channel_id;
  if (!channelId) return;

  const channel = await client.channels.fetch(channelId).catch(() => null);
  if (!channel?.isSendable()) return;

  await channel.send({
    embeds: [embed],
    allowedMentions: { parse: [] }
  });
}

export function auditActionName(action: number): string {
  return actionNames.get(action) ?? `Audit action ${action}`;
}

export function isTimeoutAuditEntry(entry: GuildAuditLogsEntry): boolean {
  return (
    entry.action === AuditLogEvent.MemberUpdate &&
    entry.changes.some((change) => change.key === "communication_disabled_until")
  );
}

export function shouldLogGenericLiveAudit(action: number): boolean {
  if (detailedLiveActions.has(action)) return false;
  // MemberUpdate is handled specially for timeouts and by GuildMemberUpdate for
  // nickname changes. Avoid a second generic member-update card.
  if (action === AuditLogEvent.MemberUpdate) return false;
  return actionNames.has(action);
}

export async function claimAuditEntry(
  guildId: string,
  entry: GuildAuditLogsEntry,
  source: AuditSource
): Promise<boolean> {
  const result = await db.query(
    `
    INSERT INTO processed_audit_entries (
      guild_id, audit_entry_id, action_type, executor_id, target_id,
      occurred_at, source
    )
    VALUES ($1, $2, $3, $4, $5, to_timestamp($6::double precision / 1000.0), $7)
    ON CONFLICT (guild_id, audit_entry_id) DO NOTHING
    RETURNING audit_entry_id
    `,
    [
      guildId,
      entry.id,
      Number(entry.action),
      entry.executorId ?? null,
      entry.targetId ?? null,
      entry.createdTimestamp,
      source
    ]
  );

  await db.query(
    `
    INSERT INTO guild_reconcile_state (
      guild_id, last_audit_entry_id, last_reconciled_at, updated_at
    )
    VALUES ($1, $2, clock_timestamp(), clock_timestamp())
    ON CONFLICT (guild_id) DO UPDATE
    SET last_audit_entry_id = CASE
          WHEN guild_reconcile_state.last_audit_entry_id IS NULL THEN EXCLUDED.last_audit_entry_id
          WHEN EXCLUDED.last_audit_entry_id::numeric > guild_reconcile_state.last_audit_entry_id::numeric
            THEN EXCLUDED.last_audit_entry_id
          ELSE guild_reconcile_state.last_audit_entry_id
        END,
        last_reconciled_at = clock_timestamp(),
        updated_at = clock_timestamp()
    `,
    [guildId, entry.id]
  );

  return result.rowCount === 1;
}

export async function getAuditCursor(guildId: string): Promise<string | null> {
  const result = await db.query<{ last_audit_entry_id: string | null }>(
    `SELECT last_audit_entry_id FROM guild_reconcile_state WHERE guild_id = $1`,
    [guildId]
  );
  return result.rows[0]?.last_audit_entry_id ?? null;
}

export async function initializeAuditCursor(
  guildId: string,
  auditEntryId: string | null
): Promise<void> {
  await db.query(
    `
    INSERT INTO guild_reconcile_state (
      guild_id, last_audit_entry_id, last_reconciled_at, updated_at
    )
    VALUES ($1, $2, clock_timestamp(), clock_timestamp())
    ON CONFLICT (guild_id) DO UPDATE
    SET last_audit_entry_id = COALESCE(guild_reconcile_state.last_audit_entry_id, EXCLUDED.last_audit_entry_id),
        last_reconciled_at = clock_timestamp(),
        updated_at = clock_timestamp()
    `,
    [guildId, auditEntryId]
  );
}

export async function logGenericAuditEntry(
  client: Client,
  guild: Guild,
  entry: GuildAuditLogsEntry,
  source: AuditSource
): Promise<void> {
  const action = Number(entry.action);
  const actionName = isTimeoutAuditEntry(entry)
    ? "Member timeout changed"
    : auditActionName(action);
  const executor =
    entry.executor ??
    (entry.executorId
      ? await client.users.fetch(entry.executorId).catch(() => null)
      : null);

  const titlePrefix = source === "startup-replay" ? "♻️ Recovered" : "🛠️";
  const embed = new EmbedBuilder()
    .setTitle(`${titlePrefix} ${actionName}`)
    .addFields(
      {
        name: "Performed by",
        value: entry.executorId
          ? `<@${entry.executorId}> (\`${entry.executorId}\`)${executor ? `\n${executor.tag}` : ""}`
          : "*Unknown / system action*"
      },
      {
        name: "Target",
        value: targetLabel(action, entry.targetId ?? null)
      },
      {
        name: "Occurred",
        value: discordTimestamp(entry.createdTimestamp)
      },
      {
        name: "Reason",
        value: entry.reason ? trunc(entry.reason, 1024) : "*No reason provided*"
      },
      {
        name: "Changes",
        value: formatChanges(entry.changes as readonly AuditChange[])
      },
      {
        name: "Audit entry",
        value: `\`${entry.id}\`\nAction type: \`${action}\``
      }
    )
    .setTimestamp(entry.createdTimestamp);

  if (source === "startup-replay") {
    embed.setFooter({
      text: "Recovered from Discord Audit Log after Nifibot was offline."
    });
  }

  await sendAuditEmbed(client, guild, categoryForAction(action), embed);
  await recordEvent({
    guildId: guild.id,
    eventType: `audit.${action}`,
    actorId: entry.executorId ?? null,
    targetId: entry.targetId ?? null,
    data: {
      action,
      actionName,
      auditEntryId: entry.id,
      reason: entry.reason ?? null,
      changes: entry.changes,
      source
    }
  });
}

export async function saveReconcileSnapshot(
  guildId: string,
  snapshot: unknown
): Promise<void> {
  await db.query(
    `
    INSERT INTO guild_reconcile_state (
      guild_id, last_reconciled_at, snapshot, updated_at
    )
    VALUES ($1, clock_timestamp(), $2::jsonb, clock_timestamp())
    ON CONFLICT (guild_id) DO UPDATE
    SET last_reconciled_at = clock_timestamp(),
        snapshot = EXCLUDED.snapshot,
        updated_at = clock_timestamp()
    `,
    [guildId, JSON.stringify(snapshot)]
  );
}

export async function getReconcileState(guildId: string): Promise<{
  last_audit_entry_id: string | null;
  last_reconciled_at: Date | null;
  snapshot: unknown;
} | null> {
  const result = await db.query<{
    last_audit_entry_id: string | null;
    last_reconciled_at: Date | null;
    snapshot: unknown;
  }>(
    `
    SELECT last_audit_entry_id, last_reconciled_at, snapshot
    FROM guild_reconcile_state
    WHERE guild_id = $1
    `,
    [guildId]
  );
  return result.rows[0] ?? null;
}

export function logAuditFailure(error: unknown, context: Record<string, unknown>): void {
  log.warn({ err: error, ...context }, "Audit reconciliation/logging operation failed");
}
