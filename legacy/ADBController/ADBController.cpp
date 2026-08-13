// ADBController.cpp
// A direct Win32 GUI for selecting and controlling ADB-over-TCP Android TVs.
//
// The application deliberately never runs `adb kill-server` or `adb start-server`.
// `adb connect` reuses the existing server and starts one only when ADB itself has
// no server yet. Every device command is scoped with `adb -s <ip>:5555` so a
// previously connected TV cannot receive a command intended for the selection.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../../dependencies/desktop_app_baseline.h"

namespace
{
constexpr wchar_t kWindowClass[] = L"AIProjects.ADBController.Window";
constexpr wchar_t kDeviceEditorClass[] = L"AIProjects.ADBController.DeviceEditor";
constexpr wchar_t kWindowTitle[] = L"ADB TV Controller";
constexpr UINT kMessageCommandCompleted = WM_APP + 31;
constexpr UINT kMessageActivateExisting = WM_APP + 32;
constexpr DWORD kAdbTimeoutMs = 30000;
constexpr size_t kMaxCapturedOutputBytes = 1024 * 1024;

enum ControlId
{
    IdConnect = 1001,
    IdRefreshState,
    IdDeviceList,
    IdPower,
    IdHome,
    IdBack,
    IdVolumeDown,
    IdMute,
    IdVolumeUp,
    IdScreenshot,
    IdReboot,
    IdOpenShell,
    IdDisconnectSelected,
    IdDisconnectAll,
    IdAddDevice,
    IdEditDevice,
    IdRemoveDevice,
    IdEditConfiguration,
    IdReloadConfiguration,
    IdClearOutput,
    IdMoreMenu,
    IdThemeAuto,
    IdThemeLight,
    IdThemeDark,
    IdExit,
    IdAbout,
    IdEditorName = 2001,
    IdEditorHost,
    IdEditorSave
};

enum class ThemeMode
{
    Auto,
    Light,
    Dark
};

struct Device
{
    std::wstring name;
    std::wstring host;
    std::wstring serial;
    std::wstring status = L"Unknown";
    bool isConfigured = true;
};

struct DeviceEditorState
{
    std::wstring name;
    std::wstring host;
    bool accepted = false;
};

enum class CommandKind
{
    Connect,
    DisconnectSelected,
    DisconnectAll,
    RefreshState,
    DeviceList,
    KeyEvent,
    Reboot,
    Screenshot
};

struct CommandRequest
{
    CommandKind kind = CommandKind::DeviceList;
    std::wstring action;
    std::wstring adbSetting;
    std::wstring executableDirectory;
    std::wstring screenshotsDirectory;
    Device device;
    std::vector<std::wstring> arguments;
};

struct CommandResult
{
    CommandKind kind = CommandKind::DeviceList;
    std::wstring action;
    std::wstring deviceSerial;
    std::wstring output;
    std::wstring screenshotPath;
    DWORD exitCode = ERROR_GEN_FAILURE;
    DWORD win32Error = ERROR_SUCCESS;
    bool launched = false;
    bool timedOut = false;
    bool outputTruncated = false;
    bool succeeded = false;
};

HINSTANCE g_instance = nullptr;
HANDLE g_instanceMutex = nullptr;
HWND g_window = nullptr;
HWND g_deviceList = nullptr;
HWND g_statusText = nullptr;
HWND g_outputText = nullptr;
HWND g_moreButton = nullptr;
HWND g_tooltipWindow = nullptr;
HWND g_devicesHeading = nullptr;
HWND g_connectionHeading = nullptr;
HWND g_remoteHeading = nullptr;
HWND g_actionsHeading = nullptr;
HWND g_activityHeading = nullptr;
HFONT g_sectionFont = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_subtitleFont = nullptr;
HFONT g_buttonFont = nullptr;
HFONT g_iconFont = nullptr;
HBRUSH g_windowBrush = nullptr;
HBRUSH g_surfaceBrush = nullptr;
HBRUSH g_controlBrush = nullptr;
HMENU g_themeMenu = nullptr;
std::vector<HWND> g_actionControls;
std::vector<Device> g_devices;
std::vector<Device> g_discoveredDevices;
std::vector<std::wstring> g_outputLines;
std::map<HWND, std::wstring> g_actionTooltipTexts;
std::array<RECT, 5> g_cardBounds = {};
HWND g_hoveredButton = nullptr;
std::thread g_worker;
bool g_busy = false;
bool g_existingInstanceActivated = false;
bool g_deviceEditorClassRegistered = false;
ThemeMode g_themeMode = ThemeMode::Auto;
bool g_darkMode = false;

aip::SidecarPaths g_paths;
aip::Utf8Logger g_logger;
std::unique_ptr<aip::IniConfigStore> g_config;

const aip::IniDefaultValue kConfigDefaults[] =
{
    { L"Settings", L"AdbPath", L"adb.exe" },
    { L"Settings", L"Theme", L"Auto" },
    { L"Devices", L"TV Kasir", L"192.168.103.28" },
    { L"Devices", L"TV Billing 2", L"192.168.103.29" },
    { L"Devices", L"TV Billing 3", L"192.168.103.13" },
    { L"Devices", L"TV Billing 4", L"192.168.103.49" },
    { L"Devices", L"TV Billing 5", L"192.168.103.30" }
};

std::wstring GetLastErrorMessage(DWORD error)
{
    std::wstring message = aip::GetLastErrorText(error);
    if (message.empty())
    {
        message = L"Unknown Windows error";
    }
    return message;
}

void Log(const wchar_t* level, const std::wstring& text)
{
    g_logger.Write(level, text);
}

std::wstring ThemeModeText(ThemeMode mode)
{
    switch (mode)
    {
    case ThemeMode::Light: return L"Light";
    case ThemeMode::Dark: return L"Dark";
    default: return L"Auto";
    }
}

ThemeMode ParseThemeMode(const std::wstring& value)
{
    if (aip::IniEquals(aip::Trim(value), L"Light"))
    {
        return ThemeMode::Light;
    }
    if (aip::IniEquals(aip::Trim(value), L"Dark"))
    {
        return ThemeMode::Dark;
    }
    return ThemeMode::Auto;
}

bool IsWindowsAppsThemeDark()
{
    DWORD appsUseLightTheme = 1;
    DWORD size = sizeof(appsUseLightTheme);
    LONG result = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &appsUseLightTheme,
        &size);
    return result == ERROR_SUCCESS && appsUseLightTheme == 0;
}

DWORD WindowsBuildNumber()
{
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto rtlGetVersion = ntdll == nullptr ? nullptr :
        reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (rtlGetVersion == nullptr)
    {
        return 0;
    }
    OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    return rtlGetVersion(&version) >= 0 ? version.dwBuildNumber : 0;
}

using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
using SetPreferredAppModeFn = int(WINAPI*)(int);
using FlushMenuThemesFn = void(WINAPI*)();

AllowDarkModeForWindowFn GetAllowDarkModeForWindow()
{
    HMODULE theme = GetModuleHandleW(L"uxtheme.dll");
    return theme == nullptr ? nullptr : reinterpret_cast<AllowDarkModeForWindowFn>(GetProcAddress(theme, MAKEINTRESOURCEA(133)));
}

void ConfigurePreferredAppMode()
{
    if (WindowsBuildNumber() < 18362)
    {
        return;
    }
    HMODULE theme = GetModuleHandleW(L"uxtheme.dll");
    if (theme == nullptr)
    {
        return;
    }
    auto setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(theme, MAKEINTRESOURCEA(135)));
    if (setPreferredAppMode != nullptr)
    {
        // PreferredAppMode: Default=0, AllowDark=1, ForceDark=2, ForceLight=3.
        int preference = g_themeMode == ThemeMode::Dark ? 2 :
            (g_themeMode == ThemeMode::Light ? 3 : 1);
        setPreferredAppMode(preference);
    }
    auto flushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(theme, MAKEINTRESOURCEA(136)));
    if (flushMenuThemes != nullptr)
    {
        flushMenuThemes();
    }
}

COLORREF WindowBackgroundColor()
{
    return g_darkMode ? RGB(32, 32, 32) : RGB(245, 245, 245);
}

COLORREF SurfaceBackgroundColor()
{
    return g_darkMode ? RGB(43, 43, 43) : RGB(255, 255, 255);
}

COLORREF ControlBackgroundColor()
{
    return g_darkMode ? RGB(54, 54, 54) : RGB(246, 246, 246);
}

COLORREF BorderColor()
{
    return g_darkMode ? RGB(73, 73, 73) : RGB(225, 225, 225);
}

COLORREF AccentColor()
{
    return g_darkMode ? RGB(96, 205, 255) : RGB(0, 103, 192);
}

COLORREF AccentPressedColor()
{
    return g_darkMode ? RGB(73, 171, 219) : RGB(0, 81, 153);
}

COLORREF DangerTextColor()
{
    return g_darkMode ? RGB(255, 153, 164) : RGB(196, 43, 28);
}

COLORREF PrimaryTextColor()
{
    return g_darkMode ? RGB(245, 245, 245) : GetSysColor(COLOR_WINDOWTEXT);
}

COLORREF MutedTextColor()
{
    return g_darkMode ? RGB(190, 190, 190) : GetSysColor(COLOR_GRAYTEXT);
}

void RecreateThemeBrushes()
{
    if (g_windowBrush != nullptr)
    {
        DeleteObject(g_windowBrush);
    }
    if (g_surfaceBrush != nullptr)
    {
        DeleteObject(g_surfaceBrush);
    }
    if (g_controlBrush != nullptr)
    {
        DeleteObject(g_controlBrush);
    }
    g_windowBrush = CreateSolidBrush(WindowBackgroundColor());
    g_surfaceBrush = CreateSolidBrush(SurfaceBackgroundColor());
    g_controlBrush = CreateSolidBrush(ControlBackgroundColor());
}

void ApplyThemeToWindow(HWND window)
{
    if (window == nullptr)
    {
        return;
    }
    if (auto allowDarkModeForWindow = GetAllowDarkModeForWindow())
    {
        allowDarkModeForWindow(window, g_darkMode ? TRUE : FALSE);
    }

    BOOL useDarkMode = g_darkMode ? TRUE : FALSE;
    HRESULT darkResult = DwmSetWindowAttribute(window, 20, &useDarkMode, sizeof(useDarkMode));
    if (FAILED(darkResult))
    {
        DwmSetWindowAttribute(window, 19, &useDarkMode, sizeof(useDarkMode));
    }
}

void ApplyThemeToControl(HWND control)
{
    if (control == nullptr)
    {
        return;
    }
    if (auto allowDarkModeForWindow = GetAllowDarkModeForWindow())
    {
        allowDarkModeForWindow(control, g_darkMode ? TRUE : FALSE);
    }
    SetWindowTheme(control, g_darkMode ? L"Explorer" : nullptr, nullptr);
}

void UpdateThemeMenu()
{
    if (g_themeMenu == nullptr)
    {
        return;
    }
    UINT selected = g_themeMode == ThemeMode::Light ? IdThemeLight :
        (g_themeMode == ThemeMode::Dark ? IdThemeDark : IdThemeAuto);
    CheckMenuRadioItem(g_themeMenu, IdThemeAuto, IdThemeDark, selected, MF_BYCOMMAND);
}

void ApplyCurrentTheme()
{
    g_darkMode = g_themeMode == ThemeMode::Dark ||
        (g_themeMode == ThemeMode::Auto && IsWindowsAppsThemeDark());
    ConfigurePreferredAppMode();
    RecreateThemeBrushes();
    UpdateThemeMenu();

    ApplyThemeToWindow(g_window);
    ApplyThemeToControl(g_deviceList);
    ApplyThemeToControl(g_outputText);
    ApplyThemeToControl(g_statusText);
    ApplyThemeToControl(g_tooltipWindow);
    for (HWND button : g_actionControls)
    {
        ApplyThemeToControl(button);
    }
    for (HWND heading : { g_devicesHeading, g_connectionHeading, g_remoteHeading, g_actionsHeading, g_activityHeading })
    {
        ApplyThemeToControl(heading);
    }

    if (g_deviceList != nullptr)
    {
        ListView_SetTextColor(g_deviceList, PrimaryTextColor());
        ListView_SetTextBkColor(g_deviceList, SurfaceBackgroundColor());
        ListView_SetBkColor(g_deviceList, SurfaceBackgroundColor());
        ApplyThemeToControl(ListView_GetHeader(g_deviceList));
    }
    if (g_outputText != nullptr)
    {
        InvalidateRect(g_outputText, nullptr, TRUE);
    }
    if (g_window != nullptr)
    {
        DrawMenuBar(g_window);
        RedrawWindow(g_window, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}

void SetThemeMode(ThemeMode mode)
{
    if (!g_config->WriteRaw(L"Settings", L"Theme", ThemeModeText(mode)))
    {
        MessageBoxW(g_window,
            (L"Could not save the appearance setting:\r\n" + GetLastErrorMessage(GetLastError())).c_str(),
            kWindowTitle,
            MB_OK | MB_ICONERROR);
        return;
    }
    g_themeMode = mode;
    ApplyCurrentTheme();
    Log(L"info", L"Appearance set to " + ThemeModeText(mode) + L".");
}

int ScaleForWindow(HWND window, int value)
{
    UINT dpi = window == nullptr ? 96 : GetDpiForWindow(window);
    return MulDiv(value, static_cast<int>(dpi == 0 ? 96 : dpi), 96);
}

bool UseCompactLayout(HWND window)
{
    RECT client = {};
    GetClientRect(window, &client);
    return client.bottom - client.top < ScaleForWindow(window, 790);
}

struct ContentArea
{
    int left = 0;
    int width = 1;
    int right = 1;
};

ContentArea GetContentArea(HWND window, const RECT& client, int margin)
{
    int availableWidth = std::max<int>(1, static_cast<int>(client.right - client.left) - margin * 2);
    int maximumWidth = ScaleForWindow(window, 1240);
    int width = std::min(availableWidth, maximumWidth);
    int left = client.left + (static_cast<int>(client.right - client.left) - width) / 2;
    return { left, width, left + width };
}

COLORREF BlendColors(COLORREF first, COLORREF second, int secondPercent)
{
    int firstPercent = 100 - secondPercent;
    return RGB(
        (GetRValue(first) * firstPercent + GetRValue(second) * secondPercent) / 100,
        (GetGValue(first) * firstPercent + GetGValue(second) * secondPercent) / 100,
        (GetBValue(first) * firstPercent + GetBValue(second) * secondPercent) / 100);
}

void DrawRoundedSurface(HDC dc, const RECT& bounds, COLORREF fill, COLORREF border, int radius)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ previousBrush = SelectObject(dc, brush);
    HGDIOBJ previousPen = SelectObject(dc, pen);
    RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, radius, radius);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawTextInRect(HDC dc, const RECT& bounds, const std::wstring& text, HFONT font, COLORREF color, UINT format)
{
    HGDIOBJ previousFont = SelectObject(dc, font != nullptr ? font : GetStockObject(DEFAULT_GUI_FONT));
    int previousMode = SetBkMode(dc, TRANSPARENT);
    COLORREF previousColor = SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), const_cast<RECT*>(&bounds), format);
    SetTextColor(dc, previousColor);
    SetBkMode(dc, previousMode);
    SelectObject(dc, previousFont);
}

enum class ActionButtonStyle
{
    Secondary,
    Primary,
    Destructive
};

ActionButtonStyle StyleForActionButton(int id)
{
    if (id == IdConnect)
    {
        return ActionButtonStyle::Primary;
    }
    if (id == IdRemoveDevice || id == IdDisconnectAll || id == IdReboot)
    {
        return ActionButtonStyle::Destructive;
    }
    return ActionButtonStyle::Secondary;
}

bool IsModernButton(HWND window)
{
    return window == g_moreButton ||
        std::find(g_actionControls.begin(), g_actionControls.end(), window) != g_actionControls.end();
}

const wchar_t* IconForAction(int id)
{
    switch (id)
    {
    case IdConnect: return L"\xE71B";             // Link
    case IdRefreshState: return L"\xE72C";        // Refresh
    case IdDeviceList: return L"\xE772";          // Devices
    case IdAddDevice: return L"\xE710";           // Add
    case IdEditDevice: return L"\xE70F";          // Edit
    case IdRemoveDevice: return L"\xE74D";        // Delete
    case IdPower: return L"\xE7E8";               // Power
    case IdHome: return L"\xE80F";                // Home
    case IdBack: return L"\xE72B";                // Back
    case IdVolumeDown: return L"\xE994";          // Volume down
    case IdMute: return L"\xE74F";                // Mute
    case IdVolumeUp: return L"\xE995";            // Volume up
    case IdScreenshot: return L"\xE722";          // Camera
    case IdReboot: return L"\xE72C";              // Refresh
    case IdOpenShell: return L"\xE756";           // Command prompt
    case IdDisconnectSelected: return L"\xE711";  // Cancel
    case IdDisconnectAll: return L"\xE711";       // Cancel
    case IdClearOutput: return L"\xE74D";         // Delete
    default: return nullptr;
    }
}

int MeasureTextWidth(HDC dc, const wchar_t* text, HFONT font)
{
    SIZE size = {};
    HGDIOBJ previousFont = SelectObject(dc, font != nullptr ? font : GetStockObject(DEFAULT_GUI_FONT));
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
    SelectObject(dc, previousFont);
    return size.cx;
}

void PaintActionButton(const DRAWITEMSTRUCT& draw)
{
    const ActionButtonStyle style = StyleForActionButton(static_cast<int>(draw.CtlID));
    const bool enabled = IsWindowEnabled(draw.hwndItem) != FALSE;
    const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
    const bool focused = (draw.itemState & ODS_FOCUS) != 0;
    const bool hovered = g_hoveredButton == draw.hwndItem;
    COLORREF fill = ControlBackgroundColor();
    COLORREF border = BorderColor();
    COLORREF text = PrimaryTextColor();

    if (!enabled)
    {
        fill = BlendColors(ControlBackgroundColor(), WindowBackgroundColor(), 45);
        border = BlendColors(BorderColor(), WindowBackgroundColor(), 50);
        text = MutedTextColor();
    }
    else if (style == ActionButtonStyle::Primary)
    {
        fill = pressed ? AccentPressedColor() : (hovered ? BlendColors(AccentColor(), RGB(255, 255, 255), g_darkMode ? 10 : 14) : AccentColor());
        border = fill;
        text = g_darkMode ? RGB(20, 20, 20) : RGB(255, 255, 255);
    }
    else if (style == ActionButtonStyle::Destructive)
    {
        text = DangerTextColor();
        if (hovered || pressed)
        {
            fill = g_darkMode ? RGB(82, 43, 47) : RGB(255, 235, 233);
            border = g_darkMode ? RGB(155, 70, 78) : RGB(219, 122, 112);
        }
    }
    else if (hovered || pressed)
    {
        fill = pressed ? BlendColors(ControlBackgroundColor(), WindowBackgroundColor(), 45) :
            BlendColors(ControlBackgroundColor(), PrimaryTextColor(), g_darkMode ? 10 : 5);
    }

    RECT bounds = draw.rcItem;
    HBRUSH backdrop = CreateSolidBrush(draw.hwndItem == g_moreButton ? WindowBackgroundColor() : SurfaceBackgroundColor());
    FillRect(draw.hDC, &bounds, backdrop);
    DeleteObject(backdrop);
    DrawRoundedSurface(draw.hDC, bounds, fill, border, ScaleForWindow(g_window, 5));
    wchar_t label[128] = {};
    GetWindowTextW(draw.hwndItem, label, static_cast<int>(std::size(label)));
    RECT textBounds = bounds;
    InflateRect(&textBounds, -ScaleForWindow(g_window, 10), 0);
    const wchar_t* icon = IconForAction(static_cast<int>(draw.CtlID));
    int iconWidth = icon == nullptr ? 0 : MeasureTextWidth(draw.hDC, icon, g_iconFont);
    int labelWidth = MeasureTextWidth(draw.hDC, label, g_buttonFont);
    int iconGap = iconWidth == 0 ? 0 : ScaleForWindow(g_window, 6);
    int groupWidth = iconWidth + iconGap + labelWidth;
    if (iconWidth == 0 || groupWidth > textBounds.right - textBounds.left)
    {
        DrawTextInRect(draw.hDC, textBounds, label, g_buttonFont, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    else
    {
        int groupLeft = textBounds.left + (textBounds.right - textBounds.left - groupWidth) / 2;
        RECT iconBounds = { groupLeft, textBounds.top, groupLeft + iconWidth, textBounds.bottom };
        RECT labelBounds = { iconBounds.right + iconGap, textBounds.top, textBounds.right, textBounds.bottom };
        DrawTextInRect(draw.hDC, iconBounds, icon, g_iconFont, text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextInRect(draw.hDC, labelBounds, label, g_buttonFont, text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (focused && enabled)
    {
        RECT focusBounds = bounds;
        InflateRect(&focusBounds, -ScaleForWindow(g_window, 4), -ScaleForWindow(g_window, 4));
        DrawFocusRect(draw.hDC, &focusBounds);
    }
}

LRESULT CALLBACK ActionButtonSubclassProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    switch (message)
    {
    case WM_MOUSEMOVE:
    {
        if (g_hoveredButton != window)
        {
            HWND previous = g_hoveredButton;
            g_hoveredButton = window;
            if (previous != nullptr)
            {
                InvalidateRect(previous, nullptr, FALSE);
            }
            InvalidateRect(window, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tracking = {};
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = window;
        TrackMouseEvent(&tracking);
        break;
    }
    case WM_MOUSELEAVE:
        if (g_hoveredButton == window)
        {
            g_hoveredButton = nullptr;
            InvalidateRect(window, nullptr, FALSE);
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        if (g_hoveredButton == window)
        {
            g_hoveredButton = nullptr;
        }
        break;
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

void PaintApplicationChrome(HWND window, HDC dc)
{
    RECT client = {};
    GetClientRect(window, &client);
    FillRect(dc, &client, g_windowBrush != nullptr ? g_windowBrush : GetSysColorBrush(COLOR_WINDOW));

    for (const RECT& card : g_cardBounds)
    {
        if (!IsRectEmpty(&card))
        {
            DrawRoundedSurface(dc, card, SurfaceBackgroundColor(), BorderColor(), ScaleForWindow(window, 6));
        }
    }

    const bool compact = UseCompactLayout(window);
    const int margin = ScaleForWindow(window, compact ? 10 : 14);
    ContentArea content = GetContentArea(window, client, margin);
    const int glyphSize = ScaleForWindow(window, compact ? 34 : 38);
    const int headerTop = ScaleForWindow(window, compact ? 10 : 16);
    RECT glyph = { content.left, headerTop, content.left + glyphSize, headerTop + glyphSize };
    DrawRoundedSurface(dc, glyph, AccentColor(), AccentColor(), ScaleForWindow(window, 10));
    DrawTextInRect(dc, glyph, L"TV", g_sectionFont, g_darkMode ? RGB(20, 20, 20) : RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT title = { glyph.right + ScaleForWindow(window, 12), headerTop - ScaleForWindow(window, 2), 0, headerTop + ScaleForWindow(window, 26) };
    title.right = content.right - (g_moreButton != nullptr ? ScaleForWindow(window, 42) : 0);
    DrawTextInRect(dc, title, L"ADB TV Controller", g_titleFont, PrimaryTextColor(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT subtitle = title;
    subtitle.top = headerTop + ScaleForWindow(window, compact ? 21 : 27);
    subtitle.bottom = headerTop + glyphSize + ScaleForWindow(window, compact ? 5 : 8);
    DrawTextInRect(dc, subtitle, L"Direct ADB-over-TCP control  ·  The server stays running when you switch TVs", g_subtitleFont, MutedTextColor(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

std::wstring QuoteWindowsArgument(const std::wstring& value)
{
    if (value.empty())
    {
        return L"\"\"";
    }

    bool needsQuotes = value.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes)
    {
        return value;
    }

    std::wstring quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back(L'\"');
    size_t slashes = 0;
    for (wchar_t ch : value)
    {
        if (ch == L'\\')
        {
            ++slashes;
            continue;
        }
        if (ch == L'\"')
        {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted.push_back(ch);
            slashes = 0;
            continue;
        }
        quoted.append(slashes, L'\\');
        slashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::wstring JoinCommandLine(const std::wstring& executable, const std::vector<std::wstring>& arguments)
{
    std::wstring line = QuoteWindowsArgument(executable);
    for (const auto& argument : arguments)
    {
        line.push_back(L' ');
        line += QuoteWindowsArgument(argument);
    }
    return line;
}

bool IsSafeDeviceName(const std::wstring& name)
{
    if (name.empty() || name.size() > 80)
    {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](wchar_t ch) {
        return ch < 0x20 || ch == 0x7f || ch == L'[' || ch == L']';
    });
}

bool TryNormalizeDeviceEndpoint(const std::wstring& value, std::wstring& endpoint)
{
    endpoint.clear();
    std::wstring input = aip::Trim(value);
    if (input.empty() || input.size() > 259)
    {
        return false;
    }

    std::wstring host = input;
    std::wstring port;
    size_t colon = input.rfind(L':');
    if (colon != std::wstring::npos)
    {
        host = input.substr(0, colon);
        port = input.substr(colon + 1);
        if (host.empty() || port.empty() || input.find(L':') != colon || port.size() > 5 ||
            !std::all_of(port.begin(), port.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; }))
        {
            return false;
        }
        unsigned long parsedPort = 0;
        for (wchar_t ch : port)
        {
            parsedPort = parsedPort * 10 + static_cast<unsigned long>(ch - L'0');
        }
        if (parsedPort == 0 || parsedPort > 65535)
        {
            return false;
        }
        port = std::to_wstring(parsedPort);
    }

    if (host.empty() || host.size() > 253 || host.front() == L'.' || host.back() == L'.' ||
        !std::all_of(host.begin(), host.end(), [](wchar_t ch) {
        return iswalnum(ch) != 0 || ch == L'.' || ch == L'-';
        }))
    {
        return false;
    }

    endpoint = host + (port.empty() ? L"" : L":" + port);
    return true;
}

bool IsSafeDeviceHost(const std::wstring& host)
{
    std::wstring endpoint;
    return TryNormalizeDeviceEndpoint(host, endpoint);
}

std::wstring SerialForConfiguredEndpoint(const std::wstring& endpoint)
{
    return endpoint.find(L':') == std::wstring::npos ? endpoint + L":5555" : endpoint;
}

std::wstring GetWindowTextValue(HWND window)
{
    int length = GetWindowTextLengthW(window);
    if (length <= 0)
    {
        return L"";
    }
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    value.resize(static_cast<size_t>(GetWindowTextW(window, value.data(), length + 1)));
    return value;
}

std::wstring SelectedAdbPath()
{
    return g_config ? aip::Trim(g_config->ReadRaw(L"Settings", L"AdbPath", L"adb.exe")) : L"adb.exe";
}

bool TrySearchPath(const std::wstring& fileName, std::wstring& fullPath)
{
    std::vector<wchar_t> buffer(512);
    for (;;)
    {
        DWORD copied = SearchPathW(nullptr, fileName.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (copied == 0)
        {
            return false;
        }
        if (copied < buffer.size())
        {
            fullPath.assign(buffer.data(), copied);
            return true;
        }
        if (buffer.size() >= 32768)
        {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return false;
        }
        buffer.resize(static_cast<size_t>(copied) + 1);
    }
}

bool ResolveAdbExecutable(const std::wstring& setting, const std::wstring& executableDirectory, std::wstring& adbPath, std::wstring& error)
{
    adbPath.clear();
    std::wstring value = aip::Trim(setting);
    if (value.empty() || value.find(L'\"') != std::wstring::npos)
    {
        error = L"[Settings] AdbPath must be a non-empty executable path without quote characters.";
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    bool hasPathSeparator = value.find_first_of(L"\\/") != std::wstring::npos;
    if (!hasPathSeparator)
    {
        std::wstring besideProgram = aip::PathJoin(executableDirectory, value);
        if (aip::FileExists(besideProgram))
        {
            adbPath = besideProgram;
            return true;
        }
        if (TrySearchPath(value, adbPath))
        {
            return true;
        }
    }
    else
    {
        std::wstring candidate = PathIsRelativeW(value.c_str())
            ? aip::PathJoin(executableDirectory, value)
            : value;
        if (aip::FileExists(candidate))
        {
            adbPath = candidate;
            return true;
        }
    }

    DWORD lookupError = GetLastError();
    error = L"Could not find adb.exe. Put adb.exe beside ADBController.exe, add it to PATH, or set [Settings] AdbPath in:\r\n" +
        (g_config ? g_config->Path() : L"ADBController.ini");
    SetLastError(lookupError == ERROR_SUCCESS ? ERROR_FILE_NOT_FOUND : lookupError);
    return false;
}

void AppendCapturedBytes(std::vector<BYTE>& output, const BYTE* bytes, DWORD count, bool& truncated)
{
    if (count == 0 || truncated)
    {
        return;
    }
    size_t room = kMaxCapturedOutputBytes - output.size();
    size_t toCopy = std::min(room, static_cast<size_t>(count));
    output.insert(output.end(), bytes, bytes + toCopy);
    if (toCopy != count)
    {
        truncated = true;
    }
}

void DrainAvailablePipe(HANDLE pipe, std::vector<BYTE>& output, bool& truncated)
{
    for (;;)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
        {
            return;
        }
        if (available == 0)
        {
            return;
        }

        std::array<BYTE, 4096> buffer = {};
        DWORD read = 0;
        DWORD requested = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
        if (!ReadFile(pipe, buffer.data(), requested, &read, nullptr) || read == 0)
        {
            return;
        }
        AppendCapturedBytes(output, buffer.data(), read, truncated);
    }
}

CommandResult RunProcessCapture(
    const std::wstring& executable,
    const std::vector<std::wstring>& arguments,
    DWORD timeoutMs,
    HANDLE stdoutFile = INVALID_HANDLE_VALUE)
{
    CommandResult result;
    std::vector<BYTE> bytes;

    SECURITY_ATTRIBUTES security = {};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE pipeRead = nullptr;
    HANDLE pipeWrite = nullptr;
    HANDLE nulInput = INVALID_HANDLE_VALUE;
    HANDLE process = nullptr;
    HANDLE thread = nullptr;

    auto cleanup = [&]() {
        if (thread != nullptr) CloseHandle(thread);
        if (process != nullptr) CloseHandle(process);
        if (pipeWrite != nullptr) CloseHandle(pipeWrite);
        if (pipeRead != nullptr) CloseHandle(pipeRead);
        if (nulInput != INVALID_HANDLE_VALUE) CloseHandle(nulInput);
    };

    if (!CreatePipe(&pipeRead, &pipeWrite, &security, 0) ||
        !SetHandleInformation(pipeRead, HANDLE_FLAG_INHERIT, 0))
    {
        result.win32Error = GetLastError();
        result.output = L"Could not prepare ADB output capture: " + GetLastErrorMessage(result.win32Error);
        cleanup();
        return result;
    }

    nulInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nulInput == INVALID_HANDLE_VALUE)
    {
        result.win32Error = GetLastError();
        result.output = L"Could not prepare ADB input handle: " + GetLastErrorMessage(result.win32Error);
        cleanup();
        return result;
    }

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nulInput;
    startup.hStdOutput = stdoutFile == INVALID_HANDLE_VALUE ? pipeWrite : stdoutFile;
    startup.hStdError = pipeWrite;

    PROCESS_INFORMATION processInfo = {};
    std::wstring commandLine = JoinCommandLine(executable, arguments);
    if (!CreateProcessW(
            executable.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &processInfo))
    {
        result.win32Error = GetLastError();
        result.output = L"Could not start adb.exe: " + GetLastErrorMessage(result.win32Error);
        cleanup();
        return result;
    }

    process = processInfo.hProcess;
    thread = processInfo.hThread;
    result.launched = true;
    CloseHandle(pipeWrite);
    pipeWrite = nullptr;
    CloseHandle(nulInput);
    nulInput = INVALID_HANDLE_VALUE;

    ULONGLONG started = GetTickCount64();
    for (;;)
    {
        DrainAvailablePipe(pipeRead, bytes, result.outputTruncated);
        DWORD wait = WaitForSingleObject(process, 25);
        if (wait == WAIT_OBJECT_0)
        {
            DrainAvailablePipe(pipeRead, bytes, result.outputTruncated);
            break;
        }
        if (wait == WAIT_FAILED)
        {
            result.win32Error = GetLastError();
            break;
        }
        if (GetTickCount64() - started >= timeoutMs)
        {
            result.timedOut = true;
            TerminateProcess(process, ERROR_TIMEOUT);
            WaitForSingleObject(process, 5000);
            DrainAvailablePipe(pipeRead, bytes, result.outputTruncated);
            break;
        }
    }

    if (!GetExitCodeProcess(process, &result.exitCode))
    {
        result.win32Error = GetLastError();
    }
    aip::DecodeTextBytes(bytes, result.output);
    if (result.outputTruncated)
    {
        result.output += L"\r\n[ADB output was truncated after 1 MiB.]";
    }
    cleanup();
    return result;
}

bool OutputReportsFailure(const std::wstring& output)
{
    std::wstring lower = aip::ToLower(output);
    static const wchar_t* failures[] =
    {
        L"failed to connect",
        L"cannot connect",
        L"unable to connect",
        L"error:",
        L"error ",
        L"device not found",
        L"no devices/emulators found",
        L"more than one device"
    };
    for (const wchar_t* failure : failures)
    {
        if (lower.find(failure) != std::wstring::npos)
        {
            return true;
        }
    }
    return false;
}

CommandResult RunAdb(
    const std::wstring& adbSetting,
    const std::wstring& executableDirectory,
    const std::vector<std::wstring>& arguments,
    DWORD timeoutMs = kAdbTimeoutMs)
{
    CommandResult result;
    std::wstring adbPath;
    std::wstring error;
    if (!ResolveAdbExecutable(adbSetting, executableDirectory, adbPath, error))
    {
        result.win32Error = GetLastError();
        result.output = error;
        return result;
    }

    result = RunProcessCapture(adbPath, arguments, timeoutMs);
    result.succeeded = result.launched && !result.timedOut && result.win32Error == ERROR_SUCCESS &&
        result.exitCode == 0 && !OutputReportsFailure(result.output);
    return result;
}

std::wstring SafeFileNameComponent(const std::wstring& value)
{
    std::wstring result;
    result.reserve(value.size());
    for (wchar_t ch : value)
    {
        result.push_back((iswalnum(ch) != 0 || ch == L'-' || ch == L'_') ? ch : L'_');
    }
    while (!result.empty() && result.back() == L'_')
    {
        result.pop_back();
    }
    return result.empty() ? L"device" : result;
}

std::wstring BuildScreenshotPath(const std::wstring& directory, const Device& device)
{
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    wchar_t timestamp[64] = {};
    swprintf_s(timestamp,
        L"%04u%02u%02u_%02u%02u%02u_%03u.png",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds);
    return aip::PathJoin(directory, SafeFileNameComponent(device.name) + L"_" + timestamp);
}

CommandResult RunScreenshot(const CommandRequest& request)
{
    CommandResult result;
    std::wstring adbPath;
    std::wstring resolveError;
    if (!ResolveAdbExecutable(request.adbSetting, request.executableDirectory, adbPath, resolveError))
    {
        result.win32Error = GetLastError();
        result.output = resolveError;
        return result;
    }

    aip::EnsureDirectory(request.screenshotsDirectory);
    if (!aip::FileExists(request.screenshotsDirectory) &&
        GetFileAttributesW(request.screenshotsDirectory.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        result.win32Error = GetLastError();
        result.output = L"Could not create screenshot directory: " + request.screenshotsDirectory + L"\r\n" +
            GetLastErrorMessage(result.win32Error);
        return result;
    }

    std::wstring target = BuildScreenshotPath(request.screenshotsDirectory, request.device);
    std::wstring temporary = target + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    SECURITY_ATTRIBUTES security = {};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE output = CreateFileW(
        temporary.c_str(),
        GENERIC_WRITE,
        0,
        &security,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (output == INVALID_HANDLE_VALUE)
    {
        result.win32Error = GetLastError();
        result.output = L"Could not create screenshot file: " + GetLastErrorMessage(result.win32Error);
        return result;
    }

    result = RunProcessCapture(adbPath, request.arguments, kAdbTimeoutMs, output);
    if (!CloseHandle(output) && result.win32Error == ERROR_SUCCESS)
    {
        result.win32Error = GetLastError();
    }
    result.succeeded = result.launched && !result.timedOut && result.win32Error == ERROR_SUCCESS && result.exitCode == 0 &&
        !OutputReportsFailure(result.output);
    if (result.succeeded)
    {
        LARGE_INTEGER size = {};
        HANDLE verify = CreateFileW(temporary.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        bool valid = verify != INVALID_HANDLE_VALUE && GetFileSizeEx(verify, &size) && size.QuadPart > 0;
        DWORD verifyError = valid ? ERROR_SUCCESS : GetLastError();
        if (verify != INVALID_HANDLE_VALUE)
        {
            CloseHandle(verify);
        }
        if (!valid || !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            result.succeeded = false;
            result.win32Error = verifyError == ERROR_SUCCESS ? GetLastError() : verifyError;
            result.output += L"\r\nThe screenshot was not saved: " + GetLastErrorMessage(result.win32Error);
            DeleteFileW(temporary.c_str());
        }
        else
        {
            result.screenshotPath = target;
        }
    }
    else
    {
        DeleteFileW(temporary.c_str());
    }
    return result;
}

CommandResult ExecuteRequest(const CommandRequest& request)
{
    CommandResult result = request.kind == CommandKind::Screenshot
        ? RunScreenshot(request)
        : RunAdb(request.adbSetting, request.executableDirectory, request.arguments);
    result.kind = request.kind;
    result.action = request.action;
    result.deviceSerial = request.device.serial;
    return result;
}

void SetWindowTextSafe(HWND window, const std::wstring& text)
{
    if (window != nullptr)
    {
        SetWindowTextW(window, text.c_str());
    }
}

void AddWrappedOutputLine(HDC dc, const std::wstring& line, int maximumWidth)
{
    if (line.empty())
    {
        SendMessageW(g_outputText, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L""));
        return;
    }

    size_t start = 0;
    while (start < line.size())
    {
        size_t end = start;
        size_t lastBreak = std::wstring::npos;
        SIZE measured = {};
        for (; end < line.size(); ++end)
        {
            if (line[end] == L' ' || line[end] == L'\t')
            {
                lastBreak = end;
            }
            std::wstring candidate = line.substr(start, end - start + 1);
            GetTextExtentPoint32W(dc, candidate.c_str(), static_cast<int>(candidate.size()), &measured);
            if (measured.cx > maximumWidth)
            {
                break;
            }
        }

        if (end == line.size())
        {
            SendMessageW(g_outputText, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.substr(start).c_str()));
            break;
        }

        size_t lineEnd = lastBreak != std::wstring::npos && lastBreak > start ? lastBreak :
            (end > start ? end : end + 1);
        SendMessageW(g_outputText, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.substr(start, lineEnd - start).c_str()));
        start = lineEnd;
        while (start < line.size() && (line[start] == L' ' || line[start] == L'\t'))
        {
            ++start;
        }
    }
}

void ReflowActivityOutput()
{
    if (g_outputText == nullptr)
    {
        return;
    }

    SendMessageW(g_outputText, LB_RESETCONTENT, 0, 0);
    RECT client = {};
    GetClientRect(g_outputText, &client);
    int maximumWidth = std::max<int>(ScaleForWindow(g_window, 24),
        static_cast<int>(client.right - client.left) - ScaleForWindow(g_window, 12) - GetSystemMetrics(SM_CXVSCROLL));
    HDC dc = GetDC(g_outputText);
    HGDIOBJ previousFont = SelectObject(dc, g_buttonFont != nullptr ? g_buttonFont : GetStockObject(DEFAULT_GUI_FONT));
    for (const std::wstring& line : g_outputLines)
    {
        AddWrappedOutputLine(dc, line, maximumWidth);
    }
    SelectObject(dc, previousFont);
    ReleaseDC(g_outputText, dc);

    int count = static_cast<int>(SendMessageW(g_outputText, LB_GETCOUNT, 0, 0));
    if (count > 0)
    {
        SendMessageW(g_outputText, LB_SETTOPINDEX, static_cast<WPARAM>(count - 1), 0);
    }
}

void AppendOutput(const std::wstring& text)
{
    if (text.empty())
    {
        return;
    }

    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find_first_of(L"\r\n", start);
        g_outputLines.push_back(end == std::wstring::npos ? text.substr(start) : text.substr(start, end - start));
        if (end == std::wstring::npos)
        {
            break;
        }
        start = end + 1;
        if (start < text.size() && text[end] == L'\r' && text[start] == L'\n')
        {
            ++start;
        }
    }

    while (g_outputLines.size() > 1000)
    {
        g_outputLines.erase(g_outputLines.begin());
    }
    ReflowActivityOutput();
}

void ClearOutput()
{
    g_outputLines.clear();
    ReflowActivityOutput();
    Log(L"info", L"Activity output cleared from the window.");
}

void SetBusy(bool busy)
{
    g_busy = busy;
    for (HWND control : g_actionControls)
    {
        EnableWindow(control, !busy);
    }
    // The request already owns a copy of the selected device, so leaving the
    // list enabled is safe and avoids Windows repainting it as a white disabled
    // control while ADB is working.
    SetWindowTextSafe(g_statusText, busy ? L"Running ADB command..." : L"Ready");
    InvalidateRect(g_window, nullptr, FALSE);
}

int SelectedDeviceIndex()
{
    if (g_deviceList == nullptr)
    {
        return -1;
    }
    int row = ListView_GetNextItem(g_deviceList, -1, LVNI_SELECTED);
    if (row < 0)
    {
        return -1;
    }
    LVITEMW item = {};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (!ListView_GetItem(g_deviceList, &item) || item.lParam < 0 ||
        static_cast<size_t>(item.lParam) >= g_devices.size())
    {
        return -1;
    }
    return static_cast<int>(item.lParam);
}

std::wstring SelectedTargetForTooltip()
{
    int selected = SelectedDeviceIndex();
    if (selected >= 0)
    {
        return g_devices[static_cast<size_t>(selected)].serial;
    }
    return L"<selected device>";
}

std::wstring TooltipForAction(int id)
{
    const std::wstring target = SelectedTargetForTooltip();
    const std::wstring scopedPrefix = L"adb -s " + target + L" ";
    switch (id)
    {
    case IdConnect:
        return L"Connects the selected TV without restarting the ADB server.\r\nRuns: adb connect " + target;
    case IdRefreshState:
        return L"Reads the selected TV's current ADB connection state.\r\nRuns: " + scopedPrefix + L"get-state";
    case IdDeviceList:
        return L"Scans connected USB and wireless devices, then refreshes this list.\r\nRuns: adb devices -l";
    case IdAddDevice:
        return L"Adds a named TV entry to the local [Devices] configuration.\r\nNo ADB command is run.";
    case IdEditDevice:
        return L"Edits the selected [Devices] entry in the local configuration.\r\nNo ADB command is run.";
    case IdRemoveDevice:
        return L"Removes the selected [Devices] entry from the local configuration.\r\nNo ADB command is run.";
    case IdPower:
        return L"Sends the Android Power key event to the selected TV.\r\nRuns: " + scopedPrefix + L"shell input keyevent 26";
    case IdHome:
        return L"Sends the Android Home key event to the selected TV.\r\nRuns: " + scopedPrefix + L"shell input keyevent 3";
    case IdBack:
        return L"Sends the Android Back key event to the selected TV.\r\nRuns: " + scopedPrefix + L"shell input keyevent 4";
    case IdVolumeDown:
        return L"Sends the Android Volume Down key event to the selected TV.\r\nRuns: " + scopedPrefix + L"shell input keyevent 25";
    case IdMute:
        return L"Sends the Android Mute key event to the selected TV.\r\nRuns: " + scopedPrefix + L"shell input keyevent 164";
    case IdVolumeUp:
        return L"Sends the Android Volume Up key event to the selected TV.\r\nRuns: " + scopedPrefix + L"shell input keyevent 24";
    case IdScreenshot:
        return L"Captures a PNG screenshot and saves it in the local Screenshots folder.\r\nRuns: " + scopedPrefix + L"exec-out screencap -p";
    case IdReboot:
        return L"Reboots the selected TV after confirmation.\r\nRuns: " + scopedPrefix + L"reboot";
    case IdOpenShell:
        return L"Opens a separate Command Prompt connected to the selected TV.\r\nRuns: " + scopedPrefix + L"shell";
    case IdDisconnectSelected:
        return L"Disconnects only the selected network TV; the ADB server stays running.\r\nRuns: adb disconnect " + target;
    case IdDisconnectAll:
        return L"Disconnects every network device known to this ADB server after confirmation.\r\nRuns: adb disconnect";
    case IdClearOutput:
        return L"Clears only the activity display in this window.\r\nNo ADB command is run.";
    default:
        return L"Action details are unavailable.";
    }
}

void CreateActionTooltips(HWND parent)
{
    g_tooltipWindow = CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON | TTS_NOPREFIX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        parent,
        nullptr,
        g_instance,
        nullptr);
    if (g_tooltipWindow != nullptr)
    {
        SendMessageW(g_tooltipWindow, TTM_SETMAXTIPWIDTH, 0, ScaleForWindow(parent, 460));
        SendMessageW(g_tooltipWindow, TTM_SETDELAYTIME, TTDT_INITIAL, 300);
        SendMessageW(g_tooltipWindow, TTM_ACTIVATE, TRUE, 0);
        ApplyThemeToControl(g_tooltipWindow);
    }
}

void RegisterActionTooltip(HWND control)
{
    if (g_tooltipWindow == nullptr || control == nullptr)
    {
        return;
    }

    TOOLINFOW tool = {};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = g_window;
    tool.uId = reinterpret_cast<UINT_PTR>(control);
    auto item = g_actionTooltipTexts.emplace(control, TooltipForAction(GetDlgCtrlID(control))).first;
    tool.lpszText = const_cast<wchar_t*>(item->second.c_str());
    SendMessageW(g_tooltipWindow, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
}

void UpdateActionTooltips()
{
    if (g_tooltipWindow == nullptr)
    {
        return;
    }

    for (HWND control : g_actionControls)
    {
        auto& text = g_actionTooltipTexts[control];
        text = TooltipForAction(GetDlgCtrlID(control));
        TOOLINFOW tool = {};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = g_window;
        tool.uId = reinterpret_cast<UINT_PTR>(control);
        tool.lpszText = const_cast<wchar_t*>(text.c_str());
        SendMessageW(g_tooltipWindow, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}

bool GetSelectedDevice(Device& device)
{
    int index = SelectedDeviceIndex();
    if (index < 0)
    {
        MessageBoxW(g_window, L"Select a TV first.", kWindowTitle, MB_OK | MB_ICONINFORMATION);
        return false;
    }
    device = g_devices[static_cast<size_t>(index)];
    return true;
}

void UpdateDeviceRow(size_t index)
{
    if (index >= g_devices.size() || g_deviceList == nullptr)
    {
        return;
    }
    int row = -1;
    LVFINDINFOW find = {};
    find.flags = LVFI_PARAM;
    find.lParam = static_cast<LPARAM>(index);
    row = ListView_FindItem(g_deviceList, -1, &find);
    if (row >= 0)
    {
        ListView_SetItemText(g_deviceList, row, 2, const_cast<wchar_t*>(g_devices[index].status.c_str()));
    }
}

void SetDeviceStatus(const std::wstring& serial, const std::wstring& status)
{
    for (size_t i = 0; i < g_devices.size(); ++i)
    {
        if (g_devices[i].serial == serial)
        {
            g_devices[i].status = status;
            UpdateDeviceRow(i);
            return;
        }
    }
}

bool LoadDevicesFromConfiguration(std::wstring& error)
{
    error.clear();
    std::vector<aip::IniSectionData> document;
    if (!aip::LoadIniDocument(g_config->Path(), document))
    {
        error = L"Could not read the configuration file:\r\n" + g_config->Path() + L"\r\n\r\n" +
            GetLastErrorMessage(GetLastError());
        return false;
    }

    std::vector<Device> loaded;
    for (const auto& section : document)
    {
        if (!aip::IniEquals(section.name, L"Devices"))
        {
            continue;
        }
        for (const auto& entry : section.entries)
        {
            std::wstring name = aip::Trim(entry.key);
            std::wstring host;
            if (!IsSafeDeviceName(name) || !TryNormalizeDeviceEndpoint(entry.value, host))
            {
                Log(L"warning", L"Ignored invalid [Devices] entry: " + name + L" = " + aip::Trim(entry.value));
                continue;
            }
            Device device;
            device.name = name;
            device.host = host;
            device.serial = SerialForConfiguredEndpoint(host);
            for (const Device& previous : g_devices)
            {
                if (previous.serial == device.serial)
                {
                    device.status = previous.status;
                    break;
                }
            }
            for (const Device& discovered : g_discoveredDevices)
            {
                if (discovered.serial == device.serial)
                {
                    device.status = discovered.status;
                    break;
                }
            }
            loaded.push_back(std::move(device));
        }
    }

    if (loaded.empty())
    {
        error = L"No valid TV entries were found in [Devices].\r\n\r\nEdit:\r\n" + g_config->Path();
        return false;
    }

    for (const Device& discovered : g_discoveredDevices)
    {
        bool alreadyConfigured = std::any_of(loaded.begin(), loaded.end(), [&](const Device& device) {
            return device.serial == discovered.serial;
        });
        if (!alreadyConfigured)
        {
            loaded.push_back(discovered);
        }
    }
    g_devices = std::move(loaded);
    return true;
}

bool EnsureInitialConfiguration(std::wstring& error)
{
    error.clear();
    DWORD attributes = GetFileAttributesW(g_config->Path().c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
    {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            error = L"The configuration path is a directory, not a file:\r\n" + g_config->Path();
            SetLastError(ERROR_DIRECTORY);
            return false;
        }
        return true;
    }

    DWORD lookupError = GetLastError();
    if (lookupError != ERROR_FILE_NOT_FOUND && lookupError != ERROR_PATH_NOT_FOUND)
    {
        error = L"Could not access the configuration file:\r\n" + g_config->Path() + L"\r\n\r\n" +
            GetLastErrorMessage(lookupError);
        return false;
    }
    if (!g_config->EnsureDefaults(kConfigDefaults, ARRAYSIZE(kConfigDefaults)))
    {
        error = L"Could not create the initial configuration file:\r\n" + g_config->Path() + L"\r\n\r\n" +
            GetLastErrorMessage(GetLastError());
        return false;
    }
    return true;
}

void PopulateDeviceList()
{
    std::wstring selectedSerial;
    int selected = SelectedDeviceIndex();
    if (selected >= 0)
    {
        selectedSerial = g_devices[static_cast<size_t>(selected)].serial;
    }
    ListView_DeleteAllItems(g_deviceList);
    for (size_t i = 0; i < g_devices.size(); ++i)
    {
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(g_devices[i].name.c_str());
        item.lParam = static_cast<LPARAM>(i);
        int row = ListView_InsertItem(g_deviceList, &item);
        ListView_SetItemText(g_deviceList, row, 1, const_cast<wchar_t*>(g_devices[i].host.c_str()));
        ListView_SetItemText(g_deviceList, row, 2, const_cast<wchar_t*>(g_devices[i].status.c_str()));
    }

    if (!g_devices.empty())
    {
        int select = 0;
        for (size_t i = 0; i < g_devices.size(); ++i)
        {
            if (g_devices[i].serial == selectedSerial)
            {
                select = static_cast<int>(i);
                break;
            }
        }
        ListView_SetItemState(g_deviceList, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    UpdateActionTooltips();
}

void SelectDeviceByName(const std::wstring& name)
{
    for (size_t i = 0; i < g_devices.size(); ++i)
    {
        if (aip::IniEquals(g_devices[i].name, name.c_str()))
        {
            ListView_SetItemState(g_deviceList, static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(g_deviceList, static_cast<int>(i), FALSE);
            return;
        }
    }
}

bool IsNetworkAdbSerial(const std::wstring& serial)
{
    std::wstring endpoint;
    return TryNormalizeDeviceEndpoint(serial, endpoint) && endpoint.find(L':') != std::wstring::npos;
}

size_t MergeDiscoveredAdbDevices(const std::wstring& output)
{
    std::vector<Device> discovered;
    size_t start = 0;
    while (start < output.size())
    {
        size_t end = output.find_first_of(L"\r\n", start);
        std::wstring line = aip::Trim(output.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (!line.empty() && line.rfind(L"List of devices attached", 0) != 0 && line.front() != L'*')
        {
            size_t serialEnd = line.find_first_of(L" \t");
            if (serialEnd != std::wstring::npos)
            {
                std::wstring serial = line.substr(0, serialEnd);
                size_t stateStart = line.find_first_not_of(L" \t", serialEnd);
                size_t stateEnd = stateStart == std::wstring::npos ? std::wstring::npos : line.find_first_of(L" \t", stateStart);
                std::wstring state = stateStart == std::wstring::npos ? L"Unknown" : line.substr(stateStart, stateEnd == std::wstring::npos ? std::wstring::npos : stateEnd - stateStart);
                if (!serial.empty() && !std::any_of(discovered.begin(), discovered.end(), [&](const Device& device) {
                    return device.serial == serial;
                }))
                {
                    Device device;
                    device.name = (IsNetworkAdbSerial(serial) ? L"Wireless — " : L"USB / local — ") + serial;
                    device.host = serial;
                    device.serial = serial;
                    device.status = state;
                    device.isConfigured = false;
                    discovered.push_back(std::move(device));
                }
            }
        }

        if (end == std::wstring::npos)
        {
            break;
        }
        start = end + 1;
        if (start < output.size() && output[end] == L'\r' && output[start] == L'\n')
        {
            ++start;
        }
    }

    g_discoveredDevices = std::move(discovered);
    std::vector<Device> merged;
    for (const Device& existing : g_devices)
    {
        if (!existing.isConfigured)
        {
            continue;
        }
        Device configured = existing;
        auto match = std::find_if(g_discoveredDevices.begin(), g_discoveredDevices.end(), [&](const Device& device) {
            return device.serial == configured.serial;
        });
        configured.status = match == g_discoveredDevices.end() ? L"Not connected" : match->status;
        merged.push_back(std::move(configured));
    }
    for (const Device& device : g_discoveredDevices)
    {
        bool alreadyConfigured = std::any_of(merged.begin(), merged.end(), [&](const Device& configured) {
            return configured.serial == device.serial;
        });
        if (!alreadyConfigured)
        {
            merged.push_back(device);
        }
    }
    g_devices = std::move(merged);
    PopulateDeviceList();
    return g_discoveredDevices.size();
}

void ReloadConfiguration()
{
    if (g_busy)
    {
        return;
    }
    std::wstring error;
    if (!LoadDevicesFromConfiguration(error))
    {
        Log(L"error", error);
        MessageBoxW(g_window, error.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        return;
    }
    PopulateDeviceList();
    AppendOutput(L"Configuration reloaded from: " + g_config->Path());
    Log(L"info", L"Configuration reloaded.");
}

bool SaveDeviceToConfiguration(
    const std::wstring& originalName,
    const std::wstring& name,
    std::wstring& host,
    std::wstring& error)
{
    error.clear();
    if (!IsSafeDeviceName(name))
    {
        error = L"Enter a display name up to 80 characters. Control characters and [ ] are not allowed.";
        return false;
    }
    std::wstring endpoint;
    if (!TryNormalizeDeviceEndpoint(host, endpoint))
    {
        error = L"Enter a hostname or IPv4 address, optionally followed by :port (1-65535). Port 5555 is used when omitted.";
        return false;
    }
    host = endpoint;

    DWORD writeError = ERROR_SUCCESS;
    bool saved = g_config->MutateFresh([&](std::wstring& text) {
        std::vector<aip::IniSectionData> document = aip::ParseIniDocument(text);
        for (const auto& section : document)
        {
            if (!aip::IniEquals(section.name, L"Devices"))
            {
                continue;
            }
            for (const auto& entry : section.entries)
            {
                if (aip::IniEquals(entry.key, name.c_str()) &&
                    (originalName.empty() || !aip::IniEquals(entry.key, originalName.c_str())))
                {
                    writeError = ERROR_ALREADY_EXISTS;
                    SetLastError(writeError);
                    return false;
                }
            }
        }

        if (!originalName.empty() && !aip::IniEquals(originalName, name.c_str()))
        {
            bool removed = false;
            if (!aip::RemoveIniValueFromText(text, L"Devices", originalName.c_str(), &removed) || !removed)
            {
                writeError = ERROR_NOT_FOUND;
                SetLastError(writeError);
                return false;
            }
        }
        if (!aip::WriteIniValueToText(text, L"Devices", name.c_str(), host))
        {
            writeError = GetLastError();
            return false;
        }
        return true;
    });
    if (!saved)
    {
        DWORD resultError = writeError == ERROR_SUCCESS ? GetLastError() : writeError;
        if (resultError == ERROR_ALREADY_EXISTS)
        {
            error = L"A device named \"" + name + L"\" already exists.";
        }
        else if (resultError == ERROR_NOT_FOUND)
        {
            error = L"The device was changed outside this window. Reload the list and try again.";
        }
        else
        {
            error = L"Could not save the device:\r\n" + GetLastErrorMessage(resultError);
        }
        return false;
    }
    return true;
}

bool RemoveDeviceFromConfiguration(const std::wstring& name, std::wstring& error)
{
    error.clear();
    size_t configuredCount = static_cast<size_t>(std::count_if(g_devices.begin(), g_devices.end(), [](const Device& device) {
        return device.isConfigured;
    }));
    if (configuredCount <= 1)
    {
        error = L"Keep at least one device in the list. Add another device before removing this one.";
        return false;
    }

    DWORD writeError = ERROR_SUCCESS;
    bool removed = g_config->MutateFresh([&](std::wstring& text) {
        bool didRemove = false;
        if (!aip::RemoveIniValueFromText(text, L"Devices", name.c_str(), &didRemove) || !didRemove)
        {
            writeError = ERROR_NOT_FOUND;
            SetLastError(writeError);
            return false;
        }
        return true;
    });
    if (!removed)
    {
        DWORD resultError = writeError == ERROR_SUCCESS ? GetLastError() : writeError;
        error = resultError == ERROR_NOT_FOUND
            ? L"The device was changed outside this window. Reload the list and try again."
            : L"Could not remove the device:\r\n" + GetLastErrorMessage(resultError);
        return false;
    }
    return true;
}

void LayoutDeviceEditor(HWND window)
{
    RECT client = {};
    GetClientRect(window, &client);
    const int margin = 14;
    const int labelWidth = 138;
    const int controlLeft = margin + labelWidth;
    const int controlWidth = std::max<int>(120, static_cast<int>(client.right) - controlLeft - margin);
    const int buttonWidth = 86;
    const int buttonY = static_cast<int>(client.bottom) - margin - 30;

    MoveWindow(GetDlgItem(window, IdEditorName), controlLeft, 20, controlWidth, 24, TRUE);
    MoveWindow(GetDlgItem(window, IdEditorHost), controlLeft, 60, controlWidth, 24, TRUE);
    MoveWindow(GetDlgItem(window, IdEditorSave), static_cast<int>(client.right) - margin - buttonWidth * 2 - 8, buttonY, buttonWidth, 30, TRUE);
    MoveWindow(GetDlgItem(window, IDCANCEL), static_cast<int>(client.right) - margin - buttonWidth, buttonY, buttonWidth, 30, TRUE);
}

LRESULT CALLBACK DeviceEditorProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto state = reinterpret_cast<DeviceEditorState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message)
    {
    case WM_CREATE:
    {
        auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<DeviceEditorState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HWND nameLabel = CreateWindowExW(0, L"STATIC", L"Display name:", WS_CHILD | WS_VISIBLE, 14, 20, 130, 24, window, nullptr, g_instance, nullptr);
        HWND hostLabel = CreateWindowExW(0, L"STATIC", L"Address / port:", WS_CHILD | WS_VISIBLE, 14, 60, 130, 24, window, nullptr, g_instance, nullptr);
        HWND name = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state ? state->name.c_str() : L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdEditorName)), g_instance, nullptr);
        HWND host = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state ? state->host.c_str() : L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdEditorHost)), g_instance, nullptr);
        HWND save = CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdEditorSave)), g_instance, nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), g_instance, nullptr);
        for (HWND control : { nameLabel, hostLabel, name, host, save, cancel })
        {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            ApplyThemeToControl(control);
        }
        ApplyThemeToWindow(window);
        LayoutDeviceEditor(window);
        SetFocus(name);
        return 0;
    }

    case WM_SIZE:
        LayoutDeviceEditor(window);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IdEditorSave)
        {
            if (state == nullptr)
            {
                return 0;
            }
            state->name = aip::Trim(GetWindowTextValue(GetDlgItem(window, IdEditorName)));
            state->host = aip::Trim(GetWindowTextValue(GetDlgItem(window, IdEditorHost)));
            if (!IsSafeDeviceName(state->name))
            {
                MessageBoxW(window, L"Enter a display name up to 80 characters. Control characters and [ ] are not allowed.", kWindowTitle, MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(window, IdEditorName));
                return 0;
            }
            if (!IsSafeDeviceHost(state->host))
            {
                MessageBoxW(window, L"Enter a hostname or IPv4 address, optionally followed by :port (1-65535). Port 5555 is used when omitted.", kWindowTitle, MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(window, IdEditorHost));
                return 0;
            }
            state->accepted = true;
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_ERASEBKGND:
    {
        RECT client = {};
        GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, g_windowBrush != nullptr ? g_windowBrush : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
        return 1;
    }

    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), PrimaryTextColor());
        SetBkColor(reinterpret_cast<HDC>(wParam), WindowBackgroundColor());
        return reinterpret_cast<LRESULT>(g_windowBrush != nullptr ? g_windowBrush : GetSysColorBrush(COLOR_BTNFACE));

    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wParam), PrimaryTextColor());
        SetBkColor(reinterpret_cast<HDC>(wParam), SurfaceBackgroundColor());
        return reinterpret_cast<LRESULT>(g_surfaceBrush != nullptr ? g_surfaceBrush : GetSysColorBrush(COLOR_WINDOW));

    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool EnsureDeviceEditorClass()
{
    if (g_deviceEditorClassRegistered)
    {
        return true;
    }
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = g_instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kDeviceEditorClass;
    windowClass.lpfnWndProc = DeviceEditorProcedure;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        MessageBoxW(g_window, (L"Could not create the device editor:\r\n" + GetLastErrorMessage(GetLastError())).c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        return false;
    }
    g_deviceEditorClassRegistered = true;
    return true;
}

bool ShowDeviceEditor(const wchar_t* title, DeviceEditorState& state)
{
    if (!EnsureDeviceEditorClass())
    {
        return false;
    }
    HWND editor = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kDeviceEditorClass,
        title,
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        480,
        154,
        g_window,
        nullptr,
        g_instance,
        &state);
    if (editor == nullptr)
    {
        MessageBoxW(g_window, (L"Could not open the device editor:\r\n" + GetLastErrorMessage(GetLastError())).c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        return false;
    }

    RECT parent = {};
    RECT editorRect = {};
    GetWindowRect(g_window, &parent);
    GetWindowRect(editor, &editorRect);
    SetWindowPos(
        editor,
        nullptr,
        parent.left + ((parent.right - parent.left) - (editorRect.right - editorRect.left)) / 2,
        parent.top + ((parent.bottom - parent.top) - (editorRect.bottom - editorRect.top)) / 2,
        0,
        0,
        SWP_NOSIZE | SWP_NOZORDER);

    EnableWindow(g_window, FALSE);
    ShowWindow(editor, SW_SHOW);
    SetForegroundWindow(editor);
    MSG message = {};
    int result = 0;
    while (IsWindow(editor) && (result = GetMessageW(&message, nullptr, 0, 0)) > 0)
    {
        if (!IsDialogMessageW(editor, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(g_window, TRUE);
    SetForegroundWindow(g_window);
    if (result == 0)
    {
        PostQuitMessage(0);
    }
    return state.accepted;
}

void AddDevice()
{
    if (g_busy)
    {
        return;
    }
    DeviceEditorState editor;
    if (!ShowDeviceEditor(L"Add TV", editor))
    {
        return;
    }
    std::wstring error;
    if (!SaveDeviceToConfiguration(L"", editor.name, editor.host, error))
    {
        MessageBoxW(g_window, error.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        return;
    }
    ReloadConfiguration();
    SelectDeviceByName(editor.name);
    AppendOutput(L"Added device: " + editor.name + L" (" + editor.host + L")");
    Log(L"info", L"Added device: " + editor.name + L" (" + editor.host + L").");
}

void EditSelectedDevice()
{
    if (g_busy)
    {
        return;
    }
    Device device;
    if (!GetSelectedDevice(device))
    {
        return;
    }
    if (!device.isConfigured)
    {
        MessageBoxW(g_window,
            L"This device was discovered from the current ADB server and is not a saved configuration entry.\r\n\r\n"
            L"Use Add TV to save a named wireless endpoint. USB devices remain available while ADB can see them.",
            kWindowTitle,
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    DeviceEditorState editor;
    editor.name = device.name;
    editor.host = device.host;
    if (!ShowDeviceEditor(L"Edit TV", editor))
    {
        return;
    }
    std::wstring error;
    if (!SaveDeviceToConfiguration(device.name, editor.name, editor.host, error))
    {
        MessageBoxW(g_window, error.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        return;
    }
    ReloadConfiguration();
    SelectDeviceByName(editor.name);
    AppendOutput(L"Updated device: " + editor.name + L" (" + editor.host + L")");
    Log(L"info", L"Updated device: " + editor.name + L" (" + editor.host + L").");
}

void RemoveSelectedDevice()
{
    if (g_busy)
    {
        return;
    }
    Device device;
    if (!GetSelectedDevice(device))
    {
        return;
    }
    if (!device.isConfigured)
    {
        MessageBoxW(g_window,
            L"This device was discovered from the current ADB server and is not stored in the local configuration.\r\n\r\n"
            L"It will disappear only when ADB no longer sees it or the device list is refreshed.",
            kWindowTitle,
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (MessageBoxW(
            g_window,
            (L"Remove \"" + device.name + L"\" from the device list?").c_str(),
            kWindowTitle,
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }
    std::wstring error;
    if (!RemoveDeviceFromConfiguration(device.name, error))
    {
        MessageBoxW(g_window, error.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        return;
    }
    ReloadConfiguration();
    AppendOutput(L"Removed device: " + device.name);
    Log(L"info", L"Removed device: " + device.name + L".");
}

void BeginRequest(CommandRequest request)
{
    if (g_busy)
    {
        return;
    }
    if (g_worker.joinable())
    {
        g_worker.join();
    }

    SetBusy(true);
    AppendOutput(request.action + L"...");
    Log(L"info", request.action + L" started.");
    HWND destination = g_window;
    g_worker = std::thread([request = std::move(request), destination]() mutable {
        auto* result = new CommandResult(ExecuteRequest(request));
        if (!PostMessageW(destination, kMessageCommandCompleted, 0, reinterpret_cast<LPARAM>(result)))
        {
            delete result;
        }
    });
}

void BeginSelectedCommand(CommandKind kind, const std::wstring& action, std::vector<std::wstring> command)
{
    Device device;
    if (!GetSelectedDevice(device))
    {
        return;
    }

    CommandRequest request;
    request.kind = kind;
    request.action = action + L" — " + device.name;
    request.adbSetting = SelectedAdbPath();
    request.executableDirectory = g_paths.exeDir;
    request.screenshotsDirectory = aip::PathJoin(g_paths.exeDir, L"Screenshots");
    request.device = std::move(device);
    request.arguments = { L"-s", request.device.serial };
    request.arguments.insert(request.arguments.end(), command.begin(), command.end());
    BeginRequest(std::move(request));
}

void BeginConnect()
{
    Device device;
    if (!GetSelectedDevice(device))
    {
        return;
    }
    CommandRequest request;
    request.kind = CommandKind::Connect;
    request.action = L"Connect — " + device.name;
    request.adbSetting = SelectedAdbPath();
    request.executableDirectory = g_paths.exeDir;
    request.device = std::move(device);
    request.arguments = { L"connect", request.device.serial };
    SetDeviceStatus(request.device.serial, L"Connecting...");
    BeginRequest(std::move(request));
}

void BeginDisconnectSelected()
{
    Device device;
    if (!GetSelectedDevice(device))
    {
        return;
    }
    CommandRequest request;
    request.kind = CommandKind::DisconnectSelected;
    request.action = L"Disconnect — " + device.name;
    request.adbSetting = SelectedAdbPath();
    request.executableDirectory = g_paths.exeDir;
    request.device = std::move(device);
    request.arguments = { L"disconnect", request.device.serial };
    BeginRequest(std::move(request));
}

void BeginDisconnectAll()
{
    if (MessageBoxW(
            g_window,
            L"Disconnect every network device known to this ADB server?\r\n\r\nThe ADB server will stay running.",
            kWindowTitle,
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }
    CommandRequest request;
    request.kind = CommandKind::DisconnectAll;
    request.action = L"Disconnect all network devices";
    request.adbSetting = SelectedAdbPath();
    request.executableDirectory = g_paths.exeDir;
    request.arguments = { L"disconnect" };
    BeginRequest(std::move(request));
}

void OpenConfiguration()
{
    if (g_busy)
    {
        return;
    }
    HINSTANCE result = ShellExecuteW(g_window, L"open", g_config->Path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
    {
        DWORD error = GetLastError();
        MessageBoxW(g_window,
            (L"Could not open the configuration file:\r\n" + g_config->Path() + L"\r\n\r\n" + GetLastErrorMessage(error)).c_str(),
            kWindowTitle,
            MB_OK | MB_ICONERROR);
    }
}

void OpenSelectedShell()
{
    Device device;
    if (!GetSelectedDevice(device))
    {
        return;
    }
    std::wstring adbPath;
    std::wstring error;
    if (!ResolveAdbExecutable(SelectedAdbPath(), g_paths.exeDir, adbPath, error))
    {
        MessageBoxW(g_window, error.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring shellCommand = QuoteWindowsArgument(adbPath) + L" -s " + QuoteWindowsArgument(device.serial) + L" shell";
    std::wstring commandLine = L"cmd.exe /d /k " + QuoteWindowsArgument(shellCommand);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, g_paths.exeDir.c_str(), &startup, &process))
    {
        MessageBoxW(g_window,
            (L"Could not open the ADB shell:\r\n" + GetLastErrorMessage(GetLastError())).c_str(),
            kWindowTitle,
            MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Log(L"info", L"Opened an ADB shell for " + device.serial + L".");
}

void HandleCommandCompleted(CommandResult* result)
{
    std::unique_ptr<CommandResult> owned(result);
    SetBusy(false);
    if (g_worker.joinable())
    {
        g_worker.join();
    }

    std::wstring headline = owned->action + (owned->succeeded ? L" succeeded." : L" failed.");
    if (owned->timedOut)
    {
        headline += L" ADB did not finish within 30 seconds.";
    }
    else if (owned->win32Error != ERROR_SUCCESS)
    {
        headline += L" " + GetLastErrorMessage(owned->win32Error);
    }
    else if (owned->launched && owned->exitCode != 0)
    {
        headline += L" adb.exe exited with code " + std::to_wstring(owned->exitCode) + L".";
    }

    if (owned->kind == CommandKind::Connect)
    {
        SetDeviceStatus(owned->deviceSerial, owned->succeeded ? L"Connected" : L"Connection failed");
    }
    else if (owned->kind == CommandKind::DisconnectSelected)
    {
        SetDeviceStatus(owned->deviceSerial, owned->succeeded ? L"Disconnected" : L"Disconnect failed");
    }
    else if (owned->kind == CommandKind::DisconnectAll && owned->succeeded)
    {
        for (size_t i = 0; i < g_devices.size(); ++i)
        {
            g_devices[i].status = L"Disconnected";
            UpdateDeviceRow(i);
        }
    }
    else if (owned->kind == CommandKind::RefreshState && owned->succeeded)
    {
        std::wstring state = aip::Trim(owned->output);
        SetDeviceStatus(owned->deviceSerial, state.empty() ? L"Connected" : state);
    }

    if (!owned->screenshotPath.empty())
    {
        headline += L" Saved to: " + owned->screenshotPath;
    }
    AppendOutput(headline);
    if (!owned->output.empty())
    {
        AppendOutput(owned->output);
    }
    Log(owned->succeeded ? L"info" : L"error", headline + (owned->output.empty() ? L"" : L"\r\n" + owned->output));
    SetWindowTextSafe(g_statusText, owned->succeeded ? L"Ready" : L"Last command failed — see output below.");
}

HWND MakeButton(HWND parent, int id, const wchar_t* text)
{
    HWND button = CreateWindowExW(
        0,
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_instance,
        nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_buttonFont != nullptr ? g_buttonFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    ApplyThemeToControl(button);
    SetWindowSubclass(button, ActionButtonSubclassProcedure, 1, 0);
    g_actionControls.push_back(button);
    RegisterActionTooltip(button);
    return button;
}

HWND MakeHeaderButton(HWND parent, int id, const wchar_t* text)
{
    HWND button = CreateWindowExW(
        0,
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_instance,
        nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_sectionFont != nullptr ? g_sectionFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    ApplyThemeToControl(button);
    SetWindowSubclass(button, ActionButtonSubclassProcedure, 1, 0);
    return button;
}

HWND MakeSectionHeading(HWND parent, const wchar_t* text)
{
    HWND heading = CreateWindowExW(
        0,
        L"STATIC",
        text,
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
        0,
        0,
        0,
        0,
        parent,
        nullptr,
        g_instance,
        nullptr);
    SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(g_sectionFont != nullptr ? g_sectionFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    ApplyThemeToControl(heading);
    return heading;
}

void LayoutWindow(HWND window)
{
    RECT client = {};
    GetClientRect(window, &client);
    const bool compact = UseCompactLayout(window);
    const int margin = ScaleForWindow(window, compact ? 10 : 14);
    const int gap = ScaleForWindow(window, compact ? 4 : 6);
    const int cardGap = ScaleForWindow(window, compact ? 4 : 6);
    const int cardPadding = ScaleForWindow(window, compact ? 3 : 4);
    ContentArea content = GetContentArea(window, client, margin);
    const int cardInset = ScaleForWindow(window, compact ? 6 : 10);
    const int innerLeft = content.left + cardInset;
    const int innerRight = content.right - cardInset;
    const int innerWidth = std::max<int>(1, innerRight - innerLeft);
    const int buttonWidth = (innerWidth - gap * 2) / 3;
    const int buttonHeight = ScaleForWindow(window, compact ? 26 : 30);
    const int headingHeight = ScaleForWindow(window, compact ? 16 : 18);
    const int headerHeight = ScaleForWindow(window, compact ? 52 : 64);
    int y = headerHeight + margin;
    g_cardBounds.fill(RECT{});
    if (g_moreButton != nullptr)
    {
        const int moreSize = ScaleForWindow(window, compact ? 30 : 34);
        MoveWindow(g_moreButton, content.right - moreSize, ScaleForWindow(window, compact ? 12 : 18), moreSize, moreSize, TRUE);
    }

    auto placeHeading = [&](HWND heading) {
        MoveWindow(heading, innerLeft, y, innerWidth, headingHeight, TRUE);
        y += headingHeight + ScaleForWindow(window, compact ? 2 : 4);
    };
    auto placeRow = [&](const std::array<int, 3>& controls) {
        int x = innerLeft;
        for (int column = 0; column < 3; ++column)
        {
            int controlWidth = column == 2 ? innerRight - x : buttonWidth;
            if (controls[static_cast<size_t>(column)] != 0)
            {
                MoveWindow(
                    GetDlgItem(window, controls[static_cast<size_t>(column)]),
                    x,
                    y,
                    controlWidth,
                    buttonHeight,
                    TRUE);
            }
            x += controlWidth + gap;
        }
        y += buttonHeight + gap;
    };
    auto beginCard = [&]() {
        int top = y;
        y += cardPadding;
        return top;
    };
    auto finishCard = [&](size_t index, int top) {
        y += cardPadding;
        g_cardBounds[index] = { content.left, top, content.right, y };
        y += cardGap;
    };

    int cardTop = beginCard();
    placeHeading(g_devicesHeading);
    const int listHeight = ScaleForWindow(window, compact ? 104 : 138);
    MoveWindow(g_deviceList, innerLeft, y, innerWidth, listHeight, TRUE);
    ListView_SetColumnWidth(g_deviceList, 0, innerWidth * 40 / 100);
    ListView_SetColumnWidth(g_deviceList, 1, innerWidth * 28 / 100);
    ListView_SetColumnWidth(g_deviceList, 2, std::max<int>(ScaleForWindow(window, 100), innerWidth - innerWidth * 68 / 100 - GetSystemMetrics(SM_CXVSCROLL)));
    y += listHeight + ScaleForWindow(window, compact ? 4 : 6);
    const int statusHeight = ScaleForWindow(window, compact ? 16 : 20);
    MoveWindow(g_statusText, innerLeft, y, innerWidth, statusHeight, TRUE);
    y += statusHeight;
    finishCard(0, cardTop);

    cardTop = beginCard();
    placeHeading(g_connectionHeading);
    placeRow({ IdConnect, IdRefreshState, IdDeviceList });
    placeRow({ IdAddDevice, IdEditDevice, IdRemoveDevice });
    finishCard(1, cardTop);

    cardTop = beginCard();
    placeHeading(g_remoteHeading);
    placeRow({ IdPower, IdHome, IdBack });
    placeRow({ IdVolumeDown, IdMute, IdVolumeUp });
    finishCard(2, cardTop);

    cardTop = beginCard();
    placeHeading(g_actionsHeading);
    placeRow({ IdScreenshot, IdReboot, IdOpenShell });
    placeRow({ IdDisconnectSelected, IdDisconnectAll, IdClearOutput });
    finishCard(3, cardTop);

    cardTop = beginCard();
    placeHeading(g_activityHeading);
    int outputHeight = std::max<int>(ScaleForWindow(window, compact ? 36 : 72), static_cast<int>(client.bottom) - y - margin - cardPadding);
    MoveWindow(g_outputText, innerLeft, y, innerWidth, outputHeight, TRUE);
    ReflowActivityOutput();
    RedrawWindow(g_outputText, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    y += outputHeight;
    finishCard(4, cardTop);
    // A resize can move every child control to a different column. Force the
    // complete surface to repaint so old card/header pixels cannot remain.
    RedrawWindow(window, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void ShowMoreMenu(HWND anchor)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"Appearance");
    AppendMenuW(menu, MF_STRING, IdThemeAuto, L"Follow Windows");
    AppendMenuW(menu, MF_STRING, IdThemeLight, L"Light");
    AppendMenuW(menu, MF_STRING, IdThemeDark, L"Dark");
    UINT selected = g_themeMode == ThemeMode::Light ? IdThemeLight :
        (g_themeMode == ThemeMode::Dark ? IdThemeDark : IdThemeAuto);
    CheckMenuRadioItem(menu, IdThemeAuto, IdThemeDark, selected, MF_BYCOMMAND);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdEditConfiguration, L"Open configuration");
    AppendMenuW(menu, MF_STRING, IdReloadConfiguration, L"Reload configuration");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdAbout, L"About");
    AppendMenuW(menu, MF_STRING, IdExit, L"Exit");

    RECT bounds = {};
    GetWindowRect(anchor, &bounds);
    SetForegroundWindow(g_window);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, bounds.right, bounds.bottom, 0, g_window, nullptr);
    DestroyMenu(menu);
    if (command != 0)
    {
        SendMessageW(g_window, WM_COMMAND, MAKEWPARAM(command, 0), 0);
    }
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        if (g_sectionFont == nullptr)
        {
            const int sectionHeight = ScaleForWindow(window, 14);
            const int titleHeight = ScaleForWindow(window, 22);
            const int subtitleHeight = ScaleForWindow(window, 12);
            g_sectionFont = CreateFontW(-sectionHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable");
            g_titleFont = CreateFontW(-titleHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Display");
            g_subtitleFont = CreateFontW(-subtitleHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
            g_buttonFont = CreateFontW(-sectionHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
            g_iconFont = CreateFontW(-ScaleForWindow(window, 15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
        }
        CreateActionTooltips(window);
        g_moreButton = MakeHeaderButton(window, IdMoreMenu, L"•••");
        g_devicesHeading = MakeSectionHeading(window, L"Devices");
        g_connectionHeading = MakeSectionHeading(window, L"Quick actions");
        g_remoteHeading = MakeSectionHeading(window, L"Remote controls");
        g_actionsHeading = MakeSectionHeading(window, L"Tools and session");
        g_activityHeading = MakeSectionHeading(window, L"Activity");
        g_deviceList = CreateWindowExW(
            0,
            WC_LISTVIEWW,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0,
            0,
            0,
            0,
            window,
            nullptr,
            g_instance,
            nullptr);
        ListView_SetExtendedListViewStyle(g_deviceList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_BORDERSELECT);
        SendMessageW(g_deviceList, WM_SETFONT, reinterpret_cast<WPARAM>(g_buttonFont != nullptr ? g_buttonFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        const std::array<std::pair<const wchar_t*, int>, 3> columns =
        {
            std::make_pair(L"TV", 260),
            std::make_pair(L"IP address", 180),
            std::make_pair(L"Status", 180)
        };
        for (int i = 0; i < static_cast<int>(columns.size()); ++i)
        {
            LVCOLUMNW column = {};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            column.pszText = const_cast<wchar_t*>(columns[static_cast<size_t>(i)].first);
            column.cx = columns[static_cast<size_t>(i)].second;
            column.iSubItem = i;
            ListView_InsertColumn(g_deviceList, i, &column);
        }

        g_statusText = CreateWindowExW(0, L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, window, nullptr, g_instance, nullptr);
        SendMessageW(g_statusText, WM_SETFONT, reinterpret_cast<WPARAM>(g_subtitleFont != nullptr ? g_subtitleFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        g_outputText = CreateWindowExW(
            0,
            L"LISTBOX",
            L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOSEL,
            0,
            0,
            0,
            0,
            window,
            nullptr,
            g_instance,
            nullptr);
        SendMessageW(g_outputText, WM_SETFONT, reinterpret_cast<WPARAM>(g_buttonFont != nullptr ? g_buttonFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);

        MakeButton(window, IdConnect, L"Connect selected");
        MakeButton(window, IdRefreshState, L"Refresh selected");
        MakeButton(window, IdDeviceList, L"ADB device list");
        MakeButton(window, IdAddDevice, L"Add TV");
        MakeButton(window, IdEditDevice, L"Edit selected TV");
        MakeButton(window, IdRemoveDevice, L"Remove selected TV");
        MakeButton(window, IdPower, L"Power");
        MakeButton(window, IdHome, L"Home");
        MakeButton(window, IdBack, L"Back");
        MakeButton(window, IdVolumeDown, L"Volume -");
        MakeButton(window, IdMute, L"Mute");
        MakeButton(window, IdVolumeUp, L"Volume +");
        MakeButton(window, IdScreenshot, L"Take screenshot");
        MakeButton(window, IdReboot, L"Reboot TV");
        MakeButton(window, IdOpenShell, L"Open ADB shell");
        MakeButton(window, IdDisconnectSelected, L"Disconnect selected");
        MakeButton(window, IdDisconnectAll, L"Disconnect all");
        MakeButton(window, IdClearOutput, L"Clear activity output");
        PopulateDeviceList();
        ApplyCurrentTheme();
        AppendOutput(L"Ready. Select a TV, then connect. The ADB server is kept running when switching TVs.");
        return 0;
    }

    case WM_SIZE:
        LayoutWindow(window);
        return 0;

    case WM_DRAWITEM:
    {
        auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw != nullptr && draw->CtlType == ODT_BUTTON && IsModernButton(draw->hwndItem))
        {
            PaintActionButton(*draw);
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY:
    {
        auto notification = reinterpret_cast<NMHDR*>(lParam);
        if (notification != nullptr && notification->hwndFrom == g_tooltipWindow &&
            notification->code == TTN_GETDISPINFOW)
        {
            auto tooltip = reinterpret_cast<NMTTDISPINFOW*>(lParam);
            HWND control = reinterpret_cast<HWND>(tooltip->hdr.idFrom);
            auto& text = g_actionTooltipTexts[control];
            text = TooltipForAction(GetDlgCtrlID(control));
            tooltip->lpszText = const_cast<wchar_t*>(text.c_str());
            return 0;
        }
        if (notification != nullptr && notification->hwndFrom == g_deviceList &&
            notification->code == LVN_ITEMCHANGED)
        {
            int selected = SelectedDeviceIndex();
            if (selected >= 0)
            {
                SetWindowTextSafe(g_statusText, L"Selected: " + g_devices[static_cast<size_t>(selected)].name + L" (" + g_devices[static_cast<size_t>(selected)].serial + L")");
            }
        }
        break;
    }

    case WM_COMMAND:
        if (HIWORD(wParam) != 0 && HIWORD(wParam) != BN_CLICKED)
        {
            break;
        }
        switch (LOWORD(wParam))
        {
        case IdConnect: BeginConnect(); return 0;
        case IdRefreshState: BeginSelectedCommand(CommandKind::RefreshState, L"Refresh connection state", { L"get-state" }); return 0;
        case IdDeviceList:
        {
            CommandRequest request;
            request.kind = CommandKind::DeviceList;
            request.action = L"ADB device list";
            request.adbSetting = SelectedAdbPath();
            request.executableDirectory = g_paths.exeDir;
            request.arguments = { L"devices", L"-l" };
            BeginRequest(std::move(request));
            return 0;
        }
        case IdPower: BeginSelectedCommand(CommandKind::KeyEvent, L"Power", { L"shell", L"input", L"keyevent", L"26" }); return 0;
        case IdHome: BeginSelectedCommand(CommandKind::KeyEvent, L"Home", { L"shell", L"input", L"keyevent", L"3" }); return 0;
        case IdBack: BeginSelectedCommand(CommandKind::KeyEvent, L"Back", { L"shell", L"input", L"keyevent", L"4" }); return 0;
        case IdVolumeDown: BeginSelectedCommand(CommandKind::KeyEvent, L"Volume down", { L"shell", L"input", L"keyevent", L"25" }); return 0;
        case IdMute: BeginSelectedCommand(CommandKind::KeyEvent, L"Mute", { L"shell", L"input", L"keyevent", L"164" }); return 0;
        case IdVolumeUp: BeginSelectedCommand(CommandKind::KeyEvent, L"Volume up", { L"shell", L"input", L"keyevent", L"24" }); return 0;
        case IdScreenshot: BeginSelectedCommand(CommandKind::Screenshot, L"Screenshot", { L"exec-out", L"screencap", L"-p" }); return 0;
        case IdReboot:
            if (MessageBoxW(g_window, L"Reboot the selected TV now?", kWindowTitle, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
            {
                BeginSelectedCommand(CommandKind::Reboot, L"Reboot", { L"reboot" });
            }
            return 0;
        case IdOpenShell: OpenSelectedShell(); return 0;
        case IdDisconnectSelected: BeginDisconnectSelected(); return 0;
        case IdDisconnectAll: BeginDisconnectAll(); return 0;
        case IdAddDevice: AddDevice(); return 0;
        case IdEditDevice: EditSelectedDevice(); return 0;
        case IdRemoveDevice: RemoveSelectedDevice(); return 0;
        case IdEditConfiguration: OpenConfiguration(); return 0;
        case IdReloadConfiguration: ReloadConfiguration(); return 0;
        case IdClearOutput: ClearOutput(); return 0;
        case IdMoreMenu: ShowMoreMenu(g_moreButton); return 0;
        case IdThemeAuto: SetThemeMode(ThemeMode::Auto); return 0;
        case IdThemeLight: SetThemeMode(ThemeMode::Light); return 0;
        case IdThemeDark: SetThemeMode(ThemeMode::Dark); return 0;
        case IdExit: SendMessageW(window, WM_CLOSE, 0, 0); return 0;
        case IdAbout:
            MessageBoxW(window,
                L"ADB TV Controller\r\n\r\nA direct GUI for ADB-over-TCP TV control.\r\n\r\nIt reuses the ADB server when switching TVs and scopes every command to the selected IP:5555 device.",
                kWindowTitle,
                MB_OK | MB_ICONINFORMATION);
            return 0;
        default:
            break;
        }
        break;

    case kMessageCommandCompleted:
        HandleCommandCompleted(reinterpret_cast<CommandResult*>(lParam));
        return 0;

    case kMessageActivateExisting:
        ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(window);
        return aip::INSTANCE_REQUEST_HANDLED;

    case WM_SETTINGCHANGE:
        if (g_themeMode == ThemeMode::Auto)
        {
            ApplyCurrentTheme();
        }
        break;

    case WM_ERASEBKGND:
    {
        RECT client = {};
        GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, g_windowBrush != nullptr ? g_windowBrush : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(window, &paint);
        PaintApplicationChrome(window, dc);
        EndPaint(window, &paint);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), PrimaryTextColor());
        SetBkMode(reinterpret_cast<HDC>(wParam), OPAQUE);
        SetBkColor(reinterpret_cast<HDC>(wParam), SurfaceBackgroundColor());
        return reinterpret_cast<LRESULT>(g_surfaceBrush != nullptr ? g_surfaceBrush : GetSysColorBrush(COLOR_WINDOW));

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wParam), PrimaryTextColor());
        SetBkColor(reinterpret_cast<HDC>(wParam), ControlBackgroundColor());
        return reinterpret_cast<LRESULT>(g_controlBrush != nullptr ? g_controlBrush : GetSysColorBrush(COLOR_WINDOW));

    case WM_GETMINMAXINFO:
    {
        auto info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = ScaleForWindow(window, 600);
        info->ptMinTrackSize.y = ScaleForWindow(window, 620);
        return 0;
    }

    case WM_CLOSE:
        if (g_busy)
        {
            MessageBoxW(window, L"An ADB command is still running. Wait for it to finish before closing the program.", kWindowTitle, MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        if (g_worker.joinable())
        {
            g_worker.join();
        }
        if (g_sectionFont != nullptr)
        {
            DeleteObject(g_sectionFont);
            g_sectionFont = nullptr;
        }
        if (g_titleFont != nullptr)
        {
            DeleteObject(g_titleFont);
            g_titleFont = nullptr;
        }
        if (g_subtitleFont != nullptr)
        {
            DeleteObject(g_subtitleFont);
            g_subtitleFont = nullptr;
        }
        if (g_buttonFont != nullptr)
        {
            DeleteObject(g_buttonFont);
            g_buttonFont = nullptr;
        }
        if (g_iconFont != nullptr)
        {
            DeleteObject(g_iconFont);
            g_iconFont = nullptr;
        }
        if (g_windowBrush != nullptr)
        {
            DeleteObject(g_windowBrush);
            g_windowBrush = nullptr;
        }
        if (g_surfaceBrush != nullptr)
        {
            DeleteObject(g_surfaceBrush);
            g_surfaceBrush = nullptr;
        }
        if (g_controlBrush != nullptr)
        {
            DeleteObject(g_controlBrush);
            g_controlBrush = nullptr;
        }
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool InitializeApplication()
{
    g_paths = aip::BuildCurrentProcessSidecarPaths(L"ADBController");
    aip::Utf8LoggerOptions logging;
    logging.filePath = g_paths.defaultLogPath;
    logging.lockWaitMs = 5000;
    g_logger.Configure(logging);

    aip::InstanceIdentity identity = aip::BuildPathScopedInstanceIdentity(
        L"ADBController",
        L"ADBController.Message",
        kWindowClass,
        kWindowTitle,
        g_paths.exePath);
    g_instanceMutex = CreateMutexW(nullptr, FALSE, identity.mutexName.c_str());
    if (g_instanceMutex == nullptr)
    {
        MessageBoxW(nullptr, (L"Could not create the instance lock:\r\n" + GetLastErrorMessage(GetLastError())).c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        aip::SendInstanceWindowRequest(kWindowClass, kWindowTitle, kMessageActivateExisting, 0, 20, 100, 1000, false);
        g_existingInstanceActivated = true;
        CloseHandle(g_instanceMutex);
        g_instanceMutex = nullptr;
        return false;
    }

    g_config = std::make_unique<aip::IniConfigStore>(
        g_paths.configPath,
        L"; ADB TV Controller settings. This file is UTF-8 with BOM.\r\n"
        L"; Add TVs under [Devices] as \"Display name\" = \"IPv4 address or hostname\".\r\n",
        5000);
    std::wstring error;
    if (!EnsureInitialConfiguration(error))
    {
        Log(L"error", error);
        MessageBoxW(nullptr, error.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        return false;
    }
    if (!LoadDevicesFromConfiguration(error))
    {
        Log(L"error", error);
        MessageBoxW(nullptr, error.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        return false;
    }
    g_themeMode = ParseThemeMode(g_config->ReadRaw(L"Settings", L"Theme", L"Auto"));
    ApplyCurrentTheme();
    return true;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    g_instance = instance;
    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&commonControls);

    if (!InitializeApplication())
    {
        if (g_instanceMutex != nullptr)
        {
            CloseHandle(g_instanceMutex);
            g_instanceMutex = nullptr;
        }
        return g_existingInstanceActivated ? 0 : 1;
    }

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    windowClass.lpfnWndProc = WindowProcedure;
    if (!RegisterClassExW(&windowClass))
    {
        MessageBoxW(nullptr, (L"Could not register the main window:\r\n" + GetLastErrorMessage(GetLastError())).c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        CloseHandle(g_instanceMutex);
        return 1;
    }

    g_window = CreateWindowExW(
        WS_EX_APPWINDOW,
        kWindowClass,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        820,
        860,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (g_window == nullptr)
    {
        MessageBoxW(nullptr, (L"Could not create the main window:\r\n" + GetLastErrorMessage(GetLastError())).c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        CloseHandle(g_instanceMutex);
        return 1;
    }

    ShowWindow(g_window, showCommand == 0 ? SW_SHOWDEFAULT : showCommand);
    UpdateWindow(g_window);
    Log(L"info", L"ADB TV Controller started.");

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_instanceMutex != nullptr)
    {
        CloseHandle(g_instanceMutex);
        g_instanceMutex = nullptr;
    }
    return static_cast<int>(message.wParam);
}
