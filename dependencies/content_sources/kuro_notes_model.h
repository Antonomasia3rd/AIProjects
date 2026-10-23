#pragma once
// Portable WuWa account and output model. Protocol parsing/transport lives in
// kuro_notes.inc; this file never opens an account, browser, or network.
#include <algorithm>
#include <climits>
#include <cwctype>
#include <string>
#include <vector>

namespace aip { namespace kuro {
enum class State { Loading, Ok, Full, Error };
struct Configuration {
    std::wstring gameToken, gameTokenProtected, playerId, region, deviceId;
    int refreshSeconds = 300, timeoutSeconds = 15, maximumBytes = 1048576;
};
struct Role { std::wstring playerId, region, name; int level = 0; };
struct Row { std::wstring label, value, iconTopic; };
struct Snapshot {
    State state = State::Loading;
    int current = 0, maximum = 0, recoverySeconds = -1;
    int refined = -1, refinedMaximum = -1, refinedRecoverySeconds = -1;
    int activity = -1, activityMaximum = -1, weeklyChallengesRemaining = -1;
    bool activityUnlocked = true;
    std::wstring playerId, region, playerName;
    std::wstring line1 = L"Wuthering Waves", line2 = L"Loading game notes...";
    std::vector<Row> rows;
};
inline std::wstring Trim(std::wstring value) {
    const auto begin = value.find_first_not_of(L" \t\r\n");
    return begin == std::wstring::npos ? L"" : value.substr(begin, value.find_last_not_of(L" \t\r\n") - begin + 1);
}
inline std::wstring Lower(std::wstring value) { for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c)); return value; }
inline bool SafeText(const std::wstring& value, std::size_t maximum) {
    return value.size() <= maximum && std::none_of(value.begin(), value.end(), [](wchar_t c) { return c < 0x20 || c == 0x7f; });
}
inline bool ParseBoundedInteger(const std::wstring& value, int minimum, int maximum, int& result) {
    if (value.empty()) return false;
    unsigned number = 0;
    for (const auto c : value) {
        if (c < L'0' || c > L'9' || number > static_cast<unsigned>(maximum) / 10 ||
            (number == static_cast<unsigned>(maximum) / 10 && static_cast<unsigned>(c - L'0') > static_cast<unsigned>(maximum) % 10)) return false;
        number = number * 10 + static_cast<unsigned>(c - L'0');
    }
    result = static_cast<int>(number); return result >= minimum;
}
inline bool NormalizeSetting(const std::wstring& rawKey, std::wstring& value, std::wstring& error) {
    const auto key = Lower(rawKey); error.clear();
    if (key == L"gametoken" || key == L"gametokenprotected") {
        if (!SafeText(value, 16384) || value.find(L' ') != std::wstring::npos) { error = L"The Kuro game access token must be a bounded single-line value (not shown)."; return false; }
        return true;
    }
    value = Trim(value);
    if (key == L"playerid" || key == L"uid") {
        if (value.empty()) return true;
        if (value.size() > 20 || value.front() == L'0' || !std::all_of(value.begin(), value.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
            error = L"Enter the Wuthering Waves player ID, or leave it empty to discover a single linked role."; return false;
        }
        return true;
    }
    if (key == L"region" || key == L"server") {
        // These are launcher queryPlayerInfo dictionary keys, not the gacha
        // API's unrelated hashed server IDs. Discovery supplies exact keys.
        if (!SafeText(value, 64)) { error = L"Use the region name returned by Wuthering Waves role discovery."; return false; }
        return true;
    }
    if (key == L"deviceid") {
        if (!SafeText(value, 128)) { error = L"The Kuro device ID must be a bounded single-line value."; return false; }
        return true;
    }
    int number = 0;
    const bool refresh = key == L"refreshseconds", timeout = key == L"timeoutseconds", bytes = key == L"maximumbytes";
    if (refresh || timeout || bytes) {
        const int low = bytes ? 1024 : (refresh ? 30 : 1), high = bytes ? 16777216 : (refresh ? 86400 : 120);
        if (!ParseBoundedInteger(value, low, high, number)) { error = L"The Kuro refresh, timeout, or response limit is outside its supported range."; return false; }
        value = std::to_wstring(number); return true;
    }
    error = L"Unknown Wuthering Waves account setting."; return false;
}
inline std::wstring ConfigurationKey(const Configuration& cfg) {
    // Internal revision key only. Callers must never log this value.
    return cfg.gameToken + L"\n" + cfg.gameTokenProtected + L"\n" + cfg.playerId + L"\n" + cfg.region + L"\n" + cfg.deviceId +
        L"\n" + std::to_wstring(cfg.refreshSeconds) + L":" + std::to_wstring(cfg.timeoutSeconds) + L":" + std::to_wstring(cfg.maximumBytes);
}
inline Snapshot MakeError(const std::wstring& title, const std::wstring& description) {
    Snapshot result; result.state = State::Error; result.line1 = title; result.line2 = description; return result;
}
inline std::wstring Count(int current, int maximum) { return std::to_wstring(current) + L"/" + std::to_wstring(maximum); }
inline std::wstring Recovery(int seconds) {
    if (seconds < 0) return L"Recovery unavailable";
    if (seconds == 0) return L"Full";
    const int minutes = static_cast<int>((static_cast<long long>(seconds) + 59) / 60);
    return minutes >= 60 ? std::to_wstring(minutes / 60) + L"h " + std::to_wstring(minutes % 60) + L"m" : std::to_wstring(minutes) + L"m";
}
inline void ComposeRows(Snapshot& result) {
    result.rows.clear();
    result.line1 = L"Waveplates " + Count(result.current, result.maximum);
    result.line2 = result.current >= result.maximum ? L"Full" : Recovery(result.recoverySeconds);
    result.rows.push_back({L"Waveplates", Count(result.current, result.maximum) + L"\n" + result.line2, L"waveplates"});
    if (result.refined >= 0 && result.refinedMaximum > 0)
        result.rows.push_back({L"Refined waveplates", Count(result.refined, result.refinedMaximum), L"refined-waveplates"});
    if (result.activity >= 0 && (result.activityMaximum > 0 || !result.activityUnlocked))
        result.rows.push_back({L"Daily activity", result.activityUnlocked ? Count(result.activity, result.activityMaximum) : L"Not unlocked", L"activity"});
    if (result.weeklyChallengesRemaining >= 0)
        result.rows.push_back({L"Weekly challenges remaining", std::to_wstring(result.weeklyChallengesRemaining), L"weekly-challenge"});
}
} }
