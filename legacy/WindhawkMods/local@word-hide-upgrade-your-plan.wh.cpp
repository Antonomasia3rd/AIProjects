// ==WindhawkMod==
// @id              word-hide-upgrade-your-plan
// @name            Word - Remove Upgrade button
// @description     Removes Word's Upgrade button and reclaims its title-bar space.
// @version         1.1.0
// @author          ChatGPT
// @include         WINWORD.EXE
// @compilerOptions -lole32 -loleaut32 -luuid
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
Removes Word's title-bar Upgrade button and reclaims its layout width.

Targeting is language-independent.

It does NOT depend on the localized text "Upgrade your plan".

Instead it identifies this direct-child sequence in the NetUI ribbon:

    NetUIStickyButton
      AutomationId = SearchToggleButton

    NetUIRibbonButton
      AutomationId = ""

    NetUIAnchor
      AutomationId = MeControlWidget

The anonymous middle RibbonButton is collapsed with:

    NetUI::Element::SetWidthPixels(0, 1)
    NetUI::Element::SetIsVisible(false)

Safety measures:

1. Exact mso.dll PE fingerprint.
2. Exact machine-code prefixes for all four private functions.
3. Structural UIA identification using nonlocalized AutomationIds.
4. Three-stage provider handshake.
5. Verify/Mutate must use:
      - the exact same NetUI::Element*
      - the exact same provider callback thread
6. Ambiguous structures fail closed.
7. Scanner supports new Word windows and UI/ribbon rebuilds.
8. No symbols/PDBs are needed at runtime.

Tested mso.dll:

    TimeDateStamp = 0x6A9F10B1
    SizeOfImage   = 0x02595000

This build is intentionally NOT portable to arbitrary Office builds.
*/
// ==/WindhawkModReadme==


#include <windows.h>
#include <uiautomation.h>
#include <wchar.h>


// ============================================================================
// Exact tested mso.dll PE fingerprint
// ============================================================================

static constexpr DWORD kExpectedTimeDateStamp =
    0x6A9F10B1;

static constexpr DWORD kExpectedSizeOfImage =
    0x02595000;


// ============================================================================
// Private mso.dll RVAs
// ============================================================================

// NetUI::Element::SetWidthPixels(int, int)
static constexpr uintptr_t kRvaSetWidthPixels =
    0x000EFCAC;

// NetUI::Element::SetIsVisible(bool)
static constexpr uintptr_t kRvaSetIsVisible =
    0x000A823C;

// NetUI::WinUia::ElementProvider::GetElement()
static constexpr uintptr_t kRvaProviderGetElement =
    0x00EFBF40;

// NetUI::WinUia::ElementProvider::GetName(VARIANT*)
static constexpr uintptr_t kRvaProviderGetName =
    0x00EFBFA0;


// ============================================================================
// Exact code fingerprints
//
// Captured from an unmodified instance of:
//
//     mso.dll
//     TimeDateStamp = 0x6A9F10B1
//     SizeOfImage   = 0x02595000
//
// These are deliberately checked BEFORE installing any hooks.
// ============================================================================

static constexpr SIZE_T kSignatureLength =
    24;


static constexpr BYTE kSigSetWidthPixels[
    kSignatureLength
] = {

    0x48, 0x89, 0x5C, 0x24,
    0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x8B, 0xC2,
    0x48, 0x8B, 0xD9, 0x8B,
    0xC8, 0x41, 0x8B, 0xD0,
    0xFF, 0x15, 0xCA, 0x5F
};


static constexpr BYTE kSigSetIsVisible[
    kSignatureLength
] = {

    0x48, 0x89, 0x5C, 0x24,
    0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x8A, 0xDA,
    0x48, 0x8B, 0xF9, 0xFF,
    0x15, 0x37, 0xC5, 0x8A,
    0x01, 0x44, 0x8A, 0xC3
};


static constexpr BYTE kSigProviderGetElement[
    kSignatureLength
] = {

    0xFF, 0x25, 0x6A, 0x90,
    0xA5, 0x00, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90,
    0xFF, 0x25, 0x52, 0x90,
    0xA5, 0x00, 0x90, 0x90
};


static constexpr BYTE kSigProviderGetName[
    kSignatureLength
] = {

    0xFF, 0x25, 0xDA, 0x8F,
    0xA5, 0x00, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90,
    0xFF, 0x25, 0xC2, 0x8F,
    0xA5, 0x00, 0x90, 0x90
};


static_assert(
    sizeof(kSigSetWidthPixels) ==
        kSignatureLength);

static_assert(
    sizeof(kSigSetIsVisible) ==
        kSignatureLength);

static_assert(
    sizeof(kSigProviderGetElement) ==
        kSignatureLength);

static_assert(
    sizeof(kSigProviderGetName) ==
        kSignatureLength);


// ============================================================================
// Nonlocalized UIA identities
// ============================================================================

static constexpr wchar_t kRibbonClass[] =
    L"NetUInetpane";

static constexpr wchar_t kSearchClass[] =
    L"NetUIStickyButton";

static constexpr wchar_t kSearchAutomationId[] =
    L"SearchToggleButton";

static constexpr wchar_t kTargetClass[] =
    L"NetUIRibbonButton";

static constexpr wchar_t kAccountClass[] =
    L"NetUIAnchor";

static constexpr wchar_t kAccountAutomationId[] =
    L"MeControlWidget";


// ============================================================================
// Private function types
// ============================================================================

using SetWidthPixels_t =
    long (__fastcall*)(
        void* element,
        int width,
        int arg2);


using SetIsVisible_t =
    long (__fastcall*)(
        void* element,
        bool visible);


using ProviderGetElement_t =
    void* (__fastcall*)(
        void* provider);


using ProviderGetName_t =
    long (__fastcall*)(
        void* provider,
        VARIANT* value);


// ============================================================================
// Resolved private functions
// ============================================================================

static SetWidthPixels_t
    g_setWidthPixels = nullptr;

static SetIsVisible_t
    g_setIsVisible = nullptr;

static ProviderGetElement_t
    g_providerGetElement = nullptr;

static ProviderGetName_t
    g_origProviderGetName = nullptr;


// ============================================================================
// Thread / lifetime state
// ============================================================================

static HANDLE g_stopEvent =
    nullptr;

static HANDLE g_loaderThread =
    nullptr;

static HANDLE g_scanThread =
    nullptr;


// ============================================================================
// Logging limiters
// ============================================================================

static volatile LONG g_successCount =
    0;

static volatile LONG g_ambiguousCount =
    0;

static volatile LONG g_handshakeFailureCount =
    0;


// ============================================================================
// Provider handshake state
// ============================================================================

enum class ArmMode {
    Idle = 0,
    Capture,
    Verify,
    Mutate
};


struct ArmState {

    ArmMode mode;

    void* expectedElement;

    DWORD expectedThreadId;

    void* capturedElement;

    DWORD capturedThreadId;

    LONG hits;

    bool mutationAttempted;

    bool mutationSucceeded;

    HWND wordWindow;
};


static SRWLOCK g_armLock =
    SRWLOCK_INIT;


static ArmState g_armState = {

    ArmMode::Idle,

    nullptr,

    0,

    nullptr,

    0,

    0,

    false,

    false,

    nullptr
};


// ============================================================================
// String helpers
// ============================================================================

static bool StringEquals(
    BSTR value,
    const wchar_t* wanted) {

    return
        value &&
        wanted &&
        wcscmp(
            value,
            wanted) == 0;
}


static bool StringEmpty(
    BSTR value) {

    return
        !value ||
        value[0] ==
            L'\0';
}


// ============================================================================
// Memory helpers
// ============================================================================

static bool IsExecutableAddress(
    const void* address) {

    if (!address)
        return false;


    MEMORY_BASIC_INFORMATION mbi = {};


    if (!VirtualQuery(
            address,
            &mbi,
            sizeof(mbi))) {

        return false;
    }


    if (mbi.State !=
        MEM_COMMIT) {

        return false;
    }


    if (mbi.Protect &
        PAGE_GUARD) {

        return false;
    }


    DWORD protection =
        mbi.Protect &
        0xFF;


    return
        protection ==
            PAGE_EXECUTE ||
        protection ==
            PAGE_EXECUTE_READ ||
        protection ==
            PAGE_EXECUTE_READWRITE ||
        protection ==
            PAGE_EXECUTE_WRITECOPY;
}


static bool IsReadableMemory(
    const void* address,
    SIZE_T size) {

    if (!address ||
        size == 0) {

        return false;
    }


    const BYTE* current =
        reinterpret_cast<const BYTE*>(
            address);


    SIZE_T remaining =
        size;


    while (remaining > 0) {


        MEMORY_BASIC_INFORMATION mbi = {};


        if (!VirtualQuery(
                current,
                &mbi,
                sizeof(mbi))) {

            return false;
        }


        if (mbi.State !=
            MEM_COMMIT) {

            return false;
        }


        if (mbi.Protect &
            PAGE_GUARD) {

            return false;
        }


        DWORD protection =
            mbi.Protect &
            0xFF;


        switch (protection) {

            case PAGE_NOACCESS:
            case PAGE_EXECUTE:

                return false;

            default:

                break;
        }


        const BYTE* end =
            reinterpret_cast<const BYTE*>(
                mbi.BaseAddress) +
            mbi.RegionSize;


        if (end <=
            current) {

            return false;
        }


        SIZE_T available =
            static_cast<SIZE_T>(
                end -
                current);


        SIZE_T step =
            remaining <
                    available
                ? remaining
                : available;


        current +=
            step;

        remaining -=
            step;
    }


    return true;
}


// ============================================================================
// Exact machine-code validation
// ============================================================================

static bool ValidateCodeSignature(
    uintptr_t base,
    const wchar_t* name,
    uintptr_t rva,
    const BYTE* expected,
    SIZE_T length) {

    const BYTE* actual =
        reinterpret_cast<const BYTE*>(
            base +
            rva);


    if (!IsReadableMemory(
            actual,
            length)) {

        Wh_Log(
            L"Signature check failed for %s: "
            L"memory unreadable",
            name);

        return false;
    }


    for (SIZE_T i = 0;
         i < length;
         i++) {


        if (actual[i] !=
            expected[i]) {


            Wh_Log(
                L"Signature mismatch: %s "
                L"RVA=0x%llX offset=%llu "
                L"expected=%02X actual=%02X",
                name,
                static_cast<unsigned long long>(
                    rva),
                static_cast<unsigned long long>(
                    i),
                static_cast<unsigned int>(
                    expected[i]),
                static_cast<unsigned int>(
                    actual[i]));


            return false;
        }
    }


    Wh_Log(
        L"Signature OK: %s",
        name);


    return true;
}


// ============================================================================
// PE fingerprint validation
// ============================================================================

static bool ValidateMso(
    HMODULE mso) {

    if (!mso)
        return false;


    uintptr_t base =
        reinterpret_cast<uintptr_t>(
            mso);


    auto* dos =
        reinterpret_cast<IMAGE_DOS_HEADER*>(
            base);


    if (dos->e_magic !=
        IMAGE_DOS_SIGNATURE) {

        Wh_Log(
            L"Invalid mso.dll DOS header");

        return false;
    }


    auto* nt =
        reinterpret_cast<IMAGE_NT_HEADERS64*>(
            base +
            dos->e_lfanew);


    if (nt->Signature !=
        IMAGE_NT_SIGNATURE) {

        Wh_Log(
            L"Invalid mso.dll PE header");

        return false;
    }


    DWORD timestamp =
        nt->FileHeader.TimeDateStamp;

    DWORD imageSize =
        nt->OptionalHeader.SizeOfImage;


    Wh_Log(
        L"mso.dll fingerprint: "
        L"timestamp=0x%08X size=0x%08X",
        timestamp,
        imageSize);


    if (timestamp !=
            kExpectedTimeDateStamp ||
        imageSize !=
            kExpectedSizeOfImage) {


        Wh_Log(
            L"Unsupported mso.dll; "
            L"private hooks disabled");


        return false;
    }


    return true;
}


// ============================================================================
// Full private-RVA validation
// ============================================================================

static bool ValidatePrivateFunctions(
    HMODULE mso) {

    uintptr_t base =
        reinterpret_cast<uintptr_t>(
            mso);


    const void* setWidthPixels =
        reinterpret_cast<const void*>(
            base +
            kRvaSetWidthPixels);

    const void* setIsVisible =
        reinterpret_cast<const void*>(
            base +
            kRvaSetIsVisible);

    const void* providerGetElement =
        reinterpret_cast<const void*>(
            base +
            kRvaProviderGetElement);

    const void* providerGetName =
        reinterpret_cast<const void*>(
            base +
            kRvaProviderGetName);


    if (!IsExecutableAddress(
            setWidthPixels) ||
        !IsExecutableAddress(
            setIsVisible) ||
        !IsExecutableAddress(
            providerGetElement) ||
        !IsExecutableAddress(
            providerGetName)) {


        Wh_Log(
            L"Private RVA executable-address "
            L"validation failed");


        return false;
    }


    if (!ValidateCodeSignature(
            base,
            L"SetWidthPixels",
            kRvaSetWidthPixels,
            kSigSetWidthPixels,
            kSignatureLength)) {

        return false;
    }


    if (!ValidateCodeSignature(
            base,
            L"SetIsVisible",
            kRvaSetIsVisible,
            kSigSetIsVisible,
            kSignatureLength)) {

        return false;
    }


    if (!ValidateCodeSignature(
            base,
            L"ElementProvider::GetElement",
            kRvaProviderGetElement,
            kSigProviderGetElement,
            kSignatureLength)) {

        return false;
    }


    if (!ValidateCodeSignature(
            base,
            L"ElementProvider::GetName",
            kRvaProviderGetName,
            kSigProviderGetName,
            kSignatureLength)) {

        return false;
    }


    Wh_Log(
        L"All private code signatures verified");


    return true;
}


// ============================================================================
// Relayout
// ============================================================================

static void RequestWordRelayout(
    HWND hwnd) {

    if (!hwnd ||
        !IsWindow(hwnd)) {

        return;
    }


    RECT rc = {};


    if (!GetClientRect(
            hwnd,
            &rc)) {

        return;
    }


    int width =
        rc.right -
        rc.left;

    int height =
        rc.bottom -
        rc.top;


    WPARAM sizeType =
        SIZE_RESTORED;


    if (IsZoomed(
            hwnd)) {

        sizeType =
            SIZE_MAXIMIZED;
    }
    else if (IsIconic(
                 hwnd)) {

        sizeType =
            SIZE_MINIMIZED;
    }


    PostMessageW(
        hwnd,
        WM_SIZE,
        sizeType,
        MAKELPARAM(
            width,
            height));


    RedrawWindow(
        hwnd,
        nullptr,
        nullptr,
        RDW_INVALIDATE |
        RDW_ALLCHILDREN);
}


// ============================================================================
// Handshake logging
// ============================================================================

static void LogHandshakeFailure(
    const wchar_t* stage,
    const ArmState& state) {

    LONG number =
        InterlockedIncrement(
            &g_handshakeFailureCount);


    if (number >
        10) {

        return;
    }


    Wh_Log(
        L"Provider handshake failed at %s: "
        L"hits=%ld element=%p thread=%lu",
        stage,
        state.hits,
        state.capturedElement,
        static_cast<unsigned long>(
            state.capturedThreadId));
}


// ============================================================================
// Provider hook
// ============================================================================

static long __fastcall
ProviderGetName_Hook(
    void* provider,
    VARIANT* value) {

    long result =
        g_origProviderGetName(
            provider,
            value);


    // The returned/localized Name is intentionally ignored.
    //
    // Nothing here depends on:
    //
    //     "Upgrade your plan"
    //
    // or any translation of it.


    ArmMode mode =
        ArmMode::Idle;

    void* expectedElement =
        nullptr;

    DWORD expectedThreadId =
        0;

    HWND wordWindow =
        nullptr;


    AcquireSRWLockShared(
        &g_armLock);


    mode =
        g_armState.mode;

    expectedElement =
        g_armState.expectedElement;

    expectedThreadId =
        g_armState.expectedThreadId;

    wordWindow =
        g_armState.wordWindow;


    ReleaseSRWLockShared(
        &g_armLock);


    if (mode ==
        ArmMode::Idle) {

        return result;
    }


    void* element =
        g_providerGetElement(
            provider);


    if (!element)
        return result;


    DWORD currentThreadId =
        GetCurrentThreadId();


    // -----------------------------------------------------------------------
    // Verify and Mutate are bound to BOTH:
    //
    //   - the NetUI element captured in stage 1
    //   - the provider callback thread captured in stage 1
    //
    // An unrelated UIA GetName callback therefore can't satisfy either
    // subsequent stage merely by happening while the scanner is armed.
    // -----------------------------------------------------------------------

    if ((mode ==
             ArmMode::Verify ||
         mode ==
             ArmMode::Mutate) &&
        (
            element !=
                expectedElement ||
            currentThreadId !=
                expectedThreadId
        )) {

        return result;
    }


    bool performMutation =
        false;


    AcquireSRWLockExclusive(
        &g_armLock);


    // Re-check after obtaining exclusive ownership.
    if (g_armState.mode ==
        mode) {


        switch (mode) {


            // ---------------------------------------------------------------
            // CAPTURE
            // ---------------------------------------------------------------

            case ArmMode::Capture:


                g_armState.hits++;


                if (g_armState.hits ==
                    1) {


                    g_armState.capturedElement =
                        element;

                    g_armState.capturedThreadId =
                        currentThreadId;
                }


                break;


            // ---------------------------------------------------------------
            // VERIFY
            // ---------------------------------------------------------------

            case ArmMode::Verify:


                if (element ==
                        g_armState.expectedElement &&
                    currentThreadId ==
                        g_armState.expectedThreadId) {


                    g_armState.hits++;


                    if (g_armState.hits ==
                        1) {


                        g_armState.capturedElement =
                            element;

                        g_armState.capturedThreadId =
                            currentThreadId;
                    }
                }


                break;


            // ---------------------------------------------------------------
            // MUTATE
            // ---------------------------------------------------------------

            case ArmMode::Mutate:


                if (element ==
                        g_armState.expectedElement &&
                    currentThreadId ==
                        g_armState.expectedThreadId &&
                    !g_armState.mutationAttempted) {


                    g_armState.hits++;

                    g_armState.mutationAttempted =
                        true;

                    performMutation =
                        true;
                }


                break;


            default:

                break;
        }
    }


    ReleaseSRWLockExclusive(
        &g_armLock);


    // -----------------------------------------------------------------------
    // NetUI mutation MUST remain synchronous in this provider callback.
    //
    // Testing previously established that arbitrary worker-thread mutation
    // can fail with 0x800403EB.
    // -----------------------------------------------------------------------

    if (performMutation) {


        long widthResult =
            g_setWidthPixels(
                element,
                0,
                1);


        long visibleResult =
            g_setIsVisible(
                element,
                false);


        bool succeeded =
            widthResult == 0 &&
            visibleResult == 0;


        AcquireSRWLockExclusive(
            &g_armLock);


        if (g_armState.mode ==
                ArmMode::Mutate &&
            g_armState.expectedElement ==
                element &&
            g_armState.expectedThreadId ==
                currentThreadId) {


            g_armState.mutationSucceeded =
                succeeded;
        }


        ReleaseSRWLockExclusive(
            &g_armLock);


        if (succeeded) {


            LONG count =
                InterlockedIncrement(
                    &g_successCount);


            if (count <=
                20) {


                Wh_Log(
                    L"Collapsed structural target: "
                    L"element=%p thread=%lu",
                    element,
                    static_cast<unsigned long>(
                        currentThreadId));
            }


            RequestWordRelayout(
                wordWindow);
        }
        else {


            Wh_Log(
                L"Target mutation failed: "
                L"width=0x%08X "
                L"visible=0x%08X",
                static_cast<unsigned int>(
                    widthResult),
                static_cast<unsigned int>(
                    visibleResult));
        }
    }


    return result;
}


// ============================================================================
// Arm helpers
// ============================================================================

static void BeginArm(
    ArmMode mode,
    void* expectedElement,
    DWORD expectedThreadId,
    HWND wordWindow) {

    AcquireSRWLockExclusive(
        &g_armLock);


    g_armState.mode =
        mode;

    g_armState.expectedElement =
        expectedElement;

    g_armState.expectedThreadId =
        expectedThreadId;

    g_armState.capturedElement =
        nullptr;

    g_armState.capturedThreadId =
        0;

    g_armState.hits =
        0;

    g_armState.mutationAttempted =
        false;

    g_armState.mutationSucceeded =
        false;

    g_armState.wordWindow =
        wordWindow;


    ReleaseSRWLockExclusive(
        &g_armLock);
}


static ArmState EndArm() {

    ArmState result = {};


    AcquireSRWLockExclusive(
        &g_armLock);


    result =
        g_armState;


    g_armState.mode =
        ArmMode::Idle;

    g_armState.expectedElement =
        nullptr;

    g_armState.expectedThreadId =
        0;

    g_armState.capturedElement =
        nullptr;

    g_armState.capturedThreadId =
        0;

    g_armState.hits =
        0;

    g_armState.mutationAttempted =
        false;

    g_armState.mutationSucceeded =
        false;

    g_armState.wordWindow =
        nullptr;


    ReleaseSRWLockExclusive(
        &g_armLock);


    return result;
}


// ============================================================================
// Trigger ElementProvider::GetName
//
// Returned string is discarded without inspection.
// ============================================================================

static HRESULT TriggerProviderName(
    IUIAutomationElement* element) {

    if (!element)
        return E_POINTER;


    BSTR ignored =
        nullptr;


    HRESULT hr =
        element->
            get_CurrentName(
                &ignored);


    if (ignored) {

        SysFreeString(
            ignored);
    }


    return hr;
}


// ============================================================================
// Three-stage provider handshake
// ============================================================================

static bool CollapseStructuralTarget(
    IUIAutomationElement* target,
    HWND wordWindow) {

    // -----------------------------------------------------------------------
    // Stage 1: CAPTURE
    //
    // Capture the backing NetUI::Element* AND callback thread.
    // -----------------------------------------------------------------------

    BeginArm(
        ArmMode::Capture,
        nullptr,
        0,
        wordWindow);


    HRESULT hr =
        TriggerProviderName(
            target);


    ArmState capture =
        EndArm();


    if (FAILED(hr) ||
        capture.hits !=
            1 ||
        !capture.capturedElement ||
        capture.capturedThreadId ==
            0) {


        LogHandshakeFailure(
            L"Capture",
            capture);


        return false;
    }


    void* candidateElement =
        capture.capturedElement;


    DWORD candidateThreadId =
        capture.capturedThreadId;


    // -----------------------------------------------------------------------
    // Stage 2: VERIFY
    //
    // Require:
    //
    //     same Element*
    //     same callback thread
    // -----------------------------------------------------------------------

    BeginArm(
        ArmMode::Verify,
        candidateElement,
        candidateThreadId,
        wordWindow);


    hr =
        TriggerProviderName(
            target);


    ArmState verify =
        EndArm();


    if (FAILED(hr) ||
        verify.hits !=
            1 ||
        verify.capturedElement !=
            candidateElement ||
        verify.capturedThreadId !=
            candidateThreadId) {


        LogHandshakeFailure(
            L"Verify",
            verify);


        return false;
    }


    // -----------------------------------------------------------------------
    // Stage 3: MUTATE
    //
    // Hook performs mutation only if BOTH identity constraints still match.
    // -----------------------------------------------------------------------

    BeginArm(
        ArmMode::Mutate,
        candidateElement,
        candidateThreadId,
        wordWindow);


    hr =
        TriggerProviderName(
            target);


    ArmState mutate =
        EndArm();


    if (FAILED(hr) ||
        mutate.hits !=
            1 ||
        !mutate.mutationAttempted ||
        !mutate.mutationSucceeded) {


        LogHandshakeFailure(
            L"Mutate",
            mutate);


        return false;
    }


    return true;
}


// ============================================================================
// Minimal UIA identity
//
// No Name property is read here.
// ============================================================================

struct ElementIdentity {

    BSTR className;

    BSTR automationId;

    CONTROLTYPEID controlType;

    BOOL offscreen;

    RECT rect;
};


static void InitIdentity(
    ElementIdentity* identity) {

    ZeroMemory(
        identity,
        sizeof(*identity));
}


static void FreeIdentity(
    ElementIdentity* identity) {

    if (identity->className) {


        SysFreeString(
            identity->className);
    }


    if (identity->automationId) {


        SysFreeString(
            identity->automationId);
    }


    ZeroMemory(
        identity,
        sizeof(*identity));
}


static bool ReadIdentity(
    IUIAutomationElement* element,
    ElementIdentity* identity) {

    InitIdentity(
        identity);


    if (!element)
        return false;


    if (FAILED(
            element->
                get_CurrentClassName(
                    &identity->className))) {


        FreeIdentity(
            identity);


        return false;
    }


    if (FAILED(
            element->
                get_CurrentAutomationId(
                    &identity->automationId))) {


        FreeIdentity(
            identity);


        return false;
    }


    if (FAILED(
            element->
                get_CurrentControlType(
                    &identity->controlType))) {


        FreeIdentity(
            identity);


        return false;
    }


    if (FAILED(
            element->
                get_CurrentIsOffscreen(
                    &identity->offscreen))) {


        FreeIdentity(
            identity);


        return false;
    }


    if (FAILED(
            element->
                get_CurrentBoundingRectangle(
                    &identity->rect))) {


        FreeIdentity(
            identity);


        return false;
    }


    return true;
}


// ============================================================================
// Structural predicates
// ============================================================================

static bool IsSearchAnchor(
    const ElementIdentity& identity) {

    return
        StringEquals(
            identity.className,
            kSearchClass) &&

        StringEquals(
            identity.automationId,
            kSearchAutomationId) &&

        identity.controlType ==
            UIA_ButtonControlTypeId;
}


static bool IsTargetCandidate(
    const ElementIdentity& identity) {

    return
        StringEquals(
            identity.className,
            kTargetClass) &&

        StringEmpty(
            identity.automationId) &&

        identity.controlType ==
            UIA_ButtonControlTypeId &&

        !identity.offscreen &&

        identity.rect.right >
            identity.rect.left &&

        identity.rect.bottom >
            identity.rect.top;
}


static bool IsAccountAnchor(
    const ElementIdentity& identity) {

    return
        StringEquals(
            identity.className,
            kAccountClass) &&

        StringEquals(
            identity.automationId,
            kAccountAutomationId);
}


// ============================================================================
// Scan one NetUInetpane
//
// Exact raw-view sequence:
//
//     SearchToggleButton
//     anonymous NetUIRibbonButton
//     MeControlWidget
//
// Counts ALL occurrences. More than one is considered ambiguous.
// ============================================================================

static int FindStructuralTargetsInRibbon(
    IUIAutomationTreeWalker* walker,
    IUIAutomationElement* ribbon,
    IUIAutomationElement** uniqueTarget) {

    if (uniqueTarget) {

        *uniqueTarget =
            nullptr;
    }


    if (!walker ||
        !ribbon ||
        !uniqueTarget) {

        return 0;
    }


    IUIAutomationElement* child =
        nullptr;


    HRESULT hr =
        walker->
            GetFirstChildElement(
                ribbon,
                &child);


    if (FAILED(hr) ||
        !child) {

        return 0;
    }


    enum SequenceState {

        LookingForSearch = 0,

        LookingForTarget,

        LookingForAccount
    };


    SequenceState state =
        LookingForSearch;


    IUIAutomationElement* candidate =
        nullptr;

    IUIAutomationElement* firstMatch =
        nullptr;


    int matchCount =
        0;


    while (child) {


        ElementIdentity identity = {};


        bool valid =
            ReadIdentity(
                child,
                &identity);


        if (!valid) {


            if (candidate) {


                candidate->Release();

                candidate =
                    nullptr;
            }


            state =
                LookingForSearch;
        }
        else {


            switch (state) {


                // -----------------------------------------------------------
                // Find SearchToggleButton.
                // -----------------------------------------------------------

                case LookingForSearch:


                    if (IsSearchAnchor(
                            identity)) {


                        state =
                            LookingForTarget;
                    }


                    break;


                // -----------------------------------------------------------
                // Immediate next child must be our anonymous RibbonButton.
                // -----------------------------------------------------------

                case LookingForTarget:


                    if (IsTargetCandidate(
                            identity)) {


                        child->AddRef();

                        candidate =
                            child;

                        state =
                            LookingForAccount;
                    }
                    else {


                        state =
                            IsSearchAnchor(
                                identity)
                                ? LookingForTarget
                                : LookingForSearch;
                    }


                    break;


                // -----------------------------------------------------------
                // Immediate next child must be MeControlWidget.
                // -----------------------------------------------------------

                case LookingForAccount:


                    if (IsAccountAnchor(
                            identity)) {


                        matchCount++;


                        if (matchCount ==
                            1) {


                            firstMatch =
                                candidate;

                            candidate =
                                nullptr;
                        }
                        else {


                            if (candidate) {


                                candidate->
                                    Release();

                                candidate =
                                    nullptr;
                            }
                        }


                        state =
                            LookingForSearch;
                    }
                    else {


                        if (candidate) {


                            candidate->
                                Release();

                            candidate =
                                nullptr;
                        }


                        state =
                            IsSearchAnchor(
                                identity)
                                ? LookingForTarget
                                : LookingForSearch;
                    }


                    break;
            }
        }


        FreeIdentity(
            &identity);


        IUIAutomationElement* next =
            nullptr;


        walker->
            GetNextSiblingElement(
                child,
                &next);


        child->Release();


        child =
            next;
    }


    if (candidate) {


        candidate->Release();

        candidate =
            nullptr;
    }


    // Exactly one match is required.
    if (matchCount ==
            1 &&
        firstMatch) {


        *uniqueTarget =
            firstMatch;
    }
    else {


        if (firstMatch) {


            firstMatch->Release();

            firstMatch =
                nullptr;
        }
    }


    return matchCount;
}


// ============================================================================
// Scan one Word top-level window
// ============================================================================

static void ScanOneWordWindow(
    IUIAutomation* automation,
    IUIAutomationTreeWalker* walker,
    HWND hwnd) {

    IUIAutomationElement* root =
        nullptr;


    if (FAILED(
            automation->
                ElementFromHandle(
                    hwnd,
                    &root)) ||
        !root) {

        return;
    }


    VARIANT classValue;

    VariantInit(
        &classValue);


    classValue.vt =
        VT_BSTR;

    classValue.bstrVal =
        SysAllocString(
            kRibbonClass);


    IUIAutomationCondition* classCondition =
        nullptr;


    HRESULT hr =
        automation->
            CreatePropertyCondition(
                UIA_ClassNamePropertyId,
                classValue,
                &classCondition);


    VariantClear(
        &classValue);


    if (FAILED(hr) ||
        !classCondition) {


        root->Release();

        return;
    }


    IUIAutomationElementArray* ribbons =
        nullptr;


    hr =
        root->
            FindAll(
                TreeScope_Descendants,
                classCondition,
                &ribbons);


    classCondition->Release();


    if (FAILED(hr) ||
        !ribbons) {


        root->Release();

        return;
    }


    int ribbonCount =
        0;


    ribbons->
        get_Length(
            &ribbonCount);


    int totalMatches =
        0;


    IUIAutomationElement* target =
        nullptr;


    for (int i = 0;
         i < ribbonCount;
         i++) {


        IUIAutomationElement* ribbon =
            nullptr;


        if (FAILED(
                ribbons->
                    GetElement(
                        i,
                        &ribbon)) ||
            !ribbon) {

            continue;
        }


        IUIAutomationElement* ribbonTarget =
            nullptr;


        int ribbonMatches =
            FindStructuralTargetsInRibbon(
                walker,
                ribbon,
                &ribbonTarget);


        ribbon->Release();


        totalMatches +=
            ribbonMatches;


        if (ribbonTarget) {


            if (!target) {


                target =
                    ribbonTarget;
            }
            else {


                ribbonTarget->
                    Release();
            }
        }
    }


    ribbons->Release();

    root->Release();


    // -----------------------------------------------------------------------
    // Fail closed unless exactly one matching structure exists in the entire
    // Word top-level window.
    // -----------------------------------------------------------------------

    if (totalMatches !=
            1 ||
        !target) {


        if (target) {


            target->Release();

            target =
                nullptr;
        }


        if (totalMatches >
            1) {


            LONG number =
                InterlockedIncrement(
                    &g_ambiguousCount);


            if (number <=
                5) {


                Wh_Log(
                    L"Ambiguous structural targets "
                    L"in hwnd=%p: %d; ignored",
                    hwnd,
                    totalMatches);
            }
        }


        return;
    }


    CollapseStructuralTarget(
        target,
        hwnd);


    target->Release();
}


// ============================================================================
// Enumerate Word top-level windows
// ============================================================================

struct WindowScanContext {

    IUIAutomation* automation;

    IUIAutomationTreeWalker* walker;
};


static BOOL CALLBACK
EnumWordWindowProc(
    HWND hwnd,
    LPARAM lParam) {

    DWORD pid =
        0;


    GetWindowThreadProcessId(
        hwnd,
        &pid);


    if (pid !=
        GetCurrentProcessId()) {

        return TRUE;
    }


    wchar_t className[64] = {};


    if (!GetClassNameW(
            hwnd,
            className,
            ARRAYSIZE(className))) {

        return TRUE;
    }


    if (_wcsicmp(
            className,
            L"OpusApp") != 0) {

        return TRUE;
    }


    if (!IsWindowVisible(
            hwnd)) {

        return TRUE;
    }


    auto* context =
        reinterpret_cast<
            WindowScanContext*>(
                lParam);


    ScanOneWordWindow(
        context->automation,
        context->walker,
        hwnd);


    return TRUE;
}


// ============================================================================
// Continuous structural scanner
// ============================================================================

static DWORD WINAPI
ScanThreadProc(
    void*) {

    HRESULT hr =
        CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED);


    if (FAILED(hr)) {


        Wh_Log(
            L"CoInitializeEx failed: "
            L"0x%08X",
            static_cast<unsigned int>(
                hr));


        return 0;
    }


    IUIAutomation* automation =
        nullptr;


    hr =
        CoCreateInstance(
            CLSID_CUIAutomation,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IUIAutomation,
            reinterpret_cast<void**>(
                &automation));


    if (FAILED(hr) ||
        !automation) {


        Wh_Log(
            L"Couldn't create UIAutomation");


        CoUninitialize();


        return 0;
    }


    IUIAutomationTreeWalker* walker =
        nullptr;


    hr =
        automation->
            get_RawViewWalker(
                &walker);


    if (FAILED(hr) ||
        !walker) {


        Wh_Log(
            L"Couldn't obtain RawViewWalker");


        automation->Release();

        CoUninitialize();


        return 0;
    }


    WindowScanContext context = {

        automation,

        walker
    };


    Wh_Log(
        L"Structural scanner active");


    while (WaitForSingleObject(
               g_stopEvent,
               0) ==
           WAIT_TIMEOUT) {


        EnumWindows(
            EnumWordWindowProc,
            reinterpret_cast<LPARAM>(
                &context));


        // One-second polling gives us:
        //
        // - additional Word windows
        // - ribbon/theme reconstruction
        // - account UI rebuilds
        // - reappearance after Word restores the property
        //
        // without retaining any private Element* between scans.
        if (WaitForSingleObject(
                g_stopEvent,
                1000) !=
            WAIT_TIMEOUT) {


            break;
        }
    }


    walker->Release();

    automation->Release();

    CoUninitialize();


    return 0;
}


// ============================================================================
// Install private functions / hook
// ============================================================================

static bool Install(
    HMODULE mso) {

    // -----------------------------------------------------------------------
    // Layer 1:
    // PE fingerprint.
    // -----------------------------------------------------------------------

    if (!ValidateMso(
            mso)) {

        return false;
    }


    // -----------------------------------------------------------------------
    // Layer 2:
    // Exact executable addresses + machine-code prefixes.
    // -----------------------------------------------------------------------

    if (!ValidatePrivateFunctions(
            mso)) {

        Wh_Log(
            L"Private-code validation failed; "
            L"mod disabled");


        return false;
    }


    uintptr_t base =
        reinterpret_cast<uintptr_t>(
            mso);


    void* setWidthPixels =
        reinterpret_cast<void*>(
            base +
            kRvaSetWidthPixels);


    void* setIsVisible =
        reinterpret_cast<void*>(
            base +
            kRvaSetIsVisible);


    void* providerGetElement =
        reinterpret_cast<void*>(
            base +
            kRvaProviderGetElement);


    void* providerGetName =
        reinterpret_cast<void*>(
            base +
            kRvaProviderGetName);


    g_setWidthPixels =
        reinterpret_cast<
            SetWidthPixels_t>(
                setWidthPixels);


    g_setIsVisible =
        reinterpret_cast<
            SetIsVisible_t>(
                setIsVisible);


    g_providerGetElement =
        reinterpret_cast<
            ProviderGetElement_t>(
                providerGetElement);


    if (!Wh_SetFunctionHook(
            providerGetName,
            reinterpret_cast<void*>(
                ProviderGetName_Hook),
            reinterpret_cast<void**>(
                &g_origProviderGetName))) {


        Wh_Log(
            L"ProviderGetName hook failed");


        return false;
    }


    if (!Wh_ApplyHookOperations()) {


        Wh_Log(
            L"Wh_ApplyHookOperations failed");


        return false;
    }


    Wh_Log(
        L"Hardened language-independent "
        L"provider hook active");


    return true;
}


// ============================================================================
// Loader
// ============================================================================

static DWORD WINAPI
LoaderThreadProc(
    void*) {

    if (WaitForSingleObject(
            g_stopEvent,
            250) !=
        WAIT_TIMEOUT) {


        return 0;
    }


    while (WaitForSingleObject(
               g_stopEvent,
               100) ==
           WAIT_TIMEOUT) {


        HMODULE mso =
            GetModuleHandleW(
                L"mso.dll");


        if (!mso)
            continue;


        if (!Install(
                mso)) {


            return 0;
        }


        g_scanThread =
            CreateThread(
                nullptr,
                0,
                ScanThreadProc,
                nullptr,
                0,
                nullptr);


        if (!g_scanThread) {


            Wh_Log(
                L"Couldn't create scanner thread");


            return 0;
        }


        Wh_Log(
            L"Ready");


        return 0;
    }


    return 0;
}


// ============================================================================
// Windhawk entry points
// ============================================================================

BOOL Wh_ModInit() {

    Wh_Log(
        L"Word Upgrade remover "
        L"v1.1.0 init");


    g_stopEvent =
        CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            nullptr);


    if (!g_stopEvent) {


        Wh_Log(
            L"CreateEventW failed");


        return FALSE;
    }


    g_loaderThread =
        CreateThread(
            nullptr,
            0,
            LoaderThreadProc,
            nullptr,
            0,
            nullptr);


    if (!g_loaderThread) {


        Wh_Log(
            L"CreateThread failed");


        CloseHandle(
            g_stopEvent);


        g_stopEvent =
            nullptr;


        return FALSE;
    }


    return TRUE;
}


void Wh_ModBeforeUninit() {

    if (g_stopEvent) {


        SetEvent(
            g_stopEvent);
    }


    if (g_loaderThread) {


        WaitForSingleObject(
            g_loaderThread,
            INFINITE);
    }


    if (g_scanThread) {


        WaitForSingleObject(
            g_scanThread,
            INFINITE);
    }
}


void Wh_ModUninit() {

    Wh_Log(
        L"Uninit");


    if (g_loaderThread) {


        CloseHandle(
            g_loaderThread);


        g_loaderThread =
            nullptr;
    }


    if (g_scanThread) {


        CloseHandle(
            g_scanThread);


        g_scanThread =
            nullptr;
    }


    if (g_stopEvent) {


        CloseHandle(
            g_stopEvent);


        g_stopEvent =
            nullptr;
    }
}