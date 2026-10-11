#pragma once
#include <string>
#include <cwctype>
#include <algorithm>

namespace aip::secure_desktop {
inline std::wstring IdentityText(std::wstring value)
{
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    value = value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
// Identity, not deployment validation: an explicit uninstall must affect the
// installation represented by this native executable, including after UAC.
inline bool OwnsService(const std::wstring& command, const std::wstring& account, const std::wstring& executable)
{
    if (executable.empty()) return false;
    const auto actual = IdentityText(command);
    const auto expected = IdentityText(executable);
    const bool launchMatches = actual == L"\"" + expected + L"\"" ||
        (expected.find_first_of(L" \t\"") == std::wstring::npos && actual == expected);
    const auto identity = IdentityText(account);
    return launchMatches && (identity == L"localsystem" || identity == L"nt authority\\system");
}
}
