#pragma once
#include <stdexcept>
#include <string>

namespace aip
{
struct AppxRegistrationSpec
{
    std::wstring manifestPath;
    bool forceUpdateFromAnyVersion = true;
};

// Pure command construction: never registers a package. Both desktop hosts
// pass PowerShellSingleQuotedString so $, backticks, quotes, and spaces in a
// manifest path remain literal data. A terminating error prevents a later
// status query from turning a failed registration into apparent success.
template <typename LiteralQuoter>
inline std::wstring BuildAppxRegistrationScript(const AppxRegistrationSpec& spec, LiteralQuoter quote)
{
    if (spec.manifestPath.empty() || spec.manifestPath.find(L'\0') != std::wstring::npos)
        throw std::invalid_argument("An AppX registration manifest path must be nonempty and contain no NUL.");
    std::wstring script = L"Add-AppxPackage -Register " + quote(spec.manifestPath);
    if (spec.forceUpdateFromAnyVersion) script += L" -ForceUpdateFromAnyVersion";
    script += L" -ErrorAction Stop; ";
    return script;
}
}
