#ifndef AIP_CONTENT_SOURCES_NOTES_INC
#define AIP_CONTENT_SOURCES_NOTES_INC

// Shared Real-Time Notes data engine extracted from RealTimeNotesDeskband.
// Account discovery/storage and all UI/registration remain host concerns.
#include "../desktop_app_baseline.h"
#include "../http_fetch.inc"
#include <wincrypt.h>
#include <ctime>
#include <random>

namespace aip { namespace notes {
inline constexpr UINT kDefaultRefreshSeconds = 300;
enum class ResourceKind
{
    Resin,
    Stamina,
    Charge
};

enum class StateKind
{
    Loading,
    Ok,
    Full,
    Error
};

struct Snapshot
{
    int current = 0;
    int maximum = 0;
    int recoverySeconds = 0;
    ResourceKind resource = ResourceKind::Resin;
    StateKind state = StateKind::Loading;
    std::wstring line1 = L"Loading";
    std::wstring line2 = L"Real-Time Notes";
    std::wstring tooltip = L"Real-Time Notes";
    std::vector<std::wstring> menuLines;
    UINT refreshSeconds = kDefaultRefreshSeconds;
};

struct Account
{
    std::string uid;
    std::string ltoken;
    std::string ltuid;
    UINT refreshSeconds = kDefaultRefreshSeconds;
};

struct Configuration : Account
{
    ResourceKind resource = ResourceKind::Resin;
    std::chrono::milliseconds timeout{15000};
    size_t maximumBytes = 1024u * 1024u;
    std::function<bool()> canceled;
};
using HttpTransport = std::function<bool(const aip::HttpFetchOptions&, aip::HttpFetchResponse&, std::wstring&)>;
struct ResourceMetadata
{
    ResourceKind resource;
    const wchar_t* name;
    const wchar_t* displayName;
    const wchar_t* host;
    const wchar_t* path;
};
inline const ResourceMetadata& ResourceDetails(ResourceKind resource)
{
    static const ResourceMetadata resources[] = {
        {ResourceKind::Resin, L"resin", L"Resin", L"bbs-api-os.hoyolab.com", L"/game_record/genshin/api/dailyNote"},
        {ResourceKind::Stamina, L"stamina", L"Stamina", L"bbs-api-os.hoyolab.com", L"/game_record/hkrpg/api/note"},
        {ResourceKind::Charge, L"charge", L"Charge", L"sg-act-nap-api.hoyolab.com", L"/event/game_record_zzz/api/zzz/note"}
    };
    for (const auto& item : resources) if (item.resource == resource) return item;
    return resources[0];
}
inline const wchar_t* ResourceName(ResourceKind resource) { return ResourceDetails(resource).name; }
inline bool ParseResourceName(const std::wstring& value, ResourceKind& resource)
{
    const auto name = aip::ToLower(aip::Trim(value));
    if (name == L"resin" || name == L"genshin") resource = ResourceKind::Resin;
    else if (name == L"stamina" || name == L"hsr") resource = ResourceKind::Stamina;
    else if (name == L"charge" || name == L"zzz") resource = ResourceKind::Charge;
    else return false;
    return true;
}
inline UINT ClampRefresh(UINT refresh) { return (std::max)(30u, (std::min)(86400u, refresh)); }
inline Snapshot MakeError(ResourceKind resource, const std::wstring& primary,
    const std::wstring& detail, UINT refresh = kDefaultRefreshSeconds)
{
    Snapshot state;
    state.resource = resource;
    state.state = StateKind::Error;
    state.line1 = primary;
    state.line2 = detail;
    std::replace(state.line2.begin(), state.line2.end(), L'\0', L' ');
    if (state.line2.size() > 512)
    {
        size_t length = 512;
        if (state.line2[length - 1] >= 0xD800 && state.line2[length - 1] <= 0xDBFF &&
            state.line2[length] >= 0xDC00 && state.line2[length] <= 0xDFFF) --length;
        state.line2.resize(length);
    }
    state.tooltip = state.line1 + L" - " + state.line2;
    state.refreshSeconds = ClampRefresh(refresh);
    return state;
}
namespace detail {
inline void SkipWhitespace(const std::string& json, size_t& position)
{
    while (position < json.size() && (json[position] == ' ' || json[position] == '\t' ||
        json[position] == '\r' || json[position] == '\n')) ++position;
}
inline bool ValidateValue(const std::string& json, size_t& position, unsigned depth)
{
    SkipWhitespace(json, position);
    if (position >= json.size() || depth > 64) return false;
    const char opening = json[position];
    if (opening == '{' || opening == '[')
    {
        const char closing = opening == '{' ? '}' : ']';
        ++position;
        SkipWhitespace(json, position);
        if (position < json.size() && json[position] == closing) { ++position; return true; }
        for (;;)
        {
            if (opening == '{')
            {
                if (position >= json.size() || json[position] != '"') return false;
                position = aip::JsonStringEnd(json, position);
                if (position == std::string::npos) return false;
                SkipWhitespace(json, position);
                if (position >= json.size() || json[position++] != ':') return false;
            }
            if (!ValidateValue(json, position, depth + 1)) return false;
            SkipWhitespace(json, position);
            if (position >= json.size()) return false;
            if (json[position] == closing) { ++position; return true; }
            if (json[position++] != ',') return false;
            SkipWhitespace(json, position);
        }
    }
    const auto end = opening == '"' ? aip::JsonStringEnd(json, position) : aip::JsonPrimitiveValueEnd(json, position);
    if (end == std::string::npos) return false;
    position = end;
    return true;
}
inline bool ValidDocument(const std::string& json)
{
    std::wstring decoded;
    if (!aip::TryUtf8ToWide(json, decoded)) return false;
    size_t position = 0;
    SkipWhitespace(json, position);
    if (position >= json.size() || json[position] != '{' || !ValidateValue(json, position, 0)) return false;
    SkipWhitespace(json, position);
    return position == json.size();
}
inline std::wstring Utf8ToWide(const std::string& text)
{
    std::wstring result;
    aip::TryUtf8ToWide(text, result);
    return result;
}
inline bool JsonRange(const std::string& json, const std::string& key, size_t& begin, size_t& end)
{
    size_t keyPosition = 0;
    return aip::FindJsonFieldValue(json, key.c_str(), keyPosition, begin, end);
}
inline bool JsonString(const std::string& json, const std::string& key, std::string& value)
{
    size_t begin = 0, end = 0;
    return JsonRange(json, key, begin, end) && aip::DecodeJsonStringUtf8Range(json, begin, end, value);
}
inline bool ParseAsciiInt(const std::string& text, int& value)
{
    if (text.empty()) return false;
    size_t i = text.front() == '-' ? 1 : 0;
    if (i == text.size()) return false;
    for (; i < text.size(); ++i) if (text[i] < '0' || text[i] > '9') return false;
    errno = 0;
    char* end = nullptr;
    const auto parsed = strtoll(text.c_str(), &end, 10);
    if (end != text.c_str() + text.size() || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX) return false;
    value = static_cast<int>(parsed);
    return true;
}
inline bool JsonInt(const std::string& json, const std::string& key, int& value)
{
    size_t begin = 0, end = 0;
    if (!JsonRange(json, key, begin, end)) return false;
    std::string raw;
    if (json[begin] == '"')
    {
        if (!aip::DecodeJsonStringUtf8Range(json, begin, end, raw)) return false;
    }
    else raw = json.substr(begin, end - begin);
    return ParseAsciiInt(raw, value);
}
inline bool JsonContainer(const std::string& json, const std::string& key, char opening, std::string& value)
{
    size_t begin = 0, end = 0;
    if (!JsonRange(json, key, begin, end) || json[begin] != opening) return false;
    value = json.substr(begin, end - begin);
    return true;
}
inline bool JsonObject(const std::string& json, const std::string& key, std::string& value)
{ return JsonContainer(json, key, '{', value); }
inline bool JsonArray(const std::string& json, const std::string& key, std::string& value)
{ return JsonContainer(json, key, '[', value); }
inline int CountFinishedExpeditions(const std::string& json)
{
    int finished = 0;
    size_t position = 1;
    for (;;)
    {
        SkipWhitespace(json, position);
        if (position >= json.size() || json[position] == ']') break;
        const auto end = aip::JsonValueEnd(json, position);
        if (end == std::string::npos) return 0;
        std::string status;
        if (json[position] == '{' && JsonString(json.substr(position, end - position), "status", status) && status == "Finished") ++finished;
        position = end;
        SkipWhitespace(json, position);
        if (position < json.size() && json[position] == ',') ++position;
        else break;
    }
    return finished;
}
inline std::string Md5Hex(const std::string& text)
{
    if (text.size() > MAXDWORD)
    {
        return "";
    }

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    BYTE bytes[16]{};
    DWORD byteCount = sizeof(bytes);

    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        return "";
    }
    if (!CryptCreateHash(provider, CALG_MD5, 0, 0, &hash))
    {
        CryptReleaseContext(provider, 0);
        return "";
    }
    if (!CryptHashData(hash, reinterpret_cast<const BYTE*>(text.data()), static_cast<DWORD>(text.size()), 0) ||
        !CryptGetHashParam(hash, HP_HASHVAL, bytes, &byteCount, 0))
    {
        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        return "";
    }

    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(32);
    for (BYTE b : bytes)
    {
        result.push_back(hex[b >> 4]);
        result.push_back(hex[b & 0x0F]);
    }

    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    return result;
}

inline std::string GenerateDS(std::time_t now, const std::string& random)
{
    static const char salt[] = "6s25p5ox5y14umn1p61aqyyvbvvl3lrt";
    const std::string source = "salt=" + std::string(salt) + "&t=" + std::to_string(static_cast<long long>(now)) + "&r=" + random;
    const auto digest = Md5Hex(source);
    return digest.empty() ? "" : std::to_string(static_cast<long long>(now)) + "," + random + "," + digest;
}

inline std::string GenerateDS()
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

    std::random_device randomDevice;
    std::mt19937 generator(randomDevice());
    std::uniform_int_distribution<int> distribution(0, static_cast<int>(sizeof(alphabet) - 2));

    std::string random;
    random.reserve(6);
    for (int i = 0; i < 6; ++i)
    {
        random.push_back(alphabet[distribution(generator)]);
    }

    return GenerateDS(std::time(nullptr), random);
}

inline bool GenshinServer(const std::string& uid, std::wstring& server)
{
    if (uid.empty())
    {
        return false;
    }
    switch (uid[0])
    {
    case '1':
    case '2': server = L"cn_gf01"; return true;
    case '5': server = L"cn_qd01"; return true;
    case '6': server = L"os_usa"; return true;
    case '7': server = L"os_euro"; return true;
    case '8': server = L"os_asia"; return true;
    case '9': server = L"os_cht"; return true;
    default: return false;
    }
}

inline bool HsrServer(const std::string& uid, std::wstring& server)
{
    if (uid.empty())
    {
        return false;
    }
    switch (uid[0])
    {
    case '1':
    case '2': server = L"prod_gf_cn"; return true;
    case '5': server = L"prod_qd_cn"; return true;
    case '6': server = L"prod_official_usa"; return true;
    case '7': server = L"prod_official_eur"; return true;
    case '8': server = L"prod_official_asia"; return true;
    case '9': server = L"prod_official_cht"; return true;
    default: return false;
    }
}

inline bool ZzzServer(const std::string& uid, std::wstring& server)
{
    if (uid.size() < 2)
    {
        return false;
    }
    switch (uid[1])
    {
    case '0': server = L"prod_gf_us"; return true;
    case '3': server = L"prod_gf_jp"; return true;
    case '5': server = L"prod_gf_eu"; return true;
    case '7': server = L"prod_gf_sg"; return true;
    default: return false;
    }
}

inline std::wstring FormatDuration(int seconds)
{
    if (seconds <= 0)
    {
        return L"Full";
    }
    int hours = seconds / 3600;
    int minutes = (seconds / 60) - hours * 60;

    wchar_t buffer[64]{};
    if (hours > 0)
    {
        swprintf(buffer, ARRAYSIZE(buffer), L"%dh %dm", hours, minutes);
    }
    else
    {
        swprintf(buffer, ARRAYSIZE(buffer), L"%dm", minutes);
    }
    return buffer;
}

inline std::wstring FormatCount(int current, int max)
{
    return std::to_wstring(current) + L"/" + std::to_wstring(max);
}

inline std::wstring FormatCountWithRecovery(int current, int max, int seconds)
{
    std::wstring value = FormatCount(current, max);
    if (seconds > 0)
    {
        value += L" [";
        value += FormatDuration(seconds);
        value += L"]";
    }
    return value;
}

inline std::wstring FormatMenuLine(const wchar_t* label, int current, int max)
{
    return std::wstring(label) + L": " + FormatCount(current, max);
}

inline std::wstring ZzzSaleStateText(const std::string& saleState)
{
    if (saleState == "SaleStateDoing")
    {
        return L"Open";
    }
    if (saleState == "SaleStateNo")
    {
        return L"Closed";
    }
    if (saleState == "SaleStateDone")
    {
        return L"Done";
    }
    return L"ERROR";
}

inline std::wstring ZzzCardStateText(const std::string& cardSign)
{
    if (cardSign == "CardSignNo")
    {
        return L"Incomplete";
    }
    if (cardSign == "CardSignDone")
    {
        return L"Done";
    }
    return L"ERROR";
}

} // namespace detail

inline Snapshot ParseResponse(ResourceKind resource, const std::string& body, UINT refreshSeconds = kDefaultRefreshSeconds)
{
    using namespace detail;
    if (resource != ResourceKind::Resin && resource != ResourceKind::Stamina && resource != ResourceKind::Charge)
        return MakeError(resource, L"Configuration error", L"Select resin, stamina, or charge", refreshSeconds);
    const auto& metadata = ResourceDetails(resource);
    Account cfg;
    cfg.refreshSeconds = refreshSeconds;
    const std::string document = body.compare(0, 3, "\xEF\xBB\xBF") == 0 ? body.substr(3) : body;
    if (body.size() > 16u * 1024u * 1024u || !ValidDocument(document))
        return MakeError(resource, L"Parse error", L"Invalid JSON response", refreshSeconds);
    int retcode = -1;
    if (!JsonInt(document, "retcode", retcode))
        return MakeError(resource, L"Parse error", L"Missing response status", refreshSeconds);
    if (retcode != 0)
    {
        std::string message;
        JsonString(document, "message", message);
        return MakeError(resource, L"API error", message.empty() ? std::to_wstring(retcode) : detail::Utf8ToWide(message), cfg.refreshSeconds);
    }

    std::string data;
    if (!JsonObject(document, "data", data))
        return MakeError(resource, L"Parse error", L"Missing resource data", refreshSeconds);
    int current = 0;
    int max = 0;
    int seconds = 0;
    bool parsed = false;
    bool recoveryParsed = false;
    if (resource == ResourceKind::Resin)
    {
        std::string recovery;
        parsed = JsonInt(data, "current_resin", current) && JsonInt(data, "max_resin", max);
        if (JsonString(data, "resin_recovery_time", recovery))
        {
            recoveryParsed = ParseAsciiInt(recovery, seconds);
        }
    }
    else if (resource == ResourceKind::Stamina)
    {
        parsed = JsonInt(data, "current_stamina", current) && JsonInt(data, "max_stamina", max);
        recoveryParsed = JsonInt(data, "stamina_recover_time", seconds);
    }
    else
    {
        std::string energy;
        std::string progress;
        parsed = JsonObject(data, "energy", energy) &&
            JsonObject(energy, "progress", progress) &&
            JsonInt(progress, "current", current) &&
            JsonInt(progress, "max", max);
        recoveryParsed = JsonInt(energy, "restore", seconds);
    }

    if (!parsed || current < 0 || max <= 0 || seconds < 0 || (!recoveryParsed && current < max))
    {
        return MakeError(resource, L"Parse error", metadata.displayName, cfg.refreshSeconds);
    }

    Snapshot state;
    state.current = current;
    state.maximum = max;
    state.recoverySeconds = seconds;
    state.resource = resource;
    state.state = current >= max ? StateKind::Full : StateKind::Ok;
    state.line1 = FormatCount(current, max);
    state.line2 = current >= max ? L"Full" : (seconds > 0 ? FormatDuration(seconds) : L"Recovery unavailable");
    state.tooltip = std::wstring(metadata.displayName) + L": " + state.line1 + L" - " + state.line2;
    state.refreshSeconds = ClampRefresh(cfg.refreshSeconds);

    if (resource == ResourceKind::Resin)
    {
        state.menuLines.push_back(L"Resin: " + FormatCountWithRecovery(current, max, seconds));

        int finishedTasks = 0;
        int totalTasks = 0;
        if (JsonInt(data, "finished_task_num", finishedTasks) && JsonInt(data, "total_task_num", totalTasks))
        {
            state.menuLines.push_back(FormatMenuLine(L"Commissions", finishedTasks, totalTasks));
        }

        std::string expeditions;
        int maxExpeditions = 0;
        if (JsonArray(data, "expeditions", expeditions) && JsonInt(data, "max_expedition_num", maxExpeditions))
        {
            state.menuLines.push_back(FormatMenuLine(L"Expeditions", CountFinishedExpeditions(expeditions), maxExpeditions));
        }

        int currentHomeCoin = 0;
        int maxHomeCoin = 0;
        if (JsonInt(data, "current_home_coin", currentHomeCoin) && JsonInt(data, "max_home_coin", maxHomeCoin))
        {
            state.menuLines.push_back(FormatMenuLine(L"Realm", currentHomeCoin, maxHomeCoin));
        }

        int remainBosses = 0;
        int bossLimit = 0;
        if (JsonInt(data, "remain_resin_discount_num", remainBosses) && JsonInt(data, "resin_discount_num_limit", bossLimit))
        {
            state.menuLines.push_back(FormatMenuLine(L"Weekly Bosses", remainBosses, bossLimit));
        }
    }
    else if (resource == ResourceKind::Stamina)
    {
        state.menuLines.push_back(L"Stamina: " + FormatCountWithRecovery(current, max, seconds));

        int currentTrain = 0;
        int maxTrain = 0;
        if (JsonInt(data, "current_train_score", currentTrain) && JsonInt(data, "max_train_score", maxTrain))
        {
            state.menuLines.push_back(FormatMenuLine(L"Training", currentTrain, maxTrain));
        }

        std::string expeditions;
        int totalExpeditions = 0;
        if (JsonArray(data, "expeditions", expeditions) && JsonInt(data, "total_expedition_num", totalExpeditions))
        {
            state.menuLines.push_back(FormatMenuLine(L"Expeditions", CountFinishedExpeditions(expeditions), totalExpeditions));
        }

        int reserve = 0;
        if (JsonInt(data, "current_reserve_stamina", reserve))
        {
            state.menuLines.push_back(FormatMenuLine(L"Reserve", reserve, 2400));
        }

        int cocoon = 0;
        int cocoonLimit = 0;
        if (JsonInt(data, "weekly_cocoon_cnt", cocoon) && JsonInt(data, "weekly_cocoon_limit", cocoonLimit))
        {
            state.menuLines.push_back(FormatMenuLine(L"Echo of War", cocoon, cocoonLimit));
        }
    }
    else
    {
        state.menuLines.push_back(L"Charge: " + FormatCountWithRecovery(current, max, seconds));

        std::string vitality;
        int currentVitality = 0;
        int maxVitality = 0;
        if (JsonObject(data, "vitality", vitality) && JsonInt(vitality, "current", currentVitality) && JsonInt(vitality, "max", maxVitality))
        {
            state.menuLines.push_back(FormatMenuLine(L"Engagement", currentVitality, maxVitality));
        }

        std::string cardSign;
        if (JsonString(data, "card_sign", cardSign))
        {
            state.menuLines.push_back(L"Scratch Card: " + ZzzCardStateText(cardSign));
        }

        std::string vhsSale;
        std::string saleState;
        if (JsonObject(data, "vhs_sale", vhsSale) && JsonString(vhsSale, "sale_state", saleState))
        {
            state.menuLines.push_back(L"Video Store: " + ZzzSaleStateText(saleState));
        }
    }
    return state;
}


inline bool BuildRequest(const Configuration& configuration, aip::HttpFetchOptions& request, std::wstring& error)
{
    request = {};
    error.clear();
    if (configuration.resource != ResourceKind::Resin && configuration.resource != ResourceKind::Stamina &&
        configuration.resource != ResourceKind::Charge)
    {
        error = L"Select resin, stamina, or charge.";
        return false;
    }
    if (configuration.uid.empty() || configuration.ltoken.empty() || configuration.ltuid.empty())
    {
        error = L"Configure the selected game's UID and account cookies.";
        return false;
    }
    if (configuration.uid.size() > 32 || !std::all_of(configuration.uid.begin(), configuration.uid.end(),
        [](char ch) { return ch >= '0' && ch <= '9'; }))
    {
        error = L"The game UID must contain only decimal digits.";
        return false;
    }
    const auto invalidCookie = [](const std::string& value) {
        return std::any_of(value.begin(), value.end(), [](unsigned char ch) { return ch < 0x20 || ch == ';' || ch == 0x7F; });
    };
    if (invalidCookie(configuration.ltoken) || invalidCookie(configuration.ltuid))
    {
        error = L"Account cookies contain characters that cannot be sent as cookie values.";
        return false;
    }
    std::wstring server;
    const bool serverOk = configuration.resource == ResourceKind::Resin ? detail::GenshinServer(configuration.uid, server) :
        (configuration.resource == ResourceKind::Stamina ? detail::HsrServer(configuration.uid, server) : detail::ZzzServer(configuration.uid, server));
    if (!serverOk)
    {
        error = L"The UID prefix does not identify a supported server for this game.";
        return false;
    }
    std::wstring token, userId;
    if (!aip::TryUtf8ToWide(configuration.ltoken, token) || !aip::TryUtf8ToWide(configuration.ltuid, userId))
    {
        error = L"Account cookies are not valid UTF-8 text.";
        return false;
    }
    const auto& metadata = ResourceDetails(configuration.resource);
    request.url = std::wstring(L"https://") + metadata.host + metadata.path +
        L"?server=" + server + L"&role_id=" + detail::Utf8ToWide(configuration.uid);
    request.userAgent = L"RealTimeNotesDeskband/1.0";
    request.accept = L"application/json, text/plain, */*";
    request.timeout = configuration.timeout;
    request.maximumBytes = configuration.maximumBytes;
    request.canceled = configuration.canceled;
    const std::string signature = detail::GenerateDS();
    if (signature.empty())
    {
        error = L"Could not create the API request signature.";
        return false;
    }
    // Preserve the deskband's current protocol literals. Storage/decryption is
    // intentionally absent: callers supply the account values explicitly.
    request.headers = {
        {L"Accept-Language", L"en-US,en;q=0.5"},
        {L"x-rpc-client_type", L"5"}, {L"x-rpc-app_version", L"1.5.0"}, {L"x-rpc-language", L"en-us"},
        {L"Origin", L"https://act.hoyolab.com"}, {L"Referer", L"https://act.hoyolab.com/"},
        {L"Sec-Fetch-Dest", L"empty"}, {L"Sec-Fetch-Mode", L"cors"}, {L"Sec-Fetch-Site", L"same-site"},
        {L"Cookie", L"ltoken_v2=" + token + L"; ltuid_v2=" + userId}, {L"DS", detail::Utf8ToWide(signature)}
    };
    std::wstring ignoredHeaders;
    return aip::http_detail::BuildRequestHeaders(request, ignoredHeaders, error);
}

inline Snapshot Fetch(const Configuration& configuration, const HttpTransport& transport = aip::FetchHttpBytes)
{
    try
    {
        if (configuration.canceled && configuration.canceled())
            return MakeError(configuration.resource, L"Canceled", L"Notes refresh was canceled", configuration.refreshSeconds);
        if (configuration.timeout.count() < 1 || configuration.timeout.count() > INT_MAX ||
            configuration.maximumBytes == 0 || configuration.maximumBytes > 16u * 1024u * 1024u)
            return MakeError(configuration.resource, L"Configuration error", L"Invalid HTTP timeout or response limit", configuration.refreshSeconds);
        aip::HttpFetchOptions request;
        std::wstring error;
        if (!BuildRequest(configuration, request, error))
            return MakeError(configuration.resource, L"Configuration error", error, configuration.refreshSeconds);
        aip::HttpFetchResponse response;
        if (!transport(request, response, error))
            return MakeError(configuration.resource, L"Network error", error.empty() ? L"Notes request failed" : error, configuration.refreshSeconds);
        if (configuration.canceled && configuration.canceled())
            return MakeError(configuration.resource, L"Canceled", L"Notes refresh was canceled", configuration.refreshSeconds);
        if (response.statusCode < 200 || response.statusCode >= 300)
            return MakeError(configuration.resource, L"Network error", L"HTTP " + std::to_wstring(response.statusCode), configuration.refreshSeconds);
        if (response.body.size() > configuration.maximumBytes)
            return MakeError(configuration.resource, L"Parse error", L"Notes response exceeded its byte limit", configuration.refreshSeconds);
        return ParseResponse(configuration.resource, std::string(response.body.begin(), response.body.end()), configuration.refreshSeconds);
    }
    catch (...)
    {
        // Exceptions must not unwind through the deskband/host worker. In
        // particular, do not echo a transport exception containing credentials.
        return MakeError(configuration.resource, L"Notes error", L"Could not retrieve resource information", configuration.refreshSeconds);
    }
}

} } // namespace aip::notes
#endif
