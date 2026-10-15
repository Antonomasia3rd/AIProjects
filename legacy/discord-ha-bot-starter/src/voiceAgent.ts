import os from "node:os";
import pg from "pg";
import {
  Client,
  EmbedBuilder,
  Events,
  GatewayIntentBits,
  type Guild
} from "discord.js";
import {
  VoiceConnectionStatus,
  entersState,
  getVoiceConnection,
  joinVoiceChannel
} from "@discordjs/voice";
import { config } from "./config.js";
import { getGuildSettings } from "./guildSettings.js";
import { log } from "./logger.js";
import { bounded, LeaseElectorCore, monotonicClock } from "./runtime/leaseCore.js";

const { Pool } = pg;
const LEASE_NAME = "discord-voice";

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

type Lease = {
  holder_id: string;
  epoch: string;
  lease_until: Date;
};

type VoiceTarget = {
  guild_id: string;
  startup_voice_channel_id: string;
};

/**
 * Voice HA deliberately uses its own tiny PostgreSQL pool.
 * Lease health should not wait behind message-history/cache traffic.
 */
const voiceDb = new Pool({
  connectionString: config.DATABASE_URL,
  ssl: config.DATABASE_SSL ? { rejectUnauthorized: false } : undefined,
  max: 1,
  idleTimeoutMillis: 30_000,
  connectionTimeoutMillis: 4_000,
  query_timeout: 4_000,
  statement_timeout: 4_000,
  keepAlive: true,
  keepAliveInitialDelayMillis: 5_000
});

// The voice HA pool is independent from the main DB pool. Supabase/pooler can
// reset idle TLS connections on both pools at the same time. The main pool has
// its own error listener in db.ts; without an equivalent listener here, an
// idle-client ECONNRESET is emitted as an unhandled `error` event and reaches
// process.uncaughtException, taking the whole Nifibot node down. pg-pool
// discards the broken idle client automatically, so log and let the next voice
// query create/use a healthy connection.
voiceDb.on("error", (error: Error) => {
  log.warn(
    {
      err: {
        name: error.name,
        message: error.message,
        code: (error as Error & { code?: unknown }).code
      }
    },
    "Voice PostgreSQL idle pooled connection dropped; keeping Nifibot online"
  );
});

async function configuredTargets(): Promise<VoiceTarget[]> {
  const result = await voiceDb.query<VoiceTarget>(
    `
    SELECT guild_id, startup_voice_channel_id
    FROM guild_settings
    WHERE startup_voice_channel_id IS NOT NULL
    ORDER BY guild_id
    `
  );
  return result.rows;
}

async function sendVoiceSystemLog(
  client: Client,
  guildId: string,
  embed: EmbedBuilder,
  signal?: AbortSignal
): Promise<void> {
  if (signal?.aborted) return;
  const settings = await getGuildSettings(guildId).catch(() => null);
  if (signal?.aborted) return;
  if (!settings?.logging_enabled || !settings.startup_log_enabled) return;

  const channelId =
    settings.voice_log_channel_id ??
    settings.member_log_channel_id ??
    settings.log_channel_id;
  if (!channelId) return;

  const channel = await client.channels.fetch(channelId).catch(() => null);
  if (signal?.aborted || !channel?.isSendable()) return;

  await channel.send({ embeds: [embed] }).catch((error: unknown) => {
    log.warn(
      { err: error, guildId, channelId },
      "Failed to send voice HA lifecycle log"
    );
  });
}

export class VoiceHaAgent {
  private stopping = false;
  private owner = false;
  private epoch: string | null = null;
  private elector: LeaseElectorCore | null = null;
  private ownershipSignal: AbortSignal | null = null;
  private shutdownSignal = new AbortController();
  private targets: VoiceTarget[] = [];
  private joinedGuildIds = new Set<string>();
  private loopPromise: Promise<void> | null = null;
  private client: Client | null = null;
  private clientReady = false;

  async start(): Promise<void> {
    if (!config.VOICE_HA_ENABLED) {
      log.info({ node: config.NODE_ID }, "Independent voice HA is disabled");
      return;
    }

    if (this.loopPromise) return;
    this.loopPromise = this.run();

    log.info(
      {
        node: config.NODE_ID,
        leaseMs: config.VOICE_LEASE_MS,
        renewMs: config.VOICE_RENEW_MS
      },
      "Independent voice HA agent started"
    );
  }

  async stop(): Promise<void> {
    this.stopping = true;
    this.shutdownSignal.abort(); this.elector?.stop();
    await this.stopVoiceWorkload();

    if (this.loopPromise) {
      await this.loopPromise.catch(() => undefined);
      this.loopPromise = null;
    }

    await voiceDb.end().catch(() => undefined);
  }

  private async ensureClient(signal: AbortSignal): Promise<Client> {
    if (signal.aborted || this.stopping) throw new Error("Voice gateway startup cancelled.");
    if (this.client?.isReady()) return this.client;

    if (this.client) {
      this.client.destroy();
      this.client = null;
      this.clientReady = false;
    }

    const client = new Client({
      intents: [GatewayIntentBits.Guilds, GatewayIntentBits.GuildVoiceStates]
    });
    // Publish the in-progress client so abort/shutdown can destroy a login
    // that never settles. Late events cannot replace a newer gateway.
    this.client = client;
    const cancel = () => {
      void client.destroy().catch(() => undefined);
      if (this.client === client) { this.client = null; this.clientReady = false; }
    };
    signal.addEventListener("abort", cancel, { once: true });

    client.once(Events.ClientReady, (readyClient) => {
      if (this.client !== client || signal.aborted || this.stopping) return;
      this.clientReady = true;
      log.info(
        {
          node: config.NODE_ID,
          user: readyClient.user.tag
        },
        "Voice-only Discord gateway ready"
      );
    });

    client.on(Events.Error, (error) => {
      log.warn({ err: error, node: config.NODE_ID }, "Voice-only Discord client error");
    });

    try {
      await bounded(client.login(config.DISCORD_TOKEN), config.VOICE_READY_TIMEOUT_MS, monotonicClock, signal);
      const deadline = monotonicClock.now() + config.VOICE_READY_TIMEOUT_MS;
      while (!client.isReady() && monotonicClock.now() < deadline && !signal.aborted && !this.stopping) await sleep(100);
      if (!client.isReady() || signal.aborted || this.stopping || this.client !== client)
        throw new Error("Voice-only Discord gateway did not become ready before its deadline.");
      return client;
    } catch (error) { cancel(); throw error; }
    finally { signal.removeEventListener("abort", cancel); }
  }

  private async stopClient(): Promise<void> {
    const client = this.client;
    this.client = null; this.clientReady = false;
    if (client) await client.destroy().catch(() => undefined);
  }

  private async tryAcquire(): Promise<Lease | null> {
    const result = await voiceDb.query<Lease>(
      `
      INSERT INTO cluster_leases AS current
        (lease_name, holder_id, lease_until, epoch, updated_at)
      VALUES
        ($1, $2, clock_timestamp() + ($3::int * interval '1 millisecond'), 1, clock_timestamp())
      ON CONFLICT (lease_name) DO UPDATE
      SET holder_id = EXCLUDED.holder_id,
          lease_until = EXCLUDED.lease_until,
          epoch = current.epoch + 1,
          updated_at = clock_timestamp()
      WHERE current.lease_until <= clock_timestamp()
      RETURNING holder_id, epoch, lease_until
      `,
      [LEASE_NAME, config.NODE_ID, config.VOICE_LEASE_MS]
    );

    return result.rows[0] ?? null;
  }

  private async renew(epoch: string): Promise<boolean> {

    const result = await voiceDb.query(
      `
      UPDATE cluster_leases
      SET lease_until = clock_timestamp() + ($4::int * interval '1 millisecond'),
          updated_at = clock_timestamp()
      WHERE lease_name = $1
        AND holder_id = $2
        AND epoch = $3::bigint
        AND lease_until > clock_timestamp()
      RETURNING lease_until
      `,
      [LEASE_NAME, config.NODE_ID, epoch, config.VOICE_LEASE_MS]
    );

    return result.rowCount === 1;
  }

  private async release(epoch: string): Promise<void> {

    await voiceDb.query(
      `
      UPDATE cluster_leases
      SET lease_until = clock_timestamp(),
          updated_at = clock_timestamp()
      WHERE lease_name = $1
        AND holder_id = $2
        AND epoch = $3::bigint
      `,
      [LEASE_NAME, config.NODE_ID, epoch]
    );
  }

  private async guildForTarget(client: Client, target: VoiceTarget): Promise<Guild | null> {
    return (
      client.guilds.cache.get(target.guild_id) ??
      (await client.guilds.fetch(target.guild_id).catch(() => null))
    );
  }

  private currentOwner(signal: AbortSignal): boolean {
    return !signal.aborted && !this.stopping && this.owner && this.ownershipSignal === signal && !!this.elector?.isLeader();
  }
  private async joinTarget(client: Client, target: VoiceTarget, signal: AbortSignal): Promise<boolean> {
    if (!this.currentOwner(signal)) return false;
    const guild = await this.guildForTarget(client, target);
    if (!this.currentOwner(signal)) return false;
    if (!guild) {
      log.warn(
        { node: config.NODE_ID, guildId: target.guild_id },
        "Voice HA target guild is unavailable to this bot"
      );
      return false;
    }

    const channel = await guild.channels
      .fetch(target.startup_voice_channel_id)
      .catch(() => null);
    if (!this.currentOwner(signal)) return false;

    if (!channel?.isVoiceBased()) {
      log.warn(
        {
          node: config.NODE_ID,
          guildId: target.guild_id,
          channelId: target.startup_voice_channel_id
        },
        "Voice HA target channel is missing or is not voice-based"
      );
      return false;
    }

    const existing = getVoiceConnection(guild.id);
    if (
      existing &&
      existing.joinConfig.channelId === channel.id &&
      existing.state.status === VoiceConnectionStatus.Ready
    ) {
      this.joinedGuildIds.add(guild.id);
      return true;
    }

    if (existing) existing.destroy();

    try {
      const connection = joinVoiceChannel({
        channelId: channel.id,
        guildId: guild.id,
        adapterCreator: guild.voiceAdapterCreator,
        selfDeaf: true,
        selfMute: false
      });
      this.joinedGuildIds.add(guild.id);

      await entersState(
        connection,
        VoiceConnectionStatus.Ready,
        config.VOICE_READY_TIMEOUT_MS
      );
      if (!this.currentOwner(signal)) { connection.destroy(); return false; }

      log.info(
        {
          node: config.NODE_ID,
          guildId: guild.id,
          channelId: channel.id,
          channelName: channel.name,
          epoch: this.epoch
        },
        "Voice HA connection ready"
      );

      // Optional lifecycle logging must not postpone the next lease renewal.
      void sendVoiceSystemLog(
        client,
        guild.id,
        new EmbedBuilder()
          .setTitle("🎧 Nifibot voice owner online")
          .addFields(
            {
              name: "Voice owner",
              value: `**${config.NODE_ID}**\nHost: \`${os.hostname()}\``
            },
            {
              name: "Channel",
              value: `<#${channel.id}>`
            },
            {
              name: "Voice lease epoch",
              value: `\`${this.epoch ?? "?"}\``
            },
            {
              name: "HA mode",
              value: "Independent from the Discord command/logging leader."
            }
          )
          .setTimestamp(),
        signal
      ).catch(error => log.warn({ err: error }, "Voice lifecycle log failed"));

      return this.currentOwner(signal);
    } catch (error) {
      // A stale continuation must not destroy a replacement owner's join.
      if (this.ownershipSignal === signal) getVoiceConnection(guild.id)?.destroy();
      log.warn(
        {
          err: error,
          node: config.NODE_ID,
          guildId: guild.id,
          channelId: channel.id
        },
        "Voice HA join failed"
      );
      return false;
    }
  }

  private async syncTargets(targets: VoiceTarget[], signal: AbortSignal): Promise<number> {
    if (!targets.length) return 0;
    const client = await this.ensureClient(signal);
    if (!this.currentOwner(signal)) return 0;
    const targetGuildIds = new Set(targets.map((target) => target.guild_id));

    for (const guild of client.guilds.cache.values()) {
      if (!targetGuildIds.has(guild.id)) {
        getVoiceConnection(guild.id)?.destroy();
        this.joinedGuildIds.delete(guild.id);
      }
    }

    let ready = 0;
    for (const target of targets) {
      if (!this.currentOwner(signal)) break;
      if (await this.joinTarget(client, target, signal)) ready += 1;
    }
    return ready;
  }

  private async stopVoiceWorkload(): Promise<void> {
    for (const guildId of this.joinedGuildIds) getVoiceConnection(guildId)?.destroy();
    this.joinedGuildIds.clear();
    if (this.client) {
      for (const guild of this.client.guilds.cache.values()) {
        getVoiceConnection(guild.id)?.destroy();
      }
    }
  }

  private clearOwnership(): void {
    this.owner = false;
    this.epoch = null;
    this.ownershipSignal = null;
  }

  private async run(): Promise<void> {
    try {
      if (config.VOICE_AGENT_START_DELAY_MS > 0)
        await bounded(new Promise<void>(() => {}), config.VOICE_AGENT_START_DELAY_MS, monotonicClock, this.shutdownSignal.signal).catch(() => {});
      if (this.stopping) return;
      this.elector = new LeaseElectorCore({
        acquire: () => this.tryAcquire(), renew: epoch => this.renew(epoch), release: epoch => this.release(epoch)
      }, {
        enabled: true, renewMs: config.VOICE_RENEW_MS, failSafeMs: config.VOICE_FAILSAFE_MS,
        standbyMs: config.VOICE_STANDBY_POLL_MS, operationMs: 4000,
        prepareMs: config.VOICE_READY_TIMEOUT_MS * 2 + 5000,
        beforeAcquire: async signal => {
          const targets = await configuredTargets();
          if (signal.aborted || this.stopping) return false;
          this.targets = targets;
          if (!targets.length) { await this.stopVoiceWorkload(); await this.stopClient(); return false; }
          // Keep the minimal standby gateway warm before acquiring a voice
          // lease. This login has its own bounded, cancellable preparation.
          await this.ensureClient(signal);
          return !signal.aborted && !this.stopping;
        }
      }, monotonicClock, (message, error) => log.warn({ node: config.NODE_ID, err: error }, `Voice HA: ${message}`));
      await this.elector.run(async signal => {
        this.owner = true; this.epoch = this.elector!.getEpoch(); this.ownershipSignal = signal;
        signal.addEventListener("abort", () => {
          this.clearOwnership();
          void this.stopVoiceWorkload().catch(error => log.warn({ err: error }, "Voice expiry cleanup failed"));
        }, { once: true });
        if (await this.syncTargets(this.targets, signal) === 0)
          throw new Error("No configured voice connection became ready before its ownership deadline.");
      }, async () => {
        this.clearOwnership(); await this.stopVoiceWorkload();
      }, async signal => {
        const targets = await configuredTargets();
        if (!this.currentOwner(signal)) throw new Error("Voice ownership expired during target refresh.");
        if (!targets.length || await this.syncTargets(targets, signal) === 0)
          throw new Error("No healthy configured voice connection remains; yielding the lease.");
        this.targets = targets;
      });
    } finally {
      this.clearOwnership(); await this.stopVoiceWorkload(); await this.stopClient();
    }
  }
}
