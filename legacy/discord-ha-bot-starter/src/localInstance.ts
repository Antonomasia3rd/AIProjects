import crypto from "node:crypto";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import fsp from "node:fs/promises";
import { removeConfirmedStaleSocket } from "./runtime/staleSocket.js";

export class LocalInstanceAlreadyRunningError extends Error {
  constructor(
    public readonly nodeId: string,
    public readonly endpoint: string
  ) {
    super(`Another local Nifibot process is already using NODE_ID ${nodeId}.`);
    this.name = "LocalInstanceAlreadyRunningError";
  }
}

export interface LocalInstanceLock {
  endpoint: string;
  close(): Promise<void>;
}

function endpointForNode(nodeId: string): string {
  const digest = crypto
    .createHash("sha256")
    .update(`nifibot-local-instance:${nodeId}`, "utf8")
    .digest("hex")
    .slice(0, 24);

  if (process.platform === "win32") {
    return `\\\\.\\pipe\\nifibot-${digest}`;
  }

  return path.join(os.tmpdir(), `nifibot-${digest}.sock`);
}

function listen(server: net.Server, endpoint: string): Promise<void> {
  return new Promise((resolve, reject) => {
    const onError = (error: NodeJS.ErrnoException) => {
      cleanup();
      reject(error);
    };
    const onListening = () => {
      cleanup();
      resolve();
    };
    const cleanup = () => {
      server.off("error", onError);
      server.off("listening", onListening);
    };

    server.once("error", onError);
    server.once("listening", onListening);
    server.listen(endpoint);
  });
}
async function recoverUnixEndpoint(server: net.Server, endpoint: string): Promise<boolean> {
  // Serializes recovery with other Nifibot processes. Leave uncertain/active
  // endpoints untouched, including an abandoned recovery lock.
  const lockPath = endpoint + ".recovery";
  const lock = await fsp.open(lockPath, "wx").catch(() => null);
  if (!lock) return false;
  try {
    const removed = await removeConfirmedStaleSocket({
      async stat() {
        try { const value = await fsp.lstat(endpoint, { bigint: true }); return { socket: value.isSocket(), device: value.dev, inode: value.ino }; }
        catch (error) { if ((error as NodeJS.ErrnoException).code === "ENOENT") return null; throw error; }
      },
      probe: () => new Promise(resolve => {
        let done = false;
        const socket = net.createConnection(endpoint);
        const finish = (state: "active" | "refused" | "unknown") => {
          if (done) return; done = true; clearTimeout(timer); socket.destroy(); resolve(state);
        };
        const timer = setTimeout(() => finish("unknown"), 500);
        socket.once("connect", () => finish("active"));
        socket.once("error", (error: NodeJS.ErrnoException) => finish(error.code === "ECONNREFUSED" ? "refused" : "unknown"));
      }),
      unlink: () => fsp.unlink(endpoint)
    });
    if (!removed) return false;
    await listen(server, endpoint); return true;
  } finally { await lock.close(); await fsp.unlink(lockPath).catch(() => undefined); }
}

export async function acquireLocalInstanceLock(
  nodeId: string
): Promise<LocalInstanceLock> {
  const endpoint = endpointForNode(nodeId);
  const server = net.createServer((socket) => {
    socket.end(
      JSON.stringify({
        app: "Nifibot",
        nodeId,
        pid: process.pid,
        hostname: os.hostname()
      })
    );
  });

  try {
    await listen(server, endpoint);
  } catch (error) {
    const code = (error as NodeJS.ErrnoException).code;
    if (code === "EADDRINUSE" && process.platform !== "win32" && await recoverUnixEndpoint(server, endpoint)) {
      // Successfully rebound only after the socket was proven stale.
    } else
    if (code === "EADDRINUSE" || code === "EACCES") {
      throw new LocalInstanceAlreadyRunningError(nodeId, endpoint);
    } else throw error;
  }

  // The pipe/socket should not by itself keep an otherwise finished process
  // alive, but it remains bound for the lifetime of this Nifibot instance.
  server.unref();

  let closed = false;
  return {
    endpoint,
    close: async () => {
      if (closed) return;
      closed = true;
      await new Promise<void>((resolve) => {
        server.close(() => resolve());
      });

      // Node's server.close already unlinks the Unix socket it created. A
      // second unlink here could delete a replacement bound by a new process.
      // Abnormal-exit leftovers are handled by the guarded recovery path.
    }
  };
}
