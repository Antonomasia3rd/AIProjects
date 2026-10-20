#pragma once
#include "content_engine.h"
#include <cstdint>
#include <utility>
#include <mutex>
namespace aip { namespace content {
inline constexpr const wchar_t* TileBrandingChoices[] = {L"Auto", L"None", L"Logo", L"Name", L"NameAndLogo"};
inline bool NormalizeTileAppearanceSetting(const std::wstring& key, std::wstring& value, std::wstring& error) {
    const auto k = Lower(Trim(key)), v = Lower(Trim(value));
    if (k == L"background") {
        if (v == L"transparent" || v == L"color") { value = v == L"color" ? L"Color" : L"Transparent"; error.clear(); return true; }
        error = L"Background expects Transparent or Color.";
    } else if (k == L"branding") {
        for (const auto* choice : TileBrandingChoices) if (v == Lower(choice)) { value = choice; error.clear(); return true; }
        error = L"Branding expects Auto, None, Logo, Name, or NameAndLogo.";
    } else if (k == L"backgroundcolor") {
        if (v == L"auto") { value = L"Auto"; error.clear(); return true; }
        auto hex = v; if (!hex.empty() && hex.front() == L'#') hex.erase(0, 1);
        if (hex.size() == 6 && hex.find_first_not_of(L"0123456789abcdef") == std::wstring::npos) {
            for (auto& ch : hex) if (ch >= L'a' && ch <= L'f') ch = static_cast<wchar_t>(ch - L'a' + L'A');
            value = L"#" + hex; error.clear(); return true;
        }
        error = L"BackgroundColor expects Auto or a six-digit #RRGGBB color.";
    } else error = L"Unknown tile appearance setting.";
    return false;
}
template<class Reader> bool ReadTileAppearance(Reader read, const std::wstring& section, TileAppearance& result, std::wstring& error) {
    TileAppearance candidate;
    for (const auto& field : {std::make_pair(L"Background", &TileAppearance::background),
            std::make_pair(L"Branding", &TileAppearance::branding), std::make_pair(L"BackgroundColor", &TileAppearance::backgroundColor)}) {
        auto value = read(section.c_str(), field.first, (candidate.*field.second).c_str());
        if (!NormalizeTileAppearanceSetting(field.first, value, error)) { error = section + L"." + field.first + L": " + error; return false; }
        candidate.*field.second = std::move(value);
    }
    result = std::move(candidate); error.clear(); return true;
}
inline std::uint32_t TileAppearanceRgb(const TileAppearance& appearance, std::uint32_t topicColor) {
    const auto color = Lower(Trim(appearance.backgroundColor));
    if (color == L"auto") return topicColor & 0xffffffu;
    std::uint32_t rgb = 0;
    const auto start = !color.empty() && color.front() == L'#' ? 1u : 0u;
    if (color.size() - start != 6) return topicColor & 0xffffffu;
    for (std::size_t i = start; i < color.size(); ++i) {
        const auto ch = color[i];
        if (!((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f'))) return topicColor & 0xffffffu;
        rgb = (rgb << 4) | static_cast<std::uint32_t>(ch >= L'a' ? ch - L'a' + 10 : ch - L'0');
    }
    return rgb & 0xffffffu;
}
struct PublishedTileAppearance { std::uint32_t argb = 0; bool nameMedium = true, nameWide = true, nameLarge = true, darkText = false; };
inline PublishedTileAppearance PublishTileAppearance(const TileAppearance& appearance, std::uint32_t topicColor) {
    const auto rgb = TileAppearanceRgb(appearance, topicColor);
    const bool colored = appearance.background == L"Color";
    const auto brightness = ((rgb >> 16) & 255u) * 299u + ((rgb >> 8) & 255u) * 587u + (rgb & 255u) * 114u;
    const bool name = appearance.branding == L"Auto" || appearance.branding == L"Name" || appearance.branding == L"NameAndLogo";
    return {colored ? 0xff000000u | rgb : 0u, name, name, name, colored && brightness >= 160000u};
}
inline bool SamePublishedTileAppearance(const PublishedTileAppearance& a, const PublishedTileAppearance& b) {
    return a.argb == b.argb && a.nameMedium == b.nameMedium && a.nameWide == b.nameWide &&
        a.nameLarge == b.nameLarge && a.darkText == b.darkText;
}
template<class Visuals, class ColorFactory, class Foreground> void ApplyPublishedTileAppearance(Visuals& visuals,
        const PublishedTileAppearance& appearance, ColorFactory color, Foreground dark, Foreground light) {
    visuals.BackgroundColor(color(appearance.argb)); visuals.ForegroundText(appearance.darkText ? dark : light);
    visuals.ShowNameOnSquare150x150Logo(appearance.nameMedium); visuals.ShowNameOnWide310x150Logo(appearance.nameWide);
    visuals.ShowNameOnSquare310x310Logo(appearance.nameLarge);
}
// Cross-thread notice only; no native API, window, timer or I/O ownership.
class AppearanceRefreshNotice {
    std::mutex mutex_;
    std::wstring observed_, scheduled_;
    bool initialized_ = false, pending_ = false;
public:
    bool Observe(const std::wstring& fingerprint) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) { observed_ = scheduled_ = fingerprint; initialized_ = true; return false; }
        observed_ = fingerprint;
        if (pending_ || observed_ == scheduled_) return false;
        pending_ = true; return true;
    }
    void PostFailed() { std::lock_guard<std::mutex> lock(mutex_); pending_ = false; }
    bool Consume() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pending_) return false;
        pending_ = false;
        if (observed_ == scheduled_) return false;
        scheduled_ = observed_; return true;
    }
    void RecordScheduled(const std::wstring& fingerprint) {
        std::lock_guard<std::mutex> lock(mutex_);
        observed_ = scheduled_ = fingerprint; initialized_ = true;
    }
};
} }
