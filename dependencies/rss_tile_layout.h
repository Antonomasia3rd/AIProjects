#pragma once
#include "content_engine.h"
namespace aip { namespace content {
inline std::wstring RssXmlEscape(const std::wstring& text);
inline bool AppendRssRichTileBinding(std::wstring& xml, const wchar_t* binding, int height, const std::vector<Text>& headlines, const std::wstring& image) {
    if (!binding || !*binding || headlines.empty()) return false;
    xml += L"<binding template=\"" + std::wstring(binding) + L"\" hint-textStacking=\"top\">";
    if (!image.empty()) xml += L"<image placement=\"background\" src=\"" + RssXmlEscape(image) + L"\"/>";
    for (std::size_t i = 0; i < (std::min)(headlines.size(), height > 200 ? std::size_t(3) : std::size_t(1)); ++i)
        xml += L"<group><subgroup><text hint-style=\"body\" hint-wrap=\"true\" hint-maxLines=\"3\">" + RssXmlEscape(std::to_wstring(i+1)+L". "+headlines[i].primary) +
            L"</text><text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"2\">" + RssXmlEscape(headlines[i].secondary) + L"</text></subgroup></group>";
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
