# Native queue delta planner

Pure source prepared on 2026-10-13; build/scan/run results remain pending root
coordination. This planner does not call Windows, start a timer, read/write a
file, retain asset ownership, or mutate the supplied snapshot vector.

`dependencies/content_queue.h` supplies `aip::content::QueueDelta`:

- `Plan(layout, snapshots, force=false)` returns ordered copies of new/changed
  ready slots. A fresh/different layout returns all slots with only the first
  marked `nativeQueueReset`. An unchanged layout returns only tag/frame-key
  deltas. Force returns all without an extra Clear. Planning is const and does
  not acknowledge a layout or a frame.
- `Ack(snapshot)` records a successful API delivery. Only explicit reset or a
  new layout clears the old committed tag/key map. A failed API never calls Ack.
- `LeaveQueue()` resets only local planner state; reentry and a new process both
  require an initial reset of any Windows queue left from the previous run.

Frame keys are opaque visual identifiers computed by the host. They must exclude
the app's cycle timer when Windows owns rotation. Tags retain the input vector's
source order, including Content9 before Content10; map lexical order does not
choose the initial reset slot. Frame inputs must be active, have unique bounded
alphanumeric tags/nonempty keys, and contain at most five slots. Invalid plans
throw before changing state; invalid acknowledgements are ignored.

The host owns mutex serialization, frozen provider reads, queue eligibility,
cooldown, actual transport and asset lifetime. It must stop a batch if its first
reset-bearing payload fails; later reset=false slots must not be sent while an
old OS layout might remain uncleared. Successful reset followed by later-slot
failure acknowledges only successful slots, so retry preserves those slots and
does not clear again. API failures, rather than merely producing PNGs, determine
which frames are acknowledged.

`tools/ContentQueueTests.cpp` covers two/five-item eligibility, six-item fallback,
disabled/non-native/host-driver cases, automatic/native driver, preview versus
active hardware/presence actions, disabled actuator entries, interval-only
changes, media-only replacement, force without reset, partial successful batches,
layout changes, leaving/reentering/restarting, immutable inputs, slots 9/10 and
invalid plan/acknowledgement preservation. These tests do not claim that Windows
uses a particular rotation interval or keeps tag replacements in the same order.

Root owns build/CI wiring and the actual host/provider integration. No production
asset cleanup or disk/registry persistence is added by this source work.
