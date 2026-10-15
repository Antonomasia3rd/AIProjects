Nifibot v0.5.1.8 known-good baseline sync

Purpose:
  Force multiple cluster nodes back onto the exact same source/runtime baseline
  after updater-canary experiments left their fingerprints different.

This archive intentionally does NOT contain:
  .env
  node_modules
  dist
  .nifibot-updates
  history-staging
  archives
  logs

Recommended reset on each node:
  1. Stop Nifibot.
  2. Delete src, sql, and dist from the live project.
  3. Extract this archive into the project root.
  4. npm run build
  5. npm start

Do not delete .env or node_modules.
No migration or slash-command redeploy is required for this reset.
