static AppOptions ParseCommandLine()
{
    AppOptions options;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
    {
        options.commandLineError = L"Windows could not parse the command line.";
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
        else
        {
            options.commandLineError = L"Unknown option: " + arg;
            break;
        }
    }

    LocalFree(argv);
    if (options.commandLineError.empty() && !options.showHelp)
    {
        int primaryModes =
            (options.showWidget ? 1 : 0) +
            (options.updateOnce ? 1 : 0) +
            (options.requestExit ? 1 : 0) +
            (options.registerPackage ? 1 : 0) +
            (options.unregisterPackage ? 1 : 0) +
            (options.launchPackaged ? 1 : 0) +
            (options.regenerateManifest ? 1 : 0);
        if (primaryModes > 1)
        {
            options.commandLineError =
                L"Choose only one primary mode: --once, --widget, --exit, --register, "
                L"--unregister, --launch-packaged, or --regenerate-manifest.";
        }
    }
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

static bool SignalExistingInstanceToExit()
{
    bool signaled = false;
    HWND hwnd = aip::FindInstanceWindow(
        WINDOW_CLASS_NAME,
        g_instanceIdentity.windowTitle.c_str(),
        20,
        50,
        false);
    if (hwnd != nullptr)
    {
        signaled = PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE || signaled;
    }

    HWND widget = FindWindowForCurrentExecutable(WIDGET_CLASS_NAME, 20, 50);
    if (widget != nullptr)
    {
        signaled = PostMessageW(widget, WM_CLOSE, 0, 0) != FALSE || signaled;
    }
    return signaled;
}
