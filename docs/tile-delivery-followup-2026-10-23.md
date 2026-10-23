# Registration, sibling updates and confirmation follow-up

This follow-up addresses the user's fresh-profile tests after commit 312d747.

## Confirmed causes

- Two unconfigured Gallery placeholder rows both became Gallery submenus: lookup compared an empty caption against an already substituted Gallery label. Captions are now normalized before grouping and unconfigured placeholders are not enumerated as galleries. Configured groups and their individual tiles remain separate.
- A stale source pin used an operation notification while a stale Gallery pin stopped in an early command-line MessageBox. Both now use the same missing-pin wording and selected desktop notification backend. A bounded notice-only request forwards Gallery launch errors to the resident for classic Balloon delivery. Invalid configuration is reported separately from an absent tile; diagnostic detail stays in the log.
- The packaged resident wrote Registration packages into Windows' virtualized AppData storage, then gave deployment a logical manifest path that did not exist outside the package. The deployed path now comes from the opened file handle, checked against the resolved owned directory. Identity, authorization and receipts remain bound to the original logical target. See [the path repair evidence](registration-delivery-repair-2026-10-23.md).
- A failed first Registration frame aborted the whole generation batch. The actual host still had package identity; Clock, Calendar and RSS were starved before their destinations were attempted. Failed destinations now stop only their remaining queue slots; unrelated destinations continue. Explicit shutdown still stops the batch, and a failed queue is not marked fully delivered.

## Confirmations

Delivery migrations and Gallery shrink confirmations use modern Yes/No toast buttons when the configured notification backend supports them. The resident retains the toast/event subscriptions, posts answers to the UI thread, and accepts only a random, one-use, profile-bound token. A request expires after five minutes. A popup merely fading does not count as cancellation or approval; the Action Center buttons may still be used during that interval. Body clicks, No, unknown/replayed/expired replies and replies after shutdown never approve a change. Profile and selected configuration are rechecked when Yes is received and again at commit where applicable.

Application > Notifications exposes Use Yes/No notifications for confirmations when available. Its INI/CLI key is Settings.NotificationConfirmations (default 1). The Yes/No labels are Strings.NotificationConfirmationYes and Strings.NotificationConfirmationNo. Classic balloons have no inline decision buttons; notifications disabled/unavailable or this preference off uses the ordinary explicit confirmation dialog. This is a capability fallback, not silent consent. Windows' own Pin to Start prompt is controlled by Windows.

This implementation is deliberately session-bound. It does not add a COM activator, protocol registration, scheduled task or new resident process. Exiting clears pending actions and hides their toasts. The existing small Registration launcher remains transient; adding another permanent resident would not fix the demonstrated physical-path error or batch-abort bug.

## Validation boundary

The portable confirmation model passed 324 checks and the notification adapter passed 1,045 checks with native transports replaced. The native WinRT adapter compiled. Final extended host checks passed 489, the combined new-source/confirmation/batch/menu suite passed 221, no-hardware checks passed 3, and source guards passed 1,937. The separate content smoke passed against the final optimized product. Fresh executables were scanned before running. No renderer/font/tolerance changes are part of this fix; the previously recorded legacy four-pixel raster defect remains open and was not rerun or claimed repaired by this unrelated follow-up.

The user removed the extra DesktopStub Start pins during testing and then explicitly requested another complete settings reset. Installation archives the current INI/account/gallery state and restores the fresh Desktop-only factory configuration. The cleared Start layout must remain clear; installation does not automatically repin tiles.

## Installed result

The final build is installed. The explicit reset archived the current profile, account/history/alert sidecars, click metadata when present, old three binaries and main manifest under DesktopStub/build/SettingsResetBackup-Delivery-20261023-085739-b7d6f3 with a hash receipt. The earlier October 23 backup remains available as well. Only the captured profile sidecars were reset; photographs, Windows package identities and the user's cleared Start layout were retained. No automatic pinning was invoked.

The existing main package activated canonical PID 32216. It retained package identity, stayed on the same PID for 75 seconds and successfully submitted the fresh primary Desktop wallpaper notification in a one-frame successful batch. No old source/gallery generation or deployment-path failure appeared in the health window. Since the requested reset removed Registration-mode configuration, this health check does not claim a real separate Registration deployment or an actual Yes/No notification click; those remain native acceptance items for fresh testing. The physical-path fix, mixed destination isolation and asynchronous continuations passed their injected host checks.

Final SHA-256 values:

| Artifact | SHA-256 |
| --- | --- |
| DesktopStub.exe | 81FF0C5893606386A30741D8A5ED9A6C64CD8BE2B4BEFF3978EAC632EC4C2170 |
| DesktopStubLiveTileBroker.exe | 36AFF4A5E25E2A5E79ED44B7FF27787F129AB42DCDA40319988A7551A8286A97 |
| DesktopStubTileLauncher.exe | 6D1CD3381AEAFDCD3AE4D5DD1F552967333BB356EBDA368912C4E807A1E96EE7 |

The fresh INI before normal runtime state writes was 5FB1A2E525853862001FC32E498BD463A27D188CCE56A23AB8F32B68B6321211. Evidence is under build/review-validation: delivery-followup-install.log, delivery-followup-revision-tests.log, delivery-followup-newsource-tests.log, delivery-followup-nohardware-tests.log, delivery-followup-sourcecheck-settled-tests.log and delivery-followup-final-content-smoke.log. The ignored guarded installer passed eight private backup/restore sentinels before use. It scans and binds installed artifacts, uses a verified atomic profile replacement, and retains immediate/deferred recovery if graceful shutdown fails.

Primary references: [toast Activated event](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.toastnotification.activated), [notification buttons](https://learn.microsoft.com/en-us/windows/apps/develop/notifications/app-notifications/app-notifications-content), [dismissal reasons](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.toastdismissalreason), [durable desktop activation](https://learn.microsoft.com/en-us/windows/apps/design/shell/tiles-and-notifications/send-local-toast-desktop-cpp-wrl).
