#ifndef AIP_HARDWARE_PROFILE_IMPORT_INC
#define AIP_HARDWARE_PROFILE_IMPORT_INC

// Read-only donor profile adapters. The existing native INI reader owns the
// dialect/encoding/file bound; the native engines own pattern validation.
// Importing never constructs a controller/backend or writes the source file.
#include "../desktop_app_baseline.h"
#include "caps_blink_engine.h"
#include "../asusblink/pattern.h"
#include <map>

namespace aip { namespace hardware_profile {
using Settings = std::map<std::wstring, std::wstring>;
struct CapsProfile {
    aip::caps::Config configuration;
    Settings ignoredPreferences; // recognized donor settings, never applied
};
struct AsusProfile {
    aip::asus::Configuration configuration;
    Settings settings; // canonical pattern keys, including numbered events
    Settings ignoredPreferences; // Startup/tray/log settings, never applied
};

inline bool HostPreference(const std::wstring& key) {
    return key == L"RunAtStartup" || key == L"ShowTrayIcon" ||
        key == L"ShowMenuAsDropdown" || key == L"ErrorLog";
}
inline std::wstring CanonicalCapsKey(const std::wstring& raw) {
    const auto key = aip::asus::Lower(aip::asus::Trim(raw));
    if (key == L"keyboardtargetpath" || key == L"keyboard-target-path") return L"KeyboardTargetPath";
    if (key == L"blinkintervalms" || key == L"blink-interval-ms") return L"BlinkIntervalMs";
    if (key == L"runatstartup" || key == L"run-at-startup") return L"RunAtStartup";
    if (key == L"showtrayicon" || key == L"show-tray" || key == L"show-tray-icon") return L"ShowTrayIcon";
    if (key == L"showmenuasdropdown" || key == L"show-menu-as-dropdown") return L"ShowMenuAsDropdown";
    return {};
}
inline std::wstring CanonicalAsusKey(const std::wstring& raw) {
    auto key = aip::asus::Lower(aip::asus::Trim(raw));
    key.erase(std::remove_if(key.begin(), key.end(), [](wchar_t ch) { return ch == L'-' || ch == L'_'; }), key.end());
    for (const auto* canonical : {L"ErrorLog", L"ErrorRetry", L"ErrorAction", L"RunAtStartup", L"ShowTrayIcon", L"ShowMenuAsDropdown",
        L"MicState", L"MicInterval", L"MicDuration", L"KeyboardState", L"KeyboardInterval", L"KeyboardDuration"})
        if (key == aip::asus::Lower(canonical)) return canonical;
    if (key == L"showtray") return L"ShowTrayIcon";
    if (key == L"runstartup") return L"RunAtStartup";
    if (key.compare(0, 5, L"event") != 0) return {};
    std::size_t end = 5;
    while (end < key.size() && key[end] >= L'0' && key[end] <= L'9') ++end;
    if (end == 5 || end > 8) return {};
    const auto number = key.substr(5, end - 5); // preserve event identity, including leading zeroes
    const auto suffix = key.substr(end);
    for (const auto* device : {L"Mic", L"Keyboard", L"Hdd"})
        for (const auto* field : {L"State", L"Interval", L"Duration"})
            if (suffix == aip::asus::Lower(std::wstring(device) + field)) return L"Event" + number + device + field;
    return {};
}
inline bool NormalizePreference(const std::wstring& key, const std::wstring& raw,
                                std::wstring& value, std::wstring& error) {
    value = aip::asus::Trim(raw);
    if (raw.find_first_of(L"\r\n") != std::wstring::npos || raw.find(L'\0') != std::wstring::npos) {
        error = key + L" must be a single-line value."; return false;
    }
    if (key == L"ErrorLog") {
        if (value.empty()) { error = L"ErrorLog requires a value."; return false; }
        return true;
    }
    bool enabled = false;
    // These are preferences only; parsing them never changes Startup or UI.
    if (!aip::ParseBoolValue(value, enabled)) { error = key + L" must be true/false."; return false; }
    value = enabled ? L"true" : L"false";
    return true;
}
inline bool ParseCapsProfile(const std::vector<aip::IniSectionData>& document, CapsProfile& result, std::wstring& error) {
    CapsProfile next;
    next.ignoredPreferences = {{L"RunAtStartup", L"false"}, {L"ShowTrayIcon", L"true"}, {L"ShowMenuAsDropdown", L"true"}};
    for (const auto& section : document) if (aip::IniEquals(section.name, L"Settings"))
        for (const auto& entry : section.entries) {
            const auto key = CanonicalCapsKey(entry.key);
            if (key.empty()) { error = L"Unknown Caps [Settings] key: " + entry.key; return false; }
            if (key == L"KeyboardTargetPath") next.configuration.target = entry.value;
            else if (key == L"BlinkIntervalMs") {
                if (!aip::ParseIntValue(entry.value, next.configuration.intervalMs)) { error = L"BlinkIntervalMs requires an integer."; return false; }
            } else {
                std::wstring normalized;
                if (!NormalizePreference(key, entry.value, normalized, error)) return false;
                next.ignoredPreferences[key] = normalized;
            }
            // The Caps donor applies aliases in its effective entry order.
            if (!aip::caps::Normalize(next.configuration, error)) return false;
        }
    next.configuration.actionsEnabled = false;
    if (!aip::caps::Normalize(next.configuration, error)) return false;
    error.clear(); result = std::move(next); return true;
}
inline bool NormalizeAsusSetting(const std::wstring& key, const std::wstring& raw,
                                 std::wstring& value, std::wstring& error) {
    if (HostPreference(key)) return NormalizePreference(key, raw, value, error);
    value = aip::asus::Trim(raw);
    if (value.empty() || raw.find_first_of(L"\r\n") != std::wstring::npos || raw.find(L'\0') != std::wstring::npos) {
        error = key + L" requires a single-line value."; return false;
    }
    aip::asus::Configuration parsed;
    if (!aip::asus::ParseConfiguration({{key, value}}, parsed, error)) return false;
    if (key == L"ErrorRetry") value = std::to_wstring(parsed.retries);
    else if (key == L"ErrorAction") {
        value.clear();
        std::vector<std::wstring> seen;
        for (const auto& action : aip::asus::Split(aip::asus::Lower(raw)))
            if (std::find(seen.begin(), seen.end(), action) == seen.end()) {
                if (!value.empty()) value += L",";
                value += action; seen.push_back(action);
            }
    } else if (key.size() >= 5 && key.compare(key.size() - 5, 5, L"State") == 0) {
        if (aip::asus::Lower(value) == L"off") value = L"off";
        else {
            value.clear();
            for (int state : parsed.events.front().states) {
                if (!value.empty()) value += L",";
                value += std::to_wstring(state);
            }
        }
    } else {
        value.clear();
        for (const auto& part : aip::asus::Split(aip::asus::Lower(raw))) {
            if (!value.empty()) value += L",";
            value += part;
        }
    }
    return true;
}
inline bool ParseAsusProfile(const std::vector<aip::IniSectionData>& document, AsusProfile& result, std::wstring& error) {
    Settings modern, legacy;
    // Canonicalize modern keys first so they mask obsolete legacy values
    // before value validation, including the legacy inverse no-tray flag.
    for (const auto& section : document) if (aip::IniEquals(section.name, L"Settings"))
        for (const auto& entry : section.entries) {
            const auto key = CanonicalAsusKey(entry.key);
            if (key.empty()) { error = L"Unknown ASUS [Settings] key: " + entry.key; return false; }
            if (!modern.emplace(key, entry.value).second) { error = L"Duplicate semantic ASUS [Settings] key: " + entry.key; return false; }
        }
    bool noTraySeen = false, noTray = false;
    for (const auto& section : document) if (aip::IniEquals(section.name, L"Options"))
        for (const auto& entry : section.entries) {
            const bool inverseTray = aip::IniEquals(entry.key, L"no-tray");
            const auto key = inverseTray ? std::wstring(L"ShowTrayIcon") : CanonicalAsusKey(entry.key);
            if (key.empty()) { error = L"Unknown ASUS [Options] key: " + entry.key; return false; }
            if (modern.count(key)) continue;
            if (inverseTray) {
                noTraySeen = true;
                if (aip::asus::Trim(entry.value).empty()) noTray = true;
                else if (!aip::ParseBoolValue(entry.value, noTray)) { error = L"Legacy no-tray must be empty or true/false."; return false; }
            } else {
                std::wstring normalized;
                if (!NormalizeAsusSetting(key, entry.value, normalized, error)) return false;
                legacy[key] = normalized;
            }
        }
    if (noTraySeen && !legacy.count(L"ShowTrayIcon")) legacy[L"ShowTrayIcon"] = noTray ? L"false" : L"true";
    AsusProfile next;
    next.settings = {{L"MicState", L"off"}, {L"MicInterval", L"0"}, {L"MicDuration", L"once"},
        {L"KeyboardState", L"off"}, {L"KeyboardInterval", L"0"}, {L"KeyboardDuration", L"once"},
        {L"ErrorRetry", L"3"}, {L"ErrorAction", L"continue"}};
    next.ignoredPreferences = {{L"RunAtStartup", L"false"}, {L"ShowTrayIcon", L"true"}, {L"ShowMenuAsDropdown", L"true"}};
    for (const auto& entry : modern) {
        std::wstring normalized;
        if (!NormalizeAsusSetting(entry.first, entry.second, normalized, error)) return false;
        legacy[entry.first] = normalized;
    }
    for (const auto& entry : legacy)
        (HostPreference(entry.first) ? next.ignoredPreferences : next.settings)[entry.first] = entry.second;
    if (!aip::asus::ParseConfiguration(next.settings, next.configuration, error)) return false;
    error.clear(); result = std::move(next); return true;
}
inline bool ParseCapsProfileText(const std::wstring& text, CapsProfile& result, std::wstring& error) {
    return ParseCapsProfile(aip::ParseIniDocument(text), result, error);
}
inline bool ParseAsusProfileText(const std::wstring& text, AsusProfile& result, std::wstring& error) {
    return ParseAsusProfile(aip::ParseIniDocument(text), result, error);
}
inline bool ReadProfileText(const std::wstring& path, std::wstring& text, std::wstring& error) {
    if (!aip::ReadTextFileUtf8BomAware(path, text)) {
        error = L"Could not read donor hardware profile: " + path + L" (error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    return true;
}
inline bool ReadCapsProfile(const std::wstring& path, CapsProfile& result, std::wstring& error) {
    std::wstring text;
    return ReadProfileText(path, text, error) && ParseCapsProfileText(text, result, error);
}
inline bool ReadAsusProfile(const std::wstring& path, AsusProfile& result, std::wstring& error) {
    std::wstring text;
    return ReadProfileText(path, text, error) && ParseAsusProfileText(text, result, error);
}
inline Settings ToHostSettings(const CapsProfile& profile) {
    return {{L"CapsBlink.KeyboardTargetPath", profile.configuration.target},
        {L"CapsBlink.BlinkIntervalMs", std::to_wstring(profile.configuration.intervalMs)}};
}
inline Settings ToHostSettings(const AsusProfile& profile) {
    Settings settings;
    for (const auto& entry : profile.settings) settings[L"AsusBlink." + entry.first] = entry.second;
    return settings;
}
} }
#endif
