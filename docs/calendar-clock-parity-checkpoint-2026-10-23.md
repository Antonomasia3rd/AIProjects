# Calendar and Clock parity correction checkpoint

**Superseded:** the October 24 correction is now installed and verified; see
[the final record](calendar-clock-parity-2026-10-24.md). The text below preserves
the interruption checkpoint, not the current installed state.

The user correctly rejected the previous manifest-only reference inspection as insufficient for the requested installed-app behavior. The current installed build is dc3115f; no DateTime parity build has been installed at this checkpoint. Preserve the current user profile and pins during the coming update; the earlier reset request was already fulfilled before this new layout report.

Read-only binary inspection now locates real generation code: Calendar package 16005.14326.22342.0 uses HxOutlookBackground.dll, including a distinct TileSmall builder around VA18008FEC8. Its strings/code select centered Small text, no branding and titlenumeral. Clock package 11.2607.6.0 Time.dll contains actual Medium/Wide/Large XML near raw offsets 0x6BBA64–0x6BBD4C, using base/title/subheader respectively. The agents are retaining module hashes, offsets and compact code/XML evidence in their final reports. These are code findings, not a claim that Windows' final visible pixels have been measured.

Work in progress: size-aware native XML and matching bitmap rules, locale-correct Small weekday formatting, Calendar Small binding delivery, preservation of explicit typography settings, focused inert parity tests, then scanned optimized artifacts and a guarded binary-only update. Root owns fixture wiring/builds. The normal app remains on the installed build until these checks pass. The historical unrelated four-pixel legacy renderer issue is unchanged.
