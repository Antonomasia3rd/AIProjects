static bool AddNowPlayingCommandLineSetting(
    AppOptions& options,
    const std::wstring& section,
    const std::wstring& key,
    const std::wstring& value,
    std::wstring& error)
{
    aip::IniSetting setting{ section, key, value };
    if (!NormalizeNowPlayingSetting(setting, error))
        return false;
    AddOrReplaceNowPlayingSetting(options, setting);
    return true;
}

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
        if (EqualsIgnoreCase(argv[i], L"--help") ||
            EqualsIgnoreCase(argv[i], L"-h") ||
            EqualsIgnoreCase(argv[i], L"/?"))
        {
            options.showHelp = true;
        }
        else if (EqualsIgnoreCase(argv[i], L"--version"))
        {
            options.showVersion = true;
        }
    }
    if (options.showHelp || options.showVersion)
    {
        LocalFree(argv);
        return options;
    }

    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg = argv[i];
        std::wstring value;
        std::wstring error;
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
        else if (aip::IsOptionOrInlineValue(arg, L"--set"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                options.commandLineError = error;
                break;
            }
            aip::IniSetting setting;
            if (!aip::ParseIniSetSpec(value, setting, error) ||
                !NormalizeNowPlayingSetting(setting, error))
            {
                options.commandLineError = error;
                break;
            }
            AddOrReplaceNowPlayingSetting(options, setting);
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--tile-layout"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error) ||
                !AddNowPlayingCommandLineSetting(options, L"Settings", L"TileLayout", value, error))
            {
                options.commandLineError = error;
                break;
            }
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--update-interval"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error) ||
                !AddNowPlayingCommandLineSetting(options, L"Settings", L"UpdateIntervalSeconds", value, error))
            {
                options.commandLineError = error;
                break;
            }
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--tile-refresh"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error) ||
                !AddNowPlayingCommandLineSetting(options, L"Settings", L"TileRefreshSeconds", value, error))
            {
                options.commandLineError = error;
                break;
            }
        }
        else if (EqualsIgnoreCase(arg, L"--tray") || EqualsIgnoreCase(arg, L"--no-tray"))
        {
            if (!AddNowPlayingCommandLineSetting(options, L"Settings", L"ShowTrayIcon", EqualsIgnoreCase(arg, L"--tray") ? L"true" : L"false", error))
            {
                options.commandLineError = error;
                break;
            }
        }
        else if (EqualsIgnoreCase(arg, L"--startup") || EqualsIgnoreCase(arg, L"--no-startup"))
        {
            if (!AddNowPlayingCommandLineSetting(options, L"Settings", L"RunAtStartup", EqualsIgnoreCase(arg, L"--startup") ? L"true" : L"false", error))
            {
                options.commandLineError = error;
                break;
            }
        }
        else if (EqualsIgnoreCase(arg, L"--dropdown") || EqualsIgnoreCase(arg, L"--no-dropdown"))
        {
            if (!AddNowPlayingCommandLineSetting(options, L"Settings", L"ShowMenuAsDropdown", EqualsIgnoreCase(arg, L"--dropdown") ? L"true" : L"false", error))
            {
                options.commandLineError = error;
                break;
            }
        }
        else
        {
            options.commandLineError = L"Unknown option: " + arg;
            break;
        }
    }

    LocalFree(argv);
    if (options.commandLineError.empty())
    {
        const int primaryModes =
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
        else if (!options.settings.empty() &&
                 (options.registerPackage || options.unregisterPackage || options.regenerateManifest))
        {
            options.commandLineError =
                L"Persistent settings cannot be combined with --register, --unregister, or --regenerate-manifest.";
        }
    }
    return options;
}

static std::wstring CommandLineUsageText()
{
    return
        L"NowPlayingTile\r\n\r\n"
        L"Options:\r\n"
        L"  --help, -h, /?          Show this help without changing files or Startup.\r\n"
        L"  --version               Show the application version.\r\n"
        L"  --once                  Read SMTC once, update the Live Tile, then exit.\r\n"
        L"  --widget                Open the optional visible diagnostic widget.\r\n"
        L"  --tray | --no-tray      Persist tray icon visibility.\r\n"
        L"  --startup | --no-startup Enable or disable the packaged StartupTask.\r\n"
        L"  --dropdown | --no-dropdown Persist tray section layout.\r\n"
        L"  --tile-layout <value>   Text, Artwork, Combined, or Cycle.\r\n"
        L"  --update-interval <n>   Persist media polling interval (1..60 seconds).\r\n"
        L"  --tile-refresh <n>      Persist tile refresh interval (1..300 seconds).\r\n"
        L"  --set Settings.Key=Value Persist any known typed setting.\r\n"
        L"  --allow-multiple        Skip the single-instance guard.\r\n"
        L"  --exit, --quit          Apply settings, then ask the running instance to exit.\r\n"
        L"  --register              Generate Appx files and register the loose package.\r\n"
        L"  --unregister            Remove the registered loose package.\r\n"
        L"  --launch-packaged       Launch through shell:AppsFolder for package identity.\r\n"
        L"  --regenerate-manifest   Rewrite AppxManifest.xml and default Assets.\r\n"
        L"\r\nSettings live next to the executable in NowPlayingTile.ini.";
}

static void ShowCommandLineText(const std::wstring& text, bool error)
{
    if (!aip::WriteCommandLineText(text, error))
    {
        MessageBoxW(
            nullptr,
            text.c_str(),
            APP_DISPLAY_NAME,
            MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
    }
}

static void ShowHelpMessage()
{
    ShowCommandLineText(CommandLineUsageText(), false);
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
