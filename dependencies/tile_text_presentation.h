#pragma once
// Opt-in adaptive text policy. No platform APIs or content/provider dependency.
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>
namespace aip { namespace content {
inline constexpr const wchar_t* TextPresentationStyles[] = {L"caption",L"captionSubtle",L"body",L"bodySubtle",L"base",L"baseSubtle",
    L"subtitle",L"subtitleSubtle",L"title",L"titleSubtle",L"titleNumeral",L"subheader",L"subheaderSubtle",L"subheaderNumeral",L"header",L"headerSubtle",L"headerNumeral"};
inline constexpr const wchar_t* TextPresentationKeys[] = {L"TextPreset",L"TextStyle",L"TextSecondaryStyle",L"TextBadgeStyle",L"TextWrap",
    L"TextMaxLines",L"TextSecondaryMaxLines",L"TextBadgeMaxLines",L"TextAlign",L"TextStacking",L"TextGapLines"};
struct TextPresentation {
    std::wstring preset = L"Default", primaryStyle = L"caption", secondaryStyle = L"captionSubtle", badgeStyle = L"captionSubtle";
    bool wrap = true;
    int primaryLines = 2, secondaryLines = 2, badgeLines = 2, gapLines = 0;
    std::wstring align = L"left", stacking = L"top";
    bool Active() const { return preset != L"Default"; }
};
inline std::wstring TextPresentationLower(std::wstring value) {
    const auto first = value.find_first_not_of(L" \r\n\t");
    if (first == std::wstring::npos) return {};
    value = value.substr(first, value.find_last_not_of(L" \r\n\t") - first + 1);
    for (auto& ch : value) ch = static_cast<wchar_t>(std::towlower(ch)); return value;
}
inline bool IsTextPresentationKey(const std::wstring& key) {
    for (const auto* known : TextPresentationKeys) if (TextPresentationLower(key) == TextPresentationLower(known)) return true;
    return false;
}
inline bool NormalizeTextPresentationSetting(const std::wstring& key, std::wstring& value, std::wstring& error, bool inherit = false) {
    const auto k = TextPresentationLower(key), v = TextPresentationLower(value);
    error = key + L": invalid text presentation value.";
    if (k == L"textpreset") {
        for (const auto* choice : {L"Default",L"Custom",L"RssHeadlines",L"Inherit"}) if (v == TextPresentationLower(choice) && (inherit || std::wstring(choice) != L"Inherit")) { value = choice; error.clear(); return true; }
    } else if (k == L"textstyle" || k == L"textsecondarystyle" || k == L"textbadgestyle") {
        for (const auto* style : TextPresentationStyles) if (v == TextPresentationLower(style)) { value = style; error.clear(); return true; }
    } else if (k == L"textwrap") {
        if (v == L"true" || v == L"yes" || v == L"on" || v == L"1") value = L"1";
        else if (v == L"false" || v == L"no" || v == L"off" || v == L"0") value = L"0"; else return false;
        error.clear(); return true;
    } else if (k == L"textalign" || k == L"textstacking") {
        const std::vector<std::wstring> choices = k == L"textalign" ? std::vector<std::wstring>{L"left",L"center",L"right"} : std::vector<std::wstring>{L"top",L"center",L"bottom"};
        if (std::find(choices.begin(), choices.end(), v) != choices.end()) { value = v; error.clear(); return true; }
    } else if (k == L"textmaxlines" || k == L"textsecondarymaxlines" || k == L"textbadgemaxlines" || k == L"textgaplines") {
        const bool gap = k == L"textgaplines";
        if (v.size() == 1 && v[0] >= (gap ? L'0' : L'1') && v[0] <= (gap ? L'2' : L'8')) { value = v; error.clear(); return true; }
    }
    return false;
}
template<class Reader> bool ReadTextPresentation(Reader read, const std::wstring& section, TextPresentation& output, std::wstring& error, const TextPresentation* inherited = nullptr) {
    TextPresentation value = inherited ? *inherited : TextPresentation{};
    auto preset = read(section.c_str(), L"TextPreset", inherited ? L"Inherit" : L"Default");
    if (!NormalizeTextPresentationSetting(L"TextPreset", preset, error, inherited != nullptr)) return false;
    if (preset != L"Inherit") value.preset = preset;
    if (preset == L"RssHeadlines") { value.primaryStyle = L"base"; value.primaryLines = 3; value.secondaryLines = 2; value.badgeLines = 1; value.wrap = true; value.gapLines = 0; }
    const auto get = [&](const wchar_t* key, const std::wstring& fallback, std::wstring& target) {
        auto candidate = read(section.c_str(), key, fallback.c_str());
        if (!NormalizeTextPresentationSetting(key, candidate, error, inherited != nullptr)) return false;
        target = std::move(candidate); return true;
    };
    if (!get(L"TextStyle", value.primaryStyle, value.primaryStyle) || !get(L"TextSecondaryStyle", value.secondaryStyle, value.secondaryStyle) ||
        !get(L"TextBadgeStyle", value.badgeStyle, value.badgeStyle) || !get(L"TextAlign", value.align, value.align) || !get(L"TextStacking", value.stacking, value.stacking)) return false;
    std::wstring parsed;
    if (!get(L"TextWrap", value.wrap ? L"1" : L"0", parsed)) return false; value.wrap = parsed == L"1";
    if (!get(L"TextMaxLines", std::to_wstring(value.primaryLines), parsed)) return false; value.primaryLines = parsed[0] - L'0';
    if (!get(L"TextSecondaryMaxLines", std::to_wstring(value.secondaryLines), parsed)) return false; value.secondaryLines = parsed[0] - L'0';
    if (!get(L"TextBadgeMaxLines", std::to_wstring(value.badgeLines), parsed)) return false; value.badgeLines = parsed[0] - L'0';
    if (!get(L"TextGapLines", std::to_wstring(value.gapLines), parsed)) return false; value.gapLines = parsed[0] - L'0';
    output = std::move(value); error.clear(); return true;
}
inline std::wstring TextPresentationKey(const TextPresentation& p) {
    return p.preset+L":"+p.primaryStyle+L":"+p.secondaryStyle+L":"+p.badgeStyle+L":"+std::to_wstring(p.wrap)+L":"+
        std::to_wstring(p.primaryLines)+L":"+std::to_wstring(p.secondaryLines)+L":"+std::to_wstring(p.badgeLines)+L":"+p.align+L":"+p.stacking+L":"+std::to_wstring(p.gapLines);
}
inline int TextStylePixels(const std::wstring& style) {
    if (style.compare(0,9,L"subheader") == 0) return 34;
    if (style.compare(0,6,L"header") == 0) return 46;
    if (style.compare(0,8,L"subtitle") == 0) return 20;
    if (style.compare(0,5,L"title") == 0) return 24;
    return style.compare(0,7,L"caption") == 0 ? 12 : 15;
}
inline void TextPresentationAttribute(std::wstring& tag, const std::wstring& key, const std::wstring& value) {
    const auto marker = L" " + key + L"=\""; const auto at = tag.find(marker);
    if (at == std::wstring::npos) tag.insert(tag.size()-1, marker + value + L"\"");
    else { const auto start = at + marker.size(), end = tag.find(L'\"',start); if (end != std::wstring::npos) tag.replace(start,end-start,value); }
}
inline std::wstring ApplyTextPresentationXml(const std::wstring& xml, const TextPresentation& p) {
    if (!p.Active()) return xml;
    std::wstring out; std::size_t pos = 0; unsigned role = 0;
    while (pos < xml.size()) {
        const auto begin = xml.find(L'<',pos); if (begin == std::wstring::npos) { out += xml.substr(pos); break; }
        out += xml.substr(pos,begin-pos); const auto end = xml.find(L'>',begin); if (end == std::wstring::npos) { out += xml.substr(begin); break; }
        auto tag = xml.substr(begin,end-begin+1);
        if (tag.compare(0,8,L"<binding") == 0 || tag.compare(0,9,L"<subgroup") == 0) { TextPresentationAttribute(tag,L"hint-textStacking",p.stacking); role = 0; }
        if (tag.compare(0,6,L"<group") == 0) role = 0;
        if (tag.compare(0,5,L"<text") == 0) {
            if (role && p.gapLines) out += L"<text hint-style=\"caption\" hint-minLines=\""+std::to_wstring(p.gapLines)+L"\" hint-maxLines=\""+std::to_wstring(p.gapLines)+L"\"> </text>";
            TextPresentationAttribute(tag,L"hint-style",role == 0 ? p.primaryStyle : role == 1 ? p.secondaryStyle : p.badgeStyle);
            TextPresentationAttribute(tag,L"hint-wrap",p.wrap ? L"true" : L"false");
            TextPresentationAttribute(tag,L"hint-maxLines",std::to_wstring(role == 0 ? p.primaryLines : role == 1 ? p.secondaryLines : p.badgeLines));
            TextPresentationAttribute(tag,L"hint-align",p.align); ++role;
        }
        out += tag; pos = end + 1;
    }
    return out;
}
} }
