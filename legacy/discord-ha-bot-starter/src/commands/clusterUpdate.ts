import crypto from "node:crypto";
import {
  MessageFlags,
  PermissionFlagsBits,
  SlashCommandBuilder
} from "discord.js";
import { config } from "../config.js";
import { db } from "../db.js";
import type { BotCommand } from "./types.js";

const ONLINE_WITHIN_MS = 20_000;
const MAX_UPDATE_BYTES = 8 * 1024 * 1024;

type ComputerNode = {
  node_id: string;
  hostname: string;
  current_version: string | null;
  last_seen: Date;
};

type LeaseRow = {
  lease_name: string;
  holder_id: string;
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

async function listNodes(): Promise<ComputerNode[]> {
  const result = await db.query<ComputerNode>(
    `
    SELECT node_id, hostname, current_version, last_seen
    FROM computer_nodes
    ORDER BY last_seen DESC, node_id
    `
  );
  return result.rows;
}

async function activeLeaseHolders(): Promise<Map<string, string>> {
  const result = await db.query<LeaseRow>(
    `
    SELECT lease_name, holder_id
    FROM cluster_leases
    WHERE lease_until > clock_timestamp()
      AND lease_name IN ('discord-gateway', 'discord-voice')
    `
  );
  return new Map(result.rows.map((row) => [row.lease_name, row.holder_id]));
}

async function storePackage(
  filename: string,
  bytes: Buffer,
  sha256: string,
  uploadedBy: string,
  reason: string
): Promise<string> {
  const result = await db.query<{ id: string }>(
    `
    INSERT INTO cluster_update_packages (
      filename, sha256, size_bytes, archive, uploaded_by, reason
    )
    VALUES ($1, $2, $3, $4, $5, $6)
    ON CONFLICT (sha256) DO UPDATE
    SET filename = EXCLUDED.filename,
        uploaded_by = EXCLUDED.uploaded_by,
        reason = EXCLUDED.reason
    RETURNING id
    `,
    [filename, sha256, bytes.length, bytes, uploadedBy, reason]
  );

  // Release ZIPs are small, but don't let the Postgres transport turn into an
  // unbounded archive. Keep the ten newest packages; historical source should
  // live in the user's normal backup/Syncthing/Git workflow instead.
  await db.query(`
    DELETE FROM cluster_update_packages AS package
    WHERE package.id NOT IN (
      SELECT id FROM cluster_update_packages ORDER BY created_at DESC LIMIT 10
    )
      AND NOT EXISTS (
        SELECT 1
        FROM computer_commands AS command
        WHERE command.action = 'self_update'
          AND command.status IN ('pending', 'claimed')
          AND command.payload->>'packageId' = package.id::text
      )
  `);

  return result.rows[0].id;
}

async function queueNodeUpdate(
  guildId: string,
  requestedBy: string,
  nodeId: string,
  packageId: string,
  reason: string,
  applyNotBeforeMs: number
): Promise<string> {
  const result = await db.query<{ id: string }>(
    `
    INSERT INTO computer_commands (
      guild_id, requested_by, target_node_id, action, payload, reason,
      expires_at
    )
    VALUES (
      $1, $2, $3, 'self_update', $4::jsonb, $5,
      clock_timestamp() + interval '30 days'
    )
    RETURNING id
    `,
    [
      guildId,
      requestedBy,
      nodeId,
      JSON.stringify({ packageId, applyNotBeforeMs }),
      reason
    ]
  );
  return result.rows[0].id;
}

export const clusterUpdateCommand: BotCommand = {
  data: new SlashCommandBuilder()
    .setName("cluster-update")
    .setDescription("Stage and roll out a Nifibot ZIP update to cluster nodes.")
    .setDefaultMemberPermissions(PermissionFlagsBits.Administrator)
    .addAttachmentOption((option) =>
      option
        .setName("package")
        .setDescription("Nifibot release/overlay ZIP. Never include .env.")
        .setRequired(true)
    )
    .addStringOption((option) =>
      option
        .setName("reason")
        .setDescription("Why this cluster update is being deployed.")
        .setRequired(true)
        .setMaxLength(400)
    )
    .addStringOption((option) =>
      option
        .setName("target")
        .setDescription("Node to update, or leave blank for every registered node.")
        .setAutocomplete(true)
        .setMaxLength(128)
    ),

  async autocomplete(interaction) {
    const focused = interaction.options.getFocused(true);
    if (focused.name !== "target") {
      await interaction.respond([]);
      return;
    }

    const query = String(focused.value ?? "").toLowerCase().trim();
    const nodes = await listNodes();
    const choices = [
      { name: "🌐 All registered Nifibot nodes (offline nodes update when they return)", value: "*" },
      ...nodes
        .filter((node) => {
          if (!query) return true;
          return (
            node.hostname.toLowerCase().includes(query) ||
            node.node_id.toLowerCase().includes(query)
          );
        })
        .map((node) => ({
          name: `${isOnline(node) ? "🟢" : "⚫"} ${node.hostname} — ${node.node_id}${
            node.current_version ? ` — v${node.current_version}` : ""
          }`.slice(0, 100),
          value: node.node_id
        }))
    ]
      .filter((choice) => !query || choice.value === "*" || choice.name.toLowerCase().includes(query))
      .slice(0, 25);

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

    const allowlist = allowedUsers();
    if (allowlist.size && !allowlist.has(interaction.user.id)) {
      await interaction.reply({
        content: "You are not in `COMPUTER_CONTROL_USER_IDS`.",
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    const attachment = interaction.options.getAttachment("package", true);
    const reason = interaction.options.getString("reason", true).trim();
    const requestedTarget = interaction.options.getString("target") ?? "*";

    if (!attachment.name.toLowerCase().endsWith(".zip")) {
      await interaction.reply({
        content: "The update package must be a `.zip` file.",
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    if (attachment.size > MAX_UPDATE_BYTES) {
      await interaction.reply({
        content: `Update ZIP is ${(attachment.size / 1024 / 1024).toFixed(1)} MiB; the current safety limit is ${MAX_UPDATE_BYTES / 1024 / 1024} MiB.`,
        flags: MessageFlags.Ephemeral
      });
      return;
    }

    await interaction.deferReply({ flags: MessageFlags.Ephemeral });

    const response = await fetch(attachment.url);
    if (!response.ok) {
      throw new Error(`Unable to download update attachment: HTTP ${response.status}.`);
    }
    const bytes = Buffer.from(await response.arrayBuffer());
    if (bytes.length > MAX_UPDATE_BYTES) {
      throw new Error("Downloaded update exceeds the configured safety limit.");
    }

    const sha256 = crypto.createHash("sha256").update(bytes).digest("hex");
    const packageId = await storePackage(
      attachment.name,
      bytes,
      sha256,
      interaction.user.id,
      reason
    );

    const allNodes = await listNodes();
    let targets =
      requestedTarget === "*"
        ? allNodes
        : allNodes.filter((node) => node.node_id.toLowerCase() === requestedTarget.toLowerCase());

    if (!targets.length) {
      await interaction.editReply(
        requestedTarget === "*"
          ? "No computer agents have registered yet."
          : `No registered node matches \`${requestedTarget}\`.`
      );
      return;
    }

    // Roll normal standbys first. Gateway and voice owners go last so another
    // freshly-updated node has time to become available before they restart.
    const leases = await activeLeaseHolders();
    const gatewayOwner = leases.get("discord-gateway");
    const voiceOwner = leases.get("discord-voice");
    const priority = (node: ComputerNode) =>
      (node.node_id === gatewayOwner ? 2 : 0) +
      (node.node_id === voiceOwner ? 1 : 0);
    targets = targets.sort((a, b) => priority(a) - priority(b));

    const baseApplyAt = Date.now() + 5_000;
    const scheduled: string[] = [];
    for (const [index, node] of targets.entries()) {
      const applyNotBeforeMs = baseApplyAt + index * 45_000;
      const commandId = await queueNodeUpdate(
        interaction.guildId,
        interaction.user.id,
        node.node_id,
        packageId,
        reason,
        applyNotBeforeMs
      );
      scheduled.push(
        `• ${isOnline(node) ? "🟢" : "⚫"} **${node.hostname}** (\`${node.node_id}\`)${
          node.current_version ? ` currently v${node.current_version}` : ""
        } — command \`${commandId}\` — restart not before <t:${Math.floor(applyNotBeforeMs / 1000)}:T>`
      );
    }

    await interaction.editReply(
      [
        "🚀 **Cluster update queued.**",
        `**Package:** ${attachment.name}`,
        `**SHA-256:** \`${sha256}\``,
        `**Stored package ID:** \`${packageId}\``,
        `**Reason:** ${reason}`,
        "",
        ...scheduled,
        "",
        "Each node validates the ZIP, stages a full local copy, runs `npm run build`, migrations and slash-command deployment **before** replacing its live files. `.env` and local archives/logs are preserved. Standbys/offline nodes are queued first; active gateway/voice owners are delayed. Offline registered nodes keep the command for up to 30 days and update when their agent returns."
      ]
        .join("\n")
        .slice(0, 1900)
    );
  }
};
