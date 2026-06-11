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
    ID_LIVE_TILE_TEMPLATE_ADAPTIVE,
    ID_LIVE_TILE_TEMPLATE_WINDOWS81_PRESET,
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
    ID_MANIFEST_EDIT_IDENTITY_NAME,
    ID_MANIFEST_EDIT_PUBLISHER,
    ID_MANIFEST_EDIT_PACKAGE_VERSION,
    ID_MANIFEST_EDIT_DISPLAY_NAME,
    ID_MANIFEST_EDIT_PUBLISHER_DISPLAY_NAME,
    ID_MANIFEST_EDIT_DESCRIPTION,
    ID_MANIFEST_EDIT_EXECUTABLE,
    ID_MANIFEST_EDIT_APPLICATION_ID,
    ID_MANIFEST_EDIT_TARGET_DEVICE_FAMILY_NAME,
    ID_MANIFEST_EDIT_MIN_VERSION,
    ID_MANIFEST_EDIT_MAX_VERSION_TESTED,
    ID_MANIFEST_EDIT_RESOURCE_LANGUAGE,
    ID_MANIFEST_EDIT_PROPERTIES_LOGO,
    ID_MANIFEST_EDIT_SQUARE150_LOGO,
    ID_MANIFEST_EDIT_SQUARE44_LOGO,
    ID_MANIFEST_EDIT_SQUARE30_LOGO,
    ID_MANIFEST_EDIT_SQUARE71_LOGO,
    ID_MANIFEST_EDIT_WIDE310_LOGO,
    ID_MANIFEST_EDIT_SQUARE310_LOGO,
    ID_MANIFEST_EDIT_BACKGROUND_COLOR,
    ID_MANIFEST_EDIT_SHOW_NAME_ON_TILES,
    ID_MANIFEST_EDIT_ENTRY_POINT,
    ID_MANIFEST_EDIT_RESTRICTED_CAPABILITY,
    ID_MANIFEST_EDIT_SPLASH_SCREEN_IMAGE,
    ID_MANIFEST_EDIT_LIVE_TILE_BROKER_EXECUTABLE,
    ID_MANIFEST_EDIT_LIVE_TILE_BROKER_ENTRY_POINT,
    ID_MANIFEST_EDIT_LIVE_TILE_OOP_SERVER_NAME,
    ID_MANIFEST_EDIT_LIVE_TILE_OOP_RUNTIME_CLASS_ID,
    ID_MANIFEST_EDIT_LIVE_TILE_BACKGROUND_TASK_DLL,
    ID_MANIFEST_EDIT_LIVE_TILE_BACKGROUND_TASK_CLASS_ID,
    ID_MANIFEST_EDIT_LIVE_TILE_BACKGROUND_TASK_NAME,
    ID_TILE_TEXT_ENABLED,
    ID_TILE_TEXT_EDIT,
    ID_TILE_TEXT_CLEAR,
    ID_TILE_TEXT_SECONDARY_EDIT,
    ID_TILE_TEXT_SECONDARY_CLEAR,
    ID_TILE_TEXT_BADGE_EDIT,
    ID_TILE_TEXT_BADGE_CLEAR,
    ID_TILE_TEXT_APPLY_MEDIUM,
    ID_TILE_TEXT_APPLY_WIDE,
    ID_TILE_TEXT_APPLY_LARGE,

    // Advanced configurable settings
    ID_ADV_POLL_INTERVAL,
    ID_ADV_CONFIRM_MS,
    ID_ADV_DEBOUNCE_MIN_MS,
    ID_ADV_POLL_INITIAL_BYPASS_MS,
    ID_ADV_INI_CACHE_REFRESH_MS,
    ID_ADV_INI_WRITE_LOCK_WAIT_MS,
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
    ID_ADV_IDLE_TRIM_INTERVAL_MS,
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

static constexpr const wchar_t* kAssetMenuKeys[] = {
    L"StoreLogo", L"MediumTile", L"Square44x44Logo", L"Square30x30Logo",
    L"SmallTile", L"WideTile", L"LargeTile", L"SplashScreen"
};

static void DispatchTrayCommand(HWND h, UINT cmd);

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
    if (!IniWrite(L"Settings", key, std::to_wstring(value).c_str()))
        return;
    ReloadRuntimeSettingsFromIni();
    Log(L"[i] Setting changed: [%s] %s = %d", L"Settings", key ? key : L"", value);
}

static void WriteSettingBool(const wchar_t* key, bool value)
{
    if (!IniWrite(L"Settings", key, value ? L"1" : L"0"))
        return;
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

static void WriteTileTextBool(const wchar_t* key, bool value)
{
    if (!IniWrite(L"TileText", key, value ? L"1" : L"0"))
        return;
    Log(L"[i] Setting changed: [%s] %s = %s", L"TileText", key ? key : L"", StateEnabled(value));
}

static void ToggleTileTextBool(const wchar_t* key, int fallback)
{
    bool value = IniReadI(L"TileText", key, fallback) == 0;
    WriteTileTextBool(key, value);
}

static void RegenerateManifestAndQueueGeneration(HWND owner)
{
    if (!RegenerateAppxManifestFromConfigAndLog())
        return;

    std::wstring wp = GetWallpaper();
    if (wp.empty())
    {
        MessageBoxW(owner,
            g_ui.currentWallpaperNotFound.c_str(),
            g_ui.generateNowTitle.c_str(),
            MB_OK | MB_ICONWARNING);
        return;
    }

    SetLiveTileModeReregistrationPending(true);
    QueueGenerate(wp, g_ui.manifestRegenerateReason);
}

static void PromptManifestStringSetting(
    HWND owner,
    const wchar_t* key,
    const std::wstring& currentValue)
{
    std::wstring value = IniReadS(L"Settings", key, currentValue.c_str());
    if (value.empty())
        value = currentValue;

    std::wstring prompt = FormatWide(g_ui.manifestSettingInputPrompt.c_str(), key ? key : L"");
    if (ShowTextInputDialog(owner, g_ui.manifestSettingInputTitle, prompt, value, true) != IDOK)
        return;

    if (!IniWrite(L"Settings", key, value.c_str()))
        return;
    Log(g_ui.manifestSettingChanged.c_str(), key ? key : L"", value.c_str());
    RegenerateManifestAndQueueGeneration(owner);
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
    if (!IniWrite(L"Settings", L"SingleInstanceFailureAction", next))
        return;
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
        if (!IniWrite(L"Settings", L"PowerShellExe", file.data()))
            return;
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

static void AfterTrayPopupClosed(HWND h)
{
    // Standard tray-popup cleanup: lets the owner window regain normal message
    // flow after TrackPopupMenu, and immediately releases menu-construction
    // heap/pages when low-idle-memory trimming is enabled. This should not be
    // used to judge GDI leaks; stable one-time menu/theme caches are normal.
    if (h)
        PostMessageW(h, WM_NULL, 0, 0);

    if (!GenerationWorkPending())
        TrimIdleWorkingSetIfConfigured();
}
