import { Readable, type Writable } from "node:stream";
import { pipeline } from "node:stream/promises";
import { createGzip } from "node:zlib";
export const ARCHIVE_BATCH_ROWS = 128;
export type ArchiveRow = Record<string, unknown>;
export async function* cursorRows(query: (sql: string) => Promise<{ rows: ArchiveRow[] }>, cursor: "archive_events" | "archive_messages"): AsyncGenerator<ArchiveRow> {
  while (true) {
    const batch = await query(`FETCH FORWARD ${ARCHIVE_BATCH_ROWS} FROM ${cursor}`);
    if (!batch.rows.length) return;
    for (const row of batch.rows) yield row;
  }
}
export async function* archiveJsonLines(metadata: ArchiveRow, events: AsyncIterable<ArchiveRow>, messages: AsyncIterable<ArchiveRow>): AsyncGenerator<string> {
  yield JSON.stringify(metadata) + "\n";
  for await (const row of events) yield JSON.stringify({ recordType: "event", ...row }) + "\n";
  for await (const row of messages) yield JSON.stringify({ recordType: "message", ...row }) + "\n";
}
export interface ArchiveOutput {
  create(temporary: string): Writable;
  commitSnapshot(): Promise<void>;
  rename(temporary: string, target: string): Promise<void>;
  remove(temporary: string): Promise<void>;
}
export async function publishArchive(lines: AsyncIterable<string>, temporary: string, target: string, output: ArchiveOutput): Promise<void> {
  try {
    await pipeline(Readable.from(lines, { objectMode: false, highWaterMark: 64 * 1024 }), createGzip({ level: 6 }), output.create(temporary));
    await output.commitSnapshot();
    await output.rename(temporary, target);
  } catch (error) { await output.remove(temporary).catch(() => undefined); throw error; }
}
