#include "../dependencies/content_presets.h"
#include <iostream>
#include <map>
static int checks = 0, failures = 0;
static void Check(bool condition, const char* label) { ++checks; if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
int main()
{
    using namespace aip::content;
    Entry wallpaper; wallpaper.name = L"User title"; wallpaper.custom = {L"User primary", L"User secondary", L"9"};
    auto media = WallpaperAndSmtcCycle(&wallpaper);
    Check(media.entries.size() == 2 && media.cycle && media.textMode == TextMode::Auto && media.cycleDriver == CycleDriver::Auto,
        "wallpaper/media preset is the requested two-item automatic-text cycle");
    Check(media.entries[0].name == wallpaper.name && media.entries[0].custom.primary == wallpaper.custom.primary &&
        media.entries[0].custom.secondary == wallpaper.custom.secondary && media.entries[0].custom.badge == wallpaper.custom.badge &&
        media.entries[0].background == L"Wallpaper" && media.entries[0].sources == std::vector<std::wstring>{L"CustomText"},
        "the wallpaper entry copies user text and naming instead of hardcoding personal contents");
    Check(media.entries[1].background == L"Transparent" && media.entries[1].sources == std::vector<std::wstring>{L"SMTC"},
        "the second item is media text without an app background");
    for (int count = 1; count <= MaxEntries; ++count) {
        for (const auto& preset : {WallpaperPreset(), WallpaperRssPreset(5), media}) {
            PresetPlan plan; std::wstring error;
            const bool success = PlanAppendPreset(preset, count, count, plan, error);
            Check(success == (count + static_cast<int>(preset.entries.size()) <= MaxEntries), "slot bounds are checked before any plan can overwrite existing contents");
            if (!success) { Check(plan.settings.empty() && !error.empty(), "a rejected preset has no partial settings"); continue; }
            Check(plan.firstEntry == count + 1 && plan.lastEntry == count + static_cast<int>(preset.entries.size()), "a preset only occupies newly appended slots");
            std::map<std::pair<std::wstring, std::wstring>, std::wstring> values;
            for (const auto& setting : plan.settings) values[{setting.section, setting.key}] = setting.value;
            for (int index = 1; index <= count; ++index) {
                const auto section = L"Content." + std::to_wstring(index);
                Check(values[{section, L"Enabled"}] == L"0" && values.count({section, L"Text"}) == 0 &&
                    values.count({section, L"Name"}) == 0 && values.count({section, L"ImagePath"}) == 0,
                    "previous slots become inactive without overwriting any saved content fields");
            }
            Configuration cfg;
            cfg.enabled = true; cfg.cycle = preset.cycle; cfg.textMode = preset.textMode; cfg.cycleDriver = preset.cycleDriver;
            for (int i = 0; i < count; ++i) { Entry dormant; dormant.enabled = false; cfg.entries.push_back(dormant); }
            for (const auto& entry : preset.entries) cfg.entries.push_back(entry);
            Check(ActivePresetMatches(cfg, preset), "an already active identical preset is recognized without allocating more slots");
            cfg.entries.back().name += L" edited";
            Check(!ActivePresetMatches(cfg, preset), "edited preset contents are preserved rather than treated as reusable slots");
        }
    }
    PresetPlan plan; std::wstring error;
    Check(PlanAppendPreset(WallpaperPreset(), 1, 30, plan, error) && plan.firstEntry == 31,
        "saved sections outside active Count also reserve their slots");
    Check(!PlanAppendPreset(WallpaperRssPreset(2), 1, 31, plan, error) && plan.settings.empty(),
        "inactive saved slots are not overwritten to make room");
    for (int invalid : {-1, 0, 21}) Check(!PlanAppendPreset(WallpaperRssPreset(invalid), 1, 1, plan, error), "invalid RSS item counts cannot build a preset");
    auto invalidMode = WallpaperPreset(); invalidMode.textMode = static_cast<TextMode>(-1);
    Check(!PlanAppendPreset(invalidMode, 1, 1, plan, error) && plan.settings.empty(), "an invalid descriptor does not silently select another text mode");
    auto missingImage = WallpaperPreset(); missingImage.entries[0].background = L"Image";
    Check(!PlanAppendPreset(missingImage, 1, 1, plan, error) && plan.settings.empty(), "a preset cannot publish an incomplete image background");
    auto invalidDriver = WallpaperPreset(); invalidDriver.cycleDriver = static_cast<CycleDriver>(-1);
    Check(!PlanAppendPreset(invalidDriver, 1, 1, plan, error) && plan.settings.empty(), "an invalid cycle driver cannot silently select another driver");
    Configuration changedDriver; changedDriver.enabled = true; changedDriver.cycle = media.cycle;
    changedDriver.textMode = media.textMode; changedDriver.entries = media.entries; changedDriver.cycleDriver = CycleDriver::DesktopStub;
    Check(!ActivePresetMatches(changedDriver, media), "semantic preset matching includes its selected cycle driver");
    std::cout << checks << " content preset checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}
