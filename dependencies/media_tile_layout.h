#pragma once
#include "content_engine.h"
namespace aip { namespace content {
inline bool AppendMediaTileBinding(std::wstring& xml, const wchar_t* tileTemplate, const Text& text,
    const std::wstring& iconUri, const std::wstring& backgroundUri = {}) {
    if (!tileTemplate || iconUri.empty()) return false;
    xml += L"<binding template=\"" + std::wstring(tileTemplate) + L"\" hint-textStacking=\"center\">";
    if (!backgroundUri.empty()) xml += L"<image placement=\"background\" src=\"" + XmlEscape(backgroundUri) + L"\"/>";
    xml += L"<group><subgroup hint-weight=\"80\" hint-textStacking=\"center\">";
    xml += L"<text hint-style=\"caption\" hint-wrap=\"true\" hint-maxLines=\"2\">" + XmlEscape(text.primary) + L"</text>";
    xml += L"<text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"2\">" + XmlEscape(text.secondary) + L"</text>";
    xml += L"<text hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"2\">" + XmlEscape(text.badge) + L"</text>";
    xml += L"</subgroup><subgroup hint-weight=\"20\" hint-textStacking=\"center\"><image src=\"" + XmlEscape(iconUri) + L"\" hint-removeMargin=\"true\"/></subgroup></group></binding>";
    return true;
}
} }
