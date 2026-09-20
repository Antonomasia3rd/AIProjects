#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace aip { namespace asus {
enum class Device { Mic, Keyboard };
enum class ErrorAction { Continue, Pause, Stop, Fault };
struct Event {
    std::wstring name;
    Device device = Device::Keyboard;
    bool hddActivity = false;
    int priority = 0;
    std::vector<int> states;
    std::vector<std::uint64_t> intervalsMs{0};
    std::int64_t durationMs = -1; // -1=one cycle, 0=infinite, positive=active duration
};
struct Configuration {
    std::vector<Event> events;
    int retries = 3;
    ErrorAction errorAction = ErrorAction::Continue;
};
struct Action { Device device; int state; std::wstring eventName; };
struct PatternSnapshot {
    int micState = -1, keyboardState = -1, diskLevel = 0;
    std::wstring micEvent, keyboardEvent;
    bool paused = false, complete = true;
};
inline std::wstring Trim(std::wstring value) {
    auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t c) { return std::iswspace(c) != 0; });
    auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t c) { return std::iswspace(c) != 0; }).base();
    return first < last ? std::wstring(first, last) : L"";
}
inline std::wstring Lower(std::wstring value) { for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c)); return value; }
inline bool ParseTime(const std::wstring& raw, std::uint64_t& result) {
    auto text = Lower(Trim(raw)); if (text.empty()) return false;
    long double factor = 1;
    if (text.size() > 2 && text.substr(text.size() - 2) == L"ms") text.resize(text.size() - 2);
    else if (std::iswalpha(text.back())) {
        switch (text.back()) { case L's': factor = 1000; break; case L'm': factor = 60000; break;
        case L'h': factor = 3600000; break; case L'd': factor = 86400000; break; default: return false; }
        text.pop_back();
    }
    try {
        std::size_t consumed = 0; long double value = std::stold(text, &consumed) * factor;
        if (consumed != text.size() || !std::isfinite(value) || value < 0 || value >= 9223372036854775808.0L || std::floor(value) != value) return false;
        result = static_cast<std::uint64_t>(value); return true;
    } catch (...) { return false; }
}
inline std::vector<std::wstring> Split(const std::wstring& raw) {
    std::vector<std::wstring> out; std::size_t start = 0;
    while (start <= raw.size()) {
        const auto end = raw.find(L',', start); auto value = Trim(raw.substr(start, end == std::wstring::npos ? end : end - start));
        if (!value.empty()) out.push_back(std::move(value));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return out;
}
inline bool ParseEvent(Event& result, const std::wstring& states, const std::wstring& intervals,
                       const std::wstring& duration, std::wstring& error) {
    Event event = result; event.states.clear(); event.intervalsMs.clear();
    if (Lower(Trim(states)) == L"off") { result = std::move(event); error.clear(); return true; }
    const int maximum = event.device == Device::Mic ? 1 : 255;
    try {
        for (const auto& raw : Split(states)) {
            std::size_t consumed = 0; int state = std::stoi(raw, &consumed);
            if (consumed != raw.size() || state < 0 || state > maximum) throw 0;
            event.states.push_back(state);
        }
        if (event.states.empty()) throw 0;
    } catch (...) { error = L"State list must contain integers in the device's supported range."; return false; }
    for (const auto& raw : Split(intervals)) {
        std::uint64_t value = 0;
        if (!ParseTime(raw, value)) { error = L"Intervals must resolve to whole nonnegative milliseconds."; return false; }
        event.intervalsMs.push_back(value);
    }
    if (event.intervalsMs.empty()) event.intervalsMs.push_back(0);
    if (Lower(Trim(duration)) == L"once") event.durationMs = -1;
    else {
        std::uint64_t value = 0;
        if (!ParseTime(duration, value)) { error = L"Duration must be once or a whole nonnegative time."; return false; }
        event.durationMs = static_cast<std::int64_t>(value);
    }
    error.clear(); result = std::move(event); return true;
}
inline bool Validate(const Configuration& cfg, std::wstring& error) {
    if (cfg.retries < 0 || cfg.retries > 100) { error = L"Retries must be 0 through 100."; return false; }
    for (const auto& event : cfg.events) {
        if (event.device != Device::Mic && event.device != Device::Keyboard) { error = L"Invalid output device."; return false; }
        if (event.durationMs < -1 || (event.hddActivity && event.device != Device::Keyboard)) { error = L"Invalid event duration or HDD target."; return false; }
        for (int state : event.states) if (state < 0 || state > (event.device == Device::Mic ? 1 : 255)) { error = L"Invalid device state."; return false; }
        for (auto interval : event.intervalsMs) if (interval > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) { error = L"Interval is too large."; return false; }
    }
    error.clear(); return true;
}
// Reader-independent adapter for an existing asusblink [Settings] dictionary.
// Host-only tray/log/Startup preferences are recognized but never acted on.
inline bool ParseConfiguration(const std::map<std::wstring, std::wstring>& settings, Configuration& result, std::wstring& error) {
    std::map<std::wstring, std::wstring> values;
    const std::wregex dynamic(L"^event([0-9]{1,3})(mic|keyboard|hdd)(state|interval|duration)$");
    const std::vector<std::wstring> fixed = {L"micstate", L"micinterval", L"micduration", L"keyboardstate", L"keyboardinterval", L"keyboardduration",
        L"errorretry", L"erroraction", L"errorlog", L"runatstartup", L"showtrayicon", L"showmenuasdropdown"};
    for (const auto& setting : settings) {
        auto key = Lower(Trim(setting.first)); key.erase(std::remove_if(key.begin(), key.end(), [](wchar_t c) { return c == L'-' || c == L'_'; }), key.end());
        if (key == L"showtray") key = L"showtrayicon";
        if (key == L"runstartup") key = L"runatstartup";
        if (std::find(fixed.begin(), fixed.end(), key) == fixed.end() && !std::regex_match(key, dynamic)) { error = L"Unknown ASUS setting: " + setting.first; return false; }
        if (!values.emplace(key, setting.second).second) { error = L"Duplicate ASUS setting alias: " + setting.first; return false; }
        if (key.size() >= 8 && key.substr(key.size() - 8) == L"interval") {
            const auto parts = Split(setting.second); std::uint64_t ignored = 0;
            if (parts.empty() || std::any_of(parts.begin(), parts.end(), [&](const std::wstring& part) { return !ParseTime(part, ignored); })) { error = L"Invalid interval: " + setting.first; return false; }
        }
        if (key.size() >= 8 && key.substr(key.size() - 8) == L"duration" && Lower(Trim(setting.second)) != L"once") {
            std::uint64_t ignored = 0;
            if (!ParseTime(setting.second, ignored)) { error = L"Invalid duration: " + setting.first; return false; }
        }
    }
    const auto read = [&](const std::wstring& key, const wchar_t* fallback) { auto it = values.find(key); return it == values.end() ? std::wstring(fallback) : it->second; };
    Configuration cfg;
    try { std::size_t end = 0; auto retry = Trim(read(L"errorretry", L"3")); cfg.retries = std::stoi(retry, &end); if (end != retry.size()) throw 0; }
    catch (...) { error = L"ErrorRetry must be an integer from 0 to 100."; return false; }
    bool pause = false, stop = false, fault = false;
    auto actions = Split(Lower(read(L"erroraction", L"continue")));
    if (actions.empty()) { error = L"ErrorAction cannot be empty."; return false; }
    for (const auto& action : actions) {
        if (action == L"pause") pause = true; else if (action == L"exit") stop = true; else if (action == L"crash") fault = true;
        else if (action != L"log" && action != L"continue") { error = L"Invalid ErrorAction."; return false; }
    }
    cfg.errorAction = stop ? ErrorAction::Stop : fault ? ErrorAction::Fault : pause ? ErrorAction::Pause : ErrorAction::Continue;
    const auto add = [&](const std::wstring& prefix, Device device, bool hdd, int priority, const std::wstring& name) {
        Event event; event.name = name; event.device = device; event.hddActivity = hdd; event.priority = priority;
        if (!ParseEvent(event, read(prefix + L"state", L"off"), read(prefix + L"interval", L"0"), read(prefix + L"duration", L"once"), error)) return false;
        if (!event.states.empty()) cfg.events.push_back(std::move(event));
        return true;
    };
    if (!add(L"mic", Device::Mic, false, 0, L"mic") || !add(L"keyboard", Device::Keyboard, false, 0, L"keyboard")) return false;
    std::map<std::wstring, std::pair<int, std::wstring>> eventPrefixes;
    for (const auto& setting : values) {
        std::wsmatch match;
        if (std::regex_match(setting.first, match, dynamic)) {
            auto number = match[1].str(), device = match[2].str();
            eventPrefixes[L"event" + number + device] = {std::stoi(number), device};
        }
    }
    for (const auto& entry : eventPrefixes)
        if (!add(entry.first, entry.second.second == L"mic" ? Device::Mic : Device::Keyboard,
            entry.second.second == L"hdd", entry.second.first, entry.first.substr(0, entry.first.size() - entry.second.second.size()))) return false;
    if (!Validate(cfg, error)) return false;
    result = std::move(cfg); return true;
}
inline int DiskLevel(std::uint64_t bytesPerSecond) {
    return bytesPerSecond == 0 ? 0 : bytesPerSecond <= 100 * 1024 ? 1 :
        bytesPerSecond <= 1024 * 1024 ? 2 : bytesPerSecond <= 10 * 1024 * 1024 ? 3 : 4;
}
inline std::uint64_t AddTime(std::uint64_t a, std::uint64_t b) {
    return b > std::numeric_limits<std::uint64_t>::max() - a ? std::numeric_limits<std::uint64_t>::max() : a + b;
}
class PatternEngine {
    struct Track { std::vector<Event> events; std::size_t event = 0, state = 0; std::uint64_t started = 0, next = 0; bool begun = false, held = false, finished = false; };
    std::array<Track, 2> tracks_;
    std::uint64_t active_ = 0, last_ = 0;
    bool initialized_ = false, wasPaused_ = false;
    PatternSnapshot snapshot_;
public:
    bool Reset(const Configuration& cfg, std::wstring& error) {
        if (!Validate(cfg, error)) return false;
        std::array<Track, 2> next;
        for (const auto& event : cfg.events) if (!event.states.empty()) next[event.device == Device::Mic ? 0 : 1].events.push_back(event);
        for (auto& track : next) std::stable_sort(track.events.begin(), track.events.end(), [](const Event& a, const Event& b) { return a.priority < b.priority; });
        tracks_ = std::move(next); active_ = last_ = 0; initialized_ = wasPaused_ = false; snapshot_ = {}; return true;
    }
    std::vector<Action> Step(std::uint64_t now, bool paused = false, std::uint64_t diskBytesPerSecond = 0) {
        if (initialized_ && now >= last_ && !wasPaused_) active_ = AddTime(active_, now - last_);
        initialized_ = true; last_ = now; wasPaused_ = paused; snapshot_.paused = paused;
        snapshot_.diskLevel = DiskLevel(diskBytesPerSecond);
        std::vector<Action> actions;
        for (std::size_t device = 0; device < tracks_.size(); ++device) {
            auto& track = tracks_[device];
            // Bound instantaneous zero-delay sequences without losing order.
            for (int work = 0; !paused && work < 64 && track.event < track.events.size(); ++work) {
                const auto& event = track.events[track.event];
                if (!track.begun) { track.begun = true; track.started = active_; track.next = active_; track.state = 0; track.held = track.finished = false; }
                if (active_ < track.next) break;
                bool done = track.finished || (event.durationMs == -1
                    ? track.state >= (event.hddActivity ? 1u : event.states.size())
                    : !event.hddActivity && event.durationMs > 0 && track.state > 0 && active_ - track.started >= static_cast<std::uint64_t>(event.durationMs));
                if (done) { ++track.event; track.begun = false; continue; }
                const auto index = event.hddActivity ? std::min(static_cast<std::size_t>(snapshot_.diskLevel), event.states.size() - 1)
                    : track.state % event.states.size();
                const int state = event.states[index];
                actions.push_back({event.device, state, event.name});
                if (device == 0) snapshot_.micState = state; else snapshot_.keyboardState = state;
                std::uint64_t delay = event.intervalsMs.empty() ? 0 : event.intervalsMs[(event.hddActivity ? 0 : track.state) % event.intervalsMs.size()];
                if (event.hddActivity) delay = event.durationMs == -1 ? 0 : (delay == 0 ? 500 : delay);
                else if (event.durationMs == 0 && delay == 0) { track.held = true; delay = 200; }
                else if (event.durationMs > 0 && delay == 0) delay = 200;
                if (!track.held) ++track.state;
                if (event.hddActivity && event.durationMs > 0 && active_ - track.started >= static_cast<std::uint64_t>(event.durationMs)) { track.finished = true; delay = 0; }
                track.next = AddTime(active_, delay);
                if (delay > 0) break;
            }
            auto name = track.event < track.events.size() ? track.events[track.event].name : L"";
            if (device == 0) snapshot_.micEvent = name; else snapshot_.keyboardEvent = name;
        }
        snapshot_.complete = tracks_[0].event >= tracks_[0].events.size() && tracks_[1].event >= tracks_[1].events.size();
        return actions;
    }
    PatternSnapshot Read() const { return snapshot_; }
};
} }
