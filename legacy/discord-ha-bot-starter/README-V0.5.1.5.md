# Nifibot v0.5.1.5 — updater apply/relaunch reliability bootstrap

This overlay combines the v0.5.1.4 privacy/build-fingerprint changes with the
v0.5.1.3 DB/updater bootstrap and fixes the post-staging apply/restart phase.

## Why this bootstrap is manual once

The previous updater could successfully validate/stage a release, then shut the
Node process down while the detached PowerShell apply helper failed or never
relaunched Nifibot. Because the updater itself is the broken component, apply
this overlay manually to every current node once.

## Improvements

- Detached apply helper must acknowledge startup before Nifibot exits.
- If helper launch acknowledgement is missing, Nifibot stays online.
- Durable updater log is written to `.nifibot-updates/apply-<commandId>.log`.
- Live-file replacement still keeps a rollback snapshot.
- Restart no longer depends on `npm start`; updater writes a small restart CMD
  which launches the current node.exe against `dist/index.js`.
- Includes v0.5.1.4 privacy-aware catch-up and build fingerprint reporting.
- Includes pg.Pool idle ECONNRESET handling and npm-cli.js staging fix.

## Apply

Stop Nifibot on the node, overlay this ZIP, then:

```cmd
npm run build
npm start
```

No migration, npm install, or command redeploy is required.

After all online nodes have this bootstrap, use the next release to test
`/cluster-update` again.
