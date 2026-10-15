import {
  EmbedBuilder,
  MessageFlags,
  PermissionFlagsBits,
  SlashCommandBuilder
} from "discord.js";
import { db } from "../db.js";
import { config } from "../config.js";
import { getGuildSettings } from "../guildSettings.js";
import type { BotCommand } from "./types.js";

const wait = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

const ONLINE_WITHIN_MS = 20_000;

type ComputerNode = {
  node_id: string;
  hostname: string;
  platform: string;
  os_release: string;
  arch: string;
  process_id: number;
  last_seen: Date;
  current_version: string | null;
  metadata: Record<string, unknown>;
};

type ComputerCommandStatus = {
  id: string;
  status: "pending" | "claimed" | "completed" | "failed" | "expired";
  claimed_by: string | null;
  completed_at: Date | null;
  result: Record<string, unknown> | null;
};

function allowedUsers(): Set<string> {
  return new Set(
    config.COMPUTER_CONTROL_USER_IDS.split(",")
      .map((value) => value.trim())
      .filter(Boolean)
  );
}

function isOnline(node: ComputerNode): boolean {
  return Date.now() - new Date(node.last_seen).getTime() <= ONLINE_WITHIN_MS;
}

function formatAge(date: Date): string {
  const seconds = Math.max(0, Math.round((Date.now() - new Date(date).getTime()) / 1000));
  return seconds < 60 ? `${seconds}s ago` : `${Math.floor(seconds / 60)}m ${seconds % 60}s ago`;
}

async function listNodes(): Promise<ComputerNode[]> {
  const result = await db.query<ComputerNode>(
    `
    SELECT node_id, hostname, platform, os_release, arch, process_id,
           last_seen, current_version, metadata
    FROM computer_nodes
    ORDER BY last_seen DESC, node_id
    `
  );
  return result.rows;
}

async function resolveTarget(target: string): Promise<ComputerNode | null> {
  const result = await db.query<ComputerNode>(
    `
    SELECT node_id, hostname, platform, os_release, arch, process_id,
           last_seen, current_version, metadata
    FROM computer_nodes
    WHERE lower(node_id) = lower($1)
       OR lower(hostname) = lower($1)
    ORDER BY last_seen DESC
    LIMIT 1
    `,
    [target]
  );
  return result.rows[0] ?? null;
}

async function enqueue(
  guildId: string,
  requestedBy: string,
  targetNodeId: string,
  action: string,
  payload: Record<string, unknown>,
  reason: string
): Promise<string> {
  const result = await db.query<{ id: string }>(
    `
    INSERT INTO computer_commands (
      guild_id, requested_by, target_node_id, action, payload, reason,
      expires_at
    )
    VALUES ($1, $2, $3, $4, $5::jsonb, $6,
            clock_timestamp() + interval '2 minutes')
    RETURNING id
    `,
    [guildId, requestedBy, targetNodeId, action, JSON.stringify(payload), reason]
  );
  return result.rows[0].id;
}

async function waitForResult(id: string, timeoutMs = 15_000): Promise<ComputerCommandStatus | null> {
  const deadline = Date.now() + timeoutMs;

  while (Date.now() < deadline) {
    const result = await db.query<ComputerCommandStatus>(
      `
      SELECT id, status, claimed_by, completed_at, result
      FROM computer_commands
      WHERE id = $1
      `,
      [id]
    );
    const row = result.rows[0];
    if (!row) return null;
    if (["completed", "failed", "expired"].includes(row.status)) return row;
    await wait(500);
  }
  return null;
}

function resultSummary(row: ComputerCommandStatus | null): string {
  if (!row) return "The target did not return a result before the interaction timeout.";
  if (row.status === "expired") return "The command expired before the target claimed it.";
  const result = row.result ?? {};
  if (row.status === "failed") return String(result.error ?? "Command failed.");
  return String(result.summary ?? "Command completed.");
}

async function sendComputerAudit(
  interaction: Parameters<BotCommand["execute"]>[0],
  target: ComputerNode,
  action: string,
  reason: string,
  commandId: string,
  row: ComputerCommandStatus | null
): Promise<void> {
  if (!interaction.guildId) return;
  const settings = await getGuildSettings(interaction.guildId);
  if (!settings.logging_enabled) return;

  const channelId =
    settings.moderation_log_channel_id ??
    settings.log_channel_id;
  if (!channelId) return;

  const channel = await interaction.client.channels.fetch(channelId).catch(() => null);
  if (!channel?.isTextBased() || channel.isDMBased()) return;

  const ok = row?.status === "completed";
  const embed = new EmbedBuilder()
    .setTitle(ok ? "🖥️ Computer command completed" : "⚠️ Computer command")
    .addFields(
      {
        name: "Requested by",
        value: `<@${interaction.user.id}> (\`${interaction.user.id}\`)`
      },
      {
        name: "Target",
        value: `**${target.hostname}**\nNode: \`${target.node_id}\``
      },
      { name: "Action", value: `\`${action}\`` },
      { name: "Reason", value: reason || "*No reason required*" },
      { name: "Result", value: resultSummary(row).slice(0, 1024) },
      { name: "Command ID", value: `\`${commandId}\`` }
    )
    .setTimestamp();

  await channel.send({ embeds: [embed], allowedMentions: { parse: [] } }).catch(() => undefined);
}

export const computerCommand: BotCommand = {
  data: new SlashCommandBuilder()
    .setName("computer")
    .setDescription("Control or inspect a Nifibot host computer.")
    .setDefaultMemberPermissions(PermissionFlagsBits.Administrator)
    .addStringOption((option) =>
      option
        .setName("action")
        .setDescription("Computer action.")
        .setRequired(true)
        .addChoices(
          { name: "List registered computers", value: "list" },
          { name: "Live status", value: "status" },
          { name: "Sleep", value: "sleep" },
          { name: "Lock workstation", value: "lock" },
          { name: "Change allowlisted setting", value: "settings" }
        )
    )
    .addStringOption((option) =>
      option
        .setName("target")
        .setDescription("Nifibot computer (autocomplete by hostname or NODE_ID).")
        .setAutocomplete(true)
        .setMaxLength(128)
    )
    .addStringOption((option) =>
      option
        .setName("reason")
        .setDescription("Required for actions that change the computer.")
        .setMaxLength(400)
    )
    .addStringOption((option) =>
      option
        .setName("key")
        .setDescription("Setting to change (settings action only).")
        .addChoices({ name: "Power mode / overlay", value: "power_mode" })
    )
    .addStringOption((option) =>
      option
        .setName("value")
        .setDescription("Setting value (settings action only).")
        .addChoices(
          { name: "Best power efficiency / Better battery", value: "better_battery" },
          { name: "Balanced", value: "balanced" },
          { name: "Best performance", value: "best_performance" }
        )
    )
    .addStringOption((option) =>
      option
        .setName("applies_to")
        .setDescription("Power source to configure.")
        .addChoices(
          { name: "AC / plugged in", value: "AC" },
          { name: "DC / battery", value: "DC" },
          { name: "Both", value: "BOTH" }
        )
    ),

  async autocomplete(interaction) {
    const focused = interaction.options.getFocused(true);
    if (focused.name !== "target") {
      await interaction.respond([]);
      return;
    }

    const query = String(focused.value ?? "").trim().toLowerCase();
    const nodes = await listNodes();
    const choices = nodes
      .filter((node) => {
        if (!query) return true;
        return (
          node.hostname.toLowerCase().includes(query) ||
          node.node_id.toLowerCase().includes(query)
        );
      })
      .slice(0, 25)
      .map((node) => ({
        name: `${isOnline(node) ? "🟢" : "⚫"} ${node.hostname} — ${node.node_id}${
          node.current_version ? ` — v${node.current_version}` : ""
        }`.slice(0, 100),
        value: node.node_id
      }));

    await interaction.respond(choices);
  },

  async execute(interaction) {
    if (!interaction.guildId) {
      await interaction.reply({
        content: "This command only works in a server.",
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (!config.COMPUTER_CONTROL_ENABLED) {
      await interaction.reply({
        content: "Computer control is disabled on this Nifibot deployment.",
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    const allowlist = allowedUsers();
    if (allowlist.size && !allowlist.has(interaction.user.id)) {
      await interaction.reply({
        content: "You are not in `COMPUTER_CONTROL_USER_IDS`.",
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    const action = interaction.options.getString("action", true);

    if (action === "list") {
      const nodes = await listNodes();
      const lines = nodes.length
        ? nodes.map((node) => {
            const online = isOnline(node);
            const version = node.current_version ? ` — v${node.current_version}` : "";
            const fingerprint =
              typeof node.metadata?.buildFingerprint === "string"
                ? ` — build \`${String(node.metadata.buildFingerprint).slice(0, 12)}\``
                : " — build unknown";
            return `${online ? "🟢" : "⚫"} **${node.hostname}** — \`${node.node_id}\`${version}${fingerprint} — ${node.platform} ${node.os_release} — last seen ${formatAge(node.last_seen)}`;
          })
        : ["No computer agents have registered yet."];

      await interaction.reply({
        content: lines.join("\n").slice(0, 1900),
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    const targetInput = interaction.options.getString("target");
    if (!targetInput) {
      await interaction.reply({
        content: "`target` is required for this action. Use `/computer action:list` to see targets.",
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    const target = await resolveTarget(targetInput);
    if (!target) {
      await interaction.reply({
        content: `No registered computer matches \`${targetInput}\`. Use \`/computer action:list\`.`,
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (!isOnline(target)) {
      await interaction.reply({
        content: `⚫ **${target.hostname}** (\`${target.node_id}\`) is stale/offline; last heartbeat ${formatAge(target.last_seen)}. Command not queued.`,
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    const mutating = ["sleep", "lock", "settings"].includes(action);
    const reason = interaction.options.getString("reason")?.trim() ?? "";
    if (mutating && !reason) {
      await interaction.reply({
        content: "`reason` is required for sleep, lock, and settings changes.",
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    const payload: Record<string, unknown> = {};
    if (action === "settings") {
      const key = interaction.options.getString("key");
      const value = interaction.options.getString("value");
      const appliesTo = interaction.options.getString("applies_to");
      if (!key || !value || !appliesTo) {
        await interaction.reply({
          content: "`settings` requires `key`, `value`, and `applies_to`.",
          flags: MessageFlags.Ephemeral
        });
        return;
      }
      payload.key = key;
      payload.value = value;
      payload.appliesTo = appliesTo;
    }

    await interaction.deferReply({ flags: MessageFlags.Ephemeral });

    const commandId = await enqueue(
      interaction.guildId,
      interaction.user.id,
      target.node_id,
      action,
      payload,
      reason || "status request"
    );

    const row = await waitForResult(commandId);
    const summary = resultSummary(row);

    await interaction.editReply(
      [
        row?.status === "completed" ? "✅ **Computer command completed.**" : "⚠️ **Computer command result.**",
        `**Target:** ${target.hostname} (\`${target.node_id}\`)`,
        `**Action:** \`${action}\``,
        mutating ? `**Reason:** ${reason}` : null,
        `**Result:** ${summary}`,
        `**Command ID:** \`${commandId}\``
      ]
        .filter(Boolean)
        .join("\n")
        .slice(0, 1900)
    );

    await sendComputerAudit(interaction, target, action, reason, commandId, row);
  }
};
