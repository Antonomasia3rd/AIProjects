import { log } from "./logger.js";

const RETRY_DELAYS_MS = [250, 750, 1500];

function isRetryableDbError(error: unknown): boolean {
  if (!(error instanceof Error)) return false;

  const code = String((error as Error & { code?: unknown }).code ?? "");
  if (
    code.startsWith("08") ||
    ["57P01", "57P02", "57P03", "53300", "53400"].includes(code)
  ) {
    return true;
  }

  const message = error.message.toLowerCase();
  return [
    "connection timeout",
    "connection terminated unexpectedly",
    "connection terminated",
    "econnreset",
    "etimedout",
    "socket hang up",
    "broken pipe",
    "network is unreachable"
  ].some((needle) => message.includes(needle));
}

const wait = (ms: number) =>
  new Promise<void>((resolve) => setTimeout(resolve, ms));

export async function withDbRetry<T>(
  label: string,
  operation: () => Promise<T>
): Promise<T> {
  let lastError: unknown;

  for (let attempt = 0; attempt <= RETRY_DELAYS_MS.length; attempt += 1) {
    try {
      return await operation();
    } catch (error) {
      lastError = error;
      if (!isRetryableDbError(error) || attempt === RETRY_DELAYS_MS.length) {
        throw error;
      }

      const delayMs = RETRY_DELAYS_MS[attempt];
      log.warn(
        { err: error, label, attempt: attempt + 1, delayMs },
        "Transient database operation failed; retrying"
      );
      await wait(delayMs);
    }
  }

  throw lastError;
}
