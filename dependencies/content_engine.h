#pragma once

// Platform-neutral composition policy. Providers return data; the host owns
// rendering and delivery. No provider writes the user's configuration.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>
#include <utility>
#include "hardware/caps_blink_engine.h"
#include "asusblink/pattern.h"

namespace aip { namespace content {
constexpr int MaxEntries = 32;
constexpr std::size_t MaxText = 4096;
enum class TextMode { Auto, Overlay, Off };
struct Choice { const wchar_t* value; const wchar_t* label; };
inline constexpr Choice TextModes[] = {
    {L"Auto", L"Windows text with bitmap fallback"},
    {L"Overlay", L"Text over the background image"},
    {L"Off", L"Hide text"},
};
inline constexpr Choice Backgrounds[] = {
    {L"Wallpaper", L"Desktop wallpaper"},
    {L"LiveWallpaper", L"Live wallpaper capture"},
    {L"Image", L"Custom image"},
    {L"None", L"Solid color"},
};
inline constexpr Choice Sources[] = {
    {L"CustomText", L"Custom text"},
    {L"RssFeed", L"RSS / Atom headlines"},
    {L"CapsLock", L"Caps Lock status"},
    {L"SMTC", L"Now playing (SMTC)"},
    {L"Notes", L"Real-Time Notes"},
    {L"DiscordRPC", L"Discord Rich Presence"},
    {L"CapsBlink", L"Caps indicator pattern"},
    {L"AsusBlink", L"ASUS indicator patterns"},
};
inline constexpr const wchar_t* AsusPatternKeys[] = {L"MicState", L"MicInterval", L"MicDuration", L"KeyboardState", L"KeyboardInterval", L"KeyboardDuration", L"ErrorRetry", L"ErrorAction"};
inline constexpr const wchar_t* AsusPatternDefaults[] = {L"off", L"0", L"once", L"off", L"0", L"once", L"3", L"continue"};
struct AsusConfiguration {
    bool hardwareEnabled = false, readDiskActivity = false, paused = false;
    std::wstring profilePath;
    std::map<std::wstring, std::wstring> settings;
};
struct DiscordConfiguration {
    bool send = false, clearOnStop = true;
    int refreshSeconds = 5, timeoutMs = 5000;
    std::wstring profilePath, clientId, name = L"DesktopStub", details = L"{win_title}", state = L"CPU: {cpu}";
};
inline constexpr Choice NotesResources[] = {{L"Resin", L"Genshin Impact resin"}, {L"Stamina", L"Star Rail stamina"}, {L"Charge", L"Zenless Zone Zero charge"}};
struct NotesAccount { std::wstring uid, token, userId; };
struct NotesConfiguration {
    int refreshSeconds = 300, timeoutSeconds = 15, maximumBytes = 1048576;
    std::array<NotesAccount, 3> accounts;
};
struct Text { std::wstring primary, secondary, badge; };
struct Entry {
    bool enabled = true;
    std::wstring name, background = L"Wallpaper", imagePath;
    std::vector<std::wstring> sources;
    Text custom;
    int rssItem = 1; // One-based index; independent of the content's cycle position.
    int notesResource = 0;
};
struct Configuration {
    bool enabled = false, cycle = false;
    int cycleSeconds = 10, refreshSeconds = 2;
    TextMode textMode = TextMode::Auto;
    std::vector<Entry> entries;
    NotesConfiguration notes;
    DiscordConfiguration discord;
    aip::caps::Config caps;
    AsusConfiguration asus;
};
struct Snapshot {
    bool active = false;
    std::wstring name, backgroundPath, activationArguments;
    Text text;
    TextMode textMode = TextMode::Auto;
};
inline std::wstring Trim(std::wstring s) {
    const auto first = s.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    return s.substr(first, s.find_last_not_of(L" \t\r\n") - first + 1);
}
inline std::wstring Lower(std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(std::towlower(ch));
    return s;
}
inline bool ParseInt(const std::wstring& input, int low, int high, int& result) {
    auto s = Trim(input);
    if (s.empty()) return false;
    std::uint64_t value = 0;
    for (auto ch : s) {
        if (ch < L'0' || ch > L'9') return false;
        value = value * 10 + static_cast<unsigned>(ch - L'0');
        if (value > static_cast<std::uint64_t>(high)) return false;
    }
    if (value < static_cast<std::uint64_t>(low)) return false;
    result = static_cast<int>(value); return true;
}
inline bool ParseBool(const std::wstring& input, bool& value) {
    auto s = Lower(Trim(input));
    if (s == L"1" || s == L"true" || s == L"yes" || s == L"on" || s == L"enabled") { value = true; return true; }
    if (s == L"0" || s == L"false" || s == L"no" || s == L"off" || s == L"disabled") { value = false; return true; }
    return false;
}
inline bool EntrySection(const std::wstring& section) {
    auto s = Lower(section);
    if (s.compare(0, 8, L"content.") != 0) return false;
    int index = 0;
    return ParseInt(s.substr(8), 1, MaxEntries, index) && s.substr(8) == std::to_wstring(index);
}
inline bool KnownSection(const std::wstring& section) {
    auto s = Lower(section);
    return s == L"content" || s.compare(0, 8, L"content.") == 0 || s == L"notes" || s.compare(0, 6, L"notes.") == 0 || s == L"discord" || s == L"capsblink" || s == L"asusblink";
}
inline bool SensitiveSetting(const std::wstring& section, const std::wstring& key) {
    return Lower(section).compare(0, 6, L"notes.") == 0 && (Lower(key) == L"ltoken" || Lower(key) == L"ltuid");
}
inline bool ParseSources(const std::wstring& value, std::vector<std::wstring>& sources) {
    sources.clear();
    if (Trim(value).empty()) return true;
    std::size_t start = 0;
    while (start <= value.size()) {
        auto end = value.find(L',', start);
        auto item = Lower(Trim(value.substr(start, end == std::wstring::npos ? end : end - start)));
        std::wstring canonical;
        for (const auto& choice : Sources) if (item == Lower(choice.value)) canonical = choice.value;
        if (canonical.empty()) return false;
        if (std::find(sources.begin(), sources.end(), canonical) != sources.end()) return false;
        sources.push_back(canonical);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return true;
}
inline bool NormalizeSetting(const std::wstring& section, const std::wstring& key,
                             std::wstring& value, std::wstring& error) {
    if (!KnownSection(section)) return true;
    const bool root = Lower(section) == L"content";
    const auto k = Lower(key);
    error = section + L"." + key + L": ";
    if (Lower(section) == L"asusblink") {
        if (k == L"hardwareenabled" || k == L"readdiskactivity" || k == L"paused") {
            bool parsed = false;
            if (!ParseBool(value, parsed)) { error += L"expected true/false."; return false; }
            value = parsed ? L"1" : L"0";
        } else if (k == L"profilepath") {
            if (value.size() > MaxText || value.find(L'\0') != std::wstring::npos) { error += L"invalid path text."; return false; }
        } else {
            bool known = false;
            for (const auto* name : AsusPatternKeys) if (k == Lower(name)) known = true;
            if (!known) { error += L"unknown inline setting; use an advanced profile for numbered events."; return false; }
            aip::asus::Configuration parsed;
            if (!aip::asus::ParseConfiguration({{key, value}}, parsed, error)) return false;
        }
        error.clear(); return true;
    }
    if (Lower(section) == L"capsblink") {
        if (k == L"hardwareenabled") {
            bool parsed = false;
            if (!ParseBool(value, parsed)) { error += L"expected true/false."; return false; }
            value = parsed ? L"1" : L"0";
        } else if (k == L"blinkintervalms") {
            int parsed = 0;
            if (!ParseInt(value, 50, 86400000, parsed)) { error += L"expected 50-86400000 milliseconds."; return false; }
            value = std::to_wstring(parsed);
        } else if (k == L"keyboardtargetpath") {
            aip::caps::Config config;
            config.target = value;
            if (!aip::caps::Normalize(config, error)) return false;
            value = config.target;
        } else { error += L"unknown CapsBlink setting."; return false; }
        error.clear(); return true;
    }
    if (Lower(section) == L"discord") {
        if (k == L"sendpresence" || k == L"clearonstop") {
            bool parsed = false;
            if (!ParseBool(value, parsed)) { error += L"expected true/false."; return false; }
            value = parsed ? L"1" : L"0";
        } else if (k == L"refreshseconds" || k == L"operationtimeoutms") {
            int parsed = 0;
            if (!ParseInt(value, k == L"refreshseconds" ? 1 : 100, k == L"refreshseconds" ? 86400 : 30000, parsed)) {
                error += L"integer outside supported range."; return false;
            }
            value = std::to_wstring(parsed);
        } else if (k == L"clientid") {
            value = Trim(value);
            if (!value.empty() && (value.size() < 17 || value.size() > 20 ||
                std::any_of(value.begin(), value.end(), [](wchar_t ch) { return ch < L'0' || ch > L'9'; }))) {
                error += L"expected 17-20 digits or an empty preview client ID."; return false;
            }
        } else if (k == L"profilepath" || k == L"name" || k == L"details" || k == L"state") {
            if (value.size() > MaxText || value.find(L'\0') != std::wstring::npos) { error += L"invalid text length or NUL."; return false; }
        } else { error += L"unknown Discord setting."; return false; }
        error.clear(); return true;
    }
    if (Lower(section) == L"notes") {
        int low = 0, high = 0, parsed = 0;
        if (k == L"refreshseconds") { low = 30; high = 86400; }
        else if (k == L"httptimeoutseconds") { low = 1; high = 120; }
        else if (k == L"maximumbytes") { low = 1024; high = 16777216; }
        else { error += L"unknown Notes setting."; return false; }
        if (!ParseInt(value, low, high, parsed)) { error += L"integer outside supported range."; return false; }
        value = std::to_wstring(parsed); error.clear(); return true;
    }
    if (Lower(section).compare(0, 6, L"notes.") == 0) {
        bool known = false;
        for (const auto& resource : NotesResources) if (Lower(section.substr(6)) == Lower(resource.value)) known = true;
        if (!known || (k != L"uid" && k != L"ltoken" && k != L"ltuid")) { error += L"unknown account setting."; return false; }
        value = Trim(value);
        if (value.size() > (k == L"uid" ? 32u : MaxText) ||
            std::any_of(value.begin(), value.end(), [&](wchar_t ch) { return k == L"uid" ? ch < L'0' || ch > L'9' : ch < 0x20 || ch == L';' || ch == 0x7F; })) {
            error += L"invalid account value (not shown)."; return false;
        }
        error.clear(); return true;
    }
    if (!root && !EntrySection(section)) { error += L"entry number must be 1 through 32 (without leading zeroes)."; return false; }
    if (k == L"enabled" || (root && k == L"cycleenabled")) {
        bool parsed = false;
        if (!ParseBool(value, parsed)) { error += L"expected true/false."; return false; }
        value = parsed ? L"1" : L"0";
    } else if (root && (k == L"count" || k == L"cycleseconds" || k == L"refreshseconds")) {
        int parsed = 0;
        if (!ParseInt(value, 1, k == L"count" ? MaxEntries : 86400, parsed)) { error += L"integer outside supported range."; return false; }
        value = std::to_wstring(parsed);
    } else if (!root && k == L"notesresource") {
        for (const auto& resource : NotesResources) if (Lower(Trim(value)) == Lower(resource.value)) {
            value = resource.value; error.clear(); return true;
        }
        error += L"expected Resin, Stamina, or Charge."; return false;
    } else if (!root && k == L"rssitem") {
        int parsed = 0;
        if (!ParseInt(value, 1, 20, parsed)) { error += L"expected a headline number from 1 to 20."; return false; }
        value = std::to_wstring(parsed);
    } else if (root && k == L"textmode") {
        auto mode = Lower(Trim(value));
        if (mode == L"auto") value = L"Auto";
        else if (mode == L"overlay") value = L"Overlay";
        else if (mode == L"off") value = L"Off";
        else { error += L"expected Auto, Overlay, or Off."; return false; }
    } else if (!root && k == L"background") {
        auto bg = Lower(Trim(value));
        if (bg == L"wallpaper") value = L"Wallpaper";
        else if (bg == L"livewallpaper") value = L"LiveWallpaper";
        else if (bg == L"image") value = L"Image";
        else if (bg == L"none") value = L"None";
        else { error += L"expected Wallpaper, LiveWallpaper, Image, or None."; return false; }
    } else if (!root && k == L"textsources") {
        std::vector<std::wstring> sources;
        if (!ParseSources(value, sources)) { error += L"expected unique CustomText, RssFeed, CapsLock, SMTC, Notes, DiscordRPC, CapsBlink, AsusBlink names separated by commas."; return false; }
        value.clear();
        for (auto& source : sources) { if (!value.empty()) value += L","; value += source; }
    } else if (!root && (k == L"name" || k == L"imagepath" || k == L"text" || k == L"secondarytext" || k == L"badgetext")) {
        if (value.size() > MaxText || value.find(L'\0') != std::wstring::npos) { error += L"text must contain at most 4096 characters and no NUL."; return false; }
    } else { error += L"unknown setting."; return false; }
    error.clear(); return true;
}
template<class Reader> bool ReadConfiguration(Reader read, Configuration& result, std::wstring& error) {
    Configuration cfg;
    bool valid = true;
    auto get = [&](const std::wstring& section, const wchar_t* key, const wchar_t* fallback) {
        auto value = read(section.c_str(), key, fallback);
        std::wstring issue;
        if (!NormalizeSetting(section, key, value, issue)) { if (valid) error = issue; valid = false; }
        return value;
    };
    cfg.enabled = get(L"Content", L"Enabled", L"0") == L"1";
    cfg.cycle = get(L"Content", L"CycleEnabled", L"0") == L"1";
    ParseInt(get(L"Content", L"CycleSeconds", L"10"), 1, 86400, cfg.cycleSeconds);
    ParseInt(get(L"Content", L"RefreshSeconds", L"2"), 1, 86400, cfg.refreshSeconds);
    auto mode = get(L"Content", L"TextMode", L"Auto");
    cfg.textMode = mode == L"Overlay" ? TextMode::Overlay : mode == L"Off" ? TextMode::Off : TextMode::Auto;
    int count = 1;
    ParseInt(get(L"Content", L"Count", L"1"), 1, MaxEntries, count);
    for (int i = 1; i <= count; ++i) {
        auto section = L"Content." + std::to_wstring(i);
        Entry entry;
        entry.enabled = get(section, L"Enabled", L"1") == L"1";
        entry.name = get(section, L"Name", section.c_str());
        entry.background = get(section, L"Background", L"Wallpaper");
        entry.imagePath = get(section, L"ImagePath", L"");
        ParseSources(get(section, L"TextSources", L""), entry.sources);
        entry.custom.primary = get(section, L"Text", L"");
        entry.custom.secondary = get(section, L"SecondaryText", L"");
        entry.custom.badge = get(section, L"BadgeText", L"");
        ParseInt(get(section, L"RssItem", L"1"), 1, 20, entry.rssItem);
        const auto notesResource = get(section, L"NotesResource", L"Resin");
        for (int resource = 0; resource < 3; ++resource) if (notesResource == NotesResources[resource].value) entry.notesResource = resource;
        if (entry.enabled && entry.background == L"Image" && Trim(entry.imagePath).empty()) {
            if (valid) error = section + L".ImagePath is required for an Image background.";
            valid = false;
        }
        cfg.entries.push_back(std::move(entry));
    }
    ParseInt(get(L"Notes", L"RefreshSeconds", L"300"), 30, 86400, cfg.notes.refreshSeconds);
    ParseInt(get(L"Notes", L"HttpTimeoutSeconds", L"15"), 1, 120, cfg.notes.timeoutSeconds);
    ParseInt(get(L"Notes", L"MaximumBytes", L"1048576"), 1024, 16777216, cfg.notes.maximumBytes);
    for (int resource = 0; resource < 3; ++resource) {
        const auto section = L"Notes." + std::wstring(NotesResources[resource].value);
        cfg.notes.accounts[resource].uid = get(section, L"UID", L"");
        cfg.notes.accounts[resource].token = get(section, L"LToken", L"");
        cfg.notes.accounts[resource].userId = get(section, L"LTUid", L"");
    }
    cfg.discord.profilePath = get(L"Discord", L"ProfilePath", L"");
    cfg.discord.clientId = get(L"Discord", L"ClientId", L"");
    cfg.discord.name = get(L"Discord", L"Name", L"DesktopStub");
    cfg.discord.details = get(L"Discord", L"Details", L"{win_title}");
    cfg.discord.state = get(L"Discord", L"State", L"CPU: {cpu}");
    cfg.discord.send = get(L"Discord", L"SendPresence", L"0") == L"1";
    cfg.discord.clearOnStop = get(L"Discord", L"ClearOnStop", L"1") == L"1";
    ParseInt(get(L"Discord", L"RefreshSeconds", L"5"), 1, 86400, cfg.discord.refreshSeconds);
    ParseInt(get(L"Discord", L"OperationTimeoutMs", L"5000"), 100, 30000, cfg.discord.timeoutMs);
    cfg.caps.actionsEnabled = get(L"CapsBlink", L"HardwareEnabled", L"0") == L"1";
    cfg.caps.target = get(L"CapsBlink", L"KeyboardTargetPath", L"\\Device\\KeyboardClass0");
    ParseInt(get(L"CapsBlink", L"BlinkIntervalMs", L"500"), 50, 86400000, cfg.caps.intervalMs);
    cfg.asus.hardwareEnabled = get(L"AsusBlink", L"HardwareEnabled", L"0") == L"1";
    cfg.asus.readDiskActivity = get(L"AsusBlink", L"ReadDiskActivity", L"0") == L"1";
    cfg.asus.paused = get(L"AsusBlink", L"Paused", L"0") == L"1";
    cfg.asus.profilePath = get(L"AsusBlink", L"ProfilePath", L"");
    for (std::size_t option = 0; option < 8; ++option)
        cfg.asus.settings[AsusPatternKeys[option]] = get(L"AsusBlink", AsusPatternKeys[option], AsusPatternDefaults[option]);
    if (cfg.asus.profilePath.empty()) {
        aip::asus::Configuration parsed;
        std::wstring issue;
        if (!aip::asus::ParseConfiguration(cfg.asus.settings, parsed, issue)) { if (valid) error = issue; valid = false; }
    }
    if (!valid) return false; // failed reload leaves the caller's last configuration intact
    error.clear(); result = std::move(cfg); return true;
}
inline void AppendBounded(std::wstring& destination, const std::wstring& source) {
    if (source.empty() || destination.size() >= MaxText) return;
    if (!destination.empty()) destination += L"\n";
    destination.append(source, 0, MaxText - destination.size());
    // Avoid splitting a UTF-16 pair on Windows; harmless for UTF-32 wchar_t.
    if (!destination.empty() && destination.back() >= 0xD800 && destination.back() <= 0xDBFF) destination.pop_back();
}
inline void Overlay(Text& destination, const Text& source) {
    AppendBounded(destination.primary, source.primary);
    AppendBounded(destination.secondary, source.secondary);
    AppendBounded(destination.badge, source.badge);
}
// Relative timer avoids wall-clock adjustments, multiplication overflow and
// bursts of catch-up rendering after resume. A stalled host advances once.
class Cycle {
    std::vector<std::size_t> enabled_;
    std::size_t position_ = 0;
    std::uint64_t last_ = 0;
    bool initialized_ = false, cycling_ = false;
    int seconds_ = 0;
public:
    std::size_t Select(const Configuration& cfg, std::uint64_t now) {
        std::vector<std::size_t> enabled;
        for (std::size_t i = 0; i < cfg.entries.size(); ++i) if (cfg.entries[i].enabled) enabled.push_back(i);
        if (!initialized_ || enabled != enabled_ || cycling_ != cfg.cycle || seconds_ != cfg.cycleSeconds || now < last_) {
            enabled_ = std::move(enabled); position_ = 0; last_ = now; initialized_ = true;
            cycling_ = cfg.cycle; seconds_ = cfg.cycleSeconds;
        } else if (cfg.cycle && enabled_.size() > 1 && now - last_ >= static_cast<std::uint64_t>(cfg.cycleSeconds) * 1000) {
            position_ = (position_ + 1) % enabled_.size(); last_ = now;
        }
        return enabled_.empty() ? static_cast<std::size_t>(-1) : enabled_[position_];
    }
};
inline std::wstring TimingWarning(const Configuration& cfg, bool nativeLiveTile) {
    const auto count = std::count_if(cfg.entries.begin(), cfg.entries.end(), [](const Entry& e) { return e.enabled; });
    if (!cfg.cycle || count < 2) return {};
    if (!nativeLiveTile) return L"Cycling in registration mode can repeatedly render assets and register the package, causing CPU and disk spikes. Cached images do not eliminate registration cost.";
    if (cfg.cycleSeconds < 10) return L"Windows controls visible Live Tile timing. Cycles below 10 seconds may be skipped. 10 seconds is an advisory threshold, not a guaranteed Windows minimum.";
    return {};
}
inline std::wstring TimingChangeWarning(const Configuration& before, const Configuration& after, bool nativeLiveTile) {
    if (!after.enabled) return {};
    auto warning = TimingWarning(after, nativeLiveTile);
    if (warning.empty()) return {};
    if (!before.enabled || warning != TimingWarning(before, nativeLiveTile) || after.cycleSeconds < before.cycleSeconds)
        return warning;
    return {}; // Editing text/name does not require approving the same risk again.
}
} }
