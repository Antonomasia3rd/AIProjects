#pragma once
#include "content_engine.h"
#include "standard_tile_layout.h"
namespace aip { namespace content {
inline std::wstring MediaXmlEscape(const std::wstring& text) {
    std::wstring out;
    for (const auto ch : text) switch (ch) {
    case L'&': out += L"&amp;"; break; case L'<': out += L"&lt;"; break;
    case L'>': out += L"&gt;"; break; case L'\"': out += L"&quot;"; break;
    case L'\'': out += L"&apos;"; break; default: out += ch; break;
    }
    return out;
}
inline const wchar_t* MediaStatusLabel(MediaPlaybackState state) {
    switch (state) {
    case MediaPlaybackState::Playing: return L"Playing";
    case MediaPlaybackState::Paused: return L"Paused";
    case MediaPlaybackState::Stopped: return L"Stopped";
    default: return L"Playback status unavailable";
    }
}
inline const wchar_t* MediaStatusTopic(MediaPlaybackState state) {
    switch (state) {
    case MediaPlaybackState::Playing: return L"play";
    case MediaPlaybackState::Paused: return L"pause";
    case MediaPlaybackState::Stopped: return L"stop";
    default: return L"unknown";
    }
}
inline const wchar_t* MediaRepeatTopic(MediaRepeatMode repeat) {
    switch (repeat) {
    case MediaRepeatMode::Off: return L"repeat-off";
    case MediaRepeatMode::List: return L"repeat-list";
    case MediaRepeatMode::Track: return L"repeat-track";
    default: return L"";
    }
}
inline const wchar_t* MediaShuffleTopic(MediaToggleState shuffle) {
    return shuffle == MediaToggleState::On ? L"shuffle-on" : shuffle == MediaToggleState::Off ? L"shuffle-off" : L"";
}
inline std::vector<std::wstring> MediaExtraTopics(const Snapshot& snapshot) {
    std::vector<std::wstring> result;
    if (!snapshot.smtcShowRepeatShuffle) return result;
    if (*MediaRepeatTopic(snapshot.smtcRepeat)) result.push_back(MediaRepeatTopic(snapshot.smtcRepeat));
    if (*MediaShuffleTopic(snapshot.smtcShuffle)) result.push_back(MediaShuffleTopic(snapshot.smtcShuffle));
    return result;
}
inline std::wstring MediaStatusCaption(const Snapshot& snapshot) {
    auto caption = std::wstring(MediaStatusLabel(snapshot.smtcState));
    if (!snapshot.smtcAppName.empty()) caption += L" - " + snapshot.smtcAppName;
    if (snapshot.smtcShowRepeatShuffle) {
        switch (snapshot.smtcRepeat) {
        case MediaRepeatMode::Off: caption += L"\nRepeat off"; break;
        case MediaRepeatMode::List: caption += L"\nRepeat list"; break;
        case MediaRepeatMode::Track: caption += L"\nRepeat track"; break;
        default: break;
        }
        if (snapshot.smtcShuffle != MediaToggleState::Unknown)
            caption += snapshot.smtcShuffle == MediaToggleState::On ? L"\nShuffle on" : L"\nShuffle off";
    }
    return caption;
}
struct MediaTileGeometry {
    double margin = 10, textX = 10, textY = 10, textWidth = 0, textHeight = 0;
    double iconX = 0, iconY = 10, iconSide = 0, extraY = 0;
};
inline MediaTileGeometry ComputeMediaTileGeometry(int width, int height, bool iconColumn, std::size_t extraCount = 0) {
    MediaTileGeometry result;
    if (width < 1 || height < 1) return result;
    result.textHeight = (std::max)(0.0, static_cast<double>(height) - 30);
    const double usable = (std::max)(0.0, static_cast<double>(width) - 20);
    result.iconSide = iconColumn ? (std::min)(48.0, usable * (extraCount ? .12 : .2)) : 0;
    // The bitmap path has an 8px gap between every indicator and reserves the
    // Registration name/logo footer. Three Wide icons must not overlap it.
    const double iconHeight=(std::max)(0.0,static_cast<double>(height)-result.margin-26.0-8.0*static_cast<double>(extraCount));
    result.iconSide=(std::min)(result.iconSide,iconHeight/(1.0+static_cast<double>(extraCount)));
    result.iconX = width - result.margin - result.iconSide;
    result.textWidth = (std::max)(0.0, usable - (iconColumn ? result.iconSide + 8 : 0));
    result.extraY = result.iconY + result.iconSide + 8;
    return result;
}
struct MediaIndicatorImage { std::wstring uri, label; };
inline bool AppendMediaTileBinding(std::wstring& xml, const wchar_t* tileTemplate, const Text& text,
    const std::wstring& iconUri, const std::wstring& backgroundUri = {},
    const std::vector<MediaIndicatorImage>& extras = {}, const std::wstring& iconLabel = L"Playback status",bool metadata=false) {
    if (!tileTemplate || !*tileTemplate) return false;
    const bool icons = !iconUri.empty() || !extras.empty();
    const auto budget=StatusMediaTextBudget(tileTemplate,metadata||!extras.empty());
    xml += L"<binding template=\"" + std::wstring(tileTemplate) + L"\" hint-textStacking=\"top\">";
    if (!backgroundUri.empty()) xml += L"<image placement=\"background\" src=\"" + MediaXmlEscape(backgroundUri) + L"\"/>";
    if (icons) xml += L"<group><subgroup hint-weight=\"" + std::wstring(extras.empty() ? L"80" : L"88") + L"\" hint-textStacking=\"top\">";
    xml += L"<text hint-style=\""+std::wstring(StatusTilePrimaryStyle(tileTemplate))+L"\" hint-wrap=\"true\" hint-maxLines=\""+std::to_wstring(budget.primary)+L"\">" + MediaXmlEscape(text.primary) + L"</text>";
    if (!text.secondary.empty()) xml += L"<text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\""+std::to_wstring(budget.secondary)+L"\">" + MediaXmlEscape(text.secondary) + L"</text>";
    if (!text.badge.empty()) xml += L"<text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\""+std::to_wstring(budget.badge)+L"\">" + MediaXmlEscape(text.badge) + L"</text>";
    if (icons) {
        xml += L"</subgroup><subgroup hint-weight=\"" + std::wstring(extras.empty() ? L"20" : L"12") + L"\" hint-textStacking=\"top\">";
        if (!iconUri.empty()) xml += L"<image src=\"" + MediaXmlEscape(iconUri) + L"\" alt=\"" + MediaXmlEscape(iconLabel) + L"\" hint-removeMargin=\"true\"/>";
        for (const auto& extra : extras) if (!extra.uri.empty())
            xml += L"<image src=\"" + MediaXmlEscape(extra.uri) + L"\" alt=\"" + MediaXmlEscape(extra.label) + L"\" hint-removeMargin=\"true\"/>";
        xml += L"</subgroup></group>";
    }
    xml += L"</binding>"; return true;
}
inline std::size_t DetailTileRows(int height) { return height > 200 ? 5u : 3u; }
struct DetailRowGeometry { double iconX = 10, iconY = 10, iconSide = 0, textX = 10, textY = 10, textWidth = 0, bottom = 0; };
inline DetailRowGeometry ComputeDetailRowGeometry(int width, int height, std::size_t count, std::size_t row, bool icon) {
    DetailRowGeometry result;
    if (width <= 20 || height <= 30 || !count || row >= count) return result;
    const auto rowHeight = static_cast<double>(height - 30) / static_cast<double>(count);
    result.iconY = result.textY = 10 + static_cast<double>(row) * rowHeight;
    result.iconSide = icon ? (std::max)(0.0, (std::min)(24.0, rowHeight - 4)) : 0;
    result.textX = 10 + (result.iconSide > 0 ? result.iconSide + 6 : 0);
    result.textWidth = (std::max)(0.0, width - 10 - result.textX);
    result.bottom = result.textY + rowHeight - 3;
    return result;
}
inline bool AppendDetailRowsBinding(std::wstring& xml, const wchar_t* tileTemplate, int height,
    const std::vector<Text>& rows, const std::wstring& backgroundUri, const std::wstring& topicIconUri = {},
    const std::vector<MediaIndicatorImage>& rowIcons = {}) {
    if (!tileTemplate || !*tileTemplate || rows.empty()) return false;
    xml += L"<binding template=\"" + std::wstring(tileTemplate) + L"\" hint-textStacking=\"top\">";
    if (!backgroundUri.empty()) xml += L"<image placement=\"background\" src=\"" + MediaXmlEscape(backgroundUri) + L"\"/>";
    for (std::size_t row = 0; row < (std::min)(DetailTileRows(height), rows.size()); ++row) {
        const auto icon = row < rowIcons.size() ? rowIcons[row] : MediaIndicatorImage{row == 0 ? topicIconUri : L"", L""};
        xml += L"<group>";
        if (!icon.uri.empty()) xml += L"<subgroup hint-weight=\"16\" hint-textStacking=\"top\"><image src=\"" + MediaXmlEscape(icon.uri) +
            L"\" alt=\"" + MediaXmlEscape(icon.label) + L"\" hint-removeMargin=\"true\"/></subgroup>";
        xml += L"<subgroup" + std::wstring(!icon.uri.empty() ? L" hint-weight=\"84\"" : L"") + L" hint-textStacking=\"top\">";
        xml += L"<text hint-style=\"caption\" hint-wrap=\"true\" hint-maxLines=\"1\">" + MediaXmlEscape(rows[row].primary) + L"</text>";
        if (!rows[row].secondary.empty()) xml += L"<text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"1\">" + MediaXmlEscape(rows[row].secondary) + L"</text>";
        xml += L"</subgroup>";
        xml += L"</group>";
    }
    xml += L"</binding>"; return true;
}
inline bool AppendWrappedSourceBinding(std::wstring& xml, const wchar_t* tileTemplate, const Text& text,
    const std::wstring& backgroundUri = {},const wchar_t* primaryStyle=nullptr,int secondaryLines=0) {
    if (!tileTemplate || !*tileTemplate) return false;
    xml += L"<binding template=\"" + std::wstring(tileTemplate) + L"\" hint-textStacking=\"top\">";
    if (!backgroundUri.empty()) xml += L"<image placement=\"background\" src=\"" + MediaXmlEscape(backgroundUri) + L"\"/>";
    xml += L"<text hint-style=\""+std::wstring(primaryStyle?primaryStyle:StatusTilePrimaryStyle(tileTemplate))+L"\" hint-wrap=\"true\" hint-maxLines=\"2\">" + MediaXmlEscape(text.primary) + L"</text>";
    if (!text.secondary.empty()) xml += L"<text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\""+std::to_wstring(secondaryLines>0?secondaryLines:StatusWrappedSecondaryLines(tileTemplate))+L"\">" + MediaXmlEscape(text.secondary) + L"</text>";
    xml += L"</binding>"; return true;
}
} }
