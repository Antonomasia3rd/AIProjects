static constexpr UINT ID_TRAY_REFRESH = 3001;
static constexpr UINT ID_TRAY_RELOAD = 3002;
static constexpr UINT ID_TRAY_OPEN_CONFIG = 3003;
static constexpr UINT ID_TRAY_OPEN_LOG_FOLDER = 3004;
static constexpr UINT ID_TRAY_RECENT_LOG = 3005;
static constexpr UINT ID_TRAY_EXIT = 3006;
static constexpr UINT ID_TOGGLE_WINDOW_DETECTION = 3010;
static constexpr UINT ID_TOGGLE_DETAILS = 3011;
static constexpr UINT ID_TOGGLE_STATE = 3012;
static constexpr UINT ID_TOGGLE_LARGE = 3013;
static constexpr UINT ID_TOGGLE_SMALL = 3014;
static constexpr UINT ID_TOGGLE_BUTTONS = 3015;
static constexpr UINT ID_TOGGLE_LOGGING = 3020;
static constexpr UINT ID_TOGGLE_FILE_LOGGING = 3021;
static constexpr UINT ID_TOGGLE_VERBOSE = 3022;
static constexpr UINT ID_TOGGLE_CONSOLE = 3023;
static constexpr UINT ID_TOGGLE_MENU_DROPDOWN = 3024;
static constexpr UINT ID_TRANSPORT_IPC = 3030;
static constexpr UINT ID_TRANSPORT_GATEWAY = 3031;
static constexpr UINT ID_TRANSPORT_AUTO = 3032;
static constexpr UINT ID_INTERVAL_5 = 3045;
static constexpr UINT ID_INTERVAL_15 = 3046;
static constexpr UINT ID_INTERVAL_30 = 3047;
static constexpr UINT ID_INTERVAL_60 = 3048;

static void ShowTrayBalloon(RuntimeContext* ctx, const std::wstring& title, const std::wstring& message, DWORD icon)
{
    if (ctx == nullptr || !ctx->trayCreated || !g_notificationsEnabled.load())
    {
        return;
    }

    aip::ShowTrayBalloon(ctx->nid, title, message, icon);
}

static void QueueTrayBalloon(RuntimeContext* ctx, const std::wstring& title, const std::wstring& message, DWORD icon)
{
    if (ctx == nullptr || !g_notificationsEnabled.load())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(ctx->notificationMutex);
        if (!ctx->acceptingNotifications)
        {
            return;
        }
        ctx->pendingNotifications.push_back({ title, message, icon });
    }

    if (!PostMessageW(ctx->hwnd, WM_DRPC_NOTIFY, 0, 0))
    {
        std::lock_guard<std::mutex> lock(ctx->notificationMutex);
        ctx->pendingNotifications.clear();
    }
}

static void ShowPendingTrayBalloons(RuntimeContext* ctx)
{
    if (ctx == nullptr)
    {
        return;
    }

    std::deque<TrayNotification> pending;
    {
        std::lock_guard<std::mutex> lock(ctx->notificationMutex);
        pending.swap(ctx->pendingNotifications);
    }
    for (const auto& notification : pending)
    {
        ShowTrayBalloon(ctx, notification.title, notification.message, notification.icon);
    }
}

static void CreateTrayIcon(RuntimeContext* ctx)
{
    if (ctx == nullptr || ctx->trayCreated)
    {
        return;
    }

    ctx->nid = {};
    ctx->nid.cbSize = sizeof(ctx->nid);
    ctx->nid.hWnd = ctx->hwnd;
    ctx->nid.uID = 1;
    ctx->nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    ctx->nid.uCallbackMessage = WM_DRPC_TRAY;
    ctx->nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(ctx->nid.szTip, ARRAYSIZE(ctx->nid.szTip), GetUiString(L"tray_tip", APP_DISPLAY_NAME).c_str(), _TRUNCATE);

    ctx->trayCreated = Shell_NotifyIconW(NIM_ADD, &ctx->nid) != FALSE;
    if (ctx->trayCreated)
    {
        ctx->nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &ctx->nid);
    }
}

static void DestroyTrayIcon(RuntimeContext* ctx)
{
    if (ctx != nullptr && ctx->trayCreated)
    {
        Shell_NotifyIconW(NIM_DELETE, &ctx->nid);
        ctx->trayCreated = false;
    }
}

static void AppendChecked(HMENU menu, UINT id, const std::wstring& text, bool checked)
{
    aip::AppendCheckedMenuItem(menu, id, text, checked);
}

static void AppendInterval(HMENU menu, UINT id, int seconds, int current)
{
    std::wstring text = std::to_wstring(seconds) + L" seconds";
    aip::AppendTrayMenuItem(menu, id, text, current == seconds);
}

static void ShowTrayMenu(RuntimeContext* ctx)
{
    if (ctx == nullptr)
    {
        return;
    }

    HMENU menu = aip::CreateTrayPopupMenu();
    if (menu == nullptr)
    {
        return;
    }

    aip::AppendTrayMenuItem(menu, ID_TRAY_REFRESH, GetUiString(L"menu_refresh", L"Refresh now"));
    aip::AppendTrayMenuItem(menu, 0, GetUiString(L"menu_status", APP_DISPLAY_NAME), false, false);
    aip::AppendMenuSeparator(menu);

    bool showMenuAsDropdown = IniReadB(L"app", L"show_menu_as_dropdown", true);
    aip::TraySectionLayout sectionLayout(menu, showMenuAsDropdown);
    auto beginSection = [&](const std::wstring& title) -> HMENU {
        return sectionLayout.Begin(title);
    };
    auto endSection = [&](HMENU section, const std::wstring& title) {
        sectionLayout.End(section, title);
    };

    HMENU general = beginSection(GetUiString(L"menu_general", L"General"));
    aip::AppendTrayMenuItem(general, ID_TRAY_RELOAD, GetUiString(L"menu_reload", L"Reload configuration"));
    AppendChecked(general, ID_TOGGLE_MENU_DROPDOWN, GetUiString(L"menu_dropdown", L"Show menu as dropdown"), showMenuAsDropdown);

    HMENU interval = aip::BeginTrayNestedMenu(general);
    int currentInterval = IniReadI(L"general", L"update_interval", 5);
    AppendInterval(interval, ID_INTERVAL_5, 5, currentInterval);
    AppendInterval(interval, ID_INTERVAL_15, 15, currentInterval);
    AppendInterval(interval, ID_INTERVAL_30, 30, currentInterval);
    AppendInterval(interval, ID_INTERVAL_60, 60, currentInterval);
    aip::EndTrayNestedMenu(general, interval, GetUiString(L"menu_update_interval", L"Update interval"));

    HMENU transport = aip::BeginTrayNestedMenu(general);
    std::wstring currentTransport = NormalizeTransportMode(IniReadS(L"general", L"transport_mode", L"ipc"));
    AppendChecked(transport, ID_TRANSPORT_IPC, GetUiString(L"menu_transport_ipc", L"Discord IPC"), currentTransport == L"ipc");
    AppendChecked(transport, ID_TRANSPORT_GATEWAY, GetUiString(L"menu_transport_gateway", L"Gateway"), currentTransport == L"gateway");
    AppendChecked(transport, ID_TRANSPORT_AUTO, GetUiString(L"menu_transport_auto", L"Auto: IPC then Gateway"), currentTransport == L"auto");
    aip::EndTrayNestedMenu(general, transport, GetUiString(L"menu_transport", L"Transport"));
    endSection(general, GetUiString(L"menu_general", L"General"));

    HMENU presence = beginSection(GetUiString(L"menu_presence", L"Presence"));
    AppendChecked(presence, ID_TOGGLE_WINDOW_DETECTION, GetUiString(L"window_detection", L"Window detection"), IniReadB(L"general", L"window_title_detection_enabled", true));
    AppendChecked(presence, ID_TOGGLE_DETAILS, GetUiString(L"details_field", L"Details field"), IniReadB(L"layout", L"details_field", true));
    AppendChecked(presence, ID_TOGGLE_STATE, GetUiString(L"state_field", L"State field"), IniReadB(L"layout", L"state_field", true));
    AppendChecked(presence, ID_TOGGLE_LARGE, GetUiString(L"large_image", L"Large image"), IniReadB(L"layout", L"show_large_image", false));
    AppendChecked(presence, ID_TOGGLE_SMALL, GetUiString(L"small_image", L"Small image"), IniReadB(L"layout", L"show_small_image", false));
    AppendChecked(presence, ID_TOGGLE_BUTTONS, GetUiString(L"buttons", L"Buttons"), IniReadB(L"layout", L"show_buttons", false));
    endSection(presence, GetUiString(L"menu_presence", L"Presence"));

    HMENU diagnostics = beginSection(GetUiString(L"menu_diagnostics", L"Diagnostics"));
    HMENU logging = aip::BeginTrayNestedMenu(diagnostics);
    AppendChecked(logging, ID_TOGGLE_LOGGING, GetUiString(L"logging_enabled", L"Logging enabled"), IniReadB(L"app", L"logging_enabled", true));
    AppendChecked(logging, ID_TOGGLE_FILE_LOGGING, GetUiString(L"file_logging", L"File logging"), IniReadB(L"app", L"file_logging_enabled", true));
    AppendChecked(logging, ID_TOGGLE_VERBOSE, GetUiString(L"verbose_logging", L"Verbose logging"), IniReadB(L"app", L"verbose_logging", false));
    AppendChecked(logging, ID_TOGGLE_CONSOLE, GetUiString(L"show_console", L"Show console"), IniReadB(L"app", L"show_console", false));
    aip::EndTrayNestedMenu(diagnostics, logging, GetUiString(L"menu_logging", L"Logging"));
    aip::AppendMenuSeparator(diagnostics);
    aip::AppendTrayMenuItem(diagnostics, ID_TRAY_OPEN_CONFIG, GetUiString(L"menu_open_config", L"Open config"));
    aip::AppendTrayMenuItem(diagnostics, ID_TRAY_OPEN_LOG_FOLDER, GetUiString(L"menu_open_log_folder", L"Open log folder"));
    aip::AppendTrayMenuItem(diagnostics, ID_TRAY_RECENT_LOG, GetUiString(L"menu_recent_log", L"Recent log"));
    endSection(diagnostics, GetUiString(L"menu_diagnostics", L"Diagnostics"));

    sectionLayout.AppendFinalSeparator();
    aip::AppendTrayMenuItem(menu, ID_TRAY_EXIT, GetUiString(L"menu_exit", L"Exit"));

    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(ctx->hwnd);
    aip::TrackPostAndDestroyTrayMenu(menu, ctx->hwnd, pt, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN);
}

static void ToggleBoolSetting(const wchar_t* section, const wchar_t* key, bool fallback)
{
    bool next = !IniReadB(section, key, fallback);
    if (!IniWrite(section, key, next ? L"true" : L"false"))
    {
        ReportConfigWriteFailure(L"Writing tray setting [" + std::wstring(section) + L"] " + key);
        return;
    }
    g_reloadRequested = true;
    g_refreshRequested = true;
}

static void SetTraySetting(const wchar_t* section, const wchar_t* key, const std::wstring& value)
{
    if (!IniWrite(section, key, value))
    {
        ReportConfigWriteFailure(L"Writing tray setting [" + std::wstring(section) + L"] " + key);
        return;
    }
    g_reloadRequested = true;
    g_refreshRequested = true;
}

static void SetIntervalSetting(int seconds)
{
    SetTraySetting(L"general", L"update_interval", std::to_wstring(seconds));
}

static void OpenConfigFile()
{
    ShellExecuteW(nullptr, L"open", g_iniPath.c_str(), nullptr, g_exeDir.c_str(), SW_SHOWNORMAL);
}

static void OpenLogFolder()
{
    std::wstring logPath;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        logPath = g_effectiveLogPath.empty() ? g_defaultLogPath : g_effectiveLogPath;
    }
    std::wstring folder = GetDirectoryName(logPath);
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RuntimeContext* ctx = reinterpret_cast<RuntimeContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ctx = reinterpret_cast<RuntimeContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        if (ctx != nullptr)
        {
            ctx->hwnd = hwnd;
        }
    }

    if (g_singleInstanceMessage != 0 && msg == g_singleInstanceMessage)
    {
        if (ctx != nullptr)
        {
            if (wParam == DRPC_INSTANCE_EXIT)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            if (wParam == DRPC_INSTANCE_RELOAD)
            {
                g_reloadRequested = true;
                g_refreshRequested = true;
                ShowTrayBalloon(ctx, GetUiString(L"notification_title", APP_DISPLAY_NAME), GetUiString(L"reload_requested", L"DiscordRPC configuration reload requested."), NIIF_INFO);
                return 0;
            }
            ShowTrayBalloon(ctx, GetUiString(L"notification_title", APP_DISPLAY_NAME), GetUiString(L"already_running", L"DiscordRPC is already running for this config."), NIIF_INFO);
        }
        return 0;
    }

    if (g_taskbarCreatedMessage != 0 && msg == g_taskbarCreatedMessage)
    {
        if (ctx != nullptr && ctx->trayEnabled)
        {
            ctx->trayCreated = false;
            CreateTrayIcon(ctx);
        }
        return 0;
    }

    switch (msg)
    {
    case WM_CREATE:
        if (ctx != nullptr && ctx->trayEnabled)
        {
            CreateTrayIcon(ctx);
            if (IniReadB(L"app", L"notify_on_start", false))
            {
                ShowTrayBalloon(ctx, GetUiString(L"notification_title", APP_DISPLAY_NAME), GetUiString(L"notification_started", L"Rich Presence is running."), NIIF_INFO);
            }
        }
        return 0;

    case WM_DRPC_TRAY:
        if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_RBUTTONUP)
        {
            ShowTrayMenu(ctx);
        }
        else if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
        {
            g_refreshRequested = true;
        }
        return 0;

    case WM_DRPC_NOTIFY:
        ShowPendingTrayBalloons(ctx);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_REFRESH:
            g_refreshRequested = true;
            return 0;
        case ID_TRAY_RELOAD:
            g_reloadRequested = true;
            g_refreshRequested = true;
            return 0;
        case ID_TRAY_OPEN_CONFIG:
            OpenConfigFile();
            return 0;
        case ID_TRAY_OPEN_LOG_FOLDER:
            OpenLogFolder();
            return 0;
        case ID_TRAY_RECENT_LOG:
            MessageBoxW(hwnd, RecentLogText().c_str(), GetUiString(L"recent_log_title", L"DiscordRPC recent log").c_str(), MB_OK | MB_ICONINFORMATION);
            return 0;
        case ID_TRAY_EXIT:
            DestroyWindow(hwnd);
            return 0;
        case ID_TOGGLE_WINDOW_DETECTION:
            ToggleBoolSetting(L"general", L"window_title_detection_enabled", true);
            return 0;
        case ID_TOGGLE_DETAILS:
            ToggleBoolSetting(L"layout", L"details_field", true);
            return 0;
        case ID_TOGGLE_STATE:
            ToggleBoolSetting(L"layout", L"state_field", true);
            return 0;
        case ID_TOGGLE_LARGE:
            ToggleBoolSetting(L"layout", L"show_large_image", false);
            return 0;
        case ID_TOGGLE_SMALL:
            ToggleBoolSetting(L"layout", L"show_small_image", false);
            return 0;
        case ID_TOGGLE_BUTTONS:
            ToggleBoolSetting(L"layout", L"show_buttons", false);
            return 0;
        case ID_TOGGLE_LOGGING:
            ToggleBoolSetting(L"app", L"logging_enabled", true);
            return 0;
        case ID_TOGGLE_FILE_LOGGING:
            ToggleBoolSetting(L"app", L"file_logging_enabled", true);
            return 0;
        case ID_TOGGLE_VERBOSE:
            ToggleBoolSetting(L"app", L"verbose_logging", false);
            return 0;
        case ID_TOGGLE_CONSOLE:
            ToggleBoolSetting(L"app", L"show_console", false);
            return 0;
        case ID_TOGGLE_MENU_DROPDOWN:
            ToggleBoolSetting(L"app", L"show_menu_as_dropdown", true);
            return 0;
        case ID_TRANSPORT_IPC:
            SetTraySetting(L"general", L"transport_mode", L"ipc");
            return 0;
        case ID_TRANSPORT_GATEWAY:
            SetTraySetting(L"general", L"transport_mode", L"gateway");
            return 0;
        case ID_TRANSPORT_AUTO:
            SetTraySetting(L"general", L"transport_mode", L"auto");
            return 0;
        case ID_INTERVAL_5:
            SetIntervalSetting(5);
            return 0;
        case ID_INTERVAL_15:
            SetIntervalSetting(15);
            return 0;
        case ID_INTERVAL_30:
            SetIntervalSetting(30);
            return 0;
        case ID_INTERVAL_60:
            SetIntervalSetting(60);
            return 0;
        default:
            break;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (ctx != nullptr)
        {
            {
                std::lock_guard<std::mutex> lock(ctx->notificationMutex);
                ctx->acceptingNotifications = false;
                ctx->pendingNotifications.clear();
            }
            DestroyTrayIcon(ctx);
        }
        g_stopRequested = true;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HWND CreateRuntimeWindow(RuntimeContext* ctx)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = g_hInst;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = WINDOW_CLASS_NAME;
    RegisterClassExW(&wc);

    return CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        g_instanceWindowTitle.c_str(),
        WS_OVERLAPPED,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        g_hInst,
        ctx);
}
