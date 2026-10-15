import { spawn } from "node:child_process";
import path from "node:path";
import { performance } from "node:perf_hooks";
import { Watchdog } from "./runtime/watchdogCore.js";

const root = process.cwd();
const watchdog = new Watchdog({
  now: () => performance.now(),
  schedule: (callback, milliseconds) => setTimeout(callback, milliseconds),
  cancel: clearTimeout,
  spawn: () => spawn(process.execPath, [path.join(root, "dist", "index.js")], {
    cwd: root, windowsHide: true, stdio: ["inherit", "inherit", "inherit", "ipc"],
    env: { ...process.env, NIFIBOT_WATCHDOG_PID: String(process.pid) }
  }),
  write: message => process.stdout.write(`[${new Date().toISOString()}] [watchdog] ${message}\n`),
  exit: code => process.exit(code)
});
process.on("SIGINT", () => watchdog.stop("SIGINT"));
process.on("SIGTERM", () => watchdog.stop("SIGTERM"));
process.on("uncaughtException", error => watchdog.stop(`uncaught exception: ${error.message}`, 1));
process.on("unhandledRejection", reason => watchdog.stop(`unhandled rejection: ${String(reason)}`, 1));
watchdog.start();
