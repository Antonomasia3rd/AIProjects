// ==WindhawkMod==
// @id              center-window-geometry
// @name            Center Windows + Geometry
// @description     Centers newly opened windows and shows X/Y/width/height while moving or resizing.
// @version         0.1
// @author          Local
// @include         windhawk.exe
// @compilerOptions -ldwmapi -lgdi32 -lshell32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Center Windows + Geometry

A lightweight, out-of-process window helper.

Features:

* Centers newly opened normal top-level windows.
* Uses the monitor work area by default.
* Shows live X/Y and width/height while moving or resizing.
* Uses Windows WinEvent hooks instead of injecting into target applications.
* Runs as a dedicated Windhawk tool process.

The geometry uses DWM extended frame bounds when available, so the displayed
dimensions correspond to the visible window frame rather than Windows'
invisible resize borders.

## Limitations

Elevated windows may not always be movable from the normal-user tool process.
Some applications also reposition their own windows after Windows reports that
they have been shown, which can override automatic centering.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- centerNewWindows: true
  $name: Center newly opened windows
  $description: Automatically center newly created normal top-level windows.

- showGeometry: true
  $name: Show window geometry
  $description: Show X/Y and width/height while moving or resizing a window.

- centerOwnedWindows: false
  $name: Center owned/dialog windows
  $description: Also center owned windows such as many dialog boxes.

- useWorkArea: true
  $name: Respect taskbar/work area
  $description: Center within the usable monitor work area instead of the complete monitor rectangle.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <stdio.h>

#include <atomic>
#include <unordered_set>

struct Settings {
    bool centerNewWindows;
    bool showGeometry;
    bool centerOwnedWindows;
    bool useWorkArea;
};

Settings g_settings{};

HANDLE g_workerThread = nullptr;
std::atomic<HWND> g_overlayWindow{nullptr};

HWINEVENTHOOK g_objectHook = nullptr;
HWINEVENTHOOK g_moveSizeHook = nullptr;
HWINEVENTHOOK g_locationHook = nullptr;

HWND g_dragWindow = nullptr;

std::unordered_set<HWND> g_knownWindows;
std::unordered_set<HWND> g_newWindows;

WCHAR g_overlayText[128] = {};

constexpr WCHAR kOverlayClass[] = L"WindhawkCenterGeometryOverlay";

constexpr UINT WM_APP_CENTER_WINDOW =
    WM_APP + 1;

constexpr UINT WM_APP_RELOAD_SETTINGS =
    WM_APP + 2;


// -----------------------------------------------------------------------------
// Settings
// -----------------------------------------------------------------------------

void LoadSettings() {
    g_settings.centerNewWindows =
        Wh_GetIntSetting(L"centerNewWindows") != 0;

    g_settings.showGeometry =
        Wh_GetIntSetting(L"showGeometry") != 0;

    g_settings.centerOwnedWindows =
        Wh_GetIntSetting(L"centerOwnedWindows") != 0;

    g_settings.useWorkArea =
        Wh_GetIntSetting(L"useWorkArea") != 0;
}


// -----------------------------------------------------------------------------
// Window helpers
// -----------------------------------------------------------------------------

bool GetVisibleWindowRect(HWND hwnd, RECT* rect) {
    if (!GetWindowRect(hwnd, rect)) {
        return false;
    }

    RECT dwmRect;

    if (SUCCEEDED(DwmGetWindowAttribute(
            hwnd,
            DWMWA_EXTENDED_FRAME_BOUNDS,
            &dwmRect,
            sizeof(dwmRect)))) {
        *rect = dwmRect;
    }

    return true;
}


bool IsCloaked(HWND hwnd) {
    DWORD cloaked = 0;

    if (SUCCEEDED(DwmGetWindowAttribute(
            hwnd,
            DWMWA_CLOAKED,
            &cloaked,
            sizeof(cloaked)))) {
        return cloaked != 0;
    }

    return false;
}


bool IsShellWindowClass(HWND hwnd) {
    WCHAR className[128];

    if (!GetClassNameW(hwnd, className, ARRAYSIZE(className))) {
        return false;
    }

    return
        wcscmp(className, L"Progman") == 0 ||
        wcscmp(className, L"WorkerW") == 0 ||
        wcscmp(className, L"Shell_TrayWnd") == 0 ||
        wcscmp(className, L"Shell_SecondaryTrayWnd") == 0 ||
        wcscmp(className, L"NotifyIconOverflowWindow") == 0;
}


bool IsRootWindow(HWND hwnd) {
    return hwnd &&
           GetAncestor(hwnd, GA_ROOT) == hwnd;
}


bool IsGeometryEligible(HWND hwnd) {
    if (!IsWindow(hwnd) ||
        !IsRootWindow(hwnd)) {
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (pid == GetCurrentProcessId()) {
        return false;
    }

    return true;
}


bool IsCenterEligible(HWND hwnd) {
    if (!IsWindow(hwnd) ||
        !IsWindowVisible(hwnd) ||
        !IsRootWindow(hwnd)) {
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (pid == 0 ||
        pid == GetCurrentProcessId()) {
        return false;
    }

    LONG_PTR style =
        GetWindowLongPtrW(hwnd, GWL_STYLE);

    LONG_PTR exStyle =
        GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    if (style & WS_CHILD) {
        return false;
    }

    if (exStyle & WS_EX_TOOLWINDOW) {
        return false;
    }

    if (!g_settings.centerOwnedWindows &&
        GetWindow(hwnd, GW_OWNER)) {
        return false;
    }

    if (IsIconic(hwnd) ||
        IsZoomed(hwnd) ||
        IsCloaked(hwnd) ||
        IsShellWindowClass(hwnd)) {
        return false;
    }

    RECT visibleRect;

    if (!GetVisibleWindowRect(hwnd, &visibleRect)) {
        return false;
    }

    LONG width =
        visibleRect.right - visibleRect.left;

    LONG height =
        visibleRect.bottom - visibleRect.top;

    // Ignore microscopic helper windows.
    if (width < 80 || height < 50) {
        return false;
    }

    HMONITOR monitor =
        MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    MONITORINFO mi{
        sizeof(mi)
    };

    if (!GetMonitorInfoW(monitor, &mi)) {
        return false;
    }

    LONG monitorWidth =
        mi.rcMonitor.right - mi.rcMonitor.left;

    LONG monitorHeight =
        mi.rcMonitor.bottom - mi.rcMonitor.top;

    // Avoid messing with borderless/fullscreen windows.
    if (width >= monitorWidth - 2 &&
        height >= monitorHeight - 2) {
        return false;
    }

    return true;
}


// -----------------------------------------------------------------------------
// Centering
// -----------------------------------------------------------------------------

void CenterWindow(HWND hwnd) {
    if (!g_settings.centerNewWindows ||
        !IsCenterEligible(hwnd)) {
        return;
    }

    RECT windowRect;

    if (!GetWindowRect(hwnd, &windowRect)) {
        return;
    }

    RECT visibleRect;

    if (!GetVisibleWindowRect(hwnd, &visibleRect)) {
        return;
    }

    HMONITOR monitor =
        MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    MONITORINFO mi{
        sizeof(mi)
    };

    if (!GetMonitorInfoW(monitor, &mi)) {
        return;
    }

    RECT area =
        g_settings.useWorkArea
            ? mi.rcWork
            : mi.rcMonitor;

    LONG visibleWidth =
        visibleRect.right - visibleRect.left;

    LONG visibleHeight =
        visibleRect.bottom - visibleRect.top;

    LONG areaWidth =
        area.right - area.left;

    LONG areaHeight =
        area.bottom - area.top;

    // Don't attempt to center windows larger than their target area.
    if (visibleWidth > areaWidth ||
        visibleHeight > areaHeight) {
        return;
    }

    LONG desiredVisibleX =
        area.left +
        (areaWidth - visibleWidth) / 2;

    LONG desiredVisibleY =
        area.top +
        (areaHeight - visibleHeight) / 2;

    // DWM visible bounds can differ from GetWindowRect because Windows
    // keeps invisible resize borders around many normal windows.
    LONG invisibleLeft =
        visibleRect.left - windowRect.left;

    LONG invisibleTop =
        visibleRect.top - windowRect.top;

    LONG desiredWindowX =
        desiredVisibleX - invisibleLeft;

    LONG desiredWindowY =
        desiredVisibleY - invisibleTop;

    SetWindowPos(
        hwnd,
        nullptr,
        desiredWindowX,
        desiredWindowY,
        0,
        0,
        SWP_NOSIZE |
        SWP_NOZORDER |
        SWP_NOACTIVATE |
        SWP_ASYNCWINDOWPOS);
}


// -----------------------------------------------------------------------------
// Geometry overlay
// -----------------------------------------------------------------------------

void HideGeometryOverlay() {
    HWND overlay = g_overlayWindow.load();

    if (overlay) {
        ShowWindow(overlay, SW_HIDE);
    }
}


void UpdateGeometryOverlay(HWND hwnd) {
    if (!g_settings.showGeometry ||
        !IsGeometryEligible(hwnd)) {
        HideGeometryOverlay();
        return;
    }

    RECT rect;

    if (!GetVisibleWindowRect(hwnd, &rect)) {
        HideGeometryOverlay();
        return;
    }

    LONG width =
        rect.right - rect.left;

    LONG height =
        rect.bottom - rect.top;

    swprintf_s(
        g_overlayText,
        L"%ld \u00D7 %ld\r\nX: %ld    Y: %ld",
        width,
        height,
        rect.left,
        rect.top);

    HWND overlay =
        g_overlayWindow.load();

    if (!overlay) {
        return;
    }

    constexpr int overlayWidth = 190;
    constexpr int overlayHeight = 52;
    constexpr int cursorOffsetX = 18;
    constexpr int cursorOffsetY = 24;

    POINT cursor;

    if (!GetCursorPos(&cursor)) {
        return;
    }

    HMONITOR monitor =
        MonitorFromPoint(
            cursor,
            MONITOR_DEFAULTTONEAREST);

    MONITORINFO mi{
        sizeof(mi)
    };

    if (!GetMonitorInfoW(monitor, &mi)) {
        return;
    }

    int x =
        cursor.x + cursorOffsetX;

    int y =
        cursor.y + cursorOffsetY;

    if (x + overlayWidth > mi.rcWork.right) {
        x =
            cursor.x -
            overlayWidth -
            cursorOffsetX;
    }

    if (y + overlayHeight > mi.rcWork.bottom) {
        y =
            cursor.y -
            overlayHeight -
            cursorOffsetY;
    }

    if (x < mi.rcWork.left) {
        x = mi.rcWork.left;
    }

    if (y < mi.rcWork.top) {
        y = mi.rcWork.top;
    }

    SetWindowPos(
        overlay,
        HWND_TOPMOST,
        x,
        y,
        overlayWidth,
        overlayHeight,
        SWP_NOACTIVATE |
        SWP_SHOWWINDOW);

    InvalidateRect(
        overlay,
        nullptr,
        TRUE);
}


// -----------------------------------------------------------------------------
// WinEvent callbacks
// -----------------------------------------------------------------------------

void CALLBACK WinEventProc(
    HWINEVENTHOOK hook,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD eventThread,
    DWORD eventTime) {

    if (!hwnd) {
        return;
    }

    switch (event) {
        case EVENT_SYSTEM_MOVESIZESTART:
            if (g_settings.showGeometry &&
                IsGeometryEligible(hwnd)) {
                g_dragWindow = hwnd;
                UpdateGeometryOverlay(hwnd);
            }
            break;

        case EVENT_SYSTEM_MOVESIZEEND:
            if (hwnd == g_dragWindow) {
                UpdateGeometryOverlay(hwnd);
                HideGeometryOverlay();
                g_dragWindow = nullptr;
            }
            break;

        case EVENT_OBJECT_LOCATIONCHANGE:
            if (idObject == OBJID_WINDOW &&
                idChild == CHILDID_SELF &&
                hwnd == g_dragWindow) {
                UpdateGeometryOverlay(hwnd);
            }
            break;

        case EVENT_OBJECT_CREATE:
            if (idObject == OBJID_WINDOW &&
                idChild == CHILDID_SELF &&
                IsRootWindow(hwnd)) {
                g_knownWindows.insert(hwnd);
                g_newWindows.insert(hwnd);
            }
            break;

        case EVENT_OBJECT_DESTROY:
            if (idObject == OBJID_WINDOW &&
                idChild == CHILDID_SELF) {
                g_knownWindows.erase(hwnd);
                g_newWindows.erase(hwnd);

                if (g_dragWindow == hwnd) {
                    g_dragWindow = nullptr;
                    HideGeometryOverlay();
                }
            }
            break;

        case EVENT_OBJECT_SHOW:
            if (idObject != OBJID_WINDOW ||
                idChild != CHILDID_SELF ||
                !IsRootWindow(hwnd)) {
                break;
            }

            // CREATE can occasionally be missed depending on how a
            // framework exposes its window. A completely unknown HWND
            // which suddenly appears is also treated as new.
            if (!g_knownWindows.contains(hwnd)) {
                g_knownWindows.insert(hwnd);
                g_newWindows.insert(hwnd);
            }

            if (g_newWindows.erase(hwnd) != 0) {
                HWND overlay =
                    g_overlayWindow.load();

                if (overlay) {
                    PostMessageW(
                        overlay,
                        WM_APP_CENTER_WINDOW,
                        reinterpret_cast<WPARAM>(hwnd),
                        0);
                }
            }

            break;
    }
}


// -----------------------------------------------------------------------------
// Overlay window
// -----------------------------------------------------------------------------

LRESULT CALLBACK OverlayWndProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {

    switch (message) {
        case WM_APP_CENTER_WINDOW:
            CenterWindow(
                reinterpret_cast<HWND>(wParam));
            return 0;

        case WM_APP_RELOAD_SETTINGS:
            LoadSettings();

            if (!g_settings.showGeometry) {
                g_dragWindow = nullptr;
                HideGeometryOverlay();
            }

            return 0;

        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc =
                BeginPaint(hwnd, &ps);

            RECT client;
            GetClientRect(hwnd, &client);

            HBRUSH background =
                CreateSolidBrush(RGB(24, 24, 24));

            FillRect(
                dc,
                &client,
                background);

            DeleteObject(background);

            SetBkMode(
                dc,
                TRANSPARENT);

            SetTextColor(
                dc,
                RGB(245, 245, 245));

            HFONT oldFont =
                static_cast<HFONT>(
                    SelectObject(
                        dc,
                        GetStockObject(DEFAULT_GUI_FONT)));

            RECT textRect = client;

            textRect.left += 8;
            textRect.right -= 8;
            textRect.top += 5;
            textRect.bottom -= 5;

            DrawTextW(
                dc,
                g_overlayText,
                -1,
                &textRect,
                DT_CENTER |
                DT_VCENTER |
                DT_WORDBREAK |
                DT_NOPREFIX);

            SelectObject(
                dc,
                oldFont);

            EndPaint(
                hwnd,
                &ps);

            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_objectHook) {
                UnhookWinEvent(g_objectHook);
                g_objectHook = nullptr;
            }

            if (g_moveSizeHook) {
                UnhookWinEvent(g_moveSizeHook);
                g_moveSizeHook = nullptr;
            }

            if (g_locationHook) {
                UnhookWinEvent(g_locationHook);
                g_locationHook = nullptr;
            }

            g_overlayWindow.store(nullptr);

            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam);
}


// -----------------------------------------------------------------------------
// Initial window inventory
// -----------------------------------------------------------------------------

BOOL CALLBACK EnumExistingWindowsProc(
    HWND hwnd,
    LPARAM lParam) {

    if (IsRootWindow(hwnd)) {
        g_knownWindows.insert(hwnd);
    }

    return TRUE;
}


// -----------------------------------------------------------------------------
// Worker thread
// -----------------------------------------------------------------------------

DWORD WINAPI WorkerThreadProc(LPVOID) {
    LoadSettings();

    HINSTANCE instance =
        GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kOverlayClass;

    if (!RegisterClassW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Wh_Log(L"RegisterClass failed");
        return 1;
    }

    HWND overlay =
        CreateWindowExW(
            WS_EX_TOPMOST |
            WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE |
            WS_EX_LAYERED |
            WS_EX_TRANSPARENT,
            kOverlayClass,
            L"",
            WS_POPUP,
            0,
            0,
            190,
            52,
            nullptr,
            nullptr,
            instance,
            nullptr);

    if (!overlay) {
        Wh_Log(L"CreateWindowEx failed");
        UnregisterClassW(
            kOverlayClass,
            instance);
        return 1;
    }

    g_overlayWindow.store(overlay);

    SetLayeredWindowAttributes(
        overlay,
        0,
        235,
        LWA_ALPHA);

    // Mark every window which already existed when the tool started.
    // Those windows should not later be mistaken for newly opened ones.
    EnumWindows(
        EnumExistingWindowsProc,
        0);

    constexpr DWORD eventFlags =
        WINEVENT_OUTOFCONTEXT |
        WINEVENT_SKIPOWNPROCESS;

    g_objectHook =
        SetWinEventHook(
            EVENT_OBJECT_CREATE,
            EVENT_OBJECT_SHOW,
            nullptr,
            WinEventProc,
            0,
            0,
            eventFlags);

    g_moveSizeHook =
        SetWinEventHook(
            EVENT_SYSTEM_MOVESIZESTART,
            EVENT_SYSTEM_MOVESIZEEND,
            nullptr,
            WinEventProc,
            0,
            0,
            eventFlags);

    g_locationHook =
        SetWinEventHook(
            EVENT_OBJECT_LOCATIONCHANGE,
            EVENT_OBJECT_LOCATIONCHANGE,
            nullptr,
            WinEventProc,
            0,
            0,
            eventFlags);

    if (!g_objectHook ||
        !g_moveSizeHook ||
        !g_locationHook) {
        Wh_Log(L"One or more SetWinEventHook calls failed");

        DestroyWindow(overlay);
    }

    MSG msg;

    while (GetMessageW(
               &msg,
               nullptr,
               0,
               0) > 0) {

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (IsWindow(overlay)) {
        DestroyWindow(overlay);
    }

    UnregisterClassW(
        kOverlayClass,
        instance);

    g_knownWindows.clear();
    g_newWindows.clear();

    return 0;
}


// -----------------------------------------------------------------------------
// Tool-mod callbacks
// -----------------------------------------------------------------------------

BOOL WhTool_ModInit() {
    g_workerThread =
        CreateThread(
            nullptr,
            0,
            WorkerThreadProc,
            nullptr,
            0,
            nullptr);

    return g_workerThread != nullptr;
}


void WhTool_ModSettingsChanged() {
    HWND overlay =
        g_overlayWindow.load();

    if (overlay) {
        PostMessageW(
            overlay,
            WM_APP_RELOAD_SETTINGS,
            0,
            0);
    }
}


void WhTool_ModUninit() {
    HWND overlay =
        g_overlayWindow.load();

    if (overlay) {
        PostMessageW(
            overlay,
            WM_CLOSE,
            0,
            0);
    }

    if (g_workerThread) {
        WaitForSingleObject(
            g_workerThread,
            INFINITE);

        CloseHandle(
            g_workerThread);

        g_workerThread = nullptr;
    }
}


// =============================================================================
// Windhawk tool-mod launcher.
//
// This makes the actual mod run in a dedicated windhawk.exe process instead of
// attaching the functionality to Explorer or applications.
// =============================================================================

bool g_isToolModProcessLauncher;
HANDLE g_toolModProcessMutex;


void WINAPI EntryPoint_Hook() {
    Wh_Log(L">");
    ExitThread(0);
}


BOOL Wh_ModInit() {
    DWORD sessionId;

    if (ProcessIdToSessionId(
            GetCurrentProcessId(),
            &sessionId) &&
        sessionId == 0) {
        return FALSE;
    }

    bool isExcluded = false;
    bool isToolModProcess = false;
    bool isCurrentToolModProcess = false;

    int argc;

    LPWSTR* argv =
        CommandLineToArgvW(
            GetCommandLineW(),
            &argc);

    if (!argv) {
        Wh_Log(L"CommandLineToArgvW failed");
        return FALSE;
    }

    for (int i = 1;
         i < argc;
         i++) {

        if (wcscmp(argv[i], L"-service") == 0 ||
            wcscmp(argv[i], L"-service-start") == 0 ||
            wcscmp(argv[i], L"-service-stop") == 0) {

            isExcluded = true;
            break;
        }
    }

    for (int i = 1;
         i < argc - 1;
         i++) {

        if (wcscmp(
                argv[i],
                L"-tool-mod") == 0) {

            isToolModProcess = true;

            if (wcscmp(
                    argv[i + 1],
                    WH_MOD_ID) == 0) {
                isCurrentToolModProcess = true;
            }

            break;
        }
    }

    LocalFree(argv);

    if (isExcluded) {
        return FALSE;
    }

    if (isCurrentToolModProcess) {
        g_toolModProcessMutex =
            CreateMutexW(
                nullptr,
                TRUE,
                L"windhawk-tool-mod_" WH_MOD_ID);

        if (!g_toolModProcessMutex) {
            Wh_Log(L"CreateMutex failed");
            ExitProcess(1);
        }

        if (GetLastError() ==
            ERROR_ALREADY_EXISTS) {

            Wh_Log(
                L"Tool mod already running (%s)",
                WH_MOD_ID);

            ExitProcess(1);
        }

        if (!WhTool_ModInit()) {
            ExitProcess(1);
        }

        IMAGE_DOS_HEADER* dosHeader =
            reinterpret_cast<IMAGE_DOS_HEADER*>(
                GetModuleHandleW(nullptr));

        IMAGE_NT_HEADERS* ntHeaders =
            reinterpret_cast<IMAGE_NT_HEADERS*>(
                reinterpret_cast<BYTE*>(dosHeader) +
                dosHeader->e_lfanew);

        DWORD entryPointRVA =
            ntHeaders->OptionalHeader.AddressOfEntryPoint;

        void* entryPoint =
            reinterpret_cast<BYTE*>(dosHeader) +
            entryPointRVA;

        Wh_SetFunctionHook(
            entryPoint,
            reinterpret_cast<void*>(EntryPoint_Hook),
            nullptr);

        return TRUE;
    }

    if (isToolModProcess) {
        return FALSE;
    }

    g_isToolModProcessLauncher = true;
    return TRUE;
}


void Wh_ModAfterInit() {
    if (!g_isToolModProcessLauncher) {
        return;
    }

    WCHAR currentProcessPath[MAX_PATH];

    switch (GetModuleFileNameW(
        nullptr,
        currentProcessPath,
        ARRAYSIZE(currentProcessPath))) {

        case 0:
        case ARRAYSIZE(currentProcessPath):
            Wh_Log(L"GetModuleFileName failed");
            return;
    }

    WCHAR commandLine[
        MAX_PATH + 2 +
        (sizeof(
            L" -tool-mod \"" WH_MOD_ID "\"") /
             sizeof(WCHAR)) -
        1];

    swprintf_s(
        commandLine,
        L"\"%s\" -tool-mod \"%s\"",
        currentProcessPath,
        WH_MOD_ID);

    HMODULE kernelModule =
        GetModuleHandleW(
            L"kernelbase.dll");

    if (!kernelModule) {
        kernelModule =
            GetModuleHandleW(
                L"kernel32.dll");

        if (!kernelModule) {
            Wh_Log(
                L"No kernelbase.dll/kernel32.dll");
            return;
        }
    }

    using CreateProcessInternalW_t =
        BOOL(WINAPI*)(
            HANDLE,
            LPCWSTR,
            LPWSTR,
            LPSECURITY_ATTRIBUTES,
            LPSECURITY_ATTRIBUTES,
            WINBOOL,
            DWORD,
            LPVOID,
            LPCWSTR,
            LPSTARTUPINFOW,
            LPPROCESS_INFORMATION,
            PHANDLE);

    auto pCreateProcessInternalW =
        reinterpret_cast<CreateProcessInternalW_t>(
            GetProcAddress(
                kernelModule,
                "CreateProcessInternalW"));

    if (!pCreateProcessInternalW) {
        Wh_Log(
            L"No CreateProcessInternalW");
        return;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_FORCEOFFFEEDBACK;

    PROCESS_INFORMATION pi{};

    if (!pCreateProcessInternalW(
            nullptr,
            currentProcessPath,
            commandLine,
            nullptr,
            nullptr,
            FALSE,
            NORMAL_PRIORITY_CLASS,
            nullptr,
            nullptr,
            &si,
            &pi,
            nullptr)) {

        Wh_Log(
            L"CreateProcess failed");
        return;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}


void Wh_ModSettingsChanged() {
    if (g_isToolModProcessLauncher) {
        return;
    }

    WhTool_ModSettingsChanged();
}


void Wh_ModUninit() {
    if (g_isToolModProcessLauncher) {
        return;
    }

    WhTool_ModUninit();

    ExitProcess(0);
}