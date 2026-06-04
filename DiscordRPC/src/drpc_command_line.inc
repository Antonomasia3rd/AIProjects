static void AddSetting(AppOptions& options, const std::wstring& section, const std::wstring& key, const std::wstring& value)
{
    CommandLineSetting setting;
    setting.section = section;
    setting.key = key;
    setting.value = value;
    options.settings.push_back(setting);
}

static bool ParseSetSpec(const std::wstring& spec, CommandLineSetting& setting, std::wstring& error)
{
    aip::IniSetting parsed;
    if (!aip::ParseIniSetSpec(spec, parsed, error))
    {
        return false;
    }

    setting.section = parsed.section;
    setting.key = parsed.key;
    setting.value = parsed.value;
    return true;
}

static bool IsSensitiveCommandLineSetting(const std::wstring& section, const std::wstring& key)
{
    (void)section;
    return aip::IsLikelySensitiveSettingName(key);
}

static std::wstring RedactCommandLineSettingValue(const CommandLineSetting& setting)
{
    return IsSensitiveCommandLineSetting(setting.section, setting.key) ? L"<redacted>" : setting.value;
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
        else if (aip::IsOptionOrInlineValue(arg, L"--ini"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            options.configPath = value;
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--set"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error))
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
        else if (aip::IsOptionOrInlineValue(arg, L"--client-id"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"client_id", value);
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--token-env"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"token_env", value);
            AddSetting(options, L"general", L"token", L"");
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--token"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"token", value);
            AddSetting(options, L"general", L"token_env", L"");
            options.protectToken = true;
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--status"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"status", NormalizeStatus(value));
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--update-interval"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"update_interval", value);
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--details-template"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"details_template", value);
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--state-template"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"state_template", value);
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--transport"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            AddSetting(options, L"general", L"transport_mode", NormalizeTransportMode(value));
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--bool"))
        {
            if (!aip::TakeCommandLineValue(argc, argv, i, arg, value, error))
            {
                return false;
            }
            CommandLineSetting setting;
            if (!ParseSetSpec(value, setting, error))
            {
                return false;
            }
            bool parsed = false;
            if (!aip::ParseBoolValue(setting.value, parsed))
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

static bool ApplyCommandLineSettings(const AppOptions& options)
{
    for (const auto& setting : options.settings)
    {
        if (setting.section.empty() || setting.key.empty())
        {
            continue;
        }
        if (!IniWrite(setting.section.c_str(), setting.key.c_str(), setting.value))
        {
            ReportConfigWriteFailure(L"Writing command-line setting [" + setting.section + L"] " + setting.key);
            return false;
        }
        LogInfo(L"Command-line setting: [" + setting.section + L"] " + setting.key + L" = " + RedactCommandLineSettingValue(setting));
    }
    return true;
}
