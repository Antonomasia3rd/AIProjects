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
    ID_LIVE_TILE_MODE_AUTO,
    ID_LIVE_TILE_MODE_REGISTRATION,
    ID_LIVE_TILE_MODE_LIVE_TILE,
    ID_LIVE_TILE_RELAUNCH_ON_SWITCH,
    ID_CLEAR_LIVE_TILE_ON_SHUTDOWN,
    ID_WIN8_LIVE_TILE_OOP_HELPER,
    ID_WIN8_LIVE_TILE_BACKGROUND_TASK,
    ID_WIN8_LIVE_TILE_BROKER_APP,
    ID_MANIFEST_TARGET_WINDOWS10,
    ID_MANIFEST_TARGET_WINDOWS81,
    ID_MANIFEST_TARGET_WINDOWS8,
    ID_LOGFILE,
    ID_LOG_RENAME,
    ID_LOG_OPEN,
    ID_LOG_RESET,
    ID_GENERATE_NOW,
    ID_CANCEL_SHUTDOWN,
    ID_FORCE_SHUTDOWN,
    ID_GENERATE_STARTUP,
    ID_GENERATE_STARTUP_CHANGED_ONLY,
    ID_HIDE_DISABLED,
    ID_GENERATE_DESKTOP_ICON_DISABLED,
    ID_DELETE_DISABLED_ASSETS,
    ID_SHOW_MENU_DROPDOWN,
    ID_NOTIF_ENABLED,
    ID_NOTIFY_START,
    ID_NOTIFY_SUCCESS,
    ID_NOTIFY_FAILURE,
    ID_NOTIFY_BUSY,
    ID_NOTIFY_ALREADY_RUNNING,
    ID_NOTIFY_SINGLE_INSTANCE_FAILURE,
    ID_SHOW_PSLOG,
    ID_LISTEN_WP,
    ID_LISTEN_FIT,
    ID_DISABLE_FIT,
    ID_WP_METHOD_TRANSCODED_CACHE,
    ID_WP_METHOD_SYSTEM_PARAMETERS,
    ID_WP_METHOD_DESKTOP_WALLPAPER_COM,
    ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE,
    ID_WP_METHOD_AUTO,
    ID_WP_METHOD_FALLBACK,
    ID_WALLPAPER_DECODE_LOW_MEMORY,
    ID_WALLPAPER_DECODE_MAX_LONG_EDGE,
    ID_LIVE_WALLPAPER_CAPTURE,
    ID_LIVE_WALLPAPER_CAPTURE_SCREEN_FALLBACK,
    ID_LIVE_WALLPAPER_CAPTURE_DELAY,
    ID_LIVE_WALLPAPER_CAPTURE_REFRESH,
    ID_LIVE_WALLPAPER_CAPTURE_STARTUP_REFRESH,
    ID_LIVE_WALLPAPER_CAPTURE_STARTUP_REFRESH_DURATION,
    ID_SCALE_AUTO,
    ID_SCALE_100,
    ID_SCALE_125,
    ID_SCALE_150,
    ID_SCALE_200,
    ID_SCALE_400,
    ID_GENERATED_ASSET_CACHE,
    ID_GENERATED_ASSET_PRECACHE,
    ID_GENERATED_ASSET_CACHE_MAX_ENTRIES,
    ID_GENERATED_ASSET_PRECACHE_MAX_FILES,
    ID_MANIFEST_REGENERATE,
    ID_TILE_TEXT_ENABLED,
    ID_TILE_TEXT_EDIT,
    ID_TILE_TEXT_CLEAR,
    ID_TILE_TEXT_SECONDARY_EDIT,
    ID_TILE_TEXT_SECONDARY_CLEAR,
    ID_TILE_TEXT_BADGE_EDIT,
    ID_TILE_TEXT_BADGE_CLEAR,
    ID_TILE_TEXT_H_ALIGN,
    ID_TILE_TEXT_V_ALIGN,
    ID_TILE_TEXT_BADGE_H_ALIGN,
    ID_TILE_TEXT_BADGE_V_ALIGN,
    ID_TILE_TEXT_FONT,
    ID_TILE_TEXT_FONT_SIZE,
    ID_TILE_TEXT_SECONDARY_FONT_SIZE,
    ID_TILE_TEXT_BADGE_FONT_SIZE,
    ID_TILE_TEXT_BOLD,
    ID_TILE_TEXT_SECONDARY_BOLD,
    ID_TILE_TEXT_BADGE_BOLD,
    ID_TILE_TEXT_ITALIC,
    ID_TILE_TEXT_COLOR,
    ID_TILE_TEXT_SECONDARY_COLOR,
    ID_TILE_TEXT_BADGE_COLOR,
    ID_TILE_TEXT_SHADOW,
    ID_TILE_TEXT_SHADOW_COLOR,
    ID_TILE_TEXT_MARGIN_X,
    ID_TILE_TEXT_MARGIN_Y,
    ID_TILE_TEXT_LINE_GAP,
    ID_TILE_TEXT_MAX_SECONDARY_LINES,
    ID_TILE_TEXT_APPLY_MEDIUM,
    ID_TILE_TEXT_APPLY_WIDE,
    ID_TILE_TEXT_APPLY_LARGE,
    ID_TILE_TEXT_APPLY_SMALL,
    ID_TILE_TEXT_APPLY_LOGOS,

    // Advanced configurable settings
    ID_ADV_POLL_INTERVAL,
    ID_ADV_CONFIRM_MS,
    ID_ADV_DEBOUNCE_MIN_MS,
    ID_ADV_POLL_INITIAL_BYPASS_MS,
    ID_ADV_INI_CACHE_REFRESH_MS,
    ID_ADV_WALLPAPER_DETECTION_LOG_REPEAT_MS,
    ID_ADV_NOTIFICATION_TIMEOUT_MS,
    ID_ADV_SHUTDOWN_INITIAL_MS,
    ID_ADV_SHUTDOWN_REPEAT_MS,
    ID_ADV_SINGLE_INSTANCE_SIGNAL_RETRIES,
    ID_ADV_SINGLE_INSTANCE_SIGNAL_DELAY_MS,
    ID_ADV_COM_REGISTRATION_TIMEOUT_MS,
    ID_ADV_COM_REGISTRATION_CANCEL_WAIT_MS,
    ID_COM_REGISTRATION_USE_HELPER,
    ID_ADV_TRAY_ICON_HIDDEN_DELAY_MS,
    ID_ADV_LOG_BUFFER_LINES,
    ID_ADV_LOG_APPEND_LOCK_WAIT_MS,
    ID_ADV_TRIM_WORKING_SET_ON_IDLE,
    ID_ADV_COMPACT_CRT_HEAP_ON_IDLE,
    ID_ADV_KEEP_GDIPLUS_LOADED,
    ID_ADV_POWER_SHELL_EXE,
    ID_ADV_POWER_SHELL_POLL_MS,
    ID_ADV_POWER_SHELL_TIMEOUT_MS,
    ID_ADV_POWER_SHELL_TERMINATE_WAIT_MS,
    ID_ADV_POWER_SHELL_OUTPUT_LIMIT,
    ID_ADV_REGISTRATION_UNRESOLVED_BLOCK_MS,
    ID_ADV_SAVE_PNG_REPLACE_RETRIES,
    ID_ADV_SAVE_PNG_REPLACE_RETRY_DELAY_MS,
    ID_ADV_SINGLE_INSTANCE_FAILURE_ACTION,

    // Assets
    ID_A1=2001, ID_A2, ID_A3, ID_A4, ID_A5, ID_A6, ID_A7, ID_A8
};

static int NextPresetValue(int current, const int* values, int count)
{
    if (!values || count <= 0)
        return current;

    for (int i = 0; i < count; ++i)
    {
        if (current < values[i])
            return values[i];
    }
    return values[0];
}

static int NextLiveWallpaperCaptureDelayMs(int current)
{
    const int presets[] = { 0, 500, 1000, 2500, 5000, 10000 };
    return NextPresetValue(current, presets, static_cast<int>(_countof(presets)));
}

static int NextLiveWallpaperCaptureRefreshMs(int current)
{
    const int presets[] = { 0, 1000, 2000, 5000, 10000, 30000, 60000 };
    return NextPresetValue(current, presets, static_cast<int>(_countof(presets)));
}

static int NextLiveWallpaperCaptureStartupRefreshMs(int current)
{
    const int presets[] = { 0, 500, 1000, 2000, 5000, 10000 };
    return NextPresetValue(current, presets, static_cast<int>(_countof(presets)));
}

static int NextLiveWallpaperCaptureStartupRefreshDurationMs(int current)
{
    const int presets[] = { 0, 5000, 10000, 15000, 30000, 60000 };
    return NextPresetValue(current, presets, static_cast<int>(_countof(presets)));
}

static std::wstring LiveWallpaperMsDisplayText(int valueMs, const std::wstring& zeroText)
{
    if (valueMs <= 0)
        return zeroText;
    return FormatWide(g_ui.liveWallpaperCaptureMsText.c_str(), valueMs);
}

static std::wstring LiveWallpaperRefreshDisplayText(int refreshMs)
{
    return LiveWallpaperMsDisplayText(refreshMs, g_ui.liveWallpaperCaptureRefreshOnce);
}

static std::wstring LiveWallpaperOffDisplayText(int valueMs)
{
    return LiveWallpaperMsDisplayText(valueMs, g_ui.liveWallpaperCaptureOff);
}

static std::wstring WallpaperDecodeMaxLongEdgeDisplayText(int valuePx)
{
    if (valuePx <= 0)
        return g_ui.wallpaperDecodeAuto;
    return FormatWide(g_ui.wallpaperDecodePxText.c_str(), valuePx);
}

static int ReadSettingClamped(const wchar_t* key, int fallback, int minValue, int maxValue)
{
    return IniReadClampedI(L"Settings", key, fallback, minValue, maxValue);
}

static int NextSettingPreset(int current, std::initializer_list<int> values)
{
    if (values.size() == 0)
        return current;

    std::vector<int> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    return NextPresetValue(current, sorted.data(), static_cast<int>(sorted.size()));
}

static void WriteSettingInt(const wchar_t* key, int value)
{
    IniWrite(L"Settings", key, std::to_wstring(value).c_str());
    ReloadRuntimeSettingsFromIni();
    Log(L"[i] Setting changed: [%s] %s = %d", L"Settings", key ? key : L"", value);
}

static void WriteSettingBool(const wchar_t* key, bool value)
{
    IniWrite(L"Settings", key, value ? L"1" : L"0");
    ReloadRuntimeSettingsFromIni();
    Log(L"[i] Setting changed: [%s] %s = %s", L"Settings", key ? key : L"", StateEnabled(value));
}

static void ToggleSettingBool(const wchar_t* key, int fallback)
{
    bool value = IniReadI(L"Settings", key, fallback) == 0;
    WriteSettingBool(key, value);
}

static void CycleSettingIntPreset(
    const wchar_t* key,
    int fallback,
    int minValue,
    int maxValue,
    std::initializer_list<int> presets)
{
    int current = ReadSettingClamped(key, fallback, minValue, maxValue);
    int next = NextSettingPreset(current, presets);
    next = ClampInt(next, minValue, maxValue);
    WriteSettingInt(key, next);
}

static const wchar_t* NextTileTextHorizontalAlign(const std::wstring& current)
{
    std::wstring value = ToLowerCopy(TrimCopy(current));
    if (value == L"left")
        return L"Center";
    if (value == L"center")
        return L"Right";
    return L"Left";
}

static const wchar_t* NextTileTextVerticalAlign(const std::wstring& current)
{
    std::wstring value = ToLowerCopy(TrimCopy(current));
    if (value == L"top")
        return L"Center";
    if (value == L"center" || value == L"middle")
        return L"Bottom";
    return L"Top";
}

static void WriteTileTextBool(const wchar_t* key, bool value)
{
    IniWrite(L"TileText", key, value ? L"1" : L"0");
    Log(L"[i] Setting changed: [%s] %s = %s", L"TileText", key ? key : L"", StateEnabled(value));
}

static void ToggleTileTextBool(const wchar_t* key, int fallback)
{
    bool value = IniReadI(L"TileText", key, fallback) == 0;
    WriteTileTextBool(key, value);
}

static void CycleTileTextFontSize(const wchar_t* key, int fallback, const std::wstring& logFormat)
{
    int current = IniReadClampedI(L"TileText", key, fallback, 6, 256);
    int next = NextSettingPreset(current, { 8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 64 });
    next = ClampInt(next, 6, 256);
    IniWrite(L"TileText", key, std::to_wstring(next).c_str());
    Log(logFormat.c_str(), next);
}

static void PromptTileTextStringSetting(
    HWND owner,
    const wchar_t* key,
    const std::wstring& title,
    const std::wstring& prompt,
    const wchar_t* fallback = L"")
{
    std::wstring value = IniReadS(L"TileText", key, fallback ? fallback : L"");
    if (ShowTextInputDialog(owner, title, prompt, value, true) == IDOK)
    {
        IniWrite(L"TileText", key, value.c_str());
        Log(g_ui.tileTextSettingChanged.c_str(), key ? key : L"", value.c_str());
    }
}

static void PromptTileTextIntSetting(
    HWND owner,
    const wchar_t* key,
    int fallback,
    int minValue,
    int maxValue,
    const std::wstring& title,
    const std::wstring& prompt,
    const std::wstring& logFormat)
{
    int current = IniReadClampedI(L"TileText", key, fallback, minValue, maxValue);
    std::wstring text = std::to_wstring(current);
    if (ShowTextInputDialog(owner, title, prompt, text, false) != IDOK)
        return;

    int parsed = 0;
    if (!TryParseIntStrict(text, parsed) || parsed < minValue || parsed > maxValue)
    {
        std::wstring message = FormatWide(g_ui.tileTextIntInputInvalid.c_str(), minValue, maxValue);
        MessageBoxW(owner, message.c_str(), g_ui.errorTitle.c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    IniWrite(L"TileText", key, std::to_wstring(parsed).c_str());
    Log(logFormat.c_str(), parsed);
}

static const wchar_t* NextSingleInstanceFailureActionName(ErrorAction current)
{
    switch (current)
    {
    case ErrorAction::Ignore: return L"Warn";
    case ErrorAction::Warn: return L"Exit";
    case ErrorAction::Exit: return L"Crash";
    case ErrorAction::Crash:
    default: return L"Ignore";
    }
}

static void CycleSingleInstanceFailureActionSetting()
{
    const wchar_t* next = NextSingleInstanceFailureActionName(CurrentSingleInstanceFailureAction());
    IniWrite(L"Settings", L"SingleInstanceFailureAction", next);
    Log(L"[i] Setting changed: [%s] %s = %s", L"Settings", L"SingleInstanceFailureAction", next);
}

static void ChoosePowerShellExe(HWND owner)
{
    std::vector<wchar_t> file(32768, L'\0');
    wcsncpy_s(file.data(), file.size(), ReadPowerShellExe().c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    std::wstring filter = L"Executable files\0*.exe\0All files\0*.*\0\0";
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"exe";
    ofn.lpstrTitle = L"Select PowerShell executable";

    if (GetOpenFileNameW(&ofn))
    {
        IniWrite(L"Settings", L"PowerShellExe", file.data());
        Log(L"[i] Setting changed: [%s] %s = %s", L"Settings", L"PowerShellExe", file.data());
    }
}

static bool EnsureTrayIcon(DWORD* err = nullptr)
{
    if (err) *err = ERROR_SUCCESS;

    std::lock_guard<std::mutex> lk(g_trayMutex);
    if (g_nid.hWnd)
    {
        g_tray = true;
        return true;
    }

    if (!g_hwnd)
    {
        if (err) *err = ERROR_INVALID_WINDOW_HANDLE;
        return false;
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize=sizeof(nid);
    nid.hWnd=g_hwnd; nid.uID=1;
    nid.uFlags= NIF_MESSAGE|NIF_ICON|NIF_TIP;
    nid.uCallbackMessage=WM_USER+1;
    HICON ic = LoadIconW(nullptr,IDI_APPLICATION);
    if (!ic)
    {
        if (err) *err = GetLastError();
        return false;
    }
    nid.hIcon=ic;
    CopyTruncated(nid.szTip, g_ui.trayTip);

    if (!Shell_NotifyIconW(NIM_ADD,&nid))
    {
        if (err) *err = GetLastError();
        return false;
    }

    g_nid = nid;
    g_tray = true;
    return true;
}

void TrayRemove()
{
    std::lock_guard<std::mutex> lk(g_trayMutex);
    if (g_nid.hWnd){
        SetLastError(ERROR_SUCCESS);
        if (!Shell_NotifyIconW(NIM_DELETE,&g_nid))
        {
            DWORD err = GetLastError();
            if (err != ERROR_SUCCESS)
                LogWin32Failure(L"Shell_NotifyIconW(NIM_DELETE)", err);
        }
        g_nid = {};
    }
}

static bool TrayIconPresent()
{
    std::lock_guard<std::mutex> lk(g_trayMutex);
    return g_nid.hWnd != nullptr;
}

static void ForgetTrayIconHandle()
{
    std::lock_guard<std::mutex> lk(g_trayMutex);
    g_nid = {};
}

void ShowPSLog(HWND h)
{
    std::wstring psOut;
    std::wstring psMsg;
    bool psErr = false;
    {
        std::lock_guard<std::mutex> lk(g_psMutex);
        psOut = g_psOut;
        psMsg = g_psMsg;
        psErr = g_psErr;
    }
    psOut = FormatPowerShellOutput(psOut);
    psMsg = FormatPowerShellOutput(psMsg);

    if (psOut.empty() && !psMsg.empty())
        psOut = psMsg;
    else if (!psOut.empty() && !psMsg.empty() && psOut.find(psMsg) == std::wstring::npos)
        psOut = psMsg + L"\r\n\r\n" + psOut;

    if (psOut.empty()){
        MessageBoxW(h, g_ui.noPowerShellOutput.c_str(), g_ui.noPowerShellOutputTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    int maxChars = g_powerShellOutputMessageBoxChars.load();
    if (maxChars > 0 && psOut.size() > static_cast<size_t>(maxChars))
    {
        size_t hidden = psOut.size() - static_cast<size_t>(maxChars);
        psOut = psOut.substr(0, static_cast<size_t>(maxChars));
        psOut += L"\r\n\r\n";
        psOut += FormatWide(g_ui.powerShellOutputTruncated.c_str(), static_cast<int>(std::min<size_t>(hidden, INT_MAX)));
    }
    MessageBoxW(h, psOut.c_str(), g_ui.noPowerShellOutputTitle.c_str(), MB_OK | (psErr ? MB_ICONERROR : MB_ICONINFORMATION));
}

INT_PTR CALLBACK RenameDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) noexcept
{
    struct TextInputDialogState
    {
        std::wstring* value;
        bool allowEmpty;
    };
    static TextInputDialogState* state = nullptr;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        state = reinterpret_cast<TextInputDialogState*>(lParam);
        if (state && state->value)
            SetDlgItemTextW(hDlg, TEXT_INPUT_EDIT_ID, state->value->c_str());

        HWND edit = GetDlgItem(hDlg, TEXT_INPUT_EDIT_ID);
        if (edit)
        {
            SetFocus(edit);
            SendMessageW(edit, EM_SETSEL, 0, 0);
            SendMessageW(edit, EM_SCROLLCARET, 0, 0);
            return FALSE;
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            HWND edit = GetDlgItem(hDlg, TEXT_INPUT_EDIT_ID);
            int len = GetWindowTextLengthW(edit);
            std::vector<wchar_t> buf((size_t)len + 1);
            GetWindowTextW(edit, buf.data(), (int)buf.size());

            if ((buf[0] || (state && state->allowEmpty)) && state && state->value)
            {
                *state->value = buf.data();
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

    case WM_DESTROY:
        state = nullptr;
        return FALSE;
    }
    return FALSE;
}

static bool AppendMenuChecked(HMENU menu, UINT flags, UINT_PTR id, const wchar_t* text)
{
    if (!menu)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        LogWin32Failure(L"AppendMenuW");
        return false;
    }

    if (!AppendMenuW(menu, flags, id, text))
    {
        LogWin32Failure(L"AppendMenuW");
        return false;
    }

    return true;
}

void Menu(HWND h)
{
    ReloadRuntimeSettingsFromIni();

    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    if (!m)
    {
        LogWin32Failure(L"CreatePopupMenu(root)");
        return;
    }
    bool showMenuAsDropdown = g_showMenuAsDropdown;

    auto addTo = [&](HMENU target, UINT id, const std::wstring& t, bool chk=false, bool en=true){
        if (g_hideDisabled && !en)
            return;
        UINT flags = MF_STRING;
        if (chk)
            flags |= MF_CHECKED;
        if (!en)
            flags |= MF_DISABLED;
        AppendMenuChecked(target, flags, id, t.c_str());
    };

    if (g_shutdownRequested)
    {
        if (!GenerationWorkPending())
        {
            DestroyMenu(m);
            PostQuitMessage(0);
            return;
        }

        std::wstring reason = FormatWide(g_ui.shutdownPendingReason.c_str(), CurrentGenerationReason().c_str());
        AppendMenuChecked(m, MF_STRING | MF_DISABLED, 0, g_ui.shutdownPendingTitle.c_str());
        AppendMenuChecked(m, MF_STRING | MF_DISABLED, 0, reason.c_str());
        AppendMenuChecked(m, MF_STRING, ID_CANCEL_SHUTDOWN, g_ui.cancelShutdown.c_str());
        AppendMenuChecked(m, MF_STRING, ID_FORCE_SHUTDOWN, g_ui.forceShutdown.c_str());

        SetForegroundWindow(h);
        UINT cmd = static_cast<UINT>(TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr));
        DestroyMenu(m);

        if (cmd == ID_CANCEL_SHUTDOWN)
            CancelRequestedShutdown(h);
        else if (cmd == ID_FORCE_SHUTDOWN)
            ForceShutdownNow();
        return;
    }

    auto menuTitle = [](const std::wstring& text) {
        std::wstring title = TrimCopy(text);
        while (!title.empty() && (title.back() == L':' || iswspace(title.back())))
            title.pop_back();
        return title.empty() ? text : title;
    };

    auto beginSection = [&](const std::wstring& title) -> HMENU {
        if (showMenuAsDropdown)
        {
            HMENU submenu = CreatePopupMenu();
            if (!submenu)
            {
                LogWin32Failure(L"CreatePopupMenu(section)");
                return m;
            }
            return submenu;
        }
        AppendMenuChecked(m, MF_STRING | MF_DISABLED, 0, title.c_str());
        return m;
    };

    auto endSection = [&](HMENU target, const std::wstring& title) {
        if (showMenuAsDropdown)
        {
            if (!target || target == m)
                return;
            std::wstring popupTitle = menuTitle(title);
            if (!AppendMenuChecked(m, MF_POPUP, (UINT_PTR)target, popupTitle.c_str()))
                DestroyMenu(target);
        }
        else
        {
            AppendMenuChecked(m, MF_SEPARATOR, 0, nullptr);
        }
    };

    auto beginNestedMenu = [&](HMENU parent, const wchar_t* logName) -> HMENU {
        HMENU submenu = CreatePopupMenu();
        if (!submenu)
        {
            LogWin32Failure(logName);
            return parent ? parent : m;
        }
        return submenu;
    };

    auto endNestedMenu = [&](HMENU parent, HMENU child, const std::wstring& title) {
        if (!parent || !child)
            return;
        if (child == parent)
        {
            AppendMenuChecked(parent, MF_SEPARATOR, 0, nullptr);
            return;
        }
        std::wstring popupTitle = menuTitle(title);
        if (!AppendMenuChecked(parent, MF_POPUP, (UINT_PTR)child, popupTitle.c_str()))
            DestroyMenu(child);
    };

    addTo(m, ID_SHOW_MENU_DROPDOWN, g_ui.showMenuAsDropdown.c_str(), showMenuAsDropdown);
    addTo(m, ID_GENERATE_NOW, g_ui.generateNow.c_str());
    AppendMenuChecked(m, MF_SEPARATOR, 0, nullptr);

    HMENU generalMenu = beginSection(g_ui.generalTitle);
    addTo(generalMenu, ID_TRAYICON, g_ui.trayIcon.c_str(), g_tray);
    addTo(generalMenu, ID_GENERATE_STARTUP, g_ui.generateOnStartup.c_str(), g_generateOnStartup);
    addTo(generalMenu, ID_GENERATE_STARTUP_CHANGED_ONLY, g_ui.generateOnStartupOnlyWhenChanged.c_str(), g_generateOnStartupOnlyWhenChanged, g_generateOnStartup);
    addTo(generalMenu, ID_HIDE_DISABLED, g_ui.hideDisabledEntries.c_str(), g_hideDisabled);
    addTo(generalMenu, ID_CONSOLE, g_ui.showConsole.c_str(), g_console);
    endSection(generalMenu, g_ui.generalTitle);

    HMENU wallpaperMenu = beginSection(g_ui.wallpaperTitle);
    addTo(wallpaperMenu, ID_LISTEN_WP, g_ui.listenWallpaper.c_str(), g_listenWallpaper);
    HMENU fittingMenu = beginNestedMenu(wallpaperMenu, L"CreatePopupMenu(Wallpaper fitting)");
    addTo(fittingMenu, ID_DISABLE_FIT, g_ui.disableFitting.c_str(), g_disableFitting);
    addTo(fittingMenu, ID_LISTEN_FIT, g_ui.listenFit.c_str(), g_listenFit, !g_disableFitting);
    FitMode mode = g_disableFitting ? FitMode::Fill : GetWallpaperFit();
    std::wstring fitLine;
    if (g_disableFitting)
        fitLine = g_ui.fitModeForced;
    else {
        fitLine = g_ui.fitModePrefix;
        fitLine += FitModeToString(mode);
    }
    AppendMenuChecked(fittingMenu, MF_STRING | MF_DISABLED, 0, fitLine.c_str());
    endNestedMenu(wallpaperMenu, fittingMenu, g_ui.wallpaperFittingTitle);

    HMENU methodsMenu = beginNestedMenu(wallpaperMenu, L"CreatePopupMenu(Wallpaper detection)");
    WallpaperDetectionMethod wallpaperMethod = CurrentWallpaperDetectionMethod();
    addTo(methodsMenu, ID_WP_METHOD_TRANSCODED_CACHE, WallpaperDetectionMethodName(WallpaperDetectionMethod::TranscodedImageCache), wallpaperMethod == WallpaperDetectionMethod::TranscodedImageCache);
    addTo(methodsMenu, ID_WP_METHOD_SYSTEM_PARAMETERS, WallpaperDetectionMethodName(WallpaperDetectionMethod::SystemParametersInfo), wallpaperMethod == WallpaperDetectionMethod::SystemParametersInfo);
    addTo(methodsMenu, ID_WP_METHOD_DESKTOP_WALLPAPER_COM, WallpaperDetectionMethodName(WallpaperDetectionMethod::DesktopWallpaperCOM), wallpaperMethod == WallpaperDetectionMethod::DesktopWallpaperCOM);
    addTo(methodsMenu, ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE, WallpaperDetectionMethodName(WallpaperDetectionMethod::TranscodedWallpaperFile), wallpaperMethod == WallpaperDetectionMethod::TranscodedWallpaperFile);
    addTo(methodsMenu, ID_WP_METHOD_AUTO, WallpaperDetectionMethodName(WallpaperDetectionMethod::Auto), wallpaperMethod == WallpaperDetectionMethod::Auto);
    addTo(methodsMenu, ID_WP_METHOD_FALLBACK, g_ui.wallpaperDetectionFallback.c_str(), g_wallpaperDetectionFallbackOnInvalid);
    std::wstring wallpaperDetectionLine = FormatWide(g_ui.wallpaperDetectionMethodLabel.c_str(), WallpaperDetectionMethodName(wallpaperMethod));
    AppendMenuChecked(methodsMenu, MF_STRING | MF_DISABLED, 0, wallpaperDetectionLine.c_str());
    endNestedMenu(wallpaperMenu, methodsMenu, g_ui.wallpaperDetectionTitle);

    HMENU wallpaperDecodeMenu = beginNestedMenu(wallpaperMenu, L"CreatePopupMenu(Wallpaper decode)");
    bool wallpaperDecodeLowMemory = IniReadI(L"Settings", L"WallpaperDecodeLowMemory", 1) != 0;
    int wallpaperDecodeMaxLongEdge = IniReadClampedI(L"Settings", L"WallpaperDecodeMaxLongEdge", 0, 0, 32768);
    std::wstring wallpaperDecodeMaxLongEdgeText = WallpaperDecodeMaxLongEdgeDisplayText(wallpaperDecodeMaxLongEdge);
    std::wstring wallpaperDecodeMaxLongEdgeLine = FormatWide(g_ui.wallpaperDecodeMaxLongEdgeLabel.c_str(), wallpaperDecodeMaxLongEdgeText.c_str());
    addTo(wallpaperDecodeMenu, ID_WALLPAPER_DECODE_LOW_MEMORY, g_ui.wallpaperDecodeLowMemory.c_str(), wallpaperDecodeLowMemory);
    addTo(wallpaperDecodeMenu, ID_WALLPAPER_DECODE_MAX_LONG_EDGE, wallpaperDecodeMaxLongEdgeLine);
    endNestedMenu(wallpaperMenu, wallpaperDecodeMenu, g_ui.wallpaperDecodeTitle);

    HMENU liveWallpaperMenu = beginNestedMenu(wallpaperMenu, L"CreatePopupMenu(Live Wallpaper Detection)");
    {
        bool liveWallpaperCapture = IniReadI(L"Settings", L"LiveWallpaperCapture", 1) != 0;
        bool liveWallpaperCaptureScreenFallback = IniReadI(L"Settings", L"LiveWallpaperCaptureScreenFallback", 0) != 0;
        int liveWallpaperCaptureDelayMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureDelayMs", 2500, 0, 30000);
        int liveWallpaperCaptureRefreshMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureRefreshMs", 10000, 0, 3600000);
        int liveWallpaperCaptureStartupRefreshMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshMs", 1000, 0, 3600000);
        int liveWallpaperCaptureStartupRefreshDurationMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshDurationMs", 0, 0, 3600000);
        std::wstring liveWallpaperCaptureDelayLine = FormatWide(g_ui.liveWallpaperCaptureDelayMsLabel.c_str(), liveWallpaperCaptureDelayMs);
        std::wstring liveWallpaperCaptureRefreshLine = FormatWide(g_ui.liveWallpaperCaptureRefreshMsLabel.c_str(), LiveWallpaperRefreshDisplayText(liveWallpaperCaptureRefreshMs).c_str());
        std::wstring liveWallpaperCaptureStartupRefreshLine = FormatWide(g_ui.liveWallpaperCaptureStartupRefreshMsLabel.c_str(), LiveWallpaperOffDisplayText(liveWallpaperCaptureStartupRefreshMs).c_str());
        std::wstring liveWallpaperCaptureStartupRefreshDurationLine = FormatWide(g_ui.liveWallpaperCaptureStartupRefreshDurationMsLabel.c_str(), LiveWallpaperOffDisplayText(liveWallpaperCaptureStartupRefreshDurationMs).c_str());
        addTo(liveWallpaperMenu, ID_LIVE_WALLPAPER_CAPTURE, g_ui.liveWallpaperCapture.c_str(), liveWallpaperCapture);
        addTo(liveWallpaperMenu, ID_LIVE_WALLPAPER_CAPTURE_SCREEN_FALLBACK, g_ui.liveWallpaperCaptureScreenFallback.c_str(), liveWallpaperCaptureScreenFallback, liveWallpaperCapture);
        addTo(liveWallpaperMenu, ID_LIVE_WALLPAPER_CAPTURE_DELAY, liveWallpaperCaptureDelayLine.c_str(), false, liveWallpaperCapture);
        addTo(liveWallpaperMenu, ID_LIVE_WALLPAPER_CAPTURE_REFRESH, liveWallpaperCaptureRefreshLine.c_str(), false, liveWallpaperCapture);
        addTo(liveWallpaperMenu, ID_LIVE_WALLPAPER_CAPTURE_STARTUP_REFRESH, liveWallpaperCaptureStartupRefreshLine.c_str(), false, liveWallpaperCapture);
        addTo(liveWallpaperMenu, ID_LIVE_WALLPAPER_CAPTURE_STARTUP_REFRESH_DURATION, liveWallpaperCaptureStartupRefreshDurationLine.c_str(), false, liveWallpaperCapture);
    }
    endNestedMenu(wallpaperMenu, liveWallpaperMenu, g_ui.liveWallpaperCaptureTitle);
    endSection(wallpaperMenu, g_ui.wallpaperTitle);

    HMENU liveTileMenu = beginSection(g_ui.liveTileTitle);
    {
        LiveTileUpdateMode liveTileMode = ConfiguredLiveTileUpdateMode();

        HMENU modeMenu = CreatePopupMenu();
        if (modeMenu)
        {
            addTo(modeMenu, ID_LIVE_TILE_MODE_LIVE_TILE, g_ui.liveTileModeEnabled.c_str(), liveTileMode == LiveTileUpdateMode::LiveTile);
            addTo(modeMenu, ID_LIVE_TILE_MODE_REGISTRATION, g_ui.liveTileModeDisabled.c_str(), liveTileMode == LiveTileUpdateMode::Registration);
            addTo(modeMenu, ID_LIVE_TILE_MODE_AUTO, g_ui.liveTileModeAuto.c_str(), liveTileMode == LiveTileUpdateMode::Auto);
            if (!AppendMenuChecked(liveTileMenu, MF_POPUP, (UINT_PTR)modeMenu, g_ui.liveTileModeTitle.c_str()))
                DestroyMenu(modeMenu);
        }
        else
        {
            LogWin32Failure(L"CreatePopupMenu(Live Tile Mode)");
            addTo(liveTileMenu, ID_LIVE_TILE_MODE_LIVE_TILE, g_ui.liveTileModeEnabled.c_str(), liveTileMode == LiveTileUpdateMode::LiveTile);
            addTo(liveTileMenu, ID_LIVE_TILE_MODE_REGISTRATION, g_ui.liveTileModeDisabled.c_str(), liveTileMode == LiveTileUpdateMode::Registration);
            addTo(liveTileMenu, ID_LIVE_TILE_MODE_AUTO, g_ui.liveTileModeAuto.c_str(), liveTileMode == LiveTileUpdateMode::Auto);
        }

        HMENU registrationMenu = CreatePopupMenu();
        if (registrationMenu)
        {
            addTo(registrationMenu, ID_USE_PS, g_ui.usePowerShell.c_str(), UsePowerShell());
            addTo(registrationMenu, ID_COM_REGISTRATION_USE_HELPER, FormatWide(g_ui.comRegistrationHelperLabel.c_str(), StateEnabled(IniReadI(L"Settings", L"ComRegistrationUseHelper", 0) != 0)), IniReadI(L"Settings", L"ComRegistrationUseHelper", 0) != 0);
            if (!AppendMenuChecked(liveTileMenu, MF_POPUP, (UINT_PTR)registrationMenu, g_ui.liveTileRegistrationTitle.c_str()))
                DestroyMenu(registrationMenu);
        }
        else
        {
            LogWin32Failure(L"CreatePopupMenu(Live Tile Registration)");
            addTo(liveTileMenu, ID_USE_PS, g_ui.usePowerShell.c_str(), UsePowerShell());
            addTo(liveTileMenu, ID_COM_REGISTRATION_USE_HELPER, FormatWide(g_ui.comRegistrationHelperLabel.c_str(), StateEnabled(IniReadI(L"Settings", L"ComRegistrationUseHelper", 0) != 0)), IniReadI(L"Settings", L"ComRegistrationUseHelper", 0) != 0);
        }

        HMENU runtimeMenu = CreatePopupMenu();
        if (runtimeMenu)
        {
            addTo(runtimeMenu, ID_LIVE_TILE_RELAUNCH_ON_SWITCH, g_ui.liveTileRelaunchOnSwitch.c_str(), g_liveTileRelaunchOnSwitch);
            addTo(runtimeMenu, ID_CLEAR_LIVE_TILE_ON_SHUTDOWN, g_ui.clearLiveTileOnShutdown.c_str(), g_clearLiveTileOnShutdown);
            if (!AppendMenuChecked(liveTileMenu, MF_POPUP, (UINT_PTR)runtimeMenu, g_ui.liveTileRuntimeTitle.c_str()))
                DestroyMenu(runtimeMenu);
        }
        else
        {
            LogWin32Failure(L"CreatePopupMenu(Live Tile Runtime)");
            addTo(liveTileMenu, ID_LIVE_TILE_RELAUNCH_ON_SWITCH, g_ui.liveTileRelaunchOnSwitch.c_str(), g_liveTileRelaunchOnSwitch);
            addTo(liveTileMenu, ID_CLEAR_LIVE_TILE_ON_SHUTDOWN, g_ui.clearLiveTileOnShutdown.c_str(), g_clearLiveTileOnShutdown);
        }

        HMENU win8Menu = CreatePopupMenu();
        if (win8Menu)
        {
            addTo(win8Menu, ID_WIN8_LIVE_TILE_BROKER_APP, g_ui.win8LiveTileBrokerApp.c_str(), IniReadI(L"Settings", L"Win8LiveTileBrokerApp", 1) != 0);
            addTo(win8Menu, ID_WIN8_LIVE_TILE_BACKGROUND_TASK, g_ui.win8LiveTileBackgroundTask.c_str(), IniReadI(L"Settings", L"Win8LiveTileBackgroundTask", 0) != 0);
            addTo(win8Menu, ID_WIN8_LIVE_TILE_OOP_HELPER, g_ui.win8LiveTileOopHelper.c_str(), IniReadI(L"Settings", L"Win8LiveTileOopHelper", 0) != 0);
            if (!AppendMenuChecked(liveTileMenu, MF_POPUP, (UINT_PTR)win8Menu, g_ui.liveTileWindows8Title.c_str()))
                DestroyMenu(win8Menu);
        }
        else
        {
            LogWin32Failure(L"CreatePopupMenu(Live Tile Windows 8)");
            addTo(liveTileMenu, ID_WIN8_LIVE_TILE_BROKER_APP, g_ui.win8LiveTileBrokerApp.c_str(), IniReadI(L"Settings", L"Win8LiveTileBrokerApp", 1) != 0);
            addTo(liveTileMenu, ID_WIN8_LIVE_TILE_BACKGROUND_TASK, g_ui.win8LiveTileBackgroundTask.c_str(), IniReadI(L"Settings", L"Win8LiveTileBackgroundTask", 0) != 0);
            addTo(liveTileMenu, ID_WIN8_LIVE_TILE_OOP_HELPER, g_ui.win8LiveTileOopHelper.c_str(), IniReadI(L"Settings", L"Win8LiveTileOopHelper", 0) != 0);
        }
    }
    endSection(liveTileMenu, g_ui.liveTileTitle);

    HMENU appearanceAssetsMenu = beginSection(g_ui.appearanceAssetsTitle);
    HMENU tileTextMenu = beginNestedMenu(appearanceAssetsMenu, L"CreatePopupMenu(Tile Text)");
    {
        HMENU textMenu = tileTextMenu;
        if (!textMenu)
        {
            LogWin32Failure(L"CreatePopupMenu(Tile Text)");
            textMenu = appearanceAssetsMenu;
        }

        auto shortTileText = [](const std::wstring& value) -> std::wstring
        {
            std::wstring text = TrimCopy(value);
            if (text.empty())
                return L"(empty)";
            std::replace(text.begin(), text.end(), L'\r', L' ');
            std::replace(text.begin(), text.end(), L'\n', L' ');
            if (text.size() > 48)
                text = text.substr(0, 45) + L"...";
            return text;
        };

        bool tileTextEnabled = IniReadI(L"TileText", L"Enabled", 0) != 0;
        std::wstring tileText = TrimCopy(IniReadS(L"TileText", L"Text", L""));
        std::wstring secondaryText = TrimCopy(IniReadS(L"TileText", L"SecondaryText", L""));
        std::wstring badgeText = TrimCopy(IniReadS(L"TileText", L"BadgeText", L""));
        std::wstring textLine = FormatWide(g_ui.tileTextCurrent.c_str(), shortTileText(tileText).c_str());
        std::wstring secondaryLine = FormatWide(g_ui.tileTextSecondaryCurrent.c_str(), shortTileText(secondaryText).c_str());
        std::wstring badgeLine = FormatWide(g_ui.tileTextBadgeCurrent.c_str(), shortTileText(badgeText).c_str());
        std::wstring hLine = FormatWide(g_ui.tileTextHorizontalAlign.c_str(), NormalizedTileTextHorizontalAlign());
        std::wstring vLine = FormatWide(g_ui.tileTextVerticalAlign.c_str(), NormalizedTileTextVerticalAlign());
        std::wstring badgeHLine = FormatWide(g_ui.tileTextBadgeHorizontalAlign.c_str(), NormalizedTileTextBadgeHorizontalAlign());
        std::wstring badgeVLine = FormatWide(g_ui.tileTextBadgeVerticalAlign.c_str(), NormalizedTileTextBadgeVerticalAlign());
        std::wstring fontLine = FormatWide(g_ui.tileTextFont.c_str(), IniReadS(L"TileText", L"Font", L"Segoe UI").c_str());
        std::wstring fontSizeLine = FormatWide(g_ui.tileTextFontSize.c_str(), IniReadClampedI(L"TileText", L"FontSize", 18, 6, 256));
        std::wstring secondaryFontSizeLine = FormatWide(g_ui.tileTextSecondaryFontSize.c_str(), IniReadClampedI(L"TileText", L"SecondaryFontSize", 12, 6, 256));
        std::wstring badgeFontSizeLine = FormatWide(g_ui.tileTextBadgeFontSize.c_str(), IniReadClampedI(L"TileText", L"BadgeFontSize", 40, 6, 256));
        std::wstring colorLine = FormatWide(g_ui.tileTextColor.c_str(), IniReadS(L"TileText", L"Color", L"#FFFFFFFF").c_str());
        std::wstring secondaryColorLine = FormatWide(g_ui.tileTextSecondaryColor.c_str(), IniReadS(L"TileText", L"SecondaryColor", L"#CCFFFFFF").c_str());
        std::wstring badgeColorLine = FormatWide(g_ui.tileTextBadgeColor.c_str(), IniReadS(L"TileText", L"BadgeColor", L"#FFFFFFFF").c_str());
        std::wstring shadowColorLine = FormatWide(g_ui.tileTextShadowColor.c_str(), IniReadS(L"TileText", L"ShadowColor", L"#AA000000").c_str());
        std::wstring marginXLine = FormatWide(g_ui.tileTextMarginX.c_str(), IniReadClampedI(L"TileText", L"MarginX", 8, 0, 512));
        std::wstring marginYLine = FormatWide(g_ui.tileTextMarginY.c_str(), IniReadClampedI(L"TileText", L"MarginY", 10, 0, 512));
        std::wstring lineGapLine = FormatWide(g_ui.tileTextLineGap.c_str(), IniReadClampedI(L"TileText", L"LineGap", 2, 0, 128));
        std::wstring maxSecondaryLinesLine = FormatWide(g_ui.tileTextMaxSecondaryLines.c_str(), IniReadClampedI(L"TileText", L"MaxSecondaryLines", 2, 0, 32));

        addTo(textMenu, ID_TILE_TEXT_ENABLED, g_ui.tileTextEnabled.c_str(), tileTextEnabled);
        addTo(textMenu, ID_TILE_TEXT_EDIT, g_ui.tileTextEdit.c_str());
        addTo(textMenu, ID_TILE_TEXT_CLEAR, g_ui.tileTextClear.c_str(), false, !tileText.empty());
        AppendMenuChecked(textMenu, MF_STRING | MF_DISABLED, 0, textLine.c_str());
        addTo(textMenu, ID_TILE_TEXT_SECONDARY_EDIT, g_ui.tileTextSecondaryEdit.c_str());
        addTo(textMenu, ID_TILE_TEXT_SECONDARY_CLEAR, g_ui.tileTextSecondaryClear.c_str(), false, !secondaryText.empty());
        AppendMenuChecked(textMenu, MF_STRING | MF_DISABLED, 0, secondaryLine.c_str());
        addTo(textMenu, ID_TILE_TEXT_BADGE_EDIT, g_ui.tileTextBadgeEdit.c_str());
        addTo(textMenu, ID_TILE_TEXT_BADGE_CLEAR, g_ui.tileTextBadgeClear.c_str(), false, !badgeText.empty());
        AppendMenuChecked(textMenu, MF_STRING | MF_DISABLED, 0, badgeLine.c_str());
        addTo(textMenu, ID_TILE_TEXT_H_ALIGN, hLine);
        addTo(textMenu, ID_TILE_TEXT_V_ALIGN, vLine);
        addTo(textMenu, ID_TILE_TEXT_BADGE_H_ALIGN, badgeHLine);
        addTo(textMenu, ID_TILE_TEXT_BADGE_V_ALIGN, badgeVLine);
        addTo(textMenu, ID_TILE_TEXT_FONT, fontLine);
        addTo(textMenu, ID_TILE_TEXT_FONT_SIZE, fontSizeLine);
        addTo(textMenu, ID_TILE_TEXT_SECONDARY_FONT_SIZE, secondaryFontSizeLine);
        addTo(textMenu, ID_TILE_TEXT_BADGE_FONT_SIZE, badgeFontSizeLine);
        addTo(textMenu, ID_TILE_TEXT_BOLD, g_ui.tileTextBold.c_str(), IniReadI(L"TileText", L"Bold", 0) != 0);
        addTo(textMenu, ID_TILE_TEXT_SECONDARY_BOLD, g_ui.tileTextSecondaryBold.c_str(), IniReadI(L"TileText", L"SecondaryBold", 0) != 0);
        addTo(textMenu, ID_TILE_TEXT_BADGE_BOLD, g_ui.tileTextBadgeBold.c_str(), IniReadI(L"TileText", L"BadgeBold", 0) != 0);
        addTo(textMenu, ID_TILE_TEXT_ITALIC, g_ui.tileTextItalic.c_str(), IniReadI(L"TileText", L"Italic", 0) != 0);
        addTo(textMenu, ID_TILE_TEXT_COLOR, colorLine);
        addTo(textMenu, ID_TILE_TEXT_SECONDARY_COLOR, secondaryColorLine);
        addTo(textMenu, ID_TILE_TEXT_BADGE_COLOR, badgeColorLine);
        addTo(textMenu, ID_TILE_TEXT_SHADOW, g_ui.tileTextShadow.c_str(), IniReadI(L"TileText", L"Shadow", 1) != 0);
        addTo(textMenu, ID_TILE_TEXT_SHADOW_COLOR, shadowColorLine);
        addTo(textMenu, ID_TILE_TEXT_MARGIN_X, marginXLine);
        addTo(textMenu, ID_TILE_TEXT_MARGIN_Y, marginYLine);
        addTo(textMenu, ID_TILE_TEXT_LINE_GAP, lineGapLine);
        addTo(textMenu, ID_TILE_TEXT_MAX_SECONDARY_LINES, maxSecondaryLinesLine);
        addTo(textMenu, ID_TILE_TEXT_APPLY_MEDIUM, g_ui.tileTextApplyMedium.c_str(), IniReadI(L"TileText", L"ApplyToMediumTile", 1) != 0);
        addTo(textMenu, ID_TILE_TEXT_APPLY_WIDE, g_ui.tileTextApplyWide.c_str(), IniReadI(L"TileText", L"ApplyToWideTile", 1) != 0);
        addTo(textMenu, ID_TILE_TEXT_APPLY_LARGE, g_ui.tileTextApplyLarge.c_str(), IniReadI(L"TileText", L"ApplyToLargeTile", 1) != 0);
        addTo(textMenu, ID_TILE_TEXT_APPLY_SMALL, g_ui.tileTextApplySmall.c_str(), IniReadI(L"TileText", L"ApplyToSmallTile", 0) != 0);
        addTo(textMenu, ID_TILE_TEXT_APPLY_LOGOS, g_ui.tileTextApplyLogos.c_str(), IniReadI(L"TileText", L"ApplyToLogos", 0) != 0);
    }
    endNestedMenu(appearanceAssetsMenu, tileTextMenu, g_ui.tileTextTitle);

    HMENU assetsMenu = beginNestedMenu(appearanceAssetsMenu, L"CreatePopupMenu(Assets)");
    addTo(assetsMenu, ID_GENERATE_DESKTOP_ICON_DISABLED, g_ui.generateDesktopIconForDisabledEntries.c_str(), g_generateDesktopIconForDisabledEntries);
    addTo(assetsMenu, ID_DELETE_DISABLED_ASSETS, g_ui.deleteDisabledAssets.c_str(), g_deleteDisabledAssets, !g_generateDesktopIconForDisabledEntries);
    const wchar_t* keys[] = {
        L"StoreLogo", L"MediumTile", L"Square44x44Logo", L"Square30x30Logo",
        L"SmallTile", L"WideTile", L"LargeTile", L"SplashScreen"
    };
    for (int i = 0; i < static_cast<int>(_countof(keys)); i++)
        addTo(assetsMenu, static_cast<UINT>(ID_A1 + i), keys[i], IniReadI(L"Assets", keys[i], 0) != 0);
    endNestedMenu(appearanceAssetsMenu, assetsMenu, g_ui.assetsTitle);

    HMENU scaleMenu = beginNestedMenu(appearanceAssetsMenu, L"CreatePopupMenu(DPI scales)");
    addTo(scaleMenu, ID_SCALE_AUTO, g_ui.generateScaleAuto.c_str(), g_generateScaleAuto);
    // Manual scale items are greyed out while Auto is active (they are ignored by GetConfiguredScales)
    bool manualScalesEnabled = !g_generateScaleAuto;
    addTo(scaleMenu, ID_SCALE_100, g_ui.generateScale100.c_str(), g_generateScale100, manualScalesEnabled);
    addTo(scaleMenu, ID_SCALE_125, g_ui.generateScale125.c_str(), g_generateScale125, manualScalesEnabled);
    addTo(scaleMenu, ID_SCALE_150, g_ui.generateScale150.c_str(), g_generateScale150, manualScalesEnabled);
    addTo(scaleMenu, ID_SCALE_200, g_ui.generateScale200.c_str(), g_generateScale200, manualScalesEnabled);
    addTo(scaleMenu, ID_SCALE_400, g_ui.generateScale400.c_str(), g_generateScale400, manualScalesEnabled);
    std::wstring currentDpiLine = FormatWide(g_ui.currentDpiScaleLabel.c_str(), CurrentDpiPercent(), CurrentAssetScale());
    AppendMenuChecked(scaleMenu, MF_STRING | MF_DISABLED, 0, currentDpiLine.c_str());
    if (g_generateScaleAuto)
        AppendMenuChecked(scaleMenu, MF_STRING | MF_DISABLED, 0, g_ui.generateScaleAutoPreservesManualToggles.c_str());
    endNestedMenu(appearanceAssetsMenu, scaleMenu, g_ui.dpiScalesTitle);

    HMENU cachingMenu = beginNestedMenu(appearanceAssetsMenu, L"CreatePopupMenu(Caching)");
    bool cacheEnabled = GeneratedAssetCacheEnabled();
    bool cacheCanStore = cacheEnabled && GeneratedAssetCacheMaxEntries() > 0;
    bool precacheConfigured = IniReadI(L"Settings", L"GeneratedAssetPrecache", 1) != 0;
    addTo(cachingMenu, ID_GENERATED_ASSET_CACHE, g_ui.generatedAssetCache.c_str(), cacheEnabled);
    addTo(cachingMenu, ID_GENERATED_ASSET_PRECACHE, g_ui.generatedAssetPrecache.c_str(), precacheConfigured, cacheCanStore);
    std::wstring cacheMaxEntriesLine = FormatWide(g_ui.generatedAssetCacheMaxEntriesLabel.c_str(), GeneratedAssetCacheMaxEntries());
    std::wstring precacheMaxFilesLine = FormatWide(g_ui.generatedAssetPrecacheMaxFilesLabel.c_str(), GeneratedAssetPrecacheMaxFiles());
    addTo(cachingMenu, ID_GENERATED_ASSET_CACHE_MAX_ENTRIES, cacheMaxEntriesLine);
    addTo(cachingMenu, ID_GENERATED_ASSET_PRECACHE_MAX_FILES, precacheMaxFilesLine, false, cacheCanStore);
    endNestedMenu(appearanceAssetsMenu, cachingMenu, g_ui.cachingTitle);

    HMENU manifestMenu = beginNestedMenu(appearanceAssetsMenu, L"CreatePopupMenu(Manifest)");
    ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();
    std::wstring manifestSourceLine = FormatWide(g_ui.manifestSourceLabel.c_str(), manifestInfo.existing ? g_ui.manifestSourceExisting.c_str() : g_ui.manifestSourceGenerated.c_str());
    std::wstring manifestTargetLine = FormatWide(g_ui.manifestTargetLabel.c_str(), manifestInfo.manifestTarget.c_str());
    std::wstring manifestIdentityLine = FormatWide(g_ui.manifestIdentityNameLabel.c_str(), manifestInfo.identityName.c_str());
    std::wstring manifestPublisherLine = FormatWide(g_ui.manifestPublisherLabel.c_str(), manifestInfo.publisher.c_str());
    std::wstring manifestPackageVersionLine = FormatWide(g_ui.manifestPackageVersionLabel.c_str(), manifestInfo.packageVersion.c_str());
    std::wstring manifestDisplayNameLine = FormatWide(g_ui.manifestDisplayNameLabel.c_str(), manifestInfo.displayName.c_str());
    std::wstring manifestPublisherDisplayNameLine = FormatWide(g_ui.manifestPublisherDisplayNameLabel.c_str(), manifestInfo.publisherDisplayName.c_str());
    std::wstring manifestDescriptionLine = FormatWide(g_ui.manifestDescriptionLabel.c_str(), manifestInfo.description.c_str());
    std::wstring manifestExecutableLine = FormatWide(g_ui.manifestExecutableLabel.c_str(), manifestInfo.executable.c_str());
    std::wstring manifestApplicationIdLine = FormatWide(g_ui.manifestApplicationIdLabel.c_str(), manifestInfo.applicationId.c_str());
    std::wstring manifestTargetDeviceFamilyNameLine = FormatWide(g_ui.manifestTargetDeviceFamilyNameLabel.c_str(), manifestInfo.targetDeviceFamilyName.c_str());
    std::wstring manifestMinVersionLine = FormatWide(g_ui.manifestMinVersionLabel.c_str(), manifestInfo.minVersion.c_str());
    std::wstring manifestMaxVersionTestedLine = FormatWide(g_ui.manifestMaxVersionTestedLabel.c_str(), manifestInfo.maxVersionTested.c_str());
    std::wstring manifestResourceLanguageLine = FormatWide(g_ui.manifestResourceLanguageLabel.c_str(), manifestInfo.resourceLanguage.c_str());
    std::wstring manifestPropertiesLogoLine = FormatWide(g_ui.manifestPropertiesLogoLabel.c_str(), manifestInfo.propertiesLogo.c_str());
    std::wstring manifestSquare150x150LogoLine = FormatWide(g_ui.manifestSquare150x150LogoLabel.c_str(), manifestInfo.square150x150Logo.c_str());
    std::wstring manifestSquare44x44LogoLine = FormatWide(g_ui.manifestSquare44x44LogoLabel.c_str(), manifestInfo.square44x44Logo.c_str());
    std::wstring manifestSquare30x30LogoLine = FormatWide(g_ui.manifestSquare30x30LogoLabel.c_str(), manifestInfo.square30x30Logo.c_str());
    std::wstring manifestSquare71x71LogoLine = FormatWide(g_ui.manifestSquare71x71LogoLabel.c_str(), manifestInfo.square71x71Logo.c_str());
    std::wstring manifestWide310x150LogoLine = FormatWide(g_ui.manifestWide310x150LogoLabel.c_str(), manifestInfo.wide310x150Logo.c_str());
    std::wstring manifestSquare310x310LogoLine = FormatWide(g_ui.manifestSquare310x310LogoLabel.c_str(), manifestInfo.square310x310Logo.c_str());
    std::wstring manifestBackgroundColorLine = FormatWide(g_ui.manifestBackgroundColorLabel.c_str(), manifestInfo.backgroundColor.c_str());
    std::wstring manifestShowNameOnTilesLine = FormatWide(g_ui.manifestShowNameOnTilesLabel.c_str(), manifestInfo.showNameOnTiles.c_str());
    std::wstring manifestEntryPointLine = FormatWide(g_ui.manifestEntryPointLabel.c_str(), manifestInfo.entryPoint.c_str());
    std::wstring manifestRestrictedCapabilityLine = FormatWide(g_ui.manifestRestrictedCapabilityLabel.c_str(), manifestInfo.restrictedCapability.c_str());
    std::wstring manifestSplashScreenImageLine = FormatWide(g_ui.manifestSplashScreenImageLabel.c_str(), manifestInfo.splashScreenImage.c_str());
    addTo(manifestMenu, ID_MANIFEST_REGENERATE, g_ui.manifestRegenerateNow.c_str());
    {
        ManifestCompatibilityTarget manifestTarget = ConfiguredManifestCompatibilityTarget();
        addTo(manifestMenu, ID_MANIFEST_TARGET_WINDOWS10, g_ui.appxManifestTargetWindows10.c_str(), manifestTarget == ManifestCompatibilityTarget::Windows10);
        addTo(manifestMenu, ID_MANIFEST_TARGET_WINDOWS81, g_ui.appxManifestTargetWindows81.c_str(), manifestTarget == ManifestCompatibilityTarget::Windows81);
        addTo(manifestMenu, ID_MANIFEST_TARGET_WINDOWS8, g_ui.appxManifestTargetWindows8.c_str(), manifestTarget == ManifestCompatibilityTarget::Windows8);
    }

    HMENU manifestDetailsMenu = CreatePopupMenu();
    if (!manifestDetailsMenu)
    {
        LogWin32Failure(L"CreatePopupMenu(manifest details)");
        manifestDetailsMenu = manifestMenu;
    }
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestSourceLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestTargetLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestIdentityLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestPublisherLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestPackageVersionLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestDisplayNameLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestPublisherDisplayNameLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestDescriptionLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestExecutableLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestApplicationIdLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestTargetDeviceFamilyNameLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestMinVersionLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestMaxVersionTestedLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestResourceLanguageLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestPropertiesLogoLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestSquare150x150LogoLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestSquare44x44LogoLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestSquare30x30LogoLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestSquare71x71LogoLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestWide310x150LogoLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestSquare310x310LogoLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestBackgroundColorLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestShowNameOnTilesLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestEntryPointLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestRestrictedCapabilityLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestSplashScreenImageLine.c_str());
    if (manifestDetailsMenu != manifestMenu)
    {
        if (!AppendMenuChecked(manifestMenu, MF_POPUP, (UINT_PTR)manifestDetailsMenu, g_ui.manifestDetailsTitle.c_str()))
            DestroyMenu(manifestDetailsMenu);
    }
    endNestedMenu(appearanceAssetsMenu, manifestMenu, g_ui.manifestTitle);
    endSection(appearanceAssetsMenu, g_ui.appearanceAssetsTitle);

    HMENU diagnosticsMenu = beginSection(g_ui.diagnosticsTitle);
    HMENU notificationsMenu = beginNestedMenu(diagnosticsMenu, L"CreatePopupMenu(Notifications)");
    addTo(notificationsMenu, ID_NOTIF_ENABLED, g_ui.notificationsEnabled.c_str(), g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_START, g_ui.notifyOnStart.c_str(), g_notifyOnStart, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_SUCCESS, g_ui.notifyOnSuccess.c_str(), g_notifyOnSuccess, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_FAILURE, g_ui.notifyOnFailure.c_str(), g_notifyOnFailure, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_BUSY, g_ui.notifyOnBusy.c_str(), g_notifyOnBusy, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_ALREADY_RUNNING, g_ui.notifyOnAlreadyRunning.c_str(), g_notifyOnAlreadyRunning, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_SINGLE_INSTANCE_FAILURE, g_ui.notifyOnSingleInstanceFailure.c_str(), g_notifyOnSingleInstanceFailure, g_notificationsEnabled);
    endNestedMenu(diagnosticsMenu, notificationsMenu, g_ui.notificationsTitle);

    HMENU loggingMenu = beginNestedMenu(diagnosticsMenu, L"CreatePopupMenu(Logging)");
    addTo(loggingMenu, ID_LOG, g_ui.loggingEnable.c_str(), g_logging);
    addTo(loggingMenu, ID_LOGFILE, g_ui.changePath.c_str(), false, g_logging);
    addTo(loggingMenu, ID_LOG_RENAME, g_ui.renamePath.c_str(), false, g_logging);
    addTo(loggingMenu, ID_LOG_OPEN, g_ui.openInExplorer.c_str(), false, g_logging);
    addTo(loggingMenu, ID_LOG_RESET, g_ui.resetDefault.c_str(), false, g_logging);
    addTo(loggingMenu, ID_SHOW_PSLOG, g_ui.showPowerShellLog.c_str());

    std::wstring pathLine = g_ui.logPathPrefix + GetLogPathCopy();
    HMENU hPath = CreatePopupMenu();
    if (!hPath)
        LogWin32Failure(L"CreatePopupMenu(log path)");
    else
    {
        AppendMenuChecked(hPath, MF_STRING | MF_DISABLED, 0, pathLine.c_str());
        if (!AppendMenuChecked(loggingMenu, MF_POPUP, (UINT_PTR)hPath, g_ui.logPathTitle.c_str()))
            DestroyMenu(hPath);
    }

    std::wstring psLine = g_ui.psStatusPrefix;
    {
        std::lock_guard<std::mutex> lk(g_psMutex);
        if (!g_psMsg.empty())
            psLine += g_psMsg;
        else if (g_psErr)
            psLine += g_ui.errorTitle.c_str();
        else
            psLine += g_ui.okText.c_str();
    }

    HMENU hPs = CreatePopupMenu();
    if (!hPs)
        LogWin32Failure(L"CreatePopupMenu(PowerShell status)");
    else
    {
        AppendMenuChecked(hPs, MF_STRING | MF_DISABLED, 0, psLine.c_str());
        if (!AppendMenuChecked(loggingMenu, MF_POPUP, (UINT_PTR)hPs, g_ui.psStatusTitle.c_str()))
            DestroyMenu(hPs);
    }
    endNestedMenu(diagnosticsMenu, loggingMenu, g_ui.loggingTitle);
    endSection(diagnosticsMenu, g_ui.diagnosticsTitle);

    HMENU advancedMenu = beginSection(g_ui.advancedTitle);
    std::wstring pollLine = FormatWide(g_ui.pollIntervalLabel.c_str(), IniReadClampedI(L"Settings", L"PollIntervalMs", 2000, 250, 60000));
    std::wstring confirmLine = FormatWide(g_ui.confirmMsLabel.c_str(), IniReadClampedI(L"Settings", L"ConfirmMs", 800, 0, 10000));
    std::wstring debounceLine = FormatWide(g_ui.debounceMinMsLabel.c_str(), IniReadClampedI(L"Settings", L"DebounceMinMs", 1200, 0, 60000));
    std::wstring pollInitialBypassLine = FormatWide(g_ui.pollInitialDebounceBypassMsLabel.c_str(), IniReadClampedI(L"Settings", L"PollInitialDebounceBypassMs", 5000, 0, 60000));
    std::wstring iniCacheRefreshLine = FormatWide(g_ui.iniCacheRefreshMsLabel.c_str(), IniReadClampedI(L"Settings", L"IniCacheRefreshMs", 250, 0, 60000));
    std::wstring wallpaperDetectionLogRepeatLine = FormatWide(g_ui.wallpaperDetectionLogRepeatMsLabel.c_str(), IniReadClampedI(L"Settings", L"WallpaperDetectionLogRepeatMs", 60000, 0, 3600000));
    std::wstring notificationTimeoutLine = FormatWide(g_ui.notificationTimeoutMsLabel.c_str(), IniReadClampedI(L"Settings", L"NotificationTimeoutMs", 4000, 1000, 60000));
    std::wstring shutdownInitialLine = FormatWide(g_ui.shutdownInitialNoticeMsLabel.c_str(), IniReadClampedI(L"Settings", L"ShutdownInitialNoticeMs", 1000, 250, 60000));
    std::wstring shutdownRepeatLine = FormatWide(g_ui.shutdownRepeatNoticeMsLabel.c_str(), IniReadClampedI(L"Settings", L"ShutdownRepeatNoticeMs", 5000, 250, 60000));
    std::wstring signalRetriesLine = FormatWide(g_ui.singleInstanceSignalRetriesLabel.c_str(), IniReadClampedI(L"Settings", L"SingleInstanceSignalRetries", 50, 0, 200));
    std::wstring signalDelayLine = FormatWide(g_ui.singleInstanceSignalDelayMsLabel.c_str(), IniReadClampedI(L"Settings", L"SingleInstanceSignalDelayMs", 100, 0, 5000));
    int comRegistrationTimeoutMs = IniReadClampedI(L"Settings", L"ComRegistrationTimeoutMs", 120000, 0, 600000);
    int powerShellTimeoutMs = IniReadClampedI(L"Settings", L"PowerShellTimeoutMs", 120000, 0, 600000);
    std::wstring comRegistrationTimeoutText = TimeoutDisplayText(comRegistrationTimeoutMs);
    std::wstring powerShellTimeoutText = TimeoutDisplayText(powerShellTimeoutMs);
    std::wstring comRegistrationTimeoutLine = FormatWide(g_ui.comRegistrationTimeoutMsLabel.c_str(), comRegistrationTimeoutText.c_str());
    std::wstring comRegistrationCancelWaitLine = FormatWide(g_ui.comRegistrationCancelWaitMsLabel.c_str(), IniReadClampedI(L"Settings", L"ComRegistrationCancelWaitMs", 5000, 0, 60000));
    std::wstring trayHideDelayLine = FormatWide(g_ui.trayIconHiddenDelayMsLabel.c_str(), IniReadClampedI(L"Settings", L"TrayIconHiddenDelayMs", 750, 0, 10000));
    std::wstring logBufferLine = FormatWide(g_ui.logBufferLinesLabel.c_str(), IniReadClampedI(L"Settings", L"LogBufferLines", 1024, 1, 100000));
    std::wstring logAppendLockWaitLine = FormatWide(g_ui.logAppendLockWaitMsLabel.c_str(), IniReadClampedI(L"Settings", L"LogAppendLockWaitMs", 1000, 0, 60000));
    std::wstring powerShellExeLine = FormatWide(g_ui.powerShellExeLabel.c_str(), ReadPowerShellExe().c_str());
    std::wstring powerShellPollLine = FormatWide(g_ui.powerShellPollMsLabel.c_str(), IniReadClampedI(L"Settings", L"PowerShellPollMs", 50, 10, 1000));
    std::wstring powerShellTimeoutLine = FormatWide(g_ui.powerShellTimeoutMsLabel.c_str(), powerShellTimeoutText.c_str());
    std::wstring powerShellTerminateWaitLine = FormatWide(g_ui.powerShellTerminateWaitMsLabel.c_str(), IniReadClampedI(L"Settings", L"PowerShellTerminateWaitMs", 5000, 0, 60000));
    std::wstring powerShellOutputLimitLine = FormatWide(g_ui.powerShellOutputMessageBoxCharsLabel.c_str(), IniReadClampedI(L"Settings", L"PowerShellOutputMessageBoxChars", 6000, 0, 60000));
    std::wstring registrationUnresolvedBlockLine = FormatWide(g_ui.registrationUnresolvedBlockMsLabel.c_str(), IniReadClampedI(L"Settings", L"RegistrationUnresolvedBlockMs", 300000, 0, 3600000));
    std::wstring savePngReplaceRetriesLine = FormatWide(g_ui.savePngReplaceRetriesLabel.c_str(), IniReadClampedI(L"Settings", L"SavePngReplaceRetries", 3, 0, 20));
    std::wstring savePngReplaceRetryDelayLine = FormatWide(g_ui.savePngReplaceRetryDelayMsLabel.c_str(), IniReadClampedI(L"Settings", L"SavePngReplaceRetryDelayMs", 75, 0, 5000));
    std::wstring singleInstanceLine = FormatWide(g_ui.singleInstanceFailureActionLabel.c_str(), ErrorActionName(CurrentSingleInstanceFailureAction()));
    HMENU advancedTimingMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced timing)");
    addTo(advancedTimingMenu, ID_ADV_POLL_INTERVAL, pollLine);
    addTo(advancedTimingMenu, ID_ADV_CONFIRM_MS, confirmLine);
    addTo(advancedTimingMenu, ID_ADV_DEBOUNCE_MIN_MS, debounceLine);
    addTo(advancedTimingMenu, ID_ADV_POLL_INITIAL_BYPASS_MS, pollInitialBypassLine);
    addTo(advancedTimingMenu, ID_ADV_INI_CACHE_REFRESH_MS, iniCacheRefreshLine);
    addTo(advancedTimingMenu, ID_ADV_WALLPAPER_DETECTION_LOG_REPEAT_MS, wallpaperDetectionLogRepeatLine);
    addTo(advancedTimingMenu, ID_ADV_NOTIFICATION_TIMEOUT_MS, notificationTimeoutLine);
    endNestedMenu(advancedMenu, advancedTimingMenu, g_ui.advancedTimingTitle);

    HMENU advancedShutdownMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced shutdown)");
    addTo(advancedShutdownMenu, ID_ADV_SHUTDOWN_INITIAL_MS, shutdownInitialLine);
    addTo(advancedShutdownMenu, ID_ADV_SHUTDOWN_REPEAT_MS, shutdownRepeatLine);
    addTo(advancedShutdownMenu, ID_ADV_TRAY_ICON_HIDDEN_DELAY_MS, trayHideDelayLine);
    addTo(advancedShutdownMenu, ID_ADV_TRIM_WORKING_SET_ON_IDLE, g_ui.trimWorkingSetOnIdle.c_str(), g_trimWorkingSetOnIdle);
    addTo(advancedShutdownMenu, ID_ADV_COMPACT_CRT_HEAP_ON_IDLE, g_ui.compactCrtHeapOnIdle.c_str(), g_compactCrtHeapOnIdle);
    endNestedMenu(advancedMenu, advancedShutdownMenu, g_ui.advancedShutdownTitle);

    HMENU advancedRegistrationMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced registration)");
    addTo(advancedRegistrationMenu, ID_ADV_COM_REGISTRATION_TIMEOUT_MS, comRegistrationTimeoutLine);
    addTo(advancedRegistrationMenu, ID_ADV_COM_REGISTRATION_CANCEL_WAIT_MS, comRegistrationCancelWaitLine);
    addTo(advancedRegistrationMenu, ID_ADV_REGISTRATION_UNRESOLVED_BLOCK_MS, registrationUnresolvedBlockLine);
    endNestedMenu(advancedMenu, advancedRegistrationMenu, g_ui.advancedRegistrationTitle);

    HMENU advancedPowerShellMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced PowerShell)");
    addTo(advancedPowerShellMenu, ID_ADV_POWER_SHELL_EXE, powerShellExeLine);
    addTo(advancedPowerShellMenu, ID_ADV_POWER_SHELL_POLL_MS, powerShellPollLine);
    addTo(advancedPowerShellMenu, ID_ADV_POWER_SHELL_TIMEOUT_MS, powerShellTimeoutLine);
    addTo(advancedPowerShellMenu, ID_ADV_POWER_SHELL_TERMINATE_WAIT_MS, powerShellTerminateWaitLine);
    addTo(advancedPowerShellMenu, ID_ADV_POWER_SHELL_OUTPUT_LIMIT, powerShellOutputLimitLine);
    endNestedMenu(advancedMenu, advancedPowerShellMenu, g_ui.advancedPowerShellTitle);

    HMENU advancedLoggingMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced logging)");
    addTo(advancedLoggingMenu, ID_ADV_LOG_BUFFER_LINES, logBufferLine);
    addTo(advancedLoggingMenu, ID_ADV_LOG_APPEND_LOCK_WAIT_MS, logAppendLockWaitLine);
    endNestedMenu(advancedMenu, advancedLoggingMenu, g_ui.advancedLoggingTitle);

    HMENU advancedPngSaveMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced PNG saving)");
    addTo(advancedPngSaveMenu, ID_ADV_SAVE_PNG_REPLACE_RETRIES, savePngReplaceRetriesLine);
    addTo(advancedPngSaveMenu, ID_ADV_SAVE_PNG_REPLACE_RETRY_DELAY_MS, savePngReplaceRetryDelayLine);
    addTo(advancedPngSaveMenu, ID_ADV_KEEP_GDIPLUS_LOADED, g_ui.keepGdiPlusLoaded.c_str(), g_keepGdiPlusLoaded);
    endNestedMenu(advancedMenu, advancedPngSaveMenu, g_ui.advancedPngSaveTitle);

    HMENU advancedSingleInstanceMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced single instance)");
    addTo(advancedSingleInstanceMenu, ID_ADV_SINGLE_INSTANCE_SIGNAL_RETRIES, signalRetriesLine);
    addTo(advancedSingleInstanceMenu, ID_ADV_SINGLE_INSTANCE_SIGNAL_DELAY_MS, signalDelayLine);
    addTo(advancedSingleInstanceMenu, ID_ADV_SINGLE_INSTANCE_FAILURE_ACTION, singleInstanceLine);
    endNestedMenu(advancedMenu, advancedSingleInstanceMenu, g_ui.advancedSingleInstanceTitle);
    endSection(advancedMenu, g_ui.advancedTitle);

    if (showMenuAsDropdown)
        AppendMenuChecked(m, MF_SEPARATOR, 0, nullptr);
    addTo(m, ID_EXIT, g_ui.exitText.c_str());

    SetForegroundWindow(h);
    UINT cmd = static_cast<UINT>(TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,h,nullptr));
    DestroyMenu(m);

    switch(cmd)
    {
    case ID_USE_PS:
    {
        bool v = !UsePowerShell();
        IniWrite(L"Settings", L"UsePowerShell", v ? L"1" : L"0");
        Log(g_ui.usePowerShellSummary.c_str(), v, v ? g_ui.powerShellEnabledMode.c_str() : g_ui.comPreferredMode.c_str());
    }
    break;

    case ID_LIVE_TILE_MODE_AUTO:
    case ID_LIVE_TILE_MODE_REGISTRATION:
    case ID_LIVE_TILE_MODE_LIVE_TILE:
    {
        LiveTileUpdateMode newLiveTileMode = LiveTileUpdateMode::Auto;
        if (cmd == ID_LIVE_TILE_MODE_REGISTRATION)
            newLiveTileMode = LiveTileUpdateMode::Registration;
        else if (cmd == ID_LIVE_TILE_MODE_LIVE_TILE)
            newLiveTileMode = LiveTileUpdateMode::LiveTile;

        if (ConfiguredLiveTileUpdateMode() != newLiveTileMode)
        {
            bool wasPackaged = CurrentProcessHasPackageIdentity();
            bool relaunchOnSwitch = g_liveTileRelaunchOnSwitch.load();
            g_liveTileUpdateMode = static_cast<int>(newLiveTileMode);
            IniWrite(L"Settings", L"ExperimentalLiveTileUpdate", LiveTileUpdateModeIniValue(newLiveTileMode).c_str());
            Log(g_ui.liveTileUpdateSummary.c_str(), LiveTileUpdateModeSummaryText().c_str());

            bool shouldRunPackagedForNewMode = false;
            bool newModeHasFixedRuntime = LiveTileModeHasFixedRuntime(
                newLiveTileMode,
                ConfiguredManifestCompatibilityTarget(),
                shouldRunPackagedForNewMode);
            bool requestPackagedRelaunch = relaunchOnSwitch &&
                newModeHasFixedRuntime &&
                shouldRunPackagedForNewMode &&
                !wasPackaged;
            bool requestUnpackagedRelaunch = relaunchOnSwitch &&
                newModeHasFixedRuntime &&
                !shouldRunPackagedForNewMode &&
                wasPackaged;

            if (requestPackagedRelaunch)
            {
                // Enabling Live Tile from the unpackaged tray process must first
                // re-register the manifest/static assets for the new mode.
                // Otherwise the packaged instance starts before the Start icon has
                // been changed back to the Desktop/disabled static asset.
                g_relaunchAfterExit = 1;
                if (!QueueLiveTileModeReregistration())
                {
                    g_relaunchAfterExit = 0;
                    break;
                }
                LogText(g_ui.liveTileRelaunchToPackagedQueued);
                RequestGracefulShutdown(h);
            }
            else if (requestUnpackagedRelaunch)
            {
                // Disabling from a packaged instance cannot safely call
                // RegisterPackageByFullName while that same package process is
                // still running. Mark it pending, exit the package identity first,
                // then let the unpackaged relaunch do the registration.
                SetLiveTileModeReregistrationPending(true);
                g_relaunchAfterExit = 2;
                LogText(g_ui.liveTileRelaunchToUnpackagedQueued);
                RequestGracefulShutdown(h);
            }
            else
            {
                QueueLiveTileModeReregistration();
            }
        }
    }
    break;

    case ID_LIVE_TILE_RELAUNCH_ON_SWITCH:
    {
        g_liveTileRelaunchOnSwitch = !g_liveTileRelaunchOnSwitch;
        IniWrite(L"Settings", L"LiveTileRelaunchOnSwitch", g_liveTileRelaunchOnSwitch ? L"1" : L"0");
    }
    break;

    case ID_CLEAR_LIVE_TILE_ON_SHUTDOWN:
    {
        g_clearLiveTileOnShutdown = !g_clearLiveTileOnShutdown;
        IniWrite(L"Settings", L"ClearLiveTileOnShutdown", g_clearLiveTileOnShutdown ? L"1" : L"0");
    }
    break;

    case ID_WIN8_LIVE_TILE_BROKER_APP:
    {
        bool value = IniReadI(L"Settings", L"Win8LiveTileBrokerApp", 1) == 0;
        IniWrite(L"Settings", L"Win8LiveTileBrokerApp", value ? L"1" : L"0");
        Log(g_ui.win8LiveTileBrokerAppState.c_str(), value ? 1 : 0);
    }
    break;

    case ID_WIN8_LIVE_TILE_BACKGROUND_TASK:
    {
        bool value = IniReadI(L"Settings", L"Win8LiveTileBackgroundTask", 0) == 0;
        IniWrite(L"Settings", L"Win8LiveTileBackgroundTask", value ? L"1" : L"0");
        Log(g_ui.win8LiveTileBackgroundTaskState.c_str(), value ? 1 : 0);
    }
    break;

    case ID_WIN8_LIVE_TILE_OOP_HELPER:
    {
        bool value = IniReadI(L"Settings", L"Win8LiveTileOopHelper", 0) == 0;
        IniWrite(L"Settings", L"Win8LiveTileOopHelper", value ? L"1" : L"0");
        Log(g_ui.win8LiveTileOopHelperState.c_str(), value ? 1 : 0);
    }
    break;

    case ID_MANIFEST_REGENERATE:
    {
        if (!RegenerateAppxManifestFromConfigAndLog())
            break;

        std::wstring wp = GetWallpaper();
        if (wp.empty())
        {
            MessageBoxW(h,
                g_ui.currentWallpaperNotFound.c_str(),
                g_ui.generateNowTitle.c_str(),
                MB_OK | MB_ICONWARNING);
            break;
        }

        SetLiveTileModeReregistrationPending(true);
        QueueGenerate(wp, g_ui.manifestRegenerateReason);
    }
    break;

    case ID_TRAYICON:
    {
        g_tray = !g_tray;

        if (!g_tray)
        {
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationTrayIconHidden, NIIF_WARNING, true);
            Sleep(static_cast<DWORD>(ClampInt(g_trayIconHiddenDelayMs.load(), 0, 10000)));
            TrayRemove();
        }
        else
        {
            DWORD err = 0;
            if (!EnsureTrayIcon(&err))
            {
                LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
                g_tray = false;
            }
        }

        Log(g_ui.trayIconSummary.c_str(), StateEnabled(g_tray));
    }
    break;

    case ID_CONSOLE:
    {
        ApplyConsoleSetting(!g_console, true, true);
    }
    break;

    case ID_LOG:
    {
        g_logging = !g_logging;
        IniWrite(L"Settings",L"Logging", g_logging?L"1":L"0");
        Log(g_ui.loggingSummary.c_str(), StateOn(g_logging));
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
            LogText(g_ui.manualGenerationRequested);
            QueueGenerate(wp, g_ui.manualGenerateTriggered);
        }
    }
    break;

    case ID_MANIFEST_TARGET_WINDOWS10:
    case ID_MANIFEST_TARGET_WINDOWS81:
    case ID_MANIFEST_TARGET_WINDOWS8:
    {
        ManifestCompatibilityTarget newTarget = ManifestCompatibilityTarget::Windows10;
        if (cmd == ID_MANIFEST_TARGET_WINDOWS81)
            newTarget = ManifestCompatibilityTarget::Windows81;
        else if (cmd == ID_MANIFEST_TARGET_WINDOWS8)
            newTarget = ManifestCompatibilityTarget::Windows8;

        if (ConfiguredManifestCompatibilityTarget() == newTarget)
            break;

        g_manifestCompatibilityTarget = static_cast<int>(newTarget);
        IniWrite(L"Settings", L"AppxManifestTarget", ManifestCompatibilityTargetIniValue(newTarget).c_str());
        Log(g_ui.manifestTargetSummary.c_str(), ManifestCompatibilityTargetSummaryText().c_str());
        if (!RegenerateAppxManifestFromConfigAndLog())
            break;

        std::wstring wp = GetWallpaper();
        if (wp.empty())
        {
            MessageBoxW(h,
                g_ui.currentWallpaperNotFound.c_str(),
                g_ui.generateNowTitle.c_str(),
                MB_OK | MB_ICONWARNING);
            break;
        }

        SetLiveTileModeReregistrationPending(true);
        QueueGenerate(wp, g_ui.manifestRegenerateReason);
    }
    break;

    case ID_GENERATE_STARTUP:
    {
        g_generateOnStartup = !g_generateOnStartup;
        IniWrite(L"Settings", L"GenerateOnStartup", g_generateOnStartup ? L"1" : L"0");
        Log(g_ui.generateOnStartupSummary.c_str(), StateEnabled(g_generateOnStartup));
    }
    break;

    case ID_GENERATE_STARTUP_CHANGED_ONLY:
    {
        if (!g_generateOnStartup)
            break;
        g_generateOnStartupOnlyWhenChanged = !g_generateOnStartupOnlyWhenChanged;
        IniWrite(L"Settings", L"GenerateOnStartupOnlyWhenChanged", g_generateOnStartupOnlyWhenChanged ? L"1" : L"0");
        Log(g_ui.generateOnStartupOnlyWhenChangedSummary.c_str(), StateEnabled(g_generateOnStartupOnlyWhenChanged));
    }
    break;

    case ID_HIDE_DISABLED:
    {
        g_hideDisabled = !g_hideDisabled;
        IniWrite(L"Settings", L"HideDisabledEntries", g_hideDisabled ? L"1" : L"0");
        Log(g_ui.hideDisabledEntriesSummary.c_str(), StateEnabled(g_hideDisabled));
    }
    break;

    case ID_GENERATE_DESKTOP_ICON_DISABLED:
    {
        g_generateDesktopIconForDisabledEntries = !g_generateDesktopIconForDisabledEntries;
        IniWrite(L"Settings", L"GenerateDesktopIconForDisabledEntries", g_generateDesktopIconForDisabledEntries ? L"1" : L"0");
        Log(g_ui.generateDesktopIconForDisabledEntriesSummary.c_str(), StateEnabled(g_generateDesktopIconForDisabledEntries));
    }
    break;

    case ID_DELETE_DISABLED_ASSETS:
    {
        if (g_generateDesktopIconForDisabledEntries)
            break;
        g_deleteDisabledAssets = !g_deleteDisabledAssets;
        IniWrite(L"Settings", L"DeleteDisabledAssets", g_deleteDisabledAssets ? L"1" : L"0");
        Log(g_ui.deleteDisabledAssetsSummary.c_str(), StateEnabled(g_deleteDisabledAssets));
    }
    break;

    case ID_SCALE_AUTO:
    {
        g_generateScaleAuto = !g_generateScaleAuto;
        IniWrite(L"Settings", L"GenerateScaleAuto", g_generateScaleAuto ? L"1" : L"0");
        Log(g_ui.generateScaleAutoSummary.c_str(), StateEnabled(g_generateScaleAuto));
    }
    break;

    case ID_SCALE_100:
    case ID_SCALE_125:
    case ID_SCALE_150:
    case ID_SCALE_200:
    case ID_SCALE_400:
    {
        int scale = 100;
        std::atomic<bool>* setting = &g_generateScale100;
        const wchar_t* key = L"GenerateScale100";

        if (cmd == ID_SCALE_125) { scale = 125; setting = &g_generateScale125; key = L"GenerateScale125"; }
        else if (cmd == ID_SCALE_150) { scale = 150; setting = &g_generateScale150; key = L"GenerateScale150"; }
        else if (cmd == ID_SCALE_200) { scale = 200; setting = &g_generateScale200; key = L"GenerateScale200"; }
        else if (cmd == ID_SCALE_400) { scale = 400; setting = &g_generateScale400; key = L"GenerateScale400"; }

        bool value = !setting->load();
        *setting = value;
        IniWrite(L"Settings", key, value ? L"1" : L"0");
        Log(g_ui.generateScaleSummary.c_str(), scale, StateEnabled(value));
    }
    break;

    case ID_TILE_TEXT_ENABLED:
        ToggleTileTextBool(L"Enabled", 0);
        break;

    case ID_TILE_TEXT_EDIT:
    {
        std::wstring text = IniReadS(L"TileText", L"Text", L"");
        if (ShowTextInputDialog(h, g_ui.tileTextInputTitle, g_ui.tileTextInputPrompt, text, true) == IDOK)
        {
            IniWrite(L"TileText", L"Text", text.c_str());
            if (!TrimCopy(text).empty())
                IniWrite(L"TileText", L"Enabled", L"1");
            Log(g_ui.tileTextEdited.c_str(), text.c_str());
        }
    }
    break;

    case ID_TILE_TEXT_CLEAR:
        IniWrite(L"TileText", L"Text", L"");
        LogText(g_ui.tileTextCleared);
        break;

    case ID_TILE_TEXT_SECONDARY_EDIT:
    {
        std::wstring text = IniReadS(L"TileText", L"SecondaryText", L"");
        if (ShowTextInputDialog(h, g_ui.tileTextSecondaryInputTitle, g_ui.tileTextSecondaryInputPrompt, text, true) == IDOK)
        {
            IniWrite(L"TileText", L"SecondaryText", text.c_str());
            if (!TrimCopy(text).empty())
                IniWrite(L"TileText", L"Enabled", L"1");
            Log(g_ui.tileTextEdited.c_str(), text.c_str());
        }
    }
    break;

    case ID_TILE_TEXT_SECONDARY_CLEAR:
        IniWrite(L"TileText", L"SecondaryText", L"");
        LogText(g_ui.tileTextCleared);
        break;

    case ID_TILE_TEXT_BADGE_EDIT:
    {
        std::wstring text = IniReadS(L"TileText", L"BadgeText", L"");
        if (ShowTextInputDialog(h, g_ui.tileTextBadgeInputTitle, g_ui.tileTextBadgeInputPrompt, text, true) == IDOK)
        {
            IniWrite(L"TileText", L"BadgeText", text.c_str());
            if (!TrimCopy(text).empty())
                IniWrite(L"TileText", L"Enabled", L"1");
            Log(g_ui.tileTextEdited.c_str(), text.c_str());
        }
    }
    break;

    case ID_TILE_TEXT_BADGE_CLEAR:
        IniWrite(L"TileText", L"BadgeText", L"");
        LogText(g_ui.tileTextCleared);
        break;

    case ID_TILE_TEXT_H_ALIGN:
    {
        const wchar_t* next = NextTileTextHorizontalAlign(IniReadS(L"TileText", L"HorizontalAlign", L"Center"));
        IniWrite(L"TileText", L"HorizontalAlign", next);
        Log(g_ui.tileTextHorizontalAlign.c_str(), next);
    }
    break;

    case ID_TILE_TEXT_V_ALIGN:
    {
        const wchar_t* next = NextTileTextVerticalAlign(IniReadS(L"TileText", L"VerticalAlign", L"Top"));
        IniWrite(L"TileText", L"VerticalAlign", next);
        Log(g_ui.tileTextVerticalAlign.c_str(), next);
    }
    break;

    case ID_TILE_TEXT_BADGE_H_ALIGN:
    {
        const wchar_t* next = NextTileTextHorizontalAlign(IniReadS(L"TileText", L"BadgeHorizontalAlign", L"Right"));
        IniWrite(L"TileText", L"BadgeHorizontalAlign", next);
        Log(g_ui.tileTextBadgeHorizontalAlign.c_str(), next);
    }
    break;

    case ID_TILE_TEXT_BADGE_V_ALIGN:
    {
        const wchar_t* next = NextTileTextVerticalAlign(IniReadS(L"TileText", L"BadgeVerticalAlign", L"Top"));
        IniWrite(L"TileText", L"BadgeVerticalAlign", next);
        Log(g_ui.tileTextBadgeVerticalAlign.c_str(), next);
    }
    break;

    case ID_TILE_TEXT_FONT:
        PromptTileTextStringSetting(h, L"Font", g_ui.tileTextFontInputTitle, g_ui.tileTextFontInputPrompt, L"Segoe UI");
        break;

    case ID_TILE_TEXT_FONT_SIZE:
        CycleTileTextFontSize(L"FontSize", 18, g_ui.tileTextFontSize);
        break;

    case ID_TILE_TEXT_SECONDARY_FONT_SIZE:
        CycleTileTextFontSize(L"SecondaryFontSize", 12, g_ui.tileTextSecondaryFontSize);
        break;

    case ID_TILE_TEXT_BADGE_FONT_SIZE:
        CycleTileTextFontSize(L"BadgeFontSize", 40, g_ui.tileTextBadgeFontSize);
        break;

    case ID_TILE_TEXT_BOLD:
        ToggleTileTextBool(L"Bold", 0);
        break;

    case ID_TILE_TEXT_SECONDARY_BOLD:
        ToggleTileTextBool(L"SecondaryBold", 0);
        break;

    case ID_TILE_TEXT_BADGE_BOLD:
        ToggleTileTextBool(L"BadgeBold", 0);
        break;

    case ID_TILE_TEXT_ITALIC:
        ToggleTileTextBool(L"Italic", 0);
        break;

    case ID_TILE_TEXT_COLOR:
        PromptTileTextStringSetting(h, L"Color", g_ui.tileTextColorInputTitle, g_ui.tileTextColorInputPrompt, L"#FFFFFFFF");
        break;

    case ID_TILE_TEXT_SECONDARY_COLOR:
        PromptTileTextStringSetting(h, L"SecondaryColor", g_ui.tileTextSecondaryColorInputTitle, g_ui.tileTextSecondaryColorInputPrompt, L"#CCFFFFFF");
        break;

    case ID_TILE_TEXT_BADGE_COLOR:
        PromptTileTextStringSetting(h, L"BadgeColor", g_ui.tileTextBadgeColorInputTitle, g_ui.tileTextBadgeColorInputPrompt, L"#FFFFFFFF");
        break;

    case ID_TILE_TEXT_SHADOW:
        ToggleTileTextBool(L"Shadow", 1);
        break;

    case ID_TILE_TEXT_SHADOW_COLOR:
        PromptTileTextStringSetting(h, L"ShadowColor", g_ui.tileTextShadowColorInputTitle, g_ui.tileTextShadowColorInputPrompt, L"#AA000000");
        break;

    case ID_TILE_TEXT_MARGIN_X:
        PromptTileTextIntSetting(h, L"MarginX", 8, 0, 512, g_ui.tileTextMarginXInputTitle, g_ui.tileTextMarginXInputPrompt, g_ui.tileTextMarginX);
        break;

    case ID_TILE_TEXT_MARGIN_Y:
        PromptTileTextIntSetting(h, L"MarginY", 10, 0, 512, g_ui.tileTextMarginYInputTitle, g_ui.tileTextMarginYInputPrompt, g_ui.tileTextMarginY);
        break;

    case ID_TILE_TEXT_LINE_GAP:
        PromptTileTextIntSetting(h, L"LineGap", 2, 0, 128, g_ui.tileTextLineGapInputTitle, g_ui.tileTextLineGapInputPrompt, g_ui.tileTextLineGap);
        break;

    case ID_TILE_TEXT_MAX_SECONDARY_LINES:
        PromptTileTextIntSetting(h, L"MaxSecondaryLines", 2, 0, 32, g_ui.tileTextMaxSecondaryLinesInputTitle, g_ui.tileTextMaxSecondaryLinesInputPrompt, g_ui.tileTextMaxSecondaryLines);
        break;

    case ID_TILE_TEXT_APPLY_MEDIUM:
        ToggleTileTextBool(L"ApplyToMediumTile", 1);
        break;

    case ID_TILE_TEXT_APPLY_WIDE:
        ToggleTileTextBool(L"ApplyToWideTile", 1);
        break;

    case ID_TILE_TEXT_APPLY_LARGE:
        ToggleTileTextBool(L"ApplyToLargeTile", 1);
        break;

    case ID_TILE_TEXT_APPLY_SMALL:
        ToggleTileTextBool(L"ApplyToSmallTile", 0);
        break;

    case ID_TILE_TEXT_APPLY_LOGOS:
        ToggleTileTextBool(L"ApplyToLogos", 0);
        break;

    case ID_WALLPAPER_DECODE_LOW_MEMORY:
        ToggleSettingBool(L"WallpaperDecodeLowMemory", 1);
        break;

    case ID_WALLPAPER_DECODE_MAX_LONG_EDGE:
        CycleSettingIntPreset(L"WallpaperDecodeMaxLongEdge", 0, 0, 32768, { 0, 1024, 1920, 2560, 3840, 4096, 7680, 8192, 16384, 32768 });
        break;

    case ID_GENERATED_ASSET_CACHE:
    {
        bool value = !GeneratedAssetCacheEnabled();
        IniWrite(L"Settings", L"GeneratedAssetCache", value ? L"1" : L"0");
        Log(g_ui.generatedAssetCacheSummary.c_str(), StateEnabled(value), GeneratedAssetCacheMaxEntries());
    }
    break;

    case ID_GENERATED_ASSET_PRECACHE:
    {
        if (!GeneratedAssetCacheEnabled() || GeneratedAssetCacheMaxEntries() <= 0)
            break;

        bool value = IniReadI(L"Settings", L"GeneratedAssetPrecache", 1) == 0;
        IniWrite(L"Settings", L"GeneratedAssetPrecache", value ? L"1" : L"0");
        Log(g_ui.generatedAssetPrecacheSummary.c_str(), StateEnabled(value), GeneratedAssetPrecacheMaxFiles());
    }
    break;

    case ID_GENERATED_ASSET_CACHE_MAX_ENTRIES:
        CycleSettingIntPreset(L"GeneratedAssetCacheMaxEntries", 64, 0, 4096, { 0, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 });
        break;

    case ID_GENERATED_ASSET_PRECACHE_MAX_FILES:
        if (GeneratedAssetCacheEnabled() && GeneratedAssetCacheMaxEntries() > 0)
            CycleSettingIntPreset(L"GeneratedAssetPrecacheMaxFiles", 16, 0, 256, { 0, 4, 8, 16, 32, 64, 128, 256 });
        break;

    case ID_ADV_POLL_INTERVAL:
        CycleSettingIntPreset(L"PollIntervalMs", 2000, 250, 60000, { 250, 500, 1000, 2000, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_CONFIRM_MS:
        CycleSettingIntPreset(L"ConfirmMs", 800, 0, 10000, { 0, 250, 500, 800, 1000, 1500, 2500, 5000, 10000 });
        break;

    case ID_ADV_DEBOUNCE_MIN_MS:
        CycleSettingIntPreset(L"DebounceMinMs", 1200, 0, 60000, { 0, 500, 800, 1200, 2000, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_POLL_INITIAL_BYPASS_MS:
        CycleSettingIntPreset(L"PollInitialDebounceBypassMs", 5000, 0, 60000, { 0, 1000, 2500, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_INI_CACHE_REFRESH_MS:
        CycleSettingIntPreset(L"IniCacheRefreshMs", 250, 0, 60000, { 0, 100, 250, 500, 1000, 2500, 5000, 10000, 60000 });
        break;

    case ID_ADV_WALLPAPER_DETECTION_LOG_REPEAT_MS:
        CycleSettingIntPreset(L"WallpaperDetectionLogRepeatMs", 60000, 0, 3600000, { 0, 10000, 30000, 60000, 300000, 600000, 1800000, 3600000 });
        break;

    case ID_ADV_NOTIFICATION_TIMEOUT_MS:
        CycleSettingIntPreset(L"NotificationTimeoutMs", 4000, 1000, 60000, { 1000, 2000, 4000, 8000, 15000, 30000, 60000 });
        break;

    case ID_ADV_SHUTDOWN_INITIAL_MS:
        CycleSettingIntPreset(L"ShutdownInitialNoticeMs", 1000, 250, 60000, { 250, 500, 1000, 2000, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_SHUTDOWN_REPEAT_MS:
        CycleSettingIntPreset(L"ShutdownRepeatNoticeMs", 5000, 250, 60000, { 250, 1000, 2500, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_SINGLE_INSTANCE_SIGNAL_RETRIES:
        CycleSettingIntPreset(L"SingleInstanceSignalRetries", 50, 0, 200, { 0, 1, 5, 10, 25, 50, 100, 200 });
        break;

    case ID_ADV_SINGLE_INSTANCE_SIGNAL_DELAY_MS:
        CycleSettingIntPreset(L"SingleInstanceSignalDelayMs", 100, 0, 5000, { 0, 25, 50, 100, 250, 500, 1000, 2500, 5000 });
        break;

    case ID_ADV_COM_REGISTRATION_TIMEOUT_MS:
        CycleSettingIntPreset(L"ComRegistrationTimeoutMs", 120000, 0, 600000, { 0, 30000, 60000, 120000, 180000, 300000, 600000 });
        break;

    case ID_ADV_COM_REGISTRATION_CANCEL_WAIT_MS:
        CycleSettingIntPreset(L"ComRegistrationCancelWaitMs", 5000, 0, 60000, { 0, 1000, 2500, 5000, 10000, 30000, 60000 });
        break;

    case ID_COM_REGISTRATION_USE_HELPER:
        ToggleSettingBool(L"ComRegistrationUseHelper", 0);
        break;

    case ID_ADV_TRAY_ICON_HIDDEN_DELAY_MS:
        CycleSettingIntPreset(L"TrayIconHiddenDelayMs", 750, 0, 10000, { 0, 250, 500, 750, 1000, 2000, 5000, 10000 });
        break;

    case ID_ADV_LOG_BUFFER_LINES:
        CycleSettingIntPreset(L"LogBufferLines", 1024, 1, 100000, { 1, 100, 500, 1024, 2500, 5000, 10000, 50000, 100000 });
        break;

    case ID_ADV_LOG_APPEND_LOCK_WAIT_MS:
        CycleSettingIntPreset(L"LogAppendLockWaitMs", 1000, 0, 60000, { 0, 100, 250, 500, 1000, 2500, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_TRIM_WORKING_SET_ON_IDLE:
    {
        g_trimWorkingSetOnIdle = !g_trimWorkingSetOnIdle;
        IniWrite(L"Settings", L"TrimWorkingSetOnIdle", g_trimWorkingSetOnIdle ? L"1" : L"0");
        Log(g_ui.trimWorkingSetOnIdleSummary.c_str(), StateEnabled(g_trimWorkingSetOnIdle));
        if (g_trimWorkingSetOnIdle)
            TrimIdleWorkingSetIfConfigured();
    }
    break;

    case ID_ADV_COMPACT_CRT_HEAP_ON_IDLE:
    {
        g_compactCrtHeapOnIdle = !g_compactCrtHeapOnIdle;
        IniWrite(L"Settings", L"CompactCrtHeapOnIdle", g_compactCrtHeapOnIdle ? L"1" : L"0");
        Log(g_ui.compactCrtHeapOnIdleSummary.c_str(), StateEnabled(g_compactCrtHeapOnIdle));
        TrimIdleWorkingSetIfConfigured();
    }
    break;

    case ID_ADV_KEEP_GDIPLUS_LOADED:
    {
        g_keepGdiPlusLoaded = !g_keepGdiPlusLoaded;
        IniWrite(L"Settings", L"KeepGdiPlusLoaded", g_keepGdiPlusLoaded ? L"1" : L"0");
        Log(g_ui.keepGdiPlusLoadedSummary.c_str(), StateEnabled(g_keepGdiPlusLoaded));
        ReleaseIdleGdiPlusIfAllowed();
        TrimIdleWorkingSetIfConfigured();
    }
    break;

    case ID_ADV_POWER_SHELL_EXE:
        ChoosePowerShellExe(h);
        break;

    case ID_ADV_POWER_SHELL_POLL_MS:
        CycleSettingIntPreset(L"PowerShellPollMs", 50, 10, 1000, { 10, 25, 50, 100, 250, 500, 1000 });
        break;

    case ID_ADV_POWER_SHELL_TIMEOUT_MS:
        CycleSettingIntPreset(L"PowerShellTimeoutMs", 120000, 0, 600000, { 0, 30000, 60000, 120000, 180000, 300000, 600000 });
        break;

    case ID_ADV_POWER_SHELL_TERMINATE_WAIT_MS:
        CycleSettingIntPreset(L"PowerShellTerminateWaitMs", 5000, 0, 60000, { 0, 1000, 2500, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_POWER_SHELL_OUTPUT_LIMIT:
        CycleSettingIntPreset(L"PowerShellOutputMessageBoxChars", 6000, 0, 60000, { 0, 1000, 3000, 6000, 10000, 30000, 60000 });
        g_powerShellOutputMessageBoxChars = IniReadClampedI(L"Settings", L"PowerShellOutputMessageBoxChars", 6000, 0, 60000);
        break;

    case ID_ADV_REGISTRATION_UNRESOLVED_BLOCK_MS:
        CycleSettingIntPreset(L"RegistrationUnresolvedBlockMs", 300000, 0, 3600000, { 0, 30000, 60000, 120000, 300000, 600000, 1800000, 3600000 });
        break;

    case ID_ADV_SAVE_PNG_REPLACE_RETRIES:
        CycleSettingIntPreset(L"SavePngReplaceRetries", 3, 0, 20, { 0, 1, 2, 3, 5, 10, 20 });
        break;

    case ID_ADV_SAVE_PNG_REPLACE_RETRY_DELAY_MS:
        CycleSettingIntPreset(L"SavePngReplaceRetryDelayMs", 75, 0, 5000, { 0, 25, 50, 75, 100, 250, 500, 1000, 2500, 5000 });
        break;

    case ID_ADV_SINGLE_INSTANCE_FAILURE_ACTION:
        CycleSingleInstanceFailureActionSetting();
        break;

    case ID_SHOW_MENU_DROPDOWN:
    {
        g_showMenuAsDropdown = !g_showMenuAsDropdown;
        IniWrite(L"Settings", L"ShowMenuAsDropdown", g_showMenuAsDropdown ? L"1" : L"0");
        Log(g_ui.showMenuAsDropdownSummary.c_str(), StateEnabled(g_showMenuAsDropdown));
    }
    break;

    case ID_NOTIF_ENABLED:
    {
        g_notificationsEnabled = !g_notificationsEnabled;
        IniWrite(L"Settings", L"NotificationsEnabled", g_notificationsEnabled ? L"1" : L"0");
        Log(g_ui.notificationsSummary.c_str(), StateEnabled(g_notificationsEnabled), StateOn(g_notifyOnStart), StateOn(g_notifyOnSuccess), StateOn(g_notifyOnFailure), StateOn(g_notifyOnBusy), StateOn(g_notifyOnAlreadyRunning), StateOn(g_notifyOnSingleInstanceFailure));
    }
    break;

    case ID_NOTIFY_START:
    {
        g_notifyOnStart = !g_notifyOnStart;
        IniWrite(L"Settings", L"NotifyOnStart", g_notifyOnStart ? L"1" : L"0");
        Log(g_ui.notifyOnStartState.c_str(), (int)g_notifyOnStart);
    }
    break;

    case ID_NOTIFY_SUCCESS:
    {
        g_notifyOnSuccess = !g_notifyOnSuccess;
        IniWrite(L"Settings", L"NotifyOnSuccess", g_notifyOnSuccess ? L"1" : L"0");
        Log(g_ui.notifyOnSuccessState.c_str(), (int)g_notifyOnSuccess);
    }
    break;

    case ID_NOTIFY_FAILURE:
    {
        g_notifyOnFailure = !g_notifyOnFailure;
        IniWrite(L"Settings", L"NotifyOnFailure", g_notifyOnFailure ? L"1" : L"0");
        Log(g_ui.notifyOnFailureState.c_str(), (int)g_notifyOnFailure);
    }
    break;

    case ID_NOTIFY_BUSY:
    {
        g_notifyOnBusy = !g_notifyOnBusy;
        IniWrite(L"Settings", L"NotifyOnBusy", g_notifyOnBusy ? L"1" : L"0");
        Log(g_ui.notifyOnBusyState.c_str(), (int)g_notifyOnBusy);
    }
    break;

    case ID_NOTIFY_ALREADY_RUNNING:
    {
        g_notifyOnAlreadyRunning = !g_notifyOnAlreadyRunning;
        IniWrite(L"Settings", L"NotifyOnAlreadyRunning", g_notifyOnAlreadyRunning ? L"1" : L"0");
        Log(g_ui.notifyOnAlreadyRunningState.c_str(), (int)g_notifyOnAlreadyRunning);
    }
    break;

    case ID_NOTIFY_SINGLE_INSTANCE_FAILURE:
    {
        g_notifyOnSingleInstanceFailure = !g_notifyOnSingleInstanceFailure;
        IniWrite(L"Settings", L"NotifyOnSingleInstanceFailure", g_notifyOnSingleInstanceFailure ? L"1" : L"0");
        Log(g_ui.notifyOnSingleInstanceFailureState.c_str(), (int)g_notifyOnSingleInstanceFailure);
    }
    break;

    case ID_SHOW_PSLOG:
        ShowPSLog(h);
        break;

    case ID_EXIT:
        RequestGracefulShutdown(h);
        break;

    case ID_LISTEN_WP:
    {
        g_listenWallpaper = !g_listenWallpaper;
        IniWrite(L"Settings", L"ListenWallpaper", g_listenWallpaper ? L"1" : L"0");
        Log(g_ui.listenWallpaperState.c_str(), (int)g_listenWallpaper);
    }
    break;

    case ID_LISTEN_FIT:
    {
        g_listenFit = !g_listenFit;
        IniWrite(L"Settings", L"ListenFit", g_listenFit ? L"1" : L"0");
        Log(g_ui.listenFitState.c_str(), (int)g_listenFit);
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
                LogText(g_ui.listenFitAutoDisabled);
            }
        }
        Log(g_ui.disableFittingState.c_str(), (int)g_disableFitting);
    }
    break;

    case ID_WP_METHOD_TRANSCODED_CACHE:
    case ID_WP_METHOD_SYSTEM_PARAMETERS:
    case ID_WP_METHOD_DESKTOP_WALLPAPER_COM:
    case ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE:
    case ID_WP_METHOD_AUTO:
    {
        WallpaperDetectionMethod method = WallpaperDetectionMethod::TranscodedImageCache;
        if (cmd == ID_WP_METHOD_SYSTEM_PARAMETERS)
            method = WallpaperDetectionMethod::SystemParametersInfo;
        else if (cmd == ID_WP_METHOD_DESKTOP_WALLPAPER_COM)
            method = WallpaperDetectionMethod::DesktopWallpaperCOM;
        else if (cmd == ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE)
            method = WallpaperDetectionMethod::TranscodedWallpaperFile;
        else if (cmd == ID_WP_METHOD_AUTO)
            method = WallpaperDetectionMethod::Auto;

        const wchar_t* methodName = WallpaperDetectionMethodName(method);
        IniWrite(L"Settings", L"WallpaperDetectionMethod", methodName);
        Log(g_ui.wallpaperDetectionMethodLabel.c_str(), methodName);
    }
    break;

    case ID_WP_METHOD_FALLBACK:
    {
        g_wallpaperDetectionFallbackOnInvalid = !g_wallpaperDetectionFallbackOnInvalid;
        IniWrite(L"Settings", L"WallpaperDetectionFallbackOnInvalid", g_wallpaperDetectionFallbackOnInvalid ? L"1" : L"0");
        Log(g_ui.wallpaperDetectionFallbackSummary.c_str(), StateEnabled(g_wallpaperDetectionFallbackOnInvalid));
    }
    break;

    case ID_LIVE_WALLPAPER_CAPTURE:
    {
        bool value = IniReadI(L"Settings", L"LiveWallpaperCapture", 1) == 0;
        IniWrite(L"Settings", L"LiveWallpaperCapture", value ? L"1" : L"0");
        Log(g_ui.liveWallpaperCaptureSummary.c_str(),
            StateEnabled(value),
            IniReadClampedI(L"Settings", L"LiveWallpaperCaptureDelayMs", 2500, 0, 30000),
            LiveWallpaperRefreshDisplayText(IniReadClampedI(L"Settings", L"LiveWallpaperCaptureRefreshMs", 10000, 0, 3600000)).c_str());
    }
    break;

    case ID_LIVE_WALLPAPER_CAPTURE_SCREEN_FALLBACK:
    {
        bool value = IniReadI(L"Settings", L"LiveWallpaperCaptureScreenFallback", 0) == 0;
        IniWrite(L"Settings", L"LiveWallpaperCaptureScreenFallback", value ? L"1" : L"0");
        Log(g_ui.liveWallpaperCaptureScreenFallbackState.c_str(), value ? 1 : 0);
    }
    break;

    case ID_LIVE_WALLPAPER_CAPTURE_DELAY:
    {
        int current = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureDelayMs", 2500, 0, 30000);
        int next = NextLiveWallpaperCaptureDelayMs(current);
        IniWrite(L"Settings", L"LiveWallpaperCaptureDelayMs", std::to_wstring(next).c_str());
        Log(g_ui.liveWallpaperCaptureDelayState.c_str(), next);
    }
    break;

    case ID_LIVE_WALLPAPER_CAPTURE_REFRESH:
    {
        int current = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureRefreshMs", 10000, 0, 3600000);
        int next = NextLiveWallpaperCaptureRefreshMs(current);
        IniWrite(L"Settings", L"LiveWallpaperCaptureRefreshMs", std::to_wstring(next).c_str());
        Log(g_ui.liveWallpaperCaptureRefreshState.c_str(), next);
    }
    break;

    case ID_LIVE_WALLPAPER_CAPTURE_STARTUP_REFRESH:
    {
        int current = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshMs", 1000, 0, 3600000);
        int next = NextLiveWallpaperCaptureStartupRefreshMs(current);
        IniWrite(L"Settings", L"LiveWallpaperCaptureStartupRefreshMs", std::to_wstring(next).c_str());
        Log(g_ui.liveWallpaperCaptureStartupRefreshState.c_str(), next);
    }
    break;

    case ID_LIVE_WALLPAPER_CAPTURE_STARTUP_REFRESH_DURATION:
    {
        int current = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshDurationMs", 0, 0, 3600000);
        int next = NextLiveWallpaperCaptureStartupRefreshDurationMs(current);
        IniWrite(L"Settings", L"LiveWallpaperCaptureStartupRefreshDurationMs", std::to_wstring(next).c_str());
        Log(g_ui.liveWallpaperCaptureStartupRefreshDurationState.c_str(), next);
    }
    break;

    case ID_LOGFILE:
    {
        std::vector<wchar_t> file(32768);
        wcsncpy_s(file.data(), file.size(), GetLogPathCopy().c_str(), _TRUNCATE);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = h;
        std::wstring filter = g_ui.logFileFilter;
        std::replace(filter.begin(), filter.end(), L'|', L'\0');
        filter.push_back(L'\0');
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile = file.data();
        ofn.nMaxFile = (DWORD)file.size();
        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = L"log";
        if (GetSaveFileNameW(&ofn))
        {
            std::wstring newLogPath = file.data();
            SetLogPathCopy(newLogPath);
            IniWrite(L"Settings", L"LogPath", newLogPath.c_str());

            Log(g_ui.logPathChanged.c_str(), newLogPath.c_str());
            LogText(g_ui.futureLogEntriesNewPath);
        }
    }
    break;

    case ID_LOG_RENAME:
    {
        std::wstring newPath = GetLogPathCopy();
        if (ShowRenameDialog(h, newPath) == IDOK)
        {
            SetLogPathCopy(newPath);
            IniWrite(L"Settings", L"LogPath", newPath.c_str());
            Log(g_ui.logPathRenamed.c_str(), newPath.c_str());
            LogText(g_ui.futureLogEntriesRenamedPath);
        }
    }
    break;

    case ID_LOG_OPEN:
    {
        std::wstring logPath = GetLogPathCopy();
        if (!logPath.empty())
        {
            std::wstring param = L"/select,\"" + logPath + L"\"";
            HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOW);
            if ((INT_PTR)result <= 32)
                LogWin32Failure(L"ShellExecuteW(explorer.exe)", static_cast<DWORD>((INT_PTR)result));
        }
    }
    break;

    case ID_LOG_RESET:
    {
        std::wstring newLogPath = DefaultLogPath();
        SetLogPathCopy(newLogPath);
        IniWrite(L"Settings", L"LogPath", L"");
        Log(g_ui.logPathReset.c_str(), newLogPath.c_str());
        LogText(g_ui.futureLogEntriesDefaultPath);
    }
    break;

    default:
        if (cmd >= ID_A1 && cmd < ID_A1 + _countof(keys)){
            int i = static_cast<int>(cmd) - ID_A1;
            const wchar_t* k = keys[i];
            bool nv = IniReadI(L"Assets",k,0) == 0;
            IniWrite(L"Assets",k,nv?L"1":L"0");
            Log(g_ui.assetToggleState.c_str(), k, StateOn(nv));
        }
        break;
    }
}

static void HandleExistingInstanceMessageBoxRequest()
{
    if (!g_notificationsEnabled || !g_notifyOnAlreadyRunning)
        return;

    MessageBoxW(g_hwnd ? g_hwnd : nullptr,
        g_ui.notificationAlreadyRunning.c_str(),
        g_ui.notificationTitle.c_str(),
        MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
}

static void HandleExistingInstanceRequest()
{
    DWORD err = 0;
    if (!EnsureTrayIcon(&err))
    {
        LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
        if (g_notificationsEnabled && g_notifyOnAlreadyRunning)
            MessageBoxW(nullptr, g_ui.notificationAlreadyRunning.c_str(), g_ui.notificationTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!g_notificationsEnabled || !g_notifyOnAlreadyRunning)
        return;

    bool trayPersistsAfterRestart = IniReadI(L"Settings", L"TrayIcon", 1) != 0;
    const std::wstring& message = trayPersistsAfterRestart
        ? g_ui.notificationAlreadyRunningTrayPersistent
        : g_ui.notificationAlreadyRunningTraySession;
    NotifyTrayBalloon(g_ui.notificationTitle, message, static_cast<DWORD>(trayPersistsAfterRestart ? NIIF_INFO : NIIF_WARNING), true);
}

static int HandleSingleInstanceMutexFailure(DWORD err)
{
    ErrorAction action = CurrentSingleInstanceFailureAction();
    if (action == ErrorAction::Ignore)
        return 0;

    std::wstring errText = Win32ErrorString(err);
    const wchar_t* actionName = ErrorActionName(action);
    Log(g_ui.singleInstanceMutexFailure.c_str(), err, errText.c_str(), actionName);

    if (g_notificationsEnabled && g_notifyOnSingleInstanceFailure)
    {
        if (action == ErrorAction::Warn && TrayIconPresent())
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationSingleInstanceFailure, NIIF_WARNING, true);
        else
            MessageBoxW(g_hwnd, g_ui.notificationSingleInstanceFailure.c_str(), g_ui.notificationTitle.c_str(), MB_OK | MB_ICONWARNING);
    }

    if (action == ErrorAction::Exit)
        return 1;

    if (action == ErrorAction::Crash)
        RaiseFailFastException(nullptr, nullptr, 0);

    return 0;
}

