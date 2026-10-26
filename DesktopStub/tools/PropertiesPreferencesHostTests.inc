// Exercise the real CLI schema and shared tray/Properties dispatch against
// the host fixture's private INI. No window, package or account is opened.
static void TestPropertiesPreferencesHost(){
    std::wstring original;ReadIniFileForMutation(original);const auto command=g_commandLine;const auto commandText=contentTestCommandLine;
    const auto enabled=g_notificationsEnabled.load();g_notificationsEnabled=false;
    contentTestCommandLine=L"DesktopStub.exe --configure-only --set Properties.HighlightSource=AccentLight2 --set Properties.HighlightContrast=Readable";
    Check(ParseCommandLineOptions()&&ApplyCommandLinePersistentSettings(),"CLI accepts and persists documented Properties color-source and contrast settings");
    Check(IniReadS(L"Properties",L"HighlightSource",L"")==L"AccentLight2"&&IniReadS(L"Properties",L"HighlightContrast",L"")==L"Readable","INI receives the same canonical Properties values as the GUI");
    contentTestCommandLine=L"DesktopStub.exe --configure-only --set Properties.HighlightSource=Invalid";
    Check(!ParseCommandLineOptions(),"CLI rejects an unknown color source before mutating settings");
    const std::wstring fixture=L"[Properties]\r\nHighlightMode=Accent\r\nHighlightSource=Auto\r\nHighlightContrast=Readable\r\n";
    Check(WriteContentFixture(fixture),"Properties command fixture writes only its temporary INI");
    HMENU menu=CreatePopupMenu();AppendDesktopStubPropertiesOptions(menu);
    const auto contains=[&](const auto& self,HMENU current,UINT id)->bool{for(int i=0;i<GetMenuItemCount(current);++i){if(GetMenuItemID(current,i)==id)return true;if(const auto child=GetSubMenu(current,i);child&&self(self,child,id))return true;}return false;};
    for(UINT i=0;i<std::size(aip::properties::HighlightSources);++i){const auto id=ID_PROPERTIES_SOURCE_FIRST+i;
        Check(contains(contains,menu,id)&&IsDesktopStubPropertiesChoice(id),"every color source is exposed as a shared tray/Properties choice");
        Check(DispatchDesktopStubPropertiesOption(nullptr,id)&&IniReadS(L"Properties",L"HighlightSource",L"")==aip::properties::HighlightSources[i],"shared GUI color-source dispatcher persists the selected canonical enum");}
    Check(contains(contains,menu,ID_PROPERTIES_CONTRAST_READABLE)&&contains(contains,menu,ID_PROPERTIES_CONTRAST_EXACT),"both contrast policies are visible in the common GUI menu model");
    Check(DispatchDesktopStubPropertiesOption(nullptr,ID_PROPERTIES_CONTRAST_EXACT)&&IniReadS(L"Properties",L"HighlightContrast",L"")==L"Exact","exact-color opt-out uses the same validated writer");
    Check(DispatchDesktopStubPropertiesOption(nullptr,ID_PROPERTIES_CONTRAST_READABLE)&&IniReadS(L"Properties",L"HighlightContrast",L"")==L"Readable","readability can be restored through the common dispatcher");
    DestroyMenu(menu);WriteContentFixture(original);g_commandLine=command;contentTestCommandLine=commandText;g_notificationsEnabled=enabled;
}
