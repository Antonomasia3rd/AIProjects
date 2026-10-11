# DesktopStub settings reorganization validation, 2026-10-11

The user confirmed that the preceding Live Tile enable crash no longer occurs,
then requested a complete reorganization of the confusing settings menu.

## Result

Seven categories replace the overlapping General/Content/Wallpaper/Live Tile/
Appearance layout: Tile contents; Sources and accounts; Tile display; Updates
and cycling; Application; Diagnostics; Advanced. Provider credentials no longer
share a flat list with content slots and rotation controls. Wallpaper settings
are a source submenu. Quick layouts replace the user-facing legacy-preset label.
Standalone fallback text is clearly distinguished from composed-entry text.

Content entries group backgrounds, source selection/order, custom text and
RSS/Notes details. Discord advanced options group by the real descriptor section;
ASUS groups microphone, keyboard and error options; manifest editing groups
identity, appearance, application, compatibility, images and helpers.

Command IDs and functional INI/CLI mappings are unchanged. Stock label migration
preserves customized labels. Dynamic menu values escape literal ampersands.
Construction is factored into `PopulateDesktopStubTrayMenu`, separate from
resident Startup reconciliation, provider lifecycle work and visible popup
tracking. The old flat presentation remains available through the same setting.

## Evidence

- **6,443 runtime/menu checks passed**, including recursive command reachability
  and uniqueness, all Discord and manifest fields, grouped/flat states, 32-slot
  reachability, masked account values, unchanged configuration on construction,
  repeated USER-object cleanup and stock/custom label migration. The existing
  content/provider/import/transaction/snapshot fixtures also passed. The complete
  fixture took 24.993 seconds under its existing 45-second outer bound.
- **1,764 DesktopStub source checks passed**, including all new UI-string loads.
- Normal v27 host/broker built successfully; custom Defender scans found no
  threats before execution or installation.
- Copied fixed-host offline content smoke passed: version/help/configuration,
  offline integration rejection, Medium/Wide/Large composed text/cache behavior,
  invalid inputs and concurrent INI writers. It used render/configure-only paths;
  no real package, Startup, account, hardware or provider networking was used.
- The normal executable/broker were replaced only after confirming no resident
  instance was running. Their hashes matched the scanned review builds, both
  installed paths scanned clean, and the existing INI hash remained unchanged.
  Original pre-reorganization executables are preserved locally.

| Installed binary | SHA-256 |
| --- | --- |
| DesktopStub.exe | `974D3FB8566C6AD574A5D548663B0B268B895ABA6D58082DA53324925ED56E83` |
| DesktopStubLiveTileBroker.exe | `1EB803211B3829D36F92AC5022CA6FDC28E114D2AB47B199286BF9AFCEDE3CD7` |

Independent read-only review compared the old/new named command inventories and
found no omitted old controls. It found two allocation-fallback regressions in
the initial draft: a failed entries submenu hid Add/count controls, and a
redundant background-choice allocation could hide an otherwise valid selector.
Both were corrected before the final tests/build. Failed Discord section-menu
allocation also retains its fields in the parent rather than dropping them.

Initial checks caught the layout checkbox's expected grouped/flat state
difference and the new titles' missing standard UI-string-loader wiring. The
checkbox now has its own state assertion, and titles use the existing loader;
the final suites above pass. No assertions were removed to hide a missing control.

Logs, scan hashes and local binary backups are under ignored
`build/review-validation/desktop-settings-*` and `DesktopStubBeforeSettings*`.
See the [settings guide](desktopstub-settings.md) for navigation. Actual visual
popup/dialog acceptance remains for the user; in-memory menu inspection does not
claim screenshot or target-Windows appearance verification. The independent
four-pixel renderer discrepancy remains unresolved, and its exact gate was
neither rerun nor relaxed. Windhawk files were untouched.
