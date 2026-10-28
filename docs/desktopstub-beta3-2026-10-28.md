# DesktopStub v27 beta3 — October28

Local beta3 is version27.0.0.3, built from source commit
`3d9747d1622a7be3c4e194ee8dcacba1c53f11f7`. The matching host, broker and
Tile Launcher built successfully and scanned clean before delivery.

Payload: DesktopStub/releases/DesktopStub-v27-beta3/.
ZIP: DesktopStub/releases/DesktopStub-v27-beta3-x64.zip.
SHA256: `6595ACCF8CC3F0DA6A13574BAD9A7F5C8DEA774AEE80700A6599E1F18FE4F136`.
The seven-file payload has no user settings or account data.

This build includes Large Prayer timetable separation, standardized Clock-sized
status headings, footer-safe SMTC text/icon allocations, RSS-style Default game
rows, preserved Custom setup/error styles and localized Clock Auto icon policy.
See [the implementation checkpoint](desktopstub-next-pass-2026-10-28.md).

Local validation:24,419 broad and2,585 focused host checks;2,287 source checks;
238Prayer model,132media layout,56game layout and780artwork-policy checks.
The INI alias fixture additionally passes27mixed-language checks. Offscreen
production-footer witnesses and measured line heights are retained privately.
The unchanged four-pixel legacy raster issue remains open.

The canonical D: DesktopStub/build payload was updated while no matching
resident was running. All payload hashes were checked. No INI existed at the
time; its absent/present hash state was preserved. No package registration,
account operation or resident launch was performed. Previous seven-file payload
is recoverable under artifacts/desktopstub-beta3-update-20261028-104121/.
This was an update, not another configuration reset.

The hosted workflow is being repaired independently. Local beta3 is not an
Actions-produced or attested release. Failed hosted checks continue to block
artifact publication and stable releases; the optional draft path was not
dispatched. Current CI scanner/fixture evidence is tracked in
[the scanner retry note](ci-defender-rpc-retry-2026-10-28.md) and
[the broad fixture note](ci-broad-host-contracts-2026-10-28.md).

The new Discord Channel/Profile request is separate, subsequent work. Beta3
does not contain those new reader tiles. The user's selected default is a
compose box for review; no live token extraction or message send occurred in
the beta3 pass. Experimental personal-account connections must show prominent
Discord policy/account-risk warnings. Existing rich-presence IPC remains.
