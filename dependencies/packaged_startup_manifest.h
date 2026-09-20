#pragma once
#include <string>

namespace aip
{
inline constexpr const wchar_t* DesktopStartupNamespace =
    L"http://schemas.microsoft.com/appx/manifest/desktop/windows10";

// Pure fragment builder. The host owns the Package namespace declaration and
// application Extensions container. The initial state is ALWAYS disabled;
// generating/registering this XML does not opt the user into sign-in launch.
template <typename Escape>
inline std::wstring BuildDesktopStartupExtension(
    const std::wstring& taskId, const std::wstring& executable,
    const std::wstring& displayName, Escape escape)
{
    return L"        <desktop:Extension Category=\"windows.startupTask\" Executable=\"" +
        escape(executable) + L"\" EntryPoint=\"Windows.FullTrustApplication\">\r\n"
        L"          <desktop:StartupTask TaskId=\"" + escape(taskId) +
        L"\" Enabled=\"false\" DisplayName=\"" + escape(displayName) + L"\" />\r\n"
        L"        </desktop:Extension>\r\n";
}
}
