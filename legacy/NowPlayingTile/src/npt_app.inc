static int RunUpdateOnce()
{
    AppSettings settings = LoadSettings();
    MediaSnapshot snapshot = ReadMediaSnapshot();
    return TryUpdateLiveTile(snapshot, settings) ? 0 : 2;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    g_hInst = hInstance;
    InitPaths();
    g_restoreMessage = RegisterWindowMessageW(g_instanceIdentity.messageName.c_str());
    g_taskbarCreatedMessage = aip::RegisterTaskbarCreatedMessage();
    if (g_taskbarCreatedMessage == 0)
    {
        Log(L"Could not register the TaskbarCreated message: %s",
            GetLastErrorText(GetLastError()).c_str());
    }

    AppOptions options = ParseCommandLine();
    if (!options.commandLineError.empty())
    {
        std::wstring message = options.commandLineError +
            L"\r\n\r\nRun NowPlayingTile.exe --help for supported options.";
        MessageBoxW(nullptr, message.c_str(), APP_DISPLAY_NAME, MB_OK | MB_ICONERROR);
        return 2;
    }
    if (options.showHelp)
    {
        ShowHelpMessage();
        return 0;
    }

    if (options.requestExit)
    {
        return SignalExistingInstanceToExit() ? 0 : 2;
    }

    if (options.unregisterPackage)
    {
        return UnregisterDevelopmentPackage(false);
    }

    if (options.launchPackaged)
    {
        return LaunchPackagedInstance(false, PackagedLaunchArguments(options));
    }

    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr) != Ok)
    {
        MessageBoxW(nullptr, L"GDI+ initialization failed.", APP_DISPLAY_NAME, MB_OK | MB_ICONERROR);
        return 1;
    }

    if (options.regenerateManifest)
    {
        int rc = EnsurePackageFiles(true) ? 0 : 1;
        MessageBoxW(nullptr,
            rc == 0 ? L"Regenerated AppxManifest.xml and default Assets next to NowPlayingTile.exe." : L"Could not regenerate AppxManifest.xml or Assets. See NowPlayingTile.log for details.",
            APP_DISPLAY_NAME,
            MB_OK | (rc == 0 ? MB_ICONINFORMATION : MB_ICONERROR));
        GdiplusShutdown(g_gdiplusToken);
        return rc;
    }

    if (options.registerPackage)
    {
        int rc = RegisterDevelopmentPackage(false);
        GdiplusShutdown(g_gdiplusToken);
        return rc;
    }

    int bootstrapExitCode = 0;
    if (AutoRegisterAndLaunchIfNeeded(options, &bootstrapExitCode))
    {
        GdiplusShutdown(g_gdiplusToken);
        return bootstrapExitCode;
    }

    if (!options.showWidget && !EnsurePackageFiles(false))
    {
        GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    }
    catch (const winrt::hresult_error& ex)
    {
        Log(L"WinRT apartment initialization failed: 0x%08X %s", static_cast<unsigned int>(ex.code()), ex.message().c_str());
        GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    HANDLE mutex = nullptr;
    if (!options.allowMultiple)
    {
        mutex = CreateMutexW(nullptr, TRUE, g_instanceIdentity.mutexName.c_str());
        if (mutex == nullptr)
        {
            Log(L"Could not create single-instance mutex: %s", GetLastErrorText(GetLastError()).c_str());
            winrt::uninit_apartment();
            GdiplusShutdown(g_gdiplusToken);
            return 1;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            HWND hwnd = aip::FindInstanceWindow(
                WINDOW_CLASS_NAME,
                g_instanceIdentity.windowTitle.c_str(),
                20,
                50,
                false);
            if (hwnd != nullptr && g_restoreMessage != 0)
            {
                PostMessageW(hwnd, g_restoreMessage, 0, 0);
            }
            else
            {
                HWND widget = FindWindowForCurrentExecutable(WIDGET_CLASS_NAME, 20, 50);
                if (widget != nullptr)
                {
                    ShowWindow(widget, SW_RESTORE);
                    SetForegroundWindow(widget);
                }
            }
            CloseHandle(mutex);
            winrt::uninit_apartment();
            GdiplusShutdown(g_gdiplusToken);
            return 0;
        }
    }

    int rc = 0;
    if (options.updateOnce)
    {
        rc = RunUpdateOnce();
    }
    else if (options.showWidget)
    {
        rc = RunWidget(options);
    }
    else
    {
        rc = RunBackground(options);
    }

    if (mutex != nullptr)
    {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }

    try
    {
        winrt::uninit_apartment();
    }
    catch (...)
    {
    }

    GdiplusShutdown(g_gdiplusToken);
    return rc;
}
