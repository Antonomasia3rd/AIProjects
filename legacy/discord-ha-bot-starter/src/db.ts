import pg from "pg";
import { config } from "./config.js";
import { log } from "./logger.js";

const { Pool } = pg;

export const db = new Pool({
  connectionString: config.DATABASE_URL,
  ssl: config.DATABASE_SSL ? { rejectUnauthorized: false } : undefined,
  max: 5,
  idleTimeoutMillis: 30_000,
  connectionTimeoutMillis: 15_000,
  query_timeout: 15_000,
  statement_timeout: 15_000,
  keepAlive: true,
  keepAliveInitialDelayMillis: 10_000
});

// node-postgres can emit an `error` event when an *idle* pooled connection is
// reset by the network/pooler. Without a Pool-level listener, EventEmitter
// treats that as an uncaught exception and our process-level safety handler
// shuts Nifibot down. The pool already discards the broken client; logging the
// event here keeps the process alive so the next query can use/open a healthy
// connection. Query-time failures are handled separately by dbRetry.ts.
db.on("error", (error: Error) => {
  log.warn(
    {
      err: {
        name: error.name,
        message: error.message,
        code: (error as Error & { code?: unknown }).code
      }
    },
    "PostgreSQL idle pooled connection dropped; keeping Nifibot online"
  );
});

export async function testDatabase(): Promise<void> {
  await db.query("SELECT 1");
}
