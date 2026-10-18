#ifndef AIP_CONTENT_SOURCES_PRAYER_TIMES_INC
#define AIP_CONTENT_SOURCES_PRAYER_TIMES_INC
#include "../desktop_app_baseline.h"
#include "../http_fetch.inc"
#include "prayer_times_model.h"
#include <chrono>
#include <functional>

namespace aip { namespace prayer {
using Cancel = std::function<bool()>;
using HttpTransport = std::function<bool(const aip::HttpFetchOptions&, aip::HttpFetchResponse&, std::wstring&)>;
namespace source_detail {
inline void Space(const std::string& json, size_t& pos) { while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\r' || json[pos] == '\n' || json[pos] == '\t')) ++pos; }
inline bool Value(const std::string& json, size_t& pos, int depth) {
    Space(json,pos); if (pos >= json.size() || depth > 32) return false;
    const char open = json[pos];
    if (open == '{' || open == '[') {
        const char close = open == '{' ? '}' : ']'; ++pos; Space(json,pos);
        if (pos < json.size() && json[pos] == close) { ++pos; return true; }
        for (;;) {
            if (open == '{') {
                if (pos >= json.size() || json[pos] != '"') return false;
                pos = aip::JsonStringEnd(json,pos); if (pos == std::string::npos) return false;
                Space(json,pos); if (pos >= json.size() || json[pos++] != ':') return false;
            }
            if (!Value(json,pos,depth+1)) return false;
            Space(json,pos); if (pos >= json.size()) return false;
            if (json[pos] == close) { ++pos; return true; }
            if (json[pos++] != ',') return false;
            Space(json,pos);
        }
    }
    pos = open == '"' ? aip::JsonStringEnd(json,pos) : aip::JsonPrimitiveValueEnd(json,pos);
    return pos != std::string::npos;
}
inline bool Document(const std::string& json) {
    std::wstring unicode; if (!aip::TryUtf8ToWide(json,unicode)) return false;
    size_t pos = 0; Space(json,pos);
    if (pos >= json.size() || json[pos] != '{' || !Value(json,pos,0)) return false;
    Space(json,pos); return pos == json.size();
}
inline bool Object(const std::string& json, const char* key, std::string& result) {
    size_t at=0,begin=0,end=0;
    if (!aip::FindJsonFieldValue(json,key,at,begin,end) || json[begin] != '{') return false;
    result = json.substr(begin,end-begin); return true;
}
inline bool Number(const std::string& json, const char* key, int& result) {
    size_t at=0,begin=0,end=0; if (!aip::FindJsonFieldValue(json,key,at,begin,end)) return false;
    const std::string raw = json.substr(begin,end-begin);
    return Integer(std::wstring(raw.begin(),raw.end()),result);
}
inline std::wstring Encoded(const std::wstring& text) {
    std::string utf8; if (!aip::TryWideToUtf8(text,utf8)) return {};
    static const wchar_t* hex = L"0123456789ABCDEF"; std::wstring out;
    for (const unsigned char ch : utf8) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') out += static_cast<wchar_t>(ch);
        else { out += L'%'; out += hex[ch >> 4]; out += hex[ch & 15]; }
    }
    return out;
}
inline void Store(Data& data, Day day) {
    for (auto& existing : data.schedule.days) if (existing.date == day.date) { existing = std::move(day); return; }
    data.schedule.days.push_back(std::move(day));
}
inline bool Expired(const Day* day, const Configuration& cfg, UTC now) {
    return day == nullptr || day->fetchedAtUtc > now + 300 || now - day->fetchedAtUtc >= cfg.refreshSeconds;
}
}

inline bool BuildRequest(const Configuration& input, Date date, aip::HttpFetchOptions& request, std::wstring& error, bool iso8601 = true) {
    request = {}; Configuration cfg = input;
    if (!Validate(cfg,error,true) || !ValidDate(date)) { if (error.empty()) error = L"PrayerTimes request date is invalid."; return false; }
    request.url = L"https://api.aladhan.com/v1/timingsByCity/" + Two(date.day) + L"-" + Two(date.month) + L"-" + std::to_wstring(date.year) +
        L"?city=" + source_detail::Encoded(cfg.city) + L"&country=" + source_detail::Encoded(cfg.country);
    if (!cfg.state.empty()) request.url += L"&state=" + source_detail::Encoded(cfg.state);
    if (cfg.method >= 0) request.url += L"&method=" + std::to_wstring(cfg.method);
    request.url += L"&school=" + std::to_wstring(cfg.school);
    if (!cfg.timeZone.empty()) request.url += L"&timezonestring=" + source_detail::Encoded(cfg.timeZone);
    request.url += L"&latitudeAdjustmentMethod=" + std::to_wstring(cfg.latitudeAdjustmentMethod) + (iso8601 ? L"&iso8601=true" : L"&iso8601=false");
    request.accept = L"application/json"; request.userAgent = L"AIProjects PrayerTimes/1";
    request.maximumBytes = static_cast<size_t>(cfg.maximumBytes);
    request.timeout = std::chrono::milliseconds(cfg.timeoutSeconds * 1000);
    return true;
}

inline bool CivilOffset(const std::wstring& clock, Date date, UTC instant, int& offset, bool allowNextDate = false, int referenceOffset = 0) {
    if (clock.size() < 5 || clock[2] != L':' ||
        !std::all_of(clock.begin(),clock.begin()+2,[](wchar_t c){return c>=L'0' && c<=L'9';}) ||
        !std::all_of(clock.begin()+3,clock.begin()+5,[](wchar_t c){return c>=L'0' && c<=L'9';})) return false;
    const auto suffix = Trim(clock.substr(5));
    if (!suffix.empty() && (suffix.front() != L'(' || suffix.back() != L')')) return false;
    int hour=0,minute=0; Integer(clock.substr(0,2),hour); Integer(clock.substr(3,2),minute);
    if (hour > 23 || minute > 59) return false;
    const UTC delta = CivilDay(date)*86400 + hour*3600 + minute*60 - instant;
    if (delta % 60) return false;
    const UTC sameDay = delta/60, nextDay = sameDay+1440;
    const bool sameValid = sameDay >= -840 && sameDay <= 840, nextValid = allowNextDate && nextDay >= -840 && nextDay <= 840;
    if (!sameValid && !nextValid) return false;
    if (nextValid && (!sameValid || std::abs(nextDay-referenceOffset) < std::abs(sameDay-referenceOffset))) offset = static_cast<int>(nextDay);
    else offset = static_cast<int>(sameDay);
    return true;
}

inline bool ParseDayJson(const std::string& bytes, const std::string& civilBytes, Date requested, Day& result, std::wstring& error) {
    result = {}; error = L"PrayerTimes returned an invalid or incomplete daily schedule.";
    if (bytes.size() > 1048576 || civilBytes.size() > 1048576) return false;
    const auto json = bytes.compare(0,3,"\xEF\xBB\xBF") == 0 ? bytes.substr(3) : bytes;
    if (!source_detail::Document(json)) return false;
    int code = 0;
    if (!source_detail::Number(json,"code",code)) return false;
    if (code != 200) { error = L"PrayerTimes API returned status " + std::to_wstring(code) + L"."; return false; }
    std::string data,meta,timings,date,gregorian,hijri;
    if (!source_detail::Object(json,"data",data) || !source_detail::Object(data,"meta",meta) ||
        !source_detail::Object(data,"timings",timings) || !source_detail::Object(data,"date",date) ||
        !source_detail::Object(date,"gregorian",gregorian)) return false;
    Day day; std::wstring formattedDate;
    if (!aip::TryExtractJsonStringValue(gregorian,"date",formattedDate) || !ParseDate(formattedDate,day.date,true) || day.date != requested ||
        !aip::TryExtractJsonStringValue(meta,"timezone",day.timeZone) || day.timeZone.empty()) return false;
    if (source_detail::Object(date,"hijri",hijri)) aip::TryExtractJsonStringValue(hijri,"date",day.hijriDate);
    const auto civilJson = civilBytes.compare(0,3,"\xEF\xBB\xBF") == 0 ? civilBytes.substr(3) : civilBytes;
    std::string civilData,civilMeta,civilTimings,civilDate,civilGregorian;
    std::wstring civilDateText,civilZone; Date civilDay; int civilCode=0;
    if (!source_detail::Document(civilJson) || !source_detail::Number(civilJson,"code",civilCode) || civilCode != 200 ||
        !source_detail::Object(civilJson,"data",civilData) || !source_detail::Object(civilData,"meta",civilMeta) ||
        !source_detail::Object(civilData,"timings",civilTimings) || !source_detail::Object(civilData,"date",civilDate) ||
        !source_detail::Object(civilDate,"gregorian",civilGregorian) ||
        !aip::TryExtractJsonStringValue(civilGregorian,"date",civilDateText) || !ParseDate(civilDateText,civilDay,true) || civilDay != day.date ||
        !aip::TryExtractJsonStringValue(civilMeta,"timezone",civilZone) || civilZone != day.timeZone) {
        error = L"PrayerTimes ISO and local-clock responses disagree about their date or time zone."; return false;
    }
    static const char* keys[] = {"Fajr","Dhuhr","Asr","Maghrib","Isha"};
    for (size_t i=0; i<5; ++i) {
        std::wstring value; IsoTime parsed;
        if (!aip::TryExtractJsonStringValue(timings,keys[i],value) || !ParseIso8601(value,parsed)) {
            error = std::wstring(L"PrayerTimes is missing a valid explicit-offset ") + PrayerName(static_cast<PrayerId>(i)) + L" time."; return false;
        }
        std::wstring localClock; int cityOffset=0;
        if (!aip::TryExtractJsonStringValue(civilTimings,keys[i],localClock) ||
            !CivilOffset(localClock,day.date,parsed.utcSeconds,cityOffset,i == 4,i ? day.prayers[i-1].offsetMinutes : 0)) {
            error = L"PrayerTimes could not correlate UTC and local prayer clocks."; return false;
        }
        day.prayers[i] = {static_cast<PrayerId>(i),parsed.utcSeconds,cityOffset};
    }
    std::wstring sunrise; IsoTime parsedSunrise;
    std::wstring localSunrise; int sunriseOffset=0;
    if (aip::TryExtractJsonStringValue(timings,"Sunrise",sunrise) && ParseIso8601(sunrise,parsedSunrise) &&
        aip::TryExtractJsonStringValue(civilTimings,"Sunrise",localSunrise) && CivilOffset(localSunrise,day.date,parsedSunrise.utcSeconds,sunriseOffset)) {
        day.sunrise = {PrayerId::Sunrise,parsedSunrise.utcSeconds,sunriseOffset}; day.hasSunrise = true;
    }
    // ISO timestamps may be serialized in the API server's zone even though
    // meta.timezone names the requested city. Pair ISO instants with the plain
    // city's HH:mm values; never treat the serialized offset as the city zone.
    day.offsetMinutes = day.prayers[1].offsetMinutes;
    if (!ValidDay(day)) return false;
    result = std::move(day); error.clear(); return true;
}

inline Data Refresh(const Configuration& input, UTC now, const Data& previous = {},
    const Cancel& canceled = {}, const HttpTransport& transport = aip::FetchHttpBytes) {
    Configuration cfg = input; Data data; std::wstring error;
    if (!Validate(cfg,error,true) || now < -11644473600LL || now > 253402214399LL) {
        data.error = error.empty() ? L"PrayerTimes clock is outside the supported range." : error;
        data.nextRefreshUtc = now > 253402214399LL ? now : now + 300; return data;
    }
    const auto key = ConfigurationKey(cfg);
    if (previous.configKey == key) data = previous;
    data.configKey = key;
    auto fail = [&](const std::wstring& message) {
        data.stale = !data.schedule.days.empty(); data.error = message;
        data.nextRefreshUtc = now + 300; return data;
    };
    try {
        data.schedule.days.erase(std::remove_if(data.schedule.days.begin(),data.schedule.days.end(),
            [&](const Day& day) { return !ValidDay(day) || day.fetchedAtUtc > now + 300; }),data.schedule.days.end());
        if (data.schedule.days.size() > 3) data.schedule.days.clear();
        if (canceled && canceled()) return fail(L"PrayerTimes refresh canceled.");
        if (!data.error.empty() && data.nextRefreshUtc > now && data.nextRefreshUtc <= now + 300) return data;
        const Day* current = CurrentDay(data,now);
        Date today = current ? current->date : DateAtUTC(now, data.schedule.days.empty() ? 0 : data.schedule.days.back().offsetMinutes);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(cfg.timeoutSeconds);
        unsigned requests = 0;
        const auto expired = [&] { return (canceled && canceled()) || std::chrono::steady_clock::now() >= deadline; };
        auto download = [&](Date date, bool iso, std::string& body) {
            if (expired() || ++requests > 6) { error = L"PrayerTimes refresh was canceled or exceeded its total deadline."; return false; }
            aip::HttpFetchOptions request; aip::HttpFetchResponse response;
            if (!BuildRequest(cfg,date,request,error,iso)) return false;
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0) { error = L"PrayerTimes refresh exceeded its total deadline."; return false; }
            request.timeout = left; request.canceled = expired;
            if (!transport(request,response,error)) {
                if (error.empty()) error = L"PrayerTimes daily request failed.";
                return false;
            }
            if (expired()) { error = L"PrayerTimes refresh was canceled or exceeded its total deadline."; return false; }
            if (response.statusCode < 200 || response.statusCode >= 300) { error = L"PrayerTimes HTTP status " + std::to_wstring(response.statusCode) + L"."; return false; }
            if (response.body.size() > static_cast<size_t>(cfg.maximumBytes)) { error = L"PrayerTimes response exceeded its byte limit."; return false; }
            body.assign(response.body.begin(),response.body.end()); return true;
        };
        auto fetch = [&](Date date) {
            std::string iso,civil;
            if (!download(date,true,iso) || !download(date,false,civil)) return false;
            Day day;
            if (!ParseDayJson(iso,civil,date,day,error)) return false;
            day.fetchedAtUtc = now; source_detail::Store(data,std::move(day)); data.lastSuccessUtc = now; return true;
        };
        if (source_detail::Expired(FindDay(data,today),cfg,now) && !fetch(today)) return fail(error);
        // The first cold request is a UTC date, not the PC's local date. Correct
        // it using the returned zone offset before choosing today/tomorrow.
        const Day* guess = FindDay(data,today);
        if (!guess) return fail(L"PrayerTimes could not determine the location's local date.");
        const Date local = DateAtUTC(now,guess->offsetMinutes);
        if (local != today) {
            today = local;
            if (source_detail::Expired(FindDay(data,today),cfg,now) && !fetch(today)) return fail(error);
        }
        const Date tomorrow = AddDays(today,1);
        if (source_detail::Expired(FindDay(data,tomorrow),cfg,now) && !fetch(tomorrow)) return fail(error);
        data.schedule.days.erase(std::remove_if(data.schedule.days.begin(),data.schedule.days.end(),
            [&](const Day& day) { return day.date != today && day.date != tomorrow; }),data.schedule.days.end());
        std::sort(data.schedule.days.begin(),data.schedule.days.end(),[](const Day& a, const Day& b) { return CivilDay(a.date) < CivilDay(b.date); });
        data.stale = false; data.error.clear(); data.nextRefreshUtc = now + cfg.refreshSeconds;
        for (const auto& day : data.schedule.days) data.nextRefreshUtc = (std::min)(data.nextRefreshUtc,day.fetchedAtUtc + cfg.refreshSeconds);
        const auto* ready = FindDay(data,today);
        if (ready) data.nextRefreshUtc = (std::min)(data.nextRefreshUtc,(CivilDay(today)+1)*86400 - ready->offsetMinutes*60);
        data.nextRefreshUtc = (std::max)(now + 1,data.nextRefreshUtc);
        return data;
    } catch (...) { return fail(L"PrayerTimes refresh could not be completed."); }
}
} }
#endif
