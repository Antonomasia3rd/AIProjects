#include "../dependencies/SecureDesktopLauncher/service_identity.h"
#include <iostream>
int main()
{
    using aip::secure_desktop::OwnsService;
    const std::wstring exe = L"C:\\Program Files\\Example\\SecureDesktopLauncher.exe";
    const bool ok = OwnsService(L"\"" + exe + L"\"", L"LocalSystem", exe) &&
        OwnsService(L" \"C:\\PROGRAM FILES\\Example\\SecureDesktopLauncher.exe\" ", L"NT AUTHORITY\\SYSTEM", exe) &&
        !OwnsService(exe, L"LocalSystem", exe) &&
        !OwnsService(L"\"" + exe + L"\" --other", L"LocalSystem", exe) &&
        !OwnsService(L"\"C:\\other\\SecureDesktopLauncher.exe\"", L"LocalSystem", exe) &&
        !OwnsService(L"\"" + exe + L"\"", L"SomeOtherAccount", exe) &&
        OwnsService(L"C:\\Tools\\service.exe", L"LocalSystem", L"C:\\Tools\\service.exe") &&
        !OwnsService(L"", L"LocalSystem", L"");
    std::cout << (ok ? "Passed 8 pure service identity cases.\n" : "Service identity case failed.\n");
    return ok ? 0 : 1;
}
