#ifndef AIP_CONTENT_SOURCES_NOW_PLAYING_PROFILE_INC
#define AIP_CONTENT_SOURCES_NOW_PLAYING_PROFILE_INC

// Explicit, read-only NowPlayingTile donor import. The shared native INI
// parser owns quoting, comments, encoding and repeated-key semantics. This
// adapter only returns a plan; it never queries media or changes Startup,
// package, registry, session, account or device state.
#include "../desktop_app_baseline.h"
#include "../content_engine.h"
#include <map>

namespace aip { namespace now_playing_profile {
enum class Layout { Text, Artwork, Combined, Cycle };
struct ProfileSnapshot {
    Layout layout = Layout::Text;
    int updateIntervalSeconds = 2;
    int tileRefreshSeconds = 60; // Donor delivery cadence, not a host cycle interval.
    bool showTrayIcon = true, runAtStartup = false, showMenuAsDropdown = true;
    std::vector<aip::IniSetting> unsupportedSettings;
};
struct ImportPlan {
    ProfileSnapshot snapshot;
    std::vector<aip::IniSetting> settings;
    std::vector<std::wstring> diagnostics;
};
struct ConfigurationProjection {
    bool donorRunAtStartup = false; // Preserved for display; never an applied setting.
    std::vector<aip::IniSetting> settings;
    std::vector<std::wstring> diagnostics;
};

// The full plan describes the donor-to-host equivalents. Offline import must
// use this projection so a donor Startup preference cannot become an implicit
// external transaction. Existing explicit host Startup controls remain the
// opt-in path; the donor preference is visible in both metadata and diagnostics.
inline bool ProjectConfigurationOnly(const ImportPlan& plan, ConfigurationProjection& output, std::wstring& error) {
    output = {}; error.clear();
    if (plan.settings.empty()) {
        error = L"A successful NowPlayingTile import plan is required before projection."; return false;
    }
    ConfigurationProjection next;
    next.donorRunAtStartup = plan.snapshot.runAtStartup;
    next.diagnostics = plan.diagnostics;
    for (const auto& setting : plan.settings) {
        if (aip::IniEquals(setting.section, L"Settings") &&
            (aip::IniEquals(setting.key, L"RunAtStartup") || aip::IniEquals(setting.key, L"RunAtStartupPackaged"))) continue;
        next.settings.push_back(setting);
    }
    next.diagnostics.push_back(L"Donor RunAtStartup=" + std::wstring(next.donorRunAtStartup ? L"true" : L"false") +
        L" is retained in the import snapshot and deferred. This configuration import preserves the current host Startup preference; use the host Run at sign-in control or a separate --startup/--no-startup command to apply your choice.");
    output = std::move(next); return true;
}

// Called inside the host's existing INI transaction before applying the new
// entry batch and any later explicit overrides. Works only on the supplied
// text; it does not open or mutate a source/destination file itself.
inline bool RemoveContentEntriesFromText(std::wstring& text) {
    auto candidate = text;
    const auto document = aip::ParseIniDocument(candidate);
    for (const auto& section : document)
        if (aip::ToLower(section.name).compare(0, 8, L"content.") == 0 &&
            !aip::RemoveIniSectionFromText(candidate, section.name.c_str())) return false;
    text = std::move(candidate); return true;
}

inline std::wstring CanonicalKey(const std::wstring& raw) {
    auto key = aip::ToLower(aip::Trim(raw));
    key.erase(std::remove_if(key.begin(), key.end(), [](wchar_t ch) {
        return ch == L'-' || ch == L'_';
    }), key.end());
    for (const auto* name : {L"TileLayout", L"UpdateIntervalSeconds", L"TileRefreshSeconds",
        L"ShowTrayIcon", L"RunAtStartup", L"ShowMenuAsDropdown"})
        if (key == aip::ToLower(name)) return name;
    return {};
}

inline bool ValidateText(const std::wstring& text, std::wstring& error) {
    if (text.size() > aip::kMaxIniFileBytes) {
        error = L"The NowPlayingTile profile exceeds the native INI text bound."; return false;
    }
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto ch = text[index];
        if (!ch || (ch >= 0xdc00 && ch <= 0xdfff) || (ch >= 0xd800 && ch <= 0xdbff &&
            (++index == text.size() || text[index] < 0xdc00 || text[index] > 0xdfff))) {
            error = L"The NowPlayingTile profile contains invalid Unicode or NUL text."; return false;
        }
    }
    bool hasSection = false;
    auto lines = aip::SplitIniLines(text);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        auto line = lines[index];
        if (!index) aip::StripLeadingIniBom(line);
        const auto trimmed = aip::Trim(line);
        if (trimmed.empty() || trimmed.front() == L';' || trimmed.front() == L'#') continue;
        std::wstring section;
        if (aip::TryParseIniSectionHeader(line, section)) {
            if (!section.empty()) { hasSection = true; continue; }
        } else if (trimmed.front() != L'[' && hasSection) {
            const auto equals = line.find(L'=');
            if (equals != std::wstring::npos && !aip::ParseIniName(line.substr(0, equals)).empty()) continue;
        }
        error = L"Malformed NowPlayingTile INI line " + std::to_wstring(index + 1) + L".";
        return false;
    }
    return true;
}

inline bool NormalizeDonorValue(const std::wstring& key, std::wstring& value, std::wstring& error) {
    value = aip::Trim(value);
    if (key == L"TileLayout") {
        for (const auto* layout : {L"Text", L"Artwork", L"Combined", L"Cycle"})
            if (aip::IniEquals(value, layout)) { value = layout; return true; }
        error = L"[Settings] TileLayout expects Text, Artwork, Combined, or Cycle.";
        return false;
    }
    if (key == L"UpdateIntervalSeconds" || key == L"TileRefreshSeconds") {
        int number = 0;
        const int maximum = key == L"UpdateIntervalSeconds" ? 60 : 300;
        if (!aip::ParseIntValueInRange(value, 1, maximum, number)) {
            error = L"[Settings] " + key + L" expects an integer from 1 through " +
                std::to_wstring(maximum) + L"."; return false;
        }
        value = std::to_wstring(number); return true;
    }
    bool enabled = false;
    if (!aip::ParseBoolValue(value, enabled)) {
        error = L"[Settings] " + key + L" expects a true/false value."; return false;
    }
    value = enabled ? L"1" : L"0"; return true;
}

inline bool AddHostSetting(ImportPlan& plan, const wchar_t* section, const wchar_t* key,
    std::wstring value, std::wstring& error) {
    if (aip::content::KnownSection(section) && !aip::content::NormalizeSetting(section, key, value, error)) return false;
    plan.settings.push_back({section, key, std::move(value)}); return true;
}
inline bool AddEntry(ImportPlan& plan, int index, const wchar_t* name, const wchar_t* background,
    const wchar_t* sources, std::wstring& error) {
    const auto section = L"Content." + std::to_wstring(index);
    for (const auto& setting : std::vector<aip::IniSetting>{{section, L"Enabled", L"1"},
        {section, L"Name", name}, {section, L"Background", background}, {section, L"ImagePath", L""},
        {section, L"TextSources", sources}, {section, L"Text", L""}, {section, L"SecondaryText", L""}, {section, L"BadgeText", L""}})
        if (!AddHostSetting(plan, section.c_str(), setting.key.c_str(), setting.value, error)) return false;
    return true;
}

inline bool ImportProfileText(const std::wstring& text, ImportPlan& output, std::wstring& error) {
    output = {}; error.clear();
    if (!ValidateText(text, error)) return false;
    ImportPlan next;
    std::map<std::wstring, std::wstring> donor;
    bool settingsSeen = false;
    const auto document = aip::ParseIniDocument(text);
    for (const auto& section : document) {
        if (!aip::IniEquals(section.name, L"Settings")) {
            next.diagnostics.push_back(L"[" + section.name + L"] has no equivalent NowPlayingTile host section; its preferences were not applied.");
            for (const auto& entry : section.entries) next.snapshot.unsupportedSettings.push_back({section.name, entry.key, entry.value});
            continue;
        }
        settingsSeen = true;
        for (const auto& entry : section.entries) {
            const auto key = CanonicalKey(entry.key);
            if (key.empty()) {
                next.snapshot.unsupportedSettings.push_back({section.name, entry.key, entry.value});
                next.diagnostics.push_back(L"[Settings] " + entry.key + L" has no equivalent donor-to-host setting; widget text, font, color, image and geometry preferences are not applied.");
                continue;
            }
            auto value = entry.value;
            if (!NormalizeDonorValue(key, value, error)) return false;
            const auto inserted = donor.emplace(key, value);
            if (!inserted.second && inserted.first->second != value) {
                error = L"Conflicting NowPlayingTile aliases for [Settings] " + key + L"."; return false;
            }
        }
    }
    if (!settingsSeen) { error = L"No NowPlayingTile [Settings] section was found in the selected profile."; return false; }
    donor.emplace(L"TileLayout", L"Text"); donor.emplace(L"UpdateIntervalSeconds", L"2");
    donor.emplace(L"TileRefreshSeconds", L"60"); donor.emplace(L"ShowTrayIcon", L"1");
    donor.emplace(L"RunAtStartup", L"0"); donor.emplace(L"ShowMenuAsDropdown", L"1");
    auto& snapshot = next.snapshot;
    if (donor.at(L"TileLayout") == L"Artwork") snapshot.layout = Layout::Artwork;
    else if (donor.at(L"TileLayout") == L"Combined") snapshot.layout = Layout::Combined;
    else if (donor.at(L"TileLayout") == L"Cycle") snapshot.layout = Layout::Cycle;
    aip::ParseIntValue(donor.at(L"UpdateIntervalSeconds"), snapshot.updateIntervalSeconds);
    aip::ParseIntValue(donor.at(L"TileRefreshSeconds"), snapshot.tileRefreshSeconds);
    snapshot.showTrayIcon = donor.at(L"ShowTrayIcon") == L"1";
    snapshot.runAtStartup = donor.at(L"RunAtStartup") == L"1";
    snapshot.showMenuAsDropdown = donor.at(L"ShowMenuAsDropdown") == L"1";
    const bool cycle = snapshot.layout == Layout::Cycle;
    const bool overlay = snapshot.layout == Layout::Combined || cycle;
    if (!AddHostSetting(next, L"Content", L"Enabled", L"1", error) ||
        !AddHostSetting(next, L"Content", L"Count", cycle ? L"3" : L"1", error) ||
        !AddHostSetting(next, L"Content", L"CycleEnabled", cycle ? L"1" : L"0", error) ||
        !AddHostSetting(next, L"Content", L"RefreshSeconds", donor.at(L"UpdateIntervalSeconds"), error) ||
        !AddHostSetting(next, L"Content", L"TextMode", overlay ? L"Overlay" : L"Auto", error)) return false;
    if (cycle) {
        if (!AddEntry(next, 1, L"Now playing text", L"None", L"SMTC", error) ||
            !AddEntry(next, 2, L"Now playing artwork", L"SMTCArtwork", L"", error) ||
            !AddEntry(next, 3, L"Now playing combined", L"SMTCArtwork", L"SMTC", error)) return false;
        next.diagnostics.push_back(L"Cycle imports Text, Artwork and Combined entries. Windows controlled the donor's queued rotation; the host uses its existing Content.CycleSeconds preference (10 seconds when absent).");
        next.diagnostics.push_back(L"The host TextMode is global. Cycle uses Overlay for the Combined entry, so its Text entry also uses the host's bitmap text renderer.");
    } else if (snapshot.layout == Layout::Artwork) {
        if (!AddEntry(next, 1, L"Now playing artwork", L"SMTCArtwork", L"", error)) return false;
    } else if (snapshot.layout == Layout::Combined) {
        if (!AddEntry(next, 1, L"Now playing combined", L"SMTCArtwork", L"SMTC", error)) return false;
    } else if (!AddEntry(next, 1, L"Now playing text", L"None", L"SMTC", error)) return false;
    // Only these three host preferences have equivalents. In particular the
    // packaged donor StartupTask must not select RunAtStartupPackaged.
    if (!AddHostSetting(next, L"Settings", L"TrayIcon", donor.at(L"ShowTrayIcon"), error) ||
        !AddHostSetting(next, L"Settings", L"RunAtStartup", donor.at(L"RunAtStartup"), error) ||
        !AddHostSetting(next, L"Settings", L"ShowMenuAsDropdown", donor.at(L"ShowMenuAsDropdown"), error)) return false;
    next.diagnostics.push_back(L"TileRefreshSeconds=" + std::to_wstring(snapshot.tileRefreshSeconds) +
        L" controls donor delivery refresh. The host has no equivalent preference, so it was retained in the import snapshot and not mapped to source refresh or cycle timing.");
    next.diagnostics.push_back(L"RunAtStartup maps to the host's native Settings.RunAtStartup preference. This plan performs no Startup change; later application must use the host's normal Startup transaction.");
    error.clear(); output = std::move(next); return true;
}

inline bool ImportProfileFile(const std::wstring& path, ImportPlan& output, std::wstring& error) {
    output = {}; error.clear();
    std::vector<BYTE> bytes;
    std::wstring text;
    if (!aip::ReadWholeFileBytes(path, bytes, aip::kMaxIniFileBytes)) {
        error = L"Could not read the NowPlayingTile donor profile (error " + std::to_wstring(GetLastError()) + L")."; return false;
    }
    if (!aip::DecodeTextBytes(bytes, text)) {
        error = L"The NowPlayingTile donor profile has invalid native INI encoding."; return false;
    }
    return ImportProfileText(text, output, error);
}
} }
#endif
