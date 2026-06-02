// ==WindhawkMod==
// @id              always-uiaccess
// @name            Always UIAccess
// @description     Creates or patches allowlisted child processes as UIAccess from hook-host processes selected by Windhawk's own inclusion list.
// @version         2.4.1
// @author          local
// @include         windhawk.exe
// @compilerOptions -ladvapi32 -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
Simplified model:

1. Windhawk's built-in process inclusion list selects hook hosts.
   Add processes that should have a CreateProcessW hook installed, for example:

     explorer.exe
     CharmBar.exe

   Keep the built-in windhawk.exe include. The service copy is still needed so
   the mod can set TokenUIAccess with SeTcbPrivilege.

2. This mod's own "UIAccess target include list" selects programs that should
   run as UIAccess.

   The list supports Windhawk-style executable name/path patterns:

     D:\Program Files\Charms Bar\CharmBar.exe
     C:\Windows\System32\winver.exe
     YouTube Music.exe
     C:\Tools\*.exe

   Use semicolons if the settings textbox does not allow line breaks.

Runtime flow:

- If a hook host creates an allowlisted non-debug child, the CreateProcessW hook
  asks the Windhawk service to create the child directly as UIAccess. There is no
  normal child PID that gets killed/relaunched.

- If a hook host creates an allowlisted debug child, the hook lets the original
  CreateProcessW call succeed, then patches the new child token immediately
  before returning to the parent. This preserves debugger/debuggee relationships
  such as CharmBar.exe's parent/child design.

- If an allowlisted target itself is injected by Windhawk and starts without
  UIAccess, the legacy self-relaunch path is still available as a fallback.

No osk.exe token source is used.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- targetPathAllowlist: |
  $name: UIAccess target include list
  $description: Executable names or full paths to run as UIAccess. Supports Windhawk-style * and ? wildcards, full paths, base names, environment variables such as %ProgramFiles%, comments starting with #, and semicolon-separated entries.
- patchDebugChildrenEarly: true
  $name: Patch debug child processes early
  $description: When a hook host creates an allowlisted child with DEBUG_PROCESS or DEBUG_ONLY_THIS_PROCESS, patch the child token immediately after CreateProcessW returns. Recommended for CharmBar.exe.
- skipChromiumElectronSubprocessTargets: true
  $name: Skip Chromium/Electron subprocess targets
  $description: Do not intercept allowlisted child launches whose command line contains Chromium/Electron helper switches such as --type= or --mojo-platform-channel-handle=. Recommended for Electron apps such as YouTube Music; only the main/browser process is made UIAccess.
- autoAlwaysOnTop: false
  $name: Automatically always on top
  $description: Calls SetWindowPos(HWND_TOPMOST) on top-level windows owned by a UIAccess process. Usually unnecessary when windows naturally land in ZBID_UIACCESS.
- autoAlwaysOnTopVisibleOnly: true
  $name: Only visible top-level windows
  $description: Avoids forcing helper/IME/hidden message windows to topmost.
- autoAlwaysOnTopDurationMs: 15000
  $name: Auto-topmost scan duration, ms
  $description: How long after process start to scan for newly-created top-level windows. Use 0 to keep scanning until the process exits.
- autoAlwaysOnTopIntervalMs: 250
  $name: Auto-topmost scan interval, ms
  $description: How often to scan for windows while the auto-topmost worker is active.
*/
// ==/WindhawkModSettings==

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>
#include <cwctype>

#define TOKEN_UI_ACCESS_CLASS ((TOKEN_INFORMATION_CLASS)26)

static constexpr const wchar_t* kPipeName =
    L"\\\\.\\pipe\\WindhawkAlwaysUIAccess";
static constexpr const wchar_t* kRelaunchMarkerName =
    L"WH_ALWAYS_UIACCESS_RELAUNCHED";
static constexpr const wchar_t* kRelaunchMarkerEntry =
    L"WH_ALWAYS_UIACCESS_RELAUNCHED=1";
static constexpr DWORD kPipeTimeoutMs = 5000;
static constexpr size_t kCommandLineChars = 32768;
static constexpr DWORD kRequestFlagPatchCurrentProcessToken = 0x00000001;
static constexpr DWORD kRequestFlagPatchTargetProcessToken = 0x00000002;
static constexpr DWORD kRequestFlagCreateTargetProcess = 0x00000004;

using NtSetInformationToken_t = NTSTATUS(NTAPI*)(
    HANDLE TokenHandle,
    TOKEN_INFORMATION_CLASS TokenInformationClass,
    PVOID TokenInformation,
    ULONG TokenInformationLength);

using CreateEnvironmentBlock_t = BOOL(WINAPI*)(LPVOID*, HANDLE, BOOL);
using DestroyEnvironmentBlock_t = BOOL(WINAPI*)(LPVOID);
using GetNamedPipeClientProcessId_t = BOOL(WINAPI*)(HANDLE, PULONG);
using Wow64DisableWow64FsRedirection_t = BOOL(WINAPI*)(PVOID*);
using Wow64RevertWow64FsRedirection_t = BOOL(WINAPI*)(PVOID);
using CreateProcessW_t = BOOL(WINAPI*)(
    LPCWSTR lpApplicationName,
    LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation);

static NtSetInformationToken_t g_NtSetInformationToken = nullptr;
static CreateEnvironmentBlock_t g_CreateEnvironmentBlock = nullptr;
static DestroyEnvironmentBlock_t g_DestroyEnvironmentBlock = nullptr;
static GetNamedPipeClientProcessId_t g_GetNamedPipeClientProcessId = nullptr;
static Wow64DisableWow64FsRedirection_t g_Wow64DisableWow64FsRedirection = nullptr;
static Wow64RevertWow64FsRedirection_t g_Wow64RevertWow64FsRedirection = nullptr;
static CreateProcessW_t g_CreateProcessW_Original = nullptr;
static HANDLE g_stopEvent = nullptr;
static HANDLE g_pipeThread = nullptr;
static HANDLE g_autoTopmostStopEvent = nullptr;
static HANDLE g_autoTopmostThread = nullptr;
static bool g_patchDebugChildrenEarly = true;
static bool g_createProcessHookInstalled = false;
static bool g_autoAlwaysOnTop = false;
static bool g_autoAlwaysOnTopVisibleOnly = true;
static bool g_skipChromiumElectronSubprocessTargets = true;
static std::vector<std::wstring> g_uiAccessTargetPatterns;
static DWORD g_autoAlwaysOnTopDurationMs = 15000;
static DWORD g_autoAlwaysOnTopIntervalMs = 250;

#pragma pack(push, 1)
struct UIAccessRequest {
    DWORD size;
    DWORD flags;
    DWORD targetPid;
    WCHAR exe[MAX_PATH];
    WCHAR currentDirectory[MAX_PATH];
    DWORD creationFlags;
    DWORD startupFlags;
    WORD startupShowWindow;
    WCHAR commandLine[kCommandLineChars];
};

// Keep this structure pointer-size independent. The Windhawk service is often
// 64-bit while a target such as CharmBar.exe can be 32-bit. Using ULONG_PTR
// here changes sizeof(UIAccessResponse) across bitness and breaks the legacy
// client ReadFile(path) with message pipes.
struct UIAccessResponse {
    DWORD size;
    BOOL success;
    DWORD pid;
    DWORD tid;
    DWORD win32Error;
    NTSTATUS ntStatus;
    ULONGLONG processHandle;
    ULONGLONG threadHandle;
    WCHAR message[512];
};
#pragma pack(pop)

static_assert(sizeof(UIAccessResponse) == 1064,
              "UIAccessResponse must remain identical between 32-bit and 64-bit builds.");

static bool CurrentProcessHasUIAccess();


static bool IsSlash(WCHAR ch) {
    return ch == L'\\' || ch == L'/';
}

static bool IsAbsolutePath(PCWSTR path) {
    if (!path || !*path) {
        return false;
    }

    if (IsSlash(path[0]) && IsSlash(path[1])) {
        return true;
    }

    return ((path[0] >= L'A' && path[0] <= L'Z') ||
            (path[0] >= L'a' && path[0] <= L'z')) &&
           path[1] == L':' && IsSlash(path[2]);
}

static void TrimStringInPlace(std::wstring* value) {
    size_t first = 0;
    while (first < value->size()) {
        WCHAR ch = (*value)[first];
        if (ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n') {
            break;
        }
        ++first;
    }

    size_t last = value->size();
    while (last > first) {
        WCHAR ch = (*value)[last - 1];
        if (ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n') {
            break;
        }
        --last;
    }

    *value = value->substr(first, last - first);

    if (value->size() >= 2 && value->front() == L'"' && value->back() == L'"') {
        *value = value->substr(1, value->size() - 2);
        TrimStringInPlace(value);
    }
}

static std::wstring ExpandEnvironmentStringsToString(PCWSTR value) {
    if (!value) {
        return std::wstring();
    }

    DWORD needed = ExpandEnvironmentStringsW(value, nullptr, 0);
    if (!needed) {
        return value;
    }

    std::wstring expanded(needed, L'\0');
    DWORD copied = ExpandEnvironmentStringsW(value, expanded.data(), needed);
    if (!copied || copied > needed) {
        return value;
    }

    if (!expanded.empty() && expanded.back() == L'\0') {
        expanded.pop_back();
    }
    return expanded;
}

static std::wstring FullPathToString(PCWSTR value) {
    if (!value || !*value) {
        return std::wstring();
    }

    WCHAR fullPath[MAX_PATH]{};
    DWORD copied = GetFullPathNameW(value, ARRAYSIZE(fullPath), fullPath, nullptr);
    if (copied && copied < ARRAYSIZE(fullPath)) {
        return fullPath;
    }

    return value;
}

static std::wstring NormalizeTargetPathForMatch(std::wstring path) {
    TrimStringInPlace(&path);
    if (path.empty()) {
        return std::wstring();
    }

    path = ExpandEnvironmentStringsToString(path.c_str());
    TrimStringInPlace(&path);

    if (IsAbsolutePath(path.c_str()) || wcschr(path.c_str(), L'\\') ||
        wcschr(path.c_str(), L'/')) {
        return FullPathToString(path.c_str());
    }

    return path;
}

static bool PatternHasPathPart(PCWSTR pattern) {
    if (!pattern) {
        return false;
    }

    return IsAbsolutePath(pattern) || wcschr(pattern, L'\\') ||
           wcschr(pattern, L'/') || (pattern[0] && pattern[1] == L':');
}

static bool WildcardMatchInsensitive(PCWSTR pattern, PCWSTR text) {
    if (!pattern || !text) {
        return false;
    }

    PCWSTR star = nullptr;
    PCWSTR retryText = nullptr;

    while (*text) {
        if (*pattern == L'*') {
            star = pattern++;
            retryText = text;
            continue;
        }

        if (*pattern == L'?' ||
            towlower(static_cast<wint_t>(*pattern)) ==
                towlower(static_cast<wint_t>(*text))) {
            ++pattern;
            ++text;
            continue;
        }

        if (star) {
            pattern = star + 1;
            text = ++retryText;
            continue;
        }

        return false;
    }

    while (*pattern == L'*') {
        ++pattern;
    }

    return *pattern == L'\0';
}

static PCWSTR PathBaseName(PCWSTR path) {
    if (!path) {
        return L"";
    }

    PCWSTR base = wcsrchr(path, L'\\');
    PCWSTR baseForwardSlash = wcsrchr(path, L'/');
    if (baseForwardSlash && (!base || baseForwardSlash > base)) {
        base = baseForwardSlash;
    }

    return base ? base + 1 : path;
}

static void LoadTargetPathAllowlistSetting() {
    g_uiAccessTargetPatterns.clear();

    PCWSTR rawAllowlist = Wh_GetStringSetting(L"targetPathAllowlist");
    if (!rawAllowlist) {
        return;
    }

    std::wstring text = rawAllowlist;
    Wh_FreeStringSetting(rawAllowlist);

    for (WCHAR& ch : text) {
        if (ch == L';') {
            ch = L'\n';
        }
    }

    size_t position = 0;
    while (position <= text.size()) {
        size_t next = text.find(L'\n', position);
        std::wstring line = text.substr(position,
                                        next == std::wstring::npos
                                            ? std::wstring::npos
                                            : next - position);
        position = next == std::wstring::npos ? text.size() + 1 : next + 1;

        TrimStringInPlace(&line);
        if (line.empty() || line[0] == L'#') {
            continue;
        }

        std::wstring normalized = NormalizeTargetPathForMatch(line);
        if (!normalized.empty()) {
            g_uiAccessTargetPatterns.push_back(normalized);
        }
    }
}

static bool IsPathInTargetAllowlist(PCWSTR path) {
    if (g_uiAccessTargetPatterns.empty()) {
        return false;
    }

    std::wstring normalizedPath = NormalizeTargetPathForMatch(path ? path : L"");
    if (normalizedPath.empty()) {
        return false;
    }

    PCWSTR baseName = PathBaseName(normalizedPath.c_str());

    for (const std::wstring& pattern : g_uiAccessTargetPatterns) {
        if (PatternHasPathPart(pattern.c_str())) {
            if (WildcardMatchInsensitive(pattern.c_str(), normalizedPath.c_str())) {
                return true;
            }
        } else {
            if (WildcardMatchInsensitive(pattern.c_str(), baseName)) {
                return true;
            }
        }
    }

    return false;
}


static bool IsNativeSystem32Path(PCWSTR path) {
    if (!path || !*path) {
        return false;
    }

    WCHAR windowsDirectory[MAX_PATH]{};
    DWORD windowsDirectoryLength = GetWindowsDirectoryW(
        windowsDirectory,
        ARRAYSIZE(windowsDirectory));
    if (!windowsDirectoryLength ||
        windowsDirectoryLength >= ARRAYSIZE(windowsDirectory)) {
        return false;
    }

    std::wstring normalizedPath = FullPathToString(path);
    std::wstring system32Prefix = FullPathToString(windowsDirectory);
    TrimStringInPlace(&normalizedPath);
    TrimStringInPlace(&system32Prefix);

    if (normalizedPath.empty() || system32Prefix.empty()) {
        return false;
    }

    while (!system32Prefix.empty() && IsSlash(system32Prefix.back())) {
        system32Prefix.pop_back();
    }

    system32Prefix += L"\\System32\\";

    return normalizedPath.size() > system32Prefix.size() &&
           _wcsnicmp(normalizedPath.c_str(),
                     system32Prefix.c_str(),
                     system32Prefix.size()) == 0;
}

class ScopedWow64FsRedirectionDisable {
public:
    explicit ScopedWow64FsRedirectionDisable(bool enabled) {
        if (enabled && g_Wow64DisableWow64FsRedirection &&
            g_Wow64RevertWow64FsRedirection) {
            disabled_ = !!g_Wow64DisableWow64FsRedirection(&oldValue_);
        }
    }

    ~ScopedWow64FsRedirectionDisable() {
        if (disabled_) {
            g_Wow64RevertWow64FsRedirection(oldValue_);
        }
    }

    ScopedWow64FsRedirectionDisable(const ScopedWow64FsRedirectionDisable&) = delete;
    ScopedWow64FsRedirectionDisable& operator=(const ScopedWow64FsRedirectionDisable&) = delete;

    bool disabled() const {
        return disabled_;
    }

private:
    PVOID oldValue_ = nullptr;
    bool disabled_ = false;
};


static DWORD ClampDword(DWORD value, DWORD minimum, DWORD maximum) {
    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return value;
}

static void LoadSettings() {
    LoadTargetPathAllowlistSetting();

    g_patchDebugChildrenEarly = Wh_GetIntSetting(L"patchDebugChildrenEarly") != 0;
    g_autoAlwaysOnTop = Wh_GetIntSetting(L"autoAlwaysOnTop") != 0;
    g_autoAlwaysOnTopVisibleOnly = Wh_GetIntSetting(L"autoAlwaysOnTopVisibleOnly") != 0;
    g_skipChromiumElectronSubprocessTargets =
        Wh_GetIntSetting(L"skipChromiumElectronSubprocessTargets") != 0;

    g_autoAlwaysOnTopDurationMs = static_cast<DWORD>(
        Wh_GetIntSetting(L"autoAlwaysOnTopDurationMs"));
    if (g_autoAlwaysOnTopDurationMs != 0) {
        g_autoAlwaysOnTopDurationMs = ClampDword(g_autoAlwaysOnTopDurationMs,
                                                 500,
                                                 120000);
    }

    g_autoAlwaysOnTopIntervalMs = ClampDword(
        static_cast<DWORD>(Wh_GetIntSetting(L"autoAlwaysOnTopIntervalMs")),
        50,
        5000);
}

static BOOL CALLBACK AutoTopmostEnumWindowsProc(HWND hwnd, LPARAM) {
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid != GetCurrentProcessId()) {
        return TRUE;
    }

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (style & WS_CHILD) {
        return TRUE;
    }

    if (g_autoAlwaysOnTopVisibleOnly && !IsWindowVisible(hwnd)) {
        return TRUE;
    }

    SetWindowPos(hwnd,
                 HWND_TOPMOST,
                 0,
                 0,
                 0,
                 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                     SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
    return TRUE;
}

static void ApplyAutoAlwaysOnTop() {
    EnumWindows(AutoTopmostEnumWindowsProc, 0);
}

static DWORD WINAPI AutoTopmostThreadProc(LPVOID) {
    Wh_Log(L"[Always UIAccess] Auto always-on-top worker started. duration=%u interval=%u visibleOnly=%d",
           g_autoAlwaysOnTopDurationMs,
           g_autoAlwaysOnTopIntervalMs,
           g_autoAlwaysOnTopVisibleOnly ? 1 : 0);

    const DWORD started = GetTickCount();

    for (;;) {
        ApplyAutoAlwaysOnTop();

        if (g_autoAlwaysOnTopDurationMs != 0 &&
            GetTickCount() - started >= g_autoAlwaysOnTopDurationMs) {
            break;
        }

        if (WaitForSingleObject(g_autoTopmostStopEvent,
                                g_autoAlwaysOnTopIntervalMs) == WAIT_OBJECT_0) {
            break;
        }
    }

    Wh_Log(L"[Always UIAccess] Auto always-on-top worker stopped.");
    return 0;
}

static bool StartAutoTopmostThreadIfNeeded() {
    if (!g_autoAlwaysOnTop || g_autoTopmostThread) {
        return g_autoTopmostThread != nullptr;
    }

    g_autoTopmostStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_autoTopmostStopEvent) {
        Wh_Log(L"[Always UIAccess] CreateEventW(auto-topmost stop) failed: %u",
               GetLastError());
        return false;
    }

    g_autoTopmostThread = CreateThread(nullptr,
                                       0,
                                       AutoTopmostThreadProc,
                                       nullptr,
                                       0,
                                       nullptr);
    if (!g_autoTopmostThread) {
        Wh_Log(L"[Always UIAccess] CreateThread(auto-topmost) failed: %u",
               GetLastError());
        CloseHandle(g_autoTopmostStopEvent);
        g_autoTopmostStopEvent = nullptr;
        return false;
    }

    return true;
}

static void StopAutoTopmostThread() {
    if (g_autoTopmostStopEvent) {
        SetEvent(g_autoTopmostStopEvent);
    }

    if (g_autoTopmostThread) {
        WaitForSingleObject(g_autoTopmostThread, 3000);
        CloseHandle(g_autoTopmostThread);
        g_autoTopmostThread = nullptr;
    }

    if (g_autoTopmostStopEvent) {
        CloseHandle(g_autoTopmostStopEvent);
        g_autoTopmostStopEvent = nullptr;
    }
}

static bool KeepModLoadedForAutoTopmostIfEnabled() {
    if (!g_autoAlwaysOnTop) {
        return false;
    }

    if (!CurrentProcessHasUIAccess()) {
        Wh_Log(L"[Always UIAccess] Auto always-on-top requested, but process does not have UIAccess.");
        return false;
    }

    StartAutoTopmostThreadIfNeeded();
    return g_autoTopmostThread != nullptr;
}

static void SetResponseError(UIAccessResponse* response,
                             const wchar_t* message,
                             DWORD win32Error = 0,
                             NTSTATUS ntStatus = 0) {
    response->size = sizeof(*response);
    response->success = FALSE;
    response->win32Error = win32Error;
    response->ntStatus = ntStatus;
    wcsncpy_s(response->message, message, _TRUNCATE);
}

static bool IsWindhawkProcess() {
    WCHAR path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, ARRAYSIZE(path))) {
        return false;
    }

    const WCHAR* base = wcsrchr(path, L'\\');
    return base && _wcsicmp(base + 1, L"windhawk.exe") == 0;
}

static bool IsWindhawkServiceProcess() {
    if (!IsWindhawkProcess()) {
        return false;
    }

    const WCHAR* cmd = GetCommandLineW();
    const WCHAR* serviceArg = wcsstr(cmd, L" -service");
    if (!serviceArg) {
        return false;
    }

    WCHAR next = serviceArg[9];
    return (next == L'\0' || next == L' ') && !wcsstr(cmd, L"-tray-only");
}

static bool EnablePrivilege(PCWSTR privilegeName) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &token)) {
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    bool ok = false;
    if (LookupPrivilegeValueW(nullptr,
                              privilegeName,
                              &privileges.Privileges[0].Luid)) {
        AdjustTokenPrivileges(token,
                              FALSE,
                              &privileges,
                              sizeof(privileges),
                              nullptr,
                              nullptr);
        ok = GetLastError() != ERROR_NOT_ALL_ASSIGNED;
    }

    CloseHandle(token);
    return ok;
}

static bool TokenHasUIAccess(HANDLE token) {
    DWORD uiAccess = 0;
    DWORD returned = 0;
    return !!GetTokenInformation(token,
                                 TOKEN_UI_ACCESS_CLASS,
                                 &uiAccess,
                                 sizeof(uiAccess),
                                 &returned) &&
           uiAccess != 0;
}

static bool CurrentProcessHasUIAccess() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    bool result = TokenHasUIAccess(token);
    CloseHandle(token);
    return result;
}

static bool QueryCurrentProcessPath(WCHAR path[MAX_PATH]) {
    return !!GetModuleFileNameW(nullptr, path, MAX_PATH);
}

static void CopyFirstCommandLineToken(PCWSTR commandLine,
                                      WCHAR token[MAX_PATH]) {
    token[0] = L'\0';
    if (!commandLine) {
        return;
    }

    while (*commandLine == L' ' || *commandLine == L'\t') {
        ++commandLine;
    }

    if (!*commandLine) {
        return;
    }

    const WCHAR* start = commandLine;
    const WCHAR* end = nullptr;

    if (*start == L'"') {
        ++start;
        end = wcschr(start, L'"');
        if (!end) {
            end = start + wcslen(start);
        }
    } else {
        end = start;
        while (*end && *end != L' ' && *end != L'\t') {
            ++end;
        }
    }

    size_t length = static_cast<size_t>(end - start);
    if (length >= MAX_PATH) {
        length = MAX_PATH - 1;
    }

    wcsncpy_s(token, MAX_PATH, start, length);
}


static bool SearchPathToString(PCWSTR fileName, std::wstring* resolved) {
    WCHAR found[MAX_PATH]{};
    DWORD copied = SearchPathW(nullptr,
                               fileName,
                               L".exe",
                               ARRAYSIZE(found),
                               found,
                               nullptr);
    if (copied && copied < ARRAYSIZE(found)) {
        *resolved = FullPathToString(found);
        return true;
    }

    return false;
}

static bool ResolveCreateProcessTargetPath(LPCWSTR applicationName,
                                           LPCWSTR commandLine,
                                           std::wstring* resolved) {
    WCHAR rawTarget[MAX_PATH]{};

    if (applicationName && *applicationName) {
        wcsncpy_s(rawTarget, applicationName, _TRUNCATE);
    } else {
        CopyFirstCommandLineToken(commandLine, rawTarget);
    }

    std::wstring target = ExpandEnvironmentStringsToString(rawTarget);
    TrimStringInPlace(&target);
    if (target.empty()) {
        return false;
    }

    if (IsAbsolutePath(target.c_str()) || wcschr(target.c_str(), L'\\') ||
        wcschr(target.c_str(), L'/')) {
        *resolved = FullPathToString(target.c_str());
        return !resolved->empty();
    }

    if (SearchPathToString(target.c_str(), resolved)) {
        return true;
    }

    *resolved = FullPathToString(target.c_str());
    return !resolved->empty();
}

static bool QueryCurrentDirectoryPath(WCHAR path[MAX_PATH]) {
    DWORD copied = GetCurrentDirectoryW(MAX_PATH, path);
    return copied != 0 && copied < MAX_PATH;
}

static bool AppendRelaunchMarkerToEnvironment(LPVOID environment,
                                               std::vector<WCHAR>* patched) {
    patched->clear();

    if (!environment) {
        return false;
    }

    const WCHAR* cursor = static_cast<const WCHAR*>(environment);

    while (*cursor) {
        size_t itemLength = wcslen(cursor) + 1;
        patched->insert(patched->end(), cursor, cursor + itemLength);
        cursor += itemLength;
    }

    size_t markerLength = wcslen(kRelaunchMarkerEntry) + 1;
    patched->insert(patched->end(),
                    kRelaunchMarkerEntry,
                    kRelaunchMarkerEntry + markerLength);
    patched->push_back(L'\0');
    return true;
}

static bool SetTokenUIAccess(HANDLE token, UIAccessResponse* response) {
    ULONG uiAccess = 1;
    NTSTATUS status = g_NtSetInformationToken(
        token,
        TOKEN_UI_ACCESS_CLASS,
        &uiAccess,
        sizeof(uiAccess));

    if (status < 0) {
        WCHAR message[160]{};
        swprintf_s(message,
                   L"NtSetInformationToken(TokenUIAccess) failed: 0x%08X",
                   static_cast<ULONG>(status));
        SetResponseError(response, message, 0, status);
        return false;
    }

    return true;
}


static bool PatchClientProcessTokenUIAccess(DWORD clientPid,
                                            PCWSTR clientPathForPolicy,
                                            UIAccessResponse* response) {
    response->size = sizeof(*response);

    if (!IsPathInTargetAllowlist(clientPathForPolicy)) {
        SetResponseError(response,
                         L"Target path is not in the UIAccess allowlist.",
                         ERROR_ACCESS_DENIED);
        return false;
    }

    if (!g_NtSetInformationToken) {
        SetResponseError(response, L"NtSetInformationToken is unavailable.");
        return false;
    }

    if (!EnablePrivilege(SE_TCB_NAME)) {
        SetResponseError(response,
                         L"SeTcbPrivilege is unavailable. Windhawk must be running as a service.",
                         GetLastError());
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE,
                                 clientPid);
    if (!process) {
        SetResponseError(response,
                         L"OpenProcess(client) failed.",
                         GetLastError());
        return false;
    }

    HANDLE token = nullptr;
    if (!OpenProcessToken(process,
                          TOKEN_QUERY | TOKEN_ADJUST_DEFAULT,
                          &token)) {
        DWORD error = GetLastError();
        CloseHandle(process);
        SetResponseError(response,
                         L"OpenProcessToken(client for in-place UIAccess patch) failed.",
                         error);
        return false;
    }

    CloseHandle(process);

    if (!SetTokenUIAccess(token, response)) {
        CloseHandle(token);
        return false;
    }

    if (!TokenHasUIAccess(token)) {
        CloseHandle(token);
        SetResponseError(response,
                         L"TokenUIAccess was set but verification still reports UIAccess=0.",
                         ERROR_INVALID_DATA);
        return false;
    }

    CloseHandle(token);

    response->success = TRUE;
    response->pid = clientPid;
    response->tid = 0;
    response->win32Error = 0;
    response->ntStatus = 0;
    response->message[0] = L'\0';
    return true;
}

static HANDLE DuplicateClientPrimaryToken(DWORD clientPid,
                                          UIAccessResponse* response) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE,
                                 clientPid);
    if (!process) {
        SetResponseError(response,
                         L"OpenProcess(client) failed.",
                         GetLastError());
        return nullptr;
    }

    HANDLE sourceToken = nullptr;
    if (!OpenProcessToken(process,
                          TOKEN_DUPLICATE | TOKEN_QUERY,
                          &sourceToken)) {
        DWORD error = GetLastError();
        CloseHandle(process);
        SetResponseError(response, L"OpenProcessToken(client) failed.", error);
        return nullptr;
    }

    CloseHandle(process);

    HANDLE duplicatedToken = nullptr;
    BOOL duplicated = DuplicateTokenEx(
        sourceToken,
        TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
            TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
        nullptr,
        SecurityImpersonation,
        TokenPrimary,
        &duplicatedToken);
    DWORD duplicateError = GetLastError();
    CloseHandle(sourceToken);

    if (!duplicated) {
        SetResponseError(response,
                         L"DuplicateTokenEx(client token) failed.",
                         duplicateError);
        return nullptr;
    }

    return duplicatedToken;
}


static bool DuplicateProcessHandleForClient(DWORD clientPid,
                                            HANDLE sourceHandle,
                                            ULONGLONG* duplicatedHandleValue,
                                            UIAccessResponse* response) {
    *duplicatedHandleValue = 0;

    HANDLE clientProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, clientPid);
    if (!clientProcess) {
        SetResponseError(response,
                         L"OpenProcess(client for handle duplication) failed.",
                         GetLastError());
        return false;
    }

    HANDLE duplicatedHandle = nullptr;
    BOOL duplicated = DuplicateHandle(GetCurrentProcess(),
                                      sourceHandle,
                                      clientProcess,
                                      &duplicatedHandle,
                                      0,
                                      FALSE,
                                      DUPLICATE_SAME_ACCESS);
    DWORD duplicateError = GetLastError();
    CloseHandle(clientProcess);

    if (!duplicated) {
        SetResponseError(response,
                         L"DuplicateHandle(process/thread into client) failed.",
                         duplicateError);
        return false;
    }

    *duplicatedHandleValue = static_cast<ULONGLONG>(reinterpret_cast<ULONG_PTR>(duplicatedHandle));
    return true;
}

static BOOL CreateProcessAsUserPreservingSystem32W(
    HANDLE token,
    LPCWSTR applicationName,
    LPWSTR commandLine,
    LPSECURITY_ATTRIBUTES processAttributes,
    LPSECURITY_ATTRIBUTES threadAttributes,
    BOOL inheritHandles,
    DWORD creationFlags,
    LPVOID environment,
    LPCWSTR currentDirectory,
    LPSTARTUPINFOW startupInfo,
    LPPROCESS_INFORMATION processInfo) {
    // If the Windhawk service copy running this code is 32-bit/WOW64, a request
    // for C:\Windows\System32\foo.exe would otherwise be file-system redirected
    // to C:\Windows\SysWOW64\foo.exe. Disable redirection only around the
    // process creation call so exact System32 allowlist entries stay exact.
    ScopedWow64FsRedirectionDisable wow64RedirectionGuard(
        IsNativeSystem32Path(applicationName));

    BOOL created = CreateProcessAsUserW(token,
                                        applicationName,
                                        commandLine,
                                        processAttributes,
                                        threadAttributes,
                                        inheritHandles,
                                        creationFlags,
                                        environment,
                                        currentDirectory,
                                        startupInfo,
                                        processInfo);

    if (wow64RedirectionGuard.disabled() && created) {
        Wh_Log(L"[Always UIAccess] Created System32 target with WOW64 file-system redirection temporarily disabled: %s",
               applicationName ? applicationName : L"(null)");
    }

    return created;
}

static bool CreateUIAccessTargetProcessForClient(DWORD clientPid,
                                                 const UIAccessRequest* request,
                                                 UIAccessResponse* response) {
    response->size = sizeof(*response);

    if (!IsPathInTargetAllowlist(request->exe)) {
        SetResponseError(response,
                         L"Target path is not in the UIAccess allowlist.",
                         ERROR_ACCESS_DENIED);
        return false;
    }

    if (!g_NtSetInformationToken) {
        SetResponseError(response, L"NtSetInformationToken is unavailable.");
        return false;
    }

    if (!EnablePrivilege(SE_TCB_NAME)) {
        SetResponseError(response,
                         L"SeTcbPrivilege is unavailable. Windhawk must be running as a service.",
                         GetLastError());
        return false;
    }

    EnablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME);
    EnablePrivilege(SE_INCREASE_QUOTA_NAME);

    HANDLE token = DuplicateClientPrimaryToken(clientPid, response);
    if (!token) {
        return false;
    }

    if (!SetTokenUIAccess(token, response)) {
        CloseHandle(token);
        return false;
    }

    LPVOID environment = nullptr;
    LPVOID environmentToUse = nullptr;
    std::vector<WCHAR> patchedEnvironment;
    DWORD creationFlags = CREATE_DEFAULT_ERROR_MODE;

    DWORD preservedCreationFlags = request->creationFlags &
        (CREATE_SUSPENDED | CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP |
         CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_SEPARATE_WOW_VDM |
         CREATE_BREAKAWAY_FROM_JOB);
    creationFlags |= preservedCreationFlags;

    if (g_CreateEnvironmentBlock &&
        g_CreateEnvironmentBlock(&environment, token, FALSE)) {
        if (AppendRelaunchMarkerToEnvironment(environment, &patchedEnvironment)) {
            environmentToUse = patchedEnvironment.data();
        } else {
            environmentToUse = environment;
        }

        creationFlags |= CREATE_UNICODE_ENVIRONMENT;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    if (request->startupFlags & STARTF_USESHOWWINDOW) {
        startupInfo.dwFlags |= STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = request->startupShowWindow;
    }

    PROCESS_INFORMATION processInfo{};
    std::wstring commandLine = request->commandLine[0]
        ? request->commandLine
        : std::wstring(L"\"") + request->exe + L"\"";

    BOOL created = CreateProcessAsUserPreservingSystem32W(
        token,
        request->exe[0] ? request->exe : nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        creationFlags,
        environmentToUse,
        request->currentDirectory[0] ? request->currentDirectory : nullptr,
        &startupInfo,
        &processInfo);
    DWORD createError = GetLastError();

    if (environment && g_DestroyEnvironmentBlock) {
        g_DestroyEnvironmentBlock(environment);
    }

    CloseHandle(token);

    if (!created) {
        SetResponseError(response,
                         L"CreateProcessAsUserW(target from hook host) failed.",
                         createError);
        return false;
    }

    AllowSetForegroundWindow(processInfo.dwProcessId);

    ULONGLONG duplicatedProcessHandle = 0;
    ULONGLONG duplicatedThreadHandle = 0;
    bool duplicatedHandles =
        DuplicateProcessHandleForClient(clientPid,
                                        processInfo.hProcess,
                                        &duplicatedProcessHandle,
                                        response) &&
        DuplicateProcessHandleForClient(clientPid,
                                        processInfo.hThread,
                                        &duplicatedThreadHandle,
                                        response);

    if (!duplicatedHandles) {
        TerminateProcess(processInfo.hProcess, ERROR_ACCESS_DENIED);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return false;
    }

    response->success = TRUE;
    response->pid = processInfo.dwProcessId;
    response->tid = processInfo.dwThreadId;
    response->processHandle = duplicatedProcessHandle;
    response->threadHandle = duplicatedThreadHandle;
    response->win32Error = 0;
    response->ntStatus = 0;
    response->message[0] = L'\0';

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

static bool CreateUIAccessProcessForClient(DWORD clientPid,
                                           const UIAccessRequest* request,
                                           UIAccessResponse* response) {
    response->size = sizeof(*response);

    if (!IsPathInTargetAllowlist(request->exe)) {
        SetResponseError(response,
                         L"Target path is not in the UIAccess allowlist.",
                         ERROR_ACCESS_DENIED);
        return false;
    }

    if (!g_NtSetInformationToken) {
        SetResponseError(response, L"NtSetInformationToken is unavailable.");
        return false;
    }

    if (!EnablePrivilege(SE_TCB_NAME)) {
        SetResponseError(response,
                         L"SeTcbPrivilege is unavailable. Windhawk must be running as a service.",
                         GetLastError());
        return false;
    }

    EnablePrivilege(SE_ASSIGNPRIMARYTOKEN_NAME);
    EnablePrivilege(SE_INCREASE_QUOTA_NAME);

    HANDLE token = DuplicateClientPrimaryToken(clientPid, response);
    if (!token) {
        return false;
    }

    if (!SetTokenUIAccess(token, response)) {
        CloseHandle(token);
        return false;
    }

    LPVOID environment = nullptr;
    LPVOID environmentToUse = nullptr;
    std::vector<WCHAR> patchedEnvironment;
    DWORD creationFlags = CREATE_DEFAULT_ERROR_MODE;

    if (g_CreateEnvironmentBlock &&
        g_CreateEnvironmentBlock(&environment, token, FALSE)) {
        if (AppendRelaunchMarkerToEnvironment(environment, &patchedEnvironment)) {
            environmentToUse = patchedEnvironment.data();
        } else {
            environmentToUse = environment;
        }

        creationFlags |= CREATE_UNICODE_ENVIRONMENT;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_SHOWNORMAL;

    PROCESS_INFORMATION processInfo{};
    std::wstring commandLine = request->commandLine[0]
        ? request->commandLine
        : std::wstring(L"\"") + request->exe + L"\"";

    BOOL created = CreateProcessAsUserPreservingSystem32W(
        token,
        request->exe[0] ? request->exe : nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        creationFlags,
        environmentToUse,
        request->currentDirectory[0] ? request->currentDirectory : nullptr,
        &startupInfo,
        &processInfo);
    DWORD createError = GetLastError();

    if (environment && g_DestroyEnvironmentBlock) {
        g_DestroyEnvironmentBlock(environment);
    }

    CloseHandle(token);

    if (!created) {
        SetResponseError(response,
                         L"CreateProcessAsUserW failed.",
                         createError);
        return false;
    }

    AllowSetForegroundWindow(processInfo.dwProcessId);

    response->success = TRUE;
    response->pid = processInfo.dwProcessId;
    response->tid = processInfo.dwThreadId;
    response->win32Error = 0;
    response->ntStatus = 0;
    response->message[0] = L'\0';

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

static HANDLE CreateServerPipe() {
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
    SECURITY_ATTRIBUTES securityAttributes{};
    SECURITY_ATTRIBUTES* securityAttributesPtr = nullptr;

    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)",
            SDDL_REVISION_1,
            &securityDescriptor,
            nullptr)) {
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.lpSecurityDescriptor = securityDescriptor;
        securityAttributes.bInheritHandle = FALSE;
        securityAttributesPtr = &securityAttributes;
    }

    DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
    DWORD pipeMode = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT;
#ifdef PIPE_REJECT_REMOTE_CLIENTS
    pipeMode |= PIPE_REJECT_REMOTE_CLIENTS;
#endif

    HANDLE pipe = CreateNamedPipeW(
        kPipeName,
        openMode,
        pipeMode,
        PIPE_UNLIMITED_INSTANCES,
        sizeof(UIAccessResponse),
        sizeof(UIAccessRequest),
        kPipeTimeoutMs,
        securityAttributesPtr);

    if (securityDescriptor) {
        LocalFree(securityDescriptor);
    }

    return pipe;
}

static bool WaitForPipeClient(HANDLE pipe) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        return false;
    }

    BOOL connected = ConnectNamedPipe(pipe, &overlapped);
    DWORD error = connected ? ERROR_SUCCESS : GetLastError();
    bool result = false;

    if (connected || error == ERROR_PIPE_CONNECTED) {
        result = true;
    } else if (error == ERROR_IO_PENDING) {
        HANDLE waitHandles[2] = { overlapped.hEvent, g_stopEvent };
        DWORD wait = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

        if (wait == WAIT_OBJECT_0) {
            DWORD transferred = 0;
            result = !!GetOverlappedResult(pipe,
                                           &overlapped,
                                           &transferred,
                                           FALSE);
        } else {
            CancelIo(pipe);
        }
    }

    CloseHandle(overlapped.hEvent);
    return result;
}

static DWORD WINAPI PipeServerThread(LPVOID) {
    Wh_Log(L"[Always UIAccess] Service pipe started: %s", kPipeName);

    while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateServerPipe();
        if (pipe == INVALID_HANDLE_VALUE) {
            Wh_Log(L"[Always UIAccess] CreateNamedPipeW failed: %u", GetLastError());
            Sleep(1000);
            continue;
        }

        if (!WaitForPipeClient(pipe)) {
            CloseHandle(pipe);
            continue;
        }

        UIAccessRequest request{};
        UIAccessResponse response{};
        response.size = sizeof(response);

        DWORD bytesRead = 0;
        BOOL readOk = ReadFile(pipe,
                               &request,
                               sizeof(request),
                               &bytesRead,
                               nullptr);

        ULONG clientPid = 0;
        if (readOk &&
            bytesRead == sizeof(request) &&
            request.size == sizeof(request) &&
            g_GetNamedPipeClientProcessId &&
            g_GetNamedPipeClientProcessId(pipe, &clientPid)) {
            if (request.flags & kRequestFlagCreateTargetProcess) {
                Wh_Log(L"[Always UIAccess] Hook-host target creation request from PID %u: %s",
                       clientPid,
                       request.exe);
                CreateUIAccessTargetProcessForClient(clientPid, &request, &response);
            } else if (request.flags & kRequestFlagPatchTargetProcessToken) {
                DWORD targetPid = request.targetPid;
                Wh_Log(L"[Always UIAccess] Early target token patch request from PID %u for PID %u: %s",
                       clientPid,
                       targetPid,
                       request.exe);

                if (targetPid) {
                    PatchClientProcessTokenUIAccess(targetPid, request.exe, &response);
                } else {
                    SetResponseError(&response,
                                     L"Target PID missing for token patch request.",
                                     ERROR_INVALID_PARAMETER);
                }
            } else if (request.flags & kRequestFlagPatchCurrentProcessToken) {
                Wh_Log(L"[Always UIAccess] In-place token patch request from PID %u: %s",
                       clientPid,
                       request.exe);
                PatchClientProcessTokenUIAccess(clientPid, request.exe, &response);
            } else {
                Wh_Log(L"[Always UIAccess] Relaunch request from PID %u: %s",
                       clientPid,
                       request.exe);
                CreateUIAccessProcessForClient(clientPid, &request, &response);
            }
        } else {
            SetResponseError(&response,
                             L"Invalid pipe request.",
                             readOk ? ERROR_INVALID_DATA : GetLastError());
        }

        DWORD bytesWritten = 0;
        WriteFile(pipe, &response, sizeof(response), &bytesWritten, nullptr);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    Wh_Log(L"[Always UIAccess] Service pipe stopped.");
    return 0;
}

static bool SendUIAccessRequest(DWORD flags,
                                  DWORD targetPid,
                                  PCWSTR targetExeForLog,
                                  DWORD creationFlags,
                                  const STARTUPINFOW* startupInfo,
                                  LPCWSTR currentDirectoryOverride,
                                  LPCWSTR commandLineOverride,
                                  UIAccessResponse* response) {
    UIAccessRequest request{};
    request.size = sizeof(request);
    request.flags = flags;
    request.targetPid = targetPid;
    request.creationFlags = creationFlags;
    if (startupInfo) {
        request.startupFlags = startupInfo->dwFlags;
        request.startupShowWindow = startupInfo->wShowWindow;
    }

    if (targetExeForLog && *targetExeForLog) {
        wcsncpy_s(request.exe, targetExeForLog, _TRUNCATE);
    } else if (!QueryCurrentProcessPath(request.exe)) {
        SetResponseError(response,
                         L"GetModuleFileNameW failed.",
                         GetLastError());
        return false;
    }

    if (currentDirectoryOverride && *currentDirectoryOverride) {
        wcsncpy_s(request.currentDirectory, currentDirectoryOverride, _TRUNCATE);
    } else {
        QueryCurrentDirectoryPath(request.currentDirectory);
    }

    if (commandLineOverride) {
        wcsncpy_s(request.commandLine, commandLineOverride, _TRUNCATE);
    } else if (flags & kRequestFlagCreateTargetProcess) {
        request.commandLine[0] = L'\0';
    } else {
        wcsncpy_s(request.commandLine, GetCommandLineW(), _TRUNCATE);
    }

    HANDLE pipe = INVALID_HANDLE_VALUE;

    for (int attempt = 0; attempt < 10; ++attempt) {
        pipe = CreateFileW(kPipeName,
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           nullptr,
                           OPEN_EXISTING,
                           0,
                           nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            break;
        }

        DWORD error = GetLastError();
        if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND) {
            SetResponseError(response, L"CreateFileW(pipe) failed.", error);
            return false;
        }

        WaitNamedPipeW(kPipeName, kPipeTimeoutMs);
    }

    if (pipe == INVALID_HANDLE_VALUE) {
        SetResponseError(response,
                         L"The Windhawk Always UIAccess service pipe is unavailable.",
                         GetLastError());
        return false;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    DWORD bytesWritten = 0;
    BOOL writeOk = WriteFile(pipe,
                             &request,
                             sizeof(request),
                             &bytesWritten,
                             nullptr);

    DWORD bytesRead = 0;
    BOOL readOk = FALSE;
    if (writeOk && bytesWritten == sizeof(request)) {
        readOk = ReadFile(pipe,
                          response,
                          sizeof(*response),
                          &bytesRead,
                          nullptr);
    }

    DWORD pipeError = GetLastError();
    CloseHandle(pipe);

    if (!writeOk || bytesWritten != sizeof(request)) {
        SetResponseError(response, L"WriteFile(pipe) failed.", pipeError);
        return false;
    }

    if (!readOk || bytesRead != sizeof(*response) || response->size != sizeof(*response)) {
        SetResponseError(response, L"ReadFile(pipe) failed.", pipeError);
        return false;
    }

    return response->success != FALSE;
}

static bool SendRelaunchRequest(UIAccessResponse* response) {
    return SendUIAccessRequest(0, 0, nullptr, 0, nullptr, nullptr, nullptr, response);
}

static bool SendPatchCurrentProcessTokenRequest(UIAccessResponse* response) {
    return SendUIAccessRequest(kRequestFlagPatchCurrentProcessToken,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               nullptr,
                               nullptr,
                               response);
}

static bool SendPatchTargetProcessTokenRequest(DWORD targetPid,
                                               PCWSTR targetExeForLog,
                                               UIAccessResponse* response) {
    return SendUIAccessRequest(kRequestFlagPatchTargetProcessToken,
                               targetPid,
                               targetExeForLog,
                               0,
                               nullptr,
                               nullptr,
                               nullptr,
                               response);
}


static bool SendCreateTargetProcessRequest(PCWSTR targetExe,
                                           DWORD creationFlags,
                                           const STARTUPINFOW* startupInfo,
                                           LPCWSTR currentDirectory,
                                           LPCWSTR commandLine,
                                           UIAccessResponse* response) {
    return SendUIAccessRequest(kRequestFlagCreateTargetProcess,
                               0,
                               targetExe,
                               creationFlags,
                               startupInfo,
                               currentDirectory,
                               commandLine,
                               response);
}

static bool RelaunchMarkerIsSet() {
    WCHAR buffer[4]{};
    return GetEnvironmentVariableW(kRelaunchMarkerName,
                                   buffer,
                                   ARRAYSIZE(buffer)) != 0;
}
static bool CommandLineHasChromiumOrElectronSubprocessSwitch(PCWSTR commandLine) {
    if (!commandLine || !*commandLine) {
        return false;
    }

    return wcsstr(commandLine, L" --type=") ||
           wcsstr(commandLine, L"\t--type=") ||
           wcsstr(commandLine, L" --mojo-platform-channel-handle=") ||
           wcsstr(commandLine, L"\t--mojo-platform-channel-handle=") ||
           wcsncmp(commandLine, L"--type=", 7) == 0 ||
           wcsncmp(commandLine, L"--mojo-platform-channel-handle=", 31) == 0;
}

static bool IsChromiumOrElectronSubprocess() {
    return CommandLineHasChromiumOrElectronSubprocessSwitch(GetCommandLineW());
}

static BOOL WINAPI CreateProcessW_Hook(
    LPCWSTR lpApplicationName,
    LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation) {
    std::wstring targetPath;
    bool targetIsUiAccessListed =
        ResolveCreateProcessTargetPath(lpApplicationName,
                                       lpCommandLine,
                                       &targetPath) &&
        IsPathInTargetAllowlist(targetPath.c_str());

    if (targetIsUiAccessListed && g_skipChromiumElectronSubprocessTargets &&
        CommandLineHasChromiumOrElectronSubprocessSwitch(lpCommandLine)) {
        Wh_Log(L"[Always UIAccess] Chromium/Electron subprocess target detected in CreateProcessW hook; leaving launch unchanged: %s",
               targetPath.c_str());
        targetIsUiAccessListed = false;
    }

    if (targetIsUiAccessListed && lpProcessInformation &&
        (dwCreationFlags & (DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS)) == 0) {
        UIAccessResponse response{};
        if (SendCreateTargetProcessRequest(targetPath.c_str(),
                                           dwCreationFlags,
                                           lpStartupInfo,
                                           lpCurrentDirectory,
                                           lpCommandLine,
                                           &response)) {
            lpProcessInformation->hProcess =
                reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(response.processHandle));
            lpProcessInformation->hThread =
                reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(response.threadHandle));
            lpProcessInformation->dwProcessId = response.pid;
            lpProcessInformation->dwThreadId = response.tid;
            Wh_Log(L"[Always UIAccess] Intercepted CreateProcessW in PID %u; created UIAccess target PID %u directly: %s",
                   GetCurrentProcessId(),
                   response.pid,
                   targetPath.c_str());
            SetLastError(ERROR_SUCCESS);
            return TRUE;
        }

        Wh_Log(L"[Always UIAccess] UIAccess target creation failed for %s: %s (win32=%u nt=0x%08X); falling back to normal CreateProcessW",
               targetPath.c_str(),
               response.message[0] ? response.message : L"unknown error",
               response.win32Error,
               static_cast<ULONG>(response.ntStatus));
    }

    BOOL result = g_CreateProcessW_Original(lpApplicationName,
                                            lpCommandLine,
                                            lpProcessAttributes,
                                            lpThreadAttributes,
                                            bInheritHandles,
                                            dwCreationFlags,
                                            lpEnvironment,
                                            lpCurrentDirectory,
                                            lpStartupInfo,
                                            lpProcessInformation);

    if (!result || !lpProcessInformation || !g_patchDebugChildrenEarly ||
        !targetIsUiAccessListed ||
        (dwCreationFlags & (DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS)) == 0) {
        return result;
    }

    UIAccessResponse response{};
    if (SendPatchTargetProcessTokenRequest(lpProcessInformation->dwProcessId,
                                           targetPath.c_str(),
                                           &response)) {
        Wh_Log(L"[Always UIAccess] Patched debug child PID %u before returning from CreateProcessW: %s",
               lpProcessInformation->dwProcessId,
               targetPath.c_str());
    } else {
        Wh_Log(L"[Always UIAccess] Failed to patch debug child PID %u before returning from CreateProcessW: %s (win32=%u nt=0x%08X)",
               lpProcessInformation->dwProcessId,
               response.message[0] ? response.message : L"unknown error",
               response.win32Error,
               static_cast<ULONG>(response.ntStatus));
    }

    return result;
}

static bool InstallCreateProcessHookIfNeeded() {
    if (g_createProcessHookInstalled) {
        return true;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) {
        return false;
    }

    void* createProcessW = reinterpret_cast<void*>(
        GetProcAddress(kernel32, "CreateProcessW"));
    if (!createProcessW) {
        Wh_Log(L"[Always UIAccess] GetProcAddress(CreateProcessW) failed.");
        return false;
    }

    if (!Wh_SetFunctionHook(createProcessW,
                            reinterpret_cast<void*>(CreateProcessW_Hook),
                            reinterpret_cast<void**>(&g_CreateProcessW_Original))) {
        Wh_Log(L"[Always UIAccess] Wh_SetFunctionHook(CreateProcessW) failed.");
        return false;
    }

    g_createProcessHookInstalled = true;
    Wh_Log(L"[Always UIAccess] Installed CreateProcessW hook in PID %u. UIAccess target patterns=%u",
           GetCurrentProcessId(),
           static_cast<unsigned>(g_uiAccessTargetPatterns.size()));
    return true;
}

static bool KeepModLoadedForHooksOrAutoTopmostIfNeeded() {
    bool keepLoaded = false;

    if (InstallCreateProcessHookIfNeeded()) {
        keepLoaded = true;
    }

    if (KeepModLoadedForAutoTopmostIfEnabled()) {
        keepLoaded = true;
    }

    return keepLoaded;
}

BOOL Wh_ModInit() {
    LoadSettings();

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        g_NtSetInformationToken = reinterpret_cast<NtSetInformationToken_t>(
            GetProcAddress(ntdll, "NtSetInformationToken"));
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32) {
        g_GetNamedPipeClientProcessId = reinterpret_cast<GetNamedPipeClientProcessId_t>(
            GetProcAddress(kernel32, "GetNamedPipeClientProcessId"));
        g_Wow64DisableWow64FsRedirection = reinterpret_cast<Wow64DisableWow64FsRedirection_t>(
            GetProcAddress(kernel32, "Wow64DisableWow64FsRedirection"));
        g_Wow64RevertWow64FsRedirection = reinterpret_cast<Wow64RevertWow64FsRedirection_t>(
            GetProcAddress(kernel32, "Wow64RevertWow64FsRedirection"));
    }

    HMODULE userenv = LoadLibraryW(L"userenv.dll");
    if (userenv) {
        g_CreateEnvironmentBlock = reinterpret_cast<CreateEnvironmentBlock_t>(
            GetProcAddress(userenv, "CreateEnvironmentBlock"));
        g_DestroyEnvironmentBlock = reinterpret_cast<DestroyEnvironmentBlock_t>(
            GetProcAddress(userenv, "DestroyEnvironmentBlock"));
    }

    if (IsWindhawkServiceProcess()) {
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_stopEvent) {
            return FALSE;
        }

        g_pipeThread = CreateThread(nullptr, 0, PipeServerThread, nullptr, 0, nullptr);
        if (!g_pipeThread) {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
            return FALSE;
        }

        return TRUE;
    }

    if (IsWindhawkProcess()) {
        return FALSE;
    }

    WCHAR currentPath[MAX_PATH]{};
    bool haveCurrentPath = QueryCurrentProcessPath(currentPath);
    bool currentProcessIsTarget =
        haveCurrentPath && IsPathInTargetAllowlist(currentPath);

    bool hasUIAccess = CurrentProcessHasUIAccess();
    if (hasUIAccess) {
        SetEnvironmentVariableW(kRelaunchMarkerName, nullptr);
        Wh_Log(L"[Always UIAccess] Process already has UIAccess.");
        return KeepModLoadedForHooksOrAutoTopmostIfNeeded() ? TRUE : FALSE;
    }

    if (!currentProcessIsTarget) {
        return KeepModLoadedForHooksOrAutoTopmostIfNeeded() ? TRUE : FALSE;
    }

    if (IsChromiumOrElectronSubprocess()) {
        Wh_Log(L"[Always UIAccess] Chromium/Electron subprocess target detected; skipping self-relaunch.");
        return KeepModLoadedForHooksOrAutoTopmostIfNeeded() ? TRUE : FALSE;
    }

    if (IsDebuggerPresent()) {
        UIAccessResponse response{};
        if (SendPatchCurrentProcessTokenRequest(&response) && CurrentProcessHasUIAccess()) {
            Wh_Log(L"[Always UIAccess] Debuggee target token patched in-place; continuing without relaunch.");
        } else {
            Wh_Log(L"[Always UIAccess] Debuggee target detected; in-place token patch failed: %s (win32=%u nt=0x%08X). Continuing without relaunch to avoid breaking the debugger relationship.",
                   response.message[0] ? response.message : L"unknown error",
                   response.win32Error,
                   static_cast<ULONG>(response.ntStatus));
        }
        return KeepModLoadedForHooksOrAutoTopmostIfNeeded() ? TRUE : FALSE;
    }

    if (RelaunchMarkerIsSet()) {
        Wh_Log(L"[Always UIAccess] Relaunch marker is set but UIAccess is absent; leaving target process unchanged.");
        return KeepModLoadedForHooksOrAutoTopmostIfNeeded() ? TRUE : FALSE;
    }

    UIAccessResponse response{};
    if (SendRelaunchRequest(&response)) {
        Wh_Log(L"[Always UIAccess] Relaunched target as UIAccess PID %u. Exiting original.",
               response.pid);
        ExitProcess(0);
    }

    Wh_Log(L"[Always UIAccess] Relaunch failed for target process: %s (win32=%u nt=0x%08X)",
           response.message[0] ? response.message : L"unknown error",
           response.win32Error,
           static_cast<ULONG>(response.ntStatus));
    return KeepModLoadedForHooksOrAutoTopmostIfNeeded() ? TRUE : FALSE;
}

void Wh_ModSettingsChanged() {
    LoadSettings();

    if (IsWindhawkProcess()) {
        return;
    }

    InstallCreateProcessHookIfNeeded();

    if (CurrentProcessHasUIAccess() && g_autoAlwaysOnTop) {
        StartAutoTopmostThreadIfNeeded();
    } else {
        StopAutoTopmostThread();
    }
}

void Wh_ModBeforeUninit() {
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }

    if (g_autoTopmostStopEvent) {
        SetEvent(g_autoTopmostStopEvent);
    }
}

void Wh_ModUninit() {
    StopAutoTopmostThread();

    if (g_pipeThread) {
        WaitForSingleObject(g_pipeThread, 3000);
        CloseHandle(g_pipeThread);
        g_pipeThread = nullptr;
    }

    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}