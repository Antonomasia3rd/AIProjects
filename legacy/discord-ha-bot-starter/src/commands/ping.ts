import { SlashCommandBuilder } from "discord.js";
import type { BotCommand } from "./types.js";

export const pingCommand: BotCommand = {
  data: new SlashCommandBuilder()
    .setName("ping")
    .setDescription("Check whether the bot is alive."),

  async execute(interaction) {
    const sent = await interaction.reply({
      content: "Pinging…",
      withResponse: true
    });

    const responseMessage = sent.resource?.message;
    const latency = responseMessage
      ? responseMessage.createdTimestamp - interaction.createdTimestamp
      : 0;

    await interaction.editReply(
      `🏓 Alive. Interaction latency: ${latency} ms`
    );
  }
};
