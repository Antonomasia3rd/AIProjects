// CharmTray.cpp  — corrected
// Tray icon launcher for Windows 8/8.1 immersive shell charm flyouts.
// GUIDs and vtable offsets verified by disassembly of CharmBar.exe.
//
// Build (x64, MSVC v142 / VS Build Tools 2019):
//   cl /std:c++17 /EHsc /O2 /W3 /MT
//      /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0602
//      CharmTray.cpp
//      /link user32.lib ole32.lib shell32.lib
//
// Only runs on Windows 8 / 8.1 — ImmersiveShell does not exist on Win10+.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>

#include "../../dependencies/desktop_app_baseline.h"

// ---------------------------------------------------------------------------
// Tray / menu constants
// ---------------------------------------------------------------------------
#define WM_TRAY_ICON   (WM_USER + 1)
#define IDI_TRAY        1
#define ID_SEARCH       100
#define ID_SHARE        101
#define ID_START        102
#define ID_DEVICES      103
#define ID_SETTINGS     104
#define ID_EXIT         105
#define CHARM_SEARCH    0
#define CHARM_SHARE     1
#define CHARM_START     2
#define CHARM_DEVICES   3
#define CHARM_SETTINGS  4

static aip::SidecarPaths g_paths;
static aip::InstanceIdentity g_instanceIdentity;
static aip::Utf8Logger g_logger;
static UINT g_taskbarCreated = 0;
static bool g_trayCreated = false;
static const wchar_t kWindowClass[] = L"CharmTrayWnd";

static void LogLine(const wchar_t* message)
{
    g_logger.Write(L"info", message != nullptr ? message : L"");
}

static void LogHResult(const wchar_t* operation, HRESULT result)
{
    wchar_t code[32] = {};
    swprintf_s(code, L"0x%08X", static_cast<unsigned int>(result));
    LogLine((std::wstring(operation) + L" failed (" + code + L"): " +
        aip::GetLastErrorText(static_cast<DWORD>(result))).c_str());
}

static void ShowStartupError(const std::wstring& message)
{
    LogLine(message.c_str());
    MessageBoxW(nullptr, message.c_str(), L"CharmTray", MB_OK | MB_ICONERROR);
}

static bool IsSupportedWindowsVersion()
{
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto getVersion = ntdll == nullptr ? nullptr :
        reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (getVersion == nullptr)
    {
        return false;
    }

    OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    return getVersion(&version) >= 0 &&
        version.dwMajorVersion == 6 &&
        (version.dwMinorVersion == 2 || version.dwMinorVersion == 3);
}

static bool InitializeSettings()
{
    g_paths = aip::BuildCurrentProcessSidecarPaths(L"CharmTray");
    g_instanceIdentity = aip::BuildPathScopedInstanceIdentity(
        L"CharmTray",
        L"CharmTray.Message",
        kWindowClass,
        L"CharmTray",
        g_paths.exePath);

    const aip::IniDefaultValue defaults[] =
    {
        { L"Settings", L"LoggingEnabled", L"1" }
    };
    aip::IniConfigStore config(
        g_paths.configPath,
        L"; CharmTray settings\r\n",
        5000);
    if (!config.EnsureDefaults(defaults, ARRAYSIZE(defaults)))
    {
        ShowStartupError(
            L"Could not create or update the settings file:\r\n" +
            g_paths.configPath + L"\r\n\r\n" +
            aip::GetLastErrorText(GetLastError()));
        return false;
    }

    bool loggingEnabled = true;
    std::wstring raw = config.ReadRaw(L"Settings", L"LoggingEnabled", L"1");
    if (!aip::ParseBoolValue(raw, loggingEnabled))
    {
        ShowStartupError(
            L"Invalid [Settings] LoggingEnabled value in:\r\n" +
            g_paths.configPath + L"\r\n\r\nUse 0/1, true/false, yes/no, or on/off.");
        return false;
    }

    aip::Utf8LoggerOptions options;
    options.enabled = true;
    options.fileEnabled = loggingEnabled;
    options.filePath = g_paths.defaultLogPath;
    options.lockWaitMs = 5000;
    g_logger.Configure(options);
    return true;
}

// ---------------------------------------------------------------------------
// GUIDs — all verified from CharmBar.exe disassembly.
// IMPORTANT: QueryService(this, rdx=SID, r8=IID, r9=ppv)
//            rdx and r8 are the REVERSE of what you might expect from docs.
// ---------------------------------------------------------------------------

// CoCreateInstance: CLSID_ImmersiveShell
static const CLSID CLSID_ImmersiveShell =
    {0xc2f03a33,0x21f5,0x47fa,{0xb4,0xbb,0x15,0x63,0x62,0xa2,0xf2,0x39}};

// IServiceProvider IID — also used as IID in DevSett level-1 query
static const IID IID_IServiceProvider_ =
    {0x6d5140c1,0x7436,0x11ce,{0x80,0x34,0x00,0xaa,0x00,0x60,0x09,0xfa}};

// ---- Search ----------------------------------------------------------------
// QueryService: rdx(SID)={62b07e68}, r8(IID)={ce05d854}
// Show: vtable[3](pPlacement, NULL, NULL)
static const GUID SID_SearchPane =
    {0x62b07e68,0x89e3,0x4073,{0xac,0xc4,0x06,0xf9,0x6f,0x0d,0xda,0x86}};
static const GUID IID_ISearchPane =
    {0xce05d854,0xa9d8,0x481b,{0x98,0x07,0x4e,0x67,0x53,0x4b,0x33,0xcd}};

// ---- Share -----------------------------------------------------------------
// QueryService: rdx(SID)={768bdc50}, r8(IID)={00d0f427}
static const GUID SID_IShareFlyout =
    {0x768bdc50,0xd256,0x4ce5,{0x8d,0x32,0x69,0x9e,0xc0,0xc9,0xdc,0x3f}};

// ---- Start -----------------------------------------------------------------
// QueryService: rdx(SID)={705f0106}, r8(IID)={00d0f427}
static const GUID SID_IStartFlyout =
    {0x705f0106,0x9f11,0x4960,{0xab,0xd0,0x13,0x78,0x4f,0xc1,0xf5,0x05}};

// ---- Share / Start / Devices / Settings share this IID --------------------
// IID returned for all three above + both Devices/Settings levels
static const GUID IID_ICharmFlyout =
    {0x00d0f427,0xebb3,0x4304,{0xb6,0xf8,0xae,0x4f,0xa0,0xfa,0x7c,0x20}};

// ---- Devices / Settings (two-level QueryService) ---------------------------
// Level-1:  rdx(SID)={60ed3061}, r8(IID)={6d5140c1} (=IID_IServiceProvider_)
static const GUID SID_DevSettIntermediate =
    {0x60ed3061,0x4e71,0x4282,{0x94,0x94,0x81,0x1d,0x88,0x65,0x64,0x92}};
// Level-2:  rdx(SID)={3b228825}, r8(IID)={00d0f427} (=IID_ICharmFlyout)
static const GUID SID_IDevSettFlyout =
    {0x3b228825,0x95e7,0x4ad9,{0x86,0x16,0x5f,0x94,0xbf,0x6e,0xa1,0xfd}};

// ---- Edge placement helper -------------------------------------------------
// QueryService: rdx(SID)={47094e3a}, r8(IID)={4d4c1e64}
// SetMonitorEdge: vtable[6] = offset 0x30
static const GUID SID_IEdgeUiTracker =
    {0x47094e3a,0x0cf2,0x430f,{0x80,0x6f,0xcf,0x9e,0x4f,0x0f,0x12,0xdd}};
static const GUID IID_IEdgeUiTracker =
    {0x4d4c1e64,0xe410,0x4faa,{0xba,0xfa,0x59,0xca,0x06,0x9b,0xfe,0xc2}};

// ---- Immersive HWND helper chain -------------------------------------------
// Step 1 - QueryService: rdx(SID)={bf63999f}, r8(IID)={bf63999f}  (SID==IID)
//          vtable[4] = offset 0x20 = GetHostContainer(IUnknown** ppContainer)
static const GUID GUID_IShellHostObject =
    {0xbf63999f,0x7411,0x40da,{0x86,0x1c,0xdf,0x72,0xc0,0xff,0xee,0x84}};
// Step 2 - QueryService on container: rdx=NULL(xor edx,edx), r8={c6636ec2}
//          vtable[4] = offset 0x20 = GetHwnd(HWND* pHwnd)
static const GUID IID_IHwndProvider =
    {0xc6636ec2,0xeba1,0x4e6d,{0xa9,0x95,0x8f,0xa1,0x4b,0x8b,0x28,0x91}};

// ---------------------------------------------------------------------------
// COM interface stubs
// vtable layout derived from disassembly; _padN slots are never called.
// All Show() methods correspond to vtable[3] = offset 0x18.
// ---------------------------------------------------------------------------

struct IServiceProvider_ : IUnknown
{
    // vtable[3] offset 0x18
    virtual HRESULT STDMETHODCALLTYPE QueryService(
        REFGUID guidService, REFIID riid, void** ppv) = 0;
};

// Search pane — Show takes 3 args after `this`
struct ISearchPane : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Show(  // vtable[3]
        IUnknown* pPlacement,
        void*     reserved1,
        void*     reserved2) = 0;
};

// Share / Start / Devices / Settings — Show takes 2 args after `this`
struct ICharmFlyout : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Show(  // vtable[3]
        IUnknown* pPlacement,
        HWND      hwnd) = 0;
};

// IEdgeUiTracker: slots 3-5 are unknown; slot 6 = SetMonitorEdge
struct IEdgeUiTracker : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE _pad3() = 0;
    virtual HRESULT STDMETHODCALLTYPE _pad4() = 0;
    virtual HRESULT STDMETHODCALLTYPE _pad5() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMonitorEdge( // vtable[6] offset 0x30
        HMONITOR   hMonitor,
        IUnknown** ppPlacement) = 0;
};

// IShellHostObject: slot 4 = GetHostContainer
struct IShellHostObject : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE _pad3() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetHostContainer( // vtable[4] offset 0x20
        IUnknown** ppContainer) = 0;
};

// IHwndProvider: slot 4 = GetHwnd
struct IHwndProvider : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE _pad3() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetHwnd( // vtable[4] offset 0x20
        HWND* pHwnd) = 0;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool GetShellSP(IServiceProvider_** shell)
{
    if (shell == nullptr)
    {
        return false;
    }
    *shell = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_ImmersiveShell,
        nullptr,
        CLSCTX_LOCAL_SERVER,
        IID_IServiceProvider_,
        reinterpret_cast<void**>(shell));
    if (FAILED(result) || *shell == nullptr)
    {
        LogHResult(L"CoCreateInstance(CLSID_ImmersiveShell)", result);
        return false;
    }
    return true;
}

// func_0x1740 — get edge placement for the current cursor monitor
static bool GetEdgePlacement(IServiceProvider_* pSP, IUnknown** placement)
{
    if (pSP == nullptr || placement == nullptr)
    {
        return false;
    }
    *placement = nullptr;

    IEdgeUiTracker* pTracker = nullptr;
    // rdx=SID_IEdgeUiTracker, r8=IID_IEdgeUiTracker  (verified from disasm)
    HRESULT result = pSP->QueryService(
        SID_IEdgeUiTracker,
        IID_IEdgeUiTracker,
        reinterpret_cast<void**>(&pTracker));
    if (FAILED(result) || pTracker == nullptr)
    {
        LogHResult(L"QueryService(IEdgeUiTracker)", result);
        return false;
    }

    POINT pt = {};
    if (!GetCursorPos(&pt))
    {
        LogLine(L"GetCursorPos failed while selecting the charm monitor.");
        pTracker->Release();
        return false;
    }
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (hMon == nullptr)
    {
        LogLine(L"MonitorFromPoint did not return a monitor.");
        pTracker->Release();
        return false;
    }

    result = pTracker->SetMonitorEdge(hMon, placement);
    pTracker->Release();
    if (FAILED(result) || *placement == nullptr)
    {
        LogHResult(L"IEdgeUiTracker::SetMonitorEdge", result);
        return false;
    }
    return true;
}

// func_0x1670 — get immersive shell HWND (desktop window as fallback)
static HWND GetImmersiveHWND(IServiceProvider_* pSP)
{
    HWND hwnd = GetDesktopWindow();
    if (!pSP) return hwnd;

    // Step 1: QueryService(SID=IID=GUID_IShellHostObject) → IShellHostObject
    // NOTE: SID == IID here (bf63999f) — verified from disasm
    IShellHostObject* pShellHost = nullptr;
    HRESULT result = pSP->QueryService(
        GUID_IShellHostObject,
        GUID_IShellHostObject,
        reinterpret_cast<void**>(&pShellHost));
    if (FAILED(result) || pShellHost == nullptr)
    {
        LogHResult(L"QueryService(IShellHostObject)", result);
        return hwnd;
    }

    // Step 2: vtable[4] = GetHostContainer
    IUnknown* pContainer = nullptr;
    result = pShellHost->GetHostContainer(&pContainer);
    pShellHost->Release();
    if (FAILED(result) || pContainer == nullptr)
    {
        LogHResult(L"IShellHostObject::GetHostContainer", result);
        return hwnd;
    }

    // Step 3: QueryService on container: SID=NULL, IID=IHwndProvider
    IServiceProvider_* pContainerSP = nullptr;
    result = pContainer->QueryInterface(
        IID_IServiceProvider_,
        reinterpret_cast<void**>(&pContainerSP));
    if (SUCCEEDED(result) && pContainerSP != nullptr)
    {
        IHwndProvider* pHwndProv = nullptr;
        // rdx=NULL (xor edx,edx), r8=IID_IHwndProvider — verified from disasm
        result = pContainerSP->QueryService(
            GUID_NULL,
            IID_IHwndProvider,
            reinterpret_cast<void**>(&pHwndProv));
        if (SUCCEEDED(result) && pHwndProv != nullptr)
        {
            HWND immersiveHwnd = nullptr;
            result = pHwndProv->GetHwnd(&immersiveHwnd);
            if (SUCCEEDED(result) && immersiveHwnd != nullptr)
            {
                hwnd = immersiveHwnd;
            }
            else
            {
                LogHResult(L"IHwndProvider::GetHwnd", result);
            }
            pHwndProv->Release();
        }
        else
        {
            LogHResult(L"QueryService(IHwndProvider)", result);
        }
        pContainerSP->Release();
    }
    else
    {
        LogHResult(L"QueryInterface(IServiceProvider)", result);
    }
    pContainer->Release();
    return hwnd;
}

// ---------------------------------------------------------------------------
// ShowCharm — the core function
// ---------------------------------------------------------------------------
static bool ShowCharm(int charmId)
{
    IServiceProvider_* pSP = nullptr;
    if (!GetShellSP(&pSP))
    {
        return false;
    }

    bool shown = false;
    if (charmId == CHARM_SEARCH)
    {
        // QueryService: rdx(SID)=SID_SearchPane, r8(IID)=IID_ISearchPane
        ISearchPane* pSearch = nullptr;
        HRESULT result = pSP->QueryService(
            SID_SearchPane,
            IID_ISearchPane,
            reinterpret_cast<void**>(&pSearch));
        if (SUCCEEDED(result) && pSearch != nullptr)
        {
            IUnknown* pPlacement = nullptr;
            if (GetEdgePlacement(pSP, &pPlacement))
            {
                result = pSearch->Show(pPlacement, nullptr, nullptr);
                shown = SUCCEEDED(result);
                if (!shown)
                {
                    LogHResult(L"ISearchPane::Show", result);
                }
                pPlacement->Release();
            }
            pSearch->Release();
        }
        else
        {
            LogHResult(L"QueryService(ISearchPane)", result);
        }
    }
    else if (charmId == CHARM_SHARE || charmId == CHARM_START)
    {
        // Share: rdx(SID)=SID_IShareFlyout, r8(IID)=IID_ICharmFlyout
        // Start: rdx(SID)=SID_IStartFlyout, r8(IID)=IID_ICharmFlyout
        const GUID& sid =
            (charmId == CHARM_SHARE) ? SID_IShareFlyout : SID_IStartFlyout;
        ICharmFlyout* pFlyout = nullptr;
        HRESULT result = pSP->QueryService(
            sid,
            IID_ICharmFlyout,
            reinterpret_cast<void**>(&pFlyout));
        if (SUCCEEDED(result) && pFlyout != nullptr)
        {
            IUnknown* pPlacement = nullptr;
            if (GetEdgePlacement(pSP, &pPlacement))
            {
                HWND hwnd = GetImmersiveHWND(pSP);
                result = pFlyout->Show(pPlacement, hwnd);
                shown = SUCCEEDED(result);
                if (!shown)
                {
                    LogHResult(L"ICharmFlyout::Show", result);
                }
                pPlacement->Release();
            }
            pFlyout->Release();
        }
        else
        {
            LogHResult(L"QueryService(ICharmFlyout)", result);
        }
    }
    else if (charmId == CHARM_DEVICES || charmId == CHARM_SETTINGS)
    {
        // Level-1: rdx(SID)=SID_DevSettIntermediate, r8(IID)=IID_IServiceProvider_
        IServiceProvider_* pInterm = nullptr;
        HRESULT result = pSP->QueryService(
            SID_DevSettIntermediate,
            IID_IServiceProvider_,
            reinterpret_cast<void**>(&pInterm));
        if (SUCCEEDED(result) && pInterm != nullptr)
        {
            // Level-2: rdx(SID)=SID_IDevSettFlyout, r8(IID)=IID_ICharmFlyout
            ICharmFlyout* pFlyout = nullptr;
            result = pInterm->QueryService(
                SID_IDevSettFlyout,
                IID_ICharmFlyout,
                reinterpret_cast<void**>(&pFlyout));
            if (SUCCEEDED(result) && pFlyout != nullptr)
            {
                IUnknown* pPlacement = nullptr;
                if (GetEdgePlacement(pSP, &pPlacement))
                {
                    HWND hwnd = GetImmersiveHWND(pSP);
                    result = pFlyout->Show(pPlacement, hwnd);
                    shown = SUCCEEDED(result);
                    if (!shown)
                    {
                        LogHResult(L"ICharmFlyout::Show", result);
                    }
                    pPlacement->Release();
                }
                pFlyout->Release();
            }
            else
            {
                LogHResult(L"QueryService(IDevSettFlyout)", result);
            }
            pInterm->Release();
        }
        else
        {
            LogHResult(L"QueryService(DevSettIntermediate)", result);
        }
    }
    else
    {
        LogLine(L"Rejected an unknown charm command.");
    }

    pSP->Release();
    return shown;
}

// ---------------------------------------------------------------------------
// Tray icon
// ---------------------------------------------------------------------------
static NOTIFYICONDATA g_nid = {};

static bool AddTrayIcon(HWND hwnd)
{
    if (g_trayCreated)
    {
        return true;
    }

    g_nid = {};
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = IDI_TRAY;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    g_nid.hIcon            = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(g_nid.szTip, L"Charm Launcher", _TRUNCATE);
    if (g_nid.hIcon == nullptr || !Shell_NotifyIconW(NIM_ADD, &g_nid))
    {
        return false;
    }

    g_trayCreated = true;
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &g_nid))
    {
        LogLine(L"Shell_NotifyIconW(NIM_SETVERSION) failed.");
    }
    return true;
}

static void RemoveTrayIcon()
{
    if (g_trayCreated)
    {
        if (!Shell_NotifyIconW(NIM_DELETE, &g_nid))
        {
            LogLine(L"Shell_NotifyIconW(NIM_DELETE) failed.");
        }
        g_trayCreated = false;
    }
}

static void ShowTrayFailure()
{
    if (!g_trayCreated)
    {
        return;
    }
    NOTIFYICONDATAW notification = g_nid;
    notification.uFlags = NIF_INFO;
    notification.dwInfoFlags = NIIF_ERROR;
    wcsncpy_s(notification.szInfoTitle, L"Charm Launcher", _TRUNCATE);
    wcsncpy_s(
        notification.szInfo,
        L"The selected charm could not be opened. See the log for details.",
        _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &notification);
}

static void ShowContextMenu(HWND hwnd)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    bool built =
        aip::AppendTrayMenuItem(hMenu, ID_SEARCH, L"Search") &&
        aip::AppendTrayMenuItem(hMenu, ID_SHARE, L"Share") &&
        aip::AppendTrayMenuItem(hMenu, ID_START, L"Start") &&
        aip::AppendTrayMenuItem(hMenu, ID_DEVICES, L"Devices") &&
        aip::AppendTrayMenuItem(hMenu, ID_SETTINGS, L"Settings") &&
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr) != FALSE &&
        aip::AppendTrayMenuItem(hMenu, ID_EXIT, L"Exit");
    if (!built)
    {
        LogLine(L"Failed to build the tray menu.");
        DestroyMenu(hMenu);
        return;
    }

    SetForegroundWindow(hwnd);
    POINT pt = {};
    if (!GetCursorPos(&pt))
    {
        LogLine(L"GetCursorPos failed while opening the tray menu.");
        DestroyMenu(hMenu);
        return;
    }
    UINT command = static_cast<UINT>(TrackPopupMenu(
        hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        pt.x,
        pt.y,
        0,
        hwnd,
        nullptr));
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
    if (command != 0)
    {
        SendMessageW(hwnd, WM_COMMAND, command, 0);
    }
}

// ---------------------------------------------------------------------------
// Window procedure + entry point
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_taskbarCreated != 0 && msg == g_taskbarCreated)
    {
        g_trayCreated = false;
        if (!AddTrayIcon(hwnd))
        {
            LogLine(L"Failed to restore the tray icon after Explorer restarted.");
        }
        return 0;
    }

    switch (msg)
    {
    case WM_TRAY_ICON:
    {
        UINT notification = LOWORD(static_cast<DWORD_PTR>(lp));
        if (notification == WM_CONTEXTMENU ||
            notification == WM_RBUTTONUP ||
            notification == WM_LBUTTONUP ||
            notification == NIN_SELECT ||
            notification == NIN_KEYSELECT)
        {
            ShowContextMenu(hwnd);
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case ID_SEARCH:
            if (!ShowCharm(CHARM_SEARCH)) ShowTrayFailure();
            break;
        case ID_SHARE:
            if (!ShowCharm(CHARM_SHARE)) ShowTrayFailure();
            break;
        case ID_START:
            if (!ShowCharm(CHARM_START)) ShowTrayFailure();
            break;
        case ID_DEVICES:
            if (!ShowCharm(CHARM_DEVICES)) ShowTrayFailure();
            break;
        case ID_SETTINGS:
            if (!ShowCharm(CHARM_SETTINGS)) ShowTrayFailure();
            break;
        case ID_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    if (!IsSupportedWindowsVersion())
    {
        MessageBoxW(
            nullptr,
            L"CharmTray supports only Windows 8 and Windows 8.1.",
            L"CharmTray",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!InitializeSettings())
    {
        return 1;
    }
    LogLine(L"Starting");

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, g_instanceIdentity.mutexName.c_str());
    if (!hMutex)
    {
        ShowStartupError(L"Failed to create the single-instance mutex.");
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        LogLine(L"Another instance is already running");
        CloseHandle(hMutex);
        return 0;
    }

    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult))
    {
        LogHResult(L"CoInitializeEx", comResult);
        ShowStartupError(L"Failed to initialize COM.");
        CloseHandle(hMutex);
        return 1;
    }

    g_taskbarCreated = aip::RegisterTaskbarCreatedMessage();
    if (g_taskbarCreated == 0)
    {
        LogLine(L"Could not register the TaskbarCreated message.");
    }

    WNDCLASSEX wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc))
    {
        ShowStartupError(L"Failed to register the message-window class.");
        CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClass,
        g_instanceIdentity.windowTitle.c_str(),
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        hInstance,
        nullptr);
    if (!hwnd)
    {
        ShowStartupError(L"Failed to create the message window.");
        UnregisterClassW(kWindowClass, hInstance);
        CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    if (!AddTrayIcon(hwnd))
    {
        ShowStartupError(L"Failed to add the tray icon.");
        DestroyWindow(hwnd);
        UnregisterClassW(kWindowClass, hInstance);
        CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    MSG msg = {};
    int exitCode = 0;
    for (;;)
    {
        BOOL messageResult = GetMessage(&msg, nullptr, 0, 0);
        if (messageResult == 0)
        {
            exitCode = static_cast<int>(msg.wParam);
            break;
        }
        if (messageResult == -1)
        {
            LogLine(L"Failed to read the window message queue");
            exitCode = 1;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (IsWindow(hwnd))
    {
        DestroyWindow(hwnd);
    }
    UnregisterClassW(kWindowClass, hInstance);
    CoUninitialize();
    CloseHandle(hMutex);
    LogLine(L"Stopped");
    return exitCode;
}
