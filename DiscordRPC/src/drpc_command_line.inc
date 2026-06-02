static void AddSetting(AppOptions& options, const std::wstring& section, const std::wstring& key, const std::wstring& value)
{
    CommandLineSetting setting;
    setting.section = section;
    setting.key = key;
    setting.value = value;
    options.settings.push_back(setting);
}

static bool TakeCommandLineValue(int argc, wchar_t** argv, int& i, const std::wstring& option, std::wstring& value, std::wstring& error)
{
    size_t eq = option.find(L'=');
    if (eq != std::wstring::npos)
    {
        value = option.substr(eq + 1);
        return true;
    }

    if (i + 1 >= argc)
    {
        error = L"Missing value after " + option + L".";
        return false;
    }

    value = argv[++i];
    return true;
}

static bool ParseSetSpec(const std::wstring& spec, CommandLineSetting& setting, std::wstring& error)
{
    size_t equals = spec.find(L'=');
    if (equals == std::wstring::npos)
    {
        error = L"--set expects Section.Key=Value.";
        return false;
    }

    std::wstring name = spec.substr(0, equals);
    size_t dot = name.find(L'.');
    if (dot == std::wstring::npos || dot == 0 || dot + 1 >= name.size())
    {
        error = L"--set expects Section.Key=Value.";
        return false;
    }

    setting.section = name.substr(0, dot);
    setting.key = name.substr(dot + 1);
    setting.value = spec.substr(equals + 1);
    return true;
}

static bool ParseBoolValue(const std::wstring& raw, bool& value)
{
    std::wstring v = ToLower(Trim(raw));
    if (v == L"1" || v == L"true" || v == L"yes" || v == L"on")
    {
        value = true;
        return true;
    }
    if (v == L"0" || v == L"false" || v == L"no" || v == L"off")
    {
        value = false;
        return true;
    }
    return false;
}

static bool ParseCommandLine(AppOptions& options, std::wstring& error)
{
    int argc = 0;
    LPWSTR* rawArgv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (rawArgv == nullptr)
    {
        error = L"CommandLineToArgvW failed: " + GetLastErrorText(GetLastError());
        return false;
    }

    std::unique_ptr<LPWSTR, decltype(&LocalFree)> argvHolder(rawArgv, LocalFree);
    wchar_t** argv = rawArgv;

    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        std::wstring lower = ToLower(arg);
        std::wstring value;

        if (lower == L"--help" || lower == L"-h" || lower == L"/?")
        {
            options.showHelp = true;
        }
        else if (lower == L"--version")
        {
            options.showVersion = true;
        }
        else if (lower == L"--dry-run")
        {
            options.dryRun = true;
        }
        else if (lower == L"--dry-run-full" || lower == L"--dry-run-unredacted")
        {
            options.dryRun = true;
            options.dryRunFull = true;
        }
        else if (lower == L"--verbose")
        {
            options.verbose = true;
        }
        else if (lower == L"--once")
        {
            options.once = true;
        }
        else if (lower == L"--no-tray")
        {
            options.forceNoTray = true;
            options.forceTray = false;
        }
        else if (lower == L"--tray")
        {
            options.forceTray = true;
            options.forceNoTray = false;
        }
        else if (lower == L"--show-console")
        {
            options.forceShowConsole = true;
            options.forceHideConsole = false;
        }
        else if (lower == L"--hide-console")
        {
            options.forceHideConsole = true;
            options.forceShowConsole = false;
        }
        else if (lower == L"--exit")
        {
            options.requestExit = true;
        }
        else if (lower == L"--protect-token")
        {
            options.protectToken = true;
        }
        else if (lower == L"--window-detection")
        {
            AddSetting(options, L"general", L"window_title_detection_enabled", L"true");
        }
        else if (lower == L"--no-window-detection")
        {
            AddSetting(options, L"general", L"window_title_detection_enabled", L"false");
        }
        else if (lower == L"--show-large-image")
        {
            AddSetting(options, L"layout", L"show_large_image", L"true");
        }
        else if (lower == L"--hide-large-image")
        {
            AddSetting(options, L"layout", L"show_large_image", L"false");
        }
        else if (lower == L"--show-small-image")
        {
            AddSetting(options, L"layout", L"show_small_image", L"true");
        }
        else if (lower == L"--hide-small-image")
        {
            AddSetting(options, L"layout", L"show_small_image", L"false");
        }
        else if (lower == L"--buttons")
        {
            AddSetting(options, L"layout", L"show_buttons", L"true");
        }
        else if (lower == L"--no-buttons")
        {
            AddSetting(options, L"layout", L"show_buttons", L"false");
        }
        else if (lower == L"--logging")
        {
            AddSetting(options, L"app", L"logging_enabled", L"true");
        }
        else if (lower == L"--no-logging")
        {
            AddSetting(options, L"app", L"logging_enabled", L"false");
        }
        else if (lower == L"--file-logging")
        {
            AddSetting(options, L"app", L"file_logging_enabled", L"true");
        }
        else if (lower == L"--no-file-logging")
        {
            AddSetting(options, L"app", L"file_logging_enabled", L"false");
        }
        else if (lower == L"--show-console-setting")
        {
            AddSetting(options, L"app", L"show_console", L"true");
        }
        else if (lower == L"--hide-console-setting")
        {
            AddSetting(options, L"app", L"show_console", L"false");
        }
        else if (StartsWithI(arg, L"--ini"))
        {
            if (!TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            options.configPath = value;
        }
        else if (StartsWithI(arg, L"--set"))
        {
            if (!TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            CommandLineSetting setting;
            if (!ParseSetSpec(value, setting, error))
            {
                return false;
            }
            options.settings.push_back(setting);
        }
        else if (StartsWithI(arg, L"--client-id"))
        {
            if (!TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"client_id", value);
        }
        else if (StartsWithI(arg, L"--token-env"))
        {
            if (!TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"token_env", value);
            AddSetting(options, L"general", L"token", L"");
        }
        else if (StartsWithI(arg, L"--token"))
        {
            if (!TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"token", value);
            AddSetting(options, L"general", L"token_env", L"");
            options.protectToken = true;
        }
        else if (StartsWithI(arg, L"--status"))
        {
            if (!TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"status", NormalizeStatus(value));
        }
        else if (StartsWithI(arg, L"--update-interval"))
        {
            if (!TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"update_interval", value);
        }
        else if (StartsWithI(arg, L"--details-template"))
        {
            if (!TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"details_template", value);
        }
        else if (StartsWithI(arg, L"--state-template"))
        {
            if (!TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"state_template", value);
        }
        else if (StartsWithI(arg, L"--transport"))
        {
            if (!TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"transport_mode", NormalizeTransportMode(value));
        }
        else if (StartsWithI(arg, L"--bool"))
        {
            if (!TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            CommandLineSetting setting;
            if (!ParseSetSpec(value, setting, error))
            {
                return false;
            }
            bool parsed = false;
            if (!ParseBoolValue(setting.value, parsed))
            {
                error = L"--bool expects a true/false value.";
                return false;
            }
            setting.value = parsed ? L"true" : L"false";
            options.settings.push_back(setting);
        }
        else if (!arg.empty() && (arg[0] == L'-' || arg[0] == L'/'))
        {
            error = L"Unknown argument: " + arg;
            return false;
        }
        else if (options.configPath.empty())
        {
            options.configPath = arg;
        }
        else
        {
            error = L"Unexpected positional argument: " + arg;
            return false;
        }
    }

    return true;
}

static void ApplyCommandLineSettings(const AppOptions& options)
{
    for (const auto& setting : options.settings)
    {
        if (setting.section.empty() || setting.key.empty())
        {
            continue;
        }
        IniWrite(setting.section.c_str(), setting.key.c_str(), setting.value);
        LogInfo(L"Command-line setting: [" + setting.section + L"] " + setting.key + L" = " + setting.value);
    }
}
