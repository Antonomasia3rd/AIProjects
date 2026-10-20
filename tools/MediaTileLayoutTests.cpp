// Pure presentation and settings policy; no Windows, media, files or tile APIs.
#include "../dependencies/media_tile_layout.h"
#include "../dependencies/prayer_tile_layout.h"
#include "../dependencies/rss_tile_layout.h"
#include "../dependencies/secondary_tile_anchor.h"
#include <iostream>
#include <map>
#include <stdexcept>
using namespace aip::content;
namespace {
int checks = 0;
void Check(bool condition, const char* label) { ++checks; if (!condition) throw std::runtime_error(label); }
struct Profile {
    std::map<std::pair<std::wstring, std::wstring>, std::wstring> values;
    std::wstring operator()(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) const {
        const auto found = values.find({section, key});
        return found == values.end() ? std::wstring(fallback) : found->second;
    }
};
std::size_t Count(const std::wstring& text, const std::wstring& needle) {
    std::size_t count = 0, position = 0;
    while ((position = text.find(needle, position)) != std::wstring::npos) { ++count; position += needle.size(); }
    return count;
}
void TestStatus() {
    Snapshot snapshot; snapshot.smtcSource = true; snapshot.text.badge = L"Playing - a misleading visible caption";
    const MediaPlaybackState states[] = {MediaPlaybackState::Playing, MediaPlaybackState::Paused, MediaPlaybackState::Stopped, MediaPlaybackState::Unknown};
    const wchar_t* topics[] = {L"play", L"pause", L"stop", L"unknown"};
    for (std::size_t index = 0; index < 4; ++index) {
        snapshot.smtcState = states[index];
        Check(std::wstring(MediaStatusTopic(snapshot.smtcState)) == topics[index], "indicator expresses current playback state independently of formatted captions or click actions");
    }
    snapshot.smtcShowRepeatShuffle = true;
    Check(MediaExtraTopics(snapshot).empty(), "unknown repeat/shuffle metadata is omitted instead of invented as off");
    snapshot.smtcRepeat = MediaRepeatMode::Off; snapshot.smtcShuffle = MediaToggleState::Off;
    Check(MediaExtraTopics(snapshot) == std::vector<std::wstring>({L"repeat-off", L"shuffle-off"}), "known off metadata is distinct from missing metadata");
    snapshot.smtcRepeat = MediaRepeatMode::Track; snapshot.smtcShuffle = MediaToggleState::On;
    Check(MediaExtraTopics(snapshot) == std::vector<std::wstring>({L"repeat-track", L"shuffle-on"}), "current track repeat and shuffle-on have their own indicators");
    Check(MediaStatusCaption(snapshot).find(L"Repeat track") != std::wstring::npos &&
        MediaStatusCaption(snapshot).find(L"Shuffle on") != std::wstring::npos, "visible metadata labels use the same explicit current values");
    snapshot.smtcShowRepeatShuffle = false;
    Check(MediaExtraTopics(snapshot).empty() && MediaStatusCaption(snapshot).find(L"Repeat") == std::wstring::npos,
        "optional metadata is hidden without discarding the provider values");
}
void TestLayouts() {
    const int widths[] = {150, 310, 310}, heights[] = {150, 150, 310};
    const wchar_t* names[] = {L"TileMedium", L"TileWide", L"TileLarge"};
    for (std::size_t size = 0; size < 3; ++size) {
        const auto layout = ComputeMediaTileGeometry(widths[size], heights[size], true);
        Check(layout.textY == 10 && layout.iconY == 10 && layout.textX + layout.textWidth < layout.iconX &&
            layout.iconX + layout.iconSide <= widths[size] - layout.margin && layout.textHeight > 0,
            "each bitmap size places text and indicator at the top in disjoint bounded columns");
        const auto plain = ComputeMediaTileGeometry(widths[size], heights[size], false);
        Check(plain.textWidth > layout.textWidth && plain.iconSide == 0, "no-indicator mode gives the caption the full available width");
        const auto extras = ComputeMediaTileGeometry(widths[size], heights[size], true, 2);
        Check(extras.iconY + extras.iconSide * 3 + 8 <= heights[size] - 20,
            "playback plus repeat/shuffle indicators remain inside every supported tile height");
        std::wstring xml;
        Check(AppendMediaTileBinding(xml, names[size], {L"Song <&>", L"Artist", L"Paused - Player"}, L"ms-appx:///pause.png", {}, {}, L"Paused") &&
            xml.find(L"hint-textStacking=\"center\"") == std::wstring::npos &&
            xml.find(L"Song &lt;&amp;&gt;") != std::wstring::npos && xml.find(L"alt=\"Paused\"") != std::wstring::npos,
            "native media bindings use top stacking, escaped text and current-status descriptions");
        xml.clear();
        Check(AppendMediaTileBinding(xml, names[size], {L"Song", L"Artist", L"Playing"}, L"") &&
            xml.find(L"hint-weight") == std::wstring::npos && xml.find(L"<image") == std::wstring::npos,
            "no-indicator native bindings do not reserve an empty icon column");
        xml.clear();
        std::vector<Text> rows;
        for (int row = 0; row < 6; ++row) rows.push_back({L"Resource " + std::to_wstring(row), L"Value <&>", L""});
        Check(AppendDetailRowsBinding(xml, names[size], heights[size], rows, L"", L"ms-appx:///resin.png") &&
            Count(xml, L"<group>") == DetailTileRows(heights[size]) && Count(xml, L"<image") == 1 &&
            xml.find(L"1. Resource") == std::wstring::npos && xml.find(L"Value &lt;&amp;&gt;") != std::wstring::npos,
            "detail rows are bounded for each size, unnumbered, escaped and have one resource icon");
        xml.clear();
        std::vector<MediaIndicatorImage> rowIcons;
        for (std::size_t row = 0; row < rows.size(); ++row) rowIcons.push_back({L"ms-appx:///activity" + std::to_wstring(row) + L".png", L"Activity <&>"});
        Check(AppendDetailRowsBinding(xml, names[size], heights[size], rows, L"", L"", rowIcons) &&
            Count(xml, L"<image") == DetailTileRows(heights[size]) &&
            xml.find(L"<image") < xml.find(L"<text") && xml.find(L"alt=\"Activity &lt;&amp;&gt;\"") != std::wstring::npos,
            "every visible activity row has its own escaped original icon in a consistent left subgroup");
        for (std::size_t row = 0; row < DetailTileRows(heights[size]); ++row) {
            const auto geometry = ComputeDetailRowGeometry(widths[size], heights[size], DetailTileRows(heights[size]), row, true);
            Check(geometry.iconSide > 0 && geometry.iconX + geometry.iconSide < geometry.textX &&
                geometry.textX + geometry.textWidth <= widths[size] - 10 && geometry.iconY + geometry.iconSide <= geometry.bottom,
                "bitmap row geometry keeps each left icon and text in disjoint bounded columns at every tile size");
        }
        xml.clear();
        Check(AppendDetailRowsBinding(xml, names[size], heights[size], rows, L"", L"", {}) &&
            xml.find(L"hint-weight") == std::wstring::npos && xml.find(L"<image") == std::wstring::npos,
            "absent icons have a clean full-width text fallback with no empty column");
        xml.clear();
        Check(AppendWrappedSourceBinding(xml, names[size], {L"Discord presence", L"Set Details and State in Discord settings.", L""}) &&
            xml.find(L"hint-wrap=\"true\" hint-maxLines=\"5\"") != std::wstring::npos,
            "compact source headlines keep setup instructions in a wrapped lower field");
    }
    std::wstring xml;
    Check(!AppendMediaTileBinding(xml, nullptr, {}, L"") && !AppendDetailRowsBinding(xml, L"TileWide", 150, {}, L""),
        "invalid/empty native bindings fail without partial markup");
}
void TestSettings() {
    Profile profile; Configuration cfg; std::wstring error;
    Check(ReadConfiguration(profile, cfg, error) && cfg.smtc.indicatorMode == MediaIndicatorMode::Status && !cfg.smtc.showRepeatShuffle,
        "default media presentation uses current status without extra metadata");
    for (const auto* mode : {L"Status", L"AppIcon", L"None"}) {
        profile.values[{L"SMTC", L"Indicator"}] = mode;
        Check(ReadConfiguration(profile, cfg, error), "every public indicator preference is accepted by the shared model");
    }
    profile.values[{L"SMTC", L"ShowRepeatShuffle"}] = L"yes";
    Check(ReadConfiguration(profile, cfg, error) && cfg.smtc.indicatorMode == MediaIndicatorMode::None && cfg.smtc.showRepeatShuffle,
        "typed boolean accepts optional metadata independently of the main indicator choice");
    auto value = std::wstring(L" appicon ");
    Check(KnownSection(L"smtc") && NormalizeSetting(L"smtc", L"Indicator", value, error) && value == L"AppIcon",
        "generic CLI/tray normalization reuses case-insensitive canonical media values");
    value = L"show a play action";
    Check(!NormalizeSetting(L"SMTC", L"Indicator", value, error), "action names are not accepted as playback indicator modes");
    profile.values[{L"SMTC", L"Indicator"}] = L"invalid";
    Check(!ReadConfiguration(profile, cfg, error), "invalid persisted indicator preferences reject configuration");
    value = L"OS_ASIA";
    Check(NormalizeSetting(L"Notes.Resin", L"Server", value, error) && value == L"os_asia", "account server values normalize through the game-specific pure whitelist");
    value = L"os_asia";
    Check(!NormalizeSetting(L"Notes.Charge", L"Server", value, error), "a server from a different game is not silently reused");
}
void TestPresentation() {
    TextPresentation p; const std::wstring xml=L"<binding template=\"TileLarge\"><text hint-style=\"caption\">Line one\nLine two &amp;</text><image src=\"icon.png\"/></binding>";
    Check(!p.Active() && ApplyTextPresentationXml(xml,p)==xml,"Default presentation preserves exact native XML");
    for(const auto* style:TextPresentationStyles){auto value=std::wstring(style);std::wstring error;Check(NormalizeTextPresentationSetting(L"TextStyle",value,error),"every documented named text style is selectable");}
    Profile profile;profile.values[{L"TileText",L"TextPreset"}]=L"Custom";profile.values[{L"TileText",L"TextStyle"}]=L"headerNumeral";
    profile.values[{L"TileText",L"TextAlign"}]=L"center";std::wstring error;
    Check(ReadTextPresentation(profile,L"TileText",p,error)&&p.Active()&&p.primaryStyle==L"headerNumeral","typed global presentation accepts named numeral size and alignment");
    auto styled=ApplyTextPresentationXml(xml,p);
    Check(styled.find(L"headerNumeral")!=std::wstring::npos&&styled.find(L"hint-align=\"center\"")!=std::wstring::npos&&
        styled.find(L"Line one\nLine two &amp;")!=std::wstring::npos&&styled.find(L"icon.png")!=std::wstring::npos,"custom styling preserves explicit line breaks, escaped data and source images");
    TextPresentation inherited;Check(ReadTextPresentation(profile,L"Tile.prayer",inherited,error,&p)&&TextPresentationKey(inherited)==TextPresentationKey(p),"per-source presentation inherits global defaults");
    profile.values[{L"Tile.prayer",L"TextStyle"}]=L"body";
    Check(ReadTextPresentation(profile,L"Tile.prayer",inherited,error,&p)&&inherited.primaryStyle==L"body"&&p.primaryStyle==L"headerNumeral","per-source overrides do not mutate global settings");
    profile.values[{L"Tile.prayer",L"TextMaxLines"}]=L"99";const auto before=TextPresentationKey(inherited);
    Check(!ReadTextPresentation(profile,L"Tile.prayer",inherited,error,&p)&&TextPresentationKey(inherited)==before,"invalid presentation reload leaves prior settings intact");
    std::vector<Text> prayers;for(const auto* name:{L"Fajr",L"Dhuhr",L"Asr",L"Maghrib",L"Isha"})prayers.push_back({name,L"12:00",L""});
    std::wstring prayer;AppendPrayerTileBinding(prayer,L"TileLarge",310,{L"Next",L"City",L"Sunrise"},prayers);
    const auto presented=ApplyTextPresentationXml(prayer,p);
    Check(Count(presented,L"<group>")==5&&presented.find(L"Isha")!=std::wstring::npos,"font changes retain all five Prayer rows even when selected size must clip within bounds");
    std::vector<Text> headlines;for(int i=0;i<5;++i)headlines.push_back({L"Wrapped headline\nsecond line "+std::to_wstring(i),L"Description",L""});
    std::wstring rich;AppendRssRichTileBinding(rich,L"TileMedium",150,headlines,L"");
    Check(Count(rich,L"<group>")==1&&rich.find(L"\nsecond line")!=std::wstring::npos&&rich.find(L"Description")!=std::wstring::npos,"RSS rich preset explicitly selects one larger wrapped Medium headline with its description");
}
void TestRssReadableDefaults() {
    std::vector<Text> headlines;
    for (int i = 0; i < 6; ++i) headlines.push_back({L"Headline <&> " + std::to_wstring(i),L"Summary & details",L""});
    const int widths[] = {150,310,310}, heights[] = {150,150,310};
    const wchar_t* bindings[] = {L"TileMedium",L"TileWide",L"TileLarge"};
    const std::size_t expected[] = {1,2,3};
    TextPresentation defaults;
    Check(RssUsesRichLayout(defaults),"RSS Default is readable without an extra preset switch or profile mutation");
    for (std::size_t index = 0; index < 3; ++index) {
        std::wstring xml;
        Check(AppendRssRichTileBinding(xml,bindings[index],heights[index],headlines,L"image<&>.png",defaults) &&
            Count(xml,L"<group>") == expected[index] && RssRichTileRows(widths[index],heights[index]) == expected[index],
            "RSS native and bitmap policy show one Medium, two Wide and three Large rich entries");
        const auto p = RssRichPresentation(defaults,widths[index],heights[index]);
        Check(p.primaryStyle == L"base" && p.secondaryStyle == L"captionSubtle" && p.wrap && !p.gapLines &&
            xml.find(L"hint-style=\"base\"") != std::wstring::npos && xml.find(L"hint-style=\"captionSubtle\"") != std::wstring::npos,
            "readable RSS distinguishes semibold titles from subdued summaries without blank lines inside each entry");
        Check(xml.find(L"Headline &lt;&amp;&gt;") != std::wstring::npos && xml.find(L"Summary &amp; details") != std::wstring::npos &&
            xml.find(L"image&lt;&amp;&gt;.png") != std::wstring::npos && xml.find(L"1. Headline") == std::wstring::npos,
            "RSS rich layout escapes every field and avoids list numbering noise");
        Check(p.primaryLines <= 3 && p.secondaryLines <= 2 && (index != 1 || p.secondaryLines == 1),
            "each tile size reserves bounded space for both the title and its summary");
        auto larger = defaults; larger.preset = L"RssHeadlines"; larger.primaryStyle = L"subtitle";
        const auto sized = RssRichPresentation(larger,widths[index],heights[index]);
        Check(sized.primaryStyle == L"subtitle" && (index != 1 || sized.primaryLines == 1),
            "explicit larger RSS titles are retained with size-aware line limits");
    }
    auto custom = defaults; custom.preset = L"Custom";
    Check(!RssUsesRichLayout(custom),"intentional Custom list composition is not silently upgraded");
    auto spaced = defaults; spaced.preset = L"RssHeadlines"; spaced.gapLines = 2;
    const auto spacedWide = RssRichPresentation(spaced,310,150);
    Check(spacedWide.gapLines == 2 && spacedWide.primaryLines == 1 && spacedWide.secondaryLines == 1,
        "explicit RSS text spacing remains effective while wide row limits reserve summary space");
    Profile profile; profile.values[{L"TileText",L"TextPreset"}] = L"RssHeadlines";
    TextPresentation parsed; std::wstring error;
    Check(ReadTextPresentation(profile,L"TileText",parsed,error) && parsed.primaryStyle == L"base" && parsed.gapLines == 0,
        "named RSS preset shares the same readable title defaults as automatic RSS layout");
    std::wstring invalid;
    Check(!AppendRssRichTileBinding(invalid,nullptr,150,headlines,L"") && invalid.empty(),"invalid RSS binding preserves the output");
}
void TestCenteredPinAnchor() {
    const auto primary = CenteredSecondaryPinAnchor({0,0,1920,1040},0,0,96);
    Check(primary.x == 960 && primary.y == 520 && primary.width == 1 && primary.height == 1,
        "pin selection uses the work-area center instead of the cursor position");
    const auto secondary = CenteredSecondaryPinAnchor({-2560,-1440,0,0},-2560,-1440,144);
    Check(secondary.x > 853.3f && secondary.x < 853.4f && secondary.y == 480,
        "negative-origin monitor coordinates are first made owner-relative then converted to DIPs");
    const auto visible = CenteredSecondaryPinAnchor({1920,0,5760,2080},2100,120,192);
    Check(visible.x == 870 && visible.y == 460,
        "an already positioned visible owner receives its own client-coordinate anchor at 200 percent DPI");
    const auto fallback = CenteredSecondaryPinAnchor({0,40,1920,1080},0,40,0);
    Check(fallback.x == 960 && fallback.y == 520,
        "unknown DPI falls back to 96 without changing work-area taskbar offsets");
}
}
int main() {
    try { TestStatus(); TestLayouts(); TestSettings(); TestPresentation(); TestRssReadableDefaults(); TestCenteredPinAnchor(); std::cout << checks << " media presentation checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "After " << checks << " checks: " << error.what() << '\n'; return 1; }
}
