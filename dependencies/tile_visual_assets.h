#pragma once
// Pure catalog/path plan for repo-native tile artwork. No Windows APIs or files.
#include <cstdint>
#include <string>
#include <vector>

namespace aip { namespace tile_visual {
enum class Glyph { Music, Feed, Genshin, StarRail, Zenless, Chat, Toggle, Play, Pause,
    Previous, Next, Repeat, Shuffle, VolumeDown, VolumeUp };
struct Topic { const wchar_t* key; Glyph glyph; std::uint32_t rgb; };
inline constexpr Topic Topics[] = {
    {L"smtc", Glyph::Music, 0x159b91}, {L"rss", Glyph::Feed, 0xe77924},
    {L"genshin", Glyph::Genshin, 0x397cca}, {L"hsr", Glyph::StarRail, 0x7756bd},
    {L"zzz", Glyph::Zenless, 0xbf8c17}, {L"discord", Glyph::Chat, 0x5865f2},
    {L"toggle", Glyph::Toggle, 0x159b91}, {L"play", Glyph::Play, 0x159b91},
    {L"pause", Glyph::Pause, 0x159b91}, {L"previous", Glyph::Previous, 0x159b91},
    {L"next", Glyph::Next, 0x159b91}, {L"repeat", Glyph::Repeat, 0x159b91},
    {L"shuffle", Glyph::Shuffle, 0x159b91}, {L"volume-down", Glyph::VolumeDown, 0x159b91},
    {L"volume-up", Glyph::VolumeUp, 0x159b91}
};
inline const Topic* FindTopic(std::wstring key) {
    const std::wstring source = L"AipSource_", action = L"AipAction_";
    if (key.compare(0, source.size(), source) == 0) key.erase(0, source.size());
    else if (key.compare(0, action.size(), action) == 0) {
        key.erase(0, action.size());
        for (auto& ch : key) if (ch == L'_') ch = L'-';
    }
    for (auto& ch : key) if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
    if (key == L"rssfeed") key = L"rss";
    if (key == L"discordrpc") key = L"discord";
    for (const auto& topic : Topics) if (key == topic.key) return &topic;
    return nullptr;
}
struct Asset { std::wstring relative; unsigned width, height; bool transparent; };
inline std::vector<Asset> AssetPlan(const Topic& topic) {
    const auto base = std::wstring(L"Assets/TopicTiles/v1/") + topic.key + L"/";
    return {{base + L"Medium.png", 150, 150, false}, {base + L"Wide.png", 310, 150, false},
        {base + L"Large.png", 310, 310, false}, {base + L"Small.png", 70, 70, true},
        {base + L"Logo44.png", 44, 44, true}};
}
struct Assets { std::wstring medium, wide, large, smallTile, logo44; };
inline std::wstring PackageUri(const std::wstring& relative) { return L"ms-appx:///" + relative; }
} }
