# Current work and remaining findings

This is the current index for the repository rework. Dated audit files retain
their historical results; a passing test or an old statement that a slice was
complete does not close the whole repository review.

The prioritized next-reset fixes and the user/agent acceptance split are saved
in [NEXT_PASS.md](NEXT_PASS.md). The user reported the delayed gallery update
coincided with Battery Saver delaying all applications' tiles; recursive scanning
is enabled and its focused native recheck passed.

## Saved and installed

- October 20: independent tile instances, per-type settings, Plugins, Prayer
  clock/countdown, scoped status, pin-state menus and owned gallery clearing
  are installed. Native source publication and startup health passed after
  repairing the old generation admission gate. See
  [the guide](tile-instances-2026-10-20.md) and
  [validation/remaining limits](tile-instance-validation-2026-10-20.md).
  The user's configured Surabaya location is retained; older entries below
  describing an unset location refer to the initial October 18 installation.

- Text size/wrapping/layout controls and a readable RSS preset are installed,
  with Default preserving the existing appearance and source-tile inheritance.
  Non-gallery secondary updates no longer overwrite primary stable PNG paths.
  Full focused/core/render/source/smoke gates and guarded startup passed. The
  09:47 burst and memory-counter comparison are recorded in
  [the text/performance/memory follow-up](tile-text-memory-followup-2026-10-18.md).
- The later October 18 build adds the optional Prayer Times source/tile and
  reuses retained secondary PNGs after generated-cache eviction. It passed the
  full focused/core/render/source/smoke gates and guarded installation, with
  existing settings/history preserved. Prayer location remains blank, no new
  tile was pinned, and no prayer cache was created for the real profile.
  See [Prayer Times and validation](prayer-times-2026-10-18.md) and
  [gallery refresh repair](gallery-refresh-burst-2026-10-18.md).
- The 2026-10-18 CPU repairs are installed: optimized native builds,
  one cleanup per generation batch, Live Tile-only secondary assets, exact
  gallery-frame validation and a shared idle memory-maintenance cooldown.
  A 74-frame startup batch completed successfully with one cleanup. Short
  passive samples measured 0.454% mean CPU before versus approximately 0.15–0.18%
  after the repairs on this 12-thread CPU; they do not measure gallery-cycle or lock-screen
  peaks. Unchanged content/appearance parsing caches and profile/readiness
  guards are now validated and installed with preserved settings.
  See [CPU evidence and validation](gallery-cpu-batch-2026-10-18.md).
- Gallery pages now stay in Tiles status after a complete successful submission,
  through a separate status-only record. Prepared and submitted queues have
  distinct configurable labels; neither claims Windows' currently visible
  image. The fix passed 95 coordinator and 14,436 core host checks, clean scans,
  offline smoke and guarded installation. See
  [gallery status retention](gallery-status-retention-2026-10-18.md).
- AllowContentAboveLock and YourPhoneHideBanner CLI edits now request an
  acknowledged reload from an existing tray for the same profile. Offline
  configuration and utility trays remain inert. All 89 fake-runtime/private-IPC
  cases and 33 offscreen editor checks passed; those two real policy apps were
  not launched. See [notification CLI reload](notification-cli-reload-2026-10-18.md).
- Gallery changes now have a private recovery journal, startup/manual recovery,
  expected-configuration guards for dialogs and full-ledger compare-and-swap.
  Explicit source Rename now updates an existing Windows tile's DisplayName
  through a persisted name-only intent, preserving placement and native ID.
  These are installed with the first CPU repair after injected recovery and
  rename checks, clean scans, offline smoke and guarded startup. Actual
  interactive Windows rename/recovery acceptance remains separate. See
  [gallery recovery](gallery-recovery-2026-10-18.md) and
  [source rename](source-tile-rename-2026-10-18.md).
- Tile information pagination replaces the oversized MessageBox with one
  scrollable page per tile and Previous/Next controls; see
  [paged status validation](tile-status-paging-2026-10-15.md). Installed after
  14,439 passing host checks, clean scans and the guarded startup check. The
  user-selected Windhawk Atom URL was saved and preserved through relaunch.
- Follow-up: the primary tile could lose its referenced images to cleanup after
  source/gallery updates, despite successful delivery. The cause and regression
  fix are recorded in [primary image retention](primary-tile-retention-2026-10-15.md).
  The fix is installed after 14,419 passing host checks and clean scans; its
  primary files survived subsequent gallery pruning and the resident passed
  its guarded startup check. Existing settings and pins were preserved.
- The 2026-10-15 gallery/menu follow-up is built, scanned, validated and installed:
  1,024 configured photo tiles, folder groups, queued pin prompts, pinned-driven
  updates, menu/wording cleanup, prepared status parity and original game-row
  artwork. A first-start GDI+ lifetime crash was diagnosed locally, rolled back,
  fixed and covered by a new regression. The corrected resident survived the
  guarded startup check. User settings/accounts and eight existing photo tiles
  were preserved. See the [follow-up and final validation](tile-feedback-followup-2026-10-15.md).
- `2219d24`: transparent source/Quick Action fallback icons, independent
  background/color/branding controls, raw-profile validation and coalesced
  INI/CLI/tray appearance refresh. Installed after scans and bounded validation;
  the existing six source pins and Previous Track pin passed native property
  readback. The user's accounts and INI were preserved, and DesktopStub was
  relaunched. See [appearance validation](tile-appearance-2026-10-15.md).
- `0c95e6b`: the two user-created legacy bot projects and focused reliability
  repairs are committed and pushed on the review branch. The parent repeated
  TypeScript compilation and 27 injected Discord-bot tests, plus 42 injected
  Minecraft checks. No bot, game, relay, authenticated service, database or
  Startup action was run. See the [Discord review](../legacy/discord-ha-bot-starter/REVIEW-2026-10-15.md)
  and [Minecraft review](../legacy/Minecraft-Bedrock-AFK/OFFLINE-AUDIT.md).

These Node projects remain legacy applications. Their publication does not mean
they already meet the native dependency-overlay, INI/tray or DesktopStub provider
requirements. Credentials, diagnostic captures, auth caches, dependencies and
runtime output were excluded from publication.

## Gallery implemented and installed

Folder-gallery support uses the user's approved separate pools per tile. The
ownership model, bounded inventory worker, coordinator, host delivery, setup,
typed settings and per-tile appearance passed the combined build/scan/smoke gates
and are installed. The user has since set up and confirmed eight gallery tiles
with normal CPU usage. The later follow-up keeps those pins/pools and adds
scale/multiple-folder controls, without expanding them automatically. See the
[final validation and installed identities](gallery-validation-2026-10-15.md).
See the [model](gallery-engine-2026-10-15.md),
[inventory](gallery-inventory-2026-10-15.md) and
[coordinator](gallery-runtime-2026-10-15.md).

## Still open

| Area | What remains |
| --- | --- |
| Original repository review | Reconcile the implemented shared modules and all active/legacy products against the user's current rules. The dated rework log is not an exhaustive closure audit. |
| Windows presentation | Actual Start appearance, RTL/DPI, theme contrast, static fallback, and branding need target-shell acceptance. Offscreen PNG/XML and native metadata readback do not prove what every shell renders. |
| Gallery acceptance | The real folder chooser/pin confirmations, Windows animation, actual image sets and nonresponsive native-codec shutdown need local acceptance. Exact copies deduplicate; different encodings of visually identical pictures do not. Pools and cached published images are retained. |
| Gallery storage and native acceptance | Recovery and stale-dialog guards are installed and pass private failure injection. Real interactive recovery, Windows queue behavior and owner-aware retirement/storage reporting remain acceptance or engineering work. Do not force failures against the user's actual ledger. |
| Historical renderer variation | The unexplained four-pixel opaque preset difference remains open. Exact assertions have not been relaxed; current passing renders do not establish its cause. |
| CPU attribution and idle work | Idle samples remain about 0.15–0.18% versus 0.454% before repairs. The scheduled gallery refresh peak was traced to repeated rendering beyond a 32-entry cache; retained-PNG reuse is now installed and passes the 40-image eviction fixture. Measure the next ordinary refresh before claiming a real peak reduction. Residual idle-thread cost, Win+L causality and the older NPSMSvc episode remain separate work. |
| Real integrations | Actual Startup-folder/StartupTask, sleep/wake keyboards, player icons/actions, and HoYoLAB browser/discovery/session behavior still need local acceptance beyond injected fixtures. Preserve existing accounts while testing. |
| Source tile names | Explicit persisted Rename now has name-only native propagation and private injected tests. Confirm a real renamed pin keeps placement and appearance in normal use. Feed changes and artwork migration continue to preserve user-chosen names. |
| Older delivery paths | Unpackaged/older-Windows helper queue correlation remains conservative; those paths keep app-managed rotation where individual delivery cannot be acknowledged safely. |
| Notes compatibility | The current v2 account flow works in the user's screenshots. Older credential/import aliases still need protocol review; role discovery alone is not daily-notes acceptance for every alias. |
| Windhawk | The source catalog and the syntax-check scope are corrected. Functional hooks were not reviewed or loaded in this pass; build-specific work requires current local binaries and matching symbols first. |
| Legacy bots | Focused offline reliability repairs passed. Live HA, database, voice, updater, relay and protocol behavior were not exercised, and full native overlay migration remains separate work. |

Security additions remain opt-in and lower priority than functional repairs.
Plain credentials, configuration backups and full dumps should stay private.
The Microsoft comparison submission remains deferred at the user's request;
no additional file or dump was submitted and Defender was not bypassed.
