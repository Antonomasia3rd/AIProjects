static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
    {
        HWND hwnd = g_runtimeWindow.load();
        if (hwnd != nullptr)
        {
            if (!PostMessageW(hwnd, WM_DRPC_REQUEST_SHUTDOWN, ctrlType, 0))
            {
                g_stopRequested = true;
            }
        }
        else
        {
            g_stopRequested = true;
        }
        return TRUE;
    }
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

static bool BackendControlRequested()
{
    return g_stopRequested.load() || g_reloadRequested.load() || g_refreshRequested.load();
}

static void Notify(RuntimeContext* ctx, const wchar_t* stringKey, const wchar_t* fallback, DWORD icon, const wchar_t* flagKey)
{
    if (ctx == nullptr || flagKey == nullptr || !IniReadB(L"app", flagKey, true))
    {
        return;
    }
    QueueTrayBalloon(ctx, GetUiString(L"notification_title", APP_DISPLAY_NAME), GetUiString(stringKey, fallback), icon);
}

static DWORD RunPresenceLoop(const AppOptions& invocationOptions, bool once, RuntimeContext* ctx)
{
    aip::discord::Service service;
    const auto serviceOptions = [&] {
        aip::discord::Options options;
        options.profilePath = MakeAbsolutePath(g_iniPath);
        options.sendEnabled = true; // Standalone normal launch is explicit send intent.
        options.refreshMs = ClampInt(IniReadI(L"general", L"update_interval", 5), 1, 86400) * 1000;
        options.operationTimeoutMs = 30000;
        options.clearOnStop = !once;
        return options;
    };
    LogInfo(L"Starting DiscordRPC C++.");
    LogInfo(L"DiscordRPC version: " + aip::ReleaseVersionDisplayText());
    LogInfo(L"Config: " + MakeAbsolutePath(g_iniPath));
    if (IniReadB(L"state", L"force_shutdown_pending_cleanup", false))
    {
        LogError(GetUiString(L"previous_force_shutdown_warning",
            L"The previous run was force-closed while Discord cleanup was pending."));
        if (!IniWrite(L"state", L"force_shutdown_pending_cleanup", L"false"))
            ReportConfigWriteFailure(L"Clearing force-shutdown pending cleanup state");
    }
    std::wstring serviceError;
    if (!service.Start(serviceOptions(), serviceError)) { LogError(serviceError); return 2; }
    DWORD exitCode = 0;
    std::uint64_t revision = 0;
    aip::discord::Phase previousPhase = aip::discord::Phase::Stopped;
    while (!g_stopRequested.load())
    {
        bool reload = g_reloadRequested.exchange(false);
        bool refresh = g_refreshRequested.exchange(false);
        if (reload)
        {
            // Only the standalone host owns migration, UI and Startup changes.
            if (!ProtectDiscordTokenInConfig(!Trim(IniReadS(L"general", L"token", L"")).empty()))
                LogError(L"Configuration reload could not complete Discord token protection.");
            ConfigureRuntimeFromConfig(invocationOptions);
            std::wstring startupError;
            if (!ReconcileDiscordStartupShortcutFromConfig(startupError))
                LogError(L"Could not synchronize this profile's shell:startup shortcut after reload: " + startupError);
            if (ctx && ctx->trayCreated)
            {
                DWORD tooltipError = ERROR_SUCCESS;
                if (!aip::ModifyTrayIconTooltip(ctx->nid, GetUiString(L"tray_tip", APP_DISPLAY_NAME),
                        APP_DISPLAY_NAME, true, &tooltipError))
                    LogError(L"Could not update tray tooltip after configuration reload: " + GetLastErrorText(tooltipError));
            }
            Notify(ctx, L"notification_reloaded", L"Configuration reloaded.", NIIF_INFO, L"notify_on_reload");
        }
        if ((reload || refresh) && !service.Reload(serviceOptions(), serviceError)) LogError(serviceError);
        const auto snapshot = service.Read();
        if (snapshot.revision != revision)
        {
            revision = snapshot.revision;
            if (snapshot.phase == aip::discord::Phase::Error)
            {
                exitCode = 1;
                LogError(L"DiscordRPC update failed: " + snapshot.error);
                Notify(ctx, L"notification_failed", L"Rich Presence update failed.", NIIF_WARNING, L"notify_on_failure");
                if (once) break;
            }
            else if (snapshot.phase == aip::discord::Phase::Active)
            {
                exitCode = 0;
                if (g_verboseLogging.load()) LogDebug(L"Presence JSON: " + Utf8ToWide(snapshot.activityJson));
                if (previousPhase != aip::discord::Phase::Active)
                    Notify(ctx, L"notification_connected", L"Rich Presence is active.", NIIF_INFO, L"notify_on_success");
                if (once) break;
            }
            previousPhase = snapshot.phase;
        }
        SleepInterruptible(50);
    }
    if (!service.Stop(5000))
    {
        LogError(L"Discord transport cleanup is still pending after its bounded stop wait.");
        exitCode = 1;
    }
    LogInfo(L"DiscordRPC stopped.");
    return exitCode;
}
static bool SignalExistingInstance(WPARAM request)
{
    ConfigureSingleInstanceIdentity();
    return aip::SendInstanceWindowRequest(
        WINDOW_CLASS_NAME,
        g_instanceWindowTitle.c_str(),
        WM_DRPC_INSTANCE_CONTROL,
        request,
        20,
        50,
        10000,
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
    if (g_taskbarCreatedMessage == 0)
    {
        LogError(L"Could not register the TaskbarCreated message: " +
            GetLastErrorText(GetLastError()));
    }
    HWND hwnd = CreateRuntimeWindow(&ctx);
    if (hwnd == nullptr)
    {
        LogError(L"Could not create DiscordRPC control window: " + GetLastErrorText(GetLastError()));
        if (options.once)
        {
            return static_cast<int>(RunPresenceLoop(options, true, nullptr));
        }
        return 1;
    }
    g_runtimeWindow = hwnd;
    LogInfo(L"Resident control window ready.");

    try
    {
        ctx.worker = std::thread([&ctx, &options]() {
            ctx.workerExitCode = RunPresenceLoop(options, options.once, &ctx);
            ctx.shutdown.MarkWorkComplete();
            SendMessageW(ctx.hwnd, WM_DRPC_WORKER_FINISHED, 0, 0);
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
    bool messageLoopFailed = false;
    for (;;)
    {
        BOOL messageResult = GetMessageW(&msg, nullptr, 0, 0);
        if (messageResult == 0)
        {
            break;
        }
        if (messageResult == -1)
        {
            LogError(L"GetMessageW failed: " + GetLastErrorText(GetLastError()));
            messageLoopFailed = true;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_stopRequested = true;
    g_runtimeWindow = nullptr;
    if (ctx.worker.joinable())
    {
        ctx.worker.join();
    }

    return messageLoopFailed ? 1 : static_cast<int>(ctx.workerExitCode);
}

static int RunApplication(const AppOptions& options)
{
    ConfigureSingleInstanceIdentity();

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

        SignalExistingInstance(DRPC_INSTANCE_SHOW);

        PrintLine(GetUiString(L"already_running", L"DiscordRPC is already running for this config."), true);
        ReleaseSingleInstance();
        return 1;
    }

    if (!options.once)
    {
        std::wstring startupError;
        if (!ReconcileDiscordStartupShortcutFromConfig(startupError))
        {
            LogError(L"Could not synchronize this profile's shell:startup shortcut: " +
                startupError);
        }
    }

    bool trayEnabled = !options.once && !options.forceNoTray && (options.forceTray || IniReadB(L"app", L"show_tray", true));
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    int exitCode = RunWithControlWindow(options, trayEnabled);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
    ReleaseSingleInstance();
    return exitCode;
}
