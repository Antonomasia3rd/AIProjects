import {
  AttachmentBuilder,
  ChannelType,
  MessageFlags,
  PermissionFlagsBits,
  SlashCommandBuilder
} from "discord.js";
import fs from "node:fs/promises";
import type { BotCommand } from "./types.js";
import {
  addIgnoredMessageUser,
  getGuildSettings,
  removeIgnoredMessageUser,
  setLoggingChannel,
  setLoggingEnabled,
  setMessageRetentionDays,
  setStartupLogEnabled,
  setStartupVoiceChannel,
  type LogChannelKind
} from "../guildSettings.js";
import { createGuildMonthArchive } from "../archive.js";
import {
  cancelHistoryJob,
  getHistoryJobStatus,
  getLatestHistoryStage,
  startHistoryImport,
  startHistoryScan
} from "../backfill.js";

function formatBytes(value: number | null | undefined): string {
  if (value === null || value === undefined) return "unknown";
  const units = ["B", "KiB", "MiB", "GiB"];
  let n = value;
  let i = 0;
  while (n >= 1024 && i < units.length - 1) {
    n /= 1024;
    i += 1;
  }
  return `${n >= 10 || i === 0 ? n.toFixed(0) : n.toFixed(1)} ${units[i]}`;
}

export const configLoggingCommand: BotCommand = {
  data: new SlashCommandBuilder()
    .setName("logging")
    .setDescription("Configure Nifibot's logging system.")
    .setDefaultMemberPermissions(PermissionFlagsBits.ManageGuild)

    .addSubcommand((sub) =>
      sub
        .setName("enabled")
        .setDescription("Enable or disable logging.")
        .addBooleanOption((option) =>
          option
            .setName("value")
            .setDescription("Whether logging is enabled.")
            .setRequired(true)
        )
    )

    .addSubcommand((sub) =>
      sub
        .setName("channel")
        .setDescription("Set a log destination.")
        .addStringOption((option) =>
          option
            .setName("kind")
            .setDescription("Which category should use this channel.")
            .setRequired(true)
            .addChoices(
              { name: "Everything (current + future default)", value: "everything" },
              { name: "Messages + reactions", value: "messages" },
              { name: "Members + roles", value: "members" },
              { name: "Voice activity", value: "voice" },
              { name: "Moderation", value: "moderation" },
              { name: "Archives", value: "archive" }
            )
        )
        .addChannelOption((option) =>
          option
            .setName("channel")
            .setDescription("Destination text channel.")
            .addChannelTypes(ChannelType.GuildText)
            .setRequired(true)
        )
    )

    .addSubcommand((sub) =>
      sub
        .setName("startup-voice")
        .setDescription("Configure the independent HA voice-channel target.")
        .addBooleanOption((option) =>
          option
            .setName("enabled")
            .setDescription("Enable or disable independent Voice HA.")
            .setRequired(true)
        )
        .addChannelOption((option) =>
          option
            .setName("channel")
            .setDescription("Voice channel to join when enabled.")
            .addChannelTypes(ChannelType.GuildVoice, ChannelType.GuildStageVoice)
            .setRequired(false)
        )
    )

    .addSubcommand((sub) =>
      sub
        .setName("startup-log")
        .setDescription("Enable or disable the leader-online startup log.")
        .addBooleanOption((option) =>
          option
            .setName("enabled")
            .setDescription("Whether Nifibot logs leader startup/failover online events.")
            .setRequired(true)
        )
    )

    .addSubcommand((sub) =>
      sub
        .setName("retention")
        .setDescription("Set message-cache retention. 0 means forever.")
        .addIntegerOption((option) =>
          option
            .setName("days")
            .setDescription("Days to retain message records; 0 = forever.")
            .setMinValue(0)
            .setMaxValue(3650)
            .setRequired(true)
        )
    )

    .addSubcommand((sub) =>
      sub
        .setName("ignore-user")
        .setDescription("Ignore message/reaction logs involving a user or bot, but keep caching.")
        .addUserOption((option) =>
          option
            .setName("user")
            .setDescription("User or bot to ignore in message/reaction logs.")
            .setRequired(true)
        )
    )

    .addSubcommand((sub) =>
      sub
        .setName("unignore-user")
        .setDescription("Stop ignoring a user or bot in message/reaction logs.")
        .addUserOption((option) =>
          option
            .setName("user")
            .setDescription("User or bot to resume logging.")
            .setRequired(true)
        )
    )

    .addSubcommand((sub) =>
      sub
        .setName("status")
        .setDescription("Show the current logging configuration.")
    )

    .addSubcommand((sub) =>
      sub
        .setName("archive")
        .setDescription("Create a compressed monthly JSONL archive.")
        .addStringOption((option) =>
          option
            .setName("month")
            .setDescription("Month in YYYY-MM format.")
            .setRequired(true)
        )
        .addBooleanOption((option) =>
          option
            .setName("upload")
            .setDescription("Upload the archive to the configured archive/log channel.")
            .setRequired(false)
        )
    )

    .addSubcommand((sub) =>
      sub
        .setName("history-scan")
        .setDescription("Download existing Discord history to a LOCAL staging file only.")
        .addStringOption((option) =>
          option
            .setName("scope")
            .setDescription("Scan one channel or all accessible server text channels.")
            .setRequired(true)
            .addChoices(
              { name: "One channel", value: "channel" },
              { name: "Whole server", value: "server" }
            )
        )
        .addIntegerOption((option) =>
          option
            .setName("days")
            .setDescription("History age in days; 0 = all available history.")
            .setMinValue(0)
            .setMaxValue(3650)
            .setRequired(true)
        )
        .addChannelOption((option) =>
          option
            .setName("channel")
            .setDescription("Channel to scan when scope = channel.")
            .addChannelTypes(
              ChannelType.GuildText,
              ChannelType.GuildAnnouncement,
              ChannelType.PublicThread,
              ChannelType.PrivateThread,
              ChannelType.AnnouncementThread
            )
            .setRequired(false)
        )
        .addIntegerOption((option) =>
          option
            .setName("max_per_channel")
            .setDescription("Safety cap per channel; 0 = unlimited.")
            .setMinValue(0)
            .setMaxValue(1000000)
            .setRequired(false)
        )
    )

    .addSubcommand((sub) =>
      sub
        .setName("history-status")
        .setDescription("Show local history-scan/import progress and staged size.")
    )

    .addSubcommand((sub) =>
      sub
        .setName("history-import")
        .setDescription("Upload the latest LOCAL history scan into Supabase.")
        .addBooleanOption((option) =>
          option
            .setName("confirm")
            .setDescription("Must be true to start the Supabase import.")
            .setRequired(true)
        )
    )

    .addSubcommand((sub) =>
      sub
        .setName("history-cancel")
        .setDescription("Stop the currently running history scan/import.")
    ),

  async execute(interaction) {
    if (!interaction.guildId || !interaction.guild) {
      await interaction.reply({
        content: "This command only works in a server.",
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    const sub = interaction.options.getSubcommand();

    if (sub === "enabled") {
      const enabled = interaction.options.getBoolean("value", true);
      await setLoggingEnabled(interaction.guildId, enabled);

      await interaction.reply({
        content: `✅ Logging is now **${enabled ? "enabled" : "disabled"}**.`,
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (sub === "channel") {
      const kind = interaction.options.getString("kind", true) as LogChannelKind;
      const channel = interaction.options.getChannel("channel", true);

      await setLoggingChannel(interaction.guildId, kind, channel.id);

      await interaction.reply({
        content: `✅ **${kind}** logs will use <#${channel.id}>.`,
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (sub === "startup-voice") {
      const enabled = interaction.options.getBoolean("enabled", true);
      const channel = interaction.options.getChannel("channel");

      if (enabled && !channel) {
        await interaction.reply({
          content: "❌ `channel` is required when Voice HA is enabled.",
          flags: MessageFlags.Ephemeral
        });
        return;
      }

      await setStartupVoiceChannel(
        interaction.guildId,
        enabled ? channel!.id : null
      );

      await interaction.reply({
        content: enabled
          ? `✅ Independent voice HA will keep Nifibot in <#${channel!.id}>. The voice-owning node does not have to be the Discord gateway leader.`
          : "✅ Voice HA joining is disabled for this server.",
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (sub === "startup-log") {
      const enabled = interaction.options.getBoolean("enabled", true);
      await setStartupLogEnabled(interaction.guildId, enabled);
      await interaction.reply({
        content: `✅ Startup/leader-online Discord logging is now **${enabled ? "enabled" : "disabled"}**.`,
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (sub === "retention") {
      const days = interaction.options.getInteger("days", true);
      await setMessageRetentionDays(interaction.guildId, days);

      await interaction.reply({
        content:
          days === 0
            ? "✅ Message retention is now **forever**."
            : `✅ Message retention is now **${days} day(s)**.`,
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (sub === "ignore-user") {
      const user = interaction.options.getUser("user", true);
      if (interaction.client.user?.id === user.id) {
        await interaction.reply({
          content: "Nifibot already excludes its own output from message logging.",
          flags: MessageFlags.Ephemeral
        });
        return;
      }

      await addIgnoredMessageUser(interaction.guildId, user.id);
      await interaction.reply({
        content: [
          `🙈 Message/reaction logging will ignore <@${user.id}> (\`${user.id}\`).`,
          "Messages from this user/bot are **still cached** for history/recovery; Nifibot simply suppresses their edit/delete/reaction log spam."
        ].join("\n"),
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (sub === "unignore-user") {
      const user = interaction.options.getUser("user", true);
      await removeIgnoredMessageUser(interaction.guildId, user.id);
      await interaction.reply({
        content: `👀 Message/reaction logging will resume for <@${user.id}> (\`${user.id}\`).`,
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (sub === "status") {
      const settings = await getGuildSettings(interaction.guildId);

      await interaction.reply({
        content: [
          `**Enabled:** ${settings.logging_enabled ? "yes" : "no"}`,
          `**Everything / future default:** ${settings.log_channel_id ? `<#${settings.log_channel_id}>` : "not set"}`,
          `**Messages:** ${settings.log_channel_id ? `<#${settings.log_channel_id}>` : "not set"}`,
          `**Members:** ${settings.member_log_channel_id ? `<#${settings.member_log_channel_id}> *(override)*` : settings.log_channel_id ? `default → <#${settings.log_channel_id}>` : "not set"}`,
          `**Voice:** ${settings.voice_log_channel_id ? `<#${settings.voice_log_channel_id}> *(override)*` : settings.member_log_channel_id ? `members override → <#${settings.member_log_channel_id}>` : settings.log_channel_id ? `default → <#${settings.log_channel_id}>` : "not set"}`,
          `**Moderation:** ${settings.moderation_log_channel_id ? `<#${settings.moderation_log_channel_id}> *(override)*` : settings.member_log_channel_id ? `members override → <#${settings.member_log_channel_id}>` : settings.log_channel_id ? `default → <#${settings.log_channel_id}>` : "not set"}`,
          `**Archive:** ${settings.archive_channel_id ? `<#${settings.archive_channel_id}> *(override)*` : settings.log_channel_id ? `default → <#${settings.log_channel_id}>` : "not set"}`,
          `**Voice HA target:** ${settings.startup_voice_channel_id ? `<#${settings.startup_voice_channel_id}>` : "disabled"}`,
          `**Startup log:** ${settings.startup_log_enabled ? "enabled" : "disabled"}`,
          `**Retention:** ${settings.message_retention_days === 0 ? "forever" : `${settings.message_retention_days} days`}`,
          `**Ignored message users/bots:** ${
            settings.ignored_message_user_ids.length
              ? settings.ignored_message_user_ids.map((id) => `<@${id}>`).join(", ")
              : "none"
          }`
        ].join("\n"),
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (sub === "history-scan") {
      const scope = interaction.options.getString("scope", true) as
        | "channel"
        | "server";
      const channel = interaction.options.getChannel("channel");
      const days = interaction.options.getInteger("days", true);
      const maxPerChannel = interaction.options.getInteger("max_per_channel") ?? 0;

      if (scope === "channel" && !channel) {
        await interaction.reply({
          content: "❌ `channel` is required when `scope` is `channel`.",
          flags: MessageFlags.Ephemeral
        });
        return;
      }

      try {
        startHistoryScan(interaction.client, interaction.guild, {
          scope,
          channelId: channel?.id ?? null,
          days,
          maxPerChannel
        });

        await interaction.reply({
          content: [
            "📥 **Local history scan started. Nothing is being uploaded to Supabase.**",
            `**Scope:** ${scope === "server" ? "whole server" : `<#${channel!.id}>`}`,
            `**History:** ${days === 0 ? "all available" : `${days} day(s)`}`,
            `**Per-channel cap:** ${maxPerChannel === 0 ? "unlimited" : maxPerChannel}`,
            "The result is saved as a compressed local JSONL staging file.",
            "Use `/logging history-status` to see message count and exact local size before deciding whether to import."
          ].join("\n"),
          flags: MessageFlags.Ephemeral
        });
      } catch (error) {
        await interaction.reply({
          content: `❌ Could not start history scan: ${error instanceof Error ? error.message : String(error)}`,
          flags: MessageFlags.Ephemeral
        });
      }
      return;
    }

    if (sub === "history-status") {
      const job = getHistoryJobStatus(interaction.guildId);
      const stage = await getLatestHistoryStage(interaction.guildId);

      if (!job && !stage) {
        await interaction.reply({
          content: "No local history scan exists yet.",
          flags: MessageFlags.Ephemeral
        });
        return;
      }

      if (job) {
        await interaction.reply({
          content: [
            `**Operation:** ${job.kind}`,
            `**Status:** ${job.status}`,
            `**Scope:** ${job.scope}`,
            `**Channels:** ${job.channelsDone}/${job.channelsTotal || "?"}`,
            `**Messages processed:** ${job.messagesProcessed}`,
            `**Current channel:** ${job.currentChannelId ? `<#${job.currentChannelId}>` : "none"}`,
            `**Raw JSON payload:** ${formatBytes(job.rawBytes)}`,
            `**Compressed local file:** ${job.compressedBytes ? formatBytes(job.compressedBytes) : "not finalized yet"}`,
            job.stagePath ? `**Local file:** \`${job.stagePath}\`` : null,
            job.dbBytesBefore !== null ? `**DB size before import:** ${formatBytes(job.dbBytesBefore)}` : null,
            job.dbBytesAfter !== null ? `**DB size after import:** ${formatBytes(job.dbBytesAfter)}` : null,
            job.dbBytesDelta !== null ? `**DB growth:** ${formatBytes(job.dbBytesDelta)}` : null,
            job.error ? `**Error:** ${job.error}` : null
          ].filter(Boolean).join("\n"),
          flags: MessageFlags.Ephemeral
        });
        return;
      }

      await interaction.reply({
        content: [
          "**Latest completed local scan:**",
          `**Messages:** ${stage!.messages}`,
          `**Channels:** ${stage!.channels}`,
          `**Raw JSON payload:** ${formatBytes(stage!.rawBytes)}`,
          `**Compressed local file:** ${formatBytes(stage!.compressedBytes)}`,
          `**Local file:** \`${stage!.stagePath}\``,
          "Nothing from this staged scan has to be uploaded until you explicitly run `/logging history-import confirm:true`."
        ].join("\n"),
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (sub === "history-import") {
      const confirm = interaction.options.getBoolean("confirm", true);
      if (!confirm) {
        await interaction.reply({
          content: "Import cancelled. Set `confirm:true` only after reviewing `/logging history-status`.",
          flags: MessageFlags.Ephemeral
        });
        return;
      }

      try {
        const stage = await getLatestHistoryStage(interaction.guildId);
        if (!stage) {
          throw new Error("No completed local history scan exists yet.");
        }

        await startHistoryImport(interaction.guild);
        await interaction.reply({
          content: [
            "⬆️ **Supabase history import started.**",
            `**Messages staged:** ${stage.messages}`,
            `**Raw payload:** ${formatBytes(stage.rawBytes)}`,
            `**Compressed local copy:** ${formatBytes(stage.compressedBytes)}`,
            "Use `/logging history-status` to watch progress and see the actual PostgreSQL size increase when finished."
          ].join("\n"),
          flags: MessageFlags.Ephemeral
        });
      } catch (error) {
        await interaction.reply({
          content: `❌ Could not start history import: ${error instanceof Error ? error.message : String(error)}`,
          flags: MessageFlags.Ephemeral
        });
      }
      return;
    }

    if (sub === "history-cancel") {
      const cancelled = cancelHistoryJob(interaction.guildId);
      await interaction.reply({
        content: cancelled
          ? "🛑 History scan/import cancellation requested. It will stop after the current Discord/API/database operation."
          : "There is no running history scan/import to cancel.",
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (sub === "archive") {
      const month = interaction.options.getString("month", true);
      const upload = interaction.options.getBoolean("upload") ?? false;

      await interaction.deferReply({ flags: MessageFlags.Ephemeral });

      try {
        const archivePath = await createGuildMonthArchive(interaction.guildId, month);
        const stat = await fs.stat(archivePath);

        let message = `✅ Archive created: \`${archivePath}\` (${Math.ceil(stat.size / 1024)} KiB).`;

        if (upload) {
          const settings = await getGuildSettings(interaction.guildId);
          const channelId = settings.archive_channel_id ?? settings.log_channel_id;

          if (!channelId) {
            message += "\n⚠️ No archive/message log channel is configured, so it was not uploaded.";
          } else {
            const channel = await interaction.client.channels.fetch(channelId).catch(() => null);

            if (!channel?.isTextBased() || channel.isDMBased()) {
              message += "\n⚠️ Configured archive channel is unavailable.";
            } else {
              try {
                await channel.send({
                  content: `📦 Nifibot archive for **${month}**`,
                  files: [new AttachmentBuilder(archivePath)]
                });
                message += `\n📤 Uploaded to <#${channelId}>.`;
              } catch {
                message += "\n⚠️ Discord rejected the upload (for example because of file-size limits). The local archive is still intact.";
              }
            }
          }
        }

        await interaction.editReply(message);
      } catch (error) {
        await interaction.editReply(
          `❌ Archive failed: ${error instanceof Error ? error.message : String(error)}`
        );
      }
    }
  }
};
