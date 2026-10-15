import fs from "node:fs/promises";
import path from "node:path";
import { db } from "../db.js";
import { log } from "../logger.js";

const MIGRATION_LOCK_KEY = "nifibot-schema-migrations-v1";

async function main() {
  // Cluster updates can stage several nodes in parallel. Serialize migration
  // runners with a PostgreSQL advisory lock so two nodes can't both decide the
  // same migration is missing and race its schema_migrations insert.
  const client = await db.connect();

  try {
    await client.query("SELECT pg_advisory_lock(hashtext($1))", [MIGRATION_LOCK_KEY]);

    await client.query(`
      CREATE TABLE IF NOT EXISTS schema_migrations (
        filename text PRIMARY KEY,
        applied_at timestamptz NOT NULL DEFAULT clock_timestamp()
      )
    `);

    const sqlDir = path.join(process.cwd(), "sql");
    const files = (await fs.readdir(sqlDir))
      .filter((name) => name.endsWith(".sql"))
      .sort();

    for (const filename of files) {
      const exists = await client.query(
        "SELECT 1 FROM schema_migrations WHERE filename = $1",
        [filename]
      );

      if (exists.rowCount) {
        log.info({ filename }, "Migration already applied");
        continue;
      }

      const sql = await fs.readFile(path.join(sqlDir, filename), "utf8");

      try {
        await client.query("BEGIN");
        await client.query(sql);
        await client.query(
          "INSERT INTO schema_migrations (filename) VALUES ($1)",
          [filename]
        );
        await client.query("COMMIT");
        log.info({ filename }, "Migration applied");
      } catch (error) {
        await client.query("ROLLBACK");
        throw error;
      }
    }
  } finally {
    await client
      .query("SELECT pg_advisory_unlock(hashtext($1))", [MIGRATION_LOCK_KEY])
      .catch(() => undefined);
    client.release();
  }
}

main()
  .then(async () => {
    await db.end();
  })
  .catch(async (error) => {
    log.error({ err: error }, "Migration failed");
    await db.end().catch(() => undefined);
    process.exitCode = 1;
  });
