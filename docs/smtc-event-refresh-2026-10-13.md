# SMTC event refresh, 2026-10-13

The retained media reader now supports worker-owned event subscriptions when
the host requests event-driven operation. The host enables that mode whenever
direct Live Tile delivery is available: Live Tile mode is enabled, registration
is complete, and the resident has package identity. This includes a single SMTC
content and does not depend on content count or rotation driver. Registration
mode and compatibility-helper delivery preserve configured polling.

The worker watches the manager's
[CurrentSessionChanged](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager.currentsessionchanged)
event and the selected session's
[MediaPropertiesChanged](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssession.mediapropertieschanged)
and [PlaybackInfoChanged](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssession.playbackinfochanged)
events. These identify current-session, metadata and playback changes. Timeline
progress is deliberately not subscribed, so advancing playback position does
not cause a refresh storm.

Callbacks only mark a coalesced dirty flag and wake the existing host poller.
They perform no media/COM reads, image decoding, INI writes or tile updates. The
worker obtains one session and property snapshot for text and artwork after the
host observes dirty state. A fetch already running is allowed to complete;
duplicate events cannot force-cancel it. A manual force/new profile still
requests fresh work.

With supported event subscriptions, safety polling is no faster than 30 seconds
(or the longer configured source interval). A new event can request an earlier
refresh. If subscription support is unavailable, the reader preserves ordinary
configured polling rather than claiming it can observe changes. The existing
host tick, cache comparison and Windows queue acknowledgments determine which
changed frames need delivery.

Each callback holds only a weak reference to an epoch-specific state. Reset
closes the wake gate and clears its callback before revoking event tokens and
releasing the session/manager proxies. The owning worker's MTA is uninitialized
last. Closing waits for any already-entered wake to finish, preventing use of the
host condition variable after its lifetime. A replaced selected session also
closes its own epoch, so already-queued old-session callbacks cannot wake the
replacement. Unsupported subscription attempts are not repeated every snapshot
for the same manager/session.

## Validation status

No builds, execution, real media sessions, native media events or user profiles
were accessed by this subtask. Root coordinates fresh scans and bounded fixtures.

Prepared `tools/SmtcSourceTests.cpp` cases use fake WinRT managers/sessions and
synthetic callbacks. They cover retained subscriptions, actual metadata/playback
token registration and revocation, no timeline subscription, 10,000-event
coalescing, callback-only behavior, unsupported subscription fallback, stale
callbacks after session replacement/Stop, and closing an in-flight wake before
teardown. Existing injected text/artwork fixtures preserve the coherent snapshot
and asset-lease coverage. Shared source tests cover the read-only pending-work
query used to avoid busy-fetch cancellation.

`DesktopStub/tools/NativeQueueHostTests.inc` uses an intercepted batch delivery
that acknowledges successful synthetic frames. It covers unchanged-frame
suppression, updating one tag, queue exit with the same first frame, app-managed
fallback, sparse tags 9/10, native text visual-key reuse versus baked overlay
keys, and protection of older assets still referenced by queued slots. It never
calls a real tile updater or starts the product entry point.

The single-content direct-delivery regression uses an injected SMTC reader. It
checks that event cadence is requested while native queue eligibility remains
false, that the synthetic media frame stays coherent, and that its delivery
creates one ordinary update without a queue batch or repeated read. This case
was added on 2026-10-14; root owns build and execution validation.

Real Windows/player notification behavior and native queue rotation remain
target-system acceptance, separately from these inert checks.
