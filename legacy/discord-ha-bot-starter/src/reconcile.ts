import {
  Client,
  EmbedBuilder,
  type Guild,
  type GuildAuditLogsEntry,
  type Presence
} from "discord.js";
import { getGuildSettings } from "./guildSettings.js";
import { log } from "./logger.js";
import {
  claimAuditEntry,
  getAuditCursor,
  initializeAuditCursor,
  logGenericAuditEntry,
  saveReconcileSnapshot
} from "./auditTrail.js";
import { recordEvent } from "./eventLog.js";
import {
  botCanViewChannel,
  compactDiscordError,
  hasAuditLogPermission
} from "./visibility.js";

const MAX_AUDIT_REPLAY = 1000;

type AuditCatchupStatus =
  | "ok"
  | "baseline-initialized"
  | "unavailable-permission"
  | "fetch-failed";

function isEmbeddedPresence(presence: Presence): boolean {
  return presence.activities.some((activity) => {
    try {
      return activity.flags.has("Embedded");
    } catch {
      return (Number(activity.flags.bitfield) & 256) === 256;
    }
  });
}

function auditCatchupText(args: {
  status: AuditCatchupStatus;
  recovered: number;
  truncated: boolean;
}): string {
  if (args.status === "unavailable-permission") {
    return [
      "⚠️ **Unavailable:** Nifibot does not have **View Audit Log**.",
      "No audit cursor was advanced. Message/history recovery and the current-state snapshot still ran."
    ].join("\n");
  }

  if (args.status === "fetch-failed") {
    return [
      "⚠️ **Partial:** Discord Audit Log could not be fetched.",
      `${args.recovered} administrative action(s) were recovered before the failure.`,
      "The cursor only advances for entries that were actually fetched/claimed; a later startup can retry the remainder."
    ].join("\n");
  }

  if (args.status === "baseline-initialized") {
    return "Baseline initialized at Discord's newest audit entry. Older audit entries were not replayed automatically.";
  }

  return `${args.recovered} missed administrative action(s) recovered.${
    args.truncated
      ? "\n⚠️ Replay hit the safety cap; older entries may remain."
      : ""
  }`;
}

async function sendReconcileSummary(
  client: Client,
  guild: Guild,
  recovered: number,
  auditStatus: AuditCatchupStatus,
  truncated: boolean,
  snapshot: Record<string, unknown>
): Promise<void> {
  const settings = await getGuildSettings(guild.id);
  if (!settings.logging_enabled || !settings.startup_log_enabled) return;
  if (!settings.log_channel_id) return;

  const channel = await client.channels
    .fetch(settings.log_channel_id)
    .catch(() => null);
  if (!channel?.isSendable()) return;

  const partial =
    auditStatus === "unavailable-permission" || auditStatus === "fetch-failed";

  const embed = new EmbedBuilder()
    .setTitle(
      partial
        ? "🔄 Startup reconciliation complete (partial)"
        : "🔄 Startup reconciliation complete"
    )
    .addFields(
      {
        name: "Audit Log catch-up",
        value: auditCatchupText({
          status: auditStatus,
          recovered,
          truncated
        })
      },
      {
        name: "Current visible state",
        value: [
          `Members: **${snapshot.memberCount}**`,
          `Visible channels/threads cached: **${snapshot.channelCount}**`,
          `Roles: **${snapshot.roleCount}**`,
          `Visible VC occupants now: **${snapshot.voiceOccupants}**`,
          `Embedded Activities visible now: **${snapshot.embeddedActivities}**`,
          `Active timeouts visible in cache: **${snapshot.activeTimeouts}**`
        ].join("\n")
      },
      {
        name: "Privacy / recovery limits",
        value:
          "Channel/message reconciliation only includes channels Nifibot can currently view. Transient events that start **and** end while every Nifibot node is offline (for example VC join→leave, camera on→off, or Activity start→end) cannot be reconstructed."
      }
    )
    .setTimestamp();

  await channel.send({ embeds: [embed], allowedMentions: { parse: [] } });
}

function buildSnapshot(guild: Guild): Record<string, unknown> {
  const now = Date.now();
  const visibleChannels = [...guild.channels.cache.values()].filter((channel) =>
    botCanViewChannel(guild, channel)
  );
  const visibleChannelIds = new Set(visibleChannels.map((channel) => channel.id));

  const voiceStates = [...guild.voiceStates.cache.values()].filter(
    (state) => Boolean(state.channelId && visibleChannelIds.has(state.channelId))
  );
  const visiblePresences = [...guild.presences.cache.values()];
  const activeTimeouts = [...guild.members.cache.values()].filter(
    (member) => (member.communicationDisabledUntilTimestamp ?? 0) > now
  );

  return {
    capturedAt: new Date().toISOString(),
    memberCount: guild.memberCount,
    cachedMemberCount: guild.members.cache.size,
    channelCount: visibleChannels.length,
    roleCount: guild.roles.cache.size,
    voiceOccupants: voiceStates.length,
    voice: voiceStates.slice(0, 100).map((state) => ({
      userId: state.id,
      channelId: state.channelId,
      serverMute: state.serverMute,
      serverDeaf: state.serverDeaf,
      selfMute: state.selfMute,
      selfDeaf: state.selfDeaf,
      streaming: state.streaming,
      selfVideo: state.selfVideo
    })),
    embeddedActivities: visiblePresences.filter(isEmbeddedPresence).length,
    activityUsers: visiblePresences
      .filter(isEmbeddedPresence)
      .slice(0, 100)
      .map((presence) => presence.userId),
    activeTimeouts: activeTimeouts.length,
    timeoutUsers: activeTimeouts.slice(0, 100).map((member) => ({
      userId: member.id,
      until: member.communicationDisabledUntil?.toISOString() ?? null
    }))
  };
}

async function fetchAuditPageAfter(
  guild: Guild,
  after: string
): Promise<GuildAuditLogsEntry[]> {
  const logs = await guild.fetchAuditLogs({ limit: 100, after });
  return [...logs.entries.values()].sort(
    (a, b) => a.createdTimestamp - b.createdTimestamp
  );
}

async function reconcileGuild(client: Client, guild: Guild): Promise<void> {
  const settings = await getGuildSettings(guild.id);
  if (!settings.logging_enabled) return;

  let cursor = await getAuditCursor(guild.id);
  let recovered = 0;
  let truncated = false;
  let auditStatus: AuditCatchupStatus = "ok";

  if (!hasAuditLogPermission(guild)) {
    auditStatus = "unavailable-permission";
  } else if (!cursor) {
    try {
      const latest = await guild.fetchAuditLogs({ limit: 1 });
      const newest = latest.entries.first() ?? null;
      cursor = newest?.id ?? null;
      await initializeAuditCursor(guild.id, cursor);
      auditStatus = "baseline-initialized";
    } catch (error) {
      auditStatus = "fetch-failed";
      log.warn(
        {
          guildId: guild.id,
          error: compactDiscordError(error)
        },
        "Audit Log baseline fetch failed; cursor was not advanced"
      );
    }
  } else {
    let replayCursor: string = cursor;

    while (recovered < MAX_AUDIT_REPLAY) {
      let page: GuildAuditLogsEntry[];
      try {
        page = await fetchAuditPageAfter(guild, replayCursor);
      } catch (error) {
        auditStatus = "fetch-failed";
        log.warn(
          {
            guildId: guild.id,
            cursor: replayCursor,
            error: compactDiscordError(error)
          },
          "Audit Log replay fetch failed; leaving remaining entries for a later startup"
        );
        break;
      }

      if (!page.length) break;

      for (const entry of page) {
        replayCursor = entry.id;
        const claimed = await claimAuditEntry(
          guild.id,
          entry,
          "startup-replay"
        );
        if (!claimed) continue;

        await logGenericAuditEntry(
          client,
          guild,
          entry,
          "startup-replay"
        ).catch((error) => {
          log.warn(
            {
              error: compactDiscordError(error),
              guildId: guild.id,
              auditEntryId: entry.id
            },
            "Failed to emit recovered audit log entry"
          );
        });
        recovered += 1;
        if (recovered >= MAX_AUDIT_REPLAY) break;
      }

      if (page.length < 100) break;
    }

    if (recovered >= MAX_AUDIT_REPLAY) truncated = true;
  }

  const snapshot = buildSnapshot(guild);
  await saveReconcileSnapshot(guild.id, snapshot);

  await recordEvent({
    guildId: guild.id,
    eventType: "system.reconcile",
    actorId: client.user?.id ?? null,
    data: {
      recovered,
      auditStatus,
      truncated,
      snapshot
    }
  }).catch(() => undefined);

  await sendReconcileSummary(
    client,
    guild,
    recovered,
    auditStatus,
    truncated,
    snapshot
  ).catch((error) => {
    log.warn(
      { error: compactDiscordError(error), guildId: guild.id },
      "Failed to send reconciliation summary"
    );
  });

  const logPayload = {
    guildId: guild.id,
    recoveredAuditEntries: recovered,
    auditStatus,
    truncated
  };

  if (
    auditStatus === "unavailable-permission" ||
    auditStatus === "fetch-failed"
  ) {
    log.warn(logPayload, "Startup state reconciliation complete with partial Audit Log coverage");
  } else {
    log.info(logPayload, "Startup audit/state reconciliation complete");
  }
}

export async function runStartupReconciliation(client: Client): Promise<void> {
  for (const guild of client.guilds.cache.values()) {
    await reconcileGuild(client, guild).catch((error) => {
      log.warn(
        { error: compactDiscordError(error), guildId: guild.id },
        "Startup audit/state reconciliation failed for guild"
      );
    });
  }
}
