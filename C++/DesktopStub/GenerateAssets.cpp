// compile command: cl /std:c++17 /EHsc /DUNICODE /D_UNICODE GenerateAssets.cpp /link gdiplus.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib comdlg32.lib advapi32.lib windowsapp.lib /SUBSYSTEM:WINDOWS
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>

enum class FitMode {
    Fill,
    Fit,
    Stretch,
    Center,
    Tile,
    Span
};

using namespace Gdiplus;
using namespace std::chrono;

struct IniDefault {
    const wchar_t* section;
    const wchar_t* key;
    const wchar_t* value;
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
static std::wstring g_iniPath, g_logPath, g_exePath;
static std::atomic<bool> g_running(true), g_console(false), g_logging(true), g_tray(true);
static std::atomic<bool> g_listenWallpaper(true);
static std::atomic<bool> g_listenFit(true);
static std::atomic<bool> g_generateOnStartup(true);
static const IniDefault g_defaults[] = {
    // Settings
    {L"Settings", L"PollIntervalMs", L"2000"},
    {L"Settings", L"ConfirmMs", L"800"},
    {L"Settings", L"DebounceMinMs", L"1200"},
    {L"Settings", L"Logging", L"1"},
    {L"Settings", L"LogPath", L"logs.txt"},
    {L"Settings", L"TrayIcon", L"1"},
    {L"Settings", L"ShowConsole", L"0"},
    {L"Settings", L"GenerateOnStartup", L"1"},
    {L"Settings", L"ListenWallpaper", L"1"},
    {L"Settings", L"ListenFit", L"1"},
    {L"Settings", L"UsePowerShell", L"1"},

    // Assets (IMPORTANT: correct defaults)
    {L"Assets", L"StoreLogo", L"0"},
    {L"Assets", L"MediumTile", L"1"},
    {L"Assets", L"Square44x44Logo", L"0"},
    {L"Assets", L"SmallTile", L"0"},
    {L"Assets", L"WideTile", L"1"},
    {L"Assets", L"LargeTile", L"1"},
};

// Logging buffer
static std::mutex g_logMutex;
static std::vector<std::wstring> g_logBuf;
static const size_t LOGMAX = 1024;

// PowerShell status
static std::wstring g_psOut, g_psMsg;
static DWORD g_psCode = 0;
static bool g_psErr = false;

// Tray
static NOTIFYICONDATAW g_nid{};
static HWND g_hwnd = nullptr;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
void Log(const wchar_t* fmt, ...)
{
    wchar_t msg[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf(msg, 2047, fmt, ap);
    va_end(ap);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t ts[64];
    swprintf(ts, 64, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::wstring line = ts + std::wstring(msg);

    {   std::lock_guard<std::mutex> lk(g_logMutex);
        if (g_logBuf.size() >= LOGMAX) g_logBuf.erase(g_logBuf.begin());
        g_logBuf.push_back(line);
    }

    if (g_console) { fwprintf(stdout, L"%ls\n", line.c_str()); fflush(stdout); }

    if (g_logging)
    {
        FILE* f = _wfopen(g_logPath.c_str(), L"a, ccs=UTF-8");
        if (f) { fwprintf(f, L"%ls\n", line.c_str()); fclose(f); }
    }
}

int IniReadI(const wchar_t* s, const wchar_t* k, int d)
{ return GetPrivateProfileIntW(s, k, d, g_iniPath.c_str()); }

std::wstring IniReadS(const wchar_t* s, const wchar_t* k, const wchar_t* d)
{
    wchar_t b[260]; GetPrivateProfileStringW(s, k, d, b, 260, g_iniPath.c_str());
    return b;
}

void IniWrite(const wchar_t* s, const wchar_t* k, const wchar_t* v)
{ WritePrivateProfileStringW(s, k, v, g_iniPath.c_str()); }

void EnsureIniDefaults()
{
    for (const auto& d : g_defaults)
    {
        wchar_t buf[260];

        GetPrivateProfileStringW(
            d.section,
            d.key,
            L"",
            buf,
            260,
            g_iniPath.c_str());

        // If missing → write default
        if (buf[0] == L'\0')
        {
            WritePrivateProfileStringW(
                d.section,
                d.key,
                d.value,
                g_iniPath.c_str());
        }
    }
}

bool UsePowerShell()
{
    return IniReadI(L"Settings", L"UsePowerShell", 1) != 0;
}

const wchar_t* FitModeToString(FitMode m)
{
    switch (m)
    {
    case FitMode::Fill:    return L"Fill";
    case FitMode::Fit:     return L"Fit";
    case FitMode::Stretch: return L"Stretch";
    case FitMode::Center:  return L"Center";
    case FitMode::Tile:    return L"Tile";
    case FitMode::Span:    return L"Span";
    default:               return L"?";
    }
}

// -----------------------------------------------------------------------------
// Wallpaper
// -----------------------------------------------------------------------------

std::wstring GetWallpaper()
{
    HKEY h; if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Control Panel\\Desktop", 0, KEY_READ, &h)) return L"";

    DWORD t=0, sz=0;
    if (RegQueryValueExW(h, L"TranscodedImageCache", nullptr, &t, nullptr, &sz) || t!=REG_BINARY)
    { RegCloseKey(h); return L""; }

    std::vector<BYTE> buf(sz);
    if (RegQueryValueExW(h, L"TranscodedImageCache", nullptr, nullptr, buf.data(), &sz))
    { RegCloseKey(h); return L""; }

    RegCloseKey(h);
    if (sz <= 24) return L"";
    const wchar_t* p = (wchar_t*)(buf.data()+24);
    size_t len = (sz-24)/sizeof(wchar_t);
    return std::wstring(p, wcsnlen(p, len));
}

FitMode GetWallpaperFit()
{
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Control Panel\\Desktop", 0, KEY_READ, &h))
        return FitMode::Fill;

    wchar_t style[16] = {};
    wchar_t tile[8] = {};

    DWORD sz = sizeof(style);
    RegQueryValueExW(h, L"WallpaperStyle", nullptr, nullptr, (LPBYTE)style, &sz);

    sz = sizeof(tile);
    RegQueryValueExW(h, L"TileWallpaper", nullptr, nullptr, (LPBYTE)tile, &sz);

    RegCloseKey(h);

    int s = _wtoi(style);
    int t = _wtoi(tile);

    if (t == 1) return FitMode::Tile;

    switch (s)
    {
    case 10: return FitMode::Fill;
    case 6:  return FitMode::Fit;
    case 2:  return FitMode::Stretch;
    case 22: return FitMode::Span;
    default: return FitMode::Center;
    }
}

// -----------------------------------------------------------------------------
// GDI+ PNG save helpers
// -----------------------------------------------------------------------------
static CLSID g_png{};
bool FindPngEncoder()
{
    if (g_png.Data1) return true;

    UINT n=0, sz=0;
    if (GetImageEncodersSize(&n, &sz) != Ok || !sz) return false;
    auto* p = (ImageCodecInfo*)malloc(sz);
    if (!p) return false;

    if (GetImageEncoders(n, sz, p) == Ok)
        for (UINT i=0;i<n;i++)
            if (wcscmp(p[i].MimeType, L"image/png")==0)
                g_png = p[i].Clsid;

    free(p);
    return g_png.Data1 != 0;
}

Bitmap* ResizeWithFit(Bitmap* src, int w, int h, FitMode mode)
{
    if (!src) return nullptr;

    int sw = src->GetWidth();
    int sh = src->GetHeight();

    auto* out = new Bitmap(w, h, PixelFormat32bppARGB);
    Graphics g(out);

    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.Clear(Color(255, 0, 0, 0)); // black background

    double scaleX = (double)w / sw;
    double scaleY = (double)h / sh;

    int dw = w, dh = h;
    int dx = 0, dy = 0;

    switch (mode)
    {
    case FitMode::Fill:
    {
        double s = std::max(scaleX, scaleY);
        dw = int(sw * s + 0.5);
        dh = int(sh * s + 0.5);
        dx = (w - dw) / 2;
        dy = (h - dh) / 2;
        break;
    }

    case FitMode::Fit:
    {
        double s = std::min(scaleX, scaleY);
        dw = int(sw * s + 0.5);
        dh = int(sh * s + 0.5);
        dx = (w - dw) / 2;
        dy = (h - dh) / 2;
        break;
    }

    case FitMode::Stretch:
    {
        dw = w;
        dh = h;
        dx = 0;
        dy = 0;
        break;
    }

    case FitMode::Center:
    {
        dw = sw;
        dh = sh;
        dx = (w - sw) / 2;
        dy = (h - sh) / 2;
        break;
    }

    case FitMode::Tile:
    {
        for (int y = 0; y < h; y += sh)
            for (int x = 0; x < w; x += sw)
                g.DrawImage(src, x, y, sw, sh);
        return out;
    }

    case FitMode::Span:
    {
        // fallback: treat as Fill
        double s = std::max(scaleX, scaleY);
        dw = int(sw * s + 0.5);
        dh = int(sh * s + 0.5);
        dx = (w - dw) / 2;
        dy = (h - dh) / 2;
        break;
    }
    }

    g.DrawImage(src, Rect(dx, dy, dw, dh), 0, 0, sw, sh, UnitPixel);
    return out;
}

bool SavePNG(Bitmap* b, const wchar_t* f)
{ return b && FindPngEncoder() && b->Save(f,&g_png,nullptr)==Ok; }

// -----------------------------------------------------------------------------
// PowerShell execution
// -----------------------------------------------------------------------------
void PS_Clear() { g_psErr=false; g_psOut.clear(); g_psMsg.clear(); g_psCode=0; }

bool PS_Run(const std::wstring& cmd)
{
    Log(L"[i] Run PS: %s", cmd.c_str());

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE r=0,w=0; if (!CreatePipe(&r,&w,&sa,0)) return false;
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{}; si.cb=sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE; si.hStdOutput=w; si.hStdError=w;

    PROCESS_INFORMATION pi{};
    std::wstring cmdline = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"" + cmd + L"\"";

    BOOL ok = CreateProcessW(nullptr, &cmdline[0], nullptr,nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr,nullptr, &si, &pi);

    CloseHandle(w);
    if (!ok) { CloseHandle(r); return false; }

    char buf[2048]; DWORD n;
    std::wstring out;

    while (ReadFile(r,buf,2047,&n,nullptr) && n>0)
    {
        buf[n]=0;
        int need = MultiByteToWideChar(CP_UTF8,0,buf,n,nullptr,0);
        if (need>0){
            std::wstring tmp; tmp.resize(need);
            MultiByteToWideChar(CP_UTF8,0,buf,n,&tmp[0],need);
            out += tmp;
        }
    }
    CloseHandle(r);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec=0; GetExitCodeProcess(pi.hProcess,&ec);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    g_psOut = out; g_psCode = ec;
    if (ec==0){ PS_Clear(); return true; }

    g_psErr = true;

    // Special case: Sideloading disabled (HRESULT 0x80073CFF)
    if (g_psOut.find(L"0x80073CFF") != std::wstring::npos) {
        g_psMsg = L"PowerShell error: Enable sideloading first!";
        Log(L"[!] PowerShell: sideloading disabled (HRESULT 0x80073CFF)");
    } else {
        wchar_t msg[64];
        swprintf(msg, 64, L"PowerShell error: 0x%08X", ec);
        g_psMsg = msg;
    }

    Log(L"[!] PS error: %lu", ec);
    Log(L"[!] Output:\n%ls", out.c_str());

    return false;
}

bool Appx_Register_COM(const std::wstring& manifestPath)
{
    try
    {
        Log(L"[i] Using COM Appx registration...");

        winrt::init_apartment(winrt::apartment_type::single_threaded);

        using namespace winrt::Windows::Management::Deployment;
        using namespace winrt::Windows::Foundation;
        using namespace winrt::Windows::Foundation::Collections;

        PackageManager pm;

        Uri uri(manifestPath);

        IVector<Uri> deps = winrt::single_threaded_vector<Uri>();

        auto op = pm.RegisterPackageAsync(
            uri,
            deps,
            DeploymentOptions::ForceUpdateFromAnyVersion
        );

        op.get(); // wait

        if (op.Status() == winrt::Windows::Foundation::AsyncStatus::Completed)
        {
            Log(L"[i] COM registration success.");
            return true;
        }
        else
        {
            Log(L"[!] COM registration failed (status=%d)", (int)op.Status());
            return false;
        }
    }
    catch (const winrt::hresult_error& e)
    {
        Log(L"[!] COM exception: 0x%08X", e.code().value);
        Log(L"[!] Message: %s", e.message().c_str());
        return false;
    }
}

// -----------------------------------------------------------------------------
// Tile generation
// -----------------------------------------------------------------------------
struct Tile { const wchar_t* name; const wchar_t* file; int w,h; };
static const Tile g_tiles[] = {
    {L"StoreLogo",       L"Assets\\StoreLogo.png", 50,50},
    {L"MediumTile",      L"Assets\\MediumTile.png",150,150},
    {L"Square44x44Logo", L"Assets\\Square44x44Logo.png",44,44},
    {L"SmallTile",       L"Assets\\SmallTile.png",71,71},
    {L"WideTile",        L"Assets\\WideTile.png",310,150},
    {L"LargeTile",       L"Assets\\LargeTile.png",310,310},
};

void Generate(const wchar_t* wp)
{
    if (!wp||!*wp){ Log(L"[!] Empty wallpaper."); return; }

    Log(L"[i] Generating from: %s", wp);
    Bitmap* src = new Bitmap(wp);
    if (src->GetLastStatus()!=Ok){ Log(L"[!] Load fail."); delete src; return; }
    FitMode mode = GetWallpaperFit();
    CreateDirectoryW(L"Assets", nullptr);

    for (auto& t: g_tiles)
    {
        if (!IniReadI(L"Assets", t.name, 0)){
            Log(L" [!] %s disabled", t.name);
            continue;
        }
        auto* o = ResizeWithFit(src, t.w, t.h, mode);
        if (o){
            if (SavePNG(o, t.file)) Log(L" [i] %s", t.file);
            else Log(L" [!] Fail %s", t.file);
            delete o;
        }
    }
    delete src;

    Log(L"[i] Re-register AppxManifest...");

    wchar_t fullPath[MAX_PATH];
    GetFullPathNameW(L".\\AppxManifest.xml", MAX_PATH, fullPath, nullptr);

    bool ok = false;

    if (!UsePowerShell())
    {
        ok = Appx_Register_COM(fullPath);

        if (!ok)
        {
            Log(L"[!] COM failed, falling back to PowerShell...");
            ok = PS_Run(L"Add-AppxPackage -Register .\\AppxManifest.xml -ForceUpdateFromAnyVersion");
        }
    }
    else
    {
        ok = PS_Run(L"Add-AppxPackage -Register .\\AppxManifest.xml -ForceUpdateFromAnyVersion");
    }

    if (ok) Log(L"[i] Done.");
    else    Log(L"[!] Registration failed.");
}

// -----------------------------------------------------------------------------
// Poll thread
// -----------------------------------------------------------------------------
void PollThread()
{
    std::wstring last;
    FitMode lastFit = GetWallpaperFit();
    auto lastGen = steady_clock::now() - seconds(5);

    while (g_running)
    {
        int poll = IniReadI(L"Settings",L"PollIntervalMs",2000);
        int confirm = IniReadI(L"Settings",L"ConfirmMs",800);
        int deb = IniReadI(L"Settings",L"DebounceMinMs",1200);

        std::wstring cur = GetWallpaper();
        FitMode curFit = GetWallpaperFit();

        bool wallpaperChanged = (cur != last);
        bool fitChanged = (curFit != lastFit);

        if ((g_listenWallpaper && wallpaperChanged) ||
            (g_listenFit && fitChanged))
        {
            std::this_thread::sleep_for(milliseconds(confirm));

            std::wstring cur2 = GetWallpaper();
            FitMode fit2 = GetWallpaperFit();

            bool wallpaperChanged2 = (cur2 != last);
            bool fitChanged2 = (fit2 != lastFit);

            if ((g_listenWallpaper && wallpaperChanged2) ||
                (g_listenFit && fitChanged2))
            {
                auto now = steady_clock::now();

                if (duration_cast<milliseconds>(now - lastGen).count() >= deb)
                {
                    last = cur2;
                    lastFit = fit2;

                    Generate(cur2.c_str());
                    lastGen = steady_clock::now();
                }
            }
        }
        std::this_thread::sleep_for(milliseconds(poll));
    }
}

// -----------------------------------------------------------------------------
// Tray
// -----------------------------------------------------------------------------
enum {
    ID_EXIT=1001,

    // General
    ID_USE_PS,
    ID_TRAYICON,
    ID_CONSOLE,
    ID_LOG,
    ID_LOGFILE,
    ID_GENERATE_NOW,
    ID_LISTEN_WP,
    ID_LISTEN_FIT,

    // Assets
    ID_A1=2001, ID_A2, ID_A3, ID_A4, ID_A5, ID_A6
};

void TrayRemove()
{
    if (g_nid.hWnd){
        Shell_NotifyIconW(NIM_DELETE,&g_nid);
        if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
        g_nid={};
    }
}

void ShowPSLog(HWND h)
{
    if (!g_psErr && g_psOut.empty()){
        MessageBoxW(h, L"No PowerShell output.", L"PS Log", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const size_t CH=3000;
    for (size_t p=0;p<g_psOut.size();p+=CH){
        std::wstring s = g_psOut.substr(p,CH);
        MessageBoxW(h, s.c_str(), L"PowerShell Output", MB_OK | MB_ICONERROR);
    }
}

void Menu(HWND h)
{
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();

    auto add = [&](UINT id, const std::wstring& t, bool chk=false, bool en=true){
        AppendMenuW(m,
            MF_STRING |
            (chk ? MF_CHECKED : 0) |
            (!en ? MF_DISABLED : 0),
            id, t.c_str());
    };

    // =====================
    // General Section
    // =====================
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"General:");
    add(ID_LISTEN_WP, L"Listen Wallpaper", g_listenWallpaper);
    add(ID_LISTEN_FIT, L"Listen Fit Mode", g_listenFit);
    FitMode mode = GetWallpaperFit();
    std::wstring fitLine = L"Fit Mode: ";
    fitLine += FitModeToString(mode);
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, fitLine.c_str());
    add(ID_USE_PS, L"Use PowerShell", UsePowerShell());
    add(ID_TRAYICON, L"Tray Icon", g_tray);
    add(ID_CONSOLE, L"Show Console", g_console);
    add(ID_GENERATE_NOW, L"Generate now");
    add(ID_LOG, L"Logging", g_logging);
    add(ID_LOGFILE, L"Change log path...");

    // Show current path (disabled)
    std::wstring pathLine = L"Path: " + g_logPath;
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, pathLine.c_str());

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    // =====================
    // Assets Section
    // =====================
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"Assets:");

    const wchar_t* keys[] = {
        L"StoreLogo",L"MediumTile",L"Square44x44Logo",
        L"SmallTile",L"WideTile",L"LargeTile"
    };

    for (int i=0;i<6;i++)
        add(ID_A1+i, keys[i], IniReadI(L"Assets",keys[i],0)!=0);

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    // Exit
    add(ID_EXIT, L"Exit");

    SetForegroundWindow(h);
    UINT cmd = TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,h,nullptr);
    DestroyMenu(m);

    switch(cmd)
    {
    case ID_USE_PS:
    {
        bool v = !UsePowerShell();
        IniWrite(L"Settings", L"UsePowerShell", v ? L"1" : L"0");
        Log(L"[i] UsePowerShell = %d", v);
    }
    break;

    case ID_TRAYICON:
    {
        g_tray = !g_tray;
        IniWrite(L"Settings", L"TrayIcon", g_tray ? L"1" : L"0");

        if (!g_tray)
        {
            TrayRemove();
        }
        else
        {
            // recreate tray icon
            g_nid.cbSize=sizeof(g_nid);
            g_nid.hWnd=g_hwnd; g_nid.uID=1;
            g_nid.uFlags= NIF_MESSAGE|NIF_ICON|NIF_TIP;
            g_nid.uCallbackMessage=WM_USER+1;
            g_nid.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
            wcscpy_s(g_nid.szTip,L"Desktop Tile Generator");
            Shell_NotifyIconW(NIM_ADD,&g_nid);
        }

        Log(L"[i] Tray icon %s.", g_tray?L"enabled":L"disabled");
    }
    break;

    case ID_CONSOLE:
    {
        g_console = !g_console;

        if (g_console)
        {
            AllocConsole();
            FILE* f;
            freopen_s(&f,"CONOUT$","w",stdout);
            freopen_s(&f,"CONOUT$","w",stderr);
            freopen_s(&f,"CONIN$","r",stdin);
            SetConsoleOutputCP(CP_UTF8);

            std::lock_guard<std::mutex>lk(g_logMutex);
            for (auto& l:g_logBuf)
                fwprintf(stdout,L"%ls\n",l.c_str());
        }
        else
        {
            FreeConsole();
        }
        IniWrite(L"Settings", L"ShowConsole", g_console ? L"1" : L"0");
        Log(L"[i] Console %s.", g_console?L"on":L"off");
    }
    break;

    case ID_LOG:
    {
        g_logging = !g_logging;
        IniWrite(L"Settings",L"Logging", g_logging?L"1":L"0");
        Log(L"[i] Logging %s.", g_logging?L"on":L"off");
    }
    break;

    case ID_GENERATE_NOW:
    {
        std::wstring wp = GetWallpaper();

        if (wp.empty())
        {
            MessageBoxW(h,
                L"Could not detect current wallpaper.",
                L"Generate Now",
                MB_OK | MB_ICONWARNING);
        }
        else
        {
            Log(L"[i] Manual generate triggered.");
            Generate(wp.c_str());
        }
    }
    break;

    case ID_EXIT:
        g_running = false;
        TrayRemove();
        PostQuitMessage(0);
        break;

    case ID_LISTEN_WP:
    {
        g_listenWallpaper = !g_listenWallpaper;
        IniWrite(L"Settings", L"ListenWallpaper", g_listenWallpaper ? L"1" : L"0");
        Log(L"[i] ListenWallpaper = %d", (int)g_listenWallpaper);
    }
    break;

    case ID_LISTEN_FIT:
    {
        g_listenFit = !g_listenFit;
        IniWrite(L"Settings", L"ListenFit", g_listenFit ? L"1" : L"0");
        Log(L"[i] ListenFit = %d", (int)g_listenFit);
    }
    break;

    default:
        if (cmd>=ID_A1 && cmd<=ID_A6){
            int i = cmd-ID_A1;
            const wchar_t* k = keys[i];
            int nv = !IniReadI(L"Assets",k,0);
            IniWrite(L"Assets",k,nv?L"1":L"0");
            Log(L"[i] Asset %s %s.", k, nv?L"on":L"off");
        }
        break;
    }
}

// -----------------------------------------------------------------------------
// Window proc
// -----------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if (m==WM_USER+1){
        if (l==WM_RBUTTONUP || l==WM_CONTEXTMENU) Menu(h);
    }
    else if (m==WM_DESTROY){
        TrayRemove(); PostQuitMessage(0);
    }
    return DefWindowProcW(h,m,w,l);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE, PWSTR, int)
{
    wchar_t buf[260];
    GetModuleFileNameW(nullptr,buf,260);
    g_exePath = buf;

    std::wstring dir = g_exePath.substr(0, g_exePath.find_last_of(L"\\/"));
    std::wstring base = g_exePath.substr(g_exePath.find_last_of(L"\\/")+1);
    base = base.substr(0, base.find_last_of(L'.'));
    g_iniPath = dir + L"\\" + base + L".ini";

    // defaults
    EnsureIniDefaults();
    g_logging = IniReadI(L"Settings",L"Logging",1)!=0;
    g_tray    = IniReadI(L"Settings",L"TrayIcon",1)!=0;
    g_logPath = IniReadS(L"Settings",L"LogPath",L"logs.txt");
    g_console = IniReadI(L"Settings", L"ShowConsole", 0) != 0;
    g_generateOnStartup = IniReadI(L"Settings", L"GenerateOnStartup", 1) != 0;
    g_listenWallpaper = IniReadI(L"Settings", L"ListenWallpaper", 1) != 0;
    g_listenFit       = IniReadI(L"Settings", L"ListenFit", 1) != 0;

    // GDI+
    GdiplusStartupInput in; ULONG_PTR tk;
    if (GdiplusStartup(&tk,&in,nullptr)!=Ok){ MessageBoxW(nullptr,L"GDI+",L"Error",0); return 1; }

    Log(L"[i] Program starting...");

    // Console setting
    if (g_console)
    {
        AllocConsole();

        FILE* f;
        freopen_s(&f,"CONOUT$","w",stdout);
        freopen_s(&f,"CONOUT$","w",stderr);
        freopen_s(&f,"CONIN$","r",stdin);

        SetConsoleOutputCP(CP_UTF8);

        // 👇 THIS is where it goes
        std::lock_guard<std::mutex> lk(g_logMutex);
        for (auto& l : g_logBuf)
            fwprintf(stdout, L"%ls\n", l.c_str());
    }

    // Window
    WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=hi; wc.lpszClassName=L"TrayWndRef";
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(0,L"TrayWndRef",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,hi,nullptr);

    // Tray
    if (g_tray){
        g_nid.cbSize=sizeof(g_nid);
        g_nid.hWnd=g_hwnd; g_nid.uID=1;
        g_nid.uFlags= NIF_MESSAGE|NIF_ICON|NIF_TIP;
        g_nid.uCallbackMessage=WM_USER+1;
        HICON ic = LoadIconW(nullptr,IDI_APPLICATION);
        g_nid.hIcon=ic;
        wcscpy_s(g_nid.szTip,L"Desktop Tile Generator");
        Shell_NotifyIconW(NIM_ADD,&g_nid);
    }

    std::thread th(PollThread);

    MSG msg;
    while (GetMessageW(&msg,nullptr,0,0))
    { TranslateMessage(&msg); DispatchMessageW(&msg); }

    g_running=false;
    if (th.joinable()) th.join();

    TrayRemove();
    GdiplusShutdown(tk);
    return 0;
}
