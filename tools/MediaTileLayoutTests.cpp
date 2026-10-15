// Pure presentation and settings policy; no Windows, media, files or tile APIs.
#include "../dependencies/media_tile_layout.h"
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
}
int main() {
    try { TestStatus(); TestLayouts(); TestSettings(); std::cout << checks << " media presentation checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "After " << checks << " checks: " << error.what() << '\n'; return 1; }
}
