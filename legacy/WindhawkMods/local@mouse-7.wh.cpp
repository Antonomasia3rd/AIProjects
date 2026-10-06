// ==WindhawkMod==
// @id              mouse-7
// @name            MOUSE-7 — Tablet Mode mouse diagnostic suite
// @description     Consolidated read-only Win10/Win11 Tablet Mode mouse diagnostics: top-edge HWND state, s_WndProc dispatch, Win10 legacy route baseline, desktop move/size competition, and shell window-management tracing
// @version         1.0.16
// @author          AI-assisted reverse-engineering experiment
// @include         explorer.exe
// @compilerOptions -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# MOUSE-7 — consolidated Tablet Mode mouse diagnostic suite

This is the single read-only mouse diagnostic mod for the project from now on.
It supersedes the current purposes of:
- MOUSE-2 (Win10 legacy mouse-route baseline),
- the old MOUSE-7 (top-edge HWND/window-state inspection),
- MOUSE-8 (CEdgeUiInput::s_WndProc dispatch inspection).

The mod ID is intentionally stable: `mouse-7`.
Future read-only mouse diagnostics should be added here by version bump instead
of creating MOUSE-9, MOUSE-10, etc.

Supported exact twinui.dll builds:
- Windows 10 baseline: timestamp 0x15FB3EA7, SizeOfImage 0x0061B000
- Windows 11 25H2:    timestamp 0x7827BEC5, SizeOfImage 0x004FD000

Behavior:
- Both OSes:
  * watches physical top-edge HWND state using Win32/DWM APIs;
  * hooks CEdgeUiInput::s_WndProc and logs real USER32-facing mouse dispatch;
  * never changes visibility, capture, z-order, input, return values, or COM state;
  * v1.0.2 adds out-of-context WinEvent + button-transition tracing so a
    physical top-edge drag can be correlated with the normal desktop move/size
    loop (unmaximize/move/re-maximize/foreground changes).
  * v1.0.3 identifies the executable/title/owner of foreground windows and
    read-only hooks Explorer's ShowWindow/ShowWindowAsync/SetWindowPos/
    SetWindowPlacement calls for the exact physical-drag target.
  * v1.0.4 also traces those Win32 visibility APIs when their target is an
    EdgeUiInputTopWndClass listener and decodes WINDOWPOS show/hide flags.
  * v1.0.5 additionally resolves the surviving
    CEdgeUiInput::s_fDisableTitlebarInvocation flag and passively snapshots it
    together with the per-instance SetVisible state byte used by the exact
    Win10/Win11 builds. No value is modified. This distinguishes a still-closed
    titlebar gate from a per-instance/native-state suppression of SetVisible.
  * v1.0.6 additionally reads the legacy WNF_TMCN_ISTABLETMODE state through
    RtlQueryWnfStateData on each top-edge snapshot. This is read-only and lets
    us distinguish the remaining native SetVisible edge-2 branch without
    changing the persistent WNF state or synthesizing any mode notification.
  * v1.0.7 attempted Win10-only TabletModeInputHandler tracing.
  * v1.0.8 corrects that probe to target twinui.pcshell.dll (where
    TabletModeInputHandler and TabletModeViewManager actually live) and also
    traces the Win10 ViewManager StartDrag/ContinueDrag/CommitDrag/CancelDrag
    lifecycle. Win11 deliberately does not duplicate these hooks because the
    real restoration mod already traces them there.
  * v1.0.9 adds a tiny cross-build titlebar hit-test probe for
    CTitleBarInvoker::v_HitCornerOrEdge and _IsPointOverTitleBarUI. This is
    read-only and is intended to explain why the restored listener can stay
    armed over SystemSettings while the same top-edge hit over a Win32 app can
    immediately fall into InputObserveStart/raw observation.
  * v1.0.10 adds a cross-build twinui.pcshell probe for the normal desktop
    titlebar-drag handoff: TabletModeViewManager::ShowAppResizeView,
    TabletModeViewManagerProxy::ShowAppResizeView,
    TabletModeViewManager::ShowWindowArrangementView, and
    TabletModeInputHandler_CreateInstance. It is read-only and is intended to
    locate the Win10 interception that produces the Tablet Mode drag proxy for
    maximized Win32 windows, then show whether Win11 still enters that path.
  * v1.0.11 follows the now-proven Win10 path one step upstream by tracing
    TabletModePositionerManager::OnMoveSizeAttempted plus
    TabletModeViewManager{Proxy}::MoveSizeAttempted. The positioner callback
    changed signature between Win10 and Win11, so the probe uses the exact
    decorated symbol/signature for each build. It also tags nested
    ShowWindowArrangementView calls with the current move-size depths.
  * v1.0.12 is intentionally skipped/retired; that experimental guessed
    CApplicationManager hook had the wrong hot-path/ABI assumptions.
  * v1.0.13 added exact-PDB, read-only WindowManagementEvents probes for
    RegisterShowMoveSize and OnShellWindowManagementNotify.
  * v1.0.14 removes the earlier assumption that the 1809 notify-ID numbering
    is unchanged on 25H2, logs every observed notify ID with raw payload fields,
    and adds the exact WindowManagementEvents::v_WndProc predecessor. Only
    private messages 0x341/0x342 are logged. A per-drag hook-status line remains
    visible even after DbgView is cleared, so hook installation is unambiguous.
    No event is synthesized and no input, return value, or object state changes.
  * v1.0.15 adds one exact-PDB, read-only 25H2 probe for
    CPrivilegedArrangementOperations::EnableShellWindowManagementBehavior(HWND,...)
    so we can observe the modern per-window shell-window-management policy that
    GamingPosturePositioner uses for SetInterceptMoveAndSize. The hook only logs
    HWND/mask/features and returns the native result unchanged.
  * v1.0.16 fixes the v1.0.15 startup race: on a fresh Explorer,
    twinui.pcshell.dll can load after MOUSE-7. The existing watcher now performs
    one deferred exact-PDB install as soon as pcshell appears, then applies the
    hook operations. This remains read-only and makes the per-window probe active
    before the delayed Tablet Mode transition.
- Windows 10:
  * additionally installs the historical MOUSE-2 read-only route hooks so one
    mod captures _WndProc -> _OnMouseDown/_OnMouseMove -> MouseDragStart ->
    CEdgeInvoker -> CTitleBarInvoker.
- Windows 11:
  * intentionally does NOT duplicate those route hooks because the real
    windows-10-tablet-mode-restoration mod already traces that downstream path.
    This minimizes interference while still exposing the missing s_WndProc
    boundary and top-edge HWND state.

Recommended project layout: keep only this probe (`mouse-7`) plus the real
`windows-10-tablet-mode-restoration` implementation installed. Old numbered
probe mods are superseded by this suite.
*/
// ==/WindhawkModReadme==

#include <windows.h>
#include <windowsx.h>
#include <atomic>
#include <cwchar>
#include <windhawk_api.h>

namespace {

constexpr DWORD kWin10Timestamp = 0x15FB3EA7;
constexpr DWORD kWin10Size      = 0x0061B000;
constexpr DWORD kWin11Timestamp = 0x7827BEC5;
constexpr DWORD kWin11Size      = 0x004FD000;

enum class BuildKind {
    Unsupported,
    Win10,
    Win11_25H2,
};

BuildKind g_build = BuildKind::Unsupported;

// Read-only native SetVisible gating state. On the exact tested builds, the
// CEdgeUiInput object stored in HWND extra bytes is 0x18 before the interface
// pointer received by SetVisible, and SetVisible caches one native eligibility
// byte at +0x7F (Win10) / +0x87 (Win11) from that interface pointer.
volatile bool* g_disableTitlebarInvocationFlag = nullptr;
constexpr size_t kEdgeUiInputInterfaceOffset = 0x18;

// Legacy Tablet Mode WNF state consumed directly by CEdgeUiInput::SetVisible
// for the titlebar (edge 2) listener. Reading it is diagnostic only.
constexpr unsigned long long kWnfTmcnIsTabletMode =
    0x0F850339A3BC0835ULL;
using WnfQueryCallback_t = LONG(NTAPI*)(
    unsigned long long stateName, ULONG changeStamp, void* typeId,
    void* callbackContext, const void* buffer, ULONG length);
using RtlQueryWnfStateData_t = LONG(NTAPI*)(
    ULONG* changeStamp, unsigned long long stateName, void* callback,
    void* callbackContext, const void* typeId);
RtlQueryWnfStateData_t g_RtlQueryWnfStateData = nullptr;


bool IsMainShellExplorer() {
    HWND shell = GetShellWindow();
    if (!shell) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(shell, &pid);
    return pid == 0 || pid == GetCurrentProcessId();
}

BuildKind DetectTwinuiBuild(HMODULE module) {
    if (!module) return BuildKind::Unsupported;
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return BuildKind::Unsupported;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) return BuildKind::Unsupported;

    const DWORD ts = nt->FileHeader.TimeDateStamp;
    const DWORD size = nt->OptionalHeader.SizeOfImage;
    BuildKind kind = BuildKind::Unsupported;
    const wchar_t* name = L"unsupported";
    if (ts == kWin10Timestamp && size == kWin10Size) {
        kind = BuildKind::Win10;
        name = L"Win10";
    } else if (ts == kWin11Timestamp && size == kWin11Size) {
        kind = BuildKind::Win11_25H2;
        name = L"Win11-25H2";
    }

    Wh_Log(L"[mouse-7] twinui=%p timestamp=0x%08X size=0x%08X build=%s",
           module, ts, size, name);
    return kind;
}

std::atomic<unsigned long> g_acquire{0}, g_observeStart{0}, g_raw{0};
std::atomic<unsigned long> g_uiDown{0}, g_uiMove{0}, g_handle{0};
std::atomic<unsigned long> g_invokerDown{0}, g_invokerMove{0}, g_invokerStart{0};
std::atomic<unsigned long> g_managerStart{0};
std::atomic<unsigned long> g_wndProcMouse{0}, g_isMousePen{0};
std::atomic<unsigned long> g_onMouseDown{0}, g_onMouseMove{0}, g_moveToEdge{0};
std::atomic<unsigned long> g_managerHit{0}, g_invokerHit{0};
std::atomic<unsigned long> g_titleDown{0}, g_titleMove{0}, g_titleStart{0}, g_titleInvoke{0};
std::atomic<unsigned long> g_titleHit{0}, g_titleIsOver{0};
std::atomic<unsigned long> g_inputInit{0}, g_inputDown{0}, g_inputUpdate{0}, g_inputUp{0};
std::atomic<unsigned long> g_vmStartDrag{0}, g_vmContinueDrag{0}, g_vmCommitDrag{0}, g_vmCancelDrag{0};
std::atomic<unsigned long> g_showAppResize{0}, g_proxyShowAppResize{0};
std::atomic<unsigned long> g_showArrangement{0}, g_inputCreate{0};
std::atomic<unsigned long> g_vmMoveSizeAttempted{0}, g_proxyMoveSizeAttempted{0};
std::atomic<unsigned long> g_positionerMoveSizeAttempted{0};
std::atomic<unsigned long> g_wmNotify{0}, g_wmWndProcPrivate{0},
    g_wmRegisterShowMoveSize{0}, g_wmHooksInstalled{0};
std::atomic<unsigned long> g_perWindowShellBehavior{0},
    g_perWindowShellBehaviorHooksInstalled{0};
std::atomic<bool> g_deferredPcshellProbeAttempted{false};
thread_local unsigned long t_vmMoveSizeDepth = 0;
thread_local unsigned long t_positionerMoveSizeDepth = 0;

bool ShouldLogLimited(std::atomic<unsigned long>& counter, unsigned long* callNumber) {
    const auto n = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (callNumber) {
        *callNumber = n;
    }
    return n <= 120 || (n % 100) == 0;
}

using Acquire_t = HRESULT(__cdecl*)(void*, int, void**);
using ObserveStart_t = HRESULT(__cdecl*)(void*, void*);
using RegisterRaw_t = void(__cdecl*)(void*, bool);
using UiDown_t = HRESULT(__cdecl*)(void*, POINT, int);
using UiMove_t = HRESULT(__cdecl*)(void*, POINT, unsigned short, POINT);
using Handle_t = void(__cdecl*)(void*, bool, POINT, POINT);
using InvokerPoint_t = HRESULT(__cdecl*)(void*, POINT);
using InvokerMove_t = HRESULT(__cdecl*)(void*, POINT, POINT);
using ManagerStart_t = HRESULT(__cdecl*)(void*, void*, void**);
using WndProc_t = LRESULT(__cdecl*)(void*, unsigned int, UINT_PTR, LONG_PTR, bool);
using IsMousePen_t = int(__cdecl*)(void*);
using OnMouseDown_t = void(__cdecl*)(void*, POINT, bool);
using OnMouseMove_t = void(__cdecl*)(void*, POINT, bool, bool);
using MoveToEdge_t = HRESULT(__cdecl*)(void*, int, POINT, bool, bool, bool);
using ManagerHit_t = HRESULT(__cdecl*)(void*, void*, bool, bool, POINT, bool*, void**);
using InvokerHit_t = HRESULT(__cdecl*)(void*, int, POINT, int*, int);
using DerivedPointVoid_t = void(__cdecl*)(void*, POINT);
using DerivedPointHr_t = HRESULT(__cdecl*)(void*, POINT);
using DerivedInvoke_t = HRESULT(__cdecl*)(void*, bool, POINT, int);
using TitleHit_t = HRESULT(__cdecl*)(void*, POINT, bool);
using TitleIsOver_t = bool(__cdecl*)(void*, POINT);
using TabletInputInit_t = HRESULT(__cdecl*)(void*, int, void*);
using TabletInputPointer_t = void(__cdecl*)(void*, unsigned int, POINT);
using TabletVmPoint_t = HRESULT(__cdecl*)(void*, POINT);
using TabletVmCancel_t = HRESULT(__cdecl*)(void*);
using TabletVmShowAppResize_t = HRESULT(__cdecl*)(void*, void*, int, POINT);
using TabletVmShowArrangement_t = HRESULT(__cdecl*)(void*, void*, void*, RECT*, POINT*, RECT*, int, int, void*);
using TabletInputCreate_t = HRESULT(__cdecl*)(int, void*, const GUID*, void**);
using TabletVmMoveSizeAttempted_t = HRESULT(__cdecl*)(void*, void*, int);
using TabletPositionerMoveSizeWin10_t = void(__cdecl*)(void*, void*, ULONG);
using TabletPositionerMoveSizeWin11_t = void(__cdecl*)(void*, HWND, void*, ULONG, ULONG);
using WindowManagementWndProc_t = LRESULT(__cdecl*)(void*, HWND, UINT, WPARAM, LPARAM);
using WindowManagementNotify_t = void(__cdecl*)(void*, const void*);
using WindowManagementRegisterShowMoveSize_t = HRESULT(__cdecl*)(void*, void*);
using PerWindowShellBehavior_t = int(__cdecl*)(void*, HWND, int, int);

Acquire_t Acquire_Original = nullptr;
ObserveStart_t ObserveStart_Original = nullptr;
RegisterRaw_t RegisterRaw_Original = nullptr;
UiDown_t UiDown_Original = nullptr;
UiMove_t UiMove_Original = nullptr;
Handle_t Handle_Original = nullptr;
InvokerPoint_t InvokerDown_Original = nullptr;
InvokerMove_t InvokerMove_Original = nullptr;
InvokerPoint_t InvokerStart_Original = nullptr;
ManagerStart_t ManagerStart_Original = nullptr;
WndProc_t WndProc_Original = nullptr;
IsMousePen_t IsMousePen_Original = nullptr;
OnMouseDown_t OnMouseDown_Original = nullptr;
OnMouseMove_t OnMouseMove_Original = nullptr;
MoveToEdge_t MoveToEdge_Original = nullptr;
ManagerHit_t ManagerHit_Original = nullptr;
InvokerHit_t InvokerHit_Original = nullptr;
DerivedPointVoid_t TitleDown_Original = nullptr;
DerivedPointVoid_t TitleMove_Original = nullptr;
DerivedPointHr_t TitleStart_Original = nullptr;
DerivedInvoke_t TitleInvoke_Original = nullptr;
TitleHit_t TitleHit_Original = nullptr;
TitleIsOver_t TitleIsOver_Original = nullptr;
TabletInputInit_t InputInit_Original = nullptr;
TabletInputPointer_t InputDown_Original = nullptr;
TabletInputPointer_t InputUpdate_Original = nullptr;
TabletInputPointer_t InputUp_Original = nullptr;
TabletVmPoint_t VmStartDrag_Original = nullptr;
TabletVmPoint_t VmContinueDrag_Original = nullptr;
TabletVmPoint_t VmCommitDrag_Original = nullptr;
TabletVmCancel_t VmCancelDrag_Original = nullptr;
TabletVmShowAppResize_t VmShowAppResize_Original = nullptr;
TabletVmShowAppResize_t ProxyShowAppResize_Original = nullptr;
TabletVmShowArrangement_t VmShowArrangement_Original = nullptr;
TabletInputCreate_t InputCreate_Original = nullptr;
TabletVmMoveSizeAttempted_t VmMoveSizeAttempted_Original = nullptr;
TabletVmMoveSizeAttempted_t ProxyMoveSizeAttempted_Original = nullptr;
TabletPositionerMoveSizeWin10_t PositionerMoveSizeWin10_Original = nullptr;
TabletPositionerMoveSizeWin11_t PositionerMoveSizeWin11_Original = nullptr;
WindowManagementWndProc_t WindowManagementWndProc_Original = nullptr;
WindowManagementNotify_t WindowManagementNotify_Original = nullptr;
WindowManagementRegisterShowMoveSize_t WindowManagementRegisterShowMoveSize_Original = nullptr;
PerWindowShellBehavior_t PerWindowShellBehavior_Original = nullptr;

HRESULT Acquire_Hook(void* self, int edge, void** out) {
    auto n = ++g_acquire;
    HRESULT hr = Acquire_Original(self, edge, out);
    Wh_Log(L"[mouse-7-route] CEdgeUiManager::_AcquireMouseInvokerForEdge #%lu edge=%d hr=0x%08X invoker=%p tid=%lu",
           n, edge, (unsigned)hr, out ? *out : nullptr, GetCurrentThreadId());
    return hr;
}
HRESULT ObserveStart_Hook(void* self, void* edgeInput) {
    auto n = ++g_observeStart;
    Wh_Log(L"[mouse-7-route] CEdgeUiManager::InputObserveStart #%lu edgeInput=%p ENTER tid=%lu", n, edgeInput, GetCurrentThreadId());
    HRESULT hr = ObserveStart_Original(self, edgeInput);
    Wh_Log(L"[mouse-7-route] CEdgeUiManager::InputObserveStart #%lu EXIT hr=0x%08X", n, (unsigned)hr);
    return hr;
}
void RegisterRaw_Hook(void* self, bool enable) {
    auto n = ++g_raw;
    Wh_Log(L"[mouse-7-route] CEdgeUiInput::_RegisterRawInput #%lu this=%p enable=%d tid=%lu", n, self, enable ? 1 : 0, GetCurrentThreadId());
    RegisterRaw_Original(self, enable);
}
HRESULT UiDown_Hook(void* self, POINT p, int button) {
    auto n = ++g_uiDown;
    Wh_Log(L"[mouse-7-route] CEdgeUiInput::ObservedMouseButtonDown #%lu this=%p button=%d pt=(%ld,%ld) tid=%lu", n, self, button, p.x, p.y, GetCurrentThreadId());
    return UiDown_Original(self, p, button);
}
HRESULT UiMove_Hook(void* self, POINT p, unsigned short flags, POINT a) {
    unsigned long n = 0;
    if (ShouldLogLimited(g_uiMove, &n)) {
        Wh_Log(L"[mouse-7-route] CEdgeUiInput::ObservedMouseMove #%lu this=%p pt=(%ld,%ld) flags=0x%04X anchor=(%ld,%ld) tid=%lu",
               n, self, p.x, p.y, (unsigned)flags, a.x, a.y, GetCurrentThreadId());
    }
    return UiMove_Original(self, p, flags, a);
}
void Handle_Hook(void* self, bool down, POINT p, POINT a) {
    auto n = ++g_handle;
    Wh_Log(L"[mouse-7-route] CEdgeUiInput::_HandleObservedMouseInput #%lu this=%p down=%d pt=(%ld,%ld) anchor=(%ld,%ld) tid=%lu",
           n, self, down ? 1 : 0, p.x, p.y, a.x, a.y, GetCurrentThreadId());
    Handle_Original(self, down, p, a);
}
HRESULT InvokerDown_Hook(void* self, POINT p) {
    auto n = ++g_invokerDown;
    Wh_Log(L"[mouse-7-route] CEdgeInvoker::ObservedMouseButtonDown #%lu this=%p pt=(%ld,%ld) ENTER tid=%lu", n, self, p.x, p.y, GetCurrentThreadId());
    HRESULT hr = InvokerDown_Original(self, p);
    Wh_Log(L"[mouse-7-route] CEdgeInvoker::ObservedMouseButtonDown #%lu EXIT hr=0x%08X", n, (unsigned)hr);
    return hr;
}
HRESULT InvokerMove_Hook(void* self, POINT p, POINT a) {
    unsigned long n = 0;
    if (ShouldLogLimited(g_invokerMove, &n)) {
        Wh_Log(L"[mouse-7-route] CEdgeInvoker::ObservedMouseMove #%lu this=%p pt=(%ld,%ld) anchor=(%ld,%ld) tid=%lu",
               n, self, p.x, p.y, a.x, a.y, GetCurrentThreadId());
    }
    return InvokerMove_Original(self, p, a);
}
HRESULT InvokerStart_Hook(void* self, POINT p) {
    auto n = ++g_invokerStart;
    Wh_Log(L"[mouse-7-route] CEdgeInvoker::StartDrag #%lu this=%p pt=(%ld,%ld) ENTER tid=%lu", n, self, p.x, p.y, GetCurrentThreadId());
    HRESULT hr = InvokerStart_Original(self, p);
    Wh_Log(L"[mouse-7-route] CEdgeInvoker::StartDrag #%lu EXIT hr=0x%08X", n, (unsigned)hr);
    return hr;
}
HRESULT ManagerStart_Hook(void* self, void* edgeInput, void** out) {
    auto n = ++g_managerStart;
    Wh_Log(L"[mouse-7-route] CEdgeUiManager::MouseDragStart #%lu edgeInput=%p ENTER tid=%lu", n, edgeInput, GetCurrentThreadId());
    HRESULT hr = ManagerStart_Original(self, edgeInput, out);
    Wh_Log(L"[mouse-7-route] CEdgeUiManager::MouseDragStart #%lu EXIT hr=0x%08X invocation=%p", n, (unsigned)hr, out ? *out : nullptr);
    return hr;
}

LRESULT WndProc_Hook(void* self, unsigned int msg, UINT_PTR wParam, LONG_PTR lParam, bool forwarded) {
    const bool interesting = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_NCMOUSEMOVE || msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONUP;
    if (interesting) {
        const auto n = g_wndProcMouse.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 160 || (n % 100) == 0) {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            Wh_Log(L"[mouse-7-route] CEdgeUiInput::_WndProc(mouse) #%lu this=%p msg=0x%04X wParam=0x%llX pt=(%ld,%ld) forwarded=%d tid=%lu",
                   n, self, msg, (unsigned long long)wParam, p.x, p.y, forwarded ? 1 : 0, GetCurrentThreadId());
        }
    }
    return WndProc_Original(self, msg, wParam, lParam, forwarded);
}
int IsMousePen_Hook(void* self) {
    int result = IsMousePen_Original(self);
    const auto n = g_isMousePen.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 80 || (n % 100) == 0) {
        Wh_Log(L"[mouse-7-route] CEdgeUiInput::_IsCurrentInputFromMouseOrPen #%lu this=%p -> %d tid=%lu", n, self, result, GetCurrentThreadId());
    }
    return result;
}
void OnMouseDown_Hook(void* self, POINT p, bool mode) {
    auto n = ++g_onMouseDown;
    Wh_Log(L"[mouse-7-route] CEdgeUiInput::_OnMouseDown #%lu this=%p pt=(%ld,%ld) arg=%d ENTER tid=%lu", n, self, p.x, p.y, mode ? 1 : 0, GetCurrentThreadId());
    OnMouseDown_Original(self, p, mode);
    Wh_Log(L"[mouse-7-route] CEdgeUiInput::_OnMouseDown #%lu EXIT", n);
}
void OnMouseMove_Hook(void* self, POINT p, bool a, bool b) {
    unsigned long n = 0;
    if (ShouldLogLimited(g_onMouseMove, &n)) {
        Wh_Log(L"[mouse-7-route] CEdgeUiInput::_OnMouseMove #%lu this=%p pt=(%ld,%ld) a=%d b=%d tid=%lu",
               n, self, p.x, p.y, a ? 1 : 0, b ? 1 : 0, GetCurrentThreadId());
    }
    OnMouseMove_Original(self, p, a, b);
}
HRESULT MoveToEdge_Hook(void* self, int cornerOrEdge, POINT p, bool a, bool b, bool c) {
    auto n = ++g_moveToEdge;
    Wh_Log(L"[mouse-7-route] CEdgeUiInput::_MouseMoveToCornerOrEdge #%lu this=%p edge=%d pt=(%ld,%ld) a=%d b=%d c=%d ENTER tid=%lu",
           n, self, cornerOrEdge, p.x, p.y, a ? 1 : 0, b ? 1 : 0, c ? 1 : 0, GetCurrentThreadId());
    HRESULT hr = MoveToEdge_Original(self, cornerOrEdge, p, a, b, c);
    Wh_Log(L"[mouse-7-route] CEdgeUiInput::_MouseMoveToCornerOrEdge #%lu EXIT hr=0x%08X", n, (unsigned)hr);
    return hr;
}
HRESULT ManagerHit_Hook(void* self, void* edgeInput, bool a, bool b, POINT p, bool* outHit, void** outInvoker) {
    auto n = ++g_managerHit;
    Wh_Log(L"[mouse-7-route] CEdgeUiManager::MouseHitCornerOrEdge #%lu edgeInput=%p a=%d b=%d pt=(%ld,%ld) ENTER tid=%lu",
           n, edgeInput, a ? 1 : 0, b ? 1 : 0, p.x, p.y, GetCurrentThreadId());
    HRESULT hr = ManagerHit_Original(self, edgeInput, a, b, p, outHit, outInvoker);
    Wh_Log(L"[mouse-7-route] CEdgeUiManager::MouseHitCornerOrEdge #%lu EXIT hr=0x%08X hit=%d invoker=%p",
           n, (unsigned)hr, outHit ? (*outHit ? 1 : 0) : -1, outInvoker ? *outInvoker : nullptr);
    return hr;
}
HRESULT InvokerHit_Hook(void* self, int cornerOrEdge, POINT p, int* notification, int arg) {
    auto n = ++g_invokerHit;
    Wh_Log(L"[mouse-7-route] CEdgeInvoker::HitCornerOrEdge #%lu this=%p edge=%d pt=(%ld,%ld) arg=%d ENTER tid=%lu",
           n, self, cornerOrEdge, p.x, p.y, arg, GetCurrentThreadId());
    HRESULT hr = InvokerHit_Original(self, cornerOrEdge, p, notification, arg);
    Wh_Log(L"[mouse-7-route] CEdgeInvoker::HitCornerOrEdge #%lu EXIT hr=0x%08X notification=%d",
           n, (unsigned)hr, notification ? *notification : -1);
    return hr;
}
void TitleDown_Hook(void* self, POINT p) {
    auto n = ++g_titleDown;
    Wh_Log(L"[mouse-7-route] CTitleBarInvoker::v_ObservedMouseButtonDown #%lu this=%p pt=(%ld,%ld) ENTER tid=%lu", n, self, p.x, p.y, GetCurrentThreadId());
    TitleDown_Original(self, p);
    Wh_Log(L"[mouse-7-route] CTitleBarInvoker::v_ObservedMouseButtonDown #%lu EXIT", n);
}
void TitleMove_Hook(void* self, POINT p) {
    unsigned long n = 0;
    if (ShouldLogLimited(g_titleMove, &n)) {
        Wh_Log(L"[mouse-7-route] CTitleBarInvoker::v_ObservedMouseMove #%lu this=%p pt=(%ld,%ld) tid=%lu",
               n, self, p.x, p.y, GetCurrentThreadId());
    }
    TitleMove_Original(self, p);
}
HRESULT TitleStart_Hook(void* self, POINT p) {
    auto n = ++g_titleStart;
    Wh_Log(L"[mouse-7-route] CTitleBarInvoker::v_StartDrag #%lu this=%p pt=(%ld,%ld) ENTER tid=%lu", n, self, p.x, p.y, GetCurrentThreadId());
    HRESULT hr = TitleStart_Original(self, p);
    Wh_Log(L"[mouse-7-route] CTitleBarInvoker::v_StartDrag #%lu EXIT hr=0x%08X", n, (unsigned)hr);
    return hr;
}
HRESULT TitleInvoke_Hook(void* self, bool a, POINT p, int rawType) {
    auto n = ++g_titleInvoke;
    Wh_Log(L"[mouse-7-route] CTitleBarInvoker::v_Invoke #%lu this=%p a=%d pt=(%ld,%ld) rawType=%d ENTER tid=%lu", n, self, a ? 1 : 0, p.x, p.y, rawType, GetCurrentThreadId());
    HRESULT hr = TitleInvoke_Original(self, a, p, rawType);
    Wh_Log(L"[mouse-7-route] CTitleBarInvoker::v_Invoke #%lu EXIT hr=0x%08X", n, (unsigned)hr);
    return hr;
}

HRESULT TitleHit_Hook(void* self, POINT p, bool a) {
    const auto n = ++g_titleHit;
    POINT below{p.x, p.y < 5 ? 5 : p.y};
    HWND atPoint = WindowFromPoint(p);
    HWND belowPoint = WindowFromPoint(below);
    Wh_Log(L"[mouse-7-titlehit] CTitleBarInvoker::v_HitCornerOrEdge #%lu this=%p pt=(%ld,%ld) arg=%d at=%p below=%p ENTER tid=%lu",
           n, self, p.x, p.y, a ? 1 : 0, atPoint, belowPoint, GetCurrentThreadId());
    HRESULT hr = TitleHit_Original(self, p, a);
    Wh_Log(L"[mouse-7-titlehit] CTitleBarInvoker::v_HitCornerOrEdge #%lu EXIT hr=0x%08X",
           n, (unsigned)hr);
    return hr;
}

bool TitleIsOver_Hook(void* self, POINT p) {
    const auto n = ++g_titleIsOver;
    POINT below{p.x, p.y < 5 ? 5 : p.y};
    HWND atPoint = WindowFromPoint(p);
    HWND belowPoint = WindowFromPoint(below);
    bool result = TitleIsOver_Original(self, p);
    Wh_Log(L"[mouse-7-titlehit] CTitleBarInvoker::_IsPointOverTitleBarUI #%lu this=%p pt=(%ld,%ld) -> %d at=%p below=%p tid=%lu",
           n, self, p.x, p.y, result ? 1 : 0, atPoint, belowPoint, GetCurrentThreadId());
    return result;
}

struct HookSpec { const wchar_t* decorated; void* hook; void** original; const wchar_t* name; bool found; };

HRESULT InputInit_Hook(void* self, int source, void* callback) {
    const auto n = ++g_inputInit;
    Wh_Log(L"[mouse-7-input] TabletModeInputHandler::RuntimeClassInitialize #%lu this=%p source=%d callback=%p ENTER tid=%lu",
           n, self, source, callback, GetCurrentThreadId());
    HRESULT hr = InputInit_Original(self, source, callback);
    Wh_Log(L"[mouse-7-input] TabletModeInputHandler::RuntimeClassInitialize #%lu EXIT hr=0x%08X",
           n, (unsigned)hr);
    return hr;
}

void InputDown_Hook(void* self, unsigned int pointerId, POINT p) {
    const auto n = ++g_inputDown;
    Wh_Log(L"[mouse-7-input] TabletModeInputHandler::PointerDown #%lu this=%p pointer=%u pt=(%ld,%ld) tid=%lu",
           n, self, pointerId, p.x, p.y, GetCurrentThreadId());
    InputDown_Original(self, pointerId, p);
}

void InputUpdate_Hook(void* self, unsigned int pointerId, POINT p) {
    unsigned long n = 0;
    if (ShouldLogLimited(g_inputUpdate, &n)) {
        Wh_Log(L"[mouse-7-input] TabletModeInputHandler::PointerUpdate #%lu this=%p pointer=%u pt=(%ld,%ld) tid=%lu",
               n, self, pointerId, p.x, p.y, GetCurrentThreadId());
    }
    InputUpdate_Original(self, pointerId, p);
}

void InputUp_Hook(void* self, unsigned int pointerId, POINT p) {
    const auto n = ++g_inputUp;
    Wh_Log(L"[mouse-7-input] TabletModeInputHandler::PointerUp #%lu this=%p pointer=%u pt=(%ld,%ld) tid=%lu",
           n, self, pointerId, p.x, p.y, GetCurrentThreadId());
    InputUp_Original(self, pointerId, p);
}

HRESULT VmStartDrag_Hook(void* self, POINT p) {
    const auto n = ++g_vmStartDrag;
    Wh_Log(L"[mouse-7-input] TabletModeViewManager::StartDrag #%lu this=%p pt=(%ld,%ld) ENTER tid=%lu",
           n, self, p.x, p.y, GetCurrentThreadId());
    HRESULT hr = VmStartDrag_Original(self, p);
    Wh_Log(L"[mouse-7-input] TabletModeViewManager::StartDrag #%lu EXIT hr=0x%08X",
           n, (unsigned)hr);
    return hr;
}

HRESULT VmContinueDrag_Hook(void* self, POINT p) {
    unsigned long n = 0;
    if (ShouldLogLimited(g_vmContinueDrag, &n)) {
        Wh_Log(L"[mouse-7-input] TabletModeViewManager::ContinueDrag #%lu this=%p pt=(%ld,%ld) tid=%lu",
               n, self, p.x, p.y, GetCurrentThreadId());
    }
    return VmContinueDrag_Original(self, p);
}

HRESULT VmCommitDrag_Hook(void* self, POINT p) {
    const auto n = ++g_vmCommitDrag;
    Wh_Log(L"[mouse-7-input] TabletModeViewManager::CommitDrag #%lu this=%p pt=(%ld,%ld) ENTER tid=%lu",
           n, self, p.x, p.y, GetCurrentThreadId());
    HRESULT hr = VmCommitDrag_Original(self, p);
    Wh_Log(L"[mouse-7-input] TabletModeViewManager::CommitDrag #%lu EXIT hr=0x%08X",
           n, (unsigned)hr);
    return hr;
}

HRESULT VmCancelDrag_Hook(void* self) {
    const auto n = ++g_vmCancelDrag;
    Wh_Log(L"[mouse-7-input] TabletModeViewManager::CancelDrag #%lu this=%p ENTER tid=%lu",
           n, self, GetCurrentThreadId());
    HRESULT hr = VmCancelDrag_Original(self);
    Wh_Log(L"[mouse-7-input] TabletModeViewManager::CancelDrag #%lu EXIT hr=0x%08X",
           n, (unsigned)hr);
    return hr;
}

HRESULT VmShowAppResize_Hook(void* self, void* appView, int moveSizeType, POINT p) {
    const auto n = ++g_showAppResize;
    Wh_Log(L"[mouse-7-resize] TabletModeViewManager::ShowAppResizeView #%lu this=%p appView=%p moveSizeType=%d pt=(%ld,%ld) ENTER tid=%lu",
           n, self, appView, moveSizeType, p.x, p.y, GetCurrentThreadId());
    HRESULT hr = VmShowAppResize_Original(self, appView, moveSizeType, p);
    Wh_Log(L"[mouse-7-resize] TabletModeViewManager::ShowAppResizeView #%lu EXIT hr=0x%08X",
           n, (unsigned)hr);
    return hr;
}

HRESULT ProxyShowAppResize_Hook(void* self, void* appView, int moveSizeType, POINT p) {
    const auto n = ++g_proxyShowAppResize;
    Wh_Log(L"[mouse-7-resize] TabletModeViewManagerProxy::ShowAppResizeView #%lu this=%p appView=%p moveSizeType=%d pt=(%ld,%ld) ENTER tid=%lu",
           n, self, appView, moveSizeType, p.x, p.y, GetCurrentThreadId());
    HRESULT hr = ProxyShowAppResize_Original(self, appView, moveSizeType, p);
    Wh_Log(L"[mouse-7-resize] TabletModeViewManagerProxy::ShowAppResizeView #%lu EXIT hr=0x%08X",
           n, (unsigned)hr);
    return hr;
}

HRESULT VmShowArrangement_Hook(void* self, void* appView, void* appLayout, RECT* targetRect,
                               POINT* point, RECT* sourceRect, int mode, int source, void* inputSource) {
    const auto n = ++g_showArrangement;
    Wh_Log(L"[mouse-7-resize] TabletModeViewManager::ShowWindowArrangementView #%lu this=%p appView=%p layout=%p mode=%d source=%d inputSource=%p pointPtr=%p moveDepth=%lu positionerDepth=%lu ENTER tid=%lu",
           n, self, appView, appLayout, mode, source, inputSource, point,
           t_vmMoveSizeDepth, t_positionerMoveSizeDepth, GetCurrentThreadId());
    HRESULT hr = VmShowArrangement_Original(self, appView, appLayout, targetRect, point, sourceRect, mode, source, inputSource);
    Wh_Log(L"[mouse-7-resize] TabletModeViewManager::ShowWindowArrangementView #%lu EXIT hr=0x%08X",
           n, (unsigned)hr);
    return hr;
}

HRESULT InputCreate_Hook(int source, void* callback, const GUID* iid, void** out) {
    const auto n = ++g_inputCreate;
    Wh_Log(L"[mouse-7-resize] TabletModeInputHandler_CreateInstance #%lu source=%d callback=%p iid=%p ENTER tid=%lu",
           n, source, callback, iid, GetCurrentThreadId());
    HRESULT hr = InputCreate_Original(source, callback, iid, out);
    Wh_Log(L"[mouse-7-resize] TabletModeInputHandler_CreateInstance #%lu EXIT hr=0x%08X out=%p",
           n, (unsigned)hr, out ? *out : nullptr);
    return hr;
}

HRESULT VmMoveSizeAttempted_Hook(void* self, void* appView, int moveSizeType) {
    const auto n = ++g_vmMoveSizeAttempted;
    ++t_vmMoveSizeDepth;
    Wh_Log(L"[mouse-7-movesize] TabletModeViewManager::MoveSizeAttempted #%lu this=%p appView=%p type=%d positionerDepth=%lu ENTER tid=%lu",
           n, self, appView, moveSizeType, t_positionerMoveSizeDepth, GetCurrentThreadId());
    HRESULT hr = VmMoveSizeAttempted_Original(self, appView, moveSizeType);
    Wh_Log(L"[mouse-7-movesize] TabletModeViewManager::MoveSizeAttempted #%lu EXIT hr=0x%08X",
           n, (unsigned)hr);
    --t_vmMoveSizeDepth;
    return hr;
}

HRESULT ProxyMoveSizeAttempted_Hook(void* self, void* appView, int moveSizeType) {
    const auto n = ++g_proxyMoveSizeAttempted;
    Wh_Log(L"[mouse-7-movesize] TabletModeViewManagerProxy::MoveSizeAttempted #%lu this=%p appView=%p type=%d positionerDepth=%lu ENTER tid=%lu",
           n, self, appView, moveSizeType, t_positionerMoveSizeDepth, GetCurrentThreadId());
    HRESULT hr = ProxyMoveSizeAttempted_Original(self, appView, moveSizeType);
    Wh_Log(L"[mouse-7-movesize] TabletModeViewManagerProxy::MoveSizeAttempted #%lu EXIT hr=0x%08X",
           n, (unsigned)hr);
    return hr;
}

void PositionerMoveSizeWin10_Hook(void* self, void* appView, ULONG message) {
    const auto n = ++g_positionerMoveSizeAttempted;
    ++t_positionerMoveSizeDepth;
    Wh_Log(L"[mouse-7-movesize] TabletModePositionerManager::OnMoveSizeAttempted[Win10] #%lu this=%p appView=%p message=%lu ENTER tid=%lu",
           n, self, appView, message, GetCurrentThreadId());
    PositionerMoveSizeWin10_Original(self, appView, message);
    Wh_Log(L"[mouse-7-movesize] TabletModePositionerManager::OnMoveSizeAttempted[Win10] #%lu EXIT", n);
    --t_positionerMoveSizeDepth;
}

void PositionerMoveSizeWin11_Hook(void* self, HWND hwnd, void* appView, ULONG message, ULONG extra) {
    const auto n = ++g_positionerMoveSizeAttempted;
    ++t_positionerMoveSizeDepth;
    Wh_Log(L"[mouse-7-movesize] TabletModePositionerManager::OnMoveSizeAttempted[Win11] #%lu this=%p hwnd=%p appView=%p message=%lu extra=%lu ENTER tid=%lu",
           n, self, hwnd, appView, message, extra, GetCurrentThreadId());
    PositionerMoveSizeWin11_Original(self, hwnd, appView, message, extra);
    Wh_Log(L"[mouse-7-movesize] TabletModePositionerManager::OnMoveSizeAttempted[Win11] #%lu EXIT", n);
    --t_positionerMoveSizeDepth;
}


// Exact PDB type information from the Win10 1809 twinui.pdb identifies the
// first two fields of _SHELL_WINDOWMANAGEMENT_NOTIFY_INFO as:
//   +0x00 HWND hwnd
//   +0x08 SHELL_WINDOWMANAGEMENT_NOTIFY_MSG_ID msg
// Exact 25H2 static comparison:
// WindowManagementEvents::v_WndProc routes private message 0x342 directly to
// OnShellWindowManagementNotify. 0x341 is its adjacent private message.
// Both probes are read-only and call the original unchanged.
LRESULT WindowManagementWndProc_Hook(void* self, HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam) {
    if (msg == 0x341 || msg == 0x342) {
        const auto n = ++g_wmWndProcPrivate;
        Wh_Log(L"[mouse-7-wmnotify] WNDPROC_PRIVATE #%lu self=%p hwnd=%p msg=0x%04X wParam=0x%llX lParam=0x%llX left=%d capture=%p foreground=%p tid=%lu",
               n, self, hwnd, msg,
               (unsigned long long)wParam, (unsigned long long)lParam,
               (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
               GetCapture(), GetForegroundWindow(), GetCurrentThreadId());
    }
    return WindowManagementWndProc_Original(self, hwnd, msg, wParam, lParam);
}

void WindowManagementNotify_Hook(void* self, const void* info) {
    const auto n = ++g_wmNotify;
    HWND hwnd = nullptr;
    ULONG id = 0xFFFFFFFFu;
    unsigned long long raw10 = 0;
    unsigned long long raw18 = 0;
    if (info) {
        const auto* p = reinterpret_cast<const unsigned char*>(info);
        hwnd = *reinterpret_cast<HWND const*>(p + 0x00);
        id = *reinterpret_cast<ULONG const*>(p + 0x08);
        raw10 = *reinterpret_cast<unsigned long long const*>(p + 0x10);
        raw18 = *reinterpret_cast<unsigned long long const*>(p + 0x18);
        Wh_Log(L"[mouse-7-wmnotify] NOTIFY #%lu self=%p hwnd=%p id=%lu raw10=0x%llX raw18=0x%llX left=%d capture=%p foreground=%p tid=%lu",
               n, self, hwnd, id, raw10, raw18,
               (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
               GetCapture(), GetForegroundWindow(), GetCurrentThreadId());
    } else {
        Wh_Log(L"[mouse-7-wmnotify] NOTIFY #%lu self=%p info=NULL tid=%lu",
               n, self, GetCurrentThreadId());
    }
    WindowManagementNotify_Original(self, info);
}

HRESULT WindowManagementRegisterShowMoveSize_Hook(void* self, void* sink) {
    const auto n = ++g_wmRegisterShowMoveSize;
    Wh_Log(L"[mouse-7-wmnotify] RegisterShowMoveSize #%lu self=%p sink=%p ENTER tid=%lu",
           n, self, sink, GetCurrentThreadId());
    HRESULT hr = WindowManagementRegisterShowMoveSize_Original(self, sink);
    Wh_Log(L"[mouse-7-wmnotify] RegisterShowMoveSize #%lu EXIT hr=0x%08X",
           n, (unsigned)hr);
    return hr;
}

size_t InstallWindowManagementNotifyHooks(HMODULE module) {
    HookSpec hooks[] = {
        {L"?v_WndProc@WindowManagementEvents@@EEAA_JPEAUHWND__@@I_K_J@Z",
         (void*)WindowManagementWndProc_Hook, (void**)&WindowManagementWndProc_Original,
         L"WindowManagementEvents::v_WndProc", false},
        {L"?OnShellWindowManagementNotify@WindowManagementEvents@@AEAAXQEBU_SHELL_WINDOWMANAGEMENT_NOTIFY_INFO@@@Z",
         (void*)WindowManagementNotify_Hook, (void**)&WindowManagementNotify_Original,
         L"WindowManagementEvents::OnShellWindowManagementNotify", false},
        {L"?RegisterShowMoveSize@WindowManagementEvents@@UEAAJPEAUIWindowManagementShowMoveSizeEvents@@@Z",
         (void*)WindowManagementRegisterShowMoveSize_Hook,
         (void**)&WindowManagementRegisterShowMoveSize_Original,
         L"WindowManagementEvents::RegisterShowMoveSize", false},
    };

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;
    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, &options, &symbol);
    if (!search) {
        Wh_Log(L"[mouse-7-wmnotify] symbol enumeration failed");
        return 0;
    }

    size_t installed = 0;
    do {
        const wchar_t* d = symbol.symbolDecorated;
        if (!d || !*d) d = symbol.symbol;
        if (!d || !*d) continue;
        for (auto& h : hooks) {
            if (h.found || wcscmp(d, h.decorated) != 0) continue;
            h.found = true;
            if (Wh_SetFunctionHook(symbol.address, h.hook, h.original)) {
                ++installed;
                Wh_Log(L"[mouse-7-wmnotify] hooked %s at %p", h.name, symbol.address);
            } else {
                Wh_Log(L"[mouse-7-wmnotify] FAILED hook %s", h.name);
            }
            break;
        }
    } while (Wh_FindNextSymbol(search, &symbol));
    Wh_FindCloseSymbol(search);
    g_wmHooksInstalled.store((unsigned long)installed, std::memory_order_relaxed);
    Wh_Log(L"[mouse-7-wmnotify] installed %llu/%llu exact read-only hooks",
           (unsigned long long)installed,
           (unsigned long long)ARRAYSIZE(hooks));
    return installed;
}


// 25H2 adds a per-HWND shell-window-management policy surface to
// IPrivilegedArrangementOperations. Exact PDB signature:
//   int CPrivilegedArrangementOperations::EnableShellWindowManagementBehavior(
//       HWND, enum mask, enum features)
// GamingPosturePositioner::SetInterceptMoveAndSize uses this exact interface
// with mask=0x6 and features=0x6/0. This diagnostic only observes native calls.
int PerWindowShellBehavior_Hook(void* self, HWND hwnd, int mask, int features) {
    const auto n = ++g_perWindowShellBehavior;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    wchar_t cls[128]{};
    wchar_t title[160]{};
    if (hwnd) {
        GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
        GetWindowTextW(hwnd, title, ARRAYSIZE(title));
    }
    Wh_Log(L"[mouse-7-windowbehavior] PER_WINDOW #%lu self=%p hwnd=%p pid=%lu class='%s' title='%s' mask=0x%08X features=0x%08X ENTER tid=%lu",
           n, self, hwnd, pid, cls, title, (unsigned)mask, (unsigned)features,
           GetCurrentThreadId());
    const int result = PerWindowShellBehavior_Original(self, hwnd, mask, features);
    Wh_Log(L"[mouse-7-windowbehavior] PER_WINDOW #%lu EXIT result=0x%08X",
           n, (unsigned)result);
    return result;
}

size_t InstallPerWindowShellBehaviorHook(HMODULE module) {
    if (g_build != BuildKind::Win11_25H2) {
        return 0;
    }

    constexpr const wchar_t* kDecorated =
        L"?EnableShellWindowManagementBehavior@CPrivilegedArrangementOperations@@EEAAHPEAUHWND__@@W4__MIDL___MIDL_itf_privilegedoperations_0000_0004_0001@@1@Z";

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;
    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, &options, &symbol);
    if (!search) {
        Wh_Log(L"[mouse-7-windowbehavior] symbol enumeration failed");
        return 0;
    }

    size_t installed = 0;
    do {
        const wchar_t* d = symbol.symbolDecorated;
        if (!d || !*d) d = symbol.symbol;
        if (!d || wcscmp(d, kDecorated) != 0) continue;
        if (Wh_SetFunctionHook(symbol.address,
                               (void*)PerWindowShellBehavior_Hook,
                               (void**)&PerWindowShellBehavior_Original)) {
            installed = 1;
            Wh_Log(L"[mouse-7-windowbehavior] hooked CPrivilegedArrangementOperations::EnableShellWindowManagementBehavior at %p",
                   symbol.address);
        } else {
            Wh_Log(L"[mouse-7-windowbehavior] FAILED hook CPrivilegedArrangementOperations::EnableShellWindowManagementBehavior");
        }
        break;
    } while (Wh_FindNextSymbol(search, &symbol));
    Wh_FindCloseSymbol(search);

    g_perWindowShellBehaviorHooksInstalled.store((unsigned long)installed,
                                                  std::memory_order_relaxed);
    Wh_Log(L"[mouse-7-windowbehavior] installed %llu/1 exact read-only per-window behavior hook",
           (unsigned long long)installed);
    return installed;
}

// v1.0.16: fresh Explorer commonly loads twinui.pcshell.dll after MOUSE-7.
// Queue this exact read-only hook once the module appears, then apply it from the
// existing watcher thread. No private object is created and no policy is changed.
void TryInstallDeferredPerWindowShellBehaviorHook() {
    if (g_build != BuildKind::Win11_25H2 ||
        g_perWindowShellBehaviorHooksInstalled.load(std::memory_order_acquire) != 0 ||
        g_deferredPcshellProbeAttempted.load(std::memory_order_acquire)) {
        return;
    }

    HMODULE pcshell = GetModuleHandleW(L"twinui.pcshell.dll");
    if (!pcshell) return;

    bool expected = false;
    if (!g_deferredPcshellProbeAttempted.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return;
    }

    Wh_Log(L"[mouse-7-windowbehavior] twinui.pcshell.dll appeared at %p; attempting deferred exact hook",
           pcshell);
    const size_t installed = InstallPerWindowShellBehaviorHook(pcshell);
    if (!installed) {
        Wh_Log(L"[mouse-7-windowbehavior] deferred exact hook was not queued");
        return;
    }

    if (!Wh_ApplyHookOperations()) {
        Wh_Log(L"[mouse-7-windowbehavior] Wh_ApplyHookOperations failed for deferred per-window hook");
        return;
    }

    Wh_Log(L"[mouse-7-windowbehavior] deferred per-window hook ACTIVE original=%p",
           PerWindowShellBehavior_Original);
}

size_t InstallMoveSizeHandoffHooks(HMODULE module) {
    const wchar_t* positionerDecorated = nullptr;
    void* positionerHook = nullptr;
    void** positionerOriginal = nullptr;

    if (g_build == BuildKind::Win10) {
        positionerDecorated =
            L"?OnMoveSizeAttempted@TabletModePositionerManager@@UEAAXPEAUIApplicationView@@K@Z";
        positionerHook = (void*)PositionerMoveSizeWin10_Hook;
        positionerOriginal = (void**)&PositionerMoveSizeWin10_Original;
    } else if (g_build == BuildKind::Win11_25H2) {
        positionerDecorated =
            L"?OnMoveSizeAttempted@TabletModePositionerManager@@UEAAXPEAUHWND__@@PEAUIApplicationView@@KK@Z";
        positionerHook = (void*)PositionerMoveSizeWin11_Hook;
        positionerOriginal = (void**)&PositionerMoveSizeWin11_Original;
    } else {
        return 0;
    }

    HookSpec hooks[] = {
        {L"?MoveSizeAttempted@TabletModeViewManager@@UEAAJPEAUIApplicationView@@W4MOVE_SIZE_TYPE@@@Z",
         (void*)VmMoveSizeAttempted_Hook, (void**)&VmMoveSizeAttempted_Original,
         L"TabletModeViewManager::MoveSizeAttempted", false},
        {L"?MoveSizeAttempted@TabletModeViewManagerProxy@@UEAAJPEAUIApplicationView@@W4MOVE_SIZE_TYPE@@@Z",
         (void*)ProxyMoveSizeAttempted_Hook, (void**)&ProxyMoveSizeAttempted_Original,
         L"TabletModeViewManagerProxy::MoveSizeAttempted", false},
        {positionerDecorated, positionerHook, positionerOriginal,
         L"TabletModePositionerManager::OnMoveSizeAttempted", false},
    };

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;
    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, &options, &symbol);
    if (!search) {
        Wh_Log(L"[mouse-7-movesize] symbol enumeration failed");
        return 0;
    }

    size_t installed = 0;
    do {
        const wchar_t* d = symbol.symbolDecorated;
        if (!d || !*d) d = symbol.symbol;
        if (!d || !*d) continue;
        for (auto& h : hooks) {
            if (h.found || wcscmp(d, h.decorated) != 0) continue;
            h.found = true;
            if (Wh_SetFunctionHook(symbol.address, h.hook, h.original)) {
                ++installed;
                Wh_Log(L"[mouse-7-movesize] hooked %s at %p", h.name, symbol.address);
            } else {
                Wh_Log(L"[mouse-7-movesize] FAILED hook %s", h.name);
            }
            break;
        }
    } while (Wh_FindNextSymbol(search, &symbol));
    Wh_FindCloseSymbol(search);
    Wh_Log(L"[mouse-7-movesize] installed %llu/%llu hooks",
           (unsigned long long)installed,
           (unsigned long long)ARRAYSIZE(hooks));
    return installed;
}

size_t InstallResizeHandoffHooks(HMODULE module) {
    HookSpec hooks[] = {
        {L"?ShowAppResizeView@TabletModeViewManager@@UEAAJPEAUIApplicationView@@W4MOVE_SIZE_TYPE@@UtagPOINT@@@Z",
         (void*)VmShowAppResize_Hook, (void**)&VmShowAppResize_Original,
         L"TabletModeViewManager::ShowAppResizeView", false},
        {L"?ShowAppResizeView@TabletModeViewManagerProxy@@UEAAJPEAUIApplicationView@@W4MOVE_SIZE_TYPE@@UtagPOINT@@@Z",
         (void*)ProxyShowAppResize_Hook, (void**)&ProxyShowAppResize_Original,
         L"TabletModeViewManagerProxy::ShowAppResizeView", false},
        {L"?ShowWindowArrangementView@TabletModeViewManager@@AEAAJPEAUIApplicationView@@PEAUIAppLayout@@PEAUtagRECT@@PEAUtagPOINT@@2W4SHOW_WINDOW_ARRANGEMENT_VIEW_MODE@@W4SHOW_WINDOW_ARRANGEMENT_VIEW_SOURCE@@PEAUITabletModeInputSource@@@Z",
         (void*)VmShowArrangement_Hook, (void**)&VmShowArrangement_Original,
         L"TabletModeViewManager::ShowWindowArrangementView", false},
        {L"?TabletModeInputHandler_CreateInstance@@YAJW4SHOW_WINDOW_ARRANGEMENT_VIEW_SOURCE@@PEAUITabletModeInputCallback@@AEBU_GUID@@PEAPEAX@Z",
         (void*)InputCreate_Hook, (void**)&InputCreate_Original,
         L"TabletModeInputHandler_CreateInstance", false},
    };

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;
    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, &options, &symbol);
    if (!search) {
        Wh_Log(L"[mouse-7-resize] symbol enumeration failed");
        return 0;
    }

    size_t installed = 0;
    do {
        const wchar_t* d = symbol.symbolDecorated;
        if (!d || !*d) d = symbol.symbol;
        if (!d || !*d) continue;
        for (auto& h : hooks) {
            if (h.found || wcscmp(d, h.decorated) != 0) continue;
            h.found = true;
            if (Wh_SetFunctionHook(symbol.address, h.hook, h.original)) {
                ++installed;
                Wh_Log(L"[mouse-7-resize] hooked %s at %p", h.name, symbol.address);
            } else {
                Wh_Log(L"[mouse-7-resize] FAILED hook %s", h.name);
            }
            break;
        }
    } while (Wh_FindNextSymbol(search, &symbol));
    Wh_FindCloseSymbol(search);
    Wh_Log(L"[mouse-7-resize] installed %llu/%llu hooks",
           (unsigned long long)installed,
           (unsigned long long)ARRAYSIZE(hooks));
    return installed;
}

size_t InstallInputHandlerHooks(HMODULE module) {
    HookSpec hooks[] = {
        {L"?RuntimeClassInitialize@TabletModeInputHandler@@QEAAJW4SHOW_WINDOW_ARRANGEMENT_VIEW_SOURCE@@PEAUITabletModeInputCallback@@@Z",
         (void*)InputInit_Hook, (void**)&InputInit_Original,
         L"TabletModeInputHandler::RuntimeClassInitialize", false},
        {L"?PointerDown@TabletModeInputHandler@@UEAAXIUtagPOINT@@@Z",
         (void*)InputDown_Hook, (void**)&InputDown_Original,
         L"TabletModeInputHandler::PointerDown", false},
        {L"?PointerUpdate@TabletModeInputHandler@@UEAAXIUtagPOINT@@@Z",
         (void*)InputUpdate_Hook, (void**)&InputUpdate_Original,
         L"TabletModeInputHandler::PointerUpdate", false},
        {L"?PointerUp@TabletModeInputHandler@@UEAAXIUtagPOINT@@@Z",
         (void*)InputUp_Hook, (void**)&InputUp_Original,
         L"TabletModeInputHandler::PointerUp", false},
        {L"?StartDrag@TabletModeViewManager@@UEAAJUtagPOINT@@@Z",
         (void*)VmStartDrag_Hook, (void**)&VmStartDrag_Original,
         L"TabletModeViewManager::StartDrag", false},
        {L"?ContinueDrag@TabletModeViewManager@@UEAAJUtagPOINT@@@Z",
         (void*)VmContinueDrag_Hook, (void**)&VmContinueDrag_Original,
         L"TabletModeViewManager::ContinueDrag", false},
        {L"?CommitDrag@TabletModeViewManager@@UEAAJUtagPOINT@@@Z",
         (void*)VmCommitDrag_Hook, (void**)&VmCommitDrag_Original,
         L"TabletModeViewManager::CommitDrag", false},
        {L"?CancelDrag@TabletModeViewManager@@UEAAJXZ",
         (void*)VmCancelDrag_Hook, (void**)&VmCancelDrag_Original,
         L"TabletModeViewManager::CancelDrag", false},
    };

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;
    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, &options, &symbol);
    if (!search) {
        Wh_Log(L"[mouse-7-input] symbol enumeration failed");
        return 0;
    }

    size_t installed = 0;
    do {
        const wchar_t* d = symbol.symbolDecorated;
        if (!d || !*d) d = symbol.symbol;
        if (!d || !*d) continue;
        for (auto& h : hooks) {
            if (h.found || wcscmp(d, h.decorated) != 0) continue;
            h.found = true;
            if (Wh_SetFunctionHook(symbol.address, h.hook, h.original)) {
                ++installed;
                Wh_Log(L"[mouse-7-input] hooked %s at %p", h.name, symbol.address);
            } else {
                Wh_Log(L"[mouse-7-input] FAILED hook %s", h.name);
            }
            break;
        }
    } while (Wh_FindNextSymbol(search, &symbol));
    Wh_FindCloseSymbol(search);
    Wh_Log(L"[mouse-7-input] installed %llu/%llu hooks",
           (unsigned long long)installed,
           (unsigned long long)ARRAYSIZE(hooks));
    return installed;
}


size_t InstallTitleHitProbe(HMODULE module) {
    HookSpec hooks[] = {
        {L"?v_HitCornerOrEdge@CTitleBarInvoker@@MEAAJUtagPOINT@@_N@Z",
         (void*)TitleHit_Hook, (void**)&TitleHit_Original,
         L"CTitleBarInvoker::v_HitCornerOrEdge", false},
        {L"?_IsPointOverTitleBarUI@CTitleBarInvoker@@AEAA_NUtagPOINT@@@Z",
         (void*)TitleIsOver_Hook, (void**)&TitleIsOver_Original,
         L"CTitleBarInvoker::_IsPointOverTitleBarUI", false},
    };

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;
    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, &options, &symbol);
    if (!search) {
        Wh_Log(L"[mouse-7-titlehit] symbol enumeration failed");
        return 0;
    }

    size_t installed = 0;
    do {
        const wchar_t* d = symbol.symbolDecorated;
        if (!d || !*d) d = symbol.symbol;
        if (!d || !*d) continue;
        for (auto& h : hooks) {
            if (h.found || wcscmp(d, h.decorated) != 0) continue;
            h.found = true;
            if (Wh_SetFunctionHook(symbol.address, h.hook, h.original)) {
                ++installed;
                Wh_Log(L"[mouse-7-titlehit] hooked %s at %p", h.name, symbol.address);
            } else {
                Wh_Log(L"[mouse-7-titlehit] FAILED hook %s", h.name);
            }
            break;
        }
    } while (Wh_FindNextSymbol(search, &symbol));
    Wh_FindCloseSymbol(search);
    Wh_Log(L"[mouse-7-titlehit] installed %llu/%llu hooks",
           (unsigned long long)installed,
           (unsigned long long)ARRAYSIZE(hooks));
    return installed;
}

size_t InstallHooks(HMODULE module) {
    HookSpec hooks[] = {
        {L"?_AcquireMouseInvokerForEdge@CEdgeUiManager@@AEAAJW4EDGEUI_INDEX@@PEAPEAUIEdgeUiMouseInvocation@@@Z", (void*)Acquire_Hook, (void**)&Acquire_Original, L"CEdgeUiManager::_AcquireMouseInvokerForEdge", false},
        {L"?InputObserveStart@CEdgeUiManager@@UEAAJPEAUIEdgeUiInput@@@Z", (void*)ObserveStart_Hook, (void**)&ObserveStart_Original, L"CEdgeUiManager::InputObserveStart", false},
        {L"?_RegisterRawInput@CEdgeUiInput@@AEAAX_N@Z", (void*)RegisterRaw_Hook, (void**)&RegisterRaw_Original, L"CEdgeUiInput::_RegisterRawInput", false},
        {L"?ObservedMouseButtonDown@CEdgeUiInput@@UEAAJUtagPOINT@@W4RAW_INPUT_MOUSE_BUTTON@@@Z", (void*)UiDown_Hook, (void**)&UiDown_Original, L"CEdgeUiInput::ObservedMouseButtonDown", false},
        {L"?ObservedMouseMove@CEdgeUiInput@@UEAAJUtagPOINT@@G0@Z", (void*)UiMove_Hook, (void**)&UiMove_Original, L"CEdgeUiInput::ObservedMouseMove", false},
        {L"?_HandleObservedMouseInput@CEdgeUiInput@@AEAAX_NUtagPOINT@@1@Z", (void*)Handle_Hook, (void**)&Handle_Original, L"CEdgeUiInput::_HandleObservedMouseInput", false},
        {L"?ObservedMouseButtonDown@CEdgeInvoker@@UEAAJUtagPOINT@@@Z", (void*)InvokerDown_Hook, (void**)&InvokerDown_Original, L"CEdgeInvoker::ObservedMouseButtonDown", false},
        {L"?ObservedMouseMove@CEdgeInvoker@@UEAAJUtagPOINT@@0@Z", (void*)InvokerMove_Hook, (void**)&InvokerMove_Original, L"CEdgeInvoker::ObservedMouseMove", false},
        {L"?StartDrag@CEdgeInvoker@@UEAAJUtagPOINT@@@Z", (void*)InvokerStart_Hook, (void**)&InvokerStart_Original, L"CEdgeInvoker::StartDrag", false},
        {L"?MouseDragStart@CEdgeUiManager@@UEAAJPEAUIEdgeUiInput@@PEAPEAUIEdgeUiMouseInvocation@@@Z", (void*)ManagerStart_Hook, (void**)&ManagerStart_Original, L"CEdgeUiManager::MouseDragStart", false},
        {L"?_WndProc@CEdgeUiInput@@AEAA_JI_K_J_N@Z", (void*)WndProc_Hook, (void**)&WndProc_Original, L"CEdgeUiInput::_WndProc", false},
        {L"?_IsCurrentInputFromMouseOrPen@CEdgeUiInput@@AEAAHXZ", (void*)IsMousePen_Hook, (void**)&IsMousePen_Original, L"CEdgeUiInput::_IsCurrentInputFromMouseOrPen", false},
        {L"?_OnMouseDown@CEdgeUiInput@@AEAAXUtagPOINT@@_N@Z", (void*)OnMouseDown_Hook, (void**)&OnMouseDown_Original, L"CEdgeUiInput::_OnMouseDown", false},
        {L"?_OnMouseMove@CEdgeUiInput@@AEAAXUtagPOINT@@_N1@Z", (void*)OnMouseMove_Hook, (void**)&OnMouseMove_Original, L"CEdgeUiInput::_OnMouseMove", false},
        {L"?_MouseMoveToCornerOrEdge@CEdgeUiInput@@AEAAJW4EDGEUI_CORNEROREDGE@@UtagPOINT@@_N22@Z", (void*)MoveToEdge_Hook, (void**)&MoveToEdge_Original, L"CEdgeUiInput::_MouseMoveToCornerOrEdge", false},
        {L"?MouseHitCornerOrEdge@CEdgeUiManager@@UEAAJPEAUIEdgeUiInput@@_N1UtagPOINT@@PEA_NPEAPEAUIEdgeUiMouseInvocation@@@Z", (void*)ManagerHit_Hook, (void**)&ManagerHit_Original, L"CEdgeUiManager::MouseHitCornerOrEdge", false},
        {L"?HitCornerOrEdge@CEdgeInvoker@@UEAAJW4EDGEUI_CORNEROREDGE@@UtagPOINT@@PEAW4EDGEUI_INPUTNOTIFICATION@@H@Z", (void*)InvokerHit_Hook, (void**)&InvokerHit_Original, L"CEdgeInvoker::HitCornerOrEdge", false},
        {L"?v_ObservedMouseButtonDown@CTitleBarInvoker@@MEAAXUtagPOINT@@@Z", (void*)TitleDown_Hook, (void**)&TitleDown_Original, L"CTitleBarInvoker::v_ObservedMouseButtonDown", false},
        {L"?v_ObservedMouseMove@CTitleBarInvoker@@MEAAXUtagPOINT@@@Z", (void*)TitleMove_Hook, (void**)&TitleMove_Original, L"CTitleBarInvoker::v_ObservedMouseMove", false},
        {L"?v_StartDrag@CTitleBarInvoker@@MEAAJUtagPOINT@@@Z", (void*)TitleStart_Hook, (void**)&TitleStart_Original, L"CTitleBarInvoker::v_StartDrag", false},
        {L"?v_Invoke@CTitleBarInvoker@@MEAAJ_NUtagPOINT@@W4RAW_INPUT_TYPE@@@Z", (void*)TitleInvoke_Hook, (void**)&TitleInvoke_Original, L"CTitleBarInvoker::v_Invoke", false},
    };

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;
    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, &options, &symbol);
    if (!search) {
        Wh_Log(L"[mouse-7-route] symbol enumeration failed");
        return 0;
    }
    size_t installed = 0;
    do {
        const wchar_t* d = symbol.symbolDecorated;
        if (!d || !*d) d = symbol.symbol;
        if (!d || !*d) continue;
        for (auto& h : hooks) {
            if (h.found || wcscmp(d, h.decorated) != 0) continue;
            h.found = true;
            if (Wh_SetFunctionHook(symbol.address, h.hook, h.original)) {
                ++installed;
                Wh_Log(L"[mouse-7-route] hooked %s at %p", h.name, symbol.address);
            } else {
                Wh_Log(L"[mouse-7-route] FAILED hook %s", h.name);
            }
            break;
        }
    } while (Wh_FindNextSymbol(search, &symbol));
    Wh_FindCloseSymbol(search);
    Wh_Log(L"[mouse-7-route] installed %llu/%llu hooks", (unsigned long long)installed, (unsigned long long)ARRAYSIZE(hooks));
    return installed;
}


// -----------------------------------------------------------------------------
// CEdgeUiInput visibility/lifecycle probe
// -----------------------------------------------------------------------------

using SetVisible_t = HRESULT(__cdecl*)(void*, bool);
using GetListenerHwnd_t = HWND(__cdecl*)(void*);

SetVisible_t g_SetVisible_Original = nullptr;
GetListenerHwnd_t g_GetListenerHwnd = nullptr;
std::atomic<unsigned long> g_setVisibleCalls{0};
std::atomic<unsigned long> g_visStacks{0};

bool IsTopEdgeListener(HWND hwnd) {
    if (!hwnd) return false;
    wchar_t cls[96] = {};
    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    return lstrcmpW(cls, L"EdgeUiInputTopWndClass") == 0;
}

void LogVisStackLimited() {
    const unsigned long n =
        g_visStacks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n > 8) return;

    void* frames[14] = {};
    const USHORT count =
        RtlCaptureStackBackTrace(1, ARRAYSIZE(frames), frames, nullptr);
    Wh_Log(L"[mouse-7-vis] STACK #%lu frames=%u", n, (unsigned)count);

    for (USHORT i = 0; i < count; ++i) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(frames[i], &mbi, sizeof(mbi)) ||
            !mbi.AllocationBase) {
            Wh_Log(L"[mouse-7-vis]   #%u addr=%p",
                   (unsigned)i, frames[i]);
            continue;
        }

        HMODULE mod = reinterpret_cast<HMODULE>(mbi.AllocationBase);
        wchar_t path[MAX_PATH] = {};
        if (!GetModuleFileNameW(mod, path, ARRAYSIZE(path))) {
            Wh_Log(L"[mouse-7-vis]   #%u addr=%p module=%p",
                   (unsigned)i, frames[i], mod);
            continue;
        }

        wchar_t* base = path;
        for (wchar_t* p = path; *p; ++p) {
            if (*p == L'\\' || *p == L'/') base = p + 1;
        }

        const ULONG_PTR rva =
            reinterpret_cast<BYTE*>(frames[i]) -
            reinterpret_cast<BYTE*>(mod);
        Wh_Log(L"[mouse-7-vis]   #%u %s+0x%llX addr=%p",
               (unsigned)i, base,
               (unsigned long long)rva, frames[i]);
    }
}

void LogListenerState(const wchar_t* phase, void* self, HWND hwnd,
                      bool requestedVisible, HRESULT hr) {
    if (!hwnd) {
        Wh_Log(L"[mouse-7-vis] %s this=%p requested=%d hwnd=NULL hr=0x%08X tid=%lu",
               phase, self, requestedVisible ? 1 : 0,
               (unsigned)hr, GetCurrentThreadId());
        return;
    }

    wchar_t cls[96] = {};
    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    RECT rc{};
    GetWindowRect(hwnd, &rc);
    POINT cursor{};
    GetCursorPos(&cursor);
    HWND under = WindowFromPoint(cursor);
    const auto style =
        static_cast<unsigned long long>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const auto ex =
        static_cast<unsigned long long>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));

    Wh_Log(L"[mouse-7-vis] %s this=%p requested=%d hwnd=%p class='%s' rect=(%ld,%ld)-(%ld,%ld) actualVis=%d style=0x%llX ex=0x%llX cursor=(%ld,%ld) WindowFromPoint=%p hr=0x%08X tid=%lu",
           phase, self, requestedVisible ? 1 : 0, hwnd, cls,
           rc.left, rc.top, rc.right, rc.bottom,
           IsWindowVisible(hwnd) ? 1 : 0,
           style, ex, cursor.x, cursor.y, under,
           (unsigned)hr, GetCurrentThreadId());
}

HRESULT SetVisible_Hook(void* self, bool visible) {
    const unsigned long n =
        g_setVisibleCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    HWND beforeHwnd =
        g_GetListenerHwnd ? g_GetListenerHwnd(self) : nullptr;
    const bool top = IsTopEdgeListener(beforeHwnd);

    if (top) {
        Wh_Log(L"[mouse-7-vis] SetVisible #%lu ENTER top=1",
               n);
        LogListenerState(L"BEFORE", self, beforeHwnd, visible, S_OK);
        LogVisStackLimited();
    }

    const HRESULT hr = g_SetVisible_Original(self, visible);

    HWND afterHwnd =
        g_GetListenerHwnd ? g_GetListenerHwnd(self) : beforeHwnd;
    if (top || IsTopEdgeListener(afterHwnd)) {
        LogListenerState(L"AFTER", self, afterHwnd, visible, hr);
        Wh_Log(L"[mouse-7-vis] SetVisible #%lu EXIT hr=0x%08X",
               n, (unsigned)hr);
    }

    return hr;
}

bool InstallVisibilityProbe(HMODULE twinui) {
    constexpr const wchar_t* kSetVisible =
        L"?SetVisible@CEdgeUiInput@@UEAAJ_N@Z";
    constexpr const wchar_t* kGetListener =
        L"?GetListenerHwnd@CEdgeUiInput@@UEAAPEAUHWND__@@XZ";

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;

    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(twinui, &options, &symbol);
    if (!search) {
        Wh_Log(L"[mouse-7-vis] symbol enumeration failed");
        return false;
    }

    void* setVisibleAddress = nullptr;
    void* getListenerAddress = nullptr;

    do {
        const wchar_t* d = symbol.symbolDecorated;
        if (!d || !*d) d = symbol.symbol;
        if (!d || !*d) continue;

        if (!setVisibleAddress && wcscmp(d, kSetVisible) == 0) {
            setVisibleAddress = symbol.address;
        } else if (!getListenerAddress && wcscmp(d, kGetListener) == 0) {
            getListenerAddress = symbol.address;
        }

        if (setVisibleAddress && getListenerAddress) break;
    } while (Wh_FindNextSymbol(search, &symbol));

    Wh_FindCloseSymbol(search);

    if (!setVisibleAddress || !getListenerAddress) {
        Wh_Log(L"[mouse-7-vis] missing symbol(s): SetVisible=%p GetListenerHwnd=%p",
               setVisibleAddress, getListenerAddress);
        return false;
    }

    g_GetListenerHwnd =
        reinterpret_cast<GetListenerHwnd_t>(getListenerAddress);

    if (!Wh_SetFunctionHook(
            setVisibleAddress,
            reinterpret_cast<void*>(&SetVisible_Hook),
            reinterpret_cast<void**>(&g_SetVisible_Original))) {
        Wh_Log(L"[mouse-7-vis] FAILED hook CEdgeUiInput::SetVisible at %p",
               setVisibleAddress);
        return false;
    }

    Wh_Log(L"[mouse-7-vis] hooked CEdgeUiInput::SetVisible at %p; GetListenerHwnd=%p",
           setVisibleAddress, getListenerAddress);
    return true;
}


// -----------------------------------------------------------------------------
// s_WndProc dispatch probe (formerly MOUSE-8)
// -----------------------------------------------------------------------------

using SWndProc_t = LRESULT(__cdecl*)(HWND, UINT, UINT_PTR, LONG_PTR);
SWndProc_t g_sWndProc_Original = nullptr;

std::atomic<unsigned long> g_sWndCalls{0};
std::atomic<unsigned long> g_sWndTopMoves{0};
std::atomic<unsigned long> g_sWndHeldMoves{0};
std::atomic<unsigned long> g_sWndButtonMsgs{0};
std::atomic<unsigned long> g_sWndStacks{0};

const wchar_t* SWndMsgName(UINT msg) {
    switch (msg) {
        case WM_MOUSEMOVE: return L"WM_MOUSEMOVE";
        case WM_LBUTTONDOWN: return L"WM_LBUTTONDOWN";
        case WM_LBUTTONUP: return L"WM_LBUTTONUP";
        case WM_NCMOUSEMOVE: return L"WM_NCMOUSEMOVE";
        case WM_NCLBUTTONDOWN: return L"WM_NCLBUTTONDOWN";
        case WM_NCLBUTTONUP: return L"WM_NCLBUTTONUP";
        case WM_SHOWWINDOW: return L"WM_SHOWWINDOW";
        case WM_WINDOWPOSCHANGING: return L"WM_WINDOWPOSCHANGING";
        case WM_WINDOWPOSCHANGED: return L"WM_WINDOWPOSCHANGED";
        case WM_STYLECHANGING: return L"WM_STYLECHANGING";
        case WM_STYLECHANGED: return L"WM_STYLECHANGED";
        default: return L"other";
    }
}

void BaseNameInPlace(wchar_t* s) {
    if (!s) return;
    wchar_t* last = s;
    for (wchar_t* p = s; *p; ++p) {
        if (*p == L'\\' || *p == L'/') last = p + 1;
    }
    if (last != s) {
        MoveMemory(s, last, (wcslen(last) + 1) * sizeof(wchar_t));
    }
}

void LogSWndStackLimited() {
    const unsigned long n =
        g_sWndStacks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n > 6) return;

    void* frames[16] = {};
    const USHORT count =
        RtlCaptureStackBackTrace(1, ARRAYSIZE(frames), frames, nullptr);
    Wh_Log(L"[mouse-7-dispatch] STACK #%lu frames=%u", n, (unsigned)count);

    for (USHORT i = 0; i < count; ++i) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(frames[i], &mbi, sizeof(mbi)) ||
            !mbi.AllocationBase) {
            Wh_Log(L"[mouse-7-dispatch]   #%u addr=%p",
                   (unsigned)i, frames[i]);
            continue;
        }

        HMODULE mod = reinterpret_cast<HMODULE>(mbi.AllocationBase);
        wchar_t path[MAX_PATH] = {};
        if (!GetModuleFileNameW(mod, path, ARRAYSIZE(path))) {
            Wh_Log(L"[mouse-7-dispatch]   #%u addr=%p module=%p",
                   (unsigned)i, frames[i], mod);
            continue;
        }

        BaseNameInPlace(path);
        const ULONG_PTR rva =
            reinterpret_cast<BYTE*>(frames[i]) -
            reinterpret_cast<BYTE*>(mod);
        Wh_Log(L"[mouse-7-dispatch]   #%u %s+0x%llX addr=%p",
               (unsigned)i, path,
               (unsigned long long)rva, frames[i]);
    }
}

LRESULT SWndProc_Hook(HWND hwnd, UINT msg, UINT_PTR wParam, LONG_PTR lParam) {
    g_sWndCalls.fetch_add(1, std::memory_order_relaxed);

    POINT cursor{};
    GetCursorPos(&cursor);

    wchar_t cls[96] = {};
    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    const bool topClass =
        lstrcmpW(cls, L"EdgeUiInputTopWndClass") == 0;

    const bool topMove = msg == WM_MOUSEMOVE && cursor.y <= 4;
    const bool heldMove =
        msg == WM_MOUSEMOVE && ((wParam & MK_LBUTTON) != 0);
    const bool button =
        msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
        msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONUP;
    const bool lifecycle =
        topClass &&
        (msg == WM_SHOWWINDOW ||
         msg == WM_WINDOWPOSCHANGING ||
         msg == WM_WINDOWPOSCHANGED ||
         msg == WM_STYLECHANGING ||
         msg == WM_STYLECHANGED);

    bool shouldLog = false;
    if (topMove) {
        const unsigned long n =
            g_sWndTopMoves.fetch_add(1, std::memory_order_relaxed) + 1;
        shouldLog = n <= 40 || (n % 100) == 0;
    }
    if (heldMove) {
        const unsigned long n =
            g_sWndHeldMoves.fetch_add(1, std::memory_order_relaxed) + 1;
        shouldLog = shouldLog || n <= 120 || (n % 100) == 0;
    }
    if (button) {
        g_sWndButtonMsgs.fetch_add(1, std::memory_order_relaxed);
        shouldLog = true;
    }
    if (lifecycle) {
        shouldLog = true;
    }

    bool preVis = false;
    HWND preUnder = nullptr;
    LONG_PTR preStyle = 0;

    if (shouldLog) {
        RECT rc{};
        GetWindowRect(hwnd, &rc);
        preUnder = WindowFromPoint(cursor);
        const LONG_PTR extra0 = GetWindowLongPtrW(hwnd, 0);
        preStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
        preVis = IsWindowVisible(hwnd) != FALSE;

        Wh_Log(L"[mouse-7-dispatch] PRE %s msg=0x%04X hwnd=%p class='%s' rect=(%ld,%ld)-(%ld,%ld) vis=%d enabled=%d style=0x%llX extra0=%p cursor=(%ld,%ld) WindowFromPoint=%p wParam=0x%llX lParam=0x%llX tid=%lu",
               SWndMsgName(msg), msg, hwnd, cls,
               rc.left, rc.top, rc.right, rc.bottom,
               preVis ? 1 : 0,
               IsWindowEnabled(hwnd) ? 1 : 0,
               (unsigned long long)preStyle,
               reinterpret_cast<void*>(extra0),
               cursor.x, cursor.y, preUnder,
               (unsigned long long)wParam,
               (unsigned long long)lParam,
               GetCurrentThreadId());

        if ((msg == WM_WINDOWPOSCHANGING || msg == WM_WINDOWPOSCHANGED) && lParam) {
            const WINDOWPOS* wp = reinterpret_cast<const WINDOWPOS*>(lParam);
            Wh_Log(L"[mouse-7-dispatch]   WINDOWPOS hwnd=%p after=%p x=%d y=%d cx=%d cy=%d flags=0x%X show=%d hide=%d noMove=%d noSize=%d noZ=%d",
                   wp->hwnd, wp->hwndInsertAfter, wp->x, wp->y, wp->cx, wp->cy, wp->flags,
                   (wp->flags & SWP_SHOWWINDOW) ? 1 : 0,
                   (wp->flags & SWP_HIDEWINDOW) ? 1 : 0,
                   (wp->flags & SWP_NOMOVE) ? 1 : 0,
                   (wp->flags & SWP_NOSIZE) ? 1 : 0,
                   (wp->flags & SWP_NOZORDER) ? 1 : 0);
        }

        if (button ||
            lifecycle ||
            (topMove &&
             g_sWndStacks.load(std::memory_order_relaxed) < 2)) {
            LogSWndStackLimited();
        }
    }

    const LRESULT result =
        g_sWndProc_Original(hwnd, msg, wParam, lParam);

    if (shouldLog) {
        POINT afterCursor{};
        GetCursorPos(&afterCursor);
        const HWND afterUnder = WindowFromPoint(afterCursor);
        const LONG_PTR afterStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const bool afterVis = IsWindowVisible(hwnd) != FALSE;

        Wh_Log(L"[mouse-7-dispatch] POST %s hwnd=%p result=0x%llX vis=%d->%d style=0x%llX->0x%llX cursor=(%ld,%ld) WindowFromPoint=%p->%p tid=%lu",
               SWndMsgName(msg), hwnd,
               (unsigned long long)result,
               preVis ? 1 : 0, afterVis ? 1 : 0,
               (unsigned long long)preStyle,
               (unsigned long long)afterStyle,
               afterCursor.x, afterCursor.y,
               preUnder, afterUnder,
               GetCurrentThreadId());
    }

    return result;
}

bool InstallSWndProcHook(HMODULE twinui) {
    constexpr const wchar_t* kSymbol =
        L"?s_WndProc@CEdgeUiInput@@CA_JPEAUHWND__@@I_K_J@Z";

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;

    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(twinui, &options, &symbol);
    if (!search) {
        Wh_Log(L"[mouse-7-dispatch] symbol enumeration failed");
        return false;
    }

    bool installed = false;
    do {
        const wchar_t* d = symbol.symbolDecorated;
        if (!d || !*d) d = symbol.symbol;
        if (!d || wcscmp(d, kSymbol) != 0) continue;

        if (Wh_SetFunctionHook(
                symbol.address,
                reinterpret_cast<void*>(&SWndProc_Hook),
                reinterpret_cast<void**>(&g_sWndProc_Original))) {
            Wh_Log(L"[mouse-7-dispatch] hooked CEdgeUiInput::s_WndProc at %p",
                   symbol.address);
            installed = true;
        } else {
            Wh_Log(L"[mouse-7-dispatch] FAILED hook CEdgeUiInput::s_WndProc at %p",
                   symbol.address);
        }
        break;
    } while (Wh_FindNextSymbol(search, &symbol));

    Wh_FindCloseSymbol(search);

    if (!installed) {
        Wh_Log(L"[mouse-7-dispatch] CEdgeUiInput::s_WndProc symbol not found");
    }
    return installed;
}



// -----------------------------------------------------------------------------
// Native titlebar-gate state resolver (read-only)
// -----------------------------------------------------------------------------

bool InstallEdgeGateStateProbe(HMODULE twinui) {
    constexpr const wchar_t* kDisableTitlebar =
        L"?s_fDisableTitlebarInvocation@CEdgeUiInput@@0_NA";

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;

    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(twinui, &options, &symbol);
    if (!search) {
        Wh_Log(L"[mouse-7-edge-state] symbol enumeration failed");
        return false;
    }

    do {
        const wchar_t* d = symbol.symbolDecorated;
        if (!d || !*d) d = symbol.symbol;
        if (!d || wcscmp(d, kDisableTitlebar) != 0) continue;
        g_disableTitlebarInvocationFlag =
            reinterpret_cast<volatile bool*>(symbol.address);
        break;
    } while (Wh_FindNextSymbol(search, &symbol));

    Wh_FindCloseSymbol(search);

    if (!g_disableTitlebarInvocationFlag) {
        Wh_Log(L"[mouse-7-edge-state] s_fDisableTitlebarInvocation symbol not found");
        return false;
    }

    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        g_RtlQueryWnfStateData = reinterpret_cast<RtlQueryWnfStateData_t>(
            GetProcAddress(ntdll, "RtlQueryWnfStateData"));
    }

    Wh_Log(L"[mouse-7-edge-state] resolved s_fDisableTitlebarInvocation=%p value=%d stateOffset=0x%X WNFstate=0x%016llX query=%p",
           const_cast<bool*>(g_disableTitlebarInvocationFlag),
           *g_disableTitlebarInvocationFlag ? 1 : 0,
           g_build == BuildKind::Win10 ? 0x7F : 0x87,
           kWnfTmcnIsTabletMode,
           reinterpret_cast<void*>(g_RtlQueryWnfStateData));
    return true;
}

bool ReadByteSafe(const void* address, BYTE* value) {
    if (!address || !value) return false;
    SIZE_T read = 0;
    BYTE temp = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), address, &temp, sizeof(temp), &read) ||
        read != sizeof(temp)) {
        return false;
    }
    *value = temp;
    return true;
}

struct WnfRawContext {
    bool called = false;
    DWORD raw = 0;
};

LONG NTAPI CaptureTabletWnfRawCallback(
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

bool QueryTabletWnfRaw(DWORD* rawOut, ULONG* stampOut, LONG* statusOut) {
    if (rawOut) *rawOut = 0;
    if (stampOut) *stampOut = 0;
    if (statusOut) *statusOut = static_cast<LONG>(0xC0000002L);
    if (!g_RtlQueryWnfStateData) return false;

    WnfRawContext ctx{};
    ULONG stamp = 0;
    const LONG status = g_RtlQueryWnfStateData(
        &stamp, kWnfTmcnIsTabletMode,
        reinterpret_cast<void*>(&CaptureTabletWnfRawCallback),
        &ctx, nullptr);

    if (rawOut) *rawOut = ctx.raw;
    if (stampOut) *stampOut = stamp;
    if (statusOut) *statusOut = status;
    return status >= 0 && ctx.called;
}

// -----------------------------------------------------------------------------
// Top-edge HWND/window-state watcher (formerly MOUSE-7 v0.1.x)
// -----------------------------------------------------------------------------

std::atomic<bool> g_windowWatcherStop{false};
HANDLE g_windowWatcherThread = nullptr;
DWORD g_pid = 0;
std::atomic<unsigned long> g_snapshot{0};

void GetClassSafe(HWND hwnd, wchar_t* out, int cch);

// v1.0.2: read-only desktop move/size competition tracer.  This is intentionally
// Win32-only and uses out-of-context WinEvent notifications; it never injects or
// suppresses input.
HWINEVENTHOOK g_moveSizeHook = nullptr;
HWINEVENTHOOK g_minimizeHook = nullptr;
HWINEVENTHOOK g_locationHook = nullptr;
HWINEVENTHOOK g_foregroundHook = nullptr;
std::atomic<HWND> g_gestureHwnd{nullptr};
std::atomic<ULONGLONG> g_gestureUntil{0};
std::atomic<bool> g_moveSizeActive{false};
std::atomic<ULONGLONG> g_lastLocationLog{0};
std::atomic<unsigned long> g_desktopApiStacks{0};
std::atomic<unsigned long> g_edgeApiStacks{0};

using ShowWindow_t = BOOL(WINAPI*)(HWND, int);
using ShowWindowAsync_t = BOOL(WINAPI*)(HWND, int);
using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using SetWindowPlacement_t = BOOL(WINAPI*)(HWND, const WINDOWPLACEMENT*);
ShowWindow_t g_ShowWindow_Original = nullptr;
ShowWindowAsync_t g_ShowWindowAsync_Original = nullptr;
SetWindowPos_t g_SetWindowPos_Original = nullptr;
SetWindowPlacement_t g_SetWindowPlacement_Original = nullptr;

const wchar_t* ShowCmdName(UINT cmd);

void GetProcessImageBaseName(DWORD pid, wchar_t* out, DWORD cch) {
    if (!out || cch == 0) return;
    out[0] = L'\0';
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return;
    DWORD len = cch;
    if (QueryFullProcessImageNameW(process, 0, out, &len)) {
        BaseNameInPlace(out);
    } else {
        out[0] = L'\0';
    }
    CloseHandle(process);
}

bool ShouldTraceDesktopApiTarget(HWND hwnd) {
    if (!hwnd || hwnd != g_gestureHwnd.load(std::memory_order_relaxed)) return false;
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG until = g_gestureUntil.load(std::memory_order_relaxed);
    return now <= until || g_moveSizeActive.load(std::memory_order_relaxed);
}

bool IsTopEdgeListenerWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    wchar_t cls[96] = {};
    return GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) > 0 &&
           lstrcmpW(cls, L"EdgeUiInputTopWndClass") == 0;
}

void LogEdgeApiStackLimited(const wchar_t* api) {
    const unsigned long n =
        g_edgeApiStacks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n > 24) return;

    void* frames[16] = {};
    const USHORT count =
        RtlCaptureStackBackTrace(1, ARRAYSIZE(frames), frames, nullptr);
    Wh_Log(L"[mouse-7-edge-api] STACK #%lu api=%s frames=%u",
           n, api, (unsigned)count);
    for (USHORT i = 0; i < count; ++i) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(frames[i], &mbi, sizeof(mbi)) || !mbi.AllocationBase) {
            Wh_Log(L"[mouse-7-edge-api]   #%u addr=%p", (unsigned)i, frames[i]);
            continue;
        }
        HMODULE mod = reinterpret_cast<HMODULE>(mbi.AllocationBase);
        wchar_t path[MAX_PATH] = {};
        if (!GetModuleFileNameW(mod, path, ARRAYSIZE(path))) {
            Wh_Log(L"[mouse-7-edge-api]   #%u addr=%p module=%p",
                   (unsigned)i, frames[i], mod);
            continue;
        }
        BaseNameInPlace(path);
        const ULONG_PTR rva = reinterpret_cast<BYTE*>(frames[i]) -
                              reinterpret_cast<BYTE*>(mod);
        Wh_Log(L"[mouse-7-edge-api]   #%u %s+0x%llX addr=%p",
               (unsigned)i, path, (unsigned long long)rva, frames[i]);
    }
}

void LogDesktopApiStackLimited(const wchar_t* api) {
    const unsigned long n =
        g_desktopApiStacks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n > 12) return;

    void* frames[16] = {};
    const USHORT count =
        RtlCaptureStackBackTrace(1, ARRAYSIZE(frames), frames, nullptr);
    Wh_Log(L"[mouse-7-desktop-api] STACK #%lu api=%s frames=%u",
           n, api, (unsigned)count);
    for (USHORT i = 0; i < count; ++i) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(frames[i], &mbi, sizeof(mbi)) || !mbi.AllocationBase) {
            Wh_Log(L"[mouse-7-desktop-api]   #%u addr=%p", (unsigned)i, frames[i]);
            continue;
        }
        HMODULE mod = reinterpret_cast<HMODULE>(mbi.AllocationBase);
        wchar_t path[MAX_PATH] = {};
        if (!GetModuleFileNameW(mod, path, ARRAYSIZE(path))) {
            Wh_Log(L"[mouse-7-desktop-api]   #%u addr=%p module=%p",
                   (unsigned)i, frames[i], mod);
            continue;
        }
        BaseNameInPlace(path);
        const ULONG_PTR rva = reinterpret_cast<BYTE*>(frames[i]) -
                              reinterpret_cast<BYTE*>(mod);
        Wh_Log(L"[mouse-7-desktop-api]   #%u %s+0x%llX addr=%p",
               (unsigned)i, path, (unsigned long long)rva, frames[i]);
    }
}

BOOL WINAPI ShowWindow_Hook(HWND hwnd, int cmd) {
    const bool trace = ShouldTraceDesktopApiTarget(hwnd);
    const bool edge = IsTopEdgeListenerWindow(hwnd);
    if (trace) {
        Wh_Log(L"[mouse-7-desktop-api] PRE ShowWindow hwnd=%p cmd=%d(%s) callerTid=%lu", hwnd, cmd, ShowCmdName(cmd), GetCurrentThreadId());
        LogDesktopApiStackLimited(L"ShowWindow");
    }
    if (edge) {
        Wh_Log(L"[mouse-7-edge-api] PRE ShowWindow hwnd=%p cmd=%d(%s) vis=%d style=0x%llX callerTid=%lu", hwnd, cmd, ShowCmdName(cmd), IsWindowVisible(hwnd)?1:0, (unsigned long long)GetWindowLongPtrW(hwnd,GWL_STYLE), GetCurrentThreadId());
        LogEdgeApiStackLimited(L"ShowWindow");
    }
    const BOOL result = g_ShowWindow_Original(hwnd, cmd);
    if (trace) Wh_Log(L"[mouse-7-desktop-api] POST ShowWindow hwnd=%p cmd=%d result=%d", hwnd, cmd, result ? 1 : 0);
    if (edge) Wh_Log(L"[mouse-7-edge-api] POST ShowWindow hwnd=%p cmd=%d result=%d vis=%d style=0x%llX", hwnd, cmd, result?1:0, IsWindowVisible(hwnd)?1:0, (unsigned long long)GetWindowLongPtrW(hwnd,GWL_STYLE));
    return result;
}

BOOL WINAPI ShowWindowAsync_Hook(HWND hwnd, int cmd) {
    const bool trace = ShouldTraceDesktopApiTarget(hwnd);
    const bool edge = IsTopEdgeListenerWindow(hwnd);
    if (trace) { Wh_Log(L"[mouse-7-desktop-api] PRE ShowWindowAsync hwnd=%p cmd=%d(%s) callerTid=%lu", hwnd, cmd, ShowCmdName(cmd), GetCurrentThreadId()); LogDesktopApiStackLimited(L"ShowWindowAsync"); }
    if (edge) { Wh_Log(L"[mouse-7-edge-api] PRE ShowWindowAsync hwnd=%p cmd=%d(%s) vis=%d style=0x%llX callerTid=%lu", hwnd, cmd, ShowCmdName(cmd), IsWindowVisible(hwnd)?1:0, (unsigned long long)GetWindowLongPtrW(hwnd,GWL_STYLE), GetCurrentThreadId()); LogEdgeApiStackLimited(L"ShowWindowAsync"); }
    const BOOL result = g_ShowWindowAsync_Original(hwnd, cmd);
    if (trace) Wh_Log(L"[mouse-7-desktop-api] POST ShowWindowAsync hwnd=%p cmd=%d result=%d", hwnd, cmd, result?1:0);
    if (edge) Wh_Log(L"[mouse-7-edge-api] POST ShowWindowAsync hwnd=%p cmd=%d result=%d vis=%d style=0x%llX", hwnd, cmd, result?1:0, IsWindowVisible(hwnd)?1:0, (unsigned long long)GetWindowLongPtrW(hwnd,GWL_STYLE));
    return result;
}

BOOL WINAPI SetWindowPos_Hook(HWND hwnd, HWND insertAfter, int x, int y, int cx, int cy, UINT flags) {
    const bool trace = ShouldTraceDesktopApiTarget(hwnd);
    const bool edge = IsTopEdgeListenerWindow(hwnd);
    if (trace) { Wh_Log(L"[mouse-7-desktop-api] PRE SetWindowPos hwnd=%p after=%p x=%d y=%d cx=%d cy=%d flags=0x%X callerTid=%lu", hwnd, insertAfter, x, y, cx, cy, flags, GetCurrentThreadId()); LogDesktopApiStackLimited(L"SetWindowPos"); }
    if (edge) { Wh_Log(L"[mouse-7-edge-api] PRE SetWindowPos hwnd=%p after=%p x=%d y=%d cx=%d cy=%d flags=0x%X show=%d hide=%d vis=%d style=0x%llX callerTid=%lu", hwnd, insertAfter, x, y, cx, cy, flags, (flags & SWP_SHOWWINDOW)?1:0, (flags & SWP_HIDEWINDOW)?1:0, IsWindowVisible(hwnd)?1:0, (unsigned long long)GetWindowLongPtrW(hwnd,GWL_STYLE), GetCurrentThreadId()); LogEdgeApiStackLimited(L"SetWindowPos"); }
    const BOOL result = g_SetWindowPos_Original(hwnd, insertAfter, x, y, cx, cy, flags);
    if (trace) Wh_Log(L"[mouse-7-desktop-api] POST SetWindowPos hwnd=%p result=%d", hwnd, result?1:0);
    if (edge) Wh_Log(L"[mouse-7-edge-api] POST SetWindowPos hwnd=%p result=%d vis=%d style=0x%llX", hwnd, result?1:0, IsWindowVisible(hwnd)?1:0, (unsigned long long)GetWindowLongPtrW(hwnd,GWL_STYLE));
    return result;
}

BOOL WINAPI SetWindowPlacement_Hook(HWND hwnd, const WINDOWPLACEMENT* wp) {
    const bool trace = ShouldTraceDesktopApiTarget(hwnd);
    if (trace) {
        Wh_Log(L"[mouse-7-desktop-api] PRE SetWindowPlacement hwnd=%p showCmd=%u(%s) flags=0x%X normal=(%ld,%ld)-(%ld,%ld) callerTid=%lu",
               hwnd,
               wp ? wp->showCmd : 0,
               wp ? ShowCmdName(wp->showCmd) : L"n/a",
               wp ? wp->flags : 0,
               wp ? wp->rcNormalPosition.left : 0,
               wp ? wp->rcNormalPosition.top : 0,
               wp ? wp->rcNormalPosition.right : 0,
               wp ? wp->rcNormalPosition.bottom : 0,
               GetCurrentThreadId());
        LogDesktopApiStackLimited(L"SetWindowPlacement");
    }
    const BOOL result = g_SetWindowPlacement_Original(hwnd, wp);
    if (trace) {
        Wh_Log(L"[mouse-7-desktop-api] POST SetWindowPlacement hwnd=%p result=%d",
               hwnd, result ? 1 : 0);
    }
    return result;
}

bool InstallDesktopApiHooks() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return false;
    void* show = reinterpret_cast<void*>(GetProcAddress(user32, "ShowWindow"));
    void* showAsync = reinterpret_cast<void*>(GetProcAddress(user32, "ShowWindowAsync"));
    void* setPos = reinterpret_cast<void*>(GetProcAddress(user32, "SetWindowPos"));
    void* setPlacement = reinterpret_cast<void*>(GetProcAddress(user32, "SetWindowPlacement"));
    bool ok = show && showAsync && setPos && setPlacement;
    if (show) ok = Wh_SetFunctionHook(show, reinterpret_cast<void*>(&ShowWindow_Hook),
                                      reinterpret_cast<void**>(&g_ShowWindow_Original)) && ok;
    if (showAsync) ok = Wh_SetFunctionHook(showAsync, reinterpret_cast<void*>(&ShowWindowAsync_Hook),
                                           reinterpret_cast<void**>(&g_ShowWindowAsync_Original)) && ok;
    if (setPos) ok = Wh_SetFunctionHook(setPos, reinterpret_cast<void*>(&SetWindowPos_Hook),
                                        reinterpret_cast<void**>(&g_SetWindowPos_Original)) && ok;
    if (setPlacement) ok = Wh_SetFunctionHook(setPlacement, reinterpret_cast<void*>(&SetWindowPlacement_Hook),
                                              reinterpret_cast<void**>(&g_SetWindowPlacement_Original)) && ok;
    Wh_Log(L"[mouse-7-desktop-api] hooks %s ShowWindow=%p ShowWindowAsync=%p SetWindowPos=%p SetWindowPlacement=%p",
           ok ? L"READY" : L"PARTIAL", show, showAsync, setPos, setPlacement);
    return ok;
}

const wchar_t* ShowCmdName(UINT cmd) {
    switch (cmd) {
        case SW_HIDE: return L"SW_HIDE";
        case SW_SHOWNORMAL: return L"SW_SHOWNORMAL";
        case SW_SHOWMINIMIZED: return L"SW_SHOWMINIMIZED";
        case SW_SHOWMAXIMIZED: return L"SW_SHOWMAXIMIZED";
        case SW_SHOWNOACTIVATE: return L"SW_SHOWNOACTIVATE";
        case SW_SHOW: return L"SW_SHOW";
        case SW_MINIMIZE: return L"SW_MINIMIZE";
        case SW_SHOWMINNOACTIVE: return L"SW_SHOWMINNOACTIVE";
        case SW_SHOWNA: return L"SW_SHOWNA";
        case SW_RESTORE: return L"SW_RESTORE";
        case SW_SHOWDEFAULT: return L"SW_SHOWDEFAULT";
        default: return L"SW_?";
    }
}

const wchar_t* WinEventName(DWORD event) {
    switch (event) {
        case EVENT_SYSTEM_FOREGROUND: return L"FOREGROUND";
        case EVENT_SYSTEM_MOVESIZESTART: return L"MOVESIZESTART";
        case EVENT_SYSTEM_MOVESIZEEND: return L"MOVESIZEEND";
        case EVENT_SYSTEM_MINIMIZESTART: return L"MINIMIZESTART";
        case EVENT_SYSTEM_MINIMIZEEND: return L"MINIMIZEEND";
        case EVENT_OBJECT_LOCATIONCHANGE: return L"LOCATIONCHANGE";
        default: return L"?";
    }
}

void LogDesktopWindowState(PCWSTR reason, DWORD event, HWND hwnd, DWORD eventThread) {
    if (!hwnd || !IsWindow(hwnd)) return;

    RECT r{};
    GetWindowRect(hwnd, &r);
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    const BOOL wpOk = GetWindowPlacement(hwnd, &wp);
    wchar_t cls[128]{};
    GetClassSafe(hwnd, cls, ARRAYSIZE(cls));
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    wchar_t image[MAX_PATH] = {};
    GetProcessImageBaseName(pid, image, ARRAYSIZE(image));
    wchar_t title[160] = {};
    GetWindowTextW(hwnd, title, ARRAYSIZE(title));
    HWND owner = GetWindow(hwnd, GW_OWNER);
    HWND root = GetAncestor(hwnd, GA_ROOT);
    POINT pt{};
    GetCursorPos(&pt);
    const bool left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    Wh_Log(L"[mouse-7-desktop] %s event=%s(0x%lX) hwnd=%p class='%s' image='%s' title='%s' pid=%lu tid=%lu eventTid=%lu owner=%p root=%p rect=(%ld,%ld)-(%ld,%ld) wpOk=%d showCmd=%u(%s) wpFlags=0x%X normal=(%ld,%ld)-(%ld,%ld) cursor=(%ld,%ld) left=%d foreground=%p WindowFromPoint=%p",
           reason, WinEventName(event), event, hwnd, cls, image, title,
           pid, tid, eventThread, owner, root,
           r.left, r.top, r.right, r.bottom, wpOk ? 1 : 0,
           wpOk ? wp.showCmd : 0, wpOk ? ShowCmdName(wp.showCmd) : L"n/a",
           wpOk ? wp.flags : 0,
           wpOk ? wp.rcNormalPosition.left : 0, wpOk ? wp.rcNormalPosition.top : 0,
           wpOk ? wp.rcNormalPosition.right : 0, wpOk ? wp.rcNormalPosition.bottom : 0,
           pt.x, pt.y, left ? 1 : 0, GetForegroundWindow(), WindowFromPoint(pt));
}

void CALLBACK DesktopWinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                  LONG idObject, LONG idChild,
                                  DWORD eventThread, DWORD) {
    if (!hwnd) return;
    if (event == EVENT_OBJECT_LOCATIONCHANGE &&
        (idObject != OBJID_WINDOW || idChild != 0)) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG until = g_gestureUntil.load(std::memory_order_relaxed);
    HWND target = g_gestureHwnd.load(std::memory_order_relaxed);
    const bool recent = now <= until;

    if (event == EVENT_SYSTEM_MOVESIZESTART) {
        if (!recent && hwnd != target) return;
        g_moveSizeActive.store(true, std::memory_order_relaxed);
        g_gestureHwnd.store(hwnd, std::memory_order_relaxed);
        g_gestureUntil.store(now + 10000, std::memory_order_relaxed);
        LogDesktopWindowState(L"WIN_EVENT", event, hwnd, eventThread);
        return;
    }
    if (event == EVENT_SYSTEM_MOVESIZEEND) {
        if (!g_moveSizeActive.load(std::memory_order_relaxed) && hwnd != target) return;
        LogDesktopWindowState(L"WIN_EVENT", event, hwnd, eventThread);
        g_moveSizeActive.store(false, std::memory_order_relaxed);
        g_gestureUntil.store(now + 5000, std::memory_order_relaxed);
        return;
    }
    if (event == EVENT_OBJECT_LOCATIONCHANGE) {
        if (hwnd != target || (!recent && !g_moveSizeActive.load(std::memory_order_relaxed))) return;
        const ULONGLONG last = g_lastLocationLog.load(std::memory_order_relaxed);
        if (now - last < 100) return;
        g_lastLocationLog.store(now, std::memory_order_relaxed);
        LogDesktopWindowState(L"WIN_EVENT", event, hwnd, eventThread);
        return;
    }
    if (event == EVENT_SYSTEM_FOREGROUND) {
        if (!recent && !g_moveSizeActive.load(std::memory_order_relaxed)) return;
        LogDesktopWindowState(L"WIN_EVENT", event, hwnd, eventThread);
        return;
    }
    if (event == EVENT_SYSTEM_MINIMIZESTART || event == EVENT_SYSTEM_MINIMIZEEND) {
        if (!recent && hwnd != target) return;
        LogDesktopWindowState(L"WIN_EVENT", event, hwnd, eventThread);
    }
}

bool StartDesktopWinEventTrace() {
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    g_moveSizeHook = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND,
                                     nullptr, DesktopWinEventProc, 0, 0, flags);
    g_minimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
                                     nullptr, DesktopWinEventProc, 0, 0, flags);
    g_locationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                                     nullptr, DesktopWinEventProc, 0, 0, flags);
    g_foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                       nullptr, DesktopWinEventProc, 0, 0, flags);
    const bool ok = g_moveSizeHook && g_minimizeHook && g_locationHook && g_foregroundHook;
    Wh_Log(L"[mouse-7-desktop] WinEvent trace %s move=%p minimize=%p location=%p foreground=%p",
           ok ? L"READY" : L"PARTIAL", g_moveSizeHook, g_minimizeHook,
           g_locationHook, g_foregroundHook);
    return ok;
}

void StopDesktopWinEventTrace() {
    if (g_moveSizeHook) { UnhookWinEvent(g_moveSizeHook); g_moveSizeHook = nullptr; }
    if (g_minimizeHook) { UnhookWinEvent(g_minimizeHook); g_minimizeHook = nullptr; }
    if (g_locationHook) { UnhookWinEvent(g_locationHook); g_locationHook = nullptr; }
    if (g_foregroundHook) { UnhookWinEvent(g_foregroundHook); g_foregroundHook = nullptr; }
}


using DwmGetWindowAttribute_t =
    HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
DwmGetWindowAttribute_t g_DwmGetWindowAttribute = nullptr;
constexpr DWORD kDwmaCloaked = 14;

void GetClassSafe(HWND hwnd, wchar_t* out, int cch) {
    if (!out || cch <= 0) return;
    out[0] = L'\0';
    if (hwnd) GetClassNameW(hwnd, out, cch);
}

void LogWindowBrief(PCWSTR label, HWND hwnd, POINT pt) {
    if (!hwnd) {
        Wh_Log(L"[mouse-7-window] %s hwnd=NULL pt=(%ld,%ld)",
               label, pt.x, pt.y);
        return;
    }

    RECT r{};
    GetWindowRect(hwnd, &r);
    wchar_t cls[128]{};
    GetClassSafe(hwnd, cls, ARRAYSIZE(cls));

    DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    const auto style =
        static_cast<unsigned long long>(
            GetWindowLongPtrW(hwnd, GWL_STYLE));
    const auto ex =
        static_cast<unsigned long long>(
            GetWindowLongPtrW(hwnd, GWL_EXSTYLE));

    DWORD cloaked = 0xFFFFFFFFu;
    if (g_DwmGetWindowAttribute) {
        if (FAILED(g_DwmGetWindowAttribute(
                hwnd, kDwmaCloaked, &cloaked, sizeof(cloaked)))) {
            cloaked = 0xFFFFFFFEu;
        }
    }

    Wh_Log(L"[mouse-7-window] %s hwnd=%p class='%s' rect=(%ld,%ld)-(%ld,%ld) vis=%d enabled=%d pid=%lu tid=%lu style=0x%llX ex=0x%llX cloaked=%lu pt=(%ld,%ld)",
           label, hwnd, cls,
           r.left, r.top, r.right, r.bottom,
           IsWindowVisible(hwnd) ? 1 : 0,
           IsWindowEnabled(hwnd) ? 1 : 0,
           pid, tid, style, ex, cloaked, pt.x, pt.y);
}

struct WindowEnumCtx {
    POINT pt;
    bool noButtons;
    unsigned zIndex;
    unsigned found;
};

BOOL CALLBACK WindowEnumProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<WindowEnumCtx*>(lp);
    const unsigned z = ctx->zIndex++;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != g_pid) return TRUE;

    wchar_t cls[128]{};
    GetClassSafe(hwnd, cls, ARRAYSIZE(cls));
    if (lstrcmpW(cls, L"EdgeUiInputTopWndClass") != 0) {
        return TRUE;
    }

    RECT r{};
    GetWindowRect(hwnd, &r);
    const bool contains =
        ctx->pt.x >= r.left && ctx->pt.x < r.right &&
        ctx->pt.y >= r.top && ctx->pt.y < r.bottom;

    DWORD cloaked = 0xFFFFFFFFu;
    if (g_DwmGetWindowAttribute) {
        if (FAILED(g_DwmGetWindowAttribute(
                hwnd, kDwmaCloaked, &cloaked, sizeof(cloaked)))) {
            cloaked = 0xFFFFFFFEu;
        }
    }

    LRESULT hit = 0x7FFFFFFF;
    BOOL hitOk = FALSE;
    if (ctx->noButtons) {
        DWORD_PTR result = 0;
        const LPARAM packed =
            MAKELPARAM(static_cast<SHORT>(ctx->pt.x),
                       static_cast<SHORT>(ctx->pt.y));
        hitOk = SendMessageTimeoutW(
                    hwnd, WM_NCHITTEST, 0, packed,
                    SMTO_ABORTIFHUNG | SMTO_BLOCK,
                    100, &result) != 0;
        if (hitOk) hit = static_cast<LRESULT>(result);
    }

    const auto style =
        static_cast<unsigned long long>(
            GetWindowLongPtrW(hwnd, GWL_STYLE));
    const auto ex =
        static_cast<unsigned long long>(
            GetWindowLongPtrW(hwnd, GWL_EXSTYLE));

    DWORD ownerPid = 0;
    const DWORD tid = GetWindowThreadProcessId(hwnd, &ownerPid);

    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    const BOOL gtiOk =
        tid ? GetGUIThreadInfo(tid, &gti) : FALSE;

    const LONG_PTR extra0 = GetWindowLongPtrW(hwnd, 0);
    const auto edgeThis = extra0
        ? reinterpret_cast<const BYTE*>(extra0) + kEdgeUiInputInterfaceOffset
        : nullptr;
    const size_t stateOffset =
        g_build == BuildKind::Win10 ? 0x7F : 0x87;
    BYTE nativeState = 0xFF;
    const bool nativeStateOk = edgeThis &&
        ReadByteSafe(edgeThis + stateOffset, &nativeState);
    const int titlebarDisabled = g_disableTitlebarInvocationFlag
        ? (*g_disableTitlebarInvocationFlag ? 1 : 0)
        : -1;

    Wh_Log(L"[mouse-7-window] TOPHWND #%u z=%u hwnd=%p rect=(%ld,%ld)-(%ld,%ld) contains=%d vis=%d enabled=%d cloaked=%lu style=0x%llX ex=0x%llX WM_NCHITTEST=%lld hitOk=%d pid=%lu tid=%lu extra0=%p edgeThis=%p titlebarDisabled=%d nativeSetVisibleState=%d stateOk=%d gtiOk=%d gtiFlags=0x%lX threadCapture=%p active=%p focus=%p moveSize=%p menuOwner=%p",
           ++ctx->found, z, hwnd,
           r.left, r.top, r.right, r.bottom,
           contains ? 1 : 0,
           IsWindowVisible(hwnd) ? 1 : 0,
           IsWindowEnabled(hwnd) ? 1 : 0,
           cloaked, style, ex,
           static_cast<long long>(hit), hitOk ? 1 : 0,
           ownerPid, tid,
           reinterpret_cast<void*>(extra0),
           const_cast<BYTE*>(edgeThis),
           titlebarDisabled,
           nativeStateOk ? static_cast<int>(nativeState) : -1,
           nativeStateOk ? 1 : 0,
           gtiOk ? 1 : 0,
           gtiOk ? gti.flags : 0,
           gtiOk ? gti.hwndCapture : nullptr,
           gtiOk ? gti.hwndActive : nullptr,
           gtiOk ? gti.hwndFocus : nullptr,
           gtiOk ? gti.hwndMoveSize : nullptr,
           gtiOk ? gti.hwndMenuOwner : nullptr);

    return TRUE;
}

void WindowStateSnapshot() {
    POINT pt{};
    GetCursorPos(&pt);

    const bool left =
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool right =
        (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool middle =
        (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
    const bool noButtons = !left && !right && !middle;

    const HWND at = WindowFromPoint(pt);
    const HWND fg = GetForegroundWindow();
    const HWND localCap = GetCapture();
    const unsigned long snapshot =
        g_snapshot.fetch_add(1, std::memory_order_relaxed) + 1;

    Wh_Log(L"[mouse-7-window] SNAPSHOT #%lu cursor=(%ld,%ld) buttons(LRM)=%d%d%d WindowFromPoint=%p localThreadCapture=%p foreground=%p",
           snapshot, pt.x, pt.y,
           left ? 1 : 0, right ? 1 : 0, middle ? 1 : 0,
           at, localCap, fg);
    DWORD tabletWnfRaw = 0;
    ULONG tabletWnfStamp = 0;
    LONG tabletWnfStatus = static_cast<LONG>(0xC0000002L);
    const bool tabletWnfOk = QueryTabletWnfRaw(
        &tabletWnfRaw, &tabletWnfStamp, &tabletWnfStatus);
    Wh_Log(L"[mouse-7-edge-state] SNAPSHOT #%lu s_fDisableTitlebarInvocation=%d WNF_TMCN_ISTABLETMODE ok=%d raw=%lu stamp=%lu status=0x%08X",
           snapshot,
           g_disableTitlebarInvocationFlag
               ? (*g_disableTitlebarInvocationFlag ? 1 : 0)
               : -1,
           tabletWnfOk ? 1 : 0,
           tabletWnfRaw, tabletWnfStamp,
           static_cast<unsigned int>(tabletWnfStatus));

    LogWindowBrief(L"WindowFromPoint", at, pt);

    WindowEnumCtx ctx{pt, noButtons, 0, 0};
    EnumWindows(WindowEnumProc,
                reinterpret_cast<LPARAM>(&ctx));

    Wh_Log(L"[mouse-7-window] SNAPSHOT #%lu done topEdgeWindows=%u",
           snapshot, ctx.found);
}

DWORD WINAPI WindowWatcherThreadProc(LPVOID) {
    StartDesktopWinEventTrace();

    bool inBand = false;
    bool lastLeft = false;
    ULONGLONG lastSnapshot = 0;

    while (!g_windowWatcherStop.load()) {
        TryInstallDeferredPerWindowShellBehaviorHook();

        POINT pt{};
        GetCursorPos(&pt);

        const bool now = pt.y >= 0 && pt.y <= 3;
        const bool left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        const ULONGLONG tick = GetTickCount64();

        if (now && (!inBand || tick - lastSnapshot >= 1000)) {
            WindowStateSnapshot();
            lastSnapshot = tick;
        }

        if (left != lastLeft) {
            HWND at = WindowFromPoint(pt);
            HWND fg = GetForegroundWindow();
            if (left) {
                Wh_Log(L"[mouse-7-wmnotify] DRAG_HOOK_STATUS cursor=(%ld,%ld) installed=%lu/3 wndprocOriginal=%p notifyOriginal=%p registerOriginal=%p counts(private=%lu notify=%lu register=%lu) perWindow=%lu/1 perWindowOriginal=%p perWindowCalls=%lu",
                       pt.x, pt.y,
                       g_wmHooksInstalled.load(std::memory_order_relaxed),
                       WindowManagementWndProc_Original, WindowManagementNotify_Original,
                       WindowManagementRegisterShowMoveSize_Original,
                       g_wmWndProcPrivate.load(), g_wmNotify.load(),
                       g_wmRegisterShowMoveSize.load(),
                       g_perWindowShellBehaviorHooksInstalled.load(std::memory_order_relaxed),
                       PerWindowShellBehavior_Original,
                       g_perWindowShellBehavior.load());
            }
            if (left && pt.y >= 0 && pt.y <= 8) {
                HWND target = at ? at : fg;
                g_gestureHwnd.store(target, std::memory_order_relaxed);
                g_gestureUntil.store(tick + 10000, std::memory_order_relaxed);
                Wh_Log(L"[mouse-7-desktop] LBUTTON_DOWN_TOP cursor=(%ld,%ld) WindowFromPoint=%p foreground=%p target=%p",
                       pt.x, pt.y, at, fg, target);
                if (target) LogDesktopWindowState(L"BUTTON", 0, target, 0);
            } else if (!left) {
                HWND target = g_gestureHwnd.load(std::memory_order_relaxed);
                const ULONGLONG until = g_gestureUntil.load(std::memory_order_relaxed);
                if (target && tick <= until) {
                    Wh_Log(L"[mouse-7-desktop] LBUTTON_UP cursor=(%ld,%ld) target=%p foreground=%p",
                           pt.x, pt.y, target, fg);
                    LogDesktopWindowState(L"BUTTON", 0, target, 0);
                    g_gestureUntil.store(tick + 5000, std::memory_order_relaxed);
                }
            }
            lastLeft = left;
        }

        inBand = now;

        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }

    StopDesktopWinEventTrace();
    return 0;
}

bool StartWindowWatcher() {
    g_pid = GetCurrentProcessId();

    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        g_DwmGetWindowAttribute =
            reinterpret_cast<DwmGetWindowAttribute_t>(
                GetProcAddress(dwm, "DwmGetWindowAttribute"));
    }

    g_windowWatcherStop.store(false);
    g_windowWatcherThread =
        CreateThread(nullptr, 0,
                     WindowWatcherThreadProc,
                     nullptr, 0, nullptr);
    if (!g_windowWatcherThread) {
        Wh_Log(L"[mouse-7-window] ERROR: CreateThread failed=%lu",
               GetLastError());
        return false;
    }

    return true;
}

void StopWindowWatcher() {
    g_windowWatcherStop.store(true);
    if (g_windowWatcherThread) {
        WaitForSingleObject(g_windowWatcherThread, 1000);
        CloseHandle(g_windowWatcherThread);
        g_windowWatcherThread = nullptr;
    }
}



} // namespace

BOOL Wh_ModInit() {
    if (!IsMainShellExplorer()) {
        Wh_Log(L"[mouse-7] not main shell Explorer; skipping pid=%lu",
               GetCurrentProcessId());
        return FALSE;
    }

    Wh_Log(L"[mouse-7] loading consolidated read-only MOUSE-7 v1.0.16 pid=%lu",
           GetCurrentProcessId());

    HMODULE twinui = GetModuleHandleW(L"twinui.dll");
    if (!twinui) {
        Wh_Log(L"[mouse-7] twinui.dll not loaded");
        return FALSE;
    }

    g_build = DetectTwinuiBuild(twinui);
    if (g_build == BuildKind::Unsupported) {
        Wh_Log(L"[mouse-7] unsupported twinui build; refusing private hooks");
        return FALSE;
    }

    const size_t wmNotifyInstalled = InstallWindowManagementNotifyHooks(twinui);
    Wh_Log(L"[mouse-7] WindowManagementEvents ingress trace installed hooks=%llu/3",
           (unsigned long long)wmNotifyInstalled);

    if (!InstallEdgeGateStateProbe(twinui)) {
        Wh_Log(L"[mouse-7] WARNING: native titlebar-gate state probe unavailable");
    }

    if (!InstallSWndProcHook(twinui)) {
        Wh_Log(L"[mouse-7] required s_WndProc hook was not installed");
        return FALSE;
    }

    const size_t titleHitInstalled = InstallTitleHitProbe(twinui);
    Wh_Log(L"[mouse-7] cross-build titlebar hit-test probe installed hooks=%llu/2",
           (unsigned long long)titleHitInstalled);

    HMODULE pcshell = GetModuleHandleW(L"twinui.pcshell.dll");
    if (!pcshell) {
        Wh_Log(L"[mouse-7-resize] twinui.pcshell.dll not loaded; resize-handoff trace unavailable");
    } else {
        const size_t resizeInstalled = InstallResizeHandoffHooks(pcshell);
        Wh_Log(L"[mouse-7] cross-build pcshell resize-handoff trace installed hooks=%llu/4 module=%p",
               (unsigned long long)resizeInstalled, pcshell);
        const size_t moveSizeInstalled = InstallMoveSizeHandoffHooks(pcshell);
        Wh_Log(L"[mouse-7] cross-build pcshell move-size handoff trace installed hooks=%llu/3 module=%p",
               (unsigned long long)moveSizeInstalled, pcshell);
        if (g_build == BuildKind::Win11_25H2) {
            const size_t perWindowInstalled = InstallPerWindowShellBehaviorHook(pcshell);
            Wh_Log(L"[mouse-7] 25H2 per-window shell-management behavior trace installed hooks=%llu/1 module=%p",
                   (unsigned long long)perWindowInstalled, pcshell);
        }
    }

    if (g_build == BuildKind::Win10) {
        if (!InstallVisibilityProbe(twinui)) {
            Wh_Log(L"[mouse-7] required Win10 visibility probe was not installed");
            return FALSE;
        }
        const size_t installed = InstallHooks(twinui);
        Wh_Log(L"[mouse-7] Win10 full legacy route trace installed hooks=%llu",
               (unsigned long long)installed);

        if (!pcshell) {
            Wh_Log(L"[mouse-7-input] twinui.pcshell.dll not loaded; Win10 input/ViewManager lifecycle trace unavailable");
        } else {
            const size_t inputInstalled = InstallInputHandlerHooks(pcshell);
            Wh_Log(L"[mouse-7] Win10 pcshell TabletModeInputHandler/ViewManager trace installed hooks=%llu/8 module=%p",
                   (unsigned long long)inputInstalled, pcshell);
        }
    } else {
        Wh_Log(L"[mouse-7] Win11: skipping duplicate route/SetVisible/InputHandler hooks; use the main restoration mod for downstream, SetVisible, and TabletModeInputHandler tracing");
    }

    if (!InstallDesktopApiHooks()) {
        Wh_Log(L"[mouse-7] WARNING: one or more desktop API hooks were not installed");
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    if (!Wh_ApplyHookOperations()) {
        Wh_Log(L"[mouse-7] Wh_ApplyHookOperations failed");
        return;
    }

    if (!StartWindowWatcher()) {
        Wh_Log(L"[mouse-7] WARNING: top-edge window watcher did not start; private hooks remain active");
    }
    Wh_Log(L"[mouse-7] READY v1.0.16: read-only diagnostics + WindowManagementEvents ingress + deferred 25H2 per-window shell-management policy probe");
}

void Wh_ModUninit() {
    StopWindowWatcher();

    Wh_Log(L"[mouse-7-dispatch] totals calls=%lu topMoves=%lu heldMoves=%lu buttonMsgs=%lu",
           g_sWndCalls.load(),
           g_sWndTopMoves.load(),
           g_sWndHeldMoves.load(),
           g_sWndButtonMsgs.load());
    Wh_Log(L"[mouse-7-resize] totals showAppResize=%lu proxyShowAppResize=%lu showArrangement=%lu inputCreate=%lu",
           g_showAppResize.load(), g_proxyShowAppResize.load(),
           g_showArrangement.load(), g_inputCreate.load());
    Wh_Log(L"[mouse-7-windowbehavior] totals perWindowCalls=%lu hookInstalled=%lu/1",
           g_perWindowShellBehavior.load(),
           g_perWindowShellBehaviorHooksInstalled.load(std::memory_order_relaxed));
    if (g_build == BuildKind::Win10) {
        Wh_Log(L"[mouse-7-vis] totals SetVisible=%lu",
               g_setVisibleCalls.load());
        Wh_Log(L"[mouse-7-route] totals acquire=%lu observeStart=%lu raw=%lu uiDown=%lu uiMove=%lu handle=%lu invDown=%lu invMove=%lu invStart=%lu managerStart=%lu wndMouse=%lu isMousePen=%lu onDown=%lu onMove=%lu moveToEdge=%lu managerHit=%lu invHit=%lu titleDown=%lu titleMove=%lu titleStart=%lu titleInvoke=%lu",
               g_acquire.load(),
               g_observeStart.load(),
               g_raw.load(),
               g_uiDown.load(),
               g_uiMove.load(),
               g_handle.load(),
               g_invokerDown.load(),
               g_invokerMove.load(),
               g_invokerStart.load(),
               g_managerStart.load(),
               g_wndProcMouse.load(),
               g_isMousePen.load(),
               g_onMouseDown.load(),
               g_onMouseMove.load(),
               g_moveToEdge.load(),
               g_managerHit.load(),
               g_invokerHit.load(),
               g_titleDown.load(),
               g_titleMove.load(),
               g_titleStart.load(),
               g_titleInvoke.load());
        Wh_Log(L"[mouse-7-input] totals init=%lu down=%lu update=%lu up=%lu",
               g_inputInit.load(), g_inputDown.load(), g_inputUpdate.load(), g_inputUp.load());
    }

    Wh_Log(L"[mouse-7] unloaded");
}
