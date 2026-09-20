static constexpr UINT ID_TRAY_REFRESH = 2001;
static constexpr UINT ID_TRAY_WIDGET = 2002;
static constexpr UINT ID_TRAY_REGISTER = 2003;
static constexpr UINT ID_TRAY_LAUNCH_PACKAGED = 2004;
static constexpr UINT ID_TRAY_EXIT = 2005;
static constexpr UINT ID_TOGGLE_MENU_DROPDOWN = 2006;
static constexpr UINT ID_TRAY_RELOAD = 2007;
static constexpr UINT ID_TOGGLE_STARTUP = 2008;
static constexpr UINT ID_TOGGLE_TRAY = 2009;
static constexpr UINT ID_LAYOUT_TEXT = 2010;
static constexpr UINT ID_LAYOUT_ARTWORK = 2011;
static constexpr UINT ID_LAYOUT_COMBINED = 2012;
static constexpr UINT ID_LAYOUT_CYCLE = 2013;
static constexpr UINT ID_UPDATE_INTERVAL_1 = 2014;
static constexpr UINT ID_UPDATE_INTERVAL_2 = 2015;
static constexpr UINT ID_UPDATE_INTERVAL_5 = 2016;
static constexpr UINT ID_UPDATE_INTERVAL_10 = 2017;
static constexpr UINT ID_TILE_REFRESH_15 = 2018;
static constexpr UINT ID_TILE_REFRESH_30 = 2019;
static constexpr UINT ID_TILE_REFRESH_60 = 2020;
static constexpr UINT ID_TILE_REFRESH_120 = 2021;
static constexpr UINT WM_NPT_TRAY = WM_APP + 43;

static void SetTrayTip(RuntimeContext* ctx, const std::wstring& text)
{
    if (ctx == nullptr || !ctx->trayCreated)
    {
        return;
    }

    std::wstring tip = text.empty() ? APP_DISPLAY_NAME : text;
    if (tip.size() >= ARRAYSIZE(ctx->nid.szTip))
    {
        tip = tip.substr(0, ARRAYSIZE(ctx->nid.szTip) - 4) + L"...";
    }

    DWORD error = ERROR_SUCCESS;
    if (!aip::ModifyTrayIconTooltip(
        ctx->nid,
        tip,
        APP_DISPLAY_NAME,
        true,
        &error))
    {
        Log(L"Could not update the tray tooltip: %s", GetLastErrorText(error).c_str());
    }
}

static bool CreateTrayIcon(RuntimeContext* ctx)
{
    if (ctx == nullptr || ctx->trayCreated)
    {
        return ctx != nullptr && ctx->trayCreated;
    }

    DWORD addError = ERROR_SUCCESS;
    DWORD versionError = ERROR_SUCCESS;
    ctx->trayCreated = aip::RegisterTrayIcon(
        ctx->nid,
        ctx->hwnd,
        1,
        WM_NPT_TRAY,
        LoadIconW(nullptr, IDI_APPLICATION),
        APP_DISPLAY_NAME,
        APP_DISPLAY_NAME,
        true,
        &addError,
        &versionError);
    if (!ctx->trayCreated)
    {
        Log(L"Could not create the tray icon: %s", GetLastErrorText(addError).c_str());
    }
    else if (versionError != ERROR_SUCCESS)
    {
        Log(L"Could not select the modern tray notification behavior: %s",
            GetLastErrorText(versionError).c_str());
    }
    return ctx->trayCreated;
}

static void DestroyTrayIcon(RuntimeContext* ctx)
{
    if (ctx != nullptr && ctx->trayCreated)
    {
        aip::UnregisterTrayIcon(ctx->nid);
        ctx->trayCreated = false;
    }
}

static void ReloadRuntimeSettings(
    RuntimeContext* ctx,
    bool reconcileStartup)
{
    if (ctx == nullptr)
        return;

    ctx->settings = LoadSettings();
    if (SetTimer(
            ctx->hwnd,
            UPDATE_TIMER_ID,
            static_cast<UINT>((std::max)(1, ctx->settings.updateIntervalSeconds) * 1000),
            nullptr) == 0)
    {
        Log(L"Could not update the media refresh timer: %s",
            GetLastErrorText(GetLastError()).c_str());
    }

    if (ctx->settings.showTrayIcon)
    {
        if (!ctx->trayCreated && !CreateTrayIcon(ctx))
        {
            Log(L"Could not create the configured tray icon.");
        }
        else if (ctx->trayCreated)
        {
            SetTrayTip(ctx, ctx->current.title);
        }
    }
    else
    {
        DestroyTrayIcon(ctx);
    }

    if (reconcileStartup)
    {
        std::wstring startupError;
        if (!ReconcileNowPlayingStartupFromConfig(startupError))
        {
            Log(L"Could not synchronize the packaged StartupTask after reload: %s",
                startupError.c_str());
        }
    }
}

static void AppendSecondsChoice(
    HMENU menu,
    UINT command,
    int value,
    int current)
{
    aip::AppendTrayMenuItem(
        menu,
        command,
        std::to_wstring(value) + L" seconds",
        value == current);
}

static void ShowTrayMenu(RuntimeContext* ctx)
{
    if (ctx == nullptr)
    {
        return;
    }

    ReloadRuntimeSettings(ctx, true);

    HMENU menu = aip::CreateTrayPopupMenu();
    if (menu == nullptr)
    {
        return;
    }

    const bool showMenuAsDropdown = ctx->settings.showMenuAsDropdown;
    bool built = aip::AppendBaselineTrayMenuHeader(
        menu,
        ID_TOGGLE_MENU_DROPDOWN,
        L"Show menu as dropdown",
        showMenuAsDropdown,
        ID_TRAY_REFRESH,
        L"Refresh now",
        std::wstring(L"Version: ") + APP_VERSION_TEXT);

    aip::TraySectionLayout sections(menu, showMenuAsDropdown);
    HMENU general = sections.Begin(L"General");
    built = aip::AppendTrayMenuItem(general, ID_TRAY_RELOAD, L"Reload configuration") && built;

    aip::PackagedStartupStatus startupStatus;
    std::wstring startupError;
    bool startupEnabled = ctx->settings.runAtStartup;
    if (QueryNowPlayingStartupTask(startupStatus, startupError))
    {
        startupEnabled = startupStatus.enabled;
    }
    else
    {
        Log(L"Could not query the packaged StartupTask: %s", startupError.c_str());
    }
    built = aip::AppendTrayMenuItem(general, ID_TOGGLE_STARTUP, L"Run at startup", startupEnabled) && built;
    built = aip::AppendTrayMenuItem(general, ID_TOGGLE_TRAY, L"Show tray icon", ctx->settings.showTrayIcon) && built;
    sections.End(general, L"General");

    HMENU tile = sections.Begin(L"Tile and media");
    HMENU layout = aip::BeginTrayNestedMenu(tile);
    built = aip::AppendTrayMenuItem(layout, ID_LAYOUT_TEXT, L"Text", ctx->settings.tileLayout == TileLayout::Text) && built;
    built = aip::AppendTrayMenuItem(layout, ID_LAYOUT_ARTWORK, L"Artwork", ctx->settings.tileLayout == TileLayout::Artwork) && built;
    built = aip::AppendTrayMenuItem(layout, ID_LAYOUT_COMBINED, L"Combined", ctx->settings.tileLayout == TileLayout::Combined) && built;
    built = aip::AppendTrayMenuItem(layout, ID_LAYOUT_CYCLE, L"Cycle", ctx->settings.tileLayout == TileLayout::Cycle) && built;
    aip::EndTrayNestedMenu(tile, layout, L"Tile layout");

    HMENU updateInterval = aip::BeginTrayNestedMenu(tile);
    AppendSecondsChoice(updateInterval, ID_UPDATE_INTERVAL_1, 1, ctx->settings.updateIntervalSeconds);
    AppendSecondsChoice(updateInterval, ID_UPDATE_INTERVAL_2, 2, ctx->settings.updateIntervalSeconds);
    AppendSecondsChoice(updateInterval, ID_UPDATE_INTERVAL_5, 5, ctx->settings.updateIntervalSeconds);
    AppendSecondsChoice(updateInterval, ID_UPDATE_INTERVAL_10, 10, ctx->settings.updateIntervalSeconds);
    aip::EndTrayNestedMenu(tile, updateInterval, L"Media polling interval");

    HMENU tileRefresh = aip::BeginTrayNestedMenu(tile);
    AppendSecondsChoice(tileRefresh, ID_TILE_REFRESH_15, 15, ctx->settings.tileRefreshSeconds);
    AppendSecondsChoice(tileRefresh, ID_TILE_REFRESH_30, 30, ctx->settings.tileRefreshSeconds);
    AppendSecondsChoice(tileRefresh, ID_TILE_REFRESH_60, 60, ctx->settings.tileRefreshSeconds);
    AppendSecondsChoice(tileRefresh, ID_TILE_REFRESH_120, 120, ctx->settings.tileRefreshSeconds);
    aip::EndTrayNestedMenu(tile, tileRefresh, L"Tile refresh interval");
    sections.End(tile, L"Tile and media");

    HMENU actions = sections.Begin(L"Actions");
    built = aip::AppendTrayMenuItem(actions, ID_TRAY_WIDGET, L"Open widget") && built;
    built = aip::AppendTrayMenuItem(actions, ID_TRAY_REGISTER, L"Register Start tile") && built;
    built = aip::AppendTrayMenuItem(actions, ID_TRAY_LAUNCH_PACKAGED, L"Launch packaged instance") && built;
    sections.End(actions, L"Actions");

    sections.AppendFinalSeparator();
    built = aip::AppendTrayMenuItem(menu, ID_TRAY_EXIT, L"Exit") && built;
    if (!built)
    {
        Log(L"Could not build the tray menu: %s", GetLastErrorText(GetLastError()).c_str());
        DestroyMenu(menu);
        return;
    }

    POINT pt = {};
    if (!GetCursorPos(&pt))
    {
        Log(L"Could not read the cursor position for the tray menu: %s",
            GetLastErrorText(GetLastError()).c_str());
        DestroyMenu(menu);
        return;
    }
    SetForegroundWindow(ctx->hwnd);
    aip::TrackPostAndDestroyTrayMenu(
        menu,
        ctx->hwnd,
        pt,
        TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN);
}

static bool LaunchSelfFromTray(const wchar_t* arguments)
{
    HINSTANCE result = ShellExecuteW(
        nullptr,
        L"open",
        g_exePath.c_str(),
        arguments,
        g_exeDir.c_str(),
        SW_SHOWNORMAL);
    INT_PTR status = reinterpret_cast<INT_PTR>(result);
    if (status <= 32)
    {
        Log(L"Could not launch tray action \"%s\" (ShellExecute=%lld).",
            arguments ? arguments : L"",
            static_cast<long long>(status));
        return false;
    }
    return true;
}

static void BeginUpdate(RuntimeContext* ctx)
{
    if (ctx == nullptr || ctx->closing.load() || ctx->updateRunning.load())
    {
        return;
    }

    if (ctx->updateThread.joinable())
    {
        ctx->updateThread.join();
    }

    ReloadRuntimeSettings(ctx, false);
    ctx->updateRunning.store(true);

    try
    {
        ctx->updateThread = std::thread([ctx]() {
            bool apartmentInitialized = false;
            try
            {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                apartmentInitialized = true;

                std::unique_ptr<MediaSnapshot> snapshot =
                    std::make_unique<MediaSnapshot>(ReadMediaSnapshot());
                if (!ctx->closing.load() &&
                    PostMessageW(
                        ctx->hwnd,
                        WM_NPT_UPDATE_DONE,
                        0,
                        reinterpret_cast<LPARAM>(snapshot.get())) != FALSE)
                {
                    snapshot.release();
                }
                else
                {
                    ctx->updateRunning.store(false);
                }
            }
            catch (const winrt::hresult_error& ex)
            {
                ctx->updateRunning.store(false);
                Log(L"Media update worker failed: 0x%08X %s",
                    static_cast<unsigned int>(ex.code()),
                    ex.message().c_str());
            }
            catch (const std::exception& ex)
            {
                ctx->updateRunning.store(false);
                Log(L"Media update worker failed: %S", ex.what());
            }
            catch (...)
            {
                ctx->updateRunning.store(false);
                Log(L"Media update worker failed with an unknown exception.");
            }

            if (apartmentInitialized)
            {
                try
                {
                    winrt::uninit_apartment();
                }
                catch (...)
                {
                }
            }
        });
    }
    catch (const std::exception&)
    {
        ctx->updateRunning.store(false);
        Log(L"Could not start media update worker.");
    }
}

static void StopUpdates(RuntimeContext* ctx)
{
    if (ctx == nullptr)
    {
        return;
    }

    ctx->closing = true;
    if (ctx->updateThread.joinable() &&
        ctx->updateThread.get_id() != std::this_thread::get_id())
    {
        ctx->updateThread.join();
    }

    MSG pending = {};
    while (PeekMessageW(&pending, ctx->hwnd, WM_NPT_UPDATE_DONE, WM_NPT_UPDATE_DONE, PM_REMOVE))
    {
        delete reinterpret_cast<MediaSnapshot*>(pending.lParam);
    }
    ctx->updateRunning.store(false);
}

static void ReportTraySettingError(HWND hwnd, const std::wstring& error)
{
    Log(L"Could not update a tray setting: %s", error.c_str());
    MessageBoxW(
        hwnd,
        error.c_str(),
        APP_DISPLAY_NAME,
        MB_OK | MB_ICONERROR);
}

static bool ApplyTraySetting(
    RuntimeContext* ctx,
    const wchar_t* key,
    const std::wstring& value)
{
    if (ctx == nullptr)
        return false;

    std::wstring error;
    if (!PersistNowPlayingSetting(key, value, error))
    {
        ReportTraySettingError(ctx->hwnd, error);
        return false;
    }
    ReloadRuntimeSettings(ctx, true);
    BeginUpdate(ctx);
    return true;
}

static bool DispatchTraySettingsCommand(RuntimeContext* ctx, UINT command)
{
    if (ctx == nullptr)
        return false;

    switch (command)
    {
    case ID_TOGGLE_MENU_DROPDOWN:
        return ApplyTraySetting(
            ctx,
            L"ShowMenuAsDropdown",
            ctx->settings.showMenuAsDropdown ? L"false" : L"true");
    case ID_TRAY_RELOAD:
        ReloadRuntimeSettings(ctx, true);
        BeginUpdate(ctx);
        return true;
    case ID_TOGGLE_STARTUP:
    {
        aip::PackagedStartupStatus status;
        std::wstring error;
        if (!QueryNowPlayingStartupTask(status, error))
        {
            ReportTraySettingError(ctx->hwnd, error);
            return true;
        }
        ApplyTraySetting(ctx, L"RunAtStartup", status.enabled ? L"false" : L"true");
        return true;
    }
    case ID_TOGGLE_TRAY:
        return ApplyTraySetting(
            ctx,
            L"ShowTrayIcon",
            ctx->settings.showTrayIcon ? L"false" : L"true");
    case ID_LAYOUT_TEXT:
        return ApplyTraySetting(ctx, L"TileLayout", L"Text");
    case ID_LAYOUT_ARTWORK:
        return ApplyTraySetting(ctx, L"TileLayout", L"Artwork");
    case ID_LAYOUT_COMBINED:
        return ApplyTraySetting(ctx, L"TileLayout", L"Combined");
    case ID_LAYOUT_CYCLE:
        return ApplyTraySetting(ctx, L"TileLayout", L"Cycle");
    case ID_UPDATE_INTERVAL_1:
        return ApplyTraySetting(ctx, L"UpdateIntervalSeconds", L"1");
    case ID_UPDATE_INTERVAL_2:
        return ApplyTraySetting(ctx, L"UpdateIntervalSeconds", L"2");
    case ID_UPDATE_INTERVAL_5:
        return ApplyTraySetting(ctx, L"UpdateIntervalSeconds", L"5");
    case ID_UPDATE_INTERVAL_10:
        return ApplyTraySetting(ctx, L"UpdateIntervalSeconds", L"10");
    case ID_TILE_REFRESH_15:
        return ApplyTraySetting(ctx, L"TileRefreshSeconds", L"15");
    case ID_TILE_REFRESH_30:
        return ApplyTraySetting(ctx, L"TileRefreshSeconds", L"30");
    case ID_TILE_REFRESH_60:
        return ApplyTraySetting(ctx, L"TileRefreshSeconds", L"60");
    case ID_TILE_REFRESH_120:
        return ApplyTraySetting(ctx, L"TileRefreshSeconds", L"120");
    default:
        return false;
    }
}

static LRESULT CALLBACK BackgroundWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
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

    if (g_taskbarCreatedMessage != 0 && msg == g_taskbarCreatedMessage)
    {
        if (ctx != nullptr)
        {
            ctx->trayCreated = false;
            ReloadRuntimeSettings(ctx, false);
        }
        return 0;
    }

    if (g_restoreMessage != 0 && msg == g_restoreMessage)
    {
        if (ctx != nullptr)
        {
            ReloadRuntimeSettings(ctx, true);
            SetTrayTip(ctx, ctx->current.title);
            BeginUpdate(ctx);
        }
        return 0;
    }

    switch (msg)
    {
    case WM_CREATE:
        if (ctx != nullptr)
        {
            ReloadRuntimeSettings(ctx, true);
            BeginUpdate(ctx);
        }
        return 0;

    case WM_TIMER:
        if (wParam == UPDATE_TIMER_ID)
        {
            BeginUpdate(ctx);
            return 0;
        }
        break;

    case WM_NPT_UPDATE_DONE:
        if (ctx != nullptr)
        {
            std::unique_ptr<MediaSnapshot> snapshot(reinterpret_cast<MediaSnapshot*>(lParam));
            if (ctx->updateThread.joinable())
            {
                ctx->updateThread.join();
            }
            ctx->updateRunning.store(false);
            ctx->current = *snapshot;
            MaybeUpdateLiveTile(ctx, *snapshot);
            SetTrayTip(ctx, snapshot->title);
        }
        return 0;

    case WM_NPT_TRAY:
        if (LOWORD(lParam) == WM_CONTEXTMENU ||
            LOWORD(lParam) == WM_RBUTTONUP ||
            LOWORD(lParam) == NIN_KEYSELECT)
        {
            ShowTrayMenu(ctx);
        }
        else if (LOWORD(lParam) == NIN_SELECT ||
            LOWORD(lParam) == WM_LBUTTONDBLCLK)
        {
            LaunchSelfFromTray(L"--widget --allow-multiple");
        }
        return 0;

    case WM_COMMAND:
        if (DispatchTraySettingsCommand(ctx, LOWORD(wParam)))
            return 0;
        switch (LOWORD(wParam))
        {
        case ID_TRAY_REFRESH:
            BeginUpdate(ctx);
            return 0;
        case ID_TRAY_WIDGET:
            LaunchSelfFromTray(L"--widget --allow-multiple");
            return 0;
        case ID_TRAY_REGISTER:
            LaunchSelfFromTray(L"--register");
            return 0;
        case ID_TRAY_LAUNCH_PACKAGED:
            LaunchSelfFromTray(L"--launch-packaged");
            return 0;
        case ID_TRAY_EXIT:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;

    case WM_CLOSE:
        StopUpdates(ctx);
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (ctx != nullptr)
        {
            DestroyTrayIcon(ctx);
            StopUpdates(ctx);
        }
        KillTimer(hwnd, UPDATE_TIMER_ID);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static int RunBackground(const AppOptions& options)
{
    (void)options;
    RuntimeContext ctx;
    ctx.settings = LoadSettings();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = BackgroundWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (RegisterClassExW(&wc) == 0)
    {
        Log(L"Could not register background window class: %s", GetLastErrorText(GetLastError()).c_str());
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        g_instanceIdentity.windowTitle.c_str(),
        WS_OVERLAPPED,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        g_hInst,
        &ctx);

    if (hwnd == nullptr)
    {
        Log(L"Could not create background window: %s", GetLastErrorText(GetLastError()).c_str());
        return 1;
    }

    MSG msg = {};
    for (;;)
    {
        BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result == 0)
        {
            return static_cast<int>(msg.wParam);
        }
        if (result == -1)
        {
            Log(L"GetMessageW(background window) failed: %s", GetLastErrorText(GetLastError()).c_str());
            return 1;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
