# Resume checkpoint — October 26, after v27 beta 1

**Latest repository state:** all checkpoints are merged/pushed on `main` in the
original D: checkout. See [main consolidation](main-consolidation-2026-10-26.md)
for the two remaining follow-ups: Codex blocks removing this chat's primary
extra checkout; hosted CI now reaches the mixed native/managed INI fixture and
fails because the native worker exits 2. Use D: main explicitly for next work.

**Latest completion:** [beta 2 delivery](desktopstub-beta2-2026-10-26.md)
supersedes the initial request snapshot below. Properties/Profiles/Kuro/host
preparation changes are implemented and validated, research reports are saved,
and the renewed fresh reset is complete. The source workspace is now the
managed C: worktree described there; the installed app remains at the canonical
D: path. Verify current state before any subsequent integration/reset.

This checkpoint records the user's latest requested work for resumption after
context compaction. Updating these documents does not implement the new work
or authorize another reset. The user subsequently resumed implementation and
explicitly renewed reset permission after compaction: they have not configured
the app for daily use yet. Preserve the old state in the recovery archive when
resetting for the next validated beta. [Current findings](desktopstub-open-findings-2026-10-25.md)
remain the reconciled status list.

## Delivered baseline and current-state caution

- Workspace: `D:\Users\Amiya\AppData\Local\Programs\Scripts\AIProjects`.
- Branch: `codex/review-original-requirements`.
- Delivered code `1950eee`; delivery proof `6d57629`. Version 27.0.0.1,
  DesktopStub-v27-beta1. Later documentation commits may follow this baseline.
- Shareable payload: `DesktopStub/releases/DesktopStub-v27-beta1/`; ZIP
  `DesktopStub-v27-beta1-x64.zip`, SHA256
  `E48C80FBC8C30013C209DE01CBCF0ED5893F0F526B3E2F2FAF2F9DEB34D96D12`.
- Registration/Main first run and the core single-EXE workflow were verified
  for 75 seconds: ordinary launch generated wallpaper and registered one main
  package with both helpers temporarily absent. The full three-EXE payload is
  recommended for the additional features.
- The authorized reset then removed the trial too. Old build trees/settings
  are recoverable at
  `artifacts/desktopstub-reset-20261026-175309-dad291/`. In particular, old
  `build/review-validation/` scripts are now below its `validation-build/`.
- **Historical checkpoint caution:** the user had relaunched the beta. Do not
  assume no INI/package/resident. Later explicit reset permission supersedes
  the earlier preserve-only instruction; any reset must stay scoped to this
  installation and current user and retain a recovery archive.
- Previous gates: 2,040 combined host, 2,285 source, 180 SMTC, 4,010 historical
  migration, 69 instance, 1,644 icon policy, 161 native icon, 774 asset,
  398,520 pixel/render, 39 registration and 60 activation-binding checks.
  These do not establish the newly requested UI behavior or actual 1809 display.

## New instructions to implement or record

1. **Lock screen:** add detailed-status and quick-status integration to missing
   features. Research supported Windows versions/APIs and provider eligibility
   before promising implementation of historical status slots.
2. **Kuro/WuWa stale data:** show configurable last-success age, e.g. “Refreshed
   5m ago.” Retain useful last-good resources and retry on user settings. Add
   configurable consecutive actual-failure and/or elapsed thresholds before
   tile failure text and popup notices. Five retries/50 minutes is an example,
   dependent on the configured interval. Do not count idle poll ticks as fetch
   attempts. Separate quiet retry, authentication failures, stale data and
   repeated failure; clear state on successful recovery. Prefer a reusable
   source policy with separately configurable tile/popup presentation.
3. **Calendar scope:** 1:1 UWP Mail & Calendar means provider/account support
   too: Google, Yahoo, Microsoft and appropriate generic connections. Current
   Calendar is local ICS/Google; current Mail is Gmail/Google Workspace. IMAP
   handles mail, not calendar sync itself; investigate the corresponding API,
   CalDAV/ICS or other supported source and modern authentication per provider.
4. **1809 history:** the user confirms the main Desktop issue occurs in v24
   and earlier. The newer 19041/uap10 independent-Registration restriction was
   repaired but cannot alone explain that older report. Deeply compare actual
   old binaries/configs on build 17763, Win32/Centennial versus UWP primary and
   secondary senders, package/AUMID/activation identity, manifests, publication,
   image access/ACLs and caching. Prayer text working does not prove equal
   primary Desktop Bridge support. Wallpaper/static-icon/live-corner symptoms
   must remain distinct until native evidence identifies their causes.
5. **Performance closure:** completed for now based on the user's observation
   after prior fixes. Remove the complaint from active unresolved findings;
   do not launch lock/CPU/RAM experiments without a new regression. The unrun
   controlled memory comparison is optional measurement, not an active bug.
   The independent four-pixel legacy raster discrepancy stays unresolved.
6. **Properties highlight:** remove the unwanted filled/tinted row background
   visible behind “Tile name: Desktop.” Keep restrained, readable pointer and
   keyboard focus feedback without recreating the fill. Compare EP's behavior.
7. **Properties color:** dark-on-dark/light-on-light custom or accent colors
   are unreadable. Research Accent Color Synchronizer or equivalent primary
   implementations; expose supported color-source/detection methods with
   readable palette/contrast-aware defaults. Consider custom colors, light,
   dark, high-contrast and older-Windows fallbacks. All controls use CLI/INI/GUI.
8. **Profiles UI:** integrate the separate custom Profiles window into the
   existing EP-inspired Properties framework; route its tray action there.
   Reuse the existing backend and preserve all profile actions, ownership and
   migration checks, separate-tray default, optional unified tray, automatic
   startup and crash notices. Avoid a second independent settings interface.
9. **Main host setup action:** explain/rework “Set up main Live Tile host…”.
   It prepares packaged identity for Live Tile delivery and is not needed for
   ordinary Registration/Main first run. The current setup routine can also
   change the primary Desktop to LiveTile and relaunch packaged. Investigate
   actual visibility/readiness; do not assert the user's suspected one-time
   visibility as fact. Prefer contextual/advanced placement and clear status,
   and decouple sibling Live Tile support from silently changing Desktop mode.

## Screenshot evidence and implementation map

The user's latest Properties screenshot shows low-contrast green text and a
filled green row behind the Desktop tile-name setting. The tray screenshot
shows the setup-host action at the top of Tiles. Originals were attached as
`codex-clipboard-b10a6657-9576-492a-b3cc-c58e988fa7ab.png` and
`codex-clipboard-3ad0ebaa-569c-40ef-9aed-e45968b1173a.png` in the local Temp
folder. Exact copies have now been retained privately at
`artifacts/desktopstub-feedback-20261026-properties/Properties-highlight-and-contrast.png`
and `Setup-host-action.png`. Their hashes matched the attached originals. They
are ignored artifacts, not staged desktop screenshots.

- Properties: `dependencies/DesktopStub/ga_settings_window.inc`,
  `ga_properties_options.inc`, `dependencies/properties_ui_model.h`.
- Profiles: `dependencies/DesktopStub/ga_profile_manager.inc` and
  `dependencies/desktopstub_profiles.h`, `desktopstub_profiles_native.inc`,
  `desktopstub_profile_migration.inc`.
- Host setup/menu/runtime: `ga_desktop_host_setup.inc`, `ga_tray_menu.inc`,
  `ga_tray_dispatch.inc`, `ga_app.inc` under `dependencies/DesktopStub/`.
- Kuro: `ga_kuro_provider.inc` and Kuro models/adapters in
  `dependencies/content_sources/`; reuse existing retry/last-good machinery.
- Primary rendering/delivery: `ga_generation.inc`, `ga_live_tile.inc`,
  `ga_content_state.inc`, `ga_tile_instance_runtime.inc`.
- Read-only 1809 collector: `tools/InspectDesktopStubTileImages.ps1`.
- Older independent activation: `dependencies/tile_registration.h`,
  `tile_registration_binding.h`, `tile_registration_binding_win32.inc`,
  `DesktopStub/TileLauncher.cpp`. Font policy is already implemented in
  `tile_icon_font_policy.h` and `tile_visual_assets.h/.inc`.

## Standing constraints and remaining scope

Every new behavior, including advanced controls, needs the same validated
CLI/INI/GUI representation. Match actual native/reference versions with
evidence; do not equate injected tests with complete 1:1 parity. Keep tiles
independent with coherent defaults and unused workers/observers inactive.
Preserve present settings, credentials and pins except for the explicitly
renewed fresh-beta reset, which retains recovery archives. Consent/CAPTCHA remains human.
Use bounded inert/offscreen checks and scan new binaries before execution.
Capacity-error turns are interruptions: verify saved files and test receipts
before treating an agent's work as complete. No model changes are implied.

Still open beyond the latest requests: Calendar layout/selection/localization
and reminder overrides, broader Kuro/login support, additional mail providers
and new-mail popups, full original Notes parity, lyrics, weekly timetable,
fitness APIs, Window Capture, Widgets, charging ETA/Smart Charging, the raster
defect and remaining real Windows/account/hardware acceptance. Icon fonts,
album-art presets, SMTC selection, conservative legacy migration and the basic
Registration-first flow are implemented; do not reopen them as missing features.
