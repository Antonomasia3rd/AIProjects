#pragma once
// Pure catalog/path plan for repo-native tile artwork. No Windows APIs or files.
#include <cstdint>
#include <string>
#include <vector>
#include "tile_icon_font_policy.h"

namespace aip { namespace tile_visual {
enum class Glyph { Music, Feed, Genshin, StarRail, Zenless, Chat, Toggle, Play, Pause,
    Previous, Next, Repeat, Shuffle, VolumeDown, VolumeUp, Stop, Unknown,
    RepeatOff, RepeatTrack, ShuffleOff, Resin, Stamina, Charge, Photo, Prayer, Clock, Calendar, Game, Desktop, WorldClock, Battery };
struct Topic { const wchar_t* key; Glyph glyph; std::uint32_t rgb; };
inline constexpr Topic Topics[] = {
    {L"smtc", Glyph::Music, 0x159b91}, {L"rss", Glyph::Feed, 0xe77924},
    {L"genshin", Glyph::Genshin, 0x397cca}, {L"hsr", Glyph::StarRail, 0x7756bd},
    {L"zzz", Glyph::Zenless, 0xbf8c17}, {L"discord", Glyph::Chat, 0x5865f2},
    {L"toggle", Glyph::Toggle, 0x159b91}, {L"play", Glyph::Play, 0x159b91},
    {L"pause", Glyph::Pause, 0x159b91}, {L"previous", Glyph::Previous, 0x159b91},
    {L"next", Glyph::Next, 0x159b91}, {L"repeat", Glyph::Repeat, 0x159b91},
    {L"shuffle", Glyph::Shuffle, 0x159b91}, {L"volume-down", Glyph::VolumeDown, 0x159b91},
    {L"volume-up", Glyph::VolumeUp, 0x159b91},
    {L"stop", Glyph::Stop, 0x159b91}, {L"unknown", Glyph::Unknown, 0x159b91},
    {L"repeat-off", Glyph::RepeatOff, 0x159b91}, {L"repeat-list", Glyph::Repeat, 0x159b91},
    {L"repeat-track", Glyph::RepeatTrack, 0x159b91}, {L"shuffle-off", Glyph::ShuffleOff, 0x159b91},
    {L"shuffle-on", Glyph::Shuffle, 0x159b91}, {L"resin", Glyph::Resin, 0x397cca},
    {L"stamina", Glyph::Stamina, 0x7756bd}, {L"charge", Glyph::Charge, 0xbf8c17},
    {L"gallery", Glyph::Photo, 0x477dac}, {L"prayer", Glyph::Prayer, 0x16876b},
    {L"desktop", Glyph::Desktop, 0x477dac},
    {L"clock",Glyph::Clock,0x477dac},{L"worldclock",Glyph::WorldClock,0x477dac},
    {L"calendar",Glyph::Calendar,0x477dac},{L"wuwa",Glyph::Game,0x477dac},{L"battery",Glyph::Battery,0x477dac}
};
inline const Topic* FindTopic(std::wstring key) {
    const std::wstring source = L"AipSource_", action = L"AipAction_";
    const bool sourceId = key.compare(0, source.size(), source) == 0;
    const bool actionId = key.compare(0, action.size(), action) == 0;
    if (key.compare(0, source.size(), source) == 0) key.erase(0, source.size());
    else if (key.compare(0, action.size(), action) == 0) {
        key.erase(0, action.size());
        for (auto& ch : key) if (ch == L'_') ch = L'-';
    }
    for (auto& ch : key) if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
    if (sourceId) {
        bool known = false;
        for (const auto* item : {L"smtc", L"rss", L"genshin", L"hsr", L"zzz", L"discord", L"prayer",L"clock",L"worldclock",L"calendar",L"wuwa",L"battery"}) known = known || key == item;
        if (!known) return nullptr;
    }
    if (actionId) {
        bool known = false;
        for (const auto* item : {L"toggle", L"play", L"pause", L"previous", L"next", L"repeat", L"shuffle", L"volume-down", L"volume-up"}) known = known || key == item;
        if (!known) return nullptr;
    }
    if (key == L"rssfeed") key = L"rss";
    if (key == L"discordrpc") key = L"discord";
    for (const auto& topic : Topics) if (key == topic.key) return &topic;
    return nullptr;
}
struct Asset { std::wstring relative; unsigned width, height; bool transparent; };
enum class FontStyle { Vector, Mdl2, Fluent };
inline bool IsMonochromeTopic(const Topic& topic) {
    return topic.glyph != Glyph::Music && topic.glyph != Glyph::Feed && topic.glyph != Glyph::Genshin &&
        topic.glyph != Glyph::StarRail && topic.glyph != Glyph::Zenless && topic.glyph != Glyph::Chat && topic.glyph != Glyph::Prayer;
}
struct NativeGlyph { wchar_t primary = 0, secondary = 0; bool slash = false; };
// Microsoft Segoe Fluent/MDL2 tables use the same values for these glyphs.
// PUA characters are rasterized with an explicit, verified installed font;
// they are never passed to Windows tile text/font fallback.
inline NativeGlyph NativeGlyphFor(Glyph glyph) {
    switch (glyph) {
    case Glyph::Music: return {0xe90b, 0, false};
    case Glyph::Chat: return {0xe8f2, 0, false};
    case Glyph::Toggle: return {0xe768, 0xe769, false};
    case Glyph::Play: return {0xe768, 0, false};
    case Glyph::Pause: return {0xe769, 0, false};
    case Glyph::Previous: return {0xe892, 0, false};
    case Glyph::Next: return {0xe893, 0, false};
    case Glyph::Repeat: return {0xe8ee, 0, false};
    case Glyph::RepeatOff: return {0xe8ee, 0, true};
    case Glyph::RepeatTrack: return {0xe8ed, 0, false};
    case Glyph::Shuffle: return {0xe8b1, 0, false};
    case Glyph::ShuffleOff: return {0xe8b1, 0, true};
    case Glyph::VolumeDown: return {0xe993, 0, false};
    case Glyph::VolumeUp: return {0xe995, 0, false};
    case Glyph::Stop: return {0xe71a, 0, false};
    case Glyph::Unknown: return {0xe897, 0, false};
    case Glyph::Resin: return {0xeb42, 0, false};
    case Glyph::Stamina: return {0xe83f, 0, false};
    case Glyph::Charge: return {0xe83e, 0, false};
    case Glyph::Photo: return {0xe91b, 0, false};
    case Glyph::Desktop: return {0xe977,0,false}; // Microsoft PC1 glyph, distinct from Photo.
    case Glyph::Prayer: return {0xe708, 0, false}; // Crescent moon; verified font availability gates native rendering.
    case Glyph::Clock: return {0xe917,0,false};
    // The installed Clock app uses a composite clock/globe image, not the
    // generic Clock character. Draw our own geometry until a matching native
    // font glyph is verified; never substitute an unverified PUA codepoint.
    case Glyph::WorldClock: return {};
    case Glyph::Calendar: return {0xe787,0,false};
    case Glyph::Game: return {0xe7fc,0,false};
    default: return {};
    }
}
template<class Available> FontStyle SelectFont(Glyph glyph,FontPreference preference,unsigned windowsBuild,Available available) {
    const auto native = NativeGlyphFor(glyph);
    if (preference==FontPreference::Vector||!native.primary) return FontStyle::Vector;
    const auto preferred=PreferFluent(preference,windowsBuild)?FontStyle::Fluent:FontStyle::Mdl2;
    for (const auto style : {preferred,preferred==FontStyle::Fluent?FontStyle::Mdl2:FontStyle::Fluent})
        if (available(style, native.primary) && (!native.secondary || available(style, native.secondary))) return style;
    return FontStyle::Vector;
}
// Preserve the old portable overload's Fluent-first contract for callers that
// have already selected their platform policy. The native adapter passes the
// real OS build and explicit preference through the overload above.
template<class Available> FontStyle SelectFont(Glyph glyph,Available available) {
    return SelectFont(glyph,FontPreference::Auto,22000,available);
}
struct AssetBackground { bool colored = false; std::uint32_t rgb = 0; };
inline std::wstring BackgroundAssetKey(AssetBackground background) {
    if (!background.colored) return L"transparent";
    std::wstring hex(6, L'0');
    for (int i = 5; i >= 0; --i) { hex[static_cast<std::size_t>(i)] = L"0123456789ABCDEF"[background.rgb & 15u]; background.rgb >>= 4; }
    return L"color" + hex;
}
inline bool DarkArtworkInk(AssetBackground background) {
    return background.colored && (((background.rgb >> 16) & 255u) * 299u +
        ((background.rgb >> 8) & 255u) * 587u + (background.rgb & 255u) * 114u) >= 160000u;
}
inline std::vector<Asset> AssetPlan(const Topic& topic, FontStyle style = FontStyle::Vector, AssetBackground background = {}) {
    const auto base = std::wstring(L"Assets/TopicTiles/v3/") +
        (style == FontStyle::Fluent ? L"fluent/" : style == FontStyle::Mdl2 ? L"mdl2/" : L"vector/") +
        BackgroundAssetKey(background) + L"/" + topic.key + L"/";
    const auto corner=std::wstring(L"Assets/TopicCorners/v1/")+(style==FontStyle::Fluent?L"fluent/":style==FontStyle::Mdl2?L"mdl2/":L"vector/")+BackgroundAssetKey(background)+L"/"+topic.key+L"/";
    return {{base + L"Medium.png", 150, 150, !background.colored}, {base + L"Wide.png", 310, 150, !background.colored},
        {base + L"Large.png", 310, 310, !background.colored}, {base + L"Small.png", 70, 70, !background.colored},
        {corner + L"Logo44.png", 44, 44, true}, {corner + L"Logo30.png", 30, 30, true}};
}
struct Assets { std::wstring medium, wide, large, smallTile, logo44, logo30; };
inline std::wstring PackageUri(const std::wstring& relative) { return L"ms-appx:///" + relative; }
inline bool IsKnownSecondaryArtworkId(const std::wstring& id) {
    const auto* topic = FindTopic(id);
    if (!topic) return false;
    if (id.compare(0, 10, L"AipSource_") == 0)
        return !IsMonochromeTopic(*topic) && id == std::wstring(L"AipSource_") + topic->key;
    if (id.compare(0, 10, L"AipAction_") != 0) return false;
    // Status/resource topics are render-only, never new pin destinations.
    switch (topic->glyph) {
    case Glyph::Toggle: case Glyph::Play: case Glyph::Pause: case Glyph::Previous: case Glyph::Next:
    case Glyph::Repeat: case Glyph::Shuffle: case Glyph::VolumeDown: case Glyph::VolumeUp: break;
    default: return false;
    }
    std::wstring key = topic->key;
    for (auto& ch : key) if (ch == L'-') ch = L'_';
    return id == L"AipAction_" + key && key != L"repeat_list" && key != L"shuffle_on";
}
inline bool SameLogos(const Assets& a, const Assets& b) {
    return a.medium == b.medium && a.wide == b.wide && a.large == b.large &&
        a.smallTile == b.smallTile && a.logo44 == b.logo44 && a.logo30 == b.logo30;
}
inline bool SamePublishedLogos(const Assets& actual, const Assets& expected) {
    // On the observed Windows 11 runtime the deprecated 30px property aliases
    // the 44px property. Require the modern 44px asset, and accept its alias
    // only for that deprecated field; all real tile sizes still match exactly.
    return actual.medium == expected.medium && actual.wide == expected.wide && actual.large == expected.large &&
        actual.smallTile == expected.smallTile && actual.logo44 == expected.logo44 &&
        (actual.logo30 == expected.logo30 || actual.logo30 == expected.logo44);
}
inline bool NeedsTopicLogoUpdate(const std::wstring& id, const Assets& current, const Assets& expected) {
    return IsKnownSecondaryArtworkId(id) && !SamePublishedLogos(current, expected);
}
inline Assets LogoUris(const Assets& relative) {
    return {PackageUri(relative.medium), PackageUri(relative.wide), PackageUri(relative.large),
        PackageUri(relative.smallTile), PackageUri(relative.logo44), PackageUri(relative.logo30)};
}
template<class Visuals, class MakeUri> void ApplyTopicLogos(Visuals& visuals, const Assets& logos, MakeUri uri) {
    visuals.Square150x150Logo(uri(logos.medium));
    visuals.Wide310x150Logo(uri(logos.wide));
    visuals.Square310x310Logo(uri(logos.large));
    visuals.Square70x70Logo(uri(logos.smallTile));
    visuals.Square30x30Logo(uri(logos.logo30));
    // Keep the supported property last: older aliases must not replace it
    // with a smaller branding asset on modern Windows.
    visuals.Square44x44Logo(uri(logos.logo44));
}
} }
