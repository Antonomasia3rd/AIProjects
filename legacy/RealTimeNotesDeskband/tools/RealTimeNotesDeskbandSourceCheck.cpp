#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

static int g_checks = 0;

static std::string ReadAll(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("missing source file: " + path);
    return std::string(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
}

static void RequireContains(const std::string& source, const std::string& needle, const char* name)
{
    ++g_checks;
    if (source.find(needle) == std::string::npos)
        throw std::runtime_error(std::string("RealTimeNotesDeskband source regression: ") + name);
    std::cout << "ok - " << name << "\n";
}

static void RequireNotContains(const std::string& source, const std::string& needle, const char* name)
{
    ++g_checks;
    if (source.find(needle) != std::string::npos)
        throw std::runtime_error(std::string("RealTimeNotesDeskband source regression: ") + name);
    std::cout << "ok - " << name << "\n";
}

int main()
{
    try
    {
        const std::string source = ReadAll("RealTimeNotesDeskband.cpp");

        RequireContains(
            source,
            "dependencies/desktop_app_baseline.h",
            "deskband consumes the shared desktop baseline");
        RequireNotContains(
            source,
            "GetPrivateProfileStringW",
            "deskband does not use Win32 profile INI readers");
        RequireNotContains(
            source,
            "WritePrivateProfileStringW",
            "deskband does not use Win32 profile INI writers");
        RequireContains(
            source,
            "path.resize((std::min)(path.size() * 2, static_cast<size_t>(32768)))",
            "deskband module paths grow beyond MAX_PATH");
        RequireContains(
            source,
            "CLSID_FileOpenDialog",
            "deskband file and folder pickers support long filesystem paths");
        RequireNotContains(
            source,
            "wchar_t fileName[MAX_PATH]",
            "cookie JSON picker does not truncate long paths");
        RequireContains(
            source,
            "if (!WriteSettingString(L\"InstallDir\", installDir))",
            "COM registration reports INI persistence failures");
        RequireContains(
            source,
            "result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND",
            "COM unregistration reports registry deletion failures");
        RequireContains(
            source,
            "aip::IniConfigStore(AppIniPath(), L\"\", 5000).MutateFresh",
            "account changes use atomic fresh INI mutations");
        RequireContains(
            source,
            "aip::RemoveIniSectionFromText",
            "account deletion uses shared section removal");
        RequireContains(
            source,
            "wcstoull(raw.c_str(), &end, 10)",
            "DWORD settings use overflow-aware parsing");
        RequireContains(
            source,
            "aip::TryUtf8ToWide",
            "UTF-8 decoding uses the shared checked conversion");
        RequireContains(
            source,
            "aip::TryWideToUtf8",
            "UTF-8 encoding uses the shared checked conversion");
        RequireContains(
            source,
            "aip::AppendUtf16LineToFile(",
            "deskband log records use the shared synchronized UTF-16 appender");
        RequireContains(
            source,
            "if (text.size() > MAXDWORD)",
            "MD5 input length is validated before the CryptoAPI DWORD conversion");
        RequireContains(
            source,
            "while (offset < data.size())",
            "cookie JSON reads handle partial file reads");
        RequireContains(
            source,
            "if (!WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000))",
            "deskband requires bounded WinHTTP timeouts");
        RequireContains(
            source,
            "Could not open shell target:",
            "deskband reports shell activation failures");
        RequireContains(
            source,
            "ParseAsciiInt(recovery, seconds)",
            "string recovery time uses strict integer parsing");
        RequireContains(
            source,
            "json[pos] != ','",
            "JSON integers require a valid trailing delimiter");
        RequireContains(
            source,
            "if (got == -1)",
            "account dialog handles message-loop errors");
        RequireContains(
            source,
            "PostQuitMessage(quitCode)",
            "account dialog preserves thread quit messages");
        RequireContains(
            source,
            "return state && CreateAccountDialogControls(state) ? 0 : -1;",
            "account dialog aborts when required controls fail");
        RequireContains(
            source,
            "if (!RegisterWindowClass())",
            "deskband window class registration is checked");
        RequireContains(
            source,
            "CloseDW(0);",
            "deskband destroys the old window before replacing its site");
        RequireContains(
            source,
            "if (SetTimer(hwnd, kRefreshTimer",
            "deskband refresh timer creation is checked");
        RequireContains(
            source,
            "band->m_windowToken == refresh->windowToken",
            "refresh completion is tied to the originating window generation");
        RequireContains(
            source,
            "aip::CriticalSectionLock",
            "deskband uses exception-safe shared critical-section locking");
        RequireContains(
            source,
            "m_state.refreshSeconds = refreshSeconds;\n        }\n\n        if (m_hwnd)",
            "loading-state lock is released before Explorer callbacks");
        RequireNotContains(
            source,
            "EnterCriticalSection(",
            "deskband does not leave critical sections locked when C++ copies throw");
        RequireContains(
            source,
            "Deskband refresh worker failed with a C++ exception.",
            "deskband refresh worker contains C++ exceptions");
        RequireContains(
            source,
            "InterlockedExchange(&band->m_refreshing, 0);",
            "deskband refresh worker always clears its in-progress flag");
        RequireContains(
            source,
            "Could not request a refresh for the replacement deskband window.",
            "deskband checks replacement-window refresh delivery");

        std::cout << "RealTimeNotesDeskband source checks passed (" << g_checks << " checks).\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
