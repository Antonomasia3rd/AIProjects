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
#include <commdlg.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>

INT_PTR CALLBACK RenameDlgProc(HWND, UINT, WPARAM, LPARAM);

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
static std::atomic<bool> g_disableFitting(false);
static std::atomic<bool> g_generateOnStartup(true);
static std::atomic<bool> g_hideDisabled(false);
static const IniDefault g_defaults[] = {
    // Settings
    {L"Settings", L"PollIntervalMs", L"2000"},
    {L"Settings", L"ConfirmMs", L"800"},
    {L"Settings", L"DebounceMinMs", L"1200"},
    {L"Settings", L"Logging", L"1"},
    {L"Settings", L"LogPath", L""},
    {L"Settings", L"TrayIcon", L"1"},
    {L"Settings", L"ShowConsole", L"0"},
    {L"Settings", L"GenerateOnStartup", L"1"},
    {L"Settings", L"ListenWallpaper", L"1"},
    {L"Settings", L"ListenFit", L"1"},
    {L"Settings", L"DisableFitting", L"0"},
    {L"Settings", L"UsePowerShell", L"1"},
    {L"Settings", L"HideDisabledEntries", L"0"},

    // Assets (IMPORTANT: correct defaults)
    {L"Assets", L"StoreLogo", L"0"},
    {L"Assets", L"MediumTile", L"1"},
    {L"Assets", L"Square44x44Logo", L"0"},
    {L"Assets", L"SmallTile", L"0"},
    {L"Assets", L"WideTile", L"1"},
    {L"Assets", L"LargeTile", L"1"},

};

struct StringDefault {
    const wchar_t* key;
    const wchar_t* value;
};

static const StringDefault g_stringDefaults[] = {
    {L"GeneralTitle", L"General:"},
    {L"LoggingTitle", L"Logging:"},
    {L"WallpaperFittingTitle", L"Wallpaper Fitting:"},
    {L"AssetsTitle", L"Assets:"},
    {L"ListenWallpaper", L"Listen Wallpaper"},
    {L"TrayIcon", L"Tray Icon"},
    {L"UsePowerShell", L"Use PowerShell"},
    {L"GenerateOnStartup", L"Generate on Startup"},
    {L"HideDisabledEntries", L"Hide disabled entries"},
    {L"ShowConsole", L"Show Console"},
    {L"GenerateNow", L"Generate now"},
    {L"LoggingEnable", L"Enable"},
    {L"ChangePath", L"Change path..."},
    {L"RenamePath", L"Rename path..."},
    {L"OpenInExplorer", L"Open in Explorer"},
    {L"ResetDefault", L"Reset to default"},
    {L"ShowPowerShellLog", L"Show PowerShell Log"},
    {L"LogPathPrefix", L"Path: "},
    {L"PSStatusPrefix", L"PS Status: "},
    {L"DisableFitting", L"Disable Fitting"},
    {L"ListenFit", L"Listen Fit Mode"},
    {L"FitModePrefix", L"Fit Mode: "},
    {L"FitModeForced", L"Fit Mode: Fill (forced)"},
    {L"Exit", L"Exit"},
    {L"RenameDialogTitle", L"Rename Log Path"},
    {L"OKText", L"OK"},
    {L"CancelText", L"Cancel"},
    {L"ErrorTitle", L"Error"},
    {L"PathCannotBeEmpty", L"Path cannot be empty."},
    {L"NoPowerShellOutputTitle", L"PowerShell Output"},
    {L"NoPowerShellOutput", L"No PowerShell output."},
    {L"GenerateNowTitle", L"Generate Now"},
    {L"WallpaperNotFound", L"Could not detect current wallpaper."},
    {L"CurrentWallpaperNotFound", L"Could not detect current wallpaper."},
    {L"ProgramStarting", L"Program starting..."},
    {L"ManualGenerateTriggered", L"Manual generate triggered."},
    {L"StartingWallpaperGeneration", L"Starting wallpaper generation due to %s."},
    {L"WallpaperSource", L"Wallpaper source: %s"},
    {L"FailedLoadWallpaper", L"Failed to load wallpaper image."},
    {L"SkippingAssetDisabled", L"Skipping %s (disabled in settings)."},
    {L"SavedAsset", L"Saved %s"},
    {L"FailedSaveAsset", L"Failed to save %s"},
    {L"ReRegisteringManifest", L"Re-registering AppxManifest due to regenerated assets."},
    {L"ManifestPath", L"Manifest path: %s"},
    {L"UsingComRegistration", L"Using COM Appx registration..."},
    {L"InvalidManifestPath", L"Invalid manifest path."},
    {L"ComRegistrationSuccess", L"COM registration success."},
    {L"ComRegistrationFailed", L"COM registration failed; falling back to PowerShell registration."},
    {L"ComRegistrationException", L"COM registration threw exception: 0x%08X"},
    {L"ComExceptionMessage", L"COM message: %s"},
    {L"ComFallbackToPowerShell", L"COM failed, falling back to PowerShell..."},
    {L"LaunchingPowerShellRegistration", L"Launching PowerShell registration..."},
    {L"PowerShellCommand", L"Command: %s"},
    {L"PowerShellCompleted", L"PowerShell registration completed successfully."},
    {L"PowerShellErrorSideloadDisabled", L"PowerShell error: Enable sideloading first!"},
    {L"PowerShellErrorCode", L"PowerShell registration failed with exit code 0x%08X."},
    {L"PowerShellOutputFollows", L"PowerShell output follows:"},
    {L"PowerShellRegistrationFailed", L"PowerShell registration did not complete successfully."},
    {L"AssetGenerationFinished", L"Asset generation and registration finished successfully."},
    {L"AppRegistrationFailed", L"App registration did not complete successfully."},
    {L"ChangeConfirmed", L"Change confirmed; regeneration allowed after debounce."},
    {L"WallpaperAndFitChangeDetected", L"Wallpaper and fit mode change detected."},
    {L"WallpaperChangeDetected", L"Wallpaper change detected."},
    {L"FitModeChangeDetected", L"Fit mode change detected."},
    {L"RegenerationAllowedAfterDebounce", L"Regeneration allowed after debounce."},
};

static void EnsureIniStringDefaults()
{
    for (const auto& d : g_stringDefaults)
    {
        wchar_t buf[260];
        GetPrivateProfileStringW(L"Strings", d.key, L"", buf, 260, g_iniPath.c_str());
        if (buf[0] == L'\0')
        {
            WritePrivateProfileStringW(L"Strings", d.key, d.value, g_iniPath.c_str());
        }
    }
}

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


struct UiStrings
{
    std::wstring generalTitle;
    std::wstring loggingTitle;
    std::wstring wallpaperFittingTitle;
    std::wstring assetsTitle;

    std::wstring listenWallpaper;
    std::wstring trayIcon;
    std::wstring usePowerShell;
    std::wstring generateOnStartup;
    std::wstring hideDisabledEntries;
    std::wstring showConsole;
    std::wstring generateNow;

    std::wstring loggingEnable;
    std::wstring changePath;
    std::wstring renamePath;
    std::wstring openInExplorer;
    std::wstring resetDefault;
    std::wstring showPowerShellLog;

    std::wstring logPathPrefix;
    std::wstring psStatusPrefix;

    std::wstring disableFitting;
    std::wstring listenFit;
    std::wstring fitModePrefix;
    std::wstring fitModeForced;

    std::wstring exitText;

    std::wstring renameDialogTitle;
    std::wstring okText;
    std::wstring cancelText;
    std::wstring errorTitle;
    std::wstring pathCannotBeEmpty;

    std::wstring noPowerShellOutputTitle;
    std::wstring noPowerShellOutput;

    std::wstring generateNowTitle;
    std::wstring wallpaperNotFound;
    std::wstring currentWallpaperNotFound;

    std::wstring programStarting;
    std::wstring manualGenerateTriggered;
    std::wstring startingWallpaperGeneration;
    std::wstring wallpaperSource;
    std::wstring failedLoadWallpaper;
    std::wstring skippingAssetDisabled;
    std::wstring savedAsset;
    std::wstring failedSaveAsset;
    std::wstring reRegisteringManifest;
    std::wstring manifestPath;
    std::wstring usingComRegistration;
    std::wstring invalidManifestPath;
    std::wstring comRegistrationSuccess;
    std::wstring comRegistrationFailed;
    std::wstring comRegistrationException;
    std::wstring comExceptionMessage;
    std::wstring comFallbackToPowerShell;
    std::wstring launchingPowerShellRegistration;
    std::wstring powerShellCommand;
    std::wstring powerShellCompleted;
    std::wstring powerShellErrorSideloadDisabled;
    std::wstring powerShellErrorCode;
    std::wstring powerShellOutputFollows;
    std::wstring powerShellRegistrationFailed;
    std::wstring assetGenerationFinished;
    std::wstring appRegistrationFailed;

    std::wstring changeConfirmed;
    std::wstring wallpaperAndFitChangeDetected;
    std::wstring wallpaperChangeDetected;
    std::wstring fitModeChangeDetected;
    std::wstring regenerationAllowedAfterDebounce;
};

static UiStrings g_ui;
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

std::wstring GetExeDir()
{
    size_t pos = g_exePath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return g_exePath.substr(0, pos);
}

std::wstring MakeFileUri(const std::wstring& path)
{
    wchar_t full[MAX_PATH];
    DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
    if (!n || n >= MAX_PATH) return L"";

    std::wstring p = full;
    std::replace(p.begin(), p.end(), L'\\', L'/');

    std::wstring esc;
    esc.reserve(p.size() * 3);
    for (wchar_t ch : p)
    {
        switch (ch)
        {
        case L' ':  esc += L"%20"; break;
        case L'#':  esc += L"%23"; break;
        case L'%':  esc += L"%25"; break;
        case L'?':  esc += L"%3F"; break;
        case L'"':  esc += L"%22"; break;
        case L'<':  esc += L"%3C"; break;
        case L'>':  esc += L"%3E"; break;
        case L'|':  esc += L"%7C"; break;
        default:    esc.push_back(ch); break;
        }
    }

    if (esc.size() >= 2 && esc[1] == L':')
        esc.insert(esc.begin(), L'/');

    return L"file://" + esc;
}

std::wstring Win32ErrorString(DWORD err)
{
    wchar_t* buf = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);

    std::wstring msg;
    if (len && buf)
    {
        msg.assign(buf, buf + len);
        while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L'.' || msg.back() == L' '))
            msg.pop_back();
    }
    else
    {
        msg = L"Unknown error";
    }

    if (buf) LocalFree(buf);
    return msg;
}

void LogWin32Failure(const wchar_t* what, DWORD err = GetLastError())
{
    Log(L"[!] %s failed (error %lu: %s)", what, err, Win32ErrorString(err).c_str());
}

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


void LoadUiStrings()
{
    g_ui.generalTitle = IniReadS(L"Strings", L"GeneralTitle", L"General:");
    g_ui.loggingTitle = IniReadS(L"Strings", L"LoggingTitle", L"Logging:");
    g_ui.wallpaperFittingTitle = IniReadS(L"Strings", L"WallpaperFittingTitle", L"Wallpaper Fitting:");
    g_ui.assetsTitle = IniReadS(L"Strings", L"AssetsTitle", L"Assets:");

    g_ui.listenWallpaper = IniReadS(L"Strings", L"ListenWallpaper", L"Listen Wallpaper");
    g_ui.trayIcon = IniReadS(L"Strings", L"TrayIcon", L"Tray Icon");
    g_ui.usePowerShell = IniReadS(L"Strings", L"UsePowerShell", L"Use PowerShell");
    g_ui.generateOnStartup = IniReadS(L"Strings", L"GenerateOnStartup", L"Generate on Startup");
    g_ui.hideDisabledEntries = IniReadS(L"Strings", L"HideDisabledEntries", L"Hide disabled entries");
    g_ui.showConsole = IniReadS(L"Strings", L"ShowConsole", L"Show Console");
    g_ui.generateNow = IniReadS(L"Strings", L"GenerateNow", L"Generate now");

    g_ui.loggingEnable = IniReadS(L"Strings", L"LoggingEnable", L"Enable");
    g_ui.changePath = IniReadS(L"Strings", L"ChangePath", L"Change path...");
    g_ui.renamePath = IniReadS(L"Strings", L"RenamePath", L"Rename path...");
    g_ui.openInExplorer = IniReadS(L"Strings", L"OpenInExplorer", L"Open in Explorer");
    g_ui.resetDefault = IniReadS(L"Strings", L"ResetDefault", L"Reset to default");
    g_ui.showPowerShellLog = IniReadS(L"Strings", L"ShowPowerShellLog", L"Show PowerShell Log");

    g_ui.logPathPrefix = IniReadS(L"Strings", L"LogPathPrefix", L"Path: ");
    g_ui.psStatusPrefix = IniReadS(L"Strings", L"PSStatusPrefix", L"PS Status: ");

    g_ui.disableFitting = IniReadS(L"Strings", L"DisableFitting", L"Disable Fitting");
    g_ui.listenFit = IniReadS(L"Strings", L"ListenFit", L"Listen Fit Mode");
    g_ui.fitModePrefix = IniReadS(L"Strings", L"FitModePrefix", L"Fit Mode: ");
    g_ui.fitModeForced = IniReadS(L"Strings", L"FitModeForced", L"Fit Mode: Fill (forced)");

    g_ui.exitText = IniReadS(L"Strings", L"Exit", L"Exit");

    g_ui.renameDialogTitle = IniReadS(L"Strings", L"RenameDialogTitle", L"Rename Log Path");
    g_ui.okText = IniReadS(L"Strings", L"OKText", L"OK");
    g_ui.cancelText = IniReadS(L"Strings", L"CancelText", L"Cancel");
    g_ui.errorTitle = IniReadS(L"Strings", L"ErrorTitle", L"Error");
    g_ui.pathCannotBeEmpty = IniReadS(L"Strings", L"PathCannotBeEmpty", L"Path cannot be empty.");

    g_ui.noPowerShellOutputTitle = IniReadS(L"Strings", L"NoPowerShellOutputTitle", L"PowerShell Output");
    g_ui.noPowerShellOutput = IniReadS(L"Strings", L"NoPowerShellOutput", L"No PowerShell output.");

    g_ui.generateNowTitle = IniReadS(L"Strings", L"GenerateNowTitle", L"Generate Now");
    g_ui.wallpaperNotFound = IniReadS(L"Strings", L"WallpaperNotFound", L"Could not detect current wallpaper.");
    g_ui.currentWallpaperNotFound = IniReadS(L"Strings", L"CurrentWallpaperNotFound", L"Could not detect current wallpaper.");

    g_ui.programStarting = IniReadS(L"Strings", L"ProgramStarting", L"Program starting...");
    g_ui.manualGenerateTriggered = IniReadS(L"Strings", L"ManualGenerateTriggered", L"Manual generate triggered.");
    g_ui.startingWallpaperGeneration = IniReadS(L"Strings", L"StartingWallpaperGeneration", L"Starting wallpaper generation due to %s.");
    g_ui.wallpaperSource = IniReadS(L"Strings", L"WallpaperSource", L"Wallpaper source: %s");
    g_ui.failedLoadWallpaper = IniReadS(L"Strings", L"FailedLoadWallpaper", L"Failed to load wallpaper image.");
    g_ui.skippingAssetDisabled = IniReadS(L"Strings", L"SkippingAssetDisabled", L"Skipping %s (disabled in settings).");
    g_ui.savedAsset = IniReadS(L"Strings", L"SavedAsset", L"Saved %s");
    g_ui.failedSaveAsset = IniReadS(L"Strings", L"FailedSaveAsset", L"Failed to save %s");
    g_ui.reRegisteringManifest = IniReadS(L"Strings", L"ReRegisteringManifest", L"Re-registering AppxManifest due to regenerated assets.");
    g_ui.manifestPath = IniReadS(L"Strings", L"ManifestPath", L"Manifest path: %s");
    g_ui.usingComRegistration = IniReadS(L"Strings", L"UsingComRegistration", L"Using COM Appx registration...");
    g_ui.invalidManifestPath = IniReadS(L"Strings", L"InvalidManifestPath", L"Invalid manifest path.");
    g_ui.comRegistrationSuccess = IniReadS(L"Strings", L"ComRegistrationSuccess", L"COM registration success.");
    g_ui.comRegistrationFailed = IniReadS(L"Strings", L"ComRegistrationFailed", L"COM registration failed; falling back to PowerShell registration.");
    g_ui.comRegistrationException = IniReadS(L"Strings", L"ComRegistrationException", L"COM registration threw exception: 0x%08X");
    g_ui.comExceptionMessage = IniReadS(L"Strings", L"ComExceptionMessage", L"COM message: %s");
    g_ui.comFallbackToPowerShell = IniReadS(L"Strings", L"ComFallbackToPowerShell", L"COM failed, falling back to PowerShell...");
    g_ui.launchingPowerShellRegistration = IniReadS(L"Strings", L"LaunchingPowerShellRegistration", L"Launching PowerShell registration...");
    g_ui.powerShellCommand = IniReadS(L"Strings", L"PowerShellCommand", L"Command: %s");
    g_ui.powerShellCompleted = IniReadS(L"Strings", L"PowerShellCompleted", L"PowerShell registration completed successfully.");
    g_ui.powerShellErrorSideloadDisabled = IniReadS(L"Strings", L"PowerShellErrorSideloadDisabled", L"PowerShell error: Enable sideloading first!");
    g_ui.powerShellErrorCode = IniReadS(L"Strings", L"PowerShellErrorCode", L"PowerShell registration failed with exit code 0x%08X.");
    g_ui.powerShellOutputFollows = IniReadS(L"Strings", L"PowerShellOutputFollows", L"PowerShell output follows:");
    g_ui.powerShellRegistrationFailed = IniReadS(L"Strings", L"PowerShellRegistrationFailed", L"PowerShell registration did not complete successfully.");
    g_ui.assetGenerationFinished = IniReadS(L"Strings", L"AssetGenerationFinished", L"Asset generation and registration finished successfully.");
    g_ui.appRegistrationFailed = IniReadS(L"Strings", L"AppRegistrationFailed", L"App registration did not complete successfully.");

    g_ui.changeConfirmed = IniReadS(L"Strings", L"ChangeConfirmed", L"Change confirmed; regeneration allowed after debounce.");
    g_ui.wallpaperAndFitChangeDetected = IniReadS(L"Strings", L"WallpaperAndFitChangeDetected", L"Wallpaper and fit mode change detected.");
    g_ui.wallpaperChangeDetected = IniReadS(L"Strings", L"WallpaperChangeDetected", L"Wallpaper change detected.");
    g_ui.fitModeChangeDetected = IniReadS(L"Strings", L"FitModeChangeDetected", L"Fit mode change detected.");
    g_ui.regenerationAllowedAfterDebounce = IniReadS(L"Strings", L"RegenerationAllowedAfterDebounce", L"Regeneration allowed after debounce.");
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

INT_PTR ShowRenameDialog(HWND parent, std::wstring& path)
{
    // Very small dialog template
    BYTE tmpl[1024] = {};
    DLGTEMPLATE* dlg = (DLGTEMPLATE*)tmpl;

    dlg->style = WS_POPUP | WS_BORDER | WS_SYSMENU | DS_MODALFRAME | WS_CAPTION | DS_SETFONT;
    dlg->cdit = 3;
    dlg->x = 10; dlg->y = 10; dlg->cx = 200; dlg->cy = 60;

    WORD* p = (WORD*)(dlg + 1);

    *p++ = 0; // no menu
    *p++ = 0; // default class

    const wchar_t* title = g_ui.renameDialogTitle.c_str();
    wcscpy((wchar_t*)p, title);
    p += wcslen(title) + 1;
    *p++ = 9; // font size
    const wchar_t fontName[] = L"Segoe UI";
    wcscpy((wchar_t*)p, fontName);
    p += wcslen(fontName) + 1;
    // ---- Edit box ----
    DLGITEMTEMPLATE* item = (DLGITEMTEMPLATE*)(((DWORD_PTR)p + 3) & ~3);
    item->x = 5; item->y = 5; item->cx = 190; item->cy = 12;
    item->id = 1001;
    item->style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;

    p = (WORD*)(item + 1);
    *p++ = 0xFFFF; *p++ = 0x0081; // EDIT class
    *p++ = 0; // no text
    *p++ = 0; // no extra data

    // ---- OK button ----
    item = (DLGITEMTEMPLATE*)(((DWORD_PTR)p + 3) & ~3);
    item->x = 40; item->y = 25; item->cx = 50; item->cy = 14;
    item->id = IDOK;
    item->style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON;

    p = (WORD*)(item + 1);
    *p++ = 0xFFFF; *p++ = 0x0080; // BUTTON
    wcscpy((wchar_t*)p, g_ui.okText.c_str());
    p += 3;
    *p++ = 0;

    // ---- Cancel button ----
    item = (DLGITEMTEMPLATE*)(((DWORD_PTR)p + 3) & ~3);
    item->x = 110; item->y = 25; item->cx = 50; item->cy = 14;
    item->id = IDCANCEL;
    item->style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;

    p = (WORD*)(item + 1);
    *p++ = 0xFFFF; *p++ = 0x0080;
    wcscpy((wchar_t*)p, g_ui.cancelText.c_str());
    p += 7;
    *p++ = 0;

    return DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr),
        dlg,
        parent,
        RenameDlgProc,
        (LPARAM)&path
    );
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
    Log(g_ui.launchingPowerShellRegistration.c_str());
    Log(g_ui.powerShellCommand.c_str(), cmd.c_str());
    PS_Clear();

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE r=0,w=0;
    if (!CreatePipe(&r,&w,&sa,0))
    {
        LogWin32Failure(L"CreatePipe");
        return false;
    }
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{}; si.cb=sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE; si.hStdOutput=w; si.hStdError=w;

    PROCESS_INFORMATION pi{};
    std::wstring cmdline = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"" + cmd + L"\"";

    BOOL ok = CreateProcessW(nullptr, &cmdline[0], nullptr,nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr,nullptr, &si, &pi);

    CloseHandle(w);
    if (!ok)
    {
        LogWin32Failure(L"CreateProcessW");
        CloseHandle(r);
        return false;
    }

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
    if (ec == 0) {
        g_psMsg = g_ui.powerShellCompleted;
        Log(g_ui.powerShellCompleted.c_str());
        return true;
    }

    g_psErr = true;

    if (g_psOut.find(L"0x80073CFF") != std::wstring::npos) {
        g_psMsg = g_ui.powerShellErrorSideloadDisabled;
        Log(L"[!] PowerShell registration failed because sideloading is disabled (HRESULT 0x80073CFF).");
    } else {
        wchar_t msg[128];
        swprintf(msg, 128, g_ui.powerShellErrorCode.c_str(), ec);
        g_psMsg = msg;
        Log(g_ui.powerShellErrorCode.c_str(), ec);
    }

    Log(g_ui.powerShellOutputFollows.c_str());
    Log(L"%ls", out.c_str());

    return false;
}

bool Appx_Register_COM(const std::wstring& manifestPath)
{
    try
    {
        Log(g_ui.usingComRegistration.c_str());

        winrt::init_apartment(winrt::apartment_type::single_threaded);

        using namespace winrt::Windows::Management::Deployment;
        using namespace winrt::Windows::Foundation;
        using namespace winrt::Windows::Foundation::Collections;

        PackageManager pm;

        std::wstring uriStr = MakeFileUri(manifestPath);
        if (uriStr.empty())
        {
            Log(g_ui.invalidManifestPath.c_str());
            return false;
        }
        Uri uri{ winrt::hstring(uriStr) };

        IVector<Uri> deps = winrt::single_threaded_vector<Uri>();

        auto op = pm.RegisterPackageAsync(
            uri,
            deps,
            DeploymentOptions::ForceUpdateFromAnyVersion
        );

        op.get(); // wait

        if (op.Status() == winrt::Windows::Foundation::AsyncStatus::Completed)
        {
            Log(g_ui.comRegistrationSuccess.c_str());
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
        Log(g_ui.comRegistrationException.c_str(), e.code().value);
        Log(g_ui.comExceptionMessage.c_str(), e.message().c_str());
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

void Generate(const wchar_t* wp, const wchar_t* reason = nullptr)
{
    if (!wp || !*wp){ Log(L"[!] Empty wallpaper."); return; }

    Log(g_ui.startingWallpaperGeneration.c_str(), reason && *reason ? reason : L"an unspecified reason");
    Log(g_ui.wallpaperSource.c_str(), wp);
    Bitmap* src = new Bitmap(wp);
    if (src->GetLastStatus()!=Ok){ Log(g_ui.failedLoadWallpaper.c_str()); delete src; return; }
    FitMode mode = g_disableFitting ? FitMode::Fill : GetWallpaperFit();

    std::wstring exeDir = GetExeDir();
    std::wstring assetsDir = exeDir + L"\\Assets";
    CreateDirectoryW(assetsDir.c_str(), nullptr);

    for (auto& t: g_tiles)
    {
        if (!IniReadI(L"Assets", t.name, 0)){
            Log(g_ui.skippingAssetDisabled.c_str(), t.name);
            continue;
        }
        auto* o = ResizeWithFit(src, t.w, t.h, mode);
        if (o){
            std::wstring outPath = exeDir + L"\\" + t.file;
            if (SavePNG(o, outPath.c_str())) Log(g_ui.savedAsset.c_str(), outPath.c_str());
            else Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());
            delete o;
        }
    }
    delete src;

    Log(g_ui.reRegisteringManifest.c_str());

    std::wstring manifestPath = exeDir + L"\\AppxManifest.xml";
    Log(g_ui.manifestPath.c_str(), manifestPath.c_str());

    bool ok = false;

    if (!UsePowerShell())
    {
        ok = Appx_Register_COM(manifestPath);

        if (!ok)
        {
            Log(g_ui.comRegistrationFailed.c_str());
            std::wstring ps = L"Add-AppxPackage -Register \"" + manifestPath + L"\" -ForceUpdateFromAnyVersion";
            ok = PS_Run(ps);
        }
    }
    else
    {
        std::wstring ps = L"Add-AppxPackage -Register \"" + manifestPath + L"\" -ForceUpdateFromAnyVersion";
        ok = PS_Run(ps);
    }

    if (ok) Log(g_ui.assetGenerationFinished.c_str());
    else    Log(g_ui.appRegistrationFailed.c_str());
}

// -----------------------------------------------------------------------------
// Poll thread
// -----------------------------------------------------------------------------
void PollThread()
{
    std::wstring last = GetWallpaper();
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
        bool fitChanged = (!g_disableFitting && curFit != lastFit);

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
                    const wchar_t* reason = L"change detected";
                    if (g_listenWallpaper && wallpaperChanged2 && g_listenFit && fitChanged2)
                        reason = L"wallpaper and fit change detected";
                    else if (g_listenWallpaper && wallpaperChanged2)
                        reason = L"wallpaper change detected";
                    else if (g_listenFit && fitChanged2)
                        reason = L"fit mode change detected";

                    Log(g_ui.changeConfirmed.c_str());
                    last = cur2;
                    lastFit = fit2;

                    Generate(cur2.c_str(), reason);
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
    ID_LOG_RENAME,
    ID_LOG_OPEN,
    ID_LOG_RESET,
    ID_GENERATE_NOW,
    ID_GENERATE_STARTUP,
    ID_HIDE_DISABLED,
    ID_SHOW_PSLOG,
    ID_LISTEN_WP,
    ID_LISTEN_FIT,
    ID_DISABLE_FIT,

    // Assets
    ID_A1=2001, ID_A2, ID_A3, ID_A4, ID_A5, ID_A6
};

void TrayRemove()
{
    if (g_nid.hWnd){
        Shell_NotifyIconW(NIM_DELETE,&g_nid);
        g_nid = {};
    }
}

void ShowPSLog(HWND h)
{
    if (g_psOut.empty()){
        MessageBoxW(h, g_ui.noPowerShellOutput.c_str(), g_ui.noPowerShellOutputTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    const size_t CH=3000;
    for (size_t p=0;p<g_psOut.size();p+=CH){
        std::wstring s = g_psOut.substr(p,CH);
        MessageBoxW(h, s.c_str(), g_ui.noPowerShellOutputTitle.c_str(), MB_OK | MB_ICONERROR);
    }
}

INT_PTR CALLBACK RenameDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static std::wstring* pPath = nullptr;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        pPath = (std::wstring*)lParam;
        SetDlgItemTextW(hDlg, 1001, pPath->c_str()); // edit box
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            wchar_t buf[MAX_PATH];
            GetDlgItemTextW(hDlg, 1001, buf, MAX_PATH);

            if (wcslen(buf) > 0)
            {
                *pPath = buf;
                EndDialog(hDlg, IDOK);
            }
            else
            {
                MessageBoxW(hDlg, g_ui.pathCannotBeEmpty.c_str(), g_ui.errorTitle.c_str(), MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void Menu(HWND h)
{
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();

    auto add = [&](UINT id, const std::wstring& t, bool chk=false, bool en=true){
        if (g_hideDisabled && !en)
            return;
        AppendMenuW(m,
            MF_STRING |
            (chk ? MF_CHECKED : 0) |
            (!en ? MF_DISABLED : 0),
            id, t.c_str());
    };

    // =====================
    // General
    // =====================
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, g_ui.generalTitle.c_str());
    add(ID_LISTEN_WP, g_ui.listenWallpaper.c_str(), g_listenWallpaper);
    add(ID_TRAYICON, g_ui.trayIcon.c_str(), g_tray);
    add(ID_USE_PS, g_ui.usePowerShell.c_str(), UsePowerShell());
    add(ID_GENERATE_STARTUP, g_ui.generateOnStartup.c_str(), g_generateOnStartup);
    add(ID_HIDE_DISABLED, g_ui.hideDisabledEntries.c_str(), g_hideDisabled);
    add(ID_CONSOLE, g_ui.showConsole.c_str(), g_console);
    add(ID_GENERATE_NOW, g_ui.generateNow.c_str());

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    // =====================
    // Logging
    // =====================
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, g_ui.loggingTitle.c_str());
    add(ID_LOG, g_ui.loggingEnable.c_str(), g_logging);
    add(ID_LOGFILE, g_ui.changePath.c_str(), false, g_logging);
    add(ID_LOG_RENAME, g_ui.renamePath.c_str(), false, g_logging);
    add(ID_LOG_OPEN, g_ui.openInExplorer.c_str(), false, g_logging);
    add(ID_LOG_RESET, g_ui.resetDefault.c_str(), false, g_logging);
    add(ID_SHOW_PSLOG, g_ui.showPowerShellLog.c_str());

    std::wstring pathLine = g_ui.logPathPrefix + g_logPath;
    HMENU hPath = CreatePopupMenu();
    AppendMenuW(hPath, MF_STRING | MF_DISABLED, 0, pathLine.c_str());
    AppendMenuW(m, MF_POPUP, (UINT_PTR)hPath, L"Log Path");

    std::wstring psLine = g_ui.psStatusPrefix;
    if (!g_psMsg.empty())
        psLine += g_psMsg;
    else if (g_psErr)
        psLine += g_ui.errorTitle.c_str();
    else
        psLine += g_ui.okText.c_str();

    HMENU hPs = CreatePopupMenu();
    AppendMenuW(hPs, MF_STRING | MF_DISABLED, 0, psLine.c_str());
    AppendMenuW(m, MF_POPUP, (UINT_PTR)hPs, L"PS Status");

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    // =====================
    // Wallpaper Fitting
    // =====================
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, g_ui.wallpaperFittingTitle.c_str());
    add(ID_DISABLE_FIT, g_ui.disableFitting.c_str(), g_disableFitting);
    add(ID_LISTEN_FIT, g_ui.listenFit.c_str(), g_listenFit, !g_disableFitting);

    // Fit mode display
    FitMode mode = g_disableFitting ? FitMode::Fill : GetWallpaperFit();
    std::wstring fitLine;
    if (g_disableFitting)
        fitLine = g_ui.fitModeForced;
    else {
        fitLine = g_ui.fitModePrefix;
        fitLine += FitModeToString(mode);
    }
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, fitLine.c_str());

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    // =====================
    // Assets
    // =====================
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, g_ui.assetsTitle.c_str());

    const wchar_t* keys[] = {
        L"StoreLogo",L"MediumTile",L"Square44x44Logo",
        L"SmallTile",L"WideTile",L"LargeTile"
    };

    for (int i=0;i<6;i++)
        add(ID_A1+i, keys[i], IniReadI(L"Assets",keys[i],0)!=0);

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    // Exit
    add(ID_EXIT, g_ui.exitText.c_str());

    SetForegroundWindow(h);
    UINT cmd = TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,h,nullptr);
    DestroyMenu(m);

    switch(cmd)
    {
    case ID_USE_PS:
    {
        bool v = !UsePowerShell();
        IniWrite(L"Settings", L"UsePowerShell", v ? L"1" : L"0");
        Log(L"[i] UsePowerShell = %d (PowerShell %s)", v, v ? L"enabled" : L"disabled; COM fallback will be used");
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
                g_ui.currentWallpaperNotFound.c_str(),
                g_ui.generateNowTitle.c_str(),
                MB_OK | MB_ICONWARNING);
        }
        else
        {
            Log(L"[i] Manual generation requested from the tray menu.");
            Generate(wp.c_str(), L"user requested generation from the tray menu");
        }
    }
    break;

    case ID_GENERATE_STARTUP:
    {
        g_generateOnStartup = !g_generateOnStartup;
        IniWrite(L"Settings", L"GenerateOnStartup", g_generateOnStartup ? L"1" : L"0");
        Log(L"[i] GenerateOnStartup %s.", g_generateOnStartup ? L"enabled" : L"disabled");
    }
    break;

    case ID_HIDE_DISABLED:
    {
        g_hideDisabled = !g_hideDisabled;
        IniWrite(L"Settings", L"HideDisabledEntries", g_hideDisabled ? L"1" : L"0");
        Log(L"[i] Hide disabled entries %s.", g_hideDisabled ? L"enabled" : L"disabled");
    }
    break;

    case ID_SHOW_PSLOG:
        ShowPSLog(h);
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

    case ID_DISABLE_FIT:
    {
        g_disableFitting = !g_disableFitting;
        IniWrite(L"Settings", L"DisableFitting", g_disableFitting ? L"1" : L"0");
        if (g_disableFitting)
        {
            // Turn off listener to save CPU
            if (g_listenFit)
            {
                g_listenFit = false;
                IniWrite(L"Settings", L"ListenFit", L"0");
                Log(L"[i] ListenFit auto-disabled due to DisableFitting.");
            }
        }
        Log(L"[i] DisableFitting = %d", (int)g_disableFitting);
    }
    break;

    case ID_LOGFILE:
    {
        wchar_t file[MAX_PATH] = {};
        // Start with current path
        wcscpy_s(file, g_logPath.c_str());
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = h;
        ofn.lpstrFilter = L"Log files (*.log)\0*.log\0All files (*.*)\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = L"log";
        if (GetSaveFileNameW(&ofn))
        {
            g_logPath = file;
            IniWrite(L"Settings", L"LogPath", g_logPath.c_str());

            Log(L"[i] Log path changed to: %s", g_logPath.c_str());
        Log(L"[i] Future log entries will be written to the new path.");
        }
    }
    break;

    case ID_LOG_RENAME:
    {
        std::wstring newPath = g_logPath;
        if (ShowRenameDialog(h, newPath) == IDOK)
        {
            g_logPath = newPath;
            IniWrite(L"Settings", L"LogPath", g_logPath.c_str());
            Log(L"[i] Log path renamed to: %s", g_logPath.c_str());
            Log(L"[i] Future log entries will be written to the renamed path.");
        }
    }
    break;

    case ID_LOG_OPEN:
    {
        if (!g_logPath.empty())
        {
            std::wstring param = L"/select,\"" + g_logPath + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOW);
        }
    }
    break;

    case ID_LOG_RESET:
    {
        std::wstring base = g_exePath.substr(g_exePath.find_last_of(L"\\/") + 1);
        base = base.substr(0, base.find_last_of(L'.'));
        std::wstring dir = g_exePath.substr(0, g_exePath.find_last_of(L"\\/"));
        g_logPath = dir + L"\\" + base + L".log";
        IniWrite(L"Settings", L"LogPath", g_logPath.c_str());
        Log(L"[i] Log path reset to default: %s", g_logPath.c_str());
        Log(L"[i] Future log entries will be written to the default path.");
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
        if (l==WM_RBUTTONUP || l==WM_CONTEXTMENU){
            Menu(h);
            return 0;
        }
    }
    else if (m==WM_DESTROY){
        TrayRemove();
        PostQuitMessage(0);
        return 0;
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

    std::wstring dir = GetExeDir();
    std::wstring base = g_exePath.substr(g_exePath.find_last_of(L"\\/")+1);
    base = base.substr(0, base.find_last_of(L'.'));
    g_iniPath = dir + L"\\" + base + L".ini";

    // defaults
    EnsureIniDefaults();
    EnsureIniStringDefaults();
    LoadUiStrings();
    g_logging = IniReadI(L"Settings",L"Logging",1)!=0;
    g_tray    = IniReadI(L"Settings",L"TrayIcon",1)!=0;
    g_logPath = IniReadS(L"Settings", L"LogPath", L"");
    if (g_logPath.empty())
    {
        g_logPath = dir + L"\\" + base + L".log";
        IniWrite(L"Settings", L"LogPath", g_logPath.c_str());
    }
    g_console = IniReadI(L"Settings", L"ShowConsole", 0) != 0;
    g_generateOnStartup = IniReadI(L"Settings", L"GenerateOnStartup", 1) != 0;
    g_hideDisabled = IniReadI(L"Settings", L"HideDisabledEntries", 0) != 0;
    g_listenWallpaper = IniReadI(L"Settings", L"ListenWallpaper", 1) != 0;
    g_listenFit       = IniReadI(L"Settings", L"ListenFit", 1) != 0;
    g_disableFitting = IniReadI(L"Settings", L"DisableFitting", 0) != 0;

    // GDI+
    GdiplusStartupInput in; ULONG_PTR tk;
    if (GdiplusStartup(&tk,&in,nullptr)!=Ok){ MessageBoxW(nullptr,L"GDI+",g_ui.errorTitle.c_str(),0); return 1; }

    Log(g_ui.programStarting.c_str());
    Log(L"[i] EXE: %s", g_exePath.c_str());
    Log(L"[i] INI: %s", g_iniPath.c_str());
    Log(L"[i] Log file: %s", g_logPath.c_str());
    Log(L"[i] Generate on Startup: %s", g_generateOnStartup ? L"enabled" : L"disabled");
    Log(L"[i] Hide disabled entries: %s", g_hideDisabled ? L"enabled" : L"disabled");
    Log(L"[i] Tray icon: %s", g_tray ? L"enabled" : L"disabled");
    Log(L"[i] PowerShell registration: %s", UsePowerShell() ? L"PowerShell enabled" : L"COM preferred with PowerShell fallback");

    // Console setting
    if (g_console)
    {
        if (!AllocConsole())
            LogWin32Failure(L"AllocConsole");
        else
            Log(L"[i] Console allocated.");

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
    if (!RegisterClassW(&wc))
    {
        LogWin32Failure(L"RegisterClassW");
        GdiplusShutdown(tk);
        return 1;
    }
    g_hwnd = CreateWindowExW(0,L"TrayWndRef",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,hi,nullptr);
    if (!g_hwnd)
    {
        LogWin32Failure(L"CreateWindowExW");
        GdiplusShutdown(tk);
        return 1;
    }

    // Tray
    if (g_tray){
        g_nid.cbSize=sizeof(g_nid);
        g_nid.hWnd=g_hwnd; g_nid.uID=1;
        g_nid.uFlags= NIF_MESSAGE|NIF_ICON|NIF_TIP;
        g_nid.uCallbackMessage=WM_USER+1;
        HICON ic = LoadIconW(nullptr,IDI_APPLICATION);
        if (!ic)
            LogWin32Failure(L"LoadIconW");
        g_nid.hIcon=ic;
        wcscpy_s(g_nid.szTip,L"Desktop Tile Generator");
        if (!Shell_NotifyIconW(NIM_ADD,&g_nid))
        {
            LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)");
            g_tray = false;
        }
    }

    if (g_generateOnStartup)
    {
        std::wstring wp = GetWallpaper();
        if (!wp.empty())
        {
            Log(L"[i] Startup generation enabled.");
            Generate(wp.c_str(), L"Generate on Startup is enabled");
        }
        else
        {
            Log(L"[!] Startup generation skipped: no wallpaper detected.");
        }
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
