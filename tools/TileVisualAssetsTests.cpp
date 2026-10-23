#include "../dependencies/tile_visual_assets.h"
#include "../dependencies/tile_instance_metadata.h"
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>
static int checks = 0;
static void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
struct FakeVisuals {
    aip::tile_visual::Assets logos;
    std::wstring name = L"My name", arguments = L"--ini custom.ini --tile-setup smtc";
    int nameFlags = 7, color = 123, setters = 0;
    void Square150x150Logo(const std::wstring& value) { logos.medium = value; ++setters; }
    void Wide310x150Logo(const std::wstring& value) { logos.wide = value; ++setters; }
    void Square310x310Logo(const std::wstring& value) { logos.large = value; ++setters; }
    void Square70x70Logo(const std::wstring& value) { logos.smallTile = value; ++setters; }
    void Square44x44Logo(const std::wstring& value) { logos.logo44 = value; ++setters; }
    void Square30x30Logo(const std::wstring& value) { logos.logo30 = value; ++setters; }
};
static void TestFontFallback() {
    using namespace aip::tile_visual;
    for (const auto& topic : Topics) {
        Check(SelectFont(topic.glyph, [](FontStyle, wchar_t) { return false; }) == FontStyle::Vector,
            "missing native glyphs use vector fallback");
        const auto native = NativeGlyphFor(topic.glyph);
        if (!native.primary) continue;
        Check(SelectFont(topic.glyph, [](FontStyle style, wchar_t) { return style == FontStyle::Mdl2; }) == FontStyle::Mdl2,
            "Windows 10 MDL2 font remains supported without Fluent");
        Check(SelectFont(topic.glyph, [](FontStyle, wchar_t) { return true; }) == FontStyle::Fluent,
            "installed Fluent glyphs take priority");
        if (native.secondary) Check(SelectFont(topic.glyph, [&](FontStyle, wchar_t glyph) { return glyph != native.secondary; }) == FontStyle::Vector,
            "composite toggle requires both glyphs and cannot render a missing pause box");
    }
    Check(NativeGlyphFor(Glyph::Play).primary == 0xe768 && NativeGlyphFor(Glyph::Pause).primary == 0xe769 &&
        NativeGlyphFor(Glyph::Previous).primary == 0xe892 && NativeGlyphFor(Glyph::Next).primary == 0xe893,
        "media navigation mappings match the Microsoft native icon tables");
    Check(NativeGlyphFor(Glyph::Stop).primary == 0xe71a && NativeGlyphFor(Glyph::Unknown).primary == 0xe897 &&
        NativeGlyphFor(Glyph::RepeatOff).slash && NativeGlyphFor(Glyph::ShuffleOff).slash &&
        NativeGlyphFor(Glyph::RepeatTrack).primary == 0xe8ed, "status glyphs distinguish stopped/unknown/disabled/single-track states");
    Check(FindTopic(L"gallery") && NativeGlyphFor(FindTopic(L"gallery")->glyph).primary == 0xe91b,
        "gallery fallback maps to the documented Photo glyph");
    Check(FindTopic(L"desktop")&&NativeGlyphFor(FindTopic(L"desktop")->glyph).primary==0xe977&&
        FindTopic(L"desktop")->glyph!=FindTopic(L"gallery")->glyph,
        "Desktop fallback resolves the documented PC1 glyph instead of the gallery Photo glyph");
    const auto* worldClock=FindTopic(L"worldclock");
    Check(worldClock&&worldClock->glyph==Glyph::WorldClock&&FindTopic(L"clock")->glyph==Glyph::Clock&&
        worldClock->glyph!=FindTopic(L"clock")->glyph&&IsMonochromeTopic(*worldClock),
        "world-clock artwork is a distinct transparent composite and preserves generic Clock artwork");
    Check(NativeGlyphFor(Glyph::WorldClock).primary==0&&
        SelectFont(Glyph::WorldClock,[](FontStyle,wchar_t){return true;})==FontStyle::Vector,
        "world-clock composite uses independent vector geometry rather than an unverified native codepoint");
    Check(FindTopic(L"AipSource_worldclock")==worldClock&&FindTopic(L"WORLDCLOCK")==worldClock&&
        !IsKnownSecondaryArtworkId(L"AipSource_worldclock"),
        "world-clock topic resolves for rendering without becoming a new legacy pin destination");
    aip::content::TileInstance clock;clock.type=aip::content::TileInstanceType::Clock;
    Check(aip::content::TileInstanceArtworkTopic(clock)==L"worldclock"&&
        AssetPlan(*worldClock)[3].relative.find(L"/worldclock/Small.png")!=std::wstring::npos&&
        AssetPlan(*worldClock)[3].relative!=AssetPlan(*FindTopic(L"clock"))[3].relative,
        "Clock tile instances select distinct world-clock assets so old generic-clock caches cannot survive");
    aip::content::TileInstance wallpaper,gallery;wallpaper.type=aip::content::TileInstanceType::Wallpaper;gallery.type=aip::content::TileInstanceType::Gallery;
    Check(aip::content::TileInstanceArtworkTopic(wallpaper)==L"desktop"&&aip::content::TileInstanceArtworkTopic(gallery)==L"gallery",
        "primary and independent Wallpaper instances never inherit photo-gallery artwork");
    Check(!FindTopic(L"AipGallery_unknown.tile") && !IsKnownSecondaryArtworkId(L"AipGallery_unknown.tile"),
        "pure artwork does not classify arbitrary gallery IDs as pin destinations without host configuration");
}
static void TestExistingLogoRefresh() {
    using namespace aip::tile_visual;
    struct AliasedVisuals : FakeVisuals {
        void Square44x44Logo(const std::wstring& value) { logos.logo44 = logos.logo30 = value; ++setters; }
        void Square30x30Logo(const std::wstring& value) { logos.logo44 = logos.logo30 = value; ++setters; }
    };
    const Assets sample{L"m", L"w", L"l", L"s", L"44", L"30"};
    AliasedVisuals aliased;
    ApplyTopicLogos(aliased, sample, PackageUri);
    Check(aliased.logos.logo44 == PackageUri(sample.logo44) && aliased.logos.logo30 == aliased.logos.logo44,
        "deprecated 30px alias cannot overwrite the supported 44px asset");
    Check(SamePublishedLogos(aliased.logos, LogoUris(sample)), "readback recognizes the observed 30px alias without repeated migrations");
    auto wrongSize = aliased.logos; wrongSize.medium = L"Desktop";
    Check(!SamePublishedLogos(wrongSize, LogoUris(sample)), "alias handling never hides a stale real tile size");
    for (const auto* id : {L"AipSource_smtc", L"AipSource_rss", L"AipSource_genshin", L"AipSource_hsr", L"AipSource_zzz", L"AipSource_discord", L"AipSource_prayer",
            L"AipAction_toggle", L"AipAction_play", L"AipAction_pause", L"AipAction_previous", L"AipAction_next",
            L"AipAction_repeat", L"AipAction_shuffle", L"AipAction_volume_down", L"AipAction_volume_up"}) {
        Check(IsKnownSecondaryArtworkId(id), "only recognized existing source/action destinations are eligible");
        const auto plan = AssetPlan(*FindTopic(id), FontStyle::Mdl2);
        const Assets relative{plan[0].relative, plan[1].relative, plan[2].relative, plan[3].relative, plan[4].relative, plan[5].relative};
        const auto expected = LogoUris(relative);
        FakeVisuals visual;
        Check(NeedsTopicLogoUpdate(id, visual.logos, expected), "old or empty desktop-logo metadata needs migration");
        ApplyTopicLogos(visual, relative, PackageUri);
        Check(SameLogos(visual.logos, expected) && visual.setters == 6, "all logo properties including medium and 30/44 are explicitly published");
        Check(visual.name == L"My name" && visual.arguments == L"--ini custom.ini --tile-setup smtc" && visual.nameFlags == 7 && visual.color == 123,
            "logo migration preserves custom labels, activation profile, branding and background");
        Check(!NeedsTopicLogoUpdate(id, visual.logos, expected), "already-current metadata is skipped on later startups");
        for (auto member : {&Assets::medium, &Assets::wide, &Assets::large, &Assets::smallTile, &Assets::logo44, &Assets::logo30}) {
            auto stale = expected; stale.*member = L"ms-appx:///Assets/Desktop.png";
            Check(NeedsTopicLogoUpdate(id, stale, expected), "any single stale size triggers a complete metadata repair");
        }
    }
    for (const auto* id : {L"App", L"smtc", L"AipSource_play", L"AipAction_stop", L"AipAction_unknown", L"AipAction_repeat_list",
            L"AipAction_shuffle_on", L"AipSource_OTHER", L"AipAction_volume-down", L"other.application"})
        Check(!IsKnownSecondaryArtworkId(id) && !NeedsTopicLogoUpdate(id, {}, {L"changed", {}, {}, {}, {}, {}}),
            "primary, foreign, aliases and render-only status IDs cannot be migrated or created");
}
int main() {
    try {
        using namespace aip::tile_visual;
        std::set<std::wstring> paths, keys;
        for (const auto& topic : Topics) {
            Check(keys.insert(topic.key).second && FindTopic(topic.key) == &topic, "unique canonical topic resolves");
            const auto plan = AssetPlan(topic);
            Check(plan.size() == 6 && plan[0].width == 150 && plan[0].height == 150 &&
                plan[1].width == 310 && plan[1].height == 150 && plan[2].width == 310 && plan[2].height == 310,
                "medium/wide/large match Windows tile logo dimensions");
            Check(plan[3].width == 70 && plan[3].height == 70 && plan[3].transparent &&
                plan[4].width == 44 && plan[4].height == 44 && plan[4].transparent &&
                plan[5].width == 30 && plan[5].height == 30 && plan[5].transparent, "small and branding icons preserve transparent margins");
            for (const auto& asset : plan) Check(paths.insert(asset.relative).second &&
                asset.relative.find(L"..") == std::wstring::npos && PackageUri(asset.relative).find(L"ms-appx:///Assets/TopicTiles/v3/vector/transparent/") == 0 && asset.transparent,
                "topic logos are isolated and use stable package-relative URIs");
        }
        Check(FindTopic(L"AipSource_smtc")->glyph == Glyph::Music && FindTopic(L"SMTC")->glyph == Glyph::Music, "source runtime ID and provider key resolve music glyph");
        Check(FindTopic(L"RssFeed")->glyph == Glyph::Feed && FindTopic(L"DiscordRPC")->glyph == Glyph::Chat, "provider aliases match topic glyphs");
        for (const auto* action : {L"toggle", L"play", L"pause", L"previous", L"next", L"repeat", L"shuffle", L"volume-down", L"volume-up"})
            Check(FindTopic(std::wstring(L"AipAction_") + action) == FindTopic(action), "quick-action runtime IDs use shared topic artwork");
        for (const auto* invalid : {L"", L"../smtc", L"smtc/other", L"AipSource_unknown", L"AipAction_unknown"})
            Check(!FindTopic(invalid), "unknown artwork topics cannot influence asset paths");
        Check(FindTopic(L"AipAction_volume_down") == FindTopic(L"volume-down") &&
            FindTopic(L"AipAction_volume_up") == FindTopic(L"volume-up"), "Windows-safe action IDs preserve CLI topic names");
        Check(FindTopic(L"genshin")->rgb != FindTopic(L"hsr")->rgb && FindTopic(L"hsr")->rgb != FindTopic(L"zzz")->rgb,
            "game fallback topics remain distinguishable without their live data");
        for (const auto* key : {L"toggle", L"previous", L"volume-up", L"stop", L"unknown", L"resin", L"stamina", L"charge"}) {
            const auto* topic = FindTopic(key);
            for (const auto& asset : AssetPlan(*topic)) Check(asset.transparent, "native-looking action/status icons have no colored circular backdrop");
            Check(AssetPlan(*topic, FontStyle::Fluent)[0].relative != AssetPlan(*topic, FontStyle::Mdl2)[0].relative &&
                AssetPlan(*topic, FontStyle::Mdl2)[0].relative != AssetPlan(*topic)[0].relative,
                "versioned font-specific paths prevent stale Windows shell artwork after style changes");
        }
        TestFontFallback(); TestExistingLogoRefresh();
        for (const auto& topic : Topics) {
            const auto transparent = AssetPlan(topic);
            const auto colored = AssetPlan(topic, FontStyle::Vector, {true, 0x123456});
            Check(transparent[0].relative != colored[0].relative && colored[0].relative.find(L"color123456") != std::wstring::npos,
                "transparent and selected-color assets use distinct versioned paths");
            Check(!colored[0].transparent && !colored[1].transparent && !colored[2].transparent && !colored[3].transparent &&
                colored[4].transparent && colored[5].transparent, "Color opts into plates without adding branding-icon circles");
        }
        Check(!DarkArtworkInk({}) && !DarkArtworkInk({true, 0x000000}) && DarkArtworkInk({true, 0xffffff}),
            "glyphs stay white on transparent/dark backgrounds and readable on light colors");
        std::cout << "Tile visual asset plans: " << checks << " checks passed; no files or Windows APIs invoked\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
