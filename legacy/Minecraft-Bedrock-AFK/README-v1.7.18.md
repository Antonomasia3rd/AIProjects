# v1.7.18 finalizer-retry lab

This is a narrow experimental preload for the existing `v1.7.16-patched`
`bot.js`.

## Why this test exists

The v1.7.17 capture showed the real death sequence clearly:

1. Server sets the bot's self metadata `player_has_died=1`.
2. Server sets the bot's `minecraft:health` attribute to `0`.
3. Bot sends `Respawn state 2`.
4. Server sends `ReadyToSpawn` (`state 1`).
5. Bot sends the known-good standalone wire-6 final PlayerAction.
6. The server never sends any observed "alive again" signal:
   - no `player_has_died=0`
   - no self health > 0
   - no `entity_event: respawn`
   - no `play_status: player_spawn`
7. Server still processes movement ticks but authoritative movement stays zero.

This lab tests only whether the finalizer is being missed / timing-sensitive.

## Behavior

After **actual self death** only (not initial join), the preload observes the
bot's own existing final PlayerAction. If no server-side alive evidence appears,
it resends **the exact same packet** after:

- +1.2 s
- +2.6 s

It does NOT:

- send PlayerAction wire 7;
- use `PlayerAuthInput.block_action`;
- modify `bot.js`;
- alter normal movement/AI;
- synthesize health or metadata locally.

## Run

Copy these files next to `bot.js`, then:

```cmd
start-v1.7.18-finalizer-retry-lab.cmd
```

Test one `/kill`, then leave the bot running for ~8–10 seconds after
`ReadyToSpawn`.

The console is intentionally compact. The JSONL is written under:

```text
.\diag\respawn-finalizer-lab-*.jsonl
```

Send both the console excerpt and JSONL after the test.

## What counts as success?

Any of these server-originated signals cancels retries and marks respawn complete:

- `player_has_died = 0` for self
- self `minecraft:health > 0`
- `SetHealth > 0`
- self `EntityEvent = respawn`
- `PlayStatus = player_spawn`

If none appears even after both identical wire-6 retries, the next hypothesis
is no longer packet loss/timing: we should compare the exact modern vanilla
1.26.51/1.26.52 finalization sequence against this client.
