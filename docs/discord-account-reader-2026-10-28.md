# Discord account reader — October 28, 2026

The account reader is separate from the existing rich-presence writer. It is
compiled into `DiscordService.obj` through `service.cpp`, with its public API in
`dependencies/DiscordRPC/account_reader.h`. Constructing the object performs no
I/O. `Start` requires an explicit enabled option, validated account/profile/channel
binding, supported connection, credential profile, and any required warning
acknowledgement before it can resolve credentials or start a worker.

## Connections and credential purpose

- `UserTokenExperimental`: reads public account fields, the selected channel's
  recent messages, and available account Gateway events. Personal-account
  automation violates Discord's rules and may result in account termination.
  The host must show the prominent warning before the identity probe and again
  bind acknowledgement to the verified account, owned profile, connection and
  relevant configuration/credential revision. A generic global checkbox is not
  sufficient for the host's authorization boundary.
- `Bot`: uses a separately protected bot credential for explicit REST identity,
  selected-channel history and optional reviewed message sending. The reader
  does not label bot observations as personal unreads, personal mentions, friends
  or personal device presence. This adapter does not start a bot Gateway.
- `OAuth`: uses a separately protected access token for explicit identity only.
  It does not assume that an ordinary OAuth grant authorizes personal message
  history, friend lists or message sending. OAuth sign-in and refresh-token
  management are outside this transport.
- IPC remains the unchanged default presence connection. Reader options naming
  RPC, SocialSDK or IPC fail explicitly rather than substituting another backend.

Personal credentials use the existing `[general] token_protected` profile field;
the existing legacy plaintext token fallback is consulted only if the protected
field is absent. Bot and OAuth use `[reader] bot_token_protected` and
`[reader] oauth_token_protected`. They never fall back to personal credentials or
to each other. DPAPI current-user decoding is the default; tests inject a decoder.
The host may supply a session-only protected profile snapshot instead of a file.
Credentials are not part of the portable model, snapshot or public probe result.

The embedded presence writer's existing Gateway route now additionally requires
`Options.experimentalGatewayAcknowledged`, default false, before credential
resolution. Its Auto route can continue using IPC; an IPC failure cannot silently
activate personal-token Gateway transport without that host acknowledgement.
Injected transports and ordinary IPC behavior remain available. The standalone
legacy DiscordRPC executable is a separate entry point and was not reworked here.

## Bootstrap, background reading and projections

`ProbeReaderIdentity` is an explicit bounded `GET /users/@me`. Experimental mode
requires a matching profile-scoped warning before resolving the credential. It
returns only validated user ID, display name, username and avatar hash. It cannot
open a Gateway, write presence or send/acknowledge a message. The host uses the
verified account to construct the full `Binding` and account-bound warning.

`AccountReader.Start` verifies that the credential's returned account matches its
binding before opening a Gateway. It fetches selected-channel history through a
fixed HTTPS Discord API origin, disables redirects, bounds response sizes and
operation duration, and sanitizes all transport/status errors. Refreshes repeat
identity verification and bounded history recovery. Authentication or permission
failure stops automatic retry until an explicit reload; transient failures retry
with bounded exponential backoff and respect the server's 429 retry interval.
Server intervals are not shortened to the configured reconnect maximum.

The experimental Gateway uses one receive pump for READY, RESUMED, messages,
edits, deletes, selected read state, relationships, public user changes, and
visible presence/platform changes. It retains heartbeat and resume handling.
Reader IDENTIFY omits the entire presence field; reader mode rejects SET_ACTIVITY
and does not clear presence on stop. It uses existing configured Gateway
properties and does not introduce a new fingerprint or anti-abuse mechanism.

JSON projection rejects duplicate object keys and malformed or oversized input.
Snowflake IDs are exact decimal integers; timestamps retain fractional precision
and offsets. Partial edits do not erase omitted fields. Initial history establishes
a high-water baseline without treating old messages as newly received. Recovery
and sequence gaps keep local counts explicitly incomplete. History reconciliation
removes missing cached messages only inside a returned page's covered range and
only when no live events arrived while the request was in flight.

Personal read-state counts apply only to the selected channel and only when
provided by that backend. A missing/null cursor or missing total remains unknown.
Local observed counts are distinct from Discord's authoritative unread totals.
Friend lists are bounded and incomplete batches stay incomplete. Presence exposes
only statuses and optional desktop/mobile/web fields actually supplied by Discord;
it does not discover security sessions, device names, IP addresses or invisible
friends. `SESSIONS_REPLACE` is not interpreted as a device inventory.

Background reading never marks a channel read, resets an unread cursor, sends a
message, or invokes presence writing. `Reload` immediately clears prior private
model data and rejects late callbacks from an earlier generation. `Stop` cancels
HTTP/Gateway work and reports whether cleanup met its deadline. A noncooperative
injected transport cannot make cleanup appear complete.

## Explicit sending

`SendReviewed` is the sole send entry point. Both the model's
`DiscordReader.AllowReviewedSend` and the native option's `allowReviewedSend`
must be enabled. The host must construct a compose intent, let the user review
it, and submit the reviewed intent with an unchanged account/channel/configuration
binding and unique activation/nonce. The default click action remains Compose.

The request suppresses user, role and everyone mentions by default, uses the
reviewed text and nonce, and verifies the returned author and channel. The reader
does not automatically retry any send. Duplicate activations are consumed even
after rate limiting or uncertain delivery. A 429 also blocks a newly reviewed
activation until the server interval elapses. Ambiguous network/5xx results report
unknown delivery and require the user to check the channel before composing again.

## Configuration surfaces

The canonical typed schema is `DiscordReader.*` from
`dependencies/discord_tile_model.h`; the host owns CLI, INI, tray and Properties
bindings. Native duplicate timing fields are compatibility inputs; validated
typed configuration is the source of truth. Native safety booleans may further
disable Gateway or sending, but cannot enable a disabled typed setting.

| Setting | Default | Range / behavior |
| --- | --- | --- |
| `DiscordReader.RefreshMs` | 30000 | 1000–86400000 |
| `DiscordReader.OperationTimeoutMs` | 5000 | 100–30000; HTTP and native Gateway connect scope |
| `DiscordReader.ReconnectMinimumMs` | 1000 | 1000–3600000 |
| `DiscordReader.ReconnectMaximumMs` | 60000 | minimum through 3600000; server 429 may wait longer |
| `DiscordReader.MaximumBytes` | 262144 | 1024–1048576 |
| `DiscordReader.UseGateway` | true | reader still remains disabled unless demanded and explicitly enabled |
| `DiscordReader.AllowReviewedSend` | false | separate review/context/nonce checks still apply |

The model schema also includes privacy, display, stale retention, message/friend
limits, warning binding, click/compose policy and outbound mention controls. See
`docs/discord-tile-model-2026-10-28.md` for that inert model contract.

## Validation and limits

`tools/BuildDiscordAccountReaderTests.cmd` builds a separately named fixture.
`tools/DiscordAccountReaderTests.cpp` supplies synthetic HTTP and Gateway
transports; it does not contact Discord or use a real token/account/channel.
Tests cover the actual lifecycle and reader-only IDENTIFY serializer, account
and credential-purpose gating, sequence/history projection, partial edits,
generation changes, cancellation, late authentication closure, REST deletion
reconciliation, concurrent REST/live updates, explicit reviewed sends, duplicate
activation rejection, retry timing and sanitized errors. The embedded legacy
Gateway guard is tested before credential resolution using an invalid protected
fixture value. The final reader fixture passed 135 checks. The existing
DiscordService fixture passed 149 checks, 40000 snapshot reads and 80000 relay
reads. Both fixture binaries were scanned with Microsoft Defender before their
bounded local runs; neither scan reported a threat. Build/run logs are retained
under the ignored `build/review-validation` directory.

Compilation and inert fixtures establish local implementation behavior only.
They do not prove Discord will accept an experimental personal-account session,
that a particular account has access to a selected channel, or that undocumented
personal Gateway fields will remain available. Host warning/credential/compose
UI acceptance and any explicitly authorized account test remain separate work.
