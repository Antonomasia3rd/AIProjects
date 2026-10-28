# Portable Discord tile model — October 28

`dependencies/discord_tile_model.h` is an inert foundation for richer Discord
tiles. It does not connect to Discord, read browser storage, resolve credentials,
send messages, set presence or acknowledge a channel. The existing presence
service/IPC writer is not changed by this model.

The canonical host setting namespace is `DiscordReader.*`, avoiding the existing
`Discord.*` presence fields. `Fields`, `NormalizeSetting`, `ReadConfiguration`,
`ConfigurationValues` and `SettingValue` provide one typed schema for the
CLI/INI/tray/Properties adapter. `ConfigurationKey` is an opaque deterministic
cache fingerprint; it is not an authorization token or security revision. The
host must maintain an actual monotonic configuration/connection revision.
`ConfigurationValues` can contain private configured message text and must not
be logged. There are no authentication-token fields in this model.

## Configuration and binding

Views are Presence, Channel and Profile. Connection choices are IPC, RPC,
OAuth, SocialSDK, Bot and UserTokenExperimental; naming a connection does not
establish that an adapter or approved scope is available. Default Presence/IPC
preserves the existing behavior. The new reader should remain dormant when it
is not demanded by an enabled tile.

The schema includes selected account/guild/channel IDs; body privacy and preview
limits; author/time/name/status toggles; System/12/24/ISO time display; right/none
avatar placement and scale; friends filtering/display/cache limits; count policy;
stale retention/age; event/send-history limits; compose/direct-send policy;
outbound mention suppression; account/profile-bound warning acknowledgement;
and the reader's refresh, timeout, reconnect, byte and Gateway/send controls.
All bounded numeric ranges are declared in `Fields`; reconnect maximum must be
at least its minimum. Display author/time/name/status/scale controls belong to
the renderer; they do not rewrite the stored UTC events.

Each `Binding` contains the owned profile key, verified account ID, selected
guild/channel, connection generation and configuration revision. `Reset` checks
the selected identities and clears private state. `Reconnect` accepts only the
same account/channel/configuration and a strictly newer connection generation;
it rejects old callbacks, marks the result incomplete, and retains useful data
only if configured to do so. Explicit sign-out uses `Clear`.

## Event input contract

The adapter supplies a nonzero monotonic receipt sequence for every event it
projects to a reducer. Stale sequences and foreign bindings are ignored. Message
IDs remain exact uint64 decimal snowflakes, including values beyond double
precision. `ParseTimestamp` accepts strict offset-bearing ISO timestamps with
up to nine fractional digits using integer seconds/nanoseconds.

Events carry typed optional patches. Missing fields do not mean empty data.
Explicit empty strings can clear identity fields. Initial history does not
increase the local new-message count. The adapter should freeze the initial
high-water cursor and supply it on `SyncComplete`; this lets concurrent live
messages above that cursor count without treating old pages as new. Recovery
history and an explicit `Gap` keep the count inexact. Complete snapshots are
distinct from partial/truncated ones.

Edits update known content/mention predicates without increasing message count.
Late REST pages cannot overwrite a newer edit. Delete tombstones and an eviction
floor prevent old deleted/evicted IDs from repopulating the latest cache. Bounded
counter history returns only currently retained observed IDs; overflow/unknown
authors/gaps are labeled inexact rather than pretending to be Discord's inbox.
Unknown mention fields cannot become an authoritative zero.

Authoritative personal unread/mention counts require declared personal read-state
capability and the configured channel context. They are separate from local
baseline counts. A read-cursor change invalidates omitted dependent totals. Bot
and basic IPC observations cannot be relabeled as personal counts or friends.
There is no automatic read acknowledgement or cursor reset during `Read`.

Friends use explicit Friend relationships only. Blocked/pending/other users do
not enter the list. Refreshes stage a bounded list before replacing the useful
previous snapshot; truncation or unknown presence prevents an exact online total.
Presence should be seeded after its relationship or included in that snapshot.
`PresencePatch.platformSnapshot=true` replaces a complete client-status object;
otherwise only supplied platform fields merge. Unknown, offline and explicitly
reported invisible are distinct. Invisible is never inferred for another user.
`VisiblePlatformCount` counts reported visible desktop/mobile/web activity, not
machines, authentication sessions or logged-in devices.

Only controlled Discord avatar hashes are stored. `AvatarUrl` builds a bounded
PNG CDN URL from a valid user ID/hash; it does not accept arbitrary URLs or
forward Authorization headers. Actual download/decode limits remain an adapter
responsibility. Unknown avatar data can use the host's ordinary fallback.

## Compose and send contract

Compose is the default. `BeginCompose` creates a local draft; `SetComposeText`
invalidates its review; `ReviewCompose` checks the frozen destination and exact
Unicode text; `AuthorizeSend` returns a request only after the source is ready,
the current account/channel/configuration matches, the message is reviewed,
the connection and `AllowReviewedSend` both permit sending, and the warning
acknowledgement matches when using a personal token. The returned request has
no credential and performs no HTTP operation.

The host displays the sender/destination and must call this path only from an
explicit activation. `SendConfigured` additionally requires DirectSendEnabled
and literal ConfiguredText. It is opt-in and does not introduce background sends.
User/role/everyone mentions are all suppressed by default. Text is literal data,
not a command or recursive template. UTF-16 length/control validation is shared
across platforms.

Use a `SendLedger` per account-bound reader. Repeated activation IDs are rejected
even after the bounded detailed history is evicted. Pending and unknown network
outcomes are not automatic retry authorization. The adapter should generate
unique 1–25-character client nonces and use server nonce enforcement where that
connection supports it. Completion from an obsolete binding is ignored.

Personal-token warning acknowledgement is tied to profile/account/backend;
WarnEveryConnection requires the current connection generation's explicit
acknowledgement. The model contains a prominent factual warning. It does not
claim Discord approves the experimental backend or that an alt cannot be banned.
The transport must apply the same gate before connecting, not merely before send.

## Validation and remaining integration

The portable `/W4 /WX /O2` fixture passed **347 checks** after a fresh Defender
scan. It covers every field's mapping/fingerprint, invalid configuration,
Unicode/ID/timestamp exactness, paged history/live interleaving, replay/edit/delete,
bounded privacy caches, account/channel/generation transitions, personal/local
counter distinctions, staged friends/presence, warning gates and reviewed-send
replay/ambiguity. No account/network/message/read-ack operation was exercised.

This model alone is not a working connected tile. Reader transport, typed host
integration, real XML/GDI layouts, account/session acceptance and any actual
message delivery must be validated separately before being advertised as working.
