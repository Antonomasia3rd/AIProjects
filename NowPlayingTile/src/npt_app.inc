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
    EnsureDefaultSettingsFile();
    g_restoreMessage = RegisterWindowMessageW(RESTORE_MESSAGE_NAME);

    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr) != Ok)
    {
        MessageBoxW(nullptr, L"GDI+ initialization failed.", APP_DISPLAY_NAME, MB_OK | MB_ICONERROR);
        return 1;
    }

    EnsurePackageFiles(false);

    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    }
    catch (const winrt::hresult_error& ex)
    {
        Log(L"WinRT apartment initialization failed: 0x%08X %s", static_cast<unsigned int>(ex.code()), ex.message().c_str());
    }

    AppOptions options = ParseCommandLine();
    if (options.showHelp)
    {
        ShowHelpMessage();
        winrt::uninit_apartment();
        GdiplusShutdown(g_gdiplusToken);
        return 0;
    }

    if (options.requestExit)
    {
        SignalExistingInstanceToExit();
        winrt::uninit_apartment();
        GdiplusShutdown(g_gdiplusToken);
        return 0;
    }

    if (options.regenerateManifest)
    {
        int rc = EnsurePackageFiles(true) ? 0 : 1;
        MessageBoxW(nullptr,
            rc == 0 ? L"Regenerated AppxManifest.xml and default Assets next to NowPlayingTile.exe." : L"Could not regenerate AppxManifest.xml or Assets. See NowPlayingTile.log for details.",
            APP_DISPLAY_NAME,
            MB_OK | (rc == 0 ? MB_ICONINFORMATION : MB_ICONERROR));
        winrt::uninit_apartment();
        GdiplusShutdown(g_gdiplusToken);
        return rc;
    }

    if (options.registerPackage)
    {
        int rc = RegisterDevelopmentPackage(false);
        winrt::uninit_apartment();
        GdiplusShutdown(g_gdiplusToken);
        return rc;
    }

    if (options.unregisterPackage)
    {
        int rc = UnregisterDevelopmentPackage(false);
        winrt::uninit_apartment();
        GdiplusShutdown(g_gdiplusToken);
        return rc;
    }

    if (options.launchPackaged)
    {
        int rc = LaunchPackagedInstance(false);
        winrt::uninit_apartment();
        GdiplusShutdown(g_gdiplusToken);
        return rc;
    }

    if (AutoRegisterAndLaunchIfNeeded(options))
    {
        winrt::uninit_apartment();
        GdiplusShutdown(g_gdiplusToken);
        return 0;
    }

    HANDLE mutex = nullptr;
    if (!options.allowMultiple)
    {
        mutex = CreateMutexW(nullptr, TRUE, SINGLE_INSTANCE_MUTEX_NAME);
        if (mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            HWND hwnd = FindWindowW(WINDOW_CLASS_NAME, nullptr);
            if (hwnd != nullptr && g_restoreMessage != 0)
            {
                PostMessageW(hwnd, g_restoreMessage, 0, 0);
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
