# Discord native read-only acceptance — October 28

The user explicitly approved using their already logged-in alternative account
for the prepared native test. The actual host's prominent policy warning,
masked credential dialog and verified-account review were exercised. The
reviewed account matched the identity observed in the earlier browser preflight.
This is account acceptance of one bounded HTTP read, not a promise that Discord
permits personal-token automation or that the account cannot be terminated.

A separately named, scanned private driver included the actual DesktopStub
host without calling its application entry point. It used the real identity
probe, current-user DPAPI and account-reader HTTP transport. It never entered
resident startup, package/Startup reconciliation, hardware, pinning or the tile
engine. Remembering credentials, Gateway, reviewed/direct sending and the
composer were disabled. Only aggregate evidence was retained; no private tile
previews were requested. Its source, binary and per-run files remain ignored.

The first attempt expired at the 180-second outer UI limit while the credential
dialog was being recovered; it produced no account result. The retry allowed
300 seconds for the manual dialog stages, with the same 90-second maximum read
phase. It completed normally with exit zero:

- One explicit identity probe and matching verified account.
- Session-only configuration; credential absent from the INI.
- Reader success, connection ready and complete selected-channel history.
- One selected-channel message observed; no message content or author in logs.
- Avatar advertised and successfully downloaded/decoded through the auth-free
  bounded CDN path.
- Sending disabled; no message or read acknowledgement sent.
- Reader stopped and protected session map cleared.

The automation session holding the preflight credential was reset afterwards.
No raw credential was placed in source, command-line arguments, logs or files.

The private receipt is under
`artifacts/discord-reader-acceptance-20261028/run-54864-343089375/summary.json`;
the sanitized process receipt is
`build/review-validation/discord-native-acceptance-retry-run.log`.

Not established by this test: live Gateway fields/reconnect/session expiry,
friends/platform presence availability, actual Start rendering/pinning,
profile/channel bitmap appearance, outbound message delivery, other accounts,
or other Windows versions. Inert fixture render/send checks remain separate.
