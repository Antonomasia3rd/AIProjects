# CapsBlink hardware activation and automatic targets, 2026-10-14

The reported unwanted tile text and sleep failure had separate causes. Hardware
activation previously depended on selecting the CapsBlink text source. The
provider also retained a matching configuration key after its worker faulted,
so that key could suppress reopening after a stale device handle failed.
DesktopStub's root integration now separates the hardware preference from text
selection and resets/retries the controller after resume/device changes.

`CapsBlink.KeyboardTargetPath=Auto` is the new missing-setting default. Existing
explicit `\Device\KeyboardClassN` preferences remain supported. Auto discovers
currently present keyboard class-driver interfaces, then opens all accessible
endpoints. It does not guess device numbers or stop after class zero. Microsoft
documents that the system keyboard class driver registers
[GUID_DEVINTERFACE_KEYBOARD](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/guid-devinterface-keyboard)
and supports its keyboard I/O interface. Discovery uses
[SetupDiGetClassDevsW](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdigetclassdevsw)
with present/interface flags and
[SetupDiGetDeviceInterfaceDetailW](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdigetdeviceinterfacedetailw)
for device paths.

The shared portable GroupBackend de-duplicates endpoints and keeps successful
opens even when another keyboard is unavailable. It reports partial availability
through `Snapshot.warning`. Caps-only writes preserve each keyboard's own unit
identifier, Num Lock and Scroll Lock bits. Logical Caps Lock on keeps all opened
Caps indicators on; logical Caps Lock off blinks their indicators together.
Cleanup restores logical Caps state, or each device's original Caps bit if the
logical state cannot be read. It never synthesizes key presses or changes the
logical Caps Lock state.

Discovery and native operations remain on the controller's owner worker.
Construction is inert. Pending canceled IOCTLs retain endpoint ownership and
kernel-visible storage until completion. A hung driver can therefore delay
final cleanup, as in the existing single-device implementation. Native access
restrictions and driver LED support still apply; Auto does not elevate or bypass
them. Reopening re-enumerates present endpoints, allowing resume/hotplug hooks
to replace stale handles.

The inert CapsBlinkEngineTests additions cover Auto normalization/defaults,
constructor inactivity, duplicate discovery, partial acquisition, per-device
unit/other-LED preservation, synchronized Caps phase, logical-on fan-out,
original-state restoration, changed-device discovery on reopen, pending-I/O
ownership, and explicit numbered-target compatibility. Existing pattern,
single-device cleanup and late-completion tests remain unchanged. This subtask
did not build or execute tests and did not discover/open actual keyboards or
change their LEDs. Root owns build, scan, runtime integration and any explicitly
requested local acceptance.
