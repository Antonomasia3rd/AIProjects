#pragma once
#include "content_engine.h"
namespace aip { namespace content {
inline std::wstring RssXmlEscape(const std::wstring& text);
// RSS Default is deliberately readable without requiring a hidden preset switch.
// Custom retains the user's compact/list composition and individual styles.
inline bool RssUsesRichLayout(const TextPresentation& presentation) {
    return presentation.preset == L"Default" || presentation.preset == L"RssHeadlines";
}
inline std::size_t RssRichTileRows(int width, int height) { return height > 200 ? 3u : width > 200 ? 2u : 1u; }
inline TextPresentation RssRichPresentation(const TextPresentation& supplied, int width, int height) {
    auto value = supplied;
    if (value.preset == L"Default") {
        value = TextPresentation{}; value.preset = L"RssHeadlines";
        value.primaryStyle = L"base"; value.primaryLines = 3; value.secondaryLines = 2;
        value.badgeLines = 1; value.gapLines = 0;
    }
    const bool wide = height <= 200 && width > 200;
    const bool large = height > 200;
    // Reserve room for a summary in every row, even after selecting a larger
    // title style. Windows and the bitmap fallback use these same row limits.
    const bool largerTitle = TextStylePixels(value.primaryStyle) > 15;
    value.primaryLines = (std::min)(value.primaryLines, wide ? (largerTitle || value.gapLines ? 1 : 2) : large ? 2 : 3);
    value.secondaryLines = (std::min)(value.secondaryLines, wide || (large && (largerTitle || value.gapLines > 1)) ? 1 : 2);
    value.badgeLines = 1;
    return value;
}
inline bool AppendRssRichTileBinding(std::wstring& xml, const wchar_t* binding, int height, const std::vector<Text>& headlines,
    const std::wstring& image, const TextPresentation& supplied = TextPresentation{}) {
    if (!binding || !*binding || headlines.empty()) return false;
    const int width = std::wstring(binding) == L"TileWide" || height > 200 ? 310 : 150;
    const auto presentation = RssRichPresentation(supplied, width, height);
    xml += L"<binding template=\"" + std::wstring(binding) + L"\" hint-textStacking=\"top\">";
    if (!image.empty()) xml += L"<image placement=\"background\" src=\"" + RssXmlEscape(image) + L"\"/>";
    const auto count = (std::min)(headlines.size(), RssRichTileRows(width, height));
    for (std::size_t i = 0; i < count; ++i) {
        // Separate Large entries without inserting a full blank line between
        // each title and its own summary. Wide already uses both compact rows.
        if (i && height > 200) xml += L"<text hint-style=\"caption\" hint-minLines=\"1\" hint-maxLines=\"1\"> </text>";
        const auto text = L"<group><subgroup><text>" + RssXmlEscape(headlines[i].primary) +
            L"</text>" + (headlines[i].secondary.empty() ? std::wstring{} : L"<text>" + RssXmlEscape(headlines[i].secondary) + L"</text>") +
            L"</subgroup></group>";
        xml += ApplyTextPresentationXml(text, presentation);
    }
    xml += L"</binding>"; return true;
}
inline std::size_t RssTileRows(int height) { return height > 200 ? 5u : 3u; }
inline std::wstring RssXmlEscape(const std::wstring& text) {
    std::wstring out;
    for (wchar_t c : text) switch(c) {
    case L'&': out += L"&amp;"; break; case L'<': out += L"&lt;"; break;
    case L'>': out += L"&gt;"; break; case L'\"': out += L"&quot;"; break;
    case L'\'': out += L"&apos;"; break; default: out += c;
    }
    return out;
}
inline bool AppendRssTileBinding(std::wstring& xml, const wchar_t* binding, int height,
    const std::vector<Text>& headlines, const std::wstring& image) {
    if (headlines.empty() || !binding || !*binding) return false;
    xml += L"    <binding template=\"" + std::wstring(binding) + L"\" hint-textStacking=\"top\">\r\n";
    if (!image.empty()) xml += L"      <image placement=\"background\" src=\"" + RssXmlEscape(image) + L"\" />\r\n";
    for (std::size_t index = 0; index < (std::min)(RssTileRows(height), headlines.size()); ++index) {
        xml += L"      <group><subgroup>\r\n        <text hint-style=\"caption\" hint-wrap=\"true\" hint-maxLines=\"2\">";
        xml += RssXmlEscape(std::to_wstring(index + 1) + L". " + headlines[index].primary);
        xml += L"</text>\r\n";
        if (height > 200 && !headlines[index].secondary.empty())
            xml += L"        <text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"1\">" + RssXmlEscape(headlines[index].secondary) + L"</text>\r\n";
        xml += L"      </subgroup></group>\r\n";
    }
    xml += L"    </binding>\r\n"; return true;
}
} }
