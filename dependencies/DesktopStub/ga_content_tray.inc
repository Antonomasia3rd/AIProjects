// Declarative choices are shared with the portable configuration model.
#ifndef DESKTOPSTUB_CONTENT_MESSAGEBOX
#define DESKTOPSTUB_CONTENT_MESSAGEBOX MessageBoxW
#endif
#include "../content_presets.h"
static constexpr UINT ID_CONTENT_OPTIONS = 19000;
static constexpr UINT ID_CONTENT_ADD = 19010;
static constexpr UINT ID_CONTENT_NOW_PLAYING_IMPORT = 19011;
static constexpr UINT ID_CONTENT_TEXT_MODE = 19020;
static constexpr UINT ID_CONTENT_BUILTIN_PRESET = 19030;
static constexpr UINT ID_CONTENT_CYCLE_DRIVER = 19040;
static constexpr UINT ID_CONTENT_ENTRY_OPTIONS = 20000;
static constexpr UINT CONTENT_ENTRY_COMMANDS = 64;
static void AppendPrayerSettingsMenu(HMENU menu);
static bool DispatchPrayerSettingsCommand(HWND owner, UINT command);
static void AppendTextPresentationMenu(HMENU parent,const std::wstring& section,UINT slot);
static bool DispatchTextPresentationCommand(HWND owner,UINT command);
static constexpr UINT ContentSourceChoiceOffset(UINT choice) { return choice < 8 ? 24 + choice : 34 + choice - 8; }
static const aip::content::SourceDescriptor* ContentSourceChoiceForOffset(UINT field) {
    static_assert(_countof(aip::content::Sources) <= 14, "Source menu choices exhausted their reserved command offsets.");
    for (UINT choice = 0; choice < _countof(aip::content::Sources); ++choice)
        if (ContentSourceChoiceOffset(choice) == field) return &aip::content::Sources[choice];
    return nullptr;
}
static constexpr UINT ID_NOTES_SETTINGS = 24000;
static constexpr UINT ID_DISCORD_SETTINGS = 25000;
static constexpr UINT ID_DISCORD_PROFILE_OPTION = 25500;
static constexpr UINT ID_CAPS_SETTINGS = 26000;
static constexpr UINT ID_ASUS_SETTINGS = 27000;
static const wchar_t* g_notesOptionKeys[] = {L"RefreshSeconds", L"HttpTimeoutSeconds", L"MaximumBytes"};
static const wchar_t* g_notesOptionDefaults[] = {L"300", L"15", L"1048576"};
static const wchar_t* g_notesOptionLabels[] = {L"Refresh (seconds)", L"HTTP timeout (seconds)", L"Response limit (bytes)"};
static const wchar_t* g_notesAccountKeys[] = {L"UID", L"LToken", L"LTUid", L"RefreshSeconds", L"LTokenProtected", L"LTUidProtected"};
struct ContentTrayOption { const wchar_t* key; const wchar_t* label; const wchar_t* fallback; bool toggle; };
static const ContentTrayOption g_capsOptions[] = {
    {L"HardwareEnabled", L"Control physical Caps indicator", L"0", true},
    {L"KeyboardTargetPath", L"Keyboard device (Auto selects all available)", L"Auto", false},
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
    {L"Name", L"Activity name", aip::discord::DefaultActivityName, false},
    {L"Details", L"Details template", aip::discord::DefaultDetailsTemplate, false},
    {L"State", L"State template", aip::discord::DefaultStateTemplate, false},
    {L"RefreshSeconds", L"Refresh (seconds)", L"5", false},
    {L"OperationTimeoutMs", L"Operation timeout (milliseconds)", L"5000", false},
    {L"ClearOnStop", L"Clear sent presence on shutdown", L"1", true},
};
static const ContentTrayOption g_contentOptions[] = {
    {L"Enabled", L"Use tile contents", L"0", true},
    {L"Count", L"Content slots (1-32)", L"1", false},
    {L"CycleEnabled", L"Cycle contents on the Desktop tile", L"0", true},
    {L"CycleSeconds", L"DesktopStub cycle interval (seconds)", L"10", false},
    {L"RefreshSeconds", L"Check for source updates every (seconds)", L"2", false},
    {L"TextMode", L"Text delivery", L"Auto", false},
    {L"CycleDriver", L"Desktop tile cycle method", L"DesktopStub", false},
};
static const ContentTrayOption g_contentEntryOptions[] = {
    {L"Enabled", L"Use this content", L"1", true},
    {L"Name", L"Name", L"Content", false},
    {L"Background", L"Background", L"Wallpaper", false},
    {L"ImagePath", L"Choose background image...", L"", false},
    {L"TextSources", L"Text sources", L"", false},
    {L"Text", L"Custom primary text", L"", false},
    {L"SecondaryText", L"Custom secondary text", L"", false},
    {L"BadgeText", L"Custom badge text", L"", false},
    {L"RssItem", L"RSS headline number (1-20)", L"1", false},
    {L"NotesResource", L"Notes game/resource", L"Resin", false},
    {L"NotesDetail", L"Notes detail", L"Summary", false},
};

static std::wstring ContentMenuValue(std::wstring value)
{
    for (auto& ch : value) if (ch == L'\r' || ch == L'\n') ch = L' ';
    if (value.size() > 72) {
        value.resize(69);
        if (!value.empty() && value.back() >= 0xD800 && value.back() <= 0xDBFF) value.pop_back();
        value += L"...";
    }
    std::wstring display;
    for (const auto ch : value) { display += ch; if (ch == L'&') display += ch; }
    return display.empty() ? L"(empty)" : display;
}

static void AppendContentSubmenu(HMENU menu, HMENU child, const std::wstring& label)
{
    if (child && !AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(child), label.c_str())) DestroyMenu(child);
}

enum class ContentMenuPart { All, Contents, Sources, Timing, TextDelivery };

static void AppendContentEngineMenu(HMENU menu, ContentMenuPart part = ContentMenuPart::All)
{
    if (part == ContentMenuPart::All) {
        const std::pair<ContentMenuPart, const wchar_t*> groups[] = {
            {ContentMenuPart::Contents, L"Tile contents"},
            {ContentMenuPart::Sources, L"Sources and accounts"},
            {ContentMenuPart::Timing, L"Updates and cycling"},
            {ContentMenuPart::TextDelivery, L"Text display"},
        };
        for (const auto& group : groups) {
            HMENU child = CreatePopupMenu();
            if (!child) continue;
            AppendContentEngineMenu(child, group.first);
            AppendContentSubmenu(menu, child, group.second);
        }
        return;
    }
    auto append = [](HMENU target, UINT id, const wchar_t* section, const ContentTrayOption& option) {
        const auto value = IniReadS(section, option.key, option.fallback);
        bool checked = false;
        aip::content::ParseBool(value, checked);
        auto label = std::wstring(option.label);
        if (!option.toggle) label += L": " + ContentMenuValue(value);
        aip::AppendTrayMenuItem(target, id, label, option.toggle && checked, true);
    };
    for (UINT i = 0; i < _countof(g_contentOptions); ++i) {
        if (i < 2 && part != ContentMenuPart::Contents) continue;
        if (i >= 2 && i <= 4 && part != ContentMenuPart::Timing) continue;
        if (i == 5 && part != ContentMenuPart::TextDelivery) continue;
        if (i == 6 && part != ContentMenuPart::Timing) continue;
        if (i == 1) continue; // List size belongs next to the entries, below.
        if (IEquals(g_contentOptions[i].key, L"CycleDriver")) {
            HMENU choices = CreatePopupMenu();
            if (!choices) continue;
            const auto value = IniReadS(L"Content", L"CycleDriver", L"DesktopStub");
            for (UINT choice = 0; choice < _countof(aip::content::CycleDrivers); ++choice)
                aip::AppendTrayMenuItem(choices, ID_CONTENT_CYCLE_DRIVER + choice,
                    aip::content::CycleDrivers[choice].label, IEquals(value, aip::content::CycleDrivers[choice].value), true);
            aip::AppendTrayMenuItem(choices, 0, L"Windows chooses queue display order and timing", false, false);
            AppendContentSubmenu(menu, choices, IniReadS(L"Strings",L"ContentCycleMethodTitle",L"Desktop tile cycle method"));
        } else if (IEquals(g_contentOptions[i].key, L"TextMode")) {
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
    if (part == ContentMenuPart::TextDelivery) AppendTextPresentationMenu(menu,L"TileText",0);
    if (part == ContentMenuPart::Sources) {
    HMENU prayer = aip::BeginTrayNestedMenu(menu);
    AppendPrayerSettingsMenu(prayer);
    aip::EndTrayNestedMenu(menu, prayer, IniReadS(L"Strings", L"PrayerSettingsTitle", L"Prayer times settings"));
    HMENU notes = CreatePopupMenu();
    if (notes) {
        for (UINT i = 0; i < 3; ++i)
            aip::AppendTrayMenuItem(notes, ID_NOTES_SETTINGS + i,
                std::wstring(g_notesOptionLabels[i]) + L": " + IniReadS(L"Notes", g_notesOptionKeys[i], g_notesOptionDefaults[i]), false, true);
        for (UINT resource = 0; resource < 3; ++resource) {
            HMENU accountMenu = CreatePopupMenu();
            if (!accountMenu) continue;
            const auto section = L"Notes." + std::wstring(aip::content::NotesResources[resource].value);
            for (UINT field = 0; field < _countof(g_notesAccountKeys); ++field) {
                const auto fallback = field == 4 ? IniReadS(section.c_str(), L"LTokenV2Protected", L"") :
                    field == 5 ? IniReadS(section.c_str(), L"LTuidV2Protected", L"") : field == 3 ? L"0" : L"";
                const auto value = IniReadS(section.c_str(), g_notesAccountKeys[field], fallback.c_str());
                const auto display = field == 0 || field == 3 ? ContentMenuValue(value) : value.empty() ? L"not configured" : L"configured";
                aip::AppendTrayMenuItem(accountMenu, ID_NOTES_SETTINGS + 16 + resource * 8 + field,
                    std::wstring(g_notesAccountKeys[field]) + L": " + display, false, true);
            }
            aip::AppendTrayMenuItem(accountMenu, ID_NOTES_SETTINGS + 16 + resource * 8 + 6, L"Refresh this game now", false, true);
            aip::AppendTrayMenuItem(accountMenu, ID_NOTES_SETTINGS + 16 + resource * 8 + 7, L"Clear this account", false, true);
            aip::AppendTrayMenuItem(accountMenu, ID_NOTES_SETTINGS + 64 + resource, L"Import cookie JSON...", false, true);
            AppendContentSubmenu(notes, accountMenu, aip::content::NotesResources[resource].label);
        }
        aip::AppendTrayMenuItem(notes, ID_NOTES_SETTINGS + 72, L"Import legacy account INI...", false, true);
        AppendContentSubmenu(menu, notes, L"Real-Time Notes accounts/settings");
    }
    HMENU discord = CreatePopupMenu();
    if (discord) {
        bool sending = false;
        aip::content::ParseBool(IniReadS(L"Discord", L"SendPresence", L"0"), sending);
        aip::AppendTrayMenuItem(discord, 0, sending ? L"Sending presence to Discord" : L"Preview only; sending is off", false, false);
        const auto activeProfile = TrimCopy(IniReadS(L"Discord", L"ProfilePath", L""));
        if (!activeProfile.empty()) {
            aip::AppendTrayMenuItem(discord, 0, L"Active advanced profile: " + ContentMenuValue(activeProfile), false, false);
            aip::AppendTrayMenuItem(discord, 0, L"Actual host refresh: " + IniReadS(L"Discord", L"RefreshSeconds", L"5") + L" seconds", false, false);
        }
        for (UINT option = 0; option < _countof(g_discordOptions); ++option) {
            const auto& item = g_discordOptions[option];
            const bool inlineOnly = IEquals(item.key, L"Name") || IEquals(item.key, L"Details") || IEquals(item.key, L"State") || IEquals(item.key, L"ClientId");
            if (!activeProfile.empty() && inlineOnly) {
                auto storedOption = item;
                const auto storedLabel = std::wstring(item.label) + L" (used without advanced profile)";
                storedOption.label = storedLabel.c_str();
                append(discord, ID_DISCORD_SETTINGS + option, L"Discord", storedOption);
            } else append(discord, ID_DISCORD_SETTINGS + option, L"Discord", item);
        }
        HMENU advanced = CreatePopupMenu();
        if (advanced) {
            const auto options = aip::discord::ProfileOptions();
            std::vector<std::pair<std::wstring, HMENU>> sections;
            for (UINT index = 0; index < options.size(); ++index) {
                auto found = std::find_if(sections.begin(), sections.end(), [&](const auto& section) { return section.first == options[index].section; });
                if (found == sections.end()) {
                    HMENU section = CreatePopupMenu();
                    if (!section) {
                        aip::AppendTrayMenuItem(advanced, ID_DISCORD_PROFILE_OPTION + index, options[index].label, false, true);
                        continue;
                    }
                    sections.push_back({options[index].section, section});
                    found = sections.end() - 1;
                }
                aip::AppendTrayMenuItem(found->second, ID_DISCORD_PROFILE_OPTION + index, options[index].label, false, true);
            }
            for (const auto& section : sections) {
                const std::pair<const wchar_t*, const wchar_t*> titles[] = {
                    {L"general", L"Activity and connection"}, {L"layout", L"Text layout"},
                    {L"censor_map", L"Text filters"}, {L"large_time_ranges", L"Large image schedule"},
                    {L"small_time_ranges", L"Small image schedule"}, {L"large_assets", L"Large image assets"},
                    {L"small_assets", L"Small image assets"}, {L"buttons", L"Buttons"},
                    {L"afk", L"Away status"}, {L"ipc", L"Discord connection"}, {L"gateway", L"Gateway connection"},
                };
                std::wstring title = section.first;
                for (const auto& item : titles) if (section.first == item.first) { title = item.second; break; }
                AppendContentSubmenu(advanced, section.second, title);
            }
            AppendContentSubmenu(discord, advanced, L"Edit advanced profile options");
        }
        aip::AppendTrayMenuItem(discord, ID_DISCORD_SETTINGS + 80, L"Create new advanced INI...", false, true);
        AppendContentSubmenu(menu, discord, L"Discord Rich Presence");
    }
    HMENU caps = CreatePopupMenu();
    if (caps) {
        for (UINT option = 0; option < _countof(g_capsOptions); ++option)
            append(caps, ID_CAPS_SETTINGS + option, L"CapsBlink", g_capsOptions[option]);
        aip::AppendTrayMenuItem(caps, ID_CAPS_SETTINGS + 30, L"Restart indicator pattern", false, true);
        aip::AppendTrayMenuItem(caps, ID_CAPS_SETTINGS + 31, L"Import Caps donor INI...", false, true);
        AppendContentSubmenu(menu, caps, L"Caps indicator settings");
    }
    HMENU asus = CreatePopupMenu();
    if (asus) {
        HMENU microphone = CreatePopupMenu(), keyboard = CreatePopupMenu(), errors = CreatePopupMenu();
        for (UINT option = 0; option < _countof(g_asusOptions); ++option) {
            HMENU target = option >= 4 && option <= 6 && microphone ? microphone :
                option >= 7 && option <= 9 && keyboard ? keyboard :
                option >= 10 && errors ? errors : asus;
            append(target, ID_ASUS_SETTINGS + option, L"AsusBlink", g_asusOptions[option]);
        }
        AppendContentSubmenu(asus, microphone, L"Microphone indicator");
        AppendContentSubmenu(asus, keyboard, L"Keyboard lighting");
        AppendContentSubmenu(asus, errors, L"Error handling");
        aip::AppendTrayMenuItem(asus, ID_ASUS_SETTINGS + 30, L"Restart patterns", false, true);
        aip::AppendTrayMenuItem(asus, ID_ASUS_SETTINGS + 31, L"Import ASUS donor INI...", false, true);
        aip::AppendTrayMenuItem(asus, ID_ASUS_SETTINGS + 32, L"Edit numbered event setting...", false, true);
        AppendContentSubmenu(menu, asus, L"ASUS indicator settings");
    }
    }
    if (part != ContentMenuPart::Contents) return;
    HMENU entries = CreatePopupMenu();
    const bool ownEntries = entries != nullptr;
    if (!entries) entries = menu;
    aip::AppendTrayMenuItem(entries, ID_CONTENT_ADD, L"Add content...", false, count < aip::content::MaxEntries);
    aip::AppendMenuSeparator(entries);
    for (int entry = 1; entry <= count; ++entry) {
        auto section = L"Content." + std::to_wstring(entry);
        const UINT base = ID_CONTENT_ENTRY_OPTIONS + static_cast<UINT>(entry - 1) * CONTENT_ENTRY_COMMANDS;
        HMENU child = CreatePopupMenu();
        if (!child) continue;
        HMENU backgroundMenu = CreatePopupMenu();
        HMENU customTextMenu = CreatePopupMenu();
        HMENU sourceOptionsMenu = CreatePopupMenu();
        for (UINT i = 0; i < _countof(g_contentEntryOptions); ++i) {
            HMENU target = (i == 2 || i == 3) && backgroundMenu ? backgroundMenu :
                (i >= 5 && i <= 7) && customTextMenu ? customTextMenu :
                (i >= 8 && i <= 10) && sourceOptionsMenu ? sourceOptionsMenu : child;
            if (i == 2 && backgroundMenu) {
                const auto selected = IniReadS(section.c_str(), L"Background", L"Wallpaper");
                for (UINT choice = 0; choice < _countof(aip::content::Backgrounds); ++choice)
                    aip::AppendTrayMenuItem(target, base + 16 + choice, aip::content::Backgrounds[choice].label,
                        IEquals(selected, aip::content::Backgrounds[choice].value), true);
                aip::AppendMenuSeparator(target);
                continue;
            }
            if (i == 2 || i == 4 || i == 9 || i == 10) {
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
                        aip::AppendTrayMenuItem(choices, base + ContentSourceChoiceOffset(choice), aip::content::Sources[choice].label,
                            std::find(sources.begin(), sources.end(), aip::content::Sources[choice].value) != sources.end(), true);
                    aip::AppendTrayMenuItem(choices, base + 4, L"Edit source order...", false, true);
                } else if (i == 10) {
                    int resource = 0; aip::notes::ParseHostResource(IniReadS(section.c_str(), L"NotesResource", L"Resin"), resource);
                    const auto selected = IniReadS(section.c_str(), L"NotesDetail", L"Summary");
                    for (UINT detail = 0; detail < _countof(aip::notes::Details); ++detail)
                        if (aip::notes::DetailAvailable(resource, aip::notes::Details[detail].id))
                            aip::AppendTrayMenuItem(choices, base + 48 + detail, aip::notes::Details[detail].label, IEquals(selected, aip::notes::Details[detail].value), true);
                } else {
                    const auto selected = IniReadS(section.c_str(), L"NotesResource", L"Resin");
                    for (UINT resource = 0; resource < 3; ++resource)
                        aip::AppendTrayMenuItem(choices, base + 40 + resource, aip::content::NotesResources[resource].label,
                            IEquals(selected, aip::content::NotesResources[resource].value), true);
                }
                AppendContentSubmenu(target, choices, g_contentEntryOptions[i].label);
            } else append(target, base + i, section.c_str(), g_contentEntryOptions[i]);
        }
        AppendContentSubmenu(child, backgroundMenu, L"Background");
        AppendContentSubmenu(child, customTextMenu, L"Custom text");
        AppendContentSubmenu(child, sourceOptionsMenu, L"RSS headline and Notes detail");
        aip::AppendMenuSeparator(child);
        aip::AppendTrayMenuItem(child, base + 32, L"Move earlier in cycle", false, entry > 1);
        aip::AppendTrayMenuItem(child, base + 33, L"Move later in cycle", false, entry < count);
        auto label = std::to_wstring(entry) + L". " + ContentMenuValue(IniReadS(section.c_str(), L"Name", L"Content"));
        bool enabled = true;
        aip::content::ParseBool(IniReadS(section.c_str(), L"Enabled", L"1"), enabled);
        if (!enabled) label += L" (disabled)";
        AppendContentSubmenu(entries, child, label);
    }
    aip::AppendMenuSeparator(entries);
    append(entries, ID_CONTENT_OPTIONS + 1, L"Content", g_contentOptions[1]);
    if (ownEntries) AppendContentSubmenu(menu, entries, L"Content list (" + std::to_wstring(count) + L")");
}

// A menu action works from one file snapshot and commits once. Concurrent
// writers cannot get half of an image/path update or overwrite a reorder.
using ContentMenuConfirmation = std::function<bool(const std::wstring&, const aip::content::Configuration&, const std::wstring&)>;
template<class Mutator> static bool CommitContentMenuMutation(HWND owner, Mutator mutate, const ContentMenuConfirmation& confirm = {})
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
    if (candidate == original) return true;
    auto readConfig = [](const std::wstring& text, aip::content::Configuration& cfg, std::wstring& issue) {
        const auto doc = aip::ParseIniDocument(text);
        return aip::content::ReadConfiguration([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            std::wstring value;
            return aip::ReadIniValueFromDoc(doc, section, key, value) ? value : std::wstring(fallback);
        }, cfg, issue, ContentAsusEventsFromDocument(doc));
    };
    aip::content::Configuration before, after;
    std::wstring ignored;
    readConfig(original, before, ignored);
    if (!readConfig(candidate, after, error)) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Invalid content setting", MB_OK | MB_ICONWARNING); return false;
    }
    bool separateEnabled = false;
    { std::wstring enabled; aip::ReadIniValueFromDoc(aip::ParseIniDocument(candidate), L"Tiles", L"Enabled", enabled);
      aip::content::ParseBool(enabled, separateEnabled); }
    for (const auto& entry : after.entries) {
        if (!separateEnabled && after.enabled && entry.enabled &&
            aip::content::EntryUsesSource(entry, aip::content::SourceId::RssFeed) &&
            !ValidateConfiguredRssSettingsForContentSource(ContentSource::RssFeed, error)) {
            DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"RSS configuration", MB_OK | MB_ICONWARNING); return false;
        }
    }
    const auto warning = ContentTimingChangeWarning(before, after);
    if (confirm) {
        if (!confirm(original, after, warning)) return false;
    } else if (!warning.empty() && DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, (warning + L"\r\n\r\nApply this setting?").c_str(), L"Content cycling", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES) return false;
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

static bool SaveContentMenuSettings(HWND owner, const std::vector<aip::IniSetting>& settings, bool replaceAsusEvents = false,
    bool replaceContentEntries = false, const ContentMenuConfirmation& confirm = {})
{
    return CommitContentMenuMutation(owner, [&](std::wstring& text, std::wstring& error) {
        if (replaceAsusEvents && !RemoveContentAsusEvents(text)) return false;
        if (replaceContentEntries && !aip::now_playing_profile::RemoveContentEntriesFromText(text)) return false;
        for (const auto& setting : settings) {
            auto value = setting.value;
            if (!aip::content::NormalizeSetting(setting.section, setting.key, value, error)) return false;
            if (!WriteContentProfileSetting(text, {setting.section, setting.key, value})) {
                error = L"Could not store " + setting.section + L"." + setting.key +
                    L" in the single-line INI format. Stored fields cannot contain actual line breaks or NUL characters.";
                return false;
            }
        }
        return true;
    }, confirm);
}

static bool ApplyContentPreset(HWND owner, const aip::content::PresetDefinition& preset)
{
    auto requested = preset;
    aip::content::PresetPlan planned;
    return CommitContentMenuMutation(owner, [&](std::wstring& text, std::wstring& error) {
        const auto document = aip::ParseIniDocument(text);
        aip::content::Configuration current;
        std::wstring ignored;
        if (aip::content::ReadConfiguration([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
                std::wstring value; return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
            }, current, ignored, ContentAsusEventsFromDocument(document))) {
            if (requested.value == L"WallpaperSMTC") {
                const aip::content::Entry* wallpaper = nullptr;
                for (const auto& entry : current.entries)
                    if (entry.enabled && aip::content::BackgroundIdFor(entry.background) == aip::content::BackgroundId::Wallpaper) { wallpaper = &entry; break; }
                if (!wallpaper) for (const auto& entry : current.entries)
                    if (aip::content::BackgroundIdFor(entry.background) == aip::content::BackgroundId::Wallpaper) { wallpaper = &entry; break; }
                requested = aip::content::WallpaperAndSmtcCycle(wallpaper);
            }
            if (aip::content::ActivePresetMatches(current, requested)) return true;
        }
        std::wstring countValue;
        int count = 1, highest = 0;
        if (aip::ReadIniValueFromDoc(document, L"Content", L"Count", countValue) &&
            !aip::content::ParseInt(countValue, 1, aip::content::MaxEntries, count)) {
            error = L"Content.Count must be from 1 through 32 before adding a preset."; return false;
        }
        for (const auto& section : document) if (aip::content::EntrySection(section.name)) {
            int index = 0;
            aip::content::ParseInt(section.name.substr(8), 1, aip::content::MaxEntries, index);
            highest = std::max(highest, index);
        }
        if (!aip::content::PlanAppendPreset(requested, count, highest, planned, error)) return false;
        for (const auto& setting : planned.settings)
            if (!WriteContentProfileSetting(text, {setting.section, setting.key, setting.value})) {
                error = L"The preset could not be stored in the selected INI."; return false;
            }
        return true;
    }, [&](const std::wstring&, const aip::content::Configuration&, const std::wstring& warning) {
        std::wstring summary = L"Apply " + requested.label + L"?\r\n\r\n";
        summary += L"This adds new contents in slots " + std::to_wstring(planned.firstEntry) + L" through " +
            std::to_wstring(planned.lastEntry) + L" and makes them active.\r\n";
        summary += L"The " + std::to_wstring(planned.previousSlots) + L" previous slots stay saved but become inactive; re-enable them from Content list.\r\n";
        summary += L"\r\nWindows handles rotation when available; otherwise DesktopStub uses the configured app cycle interval. Provider/accounts and Startup settings stay configured.";
        if (!warning.empty()) summary += L"\r\n\r\n" + warning;
        return DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, summary.c_str(), L"Apply content preset",
            MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES;
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

static bool ImportContentNotesFile(HWND owner, int resource, const std::wstring& path)
{
    if (aip::ToLower(aip::MakeAbsolutePath(path)) == aip::ToLower(aip::MakeAbsolutePath(g_iniPath))) return false;
    std::wstring text, error;
    std::vector<aip::IniSetting> changes;
    if (!aip::notes::ReadImportFile(path, text, error) ||
        !(resource < 0 ? aip::notes::ImportAccountProfile(text, changes, error) : aip::notes::ImportCookieJson(resource, text, changes, error))) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Notes import", MB_OK | MB_ICONWARNING); return false;
    }
    return SaveContentMenuSettings(owner, changes);
}

static bool ImportContentHardwareFile(HWND owner, bool asus, const std::wstring& path)
{
    std::vector<aip::IniSetting> changes;
    std::wstring error;
    if (!ReadContentHardwareImport(path, asus, changes, error)) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Hardware profile import", MB_OK | MB_ICONWARNING); return false;
    }
    return SaveContentMenuSettings(owner, changes, asus);
}

static bool ImportContentNowPlayingFile(HWND owner, const std::wstring& path)
{
    aip::now_playing_profile::ImportPlan plan;
    aip::now_playing_profile::ConfigurationProjection projection;
    std::wstring error;
    if (!ReadContentNowPlayingImport(path, plan, projection, error)) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"NowPlayingTile import", MB_OK | MB_ICONWARNING); return false;
    }
    const auto confirm = [&](const std::wstring& original, const aip::content::Configuration& after, const std::wstring& warning) {
        std::size_t removed = 0;
        for (const auto& section : aip::ParseIniDocument(original))
            if (aip::ToLower(section.name).compare(0, 8, L"content.") == 0) ++removed;
        std::wstring summary = L"Import this NowPlayingTile profile?\r\n\r\nThe imported layout creates " +
            std::to_wstring(after.entries.size()) + L" media content entries and replaces the entire current Content list.\r\n";
        if (removed) summary += L"This removes " + std::to_wstring(removed) + L" saved Content.* sections, including inactive or unused entries.\r\n";
        summary += L"\r\nIt also applies the profile's text delivery, source refresh, tray preference (next launch) and menu grouping. Startup preferences and the cycle interval stay as configured.\r\n";
        if (!warning.empty()) summary += L"\r\n" + warning;
        summary += L"\r\nContinue with replacement?";
        return DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, summary.c_str(), L"Replace contents from NowPlayingTile profile",
            MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) == IDYES;
    };
    if (!SaveContentMenuSettings(owner, projection.settings, false, true, confirm)) return false;
    std::wstring report = L"NowPlayingTile content was imported.\r\n";
    for (const auto& issue : projection.diagnostics) report += L"\r\n" + issue;
    DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, report.c_str(), L"NowPlayingTile import", MB_OK | MB_ICONINFORMATION);
    return true;
}

static bool SaveContentAsusEventSetting(HWND owner, const std::wstring& key, const std::wstring& value)
{
    const auto canonical = aip::hardware_profile::CanonicalAsusKey(key);
    if (canonical.compare(0, 5, L"Event") != 0) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"A supported numbered ASUS event key is required.", L"ASUS numbered event", MB_OK | MB_ICONWARNING);
        return false;
    }
    return SaveContentMenuSettings(owner, {{L"AsusBlink", canonical, value}});
}

static bool DispatchContentEngineCommand(HWND owner, UINT command)
{
    if (DispatchPrayerSettingsCommand(owner, command)) return true;
    if (DispatchTextPresentationCommand(owner, command)) return true;
    if (command >= ID_CONTENT_CYCLE_DRIVER && command < ID_CONTENT_CYCLE_DRIVER + _countof(aip::content::CycleDrivers)) {
        SaveContentMenuSettings(owner, {{L"Content", L"CycleDriver", aip::content::CycleDrivers[command - ID_CONTENT_CYCLE_DRIVER].value}}); return true;
    }
    if (command == ID_CONTENT_BUILTIN_PRESET) {
        ApplyContentPreset(owner, aip::content::WallpaperPreset()); return true;
    }
    if (command == ID_CONTENT_BUILTIN_PRESET + 1) {
        ApplyContentPreset(owner, aip::content::WallpaperRssPreset(IniReadClampedI(L"RssFeed", L"MaxItems", 5, 1, 20))); return true;
    }
    if (command == ID_CONTENT_BUILTIN_PRESET + 2) {
        ApplyContentPreset(owner, aip::content::WallpaperAndSmtcCycle()); return true;
    }
    if (command == ID_DISCORD_SETTINGS + 80) {
        wchar_t path[32768]{};
        OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
        dialog.lpstrFile = path; dialog.nMaxFile = _countof(path); dialog.lpstrDefExt = L"ini";
        dialog.lpstrFilter = L"INI profiles\0*.ini\0All files\0*.*\0\0";
        dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog)) return true;
        DiscordHostEditOptions options; options.create = true; options.createPath = path;
        std::wstring error;
        if (!CommitDiscordHostProfile({}, {}, options, error)) DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Discord profile setup", MB_OK | MB_ICONWARNING);
        else WakePollThread();
        return true;
    }
    if (command >= ID_DISCORD_PROFILE_OPTION && command < ID_DISCORD_PROFILE_OPTION + 256) {
        const auto options = aip::discord::ProfileOptions(); const auto index = command - ID_DISCORD_PROFILE_OPTION;
        if (index >= options.size()) return false;
        DiscordRawFileSnapshot host;
        std::wstring reference, path, error;
        aip::discord::ProfileDocument document;
        if (!CaptureDiscordRawFile(g_iniPath, host, error) ||
            !aip::ReadIniValueFromDoc(aip::ParseIniDocument(host.text), L"Discord", L"ProfilePath", reference) || reference.empty() ||
            !aip::discord::ResolveProfileReference(reference, g_iniPath, path, error) || !aip::discord::ReadProfileDocument(path, document, error)) {
            if (error.empty()) error = L"Choose or create an advanced INI before editing its options.";
            DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Advanced Discord profile", MB_OK | MB_ICONWARNING); return true;
        }
        const auto& option = options[index];
        std::wstring value, label = option.label;
        if (option.Sensitive()) label += L" (private replacement; blank clears the configured credential)";
        else if (option.hostCadence) {
            if (!aip::ReadIniValueFromDoc(aip::ParseIniDocument(host.text), L"Discord", L"RefreshSeconds", value)) value = L"5";
        } else aip::discord::ReadProfileOptionForEditor(document.profile, option, value);
        if (option.hostCadence) label += L" (also updates this source's host refresh interval)";
        if (ShowTextInputDialog(owner, L"Advanced Discord profile", label, value, true, option.Sensitive()) != IDOK) return true;
        DiscordHostEditOptions edit;
        edit.privateInput = option.Sensitive(); edit.optimistic = true;
        edit.expectedHost = host.text; edit.expectedProfile = document.text;
        if (!CommitDiscordHostProfile({}, {{option.section, option.key, value}}, edit, error))
            DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Advanced Discord profile", MB_OK | MB_ICONWARNING);
        else WakePollThread();
        return true;
    }
    if (command == ID_CONTENT_NOW_PLAYING_IMPORT) {
        wchar_t path[32768]{};
        OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
        dialog.lpstrFile = path; dialog.nMaxFile = _countof(path);
        dialog.lpstrFilter = L"INI profiles\0*.ini\0All files\0*.*\0\0";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        dialog.lpstrTitle = L"Import NowPlayingTile profile";
        if (GetOpenFileNameW(&dialog)) ImportContentNowPlayingFile(owner, path);
        return true;
    }
    if (command == ID_CAPS_SETTINGS + 31 || command == ID_ASUS_SETTINGS + 31) {
        wchar_t path[32768]{};
        OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
        dialog.lpstrFile = path; dialog.nMaxFile = _countof(path);
        dialog.lpstrFilter = L"INI profiles\0*.ini\0All files\0*.*\0\0";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog)) ImportContentHardwareFile(owner, command == ID_ASUS_SETTINGS + 31, path);
        return true;
    }
    if (command == ID_ASUS_SETTINGS + 32) {
        std::wstring key = L"Event1HddState";
        if (ShowTextInputDialog(owner, L"ASUS numbered event", L"Setting name (for example Event001MicInterval)", key, true) != IDOK) return true;
        const auto canonical = aip::hardware_profile::CanonicalAsusKey(key);
        if (canonical.compare(0, 5, L"Event") != 0) {
            DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"Use Event followed by 1-3 digits, Mic/Keyboard/Hdd, and State/Interval/Duration.", L"ASUS numbered event", MB_OK | MB_ICONWARNING);
            return true;
        }
        std::wstring value = IniReadS(L"AsusBlink", canonical.c_str(), canonical.find(L"State") != std::wstring::npos ? L"off" : canonical.find(L"Duration") != std::wstring::npos ? L"once" : L"0");
        if (ShowTextInputDialog(owner, L"ASUS numbered event", canonical, value, true) == IDOK)
            SaveContentAsusEventSetting(owner, canonical, value);
        return true;
    }
    if ((command >= ID_NOTES_SETTINGS + 64 && command < ID_NOTES_SETTINGS + 67) || command == ID_NOTES_SETTINGS + 72) {
        const int resource = command == ID_NOTES_SETTINGS + 72 ? -1 : static_cast<int>(command - ID_NOTES_SETTINGS - 64);
        wchar_t path[32768]{};
        OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
        dialog.lpstrFile = path; dialog.nMaxFile = _countof(path);
        dialog.lpstrFilter = resource < 0 ? L"INI profiles\0*.ini\0All files\0*.*\0\0" : L"Cookie JSON\0*.json\0All files\0*.*\0\0";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog)) ImportContentNotesFile(owner, resource, path);
        return true;
    }
    if (command >= ID_NOTES_SETTINGS + 16 && command < ID_NOTES_SETTINGS + 40) {
        const auto offset = command - ID_NOTES_SETTINGS - 16;
        if (offset % 8 == 6) { RequestContentNotesRefresh(static_cast<int>(offset / 8)); return true; }
        if (offset % 8 == 7) {
            const auto resource = static_cast<int>(offset / 8);
            if (SaveContentMenuSettings(owner, aip::notes::ClearAccountSettings(resource))) RequestContentNotesRefresh(resource);
            return true;
        }
    }
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
        else if (offset >= 16 && (offset - 16) / 8 < 3 && (offset - 16) % 8 < _countof(g_notesAccountKeys)) {
            const UINT resource = (offset - 16) / 8, field = (offset - 16) % 8;
            section += L"." + std::wstring(aip::content::NotesResources[resource].value);
            key = g_notesAccountKeys[field]; label = key; if (field == 3) fallback = L"0";
        } else return false;
        const bool secret = aip::content::SensitiveSetting(section, key);
        std::wstring value = secret ? L"" : IniReadS(section.c_str(), key.c_str(), fallback.c_str());
        if (secret) label += key.find(L"Protected") != std::wstring::npos ? L" (paste protected blob; blank clears it)." : L" (replace saved cookie; blank clears it). Stored in plain text in this INI.";
        if (ShowTextInputDialog(owner, L"Real-Time Notes", label, value, true, secret) == IDOK) {
            std::vector<aip::IniSetting> changes{{section, key, value}};
            if (key == L"UID" && value != IniReadS(section.c_str(), L"UID", L""))
                changes.push_back({section, L"Server", L""});
            if (key == L"LToken" || key == L"LTUid" || key == L"LTokenProtected" || key == L"LTUidProtected") {
                const bool token = key.find(L"Token") != std::wstring::npos;
                static constexpr const wchar_t* tokenKeys[] = {L"LToken", L"LTokenV2", L"ltoken_v2", L"LTokenProtected", L"LTokenV2Protected"};
                static constexpr const wchar_t* userKeys[] = {L"LTUid", L"LTuidV2", L"ltuid_v2", L"LTUidProtected", L"LTuidV2Protected"};
                for (UINT index = 0; index < _countof(tokenKeys); ++index) {
                    const auto* other = token ? tokenKeys[index] : userKeys[index];
                    if (key != other) changes.push_back({section, other, L""});
                }
            }
            if (SaveContentMenuSettings(owner, changes)) {
                if (section.size() > 6) { int resource = 0; aip::notes::ParseHostResource(section.substr(6), resource); RequestContentNotesRefresh(resource); }
                else RequestContentNotesRefresh(3);
            }
        }
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
            if (aip::content::BackgroundIdFor(background) == aip::content::BackgroundId::Image &&
                IniReadS(section.c_str(), L"ImagePath", L"").empty()) ChooseContentImage(owner, section);
            else SaveContentMenuSettings(owner, {{section, L"Background", background}});
            return true;
        }
        if (const auto* selectedSource = ContentSourceChoiceForOffset(field)) {
            std::vector<std::wstring> sources;
            aip::content::ParseSources(IniReadS(section.c_str(), L"TextSources", L""), sources);
            const auto source = selectedSource->value;
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
            SaveContentMenuSettings(owner, {{section, L"NotesResource", aip::content::NotesResources[field - 40].value}, {section, L"NotesDetail", L"Summary"}});
            return true;
        }
        if (field >= 48 && field < 48 + _countof(aip::notes::Details)) {
            SaveContentMenuSettings(owner, {{section, L"NotesDetail", aip::notes::Details[field - 48].value}});
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
        if (IEquals(option->key, L"TextSources")) {
            prompt += L"\r\n";
            prompt += aip::content::SupportedSourceNames();
            prompt += L" in display order. Empty means no text.";
        }
        if (ShowTextInputDialog(owner, L"Content and sources", prompt, value, true) != IDOK) return true;
    }
    SaveContentMenuSettings(owner, {{section, option->key, value}});
    return true;
}
#include "ga_prayer_settings_tray.inc"
#include "ga_text_presentation_tray.inc"
