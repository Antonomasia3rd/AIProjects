# Minecraft-Bedrock-AFK v1.7.20 safe ready-handshake replay lab

v1.7.19 proved that respawn timing is not the issue:

- ClientReadyToSpawn was released at ~+216 ms after death.
- Server ReadyToSpawn arrived at ~+477 ms.
- Existing wire-6 PlayerAction was sent ~202 ms after ReadyToSpawn.
- No server-side alive evidence appeared.

This test moves to the next lifecycle boundary: **client-ready initialization after
respawn**.

## What v1.7.20 does

During the successful initial join, the preload records outbound:

- `serverbound_loading_screen`
- `interact`
- `set_local_player_as_initialized`

After an actual death and the bot's existing final wire-6 PlayerAction, it replays
only the safe readiness subset observed during that successful join:

- **EndLoadingScreen only** (protocol 2193 value `1`) if it was sent initially.
- `Interact / mouse_over_entity` if it was sent initially.
- `SetLocalPlayerAsInitialized` if it was sent initially.

### Safety rule

It **never** replays `StartLoadingScreen` (protocol 2193 value `0`).

Mojang documents that a StartLoadingScreen sent when the server does not expect one
may disconnect the client. This lab therefore tests the ready-state hypothesis
without deliberately taking that risk.

## Run

Copy the three files beside `bot.js` and run:

```cmd
start-v1.7.20-ready-replay-lab.cmd
```

Then:

1. Wait for normal initial spawn.
2. Confirm the console prints the captured `baseline:` readiness packets.
3. `/kill` the bot once.
4. Wait ~12 seconds.

The output file automatically closes:

```text
RESP-READY 📁 capture selesai dan file sudah UNLOCKED
```

Upload the resulting:

```text
.\diag\respawn-ready-replay-*.jsonl
```

No Ctrl+C is required just to upload the capture.

## Interpretation

If the server sends any of:

- `player_has_died=0`
- self `minecraft:health > 0`
- `SetHealth > 0`
- self `EntityEvent=respawn`
- `PlayStatus=player_spawn`

then the missing post-respawn ready handshake was the cause.

If it still fails, the next experiment should compare the modern vanilla / verified
1.26.51 client sequence at the packet-capture level, especially any packet that is
not part of our successful initial-ready sequence.
