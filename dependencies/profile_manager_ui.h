#pragma once
// Portable Profiles page for the shared Properties presenter. No window,
// process, filesystem, account, package or Startup operation is performed here.
#include "desktopstub_profiles.h"
#include <memory>
namespace aip { namespace profiles {
enum class UiVerb {None,Create,Start,Stop,Settings,AutoStart,MakeDefault,Identity,Startup,UnifiedTray,CrashNotices};
struct UiAction {
    UiVerb verb=UiVerb::None;ProfileEntry expected;
    std::shared_ptr<const std::wstring> revision;
    bool checked=false;
};
struct UiSnapshot {
    Registry registry;std::set<std::wstring> running;
    std::shared_ptr<const std::wstring> revision;
};
enum class UiKind {Info,Page,Action,Toggle};
struct UiRow {
    UiKind kind=UiKind::Info;std::wstring key,label;bool checked=false,enabled=true;
    UiAction action;std::vector<UiRow> children;
};
template<class Text> std::vector<UiRow> BuildUiRows(const UiSnapshot& snapshot,Text text){
    const auto revision=snapshot.revision?snapshot.revision:std::make_shared<const std::wstring>(SerializeRegistry(snapshot.registry));
    const auto label=[&](const wchar_t* key,const wchar_t* fallback){return text(key,fallback);};
    const auto action=[&](UiVerb verb,std::wstring caption,bool checked=false,bool toggle=false,const ProfileEntry& profile=ProfileEntry{}){
        UiRow row;row.kind=toggle?UiKind::Toggle:UiKind::Action;row.label=std::move(caption);row.checked=checked;
        row.action={verb,profile,revision,checked};return row;
    };
    std::vector<UiRow> rows;
    rows.push_back(action(UiVerb::Create,label(L"ProfilesCreate",L"Create profile...")));
    rows.push_back(action(UiVerb::Startup,label(L"ProfilesStartup",L"Start selected profiles when the main resident starts"),snapshot.registry.startupEnabled,true));
    rows.push_back(action(UiVerb::UnifiedTray,label(L"ProfilesUnifiedTray",L"Use one tray icon for Toast profiles (after restart)"),snapshot.registry.unifiedTray,true));
    rows.push_back(action(UiVerb::CrashNotices,label(L"ProfilesCrashNotices",L"Notify if a profile started here crashes"),snapshot.registry.notifyCrashes,true));
    UiRow info;info.label=label(L"ProfilesExplanation",L"Profiles have separate settings, accounts, tiles and tray icons. New profiles start with factory defaults.");info.enabled=false;rows.push_back(std::move(info));
    for(const auto& profile:snapshot.registry.profiles){
        UiRow page;page.kind=UiKind::Page;page.key=profile.id;page.label=profile.name;
        if(profile.id==snapshot.registry.defaultProfileId)page.label+=label(L"ProfilesDefaultSuffix",L" (default)");
        if(profile.autoStart)page.label+=label(L"ProfilesStartupSuffix",L" [startup]");
        const bool running=snapshot.running.count(profile.id)!=0;
        UiRow path;path.label=profile.iniPath;path.enabled=false;page.children.push_back(std::move(path));
        UiRow status;status.label=label(running?L"ProfilesRunning":L"ProfilesStopped",running?L"Status: running":L"Status: stopped");status.enabled=false;page.children.push_back(std::move(status));
        page.children.push_back(action(UiVerb::Start,label(L"ProfilesStart",L"Start"),false,false,profile));
        auto stop=action(UiVerb::Stop,label(L"ProfilesStop",L"Stop"),false,false,profile);stop.enabled=running;page.children.push_back(std::move(stop));
        page.children.push_back(action(UiVerb::Settings,label(L"ProfilesSettings",L"Open this profile's settings..."),false,false,profile));
        page.children.push_back(action(UiVerb::AutoStart,label(L"ProfilesAutoStart",L"Start this profile automatically"),profile.autoStart,true,profile));
        auto makeDefault=action(UiVerb::MakeDefault,label(L"ProfilesMakeDefault",L"Make default"),false,false,profile);makeDefault.enabled=profile.id!=snapshot.registry.defaultProfileId;page.children.push_back(std::move(makeDefault));
        page.children.push_back(action(UiVerb::Identity,label(L"ProfilesIdentity",L"Identity and migration..."),false,false,profile));
        rows.push_back(std::move(page));
    }
    return rows;
}
} }
