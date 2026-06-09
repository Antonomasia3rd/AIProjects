static constexpr const wchar_t* DEFAULT_HELP_TEMPLATE =
    L"Usage:\\n"
    L"  {exe} [config.ini] [options]\\n"
    L"\\n"
    L"Options:\\n"
    L"  --help                         Show this help text.\\n"
    L"  --version                      Show version information.\\n"
    L"  --ini <path>                   Use an alternate INI file.\\n"
    L"  --exit                         Ask a running resident instance to exit.\\n"
    L"  --set Section.Key=Value        Set and save any INI value.\\n"
    L"  --bool Section.Key=Value       Set and save a boolean INI value.\\n"
    L"  --client-id <id>               Set [general] client_id (17-20 decimal digits).\\n"
    L"  --token <token>                Set [general] token, then protect it with DPAPI.\\n"
    L"  --protect-token                Migrate plaintext [general] token to token_protected.\\n"
    L"  --status <value>               Set [general] status: online, idle, dnd, invisible.\\n"
    L"  --update-interval <seconds>    Set [general] update_interval (1-86400).\\n"
    L"  --details-template <template>  Set [general] details_template.\\n"
    L"  --state-template <template>    Set [general] state_template.\\n"
    L"  --window-detection             Enable foreground-window placeholders.\\n"
    L"  --no-window-detection          Disable foreground-window placeholders.\\n"
    L"  --show-large-image             Enable large activity image.\\n"
    L"  --hide-large-image             Disable large activity image.\\n"
    L"  --show-small-image             Enable small activity image.\\n"
    L"  --hide-small-image             Disable small activity image.\\n"
    L"  --buttons                      Enable activity buttons.\\n"
    L"  --no-buttons                   Disable activity buttons.\\n"
    L"  --dry-run                      Print generated activity JSON and tokens.\\n"
    L"  --dry-run-full                 Print unredacted dry-run token values.\\n"
    L"  --dry-run-unredacted           Alias for --dry-run-full.\\n"
    L"  --once                         Send one IPC update and exit.\\n"
    L"  --verbose                      Enable verbose logging for this run.\\n"
    L"  --logging / --no-logging       Set [app] logging_enabled.\\n"
    L"  --file-logging / --no-file-logging Set [app] file_logging_enabled.\\n"
    L"  --log-lock-wait-ms <ms>        Set log append wait from 0 to 60000 ms.\\n"
    L"  --tray / --no-tray             Force tray icon on or off for this run.\\n"
    L"  --show-console / --hide-console Force console visibility.\\n"
    L"  --show-console-setting / --hide-console-setting Save console visibility.\\n"
    L"  --transport <ipc|gateway|auto> Set [general] transport_mode.\\n"
    L"\\n"
    L"Transports: ipc uses the local Discord desktop pipe; gateway uses a Discord\\n"
    L"user token stored through DPAPI token_protected; auto tries IPC first and\\n"
    L"falls back to Gateway.\\n";

static std::wstring DefaultHelpText()
{
    std::wstring rawTemplate = aip::IniConfigStore(g_iniPath).ReadRaw(
        L"CommandLineHelp",
        L"Template",
        DEFAULT_HELP_TEMPLATE);
    return aip::FormatTextTemplate(
        rawTemplate,
        { { L"{exe}", g_exeBaseName + L".exe" } });
}

static const aip::IniDefaultValue kDefaults[] =
{
    { L"general", L"transport_mode", L"ipc" },
    { L"general", L"client_id", L"" },
    { L"general", L"update_interval", L"5" },
    { L"general", L"idle_message", L"Idle" },
    { L"general", L"window_title_detection_enabled", L"true" },
    { L"general", L"fallback_details", L"Foreground program detection is disabled." },
    { L"general", L"activity_name", L"a game" },
    { L"general", L"activity_type", L"0" },
    { L"general", L"status", L"online" },
    { L"general", L"details_template", L"{win_title}" },
    { L"general", L"state_template", L"CPU: {cpu}, RAM: {ram_used}/{ram_total} ({ram_pct})" },
    { L"general", L"token_protected", L"" },
    { L"general", L"token", L"" },

    { L"censor_map", L"rule_order", L"full_replace, word_replace, pattern_replace" },
    { L"censor_map", L"full_replace", L"" },
    { L"censor_map", L"word_replace", L"" },
    { L"censor_map", L"pattern_replace", L"" },
    { L"censor_map", L"apply_pattern_on_raw", L"false" },

    { L"large_time_ranges", L"morning_start", L"5" },
    { L"large_time_ranges", L"morning_end", L"12" },
    { L"large_time_ranges", L"afternoon_start", L"12" },
    { L"large_time_ranges", L"afternoon_end", L"16" },
    { L"large_time_ranges", L"evening_start", L"16" },
    { L"large_time_ranges", L"evening_end", L"22" },
    { L"large_time_ranges", L"night_start", L"22" },
    { L"large_time_ranges", L"night_end", L"5" },

    { L"small_time_ranges", L"morning_start", L"5" },
    { L"small_time_ranges", L"morning_end", L"12" },
    { L"small_time_ranges", L"afternoon_start", L"12" },
    { L"small_time_ranges", L"afternoon_end", L"16" },
    { L"small_time_ranges", L"evening_start", L"16" },
    { L"small_time_ranges", L"evening_end", L"22" },
    { L"small_time_ranges", L"night_start", L"22" },
    { L"small_time_ranges", L"night_end", L"5" },

    { L"large_assets", L"morning_text", L"Morning" },
    { L"large_assets", L"morning_image", L"" },
    { L"large_assets", L"afternoon_text", L"Afternoon" },
    { L"large_assets", L"afternoon_image", L"" },
    { L"large_assets", L"evening_text", L"Evening" },
    { L"large_assets", L"evening_image", L"" },
    { L"large_assets", L"night_text", L"Night" },
    { L"large_assets", L"night_image", L"" },
    { L"large_assets", L"default_text", L"Online" },
    { L"large_assets", L"default_image", L"" },

    { L"small_assets", L"morning_text", L"Morning" },
    { L"small_assets", L"morning_image", L"" },
    { L"small_assets", L"afternoon_text", L"Afternoon" },
    { L"small_assets", L"afternoon_image", L"" },
    { L"small_assets", L"evening_text", L"Evening" },
    { L"small_assets", L"evening_image", L"" },
    { L"small_assets", L"night_text", L"Night" },
    { L"small_assets", L"night_image", L"" },
    { L"small_assets", L"default_text", L"Online" },
    { L"small_assets", L"default_image", L"" },

    { L"layout", L"show_large_image", L"false" },
    { L"layout", L"show_small_image", L"false" },
    { L"layout", L"show_buttons", L"false" },
    { L"layout", L"details_field", L"true" },
    { L"layout", L"state_field", L"true" },

    { L"buttons", L"button_1_label", L"Local time: {time}" },
    { L"buttons", L"button_1_url", L"https://time.is/" },
    { L"buttons", L"button_2_label", L"Discord Developer Portal" },
    { L"buttons", L"button_2_url", L"https://discord.com/developers/applications" },

    { L"afk", L"idle_threshold", L"60" },
    { L"afk", L"afk_message", L"AFK" },
    { L"afk", L"afk_image", L"" },
    { L"afk", L"show_idle_time", L"true" },
    { L"afk", L"idle_text", L"since {idle_hh}h {idle_mm}m {idle_ss}s" },

    { L"app", L"show_tray", L"true" },
    { L"app", L"show_menu_as_dropdown", L"true" },
    { L"app", L"single_instance", L"true" },
    { L"app", L"show_console", L"false" },
    { L"app", L"notifications_enabled", L"true" },
    { L"app", L"notify_on_start", L"false" },
    { L"app", L"notify_on_success", L"false" },
    { L"app", L"notify_on_failure", L"true" },
    { L"app", L"notify_on_reload", L"true" },
    { L"app", L"logging_enabled", L"true" },
    { L"app", L"file_logging_enabled", L"true" },
    { L"app", L"log_path", L"" },
    { L"app", L"log_append_lock_wait_ms", L"5000" },
    { L"app", L"ini_write_lock_wait_ms", L"5000" },
    { L"app", L"verbose_logging", L"false" },
    { L"app", L"dry_run_redact_sensitive", L"true" },

    { L"ipc", L"connect_timeout_ms", L"250" },
    { L"ipc", L"response_timeout_ms", L"5000" },

    { L"gateway", L"supported", L"true" },
    { L"gateway", L"system_locale", L"en-US" },
    { L"gateway", L"browser_user_agent", L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) discord/1.0.9181 Chrome/128.0.6613.186 Electron/32.2.7 Safari/537.36" },
    { L"gateway", L"browser_version", L"32.2.7" },
    { L"gateway", L"os_version", L"10.0.26100" },
    { L"gateway", L"release_channel", L"stable" },
    { L"gateway", L"client_build_number", L"390000" },
    { L"gateway", L"capabilities", L"65" },
    { L"gateway", L"connect_timeout_ms", L"10000" },
    { L"gateway", L"hello_timeout_ms", L"10000" },
    { L"gateway", L"ready_timeout_ms", L"30000" },
    { L"gateway", L"send_timeout_ms", L"10000" },
    { L"gateway", L"close_timeout_ms", L"2000" },
    { L"gateway", L"asset_fetch_timeout_ms", L"10000" },
    { L"gateway", L"asset_fetch_user_agent", L"DiscordBot (https://github.com, 1)" },

    { L"strings", L"tray_tip", L"DiscordRPC" },
    { L"strings", L"menu_status", L"DiscordRPC" },
    { L"strings", L"menu_refresh", L"Refresh now" },
    { L"strings", L"menu_reload", L"Reload configuration" },
    { L"strings", L"menu_open_config", L"Open config" },
    { L"strings", L"menu_open_log_folder", L"Open log folder" },
    { L"strings", L"menu_recent_log", L"Recent log" },
    { L"strings", L"menu_exit", L"Exit" },
    { L"strings", L"menu_shutdown_pending", L"Shutdown pending: waiting for Discord cleanup" },
    { L"strings", L"menu_force_shutdown", L"Force shutdown" },
    { L"strings", L"menu_general", L"General" },
    { L"strings", L"menu_presence", L"Presence" },
    { L"strings", L"menu_diagnostics", L"Diagnostics" },
    { L"strings", L"menu_logging", L"Logging" },
    { L"strings", L"menu_update_interval", L"Update interval" },
    { L"strings", L"menu_transport", L"Transport" },
    { L"strings", L"menu_transport_ipc", L"Discord IPC" },
    { L"strings", L"menu_transport_gateway", L"Gateway" },
    { L"strings", L"menu_transport_auto", L"Auto: IPC then Gateway" },
    { L"strings", L"menu_dropdown", L"Show menu as dropdown" },
    { L"strings", L"window_detection", L"Window detection" },
    { L"strings", L"details_field", L"Details field" },
    { L"strings", L"state_field", L"State field" },
    { L"strings", L"large_image", L"Large image" },
    { L"strings", L"small_image", L"Small image" },
    { L"strings", L"buttons", L"Buttons" },
    { L"strings", L"logging_enabled", L"Logging enabled" },
    { L"strings", L"file_logging", L"File logging" },
    { L"strings", L"log_lock_wait", L"Log lock wait" },
    { L"strings", L"verbose_logging", L"Verbose logging" },
    { L"strings", L"show_console", L"Show console" },
    { L"strings", L"no_recent_log", L"No log entries yet." },
    { L"strings", L"recent_log_title", L"DiscordRPC recent log" },
    { L"strings", L"notification_title", L"DiscordRPC" },
    { L"strings", L"notification_started", L"Rich Presence is running." },
    { L"strings", L"notification_connected", L"Rich Presence is active through Discord IPC." },
    { L"strings", L"notification_connected_gateway", L"Rich Presence is active through Discord Gateway." },
    { L"strings", L"notification_failed", L"Rich Presence update failed." },
    { L"strings", L"notification_reloaded", L"Configuration reloaded." },
    { L"strings", L"notification_gateway_missing_token", L"Gateway mode needs token_protected or token." },
    { L"strings", L"notification_gateway_unsupported", L"Discord Gateway transport is disabled by [gateway] supported." },
    { L"strings", L"notification_gateway_auth_failed", L"Discord Gateway authentication failed. Check your token." },
    { L"strings", L"notification_shutdown_pending", L"Shutdown requested. Use Force shutdown from the tray if Discord cleanup does not finish." },
    { L"strings", L"previous_force_shutdown_warning", L"The previous run was force-closed while Discord cleanup was pending. Presence clearing or the Gateway close handshake may not have completed." },
    { L"strings", L"already_running", L"DiscordRPC is already running for this config." },
    { L"strings", L"reload_requested", L"DiscordRPC configuration reload requested." },
    { L"strings", L"missing_client_id", L"Missing [general] client_id. Create a Discord application and paste its Application ID into the INI." },
    { L"strings", L"dry_run_header", L"=== Discord activity JSON ===" },
    { L"strings", L"dry_run_tokens", L"=== Available template tokens ===" },
    { L"strings", L"dry_run_redacted_note", L"Sensitive local tokens are redacted. Use --dry-run-full to show raw values." },

    { L"CommandLineHelp", L"Template", DEFAULT_HELP_TEMPLATE }
};

static bool EnsureDefaultConfigFile()
{
    if (!aip::IniConfigStore(g_iniPath, DefaultConfigHeader(), g_iniWriteLockWaitMs.load()).EnsureDefaults(
        kDefaults,
        ARRAYSIZE(kDefaults)))
    {
        ReportConfigWriteFailure(L"Creating or updating default DiscordRPC config");
        return false;
    }
    return true;
}
