import {
  AuditLogEvent,
  Client,
  EmbedBuilder,
  Events,
  GatewayIntentBits,
  MessageFlags,
  Partials,
  type Activity,
  type GuildMember,
  type Message,
  type MessageReaction,
  type Presence,
  type User
} from "discord.js";
import { config } from "./config.js";
import { log } from "./logger.js";
import { commandMap } from "./commands/index.js";
import { getGuildSettings, isIgnoredMessageUser, type GuildSettings } from "./guildSettings.js";
import {
  cacheMessage,
  getCachedMessage,
  markMessageDeleted,
  pruneExpiredMessages,
  type CachedMessage
} from "./messageCache.js";
import { recordEvent } from "./eventLog.js";
import { findRecentAuditActor, type AuditActor } from "./audit.js";
import { runStartupCatchup } from "./backfill.js";
import { runStartupReconciliation } from "./reconcile.js";
import {
  claimAuditEntry,
  isTimeoutAuditEntry,
  logGenericAuditEntry,
  shouldLogGenericLiveAudit
} from "./auditTrail.js";
import os from "node:os";

type LogCategory = "messages" | "members" | "voice" | "moderation" | "system";

function isConfiguredOutputChannel(
  settings: GuildSettings,
  channelId: string
): boolean {
  return [
    settings.log_channel_id,
    settings.member_log_channel_id,
    settings.voice_log_channel_id,
    settings.moderation_log_channel_id,
    settings.archive_channel_id
  ].some((id) => id === channelId);
}

function isSelfAuthoredMessage(
  client: Client,
  message: { author?: { id: string } | null }
): boolean {
  return Boolean(client.user && message.author?.id === client.user.id);
}

const compareIgnoredKeys = new Set([
  "proxy_url",
  "proxyURL",
  "height",
  "width"
]);

function canonicalizeForComparison(value: unknown): unknown {
  if (Array.isArray(value)) {
    return value.map(canonicalizeForComparison);
  }

  if (value && typeof value === "object") {
    const record = value as Record<string, unknown>;
    return Object.fromEntries(
      Object.entries(record)
        .filter(([key]) => !compareIgnoredKeys.has(key))
        .sort(([a], [b]) => a.localeCompare(b))
        .map(([key, item]) => [key, canonicalizeForComparison(item)])
    );
  }

  return value;
}

function meaningfulStateFromCached(cached: CachedMessage) {
  return canonicalizeForComparison({
    content: cached.content ?? "",
    embeds: cached.embeds ?? [],
    attachments: cached.attachments ?? [],
    stickers: cached.stickers ?? [],
    replyToMessageId: cached.reply_to_message_id ?? null
  });
}

function meaningfulStateFromMessage(message: Message) {
  return canonicalizeForComparison({
    content: message.content ?? "",
    embeds: message.embeds.map((embed) => embed.toJSON()),
    attachments: [...message.attachments.values()].map((attachment) => ({
      id: attachment.id,
      name: attachment.name ?? null,
      url: attachment.url,
      contentType: attachment.contentType ?? null,
      description: attachment.description ?? null,
      size: attachment.size
    })),
    stickers: [...message.stickers.values()].map((sticker) => ({
      id: sticker.id,
      name: sticker.name,
      description: sticker.description ?? null,
      format: sticker.format
    })),
    replyToMessageId: message.reference?.messageId ?? null
  });
}


function botSemanticEmbed(value: unknown): unknown {
  if (!value || typeof value !== "object" || Array.isArray(value)) return value;
  const raw = value as Record<string, unknown>;

  const objectSubset = (
    input: unknown,
    keys: string[]
  ): Record<string, unknown> | null => {
    if (!input || typeof input !== "object" || Array.isArray(input)) return null;
    const record = input as Record<string, unknown>;
    const out: Record<string, unknown> = {};
    for (const key of keys) {
      if (record[key] !== undefined) out[key] = record[key];
    }
    return Object.keys(out).length ? out : null;
  };

  return canonicalizeForComparison({
    title: raw.title ?? null,
    description: raw.description ?? null,
    url: raw.url ?? null,
    author: objectSubset(raw.author, ["name", "url", "icon_url"]),
    footer: objectSubset(raw.footer, ["text", "icon_url"]),
    fields: Array.isArray(raw.fields)
      ? raw.fields.map((field) => objectSubset(field, ["name", "value", "inline"]))
      : [],
    image: objectSubset(raw.image, ["url"]),
    thumbnail: objectSubset(raw.thumbnail, ["url"])
  });
}

function botMeaningfulStateFromCached(cached: CachedMessage) {
  return canonicalizeForComparison({
    content: cached.content ?? "",
    embeds: (cached.embeds ?? []).map(botSemanticEmbed),
    attachments: cached.attachments ?? [],
    stickers: cached.stickers ?? [],
    replyToMessageId: cached.reply_to_message_id ?? null
  });
}

function botMeaningfulStateFromMessage(message: Message) {
  return canonicalizeForComparison({
    content: message.content ?? "",
    embeds: message.embeds.map((embed) => botSemanticEmbed(embed.toJSON())),
    attachments: [...message.attachments.values()].map((attachment) => ({
      id: attachment.id,
      name: attachment.name ?? null,
      url: attachment.url,
      contentType: attachment.contentType ?? null,
      description: attachment.description ?? null,
      size: attachment.size
    })),
    stickers: [...message.stickers.values()].map((sticker) => ({
      id: sticker.id,
      name: sticker.name,
      description: sticker.description ?? null,
      format: sticker.format
    })),
    replyToMessageId: message.reference?.messageId ?? null
  });
}

function trunc(value: string | null | undefined, max = 1000): string {
  const text = value?.trim() || "*No text content*";
  return text.length <= max ? text : `${text.slice(0, max - 1)}…`;
}


function discordMessageUrl(
  guildId: string,
  channelId: string,
  messageId: string
): string {
  return `https://discord.com/channels/${guildId}/${channelId}/${messageId}`;
}

function discordChannelUrl(guildId: string, channelId: string): string {
  return `https://discord.com/channels/${guildId}/${channelId}`;
}

function discordTimestamp(value: Date | number): string {
  const ms = value instanceof Date ? value.getTime() : value;
  const seconds = Math.floor(ms / 1000);
  return `<t:${seconds}:F>\n<t:${seconds}:R>`;
}

function durationLabel(start: Date | number, end: Date | number): string {
  const startMs = start instanceof Date ? start.getTime() : start;
  const endMs = end instanceof Date ? end.getTime() : end;
  let total = Math.max(0, Math.floor((endMs - startMs) / 1000));
  const days = Math.floor(total / 86400);
  total %= 86400;
  const hours = Math.floor(total / 3600);
  total %= 3600;
  const minutes = Math.floor(total / 60);
  const seconds = total % 60;

  const parts: string[] = [];
  if (days) parts.push(`${days}d`);
  if (hours) parts.push(`${hours}h`);
  if (minutes) parts.push(`${minutes}m`);
  if (seconds || parts.length === 0) parts.push(`${seconds}s`);
  return parts.slice(0, 3).join(" ");
}

const sleep = (ms: number) =>
  new Promise<void>((resolve) => setTimeout(resolve, ms));

function authorLabel(
  id: string,
  name: string | null | undefined,
  isBot = false
): string {
  return `<@${id}> (\`${id}\`)${isBot ? " 🤖" : ""}${name ? `\n${name}` : ""}`;
}

function summarizeAttachments(
  attachments: Array<Record<string, unknown>> | undefined
): string | null {
  if (!attachments?.length) return null;

  return trunc(
    attachments
      .slice(0, 8)
      .map((a) => {
        const name = String(a.name ?? a.id ?? "attachment");
        const url = String(a.url ?? "");
        const size = typeof a.size === "number" ? ` — ${a.size} bytes` : "";
        return url ? `[${name}](${url})${size}` : `${name}${size}`;
      })
      .join("\n")
  );
}

function summarizeEmbeds(
  embeds: readonly unknown[] | undefined
): string | null {
  if (!embeds?.length) return null;

  const out: string[] = [];

  for (const [index, value] of embeds.slice(0, 4).entries()) {
    if (typeof value !== "object" || value === null || Array.isArray(value)) {
      continue;
    }

    const raw = value as Record<string, unknown>;
    const title = typeof raw.title === "string" ? raw.title : null;
    const description =
      typeof raw.description === "string" ? raw.description : null;
    const url = typeof raw.url === "string" ? raw.url : null;
    const fields = Array.isArray(raw.fields) ? raw.fields : [];

    const author =
      raw.author && typeof raw.author === "object" && !Array.isArray(raw.author)
        ? (raw.author as Record<string, unknown>)
        : null;
    const footer =
      raw.footer && typeof raw.footer === "object" && !Array.isArray(raw.footer)
        ? (raw.footer as Record<string, unknown>)
        : null;

    const fieldLines = fields
      .slice(0, 8)
      .map((field) => {
        if (typeof field !== "object" || field === null || Array.isArray(field)) {
          return null;
        }
        const record = field as Record<string, unknown>;
        const name = typeof record.name === "string" ? record.name : "Field";
        const fieldValue =
          typeof record.value === "string" ? record.value : "*No value*";
        return `• **${trunc(name, 120)}**: ${trunc(fieldValue, 300)}`;
      })
      .filter((line): line is string => Boolean(line));

    out.push(
      [
        `**Embed ${index + 1}**`,
        author && typeof author.name === "string"
          ? `Author: ${author.name}`
          : null,
        title ? `Title: ${title}` : null,
        description ? `Description: ${description}` : null,
        ...fieldLines,
        footer && typeof footer.text === "string"
          ? `Footer: ${footer.text}`
          : null,
        url ? `URL: ${url}` : null
      ]
        .filter(Boolean)
        .join("\n")
    );
  }

  return trunc(out.join("\n\n"));
}

function summarizeStickers(
  stickers: Array<Record<string, unknown>> | undefined
): string | null {
  if (!stickers?.length) return null;

  return trunc(
    stickers
      .slice(0, 8)
      .map((s) => `${String(s.name ?? "Sticker")} (\`${String(s.id ?? "?")}\`)`)
      .join("\n")
  );
}

async function sendLog(
  client: Client,
  guildId: string,
  category: LogCategory,
  embed: EmbedBuilder
): Promise<boolean> {
  const settings = await getGuildSettings(guildId);
  if (!settings.logging_enabled) return false;

  const channelId =
    category === "members"
      ? settings.member_log_channel_id ?? settings.log_channel_id
      : category === "voice"
        ? settings.voice_log_channel_id ??
          settings.member_log_channel_id ??
          settings.log_channel_id
        : category === "moderation"
          ? settings.moderation_log_channel_id ??
            settings.member_log_channel_id ??
            settings.log_channel_id
          : settings.log_channel_id;

  if (!channelId) return false;

  const channel = await client.channels.fetch(channelId).catch(() => null);
  if (!channel?.isTextBased() || channel.isDMBased()) return false;

  await channel.send({
    embeds: [embed],
    allowedMentions: { parse: [] }
  });

  return true;
}

function addCachedMessageFields(
  embed: EmbedBuilder,
  cached: CachedMessage | null,
  guildId: string,
  channelId: string,
  messageId: string,
  deletedAt = new Date()
) {
  if (!cached) {
    embed.addFields(
      { name: "Author", value: "*Unknown — message was not cached*" },
      {
        name: "Channel",
        value: `<#${channelId}>\n[Open channel](${discordChannelUrl(guildId, channelId)})`
      },
      { name: "Content", value: "*No cached content*" },
      {
        name: "Original message",
        value: `[Open original URL](${discordMessageUrl(guildId, channelId, messageId)})\n*The message itself is deleted, so Discord may not resolve it.*`
      }
    );
    return;
  }

  embed.addFields(
    {
      name: "Author",
      value: authorLabel(
        cached.author_id,
        cached.author_name,
        cached.author_is_bot
      )
    },
    {
      name: "Channel",
      value: `<#${channelId}>\n[Open channel](${discordChannelUrl(guildId, channelId)})`
    },
    {
      name: "Sent",
      value: `${discordTimestamp(cached.message_created_at)}\n**Age when deleted:** ${durationLabel(cached.message_created_at, deletedAt)}`
    },
    {
      name: "Original message",
      value: `[Open original URL](${discordMessageUrl(guildId, channelId, cached.message_id)})\n*The message is deleted, so Discord may no longer resolve this jump.*`
    }
  );

  if (cached.message_updated_at) {
    embed.addFields({
      name: "Last edited",
      value: discordTimestamp(cached.message_updated_at)
    });
  }

  if ((cached.content ?? "").trim()) {
    embed.addFields({ name: "Content", value: trunc(cached.content) });
  }

  const embeds = summarizeEmbeds(cached.embeds);
  const attachments = summarizeAttachments(cached.attachments);
  const stickers = summarizeStickers(cached.stickers);

  if (embeds) embed.addFields({ name: "Embeds", value: embeds });
  if (attachments) embed.addFields({ name: "Attachments", value: attachments });
  if (stickers) embed.addFields({ name: "Stickers", value: stickers });

  if (cached.reply_to_message_id) {
    embed.addFields({
      name: "Reply target",
      value: `[Open replied-to message](${discordMessageUrl(
        guildId,
        channelId,
        cached.reply_to_message_id
      )})\nMessage ID: \`${cached.reply_to_message_id}\``
    });
  }
}

type ReactionInput = {
  partial: boolean;
  fetch(): Promise<MessageReaction>;
};

type UserInput = {
  partial: boolean;
  fetch(): Promise<User>;
};

async function hydrateReaction(
  reaction: ReactionInput,
  user: UserInput
): Promise<{ reaction: MessageReaction; user: User }> {
  const hydratedReaction = reaction.partial
    ? await reaction.fetch()
    : (reaction as MessageReaction);
  const hydratedUser = user.partial
    ? await user.fetch()
    : (user as User);

  if (hydratedReaction.message.partial) {
    await hydratedReaction.message.fetch();
  }

  return { reaction: hydratedReaction, user: hydratedUser };
}


function embeddedActivities(presence: Presence | null): Activity[] {
  if (!presence) return [];

  return presence.activities.filter((activity) => {
    try {
      return activity.flags.has("Embedded");
    } catch {
      // Discord's EMBEDDED activity flag is 1 << 8 (256). Keep a numeric
      // fallback for compatibility across discord.js minor versions.
      return (Number(activity.flags.bitfield) & 256) === 256;
    }
  });
}

function embeddedActivityKey(activity: Activity): string {
  return [
    activity.applicationId ?? `name:${activity.name}`,
    activity.party?.id ?? "no-party"
  ].join(":");
}

function embeddedActivitySummary(activity: Activity): string {
  const lines: string[] = [`**${trunc(activity.name, 180)}**`];

  if (activity.details) lines.push(`**Details:** ${trunc(activity.details, 350)}`);
  if (activity.state) lines.push(`**State:** ${trunc(activity.state, 350)}`);
  if (activity.applicationId) {
    lines.push(`**Application ID:** \`${activity.applicationId}\``);
  }
  if (activity.party?.id) lines.push(`**Party / instance:** \`${activity.party.id}\``);
  if (activity.party?.size) {
    lines.push(
      `**Party size:** ${activity.party.size[0]}/${activity.party.size[1]}`
    );
  }
  if (activity.timestamps?.start) {
    lines.push(`**Activity since:** ${discordTimestamp(activity.timestamps.start)}`);
  }

  return trunc(lines.join("\n"), 1024);
}

function presenceHasEmbeddedActivity(
  presence: Presence,
  key: string
): boolean {
  return embeddedActivities(presence).some(
    (activity) => embeddedActivityKey(activity) === key
  );
}

async function logLeaderStartup(client: Client): Promise<void> {
  const startedAt = Date.now() - Math.floor(process.uptime() * 1000);

  for (const guild of client.guilds.cache.values()) {
    const settings = await getGuildSettings(guild.id);
    if (!settings.startup_log_enabled || !settings.logging_enabled) continue;

    const voiceValue = settings.startup_voice_channel_id
      ? `🎧 Independent voice HA target: <#${settings.startup_voice_channel_id}>\nThe voice owner may be a different Nifibot node from this Discord gateway leader.`
      : "Disabled / not configured";

    const embed = new EmbedBuilder()
      .setTitle("🟢 Nifibot leader online")
      .addFields(
        { name: "Node", value: `**${config.NODE_ID}**\nHost: \`${os.hostname()}\`` },
        { name: "Process", value: `PID: \`${process.pid}\`\nNode.js: \`${process.version}\`` },
        { name: "Process started", value: discordTimestamp(startedAt) },
        { name: "Discord", value: client.user ? `${client.user.tag} (\`${client.user.id}\`)` : "unknown" },
        { name: "Startup voice", value: voiceValue },
        { name: "History catch-up", value: "Started in the background after gateway readiness." }
      )
      .setTimestamp();

    await sendLog(client, guild.id, "system", embed);
    await recordEvent({
      guildId: guild.id,
      eventType: "system.startup",
      actorId: client.user?.id ?? null,
      data: {
        nodeId: config.NODE_ID,
        hostname: os.hostname(),
        pid: process.pid,
        nodeVersion: process.version,
        processStartedAt: new Date(startedAt).toISOString(),
        startupVoiceChannelId: settings.startup_voice_channel_id,
        voiceHaMode: "independent"
      }
    }).catch((error) => {
      log.warn({ err: error, guildId: guild.id }, "Failed to persist startup event");
    });
  }
}

export function createDiscordClient(): Client {
  const client = new Client({
    intents: [
      GatewayIntentBits.Guilds,
      GatewayIntentBits.GuildMembers,
      GatewayIntentBits.GuildPresences,
      GatewayIntentBits.GuildModeration,
      GatewayIntentBits.GuildVoiceStates,
      GatewayIntentBits.GuildMessages,
      GatewayIntentBits.GuildMessageReactions,
      GatewayIntentBits.MessageContent
    ],
    partials: [
      Partials.Channel,
      Partials.Message,
      Partials.Reaction,
      Partials.User
    ]
  });

  client.once(Events.ClientReady, async (readyClient) => {
    log.info(
      { user: readyClient.user.tag, node: config.NODE_ID },
      "Discord gateway ready"
    );

    await logLeaderStartup(readyClient).catch((error) => {
      log.warn({ err: error }, "Startup Discord log failed");
    });

    // Recover durable state after downtime. Message history is handled first,
    // then Discord Audit Log / current-state reconciliation runs. Transient
    // events that begin and end while every node is offline are fundamentally
    // unavailable from Discord and are called out in the reconciliation card.
    void (async () => {
      await runStartupCatchup(readyClient).catch((error) => {
        log.warn({ err: error }, "Startup message-history catch-up failed");
      });

      await runStartupReconciliation(readyClient).catch((error) => {
        log.warn({ err: error }, "Startup audit/state reconciliation failed");
      });
    })();
  });

  client.on(Events.InteractionCreate, async (interaction) => {
    if (interaction.isAutocomplete()) {
      const command = commandMap.get(interaction.commandName);
      if (!command) {
        await interaction.respond([]).catch(() => undefined);
        return;
      }

      if (!command.autocomplete) {
        await interaction.respond([]).catch(() => undefined);
        return;
      }

      try {
        await command.autocomplete(interaction);
      } catch (error) {
        log.warn(
          { err: error, command: interaction.commandName },
          "Command autocomplete failed"
        );
        await interaction.respond([]).catch(() => undefined);
      }
      return;
    }

    if (!interaction.isChatInputCommand()) return;

    const command = commandMap.get(interaction.commandName);
    if (!command) return;

    try {
      await command.execute(interaction);
    } catch (error) {
      log.error(
        { err: error, command: interaction.commandName },
        "Command failed"
      );

      const payload = {
        content: "❌ That command failed. Check the bot logs.",
        flags: MessageFlags.Ephemeral
      } as const;

      if (interaction.replied || interaction.deferred) {
        await interaction.followUp(payload).catch(() => undefined);
      } else {
        await interaction.reply(payload).catch(() => undefined);
      }
    }
  });

  client.on(Events.MessageCreate, async (message) => {
    if (!message.guildId) return;

    try {
      const settings = await getGuildSettings(message.guildId);
      if (!settings.logging_enabled) return;

      // Log other bots/webhooks, but never feed Nifibot's own output back
      // into the logger. Configured output channels are recursion boundaries.
      if (isSelfAuthoredMessage(client, message)) return;
      if (isConfiguredOutputChannel(settings, message.channelId)) return;

      await cacheMessage(message, settings.message_retention_days);
    } catch (error) {
      log.error({ err: error, messageId: message.id }, "Failed to cache message");
    }
  });

  client.on(Events.MessageUpdate, async (_oldMessage, newMessage) => {
    if (!newMessage.guildId) return;

    try {
      const settings = await getGuildSettings(newMessage.guildId);
      if (!settings.logging_enabled || !settings.log_message_edits) return;

      if (newMessage.partial) {
        await newMessage.fetch().catch(() => undefined);
      }

      if (!newMessage.author) return;
      if (isSelfAuthoredMessage(client, newMessage)) return;
      if (isConfiguredOutputChannel(settings, newMessage.channelId)) return;

      const current = newMessage as Message;
      const old = await getCachedMessage(newMessage.id);

      // Ignored users/bots are still kept in the message cache so history
      // backfill/delete recovery remains complete; only visible message logs
      // are suppressed.
      if (
        isIgnoredMessageUser(settings, current.author.id) ||
        isIgnoredMessageUser(settings, old?.author_id)
      ) {
        await cacheMessage(current, settings.message_retention_days);
        return;
      }

      // If we never observed the original message, cache the current state but
      // don't invent a fake "edit from nothing" event.
      if (!old) {
        await cacheMessage(current, settings.message_retention_days);
        return;
      }

      const isBotLike =
        old.author_is_bot ||
        old.webhook_id !== null ||
        current.author.bot ||
        current.webhookId !== null;

      const placeholderText = (old.content ?? "").trim();
      const oldHasRichPayload =
        (old.embeds?.length ?? 0) > 0 ||
        (old.attachments?.length ?? 0) > 0 ||
        (old.stickers?.length ?? 0) > 0;
      const newHasRichPayload =
        current.embeds.length > 0 ||
        current.attachments.size > 0 ||
        current.stickers.size > 0;
      const looksLikeBotPlaceholder =
        !oldHasRichPayload &&
        (
          placeholderText.length === 0 ||
          /(?:thinking|loading|processing|working|please\s+wait|generating)/i.test(
            placeholderText
          )
        );

      // Some bots (Mimu is a good example) create an empty/thinking shell, then
      // render the first real embed later. Discord exposes that as messageUpdate,
      // but semantically it is usually initial response rendering, not a human edit.
      const isInitialBotRender =
        isBotLike && looksLikeBotPlaceholder && newHasRichPayload;

      const beforeState = meaningfulStateFromCached(old);
      const afterState = meaningfulStateFromMessage(current);

      const before = old.content ?? null;
      const beforeEmbeds = old.embeds ?? [];
      const after = newMessage.content ?? null;
      const afterEmbeds = newMessage.embeds.map((e) => e.toJSON());

      await cacheMessage(current, settings.message_retention_days);

      // Discord can emit messageUpdate for embed hydration/proxy metadata or
      // other non-user-visible changes. Ignore those no-op updates.
      if (JSON.stringify(beforeState) === JSON.stringify(afterState)) {
        return;
      }

      // Bots such as Dyno can also refresh embed bookkeeping (timestamps,
      // colour/proxy/provider metadata, etc.) while the actual rendered
      // message text/fields/links stay the same. For bot/webhook messages,
      // compare a stricter user-visible semantic shape before emitting an
      // "edited" card. This keeps genuine text/field/image/link changes.
      if (isBotLike) {
        const beforeBotState = botMeaningfulStateFromCached(old);
        const afterBotState = botMeaningfulStateFromMessage(current);
        if (JSON.stringify(beforeBotState) === JSON.stringify(afterBotState)) {
          return;
        }
      }

      const embed = new EmbedBuilder()
        .setTitle(isInitialBotRender ? "🤖 Bot response rendered" : "✏️ Message edited")
        .addFields(
          {
            name: "Author",
            value: authorLabel(
              newMessage.author.id,
              newMessage.author.tag,
              newMessage.author.bot
            )
          },
          {
            name: "Channel",
            value: `<#${newMessage.channelId}>
[Open channel](${discordChannelUrl(
              newMessage.guildId,
              newMessage.channelId
            )})`
          },
          {
            name: "Sent",
            value: discordTimestamp(old.message_created_at)
          },
          {
            name: "Jump",
            value: `[Open message](${discordMessageUrl(
              newMessage.guildId,
              newMessage.channelId,
              newMessage.id
            )})`
          }
        )
        .setFooter({ text: `Message ID: ${newMessage.id}` })
        .setTimestamp();

      if ((before ?? "").trim() || (after ?? "").trim()) {
        embed.addFields(
          { name: "Text before", value: trunc(before) },
          { name: "Text after", value: trunc(after) }
        );
      }

      if (isInitialBotRender) {
        embed.addFields({
          name: "Note",
          value: "This looks like a bot's initial placeholder/thinking response being replaced by its first real payload. Discord reports that transition as a message edit."
        });
      }

      const beforeEmbedSummary = summarizeEmbeds(beforeEmbeds);
      const afterEmbedSummary = summarizeEmbeds(afterEmbeds);

      if (beforeEmbedSummary || afterEmbedSummary) {
        embed.addFields(
          { name: "Embeds before", value: beforeEmbedSummary ?? "*None*" },
          { name: "Embeds after", value: afterEmbedSummary ?? "*None*" }
        );
      }

      await sendLog(client, newMessage.guildId, "messages", embed);

      await recordEvent({
        guildId: newMessage.guildId,
        eventType: isInitialBotRender ? "message.bot_render" : "message.edit",
        actorId: newMessage.author.id,
        targetId: newMessage.author.id,
        channelId: newMessage.channelId,
        messageId: newMessage.id,
        data: {
          before,
          after,
          beforeEmbeds,
          afterEmbeds,
          authorIsBot: newMessage.author.bot,
          initialBotRender: isInitialBotRender
        }
      });
    } catch (error) {
      log.error({ err: error, messageId: newMessage.id }, "Message edit logger failed");
    }
  });

  client.on(Events.MessageDelete, async (message) => {
    if (!message.guildId) return;

    try {
      const settings = await getGuildSettings(message.guildId);
      if (!settings.logging_enabled || !settings.log_message_deletes) return;
      if (isConfiguredOutputChannel(settings, message.channelId)) return;
      if (isSelfAuthoredMessage(client, message)) return;

      let cached = await getCachedMessage(message.id);

      // If this process has a complete Discord.js message object but the DB
      // missed the original create (e.g. logging was enabled later), salvage
      // whatever Discord still gave us before declaring it uncached.
      if (!cached && !message.partial && message.author) {
        await cacheMessage(message as Message, settings.message_retention_days);
        cached = await getCachedMessage(message.id);
      }

      if (client.user && cached?.author_id === client.user.id) return;

      const deletedAuthorId = cached?.author_id ?? message.author?.id ?? null;
      if (isIgnoredMessageUser(settings, deletedAuthorId)) {
        await markMessageDeleted(message.id);
        return;
      }

      let deletedBy = null;
      if (cached) {
        deletedBy = await findRecentAuditActor(
          message.guild!,
          AuditLogEvent.MessageDelete,
          cached.author_id,
          10_000
        );
      }

      const embed = new EmbedBuilder()
        .setTitle("🗑️ Message deleted")
        .setFooter({ text: `Message ID: ${message.id}` })
        .setTimestamp();

      const deletionObservedAt = new Date();
      addCachedMessageFields(
        embed,
        cached,
        message.guildId,
        message.channelId,
        message.id,
        deletionObservedAt
      );

      embed.addFields({
        name: "Deleted / observed",
        value: discordTimestamp(deletionObservedAt)
      });

      if (deletedBy) {
        embed.addFields({
          name: "Deleted by (audit-log correlation)",
          value: `<@${deletedBy.id}> (\`${deletedBy.id}\`)${deletedBy.reason ? `\nReason: ${trunc(deletedBy.reason, 500)}` : ""}`
        });
      }

      await sendLog(client, message.guildId, "messages", embed);

      await recordEvent({
        guildId: message.guildId,
        eventType: "message.delete",
        actorId: deletedBy?.id ?? null,
        targetId: cached?.author_id ?? null,
        channelId: message.channelId,
        messageId: message.id,
        data: { cached, auditActor: deletedBy }
      });

      await markMessageDeleted(message.id);
    } catch (error) {
      log.error({ err: error, messageId: message.id }, "Message delete logger failed");
    }
  });

  client.on(Events.MessageBulkDelete, async (messages) => {
    const first = messages.first();
    if (!first?.guildId || !first.guild) return;

    try {
      const settings = await getGuildSettings(first.guildId);
      if (!settings.logging_enabled) return;
      if (isConfiguredOutputChannel(settings, first.channelId)) return;

      const details: Array<{
        id: string;
        authorId: string | null;
        content: string | null;
      }> = [];

      for (const message of messages.values()) {
        const cached = await getCachedMessage(message.id);
        const authorId = cached?.author_id ?? message.author?.id ?? null;
        await markMessageDeleted(message.id);

        if (client.user && authorId === client.user.id) continue;
        if (isIgnoredMessageUser(settings, authorId)) continue;

        details.push({
          id: message.id,
          authorId,
          content: cached?.content ?? null
        });
      }

      if (!details.length) return;

      const audit = await findRecentAuditActor(
        first.guild,
        AuditLogEvent.MessageBulkDelete,
        null,
        15_000
      );

      const preview = details
        .slice(0, 10)
        .map(
          (item) =>
            `• \`${item.id}\`${item.authorId ? ` <@${item.authorId}>` : ""}: ${trunc(item.content, 160)}`
        )
        .join("\n");

      const embed = new EmbedBuilder()
        .setTitle("🧹 Messages bulk deleted")
        .addFields(
          { name: "Count", value: String(messages.size) },
          { name: "Channel", value: `<#${first.channelId}>` },
          { name: "Preview", value: trunc(preview || "*No cached message bodies*") }
        )
        .setTimestamp();

      if (audit) {
        embed.addFields({
          name: "Deleted by",
          value: `<@${audit.id}> (\`${audit.id}\`)`
        });
      }

      await sendLog(client, first.guildId, "messages", embed);

      await recordEvent({
        guildId: first.guildId,
        eventType: "message.bulk_delete",
        actorId: audit?.id ?? null,
        channelId: first.channelId,
        data: { count: messages.size, messages: details, auditActor: audit }
      });
    } catch (error) {
      log.error({ err: error }, "Bulk-delete logger failed");
    }
  });

  const onReaction = async (
    eventType: "reaction.add" | "reaction.remove",
    rawReaction: ReactionInput,
    rawUser: UserInput
  ) => {
    try {
      const { reaction, user } = await hydrateReaction(rawReaction, rawUser);
      const message = reaction.message;
      if (!message.guildId || !message.guild) return;

      const settings = await getGuildSettings(message.guildId);
      if (!settings.logging_enabled) return;
      if (isConfiguredOutputChannel(settings, message.channelId)) return;
      if (client.user && user.id === client.user.id) return;
      if (isSelfAuthoredMessage(client, message)) return;
      if (isIgnoredMessageUser(settings, user.id)) return;

      const cachedForIgnore = await getCachedMessage(message.id);
      const messageAuthorId =
        message.author?.id ?? cachedForIgnore?.author_id ?? null;
      if (isIgnoredMessageUser(settings, messageAuthorId)) return;

      if (message.author && !cachedForIgnore) {
        await cacheMessage(message as Message, settings.message_retention_days);
      }

      const emoji = reaction.emoji.toString();
      const verb = eventType === "reaction.add" ? "Reaction added" : "Reaction removed";

      const embed = new EmbedBuilder()
        .setTitle(eventType === "reaction.add" ? "➕ Reaction added" : "➖ Reaction removed")
        .addFields(
          {
            name: "User",
            value: authorLabel(user.id, user.tag, user.bot)
          },
          { name: "Reaction", value: emoji || reaction.emoji.name || "*unknown*" },
          { name: "Channel", value: `<#${message.channelId}>` },
          {
            name: "Message author",
            value: message.author
              ? authorLabel(message.author.id, message.author.tag, message.author.bot)
              : "*Unknown*"
          },
          { name: "Message", value: trunc(message.content) },
          { name: "Sent", value: discordTimestamp(message.createdAt) },
          { name: "Jump", value: `[Open message](${message.url})` }
        )
        .setFooter({ text: `Message ID: ${message.id}` })
        .setTimestamp();

      const embedSummary = summarizeEmbeds(
        message.embeds.map((e) => e.toJSON())
      );
      if (embedSummary) {
        embed.addFields({ name: "Message embeds", value: embedSummary });
      }

      await sendLog(client, message.guildId, "messages", embed);

      await recordEvent({
        guildId: message.guildId,
        eventType,
        actorId: user.id,
        targetId: message.author?.id ?? null,
        channelId: message.channelId,
        messageId: message.id,
        data: {
          emoji,
          messageContent: message.content,
          messageAuthorId: message.author?.id ?? null,
          actorIsBot: user.bot
        }
      });
    } catch (error) {
      log.error({ err: error, eventType }, "Reaction logger failed");
    }
  };

  client.on(Events.MessageReactionAdd, (reaction, user) => {
    void onReaction("reaction.add", reaction, user);
  });

  client.on(Events.MessageReactionRemove, (reaction, user) => {
    void onReaction("reaction.remove", reaction, user);
  });

  client.on(Events.GuildMemberAdd, async (member) => {
    try {
      const settings = await getGuildSettings(member.guild.id);
      if (!settings.logging_enabled) return;

      const embed = new EmbedBuilder()
        .setTitle("👋 Member joined")
        .addFields(
          {
            name: "Member",
            value: authorLabel(member.id, member.user.tag, member.user.bot)
          },
          {
            name: "Account created",
            value: discordTimestamp(member.user.createdAt)
          },
          {
            name: "Joined server",
            value: member.joinedAt
              ? discordTimestamp(member.joinedAt)
              : discordTimestamp(Date.now())
          },
          {
            name: "Member count",
            value: String(member.guild.memberCount)
          }
        )
        .setTimestamp();

      await sendLog(client, member.guild.id, "members", embed);
      await recordEvent({
        guildId: member.guild.id,
        eventType: "member.join",
        actorId: member.id,
        targetId: member.id,
        data: { tag: member.user.tag, bot: member.user.bot }
      });
    } catch (error) {
      log.error({ err: error }, "Member-join logger failed");
    }
  });

  client.on(Events.GuildMemberRemove, async (member) => {
    try {
      const settings = await getGuildSettings(member.guild.id);
      if (!settings.logging_enabled) return;

      // Audit-log entries can lag the gateway event slightly.
      await sleep(500);
      const kick = await findRecentAuditActor(
        member.guild,
        AuditLogEvent.MemberKick,
        member.id,
        12_000
      );

      const joinedAt = member.joinedAt;
      const title = kick ? "🥾 Member kicked" : "👋 Member left";
      const embed = new EmbedBuilder()
        .setTitle(title)
        .addFields(
          {
            name: "Member",
            value: authorLabel(member.id, member.user.tag, member.user.bot)
          },
          {
            name: "Account created",
            value: discordTimestamp(member.user.createdAt)
          }
        )
        .setTimestamp();

      if (joinedAt) {
        embed.addFields({
          name: "Was in server since",
          value: `${discordTimestamp(joinedAt)}\n**Membership duration:** ${durationLabel(
            joinedAt,
            Date.now()
          )}`
        });
      }

      const roles = member.roles.cache
        .filter((role) => role.id !== member.guild.id)
        .sort((a, b) => b.position - a.position)
        .map((role) => `<@&${role.id}>`);

      if (roles.length) {
        embed.addFields({
          name: "Roles at departure",
          value: trunc(roles.join(", "), 1000)
        });
      }

      if (kick) {
        embed.addFields({
          name: "Kicked by",
          value: `<@${kick.id}> (\`${kick.id}\`)${kick.reason ? `\nReason: ${trunc(
            kick.reason,
            500
          )}` : ""}\nAudit entry: \`${kick.entryId}\``
        });
      }

      await sendLog(
        client,
        member.guild.id,
        kick ? "moderation" : "members",
        embed
      );
      await recordEvent({
        guildId: member.guild.id,
        eventType: kick ? "member.kick" : "member.leave",
        actorId: kick?.id ?? member.id,
        targetId: member.id,
        data: {
          tag: member.user.tag,
          bot: member.user.bot,
          joinedAt: joinedAt?.toISOString() ?? null,
          roles: roles,
          auditActor: kick
        }
      });
    } catch (error) {
      log.error({ err: error }, "Member-leave/kick logger failed");
    }
  });

  client.on(Events.GuildMemberUpdate, async (oldMember, newMember) => {
    try {
      const settings = await getGuildSettings(newMember.guild.id);
      if (!settings.logging_enabled) return;

      if (oldMember.nickname !== newMember.nickname) {
        const audit = await findRecentAuditActor(
          newMember.guild,
          AuditLogEvent.MemberUpdate,
          newMember.id
        );

        const embed = new EmbedBuilder()
          .setTitle("📝 Nickname changed")
          .addFields(
            {
              name: "Member",
              value: authorLabel(
                newMember.id,
                newMember.user.tag,
                newMember.user.bot
              )
            },
            { name: "Before", value: oldMember.nickname ?? "*No nickname*" },
            { name: "After", value: newMember.nickname ?? "*No nickname*" },
            {
              name: "Changed by",
              value: audit
                ? `<@${audit.id}> (\`${audit.id}\`)${audit.reason ? `
Reason: ${trunc(audit.reason, 500)}` : ""}
Audit entry: \`${audit.entryId}\``
                : "*Unknown / no matching audit entry*"
            }
          )
          .setTimestamp();

        await sendLog(client, newMember.guild.id, "members", embed);

        await recordEvent({
          guildId: newMember.guild.id,
          eventType: "member.nickname",
          actorId: audit?.id ?? null,
          targetId: newMember.id,
          data: {
            before: oldMember.nickname,
            after: newMember.nickname,
            auditActor: audit
          }
        });
      }


      // Administrative timeout add/change/remove events are logged from
      // GuildAuditLogEntryCreate below. That path does not depend on the member
      // already being present in discord.js' member cache.
      //
      // Keep GuildMemberUpdate only for a *natural timeout expiry*, because a
      // timer expiring by itself does not create a new moderator audit entry.
      if (
        oldMember.communicationDisabledUntilTimestamp !==
          newMember.communicationDisabledUntilTimestamp &&
        oldMember.communicationDisabledUntilTimestamp &&
        !newMember.communicationDisabledUntilTimestamp &&
        oldMember.communicationDisabledUntilTimestamp <= Date.now() + 5_000
      ) {
        const expiredAt = oldMember.communicationDisabledUntilTimestamp;

        const embed = new EmbedBuilder()
          .setTitle("⌛ Timeout expired")
          .addFields(
            {
              name: "Member",
              value: authorLabel(
                newMember.id,
                newMember.user.tag,
                newMember.user.bot
              )
            },
            {
              name: "Timeout expired",
              value: discordTimestamp(expiredAt)
            },
            {
              name: "Source",
              value:
                "The timeout duration elapsed naturally; no moderator audit entry is expected."
            }
          )
          .setTimestamp();

        await sendLog(client, newMember.guild.id, "moderation", embed);
        await recordEvent({
          guildId: newMember.guild.id,
          eventType: "member.timeout_expired",
          actorId: null,
          targetId: newMember.id,
          data: { expiredAt }
        });
      }

      const oldRoles = new Set(oldMember.roles.cache.keys());
      const newRoles = new Set(newMember.roles.cache.keys());

      const added = [...newRoles].filter(
        (id) => !oldRoles.has(id) && id !== newMember.guild.id
      );
      const removed = [...oldRoles].filter(
        (id) => !newRoles.has(id) && id !== newMember.guild.id
      );

      if (added.length || removed.length) {
        const audit = await findRecentAuditActor(
          newMember.guild,
          AuditLogEvent.MemberRoleUpdate,
          newMember.id
        );

        const embed = new EmbedBuilder()
          .setTitle("🎭 Member roles changed")
          .addFields({
            name: "Member",
            value: authorLabel(
              newMember.id,
              newMember.user.tag,
              newMember.user.bot
            )
          })
          .setTimestamp();

        if (added.length) {
          embed.addFields({
            name: "Added",
            value: trunc(added.map((id) => `<@&${id}>`).join("\n"))
          });
        }

        if (removed.length) {
          embed.addFields({
            name: "Removed",
            value: trunc(removed.map((id) => `<@&${id}>`).join("\n"))
          });
        }

        embed.addFields({
          name: "Changed by",
          value: audit
            ? `<@${audit.id}> (\`${audit.id}\`)${audit.reason ? `
Reason: ${trunc(audit.reason, 500)}` : ""}
Audit entry: \`${audit.entryId}\``
            : "*Unknown / no matching audit entry*"
        });

        await sendLog(client, newMember.guild.id, "members", embed);

        await recordEvent({
          guildId: newMember.guild.id,
          eventType: "member.roles",
          actorId: audit?.id ?? null,
          targetId: newMember.id,
          data: { added, removed, auditActor: audit }
        });
      }
    } catch (error) {
      log.error({ err: error }, "Member-update logger failed");
    }
  });



  // Use the audit-log gateway event as the authoritative source for
  // moderator-applied timeout changes. This works even when the target member
  // was not previously present in discord.js' cache (for example, an account
  // that has never sent a message).
  client.on(Events.GuildAuditLogEntryCreate, async (entry, guild) => {
    try {
      // Claim every audit entry even if another richer Gateway logger handles
      // its live presentation. This gives startup reconciliation a durable
      // cursor and prevents replay duplicates after failover/restart.
      const claimed = await claimAuditEntry(guild.id, entry, "live");
      if (!claimed) return;

      if (!isTimeoutAuditEntry(entry)) {
        if (shouldLogGenericLiveAudit(Number(entry.action))) {
          await logGenericAuditEntry(client, guild, entry, "live");
        }
        return;
      }

      const timeoutChange = entry.changes.find(
        (change) => change.key === "communication_disabled_until"
      );
      if (!timeoutChange) return;

      const settings = await getGuildSettings(guild.id);
      if (!settings.logging_enabled) return;

      const targetId = entry.targetId;
      if (!targetId) return;

      const parseAuditTimestamp = (value: unknown): number | null => {
        if (typeof value !== "string") return null;
        const parsed = Date.parse(value);
        return Number.isFinite(parsed) ? parsed : null;
      };

      const before = parseAuditTimestamp(timeoutChange.old);
      const after = parseAuditTimestamp(timeoutChange.new);
      const now = Date.now();

      const targetMember = await guild.members.fetch(targetId).catch(() => null);
      const targetUser =
        targetMember?.user ??
        (await client.users.fetch(targetId).catch(() => null));

      const executorId = entry.executorId;
      const executorUser = executorId
        ? await client.users.fetch(executorId).catch(() => entry.executor ?? null)
        : entry.executor ?? null;

      // The real-time audit-log gateway payload can occasionally omit `reason`
      // even though Discord's Audit Log UI shows one. Re-fetch the authoritative
      // audit entry by ID before rendering the timeout log.
      let timeoutReason = entry.reason ?? null;
      if (!timeoutReason) {
        await sleep(350);
        const fetchedAudit = await guild
          .fetchAuditLogs({
            type: AuditLogEvent.MemberUpdate,
            limit: 6
          })
          .catch(() => null);

        const authoritativeEntry =
          fetchedAudit?.entries.get(entry.id) ??
          fetchedAudit?.entries.find(
            (candidate) =>
              candidate.targetId === targetId &&
              Math.abs(candidate.createdTimestamp - entry.createdTimestamp) <= 5_000 &&
              candidate.changes.some(
                (change) => change.key === "communication_disabled_until"
              )
          );

        timeoutReason = authoritativeEntry?.reason ?? null;
      }

      const title =
        after && after > now
          ? before && before > now
            ? "⏱️ Timeout changed"
            : "⏱️ Member timed out"
          : "✅ Timeout removed";

      const embed = new EmbedBuilder()
        .setTitle(title)
        .addFields(
          {
            name: "Member",
            value: authorLabel(
              targetId,
              targetUser?.tag ?? targetUser?.username ?? null,
              targetUser?.bot ?? false
            )
          },
          {
            name: "Changed by",
            value: executorId
              ? `${authorLabel(
                  executorId,
                  executorUser?.tag ?? executorUser?.username ?? null,
                  executorUser?.bot ?? false
                )}`
              : "*Unknown executor*"
          },
          {
            name: "Audit entry",
            value: `\`${entry.id}\`\n${discordTimestamp(entry.createdTimestamp)}`
          },
          {
            name: "Reason",
            value: timeoutReason ? trunc(timeoutReason, 1024) : "*No reason provided*"
          }
        )
        .setTimestamp();

      if (before) {
        embed.addFields({
          name: "Previous timeout until",
          value: discordTimestamp(before)
        });
      }

      if (after && after > now) {
        embed.addFields({
          name: "Timeout until",
          value: `${discordTimestamp(after)}\n**Duration from audit action:** ${durationLabel(
            entry.createdTimestamp,
            after
          )}`
        });
      } else {
        embed.addFields({
          name: "Timeout state",
          value: "No active timeout after this change."
        });
      }

      embed.addFields({
        name: "Detection",
        value:
          "Discord Audit Log gateway event (`MEMBER_UPDATE` → `communication_disabled_until`)."
      });

      await sendLog(client, guild.id, "moderation", embed);
      await recordEvent({
        guildId: guild.id,
        eventType:
          after && after > now
            ? before && before > now
              ? "member.timeout_change"
              : "member.timeout_add"
            : "member.timeout_remove",
        actorId: executorId ?? null,
        targetId,
        data: {
          before,
          after,
          reason: timeoutReason,
          auditEntryId: entry.id
        }
      });
    } catch (error) {
      log.error({ err: error }, "Audit-log timeout logger failed");
    }
  });

  client.on(Events.GuildBanAdd, async (ban) => {
    try {
      const settings = await getGuildSettings(ban.guild.id);
      if (!settings.logging_enabled) return;

      await sleep(500);
      const audit = await findRecentAuditActor(
        ban.guild,
        AuditLogEvent.MemberBanAdd,
        ban.user.id,
        15_000
      );

      const embed = new EmbedBuilder()
        .setTitle("🔨 Member banned")
        .addFields(
          {
            name: "Member",
            value: authorLabel(ban.user.id, ban.user.tag, ban.user.bot)
          },
          {
            name: "Account created",
            value: discordTimestamp(ban.user.createdAt)
          },
          {
            name: "Banned by",
            value: audit
              ? `<@${audit.id}> (\`${audit.id}\`)${audit.reason ? `\nReason: ${trunc(
                  audit.reason,
                  500
                )}` : ""}\nAudit entry: \`${audit.entryId}\``
              : "*Unknown / no matching audit entry*"
          }
        )
        .setTimestamp();

      await sendLog(client, ban.guild.id, "moderation", embed);
      await recordEvent({
        guildId: ban.guild.id,
        eventType: "member.ban",
        actorId: audit?.id ?? null,
        targetId: ban.user.id,
        data: {
          tag: ban.user.tag,
          bot: ban.user.bot,
          auditActor: audit
        }
      });
    } catch (error) {
      log.error({ err: error }, "Ban logger failed");
    }
  });

  client.on(Events.GuildBanRemove, async (ban) => {
    try {
      const settings = await getGuildSettings(ban.guild.id);
      if (!settings.logging_enabled) return;

      await sleep(500);
      const audit = await findRecentAuditActor(
        ban.guild,
        AuditLogEvent.MemberBanRemove,
        ban.user.id,
        15_000
      );

      const embed = new EmbedBuilder()
        .setTitle("🔓 Member unbanned")
        .addFields(
          {
            name: "Member",
            value: authorLabel(ban.user.id, ban.user.tag, ban.user.bot)
          },
          {
            name: "Unbanned by",
            value: audit
              ? `<@${audit.id}> (\`${audit.id}\`)${audit.reason ? `\nReason: ${trunc(
                  audit.reason,
                  500
                )}` : ""}\nAudit entry: \`${audit.entryId}\``
              : "*Unknown / no matching audit entry*"
          }
        )
        .setTimestamp();

      await sendLog(client, ban.guild.id, "moderation", embed);
      await recordEvent({
        guildId: ban.guild.id,
        eventType: "member.unban",
        actorId: audit?.id ?? null,
        targetId: ban.user.id,
        data: {
          tag: ban.user.tag,
          bot: ban.user.bot,
          auditActor: audit
        }
      });
    } catch (error) {
      log.error({ err: error }, "Unban logger failed");
    }
  });


  // Discord "Activities" such as Watch Together / games are not part of
  // VOICE_STATE_UPDATE. They are advertised through guild presence activities
  // with Discord's EMBEDDED activity flag, so this requires GuildPresences.
  client.on(Events.PresenceUpdate, async (oldPresence, newPresence) => {
    try {
      const guild = newPresence.guild ?? oldPresence?.guild;
      if (!guild) return;

      const settings = await getGuildSettings(guild.id);
      if (!settings.logging_enabled) return;

      // Avoid treating the initial presence snapshot as a user action when this
      // node first comes online.
      if (!oldPresence) return;

      const userId = newPresence.userId;
      if (client.user?.id === userId) return;

      const before = new Map(
        embeddedActivities(oldPresence).map((activity) => [
          embeddedActivityKey(activity),
          activity
        ])
      );
      const after = new Map(
        embeddedActivities(newPresence).map((activity) => [
          embeddedActivityKey(activity),
          activity
        ])
      );

      const started = [...after.entries()].filter(([key]) => !before.has(key));
      const stopped = [...before.entries()].filter(([key]) => !after.has(key));
      if (!started.length && !stopped.length) return;

      const member =
        newPresence.member ??
        oldPresence.member ??
        (await guild.members.fetch(userId).catch(() => null));
      const user = member?.user ?? newPresence.user ?? oldPresence.user;
      const voiceChannelId =
        member?.voice.channelId ??
        guild.voiceStates.cache.get(userId)?.channelId ??
        null;

      const countOtherParticipants = (key: string): number =>
        guild.presences.cache.filter((presence) => {
          if (presence.userId === userId) return false;

          const otherVoiceChannel =
            presence.member?.voice.channelId ??
            guild.voiceStates.cache.get(presence.userId)?.channelId ??
            null;

          if (voiceChannelId && otherVoiceChannel !== voiceChannelId) {
            return false;
          }

          return presenceHasEmbeddedActivity(presence, key);
        }).size;

      for (const [key, activity] of started) {
        const otherParticipants = countOtherParticipants(key);
        const isLikelyFirstParticipant = otherParticipants === 0;

        const embed = new EmbedBuilder()
          .setTitle(
            isLikelyFirstParticipant
              ? "🎮 Discord Activity started"
              : "🎮 Joined Discord Activity"
          )
          .addFields(
            {
              name: "Member",
              value: authorLabel(
                userId,
                user?.tag ?? user?.username ?? null,
                user?.bot ?? false
              )
            },
            {
              name: "Activity",
              value: embeddedActivitySummary(activity)
            },
            {
              name: "Voice channel",
              value: voiceChannelId
                ? `<#${voiceChannelId}>\n[Open channel](${discordChannelUrl(
                    guild.id,
                    voiceChannelId
                  )})`
                : "*No voice channel could be resolved*"
            },
            {
              name: "Participants observed",
              value: String(otherParticipants + 1)
            },
            {
              name: "Detection",
              value:
                "Discord guild presence activity with the `EMBEDDED` activity flag."
            }
          )
          .setTimestamp();

        await sendLog(client, guild.id, "voice", embed);
        await recordEvent({
          guildId: guild.id,
          eventType: isLikelyFirstParticipant
            ? "voice.activity_start"
            : "voice.activity_join",
          actorId: userId,
          targetId: null,
          channelId: voiceChannelId,
          data: {
            activityName: activity.name,
            applicationId: activity.applicationId ?? null,
            partyId: activity.party?.id ?? null,
            details: activity.details ?? null,
            state: activity.state ?? null,
            participantsObserved: otherParticipants + 1
          }
        });
      }

      for (const [key, activity] of stopped) {
        const otherParticipants = countOtherParticipants(key);
        const likelyEnded = otherParticipants === 0;

        const embed = new EmbedBuilder()
          .setTitle(
            likelyEnded
              ? "🛑 Discord Activity ended"
              : "🚪 Left Discord Activity"
          )
          .addFields(
            {
              name: "Member",
              value: authorLabel(
                userId,
                user?.tag ?? user?.username ?? null,
                user?.bot ?? false
              )
            },
            {
              name: "Activity",
              value: embeddedActivitySummary(activity)
            },
            {
              name: "Voice channel",
              value: voiceChannelId
                ? `<#${voiceChannelId}>\n[Open channel](${discordChannelUrl(
                    guild.id,
                    voiceChannelId
                  )})`
                : "*No voice channel could be resolved (the member may already have left VC).*"
            },
            {
              name: "Participants remaining",
              value: String(otherParticipants)
            },
            {
              name: "Detection",
              value:
                "Discord guild presence activity with the `EMBEDDED` activity flag."
            }
          )
          .setTimestamp();

        await sendLog(client, guild.id, "voice", embed);
        await recordEvent({
          guildId: guild.id,
          eventType: likelyEnded
            ? "voice.activity_end"
            : "voice.activity_leave",
          actorId: userId,
          targetId: null,
          channelId: voiceChannelId,
          data: {
            activityName: activity.name,
            applicationId: activity.applicationId ?? null,
            partyId: activity.party?.id ?? null,
            details: activity.details ?? null,
            state: activity.state ?? null,
            participantsRemaining: otherParticipants
          }
        });
      }
    } catch (error) {
      log.error({ err: error }, "Embedded-activity logger failed");
    }
  });

  client.on(Events.VoiceStateUpdate, async (oldState, newState) => {
    try {
      const guild = newState.guild;
      const settings = await getGuildSettings(guild.id);
      if (!settings.logging_enabled) return;

      const member = newState.member ?? oldState.member;
      if (!member) return;

      const changes: string[] = [];
      let title = "🔊 Voice state changed";
      let auditActor: AuditActor = null;

      const isFreshJoin = !oldState.channelId && Boolean(newState.channelId);

      if (oldState.channelId !== newState.channelId) {
        if (!oldState.channelId && newState.channelId) {
          title = "🔊 Joined voice channel";
          changes.push(`**Joined:** <#${newState.channelId}>`);

          // A join event is also a useful state snapshot. Show every voice
          // flag even when Discord reports the same value on both sides, so
          // the log records the exact state the member entered with.
          changes.push(
            `**Server mute:** ${oldState.serverMute ? "on" : "off"} → ${
              newState.serverMute ? "on" : "off"
            }`,
            `**Server deaf:** ${oldState.serverDeaf ? "on" : "off"} → ${
              newState.serverDeaf ? "on" : "off"
            }`,
            `**Self mute:** ${oldState.selfMute ? "on" : "off"} → ${
              newState.selfMute ? "on" : "off"
            }`,
            `**Self deaf:** ${oldState.selfDeaf ? "on" : "off"} → ${
              newState.selfDeaf ? "on" : "off"
            }`,
            `**Streaming:** ${oldState.streaming ? "on" : "off"} → ${
              newState.streaming ? "on" : "off"
            }`,
            `**Camera:** ${oldState.selfVideo ? "on" : "off"} → ${
              newState.selfVideo ? "on" : "off"
            }`
          );
        } else if (oldState.channelId && !newState.channelId) {
          title = "🔇 Left voice channel";
          changes.push(`**Left:** <#${oldState.channelId}>`);
        } else if (oldState.channelId && newState.channelId) {
          title = "➡️ Moved voice channel";
          changes.push(
            `**From:** <#${oldState.channelId}>`,
            `**To:** <#${newState.channelId}>`
          );

          // MemberMove audit entries do not always expose the moved member as
          // their target, so this is deliberately labelled best-effort.
          auditActor = await findRecentAuditActor(
            guild,
            AuditLogEvent.MemberMove,
            null,
            5_000
          );
        }
      }

      if (!isFreshJoin && oldState.serverMute !== newState.serverMute) {
        changes.push(
          `**Server mute:** ${oldState.serverMute ? "on" : "off"} → ${
            newState.serverMute ? "on" : "off"
          }`
        );
        auditActor ??= await findRecentAuditActor(
          guild,
          AuditLogEvent.MemberUpdate,
          member.id,
          10_000
        );
      }

      if (!isFreshJoin && oldState.serverDeaf !== newState.serverDeaf) {
        changes.push(
          `**Server deaf:** ${oldState.serverDeaf ? "on" : "off"} → ${
            newState.serverDeaf ? "on" : "off"
          }`
        );
        auditActor ??= await findRecentAuditActor(
          guild,
          AuditLogEvent.MemberUpdate,
          member.id,
          10_000
        );
      }

      if (!isFreshJoin && oldState.selfMute !== newState.selfMute) {
        changes.push(
          `**Self mute:** ${oldState.selfMute ? "on" : "off"} → ${
            newState.selfMute ? "on" : "off"
          }`
        );
      }

      if (!isFreshJoin && oldState.selfDeaf !== newState.selfDeaf) {
        changes.push(
          `**Self deaf:** ${oldState.selfDeaf ? "on" : "off"} → ${
            newState.selfDeaf ? "on" : "off"
          }`
        );
      }

      if (!isFreshJoin && oldState.streaming !== newState.streaming) {
        changes.push(
          `**Streaming:** ${oldState.streaming ? "on" : "off"} → ${
            newState.streaming ? "on" : "off"
          }`
        );
      }

      if (!isFreshJoin && oldState.selfVideo !== newState.selfVideo) {
        changes.push(
          `**Camera:** ${oldState.selfVideo ? "on" : "off"} → ${
            newState.selfVideo ? "on" : "off"
          }`
        );
      }

      if (!changes.length) return;

      const embed = new EmbedBuilder()
        .setTitle(title)
        .addFields(
          {
            name: "Member",
            value: authorLabel(member.id, member.user.tag, member.user.bot)
          },
          {
            name: "Changes",
            value: trunc(changes.join("\n"), 1000)
          }
        )
        .setTimestamp();

      if (auditActor) {
        embed.addFields({
          name: "Changed / moved by (best-effort audit correlation)",
          value: `<@${auditActor.id}> (\`${auditActor.id}\`)\nAudit entry: \`${auditActor.entryId}\``
        });
      }

      await sendLog(client, guild.id, "voice", embed);
      await recordEvent({
        guildId: guild.id,
        eventType: "voice.state",
        actorId: auditActor?.id ?? member.id,
        targetId: member.id,
        channelId: newState.channelId ?? oldState.channelId,
        data: {
          oldChannelId: oldState.channelId,
          newChannelId: newState.channelId,
          oldServerMute: oldState.serverMute,
          newServerMute: newState.serverMute,
          oldServerDeaf: oldState.serverDeaf,
          newServerDeaf: newState.serverDeaf,
          oldSelfMute: oldState.selfMute,
          newSelfMute: newState.selfMute,
          oldSelfDeaf: oldState.selfDeaf,
          newSelfDeaf: newState.selfDeaf,
          oldStreaming: oldState.streaming,
          newStreaming: newState.streaming,
          oldSelfVideo: oldState.selfVideo,
          newSelfVideo: newState.selfVideo,
          auditActor
        }
      });
    } catch (error) {
      log.error({ err: error }, "Voice-state logger failed");
    }
  });

  const pruneTimer = setInterval(async () => {
    try {
      const count = await pruneExpiredMessages();
      if (count > 0) {
        log.info({ count }, "Pruned expired message cache rows");
      }
    } catch (error) {
      log.warn({ err: error }, "Message-cache pruning failed");
    }
  }, 60 * 60 * 1000);

  pruneTimer.unref();

  return client;
}
