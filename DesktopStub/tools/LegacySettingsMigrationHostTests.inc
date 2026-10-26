// Real defaults/migration and settings adapter, on private INIs only. No
// application entry point, account source, package, Startup or hardware work.
#include "../../tools/fixtures/DesktopStubLegacyDefaults.h"
static void TestLegacySettingsMigrationHost()
{
    const auto originalPath=g_iniPath;
    struct RestoreLegacyPath{std::wstring path;~RestoreLegacyPath(){g_iniPath=path;
        std::lock_guard<std::mutex> lock(g_iniCacheMutex);g_iniCacheLoaded=false;g_iniCacheDoc.clear();}}restore{originalPath};
    std::vector<std::wstring> owned;
    std::wstring error;
    const auto base=aip::GetDirectoryName(originalPath)+L"\\LegacyMigration-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    const auto readFrom=[](const std::vector<aip::IniSectionData>& document,const wchar_t* section,const wchar_t* key,const wchar_t* fallback){
        std::wstring value;return aip::ReadIniValueFromDoc(document,section,key,value)?value:std::wstring(fallback);
    };
    for(std::size_t index=0;index<sizeof(LegacyReleases)/sizeof(LegacyReleases[0]);++index){
        const auto& release=LegacyReleases[index];std::wstring source=L"; Preserved historical defaults\r\n";
        // The portable matrix covers every release. Native INI startup needs
        // each distinct default set plus the customized v25 representative.
        if(index&&index!=24&&release.values==LegacyReleases[index-1].values)continue;
        for(std::size_t field=0;field<release.count;++field){const auto& row=release.values[field];
            Check(aip::WriteIniValueToText(source,row.section,row.key,row.value),"historical host fixture stages original released defaults");}
        if(index==24){
            aip::WriteIniValueToText(source,L"TileText",L"Enabled",L"1");
            aip::WriteIniValueToText(source,L"TileText",L"Text",L"Preserved v25 text ; # 日本語");
            aip::WriteIniValueToText(source,L"Settings",L"LiveTileBranding",L"Logo");
        }
        g_iniPath=base+L"-v"+std::to_wstring(index+1)+L".ini";
        const auto backup=g_iniPath+L".before-tile-instances.bak";
        if(GetFileAttributesW(g_iniPath.c_str())!=INVALID_FILE_ATTRIBUTES||GetFileAttributesW(backup.c_str())!=INVALID_FILE_ATTRIBUTES){
            Check(false,"legacy host fixture never overwrites a pre-existing path");continue;
        }
        Check(WriteContentFixture(source),"historical host fixture writes only its own private INI");owned.push_back(g_iniPath);
        const bool initialized=EnsureIniDefaults();Check(initialized,"actual defaults stage migration before manufacturing new provider sections");
        std::wstring after,prior;Check(ReadIniFileForMutation(after)&&ReadTextFile(backup,prior)&&prior==source,
            "actual migration backup contains the exact original pre-default document");owned.push_back(backup);
        const auto doc=aip::ParseIniDocument(after);aip::content::TileInstanceConfiguration cfg;
        Check(aip::content::ReadTileInstanceConfiguration([&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){
            return readFrom(doc,section,key,fallback);
        },cfg,error)&&cfg.tiles.size()==1&&cfg.tiles.front().id==L"desktop"&&cfg.tiles.front().primary,
            "actual initialized old release preserves one primary Desktop rather than adding unrelated providers");
        if(index==24&&!cfg.tiles.empty()){
            Check(cfg.tiles.front().policy.Read(L"Settings",L"PrimaryRegistration",L"")==L"Main","actual v25 upgrade preserves primary main-package registration");
            Check(cfg.tiles.front().configuration.textMode==aip::content::TextMode::Auto&&
                cfg.tiles.front().configuration.entries.front().custom.primary==L"Preserved v25 text ; # 日本語"&&cfg.tiles.front().appearance.branding==L"Logo",
                "actual v25 upgrade preserves enabled text and explicit global branding");
            auto settings=after;
            Check(WriteContentProfileSetting(settings,{L"TileText",L"Text",L"old CLI update"},&error),"old script field routes through real settings adapter");
            auto aliases=aip::ParseIniDocument(settings);
            Check(readFrom(aliases,L"TileText",L"Text",L"")==L"old CLI update"&&readFrom(aliases,L"Tile.desktop",L"Content.Text",L"")==L"old CLI update",
                "old script updates both its legacy global and the visible migrated Desktop");
            Check(WriteContentProfileSetting(settings,{L"Tile.desktop",L"Content.Text",L"new explicit update"},&error),"explicit typed update follows a legacy field in the same batch");
            aliases=aip::ParseIniDocument(settings);
            Check(readFrom(aliases,L"Tile.desktop",L"Content.Text",L"")==L"new explicit update","last explicit assignment wins after a legacy alias");
            Check(WriteContentProfileSetting(settings,{L"TileText",L"Text",L"last legacy update"},&error),"legacy field can follow a typed field in the same batch");
            aliases=aip::ParseIniDocument(settings);
            Check(readFrom(aliases,L"Tile.desktop",L"Content.Text",L"")==L"last legacy update","last legacy assignment wins after a typed field");
            Check(WriteContentProfileSetting(settings,{L"Settings",L"ExperimentalLiveTileUpdate",L"0"},&error),"legacy numeric delivery flag is accepted for a migrated profile");
            aliases=aip::ParseIniDocument(settings);
            Check(readFrom(aliases,L"Tile.desktop",L"Delivery.Mode",L"")==L"Registration","legacy delivery changes the actual primary tile policy");
            Check(WriteContentProfileSetting(settings,{L"TileText",L"Enabled",L"0"},&error),"legacy text disable uses the real adapter");
            aliases=aip::ParseIniDocument(settings);
            Check(readFrom(aliases,L"Tile.desktop",L"TextMode",L"")==L"Off","legacy text disable actually hides the primary text");
            aip::WriteIniValueToText(settings,L"Tiles",L"Instances",L"");const auto missing=settings;
            Check(!WriteContentProfileSetting(settings,{L"TileText",L"Text",L"must not save"},&error)&&settings==missing&&!error.empty(),
                "legacy alias after removing the primary fails clearly and cannot partially write a global");
        }
        Check(EnsureIniDefaults(),"upgraded defaults can be checked again");std::wstring repeated;
        Check(ReadIniFileForMutation(repeated)&&repeated==after,"rechecking defaults after an upgrade is byte-for-byte idempotent");
    }
    g_iniPath=base+L"-complex.ini";
    if(GetFileAttributesW(g_iniPath.c_str())==INVALID_FILE_ATTRIBUTES){
        const std::wstring source=L"[Content]\r\nEnabled=1\r\nCount=2\r\nCycleEnabled=1\r\n[Content.1]\r\nBackground=Wallpaper\r\nTextSources=CustomText\r\nText=one\r\n[Content.2]\r\nBackground=None\r\nTextSources=CustomText\r\nText=two\r\n";
        Check(WriteContentFixture(source)&&EnsureIniDefaults()&&EnsureTileInstanceProfile(error),"complex legacy contents remain valid instead of failing startup");owned.push_back(g_iniPath);
        std::wstring after;ReadIniFileForMutation(after);const auto document=aip::ParseIniDocument(after);
        Check(readFrom(document,L"Tiles",L"Instances",L"absent")==L"absent"&&readFrom(document,L"Content",L"Count",L"")==L"2"&&
            readFrom(document,L"Content.2",L"Text",L"")==L"two","complex legacy cycle stays on its legacy engine without collapsing the second entry");
    }
    for(const auto& path:owned)Check(DeleteFileW(path.c_str())!=FALSE,"legacy fixture removes only its privately created path");
}
