import pg from "pg";
import { config } from "../config.js";
import { log } from "../logger.js";
import { LeaseElectorCore, type LeaseStore } from "../runtime/leaseCore.js";

const queryMs = Math.max(250, Math.min(4000, Math.floor(config.CLUSTER_FAILSAFE_MS / 2)));
// Independent pool: lease renewal cannot queue behind history/cache traffic.
const leaseDb = new pg.Pool({
  connectionString: config.DATABASE_URL,
  ssl: config.DATABASE_SSL ? { rejectUnauthorized: false } : undefined,
  max: 1, idleTimeoutMillis: 30000, connectionTimeoutMillis: queryMs,
  query_timeout: queryMs, statement_timeout: queryMs, keepAlive: true
});
leaseDb.on("error", error => log.warn({ err: error }, "Lease pool idle connection dropped"));
const leaseName = "discord-gateway";
const store: LeaseStore = {
  async acquire() {
    const result = await leaseDb.query<{ epoch: string }>(`
      INSERT INTO cluster_leases AS current (lease_name, holder_id, lease_until, epoch, updated_at)
      VALUES ($1, $2, clock_timestamp() + ($3::int * interval '1 millisecond'), 1, clock_timestamp())
      ON CONFLICT (lease_name) DO UPDATE
      SET holder_id = EXCLUDED.holder_id, lease_until = EXCLUDED.lease_until,
          epoch = current.epoch + 1, updated_at = clock_timestamp()
      WHERE current.lease_until <= clock_timestamp()
      RETURNING epoch`, [leaseName, config.NODE_ID, config.CLUSTER_LEASE_MS]);
    return result.rows[0] ?? null;
  },
  async renew(epoch) {
    const result = await leaseDb.query(`
      UPDATE cluster_leases SET lease_until = clock_timestamp() + ($4::int * interval '1 millisecond'), updated_at = clock_timestamp()
      WHERE lease_name = $1 AND holder_id = $2 AND epoch = $3::bigint AND lease_until > clock_timestamp()
      RETURNING epoch`, [leaseName, config.NODE_ID, epoch, config.CLUSTER_LEASE_MS]);
    return result.rowCount === 1;
  },
  async release(epoch) {
    await leaseDb.query(`UPDATE cluster_leases SET lease_until = clock_timestamp(), updated_at = clock_timestamp()
      WHERE lease_name = $1 AND holder_id = $2 AND epoch = $3::bigint`, [leaseName, config.NODE_ID, epoch]);
  }
};
export class LeaderElector {
  private core = new LeaseElectorCore(store, {
    enabled: config.CLUSTER_ENABLED, renewMs: config.CLUSTER_RENEW_MS,
    failSafeMs: config.CLUSTER_FAILSAFE_MS, standbyMs: config.CLUSTER_STANDBY_POLL_MS,
    operationMs: queryMs
  }, undefined, (message, error) => log.warn({ node: config.NODE_ID, err: error }, message));
  isLeader(): boolean { return this.core.isLeader(); }
  getEpoch(): string | null { return this.core.getEpoch(); }
  async stop(): Promise<void> { this.core.stop(); await leaseDb.end().catch(() => undefined); }
  run(acquired: (signal: AbortSignal) => Promise<void>, lost: () => Promise<void>): Promise<void> {
    return this.core.run(acquired, lost);
  }
}
