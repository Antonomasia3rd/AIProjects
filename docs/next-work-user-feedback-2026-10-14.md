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