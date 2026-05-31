// ==WindhawkMod==
// @id              always-uiaccess
// @name            Always UIAccess
// @description     Relaunches included processes with TokenUIAccess set by the Windhawk service, like System Informer's Create UIAccess path.
// @version         2.0.0
// @author          local
// @include         windhawk.exe
// @compilerOptions -ladvapi32 -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
Use Windhawk's process inclusion list for the programs that should always run as
UIAccess. This mod does not keep its own target allowlist or blocklist.

Keep the windhawk.exe include. The mod needs one copy loaded in Windhawk's
-service process so it can set TokenUIAccess with SeTcbPrivilege, matching the
approach used by System Informer's Run As dialog.

Flow:

1. The targeted process starts and loads this mod.
2. If the token already has UIAccess, nothing is changed.
3. Otherwise, the target asks the Windhawk service process to relaunch it.
4. The service opens the target process token, duplicates it, enables
   TokenUIAccess on the duplicate, creates a replacement process with
   CreateProcessAsUserW, and the original target exits.

No osk.exe token source is used.
*/
// ==/WindhawkModReadme==

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>

#define TOKEN_UI_ACCESS_CLASS ((TOKEN_INFORMATION_CLASS)26)

static constexpr const wchar_t* kPipeName =
    L"\\\\.\\pipe\\WindhawkAlwaysUIAccess";
static constexpr const wchar_t* kRelaunchMarkerName =
    L"WH_ALWAYS_UIACCESS_RELAUNCHED";
static constexpr const wchar_t* kRelaunchMarkerEntry =
    L"WH_ALWAYS_UIACCESS_RELAUNCHED=1";
static constexpr DWORD kPipeTimeoutMs = 5000;
static constexpr size_t kCommandLineChars = 32768;

using NtSetInformationToken_t = NTSTATUS(NTAPI*)(
    HANDLE TokenHandle,
    TOKEN_INFORMATION_CLASS TokenInformationClass,
    PVOID TokenInformation,
    ULONG TokenInformationLength);

using CreateEnvironmentBlock_t = BOOL(WINAPI*)(LPVOID*, HANDLE, BOOL);
using DestroyEnvironmentBlock_t = BOOL(WINAPI*)(LPVOID);
using GetNamedPipeClientProcessId_t = BOOL(WINAPI*)(HANDLE, PULONG);

static NtSetInformationToken_t g_NtSetInformationToken = nullptr;
static CreateEnvironmentBlock_t g_CreateEnvironmentBlock = nullptr;
static DestroyEnvironmentBlock_t g_DestroyEnvironmentBlock = nullptr;
static GetNamedPipeClientProcessId_t g_GetNamedPipeClientProcessId = nullptr;
static HANDLE g_stopEvent = nullptr;
static HANDLE g_pipeThread = nullptr;

#pragma pack(push, 1)
struct UIAccessRequest {
    DWORD size;
    WCHAR exe[MAX_PATH];
    WCHAR currentDirectory[MAX_PATH];
    WCHAR commandLine[kCommandLineChars];
};

struct UIAccessResponse {
    DWORD size;
    BOOL success;
    DWORD pid;
    DWORD tid;
    DWORD win32Error;
    NTSTATUS ntStatus;
    WCHAR message[512];
};
#pragma pack(pop)

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

static bool CreateUIAccessProcessForClient(DWORD clientPid,
                                           const UIAccessRequest* request,
                                           UIAccessResponse* response) {
    response->size = sizeof(*response);

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

    BOOL created = CreateProcessAsUserW(
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
            Wh_Log(L"[Always UIAccess] Relaunch request from PID %u: %s",
                   clientPid,
                   request.exe);
            CreateUIAccessProcessForClient(clientPid, &request, &response);
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

static bool SendRelaunchRequest(UIAccessResponse* response) {
    UIAccessRequest request{};
    request.size = sizeof(request);

    if (!QueryCurrentProcessPath(request.exe)) {
        SetResponseError(response,
                         L"GetModuleFileNameW failed.",
                         GetLastError());
        return false;
    }

    QueryCurrentDirectoryPath(request.currentDirectory);
    wcsncpy_s(request.commandLine, GetCommandLineW(), _TRUNCATE);

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

static bool RelaunchMarkerIsSet() {
    WCHAR buffer[4]{};
    return GetEnvironmentVariableW(kRelaunchMarkerName,
                                   buffer,
                                   ARRAYSIZE(buffer)) != 0;
}
static bool IsChromiumOrElectronSubprocess() {
    const WCHAR* commandLine = GetCommandLineW();

    return wcsstr(commandLine, L" --type=") ||
           wcsstr(commandLine, L"\t--type=") ||
           wcsstr(commandLine, L" --mojo-platform-channel-handle=") ||
           wcsstr(commandLine, L"\t--mojo-platform-channel-handle=") ||
           wcsncmp(commandLine, L"--type=", 7) == 0 ||
           wcsncmp(commandLine, L"--mojo-platform-channel-handle=", 31) == 0;
}

BOOL Wh_ModInit() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        g_NtSetInformationToken = reinterpret_cast<NtSetInformationToken_t>(
            GetProcAddress(ntdll, "NtSetInformationToken"));
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32) {
        g_GetNamedPipeClientProcessId = reinterpret_cast<GetNamedPipeClientProcessId_t>(
            GetProcAddress(kernel32, "GetNamedPipeClientProcessId"));
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

    if (CurrentProcessHasUIAccess()) {
        SetEnvironmentVariableW(kRelaunchMarkerName, nullptr);
        Wh_Log(L"[Always UIAccess] Process already has UIAccess.");
        return FALSE;
    }

    if (IsChromiumOrElectronSubprocess()) {
        Wh_Log(L"[Always UIAccess] Chromium/Electron subprocess detected; skipping relaunch.");
        return FALSE;
    }

    if (RelaunchMarkerIsSet()) {
        Wh_Log(L"[Always UIAccess] Relaunch marker is set but UIAccess is absent; leaving process unchanged.");
        return FALSE;
    }

    UIAccessResponse response{};
    if (SendRelaunchRequest(&response)) {
        Wh_Log(L"[Always UIAccess] Relaunched as UIAccess PID %u. Exiting original.",
               response.pid);
        ExitProcess(0);
    }

    Wh_Log(L"[Always UIAccess] Relaunch failed: %s (win32=%u nt=0x%08X)",
           response.message[0] ? response.message : L"unknown error",
           response.win32Error,
           static_cast<ULONG>(response.ntStatus));
    return FALSE;
}

void Wh_ModBeforeUninit() {
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
}

void Wh_ModUninit() {
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