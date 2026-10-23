#pragma once
// Portable prayer schedule/configuration/display model. No Windows, location,
// network, persistence, or content-engine dependency.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace aip { namespace prayer {
using UTC = std::int64_t;
struct Configuration {
    std::wstring city, country, state, timeZone;
    int method = -1, school = 0, latitudeAdjustmentMethod = 3;
    std::array<int, 5> adjustmentsMinutes{};
    int refreshSeconds = 43200, timeoutSeconds = 10, maximumBytes = 262144;
    bool showSunrise = true, twentyFourHour = false, showTimetable = true, nextIncludesSunrise = false;
};
inline constexpr const wchar_t* SettingKeys[] = {L"City", L"Country", L"State", L"TimeZone", L"Method", L"School",
    L"LatitudeAdjustmentMethod", L"FajrAdjustmentMinutes", L"DhuhrAdjustmentMinutes", L"AsrAdjustmentMinutes",
    L"MaghribAdjustmentMinutes", L"IshaAdjustmentMinutes", L"RefreshSeconds", L"TimeoutSeconds", L"MaximumBytes",
    L"ShowSunrise", L"TwentyFourHour", L"ShowTimetable", L"NextIncludesSunrise"};
inline constexpr const wchar_t* SettingDefaults[] = {L"",L"",L"",L"",L"Auto",L"0",L"3",L"0",L"0",L"0",L"0",L"0",
    L"43200",L"10",L"262144",L"1",L"0",L"1",L"0"};
inline std::wstring Trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    return first == std::wstring::npos ? L"" : value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
}
inline std::wstring Lower(std::wstring value) { for (auto& ch : value) ch = static_cast<wchar_t>(std::towlower(ch)); return value; }
inline bool Integer(const std::wstring& text, int& result) {
    if (text.empty()) return false;
    size_t pos = text[0] == '-' || text[0] == '+' ? 1 : 0;
    if (pos == text.size()) return false;
    std::int64_t value = 0;
    for (; pos < text.size(); ++pos) {
        if (text[pos] < L'0' || text[pos] > L'9') return false;
        value = value * 10 + text[pos] - L'0';
        if (value > 2147483648LL) return false;
    }
    if (text[0] == '-') value = -value;
    if (value < (std::numeric_limits<int>::min)() || value > (std::numeric_limits<int>::max)()) return false;
    result = static_cast<int>(value); return true;
}
inline bool PlainText(const std::wstring& value, size_t maximum) {
    if (value.size() > maximum) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        const auto ch = static_cast<std::uint32_t>(value[i]);
        if (ch < 0x20 || ch == 0x7F || ch > 0x10FFFF) return false;
        if (ch >= 0xD800 && ch <= 0xDBFF) {
            if (i + 1 >= value.size() || value[i + 1] < 0xDC00 || value[i + 1] > 0xDFFF) return false;
            ++i;
        } else if (ch >= 0xDC00 && ch <= 0xDFFF) return false;
    }
    return true;
}
inline bool NormalizeSetting(std::wstring& key, std::wstring& value, std::wstring& error) {
    error.clear(); const auto lower = Lower(Trim(key)); size_t index = 0;
    for (; index < std::size(SettingKeys); ++index) if (Lower(SettingKeys[index]) == lower) break;
    if (index == std::size(SettingKeys)) { error = L"Unknown PrayerTimes setting."; return false; }
    key = SettingKeys[index]; value = Trim(value);
    if (index <= 3) {
        if (!PlainText(value, index == 3 ? 128 : 256)) { error = key + L" must be bounded single-line text."; return false; }
        if (index == 3 && std::any_of(value.begin(), value.end(), [](wchar_t ch) {
            return !((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') ||
                ch == L'/' || ch == L'_' || ch == L'-' || ch == L'+' || ch == L'.'); })) {
            error = L"TimeZone must be empty or an IANA zone name such as Asia/Jakarta."; return false;
        }
        return true;
    }
    if (index >= 15) {
        const auto boolean = Lower(value);
        if (boolean == L"true" || boolean == L"yes" || boolean == L"on" || boolean == L"1") value = L"1";
        else if (boolean == L"false" || boolean == L"no" || boolean == L"off" || boolean == L"0") value = L"0";
        else { error = key + L" must be a boolean."; return false; }
        return true;
    }
    if (index == 4 && Lower(value) == L"auto") value = L"-1";
    int number;
    if (!Integer(value, number)) { error = key + L" must be an integer."; return false; }
    bool valid = index == 4 ? number == -1 || (number >= 0 && number <= 23 && number != 6) :
        index == 5 ? number >= 0 && number <= 1 : index == 6 ? number >= 1 && number <= 3 :
        index <= 11 ? number >= -180 && number <= 180 : index == 12 ? number >= 300 && number <= 604800 :
        index == 13 ? number >= 1 && number <= 60 : number >= 4096 && number <= 1048576;
    if (!valid) { error = key + L" is outside its supported range."; return false; }
    value = index == 4 && number == -1 ? L"Auto" : std::to_wstring(number); return true;
}
template<class Reader> inline bool ReadConfiguration(Reader get, Configuration& cfg, std::wstring& error) {
    Configuration result;
    for (size_t i = 0; i < std::size(SettingKeys); ++i) {
        std::wstring key = SettingKeys[i], value = get(SettingKeys[i], SettingDefaults[i]);
        if (!NormalizeSetting(key, value, error)) return false;
        int number = 0;
        if (i >= 4 && i < 15) { if (value == L"Auto") number = -1; else Integer(value, number); }
        switch (i) {
        case 0: result.city = value; break; case 1: result.country = value; break;
        case 2: result.state = value; break; case 3: result.timeZone = value; break;
        case 4: result.method = number; break; case 5: result.school = number; break;
        case 6: result.latitudeAdjustmentMethod = number; break;
        case 12: result.refreshSeconds = number; break; case 13: result.timeoutSeconds = number; break;
        case 14: result.maximumBytes = number; break; case 15: result.showSunrise = value == L"1"; break;
        case 16: result.twentyFourHour = value == L"1"; break;
        case 17: result.showTimetable = value == L"1"; break;
        case 18: result.nextIncludesSunrise = value == L"1"; break;
        default: result.adjustmentsMinutes[i - 7] = number; break;
        }
    }
    cfg = std::move(result); return true;
}
inline bool Validate(Configuration& cfg, std::wstring& error, bool requireLocation = false) {
    const Configuration input = cfg;
    bool ok = ReadConfiguration([&](const wchar_t* key, const wchar_t*) -> std::wstring {
        if (std::wstring(key) == L"City") return input.city;
        if (std::wstring(key) == L"Country") return input.country;
        if (std::wstring(key) == L"State") return input.state;
        if (std::wstring(key) == L"TimeZone") return input.timeZone;
        if (std::wstring(key) == L"Method") return std::to_wstring(input.method);
        if (std::wstring(key) == L"School") return std::to_wstring(input.school);
        if (std::wstring(key) == L"LatitudeAdjustmentMethod") return std::to_wstring(input.latitudeAdjustmentMethod);
        if (std::wstring(key) == L"RefreshSeconds") return std::to_wstring(input.refreshSeconds);
        if (std::wstring(key) == L"TimeoutSeconds") return std::to_wstring(input.timeoutSeconds);
        if (std::wstring(key) == L"MaximumBytes") return std::to_wstring(input.maximumBytes);
        if (std::wstring(key) == L"ShowSunrise") return input.showSunrise ? L"1" : L"0";
        if (std::wstring(key) == L"TwentyFourHour") return input.twentyFourHour ? L"1" : L"0";
        if (std::wstring(key) == L"ShowTimetable") return input.showTimetable ? L"1" : L"0";
        if (std::wstring(key) == L"NextIncludesSunrise") return input.nextIncludesSunrise ? L"1" : L"0";
        for (size_t i = 0; i < 5; ++i) if (std::wstring(key) == SettingKeys[i + 7]) return std::to_wstring(input.adjustmentsMinutes[i]);
        return L"";
    }, cfg, error);
    if (ok && requireLocation && (cfg.city.empty() || cfg.country.empty())) { error = L"Set PrayerTimes City and Country to retrieve a schedule."; return false; }
    return ok;
}
inline std::wstring ConfigurationKey(const Configuration& input) {
    auto cfg = input; std::wstring ignored; if (!Validate(cfg, ignored)) return {};
    std::wstring key = L"PrayerTimes:1";
    for (const auto& text : {cfg.city,cfg.country,cfg.state,cfg.timeZone}) key += L":" + std::to_wstring(text.size()) + L":" + text;
    return key + L":" + std::to_wstring(cfg.method) + L":" + std::to_wstring(cfg.school) + L":" + std::to_wstring(cfg.latitudeAdjustmentMethod);
}

struct Date { int year = 0, month = 0, day = 0; };
inline bool operator==(Date a, Date b) { return a.year == b.year && a.month == b.month && a.day == b.day; }
inline bool operator!=(Date a, Date b) { return !(a == b); }
inline bool Leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
inline bool ValidDate(Date date) {
    static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return date.year >= 1601 && date.year <= 9999 && date.month >= 1 && date.month <= 12 && date.day >= 1 &&
        date.day <= lengths[date.month - 1] + (date.month == 2 && Leap(date.year) ? 1 : 0);
}
inline UTC CivilDay(Date date) {
    int year = date.year - (date.month <= 2 ? 1 : 0);
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned month = static_cast<unsigned>(date.month + (date.month > 2 ? -3 : 9));
    const unsigned doy = (153 * month + 2) / 5 + static_cast<unsigned>(date.day - 1);
    return static_cast<UTC>(era) * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
inline Date FromCivilDay(UTC day) {
    day += 719468; const UTC era = (day >= 0 ? day : day - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(day - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int year = static_cast<int>(yoe + era * 400);
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    const int month = static_cast<int>(mp) + (mp < 10 ? 3 : -9);
    year += month <= 2 ? 1 : 0;
    return {year, month, static_cast<int>(doy - (153 * mp + 2) / 5 + 1)};
}
inline UTC FloorDay(UTC seconds) { UTC day = seconds / 86400; if (seconds < 0 && seconds % 86400) --day; return day; }
inline Date DateAtUTC(UTC seconds, int offsetMinutes = 0) { return FromCivilDay(FloorDay(seconds + static_cast<UTC>(offsetMinutes) * 60)); }
inline Date AddDays(Date date, int count) { return FromCivilDay(CivilDay(date) + count); }
inline std::wstring Two(int number) { return (number < 10 ? L"0" : L"") + std::to_wstring(number); }
inline std::wstring DateText(Date date) { return std::to_wstring(date.year) + L"-" + Two(date.month) + L"-" + Two(date.day); }
inline bool ParseDate(const std::wstring& text, Date& result, bool dayFirst = false) {
    if (text.size() != 10 || text[dayFirst ? 2 : 4] != L'-' || text[dayFirst ? 5 : 7] != L'-') return false;
    for (size_t i=0; i<text.size(); ++i) if (i != static_cast<size_t>(dayFirst ? 2 : 4) && i != static_cast<size_t>(dayFirst ? 5 : 7) && (text[i] < L'0' || text[i] > L'9')) return false;
    Date value;
    if (!Integer(text.substr(dayFirst ? 6 : 0, 4), value.year) || !Integer(text.substr(dayFirst ? 3 : 5, 2), value.month) ||
        !Integer(text.substr(dayFirst ? 0 : 8, 2), value.day) || !ValidDate(value)) return false;
    result = value; return true;
}
struct IsoTime { Date date; UTC utcSeconds = 0; int offsetMinutes = 0; };
inline bool ParseIso8601(const std::wstring& text, IsoTime& result) {
    if (text.size() < 20 || text[10] != L'T' || text[13] != L':' || text[16] != L':') return false;
    IsoTime parsed; int hour = 0, minute = 0, second = 0;
    for (const size_t pos : {11u,12u,14u,15u,17u,18u}) if (text[pos] < L'0' || text[pos] > L'9') return false;
    if (!ParseDate(text.substr(0,10), parsed.date) || !Integer(text.substr(11,2),hour) || !Integer(text.substr(14,2),minute) ||
        !Integer(text.substr(17,2),second) || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;
    size_t offset = 19;
    if (text[offset] == L'.') { ++offset; const auto begin = offset; while (offset < text.size() && text[offset] >= L'0' && text[offset] <= L'9') ++offset; if (offset == begin || offset - begin > 9) return false; }
    if (offset + 1 == text.size() && text[offset] == L'Z') parsed.offsetMinutes = 0;
    else {
        if (offset + 6 != text.size() || (text[offset] != L'+' && text[offset] != L'-') || text[offset + 3] != L':') return false;
        int oh = 0, om = 0;
        for (const size_t pos : {offset+1,offset+2,offset+4,offset+5}) if (text[pos] < L'0' || text[pos] > L'9') return false;
        if (!Integer(text.substr(offset + 1,2),oh) || !Integer(text.substr(offset + 4,2),om) || oh < 0 || oh > 14 || om < 0 || om > 59 || (oh == 14 && om)) return false;
        parsed.offsetMinutes = (oh * 60 + om) * (text[offset] == L'-' ? -1 : 1);
    }
    parsed.utcSeconds = CivilDay(parsed.date) * 86400 + hour * 3600 + minute * 60 + second - parsed.offsetMinutes * 60;
    result = parsed; return true;
}
enum class PrayerId { Fajr, Dhuhr, Asr, Maghrib, Isha, Sunrise };
inline const wchar_t* PrayerName(PrayerId id) { static const wchar_t* names[] = {L"Fajr",L"Dhuhr",L"Asr",L"Maghrib",L"Isha",L"Sunrise"}; const int i = static_cast<int>(id); return i >= 0 && i < 6 ? names[i] : L"Prayer"; }
struct Prayer { PrayerId id = PrayerId::Fajr; UTC utcSeconds = 0; int offsetMinutes = 0; };
struct Day {
    Date date; std::wstring timeZone, hijriDate;
    std::array<Prayer,5> prayers{}; Prayer sunrise{PrayerId::Sunrise,0,0}; bool hasSunrise = false;
    int offsetMinutes = 0; UTC fetchedAtUtc = 0;
};
struct Schedule { std::vector<Day> days; };
struct Data { std::wstring configKey; Schedule schedule; bool stale = false; std::wstring error; UTC nextRefreshUtc = 0, lastSuccessUtc = 0; };
struct DetailRow { PrayerId id = PrayerId::Fajr; std::wstring label, value; bool next = false; };
// Semantic display fields travel with the immutable snapshot. Renderers never
// recover a prayer or countdown by parsing a translated summary sentence.
struct ClockText {
    std::wstring time, prayer, location, minutes, countdown, details;
    bool showTimetable = true;
    bool Available() const { return !time.empty() && !prayer.empty(); }
};
struct View {
    std::wstring title, secondary, badge, timeZone, hijriDate, error;
    ClockText clock;
    std::vector<DetailRow> detailRows; bool available = false, stale = false; UTC nextUTCDeadline = 0;
};
inline bool ValidDay(const Day& day) {
    if (!ValidDate(day.date) || day.timeZone.empty() || !PlainText(day.timeZone,128) || !PlainText(day.hijriDate,128) ||
        day.offsetMinutes < -840 || day.offsetMinutes > 840) return false;
    for (size_t i = 0; i < day.prayers.size(); ++i) {
        const auto& prayer = day.prayers[i];
        if (prayer.id != static_cast<PrayerId>(i) || prayer.offsetMinutes < -840 || prayer.offsetMinutes > 840 ||
            prayer.utcSeconds < CivilDay(day.date) * 86400 - 86400 || prayer.utcSeconds > (CivilDay(day.date) + 2) * 86400) return false;
        const Date local = DateAtUTC(prayer.utcSeconds, prayer.offsetMinutes);
        if (local != day.date && !(i == 4 && local == AddDays(day.date,1))) return false;
        if (i && prayer.utcSeconds <= day.prayers[i - 1].utcSeconds) return false;
    }
    if (day.hasSunrise && (day.sunrise.id != PrayerId::Sunrise || day.sunrise.offsetMinutes < -840 || day.sunrise.offsetMinutes > 840 ||
        DateAtUTC(day.sunrise.utcSeconds,day.sunrise.offsetMinutes) != day.date ||
        day.sunrise.utcSeconds <= day.prayers[0].utcSeconds || day.sunrise.utcSeconds >= day.prayers[1].utcSeconds)) return false;
    return true;
}
inline const Day* FindDay(const Data& data, Date date) { for (const auto& day : data.schedule.days) if (day.date == date && ValidDay(day)) return &day; return nullptr; }
inline const Day* CurrentDay(const Data& data, UTC now) {
    for (const auto& day : data.schedule.days) if (ValidDay(day) && DateAtUTC(now,day.offsetMinutes) == day.date) return &day;
    return nullptr;
}
inline std::wstring TimeText(UTC utc, int offset, bool twentyFourHour) {
    const UTC local = utc + static_cast<UTC>(offset) * 60, minute = (local - FloorDay(local) * 86400) / 60;
    const int hour = static_cast<int>(minute / 60), minutes = static_cast<int>(minute % 60);
    return twentyFourHour ? Two(hour) + L":" + Two(minutes) : std::to_wstring(hour % 12 ? hour % 12 : 12) + L":" + Two(minutes) + (hour >= 12 ? L" PM" : L" AM");
}
inline UTC Adjusted(const Prayer& prayer, const Configuration& cfg) { const auto id = static_cast<size_t>(prayer.id); return prayer.utcSeconds + (id < 5 ? static_cast<UTC>(cfg.adjustmentsMinutes[id]) * 60 : 0); }
inline View Compose(const Configuration& input, const Data& data, UTC now) {
    View view; Configuration cfg = input; std::wstring error;
    view.title = L"Prayer times";
    if (now < -11644473600LL || now > 253402214399LL) { view.secondary = view.error = L"Clock outside supported range"; return view; }
    view.nextUTCDeadline = now + 60;
    if (!Validate(cfg,error,true)) { view.secondary = error; view.error = error; return view; }
    view.title = cfg.city;
    if (data.configKey != ConfigurationKey(cfg)) { view.secondary = L"Waiting for this location's schedule"; return view; }
    const Day* day = CurrentDay(data,now);
    view.error = data.error; view.stale = data.stale;
    if (!day) { view.secondary = data.error.empty() ? L"Today's schedule is unavailable" : data.error; view.stale = !data.schedule.days.empty(); return view; }
    view.available = true; view.timeZone = day->timeZone; view.hijriDate = day->hijriDate;
    view.title += L" - " + DateText(day->date);
    const Prayer* next = nullptr; UTC nextUtc = (std::numeric_limits<UTC>::max)();
    for (const auto& scheduleDay : data.schedule.days) {
        if (!ValidDay(scheduleDay)) continue;
        for (const auto& prayer : scheduleDay.prayers) { const auto time = Adjusted(prayer,cfg); if (time >= now && time < nextUtc) { nextUtc = time; next = &prayer; } }
        if (cfg.nextIncludesSunrise && scheduleDay.hasSunrise && scheduleDay.sunrise.utcSeconds >= now && scheduleDay.sunrise.utcSeconds < nextUtc) {
            nextUtc = scheduleDay.sunrise.utcSeconds; next = &scheduleDay.sunrise;
        }
    }
    for (const auto& prayer : day->prayers) {
        view.detailRows.push_back({prayer.id,PrayerName(prayer.id),TimeText(Adjusted(prayer,cfg),prayer.offsetMinutes,cfg.twentyFourHour),next == &prayer});
        if (prayer.id == PrayerId::Fajr && cfg.showSunrise && day->hasSunrise)
            view.detailRows.push_back({PrayerId::Sunrise,L"Sunrise / Shuruq",TimeText(day->sunrise.utcSeconds,day->sunrise.offsetMinutes,cfg.twentyFourHour),next == &day->sunrise});
    }
    if (next) {
        const UTC minutes = (nextUtc - now + 59) / 60;
        view.clock.time = TimeText(nextUtc,next->offsetMinutes,cfg.twentyFourHour);
        view.clock.prayer = PrayerName(next->id); view.clock.location = cfg.city;
        view.clock.minutes = std::to_wstring(minutes);
        view.clock.countdown = L"in " + view.clock.minutes + (minutes == 1 ? L" minute" : L" minutes");
        view.clock.showTimetable = cfg.showTimetable;
        view.secondary = std::wstring(L"Next: ") + PrayerName(next->id) + L" " + TimeText(nextUtc,next->offsetMinutes,cfg.twentyFourHour) + L" - " + std::to_wstring(minutes) + L" min";
        view.nextUTCDeadline = minutes ? nextUtc - (minutes - 1) * 60 : now + 1;
    } else view.secondary = L"Next prayer schedule unavailable";
    view.badge = day->timeZone;
    if (cfg.showSunrise && day->hasSunrise) view.badge += L" | Sunrise " + TimeText(day->sunrise.utcSeconds,day->sunrise.offsetMinutes,cfg.twentyFourHour);
    if (view.stale) view.secondary += L" | Stale";
    if (!view.error.empty()) view.secondary += L" | " + view.error;
    if (cfg.showSunrise && day->hasSunrise) view.clock.details = L"Sunrise " + TimeText(day->sunrise.utcSeconds,day->sunrise.offsetMinutes,cfg.twentyFourHour);
    if (view.stale) view.clock.details += (view.clock.details.empty() ? L"" : L" | ") + std::wstring(L"Stale");
    if (!view.error.empty()) view.clock.details += (view.clock.details.empty() ? L"" : L" | ") + view.error;
    const UTC midnight = (CivilDay(day->date) + 1) * 86400 - day->offsetMinutes * 60;
    if (midnight > now) view.nextUTCDeadline = (std::min)(view.nextUTCDeadline,midnight);
    if (data.nextRefreshUtc > now) view.nextUTCDeadline = (std::min)(view.nextUTCDeadline,data.nextRefreshUtc);
    view.nextUTCDeadline = (std::max)(now + 1,view.nextUTCDeadline);
    return view;
}
inline UTC NextUTCDeadline(const Configuration& cfg, const Data& data, UTC now) { return Compose(cfg,data,now).nextUTCDeadline; }

// Versioned, ASCII-only cache. Text fields encode Unicode scalar values, so
// Windows UTF-16 and portable UTF-32 wchar_t hosts share one representation.
namespace cache_detail {
inline std::string Text(const std::wstring& value) {
    static const char hex[] = "0123456789abcdef"; std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        std::uint32_t cp = static_cast<std::uint32_t>(value[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < value.size()) cp = 0x10000 + ((cp - 0xD800) << 10) + (value[++i] - 0xDC00);
        for (int shift = 28; shift >= 0; shift -= 4) out += hex[(cp >> shift) & 15];
    }
    return out.empty() ? "-" : out;
}
inline bool Text(const std::string& encoded, std::wstring& value, size_t maximum) {
    value.clear(); if (encoded == "-") return true;
    if (encoded.size() % 8 || encoded.size() / 8 > maximum) return false;
    for (size_t i = 0; i < encoded.size(); i += 8) {
        std::uint32_t cp = 0;
        for (size_t n = 0; n < 8; ++n) { const char c = encoded[i+n]; int digit = c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : -1; if (digit < 0) return false; cp = cp * 16 + static_cast<unsigned>(digit); }
        if (cp < 0x20 || cp == 0x7F || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (sizeof(wchar_t) == 2 && cp > 0xFFFF) { cp -= 0x10000; value += static_cast<wchar_t>(0xD800 + (cp >> 10)); value += static_cast<wchar_t>(0xDC00 + (cp & 1023)); }
        else value += static_cast<wchar_t>(cp);
    }
    return value.size() <= maximum;
}
}
inline std::string SerializeCache(const Data& data) {
    if (data.configKey.empty() || !PlainText(data.configKey,2048) || data.schedule.days.size() > 3) return {};
    if (!PlainText(data.error,512)) return {};
    std::ostringstream out; out << "AIP_PRAYER_1\n" << cache_detail::Text(data.configKey) << '\n' << data.nextRefreshUtc << ' ' << data.lastSuccessUtc << ' ' << data.stale << '\n' << cache_detail::Text(data.error) << '\n' << data.schedule.days.size() << '\n';
    for (const auto& day : data.schedule.days) {
        if (!ValidDay(day)) return {};
        out << day.date.year << ' ' << day.date.month << ' ' << day.date.day << ' ' << day.offsetMinutes << ' ' << day.fetchedAtUtc << ' ' << day.hasSunrise << '\n';
        out << cache_detail::Text(day.timeZone) << '\n' << cache_detail::Text(day.hijriDate) << '\n';
        for (const auto& p : day.prayers) out << p.utcSeconds << ' ' << p.offsetMinutes << '\n';
        out << day.sunrise.utcSeconds << ' ' << day.sunrise.offsetMinutes << '\n';
    }
    return out.str().size() <= 65536 ? out.str() : std::string();
}
inline bool ParseCache(const std::string& bytes, const Configuration& cfg, Data& result, std::wstring& error) {
    result = {}; error = L"PrayerTimes cache is invalid or belongs to another configuration.";
    if (bytes.size() > 65536 || bytes.empty()) return false;
    std::istringstream in(bytes); std::string version, key, failure; Data data; size_t count = 0; int stale=0;
    if (!(in >> version >> key >> data.nextRefreshUtc >> data.lastSuccessUtc >> stale >> failure >> count) || version != "AIP_PRAYER_1" || count > 3 || (stale != 0 && stale != 1) ||
        !cache_detail::Text(failure,data.error,512) ||
        !cache_detail::Text(key,data.configKey,2048) || data.configKey != ConfigurationKey(cfg)) return false;
    data.stale = stale == 1;
    constexpr UTC minimum = -11644473600LL, maximum = 253402300799LL;
    if (data.nextRefreshUtc < minimum || data.nextRefreshUtc > maximum || data.lastSuccessUtc < minimum || data.lastSuccessUtc > maximum) return false;
    for (size_t i = 0; i < count; ++i) {
        Day day; int sunrise = 0; std::string zone,hijri;
        if (!(in >> day.date.year >> day.date.month >> day.date.day >> day.offsetMinutes >> day.fetchedAtUtc >> sunrise >> zone >> hijri) ||
            (sunrise != 0 && sunrise != 1) || day.fetchedAtUtc < minimum || day.fetchedAtUtc > maximum ||
            !cache_detail::Text(zone,day.timeZone,128) || !cache_detail::Text(hijri,day.hijriDate,128)) return false;
        day.hasSunrise = sunrise == 1;
        for (size_t p = 0; p < 5; ++p) { day.prayers[p].id = static_cast<PrayerId>(p); if (!(in >> day.prayers[p].utcSeconds >> day.prayers[p].offsetMinutes)) return false; }
        if (!(in >> day.sunrise.utcSeconds >> day.sunrise.offsetMinutes) || !ValidDay(day) || FindDay(data,day.date)) return false;
        data.schedule.days.push_back(std::move(day));
    }
    std::string trailing; if (in >> trailing) return false;
    result = std::move(data); error.clear(); return true;
}
} }
