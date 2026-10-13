#pragma once
// Reusable content-layout plans. Pure data only: no files, providers or OS APIs.
#include "content_engine.h"

namespace aip { namespace content {
struct PresetDefinition {
    std::wstring value, label;
    std::vector<Entry> entries;
    bool cycle = false;
    TextMode textMode = TextMode::Auto;
    CycleDriver cycleDriver = CycleDriver::Auto;
};
struct PresetSetting { std::wstring section, key, value; };
struct PresetPlan {
    std::vector<PresetSetting> settings;
    int firstEntry = 0, lastEntry = 0, previousSlots = 0;
};
inline PresetDefinition WallpaperPreset()
{
    PresetDefinition preset; preset.value = L"Wallpaper"; preset.label = L"Wallpaper only";
    Entry entry; entry.name = L"Wallpaper"; preset.entries.push_back(std::move(entry));
    return preset;
}
inline PresetDefinition WallpaperRssPreset(int items)
{
    PresetDefinition preset; preset.value = L"WallpaperRSS"; preset.label = L"Wallpaper with RSS headlines";
    if (items < 1 || items > 20) return preset;
    for (int i = 1; i <= items; ++i) {
        Entry entry; entry.name = L"RSS headline " + std::to_wstring(i); entry.sources = {L"RssFeed"}; entry.rssItem = i;
        preset.entries.push_back(std::move(entry));
    }
    preset.cycle = items > 1;
    return preset;
}
inline PresetDefinition WallpaperAndSmtcCycle(const Entry* currentWallpaper = nullptr)
{
    PresetDefinition preset; preset.value = L"WallpaperSMTC"; preset.label = L"Wallpaper and now playing (two-item cycle)";
    preset.cycle = true;
    Entry wallpaper;
    if (currentWallpaper) wallpaper = *currentWallpaper;
    else wallpaper.name = L"Wallpaper";
    wallpaper.enabled = true; wallpaper.background = L"Wallpaper"; wallpaper.imagePath.clear();
    wallpaper.sources = {L"CustomText"};
    Entry media; media.name = L"Now playing"; media.background = L"Transparent"; media.sources = {L"SMTC"};
    preset.entries.push_back(std::move(wallpaper)); preset.entries.push_back(std::move(media));
    return preset;
}
inline bool ActivePresetMatches(const Configuration& configuration, const PresetDefinition& preset)
{
    if (!configuration.enabled || configuration.cycle != preset.cycle || configuration.textMode != preset.textMode ||
        configuration.cycleDriver != preset.cycleDriver) return false;
    std::vector<const Entry*> active;
    for (const auto& entry : configuration.entries) if (entry.enabled) active.push_back(&entry);
    if (active.size() != preset.entries.size()) return false;
    for (std::size_t index = 0; index < active.size(); ++index) {
        const auto& entry = *active[index]; const auto& expected = preset.entries[index];
        if (entry.name != expected.name || Lower(entry.background) != Lower(expected.background) || entry.imagePath != expected.imagePath ||
            entry.sources != expected.sources || entry.custom.primary != expected.custom.primary ||
            entry.custom.secondary != expected.custom.secondary || entry.custom.badge != expected.custom.badge ||
            entry.rssItem != expected.rssItem || entry.notesResource != expected.notesResource || entry.notesDetail != expected.notesDetail)
            return false;
    }
    return true;
}
inline bool PlanAppendPreset(const PresetDefinition& preset, int configuredCount, int highestSavedEntry,
    PresetPlan& output, std::wstring& error)
{
    output = {}; error.clear();
    const auto count = preset.entries.size();
    if (configuredCount < 1 || configuredCount > MaxEntries || highestSavedEntry < 0 || highestSavedEntry > MaxEntries ||
        !count || count > static_cast<std::size_t>(MaxEntries)) {
        error = L"The preset or saved content count is outside the supported range."; return false;
    }
    if (preset.textMode != TextMode::Auto && preset.textMode != TextMode::Overlay && preset.textMode != TextMode::Off) {
        error = L"The preset contains an unsupported text delivery mode."; return false;
    }
    if (preset.cycleDriver != CycleDriver::DesktopStub && preset.cycleDriver != CycleDriver::Auto && preset.cycleDriver != CycleDriver::Windows) {
        error = L"The preset contains an unsupported cycle driver."; return false;
    }
    const int occupied = std::max(configuredCount, highestSavedEntry);
    if (count > static_cast<std::size_t>(MaxEntries - occupied)) {
        error = L"This preset needs " + std::to_wstring(count) + L" new content slots, but only " +
            std::to_wstring(MaxEntries - occupied) + L" remain. Existing saved entries are preserved.";
        return false;
    }
    PresetPlan plan; plan.previousSlots = occupied; plan.firstEntry = occupied + 1;
    plan.lastEntry = occupied + static_cast<int>(count);
    const auto add = [&](const std::wstring& section, const wchar_t* key, std::wstring value) {
        if (!NormalizeSetting(section, key, value, error)) return false;
        plan.settings.push_back({section, key, std::move(value)}); return true;
    };
    if (!add(L"Content", L"Enabled", L"1") || !add(L"Content", L"Count", std::to_wstring(plan.lastEntry)) ||
        !add(L"Content", L"CycleEnabled", preset.cycle ? L"1" : L"0") ||
        !add(L"Content", L"CycleDriver", preset.cycleDriver == CycleDriver::Auto ? L"Auto" : preset.cycleDriver == CycleDriver::Windows ? L"Windows" : L"DesktopStub") ||
        !add(L"Content", L"TextMode", preset.textMode == TextMode::Overlay ? L"Overlay" : preset.textMode == TextMode::Off ? L"Off" : L"Auto")) return false;
    // Disable occupied slots and gaps without replacing any saved names, paths,
    // credentials or custom fields. Missing slots would otherwise default on.
    for (int index = 1; index <= occupied; ++index)
        if (!add(L"Content." + std::to_wstring(index), L"Enabled", L"0")) return false;
    for (std::size_t offset = 0; offset < count; ++offset) {
        const auto section = L"Content." + std::to_wstring(plan.firstEntry + static_cast<int>(offset));
        const auto& entry = preset.entries[offset];
        if (BackgroundIdFor(entry.background) == BackgroundId::Image && Trim(entry.imagePath).empty()) {
            error = L"The preset needs an image path for its image background."; return false;
        }
        const auto* detail = aip::notes::FindDetail(entry.notesDetail);
        if (!detail || !aip::notes::DetailAvailable(entry.notesResource, entry.notesDetail)) {
            error = L"The preset contains an unavailable Notes detail."; return false;
        }
        std::wstring sources;
        for (const auto& source : entry.sources) { if (!sources.empty()) sources += L","; sources += source; }
        const PresetSetting fields[] = {
            {section, L"Enabled", L"1"}, {section, L"Name", entry.name},
            {section, L"Background", entry.background}, {section, L"ImagePath", entry.imagePath},
            {section, L"TextSources", sources}, {section, L"Text", entry.custom.primary},
            {section, L"SecondaryText", entry.custom.secondary}, {section, L"BadgeText", entry.custom.badge},
            {section, L"RssItem", std::to_wstring(entry.rssItem)},
            {section, L"NotesResource", entry.notesResource >= 0 && entry.notesResource < 3 ? NotesResources[entry.notesResource].value : L""},
            {section, L"NotesDetail", detail->value},
        };
        for (const auto& field : fields) if (!add(field.section, field.key.c_str(), field.value)) return false;
    }
    output = std::move(plan); return true;
}
} }
