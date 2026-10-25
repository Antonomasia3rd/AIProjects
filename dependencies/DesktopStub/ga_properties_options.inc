// Properties preference writes share the CLI's portable schema and the profile
// mutation mutex. Included before the tray model/dispatcher; no resident work.
#include "../properties_ui_model.h"
static constexpr UINT ID_PROPERTIES_ACCENT=64910,ID_PROPERTIES_CUSTOM=64911,ID_PROPERTIES_COLOR=64912,
    ID_PROPERTIES_CAPTION_CUSTOM=64913,ID_PROPERTIES_CAPTION_SYSTEM=64914,ID_PROPERTIES_RESTART=64915;
static void PropertiesOptionsChanged();
static void RequestDesktopStubRestart(HWND owner);
static bool IsDesktopStubPropertiesChoice(UINT id){return id==ID_PROPERTIES_ACCENT||id==ID_PROPERTIES_CUSTOM||id==ID_PROPERTIES_CAPTION_CUSTOM||id==ID_PROPERTIES_CAPTION_SYSTEM;}
static void AppendDesktopStubPropertiesOptions(HMENU parent){
    HMENU menu=aip::BeginTrayNestedMenu(parent),highlight=aip::BeginTrayNestedMenu(menu);
    const auto mode=IniReadS(L"Properties",L"HighlightMode",L"Accent"),caption=IniReadS(L"Properties",L"CaptionMode",L"Properties");
    aip::AppendTrayMenuItem(highlight,ID_PROPERTIES_ACCENT,IniReadS(L"Strings",L"PropertiesAccent",L"Windows accent color"),IEquals(mode,L"Accent"),true);
    aip::AppendTrayMenuItem(highlight,ID_PROPERTIES_CUSTOM,IniReadS(L"Strings",L"PropertiesCustom",L"Custom color"),IEquals(mode,L"Custom"),true);
    aip::EndTrayNestedMenu(menu,highlight,IniReadS(L"Strings",L"PropertiesHighlight",L"Highlight color"));
    aip::AppendTrayMenuItem(menu,ID_PROPERTIES_COLOR,IniReadS(L"Strings",L"PropertiesColor",L"Custom color (#RRGGBB)...")+L" "+IniReadS(L"Properties",L"HighlightColor",L"#0078D4"),false,true);
    HMENU title=aip::BeginTrayNestedMenu(menu);
    aip::AppendTrayMenuItem(title,ID_PROPERTIES_CAPTION_CUSTOM,IniReadS(L"Strings",L"PropertiesCaptionCustom",L"Properties heading"),IEquals(caption,L"Properties"),true);
    aip::AppendTrayMenuItem(title,ID_PROPERTIES_CAPTION_SYSTEM,IniReadS(L"Strings",L"PropertiesCaptionSystem",L"Windows title bar"),IEquals(caption,L"System"),true);
    aip::EndTrayNestedMenu(menu,title,IniReadS(L"Strings",L"PropertiesCaption",L"Title bar"));
    aip::EndTrayNestedMenu(parent,menu,IniReadS(L"Strings",L"PropertiesOptions",L"Properties window"));
}
static bool DispatchDesktopStubPropertiesOption(HWND owner,UINT command){
    if(command<ID_PROPERTIES_ACCENT||command>ID_PROPERTIES_RESTART)return false;
    if(command==ID_PROPERTIES_RESTART){RequestDesktopStubRestart(owner);return true;}
    std::wstring profile;{std::lock_guard<std::mutex> lock(g_pathMutex);profile=aip::MakeAbsolutePath(g_iniPath);}
    std::wstring revision,current,error;if(!ReadIniFileForMutation(revision))return true;
    struct Wipe{std::wstring& a;std::wstring& b;~Wipe(){if(!a.empty())SecureZeroMemory(&a[0],a.size()*sizeof(wchar_t));if(!b.empty())SecureZeroMemory(&b[0],b.size()*sizeof(wchar_t));}} wipe{revision,current};
    const auto key=command==ID_PROPERTIES_COLOR?L"HighlightColor":command==ID_PROPERTIES_ACCENT||command==ID_PROPERTIES_CUSTOM?L"HighlightMode":L"CaptionMode";
    std::wstring value=command==ID_PROPERTIES_ACCENT?L"Accent":command==ID_PROPERTIES_CUSTOM?L"Custom":command==ID_PROPERTIES_CAPTION_CUSTOM?L"Properties":L"System";
    if(command==ID_PROPERTIES_COLOR){value=IniReadS(L"Properties",key,L"#0078D4");
        if(ShowTextInputDialog(owner,IniReadS(L"Strings",L"PropertiesOptions",L"Properties window"),IniReadS(L"Strings",L"PropertiesColorPrompt",L"Enter a color as #RRGGBB, for example #0078D4. Select Custom color to use it."),value,true)!=IDOK)return true;}
    if(!aip::properties::NormalizeSetting(key,value,error)){NotifyTrayBalloon(g_ui.notificationTitle,error,NIIF_WARNING);return true;}
    aip::IniWriteMutexGuard lease(profile,CurrentIniWriteLockWaitMs());
    std::wstring active;{std::lock_guard<std::mutex> lock(g_pathMutex);active=aip::MakeAbsolutePath(g_iniPath);}
    if(!lease.IsLocked()||!IEquals(active,profile)||!ReadIniFileForMutation(current)||current!=revision){NotifyTrayBalloon(g_ui.notificationTitle,IniReadS(L"Strings",L"SettingsWindowStale",L"The profile or settings changed. Press F5 to reload before applying another change."),NIIF_WARNING);return true;}
    if(!IniWrite(L"Properties",key,value.c_str())){NotifyTrayBalloon(g_ui.notificationTitle,IniReadS(L"Strings",L"PropertiesSaveFailed",L"The Properties preference could not be saved."),NIIF_WARNING);return true;}
    PropertiesOptionsChanged();return true;
}
