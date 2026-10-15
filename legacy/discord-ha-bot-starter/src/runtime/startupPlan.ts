import crypto from "node:crypto";
import path from "node:path";
export interface StartupPlan { targetName: string; legacyName: string; owner: string; contents: string; legacyContents: string; }
function quotedBatchPath(value: string): string {
  // These characters cannot be part of a Windows file name. Benign %, !, &,
  // parentheses, carets and Unicode remain supported, not rejected as policy.
  if (/[\0\r\n"]/.test(value)) throw new Error("A launcher path contains an invalid Windows filename character.");
  return '"' + value.replace(/%/g, "%%") + '"';
}
export function createStartupPlan(nodeId: string, root: string, nodeExe: string): StartupPlan {
  const p = path.win32, canonicalRoot = p.resolve(root).replace(/\\+$/, "").toLowerCase();
  const owner = crypto.createHash("sha256").update(JSON.stringify([canonicalRoot, nodeId])).digest("hex");
  const readable = nodeId.replace(/[^A-Za-z0-9._-]+/g, "_").slice(0, 32) || "node";
  const legacy = nodeId.replace(/[^A-Za-z0-9._-]+/g, "_").slice(0, 80) || "node";
  const logDir = p.join(root, "logs"), runtimeLog = p.join(logDir, "startup-runtime.log"), launcherLog = p.join(logDir, "startup-launcher.log");
  const entry = p.join(root, "dist", "watchdog.js");
  const contents = ["@echo off", `rem Nifibot-Startup-Owner: ${owner}`, "setlocal DisableDelayedExpansion", "chcp 65001 >nul",
    `cd /d ${quotedBatchPath(root)}`, `if not exist ${quotedBatchPath(logDir)} mkdir ${quotedBatchPath(logDir)}`,
    `echo Startup-folder launch>>${quotedBatchPath(launcherLog)}`,
    `${quotedBatchPath(nodeExe)} ${quotedBatchPath(entry)} >>${quotedBatchPath(runtimeLog)} 2>&1`,
    'set "NIFIBOT_EXIT_CODE=%ERRORLEVEL%"', `echo Nifibot exited with code %NIFIBOT_EXIT_CODE%.>>${quotedBatchPath(launcherLog)}`,
    "exit /b %NIFIBOT_EXIT_CODE%"].join("\r\n") + "\r\n";
  const legacyContents = ["@echo off", "setlocal", `cd /d "${root}"`, `if not exist "${logDir}" mkdir "${logDir}"`,
    `echo [%date% %time%] Startup-folder launch for ${nodeId}>>"${launcherLog}"`,
    `"${nodeExe}" "${entry}" >>"${runtimeLog}" 2>&1`,
    `echo [%date% %time%] Nifibot exited with code %errorlevel%.>>"${launcherLog}"`, "endlocal"].join("\r\n") + "\r\n";
  return { targetName: `Nifibot-${readable}-${owner.slice(0, 24)}.cmd`, legacyName: `Nifibot-${legacy}.cmd`, owner, contents, legacyContents };
}
export function ownsStartupContents(contents: string, plan: StartupPlan): boolean {
  return contents.split(/\r?\n/).some(line => line === `rem Nifibot-Startup-Owner: ${plan.owner}`);
}
export interface StartupStore {
  read(name: string): Promise<string | null>;
  writeAtomic(name: string, contents: string): Promise<void>;
  remove(name: string): Promise<void>;
}
export async function installStartupPlan(plan: StartupPlan, store: StartupStore): Promise<void> {
  const current = await store.read(plan.targetName);
  if (current !== null && !ownsStartupContents(current, plan)) throw new Error("The Startup destination belongs to another entry; it was left unchanged.");
  await store.writeAtomic(plan.targetName, plan.contents);
  // Install before removing the exact known legacy entry. Sanitized-name
  // collisions or launchers from another folder are never adopted/deleted.
  if (await store.read(plan.legacyName) === plan.legacyContents) await store.remove(plan.legacyName);
}
export async function removeStartupPlan(plan: StartupPlan, store: StartupStore): Promise<boolean> {
  let removed = false;
  const current = await store.read(plan.targetName);
  if (current !== null && ownsStartupContents(current, plan)) { await store.remove(plan.targetName); removed = true; }
  if (await store.read(plan.legacyName) === plan.legacyContents) { await store.remove(plan.legacyName); removed = true; }
  return removed;
}
