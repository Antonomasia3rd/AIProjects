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
void TestAppearance() {
    Profile profile; SecondaryTileConfiguration tiles; std::wstring error;
    Check(ReadSecondaryTileConfiguration(profile, 1, tiles, error), "old profiles read secondary appearance defaults");
    for (const auto& tile : tiles.tiles) Check(tile.appearance.background == L"Transparent" && tile.appearance.branding == L"Name" &&
        tile.appearance.backgroundColor == L"Auto", "each source defaults to transparent fallback and name-only branding");
    TileAppearance actionAppearance;
    Check(ReadTileAppearance(profile, L"MediaActions", actionAppearance, error) && actionAppearance.background == L"Transparent" &&
        actionAppearance.branding == L"Name", "shared action appearance reader uses the same missing-field defaults");
    for (const auto& entry : {std::make_pair(L"Background", L"Wallpaper"), std::make_pair(L"Branding", L"BadBranding"),
            std::make_pair(L"BackgroundColor", L"#GGGGGG")}) {
        Profile invalid; invalid.Set(L"MediaActions", entry.first, entry.second);
        Check(!ReadTileAppearance(invalid, L"MediaActions", actionAppearance, error) &&
            error.find(std::wstring(L"MediaActions.") + entry.first) != std::wstring::npos &&
            actionAppearance.background == L"Transparent" && actionAppearance.branding == L"Name" && actionAppearance.backgroundColor == L"Auto",
            "shared action appearance reader rejects invalid raw values without publishing partial state");
    }
    profile.Set(L"Tile.smtc", L"Background", L" color "); profile.Set(L"Tile.smtc", L"Branding", L"none");
    profile.Set(L"Tile.smtc", L"BackgroundColor", L"a1b2c3");
    Check(ReadSecondaryTileConfiguration(profile, 1, tiles, error) && tiles.tiles[0].appearance.background == L"Color" &&
        tiles.tiles[0].appearance.branding == L"None" && tiles.tiles[0].appearance.backgroundColor == L"#A1B2C3" &&
        tiles.tiles[1].appearance.background == L"Transparent", "appearance normalizes independently per source");
    profile.Set(L"Tile.smtc", L"BackgroundColor", L"#12345G");
    Check(!ReadSecondaryTileConfiguration(profile, 1, tiles, error) && tiles.tiles[0].appearance.backgroundColor == L"#A1B2C3",
        "invalid appearance rejects without partially replacing prior configuration");
    for (const auto* branding : TileBrandingChoices) {
        std::wstring value = branding;
        Check(NormalizeSecondaryTileSetting(L"Tile.rss", L"Branding", value, error) && value == branding,
            "INI/CLI source branding supports the same five primary choices");
        Check(NormalizeTileActionSetting(L"MediaActions", L"Branding", value, error), "action branding shares the typed normalizer");
    }
    for (const auto* invalid : {L"#1234", L"#12345678", L"blue", L"#12 456"}) {
        std::wstring value = invalid;
        Check(!NormalizeTileAppearanceSetting(L"BackgroundColor", value, error), "unsupported or alpha colors reject explicitly");
    }
    TileAppearance appearance;
    auto published = PublishTileAppearance(appearance, 0x123456);
    Check(published.argb == 0 && published.nameMedium && published.nameWide && published.nameLarge && !published.darkText,
        "transparent publication explicitly clears inherited color and keeps white/name defaults");
    appearance.background = L"Color"; appearance.backgroundColor = L"#FFFFFF"; appearance.branding = L"Logo";
    published = PublishTileAppearance(appearance, 0x123456);
    Check(published.argb == 0xffffffffu && !published.nameMedium && !published.nameWide && !published.nameLarge && published.darkText,
        "colored publication selects readable text and honors logo-only name suppression");
    struct Visuals {
        std::uint32_t argb = 123; bool medium = false, wide = false, large = false, dark = true;
        void BackgroundColor(std::uint32_t color) { argb = color; }
        void ForegroundText(bool value) { dark = value; }
        void ShowNameOnSquare150x150Logo(bool value) { medium = value; }
        void ShowNameOnWide310x150Logo(bool value) { wide = value; }
        void ShowNameOnSquare310x310Logo(bool value) { large = value; }
    } visuals;
    ApplyPublishedTileAppearance(visuals, published, [](std::uint32_t color) { return color; }, true, false);
    Check(visuals.argb == 0xffffffffu && !visuals.medium && !visuals.wide && !visuals.large && visuals.dark,
        "fake native visuals receive background, foreground and all branding flags");
    auto mismatched = published; mismatched.nameWide = true;
    Check(!SamePublishedTileAppearance(mismatched, published), "one stale name flag triggers appearance repair");
    mismatched = published; mismatched.argb = 0;
    Check(!SamePublishedTileAppearance(mismatched, published), "stale shell background color triggers appearance repair");
    mismatched = published; mismatched.darkText = !published.darkText;
    Check(!SamePublishedTileAppearance(mismatched, published), "stale shell foreground selection triggers appearance repair");
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
void TestAppearanceNotices() {
    Profile profile; std::wstring first, changed, issue;
    Check(ReadSecondaryAppearanceFingerprint(profile, first, issue), "default appearance fingerprint reads all destinations");
    profile.Set(L"Tile.smtc", L"Branding", L" name "); profile.Set(L"Tile.smtc", L"Name", L"Renamed");
    profile.Set(L"Notes.Resin", L"UID", L"123456789"); profile.Set(L"Content.1", L"Text", L"Different text");
    Check(ReadSecondaryAppearanceFingerprint(profile, changed, issue) && changed == first,
        "canonical appearance key ignores names/accounts/text and case-only edits");
    AppearanceRefreshNotice notice; notice.RecordScheduled(first);
    Check(!notice.Observe(first), "startup scheduling suppresses duplicate initial migration");
    profile.Set(L"Tile.smtc", L"Branding", L"None");
    Check(ReadSecondaryAppearanceFingerprint(profile, changed, issue) && changed != first && notice.Observe(changed),
        "source appearance change requests one UI notice");
    profile.Set(L"MediaActions", L"Background", L"Color");
    Check(ReadSecondaryAppearanceFingerprint(profile, changed, issue) && !notice.Observe(changed) && notice.Consume() && !notice.Consume(),
        "action appearance edits coalesce while a source change is pending");
    Check(!notice.Observe(changed), "native failure cannot produce continual same-key enumeration");
    Check(notice.Observe(first), "later semantic change posts a new notice"); notice.PostFailed();
    Check(notice.Observe(first) && notice.Consume(), "failed message posting remains retryable");
    profile.Set(L"MediaActions", L"Branding", L"invalid"); changed = L"preserved";
    Check(!ReadSecondaryAppearanceFingerprint(profile, changed, issue) && changed == L"preserved",
        "invalid partial raw edits never replace a valid appearance fingerprint");
}
void TestPinnedSelectionAndBatches() {
    Profile profile; SecondaryTileConfiguration tiles; std::wstring error;
    Check(ReadSecondaryTileConfiguration(profile, MaxEntries, tiles, error), "pin selection reads saved mappings without activating them");
    Configuration content; content.enabled = false; content.entries.resize(1); content.entries[0].custom.primary = L"Kept";
    tiles.enabled = false; for (auto& tile : tiles.tiles) tile.enabled = false;
    ApplyPinnedSecondaryTiles(content, tiles, {L"AipSource_smtc", L"AipSource_hsr"});
    Check(content.enabled && tiles.enabled && tiles.tiles[0].enabled && tiles.tiles[3].enabled && !tiles.tiles[1].enabled,
        "OS pin membership overrides retired source and master enable preferences");
    Check(content.entries[0].custom.primary == L"Kept" && content.entries[1].sources == std::vector<std::wstring>{L"SMTC"} &&
        content.entries[4].sources == std::vector<std::wstring>{L"Notes"} && content.entries[4].notesResource == 1,
        "missing mapped rows receive catalog defaults while saved primary content survives");
    SecondaryPinInventory inventory;
    Check(!inventory.Known() && inventory.RequestDue(0) && !inventory.RequestDue(1), "one asynchronous inventory request is coalesced");
    inventory.Complete(true, {L"AipSource_smtc", L"AipGallery_g.old"}, 100);
    Check(inventory.Known() && inventory.Contains(L"AipGallery_g.old") && !inventory.RequestDue(30099) && inventory.RequestDue(30100),
        "confirmed pin inventory includes unknown historical gallery IDs and refreshes at thirty seconds");
    inventory.Complete(false, {}, 30100);
    Check(inventory.Contains(L"AipSource_smtc") && !inventory.RequestDue(30101), "enumeration errors retain confirmed pins and back off");
    inventory.Mutation(L"AipSource_smtc", false); inventory.Mutation(L"AipAction_play", true);
    Check(!inventory.Contains(L"AipSource_smtc") && inventory.Contains(L"AipAction_play"), "own pin acknowledgements immediately amend cached membership");
    SecondaryPinBatch batch; std::vector<std::wstring> requests;
    const auto begin = [&](const auto& request) { requests.push_back(request.first); return request.first != L"bad"; };
    batch.Enqueue({{L"one", L"A"}, {L"two", L"B"}, {L"one", L"duplicate"}, {L"bad", L"C"}, {L"three", L"D"}});
    batch.Advance(begin); batch.Advance(begin);
    Check(requests == std::vector<std::wstring>{L"one"}, "pin batches never overlap confirmations");
    batch.Complete(L"one"); batch.Advance(begin);
    Check(requests.back() == L"two", "declining a pin advances to the next requested tile");
    batch.Complete(L"two"); batch.Advance(begin);
    Check(requests == std::vector<std::wstring>({L"one", L"two", L"bad", L"three"}), "preparation failures skip only their own item");
    batch.Complete(L"three"); batch.Advance(begin); Check(!batch.Busy(), "completed pin batch releases all state");
}
}
int main() {
    try { TestMapping(); TestAppearance(); TestProviderSelection(); TestAppearanceNotices(); TestPinnedSelectionAndBatches(); std::cout << checks << " secondary tile checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "After " << checks << " checks: " << error.what() << '\n'; return 1; }
}
