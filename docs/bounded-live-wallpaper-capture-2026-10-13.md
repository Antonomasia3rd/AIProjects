# Bounded live wallpaper capture, 2026-10-13

Live wallpaper capture previously called synchronous `PrintWindow` from the host.
Failed-attempt throttling and the in-flight guard stopped redundant calls, but
could not recover if the wallpaper renderer never returned from that call.

The host now starts a fresh child of the same DesktopStub executable for each
candidate capture. This needs no additional DLL or executable in the portable
folder. The private child command runs before INI loading, Startup reconciliation,
package operations, single-instance signaling or the resident message loop.
It receives only its target HWND/expected owner PID, minimum dimensions, the
explicit screen-fallback bit, request ID and two inherited handles.

## Deadline and containment

- A capture attempt has one 3,000 ms deadline shared by all candidate windows.
  It does not grant another three seconds to each candidate.
- The child starts suspended. A kill-on-close Job with a one-process limit is
  assigned before its initial thread is resumed. Failure to assign the Job ends
  the suspended owned child; no uncontained or breakaway fallback is used.
- The parent polls the child/cancellation state at intervals of at most 25 ms.
  Deadline or shutdown ends only this owned Job/process and allows at most
  1,000 ms to observe process exit. Parent process death closes the Job and ends
  its child independently of C++ cleanup.
- Startup capture delay now returns pending content until its configured time;
  it no longer sleeps on a UI or registration caller. Startup burst intervals,
  once-after-success, last-good fallback and the in-flight schedule remain.

The private deadline bounds waiting for the renderer's `PrintWindow`. Process
creation, local file operations and Windows termination still depend on OS
services; this does not promise hard real-time behavior for every kernel call.

## Publishing and metadata

The parent creates a fresh staging BMP file beside the intended snapshot and an
anonymous result pipe. Its explicit inherited-handle list contains only the
staging-file and result-write handles. The child writes to the supplied file
handle, with no output path or configuration file on its command line.

The child checks the window owner PID before preparing the surface and again
before `PrintWindow`. This prevents a changed/reused HWND from accidentally
capturing a different process. It preserves the existing blank/black and minimum
dimension rejection, as well as explicit opt-in screen-copy fallback.

A fixed 56-byte response preserves request identity, status, dimensions,
minimum dimensions, blank/too-small flags and a Win32 error. The parent accepts
a success only after the child has exited successfully and the response matches.
It validates the 54-byte BMP headers and total file size rather than reading a
potentially large screenshot into memory. The existing 512 MiB encoded bitmap
bound is preserved. Only then does an atomic file replacement publish the image.
Timeout, cancellation, malformed response, child failure or partial BMP cannot
replace the previous good snapshot.

Ordinary cleanup removes only the staging file created by that attempt. A parent
crash can leave a partial staging file; it is never published as a tile image.
As before, capture images are ordinary local temporary BMPs. Enabling screen-copy
fallback may record desktop icons, taskbars, overlays or other visible content;
keep that option disabled when it is not needed. This work adds no credential,
privileged launcher, protected-path or registry policy.

## Validation status

Root compiled and scanned the fixtures before bounded execution. Portable
capture cadence/protocol tests pass 1,031 checks; the Windows owned-process
fixture passes 46 checks, including parent-death containment. The normal host
also compiles and scans clean. See the [final coordinated checkpoint](queue-presets-validation-2026-10-13.md)
for installed identities and scope.

`tools/CaptureAttemptTests.cpp` adds platform-neutral isolated-helper parsing and
reply-validation checks. `tools/BoundedCaptureProcessTests.cpp` uses fresh copies
of its own fixture process with sleeping or synthetic BMP callbacks, never
Windows or desktop capture APIs. It covers completed output, timeout, shutdown,
failed Job assignment, exceptions, child crashes, wrong replies, partial BMPs,
rejection metadata, exact preservation of the old BMP and owned temporary cleanup.
An independent parent-death test transfers a synchronization handle for its own
new sleeping child, exits its parent without C++ destructors, and observes that
the production Job kills that child while the outer fixture safety Job remains
open. There is no existing-process attach or PID-based termination.

The current user DesktopStub process and its profile remain untouched by these
source edits. Actual live wallpaper acceptance remains user/target-OS testing.
