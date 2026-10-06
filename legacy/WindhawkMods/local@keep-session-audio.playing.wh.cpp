// ==WindhawkMod==
// @id              keep-session-audio-playing
// @name            Keep audio playing in switched-away sessions
// @description     Windows 8 - 11: stop the Windows Audio service from muting the previous console session when another user takes over (restores the Windows 7 behavior)
// @version         0.2.0
// @author          you
// @include         svchost.exe
// @architecture    x86-64
// @compilerOptions -ladvapi32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Keep audio playing in switched-away sessions

Since Windows 8 the Windows Audio service mutes every app of the previous console session
when another session becomes the "primary console audio session" (fast user switching):
`TsSessionNewPrimaryConsoleAudioSession` queues `CApplicationManager::MuteAllAppsInSession`
-> `SilenceAndRevokePLMExemption` (the same fade-out primitive used for closed/suspended
WinRT apps) and flags the old session as muted (`TsSessionIdIsMuted`, used by
`CApplicationManager::GetSoundLevel`).

All of that is only armed when `g_MaxSessions <= 1`. The service fills that global once, in
`TS_ServiceStart`, from `WinStationQueryEnforcementCore(0, 0, 1, &g_MaxSessions, 4, &len)`
and falls back to 1 when the call fails. This mod hooks that call (inside the Audiosrv
svchost only) and reports a limit of 2 - the state a multi-session SKU is in: nobody is
flagged muted and no mute work item is queued.

Where the code lives: audiosrv.dll on Windows 8/8.1, AudioSrvPolicyManager.dll on
Windows 10/11 (checked against 10.0.26100.5074).

After enabling the mod, restart the audio service (or reboot) so `TS_ServiceStart` runs
again with the hook in place:

    net stop audiosrv && net start audiosrv

Notes:
* Windhawk must be able to inject into the svchost that hosts Audiosrv. If the
  "Enable svchost.exe mitigation options" policy
  (HKLM\SYSTEM\CurrentControlSet\Control\SCMConfig\EnableSvchostMitigationPolicy) is on,
  code integrity guard refuses non-Microsoft DLLs there and the mod can't load.
* Only the session-switch mute is touched, not the PLM / Connected Standby fade paths.
*/
// ==/WindhawkModReadme==

#include <windows.h>

#include <cwctype>
#include <cwchar>
#include <string>

// BOOLEAN WinStationQueryEnforcementCore(HANDLE hServer, ULONG, ULONG queryClass,
//                                        PVOID buffer, ULONG bufferSize, PULONG returnLength);
// Signature recovered from the call site in audiosrv!TS_ServiceStart (Win 8.0 / 8.1):
//   rcx = 0, edx = 0, r8d = 1, r9 = &g_MaxSessions, [rsp+0x20] = 4, [rsp+0x28] = &len
using WinStationQueryEnforcementCore_t =
    BOOLEAN(WINAPI*)(HANDLE, ULONG, ULONG, PVOID, ULONG, PULONG);
static WinStationQueryEnforcementCore_t WinStationQueryEnforcementCore_Original;

static BOOLEAN WINAPI WinStationQueryEnforcementCore_Hook(HANDLE hServer,
                                                          ULONG arg2,
                                                          ULONG queryClass,
                                                          PVOID buffer,
                                                          ULONG bufferSize,
                                                          PULONG returnLength) {
    BOOLEAN ok = WinStationQueryEnforcementCore_Original(
        hServer, arg2, queryClass, buffer, bufferSize, returnLength);

    // Only the "max sessions" query audiosrv issues: class 1, DWORD output.
    if (queryClass == 1 && bufferSize == sizeof(DWORD) && buffer) {
        DWORD original = ok ? *static_cast<DWORD*>(buffer) : 0;
        if (!ok || original < 2) {
            Wh_Log(L"max sessions query: ok=%d value=%u -> reporting 2", (int)ok,
                   (unsigned)original);
            *static_cast<DWORD*>(buffer) = 2;
            if (returnLength) {
                *returnLength = sizeof(DWORD);
            }
            return TRUE;
        }
    }

    return ok;
}

// Value following `flag` (" -k " / " -s ") in a svchost command line, lower-cased and
// unquoted ("" if the flag is absent).
static std::wstring GetSvchostArg(const std::wstring& commandLine, const wchar_t* flag) {
    std::wstring lower = commandLine;
    for (auto& c : lower) {
        c = static_cast<wchar_t>(std::towlower(c));
    }

    size_t pos = lower.find(flag);
    if (pos == std::wstring::npos) {
        return L"";
    }

    pos += wcslen(flag);
    while (pos < lower.size() && lower[pos] == L' ') {
        pos++;
    }

    size_t end = pos;
    while (end < lower.size() && lower[end] != L' ') {
        end++;
    }

    std::wstring value = lower.substr(pos, end - pos);
    if (!value.empty() && value.front() == L'"') {
        value.erase(0, 1);
    }
    if (!value.empty() && value.back() == L'"') {
        value.pop_back();
    }
    return value;
}

// True if this svchost instance hosts Audiosrv. If that can't be determined we assume yes:
// the hook is harmless elsewhere because only audiosrv calls
// WinStationQueryEnforcementCore with class 1.
static bool IsAudioServiceHost() {
    std::wstring commandLine = GetCommandLineW();

    // Split svchost (Windows 10+ with enough RAM): one service per process, "-s <name>".
    std::wstring service = GetSvchostArg(commandLine, L" -s ");
    if (!service.empty()) {
        return service == L"audiosrv";
    }

    // Shared host: same "-k <group>" as Audiosrv's configured ImagePath.
    WCHAR imagePath[512];
    DWORD size = sizeof(imagePath);
    LSTATUS status = RegGetValueW(
        HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Audiosrv",
        L"ImagePath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr,
        imagePath, &size);
    if (status != ERROR_SUCCESS) {
        Wh_Log(L"Audiosrv ImagePath not readable (%ld), not filtering by service group",
               (long)status);
        return true;
    }

    std::wstring serviceGroup = GetSvchostArg(imagePath, L" -k ");
    std::wstring ourGroup = GetSvchostArg(commandLine, L" -k ");
    if (serviceGroup.empty() || ourGroup.empty()) {
        return true;
    }

    return serviceGroup == ourGroup;
}

BOOL Wh_ModInit() {
    if (!IsAudioServiceHost()) {
        return FALSE;
    }

    // audiosrv delay-loads this through ext-ms-win-session-winsta-l1-1-0 (-> winsta.dll).
    // Resolve it ourselves so the export can be hooked before the service starts; the later
    // delay-load resolves to the same (hooked) function.
    HMODULE winsta = LoadLibraryExW(L"ext-ms-win-session-winsta-l1-1-0.dll", nullptr,
                                    LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!winsta) {
        winsta = LoadLibraryExW(L"winsta.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (!winsta) {
        Wh_Log(L"loading the winsta apiset / winsta.dll failed: %lu", GetLastError());
        return FALSE;
    }

    void* target =
        reinterpret_cast<void*>(GetProcAddress(winsta, "WinStationQueryEnforcementCore"));
    if (!target) {
        Wh_Log(L"WinStationQueryEnforcementCore not exported by winsta.dll");
        return FALSE;
    }

    if (!Wh_SetFunctionHook(
            target, reinterpret_cast<void*>(WinStationQueryEnforcementCore_Hook),
            reinterpret_cast<void**>(&WinStationQueryEnforcementCore_Original))) {
        Wh_Log(L"Wh_SetFunctionHook failed");
        return FALSE;
    }

    Wh_Log(L"hooked, restart Audiosrv (or reboot) for it to take effect");
    return TRUE;
}

void Wh_ModUninit() {}