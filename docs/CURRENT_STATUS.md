# Current work and remaining findings

This is the current index for the repository rework. Dated audit files retain
their historical results; a passing test or an old statement that a slice was
complete does not close the whole repository review.

## Saved and installed

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
| Gallery failure recovery | Explicit redistribution clears historical notification queues before a ledger reset, but ledger and INI publication are separate atomic files. Final INI-write failure after reset and concurrent edits during add/resize dialogs need additional cross-file recovery/expected-configuration guards. See the follow-up record; clear failure and ordinary model tests do not close these cases. |
| Historical renderer variation | The unexplained four-pixel opaque preset difference remains open. Exact assertions have not been relaxed; current passing renders do not establish its cause. |
| Media CPU | Cached SMTC lifetime and event scheduling are repaired, and the user reported 17 hours of normal CPU. The original DesktopStub/NPSMSvc episode was not reproduced, so its precise cause remains unproved. |
| Real integrations | Actual Startup-folder/StartupTask, sleep/wake keyboards, player icons/actions, and HoYoLAB browser/discovery/session behavior still need local acceptance beyond injected fixtures. Preserve existing accounts while testing. |
| Source tile names | A source's saved menu/status name is independent of its RSS URL. Review propagation of source Rename changes to an already-pinned Windows DisplayName; current artwork migration intentionally preserves native names. |
| Older delivery paths | Unpackaged/older-Windows helper queue correlation remains conservative; those paths keep app-managed rotation where individual delivery cannot be acknowledged safely. |
| Notes compatibility | The current v2 account flow works in the user's screenshots. Older credential/import aliases still need protocol review; role discovery alone is not daily-notes acceptance for every alias. |
| Windhawk | The source catalog and the syntax-check scope are corrected. Functional hooks were not reviewed or loaded in this pass; build-specific work requires current local binaries and matching symbols first. |
| Legacy bots | Focused offline reliability repairs passed. Live HA, database, voice, updater, relay and protocol behavior were not exercised, and full native overlay migration remains separate work. |

Security additions remain opt-in and lower priority than functional repairs.
Plain credentials, configuration backups and full dumps should stay private.
The Microsoft comparison submission remains deferred at the user's request;
no additional file or dump was submitted and Defender was not bypassed.
