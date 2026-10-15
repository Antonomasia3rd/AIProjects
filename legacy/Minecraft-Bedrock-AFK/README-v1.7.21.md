# Minecraft-Bedrock-AFK v1.7.21 — Respawn state-2 field A/B lab

## Why v1.7.20 was retired

v1.7.20 replayed three packets from the successful initial-join ready handshake
after the respawn finalizer:

- `serverbound_loading_screen`
- `interact / mouse_over_entity`
- `set_local_player_as_initialized`

The server reproducibly kicked the bot with `unexpected_packet` immediately after
that replay bundle.

**Do not use v1.7.20 again.**

v1.7.21 introduces no new packet type. It modifies only fields of the
already-expected `Respawn(ClientReadyToSpawn / state=2)` packet.

---

## Why test these fields?

Captured server respawn packets consistently contain:

```text
state = 0 or 1
runtime_entity_id = 0
```

while the bot currently replies:

```text
state = 2
position = 0,0,0
runtime_entity_id = <self runtime ID>
```

The official protocol schema calls the third field `Player Runtime Id`, so zero
is NOT being asserted as the standards-correct value. This is an A/B compatibility
test against the behavior actually observed from this server.

The position field is also testable because the server supplies a real respawn
position while the bot sends origin `(0,0,0)`.

---

## Test order

### TEST B — do this first

```cmd
start-v1.7.21-B-zero-runtime.cmd
```

Only change:

```text
runtime_entity_id: <self> -> 0
```

Position remains whatever `bot.js` normally sends.

After `/kill`, wait until the lab prints either:

```text
STATE2-LAB ✅ SERVER ALIVE evidence: ...
```

or:

```text
STATE2-LAB ❌ tidak ada server-alive evidence ...
STATE2-LAB 📁 capture selesai dan file sudah UNLOCKED: ...
```

Upload the resulting:

```text
.\diag\respawn-state2-zero-runtime-*.jsonl
```

Do not run Test C/D yet unless Test B fails.

---

### TEST C — server position only

```cmd
start-v1.7.21-C-server-position.cmd
```

Only changes state-2 position to the latest server `Respawn(state=0)` position.
Self runtime ID remains unchanged.

---

### TEST D — both changes

```cmd
start-v1.7.21-D-zero-runtime-server-position.cmd
```

Changes both runtime ID and position.

---

### Control

```cmd
start-v1.7.21-A-control.cmd
```

Makes no state-2 field change. This exists only for a fresh baseline if needed;
v1.7.19 already effectively established the current behavior.

---

## What v1.7.21 does NOT do

It does NOT:

- replay loading-screen packets;
- replay `interact`;
- send extra `set_local_player_as_initialized`;
- resend the finalizer;
- send action wire 7;
- use `PlayerAuthInput.block_action`;
- change the final PlayerAction;
- modify `bot.js`.

The JSONL automatically closes after the observation window, so it should become
uploadable without stopping Node.
