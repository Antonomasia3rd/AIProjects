# Minecraft-Bedrock-AFK v1.7.22 — no-finalizer A/B lab

## What this tests

All state-2 field tests are now exhausted:

- `runtime_entity_id = 0` broke the handshake before ReadyToSpawn.
- using the server-provided state-0 position still reached ReadyToSpawn but did
  not revive the player.
- zero position + self runtime (the normal v1.7.16 flow) also reaches
  ReadyToSpawn but does not revive.
- both `face=0` and `face=-1` final PlayerAction forms have previously failed.

Protocol 2193 documents `RespawnPacket` itself as the respawn handshake and says
one RespawnPacket is sent by the client while three are sent by the server.

This lab therefore tests the remaining clean A/B:

**What if the standalone PlayerAction Respawn sent after ReadyToSpawn is not
required and is actually extra for this server's automatic respawn flow?**

## What v1.7.22 changes

Nothing about state-2.

The normal bot sequence reaches:

```text
server state-0
client state-2
server state-0
server state-1 / ReadyToSpawn
```

When `bot.js` then queues its standalone PlayerAction wire-6, the preload
intercepts and swallows it.

`bot.js` is allowed to continue as if `queue()` returned normally, so its own
post-respawn logic does not intentionally disconnect just because the test
packet was suppressed.

## What it does NOT do

- no loading-screen packet
- no interact replay
- no extra local-player init
- no action 7
- no PlayerAuthInput block_action
- no state-2 runtime/position modification
- no finalizer retry
- no bot.js modification

## Run

```cmd
start-v1.7.22-no-finalizer-lab.cmd
```

Wait for normal spawn, then `/kill` once.

Expected diagnostic sequence:

```text
FINALIZER-LAB 💀 actual self death detected
FINALIZER-LAB ➡️ normal state-2 observed ...
FINALIZER-LAB 🟢 ReadyToSpawn ...
FINALIZER-LAB 🛑 SUPPRESSED final wire-6 ...
```

If omission fixes the server-side lifecycle:

```text
FINALIZER-LAB ✅ SERVER ALIVE evidence WITHOUT final wire-6: ...
```

If not:

```text
FINALIZER-LAB ❌ tidak ada server-alive evidence ...
FINALIZER-LAB 📁 capture selesai dan file sudah UNLOCKED: ...
```

Upload:

```text
diag\respawn-no-finalizer-*.jsonl
```

The one-shot JSONL closes automatically after the observation window.
