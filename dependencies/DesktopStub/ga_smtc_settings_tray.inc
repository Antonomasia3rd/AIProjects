// Presentation preferences share the provider's typed INI/CLI configuration.
static constexpr UINT ID_SMTC_INDICATOR = 29400;
static constexpr UINT ID_SMTC_REPEAT_SHUFFLE = 29410;
static void AppendSmtcDisplaySettingsMenu(HMENU parent)
{
    HMENU menu = aip::BeginTrayNestedMenu(parent);
    const auto current = aip::content::Lower(IniReadS(L"SMTC", L"Indicator", L"Status"));
    aip::AppendTrayMenuItem(menu, ID_SMTC_INDICATOR, L"Current playback status", current == L"status", true);
    aip::AppendTrayMenuItem(menu, ID_SMTC_INDICATOR + 1, L"Player app icon (status when unavailable)", current == L"appicon", true);
    aip::AppendTrayMenuItem(menu, ID_SMTC_INDICATOR + 2, L"No icon", current == L"none", true);
    bool extra = false;
    aip::content::ParseBool(IniReadS(L"SMTC", L"ShowRepeatShuffle", L"0"), extra);
    aip::AppendTrayMenuItem(menu, ID_SMTC_REPEAT_SHUFFLE, L"Show repeat and shuffle status when available", extra, true);
    aip::EndTrayNestedMenu(parent, menu, L"Now playing appearance");
}
static bool DispatchSmtcDisplaySettingsCommand(HWND owner, UINT command)
{
    if (command >= ID_SMTC_INDICATOR && command <= ID_SMTC_INDICATOR + 2) {
        const wchar_t* modes[] = {L"Status", L"AppIcon", L"None"};
        SaveContentMenuSettings(owner, {{L"SMTC", L"Indicator", modes[command - ID_SMTC_INDICATOR]}}); return true;
    }
    if (command == ID_SMTC_REPEAT_SHUFFLE) {
        bool value = false;
        aip::content::ParseBool(IniReadS(L"SMTC", L"ShowRepeatShuffle", L"0"), value);
        SaveContentMenuSettings(owner, {{L"SMTC", L"ShowRepeatShuffle", value ? L"0" : L"1"}}); return true;
    }
    return false;
}
