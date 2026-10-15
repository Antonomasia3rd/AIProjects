# Minecraft-Bedrock-AFK v1.7.17 diagnostic overlay

This is a **diagnostic-only overlay** for the existing `v1.7.16-patched` bot.

It deliberately does **not** change:

- the working v1.7.11 respawn handshake;
- wire PlayerAction 6 / face -1 compatibility shim;
- PlayerAuthInput generation;
- movement physics;
- AI/recovery logic;
- reconnect logic;
- `set_local_player_as_initialized` retry behaviour.

## Install

Copy these files into the same directory as the existing `bot.js`:

- `respawn-lifecycle-diag-preload.js`
- `start-v1.7.17-diag.cmd`

Then run:

```cmd
start-v1.7.17-diag.cmd
```

Equivalent command:

```cmd
node -r ./respawn-lifecycle-diag-preload.js bot.js
```

Normal `npm start` remains unchanged and bypasses the overlay.

## What it captures

The overlay keeps a rolling state cache and, on a death/respawn lifecycle,
captures inbound state packets including:

- `respawn`
- `move_player`
- `correct_player_move_prediction`
- `set_health`
- `update_attributes`
- `update_abilities`
- `update_adventure_settings` / `adventure_settings`
- `set_player_game_type` / `update_player_game_type`
- `update_client_input_locks`
- `set_entity_data` / `set_actor_data`
- `set_entity_motion` / `set_actor_motion`
- `mob_effect`
- `set_spawn_position`
- `change_dimension`
- `play_status`
- `death_info`
- `entity_event`

It also observes outbound:

- `respawn`
- `player_action`
- `set_local_player_as_initialized`
- sampled `player_auth_input`

At death it writes a **pre-death baseline snapshot**. After
`ReadyToSpawn`, it writes snapshots at approximately:

- +250 ms
- +1 s
- +3 s
- +5 s
- +10 s
- +20 s
- +40 s
- +55 s

Each snapshot contains a `changed` array showing which cached state packet
families differ from the pre-death baseline.

During the capture window it also records the **first occurrence of every
inbound packet name**, even when that packet is not in the payload whitelist.
That helps reveal an unexpected lifecycle packet without flooding the console.

## Output

A JSONL file is written to:

```text
.\diag\respawn-lifecycle-<timestamp>-c1.jsonl
```

After reproducing one death -> respawn -> frozen-movement cycle, stop the bot
and send that JSONL file.

## Optional environment variables

Default capture window is 60 seconds:

```cmd
set MC_RESPAWN_DIAG_MS=90000
```

Change output directory:

```cmd
set MC_RESPAWN_DIAG_DIR=.\diag-custom
```

Disable console diagnostic spam while keeping JSONL output:

```cmd
set MC_RESPAWN_DIAG_CONSOLE=0
```

## Rollback

Nothing needs to be reverted. Run the original:

```cmd
npm start
```

and the preload is not loaded.
