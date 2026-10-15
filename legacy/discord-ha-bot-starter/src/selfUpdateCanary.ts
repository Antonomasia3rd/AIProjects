// Harmless self-update canary.
// This file is intentionally unused at runtime; TypeScript still emits it into dist/,
// which changes Nifibot's runtime build fingerprint and lets us prove that a
// /cluster-update package was actually staged, applied, rebuilt, restarted, and
// advertised by the target node.
export const SELF_UPDATE_CANARY = "v0.5.1.6a-2026-10-13";
