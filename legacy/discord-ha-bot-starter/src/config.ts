import "dotenv/config";
import { z } from "zod";

const boolString = z.string().transform((v) => v.toLowerCase() === "true");

const schema = z.object({
  DISCORD_TOKEN: z.string().min(20),
  DISCORD_CLIENT_ID: z.string().min(5),
  DISCORD_GUILD_ID: z.string().optional(),
  DATABASE_URL: z.string().min(10),
  DATABASE_SSL: boolString.default(true),
  NODE_ID: z.string().min(1),
  CLUSTER_ENABLED: boolString.default(true),
  CLUSTER_LEASE_MS: z.coerce.number().int().min(10000).default(20000),
  CLUSTER_RENEW_MS: z.coerce.number().int().min(1000).default(5000),
  CLUSTER_STANDBY_POLL_MS: z.coerce.number().int().min(1000).default(4000),
  CLUSTER_FAILSAFE_MS: z.coerce.number().int().min(1000).default(10000),
  MESSAGE_CACHE_RETENTION_DAYS: z.coerce.number().int().min(0).max(3650).default(0),
  STARTUP_CATCHUP_ENABLED: boolString.default(true),
  STARTUP_BOOTSTRAP_MESSAGES: z.coerce.number().int().min(0).max(1000).default(100),
  COMPUTER_CONTROL_ENABLED: boolString.default(true),
  COMPUTER_CONTROL_USER_IDS: z.string().default(""),
  COMPUTER_AGENT_POLL_MS: z.coerce.number().int().min(750).max(30000).default(2000),
  COMPUTER_AGENT_HEARTBEAT_MS: z.coerce.number().int().min(2000).max(60000).default(5000),
  VOICE_HA_ENABLED: boolString.default(true),
  VOICE_LEASE_MS: z.coerce.number().int().min(8000).max(60000).default(12000),
  VOICE_RENEW_MS: z.coerce.number().int().min(1000).max(15000).default(3000),
  VOICE_STANDBY_POLL_MS: z.coerce.number().int().min(1000).max(15000).default(2000),
  VOICE_FAILSAFE_MS: z.coerce.number().int().min(2000).max(30000).default(7000),
  VOICE_READY_TIMEOUT_MS: z.coerce.number().int().min(3000).max(30000).default(6000),
  VOICE_AGENT_START_DELAY_MS: z.coerce.number().int().min(0).max(30000).default(6000),
  LOG_LEVEL: z.string().default("info")
});

export const config = schema.parse(process.env);

if (config.CLUSTER_FAILSAFE_MS >= config.CLUSTER_LEASE_MS) {
  throw new Error("CLUSTER_FAILSAFE_MS must be smaller than CLUSTER_LEASE_MS.");
}

if (config.CLUSTER_RENEW_MS >= config.CLUSTER_FAILSAFE_MS) {
  throw new Error("CLUSTER_RENEW_MS must be smaller than CLUSTER_FAILSAFE_MS.");
}

if (config.VOICE_FAILSAFE_MS >= config.VOICE_LEASE_MS) {
  throw new Error("VOICE_FAILSAFE_MS must be smaller than VOICE_LEASE_MS.");
}

if (config.VOICE_RENEW_MS >= config.VOICE_FAILSAFE_MS) {
  throw new Error("VOICE_RENEW_MS must be smaller than VOICE_FAILSAFE_MS.");
}

if (config.VOICE_READY_TIMEOUT_MS >= config.VOICE_LEASE_MS) {
  throw new Error("VOICE_READY_TIMEOUT_MS must be smaller than VOICE_LEASE_MS.");
}
