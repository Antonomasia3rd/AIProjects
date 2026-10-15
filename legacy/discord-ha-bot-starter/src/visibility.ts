import {
  PermissionFlagsBits,
  type Guild
} from "discord.js";

type PermissionView = {
  has(permission: bigint): boolean;
};

type PermissionAwareChannel = {
  id?: unknown;
  permissionsFor?: (member: unknown) => PermissionView | null;
};

function permissionViewForBot(
  guild: Guild,
  channel: unknown
): PermissionView | null | undefined {
  const me = guild.members.me;
  if (!me) return null;
  if (!channel || typeof channel !== "object") return null;

  const candidate = channel as PermissionAwareChannel;
  if (typeof candidate.permissionsFor !== "function") {
    // Some message-capable structures may not expose permissionsFor through a
    // narrowed structural type. Don't invent a denial; the REST operation is
    // still protected by a per-channel error boundary.
    return undefined;
  }

  return candidate.permissionsFor(me);
}

export function botCanViewChannel(guild: Guild, channel: unknown): boolean {
  const permissions = permissionViewForBot(guild, channel);
  if (permissions === undefined) return true;
  return Boolean(permissions?.has(PermissionFlagsBits.ViewChannel));
}

export function botCanReadMessageHistory(
  guild: Guild,
  channel: unknown
): boolean {
  const permissions = permissionViewForBot(guild, channel);
  if (permissions === undefined) return true;
  return Boolean(
    permissions?.has(PermissionFlagsBits.ViewChannel) &&
      permissions.has(PermissionFlagsBits.ReadMessageHistory)
  );
}

export function hasAuditLogPermission(guild: Guild): boolean {
  return Boolean(
    guild.members.me?.permissions.has(PermissionFlagsBits.ViewAuditLog)
  );
}

export function compactDiscordError(
  error: unknown
): Record<string, unknown> {
  if (!error || typeof error !== "object") {
    return { message: String(error) };
  }

  const value = error as {
    name?: unknown;
    message?: unknown;
    code?: unknown;
    status?: unknown;
  };

  return {
    name: typeof value.name === "string" ? value.name : undefined,
    message:
      typeof value.message === "string" ? value.message : String(error),
    code: value.code,
    status: value.status
  };
}
