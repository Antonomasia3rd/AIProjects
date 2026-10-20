#pragma once
#include "tile_instances.h"
namespace aip { namespace content {
inline std::wstring TileInstanceArtworkTopic(const TileInstance& tile) {
    switch (tile.type) {
    case TileInstanceType::Wallpaper: case TileInstanceType::Gallery: return L"gallery";
    case TileInstanceType::Rss: return L"rss";
    case TileInstanceType::NowPlaying: return L"smtc";
    case TileInstanceType::PrayerTimes: return L"prayer";
    case TileInstanceType::Genshin: return L"genshin";
    case TileInstanceType::Hsr: return L"hsr";
    case TileInstanceType::Zzz: return L"zzz";
    case TileInstanceType::Discord: return L"discord";
    case TileInstanceType::QuickAction: return tile.clickAction;
    }
    return {};
}
}} // namespace aip::content
