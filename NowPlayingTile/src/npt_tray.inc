static constexpr UINT ID_TRAY_REFRESH = 2001;
static constexpr UINT ID_TRAY_WIDGET = 2002;
static constexpr UINT ID_TRAY_REGISTER = 2003;
static constexpr UINT ID_TRAY_LAUNCH_PACKAGED = 2004;
static constexpr UINT ID_TRAY_EXIT = 2005;
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

    wcsncpy_s(ctx->nid.szTip, ARRAYSIZE(ctx->nid.szTip), tip.c_str(), _TRUNCATE);
    ctx->nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &ctx->nid);
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
    ctx->nid.uCallbackMessage = WM_NPT_TRAY;
    ctx->nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(ctx->nid.szTip, ARRAYSIZE(ctx->nid.szTip), APP_DISPLAY_NAME, _TRUNCATE);

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

static void ShowTrayMenu(RuntimeContext* ctx)
{
    if (ctx == nullptr)
    {
        return;
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        return;
    }

    AppendMenuW(menu, MF_STRING, ID_TRAY_REFRESH, L"Refresh now");
    AppendMenuW(menu, MF_STRING, ID_TRAY_WIDGET, L"Open widget");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_REGISTER, L"Register Start tile");
    AppendMenuW(menu, MF_STRING, ID_TRAY_LAUNCH_PACKAGED, L"Launch packaged instance");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(ctx->hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, ctx->hwnd, nullptr);
    PostMessageW(ctx->hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static void BeginUpdate(RuntimeContext* ctx)
{
    if (ctx == nullptr || ctx->updateRunning)
    {
        return;
    }

    ctx->settings = LoadSettings();
    SetTimer(ctx->hwnd, UPDATE_TIMER_ID, static_cast<UINT>(std::max(1, ctx->settings.updateIntervalSeconds) * 1000), nullptr);
    ctx->updateRunning = true;

    HWND hwnd = ctx->hwnd;
    std::thread([hwnd]() {
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }
        catch (...)
        {
        }

        auto* snapshot = new MediaSnapshot(ReadMediaSnapshot());
        PostMessageW(hwnd, WM_NPT_UPDATE_DONE, 0, reinterpret_cast<LPARAM>(snapshot));

        try
        {
            winrt::uninit_apartment();
        }
        catch (...)
        {
        }
    }).detach();
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

    if (g_restoreMessage != 0 && msg == g_restoreMessage)
    {
        if (ctx != nullptr && ctx->trayCreated)
        {
            SetTrayTip(ctx, APP_DISPLAY_NAME);
        }
        return 0;
    }

    switch (msg)
    {
    case WM_CREATE:
        if (ctx != nullptr)
        {
            if (ctx->settings.showTrayIcon)
            {
                CreateTrayIcon(ctx);
            }
            SetTimer(hwnd, UPDATE_TIMER_ID, static_cast<UINT>(std::max(1, ctx->settings.updateIntervalSeconds) * 1000), nullptr);
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
            ctx->updateRunning = false;
            ctx->current = *snapshot;
            MaybeUpdateLiveTile(ctx, *snapshot);
            SetTrayTip(ctx, snapshot->title);
        }
        return 0;

    case WM_NPT_TRAY:
        if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_RBUTTONUP)
        {
            ShowTrayMenu(ctx);
        }
        else if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
        {
            ShellExecuteW(nullptr, L"open", g_exePath.c_str(), L"--widget --allow-multiple", g_exeDir.c_str(), SW_SHOWNORMAL);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_REFRESH:
            BeginUpdate(ctx);
            return 0;
        case ID_TRAY_WIDGET:
            ShellExecuteW(nullptr, L"open", g_exePath.c_str(), L"--widget --allow-multiple", g_exeDir.c_str(), SW_SHOWNORMAL);
            return 0;
        case ID_TRAY_REGISTER:
            ShellExecuteW(nullptr, L"open", g_exePath.c_str(), L"--register", g_exeDir.c_str(), SW_SHOWNORMAL);
            return 0;
        case ID_TRAY_LAUNCH_PACKAGED:
            ShellExecuteW(nullptr, L"open", g_exePath.c_str(), L"--launch-packaged", g_exeDir.c_str(), SW_SHOWNORMAL);
            return 0;
        case ID_TRAY_EXIT:
            DestroyWindow(hwnd);
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
            DestroyTrayIcon(ctx);
        }
        KillTimer(hwnd, UPDATE_TIMER_ID);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static int RunBackground(const AppOptions& options)
{
    RuntimeContext ctx;
    ctx.settings = LoadSettings();
    if (options.forceTray)
    {
        ctx.settings.showTrayIcon = true;
    }
    if (options.forceNoTray)
    {
        ctx.settings.showTrayIcon = false;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = BackgroundWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        APP_DISPLAY_NAME,
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
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
