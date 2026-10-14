// Platform-neutral mapping tests. No Windows headers, pinning, tile delivery,
// network/accounts, media playback, user files or hardware APIs are involved.
#include "../dependencies/secondary_tiles.h"
#include <iostream>
#include <map>
#include <stdexcept>

using namespace aip::content;
namespace {
int checks = 0;
void Check(bool value, const char* label) { ++checks; if (!value) throw std::runtime_error(label); }
struct Profile {
    std::map<std::pair<std::wstring, std::wstring>, std::wstring> values;
    void Set(std::wstring section, std::wstring key, std::wstring value) {
        values[{Lower(section), Lower(key)}] = std::move(value);
    }
    std::wstring operator()(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) const {
        const auto found = values.find({Lower(section), Lower(key)});
        return found == values.end() ? std::wstring(fallback) : found->second;
    }
};
void TestMapping() {
    Profile profile;
    SecondaryTileConfiguration tiles;
    std::wstring error;
    Check(ReadSecondaryTileConfiguration(profile, 1, tiles, error) && !tiles.enabled && tiles.tiles.size() == 6,
        "existing one-content profiles keep secondary tiles disabled without range errors");
    std::set<std::wstring> ids;
    for (std::size_t index = 0; index < SecondaryTileCount; ++index) {
        const auto& descriptor = SecondaryTiles[index];
        const auto& tile = tiles.tiles[index];
        Check(ids.insert(descriptor.id).second && FindSecondaryTileDescriptor(descriptor.id) == &descriptor &&
            tile.id == descriptor.id && tile.name == descriptor.name && tile.content == static_cast<int>(index + 2) && tile.enabled,
            "six catalog mappings have unique stable IDs and numbered content defaults");
        Check(FindSourceDescriptor(descriptor.source) && BackgroundIdFor(descriptor.background) == BackgroundId::Transparent,
            "mapping defaults reuse the source/background catalog");
    }
    Check(FindSecondaryTileDescriptor(L" GENSHIN ") == &SecondaryTiles[2] && !FindSecondaryTileDescriptor(L"unknown"),
        "catalog lookup normalizes known IDs and rejects invented targets");
    profile.Set(L"Tiles", L"Enabled", L"yes");
    Check(ReadSecondaryTileConfiguration(profile, 7, tiles, error) && tiles.enabled && error.empty(),
        "enabling six source tiles reads canonical typed booleans");
    const auto unchanged = tiles.tiles;
    profile.Set(L"Tile.smtc", L"Content", L"8");
    Check(!ReadSecondaryTileConfiguration(profile, 7, tiles, error) && error.find(L"Tile.smtc.Content") != std::wstring::npos &&
        tiles.tiles[0].content == unchanged[0].content,
        "an active mapping outside numbered content rejects without partial publication");
    profile.Set(L"Tile.smtc", L"Enabled", L"off");
    Check(ReadSecondaryTileConfiguration(profile, 7, tiles, error) && !tiles.tiles[0].enabled && tiles.tiles[0].content == 8,
        "an inactive mapping may retain a future existing-range content preference");
    profile.Set(L"Tile.smtc", L"Content", L"0");
    Check(!ReadSecondaryTileConfiguration(profile, 7, tiles, error), "zero content indices always reject");
    profile.Set(L"Tile.smtc", L"Content", L"33");
    Check(!ReadSecondaryTileConfiguration(profile, 7, tiles, error), "out-of-model content indices always reject");
    profile.Set(L"Tile.smtc", L"Content", L"2");
    profile.Set(L"Tile.rss", L"Name", L"  ");
    Check(!ReadSecondaryTileConfiguration(profile, 7, tiles, error), "empty secondary display names reject");
    profile.Set(L"Tile.rss", L"Name", std::wstring(L"Bad\0name", 8));
    Check(!ReadSecondaryTileConfiguration(profile, 7, tiles, error), "embedded NUL display names reject");
    profile.Set(L"Tile.rss", L"Name", L"News");
    profile.Set(L"Tile.zzz", L"Enabled", L"maybe");
    Check(!ReadSecondaryTileConfiguration(profile, 7, tiles, error), "invalid tile booleans reject");
    profile.Set(L"Tile.zzz", L"Enabled", L"1");
    Check(ReadSecondaryTileConfiguration(profile, 7, tiles, error) && tiles.tiles[1].name == L"News",
        "custom mapped names survive successful configuration reads");
    tiles.tiles[1].id = tiles.tiles[0].id;
    Check(!ValidateSecondaryTileConfiguration(tiles, 7, error), "duplicate destination IDs reject");
    Check(ReadSecondaryTileConfiguration(profile, 7, tiles, error), "restore the valid catalog mappings");
    tiles.tiles[1].id = L"invented";
    Check(!ValidateSecondaryTileConfiguration(tiles, 7, error), "unknown destination IDs reject");
    std::wstring value = L"1";
    Check(!NormalizeSecondaryTileSetting(L"Tile.unknown", L"Enabled", value, error) &&
        !NormalizeSecondaryTileSetting(L"Tiles", L"Content", value, error), "unknown sections and keys reject");
    value = L" enabled ";
    Check(NormalizeSecondaryTileSetting(L"tile.SMTC", L"enabled", value, error) && value == L"1",
        "generic settings normalization shares mapping boolean semantics");
}
void TestProviderSelection() {
    Profile profile;
    profile.Set(L"Tiles", L"Enabled", L"1");
    SecondaryTileConfiguration tiles; std::wstring error;
    Check(ReadSecondaryTileConfiguration(profile, 8, tiles, error), "provider fixture reads six source mappings");
    Configuration content; content.enabled = true; content.entries.resize(8);
    Check(SecondaryTileContentIndices(tiles, content) == std::set<std::size_t>({0,1,2,3,4,5,6}),
        "source tiles collect primary and mapped contents while excluding unmapped entries");
    content.entries[2].enabled = false; tiles.tiles[3].enabled = false;
    Check(SecondaryTileContentIndices(tiles, content) == std::set<std::size_t>({0,1,3,5,6}),
        "disabled mappings and disabled contents do not request providers");
    tiles.tiles[4].content = 2;
    Check(SecondaryTileContentIndices(tiles, content) == std::set<std::size_t>({0,1,3,6}),
        "multiple destinations sharing one content read its providers once");
    content.entries[0].enabled = false;
    Check(SecondaryTileContentIndices(tiles, content) == std::set<std::size_t>({1,3,6}),
        "disabled primary content does not suppress independent source tiles");
    tiles.enabled = false;
    Check(SecondaryTileContentIndices(tiles, content).empty(), "disabled secondary layout with disabled primary needs no providers");
    content.entries[0].enabled = true;
    Check(SecondaryTileContentIndices(tiles, content) == std::set<std::size_t>({0}), "disabled secondary layout retains enabled primary only");
    content.enabled = false;
    Check(SecondaryTileContentIndices(tiles, content).empty(), "disabled content engine requests no source providers");
}
}
int main() {
    try { TestMapping(); TestProviderSelection(); std::cout << checks << " secondary tile checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "After " << checks << " checks: " << error.what() << '\n'; return 1; }
}
