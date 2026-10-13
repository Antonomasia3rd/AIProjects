# Live Tile queue transport

Prepared on 2026-10-13. Compilation, scans and execution are pending root
coordination. No Windows notification API was called by this subtask.

## Queue semantics

Microsoft documents that [EnableNotificationQueue](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.tileupdater.enablenotificationqueue)
enables up to five notifications across tile sizes. Windows controls rotation.
A [Tag of at most 16 characters](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.tilenotification.tag)
lets a new item replace one with the same tag, without a guarantee that it keeps
the old queue position. This transport uses bounded ASCII alphanumeric tags for
its own stable content identifiers; that alphabet is an internal format choice,
not an additional Windows restriction.

The scheduler/layout/asset lease implementation is owned separately. This
change only carries its `nativeQueue`, `nativeQueueTag`, and `nativeQueueReset`
snapshot fields across direct and helper update paths.

## Private payload

`dependencies/tile_notification_payload.h` provides pure C++17
`aip::tile_notification::Options`, `Payload`, `Encode`, `Decode`, and an injected
`Deliver` operation. A private fixed-order versioned header precedes unchanged
Windows XML when queue or explicit layout-reset metadata is present. The
metadata is removed before `XmlDocument::LoadXml`; no unsupported attribute is
inserted into the Windows tile schema.

Ordinary unqueued/default XML is returned unchanged and remains compatible with
old raw pending XML. An unqueued explicit reset uses the wrapper so leaving a
queue can clear once. Queued tags require 1-16 ASCII letters/digits; an unqueued
message has no tag. Missing/unknown versions, malformed flags, extra fields,
bad tags, NUL/control/invalid Unicode text and XML at/above 64 KiB UTF-8 fail
before an updater is created. Body XML is loaded and the notification/tag is
constructed before queue/clear/update side effects. Native loader exceptions
retain the calling consumer's existing WinRT error reporting.

`dependencies/tile_notification_winrt.inc` owns the single production delivery
adapter. It enables the decoded queue flag, clears only when the caller requests
a layout reset, and updates the tagged notification. It does not clear whenever
an existing slot changes. Explicit lifecycle/manual Clear requests retain their
separate existing path.

## Consumer parity

The normal host, bare activation/OOP host, Win8/8.1 broker and experimental
background task all use that same adapter. The existing package-LocalState copy
preserves the entire private payload. Their pending-file behavior no longer
downgrades an existing unreadable/empty payload into a stable unqueued update:
only a genuinely absent pending file may use the legacy activation fallback,
and the broker falls back to its module-side file only when the LocalState file
is absent. UTF-8 helper readers keep strict decoding and bounded payload reads.

No pending file is deleted after a decode/update failure. Asset retention,
successful-layout commit, Windows queue scheduling, cross-restart asset lifetime
and per-layout reset issuance are separate root-owned work.

## Pure/injected regression

`tools/TileNotificationPayloadTests.cpp` covers raw legacy compatibility,
canonical queued/unqueued/reset round-trips, a maximum-length tag, malformed
header/tag/Unicode rejection with zero loader/updater calls, UTF-8 byte limits,
supplementary/CJK text, creation ordering, one initial reset, adding/replacing
tagged slots without another Clear, explicit queue departure, and XML/notification/
tag-construction failures before updater creation. Its fake updater models
tag replacement and five-item capacity; it makes no actual Windows/API calls
and does not claim to prove OS rotation timing.

The fixture remains portable without Windows headers. Root owns its build/CI
wiring and normal host/helper compile checks. No assertion was relaxed, account
or device accessed, package/Startup configured, service changed or protection
bypassed by this source work.
