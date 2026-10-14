# Next work: user feedback after source tile actions, 2026-10-14

The user explicitly requested recording these findings for future work because
usage is at 3% remaining. Do not implement or run more tests in this checkpoint.
Base implementation commit: 803bbba, codex/review-original-requirements.

## What the user has now verified

- At least the Previous track Quick Actions tile works.
- Embedded HoYoLAB sign-in opens; the user reports having signed in.
- Later screenshot shows game resource values (ZZZ 240/240, HSR 300/300,
  Genshin 200/200) instead of configuration-error placeholders. This is visual
  feedback, not an independent account/backend verification.
- The Desktop wallpaper tile is visually clean in the supplied screenshots.
- Sleep/wake CapsBlink recovery has not been confirmed by this message.

Preserve current user account credentials, pins, settings and running process.
Do not repeat the earlier reset/migration or extract tokens merely to resume.

## Requested fixes, still open

1. Remove the redundant Quick Actions pinning-enable gate.
   The user asks why a separate Enable pinning Quick Actions tiles switch exists
   when individual Pin/update actions can express the choice. Prefer direct
   per-action pin/unpin controls; retain the user's existing action pins.

2. Fix Discord placeholder truncation.
   Current top line says Configure Details and State under Tiles > ... and gets
   clipped. Use wrapped text or move the setup instruction to a lower/subtle
   field, with a short meaningful headline. Validate Windows template wrapping
   and Medium/Wide/Large layouts, rather than lengthening the headline.

3. Use native-looking Quick Actions icons.
   The current colored circular geometric icons are not the desired appearance.
   Prefer installed Windows system glyph fonts/icons with OS-version fallback.
   Verify glyph availability on supported Windows versions and avoid introducing
   mandatory new DLLs or downloaded icon packs.

4. Fix SMTC alignment and indicator semantics.
   Text moved to the vertical center after the two-column template change; user
   questions that placement. Restore the previous/top layout by default and test
   all tile sizes. The adjacent icon currently resembles an action; user wants
   current playback status, with correct playing/paused/stopped/unknown handling.
   Consider configurable indicator modes: playback status, originating app icon,
   and optional additional repeat/shuffle indicators where the player exposes
   them. Keep click action distinct from the displayed status.
   Inspect media_tile_layout.h and ga_live_tile.inc. Current glyph selection
   searches formatted badge text for Playing; replace that with explicit media
   status data, not a localized-text substring heuristic.

5. Rework embedded HoYoLAB UX against the original Real-Time Notes project.
   User prefers 1:1 original behavior where possible. Find the correct original
   implementation/history/UI before deciding the old project lacks a feature;
   the prior quick history lookup found one native manual-import version only.
   Investigate intermittently blank/white WebView. One screenshot shows HoYoLAB's
   account-security verification page; another is completely white. Do not
   assume those have the same cause. Add navigation/process-failure diagnostics,
   loading/error/retry UI and useful recovery without logging credentials.

6. Detect game account/UID automatically.
   Generic Game UID / Enter the numeric game UID prompts are confusing and do
   not identify which game the user opened. Clearly show Genshin/HSR/ZZZ in the
   window and selected account. Prefer retrieving bound game accounts through
   the original supported flow after sign-in, letting users choose when multiple
   accounts/servers exist. Keep manual entry as a explained fallback. Verify
   relevant API behavior before implementation; do not guess a game UID.

7. Make embedded browser-session retention configurable.
   User recalls the original discarding browser cookies and wants this useful
   behavior for separate HoYoLAB accounts across games. Clear local embedded
   session data without calling HoYoLAB Logout, which the user warns can
   invalidate already imported tokens. Distinguish stored app credentials from
   browser-session cleanup. Consider fresh/ephemeral login sessions and explicit
   retained-session mode. Prevent accidental reuse of one game's account for
   another. Preserve the app's imported account tokens when cleaning browser
   storage; no server-side logout or silent credential clearing.
   Verify original behavior and actual token lifecycle before finalizing defaults.

8. Expand game tile information.
   Current resource-only tiles waste space. Stack available fields similarly to
   RSS, using the original Real-Time Notes models: resource count/recovery,
   expedition progress, game-specific scratch-card/daily-task information, etc.
   Use appropriate resource/topic icons on the right. Show only fields actually
   available for that game/account; represent unavailable data honestly.
   Cover full/partial/error states and Medium/Wide/Large plus bitmap Overlay.

## Evidence and resumption

Private screenshot copies are under:
build/review-validation/Feedback-20261014-2104/Screenshot-1.png through
Screenshot-4.png. This ignored directory is local evidence, not part of Git.
The original attachments were clipboard temp PNGs, so copies prevent their loss.

Related implementation/validation records:
- docs/tile-actions-validation-2026-10-14.md
- docs/notes-embedded-signin-2026-10-14.md
- docs/caps-auto-keyboards-2026-10-14.md
- docs/multiple-tiles-2026-10-14.md

Resume with usage check, working-tree inspection and the current account's safety
constraints. Ignore Windhawk and the unrelated legacy/discord-ha-bot-starter work.
Use bounded inert tests by default, scan before execution, and preserve/relaunch
the normal DesktopStub if replacement is required. Do not claim prior offline
checks cover the visual/account behavior newly reported here.
## Additional user feedback: fallback logos, galleries and deferred app audits

The user again explicitly requested saving only, because limits have not reset.

### Confirmed fallback-logo defect (priority with existing DesktopStub bugs)

The attached screenshot shows Now playing, RSS, Genshin, HSR, ZZZ and Discord
all displaying the Desktop monitor logo after live content clears. The intended
topic-specific static fallback is therefore NOT fulfilled on the user's actual
pins. This is an open bug, not a request to change the main Desktop tile.
The existing Previous track action tile has its own icon, demonstrating a useful
comparison. Inspect actual existing secondary tile VisualElements, UpdateAsync
metadata publication, all size logos, asset URIs and shell caching. Check whether
requiring Pin/update on older tiles is hiding a migration bug; make the normal
upgrade/setup path apply the correct topic assets without repinning everything
or silently creating duplicate destinations. Treat source implementation as
unverified until a cleared/offline existing source tile visibly shows its own
topic icon. Do not claim generation of PNGs alone fixes the shell fallback.

Private screenshot copy:
build/review-validation/Feedback-20261014-2104/Screenshot-5-static-fallback.png.

### Requested folder galleries (after bug fixes)

Example folder supplied by the user:
D:UsersAmiyaPicturesArtworksScreenLandscapeProject SEKAINene Kusanagi

Create multiple gallery tiles with Photos-style image rotation, all managed by
the existing process/tray/configuration/Startup. Hard requirement: no two tiles
show the same image at the same time. Keep CPU/disk work low. The user asks whether
Windows' five-notification queue can be replenished just before the fifth item.

Current shared catalog supports an explicit ImagePath per content, manual content
cycling and eligible primary notification queues, but has no folder gallery source
or automatic coordinated multi-gallery allocator. Secondary source mappings
currently have one composed frame each and a fixed six-source catalog. The
requested feature is not implemented. Do not inspect the example artwork folder
or configure galleries during this save-only turn.

Future design/research:
- Verify Windows queue limits and observable display state before promising
  detection of the currently shown/fifth item. Queue position/order cannot be
  inferred reliably just from the application's submission count or a timer.
- Microsoft documents up to five queued notifications and tagged replacement:
  https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.tileupdater.enablenotificationqueue?view=winrt-28000
- Because Windows owns visible cycling, consider disjoint image allocations for
  every simultaneously active tile's entire queue/pool. Merely choosing different
  current submission heads cannot guarantee no simultaneous duplicates.
- Consider stable per-tile disjoint pools as a safe design candidate: independent
  Windows rotations then cannot select the same reserved image. This is a design
  proposal, not an implemented guarantee or final UX decision.
- Handle too few images, duplicate file aliases/content, additions/deletions,
  failed delivery, retained old notifications, paused/offline tiles, changing
  tile counts and restart recovery. Never recycle an image reservation while
  another destination may still display it.
- Use cached thumbnails/assets and event-driven directory changes, bounded
  background decoding and infrequent queued replacements. Compare strict
  exclusivity with controllable app-managed rotation if the OS cannot expose
  needed state. Clearly explain limitations rather than approximating the hard
  requirement without telling the user.
- Expose folder/tile creation, fitting, order/shuffle, cadence and failure state
  consistently through INI/CLI/tray, with no extra resident processes.

### Low-priority audits now authorized

The user finished their own edits to:
- legacy/WindhawkMods
- legacy/discord-ha-bot-starter
- legacy/Minecraft-Bedrock-AFK

The earlier instruction to ignore those areas is superseded: future bug/quirk/
improvement reviews are authorized, and relevant reviewed changes may be pushed.
Priority is explicitly very low; complete the DesktopStub bugs/UX above first.
Bot and Minecraft folders are currently untracked user work. Do not stage or
publish them merely as part of this checkpoint; inspect secrets, build/output
files and intended repository contents before a later push. Preserve all edits.
For build-specific Windhawk hooks, verify installed binaries and matching symbols
before relying on old offsets or reverse-engineering. No audit, build, execution,
installation or push of those projects was performed in this save-only turn.

### Original repository task remains open

The user explicitly asked whether the original bugs/findings are all finished.
Answer is NO. Many documented consolidation/configuration/Startup/provider and
smoke fixes are implemented, but no exhaustive closure audit has been completed.
Current records to reconcile before expanding features further:
- docs/REWORK_AUDIT.md (chronological; early entries are historical)
- docs/remaining-findings-validation-2026-10-13.md
- docs/queue-presets-validation-2026-10-13.md
- docs/audit-render-determinism.md
- docs/one-host-tiles-validation-2026-10-14.md
- docs/tile-actions-validation-2026-10-14.md

Known retained original items include the unexplained historical four-pixel
renderer variation; actual high-CPU/NPSMSvc causality (the user's 17-hour stable
run is encouraging but not a reproduced cause/fix); native Windows presentation/
branding/RTL/DPI and actual Startup/account/hardware acceptance; unpackaged older
Windows helper queue correlation; and remaining repository-wide requirement
review. New user findings above are additional open bugs, not evidence that the
original review is complete. Security additions remain opt-in and secondary to
functional fixes. Update older docs that still describe SMTC as read-only or
CapsBlink hardware as dependent on visible text when doing the closure pass.