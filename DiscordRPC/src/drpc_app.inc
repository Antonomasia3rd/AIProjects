static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_stopRequested = true;
        return TRUE;
    default:
        return FALSE;
    }
}

static bool SleepInterruptible(int milliseconds)
{
    int remaining = std::max(0, milliseconds);
    while (remaining > 0 && !g_stopRequested.load())
    {
        if (g_reloadRequested.load() || g_refreshRequested.load())
        {
            return false;
        }
        int slice = std::min(remaining, 250);
        Sleep(static_cast<DWORD>(slice));
        remaining -= slice;
    }
    return !g_stopRequested.load();
}

static void Notify(RuntimeContext* ctx, const wchar_t* stringKey, const wchar_t* fallback, DWORD icon, const wchar_t* flagKey)
{
    if (ctx == nullptr || flagKey == nullptr || !IniReadB(L"app", flagKey, true))
    {
        return;
    }
    QueueTrayBalloon(ctx, GetUiString(L"notification_title", APP_DISPLAY_NAME), GetUiString(stringKey, fallback), icon);
}

static int UpdatePresenceOnce(std::unique_ptr<DiscordIpcClient>& ipc, std::unique_ptr<DiscordGatewayClient>& gateway, bool verbose, RuntimeContext* ctx)
{
    std::wstring transport = NormalizeTransportMode(IniReadS(L"general", L"transport_mode", L"ipc"));
    std::wstring clientId = Trim(IniReadS(L"general", L"client_id", L""));
    if (clientId.empty())
    {
        std::wstring message = GetUiString(L"missing_client_id", L"Missing [general] client_id. Create a Discord application and paste its Application ID into the INI.");
        LogError(message);
        Notify(ctx, L"notification_failed", L"Rich Presence update failed.", NIIF_WARNING, L"notify_on_failure");
        return 2;
    }

    PresencePayload payload = BuildPresencePayload();
    if (verbose)
    {
        LogDebug(L"Presence JSON: " + Utf8ToWide(payload.activityJson));
    }

    auto updateIpc = [&]() -> int
    {
        gateway.reset();
        if (!ipc)
        {
            ipc.reset(new DiscordIpcClient(clientId, verbose, LoadIpcOptions()));
        }

        if (!ipc->IsConnected())
        {
            ipc->Connect();
            Notify(ctx, L"notification_connected", L"Rich Presence is active through Discord IPC.", NIIF_INFO, L"notify_on_success");
        }

        ipc->SetActivity(payload.activityJson);
        return 0;
    };

    auto updateGateway = [&]() -> int
    {
        ipc.reset();
        std::wstring token = ResolveDiscordToken();
        if (token.empty())
        {
            std::wstring message = GetUiString(L"notification_gateway_missing_token", L"Gateway mode needs token_protected, token, or token_env.");
            LogError(message);
            Notify(ctx, L"notification_gateway_missing_token", L"Gateway mode needs token_protected, token, or token_env.", NIIF_WARNING, L"notify_on_failure");
            return 2;
        }

        std::wstring status = NormalizeStatus(IniReadS(L"general", L"status", L"online"));
        if (!gateway)
        {
            gateway.reset(new DiscordGatewayClient(token, clientId, verbose, LoadGatewayOptions()));
        }

        if (!gateway->IsConnected())
        {
            gateway->Connect(payload.activityJson, status);
            Notify(ctx, L"notification_connected_gateway", L"Rich Presence is active through Discord Gateway.", NIIF_INFO, L"notify_on_success");
        }

        gateway->SetPresence(payload.activityJson, status);
        return 0;
    };

    if (transport == L"gateway")
    {
        return updateGateway();
    }

    if (transport == L"auto")
    {
        try
        {
            return updateIpc();
        }
        catch (const std::exception& ex)
        {
            LogError(L"Discord IPC update failed; trying Gateway fallback: " + Utf8ToWide(ex.what()));
            ipc.reset();
            return updateGateway();
        }
    }

    return updateIpc();
}

static DWORD RunPresenceLoop(bool once, RuntimeContext* ctx)
{
    std::unique_ptr<DiscordIpcClient> ipc;
    std::unique_ptr<DiscordGatewayClient> gateway;
    bool verbose = g_verboseLogging.load();
    int updateIntervalSeconds = ClampInt(IniReadI(L"general", L"update_interval", 5), 1, 86400);
    DWORD exitCode = 0;

    LogInfo(L"Starting DiscordRPC C++.");
    LogInfo(L"Config: " + MakeAbsolutePath(g_iniPath));

    try
    {
        while (!g_stopRequested.load())
        {
            if (g_reloadRequested.exchange(false))
            {
                if (!ProtectDiscordTokenInConfig(!Trim(IniReadS(L"general", L"token", L"")).empty()))
                {
                    LogError(L"Configuration reload could not complete Discord token protection.");
                }
                ConfigureRuntimeFromConfig(AppOptions{});
                verbose = g_verboseLogging.load();
                updateIntervalSeconds = ClampInt(IniReadI(L"general", L"update_interval", 5), 1, 86400);
                ipc.reset();
                gateway.reset();
                LogInfo(L"Configuration reloaded.");
                Notify(ctx, L"notification_reloaded", L"Configuration reloaded.", NIIF_INFO, L"notify_on_reload");
            }

            g_refreshRequested = false;
            try
            {
                int updateStatus = UpdatePresenceOnce(ipc, gateway, verbose, ctx);
                if (updateStatus != 0)
                {
                    exitCode = static_cast<DWORD>(updateStatus);
                    if (once)
                    {
                        break;
                    }
                }
                else if (once)
                {
                    exitCode = 0;
                    break;
                }
            }
            catch (const std::exception& ex)
            {
                exitCode = 1;
                std::wstring message = Utf8ToWide(ex.what());
                LogError(L"DiscordRPC update failed: " + message);
                if (message.find(L"4004") != std::wstring::npos ||
                    message.find(L"authentication") != std::wstring::npos ||
                    message.find(L"Authentication") != std::wstring::npos)
                {
                    Notify(ctx, L"notification_gateway_auth_failed", L"Discord Gateway authentication failed. Check your token.", NIIF_ERROR, L"notify_on_failure");
                }
                else
                {
                    Notify(ctx, L"notification_failed", L"Rich Presence update failed.", NIIF_WARNING, L"notify_on_failure");
                }
                ipc.reset();
                gateway.reset();
                if (once)
                {
                    break;
                }
            }

            if (!SleepInterruptible(updateIntervalSeconds * 1000))
            {
                continue;
            }
        }

        if (!once && ipc && ipc->IsConnected())
        {
            ipc->ClearActivity();
        }
        if (!once && gateway && gateway->IsConnected())
        {
            gateway->ClearPresence(IniReadS(L"general", L"status", L"online"));
        }
    }
    catch (const std::exception& ex)
    {
        LogError(L"Fatal DiscordRPC loop error: " + Utf8ToWide(ex.what()));
        exitCode = 1;
    }

    LogInfo(L"DiscordRPC stopped.");
    return exitCode;
}

static bool SignalExistingInstance(WPARAM request)
{
    ConfigureSingleInstanceIdentity();
    return aip::SignalInstanceWindow(
        WINDOW_CLASS_NAME,
        g_instanceWindowTitle.c_str(),
        g_singleInstanceMessage,
        request,
        20,
        50,
        false);
}

static bool SignalExistingInstanceToExit()
{
    return SignalExistingInstance(DRPC_INSTANCE_EXIT);
}

static bool ShouldReloadExistingInstance(const AppOptions& options)
{
    return options.reloadExistingInstance || !options.settings.empty() || options.protectToken;
}

static int RunWithControlWindow(const AppOptions& options, bool trayEnabled)
{
    RuntimeContext ctx;
    ctx.trayEnabled = trayEnabled;
    g_taskbarCreatedMessage = aip::RegisterTaskbarCreatedMessage();
    HWND hwnd = CreateRuntimeWindow(&ctx);
    if (hwnd == nullptr)
    {
        LogError(L"Could not create DiscordRPC control window: " + GetLastErrorText(GetLastError()));
        if (options.once)
        {
            return static_cast<int>(RunPresenceLoop(true, nullptr));
        }
        return 1;
    }

    try
    {
        ctx.worker = std::thread([&ctx, &options]() {
            ctx.workerExitCode = RunPresenceLoop(options.once, &ctx);
            if (options.once || g_stopRequested.load())
            {
                PostMessageW(ctx.hwnd, WM_CLOSE, 0, 0);
            }
        });
    }
    catch (const std::exception& ex)
    {
        LogError(L"Could not start DiscordRPC worker thread: " + Utf8ToWide(ex.what()));
        DestroyWindow(hwnd);
        return 1;
    }
    catch (...)
    {
        LogError(L"Could not start DiscordRPC worker thread.");
        DestroyWindow(hwnd);
        return 1;
    }

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_stopRequested = true;
    if (ctx.worker.joinable())
    {
        ctx.worker.join();
    }

    return static_cast<int>(ctx.workerExitCode);
}

static int RunApplication(const AppOptions& options)
{
    if (options.requestExit)
    {
        return SignalExistingInstanceToExit() ? 0 : 2;
    }

    if (!options.once && !AcquireSingleInstance())
    {
        if (ShouldReloadExistingInstance(options))
        {
            if (SignalExistingInstance(DRPC_INSTANCE_RELOAD))
            {
                PrintLine(GetUiString(L"reload_requested", L"DiscordRPC configuration reload requested."));
                ReleaseSingleInstance();
                return 0;
            }
        }

        if (g_singleInstanceMessage != 0)
        {
            SignalExistingInstance(DRPC_INSTANCE_SHOW);
        }

        PrintLine(GetUiString(L"already_running", L"DiscordRPC is already running for this config."), true);
        ReleaseSingleInstance();
        return 1;
    }

    bool trayEnabled = !options.once && !options.forceNoTray && (options.forceTray || IniReadB(L"app", L"show_tray", true));
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    int exitCode = RunWithControlWindow(options, trayEnabled);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
    ReleaseSingleInstance();
    return exitCode;
}
