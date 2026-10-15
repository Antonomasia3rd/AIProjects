// Dependency-injected supervision. Importing this module never starts a child.
export type Timer = ReturnType<typeof setTimeout>;
export interface SupervisedChild {
  pid?: number;
  connected?: boolean;
  once(event: string, listener: (...args: any[]) => void): unknown;
  kill(signal?: NodeJS.Signals): unknown;
  send?(message: object): unknown;
}
export interface WatchdogRuntime {
  now(): number;
  schedule(callback: () => void, milliseconds: number): Timer;
  cancel(timer: Timer): void;
  spawn(): SupervisedChild;
  write(message: string): void;
  exit(code: number): void;
}
export class Watchdog {
  private runtime: WatchdogRuntime;
  private child: SupervisedChild | null = null;
  private stopping = false;
  private finished = false;
  private restartCount = 0;
  private restartTimer: Timer | null = null;
  private forceTimer: Timer | null = null;
  private stopCode = 0;
  constructor(runtime: WatchdogRuntime) { this.runtime = runtime; }
  private finish(code: number): void {
    if (this.finished) return;
    this.finished = true;
    if (this.restartTimer) this.runtime.cancel(this.restartTimer);
    if (this.forceTimer) this.runtime.cancel(this.forceTimer);
    this.restartTimer = this.forceTimer = null;
    this.runtime.exit(code);
  }
  start(): void {
    if (this.stopping || this.finished || this.child) return;
    const startedAt = this.runtime.now();
    let child: SupervisedChild;
    try { child = this.runtime.spawn(); }
    catch (error) { this.runtime.write(`Child launch failed: ${String(error)}`); this.exited(1, startedAt); return; }
    this.child = child;
    this.runtime.write(`Started Nifibot child PID ${child.pid ?? "?"}.`);
    let reported = false;
    const exited = (code: number | null) => {
      if (reported) return;
      reported = true;
      if (this.child === child) this.child = null;
      this.exited(code, startedAt);
    };
    child.once("error", (error: Error) => {
      this.runtime.write(`Child error: ${error.message}`);
      if (child.pid === undefined) exited(1); // A signal error is not an exit.
    });
    child.once("exit", (code: number | null) => exited(code));
  }
  private exited(code: number | null, startedAt: number): void {
    if (this.stopping) { this.finish(this.stopCode); return; }
    if (code === 0 || code === 75) { this.finish(0); return; }
    if (this.runtime.now() - startedAt >= 60_000) this.restartCount = 0;
    if (++this.restartCount > 10) { this.finish(1); return; }
    const delays = [2000, 5000, 10000, 30000, 60000];
    const delay = delays[Math.min(this.restartCount - 1, delays.length - 1)];
    this.runtime.write(`Child stopped unexpectedly; restarting in ${delay}ms (attempt ${this.restartCount}/10).`);
    // This timer must keep the watchdog alive after the child has exited.
    this.restartTimer = this.runtime.schedule(() => { this.restartTimer = null; this.start(); }, delay);
  }
  stop(reason: string, code = 0): void {
    if (this.stopping || this.finished) return;
    this.stopping = true; this.stopCode = code;
    this.runtime.write(`Stopping watchdog: ${reason}.`);
    if (this.restartTimer) { this.runtime.cancel(this.restartTimer); this.restartTimer = null; }
    const child = this.child;
    if (!child) { this.finish(code); return; }
    // IPC also permits graceful cleanup on Windows, where SIGTERM is a hard
    // termination. Old/non-IPC children retain the signal fallback.
    try { if (child.connected && child.send) child.send({ type: "nifibot-shutdown" }); else child.kill("SIGTERM"); }
    catch { try { child.kill("SIGTERM"); } catch {} }
    if (this.finished || this.child !== child) return;
    this.forceTimer = this.runtime.schedule(() => {
      this.forceTimer = null;
      // killed merely means a signal was sent; this reference tracks exit.
      if (this.child === child) { this.runtime.write("Child shutdown deadline elapsed; forcing termination."); try { child.kill("SIGKILL"); } catch {} }
    }, 10_000);
  }
}
