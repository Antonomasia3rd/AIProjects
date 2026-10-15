import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";

export type BuildFingerprintInfo = {
  full: string;
  short: string;
  files: number;
};

function walkFiles(root: string, relativeDir: string): string[] {
  const absoluteDir = path.join(root, relativeDir);
  if (!fs.existsSync(absoluteDir)) return [];

  const out: string[] = [];
  const stack = [relativeDir];

  while (stack.length) {
    const current = stack.pop()!;
    const absolute = path.join(root, current);
    const entries = fs
      .readdirSync(absolute, { withFileTypes: true })
      .sort((a, b) => a.name.localeCompare(b.name));

    for (const entry of entries) {
      const rel = path.join(current, entry.name);
      if (entry.isDirectory()) {
        stack.push(rel);
      } else if (entry.isFile()) {
        out.push(rel);
      }
    }
  }

  return out.sort((a, b) => a.localeCompare(b));
}

export function computeBuildFingerprint(
  root = process.cwd()
): BuildFingerprintInfo {
  const candidates = [
    "package.json",
    "package-lock.json",
    ...walkFiles(root, "dist"),
    ...walkFiles(root, "sql")
  ];

  const files = candidates.filter((relative) =>
    fs.existsSync(path.join(root, relative))
  );
  const hash = crypto.createHash("sha256");

  for (const relative of files) {
    const normalized = relative.replaceAll("\\", "/");
    const bytes = fs.readFileSync(path.join(root, relative));
    hash.update(Buffer.from(normalized, "utf8"));
    hash.update(Buffer.from([0]));
    hash.update(bytes);
    hash.update(Buffer.from([0]));
  }

  const full = hash.digest("hex");
  return {
    full,
    short: full.slice(0, 12),
    files: files.length
  };
}

export const BUILD_FINGERPRINT = computeBuildFingerprint();
