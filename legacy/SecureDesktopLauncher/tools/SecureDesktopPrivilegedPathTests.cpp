#define NOMINMAX

#include "../../../dependencies/privileged_path_trust.h"

#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;

static void Check(bool condition, const char* name)
{
    if (condition)
    {
        std::cout << "ok - " << name << "\n";
        return;
    }
    std::cerr << "not ok - " << name << "\n";
    ++g_failures;
}

static std::wstring CurrentExecutablePath()
{
    std::vector<wchar_t> buffer(512);
    for (;;)
    {
        DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return L"";
        }
        if (length < buffer.size() - 1)
        {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(buffer.size() * 2);
    }
}

int wmain()
{
    std::vector<wchar_t> systemDirectory(32768);
    UINT systemLength = GetSystemDirectoryW(
        systemDirectory.data(),
        static_cast<UINT>(systemDirectory.size()));
    Check(
        systemLength > 0 && systemLength < systemDirectory.size(),
        "system directory is available");
    if (systemLength == 0 || systemLength >= systemDirectory.size())
    {
        return 1;
    }

    std::wstring cmdPath(systemDirectory.data(), systemLength);
    cmdPath += L"\\cmd.exe";
    aip::PrivilegedPathPin pin;
    std::wstring error;
    Check(
        aip::OpenPinnedProtectedFile(
            cmdPath,
            aip::PrivilegedRootPolicy::WindowsOrProgramFiles,
            L"System32 fixture",
            pin,
            error),
        "Windows-root policy accepts protected System32 executable");
    Check(
        !aip::OpenPinnedProtectedFile(
            cmdPath,
            aip::PrivilegedRootPolicy::ProgramFilesOnly,
            L"System32 fixture",
            pin,
            error),
        "Program-Files-only policy rejects System32 executable");

    const std::wstring current = CurrentExecutablePath();
    Check(
        !current.empty() &&
            !aip::OpenPinnedProtectedFile(
                current,
                aip::PrivilegedRootPolicy::WindowsOrProgramFiles,
                L"AppData test executable",
                pin,
                error),
        "privileged policy rejects the AppData checkout binary");
    Check(
        !aip::OpenPinnedProtectedFile(
            current + L":untrusted",
            aip::PrivilegedRootPolicy::WindowsOrProgramFiles,
            L"alternate-stream fixture",
            pin,
            error),
        "privileged policy rejects alternate data streams");
    Check(
        !aip::OpenPinnedProtectedFile(
            L"cmd.exe",
            aip::PrivilegedRootPolicy::WindowsOrProgramFiles,
            L"relative fixture",
            pin,
            error),
        "privileged policy rejects relative paths");

    if (g_failures != 0)
    {
        std::cerr << "SecureDesktop privileged-path tests failed: "
                  << g_failures << "\n";
        return 1;
    }
    std::cout << "SecureDesktop privileged-path tests passed.\n";
    return 0;
}
