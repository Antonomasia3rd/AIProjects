// ==WindhawkMod==
// @id              snipping-tool-border-fix
// @name            Snipping Tool Border Fix
// @description     Disables extended-frame-bound queries inside Snipping Tool to avoid the Smart Crop border issue.
// @version         1.0
// @architecture    x86-64
// @include         SnippingTool.exe
// ==/WindhawkMod==

#include <windhawk_utils.h>
#include <dwmapi.h>

// Define the original function structure
typedef HRESULT (WINAPI *DwmGetWindowAttribute_t)(HWND, DWORD, PVOID, DWORD);
DwmGetWindowAttribute_t DwmGetWindowAttribute_Original;

// The Hijack logic
HRESULT WINAPI DwmGetWindowAttribute_Hook(HWND hwnd, DWORD dwAttribute, PVOID pvAttribute, DWORD cbAttribute) {
    if (dwAttribute == DWMWA_EXTENDED_FRAME_BOUNDS) {
        // Force the app to think the "Smart Crop" failed
        return E_NOTIMPL;
    }
    return DwmGetWindowAttribute_Original(hwnd, dwAttribute, pvAttribute, cbAttribute);
}

BOOL Wh_ModInit() {
    Wh_Log(L"Snipping Fix: Searching for DWM API...");

    HMODULE hDwmapi = GetModuleHandle(L"dwmapi.dll");
    if (!hDwmapi) {
        hDwmapi = LoadLibraryW(L"dwmapi.dll");
    }
    if (!hDwmapi) {
        Wh_Log(L"Snipping Fix: dwmapi.dll is unavailable.");
        return FALSE;
    }

    FARPROC pFunc = GetProcAddress(hDwmapi, "DwmGetWindowAttribute");
    if (!pFunc) {
        Wh_Log(L"Snipping Fix: DwmGetWindowAttribute is unavailable.");
        return FALSE;
    }
    if (!Wh_SetFunctionHook(
            (void*)pFunc,
            (void*)DwmGetWindowAttribute_Hook,
            (void**)&DwmGetWindowAttribute_Original)) {
        Wh_Log(L"Snipping Fix: Wh_SetFunctionHook failed.");
        return FALSE;
    }
    Wh_Log(L"Snipping Fix: Hooked successfully!");
    return TRUE;
}
