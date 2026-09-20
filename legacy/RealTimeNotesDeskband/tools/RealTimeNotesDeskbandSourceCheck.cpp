#include <algorithm>
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
    std::string text(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    text.erase(
        std::remove(text.begin(), text.end(), '\r'),
        text.end());
    return text;
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
        const std::string wrapper = ReadAll("RealTimeNotesDeskband.cpp");
        const std::string source = ReadAll("../../dependencies/RealTimeNotesDeskband/deskband_app.inc");
        const std::string notesSource = ReadAll("../../dependencies/content_sources/notes.inc");
        const std::string testScript = ReadAll("TestRealTimeNotesDeskbandSource.cmd");
        const std::string configureScript = ReadAll("ConfigureDeskband.cmd");
        const std::string readme = ReadAll("README.md");

        RequireContains(
            wrapper,
            "#include \"../../dependencies/RealTimeNotesDeskband/deskband_app.inc\"",
            "project entry point is a dependency overlay");
        RequireContains(
            source,
            "#include \"../desktop_app_baseline.h\"",
            "deskband consumes the shared desktop baseline");
        RequireContains(
            source,
            "#include \"../dpapi.inc\"",
            "deskband consumes the shared DPAPI serializer");
        RequireContains(
            source,
            "aip::DpapiLegacyEncoding::Utf16LittleEndian",
            "deskband explicitly identifies its historical UTF-16LE DPAPI format");
        RequireNotContains(
            source,
            "CryptProtectData(",
            "deskband does not keep a private DPAPI protect implementation");
        RequireNotContains(
            source,
            "CryptUnprotectData(",
            "deskband does not keep a private DPAPI unprotect implementation");
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
            "if (!PrepareRegistrationIni(installDir, configDir, assetDir, iniSnapshot))",
            "COM registration reports atomic INI persistence failures");
        RequireContains(
            source,
            "aip::IniWriteMutexGuard iniGuard(AppIniPath(), 5000);",
            "COM registration serializes its INI snapshot and rollback");
        RequireContains(
            source,
            "CaptureRegistryValue(HKEY_CURRENT_USER, target.subkey, target.valueName, snapshot)",
            "COM registration snapshots overwritten registry values");
        RequireContains(
            source,
            "RollBackRegistration(valueSnapshots, keySnapshots, iniSnapshot)",
            "COM registration rolls back registry and INI state after failure");
        RequireContains(
            source,
            "Write it only after every other\n    // registration and INI operation has succeeded",
            "COM registration writes the activation path last");
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
            "DeleteMigratedLegacyAccountKey(item.resource)",
            "verified DPAPI migration removes the legacy registry account key");
        RequireContains(
            source,
            "HasIniAccountConfig(item.resource) &&",
            "legacy plaintext cleanup follows protected INI verification");
        RequireContains(
            source,
            "Protected INI account is valid, but the legacy registry account key could not be removed",
            "failed legacy plaintext cleanup is retried and reported");
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
            notesSource,
            "if (text.size() > MAXDWORD)",
            "MD5 input length is validated before the CryptoAPI DWORD conversion");
        RequireContains(
            source,
            "while (offset < data.size())",
            "cookie JSON reads handle partial file reads");
        RequireContains(
            source,
            "aip::notes::Fetch(configuration)",
            "deskband consumes the shared deadline/cancellation-aware notes engine");
        RequireContains(
            source,
            "Could not open shell target:",
            "deskband reports shell activation failures");
        RequireContains(
            notesSource,
            "ParseAsciiInt(recovery, seconds)",
            "string recovery time uses strict integer parsing");
        RequireContains(
            notesSource,
            "aip::FindJsonFieldValue(json, key.c_str()",
            "notes JSON fields stay in their containing object");
        RequireNotContains(source, "static bool HttpGet(", "deskband does not retain a private synchronous HTTP engine");
        RequireContains(source, "using NoteState = aip::notes::Snapshot", "deskband consumes the shared snapshot model");
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
        RequireContains(
            source,
            "lstrcpynW(info->wszTitle, title.c_str(), ARRAYSIZE(info->wszTitle));",
            "deskband publishes state-bearing DBIM_TITLE text");
        RequireNotContains(
            source,
            "info->dwMask &= ~DBIM_TITLE",
            "deskband no longer suppresses its title text");
        RequireContains(
            source,
            "TOOLTIPS_CLASSW",
            "deskband creates a real hover tooltip control");
        RequireContains(
            source,
            "TTN_GETDISPINFOW",
            "deskband hover tooltip reads current state on demand");
        RequireContains(
            source,
            "ShowSettingsDialog(m_hwnd)",
            "deskband context menu exposes complete general settings");
        RequireContains(
            source,
            "SaveGeneralSettingsProfile(profile)",
            "deskband GUI and command configuration share atomic persistence");
        RequireContains(
            source,
            "ApplyGeneralSetting(profile, assignment.substr(0, equals), assignment.substr(equals + 1), error)",
            "deskband command line exposes typed general settings");
        RequireContains(
            source,
            "parsed < kMinRefreshSeconds",
            "deskband rejects out-of-range refresh settings instead of clamping them");
        RequireContains(
            source,
            "aip::WriteCommandLineText(message, error)",
            "deskband command output supports consoles and redirection");
        RequireContains(
            source,
            "ExitProcess(static_cast<UINT>(exitCode));",
            "deskband rundll32 controller returns meaningful CLI exit status");
        RequireContains(
            source,
            "#define RTN_COM_EXPORT STDAPI",
            "deskband COM exports compile with the installed MSVC SDK declarations");
        RequireContains(
            source,
            "#pragma comment(linker, \"/EXPORT:DllRegisterServer,PRIVATE\")",
            "deskband COM entry points avoid import-library linker warnings");
        RequireContains(
            source,
            "RTN_COM_EXPORT DllUnregisterServer()",
            "deskband registration exports share the compiler-compatible declaration");
        RequireContains(
            testScript,
            "where cl.exe",
            "deskband source checks support MSVC");
        RequireContains(
            testScript,
            "where g++",
            "deskband source checks retain MinGW portability");
        RequireContains(
            testScript,
            "/W4 /WX",
            "deskband MSVC source checks treat warnings as errors");
        RequireContains(
            configureScript,
            "exit /b %ERRORLEVEL%",
            "deskband command wrapper preserves rundll32 exit status");
        RequireContains(
            readme,
            "hover tooltip",
            "deskband hover behavior is documented");
        RequireContains(
            readme,
            "--set Name=Value",
            "deskband typed command configuration is documented");
        RequireContains(
            readme,
            "rolls back",
            "deskband registration recovery is documented");

        std::cout << "RealTimeNotesDeskband source checks passed (" << g_checks << " checks).\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
