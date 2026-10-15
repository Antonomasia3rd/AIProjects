Nifibot v0.5.1.7 — Node-based updater helper bootstrap

The v0.5.1.5 PowerShell apply helper never reached the script body on A1504VA:
there was no apply log and no READY marker, although powershell.exe itself
launched without a spawn error.

This patch removes PowerShell from the post-staging apply path entirely.
Nifibot now generates apply-update.cjs and launches it with the SAME node.exe
that is already running Nifibot.

Benefits:
- no PowerShell ExecutionPolicy / script-host dependency
- no PowerShell named-argument quoting edge cases
- helper READY marker is written by Node itself
- durable .nifibot-updates/apply-<id>.log remains
- rollback behavior remains
- restart behavior remains

Apply MANUALLY once to both v0.5.1.5 nodes:
  overwrite src/selfUpdate.ts
  npm run build
  npm start

No migration, npm install, or command redeploy is required.

After both nodes have this bootstrap patch, retry the corrected v0.5.1.6a
self-update canary through /cluster-update.
