#include "../dependencies/tile_instances.h"
#include "../dependencies/baseline_app.h"
#include <appmodel.h>
#include "../dependencies/core.inc"
#include "../dependencies/config_ini.inc"
#include <iostream>
#include <map>
#include "fixtures/DesktopStubLegacyDefaults.h"
namespace {
int checks=0,failures=0;
void Check(bool passed,const char* message){++checks;if(!passed){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
using namespace aip::content;
std::wstring Fixture(const LegacyReleaseFixture& release){
    std::wstring text=L"; Historical factory profile, not a user account.\r\n";
    for(std::size_t i=0;i<release.count;++i)
        if(!aip::WriteIniValueToText(text,release.values[i].section,release.values[i].key,release.values[i].value))return {};
    return text;
}
auto Reader(const std::vector<aip::IniSectionData>& document){
    return [&document](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){
        std::wstring value;return aip::ReadIniValueFromDoc(document,section,key,value)?value:std::wstring(fallback);
    };
}
bool Upgrade(const std::wstring& source,std::wstring& upgraded,TileInstanceConfiguration& configuration,std::wstring& error){
    const auto document=aip::ParseIniDocument(source);
    if(!StageLegacyTileInstances(Reader(document),source,[](std::wstring& text,const std::wstring& section,const std::wstring& key,
            const std::wstring& value,std::wstring& issue){
        if(aip::WriteIniValueToText(text,section.c_str(),key.c_str(),value))return true;issue=L"Fixture INI staging failed.";return false;
    },upgraded,error))return false;
    const auto result=aip::ParseIniDocument(upgraded);return ReadTileInstanceConfiguration(Reader(result),configuration,error);
}
void Preserved(const std::wstring& source,const std::wstring& result){
    const auto original=aip::ParseIniDocument(source),changed=aip::ParseIniDocument(result);
    for(const auto& section:original)for(const auto& field:section.entries){
        std::wstring value;
        Check(aip::ReadIniValueFromDoc(changed,section.name.c_str(),field.key.c_str(),value)&&value==field.value,
            "migration preserves every pre-existing setting, including unknown and protected values");
    }
    Check(result.find(L"; Historical factory profile, not a user account.")==0,"migration retains original comments");
}
void HistoricalDefaults(){
    Check(sizeof(LegacyReleases)/sizeof(LegacyReleases[0])==25,"all released v1 through v25 default sets are represented");
    for(const auto& release:LegacyReleases){
        const auto original=Fixture(release);Check(!original.empty(),"historical literal defaults serialize through the production INI writer");
        const auto source=aip::ParseIniDocument(original);std::wstring unused;
        Check(!aip::ReadIniValueFromDoc(source,L"Content",L"Enabled",unused),"the released pre-source profile has no Content schema");
        std::wstring changed,error;TileInstanceConfiguration config;
        const bool ok=Upgrade(original,changed,config,error);
        if(!ok)std::wcerr<<L"migration diagnostic: "<<error<<L'\n';
        Check(ok,"released defaults migrate successfully");
        Check(config.configured&&config.tiles.size()==1&&config.tiles.front().id==L"desktop"&&config.tiles.front().primary,
            "an old wallpaper-only profile creates only its original primary Desktop");
        if(!config.tiles.empty()){
            const auto& tile=config.tiles.front();
            Check(tile.type==TileInstanceType::Wallpaper&&tile.configuration.entries.front().background==L"Wallpaper"&&
                tile.configuration.textMode==TextMode::Off,"old disabled text and Desktop wallpaper semantics survive");
            Check(tile.policy.Mode()==L"Auto","historical explicit Auto delivery is preserved rather than replaced by new factory defaults");
            Check(tile.policy.Read(L"Settings",L"PrimaryRegistration",L"")==L"Main","old primary Desktop keeps its main-package Registration route");
        }
        if(ok){Preserved(original,changed);std::wstring again;TileInstanceConfiguration repeat;
            Check(Upgrade(changed,again,repeat,error)&&again==changed,"a repeated real INI migration is byte-for-byte idempotent");}
    }
}
void Customized(){
    auto source=Fixture(LegacyReleases[24]);
    const auto set=[&](const wchar_t* section,const wchar_t* key,const wchar_t* value){Check(aip::WriteIniValueToText(source,section,key,value),"custom fixture setting writes in memory only");};
    set(L"Settings",L"ManifestDisplayName",L"My Desktop");set(L"Settings",L"ExperimentalLiveTileUpdate",L"0");
    set(L"Settings",L"ManifestIdentityName",L"custom.iso.desktop");set(L"Settings",L"ManifestPublisher",L"CN=CustomPublisher");
    set(L"Settings",L"GeneratedAssetCacheMaxEntries",L"73");set(L"Assets",L"WideTile",L"0");
    set(L"TileText",L"Enabled",L"1");set(L"TileText",L"Text",L"  Primary ; # text 日本語  ");
    set(L"TileText",L"SecondaryText",L"Second\\nline");set(L"TileText",L"BadgeText",L"25%");
    set(L"Notes.Resin",L"LTokenProtected",L"dpapi:v1:utf8:SYNTHETIC-NOT-A-TOKEN");
    set(L"Account.saved",L"UID",L"SYNTHETIC-ACCOUNT-REFERENCE");
    set(L"Gallery",L"GroupId",L"SYNTHETIC-OWNED-GROUP");set(L"Gallery.Tile.1",L"Id",L"SYNTHETIC-EXISTING-PIN");
    set(L"PrayerTimes",L"Method",L"bad but dormant");set(L"UnrecognizedUserSection",L"PrivatePreference",L"keep exactly");
    std::wstring result,error;TileInstanceConfiguration config;
    Check(Upgrade(source,result,config,error)&&config.tiles.size()==1,"customized v25 settings migrate without activating dormant sources");
    if(!config.tiles.empty()){
        const auto& tile=config.tiles.front();const auto& text=tile.configuration.entries.front().custom;
        Check(tile.name==L"My Desktop"&&tile.configuration.textMode==TextMode::Auto&&text.primary==L"Primary ; # text 日本語"&&
            text.secondary==L"Second\\nline"&&text.badge==L"25%","global legacy enabled TileText preserves content and original trimming behavior");
        Check(tile.policy.Mode()==L"Registration"&&tile.policy.Read(L"Settings",L"GeneratedAssetCacheMaxEntries",L"")==L"73"&&
            tile.policy.Read(L"Assets",L"WideTile",L"")==L"0","delivery aliases cache budgets and disabled assets retain explicit overrides");
    }
    Preserved(source,result);
    const auto document=aip::ParseIniDocument(result);const auto read=Reader(document);
    Check(read(L"Tile.desktop",L"Account",L"").empty()&&read(L"Tile.desktop",L"Notes.LTokenProtected",L"").empty(),
        "opaque account values and gallery ownership remain in place, never copied into an unrelated tile");
    auto disabled=source;aip::WriteIniValueToText(disabled,L"TileText",L"Enabled",L"0");
    Check(Upgrade(disabled,result,config,error)&&config.tiles.front().configuration.textMode==TextMode::Off&&
        config.tiles.front().configuration.entries.front().custom.primary==L"Primary ; # text 日本語",
        "disabled legacy text remains saved and hidden rather than discarded or enabled");
}
void RefuseLoss(){
    const auto original=Fixture(LegacyReleases[24]);
    const auto refused=[&](std::wstring source){
        const auto before=source;std::wstring result,error;TileInstanceConfiguration config;
        const auto doc=aip::ParseIniDocument(source);TileInstanceMigrationPlan plan;
        Check(PlanLegacyTileInstanceMigration(Reader(doc),plan,error)&&plan.legacyModeRequired&&!plan.legacyReason.empty()&&!plan.changed&&plan.settings.empty(),
            "unsupported composition returns an explicit successful legacy-mode decision");
        Check(Upgrade(source,result,config,error)&&!config.configured&&error.empty()&&source==before&&result==source,
            "unsupported composition keeps its exact legacy document rather than flattening data or failing startup");
    };
    auto source=original;aip::WriteIniValueToText(source,L"Settings",L"ContentSource",L"RssFeed");refused(source);
    source=original;aip::WriteIniValueToText(source,L"Settings",L"ContentSource",L"SMTC");refused(source);
    source=original;aip::WriteIniValueToText(source,L"Content",L"Enabled",L"1");aip::WriteIniValueToText(source,L"Content",L"Count",L"2");
    aip::WriteIniValueToText(source,L"Content",L"CycleEnabled",L"1");refused(source);
    aip::WriteIniValueToText(source,L"Content",L"Enabled",L"0");refused(source);
    source=original;aip::WriteIniValueToText(source,L"Content",L"Enabled",L"1");aip::WriteIniValueToText(source,L"Content.1",L"TextSources",L"CustomText,SMTC");refused(source);
}
}
int main(){HistoricalDefaults();Customized();RefuseLoss();std::cout<<checks<<" legacy release migration checks; "<<failures<<" failures; no files/accounts/providers/packages accessed\n";return failures?1:0;}
