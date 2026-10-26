// Presentation preferences share the provider's typed INI/CLI configuration.
static constexpr UINT ID_SMTC_INDICATOR = 29400;
static constexpr UINT ID_SMTC_REPEAT_SHUFFLE = 29410;
static constexpr UINT ID_SMTC_SESSION_CURRENT=29420,ID_SMTC_SESSION_APPLICATION=29421,ID_SMTC_SESSION_APPID=29422,ID_SMTC_SESSION_INSTANCE=29423;
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
    const auto source=IniReadS(L"SMTC",L"SessionMode",L"Current");
    aip::AppendTrayMenuItem(menu,ID_SMTC_SESSION_CURRENT,L"Follow Windows current session",source==L"Current",true);
    aip::AppendTrayMenuItem(menu,ID_SMTC_SESSION_APPLICATION,L"Follow chosen application",source==L"Application",true);
    aip::AppendTrayMenuItem(menu,ID_SMTC_SESSION_APPID,L"Chosen application ID...",false,true);
    aip::AppendTrayMenuItem(menu,ID_SMTC_SESSION_INSTANCE,L"Session within application...",false,true);
    aip::EndTrayNestedMenu(parent, menu, L"Now playing appearance");
}
static bool DispatchSmtcDisplaySettingsCommand(HWND owner, UINT command)
{
    if(command==ID_SMTC_SESSION_CURRENT||command==ID_SMTC_SESSION_APPLICATION){SaveContentMenuSettings(owner,{{L"SMTC",L"SessionMode",command==ID_SMTC_SESSION_CURRENT?L"Current":L"Application"}});return true;}
    if(command==ID_SMTC_SESSION_APPID||command==ID_SMTC_SESSION_INSTANCE){
        const auto key=command==ID_SMTC_SESSION_APPID?L"SessionAppId":L"SessionInstance";
        auto value=IniReadS(L"SMTC",key,command==ID_SMTC_SESSION_APPID?L"":L"0");
        if(ShowTextInputDialog(owner,L"Now playing",command==ID_SMTC_SESSION_APPID?L"Enter the exact SMTC application ID":L"0 follows current/first; 1-64 selects that occurrence",value,true)==IDOK)
            SaveContentMenuSettings(owner,{{L"SMTC",key,value}});
        return true;
    }
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
