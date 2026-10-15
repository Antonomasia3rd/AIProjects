import type { Client } from "discord.js";
import { config } from "./config.js";
import { db, testDatabase } from "./db.js";
import { log } from "./logger.js";
import { LeaderElector } from "./cluster/leader.js";
import { createDiscordClient } from "./discordClient.js";
import { ComputerAgent } from "./computerAgent.js";
import { VoiceHaAgent } from "./voiceAgent.js";
import {
  acquireLocalInstanceLock,
  LocalInstanceAlreadyRunningError,
  type LocalInstanceLock
} from "./localInstance.js";

const elector = new LeaderElector();
const voiceAgent = new VoiceHaAgent();
let discord: Client | null = null;
let localInstanceLock: LocalInstanceLock | null = null;
let shuttingDown = false;

const EXIT_SELF_UPDATE = 75;

const computerAgent = new ComputerAgent((reason) => {
  const exitCode = reason.startsWith("self-update ") ? EXIT_SELF_UPDATE : 0;
  void shutdown(reason, exitCode);
});

async function startDiscord(signal?: AbortSignal): Promise<void> {
  if (discord) return;
  if (signal?.aborted || shuttingDown) throw new Error("Discord startup was cancelled.");
  const client = createDiscordClient();
  discord = client;
  const cancel = () => { client.destroy(); if (discord === client) discord = null; };
  signal?.addEventListener("abort", cancel, { once: true });
  try {
    await client.login(config.DISCORD_TOKEN);
    if (signal?.aborted || shuttingDown) throw new Error("Discord startup lease expired.");
  } catch (error) { cancel(); throw error; }
  finally { signal?.removeEventListener("abort", cancel); }
}

async function stopDiscord(): Promise<void> {
  if (!discord) return;

  discord.destroy();
  discord = null;
  log.info({ node: config.NODE_ID }, "Discord gateway stopped");
}

async function shutdown(signal: string, exitCode = 0): Promise<void> {
  if (shuttingDown) return;
  shuttingDown = true;

  log.info({ signal }, "Shutting down");
  await stopDiscord().catch(() => undefined);
  await voiceAgent.stop().catch(() => undefined);
  await computerAgent.stop().catch(() => undefined);
  await elector.stop().catch(() => undefined);
  await db.end().catch(() => undefined);
  await localInstanceLock?.close().catch(() => undefined);
  localInstanceLock = null;
  process.exit(exitCode);
}

process.on("SIGINT", () => void shutdown("SIGINT"));
process.on("SIGTERM", () => void shutdown("SIGTERM"));
process.on("message", message => {
  if (message && typeof message === "object" && "type" in message && message.type === "nifibot-shutdown")
    void shutdown("watchdog-request");
});
process.on("disconnect", () => void shutdown("watchdog-disconnected", 1));

process.on("unhandledRejection", (reason) => {
  log.error({ err: reason }, "Unhandled promise rejection");
});

process.on("uncaughtException", (error) => {
  log.fatal({ err: error }, "Uncaught exception");
  void shutdown("uncaughtException", 1);
});

async function main(): Promise<void> {
  let acquiredLock: LocalInstanceLock;
  try {
    acquiredLock = await acquireLocalInstanceLock(config.NODE_ID);
    localInstanceLock = acquiredLock;
  } catch (error) {
    if (error instanceof LocalInstanceAlreadyRunningError) {
      log.warn(
        { node: config.NODE_ID, endpoint: error.endpoint },
        "Another local Nifibot instance is already running; exiting duplicate process"
      );
      await shutdown("duplicate-instance", 0);
      return;
    }
    throw error;
  }

  await testDatabase();

  log.info(
    {
      node: config.NODE_ID,
      clusterEnabled: config.CLUSTER_ENABLED,
      localInstanceEndpoint: acquiredLock.endpoint
    },
    "Database reachable; bot process started"
  );

  await computerAgent.start();
  await voiceAgent.start();
  await elector.run(startDiscord, stopDiscord);
}

main().catch((error) => {
  log.fatal({ err: error }, "Fatal startup error");
  void shutdown("fatalStartup", 1);
});
