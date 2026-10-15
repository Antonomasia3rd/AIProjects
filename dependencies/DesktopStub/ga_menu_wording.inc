// Shared menu text overrides are applied after construction, so dynamic
// settings keep their current value while users can rewrite the label.
static constexpr UINT ID_MENU_WORDING_EXPORT=59000, ID_MENU_WORDING_EDIT=59001, ID_MENU_WORDING_LITERAL=59002;
static std::map<std::wstring,std::wstring> g_menuWordingDefaults;
static std::wstring MenuTitleKey(const std::wstring& text) {
    unsigned long long hash=14695981039346656037ull;
    for(const auto ch:text){hash^=static_cast<unsigned>(ch);hash*=1099511628211ull;}
    return L"MenuTitle"+std::to_wstring(hash);
}
static void ApplyMenuWording(HMENU menu) {
    for(int i=0;i<GetMenuItemCount(menu);++i) {
        wchar_t buffer[4096]{};MENUITEMINFOW item{sizeof(item)};
        item.fMask=MIIM_FTYPE|MIIM_ID|MIIM_SUBMENU|MIIM_STRING;item.dwTypeData=buffer;item.cch=4095;
        if(!GetMenuItemInfoW(menu,i,TRUE,&item)||(item.fType&MFT_SEPARATOR))continue;
        const std::wstring original=buffer;
        if(item.hSubMenu)ApplyMenuWording(item.hSubMenu);
        if(!item.hSubMenu&&!item.wID)continue; // Provider values/status are data, not labels.
        const auto key=item.hSubMenu?MenuTitleKey(original):L"Menu"+std::to_wstring(item.wID);
        const auto colon=original.find(L": ");
        const auto fallback=colon==std::wstring::npos?original:original.substr(0,colon)+L": {value}";
        g_menuWordingDefaults[key]=fallback;
        auto text=IniReadS(L"Strings",key.c_str(),original.c_str());
        const auto token=text.find(L"{value}");
        if(token!=std::wstring::npos)text.replace(token,7,colon==std::wstring::npos?L"":original.substr(colon+2));
        MENUITEMINFOW replacement{sizeof(replacement)};replacement.fMask=MIIM_STRING;replacement.dwTypeData=text.data();
        SetMenuItemInfoW(menu,i,TRUE,&replacement);
    }
}
static void AppendMenuWordingSettings(HMENU menu) {
    HMENU child=aip::BeginTrayNestedMenu(menu);
    aip::AppendTrayMenuItem(child,ID_MENU_WORDING_EXPORT,IniReadS(L"Strings",L"MenuWordingExport",L"Add missing menu text defaults to INI"));
    aip::AppendTrayMenuItem(child,ID_MENU_WORDING_EDIT,IniReadS(L"Strings",L"MenuWordingEdit",L"Edit a text string..."));
    aip::AppendTrayMenuItem(child,ID_MENU_WORDING_LITERAL,IniReadS(L"Strings",L"DialogWordingEdit",L"Replace dialog wording..."));
    aip::EndTrayNestedMenu(menu,child,IniReadS(L"Strings",L"MenuWordingTitle",L"Interface wording"));
}
static bool DispatchMenuWordingCommand(HWND owner,UINT command) {
    if(command!=ID_MENU_WORDING_EXPORT&&command!=ID_MENU_WORDING_EDIT&&command!=ID_MENU_WORDING_LITERAL)return false;
    if(command==ID_MENU_WORDING_EXPORT) {
        std::vector<aip::IniSetting> defaults;
        for(const auto& item:g_menuWordingDefaults){std::wstring value;if(!ReadIniValueCached(L"Strings",item.first.c_str(),value))defaults.push_back({L"Strings",item.first,item.second});}
        {std::lock_guard<std::mutex> lock(g_uiLiteralMutex);for(const auto& item:g_uiLiteralDefaults){std::wstring value;if(!ReadIniValueCached(L"Strings",item.first.c_str(),value))defaults.push_back({L"Strings",item.first,item.second});}}
        SaveSecondaryTileMenuSettings(owner,defaults);
        return true;
    }
    if(command==ID_MENU_WORDING_LITERAL) {
        std::wstring original;
        if(ShowTextInputDialog(owner,L"Dialog wording",L"Exact app-owned dialog title, prompt or message text to replace",original,false)!=IDOK)return true;
        auto replacement=UiLiteralText(original);
        if(ShowTextInputDialog(owner,L"Dialog wording",L"Replacement text",replacement,true)==IDOK)SaveSecondaryTileMenuSettings(owner,{{L"Strings",UiLiteralKey(original),replacement}});
        return true;
    }
    std::wstring key=L"Menu19002";
    if(ShowTextInputDialog(owner,L"Interface wording",L"INI key in [Strings] (export menu defaults to see keys; existing named Strings keys work too)",key,true)!=IDOK)return true;
    const auto found=g_menuWordingDefaults.find(key);
    auto value=IniReadS(L"Strings",key.c_str(),found==g_menuWordingDefaults.end()?L"":found->second.c_str());
    if(ShowTextInputDialog(owner,L"Interface wording",L"Replacement text. {value} keeps a menu setting's current value.",value,true)==IDOK&&SaveSecondaryTileMenuSettings(owner,{{L"Strings",key,value}})) {
        LoadUiStrings();ValidateFormatStrings();NormalizeSeparatorStrings();
    }
    return true;
}
static void RemoveDirectMenuSeparators(HMENU menu) {
    for(int i=GetMenuItemCount(menu)-1;i>=0;--i) {
        MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_FTYPE;
        if(GetMenuItemInfoW(menu,i,TRUE,&item)&&(item.fType&MFT_SEPARATOR))DeleteMenu(menu,i,MF_BYPOSITION);
    }
}
