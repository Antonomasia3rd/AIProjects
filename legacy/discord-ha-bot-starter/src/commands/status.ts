import { MessageFlags, SlashCommandBuilder } from "discord.js";
import os from "node:os";
import type { BotCommand } from "./types.js";
import { config } from "../config.js";
import { db } from "../db.js";
import { getReconcileState } from "../auditTrail.js";
import { APP_VERSION } from "../version.js";
import { BUILD_FINGERPRINT } from "../buildFingerprint.js";

type LeaseRow = {
  lease_name: string;
  holder_id: string;
  epoch: string;
  lease_until: Date;
  updated_at: Date;
};

type NodeRow = {
  node_id: string;
  hostname: string;
  platform: string;
  os_release: string;
  process_id: number;
  last_seen: Date;
  current_version: string | null;
  metadata: Record<string, unknown>;
};

function age(date: Date): string {
  const seconds = Math.max(0, Math.floor((Date.now() - new Date(date).getTime()) / 1000));
  if (seconds < 60) return `${seconds}s ago`;
  const minutes = Math.floor(seconds / 60);
  return `${minutes}m ${seconds % 60}s ago`;
}

function leaseLine(name: string, lease: LeaseRow | undefined): string {
  if (!lease) return `**${name}:** *(no lease row yet)*`;
  const active = new Date(lease.lease_until).getTime() > Date.now();
  return `**${name}:** ${active ? "🟢" : "⚫"} \`${lease.holder_id}\` — epoch \`${lease.epoch}\` — ${active ? "expires" : "expired"} <t:${Math.floor(new Date(lease.lease_until).getTime()/1000)}:R>`;
}

export const statusCommand: BotCommand = {
  data: new SlashCommandBuilder()
    .setName("status")
    .setDescription("Show Nifibot runtime and HA cluster status."),

  async execute(interaction) {
    const mem = process.memoryUsage();
    const rssMiB = Math.round(mem.rss / 1024 / 1024);
    const uptime = Math.floor(process.uptime());

    const [leasesResult, nodesResult, reconcile] = await Promise.all([
      db.query<LeaseRow>(
        `SELECT lease_name, holder_id, epoch, lease_until, updated_at
         FROM cluster_leases
         WHERE lease_name IN ('discord-gateway', 'discord-voice')`
      ),
      db.query<NodeRow>(
        `SELECT node_id, hostname, platform, os_release, process_id, last_seen, current_version, metadata
         FROM computer_nodes
         ORDER BY last_seen DESC
         LIMIT 12`
      ),
      interaction.guildId ? getReconcileState(interaction.guildId) : Promise.resolve(null)
    ]);

    const leases = new Map(leasesResult.rows.map((row) => [row.lease_name, row]));

    const onlineNodes = nodesResult.rows.filter(
      (node) =>
        Date.now() - new Date(node.last_seen).getTime() <=
        Math.max(config.COMPUTER_AGENT_HEARTBEAT_MS * 3, 15_000)
    );
    const onlineFingerprints = onlineNodes.map((node) => {
      const value = node.metadata?.buildFingerprint;
      return typeof value === "string" && value ? value : null;
    });
    const knownFingerprints = onlineFingerprints.filter(
      (value): value is string => Boolean(value)
    );
    const uniqueFingerprints = new Set(knownFingerprints);
    const missingFingerprints = onlineFingerprints.length - knownFingerprints.length;

    const clusterBuildLine =
      onlineNodes.length === 0
        ? "**Cluster build consistency:** *(no online node heartbeats)*"
        : missingFingerprints > 0
          ? `**Cluster build consistency:** ⚠️ ${missingFingerprints}/${onlineNodes.length} online node(s) have no build fingerprint yet.`
          : uniqueFingerprints.size === 1
            ? `**Cluster build consistency:** ✅ ${onlineNodes.length}/${onlineNodes.length} online node(s) match build \`${knownFingerprints[0].slice(0, 12)}\`.`
            : `**Cluster build consistency:** ⚠️ **MIXED BUILDS** across ${onlineNodes.length} online node(s).`;

    const nodeLines = nodesResult.rows.length
      ? nodesResult.rows.map((node) => {
          const online = Date.now() - new Date(node.last_seen).getTime() <= Math.max(config.COMPUTER_AGENT_HEARTBEAT_MS * 3, 15_000);
          const here = node.node_id === config.NODE_ID ? " **← this process**" : "";
          const version = node.current_version ? `v${node.current_version}` : "version unknown";
          const fingerprint =
            typeof node.metadata?.buildFingerprint === "string"
              ? String(node.metadata.buildFingerprint).slice(0, 12)
              : "unknown";
          return `${online ? "🟢" : "⚫"} **${node.hostname}** — \`${node.node_id}\` — ${version} — build \`${fingerprint}\` — ${node.platform} ${node.os_release} — ${age(node.last_seen)}${here}`;
        })
      : ["*No computer-agent heartbeats found.*"];

    const reconcileLine = reconcile?.last_reconciled_at
      ? `<t:${Math.floor(new Date(reconcile.last_reconciled_at).getTime()/1000)}:R>`
      : "never / not initialized";

    await interaction.reply({
      content: [
        "### Nifibot HA status",
        `**This command gateway:** \`${config.NODE_ID}\` on \`${os.hostname()}\` — **v${APP_VERSION}**`,
        `**Running build:** \`${BUILD_FINGERPRINT.short}\` (${BUILD_FINGERPRINT.files} fingerprinted file(s))`,
        `**Process:** PID ${process.pid} • uptime ${uptime}s • RSS ${rssMiB} MiB • ${process.version}`,
        clusterBuildLine,
        "",
        leaseLine("Discord gateway owner", leases.get("discord-gateway")),
        leaseLine("Voice owner", leases.get("discord-voice")),
        `**Last startup reconciliation:** ${reconcileLine}`,
        "",
        "**Known Nifibot hosts:**",
        ...nodeLines
      ].join("\n").slice(0, 1950),
      flags: MessageFlags.Ephemeral
    });
  }
};
