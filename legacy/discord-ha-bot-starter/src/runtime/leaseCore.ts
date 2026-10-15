import { performance } from "node:perf_hooks";
export interface LeaseClock {
  now(): number;
  schedule(callback: () => void, milliseconds: number): ReturnType<typeof setTimeout>;
  cancel(timer: ReturnType<typeof setTimeout>): void;
}
export const monotonicClock: LeaseClock = {
  now: () => performance.now(), schedule: (callback, delay) => setTimeout(callback, delay), cancel: clearTimeout
};
export interface LeaseStore {
  acquire(): Promise<{ epoch: string } | null>;
  renew(epoch: string): Promise<boolean>;
  release(epoch: string): Promise<void>;
}
export interface LeaseOptions {
  enabled: boolean; renewMs: number; failSafeMs: number; standbyMs: number; operationMs: number;
  prepareMs?: number;
  beforeAcquire?: (signal: AbortSignal) => Promise<boolean>;
}
const aborted = () => new Error("Lease operation cancelled or its deadline elapsed.");
export function bounded<T>(operation: Promise<T>, milliseconds: number, clock: LeaseClock,
    signal?: AbortSignal): Promise<T> {
  return new Promise((resolve, reject) => {
    let settled = false;
    const finish = (value: T | Error, failed: boolean) => {
      if (settled) return;
      settled = true; clock.cancel(timer); signal?.removeEventListener("abort", cancel);
      if (failed) reject(value); else resolve(value as T);
    };
    const cancel = () => finish(aborted(), true);
    const timer = clock.schedule(cancel, Math.max(0, milliseconds));
    signal?.addEventListener("abort", cancel, { once: true });
    operation.then(value => finish(value, false), error => finish(error, true));
    if (signal?.aborted) cancel();
  });
}
// No config/DB/client import: fake clocks and stores exercise the real loop.
export class LeaseElectorCore {
  private store: LeaseStore;
  private options: LeaseOptions;
  private clock: LeaseClock;
  private log: (message: string, error?: unknown) => void;
  private stopSignal = new AbortController();
  private ownerSignal: AbortController | null = null;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private deadline = 0;
  private epoch: string | null = null;
  private generation = 0;
  private lost: () => Promise<void> = async () => {};
  constructor(store: LeaseStore, options: LeaseOptions, clock = monotonicClock,
      log: (message: string, error?: unknown) => void = () => {}) {
    this.store = store; this.options = options; this.clock = clock; this.log = log;
  }
  isLeader(): boolean { return this.epoch !== null && !!this.ownerSignal && !this.ownerSignal.signal.aborted &&
    (!this.options.enabled || this.clock.now() < this.deadline); }
  getEpoch(): string | null { return this.isLeader() ? this.epoch : null; }
  private arm(startedAt: number): void {
    if (this.timer) this.clock.cancel(this.timer);
    this.deadline = startedAt + this.options.failSafeMs;
    this.timer = this.clock.schedule(() => { this.timer = null; this.demote("Independent lease deadline elapsed"); },
      Math.max(0, this.deadline - this.clock.now()));
  }
  private demote(reason: string): void {
    const epoch = this.epoch;
    if (epoch === null) return;
    this.epoch = null; ++this.generation;
    if (this.timer) this.clock.cancel(this.timer);
    this.timer = null;
    // Abort first: an in-flight login must destroy its unpublished client too.
    this.ownerSignal?.abort(); this.ownerSignal = null;
    this.log(reason);
    try { void this.lost().catch(error => this.log("Leader workload stop failed", error)); }
    catch (error) { this.log("Leader workload stop failed", error); }
    if (this.options.enabled) void bounded(this.store.release(epoch), this.options.operationMs, this.clock)
      .catch(error => this.log("Lease release failed; server expiry remains authoritative", error));
  }
  stop(): void { this.stopSignal.abort(); this.demote("Leader elector stopped"); }
  private async sleep(milliseconds: number, signal = this.stopSignal.signal): Promise<void> {
    // This resolved-by-timer promise also gets cancelled when shutdown/demotion
    // interrupts a wait. bounded owns and clears the actual timer.
    await bounded(new Promise<void>(() => {}), milliseconds, this.clock, signal).catch(() => {});
  }
  async run(acquired: (signal: AbortSignal) => Promise<void>, lost: () => Promise<void>,
      refreshed?: (signal: AbortSignal) => Promise<void>): Promise<void> {
    this.lost = lost;
    if (!this.options.enabled) {
      this.epoch = "0"; this.ownerSignal = new AbortController();
      await acquired(this.ownerSignal.signal)
        .catch(error => { this.demote("Unclustered workload failed to start"); throw error; });
      return;
    }
    while (!this.stopSignal.signal.aborted) {
      if (!this.isLeader()) {
        if (this.epoch !== null) this.demote("Lease deadline passed before an awaited continuation");
        if (this.options.beforeAcquire) {
          const preparation = new AbortController();
          const stopPreparation = () => preparation.abort();
          this.stopSignal.signal.addEventListener("abort", stopPreparation, { once: true });
          let ready = false;
          try {
            ready = await bounded(this.options.beforeAcquire(preparation.signal), this.options.prepareMs ?? this.options.operationMs,
              this.clock, preparation.signal);
          } catch (error) { if (!this.stopSignal.signal.aborted) this.log("Standby workload preparation failed", error); }
          finally { this.stopSignal.signal.removeEventListener("abort", stopPreparation); preparation.abort(); }
          if (!ready || this.stopSignal.signal.aborted) { await this.sleep(this.options.standbyMs); continue; }
        }
        const startedAt = this.clock.now();
        try {
          const attempt = this.store.acquire();
          // If a timed-out acquire finishes later, release only its own epoch.
          let accepted = false;
          void attempt.then(lease => {
            if (lease && !accepted && (this.stopSignal.signal.aborted || this.clock.now() - startedAt >= this.options.operationMs))
              void bounded(this.store.release(lease.epoch), this.options.operationMs, this.clock).catch(() => {});
          }, () => {});
          const lease = await bounded(attempt, this.options.operationMs, this.clock, this.stopSignal.signal);
          if (lease && !this.stopSignal.signal.aborted && this.clock.now() < startedAt + Math.min(this.options.operationMs, this.options.failSafeMs)) {
            accepted = true; this.epoch = lease.epoch; this.ownerSignal = new AbortController();
            const generation = ++this.generation; this.arm(startedAt);
            const signal = this.ownerSignal.signal;
            try { await bounded(acquired(signal), this.deadline - this.clock.now(), this.clock, signal); }
            catch (error) { if (this.generation === generation) this.demote("Leader workload startup failed or expired"); this.log("Leader startup interrupted", error); }
            if (this.generation === generation && this.isLeader()) continue;
          } else if (lease) void bounded(this.store.release(lease.epoch), this.options.operationMs, this.clock).catch(() => {});
        } catch (error) { if (!this.stopSignal.signal.aborted) this.log("Leader acquisition failed", error); }
        await this.sleep(this.options.standbyMs);
        continue;
      }
      const signal = this.ownerSignal!.signal, generation = this.generation, epoch = this.epoch!;
      await this.sleep(this.options.renewMs, signal);
      if (!this.isLeader() || this.stopSignal.signal.aborted || generation !== this.generation) continue;
      const startedAt = this.clock.now();
      try {
        const renewed = await bounded(this.store.renew(epoch),
          Math.min(this.options.operationMs, this.deadline - startedAt), this.clock, signal);
        if (generation !== this.generation) continue;
        if (!renewed || this.clock.now() >= this.deadline) this.demote("Lease ownership lost or deadline elapsed");
        else {
          this.arm(startedAt);
          if (refreshed) try { await bounded(refreshed(signal), this.deadline - this.clock.now(), this.clock, signal); }
          catch (error) {
            if (generation === this.generation) this.demote("Leader workload refresh failed or expired");
            this.log("Leader workload refresh interrupted", error);
          }
        }
      } catch (error) {
        if (generation === this.generation && this.clock.now() >= this.deadline) this.demote("Lease renewal deadline elapsed");
        if (!this.stopSignal.signal.aborted) this.log("Lease renewal failed", error);
      }
    }
    this.demote("Leader elector stopped");
  }
}
