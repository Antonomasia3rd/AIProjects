import fs from "node:fs";
import path from "node:path";

export function getAppVersion(root = process.cwd()): string {
  try {
    const raw = fs.readFileSync(path.join(root, "package.json"), "utf8");
    const parsed = JSON.parse(raw) as { version?: unknown };
    return typeof parsed.version === "string" && parsed.version.trim()
      ? parsed.version.trim()
      : "unknown";
  } catch {
    return "unknown";
  }
}

export const APP_VERSION = getAppVersion();
