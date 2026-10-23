#pragma once
#include "media_tile_layout.h"
namespace aip { namespace content {
inline bool PrayerTileShowsSchedule(int height, const std::vector<Text>& rows,
    const aip::prayer::ClockText& clock = {}) { return height > 200 && rows.size() >= 5 && rows.size() <= 6 && clock.showTimetable; }
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
    layout.timePixels = 24;
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
inline const wchar_t* PrayerTimeStyle(bool) { return L"title"; }
inline const wchar_t* PrayerCountdownStyle(const aip::prayer::ClockText& clock) {
    return clock.minutes.size() > 3 ? L"titleNumeral" : clock.minutes.size() > 2 ? L"subheaderNumeral" : L"headerNumeral";
}
// Stable semantic roles: both the prayer name and location use Secondary.
// The small countdown label remains a caption when the time size is customized.
enum class PrayerFieldRole { Time, Secondary, Badge, Fixed };
inline std::wstring PrayerFieldStyle(const TextPresentation& p, PrayerFieldRole role, const wchar_t* fallback) {
    if (!p.Active() || role == PrayerFieldRole::Fixed) return fallback;
    return role == PrayerFieldRole::Time ? p.primaryStyle : role == PrayerFieldRole::Secondary ? p.secondaryStyle : p.badgeStyle;
}
inline int PrayerFieldLines(const TextPresentation& p, PrayerFieldRole role, int fallback) {
    if (!p.Active() || role == PrayerFieldRole::Fixed) return fallback;
    return p.wrap ? (role == PrayerFieldRole::Time ? p.primaryLines : role == PrayerFieldRole::Secondary ? p.secondaryLines : p.badgeLines) : 1;
}
inline void AppendPrayerClockText(std::wstring& xml, const std::wstring& value,
    const wchar_t* defaultStyle, int lines = 1, const wchar_t* align = L"left",
    const TextPresentation& p = {}, PrayerFieldRole role = PrayerFieldRole::Fixed) {
    if (value.empty()) return;
    const auto style = PrayerFieldStyle(p,role,defaultStyle);
    if (p.Active() && role != PrayerFieldRole::Time && role != PrayerFieldRole::Fixed && p.gapLines)
        xml += L"<text hint-style=\"caption\" hint-minLines=\"" + std::to_wstring(p.gapLines) + L"\" hint-maxLines=\"" + std::to_wstring(p.gapLines) + L"\"> </text>";
    const auto alignment = p.Active() && std::wstring(align) != L"right" ? p.align : std::wstring(align);
    xml += L"<text hint-style=\"" + style + L"\" hint-wrap=\"" + (p.Active() && !p.wrap ? L"false" : L"true") + L"\" hint-maxLines=\"" +
        std::to_wstring(PrayerFieldLines(p,role,lines)) + L"\" hint-align=\"" + alignment + L"\">" + MediaXmlEscape(value) + L"</text>";
}
inline bool AppendPrayerTileBinding(std::wstring& xml, const wchar_t* tileTemplate, int height,
    const Text& text, const std::vector<Text>& rows, const std::wstring& background = {},
    const aip::prayer::ClockText& clock = {}, const TextPresentation& p = {}) {
    if (!tileTemplate || !*tileTemplate || height < 150) return false;
    xml += L"<binding template=\"" + std::wstring(tileTemplate) + L"\" hint-textStacking=\"top\">";
    if (!background.empty()) xml += L"<image placement=\"background\" src=\"" + MediaXmlEscape(background) + L"\"/>";
    if (clock.Available()) {
        const bool column = std::wstring(tileTemplate) != L"TileMedium" && !clock.minutes.empty();
        if (column) xml += L"<group><subgroup hint-weight=\"74\">";
        AppendPrayerClockText(xml, clock.time, PrayerTimeStyle(column),1,L"left",p,PrayerFieldRole::Time);
        AppendPrayerClockText(xml, clock.prayer, L"captionSubtle",1,L"left",p,PrayerFieldRole::Secondary);
        AppendPrayerClockText(xml, PrayerLocationText(clock), L"captionSubtle",2,L"left",p,PrayerFieldRole::Secondary);
        if (column) {
            xml += L"</subgroup><subgroup hint-weight=\"26\">";
            AppendPrayerClockText(xml, L"minutes", L"captionSubtle", 1, L"right",p);
            AppendPrayerClockText(xml, clock.minutes, PrayerCountdownStyle(clock), 1, L"right",p,PrayerFieldRole::Badge);
            xml += L"</subgroup></group>";
        } else AppendPrayerClockText(xml, clock.countdown, L"caption",1,L"left",p,PrayerFieldRole::Badge);
        AppendPrayerClockText(xml, clock.details, L"captionSubtle", 2,L"left",p,PrayerFieldRole::Secondary);
    } else {
        AppendPrayerClockText(xml, text.primary, L"caption", 2,L"left",p,PrayerFieldRole::Time);
        AppendPrayerClockText(xml, text.secondary, L"captionSubtle",1,L"left",p,PrayerFieldRole::Secondary);
        AppendPrayerClockText(xml, text.badge, L"captionSubtle", 2,L"left",p,PrayerFieldRole::Secondary);
    }
    if (PrayerTileShowsSchedule(height, rows, clock)) for (const auto& row : rows) {
        xml += L"<group><subgroup hint-weight=\"60\">";
        AppendPrayerClockText(xml,row.primary,L"caption",1,L"left",p,PrayerFieldRole::Secondary);
        xml += L"</subgroup><subgroup hint-weight=\"40\">";
        AppendPrayerClockText(xml,row.secondary,L"caption",1,L"right",p,PrayerFieldRole::Secondary);
        xml += L"</subgroup></group>";
    }
    xml += L"</binding>"; return true;
}
} }
