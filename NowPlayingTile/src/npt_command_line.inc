static AppOptions ParseCommandLine()
{
    AppOptions options;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
    {
        return options;
    }

    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        if (EqualsIgnoreCase(arg, L"--widget") || EqualsIgnoreCase(arg, L"--show") || EqualsIgnoreCase(arg, L"/widget") || EqualsIgnoreCase(arg, L"/show"))
        {
            options.showWidget = true;
        }
        else if (EqualsIgnoreCase(arg, L"--once") || EqualsIgnoreCase(arg, L"/once"))
        {
            options.updateOnce = true;
        }
        else if (EqualsIgnoreCase(arg, L"--allow-multiple") || EqualsIgnoreCase(arg, L"/allow-multiple"))
        {
            options.allowMultiple = true;
        }
        else if (EqualsIgnoreCase(arg, L"--exit") || EqualsIgnoreCase(arg, L"--quit") || EqualsIgnoreCase(arg, L"/exit") || EqualsIgnoreCase(arg, L"/quit"))
        {
            options.requestExit = true;
        }
        else if (EqualsIgnoreCase(arg, L"--register") || EqualsIgnoreCase(arg, L"/register"))
        {
            options.registerPackage = true;
        }
        else if (EqualsIgnoreCase(arg, L"--unregister") || EqualsIgnoreCase(arg, L"/unregister"))
        {
            options.unregisterPackage = true;
        }
        else if (EqualsIgnoreCase(arg, L"--launch-packaged") || EqualsIgnoreCase(arg, L"--launch") || EqualsIgnoreCase(arg, L"/launch"))
        {
            options.launchPackaged = true;
        }
        else if (EqualsIgnoreCase(arg, L"--regenerate-manifest") || EqualsIgnoreCase(arg, L"/regenerate-manifest"))
        {
            options.regenerateManifest = true;
        }
        else if (EqualsIgnoreCase(arg, L"--tray"))
        {
            options.forceTray = true;
            options.forceNoTray = false;
        }
        else if (EqualsIgnoreCase(arg, L"--no-tray"))
        {
            options.forceNoTray = true;
            options.forceTray = false;
        }
        else if (EqualsIgnoreCase(arg, L"--help") || EqualsIgnoreCase(arg, L"-h") || EqualsIgnoreCase(arg, L"/?"))
        {
            options.showHelp = true;
        }
    }

    LocalFree(argv);
    return options;
}

static void ShowHelpMessage()
{
    MessageBoxW(
        nullptr,
        L"NowPlayingTile\r\n\r\n"
        L"Options:\r\n"
        L"  --once              Read SMTC once, update the Live Tile, then exit.\r\n"
        L"  --widget            Open the optional visible diagnostic widget.\r\n"
        L"  --tray              Force tray icon on for this run.\r\n"
        L"  --no-tray           Force tray icon off for this run.\r\n"
        L"  --allow-multiple    Skip the single-instance guard.\r\n"
        L"  --exit, --quit      Ask the running instance to exit.\r\n"
        L"  --register          Generate Appx files and register the loose package.\r\n"
        L"  --unregister        Remove the registered loose package.\r\n"
        L"  --launch-packaged   Launch through shell:AppsFolder for package identity.\r\n"
        L"  --regenerate-manifest  Rewrite AppxManifest.xml and default Assets.\r\n"
        L"\r\nSettings live next to the executable in NowPlayingTile.ini.",
        APP_DISPLAY_NAME,
        MB_OK | MB_ICONINFORMATION);
}

static void SignalExistingInstanceToExit()
{
    HWND hwnd = FindWindowW(WINDOW_CLASS_NAME, nullptr);
    if (hwnd != nullptr)
    {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }

    HWND widget = FindWindowW(WIDGET_CLASS_NAME, nullptr);
    if (widget != nullptr)
    {
        PostMessageW(widget, WM_CLOSE, 0, 0);
    }
}
