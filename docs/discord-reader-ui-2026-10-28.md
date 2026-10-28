# Discord account tile host integration

The independent Discord tile retains its existing `Presence` view and IPC
presence writer. New `Channel` and `Profile` views use the separate account
reader. A view change does not enable account access or presence writing.

In a Discord tile's Source settings, choose the account-reader connection,
then **Set protected reader credential and review account**. Personal account
tokens show the prominent Discord policy warning before asking for a token or
making an identity request. The native masked credential dialog has a
**Remember token protected in this profile** checkbox, unchecked by default.
An explicit identity-only probe precedes a second confirmation containing the
verified account name, username, account ID, connection and tile.

Unchecked credentials are DPAPI protected in memory, bound to the normalized
profile path, tile ID, account ID, connection and fresh credential revision.
Neither plaintext nor the protected credential is written to the INI. They
are discarded when that reader/profile stops, on disconnect, removal, or an
account/connection change. The next process requires local credential entry
again. Checked credentials use the protected-only purpose-separated inline
settings. An explicit advanced Discord profile path remains available.

The common `DiscordReader.*` schema drives typed CLI, INI, tray menus and the
Properties window. `Enabled=0` and `RememberCredential=0` are fresh defaults.
`CredentialProtected`, `CredentialConnection`, `CredentialRevision` and
`AcknowledgedCredentialRevision` are protected host metadata; there is no
plaintext token CLI setting. Changing the account or credential revision
invalidates the previous personal-account acknowledgement. Available reader
connections are basic OAuth identity, bot channel access and the explicitly
experimental personal-token reader. RPC and Social SDK are not advertised as
implemented readers.

Channel projection uses the newest available selected-channel message, with
separately configurable content, author and local timestamp. Native personal
unread counts are used only when actually supplied; otherwise counts explicitly
say **New since tile baseline**. Unknown counters remain absent. Profile
projection uses supplied display name, username, visible presence/platforms,
mentions and friends. An unknown platform is not an invented device/session.
Only an already prepared, bounded avatar lease reaches XML/GDI rendering; CDN
requests have no Authorization header and do not follow redirects.

The default account click action is a native compose box followed by explicit
message review. `AllowReviewedSend` is separately off by default. Sender,
channel, profile, connection generation, credential/config revision and message
content are frozen and revalidated before the transport send. Direct configured
send additionally requires its separate opt-in and configured click action.
Background refresh, reconnect, layout, tile rotation, preview and startup do
not send messages or read acknowledgements. Send outcomes that cannot be
confirmed are not automatically retried.

The portable fixture is `tools/DiscordReaderLayoutTests.cpp`; the actual host
fixture is `DesktopStub/tools/DiscordReaderHostTests.cpp` using the real host
configuration, runtime and hidden native dialogs with synthetic transports.
`tools/BuildDiscordReaderUiTests.cmd` compiles the portable fixture;
`CompileHost` compiles the inert actual host; `Host` rebuilds its Discord service
object, editor and inert Asus objects before linking the host fixture. The
standalone build does not require another fixture to have run first.
Rebuild all dependent objects after any shared configuration layout
change; mixing old service objects with new headers is not a valid test. The scripts
compile only. Scan the resulting EXE before executing it. All account work in
the host fixture is injected and no native account/network/send is performed.

Inert injection points are `g_testDiscordIdentityProbe`,
`g_testDiscordProtect`, `g_testDiscordCredentialDialog`,
`g_testDiscordComposeDialog`, and `ContentDiscordAccountProvider`'s
`configureTestOptions` / `avatarTransport`. Without explicit injections,
account-reader operations are inert in validation builds.

Fresh scanned validation on October 28 initially passed **62 portable layout
checks**, **353 model checks**, and **41 actual host checks**. The host fixture covers
warning cancellation before credential entry/probe, session-only persistence,
account/revision binding, injected runtime GET, native GDI rendering, real
hidden dialog geometry, compose/review cancellation, destination change during
composition and a single explicit reviewed POST through the fake transport.
The actual inert host compiled as well. No actual account/message operation was
performed by these fixtures.

Independent review then corrected Custom profile/channel rendering so its right
avatar and resource rows cannot fall through to generic text, and corrected
empty display-name fallback to username for profile/friend/account-review text.
The portable layout fixture now passes 64 checks. The host regression compares
every production-rendered pixel with dedicated rendering, plus resource-row ink
and a known right-avatar sample. Its original 1x1 BMP input sampled transparent
edges when stretched (A=67 at the center); a proper 8x8 solid fixture replaces
that degenerate input. Exact pixel equality and the original opacity/color
requirements are unchanged. The final scanned rebuild passes all **45 host
checks**, with 64 layout, 353 model, 135 transport and 2,287 maintainer source
checks also passing. Fresh broad/Revision hosts pass 24,420 / 1,282 checks.
The final host receipt is
`build/review-validation/discord-host-solid-avatar-tests.log`.

`WarnEveryConnection` currently applies to a new host reader start/Stop session;
automatic transport reconnects within that reviewed generation do not show a
new modal warning. `OpenProfile` opens Discord's current `@me` route. The old
global/numbered rich-presence source retains IPC but does not have a separate
global personal-token review wizard; its experimental Gateway fallback remains
blocked. Independent typed Presence tiles can review an advanced profile.

Actual Discord account/server acceptance, Start pinning, real multi-monitor DPI
transitions, and live session expiry/reconnect are separate acceptance tasks.
Hidden dialog geometry tests use actual native controls at 100/150/200 percent
equivalent font scales and do not establish real multi-monitor DPI behavior.

The later explicitly authorized [native HTTP acceptance](discord-reader-native-acceptance-2026-10-28.md)
passed verified alternative-account identity, selected-channel history and
avatar decoding using session-only credentials. It sent no message/read ACK
and did not enable Gateway or Start/package/Startup integration. Those remaining
native boundaries are not closed by the HTTP result.
