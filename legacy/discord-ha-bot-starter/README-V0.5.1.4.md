# Nifibot v0.5.1.4 — privacy-aware recovery + cluster build fingerprints

This is an overlay patch for the v0.5.1 cluster after the v0.5.1.3 updater/bootstrap hotfix.

## Changes

- Startup message-history catch-up only attempts channels where Nifibot currently has
  View Channel + Read Message History.
- A channel that becomes inaccessible/fails during catch-up is skipped individually;
  it no longer aborts the whole guild catch-up.
- Server-wide historical scans use the same visibility/read-history filter.
- Startup reconciliation no longer calls Discord Audit Log when Nifibot lacks
  View Audit Log. The summary explicitly reports partial Audit Log coverage and
  does not advance the audit cursor.
- Audit Log fetch failures are reported as partial and leave remaining entries
  for a later startup.
- The startup state snapshot only counts/stores voice/channel state for channels
  Nifibot can currently view.
- Every node computes a SHA-256 build fingerprint from the actually-running
  dist tree + SQL migrations + package.json + package-lock.json.
- Computer-agent heartbeats publish the build fingerprint in existing metadata.
- /status reports whether all online nodes are running the same build and prints
  each node's short fingerprint.
- /computer action:list also shows each node's short build fingerprint.

No database migration or slash-command schema change is required.

## Preferred rollout

Because v0.5.1.3 fixed the Windows self-updater bootstrap, use `/cluster-update`
with this ZIP. For the first successful updater test, target the standby node
first, verify it returns healthy, then update the other node.

Manual fallback:

    npm run build
    npm start
