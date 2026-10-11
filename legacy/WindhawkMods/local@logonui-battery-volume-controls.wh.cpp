// ==WindhawkMod==
// @id              logonui-battery-volume-controls
// @name            LogonUI Battery & Volume Controls
// @description     Adds configurable Battery and Volume controls to Windows sign-in and security screens
// @version         1.0.0
// @author          Antonomasia
// @github          https://github.com/Antonomasia3rd
// @include         logonui.exe
// @architecture    amd64
// @compilerOptions -ladvapi32 -lole32 -loleaut32 -lruntimeobject -lgdi32
// @license         MIT
// ==/WindhawkMod==


// ==WindhawkModReadme==
/*
# LogonUI Battery & Volume Controls

Adds Battery and Volume controls to the Windows sign-in/security system-button
strip while keeping the underlying behavior as close to Windows itself as
practical.

## Features

- Live Battery icon using Windows-owned LogonUX battery state.
- `Automatic` Battery visibility follows Windows' native
  `IsBatteryStatusVisible` decision; Always show and Hide are also available.
- Genuine classic Windows battery flyout.
- Live master-volume icon using event-driven Core Audio notifications.
- Genuine `SndVol.exe -f` compact volume flyout and classic Volume Mixer.
- Mouse-wheel and precision-touchpad two-finger scrolling over the Volume
  button to adjust master volume.
- Mouse-wheel and precision-touchpad two-finger scrolling over the Battery
  button to adjust internal-display brightness. The WMI backend matches
  `WmiMonitorBrightness` and `WmiMonitorBrightnessMethods` by `InstanceName`
  and adjusts all active internal brightness-capable displays.
- Precision-touchpad / high-resolution wheel streams use a shared percentage
  step (1% by default) plus a separately configurable delta threshold
  (30 units by default) so fine 1% changes don't make scrolling feel sluggish.
- Per-control visibility for Windows-owned IME mode, Input/language,
  Network sign-in, Disconnect, Network, Accessibility, and Power controls.
- Optional Battery and Volume percentage labels with configurable font, size,
  position, spacing, and formatting.
- Configurable button order.
- Independent placement controls for the Battery flyout, compact SndVol,
  Volume Mixer, and Advanced Power Options.
- Direct Advanced Power Options hosting for the classic battery flyout on the
  secure desktop.
- Hot-disable teardown detaches persistent XAML/Core Audio callbacks on the
  LogonUI UI thread and restores the native button strip before the mod DLL is
  unloaded. Process-local helper window classes are explicitly unregistered so
  a later hot-load cannot reuse a stale WNDPROC from an unloaded mod image.
- Optional shared file logging is useful for console/Secure Desktop sessions
  whose Windhawk output isn't visible to the interactive user. Its full path
  is configurable and defaults to
  `%PUBLIC%\Desktop\LogonUIBatteryVolumeControls.log`, alongside the default
  diagnostic XAML-capture directory.
- Settings are grouped into Battery, Volume, shared scrolling/percentage
  appearance, Windows-owned buttons, layout, and diagnostics. Saving settings
  requests a controlled mod reload so visual and behavioral changes apply
  consistently to an already-running LogonUI.

## Native behavior

The mod intentionally does **not** synthesize click-toggle behavior.

- Battery click opens the genuine classic Windows battery flyout.
- Volume click launches `SndVol.exe` with the configured parameters.

This preserves Windows-version-specific SndVol behavior. For example, newer
Windows versions can open the compact `-f` flyout while the full Volume Mixer
is already open, while older versions may instead focus the existing Mixer.

## Live updates and input

Normal operation does not poll continuously:

- Volume state uses Core Audio callbacks.
- Battery percentage/state uses Windows power notifications.
- Optional Battery flyout live refresh uses a one-shot debounce after an
  actual power notification.
- Wheel/touchpad volume adjustment reacts only to routed pointer-wheel events.

Traditional mouse-wheel detents use the per-control defaults: 2% for Volume
and 10% for Battery brightness. Precision-touchpad / high-resolution wheel
streams use a shared 1% step every 30 accumulated wheel-delta units by
default, which is roughly four 1% changes per traditional 120-unit detent of
finger travel. Windows doesn't expose a reliable "this wheel event came from
a touchpad" flag here, so the mod classifies high-resolution streams from
their wheel-delta pattern.

Rapid WMI brightness gestures keep a short-lived desired-brightness cache
per active internal display so multiple fine scroll packets build on the last
requested value instead of a possibly stale WMI read. The monitor/method
instances are paired by `InstanceName`, and the cache automatically
resynchronizes after an idle pause.

The button-order parser is defensive: duplicate/unknown tokens are ignored and
an accidental leading `buttonOrder:` label is tolerated rather than silently
moving the first valid control to the end.

## Compatibility

The intended support floor is **Windows 10 1809 (17763) and newer**. Older
Windows releases use substantially different LogonUX/XAML implementations and
aren't supported by the structural SystemProvider resolver.

The mod has no Windows-build allowlist. Known builds can use verified fast
paths. Unknown builds must pass structural validation or symbol resolution;
otherwise the affected internal feature fails closed instead of guessing an
address.

**End-to-end runtime-tested builds:**

- Windows 10 1809 / build 17763.2114
- Windows 10 22H2 / build 19045.7725
- Windows 11 25H2 / build 26200.6584
- Windows 11 25H2 / build 26200.9445

The final hot-load/hot-unload lifecycle was additionally stress-tested on
Windows 10 22H2 with a persistent LogonUI process, including rapid repeated
enable/disable cycles and the language/IME selector's nested UI state.

**Offline structurally validated implementation families:**

- 17763
- 18362
- 19041
- 22000
- 22621
- 26100

For the classic Battery flyout, unknown `stobject.dll` builds first use a
symbol-free structural resolver that recognizes Windows' own BatMeterFlyout,
BATTERY_STATE, and Power Options relationships; symbols are only a fallback if
that validation fails. The SystemProvider resolver validates a unique x64
runtime-function start and implementation shape before hooking.


## Notes

- The percentage text is an intentional non-native addition. It uses `Segoe UI`
  by default and can be changed in Settings.
- `Automatic` Volume renderer follows the native generation where possible and
  can fall back to Windows SndVol resources.
- The optional event-driven classic Battery flyout refresh rebuilds the genuine
  flyout after a power event. On some versions this can produce a brief visual
  refresh/flicker.
- Battery brightness scrolling prefers the Windows `ROOT\\WMI`
  `WmiMonitorBrightness` / `WmiMonitorBrightnessMethods` interface, pairs
  active value/method instances by `InstanceName`, and adjusts every matched
  internal brightness-capable display. It uses WMI's Automation-compatible
  integer types for method parameters, with the legacy `\\.\LCD` backlight
  IOCTL path as a fallback when WMI isn't usable.
- Windows 11 can keep the Input/language button visually empty until a
  credential text box is activated. The mod identifies the containing
  `LogonUX.InputSwitchUserControl` up front, so visibility/order settings do not
  depend on the inner button having been lazy-loaded yet. Dormant IME/Input
  slots collapse instead of leaving blank gaps when reordered. External spacing
  before a reordered Input/standalone-IME control is configurable and defaults
  to Automatic, derived from the native Power button's left margin so Windows
  10 and Windows 11 keep their own generation-specific spacing. An adjacent
  IME+Input pair is treated as one group, and the narrow IME-mode glyph has a
  separate visual-only X offset so it can be tightened without shrinking the
  native hit target.
- Windows 11 exposes the IME mode indicator as a separate Windows-owned
  `LogonUX.ImeModeIndicatorUserControl` with its own visibility/order token.
  The tested Windows 10 1809 and Windows 10 22H2 layouts have no separate
  IME-mode container; their language/IME state is represented by
  `LogonUX.InputSwitchUserControl` alone.
- On the tested Windows 10/11 layouts, the two auxiliary direct-button slots
  immediately before Network are exposed as `network-signin` and `disconnect`.
  Unknown extra native slots remain available as `native1`, `native2`, etc.
- Diagnostic logging is disabled by default. Normal logging keeps resolver,
  backend, layout, and error information concise. `Verbose diagnostics`
  enables the large native-button/XAML-tree traces used for reverse
  engineering and can write through either Windhawk logging or shared file
  logging. Shared-file logging defaults to
  `%PUBLIC%\Desktop\LogonUIBatteryVolumeControls.log`; its path is
  configurable and the file isn't automatically rotated. Diagnostic XAML
  capture is independent, captures only the SystemProvider subtree, and
  defaults to `%PUBLIC%\Desktop`; its output directory is configurable.
- Transient `SystemProvider.Unloaded` events are treated as non-terminal:
  the injection session and its callbacks are retained and revalidated on the
  matching `Loaded` event. Callbacks are detached when a session is actually
  retired or during hot-unload. Duplicate PowerOptions lazy-load retries for
  the same SystemProvider instance are coalesced.
- Hot-load/reload recovery uses both queued and sent HWND messages plus
  GETMESSAGE/CALLWNDPROC rendezvous hooks, with a unique probe cookie for each
  attempt. It fans probes out to active/focused/fallback HWNDs and the thread
  queue, then performs bounded delayed rescans if a transient nested UI (such
  as the language/IME selector) still delays the first probe.
- Native Windows controls are never removed/reinserted merely to implement
  keyboard traversal. Visual order uses Grid.Column and keyboard order uses
  TabIndex, avoiding native Unloaded/Loaded side effects during hot-load and
  hot-unload.
- Saving settings requests a controlled Windhawk reload of the mod. This uses
  the same hot-unload-safe teardown path and ensures button visibility, order,
  renderer, placement, diagnostics, and interaction settings all take effect
  immediately on an already-running LogonUI.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- battery:
  - visibility: automatic
    $name: Battery button visibility
    $description: >-
      Automatic follows Windows' native LogonUX battery availability. Always
      show keeps the Battery control visible even when Windows reports no
      battery. Hide removes the injected Battery control entirely.
    $options:
    - automatic: Automatic (follow Windows)
    - show: Always show
    - hide: Hide
  - clickAction: open
    $name: Click action
    $description: >-
      Open the genuine classic Windows battery flyout, or disable clicking.
      No synthetic toggle behavior is applied.
    $options:
    - open: Open classic battery flyout
    - none: Do nothing
  - brightnessScroll:
    - action: adjust
      $name: Wheel / touchpad scroll action
      $description: >-
        Adjust brightness from a physical mouse wheel or precision-touchpad
        two-finger vertical scroll while the pointer is over the Battery
        button. The WMI backend adjusts all active internal brightness-capable
        displays by matching their InstanceName values; the legacy LCD backend
        remains a fallback.
      $options:
      - none: Do nothing
      - adjust: Adjust display brightness
    - stepPercent: 10
      $name: Brightness step per mouse-wheel notch (%)
    - reverse: false
      $name: Reverse brightness scroll direction
    $name: Brightness scrolling
  - percentage:
    - show: true
      $name: Show Battery percentage beside icon
    - position: right
      $name: Percentage position
      $options:
      - right: Right of icon
      - left: Left of icon
    - hideAt100: false
      $name: Hide Battery percentage at 100%
    $name: Percentage
  - flyout:
    - placement: button
      $name: Classic battery flyout placement
      $options:
      - button: Anchor to Battery button
      - windows: Windows default
    - anchorSide: automatic
      $name: Anchor side
      $options:
      - automatic: Automatic (prefer above)
      - above: Above
      - below: Below
    - offsetX: 0
      $name: X offset (screen px)
    - offsetY: 0
      $name: Y offset (screen px)
    - refresh: windows
      $name: Live refresh
      $options:
      - windows: Windows default (refresh on open only)
      - event-driven: Event-driven rebuild while open
    - eventDebounceDelayMs: 500
      $name: Event debounce delay (ms)
      $description: One-shot debounce after a Windows power notification. This is not a polling interval.
    $name: Classic battery flyout
  - advancedPower:
    - action: advanced
      $name: '"More power options" action'
      $options:
      - advanced: Open Advanced Power Options directly
      - windows: Windows default
    - placement: battery
      $name: Advanced Power Options placement
      $options:
      - battery: Anchor to Battery button
      - windows: Windows default
    - anchorSide: automatic
      $name: Anchor side
      $options:
      - automatic: Automatic (prefer above)
      - above: Above
      - below: Below
    - offsetX: 0
      $name: X offset (screen px)
    - offsetY: 0
      $name: Y offset (screen px)
    $name: Advanced Power Options
  $name: Battery

- volume:
  - showButton: true
    $name: Show Volume button
  - clickAction: launch
    $name: Click action
    $description: >-
      Launch SndVol using the configured parameters, or disable clicking.
      SndVol itself decides what happens when a full Volume Mixer already
      exists, preserving version-specific Windows behavior.
    $options:
    - launch: Launch SndVol
    - none: Do nothing
  - scroll:
    - action: adjust
      $name: Wheel / touchpad scroll action
      $description: >-
        Adjust master volume from a physical mouse wheel or precision-touchpad
        two-finger scroll while the pointer is over the Volume button.
      $options:
      - none: Do nothing
      - adjust: Adjust master volume
    - stepPercent: 2
      $name: Volume step per mouse-wheel notch (%)
    - unmute: false
      $name: Unmute when wheel changes volume
    - reverse: false
      $name: Reverse Volume scroll direction
    $name: Volume scrolling
  - percentage:
    - show: true
      $name: Show Volume percentage beside icon
    - position: right
      $name: Percentage position
      $options:
      - right: Right of icon
      - left: Left of icon
    - hideAt100: false
      $name: Hide Volume percentage at 100%
    - showMutedText: false
      $name: Append "(muted)" to Volume percentage
    $name: Percentage
  - renderer: automatic
    $name: Volume icon renderer
    $options:
    - automatic: Automatic (recommended)
    - mdl2: Segoe MDL2 Assets
    - fluent: Segoe Fluent Icons
    - sndvol-auto: SndVolSSO automatic resource lookup
    - sndvol-dll: SndVolSSO.dll resources
    - sndvol-mun: SndVolSSO.dll.mun resources
  - uiHandoff: automatic
    $name: Volume live-update UI handoff
    $options:
    - automatic: Automatic (runtime capability detection)
    - dispatcherqueue: Windows.System.DispatcherQueue
    - coredispatcher: Windows.UI.Core.CoreDispatcher
    - legacy-message-window: Legacy message-only window
  - sndVol:
    - launchParameters: -f
      $name: SndVol launch parameters
    - compact:
      - placement: button
        $name: Compact flyout placement
        $options:
        - button: Anchor to Volume button
        - windows: Windows default
      - anchorSide: automatic
        $name: Anchor side
        $options:
        - automatic: Automatic (prefer above)
        - above: Above
        - below: Below
      - offsetX: 0
        $name: X offset (screen px)
      - offsetY: 0
        $name: Y offset (screen px)
      $name: Compact SndVol flyout
    - mixer:
      - placement: button
        $name: Full Volume Mixer placement
        $options:
        - button: Anchor to Volume button
        - windows: Windows default
      - anchorSide: automatic
        $name: Anchor side
        $options:
        - automatic: Automatic (prefer above)
        - above: Above
        - below: Below
      - offsetX: 0
        $name: X offset (screen px)
      - offsetY: 0
        $name: Y offset (screen px)
      $name: Full Volume Mixer
    $name: SndVol
  $name: Volume

- precisionScrolling:
  - stepPercent: 1
    $name: Precision touchpad / high-resolution scroll step (%)
    $description: >-
      Shared percentage step for Battery and Volume when the routed wheel
      stream behaves like a precision touchpad or high-resolution wheel.
      Traditional mouse-wheel detents keep each control's own step setting.
  - deltaPerStep: 30
    $name: Precision touchpad / high-resolution delta per step
    $description: >-
      Accumulated wheel-delta units required for each precision-scroll
      percentage step. Lower values are faster/more sensitive; higher values
      are slower. Traditional mouse-wheel detents always use the standard
      120-unit detent and are unaffected by this setting.
  $name: Precision scrolling (shared)

- percentageLabels:
  - showPercentSign: true
    $name: Show percent sign
  - fontFamily: Segoe UI
    $name: Percentage font family
    $description: Windows font-family name used by both percentage labels.
  - fontSize: 12
    $name: Percentage text size
  - spacing: 4
    $name: Icon/percentage spacing (DIP)
  $name: Percentage label appearance (shared)

- nativeButtons:
  - showImeMode: true
    $name: Show IME mode indicator
    $description: >-
      Controls Windows' separate IME-mode indicator container. It may remain
      visually empty unless the active input method exposes an IME mode.
  - showInput: true
    $name: Show Input / language button
    $description: >-
      Controls Windows' InputSwitchUserControl (for example ENG / US). On
      Windows 11 the inner language button can be lazy-loaded only after a
      credential text box is activated; this setting still targets the
      container before that.
  - showNetworkSignIn: true
    $name: Show Network sign-in button
    $description: >-
      Controls Windows' optional Network sign-in system button. It is normally
      hidden unless the current sign-in/network scenario needs it.
  - showDisconnect: true
    $name: Show Disconnect button
    $description: >-
      Controls Windows' optional Disconnect system button. It is normally
      hidden unless the current sign-in/network scenario needs it.
  - showNetwork: true
    $name: Show Network button
  - showAccessibility: true
    $name: Show Accessibility button
  - showPower: true
    $name: Show Power button
  - showOther: true
    $name: Show other Windows-owned system buttons
    $description: >-
      Controls any additional native slots that this mod can't identify as
      IME, Input, Network sign-in, Disconnect, Network, Accessibility, or
      Power.
  - imeInputSpacing: -1
    $name: Reordered IME / Input spacing (DIP)
    $description: >-
      Extra left spacing used when a visible Input container (or a standalone
      IME container) follows a different visible system control. -1
      automatically follows the native generation's system-button spacing
      (for example 8 DIP on the tested Windows 11 layout and 0 DIP on Windows
      10 1809). Values 0..32 force an explicit spacing. A visible adjacent
      IME + Input pair is treated as one native group.
  - imeModeVisualOffsetX: -4
    $name: IME mode glyph X offset (DIP)
    $description: >-
      Visual-only horizontal offset for the native IME mode glyph. Negative
      values move the glyph left without shrinking or moving its native hit
      target.
  $name: Windows-owned system buttons

- layout:
  - buttonOrder: ime,input,battery,volume,network-signin,disconnect,network,accessibility,power
    $name: System button order (left to right)
    $description: >-
      Comma-separated order. Named tokens are ime, input, battery, volume,
      network-signin, disconnect, network, accessibility, power, plus
      native1/native2/... when an unknown native slot is discovered.
  $name: System strip layout

- diagnostics:
  - windhawkLogging: false
    $name: Windhawk diagnostic logging
    $description: >-
      Enable concise resolver, backend, layout, and error logging in Windhawk.
  - fileLogging: false
    $name: Write diagnostics to a shared file
    $description: >-
      Also append diagnostic messages to the configured log path. This is
      useful for console/Secure Desktop LogonUI sessions whose Windhawk output
      isn't visible in the current user's UI. The file is not automatically
      rotated, so delete or archive it if it becomes large.
  - fileLogPath: '%PUBLIC%\Desktop\LogonUIBatteryVolumeControls.log'
    $name: Diagnostic log file path
    $description: >-
      Full path for shared file logging. Environment variables such as
      %PUBLIC% are expanded. The parent directory must already exist.
  - verbose: false
    $name: Verbose reverse-engineering diagnostics
    $description: >-
      Enable large native-button semantic dumps and dynamic XAML-tree traces.
      Normally leave this off. It is independent of screenshot capture, but
      requires either Windhawk logging or file logging for text output.
  - xamlCapture: false
    $name: Diagnostic XAML screenshot capture
    $description: >-
      Capture only the LogonUI SystemProvider subtree for troubleshooting and
      reverse engineering. Disabled by default.
  - xamlCaptureDirectory: '%PUBLIC%\Desktop'
    $name: XAML capture directory
    $description: >-
      Directory for diagnostic BMP captures. Environment variables such as
      %PUBLIC% are expanded. The directory must already exist.
  $name: Diagnostics
*/
// ==/WindhawkModSettings==

// Design notes:
// - No Windows build/version allowlist. Tested builds use verified RVAs only as
//   fast paths. Unknown stobject.dll builds use a symbol-free structural Battery
//   resolver first, then fall back to runtime symbols if structural validation
//   fails.
// - Structural ABI/layout checks remain fail-closed to avoid mutating an
//   unexpected LogonUI tree.
// - Battery uses Windows-owned LogonUX state and the genuine stobject/BatMeter
//   classic flyout. Optional live refresh rebuilds through ShowBatteryUI rather
//   than re-entering stobject's low-level UI population routine. Volume uses event-driven Core Audio state and native SndVol.
// - Placement, click/wheel behavior, injected/native-button visibility,
 //   dynamic percentage labels, renderer choice, live-update handoff, and
 //   concise/verbose diagnostics are independently configurable.
// - Battery and Volume clicks deliberately don't synthesize toggle behavior.
//   Battery always invokes the genuine classic Windows flyout path; Volume
//   directly launches SndVol so Windows keeps its own version-specific
//   existing-Mixer behavior.
// - Detailed behavior and compatibility notes are documented above and in
//   the companion README.

#include <windows.h>
#include <dbt.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <objidl.h>
#include <propsys.h>
#include <wrl.h>
#include <winternl.h>
#include <winioctl.h>
#include <wbemidl.h>
#include <windhawk_utils.h>

#include <algorithm>
#include <limits>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <cstddef>
#include <cmath>
#include <memory>
#include <string>
#include <new>
#include <string_view>
#include <vector>

#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/base.h>

using namespace winrt;
using namespace winrt::Windows::UI::Xaml;
namespace Automation = winrt::Windows::UI::Xaml::Automation;
namespace Controls = winrt::Windows::UI::Xaml::Controls;
namespace Data = winrt::Windows::UI::Xaml::Data;
namespace Input = winrt::Windows::UI::Xaml::Input;
namespace Media = winrt::Windows::UI::Xaml::Media;

namespace {

constexpr wchar_t kTargetModule[] = L"Windows.UI.Logon.dll";
constexpr wchar_t kInjectedBatteryName[] = L"WindhawkSignInBatteryVisual";
constexpr wchar_t kInjectedVolumeName[] = L"WindhawkSignInVolumeVisual";

#ifndef FILE_DEVICE_VIDEO
#define FILE_DEVICE_VIDEO 0x00000023
#endif

#ifndef IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS
#define IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x125, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x126, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x127, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

struct DisplayBrightnessIoctl {
    UCHAR displayPolicy;
    UCHAR acBrightness;
    UCHAR dcBrightness;
};

constexpr UCHAR kDisplayPolicyAc = 0x01;
constexpr UCHAR kDisplayPolicyDc = 0x02;
constexpr UCHAR kDisplayPolicyBoth = 0x03;

HANDLE g_stopEvent = nullptr;
HANDLE g_hookThread = nullptr;
std::atomic<bool> g_hookInstalled{false};
std::atomic<bool> g_captureScheduled{false};
std::atomic<unsigned> g_captureAsyncInFlight{0};
std::atomic<bool> g_lateAttachPending{false};
std::atomic<HHOOK> g_lateAttachHook{nullptr};
std::atomic<HHOOK> g_lateAttachCallWndHook{nullptr};
UINT g_lateAttachMessage = 0;
HANDLE g_lateAttachCompletedEvent = nullptr;
std::atomic<bool> g_lateAttachProbeMatched{false};
std::atomic<DWORD> g_lateAttachProbeThreadId{0};
std::atomic<ULONG_PTR> g_lateAttachProbeCookie{0};
std::atomic<ULONG_PTR> g_lateAttachCookieCounter{1};

// Hot-unload safety. Windhawk calls Wh_ModBeforeUninit while this DLL and its
// callbacks are still executable. Use that phase to detach every callback
// owned by persistent LogonUI XAML objects before the DLL can disappear.
std::atomic<bool> g_unloading{false};
std::atomic<bool> g_hotCleanupPending{false};
std::atomic<HHOOK> g_hotCleanupHook{nullptr};
std::atomic<HHOOK> g_hotCleanupCallWndHook{nullptr};
UINT g_hotCleanupMessage = 0;
std::atomic<DWORD> g_hotCleanupProbeThreadId{0};
std::atomic<ULONG_PTR> g_hotCleanupProbeCookie{0};
std::atomic<ULONG_PTR> g_hotCleanupCookieCounter{1};
HANDLE g_hotCleanupCompletedEvent = nullptr;
HANDLE g_hotCleanupFenceEvent = nullptr;
std::atomic<int> g_hotCleanupFenceCountdown{0};
std::atomic<unsigned> g_audioCallbackObjectsAlive{0};
std::atomic<unsigned> g_audioCallbacksInFlight{0};
// Last thread positively validated as hosting LogonUX.SystemProviderUserControl.
// Keep this across transient Unloaded events; LogonUI can have more than one
// XAML thread/window over the lifetime of the process.
std::atomic<DWORD> g_xamlUiThreadId{0};
SRWLOCK g_knownXamlThreadsLock = SRWLOCK_INIT;
std::vector<DWORD> g_knownXamlThreads;

void RememberXamlThread(DWORD threadId, bool hostsSystemProvider) {
    if (!threadId) return;

    AcquireSRWLockExclusive(&g_knownXamlThreadsLock);
    if (std::find(g_knownXamlThreads.begin(), g_knownXamlThreads.end(),
                  threadId) == g_knownXamlThreads.end()) {
        g_knownXamlThreads.push_back(threadId);
    }
    ReleaseSRWLockExclusive(&g_knownXamlThreadsLock);

    if (hostsSystemProvider) {
        g_xamlUiThreadId.store(threadId, std::memory_order_release);
    }
}

std::vector<DWORD> SnapshotKnownXamlThreads() {
    std::vector<DWORD> result;
    AcquireSRWLockShared(&g_knownXamlThreadsLock);
    result = g_knownXamlThreads;
    ReleaseSRWLockShared(&g_knownXamlThreadsLock);
    return result;
}

void ClearKnownXamlThreads() {
    AcquireSRWLockExclusive(&g_knownXamlThreadsLock);
    g_knownXamlThreads.clear();
    ReleaseSRWLockExclusive(&g_knownXamlThreadsLock);
    g_xamlUiThreadId.store(0, std::memory_order_release);
}

std::atomic<bool> g_enableLogging{false};
std::atomic<bool> g_enableFileLogging{false};
std::atomic<bool> g_enableVerboseDiagnostics{false};
std::atomic<bool> g_enableXamlCapture{false};

SRWLOCK g_layoutRetryLock = SRWLOCK_INIT;
std::vector<void*> g_layoutRetryOwners;
SRWLOCK g_fileLogLock = SRWLOCK_INIT;
std::wstring g_sharedDiagnosticLogPath =
    L"C:\\Users\\Public\\Desktop\\LogonUIBatteryVolumeControls.log";

void ModLogImpl(int sourceLine, const char* sourceFunction,
                const wchar_t* format, ...) {
    const bool windhawkLogging =
        g_enableLogging.load(std::memory_order_relaxed);
    const bool fileLogging =
        g_enableFileLogging.load(std::memory_order_relaxed);
    if (!windhawkLogging && !fileLogging) {
        return;
    }

    wchar_t message[2048]{};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
    va_end(args);

    if (windhawkLogging) {
        // Keep the original call site visible even though formatting is
        // centralized so the same message can also be written to a shared
        // cross-session file.
        Wh_Log(L"[src:%d:%S] %s", sourceLine,
               sourceFunction ? sourceFunction : "?", message);
    }

    if (!fileLogging) {
        return;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t line[2600]{};
    _snwprintf_s(
        line, _countof(line), _TRUNCATE,
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu [src:%d:%S] %s\r\n",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        GetCurrentProcessId(), GetCurrentThreadId(), sourceLine,
        sourceFunction ? sourceFunction : "?", message);

    const int utf8Length = WideCharToMultiByte(
        CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 1) {
        return;
    }
    std::string utf8(static_cast<size_t>(utf8Length - 1), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, line, -1, utf8.data(), utf8Length - 1,
        nullptr, nullptr);

    AcquireSRWLockExclusive(&g_fileLogLock);
    HANDLE file = INVALID_HANDLE_VALUE;
    if (!g_sharedDiagnosticLogPath.empty()) {
        file = CreateFileW(
            g_sharedDiagnosticLogPath.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    }
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()),
                  &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&g_fileLogLock);
}

#define ModLog(...) ModLogImpl(__LINE__, __FUNCTION__, __VA_ARGS__)

enum class VolumeRendererMode : int {
    Automatic = 0,
    SegoeMdl2,
    SegoeFluent,
    SndVolAutomatic,
    SndVolDll,
    SndVolMun,
};

enum class SndVolResourceSource : int {
    WindowsAutomatic = 0,
    DirectDll,
    DirectMun,
};

enum class VolumeUiHandoffMode : int {
    Automatic = 0,
    DispatcherQueue,
    CoreDispatcher,
    LegacyMessageWindow,
};

enum class BatteryClickAction : int {
    Open = 0,
    None,
};

enum class BatteryFlyoutRefreshMode : int {
    WindowsDefault = 0,
    EventDriven,
};

enum class VolumeClickAction : int {
    Launch = 0,
    None,
};

enum class BatteryFlyoutResolveState : int {
    NotStarted = 0,
    Resolving,
    Ready,
    Failed,
};

enum class BatteryFlyoutPlacementMode : int {
    AnchorToButton = 0,
    WindowsDefault,
};

enum class MorePowerOptionsAction : int {
    AdvancedPowerOptions = 0,
    WindowsDefault,
};

enum class AdvancedPowerPlacementMode : int {
    AnchorToBatteryButton = 0,
    WindowsDefault,
};

enum class SndVolPlacementMode : int {
    AnchorToButton = 0,
    WindowsDefault,
};

enum class PercentagePosition : int {
    Right = 0,
    Left,
};

enum class AnchorSide : int {
    Automatic = 0,
    Above,
    Below,
};

enum class VolumeWheelAction : int {
    None = 0,
    Adjust,
};

enum class BatteryWheelAction : int {
    None = 0,
    AdjustBrightness,
};

std::atomic<VolumeRendererMode> g_volumeRenderer{
    VolumeRendererMode::Automatic};
std::atomic<VolumeUiHandoffMode> g_volumeUiHandoff{
    VolumeUiHandoffMode::Automatic};
std::atomic<bool> g_showBatteryButton{true};
std::atomic<bool> g_batteryVisibilityAutomatic{true};
std::atomic<bool> g_showVolumeButton{true};
std::atomic<bool> g_showImeModeButton{true};
std::atomic<bool> g_showInputButton{true};
std::atomic<bool> g_showNetworkSignInButton{true};
std::atomic<bool> g_showDisconnectButton{true};
std::atomic<bool> g_showNetworkButton{true};
std::atomic<bool> g_showAccessibilityButton{true};
std::atomic<bool> g_showPowerButton{true};
std::atomic<bool> g_showOtherNativeButtons{true};
std::atomic<int> g_nativeImeInputSpacing{-1};
std::atomic<int> g_imeModeVisualOffsetX{-4};
std::atomic<bool> g_showBatteryPercentage{true};
std::atomic<bool> g_showVolumePercentage{true};
std::atomic<PercentagePosition> g_batteryPercentagePosition{
    PercentagePosition::Right};
std::atomic<PercentagePosition> g_volumePercentagePosition{
    PercentagePosition::Right};
std::atomic<bool> g_hideBatteryPercentageAt100{false};
std::atomic<bool> g_hideVolumePercentageAt100{false};
std::atomic<bool> g_showMutedTextInVolumePercentage{false};
std::atomic<bool> g_percentageShowPercentSign{true};
std::atomic<int> g_percentageFontSize{12};
std::atomic<int> g_percentageSpacing{4};
std::atomic<BatteryClickAction> g_batteryClickAction{
    BatteryClickAction::Open};
std::atomic<BatteryFlyoutRefreshMode> g_batteryFlyoutRefresh{
    BatteryFlyoutRefreshMode::WindowsDefault};
std::atomic<DWORD> g_batteryEventDebounceDelayMs{500};
std::atomic<VolumeClickAction> g_volumeClickAction{
    VolumeClickAction::Launch};
std::atomic<VolumeWheelAction> g_volumeWheelAction{
    VolumeWheelAction::None};
std::atomic<int> g_volumeWheelStepPercent{2};
std::atomic<bool> g_volumeWheelUnmute{false};
std::atomic<bool> g_volumeWheelReverse{false};
std::atomic<BatteryWheelAction> g_batteryWheelAction{
    BatteryWheelAction::AdjustBrightness};
std::atomic<int> g_batteryWheelStepPercent{10};
std::atomic<bool> g_batteryWheelReverse{false};
std::atomic<int> g_precisionScrollStepPercent{1};
std::atomic<int> g_precisionScrollDeltaPerStep{30};
std::atomic<int> g_brightnessPendingPercent{0};
std::atomic<bool> g_brightnessWorkerStop{false};
HANDLE g_brightnessWorkerEvent = nullptr;
HANDLE g_brightnessWorkerThread = nullptr;
SRWLOCK g_brightnessWorkerLock = SRWLOCK_INIT;
std::atomic<BatteryFlyoutPlacementMode> g_batteryFlyoutPlacement{
    BatteryFlyoutPlacementMode::AnchorToButton};
std::atomic<AnchorSide> g_batteryFlyoutAnchorSide{
    AnchorSide::Automatic};
std::atomic<int> g_batteryFlyoutOffsetX{0};
std::atomic<int> g_batteryFlyoutOffsetY{0};
std::atomic<MorePowerOptionsAction> g_morePowerOptionsAction{
    MorePowerOptionsAction::AdvancedPowerOptions};
std::atomic<AdvancedPowerPlacementMode> g_advancedPowerPlacement{
    AdvancedPowerPlacementMode::AnchorToBatteryButton};
std::atomic<AnchorSide> g_advancedPowerAnchorSide{
    AnchorSide::Automatic};
std::atomic<int> g_advancedPowerOffsetX{0};
std::atomic<int> g_advancedPowerOffsetY{0};
std::atomic<SndVolPlacementMode> g_sndVolPlacement{
    SndVolPlacementMode::AnchorToButton};
std::atomic<AnchorSide> g_sndVolAnchorSide{
    AnchorSide::Automatic};
std::atomic<int> g_sndVolOffsetX{0};
std::atomic<int> g_sndVolOffsetY{0};
std::atomic<SndVolPlacementMode> g_sndVolMixerPlacement{
    SndVolPlacementMode::AnchorToButton};
std::atomic<AnchorSide> g_sndVolMixerAnchorSide{
    AnchorSide::Automatic};
std::atomic<int> g_sndVolMixerOffsetX{0};
std::atomic<int> g_sndVolMixerOffsetY{0};
std::atomic<void*> g_showBatteryUiTarget{nullptr};
std::atomic<void*> g_launchPowerCplTarget{nullptr};
std::atomic<void*> g_stobjectBatteryState{nullptr};
std::atomic<HMODULE> g_stobjectModule{nullptr};
std::atomic<HMODULE> g_batMeterModule{nullptr};
std::atomic<void*> g_queryBatteryDataTarget{nullptr};
std::atomic<void*> g_updateBatteryDataTarget{nullptr};
std::atomic<void*> g_cleanupBatteryDataTarget{nullptr};
std::atomic<BatteryFlyoutResolveState> g_batteryFlyoutResolveState{
    BatteryFlyoutResolveState::NotStarted};
HANDLE g_batteryResolverThread = nullptr;
HANDLE g_batteryNotificationThread = nullptr;
std::atomic<HWND> g_batteryUiBridgeWindow{nullptr};
winrt::weak_ref<Controls::TextBlock> g_batteryPercentText;
std::atomic<bool> g_launchPowerCplHookInstalled{false};
std::atomic<bool> g_advancedPowerDialogActive{false};
HANDLE g_advancedPowerThread = nullptr;
std::atomic<DWORD> g_osBuild{0};
SRWLOCK g_settingsLock = SRWLOCK_INIT;
SRWLOCK g_batteryAnchorLock = SRWLOCK_INIT;
RECT g_batteryAnchorRect{};
std::atomic<bool> g_batteryAnchorValid{false};
SRWLOCK g_sndVolAnchorLock = SRWLOCK_INIT;
RECT g_sndVolAnchorRect{};
std::atomic<bool> g_sndVolAnchorValid{false};
HANDLE g_sndVolWindowMonitorThread = nullptr;
std::wstring g_buttonOrder = L"ime,input,battery,volume,network-signin,disconnect,network,accessibility,power";
std::wstring g_sndVolLaunchParameters = L"-f";
std::wstring g_percentageFontFamily = L"Segoe UI";
std::wstring g_xamlCaptureDirectory = L"C:\\Users\\Public\\Desktop";

const wchar_t* VolumeRendererModeName(VolumeRendererMode mode) {
    switch (mode) {
        case VolumeRendererMode::Automatic:
            return L"Automatic";
        case VolumeRendererMode::SegoeMdl2:
            return L"Segoe MDL2 Assets";
        case VolumeRendererMode::SegoeFluent:
            return L"Segoe Fluent Icons";
        case VolumeRendererMode::SndVolAutomatic:
            return L"SndVolSSO (automatic Windows resource lookup)";
        case VolumeRendererMode::SndVolDll:
            return L"SndVolSSO.dll resources";
        case VolumeRendererMode::SndVolMun:
            return L"SndVolSSO.dll.mun resources";
    }
    return L"Unknown";
}

const wchar_t* VolumeUiHandoffModeName(VolumeUiHandoffMode mode) {
    switch (mode) {
        case VolumeUiHandoffMode::Automatic:
            return L"Automatic";
        case VolumeUiHandoffMode::DispatcherQueue:
            return L"DispatcherQueue";
        case VolumeUiHandoffMode::CoreDispatcher:
            return L"CoreDispatcher";
        case VolumeUiHandoffMode::LegacyMessageWindow:
            return L"Legacy message-only window";
    }
    return L"Unknown";
}

const wchar_t* BatteryClickActionName(BatteryClickAction action) {
    switch (action) {
        case BatteryClickAction::Open:
            return L"Open classic battery flyout";
        case BatteryClickAction::None:
            return L"Do nothing";
    }
    return L"Unknown";
}

const wchar_t* BatteryFlyoutRefreshModeName(
        BatteryFlyoutRefreshMode mode) {
    switch (mode) {
        case BatteryFlyoutRefreshMode::WindowsDefault:
            return L"Windows default";
        case BatteryFlyoutRefreshMode::EventDriven:
            return L"Event-driven";
    }
    return L"Unknown";
}

const wchar_t* VolumeClickActionName(VolumeClickAction action) {
    switch (action) {
        case VolumeClickAction::Launch:
            return L"Launch SndVol";
        case VolumeClickAction::None:
            return L"Do nothing";
    }
    return L"Unknown";
}

const wchar_t* PercentagePositionName(PercentagePosition position) {
    return position == PercentagePosition::Left ? L"Left" : L"Right";
}

const wchar_t* AnchorSideName(AnchorSide side) {
    switch (side) {
        case AnchorSide::Automatic:
            return L"Automatic";
        case AnchorSide::Above:
            return L"Above";
        case AnchorSide::Below:
            return L"Below";
    }
    return L"Unknown";
}

const wchar_t* VolumeWheelActionName(VolumeWheelAction action) {
    return action == VolumeWheelAction::Adjust
        ? L"Adjust master volume"
        : L"Do nothing";
}

const wchar_t* BatteryWheelActionName(BatteryWheelAction action) {
    return action == BatteryWheelAction::AdjustBrightness
        ? L"Adjust display brightness"
        : L"Do nothing";
}

AnchorSide ParseAnchorSide(PCWSTR value) {
    if (value) {
        if (_wcsicmp(value, L"above") == 0) {
            return AnchorSide::Above;
        }
        if (_wcsicmp(value, L"below") == 0) {
            return AnchorSide::Below;
        }
    }
    return AnchorSide::Automatic;
}

PercentagePosition ParsePercentagePosition(PCWSTR value) {
    return value && _wcsicmp(value, L"left") == 0
        ? PercentagePosition::Left
        : PercentagePosition::Right;
}

const wchar_t* BatteryFlyoutPlacementModeName(
        BatteryFlyoutPlacementMode mode) {
    switch (mode) {
        case BatteryFlyoutPlacementMode::AnchorToButton:
            return L"Anchor to Battery button";
        case BatteryFlyoutPlacementMode::WindowsDefault:
            return L"Windows default";
    }
    return L"Unknown";
}

const wchar_t* MorePowerOptionsActionName(
        MorePowerOptionsAction action) {
    switch (action) {
        case MorePowerOptionsAction::AdvancedPowerOptions:
            return L"Open Advanced Power Options directly";
        case MorePowerOptionsAction::WindowsDefault:
            return L"Windows default";
    }
    return L"Unknown";
}

const wchar_t* AdvancedPowerPlacementModeName(
        AdvancedPowerPlacementMode mode) {
    switch (mode) {
        case AdvancedPowerPlacementMode::AnchorToBatteryButton:
            return L"Anchor to Battery button";
        case AdvancedPowerPlacementMode::WindowsDefault:
            return L"Windows default";
    }
    return L"Unknown";
}

const wchar_t* SndVolPlacementModeName(
        SndVolPlacementMode mode) {
    switch (mode) {
        case SndVolPlacementMode::AnchorToButton:
            return L"Anchor to Volume button";
        case SndVolPlacementMode::WindowsDefault:
            return L"Windows default";
    }
    return L"Unknown";
}

std::wstring ExpandEnvironmentPathOrDefault(
        PCWSTR configuredValue,
        PCWSTR defaultValue) {
    std::wstring input =
        configuredValue && *configuredValue
            ? std::wstring(configuredValue)
            : std::wstring(defaultValue ? defaultValue : L"");

    if (input.empty()) {
        return {};
    }

    const DWORD required =
        ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (required == 0) {
        return input;
    }

    std::vector<wchar_t> buffer(required);
    const DWORD written = ExpandEnvironmentStringsW(
        input.c_str(), buffer.data(), required);
    if (written == 0 || written > required) {
        return input;
    }

    return std::wstring(buffer.data());
}

void LoadSettings() {
    const bool enableLogging =
        Wh_GetIntSetting(L"diagnostics.windhawkLogging") != 0;
    const bool enableFileLogging =
        Wh_GetIntSetting(L"diagnostics.fileLogging") != 0;
    const bool enableVerboseDiagnostics =
        Wh_GetIntSetting(L"diagnostics.verbose") != 0;
    const bool enableXamlCapture =
        Wh_GetIntSetting(L"diagnostics.xamlCapture") != 0;

    PCWSTR fileLogPathSetting =
        Wh_GetStringSetting(L"diagnostics.fileLogPath");
    std::wstring fileLogPath = ExpandEnvironmentPathOrDefault(
        fileLogPathSetting,
        L"%PUBLIC%\\Desktop\\LogonUIBatteryVolumeControls.log");
    Wh_FreeStringSetting(fileLogPathSetting);

    PCWSTR xamlCaptureDirectorySetting =
        Wh_GetStringSetting(L"diagnostics.xamlCaptureDirectory");
    std::wstring xamlCaptureDirectory = ExpandEnvironmentPathOrDefault(
        xamlCaptureDirectorySetting,
        L"%PUBLIC%\\Desktop");
    Wh_FreeStringSetting(xamlCaptureDirectorySetting);

    AcquireSRWLockExclusive(&g_fileLogLock);
    g_sharedDiagnosticLogPath = fileLogPath;
    ReleaseSRWLockExclusive(&g_fileLogLock);

    AcquireSRWLockExclusive(&g_settingsLock);
    g_xamlCaptureDirectory = xamlCaptureDirectory;
    ReleaseSRWLockExclusive(&g_settingsLock);

    g_enableLogging.store(enableLogging, std::memory_order_relaxed);
    g_enableFileLogging.store(enableFileLogging, std::memory_order_relaxed);
    g_enableVerboseDiagnostics.store(
        enableVerboseDiagnostics, std::memory_order_relaxed);
    g_enableXamlCapture.store(enableXamlCapture, std::memory_order_relaxed);

    PCWSTR batteryVisibilitySetting =
        Wh_GetStringSetting(L"battery.visibility");
    bool showBattery = true;
    bool automaticBatteryVisibility = true;
    if (batteryVisibilitySetting) {
        if (_wcsicmp(batteryVisibilitySetting, L"hide") == 0) {
            showBattery = false;
            automaticBatteryVisibility = false;
        } else if (_wcsicmp(batteryVisibilitySetting, L"show") == 0) {
            showBattery = true;
            automaticBatteryVisibility = false;
        }
    }
    Wh_FreeStringSetting(batteryVisibilitySetting);

    const bool showVolume =
        Wh_GetIntSetting(L"volume.showButton") != 0;
    const bool showImeMode =
        Wh_GetIntSetting(L"nativeButtons.showImeMode") != 0;
    const bool showInput =
        Wh_GetIntSetting(L"nativeButtons.showInput") != 0;
    const bool showNetworkSignIn =
        Wh_GetIntSetting(L"nativeButtons.showNetworkSignIn") != 0;
    const bool showDisconnect =
        Wh_GetIntSetting(L"nativeButtons.showDisconnect") != 0;
    const bool showNetwork =
        Wh_GetIntSetting(L"nativeButtons.showNetwork") != 0;
    const bool showAccessibility =
        Wh_GetIntSetting(L"nativeButtons.showAccessibility") != 0;
    const bool showPower =
        Wh_GetIntSetting(L"nativeButtons.showPower") != 0;
    const bool showOtherNative =
        Wh_GetIntSetting(L"nativeButtons.showOther") != 0;
    int nativeImeInputSpacing =
        Wh_GetIntSetting(L"nativeButtons.imeInputSpacing");
    nativeImeInputSpacing = std::clamp(nativeImeInputSpacing, -1, 32);
    int imeModeVisualOffsetX =
        Wh_GetIntSetting(L"nativeButtons.imeModeVisualOffsetX");
    imeModeVisualOffsetX = std::clamp(imeModeVisualOffsetX, -32, 32);
    const bool showBatteryPercentage =
        Wh_GetIntSetting(L"battery.percentage.show") != 0;
    const bool showVolumePercentage =
        Wh_GetIntSetting(L"volume.percentage.show") != 0;
    const bool hideBatteryPercentageAt100 =
        Wh_GetIntSetting(L"battery.percentage.hideAt100") != 0;
    const bool hideVolumePercentageAt100 =
        Wh_GetIntSetting(L"volume.percentage.hideAt100") != 0;
    const bool showMutedTextInVolumePercentage =
        Wh_GetIntSetting(L"volume.percentage.showMutedText") != 0;
    const bool percentageShowPercentSign =
        Wh_GetIntSetting(L"percentageLabels.showPercentSign") != 0;

    int percentageFontSize =
        Wh_GetIntSetting(L"percentageLabels.fontSize");
    percentageFontSize = std::clamp(percentageFontSize, 8, 30);
    int percentageSpacing =
        Wh_GetIntSetting(L"percentageLabels.spacing");
    percentageSpacing = std::clamp(percentageSpacing, 0, 32);

    PCWSTR batteryPercentagePositionSetting =
        Wh_GetStringSetting(L"battery.percentage.position");
    const auto batteryPercentagePosition =
        ParsePercentagePosition(batteryPercentagePositionSetting);
    Wh_FreeStringSetting(batteryPercentagePositionSetting);

    PCWSTR volumePercentagePositionSetting =
        Wh_GetStringSetting(L"volume.percentage.position");
    const auto volumePercentagePosition =
        ParsePercentagePosition(volumePercentagePositionSetting);
    Wh_FreeStringSetting(volumePercentagePositionSetting);

    PCWSTR percentageFontFamilySetting =
        Wh_GetStringSetting(L"percentageLabels.fontFamily");
    std::wstring percentageFontFamily =
        (percentageFontFamilySetting && *percentageFontFamilySetting)
            ? std::wstring(percentageFontFamilySetting)
            : std::wstring(L"Segoe UI");
    Wh_FreeStringSetting(percentageFontFamilySetting);

    g_showBatteryButton.store(showBattery, std::memory_order_relaxed);
    g_batteryVisibilityAutomatic.store(
        automaticBatteryVisibility, std::memory_order_relaxed);
    g_showVolumeButton.store(showVolume, std::memory_order_relaxed);
    g_showImeModeButton.store(showImeMode, std::memory_order_relaxed);
    g_showInputButton.store(showInput, std::memory_order_relaxed);
    g_showNetworkSignInButton.store(
        showNetworkSignIn, std::memory_order_relaxed);
    g_showDisconnectButton.store(
        showDisconnect, std::memory_order_relaxed);
    g_showNetworkButton.store(showNetwork, std::memory_order_relaxed);
    g_showAccessibilityButton.store(
        showAccessibility, std::memory_order_relaxed);
    g_showPowerButton.store(showPower, std::memory_order_relaxed);
    g_showOtherNativeButtons.store(
        showOtherNative, std::memory_order_relaxed);
    g_nativeImeInputSpacing.store(
        nativeImeInputSpacing, std::memory_order_relaxed);
    g_imeModeVisualOffsetX.store(
        imeModeVisualOffsetX, std::memory_order_relaxed);
    g_showBatteryPercentage.store(
        showBatteryPercentage, std::memory_order_relaxed);
    g_showVolumePercentage.store(
        showVolumePercentage, std::memory_order_relaxed);
    g_batteryPercentagePosition.store(
        batteryPercentagePosition, std::memory_order_relaxed);
    g_volumePercentagePosition.store(
        volumePercentagePosition, std::memory_order_relaxed);
    g_hideBatteryPercentageAt100.store(
        hideBatteryPercentageAt100, std::memory_order_relaxed);
    g_hideVolumePercentageAt100.store(
        hideVolumePercentageAt100, std::memory_order_relaxed);
    g_showMutedTextInVolumePercentage.store(
        showMutedTextInVolumePercentage, std::memory_order_relaxed);
    g_percentageShowPercentSign.store(
        percentageShowPercentSign, std::memory_order_relaxed);
    g_percentageFontSize.store(
        percentageFontSize, std::memory_order_relaxed);
    g_percentageSpacing.store(
        percentageSpacing, std::memory_order_relaxed);

    PCWSTR renderer = Wh_GetStringSetting(L"volume.renderer");
    VolumeRendererMode mode = VolumeRendererMode::Automatic;

    if (renderer) {
        if (_wcsicmp(renderer, L"mdl2") == 0) {
            mode = VolumeRendererMode::SegoeMdl2;
        } else if (_wcsicmp(renderer, L"fluent") == 0) {
            mode = VolumeRendererMode::SegoeFluent;
        } else if (_wcsicmp(renderer, L"sndvol-auto") == 0) {
            mode = VolumeRendererMode::SndVolAutomatic;
        } else if (_wcsicmp(renderer, L"sndvol-dll") == 0) {
            mode = VolumeRendererMode::SndVolDll;
        } else if (_wcsicmp(renderer, L"sndvol-mun") == 0) {
            mode = VolumeRendererMode::SndVolMun;
        }
    }

    Wh_FreeStringSetting(renderer);
    g_volumeRenderer.store(mode);
    ModLog(L"Volume renderer setting: %s", VolumeRendererModeName(mode));

    PCWSTR handoffSetting = Wh_GetStringSetting(L"volume.uiHandoff");
    VolumeUiHandoffMode handoffMode = VolumeUiHandoffMode::Automatic;
    if (handoffSetting) {
        if (_wcsicmp(handoffSetting, L"dispatcherqueue") == 0) {
            handoffMode = VolumeUiHandoffMode::DispatcherQueue;
        } else if (_wcsicmp(handoffSetting, L"coredispatcher") == 0) {
            handoffMode = VolumeUiHandoffMode::CoreDispatcher;
        } else if (_wcsicmp(handoffSetting, L"legacy-message-window") == 0) {
            handoffMode = VolumeUiHandoffMode::LegacyMessageWindow;
        }
    }
    Wh_FreeStringSetting(handoffSetting);
    g_volumeUiHandoff.store(handoffMode);
    ModLog(L"Volume UI handoff setting: %s",
           VolumeUiHandoffModeName(handoffMode));

    PCWSTR batteryClickSetting =
        Wh_GetStringSetting(L"battery.clickAction");
    BatteryClickAction batteryClickAction = BatteryClickAction::Open;
    if (batteryClickSetting &&
        _wcsicmp(batteryClickSetting, L"none") == 0) {
        batteryClickAction = BatteryClickAction::None;
    }
    Wh_FreeStringSetting(batteryClickSetting);
    g_batteryClickAction.store(batteryClickAction);
    ModLog(L"Battery click action: %s",
           BatteryClickActionName(batteryClickAction));

    PCWSTR batteryRefreshSetting =
        Wh_GetStringSetting(L"battery.flyout.refresh");
    BatteryFlyoutRefreshMode batteryRefresh =
        BatteryFlyoutRefreshMode::WindowsDefault;
    if (batteryRefreshSetting &&
        _wcsicmp(batteryRefreshSetting, L"event-driven") == 0) {
        batteryRefresh = BatteryFlyoutRefreshMode::EventDriven;
    }
    Wh_FreeStringSetting(batteryRefreshSetting);
    g_batteryFlyoutRefresh.store(batteryRefresh);
    ModLog(L"Battery flyout refresh: %s",
           BatteryFlyoutRefreshModeName(batteryRefresh));

    int batteryEventDebounceDelay =
        Wh_GetIntSetting(L"battery.flyout.eventDebounceDelayMs");
    batteryEventDebounceDelay =
        std::clamp(batteryEventDebounceDelay, 0, 5000);
    g_batteryEventDebounceDelayMs.store(
        static_cast<DWORD>(batteryEventDebounceDelay),
        std::memory_order_relaxed);
    ModLog(L"Battery event debounce delay: %d ms (one-shot; not polling)",
           batteryEventDebounceDelay);

    PCWSTR batteryWheelSetting =
        Wh_GetStringSetting(L"battery.brightnessScroll.action");
    BatteryWheelAction batteryWheelAction =
        (batteryWheelSetting &&
         _wcsicmp(batteryWheelSetting, L"adjust") == 0)
            ? BatteryWheelAction::AdjustBrightness
            : BatteryWheelAction::None;
    Wh_FreeStringSetting(batteryWheelSetting);

    int batteryWheelStep =
        Wh_GetIntSetting(L"battery.brightnessScroll.stepPercent");
    batteryWheelStep = std::clamp(batteryWheelStep, 1, 25);
    const bool batteryWheelReverse =
        Wh_GetIntSetting(L"battery.brightnessScroll.reverse") != 0;

    g_batteryWheelAction.store(batteryWheelAction);
    g_batteryWheelStepPercent.store(batteryWheelStep);
    g_batteryWheelReverse.store(batteryWheelReverse);

    ModLog(L"Battery wheel: action=%s step=%d%% reverse=%d",
           BatteryWheelActionName(batteryWheelAction),
           batteryWheelStep,
           batteryWheelReverse ? 1 : 0);

    int precisionScrollStep =
        Wh_GetIntSetting(L"precisionScrolling.stepPercent");
    precisionScrollStep = std::clamp(precisionScrollStep, 1, 25);
    g_precisionScrollStepPercent.store(
        precisionScrollStep, std::memory_order_relaxed);

    int precisionScrollDeltaPerStep =
        Wh_GetIntSetting(L"precisionScrolling.deltaPerStep");
    precisionScrollDeltaPerStep =
        std::clamp(precisionScrollDeltaPerStep, 1, WHEEL_DELTA);
    g_precisionScrollDeltaPerStep.store(
        precisionScrollDeltaPerStep, std::memory_order_relaxed);

    ModLog(L"Precision/high-resolution scroll: step=%d%% deltaPerStep=%d (shared Battery/Volume)",
           precisionScrollStep,
           precisionScrollDeltaPerStep);

    PCWSTR volumeClickSetting =
        Wh_GetStringSetting(L"volume.clickAction");
    VolumeClickAction volumeClickAction =
        VolumeClickAction::Launch;
    if (volumeClickSetting &&
        _wcsicmp(volumeClickSetting, L"none") == 0) {
        volumeClickAction = VolumeClickAction::None;
    }
    Wh_FreeStringSetting(volumeClickSetting);
    g_volumeClickAction.store(volumeClickAction);
    ModLog(L"Volume click action: %s",
           VolumeClickActionName(volumeClickAction));

    PCWSTR volumeWheelSetting =
        Wh_GetStringSetting(L"volume.scroll.action");
    VolumeWheelAction volumeWheelAction =
        (volumeWheelSetting &&
         _wcsicmp(volumeWheelSetting, L"adjust") == 0)
            ? VolumeWheelAction::Adjust
            : VolumeWheelAction::None;
    Wh_FreeStringSetting(volumeWheelSetting);

    int volumeWheelStep =
        Wh_GetIntSetting(L"volume.scroll.stepPercent");
    volumeWheelStep = std::clamp(volumeWheelStep, 1, 25);
    const bool volumeWheelUnmute =
        Wh_GetIntSetting(L"volume.scroll.unmute") != 0;
    const bool volumeWheelReverse =
        Wh_GetIntSetting(L"volume.scroll.reverse") != 0;

    g_volumeWheelAction.store(volumeWheelAction);
    g_volumeWheelStepPercent.store(volumeWheelStep);
    g_volumeWheelUnmute.store(volumeWheelUnmute);
    g_volumeWheelReverse.store(volumeWheelReverse);

    ModLog(L"Volume wheel: action=%s step=%d%% unmute=%d reverse=%d",
           VolumeWheelActionName(volumeWheelAction),
           volumeWheelStep,
           volumeWheelUnmute ? 1 : 0,
           volumeWheelReverse ? 1 : 0);

    PCWSTR batteryPlacementSetting =
        Wh_GetStringSetting(L"battery.flyout.placement");
    BatteryFlyoutPlacementMode batteryPlacement =
        BatteryFlyoutPlacementMode::AnchorToButton;
    if (batteryPlacementSetting &&
        _wcsicmp(batteryPlacementSetting, L"windows") == 0) {
        batteryPlacement = BatteryFlyoutPlacementMode::WindowsDefault;
    }
    Wh_FreeStringSetting(batteryPlacementSetting);
    g_batteryFlyoutPlacement.store(batteryPlacement);
    ModLog(L"Battery flyout placement: %s",
           BatteryFlyoutPlacementModeName(batteryPlacement));

    PCWSTR batteryFlyoutSideSetting =
        Wh_GetStringSetting(L"battery.flyout.anchorSide");
    const auto batteryFlyoutSide =
        ParseAnchorSide(batteryFlyoutSideSetting);
    Wh_FreeStringSetting(batteryFlyoutSideSetting);
    const int batteryFlyoutOffsetX =
        std::clamp(Wh_GetIntSetting(L"battery.flyout.offsetX"), -2000, 2000);
    const int batteryFlyoutOffsetY =
        std::clamp(Wh_GetIntSetting(L"battery.flyout.offsetY"), -2000, 2000);
    g_batteryFlyoutAnchorSide.store(batteryFlyoutSide);
    g_batteryFlyoutOffsetX.store(batteryFlyoutOffsetX);
    g_batteryFlyoutOffsetY.store(batteryFlyoutOffsetY);
    ModLog(L"Battery flyout anchor: side=%s offset=(%d,%d)",
           AnchorSideName(batteryFlyoutSide),
           batteryFlyoutOffsetX,
           batteryFlyoutOffsetY);

    PCWSTR morePowerSetting =
        Wh_GetStringSetting(L"battery.advancedPower.action");
    MorePowerOptionsAction morePowerAction =
        MorePowerOptionsAction::AdvancedPowerOptions;
    if (morePowerSetting &&
        _wcsicmp(morePowerSetting, L"windows") == 0) {
        morePowerAction = MorePowerOptionsAction::WindowsDefault;
    }
    Wh_FreeStringSetting(morePowerSetting);
    g_morePowerOptionsAction.store(morePowerAction);
    ModLog(L"More power options action: %s",
           MorePowerOptionsActionName(morePowerAction));

    PCWSTR advancedPowerPlacementSetting =
        Wh_GetStringSetting(L"battery.advancedPower.placement");
    AdvancedPowerPlacementMode advancedPowerPlacement =
        AdvancedPowerPlacementMode::AnchorToBatteryButton;
    if (advancedPowerPlacementSetting &&
        _wcsicmp(advancedPowerPlacementSetting, L"windows") == 0) {
        advancedPowerPlacement =
            AdvancedPowerPlacementMode::WindowsDefault;
    }
    Wh_FreeStringSetting(advancedPowerPlacementSetting);
    g_advancedPowerPlacement.store(advancedPowerPlacement);
    ModLog(L"Advanced Power Options placement: %s",
           AdvancedPowerPlacementModeName(advancedPowerPlacement));

    PCWSTR advancedPowerSideSetting =
        Wh_GetStringSetting(L"battery.advancedPower.anchorSide");
    const auto advancedPowerSide =
        ParseAnchorSide(advancedPowerSideSetting);
    Wh_FreeStringSetting(advancedPowerSideSetting);
    const int advancedPowerOffsetX =
        std::clamp(Wh_GetIntSetting(L"battery.advancedPower.offsetX"), -2000, 2000);
    const int advancedPowerOffsetY =
        std::clamp(Wh_GetIntSetting(L"battery.advancedPower.offsetY"), -2000, 2000);
    g_advancedPowerAnchorSide.store(advancedPowerSide);
    g_advancedPowerOffsetX.store(advancedPowerOffsetX);
    g_advancedPowerOffsetY.store(advancedPowerOffsetY);
    ModLog(L"Advanced Power anchor: side=%s offset=(%d,%d)",
           AnchorSideName(advancedPowerSide),
           advancedPowerOffsetX,
           advancedPowerOffsetY);

    PCWSTR sndVolPlacementSetting =
        Wh_GetStringSetting(L"volume.sndVol.compact.placement");
    SndVolPlacementMode sndVolPlacement =
        SndVolPlacementMode::AnchorToButton;
    if (sndVolPlacementSetting &&
        _wcsicmp(sndVolPlacementSetting, L"windows") == 0) {
        sndVolPlacement = SndVolPlacementMode::WindowsDefault;
    }
    Wh_FreeStringSetting(sndVolPlacementSetting);
    g_sndVolPlacement.store(sndVolPlacement);
    ModLog(L"SndVol placement: %s",
           SndVolPlacementModeName(sndVolPlacement));

    PCWSTR sndVolSideSetting =
        Wh_GetStringSetting(L"volume.sndVol.compact.anchorSide");
    const auto sndVolSide = ParseAnchorSide(sndVolSideSetting);
    Wh_FreeStringSetting(sndVolSideSetting);
    const int sndVolOffsetX =
        std::clamp(Wh_GetIntSetting(L"volume.sndVol.compact.offsetX"), -2000, 2000);
    const int sndVolOffsetY =
        std::clamp(Wh_GetIntSetting(L"volume.sndVol.compact.offsetY"), -2000, 2000);
    g_sndVolAnchorSide.store(sndVolSide);
    g_sndVolOffsetX.store(sndVolOffsetX);
    g_sndVolOffsetY.store(sndVolOffsetY);
    ModLog(L"SndVol compact anchor: side=%s offset=(%d,%d)",
           AnchorSideName(sndVolSide), sndVolOffsetX, sndVolOffsetY);

    PCWSTR sndVolMixerPlacementSetting =
        Wh_GetStringSetting(L"volume.sndVol.mixer.placement");
    SndVolPlacementMode sndVolMixerPlacement =
        SndVolPlacementMode::AnchorToButton;
    if (sndVolMixerPlacementSetting &&
        _wcsicmp(sndVolMixerPlacementSetting, L"windows") == 0) {
        sndVolMixerPlacement = SndVolPlacementMode::WindowsDefault;
    }
    Wh_FreeStringSetting(sndVolMixerPlacementSetting);
    g_sndVolMixerPlacement.store(sndVolMixerPlacement);
    ModLog(L"SndVol mixer placement: %s",
           SndVolPlacementModeName(sndVolMixerPlacement));

    PCWSTR sndVolMixerSideSetting =
        Wh_GetStringSetting(L"volume.sndVol.mixer.anchorSide");
    const auto sndVolMixerSide =
        ParseAnchorSide(sndVolMixerSideSetting);
    Wh_FreeStringSetting(sndVolMixerSideSetting);
    const int sndVolMixerOffsetX =
        std::clamp(Wh_GetIntSetting(L"volume.sndVol.mixer.offsetX"), -2000, 2000);
    const int sndVolMixerOffsetY =
        std::clamp(Wh_GetIntSetting(L"volume.sndVol.mixer.offsetY"), -2000, 2000);
    g_sndVolMixerAnchorSide.store(sndVolMixerSide);
    g_sndVolMixerOffsetX.store(sndVolMixerOffsetX);
    g_sndVolMixerOffsetY.store(sndVolMixerOffsetY);
    ModLog(L"SndVol mixer anchor: side=%s offset=(%d,%d)",
           AnchorSideName(sndVolMixerSide),
           sndVolMixerOffsetX,
           sndVolMixerOffsetY);

    PCWSTR sndVolParametersSetting =
        Wh_GetStringSetting(L"volume.sndVol.launchParameters");
    std::wstring sndVolParameters =
        sndVolParametersSetting ? std::wstring(sndVolParametersSetting)
                                : std::wstring(L"-f");
    Wh_FreeStringSetting(sndVolParametersSetting);

    PCWSTR buttonOrder = Wh_GetStringSetting(L"layout.buttonOrder");
    std::wstring order =
        (buttonOrder && *buttonOrder)
            ? std::wstring(buttonOrder)
            : std::wstring(L"ime,input,battery,volume,network-signin,disconnect,network,accessibility,power");
    Wh_FreeStringSetting(buttonOrder);

    AcquireSRWLockExclusive(&g_settingsLock);
    g_buttonOrder = order;
    g_sndVolLaunchParameters = sndVolParameters;
    g_percentageFontFamily = percentageFontFamily;
    ReleaseSRWLockExclusive(&g_settingsLock);
    ModLog(L"Button order setting: %s", order.c_str());
    ModLog(L"SndVol launch parameters: %s",
           sndVolParameters.empty() ? L"<empty>" : sndVolParameters.c_str());
    ModLog(L"Native visibility: ime=%d input=%d networkSignIn=%d disconnect=%d network=%d accessibility=%d power=%d other=%d",
           showImeMode ? 1 : 0,
           showInput ? 1 : 0,
           showNetworkSignIn ? 1 : 0,
           showDisconnect ? 1 : 0,
           showNetwork ? 1 : 0,
           showAccessibility ? 1 : 0,
           showPower ? 1 : 0,
           showOtherNative ? 1 : 0);
    if (nativeImeInputSpacing < 0) {
        ModLog(L"Reordered IME/Input external spacing: Automatic; IME glyph visual offset: %d DIP",
               imeModeVisualOffsetX);
    } else {
        ModLog(L"Reordered IME/Input external spacing: %d DIP; IME glyph visual offset: %d DIP",
               nativeImeInputSpacing,
               imeModeVisualOffsetX);
    }
    const wchar_t* batteryVisibilityName =
        !showBattery
            ? L"Hide"
            : automaticBatteryVisibility
                ? L"Automatic"
                : L"Always show";
    ModLog(L"Injected buttons: battery=%s volume=%d percentages: battery=%d(%s) volume=%d(%s) font=%s size=%d spacing=%d sign=%d",
           batteryVisibilityName,
           showVolume ? 1 : 0,
           showBatteryPercentage ? 1 : 0,
           PercentagePositionName(batteryPercentagePosition),
           showVolumePercentage ? 1 : 0,
           PercentagePositionName(volumePercentagePosition),
           percentageFontFamily.c_str(),
           percentageFontSize,
           percentageSpacing,
           percentageShowPercentSign ? 1 : 0);
    ModLog(L"Diagnostics: logging=%d fileLogging=%d verbose=%d xamlCapture=%d filePath=%s captureDir=%s",
           enableLogging ? 1 : 0,
           enableFileLogging ? 1 : 0,
           enableVerboseDiagnostics ? 1 : 0,
           enableXamlCapture ? 1 : 0,
           fileLogPath.empty() ? L"<empty>" : fileLogPath.c_str(),
           xamlCaptureDirectory.empty() ? L"<empty>" : xamlCaptureDirectory.c_str());
}

std::wstring GetButtonOrderSetting() {
    AcquireSRWLockShared(&g_settingsLock);
    std::wstring value = g_buttonOrder;
    ReleaseSRWLockShared(&g_settingsLock);
    return value;
}

std::wstring GetSndVolLaunchParameters() {
    AcquireSRWLockShared(&g_settingsLock);
    std::wstring value = g_sndVolLaunchParameters;
    ReleaseSRWLockShared(&g_settingsLock);
    return value;
}

std::wstring GetPercentageFontFamilySetting() {
    AcquireSRWLockShared(&g_settingsLock);
    std::wstring value = g_percentageFontFamily;
    ReleaseSRWLockShared(&g_settingsLock);
    return value.empty() ? std::wstring(L"Segoe UI") : value;
}

bool GetLoadedModulePeIdentity(HMODULE module,
                               DWORD* timeDateStamp,
                               DWORD* sizeOfImage) {
    if (!module || !timeDateStamp || !sizeOfImage) {
        return false;
    }

    // Windhawk's Clang toolchain doesn't support MSVC __try/__except.
    // The module base comes from GetModuleHandle/LoadLibraryEx, so validate the
    // PE headers normally and fail closed if they don't match expectations.
    auto base = reinterpret_cast<const BYTE*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew <= 0 ||
        dos->e_lfanew > 0x100000) {
        return false;
    }

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }

    *timeDateStamp = nt->FileHeader.TimeDateStamp;
    *sizeOfImage = nt->OptionalHeader.SizeOfImage;
    return true;
}

bool IsShowBatteryUiSymbolName(PCWSTR symbol) {
    if (!symbol) {
        return false;
    }

    if (_wcsicmp(symbol, L"ShowBatteryUI") == 0) {
        return true;
    }

    const std::wstring_view value(symbol);
    const size_t pos = value.find(L"ShowBatteryUI");
    if (pos == std::wstring_view::npos) {
        return false;
    }

    // Accept a full undecorated signature if Windhawk/DbgHelp emits one, but
    // avoid accidentally matching unrelated helper/local symbols.
    const size_t after = pos + wcslen(L"ShowBatteryUI");
    return (pos == 0 || value[pos - 1] == L' ') &&
           (after == value.size() || value[after] == L'(');
}

bool IsStobjectBatteryStateSymbolName(PCWSTR symbol) {
    if (!symbol) {
        return false;
    }

    const std::wstring_view value(symbol);
    if (_wcsicmp(symbol, L"g_bs") == 0) {
        return true;
    }

    // Public PDBs commonly expose this global as:
    // ?g_bs@@3U_BATTERY_STATE@@A
    return value.find(L"?g_bs@@") != std::wstring_view::npos;
}

bool IsLaunchPowerCplSymbolName(PCWSTR symbol) {
    if (!symbol) {
        return false;
    }

    if (_wcsicmp(symbol, L"LaunchPowerCPL") == 0) {
        return true;
    }

    const std::wstring_view value(symbol);
    const size_t pos = value.find(L"LaunchPowerCPL");
    if (pos == std::wstring_view::npos) {
        return false;
    }

    const size_t after = pos + wcslen(L"LaunchPowerCPL");
    return (pos == 0 || value[pos - 1] == L' ') &&
           (after == value.size() || value[after] == L'(');
}



struct StobjectPeView {
    BYTE* base = nullptr;
    const IMAGE_NT_HEADERS64* nt = nullptr;
    const IMAGE_SECTION_HEADER* sections = nullptr;
    WORD sectionCount = 0;
};

bool GetStobjectPeView(HMODULE module, StobjectPeView* view) {
    if (!module || !view) {
        return false;
    }

    auto base = reinterpret_cast<BYTE*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew <= 0 ||
        dos->e_lfanew > 0x100000) {
        return false;
    }

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage < 0x1000) {
        return false;
    }

    view->base = base;
    view->nt = nt;
    view->sections = IMAGE_FIRST_SECTION(
        const_cast<IMAGE_NT_HEADERS64*>(nt));
    view->sectionCount = nt->FileHeader.NumberOfSections;
    return true;
}

size_t PeSectionVirtualSpan(const IMAGE_SECTION_HEADER& section) {
    return std::max<size_t>(
        section.Misc.VirtualSize,
        section.SizeOfRawData);
}

const IMAGE_SECTION_HEADER* FindPeSectionByName(
        const StobjectPeView& view,
        const char* name) {
    if (!name) {
        return nullptr;
    }

    for (WORD i = 0; i < view.sectionCount; ++i) {
        char sectionName[IMAGE_SIZEOF_SHORT_NAME + 1]{};
        memcpy(sectionName,
               view.sections[i].Name,
               IMAGE_SIZEOF_SHORT_NAME);
        if (strcmp(sectionName, name) == 0) {
            return &view.sections[i];
        }
    }

    return nullptr;
}

const IMAGE_SECTION_HEADER* FindPeSectionForRva(
        const StobjectPeView& view,
        DWORD rva) {
    for (WORD i = 0; i < view.sectionCount; ++i) {
        const auto& section = view.sections[i];
        const size_t span = PeSectionVirtualSpan(section);
        if (rva >= section.VirtualAddress &&
            static_cast<uint64_t>(rva) <
                static_cast<uint64_t>(section.VirtualAddress) + span) {
            return &section;
        }
    }

    return nullptr;
}

bool IsPeRvaRangeValid(
        const StobjectPeView& view,
        DWORD rva,
        size_t size) {
    const uint64_t end =
        static_cast<uint64_t>(rva) + static_cast<uint64_t>(size);
    return end >= rva &&
           end <= view.nt->OptionalHeader.SizeOfImage;
}

bool IsWritableNonExecutableRva(
        const StobjectPeView& view,
        DWORD rva,
        size_t size) {
    const auto* section = FindPeSectionForRva(view, rva);
    if (!section) {
        return false;
    }

    if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) == 0 ||
        (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
        return false;
    }

    const uint64_t sectionEnd =
        static_cast<uint64_t>(section->VirtualAddress) +
        PeSectionVirtualSpan(*section);
    const uint64_t rangeEnd =
        static_cast<uint64_t>(rva) + size;
    return rangeEnd >= rva && rangeEnd <= sectionEnd;
}

bool FindContainingRuntimeFunction(
        const StobjectPeView& view,
        DWORD rva,
        DWORD* beginRva,
        DWORD* endRva) {
    if (!beginRva || !endRva) {
        return false;
    }

    const auto& dir =
        view.nt->OptionalHeader
            .DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!dir.VirtualAddress ||
        dir.Size < sizeof(RUNTIME_FUNCTION) ||
        !IsPeRvaRangeValid(view, dir.VirtualAddress, dir.Size)) {
        return false;
    }

    auto functions = reinterpret_cast<const RUNTIME_FUNCTION*>(
        view.base + dir.VirtualAddress);
    const size_t count = dir.Size / sizeof(RUNTIME_FUNCTION);

    for (size_t i = 0; i < count; ++i) {
        const DWORD begin = functions[i].BeginAddress;
        const DWORD end = functions[i].EndAddress;
        if (begin < end && rva >= begin && rva < end) {
            *beginRva = begin;
            *endRva = end;
            return true;
        }
    }

    return false;
}

void PushUniqueRva(std::vector<DWORD>* values, DWORD value) {
    if (!values) {
        return;
    }

    if (std::find(values->begin(), values->end(), value) ==
        values->end()) {
        values->push_back(value);
    }
}

std::vector<DWORD> FindWideLiteralRvas(
        const StobjectPeView& view,
        std::wstring_view literal) {
    std::vector<DWORD> matches;
    if (literal.empty()) {
        return matches;
    }

    const size_t literalBytes =
        literal.size() * sizeof(wchar_t);
    const size_t totalBytes =
        literalBytes + sizeof(wchar_t);

    for (WORD i = 0; i < view.sectionCount; ++i) {
        const auto& section = view.sections[i];

        // String literals belong in readable, non-executable image data.
        if ((section.Characteristics & IMAGE_SCN_MEM_READ) == 0 ||
            (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
            continue;
        }

        const size_t span = PeSectionVirtualSpan(section);
        if (span < totalBytes ||
            !IsPeRvaRangeValid(
                view,
                section.VirtualAddress,
                span)) {
            continue;
        }

        const BYTE* bytes =
            view.base + section.VirtualAddress;
        for (size_t offset = 0;
             offset + totalBytes <= span;
             ++offset) {
            if (memcmp(bytes + offset,
                       literal.data(),
                       literalBytes) != 0) {
                continue;
            }

            wchar_t terminator = L'\0';
            memcpy(&terminator,
                   bytes + offset + literalBytes,
                   sizeof(terminator));
            if (terminator != L'\0') {
                continue;
            }

            PushUniqueRva(
                &matches,
                section.VirtualAddress +
                    static_cast<DWORD>(offset));
        }
    }

    return matches;
}

std::vector<DWORD> FindRipRelativeLeaXrefs(
        const StobjectPeView& view,
        DWORD targetRva) {
    std::vector<DWORD> xrefs;
    const auto* text = FindPeSectionByName(view, ".text");
    if (!text) {
        return xrefs;
    }

    const size_t span = PeSectionVirtualSpan(*text);
    if (span < 7 ||
        !IsPeRvaRangeValid(
            view,
            text->VirtualAddress,
            span)) {
        return xrefs;
    }

    const BYTE* bytes =
        view.base + text->VirtualAddress;

    for (size_t offset = 0; offset + 7 <= span; ++offset) {
        const BYTE rex = bytes[offset];
        const BYTE opcode = bytes[offset + 1];
        const BYTE modrm = bytes[offset + 2];

        // REX.W + LEA r64,[RIP+disp32].
        if ((rex & 0xF8) != 0x48 ||
            opcode != 0x8D ||
            (modrm & 0xC7) != 0x05) {
            continue;
        }

        int32_t displacement = 0;
        memcpy(&displacement, bytes + offset + 3,
               sizeof(displacement));

        const int64_t instructionRva =
            static_cast<int64_t>(text->VirtualAddress) +
            static_cast<int64_t>(offset);
        const int64_t referencedRva =
            instructionRva + 7 + displacement;

        if (referencedRva == targetRva) {
            xrefs.push_back(
                static_cast<DWORD>(instructionRva));
        }
    }

    return xrefs;
}

bool FunctionContainsDirectCallTo(
        const StobjectPeView& view,
        DWORD beginRva,
        DWORD endRva,
        DWORD targetRva) {
    if (beginRva >= endRva ||
        !IsPeRvaRangeValid(
            view,
            beginRva,
            static_cast<size_t>(endRva - beginRva))) {
        return false;
    }

    const BYTE* bytes = view.base + beginRva;
    const size_t size = endRva - beginRva;

    for (size_t offset = 0; offset + 5 <= size; ++offset) {
        if (bytes[offset] != 0xE8) {
            continue;
        }

        int32_t displacement = 0;
        memcpy(&displacement, bytes + offset + 1,
               sizeof(displacement));

        const int64_t callRva =
            static_cast<int64_t>(beginRva) +
            static_cast<int64_t>(offset);
        const int64_t calledRva =
            callRva + 5 + displacement;
        if (calledRva == targetRva) {
            return true;
        }
    }

    return false;
}

std::vector<DWORD> FindDirectCallerFunctions(
        const StobjectPeView& view,
        DWORD targetRva) {
    std::vector<DWORD> callers;
    const auto* text = FindPeSectionByName(view, ".text");
    if (!text) {
        return callers;
    }

    const size_t span = PeSectionVirtualSpan(*text);
    if (span < 5 ||
        !IsPeRvaRangeValid(
            view,
            text->VirtualAddress,
            span)) {
        return callers;
    }

    const BYTE* bytes =
        view.base + text->VirtualAddress;

    for (size_t offset = 0; offset + 5 <= span; ++offset) {
        if (bytes[offset] != 0xE8) {
            continue;
        }

        int32_t displacement = 0;
        memcpy(&displacement, bytes + offset + 1,
               sizeof(displacement));

        const int64_t callRva =
            static_cast<int64_t>(text->VirtualAddress) +
            static_cast<int64_t>(offset);
        const int64_t calledRva =
            callRva + 5 + displacement;
        if (calledRva != targetRva) {
            continue;
        }

        DWORD callerBegin = 0;
        DWORD callerEnd = 0;
        if (FindContainingRuntimeFunction(
                view,
                static_cast<DWORD>(callRva),
                &callerBegin,
                &callerEnd)) {
            PushUniqueRva(&callers, callerBegin);
        }
    }

    return callers;
}

void* ResolveShowBatteryUiStructurally(
        HMODULE module,
        const StobjectPeView& view) {
    const auto literals =
        FindWideLiteralRvas(view, L"BatMeterFlyout");
    if (literals.size() != 1) {
        ModLog(L"Symbol-free Battery resolver: expected one BatMeterFlyout literal, got %u",
               static_cast<unsigned>(literals.size()));
        return nullptr;
    }

    const auto xrefs =
        FindRipRelativeLeaXrefs(view, literals.front());

    std::vector<DWORD> xrefFunctions;
    for (DWORD xref : xrefs) {
        DWORD begin = 0;
        DWORD end = 0;
        if (FindContainingRuntimeFunction(
                view, xref, &begin, &end)) {
            PushUniqueRva(&xrefFunctions, begin);
        }
    }

    // Across the harvested 17763/18362/19041/22000/22621/26100
    // generations, the BatMeterFlyout literal is referenced by two related
    // functions. CreateFlyoutWindow is the one which directly calls the other
    // literal-referencing helper.
    std::vector<DWORD> createCandidates;
    for (DWORD candidate : xrefFunctions) {
        DWORD candidateBegin = 0;
        DWORD candidateEnd = 0;
        if (!FindContainingRuntimeFunction(
                view,
                candidate,
                &candidateBegin,
                &candidateEnd)) {
            continue;
        }

        for (DWORD other : xrefFunctions) {
            if (other == candidate) {
                continue;
            }

            if (FunctionContainsDirectCallTo(
                    view,
                    candidateBegin,
                    candidateEnd,
                    other)) {
                PushUniqueRva(
                    &createCandidates,
                    candidateBegin);
            }
        }
    }

    if (createCandidates.size() != 1) {
        ModLog(L"Symbol-free Battery resolver: BatMeterFlyout xrefs=%u functions=%u createCandidates=%u; failing closed",
               static_cast<unsigned>(xrefs.size()),
               static_cast<unsigned>(xrefFunctions.size()),
               static_cast<unsigned>(createCandidates.size()));
        return nullptr;
    }

    const DWORD createFlyoutRva =
        createCandidates.front();
    const auto callers =
        FindDirectCallerFunctions(
            view,
            createFlyoutRva);

    if (callers.size() != 1) {
        ModLog(L"Symbol-free Battery resolver: expected one direct caller of CreateFlyoutWindow RVA=0x%X, got %u; failing closed",
               createFlyoutRva,
               static_cast<unsigned>(callers.size()));
        return nullptr;
    }

    const DWORD showBatteryUiRva =
        callers.front();

    ModLog(L"Symbol-free Battery resolver: BatMeterFlyout RVA=0x%X CreateFlyoutWindow RVA=0x%X ShowBatteryUI RVA=0x%X",
           literals.front(),
           createFlyoutRva,
           showBatteryUiRva);

    return reinterpret_cast<BYTE*>(module) +
           showBatteryUiRva;
}

struct DelayImportDescriptorRva {
    DWORD attributes;
    DWORD dllNameRva;
    DWORD moduleHandleRva;
    DWORD importAddressTableRva;
    DWORD importNameTableRva;
    DWORD boundImportAddressTableRva;
    DWORD unloadInformationTableRva;
    DWORD timeDateStamp;
};

bool AsciiStringEqualsAtRva(
        const StobjectPeView& view,
        DWORD rva,
        const char* expected) {
    if (!expected ||
        !IsPeRvaRangeValid(view, rva, 1)) {
        return false;
    }

    const size_t expectedLength = strlen(expected);
    if (!IsPeRvaRangeValid(
            view,
            rva,
            expectedLength + 1)) {
        return false;
    }

    const char* value =
        reinterpret_cast<const char*>(
            view.base + rva);
    return _strnicmp(
               value,
               expected,
               expectedLength) == 0 &&
           value[expectedLength] == '\0';
}

bool FindDelayImportIatRva(
        const StobjectPeView& view,
        const char* dllName,
        const char* functionName,
        DWORD* iatRva) {
    if (!dllName || !functionName || !iatRva) {
        return false;
    }

    const auto& dir =
        view.nt->OptionalHeader
            .DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (!dir.VirtualAddress ||
        dir.Size < sizeof(DelayImportDescriptorRva) ||
        !IsPeRvaRangeValid(
            view,
            dir.VirtualAddress,
            dir.Size)) {
        return false;
    }

    auto descriptors =
        reinterpret_cast<const DelayImportDescriptorRva*>(
            view.base + dir.VirtualAddress);
    const size_t descriptorCount =
        dir.Size / sizeof(DelayImportDescriptorRva);

    for (size_t i = 0; i < descriptorCount; ++i) {
        const auto& descriptor = descriptors[i];

        if (descriptor.attributes == 0 &&
            descriptor.dllNameRva == 0 &&
            descriptor.importAddressTableRva == 0 &&
            descriptor.importNameTableRva == 0) {
            break;
        }

        // dlattrRva: descriptor fields are RVAs. Every harvested stobject.dll
        // sample uses this form; reject the legacy VA form rather than guessing.
        if ((descriptor.attributes & 1) == 0 ||
            !AsciiStringEqualsAtRva(
                view,
                descriptor.dllNameRva,
                dllName)) {
            continue;
        }

        if (!descriptor.importAddressTableRva ||
            !descriptor.importNameTableRva) {
            return false;
        }

        for (size_t thunkIndex = 0;
             thunkIndex < 512;
             ++thunkIndex) {
            const uint64_t thunkRva64 =
                static_cast<uint64_t>(
                    descriptor.importNameTableRva) +
                thunkIndex * sizeof(IMAGE_THUNK_DATA64);
            if (thunkRva64 > MAXDWORD ||
                !IsPeRvaRangeValid(
                    view,
                    static_cast<DWORD>(thunkRva64),
                    sizeof(IMAGE_THUNK_DATA64))) {
                return false;
            }

            auto thunk =
                reinterpret_cast<const IMAGE_THUNK_DATA64*>(
                    view.base +
                    static_cast<DWORD>(thunkRva64));
            const ULONGLONG value =
                thunk->u1.AddressOfData;
            if (value == 0) {
                break;
            }

            if (IMAGE_SNAP_BY_ORDINAL64(value)) {
                continue;
            }

            if (value > MAXDWORD ||
                !IsPeRvaRangeValid(
                    view,
                    static_cast<DWORD>(value),
                    sizeof(WORD) + 1)) {
                continue;
            }

            const uint64_t nameRva64 =
                value +
                offsetof(IMAGE_IMPORT_BY_NAME, Name);
            if (nameRva64 > MAXDWORD ||
                !AsciiStringEqualsAtRva(
                    view,
                    static_cast<DWORD>(nameRva64),
                    functionName)) {
                continue;
            }

            const uint64_t slotRva64 =
                static_cast<uint64_t>(
                    descriptor.importAddressTableRva) +
                thunkIndex * sizeof(void*);
            if (slotRva64 > MAXDWORD ||
                !IsPeRvaRangeValid(
                    view,
                    static_cast<DWORD>(slotRva64),
                    sizeof(void*))) {
                return false;
            }

            *iatRva =
                static_cast<DWORD>(slotRva64);
            return true;
        }

        return false;
    }

    return false;
}

std::vector<DWORD> FindIndirectCallSitesToIat(
        const StobjectPeView& view,
        DWORD iatRva) {
    std::vector<DWORD> calls;
    const auto* text = FindPeSectionByName(view, ".text");
    if (!text) {
        return calls;
    }

    const size_t span = PeSectionVirtualSpan(*text);
    if (span < 6 ||
        !IsPeRvaRangeValid(
            view,
            text->VirtualAddress,
            span)) {
        return calls;
    }

    const BYTE* bytes =
        view.base + text->VirtualAddress;

    for (size_t offset = 0; offset + 6 <= span; ++offset) {
        // CALL qword ptr [RIP+disp32]
        if (bytes[offset] != 0xFF ||
            bytes[offset + 1] != 0x15) {
            continue;
        }

        int32_t displacement = 0;
        memcpy(&displacement,
               bytes + offset + 2,
               sizeof(displacement));

        const int64_t callRva =
            static_cast<int64_t>(text->VirtualAddress) +
            static_cast<int64_t>(offset);
        const int64_t referencedRva =
            callRva + 6 + displacement;

        if (referencedRva == iatRva) {
            calls.push_back(
                static_cast<DWORD>(callRva));
        }
    }

    return calls;
}

void* ResolveStobjectBatteryStateStructurally(
        HMODULE module,
        const StobjectPeView& view) {
    DWORD queryBatteryDataIatRva = 0;
    if (!FindDelayImportIatRva(
            view,
            "BatMeter.dll",
            "QueryBatteryData",
            &queryBatteryDataIatRva)) {
        ModLog(L"Symbol-free Battery resolver: couldn't locate BatMeter!QueryBatteryData delay-IAT slot");
        return nullptr;
    }

    const auto callSites =
        FindIndirectCallSitesToIat(
            view,
            queryBatteryDataIatRva);

    std::vector<DWORD> candidates;

    for (DWORD callRva : callSites) {
        DWORD functionBegin = 0;
        DWORD functionEnd = 0;
        if (!FindContainingRuntimeFunction(
                view,
                callRva,
                &functionBegin,
                &functionEnd)) {
            continue;
        }

        const DWORD scanBegin =
            std::max<DWORD>(
                functionBegin,
                callRva > 128 ? callRva - 128 : 0);
        if (scanBegin >= callRva) {
            continue;
        }

        std::vector<DWORD> cbSizeTargets;
        std::vector<DWORD> secondArgumentTargets;

        for (DWORD cursor = scanBegin;
             cursor < callRva;
             ++cursor) {
            const size_t remaining =
                static_cast<size_t>(callRva - cursor);
            const BYTE* p = view.base + cursor;

            // mov dword ptr [rip+disp32], 0x80
            // stobject uses this to write BATTERY_STATE::cbSize.
            if (remaining >= 10 &&
                p[0] == 0xC7 &&
                p[1] == 0x05) {
                int32_t displacement = 0;
                DWORD immediate = 0;
                memcpy(&displacement, p + 2,
                       sizeof(displacement));
                memcpy(&immediate, p + 6,
                       sizeof(immediate));

                if (immediate == 0x80) {
                    const int64_t target =
                        static_cast<int64_t>(cursor) +
                        10 + displacement;
                    if (target >= 0 &&
                        target <= MAXDWORD &&
                        IsWritableNonExecutableRva(
                            view,
                            static_cast<DWORD>(target),
                            0x80)) {
                        PushUniqueRva(
                            &cbSizeTargets,
                            static_cast<DWORD>(target));
                    }
                }
            }

            // lea rdx,[rip+disp32]
            // QueryBatteryData's second argument is BATTERY_STATE*.
            if (remaining >= 7 &&
                p[0] == 0x48 &&
                p[1] == 0x8D &&
                p[2] == 0x15) {
                int32_t displacement = 0;
                memcpy(&displacement, p + 3,
                       sizeof(displacement));

                const int64_t target =
                    static_cast<int64_t>(cursor) +
                    7 + displacement;
                if (target >= 0 &&
                    target <= MAXDWORD &&
                    IsWritableNonExecutableRva(
                        view,
                        static_cast<DWORD>(target),
                        0x80)) {
                    PushUniqueRva(
                        &secondArgumentTargets,
                        static_cast<DWORD>(target));
                }
            }
        }

        for (DWORD candidate : cbSizeTargets) {
            if (std::find(
                    secondArgumentTargets.begin(),
                    secondArgumentTargets.end(),
                    candidate) !=
                secondArgumentTargets.end()) {
                PushUniqueRva(
                    &candidates,
                    candidate);
            }
        }
    }

    if (candidates.size() != 1) {
        ModLog(L"Symbol-free Battery resolver: QueryBatteryData IAT RVA=0x%X callSites=%u batteryStateCandidates=%u; failing closed",
               queryBatteryDataIatRva,
               static_cast<unsigned>(callSites.size()),
               static_cast<unsigned>(candidates.size()));
        return nullptr;
    }

    ModLog(L"Symbol-free Battery resolver: QueryBatteryData IAT RVA=0x%X g_bs/BATTERY_STATE RVA=0x%X",
           queryBatteryDataIatRva,
           candidates.front());

    return reinterpret_cast<BYTE*>(module) +
           candidates.front();
}


void* ResolveLaunchPowerCplStructurally(
        HMODULE module,
        const StobjectPeView& view) {
    const auto literals =
        FindWideLiteralRvas(
            view,
            L"Microsoft.PowerOptions");
    if (literals.size() != 1) {
        ModLog(L"Symbol-free Battery resolver: expected one Microsoft.PowerOptions literal, got %u",
               static_cast<unsigned>(literals.size()));
        return nullptr;
    }

    const auto xrefs =
        FindRipRelativeLeaXrefs(
            view,
            literals.front());

    std::vector<DWORD> functions;
    for (DWORD xref : xrefs) {
        DWORD begin = 0;
        DWORD end = 0;
        if (FindContainingRuntimeFunction(
                view,
                xref,
                &begin,
                &end)) {
            PushUniqueRva(&functions, begin);
        }
    }

    if (functions.size() != 1) {
        ModLog(L"Symbol-free Battery resolver: Microsoft.PowerOptions xrefs=%u functions=%u; LaunchPowerCPL left unresolved",
               static_cast<unsigned>(xrefs.size()),
               static_cast<unsigned>(functions.size()));
        return nullptr;
    }

    const DWORD launchPowerCplRva =
        functions.front();
    ModLog(L"Symbol-free Battery resolver: Microsoft.PowerOptions RVA=0x%X LaunchPowerCPL RVA=0x%X",
           literals.front(),
           launchPowerCplRva);

    return reinterpret_cast<BYTE*>(module) +
           launchPowerCplRva;
}

void* ResolveClassicBatteryFlyoutStructurally(
        HMODULE module) {
    StobjectPeView view{};
    if (!GetStobjectPeView(module, &view)) {
        ModLog(L"Symbol-free Battery resolver: invalid stobject.dll PE view");
        return nullptr;
    }

    void* showBatteryUi =
        ResolveShowBatteryUiStructurally(
            module,
            view);
    void* batteryState =
        ResolveStobjectBatteryStateStructurally(
            module,
            view);
    void* launchPowerCpl =
        ResolveLaunchPowerCplStructurally(
            module,
            view);

    if (!showBatteryUi || !batteryState) {
        return nullptr;
    }

    g_stobjectBatteryState.store(
        batteryState,
        std::memory_order_release);
    if (launchPowerCpl) {
        g_launchPowerCplTarget.store(
            launchPowerCpl,
            std::memory_order_release);
    }

    const auto showRva =
        reinterpret_cast<uintptr_t>(showBatteryUi) -
        reinterpret_cast<uintptr_t>(module);
    const auto stateRva =
        reinterpret_cast<uintptr_t>(batteryState) -
        reinterpret_cast<uintptr_t>(module);
    const auto launchRva =
        launchPowerCpl
            ? reinterpret_cast<uintptr_t>(launchPowerCpl) -
                  reinterpret_cast<uintptr_t>(module)
            : 0;

    ModLog(L"Symbol-free Battery resolver succeeded: ShowBatteryUI RVA=0x%llX g_bs/BATTERY_STATE RVA=0x%llX LaunchPowerCPL=%s0x%llX",
           static_cast<unsigned long long>(showRva),
           static_cast<unsigned long long>(stateRva),
           launchPowerCpl ? L"" : L"unresolved/",
           static_cast<unsigned long long>(launchRva));
    return showBatteryUi;
}

void* ResolveShowBatteryUiBySymbols(HMODULE module) {
    ModLog(L"Resolving stobject!ShowBatteryUI from symbols");

    WH_FIND_SYMBOL findData{};
    HANDLE search = Wh_FindFirstSymbol(module, nullptr, &findData);
    if (!search) {
        ModLog(L"Wh_FindFirstSymbol failed for stobject.dll; classic Battery flyout unavailable");
        return nullptr;
    }

    struct SearchGuard {
        HANDLE handle;
        ~SearchGuard() {
            if (handle) {
                Wh_FindCloseSymbol(handle);
            }
        }
    } guard{search};

    std::vector<void*> distinctTargets;
    std::vector<void*> distinctBatteryStates;
    std::vector<void*> distinctLaunchPowerTargets;
    unsigned rawMatchCount = 0;
    unsigned rawStateMatchCount = 0;
    unsigned rawLaunchPowerMatchCount = 0;

    do {
        if (IsShowBatteryUiSymbolName(findData.symbol)) {
            ++rawMatchCount;
            ModLog(L"stobject ShowBatteryUI symbol candidate: %s at %p",
                   findData.symbol ? findData.symbol : L"<null>",
                   findData.address);

            if (findData.address &&
                std::find(distinctTargets.begin(),
                          distinctTargets.end(),
                          findData.address) == distinctTargets.end()) {
                distinctTargets.push_back(findData.address);
            }
        }

        if (IsStobjectBatteryStateSymbolName(findData.symbol)) {
            ++rawStateMatchCount;
            ModLog(L"stobject g_bs symbol candidate: %s at %p",
                   findData.symbol ? findData.symbol : L"<null>",
                   findData.address);

            if (findData.address &&
                std::find(distinctBatteryStates.begin(),
                          distinctBatteryStates.end(),
                          findData.address) == distinctBatteryStates.end()) {
                distinctBatteryStates.push_back(findData.address);
            }
        }

        if (IsLaunchPowerCplSymbolName(findData.symbol)) {
            ++rawLaunchPowerMatchCount;
            ModLog(L"stobject LaunchPowerCPL symbol candidate: %s at %p",
                   findData.symbol ? findData.symbol : L"<null>",
                   findData.address);

            if (findData.address &&
                std::find(distinctLaunchPowerTargets.begin(),
                          distinctLaunchPowerTargets.end(),
                          findData.address) ==
                    distinctLaunchPowerTargets.end()) {
                distinctLaunchPowerTargets.push_back(findData.address);
            }
        }

    } while (Wh_FindNextSymbol(search, &findData));

    if (distinctTargets.size() != 1 ||
        distinctBatteryStates.size() != 1) {
        ModLog(L"Expected one distinct ShowBatteryUI and g_bs address; got ShowBatteryUI=%u raw/%u distinct, g_bs=%u raw/%u distinct; failing closed",
               rawMatchCount,
               static_cast<unsigned>(distinctTargets.size()),
               rawStateMatchCount,
               static_cast<unsigned>(distinctBatteryStates.size()));
        return nullptr;
    }

    g_stobjectBatteryState.store(distinctBatteryStates.front(),
                                 std::memory_order_release);

    if (distinctLaunchPowerTargets.size() == 1) {
        g_launchPowerCplTarget.store(distinctLaunchPowerTargets.front(),
                                     std::memory_order_release);
    } else if (rawLaunchPowerMatchCount != 0) {
        ModLog(L"LaunchPowerCPL symbol was ambiguous (%u raw/%u distinct); direct Advanced Power Options interception will remain unavailable",
               rawLaunchPowerMatchCount,
               static_cast<unsigned>(distinctLaunchPowerTargets.size()));
    } else {
        ModLog(L"LaunchPowerCPL symbol wasn't found; Battery flyout will still work but its native More power options path will be left untouched");
    }

    void* target = distinctTargets.front();
    const auto rva = reinterpret_cast<uintptr_t>(target) -
                     reinterpret_cast<uintptr_t>(module);
    ModLog(L"Resolved stobject!ShowBatteryUI RVA=0x%llX",
           static_cast<unsigned long long>(rva));
    return target;
}

void* ResolveClassicBatteryFlyoutTargetBlocking() {
    if (void* cached = g_showBatteryUiTarget.load()) {
        return cached;
    }

    HMODULE module = g_stobjectModule.load();
    if (!module) {
        module = GetModuleHandleW(L"stobject.dll");
        if (!module) {
            module = LoadLibraryExW(L"stobject.dll",
                                    nullptr,
                                    LOAD_LIBRARY_SEARCH_SYSTEM32);
        }
        if (!module) {
            ModLog(L"Couldn't load System32\\stobject.dll: error=%lu",
                   GetLastError());
            return nullptr;
        }
        g_stobjectModule.store(module);
    }

    DWORD timestamp = 0;
    DWORD sizeOfImage = 0;
    if (!GetLoadedModulePeIdentity(module, &timestamp, &sizeOfImage)) {
        ModLog(L"Couldn't read stobject.dll PE identity; failing closed");
        return nullptr;
    }

    ModLog(L"stobject.dll identity: timestamp=0x%08X size=0x%X",
           timestamp, sizeOfImage);

    void* target = nullptr;

    // Windows 11 25H2 build 26200.6584, supplied and statically verified.
    // PDB: stobject.pdb 41F4C92C-CD15-6B03-2BE5-DFA9211C4107 age 1.
    if (timestamp == 0x6EDCBE49 && sizeOfImage == 0x00060000) {
        constexpr uintptr_t kShowBatteryUiRva = 0x26614;
        constexpr uintptr_t kLaunchPowerCplRva = 0x21E14;
        constexpr uintptr_t kBatteryStateRva = 0x58780;
        target = reinterpret_cast<BYTE*>(module) + kShowBatteryUiRva;
        g_launchPowerCplTarget.store(
            reinterpret_cast<BYTE*>(module) + kLaunchPowerCplRva,
            std::memory_order_release);
        g_stobjectBatteryState.store(
            reinterpret_cast<BYTE*>(module) + kBatteryStateRva,
            std::memory_order_release);
        ModLog(L"Known stobject.dll fast path: ShowBatteryUI RVA=0x%llX LaunchPowerCPL RVA=0x%llX g_bs RVA=0x%llX",
               static_cast<unsigned long long>(kShowBatteryUiRva),
               static_cast<unsigned long long>(kLaunchPowerCplRva),
               static_cast<unsigned long long>(kBatteryStateRva));
    } else {
        target = ResolveClassicBatteryFlyoutStructurally(module);
        if (!target) {
            ModLog(L"Symbol-free Battery resolver couldn't validate this stobject.dll; falling back to symbols");
            target = ResolveShowBatteryUiBySymbols(module);
        }
    }

    if (target) {
        g_showBatteryUiTarget.store(target);
    }
    return target;
}

bool EnsureBatMeterBackendLoaded() {
    if (g_updateBatteryDataTarget.load(std::memory_order_acquire) &&
        g_queryBatteryDataTarget.load(std::memory_order_acquire)) {
        return true;
    }

    HMODULE module = g_batMeterModule.load(std::memory_order_acquire);
    if (!module) {
        module = GetModuleHandleW(L"BatMeter.dll");
        if (!module) {
            module = LoadLibraryExW(L"BatMeter.dll",
                                    nullptr,
                                    LOAD_LIBRARY_SEARCH_SYSTEM32);
        }
        if (!module) {
            ModLog(L"Couldn't load System32\\BatMeter.dll: error=%lu",
                   GetLastError());
            return false;
        }
        g_batMeterModule.store(module, std::memory_order_release);
    }

    void* update = reinterpret_cast<void*>(
        GetProcAddress(module, "UpdateBatteryData"));
    void* query = reinterpret_cast<void*>(
        GetProcAddress(module, "QueryBatteryData"));
    void* cleanup = reinterpret_cast<void*>(
        GetProcAddress(module, "CleanupBatteryData"));

    if (!update || !query) {
        ModLog(L"BatMeter.dll is missing UpdateBatteryData/QueryBatteryData exports; refusing classic Battery backend initialization");
        return false;
    }

    g_updateBatteryDataTarget.store(update, std::memory_order_release);
    g_queryBatteryDataTarget.store(query, std::memory_order_release);
    g_cleanupBatteryDataTarget.store(cleanup, std::memory_order_release);

    ModLog(L"BatMeter backend resolved: UpdateBatteryData=%p QueryBatteryData=%p CleanupBatteryData=%p",
           update, query, cleanup);
    return true;
}

bool PrimeClassicBatteryStateBlocking(DWORD waitMilliseconds) {
    void* state = g_stobjectBatteryState.load(std::memory_order_acquire);
    if (!state) {
        ModLog(L"Classic Battery state prime unavailable: stobject!g_bs not resolved");
        return false;
    }

    if (!EnsureBatMeterBackendLoaded()) {
        return false;
    }

    // Reverse-confirmed on the supplied 25H2 build:
    // BATTERY_STATE begins with cbSize=0x80, and stobject itself writes 0x80
    // before calling BatMeter!QueryBatteryData.
    *reinterpret_cast<DWORD*>(state) = 0x80;

    using UpdateBatteryData_t = BOOL(WINAPI*)(DWORD*, void*, DWORD);
    auto updateBatteryData =
        reinterpret_cast<UpdateBatteryData_t>(
            g_updateBatteryDataTarget.load(std::memory_order_acquire));

    const BOOL ok = updateBatteryData(nullptr, state, waitMilliseconds);
    ModLog(L"BatMeter UpdateBatteryData(g_bs, wait=%lu) returned %d",
           waitMilliseconds, static_cast<int>(ok));
    return ok != FALSE;
}

bool RefreshClassicBatteryStateFromCache() {
    void* state = g_stobjectBatteryState.load(std::memory_order_acquire);
    void* queryTarget =
        g_queryBatteryDataTarget.load(std::memory_order_acquire);
    if (!state || !queryTarget) {
        return false;
    }

    *reinterpret_cast<DWORD*>(state) = 0x80;

    using QueryBatteryData_t = BOOL(WINAPI*)(DWORD*, void*);
    auto queryBatteryData =
        reinterpret_cast<QueryBatteryData_t>(queryTarget);

    const BOOL ok = queryBatteryData(nullptr, state);
    ModLog(L"BatMeter QueryBatteryData(g_bs) returned %d",
           static_cast<int>(ok));
    return ok != FALSE;
}

DWORD WINAPI BatteryFlyoutResolverThreadProc(void*) {
    ModLog(L"Battery flyout background resolver started");

    void* target = ResolveClassicBatteryFlyoutTargetBlocking();
    const bool statePrimed =
        target && PrimeClassicBatteryStateBlocking(500);

    if (target && statePrimed) {
        g_batteryFlyoutResolveState.store(
            BatteryFlyoutResolveState::Ready,
            std::memory_order_release);
        ModLog(L"Battery flyout prefetch ready: target=%p state=%p",
               target,
               g_stobjectBatteryState.load(std::memory_order_acquire));
    } else {
        g_batteryFlyoutResolveState.store(
            BatteryFlyoutResolveState::Failed,
            std::memory_order_release);
        ModLog(L"Battery flyout background resolver failed; classic Battery click will remain unavailable");
    }

    return 0;
}

void StartBatteryFlyoutSymbolPrefetch() {
    if (!g_showBatteryButton.load(std::memory_order_relaxed) ||
        g_batteryClickAction.load(std::memory_order_relaxed) ==
            BatteryClickAction::None) {
        return;
    }

    BatteryFlyoutResolveState expected = BatteryFlyoutResolveState::NotStarted;
    if (!g_batteryFlyoutResolveState.compare_exchange_strong(
            expected,
            BatteryFlyoutResolveState::Resolving,
            std::memory_order_acq_rel)) {
        return;
    }

    g_batteryResolverThread = CreateThread(
        nullptr,
        0,
        BatteryFlyoutResolverThreadProc,
        nullptr,
        0,
        nullptr);

    if (!g_batteryResolverThread) {
        const DWORD error = GetLastError();
        g_batteryFlyoutResolveState.store(
            BatteryFlyoutResolveState::Failed,
            std::memory_order_release);
        ModLog(L"Couldn't start Battery flyout resolver worker: error=%lu",
               error);
        return;
    }

    ModLog(L"Battery flyout resolver queued immediately; UI thread will never wait for symbols");
}

struct BatMeterFlyoutSearch {
    HWND hwnd = nullptr;
};

BOOL CALLBACK EnumBatMeterFlyoutWindow(HWND hwnd, LPARAM lParam) {
    auto* search = reinterpret_cast<BatMeterFlyoutSearch*>(lParam);
    if (!search || search->hwnd) {
        return FALSE;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId != GetCurrentProcessId()) {
        return TRUE;
    }

    wchar_t className[128]{};
    if (!GetClassNameW(hwnd, className, ARRAYSIZE(className))) {
        return TRUE;
    }

    if (_wcsicmp(className, L"BatMeterFlyout") == 0) {
        search->hwnd = hwnd;
        return FALSE;
    }

    return TRUE;
}

HWND FindBatMeterFlyoutWindow() {
    BatMeterFlyoutSearch search{};

    // stobject creates the flyout synchronously on the caller's UI thread on
    // the verified builds, so search that thread first.
    EnumThreadWindows(GetCurrentThreadId(),
                      EnumBatMeterFlyoutWindow,
                      reinterpret_cast<LPARAM>(&search));
    if (search.hwnd) {
        return search.hwnd;
    }

    // Fallback in case another supported stobject build owns it on a helper
    // thread in the same LogonUI process.
    EnumWindows(EnumBatMeterFlyoutWindow,
                reinterpret_cast<LPARAM>(&search));
    return search.hwnd;
}

bool TryGetXamlElementScreenRect(
        FrameworkElement const& element,
        RECT* screenRect,
        const wchar_t* logPrefix) {
    if (!element || !screenRect) {
        return false;
    }

    if (!logPrefix) {
        logPrefix = L"XAML element";
    }

    try {
        auto window = Window::Current();
        if (!window) {
            ModLog(L"%s anchor: Window::Current is null", logPrefix);
            return false;
        }

        auto root = window.Content().try_as<FrameworkElement>();
        if (!root) {
            ModLog(L"%s anchor: XAML Window has no FrameworkElement root",
                   logPrefix);
            return false;
        }

        const double rootWidth = root.ActualWidth();
        const double rootHeight = root.ActualHeight();
        const double elementWidth = element.ActualWidth();
        const double elementHeight = element.ActualHeight();
        if (rootWidth <= 0.0 || rootHeight <= 0.0 ||
            elementWidth <= 0.0 || elementHeight <= 0.0) {
            ModLog(L"%s anchor: invalid XAML geometry root=%.1fx%.1f button=%.1fx%.1f",
                   logPrefix,
                   rootWidth,
                   rootHeight,
                   elementWidth,
                   elementHeight);
            return false;
        }

        auto transform = element.TransformToVisual(root);
        const auto origin = transform.TransformPoint(
            winrt::Windows::Foundation::Point{0.0f, 0.0f});

        HWND host = GetForegroundWindow();
        DWORD processId = 0;
        if (host) {
            GetWindowThreadProcessId(host, &processId);
        }
        if (!host || processId != GetCurrentProcessId()) {
            host = GetActiveWindow();
            processId = 0;
            if (host) {
                GetWindowThreadProcessId(host, &processId);
            }
        }
        if (!host || processId != GetCurrentProcessId()) {
            ModLog(L"%s anchor: couldn't identify foreground LogonUI host HWND",
                   logPrefix);
            return false;
        }

        RECT client{};
        if (!GetClientRect(host, &client)) {
            ModLog(L"%s anchor: GetClientRect failed error=%lu",
                   logPrefix,
                   GetLastError());
            return false;
        }

        POINT clientOrigin{0, 0};
        if (!ClientToScreen(host, &clientOrigin)) {
            ModLog(L"%s anchor: ClientToScreen failed error=%lu",
                   logPrefix,
                   GetLastError());
            return false;
        }

        const double clientWidth =
            static_cast<double>(client.right - client.left);
        const double clientHeight =
            static_cast<double>(client.bottom - client.top);
        if (clientWidth <= 0.0 || clientHeight <= 0.0) {
            return false;
        }

        const double scaleX = clientWidth / rootWidth;
        const double scaleY = clientHeight / rootHeight;

        const LONG left = clientOrigin.x +
            static_cast<LONG>(std::lround(origin.X * scaleX));
        const LONG top = clientOrigin.y +
            static_cast<LONG>(std::lround(origin.Y * scaleY));
        const LONG right = left +
            static_cast<LONG>(std::lround(elementWidth * scaleX));
        const LONG bottom = top +
            static_cast<LONG>(std::lround(elementHeight * scaleY));

        *screenRect = RECT{left, top, right, bottom};

        ModLog(L"%s anchor rect=(%ld,%ld)-(%ld,%ld) XAML-to-screen scale=%.3fx%.3f",
               logPrefix,
               left,
               top,
               right,
               bottom,
               scaleX,
               scaleY);
        return true;
    } catch (const winrt::hresult_error& e) {
        ModLog(L"%s anchor geometry failed: 0x%08X %s",
               logPrefix,
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"%s anchor geometry failed with unknown exception",
               logPrefix);
    }

    return false;
}

bool RepositionWindowToAnchor(
        HWND hwnd,
        const RECT& anchorRect,
        AnchorSide side,
        int offsetX,
        int offsetY,
        const wchar_t* logPrefix) {
    if (!hwnd) {
        return false;
    }

    RECT windowRect{};
    if (!GetWindowRect(hwnd, &windowRect)) {
        ModLog(L"%s anchor: GetWindowRect failed error=%lu",
               logPrefix, GetLastError());
        return false;
    }

    const LONG width = windowRect.right - windowRect.left;
    const LONG height = windowRect.bottom - windowRect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    HMONITOR monitor =
        MonitorFromRect(&anchorRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
        ModLog(L"%s anchor: couldn't obtain monitor bounds", logPrefix);
        return false;
    }

    const RECT bounds = monitorInfo.rcMonitor;
    const LONG anchorHeight = anchorRect.bottom - anchorRect.top;
    const LONG gap = std::max<LONG>(4, anchorHeight / 6);

    LONG x = anchorRect.right - width;
    LONG yAbove = anchorRect.top - height - gap;
    LONG yBelow = anchorRect.bottom + gap;
    LONG y = yAbove;

    switch (side) {
        case AnchorSide::Above:
            y = yAbove;
            break;
        case AnchorSide::Below:
            y = yBelow;
            break;
        case AnchorSide::Automatic:
        default:
            y = (yAbove >= bounds.top) ? yAbove : yBelow;
            break;
    }

    x += offsetX;
    y += offsetY;

    x = std::clamp(
        x,
        bounds.left,
        std::max(bounds.left, bounds.right - width));
    y = std::clamp(
        y,
        bounds.top,
        std::max(bounds.top, bounds.bottom - height));

    if (!SetWindowPos(
            hwnd,
            nullptr,
            x,
            y,
            0,
            0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                SWP_NOOWNERZORDER)) {
        ModLog(L"%s anchor: SetWindowPos failed error=%lu",
               logPrefix, GetLastError());
        return false;
    }

    ModLog(L"%s repositioned: hwnd=%p side=%s offset=(%d,%d) pos=(%ld,%ld) size=%ldx%ld",
           logPrefix,
           hwnd,
           AnchorSideName(side),
           offsetX,
           offsetY,
           x,
           y,
           width,
           height);
    return true;
}

bool RepositionClassicBatteryFlyout(const RECT& anchorRect) {
    HWND flyout = FindBatMeterFlyoutWindow();
    if (!flyout) {
        ModLog(L"Battery flyout anchor: couldn't find BatMeterFlyout HWND");
        return false;
    }

    return RepositionWindowToAnchor(
        flyout,
        anchorRect,
        g_batteryFlyoutAnchorSide.load(std::memory_order_relaxed),
        g_batteryFlyoutOffsetX.load(std::memory_order_relaxed),
        g_batteryFlyoutOffsetY.load(std::memory_order_relaxed),
        L"Battery flyout");
}

using LaunchPowerCPL_t = HRESULT(WINAPI*)();
LaunchPowerCPL_t g_launchPowerCplOriginal = nullptr;

thread_local bool g_advancedPowerMainDialogPositioned = false;

bool GetCurrentBatteryAnchorRect(RECT* rect) {
    if (!rect ||
        !g_batteryAnchorValid.load(std::memory_order_acquire)) {
        return false;
    }

    AcquireSRWLockShared(&g_batteryAnchorLock);
    *rect = g_batteryAnchorRect;
    ReleaseSRWLockShared(&g_batteryAnchorLock);
    return true;
}


LRESULT CALLBACK AdvancedPowerCbtHookProc(
        int code,
        WPARAM wParam,
        LPARAM lParam) {
    if (code == HCBT_ACTIVATE &&
        !g_advancedPowerMainDialogPositioned &&
        g_advancedPowerPlacement.load(std::memory_order_relaxed) ==
            AdvancedPowerPlacementMode::AnchorToBatteryButton) {
        HWND hwnd = reinterpret_cast<HWND>(wParam);

        wchar_t className[64]{};
        if (hwnd &&
            GetClassNameW(hwnd, className, ARRAYSIZE(className)) &&
            _wcsicmp(className, L"#32770") == 0 &&
            !(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)) {
            RECT anchorRect{};
            if (GetCurrentBatteryAnchorRect(&anchorRect)) {
                wchar_t title[256]{};
                GetWindowTextW(hwnd, title, ARRAYSIZE(title));
                ModLog(L"Advanced Power Options dialog activated: hwnd=%p title=%s",
                       hwnd,
                       title[0] ? title : L"<none>");

                if (RepositionWindowToAnchor(
                        hwnd,
                        anchorRect,
                        g_advancedPowerAnchorSide.load(
                            std::memory_order_relaxed),
                        g_advancedPowerOffsetX.load(
                            std::memory_order_relaxed),
                        g_advancedPowerOffsetY.load(
                            std::memory_order_relaxed),
                        L"Advanced Power Options")) {
                    g_advancedPowerMainDialogPositioned = true;
                }
            } else {
                ModLog(L"Advanced Power Options anchor requested but Battery button geometry isn't cached");
            }
        }
    }

    return CallNextHookEx(nullptr, code, wParam, lParam);
}

DWORD WINAPI AdvancedPowerOptionsThreadProc(void*) {
    g_advancedPowerDialogActive.store(true, std::memory_order_release);

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitializeCom =
        SUCCEEDED(comHr);

    wchar_t systemDir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir));
    if (!length || length >= ARRAYSIZE(systemDir)) {
        ModLog(L"Advanced Power Options: GetSystemDirectoryW failed error=%lu",
               GetLastError());
        if (uninitializeCom) {
            CoUninitialize();
        }
        g_advancedPowerDialogActive.store(false, std::memory_order_release);
        return 0;
    }

    std::wstring path = systemDir;
    if (!path.empty() && path.back() != L'\\') {
        path.push_back(L'\\');
    }
    path += L"powercfg.cpl";

    HMODULE module = LoadLibraryExW(
        path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        ModLog(L"Advanced Power Options: couldn't load %s error=%lu",
               path.c_str(), GetLastError());
        if (uninitializeCom) {
            CoUninitialize();
        }
        g_advancedPowerDialogActive.store(false, std::memory_order_release);
        return 0;
    }

    using CPlApplet_t =
        LONG(CALLBACK*)(HWND, UINT, LPARAM, LPARAM);
    auto cplApplet = reinterpret_cast<CPlApplet_t>(
        GetProcAddress(module, "CPlApplet"));
    if (!cplApplet) {
        ModLog(L"Advanced Power Options: powercfg.cpl has no CPlApplet export");
        FreeLibrary(module);
        if (uninitializeCom) {
            CoUninitialize();
        }
        g_advancedPowerDialogActive.store(false, std::memory_order_release);
        return 0;
    }

    constexpr UINT kCplInit = 1;
    constexpr UINT kCplGetCount = 2;
    constexpr UINT kCplStop = 6;
    constexpr UINT kCplExit = 7;
    constexpr UINT kCplStartWParms = 10;

    LONG initResult = cplApplet(nullptr, kCplInit, 0, 0);
    LONG count = initResult ? cplApplet(nullptr, kCplGetCount, 0, 0) : 0;

    ModLog(L"Advanced Power Options: CPlApplet init=%ld count=%ld",
           initResult, count);

    g_advancedPowerMainDialogPositioned = false;
    HHOOK placementHook = nullptr;
    if (g_advancedPowerPlacement.load(std::memory_order_relaxed) ==
            AdvancedPowerPlacementMode::AnchorToBatteryButton) {
        placementHook = SetWindowsHookExW(
            WH_CBT,
            AdvancedPowerCbtHookProc,
            nullptr,
            GetCurrentThreadId());
        if (!placementHook) {
            ModLog(L"Advanced Power Options: couldn't install CBT placement hook error=%lu",
                   GetLastError());
        }
    }

    if (initResult && count > 0) {
        // Equivalent intent to the classic "control powercfg.cpl,,1" path,
        // but without control.exe / Explorer's Control Panel COM namespace.
        wchar_t startParameters[] = L"1";
        LONG result = cplApplet(
            nullptr,
            kCplStartWParms,
            0,
            reinterpret_cast<LPARAM>(startParameters));
        ModLog(L"Advanced Power Options: CPL_STARTWPARMSW(\"1\") returned %ld",
               result);

        cplApplet(nullptr, kCplStop, 0, 0);
    }

    if (placementHook) {
        UnhookWindowsHookEx(placementHook);
        placementHook = nullptr;
    }

    if (initResult) {
        cplApplet(nullptr, kCplExit, 0, 0);
    }

    FreeLibrary(module);

    if (uninitializeCom) {
        CoUninitialize();
    }

    g_advancedPowerDialogActive.store(false, std::memory_order_release);
    ModLog(L"Advanced Power Options host thread exited");
    return 0;
}

bool LaunchAdvancedPowerOptionsDirect() {
    bool expected = false;
    if (!g_advancedPowerDialogActive.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        ModLog(L"Advanced Power Options is already active; duplicate launch ignored");
        return true;
    }

    // The worker sets this again on entry; pre-setting it closes the race
    // between rapid repeated clicks and thread startup.
    HANDLE thread = CreateThread(
        nullptr,
        0,
        AdvancedPowerOptionsThreadProc,
        nullptr,
        0,
        nullptr);
    if (!thread) {
        g_advancedPowerDialogActive.store(false, std::memory_order_release);
        ModLog(L"Advanced Power Options: CreateThread failed error=%lu",
               GetLastError());
        return false;
    }

    if (g_advancedPowerThread) {
        CloseHandle(g_advancedPowerThread);
    }
    g_advancedPowerThread = thread;

    ModLog(L"Advanced Power Options direct host queued");
    return true;
}

HRESULT WINAPI LaunchPowerCPL_Hook() {
    if (g_unloading.load(std::memory_order_acquire)) {
        return g_launchPowerCplOriginal ? g_launchPowerCplOriginal() : E_FAIL;
    }

    const auto action =
        g_morePowerOptionsAction.load(std::memory_order_relaxed);

    if (action == MorePowerOptionsAction::WindowsDefault) {
        if (g_launchPowerCplOriginal) {
            return g_launchPowerCplOriginal();
        }
        return E_FAIL;
    }

    return LaunchAdvancedPowerOptionsDirect() ? S_OK : E_FAIL;
}

bool EnsureLaunchPowerCplHookInstalled() {
    if (g_morePowerOptionsAction.load(std::memory_order_relaxed) ==
        MorePowerOptionsAction::WindowsDefault) {
        return true;
    }

    if (g_launchPowerCplHookInstalled.load(std::memory_order_acquire)) {
        return true;
    }

    void* target =
        g_launchPowerCplTarget.load(std::memory_order_acquire);
    if (!target) {
        ModLog(L"LaunchPowerCPL hook unavailable: target not resolved yet");
        return false;
    }

    bool expected = false;
    if (!g_launchPowerCplHookInstalled.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return true;
    }

    if (!Wh_SetFunctionHook(
            target,
            reinterpret_cast<void*>(LaunchPowerCPL_Hook),
            reinterpret_cast<void**>(&g_launchPowerCplOriginal))) {
        g_launchPowerCplHookInstalled.store(false,
                                            std::memory_order_release);
        ModLog(L"Wh_SetFunctionHook failed for stobject!LaunchPowerCPL");
        return false;
    }

    Wh_ApplyHookOperations();
    ModLog(L"Hooked stobject!LaunchPowerCPL -> direct powercfg.cpl host");
    return true;
}

std::wstring FormatPercentageText(
        int percent,
        bool volume,
        bool muted = false);

void SetPercentageLabelText(
        Controls::TextBlock const& label,
        const std::wstring& value,
        bool featureEnabled);

constexpr UINT kBatteryUiRefreshMessage = WM_APP + 0x431;
constexpr UINT kBatteryUiPercentMessage = WM_APP + 0x432;
constexpr wchar_t kBatteryUiBridgeClass[] =
    L"Windhawk.LogonUI.BatteryVolumeControls.BatteryUiBridge.v2";
constexpr wchar_t kBatteryNotifyClass[] =
    L"Windhawk.LogonUI.BatteryVolumeControls.BatteryNotify.v2";

const GUID kGuidAcDcPowerSource =
    {0x5D3E9A59, 0xE9D5, 0x4B00,
     {0xA6, 0xBD, 0xFF, 0x34, 0xFF, 0x51, 0x65, 0x48}};
const GUID kGuidBatteryPercentageRemaining =
    {0xA7AD8041, 0xB45A, 0x4CAE,
     {0x87, 0xA3, 0xEE, 0xCB, 0xB4, 0x68, 0xA9, 0xE1}};

void QueueBatteryPercentUiUpdate(BYTE percent) {
    HWND bridge = g_batteryUiBridgeWindow.load(std::memory_order_acquire);
    if (bridge) {
        PostMessageW(
            bridge,
            kBatteryUiPercentMessage,
            static_cast<WPARAM>(percent),
            0);
    }
}

void ApplyBatteryPercentageToCurrentLabel(BYTE percent) {
    auto label = g_batteryPercentText.get();
    if (!label) {
        return;
    }

    try {
        const bool enabled =
            g_showBatteryPercentage.load(std::memory_order_relaxed);
        const std::wstring value =
            (percent <= 100)
                ? FormatPercentageText(
                      static_cast<int>(percent), false, false)
                : std::wstring{};
        SetPercentageLabelText(label, value, enabled);
    } catch (...) {
    }
}

void QueueBatteryFlyoutUiRefresh() {
    if (g_batteryFlyoutRefresh.load(std::memory_order_relaxed) !=
        BatteryFlyoutRefreshMode::EventDriven) {
        return;
    }

    HWND bridge = g_batteryUiBridgeWindow.load(std::memory_order_acquire);
    if (bridge) {
        PostMessageW(bridge, kBatteryUiRefreshMessage, 0, 0);
    }
}

bool EnsurePrivateProcessWindowClass(
        const wchar_t* className, WNDPROC wndProc) {
    if (!className || !*className || !wndProc) {
        return false;
    }

    HINSTANCE owner = GetModuleHandleW(nullptr);
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(owner, className, &existing)) {
        if (existing.lpfnWndProc == wndProc) {
            return true;
        }

        // Never accept ERROR_CLASS_ALREADY_EXISTS blindly. A previous hot-
        // loaded mod DLL can be gone while the process-local class still owns
        // its old WNDPROC. Reusing that class causes USER32/CFG to jump into
        // the unloaded DLL (or into unrelated code at the same remapped RVA).
        ModLog(L"Window class %s exists with a stale/different WNDPROC; attempting safe unregister before reuse",
               className);
        if (!UnregisterClassW(className, owner)) {
            ModLog(L"Window class %s couldn't be safely replaced (UnregisterClass error=%lu); refusing to create a window with a stale callback",
                   className, GetLastError());
            return false;
        }
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = owner;
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc)) {
        ModLog(L"RegisterClassEx failed for %s: %lu",
               className, GetLastError());
        return false;
    }
    return true;
}

void UnregisterPrivateProcessWindowClass(
        const wchar_t* className, WNDPROC wndProc) {
    if (!className || !*className || !wndProc) {
        return;
    }

    HINSTANCE owner = GetModuleHandleW(nullptr);
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (!GetClassInfoExW(owner, className, &existing)) {
        return;
    }

    // Don't unregister someone else's class if a future Windows/mod change
    // happens to reuse the name.
    if (existing.lpfnWndProc != wndProc) {
        ModLog(L"Refusing to unregister window class %s because its WNDPROC isn't ours",
               className);
        return;
    }

    if (!UnregisterClassW(className, owner)) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_DOES_NOT_EXIST) {
            ModLog(L"UnregisterClass failed for %s: %lu", className, error);
        }
    } else {
        ModLog(L"Unregistered private window class %s", className);
    }
}

LRESULT CALLBACK BatteryUiBridgeWndProc(
        HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kBatteryUiPercentMessage) {
        ApplyBatteryPercentageToCurrentLabel(
            static_cast<BYTE>(wParam & 0xFF));
        return 0;
    }

    if (message == kBatteryUiRefreshMessage) {
        if (g_batteryFlyoutRefresh.load(std::memory_order_relaxed) !=
            BatteryFlyoutRefreshMode::EventDriven) {
            return 0;
        }

        HWND flyout = FindBatMeterFlyoutWindow();
        if (!flyout || !IsWindowVisible(flyout)) {
            return 0;
        }

        void* showTarget =
            g_showBatteryUiTarget.load(std::memory_order_acquire);
        if (!showTarget) {
            ModLog(L"Battery live refresh skipped: stobject!ShowBatteryUI isn't resolved");
            return 0;
        }

        // Important: don't call stobject!UpdateFlyoutUI directly here.
        // It is a low-level population routine invoked by Flyout_UpdateUI
        // after the native hide/cleanup path. Re-entering it against an
        // already-populated flyout appends duplicate controls and grows the
        // window. ShowBatteryUI performs the genuine hide + rebuild sequence.
        using ShowBatteryUI_t = HRESULT(WINAPI*)();
        auto showBatteryUI =
            reinterpret_cast<ShowBatteryUI_t>(showTarget);
        const HRESULT hr = showBatteryUI();

        RECT anchorRect{};
        if (g_batteryFlyoutPlacement.load(std::memory_order_relaxed) ==
                BatteryFlyoutPlacementMode::AnchorToButton &&
            GetCurrentBatteryAnchorRect(&anchorRect)) {
            RepositionClassicBatteryFlyout(anchorRect);
        }

        ModLog(L"Battery flyout event-refresh rebuilt native UI; ShowBatteryUI returned 0x%08X",
               static_cast<unsigned>(hr));
        return 0;
    }

    if (message == WM_NCDESTROY) {
        if (g_batteryUiBridgeWindow.load() == hwnd) {
            g_batteryUiBridgeWindow.store(nullptr);
        }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND EnsureBatteryUiBridgeWindow() {
    if (HWND existing =
            g_batteryUiBridgeWindow.load(std::memory_order_acquire)) {
        return existing;
    }

    if (!EnsurePrivateProcessWindowClass(
            kBatteryUiBridgeClass, BatteryUiBridgeWndProc)) {
        ModLog(L"Battery UI bridge class unavailable; UI bridge disabled for this mod instance");
        return nullptr;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kBatteryUiBridgeClass,
        L"",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!hwnd) {
        ModLog(L"Battery UI bridge CreateWindowEx failed: %lu",
               GetLastError());
        return nullptr;
    }

    g_batteryUiBridgeWindow.store(hwnd, std::memory_order_release);
    ModLog(L"Battery UI bridge created on LogonUI UI thread=%lu",
           GetCurrentThreadId());
    return hwnd;
}

constexpr UINT_PTR kBatteryRefreshTimerId = 1;

void HandleBatteryEventAfterDebounce() {
    if (g_showBatteryPercentage.load(std::memory_order_relaxed)) {
        SYSTEM_POWER_STATUS status{};
        if (GetSystemPowerStatus(&status)) {
            QueueBatteryPercentUiUpdate(status.BatteryLifePercent);
        }
    }

    if (g_batteryFlyoutRefresh.load(std::memory_order_relaxed) !=
        BatteryFlyoutRefreshMode::EventDriven) {
        return;
    }

    HWND flyout = FindBatMeterFlyoutWindow();
    if (!flyout || !IsWindowVisible(flyout)) {
        return;
    }

    // QueryBatteryData only copies BatMeter's current cache. Force a fresh
    // BatMeter read once after the Windows event, then rebuild the genuine
    // classic flyout on the LogonUI UI thread.
    if (!PrimeClassicBatteryStateBlocking(1000)) {
        ModLog(L"Battery live refresh skipped: fresh UpdateBatteryData failed");
        return;
    }

    QueueBatteryFlyoutUiRefresh();
}

LRESULT CALLBACK BatteryNotifyWndProc(
        HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_POWERBROADCAST) {
        if (wParam == PBT_APMPOWERSTATUSCHANGE ||
            wParam == PBT_POWERSETTINGCHANGE) {
            const bool needsBatteryEvent =
                g_showBatteryPercentage.load(std::memory_order_relaxed) ||
                g_batteryFlyoutRefresh.load(std::memory_order_relaxed) ==
                    BatteryFlyoutRefreshMode::EventDriven;
            if (needsBatteryEvent) {
                const DWORD delay =
                    g_batteryEventDebounceDelayMs.load(
                        std::memory_order_relaxed);

                if (delay == 0) {
                    HandleBatteryEventAfterDebounce();
                } else {
                    // Reusing the same timer ID coalesces bursts of AC/DC +
                    // percentage notifications into one fresh-data rebuild.
                    if (!SetTimer(
                            hwnd,
                            kBatteryRefreshTimerId,
                            delay,
                            nullptr)) {
                        ModLog(L"Battery event debounce SetTimer failed error=%lu; handling immediately",
                               GetLastError());
                        HandleBatteryEventAfterDebounce();
                    }
                }
            }
        }
        return TRUE;
    }

    if (message == WM_TIMER &&
        wParam == kBatteryRefreshTimerId) {
        KillTimer(hwnd, kBatteryRefreshTimerId);
        HandleBatteryEventAfterDebounce();
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

DWORD WINAPI BatteryNotificationThreadProc(void*) {
    if (!EnsurePrivateProcessWindowClass(
            kBatteryNotifyClass, BatteryNotifyWndProc)) {
        ModLog(L"Battery notification class unavailable; watcher disabled for this mod instance");
        return 0;
    }

    // Hidden top-level window: unlike HWND_MESSAGE, it can also receive the
    // general PBT_APMPOWERSTATUSCHANGE broadcast in addition to registered
    // power-setting notifications.
    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kBatteryNotifyClass,
        L"",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!hwnd) {
        ModLog(L"Battery notification CreateWindowEx failed: %lu",
               GetLastError());
        return 0;
    }

    HPOWERNOTIFY acNotify =
        RegisterPowerSettingNotification(
            hwnd, &kGuidAcDcPowerSource, DEVICE_NOTIFY_WINDOW_HANDLE);
    HPOWERNOTIFY percentNotify =
        RegisterPowerSettingNotification(
            hwnd, &kGuidBatteryPercentageRemaining,
            DEVICE_NOTIFY_WINDOW_HANDLE);

    ModLog(L"Battery notification watcher started (event-driven)");

    while (true) {
        DWORD wait = MsgWaitForMultipleObjects(
            1, &g_stopEvent, FALSE, INFINITE, QS_ALLINPUT);

        if (wait == WAIT_OBJECT_0) {
            break;
        }

        if (wait == WAIT_OBJECT_0 + 1) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            break;
        }
    }

    if (acNotify) {
        UnregisterPowerSettingNotification(acNotify);
    }
    if (percentNotify) {
        UnregisterPowerSettingNotification(percentNotify);
    }
    DestroyWindow(hwnd);
    UnregisterPrivateProcessWindowClass(
        kBatteryNotifyClass, BatteryNotifyWndProc);

    ModLog(L"Battery notification watcher stopped");
    return 0;
}

void EnsureBatteryNotificationWatcherStarted() {
    const bool needed =
        g_showBatteryButton.load(std::memory_order_relaxed) &&
        (g_showBatteryPercentage.load(std::memory_order_relaxed) ||
         g_batteryFlyoutRefresh.load(std::memory_order_relaxed) ==
             BatteryFlyoutRefreshMode::EventDriven);

    if (!needed || g_batteryNotificationThread) {
        return;
    }

    HANDLE thread = CreateThread(
        nullptr, 0, BatteryNotificationThreadProc, nullptr, 0, nullptr);
    if (!thread) {
        ModLog(L"Battery notification watcher CreateThread failed: %lu",
               GetLastError());
        return;
    }

    g_batteryNotificationThread = thread;
}

bool ShowClassicBatteryFlyout(FrameworkElement const& anchorElement) {
    if (g_batteryClickAction.load(std::memory_order_relaxed) ==
        BatteryClickAction::None) {
        ModLog(L"Battery click ignored by setting");
        return false;
    }

    RECT anchorRect{};
    const bool shouldAnchor =
        g_batteryFlyoutPlacement.load(std::memory_order_relaxed) ==
        BatteryFlyoutPlacementMode::AnchorToButton;
    const bool haveAnchorRect =
        shouldAnchor &&
        TryGetXamlElementScreenRect(anchorElement, &anchorRect, L"Battery flyout");

    if (haveAnchorRect) {
        AcquireSRWLockExclusive(&g_batteryAnchorLock);
        g_batteryAnchorRect = anchorRect;
        ReleaseSRWLockExclusive(&g_batteryAnchorLock);
        g_batteryAnchorValid.store(true, std::memory_order_release);
    }

    void* target = g_showBatteryUiTarget.load(std::memory_order_acquire);
    if (!target) {
        const auto state =
            g_batteryFlyoutResolveState.load(std::memory_order_acquire);

        if (state == BatteryFlyoutResolveState::NotStarted) {
            StartBatteryFlyoutSymbolPrefetch();
            ModLog(L"Classic Battery flyout target not ready yet; background resolver started");
        } else if (state == BatteryFlyoutResolveState::Resolving) {
            ModLog(L"Classic Battery flyout target is still resolving in background; click ignored without blocking LogonUI");
        } else if (state == BatteryFlyoutResolveState::Failed) {
            ModLog(L"Classic Battery flyout unavailable: background resolver failed");
        } else {
            ModLog(L"Classic Battery flyout target state is Ready but target pointer is null; refusing call");
        }
        return false;
    }

    // Refresh the stobject-owned BATTERY_STATE from BatMeter's cache before
    // constructing/updating the native flyout. The expensive first acquisition
    // already happened on the background prefetch worker.
    if (!RefreshClassicBatteryStateFromCache()) {
        ModLog(L"Classic Battery flyout state refresh failed; refusing to show stale/zeroed UI");
        return false;
    }

    if (g_batteryFlyoutRefresh.load(std::memory_order_relaxed) ==
        BatteryFlyoutRefreshMode::EventDriven) {
        EnsureBatteryUiBridgeWindow();
        EnsureBatteryNotificationWatcherStarted();
    }

    EnsureLaunchPowerCplHookInstalled();

    using ShowBatteryUI_t = HRESULT(WINAPI*)();
    auto showBatteryUI =
        reinterpret_cast<ShowBatteryUI_t>(target);

    // The target is only used after an exact-build identity match or a
    // verified structural resolution or unique symbol resolution. Avoid MSVC-only SEH syntax so this compiles
    // under Windhawk's Clang toolchain.
    HRESULT hr = showBatteryUI();

    ModLog(L"stobject!ShowBatteryUI returned 0x%08X",
           static_cast<unsigned>(hr));

    if (SUCCEEDED(hr) && shouldAnchor) {
        if (haveAnchorRect) {
            RepositionClassicBatteryFlyout(anchorRect);
        } else {
            ModLog(L"Battery flyout anchor requested but button screen geometry wasn't available; keeping Windows placement");
        }
    }

    return SUCCEEDED(hr);
}

bool IsSndVolProcessId(DWORD processId) {
    if (!processId) {
        return false;
    }

    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        processId);
    if (!process) {
        return false;
    }

    wchar_t imagePath[1024]{};
    DWORD imagePathLength = ARRAYSIZE(imagePath);
    const BOOL ok = QueryFullProcessImageNameW(
        process,
        0,
        imagePath,
        &imagePathLength);
    CloseHandle(process);

    if (!ok || !imagePathLength) {
        return false;
    }

    const wchar_t* fileName = wcsrchr(imagePath, L'\\');
    fileName = fileName ? fileName + 1 : imagePath;
    return _wcsicmp(fileName, L"SndVol.exe") == 0;
}

bool IsUsableSndVolTopLevelWindow(HWND hwnd, DWORD* processIdOut = nullptr) {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (!IsSndVolProcessId(processId)) {
        return false;
    }

    wchar_t className[64]{};
    if (!GetClassNameW(hwnd, className, ARRAYSIZE(className)) ||
        _wcsicmp(className, L"#32770") != 0 ||
        (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)) {
        return false;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect) ||
        rect.right <= rect.left || rect.bottom <= rect.top) {
        return false;
    }

    if (processIdOut) {
        *processIdOut = processId;
    }
    return true;
}

bool GetCurrentSndVolAnchorRect(RECT* rect) {
    if (!rect ||
        !g_sndVolAnchorValid.load(std::memory_order_acquire)) {
        return false;
    }

    AcquireSRWLockShared(&g_sndVolAnchorLock);
    *rect = g_sndVolAnchorRect;
    ReleaseSRWLockShared(&g_sndVolAnchorLock);
    return true;
}


bool RepositionSndVolWindow(HWND hwnd, const wchar_t* reason) {
    DWORD processId = 0;
    if (!IsUsableSndVolTopLevelWindow(hwnd, &processId)) {
        return false;
    }

    wchar_t className[128]{};
    wchar_t title[256]{};
    GetClassNameW(hwnd, className, ARRAYSIZE(className));
    GetWindowTextW(hwnd, title, ARRAYSIZE(title));

    const bool isMixer = title[0] != L'\0';

    ModLog(L"SndVol %s window %s: pid=%lu hwnd=%p class=%s title=%s",
           isMixer ? L"mixer" : L"compact",
           reason ? reason : L"discovered",
           processId,
           hwnd,
           className[0] ? className : L"<none>",
           title[0] ? title : L"<none>");

    const auto placement =
        isMixer
            ? g_sndVolMixerPlacement.load(std::memory_order_relaxed)
            : g_sndVolPlacement.load(std::memory_order_relaxed);
    if (placement != SndVolPlacementMode::AnchorToButton) {
        return true;
    }

    RECT anchorRect{};
    if (!GetCurrentSndVolAnchorRect(&anchorRect)) {
        // Tracking succeeded. Placement can happen later when an anchor exists.
        return true;
    }

    return RepositionWindowToAnchor(
        hwnd,
        anchorRect,
        isMixer
            ? g_sndVolMixerAnchorSide.load(std::memory_order_relaxed)
            : g_sndVolAnchorSide.load(std::memory_order_relaxed),
        isMixer
            ? g_sndVolMixerOffsetX.load(std::memory_order_relaxed)
            : g_sndVolOffsetX.load(std::memory_order_relaxed),
        isMixer
            ? g_sndVolMixerOffsetY.load(std::memory_order_relaxed)
            : g_sndVolOffsetY.load(std::memory_order_relaxed),
        isMixer ? L"SndVol mixer" : L"SndVol compact");
}

BOOL CALLBACK EnumAndRepositionSndVolWindow(HWND hwnd, LPARAM) {
    RepositionSndVolWindow(hwnd, L"enumerated");
    return TRUE;
}

void RepositionExistingSndVolWindows() {
    if (!g_sndVolAnchorValid.load(std::memory_order_acquire)) {
        return;
    }
    EnumWindows(EnumAndRepositionSndVolWindow, 0);
}

void CALLBACK SndVolWinEventProc(
        HWINEVENTHOOK,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD,
        DWORD) {
    if (g_unloading.load(std::memory_order_acquire) ||
        event != EVENT_OBJECT_SHOW ||
        idObject != OBJID_WINDOW ||
        idChild != CHILDID_SELF ||
        !hwnd) {
        return;
    }

    RepositionSndVolWindow(hwnd, L"shown");
}

DWORD WINAPI SndVolWindowMonitorThreadProc(void*) {
    HWINEVENTHOOK hook = SetWinEventHook(
        EVENT_OBJECT_SHOW,
        EVENT_OBJECT_SHOW,
        nullptr,
        SndVolWinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    if (!hook) {
        ModLog(L"SndVol monitor: SetWinEventHook failed error=%lu",
               GetLastError());
        return 0;
    }

    ModLog(L"SndVol window monitor started (event-driven SHOW only)");

    RepositionExistingSndVolWindows();

    while (true) {
        DWORD wait = MsgWaitForMultipleObjects(
            1,
            &g_stopEvent,
            FALSE,
            INFINITE,
            QS_ALLINPUT);

        if (wait == WAIT_OBJECT_0) {
            break;
        }

        if (wait == WAIT_OBJECT_0 + 1) {
            MSG msg{};
            while (PeekMessageW(
                    &msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            ModLog(L"SndVol monitor: MsgWaitForMultipleObjects failed error=%lu",
                   GetLastError());
            break;
        }
    }

    UnhookWinEvent(hook);
    ModLog(L"SndVol window monitor stopped");
    return 0;
}

bool EnsureSndVolWindowMonitorStarted() {
    if (g_sndVolWindowMonitorThread) {
        return true;
    }

    HANDLE thread = CreateThread(
        nullptr,
        0,
        SndVolWindowMonitorThreadProc,
        nullptr,
        0,
        nullptr);
    if (!thread) {
        ModLog(L"SndVol monitor: CreateThread failed error=%lu",
               GetLastError());
        return false;
    }

    g_sndVolWindowMonitorThread = thread;
    return true;
}

void UpdateSndVolAnchorRect(const RECT& anchorRect) {
    AcquireSRWLockExclusive(&g_sndVolAnchorLock);
    g_sndVolAnchorRect = anchorRect;
    ReleaseSRWLockExclusive(&g_sndVolAnchorLock);

    g_sndVolAnchorValid.store(true, std::memory_order_release);

    if (EnsureSndVolWindowMonitorStarted()) {
        // Handles relay/single-instance behavior: if this click activates an
        // already-existing SndVol process/window, move that existing window
        // immediately. Future compact/mixer windows are caught by WinEvent.
        RepositionExistingSndVolWindows();
    }
}

bool HandleVolumeClickBeforeLaunch() {
    if (g_volumeClickAction.load(std::memory_order_relaxed) ==
        VolumeClickAction::None) {
        ModLog(L"Volume click ignored by setting");
        return true;
    }

    return false;
}

bool LaunchNativeSndVol(FrameworkElement const& anchorElement) {
    if (HandleVolumeClickBeforeLaunch()) {
        return true;
    }
    wchar_t systemDir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir));
    if (!length || length >= ARRAYSIZE(systemDir)) {
        ModLog(L"SndVol launch failed: GetSystemDirectoryW error=%lu",
               GetLastError());
        return false;
    }

    std::wstring applicationPath = systemDir;
    if (!applicationPath.empty() && applicationPath.back() != L'\\') {
        applicationPath.push_back(L'\\');
    }
    applicationPath += L"SndVol.exe";

    const std::wstring parameters = GetSndVolLaunchParameters();
    std::wstring commandLine = L"\"" + applicationPath + L"\"";
    if (!parameters.empty()) {
        commandLine += L" ";
        commandLine += parameters;
    }

    RECT anchorRect{};
    const bool shouldAnchor =
        g_sndVolPlacement.load(std::memory_order_relaxed) ==
            SndVolPlacementMode::AnchorToButton ||
        g_sndVolMixerPlacement.load(std::memory_order_relaxed) ==
            SndVolPlacementMode::AnchorToButton;
    const bool haveAnchorRect =
        shouldAnchor &&
        TryGetXamlElementScreenRect(anchorElement, &anchorRect, L"SndVol");

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(),
                                             commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(applicationPath.c_str(),
                        mutableCommandLine.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_UNICODE_ENVIRONMENT,
                        nullptr,
                        nullptr,
                        &si,
                        &pi)) {
        ModLog(L"SndVol launch failed: error=%lu command=%s",
               GetLastError(), commandLine.c_str());
        return false;
    }

    ModLog(L"SndVol launched: pid=%lu parameters=%s",
           pi.dwProcessId,
           parameters.empty() ? L"<empty>" : parameters.c_str());
    CloseHandle(pi.hThread);

    if (shouldAnchor && haveAnchorRect) {
        UpdateSndVolAnchorRect(anchorRect);
    } else if (shouldAnchor && !haveAnchorRect) {
        ModLog(L"SndVol anchor requested but Volume button screen geometry wasn't available; keeping Windows placement");
    }

    CloseHandle(pi.hProcess);
    return true;
}

std::wstring NormalizeButtonOrderToken(std::wstring token) {
    auto isSpace = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
    };

    while (!token.empty() && isSpace(token.front())) token.erase(token.begin());
    while (!token.empty() && isSpace(token.back())) token.pop_back();

    for (auto& ch : token) {
        if (ch >= L'A' && ch <= L'Z') ch = ch - L'A' + L'a';
    }

    std::wstring compact;
    compact.reserve(token.size());
    for (wchar_t ch : token) {
        if (ch != L' ' && ch != L'-' && ch != L'_') compact.push_back(ch);
    }

    if (compact == L"ime" || compact == L"imemode" ||
        compact == L"imeindicator") return L"ime";
    if (compact == L"input" || compact == L"language" ||
        compact == L"languages" || compact == L"keyboard" ||
        compact == L"locale") return L"input";
    if (compact == L"networksignin" || compact == L"netsignin")
        return L"network-signin";
    if (compact == L"disconnect" || compact == L"networkdisconnect")
        return L"disconnect";
    if (compact == L"network" || compact == L"net") return L"network";
    if (compact == L"accessibility" || compact == L"easeofaccess" ||
        compact == L"ease" || compact == L"a11y") return L"accessibility";
    if (compact == L"battery") return L"battery";
    if (compact == L"volume" || compact == L"audio" || compact == L"speaker") return L"volume";
    if (compact == L"power" || compact == L"poweroptions") return L"power";

    if (compact.rfind(L"native", 0) == 0 && compact.size() > 6) {
        bool digits = true;
        for (size_t i = 6; i < compact.size(); ++i) {
            if (compact[i] < L'0' || compact[i] > L'9') {
                digits = false;
                break;
            }
        }
        if (digits) return compact;
    }

    return {};
}

std::vector<std::wstring> ParseButtonOrderSetting(const std::wstring& text) {
    std::vector<std::wstring> result;
    std::wstring token;

    auto trim = [](std::wstring value) {
        auto isSpace = [](wchar_t ch) {
            return ch == L' ' || ch == L'\t' ||
                   ch == L'\r' || ch == L'\n';
        };
        while (!value.empty() && isSpace(value.front())) {
            value.erase(value.begin());
        }
        while (!value.empty() && isSpace(value.back())) {
            value.pop_back();
        }
        return value;
    };

    auto compactLower = [](std::wstring value) {
        std::wstring compact;
        compact.reserve(value.size());
        for (wchar_t ch : value) {
            if (ch >= L'A' && ch <= L'Z') {
                ch = ch - L'A' + L'a';
            }
            if (ch != L' ' && ch != L'\t' &&
                ch != L'-' && ch != L'_') {
                compact.push_back(ch);
            }
        }
        return compact;
    };

    auto flush = [&]() {
        std::wstring raw = trim(token);
        token.clear();
        if (raw.empty()) {
            return;
        }

        // Be forgiving of users pasting a YAML-style key into Windhawk's
        // string value, e.g. "buttonOrder: battery,volume,...".
        const size_t colon = raw.find(L':');
        if (colon != std::wstring::npos) {
            const std::wstring prefix =
                compactLower(trim(raw.substr(0, colon)));
            if (prefix == L"buttonorder") {
                ModLog(L"Button-order parser: ignored accidental 'buttonOrder:' prefix");
                raw = trim(raw.substr(colon + 1));
                if (raw.empty()) {
                    return;
                }
            }
        }

        auto normalized = NormalizeButtonOrderToken(raw);
        if (normalized.empty()) {
            ModLog(L"Button-order parser: ignoring unknown token: %s",
                   raw.c_str());
            return;
        }

        if (std::find(result.begin(), result.end(), normalized) !=
            result.end()) {
            ModLog(L"Button-order parser: ignoring duplicate token: %s",
                   normalized.c_str());
            return;
        }

        result.push_back(std::move(normalized));
    };

    for (wchar_t ch : text) {
        if (ch == L',' || ch == L';' || ch == L'>' || ch == L'|') {
            flush();
        } else {
            token.push_back(ch);
        }
    }
    flush();
    return result;
}

using OnLoaded_t = void(*)(void* pThis, void* sender, void* args);
OnLoaded_t g_OnLoaded_Original = nullptr;


constexpr wchar_t kVolumeGlyphMute = L'\uE74F';
constexpr wchar_t kVolumeGlyphDisabledFluent = L'\uEA85'; // VolumeDisabled (Segoe Fluent Icons)
constexpr wchar_t kVolumeGlyph0 = L'\uE992';
constexpr wchar_t kVolumeGlyph1 = L'\uE993';
constexpr wchar_t kVolumeGlyph2 = L'\uE994';
constexpr wchar_t kVolumeGlyph3 = L'\uE995';

constexpr UINT kSndVolGroupMute = 120;
constexpr UINT kSndVolGroup0 = 121;
constexpr UINT kSndVolGroup1 = 122;
constexpr UINT kSndVolGroup2 = 123;
constexpr UINT kSndVolGroup3 = 124;
constexpr UINT kSndVolGroupNoEndpoint = 125;

namespace WRL = Microsoft::WRL;
namespace Core = winrt::Windows::UI::Core;
namespace System = winrt::Windows::System;
namespace Streams = winrt::Windows::Storage::Streams;


#pragma pack(push, 2)
struct NativeGrpIconDir {
    WORD reserved;
    WORD type;
    WORD count;
};

struct NativeGrpIconDirEntry {
    BYTE width;
    BYTE height;
    BYTE colorCount;
    BYTE reserved;
    WORD planes;
    WORD bitCount;
    DWORD bytesInRes;
    WORD iconId;
};
#pragma pack(pop)

void AppendWord(std::vector<uint8_t>& out, WORD value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void AppendDword(std::vector<uint8_t>& out, DWORD value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

bool BuildSndVolGroupIco(UINT groupId,
                        SndVolResourceSource source,
                        std::vector<uint8_t>& icoBytes,
                        std::wstring* resourcePathOut = nullptr) {
    wchar_t windowsDir[MAX_PATH]{};
    const UINT windowsDirLen =
        GetWindowsDirectoryW(windowsDir, ARRAYSIZE(windowsDir));
    if (!windowsDirLen || windowsDirLen >= ARRAYSIZE(windowsDir)) {
        return false;
    }

    std::wstring munPath = windowsDir;
    munPath += L"\\SystemResources\\SndVolSSO.dll.mun";

    std::wstring dllPath = windowsDir;
    dllPath += L"\\System32\\SndVolSSO.dll";

    // Three deliberately different resource-source modes:
    //
    // WindowsAutomatic:
    //   Open the canonical SndVolSSO.dll resource container and let the
    //   Windows resource loader resolve an associated resource file. This is
    //   the closest match to normal Win32 resource loading and avoids this mod
    //   making a DLL-vs-MUN choice itself.
    // DirectDll / DirectMun:
    //   Open exactly the requested container. The caller generation-gates
    //   these diagnostic modes so DirectDll is only used where icons are
    //   embedded in the legacy DLL and DirectMun only where the MUN exists.
    std::wstring resourcePath;
    switch (source) {
        case SndVolResourceSource::WindowsAutomatic:
        case SndVolResourceSource::DirectDll:
            resourcePath = dllPath;
            break;
        case SndVolResourceSource::DirectMun:
            resourcePath = munPath;
            break;
    }

    HMODULE resourceModule = LoadLibraryExW(
        resourcePath.c_str(),
        nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!resourceModule) {
        return false;
    }

    HRSRC probe = FindResourceW(
        resourceModule, MAKEINTRESOURCEW(groupId), RT_GROUP_ICON);
    if (!probe) {
        FreeLibrary(resourceModule);
        return false;
    }

    struct ModuleGuard {
        HMODULE module;
        ~ModuleGuard() {
            if (module) {
                FreeLibrary(module);
            }
        }
    } moduleGuard{resourceModule};

    HRSRC groupResource =
        FindResourceW(resourceModule, MAKEINTRESOURCEW(groupId), RT_GROUP_ICON);
    if (!groupResource) {
        return false;
    }

    HGLOBAL groupHandle = LoadResource(resourceModule, groupResource);
    const DWORD groupSize = SizeofResource(resourceModule, groupResource);
    const auto* groupData =
        static_cast<const uint8_t*>(LockResource(groupHandle));
    if (!groupHandle || !groupData || groupSize < sizeof(NativeGrpIconDir)) {
        return false;
    }

    const auto* groupDir =
        reinterpret_cast<const NativeGrpIconDir*>(groupData);
    if (groupDir->reserved != 0 || groupDir->type != 1 ||
        groupDir->count == 0) {
        return false;
    }

    const size_t entriesBytes =
        static_cast<size_t>(groupDir->count) *
        sizeof(NativeGrpIconDirEntry);
    if (sizeof(NativeGrpIconDir) + entriesBytes > groupSize) {
        return false;
    }

    const auto* groupEntries =
        reinterpret_cast<const NativeGrpIconDirEntry*>(
            groupData + sizeof(NativeGrpIconDir));

    struct IconPayload {
        NativeGrpIconDirEntry entry;
        const uint8_t* data;
        DWORD size;
    };

    std::vector<IconPayload> payloads;
    payloads.reserve(groupDir->count);

    for (WORD i = 0; i < groupDir->count; ++i) {
        HRSRC iconResource = FindResourceW(
            resourceModule,
            MAKEINTRESOURCEW(groupEntries[i].iconId),
            RT_ICON);
        if (!iconResource) {
            return false;
        }

        HGLOBAL iconHandle = LoadResource(resourceModule, iconResource);
        const DWORD iconSize = SizeofResource(resourceModule, iconResource);
        const auto* iconData =
            static_cast<const uint8_t*>(LockResource(iconHandle));
        if (!iconHandle || !iconData || iconSize == 0) {
            return false;
        }

        payloads.push_back(
            IconPayload{groupEntries[i], iconData, iconSize});
    }

    // RT_GROUP_ICON stores resource IDs instead of ICO file offsets.
    // Rebuild only the standard ICO container around the original RT_ICON
    // payloads. The native Windows artwork remains byte-for-byte unchanged.
    const DWORD directorySize =
        6u + static_cast<DWORD>(payloads.size()) * 16u;
    DWORD imageOffset = directorySize;

    size_t totalSize = directorySize;
    for (const auto& payload : payloads) {
        totalSize += payload.size;
    }

    icoBytes.clear();
    icoBytes.reserve(totalSize);

    AppendWord(icoBytes, 0);
    AppendWord(icoBytes, 1);
    AppendWord(icoBytes, static_cast<WORD>(payloads.size()));

    for (const auto& payload : payloads) {
        icoBytes.push_back(payload.entry.width);
        icoBytes.push_back(payload.entry.height);
        icoBytes.push_back(payload.entry.colorCount);
        icoBytes.push_back(payload.entry.reserved);
        AppendWord(icoBytes, payload.entry.planes);
        AppendWord(icoBytes, payload.entry.bitCount);
        AppendDword(icoBytes, payload.size);
        AppendDword(icoBytes, imageOffset);
        imageOffset += payload.size;
    }

    for (const auto& payload : payloads) {
        icoBytes.insert(
            icoBytes.end(), payload.data, payload.data + payload.size);
    }

    if (resourcePathOut) {
        *resourcePathOut = std::move(resourcePath);
    }
    return true;
}

Media::Imaging::BitmapImage LoadSndVolGroupBitmap(
    UINT groupId,
    SndVolResourceSource source) {
    std::vector<uint8_t> icoBytes;
    std::wstring resourcePath;
    if (!BuildSndVolGroupIco(groupId, source, icoBytes, &resourcePath)) {
        ModLog(L"Couldn't load SndVolSSO icon group %u using source mode %d",
               groupId,
               static_cast<int>(source));
        return nullptr;
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, icoBytes.size());
    if (!memory) {
        return nullptr;
    }

    void* bytes = GlobalLock(memory);
    if (!bytes) {
        GlobalFree(memory);
        return nullptr;
    }
    CopyMemory(bytes, icoBytes.data(), icoBytes.size());
    GlobalUnlock(memory);

    WRL::ComPtr<IStream> stream;
    HRESULT hr =
        CreateStreamOnHGlobal(memory, TRUE, stream.GetAddressOf());
    if (FAILED(hr)) {
        GlobalFree(memory);
        return nullptr;
    }

    HMODULE shcore = GetModuleHandleW(L"shcore.dll");
    if (!shcore) {
        shcore = LoadLibraryW(L"shcore.dll");
    }
    if (!shcore) {
        return nullptr;
    }

    using CreateRandomAccessStreamOverStream_t =
        HRESULT(WINAPI*)(IStream*, DWORD, REFIID, void**);
    auto createRandomAccessStream =
        reinterpret_cast<CreateRandomAccessStreamOverStream_t>(
            GetProcAddress(shcore, "CreateRandomAccessStreamOverStream"));
    if (!createRandomAccessStream) {
        return nullptr;
    }

    const auto winrtIid =
        winrt::guid_of<Streams::IRandomAccessStream>();
    const GUID& iid =
        reinterpret_cast<const GUID&>(winrtIid);

    void* randomAccessRaw = nullptr;
    hr = createRandomAccessStream(
        stream.Get(), 0, iid, &randomAccessRaw);
    if (FAILED(hr) || !randomAccessRaw) {
        ModLog(
            L"CreateRandomAccessStreamOverStream failed for "
            L"SndVolSSO group %u: 0x%08X",
            groupId,
            static_cast<unsigned>(hr));
        return nullptr;
    }

    Streams::IRandomAccessStream randomAccessStream{
        randomAccessRaw, winrt::take_ownership_from_abi};

    try {
        Media::Imaging::BitmapImage bitmap;
        bitmap.SetSource(randomAccessStream);
        ModLog(L"Loaded native SndVolSSO icon group %u from %s",
               groupId,
               resourcePath.c_str());
        return bitmap;
    } catch (const winrt::hresult_error& e) {
        ModLog(
            L"BitmapImage::SetSource failed for SndVolSSO group %u: "
            L"0x%08X %s",
            groupId,
            static_cast<unsigned>(e.code().value),
            e.message().c_str());
    } catch (...) {
        ModLog(
            L"BitmapImage::SetSource failed for SndVolSSO group %u",
            groupId);
    }

    return nullptr;
}

struct NativeSndVolVisuals {
    Controls::Image image{nullptr};
    Media::Imaging::BitmapImage muted{nullptr};
    Media::Imaging::BitmapImage zero{nullptr};
    Media::Imaging::BitmapImage low{nullptr};
    Media::Imaging::BitmapImage medium{nullptr};
    Media::Imaging::BitmapImage high{nullptr};
    Media::Imaging::BitmapImage noEndpoint{nullptr};

    bool Complete() const {
        return image && muted && zero && low && medium && high && noEndpoint;
    }
};

struct VolumeMonitorState;
void QueueEndpointRebind(std::shared_ptr<VolumeMonitorState> const& state);

struct AudioCallbackInFlightGuard {
    AudioCallbackInFlightGuard() {
        g_audioCallbacksInFlight.fetch_add(1, std::memory_order_acq_rel);
    }
    ~AudioCallbackInFlightGuard() {
        g_audioCallbacksInFlight.fetch_sub(1, std::memory_order_acq_rel);
    }
};

class EndpointVolumeCallback final
    : public IAudioEndpointVolumeCallback,
      public IAgileObject {
public:
    explicit EndpointVolumeCallback(std::weak_ptr<VolumeMonitorState> state)
        : state_(std::move(state)) {
        g_audioCallbackObjectsAlive.fetch_add(1, std::memory_order_acq_rel);
    }

    ~EndpointVolumeCallback() {
        g_audioCallbackObjectsAlive.fetch_sub(1, std::memory_order_acq_rel);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        AudioCallbackInFlightGuard guard;
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(IAudioEndpointVolumeCallback)) {
            *object = static_cast<IAudioEndpointVolumeCallback*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == __uuidof(IAgileObject)) {
            *object = static_cast<IAgileObject*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        AudioCallbackInFlightGuard guard;
        return ++refCount_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        AudioCallbackInFlightGuard guard;
        const ULONG remaining = --refCount_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE OnNotify(
        PAUDIO_VOLUME_NOTIFICATION_DATA data) override;

private:
    std::atomic<ULONG> refCount_{1};
    std::weak_ptr<VolumeMonitorState> state_;
};

class DefaultDeviceNotificationCallback final
    : public IMMNotificationClient,
      public IAgileObject {
public:
    explicit DefaultDeviceNotificationCallback(
        std::weak_ptr<VolumeMonitorState> state)
        : state_(std::move(state)) {
        g_audioCallbackObjectsAlive.fetch_add(1, std::memory_order_acq_rel);
    }

    ~DefaultDeviceNotificationCallback() {
        g_audioCallbackObjectsAlive.fetch_sub(1, std::memory_order_acq_rel);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        AudioCallbackInFlightGuard guard;
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == __uuidof(IAgileObject)) {
            *object = static_cast<IAgileObject*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        AudioCallbackInFlightGuard guard;
        return ++refCount_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        AudioCallbackInFlightGuard guard;
        const ULONG remaining = --refCount_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
        EDataFlow flow,
        ERole role,
        LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
        LPCWSTR,
        const PROPERTYKEY) override {
        AudioCallbackInFlightGuard guard;
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_{1};
    std::weak_ptr<VolumeMonitorState> state_;
};

constexpr PROPERTYKEY kPkeyDeviceFriendlyName = {
    {0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}},
    14
};

std::wstring ReadEndpointFriendlyName(IMMDevice* device) {
    if (!device) {
        return {};
    }

    WRL::ComPtr<IPropertyStore> store;
    HRESULT hr = device->OpenPropertyStore(STGM_READ, &store);
    if (FAILED(hr) || !store) {
        return {};
    }

    PROPVARIANT value{};
    hr = store->GetValue(kPkeyDeviceFriendlyName, &value);
    std::wstring result;
    if (SUCCEEDED(hr) && value.vt == VT_LPWSTR && value.pwszVal) {
        result.assign(value.pwszVal);
    }
    PropVariantClear(&value);
    return result;
}

std::wstring ReadEndpointId(IMMDevice* device) {
    if (!device) {
        return {};
    }

    LPWSTR rawId = nullptr;
    const HRESULT hr = device->GetId(&rawId);
    if (FAILED(hr) || !rawId) {
        return {};
    }

    std::wstring result(rawId);
    CoTaskMemFree(rawId);
    return result;
}

struct WheelDeltaAccumulator {
    int remainder = 0;
    ULONGLONG lastEventTick = 0;
    bool highResolutionStream = false;
};

struct WheelInputResult {
    int rawDelta = 0;
    int effectiveDelta = 0;
    int steps = 0;
    int remainder = 0;
    int deltaPerStep = WHEEL_DELTA;
    bool highResolution = false;
};

bool TryConsumeVerticalWheelInput(
        UIElement const& relativeTo,
        Input::PointerRoutedEventArgs const& args,
        bool reverse,
        WheelDeltaAccumulator& accumulator,
        WheelInputResult* result) {
    if (!relativeTo || !result) {
        return false;
    }

    int rawDelta = 0;
    bool horizontal = false;
    try {
        auto point = args.GetCurrentPoint(relativeTo);
        auto properties = point.Properties();
        rawDelta = properties.MouseWheelDelta();
        horizontal = properties.IsHorizontalMouseWheel();
    } catch (...) {
        return false;
    }

    if (horizontal || rawDelta == 0) {
        return false;
    }

    const int effectiveDelta = reverse ? -rawDelta : rawDelta;
    const ULONGLONG now = GetTickCount64();

    // XAML doesn't expose a reliable "precision touchpad" source flag for
    // these routed wheel events. Both a touchpad and a mouse are represented
    // through MouseWheelDelta. Treat a burst as high-resolution when any
    // packet isn't an exact WHEEL_DELTA (120) multiple. Keep that
    // classification sticky for the short gesture, then reset after idle so a
    // later physical mouse-wheel detent uses the normal per-control step.
    constexpr ULONGLONG kWheelGestureResetMs = 400;
    const bool newGesture =
        accumulator.lastEventTick == 0 ||
        now - accumulator.lastEventTick > kWheelGestureResetMs;
    if (newGesture) {
        accumulator.remainder = 0;
        accumulator.highResolutionStream = false;
    }

    const int absDelta =
        effectiveDelta == INT_MIN ? INT_MAX : std::abs(effectiveDelta);
    if (absDelta % WHEEL_DELTA != 0) {
        accumulator.highResolutionStream = true;
    }
    accumulator.lastEventTick = now;

    // A traditional mouse wheel keeps the canonical 120-unit detent. For a
    // high-resolution stream, decouple output granularity from sensitivity:
    // e.g. 1% every 30 delta units gives smooth 1% changes without requiring
    // four times as much finger travel as a normal wheel detent.
    const int deltaPerStep =
        accumulator.highResolutionStream
            ? g_precisionScrollDeltaPerStep.load(
                  std::memory_order_relaxed)
            : WHEEL_DELTA;

    const long long total =
        static_cast<long long>(accumulator.remainder) +
        static_cast<long long>(effectiveDelta);
    const int steps =
        static_cast<int>(total / static_cast<long long>(deltaPerStep));
    accumulator.remainder =
        static_cast<int>(total % static_cast<long long>(deltaPerStep));

    result->rawDelta = rawDelta;
    result->effectiveDelta = effectiveDelta;
    result->steps = steps;
    result->remainder = accumulator.remainder;
    result->deltaPerStep = deltaPerStep;
    result->highResolution = accumulator.highResolutionStream;
    return true;
}

struct VolumeMonitorState {
    Controls::TextBlock icon{nullptr};
    Controls::TextBlock percentText{nullptr};
    Controls::Button button{nullptr};
    Controls::ToolTip toolTip{nullptr};
    hstring endpointName;
    std::wstring endpointId;
    NativeSndVolVisuals nativeSndVol{};
    System::DispatcherQueue dispatcherQueue{nullptr};
    Core::CoreDispatcher coreDispatcher{nullptr};
    VolumeUiHandoffMode requestedHandoff = VolumeUiHandoffMode::Automatic;
    VolumeUiHandoffMode activeHandoff = VolumeUiHandoffMode::Automatic;
    DWORD uiThreadId = 0;
    HWND legacyUiWindow = nullptr;
    std::atomic<unsigned> legacyPendingVolume{0};
    std::atomic<bool> legacyVolumePostPending{false};
    std::atomic<unsigned> lastQueuedSignature{0xFFFFFFFFu};
    WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    WRL::ComPtr<IAudioEndpointVolume> endpointVolume;
    WRL::ComPtr<IAudioEndpointVolumeCallback> endpointCallback;
    WRL::ComPtr<IMMNotificationClient> deviceCallback;
    wchar_t lastGlyph = 0;
    int lastPercent = -1;
    bool lastMuted = false;
    HRESULT lastError = S_OK;
    bool hasReading = false;
    UINT lastNativeGroup = 0;
    std::atomic<bool> stopping{false};
    std::atomic<bool> cleanupDone{false};
    std::atomic<bool> rebindPending{false};
    winrt::Windows::Foundation::IInspectable wheelHandler{nullptr};
    bool wheelHandlerAttached = false;
    WheelDeltaAccumulator wheelAccumulator{};
    wchar_t noEndpointGlyph = kVolumeGlyphDisabledFluent;
};

HRESULT RebindDefaultPlaybackEndpoint(
    std::shared_ptr<VolumeMonitorState> const& state);

constexpr UINT kLegacyVolumeUiReadingMessage = WM_APP + 0x421;
constexpr UINT kLegacyVolumeUiRebindMessage = WM_APP + 0x422;
constexpr wchar_t kLegacyVolumeUiWindowClass[] =
    L"Windhawk.LogonUI.BatteryVolumeControls.VolumeUiBridge.v2";

LRESULT CALLBACK LegacyVolumeUiWndProc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam);
HWND CreateLegacyVolumeUiWindow(
    std::shared_ptr<VolumeMonitorState> const& state);

Media::Imaging::BitmapImage NativeSndVolBitmapForGroup(
    VolumeMonitorState const& state,
    UINT groupId) {
    switch (groupId) {
        case kSndVolGroupMute:
            return state.nativeSndVol.muted;
        case kSndVolGroup0:
            return state.nativeSndVol.zero;
        case kSndVolGroup1:
            return state.nativeSndVol.low;
        case kSndVolGroup2:
            return state.nativeSndVol.medium;
        case kSndVolGroup3:
            return state.nativeSndVol.high;
        case kSndVolGroupNoEndpoint:
            return state.nativeSndVol.noEndpoint;
        default:
            return nullptr;
    }
}

UINT ChooseNativeSndVolGroup(float scalar, bool muted) {
    if (muted) {
        return kSndVolGroupMute;
    }

    // Match the native SndVolSSO.dll tray-icon logic exactly:
    // percentage = trunc((scalar * 100.0f) + 0.5f)
    //   0      -> group 121
    //   1..32  -> group 122
    //   33..65 -> group 123
    //   66..100-> group 124
    int percent = static_cast<int>((scalar * 100.0f) + 0.5f);
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    if (percent < 1) {
        return kSndVolGroup0;
    }
    if (percent < 33) {
        return kSndVolGroup1;
    }
    if (percent < 66) {
        return kSndVolGroup2;
    }
    return kSndVolGroup3;
}

void UpdateVolumeToolTip(std::shared_ptr<VolumeMonitorState> const& state,
                         int percent,
                         bool noEndpoint) {
    if (!state || !state->toolTip) {
        return;
    }

    try {
        if (noEndpoint) {
            state->toolTip.Content(box_value(hstring(L"No audio output device is installed")));
            return;
        }

        std::wstring text;
        if (!state->endpointName.empty()) {
            text.assign(state->endpointName.c_str(), state->endpointName.size());
        } else {
            text = L"Volume";
        }
        text += L": ";
        text += std::to_wstring(percent);
        text += L"%";
        state->toolTip.Content(box_value(hstring(text)));
    } catch (...) {
    }
}

void UpdateVolumeAutomationName(
    std::shared_ptr<VolumeMonitorState> const& state,
    int percent,
    bool noEndpoint,
    bool muted) {
    if (!state || !state->button) {
        return;
    }

    try {
        std::wstring text;
        if (noEndpoint) {
            text = L"No audio output device is installed";
        } else {
            if (!state->endpointName.empty()) {
                text.assign(state->endpointName.c_str(), state->endpointName.size());
            } else {
                text = L"Volume";
            }
            text += L": ";
            text += std::to_wstring(percent);
            text += L"%";
            if (muted) {
                text += L" (muted)";
            }
        }

        Automation::AutomationProperties::SetName(
            state->button, hstring(text));
    } catch (...) {
    }
}

void ApplyNoPlaybackEndpoint(std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state || state->stopping.load() || !state->icon) {
        return;
    }

    UpdateVolumeToolTip(state, -1, true);
    UpdateVolumeAutomationName(state, -1, true, false);

    if (state->percentText) {
        SetPercentageLabelText(state->percentText, L"", false);
    }

    if (state->nativeSndVol.Complete()) {
        state->icon.Visibility(Visibility::Collapsed);
        state->nativeSndVol.image.Visibility(Visibility::Visible);
        if (state->lastNativeGroup != kSndVolGroupNoEndpoint) {
            state->nativeSndVol.image.Source(state->nativeSndVol.noEndpoint);
        }

        if (!state->hasReading ||
            state->lastNativeGroup != kSndVolGroupNoEndpoint ||
            state->lastPercent != -1) {
            ModLog(L"Volume state: no playback endpoint nativeGroup=%u",
                   kSndVolGroupNoEndpoint);
        }

        state->lastNativeGroup = kSndVolGroupNoEndpoint;
        state->lastGlyph = 0;
        state->lastPercent = -1;
        state->lastMuted = false;
        state->hasReading = true;
        return;
    }

    // Modern Win11 path: native Fluent disabled-volume glyph.
    state->icon.Visibility(Visibility::Visible);
    const wchar_t glyph = state->noEndpointGlyph;
    if (!state->hasReading ||
        state->lastGlyph != glyph ||
        state->lastPercent != -1) {
        wchar_t text[2]{glyph, L'\0'};
        state->icon.Text(text);
        ModLog(L"Volume state: no playback endpoint glyph=U+%04X",
               static_cast<unsigned>(glyph));
        state->lastGlyph = glyph;
        state->lastNativeGroup = 0;
        state->lastPercent = -1;
        state->lastMuted = false;
        state->hasReading = true;
    }
}

wchar_t ChooseVolumeGlyph(float scalar, bool muted) {
    if (muted) {
        return kVolumeGlyphMute;
    }

    // Keep the font renderer on the same state boundaries as native
    // SndVolSSO: round to an integer percentage, then 0 / 1..32 /
    // 33..65 / 66..100.
    int percent = static_cast<int>((scalar * 100.0f) + 0.5f);
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    if (percent < 1) {
        return kVolumeGlyph0;
    }
    if (percent < 33) {
        return kVolumeGlyph1;
    }
    if (percent < 66) {
        return kVolumeGlyph2;
    }
    return kVolumeGlyph3;
}

void ApplyVolumeReading(std::shared_ptr<VolumeMonitorState> const& state,
                        float scalar,
                        bool muted) {
    if (!state || state->stopping.load() || !state->icon) {
        return;
    }

    if (scalar < 0.0f) {
        scalar = 0.0f;
    } else if (scalar > 1.0f) {
        scalar = 1.0f;
    }

    const int percent =
        static_cast<int>(std::lround(static_cast<double>(scalar) * 100.0));

    UpdateVolumeToolTip(state, percent, false);
    UpdateVolumeAutomationName(state, percent, false, muted);

    if (state->percentText) {
        SetPercentageLabelText(
            state->percentText,
            FormatPercentageText(percent, true, muted),
            g_showVolumePercentage.load(std::memory_order_relaxed));
    }

    if (state->nativeSndVol.Complete()) {
        const UINT groupId = ChooseNativeSndVolGroup(scalar, muted);
        state->icon.Visibility(Visibility::Collapsed);
        state->nativeSndVol.image.Visibility(Visibility::Visible);
        if (state->lastNativeGroup != groupId) {
            auto bitmap = NativeSndVolBitmapForGroup(*state, groupId);
            if (bitmap) {
                state->nativeSndVol.image.Source(bitmap);
            }
        }

        if (!state->hasReading ||
            state->lastNativeGroup != groupId ||
            state->lastPercent != percent ||
            state->lastMuted != muted) {
            ModLog(L"Volume state: %d%% mute=%d nativeGroup=%u",
                   percent,
                   muted ? 1 : 0,
                   groupId);
        }

        state->lastNativeGroup = groupId;
        state->lastGlyph = 0;
        state->lastPercent = percent;
        state->lastMuted = muted;
        state->hasReading = true;
        return;
    }

    const wchar_t glyph = ChooseVolumeGlyph(scalar, muted);
    state->icon.Visibility(Visibility::Visible);

    if (!state->hasReading ||
        state->lastGlyph != glyph ||
        state->lastPercent != percent ||
        state->lastMuted != muted) {
        wchar_t text[2]{glyph, L'\0'};
        state->icon.Text(text);

        ModLog(L"Volume state: %d%% mute=%d glyph=U+%04X",
               percent,
               muted ? 1 : 0,
               static_cast<unsigned>(glyph));

        state->lastGlyph = glyph;
        state->lastNativeGroup = 0;
        state->lastPercent = percent;
        state->lastMuted = muted;
        state->hasReading = true;
    }
}

void QueueVolumeReading(std::shared_ptr<VolumeMonitorState> const& state,
                        float scalar,
                        bool muted) {
    if (!state || state->stopping.load() ||
        g_unloading.load(std::memory_order_acquire)) {
        return;
    }

    if (scalar < 0.0f) {
        scalar = 0.0f;
    } else if (scalar > 1.0f) {
        scalar = 1.0f;
    }

    const int percent = static_cast<int>(
        std::lround(static_cast<double>(scalar) * 100.0));
    const unsigned signature =
        static_cast<unsigned>(percent) | (muted ? 0x100u : 0u);
    if (state->lastQueuedSignature.exchange(signature) == signature) {
        return;
    }

    if (GetCurrentThreadId() == state->uiThreadId) {
        ApplyVolumeReading(state, scalar, muted);
        return;
    }

    std::weak_ptr<VolumeMonitorState> weak = state;

    switch (state->activeHandoff) {
        case VolumeUiHandoffMode::DispatcherQueue: {
            if (!state->dispatcherQueue) {
                ModLog(L"Volume UI update dropped: DispatcherQueue explicitly selected but unavailable");
                return;
            }
            try {
                if (state->dispatcherQueue.TryEnqueue(
                        [weak, scalar, muted]() {
                            if (auto locked = weak.lock()) {
                                ApplyVolumeReading(locked, scalar, muted);
                            }
                        })) {
                    return;
                }
                ModLog(L"Volume DispatcherQueue rejected UI update");
            } catch (const winrt::hresult_error& e) {
                ModLog(L"Volume DispatcherQueue failed: 0x%08X %s",
                       static_cast<unsigned>(e.code().value),
                       e.message().c_str());
            } catch (...) {
                ModLog(L"Volume DispatcherQueue failed: unknown exception");
            }
            return;
        }

        case VolumeUiHandoffMode::CoreDispatcher: {
            if (!state->coreDispatcher) {
                ModLog(L"Volume UI update dropped: CoreDispatcher explicitly selected but unavailable");
                return;
            }
            try {
                // Do not install a Completed delegate here. A completion
                // callback can outlive the XAML surface during hot-unload even
                // after the actual UI work has run. The queued work itself is
                // fenced during Wh_ModBeforeUninit.
                state->coreDispatcher.TryRunAsync(
                    Core::CoreDispatcherPriority::Normal,
                    [weak, scalar, muted]() {
                        if (auto locked = weak.lock()) {
                            ApplyVolumeReading(locked, scalar, muted);
                        }
                    });
            } catch (const winrt::hresult_error& e) {
                ModLog(L"Volume CoreDispatcher failed: 0x%08X %s",
                       static_cast<unsigned>(e.code().value),
                       e.message().c_str());
            } catch (...) {
                ModLog(L"Volume CoreDispatcher failed: unknown exception");
            }
            return;
        }

        case VolumeUiHandoffMode::LegacyMessageWindow: {
            state->legacyPendingVolume.store(signature);
            if (!state->legacyUiWindow) {
                ModLog(L"Volume UI update dropped: legacy message-window handoff selected but unavailable");
                return;
            }
            if (!state->legacyVolumePostPending.exchange(true)) {
                if (!PostMessageW(state->legacyUiWindow,
                                  kLegacyVolumeUiReadingMessage,
                                  0,
                                  0)) {
                    state->legacyVolumePostPending.store(false);
                    ModLog(L"Volume legacy UI bridge PostMessage failed: %lu",
                           GetLastError());
                }
            }
            return;
        }

        case VolumeUiHandoffMode::Automatic:
        default:
            // activeHandoff is resolved once on the XAML UI thread when the
            // monitor starts, so Automatic should never remain active here.
            ModLog(L"Volume UI update dropped: automatic handoff was not resolved");
            return;
    }
}

HRESULT QueryEndpointVolume(IAudioEndpointVolume* endpoint,
                            float* scalar,
                            bool* muted) {
    if (!endpoint || !scalar || !muted) {
        return E_POINTER;
    }

    BOOL isMuted = FALSE;
    HRESULT hr = endpoint->GetMute(&isMuted);
    if (FAILED(hr)) {
        return hr;
    }

    float level = 0.0f;
    hr = endpoint->GetMasterVolumeLevelScalar(&level);
    if (FAILED(hr)) {
        return hr;
    }

    *scalar = level;
    *muted = isMuted != FALSE;
    return S_OK;
}

HRESULT RebindDefaultPlaybackEndpoint(
    std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state || state->stopping.load() || !state->enumerator) {
        return E_ABORT;
    }

    WRL::ComPtr<IMMDevice> device;
    HRESULT hr = state->enumerator->GetDefaultAudioEndpoint(
        eRender, eMultimedia, &device);
    if (FAILED(hr)) {
        hr = state->enumerator->GetDefaultAudioEndpoint(
            eRender, eConsole, &device);
        if (FAILED(hr)) {
            if (state->endpointVolume && state->endpointCallback) {
                state->endpointVolume->UnregisterControlChangeNotify(
                    state->endpointCallback.Get());
            }
            state->endpointVolume.Reset();
            state->endpointName = {};
            state->endpointId.clear();

            if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
                ApplyNoPlaybackEndpoint(state);
            }
            return hr;
        }
    }

    const std::wstring newEndpointId = ReadEndpointId(device.Get());

    // Windows can issue several device/default-role notifications that all
    // still resolve to the exact same render endpoint. Avoid tearing down and
    // recreating the endpoint callback in that case. First verify that the
    // existing endpoint object is still healthy; if it isn't, fall through to
    // a real rebind.
    if (state->endpointVolume &&
        !state->endpointId.empty() &&
        !newEndpointId.empty() &&
        state->endpointId == newEndpointId) {
        float scalar = 0.0f;
        bool muted = false;
        const HRESULT queryHr =
            QueryEndpointVolume(state->endpointVolume.Get(),
                                &scalar,
                                &muted);
        if (SUCCEEDED(queryHr)) {
            ApplyVolumeReading(state, scalar, muted);
            ModLog(L"Default playback endpoint notification resolved to the already-bound endpoint; rebind skipped");
            return S_FALSE;
        }

        ModLog(L"Already-bound playback endpoint failed health check 0x%08X; forcing rebind",
               static_cast<unsigned>(queryHr));
    }

    if (state->endpointVolume && state->endpointCallback) {
        state->endpointVolume->UnregisterControlChangeNotify(
            state->endpointCallback.Get());
    }
    state->endpointVolume.Reset();
    state->endpointName = {};
    state->endpointId.clear();

    try {
        const std::wstring friendlyName =
            ReadEndpointFriendlyName(device.Get());
        if (!friendlyName.empty()) {
            state->endpointName = hstring(friendlyName.c_str());
            ModLog(L"Volume endpoint friendly name: %s",
                   state->endpointName.c_str());
        } else {
            ModLog(L"Volume endpoint friendly name unavailable; tooltip will use generic label");
        }
    } catch (...) {
        state->endpointName = {};
    }

    hr = device->Activate(__uuidof(IAudioEndpointVolume),
                          CLSCTX_INPROC_SERVER,
                          nullptr,
                          reinterpret_cast<void**>(
                              state->endpointVolume.GetAddressOf()));
    if (FAILED(hr)) {
        return hr;
    }

    hr = state->endpointVolume->RegisterControlChangeNotify(
        state->endpointCallback.Get());
    if (FAILED(hr)) {
        state->endpointVolume.Reset();
        return hr;
    }

    state->endpointId = newEndpointId;

    float scalar = 0.0f;
    bool muted = false;
    hr = QueryEndpointVolume(state->endpointVolume.Get(), &scalar, &muted);
    if (SUCCEEDED(hr)) {
        ApplyVolumeReading(state, scalar, muted);
    }

    ModLog(L"Live Volume endpoint bound (event-driven)%s",
           state->endpointId.empty()
               ? L""
               : L" with stable device identity");
    return hr;
}

LRESULT CALLBACK LegacyVolumeUiWndProc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam) {
    auto* holder = reinterpret_cast<std::shared_ptr<VolumeMonitorState>*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        holder = reinterpret_cast<std::shared_ptr<VolumeMonitorState>*>(
            create ? create->lpCreateParams : nullptr);
        if (!holder) {
            return FALSE;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(holder));
        return TRUE;
    }

    if (message == kLegacyVolumeUiReadingMessage) {
        if (holder && *holder) {
            auto state = *holder;
            // Clear first, then read the newest coalesced value. If another
            // callback races in after this point it will post a fresh message.
            state->legacyVolumePostPending.store(false);
            const unsigned signature = state->legacyPendingVolume.load();
            const int percent = static_cast<int>(signature & 0xFFu);
            const bool muted = (signature & 0x100u) != 0;
            if (!state->stopping.load()) {
                ApplyVolumeReading(state,
                                   static_cast<float>(percent) / 100.0f,
                                   muted);
            }
        }
        return 0;
    }

    if (message == kLegacyVolumeUiRebindMessage) {
        if (holder && *holder) {
            auto state = *holder;
            state->rebindPending.store(false);
            if (!state->stopping.load()) {
                HRESULT hr = RebindDefaultPlaybackEndpoint(state);
                if (FAILED(hr)) {
                    ModLog(L"Volume endpoint rebind failed: 0x%08X",
                           static_cast<unsigned>(hr));
                } else if (hr == S_FALSE) {
                    ModLog(L"Default playback endpoint unchanged; existing live Volume binding kept");
                } else {
                    ModLog(L"Default playback endpoint changed; live Volume rebound");
                }
            }
        }
        return 0;
    }

    if (message == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }

    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete holder;
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND CreateLegacyVolumeUiWindow(
    std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state) {
        return nullptr;
    }

    if (!EnsurePrivateProcessWindowClass(
            kLegacyVolumeUiWindowClass, LegacyVolumeUiWndProc)) {
        ModLog(L"Volume legacy UI bridge class unavailable; legacy handoff disabled for this mod instance");
        return nullptr;
    }

    auto* holder = new (std::nothrow)
        std::shared_ptr<VolumeMonitorState>(state);
    if (!holder) {
        ModLog(L"Volume legacy UI bridge couldn't allocate state holder");
        return nullptr;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kLegacyVolumeUiWindowClass,
        L"",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandleW(nullptr),
        holder);
    if (!hwnd) {
        const DWORD error = GetLastError();
        delete holder;
        ModLog(L"Volume legacy UI bridge CreateWindowEx failed: %lu", error);
        return nullptr;
    }

    return hwnd;
}

void QueueEndpointRebind(std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state || state->stopping.load() ||
        g_unloading.load(std::memory_order_acquire)) {
        return;
    }

    bool expected = false;
    if (!state->rebindPending.compare_exchange_strong(expected, true)) {
        return;
    }

    std::weak_ptr<VolumeMonitorState> weak = state;
    auto work = [weak]() {
        if (auto locked = weak.lock()) {
            locked->rebindPending.store(false);
            if (locked->stopping.load()) {
                return;
            }
            HRESULT hr = RebindDefaultPlaybackEndpoint(locked);
            if (FAILED(hr)) {
                    ModLog(L"Volume endpoint rebind failed: 0x%08X",
                           static_cast<unsigned>(hr));
                } else if (hr == S_FALSE) {
                    ModLog(L"Default playback endpoint unchanged; existing live Volume binding kept");
                } else {
                    ModLog(L"Default playback endpoint changed; live Volume rebound");
                }
        }
    };

    if (GetCurrentThreadId() == state->uiThreadId) {
        work();
        return;
    }

    switch (state->activeHandoff) {
        case VolumeUiHandoffMode::DispatcherQueue:
            if (state->dispatcherQueue) {
                try {
                    if (state->dispatcherQueue.TryEnqueue(work)) {
                        return;
                    }
                } catch (...) {
                }
            }
            break;

        case VolumeUiHandoffMode::CoreDispatcher:
            if (state->coreDispatcher) {
                try {
                    // As above, avoid a separate Completed delegate whose
                    // lifetime can extend past mod hot-unload.
                    state->coreDispatcher.TryRunAsync(
                        Core::CoreDispatcherPriority::Normal, work);
                    return;
                } catch (...) {
                }
            }
            break;

        case VolumeUiHandoffMode::LegacyMessageWindow:
            if (state->legacyUiWindow &&
                PostMessageW(state->legacyUiWindow,
                             kLegacyVolumeUiRebindMessage,
                             0,
                             0)) {
                return;
            }
            if (state->legacyUiWindow) {
                ModLog(L"Volume legacy UI bridge rebind PostMessage failed: %lu",
                       GetLastError());
            }
            break;

        case VolumeUiHandoffMode::Automatic:
        default:
            break;
    }

    state->rebindPending.store(false);
    ModLog(L"Default playback endpoint change dropped: selected UI handoff unavailable or failed");
}

HRESULT STDMETHODCALLTYPE EndpointVolumeCallback::OnNotify(
    PAUDIO_VOLUME_NOTIFICATION_DATA data) {
    AudioCallbackInFlightGuard guard;
    if (!data) {
        return E_INVALIDARG;
    }

    if (auto state = state_.lock()) {
        QueueVolumeReading(state,
                           data->fMasterVolume,
                           data->bMuted != FALSE);
    } else {
        ModLog(L"Volume endpoint callback fired after monitor state expired");
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DefaultDeviceNotificationCallback::OnDeviceStateChanged(
    LPCWSTR, DWORD) {
    AudioCallbackInFlightGuard guard;
    if (auto state = state_.lock()) {
        QueueEndpointRebind(state);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DefaultDeviceNotificationCallback::OnDeviceAdded(
    LPCWSTR) {
    AudioCallbackInFlightGuard guard;
    if (auto state = state_.lock()) {
        QueueEndpointRebind(state);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DefaultDeviceNotificationCallback::OnDeviceRemoved(
    LPCWSTR) {
    AudioCallbackInFlightGuard guard;
    if (auto state = state_.lock()) {
        QueueEndpointRebind(state);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DefaultDeviceNotificationCallback::OnDefaultDeviceChanged(
    EDataFlow flow,
    ERole role,
    LPCWSTR) {
    AudioCallbackInFlightGuard guard;
    if (flow == eRender &&
        (role == eMultimedia || role == eConsole)) {
        if (auto state = state_.lock()) {
            QueueEndpointRebind(state);
        }
    }
    return S_OK;
}

bool GetVariantByte(const VARIANT& value, int* result) {
    if (!result) {
        return false;
    }

    switch (value.vt) {
        case VT_UI1:
            *result = value.bVal;
            return true;
        case VT_I1:
            *result = static_cast<unsigned char>(value.cVal);
            return true;
        case VT_UI2:
            *result = value.uiVal;
            return true;
        case VT_I2:
            *result = value.iVal;
            return true;
        case VT_UI4:
            *result = static_cast<int>(value.ulVal);
            return true;
        case VT_I4:
            *result = value.lVal;
            return true;
        default:
            return false;
    }
}

bool GetVariantString(const VARIANT& value, std::wstring* result) {
    if (!result || value.vt != VT_BSTR || !value.bstrVal) {
        return false;
    }

    *result = value.bstrVal;
    return true;
}

HRESULT ConnectBrightnessWmi(
        winrt::com_ptr<IWbemServices>& services) {
    services = nullptr;

    CLSID clsidWbemLocator{};
    HRESULT hr = CLSIDFromString(
        L"{4590F811-1D3A-11D0-891F-00AA004B2E24}",
        &clsidWbemLocator);
    if (FAILED(hr)) {
        return hr;
    }

    winrt::com_ptr<IWbemLocator> locator;
    hr = CoCreateInstance(
        clsidWbemLocator,
        nullptr,
        CLSCTX_INPROC_SERVER,
        __uuidof(IWbemLocator),
        locator.put_void());
    if (FAILED(hr)) {
        return hr;
    }

    BSTR namespaceName = SysAllocString(L"ROOT\\WMI");
    if (!namespaceName) {
        return E_OUTOFMEMORY;
    }

    hr = locator->ConnectServer(
        namespaceName,
        nullptr,
        nullptr,
        nullptr,
        WBEM_FLAG_CONNECT_USE_MAX_WAIT,
        nullptr,
        nullptr,
        services.put());
    SysFreeString(namespaceName);
    if (FAILED(hr)) {
        services = nullptr;
        return hr;
    }

    // LogonUI can already have COM security initialized. Setting the proxy
    // blanket is sufficient for a local ROOT\\WMI call and avoids disturbing
    // process-wide COM security state.
    hr = CoSetProxyBlanket(
        services.get(),
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE);
    return hr;
}

HRESULT ExecBrightnessWmiQuery(
        IWbemServices* services,
        const wchar_t* query,
        winrt::com_ptr<IEnumWbemClassObject>& enumerator) {
    if (!services || !query) {
        return E_INVALIDARG;
    }

    BSTR language = SysAllocString(L"WQL");
    BSTR queryText = SysAllocString(query);
    if (!language || !queryText) {
        if (language) SysFreeString(language);
        if (queryText) SysFreeString(queryText);
        return E_OUTOFMEMORY;
    }

    HRESULT hr = services->ExecQuery(
        language,
        queryText,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        enumerator.put());

    SysFreeString(queryText);
    SysFreeString(language);
    return hr;
}

struct WmiBrightnessTarget {
    std::wstring instanceName;
    std::wstring methodPath;
    int currentPercent = 0;
};

bool QueryActiveBrightnessTargetsWmi(
        IWbemServices* services,
        std::vector<WmiBrightnessTarget>* targets) {
    if (!services || !targets) {
        return false;
    }

    targets->clear();

    struct BrightnessRow {
        std::wstring instanceName;
        int currentPercent = 0;
    };
    struct MethodRow {
        std::wstring instanceName;
        std::wstring path;
    };

    std::vector<BrightnessRow> brightnessRows;
    std::vector<MethodRow> methodRows;

    winrt::com_ptr<IEnumWbemClassObject> brightnessEnumerator;
    HRESULT hr = ExecBrightnessWmiQuery(
        services,
        L"SELECT InstanceName, CurrentBrightness "
        L"FROM WmiMonitorBrightness WHERE Active=TRUE",
        brightnessEnumerator);
    if (FAILED(hr) || !brightnessEnumerator) {
        ModLog(L"Battery brightness WMI query failed: 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    for (;;) {
        winrt::com_ptr<IWbemClassObject> object;
        ULONG returned = 0;
        hr = brightnessEnumerator->Next(2000, 1, object.put(), &returned);
        if (FAILED(hr)) {
            ModLog(L"Battery brightness WMI enumeration failed: 0x%08X",
                   static_cast<unsigned>(hr));
            return false;
        }
        if (returned == 0 || !object) {
            break;
        }

        VARIANT instanceName{};
        VARIANT currentBrightness{};
        VariantInit(&instanceName);
        VariantInit(&currentBrightness);

        const HRESULT instanceHr =
            object->Get(L"InstanceName", 0, &instanceName, nullptr, nullptr);
        const HRESULT brightnessHr =
            object->Get(L"CurrentBrightness", 0, &currentBrightness, nullptr, nullptr);

        std::wstring name;
        int percent = 0;
        const bool ok =
            SUCCEEDED(instanceHr) &&
            SUCCEEDED(brightnessHr) &&
            GetVariantString(instanceName, &name) &&
            GetVariantByte(currentBrightness, &percent);

        VariantClear(&currentBrightness);
        VariantClear(&instanceName);

        if (!ok) {
            ModLog(L"Battery brightness WMI skipped an active monitor with incomplete InstanceName/CurrentBrightness data");
            continue;
        }

        brightnessRows.push_back(
            {std::move(name), std::clamp(percent, 0, 100)});
    }

    if (brightnessRows.empty()) {
        ModLog(L"Battery brightness WMI found no active monitor");
        return false;
    }

    winrt::com_ptr<IEnumWbemClassObject> methodEnumerator;
    hr = ExecBrightnessWmiQuery(
        services,
        L"SELECT * FROM WmiMonitorBrightnessMethods WHERE Active=TRUE",
        methodEnumerator);
    if (FAILED(hr) || !methodEnumerator) {
        ModLog(L"Battery brightness WMI methods query failed: 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    for (;;) {
        winrt::com_ptr<IWbemClassObject> object;
        ULONG returned = 0;
        hr = methodEnumerator->Next(2000, 1, object.put(), &returned);
        if (FAILED(hr)) {
            ModLog(L"Battery brightness WMI methods enumeration failed: 0x%08X",
                   static_cast<unsigned>(hr));
            return false;
        }
        if (returned == 0 || !object) {
            break;
        }

        VARIANT instanceName{};
        VARIANT path{};
        VariantInit(&instanceName);
        VariantInit(&path);

        const HRESULT instanceHr =
            object->Get(L"InstanceName", 0, &instanceName, nullptr, nullptr);
        const HRESULT pathHr =
            object->Get(L"__PATH", 0, &path, nullptr, nullptr);

        std::wstring name;
        std::wstring objectPath;
        const bool ok =
            SUCCEEDED(instanceHr) &&
            SUCCEEDED(pathHr) &&
            GetVariantString(instanceName, &name) &&
            GetVariantString(path, &objectPath);

        VariantClear(&path);
        VariantClear(&instanceName);

        if (!ok) {
            ModLog(L"Battery brightness WMI skipped an active method instance with incomplete InstanceName/__PATH data");
            continue;
        }

        methodRows.push_back(
            {std::move(name), std::move(objectPath)});
    }

    for (const auto& brightness : brightnessRows) {
        const MethodRow* matchingMethod = nullptr;
        for (const auto& method : methodRows) {
            if (_wcsicmp(
                    brightness.instanceName.c_str(),
                    method.instanceName.c_str()) == 0) {
                matchingMethod = &method;
                break;
            }
        }

        // Some firmware exposes a single usable pair but formats InstanceName
        // slightly differently between the value/method classes. Preserve the
        // historical single-panel behavior in that unambiguous case.
        if (!matchingMethod &&
            brightnessRows.size() == 1 &&
            methodRows.size() == 1) {
            matchingMethod = &methodRows.front();
            ModLog(L"Battery brightness WMI single-monitor InstanceName mismatch; using the only active method instance");
        }

        if (!matchingMethod) {
            ModLog(L"Battery brightness WMI has no method instance matching monitor=%s",
                   brightness.instanceName.c_str());
            continue;
        }

        targets->push_back({
            brightness.instanceName,
            matchingMethod->path,
            brightness.currentPercent,
        });
    }

    if (targets->empty()) {
        ModLog(L"Battery brightness WMI found active monitors but no usable value/method InstanceName pairs");
        return false;
    }

    ModLog(L"Battery brightness WMI paired %u active monitor(s) by InstanceName",
           static_cast<unsigned>(targets->size()));
    return true;
}

bool SetBrightnessWmiTarget(
        IWbemServices* services,
        const std::wstring& methodPath,
        int targetPercent) {
    if (!services || methodPath.empty()) {
        return false;
    }

    BSTR className =
        SysAllocString(L"WmiMonitorBrightnessMethods");
    if (!className) {
        return false;
    }

    winrt::com_ptr<IWbemClassObject> classObject;
    HRESULT hr = services->GetObject(
        className,
        0,
        nullptr,
        classObject.put(),
        nullptr);
    SysFreeString(className);
    if (FAILED(hr) || !classObject) {
        ModLog(L"Battery brightness WMI class lookup failed: 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    winrt::com_ptr<IWbemClassObject> inputSignature;
    hr = classObject->GetMethod(
        L"WmiSetBrightness",
        0,
        inputSignature.put(),
        nullptr);
    if (FAILED(hr) || !inputSignature) {
        ModLog(L"Battery brightness WMI GetMethod failed: 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    winrt::com_ptr<IWbemClassObject> inputParameters;
    hr = inputSignature->SpawnInstance(
        0, inputParameters.put());
    if (FAILED(hr) || !inputParameters) {
        ModLog(L"Battery brightness WMI SpawnInstance failed: 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    VARIANT timeout{};
    VariantInit(&timeout);
    // WMI Automation represents CIM uint32 as VT_I4.
    timeout.vt = VT_I4;
    timeout.lVal = 0;

    VARIANT brightness{};
    VariantInit(&brightness);
    // CIM uint8 maps directly to VT_UI1.
    brightness.vt = VT_UI1;
    brightness.bVal =
        static_cast<UCHAR>(std::clamp(targetPercent, 0, 100));

    hr = inputParameters->Put(
        L"Timeout", 0, &timeout, 0);
    if (FAILED(hr)) {
        ModLog(L"Battery brightness WMI Timeout parameter setup failed: 0x%08X (vt=VT_I4)",
               static_cast<unsigned>(hr));
        return false;
    }

    hr = inputParameters->Put(
        L"Brightness", 0, &brightness, 0);
    if (FAILED(hr)) {
        ModLog(L"Battery brightness WMI Brightness parameter setup failed: 0x%08X (vt=VT_UI1 target=%d%%)",
               static_cast<unsigned>(hr),
               std::clamp(targetPercent, 0, 100));
        return false;
    }

    BSTR objectPath = SysAllocString(methodPath.c_str());
    BSTR methodName = SysAllocString(L"WmiSetBrightness");
    if (!objectPath || !methodName) {
        if (objectPath) SysFreeString(objectPath);
        if (methodName) SysFreeString(methodName);
        return false;
    }

    winrt::com_ptr<IWbemClassObject> outputParameters;
    hr = services->ExecMethod(
        objectPath,
        methodName,
        0,
        nullptr,
        inputParameters.get(),
        outputParameters.put(),
        nullptr);
    SysFreeString(methodName);
    SysFreeString(objectPath);

    if (FAILED(hr)) {
        ModLog(L"Battery brightness WMI ExecMethod failed: 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    if (outputParameters) {
        VARIANT returnValue{};
        VariantInit(&returnValue);
        if (SUCCEEDED(outputParameters->Get(
                L"ReturnValue",
                0,
                &returnValue,
                nullptr,
                nullptr))) {
            int methodResult = 0;
            if (GetVariantByte(returnValue, &methodResult) &&
                methodResult != 0) {
                ModLog(L"Battery brightness WMI method returned %d",
                       methodResult);
                VariantClear(&returnValue);
                return false;
            }
        }
        VariantClear(&returnValue);
    }

    return true;
}

bool AdjustBrightnessLegacyIoctl(int percentDelta) {
    HANDLE lcd = CreateFileW(
        L"\\\\.\\LCD",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (lcd == INVALID_HANDLE_VALUE) {
        ModLog(L"Battery brightness legacy backend unavailable error=%lu",
               GetLastError());
        return false;
    }

    DisplayBrightnessIoctl current{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(
            lcd,
            IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS,
            nullptr,
            0,
            &current,
            sizeof(current),
            &bytesReturned,
            nullptr)) {
        ModLog(L"Battery brightness legacy query failed error=%lu",
               GetLastError());
        CloseHandle(lcd);
        return false;
    }

    SYSTEM_POWER_STATUS powerStatus{};
    const bool havePowerStatus =
        GetSystemPowerStatus(&powerStatus) != FALSE;
    const bool onAc =
        havePowerStatus && powerStatus.ACLineStatus == 1;
    const bool onBattery =
        havePowerStatus && powerStatus.ACLineStatus == 0;

    int currentPercent =
        onAc ? current.acBrightness :
        onBattery ? current.dcBrightness :
        current.displayPolicy == kDisplayPolicyDc
            ? current.dcBrightness
            : current.acBrightness;
    currentPercent = std::clamp(currentPercent, 0, 100);

    const int desiredPercent = std::clamp(
        currentPercent + percentDelta, 0, 100);

    UCHAR supportedLevels[256]{};
    DWORD supportedBytes = 0;
    int targetPercent = desiredPercent;
    if (DeviceIoControl(
            lcd,
            IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS,
            nullptr,
            0,
            supportedLevels,
            sizeof(supportedLevels),
            &supportedBytes,
            nullptr) &&
        supportedBytes > 0) {
        int best = static_cast<int>(supportedLevels[0]);
        int bestDistance = std::abs(best - desiredPercent);
        const DWORD count =
            std::min<DWORD>(
                supportedBytes,
                ARRAYSIZE(supportedLevels));
        for (DWORD i = 1; i < count; ++i) {
            const int level =
                static_cast<int>(supportedLevels[i]);
            const int distance =
                std::abs(level - desiredPercent);
            if (distance < bestDistance) {
                best = level;
                bestDistance = distance;
            }
        }
        targetPercent = std::clamp(best, 0, 100);
    }

    DisplayBrightnessIoctl requested = current;
    if (onAc) {
        requested.displayPolicy = kDisplayPolicyAc;
        requested.acBrightness =
            static_cast<UCHAR>(targetPercent);
    } else if (onBattery) {
        requested.displayPolicy = kDisplayPolicyDc;
        requested.dcBrightness =
            static_cast<UCHAR>(targetPercent);
    } else {
        requested.displayPolicy = kDisplayPolicyBoth;
        requested.acBrightness =
            static_cast<UCHAR>(targetPercent);
        requested.dcBrightness =
            static_cast<UCHAR>(targetPercent);
    }

    bytesReturned = 0;
    const BOOL ok = DeviceIoControl(
        lcd,
        IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS,
        &requested,
        sizeof(requested),
        nullptr,
        0,
        &bytesReturned,
        nullptr);
    const DWORD error =
        ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(lcd);

    if (!ok) {
        ModLog(L"Battery brightness legacy set %d%% failed error=%lu",
               targetPercent,
               error);
        return false;
    }

    ModLog(L"Battery brightness legacy set: current=%d%% target=%d%%",
           currentPercent,
           targetPercent);
    return true;
}

DWORD WINAPI BrightnessWorkerThreadProc(void*) {
    HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom = SUCCEEDED(comResult);

    winrt::com_ptr<IWbemServices> services;
    HRESULT wmiResult = ConnectBrightnessWmi(services);
    if (SUCCEEDED(wmiResult) && services) {
        ModLog(L"Battery brightness worker: WMI backend ready");
    } else {
        ModLog(L"Battery brightness worker: WMI backend unavailable 0x%08X; legacy backend remains available",
               static_cast<unsigned>(wmiResult));
        services = nullptr;
    }

    std::vector<WmiBrightnessTarget> desiredBrightnessTargets;
    ULONGLONG lastDesiredUpdateTick = 0;
    constexpr ULONGLONG kBrightnessCacheIdleResyncMs = 700;

    while (!g_brightnessWorkerStop.load(
                std::memory_order_acquire)) {
        HANDLE eventHandle = nullptr;
        AcquireSRWLockShared(&g_brightnessWorkerLock);
        eventHandle = g_brightnessWorkerEvent;
        ReleaseSRWLockShared(&g_brightnessWorkerLock);

        if (!eventHandle) {
            break;
        }

        const DWORD wait =
            WaitForSingleObject(eventHandle, INFINITE);
        if (wait != WAIT_OBJECT_0) {
            break;
        }

        if (g_brightnessWorkerStop.load(
                std::memory_order_acquire)) {
            break;
        }

        const int percentDelta =
            g_brightnessPendingPercent.exchange(
                0, std::memory_order_acq_rel);
        if (percentDelta == 0) {
            continue;
        }

        bool applied = false;
        if (services) {
            const ULONGLONG now = GetTickCount64();
            const bool cacheExpired =
                desiredBrightnessTargets.empty() ||
                lastDesiredUpdateTick == 0 ||
                now - lastDesiredUpdateTick >
                    kBrightnessCacheIdleResyncMs;

            if (cacheExpired) {
                std::vector<WmiBrightnessTarget> refreshedTargets;
                if (QueryActiveBrightnessTargetsWmi(
                        services.get(), &refreshedTargets)) {
                    desiredBrightnessTargets =
                        std::move(refreshedTargets);
                    lastDesiredUpdateTick = now;
                    ModLog(L"Battery brightness WMI desired-value cache resynced for %u monitor(s)",
                           static_cast<unsigned>(
                               desiredBrightnessTargets.size()));
                } else {
                    desiredBrightnessTargets.clear();
                    lastDesiredUpdateTick = 0;
                }
            }

            bool anyWmiApplied = false;
            bool anyWmiFailed = false;
            for (auto& target : desiredBrightnessTargets) {
                const int basePercent =
                    std::clamp(target.currentPercent, 0, 100);
                const int targetPercent = std::clamp(
                    basePercent + percentDelta,
                    0,
                    100);

                if (targetPercent == basePercent) {
                    anyWmiApplied = true;
                    continue;
                }

                if (SetBrightnessWmiTarget(
                        services.get(),
                        target.methodPath,
                        targetPercent)) {
                    target.currentPercent = targetPercent;
                    anyWmiApplied = true;
                    ModLog(L"Battery brightness WMI set: monitor=%s delta=%+d%% base=%d%% target=%d%% cachedDesired=1",
                           target.instanceName.c_str(),
                           percentDelta,
                           basePercent,
                           targetPercent);
                } else {
                    anyWmiFailed = true;
                    ModLog(L"Battery brightness WMI set failed for monitor=%s",
                           target.instanceName.c_str());
                }
            }

            if (anyWmiApplied) {
                applied = true;
                lastDesiredUpdateTick =
                    anyWmiFailed ? 0 : now;
            } else if (anyWmiFailed) {
                desiredBrightnessTargets.clear();
                lastDesiredUpdateTick = 0;
            }
        }

        if (!applied) {
            applied = AdjustBrightnessLegacyIoctl(percentDelta);
            if (applied) {
                desiredBrightnessTargets.clear();
                lastDesiredUpdateTick = 0;
            }
        }

        if (!applied) {
            desiredBrightnessTargets.clear();
            lastDesiredUpdateTick = 0;
            ModLog(L"Battery brightness adjustment failed on all available backends");
        }
    }

    services = nullptr;
    if (uninitializeCom) {
        CoUninitialize();
    }
    return 0;
}

bool EnsureBrightnessWorkerStarted() {
    AcquireSRWLockExclusive(&g_brightnessWorkerLock);

    if (g_brightnessWorkerThread &&
        g_brightnessWorkerEvent) {
        ReleaseSRWLockExclusive(&g_brightnessWorkerLock);
        return true;
    }

    g_brightnessWorkerStop.store(
        false, std::memory_order_release);

    HANDLE eventHandle = CreateEventW(
        nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle) {
        const DWORD error = GetLastError();
        ReleaseSRWLockExclusive(&g_brightnessWorkerLock);
        ModLog(L"Couldn't create Battery brightness worker event error=%lu",
               error);
        return false;
    }

    HANDLE threadHandle = CreateThread(
        nullptr,
        0,
        BrightnessWorkerThreadProc,
        nullptr,
        0,
        nullptr);
    if (!threadHandle) {
        const DWORD error = GetLastError();
        CloseHandle(eventHandle);
        ReleaseSRWLockExclusive(&g_brightnessWorkerLock);
        ModLog(L"Couldn't create Battery brightness worker thread error=%lu",
               error);
        return false;
    }

    g_brightnessWorkerEvent = eventHandle;
    g_brightnessWorkerThread = threadHandle;

    ReleaseSRWLockExclusive(&g_brightnessWorkerLock);

    ModLog(L"Battery brightness worker started (event-driven; WMI preferred, legacy fallback)");
    return true;
}

void StopBrightnessWorker() {
    HANDLE eventHandle = nullptr;
    HANDLE threadHandle = nullptr;

    AcquireSRWLockExclusive(&g_brightnessWorkerLock);
    g_brightnessWorkerStop.store(
        true, std::memory_order_release);
    eventHandle = g_brightnessWorkerEvent;
    threadHandle = g_brightnessWorkerThread;
    ReleaseSRWLockExclusive(&g_brightnessWorkerLock);

    if (eventHandle) {
        SetEvent(eventHandle);
    }
    if (threadHandle) {
        // Hot-unload must never abandon a worker whose instruction pointer can
        // still be inside this DLL. WMI calls are bounded in normal operation,
        // so wait for the worker to actually exit rather than timing out and
        // closing only our handle while the thread keeps running.
        WaitForSingleObject(threadHandle, INFINITE);
    }

    AcquireSRWLockExclusive(&g_brightnessWorkerLock);
    if (g_brightnessWorkerThread) {
        CloseHandle(g_brightnessWorkerThread);
        g_brightnessWorkerThread = nullptr;
    }
    if (g_brightnessWorkerEvent) {
        CloseHandle(g_brightnessWorkerEvent);
        g_brightnessWorkerEvent = nullptr;
    }
    g_brightnessPendingPercent.store(
        0, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_brightnessWorkerLock);
}

bool QueueBrightnessAdjustment(
        int percentDelta,
        int logicalSteps,
        int effectiveDelta,
        int remainder,
        int deltaPerStep,
        bool highResolution,
        Input::PointerRoutedEventArgs const& args) {
    if (g_batteryWheelAction.load(
            std::memory_order_relaxed) !=
            BatteryWheelAction::AdjustBrightness ||
        percentDelta == 0 ||
        logicalSteps == 0) {
        return false;
    }

    if (!EnsureBrightnessWorkerStarted()) {
        return false;
    }

    g_brightnessPendingPercent.fetch_add(
        percentDelta, std::memory_order_acq_rel);

    HANDLE eventHandle = nullptr;
    AcquireSRWLockShared(&g_brightnessWorkerLock);
    eventHandle = g_brightnessWorkerEvent;
    ReleaseSRWLockShared(&g_brightnessWorkerLock);

    if (!eventHandle || !SetEvent(eventHandle)) {
        return false;
    }

    args.Handled(true);
    ModLog(L"Battery wheel/brightness queued: source=%s delta=%d logicalSteps=%d step=%d%% deltaPerStep=%d remainder=%d pendingDelta=%+d%%",
           highResolution ? L"high-resolution" : L"detent",
           effectiveDelta,
           logicalSteps,
           std::abs(percentDelta / logicalSteps),
           deltaPerStep,
           remainder,
           g_brightnessPendingPercent.load(
               std::memory_order_relaxed));
    return true;
}


struct BatteryWheelRegistration {
    winrt::weak_ref<Controls::Button> button;
    winrt::Windows::Foundation::IInspectable handler{nullptr};
    bool attached = false;
};

void DetachBatteryBrightnessWheelHandler(
        std::shared_ptr<BatteryWheelRegistration> const& registration) {
    if (!registration || !registration->attached) {
        return;
    }

    auto button = registration->button.get();
    if (button && registration->handler) {
        try {
            button.RemoveHandler(
                UIElement::PointerWheelChangedEvent(),
                registration->handler);
        } catch (...) {
        }
    }

    registration->handler = nullptr;
    registration->attached = false;
}

std::shared_ptr<BatteryWheelRegistration>
AttachBatteryBrightnessWheelHandler(
        Controls::Button const& batteryButton) {
    if (!batteryButton) {
        return {};
    }

    try {
        auto weakButton = winrt::make_weak(batteryButton);
        auto wheelAccumulator =
            std::make_shared<WheelDeltaAccumulator>();

        Input::PointerEventHandler handler{
            [weakButton, wheelAccumulator](
                winrt::Windows::Foundation::IInspectable const&,
                Input::PointerRoutedEventArgs const& args) {
                if (g_unloading.load(std::memory_order_acquire) ||
                    g_batteryWheelAction.load(std::memory_order_relaxed) !=
                        BatteryWheelAction::AdjustBrightness) {
                    return;
                }

                auto button = weakButton.get();
                if (!button || !wheelAccumulator) {
                    return;
                }

                WheelInputResult wheel{};
                if (!TryConsumeVerticalWheelInput(
                        button,
                        args,
                        g_batteryWheelReverse.load(
                            std::memory_order_relaxed),
                        *wheelAccumulator,
                        &wheel)) {
                    return;
                }

                if (wheel.steps == 0) {
                    args.Handled(true);
                    return;
                }

                const int stepPercent =
                    wheel.highResolution
                        ? g_precisionScrollStepPercent.load(
                              std::memory_order_relaxed)
                        : g_batteryWheelStepPercent.load(
                              std::memory_order_relaxed);
                QueueBrightnessAdjustment(
                    wheel.steps * stepPercent,
                    wheel.steps,
                    wheel.effectiveDelta,
                    wheel.remainder,
                    wheel.deltaPerStep,
                    wheel.highResolution,
                    args);
            }};

        auto boxedHandler = winrt::box_value(handler);
        batteryButton.AddHandler(
            UIElement::PointerWheelChangedEvent(),
            boxedHandler,
            true);

        auto registration =
            std::make_shared<BatteryWheelRegistration>();
        registration->button = weakButton;
        registration->handler = boxedHandler;
        registration->attached = true;

        ModLog(L"Battery wheel/brightness routed handler attached (handledEventsToo=1; shared high-resolution classifier/variable-threshold accumulator)");
        return registration;
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Couldn't attach Battery wheel/brightness handler: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"Couldn't attach Battery wheel/brightness handler");
    }

    return {};
}

bool AdjustVolumeFromWheelSteps(
        std::shared_ptr<VolumeMonitorState> const& current,
        int steps,
        int effectiveDelta,
        int remainder,
        int deltaPerStep,
        bool highResolution,
        Input::PointerRoutedEventArgs const& args) {
    if (!current || current->stopping.load() ||
        !current->endpointVolume ||
        g_volumeWheelAction.load(std::memory_order_relaxed) !=
            VolumeWheelAction::Adjust ||
        steps == 0) {
        return false;
    }

    float scalar = 0.0f;
    HRESULT hr =
        current->endpointVolume->GetMasterVolumeLevelScalar(&scalar);
    if (FAILED(hr)) {
        ModLog(L"Volume wheel/touchpad: GetMasterVolumeLevelScalar failed 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    const int stepPercent =
        highResolution
            ? g_precisionScrollStepPercent.load(
                  std::memory_order_relaxed)
            : g_volumeWheelStepPercent.load(
                  std::memory_order_relaxed);
    const float step =
        static_cast<float>(stepPercent) / 100.0f;
    const float target = std::clamp(
        scalar + (step * static_cast<float>(steps)),
        0.0f,
        1.0f);

    if (std::fabs(target - scalar) < 0.0001f) {
        args.Handled(true);
        return true;
    }

    hr = current->endpointVolume->SetMasterVolumeLevelScalar(
        target, nullptr);
    if (FAILED(hr)) {
        ModLog(L"Volume wheel/touchpad: SetMasterVolumeLevelScalar failed 0x%08X",
               static_cast<unsigned>(hr));
        return false;
    }

    if (g_volumeWheelUnmute.load(std::memory_order_relaxed)) {
        current->endpointVolume->SetMute(FALSE, nullptr);
    }

    args.Handled(true);
    ModLog(L"Volume wheel/touchpad: source=%s delta=%d logicalSteps=%d step=%d%% deltaPerStep=%d remainder=%d target=%d%%",
           highResolution ? L"high-resolution" : L"detent",
           effectiveDelta,
           steps,
           stepPercent,
           deltaPerStep,
           remainder,
           static_cast<int>(std::lround(target * 100.0f)));
    return true;
}

void AttachVolumeWheelHandler(
        std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state || !state->button || state->wheelHandlerAttached) {
        return;
    }

    std::weak_ptr<VolumeMonitorState> weak = state;
    try {
        Input::PointerEventHandler handler{
            [weak](winrt::Windows::Foundation::IInspectable const&,
                   Input::PointerRoutedEventArgs const& args) {
                auto current = weak.lock();
                if (!current || current->stopping.load() ||
                    !current->endpointVolume ||
                    g_volumeWheelAction.load(std::memory_order_relaxed) !=
                        VolumeWheelAction::Adjust) {
                    return;
                }

                WheelInputResult wheel{};
                if (!TryConsumeVerticalWheelInput(
                        current->button,
                        args,
                        g_volumeWheelReverse.load(
                            std::memory_order_relaxed),
                        current->wheelAccumulator,
                        &wheel)) {
                    return;
                }

                if (wheel.steps == 0) {
                    args.Handled(true);
                    return;
                }

                AdjustVolumeFromWheelSteps(
                    current,
                    wheel.steps,
                    wheel.effectiveDelta,
                    wheel.remainder,
                    wheel.deltaPerStep,
                    wheel.highResolution,
                    args);
            }};

        auto boxedHandler = winrt::box_value(handler);

        // PointerWheelChanged is routed. Using AddHandler with
        // handledEventsToo=true makes this work even if Button/template code
        // or another XAML ancestor has already marked the touchpad/wheel event
        // handled before ordinary user handlers would see it.
        state->button.AddHandler(
            UIElement::PointerWheelChangedEvent(),
            boxedHandler,
            true);

        state->wheelHandler = boxedHandler;
        state->wheelHandlerAttached = true;
        ModLog(L"Volume wheel/touchpad routed handler attached (handledEventsToo=1; shared high-resolution classifier/variable-threshold accumulator; action is runtime-configurable)");
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Couldn't attach Volume wheel/touchpad routed handler: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"Couldn't attach Volume wheel/touchpad routed handler");
    }
}

[[maybe_unused]] void StopVolumeMonitor(std::shared_ptr<VolumeMonitorState> const& state) {
    if (!state || state->cleanupDone.exchange(true)) {
        return;
    }
    state->stopping.store(true, std::memory_order_release);

    if (state->endpointVolume && state->endpointCallback) {
        state->endpointVolume->UnregisterControlChangeNotify(
            state->endpointCallback.Get());
    }
    if (state->enumerator && state->deviceCallback) {
        state->enumerator->UnregisterEndpointNotificationCallback(
            state->deviceCallback.Get());
    }

    // During hot-disable, keep our own COM references alive until callbacks
    // which were already inside the notification objects have returned. This
    // prevents the final Release from deleting a callback object underneath an
    // in-flight method after the registration itself has been removed.
    if (g_unloading.load(std::memory_order_acquire)) {
        while (g_audioCallbacksInFlight.load(std::memory_order_acquire) != 0) {
            Sleep(1);
        }
    }

    state->endpointVolume.Reset();
    state->endpointCallback.Reset();
    state->deviceCallback.Reset();
    state->enumerator.Reset();

    if (state->legacyUiWindow) {
        HWND hwnd = state->legacyUiWindow;
        state->legacyUiWindow = nullptr;
        if (GetCurrentThreadId() == state->uiThreadId) {
            DestroyWindow(hwnd);
        } else if (g_unloading.load(std::memory_order_acquire)) {
            DWORD_PTR ignored = 0;
            SendMessageTimeoutW(
                hwnd, WM_CLOSE, 0, 0,
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &ignored);
        } else {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
    }
    UnregisterPrivateProcessWindowClass(
        kLegacyVolumeUiWindowClass, LegacyVolumeUiWndProc);

    if (state->button && state->wheelHandlerAttached &&
        state->wheelHandler) {
        try {
            state->button.RemoveHandler(
                UIElement::PointerWheelChangedEvent(),
                state->wheelHandler);
        } catch (...) {
        }
        state->wheelHandler = nullptr;
        state->wheelHandlerAttached = false;
    }

    state->icon = nullptr;
    state->percentText = nullptr;
    state->button = nullptr;
    state->toolTip = nullptr;
    state->endpointName = {};
    state->endpointId.clear();
    state->nativeSndVol = {};
    state->dispatcherQueue = nullptr;
    state->coreDispatcher = nullptr;
}

std::shared_ptr<VolumeMonitorState> StartVolumeMonitor(Controls::UserControl const& owner,
                        Controls::Button const& volumeButton,
                        Controls::TextBlock const& volumeIcon,
                        Controls::TextBlock const& volumePercentText,
                        Controls::ToolTip const& volumeToolTip,
                        wchar_t noEndpointGlyph,
                        NativeSndVolVisuals const& nativeSndVol) {
    std::shared_ptr<VolumeMonitorState> state;
    try {
        state = std::make_shared<VolumeMonitorState>();
        state->icon = volumeIcon;
        state->percentText = volumePercentText;
        state->button = volumeButton;
        state->toolTip = volumeToolTip;
        state->noEndpointGlyph = noEndpointGlyph;
        state->nativeSndVol = nativeSndVol;
        state->uiThreadId = GetCurrentThreadId();
        state->requestedHandoff = g_volumeUiHandoff.load();

        // Probe capabilities on the actual LogonUI XAML UI thread. Automatic
        // is intentionally capability-based, never keyed to an OS build.
        state->dispatcherQueue = System::DispatcherQueue::GetForCurrentThread();
        state->coreDispatcher = owner.Dispatcher();

        if (state->requestedHandoff == VolumeUiHandoffMode::Automatic) {
            if (state->dispatcherQueue) {
                state->activeHandoff = VolumeUiHandoffMode::DispatcherQueue;
            } else if (state->coreDispatcher) {
                state->activeHandoff = VolumeUiHandoffMode::CoreDispatcher;
            } else {
                state->activeHandoff = VolumeUiHandoffMode::LegacyMessageWindow;
            }
        } else {
            state->activeHandoff = state->requestedHandoff;
        }

        if (state->activeHandoff == VolumeUiHandoffMode::LegacyMessageWindow) {
            state->legacyUiWindow = CreateLegacyVolumeUiWindow(state);
        }

        AttachVolumeWheelHandler(state);

        ModLog(L"Volume UI handoff: requested=%s active=%s thread=%u DispatcherQueue=%d CoreDispatcher=%d LegacyMessageWindow=%d",
               VolumeUiHandoffModeName(state->requestedHandoff),
               VolumeUiHandoffModeName(state->activeHandoff),
               state->uiThreadId,
               state->dispatcherQueue ? 1 : 0,
               state->coreDispatcher ? 1 : 0,
               state->legacyUiWindow ? 1 : 0);

        if (state->activeHandoff == VolumeUiHandoffMode::DispatcherQueue &&
            !state->dispatcherQueue) {
            ModLog(L"WARNING: DispatcherQueue was explicitly selected but no queue is associated with the LogonUI UI thread; initial state works, live cross-thread updates will be dropped");
        } else if (state->activeHandoff == VolumeUiHandoffMode::CoreDispatcher &&
                   !state->coreDispatcher) {
            ModLog(L"WARNING: CoreDispatcher was explicitly selected but unavailable on this LogonUI host; initial state works, live cross-thread updates will be dropped");
        } else if (state->activeHandoff == VolumeUiHandoffMode::LegacyMessageWindow &&
                   !state->legacyUiWindow) {
            ModLog(L"WARNING: legacy message-window handoff was selected but the bridge window couldn't be created; initial state works, live cross-thread updates will be dropped");
        }

        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&state->enumerator));
        if (FAILED(hr)) {
            ModLog(L"Couldn't create audio endpoint enumerator: 0x%08X",
                   static_cast<unsigned>(hr));
            return state;
        }

        auto* endpointCallback = new (std::nothrow) EndpointVolumeCallback(
            std::weak_ptr<VolumeMonitorState>(state));
        auto* deviceCallback = new (std::nothrow) DefaultDeviceNotificationCallback(
            std::weak_ptr<VolumeMonitorState>(state));
        if (!endpointCallback || !deviceCallback) {
            if (endpointCallback) {
                endpointCallback->Release();
            }
            if (deviceCallback) {
                deviceCallback->Release();
            }
            ModLog(L"Couldn't allocate live Volume callbacks");
            return state;
        }
        state->endpointCallback.Attach(endpointCallback);
        state->deviceCallback.Attach(deviceCallback);

        hr = state->enumerator->RegisterEndpointNotificationCallback(
            state->deviceCallback.Get());
        if (FAILED(hr)) {
            ModLog(L"Couldn't register default-device notification: 0x%08X",
                   static_cast<unsigned>(hr));
            return state;
        }

        hr = RebindDefaultPlaybackEndpoint(state);
        if (FAILED(hr)) {
            if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
                ModLog(L"No default playback endpoint yet (0x%08X); showing native disabled-audio state and remaining armed for device arrival",
                       static_cast<unsigned>(hr));
                ModLog(L"Live Volume watcher started (event-driven; waiting for endpoint; no polling)");
                return state;
            }

            state->enumerator->UnregisterEndpointNotificationCallback(
                state->deviceCallback.Get());
            state->stopping.store(true);
            ModLog(L"Couldn't bind live Volume endpoint: 0x%08X",
                   static_cast<unsigned>(hr));
            return state;
        }

        ModLog(L"Live Volume watcher started (event-driven; no polling)");
        return state;
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Couldn't start live Volume watcher: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"Couldn't start live Volume watcher: unknown exception");
    }
    return state;
}

bool GetOsBuild(DWORD* build, DWORD* ubr) {
    *build = 0;
    *ubr = 0;

    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }

    using RtlGetVersion_t = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    auto rtlGetVersion = reinterpret_cast<RtlGetVersion_t>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtlGetVersion) {
        return false;
    }

    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtlGetVersion(&version) != 0) {
        return false;
    }

    DWORD value = 0;
    DWORD valueSize = sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                     L"UBR",
                     RRF_RT_REG_DWORD,
                     nullptr,
                     &value,
                     &valueSize) != ERROR_SUCCESS) {
        // Build logging is diagnostic only now; a missing UBR must not block
        // symbol-based hook discovery.
        value = 0;
    }

    *build = version.dwBuildNumber;
    *ubr = value;
    return true;
}

bool IsOnLoadedSymbolName(PCWSTR symbol) {
    if (!symbol) {
        return false;
    }

    constexpr std::wstring_view needle =
        L"LogonUX::SystemProviderUserControl::_OnLoaded";
    const std::wstring_view value(symbol);
    const size_t pos = value.find(needle);
    if (pos == std::wstring_view::npos) {
        return false;
    }

    // Accept the short DbgHelp form and full undecorated signatures, but not
    // EH helper symbols such as `...::_OnLoaded'::`1'::dtor$...`.
    const size_t after = pos + needle.size();
    if (after == value.size()) {
        return true;
    }

    return value[after] == L'(';
}


bool IsExpectedOnLoadedMemberOffset(DWORD offset) {
    // Harvested/validated SystemProviderUserControl::_OnLoaded generations:
    //   17763: 0x1D0
    //   18362/19041/22000: 0x1E0
    //   22621: 0x1D8 / 0x1E8
    //   26100 / current 26200 builds: 0x1D8 / 0x1E0
    switch (offset) {
        case 0x1D0:
        case 0x1D8:
        case 0x1E0:
        case 0x1E8:
            return true;
        default:
            return false;
    }
}

bool MatchOnLoadedStructuralCandidate(
        const StobjectPeView& view,
        DWORD rva,
        DWORD functionEnd,
        const wchar_t** shapeLabel,
        DWORD* memberOffset) {
    if (!shapeLabel || !memberOffset ||
        functionEnd <= rva ||
        !IsPeRvaRangeValid(view, rva, 64)) {
        return false;
    }

    // All harvested targets are substantial event handlers. This is only a
    // broad sanity bound; the byte/semantic checks below carry the identity.
    const DWORD functionSize = functionEnd - rva;
    if (functionSize < 0x140 || functionSize > 0x500) {
        return false;
    }

    const BYTE* p = view.base + rva;

    // Legacy compiler shape observed throughout Windows 10 1809 and 1903
    // (17763 / 18362). Relative call targets and the class member offset are
    // intentionally not hard-coded.
    static constexpr BYTE kLegacyPrefix[] = {
        0x48, 0x8B, 0xC4,                         // mov rax,rsp
        0x55, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8B, 0xEC,                         // mov rbp,rsp
        0x48, 0x83, 0xEC, 0x40,                   // sub rsp,40h
        0x48, 0xC7, 0x45, 0xE0, 0xFE, 0xFF, 0xFF, 0xFF,
        0x48, 0x89, 0x58, 0x10,
        0x48, 0x89, 0x70, 0x18
    };

    if (memcmp(p, kLegacyPrefix, sizeof(kLegacyPrefix)) == 0) {
        // mov r14/r15, rcx
        if (p[34] != 0x4C || p[35] != 0x8B ||
            (p[36] != 0xF1 && p[36] != 0xF9) ||
            memcmp(p + 37, "\x83\x65\x30\x00", 4) != 0 ||
            memcmp(p + 41, "\x4C\x8D\xA1", 3) != 0) {
            return false;
        }

        DWORD offset = 0;
        memcpy(&offset, p + 44, sizeof(offset));
        if (!IsExpectedOnLoadedMemberOffset(offset) ||
            memcmp(p + 48, "\x49\x83\x3C\x24\x00", 5) != 0 ||
            p[53] != 0x0F ||
            (p[54] != 0x84 && p[54] != 0x85)) {
            return false;
        }

        *shapeLabel = L"legacy 17763/18362 family";
        *memberOffset = offset;
        return true;
    }

    // Later compiler shape: Windows 10 2004/22H2 and Windows 11 families.
    // 19041 uses a 30h stack frame; 22000+ uses 40h in every harvested sample.
    static constexpr BYTE kModernPrefix[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10,
        0x48, 0x89, 0x74, 0x24, 0x18,
        0x55, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8B, 0xEC
    };

    if (memcmp(p, kModernPrefix, sizeof(kModernPrefix)) != 0 ||
        p[21] != 0x48 || p[22] != 0x83 || p[23] != 0xEC ||
        (p[24] != 0x30 && p[24] != 0x40) ||
        p[25] != 0x4C || p[26] != 0x8B ||
        (p[27] != 0xF1 && p[27] != 0xF9) ||
        memcmp(p + 28, "\x83\x65\x30\x00", 4) != 0 ||
        memcmp(p + 32, "\x4C\x8D\xA1", 3) != 0) {
        return false;
    }

    DWORD offset = 0;
    memcpy(&offset, p + 35, sizeof(offset));
    if (!IsExpectedOnLoadedMemberOffset(offset) ||
        memcmp(p + 39, "\x49\x83\x3C\x24\x00", 5) != 0 ||
        p[44] != 0x0F ||
        (p[45] != 0x84 && p[45] != 0x85)) {
        return false;
    }

    *shapeLabel =
        p[24] == 0x30
            ? L"modern Win10 19041 family"
            : L"modern Win11 22000+ family";
    *memberOffset = offset;
    return true;
}

void* ResolveOnLoadedByStructure(HMODULE module) {
    StobjectPeView view{};
    if (!GetStobjectPeView(module, &view)) {
        ModLog(L"Symbol-free _OnLoaded resolver: invalid Windows.UI.Logon.dll PE image");
        return nullptr;
    }

    const auto* textSection = FindPeSectionByName(view, ".text");
    if (!textSection ||
        (textSection->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
        ModLog(L"Symbol-free _OnLoaded resolver: executable .text section unavailable");
        return nullptr;
    }

    const DWORD textRva = textSection->VirtualAddress;
    const size_t textSpan = PeSectionVirtualSpan(*textSection);
    if (!textSpan ||
        textSpan > view.nt->OptionalHeader.SizeOfImage ||
        !IsPeRvaRangeValid(view, textRva, textSpan)) {
        ModLog(L"Symbol-free _OnLoaded resolver: invalid .text bounds");
        return nullptr;
    }

    const auto& exceptionDir =
        view.nt->OptionalHeader
            .DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!exceptionDir.VirtualAddress ||
        exceptionDir.Size < sizeof(RUNTIME_FUNCTION) ||
        !IsPeRvaRangeValid(
            view, exceptionDir.VirtualAddress, exceptionDir.Size)) {
        ModLog(L"Symbol-free _OnLoaded resolver: x64 runtime-function table unavailable");
        return nullptr;
    }

    auto functions = reinterpret_cast<const RUNTIME_FUNCTION*>(
        view.base + exceptionDir.VirtualAddress);
    const size_t functionCount =
        exceptionDir.Size / sizeof(RUNTIME_FUNCTION);
    const uint64_t textEnd =
        static_cast<uint64_t>(textRva) + textSpan;

    std::vector<DWORD> candidates;
    const wchar_t* uniqueShape = nullptr;
    DWORD uniqueMemberOffset = 0;

    // Enumerating x64 unwind-function starts is both faster and safer than
    // byte-scanning arbitrary instruction interiors. The harvested target is
    // a real runtime-function start on every validated 17763+ sample.
    for (size_t i = 0; i < functionCount; ++i) {
        const DWORD candidateRva = functions[i].BeginAddress;
        const DWORD functionEnd = functions[i].EndAddress;
        if (candidateRva < textRva ||
            static_cast<uint64_t>(candidateRva) + 64 > textEnd ||
            functionEnd <= candidateRva) {
            continue;
        }

        const BYTE* p = view.base + candidateRva;
        if (p[0] != 0x48) {
            continue;
        }

        const wchar_t* shape = nullptr;
        DWORD memberOffset = 0;
        if (!MatchOnLoadedStructuralCandidate(
                view,
                candidateRva,
                functionEnd,
                &shape,
                &memberOffset)) {
            continue;
        }

        PushUniqueRva(&candidates, candidateRva);
        ModLog(L"Symbol-free _OnLoaded candidate: RVA=0x%X shape=%s memberOffset=0x%X",
               candidateRva,
               shape,
               memberOffset);

        if (candidates.size() == 1) {
            uniqueShape = shape;
            uniqueMemberOffset = memberOffset;
        }
    }

    if (candidates.size() != 1) {
        ModLog(L"Symbol-free _OnLoaded resolver expected exactly one validated candidate, found %u; refusing to hook",
               static_cast<unsigned>(candidates.size()));
        return nullptr;
    }

    const DWORD rva = candidates.front();
    ModLog(L"Symbol-free SystemProviderUserControl::_OnLoaded resolver succeeded: RVA=0x%X shape=%s memberOffset=0x%X",
           rva,
           uniqueShape ? uniqueShape : L"<unknown>",
           uniqueMemberOffset);

    return view.base + rva;
}

void* ResolveOnLoadedBySymbols(HMODULE module) {
    ModLog(L"Resolving SystemProviderUserControl::_OnLoaded from Windows.UI.Logon symbols");

    WH_FIND_SYMBOL findData{};
    HANDLE search = Wh_FindFirstSymbol(module, nullptr, &findData);
    if (!search) {
        ModLog(L"Wh_FindFirstSymbol failed; symbols may be unavailable for this Windows.UI.Logon.dll");
        return nullptr;
    }

    struct SearchGuard {
        HANDLE handle;
        ~SearchGuard() {
            if (handle) {
                Wh_FindCloseSymbol(handle);
            }
        }
    } searchGuard{search};

    void* target = nullptr;
    unsigned rawMatchCount = 0;
    std::vector<void*> distinctTargets;

    do {
        if (!IsOnLoadedSymbolName(findData.symbol)) {
            continue;
        }

        ++rawMatchCount;
        ModLog(L"Symbol candidate: %s at %p",
               findData.symbol ? findData.symbol : L"<null>",
               findData.address);

        if (!findData.address) {
            continue;
        }

        if (std::find(distinctTargets.begin(), distinctTargets.end(),
                      findData.address) == distinctTargets.end()) {
            distinctTargets.push_back(findData.address);
        }
    } while (Wh_FindNextSymbol(search, &findData));

    if (distinctTargets.size() != 1) {
        ModLog(L"Expected exactly one distinct _OnLoaded address, got %u raw symbol match(es) across %u distinct address(es); refusing to hook",
               rawMatchCount,
               static_cast<unsigned>(distinctTargets.size()));
        return nullptr;
    }

    target = distinctTargets.front();
    if (rawMatchCount > 1) {
        ModLog(L"Collapsed %u duplicate _OnLoaded symbol entries to one address %p",
               rawMatchCount, target);
    }

    const auto rva = reinterpret_cast<uintptr_t>(target) -
                     reinterpret_cast<uintptr_t>(module);
    ModLog(L"Resolved SystemProviderUserControl::_OnLoaded RVA=0x%llX",
           static_cast<unsigned long long>(rva));
    return target;
}

FrameworkElement FindByName(FrameworkElement root, const wchar_t* name) {
    if (!root) {
        return nullptr;
    }

    try {
        if (root.Name() == name) {
            return root;
        }

        int count = Media::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(root, i)
                             .try_as<FrameworkElement>();
            if (!child) {
                continue;
            }

            auto result = FindByName(child, name);
            if (result) {
                return result;
            }
        }
    } catch (...) {
    }

    return nullptr;
}


Controls::TextBlock FindFirstTextBlock(FrameworkElement root) {
    if (!root) {
        return nullptr;
    }

    try {
        if (auto text = root.try_as<Controls::TextBlock>()) {
            return text;
        }

        int count = Media::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(root, i)
                             .try_as<FrameworkElement>();
            if (!child) {
                continue;
            }

            auto result = FindFirstTextBlock(child);
            if (result) {
                return result;
            }
        }
    } catch (...) {
    }

    return nullptr;
}

void CopyNativeGlyphTypography(Controls::TextBlock source,
                               Controls::TextBlock target) {
    if (!source || !target) {
        return;
    }

    try { target.FontFamily(source.FontFamily()); } catch (...) {}
    try { target.FontSize(source.FontSize()); } catch (...) {}
    try { target.FontWeight(source.FontWeight()); } catch (...) {}
    try { target.FontStyle(source.FontStyle()); } catch (...) {}
    try { target.FontStretch(source.FontStretch()); } catch (...) {}
    try { target.CharacterSpacing(source.CharacterSpacing()); } catch (...) {}
    try { target.HorizontalAlignment(source.HorizontalAlignment()); } catch (...) {}
    try { target.VerticalAlignment(source.VerticalAlignment()); } catch (...) {}
    try { target.TextAlignment(source.TextAlignment()); } catch (...) {}
}


void LogTree(FrameworkElement root, int depth = 0, int* budget = nullptr) {
    int localBudget = 140;
    if (!budget) {
        budget = &localBudget;
    }

    if (!root || *budget <= 0 || depth > 10) {
        return;
    }

    --*budget;

    try {
        auto className = winrt::get_class_name(root);
        auto name = root.Name();
        ModLog(L"TREE d=%d class=%s name=%s",
               depth,
               className.c_str(),
               name.empty() ? L"-" : name.c_str());

        int count = Media::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count && *budget > 0; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(root, i)
                             .try_as<FrameworkElement>();
            if (child) {
                LogTree(child, depth + 1, budget);
            }
        }
    } catch (...) {
    }
}

void ConfigurePercentageTextBlock(
        Controls::TextBlock const& textBlock) {
    if (!textBlock) {
        return;
    }

    const double fontSize = static_cast<double>(
        g_percentageFontSize.load(std::memory_order_relaxed));
    const auto fontFamily = GetPercentageFontFamilySetting();

    try {
        textBlock.FontFamily(Media::FontFamily(fontFamily));
    } catch (...) {
        try {
            textBlock.FontFamily(Media::FontFamily(L"Segoe UI"));
        } catch (...) {
        }
    }

    try {
        textBlock.FontSize(fontSize);
        textBlock.VerticalAlignment(VerticalAlignment::Center);
        textBlock.TextAlignment(TextAlignment::Center);
        textBlock.MinWidth(0.0);
    } catch (...) {
    }
}

void AllowInjectedButtonToDynamicallySizeForPercentage(
        Controls::Button const& button,
        double nativeButtonWidth) {
    if (!button) {
        return;
    }

    try {
        // Override fixed Width from the native Power button style while
        // preserving its template. The content and Auto Grid column now drive
        // the width continuously, similar to variable-width taskbar buttons.
        button.Width(std::numeric_limits<double>::quiet_NaN());
        button.MinWidth(std::max(0.0, nativeButtonWidth));

        ModLog(L"Percentage layout: dynamic width enabled nativeMinWidth=%.1f",
               nativeButtonWidth);
    } catch (...) {
    }
}

void AllowLazyNativeContainerToDynamicallySize(
        FrameworkElement const& element,
        const std::wstring& key) {
    if (!element || (key != L"ime" && key != L"input")) {
        return;
    }

    try {
        // Windows 11 keeps the IME/Input UserControl containers alive even
        // while their inner buttons are lazy/unrealized. Those containers can
        // retain the native 40-DIP Width even when their content currently
        // contributes no visible button. In an Auto Grid column that becomes
        // a blank slot if the user moves Battery/Volume ahead of IME/Input.
        //
        // Make only the *outer container* content-driven. We deliberately do
        // not touch its Visibility, template, or internal buttons here, so
        // Windows remains in full control of when the native control appears.
        // Its left margin is handled separately and dynamically: a permanent
        // margin would itself reserve blank space while the lazy content is
        // dormant.
        element.Width(std::numeric_limits<double>::quiet_NaN());
        element.MinWidth(0.0);
        element.MaxWidth(std::numeric_limits<double>::infinity());

        ModLog(L"Lazy native container sizing normalized: %s (Width=Auto MinWidth=0)",
               key.c_str());
    } catch (...) {
    }
}


bool IsRenderedSystemStripElement(FrameworkElement const& element) {
    if (!element) {
        return false;
    }

    try {
        return element.Visibility() == Visibility::Visible &&
               element.ActualWidth() > 0.0 &&
               element.ActualHeight() > 0.0;
    } catch (...) {
        return false;
    }
}

bool IsLazyImeOrInputContainer(FrameworkElement const& element) {
    if (!element) {
        return false;
    }

    try {
        const auto className = winrt::get_class_name(element);
        return className == L"LogonUX.ImeModeIndicatorUserControl" ||
               className == L"LogonUX.InputSwitchUserControl";
    } catch (...) {
        return false;
    }
}

void ApplyImeModeGlyphVisualOffsetRecursive(
        FrameworkElement const& root,
        double offsetX,
        int depth = 0) {
    if (!root || depth > 10) {
        return;
    }

    try {
        if (auto text = root.try_as<Controls::TextBlock>()) {
            // Shift only the rendered glyph/content, never the Button or its
            // 40-DIP native hit target. Reuse a TranslateTransform if one was
            // already installed by us; don't overwrite an unrelated native
            // transform if Windows ever adds one.
            auto current = text.RenderTransform();
            auto translate = current.try_as<Media::TranslateTransform>();
            if (!current) {
                translate = Media::TranslateTransform();
                text.RenderTransform(translate);
            } else if (!translate) {
                return;
            }

            if (std::fabs(translate.X() - offsetX) >= 0.01) {
                translate.X(offsetX);
            }
            return;
        }

        const int count = Media::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(root, i)
                             .try_as<FrameworkElement>();
            if (child) {
                ApplyImeModeGlyphVisualOffsetRecursive(
                    child, offsetX, depth + 1);
            }
        }
    } catch (...) {
    }
}

void UpdateImeModeGlyphVisualOffset(FrameworkElement const& element) {
    if (!element) {
        return;
    }

    try {
        if (winrt::get_class_name(element) !=
            L"LogonUX.ImeModeIndicatorUserControl") {
            return;
        }

        ApplyImeModeGlyphVisualOffsetRecursive(
            element,
            static_cast<double>(
                g_imeModeVisualOffsetX.load(std::memory_order_relaxed)));
    } catch (...) {
    }
}

void UpdateLazyNativeContainerSpacing(
        Controls::Grid const& parentGrid,
        int firstSystemColumn,
        int usedEndColumn,
        double nativeLeftSpacing) {
    if (!parentGrid || usedEndColumn <= firstSystemColumn) {
        return;
    }

    try {
        auto children = parentGrid.Children();
        std::vector<FrameworkElement> directElements;
        directElements.reserve(children.Size());

        for (uint32_t i = 0; i < children.Size(); ++i) {
            auto element = children.GetAt(i).try_as<FrameworkElement>();
            if (!element) {
                continue;
            }

            const int col = Controls::Grid::GetColumn(element);
            if (col < firstSystemColumn || col >= usedEndColumn) {
                continue;
            }

            directElements.push_back(element);
        }

        for (const auto& element : directElements) {
            if (!IsLazyImeOrInputContainer(element)) {
                continue;
            }

            const int col = Controls::Grid::GetColumn(element);
            const bool rendered = IsRenderedSystemStripElement(element);
            FrameworkElement nearestRenderedPredecessor{nullptr};
            int nearestPredecessorColumn = INT_MIN;

            if (rendered) {
                for (const auto& other : directElements) {
                    if (!other ||
                        winrt::get_abi(other) == winrt::get_abi(element)) {
                        continue;
                    }

                    const int otherCol = Controls::Grid::GetColumn(other);
                    if (otherCol >= col || otherCol <= nearestPredecessorColumn) {
                        continue;
                    }

                    if (IsRenderedSystemStripElement(other)) {
                        nearestRenderedPredecessor = other;
                        nearestPredecessorColumn = otherCol;
                    }
                }
            }

            const bool hasRenderedPredecessor =
                static_cast<bool>(nearestRenderedPredecessor);
            const bool predecessorIsLazyPair =
                hasRenderedPredecessor &&
                IsLazyImeOrInputContainer(nearestRenderedPredecessor);

            // Also look right so an IME container that is immediately followed
            // by the rendered Input container can act as the leading half of a
            // single native pair. In that common Japanese state the pair gets
            // no extra leading margin; otherwise the narrow IME glyph appears
            // much farther from the preceding control than ENG does.
            FrameworkElement nearestRenderedSuccessor{nullptr};
            int nearestSuccessorColumn = INT_MAX;
            if (rendered) {
                for (const auto& other : directElements) {
                    if (!other ||
                        winrt::get_abi(other) == winrt::get_abi(element)) {
                        continue;
                    }

                    const int otherCol = Controls::Grid::GetColumn(other);
                    if (otherCol <= col || otherCol >= nearestSuccessorColumn) {
                        continue;
                    }

                    if (IsRenderedSystemStripElement(other)) {
                        nearestRenderedSuccessor = other;
                        nearestSuccessorColumn = otherCol;
                    }
                }
            }

            const bool successorIsLazyPair =
                nearestRenderedSuccessor &&
                IsLazyImeOrInputContainer(nearestRenderedSuccessor);
            const bool isImeContainer =
                winrt::get_class_name(element) ==
                L"LogonUX.ImeModeIndicatorUserControl";
            const bool leadingAdjacentImeInputPair =
                isImeContainer && successorIsLazyPair;

            // IME mode + Input switch are a native Windows pair. The leading
            // IME half gets no injected external margin when the Input half is
            // also rendered, and Input itself gets no second gap after IME.
            // A lone ENG/Input button still receives the configurable boundary
            // spacing that fixed the earlier no-gap regression.
            const double desiredLeft =
                rendered && hasRenderedPredecessor &&
                        !predecessorIsLazyPair &&
                        !leadingAdjacentImeInputPair
                    ? std::max(0.0, nativeLeftSpacing)
                    : 0.0;

            UpdateImeModeGlyphVisualOffset(element);

            auto current = element.Margin();
            if (std::fabs(current.Left - desiredLeft) < 0.01) {
                continue;
            }

            element.Margin(
                Thickness{
                    desiredLeft,
                    current.Top,
                    current.Right,
                    current.Bottom});

            ModLog(
                L"Lazy native spacing updated: class=%s col=%d rendered=%d predecessor=%d predecessorLazyPair=%d successorLazyPair=%d leadingImePair=%d left=%.1f",
                winrt::get_class_name(element).c_str(),
                col,
                rendered ? 1 : 0,
                hasRenderedPredecessor ? 1 : 0,
                predecessorIsLazyPair ? 1 : 0,
                successorIsLazyPair ? 1 : 0,
                leadingAdjacentImeInputPair ? 1 : 0,
                desiredLeft);
        }
    } catch (...) {
    }
}


std::wstring FormatPercentageText(
        int percent,
        bool volume,
        bool muted) {
    if (percent < 0 || percent > 100) {
        return {};
    }

    if ((!volume &&
         g_hideBatteryPercentageAt100.load(std::memory_order_relaxed) &&
         percent == 100) ||
        (volume &&
         g_hideVolumePercentageAt100.load(std::memory_order_relaxed) &&
         percent == 100)) {
        return {};
    }

    std::wstring value = std::to_wstring(percent);
    if (g_percentageShowPercentSign.load(std::memory_order_relaxed)) {
        value += L"%";
    }

    if (volume && muted &&
        g_showMutedTextInVolumePercentage.load(
            std::memory_order_relaxed)) {
        value += L" (muted)";
    }

    return value;
}

void SetPercentageLabelText(
        Controls::TextBlock const& label,
        const std::wstring& value,
        bool featureEnabled) {
    if (!label) {
        return;
    }

    try {
        if (!featureEnabled || value.empty()) {
            label.Text(L"");
            label.Visibility(Visibility::Collapsed);
        } else {
            label.Text(hstring(value));
            label.Visibility(Visibility::Visible);
        }
    } catch (...) {
    }
}


void CopySafeVisualProperties(Controls::Control source,
                              Controls::Button target) {
    if (!source || !target) {
        return;
    }

    try {
        if (auto style = source.Style()) {
            target.Style(style);
        }
    } catch (...) {
    }

    try {
        target.Margin(source.Margin());
    } catch (...) {
    }

    try {
        target.MinWidth(source.MinWidth());
        target.MinHeight(source.MinHeight());
    } catch (...) {
    }

    try {
        auto foreground = source.Foreground();
        if (foreground) {
            target.Foreground(foreground);
        }
    } catch (...) {
    }
}



struct SystemButtonGroup {
    std::wstring key;
    int originalColumn = -1;
    std::vector<FrameworkElement> elements;
};


struct ColumnSizingSnapshot {
    GridLength width{};
    double minWidth = 0.0;
    double maxWidth = std::numeric_limits<double>::infinity();
};

struct NativeElementSnapshot {
    winrt::weak_ref<FrameworkElement> element;
    std::wstring groupKey;
    int column = 0;
    Visibility visibility = Visibility::Visible;
    Thickness margin{};
    double width = std::numeric_limits<double>::quiet_NaN();
    double minWidth = 0.0;
    double maxWidth = std::numeric_limits<double>::infinity();
    bool isControl = false;
    bool isTabStop = false;
    int tabIndex = 0;
    bool restoreColumn = false;
    bool restoreVisibility = false;
    bool restoreMargin = false;
    bool restoreSizing = false;
    bool restoreTabState = false;
    bool restoreImeVisualOffset = false;
};

struct InjectionSession {
    winrt::weak_ref<Controls::UserControl> owner;
    winrt::weak_ref<Controls::Grid> grid;
    DWORD xamlThreadId = 0;
    void* ownerIdentity = nullptr;
    uint32_t originalColumnCount = 0;
    std::vector<ColumnSizingSnapshot> originalColumns;
    std::vector<NativeElementSnapshot> nativeElements;

    Controls::Button batteryButton{nullptr};
    Controls::Button volumeButton{nullptr};
    event_token batteryClickToken{};
    event_token volumeClickToken{};
    bool batteryClickAttached = false;
    bool volumeClickAttached = false;
    std::shared_ptr<BatteryWheelRegistration> batteryWheel;
    std::shared_ptr<VolumeMonitorState> volumeState;

    event_token spacingLayoutToken{};
    bool spacingLayoutAttached = false;
    event_token diagnosticsLayoutToken{};
    bool diagnosticsLayoutAttached = false;
    event_token ownerUnloadedToken{};
    bool ownerUnloadedAttached = false;
    event_token ownerLoadedToken{};
    bool ownerLoadedAttached = false;

    std::atomic<bool> active{true};
    std::atomic<bool> cleaning{false};
};

SRWLOCK g_injectionSessionsLock = SRWLOCK_INIT;
std::vector<std::shared_ptr<InjectionSession>> g_injectionSessions;

void TrackInjectionSession(
        std::shared_ptr<InjectionSession> const& session) {
    if (!session) return;
    AcquireSRWLockExclusive(&g_injectionSessionsLock);
    g_injectionSessions.push_back(session);
    ReleaseSRWLockExclusive(&g_injectionSessionsLock);
}

void UntrackInjectionSession(
        std::shared_ptr<InjectionSession> const& session) {
    if (!session) return;
    AcquireSRWLockExclusive(&g_injectionSessionsLock);
    auto it = std::remove_if(
        g_injectionSessions.begin(),
        g_injectionSessions.end(),
        [&](const std::shared_ptr<InjectionSession>& item) {
            return item.get() == session.get();
        });
    g_injectionSessions.erase(it, g_injectionSessions.end());
    ReleaseSRWLockExclusive(&g_injectionSessionsLock);
}

std::shared_ptr<InjectionSession> FindInjectionSessionForOwner(
        void* ownerIdentity) {
    if (!ownerIdentity) return nullptr;
    std::shared_ptr<InjectionSession> result;
    AcquireSRWLockShared(&g_injectionSessionsLock);
    for (const auto& session : g_injectionSessions) {
        if (session && session->ownerIdentity == ownerIdentity &&
            !session->cleaning.load(std::memory_order_acquire)) {
            result = session;
            break;
        }
    }
    ReleaseSRWLockShared(&g_injectionSessionsLock);
    return result;
}

std::vector<DWORD> SnapshotInjectionSessionThreads() {
    std::vector<DWORD> result;
    AcquireSRWLockShared(&g_injectionSessionsLock);
    for (const auto& session : g_injectionSessions) {
        if (!session || !session->xamlThreadId) continue;
        if (std::find(result.begin(), result.end(), session->xamlThreadId) ==
            result.end()) {
            result.push_back(session->xamlThreadId);
        }
    }
    ReleaseSRWLockShared(&g_injectionSessionsLock);
    return result;
}

void CleanupInjectionSession(
    std::shared_ptr<InjectionSession> const& session,
    bool restoreLayout,
    bool fromOwnerUnload);

void AttachDynamicLazyNativeSpacing(
        Controls::UserControl const& userControl,
        Controls::Grid const& parentGrid,
        int firstSystemColumn,
        int usedEndColumn,
        double nativeLeftSpacing,
        std::shared_ptr<InjectionSession> const& session) {
    if (!userControl || !parentGrid || !session ||
        usedEndColumn <= firstSystemColumn) {
        return;
    }

    try {
        auto weakGrid = winrt::make_weak(parentGrid);

        UpdateLazyNativeContainerSpacing(
            parentGrid,
            firstSystemColumn,
            usedEndColumn,
            nativeLeftSpacing);

        session->spacingLayoutToken = userControl.LayoutUpdated(
            [weakGrid,
             firstSystemColumn,
             usedEndColumn,
             nativeLeftSpacing](auto const&, auto const&) {
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                auto grid = weakGrid.get();
                if (!grid) {
                    return;
                }

                UpdateLazyNativeContainerSpacing(
                    grid,
                    firstSystemColumn,
                    usedEndColumn,
                    nativeLeftSpacing);
            });
        session->spacingLayoutAttached = true;

        ModLog(
            L"Dynamic IME/Input spacing watcher attached: cols=%d..%d nativeLeftSpacing=%.1f",
            firstSystemColumn,
            usedEndColumn - 1,
            nativeLeftSpacing);
    } catch (...) {
        ModLog(L"Failed to attach dynamic IME/Input spacing watcher");
    }
}


std::vector<Controls::Button> DirectButtonsAtGridColumn(
    Controls::Grid const& grid,
    int targetRow,
    int targetColumn) {
    std::vector<Controls::Button> result;
    if (!grid) return result;

    try {
        auto children = grid.Children();
        for (uint32_t i = 0; i < children.Size(); ++i) {
            auto button = children.GetAt(i).try_as<Controls::Button>();
            if (!button) continue;
            if (Controls::Grid::GetRow(button) != targetRow) continue;
            if (Controls::Grid::GetColumn(button) != targetColumn) continue;
            result.push_back(button);
        }
    } catch (...) {
    }
    return result;
}

bool ContainsElementIdentity(const std::vector<FrameworkElement>& elements,
                             FrameworkElement const& needle) {
    const void* needleAbi = needle ? winrt::get_abi(needle) : nullptr;
    for (const auto& element : elements) {
        if (element && winrt::get_abi(element) == needleAbi) return true;
    }
    return false;
}

void CollectSystemButtonDiagnosticHints(
        FrameworkElement const& element,
        std::vector<std::wstring>& hints,
        int depth = 0) {
    if (!element || depth > 6 || hints.size() >= 32) {
        return;
    }

    try {
        const auto name = element.Name();
        if (!name.empty()) {
            hints.emplace_back(name.c_str());
        }
    } catch (...) {
    }

    try {
        const auto automationName =
            Automation::AutomationProperties::GetName(element);
        if (!automationName.empty()) {
            hints.emplace_back(automationName.c_str());
        }
    } catch (...) {
    }

    try {
        if (auto textBlock = element.try_as<Controls::TextBlock>()) {
            const auto value = textBlock.Text();
            if (!value.empty()) {
                hints.emplace_back(value.c_str());
            }
        }
    } catch (...) {
    }

    try {
        if (auto contentControl =
                element.try_as<Controls::ContentControl>()) {
            auto content = contentControl.Content();
            if (content) {
                try {
                    const auto value =
                        winrt::unbox_value<hstring>(content);
                    if (!value.empty()) {
                        hints.emplace_back(value.c_str());
                    }
                } catch (...) {
                }
            }
        }
    } catch (...) {
    }

    try {
        const int childCount =
            Media::VisualTreeHelper::GetChildrenCount(element);
        for (int i = 0; i < childCount && hints.size() < 32; ++i) {
            auto child =
                Media::VisualTreeHelper::GetChild(element, i)
                    .try_as<FrameworkElement>();
            if (child) {
                CollectSystemButtonDiagnosticHints(
                    child, hints, depth + 1);
            }
        }
    } catch (...) {
    }
}

bool IsSystemButtonGroupCurrentlyVisible(
        const SystemButtonGroup& group) {
    for (const auto& element : group.elements) {
        if (!element) {
            continue;
        }

        try {
            if (element.Visibility() == Visibility::Visible &&
                element.ActualWidth() > 0.0 &&
                element.ActualHeight() > 0.0) {
                return true;
            }
        } catch (...) {
        }

        // A Windows-owned UserControl can itself have no rendered size while
        // one of its lazy-loaded child Buttons is transitioning. Check the
        // subtree as a diagnostic aid without using this for role detection.
        try {
            const int childCount =
                Media::VisualTreeHelper::GetChildrenCount(element);
            for (int i = 0; i < childCount; ++i) {
                auto child = Media::VisualTreeHelper::GetChild(element, i)
                                 .try_as<FrameworkElement>();
                if (child && child.Visibility() == Visibility::Visible &&
                    child.ActualWidth() > 0.0 && child.ActualHeight() > 0.0) {
                    return true;
                }
            }
        } catch (...) {
        }
    }
    return false;
}

void LogNativeSystemButtonSemanticDiagnostics(
        const std::vector<SystemButtonGroup>& groups) {
    if (!(g_enableLogging.load(std::memory_order_relaxed) ||
          g_enableFileLogging.load(std::memory_order_relaxed)) ||
        !g_enableVerboseDiagnostics.load(std::memory_order_relaxed)) {
        return;
    }

    for (const auto& group : groups) {
        std::vector<std::wstring> hints;
        for (const auto& element : group.elements) {
            CollectSystemButtonDiagnosticHints(element, hints);
        }

        ModLog(L"Native-slot diagnostic: key=%s originalCol=%d visible=%d hints=%u",
               group.key.c_str(),
               group.originalColumn,
               IsSystemButtonGroupCurrentlyVisible(group) ? 1 : 0,
               static_cast<unsigned>(hints.size()));

        for (size_t i = 0; i < hints.size(); ++i) {
            ModLog(L"Native-slot hint: key=%s originalCol=%d hint[%u]=%s",
                   group.key.c_str(),
                   group.originalColumn,
                   static_cast<unsigned>(i),
                   hints[i].c_str());
        }
    }
}

bool IsSystemButtonGroupEnabled(const std::wstring& key) {
    if (key == L"ime") {
        return g_showImeModeButton.load(std::memory_order_relaxed);
    }
    if (key == L"input") {
        return g_showInputButton.load(std::memory_order_relaxed);
    }
    if (key == L"network-signin") {
        return g_showNetworkSignInButton.load(std::memory_order_relaxed);
    }
    if (key == L"disconnect") {
        return g_showDisconnectButton.load(std::memory_order_relaxed);
    }
    if (key == L"network") {
        return g_showNetworkButton.load(std::memory_order_relaxed);
    }
    if (key == L"accessibility") {
        return g_showAccessibilityButton.load(std::memory_order_relaxed);
    }
    if (key == L"power") {
        return g_showPowerButton.load(std::memory_order_relaxed);
    }
    if (key.rfind(L"native", 0) == 0) {
        return g_showOtherNativeButtons.load(std::memory_order_relaxed);
    }
    return true;
}

std::vector<SystemButtonGroup> DiscoverNativeSystemButtonGroups(
    Controls::Grid const& grid,
    Controls::Button const& powerButton,
    int row,
    int powerColumn) {
    std::vector<SystemButtonGroup> directButtonGroups;

    // Direct Windows-owned Buttons form a contiguous strip ending at Power.
    // On the tested layouts the last three are Network, Accessibility, Power;
    // two earlier auxiliary slots are Network sign-in and Disconnect.
    for (int col = powerColumn; col >= 0; --col) {
        auto buttons = DirectButtonsAtGridColumn(grid, row, col);
        if (buttons.empty()) {
            if (col == powerColumn) return {};
            break;
        }

        SystemButtonGroup group;
        group.originalColumn = col;
        for (const auto& button : buttons) {
            group.elements.push_back(button);
        }
        directButtonGroups.push_back(std::move(group));
    }

    std::reverse(directButtonGroups.begin(), directButtonGroups.end());
    if (directButtonGroups.empty() ||
        !ContainsElementIdentity(
            directButtonGroups.back().elements, powerButton)) {
        return {};
    }

    directButtonGroups.back().key = L"power";
    if (directButtonGroups.size() >= 2) {
        directButtonGroups[directButtonGroups.size() - 2].key = L"accessibility";
    }
    if (directButtonGroups.size() >= 3) {
        directButtonGroups[directButtonGroups.size() - 3].key = L"network";
    }

    const size_t auxiliaryCount =
        directButtonGroups.size() > 3 ? directButtonGroups.size() - 3 : 0;
    if (auxiliaryCount == 2) {
        directButtonGroups[0].key = L"network-signin";
        directButtonGroups[1].key = L"disconnect";
    } else {
        size_t nativeIndex = 1;
        for (size_t i = 0; i < auxiliaryCount; ++i) {
            directButtonGroups[i].key =
                L"native" + std::to_wstring(nativeIndex++);
        }
    }

    std::vector<SystemButtonGroup> groups = directButtonGroups;

    // Windows 11 keeps both of these UserControls in the SystemProvider tree
    // even before their inner Buttons become visible. Identify the containers
    // by runtime XAML class so ordering/visibility works before lazy loading.
    try {
        auto children = grid.Children();
        for (uint32_t i = 0; i < children.Size(); ++i) {
            auto element = children.GetAt(i).try_as<FrameworkElement>();
            if (!element || element.try_as<Controls::Button>()) {
                continue;
            }
            if (Controls::Grid::GetRow(element) != row) {
                continue;
            }

            std::wstring key;
            try {
                const auto className = winrt::get_class_name(element);
                if (className == L"LogonUX.ImeModeIndicatorUserControl") {
                    key = L"ime";
                } else if (className == L"LogonUX.InputSwitchUserControl") {
                    key = L"input";
                }
            } catch (...) {
            }

            if (key.empty()) {
                continue;
            }

            SystemButtonGroup group;
            group.key = std::move(key);
            group.originalColumn = Controls::Grid::GetColumn(element);
            group.elements.push_back(element);
            groups.push_back(std::move(group));
        }
    } catch (...) {
    }

    std::stable_sort(
        groups.begin(), groups.end(),
        [](const SystemButtonGroup& a, const SystemButtonGroup& b) {
            if (a.originalColumn != b.originalColumn) {
                return a.originalColumn < b.originalColumn;
            }
            // If two native containers share a column, keep IME before Input.
            if (a.key == L"ime" && b.key == L"input") return true;
            if (a.key == L"input" && b.key == L"ime") return false;
            return a.key < b.key;
        });

    return groups;
}

void LogNativeSystemButtonGroups(
    const std::vector<SystemButtonGroup>& groups) {
    for (const auto& group : groups) {
        ModLog(L"Button-order native slot: %s originalCol=%d elementCount=%u",
               group.key.c_str(),
               group.originalColumn,
               static_cast<unsigned>(group.elements.size()));
    }
}

bool WriteBgraBmp(const wchar_t* path,
                  const uint8_t* pixels,
                  uint32_t width,
                  uint32_t height,
                  uint32_t byteCount) {
    if (!pixels || width == 0 || height == 0) {
        ModLog(L"XAML capture: invalid pixel buffer/size");
        return false;
    }

    const uint64_t expected64 =
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
    if (expected64 > UINT32_MAX || byteCount < static_cast<uint32_t>(expected64)) {
        ModLog(L"XAML capture: unexpected pixel buffer size=%u expected=%llu",
               byteCount, expected64);
        return false;
    }

    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42; // 'BM'
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize =
        fileHeader.bfOffBits + static_cast<DWORD>(expected64);

    BITMAPINFOHEADER info{};
    info.biSize = sizeof(BITMAPINFOHEADER);
    info.biWidth = static_cast<LONG>(width);
    info.biHeight = -static_cast<LONG>(height); // RenderTargetBitmap is top-down BGRA.
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = static_cast<DWORD>(expected64);

    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        ModLog(L"XAML capture: CreateFile failed for %s: %lu",
               path, GetLastError());
        return false;
    }

    DWORD written = 0;
    const BOOL w1 =
        WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr);
    const BOOL w2 =
        WriteFile(file, &info, sizeof(info), &written, nullptr);
    const BOOL w3 =
        WriteFile(file, pixels, static_cast<DWORD>(expected64), &written, nullptr);
    const bool ok = w1 && w2 && w3;
    if (!ok) {
        ModLog(L"XAML capture: WriteFile failed: %lu", GetLastError());
    }

    CloseHandle(file);
    return ok;
}

std::wstring BuildXamlCapturePath() {
    std::wstring directory;
    AcquireSRWLockShared(&g_settingsLock);
    directory = g_xamlCaptureDirectory;
    ReleaseSRWLockShared(&g_settingsLock);

    if (directory.empty()) {
        directory = ExpandEnvironmentPathOrDefault(
            nullptr, L"%PUBLIC%\\Desktop");
    }

    while (!directory.empty() &&
           (directory.back() == L'\\' || directory.back() == L'/')) {
        directory.pop_back();
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t fileName[128]{};
    swprintf_s(
        fileName, ARRAYSIZE(fileName),
        L"LogonUI-SystemProvider-%04u%02u%02u-%02u%02u%02u-%03u.bmp",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    if (directory.empty()) {
        return fileName;
    }

    return directory + L"\\" + fileName;
}

[[maybe_unused]] winrt::fire_and_forget CaptureXamlSubtreeAsync(FrameworkElement element) {
    struct CaptureInFlightGuard {
        CaptureInFlightGuard() {
            g_captureAsyncInFlight.fetch_add(1, std::memory_order_acq_rel);
        }
        ~CaptureInFlightGuard() {
            g_captureAsyncInFlight.fetch_sub(1, std::memory_order_acq_rel);
        }
    } inFlightGuard;

    // Keep the projected XAML element alive across the asynchronous render.
    auto strongElement = element;

    try {
        Media::Imaging::RenderTargetBitmap bitmap;
        co_await bitmap.RenderAsync(strongElement);

        const int pixelWidth = bitmap.PixelWidth();
        const int pixelHeight = bitmap.PixelHeight();
        if (pixelWidth <= 0 || pixelHeight <= 0) {
            ModLog(L"XAML capture: RenderTargetBitmap returned invalid size %dx%d",
                   pixelWidth, pixelHeight);
            g_captureScheduled.store(false, std::memory_order_relaxed);
            co_return;
        }

        auto buffer = co_await bitmap.GetPixelsAsync();
        const uint32_t length = buffer.Length();
        std::vector<uint8_t> pixels(length);

        auto reader =
            winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
        reader.ReadBytes(pixels);

        const std::wstring path = BuildXamlCapturePath();

        if (WriteBgraBmp(path.c_str(),
                         pixels.data(),
                         static_cast<uint32_t>(pixelWidth),
                         static_cast<uint32_t>(pixelHeight),
                         length)) {
            ModLog(L"XAML SystemProvider capture saved: %s (%dx%d)",
                   path.c_str(), pixelWidth, pixelHeight);
        } else {
            ModLog(L"XAML SystemProvider capture failed while saving");
        }
    } catch (const winrt::hresult_error& e) {
        ModLog(L"XAML SystemProvider capture failed: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"XAML SystemProvider capture failed with unknown exception");
    }

    // A successful capture used to leave this latched forever, making the
    // diagnostics effectively one-shot. Clear it after every completed render
    // so a later native-layout change can request a second capture.
    g_captureScheduled.store(false, std::memory_order_relaxed);
}

struct XamlCaptureWaitState {
    DWORD xamlThreadId = 0;
    Controls::UserControl control{nullptr};
    Controls::Image batteryImage{nullptr};
    DispatcherTimer timer{nullptr};
    event_token tickToken{};
    int attempts = 0;
    int stableTicks = 0;
    double lastWidth = -1.0;
    double lastHeight = -1.0;
};

SRWLOCK g_xamlCaptureWaitLock = SRWLOCK_INIT;
std::shared_ptr<XamlCaptureWaitState> g_xamlCaptureWaitState;

[[maybe_unused]] void StopXamlCaptureWait(std::shared_ptr<XamlCaptureWaitState> const& state) {
    if (!state) {
        return;
    }

    if (state->timer) {
        try {
            state->timer.Tick(state->tickToken);
        } catch (...) {
        }
        try {
            state->timer.Stop();
        } catch (...) {
        }
        state->timer = nullptr;
    }

    state->control = nullptr;
    state->batteryImage = nullptr;

    AcquireSRWLockExclusive(&g_xamlCaptureWaitLock);
    if (g_xamlCaptureWaitState.get() == state.get()) {
        g_xamlCaptureWaitState.reset();
    }
    ReleaseSRWLockExclusive(&g_xamlCaptureWaitLock);
}

DWORD GetOutstandingXamlCaptureThreadId() {
    DWORD threadId = 0;
    AcquireSRWLockShared(&g_xamlCaptureWaitLock);
    if (g_xamlCaptureWaitState) {
        threadId = g_xamlCaptureWaitState->xamlThreadId;
    }
    ReleaseSRWLockShared(&g_xamlCaptureWaitLock);
    return threadId;
}

void StopOutstandingXamlCaptureWaitOnUiThread() {
    std::shared_ptr<XamlCaptureWaitState> state;
    AcquireSRWLockShared(&g_xamlCaptureWaitLock);
    state = g_xamlCaptureWaitState;
    ReleaseSRWLockShared(&g_xamlCaptureWaitLock);
    if (state && state->xamlThreadId &&
        state->xamlThreadId != GetCurrentThreadId()) {
        return;
    }
    StopXamlCaptureWait(state);
    g_captureScheduled.store(false, std::memory_order_release);
}

void ScheduleXamlSubtreeCapture(Controls::UserControl const& userControl,
                                Controls::Image const& batteryImage) {
    bool expected = false;
    if (!g_captureScheduled.compare_exchange_strong(expected, true)) {
        return;
    }

    try {
        auto state = std::make_shared<XamlCaptureWaitState>();
        state->xamlThreadId = GetCurrentThreadId();
        AcquireSRWLockExclusive(&g_xamlCaptureWaitLock);
        g_xamlCaptureWaitState = state;
        ReleaseSRWLockExclusive(&g_xamlCaptureWaitLock);
        state->control = userControl;
        state->batteryImage = batteryImage;
        state->timer = DispatcherTimer();
        state->timer.Interval(std::chrono::milliseconds(200));

        state->tickToken = state->timer.Tick(
            [state](auto const&, auto const&) {
                try {
                    ++state->attempts;

                    if (!state->control || !state->batteryImage) {
                        ModLog(L"XAML capture: target disappeared before capture");
                        StopXamlCaptureWait(state);
                        g_captureScheduled = false;
                        return;
                    }

                    auto element =
                        state->control.Content().try_as<FrameworkElement>();
                    if (!element) {
                        ModLog(L"XAML capture: SystemProvider root disappeared");
                        StopXamlCaptureWait(state);
                        g_captureScheduled = false;
                        return;
                    }

                    const double width = element.ActualWidth();
                    const double height = element.ActualHeight();

                    if (std::abs(width - state->lastWidth) < 0.25 &&
                        std::abs(height - state->lastHeight) < 0.25) {
                        ++state->stableTicks;
                    } else {
                        state->stableTicks = 0;
                        state->lastWidth = width;
                        state->lastHeight = height;
                    }

                    int batteryPixelWidth = 0;
                    int batteryPixelHeight = 0;
                    bool batteryDecoded = false;

                    try {
                        auto source = state->batteryImage.Source();
                        auto bitmap =
                            source.try_as<Media::Imaging::BitmapImage>();
                        if (bitmap) {
                            batteryPixelWidth = bitmap.PixelWidth();
                            batteryPixelHeight = bitmap.PixelHeight();
                            batteryDecoded =
                                batteryPixelWidth > 0 && batteryPixelHeight > 0;
                        }
                    } catch (...) {
                    }

                    const bool rootRenderable =
                        width > 0.0 && height > 0.0;
                    const bool layoutExpanded =
                        width >= 120.0; // 96 DIP is the observed Win+L transitional width.
                    const bool batteryLaidOut =
                        state->batteryImage.ActualWidth() > 0.0 &&
                        state->batteryImage.ActualHeight() > 0.0;

                    if (rootRenderable &&
                        layoutExpanded &&
                        batteryDecoded &&
                        batteryLaidOut &&
                        state->stableTicks >= 2) {
                        ModLog(
                            L"XAML capture: stable SystemProvider %.1fx%.1f DIP, "
                            L"battery bitmap=%dx%d, image=%.1fx%.1f DIP; rendering",
                            width,
                            height,
                            batteryPixelWidth,
                            batteryPixelHeight,
                            state->batteryImage.ActualWidth(),
                            state->batteryImage.ActualHeight());

                        auto captureElement = element;
                        StopXamlCaptureWait(state);
                        CaptureXamlSubtreeAsync(captureElement);
                        return;
                    }

                    if (state->attempts == 1 ||
                        state->attempts % 5 == 0) {
                        ModLog(
                            L"XAML capture waiting: root=%.1fx%.1f DIP stable=%d "
                            L"battery=%dx%d image=%.1fx%.1f DIP attempt=%d",
                            width,
                            height,
                            state->stableTicks,
                            batteryPixelWidth,
                            batteryPixelHeight,
                            state->batteryImage.ActualWidth(),
                            state->batteryImage.ActualHeight(),
                            state->attempts);
                    }

                    if (state->attempts >= 25) {
                        ModLog(
                            L"XAML capture abandoned after 5 seconds; "
                            L"layout/image never reached a stable renderable state");
                        StopXamlCaptureWait(state);
                        g_captureScheduled = false;
                    }
                } catch (const winrt::hresult_error& e) {
                    ModLog(L"XAML capture wait failed: 0x%08X %s",
                           static_cast<unsigned>(e.code().value),
                           e.message().c_str());
                    StopXamlCaptureWait(state);
                    g_captureScheduled = false;
                } catch (...) {
                    ModLog(L"XAML capture wait failed with unknown exception");
                    StopXamlCaptureWait(state);
                    g_captureScheduled = false;
                }
            });

        state->timer.Start();
        ModLog(
            L"XAML SystemProvider capture armed; waiting for stable layout "
            L"and decoded BatteryStatusIcon");
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Couldn't arm stable XAML SystemProvider capture: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
        g_captureScheduled = false;
    } catch (...) {
        ModLog(
            L"Couldn't arm stable XAML SystemProvider capture with unknown exception");
        g_captureScheduled = false;
    }
}

void CollectNativeButtonsForDynamicDiagnostics(
        FrameworkElement const& element,
        std::vector<Controls::Button>& buttons,
        int depth = 0) {
    if (!element || depth > 12 || buttons.size() >= 64) {
        return;
    }

    try {
        if (auto button = element.try_as<Controls::Button>()) {
            bool injected = false;
            try {
                const auto name = button.Name();
                injected =
                    (!name.empty() &&
                     (_wcsicmp(name.c_str(), kInjectedBatteryName) == 0 ||
                      _wcsicmp(name.c_str(), kInjectedVolumeName) == 0));
            } catch (...) {
            }

            if (!injected) {
                buttons.push_back(button);
            }
        }

        const int childCount =
            Media::VisualTreeHelper::GetChildrenCount(element);
        for (int i = 0;
             i < childCount && buttons.size() < 64;
             ++i) {
            auto child =
                Media::VisualTreeHelper::GetChild(element, i)
                    .try_as<FrameworkElement>();
            if (child) {
                CollectNativeButtonsForDynamicDiagnostics(
                    child, buttons, depth + 1);
            }
        }
    } catch (...) {
    }
}

std::wstring BuildNativeButtonDynamicSignature(
        FrameworkElement const& root) {
    std::vector<Controls::Button> buttons;
    CollectNativeButtonsForDynamicDiagnostics(root, buttons);

    std::wstring signature;
    signature.reserve(512);
    signature += std::to_wstring(buttons.size());

    for (const auto& button : buttons) {
        if (!button) {
            continue;
        }

        signature += L"|";
        signature += std::to_wstring(
            reinterpret_cast<unsigned long long>(winrt::get_abi(button)));

        try {
            const auto name = button.Name();
            signature += L":n=";
            signature.append(name.c_str(), name.size());
        } catch (...) {
        }

        try {
            const auto automationName =
                Automation::AutomationProperties::GetName(button);
            signature += L":a=";
            signature.append(
                automationName.c_str(), automationName.size());
        } catch (...) {
        }

        try {
            const bool visible =
                button.Visibility() == Visibility::Visible;
            const bool rendered =
                visible && button.ActualWidth() > 0.0 &&
                button.ActualHeight() > 0.0;
            signature += L":v=" +
                std::to_wstring(visible ? 1 : 0);
            signature += L":r=" +
                std::to_wstring(rendered ? 1 : 0);
            signature += L":row=" +
                std::to_wstring(Controls::Grid::GetRow(button));
            signature += L":col=" +
                std::to_wstring(Controls::Grid::GetColumn(button));
        } catch (...) {
        }
    }

    return signature;
}

void LogDynamicNativeButtonDiagnostics(
        FrameworkElement const& root) {
    std::vector<Controls::Button> buttons;
    CollectNativeButtonsForDynamicDiagnostics(root, buttons);

    ModLog(L"Dynamic native-button snapshot: count=%u root=%.1fx%.1f DIP",
           static_cast<unsigned>(buttons.size()),
           root ? root.ActualWidth() : 0.0,
           root ? root.ActualHeight() : 0.0);

    for (size_t i = 0; i < buttons.size(); ++i) {
        const auto& button = buttons[i];
        if (!button) {
            continue;
        }

        hstring className;
        hstring name;
        hstring automationName;
        hstring parentClass;
        hstring parentName;
        int row = -1;
        int col = -1;
        int visible = 0;
        int rendered = 0;
        double width = 0.0;
        double height = 0.0;

        try { className = winrt::get_class_name(button); } catch (...) {}
        try { name = button.Name(); } catch (...) {}
        try {
            automationName =
                Automation::AutomationProperties::GetName(button);
        } catch (...) {}
        try {
            row = Controls::Grid::GetRow(button);
            col = Controls::Grid::GetColumn(button);
            visible =
                button.Visibility() == Visibility::Visible ? 1 : 0;
            width = button.ActualWidth();
            height = button.ActualHeight();
            rendered =
                visible && width > 0.0 && height > 0.0 ? 1 : 0;
        } catch (...) {}
        try {
            auto parent =
                Media::VisualTreeHelper::GetParent(button)
                    .try_as<FrameworkElement>();
            if (parent) {
                parentClass = winrt::get_class_name(parent);
                parentName = parent.Name();
            }
        } catch (...) {}

        ModLog(
            L"Dynamic native-button[%u]: class=%s name=%s automation=%s visible=%d rendered=%d size=%.1fx%.1f row=%d col=%d parentClass=%s parentName=%s",
            static_cast<unsigned>(i),
            className.empty() ? L"-" : className.c_str(),
            name.empty() ? L"-" : name.c_str(),
            automationName.empty() ? L"-" : automationName.c_str(),
            visible,
            rendered,
            width,
            height,
            row,
            col,
            parentClass.empty() ? L"-" : parentClass.c_str(),
            parentName.empty() ? L"-" : parentName.c_str());

        std::vector<std::wstring> hints;
        CollectSystemButtonDiagnosticHints(button, hints);
        for (size_t hintIndex = 0;
             hintIndex < hints.size();
             ++hintIndex) {
            ModLog(
                L"Dynamic native-button[%u] hint[%u]=%s",
                static_cast<unsigned>(i),
                static_cast<unsigned>(hintIndex),
                hints[hintIndex].c_str());
        }
    }
}

void AttachDynamicSystemProviderDiagnostics(
        Controls::UserControl const& userControl,
        Controls::Image const& batteryImage,
        std::shared_ptr<InjectionSession> const& session) {
    const bool verbose =
        (g_enableLogging.load(std::memory_order_relaxed) ||
         g_enableFileLogging.load(std::memory_order_relaxed)) &&
        g_enableVerboseDiagnostics.load(std::memory_order_relaxed);
    const bool capture =
        g_enableXamlCapture.load(std::memory_order_relaxed);

    if (!userControl || !session || (!verbose && !capture)) {
        return;
    }

    try {
        auto weakControl = winrt::make_weak(userControl);
        auto weakBatteryImage = winrt::make_weak(batteryImage);
        auto layoutToken = std::make_shared<winrt::event_token>();
        auto lastSignature = std::make_shared<std::wstring>();
        auto baselineReady = std::make_shared<bool>(false);
        auto pendingFollowupCapture =
            std::make_shared<bool>(false);

        // Snapshot immediately after our own injection. Waiting for the first
        // diagnostic capture to finish can miss the exact transition: if the
        // next LayoutUpdated is caused by tapping the password box, that event
        // already contains the newly materialized Input button.
        try {
            auto initialRoot =
                userControl.Content().try_as<FrameworkElement>();
            if (initialRoot) {
                *lastSignature =
                    BuildNativeButtonDynamicSignature(initialRoot);
                *baselineReady = true;
                if (verbose) {
                    ModLog(
                        L"Dynamic SystemProvider native-button watcher baseline recorded at attachment");
                    LogDynamicNativeButtonDiagnostics(initialRoot);
                }
            }
        } catch (...) {
        }

        *layoutToken = userControl.LayoutUpdated(
            [weakControl, weakBatteryImage, lastSignature,
             baselineReady, pendingFollowupCapture, verbose](
                auto const&, auto const&) {
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                auto control = weakControl.get();
                if (!control) {
                    return;
                }

                try {
                    auto root =
                        control.Content().try_as<FrameworkElement>();
                    if (!root) {
                        return;
                    }

                    // Teardown can produce a final LayoutUpdated with a
                    // zero-sized root before Unloaded fires. Ignore it instead
                    // of generating a misleading full-tree diagnostic dump.
                    if (root.ActualWidth() <= 0.0 &&
                        root.ActualHeight() <= 0.0) {
                        return;
                    }

                    const auto signature =
                        BuildNativeButtonDynamicSignature(root);

                    if (!*baselineReady) {
                        *lastSignature = signature;
                        *baselineReady = true;
                        if (verbose) {
                            ModLog(
                                L"Dynamic SystemProvider native-button watcher late baseline recorded");
                            LogDynamicNativeButtonDiagnostics(root);
                        }
                        return;
                    }

                    if (signature != *lastSignature) {
                        *lastSignature = signature;
                        *pendingFollowupCapture = true;

                        if (verbose) {
                            ModLog(
                                L"Dynamic SystemProvider native-button tree changed (possible lazy-loaded Input/language control)");
                            LogDynamicNativeButtonDiagnostics(root);

                            // One full tree dump at the transition tells us
                            // whether the new control lives in the same native
                            // button Grid or another XAML host.
                            LogTree(root);
                        }
                    }

                    if (*pendingFollowupCapture &&
                        g_enableXamlCapture.load(
                            std::memory_order_relaxed) &&
                        !g_captureScheduled.load(
                            std::memory_order_relaxed)) {
                        auto image = weakBatteryImage.get();
                        if (image) {
                            *pendingFollowupCapture = false;
                            ModLog(
                                L"Dynamic SystemProvider change: arming follow-up XAML capture");
                            ScheduleXamlSubtreeCapture(
                                control, image);
                        }
                    }
                } catch (...) {
                }
            });

        session->diagnosticsLayoutToken = *layoutToken;
        session->diagnosticsLayoutAttached = true;

        if (verbose) {
            ModLog(
                L"Dynamic SystemProvider native-button watcher attached (event-driven; verbose diagnostics)");
        } else {
            ModLog(
                L"Dynamic SystemProvider watcher attached for XAML capture only");
        }
    } catch (const winrt::hresult_error& e) {
        ModLog(
            L"Failed to attach dynamic SystemProvider diagnostics: 0x%08X %s",
            static_cast<unsigned>(e.code().value),
            e.message().c_str());
    } catch (...) {
        ModLog(
            L"Failed to attach dynamic SystemProvider diagnostics with unknown exception");
    }
}

bool IsSystemProviderUserControl(
        Controls::UserControl const& userControl) {
    if (!userControl) return false;
    try {
        return winrt::get_class_name(userControl) ==
               L"LogonUX.SystemProviderUserControl";
    } catch (...) {
        return false;
    }
}

void InjectVisual(void* sender, bool allowLayoutRetry = true);

bool TryMarkLayoutRetryActive(void* identity) {
    if (!identity) {
        return false;
    }

    AcquireSRWLockExclusive(&g_layoutRetryLock);
    const bool alreadyActive =
        std::find(g_layoutRetryOwners.begin(),
                  g_layoutRetryOwners.end(),
                  identity) != g_layoutRetryOwners.end();
    if (!alreadyActive) {
        g_layoutRetryOwners.push_back(identity);
    }
    ReleaseSRWLockExclusive(&g_layoutRetryLock);
    return !alreadyActive;
}

void ClearLayoutRetryActive(void* identity) {
    if (!identity) {
        return;
    }

    AcquireSRWLockExclusive(&g_layoutRetryLock);
    auto it = std::remove(
        g_layoutRetryOwners.begin(),
        g_layoutRetryOwners.end(),
        identity);
    g_layoutRetryOwners.erase(it, g_layoutRetryOwners.end());
    ReleaseSRWLockExclusive(&g_layoutRetryLock);
}

struct LayoutRetryRegistration {
    winrt::weak_ref<Controls::UserControl> owner;
    DWORD xamlThreadId = 0;
    event_token layoutToken{};
    event_token unloadedToken{};
    bool layoutAttached = false;
    bool unloadedAttached = false;
    void* identity = nullptr;
    std::atomic<bool> detached{false};
};

SRWLOCK g_layoutRetryRegistrationsLock = SRWLOCK_INIT;
std::vector<std::shared_ptr<LayoutRetryRegistration>>
    g_layoutRetryRegistrations;

void TrackLayoutRetryRegistration(
        std::shared_ptr<LayoutRetryRegistration> const& registration) {
    AcquireSRWLockExclusive(&g_layoutRetryRegistrationsLock);
    g_layoutRetryRegistrations.push_back(registration);
    ReleaseSRWLockExclusive(&g_layoutRetryRegistrationsLock);
}

void UntrackLayoutRetryRegistration(
        std::shared_ptr<LayoutRetryRegistration> const& registration) {
    AcquireSRWLockExclusive(&g_layoutRetryRegistrationsLock);
    auto it = std::remove_if(
        g_layoutRetryRegistrations.begin(),
        g_layoutRetryRegistrations.end(),
        [&](const std::shared_ptr<LayoutRetryRegistration>& item) {
            return item.get() == registration.get();
        });
    g_layoutRetryRegistrations.erase(
        it, g_layoutRetryRegistrations.end());
    ReleaseSRWLockExclusive(&g_layoutRetryRegistrationsLock);
}

void DetachLayoutRetryRegistration(
        std::shared_ptr<LayoutRetryRegistration> const& registration,
        bool detachUnloaded) {
    if (!registration || registration->detached.exchange(true)) {
        return;
    }

    auto control = registration->owner.get();
    if (control) {
        if (registration->layoutAttached) {
            try {
                control.LayoutUpdated(registration->layoutToken);
            } catch (...) {
            }
            registration->layoutAttached = false;
        }
        if (detachUnloaded && registration->unloadedAttached) {
            try {
                control.Unloaded(registration->unloadedToken);
            } catch (...) {
            }
            registration->unloadedAttached = false;
        }
    }

    ClearLayoutRetryActive(registration->identity);
    UntrackLayoutRetryRegistration(registration);
}

std::vector<DWORD> SnapshotLayoutRetryThreads() {
    std::vector<DWORD> result;
    AcquireSRWLockShared(&g_layoutRetryRegistrationsLock);
    for (const auto& registration : g_layoutRetryRegistrations) {
        if (!registration || !registration->xamlThreadId) continue;
        if (std::find(result.begin(), result.end(), registration->xamlThreadId) ==
            result.end()) {
            result.push_back(registration->xamlThreadId);
        }
    }
    ReleaseSRWLockShared(&g_layoutRetryRegistrationsLock);
    return result;
}

void DetachLayoutRetriesOnCurrentThread() {
    const DWORD currentThreadId = GetCurrentThreadId();
    std::vector<std::shared_ptr<LayoutRetryRegistration>> registrations;
    AcquireSRWLockShared(&g_layoutRetryRegistrationsLock);
    registrations = g_layoutRetryRegistrations;
    ReleaseSRWLockShared(&g_layoutRetryRegistrationsLock);

    for (const auto& registration : registrations) {
        if (registration && registration->xamlThreadId == currentThreadId) {
            DetachLayoutRetryRegistration(registration, true);
        }
    }
}

void AttachLayoutRetry(Controls::UserControl const& userControl) {
    RememberXamlThread(GetCurrentThreadId(), true);
    void* identity = reinterpret_cast<void*>(winrt::get_abi(userControl));
    if (!TryMarkLayoutRetryActive(identity)) {
        ModLog(
            L"PowerOptions lazy-load retry already armed for this SystemProvider; duplicate request coalesced");
        return;
    }

    try {
        auto registration =
            std::make_shared<LayoutRetryRegistration>();
        registration->owner = winrt::make_weak(userControl);
        registration->xamlThreadId = GetCurrentThreadId();
        registration->identity = identity;

        auto weakRegistration =
            std::weak_ptr<LayoutRetryRegistration>(registration);

        registration->layoutToken = userControl.LayoutUpdated(
            [weakRegistration](auto const&, auto const&) {
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                auto registration = weakRegistration.lock();
                if (!registration) {
                    return;
                }
                auto control = registration->owner.get();
                if (!control) {
                    DetachLayoutRetryRegistration(registration, false);
                    return;
                }

                try {
                    auto root = control.Content().try_as<FrameworkElement>();
                    if (!root) {
                        return;
                    }

                    if (FindByName(root, kInjectedBatteryName) ||
                        FindByName(root, kInjectedVolumeName)) {
                        DetachLayoutRetryRegistration(registration, true);
                        ModLog(
                            L"Lazy-load retry detached: visuals already present");
                        return;
                    }

                    auto powerButton = FindByName(root, L"PowerOptions")
                                           .try_as<Controls::Button>();
                    if (!powerButton || powerButton.ActualWidth() <= 0.0 ||
                        powerButton.ActualHeight() <= 0.0 ||
                        !FindFirstTextBlock(powerButton)) {
                        return;
                    }

                    ModLog(
                        L"Lazy-loaded PowerOptions is now realized; retrying injection");
                    InjectVisual(
                        reinterpret_cast<void*>(winrt::get_abi(control)),
                        false);

                    root = control.Content().try_as<FrameworkElement>();
                    if (root &&
                        (FindByName(root, kInjectedBatteryName) ||
                         FindByName(root, kInjectedVolumeName))) {
                        DetachLayoutRetryRegistration(registration, true);
                        ModLog(
                            L"Lazy-load retry detached after successful injection");
                    }
                } catch (...) {
                }
            });
        registration->layoutAttached = true;

        registration->unloadedToken = userControl.Unloaded(
            [weakRegistration](auto const&, auto const&) {
                auto registration = weakRegistration.lock();
                if (!registration) {
                    return;
                }
                // LogonUI can transiently unload and then reload the same
                // SystemProvider object while switching sign-in surfaces.
                // Keep the retry armed; BeforeUninit performs deterministic
                // cross-thread detachment on the owning XAML thread.
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                ModLog(
                    L"PowerOptions lazy-load retry retained across transient SystemProvider unload");
            });
        registration->unloadedAttached = true;

        TrackLayoutRetryRegistration(registration);
        ModLog(
            L"PowerOptions not realized yet; armed one coalesced LayoutUpdated lazy-load retry");
    } catch (const winrt::hresult_error& e) {
        ClearLayoutRetryActive(identity);
        ModLog(L"Failed to arm LayoutUpdated retry: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ClearLayoutRetryActive(identity);
        ModLog(
            L"Failed to arm LayoutUpdated retry with unknown exception");
    }
}


void CleanupInjectionSession(
        std::shared_ptr<InjectionSession> const& session,
        bool restoreLayout,
        bool fromOwnerUnload) {
    if (!session || session->cleaning.exchange(true)) {
        return;
    }

    auto owner = session->owner.get();
    auto grid = session->grid.get();

    // Detach callbacks owned by the persistent native UserControl first. This
    // is the critical hot-unload invariant: after this point Windows XAML no
    // longer holds delegates whose code lives inside this mod DLL.
    if (owner) {
        if (session->spacingLayoutAttached) {
            try {
                owner.LayoutUpdated(session->spacingLayoutToken);
            } catch (...) {
            }
            session->spacingLayoutAttached = false;
        }
        if (session->diagnosticsLayoutAttached) {
            try {
                owner.LayoutUpdated(session->diagnosticsLayoutToken);
            } catch (...) {
            }
            session->diagnosticsLayoutAttached = false;
        }
        if (!fromOwnerUnload && session->ownerUnloadedAttached) {
            try {
                owner.Unloaded(session->ownerUnloadedToken);
            } catch (...) {
            }
            session->ownerUnloadedAttached = false;
        } else if (fromOwnerUnload) {
            session->ownerUnloadedAttached = false;
        }
        if (session->ownerLoadedAttached) {
            try {
                owner.Loaded(session->ownerLoadedToken);
            } catch (...) {
            }
            session->ownerLoadedAttached = false;
        }
    }

    session->active.store(false, std::memory_order_release);
    StopVolumeMonitor(session->volumeState);
    session->volumeState.reset();

    DetachBatteryBrightnessWheelHandler(session->batteryWheel);
    session->batteryWheel.reset();

    if (session->batteryButton && session->batteryClickAttached) {
        try {
            session->batteryButton.Click(session->batteryClickToken);
        } catch (...) {
        }
        session->batteryClickAttached = false;
    }
    if (session->volumeButton && session->volumeClickAttached) {
        try {
            session->volumeButton.Click(session->volumeClickToken);
        } catch (...) {
        }
        session->volumeClickAttached = false;
    }

    if (restoreLayout && grid) {
        try {
            auto children = grid.Children();
            auto removeInjected = [&](Controls::Button const& button) {
                if (!button) return;
                uint32_t index = 0;
                if (children.IndexOf(button, index)) {
                    children.RemoveAt(index);
                }
            };
            removeInjected(session->batteryButton);
            removeInjected(session->volumeButton);
        } catch (...) {
            ModLog(L"Hot-unload: couldn't remove one or more injected controls");
        }

        for (const auto& snapshot : session->nativeElements) {
            auto element = snapshot.element.get();
            if (!element) {
                continue;
            }
            try {
                if (snapshot.restoreColumn) {
                    Controls::Grid::SetColumn(element, snapshot.column);
                }
                if (snapshot.restoreVisibility) {
                    element.Visibility(snapshot.visibility);
                }
                if (snapshot.restoreMargin) {
                    element.Margin(snapshot.margin);
                }
                if (snapshot.restoreSizing) {
                    element.Width(snapshot.width);
                    element.MinWidth(snapshot.minWidth);
                    element.MaxWidth(snapshot.maxWidth);
                }

                if (snapshot.isControl && snapshot.restoreTabState) {
                    if (auto control = element.try_as<Controls::Control>()) {
                        control.IsTabStop(snapshot.isTabStop);
                        control.TabIndex(snapshot.tabIndex);
                    }
                }

                if (snapshot.restoreImeVisualOffset) {
                    ApplyImeModeGlyphVisualOffsetRecursive(element, 0.0);
                }
            } catch (...) {
            }
        }

        try {
            auto columns = grid.ColumnDefinitions();
            while (columns.Size() > session->originalColumnCount) {
                columns.RemoveAt(columns.Size() - 1);
            }
            const uint32_t restoreCount = std::min(
                columns.Size(),
                static_cast<uint32_t>(session->originalColumns.size()));
            for (uint32_t i = 0; i < restoreCount; ++i) {
                auto definition = columns.GetAt(i);
                const auto& snapshot = session->originalColumns[i];
                definition.Width(snapshot.width);
                definition.MinWidth(snapshot.minWidth);
                definition.MaxWidth(snapshot.maxWidth);
            }
        } catch (...) {
            ModLog(L"Hot-unload: couldn't fully restore native Grid columns");
        }

        // Do not remove/reinsert Windows-owned buttons during hot-unload.
        // That transiently fires their native Unloaded lifecycle and can make
        // LogonUX permanently drop Accessibility/Network controls from the
        // still-running SystemProvider. Grid columns + TabIndex are restored
        // in place instead.
    }

    session->batteryButton = nullptr;
    session->volumeButton = nullptr;
    session->nativeElements.clear();
    session->originalColumns.clear();

    UntrackInjectionSession(session);
    if (restoreLayout) {
        ModLog(L"Hot-unload: injection session detached and native layout restored");
    } else {
        ModLog(L"Injection session retired without replaying a stale native-layout snapshot");
    }
}

void CleanupInjectionSessionsOnCurrentThread(bool restoreLayout) {
    const DWORD currentThreadId = GetCurrentThreadId();
    std::vector<std::shared_ptr<InjectionSession>> sessions;
    AcquireSRWLockShared(&g_injectionSessionsLock);
    sessions = g_injectionSessions;
    ReleaseSRWLockShared(&g_injectionSessionsLock);

    for (const auto& session : sessions) {
        if (session && session->xamlThreadId == currentThreadId) {
            CleanupInjectionSession(session, restoreLayout, false);
        }
    }
}

void RetireInactiveInjectionSessionsOnThread(
        DWORD threadId,
        void* keepOwnerIdentity) {
    std::vector<std::shared_ptr<InjectionSession>> sessions;
    AcquireSRWLockShared(&g_injectionSessionsLock);
    sessions = g_injectionSessions;
    ReleaseSRWLockShared(&g_injectionSessionsLock);

    for (const auto& session : sessions) {
        if (!session || session->xamlThreadId != threadId ||
            session->ownerIdentity == keepOwnerIdentity ||
            session->active.load(std::memory_order_acquire)) {
            continue;
        }
        ModLog(L"Retiring stale inactive SystemProvider session on XAML thread=%lu owner=%p",
               threadId, session->ownerIdentity);
        CleanupInjectionSession(session, false, false);
    }
}

void InjectVisual(void* sender, bool allowLayoutRetry) {
    try {
        if (g_unloading.load(std::memory_order_acquire)) {
            return;
        }
        if (!sender) {
            ModLog(L"Loaded hook supplied a null sender");
            return;
        }

        winrt::Windows::Foundation::IUnknown unknown;
        winrt::copy_from_abi(unknown, sender);

        auto userControl = unknown.try_as<Controls::UserControl>();
        if (!userControl) {
            ModLog(L"Couldn't project SystemProviderUserControl as UserControl");
            return;
        }

        if (!IsSystemProviderUserControl(userControl)) {
            try {
                ModLog(L"Rejected non-SystemProvider UserControl class=%s",
                       winrt::get_class_name(userControl).c_str());
            } catch (...) {
                ModLog(L"Rejected non-SystemProvider UserControl");
            }
            return;
        }

        auto root = userControl.Content().try_as<FrameworkElement>();
        if (!root) {
            ModLog(L"SystemProviderUserControl content isn't a FrameworkElement");
            return;
        }

        auto powerElement = FindByName(root, L"PowerOptions");
        auto powerButton = powerElement.try_as<Controls::Button>();
        if (!powerButton) {
            ModLog(L"PowerOptions button not found; no UI change made");
            LogTree(root);
            return;
        }

        const DWORD currentXamlThreadId = GetCurrentThreadId();
        RememberXamlThread(currentXamlThreadId, true);
        void* ownerIdentity =
            reinterpret_cast<void*>(winrt::get_abi(userControl));
        RetireInactiveInjectionSessionsOnThread(
            currentXamlThreadId, ownerIdentity);

        auto existingSession =
            FindInjectionSessionForOwner(ownerIdentity);
        const bool injectedVisualPresent =
            FindByName(root, kInjectedBatteryName) ||
            FindByName(root, kInjectedVolumeName);
        if (existingSession && injectedVisualPresent) {
            existingSession->active.store(true, std::memory_order_release);
            ModLog(L"Battery/Volume visuals already exist; retained session revalidated and duplicate skipped");
            return;
        }
        if (existingSession && !injectedVisualPresent) {
            ModLog(L"SystemProvider tree was rebuilt without retained injected visuals; retiring stale session before reinjection");
            CleanupInjectionSession(existingSession, false, false);
        } else if (injectedVisualPresent) {
            ModLog(L"Battery/Volume visuals already exist without a tracked session; skipping duplicate until orphan cleanup");
            return;
        }

        auto parent = Media::VisualTreeHelper::GetParent(powerButton);
        auto parentGrid = parent.try_as<Controls::Grid>();
        if (!parentGrid) {
            ModLog(L"PowerOptions parent isn't a Grid; no UI change made");
            LogTree(root);
            return;
        }

        auto margin = powerButton.Margin();
        const double actualWidth = powerButton.ActualWidth();
        const double actualHeight = powerButton.ActualHeight();
        const int row = Controls::Grid::GetRow(powerButton);
        const int column = Controls::Grid::GetColumn(powerButton);
        const int rowSpan = Controls::Grid::GetRowSpan(powerButton);
        const int columnSpan = Controls::Grid::GetColumnSpan(powerButton);

        ModLog(L"PowerOptions geometry: size=%.1fx%.1f margin=(%.1f,%.1f,%.1f,%.1f) row=%d col=%d rowSpan=%d colSpan=%d hAlign=%d vAlign=%d",
               actualWidth,
               actualHeight,
               margin.Left,
               margin.Top,
               margin.Right,
               margin.Bottom,
               row,
               column,
               rowSpan,
               columnSpan,
               static_cast<int>(powerButton.HorizontalAlignment()),
               static_cast<int>(powerButton.VerticalAlignment()));

        if (actualWidth <= 0.0 || actualHeight <= 0.0) {
            ModLog(L"PowerOptions exists but is not rendered yet (%.1fx%.1f)",
                   actualWidth, actualHeight);
            if (allowLayoutRetry) {
                AttachLayoutRetry(userControl);
            }
            return;
        }

        auto nativePowerGlyph = FindFirstTextBlock(powerButton);
        if (!nativePowerGlyph) {
            ModLog(L"Couldn't find native PowerOptions TextBlock; PowerOptions template is not realized yet");
            if (allowLayoutRetry) {
                AttachLayoutRetry(userControl);
            }
            return;
        }

        try {
            auto ff = nativePowerGlyph.FontFamily();
            ModLog(L"Native Power glyph typography: font=%s size=%.1f text=U+%04X",
                   ff ? ff.Source().c_str() : L"-",
                   nativePowerGlyph.FontSize(),
                   nativePowerGlyph.Text().empty() ? 0 : nativePowerGlyph.Text()[0]);
        } catch (...) {
        }
        try {
            ModLog(L"Native Power keyboard focus: IsTabStop=%d TabIndex=%d",
                   powerButton.IsTabStop() ? 1 : 0,
                   powerButton.TabIndex());
        } catch (...) {
        }

        hstring nativeGlyphFontSource;
        try {
            auto ff = nativePowerGlyph.FontFamily();
            if (ff) {
                nativeGlyphFontSource = ff.Source();
            }
        } catch (...) {
        }
        const std::wstring_view nativeGlyphFontView(
            nativeGlyphFontSource.c_str(), nativeGlyphFontSource.size());
        const bool nativePowerUsesFluent =
            nativeGlyphFontView.find(L"Fluent") != std::wstring_view::npos;

        // Explicit renderer modes are intentionally unrestricted. This is a
        // Advanced setting: an explicit renderer means "try exactly this"
        // regardless of Windows version. A missing font may therefore render
        // tofu/squares, and a resource container that doesn't exist on that OS
        // may fail to load. Automatic remains the only compatibility-oriented
        // mode.
        const VolumeRendererMode rendererMode = g_volumeRenderer.load();

        bool useSndVolResources = false;
        SndVolResourceSource sndVolSource =
            SndVolResourceSource::WindowsAutomatic;
        hstring explicitFontFamily;
        bool useFluentDisabledGlyph = false;

        switch (rendererMode) {
            case VolumeRendererMode::Automatic:
                // Existing recommended behavior: the already-realized native
                // Power glyph tells us which LogonUI visual generation is in
                // use. Modern Fluent LogonUI uses Fluent font glyphs; older
                // LogonUI uses SndVolSSO through Windows resource lookup.
                if (nativePowerUsesFluent) {
                    explicitFontFamily = L"Segoe Fluent Icons";
                    useFluentDisabledGlyph = true;
                } else {
                    useSndVolResources = true;
                    sndVolSource = SndVolResourceSource::WindowsAutomatic;
                }
                break;

            case VolumeRendererMode::SegoeMdl2:
                explicitFontFamily = L"Segoe MDL2 Assets";
                useFluentDisabledGlyph = false;
                break;

            case VolumeRendererMode::SegoeFluent:
                explicitFontFamily = L"Segoe Fluent Icons";
                useFluentDisabledGlyph = true;
                break;

            case VolumeRendererMode::SndVolAutomatic:
                useSndVolResources = true;
                sndVolSource = SndVolResourceSource::WindowsAutomatic;
                break;

            case VolumeRendererMode::SndVolDll:
                useSndVolResources = true;
                sndVolSource = SndVolResourceSource::DirectDll;
                break;

            case VolumeRendererMode::SndVolMun:
                useSndVolResources = true;
                sndVolSource = SndVolResourceSource::DirectMun;
                break;
        }

        // MDL2 has no exact single-color equivalent of Win10's multicolor
        // red-X no-endpoint artwork. Fluent does have VolumeDisabled U+EA85.
        const wchar_t noEndpointGlyph = useFluentDisabledGlyph
            ? kVolumeGlyphDisabledFluent
            : kVolumeGlyphMute;

        ModLog(L"Volume renderer selected: %s (explicit modes unrestricted)",
               VolumeRendererModeName(rendererMode));

        Controls::Button batteryButton;
        batteryButton.Name(kInjectedBatteryName);
        // Participate in keyboard focus exactly like the native PowerOptions
        // button. The system-button child collection is reordered later so
        // equal/default TabIndex values follow the configured visual order.
        batteryButton.IsHitTestVisible(true);
        try {
            batteryButton.IsTabStop(powerButton.IsTabStop());
            batteryButton.TabIndex(powerButton.TabIndex());
        } catch (...) {
            batteryButton.IsTabStop(true);
        }
        batteryButton.HorizontalAlignment(powerButton.HorizontalAlignment());
        batteryButton.VerticalAlignment(powerButton.VerticalAlignment());
        try {
            Automation::AutomationProperties::SetName(
                batteryButton, hstring(L"Battery"));
        } catch (...) {
        }

        const bool automaticBatteryVisibility =
            g_batteryVisibilityAutomatic.load(std::memory_order_relaxed);
        if (automaticBatteryVisibility) {
            // Give the button a sensible initial state before the live LogonUX
            // visibility binding resolves. BatteryFlag bit 7 means that no
            // system battery is present; 255 means unknown percentage.
            SYSTEM_POWER_STATUS visibilityStatus{};
            const bool batteryPresent =
                GetSystemPowerStatus(&visibilityStatus) &&
                (visibilityStatus.BatteryFlag & 0x80) == 0 &&
                visibilityStatus.BatteryLifePercent != 255;
            batteryButton.Visibility(
                batteryPresent ? Visibility::Visible : Visibility::Collapsed);
        }

        Controls::Image batteryImage;
        batteryImage.HorizontalAlignment(HorizontalAlignment::Center);
        batteryImage.VerticalAlignment(VerticalAlignment::Center);
        batteryImage.Stretch(Media::Stretch::Uniform);
        // Match the battery artwork to the native system-button generation.
        // Win11 uses a 16-DIP Power glyph; Win10 22H2 uses a much larger
        // 30-DIP Segoe MDL2 Power glyph. Keep the accepted Win11 battery at
        // 16 DIP, but use 24 DIP for the older/larger Win10 presentation.
        double batteryIconSize = 16.0;
        try {
            if (nativePowerGlyph.FontSize() >= 24.0) {
                batteryIconSize = 24.0;
            }
        } catch (...) {
        }
        batteryImage.Width(batteryIconSize);
        batteryImage.Height(batteryIconSize);
        ModLog(L"Battery visual size selected: %.1f DIP", batteryIconSize);

        try {
            auto dataContext = userControl.DataContext();
            hstring dataContextClass;
            if (dataContext) {
                try {
                    dataContextClass = winrt::get_class_name(dataContext);
                } catch (...) {
                }
            }

            ModLog(L"SystemProvider DataContext class=%s",
                   dataContextClass.empty() ? L"<null/unknown>" : dataContextClass.c_str());

            if (dataContext) {
                const bool isSystemStatus =
                    dataContextClass.size() != 0 &&
                    wcsstr(dataContextClass.c_str(), L"SystemStatusViewModel") != nullptr;
                const bool isRootViewModel =
                    dataContextClass.size() != 0 &&
                    wcsstr(dataContextClass.c_str(), L"RootViewModel") != nullptr;

                Data::Binding batteryBinding;
                batteryBinding.Source(dataContext);
                if (isSystemStatus) {
                    batteryBinding.Path(PropertyPath(L"BatteryStatusIcon"));
                    ModLog(L"Battery binding path selected: BatteryStatusIcon");
                } else if (isRootViewModel) {
                    batteryBinding.Path(PropertyPath(L"SysViewModel.BatteryStatusIcon"));
                    ModLog(L"Battery binding path selected: SysViewModel.BatteryStatusIcon");
                } else {
                    batteryBinding.Path(PropertyPath(L"BatteryStatusIcon"));
                    ModLog(L"Battery binding path selected speculatively: BatteryStatusIcon");
                }

                batteryBinding.Mode(Data::BindingMode::OneWay);
                batteryImage.SetBinding(Controls::Image::SourceProperty(), batteryBinding);
                ModLog(L"Installed live BatteryStatusIcon -> Image.Source binding");

                if (automaticBatteryVisibility) {
                    try {
                        // Windows 10 1607+ has a built-in Boolean-to-Visibility
                        // conversion for traditional Binding. Our supported
                        // range begins at Windows 10 1809, so binding directly
                        // to LogonUX's native visibility decision avoids
                        // retaining a mod-owned IValueConverter callback.
                        Data::Binding batteryVisibilityBinding;
                        batteryVisibilityBinding.Source(dataContext);
                        if (isSystemStatus) {
                            batteryVisibilityBinding.Path(
                                PropertyPath(L"IsBatteryStatusVisible"));
                        } else if (isRootViewModel) {
                            batteryVisibilityBinding.Path(
                                PropertyPath(L"SysViewModel.IsBatteryStatusVisible"));
                        } else {
                            batteryVisibilityBinding.Path(
                                PropertyPath(L"IsBatteryStatusVisible"));
                        }
                        batteryVisibilityBinding.Mode(Data::BindingMode::OneWay);
                        batteryButton.SetBinding(
                            UIElement::VisibilityProperty(),
                            batteryVisibilityBinding);
                        ModLog(L"Installed native LogonUX IsBatteryStatusVisible -> Battery.Visibility binding");
                    } catch (const winrt::hresult_error& e) {
                        ModLog(L"Battery automatic visibility binding failed: 0x%08X %s; keeping power-status fallback",
                               static_cast<unsigned>(e.code().value),
                               e.message().c_str());
                    } catch (...) {
                        ModLog(L"Battery automatic visibility binding failed; keeping power-status fallback");
                    }
                }

                // Reuse Windows' own LogonUX BatteryStatusText rather than
                // synthesizing a percentage/status string in the mod.
                try {
                    Controls::ToolTip batteryToolTip;
                    Data::Binding batteryTextBinding;
                    batteryTextBinding.Source(dataContext);
                    if (isSystemStatus) {
                        batteryTextBinding.Path(PropertyPath(L"BatteryStatusText"));
                    } else if (isRootViewModel) {
                        batteryTextBinding.Path(PropertyPath(L"SysViewModel.BatteryStatusText"));
                    } else {
                        batteryTextBinding.Path(PropertyPath(L"BatteryStatusText"));
                    }
                    batteryTextBinding.Mode(Data::BindingMode::OneWay);
                    batteryToolTip.SetBinding(
                        Controls::ContentControl::ContentProperty(),
                        batteryTextBinding);
                    Controls::ToolTipService::SetToolTip(
                        batteryButton, batteryToolTip);
                    ModLog(L"Installed native LogonUX BatteryStatusText hover tooltip");

                    Data::Binding batteryAutomationNameBinding;
                    batteryAutomationNameBinding.Source(dataContext);
                    if (isSystemStatus) {
                        batteryAutomationNameBinding.Path(PropertyPath(L"BatteryStatusText"));
                    } else if (isRootViewModel) {
                        batteryAutomationNameBinding.Path(PropertyPath(L"SysViewModel.BatteryStatusText"));
                    } else {
                        batteryAutomationNameBinding.Path(PropertyPath(L"BatteryStatusText"));
                    }
                    batteryAutomationNameBinding.Mode(Data::BindingMode::OneWay);
                    batteryButton.SetBinding(
                        Automation::AutomationProperties::NameProperty(),
                        batteryAutomationNameBinding);
                    ModLog(L"Installed live BatteryStatusText -> AutomationProperties.Name binding");
                } catch (const winrt::hresult_error& e) {
                    ModLog(L"Battery hover tooltip binding failed: 0x%08X %s",
                           static_cast<unsigned>(e.code().value),
                           e.message().c_str());
                } catch (...) {
                    ModLog(L"Battery hover tooltip binding failed with unknown exception");
                }

                try {
                    auto resolvedSource = batteryImage.Source();
                    hstring sourceClass;
                    if (resolvedSource) {
                        try { sourceClass = winrt::get_class_name(resolvedSource); } catch (...) {}
                    }
                    ModLog(L"Resolved BatteryStatusIcon source class=%s",
                           sourceClass.empty() ? L"<null/unknown>" : sourceClass.c_str());
                } catch (...) {
                    ModLog(L"Couldn't inspect resolved BatteryStatusIcon image source");
                }
            } else {
                ModLog(L"SystemProvider DataContext is null; Battery image source left empty");
            }
        } catch (const winrt::hresult_error& e) {
            ModLog(L"Battery image binding setup failed: 0x%08X %s",
                   static_cast<unsigned>(e.code().value),
                   e.message().c_str());
        } catch (...) {
            ModLog(L"Battery image binding setup failed with unknown exception");
        }

        Controls::TextBlock batteryPercentText;
        ConfigurePercentageTextBlock(batteryPercentText);

        const auto batteryPercentagePosition =
            g_batteryPercentagePosition.load(std::memory_order_relaxed);
        const double percentageSpacing = static_cast<double>(
            g_percentageSpacing.load(std::memory_order_relaxed));
        batteryPercentText.Margin(
            batteryPercentagePosition == PercentagePosition::Left
                ? Thickness{0.0, 0.0, percentageSpacing, 0.0}
                : Thickness{percentageSpacing, 0.0, 0.0, 0.0});

        SYSTEM_POWER_STATUS initialPowerStatus{};
        const bool haveInitialBattery =
            GetSystemPowerStatus(&initialPowerStatus) &&
            initialPowerStatus.BatteryLifePercent <= 100;
        SetPercentageLabelText(
            batteryPercentText,
            haveInitialBattery
                ? FormatPercentageText(
                      static_cast<int>(
                          initialPowerStatus.BatteryLifePercent),
                      false,
                      false)
                : std::wstring{},
            g_showBatteryPercentage.load(std::memory_order_relaxed));

        Controls::StackPanel batteryVisual;
        batteryVisual.Orientation(Controls::Orientation::Horizontal);
        batteryVisual.HorizontalAlignment(HorizontalAlignment::Center);
        batteryVisual.VerticalAlignment(VerticalAlignment::Center);
        if (batteryPercentagePosition == PercentagePosition::Left) {
            batteryVisual.Children().Append(batteryPercentText);
            batteryVisual.Children().Append(batteryImage);
        } else {
            batteryVisual.Children().Append(batteryImage);
            batteryVisual.Children().Append(batteryPercentText);
        }

        batteryButton.Content(batteryVisual);
        CopySafeVisualProperties(powerButton, batteryButton);

        if (g_showBatteryPercentage.load(std::memory_order_relaxed)) {
            AllowInjectedButtonToDynamicallySizeForPercentage(
                batteryButton,
                actualWidth);
            g_batteryPercentText = winrt::make_weak(batteryPercentText);
            EnsureBatteryUiBridgeWindow();
            EnsureBatteryNotificationWatcherStarted();
        }

        auto batteryClickToken = batteryButton.Click(RoutedEventHandler(
            [](winrt::Windows::Foundation::IInspectable const& sender,
               RoutedEventArgs const&) {
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                auto element = sender.try_as<FrameworkElement>();
                if (!element) {
                    ModLog(L"Battery click sender isn't a FrameworkElement; refusing classic flyout");
                    return;
                }
                ShowClassicBatteryFlyout(element);
            }));

        Controls::Button volumeButton;
        volumeButton.Name(kInjectedVolumeName);
        volumeButton.IsHitTestVisible(true);
        try {
            volumeButton.IsTabStop(powerButton.IsTabStop());
            volumeButton.TabIndex(powerButton.TabIndex());
        } catch (...) {
            volumeButton.IsTabStop(true);
        }
        volumeButton.HorizontalAlignment(powerButton.HorizontalAlignment());
        volumeButton.VerticalAlignment(powerButton.VerticalAlignment());
        try {
            Automation::AutomationProperties::SetName(
                volumeButton, hstring(L"Volume"));
        } catch (...) {
        }

        Controls::TextBlock volumeIcon;
        wchar_t initialVolumeText[2]{noEndpointGlyph, L'\0'};
        volumeIcon.Text(initialVolumeText); // Unavailable state until the first live Core Audio read.
        CopyNativeGlyphTypography(nativePowerGlyph, volumeIcon);
        if (!explicitFontFamily.empty()) {
            try {
                volumeIcon.FontFamily(Media::FontFamily(explicitFontFamily));
                ModLog(L"Volume font family forced to %s",
                       explicitFontFamily.c_str());
            } catch (const winrt::hresult_error& e) {
                ModLog(L"Couldn't apply requested Volume font family %s: 0x%08X %s",
                       explicitFontFamily.c_str(),
                       static_cast<unsigned>(e.code().value),
                       e.message().c_str());
            } catch (...) {
                ModLog(L"Couldn't apply requested Volume font family %s",
                       explicitFontFamily.c_str());
            }
        }

        Controls::Grid volumeIconHost;
        volumeIconHost.HorizontalAlignment(HorizontalAlignment::Center);
        volumeIconHost.VerticalAlignment(VerticalAlignment::Center);
        volumeIconHost.Children().Append(volumeIcon);

        Controls::TextBlock volumePercentText;
        ConfigurePercentageTextBlock(volumePercentText);

        const auto volumePercentagePosition =
            g_volumePercentagePosition.load(std::memory_order_relaxed);
        const double volumePercentageSpacing = static_cast<double>(
            g_percentageSpacing.load(std::memory_order_relaxed));
        volumePercentText.Margin(
            volumePercentagePosition == PercentagePosition::Left
                ? Thickness{0.0, 0.0, volumePercentageSpacing, 0.0}
                : Thickness{volumePercentageSpacing, 0.0, 0.0, 0.0});
        volumePercentText.Text(L"");
        volumePercentText.Visibility(Visibility::Collapsed);

        Controls::StackPanel volumeVisual;
        volumeVisual.Orientation(Controls::Orientation::Horizontal);
        volumeVisual.HorizontalAlignment(HorizontalAlignment::Center);
        volumeVisual.VerticalAlignment(VerticalAlignment::Center);
        if (volumePercentagePosition == PercentagePosition::Left) {
            volumeVisual.Children().Append(volumePercentText);
            volumeVisual.Children().Append(volumeIconHost);
        } else {
            volumeVisual.Children().Append(volumeIconHost);
            volumeVisual.Children().Append(volumePercentText);
        }

        NativeSndVolVisuals nativeSndVol{};
        if (useSndVolResources) {
            nativeSndVol.muted = LoadSndVolGroupBitmap(kSndVolGroupMute, sndVolSource);
            nativeSndVol.zero = LoadSndVolGroupBitmap(kSndVolGroup0, sndVolSource);
            nativeSndVol.low = LoadSndVolGroupBitmap(kSndVolGroup1, sndVolSource);
            nativeSndVol.medium = LoadSndVolGroupBitmap(kSndVolGroup2, sndVolSource);
            nativeSndVol.high = LoadSndVolGroupBitmap(kSndVolGroup3, sndVolSource);
            nativeSndVol.noEndpoint = LoadSndVolGroupBitmap(kSndVolGroupNoEndpoint, sndVolSource);

            if (nativeSndVol.muted && nativeSndVol.zero &&
                nativeSndVol.low && nativeSndVol.medium &&
                nativeSndVol.high && nativeSndVol.noEndpoint) {
                nativeSndVol.image = Controls::Image();
                nativeSndVol.image.Source(nativeSndVol.noEndpoint);
                nativeSndVol.image.Stretch(Media::Stretch::Uniform);
                nativeSndVol.image.HorizontalAlignment(
                    HorizontalAlignment::Center);
                nativeSndVol.image.VerticalAlignment(
                    VerticalAlignment::Center);

                double nativeImageSize = 30.0;
                try {
                    nativeImageSize = nativePowerGlyph.FontSize();
                } catch (...) {
                }
                nativeSndVol.image.Width(nativeImageSize);
                nativeSndVol.image.Height(nativeImageSize);
                nativeSndVol.image.Visibility(Visibility::Visible);
                volumeIcon.Visibility(Visibility::Collapsed);
                volumeIconHost.Children().Append(nativeSndVol.image);

                ModLog(
                    L"Volume visual uses SndVolSSO groups 120-125 at %.1f DIP (source mode=%d)",
                    nativeImageSize,
                    static_cast<int>(sndVolSource));
            } else {
                nativeSndVol = {};
                ModLog(
                    L"Requested SndVolSSO resource set incomplete; falling back to the selected/native Power font glyph path");
            }
        }

        // ToolTipService gives us the platform XAML tooltip presentation;
        // the content is kept live by the Core Audio monitor. The Button style
        // itself is still cloned from PowerOptions for LogonUI-native hover.
        Controls::ToolTip volumeToolTip;
        volumeToolTip.Content(box_value(hstring(L"Volume")));
        Controls::ToolTipService::SetToolTip(volumeButton, volumeToolTip);

        volumeButton.Content(volumeVisual);
        CopySafeVisualProperties(powerButton, volumeButton);

        if (g_showVolumePercentage.load(std::memory_order_relaxed)) {
            AllowInjectedButtonToDynamicallySizeForPercentage(
                volumeButton,
                actualWidth);
        }

        // Use the real classic SndVol executable instead of recreating its UI.
        // Parameters are intentionally user-controlled; the mod doesn't append
        // a hard-coded coordinate because current SndVol builds can ignore the
        // historical packed-position argument.
        auto volumeClickToken = volumeButton.Click(RoutedEventHandler(
            [](winrt::Windows::Foundation::IInspectable const& sender,
               RoutedEventArgs const&) {
                if (g_unloading.load(std::memory_order_acquire)) {
                    return;
                }
                auto element = sender.try_as<FrameworkElement>();
                if (!element) {
                    ModLog(L"Volume click sender isn't a FrameworkElement; refusing SndVol launch");
                    return;
                }
                LaunchNativeSndVol(element);
            }));

        // PowerOptions is expected to occupy the rightmost Grid column on the
        // tested builds. We still extend by two Power-sized columns, but the
        // final Grid.Column assignment for every system button is determined by
        // the user-configurable left-to-right order.
        auto columns = parentGrid.ColumnDefinitions();
        const uint32_t columnCount = columns.Size();
        ModLog(L"System grid column count=%u; PowerOptions column=%d",
               columnCount, column);

        if (column < 0 || static_cast<uint32_t>(column) >= columnCount ||
            static_cast<uint32_t>(column + 1) != columnCount) {
            ModLog(L"PowerOptions is not in the rightmost Grid column; refusing layout mutation");
            LogTree(root);
            return;
        }

        std::vector<ColumnSizingSnapshot> originalColumnSizing;
        originalColumnSizing.reserve(columns.Size());
        for (uint32_t i = 0; i < columns.Size(); ++i) {
            auto definition = columns.GetAt(i);
            originalColumnSizing.push_back(
                {definition.Width(),
                 definition.MinWidth(),
                 definition.MaxWidth()});
        }

        auto nativeGroups = DiscoverNativeSystemButtonGroups(
            parentGrid, powerButton, row, column);
        if (nativeGroups.empty()) {
            ModLog(L"Couldn't discover the contiguous native system-button strip; refusing layout mutation");
            LogTree(root);
            return;
        }
        LogNativeSystemButtonGroups(nativeGroups);
        LogNativeSystemButtonSemanticDiagnostics(nativeGroups);

        // Capture a complete-enough native layout snapshot before the first
        // mutation. Hot-disable can then remove our controls and return the
        // still-running LogonUI instance to its pre-injection state.
        auto session = std::make_shared<InjectionSession>();
        session->owner = winrt::make_weak(userControl);
        session->grid = winrt::make_weak(parentGrid);
        session->xamlThreadId = GetCurrentThreadId();
        session->ownerIdentity =
            reinterpret_cast<void*>(winrt::get_abi(userControl));
        session->active.store(true, std::memory_order_release);
        session->originalColumnCount = columnCount;
        session->originalColumns = originalColumnSizing;
        session->batteryButton = batteryButton;
        session->volumeButton = volumeButton;
        session->batteryClickToken = batteryClickToken;
        session->volumeClickToken = volumeClickToken;
        session->batteryClickAttached = true;
        session->volumeClickAttached = true;

        std::vector<void*> snappedNativeIdentities;
        for (const auto& group : nativeGroups) {
            for (const auto& element : group.elements) {
                if (!element) continue;
                void* identity =
                    reinterpret_cast<void*>(winrt::get_abi(element));
                if (std::find(snappedNativeIdentities.begin(),
                              snappedNativeIdentities.end(),
                              identity) != snappedNativeIdentities.end()) {
                    continue;
                }
                snappedNativeIdentities.push_back(identity);

                NativeElementSnapshot snapshot;
                snapshot.element = winrt::make_weak(element);
                snapshot.groupKey = group.key;
                snapshot.column = Controls::Grid::GetColumn(element);
                const bool groupEnabledAtInjection =
                    IsSystemButtonGroupEnabled(group.key);
                snapshot.restoreColumn = groupEnabledAtInjection;
                snapshot.restoreVisibility = !groupEnabledAtInjection;
                snapshot.restoreMargin =
                    group.key == L"ime" || group.key == L"input";
                snapshot.restoreSizing = snapshot.restoreMargin;
                snapshot.restoreTabState = true;
                snapshot.restoreImeVisualOffset = group.key == L"ime";
                try { snapshot.visibility = element.Visibility(); } catch (...) {}
                try { snapshot.margin = element.Margin(); } catch (...) {}
                try { snapshot.width = element.Width(); } catch (...) {}
                try { snapshot.minWidth = element.MinWidth(); } catch (...) {}
                try { snapshot.maxWidth = element.MaxWidth(); } catch (...) {}
                try {
                    if (auto control = element.try_as<Controls::Control>()) {
                        snapshot.isControl = true;
                        snapshot.isTabStop = control.IsTabStop();
                        snapshot.tabIndex = control.TabIndex();
                    }
                } catch (...) {
                }
                session->nativeElements.push_back(std::move(snapshot));
            }
        }

        TrackInjectionSession(session);
        try {
            auto weakSession = std::weak_ptr<InjectionSession>(session);
            session->ownerUnloadedToken = userControl.Unloaded(
                [weakSession](auto const&, auto const&) {
                    auto session = weakSession.lock();
                    if (!session) return;

                    // Unloaded is not terminal in LogonUI. The same native
                    // SystemProvider can be reinserted into a XAML tree after
                    // icon/flyout/security-surface transitions. Preserve all
                    // mod-owned state and let Loaded revalidate it.
                    if (g_unloading.load(std::memory_order_acquire)) {
                        ModLog(L"SystemProvider Unloaded during BeforeUninit; lifecycle state left untouched for owning-thread hot cleanup");
                        return;
                    }
                    session->active.store(false, std::memory_order_release);
                    ModLog(L"SystemProvider transiently Unloaded; retaining injection session and callbacks for reload");
                });
            session->ownerUnloadedAttached = true;

            session->ownerLoadedToken = userControl.Loaded(
                [weakSession](auto const&, auto const&) {
                    auto session = weakSession.lock();
                    if (!session ||
                        g_unloading.load(std::memory_order_acquire)) {
                        return;
                    }

                    RememberXamlThread(GetCurrentThreadId(), true);
                    auto owner = session->owner.get();
                    if (!owner || !IsSystemProviderUserControl(owner)) {
                        return;
                    }

                    try {
                        auto root = owner.Content().try_as<FrameworkElement>();
                        FrameworkElement power{nullptr};
                        if (root) {
                            power = FindByName(root, L"PowerOptions");
                        }
                        const bool batteryExpected =
                            g_showBatteryButton.load(std::memory_order_relaxed);
                        const bool volumeExpected =
                            g_showVolumeButton.load(std::memory_order_relaxed);
                        const bool batteryPresent = root &&
                            FindByName(root, kInjectedBatteryName);
                        const bool volumePresent = root &&
                            FindByName(root, kInjectedVolumeName);
                        const bool retainedVisualsValid =
                            power &&
                            (!batteryExpected || batteryPresent) &&
                            (!volumeExpected || volumePresent);

                        if (retainedVisualsValid) {
                            session->active.store(
                                true, std::memory_order_release);
                            ModLog(L"SystemProvider Loaded revalidated retained injection session on XAML thread=%lu",
                                   GetCurrentThreadId());
                            return;
                        }

                        ModLog(L"SystemProvider Loaded with rebuilt tree; retiring stale retained session and reinjecting");
                        CleanupInjectionSession(session, false, false);
                        InjectVisual(
                            reinterpret_cast<void*>(winrt::get_abi(owner)),
                            true);
                    } catch (const winrt::hresult_error& e) {
                        ModLog(L"SystemProvider Loaded revalidation failed: 0x%08X %s",
                               static_cast<unsigned>(e.code().value),
                               e.message().c_str());
                    } catch (...) {
                        ModLog(L"SystemProvider Loaded revalidation failed with unknown exception");
                    }
                });
            session->ownerLoadedAttached = true;
        } catch (...) {
            ModLog(L"Couldn't attach SystemProvider lifecycle revalidation callbacks");
        }

        int firstSystemColumn = nativeGroups.front().originalColumn;
        for (const auto& group : nativeGroups) {
            firstSystemColumn = std::min(firstSystemColumn, group.originalColumn);
        }

        auto sourceColumn = columns.GetAt(static_cast<uint32_t>(column));
        auto appendClonedColumn = [&](bool allowContentWidth) -> bool {
            Controls::ColumnDefinition newColumn;
            try {
                if (allowContentWidth) {
                    // Auto-width columns must be allowed to collapse to zero.
                    // The injected Button itself keeps the native minimum width
                    // while visible, so this also avoids a blank gap when
                    // Automatic Battery visibility collapses the control.
                    newColumn.Width(GridLengthHelper::Auto());
                    newColumn.MinWidth(0.0);
                } else {
                    newColumn.Width(sourceColumn.Width());
                    newColumn.MinWidth(sourceColumn.MinWidth());
                    newColumn.MaxWidth(sourceColumn.MaxWidth());
                }
                columns.Append(newColumn);
                return true;
            } catch (...) {
                return false;
            }
        };

        const bool showBattery =
            g_showBatteryButton.load(std::memory_order_relaxed);
        const bool showVolume =
            g_showVolumeButton.load(std::memory_order_relaxed);
        const bool showBatteryPercentage =
            g_showBatteryPercentage.load(std::memory_order_relaxed);
        const bool showVolumePercentage =
            g_showVolumePercentage.load(std::memory_order_relaxed);

        if (showBattery &&
            !appendClonedColumn(
                showBatteryPercentage ||
                g_batteryVisibilityAutomatic.load(std::memory_order_relaxed))) {
            ModLog(L"Couldn't create Battery column sizing; refusing layout mutation");
            CleanupInjectionSession(session, true, false);
            return;
        }
        if (showVolume &&
            !appendClonedColumn(showVolumePercentage)) {
            ModLog(L"Couldn't create Volume column sizing; refusing layout mutation");
            CleanupInjectionSession(session, true, false);
            return;
        }

        Controls::Grid::SetRow(batteryButton, row);
        Controls::Grid::SetRowSpan(batteryButton, rowSpan);
        Controls::Grid::SetColumnSpan(batteryButton, columnSpan);

        Controls::Grid::SetRow(volumeButton, row);
        Controls::Grid::SetRowSpan(volumeButton, rowSpan);
        Controls::Grid::SetColumnSpan(volumeButton, columnSpan);

        auto children = parentGrid.Children();

        std::vector<SystemButtonGroup> allGroups = nativeGroups;

        if (showBattery) {
            children.Append(batteryButton);

            SystemButtonGroup batteryGroup;
            batteryGroup.key = L"battery";
            batteryGroup.originalColumn = column;
            batteryGroup.elements.push_back(batteryButton);
            allGroups.push_back(std::move(batteryGroup));
        }

        if (showVolume) {
            children.Append(volumeButton);

            SystemButtonGroup volumeGroup;
            volumeGroup.key = L"volume";
            volumeGroup.originalColumn = column + (showBattery ? 1 : 0);
            volumeGroup.elements.push_back(volumeButton);
            allGroups.push_back(std::move(volumeGroup));
        }

        auto requestedOrder = ParseButtonOrderSetting(GetButtonOrderSetting());
        std::vector<size_t> orderedIndices;

        auto findGroup = [&](const std::wstring& key) -> size_t {
            for (size_t i = 0; i < allGroups.size(); ++i) {
                if (allGroups[i].key == key) return i;
            }
            return static_cast<size_t>(-1);
        };
        auto alreadyOrdered = [&](size_t index) {
            return std::find(orderedIndices.begin(), orderedIndices.end(), index) !=
                   orderedIndices.end();
        };

        std::vector<bool> groupEnabled(
            allGroups.size(), true);
        size_t enabledGroupCount = 0;
        for (size_t i = 0; i < allGroups.size(); ++i) {
            const auto& group = allGroups[i];
            bool enabled = true;

            if (group.key == L"battery") {
                enabled = showBattery;
            } else if (group.key == L"volume") {
                enabled = showVolume;
            } else {
                enabled = IsSystemButtonGroupEnabled(group.key);
            }

            groupEnabled[i] = enabled;
            if (enabled) {
                ++enabledGroupCount;
                continue;
            }

            for (const auto& element : group.elements) {
                if (!element) continue;
                try {
                    element.Visibility(Visibility::Collapsed);
                } catch (...) {
                }
                try {
                    if (auto control = element.try_as<Controls::Control>()) {
                        control.IsTabStop(false);
                    }
                } catch (...) {
                }
            }

            ModLog(L"System button hidden by setting: %s",
                   group.key.c_str());
        }

        for (const auto& key : requestedOrder) {
            const size_t index = findGroup(key);
            if (index == static_cast<size_t>(-1)) {
                if (key == L"ime" &&
                    findGroup(L"input") != static_cast<size_t>(-1)) {
                    ModLog(L"Standalone IME mode container isn't present on this LogonUI layout; current IME/input state is handled by InputSwitchUserControl, so the 'ime' order token is skipped");
                } else {
                    ModLog(L"Button-order token isn't present on this LogonUI instance: %s",
                           key.c_str());
                }
                continue;
            }
            if (!groupEnabled[index]) {
                continue;
            }
            if (!alreadyOrdered(index)) orderedIndices.push_back(index);
        }

        // A malformed/partial setting never hides a button. First honor the
        // requested named order exactly; then append anything that wasn't
        // explicitly named. Unknown native slots therefore can't jump ahead of
        // the user's battery/volume/network ordering.
        static constexpr const wchar_t* kFallbackOrder[] = {
            L"ime", L"input", L"battery", L"volume",
            L"network-signin", L"disconnect", L"network",
            L"accessibility", L"power"
        };
        for (const wchar_t* key : kFallbackOrder) {
            const size_t index = findGroup(key);
            if (index != static_cast<size_t>(-1) &&
                groupEnabled[index] &&
                !alreadyOrdered(index)) {
                orderedIndices.push_back(index);
            }
        }
        for (size_t i = 0; i < allGroups.size(); ++i) {
            if (groupEnabled[i] && !alreadyOrdered(i)) {
                orderedIndices.push_back(i);
            }
        }

        if (orderedIndices.size() != enabledGroupCount) {
            ModLog(L"Button-order internal mismatch; refusing column reassignment");
            CleanupInjectionSession(session, true, false);
            return;
        }

        auto applyColumnSizing =
            [&](uint32_t targetColumn,
                const SystemButtonGroup& group) {
                if (targetColumn >= columns.Size()) {
                    return;
                }

                auto target = columns.GetAt(targetColumn);

                const bool isBattery = group.key == L"battery";
                const bool isVolume = group.key == L"volume";
                const bool dynamicInjected =
                    (isBattery && showBatteryPercentage) ||
                    (isVolume && showVolumePercentage);
                const bool dynamicNative =
                    group.key == L"ime" || group.key == L"input" ||
                    group.key == L"network-signin" ||
                    group.key == L"disconnect";

                try {
                    if (dynamicInjected) {
                        target.Width(GridLengthHelper::Auto());
                        target.MinWidth(sourceColumn.MinWidth());
                        target.MaxWidth(
                            std::numeric_limits<double>::infinity());
                        return;
                    }

                    // Optional/lazy-loaded native controls must not reserve a
                    // blank 40-DIP slot while hidden, but must be able to grow
                    // when Windows materializes them later.
                    if (dynamicNative) {
                        target.Width(GridLengthHelper::Auto());
                        target.MinWidth(0.0);
                        target.MaxWidth(
                            std::numeric_limits<double>::infinity());
                        return;
                    }

                    if (!isBattery && !isVolume &&
                        group.originalColumn >= 0 &&
                        static_cast<size_t>(group.originalColumn) <
                            originalColumnSizing.size()) {
                        const auto& snapshot =
                            originalColumnSizing[
                                static_cast<size_t>(
                                    group.originalColumn)];
                        target.Width(snapshot.width);
                        target.MinWidth(snapshot.minWidth);
                        target.MaxWidth(snapshot.maxWidth);
                        return;
                    }

                    target.Width(sourceColumn.Width());
                    target.MinWidth(sourceColumn.MinWidth());
                    target.MaxWidth(sourceColumn.MaxWidth());
                } catch (...) {
                }
            };

        for (size_t slot = 0; slot < orderedIndices.size(); ++slot) {
            const auto& group = allGroups[orderedIndices[slot]];
            const int targetColumn =
                firstSystemColumn + static_cast<int>(slot);
            for (const auto& element : group.elements) {
                AllowLazyNativeContainerToDynamicallySize(
                    element, group.key);
                Controls::Grid::SetColumn(element, targetColumn);
            }
            applyColumnSizing(
                static_cast<uint32_t>(targetColumn), group);
            ModLog(L"Button order applied: %s -> col=%d (elementCount=%u)",
                   group.key.c_str(),
                   targetColumn,
                   static_cast<unsigned>(group.elements.size()));
        }

        // Collapse every now-unused slot at the right edge of the system
        // strip, so hiding native controls removes their layout space instead
        // of leaving a blank Power-sized gap.
        const uint32_t usedEnd =
            static_cast<uint32_t>(
                firstSystemColumn +
                static_cast<int>(orderedIndices.size()));
        for (uint32_t colIndex = usedEnd;
             colIndex < columns.Size();
             ++colIndex) {
            auto definition = columns.GetAt(colIndex);
            try {
                definition.MinWidth(0.0);
                definition.MaxWidth(0.0);
                definition.Width(GridLengthHelper::FromPixels(0.0));
            } catch (...) {
            }
        }

        // Keep Windows-owned XAML elements attached to their parent. Earlier
        // versions reordered the Children collection with RemoveAt/InsertAt to
        // influence Tab traversal. On a persistent hot-loaded LogonUI that
        // transiently fires native Unloaded/Loaded handlers and can make
        // Windows-owned buttons disappear. Use TabIndex in-place instead.
        for (size_t slot = 0; slot < orderedIndices.size(); ++slot) {
            const auto& group = allGroups[orderedIndices[slot]];
            for (const auto& element : group.elements) {
                try {
                    if (auto control = element.try_as<Controls::Control>()) {
                        control.TabIndex(static_cast<int>(slot));
                    }
                } catch (...) {
                }
            }
        }
        ModLog(L"Native TabIndex aligned with configured visual order without reparenting Windows-owned controls");

        // IME/Input are native UserControl containers that normally sit at the
        // left edge of the Windows strip and therefore don't carry the ordinary
        // left gap themselves. If the user moves one after a different visible
        // control, apply the configured external spacing only while rendered.
        // When IME and Input are adjacent to each other, keep their native pair
        // spacing (no second injected gap between the two containers). Dormant
        // containers remain margin-free so they still collapse completely.
        if (!orderedIndices.empty()) {
            const int configuredImeInputSpacing =
                g_nativeImeInputSpacing.load(std::memory_order_relaxed);
            const double resolvedImeInputSpacing =
                configuredImeInputSpacing < 0
                    ? std::max(0.0, margin.Left)
                    : static_cast<double>(configuredImeInputSpacing);

            ModLog(
                L"Resolved IME/Input external spacing: %.1f DIP (%s; native Power left margin=%.1f)",
                resolvedImeInputSpacing,
                configuredImeInputSpacing < 0 ? L"automatic" : L"explicit",
                margin.Left);

            AttachDynamicLazyNativeSpacing(
                userControl,
                parentGrid,
                firstSystemColumn,
                static_cast<int>(usedEnd),
                resolvedImeInputSpacing,
                session);
        }

        // Attach routed input handlers only after the child-order rewrite.
        // RemoveAt/InsertAt transiently unloads the injected Button, so an
        // earlier Unloaded cleanup would otherwise detach the Battery wheel
        // handler before the user can ever scroll it.
        if (showBattery) {
            session->batteryWheel =
                AttachBatteryBrightnessWheelHandler(batteryButton);
        }

        if (showVolume) {
            session->volumeState = StartVolumeMonitor(
                userControl,
                volumeButton,
                volumeIcon,
                volumePercentText,
                volumeToolTip,
                noEndpointGlyph,
                nativeSndVol);
        }

        if (!orderedIndices.empty()) {
            ModLog(L"Inserted configurable Battery/Volume controls; battery=%d volume=%d visible system strip spans cols=%d..%d",
                   showBattery ? 1 : 0,
                   showVolume ? 1 : 0,
                   firstSystemColumn,
                   firstSystemColumn +
                       static_cast<int>(orderedIndices.size()) - 1);
        } else {
            ModLog(L"All configurable system buttons are hidden; system strip columns collapsed");
        }

        if (g_enableXamlCapture.load(std::memory_order_relaxed)) {
            if (showBattery) {
                ScheduleXamlSubtreeCapture(userControl, batteryImage);
            } else {
                ModLog(L"Diagnostic XAML capture skipped because Battery button is hidden");
            }
        } else {
            ModLog(L"Diagnostic XAML capture disabled");
        }

        // Windows 11 can materialize the Input/language control only after a
        // credential field is activated. Attach the dynamic diagnostic watcher
        // only for verbose RE logging or XAML capture; functional IME/Input
        // spacing has its own independent watcher.
        AttachDynamicSystemProviderDiagnostics(
            userControl, batteryImage, session);
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Injection failed: 0x%08X %s",
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"Injection failed with unknown exception");
    }
}

struct LateAttachWindowCandidate {
    HWND hwnd = nullptr;
    DWORD threadId = 0;
    unsigned long long area = 0;
};

BOOL CALLBACK EnumLateAttachWindow(HWND hwnd, LPARAM lParam) {
    auto* candidates =
        reinterpret_cast<std::vector<LateAttachWindowCandidate>*>(lParam);
    if (!candidates) {
        return TRUE;
    }

    DWORD processId = 0;
    DWORD threadId = GetWindowThreadProcessId(hwnd, &processId);
    if (!threadId || processId != GetCurrentProcessId()) {
        return TRUE;
    }

    RECT rect{};
    unsigned long long area = 0;
    if (GetWindowRect(hwnd, &rect)) {
        const LONG width = rect.right - rect.left;
        const LONG height = rect.bottom - rect.top;
        if (width > 0 && height > 0) {
            area = static_cast<unsigned long long>(width) *
                   static_cast<unsigned long long>(height);
        }
    }

    auto it = std::find_if(
        candidates->begin(), candidates->end(),
        [threadId](const LateAttachWindowCandidate& candidate) {
            return candidate.threadId == threadId;
        });
    if (it == candidates->end()) {
        candidates->push_back({hwnd, threadId, area});
    } else if (!it->hwnd || area > it->area) {
        it->hwnd = hwnd;
        it->area = area;
    }

    return TRUE;
}

std::vector<LateAttachWindowCandidate> CollectUiThreadCandidates(
        bool includeKnownXamlThreads) {
    std::vector<LateAttachWindowCandidate> candidates;
    EnumWindows(EnumLateAttachWindow,
                reinterpret_cast<LPARAM>(&candidates));

    if (includeKnownXamlThreads) {
        for (DWORD threadId : SnapshotKnownXamlThreads()) {
            auto it = std::find_if(
                candidates.begin(), candidates.end(),
                [threadId](const LateAttachWindowCandidate& candidate) {
                    return candidate.threadId == threadId;
                });
            if (it == candidates.end()) {
                candidates.push_back({nullptr, threadId, 0});
            }
        }
    }

    const DWORD preferred =
        g_xamlUiThreadId.load(std::memory_order_acquire);
    std::stable_sort(
        candidates.begin(), candidates.end(),
        [preferred](const LateAttachWindowCandidate& a,
                    const LateAttachWindowCandidate& b) {
            const bool aPreferred = preferred && a.threadId == preferred;
            const bool bPreferred = preferred && b.threadId == preferred;
            if (aPreferred != bPreferred) return aPreferred;
            return a.area > b.area;
        });
    return candidates;
}

struct UiThreadRendezvousPostResult {
    bool postedAny = false;
    unsigned hwndPosts = 0;
    unsigned hwndSends = 0;
    bool threadPost = false;
    HWND activeHwnd = nullptr;
    HWND focusHwnd = nullptr;
    DWORD lastError = ERROR_SUCCESS;
};

bool IsWindowOnCurrentProcessThread(HWND hwnd, DWORD threadId) {
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }

    DWORD processId = 0;
    const DWORD ownerThreadId =
        GetWindowThreadProcessId(hwnd, &processId);
    return ownerThreadId == threadId &&
           processId == GetCurrentProcessId();
}

void AppendUniqueRendezvousWindow(
        std::vector<HWND>* windows,
        HWND hwnd,
        DWORD threadId) {
    if (!windows ||
        !IsWindowOnCurrentProcessThread(hwnd, threadId)) {
        return;
    }

    if (std::find(windows->begin(), windows->end(), hwnd) ==
        windows->end()) {
        windows->push_back(hwnd);
    }
}

UiThreadRendezvousPostResult PostUiThreadRendezvous(
        DWORD threadId,
        HWND fallbackHwnd,
        UINT message,
        WPARAM cookie) {
    UiThreadRendezvousPostResult result{};
    if (!threadId || !message) {
        result.lastError = ERROR_INVALID_PARAMETER;
        return result;
    }

    std::vector<HWND> windows;
    GUITHREADINFO guiInfo{};
    guiInfo.cbSize = sizeof(guiInfo);
    if (GetGUIThreadInfo(threadId, &guiInfo)) {
        result.activeHwnd = guiInfo.hwndActive;
        result.focusHwnd = guiInfo.hwndFocus;

        // A nested language/IME selector can temporarily pump only the
        // currently active/focused HWND. Put those first, then the normal
        // top-level window discovered by EnumWindows.
        AppendUniqueRendezvousWindow(
            &windows, guiInfo.hwndActive, threadId);
        AppendUniqueRendezvousWindow(
            &windows, guiInfo.hwndFocus, threadId);
    }
    AppendUniqueRendezvousWindow(
        &windows, fallbackHwnd, threadId);

    for (HWND hwnd : windows) {
        if (PostMessageW(hwnd, message, cookie, 0)) {
            result.postedAny = true;
            ++result.hwndPosts;
        } else {
            result.lastError = GetLastError();
        }

        // Also send a nonqueued notification. A XAML language/IME selector can
        // run a nested pump which keeps servicing sent messages while delaying
        // or filtering ordinary queued messages. WH_CALLWNDPROC below catches
        // this path before the native HWND procedure sees the private message.
        if (SendNotifyMessageW(hwnd, message, cookie, 0)) {
            result.postedAny = true;
            ++result.hwndSends;
        } else {
            result.lastError = GetLastError();
        }
    }

    // Also fan out to the thread queue. The hook consumes the first matching
    // rendezvous only; duplicate registered messages are harmless afterward.
    // This retains the old path for UI loops that don't filter thread messages.
    if (PostThreadMessageW(threadId, message, cookie, 0)) {
        result.postedAny = true;
        result.threadPost = true;
    } else {
        result.lastError = GetLastError();
    }

    return result;
}

Controls::UserControl FindSystemProviderFromPowerButton(
        FrameworkElement const& powerElement) {
    try {
        DependencyObject current = powerElement;
        while (current) {
            if (auto control = current.try_as<Controls::UserControl>()) {
                if (IsSystemProviderUserControl(control)) {
                    return control;
                }
            }
            current = Media::VisualTreeHelper::GetParent(current);
        }
    } catch (...) {
    }

    return nullptr;
}

bool LateAttachExistingSystemProviderOnUiThread() {
    const DWORD currentThreadId = GetCurrentThreadId();
    ModLog(L"Late-attach rendezvous probing UI thread=%lu",
           currentThreadId);

    if (g_unloading.load(std::memory_order_acquire)) {
        return false;
    }

    try {
        auto window = Window::Current();
        if (!window) {
            ModLog(L"Late-attach: Window::Current is null on thread=%lu",
                   currentThreadId);
            return false;
        }

        auto root = window.Content().try_as<FrameworkElement>();
        if (!root) {
            ModLog(L"Late-attach: current XAML Window has no FrameworkElement content on thread=%lu",
                   currentThreadId);
            return false;
        }

        // Remember every positively identified XAML Window thread, not merely
        // whichever current-process HWND happens to be largest at this moment.
        RememberXamlThread(currentThreadId, false);

        auto powerElement = FindByName(root, L"PowerOptions");
        if (!powerElement) {
            ModLog(L"Late-attach: thread=%lu has a XAML Window but no PowerOptions/SystemProvider tree",
                   currentThreadId);
            return false;
        }

        auto userControl = FindSystemProviderFromPowerButton(powerElement);
        if (!userControl || !IsSystemProviderUserControl(userControl)) {
            ModLog(L"Late-attach: thread=%lu has PowerOptions but no validated LogonUX.SystemProviderUserControl ancestor",
                   currentThreadId);
            return false;
        }

        RememberXamlThread(currentThreadId, true);
        ModLog(L"Late-attach: validated existing SystemProvider on thread=%lu class=LogonUX.SystemProviderUserControl",
               currentThreadId);

        // Reuse the normal injection path. It handles retained-session
        // revalidation, realized controls, and lazy PowerOptions templates.
        InjectVisual(
            reinterpret_cast<void*>(winrt::get_abi(userControl)), true);
        return true;
    } catch (const winrt::hresult_error& e) {
        ModLog(L"Late-attach probe failed on thread=%lu: 0x%08X %s",
               currentThreadId,
               static_cast<unsigned>(e.code().value),
               e.message().c_str());
    } catch (...) {
        ModLog(L"Late-attach probe failed on thread=%lu with unknown exception",
               currentThreadId);
    }
    return false;
}

bool TryHandleLateAttachRendezvous(UINT message, WPARAM wParam) {
    if (!g_lateAttachPending.load(std::memory_order_acquire) ||
        message != g_lateAttachMessage ||
        GetCurrentThreadId() !=
            g_lateAttachProbeThreadId.load(std::memory_order_acquire) ||
        static_cast<ULONG_PTR>(wParam) !=
            g_lateAttachProbeCookie.load(std::memory_order_acquire)) {
        return false;
    }

    bool expected = true;
    if (!g_lateAttachPending.compare_exchange_strong(
            expected, false, std::memory_order_acq_rel)) {
        return false;
    }

    const bool matched = LateAttachExistingSystemProviderOnUiThread();
    g_lateAttachProbeMatched.store(matched, std::memory_order_release);
    if (g_lateAttachCompletedEvent) {
        SetEvent(g_lateAttachCompletedEvent);
    }
    return true;
}

void DetachLateAttachRendezvousHooks() {
    if (HHOOK hook = g_lateAttachHook.exchange(nullptr)) {
        UnhookWindowsHookEx(hook);
    }
    if (HHOOK hook = g_lateAttachCallWndHook.exchange(nullptr)) {
        UnhookWindowsHookEx(hook);
    }
}

LRESULT CALLBACK LateAttachGetMsgProc(int code, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const MSG*>(lParam);
        handled = TryHandleLateAttachRendezvous(
            message->message, message->wParam);
    }

    const LRESULT next = CallNextHookEx(nullptr, code, wParam, lParam);
    if (handled) {
        DetachLateAttachRendezvousHooks();
    }
    return next;
}

LRESULT CALLBACK LateAttachCallWndProc(
        int code, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    if (code >= 0 && lParam) {
        const auto* message =
            reinterpret_cast<const CWPSTRUCT*>(lParam);
        handled = TryHandleLateAttachRendezvous(
            message->message, message->wParam);
    }

    const LRESULT next = CallNextHookEx(nullptr, code, wParam, lParam);
    if (handled) {
        DetachLateAttachRendezvousHooks();
    }
    return next;
}

void RequestLateAttachExistingSystemProvider() {
    if (g_unloading.load(std::memory_order_acquire)) {
        return;
    }
    if (!g_lateAttachMessage) {
        g_lateAttachMessage = RegisterWindowMessageW(
            L"Windhawk.LogonUI.SignInControlsLab.LateAttach.v4");
        if (!g_lateAttachMessage) {
            ModLog(L"Late-attach: RegisterWindowMessage failed: %lu",
                   GetLastError());
            return;
        }
    }
    if (!g_lateAttachCompletedEvent) {
        g_lateAttachCompletedEvent =
            CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_lateAttachCompletedEvent) {
            ModLog(L"Late-attach: CreateEvent failed: %lu",
                   GetLastError());
            return;
        }
    }

    // A Windows 10 language/IME selector can keep the SystemProvider's XAML
    // thread inside a nested message pump for several seconds after dismissal.
    // Retry long enough to bridge that transient state, but keep every delay
    // cancellable through g_stopEvent so hot-disable remains responsive.
    constexpr DWORD kRetryDelayMs[] = {
        0, 500, 1000, 2000, 4000, 5000
    };
    constexpr DWORD kProbeWaitMs = 750;

    unsigned totalProbed = 0;
    unsigned totalMatches = 0;

    for (unsigned pass = 0;
         pass < ARRAYSIZE(kRetryDelayMs);
         ++pass) {
        if (pass != 0) {
            const DWORD delay = kRetryDelayMs[pass];
            ModLog(L"Late-attach: no validated SystemProvider yet; retry pass %u/%u in %lu ms",
                   pass + 1,
                   static_cast<unsigned>(ARRAYSIZE(kRetryDelayMs)),
                   delay);

            if (g_stopEvent &&
                WaitForSingleObject(g_stopEvent, delay) ==
                    WAIT_OBJECT_0) {
                break;
            }
            if (g_unloading.load(std::memory_order_acquire)) {
                break;
            }
        }

        auto candidates = CollectUiThreadCandidates(true);
        if (candidates.empty()) {
            ModLog(L"Late-attach: no current-process UI thread candidates found on pass %u; _OnLoaded hook remains armed",
                   pass + 1);
            continue;
        }

        unsigned passProbed = 0;
        unsigned passMatches = 0;

        for (const auto& candidate : candidates) {
            if (!candidate.threadId ||
                g_unloading.load(std::memory_order_acquire)) {
                continue;
            }

            ResetEvent(g_lateAttachCompletedEvent);
            g_lateAttachProbeMatched.store(
                false, std::memory_order_release);
            g_lateAttachProbeThreadId.store(
                candidate.threadId, std::memory_order_release);
            g_lateAttachPending.store(
                true, std::memory_order_release);

            const ULONG_PTR cookie =
                g_lateAttachCookieCounter.fetch_add(
                    1, std::memory_order_acq_rel);
            g_lateAttachProbeCookie.store(
                cookie, std::memory_order_release);

            HHOOK getMsgHook = SetWindowsHookExW(
                WH_GETMESSAGE,
                LateAttachGetMsgProc,
                nullptr,
                candidate.threadId);
            HHOOK callWndHook = SetWindowsHookExW(
                WH_CALLWNDPROC,
                LateAttachCallWndProc,
                nullptr,
                candidate.threadId);
            if (!getMsgHook && !callWndHook) {
                g_lateAttachPending.store(
                    false, std::memory_order_release);
                ModLog(L"Late-attach: couldn't install GETMESSAGE/CALLWNDPROC hooks for candidate thread=%lu: %lu",
                       candidate.threadId, GetLastError());
                continue;
            }

            if (HHOOK oldHook =
                    g_lateAttachHook.exchange(getMsgHook)) {
                UnhookWindowsHookEx(oldHook);
            }
            if (HHOOK oldHook =
                    g_lateAttachCallWndHook.exchange(callWndHook)) {
                UnhookWindowsHookEx(oldHook);
            }

            const auto post = PostUiThreadRendezvous(
                candidate.threadId,
                candidate.hwnd,
                g_lateAttachMessage,
                cookie);
            if (!post.postedAny) {
                g_lateAttachPending.store(
                    false, std::memory_order_release);
                DetachLateAttachRendezvousHooks();
                ModLog(L"Late-attach: couldn't post rendezvous to candidate thread=%lu: %lu",
                       candidate.threadId, post.lastError);
                continue;
            }

            ++passProbed;
            ++totalProbed;
            ModLog(L"Late-attach: probing candidate pass=%u hwnd=%p thread=%lu fanout(postedHwnd=%u sentHwnd=%u thread=%d active=%p focus=%p)",
                   pass + 1,
                   candidate.hwnd,
                   candidate.threadId,
                   post.hwndPosts,
                   post.hwndSends,
                   post.threadPost ? 1 : 0,
                   post.activeHwnd,
                   post.focusHwnd);

            HANDLE waitHandles[2] = {
                g_lateAttachCompletedEvent,
                g_stopEvent
            };
            const DWORD waitCount = g_stopEvent ? 2 : 1;
            const DWORD wait = WaitForMultipleObjects(
                waitCount,
                waitHandles,
                FALSE,
                kProbeWaitMs);

            if (wait == WAIT_OBJECT_0) {
                if (g_lateAttachProbeMatched.load(
                        std::memory_order_acquire)) {
                    ++passMatches;
                    ++totalMatches;
                }
            } else {
                g_lateAttachPending.store(
                    false, std::memory_order_release);
                DetachLateAttachRendezvousHooks();

                if (g_stopEvent &&
                    wait == WAIT_OBJECT_0 + 1) {
                    g_lateAttachProbeThreadId.store(
                        0, std::memory_order_release);
                    ModLog(L"Late-attach: stop requested during rendezvous scan");
                    return;
                }

                ModLog(L"Late-attach: candidate thread=%lu did not service rendezvous in %lu ms on pass %u; continuing scan",
                       candidate.threadId,
                       kProbeWaitMs,
                       pass + 1);
            }
        }

        g_lateAttachProbeThreadId.store(
            0, std::memory_order_release);

        ModLog(L"Late-attach pass %u complete: probed=%u validatedSystemProviderThreads=%u",
               pass + 1, passProbed, passMatches);

        if (passMatches != 0) {
            break;
        }
    }

    g_lateAttachPending.store(false, std::memory_order_release);
    g_lateAttachProbeThreadId.store(0, std::memory_order_release);
    DetachLateAttachRendezvousHooks();

    ModLog(L"Late-attach recovery complete: totalProbed=%u validatedSystemProviderThreads=%u; _OnLoaded hook remains armed",
           totalProbed, totalMatches);
}

bool QueueHotCleanupFenceOnUiThread() {
    HANDLE fenceEvent = g_hotCleanupFenceEvent;
    if (!fenceEvent) {
        return false;
    }

    g_hotCleanupFenceCountdown.store(0, std::memory_order_release);
    auto makeFenceCallback = [fenceEvent]() {
        return [fenceEvent]() {
            if (g_hotCleanupFenceCountdown.fetch_sub(
                    1, std::memory_order_acq_rel) == 1) {
                SetEvent(fenceEvent);
            }
        };
    };

    bool queuedAny = false;

    // Fence both scheduling mechanisms. Automatic normally uses
    // DispatcherQueue on current builds, but CoreDispatcher is an explicit
    // advanced option and can have its own already-queued callbacks.
    try {
        auto queue = System::DispatcherQueue::GetForCurrentThread();
        if (queue) {
            g_hotCleanupFenceCountdown.fetch_add(
                1, std::memory_order_acq_rel);
            if (queue.TryEnqueue(makeFenceCallback())) {
                queuedAny = true;
            } else {
                g_hotCleanupFenceCountdown.fetch_sub(
                    1, std::memory_order_acq_rel);
            }
        }
    } catch (...) {
    }

    try {
        auto window = Window::Current();
        if (window) {
            auto dispatcher = window.Dispatcher();
            if (dispatcher) {
                g_hotCleanupFenceCountdown.fetch_add(
                    1, std::memory_order_acq_rel);
                try {
                    dispatcher.RunAsync(
                        Core::CoreDispatcherPriority::Normal,
                        makeFenceCallback());
                    queuedAny = true;
                } catch (...) {
                    g_hotCleanupFenceCountdown.fetch_sub(
                        1, std::memory_order_acq_rel);
                }
            }
        }
    } catch (...) {
    }

    if (!queuedAny ||
        g_hotCleanupFenceCountdown.load(std::memory_order_acquire) == 0) {
        SetEvent(fenceEvent);
        return false;
    }

    return true;
}

void SweepOrphanInjectedVisualsOnCurrentXamlThread() {
    try {
        auto window = Window::Current();
        if (!window) return;

        auto root = window.Content().try_as<FrameworkElement>();
        if (!root) return;

        auto power = FindByName(root, L"PowerOptions");
        if (!power) return;
        auto provider = FindSystemProviderFromPowerButton(power);
        if (!provider || !IsSystemProviderUserControl(provider)) return;

        RememberXamlThread(GetCurrentThreadId(), true);

        unsigned removed = 0;
        auto removeNamed = [&](const wchar_t* name) {
            auto element = FindByName(root, name);
            if (!element) return;
            try {
                auto parent = Media::VisualTreeHelper::GetParent(element)
                                  .try_as<Controls::Grid>();
                if (!parent) return;
                auto children = parent.Children();
                uint32_t index = 0;
                if (children.IndexOf(element, index)) {
                    children.RemoveAt(index);
                    ++removed;
                }
            } catch (...) {
            }
        };

        removeNamed(kInjectedBatteryName);
        removeNamed(kInjectedVolumeName);
        if (removed) {
            ModLog(L"Hot-unload orphan sweep removed %u injected control(s) on XAML thread=%lu",
                   removed, GetCurrentThreadId());
        }
    } catch (...) {
    }
}

void HotCleanupOnUiThread() {
    const DWORD currentThreadId = GetCurrentThreadId();
    ModLog(L"Hot-unload cleanup running on LogonUI UI/XAML thread=%lu",
           currentThreadId);

    StopOutstandingXamlCaptureWaitOnUiThread();
    DetachLayoutRetriesOnCurrentThread();
    CleanupInjectionSessionsOnCurrentThread(true);
    SweepOrphanInjectedVisualsOnCurrentXamlThread();

    // The bridge window is thread-affine. Destroy it only from its owning
    // thread; RequestHotCleanupOnUiThread explicitly includes that thread.
    HWND bridge = g_batteryUiBridgeWindow.load(std::memory_order_acquire);
    if (bridge &&
        GetWindowThreadProcessId(bridge, nullptr) == currentThreadId) {
        HWND expected = bridge;
        if (g_batteryUiBridgeWindow.compare_exchange_strong(
                expected, nullptr, std::memory_order_acq_rel)) {
            DestroyWindow(bridge);
        }
    }

    // Weak refs themselves are mod-owned bookkeeping. The actual XAML event
    // handlers and controls above have already been detached on this thread.
    g_batteryPercentText = {};

    QueueHotCleanupFenceOnUiThread();
}

bool TryHandleHotCleanupRendezvous(UINT message, WPARAM wParam) {
    if (!g_hotCleanupPending.load(std::memory_order_acquire) ||
        message != g_hotCleanupMessage ||
        GetCurrentThreadId() !=
            g_hotCleanupProbeThreadId.load(std::memory_order_acquire) ||
        static_cast<ULONG_PTR>(wParam) !=
            g_hotCleanupProbeCookie.load(std::memory_order_acquire)) {
        return false;
    }

    bool expected = true;
    if (!g_hotCleanupPending.compare_exchange_strong(
            expected, false, std::memory_order_acq_rel)) {
        return false;
    }

    HotCleanupOnUiThread();
    if (g_hotCleanupCompletedEvent) {
        SetEvent(g_hotCleanupCompletedEvent);
    }
    return true;
}

void DetachHotCleanupRendezvousHooks() {
    if (HHOOK hook = g_hotCleanupHook.exchange(nullptr)) {
        UnhookWindowsHookEx(hook);
    }
    if (HHOOK hook = g_hotCleanupCallWndHook.exchange(nullptr)) {
        UnhookWindowsHookEx(hook);
    }
}

LRESULT CALLBACK HotCleanupGetMsgProc(
        int code, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const MSG*>(lParam);
        handled = TryHandleHotCleanupRendezvous(
            message->message, message->wParam);
    }

    const LRESULT next = CallNextHookEx(nullptr, code, wParam, lParam);
    if (handled) {
        DetachHotCleanupRendezvousHooks();
    }
    return next;
}

LRESULT CALLBACK HotCleanupCallWndProc(
        int code, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    if (code >= 0 && lParam) {
        const auto* message =
            reinterpret_cast<const CWPSTRUCT*>(lParam);
        handled = TryHandleHotCleanupRendezvous(
            message->message, message->wParam);
    }

    const LRESULT next = CallNextHookEx(nullptr, code, wParam, lParam);
    if (handled) {
        DetachHotCleanupRendezvousHooks();
    }
    return next;
}

struct HotCleanupTarget {
    DWORD threadId = 0;
    HWND hwnd = nullptr;
    bool required = false;
};

void AddHotCleanupTarget(
        std::vector<HotCleanupTarget>* targets,
        DWORD threadId,
        HWND hwnd,
        bool required) {
    if (!targets || !threadId) return;
    auto it = std::find_if(
        targets->begin(), targets->end(),
        [threadId](const HotCleanupTarget& target) {
            return target.threadId == threadId;
        });
    if (it == targets->end()) {
        targets->push_back({threadId, hwnd, required});
        return;
    }
    if (!it->hwnd && hwnd) it->hwnd = hwnd;
    it->required = it->required || required;
}

std::vector<HotCleanupTarget> CollectHotCleanupTargets() {
    std::vector<HotCleanupTarget> targets;

    // Probe every XAML/UI thread discovered during the multi-thread late
    // attach scan so orphaned named controls cannot survive on a secondary
    // XAML Window.
    for (const auto& candidate : CollectUiThreadCandidates(true)) {
        AddHotCleanupTarget(
            &targets, candidate.threadId, candidate.hwnd, false);
    }

    // Threads which own actual mod callbacks/state are mandatory rendezvous
    // targets. They must be cleaned before Windhawk can unload this DLL.
    for (DWORD threadId : SnapshotInjectionSessionThreads()) {
        AddHotCleanupTarget(&targets, threadId, nullptr, true);
    }
    for (DWORD threadId : SnapshotLayoutRetryThreads()) {
        AddHotCleanupTarget(&targets, threadId, nullptr, true);
    }
    if (DWORD captureThreadId = GetOutstandingXamlCaptureThreadId()) {
        AddHotCleanupTarget(&targets, captureThreadId, nullptr, true);
    }
    if (HWND bridge =
            g_batteryUiBridgeWindow.load(std::memory_order_acquire)) {
        DWORD bridgeThreadId = GetWindowThreadProcessId(bridge, nullptr);
        AddHotCleanupTarget(&targets, bridgeThreadId, bridge, true);
    }

    const DWORD preferred =
        g_xamlUiThreadId.load(std::memory_order_acquire);
    std::stable_sort(
        targets.begin(), targets.end(),
        [preferred](const HotCleanupTarget& a,
                    const HotCleanupTarget& b) {
            const bool aPreferred = preferred && a.threadId == preferred;
            const bool bPreferred = preferred && b.threadId == preferred;
            if (aPreferred != bPreferred) return aPreferred;
            if (a.required != b.required) return a.required;
            return a.threadId < b.threadId;
        });
    return targets;
}

bool RequestHotCleanupOnUiThread() {
    if (!g_hotCleanupMessage) {
        g_hotCleanupMessage = RegisterWindowMessageW(
            L"Windhawk.LogonUI.BatteryVolumeControls.HotCleanup.v3");
        if (!g_hotCleanupMessage) {
            ModLog(L"Hot-unload: RegisterWindowMessage failed: %lu",
                   GetLastError());
            return false;
        }
    }

    auto targets = CollectHotCleanupTargets();
    if (targets.empty()) {
        ModLog(L"Hot-unload: no tracked XAML/UI threads or live UI state remain");
        return true;
    }

    g_hotCleanupCompletedEvent =
        CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_hotCleanupFenceEvent =
        CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_hotCleanupCompletedEvent || !g_hotCleanupFenceEvent) {
        ModLog(L"Hot-unload: couldn't create rendezvous events: %lu",
               GetLastError());
        if (g_hotCleanupCompletedEvent) {
            CloseHandle(g_hotCleanupCompletedEvent);
            g_hotCleanupCompletedEvent = nullptr;
        }
        if (g_hotCleanupFenceEvent) {
            CloseHandle(g_hotCleanupFenceEvent);
            g_hotCleanupFenceEvent = nullptr;
        }
        return false;
    }

    bool allRequiredTargetsCleaned = true;
    unsigned cleanedTargets = 0;

    for (const auto& target : targets) {
        if (!target.threadId) continue;

        ResetEvent(g_hotCleanupCompletedEvent);
        ResetEvent(g_hotCleanupFenceEvent);
        g_hotCleanupFenceCountdown.store(0, std::memory_order_release);
        g_hotCleanupProbeThreadId.store(
            target.threadId, std::memory_order_release);

        if (GetCurrentThreadId() == target.threadId) {
            ModLog(L"Hot-unload: BeforeUninit is already on target UI thread=%lu; running direct detach",
                   target.threadId);
            HotCleanupOnUiThread();
            ++cleanedTargets;
            // A same-thread dispatcher fence cannot execute while
            // BeforeUninit is blocking the dispatcher. State is detached, but
            // conservatively report incomplete confirmation for required state.
            if (target.required) {
                allRequiredTargetsCleaned = false;
            }
            continue;
        }

        const ULONG_PTR cookie =
            g_hotCleanupCookieCounter.fetch_add(
                1, std::memory_order_acq_rel);
        g_hotCleanupProbeCookie.store(
            cookie, std::memory_order_release);

        HHOOK getMsgHook = SetWindowsHookExW(
            WH_GETMESSAGE,
            HotCleanupGetMsgProc,
            nullptr,
            target.threadId);
        HHOOK callWndHook = SetWindowsHookExW(
            WH_CALLWNDPROC,
            HotCleanupCallWndProc,
            nullptr,
            target.threadId);
        if (!getMsgHook && !callWndHook) {
            ModLog(L"Hot-unload: couldn't install GETMESSAGE/CALLWNDPROC hooks for thread=%lu required=%d: %lu",
                   target.threadId, target.required ? 1 : 0,
                   GetLastError());
            if (target.required) allRequiredTargetsCleaned = false;
            continue;
        }

        if (HHOOK oldHook = g_hotCleanupHook.exchange(getMsgHook)) {
            UnhookWindowsHookEx(oldHook);
        }
        if (HHOOK oldHook =
                g_hotCleanupCallWndHook.exchange(callWndHook)) {
            UnhookWindowsHookEx(oldHook);
        }
        g_hotCleanupPending.store(true, std::memory_order_release);

        const auto post = PostUiThreadRendezvous(
            target.threadId,
            target.hwnd,
            g_hotCleanupMessage,
            cookie);
        if (!post.postedAny) {
            g_hotCleanupPending.store(false, std::memory_order_release);
            DetachHotCleanupRendezvousHooks();
            ModLog(L"Hot-unload: couldn't post cleanup rendezvous to thread=%lu required=%d: %lu",
                   target.threadId, target.required ? 1 : 0,
                   post.lastError);
            if (target.required) allRequiredTargetsCleaned = false;
            continue;
        }

        ModLog(L"Hot-unload: rendezvous posted to thread=%lu required=%d fanout(postedHwnd=%u sentHwnd=%u thread=%d active=%p focus=%p fallback=%p)",
               target.threadId,
               target.required ? 1 : 0,
               post.hwndPosts,
               post.hwndSends,
               post.threadPost ? 1 : 0,
               post.activeHwnd,
               post.focusHwnd,
               target.hwnd);

        const DWORD waitMs = target.required ? INFINITE : 1500;
        const DWORD cleanupWait = WaitForSingleObject(
            g_hotCleanupCompletedEvent, waitMs);
        DWORD fenceWait = WAIT_FAILED;
        if (cleanupWait == WAIT_OBJECT_0) {
            fenceWait = WaitForSingleObject(
                g_hotCleanupFenceEvent, waitMs);
        }

        DetachHotCleanupRendezvousHooks();
        g_hotCleanupPending.store(false, std::memory_order_release);

        if (cleanupWait == WAIT_OBJECT_0 &&
            fenceWait == WAIT_OBJECT_0) {
            ++cleanedTargets;
            ModLog(L"Hot-unload: thread=%lu callbacks detached and dispatcher fenced",
                   target.threadId);
            continue;
        }

        if (target.required) {
            allRequiredTargetsCleaned = false;
            ModLog(L"Hot-unload: REQUIRED thread=%lu cleanup/fence confirmation failed cleanupWait=%lu fenceWait=%lu",
                   target.threadId, cleanupWait, fenceWait);
        } else {
            ModLog(L"Hot-unload: optional UI thread=%lu did not complete orphan sweep/fence; continuing",
                   target.threadId);
        }
    }

    g_hotCleanupProbeThreadId.store(0, std::memory_order_release);
    g_hotCleanupPending.store(false, std::memory_order_release);
    DetachHotCleanupRendezvousHooks();

    CloseHandle(g_hotCleanupCompletedEvent);
    CloseHandle(g_hotCleanupFenceEvent);
    g_hotCleanupCompletedEvent = nullptr;
    g_hotCleanupFenceEvent = nullptr;

    // Keep the stale-WNDPROC protection and the dual GETMESSAGE/CALLWNDPROC
    // rendezvous plus queued/sent-message fan-out active during cleanup; this
    // makes cleanup resilient to nested UI message pumps. Unregister only
    // after every owning UI thread has had a chance to destroy its window.
    UnregisterPrivateProcessWindowClass(
        kBatteryUiBridgeClass, BatteryUiBridgeWndProc);
    UnregisterPrivateProcessWindowClass(
        kLegacyVolumeUiWindowClass, LegacyVolumeUiWndProc);

    ModLog(L"Hot-unload multi-thread cleanup complete: targets=%u cleaned=%u requiredOK=%d",
           static_cast<unsigned>(targets.size()), cleanedTargets,
           allRequiredTargetsCleaned ? 1 : 0);
    return allRequiredTargetsCleaned;
}


void OnLoaded_Hook(void* pThis, void* sender, void* args) {
    RememberXamlThread(GetCurrentThreadId(), true);
    ModLog(L"SystemProviderUserControl::_OnLoaded hook fired on XAML thread=%lu",
           GetCurrentThreadId());

    g_OnLoaded_Original(pThis, sender, args);

    if (g_unloading.load(std::memory_order_acquire)) {
        return;
    }

    // The RoutedEvent sender is an ABI interface pointer for the actual
    // SystemProviderUserControl, which is safer to project than the native
    // C++/CX implementation `this` pointer.
    InjectVisual(sender);
}

bool GetKnownFastPathRva(DWORD build,
                         DWORD ubr,
                         uintptr_t* rva,
                         const wchar_t** label) {
    // These entries are NOT an allowlist. They only bypass symbol enumeration
    // on builds we've already validated, so the hook is installed before the
    // first SystemProviderUserControl::_OnLoaded can race past us.
    if (build == 17763 && ubr == 2114) {
        *rva = 0x49760;
        *label = L"Windows 10 1809 17763.2114";
        return true;
    }

    if (build == 19045 && ubr == 7725) {
        *rva = 0x472D0;
        *label = L"Windows 10 22H2 19045.7725";
        return true;
    }

    if (build == 26200 && ubr == 6584) {
        *rva = 0x1BFD0;
        *label = L"Windows 11 25H2 26200.6584";
        return true;
    }

    if (build == 26200 && ubr == 9445) {
        *rva = 0x1B630;
        *label = L"Windows 11 25H2 26200.9445";
        return true;
    }

    return false;
}

bool InstallHook(HMODULE module, DWORD build, DWORD ubr) {
    if (g_hookInstalled.load()) {
        return true;
    }

    void* target = nullptr;
    uintptr_t fastRva = 0;
    const wchar_t* fastLabel = nullptr;
    bool usedStructuralResolver = false;

    if (GetKnownFastPathRva(build, ubr, &fastRva, &fastLabel)) {
        bool fastPathValid = false;
        StobjectPeView peView{};
        DWORD functionBegin = 0;
        DWORD functionEnd = 0;
        if (GetStobjectPeView(module, &peView) &&
            FindContainingRuntimeFunction(
                peView, static_cast<DWORD>(fastRva),
                &functionBegin, &functionEnd) &&
            functionBegin == fastRva &&
            functionEnd > functionBegin) {
            fastPathValid = true;
        }

        if (fastPathValid) {
            target = reinterpret_cast<void*>(
                reinterpret_cast<uintptr_t>(module) + fastRva);

            ModLog(L"Known-build fast path (not an allowlist): %s, _OnLoaded RVA=0x%llX (runtime-function boundary validated)",
                   fastLabel,
                   static_cast<unsigned long long>(fastRva));

            // Cross-check known builds only while diagnostic logging is
            // enabled. Marketplace/default operation takes the already
            // validated zero-race fast path without paying for a structural
            // scan on every LogonUI startup.
            if (g_enableLogging.load(std::memory_order_relaxed)) {
                if (void* structuralCheck =
                        ResolveOnLoadedByStructure(module)) {
                    const auto structuralRva =
                        reinterpret_cast<uintptr_t>(structuralCheck) -
                        reinterpret_cast<uintptr_t>(module);
                    if (structuralRva == fastRva) {
                        ModLog(L"Symbol-free _OnLoaded cross-check matched tested fast path at RVA=0x%llX",
                               static_cast<unsigned long long>(
                                   structuralRva));
                    } else {
                        ModLog(L"WARNING: symbol-free _OnLoaded cross-check disagreed with tested fast path: structural=0x%llX tested=0x%llX; keeping tested target",
                               static_cast<unsigned long long>(
                                   structuralRva),
                               static_cast<unsigned long long>(
                                   fastRva));
                    }
                } else {
                    ModLog(L"Symbol-free _OnLoaded cross-check did not resolve this known build; keeping tested target");
                }
            }
        } else {
            ModLog(L"Known-build _OnLoaded fast path failed PE/runtime-function validation for %s; trying symbol-free structural resolver",
                   fastLabel);
            fastLabel = nullptr;
        }
    }

    if (!target) {
        ModLog(L"No validated tested fast-path target for %lu.%lu; trying symbol-free _OnLoaded resolver",
               build,
               ubr);
        target = ResolveOnLoadedByStructure(module);
        if (target) {
            usedStructuralResolver = true;
        }
    }

    if (!target) {
        ModLog(L"Symbol-free _OnLoaded resolution unavailable; using symbol fallback");
        target = ResolveOnLoadedBySymbols(module);
        if (!target) {
            return false;
        }
    }

    bool expected = false;
    if (!g_hookInstalled.compare_exchange_strong(expected, true)) {
        return true;
    }

    if (!Wh_SetFunctionHook(
            target,
            reinterpret_cast<void*>(OnLoaded_Hook),
            reinterpret_cast<void**>(&g_OnLoaded_Original))) {
        ModLog(L"Wh_SetFunctionHook failed");
        g_hookInstalled = false;
        return false;
    }

    Wh_ApplyHookOperations();

    if (fastLabel) {
        ModLog(L"Hook installed immediately via tested fast path");
    } else if (usedStructuralResolver) {
        ModLog(L"Hook installed via cross-build symbol-free structural resolver");
    } else {
        ModLog(L"Hook installed via cross-build symbol fallback");
    }

    // Hot-attach/reload recovery: the provider may already have completed
    // _OnLoaded while first-time symbol acquisition was in progress, or the
    // user may have recompiled the mod while already on the sign-in screen.
    RequestLateAttachExistingSystemProvider();
    return true;
}

DWORD WINAPI HookThreadProc(void*) {
    DWORD build = 0;
    DWORD ubr = 0;
    if (GetOsBuild(&build, &ubr)) {
        g_osBuild.store(build);
        ModLog(L"Windows build: %lu.%lu (diagnostic only; no build allowlist)",
               build,
               ubr);
    } else {
        ModLog(L"Couldn't read Windows build; continuing with symbol resolution");
    }

    while (WaitForSingleObject(g_stopEvent, 50) == WAIT_TIMEOUT) {
        HMODULE module = GetModuleHandleW(kTargetModule);
        if (!module) {
            continue;
        }

        if (!InstallHook(module, build, ubr)) {
            ModLog(L"Couldn't resolve/hook _OnLoaded on this system; leaving LogonUI untouched");
        }
        return 0;
    }

    return 0;
}

} // namespace

BOOL Wh_ModInit() {
    g_unloading.store(false, std::memory_order_release);
    ClearKnownXamlThreads();
    g_lateAttachProbeThreadId.store(0, std::memory_order_release);
    g_hotCleanupProbeThreadId.store(0, std::memory_order_release);
    LoadSettings();
    ModLog(L"Init");

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        ModLog(L"CreateEvent failed: %lu", GetLastError());
        return FALSE;
    }

    // Start stobject symbol acquisition immediately on a worker. On an exact
    // known build this resolves essentially instantly; on an unknown build
    // Windhawk can download/enumerate symbols without ever blocking LogonUI's
    // XAML UI thread.
    StartBatteryFlyoutSymbolPrefetch();
    EnsureBatteryNotificationWatcherStarted();

    g_hookThread = CreateThread(nullptr, 0, HookThreadProc, nullptr, 0, nullptr);
    if (!g_hookThread) {
        ModLog(L"CreateThread failed: %lu", GetLastError());

        if (g_batteryResolverThread) {
            WaitForSingleObject(g_batteryResolverThread, INFINITE);
            CloseHandle(g_batteryResolverThread);
            g_batteryResolverThread = nullptr;
        }

        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        return FALSE;
    }

    return TRUE;
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    ModLog(L"SettingsChanged: requesting a controlled mod reload so all visual, layout, interaction, and diagnostic settings apply consistently");
    if (bReload) {
        *bReload = TRUE;
    }
    return TRUE;
}

void Wh_ModBeforeUninit() {
    g_unloading.store(true, std::memory_order_release);
    ModLog(L"BeforeUninit: beginning hot-unload-safe teardown");

    // Stop producers first so the UI fence only has to drain work which was
    // already queued before teardown began.
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
    StopBrightnessWorker();

    g_lateAttachPending.store(false, std::memory_order_release);
    DetachLateAttachRendezvousHooks();
    if (g_lateAttachCompletedEvent) {
        SetEvent(g_lateAttachCompletedEvent);
    }

    const bool cleaned = RequestHotCleanupOnUiThread();
    if (!cleaned) {
        ModLog(L"WARNING: hot-unload UI cleanup couldn't be fully confirmed before hook removal");
    }

    // Diagnostic RenderTargetBitmap capture is a coroutine whose continuation
    // also lives in this DLL. It is off by default, but if enabled, don't let
    // Windhawk unload the module until an already-started capture has resumed
    // and completed.
    ULONGLONG nextCaptureWaitLog = GetTickCount64() + 5000;
    while (g_captureAsyncInFlight.load(std::memory_order_acquire) != 0) {
        Sleep(10);
        if (GetTickCount64() >= nextCaptureWaitLog) {
            ModLog(L"BeforeUninit: waiting for diagnostic XAML capture coroutine to finish before DLL unload");
            nextCaptureWaitLog = GetTickCount64() + 5000;
        }
    }

    ULONGLONG nextAudioWaitLog = GetTickCount64() + 5000;
    while (g_audioCallbackObjectsAlive.load(std::memory_order_acquire) != 0 ||
           g_audioCallbacksInFlight.load(std::memory_order_acquire) != 0) {
        Sleep(10);
        if (GetTickCount64() >= nextAudioWaitLog) {
            ModLog(L"BeforeUninit: waiting for Core Audio callback objects to be released before DLL unload (objects=%u inFlight=%u)",
                   g_audioCallbackObjectsAlive.load(std::memory_order_relaxed),
                   g_audioCallbacksInFlight.load(std::memory_order_relaxed));
            nextAudioWaitLog = GetTickCount64() + 5000;
        }
    }
}

void Wh_ModUninit() {
    StopBrightnessWorker();
    ModLog(L"Uninit");

    g_lateAttachPending.store(false, std::memory_order_release);
    DetachLateAttachRendezvousHooks();
    if (g_lateAttachCompletedEvent) {
        SetEvent(g_lateAttachCompletedEvent);
    }

    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }

    if (HWND bridge =
            g_batteryUiBridgeWindow.exchange(
                nullptr, std::memory_order_acq_rel)) {
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(
            bridge,
            WM_CLOSE,
            0,
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            1000,
            &ignored);
    }

    if (g_hookThread) {
        WaitForSingleObject(g_hookThread, INFINITE);
        CloseHandle(g_hookThread);
        g_hookThread = nullptr;
    }

    if (g_lateAttachCompletedEvent) {
        CloseHandle(g_lateAttachCompletedEvent);
        g_lateAttachCompletedEvent = nullptr;
    }

    if (g_batteryResolverThread) {
        // Windhawk's symbol API doesn't expose cancellation. Wait for the
        // worker so it can't execute code after this mod DLL is unloaded.
        WaitForSingleObject(g_batteryResolverThread, INFINITE);
        CloseHandle(g_batteryResolverThread);
        g_batteryResolverThread = nullptr;
    }

    if (g_batteryNotificationThread) {
        WaitForSingleObject(g_batteryNotificationThread, INFINITE);
        CloseHandle(g_batteryNotificationThread);
        g_batteryNotificationThread = nullptr;
    }

    if (g_advancedPowerThread) {
        WaitForSingleObject(g_advancedPowerThread, INFINITE);
        CloseHandle(g_advancedPowerThread);
        g_advancedPowerThread = nullptr;
    }

    if (g_sndVolWindowMonitorThread) {
        WaitForSingleObject(g_sndVolWindowMonitorThread, INFINITE);
        CloseHandle(g_sndVolWindowMonitorThread);
        g_sndVolWindowMonitorThread = nullptr;
    }

    // Process-local window classes outlive their windows unless explicitly
    // unregistered. Leaving one registered would retain a WNDPROC pointer into
    // this DLL after hot-disable and can crash the next hot-load via CFG or a
    // jump into the same RVA of a newly mapped mod image.
    UnregisterPrivateProcessWindowClass(
        kBatteryUiBridgeClass, BatteryUiBridgeWndProc);
    UnregisterPrivateProcessWindowClass(
        kBatteryNotifyClass, BatteryNotifyWndProc);
    UnregisterPrivateProcessWindowClass(
        kLegacyVolumeUiWindowClass, LegacyVolumeUiWndProc);

    if (void* cleanupTarget =
            g_cleanupBatteryDataTarget.load(std::memory_order_acquire)) {
        using CleanupBatteryData_t = void(WINAPI*)();
        auto cleanupBatteryData =
            reinterpret_cast<CleanupBatteryData_t>(cleanupTarget);
        cleanupBatteryData();
        ModLog(L"BatMeter backend cleaned up");
    }

    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}
