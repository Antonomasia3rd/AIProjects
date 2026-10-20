#pragma once
#include "media_tile_layout.h"
namespace aip { namespace content {
inline bool PrayerTileShowsSchedule(int height, const std::vector<Text>& rows,
    const aip::prayer::ClockText& clock = {}) { return height > 200 && rows.size() == 5 && clock.showTimetable; }
struct PrayerClockGeometry {
    int margin = 10, mainWidth = 0, countdownX = 0, countdownWidth = 0;
    int timePixels = 24, countdownPixels = 46, clockBottom = 0, bottom = 0;
    bool countdownColumn = false, schedule = false;
};
inline PrayerClockGeometry ComputePrayerClockGeometry(int width, int height,
    const aip::prayer::ClockText& clock, const std::vector<Text>& rows) {
    PrayerClockGeometry layout;
    layout.bottom = (std::max)(layout.margin, height - 20);
    layout.mainWidth = (std::max)(0, width - 2 * layout.margin);
    layout.countdownColumn = width >= 250 && !clock.minutes.empty();
    layout.schedule = PrayerTileShowsSchedule(height, rows, clock);
    layout.timePixels = width >= 250 ? 34 : 24;
    layout.countdownPixels = clock.minutes.size() > 3 ? 24 : clock.minutes.size() > 2 ? 34 : 46;
    if (layout.countdownColumn) {
        layout.countdownWidth = 70;
        layout.countdownX = width - layout.margin - layout.countdownWidth;
        layout.mainWidth = (std::max)(0, layout.countdownX - layout.margin - 8);
    }
    layout.clockBottom = (std::min)(layout.bottom, layout.schedule ? 126 : layout.bottom);
    return layout;
}
inline std::wstring PrayerLocationText(const aip::prayer::ClockText& clock) {
    return clock.location.empty() ? std::wstring() : L"at " + clock.location;
}
inline const wchar_t* PrayerTimeStyle(bool countdownColumn) { return countdownColumn ? L"subheader" : L"title"; }
inline const wchar_t* PrayerCountdownStyle(const aip::prayer::ClockText& clock) {
    return clock.minutes.size() > 3 ? L"titleNumeral" : clock.minutes.size() > 2 ? L"subheaderNumeral" : L"headerNumeral";
}
inline void AppendPrayerClockText(std::wstring& xml, const std::wstring& value,
    const wchar_t* style, int lines = 1, const wchar_t* align = L"left") {
    if (value.empty()) return;
    xml += L"<text hint-style=\"" + std::wstring(style) + L"\" hint-wrap=\"true\" hint-maxLines=\"" +
        std::to_wstring(lines) + L"\" hint-align=\"" + align + L"\">" + MediaXmlEscape(value) + L"</text>";
}
inline bool AppendPrayerTileBinding(std::wstring& xml, const wchar_t* tileTemplate, int height,
    const Text& text, const std::vector<Text>& rows, const std::wstring& background = {},
    const aip::prayer::ClockText& clock = {}) {
    if (!tileTemplate || !*tileTemplate || height < 150) return false;
    xml += L"<binding template=\"" + std::wstring(tileTemplate) + L"\" hint-textStacking=\"top\">";
    if (!background.empty()) xml += L"<image placement=\"background\" src=\"" + MediaXmlEscape(background) + L"\"/>";
    if (clock.Available()) {
        const bool column = std::wstring(tileTemplate) != L"TileMedium" && !clock.minutes.empty();
        if (column) xml += L"<group><subgroup hint-weight=\"74\">";
        AppendPrayerClockText(xml, clock.time, PrayerTimeStyle(column));
        AppendPrayerClockText(xml, clock.prayer, L"body");
        AppendPrayerClockText(xml, PrayerLocationText(clock), L"captionSubtle", 2);
        if (column) {
            xml += L"</subgroup><subgroup hint-weight=\"26\">";
            AppendPrayerClockText(xml, L"minutes", L"captionSubtle", 1, L"right");
            AppendPrayerClockText(xml, clock.minutes, PrayerCountdownStyle(clock), 1, L"right");
            xml += L"</subgroup></group>";
        } else AppendPrayerClockText(xml, clock.countdown, L"caption");
        AppendPrayerClockText(xml, clock.details, L"captionSubtle", 2);
    } else {
        AppendPrayerClockText(xml, text.primary, L"caption", 2);
        AppendPrayerClockText(xml, text.secondary, L"captionSubtle");
        AppendPrayerClockText(xml, text.badge, L"captionSubtle", 2);
    }
    if (PrayerTileShowsSchedule(height, rows, clock)) for (const auto& row : rows) {
        xml += L"<group><subgroup hint-weight=\"60\"><text hint-style=\"caption\" hint-maxLines=\"1\">" + MediaXmlEscape(row.primary) +
            L"</text></subgroup><subgroup hint-weight=\"40\"><text hint-style=\"caption\" hint-align=\"right\" hint-maxLines=\"1\">" +
            MediaXmlEscape(row.secondary) + L"</text></subgroup></group>";
    }
    xml += L"</binding>"; return true;
}
} }
