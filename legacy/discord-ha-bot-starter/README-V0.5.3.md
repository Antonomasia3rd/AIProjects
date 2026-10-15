# Nifibot v0.5.3 — local watchdog

This release adds an updater-aware local watchdog so a fatal Nifibot process exit no longer leaves a node offline indefinitely.

## Behavior

- `npm start` now launches `dist/watchdog.js`, which supervises `dist/index.js`.
- Unexpected non-zero child exits are restarted with bounded exponential backoff.
- Clean Ctrl+C/SIGTERM exits are not restarted.
- Self-update exits use a dedicated code; the old watchdog exits and the updater helper launches the newly installed watchdog after replacing files.
- The updater waits briefly for the old watchdog to leave before swapping `src/sql/dist`.
- `npm run start:direct` remains available for debugging without supervision.
- Startup-folder launchers now point at the watchdog. Run `npm run startup:install` again if you already installed an older launcher.

The existing single-instance named-pipe guard remains in the child process, so duplicate Startup/manual launches still fail closed.
