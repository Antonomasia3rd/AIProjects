import {
  ChannelType,
  type Client,
  type Guild,
  type Message
} from "discord.js";
import fs from "node:fs";
import fsp from "node:fs/promises";
import path from "node:path";
import readline from "node:readline";
import { createGzip, createGunzip } from "node:zlib";
import { once } from "node:events";
import { config } from "./config.js";
import { getGuildSettings, type GuildSettings } from "./guildSettings.js";
import {
  cacheMessage,
  getMessageCacheStorageBytes,
  getNewestCachedMessageCursor,
  serializeMessageRecord,
  upsertSerializedMessages,
  type SerializedMessageRecord
} from "./messageCache.js";
import { log } from "./logger.js";
import { botCanReadMessageHistory, compactDiscordError } from "./visibility.js";

type FetchableMessageChannel = {
  id: string;
  name?: string;
  type?: ChannelType;
  messages: {
    fetch(options: {
      limit?: number;
      before?: string;
      after?: string;
      cache?: boolean;
    }): Promise<{
      size: number;
      values(): IterableIterator<Message>;
    }>;
  };
};

export type HistoryScope = "channel" | "server";
export type HistoryJobKind = "scan" | "import";

export type HistoryScanOptions = {
  scope: HistoryScope;
  channelId?: string | null;
  days: number;
  maxPerChannel: number;
};

export type HistoryJobSnapshot = {
  guildId: string;
  kind: HistoryJobKind;
  scope: HistoryScope;
  channelId: string | null;
  days: number;
  maxPerChannel: number;
  status: "running" | "done" | "failed" | "cancelled";
  startedAt: Date;
  finishedAt: Date | null;
  channelsDone: number;
  channelsTotal: number;
  messagesProcessed: number;
  currentChannelId: string | null;
  rawBytes: number;
  compressedBytes: number;
  stagePath: string | null;
  dbBytesBefore: number | null;
  dbBytesAfter: number | null;
  dbBytesDelta: number | null;
  error: string | null;
};

type HistoryJobInternal = HistoryJobSnapshot & { cancelled: boolean };

type HistoryStageManifest = {
  version: 1;
  guildId: string;
  scope: HistoryScope;
  channelId: string | null;
  days: number;
  maxPerChannel: number;
  createdAt: string;
  messages: number;
  channels: number;
  rawBytes: number;
  compressedBytes: number;
  stagePath: string;
};

const jobs = new Map<string, HistoryJobInternal>();

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

function isFetchableMessageChannel(value: unknown): value is FetchableMessageChannel {
  if (!value || typeof value !== "object") return false;
  const record = value as Record<string, unknown>;
  const messages = record.messages;
  return Boolean(
    typeof record.id === "string" &&
      messages &&
      typeof messages === "object" &&
      typeof (messages as { fetch?: unknown }).fetch === "function"
  );
}

async function collectServerMessageChannels(
  guild: Guild
): Promise<FetchableMessageChannel[]> {
  await guild.channels.fetch().catch(() => undefined);
  const activeThreads = await guild.channels.fetchActiveThreads().catch(() => null);

  const deduped = new Map<string, FetchableMessageChannel>();

  for (const channel of guild.channels.cache.values()) {
    if (isFetchableMessageChannel(channel)) {
      deduped.set(channel.id, channel);
    }
  }

  if (activeThreads) {
    for (const thread of activeThreads.threads.values()) {
      if (isFetchableMessageChannel(thread)) {
        deduped.set(thread.id, thread);
      }
    }
  }

  return [...deduped.values()];
}

async function fetchChannelHistory(args: {
  channel: FetchableMessageChannel;
  clientUserId: string | null;
  cutoffTimestampMs: number | null;
  maxMessages: number;
  stopIfCancelled?: () => boolean;
  onMessage: (message: Message) => Promise<void>;
}): Promise<number> {
  const {
    channel,
    clientUserId,
    cutoffTimestampMs,
    maxMessages,
    stopIfCancelled,
    onMessage
  } = args;

  let before: string | undefined;
  let count = 0;

  while (!stopIfCancelled?.()) {
    const page = await channel.messages.fetch({
      limit: 100,
      before,
      cache: false
    });

    if (page.size === 0) break;

    const messages = [...page.values()].sort(
      (a, b) => b.createdTimestamp - a.createdTimestamp
    );

    let reachedCutoff = false;

    for (const message of messages) {
      if (stopIfCancelled?.()) break;

      if (
        cutoffTimestampMs !== null &&
        message.createdTimestamp <= cutoffTimestampMs
      ) {
        reachedCutoff = true;
        break;
      }

      if (clientUserId && message.author.id === clientUserId) {
        continue;
      }

      await onMessage(message);
      count += 1;

      if (maxMessages > 0 && count >= maxMessages) {
        return count;
      }
    }

    if (reachedCutoff || messages.length < 100) break;

    const oldest = messages[messages.length - 1];
    if (!oldest) break;
    before = oldest.id;
  }

  return count;
}

function stageDirectory(guildId: string): string {
  return path.join(process.cwd(), "history-staging", guildId);
}

function latestManifestPath(guildId: string): string {
  return path.join(stageDirectory(guildId), "latest.json");
}

export async function getLatestHistoryStage(
  guildId: string
): Promise<HistoryStageManifest | null> {
  try {
    const text = await fsp.readFile(latestManifestPath(guildId), "utf8");
    return JSON.parse(text) as HistoryStageManifest;
  } catch {
    return null;
  }
}

async function resolveChannelsForScan(
  guild: Guild,
  settings: GuildSettings,
  scope: HistoryScope,
  channelId: string | null
): Promise<FetchableMessageChannel[]> {
  if (scope === "channel") {
    if (!channelId) throw new Error("No channel was supplied for channel scan.");
    const channel = await guild.channels.fetch(channelId);
    if (!isFetchableMessageChannel(channel)) {
      throw new Error("That channel does not expose message history.");
    }
    if (isConfiguredOutputChannel(settings, channel.id)) {
      throw new Error("Nifibot log/output channels are excluded from history scans.");
    }
    if (!botCanReadMessageHistory(guild, channel)) {
      throw new Error(
        "Nifibot cannot read that channel. Grant View Channel + Read Message History if it should be included."
      );
    }
    return [channel];
  }

  const channels = await collectServerMessageChannels(guild);
  return channels.filter(
    (channel) =>
      !isConfiguredOutputChannel(settings, channel.id) &&
      botCanReadMessageHistory(guild, channel)
  );
}

async function runHistoryScan(
  client: Client,
  guild: Guild,
  job: HistoryJobInternal
): Promise<void> {
  let output: fs.WriteStream | null = null;
  let gzip: ReturnType<typeof createGzip> | null = null;

  try {
    const settings = await getGuildSettings(guild.id);
    const channels = await resolveChannelsForScan(
      guild,
      settings,
      job.scope,
      job.channelId
    );

    job.channelsTotal = channels.length;

    const dir = stageDirectory(guild.id);
    await fsp.mkdir(dir, { recursive: true });

    const stamp = new Date().toISOString().replace(/[:.]/g, "-");
    const stagePath = path.join(dir, `history-${stamp}.jsonl.gz`);
    job.stagePath = stagePath;

    output = fs.createWriteStream(stagePath, { flags: "wx" });
    gzip = createGzip({ level: 9 });
    gzip.pipe(output);

    const cutoffTimestampMs =
      job.days === 0 ? null : Date.now() - job.days * 24 * 60 * 60 * 1000;

    for (const channel of channels) {
      if (job.cancelled) break;
      job.currentChannelId = channel.id;

      try {
        await fetchChannelHistory({
          channel,
          clientUserId: client.user?.id ?? null,
          cutoffTimestampMs,
          maxMessages: job.maxPerChannel,
          stopIfCancelled: () => job.cancelled,
          onMessage: async (message) => {
            const record = serializeMessageRecord(message);
            const line = `${JSON.stringify(record)}\n`;
            job.rawBytes += Buffer.byteLength(line, "utf8");
            job.messagesProcessed += 1;

            if (!gzip!.write(line, "utf8")) {
              await once(gzip!, "drain");
            }
          }
        });
      } catch (error) {
        if (job.scope === "channel") throw error;
        log.warn(
          {
            guildId: guild.id,
            channelId: channel.id,
            error: compactDiscordError(error)
          },
          "History scan skipped a channel that could not be read"
        );
      } finally {
        job.channelsDone += 1;
      }
    }

    gzip.end();
    await once(output, "close");

    const stat = await fsp.stat(stagePath);
    job.compressedBytes = stat.size;

    const manifest: HistoryStageManifest = {
      version: 1,
      guildId: guild.id,
      scope: job.scope,
      channelId: job.channelId,
      days: job.days,
      maxPerChannel: job.maxPerChannel,
      createdAt: new Date().toISOString(),
      messages: job.messagesProcessed,
      channels: job.channelsDone,
      rawBytes: job.rawBytes,
      compressedBytes: job.compressedBytes,
      stagePath
    };

    await fsp.writeFile(
      latestManifestPath(guild.id),
      JSON.stringify(manifest, null, 2),
      "utf8"
    );

    job.currentChannelId = null;
    job.finishedAt = new Date();
    job.status = job.cancelled ? "cancelled" : "done";
  } catch (error) {
    gzip?.destroy();
    output?.destroy();
    job.currentChannelId = null;
    job.finishedAt = new Date();
    job.status = "failed";
    job.error = error instanceof Error ? error.message : String(error);
    log.error({ err: error, guildId: guild.id }, "Local history scan failed");
  }
}

export function startHistoryScan(
  client: Client,
  guild: Guild,
  options: HistoryScanOptions
): HistoryJobSnapshot {
  const existing = jobs.get(guild.id);
  if (existing?.status === "running") {
    throw new Error("A history scan/import is already running for this server.");
  }

  const job: HistoryJobInternal = {
    guildId: guild.id,
    kind: "scan",
    scope: options.scope,
    channelId: options.channelId ?? null,
    days: options.days,
    maxPerChannel: options.maxPerChannel,
    status: "running",
    startedAt: new Date(),
    finishedAt: null,
    channelsDone: 0,
    channelsTotal: 0,
    messagesProcessed: 0,
    currentChannelId: null,
    rawBytes: 0,
    compressedBytes: 0,
    stagePath: null,
    dbBytesBefore: null,
    dbBytesAfter: null,
    dbBytesDelta: null,
    error: null,
    cancelled: false
  };

  jobs.set(guild.id, job);
  void runHistoryScan(client, guild, job);
  return { ...job };
}

async function runHistoryImport(
  guild: Guild,
  job: HistoryJobInternal,
  manifest: HistoryStageManifest
): Promise<void> {
  try {
    const settings = await getGuildSettings(guild.id);
    job.dbBytesBefore = await getMessageCacheStorageBytes();
    job.stagePath = manifest.stagePath;
    job.rawBytes = manifest.rawBytes;
    job.compressedBytes = manifest.compressedBytes;

    const source = fs.createReadStream(manifest.stagePath);
    const gunzip = createGunzip();
    const rl = readline.createInterface({
      input: source.pipe(gunzip),
      crlfDelay: Infinity
    });

    let batch: SerializedMessageRecord[] = [];

    const flush = async () => {
      if (batch.length === 0) return;
      await upsertSerializedMessages(batch, settings.message_retention_days);
      job.messagesProcessed += batch.length;
      batch = [];
    };

    for await (const line of rl) {
      if (job.cancelled) break;
      if (!line.trim()) continue;
      batch.push(JSON.parse(line) as SerializedMessageRecord);
      if (batch.length >= 250) {
        await flush();
      }
    }

    if (!job.cancelled) {
      await flush();
    }

    job.dbBytesAfter = await getMessageCacheStorageBytes();
    job.dbBytesDelta = Math.max(0, job.dbBytesAfter - job.dbBytesBefore);
    job.finishedAt = new Date();
    job.status = job.cancelled ? "cancelled" : "done";
  } catch (error) {
    job.finishedAt = new Date();
    job.status = "failed";
    job.error = error instanceof Error ? error.message : String(error);
    log.error({ err: error, guildId: guild.id }, "History import failed");
  }
}

export async function startHistoryImport(guild: Guild): Promise<HistoryJobSnapshot> {
  const existing = jobs.get(guild.id);
  if (existing?.status === "running") {
    throw new Error("A history scan/import is already running for this server.");
  }

  const manifest = await getLatestHistoryStage(guild.id);
  if (!manifest) {
    throw new Error("No completed local history scan exists yet.");
  }

  await fsp.access(manifest.stagePath);

  const job: HistoryJobInternal = {
    guildId: guild.id,
    kind: "import",
    scope: manifest.scope,
    channelId: manifest.channelId,
    days: manifest.days,
    maxPerChannel: manifest.maxPerChannel,
    status: "running",
    startedAt: new Date(),
    finishedAt: null,
    channelsDone: manifest.channels,
    channelsTotal: manifest.channels,
    messagesProcessed: 0,
    currentChannelId: null,
    rawBytes: manifest.rawBytes,
    compressedBytes: manifest.compressedBytes,
    stagePath: manifest.stagePath,
    dbBytesBefore: null,
    dbBytesAfter: null,
    dbBytesDelta: null,
    error: null,
    cancelled: false
  };

  jobs.set(guild.id, job);
  void runHistoryImport(guild, job, manifest);
  return { ...job };
}

export function getHistoryJobStatus(guildId: string): HistoryJobSnapshot | null {
  const job = jobs.get(guildId);
  return job ? { ...job } : null;
}

export function cancelHistoryJob(guildId: string): boolean {
  const job = jobs.get(guildId);
  if (!job || job.status !== "running") return false;
  job.cancelled = true;
  return true;
}

async function cacheChannelHistory(args: {
  channel: FetchableMessageChannel;
  clientUserId: string | null;
  retentionDays: number;
  cutoffTimestampMs: number | null;
  maxMessages: number;
}): Promise<number> {
  return fetchChannelHistory({
    channel: args.channel,
    clientUserId: args.clientUserId,
    cutoffTimestampMs: args.cutoffTimestampMs,
    maxMessages: args.maxMessages,
    onMessage: async (message) => {
      await cacheMessage(message, args.retentionDays);
    }
  });
}

async function startupCatchupGuild(client: Client, guild: Guild): Promise<void> {
  const settings = await getGuildSettings(guild.id);
  if (!settings.logging_enabled) return;

  const discovered = (await collectServerMessageChannels(guild)).filter(
    (channel) => !isConfiguredOutputChannel(settings, channel.id)
  );
  const channels = discovered.filter((channel) =>
    botCanReadMessageHistory(guild, channel)
  );
  const skippedByPermissions = discovered.length - channels.length;

  let total = 0;
  let failedChannels = 0;

  for (const channel of channels) {
    try {
      const cursor = await getNewestCachedMessageCursor(channel.id);

      if (cursor) {
        total += await cacheChannelHistory({
          channel,
          clientUserId: client.user?.id ?? null,
          retentionDays: settings.message_retention_days,
          cutoffTimestampMs: cursor.createdAt.getTime(),
          maxMessages: 0
        });
      } else if (config.STARTUP_BOOTSTRAP_MESSAGES > 0) {
        total += await cacheChannelHistory({
          channel,
          clientUserId: client.user?.id ?? null,
          retentionDays: settings.message_retention_days,
          cutoffTimestampMs: null,
          maxMessages: config.STARTUP_BOOTSTRAP_MESSAGES
        });
      }
    } catch (error) {
      failedChannels += 1;
      log.warn(
        {
          guildId: guild.id,
          channelId: channel.id,
          error: compactDiscordError(error)
        },
        "Startup message catch-up skipped an unreadable/failed channel"
      );
    }
  }

  log.info(
    {
      guildId: guild.id,
      channelsDiscovered: discovered.length,
      channelsReadable: channels.length,
      skippedByPermissions,
      failedChannels,
      messagesCached: total
    },
    "Startup message-history catch-up complete"
  );
}

export async function runStartupCatchup(client: Client): Promise<void> {
  if (!config.STARTUP_CATCHUP_ENABLED) return;

  for (const guild of client.guilds.cache.values()) {
    await startupCatchupGuild(client, guild).catch((error) => {
      log.warn(
        { err: error, guildId: guild.id },
        "Startup message-history catch-up failed for guild"
      );
    });
  }
}
