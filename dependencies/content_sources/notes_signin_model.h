#pragma once
#include "notes_model.h"
#include <algorithm>
#include <string>
#include <vector>

namespace aip { namespace notes {
inline bool ValidAccountServer(ResourceKind resource, const std::wstring& server) {
    const std::vector<const wchar_t*> choices = resource == ResourceKind::Resin ?
        std::vector<const wchar_t*>{L"os_usa", L"os_euro", L"os_asia", L"os_cht", L"cn_gf01", L"cn_qd01"} :
        resource == ResourceKind::Stamina ? std::vector<const wchar_t*>{L"prod_official_usa", L"prod_official_eur", L"prod_official_asia", L"prod_official_cht", L"prod_gf_cn", L"prod_qd_cn"} :
        resource == ResourceKind::Charge ? std::vector<const wchar_t*>{L"prod_gf_us", L"prod_gf_eu", L"prod_gf_jp", L"prod_gf_sg"} : std::vector<const wchar_t*>{};
    return std::any_of(choices.begin(), choices.end(), [&](const auto* item) { return server == item; });
}
struct SignInGame {
    const wchar_t* section;
    const wchar_t* name;
    const wchar_t* page;
    const wchar_t* business;
    const wchar_t* profile;
};
inline constexpr SignInGame SignInGames[] = {
    {L"Notes.SignIn.Resin", L"Genshin Impact", L"https://act.hoyolab.com/app/community-game-records-sea/index.html#/ys", L"hk4e_global", L"genshin"},
    {L"Notes.SignIn.Stamina", L"Honkai: Star Rail", L"https://act.hoyolab.com/app/community-game-records-sea/rpg/index.html#/hsr", L"hkrpg_global", L"hsr"},
    {L"Notes.SignIn.Charge", L"Zenless Zone Zero", L"https://act.hoyolab.com/app/mihoyo-zzz-game-record/index.html#/zzz", L"nap_global", L"zzz"}
};
inline std::wstring SignInLowerTrim(std::wstring value) {
    const auto space = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
    const auto first = std::find_if_not(value.begin(), value.end(), space);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), space).base();
    if (first >= last) return {};
    value = std::wstring(first, last);
    for (auto& c : value) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return value;
}
inline bool IsHoYoLabCookieDomain(std::wstring domain) {
    domain = SignInLowerTrim(domain);
    if (!domain.empty() && domain.front() == L'.') domain.erase(domain.begin());
    return domain == L"hoyolab.com" || (domain.size() > 12 && domain.compare(domain.size() - 12, 12, L".hoyolab.com") == 0);
}
inline bool KnownSignInSection(const std::wstring& section) {
    for (const auto& game : SignInGames) if (SignInLowerTrim(section) == SignInLowerTrim(game.section)) return true;
    return false;
}
inline bool NormalizeNotesSignInSetting(const std::wstring& section, const std::wstring& key,
    std::wstring& value, std::wstring& error) {
    error.clear();
    if (!KnownSignInSection(section) || SignInLowerTrim(key) != L"retainsession") {
        error = L"Unknown Notes sign-in preference."; return false;
    }
    const auto normalized = SignInLowerTrim(value);
    if (normalized == L"1" || normalized == L"true" || normalized == L"yes" || normalized == L"on") value = L"1";
    else if (normalized == L"0" || normalized == L"false" || normalized == L"no" || normalized == L"off") value = L"0";
    else { error = L"Notes sign-in RetainSession must be a Boolean."; return false; }
    return true;
}
template<class Reader> inline bool ValidateNotesSignInConfiguration(Reader read, std::wstring& error) {
    for (const auto& game : SignInGames) {
        auto value = read(game.section, L"RetainSession", L"0");
        if (!NormalizeNotesSignInSetting(game.section, L"RetainSession", value, error)) return false;
    }
    return true;
}
// UI/worker generations share cancellation: a result from a closed, reset, or
// superseded browser session must never become a saved account.
inline bool SignInResultCurrent(unsigned long long requested, unsigned long long current, bool closed, bool canceled) {
    return !closed && !canceled && requested == current;
}
enum class LocalCleanupPhase { Idle, AwaitingBlank, Clearing, Completed, Failed, Abandoned };
enum class LocalCleanupStep { None, ClearData, Failed };
class LocalBrowserCleanup {
    LocalCleanupPhase phase_ = LocalCleanupPhase::Idle;
    unsigned long long generation_ = 0, navigation_ = 0;
    bool hasNavigation_ = false, closeRequested_ = false;
public:
    bool Pending() const { return phase_ == LocalCleanupPhase::AwaitingBlank || phase_ == LocalCleanupPhase::Clearing; }
    LocalCleanupPhase Phase() const { return phase_; }
    bool CloseRequested() const { return closeRequested_; }
    bool Begin(unsigned long long generation) {
        if (Pending()) return false;
        generation_ = generation; navigation_ = 0; hasNavigation_ = false; closeRequested_ = false;
        phase_ = LocalCleanupPhase::AwaitingBlank; return true;
    }
    bool AllowNavigation(unsigned long long generation, const std::wstring& uri, unsigned long long navigation) {
        if (!Pending()) return true;
        if (generation != generation_ || phase_ != LocalCleanupPhase::AwaitingBlank || uri != L"about:blank") return false;
        if (hasNavigation_) return navigation == navigation_;
        navigation_ = navigation; hasNavigation_ = true; return true;
    }
    LocalCleanupStep NavigationCompleted(unsigned long long generation, unsigned long long navigation, bool success) {
        if (generation != generation_ || phase_ != LocalCleanupPhase::AwaitingBlank || !hasNavigation_ || navigation != navigation_)
            return LocalCleanupStep::None;
        phase_ = success ? LocalCleanupPhase::Clearing : LocalCleanupPhase::Failed;
        return success ? LocalCleanupStep::ClearData : LocalCleanupStep::Failed;
    }
    bool Complete(unsigned long long generation, bool success) {
        if (generation != generation_ || phase_ != LocalCleanupPhase::Clearing) return false;
        phase_ = success ? LocalCleanupPhase::Completed : LocalCleanupPhase::Failed; return true;
    }
    bool Fail(unsigned long long generation) {
        if (generation != generation_ || !Pending()) return false;
        phase_ = LocalCleanupPhase::Failed; return true;
    }
    bool RequestClose() {
        if (!Pending()) return true;
        closeRequested_ = true; return false;
    }
    void Abandon() { phase_ = LocalCleanupPhase::Abandoned; hasNavigation_ = false; closeRequested_ = false; }
};
} }
