# Gallery status after delivery — 2026-10-18

Previously, acknowledging a gallery's final queue slot removed its pending
frames. The next content tick replaced the shared prepared snapshots, causing
that gallery to disappear from the Tiles status dialog until its next refresh.

The gallery coordinator now keeps a separate in-memory status record containing
the last **complete successfully submitted window**, at most five frames per
configured destination. Partial acknowledgements cannot replace it. A failed
replacement keeps the preceding complete record. When a new complete prepared
window exists, status shows that window as prepared; otherwise it shows the
last complete submitted window. Successful durable acknowledgement can clear
pending delivery work while the submitted status remains available at idle.

These records never become delivery work. `CurrentFrames`, current receipts and
the generation pipeline keep their existing behavior. Native status snapshots
also have delivery keys and native-queue/reset flags cleared, so accidentally
passing one to batch planning cannot resubmit an old queue.

Status is invalidated on profile or gallery configuration changes, a new loaded
ownership epoch, unpinning, redistribution suspension and shutdown. The dialog
also checks current configuration identity and cached pin membership before
showing a record, avoiding stale destinations before the next content tick.
There is no new disk file: after restart, records are established by new
successful submissions rather than inferred from Windows' private tile state.

Prepared queues retain the existing `[Strings] TileStatusQueue` label. Submitted
queues use `[Strings] TileStatusSubmittedQueue`, defaulting to “Last successfully
submitted image queue (Windows chooses the visible image)”. Neither label claims
which image is currently visible. Gallery-only status does not add an empty
ordinary-content page. Primary/source formatting and per-destination pagination
remain in the existing status formatter.

Prepared validation consists of pure coordinator fixtures for complete versus
partial acknowledgement, idle retention, failed replacement, subsequent success,
configuration/profile/epoch/unpin invalidation and suspension. An injected host
fixture checks submitted/prepared wording, all five slots after delivery is
empty, no stale prepared duplicate, gallery-only pagination and structurally
inert status snapshots. The parent rebuilt and scanned the optimized fixtures:
all 95 portable coordinator and 14,436 core host checks passed. Fresh source
guards passed 1,764 DesktopStub and 277 shared checks. The final optimized
product built successfully, scanned clean and passed the full copied-binary
offline smoke. Host SHA-256:
`CBC685FD58CAF400B55F5409781BC43C53AC2CE6C5D51C2199E3DEEA747A6141`;
broker: `B13130B38EE89250545FD370F84D46F5E7A8A8C33E98E480C0210966983BCD46`.

Installed at 06:29:38 with backup
`DesktopStub/build/ReviewBackupOct18-20261018-062938`. Resident PID 31136 passed
the guarded 75-second startup check. INI/accounts, tile pins and current gallery
ownership were preserved. No real redistribution, deliberate write failure or
pin mutation was used for validation. Actual Start presentation remains a
normal-use acceptance check.
