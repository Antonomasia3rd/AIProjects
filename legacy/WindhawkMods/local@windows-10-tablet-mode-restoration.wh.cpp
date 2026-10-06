// ==WindhawkMod==
// @id              windows-10-tablet-mode-restoration
// @name            Windows 10 Tablet Mode restoration
// @description     Restores surviving Windows 10 Tablet Mode shell, taskbar, Start, and app-switch behavior on Windows 11
// @version         0.3.13
// @author          AI-assisted reverse-engineering experiment
// @include         explorer.exe
// @include         StartMenuExperienceHost.exe
// @compilerOptions -lole32 -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Windows 10 Tablet Mode restoration

Merged stable host mod for the Windows 10 Tablet Mode restoration project.

v0.3.13 adds one opt-in 25H2-only per-HWND move/size-interception experiment.
Static analysis shows GamingPosturePositioner queries the real
IPrivilegedArrangementOperations service (SID/IID
4aa32b90-a324-4d8b-aa7e-c8a4b13e5b90) and its SetInterceptMoveAndSize helper
calls that interface with mask=0x6, features=0x6 to enable interception and
features=0 to disable it. When the new setting is enabled, an Explorer worker
queries that same real service object through ImmersiveShell and applies the exact
0x6/0x6 policy only to the current foreground, maximized, non-Explorer top-level
window while real Tablet Mode compatibility is active. It removes the policy on
window change, restore/unmaximize, compatibility exit, and mod unload. It does not
construct private shell objects, patch COM vtables, alter global WNF, or change
the global scenario profile. The setting defaults OFF.

v0.3.12 adds a read-only, self-identifying desktop drag observer so the next
window-management test no longer depends on guessing screen coordinates or on
mouse-7 happening to print the target window. While the diagnostic setting is
enabled, an Explorer worker samples only the left-button state and, on button
down/up, logs the HWND under the cursor, its root window, class/title/process,
placement/maximized state, and cursor coordinates. While the button is held it
also queries the target GUI thread and logs the first native hwndMoveSize that
appears. The verified ViewPresentationMediator/TabletModePositionerManager
callbacks now resolve their HWND to the same identity record, and
CTitleBarInvoker::v_StartDrag snapshots the cursor target on entry. This observer
does not inject input, alter window state, install global mouse hooks, or change
the v0.3.11 0x47F -> 0x4BF window-management experiment.

v0.3.11 narrows the shell-window-management experiment to the exact low-byte
difference that remains after the v0.3.10 negative test. v0.3.10 proved that
clearing feature 0x40 alone (0x47F -> 0x43F) does not restore ordinary maximized
Win32 move-size cooperation. Static comparison now shows that working later Win10
Tablet scenario 1 uniquely uses 0xBF (0x80 set / 0x40 clear), while Win11 21H2
already changed that same scenario to 0x7F (0x80 clear / 0x40 set). Bit 0x80 is
not obsolete on Win11: both Win11 21H2 and 25H2 still emit it in native non-tablet
profiles. When enabled, v0.3.11 therefore preserves the native 25H2 mask and all
newer high bits but changes only the Tablet scenario low-byte pair:
0x47F -> 0x4BF (set 0x80, clear 0x40).

v0.3.11 also removes the CApplicationManager::OnMoveSizeAttempted trace hook from
the read-only gesture tracer. Its exact PDB symbol is ICF/hot-path ambiguous on
this build and produced high-volume nonsensical arguments; the verified
ViewPresentationMediator and TabletModePositionerManager callbacks remain traced.

v0.3.10 added the first opt-in shell-only window-management compatibility
experiment. It hooked only
ViewPresentationMediator::ComputeMaskAndFeaturesForScenarios, called the native
implementation first, and cleared feature 0x40 while real Tablet Mode
compatibility was active and scenario bit 1 was present. The controlled test
confirmed the native 0x47F -> 0x43F adjustment occurred, but ordinary Notepad
drag still entered normal USER move-size, so that single-bit experiment is now
superseded by v0.3.11.

v0.3.9 restores the native Win10 top-edge listener visibility prerequisite on the
validated Windows 11 25H2 twinui build. Win10 keeps WNF_TMCN_ISTABLETMODE=1 in
Tablet Mode; Win11's surviving TabletModeController enters mode 1 while that
legacy WNF state remains 0. CEdgeUiInput::SetVisible consults this WNF state for
legacy edge index 2 before it actually shows the uncloaked titlebar listener.
While real Tablet Mode compatibility is active, v0.3.9 therefore makes only the
native edge-2 SetVisible(true) call observe WNF_TMCN_ISTABLETMODE=1. The
persistent/global WNF state is never modified. The deprecated raw->Win32 mouse
bridge and old edge-index remap now default OFF.

Safety note for v0.2.8:
- the v0.1.4 experiment that broadly re-enabled twinui/twinui.pcshell
  TabletModeHelpers::IsTabletMode callers has been intentionally removed;
  on Windows 11 it can awaken incompatible legacy window-positioner/snap
  paths and hang Explorer. v0.2.8 keeps that rollback. The edge-swipe forcing remains
  caller-scoped to twinui.dll CEdgeUiManager::_LayoutEdgeUiInputs and
  CEdgeUiInput::_OnPointerUpdate. v0.2.8 additionally restores only
  TitlebarOverlayHelpers::OverlayTitlebarsInTabletMode in twinui.dll and
  twinui.pcshell.dll while the real controller is in Tablet Mode. It does NOT force
  the twinui.pcshell TabletModeHelpers/SnapServiceProvider gates that caused the
  black divider/hang in v0.1.4. v0.2.8 keeps the existing read-only tracing around the surviving
  SnapServiceProvider tablet/desktop snap handlers, TabletModePositioner::ShowSnapAssist,
  and TabletModeViewManager::CreateWindowArrangementViewForDrag so a single failed
  snapping attempt can be localized without changing snap behavior.
- v0.2.8 retains the fresh-Explorer startup ordering fix: if twinui.pcshell.dll is not
  loaded when Windhawk first injects, the pcshell gesture/snap tracer and titlebar
  overlay hook are deferred and applied when the module appears instead of being
  permanently skipped until a manual mod reload.
- v0.2.8 restores the surviving Win10-style top-edge mouse drag route with a
  caller-scoped compatibility fix. Static Win10/Win11 comparison showed that the
  physical top-edge mouse input resolves to EDGEUI_INDEX 2 on Win10 but 3 on the
  tested Win11 build. The shared edge-to-component table maps index 2 to component 4
  (CTitleBarInvoker) and index 3 to component 3 (CTaskbarInvoker), while the surviving
  CEdgeUiManager::MouseDragStart implementation still accepts only legacy index 2 (or 8).
  v0.2.8 therefore remaps only CEdgeUiManager::_IndexFromEdgeInput results 3 -> 2 when
  called from MouseHitCornerOrEdge or MouseDragStart while compatibility mode is active.
  No global EDGEUI mapping, raw-input suppression, or synthetic touch injection is used.
  The v0.2.6 ObservedMouseButtonUp hook remains intentionally absent because that PDB
  symbol is LTCG/ICF-folded with an unrelated shell method on this build.
It combines the previously separate HOST-1 v0.1.4 (Explorer/controller side)
and HOST-2 v0.4.0 (StartUI side) without changing their proven defaults.

Explorer-side behavior:
- enters the surviving real ITabletModeController state with SetMode(1, 4)
- bypasses the tested controller availability cache when necessary
- restores legacy Explorer and ExplorerPatcher Tablet Mode decisions
- restores touch-friendly ExplorerPatcher task-button sizing on the tested build
- commits mode transitions with a real 1-pixel taskbar geometry pulse plus the same
  WM_DWMCOMPOSITIONCHANGED refresh family ExplorerPatcher itself uses
- restores the missing legacy WNF gate used by the original Start app reuse/focus path
  without modifying the global WNF state

StartMenuExperienceHost-side behavior:
- forces the restored StartUI native Tablet Mode property and shell-mode event
- maps canonical OpenNewWindow to the old TabletMode_OpenNewWindow metadata
- restores the genuine U+E8A7 Open-in-new-window glyph
- restores the Windows 10 normal-tile InvocationSurface2 value (0)
- keeps failed/diagnostic fallbacks disabled by default

The two subsystems are process-dispatched internally. Explorer-only code is never
initialized in StartMenuExperienceHost.exe, and StartUI-only code is never
initialized in explorer.exe.

This remains build-specific reverse-engineering code. The controller availability
layout, ExplorerPatcher touch-layout offset, StartUI symbols, and related private
interfaces are validated only against the builds used during this project.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- enableRealTabletMode: true
  $name: Enter real Windows Tablet Mode
- bypassAvailabilityGate: true
  $name: Enable the controller's cached Tablet Mode availability
  $description: Build-specific. Required on the tested Windows build where TabletModeController reports Tablet Mode unavailable.
- minimumExplorerAgeMs: 10000
  $name: Minimum Explorer age before the real mode transition (ms)
  $description: Prevents the real mode-change broadcast from reaching twinui.pcshell listeners before their internal objects are initialized.
- shellStableMs: 1500
  $name: Additional shell-stable time before transition (ms)
- startupDelayMs: 500
  $name: Initial controller worker delay (ms)
- modeTrigger: 4
  $name: TabletModeController SetMode trigger
- restoreOnUnload: true
  $name: Restore the original controller mode on unload
- forceExplorerTabletMode: true
  $name: Force legacy Explorer CTray Tablet Mode query
- forceExplorerImmersiveMode: false
  $name: Force legacy Explorer CTray immersive-mode query
- forceExplorerPatcherTabletMode: true
  $name: Force ExplorerPatcher Tablet Mode queries
- restoreStartAppSwitchBehavior: true
  $name: Restore Tablet Mode Start app switching
  $description: Restores Win10 focus/reuse for normal Start tile clicks by scoping the legacy WNF Tablet Mode value only to TrySwitchToAppIfTabletMode. Global WNF state is unchanged.
- forceTouchFriendlyTaskButtons: true
  $name: Make ExplorerPatcher task buttons easier to touch
- explorerPatcherWaitMs: 30000
  $name: Wait for ExplorerPatcher taskbar DLL (ms)
- refreshTaskbarAfterModeTransition: true
  $name: Refresh taskbar layout after Tablet Mode transitions
  $description: Forces a bounded real Shell_TrayWnd size pulse, TraySettings notification, composition refresh, and repaint after entering or leaving Tablet Mode so ExplorerPatcher cannot keep cached Tablet/Desktop geometry until later interaction.
- taskbarRefreshDelayMs: 150
  $name: Delay before taskbar refresh (ms)
  $description: Gives the shell-mode transition a brief chance to settle before forcing one taskbar layout/repaint pass.
- traceLegacyTabletWindowGestures: true
  $name: Trace legacy Tablet Mode window gestures
  $description: Diagnostic only. Logs the surviving TabletModeInputHandler and TabletModeViewManager swipe pipeline without forcing the unsafe broad legacy TabletModeHelpers gates.
- traceDesktopDragIdentity: true
  $name: Trace desktop drag target identity
  $description: >-
    Read-only diagnostic. Logs the window under the cursor on left-button down/up,
    its process/class/title/maximized state, and the first native USER hwndMoveSize
    observed for that target while the button is held. No mouse hook or input injection.
- restoreLegacyEdgeSwipeGate: true
  $name: Restore legacy edge-swipe Tablet Mode gate (experimental)
  $description: >-
    Narrow experiment: reports Tablet Mode only to twinui.dll CEdgeUiManager::_LayoutEdgeUiInputs
    and CEdgeUiInput::_OnPointerUpdate while the real controller is in Tablet Mode. It deliberately
    leaves twinui.pcshell TabletModeViewManager and SnapServiceProvider gates untouched.
- restoreLegacyMouseDrag: false
  $name: Deprecated edge-index mouse remap (leave OFF)
  $description: >-
    Caller-scoped Win10 compatibility fix for the tested twinui.dll build. Remaps only
    CEdgeUiManager::_IndexFromEdgeInput results 3 -> 2 for a CEdgeUiInput that the surviving
    _IsTopEdge method identifies as the legacy top edge, and only when called from
    MouseHitCornerOrEdge or MouseDragStart while real Tablet Mode compatibility is active.
    This prevents the earlier remap from contaminating bottom-edge mouse activity.

    v0.2.9 keeps that narrow remap unchanged and adds read-only tracing of the
    derived CTitleBarInvoker/CTaskbarInvoker virtual mouse methods. This closes a
    tracer blind spot: CEdgeInvoker wrappers can dispatch to derived v_* methods,
    so the base CEdgeInvoker::StartDrag hook alone cannot prove drag recognition
    did not occur.

    v0.3.0 adds read-only tracing for the normal Win32 mouse-message path that the
    Win10 baseline proved actually initiates drag: CEdgeUiInput::_WndProc, _OnMouseMove,
    _MouseMoveToCornerOrEdge, MouseHitCornerOrEdge, HitCornerOrEdge, and
    CTitleBarInvoker::v_Invoke.

    v0.3.1 adds a narrowly-scoped mouse transport bridge for the tested Win11 build.
    The Win10 baseline proved that WM_LBUTTONDOWN -> _OnMouseDown followed by held
    WM_MOUSEMOVE -> _OnMouseMove is what actually calls MouseDragStart. Win11 no longer
    delivers those mouse messages to the edge input window, while its raw mouse observer
    still receives the same physical input. For a left-button press at a monitor's top
    edge only, v0.3.1 reroutes that raw down/move sequence through the surviving
    _OnMouseDown/_OnMouseMove handlers. All non-top-edge raw input remains native.

    v0.3.2 additionally resolves the surviving CEdgeUiInput::s_fDisableTitlebarInvocation
    gate. The tested build already reports that flag as 0, so it is not the missing mouse gate.

    v0.3.3 fixes the actual top-edge state mismatch exposed by v0.3.2. Win10 and Win11
    _IsTopEdge are still the same tiny test (legacy edge index 2 or 8), but the tested Win11
    physical top-edge object reports index 3. During one verified physical-top-edge mouse
    bridge only, that same CEdgeUiInput reports _IsTopEdge=true and remaps
    _IndexFromEdgeInput 3 -> 2 only inside a physical-top MouseHitCornerOrEdge call or the
    subsequent MouseDragStart for that exact object. Bottom-edge traffic remains native.

    v0.3.4 widens only the bridge/hit-test acquisition slop from 8 to 32 physical pixels.
    The v0.3.3 trace showed the cursor genuinely dwelling at y=0, but the raw left-button
    down arrived at y=20, so the previous 8-pixel guard never armed. This does not widen
    any global edge mapping; it applies only to this raw edge-input compatibility bridge.

    v0.3.5 makes the bridge transport-independent. Some 25H2 runs deliver the raw mouse
    stream only through CEdgeInvoker::ObservedMouse* while the CEdgeUiInput observer
    callbacks are silent. The mod now remembers the exact CEdgeUiInput passed to
    InputObserveStart and, for that exact active invoker only, can arm/continue the same
    _OnMouseDown/_OnMouseMove bridge from CEdgeInvoker::ObservedMouseButtonDown/Move.
    Native invoker dispatch is suppressed only while this one top-edge bridge is active.
- bridgeLegacyMouseWin32Path: false
  $name: Deprecated raw-to-Win32 mouse bridge (leave OFF)
  $description: >-
    Build-specific v0.3.5 experiment. Only while real Tablet Mode compatibility is active,
    a raw left-button press within 32 physical pixels of a monitor top edge is treated as the missing
    Win10 WM_LBUTTONDOWN path, and subsequent raw motion while the physical left button is
    held is passed to the surviving _OnMouseMove handler. If the CEdgeUiInput raw observer
    is silent, v0.3.5 uses the already-active CEdgeInvoker raw callbacks as a fallback
    transport for that exact edge input/invoker pair. This is mouse-only; touch and
    pointer handling are unchanged.
- restoreLegacyTitlebarMouseInvocationGate: true
  $name: Restore legacy top-edge titlebar mouse invocation gate (experimental)
  $description: >-
    Build-specific compatibility gate for the native legacy titlebar listener.
    Resolves CEdgeUiInput::s_fDisableTitlebarInvocation and clears it only while real
    Tablet Mode compatibility is active. v0.3.9 additionally scopes the already-used
    WNF compatibility hook to CEdgeUiInput::SetVisible(true) only when that exact
    CEdgeUiInput reports legacy EDGEUI_INDEX 2. This makes the native Win11 SetVisible
    decision observe WNF_TMCN_ISTABLETMODE=1 just for the edge-2 call, matching Win10,
    without modifying the persistent/global WNF state or directly showing/uncloaking
    any HWND. The original titlebar-disable byte is restored when leaving Tablet Mode
    or unloading.
- restoreTabletTitlebarOverlay: true
  $name: Restore Tablet Mode titlebar overlay / hidden close button (experimental)
  $description: >-
    Forces only TitlebarOverlayHelpers::OverlayTitlebarsInTabletMode in twinui.dll and
    twinui.pcshell.dll while the real controller is in Tablet Mode. It deliberately leaves
    twinui.pcshell TabletModeHelpers and SnapServiceProvider gates untouched.
- restoreLegacyWindowManagementProfile: false
  $name: Restore Win10 Tablet window-management low-byte profile (experimental)
  $description: >-
    Narrow 25H2 experiment. Hooks only
    ViewPresentationMediator::ComputeMaskAndFeaturesForScenarios, calls the native
    implementation first, and while real Tablet Mode compatibility is active plus
    scenario bit 1 is present, restores the later-Win10 Tablet low-byte pair:
    feature 0x80 ON and feature 0x40 OFF. On the validated 25H2 profile this changes
    features 0x47F -> 0x4BF while preserving the native 0x700004FF mask, feature
    0x400, all higher/newer Windows 11 bits, and every other scenario. Leave OFF
    except for the controlled maximized-window drag test.

- restoreModernPerWindowMoveSizeIntercept: false
  $name: Test modern per-window move/size interception (25H2 only)
  $description: >-
    Narrow opt-in compatibility experiment for the validated Windows 11 25H2 build.
    Queries the real IPrivilegedArrangementOperations service from ImmersiveShell
    using its exact native SID/IID and calls the same method/arguments used by
    GamingPosturePositioner::SetInterceptMoveAndSize: mask 0x6, features 0x6 to
    enable and mask 0x6, features 0 to disable. The policy is applied only to the
    current foreground maximized top-level window when it belongs to another
    process and real Tablet Mode compatibility is active, then removed when that
    window is no longer eligible or the mod unloads. No COM vtable patching,
    private-object construction, global WNF modification, or global behavior-profile
    forcing is used. Leave OFF except for the controlled Notepad test.

- forceStartPropertiesTabletMode: true
  $name: Force StartProperties::IsTabletMode to true
- forceShellModeEventTabletMode: true
  $name: Force StartUI shell-mode event IsTabletMode to true
- traceVerbPipeline: false
  $name: Trace TabletMode_OpenNewWindow verb pipeline
- mapOpenNewWindowToTabletMode: true
  $name: Map OpenNewWindow to TabletMode_OpenNewWindow
- mapOpenVerbFallbackToTabletMode: false
  $name: Map shell Open fallback to TabletMode_OpenNewWindow
  $description: Leave OFF. Generic Open is not the genuine Tablet Mode command.
- disableDirectLaunchForWin32Tiles: false
  $name: Disable modern direct launch for Win32 Start tiles
  $description: Leave OFF. The tested activation path did not use DirectLaunch.
- restoreTabletOpenNewWindowIcon: true
  $name: Restore Open in new window icon
  $description: Restores the genuine Windows 10 Tablet Mode glyph U+E8A7 for command 0x1C.
- traceTileActivation: false
  $name: Trace tile activation and verb icon state
- forceWin10InvocationSurface: true
  $name: Force Windows 10 invocation surface on normal tile click
- forceProvideStartWindowIdOnTileActivation: false
  $name: Force ProvideStartWindowIdOnTileActivation
  $description: Leave OFF. Genuine Windows 10 returned false.
- focusExistingDesktopAppOnTileClick: false
  $name: Focus existing desktop app on normal tile click
  $description: Leave OFF. The restored shell/broker switch path now handles compatible apps.
- waitTimeoutMs: 30000
  $name: Wait for StartUI module (ms)

- verbose: false
  $name: Verbose diagnostic logging
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <objbase.h>
#include <servprov.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <string>
#include <windhawk_api.h>

namespace ExplorerHost {
namespace {

const GUID kClsidImmersiveShell = {
    0xC2F03A33, 0x21F5, 0x47FA,
    {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};

const GUID kIidServiceProvider = {
    0x6D5140C1, 0x7436, 0x11CE,
    {0x80, 0x34, 0x00, 0xAA, 0x00, 0x60, 0x09, 0xFA}};

const GUID kIidTabletModeController = {
    0x4FDA780A, 0xACD2, 0x41F7,
    {0xB4, 0xF2, 0xEB, 0xE6, 0x74, 0xC9, 0xBF, 0x2A}};

const GUID kIidPrivilegedArrangementOperations = {
    0x4AA32B90, 0xA324, 0x4D8B,
    {0xAA, 0x7E, 0xC8, 0xA4, 0xB1, 0x3E, 0x5B, 0x90}};

struct ITabletModeController : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMode(int* mode) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMode(int mode, int trigger) = 0;
};

constexpr DWORD kExpectedTwinuiTimestamp = 0x7827BEC5;
constexpr DWORD kExpectedTwinuiSizeOfImage = 0x004FD000;
constexpr DWORD kExpectedPcshellTimestamp = 0x6A6F7B81;
constexpr DWORD kExpectedPcshellSizeOfImage = 0x0091E000;
// Exact return RVAs immediately after TabletModeHelpers::IsTabletMode calls in
// the validated Win11 twinui.dll image above. These two callsites belong only
// to the legacy edge-input layout/update path; all other callers stay native.
constexpr uintptr_t kTwinuiLayoutEdgeUiInputsIsTabletReturnRva = 0x0001C777;
constexpr uintptr_t kTwinuiEdgePointerUpdateIsTabletReturnRva = 0x001D1B86;
// Exact Win11 25H2 (twinui.dll timestamp 0x7827BEC5) return addresses after
// CEdgeUiManager::_IndexFromEdgeInput calls in the two surviving mouse-route callers.
constexpr uintptr_t kTwinuiMouseHitIndexFromInputReturnRva = 0x00009BBB;
constexpr uintptr_t kTwinuiMouseDragStartIndexFromInputReturnRva = 0x001CEC49;
constexpr ptrdiff_t kAvailabilityOffsetFromITabletModeController = 0x250;

// Legacy Windows 10 Tablet Mode state still consumed by the modern
// ApplicationTileActivationBroker switch/reuse path. Windows 11 leaves this
// state at 0 even after the surviving real TabletModeController enters mode 1.
constexpr unsigned long long kWnfTmcnIsTabletMode =
    0x0F850339A3BC0835ULL;

// Exact ExplorerPatcher build supplied for this experiment. The touch-friendly
// task-button path below relies on CTaskListWnd's private +0x540 flag, so it is
// enabled only for this exact image.
constexpr DWORD kExpectedEpTaskbarTimestamp = 0x6A4A8711;
constexpr DWORD kExpectedEpTaskbarSizeOfImage = 0x00958000;
constexpr ptrdiff_t kEpTaskListTouchImprovementFlagOffset = 0x540;

std::atomic<bool> g_enableRealTabletMode{true};
std::atomic<bool> g_bypassAvailability{true};
std::atomic<DWORD> g_minimumExplorerAgeMs{10000};
std::atomic<DWORD> g_shellStableMs{1500};
std::atomic<DWORD> g_startupDelayMs{500};
std::atomic<int> g_modeTrigger{4};
std::atomic<bool> g_restoreOnUnload{true};
std::atomic<bool> g_forceExplorerTablet{true};
std::atomic<bool> g_forceExplorerImmersive{false};
std::atomic<bool> g_forceExplorerPatcherTablet{true};
std::atomic<bool> g_restoreStartAppSwitchBehavior{true};
std::atomic<bool> g_forceTouchFriendlyTaskButtons{true};
std::atomic<DWORD> g_epWaitMs{30000};
std::atomic<bool> g_refreshTaskbarAfterModeTransition{true};
std::atomic<DWORD> g_taskbarRefreshDelayMs{150};
std::atomic<bool> g_traceLegacyTabletWindowGestures{true};
std::atomic<bool> g_traceDesktopDragIdentity{true};
std::atomic<bool> g_restoreLegacyEdgeSwipeGate{true};
std::atomic<bool> g_restoreLegacyMouseDrag{false};
std::atomic<bool> g_bridgeLegacyMouseWin32Path{false};
std::atomic<bool> g_restoreLegacyTitlebarMouseInvocationGate{true};
std::atomic<bool> g_restoreTabletTitlebarOverlay{true};
std::atomic<bool> g_restoreLegacyWindowManagementProfile{false};
std::atomic<bool> g_restoreModernPerWindowMoveSizeIntercept{false};
std::atomic<bool> g_verbose{false};

std::atomic<bool> g_afterInit{false};
std::atomic<bool> g_unloading{false};
std::atomic<bool> g_compatActive{false};
std::atomic<unsigned long> g_cTrayTabletCalls{0};
std::atomic<unsigned long> g_cTrayImmersiveCalls{0};
std::atomic<unsigned long> g_epTrayCalls{0};
std::atomic<unsigned long> g_epTaskBandCalls{0};
std::atomic<unsigned long> g_epButtonWidthCalls{0};
std::atomic<unsigned long> g_startSwitchWnfOverrideCount{0};
std::atomic<unsigned long> g_edgeSetVisibleWnfOverrideCount{0};
std::atomic<bool> g_wnfQueryHookQueued{false};
std::atomic<unsigned long> g_gesturePointerDownCalls{0};
std::atomic<unsigned long> g_gesturePointerUpdateCalls{0};
std::atomic<unsigned long> g_gesturePointerUpCalls{0};
std::atomic<unsigned long> g_gestureStartSwipeCalls{0};
std::atomic<unsigned long> g_gestureStartExtendedSwipeCalls{0};
std::atomic<unsigned long> g_gestureContinueSwipeCalls{0};
std::atomic<unsigned long> g_gestureCommitSwipeCalls{0};
std::atomic<unsigned long> g_gestureCancelSwipeCalls{0};
std::atomic<unsigned long> g_mouseStartDragCalls{0};
std::atomic<unsigned long> g_mouseContinueDragCalls{0};
std::atomic<unsigned long> g_mouseCommitDragCalls{0};
std::atomic<unsigned long> g_mouseCancelDragCalls{0};
std::atomic<unsigned long> g_mouseManagerDragStartCalls{0};
std::atomic<unsigned long> g_mouseAcquireInvokerCalls{0};
std::atomic<unsigned long> g_mouseIndexRemapCalls{0};
std::atomic<unsigned long> g_mouseObservedDownCalls{0};
std::atomic<unsigned long> g_mouseHandleObservedCalls{0};
std::atomic<unsigned long> g_mouseObservedMoveCalls{0};
std::atomic<unsigned long> g_edgeInvokerObservedDownCalls{0};
std::atomic<unsigned long> g_edgeInvokerObservedMoveCalls{0};
std::atomic<unsigned long> g_edgeInvokerStartDragCalls{0};
std::atomic<unsigned long> g_titleBarObservedDownCalls{0};
std::atomic<unsigned long> g_titleBarObservedMoveCalls{0};
std::atomic<unsigned long> g_titleBarStartDragCalls{0};
std::atomic<unsigned long> g_titleBarStartServiceQueryCalls{0};
thread_local bool g_inTitleBarStartDrag = false;
// v0.3.7: per-call state used only to classify CTitleBarInvoker::v_StartDrag's
// two immediate fallible substeps. This never changes the returned HRESULT.
thread_local bool g_titleBarStartQuerySeen = false;
thread_local HRESULT g_titleBarStartQueryHr = S_OK;
thread_local void* g_titleBarStartQueryService = nullptr;
thread_local void* g_titleBarStartSlot48 = nullptr;
thread_local unsigned long g_titleBarQueryDepth = 0;
std::atomic<unsigned long> g_titleBarContinueDragCalls{0};
std::atomic<unsigned long> g_titleBarCommitDragCalls{0};
std::atomic<unsigned long> g_titleBarCancelDragCalls{0};
std::atomic<unsigned long> g_taskbarObservedDownCalls{0};
std::atomic<unsigned long> g_taskbarObservedMoveCalls{0};
std::atomic<unsigned long> g_mouseOnDownCalls{0};
std::atomic<unsigned long> g_mouseOnUpCalls{0};
std::atomic<unsigned long> g_mouseWndProcCalls{0};
std::atomic<unsigned long> g_mouseIsMousePenCalls{0};
std::atomic<unsigned long> g_mouseOnMoveCalls{0};
std::atomic<unsigned long> g_mouseMoveToEdgeCalls{0};
std::atomic<unsigned long> g_mouseManagerHitCalls{0};
std::atomic<unsigned long> g_mouseInvokerHitCalls{0};
std::atomic<unsigned long> g_titleBarInvokeCalls{0};
std::atomic<unsigned long> g_mouseBridgeDownCalls{0};
std::atomic<unsigned long> g_mouseBridgeMoveCalls{0};
std::atomic<unsigned long> g_mouseBridgeUpCalls{0};
std::atomic<unsigned long> g_mouseTitlebarGatePatches{0};
std::atomic<unsigned long> g_mouseTopEdgeForces{0};
std::atomic<unsigned long> g_gestureShowAppResizeCalls{0};
std::atomic<unsigned long> g_gestureInputInitCalls{0};
std::atomic<unsigned long> g_gestureMoveSizeAttemptedCalls{0};
std::atomic<unsigned long> g_viewMediatorMoveCalls{0};
std::atomic<unsigned long> g_tabletPositionerManagerMoveCalls{0};
std::atomic<unsigned long> g_shellPositionerChangedCalls{0};
std::atomic<unsigned long> g_tabletPositionerChangedCalls{0};
std::atomic<unsigned long> g_desktopPositionerChangedCalls{0};
std::atomic<unsigned long> g_desktopChromeCalls{0};
std::atomic<unsigned long> g_tabletChromeCalls{0};
std::atomic<unsigned long> g_snapToLocationCalls{0};
std::atomic<unsigned long> g_handleTabletSnappingCalls{0};
std::atomic<unsigned long> g_handleDesktopSnappingCalls{0};
std::atomic<unsigned long> g_showSnapAssistCalls{0};
std::atomic<unsigned long> g_createArrangementDragCalls{0};
std::atomic<unsigned long> g_swapPositionersForViewsCalls{0};
std::atomic<unsigned long> g_changePositionerForViewCalls{0};
std::atomic<unsigned long> g_positionerHandoffCalls{0};
std::atomic<unsigned long> g_positionerHandoffArrayCalls{0};
std::atomic<unsigned long> g_positionerHandoffAppCalls{0};
std::atomic<unsigned long> g_edgeLayoutCalls{0};
std::atomic<unsigned long> g_edgeDelayedInitCalls{0};
std::atomic<unsigned long> g_edgeObserveStartCalls{0};
std::atomic<unsigned long> g_edgeSetVisibleCalls{0};
std::atomic<unsigned long> g_edgeRegisterRawInputCalls{0};
std::atomic<unsigned long> g_edgePointerDownCalls{0};
std::atomic<unsigned long> g_edgePointerUpdateCalls{0};
std::atomic<unsigned long> g_edgePointerUpCalls{0};
std::atomic<unsigned long> g_edgeTabletGateForcedCalls{0};
std::atomic<unsigned long> g_titlebarOverlayForcedCalls{0};
std::atomic<unsigned long> g_windowManagementProfileAdjustments{0};
std::atomic<unsigned long> g_perWindowInterceptApplyCalls{0};
std::atomic<unsigned long> g_perWindowInterceptRemoveCalls{0};
std::atomic<unsigned long> g_perWindowInterceptQueryFailures{0};
std::atomic<unsigned long> g_dragIdentityDownCalls{0};
std::atomic<unsigned long> g_dragIdentityUpCalls{0};
std::atomic<unsigned long> g_dragIdentityMoveSizeCalls{0};

// twinui.pcshell.dll can be absent when Windhawk injects into a freshly started
// Explorer. Track queued pcshell-dependent hooks so the post-init worker can
// install them when the module appears, before the real Tablet Mode transition
// whenever shell startup ordering permits.
std::atomic<bool> g_pcshellGestureHooksQueued{false};
std::atomic<bool> g_twinuiTitlebarHookQueued{false};
std::atomic<bool> g_pcshellTitlebarHookQueued{false};
std::atomic<bool> g_pcshellWindowManagementHookQueued{false};

HANDLE g_stopEvent = nullptr;
HANDLE g_controllerThread = nullptr;
HANDLE g_epThread = nullptr;
HANDLE g_dragIdentityThread = nullptr;
HANDLE g_perWindowInterceptThread = nullptr;

HMODULE g_twinui = nullptr;
bool g_loadedTwinui = false;
bool g_controllerBuildSupported = false;

std::atomic<int> g_originalMode{-1};
std::atomic<int> g_originalAvailability{-1};
std::atomic<bool> g_changedMode{false};
std::atomic<bool> g_leftAvailabilityForced{false};

using MemberBool_t = bool(__cdecl*)(void* pThis);
using StaticBool_t = bool(__cdecl*)();
using ComputeSingleButtonWidth_t =
    int(__cdecl*)(void* pThis, int groupType, void* taskBtnGroup,
                  int* constrainedWidth);

using WnfQueryCallback_t = LONG(NTAPI*)(
    unsigned long long stateName, ULONG changeStamp, void* typeId,
    void* callbackContext, const void* buffer, ULONG length);
using RtlQueryWnfStateData_t = LONG(NTAPI*)(
    ULONG* changeStamp, unsigned long long stateName, void* callback,
    void* callbackContext, const void* typeId);
using TrySwitchToAppIfTabletMode_t = bool (*)(const wchar_t* appId);
using TabletInputInit_t = HRESULT(__cdecl*)(void* pThis, int source, void* callback);
using TabletInputPointer_t = void(__cdecl*)(void* pThis, unsigned int pointerId, POINT point);
using TabletSwipe_t = HRESULT(__cdecl*)(void* pThis, unsigned int pointerId, POINT point);
using TabletExtendedSwipe_t = HRESULT(__cdecl*)(void* pThis, POINT point1, unsigned int pointerId, POINT point2);
using TabletCancelSwipe_t = HRESULT(__cdecl*)(void* pThis);
using TabletDrag_t = HRESULT(__cdecl*)(void* pThis, POINT point);
using TabletCancelDrag_t = HRESULT(__cdecl*)(void* pThis);
using EdgeUiMouseDragStart_t = HRESULT(__cdecl*)(void* pThis, void* edgeInput, void** mouseInvocation);
using EdgeUiIndexFromInput_t = int(__cdecl*)(void* pThis, void* edgeInput);
using EdgeUiAcquireMouseInvoker_t = HRESULT(__cdecl*)(void* pThis, int edgeIndex, void** mouseInvocation);
using EdgeUiObservedMouseButton_t = HRESULT(__cdecl*)(void* pThis, POINT point, int button);
using EdgeUiObservedMouseMove_t = HRESULT(__cdecl*)(void* pThis, POINT point, unsigned short flags, POINT anchorPoint);
using EdgeInvokerPoint_t = HRESULT(__cdecl*)(void* pThis, POINT point);
using EdgeInvokerMove_t = HRESULT(__cdecl*)(void* pThis, POINT point, POINT anchorPoint);
using DerivedInvokerObserve_t = void(__cdecl*)(void* pThis, POINT point);
using DerivedInvokerDrag_t = HRESULT(__cdecl*)(void* pThis, POINT point);
using DerivedInvokerCancel_t = HRESULT(__cdecl*)(void* pThis);
using EdgeUiHandleObservedMouseInput_t = void(__cdecl*)(void* pThis, bool buttonDown, POINT point, POINT anchorPoint);
using EdgeUiOnMouseDown_t = void(__cdecl*)(void* pThis, POINT point, bool fromRawInput);
using EdgeUiOnMouseUp_t = void(__cdecl*)(void* pThis, POINT point);
using EdgeUiWndProc_t = LRESULT(__cdecl*)(void* pThis, unsigned int msg, UINT_PTR wParam, LONG_PTR lParam, bool forwarded);
using EdgeUiIsMousePen_t = int(__cdecl*)(void* pThis);
using EdgeUiIsTopEdge_t = bool(__cdecl*)(void* pThis);
using EdgeUiOnMouseMove_t = void(__cdecl*)(void* pThis, POINT point, bool a, bool b);
using EdgeUiMoveToCornerOrEdge_t = HRESULT(__cdecl*)(void* pThis, int cornerOrEdge, POINT point, bool a, bool b, bool c);
using EdgeUiMouseHitCornerOrEdge_t = HRESULT(__cdecl*)(void* pThis, void* edgeInput, bool a, bool b, POINT point, bool* outHit, void** outInvoker);
using EdgeInvokerHitCornerOrEdge_t = HRESULT(__cdecl*)(void* pThis, int cornerOrEdge, POINT point, int* notification, int arg);
using DerivedInvokerInvoke_t = HRESULT(__cdecl*)(void* pThis, bool a, POINT point, int rawType);
using TabletShowAppResize_t = HRESULT(__cdecl*)(void* pThis, void* applicationView, int moveSizeType, POINT point);
using TabletMoveSizeAttempted_t = HRESULT(__cdecl*)(void* pThis, void* applicationView, int moveSizeType);
using SnapToLocation_t = HRESULT(__cdecl*)(void* pThis, void* applicationView,
                                              int snapLocation, int flags, RECT* snapRect);
using HandleTabletModeSnapping_t = HRESULT(__cdecl*)(void* pThis, void* applicationView,
                                                      int snapLocation, RECT* snapRect);
using HandleDesktopModeSnapping_t = HRESULT(__cdecl*)(void* pThis, void* applicationView,
                                                       int snapLocation, int flags, RECT* snapRect);
using ShowSnapAssist_t = HRESULT(__cdecl*)(void* pThis);
using CreateWindowArrangementViewForDrag_t = HRESULT(__cdecl*)(
    void* pThis, void* applicationView, void** unknownOut);
using UpstreamMoveSizeAttempted_t = void(__cdecl*)(void* pThis, HWND hwnd, void* applicationView, ULONG arg1, ULONG arg2);
using PositionerTabletModeChanged_t = HRESULT(__cdecl*)(void* pThis, int state);
using GetChromeConfigurationForView_t = HRESULT(__cdecl*)(void* pThis, void* applicationView, int* chromeOptions, void** titlebarConfiguration);
using SwapPositionersForViews_t = HRESULT(__cdecl*)(void* pThis, int fromType, int toType);
using ChangePositionerForView_t = HRESULT(__cdecl*)(void* pThis, void* applicationView, int positionerType, bool force);
using PerformPositionerHandoffView_t = HRESULT(__cdecl*)(void* pThis, void* applicationView, int fromType, int toType);
using PerformPositionerHandoffArray_t = HRESULT(__cdecl*)(
    void* pThis, void* objectArray, void* sourceMonitor, void* targetMonitor,
    void* presentationArgs, int fromType, int toType);
using PerformPositionerHandoffApp_t = HRESULT(__cdecl*)(
    void* pThis, void* applicationView, void* sourceMonitor, void* targetMonitor,
    void* presentationArgs, int fromType, int toType);
using EdgeUiLayout_t = HRESULT(__cdecl*)(void* pThis, int reason);
using EdgeUiSimple_t = HRESULT(__cdecl*)(void* pThis);
using EdgeUiInputObserveStart_t = HRESULT(__cdecl*)(void* pThis, void* edgeInput);
using EdgeUiSetVisible_t = HRESULT(__cdecl*)(void* pThis, bool visible);
using EdgeUiGetAssignedEdge_t = HRESULT(__cdecl*)(void* pThis, int* edge);
using EdgeUiRegisterRawInput_t = void(__cdecl*)(void* pThis, bool enable);
using EdgeUiPointerDown_t = void(__cdecl*)(void* pThis, unsigned int pointerId, POINT point, bool isPrimary);
using EdgeUiPointer_t = void(__cdecl*)(void* pThis, unsigned int pointerId, POINT point);
using IUnknown_QueryService_t = HRESULT(WINAPI*)(IUnknown* punk, REFGUID guidService, REFIID riid, void** ppvOut);

MemberBool_t CTray_IsTabletModeEnabled_Original = nullptr;
MemberBool_t CTray_IsModeImmersive_Original = nullptr;
MemberBool_t EP_TrayUI_IsTabletModeEnabled_Original = nullptr;
MemberBool_t EP_CTaskBand_IsTabletModeEnabled_Original = nullptr;
ComputeSingleButtonWidth_t EP_ComputeSingleButtonWidth_Original = nullptr;
RtlQueryWnfStateData_t RtlQueryWnfStateData_Original = nullptr;
TrySwitchToAppIfTabletMode_t TrySwitchToAppIfTabletMode_Original = nullptr;
IUnknown_QueryService_t IUnknown_QueryService_Original = nullptr;
TabletInputInit_t TabletModeInputHandler_RuntimeClassInitialize_Original = nullptr;
TabletInputPointer_t TabletModeInputHandler_PointerDown_Original = nullptr;
TabletInputPointer_t TabletModeInputHandler_PointerUpdate_Original = nullptr;
TabletInputPointer_t TabletModeInputHandler_PointerUp_Original = nullptr;
TabletSwipe_t TabletModeViewManager_StartSwipe_Original = nullptr;
TabletExtendedSwipe_t TabletModeViewManager_StartExtendedSwipe_Original = nullptr;
TabletSwipe_t TabletModeViewManager_ContinueSwipe_Original = nullptr;
TabletSwipe_t TabletModeViewManager_CommitSwipe_Original = nullptr;
TabletCancelSwipe_t TabletModeViewManager_CancelSwipe_Original = nullptr;
TabletDrag_t TabletModeViewManager_StartDrag_Original = nullptr;
TabletDrag_t TabletModeViewManager_ContinueDrag_Original = nullptr;
TabletDrag_t TabletModeViewManager_CommitDrag_Original = nullptr;
TabletCancelDrag_t TabletModeViewManager_CancelDrag_Original = nullptr;
TabletShowAppResize_t TabletModeViewManager_ShowAppResizeView_Original = nullptr;
TabletMoveSizeAttempted_t TabletModeViewManager_MoveSizeAttempted_Original = nullptr;
SnapToLocation_t SnapServiceProvider_SnapToLocation_Original = nullptr;
HandleTabletModeSnapping_t SnapServiceProvider_HandleTabletModeSnapping_Original = nullptr;
HandleDesktopModeSnapping_t SnapServiceProvider_HandleDesktopModeSnapping_Original = nullptr;
ShowSnapAssist_t TabletModePositioner_ShowSnapAssist_Original = nullptr;
CreateWindowArrangementViewForDrag_t TabletModeViewManager_CreateWindowArrangementViewForDrag_Original = nullptr;
UpstreamMoveSizeAttempted_t ViewPresentationMediator_OnMoveSizeAttempted_Original = nullptr;
UpstreamMoveSizeAttempted_t TabletModePositionerManager_OnMoveSizeAttempted_Original = nullptr;
PositionerTabletModeChanged_t ShellPositionerManager_TabletModeChanged_Original = nullptr;
PositionerTabletModeChanged_t TabletModePositioner_TabletModeChanged_Original = nullptr;
PositionerTabletModeChanged_t CDesktopPositioner_TabletModeChanged_Original = nullptr;
GetChromeConfigurationForView_t CDesktopPositioner_GetChromeConfigurationForView_Original = nullptr;
GetChromeConfigurationForView_t TabletModePositioner_GetChromeConfigurationForView_Original = nullptr;
SwapPositionersForViews_t ShellPositionerManager_SwapPositionersForViews_Original = nullptr;
ChangePositionerForView_t ShellPositionerManager_ChangePositionerForView_Original = nullptr;
PerformPositionerHandoffView_t ShellPositionerManager_PerformPositionerHandoffView_Original = nullptr;
PerformPositionerHandoffArray_t ShellPositionerManager_PerformPositionerHandoffArray_Original = nullptr;
PerformPositionerHandoffApp_t ShellPositionerManager_PerformPositionerHandoffApp_Original = nullptr;
EdgeUiLayout_t CEdgeUiManager_LayoutEdgeUiInputs_Original = nullptr;
EdgeUiSimple_t CEdgeUiManager_PerformDelayedInitialization_Original = nullptr;
EdgeUiInputObserveStart_t CEdgeUiManager_InputObserveStart_Original = nullptr;
EdgeUiSetVisible_t CEdgeUiInput_SetVisible_Original = nullptr;
EdgeUiGetAssignedEdge_t CEdgeUiInput_GetAssignedEdge = nullptr;
EdgeUiRegisterRawInput_t CEdgeUiInput_RegisterRawInput_Original = nullptr;
EdgeUiPointerDown_t CEdgeUiInput_OnPointerDown_Original = nullptr;
EdgeUiPointer_t CEdgeUiInput_OnPointerUpdate_Original = nullptr;
EdgeUiPointer_t CEdgeUiInput_OnPointerUp_Original = nullptr;
EdgeUiMouseDragStart_t CEdgeUiManager_MouseDragStart_Original = nullptr;
EdgeUiIndexFromInput_t CEdgeUiManager_IndexFromEdgeInput_Original = nullptr;
EdgeUiAcquireMouseInvoker_t CEdgeUiManager_AcquireMouseInvokerForEdge_Original = nullptr;
EdgeUiObservedMouseButton_t CEdgeUiInput_ObservedMouseButtonDown_Original = nullptr;
EdgeUiObservedMouseMove_t CEdgeUiInput_ObservedMouseMove_Original = nullptr;
EdgeInvokerPoint_t CEdgeInvoker_ObservedMouseButtonDown_Original = nullptr;
EdgeInvokerMove_t CEdgeInvoker_ObservedMouseMove_Original = nullptr;
EdgeInvokerPoint_t CEdgeInvoker_StartDrag_Original = nullptr;
DerivedInvokerObserve_t CTitleBarInvoker_v_ObservedMouseButtonDown_Original = nullptr;
DerivedInvokerObserve_t CTitleBarInvoker_v_ObservedMouseMove_Original = nullptr;
DerivedInvokerDrag_t CTitleBarInvoker_v_StartDrag_Original = nullptr;
DerivedInvokerDrag_t CTitleBarInvoker_v_ContinueDrag_Original = nullptr;
DerivedInvokerDrag_t CTitleBarInvoker_v_CommitDrag_Original = nullptr;
DerivedInvokerCancel_t CTitleBarInvoker_v_CancelDrag_Original = nullptr;
DerivedInvokerObserve_t CTaskbarInvoker_v_ObservedMouseButtonDown_Original = nullptr;
DerivedInvokerObserve_t CTaskbarInvoker_v_ObservedMouseMove_Original = nullptr;
EdgeUiHandleObservedMouseInput_t CEdgeUiInput_HandleObservedMouseInput_Original = nullptr;
EdgeUiOnMouseDown_t CEdgeUiInput_OnMouseDown_Original = nullptr;
EdgeUiOnMouseUp_t CEdgeUiInput_OnMouseUp_Original = nullptr;
EdgeUiWndProc_t CEdgeUiInput_WndProc_Original = nullptr;
EdgeUiIsMousePen_t CEdgeUiInput_IsCurrentInputFromMouseOrPen_Original = nullptr;
EdgeUiIsTopEdge_t CEdgeUiInput_IsTopEdge_Original = nullptr;
EdgeUiOnMouseMove_t CEdgeUiInput_OnMouseMove_Original = nullptr;
EdgeUiMoveToCornerOrEdge_t CEdgeUiInput_MouseMoveToCornerOrEdge_Original = nullptr;
EdgeUiMouseHitCornerOrEdge_t CEdgeUiManager_MouseHitCornerOrEdge_Original = nullptr;
EdgeInvokerHitCornerOrEdge_t CEdgeInvoker_HitCornerOrEdge_Original = nullptr;
DerivedInvokerInvoke_t CTitleBarInvoker_v_Invoke_Original = nullptr;
StaticBool_t Twinui_TabletModeHelpers_IsTabletMode_Original = nullptr;
StaticBool_t Twinui_TitlebarOverlayHelpers_OverlayTitlebarsInTabletMode_Original = nullptr;
StaticBool_t Pcshell_TitlebarOverlayHelpers_OverlayTitlebarsInTabletMode_Original = nullptr;

// Machine ABI verified from the exact 25H2 disassembly: the static function takes
// WINDOW_MANAGEMENT_BEHAVIOR_SCENARIO in ECX and returns the 8-byte
// BehaviorMaskAndFeatures value in RAX, low dword=mask and high dword=features.
using ComputeMaskAndFeatures_t = uint64_t (__cdecl*)(unsigned int scenario);
ComputeMaskAndFeatures_t ViewPresentationMediator_ComputeMaskAndFeatures_Original = nullptr;

volatile bool* g_disableTitlebarInvocationFlag = nullptr;
volatile bool* g_disableTopLeftFlag = nullptr;
volatile bool* g_disableTopRightFlag = nullptr;
bool g_disableTitlebarInvocationOriginal = false;
bool g_disableTitlebarInvocationCaptured = false;

thread_local bool t_forceTabletWnfForTrySwitch = false;
thread_local bool t_forceTabletWnfForEdgeSetVisible = false;

// On the tested Win10 and Win11 twinui builds the IEdgeUiInput subobject used
// by CEdgeUiManager is +0x18 from the concrete CEdgeUiInput object.
constexpr ptrdiff_t kEdgeUiInputInterfaceOffset = 0x18;

void ApplyLegacyTitlebarMouseInvocationGate(bool active) {
    if (!g_disableTitlebarInvocationFlag ||
        !g_restoreLegacyTitlebarMouseInvocationGate.load(std::memory_order_relaxed)) {
        return;
    }

    if (active) {
        if (!g_disableTitlebarInvocationCaptured) {
            g_disableTitlebarInvocationOriginal = *g_disableTitlebarInvocationFlag;
            g_disableTitlebarInvocationCaptured = true;
        }

        const bool before = *g_disableTitlebarInvocationFlag;
        if (before) {
            *g_disableTitlebarInvocationFlag = false;
            MemoryBarrier();
            const auto n = g_mouseTitlebarGatePatches.fetch_add(1, std::memory_order_relaxed) + 1;
            Wh_Log(L"[tablet-mouse-gate] patch #%lu s_fDisableTitlebarInvocation 1 -> 0 at %p",
                   n, const_cast<bool*>(g_disableTitlebarInvocationFlag));
        } else {
            Wh_Log(L"[tablet-mouse-gate] s_fDisableTitlebarInvocation already 0 at %p",
                   const_cast<bool*>(g_disableTitlebarInvocationFlag));
        }
    } else if (g_disableTitlebarInvocationCaptured) {
        const bool before = *g_disableTitlebarInvocationFlag;
        *g_disableTitlebarInvocationFlag = g_disableTitlebarInvocationOriginal;
        MemoryBarrier();
        Wh_Log(L"[tablet-mouse-gate] restore s_fDisableTitlebarInvocation %d -> %d at %p",
               before ? 1 : 0, g_disableTitlebarInvocationOriginal ? 1 : 0,
               const_cast<bool*>(g_disableTitlebarInvocationFlag));
        g_disableTitlebarInvocationCaptured = false;
    }
}

// On the tested Win10 and Win11 twinui builds the
// IImmersiveRawInputMouseNotification subobject used by ObservedMouse* is +0x30
// from the CEdgeUiInput object consumed by _OnMouseDown/_OnMouseMove/_OnMouseUp.
constexpr ptrdiff_t kObservedMouseInterfaceOffset = 0x30;
constexpr LONG kLegacyMouseBridgeTopEdgePixels = 32;
thread_local bool t_legacyMouseBridgeActive = false;
thread_local void* t_legacyMouseBridgeInput = nullptr;
thread_local POINT t_legacyMouseBridgeLastPoint{};
thread_local void* t_lastObservedConcreteInput = nullptr;
thread_local void* t_lastObservedEdgeInput = nullptr;
thread_local void* t_lastObservedInvoker = nullptr;
thread_local bool t_legacyMouseBridgeFromInvoker = false;
thread_local bool t_mouseHitScopeActive = false;
thread_local bool t_mouseHitScopePhysicalTop = false;
thread_local void* t_mouseHitScopeEdgeInput = nullptr;

bool IsPointAtMonitorTopEdge(POINT point) {
    HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    if (!monitor) return false;

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return false;

    return point.x >= info.rcMonitor.left &&
           point.x < info.rcMonitor.right &&
           point.y >= info.rcMonitor.top - 1 &&
           point.y <= info.rcMonitor.top + kLegacyMouseBridgeTopEdgePixels;
}

void FinishLegacyMouseBridge(POINT point, PCWSTR reason) {
    if (!t_legacyMouseBridgeActive) return;

    void* input = t_legacyMouseBridgeInput;
    t_legacyMouseBridgeActive = false;
    t_legacyMouseBridgeInput = nullptr;
    t_legacyMouseBridgeFromInvoker = false;
    t_legacyMouseBridgeLastPoint = point;

    const auto n = g_mouseBridgeUpCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse-bridge] finish #%lu input=%p pt=(%ld,%ld) reason=%s tid=%lu",
           n, input, point.x, point.y, reason ? reason : L"(none)",
           GetCurrentThreadId());

    if (input && CEdgeUiInput_OnMouseUp_Original) {
        CEdgeUiInput_OnMouseUp_Original(input, point);
    }
}

bool ArmLegacyMouseBridgeFromInvoker(void* invoker, POINT point, PCWSTR reason) {
    if (!g_bridgeLegacyMouseWin32Path.load(std::memory_order_relaxed) ||
        !g_restoreLegacyMouseDrag.load(std::memory_order_relaxed) ||
        !g_compatActive.load(std::memory_order_acquire) ||
        !t_lastObservedConcreteInput || !t_lastObservedInvoker ||
        invoker != t_lastObservedInvoker ||
        !IsPointAtMonitorTopEdge(point) ||
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0 ||
        !CEdgeUiInput_OnMouseDown_Original ||
        !CEdgeUiInput_OnMouseMove_Original ||
        !CEdgeUiInput_OnMouseUp_Original) {
        return false;
    }

    if (t_legacyMouseBridgeActive) {
        return t_legacyMouseBridgeInput == t_lastObservedConcreteInput;
    }

    t_legacyMouseBridgeActive = true;
    t_legacyMouseBridgeFromInvoker = true;
    t_legacyMouseBridgeInput = t_lastObservedConcreteInput;
    t_legacyMouseBridgeLastPoint = point;

    const auto n =
        g_mouseBridgeDownCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse-bridge2] arm-from-invoker #%lu invoker=%p edgeInput=%p input=%p pt=(%ld,%ld) reason=%s -> _OnMouseDown(raw=0) tid=%lu",
           n, invoker, t_lastObservedEdgeInput, t_legacyMouseBridgeInput,
           point.x, point.y, reason ? reason : L"(none)", GetCurrentThreadId());

    CEdgeUiInput_OnMouseDown_Original(t_legacyMouseBridgeInput, point, false);
    return true;
}

void LoadSettings() {
    g_enableRealTabletMode = Wh_GetIntSetting(L"enableRealTabletMode") != 0;
    g_bypassAvailability = Wh_GetIntSetting(L"bypassAvailabilityGate") != 0;

    int minAge = Wh_GetIntSetting(L"minimumExplorerAgeMs");
    if (minAge < 0) minAge = 0;
    if (minAge > 120000) minAge = 120000;
    g_minimumExplorerAgeMs = static_cast<DWORD>(minAge);

    int stable = Wh_GetIntSetting(L"shellStableMs");
    if (stable < 0) stable = 0;
    if (stable > 30000) stable = 30000;
    g_shellStableMs = static_cast<DWORD>(stable);

    int delay = Wh_GetIntSetting(L"startupDelayMs");
    if (delay < 0) delay = 0;
    if (delay > 30000) delay = 30000;
    g_startupDelayMs = static_cast<DWORD>(delay);

    int trigger = Wh_GetIntSetting(L"modeTrigger");
    if (trigger < 0) trigger = 4;
    g_modeTrigger = trigger;

    g_restoreOnUnload = Wh_GetIntSetting(L"restoreOnUnload") != 0;
    g_forceExplorerTablet = Wh_GetIntSetting(L"forceExplorerTabletMode") != 0;
    g_forceExplorerImmersive = Wh_GetIntSetting(L"forceExplorerImmersiveMode") != 0;
    g_forceExplorerPatcherTablet =
        Wh_GetIntSetting(L"forceExplorerPatcherTabletMode") != 0;
    g_restoreStartAppSwitchBehavior =
        Wh_GetIntSetting(L"restoreStartAppSwitchBehavior") != 0;
    g_forceTouchFriendlyTaskButtons =
        Wh_GetIntSetting(L"forceTouchFriendlyTaskButtons") != 0;

    int epWait = Wh_GetIntSetting(L"explorerPatcherWaitMs");
    if (epWait < 1000) epWait = 1000;
    if (epWait > 120000) epWait = 120000;
    g_epWaitMs = static_cast<DWORD>(epWait);

    g_refreshTaskbarAfterModeTransition =
        Wh_GetIntSetting(L"refreshTaskbarAfterModeTransition") != 0;
    int refreshDelay = Wh_GetIntSetting(L"taskbarRefreshDelayMs");
    if (refreshDelay < 0) refreshDelay = 0;
    if (refreshDelay > 2000) refreshDelay = 2000;
    g_taskbarRefreshDelayMs = static_cast<DWORD>(refreshDelay);
    g_traceLegacyTabletWindowGestures =
        Wh_GetIntSetting(L"traceLegacyTabletWindowGestures") != 0;
    g_traceDesktopDragIdentity =
        Wh_GetIntSetting(L"traceDesktopDragIdentity") != 0;
    g_restoreLegacyEdgeSwipeGate =
        Wh_GetIntSetting(L"restoreLegacyEdgeSwipeGate") != 0;
    g_restoreLegacyMouseDrag =
        Wh_GetIntSetting(L"restoreLegacyMouseDrag") != 0;
    g_bridgeLegacyMouseWin32Path =
        Wh_GetIntSetting(L"bridgeLegacyMouseWin32Path") != 0;
    g_restoreLegacyTitlebarMouseInvocationGate =
        Wh_GetIntSetting(L"restoreLegacyTitlebarMouseInvocationGate") != 0;
    g_restoreTabletTitlebarOverlay =
        Wh_GetIntSetting(L"restoreTabletTitlebarOverlay") != 0;
    g_restoreLegacyWindowManagementProfile =
        Wh_GetIntSetting(L"restoreLegacyWindowManagementProfile") != 0;

    g_restoreModernPerWindowMoveSizeIntercept =
        Wh_GetIntSetting(L"restoreModernPerWindowMoveSizeIntercept") != 0;

    g_verbose = Wh_GetIntSetting(L"verbose") != 0;
}

void LogDesktopWindowIdentity(const wchar_t* reason, HWND hwnd, POINT cursor) {
    HWND hit = hwnd;
    HWND root = hit ? GetAncestor(hit, GA_ROOT) : nullptr;
    if (!root) root = hit;

    wchar_t className[128] = L"<none>";
    wchar_t title[256] = L"";
    DWORD pid = 0;
    DWORD tid = 0;
    RECT rect = {};
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(wp);

    if (root && IsWindow(root)) {
        if (!GetClassNameW(root, className, ARRAYSIZE(className))) {
            wcscpy_s(className, ARRAYSIZE(className), L"<unknown>");
        }
        GetWindowTextW(root, title, ARRAYSIZE(title));
        tid = GetWindowThreadProcessId(root, &pid);
        GetWindowRect(root, &rect);
        if (!GetWindowPlacement(root, &wp)) {
            ZeroMemory(&wp, sizeof(wp));
            wp.length = sizeof(wp);
        }
    }

    wchar_t imagePath[512] = L"<unavailable>";
    if (pid) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            DWORD cch = ARRAYSIZE(imagePath);
            if (!QueryFullProcessImageNameW(process, 0, imagePath, &cch)) {
                wcscpy_s(imagePath, ARRAYSIZE(imagePath), L"<unavailable>");
            }
            CloseHandle(process);
        }
    }
    const wchar_t* imageName = wcsrchr(imagePath, L'\\');
    imageName = imageName ? imageName + 1 : imagePath;

    Wh_Log(L"[tablet-dragid] %s cursor=(%ld,%ld) hit=%p root=%p class='%s' image='%s' title='%s' pid=%lu tid=%lu rect=(%ld,%ld)-(%ld,%ld) showCmd=%u maximized=%d",
           reason, cursor.x, cursor.y, hit, root, className, imageName, title,
           pid, tid, rect.left, rect.top, rect.right, rect.bottom,
           wp.showCmd, root && IsZoomed(root) ? 1 : 0);
}

DWORD WINAPI DesktopDragIdentityThreadProc(void*) {
    bool previousDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    HWND downRoot = nullptr;
    DWORD downThreadId = 0;
    HWND lastMoveSize = nullptr;

    Wh_Log(L"[tablet-dragid] read-only drag identity observer started");

    while (WaitForSingleObject(g_stopEvent, 10) == WAIT_TIMEOUT) {
        const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

        if (down && !previousDown) {
            POINT cursor = {};
            GetCursorPos(&cursor);
            HWND hit = WindowFromPoint(cursor);
            downRoot = hit ? GetAncestor(hit, GA_ROOT) : nullptr;
            if (!downRoot) downRoot = hit;
            downThreadId = downRoot ? GetWindowThreadProcessId(downRoot, nullptr) : 0;
            lastMoveSize = nullptr;
            g_dragIdentityDownCalls.fetch_add(1, std::memory_order_relaxed);
            LogDesktopWindowIdentity(L"LBUTTON_DOWN", hit, cursor);
        }

        if (down && downThreadId) {
            GUITHREADINFO gti = {};
            gti.cbSize = sizeof(gti);
            if (GetGUIThreadInfo(downThreadId, &gti) &&
                gti.hwndMoveSize && gti.hwndMoveSize != lastMoveSize) {
                lastMoveSize = gti.hwndMoveSize;
                POINT cursor = {};
                GetCursorPos(&cursor);
                const auto n = g_dragIdentityMoveSizeCalls.fetch_add(
                                   1, std::memory_order_relaxed) + 1;
                Wh_Log(L"[tablet-dragid] native USER move-size observed #%lu capture=%p focus=%p active=%p menuOwner=%p",
                       n, gti.hwndCapture, gti.hwndFocus, gti.hwndActive,
                       gti.hwndMenuOwner);
                LogDesktopWindowIdentity(L"HWND_MOVESIZE", gti.hwndMoveSize,
                                         cursor);
            }
        }

        if (!down && previousDown) {
            POINT cursor = {};
            GetCursorPos(&cursor);
            g_dragIdentityUpCalls.fetch_add(1, std::memory_order_relaxed);
            LogDesktopWindowIdentity(L"LBUTTON_UP_ORIGINAL_TARGET", downRoot,
                                     cursor);
            HWND currentHit = WindowFromPoint(cursor);
            HWND currentRoot = currentHit ? GetAncestor(currentHit, GA_ROOT) : nullptr;
            if (!currentRoot) currentRoot = currentHit;
            if (currentRoot != downRoot) {
                LogDesktopWindowIdentity(L"LBUTTON_UP_UNDER_CURSOR", currentHit,
                                         cursor);
            }
            downRoot = nullptr;
            downThreadId = 0;
            lastMoveSize = nullptr;
        }

        previousDown = down;
    }

    Wh_Log(L"[tablet-dragid] read-only drag identity observer stopped");
    return 0;
}

bool CurrentProcessOwnsShell(bool logFailure) {
    HWND shellWindow = GetShellWindow();
    if (!shellWindow) {
        if (logFailure) {
            Wh_Log(L"[tablet] GetShellWindow returned null; shell is not ready");
        }
        return false;
    }

    DWORD shellPid = 0;
    GetWindowThreadProcessId(shellWindow, &shellPid);
    if (shellPid != GetCurrentProcessId()) {
        if (logFailure) {
            Wh_Log(L"[tablet] shell belongs to explorer PID=%lu; this PID=%lu",
                   shellPid, GetCurrentProcessId());
        }
        return false;
    }

    return true;
}

ULONGLONG GetCurrentProcessAgeMs() {
    FILETIME creation{}, exitTime{}, kernel{}, user{}, now{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user)) {
        return ~0ULL;
    }

    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER created{};
    created.LowPart = creation.dwLowDateTime;
    created.HighPart = creation.dwHighDateTime;
    ULARGE_INTEGER current{};
    current.LowPart = now.dwLowDateTime;
    current.HighPart = now.dwHighDateTime;

    if (current.QuadPart < created.QuadPart) return ~0ULL;
    return (current.QuadPart - created.QuadPart) / 10000ULL;
}

bool ShellInitializationLooksReady() {
    HWND shellWindow = GetShellWindow();
    HWND trayWindow = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!shellWindow || !trayWindow || !GetModuleHandleW(L"twinui.pcshell.dll")) {
        return false;
    }

    DWORD shellPid = 0;
    DWORD trayPid = 0;
    GetWindowThreadProcessId(shellWindow, &shellPid);
    GetWindowThreadProcessId(trayWindow, &trayPid);
    DWORD thisPid = GetCurrentProcessId();
    return shellPid == thisPid && trayPid == thisPid;
}

bool WaitForSafeShellInitialization() {
    DWORD minAge = g_minimumExplorerAgeMs.load();
    ULONGLONG age = GetCurrentProcessAgeMs();
    if (age != ~0ULL && age < minAge) {
        DWORD waitMs = static_cast<DWORD>(minAge - age);
        Wh_Log(L"[tablet] fresh Explorer detected (age=%llu ms); deferring real Tablet Mode for %lu ms",
               static_cast<unsigned long long>(age), waitMs);
        if (WaitForSingleObject(g_stopEvent, waitMs) != WAIT_TIMEOUT) {
            return false;
        }
    }

    constexpr DWORD kPollMs = 100;
    const DWORD stableTarget = g_shellStableMs.load();
    DWORD stableMs = 0;

    for (;;) {
        if (WaitForSingleObject(g_stopEvent, 0) != WAIT_TIMEOUT) return false;

        HWND shellOwnerWindow = GetShellWindow();
        if (shellOwnerWindow) {
            DWORD shellOwnerPid = 0;
            GetWindowThreadProcessId(shellOwnerWindow, &shellOwnerPid);
            if (shellOwnerPid && shellOwnerPid != GetCurrentProcessId()) {
                Wh_Log(L"[tablet] shell belongs to another explorer.exe (PID=%lu); controller worker stopping",
                       shellOwnerPid);
                return false;
            }
        }

        if (ShellInitializationLooksReady()) {
            if (stableTarget == 0) {
                Wh_Log(L"[tablet] shell startup safety gate passed (Explorer age=%llu ms)",
                       static_cast<unsigned long long>(GetCurrentProcessAgeMs()));
                return true;
            }

            stableMs += kPollMs;
            if (stableMs >= stableTarget) {
                Wh_Log(L"[tablet] shell startup safety gate passed (Explorer age=%llu ms, shell stable=%lu ms)",
                       static_cast<unsigned long long>(GetCurrentProcessAgeMs()),
                       stableMs);
                return true;
            }
        } else {
            stableMs = 0;
        }

        if (WaitForSingleObject(g_stopEvent, kPollMs) != WAIT_TIMEOUT) return false;
    }
}

HRESULT GetTabletModeController(ITabletModeController** controller) {
    if (!controller) return E_POINTER;
    *controller = nullptr;

    IServiceProvider* serviceProvider = nullptr;
    HRESULT hr = CoCreateInstance(kClsidImmersiveShell, nullptr,
                                  CLSCTX_LOCAL_SERVER,
                                  kIidServiceProvider,
                                  reinterpret_cast<void**>(&serviceProvider));
    if (FAILED(hr)) return hr;

    hr = serviceProvider->QueryService(
        kIidTabletModeController,
        kIidTabletModeController,
        reinterpret_cast<void**>(controller));
    serviceProvider->Release();
    return hr;
}

bool IsValidated25H2PcshellLoaded() {
    HMODULE module = GetModuleHandleW(L"twinui.pcshell.dll");
    if (!module) return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<BYTE*>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    return nt->FileHeader.TimeDateStamp == kExpectedPcshellTimestamp &&
           nt->OptionalHeader.SizeOfImage == kExpectedPcshellSizeOfImage;
}

HRESULT GetPrivilegedArrangementOperations(IUnknown** operations) {
    if (!operations) return E_POINTER;
    *operations = nullptr;

    IServiceProvider* serviceProvider = nullptr;
    HRESULT hr = CoCreateInstance(kClsidImmersiveShell, nullptr,
                                  CLSCTX_LOCAL_SERVER,
                                  kIidServiceProvider,
                                  reinterpret_cast<void**>(&serviceProvider));
    if (FAILED(hr)) return hr;

    // Exact 25H2 GamingPosturePositioner::subscribe_to_services pattern:
    // QueryService uses this same GUID as both SID and IID. The resulting
    // interface is the real IPrivilegedArrangementOperations service object.
    hr = serviceProvider->QueryService(
        kIidPrivilegedArrangementOperations,
        kIidPrivilegedArrangementOperations,
        reinterpret_cast<void**>(operations));
    serviceProvider->Release();
    return hr;
}

using EnablePerWindowShellBehavior_t =
    HRESULT(STDMETHODCALLTYPE*)(IUnknown* operations, HWND hwnd,
                                unsigned int mask, unsigned int features);

HRESULT SetPerWindowMoveSizeIntercept(IUnknown* operations, HWND hwnd,
                                      bool enable) {
    if (!operations || !hwnd || !IsWindow(hwnd)) return E_INVALIDARG;

    void*** object = reinterpret_cast<void***>(operations);
    if (!object || !*object) return E_POINTER;

    // Exact 25H2 IPrivilegedArrangementOperations vtable slot used by
    // GamingPosturePositioner::SetInterceptMoveAndSize: +0x40 == slot 8.
    // This is a normal interface call through the real queried object; no
    // vtable entry is modified.
    auto enableBehavior = reinterpret_cast<EnablePerWindowShellBehavior_t>(
        (*object)[8]);
    if (!enableBehavior) return E_POINTER;

    constexpr unsigned int kInterceptMoveAndSizeMask = 0x6;
    const unsigned int features = enable ? 0x6u : 0u;
    return enableBehavior(operations, hwnd, kInterceptMoveAndSizeMask, features);
}

bool IsPerWindowInterceptCandidate(HWND hwnd) {
    if (!hwnd) return false;
    hwnd = GetAncestor(hwnd, GA_ROOT);
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == GetCurrentProcessId()) return false;

    // Keep this first experiment deliberately narrow: Win10's target case is
    // dragging a maximized desktop app. Do not alter normal/restored windows.
    if (!IsZoomed(hwnd)) return false;

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & WS_CAPTION) == 0) return false;

    return true;
}

void LogPerWindowInterceptTarget(const wchar_t* action, HWND hwnd, HRESULT hr) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    wchar_t cls[128] = L"";
    wchar_t title[256] = L"";
    if (hwnd && IsWindow(hwnd)) {
        GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
        GetWindowTextW(hwnd, title, ARRAYSIZE(title));
    }
    Wh_Log(L"[tablet-window-intercept] %s hwnd=%p pid=%lu class='%s' title='%s' mask=0x00000006 features=0x%08X hr=0x%08X",
           action, hwnd, pid, cls, title,
           wcscmp(action, L"APPLY") == 0 ? 6u : 0u,
           static_cast<unsigned>(hr));
}

DWORD WINAPI PerWindowMoveSizeInterceptThreadProc(void*) {
    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(coHr);
    if (coHr == RPC_E_CHANGED_MODE) coHr = S_OK;
    if (FAILED(coHr)) {
        Wh_Log(L"[tablet-window-intercept] CoInitializeEx failed: 0x%08X",
               static_cast<unsigned>(coHr));
        return 0;
    }

    IUnknown* operations = nullptr;
    HWND appliedHwnd = nullptr;
    ULONGLONG nextQueryTick = 0;
    bool buildReadyLogged = false;

    Wh_Log(L"[tablet-window-intercept] worker started; setting=1 scope=foreground-maximized-non-Explorer mask=0x6 features=0x6");

    while (WaitForSingleObject(g_stopEvent, 50) == WAIT_TIMEOUT) {
        const bool active =
            g_restoreModernPerWindowMoveSizeIntercept.load(std::memory_order_relaxed) &&
            g_compatActive.load(std::memory_order_acquire);

        const bool exactBuild = g_controllerBuildSupported &&
                                IsValidated25H2PcshellLoaded();
        if (active && exactBuild && !buildReadyLogged) {
            buildReadyLogged = true;
            Wh_Log(L"[tablet-window-intercept] exact 25H2 twinui/twinui.pcshell pair verified");
        }

        if (active && exactBuild && !operations && GetTickCount64() >= nextQueryTick) {
            HRESULT hr = GetPrivilegedArrangementOperations(&operations);
            if (FAILED(hr) || !operations) {
                const auto n = g_perWindowInterceptQueryFailures.fetch_add(
                                   1, std::memory_order_relaxed) + 1;
                if (n <= 10 || g_verbose.load(std::memory_order_relaxed)) {
                    Wh_Log(L"[tablet-window-intercept] QueryService(IPrivilegedArrangementOperations) failed #%lu hr=0x%08X",
                           n, static_cast<unsigned>(hr));
                }
                operations = nullptr;
                nextQueryTick = GetTickCount64() + 1000;
            } else {
                Wh_Log(L"[tablet-window-intercept] queried real IPrivilegedArrangementOperations service=%p",
                       operations);
            }
        }

        HWND candidate = nullptr;
        if (active && exactBuild && operations) {
            candidate = GetForegroundWindow();
            candidate = candidate ? GetAncestor(candidate, GA_ROOT) : nullptr;
            if (!IsPerWindowInterceptCandidate(candidate)) candidate = nullptr;
        }

        if (candidate != appliedHwnd) {
            if (appliedHwnd && operations) {
                HRESULT hr = SetPerWindowMoveSizeIntercept(operations, appliedHwnd, false);
                g_perWindowInterceptRemoveCalls.fetch_add(1, std::memory_order_relaxed);
                LogPerWindowInterceptTarget(L"REMOVE", appliedHwnd, hr);
            }
            appliedHwnd = nullptr;

            if (candidate && operations) {
                HRESULT hr = SetPerWindowMoveSizeIntercept(operations, candidate, true);
                g_perWindowInterceptApplyCalls.fetch_add(1, std::memory_order_relaxed);
                LogPerWindowInterceptTarget(L"APPLY", candidate, hr);
                if (SUCCEEDED(hr)) appliedHwnd = candidate;
            }
        }
    }

    if (appliedHwnd && operations) {
        HRESULT hr = SetPerWindowMoveSizeIntercept(operations, appliedHwnd, false);
        g_perWindowInterceptRemoveCalls.fetch_add(1, std::memory_order_relaxed);
        LogPerWindowInterceptTarget(L"REMOVE-UNLOAD", appliedHwnd, hr);
    }
    if (operations) operations->Release();
    if (shouldUninitialize) CoUninitialize();

    Wh_Log(L"[tablet-window-intercept] worker stopped apply=%lu remove=%lu queryFailures=%lu",
           g_perWindowInterceptApplyCalls.load(std::memory_order_relaxed),
           g_perWindowInterceptRemoveCalls.load(std::memory_order_relaxed),
           g_perWindowInterceptQueryFailures.load(std::memory_order_relaxed));
    return 0;
}

bool PrepareAndVerifyTwinui() {
    g_twinui = GetModuleHandleW(L"twinui.dll");
    if (!g_twinui) {
        g_twinui = LoadLibraryExW(L"twinui.dll", nullptr,
                                  LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!g_twinui) g_twinui = LoadLibraryW(L"twinui.dll");
        if (g_twinui) g_loadedTwinui = true;
    }

    if (!g_twinui) {
        Wh_Log(L"[tablet] couldn't load twinui.dll: %lu", GetLastError());
        return false;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(g_twinui);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<BYTE*>(g_twinui) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    DWORD timestamp = nt->FileHeader.TimeDateStamp;
    DWORD imageSize = nt->OptionalHeader.SizeOfImage;
    Wh_Log(L"[tablet] twinui image=%p timestamp=0x%08X size=0x%08X",
           g_twinui, timestamp, imageSize);

    if (timestamp != kExpectedTwinuiTimestamp ||
        imageSize != kExpectedTwinuiSizeOfImage) {
        Wh_Log(L"[tablet] controller availability patch disabled: unsupported twinui.dll image");
        return false;
    }

    return true;
}

volatile BYTE* AvailabilityByte(ITabletModeController* controller) {
    return reinterpret_cast<volatile BYTE*>(
        reinterpret_cast<BYTE*>(controller) +
        kAvailabilityOffsetFromITabletModeController);
}

BYTE ReadAvailability(ITabletModeController* controller) {
    return *AvailabilityByte(controller);
}

void WriteAvailability(ITabletModeController* controller, BYTE value) {
    *AvailabilityByte(controller) = value;
    MemoryBarrier();
}

void SetCompatActive(bool active, PCWSTR reason);
void RefreshTaskbarLayoutAfterModeTransition(PCWSTR reason);

HRESULT ApplyControllerMode(int targetMode, bool rememberOriginal,
                            bool allowAvailabilityBypass) {
    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool shouldUninitialize = SUCCEEDED(coHr);
    if (coHr == RPC_E_CHANGED_MODE) coHr = S_OK;
    if (FAILED(coHr)) return coHr;

    ITabletModeController* controller = nullptr;
    HRESULT hr = GetTabletModeController(&controller);
    if (FAILED(hr) || !controller) {
        Wh_Log(L"[tablet] QueryService(ITabletModeController) failed: 0x%08X",
               static_cast<unsigned>(hr));
        if (shouldUninitialize) CoUninitialize();
        return hr;
    }

    int before = -1;
    HRESULT getBeforeHr = controller->GetMode(&before);
    BYTE availabilityBefore = ReadAvailability(controller);

    Wh_Log(L"[tablet] controller=%p GetMode before: hr=0x%08X mode=%d cachedAvailability=%u",
           controller, static_cast<unsigned>(getBeforeHr), before,
           static_cast<unsigned>(availabilityBefore));

    if (SUCCEEDED(getBeforeHr) && rememberOriginal && g_originalMode.load() < 0) {
        g_originalMode = before;
        g_originalAvailability = availabilityBefore;
    }

    if (FAILED(getBeforeHr)) {
        controller->Release();
        if (shouldUninitialize) CoUninitialize();
        return getBeforeHr;
    }

    if (before == targetMode) {
        SetCompatActive(targetMode == 1,
                        L"controller already in requested mode");
        Wh_Log(L"[tablet] controller is already in requested mode %d; compatibility layer=%d",
               targetMode, g_compatActive.load() ? 1 : 0);
        controller->Release();
        if (shouldUninitialize) CoUninitialize();
        return S_OK;
    }

    bool availabilityWasPatched = false;
    if (targetMode == 1 && allowAvailabilityBypass &&
        g_bypassAvailability.load() && availabilityBefore == 0) {
        WriteAvailability(controller, 1);
        availabilityWasPatched = true;
        Wh_Log(L"[tablet] patched cached Tablet Mode availability 0 -> 1 at interface +0x%llX",
               static_cast<unsigned long long>(
                   kAvailabilityOffsetFromITabletModeController));
    }

    int trigger = g_modeTrigger.load();

    // IMPORTANT: SetMode broadcasts the real shell-mode transition before it
    // returns. CTray and ExplorerPatcher can query Tablet Mode from inside
    // those callbacks. Pre-arm the compatibility layer so the transition is
    // observed consistently instead of letting the shell cache Desktop mode
    // and only notice our hooks after a later taskbar resize.
    const bool compatBefore =
        g_compatActive.load(std::memory_order_acquire);
    if (targetMode == 1) {
        SetCompatActive(true, L"pre-armed for real SetMode transition");
    } else {
        SetCompatActive(false, L"pre-armed for leaving Tablet Mode");
    }

    Wh_Log(L"[tablet] calling real SetMode(%d, trigger=%d)", targetMode, trigger);
    hr = controller->SetMode(targetMode, trigger);
    Wh_Log(L"[tablet] SetMode returned 0x%08X", static_cast<unsigned>(hr));

    int after = -1;
    HRESULT getAfterHr = controller->GetMode(&after);
    Wh_Log(L"[tablet] GetMode after: hr=0x%08X mode=%d cachedAvailability=%u",
           static_cast<unsigned>(getAfterHr), after,
           static_cast<unsigned>(ReadAvailability(controller)));

    bool succeeded = SUCCEEDED(hr) && SUCCEEDED(getAfterHr) &&
                     after == targetMode;

    if (succeeded) {
        // Already pre-armed before SetMode so the transition callbacks saw the
        // intended state. Keep that state now that the controller confirms it.
        SetCompatActive(targetMode == 1,
                        L"real controller transition confirmed");

        if (rememberOriginal && before != targetMode) {
            g_changedMode = true;
            if (availabilityWasPatched) {
                g_leftAvailabilityForced = true;
            }
        }

        Wh_Log(L"[tablet] real Tablet Mode transition succeeded; compatibility layer=%d",
               g_compatActive.load() ? 1 : 0);
        RefreshTaskbarLayoutAfterModeTransition(
            targetMode == 1 ? L"after entering Tablet Mode"
                            : L"after leaving Tablet Mode");
    } else {
        SetCompatActive(compatBefore,
                        L"real SetMode failed; rolled compatibility state back");

        if (availabilityWasPatched) {
            WriteAvailability(controller, availabilityBefore);
            Wh_Log(L"[tablet] transition failed; restored cached availability to %u",
                   static_cast<unsigned>(availabilityBefore));
        }
    }

    controller->Release();
    if (shouldUninitialize) CoUninitialize();
    return hr;
}

// Implemented below with the pcshell-dependent hook installers.
bool NeedDeferredPcshellHooks();
bool InstallDeferredPcshellHooksIfReady();

bool IsStartupTransientHr(HRESULT hr) {
    return hr == REGDB_E_CLASSNOTREG ||
           hr == CO_E_SERVER_EXEC_FAILURE ||
           hr == RPC_S_SERVER_UNAVAILABLE ||
           hr == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE);
}

DWORD WINAPI ControllerThreadProc(void*) {
    if (WaitForSingleObject(g_stopEvent, g_startupDelayMs.load()) != WAIT_TIMEOUT) {
        return 0;
    }

    if (!WaitForSafeShellInitialization()) return 0;

    constexpr int kRetries = 12;
    for (int attempt = 1; attempt <= kRetries; ++attempt) {
        if (WaitForSingleObject(g_stopEvent, 0) != WAIT_TIMEOUT) return 0;

        if (!CurrentProcessOwnsShell(false)) {
            HWND shell = GetShellWindow();
            if (shell) {
                DWORD shellPid = 0;
                GetWindowThreadProcessId(shell, &shellPid);
                if (shellPid && shellPid != GetCurrentProcessId()) {
                    Wh_Log(L"[tablet] this explorer.exe does not own the shell; controller worker stopping");
                    return 0;
                }
            }
        } else {
            // A fresh Explorer can receive the Windhawk mod before
            // twinui.pcshell.dll is loaded. Give the serialized post-init
            // worker a short opportunity to queue/apply its pcshell hooks
            // before SetMode(1), so the initial positioner handoff sees the
            // same titlebar/trace hooks as a hot-reload on an existing shell.
            if (NeedDeferredPcshellHooks()) {
                const ULONGLONG pcshellDeadline = GetTickCount64() + 2000;
                while (NeedDeferredPcshellHooks() &&
                       GetTickCount64() < pcshellDeadline) {
                    if (WaitForSingleObject(g_stopEvent, 50) != WAIT_TIMEOUT) {
                        return 0;
                    }
                }
                Wh_Log(L"[tablet-pcshell] before SetMode deferredReady=%d pcshellLoaded=%d",
                       NeedDeferredPcshellHooks() ? 0 : 1,
                       GetModuleHandleW(L"twinui.pcshell.dll") ? 1 : 0);
            }

            HRESULT hr = ApplyControllerMode(1, true, true);
            if (SUCCEEDED(hr)) return 0;
            if (!IsStartupTransientHr(hr)) {
                Wh_Log(L"[tablet] SetMode failed with non-transient HRESULT 0x%08X; not retrying",
                       static_cast<unsigned>(hr));
                return 0;
            }
        }

        if (attempt != kRetries &&
            WaitForSingleObject(g_stopEvent, 500) != WAIT_TIMEOUT) {
            return 0;
        }
    }

    Wh_Log(L"[tablet] controller worker gave up after startup retries");
    return 0;
}


enum class TrayDockEdge {
    Unknown,
    Left,
    Top,
    Right,
    Bottom,
};

PCWSTR TrayDockEdgeName(TrayDockEdge edge) {
    switch (edge) {
        case TrayDockEdge::Left: return L"left";
        case TrayDockEdge::Top: return L"top";
        case TrayDockEdge::Right: return L"right";
        case TrayDockEdge::Bottom: return L"bottom";
        default: return L"unknown";
    }
}

TrayDockEdge DetectTrayDockEdge(HWND tray, const RECT& trayRect) {
    HMONITOR monitor = MonitorFromWindow(tray, MONITOR_DEFAULTTONEAREST);
    if (!monitor) return TrayDockEdge::Unknown;

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) return TrayDockEdge::Unknown;

    const auto absLong = [](LONG value) -> LONG {
        return value < 0 ? -value : value;
    };
    const LONG dLeft = absLong(trayRect.left - mi.rcMonitor.left);
    const LONG dTop = absLong(trayRect.top - mi.rcMonitor.top);
    const LONG dRight = absLong(mi.rcMonitor.right - trayRect.right);
    const LONG dBottom = absLong(mi.rcMonitor.bottom - trayRect.bottom);

    const LONG width = trayRect.right - trayRect.left;
    const LONG height = trayRect.bottom - trayRect.top;

    // A docked taskbar normally touches two or three monitor edges, so choosing
    // the globally nearest edge is ambiguous (for example a right-docked tray
    // also touches the monitor's top and bottom edges). Determine orientation
    // first, then compare only the two meaningful docking edges.
    if (height > width) {
        return dRight <= dLeft ? TrayDockEdge::Right : TrayDockEdge::Left;
    }
    if (width > height) {
        return dBottom <= dTop ? TrayDockEdge::Bottom : TrayDockEdge::Top;
    }

    // Degenerate/square geometry: fall back to the nearest monitor edge.
    LONG best = dLeft;
    TrayDockEdge edge = TrayDockEdge::Left;
    if (dTop < best) {
        best = dTop;
        edge = TrayDockEdge::Top;
    }
    if (dRight < best) {
        best = dRight;
        edge = TrayDockEdge::Right;
    }
    if (dBottom < best) {
        edge = TrayDockEdge::Bottom;
    }
    return edge;
}

void RefreshTaskbarLayoutAfterModeTransition(PCWSTR reason) {
    if (!g_refreshTaskbarAfterModeTransition.load(std::memory_order_relaxed)) {
        return;
    }

    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) {
        Wh_Log(L"[tablet-refresh] Shell_TrayWnd not found (%s)",
               reason ? reason : L"no reason");
        return;
    }

    DWORD trayPid = 0;
    GetWindowThreadProcessId(tray, &trayPid);
    if (trayPid != GetCurrentProcessId()) {
        Wh_Log(L"[tablet-refresh] Shell_TrayWnd belongs to PID=%lu; current PID=%lu; skipping (%s)",
               trayPid, GetCurrentProcessId(),
               reason ? reason : L"no reason");
        return;
    }

    const DWORD delay =
        g_taskbarRefreshDelayMs.load(std::memory_order_relaxed);
    if (delay) Sleep(delay);

    RECT windowRect{};
    RECT clientRect{};
    if (!GetWindowRect(tray, &windowRect) || !GetClientRect(tray, &clientRect)) {
        Wh_Log(L"[tablet-refresh] couldn't read Shell_TrayWnd geometry (%s) err=%lu",
               reason ? reason : L"no reason", GetLastError());
        return;
    }

    const int windowWidth = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;
    const int clientWidth = clientRect.right - clientRect.left;
    const int clientHeight = clientRect.bottom - clientRect.top;
    const TrayDockEdge edge = DetectTrayDockEdge(tray, windowRect);

    // v0.1.1 sent a same-size WM_SIZE/FRAMECHANGED pass. The tray accepted all
    // messages, but the Back/Search/Task View geometry stayed cached until the
    // user physically resized the taskbar. Reproduce that proven trigger with
    // a one-pixel *real* geometry pulse and immediately restore the exact rect.
    int pulseX = windowRect.left;
    int pulseY = windowRect.top;
    int pulseWidth = windowWidth;
    int pulseHeight = windowHeight;

    switch (edge) {
        case TrayDockEdge::Right:
            --pulseX;
            ++pulseWidth;
            break;
        case TrayDockEdge::Left:
            ++pulseWidth;
            break;
        case TrayDockEdge::Bottom:
            --pulseY;
            ++pulseHeight;
            break;
        case TrayDockEdge::Top:
            ++pulseHeight;
            break;
        case TrayDockEdge::Unknown:
        default:
            if (windowHeight > windowWidth) {
                ++pulseWidth;
            } else {
                ++pulseHeight;
            }
            break;
    }

    // ExplorerPatcher's own UI refresh path broadcasts "TraySettings". Do it
    // before the physical size pulse so tray children invalidate their cached
    // settings before Windows delivers the real WM_SIZE/WM_WINDOWPOSCHANGED.
    const BOOL traySettingsOk = SendNotifyMessageW(
        HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        reinterpret_cast<LPARAM>(L"TraySettings"));

    const BOOL pulseOk = SetWindowPos(
        tray, nullptr, pulseX, pulseY, pulseWidth, pulseHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);

    if (pulseOk) Sleep(30);

    const BOOL restoreOk = SetWindowPos(
        tray, nullptr, windowRect.left, windowRect.top,
        windowWidth, windowHeight,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // ExplorerPatcher itself posts WM_DWMCOMPOSITIONCHANGED to Shell_TrayWnd in
    // its taskbar fix-up code. This is the targeted composition/backdrop refresh
    // missing in v0.1.1, where opening Start later happened to refresh the
    // Desktop-mode taskbar backdrop.
    DWORD_PTR compositionResult = 0;
    const LRESULT compositionOk = SendMessageTimeoutW(
        tray, WM_DWMCOMPOSITIONCHANGED, 0, 0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &compositionResult);

    const BOOL redrawOk = RedrawWindow(
        tray, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN |
            RDW_UPDATENOW);

    Wh_Log(L"[tablet-refresh] %s tray=%p edge=%s window=%dx%d client=%dx%d pulse=%d restore=%d traySettings=%d composition=%lld redraw=%d",
           reason ? reason : L"refresh", tray, TrayDockEdgeName(edge),
           windowWidth, windowHeight, clientWidth, clientHeight,
           pulseOk ? 1 : 0, restoreOk ? 1 : 0,
           traySettingsOk ? 1 : 0,
           static_cast<long long>(compositionOk), redrawOk ? 1 : 0);
}

void RestoreControllerState() {
    if (!g_controllerBuildSupported || !CurrentProcessOwnsShell(false)) return;

    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool shouldUninitialize = SUCCEEDED(coHr);
    if (coHr == RPC_E_CHANGED_MODE) coHr = S_OK;
    if (FAILED(coHr)) return;

    ITabletModeController* controller = nullptr;
    HRESULT hr = GetTabletModeController(&controller);
    if (FAILED(hr) || !controller) {
        if (shouldUninitialize) CoUninitialize();
        return;
    }

    int originalMode = g_originalMode.load();
    int originalAvailability = g_originalAvailability.load();

    if (g_leftAvailabilityForced.load() && ReadAvailability(controller) == 0) {
        WriteAvailability(controller, 1);
    }

    bool restoredMode = false;
    if (g_changedMode.load() && originalMode >= 0) {
        int current = -1;
        if (SUCCEEDED(controller->GetMode(&current)) && current != originalMode) {
            Wh_Log(L"[tablet] restoring original controller mode %d", originalMode);
            HRESULT setHr =
                controller->SetMode(originalMode, g_modeTrigger.load());
            Wh_Log(L"[tablet] restore SetMode returned 0x%08X",
                   static_cast<unsigned>(setHr));
            restoredMode = SUCCEEDED(setHr);
        }
    }

    if (restoredMode) {
        RefreshTaskbarLayoutAfterModeTransition(
            originalMode == 1 ? L"after restoring Tablet Mode on unload"
                              : L"after restoring Desktop mode on unload");
    }

    if (g_leftAvailabilityForced.load() && originalAvailability >= 0) {
        WriteAvailability(controller, static_cast<BYTE>(originalAvailability));
        Wh_Log(L"[tablet] restored cached Tablet Mode availability to %d",
               originalAvailability);
    }

    controller->Release();
    if (shouldUninitialize) CoUninitialize();
}

void SetCompatActive(bool active, PCWSTR reason) {
    const bool old =
        g_compatActive.exchange(active, std::memory_order_acq_rel);

    if (active && !old) {
        // Startup burns through many compatibility queries before the real
        // controller transition. Reset counters here so calls made by the
        // transition itself remain visible in the diagnostic log.
        g_cTrayTabletCalls.store(0, std::memory_order_relaxed);
        g_cTrayImmersiveCalls.store(0, std::memory_order_relaxed);
        g_epTrayCalls.store(0, std::memory_order_relaxed);
        g_epTaskBandCalls.store(0, std::memory_order_relaxed);
        g_epButtonWidthCalls.store(0, std::memory_order_relaxed);
        g_startSwitchWnfOverrideCount.store(0, std::memory_order_relaxed);
        g_edgeSetVisibleWnfOverrideCount.store(0, std::memory_order_relaxed);
    }

    ApplyLegacyTitlebarMouseInvocationGate(active);

    if (old != active || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-compat] compatibility layer %s (%s)",
               active ? L"ACTIVE" : L"transparent",
               reason ? reason : L"no reason");
    }
}

void LogCompatGetter(PCWSTR name, unsigned long callNo, bool real, bool out) {
    if (!g_verbose.load(std::memory_order_relaxed) || callNo > 40) return;
    Wh_Log(L"[tablet-compat] %s call=%lu real=%d returned=%d active=%d",
           name, callNo, real ? 1 : 0, out ? 1 : 0,
           g_compatActive.load(std::memory_order_relaxed) ? 1 : 0);
}

bool __cdecl CTray_IsTabletModeEnabled_Hook(void* pThis) {
    bool real = CTray_IsTabletModeEnabled_Original(pThis);
    bool force = g_compatActive.load(std::memory_order_relaxed) &&
                 g_forceExplorerTablet.load(std::memory_order_relaxed);
    bool out = force ? true : real;
    auto n = g_cTrayTabletCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogCompatGetter(L"explorer!CTray::IsTabletModeEnabled", n, real, out);
    return out;
}

bool __cdecl CTray_IsModeImmersive_Hook(void* pThis) {
    bool real = CTray_IsModeImmersive_Original(pThis);
    bool force = g_compatActive.load(std::memory_order_relaxed) &&
                 g_forceExplorerImmersive.load(std::memory_order_relaxed);
    bool out = force ? true : real;
    auto n = g_cTrayImmersiveCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogCompatGetter(L"explorer!CTray::IsModeImmersive", n, real, out);
    return out;
}

bool __cdecl EP_TrayUI_IsTabletModeEnabled_Hook(void* pThis) {
    bool real = EP_TrayUI_IsTabletModeEnabled_Original(pThis);
    bool force = g_compatActive.load(std::memory_order_relaxed) &&
                 g_forceExplorerPatcherTablet.load(std::memory_order_relaxed);
    bool out = force ? true : real;
    auto n = g_epTrayCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogCompatGetter(L"EP TrayUI::IsTabletModeEnabled", n, real, out);
    return out;
}

bool __cdecl EP_CTaskBand_IsTabletModeEnabled_Hook(void* pThis) {
    bool real = EP_CTaskBand_IsTabletModeEnabled_Original(pThis);
    bool force = g_compatActive.load(std::memory_order_relaxed) &&
                 g_forceExplorerPatcherTablet.load(std::memory_order_relaxed);
    bool out = force ? true : real;
    auto n = g_epTaskBandCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogCompatGetter(L"EP CTaskBand::IsTabletModeEnabled", n, real, out);
    return out;
}


bool EpTaskbarImageMatchesTouchLayout(HMODULE ep) {
    if (!ep) return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(ep);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<BYTE*>(ep) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const DWORD timestamp = nt->FileHeader.TimeDateStamp;
    const DWORD imageSize = nt->OptionalHeader.SizeOfImage;

    Wh_Log(L"[tablet-touch] ep_taskbar.ge.dll timestamp=0x%08X size=0x%08X",
           timestamp, imageSize);

    if (timestamp != kExpectedEpTaskbarTimestamp ||
        imageSize != kExpectedEpTaskbarSizeOfImage) {
        Wh_Log(L"[tablet-touch] private touch-width compatibility disabled: unrecognized ExplorerPatcher build");
        return false;
    }

    return true;
}

int __cdecl EP_ComputeSingleButtonWidth_Hook(void* pThis, int groupType,
                                             void* taskBtnGroup,
                                             int* constrainedWidth) {
    const bool force =
        g_compatActive.load(std::memory_order_relaxed) &&
        g_forceTouchFriendlyTaskButtons.load(std::memory_order_relaxed);

    BYTE* touchFlag = nullptr;
    BYTE oldFlag = 0;

    if (force && pThis) {
        touchFlag = reinterpret_cast<BYTE*>(pThis) +
                    kEpTaskListTouchImprovementFlagOffset;
        oldFlag = *touchFlag;

        // EP's own implementation checks this flag and, when true, adds
        // MulDiv(16, DPI, 96) to the button width (subject to its normal
        // taskbar/layout conditions). Temporarily force the flag for this
        // computation so all of EP's original scaling logic remains intact.
        if (!oldFlag) {
            *touchFlag = 1;
            MemoryBarrier();
        }
    }

    const int width = EP_ComputeSingleButtonWidth_Original(
        pThis, groupType, taskBtnGroup, constrainedWidth);

    if (touchFlag && !oldFlag) {
        MemoryBarrier();
        *touchFlag = oldFlag;
    }

    const auto n =
        g_epButtonWidthCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (g_verbose.load(std::memory_order_relaxed) && force && n <= 30) {
        Wh_Log(L"[tablet-touch] CTaskListWnd::_ComputeSingleButtonWidth call=%lu groupType=%d originalTouchFlag=%u width=%d",
               n, groupType, static_cast<unsigned>(oldFlag), width);
    }

    return width;
}

struct WnfRawContext {
    bool called = false;
    DWORD raw = 0;
};

LONG NTAPI CaptureRawWnfCallback(
    unsigned long long /*stateName*/, ULONG /*changeStamp*/, void* /*typeId*/,
    void* callbackContext, const void* buffer, ULONG length) {
    auto* ctx = static_cast<WnfRawContext*>(callbackContext);
    if (!ctx) return 0;

    ctx->called = true;
    if (buffer && length == sizeof(DWORD)) {
        ctx->raw = *static_cast<const DWORD*>(buffer);
    }
    return 0;
}

bool QueryTabletWnfRaw(DWORD* rawOut, ULONG* stampOut = nullptr) {
    if (rawOut) *rawOut = 0;
    if (stampOut) *stampOut = 0;
    if (!RtlQueryWnfStateData_Original) return false;

    WnfRawContext ctx{};
    ULONG stamp = 0;
    LONG status = RtlQueryWnfStateData_Original(
        &stamp, kWnfTmcnIsTabletMode,
        reinterpret_cast<void*>(&CaptureRawWnfCallback), &ctx, nullptr);

    if (stampOut) *stampOut = stamp;
    if (rawOut) *rawOut = ctx.raw;
    return status >= 0 && ctx.called;
}

LONG NTAPI RtlQueryWnfStateData_Hook(
    ULONG* changeStamp, unsigned long long stateName, void* callback,
    void* callbackContext, const void* typeId) {
    LONG status = RtlQueryWnfStateData_Original
                      ? RtlQueryWnfStateData_Original(
                            changeStamp, stateName, callback, callbackContext,
                            typeId)
                      : static_cast<LONG>(0xC0000002);  // STATUS_NOT_IMPLEMENTED

    const bool forceTrySwitch = t_forceTabletWnfForTrySwitch;
    const bool forceEdgeSetVisible = t_forceTabletWnfForEdgeSetVisible;
    if ((!forceTrySwitch && !forceEdgeSetVisible) ||
        stateName != kWnfTmcnIsTabletMode || status < 0 || !callback) {
        return status;
    }

    // Preserve the real Windows query, then replay only its callback with the
    // missing legacy Tablet Mode value. The persistent WNF state is untouched.
    // This hook is used by two independent, thread-local scopes:
    //  1) Start app reuse (TrySwitchToAppIfTabletMode)
    //  2) native CEdgeUiInput::SetVisible(true) for legacy EDGEUI_INDEX 2.
    DWORD one = 1;
    ULONG stamp = changeStamp ? *changeStamp : 0;
    auto cb = reinterpret_cast<WnfQueryCallback_t>(callback);
    cb(stateName, stamp, const_cast<void*>(typeId), callbackContext, &one,
       sizeof(one));

    if (forceTrySwitch) {
        const auto count =
            g_startSwitchWnfOverrideCount.fetch_add(
                1, std::memory_order_relaxed) + 1;
        if (g_verbose.load(std::memory_order_relaxed) && count <= 50) {
            Wh_Log(L"[tablet-start-switch] WNF override #%lu state=0x%016llX stamp=%lu raw=>1",
                   count, stateName, stamp);
        }
    }

    if (forceEdgeSetVisible) {
        const auto count =
            g_edgeSetVisibleWnfOverrideCount.fetch_add(
                1, std::memory_order_relaxed) + 1;
        if (count <= 40 || g_verbose.load(std::memory_order_relaxed)) {
            Wh_Log(L"[tablet-mouse-gate] edge-2 SetVisible WNF override #%lu state=0x%016llX stamp=%lu raw=>1 tid=%lu",
                   count, stateName, stamp, GetCurrentThreadId());
        }
    }

    return status;
}

bool EnsureTabletWnfQueryHook() {
    if (g_wnfQueryHookQueued.load(std::memory_order_acquire)) {
        return RtlQueryWnfStateData_Original != nullptr;
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    void* rtlQuery = ntdll ? reinterpret_cast<void*>(
                                GetProcAddress(ntdll, "RtlQueryWnfStateData"))
                           : nullptr;
    if (!rtlQuery) {
        Wh_Log(L"[tablet-wnf] RtlQueryWnfStateData export not found");
        return false;
    }

    if (!Wh_SetFunctionHook(
            rtlQuery, reinterpret_cast<void*>(&RtlQueryWnfStateData_Hook),
            reinterpret_cast<void**>(&RtlQueryWnfStateData_Original))) {
        Wh_Log(L"[tablet-wnf] failed to hook RtlQueryWnfStateData");
        return false;
    }

    g_wnfQueryHookQueued.store(true, std::memory_order_release);
    Wh_Log(L"[tablet-wnf] queued shared RtlQueryWnfStateData hook at %p",
           rtlQuery);
    return true;
}

bool TrySwitchToAppIfTabletMode_Hook(const wchar_t* appId) {
    if (!TrySwitchToAppIfTabletMode_Original) return false;

    DWORD raw = 0;
    ULONG stamp = 0;
    const bool haveWnf = QueryTabletWnfRaw(&raw, &stamp);
    const bool useShim =
        g_restoreStartAppSwitchBehavior.load(std::memory_order_relaxed) &&
        g_compatActive.load(std::memory_order_acquire) && haveWnf && raw == 0;

    if (g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-start-switch] ENTER appId='%s' compatActive=%d WNFok=%d raw=%lu stamp=%lu shim=%d",
               appId ? appId : L"<null>",
               g_compatActive.load(std::memory_order_relaxed) ? 1 : 0,
               haveWnf ? 1 : 0, raw, stamp, useShim ? 1 : 0);
    }

    const bool previous = t_forceTabletWnfForTrySwitch;
    if (useShim) t_forceTabletWnfForTrySwitch = true;
    const bool result = TrySwitchToAppIfTabletMode_Original(appId);
    t_forceTabletWnfForTrySwitch = previous;

    if (g_verbose.load(std::memory_order_relaxed) || useShim) {
        Wh_Log(L"[tablet-start-switch] EXIT appId='%s' result=%d shim=%d",
               appId ? appId : L"<null>", result ? 1 : 0,
               useShim ? 1 : 0);
    }
    return result;
}

bool ShouldLogGestureCall(unsigned long n, bool noisy) {
    if (!noisy) return true;
    return n <= 80 || (n % 50) == 0;
}

void LogGesturePoint(PCWSTR name, unsigned long n, unsigned int pointerId,
                     POINT point, bool noisy) {
    if (!ShouldLogGestureCall(n, noisy)) return;
    Wh_Log(L"[tablet-gesture] %s #%lu pointer=%u pt=(%ld,%ld) compat=%d tid=%lu",
           name, n, pointerId, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
}

HRESULT TabletModeInputHandler_RuntimeClassInitialize_Hook(
    void* pThis, int source, void* callback) {
    const auto n = g_gestureInputInitCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-gesture] InputHandler::RuntimeClassInitialize #%lu source=%d callback=%p compat=%d tid=%lu",
           n, source, callback,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    HRESULT hr = TabletModeInputHandler_RuntimeClassInitialize_Original(
        pThis, source, callback);
    Wh_Log(L"[tablet-gesture] InputHandler::RuntimeClassInitialize #%lu -> hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

void TabletModeInputHandler_PointerDown_Hook(void* pThis,
                                             unsigned int pointerId,
                                             POINT point) {
    const auto n = g_gesturePointerDownCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogGesturePoint(L"InputHandler::PointerDown", n, pointerId, point, false);
    TabletModeInputHandler_PointerDown_Original(pThis, pointerId, point);
}

void TabletModeInputHandler_PointerUpdate_Hook(void* pThis,
                                               unsigned int pointerId,
                                               POINT point) {
    const auto n = g_gesturePointerUpdateCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogGesturePoint(L"InputHandler::PointerUpdate", n, pointerId, point, true);
    TabletModeInputHandler_PointerUpdate_Original(pThis, pointerId, point);
}

void TabletModeInputHandler_PointerUp_Hook(void* pThis,
                                           unsigned int pointerId,
                                           POINT point) {
    const auto n = g_gesturePointerUpCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogGesturePoint(L"InputHandler::PointerUp", n, pointerId, point, false);
    TabletModeInputHandler_PointerUp_Original(pThis, pointerId, point);
}

HRESULT TabletModeViewManager_StartSwipe_Hook(void* pThis,
                                              unsigned int pointerId,
                                              POINT point) {
    const auto n = g_gestureStartSwipeCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogGesturePoint(L"ViewManager::StartSwipe", n, pointerId, point, false);
    HRESULT hr = TabletModeViewManager_StartSwipe_Original(pThis, pointerId, point);
    Wh_Log(L"[tablet-gesture] ViewManager::StartSwipe #%lu -> hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT TabletModeViewManager_StartExtendedSwipe_Hook(
    void* pThis, POINT point1, unsigned int pointerId, POINT point2) {
    const auto n = g_gestureStartExtendedSwipeCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-gesture] ViewManager::StartExtendedSwipe #%lu pointer=%u p1=(%ld,%ld) p2=(%ld,%ld) compat=%d tid=%lu",
           n, pointerId, point1.x, point1.y, point2.x, point2.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    HRESULT hr = TabletModeViewManager_StartExtendedSwipe_Original(
        pThis, point1, pointerId, point2);
    Wh_Log(L"[tablet-gesture] ViewManager::StartExtendedSwipe #%lu -> hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT TabletModeViewManager_ContinueSwipe_Hook(void* pThis,
                                                 unsigned int pointerId,
                                                 POINT point) {
    const auto n = g_gestureContinueSwipeCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogGesturePoint(L"ViewManager::ContinueSwipe", n, pointerId, point, true);
    return TabletModeViewManager_ContinueSwipe_Original(pThis, pointerId, point);
}

HRESULT TabletModeViewManager_CommitSwipe_Hook(void* pThis,
                                               unsigned int pointerId,
                                               POINT point) {
    const auto n = g_gestureCommitSwipeCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogGesturePoint(L"ViewManager::CommitSwipe", n, pointerId, point, false);
    HRESULT hr = TabletModeViewManager_CommitSwipe_Original(pThis, pointerId, point);
    Wh_Log(L"[tablet-gesture] ViewManager::CommitSwipe #%lu -> hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT TabletModeViewManager_CancelSwipe_Hook(void* pThis) {
    const auto n = g_gestureCancelSwipeCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-gesture] ViewManager::CancelSwipe #%lu compat=%d tid=%lu",
           n, g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    HRESULT hr = TabletModeViewManager_CancelSwipe_Original(pThis);
    Wh_Log(L"[tablet-gesture] ViewManager::CancelSwipe #%lu -> hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT TabletModeViewManager_StartDrag_Hook(void* pThis, POINT point) {
    const auto n = g_mouseStartDragCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse] ViewManager::StartDrag #%lu pt=(%ld,%ld) compat=%d tid=%lu ENTER",
           n, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    const HRESULT hr = TabletModeViewManager_StartDrag_Original(pThis, point);
    Wh_Log(L"[tablet-mouse] ViewManager::StartDrag #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT TabletModeViewManager_ContinueDrag_Hook(void* pThis, POINT point) {
    const auto n = g_mouseContinueDragCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 40 || (n % 50) == 0) {
        Wh_Log(L"[tablet-mouse] ViewManager::ContinueDrag #%lu pt=(%ld,%ld) compat=%d tid=%lu",
               n, point.x, point.y,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
               GetCurrentThreadId());
    }
    return TabletModeViewManager_ContinueDrag_Original(pThis, point);
}

HRESULT TabletModeViewManager_CommitDrag_Hook(void* pThis, POINT point) {
    const auto n = g_mouseCommitDragCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse] ViewManager::CommitDrag #%lu pt=(%ld,%ld) compat=%d tid=%lu ENTER",
           n, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    const HRESULT hr = TabletModeViewManager_CommitDrag_Original(pThis, point);
    Wh_Log(L"[tablet-mouse] ViewManager::CommitDrag #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT TabletModeViewManager_CancelDrag_Hook(void* pThis) {
    const auto n = g_mouseCancelDragCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse] ViewManager::CancelDrag #%lu compat=%d tid=%lu ENTER",
           n, g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    const HRESULT hr = TabletModeViewManager_CancelDrag_Original(pThis);
    Wh_Log(L"[tablet-mouse] ViewManager::CancelDrag #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT TabletModeViewManager_ShowAppResizeView_Hook(
    void* pThis, void* applicationView, int moveSizeType, POINT point) {
    const auto n = g_gestureShowAppResizeCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-gesture] ViewManager::ShowAppResizeView #%lu appView=%p moveSizeType=%d pt=(%ld,%ld) compat=%d tid=%lu",
           n, applicationView, moveSizeType, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    HRESULT hr = TabletModeViewManager_ShowAppResizeView_Original(
        pThis, applicationView, moveSizeType, point);
    Wh_Log(L"[tablet-gesture] ViewManager::ShowAppResizeView #%lu -> hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT TabletModeViewManager_MoveSizeAttempted_Hook(
    void* pThis, void* applicationView, int moveSizeType) {
    const auto n = g_gestureMoveSizeAttemptedCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-gesture] ViewManager::MoveSizeAttempted #%lu appView=%p moveSizeType=%d compat=%d tid=%lu",
           n, applicationView, moveSizeType,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    const HRESULT hr = TabletModeViewManager_MoveSizeAttempted_Original(
        pThis, applicationView, moveSizeType);
    Wh_Log(L"[tablet-gesture] ViewManager::MoveSizeAttempted #%lu -> hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}


HRESULT SnapServiceProvider_SnapToLocation_Hook(
    void* pThis, void* applicationView, int snapLocation, int flags, RECT* snapRect) {
    const auto n = g_snapToLocationCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-snap] SnapServiceProvider::SnapToLocation #%lu appView=%p location=%d flags=0x%X rect=%p compat=%d ENTER tid=%lu",
           n, applicationView, snapLocation, static_cast<unsigned>(flags), snapRect,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    const HRESULT hr = SnapServiceProvider_SnapToLocation_Original(
        pThis, applicationView, snapLocation, flags, snapRect);
    Wh_Log(L"[tablet-snap] SnapServiceProvider::SnapToLocation #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT SnapServiceProvider_HandleTabletModeSnapping_Hook(
    void* pThis, void* applicationView, int snapLocation, RECT* snapRect) {
    const auto n = g_handleTabletSnappingCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-snap] SnapServiceProvider::HandleTabletModeSnapping #%lu appView=%p location=%d rect=%p compat=%d ENTER tid=%lu",
           n, applicationView, snapLocation, snapRect,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    const HRESULT hr = SnapServiceProvider_HandleTabletModeSnapping_Original(
        pThis, applicationView, snapLocation, snapRect);
    Wh_Log(L"[tablet-snap] SnapServiceProvider::HandleTabletModeSnapping #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT SnapServiceProvider_HandleDesktopModeSnapping_Hook(
    void* pThis, void* applicationView, int snapLocation, int flags, RECT* snapRect) {
    const auto n = g_handleDesktopSnappingCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-snap] SnapServiceProvider::HandleDesktopModeSnapping #%lu appView=%p location=%d flags=0x%X rect=%p compat=%d ENTER tid=%lu",
           n, applicationView, snapLocation, static_cast<unsigned>(flags), snapRect,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    const HRESULT hr = SnapServiceProvider_HandleDesktopModeSnapping_Original(
        pThis, applicationView, snapLocation, flags, snapRect);
    Wh_Log(L"[tablet-snap] SnapServiceProvider::HandleDesktopModeSnapping #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT TabletModePositioner_ShowSnapAssist_Hook(void* pThis) {
    const auto n = g_showSnapAssistCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-snap] TabletModePositioner::ShowSnapAssist #%lu compat=%d ENTER tid=%lu",
           n, g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    const HRESULT hr = TabletModePositioner_ShowSnapAssist_Original(pThis);
    Wh_Log(L"[tablet-snap] TabletModePositioner::ShowSnapAssist #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT TabletModeViewManager_CreateWindowArrangementViewForDrag_Hook(
    void* pThis, void* applicationView, void** unknownOut) {
    const auto n = g_createArrangementDragCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-snap] TabletModeViewManager::CreateWindowArrangementViewForDrag #%lu appView=%p out=%p compat=%d ENTER tid=%lu",
           n, applicationView, unknownOut,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    const HRESULT hr = TabletModeViewManager_CreateWindowArrangementViewForDrag_Original(
        pThis, applicationView, unknownOut);
    Wh_Log(L"[tablet-snap] TabletModeViewManager::CreateWindowArrangementViewForDrag #%lu EXIT hr=0x%08X outValue=%p",
           n, static_cast<unsigned>(hr), unknownOut ? *unknownOut : nullptr);
    return hr;
}


void LogUpstreamMove(const wchar_t* name, std::atomic<unsigned long>& counter,
                     void* pThis, HWND hwnd, void* applicationView,
                     ULONG arg1, ULONG arg2) {
    const auto n = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 50 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-route] %s #%lu this=%p hwnd=%p appView=%p arg1=%lu arg2=%lu compat=%d tid=%lu",
               name, n, pThis, hwnd, applicationView, arg1, arg2,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
               GetCurrentThreadId());
        if (hwnd && IsWindow(hwnd)) {
            POINT cursor = {};
            GetCursorPos(&cursor);
            LogDesktopWindowIdentity(name, hwnd, cursor);
        }
    }
}

void ViewPresentationMediator_OnMoveSizeAttempted_Hook(
    void* pThis, HWND hwnd, void* applicationView, ULONG arg1, ULONG arg2) {
    LogUpstreamMove(L"ViewPresentationMediator::OnMoveSizeAttempted",
                    g_viewMediatorMoveCalls, pThis, hwnd, applicationView, arg1, arg2);
    ViewPresentationMediator_OnMoveSizeAttempted_Original(
        pThis, hwnd, applicationView, arg1, arg2);
}

void TabletModePositionerManager_OnMoveSizeAttempted_Hook(
    void* pThis, HWND hwnd, void* applicationView, ULONG arg1, ULONG arg2) {
    LogUpstreamMove(L"TabletModePositionerManager::OnMoveSizeAttempted",
                    g_tabletPositionerManagerMoveCalls, pThis, hwnd, applicationView, arg1, arg2);
    TabletModePositionerManager_OnMoveSizeAttempted_Original(
        pThis, hwnd, applicationView, arg1, arg2);
}

HRESULT LogPositionerTransition(const wchar_t* name,
                                std::atomic<unsigned long>& counter,
                                PositionerTabletModeChanged_t original,
                                void* pThis, int state) {
    const auto n = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-route] %s #%lu state=%d compat=%d ENTER tid=%lu",
           name, n, state,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    const HRESULT hr = original(pThis, state);
    Wh_Log(L"[tablet-route] %s #%lu EXIT hr=0x%08X",
           name, n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT ShellPositionerManager_TabletModeChanged_Hook(void* pThis, int state) {
    return LogPositionerTransition(L"ShellPositionerManager::TabletModeChanged",
        g_shellPositionerChangedCalls, ShellPositionerManager_TabletModeChanged_Original,
        pThis, state);
}
HRESULT TabletModePositioner_TabletModeChanged_Hook(void* pThis, int state) {
    return LogPositionerTransition(L"TabletModePositioner::TabletModeChanged",
        g_tabletPositionerChangedCalls, TabletModePositioner_TabletModeChanged_Original,
        pThis, state);
}
HRESULT CDesktopPositioner_TabletModeChanged_Hook(void* pThis, int state) {
    return LogPositionerTransition(L"CDesktopPositioner::TabletModeChanged",
        g_desktopPositionerChangedCalls, CDesktopPositioner_TabletModeChanged_Original,
        pThis, state);
}

HRESULT LogChromeConfiguration(const wchar_t* name,
                               std::atomic<unsigned long>& counter,
                               GetChromeConfigurationForView_t original,
                               void* pThis, void* applicationView,
                               int* chromeOptions, void** titlebarConfiguration) {
    const auto n = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    const HRESULT hr = original(pThis, applicationView, chromeOptions, titlebarConfiguration);
    if (n <= 50 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-chrome] %s #%lu appView=%p hr=0x%08X chrome=%d titlebar=%p compat=%d tid=%lu",
               name, n, applicationView, static_cast<unsigned>(hr),
               chromeOptions ? *chromeOptions : -1,
               titlebarConfiguration ? *titlebarConfiguration : nullptr,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
               GetCurrentThreadId());
    }
    return hr;
}
HRESULT CDesktopPositioner_GetChromeConfigurationForView_Hook(
    void* pThis, void* applicationView, int* chromeOptions, void** titlebarConfiguration) {
    return LogChromeConfiguration(L"CDesktopPositioner::GetChromeConfigurationForView",
        g_desktopChromeCalls, CDesktopPositioner_GetChromeConfigurationForView_Original,
        pThis, applicationView, chromeOptions, titlebarConfiguration);
}
HRESULT TabletModePositioner_GetChromeConfigurationForView_Hook(
    void* pThis, void* applicationView, int* chromeOptions, void** titlebarConfiguration) {
    return LogChromeConfiguration(L"TabletModePositioner::GetChromeConfigurationForView",
        g_tabletChromeCalls, TabletModePositioner_GetChromeConfigurationForView_Original,
        pThis, applicationView, chromeOptions, titlebarConfiguration);
}


HRESULT ShellPositionerManager_SwapPositionersForViews_Hook(
    void* pThis, int fromType, int toType) {
    const auto n =
        g_swapPositionersForViewsCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-select] SwapPositionersForViews #%lu from=%d to=%d compat=%d ENTER tid=%lu",
           n, fromType, toType,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    const HRESULT hr =
        ShellPositionerManager_SwapPositionersForViews_Original(
            pThis, fromType, toType);
    Wh_Log(L"[tablet-select] SwapPositionersForViews #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT ShellPositionerManager_ChangePositionerForView_Hook(
    void* pThis, void* applicationView, int positionerType, bool force) {
    const auto n =
        g_changePositionerForViewCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 40 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-select] ChangePositionerForView #%lu appView=%p type=%d force=%d compat=%d ENTER tid=%lu",
               n, applicationView, positionerType, force ? 1 : 0,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
               GetCurrentThreadId());
    }
    const HRESULT hr =
        ShellPositionerManager_ChangePositionerForView_Original(
            pThis, applicationView, positionerType, force);
    if (n <= 40 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-select] ChangePositionerForView #%lu EXIT hr=0x%08X",
               n, static_cast<unsigned>(hr));
    }
    return hr;
}

HRESULT ShellPositionerManager_PerformPositionerHandoffView_Hook(
    void* pThis, void* applicationView, int fromType, int toType) {
    const auto n =
        g_positionerHandoffCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 40 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-select] PerformPositionerHandoff(view) #%lu appView=%p from=%d to=%d compat=%d ENTER tid=%lu",
               n, applicationView, fromType, toType,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
               GetCurrentThreadId());
    }
    const HRESULT hr =
        ShellPositionerManager_PerformPositionerHandoffView_Original(
            pThis, applicationView, fromType, toType);
    if (n <= 40 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-select] PerformPositionerHandoff(view) #%lu EXIT hr=0x%08X",
               n, static_cast<unsigned>(hr));
    }
    return hr;
}


static unsigned int TryGetObjectArrayCount(void* objectArray) {
    if (!objectArray) return 0xFFFFFFFFu;
    auto** vtable = *reinterpret_cast<void***>(objectArray);
    if (!vtable || !vtable[3]) return 0xFFFFFFFFu;
    using GetCount_t = HRESULT(__cdecl*)(void*, unsigned int*);
    unsigned int count = 0xFFFFFFFFu;
    const HRESULT hr =
        reinterpret_cast<GetCount_t>(vtable[3])(objectArray, &count);
    return SUCCEEDED(hr) ? count : 0xFFFFFFFFu;
}

HRESULT ShellPositionerManager_PerformPositionerHandoffArray_Hook(
    void* pThis, void* objectArray, void* sourceMonitor, void* targetMonitor,
    void* presentationArgs, int fromType, int toType) {
    const auto n =
        g_positionerHandoffArrayCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    const unsigned int count = TryGetObjectArrayCount(objectArray);
    Wh_Log(L"[tablet-select] PerformPositionerHandoff(array) #%lu views=%u array=%p srcMon=%p dstMon=%p args=%p from=%d to=%d compat=%d ENTER tid=%lu",
           n, count, objectArray, sourceMonitor, targetMonitor, presentationArgs,
           fromType, toType,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    const HRESULT hr =
        ShellPositionerManager_PerformPositionerHandoffArray_Original(
            pThis, objectArray, sourceMonitor, targetMonitor, presentationArgs,
            fromType, toType);
    Wh_Log(L"[tablet-select] PerformPositionerHandoff(array) #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT ShellPositionerManager_PerformPositionerHandoffApp_Hook(
    void* pThis, void* applicationView, void* sourceMonitor, void* targetMonitor,
    void* presentationArgs, int fromType, int toType) {
    const auto n =
        g_positionerHandoffAppCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 80 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-select] PerformPositionerHandoff(app) #%lu appView=%p srcMon=%p dstMon=%p args=%p from=%d to=%d compat=%d ENTER tid=%lu",
               n, applicationView, sourceMonitor, targetMonitor, presentationArgs,
               fromType, toType,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
               GetCurrentThreadId());
    }
    const HRESULT hr =
        ShellPositionerManager_PerformPositionerHandoffApp_Original(
            pThis, applicationView, sourceMonitor, targetMonitor,
            presentationArgs, fromType, toType);
    if (n <= 80 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-select] PerformPositionerHandoff(app) #%lu EXIT hr=0x%08X",
               n, static_cast<unsigned>(hr));
    }
    return hr;
}

HRESULT CEdgeUiManager_PerformDelayedInitialization_Hook(void* pThis) {
    const auto n =
        g_edgeDelayedInitCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-edge] CEdgeUiManager::PerformDelayedInitialization #%lu compat=%d ENTER tid=%lu",
           n, g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    const HRESULT hr = CEdgeUiManager_PerformDelayedInitialization_Original(pThis);
    Wh_Log(L"[tablet-edge] CEdgeUiManager::PerformDelayedInitialization #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT CEdgeUiManager_InputObserveStart_Hook(void* pThis, void* edgeInput) {
    const auto n =
        g_edgeObserveStartCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-edge] CEdgeUiManager::InputObserveStart #%lu edgeInput=%p compat=%d ENTER tid=%lu",
           n, edgeInput,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());

    if (edgeInput && g_compatActive.load(std::memory_order_acquire)) {
        t_lastObservedEdgeInput = edgeInput;
        t_lastObservedConcreteInput = reinterpret_cast<void*>(
            reinterpret_cast<BYTE*>(edgeInput) - kEdgeUiInputInterfaceOffset);
        Wh_Log(L"[tablet-mouse-bridge2] observe-source edgeInput=%p input=%p tid=%lu",
               t_lastObservedEdgeInput, t_lastObservedConcreteInput, GetCurrentThreadId());
    }

    const HRESULT hr = CEdgeUiManager_InputObserveStart_Original(pThis, edgeInput);
    Wh_Log(L"[tablet-edge] CEdgeUiManager::InputObserveStart #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT CEdgeUiInput_SetVisible_Hook(void* pThis, bool visible) {
    const auto n =
        g_edgeSetVisibleCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool compat =
        g_compatActive.load(std::memory_order_acquire);
    Wh_Log(L"[tablet-edge] CEdgeUiInput::SetVisible #%lu this=%p visible=%d compat=%d ENTER tid=%lu",
           n, pThis, visible ? 1 : 0, compat ? 1 : 0,
           GetCurrentThreadId());

    // Win10 keeps WNF_TMCN_ISTABLETMODE=1 while real Tablet Mode is active.
    // On the validated Win11 25H2 build the surviving TabletModeController can
    // enter mode 1 while this legacy WNF state remains 0. Native
    // CEdgeUiInput::SetVisible checks that WNF state specifically for legacy
    // EDGEUI_INDEX 2 before showing the uncloaked titlebar listener.
    //
    // Do not show the HWND ourselves. Instead, for exactly one native
    // SetVisible(true) call on an object that reports edge 2, scope the shared
    // WNF callback shim so native twinui makes its own normal decision and, if
    // appropriate, calls ShowWindow(SW_SHOWNOACTIVATE).
    int assignedEdge = -1;
    HRESULT edgeHr = E_NOTIMPL;
    if (pThis && CEdgeUiInput_GetAssignedEdge) {
        edgeHr = CEdgeUiInput_GetAssignedEdge(pThis, &assignedEdge);
    }

    DWORD wnfRaw = 0;
    ULONG wnfStamp = 0;
    bool haveWnf = false;
    const bool edge2Candidate =
        visible && compat &&
        g_restoreLegacyTitlebarMouseInvocationGate.load(
            std::memory_order_relaxed) &&
        SUCCEEDED(edgeHr) && assignedEdge == 2;

    bool useWnfShim = false;
    if (edge2Candidate) {
        haveWnf = QueryTabletWnfRaw(&wnfRaw, &wnfStamp);
        useWnfShim = haveWnf && wnfRaw == 0 &&
                     RtlQueryWnfStateData_Original != nullptr;
        Wh_Log(L"[tablet-mouse-gate] SetVisible edge-2 candidate #%lu this=%p edgeHr=0x%08X WNFok=%d raw=%lu stamp=%lu shim=%d",
               n, pThis, static_cast<unsigned>(edgeHr),
               haveWnf ? 1 : 0, wnfRaw, wnfStamp, useWnfShim ? 1 : 0);
    }

    const bool previousWnfScope = t_forceTabletWnfForEdgeSetVisible;
    if (useWnfShim) {
        t_forceTabletWnfForEdgeSetVisible = true;
    }
    const HRESULT hr = CEdgeUiInput_SetVisible_Original(pThis, visible);
    t_forceTabletWnfForEdgeSetVisible = previousWnfScope;

    Wh_Log(L"[tablet-edge] CEdgeUiInput::SetVisible #%lu EXIT hr=0x%08X edge=%d edgeHr=0x%08X shim=%d",
           n, static_cast<unsigned>(hr), assignedEdge,
           static_cast<unsigned>(edgeHr), useWnfShim ? 1 : 0);
    return hr;
}

void CEdgeUiInput_RegisterRawInput_Hook(void* pThis, bool enable) {
    const auto n =
        g_edgeRegisterRawInputCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-edge] CEdgeUiInput::_RegisterRawInput #%lu this=%p enable=%d compat=%d tid=%lu",
           n, pThis, enable ? 1 : 0,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    CEdgeUiInput_RegisterRawInput_Original(pThis, enable);
}

int CEdgeUiManager_IndexFromEdgeInput_Hook(void* pThis, void* edgeInput) {
    const int real = CEdgeUiManager_IndexFromEdgeInput_Original(pThis, edgeInput);
    if (!g_restoreLegacyMouseDrag.load(std::memory_order_relaxed) ||
        !g_compatActive.load(std::memory_order_acquire) || !g_twinui || real != 3 ||
        !edgeInput) {
        return real;
    }

    const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const auto base = reinterpret_cast<uintptr_t>(g_twinui);
    if (caller < base) return real;

    const uintptr_t rva = caller - base;
    const bool mouseHit = rva == kTwinuiMouseHitIndexFromInputReturnRva;
    const bool mouseDragStart =
        rva == kTwinuiMouseDragStartIndexFromInputReturnRva;
    if (!mouseHit && !mouseDragStart) return real;

    bool allow = false;
    if (mouseHit) {
        // MouseHitCornerOrEdge has the physical cursor point, unlike
        // _IndexFromEdgeInput itself. Only translate this exact edge input while
        // the enclosing hit-test is at the physical top of a monitor.
        allow = t_mouseHitScopeActive &&
                t_mouseHitScopePhysicalTop &&
                t_mouseHitScopeEdgeInput == edgeInput;
    } else {
        // Once _OnMouseDown has armed the Win10 mouse state, MouseDragStart no
        // longer carries the physical point. Restrict the translation to the
        // exact CEdgeUiInput object whose top-edge raw press armed our bridge.
        void* bridgeEdgeInput = t_legacyMouseBridgeInput
            ? reinterpret_cast<void*>(
                  reinterpret_cast<BYTE*>(t_legacyMouseBridgeInput) +
                  kEdgeUiInputInterfaceOffset)
            : nullptr;
        allow = t_legacyMouseBridgeActive && bridgeEdgeInput == edgeInput;
    }

    if (!allow) return real;

    const auto n =
        g_mouseIndexRemapCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse-fix] _IndexFromEdgeInput #%lu caller=%s rva=0x%llX real=%d -> legacy=2 edgeInput=%p scopeTop=%d bridge=%d tid=%lu",
           n, mouseHit ? L"MouseHitCornerOrEdge" : L"MouseDragStart",
           static_cast<unsigned long long>(rva), real, edgeInput,
           t_mouseHitScopePhysicalTop ? 1 : 0,
           t_legacyMouseBridgeActive ? 1 : 0,
           GetCurrentThreadId());
    return 2;
}

HRESULT CEdgeUiManager_MouseDragStart_Hook(void* pThis, void* edgeInput,
                                                void** mouseInvocation) {
    const auto n = g_mouseManagerDragStartCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse] CEdgeUiManager::MouseDragStart #%lu edgeInput=%p compat=%d ENTER tid=%lu",
           n, edgeInput, g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    const HRESULT hr = CEdgeUiManager_MouseDragStart_Original(pThis, edgeInput, mouseInvocation);
    Wh_Log(L"[tablet-mouse] CEdgeUiManager::MouseDragStart #%lu EXIT hr=0x%08X invoker=%p",
           n, static_cast<unsigned>(hr), mouseInvocation ? *mouseInvocation : nullptr);
    return hr;
}

HRESULT CEdgeUiManager_AcquireMouseInvokerForEdge_Hook(void* pThis, int edgeIndex,
                                                        void** mouseInvocation) {
    const auto n = g_mouseAcquireInvokerCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    const HRESULT hr = CEdgeUiManager_AcquireMouseInvokerForEdge_Original(pThis, edgeIndex, mouseInvocation);
    if (SUCCEEDED(hr) && mouseInvocation && *mouseInvocation &&
        g_compatActive.load(std::memory_order_acquire)) {
        t_lastObservedInvoker = *mouseInvocation;
    }
    Wh_Log(L"[tablet-mouse] CEdgeUiManager::_AcquireMouseInvokerForEdge #%lu edge=%d compat=%d hr=0x%08X invoker=%p tid=%lu",
           n, edgeIndex, g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           static_cast<unsigned>(hr), mouseInvocation ? *mouseInvocation : nullptr,
           GetCurrentThreadId());
    return hr;
}

HRESULT CEdgeUiInput_ObservedMouseButtonDown_Hook(void* pThis, POINT point, int button) {
    const auto n = g_mouseObservedDownCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse] CEdgeUiInput::ObservedMouseButtonDown #%lu this=%p button=%d pt=(%ld,%ld) compat=%d tid=%lu",
           n, pThis, button, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());

    const bool bridge =
        g_bridgeLegacyMouseWin32Path.load(std::memory_order_relaxed) &&
        g_restoreLegacyMouseDrag.load(std::memory_order_relaxed) &&
        g_compatActive.load(std::memory_order_acquire) &&
        button == 0 &&
        IsPointAtMonitorTopEdge(point) &&
        CEdgeUiInput_OnMouseDown_Original &&
        CEdgeUiInput_OnMouseMove_Original &&
        CEdgeUiInput_OnMouseUp_Original;

    if (bridge) {
        // If a previous experiment was left armed because the button was released
        // without another raw-motion packet, close it before starting a new one.
        if (t_legacyMouseBridgeActive) {
            FinishLegacyMouseBridge(t_legacyMouseBridgeLastPoint,
                                    L"new top-edge button down");
        }

        auto* input = reinterpret_cast<void*>(
            reinterpret_cast<BYTE*>(pThis) - kObservedMouseInterfaceOffset);
        t_legacyMouseBridgeActive = true;
        t_legacyMouseBridgeFromInvoker = false;
        t_legacyMouseBridgeInput = input;
        t_legacyMouseBridgeLastPoint = point;

        const auto bridgeN =
            g_mouseBridgeDownCalls.fetch_add(1, std::memory_order_relaxed) + 1;
        Wh_Log(L"[tablet-mouse-bridge] down #%lu rawThis=%p input=%p pt=(%ld,%ld) -> _OnMouseDown(raw=0); raw observer down suppressed tid=%lu",
               bridgeN, pThis, input, point.x, point.y, GetCurrentThreadId());

        // Win10 baseline: WM_LBUTTONDOWN invokes _OnMouseDown(point, false).
        // Suppress the parallel raw-observer down path for this one gesture so it
        // can't immediately execute v_ObservedMouseButtonDown/v_Invoke(reason=6)
        // and tear down observation before the legacy Win32 drag state is armed.
        CEdgeUiInput_OnMouseDown_Original(input, point, false);
        return S_OK;
    }

    return CEdgeUiInput_ObservedMouseButtonDown_Original(pThis, point, button);
}

HRESULT CEdgeUiInput_ObservedMouseMove_Hook(void* pThis, POINT point, unsigned short flags,
                                                POINT anchorPoint) {
    const auto n = g_mouseObservedMoveCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 80 || (n % 100) == 0) {
        Wh_Log(L"[tablet-mouse] CEdgeUiInput::ObservedMouseMove #%lu this=%p pt=(%ld,%ld) flags=0x%04X anchor=(%ld,%ld) compat=%d tid=%lu",
               n, pThis, point.x, point.y, static_cast<unsigned>(flags),
               anchorPoint.x, anchorPoint.y,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    }

    if (t_legacyMouseBridgeActive &&
        g_bridgeLegacyMouseWin32Path.load(std::memory_order_relaxed) &&
        g_compatActive.load(std::memory_order_acquire)) {
        void* expectedRawThis = reinterpret_cast<void*>(
            reinterpret_cast<BYTE*>(t_legacyMouseBridgeInput) +
            kObservedMouseInterfaceOffset);
        if (pThis == expectedRawThis) {
            t_legacyMouseBridgeLastPoint = point;

            if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {
                const auto bridgeN =
                    g_mouseBridgeMoveCalls.fetch_add(1, std::memory_order_relaxed) + 1;
                if (bridgeN <= 80 || (bridgeN % 100) == 0) {
                    Wh_Log(L"[tablet-mouse-bridge] move #%lu rawThis=%p input=%p pt=(%ld,%ld) -> _OnMouseMove(a=1,b=0) tid=%lu",
                           bridgeN, pThis, t_legacyMouseBridgeInput,
                           point.x, point.y, GetCurrentThreadId());
                }

                // Win10 baseline: held WM_MOUSEMOVE invokes
                // _OnMouseMove(point, true, false), which is the direct caller
                // of CEdgeUiManager::MouseDragStart once drag slop is crossed.
                CEdgeUiInput_OnMouseMove_Original(
                    t_legacyMouseBridgeInput, point, true, false);
                return S_OK;
            }

            FinishLegacyMouseBridge(point, L"left button released");
            return S_OK;
        }
    } else if (t_legacyMouseBridgeActive) {
        // Compatibility was turned off mid-gesture. Don't leave the private
        // CEdgeUiInput mouse state armed.
        FinishLegacyMouseBridge(point, L"bridge disabled or Tablet Mode left");
        return S_OK;
    }

    return CEdgeUiInput_ObservedMouseMove_Original(pThis, point, flags, anchorPoint);
}

HRESULT CEdgeInvoker_ObservedMouseButtonDown_Hook(void* pThis, POINT point) {
    const auto n = g_edgeInvokerObservedDownCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse] CEdgeInvoker::ObservedMouseButtonDown #%lu this=%p pt=(%ld,%ld) compat=%d tid=%lu ENTER",
           n, pThis, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());

    if (ArmLegacyMouseBridgeFromInvoker(pThis, point, L"ObservedMouseButtonDown")) {
        Wh_Log(L"[tablet-mouse-bridge2] suppress native invoker button-down #%lu this=%p",
               n, pThis);
        return S_OK;
    }

    const HRESULT hr = CEdgeInvoker_ObservedMouseButtonDown_Original(pThis, point);
    Wh_Log(L"[tablet-mouse] CEdgeInvoker::ObservedMouseButtonDown #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT CEdgeInvoker_ObservedMouseMove_Hook(void* pThis, POINT point, POINT anchorPoint) {
    const auto n = g_edgeInvokerObservedMoveCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 80 || (n % 100) == 0) {
        Wh_Log(L"[tablet-mouse] CEdgeInvoker::ObservedMouseMove #%lu this=%p pt=(%ld,%ld) anchor=(%ld,%ld) compat=%d tid=%lu",
               n, pThis, point.x, point.y, anchorPoint.x, anchorPoint.y,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    }

    // 25H2 sometimes bypasses the CEdgeUiInput ObservedMouse* callbacks entirely
    // and sends raw motion straight to the active invoker. Use that surviving
    // transport as a fallback, but only for the exact InputObserveStart source
    // and exact invoker acquired for it.
    if (!t_legacyMouseBridgeActive) {
        ArmLegacyMouseBridgeFromInvoker(pThis, point, L"ObservedMouseMove+LBUTTON");
    }

    if (t_legacyMouseBridgeActive && t_legacyMouseBridgeFromInvoker &&
        pThis == t_lastObservedInvoker &&
        g_bridgeLegacyMouseWin32Path.load(std::memory_order_relaxed) &&
        g_compatActive.load(std::memory_order_acquire)) {
        t_legacyMouseBridgeLastPoint = point;

        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {
            const auto bridgeN =
                g_mouseBridgeMoveCalls.fetch_add(1, std::memory_order_relaxed) + 1;
            if (bridgeN <= 80 || (bridgeN % 100) == 0) {
                Wh_Log(L"[tablet-mouse-bridge2] move-from-invoker #%lu invoker=%p input=%p pt=(%ld,%ld) -> _OnMouseMove(a=1,b=0) tid=%lu",
                       bridgeN, pThis, t_legacyMouseBridgeInput, point.x, point.y,
                       GetCurrentThreadId());
            }
            CEdgeUiInput_OnMouseMove_Original(
                t_legacyMouseBridgeInput, point, true, false);
            return S_OK;
        }

        FinishLegacyMouseBridge(point, L"left button released (invoker transport)");
        return S_OK;
    }

    return CEdgeInvoker_ObservedMouseMove_Original(pThis, point, anchorPoint);
}

HRESULT CEdgeInvoker_StartDrag_Hook(void* pThis, POINT point) {
    const auto n = g_edgeInvokerStartDragCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse] CEdgeInvoker::StartDrag #%lu this=%p pt=(%ld,%ld) compat=%d tid=%lu ENTER",
           n, pThis, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    const HRESULT hr = CEdgeInvoker_StartDrag_Original(pThis, point);
    Wh_Log(L"[tablet-mouse] CEdgeInvoker::StartDrag #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

void CTitleBarInvoker_v_ObservedMouseButtonDown_Hook(void* pThis, POINT point) {
    const auto n = g_titleBarObservedDownCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse-derived] CTitleBarInvoker::v_ObservedMouseButtonDown #%lu this=%p pt=(%ld,%ld) compat=%d tid=%lu ENTER",
           n, pThis, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    CTitleBarInvoker_v_ObservedMouseButtonDown_Original(pThis, point);
    Wh_Log(L"[tablet-mouse-derived] CTitleBarInvoker::v_ObservedMouseButtonDown #%lu EXIT", n);
}

void CTitleBarInvoker_v_ObservedMouseMove_Hook(void* pThis, POINT point) {
    const auto n = g_titleBarObservedMoveCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 80 || (n % 100) == 0) {
        Wh_Log(L"[tablet-mouse-derived] CTitleBarInvoker::v_ObservedMouseMove #%lu this=%p pt=(%ld,%ld) compat=%d tid=%lu",
               n, pThis, point.x, point.y,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    }
    CTitleBarInvoker_v_ObservedMouseMove_Original(pThis, point);
}

static const GUID kTitleBarDragServiceSid =
    {0x373E56CF, 0x0A1B, 0x4B4A, {0xA1, 0xA4, 0xA4, 0x6B, 0x25, 0xFF, 0xD7, 0xE3}};
static const GUID kTitleBarDragServiceIid =
    {0xAE50431B, 0xF86A, 0x4C3C, {0xB1, 0x1D, 0x29, 0x10, 0xD9, 0x40, 0x76, 0x13}};

HRESULT WINAPI IUnknown_QueryService_TitleBarTrace_Hook(
    IUnknown* punk, REFGUID guidService, REFIID riid, void** ppvOut) {
    const bool scoped =
        g_inTitleBarStartDrag &&
        IsEqualGUID(guidService, kTitleBarDragServiceSid) &&
        IsEqualGUID(riid, kTitleBarDragServiceIid);

    unsigned long depth = 0;
    void** providerVtable = nullptr;
    void* providerQi = nullptr;
    HMODULE providerModule = nullptr;
    wchar_t providerModuleName[MAX_PATH] = L"<none>";
    unsigned long long providerQiRva = 0;

    if (scoped) {
        depth = ++g_titleBarQueryDepth;
        providerVtable = punk ? *reinterpret_cast<void***>(punk) : nullptr;
        providerQi = providerVtable ? providerVtable[0] : nullptr;
        if (providerQi &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(providerQi), &providerModule) &&
            providerModule) {
            GetModuleFileNameW(providerModule, providerModuleName,
                               ARRAYSIZE(providerModuleName));
            providerQiRva = static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(providerQi) -
                reinterpret_cast<uintptr_t>(providerModule));
        }

        Wh_Log(L"[tablet-mouse-startsvc] QueryService ENTER depth=%lu provider=%p pvtable=%p qi=%p qiModule=%s qiRva=0x%llX tid=%lu",
               depth, punk, providerVtable, providerQi, providerModuleName,
               providerQiRva, GetCurrentThreadId());
    }

    const HRESULT hr = IUnknown_QueryService_Original(
        punk, guidService, riid, ppvOut);

    if (scoped) {
        const auto n = g_titleBarStartServiceQueryCalls.fetch_add(
                           1, std::memory_order_relaxed) +
                       1;
        void* service = (SUCCEEDED(hr) && ppvOut) ? *ppvOut : nullptr;
        void** vtable = service ? *reinterpret_cast<void***>(service) : nullptr;
        void* slot48 = vtable ? vtable[0x48 / sizeof(void*)] : nullptr;

        g_titleBarStartQuerySeen = true;
        g_titleBarStartQueryHr = hr;
        g_titleBarStartQueryService = service;
        g_titleBarStartSlot48 = slot48;

        HMODULE slotModule = nullptr;
        wchar_t moduleName[MAX_PATH] = L"<none>";
        unsigned long long slotRva = 0;
        if (slot48 &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(slot48), &slotModule) &&
            slotModule) {
            GetModuleFileNameW(slotModule, moduleName, ARRAYSIZE(moduleName));
            slotRva = static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(slot48) -
                reinterpret_cast<uintptr_t>(slotModule));
        }

        Wh_Log(L"[tablet-mouse-startsvc] QueryService EXIT #%lu depth=%lu provider=%p pvtable=%p qi=%p qiModule=%s qiRva=0x%llX hr=0x%08X service=%p vtable=%p slot+0x48=%p module=%s rva=0x%llX tid=%lu",
               n, depth, punk, providerVtable, providerQi, providerModuleName,
               providerQiRva, static_cast<unsigned>(hr), service, vtable, slot48,
               moduleName, slotRva, GetCurrentThreadId());
        --g_titleBarQueryDepth;
    }

    return hr;
}

HRESULT CTitleBarInvoker_v_StartDrag_Hook(void* pThis, POINT point) {
    const auto n = g_titleBarStartDragCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    void* serviceProvider = pThis
                                ? *reinterpret_cast<void**>(
                                      reinterpret_cast<unsigned char*>(pThis) + 0x10)
                                : nullptr;
    Wh_Log(L"[tablet-mouse-derived] CTitleBarInvoker::v_StartDrag #%lu this=%p pt=(%ld,%ld) compat=%d provider=%p tid=%lu ENTER",
           n, pThis, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           serviceProvider, GetCurrentThreadId());
    if (g_traceDesktopDragIdentity.load(std::memory_order_relaxed)) {
        POINT cursor = {};
        GetCursorPos(&cursor);
        LogDesktopWindowIdentity(L"CTitleBarInvoker::v_StartDrag",
                                 WindowFromPoint(cursor), cursor);
    }
    const bool wasInStartDrag = g_inTitleBarStartDrag;
    const bool savedQuerySeen = g_titleBarStartQuerySeen;
    const HRESULT savedQueryHr = g_titleBarStartQueryHr;
    void* const savedQueryService = g_titleBarStartQueryService;
    void* const savedSlot48 = g_titleBarStartSlot48;

    g_inTitleBarStartDrag = true;
    g_titleBarStartQuerySeen = false;
    g_titleBarStartQueryHr = S_OK;
    g_titleBarStartQueryService = nullptr;
    g_titleBarStartSlot48 = nullptr;

    const HRESULT hr = CTitleBarInvoker_v_StartDrag_Original(pThis, point);

    const bool querySeen = g_titleBarStartQuerySeen;
    const HRESULT queryHr = g_titleBarStartQueryHr;
    void* const queryService = g_titleBarStartQueryService;
    void* const slot48 = g_titleBarStartSlot48;

    g_inTitleBarStartDrag = wasInStartDrag;
    g_titleBarStartQuerySeen = savedQuerySeen;
    g_titleBarStartQueryHr = savedQueryHr;
    g_titleBarStartQueryService = savedQueryService;
    g_titleBarStartSlot48 = savedSlot48;

    const wchar_t* stage = L"QUERY-NOT-OBSERVED";
    if (querySeen) {
        stage = FAILED(queryHr) ? L"QUERY-SERVICE" : L"POST-QUERY-SLOT+0x48";
    }

    Wh_Log(L"[tablet-mouse-startsvc] CLASSIFY StartDrag #%lu final=0x%08X stage=%s querySeen=%d queryHr=0x%08X service=%p slot+0x48=%p",
           n, static_cast<unsigned>(hr), stage, querySeen ? 1 : 0,
           static_cast<unsigned>(queryHr), queryService, slot48);
    Wh_Log(L"[tablet-mouse-derived] CTitleBarInvoker::v_StartDrag #%lu EXIT hr=0x%08X serviceQueries=%lu",
           n, static_cast<unsigned>(hr),
           g_titleBarStartServiceQueryCalls.load(std::memory_order_relaxed));
    return hr;
}

HRESULT CTitleBarInvoker_v_ContinueDrag_Hook(void* pThis, POINT point) {
    const auto n = g_titleBarContinueDragCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 80 || (n % 100) == 0) {
        Wh_Log(L"[tablet-mouse-derived] CTitleBarInvoker::v_ContinueDrag #%lu this=%p pt=(%ld,%ld) compat=%d tid=%lu ENTER",
               n, pThis, point.x, point.y,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    }
    const HRESULT hr = CTitleBarInvoker_v_ContinueDrag_Original(pThis, point);
    if (n <= 80 || (n % 100) == 0) {
        Wh_Log(L"[tablet-mouse-derived] CTitleBarInvoker::v_ContinueDrag #%lu EXIT hr=0x%08X",
               n, static_cast<unsigned>(hr));
    }
    return hr;
}

HRESULT CTitleBarInvoker_v_CommitDrag_Hook(void* pThis, POINT point) {
    const auto n = g_titleBarCommitDragCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse-derived] CTitleBarInvoker::v_CommitDrag #%lu this=%p pt=(%ld,%ld) compat=%d tid=%lu ENTER",
           n, pThis, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    const HRESULT hr = CTitleBarInvoker_v_CommitDrag_Original(pThis, point);
    Wh_Log(L"[tablet-mouse-derived] CTitleBarInvoker::v_CommitDrag #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT CTitleBarInvoker_v_CancelDrag_Hook(void* pThis) {
    const auto n = g_titleBarCancelDragCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse-derived] CTitleBarInvoker::v_CancelDrag #%lu this=%p compat=%d tid=%lu ENTER",
           n, pThis, g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    const HRESULT hr = CTitleBarInvoker_v_CancelDrag_Original(pThis);
    Wh_Log(L"[tablet-mouse-derived] CTitleBarInvoker::v_CancelDrag #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

void CTaskbarInvoker_v_ObservedMouseButtonDown_Hook(void* pThis, POINT point) {
    const auto n = g_taskbarObservedDownCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse-derived] CTaskbarInvoker::v_ObservedMouseButtonDown #%lu this=%p pt=(%ld,%ld) compat=%d tid=%lu ENTER",
           n, pThis, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    CTaskbarInvoker_v_ObservedMouseButtonDown_Original(pThis, point);
    Wh_Log(L"[tablet-mouse-derived] CTaskbarInvoker::v_ObservedMouseButtonDown #%lu EXIT", n);
}

void CTaskbarInvoker_v_ObservedMouseMove_Hook(void* pThis, POINT point) {
    const auto n = g_taskbarObservedMoveCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 40 || (n % 100) == 0) {
        Wh_Log(L"[tablet-mouse-derived] CTaskbarInvoker::v_ObservedMouseMove #%lu this=%p pt=(%ld,%ld) compat=%d tid=%lu",
               n, pThis, point.x, point.y,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    }
    CTaskbarInvoker_v_ObservedMouseMove_Original(pThis, point);
}

void CEdgeUiInput_HandleObservedMouseInput_Hook(void* pThis, bool buttonDown,
                                                POINT point, POINT anchorPoint) {
    const auto n = g_mouseHandleObservedCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 40 || (n % 50) == 0) {
        Wh_Log(L"[tablet-mouse] CEdgeUiInput::_HandleObservedMouseInput #%lu this=%p down=%d pt=(%ld,%ld) anchor=(%ld,%ld) compat=%d tid=%lu",
               n, pThis, buttonDown ? 1 : 0, point.x, point.y, anchorPoint.x, anchorPoint.y,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    }
    CEdgeUiInput_HandleObservedMouseInput_Original(pThis, buttonDown, point, anchorPoint);
}

LRESULT CEdgeUiInput_WndProc_Hook(void* pThis, unsigned int msg, UINT_PTR wParam,
                                      LONG_PTR lParam, bool forwarded) {
    const bool interesting = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
                             msg == WM_NCMOUSEMOVE || msg == WM_NCLBUTTONDOWN ||
                             msg == WM_NCLBUTTONUP;
    if (interesting) {
        const auto n = g_mouseWndProcCalls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 160 || (n % 100) == 0) {
            POINT p{static_cast<short>(LOWORD(static_cast<DWORD_PTR>(lParam))),
                    static_cast<short>(HIWORD(static_cast<DWORD_PTR>(lParam)))};
            Wh_Log(L"[tablet-mouse-winmsg] CEdgeUiInput::_WndProc #%lu this=%p msg=0x%04X wParam=0x%llX pt=(%ld,%ld) forwarded=%d compat=%d tid=%lu",
                   n, pThis, msg, static_cast<unsigned long long>(wParam), p.x, p.y,
                   forwarded ? 1 : 0,
                   g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
                   GetCurrentThreadId());
        }
    }
    return CEdgeUiInput_WndProc_Original(pThis, msg, wParam, lParam, forwarded);
}

bool CEdgeUiInput_IsTopEdge_Hook(void* pThis) {
    const bool real = CEdgeUiInput_IsTopEdge_Original(pThis);
    if (real) return true;

    // Win10 and this Win11 build both implement _IsTopEdge as exactly
    // "edge index == 2 || edge index == 8". The physical top-edge object on
    // Win11 now carries index 3, so _OnMouseDown refuses to arm its private
    // mouse-drag state (byte +0x98) and immediately falls back to _OnMouseMove.
    // Emulate the Win10 result only for the exact object already proven to come
    // from a physical top-edge left-button press.
    if (g_restoreLegacyMouseDrag.load(std::memory_order_relaxed) &&
        g_bridgeLegacyMouseWin32Path.load(std::memory_order_relaxed) &&
        g_compatActive.load(std::memory_order_acquire) &&
        t_legacyMouseBridgeActive &&
        pThis == t_legacyMouseBridgeInput) {
        const auto n =
            g_mouseTopEdgeForces.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 40 || (n % 100) == 0) {
            Wh_Log(L"[tablet-mouse-topedge] force CEdgeUiInput::_IsTopEdge=1 #%lu this=%p bridgePt=(%ld,%ld) tid=%lu",
                   n, pThis, t_legacyMouseBridgeLastPoint.x,
                   t_legacyMouseBridgeLastPoint.y, GetCurrentThreadId());
        }
        return true;
    }

    return false;
}

int CEdgeUiInput_IsCurrentInputFromMouseOrPen_Hook(void* pThis) {
    const int result = CEdgeUiInput_IsCurrentInputFromMouseOrPen_Original(pThis);
    const auto n = g_mouseIsMousePenCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 80 || (n % 100) == 0) {
        Wh_Log(L"[tablet-mouse-winmsg] CEdgeUiInput::_IsCurrentInputFromMouseOrPen #%lu this=%p -> %d compat=%d tid=%lu",
               n, pThis, result,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
               GetCurrentThreadId());
    }
    return result;
}

void CEdgeUiInput_OnMouseMove_Hook(void* pThis, POINT point, bool a, bool b) {
    const auto n = g_mouseOnMoveCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 160 || (n % 100) == 0) {
        Wh_Log(L"[tablet-mouse-winmsg] CEdgeUiInput::_OnMouseMove #%lu this=%p pt=(%ld,%ld) a=%d b=%d compat=%d tid=%lu",
               n, pThis, point.x, point.y, a ? 1 : 0, b ? 1 : 0,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
               GetCurrentThreadId());
    }
    CEdgeUiInput_OnMouseMove_Original(pThis, point, a, b);
}

HRESULT CEdgeUiInput_MouseMoveToCornerOrEdge_Hook(void* pThis, int cornerOrEdge,
                                                   POINT point, bool a, bool b, bool c) {
    const auto n = g_mouseMoveToEdgeCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse-winmsg] CEdgeUiInput::_MouseMoveToCornerOrEdge #%lu this=%p edge=%d pt=(%ld,%ld) a=%d b=%d c=%d compat=%d ENTER tid=%lu",
           n, pThis, cornerOrEdge, point.x, point.y, a ? 1 : 0, b ? 1 : 0, c ? 1 : 0,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    const HRESULT hr = CEdgeUiInput_MouseMoveToCornerOrEdge_Original(pThis, cornerOrEdge,
                                                                     point, a, b, c);
    Wh_Log(L"[tablet-mouse-winmsg] CEdgeUiInput::_MouseMoveToCornerOrEdge #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

HRESULT CEdgeUiManager_MouseHitCornerOrEdge_Hook(void* pThis, void* edgeInput,
                                                  bool a, bool b, POINT point,
                                                  bool* outHit, void** outInvoker) {
    const auto n = g_mouseManagerHitCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse-winmsg] CEdgeUiManager::MouseHitCornerOrEdge #%lu edgeInput=%p a=%d b=%d pt=(%ld,%ld) compat=%d ENTER tid=%lu",
           n, edgeInput, a ? 1 : 0, b ? 1 : 0, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());

    const bool oldScopeActive = t_mouseHitScopeActive;
    const bool oldScopePhysicalTop = t_mouseHitScopePhysicalTop;
    void* const oldScopeEdgeInput = t_mouseHitScopeEdgeInput;

    t_mouseHitScopeActive = true;
    t_mouseHitScopePhysicalTop = IsPointAtMonitorTopEdge(point);
    t_mouseHitScopeEdgeInput = edgeInput;

    const HRESULT hr = CEdgeUiManager_MouseHitCornerOrEdge_Original(
        pThis, edgeInput, a, b, point, outHit, outInvoker);

    t_mouseHitScopeActive = oldScopeActive;
    t_mouseHitScopePhysicalTop = oldScopePhysicalTop;
    t_mouseHitScopeEdgeInput = oldScopeEdgeInput;

    Wh_Log(L"[tablet-mouse-winmsg] CEdgeUiManager::MouseHitCornerOrEdge #%lu EXIT hr=0x%08X hit=%d invoker=%p physicalTop=%d",
           n, static_cast<unsigned>(hr), outHit ? (*outHit ? 1 : 0) : -1,
           outInvoker ? *outInvoker : nullptr,
           IsPointAtMonitorTopEdge(point) ? 1 : 0);
    return hr;
}

HRESULT CEdgeInvoker_HitCornerOrEdge_Hook(void* pThis, int cornerOrEdge, POINT point,
                                           int* notification, int arg) {
    const auto n = g_mouseInvokerHitCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse-winmsg] CEdgeInvoker::HitCornerOrEdge #%lu this=%p edge=%d pt=(%ld,%ld) arg=%d compat=%d ENTER tid=%lu",
           n, pThis, cornerOrEdge, point.x, point.y, arg,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    const HRESULT hr = CEdgeInvoker_HitCornerOrEdge_Original(pThis, cornerOrEdge,
                                                             point, notification, arg);
    Wh_Log(L"[tablet-mouse-winmsg] CEdgeInvoker::HitCornerOrEdge #%lu EXIT hr=0x%08X notification=%d",
           n, static_cast<unsigned>(hr), notification ? *notification : -1);
    return hr;
}

HRESULT CTitleBarInvoker_v_Invoke_Hook(void* pThis, bool a, POINT point, int rawType) {
    const auto n = g_titleBarInvokeCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse-winmsg] CTitleBarInvoker::v_Invoke #%lu this=%p a=%d pt=(%ld,%ld) rawType=%d compat=%d ENTER tid=%lu",
           n, pThis, a ? 1 : 0, point.x, point.y, rawType,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    const HRESULT hr = CTitleBarInvoker_v_Invoke_Original(pThis, a, point, rawType);
    Wh_Log(L"[tablet-mouse-winmsg] CTitleBarInvoker::v_Invoke #%lu EXIT hr=0x%08X",
           n, static_cast<unsigned>(hr));
    return hr;
}

void CEdgeUiInput_OnMouseDown_Hook(void* pThis, POINT point, bool fromRawInput) {
    const auto n = g_mouseOnDownCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse] CEdgeUiInput::_OnMouseDown #%lu this=%p pt=(%ld,%ld) raw=%d compat=%d tid=%lu",
           n, pThis, point.x, point.y, fromRawInput ? 1 : 0,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());
    CEdgeUiInput_OnMouseDown_Original(pThis, point, fromRawInput);
}

void CEdgeUiInput_OnMouseUp_Hook(void* pThis, POINT point) {
    const auto n = g_mouseOnUpCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-mouse] CEdgeUiInput::_OnMouseUp #%lu this=%p pt=(%ld,%ld) compat=%d tid=%lu",
           n, pThis, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0, GetCurrentThreadId());

    if (t_legacyMouseBridgeActive && pThis == t_legacyMouseBridgeInput) {
        t_legacyMouseBridgeActive = false;
        t_legacyMouseBridgeInput = nullptr;
        t_legacyMouseBridgeLastPoint = point;
        Wh_Log(L"[tablet-mouse-bridge] native _OnMouseUp observed; bridge state cleared tid=%lu",
               GetCurrentThreadId());
    }

    CEdgeUiInput_OnMouseUp_Original(pThis, point);
}

bool __cdecl Twinui_TabletModeHelpers_IsTabletMode_EdgeScoped_Hook() {
    const bool real = Twinui_TabletModeHelpers_IsTabletMode_Original();
    if (!g_restoreLegacyEdgeSwipeGate.load(std::memory_order_relaxed) ||
        !g_compatActive.load(std::memory_order_acquire) || !g_twinui) {
        return real;
    }

    const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const auto base = reinterpret_cast<uintptr_t>(g_twinui);
    if (caller < base) return real;

    const uintptr_t rva = caller - base;
    const bool edgeLayout = rva == kTwinuiLayoutEdgeUiInputsIsTabletReturnRva;
    const bool edgeUpdate = rva == kTwinuiEdgePointerUpdateIsTabletReturnRva;
    if (!edgeLayout && !edgeUpdate) return real;

    const auto n = g_edgeTabletGateForcedCalls.fetch_add(
                       1, std::memory_order_relaxed) + 1;
    if (n <= 40 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-edge-gate] force TabletModeHelpers::IsTabletMode=1 #%lu caller=%s rva=0x%llX real=%d tid=%lu",
               n, edgeLayout ? L"CEdgeUiManager::_LayoutEdgeUiInputs"
                             : L"CEdgeUiInput::_OnPointerUpdate",
               static_cast<unsigned long long>(rva), real ? 1 : 0,
               GetCurrentThreadId());
    }
    return true;
}

bool TitlebarOverlay_ShouldForce() {
    return g_restoreTabletTitlebarOverlay.load(std::memory_order_relaxed) &&
           g_compatActive.load(std::memory_order_acquire);
}

bool __cdecl Twinui_TitlebarOverlayHelpers_OverlayTitlebarsInTabletMode_Hook() {
    const bool real =
        Twinui_TitlebarOverlayHelpers_OverlayTitlebarsInTabletMode_Original();
    if (!TitlebarOverlay_ShouldForce()) {
        return real;
    }

    const auto n = g_titlebarOverlayForcedCalls.fetch_add(
                       1, std::memory_order_relaxed) + 1;
    if (n <= 20 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-titlebar] twinui OverlayTitlebarsInTabletMode -> 1 #%lu real=%d tid=%lu",
               n, real ? 1 : 0, GetCurrentThreadId());
    }
    return true;
}

bool __cdecl Pcshell_TitlebarOverlayHelpers_OverlayTitlebarsInTabletMode_Hook() {
    const bool real =
        Pcshell_TitlebarOverlayHelpers_OverlayTitlebarsInTabletMode_Original();
    if (!TitlebarOverlay_ShouldForce()) {
        return real;
    }

    const auto n = g_titlebarOverlayForcedCalls.fetch_add(
                       1, std::memory_order_relaxed) + 1;
    if (n <= 20 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-titlebar] pcshell OverlayTitlebarsInTabletMode -> 1 #%lu real=%d tid=%lu",
               n, real ? 1 : 0, GetCurrentThreadId());
    }
    return true;
}

uint64_t __cdecl ViewPresentationMediator_ComputeMaskAndFeatures_Hook(
    unsigned int scenario) {
    uint64_t packed =
        ViewPresentationMediator_ComputeMaskAndFeatures_Original(scenario);

    const uint32_t nativeMask = static_cast<uint32_t>(packed);
    const uint32_t nativeFeatures = static_cast<uint32_t>(packed >> 32);
    uint32_t features = nativeFeatures;

    if (g_restoreLegacyWindowManagementProfile.load(std::memory_order_relaxed) &&
        g_compatActive.load(std::memory_order_acquire) &&
        (scenario & 0x1u) != 0) {
        // Later Win10 Tablet scenario 1 uses low byte 0xBF:
        // feature 0x80 ON, feature 0x40 OFF. Preserve every other native bit.
        features = (features | 0x80u) & ~0x40u;

        if (features != nativeFeatures) {
            const auto n = g_windowManagementProfileAdjustments.fetch_add(
                               1, std::memory_order_relaxed) + 1;
            if (n <= 40 || g_verbose.load(std::memory_order_relaxed)) {
                Wh_Log(L"[tablet-window-profile] #%lu scenario=0x%X mask=0x%08X features native=0x%08X -> 0x%08X compat=1 tid=%lu",
                       n, scenario, nativeMask, nativeFeatures, features,
                       GetCurrentThreadId());
            }
        }
    }

    return static_cast<uint64_t>(nativeMask) |
           (static_cast<uint64_t>(features) << 32);
}

HRESULT CEdgeUiManager_LayoutEdgeUiInputs_Hook(void* pThis, int reason) {
    const auto n = g_edgeLayoutCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 30 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-edge] CEdgeUiManager::_LayoutEdgeUiInputs #%lu reason=%d compat=%d ENTER tid=%lu",
               n, reason,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
               GetCurrentThreadId());
    }
    const HRESULT hr = CEdgeUiManager_LayoutEdgeUiInputs_Original(pThis, reason);
    if (n <= 30 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-edge] CEdgeUiManager::_LayoutEdgeUiInputs #%lu EXIT hr=0x%08X",
               n, static_cast<unsigned>(hr));
    }
    return hr;
}

void CEdgeUiInput_OnPointerDown_Hook(
    void* pThis, unsigned int pointerId, POINT point, bool isPrimary) {
    const auto n = g_edgePointerDownCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-edge] CEdgeUiInput::_OnPointerDown #%lu this=%p id=%u pt=(%ld,%ld) primary=%d compat=%d tid=%lu",
           n, pThis, pointerId, point.x, point.y, isPrimary ? 1 : 0,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    CEdgeUiInput_OnPointerDown_Original(
        pThis, pointerId, point, isPrimary);
}

void CEdgeUiInput_OnPointerUpdate_Hook(
    void* pThis, unsigned int pointerId, POINT point) {
    const auto n =
        g_edgePointerUpdateCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 50 || g_verbose.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-edge] CEdgeUiInput::_OnPointerUpdate #%lu this=%p id=%u pt=(%ld,%ld) compat=%d tid=%lu",
               n, pThis, pointerId, point.x, point.y,
               g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
               GetCurrentThreadId());
    }
    CEdgeUiInput_OnPointerUpdate_Original(pThis, pointerId, point);
}

void CEdgeUiInput_OnPointerUp_Hook(
    void* pThis, unsigned int pointerId, POINT point) {
    const auto n = g_edgePointerUpCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    Wh_Log(L"[tablet-edge] CEdgeUiInput::_OnPointerUp #%lu this=%p id=%u pt=(%ld,%ld) compat=%d tid=%lu",
           n, pThis, pointerId, point.x, point.y,
           g_compatActive.load(std::memory_order_acquire) ? 1 : 0,
           GetCurrentThreadId());
    CEdgeUiInput_OnPointerUp_Original(pThis, pointerId, point);
}

void* FindPrivateSymbolAddress(HMODULE module, PCWSTR decoratedName) {
    if (!module || !decoratedName) return nullptr;

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;

    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, &options, &symbol);
    if (!search) return nullptr;

    void* result = nullptr;
    do {
        PCWSTR decorated = symbol.symbolDecorated;
        if (!decorated || !*decorated) decorated = symbol.symbol;
        if (decorated && wcscmp(decorated, decoratedName) == 0) {
            result = symbol.address;
            break;
        }
    } while (Wh_FindNextSymbol(search, &symbol));

    Wh_FindCloseSymbol(search);
    return result;
}

struct ExactHook {
    PCWSTR decoratedName;
    void* hook;
    void** original;
    PCWSTR shortName;
    bool installed;
};

size_t InstallExactHooks(HMODULE module, ExactHook* hooks, size_t hookCount,
                         PCWSTR moduleName) {
    if (!module) return 0;

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;

    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, &options, &symbol);
    if (!search) {
        Wh_Log(L"[tablet-compat] symbol enumeration failed for %s", moduleName);
        return 0;
    }

    size_t installed = 0;
    do {
        PCWSTR decorated = symbol.symbolDecorated;
        if (!decorated || !*decorated) decorated = symbol.symbol;
        if (!decorated || !*decorated) continue;

        for (size_t i = 0; i < hookCount; ++i) {
            auto& h = hooks[i];
            if (h.installed || wcscmp(decorated, h.decoratedName) != 0) continue;

            if (Wh_SetFunctionHook(symbol.address, h.hook, h.original)) {
                h.installed = true;
                ++installed;
                Wh_Log(L"[tablet-compat] hooked %s at %p",
                       h.shortName, symbol.address);
            } else {
                Wh_Log(L"[tablet-compat] found %s but hook installation failed",
                       h.shortName);
            }
            break;
        }
    } while (Wh_FindNextSymbol(search, &symbol));

    Wh_FindCloseSymbol(search);
    return installed;
}

bool InstallTabletWindowGestureTraceHooks() {
    if (!g_traceLegacyTabletWindowGestures.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-gesture] tracer disabled by setting");
        return true;
    }

    HMODULE pcshell = GetModuleHandleW(L"twinui.pcshell.dll");
    if (!pcshell) {
        Wh_Log(L"[tablet-gesture] twinui.pcshell.dll isn't loaded yet; tracer not installed");
        return false;
    }

    ExactHook hooks[] = {
        {L"?RuntimeClassInitialize@TabletModeInputHandler@@QEAAJW4SHOW_WINDOW_ARRANGEMENT_VIEW_SOURCE@@PEAUITabletModeInputCallback@@@Z",
         reinterpret_cast<void*>(&TabletModeInputHandler_RuntimeClassInitialize_Hook),
         reinterpret_cast<void**>(&TabletModeInputHandler_RuntimeClassInitialize_Original),
         L"TabletModeInputHandler::RuntimeClassInitialize", false},
        {L"?PointerDown@TabletModeInputHandler@@UEAAXIUtagPOINT@@@Z",
         reinterpret_cast<void*>(&TabletModeInputHandler_PointerDown_Hook),
         reinterpret_cast<void**>(&TabletModeInputHandler_PointerDown_Original),
         L"TabletModeInputHandler::PointerDown", false},
        {L"?PointerUpdate@TabletModeInputHandler@@UEAAXIUtagPOINT@@@Z",
         reinterpret_cast<void*>(&TabletModeInputHandler_PointerUpdate_Hook),
         reinterpret_cast<void**>(&TabletModeInputHandler_PointerUpdate_Original),
         L"TabletModeInputHandler::PointerUpdate", false},
        {L"?PointerUp@TabletModeInputHandler@@UEAAXIUtagPOINT@@@Z",
         reinterpret_cast<void*>(&TabletModeInputHandler_PointerUp_Hook),
         reinterpret_cast<void**>(&TabletModeInputHandler_PointerUp_Original),
         L"TabletModeInputHandler::PointerUp", false},
        {L"?StartSwipe@TabletModeViewManager@@UEAAJIUtagPOINT@@@Z",
         reinterpret_cast<void*>(&TabletModeViewManager_StartSwipe_Hook),
         reinterpret_cast<void**>(&TabletModeViewManager_StartSwipe_Original),
         L"TabletModeViewManager::StartSwipe", false},
        {L"?StartExtendedSwipe@TabletModeViewManager@@UEAAJUtagPOINT@@I0@Z",
         reinterpret_cast<void*>(&TabletModeViewManager_StartExtendedSwipe_Hook),
         reinterpret_cast<void**>(&TabletModeViewManager_StartExtendedSwipe_Original),
         L"TabletModeViewManager::StartExtendedSwipe", false},
        {L"?ContinueSwipe@TabletModeViewManager@@UEAAJIUtagPOINT@@@Z",
         reinterpret_cast<void*>(&TabletModeViewManager_ContinueSwipe_Hook),
         reinterpret_cast<void**>(&TabletModeViewManager_ContinueSwipe_Original),
         L"TabletModeViewManager::ContinueSwipe", false},
        {L"?CommitSwipe@TabletModeViewManager@@UEAAJIUtagPOINT@@@Z",
         reinterpret_cast<void*>(&TabletModeViewManager_CommitSwipe_Hook),
         reinterpret_cast<void**>(&TabletModeViewManager_CommitSwipe_Original),
         L"TabletModeViewManager::CommitSwipe", false},
        {L"?CancelSwipe@TabletModeViewManager@@UEAAJXZ",
         reinterpret_cast<void*>(&TabletModeViewManager_CancelSwipe_Hook),
         reinterpret_cast<void**>(&TabletModeViewManager_CancelSwipe_Original),
         L"TabletModeViewManager::CancelSwipe", false},
        {L"?StartDrag@TabletModeViewManager@@UEAAJUtagPOINT@@@Z",
         reinterpret_cast<void*>(&TabletModeViewManager_StartDrag_Hook),
         reinterpret_cast<void**>(&TabletModeViewManager_StartDrag_Original),
         L"TabletModeViewManager::StartDrag", false},
        {L"?ContinueDrag@TabletModeViewManager@@UEAAJUtagPOINT@@@Z",
         reinterpret_cast<void*>(&TabletModeViewManager_ContinueDrag_Hook),
         reinterpret_cast<void**>(&TabletModeViewManager_ContinueDrag_Original),
         L"TabletModeViewManager::ContinueDrag", false},
        {L"?CommitDrag@TabletModeViewManager@@UEAAJUtagPOINT@@@Z",
         reinterpret_cast<void*>(&TabletModeViewManager_CommitDrag_Hook),
         reinterpret_cast<void**>(&TabletModeViewManager_CommitDrag_Original),
         L"TabletModeViewManager::CommitDrag", false},
        {L"?CancelDrag@TabletModeViewManager@@UEAAJXZ",
         reinterpret_cast<void*>(&TabletModeViewManager_CancelDrag_Hook),
         reinterpret_cast<void**>(&TabletModeViewManager_CancelDrag_Original),
         L"TabletModeViewManager::CancelDrag", false},
        {L"?ShowAppResizeView@TabletModeViewManager@@UEAAJPEAUIApplicationView@@W4MOVE_SIZE_TYPE@@UtagPOINT@@@Z",
         reinterpret_cast<void*>(&TabletModeViewManager_ShowAppResizeView_Hook),
         reinterpret_cast<void**>(&TabletModeViewManager_ShowAppResizeView_Original),
         L"TabletModeViewManager::ShowAppResizeView", false},
        {L"?MoveSizeAttempted@TabletModeViewManager@@UEAAJPEAUIApplicationView@@W4MOVE_SIZE_TYPE@@@Z",
         reinterpret_cast<void*>(&TabletModeViewManager_MoveSizeAttempted_Hook),
         reinterpret_cast<void**>(&TabletModeViewManager_MoveSizeAttempted_Original),
         L"TabletModeViewManager::MoveSizeAttempted", false},
        {L"?SnapToLocation@SnapServiceProvider@@UEAAJPEAUIApplicationView@@W4SNAP_LOCATION@@W4SNAP_TO_LOCATION_FLAGS@@PEAUtagRECT@@@Z",
         reinterpret_cast<void*>(&SnapServiceProvider_SnapToLocation_Hook),
         reinterpret_cast<void**>(&SnapServiceProvider_SnapToLocation_Original),
         L"SnapServiceProvider::SnapToLocation", false},
        {L"?HandleTabletModeSnapping@SnapServiceProvider@@AEAAJPEAUIApplicationView@@W4SNAP_LOCATION@@PEAUtagRECT@@@Z",
         reinterpret_cast<void*>(&SnapServiceProvider_HandleTabletModeSnapping_Hook),
         reinterpret_cast<void**>(&SnapServiceProvider_HandleTabletModeSnapping_Original),
         L"SnapServiceProvider::HandleTabletModeSnapping", false},
        {L"?HandleDesktopModeSnapping@SnapServiceProvider@@AEAAJPEAUIApplicationView@@W4SNAP_LOCATION@@W4SNAP_TO_LOCATION_FLAGS@@PEAUtagRECT@@@Z",
         reinterpret_cast<void*>(&SnapServiceProvider_HandleDesktopModeSnapping_Hook),
         reinterpret_cast<void**>(&SnapServiceProvider_HandleDesktopModeSnapping_Original),
         L"SnapServiceProvider::HandleDesktopModeSnapping", false},
        {L"?ShowSnapAssist@TabletModePositioner@@AEAAJXZ",
         reinterpret_cast<void*>(&TabletModePositioner_ShowSnapAssist_Hook),
         reinterpret_cast<void**>(&TabletModePositioner_ShowSnapAssist_Original),
         L"TabletModePositioner::ShowSnapAssist", false},
        {L"?CreateWindowArrangementViewForDrag@TabletModeViewManager@@UEAAJPEAUIApplicationView@@PEAPEAUIUnknown@@@Z",
         reinterpret_cast<void*>(&TabletModeViewManager_CreateWindowArrangementViewForDrag_Hook),
         reinterpret_cast<void**>(&TabletModeViewManager_CreateWindowArrangementViewForDrag_Original),
         L"TabletModeViewManager::CreateWindowArrangementViewForDrag", false},
        {L"?OnMoveSizeAttempted@ViewPresentationMediator@@UEAAXPEAUHWND__@@PEAUIApplicationView@@KK@Z",
         reinterpret_cast<void*>(&ViewPresentationMediator_OnMoveSizeAttempted_Hook),
         reinterpret_cast<void**>(&ViewPresentationMediator_OnMoveSizeAttempted_Original),
         L"ViewPresentationMediator::OnMoveSizeAttempted", false},
        {L"?OnMoveSizeAttempted@TabletModePositionerManager@@UEAAXPEAUHWND__@@PEAUIApplicationView@@KK@Z",
         reinterpret_cast<void*>(&TabletModePositionerManager_OnMoveSizeAttempted_Hook),
         reinterpret_cast<void**>(&TabletModePositionerManager_OnMoveSizeAttempted_Original),
         L"TabletModePositionerManager::OnMoveSizeAttempted", false},
        {L"?TabletModeChanged@ShellPositionerManager@@UEAAJW4_TABLETMODESTATE@@@Z",
         reinterpret_cast<void*>(&ShellPositionerManager_TabletModeChanged_Hook),
         reinterpret_cast<void**>(&ShellPositionerManager_TabletModeChanged_Original),
         L"ShellPositionerManager::TabletModeChanged", false},
        {L"?TabletModeChanged@TabletModePositioner@@UEAAJW4_TABLETMODESTATE@@@Z",
         reinterpret_cast<void*>(&TabletModePositioner_TabletModeChanged_Hook),
         reinterpret_cast<void**>(&TabletModePositioner_TabletModeChanged_Original),
         L"TabletModePositioner::TabletModeChanged", false},
        {L"?TabletModeChanged@CDesktopPositioner@@UEAAJW4_TABLETMODESTATE@@@Z",
         reinterpret_cast<void*>(&CDesktopPositioner_TabletModeChanged_Hook),
         reinterpret_cast<void**>(&CDesktopPositioner_TabletModeChanged_Original),
         L"CDesktopPositioner::TabletModeChanged", false},
        {L"?SwapPositionersForViews@ShellPositionerManager@@UEAAJW4SHELL_POSITIONER_TYPE@@0@Z",
         reinterpret_cast<void*>(&ShellPositionerManager_SwapPositionersForViews_Hook),
         reinterpret_cast<void**>(&ShellPositionerManager_SwapPositionersForViews_Original),
         L"ShellPositionerManager::SwapPositionersForViews", false},
        {L"?ChangePositionerForView@ShellPositionerManager@@AEAAJPEAUIApplicationView@@W4SHELL_POSITIONER_TYPE@@_N@Z",
         reinterpret_cast<void*>(&ShellPositionerManager_ChangePositionerForView_Hook),
         reinterpret_cast<void**>(&ShellPositionerManager_ChangePositionerForView_Original),
         L"ShellPositionerManager::ChangePositionerForView", false},
        {L"?PerformPositionerHandoff@ShellPositionerManager@@AEAAJPEAUIApplicationView@@W4SHELL_POSITIONER_TYPE@@1@Z",
         reinterpret_cast<void*>(&ShellPositionerManager_PerformPositionerHandoffView_Hook),
         reinterpret_cast<void**>(&ShellPositionerManager_PerformPositionerHandoffView_Original),
         L"ShellPositionerManager::PerformPositionerHandoff(view)", false},
        {L"?PerformPositionerHandoff@ShellPositionerManager@@AEAAJPEAUIObjectArray@@PEAUIImmersiveMonitor@@1PEAUIPresentationRequestedArgs@@W4SHELL_POSITIONER_TYPE@@3@Z",
         reinterpret_cast<void*>(&ShellPositionerManager_PerformPositionerHandoffArray_Hook),
         reinterpret_cast<void**>(&ShellPositionerManager_PerformPositionerHandoffArray_Original),
         L"ShellPositionerManager::PerformPositionerHandoff(array)", false},
        {L"?PerformPositionerHandoff@ShellPositionerManager@@AEAAJPEAUIApplicationView@@PEAUIImmersiveMonitor@@1PEAUIPresentationRequestedArgs@@W4SHELL_POSITIONER_TYPE@@3@Z",
         reinterpret_cast<void*>(&ShellPositionerManager_PerformPositionerHandoffApp_Hook),
         reinterpret_cast<void**>(&ShellPositionerManager_PerformPositionerHandoffApp_Original),
         L"ShellPositionerManager::PerformPositionerHandoff(app)", false},
        {L"?GetChromeConfigurationForView@CDesktopPositioner@@UEAAJPEAUIApplicationView@@PEAHPEAPEAUIApplicationViewTitlebarConfiguration@@@Z",
         reinterpret_cast<void*>(&CDesktopPositioner_GetChromeConfigurationForView_Hook),
         reinterpret_cast<void**>(&CDesktopPositioner_GetChromeConfigurationForView_Original),
         L"CDesktopPositioner::GetChromeConfigurationForView", false},
        {L"?GetChromeConfigurationForView@TabletModePositioner@@UEAAJPEAUIApplicationView@@PEAHPEAPEAUIApplicationViewTitlebarConfiguration@@@Z",
         reinterpret_cast<void*>(&TabletModePositioner_GetChromeConfigurationForView_Hook),
         reinterpret_cast<void**>(&TabletModePositioner_GetChromeConfigurationForView_Original),
         L"TabletModePositioner::GetChromeConfigurationForView", false},
    };

    const size_t installed = InstallExactHooks(
        pcshell, hooks, ARRAYSIZE(hooks), L"twinui.pcshell.dll");
    if (installed != 0) {
        // Don't retry a partially queued set; retrying could double-hook the
        // symbols that were already queued successfully.
        g_pcshellGestureHooksQueued.store(true, std::memory_order_release);
    }
    Wh_Log(L"[tablet-gesture] installed %llu/%llu read-only routing/chrome/gesture trace hooks; no legacy TabletModeHelpers forcing",
           static_cast<unsigned long long>(installed),
           static_cast<unsigned long long>(ARRAYSIZE(hooks)));
    return installed == ARRAYSIZE(hooks);
}


bool InstallTabletTitlebarOverlayHooks() {
    if (!g_restoreTabletTitlebarOverlay.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-titlebar] overlay restoration disabled by setting");
        return true;
    }

    if (!g_twinuiTitlebarHookQueued.load(std::memory_order_acquire)) {
        HMODULE twinui = g_twinui ? g_twinui : GetModuleHandleW(L"twinui.dll");
        if (twinui) {
            ExactHook hooks[] = {
                {L"?OverlayTitlebarsInTabletMode@TitlebarOverlayHelpers@@YA_NXZ",
                 reinterpret_cast<void*>(&Twinui_TitlebarOverlayHelpers_OverlayTitlebarsInTabletMode_Hook),
                 reinterpret_cast<void**>(&Twinui_TitlebarOverlayHelpers_OverlayTitlebarsInTabletMode_Original),
                 L"twinui!TitlebarOverlayHelpers::OverlayTitlebarsInTabletMode", false},
            };
            const size_t installed =
                InstallExactHooks(twinui, hooks, ARRAYSIZE(hooks), L"twinui.dll");
            if (installed == 1) {
                g_twinuiTitlebarHookQueued.store(true, std::memory_order_release);
            }
        } else {
            Wh_Log(L"[tablet-titlebar] twinui.dll isn't loaded; twinui titlebar hook skipped");
        }
    }

    if (!g_pcshellTitlebarHookQueued.load(std::memory_order_acquire)) {
        HMODULE pcshell = GetModuleHandleW(L"twinui.pcshell.dll");
        if (pcshell) {
            ExactHook hooks[] = {
                {L"?OverlayTitlebarsInTabletMode@TitlebarOverlayHelpers@@YA_NXZ",
                 reinterpret_cast<void*>(&Pcshell_TitlebarOverlayHelpers_OverlayTitlebarsInTabletMode_Hook),
                 reinterpret_cast<void**>(&Pcshell_TitlebarOverlayHelpers_OverlayTitlebarsInTabletMode_Original),
                 L"twinui.pcshell!TitlebarOverlayHelpers::OverlayTitlebarsInTabletMode", false},
            };
            const size_t installed =
                InstallExactHooks(pcshell, hooks, ARRAYSIZE(hooks), L"twinui.pcshell.dll");
            if (installed == 1) {
                g_pcshellTitlebarHookQueued.store(true, std::memory_order_release);
            }
        } else {
            Wh_Log(L"[tablet-titlebar] twinui.pcshell.dll isn't loaded; pcshell titlebar hook deferred");
        }
    }

    const unsigned ready =
        (g_twinuiTitlebarHookQueued.load(std::memory_order_acquire) ? 1u : 0u) +
        (g_pcshellTitlebarHookQueued.load(std::memory_order_acquire) ? 1u : 0u);
    Wh_Log(L"[tablet-titlebar] ready %u/2 scoped titlebar overlay hooks; snap TabletModeHelpers gates untouched",
           ready);
    return ready == 2;
}

bool InstallShellWindowManagementProfileHook() {
    if (!g_restoreLegacyWindowManagementProfile.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-window-profile] compatibility experiment disabled by setting");
        return true;
    }

    if (g_pcshellWindowManagementHookQueued.load(std::memory_order_acquire)) {
        return true;
    }

    HMODULE pcshell = GetModuleHandleW(L"twinui.pcshell.dll");
    if (!pcshell) {
        Wh_Log(L"[tablet-window-profile] twinui.pcshell.dll isn't loaded; hook deferred");
        return false;
    }

    ExactHook hooks[] = {
        {L"?ComputeMaskAndFeaturesForScenarios@ViewPresentationMediator@@CA?AUBehaviorMaskAndFeatures@1@W4WINDOW_MANAGEMENT_BEHAVIOR_SCENARIO@@@Z",
         reinterpret_cast<void*>(&ViewPresentationMediator_ComputeMaskAndFeatures_Hook),
         reinterpret_cast<void**>(&ViewPresentationMediator_ComputeMaskAndFeatures_Original),
         L"ViewPresentationMediator::ComputeMaskAndFeaturesForScenarios", false},
    };

    const size_t installed =
        InstallExactHooks(pcshell, hooks, ARRAYSIZE(hooks), L"twinui.pcshell.dll");
    if (installed == 1) {
        g_pcshellWindowManagementHookQueued.store(true, std::memory_order_release);
        Wh_Log(L"[tablet-window-profile] exact 25H2 scenario-profile hook queued; Tablet scenario restores 0x80=1 / 0x40=0 only");
        return true;
    }

    Wh_Log(L"[tablet-window-profile] exact scenario-profile symbol wasn't hooked; no behavior change will occur");
    return false;
}

bool NeedDeferredPcshellHooks() {
    const bool needGesture =
        g_traceLegacyTabletWindowGestures.load(std::memory_order_relaxed) &&
        !g_pcshellGestureHooksQueued.load(std::memory_order_acquire);
    const bool needTitlebar =
        g_restoreTabletTitlebarOverlay.load(std::memory_order_relaxed) &&
        !g_pcshellTitlebarHookQueued.load(std::memory_order_acquire);
    const bool needWindowProfile =
        g_restoreLegacyWindowManagementProfile.load(std::memory_order_relaxed) &&
        !g_pcshellWindowManagementHookQueued.load(std::memory_order_acquire);
    return needGesture || needTitlebar || needWindowProfile;
}

bool InstallDeferredPcshellHooksIfReady() {
    if (!NeedDeferredPcshellHooks()) return true;

    HMODULE pcshell = GetModuleHandleW(L"twinui.pcshell.dll");
    if (!pcshell) return false;

    Wh_Log(L"[tablet-pcshell] twinui.pcshell.dll appeared at %p; installing deferred hooks",
           pcshell);

    if (g_traceLegacyTabletWindowGestures.load(std::memory_order_relaxed) &&
        !g_pcshellGestureHooksQueued.load(std::memory_order_acquire)) {
        InstallTabletWindowGestureTraceHooks();
    }

    if (g_restoreTabletTitlebarOverlay.load(std::memory_order_relaxed) &&
        !g_pcshellTitlebarHookQueued.load(std::memory_order_acquire)) {
        InstallTabletTitlebarOverlayHooks();
    }

    if (g_restoreLegacyWindowManagementProfile.load(std::memory_order_relaxed) &&
        !g_pcshellWindowManagementHookQueued.load(std::memory_order_acquire)) {
        InstallShellWindowManagementProfileHook();
    }

    if (!Wh_ApplyHookOperations()) {
        Wh_Log(L"[tablet-pcshell] Wh_ApplyHookOperations failed for deferred pcshell hooks");
        return false;
    }

    const bool ready = !NeedDeferredPcshellHooks();
    Wh_Log(L"[tablet-pcshell] deferred hook application complete; gesture=%d titlebar=%d windowProfile=%d ready=%d",
           g_pcshellGestureHooksQueued.load(std::memory_order_acquire) ? 1 : 0,
           g_pcshellTitlebarHookQueued.load(std::memory_order_acquire) ? 1 : 0,
           g_pcshellWindowManagementHookQueued.load(std::memory_order_acquire) ? 1 : 0,
           ready ? 1 : 0);
    return ready;
}

bool InstallTitleBarStartDragServiceTraceHook() {
    // CTitleBarInvoker::v_StartDrag imports IUnknown_QueryService through the exact
    // api-ms-win-shcore-comhelpers-l1-1-0 contract on both supplied twinui builds.
    // Resolve that contract first so the hook lands on the same SHCore host target
    // used by twinui's delay-IAT. shlwapi is retained only as a compatibility fallback.
    const wchar_t* moduleCandidates[] = {
        L"api-ms-win-shcore-comhelpers-l1-1-0.dll",
        L"SHCore.dll",
        L"shlwapi.dll",
    };

    FARPROC proc = nullptr;
    HMODULE resolvedFrom = nullptr;
    const wchar_t* resolvedName = L"<none>";

    for (const wchar_t* candidate : moduleCandidates) {
        HMODULE module = GetModuleHandleW(candidate);
        if (!module) {
            module = LoadLibraryW(candidate);
        }
        if (!module) {
            continue;
        }

        FARPROC candidateProc = GetProcAddress(module, "IUnknown_QueryService");
        if (candidateProc) {
            proc = candidateProc;
            resolvedFrom = module;
            resolvedName = candidate;
            break;
        }
    }

    if (!proc) {
        Wh_Log(L"[tablet-mouse-startsvc] could not resolve api-ms-win-shcore-comhelpers-l1-1-0!IUnknown_QueryService (or fallbacks): %lu",
               GetLastError());
        return false;
    }

    HMODULE targetModule = nullptr;
    wchar_t targetModuleName[MAX_PATH] = L"<unknown>";
    unsigned long long targetRva = 0;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(proc), &targetModule) &&
        targetModule) {
        GetModuleFileNameW(targetModule, targetModuleName, ARRAYSIZE(targetModuleName));
        targetRva = static_cast<unsigned long long>(
            reinterpret_cast<uintptr_t>(proc) -
            reinterpret_cast<uintptr_t>(targetModule));
    }

    if (!Wh_SetFunctionHook(
            reinterpret_cast<void*>(proc),
            reinterpret_cast<void*>(&IUnknown_QueryService_TitleBarTrace_Hook),
            reinterpret_cast<void**>(&IUnknown_QueryService_Original))) {
        Wh_Log(L"[tablet-mouse-startsvc] failed to hook %s!IUnknown_QueryService at %p",
               resolvedName, reinterpret_cast<void*>(proc));
        return false;
    }

    Wh_Log(L"[tablet-mouse-startsvc] queued exact comhelpers IUnknown_QueryService trace resolvedFrom=%s handle=%p target=%p module=%s rva=0x%llX; no COM vtable patches",
           resolvedName, resolvedFrom, reinterpret_cast<void*>(proc),
           targetModuleName, targetRva);
    return true;
}

bool InstallTwinuiEdgeInputTraceHooks() {
    const bool trace = g_traceLegacyTabletWindowGestures.load(std::memory_order_relaxed);
    const bool gate = g_restoreLegacyEdgeSwipeGate.load(std::memory_order_relaxed);
    const bool mouseFix = g_restoreLegacyMouseDrag.load(std::memory_order_relaxed);
    const bool mouseGate =
        g_restoreLegacyTitlebarMouseInvocationGate.load(std::memory_order_relaxed);
    if (!trace && !gate && !mouseFix && !mouseGate) {
        return true;
    }

    HMODULE twinui = g_twinui ? g_twinui : GetModuleHandleW(L"twinui.dll");
    if (!twinui) {
        Wh_Log(L"[tablet-edge] twinui.dll isn't loaded; edge-input tracer not installed");
        return false;
    }

    ExactHook hooks[] = {
        {L"?IsTabletMode@TabletModeHelpers@@YA_NXZ",
         reinterpret_cast<void*>(&Twinui_TabletModeHelpers_IsTabletMode_EdgeScoped_Hook),
         reinterpret_cast<void**>(&Twinui_TabletModeHelpers_IsTabletMode_Original),
         L"twinui!TabletModeHelpers::IsTabletMode (edge-scoped)", false},
        {L"?_LayoutEdgeUiInputs@CEdgeUiManager@@AEAAJW4EDGEUI_LAYOUT_REASON@@@Z",
         reinterpret_cast<void*>(&CEdgeUiManager_LayoutEdgeUiInputs_Hook),
         reinterpret_cast<void**>(&CEdgeUiManager_LayoutEdgeUiInputs_Original),
         L"CEdgeUiManager::_LayoutEdgeUiInputs", false},
        {L"?PerformDelayedInitialization@CEdgeUiManager@@UEAAJXZ",
         reinterpret_cast<void*>(&CEdgeUiManager_PerformDelayedInitialization_Hook),
         reinterpret_cast<void**>(&CEdgeUiManager_PerformDelayedInitialization_Original),
         L"CEdgeUiManager::PerformDelayedInitialization", false},
        {L"?InputObserveStart@CEdgeUiManager@@UEAAJPEAUIEdgeUiInput@@@Z",
         reinterpret_cast<void*>(&CEdgeUiManager_InputObserveStart_Hook),
         reinterpret_cast<void**>(&CEdgeUiManager_InputObserveStart_Original),
         L"CEdgeUiManager::InputObserveStart", false},
        {L"?SetVisible@CEdgeUiInput@@UEAAJ_N@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_SetVisible_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_SetVisible_Original),
         L"CEdgeUiInput::SetVisible", false},
        {L"?_RegisterRawInput@CEdgeUiInput@@AEAAX_N@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_RegisterRawInput_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_RegisterRawInput_Original),
         L"CEdgeUiInput::_RegisterRawInput", false},
        {L"?_IndexFromEdgeInput@CEdgeUiManager@@AEAA?AW4EDGEUI_INDEX@@PEAUIEdgeUiInput@@@Z",
         reinterpret_cast<void*>(&CEdgeUiManager_IndexFromEdgeInput_Hook),
         reinterpret_cast<void**>(&CEdgeUiManager_IndexFromEdgeInput_Original),
         L"CEdgeUiManager::_IndexFromEdgeInput (mouse-scoped)", false},
        {L"?MouseDragStart@CEdgeUiManager@@UEAAJPEAUIEdgeUiInput@@PEAPEAUIEdgeUiMouseInvocation@@@Z",
         reinterpret_cast<void*>(&CEdgeUiManager_MouseDragStart_Hook),
         reinterpret_cast<void**>(&CEdgeUiManager_MouseDragStart_Original),
         L"CEdgeUiManager::MouseDragStart", false},
        {L"?_AcquireMouseInvokerForEdge@CEdgeUiManager@@AEAAJW4EDGEUI_INDEX@@PEAPEAUIEdgeUiMouseInvocation@@@Z",
         reinterpret_cast<void*>(&CEdgeUiManager_AcquireMouseInvokerForEdge_Hook),
         reinterpret_cast<void**>(&CEdgeUiManager_AcquireMouseInvokerForEdge_Original),
         L"CEdgeUiManager::_AcquireMouseInvokerForEdge", false},
        {L"?ObservedMouseButtonDown@CEdgeUiInput@@UEAAJUtagPOINT@@W4RAW_INPUT_MOUSE_BUTTON@@@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_ObservedMouseButtonDown_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_ObservedMouseButtonDown_Original),
         L"CEdgeUiInput::ObservedMouseButtonDown", false},
        {L"?ObservedMouseMove@CEdgeUiInput@@UEAAJUtagPOINT@@G0@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_ObservedMouseMove_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_ObservedMouseMove_Original),
         L"CEdgeUiInput::ObservedMouseMove", false},
        {L"?ObservedMouseButtonDown@CEdgeInvoker@@UEAAJUtagPOINT@@@Z",
         reinterpret_cast<void*>(&CEdgeInvoker_ObservedMouseButtonDown_Hook),
         reinterpret_cast<void**>(&CEdgeInvoker_ObservedMouseButtonDown_Original),
         L"CEdgeInvoker::ObservedMouseButtonDown", false},
        {L"?ObservedMouseMove@CEdgeInvoker@@UEAAJUtagPOINT@@0@Z",
         reinterpret_cast<void*>(&CEdgeInvoker_ObservedMouseMove_Hook),
         reinterpret_cast<void**>(&CEdgeInvoker_ObservedMouseMove_Original),
         L"CEdgeInvoker::ObservedMouseMove", false},
        {L"?StartDrag@CEdgeInvoker@@UEAAJUtagPOINT@@@Z",
         reinterpret_cast<void*>(&CEdgeInvoker_StartDrag_Hook),
         reinterpret_cast<void**>(&CEdgeInvoker_StartDrag_Original),
         L"CEdgeInvoker::StartDrag", false},
        {L"?v_ObservedMouseButtonDown@CTitleBarInvoker@@MEAAXUtagPOINT@@@Z",
         reinterpret_cast<void*>(&CTitleBarInvoker_v_ObservedMouseButtonDown_Hook),
         reinterpret_cast<void**>(&CTitleBarInvoker_v_ObservedMouseButtonDown_Original),
         L"CTitleBarInvoker::v_ObservedMouseButtonDown", false},
        {L"?v_ObservedMouseMove@CTitleBarInvoker@@MEAAXUtagPOINT@@@Z",
         reinterpret_cast<void*>(&CTitleBarInvoker_v_ObservedMouseMove_Hook),
         reinterpret_cast<void**>(&CTitleBarInvoker_v_ObservedMouseMove_Original),
         L"CTitleBarInvoker::v_ObservedMouseMove", false},
        {L"?v_StartDrag@CTitleBarInvoker@@MEAAJUtagPOINT@@@Z",
         reinterpret_cast<void*>(&CTitleBarInvoker_v_StartDrag_Hook),
         reinterpret_cast<void**>(&CTitleBarInvoker_v_StartDrag_Original),
         L"CTitleBarInvoker::v_StartDrag", false},
        {L"?v_ContinueDrag@CTitleBarInvoker@@MEAAJUtagPOINT@@@Z",
         reinterpret_cast<void*>(&CTitleBarInvoker_v_ContinueDrag_Hook),
         reinterpret_cast<void**>(&CTitleBarInvoker_v_ContinueDrag_Original),
         L"CTitleBarInvoker::v_ContinueDrag", false},
        {L"?v_CommitDrag@CTitleBarInvoker@@MEAAJUtagPOINT@@@Z",
         reinterpret_cast<void*>(&CTitleBarInvoker_v_CommitDrag_Hook),
         reinterpret_cast<void**>(&CTitleBarInvoker_v_CommitDrag_Original),
         L"CTitleBarInvoker::v_CommitDrag", false},
        {L"?v_CancelDrag@CTitleBarInvoker@@MEAAJXZ",
         reinterpret_cast<void*>(&CTitleBarInvoker_v_CancelDrag_Hook),
         reinterpret_cast<void**>(&CTitleBarInvoker_v_CancelDrag_Original),
         L"CTitleBarInvoker::v_CancelDrag", false},
        {L"?v_ObservedMouseButtonDown@CTaskbarInvoker@@MEAAXUtagPOINT@@@Z",
         reinterpret_cast<void*>(&CTaskbarInvoker_v_ObservedMouseButtonDown_Hook),
         reinterpret_cast<void**>(&CTaskbarInvoker_v_ObservedMouseButtonDown_Original),
         L"CTaskbarInvoker::v_ObservedMouseButtonDown", false},
        {L"?v_ObservedMouseMove@CTaskbarInvoker@@MEAAXUtagPOINT@@@Z",
         reinterpret_cast<void*>(&CTaskbarInvoker_v_ObservedMouseMove_Hook),
         reinterpret_cast<void**>(&CTaskbarInvoker_v_ObservedMouseMove_Original),
         L"CTaskbarInvoker::v_ObservedMouseMove", false},
        {L"?_HandleObservedMouseInput@CEdgeUiInput@@AEAAX_NUtagPOINT@@1@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_HandleObservedMouseInput_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_HandleObservedMouseInput_Original),
         L"CEdgeUiInput::_HandleObservedMouseInput", false},
        {L"?_WndProc@CEdgeUiInput@@AEAA_JI_K_J_N@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_WndProc_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_WndProc_Original),
         L"CEdgeUiInput::_WndProc (mouse trace)", false},
        {L"?_IsTopEdge@CEdgeUiInput@@AEAA_NXZ",
         reinterpret_cast<void*>(&CEdgeUiInput_IsTopEdge_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_IsTopEdge_Original),
         L"CEdgeUiInput::_IsTopEdge (mouse-scoped)", false},
        {L"?_IsCurrentInputFromMouseOrPen@CEdgeUiInput@@AEAAHXZ",
         reinterpret_cast<void*>(&CEdgeUiInput_IsCurrentInputFromMouseOrPen_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_IsCurrentInputFromMouseOrPen_Original),
         L"CEdgeUiInput::_IsCurrentInputFromMouseOrPen", false},
        {L"?_OnMouseMove@CEdgeUiInput@@AEAAXUtagPOINT@@_N1@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_OnMouseMove_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_OnMouseMove_Original),
         L"CEdgeUiInput::_OnMouseMove", false},
        {L"?_MouseMoveToCornerOrEdge@CEdgeUiInput@@AEAAJW4EDGEUI_CORNEROREDGE@@UtagPOINT@@_N22@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_MouseMoveToCornerOrEdge_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_MouseMoveToCornerOrEdge_Original),
         L"CEdgeUiInput::_MouseMoveToCornerOrEdge", false},
        {L"?MouseHitCornerOrEdge@CEdgeUiManager@@UEAAJPEAUIEdgeUiInput@@_N1UtagPOINT@@PEA_NPEAPEAUIEdgeUiMouseInvocation@@@Z",
         reinterpret_cast<void*>(&CEdgeUiManager_MouseHitCornerOrEdge_Hook),
         reinterpret_cast<void**>(&CEdgeUiManager_MouseHitCornerOrEdge_Original),
         L"CEdgeUiManager::MouseHitCornerOrEdge", false},
        {L"?HitCornerOrEdge@CEdgeInvoker@@UEAAJW4EDGEUI_CORNEROREDGE@@UtagPOINT@@PEAW4EDGEUI_INPUTNOTIFICATION@@H@Z",
         reinterpret_cast<void*>(&CEdgeInvoker_HitCornerOrEdge_Hook),
         reinterpret_cast<void**>(&CEdgeInvoker_HitCornerOrEdge_Original),
         L"CEdgeInvoker::HitCornerOrEdge", false},
        {L"?v_Invoke@CTitleBarInvoker@@MEAAJ_NUtagPOINT@@W4RAW_INPUT_TYPE@@@Z",
         reinterpret_cast<void*>(&CTitleBarInvoker_v_Invoke_Hook),
         reinterpret_cast<void**>(&CTitleBarInvoker_v_Invoke_Original),
         L"CTitleBarInvoker::v_Invoke", false},
        {L"?_OnMouseDown@CEdgeUiInput@@AEAAXUtagPOINT@@_N@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_OnMouseDown_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_OnMouseDown_Original),
         L"CEdgeUiInput::_OnMouseDown", false},
        {L"?_OnMouseUp@CEdgeUiInput@@AEAAXUtagPOINT@@@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_OnMouseUp_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_OnMouseUp_Original),
         L"CEdgeUiInput::_OnMouseUp", false},
        {L"?_OnPointerDown@CEdgeUiInput@@AEAAXIUtagPOINT@@_N@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_OnPointerDown_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_OnPointerDown_Original),
         L"CEdgeUiInput::_OnPointerDown", false},
        {L"?_OnPointerUpdate@CEdgeUiInput@@AEAAXIUtagPOINT@@@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_OnPointerUpdate_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_OnPointerUpdate_Original),
         L"CEdgeUiInput::_OnPointerUpdate", false},
        {L"?_OnPointerUp@CEdgeUiInput@@AEAAXIUtagPOINT@@@Z",
         reinterpret_cast<void*>(&CEdgeUiInput_OnPointerUp_Hook),
         reinterpret_cast<void**>(&CEdgeUiInput_OnPointerUp_Original),
         L"CEdgeUiInput::_OnPointerUp", false},
    };

    const size_t installed =
        InstallExactHooks(twinui, hooks, ARRAYSIZE(hooks), L"twinui.dll");

    g_disableTitlebarInvocationFlag = reinterpret_cast<volatile bool*>(
        FindPrivateSymbolAddress(twinui, L"?s_fDisableTitlebarInvocation@CEdgeUiInput@@0_NA"));
    g_disableTopLeftFlag = reinterpret_cast<volatile bool*>(
        FindPrivateSymbolAddress(twinui, L"?s_fDisableTopLeft@CEdgeUiInput@@0_NA"));
    g_disableTopRightFlag = reinterpret_cast<volatile bool*>(
        FindPrivateSymbolAddress(twinui, L"?s_fDisableTopRight@CEdgeUiInput@@0_NA"));
    CEdgeUiInput_GetAssignedEdge =
        reinterpret_cast<EdgeUiGetAssignedEdge_t>(
            FindPrivateSymbolAddress(
                twinui,
                L"?GetAssignedEdge@CEdgeUiInput@@UEAAJPEAW4EDGEUI_INDEX@@@Z"));

    if (g_restoreLegacyTitlebarMouseInvocationGate.load(
            std::memory_order_relaxed)) {
        EnsureTabletWnfQueryHook();
    }

    Wh_Log(L"[tablet-mouse-gate] resolved _IsTopEdge=%p GetAssignedEdge=%p disableTitlebar=%p value=%d disableTopLeft=%p value=%d disableTopRight=%p value=%d",
           reinterpret_cast<void*>(CEdgeUiInput_IsTopEdge_Original),
           reinterpret_cast<void*>(CEdgeUiInput_GetAssignedEdge),
           const_cast<bool*>(g_disableTitlebarInvocationFlag),
           g_disableTitlebarInvocationFlag ? (*g_disableTitlebarInvocationFlag ? 1 : 0) : -1,
           const_cast<bool*>(g_disableTopLeftFlag),
           g_disableTopLeftFlag ? (*g_disableTopLeftFlag ? 1 : 0) : -1,
           const_cast<bool*>(g_disableTopRightFlag),
           g_disableTopRightFlag ? (*g_disableTopRightFlag ? 1 : 0) : -1);

    ApplyLegacyTitlebarMouseInvocationGate(
        g_compatActive.load(std::memory_order_acquire));

    Wh_Log(L"[tablet-edge] installed %llu/%llu twinui edge/mouse-route/Win32-message/derived-invoker hooks; scopedEdgeGate=%d mouseIndexFix=%d titlebarMouseGate=%d (pcshell/snap gates untouched)",
           static_cast<unsigned long long>(installed),
           static_cast<unsigned long long>(ARRAYSIZE(hooks)),
           gate ? 1 : 0, mouseFix ? 1 : 0, mouseGate ? 1 : 0);
    return installed != 0;
}

bool InstallStartAppSwitchCompatibilityHooks() {
    if (!g_restoreStartAppSwitchBehavior.load(std::memory_order_relaxed)) {
        Wh_Log(L"[tablet-start-switch] compatibility disabled by setting");
        return true;
    }

    HMODULE serviceProvider =
        GetModuleHandleW(L"windows.immersiveshell.serviceprovider.dll");
    if (!serviceProvider) {
        serviceProvider =
            LoadLibraryW(L"windows.immersiveshell.serviceprovider.dll");
    }
    if (!serviceProvider) {
        Wh_Log(L"[tablet-start-switch] couldn't load windows.immersiveshell.serviceprovider.dll: %lu",
               GetLastError());
        return false;
    }

    ExactHook trySwitchHook[] = {
        {L"?TrySwitchToAppIfTabletMode@ApplicationTileActivationBroker@@CA_NPEBG@Z",
         reinterpret_cast<void*>(&TrySwitchToAppIfTabletMode_Hook),
         reinterpret_cast<void**>(&TrySwitchToAppIfTabletMode_Original),
         L"ApplicationTileActivationBroker::TrySwitchToAppIfTabletMode",
         false},
    };

    const size_t found = InstallExactHooks(
        serviceProvider, trySwitchHook, ARRAYSIZE(trySwitchHook),
        L"windows.immersiveshell.serviceprovider.dll");
    if (found != 1) {
        Wh_Log(L"[tablet-start-switch] TrySwitchToAppIfTabletMode symbol/hook unavailable; compatibility disabled");
        return false;
    }

    if (!EnsureTabletWnfQueryHook()) {
        Wh_Log(L"[tablet-start-switch] shared WNF query hook unavailable");
        return false;
    }

    if (!Wh_ApplyHookOperations()) {
        Wh_Log(L"[tablet-start-switch] Wh_ApplyHookOperations failed");
        return false;
    }

    DWORD raw = 0;
    ULONG stamp = 0;
    const bool haveWnf = QueryTabletWnfRaw(&raw, &stamp);
    Wh_Log(L"[tablet-start-switch] hooks installed; shared WNF hook active; global WNF unchanged; current WNFok=%d raw=%lu stamp=%lu",
           haveWnf ? 1 : 0, raw, stamp);
    return true;
}

bool InstallExplorerCompatibilityHooks() {
    HMODULE explorer = GetModuleHandleW(nullptr);
    ExactHook hooks[] = {
        {L"?IsTabletModeEnabled@CTray@@UEBA_NXZ",
         reinterpret_cast<void*>(&CTray_IsTabletModeEnabled_Hook),
         reinterpret_cast<void**>(&CTray_IsTabletModeEnabled_Original),
         L"CTray::IsTabletModeEnabled", false},
        {L"?IsModeImmersive@CTray@@UEBA_NXZ",
         reinterpret_cast<void*>(&CTray_IsModeImmersive_Hook),
         reinterpret_cast<void**>(&CTray_IsModeImmersive_Original),
         L"CTray::IsModeImmersive", false},
    };

    size_t installed =
        InstallExactHooks(explorer, hooks, ARRAYSIZE(hooks), L"explorer.exe");
    Wh_Log(L"[tablet-compat] installed %llu/2 Explorer compatibility hooks",
           static_cast<unsigned long long>(installed));
    return installed != 0;
}

template <typename T>
bool QueueEPExportHook(HMODULE module, PCSTR exportName, void* hook,
                       T* original, PCWSTR shortName) {
    FARPROC proc = GetProcAddress(module, exportName);
    if (!proc) {
        Wh_Log(L"[tablet-compat] ExplorerPatcher export not found: %s",
               shortName);
        return false;
    }

    if (!Wh_SetFunctionHook(reinterpret_cast<void*>(proc), hook,
                            reinterpret_cast<void**>(original))) {
        Wh_Log(L"[tablet-compat] ExplorerPatcher hook failed: %s", shortName);
        return false;
    }

    Wh_Log(L"[tablet-compat] queued ExplorerPatcher hook %s at %p",
           shortName, reinterpret_cast<void*>(proc));
    return true;
}

bool InstallExplorerPatcherHooks(HMODULE ep) {
    size_t installed = 0;
    size_t expected = 2;

    installed += QueueEPExportHook(
        ep, "?IsTabletModeEnabled@TrayUI@@UEBA_NXZ",
        reinterpret_cast<void*>(&EP_TrayUI_IsTabletModeEnabled_Hook),
        &EP_TrayUI_IsTabletModeEnabled_Original,
        L"TrayUI::IsTabletModeEnabled");
    installed += QueueEPExportHook(
        ep, "?IsTabletModeEnabled@CTaskBand@@UEAA_NXZ",
        reinterpret_cast<void*>(&EP_CTaskBand_IsTabletModeEnabled_Hook),
        &EP_CTaskBand_IsTabletModeEnabled_Original,
        L"CTaskBand::IsTabletModeEnabled");

    if (EpTaskbarImageMatchesTouchLayout(ep)) {
        ++expected;
        installed += QueueEPExportHook(
            ep,
            "?_ComputeSingleButtonWidth@CTaskListWnd@@IEAAHW4eTBGROUPTYPE@@PEAUITaskBtnGroup@@PEAH@Z",
            reinterpret_cast<void*>(&EP_ComputeSingleButtonWidth_Hook),
            &EP_ComputeSingleButtonWidth_Original,
            L"CTaskListWnd::_ComputeSingleButtonWidth");
    }

    if (!installed) return false;

    if (!Wh_ApplyHookOperations()) {
        Wh_Log(L"[tablet-compat] Wh_ApplyHookOperations failed for ExplorerPatcher hooks");
        return false;
    }

    Wh_Log(L"[tablet-compat] installed %llu/%llu ExplorerPatcher compatibility hooks",
           static_cast<unsigned long long>(installed),
           static_cast<unsigned long long>(expected));
    return true;
}

DWORD WINAPI ExplorerPatcherThreadProc(void*) {
    while (!g_afterInit.load(std::memory_order_acquire)) {
        if (WaitForSingleObject(g_stopEvent, 20) != WAIT_TIMEOUT) return 0;
    }

    bool startSwitchHooksAttempted = false;
    bool epHooksAttempted = false;
    ULONGLONG deadline = GetTickCount64() + g_epWaitMs.load();

    while (!g_unloading.load(std::memory_order_relaxed)) {
        if (WaitForSingleObject(g_stopEvent, 0) != WAIT_TIMEOUT) return 0;

        if (!CurrentProcessOwnsShell(false)) {
            if (GetShellWindow()) return 0;
        } else {
            // Serialize all post-init hook application in this one worker.
            // This avoids racing Wh_ApplyHookOperations across Start-switch,
            // ExplorerPatcher, and delayed twinui.pcshell hook installation.
            if (!startSwitchHooksAttempted) {
                startSwitchHooksAttempted = true;
                InstallStartAppSwitchCompatibilityHooks();
            }

            if (NeedDeferredPcshellHooks()) {
                InstallDeferredPcshellHooksIfReady();
            }

            if (!epHooksAttempted) {
                HMODULE ep = GetModuleHandleW(L"ep_taskbar.ge.dll");
                if (ep) {
                    epHooksAttempted = true;
                    Wh_Log(L"[tablet-compat] ep_taskbar.ge.dll appeared at %p",
                           ep);
                    InstallExplorerPatcherHooks(ep);
                }
            }

            if (epHooksAttempted && !NeedDeferredPcshellHooks()) {
                return 0;
            }
        }

        if (GetTickCount64() >= deadline) {
            if (!startSwitchHooksAttempted) {
                Wh_Log(L"[tablet-start-switch] shell never became ready before compatibility timeout");
            }
            if (!epHooksAttempted) {
                Wh_Log(L"[tablet-compat] ExplorerPatcher DLL not found before timeout; continuing without EP hooks");
            }
            if (NeedDeferredPcshellHooks()) {
                Wh_Log(L"[tablet-pcshell] twinui.pcshell.dll/deferred hooks not ready before timeout; continuing without the missing pcshell hooks");
            }
            return 0;
        }

        if (WaitForSingleObject(g_stopEvent, 50) != WAIT_TIMEOUT) return 0;
    }

    return 0;
}

}  // namespace

BOOL Wh_ModInit() {
    Wh_Log(L"Loading merged Explorer Tablet Mode subsystem version %s",
           WH_MOD_VERSION);
    LoadSettings();

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        Wh_Log(L"[tablet] CreateEvent failed: %lu", GetLastError());
        return FALSE;
    }

    InstallExplorerCompatibilityHooks();
    InstallTabletWindowGestureTraceHooks();

    g_controllerBuildSupported = PrepareAndVerifyTwinui();
    InstallTwinuiEdgeInputTraceHooks();
    InstallTitleBarStartDragServiceTraceHook();
    InstallTabletTitlebarOverlayHooks();
    InstallShellWindowManagementProfileHook();

    if (g_traceDesktopDragIdentity.load(std::memory_order_relaxed)) {
        g_dragIdentityThread =
            CreateThread(nullptr, 0, DesktopDragIdentityThreadProc, nullptr, 0, nullptr);
        if (!g_dragIdentityThread) {
            Wh_Log(L"[tablet-dragid] CreateThread failed: %lu", GetLastError());
        }
    } else {
        Wh_Log(L"[tablet-dragid] desktop drag identity observer disabled by setting");
    }

    if (g_restoreModernPerWindowMoveSizeIntercept.load(std::memory_order_relaxed)) {
        g_perWindowInterceptThread = CreateThread(
            nullptr, 0, PerWindowMoveSizeInterceptThreadProc, nullptr, 0, nullptr);
        if (!g_perWindowInterceptThread) {
            Wh_Log(L"[tablet-window-intercept] CreateThread failed: %lu", GetLastError());
        }
    } else {
        Wh_Log(L"[tablet-window-intercept] modern per-window interception experiment disabled by setting");
    }

    Wh_Log(L"[tablet] public system metrics: SM_TABLETPC=%d SM_CONVERTIBLESLATEMODE=%d SM_SYSTEMDOCKED=%d",
           GetSystemMetrics(SM_TABLETPC),
           GetSystemMetrics(SM_CONVERTIBLESLATEMODE),
           GetSystemMetrics(SM_SYSTEMDOCKED));

    if (g_enableRealTabletMode.load() && g_controllerBuildSupported) {
        g_controllerThread =
            CreateThread(nullptr, 0, ControllerThreadProc, nullptr, 0, nullptr);
        if (!g_controllerThread) {
            Wh_Log(L"[tablet] CreateThread(controller) failed: %lu",
                   GetLastError());
        }
    } else if (g_enableRealTabletMode.load()) {
        Wh_Log(L"[tablet] real controller transition requested but this twinui.dll build is unsupported");
    }

    g_epThread =
        CreateThread(nullptr, 0, ExplorerPatcherThreadProc, nullptr, 0, nullptr);
    if (!g_epThread) {
        Wh_Log(L"[tablet-compat] CreateThread(ExplorerPatcher) failed: %lu",
               GetLastError());
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    g_afterInit.store(true, std::memory_order_release);
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    LoadSettings();
    *bReload = TRUE;
    return TRUE;
}

void Wh_ModBeforeUninit() {
    SetCompatActive(false, L"mod unloading");
    g_unloading = true;

    if (g_stopEvent) SetEvent(g_stopEvent);

    if (g_dragIdentityThread) {
        WaitForSingleObject(g_dragIdentityThread, 2000);
        CloseHandle(g_dragIdentityThread);
        g_dragIdentityThread = nullptr;
    }

    if (g_perWindowInterceptThread) {
        WaitForSingleObject(g_perWindowInterceptThread, 3000);
        CloseHandle(g_perWindowInterceptThread);
        g_perWindowInterceptThread = nullptr;
    }

    if (g_controllerThread) {
        WaitForSingleObject(g_controllerThread, 5000);
        CloseHandle(g_controllerThread);
        g_controllerThread = nullptr;
    }

    if (g_epThread) {
        WaitForSingleObject(g_epThread, 5000);
        CloseHandle(g_epThread);
        g_epThread = nullptr;
    }

    if (g_restoreOnUnload.load() &&
        (g_changedMode.load() || g_leftAvailabilityForced.load())) {
        RestoreControllerState();
    }
}

void Wh_ModUninit() {
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }

    if (g_loadedTwinui && g_twinui) {
        FreeLibrary(g_twinui);
    }
    g_twinui = nullptr;
    g_loadedTwinui = false;

    Wh_Log(L"[tablet-window-intercept] totals apply=%lu remove=%lu queryFailures=%lu",
           g_perWindowInterceptApplyCalls.load(std::memory_order_relaxed),
           g_perWindowInterceptRemoveCalls.load(std::memory_order_relaxed),
           g_perWindowInterceptQueryFailures.load(std::memory_order_relaxed));

    Wh_Log(L"[tablet-gesture] totals init=%lu down=%lu update=%lu up=%lu start=%lu extended=%lu continue=%lu commit=%lu cancel=%lu resize=%lu moveSize=%lu mediatorMove=%lu tabletMgrMove=%lu shellChanged=%lu tabletChanged=%lu desktopChanged=%lu desktopChrome=%lu tabletChrome=%lu",
           g_gestureInputInitCalls.load(std::memory_order_relaxed),
           g_gesturePointerDownCalls.load(std::memory_order_relaxed),
           g_gesturePointerUpdateCalls.load(std::memory_order_relaxed),
           g_gesturePointerUpCalls.load(std::memory_order_relaxed),
           g_gestureStartSwipeCalls.load(std::memory_order_relaxed),
           g_gestureStartExtendedSwipeCalls.load(std::memory_order_relaxed),
           g_gestureContinueSwipeCalls.load(std::memory_order_relaxed),
           g_gestureCommitSwipeCalls.load(std::memory_order_relaxed),
           g_gestureCancelSwipeCalls.load(std::memory_order_relaxed),
           g_gestureShowAppResizeCalls.load(std::memory_order_relaxed),
           g_gestureMoveSizeAttemptedCalls.load(std::memory_order_relaxed),
           g_viewMediatorMoveCalls.load(std::memory_order_relaxed),
           g_tabletPositionerManagerMoveCalls.load(std::memory_order_relaxed),
           g_shellPositionerChangedCalls.load(std::memory_order_relaxed),
           g_tabletPositionerChangedCalls.load(std::memory_order_relaxed),
           g_desktopPositionerChangedCalls.load(std::memory_order_relaxed),
           g_desktopChromeCalls.load(std::memory_order_relaxed),
           g_tabletChromeCalls.load(std::memory_order_relaxed));
    Wh_Log(L"[tablet-snap] totals snapToLocation=%lu tabletHandle=%lu desktopHandle=%lu showSnapAssist=%lu createArrangementDrag=%lu",
           g_snapToLocationCalls.load(std::memory_order_relaxed),
           g_handleTabletSnappingCalls.load(std::memory_order_relaxed),
           g_handleDesktopSnappingCalls.load(std::memory_order_relaxed),
           g_showSnapAssistCalls.load(std::memory_order_relaxed),
           g_createArrangementDragCalls.load(std::memory_order_relaxed));
    Wh_Log(L"[tablet-select] totals swapViews=%lu changeView=%lu handoffView=%lu handoffArray=%lu handoffApp=%lu; [tablet-edge] layout=%lu delayedInit=%lu observeStart=%lu setVisible=%lu rawInput=%lu down=%lu update=%lu up=%lu forcedGate=%lu",
           g_swapPositionersForViewsCalls.load(std::memory_order_relaxed),
           g_changePositionerForViewCalls.load(std::memory_order_relaxed),
           g_positionerHandoffCalls.load(std::memory_order_relaxed),
           g_positionerHandoffArrayCalls.load(std::memory_order_relaxed),
           g_positionerHandoffAppCalls.load(std::memory_order_relaxed),
           g_edgeLayoutCalls.load(std::memory_order_relaxed),
           g_edgeDelayedInitCalls.load(std::memory_order_relaxed),
           g_edgeObserveStartCalls.load(std::memory_order_relaxed),
           g_edgeSetVisibleCalls.load(std::memory_order_relaxed),
           g_edgeRegisterRawInputCalls.load(std::memory_order_relaxed),
           g_edgePointerDownCalls.load(std::memory_order_relaxed),
           g_edgePointerUpdateCalls.load(std::memory_order_relaxed),
           g_edgePointerUpCalls.load(std::memory_order_relaxed),
           g_edgeTabletGateForcedCalls.load(std::memory_order_relaxed));
    Wh_Log(L"[tablet-mouse-fix] total top-edge caller-scoped edge-index remaps=%lu",
           g_mouseIndexRemapCalls.load(std::memory_order_relaxed));
    Wh_Log(L"[tablet-mouse-gate] total titlebar-disable-byte patches=%lu edge-2-WNF-overrides=%lu",
           g_mouseTitlebarGatePatches.load(std::memory_order_relaxed),
           g_edgeSetVisibleWnfOverrideCount.load(std::memory_order_relaxed));
    Wh_Log(L"[tablet-titlebar] total forced overlay queries=%lu",
           g_titlebarOverlayForcedCalls.load(std::memory_order_relaxed));
    Wh_Log(L"[tablet-start-switch] total scoped WNF overrides=%lu",
           g_startSwitchWnfOverrideCount.load(std::memory_order_relaxed));
    Wh_Log(L"[tablet-dragid] totals down=%lu up=%lu nativeMoveSize=%lu",
           g_dragIdentityDownCalls.load(std::memory_order_relaxed),
           g_dragIdentityUpCalls.load(std::memory_order_relaxed),
           g_dragIdentityMoveSizeCalls.load(std::memory_order_relaxed));
    Wh_Log(L"Unloading merged Explorer Tablet Mode subsystem");
}
}  // namespace ExplorerHost

namespace StartHost {
namespace {

#if defined(__clang__) && defined(__x86_64__)
#define WH_PRESERVE_ALL __attribute__((preserve_all))
#else
#define WH_PRESERVE_ALL
#endif

std::atomic<bool> g_forceStartPropertiesTabletMode{true};
std::atomic<bool> g_forceShellModeEventTabletMode{true};
std::atomic<bool> g_traceVerbPipeline{false};
std::atomic<bool> g_mapOpenNewWindowToTabletMode{true};
std::atomic<bool> g_mapOpenVerbFallbackToTabletMode{false};
std::atomic<bool> g_disableDirectLaunchForWin32Tiles{false};
std::atomic<bool> g_restoreTabletOpenNewWindowIcon{true};
std::atomic<bool> g_traceTileActivation{false};
std::atomic<bool> g_forceWin10InvocationSurface{true};
std::atomic<bool> g_forceProvideStartWindowIdOnTileActivation{false};
std::atomic<bool> g_focusExistingDesktopAppOnTileClick{false};
std::atomic<bool> g_verbose{false};
std::atomic<DWORD> g_waitTimeoutMs{30000};
std::atomic<bool> g_afterInit{false};
std::atomic<bool> g_unloading{false};

std::atomic<unsigned long> g_tabletCalls{0};
std::atomic<unsigned long> g_fullScreenCalls{0};
std::atomic<unsigned long> g_combinedCalls{0};
std::atomic<unsigned long> g_shellEventCalls{0};
std::atomic<unsigned long> g_setTabletCalls{0};
std::atomic<unsigned long> g_contextMenuCalls{0};
std::atomic<unsigned long> g_providerAggregationCalls{0};
std::atomic<unsigned long> g_createVerbCalls{0};
std::atomic<unsigned long> g_lookupMetadataCalls{0};
std::atomic<unsigned long> g_lookupMetadataByNameCalls{0};
std::atomic<unsigned long> g_producerCalls{0};
std::atomic<unsigned long> g_payloadGetCommandCalls{0};
std::atomic<unsigned long> g_payloadGetCommandsCalls{0};
std::atomic<unsigned long> g_getVerbTypeCalls{0};
std::atomic<unsigned long> g_payloadCtorCalls{0};
std::atomic<unsigned long> g_tileOnActivatedCalls{0};
std::atomic<unsigned long> g_tileDataActivateCalls{0};
std::atomic<unsigned long> g_tileAppStateCalls{0};
std::atomic<unsigned long> g_iconGlyphGetCalls{0};
std::atomic<unsigned long> g_iconGlyphSetCalls{0};
std::atomic<unsigned long> g_focusAttempts{0};
std::atomic<unsigned long> g_perFrameDataProvideWindowIdCalls{0};
std::atomic<unsigned long> g_directLaunchCalls{0};

thread_local int g_providerAggregationDepth = 0;
thread_local int g_contextMenuDepth = 0;
thread_local PCWSTR g_currentVerbProducer = L"<none>";

constexpr int kTabletModeOpenNewWindowCommandId = 0x1C;

HANDLE g_stopEvent = nullptr;
HANDLE g_workerThread = nullptr;

using BoolThis_t = bool(__cdecl*)(void*);
using SetBoolThis_t = void(__cdecl*)(void*, bool);
using ShowContextMenu_t = void(__cdecl*)(void*, unsigned int);
using OnVerbAggregation_t = void(__cdecl*)(void*, void*);
using VerbProducer_t = void(__cdecl*)(void*, void*);
using CreateVerbDelegateCommand_t = void*(__cdecl*)(void*, int, void*);
using LookupVerbMetadata_t = const void*(__cdecl*)(int, unsigned int*);
using LookupVerbMetadataByName_t =
    const void*(__cdecl*)(const wchar_t*, unsigned int*);
using PayloadGetVerbCommand_t = void*(__cdecl*)(void*, int);
using PayloadGetVerbCommands_t = void*(__cdecl*)(void*);
using VerbGetType_t = int(__cdecl*)(void*);
using PayloadCtor1_t = void*(__cdecl*)(void*, void*);
using PayloadCtor2_t = void*(__cdecl*)(void*, void*, int);
using TileOnActivated_t = void(__cdecl*)(void*, void*);
using TileDataActivate_t = void(__cdecl*)(void*, int, int, void*);
using TileAppState_t = int(__cdecl*)(void*);
using GetPtrThis_t = void*(__cdecl*)(void*);
using SetPtrThis_t = void(__cdecl*)(void*, void*);
using TileDataGetStringAbi_t = HRESULT(__cdecl*)(void*, void**);
using WindowsGetStringRawBuffer_t = PCWSTR(WINAPI*)(void*, UINT32*);
using WindowsCreateString_t = HRESULT(WINAPI*)(PCWSTR, UINT32, void**);
using WindowsDeleteString_t = HRESULT(WINAPI*)(void*);
using DirectLaunchTryActivate_t = bool(__cdecl*)(void*, const void*);

BoolThis_t StartProperties_IsTabletMode_Original = nullptr;
BoolThis_t StartProperties_IsFullScreen_Original = nullptr;
BoolThis_t StartProperties_IsFullScreenOrTabletMode_Original = nullptr;
BoolThis_t ShellModeChangedEventArgs_IsTabletMode_Original = nullptr;
SetBoolThis_t StartProperties_SetIsTabletMode_Original = nullptr;
ShowContextMenu_t ContextMenuBehavior_ShowContextMenu_Original = nullptr;
OnVerbAggregation_t SystemListVerbProvider_OnVerbAggregation_Original = nullptr;
VerbProducer_t CommandingVerbProvider_OnVerbAggregationEvent_Original = nullptr;
VerbProducer_t TileViewModel_OnVerbAggregationEvent_Original = nullptr;
VerbProducer_t AllAppsExpandingGroupViewModel_OnVerbAggregationEvent_Original = nullptr;
VerbProducer_t TileGroupViewModel_OnVerbAggregationEvent_Original = nullptr;
VerbProducer_t TileFolderRegionViewModel_OnVerbAggregationEvent_Original = nullptr;
VerbProducer_t PlacesUtmViewModel_OnVerbAggregationEvent_Original = nullptr;
CreateVerbDelegateCommand_t CreateVerbDelegateCommand_Original = nullptr;
LookupVerbMetadata_t LookupVerbMetadata_Original = nullptr;
LookupVerbMetadataByName_t LookupVerbMetadataByName_Original = nullptr;
PayloadGetVerbCommand_t VerbAggregationEventPayload_GetVerbCommand_Original = nullptr;
PayloadGetVerbCommands_t VerbAggregationEventPayload_GetVerbCommands_Original = nullptr;
VerbGetType_t VerbDelegateCommand_GetVerbType_Original = nullptr;
PayloadCtor1_t VerbDelegateCommandPayload_Ctor1_Original = nullptr;
PayloadCtor2_t VerbDelegateCommandPayload_Ctor2_Original = nullptr;
TileOnActivated_t TileViewModel_OnActivated_Original = nullptr;
TileDataActivate_t TileData_Activate_Original = nullptr;
TileAppState_t TileData_GetAppState_Original = nullptr;
GetPtrThis_t VerbDelegateCommand_GetIconGlyph_Original = nullptr;
SetPtrThis_t VerbDelegateCommand_SetIconGlyph_Original = nullptr;
BoolThis_t PerFrameData_ProvideStartWindowId_Original = nullptr;
TileDataGetStringAbi_t TileData_GetDesktopAppExeName_Abi = nullptr;
WindowsGetStringRawBuffer_t WindowsGetStringRawBuffer_Dynamic = nullptr;
WindowsCreateString_t WindowsCreateString_Dynamic = nullptr;
WindowsDeleteString_t WindowsDeleteString_Dynamic = nullptr;
DirectLaunchTryActivate_t DirectLaunchTryActivate_Original = nullptr;

void LoadSettings() {
    g_forceStartPropertiesTabletMode =
        Wh_GetIntSetting(L"forceStartPropertiesTabletMode") != 0;
    g_forceShellModeEventTabletMode =
        Wh_GetIntSetting(L"forceShellModeEventTabletMode") != 0;
    g_traceVerbPipeline = Wh_GetIntSetting(L"traceVerbPipeline") != 0;
    g_mapOpenNewWindowToTabletMode =
        Wh_GetIntSetting(L"mapOpenNewWindowToTabletMode") != 0;
    g_mapOpenVerbFallbackToTabletMode =
        Wh_GetIntSetting(L"mapOpenVerbFallbackToTabletMode") != 0;
    g_disableDirectLaunchForWin32Tiles =
        Wh_GetIntSetting(L"disableDirectLaunchForWin32Tiles") != 0;
    g_restoreTabletOpenNewWindowIcon =
        Wh_GetIntSetting(L"restoreTabletOpenNewWindowIcon") != 0;
    g_traceTileActivation =
        Wh_GetIntSetting(L"traceTileActivation") != 0;
    g_forceWin10InvocationSurface =
        Wh_GetIntSetting(L"forceWin10InvocationSurface") != 0;
    g_forceProvideStartWindowIdOnTileActivation =
        Wh_GetIntSetting(L"forceProvideStartWindowIdOnTileActivation") != 0;
    g_focusExistingDesktopAppOnTileClick =
        Wh_GetIntSetting(L"focusExistingDesktopAppOnTileClick") != 0;
    g_verbose = Wh_GetIntSetting(L"verbose") != 0;

    int timeout = Wh_GetIntSetting(L"waitTimeoutMs");
    if (timeout < 1000) timeout = 1000;
    if (timeout > 120000) timeout = 120000;
    g_waitTimeoutMs = static_cast<DWORD>(timeout);
}

void DescribeCaller(void* caller, wchar_t* moduleName, size_t moduleNameCount,
                    uintptr_t* offset) {
    if (!moduleName || !moduleNameCount || !offset) return;

    lstrcpynW(moduleName, L"<unknown>", static_cast<int>(moduleNameCount));
    *offset = 0;

    if (!caller) return;

    HMODULE callerModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<PCWSTR>(caller), &callerModule)) {
        return;
    }

    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(callerModule, path, ARRAYSIZE(path))) {
        const wchar_t* base = wcsrchr(path, L'\\');
        lstrcpynW(moduleName, base ? base + 1 : path,
                  static_cast<int>(moduleNameCount));
    }

    *offset = reinterpret_cast<uintptr_t>(caller) -
              reinterpret_cast<uintptr_t>(callerModule);
}

void LogBoolQuery(PCWSTR name, unsigned long callNo, bool realValue,
                  bool returnedValue, void* caller) {
    if (!g_verbose.load(std::memory_order_relaxed) || callNo > 80) return;

    wchar_t moduleName[MAX_PATH] = {};
    uintptr_t offset = 0;
    DescribeCaller(caller, moduleName, ARRAYSIZE(moduleName), &offset);

    Wh_Log(L"[start-tablet] %s call=%lu real=%d returned=%d caller=%s+0x%llX tid=%lu",
           name, callNo, realValue ? 1 : 0, returnedValue ? 1 : 0,
           moduleName, static_cast<unsigned long long>(offset),
           GetCurrentThreadId());
}

void ForceNativeStartPropertiesTabletByte(void* pThis) {
    if (!pThis ||
        !g_forceStartPropertiesTabletMode.load(std::memory_order_relaxed)) {
        return;
    }

    // Exact layout from this matching StartUI build:
    // +0x50 IsFullScreen, +0x51 IsTabletMode.
    auto* tabletByte =
        reinterpret_cast<volatile unsigned char*>(
            static_cast<unsigned char*>(pThis) + 0x51);
    *tabletByte = 1;
}

WH_PRESERVE_ALL bool StartProperties_IsTabletMode_Hook(void* pThis) {
    ForceNativeStartPropertiesTabletByte(pThis);
    const bool realValue = StartProperties_IsTabletMode_Original(pThis);
    const bool returnedValue =
        g_forceStartPropertiesTabletMode.load(std::memory_order_relaxed)
            ? true
            : realValue;

    const auto n =
        g_tabletCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogBoolQuery(L"StartProperties::IsTabletMode", n, realValue, returnedValue,
                 __builtin_return_address(0));
    return returnedValue;
}

WH_PRESERVE_ALL bool StartProperties_IsFullScreen_Hook(void* pThis) {
    const bool realValue = StartProperties_IsFullScreen_Original(pThis);
    const auto n =
        g_fullScreenCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogBoolQuery(L"StartProperties::IsFullScreen", n, realValue, realValue,
                 __builtin_return_address(0));
    return realValue;
}

WH_PRESERVE_ALL bool
StartProperties_IsFullScreenOrTabletMode_Hook(void* pThis) {
    ForceNativeStartPropertiesTabletByte(pThis);
    const bool realValue =
        StartProperties_IsFullScreenOrTabletMode_Original(pThis);

    // If we're explicitly forcing the Tablet Mode property, the semantic
    // aggregate must also be true even if a caller reaches this helper without
    // consulting IsTabletMode first.
    const bool returnedValue =
        g_forceStartPropertiesTabletMode.load(std::memory_order_relaxed)
            ? true
            : realValue;

    const auto n =
        g_combinedCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogBoolQuery(L"StartProperties::IsFullScreenOrTabletMode", n, realValue,
                 returnedValue, __builtin_return_address(0));
    return returnedValue;
}

bool __cdecl ShellModeChangedEventArgs_IsTabletMode_Hook(void* pThis) {
    const bool realValue =
        ShellModeChangedEventArgs_IsTabletMode_Original(pThis);
    const bool returnedValue =
        g_forceShellModeEventTabletMode.load(std::memory_order_relaxed)
            ? true
            : realValue;

    const auto n =
        g_shellEventCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    LogBoolQuery(L"ShellModeChangedEventArgs::IsTabletMode", n, realValue,
                 returnedValue, __builtin_return_address(0));
    return returnedValue;
}

WH_PRESERVE_ALL void
StartProperties_SetIsTabletMode_Hook(void* pThis, bool value) {
    const bool force =
        g_forceStartPropertiesTabletMode.load(std::memory_order_relaxed);
    const bool valueToStore = force ? true : value;

    const auto n =
        g_setTabletCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (g_verbose.load(std::memory_order_relaxed) && n <= 80) {
        wchar_t moduleName[MAX_PATH] = {};
        uintptr_t offset = 0;
        DescribeCaller(__builtin_return_address(0), moduleName,
                       ARRAYSIZE(moduleName), &offset);
        Wh_Log(L"[start-tablet] StartProperties::IsTabletMode SET call=%lu requested=%d stored=%d force=%d caller=%s+0x%llX tid=%lu",
               n, value ? 1 : 0, valueToStore ? 1 : 0, force ? 1 : 0,
               moduleName, static_cast<unsigned long long>(offset),
               GetCurrentThreadId());
    }

    // This is the important v0.1.2 change: force the real StartProperties
    // backing state, not only its getter. Some StartUI paths may read/copy the
    // property internally without going through the public getter hook.
    StartProperties_SetIsTabletMode_Original(pThis, valueToStore);
}

void __cdecl ContextMenuBehavior_ShowContextMenu_Hook(void* pThis,
                                                       unsigned int flags) {
    const auto n =
        g_contextMenuCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (g_verbose.load(std::memory_order_relaxed) && n <= 40) {
        Wh_Log(L"[start-tablet] ContextMenuBehavior::ShowContextMenu ENTER call=%lu flags=%u forceStartTablet=%d forceShellEvent=%d tid=%lu",
               n, flags,
               g_forceStartPropertiesTabletMode.load() ? 1 : 0,
               g_forceShellModeEventTabletMode.load() ? 1 : 0,
               GetCurrentThreadId());
    }

    ++g_contextMenuDepth;
    ContextMenuBehavior_ShowContextMenu_Original(pThis, flags);
    --g_contextMenuDepth;

    if (g_verbose.load(std::memory_order_relaxed) && n <= 40) {
        Wh_Log(L"[start-tablet] ContextMenuBehavior::ShowContextMenu EXIT call=%lu tid=%lu",
               n, GetCurrentThreadId());
    }
}


PCWSTR VerbCommandName(int commandId) {
    switch (commandId) {
        case 0x09: return L"Uninstall";
        case 0x0A: return L"RunAs";
        case 0x0B: return L"OpenFileLocation";
        case 0x0C: return L"RunAsUser";
        case 0x0D: return L"Manage";
        case 0x0E: return L"ItemProperties";
        case 0x12: return L"TaskbarPin";
        case 0x13: return L"TaskbarUnpin";
        case 0x14: return L"TurnLiveTileOff";
        case 0x15: return L"TurnLiveTileOn";
        case 0x16: return L"Review";
        case 0x17: return L"Share";
        case 0x18: return L"RemoveFromList";
        case 0x19: return L"BlockFromList";
        case 0x1A: return L"BlockFromSuggestedApps";
        case 0x1B: return L"TurnOffSuggestedApps";
        case 0x1C: return L"TabletMode_OpenNewWindow";
        case 0x1D: return L"ClearList";
        case 0x1E: return L"PersonalizeList";
        case 0x1F: return L"Settings";
        default: return L"<other>";
    }
}

bool ShouldLogVerb(int commandId) {
    if (!g_traceVerbPipeline.load(std::memory_order_relaxed)) return false;
    return commandId == kTabletModeOpenNewWindowCommandId ||
           g_providerAggregationDepth > 0 || g_contextMenuDepth > 0;
}

WH_PRESERVE_ALL void
SystemListVerbProvider_OnVerbAggregation_Hook(void* pThis, void* payload) {
    const auto n =
        g_providerAggregationCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (g_traceVerbPipeline.load(std::memory_order_relaxed)) {
        Wh_Log(L"[start-verb] SystemListVerbProvider::OnVerbAggregation ENTER call=%lu payload=%p tabletRealGetterForce=%d shellEventForce=%d tid=%lu",
               n, payload,
               g_forceStartPropertiesTabletMode.load() ? 1 : 0,
               g_forceShellModeEventTabletMode.load() ? 1 : 0,
               GetCurrentThreadId());
    }

    ++g_providerAggregationDepth;
    SystemListVerbProvider_OnVerbAggregation_Original(pThis, payload);
    --g_providerAggregationDepth;

    if (g_traceVerbPipeline.load(std::memory_order_relaxed)) {
        Wh_Log(L"[start-verb] SystemListVerbProvider::OnVerbAggregation EXIT call=%lu tid=%lu",
               n, GetCurrentThreadId());
    }
}


void RunVerbProducer(PCWSTR name, VerbProducer_t original,
                     void* pThis, void* payload) {
    const auto n =
        g_producerCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    PCWSTR previous = g_currentVerbProducer;
    g_currentVerbProducer = name;

    if (g_traceVerbPipeline.load(std::memory_order_relaxed)) {
        Wh_Log(L"[start-verb] producer ENTER #%lu %s this=%p payload=%p tid=%lu",
               n, name, pThis, payload, GetCurrentThreadId());
    }

    original(pThis, payload);

    if (g_traceVerbPipeline.load(std::memory_order_relaxed)) {
        Wh_Log(L"[start-verb] producer EXIT  #%lu %s tid=%lu",
               n, name, GetCurrentThreadId());
    }

    g_currentVerbProducer = previous;
}

WH_PRESERVE_ALL void
CommandingVerbProvider_OnVerbAggregationEvent_Hook(void* pThis, void* payload) {
    RunVerbProducer(L"CommandingVerbProvider", 
                    CommandingVerbProvider_OnVerbAggregationEvent_Original,
                    pThis, payload);
}

WH_PRESERVE_ALL void
TileViewModel_OnVerbAggregationEvent_Hook(void* pThis, void* payload) {
    RunVerbProducer(L"TileViewModel",
                    TileViewModel_OnVerbAggregationEvent_Original,
                    pThis, payload);
}

WH_PRESERVE_ALL void
AllAppsExpandingGroupViewModel_OnVerbAggregationEvent_Hook(
    void* pThis, void* payload) {
    RunVerbProducer(L"AllAppsExpandingGroupViewModel",
                    AllAppsExpandingGroupViewModel_OnVerbAggregationEvent_Original,
                    pThis, payload);
}

WH_PRESERVE_ALL void
TileGroupViewModel_OnVerbAggregationEvent_Hook(void* pThis, void* payload) {
    RunVerbProducer(L"TileGroupViewModel",
                    TileGroupViewModel_OnVerbAggregationEvent_Original,
                    pThis, payload);
}

WH_PRESERVE_ALL void
TileFolderRegionViewModel_OnVerbAggregationEvent_Hook(
    void* pThis, void* payload) {
    RunVerbProducer(L"TileFolderRegionViewModel",
                    TileFolderRegionViewModel_OnVerbAggregationEvent_Original,
                    pThis, payload);
}

WH_PRESERVE_ALL void
PlacesUtmViewModel_OnVerbAggregationEvent_Hook(void* pThis, void* payload) {
    RunVerbProducer(L"PlacesUtmViewModel",
                    PlacesUtmViewModel_OnVerbAggregationEvent_Original,
                    pThis, payload);
}

WH_PRESERVE_ALL void*
CreateVerbDelegateCommand_Hook(void* executeDelegate, int commandId,
                               void* resourceLoader) {
    const auto n =
        g_createVerbCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    const bool logThis =
        ShouldLogVerb(commandId) ||
        (g_traceVerbPipeline.load(std::memory_order_relaxed) && n <= 200);
    if (logThis) {
        Wh_Log(L"[start-verb] CreateVerbDelegateCommand ENTER call=%lu command=0x%X (%s) producer=%s providerDepth=%d contextMenuDepth=%d tid=%lu",
               n, commandId, VerbCommandName(commandId), g_currentVerbProducer,
               g_providerAggregationDepth, g_contextMenuDepth,
               GetCurrentThreadId());
    }

    void* result = CreateVerbDelegateCommand_Original(
        executeDelegate, commandId, resourceLoader);

    if (logThis) {
        Wh_Log(L"[start-verb] CreateVerbDelegateCommand EXIT call=%lu command=0x%X (%s) result=%p",
               n, commandId, VerbCommandName(commandId), result);
    }

    return result;
}

WH_PRESERVE_ALL const void*
LookupVerbMetadata_Hook(int commandId, unsigned int* index) {
    const auto n =
        g_lookupMetadataCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    const void* result = LookupVerbMetadata_Original(commandId, index);

    const bool logThis =
        ShouldLogVerb(commandId) ||
        (g_traceVerbPipeline.load(std::memory_order_relaxed) && n <= 300);
    if (logThis) {
        Wh_Log(L"[start-verb] LookupVerbMetadata call=%lu command=0x%X (%s) result=%p index=%u producer=%s providerDepth=%d contextMenuDepth=%d",
               n, commandId, VerbCommandName(commandId), result,
               index ? *index : 0xFFFFFFFFu, g_currentVerbProducer,
               g_providerAggregationDepth, g_contextMenuDepth);
    }

    return result;
}


WH_PRESERVE_ALL const void*
LookupVerbMetadataByName_Hook(const wchar_t* canonicalName,
                              unsigned int* index) {
    const auto n =
        g_lookupMetadataByNameCalls.fetch_add(
            1, std::memory_order_relaxed) + 1;

    const void* result =
        LookupVerbMetadataByName_Original(canonicalName, index);

    if (g_traceVerbPipeline.load(std::memory_order_relaxed) && n <= 300) {
        Wh_Log(L"[start-canonical] LookupVerbMetadata(name) call=%lu name=\"%s\" result=%p index=%u producer=%s tid=%lu",
               n,
               canonicalName ? canonicalName : L"<null>",
               result,
               index ? *index : 0xFFFFFFFFu,
               g_currentVerbProducer,
               GetCurrentThreadId());
    }

    if (canonicalName &&
        _wcsicmp(canonicalName, L"OpenNewWindow") == 0) {
        // The shell is supplying "OpenNewWindow", while this restored StartUI
        // binary contains metadata for "TabletMode_OpenNewWindow". Probe that
        // entry directly. The lookup is read-only; mapping is separately
        // controlled by a setting.
        unsigned int tabletIndex = 0xFFFFFFFFu;
        const void* tabletResult =
            LookupVerbMetadataByName_Original(
                L"TabletMode_OpenNewWindow", &tabletIndex);

        Wh_Log(L"[start-canonical] OpenNewWindow tablet candidate: normal=%p tablet=%p tabletIndex=%u mapEnabled=%d producer=%s",
               result, tabletResult, tabletIndex,
               g_mapOpenNewWindowToTabletMode.load(
                   std::memory_order_relaxed) ? 1 : 0,
               g_currentVerbProducer);

        if (!result && tabletResult &&
            g_mapOpenNewWindowToTabletMode.load(
                std::memory_order_relaxed)) {
            if (index) {
                *index = tabletIndex;
            }

            Wh_Log(L"[start-canonical] *** MAPPING OpenNewWindow -> TabletMode_OpenNewWindow metadata=%p index=%u ***",
                   tabletResult, tabletIndex);
            return tabletResult;
        }
    }

    if (canonicalName &&
        _wcsicmp(canonicalName, L"Open") == 0 &&
        !result &&
        g_forceStartPropertiesTabletMode.load(std::memory_order_relaxed) &&
        g_mapOpenVerbFallbackToTabletMode.load(std::memory_order_relaxed) &&
        g_currentVerbProducer &&
        wcscmp(g_currentVerbProducer, L"CommandingVerbProvider") == 0) {
        unsigned int tabletIndex = 0xFFFFFFFFu;
        const void* tabletResult =
            LookupVerbMetadataByName_Original(
                L"TabletMode_OpenNewWindow", &tabletIndex);

        Wh_Log(L"[start-canonical] Open fallback candidate: normal=%p tablet=%p tabletIndex=%u fallbackEnabled=1 producer=%s",
               result, tabletResult, tabletIndex, g_currentVerbProducer);

        if (tabletResult) {
            if (index) {
                *index = tabletIndex;
            }

            Wh_Log(L"[start-canonical] *** MAPPING Open -> TabletMode_OpenNewWindow metadata=%p index=%u ***",
                   tabletResult, tabletIndex);
            return tabletResult;
        }
    }

    if (canonicalName &&
        _wcsicmp(canonicalName, L"TabletMode_OpenNewWindow") == 0) {
        Wh_Log(L"[start-canonical] *** TabletMode_OpenNewWindow reached StartUI metadata lookup: result=%p index=%u producer=%s ***",
               result,
               index ? *index : 0xFFFFFFFFu,
               g_currentVerbProducer);
    }

    return result;
}


WH_PRESERVE_ALL void*
VerbAggregationEventPayload_GetVerbCommand_Hook(void* pThis, int commandId) {
    void* result =
        VerbAggregationEventPayload_GetVerbCommand_Original(pThis, commandId);

    const auto n =
        g_payloadGetCommandCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (g_traceVerbPipeline.load(std::memory_order_relaxed) &&
        (commandId == kTabletModeOpenNewWindowCommandId || n <= 200)) {
        Wh_Log(L"[start-payload] GetVerbCommand call=%lu payload=%p command=0x%X (%s) result=%p producer=%s tid=%lu",
               n, pThis, commandId, VerbCommandName(commandId), result,
               g_currentVerbProducer, GetCurrentThreadId());
    }

    return result;
}

WH_PRESERVE_ALL void*
VerbAggregationEventPayload_GetVerbCommands_Hook(void* pThis) {
    void* result =
        VerbAggregationEventPayload_GetVerbCommands_Original(pThis);

    const auto n =
        g_payloadGetCommandsCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (g_traceVerbPipeline.load(std::memory_order_relaxed) && n <= 120) {
        Wh_Log(L"[start-payload] get_VerbCommands call=%lu payload=%p vector=%p producer=%s providerDepth=%d contextMenuDepth=%d tid=%lu",
               n, pThis, result, g_currentVerbProducer,
               g_providerAggregationDepth, g_contextMenuDepth,
               GetCurrentThreadId());
    }

    return result;
}

WH_PRESERVE_ALL int
VerbDelegateCommand_GetVerbType_Hook(void* pThis) {
    const int commandId = VerbDelegateCommand_GetVerbType_Original(pThis);

    const auto n =
        g_getVerbTypeCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (g_traceVerbPipeline.load(std::memory_order_relaxed) &&
        (commandId == kTabletModeOpenNewWindowCommandId || n <= 250)) {
        Wh_Log(L"[start-payload] VerbDelegateCommand::VerbType call=%lu commandObj=%p command=0x%X (%s) producer=%s tid=%lu",
               n, pThis, commandId, VerbCommandName(commandId),
               g_currentVerbProducer, GetCurrentThreadId());
    }

    return commandId;
}

WH_PRESERVE_ALL void*
VerbDelegateCommandPayload_Ctor1_Hook(void* pThis, void* command) {
    void* result = VerbDelegateCommandPayload_Ctor1_Original(pThis, command);

    int commandId = -1;
    if (command && VerbDelegateCommand_GetVerbType_Original) {
        commandId = VerbDelegateCommand_GetVerbType_Original(command);
    }

    const auto n =
        g_payloadCtorCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (g_traceVerbPipeline.load(std::memory_order_relaxed) &&
        (commandId == kTabletModeOpenNewWindowCommandId || n <= 200)) {
        Wh_Log(L"[start-payload] VerbDelegateCommandPayload ctor(1) call=%lu payloadObj=%p commandObj=%p command=0x%X (%s) producer=%s tid=%lu",
               n, pThis, command, commandId, VerbCommandName(commandId),
               g_currentVerbProducer, GetCurrentThreadId());
    }

    return result;
}

WH_PRESERVE_ALL void*
VerbDelegateCommandPayload_Ctor2_Hook(void* pThis, void* command, int flags) {
    void* result =
        VerbDelegateCommandPayload_Ctor2_Original(pThis, command, flags);

    int commandId = -1;
    if (command && VerbDelegateCommand_GetVerbType_Original) {
        commandId = VerbDelegateCommand_GetVerbType_Original(command);
    }

    const auto n =
        g_payloadCtorCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (g_traceVerbPipeline.load(std::memory_order_relaxed) &&
        (commandId == kTabletModeOpenNewWindowCommandId || n <= 200)) {
        Wh_Log(L"[start-payload] VerbDelegateCommandPayload ctor(2) call=%lu payloadObj=%p commandObj=%p flags=0x%X command=0x%X (%s) producer=%s tid=%lu",
               n, pThis, command, flags, commandId, VerbCommandName(commandId),
               g_currentVerbProducer, GetCurrentThreadId());
    }

    return result;
}



void* FindExactSymbolAddress(HMODULE module, PCWSTR decoratedName) {
    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;

    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, &options, &symbol);
    if (!search) {
        return nullptr;
    }

    void* result = nullptr;
    do {
        PCWSTR decorated = symbol.symbolDecorated;
        if (!decorated || !*decorated) decorated = symbol.symbol;
        if (decorated && wcscmp(decorated, decoratedName) == 0) {
            result = symbol.address;
            break;
        }
    } while (Wh_FindNextSymbol(search, &symbol));

    Wh_FindCloseSymbol(search);
    return result;
}

PCWSTR PathBaseName(PCWSTR path) {
    if (!path) return L"";
    PCWSTR slash = wcsrchr(path, L'\\');
    PCWSTR slash2 = wcsrchr(path, L'/');
    if (!slash || (slash2 && slash2 > slash)) slash = slash2;
    return slash ? slash + 1 : path;
}

bool GetTileDesktopAppExeName(void* tileData, std::wstring* value) {
    value->clear();

    if (!TileData_GetDesktopAppExeName_Abi ||
        !WindowsGetStringRawBuffer_Dynamic ||
        !WindowsDeleteString_Dynamic) {
        return false;
    }

    void* hstring = nullptr;
    HRESULT hr = TileData_GetDesktopAppExeName_Abi(tileData, &hstring);
    if (FAILED(hr) || !hstring) {
        return false;
    }

    UINT32 length = 0;
    PCWSTR raw = WindowsGetStringRawBuffer_Dynamic(hstring, &length);
    if (raw && length) {
        value->assign(raw, length);
    }

    WindowsDeleteString_Dynamic(hstring);
    return !value->empty();
}

struct ExistingDesktopWindowSearch {
    PCWSTR exeName;
    HWND hwnd;
    DWORD pid;
};

BOOL CALLBACK FindExistingDesktopWindowProc(HWND hwnd, LPARAM lParam) {
    auto* search =
        reinterpret_cast<ExistingDesktopWindowSearch*>(lParam);

    if (!IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) {
        return TRUE;
    }

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == GetCurrentProcessId()) {
        return TRUE;
    }

    HANDLE process =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return TRUE;
    }

    wchar_t imagePath[32768] = {};
    DWORD imagePathLength = ARRAYSIZE(imagePath);
    bool match = false;
    if (QueryFullProcessImageNameW(
            process, 0, imagePath, &imagePathLength)) {
        PCWSTR actualExe = PathBaseName(imagePath);
        PCWSTR requestedExe = PathBaseName(search->exeName);
        match = _wcsicmp(actualExe, requestedExe) == 0;
    }

    CloseHandle(process);

    if (!match) {
        return TRUE;
    }

    // Avoid selecting owned popup windows when an application's main
    // top-level window is also available.
    if (GetWindow(hwnd, GW_OWNER) != nullptr &&
        !(exStyle & WS_EX_APPWINDOW)) {
        return TRUE;
    }

    search->hwnd = hwnd;
    search->pid = pid;
    return FALSE;
}

bool FocusExistingDesktopAppWindow(
    PCWSTR exeName, HWND* focusedWindow, DWORD* focusedPid) {
    *focusedWindow = nullptr;
    *focusedPid = 0;

    ExistingDesktopWindowSearch search{
        exeName,
        nullptr,
        0,
    };

    EnumWindows(FindExistingDesktopWindowProc,
                reinterpret_cast<LPARAM>(&search));
    if (!search.hwnd) {
        return false;
    }

    if (IsIconic(search.hwnd)) {
        ShowWindowAsync(search.hwnd, SW_RESTORE);
    }

    BringWindowToTop(search.hwnd);
    SetForegroundWindow(search.hwnd);

    *focusedWindow = search.hwnd;
    *focusedPid = search.pid;
    return true;
}

void __cdecl TileViewModel_OnActivated_Hook(void* pThis, void* parameter) {
    const auto n =
        g_tileOnActivatedCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (g_traceTileActivation.load(std::memory_order_relaxed)) {
        Wh_Log(L"[start-activate] TileViewModel::OnActivated ENTER call=%lu this=%p parameter=%p tablet=%d tid=%lu",
               n, pThis, parameter,
               g_forceStartPropertiesTabletMode.load() ? 1 : 0,
               GetCurrentThreadId());
    }

    TileViewModel_OnActivated_Original(pThis, parameter);

    if (g_traceTileActivation.load(std::memory_order_relaxed)) {
        Wh_Log(L"[start-activate] TileViewModel::OnActivated EXIT call=%lu tid=%lu",
               n, GetCurrentThreadId());
    }
}

void __cdecl TileData_Activate_Hook(void* pThis, int activationState,
                                    int invocationSurface, void* coreWindow) {
    const auto n =
        g_tileDataActivateCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    int appState = -1;
    if (TileData_GetAppState_Original) {
        appState = TileData_GetAppState_Original(pThis);
    }

    int invocationSurfaceToUse = invocationSurface;
    const bool forceWin10Surface =
        g_forceWin10InvocationSurface.load(std::memory_order_relaxed) &&
        g_forceStartPropertiesTabletMode.load(std::memory_order_relaxed) &&
        activationState == 0 && invocationSurface == 1;

    if (forceWin10Surface) {
        invocationSurfaceToUse = 0;
    }

    if (g_traceTileActivation.load(std::memory_order_relaxed)) {
        Wh_Log(L"[start-activate] TileData::Activate ENTER call=%lu this=%p activationState=%d invocationSurface=%d useSurface=%d appState=%d coreWindow=%p tablet=%d forceWin10Surface=%d focusExisting=%d tid=%lu",
               n, pThis, activationState, invocationSurface,
               invocationSurfaceToUse, appState, coreWindow,
               g_forceStartPropertiesTabletMode.load() ? 1 : 0,
               forceWin10Surface ? 1 : 0,
               g_focusExistingDesktopAppOnTileClick.load() ? 1 : 0,
               GetCurrentThreadId());
    }

    if (forceWin10Surface) {
        Wh_Log(L"[start-activate] forcing InvocationSurface2 1 -> 0 to match genuine Windows 10 StartUI normal tile activation");
    }

    // Keep the older window-enumeration experiment separate. The explicit
    // "Open in new window" context-menu verb is a different execution path.
    if (g_focusExistingDesktopAppOnTileClick.load(
            std::memory_order_relaxed) &&
        g_forceStartPropertiesTabletMode.load(
            std::memory_order_relaxed) &&
        activationState == 0 && invocationSurfaceToUse == 0) {
        const auto focusAttempt =
            g_focusAttempts.fetch_add(
                1, std::memory_order_relaxed) + 1;

        std::wstring desktopExe;
        if (GetTileDesktopAppExeName(pThis, &desktopExe)) {
            HWND existingWindow = nullptr;
            DWORD existingPid = 0;
            const bool found = FocusExistingDesktopAppWindow(
                desktopExe.c_str(), &existingWindow, &existingPid);

            Wh_Log(L"[start-focus] attempt=%lu exe=\"%s\" existing=%d hwnd=%p pid=%lu",
                   focusAttempt, desktopExe.c_str(), found ? 1 : 0,
                   existingWindow, existingPid);

            if (found) {
                Wh_Log(L"[start-focus] suppressing normal tile launch; focused existing desktop window");
                return;
            }
        } else {
            Wh_Log(L"[start-focus] attempt=%lu no DesktopAppExeName; using original activation",
                   focusAttempt);
        }
    }

    TileData_Activate_Original(
        pThis, activationState, invocationSurfaceToUse, coreWindow);

    int appStateAfter = -1;
    if (TileData_GetAppState_Original) {
        appStateAfter = TileData_GetAppState_Original(pThis);
    }

    if (g_traceTileActivation.load(std::memory_order_relaxed)) {
        Wh_Log(L"[start-activate] TileData::Activate EXIT call=%lu appStateAfter=%d tid=%lu",
               n, appStateAfter, GetCurrentThreadId());
    }
}

WH_PRESERVE_ALL int TileData_GetAppState_Hook(void* pThis) {
    const int result = TileData_GetAppState_Original(pThis);
    const auto n =
        g_tileAppStateCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (g_traceTileActivation.load(std::memory_order_relaxed) && n <= 120) {
        Wh_Log(L"[start-activate] TileData::AppState call=%lu this=%p result=%d tid=%lu",
               n, pThis, result, GetCurrentThreadId());
    }

    return result;
}

WH_PRESERVE_ALL void* VerbDelegateCommand_GetIconGlyph_Hook(void* pThis) {
    void* result = VerbDelegateCommand_GetIconGlyph_Original(pThis);
    const auto n =
        g_iconGlyphGetCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    int commandId = -1;
    if (VerbDelegateCommand_GetVerbType_Original) {
        commandId = VerbDelegateCommand_GetVerbType_Original(pThis);
    }

    if (g_traceTileActivation.load(std::memory_order_relaxed) &&
        (commandId == kTabletModeOpenNewWindowCommandId || n <= 80)) {
        Wh_Log(L"[start-icon] VerbDelegateCommand::IconGlyph GET call=%lu commandObj=%p command=0x%X (%s) glyph=%p tid=%lu",
               n, pThis, commandId, VerbCommandName(commandId), result,
               GetCurrentThreadId());
    }

    return result;
}

WH_PRESERVE_ALL void VerbDelegateCommand_SetIconGlyph_Hook(
    void* pThis, void* glyph) {
    const auto n =
        g_iconGlyphSetCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    int commandId = -1;
    if (VerbDelegateCommand_GetVerbType_Original) {
        commandId = VerbDelegateCommand_GetVerbType_Original(pThis);
    }

    void* replacementGlyph = nullptr;
    void* glyphToUse = glyph;

    if (commandId == kTabletModeOpenNewWindowCommandId &&
        g_restoreTabletOpenNewWindowIcon.load(std::memory_order_relaxed) &&
        WindowsCreateString_Dynamic) {
        static const wchar_t kWin10OpenNewWindowGlyph[] = {0xE8A7, 0};

        if (SUCCEEDED(WindowsCreateString_Dynamic(
                kWin10OpenNewWindowGlyph, 1, &replacementGlyph)) &&
            replacementGlyph) {
            glyphToUse = replacementGlyph;
            Wh_Log(L"[start-icon] restoring genuine Win10 TabletMode_OpenNewWindow glyph U+E8A7");
        }
    }

    if (g_traceTileActivation.load(std::memory_order_relaxed) &&
        (commandId == kTabletModeOpenNewWindowCommandId || n <= 80)) {
        Wh_Log(L"[start-icon] VerbDelegateCommand::IconGlyph SET call=%lu commandObj=%p command=0x%X (%s) glyph=%p useGlyph=%p tid=%lu",
               n, pThis, commandId, VerbCommandName(commandId), glyph,
               glyphToUse, GetCurrentThreadId());
    }

    VerbDelegateCommand_SetIconGlyph_Original(pThis, glyphToUse);

    if (replacementGlyph && WindowsDeleteString_Dynamic) {
        WindowsDeleteString_Dynamic(replacementGlyph);
    }
}


WH_PRESERVE_ALL bool
PerFrameData_ProvideStartWindowId_Hook(void* pThis) {
    const bool real = PerFrameData_ProvideStartWindowId_Original(pThis);
    const bool force =
        g_forceProvideStartWindowIdOnTileActivation.load(
            std::memory_order_relaxed) &&
        g_forceStartPropertiesTabletMode.load(std::memory_order_relaxed);
    const bool result = force ? true : real;

    const auto n =
        g_perFrameDataProvideWindowIdCalls.fetch_add(
            1, std::memory_order_relaxed) + 1;
    if (g_traceTileActivation.load(std::memory_order_relaxed) && n <= 120) {
        Wh_Log(L"[start-frame] PerFrameData::ProvideStartWindowIdOnTileActivation call=%lu real=%d returned=%d force=%d tid=%lu",
               n, real ? 1 : 0, result ? 1 : 0, force ? 1 : 0,
               GetCurrentThreadId());
    }

    return result;
}


bool __cdecl DirectLaunchTryActivate_Hook(void* pThis, const void* zstringView) {
    const auto n =
        g_directLaunchCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    const bool suppress =
        g_disableDirectLaunchForWin32Tiles.load(std::memory_order_relaxed) &&
        g_forceStartPropertiesTabletMode.load(std::memory_order_relaxed);

    if (suppress) {
        Wh_Log(L"[start-directlaunch] call=%lu suppressing StartTileData DirectLaunchHelper::TryActivateUsingDirectLaunch this=%p view=%p -> false",
               n, pThis, zstringView);
        return false;
    }

    const bool result =
        DirectLaunchTryActivate_Original
            ? DirectLaunchTryActivate_Original(pThis, zstringView)
            : false;

    Wh_Log(L"[start-directlaunch] call=%lu passthrough this=%p view=%p -> %d",
           n, pThis, zstringView, result ? 1 : 0);
    return result;
}

bool IsKnownStartTileDataBuild(HMODULE module) {
    if (!module) return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<unsigned char*>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const DWORD timestamp = nt->FileHeader.TimeDateStamp;
    const DWORD size = nt->OptionalHeader.SizeOfImage;

    Wh_Log(L"[start-directlaunch] StartTileData.dll=%p timestamp/hash=0x%08X size=0x%08X",
           module, timestamp, size);

    // Exact Windows 11 25H2 reference binary supplied for this experiment.
    if (timestamp == 0xB0A6CD40 && size == 0x0055A000) {
        return true;
    }

    Wh_Log(L"[start-directlaunch] unsupported StartTileData.dll build; not installing direct-launch suppression hook");
    return false;
}

struct ExactHook {
    PCWSTR decoratedName;
    void* hook;
    void** original;
    PCWSTR shortName;
    bool installed;
};

size_t InstallExactHooks(HMODULE module, ExactHook* hooks, size_t hookCount) {
    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;

    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, &options, &symbol);
    if (!search) {
        Wh_Log(L"[start-tablet] couldn't enumerate symbols for module=%p", module);
        return 0;
    }

    size_t installed = 0;

    do {
        PCWSTR decorated = symbol.symbolDecorated;
        if (!decorated || !*decorated) decorated = symbol.symbol;
        if (!decorated || !*decorated) continue;

        for (size_t i = 0; i < hookCount; ++i) {
            auto& item = hooks[i];
            if (item.installed ||
                wcscmp(decorated, item.decoratedName) != 0) {
                continue;
            }

            if (Wh_SetFunctionHook(symbol.address, item.hook, item.original)) {
                item.installed = true;
                ++installed;
                Wh_Log(L"[start-tablet] queued %s at %p",
                       item.shortName, symbol.address);
            } else {
                Wh_Log(L"[start-tablet] hook failed for %s at %p",
                       item.shortName, symbol.address);
            }
            break;
        }
    } while (Wh_FindNextSymbol(search, &symbol));

    Wh_FindCloseSymbol(search);
    return installed;
}


bool InstallStartTileDataHooks(HMODULE startTileData) {
    if (!IsKnownStartTileDataBuild(startTileData)) {
        return false;
    }

    ExactHook hooks[] = {
        {
            L"?TryActivateUsingDirectLaunch@DirectLaunchHelper@@QEAA_NV?$basic_zstring_view@G@wil@@@Z",
            reinterpret_cast<void*>(&DirectLaunchTryActivate_Hook),
            reinterpret_cast<void**>(&DirectLaunchTryActivate_Original),
            L"DirectLaunchHelper::TryActivateUsingDirectLaunch",
            false,
        },
    };

    const size_t installed =
        InstallExactHooks(startTileData, hooks, ARRAYSIZE(hooks));

    if (installed != ARRAYSIZE(hooks)) {
        Wh_Log(L"[start-directlaunch] queued %llu/%llu StartTileData hooks; expected exact match",
               static_cast<unsigned long long>(installed),
               static_cast<unsigned long long>(ARRAYSIZE(hooks)));
        return false;
    }

    if (!Wh_ApplyHookOperations()) {
        Wh_Log(L"[start-directlaunch] Wh_ApplyHookOperations failed");
        return false;
    }

    Wh_Log(L"[start-directlaunch] installed DirectLaunch suppression hook; enabled=%d",
           g_disableDirectLaunchForWin32Tiles.load() ? 1 : 0);
    return true;
}

bool InstallHooks(HMODULE startUi) {
    TileData_GetDesktopAppExeName_Abi =
        reinterpret_cast<TileDataGetStringAbi_t>(
            FindExactSymbolAddress(
                startUi,
                L"?__abi_StartUI_ITileDataAppIdentity____abi_get_DesktopAppExeName@?QITileDataAppIdentity@StartUI@@TileData@2@UE$AAAJPEAPE$AAVString@Platform@@@Z"));

    HMODULE combase = GetModuleHandleW(L"combase.dll");
    if (!combase) {
        combase = LoadLibraryW(L"combase.dll");
    }
    if (combase) {
        WindowsGetStringRawBuffer_Dynamic =
            reinterpret_cast<WindowsGetStringRawBuffer_t>(
                GetProcAddress(combase, "WindowsGetStringRawBuffer"));
        WindowsCreateString_Dynamic =
            reinterpret_cast<WindowsCreateString_t>(
                GetProcAddress(combase, "WindowsCreateString"));
        WindowsDeleteString_Dynamic =
            reinterpret_cast<WindowsDeleteString_t>(
                GetProcAddress(combase, "WindowsDeleteString"));
    }

    Wh_Log(L"[start-focus] DesktopAppExeName getter=%p WindowsGetStringRawBuffer=%p WindowsCreateString=%p WindowsDeleteString=%p",
           reinterpret_cast<void*>(TileData_GetDesktopAppExeName_Abi),
           reinterpret_cast<void*>(WindowsGetStringRawBuffer_Dynamic),
           reinterpret_cast<void*>(WindowsCreateString_Dynamic),
           reinterpret_cast<void*>(WindowsDeleteString_Dynamic));

    ExactHook hooks[] = {
        {
            L"?get@?QIsTabletMode@__IStartPropertiesPublicNonVirtuals@StartUI@@1StartProperties@3@UE$AAA_NXZ",
            reinterpret_cast<void*>(&StartProperties_IsTabletMode_Hook),
            reinterpret_cast<void**>(&StartProperties_IsTabletMode_Original),
            L"StartProperties::IsTabletMode",
            false,
        },
        {
            L"?get@?QIsFullScreen@__IStartPropertiesPublicNonVirtuals@StartUI@@1StartProperties@3@UE$AAA_NXZ",
            reinterpret_cast<void*>(&StartProperties_IsFullScreen_Hook),
            reinterpret_cast<void**>(&StartProperties_IsFullScreen_Original),
            L"StartProperties::IsFullScreen",
            false,
        },
        {
            L"?get@?QIsFullScreenOrTabletMode@__IStartPropertiesPublicNonVirtuals@StartUI@@1StartProperties@3@UE$AAA_NXZ",
            reinterpret_cast<void*>(
                &StartProperties_IsFullScreenOrTabletMode_Hook),
            reinterpret_cast<void**>(
                &StartProperties_IsFullScreenOrTabletMode_Original),
            L"StartProperties::IsFullScreenOrTabletMode",
            false,
        },
        {
            L"?set@?QIsTabletMode@__IStartPropertiesPublicNonVirtuals@StartUI@@1StartProperties@3@UE$AAAX_N@Z",
            reinterpret_cast<void*>(&StartProperties_SetIsTabletMode_Hook),
            reinterpret_cast<void**>(&StartProperties_SetIsTabletMode_Original),
            L"StartProperties::set_IsTabletMode",
            false,
        },
        {
            L"?get@IsTabletMode@IShellModeChangedEventArgs@StartUI@Shell@Internal@Windows@@UE$AAA_NXZ",
            reinterpret_cast<void*>(&ShellModeChangedEventArgs_IsTabletMode_Hook),
            reinterpret_cast<void**>(
                &ShellModeChangedEventArgs_IsTabletMode_Original),
            L"ShellModeChangedEventArgs::IsTabletMode",
            false,
        },
        {
            L"?GetVerbCommand@?Q__IVerbAggregationEventPayloadPublicNonVirtuals@StartUI@@VerbAggregationEventPayload@2@UE$AAAPE$AAVVerbDelegateCommand@2@W4VerbCommandId@2@@Z",
            reinterpret_cast<void*>(
                &VerbAggregationEventPayload_GetVerbCommand_Hook),
            reinterpret_cast<void**>(
                &VerbAggregationEventPayload_GetVerbCommand_Original),
            L"VerbAggregationEventPayload::GetVerbCommand",
            false,
        },
        {
            L"?get@?QVerbCommands@__IVerbAggregationEventPayloadPublicNonVirtuals@StartUI@@1VerbAggregationEventPayload@3@UE$AAAPE$AAU?$IVector@PE$AAUVerbDelegateCommandPayload@StartUI@@@Collections@Foundation@Windows@@XZ",
            reinterpret_cast<void*>(
                &VerbAggregationEventPayload_GetVerbCommands_Hook),
            reinterpret_cast<void**>(
                &VerbAggregationEventPayload_GetVerbCommands_Original),
            L"VerbAggregationEventPayload::get_VerbCommands",
            false,
        },
        {
            L"?get@?QVerbType@__IVerbDelegateCommandPublicNonVirtuals@StartUI@@1VerbDelegateCommand@3@UE$AAA?AW4VerbCommandId@3@XZ",
            reinterpret_cast<void*>(
                &VerbDelegateCommand_GetVerbType_Hook),
            reinterpret_cast<void**>(
                &VerbDelegateCommand_GetVerbType_Original),
            L"VerbDelegateCommand::get_VerbType",
            false,
        },
        {
            L"??0VerbDelegateCommandPayload@StartUI@@QE$AAA@PE$AAVVerbDelegateCommand@1@@Z",
            reinterpret_cast<void*>(
                &VerbDelegateCommandPayload_Ctor1_Hook),
            reinterpret_cast<void**>(
                &VerbDelegateCommandPayload_Ctor1_Original),
            L"VerbDelegateCommandPayload::ctor(command)",
            false,
        },
        {
            L"??0VerbDelegateCommandPayload@StartUI@@QE$AAA@PE$AAVVerbDelegateCommand@1@W4VerbDelegateFlags@1@@Z",
            reinterpret_cast<void*>(
                &VerbDelegateCommandPayload_Ctor2_Hook),
            reinterpret_cast<void**>(
                &VerbDelegateCommandPayload_Ctor2_Original),
            L"VerbDelegateCommandPayload::ctor(command,flags)",
            false,
        },
        {
            L"?OnActivated@TileViewModel@StartUI@@AE$AAAXPE$AAVObject@Platform@@@Z",
            reinterpret_cast<void*>(&TileViewModel_OnActivated_Hook),
            reinterpret_cast<void**>(&TileViewModel_OnActivated_Original),
            L"TileViewModel::OnActivated",
            false,
        },
        {
            L"?Activate@?QITileActivatable@StartUI@@TileData@2@UE$AAAXW4ActivationState2@2@W4InvocationSurface2@2@PE$AAVCoreWindow@Core@UI@Windows@@@Z",
            reinterpret_cast<void*>(&TileData_Activate_Hook),
            reinterpret_cast<void**>(&TileData_Activate_Original),
            L"TileData::Activate",
            false,
        },
        {
            L"?get@?QITileDataAppData@StartUI@@AppState@TileData@2@UE$AAA?AW4TileAppState2@2@XZ",
            reinterpret_cast<void*>(&TileData_GetAppState_Hook),
            reinterpret_cast<void**>(&TileData_GetAppState_Original),
            L"TileData::get_AppState",
            false,
        },
        {
            L"?get@?QIconGlyph@__IVerbDelegateCommandPublicNonVirtuals@StartUI@@1VerbDelegateCommand@3@UE$AAAPE$AAVString@Platform@@XZ",
            reinterpret_cast<void*>(&VerbDelegateCommand_GetIconGlyph_Hook),
            reinterpret_cast<void**>(&VerbDelegateCommand_GetIconGlyph_Original),
            L"VerbDelegateCommand::get_IconGlyph",
            false,
        },
        {
            L"?set@?QIconGlyph@__IVerbDelegateCommandPublicNonVirtuals@StartUI@@1VerbDelegateCommand@3@UE$AAAXPE$AAVString@Platform@@@Z",
            reinterpret_cast<void*>(&VerbDelegateCommand_SetIconGlyph_Hook),
            reinterpret_cast<void**>(&VerbDelegateCommand_SetIconGlyph_Original),
            L"VerbDelegateCommand::set_IconGlyph",
            false,
        },
        {
            L"?get@?QIPerFrameMetrics@StartUI@@ProvideStartWindowIdOnTileActivation@PerFrameData@2@UE$AAA_NXZ",
            reinterpret_cast<void*>(
                &PerFrameData_ProvideStartWindowId_Hook),
            reinterpret_cast<void**>(
                &PerFrameData_ProvideStartWindowId_Original),
            L"PerFrameData::ProvideStartWindowIdOnTileActivation",
            false,
        },
        {
            L"?ShowContextMenu@ContextMenuBehavior@StartUI@@AE$AAAXI@Z",
            reinterpret_cast<void*>(&ContextMenuBehavior_ShowContextMenu_Hook),
            reinterpret_cast<void**>(
                &ContextMenuBehavior_ShowContextMenu_Original),
            L"ContextMenuBehavior::ShowContextMenu",
            false,
        },
        {
            L"?OnVerbAggregation@SystemListVerbProvider@StartUI@@AE$AAAXPE$AAUBaseEventPayload@2@@Z",
            reinterpret_cast<void*>(
                &SystemListVerbProvider_OnVerbAggregation_Hook),
            reinterpret_cast<void**>(
                &SystemListVerbProvider_OnVerbAggregation_Original),
            L"SystemListVerbProvider::OnVerbAggregation",
            false,
        },
        {
            L"?OnVerbAggregationEvent@?QICommandingVerbProvider@StartUI@@CommandingVerbProvider@2@UE$AAAXPE$AAUBaseEventPayload@2@@Z",
            reinterpret_cast<void*>(
                &CommandingVerbProvider_OnVerbAggregationEvent_Hook),
            reinterpret_cast<void**>(
                &CommandingVerbProvider_OnVerbAggregationEvent_Original),
            L"CommandingVerbProvider::OnVerbAggregationEvent",
            false,
        },
        {
            L"?OnVerbAggregationEvent@TileViewModel@StartUI@@AE$AAAXPE$AAUBaseEventPayload@2@@Z",
            reinterpret_cast<void*>(
                &TileViewModel_OnVerbAggregationEvent_Hook),
            reinterpret_cast<void**>(
                &TileViewModel_OnVerbAggregationEvent_Original),
            L"TileViewModel::OnVerbAggregationEvent",
            false,
        },
        {
            L"?OnVerbAggregationEvent@AllAppsExpandingGroupViewModel@StartUI@@AE$AAAXPE$AAUBaseEventPayload@2@@Z",
            reinterpret_cast<void*>(
                &AllAppsExpandingGroupViewModel_OnVerbAggregationEvent_Hook),
            reinterpret_cast<void**>(
                &AllAppsExpandingGroupViewModel_OnVerbAggregationEvent_Original),
            L"AllAppsExpandingGroupViewModel::OnVerbAggregationEvent",
            false,
        },
        {
            L"?_OnVerbAggregationEvent@TileGroupViewModel@StartUI@@AE$AAAXPE$AAUBaseEventPayload@2@@Z",
            reinterpret_cast<void*>(
                &TileGroupViewModel_OnVerbAggregationEvent_Hook),
            reinterpret_cast<void**>(
                &TileGroupViewModel_OnVerbAggregationEvent_Original),
            L"TileGroupViewModel::_OnVerbAggregationEvent",
            false,
        },
        {
            L"?_OnVerbAggregationEvent@TileFolderRegionViewModel@StartUI@@AE$AAAXPE$AAUBaseEventPayload@2@@Z",
            reinterpret_cast<void*>(
                &TileFolderRegionViewModel_OnVerbAggregationEvent_Hook),
            reinterpret_cast<void**>(
                &TileFolderRegionViewModel_OnVerbAggregationEvent_Original),
            L"TileFolderRegionViewModel::_OnVerbAggregationEvent",
            false,
        },
        {
            L"?OnVerbAggregationEvent@PlacesUtmViewModel@StartUI@@AE$AAAXPE$AAUBaseEventPayload@2@@Z",
            reinterpret_cast<void*>(
                &PlacesUtmViewModel_OnVerbAggregationEvent_Hook),
            reinterpret_cast<void**>(
                &PlacesUtmViewModel_OnVerbAggregationEvent_Original),
            L"PlacesUtmViewModel::OnVerbAggregationEvent",
            false,
        },
        {
            L"?CreateVerbDelegateCommand@StartUI@@YAPE$AAVVerbDelegateCommand@1@PE$AAVExecuteDelegate@1@W4VerbCommandId@1@PE$AAUIResourceLoaderInternal@SharedUtilities@@@Z",
            reinterpret_cast<void*>(&CreateVerbDelegateCommand_Hook),
            reinterpret_cast<void**>(&CreateVerbDelegateCommand_Original),
            L"CreateVerbDelegateCommand",
            false,
        },
        {
            L"?LookupVerbMetadata@StartUI@@YAPEBUVerbMetadata@1@W4VerbCommandId@1@PEAI@Z",
            reinterpret_cast<void*>(&LookupVerbMetadata_Hook),
            reinterpret_cast<void**>(&LookupVerbMetadata_Original),
            L"LookupVerbMetadata",
            false,
        },
        {
            L"?LookupVerbMetadata@StartUI@@YAPEBUVerbMetadata@1@PEB_WPEAI@Z",
            reinterpret_cast<void*>(&LookupVerbMetadataByName_Hook),
            reinterpret_cast<void**>(&LookupVerbMetadataByName_Original),
            L"LookupVerbMetadata(canonical name)",
            false,
        },
    };

    const size_t installed =
        InstallExactHooks(startUi, hooks, ARRAYSIZE(hooks));

    if (!installed) {
        Wh_Log(L"[start-tablet] no StartUI hooks were queued");
        return false;
    }

    if (!Wh_ApplyHookOperations()) {
        Wh_Log(L"[start-tablet] Wh_ApplyHookOperations failed");
        return false;
    }

    Wh_Log(L"[start-tablet] installed %llu/27 hooks; forceStartTablet=%d forceShellEvent=%d traceVerb=%d mapOpenNewWindow=%d mapOpenFallback=%d restoreIcon=%d disableDirectLaunch=%d traceTileActivation=%d forceWin10Surface=%d forceStartWindowId=%d focusExistingDesktop=%d",
           static_cast<unsigned long long>(installed),
           g_forceStartPropertiesTabletMode.load() ? 1 : 0,
           g_forceShellModeEventTabletMode.load() ? 1 : 0,
           g_traceVerbPipeline.load() ? 1 : 0,
           g_mapOpenNewWindowToTabletMode.load() ? 1 : 0,
           g_mapOpenVerbFallbackToTabletMode.load() ? 1 : 0,
           g_restoreTabletOpenNewWindowIcon.load() ? 1 : 0,
           g_disableDirectLaunchForWin32Tiles.load() ? 1 : 0,
           g_traceTileActivation.load() ? 1 : 0,
           g_forceWin10InvocationSurface.load() ? 1 : 0,
           g_forceProvideStartWindowIdOnTileActivation.load() ? 1 : 0,
           g_focusExistingDesktopAppOnTileClick.load() ? 1 : 0);
    return true;
}

DWORD WINAPI WorkerThreadProc(void*) {
    while (!g_afterInit.load(std::memory_order_acquire)) {
        if (WaitForSingleObject(g_stopEvent, 20) != WAIT_TIMEOUT) return 0;
    }

    const ULONGLONG deadline =
        GetTickCount64() + g_waitTimeoutMs.load(std::memory_order_relaxed);

    bool startUiInstalled = false;
    bool startTileDataInstalled =
        !g_disableDirectLaunchForWin32Tiles.load(std::memory_order_relaxed);

    if (startTileDataInstalled) {
        Wh_Log(L"[start-directlaunch] direct-launch experiment disabled; StartTileData hook will not be installed");
    }

    while (!g_unloading.load(std::memory_order_relaxed)) {
        if (WaitForSingleObject(g_stopEvent, 0) != WAIT_TIMEOUT) return 0;

        if (!startUiInstalled) {
            HMODULE startUi = GetModuleHandleW(L"StartUI_.dll");
            PCWSTR moduleName = L"StartUI_.dll";
            if (!startUi) {
                startUi = GetModuleHandleW(L"StartUI.dll");
                moduleName = L"StartUI.dll";
            }

            if (startUi) {
                Wh_Log(L"[start-tablet] %s loaded at %p; resolving matching StartUI.pdb symbols",
                       moduleName, startUi);
                startUiInstalled = InstallHooks(startUi);
            }
        }

        if (!startTileDataInstalled) {
            HMODULE startTileData = GetModuleHandleW(L"StartTileData.dll");
            if (startTileData) {
                Wh_Log(L"[start-directlaunch] StartTileData.dll loaded at %p; resolving matching PDB symbol",
                       startTileData);
                startTileDataInstalled =
                    InstallStartTileDataHooks(startTileData);
            }
        }

        if (startUiInstalled && startTileDataInstalled) {
            return 0;
        }

        if (GetTickCount64() >= deadline) {
            if (!startUiInstalled) {
                Wh_Log(L"[start-tablet] timed out waiting for/installing StartUI_.dll/StartUI.dll");
            }
            if (!startTileDataInstalled) {
                Wh_Log(L"[start-directlaunch] timed out waiting for/installing StartTileData.dll");
            }
            return 0;
        }

        if (WaitForSingleObject(g_stopEvent, 50) != WAIT_TIMEOUT) return 0;
    }

    return 0;
}

}  // namespace

BOOL Wh_ModInit() {
    Wh_Log(L"Loading merged StartUI Tablet Mode restoration subsystem version %s",
           WH_MOD_VERSION);
    LoadSettings();

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) return FALSE;

    g_workerThread =
        CreateThread(nullptr, 0, WorkerThreadProc, nullptr, 0, nullptr);
    if (!g_workerThread) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        return FALSE;
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    g_afterInit.store(true, std::memory_order_release);
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    LoadSettings();
    *bReload = FALSE;

    Wh_Log(L"[start-tablet] settings changed: forceStartTablet=%d forceShellEvent=%d traceVerb=%d mapOpenNewWindow=%d mapOpenFallback=%d restoreIcon=%d disableDirectLaunch=%d traceTileActivation=%d forceWin10Surface=%d forceStartWindowId=%d focusExistingDesktop=%d verbose=%d",
           g_forceStartPropertiesTabletMode.load() ? 1 : 0,
           g_forceShellModeEventTabletMode.load() ? 1 : 0,
           g_traceVerbPipeline.load() ? 1 : 0,
           g_mapOpenNewWindowToTabletMode.load() ? 1 : 0,
           g_mapOpenVerbFallbackToTabletMode.load() ? 1 : 0,
           g_restoreTabletOpenNewWindowIcon.load() ? 1 : 0,
           g_disableDirectLaunchForWin32Tiles.load() ? 1 : 0,
           g_traceTileActivation.load() ? 1 : 0,
           g_forceWin10InvocationSurface.load() ? 1 : 0,
           g_forceProvideStartWindowIdOnTileActivation.load() ? 1 : 0,
           g_focusExistingDesktopAppOnTileClick.load() ? 1 : 0,
           g_verbose.load() ? 1 : 0);
    return TRUE;
}

void Wh_ModBeforeUninit() {
    g_unloading.store(true, std::memory_order_release);
    if (g_stopEvent) SetEvent(g_stopEvent);
    if (g_workerThread) WaitForSingleObject(g_workerThread, 3000);
}

void Wh_ModUninit() {
    if (g_workerThread) {
        CloseHandle(g_workerThread);
        g_workerThread = nullptr;
    }

    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }

    Wh_Log(L"Unloading merged StartUI Tablet Mode restoration subsystem");
}
}  // namespace StartHost


namespace {

enum class HostProcess {
    Explorer,
    StartMenuExperienceHost,
    Unsupported,
};

HostProcess DetectHostProcess() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (!len || len >= ARRAYSIZE(path)) {
        return HostProcess::Unsupported;
    }

    const wchar_t* base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;

    if (_wcsicmp(base, L"explorer.exe") == 0) {
        return HostProcess::Explorer;
    }
    if (_wcsicmp(base, L"StartMenuExperienceHost.exe") == 0) {
        return HostProcess::StartMenuExperienceHost;
    }
    return HostProcess::Unsupported;
}

HostProcess g_hostProcess = HostProcess::Unsupported;

}  // namespace

BOOL Wh_ModInit() {
    g_hostProcess = DetectHostProcess();
    switch (g_hostProcess) {
        case HostProcess::Explorer:
            Wh_Log(L"[tablet-merged] initializing Explorer/controller subsystem");
            return ExplorerHost::Wh_ModInit();
        case HostProcess::StartMenuExperienceHost:
            Wh_Log(L"[tablet-merged] initializing StartUI subsystem");
            return StartHost::Wh_ModInit();
        default:
            Wh_Log(L"[tablet-merged] unsupported host process");
            return FALSE;
    }
}

void Wh_ModAfterInit() {
    switch (g_hostProcess) {
        case HostProcess::Explorer:
            ExplorerHost::Wh_ModAfterInit();
            break;
        case HostProcess::StartMenuExperienceHost:
            StartHost::Wh_ModAfterInit();
            break;
        default:
            break;
    }
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    switch (g_hostProcess) {
        case HostProcess::Explorer:
            return ExplorerHost::Wh_ModSettingsChanged(bReload);
        case HostProcess::StartMenuExperienceHost:
            return StartHost::Wh_ModSettingsChanged(bReload);
        default:
            if (bReload) *bReload = FALSE;
            return TRUE;
    }
}

void Wh_ModBeforeUninit() {
    switch (g_hostProcess) {
        case HostProcess::Explorer:
            ExplorerHost::Wh_ModBeforeUninit();
            break;
        case HostProcess::StartMenuExperienceHost:
            StartHost::Wh_ModBeforeUninit();
            break;
        default:
            break;
    }
}

void Wh_ModUninit() {
    switch (g_hostProcess) {
        case HostProcess::Explorer:
            ExplorerHost::Wh_ModUninit();
            break;
        case HostProcess::StartMenuExperienceHost:
            StartHost::Wh_ModUninit();
            break;
        default:
            break;
    }
}
