import crypto from "node:crypto";
import fs from "node:fs";
import fsp from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { spawn } from "node:child_process";
import { db } from "./db.js";
import { log } from "./logger.js";
import { getAppVersion } from "./version.js";
import { resolveNpmInvocation } from "./runtime/npmRuntime.js";

const MAX_PROCESS_OUTPUT = 2 * 1024 * 1024;

type UpdatePackageRow = {
  id: string;
  filename: string;
  sha256: string;
  size_bytes: string;
  archive: Buffer;
};

type PreparedUpdate = {
  version: string;
  filename: string;
  sha256: string;
  packageId: string;
  apply: () => void;
};

function hashFile(file: string): Promise<string> {
  return new Promise((resolve, reject) => {
    const hash = crypto.createHash("sha256");
    const stream = fs.createReadStream(file);
    stream.on("error", reject);
    stream.on("data", (chunk) => hash.update(chunk));
    stream.on("end", () => resolve(hash.digest("hex")));
  });
}

function runProcess(
  file: string,
  args: string[],
  cwd: string,
  timeoutMs: number
): Promise<{ stdout: string; stderr: string; code: number }> {
  return new Promise((resolve, reject) => {
    const child = spawn(file, args, {
      cwd,
      windowsHide: true,
      env: process.env,
      stdio: ["ignore", "pipe", "pipe"]
    });

    let stdout = "";
    let stderr = "";
    let settled = false;

    const append = (current: string, chunk: unknown) => {
      const next = current + String(chunk);
      return next.length > MAX_PROCESS_OUTPUT
        ? next.slice(next.length - MAX_PROCESS_OUTPUT)
        : next;
    };

    child.stdout.on("data", (chunk) => {
      stdout = append(stdout, chunk);
    });
    child.stderr.on("data", (chunk) => {
      stderr = append(stderr, chunk);
    });

    const timer = setTimeout(() => {
      if (settled) return;
      settled = true;
      child.kill();
      reject(new Error(`${file} ${args.join(" ")} timed out after ${timeoutMs} ms`));
    }, timeoutMs);

    child.on("error", (error) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      reject(error);
    });

    child.on("close", (code) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      resolve({
        stdout: stdout.trim(),
        stderr: stderr.trim(),
        code: code ?? -1
      });
    });
  });
}

async function requireSuccess(
  file: string,
  args: string[],
  cwd: string,
  timeoutMs: number
): Promise<void> {
  const result = await runProcess(file, args, cwd, timeoutMs);
  if (result.code !== 0) {
    throw new Error(
      [
        `${file} ${args.join(" ")} exited with ${result.code}.`,
        result.stderr,
        result.stdout
      ]
        .filter(Boolean)
        .join("\n")
        .slice(-6000)
    );
  }
}

function archiveTool(): string {
  return process.platform === "win32" ? "tar.exe" : "tar";
}

function npmInvocation(args: string[]): { file: string; args: string[] } {
  // Direct/Startup/watchdog launches do not inherit npm_execpath. Resolve the
  // installed CLI and invoke it through the current Node, never a .cmd shell.
  return resolveNpmInvocation(process.platform, process.execPath, process.env,
    candidate => { try { return fs.statSync(candidate).isFile(); } catch { return false; } }, args);
}

async function requireNpmSuccess(
  args: string[],
  cwd: string,
  timeoutMs: number
): Promise<void> {
  const invocation = npmInvocation(args);
  await requireSuccess(invocation.file, invocation.args, cwd, timeoutMs);
}

function validateArchiveEntry(entry: string): void {
  const clean = entry.replace(/\\/g, "/").replace(/^\.\//, "");
  if (!clean) return;
  if (clean.startsWith("/") || /^[A-Za-z]:\//.test(clean)) {
    throw new Error(`Update archive contains an absolute path: ${entry}`);
  }
  const parts = clean.split("/").filter(Boolean);
  if (parts.includes("..")) {
    throw new Error(`Update archive contains path traversal: ${entry}`);
  }
  if (parts.some((part) => part.toLowerCase() === ".env")) {
    throw new Error("Update archive must never contain .env; node-local secrets are preserved.");
  }
}

async function copyCurrentProject(root: string, stage: string): Promise<void> {
  const excluded = new Set([
    "node_modules",
    "dist",
    ".env",
    "history-staging",
    "archives",
    "logs",
    ".nifibot-updates"
  ]);

  await fsp.cp(root, stage, {
    recursive: true,
    force: true,
    filter(source) {
      const rel = path.relative(root, source);
      if (!rel) return true;
      const top = rel.split(path.sep)[0];
      return !excluded.has(top);
    }
  });
}

async function resolveOverlayRoot(overlay: string): Promise<string> {
  const rootEntries = await fsp.readdir(overlay, { withFileTypes: true });
  const hasProjectShape = rootEntries.some((entry) =>
    ["src", "sql", "package.json", "package-lock.json"].includes(entry.name)
  );
  if (hasProjectShape) return overlay;

  const directories = rootEntries.filter((entry) => entry.isDirectory());
  if (directories.length === 1) {
    const candidate = path.join(overlay, directories[0].name);
    const entries = await fsp.readdir(candidate).catch(() => [] as string[]);
    if (entries.some((name) => ["src", "sql", "package.json"].includes(name))) {
      return candidate;
    }
  }

  return overlay;
}

async function writeApplyScript(
  baseDir: string,
  root: string,
  stage: string,
  replaceNodeModules: boolean,
  commandId: string
): Promise<{ scriptPath: string; readyPath: string; applyLogPath: string }> {
  const updateRoot = path.join(root, ".nifibot-updates");
  await fsp.mkdir(updateRoot, { recursive: true });

  // Use a detached Node helper instead of a temporary PowerShell script.
  // On some Windows systems PowerShell can exit before entering the script
  // (policy/security/argument-host quirks), producing no READY marker or log.
  // Every Nifibot node necessarily already has a working node.exe, so using
  // the same runtime removes that dependency and the quoting/policy surface.
  const scriptPath = path.join(baseDir, "apply-update.cjs");
  const readyPath = path.join(baseDir, "apply-ready.flag");
  const applyLogPath = path.join(updateRoot, `apply-${commandId}.log`);

  const script = String.raw`const fs = require("node:fs");
const path = require("node:path");
const { spawn } = require("node:child_process");

const [
  root,
  stage,
  waitPidText,
  readyPath,
  logPath,
  nodeExe,
  replaceNodeModulesText
] = process.argv.slice(2);

const waitPid = Number(waitPidText);
const watchdogPid = Number(process.env.NIFIBOT_WATCHDOG_PID || "0");
const replaceNodeModules = replaceNodeModulesText === "1";

function writeLog(message) {
  const stamp = new Date().toISOString();
  fs.mkdirSync(path.dirname(logPath), { recursive: true });
  fs.appendFileSync(logPath, "[" + stamp + "] " + message + "\r\n", "utf8");
}

function exists(target) {
  try {
    return fs.existsSync(target);
  } catch {
    return false;
  }
}

function remove(target) {
  if (!exists(target)) return;
  fs.rmSync(target, { recursive: true, force: true, maxRetries: 5, retryDelay: 250 });
}

function copy(source, destination) {
  fs.cpSync(source, destination, {
    recursive: true,
    force: true,
    dereference: false
  });
}

function processExists(pid) {
  if (!Number.isFinite(pid) || pid <= 0) return false;
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    return error && error.code === "EPERM";
  }
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function main() {
  writeLog("Node updater helper started. Waiting for Nifibot PID " + waitPid + " to exit.");
  fs.writeFileSync(readyPath, "ready", "ascii");

  while (processExists(waitPid)) {
    await sleep(250);
  }

  if (Number.isFinite(watchdogPid) && watchdogPid > 0) {
    writeLog("Nifibot child exited. Waiting for watchdog PID " + watchdogPid + " to exit for self-update.");
    const watchdogDeadline = Date.now() + 10000;
    while (processExists(watchdogPid) && Date.now() < watchdogDeadline) {
      await sleep(100);
    }
    if (processExists(watchdogPid)) {
      writeLog("WARNING: old watchdog is still alive after 10 seconds; continuing update cautiously.");
    } else {
      writeLog("Old watchdog exited for self-update.");
    }
  }

  await sleep(500);
  writeLog("Original Nifibot process exited. Beginning live-file replacement.");

  const updateRoot = path.join(root, ".nifibot-updates");
  const backupRoot = path.join(updateRoot, "backup-current");
  remove(backupRoot);
  fs.mkdirSync(backupRoot, { recursive: true });

  for (const name of ["src", "sql", "dist"]) {
    const source = path.join(root, name);
    if (exists(source)) {
      copy(source, path.join(backupRoot, name));
    }
  }

  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    if (entry.isFile() && entry.name !== ".env") {
      copy(path.join(root, entry.name), path.join(backupRoot, entry.name));
    }
  }

  const oldNodeModules = path.join(updateRoot, "node_modules-backup");
  remove(oldNodeModules);

  try {
    for (const name of ["src", "sql", "dist"]) {
      remove(path.join(root, name));
    }

    if (replaceNodeModules) {
      const nodeModules = path.join(root, "node_modules");
      if (exists(nodeModules)) {
        fs.renameSync(nodeModules, oldNodeModules);
      }
    }

    const preserve = new Set([
      ".env",
      ".nifibot-updates",
      "history-staging",
      "archives",
      "logs",
      "node_modules"
    ]);

    for (const entry of fs.readdirSync(stage, { withFileTypes: true })) {
      if (preserve.has(entry.name)) continue;
      copy(path.join(stage, entry.name), path.join(root, entry.name));
    }

    if (replaceNodeModules) {
      copy(path.join(stage, "node_modules"), path.join(root, "node_modules"));
    }
  } catch (error) {
    writeLog(
      "Live-file replacement failed; rolling back: " +
        (error && error.stack ? error.stack : String(error))
    );

    for (const name of ["src", "sql", "dist"]) {
      remove(path.join(root, name));
      const backup = path.join(backupRoot, name);
      if (exists(backup)) {
        copy(backup, path.join(root, name));
      }
    }

    for (const entry of fs.readdirSync(backupRoot, { withFileTypes: true })) {
      if (entry.isFile()) {
        copy(path.join(backupRoot, entry.name), path.join(root, entry.name));
      }
    }

    if (replaceNodeModules && exists(oldNodeModules)) {
      remove(path.join(root, "node_modules"));
      fs.renameSync(oldNodeModules, path.join(root, "node_modules"));
    }

    throw error;
  }

  if (replaceNodeModules) {
    remove(oldNodeModules);
  }

  writeLog("Live files replaced successfully.");

  // Keep a small manual fallback launcher for humans, but do NOT use cmd.exe
  // for the automatic restart. We observed cmd.exe being spawned successfully
  // while Nifibot itself never appeared. Launch node.exe directly instead.
  const watchdogEntry = path.join(root, "dist", "watchdog.js");
  const directEntry = path.join(root, "dist", "index.js");
  const restartEntry = exists(watchdogEntry) ? watchdogEntry : directEntry;

  const restartCmd = path.join(updateRoot, "restart-nifibot.cmd");
  const restartLines = [
    "@echo off",
    'cd /d "' + root + '"',
    '"' + nodeExe + '" "' + restartEntry + '"'
  ];
  fs.writeFileSync(restartCmd, restartLines.join("\r\n") + "\r\n", "ascii");

  const applyLogName = path.basename(logPath);
  const applyLogMatch = /^apply-(.+)\.log$/i.exec(applyLogName);
  const restartLog = path.join(
    path.dirname(logPath),
    applyLogMatch ? "restart-" + applyLogMatch[1] + ".log" : "restart-nifibot.log"
  );
  const restartFd = fs.openSync(restartLog, "a");

  writeLog("Launching restarted Nifibot through " + (restartEntry === watchdogEntry ? "watchdog" : "direct node") + ".");
  const restarted = spawn(
    nodeExe,
    [restartEntry],
    {
      cwd: root,
      detached: true,
      windowsHide: false,
      env: process.env,
      stdio: ["ignore", restartFd, restartFd]
    }
  );

  fs.closeSync(restartFd);
  restarted.unref();
  writeLog(
    "Restart process spawned with PID " +
      restarted.pid +
      ". Output: " +
      restartLog
  );

  // Catch the common case where the replacement process dies immediately.
  await sleep(3000);
  if (processExists(restarted.pid)) {
    writeLog("Restart supervisor process is still alive after 3 seconds.");
  } else {
    writeLog(
      "WARNING: Restart supervisor exited within 3 seconds. Inspect " +
        restartLog
    );
  }
}

main().catch((error) => {
  try {
    writeLog(
      "FATAL updater error: " +
        (error && error.stack ? error.stack : String(error))
    );
  } catch {}
  process.exitCode = 1;
});`;

  await fsp.writeFile(scriptPath, script, "utf8");
  return { scriptPath, readyPath, applyLogPath };
}

export async function prepareSelfUpdate(
  packageId: string,
  commandId: string,
  applyNotBeforeMs: number | null,
  requestGracefulRestart: (reason: string) => void
): Promise<PreparedUpdate> {
  if (process.platform !== "win32") {
    throw new Error("Cluster self-update is currently implemented for Windows nodes only.");
  }

  const packageResult = await db.query<UpdatePackageRow>(
    `
    SELECT id, filename, sha256, size_bytes, archive
    FROM cluster_update_packages
    WHERE id = $1
    `,
    [packageId]
  );
  const updatePackage = packageResult.rows[0];
  if (!updatePackage) throw new Error(`Update package ${packageId} no longer exists.`);

  const root = process.cwd();
  const baseDir = await fsp.mkdtemp(path.join(os.tmpdir(), `nifibot-update-${commandId}-`));
  const archivePath = path.join(baseDir, "release.zip");
  const overlayDir = path.join(baseDir, "overlay");
  const stageDir = path.join(baseDir, "stage");

  await fsp.writeFile(archivePath, updatePackage.archive);
  const actualSha = await hashFile(archivePath);
  if (actualSha.toLowerCase() !== updatePackage.sha256.toLowerCase()) {
    throw new Error(
      `Update SHA-256 mismatch. Expected ${updatePackage.sha256}, got ${actualSha}.`
    );
  }

  const listResult = await runProcess(archiveTool(), ["-tf", archivePath], root, 30_000);
  if (listResult.code !== 0) {
    throw new Error(`Unable to inspect update ZIP: ${listResult.stderr || listResult.stdout}`);
  }
  for (const entry of listResult.stdout.split(/\r?\n/)) {
    validateArchiveEntry(entry.trim());
  }

  await fsp.mkdir(overlayDir, { recursive: true });
  await fsp.mkdir(stageDir, { recursive: true });
  await requireSuccess(archiveTool(), ["-xf", archivePath, "-C", overlayDir], root, 60_000);

  await copyCurrentProject(root, stageDir);
  const overlayRoot = await resolveOverlayRoot(overlayDir);
  await fsp.cp(overlayRoot, stageDir, { recursive: true, force: true });

  for (const required of ["package.json", "src", "sql", "tsconfig.json"]) {
    if (!fs.existsSync(path.join(stageDir, required))) {
      throw new Error(`Staged release is missing required project item: ${required}`);
    }
  }

  const currentLock = path.join(root, "package-lock.json");
  const stagedLock = path.join(stageDir, "package-lock.json");
  const rootNodeModules = path.join(root, "node_modules");
  let replaceNodeModules = true;

  if (
    fs.existsSync(currentLock) &&
    fs.existsSync(stagedLock) &&
    fs.existsSync(rootNodeModules) &&
    (await hashFile(currentLock)) === (await hashFile(stagedLock))
  ) {
    await fsp.symlink(rootNodeModules, path.join(stageDir, "node_modules"), "junction");
    replaceNodeModules = false;
  } else {
    await requireNpmSuccess(["ci"], stageDir, 10 * 60_000);
  }

  await requireNpmSuccess(["run", "build"], stageDir, 5 * 60_000);
  // Migrations are expected to remain backwards-compatible/additive so an old
  // leader can keep serving during a rolling update.
  await requireNpmSuccess(["run", "db:migrate"], stageDir, 3 * 60_000);
  await requireNpmSuccess(["run", "commands:deploy"], stageDir, 3 * 60_000);

  const version = getAppVersion(stageDir);
  const { scriptPath, readyPath, applyLogPath } = await writeApplyScript(
    baseDir,
    root,
    stageDir,
    replaceNodeModules,
    commandId
  );

  const apply = () => {
    const delay = Math.max(0, (applyNotBeforeMs ?? Date.now()) - Date.now());
    setTimeout(() => {
      const applyArgs = [
        scriptPath,
        root,
        stageDir,
        String(process.pid),
        readyPath,
        applyLogPath,
        process.execPath,
        replaceNodeModules ? "1" : "0"
      ];

      const child = spawn(process.execPath, applyArgs, {
        detached: true,
        windowsHide: false,
        stdio: "ignore"
      });
      child.unref();

      child.once("error", (error) => {
        log.error(
          { err: error, packageId, version, applyLogPath },
          "Failed to launch detached Node self-update helper; keeping Nifibot online"
        );
      });

      child.once("exit", (code, signal) => {
        if (!fs.existsSync(readyPath)) {
          log.error(
            { packageId, version, applyLogPath, helperExitCode: code, helperSignal: signal },
            "Node self-update helper exited before acknowledging startup"
          );
        }
      });

      const waitForHelperReady = async () => {
        const deadline = Date.now() + 5_000;
        while (Date.now() < deadline) {
          if (fs.existsSync(readyPath)) {
            log.warn(
              { packageId, version, delayMs: delay, applyLogPath },
              "Self-update staged; updater helper is ready; restarting Nifibot to apply release"
            );
            setTimeout(() => requestGracefulRestart(`self-update ${version}`), 500);
            return;
          }
          await new Promise((resolve) => setTimeout(resolve, 100));
        }

        log.error(
          { packageId, version, applyLogPath },
          "Self-update helper did not acknowledge startup; update aborted and Nifibot will stay online"
        );
      };

      void waitForHelperReady();
    }, delay).unref();
  };

  return {
    version,
    filename: updatePackage.filename,
    sha256: updatePackage.sha256,
    packageId: updatePackage.id,
    apply
  };
}
