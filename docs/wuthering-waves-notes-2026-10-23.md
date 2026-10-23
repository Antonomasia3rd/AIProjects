# Wuthering Waves Real-Time Notes

DesktopStub's WuWa provider reads the **overseas Wuthering Waves launcher** role data. It uses an explicitly supplied **game access token**, obtains a launcher OAuth code, optionally discovers roles, and queries the selected role. It does not read another application's files, browser cookies, game memory, or passwords. It does not log the token, OAuth code, raw request, response, or remote error message.

This is an unofficial integration with Kuro's service. The protocol was checked against [Wuthery/kuro.py](https://github.com/Wuthery/kuro.py) commit `4be620b2fbdc05eb2e2b6bb3aba68ae348d27c22` on 2026-10-23. No upstream Python implementation is bundled. Kuro can change or withdraw these endpoints, expire a token, reject an SDK session, or require additional authentication. No actual Kuro account was contacted during validation.

## Account setup

Create a Wuthering Waves tile and enter the **overseas game access token** in that tile's account settings. The token is the `access_token` returned by the SDK's `get_game_token` flow in the referenced project; it is **not** a HoYoLAB cookie, a KuroBBS web token, an OAuth code, or a gacha record URL. The upstream project's authenticated developer workflow is documented in its [auth client](https://github.com/Wuthery/kuro.py/blob/4be620b2fbdc05eb2e2b6bb3aba68ae348d27c22/kuro/client/components/auth/subclients/game.py) and [game tests](https://github.com/Wuthery/kuro.py/blob/4be620b2fbdc05eb2e2b6bb3aba68ae348d27c22/tests/client/components/test_game.py). Obtaining and supplying that token is an advanced manual setup step. DesktopStub does not automatically sign into Kuro or execute that workflow on the user's behalf.

Leave player ID and region empty to discover the account's roles. If exactly one matching role is found, the provider uses it without changing the configuration. Multiple roles require selecting the intended player ID and exact region returned by discovery; the provider never guesses between them. Partial input filters discovery, so entering a region alone can select one role in that region. The region is a launcher dictionary key such as `Europe`, **not** the hashed server IDs used by the gacha API.

Optional DPAPI protection uses the current Windows user's versioned UTF-8 credential envelope. A protected-token decode failure stops the refresh; it never falls back to another plaintext credential or user account. Keep tokens private; a token provides access to the account information available to the SDK.

Supported provider settings are `GameToken`, `GameTokenProtected`, `PlayerId`, `Region`, `DeviceId`, `RefreshSeconds` (30–86400, default 300), `TimeoutSeconds` (1–120, default 15), and `MaximumBytes` (1024–16777216, default 1048576). `UID` and `Server` are validation aliases. The host's per-tile model supplies its own section/key prefix; see the tile's settings or exported INI for the exact persisted path. A blank device ID creates one UUID for the complete refresh flow. An explicit device ID is reused across refreshes.

## Data displayed

The provider reads the upstream `Base` model's waveplates and maximum, recovery timestamp, refined waveplates and maximum, daily activity/unlock state, and weekly challenge attempts remaining. It displays only fields returned by the service. Missing recovery data is shown as unavailable, and missing optional counters are omitted. It does not invent a weekly maximum or infer a reset schedule. The returned player ID must match the selected ID before data is published.

The implementation follows [the current launcher query flow](https://github.com/Wuthery/kuro.py/blob/4be620b2fbdc05eb2e2b6bb3aba68ae348d27c22/kuro/client/components/game.py) and [role field definitions](https://github.com/Wuthery/kuro.py/blob/4be620b2fbdc05eb2e2b6bb3aba68ae348d27c22/kuro/models/game.py). Although the reference project declares a PGR game enum, its inspected role/energy endpoints and account setup are WuWa-specific. **Punishing: Gray Raven, Chinese-region WuWa, and other Kuro games are not advertised as supported.**

## Runtime and validation

Each demanded tile owns an asynchronous provider. An unconfigured or unpinned tile starts no Kuro network activity. A configuration change cancels the old request, and results from another account/role revision cannot replace the current result. Refreshes use fixed HTTPS hosts, POST bodies, a bounded response size, one deadline shared by OAuth/discovery/query, and cancellation. Automatic redirects are disabled so credentials cannot be forwarded to a redirected host. Launcher status `1005` has at most three attempts within the same deadline.

The portable model and native adapter tests inject synthetic responses and cover role mismatch, discovery ambiguity, malformed/duplicate JSON, unsafe account fields, Unix/ISO recovery timestamps, redacted failures, optional credential decoding, provider caching/cancellation, and empty-credential inactivity. Shared HTTP tests use isolated loopback listeners to check POST body integrity, disabled redirects, finite waits, and final callback/body release. They never connect to Kuro's service. Successful mocked protocol tests do not establish that a real account token or server will be accepted.
