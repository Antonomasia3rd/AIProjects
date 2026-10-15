import type { BotCommand } from "./types.js";
import { pingCommand } from "./ping.js";
import { statusCommand } from "./status.js";
import { configLoggingCommand } from "./configLogging.js";
import { computerCommand } from "./computer.js";
import { clusterUpdateCommand } from "./clusterUpdate.js";
import { profileCommand } from "./profile.js";

export const commands: BotCommand[] = [
  pingCommand,
  statusCommand,
  configLoggingCommand,
  computerCommand,
  clusterUpdateCommand,
  profileCommand
];

export const commandMap = new Map(
  commands.map((command) => [command.data.name, command])
);
