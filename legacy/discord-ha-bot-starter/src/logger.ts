import pino from "pino";
import { config } from "./config.js";

export const log = pino({
  level: config.LOG_LEVEL,
  redact: {
    paths: [
      "DISCORD_TOKEN",
      "DATABASE_URL",
      "password",
      "*.password",
      "token",
      "*.token"
    ],
    censor: "[REDACTED]"
  }
});
