#pragma once
#include "tile_instances.h"
namespace aip { namespace content {
inline std::wstring TileInstanceArtworkTopic(const TileInstance& tile) {
    switch (tile.type) {
    case TileInstanceType::Wallpaper: return L"desktop";
    case TileInstanceType::Gallery: return L"gallery";
    case TileInstanceType::Rss: return L"rss";
    case TileInstanceType::NowPlaying: return L"smtc";
    case TileInstanceType::PrayerTimes: return L"prayer";
    case TileInstanceType::Clock:return tile.clock.iconStyle==L"Clock"?L"clock":tile.clock.iconStyle==L"WorldClock"?L"worldclock":
        aip::datetime::Lower(tile.clock.timeZone)==L"local"&&(tile.clock.location.empty()||tile.clock.location==L"Local time")?L"clock":L"worldclock";
    case TileInstanceType::Calendar: return L"calendar";
    case TileInstanceType::Battery: return L"battery";
    case TileInstanceType::Wuwa: return L"wuwa";
    case TileInstanceType::Genshin: return L"genshin";
    case TileInstanceType::Hsr: return L"hsr";
    case TileInstanceType::Zzz: return L"zzz";
    case TileInstanceType::Discord: return L"discord";
    case TileInstanceType::QuickAction: return tile.clickAction;
    case TileInstanceType::AppLauncher:return L"desktop";
    }
    return {};
}
inline bool UsesOriginalPrimaryDesktopArtwork(const Snapshot& snapshot) {
    return snapshot.artworkTopic==L"desktop"&&snapshot.secondaryTileId.empty();
}
}} // namespace aip::content
