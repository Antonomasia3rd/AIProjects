# Extended Discord tiles: transport and capability audit

This is a read-only source/API audit for the user's October 28 request. It does
not implement the new tiles or establish a successful Discord account test.
No browser credential, token, message, account setting, server or channel was
read or changed. Current layout/CI work remains first; Discord is next and the
Luna Windhawk reviews remain last.

## Existing implementation

- `dependencies/DiscordRPC/service.h` exposes a presence-oriented Transport
  with Send(activityJson, status) and Clear. Its Snapshot contains name,
  details, state, activity JSON and lifecycle/error state. It has no channel,
  message, account profile, relationship, mention or device snapshot.
- `drpc_ipc.inc` opens a Discord named pipe, handshakes with client_id and
  sends SET_ACTIVITY/ClearActivity. It does not implement OAuth AUTHORIZE,
  AUTHENTICATE, generic subscriptions or message/profile event projection.
  ReadResponseForNonce explicitly discards unrelated frames while awaiting a
  reply; extending this path with subscriptions would lose events unless those
  frames are dispatched into a separate bounded event model. It also has no
  idle event pump when the presence writer is not making a request.
- `service.cpp` supports ipc, gateway and auto. Auto can fall back from failed
  IPC to Gateway when actual presence sending is enabled. Preview/injected
  transports do not resolve credentials. Reading messages must not implicitly
  enable this presence-send path or change an account's presence.
- `drpc_gateway.inc` already contains a personal-client-shaped IDENTIFY:
  browser/build properties, capabilities and client_state, with a configured
  token. Its dispatch handler recognizes READY/RESUMED and otherwise mainly
  logs event names. It does not retain message, relationship, read-state or
  platform-presence events as a usable tile model. Supplying a bot token does
  not turn that payload into a documented bot Gateway implementation.
- `content_sources/discord.h` renders the local presence service snapshot.
  Its Sending/Preview/Error wording is not an account inbox or profile reader.
- Configured token selection prefers the protected value; decode failure does
  not silently choose plaintext. Legacy plaintext is usable when the protected
  value is absent. Existing masked/private-edit and DPAPI helpers should be
  reused, with distinct credential kinds rather than a generic interchangeable
  token field. The profile's gateway.supported flag denotes an implementation
  switch, not Discord approval or proven live compatibility.

## Documented capabilities and limits

Discarding IPC is not a prerequisite. Discord's current
[RPC documentation](https://github.com/discord/discord-api-docs/blob/main/developers/topics/rpc.mdx)
describes authenticated channel queries and message/current-user/relationship/
notification events over IPC. GET_CHANNEL can include text messages. The
documented command list does not include a GET_RELATIONSHIPS snapshot command;
an event alone is not a complete initial friends list. There is no documented
arbitrary SEND_MESSAGE RPC command in that list. Deprecated localhost WebSocket
RPC is not a reason to abandon the supported IPC transport.

The [OAuth scope table](https://github.com/discord/discord-api-docs/blob/main/developers/topics/oauth2.mdx)
distinguishes identify, local-RPC messages.read and approval-dependent rpc/
rpc.notifications.read access. relationships.read is part of Social SDK access.
An ordinary identify grant does not authorize arbitrary channels or personal
unread state. The RPC relationship-event text uses relationships_read while
the OAuth table uses relationships.read; validate the approved application's
actual granted scopes rather than relying on that spelling discrepancy.

| Requested data/action | Supported candidate | Boundary to represent accurately |
| --- | --- | --- |
| Display name, username, avatar | OAuth identify/current user; authorized RPC current-user updates; SDK user handles | Bind to verified account ID. Distinguish global display name from an optional server nickname. |
| Latest configured-channel message and timestamp | Approved local RPC messages.read; bot channel history/events; approved SDK linked channel | Each connection has different access and sender semantics. A bot does not inherit the human's channel membership. |
| Personal Discord unread/mention totals | No complete total established by this audit | Notification events are eligible notifications, not an authoritative inbox count. A local cursor needs an honest local-count label. |
| Online friends | Authorized Social SDK relationships/user presence | A bot's online guild members are not the user's friends. Filter actual friend relationships; do not include blocked/pending users. |
| Platform activity | Visible Gateway client_status where supplied | Platform presence is not a list of logged-in devices, machines, sessions or IPs. Missing data must not become a fabricated device count. |
| Send configured text | Bot Create Message or authorized SDK communication; otherwise open a native composer | Label the sender and destination. Basic Rich Presence IPC does not provide arbitrary channel sending. |

The Social SDK [scope guide](https://github.com/discord/discord-api-docs/blob/main/developers/discord-social-sdk/core-concepts/oauth2-scopes.mdx)
separates presence/friends access from limited communications access. Its
[friends guide](https://github.com/discord/discord-api-docs/blob/main/developers/discord-social-sdk/development-guides/creating-a-unified-friends-list.mdx)
documents online grouping and callbacks, but other applications' rich activities
are not exposed as though the SDK were the full Discord client. The SDK is
oriented to games; DesktopStub eligibility and application access must be
established before advertising those features as connectable.

[Linked channels](https://github.com/discord/discord-api-docs/blob/main/developers/discord-social-sdk/development-guides/linked-channels.mdx)
are a lobby bridge, not arbitrary personal-client channel access. They require
communications access, a suitable server text channel, Discord-side permissions
and lobby linking permission. All lobby participants can see bridged messages,
including from a linked private channel. Do not silently create/link a lobby
as a workaround for a private personal tile.

For a bot reader, [Message Resource](https://github.com/discord/discord-api-docs/blob/main/developers/resources/message.mdx)
documents channel permissions, bounded history pagination and Create Message.
Message Content entitlement/intents can limit fields even when a message ID
arrives; empty protected content is not evidence that the channel is empty.
[Presence events](https://github.com/discord/discord-api-docs/blob/main/developers/events/gateway-events.mdx)
are guild-scoped and require the relevant intent. client_status supplies optional
desktop/mobile/web status; offline/invisible fields may be absent. Do not query
account security sessions to approximate this display feature.

Discord explicitly prohibits personal-account automation outside its OAuth/bot
interfaces and states it can terminate accounts:
[Self-Bots policy](https://support.discord.com/hc/en-us/articles/115002192352-Automated-User-Accounts-Self-Bots).
This is a concrete platform limitation, not a claim that the existing Gateway
backend was tested or approved. Browser/CDP access supplies neither application
approval nor an OAuth scope. No live personal-token experiment was performed.

## Proposed architecture

Keep rich-presence writing separate from account/channel reading and sending.
Retain the existing presence service and IPC option. Add independent typed
views such as DiscordChannel and DiscordProfile over a shared, account-bound
reader, with explicit connection capabilities:

1. Basic OAuth identity for a useful profile baseline.
2. Approved RPC or Social SDK features where the application is eligible and
   granted the required scopes.
3. A dedicated documented bot connection for configured server channels. It
   needs its own IDENTIFY/intents and must label bot-authored sends.
4. Keep the pre-existing personal-token Gateway path clearly experimental and
   separate; never let an IPC read/auth error trigger silent personal-account
   login. Its current presence-only handler is not the new reader.

An authenticated RPC adapter needs one receive pump that routes replies by
nonce and projects subscription events, rather than multiple competing pipe
readers. Bound pending replies/event queues, discard stale connection generations
and cancel them on sign-out. Do not embed a confidential OAuth client secret
in a publicly distributed EXE; establish the approved public-client/SDK or
service-side exchange design before wiring consent.

Every snapshot should carry account ID, connection generation, capability
availability, received/completed time and stale/error state. Cache identity
must include account/connection/channel so switching the alt or main account
cannot reuse another account's messages, avatar, cursors or send target.
Use the shared SourceHealth policy, bounded caches and demand-driven observers.
No dormant tile should open a Gateway or poll channels.

The profile layout can put a bounded avatar on the right and available textual
fields on the left. Hide unsupported optional fields by default; show the
reason and required capability in settings, not repeated setup clutter on the
tile. Presence/invisible/unknown states must remain distinct. Cache/decode
avatars with bounded byte/dimension limits, first-frame handling for animation,
and no Authorization header forwarded to CDN requests.

## Unread and send semantics

Store a local last-seen message cursor only when using a local-count mode.
Label it as new since the tile's baseline/reset, not Discord's full unread
count. Do not initialize by counting all historical messages. Track edits and
deletions, reconnect gaps, out-of-order/duplicate events and history truncation.
Mention counts must identify their scope and baseline; include role/everyone
mentions only by explicit policy. Unknown counters stay unknown.

Background fetch, render and tile rotation must not select a channel, send a
read acknowledgement or reset Discord's inbox. Explicit Open Channel can use
the normal client and its normal read semantics; Reset Local Counter is a
different action. A native composer is the low-side-effect first send UX.

Direct send, if enabled, needs explicit account, channel, template and click
policy. Freeze that tuple and its configuration revision before a compose or
confirmation dialog; revalidate before sending. Display sender/destination,
prevent default mass mentions, enforce content limits and rate-limit backoff,
and deduplicate repeated activation. Treat message text as inert data, never
commands or recursively expanded templates. Do not blindly retry an ambiguous
POST outcome; use documented nonce/idempotency facilities where available.
There must be no automatic message on startup, refresh, reconnect or test-mode
entry. The proposed e demonstration is one explicitly targeted interaction,
not bulk posting or an engagement loop.

All new connection, privacy, refresh, layout, stale/error, counter and click
options require the same validated CLI/INI/tray/Properties surfaces. Keep
credential changes masked/local and purpose-bound. Never print credentials,
capture them in artifact logs, persist plaintext browser storage, or make
browser-token extraction an installation requirement.

## Implementation/acceptance order

First implement portable models, capability errors and layouts with synthetic
profile/messages/friends data. Test account switching, nullable fields,
permission denial, content redaction, local counter semantics, edits/deletes,
reconnect/rate limits, cache invalidation and sender/account/channel revision
checks. Verify no render path acknowledges or sends a message. Follow with
injected transport lifecycle tests and native XML/GDI layout checks.

Before a real acceptance test, identify the intended connection and the exact
account/channel from current UI. The existence of an alt session does not prove
bot installation, SDK access or current RPC consent. Actual authentication,
read scope, sender identity, message delivery, account longevity and native
tile display remain separate evidence. No such acceptance is claimed here.
