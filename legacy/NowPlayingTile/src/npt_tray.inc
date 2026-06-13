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

static bool CreateTrayIcon(RuntimeContext* ctx)
{
    if (ctx == nullptr || ctx->trayCreated)
    {
        return ctx != nullptr && ctx->trayCreated;
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
        if (!Shell_NotifyIconW(NIM_SETVERSION, &ctx->nid))
        {
            Log(L"Could not select the modern tray notification behavior: %s",
                GetLastErrorText(GetLastError()).c_str());
        }
    }
    return ctx->trayCreated;
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

    HMENU menu = aip::CreateTrayPopupMenu();
    if (menu == nullptr)
    {
        return;
    }

    bool built =
        aip::AppendTrayMenuItem(menu, ID_TRAY_REFRESH, L"Refresh now") &&
        aip::AppendTrayMenuItem(menu, ID_TRAY_WIDGET, L"Open widget") &&
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) != FALSE &&
        aip::AppendTrayMenuItem(menu, ID_TRAY_REGISTER, L"Register Start tile") &&
        aip::AppendTrayMenuItem(menu, ID_TRAY_LAUNCH_PACKAGED, L"Launch packaged instance") &&
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) != FALSE &&
        aip::AppendTrayMenuItem(menu, ID_TRAY_EXIT, L"Exit");
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
        if (ctx != nullptr && ctx->settings.showTrayIcon)
        {
            ctx->trayCreated = false;
            if (!CreateTrayIcon(ctx))
            {
                Log(L"Could not restore the tray icon after Explorer restarted.");
            }
        }
        return 0;
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
                if (!CreateTrayIcon(ctx))
                {
                    Log(L"Could not create the tray icon: %s",
                        GetLastErrorText(GetLastError()).c_str());
                }
            }
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
