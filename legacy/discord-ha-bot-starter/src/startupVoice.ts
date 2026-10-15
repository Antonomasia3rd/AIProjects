import type { Client, Guild } from "discord.js";
import {
  VoiceConnectionStatus,
  entersState,
  getVoiceConnection,
  joinVoiceChannel
} from "@discordjs/voice";
import { getGuildSettings } from "./guildSettings.js";
import { log } from "./logger.js";

export type StartupVoiceResult = {
  guildId: string;
  channelId: string | null;
  status: "disabled" | "joined" | "failed";
  detail: string;
};

async function joinGuildStartupVoice(guild: Guild): Promise<StartupVoiceResult> {
  const settings = await getGuildSettings(guild.id);
  const channelId = settings.startup_voice_channel_id;

  if (!channelId) {
    return {
      guildId: guild.id,
      channelId: null,
      status: "disabled",
      detail: "No startup voice channel is configured."
    };
  }

  const channel = await guild.channels.fetch(channelId).catch(() => null);
  if (!channel?.isVoiceBased()) {
    return {
      guildId: guild.id,
      channelId,
      status: "failed",
      detail: "Configured startup voice channel is missing or is not voice-based."
    };
  }

  try {
    const existing = getVoiceConnection(guild.id);
    if (existing) existing.destroy();

    const connection = joinVoiceChannel({
      channelId: channel.id,
      guildId: guild.id,
      adapterCreator: guild.voiceAdapterCreator,
      selfDeaf: true,
      selfMute: false
    });

    await entersState(connection, VoiceConnectionStatus.Ready, 15_000);

    return {
      guildId: guild.id,
      channelId: channel.id,
      status: "joined",
      detail: `Joined ${channel.name}.`
    };
  } catch (error) {
    getVoiceConnection(guild.id)?.destroy();
    const detail = error instanceof Error ? error.message : String(error);
    log.warn(
      { err: error, guildId: guild.id, channelId },
      "Startup voice join failed; Discord gateway remains online"
    );
    return { guildId: guild.id, channelId, status: "failed", detail };
  }
}

export async function joinConfiguredStartupVoice(
  client: Client
): Promise<StartupVoiceResult[]> {
  const results: StartupVoiceResult[] = [];
  for (const guild of client.guilds.cache.values()) {
    results.push(await joinGuildStartupVoice(guild));
  }
  return results;
}

export function stopStartupVoice(client: Client): void {
  for (const guild of client.guilds.cache.values()) {
    getVoiceConnection(guild.id)?.destroy();
  }
}
