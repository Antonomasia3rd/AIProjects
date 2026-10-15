# Nifibot

Current consolidated Nifibot source tree.

This bot can control computers and apply uploaded program updates with the
Windows account's permissions. Treat access to its bot commands, database,
release packages and `.env` as access to those computers. Keep credentials,
message-history exports, logs and dumps private. `COMPUTER_CONTROL_USER_IDS`
provides the existing optional per-user restriction for control/update commands;
an empty list leaves command access to Discord's configured permissions.
`COMPUTER_CONTROL_ENABLED=0` disables computer-control commands, but does not
disable the separate cluster-update command. Restrict that command's Discord
permissions or its user allowlist when needed. No extra mandatory trust or
credential-protection mechanism is added by the reliability fixes below.

First install / manual upgrade:

```cmd
npm ci
npm run build
npm run db:migrate
npm run commands:deploy
npm start
```

If `node_modules` is already current and `package-lock.json` did not change, `npm ci`
is not required for a normal overlay upgrade.

Important local-only file:
- `.env` — never put this in a release ZIP. Each physical cluster node keeps its
  own `.env` and must use a unique `NODE_ID`.

After all nodes are on v0.5.1, future release ZIPs can be deployed from Discord:

```text
/cluster-update package:<zip> reason:<reason>
```

See `README-V0.5.1.md` for details.

## Current launch and recovery behavior

`npm start`, `run-bot.cmd` and the per-user Startup-folder launcher all use the
watchdog. `npm run start:direct` remains an explicit unsupervised/debug launch.
The watchdog keeps crash-backoff timers alive, limits consecutive restart
attempts and does not restart intentional exits or the self-update handoff.
It requests graceful child shutdown over IPC on Windows, then forces termination
after ten seconds if the child has not actually exited.

Gateway and voice leadership use an independent monotonic deadline while login or a
renewal query is pending. Expiry aborts pending login and destroys the gateway;
late results cannot restore expired ownership. Lease queries use a separate
small pool with client/server query bounds so message-history work does not
block renewals. Normal database queries also have a fifteen-second bound.
JavaScript timers cannot run while the event loop or machine is suspended;
the continuation checks the monotonic deadline before accepting a late result.
This does not replace actual multi-node/network-failure acceptance testing.
Voice standby login and target synchronization are cancellable and bounded too;
late work cannot join after its ownership signal expires.

Startup launcher filenames include a stable hash of the project and node ID.
Installing one instance does not overwrite another instance with a colliding
readable name. An exact known older launcher is migrated after successful new
publication; unrelated or uncertain older entries are left unchanged. Batch
paths support percent/exclamation characters and Unicode without inserting the
node ID as batch command text. Startup remains in the current user's folder.

Monthly archives fetch 128 rows at a time from a read-only snapshot and stream
JSONL through compression. An existing archive is replaced only after the new
temporary file and snapshot complete. Memory no longer scales with every row
in the month, though one unusually large record still needs space to serialize.

Windows update staging resolves npm from its installed location, an inherited
`npm_execpath`, or `NIFIBOT_NPM_CLI` pointing to `npm-cli.js`. A direct or Startup
launch does not need to run through npm. No package installation, migration,
command deployment or update is performed by the offline tests.

Unix instance recovery only removes a socket after connection refusal and a
matching device/inode check under a recovery lock. Active endpoints, timeouts,
permission failures and regular files are left intact. An abandoned `.recovery`
lock is deliberately not guessed stale; inspect it before manual removal.
Windows continues using kernel named pipes.

## Offline regression checks

With the existing development dependencies installed, run `npm test`.
It compiles to ignored `.offline-tests/`, then runs bounded fake-clock,
fake-process and fake-lease tests. These tests never import the production
configuration, read `.env`, log in to Discord, connect to PostgreSQL, install
Startup entries, change playback, or run the updater. `npm run build` still
produces the normal runnable `dist/` output and does not start it.

Keep `.env`, `history-staging/`, `archives/`, `logs/`, `node_modules/`, `dist/`,
`.nifibot-updates/` and `.offline-tests/` out of source publication. The history
and archive directories can contain actual guild messages and attachment URLs.
