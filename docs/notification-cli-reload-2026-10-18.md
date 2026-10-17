# Notification-policy CLI/resident reload repair

Updated 2026-10-18. Scope: shared managed configuration tray and the two active
notification-policy apps. DesktopStub native source is unchanged by this task.

## Confirmed problem

AllowContentAboveLock and YourPhoneHideBanner saved a second invocation's
`--disable` or other live setting, then returned from the duplicate-tray mutex
path without notifying the original watcher. Tray edits applied immediately,
but successful CLI edits left the original policy running until manual reload.

## Implementation on disk, not yet validated

- The shared tray has an opt-in reload endpoint for products that declare a
  resident reload callback. Utility configuration trays have no receiver.
- Pipe identity uses the same per-user/executable/profile scope as the tray
  mutex. One connection carries one request and one acknowledgment; settings,
  credentials, and arbitrary commands are not transmitted.
- The owner dispatches reload onto its UI thread and acknowledges only after
  the callback returns. Failure and timeout are nonzero CLI results and state
  explicitly that the INI was already saved. No rollback of persisted intent
  is claimed if a runtime operation fails or exceeds the caller's deadline.
- Requests have a total deadline of at most five seconds. A queued callback
  expires/cancels before applying late changes. A reload already in progress
  can finish after a timeout; the CLI reports uncertainty, not false success.
- Offline `--configure-only`/inspection and ordinary `--tray` invocation do not
  request live reload. Startup-only commands retain their behavior. A combined
  Startup and live-setting edit notifies an existing owner without starting a
  new watcher merely for Startup management.
- Receiver shutdown cancels pending pipe work and queued UI dispatch, disposes
  its current connection, and bounds the listener join. It is a background
  thread and cannot keep the process alive.

## Validation boundary

No builds or runs are authorized until the parent coordinates validation.
Planned fixtures use separate tray specifications, synthetic resident callbacks,
private pipe/mutex identities and temporary INI files. No actual tray, watcher,
notification registry, service, hardware, or Startup integration will run.

## Source freeze for parent-coordinated validation

The two production files and `tools/RegistryNotificationAppTests.cs` are now
edited. The fixture uses independent owner/second-invocation specifications and
real private named pipes, while the resident runtime and Startup platform are
fake. It exercises ordinary duplicate-tray compatibility, both products' live
disable/enable/logging changes, offline passivity, negative acknowledgment,
retry, profile isolation, expired dispatch, incomplete-peer disposal, queued
callback disposal, and absence of a receiver.

Compile only, from the repository root:

```cmd
tools\BuildRegistryNotificationAppTests.cmd
```

After the parent's required scan of the resulting fixture, execute only:

```cmd
build\review-validation\RegistryNotificationAppTests.exe
```

The fixture contains a 45-second watchdog. The broader
`TestRegistryNotificationServices.cmd` additionally invokes native service-mode
checks and is not needed for this bounded repair. No compile/run was performed
by this task before the parent validation gate. `git diff --check` passed for
the three edited code/test files.

## Parent validation

Fresh warnings-as-errors compilation passed. Defender scanned the fixture clean
before execution; all 89 notification-app checks passed. The shared managed
configuration fixtures were rebuilt, and the scanned offscreen editor passed
all 33 checks. Fresh notification service source guards passed 56 checks; the
shared baseline source guards passed 277. The fixed service/registry app code
was compiled as part of the injected fixture, but no real registry watcher,
policy application, service operation, tray or Startup change was executed.
Existing running copies of these two products were not replaced or launched.
