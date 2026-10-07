// ==WindhawkMod==
// @id              soft-lock-screen-combined-lab
// @name            Soft Lock Screen Combined Lab
// @description     Stage 2 native dismissible lock-screen presentation lab + passive LockApp validation for the exact tested Windows build.
// @version         0.3.0
// @author          Vivo GKB / ChatGPT
// @include         explorer.exe
// @include         LockApp.exe
// @architecture    amd64
// ==/WindhawkMod==
//
// ONE MOD, TWO PROCESS ROLES:
//
// explorer.exe:
//   - waits for LockController.dll to be lazily loaded
//   - exact-build validates it
//   - arms the one-shot VisualsController Stage 2 presentation test
//
// LockApp.exe:
//   - exact-build validates LockApp.exe + RSDS identity
//   - installs seven PASSIVE wallpaper/dismiss-flow observers
//
// The active Explorer-side Stage 2 presentation intentionally DOES NOT call:
//   LockScreenDirector::CreateController
//   LockScreenControllerProxy::Lock
//   UpdateCredentialsRequired
//   AreCredentialsRequired
//   RequestUnlock
//   Unlock
//   Winlogon/LogonUI RPC
//
// The LockApp-side observer does NOT call or modify:
//   RequestUnlock
//   NotifyBeginUnlock
//   SignalUnlock
//   UpdateCredentialsRequired
//   AreCredentialsRequired
//   Winlogon/LogonUI RPC
//
// ==WindhawkModSettings==
/*
- runTest: false
  $name: Run one Stage 2 presentation test
  $description: Starts only on a false -> true transition AFTER the Explorer side reports READY/ARMED. Uses a native presentation-only data context with credentialsRequired=false; it does not modify Windows authentication state.

- durationMs: 8000
  $name: Watchdog duration (ms)
  $description: Automatically requests VisualsController::Terminate after this many milliseconds. Recommended 5000-15000.

- logViewChanged: true
  $name: Log LockApp ScrollViewer callbacks
  $description: Logs up to the first 64 MainPage::_OnViewChanged calls per LockApp process. Disable if too noisy.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>

// ============================================================================
// Shared process-role dispatch.
// ============================================================================

enum class ProcessRole {
    Unknown,
    Explorer,
    LockApp,
};

ProcessRole g_processRole = ProcessRole::Unknown;

PCWSTR CurrentProcessBaseName() {
    static wchar_t path[MAX_PATH]{};
    static PCWSTR baseName = nullptr;

    if (baseName) {
        return baseName;
    }

    DWORD n = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (!n || n >= ARRAYSIZE(path)) {
        baseName = L"";
        return baseName;
    }

    wchar_t* slash = wcsrchr(path, L'\\');
    baseName = slash ? slash + 1 : path;
    return baseName;
}

// ============================================================================
// Explorer role: standalone VisualsController Stage 1.
// ============================================================================

namespace stage1 {
namespace {

constexpr DWORD kExpectedTimestamp   = 0x823859CD;
constexpr DWORD kExpectedSizeOfImage = 0x000AA000;
constexpr SIZE_T kVisualsControllerSize = 0x188;

HMODULE g_lockController = nullptr;

std::atomic<bool> g_unloading{false};
std::atomic<bool> g_armed{false};
std::atomic<bool> g_running{false};
std::atomic<bool> g_everStarted{false};
std::atomic<bool> g_previousRunSetting{false};

HANDLE g_stopEvent = nullptr;
HANDLE g_armStopEvent = nullptr;
HANDLE g_armThread = nullptr;
HANDLE g_workerThread = nullptr;
HANDLE g_instanceGuard = nullptr;
HANDLE g_dismissEvent = nullptr;

constexpr PCWSTR kDismissEventName =
    L"Local\\SoftLockCombinedLab-v030-Dismiss";

using OperatorNew_t = void* (__cdecl*)(SIZE_T);
using VisualsCtor_t = void* (__fastcall*)(void*);
using VisualsInit_t = HRESULT (__fastcall*)(void*, unsigned int, void*);
using VisualsStart_t = HRESULT (__fastcall*)(void*);
using VisualsShow_t = HRESULT (__fastcall*)(void*, bool, void*, unsigned int);
using VisualsTerminate_t = HRESULT (__fastcall*)(void*);

// Exact LOGONUI_DATA_CONTEXT layout recovered from this LockController build's
// native CLogonUIDataContext MakeAndInitialize implementation.
struct NativeLogonUIDataContext {
    uint32_t lockInstanceCookie;         // +0x00
    int32_t  isAltView;                  // +0x04
    PCWSTR   secureGestureText;          // +0x08
    PCWSTR   speedBumpText;              // +0x10
    int32_t  areCredentialsRequired;     // +0x18
    int32_t  shouldRequireSecureGesture; // +0x1C
    int32_t  shouldShowSpeedBump;        // +0x20
};
static_assert(sizeof(NativeLogonUIDataContext) == 0x28);

using MakeLogonUIDataContext_t =
    HRESULT (__fastcall*)(
        void** outClassObject,
        const NativeLogonUIDataContext** contextRef);

OperatorNew_t       g_operatorNew = nullptr;
VisualsCtor_t       g_ctor = nullptr;
VisualsInit_t       g_init = nullptr;
VisualsStart_t      g_start = nullptr;
VisualsShow_t       g_show = nullptr;
VisualsTerminate_t  g_terminate = nullptr;
MakeLogonUIDataContext_t g_makeLogonUIDataContext = nullptr;

// Exact IID queried by LockController immediately after creating
// CLogonUIDataContext on this build.
const GUID kIID_ILogonUIDataContext = {
    0xAF86E2E0, 0xB12D, 0x4C6A,
    {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}
};

using CoInitializeEx_t = HRESULT (WINAPI*)(LPVOID, DWORD);
using CoUninitialize_t = void (WINAPI*)();

CoInitializeEx_t g_CoInitializeEx = nullptr;
CoUninitialize_t g_CoUninitialize = nullptr;

struct SymbolWanted {
    PCWSTR decorated;
    void** out;
    PCWSTR label;
    bool found;
};

bool ResolveComApis() {
    HMODULE combase = GetModuleHandleW(L"combase.dll");
    if (!combase) {
        combase = LoadLibraryW(L"combase.dll");
    }

    if (!combase) {
        Wh_Log(L"[combined/stage1] combase.dll unavailable err=%lu",
               GetLastError());
        return false;
    }

    g_CoInitializeEx = reinterpret_cast<CoInitializeEx_t>(
        GetProcAddress(combase, "CoInitializeEx"));
    g_CoUninitialize = reinterpret_cast<CoUninitialize_t>(
        GetProcAddress(combase, "CoUninitialize"));

    if (!g_CoInitializeEx || !g_CoUninitialize) {
        Wh_Log(L"[combined/stage1] failed to resolve COM APIs "
               L"CoInitializeEx=%p CoUninitialize=%p",
               reinterpret_cast<void*>(g_CoInitializeEx),
               reinterpret_cast<void*>(g_CoUninitialize));
        return false;
    }

    Wh_Log(L"[combined/stage1] resolved COM APIs dynamically");
    return true;
}

bool ValidateModule(HMODULE module) {
    const auto* base = reinterpret_cast<const BYTE*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + dos->e_lfanew);

    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }

    const DWORD ts = nt->FileHeader.TimeDateStamp;
    const DWORD image = nt->OptionalHeader.SizeOfImage;

    Wh_Log(
        L"[combined/stage1] LockController identity "
        L"timestamp=0x%08X size=0x%08X",
        ts, image);

    return ts == kExpectedTimestamp &&
           image == kExpectedSizeOfImage;
}

bool ResolveSymbols() {
    SymbolWanted wanted[] = {
        {
            L"??2@YAPEAX_K@Z",
            reinterpret_cast<void**>(&g_operatorNew),
            L"module operator new(size_t)",
            false,
        },
        {
            L"??0VisualsController@@QEAA@XZ",
            reinterpret_cast<void**>(&g_ctor),
            L"VisualsController::VisualsController",
            false,
        },
        {
            L"?RuntimeClassInitialize@VisualsController@@QEAAJIPEAUILockAppHostInteropLocal@@@Z",
            reinterpret_cast<void**>(&g_init),
            L"VisualsController::RuntimeClassInitialize",
            false,
        },
        {
            L"?Start@VisualsController@@QEAAJXZ",
            reinterpret_cast<void**>(&g_start),
            L"VisualsController::Start",
            false,
        },
        {
            L"?Show@VisualsController@@QEAAJ_NPEAUILogonUIDataContext@@I@Z",
            reinterpret_cast<void**>(&g_show),
            L"VisualsController::Show",
            false,
        },
        {
            L"?Terminate@VisualsController@@QEAAJXZ",
            reinterpret_cast<void**>(&g_terminate),
            L"VisualsController::Terminate",
            false,
        },
        {
            L"??$MakeAndInitialize@VCLogonUIDataContext@@V1@AEAPEBULOGONUI_DATA_CONTEXT@@@Details@WRL@Microsoft@@YAJPEAPEAVCLogonUIDataContext@@AEAPEBULOGONUI_DATA_CONTEXT@@@Z",
            reinterpret_cast<void**>(&g_makeLogonUIDataContext),
            L"WRL::MakeAndInitialize<CLogonUIDataContext>",
            false,
        },
    };

    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;

    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(
        g_lockController, &options, &symbol);

    if (!search) {
        Wh_Log(
            L"[combined/stage1] Wh_FindFirstSymbol failed "
            L"for LockController.dll");
        return false;
    }

    size_t foundCount = 0;

    do {
        PCWSTR decorated = symbol.symbolDecorated;
        if (!decorated || !*decorated) {
            decorated = symbol.symbol;
        }
        if (!decorated || !*decorated) {
            continue;
        }

        for (auto& item : wanted) {
            if (item.found ||
                wcscmp(decorated, item.decorated) != 0) {
                continue;
            }

            *item.out = symbol.address;
            item.found = true;
            ++foundCount;

            Wh_Log(
                L"[combined/stage1] resolved %s at %p",
                item.label, symbol.address);
            break;
        }
    } while (Wh_FindNextSymbol(search, &symbol));

    Wh_FindCloseSymbol(search);

    bool ok = true;
    for (const auto& item : wanted) {
        if (!item.found) {
            Wh_Log(
                L"[combined/stage1] MISSING required symbol: %s (%s)",
                item.label, item.decorated);
            ok = false;
        }
    }

    Wh_Log(
        L"[combined/stage1] symbol resolution %llu/%llu",
        static_cast<unsigned long long>(foundCount),
        static_cast<unsigned long long>(ARRAYSIZE(wanted)));

    return ok;
}

bool IsDefaultInputDesktop() {
    HDESK desktop = OpenInputDesktop(
        0, FALSE, DESKTOP_READOBJECTS);

    if (!desktop) {
        Wh_Log(
            L"[combined/stage1] OpenInputDesktop failed err=%lu",
            GetLastError());
        return false;
    }

    wchar_t name[256]{};
    DWORD needed = 0;

    const BOOL ok = GetUserObjectInformationW(
        desktop,
        UOI_NAME,
        name,
        sizeof(name),
        &needed);

    const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    CloseDesktop(desktop);

    if (!ok) {
        Wh_Log(
            L"[combined/stage1] "
            L"GetUserObjectInformation(UOI_NAME) failed err=%lu",
            err);
        return false;
    }

    Wh_Log(L"[combined/stage1] input desktop=%s", name);
    return _wcsicmp(name, L"Default") == 0;
}

bool IsLogonUiRunning() {
    HANDLE snapshot =
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE) {
        Wh_Log(
            L"[combined/stage1] process snapshot failed "
            L"err=%lu; failing closed",
            GetLastError());
        return true;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    bool found = false;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"LogonUI.exe") == 0) {
                found = true;
                Wh_Log(
                    L"[combined/stage1] LogonUI.exe is running "
                    L"pid=%lu; refusing test",
                    entry.th32ProcessID);
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return found;
}

using QueryInterface_t =
    HRESULT (__fastcall*)(void*, const GUID&, void**);
using ReleaseUnknown_t =
    ULONG (__fastcall*)(void*);

ULONG ReleaseUnknown(void* object) {
    if (!object) {
        return 0;
    }

    void** vtable = *reinterpret_cast<void***>(object);
    if (!vtable || !vtable[2]) {
        Wh_Log(
            L"[combined/stage2] invalid IUnknown vtable during Release");
        return 0;
    }

    auto release =
        reinterpret_cast<ReleaseUnknown_t>(vtable[2]);
    return release(object);
}

HRESULT QueryUnknown(
    void* object,
    const GUID& iid,
    void** result)
{
    if (!object || !result) {
        return E_POINTER;
    }

    *result = nullptr;
    void** vtable = *reinterpret_cast<void***>(object);
    if (!vtable || !vtable[0]) {
        return E_NOINTERFACE;
    }

    auto qi = reinterpret_cast<QueryInterface_t>(vtable[0]);
    return qi(object, iid, result);
}

HRESULT CreatePresentationDataContext(
    unsigned int cookie,
    void** outDataContext)
{
    if (!outDataContext) {
        return E_POINTER;
    }

    *outDataContext = nullptr;

    if (!g_makeLogonUIDataContext) {
        return E_POINTER;
    }

    static constexpr wchar_t kEmpty[] = L"";

    NativeLogonUIDataContext native{};
    native.lockInstanceCookie = cookie;
    native.isAltView = 0;
    native.secureGestureText = kEmpty;
    native.speedBumpText = kEmpty;

    // IMPORTANT: presentation metadata only. This does NOT call
    // UpdateCredentialsRequired or mutate LockScreenController state.
    native.areCredentialsRequired = 0;
    native.shouldRequireSecureGesture = 0;
    native.shouldShowSpeedBump = 0;

    const NativeLogonUIDataContext* nativePtr = &native;
    void* classObject = nullptr;

    HRESULT hr =
        g_makeLogonUIDataContext(&classObject, &nativePtr);

    Wh_Log(
        L"[combined/stage2] native CLogonUIDataContext "
        L"MakeAndInitialize -> 0x%08X class=%p cookie=%u",
        static_cast<unsigned int>(hr),
        classObject,
        cookie);

    if (FAILED(hr) || !classObject) {
        if (classObject) {
            ReleaseUnknown(classObject);
        }
        return FAILED(hr) ? hr : E_FAIL;
    }

    void* iface = nullptr;
    hr = QueryUnknown(
        classObject,
        kIID_ILogonUIDataContext,
        &iface);

    Wh_Log(
        L"[combined/stage2] QI(ILogonUIDataContext) "
        L"-> 0x%08X iface=%p",
        static_cast<unsigned int>(hr),
        iface);

    // Drop the original CLogonUIDataContext owner reference. On success the
    // queried interface owns the remaining reference.
    ReleaseUnknown(classObject);

    if (FAILED(hr) || !iface) {
        if (iface) {
            ReleaseUnknown(iface);
        }
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    *outDataContext = iface;
    return S_OK;
}

ULONG ReleaseVisuals(void* object) {
    return ReleaseUnknown(object);
}

DWORD GetDurationMs() {
    int value = Wh_GetIntSetting(L"durationMs");

    if (value < 3000) {
        value = 3000;
    } else if (value > 15000) {
        value = 15000;
    }

    return static_cast<DWORD>(value);
}

void CloseGuard() {
    if (g_instanceGuard) {
        CloseHandle(g_instanceGuard);
        g_instanceGuard = nullptr;
    }
}

bool TryArmNow() {
    if (g_unloading.load(std::memory_order_acquire) ||
        g_armed.load(std::memory_order_acquire)) {
        return g_armed.load(std::memory_order_acquire);
    }

    HMODULE module = GetModuleHandleW(L"LockController.dll");
    if (!module) {
        return false;
    }

    Wh_Log(
        L"[combined/stage1] LockController.dll appeared; "
        L"attempting exact-build arm");

    g_lockController = module;

    if (!ValidateModule(g_lockController)) {
        Wh_Log(
            L"[combined/stage1] unsupported LockController build; "
            L"will not arm");
        return false;
    }

    if (!ResolveSymbols()) {
        Wh_Log(
            L"[combined/stage1] required private symbols unavailable; "
            L"will not arm");
        return false;
    }

    if (!ResolveComApis()) {
        Wh_Log(
            L"[combined/stage1] required COM APIs unavailable; "
            L"will not arm");
        return false;
    }

    g_instanceGuard = CreateEventW(
        nullptr,
        TRUE,
        FALSE,
        L"Local\\SoftLockCombinedLab-v030-26200-6584");

    if (!g_instanceGuard) {
        Wh_Log(
            L"[combined/stage1] CreateEvent(instance guard) failed "
            L"err=%lu",
            GetLastError());
        return false;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        Wh_Log(
            L"[combined/stage1] another qualified combined-lab "
            L"Explorer instance is already armed/running; refusing");
        CloseGuard();
        return false;
    }

    const bool currentRunSetting =
        Wh_GetIntSetting(L"runTest") != 0;

    g_previousRunSetting.store(
        currentRunSetting, std::memory_order_release);

    g_armed.store(true, std::memory_order_release);

    Wh_Log(
        L"[combined/stage1] READY/ARMED. Initial runTest=%u.",
        currentRunSetting ? 1u : 0u);

    if (currentRunSetting) {
        Wh_Log(
            L"[combined/stage1] SAFETY: runTest was already true "
            L"when arming completed. Toggle false, then true to start.");
    } else {
        Wh_Log(
            L"[combined/stage1] To start once: change runTest "
            L"false -> true.");
    }

    return true;
}

DWORD WINAPI ArmThreadProc(void*) {
    Wh_Log(
        L"[combined/stage1] LockController.dll not loaded yet; "
        L"waiting for Explorer lazy-load");

    while (!g_unloading.load(std::memory_order_acquire)) {
        if (TryArmNow()) {
            break;
        }

        DWORD wait = WaitForSingleObject(g_armStopEvent, 500);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
    }

    return 0;
}

DWORD WINAPI WorkerThreadProc(void*) {
    g_running.store(true, std::memory_order_release);

    Wh_Log(
        L"[combined/stage1] ===== STAGE 2 TEST BEGIN =====");

    HRESULT coHr =
        g_CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    const bool coInitialized = SUCCEEDED(coHr);

    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE) {
        Wh_Log(
            L"[combined/stage1] CoInitializeEx failed hr=0x%08X",
            static_cast<unsigned int>(coHr));
        goto Done;
    }

    if (g_unloading.load(std::memory_order_acquire)) {
        goto Done;
    }

    if (!IsDefaultInputDesktop()) {
        Wh_Log(
            L"[combined/stage1] FAIL CLOSED: "
            L"not on Default input desktop");
        goto Done;
    }

    if (IsLogonUiRunning()) {
        Wh_Log(
            L"[combined/stage1] FAIL CLOSED: "
            L"LogonUI already present");
        goto Done;
    }

    if (!g_operatorNew || !g_ctor || !g_init ||
        !g_start || !g_show || !g_terminate ||
        !g_makeLogonUIDataContext) {
        Wh_Log(
            L"[combined/stage1] FAIL CLOSED: "
            L"unresolved function pointer");
        goto Done;
    }

    {
        void* visuals = nullptr;
        void* dataContext = nullptr;
        bool startSucceeded = false;
        unsigned int presentationCookie =
            static_cast<unsigned int>(GetTickCount());
        if (presentationCookie == 0) {
            presentationCookie = 1;
        }

        Wh_Log(
            L"[combined/stage1] allocating VisualsController "
            L"size=0x%llX",
            static_cast<unsigned long long>(
                kVisualsControllerSize));

        visuals = g_operatorNew(kVisualsControllerSize);
        if (!visuals) {
            Wh_Log(
                L"[combined/stage1] operator new returned null");
            goto ObjectDone;
        }

        visuals = g_ctor(visuals);
        if (!visuals) {
            Wh_Log(
                L"[combined/stage1] constructor unexpectedly "
                L"returned null");
            goto ObjectDone;
        }

        Wh_Log(
            L"[combined/stage1] object=%p; "
            L"RuntimeClassInitialize(context=0, interop=null)",
            visuals);

        {
            HRESULT hr = g_init(visuals, 0, nullptr);

            Wh_Log(
                L"[combined/stage1] "
                L"RuntimeClassInitialize -> 0x%08X",
                static_cast<unsigned int>(hr));

            if (FAILED(hr)) {
                goto ObjectDone;
            }
        }

        {
            HRESULT hr = g_start(visuals);

            Wh_Log(
                L"[combined/stage1] Start -> 0x%08X",
                static_cast<unsigned int>(hr));

            if (FAILED(hr)) {
                goto ObjectDone;
            }

            startSucceeded = true;
        }

        if (g_unloading.load(std::memory_order_acquire) ||
            WaitForSingleObject(g_stopEvent, 0) ==
                WAIT_OBJECT_0) {
            Wh_Log(
                L"[combined/stage1] cleanup requested before Show");
            goto ActiveCleanup;
        }

        {
            HRESULT hr =
                CreatePresentationDataContext(
                    presentationCookie,
                    &dataContext);

            if (FAILED(hr) || !dataContext) {
                Wh_Log(
                    L"[combined/stage2] FAIL CLOSED: "
                    L"presentation data context creation failed "
                    L"hr=0x%08X",
                    static_cast<unsigned int>(hr));
                goto ActiveCleanup;
            }
        }

        g_dismissEvent = CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            kDismissEventName);

        if (!g_dismissEvent) {
            Wh_Log(
                L"[combined/stage2] FAIL CLOSED: "
                L"CreateEvent(dismiss) failed err=%lu",
                GetLastError());
            goto ActiveCleanup;
        }

        ResetEvent(g_dismissEvent);

        Wh_Log(
            L"[combined/stage2] calling "
            L"Show(false, nativeDataContext, cookie=%u) "
            L"[presentation credentialsRequired=false]",
            presentationCookie);

        {
            HRESULT hr =
                g_show(
                    visuals,
                    false,
                    dataContext,
                    presentationCookie);

            Wh_Log(
                L"[combined/stage2] Show -> 0x%08X",
                static_cast<unsigned int>(hr));

            if (FAILED(hr)) {
                goto ActiveCleanup;
            }
        }

        {
            const DWORD duration = GetDurationMs();

            Wh_Log(
                L"[combined/stage2] presentation active; "
                L"watchdog=%lu ms; waiting for native "
                L"LockApp dismissal completion",
                duration);

            HANDLE waits[2] = {
                g_stopEvent,
                g_dismissEvent,
            };

            DWORD wait =
                WaitForMultipleObjects(
                    ARRAYSIZE(waits),
                    waits,
                    FALSE,
                    duration);

            if (wait == WAIT_OBJECT_0) {
                Wh_Log(
                    L"[combined/stage2] early cleanup requested");
            } else if (wait == WAIT_OBJECT_0 + 1) {
                Wh_Log(
                    L"[combined/stage2] LockApp native "
                    L"_OnViewChanged dismissal completed; "
                    L"requesting canonical cleanup");
            } else if (wait == WAIT_TIMEOUT) {
                Wh_Log(
                    L"[combined/stage2] watchdog expired; "
                    L"requesting canonical cleanup");
            } else {
                Wh_Log(
                    L"[combined/stage2] wait result=0x%08X; "
                    L"cleaning up",
                    wait);
            }
        }

ActiveCleanup:
        if (startSucceeded) {
            Wh_Log(
                L"[combined/stage1] calling "
                L"VisualsController::Terminate()");

            HRESULT hr = g_terminate(visuals);

            Wh_Log(
                L"[combined/stage1] Terminate -> 0x%08X",
                static_cast<unsigned int>(hr));
        }

ObjectDone:
        if (g_dismissEvent) {
            CloseHandle(g_dismissEvent);
            g_dismissEvent = nullptr;
        }

        if (dataContext) {
            ULONG refs = ReleaseUnknown(dataContext);
            Wh_Log(
                L"[combined/stage2] dataContext Release "
                L"-> remaining=%lu",
                refs);
            dataContext = nullptr;
        }

        if (visuals) {
            Wh_Log(
                L"[combined/stage1] releasing final owner reference");

            ULONG refs = ReleaseVisuals(visuals);

            Wh_Log(
                L"[combined/stage1] Release -> remaining=%lu",
                refs);

            visuals = nullptr;
        }
    }

Done:
    if (coInitialized) {
        g_CoUninitialize();
    }

    Wh_Log(
        L"[combined/stage1] ===== STAGE 2 TEST END =====");

    g_running.store(false, std::memory_order_release);
    return 0;
}

void RequestCleanup() {
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
}

void StartTestOnce() {
    if (!g_armed.load(std::memory_order_acquire)) {
        Wh_Log(
            L"[combined/stage1] run requested but Explorer side "
            L"is not READY/ARMED yet");
        return;
    }

    if (g_unloading.load(std::memory_order_acquire)) {
        return;
    }

    bool expected = false;
    if (!g_everStarted.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        Wh_Log(
            L"[combined/stage1] one-shot test already used in "
            L"this Explorer instance; reload mod to arm again");
        return;
    }

    if (g_running.load(std::memory_order_acquire)) {
        Wh_Log(L"[combined/stage1] test already running");
        return;
    }

    ResetEvent(g_stopEvent);

    g_workerThread = CreateThread(
        nullptr, 0, WorkerThreadProc, nullptr, 0, nullptr);

    if (!g_workerThread) {
        Wh_Log(
            L"[combined/stage1] CreateThread failed err=%lu",
            GetLastError());

        g_everStarted.store(
            false, std::memory_order_release);

        return;
    }

    Wh_Log(L"[combined/stage1] worker started");
}

} // namespace

BOOL Init() {
    Wh_Log(
        L"[combined/stage1] Explorer role initializing v0.3.0 "
        L"(NO ACTIVE TEST ON LOAD)");

    g_stopEvent =
        CreateEventW(nullptr, TRUE, FALSE, nullptr);

    g_armStopEvent =
        CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if (!g_stopEvent || !g_armStopEvent) {
        Wh_Log(
            L"[combined/stage1] event creation failed err=%lu",
            GetLastError());

        if (g_stopEvent) {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
        }

        if (g_armStopEvent) {
            CloseHandle(g_armStopEvent);
            g_armStopEvent = nullptr;
        }

        return FALSE;
    }

    const bool currentRun =
        Wh_GetIntSetting(L"runTest") != 0;

    g_previousRunSetting.store(
        currentRun, std::memory_order_release);

    // If LockController.dll is already present, arm immediately.
    if (GetModuleHandleW(L"LockController.dll")) {
        TryArmNow();
    } else {
        // Otherwise stay resident and wait for the normal Explorer lazy-load.
        g_armThread =
            CreateThread(
                nullptr, 0, ArmThreadProc, nullptr, 0, nullptr);

        if (!g_armThread) {
            Wh_Log(
                L"[combined/stage1] failed to create lazy-arm "
                L"thread err=%lu",
                GetLastError());
            return FALSE;
        }
    }

    return TRUE;
}

void SettingsChanged() {
    const bool now =
        Wh_GetIntSetting(L"runTest") != 0;

    const bool before =
        g_previousRunSetting.exchange(
            now, std::memory_order_acq_rel);

    Wh_Log(
        L"[combined/stage1] settings changed runTest %u -> %u",
        before ? 1u : 0u,
        now ? 1u : 0u);

    // While not armed, only track the current setting. A pre-arm "true"
    // never auto-runs once the DLL later appears.
    if (!g_armed.load(std::memory_order_acquire)) {
        if (now) {
            Wh_Log(
                L"[combined/stage1] not armed yet; runTest=true "
                L"will be ignored until you toggle false -> true "
                L"after READY/ARMED");
        }
        return;
    }

    if (!now) {
        if (g_running.load(std::memory_order_acquire)) {
            Wh_Log(
                L"[combined/stage1] runTest=false requests "
                L"early cleanup");
            RequestCleanup();
        }
        return;
    }

    if (!before && now) {
        StartTestOnce();
    }
}

void Uninit() {
    Wh_Log(
        L"[combined/stage1] Explorer role unloading; "
        L"requesting cleanup");

    g_unloading.store(true, std::memory_order_release);

    if (g_armStopEvent) {
        SetEvent(g_armStopEvent);
    }

    RequestCleanup();

    if (g_armThread) {
        WaitForSingleObject(g_armThread, INFINITE);
        CloseHandle(g_armThread);
        g_armThread = nullptr;
    }

    if (g_workerThread) {
        Wh_Log(
            L"[combined/stage1] waiting for visual worker "
            L"to finish");

        WaitForSingleObject(g_workerThread, INFINITE);
        CloseHandle(g_workerThread);
        g_workerThread = nullptr;
    }

    CloseGuard();

    if (g_dismissEvent) {
        CloseHandle(g_dismissEvent);
        g_dismissEvent = nullptr;
    }

    if (g_armStopEvent) {
        CloseHandle(g_armStopEvent);
        g_armStopEvent = nullptr;
    }

    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }

    Wh_Log(L"[combined/stage1] Explorer role unloaded");
}

} // namespace stage1

// ============================================================================
// LockApp role: exact-build passive wallpaper + dismissal observer.
// ============================================================================

namespace lockobs {
namespace {

constexpr DWORD kExpectedTimestamp   = 0xA92624DD;
constexpr DWORD kExpectedSizeOfImage = 0x00AC9000;
constexpr DWORD kExpectedPdbAge      = 1;

const GUID kExpectedPdbGuid = {
    0xC736BF66, 0x8E51, 0xA140,
    {0xC1, 0x74, 0x9E, 0x5C, 0x73, 0x92, 0xC3, 0x8F}
};

constexpr uintptr_t RVA_UNLOCK_HELPER       = 0x00167FA0;
constexpr uintptr_t RVA_ON_UNLOCKING        = 0x0016D700;
constexpr uintptr_t RVA_SYNC_IMAGE_MAIN     = 0x0016E510;
constexpr uintptr_t RVA_UPDATE_IMAGE        = 0x00170DF4;
constexpr uintptr_t RVA_SYNC_IMAGE          = 0x00198118;
constexpr uintptr_t RVA_ON_VIEW_CHANGED     = 0x001D9170;
constexpr uintptr_t RVA_MOUSE_DISMISS       = 0x002D8000;

// Deeper passive wallpaper-source trace, resolved from the exact matching PDB.
constexpr uintptr_t RVA_CREATE_LOCKSCREEN_INFO = 0x001F4DAC;
constexpr uintptr_t RVA_WALLPAPER_SOURCE_TASK   = 0x002BC62C;
constexpr uintptr_t RVA_WALLPAPER_CONTINUATION  = 0x002C80CC;
constexpr uintptr_t RVA_ON_IMAGE_CHANGED        = 0x001981B0;

HMODULE g_module = nullptr;

std::atomic<unsigned long> g_viewChangedCalls{0};
std::atomic<unsigned long> g_updateImageCalls{0};
std::atomic<unsigned long> g_syncImageCalls{0};
std::atomic<unsigned long> g_unlockHelperCalls{0};
std::atomic<bool> g_finalViewChangedDismissSeen{false};
std::atomic<bool> g_dismissEventSignaled{false};

bool g_logViewChanged = true;

constexpr uintptr_t RVA_REASON_ON_VIEW_CHANGED = 0x009C5220;
constexpr PCWSTR kDismissEventName =
    L"Local\\SoftLockCombinedLab-v030-Dismiss";

ULONGLONG Tick() {
    return GetTickCount64();
}

bool GuidEqual(const GUID& a, const GUID& b) {
    return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}

bool ValidateModuleIdentity(HMODULE module) {
    if (!module) {
        return false;
    }

    const auto* base =
        reinterpret_cast<const BYTE*>(module);

    const auto* dos =
        reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        Wh_Log(
            L"[combined/lockapp] invalid DOS header");
        return false;
    }

    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            base + dos->e_lfanew);

    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic !=
            IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        Wh_Log(
            L"[combined/lockapp] invalid PE header");
        return false;
    }

    const DWORD ts =
        nt->FileHeader.TimeDateStamp;

    const DWORD size =
        nt->OptionalHeader.SizeOfImage;

    Wh_Log(
        L"[combined/lockapp] PE identity "
        L"timestamp=0x%08X size=0x%08X",
        ts, size);

    if (ts != kExpectedTimestamp ||
        size != kExpectedSizeOfImage) {
        Wh_Log(
            L"[combined/lockapp] unsupported LockApp.exe build");
        return false;
    }

    const auto& dir =
        nt->OptionalHeader
            .DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];

    if (!dir.VirtualAddress ||
        dir.Size < sizeof(IMAGE_DEBUG_DIRECTORY)) {
        Wh_Log(
            L"[combined/lockapp] missing PE debug directory");
        return false;
    }

    const auto* entries =
        reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(
            base + dir.VirtualAddress);

    const size_t count =
        dir.Size / sizeof(IMAGE_DEBUG_DIRECTORY);

    for (size_t i = 0; i < count; ++i) {
        const auto& entry = entries[i];

        if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW ||
            !entry.AddressOfRawData ||
            entry.SizeOfData < 24) {
            continue;
        }

        const BYTE* cv =
            base + entry.AddressOfRawData;

        if (std::memcmp(cv, "RSDS", 4) != 0) {
            continue;
        }

        GUID guid{};
        std::memcpy(&guid, cv + 4, sizeof(guid));

        DWORD age = 0;
        std::memcpy(&age, cv + 20, sizeof(age));

        Wh_Log(
            L"[combined/lockapp] RSDS GUID="
            L"%08X-%04X-%04X-%02X%02X-"
            L"%02X%02X%02X%02X%02X%02X age=%lu",
            guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1],
            guid.Data4[2], guid.Data4[3],
            guid.Data4[4], guid.Data4[5],
            guid.Data4[6], guid.Data4[7],
            age);

        if (!GuidEqual(guid, kExpectedPdbGuid) ||
            age != kExpectedPdbAge) {
            Wh_Log(
                L"[combined/lockapp] RSDS identity mismatch");
            return false;
        }

        return true;
    }

    Wh_Log(
        L"[combined/lockapp] RSDS record not found");

    return false;
}

using SyncImage_t =
    void (__fastcall*)(void* self, int options);

using UpdateImage_t =
    void (__fastcall*)(
        void* self,
        void* randomAccessStream,
        bool flag);

using OnViewChanged_t =
    void (__fastcall*)(
        void* self,
        void* sender,
        void* args);

using MouseDismiss_t =
    void (__fastcall*)(void* self);

using UnlockHelper_t =
    void (__fastcall*)(
        void* self,
        const char* reason,
        bool flag);

using OnUnlocking_t =
    void (__fastcall*)(
        void* self,
        void* lockApplicationHost,
        void* eventArgs);

using CreateLockScreenInfo_t =
    void* (__fastcall*)();

using WallpaperSourceTask_t =
    void* (__fastcall*)(void* closure);

using WallpaperContinuation_t =
    void (__fastcall*)(void* closure, void* stream);

using OnLockScreenImageChanged_t =
    void (__fastcall*)(
        void* self,
        void* lockScreenInfo,
        void* args);

SyncImage_t      g_syncImageOriginal = nullptr;
SyncImage_t      g_syncImageMainOriginal = nullptr;
UpdateImage_t    g_updateImageOriginal = nullptr;
OnViewChanged_t  g_onViewChangedOriginal = nullptr;
MouseDismiss_t   g_mouseDismissOriginal = nullptr;
UnlockHelper_t   g_unlockHelperOriginal = nullptr;
OnUnlocking_t    g_onUnlockingOriginal = nullptr;
CreateLockScreenInfo_t g_createLockScreenInfoOriginal = nullptr;
WallpaperSourceTask_t g_wallpaperSourceTaskOriginal = nullptr;
WallpaperContinuation_t g_wallpaperContinuationOriginal = nullptr;
OnLockScreenImageChanged_t g_onImageChangedOriginal = nullptr;

void __fastcall SyncImage_Hook(
    void* self,
    int options)
{
    const auto n =
        g_syncImageCalls.fetch_add(
            1, std::memory_order_relaxed) + 1;

    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"MainPage::_SyncLockScreenImage #%lu "
        L"this=%p options=%d (0x%08X)",
        Tick(), GetCurrentThreadId(), n,
        self,
        options,
        static_cast<unsigned int>(options));

    g_syncImageOriginal(self, options);
}

void __fastcall SyncImageMain_Hook(
    void* self,
    int options)
{
    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"ENTER MainPage::_SyncLockScreenImage_MainThread "
        L"this=%p options=%d (0x%08X)",
        Tick(), GetCurrentThreadId(),
        self,
        options,
        static_cast<unsigned int>(options));

    g_syncImageMainOriginal(self, options);

    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"LEAVE MainPage::_SyncLockScreenImage_MainThread",
        Tick(), GetCurrentThreadId());
}

void __fastcall UpdateImage_Hook(
    void* self,
    void* randomAccessStream,
    bool flag)
{
    const auto n =
        g_updateImageCalls.fetch_add(
            1, std::memory_order_relaxed) + 1;

    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"ENTER MainPage::_UpdateLockScreenImage #%lu "
        L"this=%p stream=%p streamIsNull=%u flag=%u",
        Tick(), GetCurrentThreadId(), n,
        self,
        randomAccessStream,
        randomAccessStream ? 0u : 1u,
        flag ? 1u : 0u);

    g_updateImageOriginal(
        self, randomAccessStream, flag);

    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"LEAVE MainPage::_UpdateLockScreenImage #%lu",
        Tick(), GetCurrentThreadId(), n);
}

void __fastcall OnViewChanged_Hook(
    void* self,
    void* sender,
    void* args)
{
    const auto n =
        g_viewChangedCalls.fetch_add(
            1, std::memory_order_relaxed) + 1;

    if (g_logViewChanged && n <= 64) {
        Wh_Log(
            L"[combined/lockapp] t=%llu tid=%lu "
            L"MainPage::_OnViewChanged #%lu "
            L"this=%p sender=%p args=%p",
            Tick(), GetCurrentThreadId(), n,
            self, sender, args);
    } else if (g_logViewChanged && n == 65) {
        Wh_Log(
            L"[combined/lockapp] MainPage::_OnViewChanged "
            L"logging capped at 64 calls");
    }

    g_onViewChangedOriginal(self, sender, args);

    // _UnlockHelper("_OnViewChanged", true) is called from the final
    // completion pass of this exact-build native ScrollViewer dismissal.
    // Signal only after the outer _OnViewChanged original has returned, so
    // Explorer tears down the presentation after LockApp has finished the
    // native animation callback.
    if (g_finalViewChangedDismissSeen.exchange(
            false, std::memory_order_acq_rel) &&
        !g_dismissEventSignaled.exchange(
            true, std::memory_order_acq_rel)) {
        HANDLE eventHandle =
            OpenEventW(
                EVENT_MODIFY_STATE,
                FALSE,
                kDismissEventName);

        if (eventHandle) {
            SetEvent(eventHandle);
            CloseHandle(eventHandle);

            Wh_Log(
                L"[combined/lockapp] signaled Stage 2 "
                L"dismiss-complete event after _OnViewChanged");
        } else {
            Wh_Log(
                L"[combined/lockapp] final native dismissal seen, "
                L"but Stage 2 dismiss event is not present");
        }
    }
}

void __fastcall MouseDismiss_Hook(void* self) {
    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"ENTER MainPage::_MouseDismiss this=%p",
        Tick(), GetCurrentThreadId(), self);

    g_mouseDismissOriginal(self);

    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"LEAVE MainPage::_MouseDismiss",
        Tick(), GetCurrentThreadId());
}

void __fastcall UnlockHelper_Hook(
    void* self,
    const char* reason,
    bool flag)
{
    const auto n =
        g_unlockHelperCalls.fetch_add(
            1, std::memory_order_relaxed) + 1;

    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"ENTER MainPage::_UnlockHelper #%lu "
        L"this=%p reasonPtr=%p flag=%u",
        Tick(), GetCurrentThreadId(), n,
        self, reason, flag ? 1u : 0u);

    g_unlockHelperOriginal(self, reason, flag);

    const char* exactOnViewChangedReason =
        reinterpret_cast<const char*>(
            reinterpret_cast<BYTE*>(g_module) +
            RVA_REASON_ON_VIEW_CHANGED);

    if (flag && reason == exactOnViewChangedReason) {
        g_finalViewChangedDismissSeen.store(
            true, std::memory_order_release);

        Wh_Log(
            L"[combined/lockapp] exact final "
            L"_UnlockHelper(\"_OnViewChanged\", true) observed");
    }

    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"LEAVE MainPage::_UnlockHelper #%lu",
        Tick(), GetCurrentThreadId(), n);
}

void __fastcall OnUnlocking_Hook(
    void* self,
    void* lockApplicationHost,
    void* eventArgs)
{
    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"ENTER MainPage::OnUnlocking "
        L"this=%p host=%p eventArgs=%p",
        Tick(), GetCurrentThreadId(),
        self, lockApplicationHost, eventArgs);

    g_onUnlockingOriginal(
        self, lockApplicationHost, eventArgs);

    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"LEAVE MainPage::OnUnlocking",
        Tick(), GetCurrentThreadId());
}


void* __fastcall CreateLockScreenInfo_Hook() {
    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"ENTER LockAppBroker::CreateLockScreenInfo",
        Tick(), GetCurrentThreadId());

    void* result = g_createLockScreenInfoOriginal();

    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"LEAVE LockAppBroker::CreateLockScreenInfo result=%p null=%u",
        Tick(), GetCurrentThreadId(),
        result, result ? 0u : 1u);

    return result;
}

void* __fastcall WallpaperSourceTask_Hook(void* closure) {
    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"ENTER wallpaper-source task lambda_93ba "
        L"closure=%p",
        Tick(), GetCurrentThreadId(), closure);

    void* result = g_wallpaperSourceTaskOriginal(closure);

    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"LEAVE wallpaper-source task lambda_93ba "
        L"stream=%p streamIsNull=%u",
        Tick(), GetCurrentThreadId(),
        result, result ? 0u : 1u);

    return result;
}

void __fastcall WallpaperContinuation_Hook(
    void* closure,
    void* stream)
{
    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"ENTER wallpaper continuation lambda_c939 "
        L"closure=%p stream=%p streamIsNull=%u",
        Tick(), GetCurrentThreadId(),
        closure, stream, stream ? 0u : 1u);

    g_wallpaperContinuationOriginal(closure, stream);

    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"LEAVE wallpaper continuation lambda_c939",
        Tick(), GetCurrentThreadId());
}

void __fastcall OnLockScreenImageChanged_Hook(
    void* self,
    void* lockScreenInfo,
    void* args)
{
    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"ENTER MainPage::_OnLockScreenImageChanged "
        L"this=%p info=%p args=%p",
        Tick(), GetCurrentThreadId(),
        self, lockScreenInfo, args);

    g_onImageChangedOriginal(self, lockScreenInfo, args);

    Wh_Log(
        L"[combined/lockapp] t=%llu tid=%lu "
        L"LEAVE MainPage::_OnLockScreenImageChanged",
        Tick(), GetCurrentThreadId());
}

bool QueueHook(
    uintptr_t rva,
    void* hook,
    void** original,
    PCWSTR name)
{
    void* target =
        reinterpret_cast<void*>(
            reinterpret_cast<BYTE*>(g_module) + rva);

    if (!Wh_SetFunctionHook(target, hook, original)) {
        Wh_Log(
            L"[combined/lockapp] FAILED to queue hook %s "
            L"rva=0x%08llX target=%p",
            name,
            static_cast<unsigned long long>(rva),
            target);

        return false;
    }

    Wh_Log(
        L"[combined/lockapp] queued %s "
        L"rva=0x%08llX target=%p",
        name,
        static_cast<unsigned long long>(rva),
        target);

    return true;
}

} // namespace

BOOL Init() {
    Wh_Log(
        L"[combined/lockapp] LockApp role initializing "
        L"passive observer v0.3.0");

    g_logViewChanged =
        Wh_GetIntSetting(L"logViewChanged") != 0;

    g_module = GetModuleHandleW(nullptr);

    if (!ValidateModuleIdentity(g_module)) {
        Wh_Log(
            L"[combined/lockapp] FAIL CLOSED: "
            L"LockApp identity validation failed");
        return FALSE;
    }

    bool ok = true;

    ok &= QueueHook(
        RVA_SYNC_IMAGE,
        reinterpret_cast<void*>(&SyncImage_Hook),
        reinterpret_cast<void**>(&g_syncImageOriginal),
        L"MainPage::_SyncLockScreenImage");

    ok &= QueueHook(
        RVA_SYNC_IMAGE_MAIN,
        reinterpret_cast<void*>(&SyncImageMain_Hook),
        reinterpret_cast<void**>(&g_syncImageMainOriginal),
        L"MainPage::_SyncLockScreenImage_MainThread");

    ok &= QueueHook(
        RVA_UPDATE_IMAGE,
        reinterpret_cast<void*>(&UpdateImage_Hook),
        reinterpret_cast<void**>(&g_updateImageOriginal),
        L"MainPage::_UpdateLockScreenImage");

    ok &= QueueHook(
        RVA_ON_VIEW_CHANGED,
        reinterpret_cast<void*>(&OnViewChanged_Hook),
        reinterpret_cast<void**>(&g_onViewChangedOriginal),
        L"MainPage::_OnViewChanged");

    ok &= QueueHook(
        RVA_MOUSE_DISMISS,
        reinterpret_cast<void*>(&MouseDismiss_Hook),
        reinterpret_cast<void**>(&g_mouseDismissOriginal),
        L"MainPage::_MouseDismiss");

    ok &= QueueHook(
        RVA_UNLOCK_HELPER,
        reinterpret_cast<void*>(&UnlockHelper_Hook),
        reinterpret_cast<void**>(&g_unlockHelperOriginal),
        L"MainPage::_UnlockHelper");

    ok &= QueueHook(
        RVA_ON_UNLOCKING,
        reinterpret_cast<void*>(&OnUnlocking_Hook),
        reinterpret_cast<void**>(&g_onUnlockingOriginal),
        L"MainPage::OnUnlocking");


    ok &= QueueHook(
        RVA_CREATE_LOCKSCREEN_INFO,
        reinterpret_cast<void*>(&CreateLockScreenInfo_Hook),
        reinterpret_cast<void**>(&g_createLockScreenInfoOriginal),
        L"LockAppBroker::CreateLockScreenInfo");

    ok &= QueueHook(
        RVA_WALLPAPER_SOURCE_TASK,
        reinterpret_cast<void*>(&WallpaperSourceTask_Hook),
        reinterpret_cast<void**>(&g_wallpaperSourceTaskOriginal),
        L"wallpaper-source task lambda_93ba");

    ok &= QueueHook(
        RVA_WALLPAPER_CONTINUATION,
        reinterpret_cast<void*>(&WallpaperContinuation_Hook),
        reinterpret_cast<void**>(&g_wallpaperContinuationOriginal),
        L"wallpaper continuation lambda_c939");

    ok &= QueueHook(
        RVA_ON_IMAGE_CHANGED,
        reinterpret_cast<void*>(&OnLockScreenImageChanged_Hook),
        reinterpret_cast<void**>(&g_onImageChangedOriginal),
        L"MainPage::_OnLockScreenImageChanged");

    if (!ok) {
        Wh_Log(
            L"[combined/lockapp] FAIL CLOSED: "
            L"one or more hooks could not be queued");
        return FALSE;
    }

    // Hooks queued during Wh_ModInit must NOT be explicitly applied here.
    // Windhawk applies them automatically after Wh_ModInit returns and before
    // Wh_ModAfterInit. Calling Wh_ApplyHookOperations from Wh_ModInit caused
    // v0.2.0's LockApp role to fail even though all seven hooks queued.
    Wh_Log(
        L"[combined/lockapp] 11/11 passive hooks queued; "
        L"waiting for Windhawk post-init apply");

    return TRUE;
}

void AfterInit() {
    Wh_Log(
        L"[combined/lockapp] READY: Windhawk post-init reached; "
        L"11/11 passive hooks should now be active; "
        L"logViewChanged=%u",
        g_logViewChanged ? 1u : 0u);
}

void SettingsChanged() {
    g_logViewChanged =
        Wh_GetIntSetting(L"logViewChanged") != 0;

    Wh_Log(
        L"[combined/lockapp] settings changed "
        L"logViewChanged=%u",
        g_logViewChanged ? 1u : 0u);
}

void Uninit() {
    Wh_Log(
        L"[combined/lockapp] passive observer unloaded");
}

} // namespace lockobs

// ============================================================================
// Windhawk entry points.
// ============================================================================

BOOL Wh_ModInit() {
    const PCWSTR process = CurrentProcessBaseName();

    if (_wcsicmp(process, L"explorer.exe") == 0) {
        g_processRole = ProcessRole::Explorer;
        return stage1::Init();
    }

    if (_wcsicmp(process, L"LockApp.exe") == 0) {
        g_processRole = ProcessRole::LockApp;
        return lockobs::Init();
    }

    Wh_Log(
        L"[combined] unexpected process %s; refusing",
        process);

    g_processRole = ProcessRole::Unknown;
    return FALSE;
}

void Wh_ModAfterInit() {
    if (g_processRole == ProcessRole::LockApp) {
        lockobs::AfterInit();
    }
}

void Wh_ModSettingsChanged() {
    switch (g_processRole) {
        case ProcessRole::Explorer:
            stage1::SettingsChanged();
            break;

        case ProcessRole::LockApp:
            lockobs::SettingsChanged();
            break;

        default:
            break;
    }
}

void Wh_ModUninit() {
    switch (g_processRole) {
        case ProcessRole::Explorer:
            stage1::Uninit();
            break;

        case ProcessRole::LockApp:
            lockobs::Uninit();
            break;

        default:
            break;
    }
}
