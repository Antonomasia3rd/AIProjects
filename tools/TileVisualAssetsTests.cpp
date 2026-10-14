#include "../dependencies/tile_visual_assets.h"
#include <iostream>
#include <set>
#include <stdexcept>
static int checks = 0;
static void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
int main() {
    try {
        using namespace aip::tile_visual;
        std::set<std::wstring> paths, keys;
        for (const auto& topic : Topics) {
            Check(keys.insert(topic.key).second && FindTopic(topic.key) == &topic, "unique canonical topic resolves");
            const auto plan = AssetPlan(topic);
            Check(plan.size() == 5 && plan[0].width == 150 && plan[0].height == 150 &&
                plan[1].width == 310 && plan[1].height == 150 && plan[2].width == 310 && plan[2].height == 310,
                "medium/wide/large match Windows tile logo dimensions");
            Check(plan[3].width == 70 && plan[3].height == 70 && plan[3].transparent &&
                plan[4].width == 44 && plan[4].height == 44 && plan[4].transparent, "small and branding icons preserve transparent margins");
            for (const auto& asset : plan) Check(paths.insert(asset.relative).second &&
                asset.relative.find(L"..") == std::wstring::npos && PackageUri(asset.relative).find(L"ms-appx:///Assets/TopicTiles/v1/") == 0,
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
        std::cout << "Tile visual asset plans: " << checks << " checks passed; no files or Windows APIs invoked\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
