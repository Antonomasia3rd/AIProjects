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

static std::wstring ModulePath()
{
    wchar_t buffer[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"CharmTray.exe";
    return std::wstring(buffer, len);
}

static std::wstring DirectoryOf(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

static std::wstring BaseNameWithoutExtension(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t start = slash == std::wstring::npos ? 0 : slash + 1;
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || dot < start) dot = path.size();
    std::wstring name = path.substr(start, dot - start);
    return name.empty() ? L"CharmTray" : name;
}

static std::wstring ModuleLocalPath(const wchar_t* extension)
{
    std::wstring module = ModulePath();
    return DirectoryOf(module) + L"\\" + BaseNameWithoutExtension(module) + extension;
}

static void EnsureIniFile()
{
    std::wstring ini = ModuleLocalPath(L".ini");
    DWORD attrs = GetFileAttributesW(ini.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return;
    WritePrivateProfileStringW(L"Settings", L"LoggingEnabled", L"1", ini.c_str());
}

static bool IsLoggingEnabled()
{
    EnsureIniFile();
    return GetPrivateProfileIntW(L"Settings", L"LoggingEnabled", 1, ModuleLocalPath(L".ini").c_str()) != 0;
}

static void LogLine(const wchar_t* message)
{
    if (!IsLoggingEnabled()) return;
    std::wstring path = ModuleLocalPath(L".log");
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[512];
    int len = wsprintfW(line, L"%04u-%02u-%02u %02u:%02u:%02u  %s\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, message);
    DWORD bytes = 0;
    WriteFile(file, line, static_cast<DWORD>(len * sizeof(wchar_t)), &bytes, nullptr);
    CloseHandle(file);
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

static IServiceProvider_* GetShellSP()
{
    IServiceProvider_* pSP = nullptr;
    CoCreateInstance(CLSID_ImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER,
                     IID_IServiceProvider_,
                     reinterpret_cast<void**>(&pSP));
    return pSP;
}

// func_0x1740 — get edge placement for the current cursor monitor
static IUnknown* GetEdgePlacement(IServiceProvider_* pSP)
{
    if (!pSP) return nullptr;
    IEdgeUiTracker* pTracker = nullptr;
    // rdx=SID_IEdgeUiTracker, r8=IID_IEdgeUiTracker  (verified from disasm)
    pSP->QueryService(SID_IEdgeUiTracker, IID_IEdgeUiTracker,
                      reinterpret_cast<void**>(&pTracker));
    if (!pTracker) return nullptr;

    POINT pt = {};
    GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

    IUnknown* pPlacement = nullptr;
    pTracker->SetMonitorEdge(hMon, &pPlacement);
    pTracker->Release();
    return pPlacement;
}

// func_0x1670 — get immersive shell HWND (desktop window as fallback)
static HWND GetImmersiveHWND(IServiceProvider_* pSP)
{
    HWND hwnd = GetDesktopWindow();
    if (!pSP) return hwnd;

    // Step 1: QueryService(SID=IID=GUID_IShellHostObject) → IShellHostObject
    // NOTE: SID == IID here (bf63999f) — verified from disasm
    IShellHostObject* pShellHost = nullptr;
    pSP->QueryService(GUID_IShellHostObject, GUID_IShellHostObject,
                      reinterpret_cast<void**>(&pShellHost));
    if (!pShellHost) return hwnd;

    // Step 2: vtable[4] = GetHostContainer
    IUnknown* pContainer = nullptr;
    pShellHost->GetHostContainer(&pContainer);
    pShellHost->Release();
    if (!pContainer) return hwnd;

    // Step 3: QueryService on container: SID=NULL, IID=IHwndProvider
    IServiceProvider_* pContainerSP = nullptr;
    pContainer->QueryInterface(IID_IServiceProvider_,
                               reinterpret_cast<void**>(&pContainerSP));
    if (pContainerSP)
    {
        IHwndProvider* pHwndProv = nullptr;
        // rdx=NULL (xor edx,edx), r8=IID_IHwndProvider — verified from disasm
        pContainerSP->QueryService(GUID_NULL, IID_IHwndProvider,
                                   reinterpret_cast<void**>(&pHwndProv));
        if (pHwndProv)
        {
            pHwndProv->GetHwnd(&hwnd);
            pHwndProv->Release();
        }
        pContainerSP->Release();
    }
    pContainer->Release();
    return hwnd;
}

// ---------------------------------------------------------------------------
// ShowCharm — the core function
// ---------------------------------------------------------------------------
static void ShowCharm(int charmId)
{
    IServiceProvider_* pSP = GetShellSP();
    if (!pSP) return;

    if (charmId == CHARM_SEARCH)
    {
        // QueryService: rdx(SID)=SID_SearchPane, r8(IID)=IID_ISearchPane
        ISearchPane* pSearch = nullptr;
        pSP->QueryService(SID_SearchPane, IID_ISearchPane,
                          reinterpret_cast<void**>(&pSearch));
        if (pSearch)
        {
            IUnknown* pPlacement = GetEdgePlacement(pSP);
            pSearch->Show(pPlacement, nullptr, nullptr);
            if (pPlacement) pPlacement->Release();
            pSearch->Release();
        }
    }
    else if (charmId == CHARM_SHARE || charmId == CHARM_START)
    {
        // Share: rdx(SID)=SID_IShareFlyout, r8(IID)=IID_ICharmFlyout
        // Start: rdx(SID)=SID_IStartFlyout, r8(IID)=IID_ICharmFlyout
        const GUID& sid =
            (charmId == CHARM_SHARE) ? SID_IShareFlyout : SID_IStartFlyout;
        ICharmFlyout* pFlyout = nullptr;
        pSP->QueryService(sid, IID_ICharmFlyout,
                          reinterpret_cast<void**>(&pFlyout));
        if (pFlyout)
        {
            IUnknown* pPlacement = GetEdgePlacement(pSP);
            HWND      hwnd       = GetImmersiveHWND(pSP);
            pFlyout->Show(pPlacement, hwnd);
            if (pPlacement) pPlacement->Release();
            pFlyout->Release();
        }
    }
    else // CHARM_DEVICES or CHARM_SETTINGS
    {
        // Level-1: rdx(SID)=SID_DevSettIntermediate, r8(IID)=IID_IServiceProvider_
        IServiceProvider_* pInterm = nullptr;
        pSP->QueryService(SID_DevSettIntermediate, IID_IServiceProvider_,
                          reinterpret_cast<void**>(&pInterm));
        if (pInterm)
        {
            // Level-2: rdx(SID)=SID_IDevSettFlyout, r8(IID)=IID_ICharmFlyout
            ICharmFlyout* pFlyout = nullptr;
            pInterm->QueryService(SID_IDevSettFlyout, IID_ICharmFlyout,
                                  reinterpret_cast<void**>(&pFlyout));
            if (pFlyout)
            {
                IUnknown* pPlacement = GetEdgePlacement(pSP);
                HWND      hwnd       = GetImmersiveHWND(pSP);
                pFlyout->Show(pPlacement, hwnd);
                if (pPlacement) pPlacement->Release();
                pFlyout->Release();
            }
            pInterm->Release();
        }
    }

    pSP->Release();
}

// ---------------------------------------------------------------------------
// Tray icon
// ---------------------------------------------------------------------------
static NOTIFYICONDATA g_nid = {};

static bool AddTrayIcon(HWND hwnd)
{
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = IDI_TRAY;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    g_nid.hIcon            = LoadIcon(nullptr, IDI_APPLICATION);
    lstrcpy(g_nid.szTip, L"Charm Launcher");
    return Shell_NotifyIcon(NIM_ADD, &g_nid) != FALSE;
}

static void RemoveTrayIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

static void ShowContextMenu(HWND hwnd)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;
    AppendMenu(hMenu, MF_STRING, ID_SEARCH,   L"Search");
    AppendMenu(hMenu, MF_STRING, ID_SHARE,    L"Share");
    AppendMenu(hMenu, MF_STRING, ID_START,    L"Start");
    AppendMenu(hMenu, MF_STRING, ID_DEVICES,  L"Devices");
    AppendMenu(hMenu, MF_STRING, ID_SETTINGS, L"Settings");
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hMenu, MF_STRING, ID_EXIT,     L"Exit");
    SetForegroundWindow(hwnd);
    POINT pt; GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(hMenu);
}

// ---------------------------------------------------------------------------
// Window procedure + entry point
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_TRAY_ICON:
        if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP)
            ShowContextMenu(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case ID_SEARCH:   ShowCharm(CHARM_SEARCH);   break;
        case ID_SHARE:    ShowCharm(CHARM_SHARE);    break;
        case ID_START:    ShowCharm(CHARM_START);    break;
        case ID_DEVICES:  ShowCharm(CHARM_DEVICES);  break;
        case ID_SETTINGS: ShowCharm(CHARM_SETTINGS); break;
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
    EnsureIniFile();
    LogLine(L"Starting");

    HANDLE hMutex = CreateMutex(nullptr, TRUE, L"CharmTray_SingleInstance");
    if (!hMutex)
    {
        LogLine(L"Failed to create the single-instance mutex");
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
        LogLine(L"Failed to initialize COM");
        CloseHandle(hMutex);
        return 1;
    }

    WNDCLASSEX wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"CharmTrayWnd";
    if (!RegisterClassEx(&wc))
    {
        LogLine(L"Failed to register the message-window class");
        CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    HWND hwnd = CreateWindowEx(0, L"CharmTrayWnd", L"CharmTray",
                                0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!hwnd)
    {
        LogLine(L"Failed to create the message window");
        CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    if (!AddTrayIcon(hwnd))
    {
        LogLine(L"Failed to add the tray icon");
        DestroyWindow(hwnd);
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
    CoUninitialize();
    CloseHandle(hMutex);
    LogLine(L"Stopped");
    return exitCode;
}
