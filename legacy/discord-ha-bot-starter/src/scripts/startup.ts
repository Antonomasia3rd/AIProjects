import fs from "node:fs";
import fsp from "node:fs/promises";
import path from "node:path";
import crypto from "node:crypto";
import { config } from "../config.js";
import { createStartupPlan, installStartupPlan, removeStartupPlan, ownsStartupContents, type StartupStore } from "../runtime/startupPlan.js";

const action = (process.argv[2] ?? "status").toLowerCase();
const root = process.cwd();
function startupDirectory(): string {
  if (process.platform !== "win32") throw new Error("Startup-folder management is currently implemented for Windows only.");
  const appData = process.env.APPDATA;
  if (!appData) throw new Error("APPDATA is unavailable; cannot locate the current user's Startup folder.");
  return path.join(appData, "Microsoft", "Windows", "Start Menu", "Programs", "Startup");
}
const plan = createStartupPlan(config.NODE_ID, root, process.execPath);
const directory = startupDirectory();
const store: StartupStore = {
  async read(name) {
    try { return await fsp.readFile(path.join(directory, name), "utf8"); }
    catch (error) { if ((error as NodeJS.ErrnoException).code === "ENOENT") return null; throw error; }
  },
  async writeAtomic(name, contents) {
    await fsp.mkdir(directory, { recursive: true });
    const target = path.join(directory, name), temporary = target + ".tmp-" + crypto.randomUUID();
    try { await fsp.writeFile(temporary, contents, { encoding: "utf8", flag: "wx" }); await fsp.rename(temporary, target); }
    finally { await fsp.rm(temporary, { force: true }).catch(() => undefined); }
  },
  async remove(name) { await fsp.rm(path.join(directory, name), { force: true }); }
};
if (action === "install") {
  if (!fs.existsSync(path.join(root, "dist", "watchdog.js"))) throw new Error("dist\\watchdog.js is missing. Run npm run build first.");
  await installStartupPlan(plan, store);
  console.log("Current-user Startup-folder launcher installed. This instance's exact older launcher, if present, was migrated.");
} else if (action === "remove" || action === "uninstall") {
  console.log(await removeStartupPlan(plan, store) ? "This instance's Startup launcher removed." : "No owned Startup launcher found; other entries were left unchanged.");
} else if (action === "status") {
  const current = await store.read(plan.targetName), legacy = await store.read(plan.legacyName);
  const owned = current !== null && ownsStartupContents(current, plan);
  console.log(`Installed: ${owned || legacy === plan.legacyContents ? "yes" : "no"}`);
  console.log(`Current configuration: ${owned && current === plan.contents ? "yes" : "no"}`);
  if (current !== null && !owned) console.log("The destination contains an unrelated entry; it will not be overwritten or removed.");
  if (legacy !== null && legacy !== plan.legacyContents) console.log("A colliding older launcher was left unchanged; review its owner before removal.");
} else throw new Error(`Unknown startup action: ${action}. Use install, status, or remove.`);
console.log(`NODE_ID: ${config.NODE_ID}`);
console.log(`Launcher: ${path.join(directory, plan.targetName)}`);
console.log(`Project: ${root}`);
console.log(`Node: ${process.execPath}`);
