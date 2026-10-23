#pragma once
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <map>
#include <string>

namespace aip::notifications {
enum class Api { Auto, Balloon, Toast };
inline Api ParseApi(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return value == L"balloon" ? Api::Balloon : value == L"toast" ? Api::Toast : Api::Auto;
}
inline Api EffectiveApi(Api requested, bool existingIdentity) {
    return requested != Api::Balloon && existingIdentity ? Api::Toast : Api::Balloon;
}
inline std::wstring StableId(const std::wstring& value) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (wchar_t ch : value) { hash ^= static_cast<uint32_t>(ch); hash *= UINT64_C(1099511628211); }
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring result(16, L'0');
    for (int i = 15; i >= 0; --i) { result[static_cast<size_t>(i)] = digits[hash & 15]; hash >>= 4; }
    return result; // Fits old Windows 10 tag/group limits too.
}
inline std::wstring Escape(const std::wstring& value, size_t limit = 1024) {
    std::wstring result;
    for (size_t i = 0; i < value.size() && i < limit; ++i) {
        const wchar_t ch = value[i];
        if (ch == L'&') result += L"&amp;";
        else if (ch == L'<') result += L"&lt;";
        else if (ch == L'>') result += L"&gt;";
        else if (ch == L'\"') result += L"&quot;";
        else if (ch == L'\'') result += L"&apos;";
        else if (ch >= 0x20 || ch == L'\n' || ch == L'\r' || ch == L'\t') result += ch;
    }
    return result;
}
inline std::wstring MessageXml(const std::wstring& title, const std::wstring& message) {
    return L"<toast><visual><binding template=\"ToastGeneric\"><text>" + Escape(title, 128) +
        L"</text><text>" + Escape(message) + L"</text></binding></visual><audio silent=\"true\"/></toast>";
}
inline std::wstring ProgressXml(const std::wstring& title) {
    return L"<toast><visual><binding template=\"ToastGeneric\"><text>" + Escape(title, 128) +
        L"</text><progress value=\"{progressValue}\" valueStringOverride=\"{progressCount}\" status=\"{progressStatus}\"/>"
        L"</binding></visual><audio silent=\"true\"/></toast>";
}
struct Progress {
    std::wstring key, title, status;
    size_t completed = 0, total = 0;
    bool finished = false, failed = false, first = true;
    uint32_t sequence = 1;
    std::wstring Count() const { return std::to_wstring(completed) + L" / " + std::to_wstring(total); }
    std::wstring Value() const {
        if (!total) return L"indeterminate";
        // Integer formatting is independent of the user's decimal locale.
        const auto thousandths = static_cast<unsigned>((static_cast<long double>(std::min(completed, total)) * 1000) / total);
        return std::to_wstring(thousandths / 1000) + L"." +
            static_cast<wchar_t>(L'0' + (thousandths / 100) % 10) +
            static_cast<wchar_t>(L'0' + (thousandths / 10) % 10) + static_cast<wchar_t>(L'0' + thousandths % 10);
    }
    std::wstring BalloonText() const { return Count() + (status.empty() ? L"" : L"\n" + status); }
};
class ProgressTracker {
    struct State { Progress value; uint64_t emittedAt = 0; };
    std::map<std::wstring, State> states_;
public:
    bool Next(Progress value, uint64_t now, uint64_t interval, Progress& emitted) {
        auto found = states_.find(value.key);
        const bool first = found == states_.end() || found->second.value.finished;
        if (!first && !value.finished && now - found->second.emittedAt < interval) return false;
        if (!first && !value.finished && found->second.value.completed == value.completed && found->second.value.total == value.total &&
            found->second.value.status == value.status && found->second.value.title == value.title) return false;
        if (value.total) value.completed = std::min(value.completed, value.total);
        value.first = first;
        value.sequence = first ? 1 : (found->second.value.sequence == UINT32_MAX ? UINT32_MAX : found->second.value.sequence + 1);
        // A bounded operation table prevents abandoned/custom operation IDs from accumulating.
        if (first && states_.size() >= 64 && found == states_.end()) states_.erase(states_.begin());
        states_[value.key] = {value, now}; emitted = std::move(value); return true;
    }
    void Clear() { states_.clear(); }
};
} // namespace aip::notifications
