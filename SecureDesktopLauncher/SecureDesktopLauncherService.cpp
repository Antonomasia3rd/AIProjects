#define UNICODE
#define _UNICODE

#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <tlhelp32.h>
#include <aclapi.h>
#include <sddl.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")

static const wchar_t* kServiceName = L"SecureDesktopLauncher";
static const wchar_t* kServiceDisplayName = L"Secure Desktop Launcher";
static const wchar_t* kConfigFileName = L"SecureDesktopLauncher.ini";

static SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
static SERVICE_STATUS gStatus = {};
static HANDLE gStopEvent = nullptr;

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
    gStatus.dwCurrentState = state;
    gStatus.dwWin32ExitCode = win32ExitCode;
    gStatus.dwWaitHint = waitHint;
    gStatus.dwControlsAccepted = 0;

    if (state == SERVICE_RUNNING) {
        gStatus.dwControlsAccepted =
            SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE;
    }

    if (gStatusHandle) {
        SetServiceStatus(gStatusHandle, &gStatus);
    }
}

static std::wstring ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
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

static std::wstring ParentDirectory(std::wstring path)
{
    while (!path.empty() && (path[path.size() - 1] == L'\\' || path[path.size() - 1] == L'/')) {
        path.resize(path.size() - 1);
    }

    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"" : path.substr(0, pos);
}

static void LogServiceWarning(const std::wstring& message)
{
    std::wstring debugLine = std::wstring(kServiceName) + L": " + message + L"\n";
    OutputDebugStringW(debugLine.c_str());

    HANDLE eventSource = RegisterEventSourceW(nullptr, kServiceName);
    if (eventSource)
    {
        LPCWSTR strings[] = { message.c_str() };
        ReportEventW(
            eventSource,
            EVENTLOG_WARNING_TYPE,
            0,
            1,
            nullptr,
            1,
            0,
            strings,
            nullptr);
        DeregisterEventSource(eventSource);
    }
}

static bool EqualKnownSid(PSID sid, WELL_KNOWN_SID_TYPE type)
{
    BYTE buffer[SECURITY_MAX_SID_SIZE] = {};
    DWORD size = sizeof(buffer);
    return CreateWellKnownSid(type, nullptr, buffer, &size) && EqualSid(sid, buffer);
}

static bool EqualSidString(PSID sid, const wchar_t* sidString)
{
    PSID parsedSid = nullptr;
    if (!ConvertStringSidToSidW(sidString, &parsedSid))
    {
        return false;
    }

    bool equal = EqualSid(sid, parsedSid) != FALSE;
    LocalFree(parsedSid);
    return equal;
}

static bool IsTrustedSecurityPrincipal(PSID sid)
{
    return EqualKnownSid(sid, WinLocalSystemSid) ||
        EqualKnownSid(sid, WinBuiltinAdministratorsSid) ||
        EqualSidString(sid, L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464");
}

static bool IsTrustedWritePrincipal(PSID sid)
{
    return IsTrustedSecurityPrincipal(sid);
}

static bool IsWriteLikeAccess(DWORD mask)
{
    const DWORD writeMask =
        FILE_WRITE_DATA |
        FILE_APPEND_DATA |
        FILE_WRITE_EA |
        FILE_WRITE_ATTRIBUTES |
        DELETE |
        FILE_DELETE_CHILD |
        WRITE_DAC |
        WRITE_OWNER |
        GENERIC_WRITE |
        GENERIC_ALL;
    return (mask & writeMask) != 0;
}

static bool IsReplaceLikeAccess(DWORD mask)
{
    const DWORD replaceMask =
        DELETE |
        FILE_DELETE_CHILD |
        WRITE_DAC |
        WRITE_OWNER |
        GENERIC_ALL;
    return (mask & replaceMask) != 0;
}

static bool PathHasUntrustedAccessAce(const std::wstring& path, bool replaceOnly)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    PACL dacl = nullptr;
    DWORD error = GetNamedSecurityInfoW(
        const_cast<LPWSTR>(path.c_str()),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        &dacl,
        nullptr,
        &sd);
    if (error != ERROR_SUCCESS)
    {
        LogServiceWarning(L"Could not read ACL for " + path + L"; refusing to trust it.");
        return true;
    }

    if (!dacl)
    {
        if (sd) LocalFree(sd);
        LogServiceWarning(L"Null DACL grants broad write access for " + path + L"; refusing to trust it.");
        return true;
    }

    bool unsafe = false;
    for (DWORD i = 0; i < dacl->AceCount && !unsafe; ++i)
    {
        void* aceData = nullptr;
        if (!GetAce(dacl, i, &aceData) || !aceData)
        {
            unsafe = true;
            break;
        }

        ACE_HEADER* header = reinterpret_cast<ACE_HEADER*>(aceData);
        if ((header->AceFlags & INHERIT_ONLY_ACE) != 0 ||
            (header->AceType != ACCESS_ALLOWED_ACE_TYPE &&
             header->AceType != ACCESS_ALLOWED_CALLBACK_ACE_TYPE))
        {
            continue;
        }

        auto* ace = reinterpret_cast<ACCESS_ALLOWED_ACE*>(aceData);
        PSID sid = reinterpret_cast<PSID>(&ace->SidStart);
        bool unsafeAccess = replaceOnly ? IsReplaceLikeAccess(ace->Mask) : IsWriteLikeAccess(ace->Mask);
        if (unsafeAccess && !IsTrustedWritePrincipal(sid))
        {
            unsafe = true;
        }
    }

    if (sd) LocalFree(sd);
    if (unsafe)
    {
        LogServiceWarning(L"Refusing to trust user-writable path: " + path);
    }
    return unsafe;
}

static bool PathHasUntrustedWriteAce(const std::wstring& path)
{
    return PathHasUntrustedAccessAce(path, false);
}

static bool PathHasUntrustedReplaceAce(const std::wstring& path)
{
    return PathHasUntrustedAccessAce(path, true);
}

static bool PathOwnerIsTrusted(const std::wstring& path)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    PSID owner = nullptr;
    DWORD error = GetNamedSecurityInfoW(
        const_cast<LPWSTR>(path.c_str()),
        SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION,
        &owner,
        nullptr,
        nullptr,
        nullptr,
        &sd);
    if (error != ERROR_SUCCESS || !owner)
    {
        if (sd) LocalFree(sd);
        LogServiceWarning(L"Could not read owner for " + path + L"; refusing to trust it.");
        return false;
    }

    bool trusted = IsTrustedSecurityPrincipal(owner);
    if (sd) LocalFree(sd);
    if (!trusted)
    {
        LogServiceWarning(L"Refusing to trust path with untrusted owner: " + path);
    }
    return trusted;
}

static bool PathSecurityIsTrusted(const std::wstring& path, bool replaceOnly = false)
{
    return PathOwnerIsTrusted(path) &&
        !(replaceOnly ? PathHasUntrustedReplaceAce(path) : PathHasUntrustedWriteAce(path));
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

static bool IsDriveRoot(const std::wstring& path)
{
    return path.size() == 3 &&
        iswalpha(path[0]) &&
        path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/');
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

static bool IsUncShareRoot(const std::wstring& path)
{
    if (path.size() < 5 || path[0] != L'\\' || path[1] != L'\\')
    {
        return false;
    }

    size_t serverEnd = path.find_first_of(L"\\/", 2);
    if (serverEnd == std::wstring::npos || serverEnd + 1 >= path.size())
    {
        return false;
    }

    size_t shareEnd = path.find_first_of(L"\\/", serverEnd + 1);
    return shareEnd == std::wstring::npos;
}

static std::wstring ParentDirectoryForTrust(std::wstring path)
{
    path = TrimTrailingDirectorySeparators(path);
    if (path.empty() || IsDriveRoot(path) || IsUncShareRoot(path))
    {
        return L"";
    }

    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
    {
        return L"";
    }
    if (pos == 2 && path[1] == L':')
    {
        return path.substr(0, 3);
    }
    return path.substr(0, pos);
}

static bool TrustedExistingAncestorDirectories(std::wstring path)
{
    path = TrimTrailingDirectorySeparators(path);
    bool strictCurrentDirectory = true;
    while (!path.empty())
    {
        if (!DirectoryExists(path) || !PathSecurityIsTrusted(path, !strictCurrentDirectory))
        {
            return false;
        }

        std::wstring parent = ParentDirectoryForTrust(path);
        if (parent.empty() || parent == path)
        {
            break;
        }
        path = parent;
        strictCurrentDirectory = false;
    }

    return true;
}

static bool TrustedExistingFilePath(const std::wstring& path)
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
    return PathSecurityIsTrusted(fullPath) &&
        !dir.empty() &&
        TrustedExistingAncestorDirectories(dir);
}

static bool TrustedExistingDirectoryPath(const std::wstring& path)
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

    return TrustedExistingAncestorDirectories(fullPath);
}

static std::wstring FindConfigPath()
{
    std::wstring exeDir = DirectoryOf(CurrentExePath());
    std::wstring parent = ParentDirectory(exeDir);
    std::vector<std::wstring> candidates;

    candidates.push_back(CombinePath(exeDir, kConfigFileName));
    if (!parent.empty()) {
        candidates.push_back(CombinePath(parent, kConfigFileName));
    }

    for (const std::wstring& candidate : candidates) {
        if (FileExists(candidate)) {
            return candidate;
        }
    }

    return CombinePath(exeDir, kConfigFileName);
}

static std::wstring ReadIniString(
    const std::wstring& path,
    const std::wstring& section,
    const std::wstring& key,
    const std::wstring& defaultValue)
{
    DWORD size = 512;
    for (;;) {
        std::vector<wchar_t> buffer(size);
        DWORD read = GetPrivateProfileStringW(
            section.c_str(),
            key.c_str(),
            defaultValue.c_str(),
            buffer.data(),
            size,
            path.c_str());

        if (read < size - 2 || size >= 32768) {
            return Trim(std::wstring(buffer.data(), read));
        }
        size *= 2;
    }
}

static bool ReadIniBool(
    const std::wstring& path,
    const std::wstring& section,
    const std::wstring& key,
    bool defaultValue)
{
    std::wstring raw = ToLower(ReadIniString(path, section, key, defaultValue ? L"1" : L"0"));
    return raw == L"1" || raw == L"true" || raw == L"yes" || raw == L"on";
}

static DWORD ReadIniDword(
    const std::wstring& path,
    const std::wstring& section,
    const std::wstring& key,
    DWORD defaultValue)
{
    std::wstring raw = ReadIniString(path, section, key, L"");
    if (raw.empty()) {
        return defaultValue;
    }

    wchar_t* end = nullptr;
    unsigned long value = wcstoul(raw.c_str(), &end, 10);
    return end && *end == L'\0' ? static_cast<DWORD>(value) : defaultValue;
}

static int ReadIniInt(
    const std::wstring& path,
    const std::wstring& section,
    const std::wstring& key,
    int defaultValue)
{
    std::wstring raw = ReadIniString(path, section, key, L"");
    if (raw.empty()) {
        return defaultValue;
    }

    wchar_t* end = nullptr;
    long value = wcstol(raw.c_str(), &end, 10);
    return end && *end == L'\0' ? static_cast<int>(value) : defaultValue;
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

static void RecordLaunchedProcess(const ProgramConfig& program, const PROCESS_INFORMATION& pi)
{
    if (!pi.hProcess || pi.dwProcessId == 0)
    {
        return;
    }

    LaunchedProcessRecord record;
    record.pid = pi.dwProcessId;
    record.path = program.path;
    if (!TryGetProcessCreationTime(pi.hProcess, record.creationTime))
    {
        LogServiceWarning(L"Could not read process creation time for launched process: " + program.name);
        return;
    }

    std::lock_guard<std::mutex> lock(gLaunchedProcessesMutex);
    gLaunchedProcesses.push_back(record);
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

static std::vector<std::wstring> ReadIniSectionNames(const std::wstring& path)
{
    DWORD size = 4096;
    for (;;) {
        std::vector<wchar_t> buffer(size);
        DWORD read = GetPrivateProfileSectionNamesW(buffer.data(), size, path.c_str());
        if (read < size - 2) {
            std::vector<std::wstring> sections;
            wchar_t* cursor = buffer.data();
            while (*cursor) {
                sections.push_back(cursor);
                cursor += wcslen(cursor) + 1;
            }
            return sections;
        }
        size *= 2;
        if (size > 65536) {
            return {};
        }
    }
}

static AppConfig LoadConfig()
{
    AppConfig config;
    std::wstring exePath = CurrentExePath();
    if (!TrustedExistingFilePath(exePath))
    {
        LogServiceWarning(L"Refusing to load configuration because the service executable is not trusted: " + exePath);
        return config;
    }

    config.configPath = FindConfigPath();
    if (!FileExists(config.configPath)) {
        LogServiceWarning(L"Configuration file does not exist: " + config.configPath);
        return config;
    }
    if (!TrustedExistingFilePath(config.configPath))
    {
        LogServiceWarning(L"Refusing to load untrusted configuration file: " + config.configPath);
        return config;
    }

    const std::wstring general = L"General";
    config.desktop = ReadIniString(config.configPath, general, L"Desktop", config.desktop);
    config.launchSpacingMs = ReadIniDword(config.configPath, general, L"LaunchSpacingMs", config.launchSpacingMs);
    config.maxProgramsPerSession = ReadIniDword(config.configPath, general, L"MaxProgramsPerSession", 0);
    config.stopOnServiceStop = ReadIniBool(config.configPath, general, L"StopOnServiceStop", true);
    config.startOnServiceStart = ReadIniBool(config.configPath, general, L"StartOnServiceStart", true);
    config.startOnConsoleConnect = ReadIniBool(config.configPath, general, L"StartOnConsoleConnect", true);
    config.startOnRemoteConnect = ReadIniBool(config.configPath, general, L"StartOnRemoteConnect", true);
    config.startOnLogon = ReadIniBool(config.configPath, general, L"StartOnLogon", true);
    config.startOnLock = ReadIniBool(config.configPath, general, L"StartOnLock", true);
    config.startOnUnlock = ReadIniBool(config.configPath, general, L"StartOnUnlock", true);
    config.launchDisconnectedSessions = ReadIniBool(config.configPath, general, L"LaunchDisconnectedSessions", true);
    config.includeUsers = SplitList(ReadIniString(config.configPath, general, L"IncludeUsers", L""));
    config.excludeUsers = SplitList(ReadIniString(config.configPath, general, L"ExcludeUsers", L""));

    std::vector<std::wstring> sections = ReadIniSectionNames(config.configPath);
    for (const std::wstring& section : sections) {
        const std::wstring prefix = L"Program:";
        if (section.rfind(prefix, 0) != 0) {
            continue;
        }

        ProgramConfig program;
        program.name = section.substr(prefix.size());
        program.enabled = ReadIniBool(config.configPath, section, L"Enabled", true);
        program.path = ReadIniString(config.configPath, section, L"Path", L"");
        program.arguments = ReadIniString(config.configPath, section, L"Arguments", L"");
        program.commandLine = ReadIniString(config.configPath, section, L"CommandLine", L"");
        program.workingDirectory = ReadIniString(config.configPath, section, L"WorkingDirectory", DirectoryOf(program.path));
        program.desktop = ReadIniString(config.configPath, section, L"Desktop", config.desktop);
        program.includeUsers = SplitList(ReadIniString(config.configPath, section, L"IncludeUsers", L""));
        program.excludeUsers = SplitList(ReadIniString(config.configPath, section, L"ExcludeUsers", L""));
        program.preventDuplicate = ReadIniBool(config.configPath, section, L"PreventDuplicate", true);
        program.stopOnServiceStop = ReadIniBool(config.configPath, section, L"StopOnServiceStop", true);
        program.launchSpacingMs = ReadIniDword(config.configPath, section, L"LaunchSpacingMs", config.launchSpacingMs);
        program.showWindow = ReadIniInt(config.configPath, section, L"ShowWindow", SW_SHOWNOACTIVATE);

        if (!program.name.empty() &&
            !program.path.empty() &&
            TrustedExistingFilePath(program.path) &&
            (program.workingDirectory.empty() || TrustedExistingDirectoryPath(program.workingDirectory))) {
            config.programs.push_back(program);
        } else if (!program.name.empty()) {
            LogServiceWarning(L"Skipped untrusted or invalid program section: " + section);
        }
    }

    return config;
}

static bool LaunchProgramOnDesktop(const AppConfig& config, const ProgramConfig& program, const SessionContext& session)
{
    if (session.id == 0 || session.id == 0xFFFFFFFF || !program.enabled || program.path.empty()) {
        return false;
    }

    if (!UserAllowed(session, config.includeUsers, config.excludeUsers) ||
        !UserAllowed(session, program.includeUsers, program.excludeUsers)) {
        return false;
    }

    static CRITICAL_SECTION launchLock;
    static INIT_ONCE launchLockOnce = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(
        &launchLockOnce,
        [](PINIT_ONCE, PVOID, PVOID*) -> BOOL {
            InitializeCriticalSection(&launchLock);
            return TRUE;
        },
        nullptr,
        nullptr);

    EnterCriticalSection(&launchLock);

    if (ConfiguredProgramAlreadyRunningInSession(program, session.id)) {
        LeaveCriticalSection(&launchLock);
        return true;
    }

    EnablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME);
    EnablePrivilege(SE_INCREASE_QUOTA_NAME);
    EnablePrivilege(SE_TCB_NAME);

    HANDLE selfToken = nullptr;
    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY |
                TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
            &selfToken)) {
        LeaveCriticalSection(&launchLock);
        return false;
    }

    HANDLE primaryToken = nullptr;
    if (!DuplicateTokenEx(
            selfToken,
            MAXIMUM_ALLOWED,
            nullptr,
            SecurityImpersonation,
            TokenPrimary,
            &primaryToken)) {
        CloseHandle(selfToken);
        LeaveCriticalSection(&launchLock);
        return false;
    }

    CloseHandle(selfToken);

    DWORD tokenSessionId = session.id;
    if (!SetTokenInformation(primaryToken, TokenSessionId, &tokenSessionId, sizeof(tokenSessionId))) {
        CloseHandle(primaryToken);
        LeaveCriticalSection(&launchLock);
        return false;
    }

    LPVOID environment = nullptr;
    CreateEnvironmentBlock(&environment, primaryToken, FALSE);

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
        primaryToken,
        program.path.c_str(),
        commandLine.empty() ? nullptr : &commandLine[0],
        nullptr,
        nullptr,
        FALSE,
        creationFlags,
        environment,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &si,
        &pi);

    if (ok)
    {
        RecordLaunchedProcess(program, pi);
    }

    if (pi.hThread) {
        CloseHandle(pi.hThread);
    }
    if (pi.hProcess) {
        CloseHandle(pi.hProcess);
    }
    if (environment) {
        DestroyEnvironmentBlock(environment);
    }
    CloseHandle(primaryToken);

    if (ok && program.launchSpacingMs > 0) {
        Sleep(program.launchSpacingMs);
    }

    LeaveCriticalSection(&launchLock);
    return ok != FALSE;
}

static void EnsureProgramsForSession(DWORD sessionId, WTS_CONNECTSTATE_CLASS fallbackState = WTSDisconnected)
{
    if (sessionId == 0 || sessionId == 0xFFFFFFFF) {
        return;
    }

    AppConfig config = LoadConfig();
    SessionContext session = GetSessionContext(sessionId, fallbackState);

    if (!UserAllowed(session, config.includeUsers, config.excludeUsers)) {
        return;
    }

    DWORD startedOrPresent = 0;
    for (const ProgramConfig& program : config.programs) {
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

        HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, target.pid);
        if (!process) {
            continue;
        }

        FILETIME creationTime = {};
        if (!TryGetProcessCreationTime(process, creationTime) ||
            !FileTimeEquals(creationTime, target.creationTime)) {
            CloseHandle(process);
            continue;
        }

        TerminateProcess(process, 0);
        WaitForSingleObject(process, 5000);
        CloseHandle(process);
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

static DWORD WINAPI EnsureWorker(void* ctx)
{
    DWORD sessionId = *reinterpret_cast<DWORD*>(ctx);
    delete reinterpret_cast<DWORD*>(ctx);
    EnsureProgramsForSession(sessionId);
    return 0;
}

static void QueueEnsure(DWORD sessionId)
{
    auto* boxedSessionId = new DWORD(sessionId);
    if (!QueueUserWorkItem(EnsureWorker, boxedSessionId, WT_EXECUTEDEFAULT)) {
        delete boxedSessionId;
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
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        SetServiceState(SERVICE_STOP_PENDING, NO_ERROR, 10000);
        if (gStopEvent) {
            SetEvent(gStopEvent);
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

static void WINAPI ServiceMain(DWORD, LPWSTR*)
{
    gStatusHandle = RegisterServiceCtrlHandlerExW(kServiceName, ServiceHandlerEx, nullptr);
    if (!gStatusHandle) {
        return;
    }

    gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    SetServiceState(SERVICE_START_PENDING, NO_ERROR, 3000);

    gStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!gStopEvent) {
        SetServiceState(SERVICE_STOPPED, GetLastError());
        return;
    }

    SetServiceState(SERVICE_RUNNING);
    ReconcileSessions();

    WaitForSingleObject(gStopEvent, INFINITE);

    SetServiceState(SERVICE_STOP_PENDING, NO_ERROR, 10000);
    StopConfiguredProcesses();

    CloseHandle(gStopEvent);
    gStopEvent = nullptr;
    SetServiceState(SERVICE_STOPPED);
}

static int InstallService()
{
    std::wstring exePath = CurrentExePath();
    if (exePath.empty() || !TrustedExistingFilePath(exePath)) {
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

