# Offline fixes, 2026-10-15

Verified cache, capture and shutdown bugs were fixed without starting Minecraft, the bot, the relay or
Microsoft/Xbox authentication.

`World.loadLevelChunk` previously installed a new empty column before validating
the packet or decoding it. A negative sentinel or decoder exception therefore
replaced usable terrain with an empty/partial column. It now commits the new
column only after the installed synchronous decoder succeeds. Negative sentinels
retain the existing unsupported-return behavior and preserve the old column.
Successful decode counts change only on successful publication. The constructor
accepts optional injected registry/chunk/Vec3 dependencies so this behavior can
be tested without loading the live bot's dependencies; normal callers retain
the installed defaults.

`vanilla-respawn-reference-relay.js` previously wrote every selected diagnostic
record without observing asynchronous stream errors or backpressure. This could
terminate the relay on an unhandled error or keep growing the write queue when
disk output was slow. Its new `relay-capture.js` helper observes errors before
the first write, drops/counts diagnostics while the stream is blocked, and resumes
on `drain`. A false return from the underlying `write` still means that particular
record was accepted; it is never replayed. These behaviors follow Node's
[writable-stream contract](https://nodejs.org/docs/latest-v24.x/api/stream.html#event-drain).

Capture has a 64 KiB per-record cap and defaults to a 16 MiB total per connection.
Set `MC_REF_MAX_CAPTURE_BYTES` to an integer from 65536 through 1073741824 to
change the total limit. Hitting the limit or an I/O error ends diagnostics;
normal relay forwarding continues. There is no userland packet queue and no
pausing or modification of gameplay packets. Closing can add one bounded summary
after a backpressured record. Capture reports successful file release only after
the stream's `close` event, rather than assuming `finish` already closed its file
descriptor. Submitted/dropped record counts describe diagnostic loss explicitly.

Diagnostic login and handshake payloads are omitted. Credential/authentication
fields, common inline JWT/bearer/query tokens and raw buffer contents are
redacted; gameplay `player_auth_input` flags/ticks remain available. Redaction
copies diagnostic objects and does not modify objects forwarded by the protocol
library. This changes no authentication or trust policy.

Reference-relay SIGINT/SIGTERM shutdown now observes both the relay's close
promise and every active capture's actual stream close under one five-second
deadline. Repeated signals share the same shutdown operation. New captures are
not created after shutdown starts. Clean completion exits with status 0;
rejection or timeout exits with status 1 and explicitly says cleanup/flush was
not confirmed. An I/O error requesting destruction is not itself treated as a
closed descriptor. The injectable `relay-shutdown.js` coordinator contains no
relay, process or filesystem integration; the entry point supplies those actions.

The same bounded/redacted capture helper is now reused by
`respawn-no-finalizer-lab.js`, `respawn-ready-replay-lab.js`,
`respawn-reference-timing-lab.js`, `respawn-finalizer-retry-lab.js`,
`respawn-state2-field-lab.js`, and `respawn-lifecycle-diag-preload.js`. Diagnostic
directory, stream creation and asynchronous write errors disable logging while
the existing client/lab behavior continues. Their packet suppression,
transformation, replays and injection experiments were not changed or executed.
Only the reference relay gets the new process-signal shutdown coordinator;
the preload/lab loggers close through their existing client lifecycle hooks.

`diag/` and `.relay-auth/` are now ignored by Git. Ignoring a directory does not
encrypt it or remove older copies. Existing captures from before this change may
contain raw login data. Authentication cache files remain sensitive, and even new
redacted captures can contain player names, coordinates, messages and server
details. Review captures before sharing them. The existing
`MC_REF_PROFILES_DIR`/`MC_REF_OUTPUT_DIR` choices can keep local cache and traces
outside a checkout that will be shared. No protected-path requirement or
mandatory credential encryption was added.

## Validation

Run only the isolated fixture with `npm run test:offline-audit`, or directly:

```text
node --max-old-space-size=128 --test --test-timeout=5000 offline-audit-test.js
```

The reviewed entry imports only Node assertions/events and three local modules.
It supplies all World dependencies and an in-memory fake stream. It opens no
game/auth/network connection or data file and imports neither the relay nor bot.
**42 checks passed** in approximately 237 ms under a 5-second test deadline.
They cover failed/new/replacement chunks and sentinels; asynchronous and thrown
capture errors; 10,000 records under backpressure; drain and final-close ordering;
total/per-record caps; login/auth redaction; unchanged forwarded data; and bounded
recursive diagnostic values. The shutdown cases cover delayed physical close,
repeated SIGINT/SIGTERM subscriptions, relay/capture deadlines, late rejection after timeout, cleanup
after thrown relay errors, and contained fake directory/file setup failures.
Syntax checks passed for all 11 changed JavaScript files, including the six
lab/preload adapters and both capture/shutdown helpers.

This is a focused audit. It does not establish Minecraft/protocol compatibility
or test real relay shutdown, authentication, filesystem failures, console
backpressure or the experimental gameplay behavior of the lab scripts.
Actual engine/server and operating-system close behavior remains separate
acceptance work. Existing authentication caches and diagnostic files were not
read, and no files were staged or committed during this subtask.
