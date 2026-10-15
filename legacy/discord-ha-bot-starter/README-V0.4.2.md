# Nifibot v0.4.2 — Independent Voice HA

This patch is for an installation that already has v0.4.1 applied.

## What changed

Voice ownership is now independent from the main Discord command/logging leader.
Every running Nifibot process can be a voice candidate, including HA standby
nodes.

Two independent leases now exist in the existing `cluster_leases` table:

- `discord-gateway` — owns the full command/logging Discord client
- `discord-voice` — owns the configured VC connection

The same physical machine may own both, or two different machines may own them.

Every node with Voice HA enabled keeps a tiny Discord gateway containing only
`Guilds` + `GuildVoiceStates` once a voice target has been configured. Only the
node holding `discord-voice` actually joins the VC.

This avoids tying the VC streak to whichever laptop currently owns the main
Nifibot gateway.

## Apply

No new npm package is needed if `@discordjs/voice@0.19.2` is already installed.
No database migration is needed; the existing `cluster_leases` table is reused.
No slash-command redeploy is strictly required, but redeploying is harmless.

Overlay the patch, then:

```cmd
npm run build
npm start
```

Configure the VC (if not already configured):

```text
/logging startup-voice enabled:true channel:<VC>
```

The command name is retained for compatibility, but it now means the persistent
Voice HA target rather than "join only when the main leader starts".

## Default voice HA timings

- lease: 12 s
- renew: 3 s
- standby poll: 2 s
- DB fail-safe: 7 s
- voice-ready timeout: 6 s
- initial voice-agent delay after process startup: 6 s

The startup delay avoids having the full gateway and voice-only gateway IDENTIFY
at almost the same instant on a node that happens to win both roles.

## Expected logs

Every node:

```text
Independent voice HA agent started
Voice-only Discord gateway ready
```

Exactly one node:

```text
Voice ownership acquired
Voice HA connection ready
```

If that node dies, another already-running candidate takes the `discord-voice`
lease after it expires and joins the configured VC without needing to become the
main command/logging leader.

## Important

Do not run multiple Nifibot processes with the same `NODE_ID`. Each physical
host/process must keep a unique `NODE_ID`, exactly like the existing gateway HA.
