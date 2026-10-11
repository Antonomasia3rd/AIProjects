# Discord advanced profile editor, 2026-10-11

The shared API compiled and its fresh binary passed a Microsoft Defender custom
scan followed by 38 synthetic checks with a 60-second process deadline. No host
UI/CLI wiring, donor retirement or release/map changes are included in this slice.

`dependencies/DiscordRPC/profile_edit.h/.cpp` exposes profile documents,
explicit typed descriptors, field normalization, read-only grammar validation,
complete-batch text staging and atomic existing-file edits. `service.cpp` exports
the functional catalog from its existing defaults with explicit bool/int/enum/
activity/client-ID/text/secret types, so activity type 0 is not guessed to be a
boolean. Real censor rule-order/replacement/regex parsers are reused without
running presence context collection, credential resolution or transport.

The editor preserves untouched comments, unrelated host sections and opaque
plaintext/protected credential values. Editing credentials requires an explicit
private/masked input policy; it never encrypts, decrypts or migrates them. Text
edits correctly encode literal backslashes and newlines for both the Discord
escape layer and the native quoted INI layer. No defaults or missing profile are
created. Optimistic expected-text checks and fresh mutation locks support editor
conflict detection and complete concurrent batches.

`general.update_interval` is marked host cadence. A caller must acknowledge its
mapping and consume `requiresHostCadenceApply/requestedRefreshSeconds`, because
the embedded service actually schedules via `Options.refreshMs`. The helper
cannot apply DesktopStub settings itself. Host integration must plan/couple that
preference rather than advertise an ignored advanced option.

Host wiring required later: resolve configured `Discord.ProfilePath` relative
to the host INI, read a profile document, display descriptors with secret masking,
pass decoded private/editor text into `CommitProfileEdits`, handle optimistic
conflicts, apply cadence mapping and request resident source reload. Link
`profile_edit.cpp` alongside a rebuilt `service.cpp`. No standalone CLI/app/tray
entry or token-protection helper should be invoked.

The fixture covers activity types 0/1/2/3/5, unsupported activity rejection,
private credential edit policy, complete batch staging, literal backslashes and
newlines, exact untouched plaintext/protected blobs/comments, real regex/rule
grammar, cadence acknowledgement/return metadata, relative references and key
case normalization, invalid-file zero writes, stale editor rejection, four
concurrent complete writers (including independent keys that detect lost updates), and two independent preview services with empty
client IDs and injected contexts. Preview constructed zero transports.

The editor module and fixture compile with warnings as errors. The separately
compiled shared service retains its existing unused standalone-function warnings.
No real account/token file, process context, credential encryption/decryption,
transport, browser, Startup or package operation was invoked. Existing token
data is never migrated as part of an unrelated edit. Cipher representations are
opaque; an actual Gateway send still uses its existing credential resolver.

Evidence: `tools/BuildDiscordProfileEditTests.cmd` builds only; execution must
follow a fresh scan. Ignored review logs are
`build/review-validation/discord-profile-edit-build-oct11.log`,
`discord-profile-edit-oct11.log`, matching Defender log and scan SHA-256 record.
The fixture is `tools/DiscordProfileEditTests.cpp`.

Limits: only the referenced profile is committed here. A host cadence update
needs coordinated locking/rollback with the host INI. Metadata reports that
mapping requirement and refuses an unacknowledged cadence edit. Native INI
effective-value/quoting behavior is reused; untouched legacy/unknown rows remain
unchanged, and only edited known fields are validated. There is no remote/API
validation or fresh real-account acceptance. Normal read/edit calls resolve
explicit paths and require an existing file. The separate, explicit
`CreateProfileDocument` setup action now generates the real functional defaults
through the same descriptor catalog, excludes standalone host/Startup controls,
reports default cadence 5 and atomically publishes without replacing an existing
file. Four additional creation/nonreplacement/no-implicit-creation checks passed.
No action launches a donor or registers a profile/package.
