import {
  MessageFlags,
  PermissionFlagsBits,
  SlashCommandBuilder,
  type Attachment,
  type ChatInputCommandInteraction
} from "discord.js";
import type { BotCommand } from "./types.js";
import { config } from "../config.js";

const MAX_IMAGE_BYTES = 10 * 1024 * 1024;
const ALLOWED_IMAGE_TYPES = new Set([
  "image/png",
  "image/jpeg",
  "image/gif",
  "image/webp"
]);

type RawCurrentUser = {
  id: string;
  username: string;
  avatar?: string | null;
  banner?: string | null;
};

type RawCurrentGuildMember = {
  nick?: string | null;
  avatar?: string | null;
  banner?: string | null;
  bio?: string | null;
};

function ensureHomeGuild(interaction: ChatInputCommandInteraction): void {
  if (!config.DISCORD_GUILD_ID || interaction.guildId !== config.DISCORD_GUILD_ID) {
    throw new Error(
      "Global bot-profile changes are only allowed from Nifibot's configured home guild."
    );
  }
}

async function attachmentToDataUri(attachment: Attachment): Promise<string> {
  if (attachment.size > MAX_IMAGE_BYTES) {
    throw new Error("Image is larger than 10 MiB.");
  }

  const contentType = attachment.contentType?.split(";", 1)[0]?.toLowerCase();
  if (!contentType || !ALLOWED_IMAGE_TYPES.has(contentType)) {
    throw new Error(
      "Unsupported image type. Use PNG, JPEG, GIF, or WebP (maximum 10 MiB)."
    );
  }

  const response = await fetch(attachment.url);
  if (!response.ok) {
    throw new Error(
      `Discord attachment download failed (${response.status} ${response.statusText}).`
    );
  }

  const bytes = Buffer.from(await response.arrayBuffer());
  if (bytes.length > MAX_IMAGE_BYTES) {
    throw new Error("Downloaded image is larger than 10 MiB.");
  }

  return `data:${contentType};base64,${bytes.toString("base64")}`;
}

function formatError(error: unknown): string {
  if (error instanceof Error) return error.message;
  return String(error);
}

async function editReplyError(
  interaction: ChatInputCommandInteraction,
  error: unknown
): Promise<void> {
  await interaction.editReply(
    `❌ Profile change failed: ${formatError(error).slice(0, 1800)}`
  );
}

async function handleServerStatus(
  interaction: ChatInputCommandInteraction
): Promise<void> {
  if (!interaction.guildId || !interaction.guild) {
    await interaction.editReply("This command only works in a server.");
    return;
  }

  // Discord only accepts `@me` on the PATCH "Modify Current Member" route.
  // GET "Guild Member" requires a real user snowflake, so use the bot user ID.
  const raw = (await interaction.client.rest.get(
    `/guilds/${interaction.guildId}/members/${interaction.client.user.id}`
  )) as RawCurrentGuildMember;

  const me = interaction.guild.members.me;
  const canChangeNick = me?.permissions.has(PermissionFlagsBits.ChangeNickname) ?? false;

  await interaction.editReply([
    "**Nifibot server profile**",
    `**Server:** ${interaction.guild.name} (${interaction.guildId})`,
    `**Nickname:** ${raw.nick ?? "*(none — using global username)*"}`,
    `**Server avatar:** ${raw.avatar ? "custom" : "inherited from global profile"}`,
    `**Server banner:** ${raw.banner ? "custom" : "inherited / none"}`,
    "**Server bio:** *(Discord's GET Guild Member response does not expose the guild bio; set/reset is still supported.)*",
    `**Nifibot has Change Nickname:** ${canChangeNick ? "yes" : "no"}`
  ].join("\n"));
}

async function handleServerSet(
  interaction: ChatInputCommandInteraction
): Promise<void> {
  if (!interaction.guildId || !interaction.guild) {
    await interaction.editReply("This command only works in a server.");
    return;
  }

  const nickname = interaction.options.getString("nickname");
  const bio = interaction.options.getString("bio");
  const avatar = interaction.options.getAttachment("avatar");
  const banner = interaction.options.getAttachment("banner");
  const reason = interaction.options.getString("reason") ?? "Nifibot /bot-profile server set";

  if (nickname === null && bio === null && !avatar && !banner) {
    await interaction.editReply(
      "Nothing to change. Supply at least one of `nickname`, `bio`, `avatar`, or `banner`."
    );
    return;
  }

  const body: Record<string, string | null> = {};
  if (nickname !== null) body.nick = nickname;
  if (bio !== null) body.bio = bio;
  if (avatar) body.avatar = await attachmentToDataUri(avatar);
  if (banner) body.banner = await attachmentToDataUri(banner);

  await interaction.client.rest.patch(
    `/guilds/${interaction.guildId}/members/@me`,
    { body, reason }
  );

  await interaction.guild.members.fetchMe({ force: true }).catch(() => undefined);

  const changed = [
    nickname !== null ? "nickname" : null,
    bio !== null ? "bio" : null,
    avatar ? "server avatar" : null,
    banner ? "server banner" : null
  ].filter(Boolean);

  await interaction.editReply(
    `✅ Updated Nifibot's **server-specific profile**: ${changed.join(", ")}.`
  );
}

async function handleServerReset(
  interaction: ChatInputCommandInteraction
): Promise<void> {
  if (!interaction.guildId || !interaction.guild) {
    await interaction.editReply("This command only works in a server.");
    return;
  }

  const field = interaction.options.getString("field", true);
  const reason = interaction.options.getString("reason") ?? `Nifibot /bot-profile server reset ${field}`;
  const body: Record<string, null> = {};

  if (field === "nickname") body.nick = null;
  else if (field === "avatar") body.avatar = null;
  else if (field === "banner") body.banner = null;
  else if (field === "bio") body.bio = null;
  else throw new Error(`Unknown server profile field: ${field}`);

  await interaction.client.rest.patch(
    `/guilds/${interaction.guildId}/members/@me`,
    { body, reason }
  );

  await interaction.guild.members.fetchMe({ force: true }).catch(() => undefined);
  await interaction.editReply(`✅ Reset Nifibot's server-specific **${field}**.`);
}

async function handleGlobalStatus(
  interaction: ChatInputCommandInteraction
): Promise<void> {
  ensureHomeGuild(interaction);

  const raw = (await interaction.client.rest.get("/users/@me")) as RawCurrentUser;
  await interaction.editReply([
    "**Nifibot global bot profile**",
    `**Username:** ${raw.username}`,
    `**Global avatar:** ${raw.avatar ? "set" : "default"}`,
    `**Global banner:** ${raw.banner ? "set" : "none"}`,
    "These values affect Nifibot everywhere. Server-specific profile fields override them where supported."
  ].join("\n"));
}

async function handleGlobalSet(
  interaction: ChatInputCommandInteraction
): Promise<void> {
  ensureHomeGuild(interaction);

  const username = interaction.options.getString("username");
  const avatar = interaction.options.getAttachment("avatar");
  const banner = interaction.options.getAttachment("banner");
  const confirm = interaction.options.getBoolean("confirm") ?? false;

  if (username === null && !avatar && !banner) {
    await interaction.editReply(
      "Nothing to change. Supply at least one of `username`, `avatar`, or `banner`."
    );
    return;
  }

  if (username !== null && !confirm) {
    await interaction.editReply(
      "⚠️ Changing the bot username is global and Discord may rate-limit/cool down renames. Re-run with `confirm:true`."
    );
    return;
  }

  const body: Record<string, string | null> = {};
  if (username !== null) body.username = username;
  if (avatar) body.avatar = await attachmentToDataUri(avatar);
  if (banner) body.banner = await attachmentToDataUri(banner);

  await interaction.client.rest.patch("/users/@me", { body });
  await interaction.client.user.fetch(true).catch(() => undefined);

  const changed = [
    username !== null ? "username" : null,
    avatar ? "global avatar" : null,
    banner ? "global banner" : null
  ].filter(Boolean);

  await interaction.editReply(
    `✅ Updated Nifibot's **global bot profile**: ${changed.join(", ")}.`
  );
}

async function handleGlobalReset(
  interaction: ChatInputCommandInteraction
): Promise<void> {
  ensureHomeGuild(interaction);

  const field = interaction.options.getString("field", true);
  const body: Record<string, null> = {};

  if (field === "avatar") body.avatar = null;
  else if (field === "banner") body.banner = null;
  else throw new Error(`Unknown global profile field: ${field}`);

  await interaction.client.rest.patch("/users/@me", { body });
  await interaction.client.user.fetch(true).catch(() => undefined);
  await interaction.editReply(`✅ Reset Nifibot's global **${field}**.`);
}

export const profileCommand: BotCommand = {
  data: new SlashCommandBuilder()
    .setName("bot-profile")
    .setDescription("View or customize Nifibot's global and per-server profile.")
    .setDefaultMemberPermissions(PermissionFlagsBits.ManageGuild)

    .addSubcommandGroup((group) =>
      group
        .setName("server")
        .setDescription("Customize Nifibot only in this server.")
        .addSubcommand((sub) =>
          sub
            .setName("status")
            .setDescription("Show Nifibot's current server-specific profile.")
        )
        .addSubcommand((sub) =>
          sub
            .setName("set")
            .setDescription("Set one or more server-specific profile fields.")
            .addStringOption((option) =>
              option
                .setName("nickname")
                .setDescription("Server-specific nickname (requires Change Nickname for Nifibot).")
                .setMaxLength(32)
            )
            .addStringOption((option) =>
              option
                .setName("bio")
                .setDescription("Server-specific About Me / bio.")
                .setMaxLength(190)
            )
            .addAttachmentOption((option) =>
              option
                .setName("avatar")
                .setDescription("Server-specific avatar (PNG/JPEG/GIF/WebP, max 10 MiB).")
            )
            .addAttachmentOption((option) =>
              option
                .setName("banner")
                .setDescription("Server-specific banner (PNG/JPEG/GIF/WebP, max 10 MiB).")
            )
            .addStringOption((option) =>
              option
                .setName("reason")
                .setDescription("Optional audit-log reason.")
                .setMaxLength(512)
            )
        )
        .addSubcommand((sub) =>
          sub
            .setName("reset")
            .setDescription("Reset one server-specific field to its global/default value.")
            .addStringOption((option) =>
              option
                .setName("field")
                .setDescription("Server-specific field to reset.")
                .setRequired(true)
                .addChoices(
                  { name: "Nickname", value: "nickname" },
                  { name: "Avatar", value: "avatar" },
                  { name: "Banner", value: "banner" },
                  { name: "Bio", value: "bio" }
                )
            )
            .addStringOption((option) =>
              option
                .setName("reason")
                .setDescription("Optional audit-log reason.")
                .setMaxLength(512)
            )
        )
    )

    .addSubcommandGroup((group) =>
      group
        .setName("global")
        .setDescription("Customize Nifibot's global Discord bot profile.")
        .addSubcommand((sub) =>
          sub
            .setName("status")
            .setDescription("Show Nifibot's current global bot profile.")
        )
        .addSubcommand((sub) =>
          sub
            .setName("set")
            .setDescription("Set global username/avatar/banner.")
            .addStringOption((option) =>
              option
                .setName("username")
                .setDescription("Global bot username; affects every server.")
                .setMinLength(2)
                .setMaxLength(32)
            )
            .addAttachmentOption((option) =>
              option
                .setName("avatar")
                .setDescription("Global avatar (PNG/JPEG/GIF/WebP, max 10 MiB).")
            )
            .addAttachmentOption((option) =>
              option
                .setName("banner")
                .setDescription("Global banner (PNG/JPEG/GIF/WebP, max 10 MiB).")
            )
            .addBooleanOption((option) =>
              option
                .setName("confirm")
                .setDescription("Required when changing the global username.")
            )
        )
        .addSubcommand((sub) =>
          sub
            .setName("reset")
            .setDescription("Reset a global visual profile field.")
            .addStringOption((option) =>
              option
                .setName("field")
                .setDescription("Global field to reset.")
                .setRequired(true)
                .addChoices(
                  { name: "Avatar", value: "avatar" },
                  { name: "Banner", value: "banner" }
                )
            )
        )
    ),

  async execute(interaction) {
    await interaction.deferReply({ flags: MessageFlags.Ephemeral });

    try {
      const group = interaction.options.getSubcommandGroup(true);
      const sub = interaction.options.getSubcommand(true);

      if (group === "server" && sub === "status") {
        await handleServerStatus(interaction);
        return;
      }
      if (group === "server" && sub === "set") {
        await handleServerSet(interaction);
        return;
      }
      if (group === "server" && sub === "reset") {
        await handleServerReset(interaction);
        return;
      }
      if (group === "global" && sub === "status") {
        await handleGlobalStatus(interaction);
        return;
      }
      if (group === "global" && sub === "set") {
        await handleGlobalSet(interaction);
        return;
      }
      if (group === "global" && sub === "reset") {
        await handleGlobalReset(interaction);
        return;
      }

      await interaction.editReply("Unknown profile action.");
    } catch (error) {
      await editReplyError(interaction, error);
    }
  }
};
