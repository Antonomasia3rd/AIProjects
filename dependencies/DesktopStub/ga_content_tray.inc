// Declarative choices are shared with the portable configuration model.
#ifndef DESKTOPSTUB_CONTENT_MESSAGEBOX
#define DESKTOPSTUB_CONTENT_MESSAGEBOX MessageBoxW
#endif
static constexpr UINT ID_CONTENT_OPTIONS = 19000;
static constexpr UINT ID_CONTENT_ADD = 19010;
static constexpr UINT ID_CONTENT_TEXT_MODE = 19020;
static constexpr UINT ID_CONTENT_ENTRY_OPTIONS = 20000;
static constexpr UINT CONTENT_ENTRY_COMMANDS = 64;
static constexpr UINT ID_NOTES_SETTINGS = 24000;
static constexpr UINT ID_DISCORD_SETTINGS = 25000;
static constexpr UINT ID_CAPS_SETTINGS = 26000;
static constexpr UINT ID_ASUS_SETTINGS = 27000;
static const wchar_t* g_notesOptionKeys[] = {L"RefreshSeconds", L"HttpTimeoutSeconds", L"MaximumBytes"};
static const wchar_t* g_notesOptionDefaults[] = {L"300", L"15", L"1048576"};
static const wchar_t* g_notesOptionLabels[] = {L"Refresh (seconds)", L"HTTP timeout (seconds)", L"Response limit (bytes)"};
static const wchar_t* g_notesAccountKeys[] = {L"UID", L"LToken", L"LTUid"};
struct ContentTrayOption { const wchar_t* key; const wchar_t* label; const wchar_t* fallback; bool toggle; };
static const ContentTrayOption g_capsOptions[] = {
    {L"HardwareEnabled", L"Control physical Caps indicator", L"0", true},
    {L"KeyboardTargetPath", L"Keyboard device", L"\\Device\\KeyboardClass0", false},
    {L"BlinkIntervalMs", L"Blink interval (milliseconds)", L"500", false},
};
static const ContentTrayOption g_asusOptions[] = {
    {L"HardwareEnabled", L"Control ASUS indicator hardware", L"0", true},
    {L"ReadDiskActivity", L"Read disk activity for HDD patterns", L"0", true},
    {L"Paused", L"Pause patterns", L"0", true},
    {L"ProfilePath", L"Advanced ASUS INI (empty uses fields below)", L"", false},
    {L"MicState", L"Mic indicator states (off or 0/1 list)", L"off", false},
    {L"MicInterval", L"Mic intervals (milliseconds or time units)", L"0", false},
    {L"MicDuration", L"Mic duration (once, 0 or time)", L"once", false},
    {L"KeyboardState", L"Keyboard states (off or 0-255 list)", L"off", false},
    {L"KeyboardInterval", L"Keyboard intervals", L"0", false},
    {L"KeyboardDuration", L"Keyboard duration (once, 0 or time)", L"once", false},
    {L"ErrorRetry", L"Retries", L"3", false},
    {L"ErrorAction", L"On error (continue, pause, exit, crash)", L"continue", false},
};
static const ContentTrayOption g_discordOptions[] = {
    {L"SendPresence", L"Send presence to Discord", L"0", true},
    {L"ProfilePath", L"Advanced Discord INI (empty uses fields below)", L"", false},
    {L"ClientId", L"Application/client ID", L"", false},
    {L"Name", L"Activity name", L"DesktopStub", false},
    {L"Details", L"Details template", L"{win_title}", false},
    {L"State", L"State template", L"CPU: {cpu}", false},
    {L"RefreshSeconds", L"Refresh (seconds)", L"5", false},
    {L"OperationTimeoutMs", L"Operation timeout (milliseconds)", L"5000", false},
    {L"ClearOnStop", L"Clear sent presence on shutdown", L"1", true},
};
static const ContentTrayOption g_contentOptions[] = {
    {L"Enabled", L"Use layered content", L"0", true},
    {L"Count", L"Number of contents (1-32)", L"1", false},
    {L"CycleEnabled", L"Cycle enabled contents", L"0", true},
    {L"CycleSeconds", L"Cycle interval (seconds)", L"10", false},
    {L"RefreshSeconds", L"Source refresh (seconds)", L"2", false},
    {L"TextMode", L"Text delivery", L"Auto", false},
};
static const ContentTrayOption g_contentEntryOptions[] = {
    {L"Enabled", L"Include in cycle", L"1", true},
    {L"Name", L"Name", L"Content", false},
    {L"Background", L"Background", L"Wallpaper", false},
    {L"ImagePath", L"Choose background image...", L"", false},
    {L"TextSources", L"Text sources", L"", false},
    {L"Text", L"Custom primary text", L"", false},
    {L"SecondaryText", L"Custom secondary text", L"", false},
    {L"BadgeText", L"Custom badge text", L"", false},
    {L"RssItem", L"RSS headline number (1-20)", L"1", false},
    {L"NotesResource", L"Notes game/resource", L"Resin", false},
};

static std::wstring ContentMenuValue(std::wstring value)
{
    for (auto& ch : value) if (ch == L'\r' || ch == L'\n') ch = L' ';
    if (value.size() > 72) {
        value.resize(69);
        if (!value.empty() && value.back() >= 0xD800 && value.back() <= 0xDBFF) value.pop_back();
        value += L"...";
    }
    return value.empty() ? L"(empty)" : value;
}

static void AppendContentSubmenu(HMENU menu, HMENU child, const std::wstring& label)
{
    if (child && !AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(child), label.c_str())) DestroyMenu(child);
}

static void AppendContentEngineMenu(HMENU menu)
{
    auto append = [](HMENU target, UINT id, const wchar_t* section, const ContentTrayOption& option) {
        const auto value = IniReadS(section, option.key, option.fallback);
        bool checked = false;
        aip::content::ParseBool(value, checked);
        auto label = std::wstring(option.label);
        if (!option.toggle) label += L": " + ContentMenuValue(value);
        aip::AppendTrayMenuItem(target, id, label, option.toggle && checked, true);
    };
    for (UINT i = 0; i < _countof(g_contentOptions); ++i) {
        if (IEquals(g_contentOptions[i].key, L"TextMode")) {
            HMENU choices = CreatePopupMenu();
            if (!choices) continue;
            const auto value = IniReadS(L"Content", L"TextMode", L"Auto");
            for (UINT choice = 0; choice < _countof(aip::content::TextModes); ++choice)
                aip::AppendTrayMenuItem(choices, ID_CONTENT_TEXT_MODE + choice,
                    aip::content::TextModes[choice].label, IEquals(value, aip::content::TextModes[choice].value), true);
            AppendContentSubmenu(menu, choices, L"Text delivery");
        } else append(menu, ID_CONTENT_OPTIONS + i, L"Content", g_contentOptions[i]);
    }
    const int count = IniReadClampedI(L"Content", L"Count", 1, 1, aip::content::MaxEntries);
    aip::AppendTrayMenuItem(menu, ID_CONTENT_ADD, L"Add content", false, count < aip::content::MaxEntries);
    HMENU notes = CreatePopupMenu();
    if (notes) {
        for (UINT i = 0; i < 3; ++i)
            aip::AppendTrayMenuItem(notes, ID_NOTES_SETTINGS + i,
                std::wstring(g_notesOptionLabels[i]) + L": " + IniReadS(L"Notes", g_notesOptionKeys[i], g_notesOptionDefaults[i]), false, true);
        for (UINT resource = 0; resource < 3; ++resource) {
            HMENU accountMenu = CreatePopupMenu();
            if (!accountMenu) continue;
            const auto section = L"Notes." + std::wstring(aip::content::NotesResources[resource].value);
            for (UINT field = 0; field < 3; ++field) {
                const auto value = IniReadS(section.c_str(), g_notesAccountKeys[field], L"");
                const auto display = field == 0 ? ContentMenuValue(value) : value.empty() ? L"not configured" : L"configured";
                aip::AppendTrayMenuItem(accountMenu, ID_NOTES_SETTINGS + 16 + resource * 8 + field,
                    std::wstring(g_notesAccountKeys[field]) + L": " + display, false, true);
            }
            AppendContentSubmenu(notes, accountMenu, aip::content::NotesResources[resource].label);
        }
        AppendContentSubmenu(menu, notes, L"Real-Time Notes accounts/settings");
    }
    HMENU discord = CreatePopupMenu();
    if (discord) {
        for (UINT option = 0; option < _countof(g_discordOptions); ++option)
            append(discord, ID_DISCORD_SETTINGS + option, L"Discord", g_discordOptions[option]);
        AppendContentSubmenu(menu, discord, L"Discord presence settings (preview until sending enabled)");
    }
    HMENU caps = CreatePopupMenu();
    if (caps) {
        for (UINT option = 0; option < _countof(g_capsOptions); ++option)
            append(caps, ID_CAPS_SETTINGS + option, L"CapsBlink", g_capsOptions[option]);
        aip::AppendTrayMenuItem(caps, ID_CAPS_SETTINGS + 30, L"Restart indicator pattern", false, true);
        AppendContentSubmenu(menu, caps, L"Caps indicator settings");
    }
    HMENU asus = CreatePopupMenu();
    if (asus) {
        for (UINT option = 0; option < _countof(g_asusOptions); ++option)
            append(asus, ID_ASUS_SETTINGS + option, L"AsusBlink", g_asusOptions[option]);
        aip::AppendTrayMenuItem(asus, ID_ASUS_SETTINGS + 30, L"Restart patterns", false, true);
        AppendContentSubmenu(menu, asus, L"ASUS indicator settings");
    }
    for (int entry = 1; entry <= count; ++entry) {
        auto section = L"Content." + std::to_wstring(entry);
        const UINT base = ID_CONTENT_ENTRY_OPTIONS + static_cast<UINT>(entry - 1) * CONTENT_ENTRY_COMMANDS;
        HMENU child = CreatePopupMenu();
        if (!child) continue;
        for (UINT i = 0; i < _countof(g_contentEntryOptions); ++i) {
            if (i == 2 || i == 4 || i == 9) {
                HMENU choices = CreatePopupMenu();
                if (!choices) continue;
                if (i == 2) {
                    const auto background = IniReadS(section.c_str(), L"Background", L"Wallpaper");
                    for (UINT choice = 0; choice < _countof(aip::content::Backgrounds); ++choice)
                        aip::AppendTrayMenuItem(choices, base + 16 + choice, aip::content::Backgrounds[choice].label,
                            IEquals(background, aip::content::Backgrounds[choice].value), true);
                } else if (i == 4) {
                    std::vector<std::wstring> sources;
                    aip::content::ParseSources(IniReadS(section.c_str(), L"TextSources", L""), sources);
                    for (UINT choice = 0; choice < _countof(aip::content::Sources); ++choice)
                        aip::AppendTrayMenuItem(choices, base + 24 + choice, aip::content::Sources[choice].label,
                            std::find(sources.begin(), sources.end(), aip::content::Sources[choice].value) != sources.end(), true);
                    aip::AppendTrayMenuItem(choices, base + 4, L"Edit source order...", false, true);
                } else {
                    const auto selected = IniReadS(section.c_str(), L"NotesResource", L"Resin");
                    for (UINT resource = 0; resource < 3; ++resource)
                        aip::AppendTrayMenuItem(choices, base + 40 + resource, aip::content::NotesResources[resource].label,
                            IEquals(selected, aip::content::NotesResources[resource].value), true);
                }
                AppendContentSubmenu(child, choices, g_contentEntryOptions[i].label);
            } else append(child, base + i, section.c_str(), g_contentEntryOptions[i]);
        }
        aip::AppendTrayMenuItem(child, base + 32, L"Move earlier in cycle", false, entry > 1);
        aip::AppendTrayMenuItem(child, base + 33, L"Move later in cycle", false, entry < count);
        auto label = std::to_wstring(entry) + L". " + ContentMenuValue(IniReadS(section.c_str(), L"Name", L"Content"));
        AppendContentSubmenu(menu, child, label);
    }
}

// A menu action works from one file snapshot and commits once. Concurrent
// writers cannot get half of an image/path update or overwrite a reorder.
template<class Mutator> static bool CommitContentMenuMutation(HWND owner, Mutator mutate)
{
    std::wstring original, error;
    if (!ReadIniFileForMutation(original)) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"The configuration could not be read.", L"Content and sources", MB_OK | MB_ICONERROR); return false;
    }
    auto candidate = original;
    if (!mutate(candidate, error)) {
        if (!error.empty()) DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Invalid content setting", MB_OK | MB_ICONWARNING);
        return false;
    }
    auto readConfig = [](const std::wstring& text, aip::content::Configuration& cfg, std::wstring& issue) {
        const auto doc = aip::ParseIniDocument(text);
        return aip::content::ReadConfiguration([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            std::wstring value;
            return aip::ReadIniValueFromDoc(doc, section, key, value) ? value : std::wstring(fallback);
        }, cfg, issue);
    };
    aip::content::Configuration before, after;
    std::wstring ignored;
    readConfig(original, before, ignored);
    if (!readConfig(candidate, after, error)) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Invalid content setting", MB_OK | MB_ICONWARNING); return false;
    }
    for (const auto& entry : after.entries) {
        if (after.enabled && entry.enabled && std::find(entry.sources.begin(), entry.sources.end(), L"RssFeed") != entry.sources.end() &&
            !ValidateConfiguredRssSettingsForContentSource(ContentSource::RssFeed, error)) {
            DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"RSS configuration", MB_OK | MB_ICONWARNING); return false;
        }
    }
    const auto warning = aip::content::TimingChangeWarning(before, after, UseExperimentalLiveTileUpdate());
    if (!warning.empty() && DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, (warning + L"\r\n\r\nApply this setting?").c_str(), L"Content cycling", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES) return false;
    if (!MutateDesktopStubIniWithWait([&](std::wstring& current) {
        if (current != original) { SetLastError(ERROR_RETRY); return false; }
        current = candidate; return true;
    }, CurrentIniWriteLockWaitMs())) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"The configuration could not be saved, or another writer changed it. Reopen the menu and try again.", L"Content and sources", MB_OK | MB_ICONWARNING); return false;
    }
    LogText(L"Content configuration updated.");
    WakePollThread();
    return true;
}

static bool SaveContentMenuSettings(HWND owner, const std::vector<aip::IniSetting>& settings)
{
    return CommitContentMenuMutation(owner, [&](std::wstring& text, std::wstring& error) {
        for (const auto& setting : settings) {
            auto value = setting.value;
            if (!aip::content::NormalizeSetting(setting.section, setting.key, value, error) ||
                !WriteIniValueToText(text, setting.section.c_str(), setting.key.c_str(), value.c_str())) return false;
        }
        return true;
    });
}

static bool ChooseContentImage(HWND owner, const std::wstring& section)
{
    std::vector<wchar_t> path(32768, L'\0');
    const auto current = IniReadS(section.c_str(), L"ImagePath", L"");
    wcsncpy_s(path.data(), path.size(), current.c_str(), _TRUNCATE);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0All files\0*.*\0\0";
    dialog.lpstrFile = path.data(); dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Choose content background";
    if (!GetOpenFileNameW(&dialog)) return false;
    return SaveContentMenuSettings(owner, {{section, L"ImagePath", path.data()}, {section, L"Background", L"Image"}});
}

static bool MoveContentMenuEntry(HWND owner, int first, int second)
{
    if (first < 1 || second < 1 || first > aip::content::MaxEntries || second > aip::content::MaxEntries) return false;
    return CommitContentMenuMutation(owner, [&](std::wstring& text, std::wstring&) {
        const auto a = L"Content." + std::to_wstring(first), b = L"Content." + std::to_wstring(second);
        auto lines = aip::SplitIniLines(text);
        for (auto& line : lines) {
            std::wstring section;
            if (!aip::TryParseIniSectionHeader(line, section)) continue;
            if (!IEquals(section, a) && !IEquals(section, b)) continue;
            const auto open = line.find(L'['), close = line.find(L']', open);
            if (close == std::wstring::npos) return false;
            line = line.substr(0, open + 1) + (IEquals(section, a) ? b : a) + line.substr(close);
        }
        text = aip::JoinIniLines(lines) + L"\r\n";
        return true;
    });
}

static bool DispatchContentEngineCommand(HWND owner, UINT command)
{
    if (command == ID_CAPS_SETTINGS + 30 || command == ID_ASUS_SETTINGS + 30) {
        if (command == ID_CAPS_SETTINGS + 30) g_contentCaps.restart = true;
        else g_contentAsus.restart = true;
        WakePollThread(); return true;
    }
    const ContentTrayOption* hardwareOption = nullptr;
    const wchar_t* hardwareSection = nullptr;
    if (command >= ID_CAPS_SETTINGS && command < ID_CAPS_SETTINGS + _countof(g_capsOptions)) {
        hardwareOption = &g_capsOptions[command - ID_CAPS_SETTINGS]; hardwareSection = L"CapsBlink";
    } else if (command >= ID_ASUS_SETTINGS && command < ID_ASUS_SETTINGS + _countof(g_asusOptions)) {
        hardwareOption = &g_asusOptions[command - ID_ASUS_SETTINGS]; hardwareSection = L"AsusBlink";
    }
    if (hardwareOption) {
        auto value = IniReadS(hardwareSection, hardwareOption->key, hardwareOption->fallback);
        if (hardwareOption->toggle) {
            bool enabled = false; aip::content::ParseBool(value, enabled); value = enabled ? L"0" : L"1";
        } else if (ShowTextInputDialog(owner, hardwareSection, hardwareOption->label, value, true) != IDOK) return true;
        SaveContentMenuSettings(owner, {{hardwareSection, hardwareOption->key, value}}); return true;
    }
    if (command >= ID_DISCORD_SETTINGS && command < ID_DISCORD_SETTINGS + _countof(g_discordOptions)) {
        const auto& option = g_discordOptions[command - ID_DISCORD_SETTINGS];
        auto value = IniReadS(L"Discord", option.key, option.fallback);
        if (option.toggle) {
            bool enabled = false; aip::content::ParseBool(value, enabled); value = enabled ? L"0" : L"1";
        } else if (ShowTextInputDialog(owner, L"Discord presence", option.label, value, true) != IDOK) return true;
        SaveContentMenuSettings(owner, {{L"Discord", option.key, value}});
        return true;
    }
    if (command >= ID_NOTES_SETTINGS && command < ID_NOTES_SETTINGS + 40) {
        const UINT offset = command - ID_NOTES_SETTINGS;
        std::wstring section = L"Notes", key, label, fallback;
        if (offset < 3) { key = g_notesOptionKeys[offset]; label = g_notesOptionLabels[offset]; fallback = g_notesOptionDefaults[offset]; }
        else if (offset >= 16 && (offset - 16) / 8 < 3 && (offset - 16) % 8 < 3) {
            const UINT resource = (offset - 16) / 8, field = (offset - 16) % 8;
            section += L"." + std::wstring(aip::content::NotesResources[resource].value);
            key = g_notesAccountKeys[field]; label = key;
        } else return false;
        const bool secret = aip::content::SensitiveSetting(section, key);
        std::wstring value = secret ? L"" : IniReadS(section.c_str(), key.c_str(), fallback.c_str());
        if (secret) label += L" (replace saved cookie; blank clears it). Stored in plain text in this INI.";
        if (ShowTextInputDialog(owner, L"Real-Time Notes", label, value, true, secret) == IDOK)
            SaveContentMenuSettings(owner, {{section, key, value}});
        return true;
    }
    if (command >= ID_CONTENT_TEXT_MODE && command < ID_CONTENT_TEXT_MODE + _countof(aip::content::TextModes)) {
        SaveContentMenuSettings(owner, {{L"Content", L"TextMode", aip::content::TextModes[command - ID_CONTENT_TEXT_MODE].value}}); return true;
    }
    if (command == ID_CONTENT_ADD) {
        const auto next = IniReadClampedI(L"Content", L"Count", 1, 1, aip::content::MaxEntries) + 1;
        if (next <= aip::content::MaxEntries) SaveContentMenuSettings(owner, {{L"Content", L"Count", std::to_wstring(next)}});
        return true;
    }
    const ContentTrayOption* option = nullptr;
    std::wstring section;
    if (command >= ID_CONTENT_OPTIONS && command < ID_CONTENT_OPTIONS + _countof(g_contentOptions)) {
        option = &g_contentOptions[command - ID_CONTENT_OPTIONS]; section = L"Content";
    } else if (command >= ID_CONTENT_ENTRY_OPTIONS && command < ID_CONTENT_ENTRY_OPTIONS + aip::content::MaxEntries * CONTENT_ENTRY_COMMANDS) {
        const UINT offset = command - ID_CONTENT_ENTRY_OPTIONS, field = offset % CONTENT_ENTRY_COMMANDS;
        const int entry = static_cast<int>(offset / CONTENT_ENTRY_COMMANDS) + 1;
        const int count = IniReadClampedI(L"Content", L"Count", 1, 1, aip::content::MaxEntries);
        if (entry > count) return false;
        section = L"Content." + std::to_wstring(entry);
        if (field >= 16 && field < 16 + _countof(aip::content::Backgrounds)) {
            const auto background = aip::content::Backgrounds[field - 16].value;
            if (IEquals(background, L"Image") && IniReadS(section.c_str(), L"ImagePath", L"").empty()) ChooseContentImage(owner, section);
            else SaveContentMenuSettings(owner, {{section, L"Background", background}});
            return true;
        }
        if (field >= 24 && field < 24 + _countof(aip::content::Sources)) {
            std::vector<std::wstring> sources;
            aip::content::ParseSources(IniReadS(section.c_str(), L"TextSources", L""), sources);
            const auto source = aip::content::Sources[field - 24].value;
            auto found = std::find(sources.begin(), sources.end(), source);
            if (found == sources.end()) sources.push_back(source); else sources.erase(found);
            std::wstring value;
            for (const auto& name : sources) { if (!value.empty()) value += L","; value += name; }
            SaveContentMenuSettings(owner, {{section, L"TextSources", value}}); return true;
        }
        if (field == 32 || field == 33) {
            const int other = entry + (field == 32 ? -1 : 1);
            if (other >= 1 && other <= count) MoveContentMenuEntry(owner, entry, other);
            return true;
        }
        if (field >= 40 && field < 43) {
            SaveContentMenuSettings(owner, {{section, L"NotesResource", aip::content::NotesResources[field - 40].value}});
            return true;
        }
        if (field >= _countof(g_contentEntryOptions)) return false;
        option = &g_contentEntryOptions[field];
        if (field == 3) { ChooseContentImage(owner, section); return true; }
    } else return false;
    auto value = IniReadS(section.c_str(), option->key, option->fallback);
    if (option->toggle) {
        bool enabled = false; aip::content::ParseBool(value, enabled); value = enabled ? L"0" : L"1";
    } else {
        auto prompt = std::wstring(option->label);
        if (IEquals(option->key, L"TextSources")) prompt += L"\r\nCustomText, RssFeed, CapsLock, SMTC, Notes, DiscordRPC, CapsBlink, AsusBlink in display order. Empty means no text.";
        if (ShowTextInputDialog(owner, L"Content and sources", prompt, value, true) != IDOK) return true;
    }
    SaveContentMenuSettings(owner, {{section, option->key, value}});
    return true;
}
