#include "../dependencies/tile_instances.h"
#include <iostream>
#include <map>

namespace {
using Values = std::map<std::pair<std::wstring, std::wstring>, std::wstring>;
int checks = 0, failures = 0;
void Check(bool condition, const char* message) {
    ++checks; if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void Put(Values& values, const std::wstring& section, const std::wstring& key, const std::wstring& value) {
    values[{aip::content::Lower(section), aip::content::Lower(key)}] = value;
}
auto Reader(const Values& values) {
    return [&values](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) -> std::wstring {
        const auto found = values.find({aip::content::Lower(section), aip::content::Lower(key)});
        return found == values.end() ? std::wstring(fallback) : found->second;
    };
}
void Tile(Values& values, const std::wstring& id, const wchar_t* type) { Put(values, L"Tile." + id, L"Type", type); }
}
int main() {
    using namespace aip::content;
    Values values; TileInstanceConfiguration cfg; std::wstring error;
    Check(ReadTileInstanceConfiguration(Reader(values), cfg, error) && !cfg.configured && cfg.tiles.empty(), "absent Instances preserves legacy mode");
    Put(values, L"Tiles", L"Instances", L"");
    Check(ReadTileInstanceConfiguration(Reader(values), cfg, error) && cfg.configured && cfg.tiles.empty(), "explicit empty instance list starts no types");
    Put(values, L"Tiles", L"Instances", L"desktop,rss1,rss2,prayer1,prayer2,game1,game2,smtc,action");
    Tile(values, L"desktop", L"Wallpaper"); Put(values, L"Tile.desktop", L"Primary", L"1");
    Tile(values, L"rss1", L"RSS"); Tile(values, L"rss2", L"RSS");
    Tile(values, L"prayer1", L"PrayerTimes"); Tile(values, L"prayer2", L"PrayerTimes");
    Tile(values, L"game1", L"Genshin"); Tile(values, L"game2", L"HSR");
    Tile(values, L"smtc", L"NowPlaying"); Tile(values, L"action", L"QuickAction");
    Put(values, L"Tile.rss1", L"RssFeed.FeedUrl", L"https://example.invalid/one.atom");
    Put(values, L"Tile.rss2", L"RssFeed.FeedUrl", L"https://example.invalid/two.atom");
    Put(values, L"Tile.rss1", L"RssFeed.UpdateIntervalSeconds", L"300");
    Put(values, L"Tile.rss2", L"RssFeed.UpdateIntervalSeconds", L"900");
    Put(values, L"Tile.prayer1", L"PrayerTimes.City", L"Surabaya");
    Put(values, L"Tile.prayer2", L"PrayerTimes.City", L"Jakarta");
    Put(values, L"Tile.game1", L"Account", L"Account.first");
    Put(values, L"Tile.game2", L"Account", L"Account.second");
    Put(values, L"Account.first", L"UID", L"111111111");
    Put(values, L"Account.second", L"UID", L"222222222");
    Put(values, L"Tile.game2", L"Notes.UID", L"333333333");
    Put(values, L"Tile.rss2", L"TextPreset", L"RssHeadlines");
    Put(values, L"Tile.action", L"Action", L"volume-down");
    Put(values, L"Tile.action", L"VolumeStepPercent", L"12");
    Check(ReadTileInstanceConfiguration(Reader(values), cfg, error), "independent multi-source document parses");
    Check(cfg.tiles.size() == 9 && cfg.tiles[0].primary && TileInstanceNativeId(cfg.tiles[0]).empty() && TileInstanceNativeId(cfg.tiles[1]) == L"AipSource_rss1", "ordered stable identities retain primary Desktop special case");
    Check(cfg.tiles[3].configuration.prayer.city == L"Surabaya" && cfg.tiles[4].configuration.prayer.city == L"Jakarta", "prayer locations are independent");
    Check(cfg.tiles[5].configuration.notes.accounts[0].uid == L"111111111" && cfg.tiles[6].configuration.notes.accounts[1].uid == L"333333333", "explicit accounts are referenced with local override and game-specific resource");
    Check(cfg.tiles[5].configuration.notes.accounts[1].uid.empty() && cfg.tiles[6].configuration.notes.accounts[0].uid.empty(), "other games are not implicitly loaded");
    Check(cfg.tiles[1].presentation.preset == L"Default" && cfg.tiles[2].presentation.preset == L"RssHeadlines", "appearance templates do not leak across RSS instances");
    Check(cfg.tiles[8].clickAction == L"volume-down", "quick action is part of selected tile");
    Check(cfg.tiles[1].clickAction == L"source", "RSS defaults explicitly to opening its featured article");
    Check(TileInstanceRead(cfg.tiles[1], Reader(values), L"RssFeed", L"FeedUrl", L"") != TileInstanceRead(cfg.tiles[2], Reader(values), L"RssFeed", L"FeedUrl", L""), "per-tile RSS URLs are independent");
    Check(TileInstanceRead(cfg.tiles[1], Reader(values), L"RssFeed", L"UpdateIntervalSeconds", L"") == L"300" && TileInstanceRead(cfg.tiles[2], Reader(values), L"RssFeed", L"UpdateIntervalSeconds", L"") == L"900", "per-tile RSS intervals are independent");
    Put(values,L"Tile.rss1",L"RssFeed.FeedUrl",L"https://example.invalid/new-revision.atom");
    Put(values,L"Tile.rss1",L"RssFeed.UpdateIntervalSeconds",L"3600");
    Check(TileInstanceRead(cfg.tiles[1],Reader(values),L"RssFeed",L"FeedUrl",L"") == L"https://example.invalid/one.atom" &&
        TileInstanceRead(cfg.tiles[1],Reader(values),L"RssFeed",L"UpdateIntervalSeconds",L"") == L"300", "provider settings remain bound to the parsed profile revision after the backing reader changes");
    Check(TileInstanceRead(cfg.tiles[8],Reader(values),L"MediaActions",L"VolumeStepPercent",L"") == L"12", "per-tile quick action volume step is captured independently");
    Put(values,L"Tile.game1",L"Notes.UID",L"");
    Check(ReadTileInstanceConfiguration(Reader(values),cfg,error) && cfg.tiles[5].configuration.notes.accounts[0].uid == L"111111111", "clearing a game UID override returns to the explicitly selected account");
    Put(values,L"Account.second",L"Server",L"os_asia");
    Put(values,L"Tile.game2",L"Notes.Server",L"");
    Check(ReadTileInstanceConfiguration(Reader(values),cfg,error) && cfg.tiles[6].configuration.notes.accounts[1].server.empty(), "explicit blank local server keeps auto-detection instead of inheriting another game's server");
    std::wstring server = L"prod_official_asia";
    Check(NormalizeTileInstanceSetting(L"Tile.game2",L"Notes.Server",server,error), "server setting normalization accepts HSR values before type-aware complete validation");
    Put(values, L"PrayerTimes", L"Method", L"bad dormant setting");
    Put(values, L"Notes", L"RefreshSeconds", L"bad dormant setting");
    Put(values, L"CapsBlink", L"HardwareEnabled", L"1");
    Put(values, L"AsusBlink", L"HardwareEnabled", L"1");
    Put(values, L"SMTC", L"Indicator", L"bad dormant setting");
    Put(values, L"TileText", L"TextPreset", L"bad dormant setting");
    Check(ReadTileInstanceConfiguration(Reader(values), cfg, error), "unrelated dormant globals do not poison new tile configurations");
    for (const auto& tile : cfg.tiles) Check(!tile.configuration.caps.actionsEnabled && !tile.configuration.asus.hardwareEnabled && tile.configuration.entries.size() == 1, "tile type composition cannot activate unrelated hardware or numbered entries");
    const auto prior = cfg.tiles.size();
    Put(values, L"Tile.prayer1", L"PrayerTimes.Method", L"invalid");
    Check(!ReadTileInstanceConfiguration(Reader(values), cfg, error) && cfg.tiles.size() == prior && error.find(L"Tile.prayer1") != std::wstring::npos, "selected-source error is scoped and leaves prior configuration unchanged");
    Put(values, L"Tile.prayer1", L"PrayerTimes.Method", L"20");
    for (const auto* bad : {L"rss1,rss1",L"rss1,RSS1",L"rss1,",L",rss1",L"a.b",L"white space",L"a/b"}) {
        std::vector<std::wstring> ids{L"preserve"};
        Check(!ParseTileInstanceIds(bad, ids, error) && ids == std::vector<std::wstring>{L"preserve"}, "invalid/doubled IDs rejected without partial output");
    }
    Check(ValidTileInstanceId(std::wstring(54, L'a')) && !ValidTileInstanceId(std::wstring(55,L'a')), "native identity bounds applied independently of list count");
    Put(values, L"Tile.rss1", L"Primary", L"1");
    Check(!ReadTileInstanceConfiguration(Reader(values), cfg, error), "non-wallpaper instance cannot claim primary Desktop identity");
    Put(values, L"Tile.rss1", L"Primary", L"0");
    Tile(values, L"rss1", L"Wallpaper"); Put(values, L"Tile.rss1", L"Primary", L"1");
    Check(!ReadTileInstanceConfiguration(Reader(values), cfg, error), "two wallpaper instances cannot claim primary identity");
    Values many; std::wstring list;
    for (int i = 0; i < 1100; ++i) {
        const auto id = L"rss-" + std::to_wstring(i); if (!list.empty()) list += L","; list += id;
        Tile(many, id, L"RSS");
    }
    Put(many, L"Tiles", L"Instances", list);
    Check(ReadTileInstanceConfiguration(Reader(many), cfg, error) && cfg.tiles.size() == 1100, "instance catalog has no eight, seven, 32 or 1024 entry ceiling");
    std::wstring value = L"37";
    Check(NormalizeTileInstanceSetting(L"Tile.rss-new", L"RefreshSeconds", value, error) && value == L"37", "instance general refresh uses canonical validation");
    value = L"0";
    Check(!NormalizeTileInstanceSetting(L"Tile.rss-new", L"RefreshSeconds", value, error), "invalid canonical refresh is rejected");
    value = L"subtitle";
    Check(NormalizeTileInstanceSetting(L"Tile.rss-new", L"TextStyle", value, error), "presentation uses shared validator");
    value = L"notes_chargE";
    Check(!NormalizeTileInstanceSetting(L"Tile.game", L"Account", value, error), "implicit ambiguous account reference rejected");
    Check(IsTileInstanceSetting(L"Tiles", L"Instances") && !IsTileInstanceSetting(L"Tiles", L"Enabled") &&
        IsTileInstanceSetting(L"Tile.rss", L"RssFeed.FeedUrl") && !IsTileInstanceSetting(L"Tile.rss", L"Content") &&
        IsTileInstanceSetting(L"Tile.new-rss", L"Name"), "new setting recognizer preserves old fixed source normalization paths");
    Values legacy;
    Put(legacy, L"Content.1", L"Name", L"My desktop");
    Put(legacy, L"Content.1", L"Text", L"Visible custom text");
    Put(legacy, L"Tile.rss", L"Name", L"Windhawk releases");
    Put(legacy, L"Tile.rss", L"Content", L"12");
    Put(legacy, L"Tile.rss", L"TextPreset", L"Inherit");
    Put(legacy, L"Content.12", L"RssItem", L"4");
    Put(legacy, L"RssFeed", L"FeedUrl", L"https://mods.windhawk.net/releases.atom");
    Put(legacy, L"PrayerTimes", L"City", L"Surabaya");
    Put(legacy, L"Notes.Resin", L"UID", L"111111111");
    Put(legacy, L"Gallery", L"GroupId", L"preserve-owner");
    Put(legacy, L"Gallery.Tile.1", L"Id", L"preserve-photo-pin");
    TileInstanceMigrationPlan plan;
    Check(PlanLegacyTileInstanceMigration(Reader(legacy), plan, error) && plan.changed, "legacy document produces validated complete conversion plan");
    Values migrated = legacy;
    for (const auto& setting : plan.settings) Put(migrated, setting.section, setting.key, setting.value);
    Check(ReadTileInstanceConfiguration(Reader(migrated), cfg, error) && cfg.tiles.size() == 8, "conversion stages complete typed instances");
    Check(cfg.tiles[0].name == L"My desktop" && cfg.tiles[2].name == L"Windhawk releases" && cfg.tiles[2].configuration.entries[0].rssItem == 4, "conversion preserves actual custom content mapping and names");
    Check(cfg.tiles[2].presentation.preset == L"Default", "orphan legacy Inherit is materialized as independent Default");
    Check(cfg.tiles[4].id == L"hsr" && TileInstanceNativeId(cfg.tiles[4]) == L"AipSource_hsr", "conversion preserves existing source pin identities");
    Check(cfg.tiles[3].configuration.notes.accounts[0].uid == L"111111111" && Reader(migrated)(L"Gallery",L"GroupId",L"") == L"preserve-owner", "conversion references existing account and preserves gallery ownership");
    bool copiedSecret = false;
    for (const auto& setting : plan.settings) if (Lower(setting.key).find(L"ltoken") != std::wstring::npos || setting.value == L"111111111") copiedSecret = true;
    Check(!copiedSecret, "conversion preserves credential references instead of duplicating account fields");
    Check(PlanLegacyTileInstanceMigration(Reader(migrated), plan, error) && !plan.changed && plan.settings.empty(), "second conversion is an idempotent no-op");
    auto invalidLegacy = legacy; Put(invalidLegacy,L"Tile.rss",L"Content",L"bad mapping");
    plan.changed = true; plan.settings = {{L"prior",L"key",L"value"}};
    Check(!PlanLegacyTileInstanceMigration(Reader(invalidLegacy),plan,error) && plan.changed && plan.settings.size() == 1 && plan.settings[0].section == L"prior",
        "invalid original mappings cannot silently discard saved content during conversion");
    std::wstring staged = L"prior staged result";
    Check(!StageLegacyTileInstances(Reader(legacy), L"original profile", [](std::wstring&,const std::wstring&,const std::wstring&,const std::wstring&,std::wstring& issue) {
        issue=L"injected mutation failure"; return false;
    }, staged,error) && staged == L"prior staged result", "failed in-memory mutation cannot return a partially converted profile");
    Check(StageLegacyTileInstances(Reader(migrated), L"original profile", [](std::wstring&,const std::wstring&,const std::wstring&,const std::wstring&,std::wstring&) { return false; },staged,error) && staged == L"original profile", "unchanged profile staging does not invoke mutation callback");
    std::cout << checks << " tile instance checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}
