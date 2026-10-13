# Live wallpaper capture scheduling, 2026-10-13

Read-only CPU review found a concrete issue in the opt-in live wallpaper capture
path. `TryGetLiveWallpaperSnapshot` updated its capture timestamp only after a
successful image. A blank, too-small, failed or unwritable capture could
therefore repeat on every wallpaper query instead of observing the configured
refresh interval. Capturing releases the state mutex around `PrintWindow`, so
concurrent poll/UI/registration callers could also perform overlapping captures.

The correction adds shared `dependencies/capture_attempt.h`. Its pure monotonic
schedule claims an in-flight token before beginning any capture work, and records
completion time for both success and failure. A second caller returns the last
good snapshot or the ordinary fallback while the token is held. Startup settling
still suppresses premature static-wallpaper fallback as before.

Normal retries use `LiveWallpaperCaptureRefreshMs`; a startup capture burst uses
its separate startup refresh interval. Refresh zero still means capture once
after success. Initial failures in that mode can recover on a bounded one-second
retry cadence. Provider reset invalidates the old timing without allowing a new
capture to overlap an old attempt still finishing. The old attempt's completion
cannot delay the replacement provider.

An RAII scope reacquires the host state mutex and releases the token on every
return or exception, including path generation, rejection, file failure and
logging failures. The existing successful-snapshot publication and eight-file
rotation remain unchanged.

Microsoft documents that `PrintWindow` is synchronous and can take enough time
to affect application responsiveness:
[PrintWindow reference](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow).
This correction bounds redundant attempts and overlap; it does not make a hung
window's synchronous call cancellable or introduce a new capture process/thread.
No native windows or user desktop images are captured by the automated checks.

## Validation status

`tools/CaptureAttemptTests.cpp` compiles, custom-scans clean and passes 1,014
checks in root's bounded runner. It uses synthetic times, primitive snapshot values, a fake throwing capture
and a bounded mutex/thread fixture. Prepared cases cover failed retry timing,
startup-vs-normal intervals, once-after-success, backwards time, stale completion,
reset during capture, exception cleanup, last-good preservation and concurrent
call exclusion. The source subtask did not execute a binary; root performed the
coordinated compile, scan and execution. The normal host build/scan also passes.

The current running DesktopStub and its profile are preserved. Live wallpaper
capture remains disabled by default. No native `PrintWindow`, window enumeration,
screen-copy, service, account or resident API was exercised for this validation.

A separate tentative sibling-precache finding was withdrawn after checking its
existing predicate: `GeneratedAssetPrecacheEnabled` already rejects composition.
Do not attribute repeated composed precaching to the original implementation.
Root independently owns any guard for an older legacy precache job when its
configuration changes while that job is running.
