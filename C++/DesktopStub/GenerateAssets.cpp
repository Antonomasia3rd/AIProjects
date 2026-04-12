// GenerateAssets_Refactored (Option 3.5, C++17, MSYS2-UCRT64-safe)

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

using namespace Gdiplus;
using namespace std::chrono;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
static std::wstring g_iniPath, g_logPath, g_exePath;
static std::atomic<bool> g_running(true), g_console(false), g_logging(true), g_tray(true);

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

Bitmap* ResizeCrop(Bitmap* src, int w, int h)
{
    if (!src) return nullptr;
    double S = std::max(double(w)/src->GetWidth(), double(h)/src->GetHeight());
    int sw = int(src->GetWidth()*S + .5);
    int sh = int(src->GetHeight()*S + .5);

    auto* out = new Bitmap(w,h,PixelFormat32bppARGB);
    Graphics g(out); g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(src, Rect((w-sw)/2,(h-sh)/2,sw,sh), 0,0,src->GetWidth(),src->GetHeight(),UnitPixel);
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

    CreateDirectoryW(L"Assets", nullptr);

    for (auto& t: g_tiles)
    {
        if (!IniReadI(L"Assets", t.name, 1)){
            Log(L" ✖ %s disabled", t.name);
            continue;
        }
        auto* o = ResizeCrop(src, t.w,t.h);
        if (o){
            if (SavePNG(o, t.file)) Log(L" ✔ %s", t.file);
            else Log(L" [!] Fail %s", t.file);
            delete o;
        }
    }
    delete src;

    Log(L"[i] Re-register AppxManifest...");
    bool ok = PS_Run(L"Add-AppxPackage -Register .\\AppxManifest.xml -ForceUpdateFromAnyVersion");
    if (ok) Log(L"[✓] Done.");
    else    Log(L"[✖] Done with PS errors.");
}

// -----------------------------------------------------------------------------
// Poll thread
// -----------------------------------------------------------------------------
void PollThread()
{
    std::wstring last;
    auto lastGen = steady_clock::now() - seconds(5);

    while (g_running)
    {
        int poll = IniReadI(L"Settings",L"PollIntervalMs",2000);
        int confirm = IniReadI(L"Settings",L"ConfirmMs",800);
        int deb = IniReadI(L"Settings",L"DebounceMinMs",1200);

        std::wstring cur = GetWallpaper();

        if (cur != last)
        {
            std::this_thread::sleep_for(milliseconds(confirm));
            if (GetWallpaper() != last)
            {
                auto now = steady_clock::now();
                if (duration_cast<milliseconds>(now-lastGen).count() >= deb)
                {
                    last = cur;
                    Generate(cur.c_str());
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
    ID_EXIT=1001, ID_CONSOLE, ID_LOG, ID_LOGFILE, ID_TRAYDISABLE, ID_PSLOG,
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
        AppendMenuW(m, MF_STRING | (chk?MF_CHECKED:0) | (!en?MF_DISABLED:0), id, t.c_str());
    };

    add(ID_CONSOLE, GetConsoleWindow()?L"Hide Console":L"Show Console");
    add(ID_LOG, g_logging?L"Disable Logging":L"Enable Logging");
    add(ID_LOGFILE, L"Change log path...");
    add(ID_TRAYDISABLE, L"Disable tray icon...");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);

    if (g_psErr){
        add(0, g_psMsg, false, false);
        add(ID_PSLOG, L"View PowerShell output...");
        AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    }

    AppendMenuW(m, MF_STRING|MF_DISABLED,0,L"Assets:");
    const wchar_t* keys[] = {
        L"StoreLogo",L"MediumTile",L"Square44x44Logo",
        L"SmallTile",L"WideTile",L"LargeTile"
    };
    for (int i=0;i<6;i++)
        add(ID_A1+i, keys[i], IniReadI(L"Assets",keys[i],1)!=0);

    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    add(ID_EXIT,L"Exit");

    SetForegroundWindow(h);
    UINT cmd = TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,h,nullptr);
    DestroyMenu(m);

    switch(cmd)
    {
    case ID_EXIT:
        g_running=false; TrayRemove(); PostQuitMessage(0); break;

    case ID_CONSOLE:
        if (GetConsoleWindow()){ FreeConsole(); g_console=false; }
        else {
            AllocConsole();
            FILE* f;
            freopen_s(&f,"CONOUT$","w",stdout);
            freopen_s(&f,"CONOUT$","w",stderr);
            freopen_s(&f,"CONIN$","r",stdin);
            SetConsoleOutputCP(CP_UTF8);
            g_console=true;
            std::lock_guard<std::mutex>lk(g_logMutex);
            for (auto& l:g_logBuf) fwprintf(stdout,L"%ls\n",l.c_str());
        }
        Log(L"[i] Console toggled.");
        break;

    case ID_LOG:
        g_logging = !g_logging;
        IniWrite(L"Settings",L"Logging", g_logging?L"1":L"0");
        Log(L"[i] Logging %s.", g_logging?L"on":L"off");
        break;

    case ID_LOGFILE: {
        wchar_t fn[260]={0};
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.hwndOwner=h; ofn.lpstrFile=fn; ofn.nMaxFile=260;
        ofn.lpstrFilter=L"Log\0*.txt;*.log\0All\0*.*\0";
        ofn.Flags=OFN_OVERWRITEPROMPT;
        ofn.lpstrTitle=L"Select log file";
        if (GetSaveFileNameW(&ofn)){
            g_logPath=fn;
            IniWrite(L"Settings",L"LogPath",fn);
            Log(L"[i] Log path changed.");
        }
    } break;

    case ID_TRAYDISABLE:
        if (MessageBoxW(h,L"Disable tray icon?\nRe-enable via INI.",L"Confirm",
            MB_YESNO|MB_ICONWARNING)==IDYES){
            g_tray=false;
            IniWrite(L"Settings",L"TrayIcon",L"0");
            TrayRemove();
        }
        break;

    case ID_PSLOG:
        ShowPSLog(h);
        break;

    default:
        if (cmd>=ID_A1 && cmd<=ID_A6){
            int i = cmd-ID_A1;
            const wchar_t* k = keys[i];
            int nv = !IniReadI(L"Assets",k,1);
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
    g_logging = IniReadI(L"Settings",L"Logging",1)!=0;
    g_tray    = IniReadI(L"Settings",L"TrayIcon",1)!=0;
    g_logPath = IniReadS(L"Settings",L"LogPath",L"logs.txt");

    // GDI+
    GdiplusStartupInput in; ULONG_PTR tk;
    if (GdiplusStartup(&tk,&in,nullptr)!=Ok){ MessageBoxW(nullptr,L"GDI+",L"Error",0); return 1; }

    Log(L"[i] Starting GenerateAssets (refactored).");

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
