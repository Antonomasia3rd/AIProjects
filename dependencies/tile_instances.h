#pragma once
// Independent, portable tile configuration. This header never reads a file,
// starts a worker, accesses an account, or mutates the user's profile.
#include "content_engine.h"
#include "tile_actions.h"
#include "tile_delivery_policy.h"
#include "content_sources/clock_calendar_model.h"
#include "content_sources/google_calendar_model.h"
#include "content_sources/kuro_notes_model.h"
#include "tile_source_alerts.h"
#include <set>

namespace aip { namespace content {
enum class TileInstanceType { Wallpaper, Gallery, Rss, NowPlaying, PrayerTimes, Genshin, Hsr, Zzz, Discord, QuickAction, Clock, Calendar, Wuwa };
struct TileInstanceDescriptor {
    const wchar_t* value;
    const wchar_t* name;
    TileInstanceType type;
    const wchar_t* source;
    const wchar_t* background;
    int notesResource;
};
inline constexpr TileInstanceDescriptor TileInstanceTypes[] = {
    {L"Wallpaper", L"Desktop", TileInstanceType::Wallpaper, L"CustomText", L"Wallpaper", -1},
    {L"Gallery", L"Photo gallery", TileInstanceType::Gallery, L"", L"Transparent", -1},
    {L"RSS", L"RSS", TileInstanceType::Rss, L"RssFeed", L"Transparent", -1},
    {L"NowPlaying", L"Now playing", TileInstanceType::NowPlaying, L"SMTC", L"Transparent", -1},
    {L"PrayerTimes", L"Prayer times", TileInstanceType::PrayerTimes, L"PrayerTimes", L"Transparent", -1},
    {L"Genshin", L"Genshin Impact", TileInstanceType::Genshin, L"Notes", L"Transparent", 0},
    {L"HSR", L"Honkai: Star Rail", TileInstanceType::Hsr, L"Notes", L"Transparent", 1},
    {L"ZZZ", L"Zenless Zone Zero", TileInstanceType::Zzz, L"Notes", L"Transparent", 2},
    {L"Discord", L"Discord", TileInstanceType::Discord, L"DiscordRPC", L"Transparent", -1},
    {L"QuickAction", L"Quick action", TileInstanceType::QuickAction, L"", L"Transparent", -1},
    {L"Clock", L"Clock / world clock", TileInstanceType::Clock, L"CustomText", L"Transparent", -1},
    {L"Calendar", L"Calendar", TileInstanceType::Calendar, L"CustomText", L"Transparent", -1},
    {L"WuWa", L"Wuthering Waves", TileInstanceType::Wuwa, L"CustomText", L"Transparent", -1},
};
inline const TileInstanceDescriptor* FindTileInstanceType(const std::wstring& value) {
    const auto canonical = Lower(Trim(value));
    for (const auto& descriptor : TileInstanceTypes) if (canonical == Lower(descriptor.value)) return &descriptor;
    return nullptr;
}
inline const TileInstanceDescriptor& DescribeTileInstanceType(TileInstanceType type) {
    for (const auto& descriptor : TileInstanceTypes) if (descriptor.type == type) return descriptor;
    return TileInstanceTypes[0];
}
// SecondaryTile.TileId has a native 64-character limit. Keep the complete
// AipSource_ identity within it; there is no numbered-content/count ceiling.
inline bool ValidTileInstanceId(const std::wstring& id) {
    if (id.empty() || id.size() > 54) return false;
    for (const auto ch : id) if (!((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_')) return false;
    return true;
}
inline std::wstring TileInstanceSection(const std::wstring& id) { return L"Tile." + id; }
struct TileInstance {
    std::wstring id, name;
    TileInstanceType type = TileInstanceType::Wallpaper;
    bool primary = false;
    Configuration configuration;
    TileAppearance appearance;
    TextPresentation presentation;
    TileDeliveryPolicy policy;
    aip::datetime::ClockConfiguration clock;
    aip::datetime::CalendarConfiguration calendar;
    aip::google_calendar::Configuration googleCalendar;
    aip::kuro::Configuration kuro;
    std::wstring clickAction = L"setup", accountReference;
    std::wstring error; // Optional host diagnostic for a dormant invalid tile.
    // Additional provider fields not represented by content::Configuration.
    // Keys are lowercase canonical Section.Key names; these values belong to
    // the same immutable profile revision as configuration and appearance.
    std::map<std::wstring,std::wstring> settings;
};
struct TileInstanceConfiguration {
    bool configured = false;
    std::vector<TileInstance> tiles;
};
inline std::wstring TileInstanceNativeId(const TileInstance& tile) { return tile.primary ? L"" : L"AipSource_" + tile.id; }
inline bool TileInstanceClearsOnShutdown(const TileInstance& tile) {
    return tile.policy.Read(L"Settings",L"ClearLiveTileOnShutdown",L"1")==L"1";
}
inline bool ParseTileInstanceIds(const std::wstring& text, std::vector<std::wstring>& output, std::wstring& error) {
    std::vector<std::wstring> ids; std::set<std::wstring> seen;
    if (!Trim(text).empty()) {
        std::size_t start = 0;
        for (;;) {
            const auto end = text.find(L',', start);
            auto id = Lower(Trim(text.substr(start, end == std::wstring::npos ? end : end - start)));
            if (!ValidTileInstanceId(id) || !seen.insert(id).second) {
                error = L"Tiles.Instances requires unique IDs of 1 to 54 lowercase letters, digits, hyphens or underscores."; return false;
            }
            ids.push_back(std::move(id));
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
    }
    output = std::move(ids); error.clear(); return true;
}
inline bool ValidTileAccountReference(const std::wstring& value) {
    if (value.empty()) return true;
    const auto lower = Lower(value);
    for (const auto& resource : NotesResources) if (lower == L"notes." + Lower(resource.value)) return true;
    return lower.compare(0, 8, L"account.") == 0 && ValidTileInstanceId(lower.substr(8));
}
inline bool IsTileInstanceSetting(const std::wstring& section, const std::wstring& key) {
    const auto s = Lower(Trim(section)), k = Lower(Trim(key));
    if (s == L"tiles") return k == L"instances" || k == L"schemaversion";
    if (s.compare(0, 5, L"tile.") != 0 || !ValidTileInstanceId(s.substr(5))) return false;
    // Legacy fixed-source appearance/name/content keys still reach their old
    // normalizer until the complete document selects the instance schema.
    bool legacy = false;
    for (const auto* id : {L"smtc",L"rss",L"genshin",L"hsr",L"zzz",L"discord",L"prayer"}) if (s.substr(5) == id) legacy = true;
    return k == L"type" || k == L"primary" || k == L"account" || k == L"clickaction" || k == L"refreshseconds" || k == L"textmode" ||
        k == L"action" || k == L"volumesteppercent" || k.find(L'.') != std::wstring::npos ||
        (!legacy && (k == L"name" || k == L"clickaction" || k == L"background" || k == L"branding" || k == L"backgroundcolor" || IsTextPresentationKey(key)));
}
inline bool TileInstanceSourceSection(const TileInstance& tile, const std::wstring& section) {
    const auto s = Lower(section);
    if (s == L"alerts") return tile.type == TileInstanceType::Rss || tile.type == TileInstanceType::PrayerTimes;
    if (tile.type == TileInstanceType::Wuwa) return s == L"kuro";
    if (tile.type == TileInstanceType::Rss) return s == L"rssfeed";
    if (tile.type == TileInstanceType::NowPlaying) return s == L"smtc";
    if (tile.type == TileInstanceType::PrayerTimes) return s == L"prayertimes";
    if (tile.type == TileInstanceType::Clock) return s == L"clock";
    if (tile.type == TileInstanceType::Calendar) return s == L"calendar" || s == L"googlecalendar";
    if (tile.type == TileInstanceType::Discord) return s == L"discord";
    const auto resource = DescribeTileInstanceType(tile.type).notesResource;
    return resource >= 0 && (s == L"notes" || s == L"notes." + Lower(NotesResources[resource].value));
}
// Adapt existing provider readers without duplicating their validation. The
// selected type is the only source of composition; unrelated global sections
// cannot start background modules or poison an instance's configuration.
template<class Reader> std::wstring TileInstanceRead(const TileInstance& tile, Reader read,
        const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
    const auto s = Lower(section), k = Lower(key), tileSection = TileInstanceSection(tile.id);
    const auto captured = tile.settings.find(s + L"." + k);
    if (captured != tile.settings.end()) return captured->second;
    const auto& descriptor = DescribeTileInstanceType(tile.type);
    const auto local = [&](const std::wstring& field, const wchar_t* defaultValue) {
        return read(tileSection.c_str(), field.c_str(), defaultValue);
    };
    if (s == L"tiletext") return local(key, fallback);
    if (s == L"mediaactions" && k == L"volumesteppercent") return local(L"VolumeStepPercent",L"5");
    if (s == L"content") {
        if (k == L"enabled") return L"1";
        if (k == L"count") return L"1";
        if (k == L"cycleenabled") return L"0";
        if (k == L"textmode") return local(L"TextMode", L"Auto");
        if (k == L"refreshseconds") return local(L"RefreshSeconds", fallback);
        return fallback;
    }
    if (s == L"content.1") {
        if (k == L"enabled") return L"1";
        if (k == L"name") return tile.name;
        if (k == L"textsources") return descriptor.source;
        if (k == L"notesresource") return descriptor.notesResource < 0 ? L"Resin" : NotesResources[descriptor.notesResource].value;
        if (k == L"background") return local(L"Content.Background", descriptor.background);
        return local(L"Content." + std::wstring(key), fallback);
    }
    if (!TileInstanceSourceSection(tile, section)) return fallback;
    if (s.compare(0, 6, L"notes.") == 0) {
        const auto inheritedRegion = tile.accountReference.empty() ? std::wstring(fallback) : read(tile.accountReference.c_str(),L"Region",fallback);
        const auto inherited = tile.accountReference.empty() ? std::wstring(fallback) : read(tile.accountReference.c_str(),key,k == L"server" ? inheritedRegion.c_str() : fallback);
        // An explicit blank server means auto-detect from this tile's UID,
        // while an absent server can still use the selected saved account.
        if (k == L"server") return local(L"Notes.Server",local(L"Notes.Region",inherited.c_str()).c_str());
        const auto value = local(L"Notes." + std::wstring(key), inherited.c_str());
        return value.empty() && !tile.accountReference.empty() ? inherited : value;
    }
    return local(std::wstring(section) + L"." + key, fallback);
}
inline bool NormalizeTileInstanceSetting(const std::wstring& section, const std::wstring& key,
        std::wstring& value, std::wstring& error) {
    const auto s = Lower(Trim(section)), k = Lower(Trim(key));
    const auto fail = [&](const wchar_t* reason) { error = section + L"." + key + L": " + reason; return false; };
    if (s == L"tiles") {
        if (k == L"schemaversion") {
            if (Trim(value) != L"1") return fail(L"expected schema version 1.");
            value = L"1"; error.clear(); return true;
        }
        if (k == L"instances") {
            std::vector<std::wstring> ids;
            if (!ParseTileInstanceIds(value, ids, error)) return false;
            value.clear(); for (const auto& id : ids) { if (!value.empty()) value += L","; value += id; }
            error.clear(); return true;
        }
        return fail(L"unknown tile collection setting.");
    }
    if (s.compare(0, 5, L"tile.") != 0 || !ValidTileInstanceId(s.substr(5))) return fail(L"invalid tile instance section.");
    if (FindTilePolicyField(key)) return NormalizeTilePolicySetting(key,value,error);
    if (k == L"type") {
        const auto* descriptor = FindTileInstanceType(value);
        if (!descriptor) return fail(L"unknown tile type.");
        value = descriptor->value;
    } else if (k == L"name") {
        if (Trim(value).empty() || value.size() > MaxText || value.find_first_of(L"\r\n") != std::wstring::npos || value.find(L'\0') != std::wstring::npos) return fail(L"expected a nonempty single-line name of at most 4096 characters.");
    } else if (k == L"primary") {
        bool enabled = false;
        if (!ParseBool(value, enabled)) return fail(L"expected a boolean.");
        value = enabled ? L"1" : L"0";
    } else if (k == L"account") {
        value = Trim(value);
        if (!ValidTileAccountReference(value)) return fail(L"expected an explicit Notes.<resource> or Account.<id> reference.");
    } else if (k == L"clickaction") {
        if (Lower(Trim(value)) == L"source") { value = L"source"; error.clear(); return true; }
        return NormalizeTileActionSetting(section, key, value, error);
    } else if (k == L"background" || k == L"branding" || k == L"backgroundcolor") {
        return NormalizeTileAppearanceSetting(key, value, error);
    } else if (IsTextPresentationKey(key)) {
        return NormalizeTextPresentationSetting(key, value, error);
    } else if (k == L"refreshseconds" || k == L"textmode") {
        return NormalizeSetting(L"Content", key, value, error);
    } else if (k == L"volumesteppercent") {
        return NormalizeTileActionSetting(L"MediaActions", key, value, error);
    } else if (k == L"action") {
        value = Lower(Trim(value)); if (!FindTileAction(value)) return fail(L"expected a supported quick action.");
    } else {
        const auto dot = key.find(L'.');
        if (dot == std::wstring::npos) return fail(L"unknown tile instance setting.");
        auto sourceSection = key.substr(0, dot), sourceKey = key.substr(dot + 1);
        const auto source = Lower(sourceSection);
        if (source == L"alerts") return aip::notifications::NormalizeAlertSetting(sourceKey, value, error);
        if (source == L"kuro") return aip::kuro::NormalizeSetting(sourceKey, value, error);
        if (source == L"googlecalendar") return aip::google_calendar::NormalizeSetting(sourceKey, value, error);
        if (source == L"clock" || source == L"calendar") return aip::datetime::NormalizeSetting(sourceSection, sourceKey, value, error);
        if (source == L"content") return NormalizeSetting(L"Content.1", sourceKey, value, error);
        if (source == L"notes") {
            const auto accountKey = Lower(sourceKey);
            if (accountKey == L"server" || accountKey == L"region") {
                value = Lower(Trim(value));
                for (int resource = 0; resource < 3; ++resource)
                    if (value.empty() || aip::notes::ValidAccountServer(static_cast<aip::notes::ResourceKind>(resource),value)) { error.clear(); return true; }
                return fail(L"expected a supported game server, or blank for automatic detection.");
            }
            if (accountKey == L"uid" || accountKey == L"server" || accountKey == L"ltoken" || accountKey == L"ltuid" ||
                accountKey == L"ltokenprotected" || accountKey == L"ltuidprotected" || accountKey == L"ltokenv2" ||
                accountKey == L"ltuidv2" || accountKey == L"ltoken_v2" || accountKey == L"ltuid_v2") sourceSection = L"Notes.Resin";
            return NormalizeSetting(sourceSection, sourceKey, value, error);
        }
        if (source == L"smtc" || source == L"discord" || source == L"prayertimes") return NormalizeSetting(sourceSection, sourceKey, value, error);
        // RSS and gallery retain the host's canonical native validators. This
        // portable model accepts only their known fields, then the host validates
        // native URL/path semantics before committing the complete document.
        const auto sourceLower = Lower(sourceKey);
        if (source == L"rssfeed") {
            if (sourceLower != L"feedurl" && sourceLower != L"useragent" && sourceLower != L"updateintervalseconds" &&
                sourceLower != L"maxitems" && sourceLower != L"httptimeoutseconds" && sourceLower != L"maxfeedbytes") return fail(L"unknown RSS setting.");
            if (value.find(L'\0') != std::wstring::npos || value.size() > MaxText) return fail(L"invalid bounded RSS value.");
        } else if (source == L"gallery") {
            if (sourceLower != L"folder" && sourceLower != L"groupid" && sourceLower != L"name" && sourceLower != L"recursive" &&
                sourceLower != L"refreshseconds" && sourceLower != L"queuesize" && sourceLower != L"order" && sourceLower != L"fit" && sourceLower != L"seed") return fail(L"unknown gallery setting.");
            if (value.find(L'\0') != std::wstring::npos || value.size() > MaxText) return fail(L"invalid bounded gallery value.");
        } else return fail(L"unknown source setting.");
    }
    error.clear(); return true;
}
template<class Reader> bool ReadTileInstanceConfiguration(Reader read, TileInstanceConfiguration& output, std::wstring& error) {
    TileInstanceConfiguration candidate;
    const std::wstring absent = L"\x1f AIP tile list absent \x1f";
    const auto list = read(L"Tiles", L"Instances", absent.c_str());
    if (list == absent) { output = std::move(candidate); error.clear(); return true; }
    candidate.configured = true;
    auto version = read(L"Tiles", L"SchemaVersion", L"1");
    if (!NormalizeTileInstanceSetting(L"Tiles", L"SchemaVersion", version, error)) return false;
    std::vector<std::wstring> ids;
    if (!ParseTileInstanceIds(list, ids, error)) return false;
    bool primarySeen = false;
    for (const auto& id : ids) {
        TileInstance tile; tile.id = id;
        const auto section = TileInstanceSection(id);
        const auto get = [&](const wchar_t* key, const wchar_t* fallback, std::wstring& target) {
            auto value = read(section.c_str(), key, fallback);
            if (!NormalizeTileInstanceSetting(section, key, value, error)) return false;
            target = std::move(value); return true;
        };
        std::wstring type, primary;
        if (!get(L"Type", L"", type)) return false;
        const auto* descriptor = FindTileInstanceType(type); tile.type = descriptor->type;
        if (!get(L"Name", descriptor->name, tile.name) || !get(L"Primary", L"0", primary) ||
            !get(L"ClickAction", tile.type == TileInstanceType::NowPlaying ? L"toggle" : tile.type == TileInstanceType::Rss ? L"source" : L"setup", tile.clickAction) ||
            !get(L"Account", L"", tile.accountReference)) return false;
        if (tile.clickAction == L"source" && tile.type != TileInstanceType::Rss) { error = section + L".ClickAction=source is only available for RSS tiles."; return false; }
        tile.primary = primary == L"1";
        if (tile.primary && (tile.type != TileInstanceType::Wallpaper || primarySeen)) {
            error = section + L".Primary: only one Wallpaper tile can own the primary Desktop identity."; return false;
        }
        primarySeen = primarySeen || tile.primary;
        if (!tile.accountReference.empty() && descriptor->notesResource < 0) { error = section + L".Account is only applicable to game tiles."; return false; }
        // Date/time bindings have native size-specific branding defaults.
        // An absent preference must remain distinguishable from explicit Name.
        const auto appearanceRead=[&](const wchar_t* area,const wchar_t* key,const wchar_t* fallback){
            const bool dateTime=tile.type==TileInstanceType::Clock||tile.type==TileInstanceType::Calendar;
            return read(area,key,dateTime&&Lower(key)==L"branding"?L"Auto":fallback);
        };
        if (!ReadTileAppearance(appearanceRead, section, tile.appearance, error)) return false;
        if (!ReadTileDeliveryPolicy(read,section,tile.id,tile.policy,error)) return false;
        if (!ReadConfiguration([&](const wchar_t* sourceSection, const wchar_t* key, const wchar_t* fallback) {
            return TileInstanceRead(tile, read, sourceSection, key, fallback);
        }, tile.configuration, error)) { error = section + L": " + error; return false; }
        tile.presentation = tile.configuration.presentation;
        if (tile.type == TileInstanceType::Clock && !aip::datetime::ReadClock([&](const wchar_t* key,const wchar_t* fallback) {
            return TileInstanceRead(tile,read,L"Clock",key,fallback);
        },tile.clock,error)) { error=section+L": "+error;return false; }
        if (tile.type == TileInstanceType::Calendar && !aip::datetime::ReadCalendar([&](const wchar_t* key,const wchar_t* fallback) {
            return TileInstanceRead(tile,read,L"Calendar",key,fallback);
        },tile.calendar,error)) { error=section+L": "+error;return false; }
        if (tile.type == TileInstanceType::Calendar) {
            if (!aip::google_calendar::ReadConfiguration([&](const wchar_t* key,const wchar_t* fallback) {
                return TileInstanceRead(tile,read,L"GoogleCalendar",key,fallback);
            },tile.googleCalendar,error)) { error=section+L": "+error;return false; }
            if (tile.calendar.source==L"Google" && tile.calendar.daysAhead>31) {
                error=section+L": Google Calendar look-ahead supports 1 to 31 days.";return false;
            }
        }
        const auto capture = [&](const wchar_t* sourceSection,const wchar_t* key,const wchar_t* fallback) {
            const auto value = TileInstanceRead(tile,read,sourceSection,key,fallback);
            tile.settings[Lower(sourceSection)+L"."+Lower(key)] = value;
        };
        if (tile.type == TileInstanceType::Rss || tile.type == TileInstanceType::PrayerTimes) {
            for (const auto& field : aip::notifications::AlertFields) {
                if(tile.type==TileInstanceType::Rss&&aip::notifications::IsPrayerAlertField(field.key))continue;
                auto value=TileInstanceRead(tile,read,L"Alerts",field.key,field.fallback);
                if(!aip::notifications::NormalizeAlertSetting(field.key,value,error)) { error=section+L": "+error;return false; }
                tile.settings[L"alerts."+Lower(field.key)]=std::move(value);
            }
        }
        if(tile.type==TileInstanceType::Wuwa) {
            const auto getKuro=[&](const wchar_t* key,const wchar_t* fallback,std::wstring& value) {
                auto inherited=std::wstring(fallback);
                if(std::wstring(key)==L"PlayerId")inherited=TileInstanceRead(tile,read,L"Kuro",L"UID",fallback);
                if(std::wstring(key)==L"Region")inherited=TileInstanceRead(tile,read,L"Kuro",L"Server",fallback);
                value=TileInstanceRead(tile,read,L"Kuro",key,inherited.c_str());
                if(!aip::kuro::NormalizeSetting(key,value,error)){error=section+L": "+error;return false;}
                tile.settings[L"kuro."+Lower(key)]=value;return true;
            };
            if(!getKuro(L"GameToken",L"",tile.kuro.gameToken)||!getKuro(L"GameTokenProtected",L"",tile.kuro.gameTokenProtected)||
                !getKuro(L"PlayerId",L"",tile.kuro.playerId)||!getKuro(L"Region",L"",tile.kuro.region)||!getKuro(L"DeviceId",L"",tile.kuro.deviceId))return false;
            for(const auto& field:{std::make_pair(L"RefreshSeconds",&tile.kuro.refreshSeconds),std::make_pair(L"TimeoutSeconds",&tile.kuro.timeoutSeconds),std::make_pair(L"MaximumBytes",&tile.kuro.maximumBytes)}){
                std::wstring value;const auto fallback=std::to_wstring(*field.second);
                if(!getKuro(field.first,fallback.c_str(),value)||!ParseInt(value,0,16777216,*field.second))return false;
            }
        }
        if (tile.type == TileInstanceType::Rss) {
            capture(L"RssFeed",L"FeedUrl",L"");
            capture(L"RssFeed",L"UserAgent",L"Mozilla/5.0 (Windows NT; DesktopStub RSS Live Tile)");
            capture(L"RssFeed",L"UpdateIntervalSeconds",L"900");
            capture(L"RssFeed",L"MaxItems",L"5");
            capture(L"RssFeed",L"HttpTimeoutSeconds",L"20");
            capture(L"RssFeed",L"MaxFeedBytes",L"2097152");
        }
        capture(L"MediaActions",L"VolumeStepPercent",L"5");
        if (tile.type == TileInstanceType::QuickAction) {
            auto action = read(section.c_str(), L"Action", L"toggle"), step = read(section.c_str(), L"VolumeStepPercent", L"5");
            if (!NormalizeTileInstanceSetting(section, L"Action", action, error) || !NormalizeTileInstanceSetting(section, L"VolumeStepPercent", step, error)) return false;
            tile.clickAction = action;
            tile.settings[L"action"] = action;
        }
        candidate.tiles.push_back(std::move(tile));
    }
    output = std::move(candidate); error.clear(); return true;
}
struct TileInstanceSetting { std::wstring section, key, value; };
struct TileInstanceMigrationPlan { bool changed = false; std::vector<TileInstanceSetting> settings; };
// The host applies this complete plan to an in-memory INI copy, writes a backup,
// validates native provider fields, then commits with its existing atomic writer.
template<class Reader> bool PlanLegacyTileInstanceMigration(Reader read, TileInstanceMigrationPlan& output, std::wstring& error) {
    const std::wstring absent = L"\x1f AIP tile list absent \x1f";
    TileInstanceMigrationPlan plan;
    if (read(L"Tiles", L"Instances", absent.c_str()) != absent) {
        TileInstanceConfiguration existing;
        if (!ReadTileInstanceConfiguration(read, existing, error)) return false;
        for(const auto& tile:existing.tiles)for(const auto& field:TilePolicyFields){
            const auto section=TileInstanceSection(tile.id);
            if(read(section.c_str(),field.key,absent.c_str())!=absent)continue;
            auto value=read(field.section,field.setting,field.fallback);
            if(!NormalizeTilePolicySetting(field.key,value,error)){error=section+L"."+error;return false;}
            plan.settings.push_back({section,field.key,value});
        }
        plan.changed=!plan.settings.empty();output=std::move(plan);error.clear();return true;
    }
    auto add = [&](const std::wstring& section, const std::wstring& key, const std::wstring& value) { plan.settings.push_back({section,key,value}); };
    const auto copy = [&](const std::wstring& target, const std::wstring& sourceSection, const wchar_t* key, const std::wstring& targetKey) {
        const auto value = read(sourceSection.c_str(), key, absent.c_str());
        if (value != absent) add(target, targetKey, value);
    };
    bool mappingsValid = true;
    const auto make = [&](const wchar_t* id, const wchar_t* type, const wchar_t* defaultName, int content, const wchar_t* account) {
        const auto section = TileInstanceSection(id);
        int oldIndex = content;
        const auto mapping = read(section.c_str(), L"Content", std::to_wstring(content).c_str());
        if (std::wstring(id) != L"desktop" && !ParseInt(mapping,1,MaxEntries,oldIndex)) {
            if (mappingsValid) error = section + L".Content must be valid before converting its saved contents.";
            mappingsValid = false;
        }
        const auto oldContent = L"Content." + std::to_wstring(oldIndex);
        add(section, L"Type", type);
        add(section, L"Name", read(section.c_str(), L"Name", read(oldContent.c_str(), L"Name", defaultName).c_str()));
        if (std::wstring(id) == L"desktop") add(section, L"Primary", L"1");
        if (*account) add(section, L"Account", account);
        for (const auto* key : {L"ImagePath",L"Text",L"SecondaryText",L"BadgeText",L"RssItem",L"NotesDetail"}) copy(section, oldContent, key, L"Content." + std::wstring(key));
        if (std::wstring(id) == L"desktop") copy(section, oldContent, L"Background", L"Content.Background");
        copy(section, L"Content", L"RefreshSeconds", L"RefreshSeconds"); copy(section, L"Content", L"TextMode", L"TextMode");
        for(const auto& field:TilePolicyFields){
            const auto local=read(section.c_str(),field.key,absent.c_str());
            if(local!=absent)continue;
            auto value=read(field.section,field.setting,field.fallback);
            if(!NormalizeTilePolicySetting(field.key,value,error)){mappingsValid=false;continue;}
            add(section,field.key,value);
        }
        for (const auto* key : TextPresentationKeys) {
            const auto local = read(section.c_str(), key, absent.c_str());
            if (local == absent || (std::wstring(key) == L"TextPreset" && Lower(Trim(local)) == L"inherit")) {
                const auto inherited = read(L"TileText", key, std::wstring(key) == L"TextPreset" ? L"Default" : absent.c_str());
                if (inherited != absent) add(section, key, inherited);
            }
        }
        return section;
    };
    make(L"desktop", L"Wallpaper", L"Desktop", 1, L"");
    const auto smtc = make(L"smtc", L"NowPlaying", L"Now playing", 2, L"");
    const auto rss = make(L"rss", L"RSS", L"RSS", 3, L"");
    make(L"genshin", L"Genshin", L"Genshin Impact", 4, L"Notes.Resin");
    make(L"hsr", L"HSR", L"Honkai: Star Rail", 5, L"Notes.Stamina");
    make(L"zzz", L"ZZZ", L"Zenless Zone Zero", 6, L"Notes.Charge");
    const auto discord = make(L"discord", L"Discord", L"Discord", 7, L"");
    const auto prayer = make(L"prayer", L"PrayerTimes", L"Prayer times", 8, L"");
    if (!mappingsValid) return false;
    for (const auto* key : {L"Indicator",L"ShowRepeatShuffle"}) copy(smtc, L"SMTC", key, L"SMTC." + std::wstring(key));
    for (const auto* key : {L"FeedUrl",L"UserAgent",L"UpdateIntervalSeconds",L"MaxItems",L"HttpTimeoutSeconds",L"MaxFeedBytes"}) copy(rss, L"RssFeed", key, L"RssFeed." + std::wstring(key));
    for (const auto* key : {L"ProfilePath",L"ClientId",L"Name",L"Details",L"State",L"SendPresence",L"ClearOnStop",L"RefreshSeconds",L"OperationTimeoutMs"}) copy(discord, L"Discord", key, L"Discord." + std::wstring(key));
    for (const auto* key : aip::prayer::SettingKeys) copy(prayer, L"PrayerTimes", key, L"PrayerTimes." + std::wstring(key));
    for (const auto* id : {L"genshin",L"hsr",L"zzz"}) for (const auto* key : {L"RefreshSeconds",L"HttpTimeoutSeconds",L"MaximumBytes"}) copy(TileInstanceSection(id), L"Notes", key, L"Notes." + std::wstring(key));
    // Pin identities are unchanged. Existing gallery identities/ledger remain in
    // their current independent group engine; migration never changes them.
    add(L"Tiles", L"SchemaVersion", L"1");
    add(L"Tiles", L"Instances", L"desktop,smtc,rss,genshin,hsr,zzz,discord,prayer");
    const auto stagedRead = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        for (auto it = plan.settings.rbegin(); it != plan.settings.rend(); ++it)
            if (Lower(it->section) == Lower(section) && Lower(it->key) == Lower(key)) return it->value;
        return read(section, key, fallback);
    };
    TileInstanceConfiguration validated;
    if (!ReadTileInstanceConfiguration(stagedRead, validated, error)) return false;
    plan.changed = true; output = std::move(plan); error.clear(); return true;
}
// Mutator(text, section, key, value, error) must modify only its text argument.
// Failure leaves both the original input and caller's prior output unchanged.
template<class Reader, class Mutator> bool StageLegacyTileInstances(Reader read, const std::wstring& original,
        Mutator mutate, std::wstring& staged, std::wstring& error) {
    TileInstanceMigrationPlan plan;
    if (!PlanLegacyTileInstanceMigration(read, plan, error)) return false;
    std::wstring candidate = original;
    for (const auto& setting : plan.settings) if (!mutate(candidate, setting.section, setting.key, setting.value, error)) return false;
    staged = std::move(candidate); error.clear(); return true;
}
} }
