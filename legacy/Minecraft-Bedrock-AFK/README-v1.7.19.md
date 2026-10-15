# Minecraft-Bedrock-AFK v1.7.19 reference-timing lab

This is a one-variable experiment for the existing `v1.7.16-patched` bot.

## Why

The v1.7.18 test ruled out a missed/timing-sensitive **final** PlayerAction:
the first wire-6 finalizer plus two identical retries still produced no
server-side alive state.

The remaining timing mismatch versus a known working Bedrock respawn flow is
earlier:

- observed bot behavior: `ClientReadyToSpawn (state=2)` about **60 ms** after
  self `death_animation`;
- reference flow: deliberately waits about **200 ms** before sending state=2;
- both then wait for `ReadyToSpawn (state=1)`;
- our existing finalizer already lands about 200 ms after ReadyToSpawn.

So v1.7.19 changes only the first delay.

## What the preload does

On an actual self death only:

1. Lets `bot.js` detect death normally.
2. Intercepts the first outbound `respawn` packet with `state: 2`.
3. Holds it until exactly ~200 ms after the death event.
4. Sends it unchanged using the same client method.
5. Suppresses duplicate state-2 packets during the same death lifecycle.
6. Does not touch the final wire-6 PlayerAction.
7. Watches for actual server-side alive evidence.

Alive evidence:

- self `player_has_died=0`
- self `minecraft:health > 0`
- `SetHealth > 0`
- self `EntityEvent=respawn`
- `PlayStatus=player_spawn`

## Run

Put these files next to `bot.js`, then run:

```cmd
start-v1.7.19-reference-timing-lab.cmd
```

After initial spawn:

1. `/kill` the bot once.
2. Wait ~15 seconds.
3. Look for:

```text
RESP-TIMING 📁 capture selesai dan file sudah UNLOCKED
```

At that point the JSONL can be uploaded **without Ctrl+C**.

Output:

```text
.\diag\respawn-reference-timing-*.jsonl
```

## Expected console shape

```text
⏱️ RESP-TIMING 💀 actual self death detected
⏱️ RESP-TIMING ⏸️ bot wanted state-2 at +60 ms; holding 140 ms
⏱️ RESP-TIMING ➡️ ClientReadyToSpawn state-2 RELEASED at +200 ms
⏱️ RESP-TIMING 🟢 ReadyToSpawn ...
⏱️ RESP-TIMING ➡️ final wire-6 ... (~200 ms after ReadyToSpawn)
```

Then either:

```text
⏱️ RESP-TIMING ✅ SERVER ALIVE evidence: ...
```

or:

```text
⏱️ RESP-TIMING ❌ tidak ada server-alive evidence ...
```

## Optional

Override the initial delay for a later A/B test:

```cmd
set MC_RESPAWN_INITIAL_DELAY_MS=350
start-v1.7.19-reference-timing-lab.cmd
```

Default is 200 ms.
