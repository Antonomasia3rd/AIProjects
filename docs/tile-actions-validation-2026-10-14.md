# Source tile clicks, actions, visuals and Caps recovery, 2026-10-14

Current source fixes:
- Windows appends /tileid to secondary activation. The parser now consumes it,
  preserves explicit source/action arguments, and routes known IDs when Windows
  supplies only metadata. Primary/default activation remains unchanged.
- Now playing clicks toggle playback by default. Tiles > Quick Actions >
  Now playing tile click selects setup, play, pause, next, previous, repeat,
  shuffle or volume. MediaActions.VolumeStepPercent defaults to five percentage
  points on the default multimedia output endpoint. SMTC controls apply to the
  current Windows media session; unsupported actions show an explanation.
- Optional static Quick Actions tiles use the same process. Enable pinning from
  their tray submenu, then pin chosen actions individually. Turning pinning off
  prevents new pins; existing tiles remain usable until explicitly unpinned.
- Static source/action logos use self-contained geometric topic artwork. Choose
  Pin/update tile for an existing source to replace its previous Desktop logo.
  SMTC adaptive live text uses a right status/action icon column. Configured
  clean-shutdown clearing now also clears pinned source notifications, returning
  them to their static logos; crashes/forced kills may retain Windows' last text.
- Embedded HoYoLAB sign-in uses WebView2 and explicit Use signed-in account.
  Enter that game's UID and optionally protect saved login fields with DPAPI.
  Only supported HoYoLAB cookies from this embedded session are imported, using
  the existing atomic Notes validator. No external browser/session extraction.
- CapsBlink hardware starts independently of selected text sources, even when
  tile content is disabled. Auto enumerates present keyboard class interfaces,
  opens accessible endpoints and preserves each keyboard's other LED bits.
  Resume/device-change requests reopen; error retries are bounded to five seconds.
  Explicit numbered targets remain supported. New profiles default to Auto.

Pinned official WebView2 SDK 1.0.4258.31 supplies the header and static loader.
No loader DLL or browser installer is shipped. Installed Evergreen WebView2
Runtime remains required for embedded sign-in; unavailable runtime gets guidance.
The browser keeps a profile-specific cached sign-in session under LocalAppData.
Plain exported/imported tokens in the INI remain readable unless the user enables
the available opt-in protection. Keep INIs, browser data, backups and dumps private.

Validation available before final installation:
- Inert host/menu: 10,774 checks passed before final metadata fallback test additions.
- Pure tile artwork plans: 138 checks passed.
- Fake notification transport: 881 checks passed.
- Caps engine fake-backend tests passed, including all 131,072 pattern/logical
  combinations, multi-endpoint acquisition, preservation, restore and reopening.
- Product/broker compiled after resolving Windows small macro and GDI+ helper-name
  collisions. Final metadata regression/build/scan results are appended below.

Native playback/audio, actual sign-in/cookie import, pin metadata updates and
sleep/wake LED behavior require local acceptance. Automated tests never drove
the user's player, browser credentials or keyboards. Existing CapsBlink queue
eligibility policy is still conservative; relaxing that policy is a follow-up,
not required for independent global hardware control.
## Final installed validation

Final activation host fixture: 10,778 checks passed (0 failures), including
Windows-only /tileid routing, explicit launch arguments, Windows-safe volume
action IDs and missing metadata values. Three scan-only/review --help activation
commands also exited 0. Final full copied offline renderer/configuration smoke
passed with exit 0. Fresh Defender custom scans of final reviewed and installed
canonical host/broker found no threats.

Host SHA256: BF9B65363CD806AB90FE196678D2E17FDA8702E65C263CA2DD23D9D2449BAFB4
Broker SHA256: 5C63211BA9855B5950BFB5A2BE6B4521544D078DC7B960B08D697F3076A7C341

Installed through graceful exit + backup + verified/scanned copies + atomic
configure-only profile update + finally relaunch. Backup is
DesktopStub/build/ReviewBackupOct14Actions-20261014-205724.
Current profile preserves custom text slot on Content 1, removes CapsBlink from
its text sources, sets keyboard target Auto and sets SMTC click toggle with
five-point volume step. Existing HardwareEnabled/Startup/account settings remain.
Observed one DesktopStub resident (PID 1932) and successful source tile updates
after relaunch. Native media button, actual keyboard/wake and embedded sign-in
acceptance remain pending the user's local test. No actual sign-in/playback action
was executed by automated validation.