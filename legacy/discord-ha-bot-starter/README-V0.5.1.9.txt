Nifibot v0.5.1.9 — Startup-folder + local single-instance patch

Adds:
- npm run startup:install
- npm run startup:status
- npm run startup:remove
- Windows Startup-folder launcher for the current user
- local per-NODE_ID single-instance guard using a Windows named pipe
- no stale Windows lock file after crashes: the named pipe disappears with the process
- updater restart output now goes to restart-<command>.log instead of apply-<command>.log

Startup-folder behavior:
- starts after this Windows user logs in
- launches node.exe directly against dist\\index.js
- writes runtime output to logs\\startup-runtime.log
- writes launcher start/exit entries to logs\\startup-launcher.log
- keeps a console window open while Nifibot runs

Single-instance behavior:
Starting Nifibot twice on the same machine with the same NODE_ID causes the
second copy to log a warning and exit before touching Supabase or Discord.

No DB migration, dependency change, or slash-command redeploy is required.
