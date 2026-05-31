// ==WindhawkMod==
// @id              snipping-tool-border-fix
// @name            Snipping Tool Border Fix
// @architecture    x86-64
// ==/WindhawkMod==

#include <windhawk_utils.h>

// Define the original function structure
typedef HRESULT (WINAPI *DwmGetWindowAttribute_t)(HWND, DWORD, PVOID, DWORD);
DwmGetWindowAttribute_t DwmGetWindowAttribute_Original;

// The Hijack logic
HRESULT WINAPI DwmGetWindowAttribute_Hook(HWND hwnd, DWORD dwAttribute, PVOID pvAttribute, DWORD cbAttribute) {
    // ID 9 is DWMWA_EXTENDED_FRAME_BOUNDS
    if (dwAttribute == 9) {
        // Force the app to think the "Smart Crop" failed
        return 0x80004001; // E_NOTIMPL
    }
    return DwmGetWindowAttribute_Original(hwnd, dwAttribute, pvAttribute, cbAttribute);
}

BOOL Wh_ModInit() {
    Wh_Log(L"Snipping Fix: Searching for DWM API...");

    // Try to find the function in the process memory
    HMODULE hDwmapi = GetModuleHandle(L"dwmapi.dll");
    if (hDwmapi) {
        FARPROC pFunc = GetProcAddress(hDwmapi, "DwmGetWindowAttribute");
        if (pFunc) {
            Wh_SetFunctionHook(
                (void*)pFunc,
                (void*)DwmGetWindowAttribute_Hook,
                (void**)&DwmGetWindowAttribute_Original
            );
            Wh_Log(L"Snipping Fix: Hooked successfully!");
        }
    }
    return TRUE;
}