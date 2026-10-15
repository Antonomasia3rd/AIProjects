export interface SocketIdentity { socket: boolean; device: number | bigint; inode: number | bigint; }
export interface StaleSocketProbe {
  stat(): Promise<SocketIdentity | null>;
  probe(): Promise<"active" | "refused" | "unknown">;
  unlink(): Promise<void>;
}
// Call while holding the endpoint's exclusive recovery lock. A timeout, access
// error, ordinary file, or changed inode is never grounds for deletion.
export async function removeConfirmedStaleSocket(io: StaleSocketProbe): Promise<boolean> {
  const before = await io.stat();
  if (!before) return true;
  if (!before.socket || await io.probe() !== "refused") return false;
  const after = await io.stat();
  if (!after) return true;
  if (!after.socket || before.device !== after.device || before.inode !== after.inode) return false;
  await io.unlink(); return true;
}
