// ==WindhawkMod==
// @id              appsfolder-unhide-hidden-apps
// @name            AppsFolder Unhide Hidden Apps
// @description     Adds selected hidden AppUserModelIDs to shell:AppsFolder enumeration.
// @version         0.9
// @author          Antonomasia
// @include         explorer.exe
// @include         powershell.exe
// @include         pwsh.exe
// ==/WindhawkMod==

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <vector>

using EnumObjects_t = HRESULT(STDMETHODCALLTYPE*)(
    IShellFolder* self,
    HWND hwndOwner,
    DWORD grfFlags,
    IEnumIDList** ppenumIDList
);

using SHBindToObject_t = HRESULT(WINAPI*)(
    IShellFolder* psf,
    PCUIDLIST_RELATIVE pidl,
    IBindCtx* pbc,
    REFIID riid,
    void** ppv
);

EnumObjects_t EnumObjects_Original = nullptr;
SHBindToObject_t SHBindToObject_Original = nullptr;

void* g_enumObjectsPtr = nullptr;
bool g_enumHookInstalled = false;
bool g_bindHookInstalled = false;

static const GUID IID_IUnknown_Local =
    {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static const GUID IID_IEnumIDList_Local =
    {0x000214F2, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static const GUID IID_IShellFolder_Local =
    {0x000214E6, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static const GUID IID_IShellFolder2_Local =
    {0x93F2F68C, 0x1D1B, 0x11D3, {0xA3, 0x0E, 0x00, 0xC0, 0x4F, 0x79, 0xAB, 0xD1}};

static bool GuidEqual(REFIID a, REFGUID b)
{
    return IsEqualGUID(a, b);
}

static const wchar_t* g_extraAumids[] = {
    L"c5e2524a-ea46-4f67-841f-6a9465d9d515_cw5n1h2txyewy!App",
    L"Microsoft.Windows.PrintQueueActionCenter_cw5n1h2txyewy!App",
};

class EnumIDListWithExtras final : public IEnumIDList
{
private:
    LONG m_ref = 1;
    IEnumIDList* m_inner = nullptr;
    std::vector<LPITEMIDLIST> m_extras;
    size_t m_extraIndex = 0;
    bool m_innerDone = false;

public:
    EnumIDListWithExtras(IEnumIDList* inner, const std::vector<LPITEMIDLIST>& extras)
        : m_inner(inner)
    {
        if (m_inner)
        {
            m_inner->AddRef();
        }

        for (auto pidl : extras)
        {
            if (pidl)
            {
                m_extras.push_back(ILClone(pidl));
            }
        }
    }

    ~EnumIDListWithExtras()
    {
        if (m_inner)
        {
            m_inner->Release();
        }

        for (auto pidl : m_extras)
        {
            ILFree(pidl);
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (!ppvObject)
        {
            return E_POINTER;
        }

        *ppvObject = nullptr;

        if (GuidEqual(riid, IID_IUnknown_Local) ||
            GuidEqual(riid, IID_IEnumIDList_Local))
        {
            *ppvObject = static_cast<IEnumIDList*>(this);
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return InterlockedIncrement(&m_ref);
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG ref = InterlockedDecrement(&m_ref);

        if (ref == 0)
        {
            delete this;
        }

        return ref;
    }

    HRESULT STDMETHODCALLTYPE Next(
        ULONG celt,
        LPITEMIDLIST* rgelt,
        ULONG* pceltFetched) override
    {
        if (!rgelt)
        {
            return E_POINTER;
        }

        ULONG fetched = 0;

        while (fetched < celt && !m_innerDone && m_inner)
        {
            ULONG innerFetched = 0;

            HRESULT hr = m_inner->Next(
                celt - fetched,
                rgelt + fetched,
                &innerFetched
            );

            fetched += innerFetched;

            if (FAILED(hr))
            {
                if (pceltFetched)
                {
                    *pceltFetched = fetched;
                }

                return hr;
            }

            if (hr == S_FALSE || innerFetched == 0)
            {
                m_innerDone = true;
                break;
            }
        }

        while (fetched < celt && m_extraIndex < m_extras.size())
        {
            rgelt[fetched] = ILClone(m_extras[m_extraIndex]);

            if (rgelt[fetched])
            {
                fetched++;
            }

            m_extraIndex++;
        }

        if (pceltFetched)
        {
            *pceltFetched = fetched;
        }

        return fetched == celt ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override
    {
        ULONG skipped = 0;

        while (skipped < celt)
        {
            LPITEMIDLIST pidl = nullptr;
            ULONG fetched = 0;

            HRESULT hr = Next(1, &pidl, &fetched);

            if (pidl)
            {
                ILFree(pidl);
            }

            if (hr != S_OK || fetched == 0)
            {
                return S_FALSE;
            }

            skipped++;
        }

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Reset() override
    {
        m_extraIndex = 0;
        m_innerDone = false;

        if (m_inner)
        {
            return m_inner->Reset();
        }

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IEnumIDList** ppenum) override
    {
        if (!ppenum)
        {
            return E_POINTER;
        }

        *ppenum = nullptr;

        IEnumIDList* clonedInner = nullptr;

        if (m_inner)
        {
            HRESULT hr = m_inner->Clone(&clonedInner);

            if (FAILED(hr))
            {
                return hr;
            }
        }

        auto clone = new EnumIDListWithExtras(clonedInner, m_extras);

        if (clonedInner)
        {
            clonedInner->Release();
        }

        clone->m_extraIndex = m_extraIndex;
        clone->m_innerDone = m_innerDone;

        *ppenum = clone;
        return S_OK;
    }
};

static std::vector<LPITEMIDLIST> BuildExtraPidls(IShellFolder* appsFolder)
{
    std::vector<LPITEMIDLIST> result;

    if (!appsFolder)
    {
        return result;
    }

    for (const wchar_t* aumid : g_extraAumids)
    {
        wchar_t parseName[512];

        HRESULT copyHr = StringCchCopyW(
            parseName,
            ARRAYSIZE(parseName),
            aumid
        );

        if (FAILED(copyHr))
        {
            Wh_Log(
                L"StringCchCopyW failed for AUMID: %s, hr=0x%08X",
                aumid,
                copyHr
            );

            continue;
        }

        ULONG eaten = 0;
        SFGAOF attrs = 0;
        LPITEMIDLIST childPidl = nullptr;

        HRESULT hr = appsFolder->ParseDisplayName(
            nullptr,
            nullptr,
            parseName,
            &eaten,
            &childPidl,
            &attrs
        );

        if (SUCCEEDED(hr) && childPidl)
        {
            Wh_Log(L"Parsed hidden AppsFolder item: %s", aumid);
            result.push_back(childPidl);
        }
        else
        {
            Wh_Log(
                L"Failed to parse hidden AppsFolder item: %s, hr=0x%08X",
                aumid,
                hr
            );
        }
    }

    return result;
}

static bool TryParseHiddenAumid(IShellFolder* folder, const wchar_t* aumid)
{
    if (!folder || !aumid)
    {
        return false;
    }

    wchar_t parseName[512];

    HRESULT copyHr = StringCchCopyW(
        parseName,
        ARRAYSIZE(parseName),
        aumid
    );

    if (FAILED(copyHr))
    {
        return false;
    }

    ULONG eaten = 0;
    SFGAOF attrs = 0;
    LPITEMIDLIST childPidl = nullptr;

    HRESULT hr = folder->ParseDisplayName(
        nullptr,
        nullptr,
        parseName,
        &eaten,
        &childPidl,
        &attrs
    );

    if (childPidl)
    {
        ILFree(childPidl);
    }

    return SUCCEEDED(hr);
}

static bool IsAppsFolderObject(IShellFolder* folder)
{
    return TryParseHiddenAumid(
        folder,
        L"c5e2524a-ea46-4f67-841f-6a9465d9d515_cw5n1h2txyewy!App"
    );
}

HRESULT STDMETHODCALLTYPE EnumObjects_Hook(
    IShellFolder* self,
    HWND hwndOwner,
    DWORD grfFlags,
    IEnumIDList** ppenumIDList)
{
    HRESULT hr = EnumObjects_Original(
        self,
        hwndOwner,
        grfFlags,
        ppenumIDList
    );

    Wh_Log(
        L"Real AppsFolder EnumObjects called, hr=0x%08X, grfFlags=0x%08X",
        hr,
        grfFlags
    );

    if (FAILED(hr) || !ppenumIDList || !*ppenumIDList)
    {
        return hr;
    }

    auto extras = BuildExtraPidls(self);

    if (extras.empty())
    {
        return hr;
    }

    IEnumIDList* originalEnum = *ppenumIDList;

    *ppenumIDList = new EnumIDListWithExtras(
        originalEnum,
        extras
    );

    originalEnum->Release();

    for (auto pidl : extras)
    {
        ILFree(pidl);
    }

    Wh_Log(
        L"Real AppsFolder enumeration wrapped with %u extra item(s)",
        (UINT)extras.size()
    );

    return hr;
}

static bool InstallEnumObjectsHookFromFolder(IShellFolder* appsFolder)
{
    if (!appsFolder)
    {
        return false;
    }

    if (g_enumHookInstalled)
    {
        return true;
    }

    void** vtbl = *(void***)appsFolder;

    // IShellFolder vtable:
    // 0 QueryInterface
    // 1 AddRef
    // 2 Release
    // 3 ParseDisplayName
    // 4 EnumObjects
    void* enumObjectsPtr = vtbl[4];

    if (!enumObjectsPtr)
    {
        Wh_Log(L"EnumObjects pointer is null");
        return false;
    }

    g_enumObjectsPtr = enumObjectsPtr;

    Wh_Log(
        L"Installing real AppsFolder EnumObjects hook: %p",
        enumObjectsPtr
    );

    Wh_SetFunctionHook(
        enumObjectsPtr,
        reinterpret_cast<void*>(EnumObjects_Hook),
        reinterpret_cast<void**>(&EnumObjects_Original)
    );

    g_enumHookInstalled = true;

    Wh_Log(L"Real AppsFolder EnumObjects hook installed");
    return true;
}

class AppsFolderShellFolderWrapper final : public IShellFolder2
{
private:
    LONG m_ref = 1;
    IShellFolder* m_inner = nullptr;
    IShellFolder2* m_inner2 = nullptr;

public:
    AppsFolderShellFolderWrapper(IShellFolder* inner)
        : m_inner(inner)
    {
        if (m_inner)
        {
            m_inner->AddRef();

            m_inner->QueryInterface(
                IID_IShellFolder2_Local,
                reinterpret_cast<void**>(&m_inner2)
            );

            InstallEnumObjectsHookFromFolder(m_inner);
        }
    }

    ~AppsFolderShellFolderWrapper()
    {
        if (m_inner2)
        {
            m_inner2->Release();
        }

        if (m_inner)
        {
            m_inner->Release();
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (!ppvObject)
        {
            return E_POINTER;
        }

        *ppvObject = nullptr;

        if (GuidEqual(riid, IID_IUnknown_Local) ||
            GuidEqual(riid, IID_IShellFolder_Local) ||
            GuidEqual(riid, IID_IShellFolder2_Local))
        {
            *ppvObject = static_cast<IShellFolder2*>(this);
            AddRef();
            return S_OK;
        }

        if (m_inner)
        {
            return m_inner->QueryInterface(riid, ppvObject);
        }

        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return InterlockedIncrement(&m_ref);
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG ref = InterlockedDecrement(&m_ref);

        if (ref == 0)
        {
            delete this;
        }

        return ref;
    }

    HRESULT STDMETHODCALLTYPE ParseDisplayName(
        HWND hwnd,
        IBindCtx* pbc,
        LPWSTR pszDisplayName,
        ULONG* pchEaten,
        PIDLIST_RELATIVE* ppidl,
        ULONG* pdwAttributes) override
    {
        return m_inner->ParseDisplayName(
            hwnd,
            pbc,
            pszDisplayName,
            pchEaten,
            ppidl,
            pdwAttributes
        );
    }

    HRESULT STDMETHODCALLTYPE EnumObjects(
        HWND hwnd,
        DWORD grfFlags,
        IEnumIDList** ppenumIDList) override
    {
        InstallEnumObjectsHookFromFolder(m_inner);

        HRESULT hr = m_inner->EnumObjects(
            hwnd,
            grfFlags,
            ppenumIDList
        );

        Wh_Log(
            L"Proxy EnumObjects called, hr=0x%08X, grfFlags=0x%08X",
            hr,
            grfFlags
        );

        if (FAILED(hr) || !ppenumIDList || !*ppenumIDList)
        {
            return hr;
        }

        auto extras = BuildExtraPidls(m_inner);

        if (extras.empty())
        {
            return hr;
        }

        IEnumIDList* originalEnum = *ppenumIDList;

        *ppenumIDList = new EnumIDListWithExtras(
            originalEnum,
            extras
        );

        originalEnum->Release();

        for (auto pidl : extras)
        {
            ILFree(pidl);
        }

        Wh_Log(
            L"Proxy wrapped AppsFolder enumeration with %u extra item(s)",
            (UINT)extras.size()
        );

        return hr;
    }

    HRESULT STDMETHODCALLTYPE BindToObject(
        PCUIDLIST_RELATIVE pidl,
        IBindCtx* pbc,
        REFIID riid,
        void** ppv) override
    {
        return m_inner->BindToObject(
            pidl,
            pbc,
            riid,
            ppv
        );
    }

    HRESULT STDMETHODCALLTYPE BindToStorage(
        PCUIDLIST_RELATIVE pidl,
        IBindCtx* pbc,
        REFIID riid,
        void** ppv) override
    {
        return m_inner->BindToStorage(
            pidl,
            pbc,
            riid,
            ppv
        );
    }

    HRESULT STDMETHODCALLTYPE CompareIDs(
        LPARAM lParam,
        PCUIDLIST_RELATIVE pidl1,
        PCUIDLIST_RELATIVE pidl2) override
    {
        return m_inner->CompareIDs(
            lParam,
            pidl1,
            pidl2
        );
    }

    HRESULT STDMETHODCALLTYPE CreateViewObject(
        HWND hwndOwner,
        REFIID riid,
        void** ppv) override
    {
        Wh_Log(L"Proxy CreateViewObject called; installing real EnumObjects hook");

        InstallEnumObjectsHookFromFolder(m_inner);

        return m_inner->CreateViewObject(
            hwndOwner,
            riid,
            ppv
        );
    }

    HRESULT STDMETHODCALLTYPE GetAttributesOf(
        UINT cidl,
        PCUITEMID_CHILD_ARRAY apidl,
        SFGAOF* rgfInOut) override
    {
        return m_inner->GetAttributesOf(
            cidl,
            apidl,
            rgfInOut
        );
    }

    HRESULT STDMETHODCALLTYPE GetUIObjectOf(
        HWND hwndOwner,
        UINT cidl,
        PCUITEMID_CHILD_ARRAY apidl,
        REFIID riid,
        UINT* rgfReserved,
        void** ppv) override
    {
        return m_inner->GetUIObjectOf(
            hwndOwner,
            cidl,
            apidl,
            riid,
            rgfReserved,
            ppv
        );
    }

    HRESULT STDMETHODCALLTYPE GetDisplayNameOf(
        PCUITEMID_CHILD pidl,
        SHGDNF uFlags,
        STRRET* pName) override
    {
        return m_inner->GetDisplayNameOf(
            pidl,
            uFlags,
            pName
        );
    }

    HRESULT STDMETHODCALLTYPE SetNameOf(
        HWND hwnd,
        PCUITEMID_CHILD pidl,
        LPCWSTR pszName,
        SHGDNF uFlags,
        PITEMID_CHILD* ppidlOut) override
    {
        return m_inner->SetNameOf(
            hwnd,
            pidl,
            pszName,
            uFlags,
            ppidlOut
        );
    }

    HRESULT STDMETHODCALLTYPE GetDefaultSearchGUID(GUID* pguid) override
    {
        if (!m_inner2)
        {
            return E_NOTIMPL;
        }

        return m_inner2->GetDefaultSearchGUID(pguid);
    }

    HRESULT STDMETHODCALLTYPE EnumSearches(IEnumExtraSearch** ppenum) override
    {
        if (!m_inner2)
        {
            return E_NOTIMPL;
        }

        return m_inner2->EnumSearches(ppenum);
    }

    HRESULT STDMETHODCALLTYPE GetDefaultColumn(
        DWORD dwRes,
        ULONG* pSort,
        ULONG* pDisplay) override
    {
        if (!m_inner2)
        {
            return E_NOTIMPL;
        }

        return m_inner2->GetDefaultColumn(
            dwRes,
            pSort,
            pDisplay
        );
    }

    HRESULT STDMETHODCALLTYPE GetDefaultColumnState(
        UINT iColumn,
        SHCOLSTATEF* pcsFlags) override
    {
        if (!m_inner2)
        {
            return E_NOTIMPL;
        }

        return m_inner2->GetDefaultColumnState(
            iColumn,
            pcsFlags
        );
    }

    HRESULT STDMETHODCALLTYPE GetDetailsEx(
        PCUITEMID_CHILD pidl,
        const SHCOLUMNID* pscid,
        VARIANT* pv) override
    {
        if (!m_inner2)
        {
            return E_NOTIMPL;
        }

        return m_inner2->GetDetailsEx(
            pidl,
            pscid,
            pv
        );
    }

    HRESULT STDMETHODCALLTYPE GetDetailsOf(
        PCUITEMID_CHILD pidl,
        UINT iColumn,
        SHELLDETAILS* psd) override
    {
        if (!m_inner2)
        {
            return E_NOTIMPL;
        }

        return m_inner2->GetDetailsOf(
            pidl,
            iColumn,
            psd
        );
    }

    HRESULT STDMETHODCALLTYPE MapColumnToSCID(
        UINT iColumn,
        SHCOLUMNID* pscid) override
    {
        if (!m_inner2)
        {
            return E_NOTIMPL;
        }

        return m_inner2->MapColumnToSCID(
            iColumn,
            pscid
        );
    }
};

static bool ShouldWrapRequestedInterface(REFIID riid)
{
    return GuidEqual(riid, IID_IShellFolder_Local) ||
           GuidEqual(riid, IID_IShellFolder2_Local);
}

HRESULT WINAPI SHBindToObject_Hook(
    IShellFolder* psf,
    PCUIDLIST_RELATIVE pidl,
    IBindCtx* pbc,
    REFIID riid,
    void** ppv)
{
    HRESULT hr = SHBindToObject_Original(
        psf,
        pidl,
        pbc,
        riid,
        ppv
    );

    if (SUCCEEDED(hr) && ppv && *ppv && ShouldWrapRequestedInterface(riid))
    {
        IShellFolder* folder = static_cast<IShellFolder*>(*ppv);

        if (IsAppsFolderObject(folder))
        {
            Wh_Log(L"SHBindToObject returned AppsFolder; installing real hook and replacing with proxy");

            InstallEnumObjectsHookFromFolder(folder);

            AppsFolderShellFolderWrapper* wrapper =
                new AppsFolderShellFolderWrapper(folder);

            folder->Release();

            if (GuidEqual(riid, IID_IShellFolder2_Local))
            {
                *ppv = static_cast<IShellFolder2*>(wrapper);
            }
            else
            {
                *ppv = static_cast<IShellFolder*>(wrapper);
            }
        }
    }

    return hr;
}

static bool InstallSHBindToObjectHook()
{
    if (g_bindHookInstalled)
    {
        return true;
    }

    HMODULE shell32 = GetModuleHandleW(L"shell32.dll");

    if (!shell32)
    {
        shell32 = LoadLibraryW(L"shell32.dll");
    }

    if (!shell32)
    {
        Wh_Log(L"Failed to load shell32.dll");
        return false;
    }

    void* target = reinterpret_cast<void*>(
        GetProcAddress(shell32, "SHBindToObject")
    );

    if (!target)
    {
        Wh_Log(L"SHBindToObject not found");
        return false;
    }

    Wh_SetFunctionHook(
        target,
        reinterpret_cast<void*>(SHBindToObject_Hook),
        reinterpret_cast<void**>(&SHBindToObject_Original)
    );

    g_bindHookInstalled = true;

    Wh_Log(L"SHBindToObject hook installed: %p", target);
    return true;
}

BOOL Wh_ModInit()
{
    Wh_Log(L"Init");

    InstallSHBindToObjectHook();

    return TRUE;
}

void Wh_ModUninit()
{
    Wh_Log(L"Uninit");
}