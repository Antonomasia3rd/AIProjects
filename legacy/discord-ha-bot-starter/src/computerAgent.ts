import os from "node:os";
import { spawn } from "node:child_process";
import { db } from "./db.js";
import { config } from "./config.js";
import { log } from "./logger.js";
import { APP_VERSION } from "./version.js";
import { BUILD_FINGERPRINT } from "./buildFingerprint.js";
import { prepareSelfUpdate } from "./selfUpdate.js";

const wait = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

export type ComputerCommandRow = {
  id: string;
  guild_id: string;
  requested_by: string;
  target_node_id: string;
  action: string;
  payload: Record<string, unknown>;
  reason: string;
};

type ActionResult = {
  summary: string;
  details?: Record<string, unknown>;
  deferredAction?: () => void;
};

function windowsBuildNumber(): number | null {
  if (process.platform !== "win32") return null;
  const parts = os.release().split(".");
  const build = Number(parts[2]);
  return Number.isFinite(build) ? build : null;
}

function runProcess(
  file: string,
  args: string[],
  timeoutMs = 20_000
): Promise<{ stdout: string; stderr: string; code: number }> {
  return new Promise((resolve, reject) => {
    const child = spawn(file, args, {
      windowsHide: true,
      stdio: ["ignore", "pipe", "pipe"]
    });

    let stdout = "";
    let stderr = "";
    let settled = false;

    const timer = setTimeout(() => {
      if (settled) return;
      child.kill();
      reject(new Error(`${file} timed out after ${timeoutMs} ms`));
    }, timeoutMs);

    child.stdout.on("data", (chunk) => {
      stdout += String(chunk);
    });
    child.stderr.on("data", (chunk) => {
      stderr += String(chunk);
    });

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
      resolve({ stdout: stdout.trim(), stderr: stderr.trim(), code: code ?? -1 });
    });
  });
}

async function powershell(script: string): Promise<string> {
  if (process.platform !== "win32") {
    throw new Error("This computer action is currently implemented for Windows only.");
  }

  const result = await runProcess(
    "powershell.exe",
    [
      "-NoLogo",
      "-NoProfile",
      "-NonInteractive",
      "-ExecutionPolicy",
      "Bypass",
      "-Command",
      script
    ]
  );

  if (result.code !== 0) {
    throw new Error(result.stderr || result.stdout || `PowerShell exited with ${result.code}`);
  }

  return result.stdout;
}

function scheduleWindowsSleep(): void {
  setTimeout(() => {
    const script = String.raw`
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NifibotPower {
    [DllImport("powrprof.dll", SetLastError=true)]
    public static extern bool SetSuspendState(bool hibernate, bool forceCritical, bool disableWakeEvent);
}
'@
if (-not [NifibotPower]::SetSuspendState($false, $false, $false)) {
    throw "SetSuspendState returned false. Win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}
`;

    void powershell(script).catch((error) => {
      log.error({ err: error, node: config.NODE_ID }, "Deferred sleep action failed");
    });
  }, 4_000);
}

async function lockWindows(): Promise<string> {
  if (process.platform !== "win32") {
    throw new Error("Lock is currently implemented for Windows only.");
  }

  const result = await runProcess("rundll32.exe", ["user32.dll,LockWorkStation"], 10_000);
  if (result.code !== 0) {
    throw new Error(result.stderr || `LockWorkStation exited with ${result.code}`);
  }
  return "Windows workstation locked.";
}

const POWER_MODE_GUIDS: Record<string, { guid: string; label: string }> = {
  better_battery: {
    guid: "961cc777-2547-4f9d-8174-7d86181b8a7a",
    label: "Best power efficiency / Better battery"
  },
  balanced: {
    guid: "00000000-0000-0000-0000-000000000000",
    label: "Balanced"
  },
  best_performance: {
    guid: "ded574b5-45a0-4f42-8737-46345c09c238",
    label: "Best performance"
  }
};

async function setWindowsPowerMode(payload: Record<string, unknown>): Promise<ActionResult> {
  if (process.platform !== "win32") {
    throw new Error("Power-mode control is currently implemented for Windows only.");
  }

  const build = windowsBuildNumber();
  if (build !== null && build < 22000) {
    throw new Error(
      `Persistent AC/DC power-mode selection currently uses the Windows 11 power-mode API. This host is build ${build}. Sleep/lock/status still work; Win10 overlay compatibility can be added separately.`
    );
  }

  const key = String(payload.key ?? "");
  const value = String(payload.value ?? "");
  const appliesTo = String(payload.appliesTo ?? "").toUpperCase();

  if (key !== "power_mode") {
    throw new Error(`Unsupported settings key: ${key || "(missing)"}`);
  }

  const mode = POWER_MODE_GUIDS[value];
  if (!mode) throw new Error(`Unsupported power mode value: ${value || "(missing)"}`);
  if (!["AC", "DC", "BOTH"].includes(appliesTo)) {
    throw new Error(`applies_to must be AC, DC, or BOTH (received ${appliesTo || "missing"}).`);
  }

  const ac = appliesTo === "AC" || appliesTo === "BOTH";
  const dc = appliesTo === "DC" || appliesTo === "BOTH";

  const script = String.raw`
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NifibotPowerMode {
    [DllImport("powrprof.dll", EntryPoint="PowerSetUserConfiguredACPowerMode")]
    public static extern uint SetAC(ref Guid mode);
    [DllImport("powrprof.dll", EntryPoint="PowerSetUserConfiguredDCPowerMode")]
    public static extern uint SetDC(ref Guid mode);
}
'@
$mode = [Guid]'${mode.guid}'
$results = New-Object System.Collections.Generic.List[string]
${ac ? "$r=[NifibotPowerMode]::SetAC([ref]$mode); if($r -ne 0){throw \"PowerSetUserConfiguredACPowerMode failed: $r\"}; $results.Add('AC=OK')" : ""}
${dc ? "$r=[NifibotPowerMode]::SetDC([ref]$mode); if($r -ne 0){throw \"PowerSetUserConfiguredDCPowerMode failed: $r\"}; $results.Add('DC=OK')" : ""}
$results -join '; '
`;

  const output = await powershell(script);

  return {
    summary: `Power mode set to ${mode.label} for ${appliesTo}.`,
    details: {
      key,
      value,
      label: mode.label,
      appliesTo,
      nativeResult: output
    }
  };
}

async function executeAction(command: ComputerCommandRow): Promise<ActionResult> {
  if (command.action === "status") {
    return {
      summary: `Host ${os.hostname()} is online.`,
      details: {
        nodeId: config.NODE_ID,
        hostname: os.hostname(),
        platform: process.platform,
        release: os.release(),
        arch: os.arch(),
        node: process.version,
        pid: process.pid,
        osUptimeSeconds: Math.floor(os.uptime()),
        processUptimeSeconds: Math.floor(process.uptime()),
        totalMemoryMiB: Math.round(os.totalmem() / 1024 / 1024),
        freeMemoryMiB: Math.round(os.freemem() / 1024 / 1024)
      }
    };
  }

  if (command.action === "lock") {
    return { summary: await lockWindows() };
  }

  if (command.action === "sleep") {
    if (process.platform !== "win32") {
      throw new Error("Sleep is currently implemented for Windows only.");
    }
    return {
      summary: "Sleep accepted. Windows will enter sleep in about 4 seconds.",
      details: { delaySeconds: 4 },
      deferredAction: scheduleWindowsSleep
    };
  }

  if (command.action === "settings") {
    return setWindowsPowerMode(command.payload);
  }

  throw new Error(`Unsupported computer action: ${command.action}`);
}

export class ComputerAgent {
  private stopping = false;
  private loopPromise: Promise<void> | null = null;

  constructor(
    private readonly requestGracefulRestart: (reason: string) => void = () => {
      process.exit(0);
    }
  ) {}

  async start(): Promise<void> {
    if (!config.COMPUTER_CONTROL_ENABLED || this.loopPromise) return;

    this.stopping = false;
    await this.heartbeat();
    this.loopPromise = this.runLoop();

    log.info(
      { node: config.NODE_ID, hostname: os.hostname() },
      "Computer control agent started"
    );
  }

  async stop(): Promise<void> {
    this.stopping = true;
    if (this.loopPromise) {
      await Promise.race([this.loopPromise, wait(config.COMPUTER_AGENT_POLL_MS + 500)]).catch(
        () => undefined
      );
      this.loopPromise = null;
    }
  }

  private async heartbeat(): Promise<void> {
    await db.query(
      `
      INSERT INTO computer_nodes (
        node_id, hostname, platform, os_release, arch, process_id,
        started_at, last_seen, current_version, metadata
      )
      VALUES ($1, $2, $3, $4, $5, $6, clock_timestamp(), clock_timestamp(), $7, $8::jsonb)
      ON CONFLICT (node_id) DO UPDATE
      SET hostname = EXCLUDED.hostname,
          platform = EXCLUDED.platform,
          os_release = EXCLUDED.os_release,
          arch = EXCLUDED.arch,
          process_id = EXCLUDED.process_id,
          last_seen = clock_timestamp(),
          current_version = EXCLUDED.current_version,
          metadata = EXCLUDED.metadata
      `,
      [
        config.NODE_ID,
        os.hostname(),
        process.platform,
        os.release(),
        os.arch(),
        process.pid,
        APP_VERSION,
        JSON.stringify({
          nodeVersion: process.version,
          processUptimeSeconds: Math.floor(process.uptime()),
          osUptimeSeconds: Math.floor(os.uptime()),
          totalMemoryMiB: Math.round(os.totalmem() / 1024 / 1024),
          freeMemoryMiB: Math.round(os.freemem() / 1024 / 1024),
          windowsBuild: windowsBuildNumber(),
          buildFingerprint: BUILD_FINGERPRINT.full,
          buildFingerprintShort: BUILD_FINGERPRINT.short,
          buildFingerprintFiles: BUILD_FINGERPRINT.files
        })
      ]
    );
  }

  private async claimNext(): Promise<ComputerCommandRow | null> {
    const result = await db.query<ComputerCommandRow>(
      `
      WITH candidate AS (
        SELECT id
        FROM computer_commands
        WHERE target_node_id = $1
          AND status = 'pending'
          AND expires_at > clock_timestamp()
        ORDER BY requested_at, id
        FOR UPDATE SKIP LOCKED
        LIMIT 1
      )
      UPDATE computer_commands AS command
      SET status = 'claimed',
          claimed_by = $1,
          claimed_at = clock_timestamp()
      FROM candidate
      WHERE command.id = candidate.id
      RETURNING command.id, command.guild_id, command.requested_by,
                command.target_node_id, command.action, command.payload,
                command.reason
      `,
      [config.NODE_ID]
    );

    return result.rows[0] ?? null;
  }

  private async complete(
    command: ComputerCommandRow,
    status: "completed" | "failed",
    result: Record<string, unknown>
  ): Promise<void> {
    await db.query(
      `
      UPDATE computer_commands
      SET status = $2,
          completed_at = clock_timestamp(),
          result = $3::jsonb
      WHERE id = $1
        AND claimed_by = $4
      `,
      [command.id, status, JSON.stringify(result), config.NODE_ID]
    );
  }

  private async prepareUpdateAction(command: ComputerCommandRow): Promise<ActionResult> {
    const packageId = String(command.payload.packageId ?? "");
    if (!packageId) throw new Error("self_update payload is missing packageId.");

    const applyNotBeforeRaw = command.payload.applyNotBeforeMs;
    const applyNotBeforeMs =
      typeof applyNotBeforeRaw === "number" && Number.isFinite(applyNotBeforeRaw)
        ? applyNotBeforeRaw
        : null;

    const prepared = await prepareSelfUpdate(
      packageId,
      command.id,
      applyNotBeforeMs,
      (reason) => this.requestGracefulRestart(reason)
    );

    return {
      summary: `Update ${prepared.filename} staged and validated as v${prepared.version}. This node will restart automatically to apply it.`,
      details: {
        packageId: prepared.packageId,
        filename: prepared.filename,
        version: prepared.version,
        sha256: prepared.sha256,
        applyNotBeforeMs
      },
      deferredAction: prepared.apply
    };
  }

  private async execute(command: ComputerCommandRow): Promise<void> {
    try {
      const result =
        command.action === "self_update"
          ? await this.prepareUpdateAction(command)
          : await executeAction(command);
      await this.complete(command, "completed", {
        ok: true,
        summary: result.summary,
        details: result.details ?? null,
        executedBy: config.NODE_ID,
        hostname: os.hostname()
      });

      result.deferredAction?.();
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      await this.complete(command, "failed", {
        ok: false,
        error: message,
        executedBy: config.NODE_ID,
        hostname: os.hostname()
      });
      log.warn({ err: error, commandId: command.id }, "Computer command failed");
    }
  }

  private async runLoop(): Promise<void> {
    let lastHeartbeat = 0;

    while (!this.stopping) {
      try {
        if (Date.now() - lastHeartbeat >= config.COMPUTER_AGENT_HEARTBEAT_MS) {
          await this.heartbeat();
          lastHeartbeat = Date.now();
        }

        await db.query(
          `
          UPDATE computer_commands
          SET status = 'expired', completed_at = clock_timestamp()
          WHERE status = 'pending'
            AND expires_at <= clock_timestamp()
          `
        );

        const command = await this.claimNext();
        if (command) {
          await this.execute(command);
          continue;
        }
      } catch (error) {
        log.warn({ err: error, node: config.NODE_ID }, "Computer agent poll failed");
      }

      await wait(config.COMPUTER_AGENT_POLL_MS);
    }
  }
}
