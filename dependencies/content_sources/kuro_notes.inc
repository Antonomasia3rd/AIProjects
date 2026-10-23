#pragma once
// Read-only overseas Wuthering Waves launcher adapter. Protocol field names and
// request flow were verified against Wuthery/kuro.py at
// 4be620b2fbdc05eb2e2b6bb3aba68ae348d27c22 (2026-10-23).
// This is an independent implementation; no upstream Python is embedded.
#include "kuro_notes_model.h"
#include "notes.inc"
#include "../dpapi.inc"
#include <set>

namespace aip { namespace kuro {
using HttpTransport = std::function<bool(const aip::HttpFetchOptions&, aip::HttpFetchResponse&, std::wstring&)>;
using CredentialDecoder = std::function<bool(const std::wstring&, std::wstring&)>;
using Cancel = std::function<bool()>;
namespace detail {
using Members = std::map<std::string, std::string>;
inline bool Object(const std::string& json, Members& members) {
    members.clear();
    if (!aip::notes::detail::ValidDocument(json)) return false;
    std::size_t position = 0; aip::notes::detail::SkipWhitespace(json, position); ++position;
    for (;;) {
        aip::notes::detail::SkipWhitespace(json, position);
        if (position >= json.size()) return false;
        if (json[position] == '}') return true;
        const auto keyEnd = aip::JsonStringEnd(json, position);
        std::string key;
        if (keyEnd == std::string::npos || !aip::DecodeJsonStringUtf8Range(json, position, keyEnd, key)) return false;
        position = keyEnd; aip::notes::detail::SkipWhitespace(json, position);
        if (position >= json.size() || json[position++] != ':') return false;
        aip::notes::detail::SkipWhitespace(json, position);
        const auto end = aip::JsonValueEnd(json, position);
        if (end == std::string::npos || !members.emplace(key, json.substr(position, end - position)).second) return false;
        position = end; aip::notes::detail::SkipWhitespace(json, position);
        if (position < json.size() && json[position] == ',') ++position;
    }
}
inline bool String(const std::string& raw, std::string& value) {
    return !raw.empty() && aip::DecodeJsonStringUtf8Range(raw, 0, raw.size(), value);
}
inline bool Wide(const std::string& raw, std::wstring& value) {
    std::string decoded; return String(raw, decoded) && aip::TryUtf8ToWide(decoded, value);
}
inline bool Int64(std::string raw, std::int64_t& value) {
    if (!raw.empty() && raw.front() == '"') { std::string decoded; if (!String(raw, decoded)) return false; raw = std::move(decoded); }
    if (raw.empty()) return false;
    std::uint64_t number = 0;
    for (const auto c : raw) {
        if (c < '0' || c > '9' || number > static_cast<std::uint64_t>(INT64_MAX) / 10 ||
            (number == static_cast<std::uint64_t>(INT64_MAX) / 10 && static_cast<unsigned>(c - '0') > static_cast<std::uint64_t>(INT64_MAX) % 10)) return false;
        number = number * 10 + static_cast<unsigned>(c - '0');
    }
    value = static_cast<std::int64_t>(number); return true;
}
inline bool Number(const Members& object, const char* key, int& value, int minimum = 0, int maximum = 1000000000) {
    const auto found = object.find(key); std::int64_t number = 0;
    if (found == object.end() || !Int64(found->second, number) || number < minimum || number > maximum) return false;
    value = static_cast<int>(number); return true;
}
inline bool Identifier(const Members& object, const char* key, std::wstring& value) {
    const auto found = object.find(key); if (found == object.end()) return false;
    std::string raw = found->second;
    if (!raw.empty() && raw.front() == '"') { std::string decoded; if (!String(raw, decoded)) return false; raw = std::move(decoded); }
    value.assign(raw.begin(), raw.end()); std::wstring error;
    return !value.empty() && NormalizeSetting(L"PlayerId", value, error);
}
inline bool ApiStatus(const Members& object, const char* key, int& status) {
    const auto found = object.find(key); if (found == object.end()) return false;
    if (found->second == "null") { if (std::string(key) != "codes") return false; status = 0; return true; }
    return aip::notes::detail::ParseAsciiInt(found->second, status);
}
inline bool UnwrapStringObject(const std::string& raw, Members& object) {
    std::string decoded; return String(raw, decoded) && decoded.size() <= 16777216 && Object(decoded, object);
}
inline std::string FormValue(const std::wstring& text) {
    const auto bytes = aip::WideToUtf8(text); static const char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char c : bytes) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') result.push_back(static_cast<char>(c));
        else { result.push_back('%'); result.push_back(hex[c >> 4]); result.push_back(hex[c & 15]); }
    }
    return result;
}
inline std::wstring NewDeviceId() {
    HCRYPTPROV provider = 0; BYTE bytes[16]{};
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return {};
    const bool ok = CryptGenRandom(provider, sizeof(bytes), bytes) != FALSE; CryptReleaseContext(provider, 0);
    if (!ok) return {};
    bytes[6] = static_cast<BYTE>((bytes[6] & 15) | 0x40); bytes[8] = static_cast<BYTE>((bytes[8] & 63) | 0x80);
    static const wchar_t hex[] = L"0123456789ABCDEF"; std::wstring value;
    for (int i = 0; i < 16; ++i) { if (i == 4 || i == 6 || i == 8 || i == 10) value += L'-'; value += hex[bytes[i] >> 4]; value += hex[bytes[i] & 15]; }
    return value;
}
inline bool DecodeNativeCredential(const std::wstring& protectedValue, std::wstring& value) {
    try { value = aip::UnprotectSecretForCurrentUser(protectedValue, aip::DpapiLegacyEncoding::Utf8); return true; }
    catch (...) { value.clear(); return false; }
}
inline bool Resolve(const Configuration& input, Configuration& cfg, std::wstring& error, const CredentialDecoder& decoder) {
    cfg = input; error.clear();
    for (auto field : {std::make_pair(L"GameToken", &cfg.gameToken), {L"GameTokenProtected", &cfg.gameTokenProtected},
        {L"PlayerId", &cfg.playerId}, {L"Region", &cfg.region}, {L"DeviceId", &cfg.deviceId}})
        if (!NormalizeSetting(field.first, *field.second, error)) return false;
    for (const auto field : {std::make_pair(L"RefreshSeconds", cfg.refreshSeconds), {L"TimeoutSeconds", cfg.timeoutSeconds}, {L"MaximumBytes", cfg.maximumBytes}}) {
        auto value = std::to_wstring(field.second); if (!NormalizeSetting(field.first, value, error)) return false;
    }
    if (!cfg.gameTokenProtected.empty()) {
        bool decoded = false;
        try { decoded = decoder && decoder(cfg.gameTokenProtected, cfg.gameToken); } catch (...) {}
        if (!decoded) { error = L"The protected Kuro game token could not be opened for this Windows user."; return false; }
        if (!NormalizeSetting(L"GameToken", cfg.gameToken, error)) return false;
    }
    if (cfg.gameToken.empty()) { error = L"Configure a Wuthering Waves overseas game access token. HoYoLAB cookies and KuroBBS web tokens are different credentials."; return false; }
    if (cfg.deviceId.empty()) cfg.deviceId = NewDeviceId();
    if (cfg.deviceId.empty()) { error = L"A Kuro request device identity could not be created."; return false; }
    return true;
}
inline std::int64_t DaysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2; const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned adjusted = month > 2 ? month - 3 : month + 9;
    const unsigned doy = (153 * adjusted + 2) / 5 + day - 1;
    return static_cast<std::int64_t>(era) * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
inline bool Timestamp(const std::string& raw, std::int64_t& epoch) {
    if (Int64(raw, epoch)) { if (epoch > 20000000000LL) epoch /= 1000; return epoch <= 253402300799LL; }
    std::string value; if (!String(raw, value) || value.size() < 20 || value.size() > 40) return false;
    const auto digits = [&](std::size_t p, std::size_t count, int& out) {
        out = 0; if (p + count > value.size()) return false;
        for (std::size_t i = 0; i < count; ++i) { const auto c = value[p + i]; if (c < '0' || c > '9') return false; out = out * 10 + c - '0'; } return true;
    };
    int year, month, day, hour, minute, second;
    if (!digits(0, 4, year) || !digits(5, 2, month) || !digits(8, 2, day) || !digits(11, 2, hour) || !digits(14, 2, minute) || !digits(17, 2, second) ||
        value[4] != '-' || value[7] != '-' || (value[10] != 'T' && value[10] != ' ') || value[13] != ':' || value[16] != ':' ||
        year < 1970 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) return false;
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const int maxDay = days[month - 1] + (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0);
    if (day < 1 || day > maxDay) return false;
    std::size_t p = 19;
    if (p < value.size() && value[p] == '.') { const auto begin = ++p; while (p < value.size() && value[p] >= '0' && value[p] <= '9') ++p; if (p == begin) return false; }
    int offset = 0;
    if (p < value.size() && value[p] == 'Z') ++p;
    else if (p < value.size() && (value[p] == '+' || value[p] == '-')) {
        const int sign = value[p++] == '+' ? 1 : -1; int oh, om;
        if (!digits(p, 2, oh) || p + 2 >= value.size() || value[p + 2] != ':' || !digits(p + 3, 2, om) || oh > 23 || om > 59) return false;
        offset = sign * (oh * 3600 + om * 60); p += 5;
    } else return false; // Do not guess a timezone for naive timestamps.
    if (p != value.size()) return false;
    epoch = DaysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 + minute * 60 + second - offset;
    return epoch >= 0;
}
inline bool RecoverySeconds(const Members& object, const char* key, std::int64_t now, int& seconds) {
    const auto found = object.find(key); if (found == object.end()) { seconds = -1; return true; }
    std::int64_t epoch = 0; if (!Timestamp(found->second, epoch)) return false;
    const auto delta = epoch > now ? epoch - now : 0;
    seconds = static_cast<int>((std::min)(delta, static_cast<std::int64_t>(INT_MAX) - 59)); return true;
}
struct Session {
    Configuration cfg; HttpTransport transport; Cancel canceled;
    std::chrono::steady_clock::time_point deadline;
    bool Request(const wchar_t* url, const std::string& body, bool form, Members& response, std::wstring& error) const {
        if ((canceled && canceled()) || std::chrono::steady_clock::now() >= deadline) { error = L"The Kuro request was canceled or exceeded its total timeout."; return false; }
        aip::HttpFetchOptions options; options.url = url; options.userAgent = L"DesktopStub";
        options.accept = L"application/json"; options.method = L"POST"; options.body = body; options.followRedirects = false;
        options.headers.push_back({L"Content-Type", form ? L"application/x-www-form-urlencoded" : L"application/json"});
        options.timeout = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (options.timeout.count() < 1) { error = L"The Kuro request exceeded its total timeout."; return false; }
        options.maximumBytes = static_cast<std::size_t>(cfg.maximumBytes); options.canceled = canceled;
        aip::HttpFetchResponse result; std::wstring transportError;
        bool ok = false;
        try { ok = transport && transport(options, result, transportError); } catch (...) {}
        // Never expose upstream error/message strings: they can echo tokens,
        // OAuth codes, player IDs, or request bodies.
        if (!ok || result.statusCode < 200 || result.statusCode >= 300) { error = L"The Kuro service could not be reached or rejected the request."; return false; }
        if ((canceled && canceled()) || std::chrono::steady_clock::now() >= deadline) { error = L"The Kuro request was canceled or exceeded its total timeout."; return false; }
        if (result.body.size() > static_cast<std::size_t>(cfg.maximumBytes) || !Object(std::string(result.body.begin(), result.body.end()), response)) { error = L"The Kuro service returned invalid or oversized JSON."; return false; }
        return true;
    }
    bool OAuth(std::wstring& code, std::wstring& error) const {
        const std::string body = "client_id=7rxmydkibzzsf12om5asjnoo&client_secret=32gh5r0p35ullmxrzzwk40ly&productId=A1725&projectId=G153&redirect_uri=1&scope=launcher&deviceNum=" +
            FormValue(cfg.deviceId) + "&access_token=" + FormValue(cfg.gameToken);
        Members response; int status = -1;
        if (!Request(L"https://sdkapi.kurogame-service.com/sdkcom/v2/user/oauth/code/generate.lg", body, true, response, error)) return false;
        if (!ApiStatus(response, "codes", status) || status != 0 || !Wide(response["oauthCode"], code) || code.empty() || !SafeText(code, 16384)) {
            error = L"The Kuro game session expired or was rejected. Supply a current overseas game access token."; return false;
        }
        return true;
    }
    bool Query(const wchar_t* url, const std::string& body, Members& response, std::wstring& error) const {
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (!Request(url, body, false, response, error)) return false;
            int status = -1;
            if (!ApiStatus(response, "code", status)) { error = L"The Kuro launcher response has no valid status."; return false; }
            if (status == 0) return true;
            if (status != 1005) { error = L"The Kuro launcher rejected the role request. Check the game token, Wuthering Waves player ID, and region."; return false; }
        }
        error = L"The Kuro launcher role data is temporarily unavailable. Retry at the next refresh."; return false;
    }
};
inline bool ParseRoles(const Members& response, std::vector<Role>& roles, std::wstring& error) {
    roles.clear(); Members data; const auto found = response.find("data");
    if (found == response.end() || !Object(found->second, data) || data.size() > 128) { error = L"The Kuro launcher returned invalid role discovery data."; return false; }
    for (const auto& field : data) {
        Role role; Members player;
        if (!aip::TryUtf8ToWide(field.first, role.region) || role.region.empty() || !SafeText(role.region, 64) || !UnwrapStringObject(field.second, player) ||
            !Identifier(player, "roleId", role.playerId) || !Wide(player["roleName"], role.name) || !SafeText(role.name, 256) || !Number(player, "level", role.level, 0)) {
            error = L"The Kuro launcher returned a malformed Wuthering Waves role."; roles.clear(); return false;
        }
        roles.push_back(std::move(role));
    }
    return true;
}
inline bool Discover(const Session& session, const std::wstring& code, std::vector<Role>& roles, std::wstring& error) {
    Members response;
    return session.Query(L"https://pc-launcher-sdk-api.kurogame.net/game/queryPlayerInfo", "{\"oauthCode\":" + aip::JsonEscape(code) + "}", response, error) && ParseRoles(response, roles, error);
}
} // namespace detail

inline Snapshot ParseRoleResponse(const std::string& body, const Role& role, std::int64_t now = static_cast<std::int64_t>(std::time(nullptr))) {
    using namespace detail; Members response, data, player, base; int status = -1;
    if (body.size() > 16777216 || !Object(body, response) || !ApiStatus(response, "code", status) || status != 0 || !Object(response["data"], data))
        return MakeError(L"Kuro data unavailable", L"The launcher response has no valid role data.");
    const auto selected = data.find(aip::WideToUtf8(role.region));
    if (selected == data.end() || !UnwrapStringObject(selected->second, player) || !Object(player["Base"], base))
        return MakeError(L"Kuro data unavailable", L"The selected Wuthering Waves region has no role details.");
    Snapshot result; result.region = role.region;
    if (!Identifier(base, "Id", result.playerId) || result.playerId != role.playerId || !Wide(base["Name"], result.playerName) || !SafeText(result.playerName, 256) ||
        !Number(base, "Energy", result.current) || !Number(base, "MaxEnergy", result.maximum, 1) ||
        !RecoverySeconds(base, "EnergyRecoverTime", now, result.recoverySeconds))
        return MakeError(L"Kuro data unavailable", L"The selected Wuthering Waves role or waveplate fields are invalid.");
    if (base.count("StoreEnergy") || base.count("MaxStoreEnergy")) {
        if (!Number(base, "StoreEnergy", result.refined) || !Number(base, "MaxStoreEnergy", result.refinedMaximum) ||
            (result.refinedMaximum == 0 && result.refined != 0) ||
            !RecoverySeconds(base, "StoreEnergyRecoverTime", now, result.refinedRecoverySeconds))
            return MakeError(L"Kuro data unavailable", L"The refined waveplate fields are invalid.");
    }
    if (base.count("Liveness") || base.count("LivenessMaxCount")) {
        if (!Number(base, "Liveness", result.activity) || !Number(base, "LivenessMaxCount", result.activityMaximum) ||
            (result.activityMaximum == 0 && result.activity != 0))
            return MakeError(L"Kuro data unavailable", L"The daily activity fields are invalid.");
        const auto unlocked = base.find("LivenessUnlock");
        if (unlocked != base.end()) {
            if (unlocked->second != "true" && unlocked->second != "false") return MakeError(L"Kuro data unavailable", L"The daily activity unlock field is invalid.");
            result.activityUnlocked = unlocked->second == "true";
        }
    }
    if (base.count("WeeklyInstCount") && !Number(base, "WeeklyInstCount", result.weeklyChallengesRemaining))
        return MakeError(L"Kuro data unavailable", L"The weekly challenge field is invalid.");
    result.state = result.current >= result.maximum ? State::Full : State::Ok;
    if (result.state == State::Full) result.recoverySeconds = 0;
    else if (result.recoverySeconds == 0) result.recoverySeconds = -1;
    ComposeRows(result); return result;
}
inline bool DiscoverRoles(const Configuration& input, std::vector<Role>& roles, std::wstring& error,
    const HttpTransport& transport = aip::FetchHttpBytes, const CredentialDecoder& decoder = detail::DecodeNativeCredential, const Cancel& canceled = {}) {
    roles.clear(); Configuration cfg;
    if (!detail::Resolve(input, cfg, error, decoder)) return false;
    detail::Session session{cfg, transport, canceled, std::chrono::steady_clock::now() + std::chrono::seconds(cfg.timeoutSeconds)};
    std::wstring code;
    return session.OAuth(code, error) && detail::Discover(session, code, roles, error);
}
inline Snapshot Fetch(const Configuration& input, const HttpTransport& transport = aip::FetchHttpBytes,
    const CredentialDecoder& decoder = detail::DecodeNativeCredential, const Cancel& canceled = {}) {
    Configuration cfg; std::wstring error;
    if (!detail::Resolve(input, cfg, error, decoder)) return MakeError(L"Wuthering Waves setup", error);
    detail::Session session{cfg, transport, canceled, std::chrono::steady_clock::now() + std::chrono::seconds(cfg.timeoutSeconds)};
    std::wstring code;
    if (!session.OAuth(code, error)) return MakeError(L"Kuro refresh failed", error);
    Role role{cfg.playerId, cfg.region, L"", 0};
    if (role.playerId.empty() || role.region.empty()) {
        std::vector<Role> roles;
        if (!detail::Discover(session, code, roles, error)) return MakeError(L"Kuro discovery failed", error);
        roles.erase(std::remove_if(roles.begin(), roles.end(), [&](const Role& candidate) {
            return (!role.playerId.empty() && candidate.playerId != role.playerId) || (!role.region.empty() && candidate.region != role.region);
        }), roles.end());
        if (roles.size() != 1) return MakeError(L"Select a Wuthering Waves role", roles.empty() ?
            L"No matching overseas role was found. Check the game account and region." : L"Several roles were found. Choose the Wuthering Waves player ID and region in this tile's settings.");
        role = roles.front();
    }
    detail::Members response;
    const std::string body = "{\"oauthCode\":" + aip::JsonEscape(code) + ",\"playerId\":" + aip::WideToUtf8(role.playerId) + ",\"region\":" + aip::JsonEscape(role.region) + "}";
    if (!session.Query(L"https://pc-launcher-sdk-api.kurogame.net/game/queryRole", body, response, error)) return MakeError(L"Kuro refresh failed", error);
    // Reconstitute the already validated object without logging any response.
    std::string document = "{"; bool first = true;
    for (const auto& field : response) { if (!first) document += ','; first = false; document += aip::JsonEscape(aip::Utf8ToWide(field.first)) + ":" + field.second; }
    return ParseRoleResponse(document + "}", role);
}
} }
