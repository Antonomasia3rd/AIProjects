# Minecraft-Bedrock-AFK v1.7.23 — Vanilla Reference Relay

## Purpose

The respawn field/order experiments are exhausted.

Current AFK-bot evidence shows:

- protocol-2193 ClientReadyToSpawn with the bot's own runtime ID is required to
  reach ReadyToSpawn;
- changing state-2 position does not revive the player;
- delaying state-2 does not revive the player;
- face 0 versus face -1 does not revive the player;
- retrying the wire-6 PlayerAction does not revive the player;
- omitting the wire-6 PlayerAction entirely also does not revive the player;
- replaying the whole initial ready-handshake bundle causes
  `unexpected_packet`.

The next useful measurement is therefore the packet sequence from a **real
Minecraft for Windows client against this same server**.

`bedrock-protocol` has a built-in `Relay` MITM API, so this tool runs:

```text
Official Minecraft
       |
       v
127.0.0.1:19132
  passive Relay
       |
       v
Nifirealm.aternos.me:24128
```

The logger never calls packet cancellation, edits packet parameters, or injects
gameplay packets.

---

## Where to put it

Extract these files into the existing `Minecraft-Bedrock-AFK` directory.

That directory already has `node_modules` / `bedrock-protocol`, so no separate
npm project is needed.

---

## Run

Double-click or execute:

```cmd
start-v1.7.23-vanilla-reference-relay.cmd
```

The default relay listens on:

```text
127.0.0.1:19132
```

and connects upstream to:

```text
Nifirealm.aternos.me:24128
```

If Aternos changes the port:

```cmd
set MC_REF_TARGET_PORT=NEWPORT
start-v1.7.23-vanilla-reference-relay.cmd
```

If local UDP port 19132 is already occupied:

```cmd
set MC_REF_LISTEN_PORT=19133
start-v1.7.23-vanilla-reference-relay.cmd
```

Then use the same new port in Minecraft's server entry.

---

## Microsoft/Xbox authentication

The Relay performs its own authenticated upstream connection.

On the first run it may print a Microsoft device-code sign-in message. Follow
that message in the browser.

Tokens are cached locally under:

```text
.\.relay-auth\
```

Do **not** upload `.relay-auth`.

The capture JSONL contains decoded protocol objects, not your Microsoft token
cache.

---

## Connect official Minecraft

In Minecraft for Windows, add a server:

```text
Server Address: 127.0.0.1
Port:           19132
```

Join it.

Wait until the terminal says:

```text
VANILLA-REF initial player_spawn observed
```

Then kill the **vanilla player** exactly once from the Aternos/server console,
the same way the bot has been tested.

Do not connect the AFK bot at the same time for this reference test.

---

## If localhost does not work

Run:

```powershell
.\show-loopback-help.ps1
```

The old UWP Minecraft package can require a Windows AppContainer loopback
exemption. Newer/unpackaged/GDK installations may not need one.

An easier alternative is often to use this PC's LAN IPv4 address in Minecraft
instead of `127.0.0.1`; the relay binds to `0.0.0.0` by default.

The helper prints candidate LAN IPv4 addresses but changes no Windows setting.

---

## What is captured

Before death:

- lifecycle/control packets;
- initial ready-handshake packets;
- one sampled PlayerAuthInput per second.

After a real death signal:

- **every client -> server packet name**;
- full payload for every non-PlayerAuthInput serverbound packet;
- relevant PlayerAuthInput fields for every post-death input packet;
- every server -> client packet name;
- full payload for respawn/health/metadata/entity-event/movement/input-lock and
  other lifecycle packets.

This means an unexpected packet type will still appear in the sequence even if
we did not know in advance that it mattered.

The capture automatically closes 15 seconds after death and prints:

```text
VANILLA-REF capture selesai dan file sudah UNLOCKED
```

Upload:

```text
diag\vanilla-respawn-reference-*.jsonl
```

Also paste/upload the console if the Relay fails to authenticate, connect, or
parse a packet.

---

## Environment overrides

```cmd
set MC_REF_VERSION=1.26.51
set MC_REF_LISTEN_HOST=0.0.0.0
set MC_REF_LISTEN_PORT=19132
set MC_REF_TARGET_HOST=Nifirealm.aternos.me
set MC_REF_TARGET_PORT=24128
set MC_REF_POST_DEATH_MS=15000
set MC_REF_OUTPUT_DIR=diag
set MC_REF_PROFILES_DIR=.relay-auth
```

No AFK `bot.js` changes are made by this lab.
