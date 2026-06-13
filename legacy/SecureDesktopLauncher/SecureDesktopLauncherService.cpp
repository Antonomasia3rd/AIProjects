#define UNICODE
#define _UNICODE
#define NOMINMAX

#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "../../dependencies/desktop_app_baseline.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")

static const wchar_t* kServiceName = L"SecureDesktopLauncher";
static const wchar_t* kServiceDisplayName = L"Secure Desktop Launcher";

static SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
static SERVICE_STATUS gStatus = {};
static HANDLE gStopEvent = nullptr;
static HANDLE gEnsureWorkersDrainedEvent = nullptr;
static std::atomic<bool> gStopping{ true };
static std::mutex gServiceStatusMutex;
static std::mutex gStopEventMutex;
static std::mutex gEnsureWorkersMutex;
static size_t gEnsureWorkerCount = 0;

struct SessionContext
{
    DWORD id = 0;
    WTS_CONNECTSTATE_CLASS state = WTSDisconnected;
    std::wstring userName;
    std::wstring domain;
    std::wstring account;
};

struct ProgramConfig
{
    std::wstring name;
    bool enabled = true;
    std::wstring path;
    std::wstring arguments;
    std::wstring commandLine;
    std::wstring workingDirectory;
    std::wstring desktop;
    std::vector<std::wstring> includeUsers;
    std::vector<std::wstring> excludeUsers;
    bool preventDuplicate = true;
    bool stopOnServiceStop = true;
    DWORD launchSpacingMs = 3000;
    int showWindow = SW_SHOWNOACTIVATE;
};

struct AppConfig
{
    std::wstring configPath;
    std::wstring desktop = L"WinSta0\\Winlogon";
    DWORD launchSpacingMs = 3000;
    DWORD maxProgramsPerSession = 0;
    bool stopOnServiceStop = true;
    bool startOnServiceStart = true;
    bool startOnConsoleConnect = true;
    bool startOnRemoteConnect = true;
    bool startOnLogon = true;
    bool startOnLock = true;
    bool startOnUnlock = true;
    bool launchDisconnectedSessions = true;
    std::vector<std::wstring> includeUsers;
    std::vector<std::wstring> excludeUsers;
    std::vector<ProgramConfig> programs;
};

struct LaunchedProcessRecord
{
    DWORD pid = 0;
    std::wstring path;
    FILETIME creationTime = {};
};

static std::mutex gLaunchedProcessesMutex;
static std::vector<LaunchedProcessRecord> gLaunchedProcesses;

static void SetServiceState(DWORD state, DWORD win32ExitCode = NO_ERROR, DWORD waitHint = 0)
{
    std::lock_guard<std::mutex> lock(gServiceStatusMutex);
    static DWORD checkpoint = 1;
    gStatus.dwCurrentState = state;
    gStatus.dwWin32ExitCode = win32ExitCode;
    gStatus.dwWaitHint = waitHint;
    gStatus.dwControlsAccepted = 0;
    gStatus.dwCheckPoint =
        state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING
            ? checkpoint++
            : 0;

    if (state == SERVICE_RUNNING) {
        gStatus.dwControlsAccepted =
            SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE;
    }

    if (gStatusHandle) {
        if (!SetServiceStatus(gStatusHandle, &gStatus)) {
            OutputDebugStringW(L"SecureDesktopLauncher: SetServiceStatus failed.\n");
        }
    }
}

static bool SignalStopEvent() noexcept
{
    std::lock_guard<std::mutex> lock(gStopEventMutex);
    return gStopEvent == nullptr || SetEvent(gStopEvent) != FALSE;
}

static std::wstring Trim(const std::wstring& value)
{
    size_t begin = 0;
    while (begin < value.size() && iswspace(value[begin])) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && iswspace(value[end - 1])) {
        --end;
    }

    return value.substr(begin, end - begin);
}

static std::wstring Quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

static std::wstring NormalizePath(std::wstring path)
{
    path = Trim(path);
    if (path.rfind(L"\\\\?\\", 0) == 0) {
        path.erase(0, 4);
    }

    for (wchar_t& ch : path) {
        if (ch == L'/') {
            ch = L'\\';
        }
    }

    return path;
}

static std::wstring BaseName(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

static std::wstring BaseNameWithoutExtension(const std::wstring& path)
{
    std::wstring name = BaseName(path);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        name.resize(dot);
    }
    return name.empty() ? L"SecureDesktopLauncher" : name;
}

static bool FileExists(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool DirectoryExists(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static std::wstring CurrentExePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            return L"";
        }
        if (len < buffer.size() - 1) {
            return std::wstring(buffer.data(), len);
        }
        buffer.resize(buffer.size() * 2);
    }
}

static std::wstring DirectoryOf(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

static std::wstring CombinePath(const std::wstring& left, const std::wstring& right)
{
    if (left.empty()) {
        return right;
    }
    wchar_t last = left[left.size() - 1];
    if (last == L'\\' || last == L'/') {
        return left + right;
    }
    return left + L"\\" + right;
}

static std::wstring DefaultLogPath()
{
    std::wstring exePath = CurrentExePath();
    return CombinePath(DirectoryOf(exePath), BaseNameWithoutExtension(exePath) + L".log");
}

static void LogServiceWarning(const std::wstring& message)
{
    std::wstring debugLine = std::wstring(kServiceName) + L": " + message + L"\n";
    OutputDebugStringW(debugLine.c_str());

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timestamp[64];
    swprintf_s(timestamp, ARRAYSIZE(timestamp), L"%04u-%02u-%02u %02u:%02u:%02u  ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    (void)aip::AppendUtf16LineToFile(
        DefaultLogPath(),
        std::wstring(timestamp) + message,
        false,
        5000);
}

static void LogServiceWarningNoThrow(const wchar_t* message) noexcept
{
    try {
        LogServiceWarning(message ? message : L"Unknown service error.");
    }
    catch (...) {
        OutputDebugStringW(L"SecureDesktopLauncher: logging failed while handling an exception.\n");
    }
}

static void LogServiceExceptionNoThrow(const wchar_t* context, const std::exception& ex) noexcept
{
    try {
        LogServiceWarning(std::wstring(context ? context : L"Service callback failed: ") +
            aip::Utf8ToWide(ex.what()));
    }
    catch (...) {
        OutputDebugStringW(L"SecureDesktopLauncher: exception reporting failed.\n");
    }
}

static bool IsAbsoluteFileSystemPath(const std::wstring& path)
{
    std::wstring value = path;
    if (value.rfind(L"\\\\?\\", 0) == 0)
    {
        value.erase(0, 4);
    }

    return value.size() >= 3 &&
        iswalpha(value[0]) &&
        value[1] == L':' &&
        (value[2] == L'\\' || value[2] == L'/');
}

static bool NormalizeFullPath(const std::wstring& path, std::wstring& fullPath)
{
    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0)
    {
        return false;
    }

    std::vector<wchar_t> buffer(needed + 1);
    DWORD written = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (written == 0 || written >= buffer.size())
    {
        return false;
    }

    fullPath.assign(buffer.data(), written);
    return true;
}

static std::wstring TrimTrailingDirectorySeparators(std::wstring path)
{
    while (path.size() > 3 &&
        (path[path.size() - 1] == L'\\' || path[path.size() - 1] == L'/'))
    {
        path.resize(path.size() - 1);
    }
    return path;
}

static bool ExistingAncestorDirectories(std::wstring path)
{
    path = TrimTrailingDirectorySeparators(path);
    return path.empty() || DirectoryExists(path);
}

static bool ExistingFilePath(const std::wstring& path)
{
    if (!IsAbsoluteFileSystemPath(path))
    {
        LogServiceWarning(L"Configured file path is not a local absolute path: " + path);
        return false;
    }

    std::wstring fullPath;
    if (!NormalizeFullPath(path, fullPath))
    {
        LogServiceWarning(L"Could not normalize configured file path: " + path);
        return false;
    }

    if (!FileExists(fullPath))
    {
        LogServiceWarning(L"Configured file does not exist: " + path);
        return false;
    }

    std::wstring dir = DirectoryOf(fullPath);
    return !dir.empty() && ExistingAncestorDirectories(dir);
}

static bool ExistingDirectoryPath(const std::wstring& path)
{
    if (!IsAbsoluteFileSystemPath(path))
    {
        LogServiceWarning(L"Configured directory path is not a local absolute path: " + path);
        return false;
    }

    std::wstring fullPath;
    if (!NormalizeFullPath(path, fullPath))
    {
        LogServiceWarning(L"Could not normalize configured directory path: " + path);
        return false;
    }

    return ExistingAncestorDirectories(fullPath);
}

static std::wstring FindConfigPath()
{
    std::wstring exeDir = DirectoryOf(CurrentExePath());
    return CombinePath(exeDir, BaseNameWithoutExtension(CurrentExePath()) + L".ini");
}

static std::wstring ReadIniString(
    const std::vector<aip::IniSectionData>& document,
    const std::wstring& section,
    const std::wstring& key,
    const std::wstring& defaultValue)
{
    std::wstring value;
    return Trim(aip::ReadIniValueFromDoc(
        document,
        section.c_str(),
        key.c_str(),
        value)
        ? value
        : defaultValue);
}

static bool ReadIniBool(
    const std::vector<aip::IniSectionData>& document,
    const std::wstring& section,
    const std::wstring& key,
    bool defaultValue)
{
    bool value = false;
    return aip::ParseBoolValue(ReadIniString(document, section, key, L""), value)
        ? value
        : defaultValue;
}

static DWORD ReadIniDword(
    const std::vector<aip::IniSectionData>& document,
    const std::wstring& section,
    const std::wstring& key,
    DWORD defaultValue)
{
    std::wstring raw = ReadIniString(document, section, key, L"");
    if (raw.empty()) {
        return defaultValue;
    }

    errno = 0;
    wchar_t* end = nullptr;
    unsigned long long value = wcstoull(raw.c_str(), &end, 10);
    return end != raw.c_str() &&
        end != nullptr &&
        *end == L'\0' &&
        errno != ERANGE &&
        value <= MAXDWORD
        ? static_cast<DWORD>(value)
        : defaultValue;
}

static int ReadIniInt(
    const std::vector<aip::IniSectionData>& document,
    const std::wstring& section,
    const std::wstring& key,
    int defaultValue)
{
    int value = 0;
    return aip::ParseIntValue(ReadIniString(document, section, key, L""), value)
        ? value
        : defaultValue;
}

static std::vector<std::wstring> SplitList(const std::wstring& value)
{
    std::vector<std::wstring> result;
    size_t start = 0;

    while (start <= value.size()) {
        size_t pos = value.find_first_of(L",;", start);
        std::wstring item = Trim(value.substr(start, pos == std::wstring::npos ? pos : pos - start));
        if (!item.empty()) {
            result.push_back(item);
        }
        if (pos == std::wstring::npos) {
            break;
        }
        start = pos + 1;
    }

    return result;
}

static bool WildcardMatchInsensitive(const wchar_t* pattern, const wchar_t* text)
{
    while (*pattern) {
        if (*pattern == L'*') {
            while (*pattern == L'*') {
                ++pattern;
            }
            if (!*pattern) {
                return true;
            }
            while (*text) {
                if (WildcardMatchInsensitive(pattern, text)) {
                    return true;
                }
                ++text;
            }
            return false;
        }

        if (!*text) {
            return false;
        }

        if (*pattern != L'?' && towlower(*pattern) != towlower(*text)) {
            return false;
        }

        ++pattern;
        ++text;
    }

    return *text == L'\0';
}

static bool UserListMatches(const std::vector<std::wstring>& patterns, const SessionContext& session)
{
    for (std::wstring pattern : patterns) {
        pattern = Trim(pattern);
        if (pattern.empty()) {
            continue;
        }

        for (wchar_t& ch : pattern) {
            if (ch == L'/') {
                ch = L'\\';
            }
        }

        if (pattern == L"*") {
            return true;
        }

        std::wstring target;
        if (pattern.find(L'\\') != std::wstring::npos) {
            target = session.account;
        } else if (pattern.find(L'@') != std::wstring::npos) {
            target = session.userName + L"@" + session.domain;
        } else {
            target = session.userName;
        }

        if (!target.empty() && WildcardMatchInsensitive(pattern.c_str(), target.c_str())) {
            return true;
        }
    }

    return false;
}

static bool UserAllowed(
    const SessionContext& session,
    const std::vector<std::wstring>& includeUsers,
    const std::vector<std::wstring>& excludeUsers)
{
    if (!includeUsers.empty() && !UserListMatches(includeUsers, session)) {
        return false;
    }

    if (!excludeUsers.empty() && UserListMatches(excludeUsers, session)) {
        return false;
    }

    return true;
}

static std::wstring ReplaceAll(std::wstring value, const std::wstring& from, const std::wstring& to)
{
    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::wstring::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
    return value;
}

static std::wstring ExpandTokens(std::wstring value, const ProgramConfig& program, const SessionContext& session)
{
    value = ReplaceAll(value, L"{ProgramName}", program.name);
    value = ReplaceAll(value, L"{SessionId}", std::to_wstring(session.id));
    value = ReplaceAll(value, L"{UserName}", session.userName);
    value = ReplaceAll(value, L"{Domain}", session.domain);
    value = ReplaceAll(value, L"{Account}", session.account);
    return value;
}

static bool EnablePrivilege(const wchar_t* name)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid)) {
        CloseHandle(token);
        return false;
    }

    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    DWORD err = GetLastError();
    CloseHandle(token);
    return err == ERROR_SUCCESS;
}

static bool IsLocalSystemProcess(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return false;
    }

    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        CloseHandle(process);
        return false;
    }

    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    std::vector<BYTE> userBuffer(needed);
    bool result = false;

    if (GetTokenInformation(token, TokenUser, userBuffer.data(), needed, &needed)) {
        BYTE systemSidBuffer[SECURITY_MAX_SID_SIZE] = {};
        DWORD systemSidSize = sizeof(systemSidBuffer);
        if (CreateWellKnownSid(WinLocalSystemSid, nullptr, systemSidBuffer, &systemSidSize)) {
            auto tokenUser = reinterpret_cast<TOKEN_USER*>(userBuffer.data());
            result = EqualSid(tokenUser->User.Sid, systemSidBuffer) != FALSE;
        }
    }

    CloseHandle(token);
    CloseHandle(process);
    return result;
}

static bool ProcessPathEquals(DWORD pid, const std::wstring& expectedPath)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return false;
    }

    std::vector<wchar_t> path(32768);
    DWORD pathLen = static_cast<DWORD>(path.size());
    bool result = false;

    if (QueryFullProcessImageNameW(process, 0, path.data(), &pathLen)) {
        std::wstring actual = NormalizePath(std::wstring(path.data(), pathLen));
        std::wstring expected = NormalizePath(expectedPath);
        result = _wcsicmp(actual.c_str(), expected.c_str()) == 0;
    }

    CloseHandle(process);
    return result;
}

static bool TryGetProcessCreationTime(HANDLE process, FILETIME& creationTime)
{
    FILETIME exitTime = {};
    FILETIME kernelTime = {};
    FILETIME userTime = {};
    return GetProcessTimes(process, &creationTime, &exitTime, &kernelTime, &userTime) != FALSE;
}

static bool FileTimeEquals(const FILETIME& left, const FILETIME& right)
{
    return left.dwLowDateTime == right.dwLowDateTime &&
        left.dwHighDateTime == right.dwHighDateTime;
}

static bool RecordLaunchedProcess(const ProgramConfig& program, const PROCESS_INFORMATION& pi) noexcept
{
    if (!pi.hProcess || pi.dwProcessId == 0)
    {
        return false;
    }

    try {
        LaunchedProcessRecord record;
        record.pid = pi.dwProcessId;
        record.path = program.path;
        if (!TryGetProcessCreationTime(pi.hProcess, record.creationTime))
        {
            LogServiceWarningNoThrow(L"Could not read process creation time for a launched process.");
            return false;
        }

        std::lock_guard<std::mutex> lock(gLaunchedProcessesMutex);
        gLaunchedProcesses.push_back(record);
        return true;
    }
    catch (const std::exception& ex) {
        LogServiceExceptionNoThrow(L"Could not record a launched process: ", ex);
    }
    catch (...) {
        LogServiceWarningNoThrow(L"Could not record a launched process due to an unknown exception.");
    }
    return false;
}

static bool StopOnServiceStopEnabledForPath(const AppConfig& config, const std::wstring& path)
{
    for (const ProgramConfig& program : config.programs)
    {
        if (program.stopOnServiceStop &&
            _wcsicmp(NormalizePath(program.path).c_str(), NormalizePath(path).c_str()) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool ConfiguredProgramAlreadyRunningInSession(const ProgramConfig& program, DWORD sessionId)
{
    if (!program.preventDuplicate) {
        return false;
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    std::wstring expectedBaseName = BaseName(program.path);
    bool found = false;

    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, expectedBaseName.c_str()) != 0) {
                continue;
            }

            DWORD processSessionId = 0;
            if (!ProcessIdToSessionId(pe.th32ProcessID, &processSessionId) || processSessionId != sessionId) {
                continue;
            }

            if (IsLocalSystemProcess(pe.th32ProcessID) && ProcessPathEquals(pe.th32ProcessID, program.path)) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return found;
}

static std::wstring QuerySessionString(DWORD sessionId, WTS_INFO_CLASS infoClass)
{
    LPWSTR value = nullptr;
    DWORD bytes = 0;
    std::wstring result;

    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, infoClass, &value, &bytes) && value) {
        result.assign(value);
    }

    if (value) {
        WTSFreeMemory(value);
    }

    return result;
}

static SessionContext GetSessionContext(DWORD sessionId, WTS_CONNECTSTATE_CLASS fallbackState = WTSDisconnected)
{
    SessionContext session;
    session.id = sessionId;
    session.state = fallbackState;

    LPWSTR stateBuffer = nullptr;
    DWORD stateBytes = 0;
    if (WTSQuerySessionInformationW(
            WTS_CURRENT_SERVER_HANDLE,
            sessionId,
            WTSConnectState,
            &stateBuffer,
            &stateBytes) &&
        stateBuffer &&
        stateBytes >= sizeof(WTS_CONNECTSTATE_CLASS)) {
        session.state = *reinterpret_cast<WTS_CONNECTSTATE_CLASS*>(stateBuffer);
    }
    if (stateBuffer) {
        WTSFreeMemory(stateBuffer);
    }

    session.userName = QuerySessionString(sessionId, WTSUserName);
    session.domain = QuerySessionString(sessionId, WTSDomainName);

    if (!session.userName.empty() && !session.domain.empty()) {
        session.account = session.domain + L"\\" + session.userName;
    } else {
        session.account = session.userName;
    }

    return session;
}

static AppConfig LoadConfig()
{
    AppConfig config;
    std::wstring exePath = CurrentExePath();
    if (!ExistingFilePath(exePath))
    {
        LogServiceWarning(L"Refusing to load configuration because the service executable path is invalid: " + exePath);
        return config;
    }

    config.configPath = FindConfigPath();
    if (!FileExists(config.configPath)) {
        LogServiceWarning(L"Configuration file does not exist: " + config.configPath);
        return config;
    }
    if (!ExistingFilePath(config.configPath))
    {
        LogServiceWarning(L"Refusing to load invalid configuration file path: " + config.configPath);
        return config;
    }

    std::vector<aip::IniSectionData> document;
    if (!aip::LoadIniDocument(config.configPath, document))
    {
        LogServiceWarning(
            L"Could not parse configuration file: " +
            config.configPath +
            L" (error " +
            std::to_wstring(GetLastError()) +
            L")");
        return config;
    }

    const std::wstring general = L"General";
    config.desktop = ReadIniString(document, general, L"Desktop", config.desktop);
    config.launchSpacingMs = ReadIniDword(document, general, L"LaunchSpacingMs", config.launchSpacingMs);
    config.maxProgramsPerSession = ReadIniDword(document, general, L"MaxProgramsPerSession", 0);
    config.stopOnServiceStop = ReadIniBool(document, general, L"StopOnServiceStop", true);
    config.startOnServiceStart = ReadIniBool(document, general, L"StartOnServiceStart", true);
    config.startOnConsoleConnect = ReadIniBool(document, general, L"StartOnConsoleConnect", true);
    config.startOnRemoteConnect = ReadIniBool(document, general, L"StartOnRemoteConnect", true);
    config.startOnLogon = ReadIniBool(document, general, L"StartOnLogon", true);
    config.startOnLock = ReadIniBool(document, general, L"StartOnLock", true);
    config.startOnUnlock = ReadIniBool(document, general, L"StartOnUnlock", true);
    config.launchDisconnectedSessions = ReadIniBool(document, general, L"LaunchDisconnectedSessions", true);
    config.includeUsers = SplitList(ReadIniString(document, general, L"IncludeUsers", L""));
    config.excludeUsers = SplitList(ReadIniString(document, general, L"ExcludeUsers", L""));

    for (const aip::IniSectionData& sectionData : document) {
        const std::wstring& section = sectionData.name;
        const std::wstring prefix = L"Program:";
        if (!aip::StartsWithI(section, prefix.c_str())) {
            continue;
        }

        ProgramConfig program;
        program.name = section.substr(prefix.size());
        program.enabled = ReadIniBool(document, section, L"Enabled", true);
        program.path = ReadIniString(document, section, L"Path", L"");
        program.arguments = ReadIniString(document, section, L"Arguments", L"");
        program.commandLine = ReadIniString(document, section, L"CommandLine", L"");
        program.workingDirectory = ReadIniString(document, section, L"WorkingDirectory", DirectoryOf(program.path));
        program.desktop = ReadIniString(document, section, L"Desktop", config.desktop);
        program.includeUsers = SplitList(ReadIniString(document, section, L"IncludeUsers", L""));
        program.excludeUsers = SplitList(ReadIniString(document, section, L"ExcludeUsers", L""));
        program.preventDuplicate = ReadIniBool(document, section, L"PreventDuplicate", true);
        program.stopOnServiceStop = ReadIniBool(document, section, L"StopOnServiceStop", true);
        program.launchSpacingMs = ReadIniDword(document, section, L"LaunchSpacingMs", config.launchSpacingMs);
        program.showWindow = ReadIniInt(document, section, L"ShowWindow", SW_SHOWNOACTIVATE);

        if (!program.name.empty() &&
            !program.path.empty() &&
            ExistingFilePath(program.path) &&
            (program.workingDirectory.empty() || ExistingDirectoryPath(program.workingDirectory))) {
            config.programs.push_back(program);
        } else if (!program.name.empty()) {
            LogServiceWarning(L"Skipped invalid program section: " + section);
        }
    }

    return config;
}

static bool LaunchProgramOnDesktop(const AppConfig& config, const ProgramConfig& program, const SessionContext& session)
{
    if (gStopping.load() ||
        session.id == 0 ||
        session.id == 0xFFFFFFFF ||
        !program.enabled ||
        program.path.empty()) {
        return false;
    }

    if (!UserAllowed(session, config.includeUsers, config.excludeUsers) ||
        !UserAllowed(session, program.includeUsers, program.excludeUsers)) {
        return false;
    }

    static CRITICAL_SECTION launchLock;
    static INIT_ONCE launchLockOnce = INIT_ONCE_STATIC_INIT;
    if (!InitOnceExecuteOnce(
        &launchLockOnce,
        [](PINIT_ONCE, PVOID, PVOID*) -> BOOL {
            InitializeCriticalSection(&launchLock);
            return TRUE;
        },
        nullptr,
        nullptr)) {
        LogServiceWarningNoThrow(L"Could not initialize the process-launch lock.");
        return false;
    }

    aip::CriticalSectionLock lock(launchLock);

    if (gStopping.load()) {
        return false;
    }

    if (ConfiguredProgramAlreadyRunningInSession(program, session.id)) {
        return true;
    }

    EnablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME);
    EnablePrivilege(SE_INCREASE_QUOTA_NAME);
    EnablePrivilege(SE_TCB_NAME);

    HANDLE rawSelfToken = nullptr;
    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY |
                TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
            &rawSelfToken)) {
        return false;
    }
    aip::UniqueKernelHandle selfToken(rawSelfToken);

    HANDLE rawPrimaryToken = nullptr;
    if (!DuplicateTokenEx(
            selfToken.Get(),
            MAXIMUM_ALLOWED,
            nullptr,
            SecurityImpersonation,
            TokenPrimary,
            &rawPrimaryToken)) {
        return false;
    }
    aip::UniqueKernelHandle primaryToken(rawPrimaryToken);

    DWORD tokenSessionId = session.id;
    if (!SetTokenInformation(primaryToken.Get(), TokenSessionId, &tokenSessionId, sizeof(tokenSessionId))) {
        return false;
    }

    struct EnvironmentBlockGuard
    {
        LPVOID value = nullptr;
        ~EnvironmentBlockGuard()
        {
            if (value) {
                DestroyEnvironmentBlock(value);
            }
        }
    } environment;
    if (!CreateEnvironmentBlock(&environment.value, primaryToken.Get(), FALSE)) {
        LogServiceWarning(L"Could not create environment block for session " + std::to_wstring(session.id));
        return false;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    std::wstring desktop = program.desktop.empty() ? config.desktop : program.desktop;
    si.lpDesktop = const_cast<LPWSTR>(desktop.c_str());
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = static_cast<WORD>(program.showWindow);

    PROCESS_INFORMATION pi = {};
    std::wstring commandLine;
    if (!program.commandLine.empty()) {
        commandLine = ExpandTokens(program.commandLine, program, session);
    } else {
        commandLine = Quote(program.path);
        std::wstring args = ExpandTokens(program.arguments, program, session);
        if (!args.empty()) {
            commandLine += L" " + args;
        }
    }

    std::wstring workingDirectory = ExpandTokens(program.workingDirectory, program, session);
    DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP;
    BOOL ok = CreateProcessAsUserW(
        primaryToken.Get(),
        program.path.c_str(),
        commandLine.empty() ? nullptr : &commandLine[0],
        nullptr,
        nullptr,
        FALSE,
        creationFlags,
        environment.value,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &si,
        &pi);

    aip::UniqueKernelHandle process(pi.hProcess);
    aip::UniqueKernelHandle thread(pi.hThread);
    if (ok) {
        RecordLaunchedProcess(program, pi);
    }

    if (ok && program.launchSpacingMs > 0 && !gStopping.load()) {
        if (gStopEvent) {
            DWORD wait = WaitForSingleObject(gStopEvent, program.launchSpacingMs);
            if (wait == WAIT_FAILED) {
                LogServiceWarningNoThrow(L"Waiting between configured program launches failed.");
            }
        } else {
            Sleep(program.launchSpacingMs);
        }
    }

    return ok != FALSE;
}

static void EnsureProgramsForSession(DWORD sessionId, WTS_CONNECTSTATE_CLASS fallbackState = WTSDisconnected)
{
    if (gStopping.load() || sessionId == 0 || sessionId == 0xFFFFFFFF) {
        return;
    }

    AppConfig config = LoadConfig();
    SessionContext session = GetSessionContext(sessionId, fallbackState);

    if (!UserAllowed(session, config.includeUsers, config.excludeUsers)) {
        return;
    }

    DWORD startedOrPresent = 0;
    for (const ProgramConfig& program : config.programs) {
        if (gStopping.load()) {
            break;
        }
        if (config.maxProgramsPerSession > 0 && startedOrPresent >= config.maxProgramsPerSession) {
            break;
        }

        if (LaunchProgramOnDesktop(config, program, session)) {
            ++startedOrPresent;
        }
    }
}

static void StopConfiguredProcesses()
{
    AppConfig config = LoadConfig();
    if (!config.stopOnServiceStop) {
        return;
    }

    std::vector<LaunchedProcessRecord> targets;
    {
        std::lock_guard<std::mutex> lock(gLaunchedProcessesMutex);
        targets = gLaunchedProcesses;
    }

    for (const LaunchedProcessRecord& target : targets) {
        if (!StopOnServiceStopEnabledForPath(config, target.path) ||
            !IsLocalSystemProcess(target.pid) ||
            !ProcessPathEquals(target.pid, target.path)) {
            continue;
        }

        aip::UniqueKernelHandle process(OpenProcess(
            PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
            FALSE,
            target.pid));
        if (!process) {
            continue;
        }

        FILETIME creationTime = {};
        if (!TryGetProcessCreationTime(process.Get(), creationTime) ||
            !FileTimeEquals(creationTime, target.creationTime)) {
            continue;
        }

        if (!TerminateProcess(process.Get(), 0)) {
            LogServiceWarningNoThrow(L"Could not terminate a configured process during service shutdown.");
            continue;
        }
        DWORD wait = WaitForSingleObject(process.Get(), 5000);
        if (wait != WAIT_OBJECT_0) {
            LogServiceWarningNoThrow(
                wait == WAIT_TIMEOUT
                    ? L"Timed out waiting for a configured process to stop."
                    : L"Waiting for a configured process to stop failed.");
        }
    }
}

static bool ShouldLaunchState(const AppConfig& config, WTS_CONNECTSTATE_CLASS state)
{
    switch (state) {
    case WTSActive:
    case WTSConnected:
    case WTSConnectQuery:
    case WTSShadow:
        return true;
    case WTSDisconnected:
        return config.launchDisconnectedSessions;
    default:
        return false;
    }
}

static void ReconcileSessions()
{
    if (gStopping.load()) {
        return;
    }

    AppConfig config = LoadConfig();
    if (!config.startOnServiceStart) {
        return;
    }

    PWTS_SESSION_INFOW sessions = nullptr;
    DWORD count = 0;
    if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count)) {
        for (DWORD i = 0; i < count; ++i) {
            if (ShouldLaunchState(config, sessions[i].State)) {
                EnsureProgramsForSession(sessions[i].SessionId, sessions[i].State);
            }
        }
        WTSFreeMemory(sessions);
    }

    DWORD consoleSession = WTSGetActiveConsoleSessionId();
    if (consoleSession != 0xFFFFFFFF) {
        EnsureProgramsForSession(consoleSession, WTSActive);
    }
}

static void CompleteEnsureWorker()
{
    std::lock_guard<std::mutex> lock(gEnsureWorkersMutex);
    if (gEnsureWorkerCount > 0) {
        --gEnsureWorkerCount;
    }
    if (gEnsureWorkerCount == 0 && gEnsureWorkersDrainedEvent) {
        if (!SetEvent(gEnsureWorkersDrainedEvent)) {
            LogServiceWarningNoThrow(L"Could not signal the worker-drained event.");
        }
    }
}

static DWORD WINAPI EnsureWorker(void* ctx)
{
    DWORD sessionId = *reinterpret_cast<DWORD*>(ctx);
    delete reinterpret_cast<DWORD*>(ctx);
    try {
        if (!gStopping.load()) {
            EnsureProgramsForSession(sessionId);
        }
    }
    catch (const std::exception& ex) {
        LogServiceExceptionNoThrow(L"Session worker failed: ", ex);
    }
    catch (...) {
        LogServiceWarningNoThrow(L"Session worker failed with an unknown exception.");
    }
    CompleteEnsureWorker();
    return 0;
}

static void QueueEnsure(DWORD sessionId)
{
    if (gStopping.load()) {
        return;
    }

    auto* boxedSessionId = new (std::nothrow) DWORD(sessionId);
    if (!boxedSessionId) {
        LogServiceWarningNoThrow(L"Could not allocate a session worker request.");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(gEnsureWorkersMutex);
        if (gStopping.load()) {
            delete boxedSessionId;
            return;
        }
        if (gEnsureWorkerCount == 0 && gEnsureWorkersDrainedEvent) {
            if (!ResetEvent(gEnsureWorkersDrainedEvent)) {
                LogServiceWarningNoThrow(L"Could not reset the worker-drained event.");
                delete boxedSessionId;
                return;
            }
        }
        ++gEnsureWorkerCount;
    }

    if (!QueueUserWorkItem(EnsureWorker, boxedSessionId, WT_EXECUTEDEFAULT)) {
        delete boxedSessionId;
        CompleteEnsureWorker();
    }
}

static void WaitForEnsureWorkersToDrain()
{
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(gEnsureWorkersMutex);
            if (gEnsureWorkerCount == 0) {
                return;
            }
        }

        DWORD wait = WaitForSingleObject(gEnsureWorkersDrainedEvent, 1000);
        if (wait == WAIT_FAILED) {
            LogServiceWarningNoThrow(L"Waiting for session workers failed; polling worker state.");
            Sleep(100);
        }
        else if (wait == WAIT_OBJECT_0) {
            // A stale signal must not bypass the protected worker count.
            Sleep(10);
        }
        SetServiceState(SERVICE_STOP_PENDING, NO_ERROR, 10000);
    }
}

static bool ShouldHandleSessionEvent(const AppConfig& config, DWORD eventType)
{
    switch (eventType) {
    case WTS_CONSOLE_CONNECT:
        return config.startOnConsoleConnect;
    case WTS_REMOTE_CONNECT:
        return config.startOnRemoteConnect;
    case WTS_SESSION_LOGON:
        return config.startOnLogon;
    case WTS_SESSION_LOCK:
        return config.startOnLock;
    case WTS_SESSION_UNLOCK:
        return config.startOnUnlock;
    default:
        return false;
    }
}

static DWORD WINAPI ServiceHandlerEx(DWORD control, DWORD eventType, LPVOID eventData, LPVOID)
{
    try {
        switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            gStopping.store(true);
            SetServiceState(SERVICE_STOP_PENDING, NO_ERROR, 10000);
            if (!SignalStopEvent()) {
                LogServiceWarningNoThrow(L"Could not signal the service stop event.");
            }
            return NO_ERROR;

        case SERVICE_CONTROL_SESSIONCHANGE:
            if (eventData) {
                AppConfig config = LoadConfig();
                if (ShouldHandleSessionEvent(config, eventType)) {
                    auto* notice = reinterpret_cast<WTSSESSION_NOTIFICATION*>(eventData);
                    QueueEnsure(notice->dwSessionId);
                }
            }
            return NO_ERROR;

        default:
            return NO_ERROR;
        }
    }
    catch (const std::exception& ex) {
        LogServiceExceptionNoThrow(L"Service control handler failed: ", ex);
    }
    catch (...) {
        LogServiceWarningNoThrow(L"Service control handler failed with an unknown exception.");
    }
    return ERROR_UNHANDLED_EXCEPTION;
}

static void WINAPI ServiceMain(DWORD, LPWSTR*)
{
    gStatusHandle = RegisterServiceCtrlHandlerExW(kServiceName, ServiceHandlerEx, nullptr);
    if (!gStatusHandle) {
        return;
    }

    gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    SetServiceState(SERVICE_START_PENDING, NO_ERROR, 3000);

    {
        std::lock_guard<std::mutex> lock(gStopEventMutex);
        gStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    if (!gStopEvent) {
        SetServiceState(SERVICE_STOPPED, GetLastError());
        return;
    }
    gEnsureWorkersDrainedEvent = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    if (!gEnsureWorkersDrainedEvent) {
        DWORD error = GetLastError();
        {
            std::lock_guard<std::mutex> lock(gStopEventMutex);
            CloseHandle(gStopEvent);
            gStopEvent = nullptr;
        }
        SetServiceState(SERVICE_STOPPED, error);
        return;
    }

    gStopping.store(false);
    SetServiceState(SERVICE_RUNNING);
    DWORD serviceError = NO_ERROR;
    try {
        ReconcileSessions();

        DWORD stopWait = WaitForSingleObject(gStopEvent, INFINITE);
        if (stopWait != WAIT_OBJECT_0) {
            serviceError = stopWait == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE;
            if (serviceError == ERROR_SUCCESS) {
                serviceError = ERROR_GEN_FAILURE;
            }
            LogServiceWarningNoThrow(L"Waiting for the service stop event failed.");
        }
    }
    catch (const std::exception& ex) {
        serviceError = ERROR_UNHANDLED_EXCEPTION;
        LogServiceExceptionNoThrow(L"Service main loop failed: ", ex);
    }
    catch (...) {
        serviceError = ERROR_UNHANDLED_EXCEPTION;
        LogServiceWarningNoThrow(L"Service main loop failed with an unknown exception.");
    }

    gStopping.store(true);
    SetServiceState(SERVICE_STOP_PENDING, NO_ERROR, 10000);
    try {
        WaitForEnsureWorkersToDrain();
        StopConfiguredProcesses();
    }
    catch (const std::exception& ex) {
        serviceError = ERROR_UNHANDLED_EXCEPTION;
        LogServiceExceptionNoThrow(L"Service shutdown cleanup failed: ", ex);
    }
    catch (...) {
        serviceError = ERROR_UNHANDLED_EXCEPTION;
        LogServiceWarningNoThrow(L"Service shutdown cleanup failed with an unknown exception.");
    }

    CloseHandle(gEnsureWorkersDrainedEvent);
    gEnsureWorkersDrainedEvent = nullptr;
    {
        std::lock_guard<std::mutex> lock(gStopEventMutex);
        CloseHandle(gStopEvent);
        gStopEvent = nullptr;
    }
    SetServiceState(SERVICE_STOPPED, serviceError);
}

static int InstallService()
{
    std::wstring exePath = CurrentExePath();
    if (exePath.empty() || !ExistingFilePath(exePath)) {
        return 1;
    }
    std::wstring serviceBinaryPath = Quote(exePath);

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        return 1;
    }

    SC_HANDLE service = CreateServiceW(
        scm,
        kServiceName,
        kServiceDisplayName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        serviceBinaryPath.c_str(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr);

    if (!service && GetLastError() == ERROR_SERVICE_EXISTS) {
        service = OpenServiceW(scm, kServiceName, SERVICE_CHANGE_CONFIG);
        if (service) {
            ChangeServiceConfigW(
                service,
                SERVICE_WIN32_OWN_PROCESS,
                SERVICE_AUTO_START,
                SERVICE_ERROR_NORMAL,
                serviceBinaryPath.c_str(),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                kServiceDisplayName);
        }
    }

    if (service) {
        SERVICE_DESCRIPTIONW desc = {};
        desc.lpDescription = const_cast<LPWSTR>(
            L"Starts configured programs as LocalSystem on each matching session's secure desktop.");
        ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &desc);
        CloseServiceHandle(service);
    }

    CloseServiceHandle(scm);
    return service ? 0 : 1;
}

static int UninstallService()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        return 1;
    }

    SC_HANDLE service = OpenServiceW(scm, kServiceName, DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!service) {
        CloseServiceHandle(scm);
        return 1;
    }

    SERVICE_STATUS status = {};
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    BOOL ok = DeleteService(service);

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return ok ? 0 : 1;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc > 1) {
        if (_wcsicmp(argv[1], L"install") == 0) {
            return InstallService();
        }
        if (_wcsicmp(argv[1], L"uninstall") == 0) {
            return UninstallService();
        }
        if (_wcsicmp(argv[1], L"test") == 0) {
            ReconcileSessions();
            return 0;
        }
    }

    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(kServiceName), ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(table)) {
        return GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT ? 0 : 1;
    }

    return 0;
}

