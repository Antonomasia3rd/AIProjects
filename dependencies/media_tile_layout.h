#pragma once
#include "content_engine.h"
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
    result.iconX = width - result.margin - result.iconSide;
    result.textWidth = (std::max)(0.0, usable - (iconColumn ? result.iconSide + 8 : 0));
    result.extraY = result.iconY + result.iconSide + 8;
    return result;
}
struct MediaIndicatorImage { std::wstring uri, label; };
inline bool AppendMediaTileBinding(std::wstring& xml, const wchar_t* tileTemplate, const Text& text,
    const std::wstring& iconUri, const std::wstring& backgroundUri = {},
    const std::vector<MediaIndicatorImage>& extras = {}, const std::wstring& iconLabel = L"Playback status") {
    if (!tileTemplate || !*tileTemplate) return false;
    const bool icons = !iconUri.empty() || !extras.empty();
    xml += L"<binding template=\"" + std::wstring(tileTemplate) + L"\" hint-textStacking=\"top\">";
    if (!backgroundUri.empty()) xml += L"<image placement=\"background\" src=\"" + MediaXmlEscape(backgroundUri) + L"\"/>";
    if (icons) xml += L"<group><subgroup hint-weight=\"" + std::wstring(extras.empty() ? L"80" : L"88") + L"\" hint-textStacking=\"top\">";
    xml += L"<text hint-style=\"caption\" hint-wrap=\"true\" hint-maxLines=\"2\">" + MediaXmlEscape(text.primary) + L"</text>";
    if (!text.secondary.empty()) xml += L"<text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"2\">" + MediaXmlEscape(text.secondary) + L"</text>";
    if (!text.badge.empty()) xml += L"<text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"4\">" + MediaXmlEscape(text.badge) + L"</text>";
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
inline bool AppendDetailRowsBinding(std::wstring& xml, const wchar_t* tileTemplate, int height,
    const std::vector<Text>& rows, const std::wstring& backgroundUri, const std::wstring& topicIconUri = {}) {
    if (!tileTemplate || !*tileTemplate || rows.empty()) return false;
    xml += L"<binding template=\"" + std::wstring(tileTemplate) + L"\" hint-textStacking=\"top\">";
    if (!backgroundUri.empty()) xml += L"<image placement=\"background\" src=\"" + MediaXmlEscape(backgroundUri) + L"\"/>";
    for (std::size_t row = 0; row < (std::min)(DetailTileRows(height), rows.size()); ++row) {
        xml += L"<group><subgroup" + std::wstring(row == 0 && !topicIconUri.empty() ? L" hint-weight=\"80\"" : L"") + L" hint-textStacking=\"top\">";
        xml += L"<text hint-style=\"caption\" hint-wrap=\"true\" hint-maxLines=\"1\">" + MediaXmlEscape(rows[row].primary) + L"</text>";
        if (!rows[row].secondary.empty()) xml += L"<text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"1\">" + MediaXmlEscape(rows[row].secondary) + L"</text>";
        xml += L"</subgroup>";
        if (row == 0 && !topicIconUri.empty()) xml += L"<subgroup hint-weight=\"20\" hint-textStacking=\"top\"><image src=\"" + MediaXmlEscape(topicIconUri) + L"\" hint-removeMargin=\"true\"/></subgroup>";
        xml += L"</group>";
    }
    xml += L"</binding>"; return true;
}
inline bool AppendWrappedSourceBinding(std::wstring& xml, const wchar_t* tileTemplate, const Text& text,
    const std::wstring& backgroundUri = {}) {
    if (!tileTemplate || !*tileTemplate) return false;
    xml += L"<binding template=\"" + std::wstring(tileTemplate) + L"\" hint-textStacking=\"top\">";
    if (!backgroundUri.empty()) xml += L"<image placement=\"background\" src=\"" + MediaXmlEscape(backgroundUri) + L"\"/>";
    xml += L"<text hint-style=\"caption\" hint-wrap=\"true\" hint-maxLines=\"2\">" + MediaXmlEscape(text.primary) + L"</text>";
    if (!text.secondary.empty()) xml += L"<text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"5\">" + MediaXmlEscape(text.secondary) + L"</text>";
    xml += L"</binding>"; return true;
}
} }
