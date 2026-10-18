#pragma once
#include "media_tile_layout.h"
namespace aip { namespace content {
inline bool PrayerTileShowsSchedule(int height, const std::vector<Text>& rows) { return height > 200 && rows.size() == 5; }
inline bool AppendPrayerTileBinding(std::wstring& xml, const wchar_t* tileTemplate, int height,
    const Text& text, const std::vector<Text>& rows, const std::wstring& background = {}) {
    if (!tileTemplate || !*tileTemplate || height < 150) return false;
    xml += L"<binding template=\"" + std::wstring(tileTemplate) + L"\" hint-textStacking=\"top\">";
    if (!background.empty()) xml += L"<image placement=\"background\" src=\"" + MediaXmlEscape(background) + L"\"/>";
    xml += L"<text hint-style=\"caption\" hint-wrap=\"true\" hint-maxLines=\"2\">" + MediaXmlEscape(text.primary) + L"</text>";
    if (!text.secondary.empty()) xml += L"<text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"1\">" + MediaXmlEscape(text.secondary) + L"</text>";
    if (!text.badge.empty()) xml += L"<text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"2\">" + MediaXmlEscape(text.badge) + L"</text>";
    if (PrayerTileShowsSchedule(height, rows)) for (const auto& row : rows) {
        xml += L"<group><subgroup hint-weight=\"60\"><text hint-style=\"caption\" hint-maxLines=\"1\">" + MediaXmlEscape(row.primary) +
            L"</text></subgroup><subgroup hint-weight=\"40\"><text hint-style=\"caption\" hint-align=\"right\" hint-maxLines=\"1\">" +
            MediaXmlEscape(row.secondary) + L"</text></subgroup></group>";
    }
    xml += L"</binding>"; return true;
}
} }
