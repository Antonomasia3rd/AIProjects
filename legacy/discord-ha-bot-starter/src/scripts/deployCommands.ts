import { REST, Routes } from "discord.js";
import { config } from "../config.js";
import { commands } from "../commands/index.js";

async function main() {
  if (!config.DISCORD_GUILD_ID) {
    throw new Error(
      "DISCORD_GUILD_ID is required by this starter deploy script. " +
      "Use a development/test guild first."
    );
  }

  const rest = new REST({ version: "10" }).setToken(config.DISCORD_TOKEN);
  const body = commands.map((command) => command.data.toJSON());

  await rest.put(
    Routes.applicationGuildCommands(
      config.DISCORD_CLIENT_ID,
      config.DISCORD_GUILD_ID
    ),
    { body }
  );

  console.log(`Deployed ${body.length} guild command(s).`);
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
