#ifndef AIP_DEPENDENCIES_STARTUP_SHORTCUT_INC
#define AIP_DEPENDENCIES_STARTUP_SHORTCUT_INC

// Optional baseline facility. Include after desktop_app_baseline.h so the
// shared path, hashing, and Win32 error helpers are available.
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace aip
{
struct StartupShortcutSpec
{
    std::wstring executablePath;
    // Stable ownership key, normally the effective absolute INI path. Keeping
    // it independent from launch arguments lets the helper repair quoting or
    // argument changes without orphaning an older shortcut.
    std::wstring identityPath;
    std::wstring fallbackBaseName;
    std::wstring arguments;
    std::wstring workingDirectory;
    std::wstring description;
};

class StartupShortcutComScope
{
public:
    StartupShortcutComScope()
        : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)),
          ownsInitialization_(SUCCEEDED(result_))
    {
    }

    ~StartupShortcutComScope()
    {
        if (ownsInitialization_)
        {
            CoUninitialize();
        }
    }

    bool Ready() const
    {
        // A caller may already have initialized COM using the multithreaded
        // apartment. ShellLink works there too; only this helper's requested
        // apartment change failed.
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

    HRESULT Result() const
    {
        return result_;
    }

    StartupShortcutComScope(const StartupShortcutComScope&) = delete;
    StartupShortcutComScope& operator=(const StartupShortcutComScope&) = delete;

private:
    HRESULT result_;
    bool ownsInitialization_;
};

inline std::wstring StartupShortcutHresultText(HRESULT result)
{
    std::wstring text = GetLastErrorText(static_cast<DWORD>(result));
    if (!text.empty())
    {
        return text;
    }

    wchar_t fallback[64] = {};
    swprintf_s(fallback, L"HRESULT 0x%08X", static_cast<unsigned int>(result));
    return fallback;
}

inline void SetStartupShortcutError(
    std::wstring* error,
    const std::wstring& operation,
    HRESULT result)
{
    if (error != nullptr)
    {
        *error = operation + L": " + StartupShortcutHresultText(result);
    }
}

inline void SetStartupShortcutWin32Error(
    std::wstring* error,
    const std::wstring& operation,
    DWORD result)
{
    if (error != nullptr)
    {
        *error = operation + L": " + GetLastErrorText(
            result == ERROR_SUCCESS ? ERROR_GEN_FAILURE : result);
    }
}

inline std::wstring SanitizeShortcutBaseName(std::wstring value)
{
    for (wchar_t& ch : value)
    {
        if (ch < 0x20 || ch == L'<' || ch == L'>' || ch == L':' ||
            ch == L'"' || ch == L'/' || ch == L'\\' || ch == L'|' ||
            ch == L'?' || ch == L'*')
        {
            ch = L'_';
        }
    }
    while (!value.empty() && (value.back() == L' ' || value.back() == L'.'))
    {
        value.pop_back();
    }
    // Keep the path comfortably below legacy ShellLink/MAX_PATH limits even
    // when the executable has an unusually long file name. The path hash keeps
    // the shortened name scoped to the exact executable.
    constexpr size_t kMaximumShortcutBaseNameLength = 80;
    if (value.size() > kMaximumShortcutBaseNameLength)
    {
        value.resize(kMaximumShortcutBaseNameLength);
        while (!value.empty() && (value.back() == L' ' || value.back() == L'.'))
        {
            value.pop_back();
        }
    }
    return value.empty() ? L"Application" : value;
}

inline std::wstring NormalizeShortcutComparisonPath(const std::wstring& path)
{
    std::wstring normalized = MakeAbsolutePath(path);
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    while (normalized.size() > 3 && normalized.back() == L'\\')
    {
        normalized.pop_back();
    }
    return ToLower(normalized);
}

inline bool IsLocalAbsoluteStartupPath(const std::wstring& path)
{
    return path.size() >= 3 &&
        iswalpha(path[0]) != 0 &&
        path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/');
}

class StartupShortcutMutationLock
{
public:
    explicit StartupShortcutMutationLock(const std::wstring& shortcutPath)
    {
        std::wstring name = L"Local\\AIProjects.StartupShortcut." +
            StableHashHex64(NormalizeShortcutComparisonPath(shortcutPath));
        handle_.Reset(CreateMutexW(nullptr, FALSE, name.c_str()));
        if (!handle_)
        {
            error_ = GetLastError();
            return;
        }

        DWORD wait = WaitForSingleObject(handle_.Get(), 10000);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED)
        {
            locked_ = true;
            return;
        }
        error_ = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        if (error_ == ERROR_SUCCESS)
        {
            error_ = ERROR_GEN_FAILURE;
        }
    }

    ~StartupShortcutMutationLock()
    {
        if (locked_)
        {
            ReleaseMutex(handle_.Get());
        }
    }

    bool IsLocked() const
    {
        return locked_;
    }

    DWORD Error() const
    {
        return error_;
    }

    StartupShortcutMutationLock(const StartupShortcutMutationLock&) = delete;
    StartupShortcutMutationLock& operator=(const StartupShortcutMutationLock&) = delete;

private:
    UniqueKernelHandle handle_;
    bool locked_ = false;
    DWORD error_ = ERROR_SUCCESS;
};

inline bool ValidateStartupShortcutIdentity(
    const StartupShortcutSpec& spec,
    std::wstring* error = nullptr)
{
    if (!IsLocalAbsoluteStartupPath(spec.executablePath))
    {
        SetStartupShortcutWin32Error(
            error,
            L"Validate Startup executable path",
            ERROR_BAD_PATHNAME);
        return false;
    }
    if (spec.executablePath.size() >= MAX_PATH)
    {
        SetStartupShortcutWin32Error(
            error,
            L"Validate Startup executable path length",
            ERROR_FILENAME_EXCED_RANGE);
        return false;
    }
    if (!spec.identityPath.empty() &&
        !IsLocalAbsoluteStartupPath(spec.identityPath))
    {
        SetStartupShortcutWin32Error(
            error,
            L"Validate Startup identity path",
            ERROR_BAD_PATHNAME);
        return false;
    }

    std::wstring workingDirectory = spec.workingDirectory.empty()
        ? GetDirectoryName(spec.executablePath)
        : spec.workingDirectory;
    if (!IsLocalAbsoluteStartupPath(workingDirectory))
    {
        SetStartupShortcutWin32Error(
            error,
            L"Validate Startup working-directory path",
            ERROR_BAD_PATHNAME);
        return false;
    }
    if (workingDirectory.size() >= MAX_PATH)
    {
        SetStartupShortcutWin32Error(
            error,
            L"Validate Startup working-directory path length",
            ERROR_FILENAME_EXCED_RANGE);
        return false;
    }
    return true;
}

inline bool ValidateStartupShortcutSpec(
    const StartupShortcutSpec& spec,
    std::wstring* error = nullptr)
{
    if (!ValidateStartupShortcutIdentity(spec, error))
    {
        return false;
    }

    DWORD executableAttributes = GetFileAttributesW(spec.executablePath.c_str());
    if (executableAttributes == INVALID_FILE_ATTRIBUTES ||
        (executableAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        SetStartupShortcutWin32Error(
            error,
            L"Validate Startup executable",
            executableAttributes == INVALID_FILE_ATTRIBUTES
                ? GetLastError()
                : ERROR_DIRECTORY);
        return false;
    }

    std::wstring workingDirectory = spec.workingDirectory.empty()
        ? GetDirectoryName(spec.executablePath)
        : spec.workingDirectory;
    DWORD workingAttributes = GetFileAttributesW(workingDirectory.c_str());
    if (workingAttributes == INVALID_FILE_ATTRIBUTES ||
        (workingAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        SetStartupShortcutWin32Error(
            error,
            L"Validate Startup working directory",
            workingAttributes == INVALID_FILE_ATTRIBUTES
                ? GetLastError()
                : ERROR_DIRECTORY);
        return false;
    }
    return true;
}

inline std::wstring StartupShortcutFileName(const StartupShortcutSpec& spec)
{
    // Prefer a stable product name when supplied. A renamed/replaced binary
    // using the same profile should repair one owned shortcut, not orphan a
    // second filename merely because argv[0] changed.
    std::wstring baseName = !spec.fallbackBaseName.empty()
        ? spec.fallbackBaseName
        : GetFileBaseName(spec.executablePath);
    std::wstring identity = spec.identityPath.empty()
        ? spec.executablePath
        : spec.identityPath;
    return SanitizeShortcutBaseName(baseName) + L"-" +
        StableHashHex64(NormalizeShortcutComparisonPath(identity)) + L".lnk";
}

inline bool ResolveStartupShortcutPath(
    const StartupShortcutSpec& spec,
    std::wstring& shortcutPath,
    std::wstring* error = nullptr)
{
    shortcutPath.clear();
    if (error != nullptr)
    {
        error->clear();
    }
    if (!ValidateStartupShortcutIdentity(spec, error))
    {
        return false;
    }

    PWSTR startupDirectory = nullptr;
    HRESULT result = SHGetKnownFolderPath(
        FOLDERID_Startup,
        KF_FLAG_DEFAULT,
        nullptr,
        &startupDirectory);
    if (FAILED(result) || startupDirectory == nullptr)
    {
        if (startupDirectory != nullptr)
        {
            CoTaskMemFree(startupDirectory);
        }
        SetStartupShortcutError(error, L"Resolve per-user Startup folder", result);
        return false;
    }

    shortcutPath = PathJoin(startupDirectory, StartupShortcutFileName(spec));
    CoTaskMemFree(startupDirectory);
    if (shortcutPath.size() >= MAX_PATH)
    {
        shortcutPath.clear();
        SetStartupShortcutWin32Error(
            error,
            L"Resolve Startup shortcut path length",
            ERROR_FILENAME_EXCED_RANGE);
        return false;
    }
    return true;
}

inline bool LoadStartupShortcut(
    const std::wstring& shortcutPath,
    std::wstring& targetPath,
    std::wstring& arguments,
    std::wstring& workingDirectory,
    std::wstring* error)
{
    targetPath.clear();
    arguments.clear();
    workingDirectory.clear();

    IShellLinkW* link = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_ShellLink,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&link));
    if (FAILED(result) || link == nullptr)
    {
        SetStartupShortcutError(error, L"Create ShellLink reader", result);
        return false;
    }

    IPersistFile* persist = nullptr;
    result = link->QueryInterface(IID_PPV_ARGS(&persist));
    if (SUCCEEDED(result) && persist != nullptr)
    {
        result = persist->Load(shortcutPath.c_str(), STGM_READ);
    }

    std::vector<wchar_t> target(MAX_PATH);
    std::vector<wchar_t> args(32768);
    std::vector<wchar_t> working(MAX_PATH);
    if (SUCCEEDED(result))
    {
        WIN32_FIND_DATAW findData = {};
        result = link->GetPath(
            target.data(),
            static_cast<int>(target.size()),
            &findData,
            SLGP_RAWPATH);
        if (result == S_FALSE || target[0] == L'\0')
        {
            result = HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        }
    }
    if (SUCCEEDED(result))
    {
        result = link->GetArguments(args.data(), static_cast<int>(args.size()));
    }
    if (SUCCEEDED(result))
    {
        result = link->GetWorkingDirectory(
            working.data(),
            static_cast<int>(working.size()));
    }

    if (persist != nullptr)
    {
        persist->Release();
    }
    link->Release();

    if (FAILED(result))
    {
        SetStartupShortcutError(error, L"Read Startup shortcut", result);
        return false;
    }

    targetPath.assign(target.data());
    arguments.assign(args.data());
    workingDirectory.assign(working.data());
    return true;
}

inline bool QueryStartupShortcutInstalledAtPath(
    const StartupShortcutSpec& spec,
    const std::wstring& shortcutPath,
    bool& installed,
    std::wstring* error = nullptr)
{
    installed = false;
    if (error != nullptr)
    {
        error->clear();
    }
    if (!ValidateStartupShortcutIdentity(spec, error))
    {
        return false;
    }
    if (shortcutPath.size() >= MAX_PATH)
    {
        SetStartupShortcutWin32Error(
            error,
            L"Validate Startup shortcut path length",
            ERROR_FILENAME_EXCED_RANGE);
        return false;
    }

    StartupShortcutMutationLock lock(shortcutPath);
    if (!lock.IsLocked())
    {
        SetStartupShortcutWin32Error(
            error,
            L"Lock Startup shortcut",
            lock.Error());
        return false;
    }

    DWORD attributes = GetFileAttributesW(shortcutPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        DWORD result = GetLastError();
        if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
        {
            return true;
        }
        SetStartupShortcutWin32Error(error, L"Inspect Startup shortcut", result);
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        SetStartupShortcutWin32Error(error, L"Inspect Startup shortcut", ERROR_DIRECTORY);
        return false;
    }

    StartupShortcutComScope com;
    if (!com.Ready())
    {
        SetStartupShortcutError(error, L"Initialize COM for Startup shortcut", com.Result());
        return false;
    }

    std::wstring targetPath;
    std::wstring arguments;
    std::wstring workingDirectory;
    if (!LoadStartupShortcut(
        shortcutPath,
        targetPath,
        arguments,
        workingDirectory,
        error))
    {
        return false;
    }

    std::wstring expectedWorkingDirectory = spec.workingDirectory.empty()
        ? GetDirectoryName(spec.executablePath)
        : spec.workingDirectory;
    installed =
        NormalizeShortcutComparisonPath(targetPath) ==
            NormalizeShortcutComparisonPath(spec.executablePath) &&
        arguments == spec.arguments &&
        NormalizeShortcutComparisonPath(workingDirectory) ==
            NormalizeShortcutComparisonPath(expectedWorkingDirectory);
    return true;
}

inline bool QueryStartupShortcutInstalled(
    const StartupShortcutSpec& spec,
    bool& installed,
    std::wstring* error = nullptr)
{
    std::wstring shortcutPath;
    if (!ResolveStartupShortcutPath(spec, shortcutPath, error))
    {
        installed = false;
        return false;
    }
    return QueryStartupShortcutInstalledAtPath(
        spec,
        shortcutPath,
        installed,
        error);
}

inline bool BuildStartupShortcutTemporaryPath(
    const std::wstring& shortcutPath,
    std::wstring& temporaryPath,
    std::wstring* error)
{
    GUID id = {};
    HRESULT result = CoCreateGuid(&id);
    if (FAILED(result))
    {
        SetStartupShortcutError(error, L"Create temporary Startup shortcut name", result);
        return false;
    }

    wchar_t idText[40] = {};
    if (StringFromGUID2(id, idText, ARRAYSIZE(idText)) == 0)
    {
        SetStartupShortcutWin32Error(
            error,
            L"Format temporary Startup shortcut name",
            ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    // A crash must not leave a runnable staging shortcut in shell:startup.
    // Stage beside that directory on the same volume for the atomic rename,
    // matching the managed Startup helper.
    std::wstring stagingParent = GetDirectoryName(GetDirectoryName(shortcutPath));
    if (stagingParent.empty())
    {
        SetStartupShortcutWin32Error(
            error, L"Resolve temporary Startup shortcut directory", ERROR_BAD_PATHNAME);
        return false;
    }
    temporaryPath = PathJoin(
        PathJoin(stagingParent, L".AIProjects-StartupStaging"),
        std::wstring(L".aip-") + idText + L".tmp.lnk");
    if (temporaryPath.size() >= MAX_PATH)
    {
        temporaryPath.clear();
        SetStartupShortcutWin32Error(
            error,
            L"Validate temporary Startup shortcut path length",
            ERROR_FILENAME_EXCED_RANGE);
        return false;
    }
    return true;
}

inline bool WriteStartupShortcut(
    const StartupShortcutSpec& spec,
    const std::wstring& shortcutPath,
    std::wstring* error)
{
    if (!ValidateStartupShortcutSpec(spec, error))
    {
        return false;
    }
    if (shortcutPath.size() >= MAX_PATH)
    {
        SetStartupShortcutWin32Error(
            error,
            L"Validate Startup shortcut path length",
            ERROR_FILENAME_EXCED_RANGE);
        return false;
    }
    IShellLinkW* link = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_ShellLink,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&link));
    if (FAILED(result) || link == nullptr)
    {
        SetStartupShortcutError(error, L"Create ShellLink writer", result);
        return false;
    }

    result = link->SetPath(spec.executablePath.c_str());
    if (SUCCEEDED(result))
    {
        result = link->SetArguments(spec.arguments.c_str());
    }
    std::wstring workingDirectory = spec.workingDirectory.empty()
        ? GetDirectoryName(spec.executablePath)
        : spec.workingDirectory;
    if (SUCCEEDED(result) && !workingDirectory.empty())
    {
        result = link->SetWorkingDirectory(workingDirectory.c_str());
    }
    if (SUCCEEDED(result) && !spec.description.empty())
    {
        result = link->SetDescription(spec.description.c_str());
    }

    IPersistFile* persist = nullptr;
    if (SUCCEEDED(result))
    {
        result = link->QueryInterface(IID_PPV_ARGS(&persist));
    }

    std::wstring temporaryPath;
    if (SUCCEEDED(result) &&
        !BuildStartupShortcutTemporaryPath(shortcutPath, temporaryPath, error))
    {
        result = E_FAIL;
    }
    if (!temporaryPath.empty())
    {
        std::wstring stagingDirectory = GetDirectoryName(temporaryPath);
        if (!CreateDirectoryW(stagingDirectory.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS)
        {
            result = HRESULT_FROM_WIN32(GetLastError());
        }
    }
    if (SUCCEEDED(result) && persist != nullptr)
    {
        result = persist->Save(temporaryPath.c_str(), TRUE);
    }

    if (persist != nullptr)
    {
        persist->Release();
    }
    link->Release();

    if (FAILED(result))
    {
        if (!temporaryPath.empty())
        {
            DeleteFileW(temporaryPath.c_str());
        }
        if (error == nullptr || error->empty())
        {
            SetStartupShortcutError(error, L"Write Startup shortcut", result);
        }
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    if (!MoveFileExW(
        temporaryPath.c_str(),
        shortcutPath.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DWORD moveError = GetLastError();
        DeleteFileW(temporaryPath.c_str());
        SetStartupShortcutWin32Error(error, L"Commit Startup shortcut", moveError);
        return false;
    }
    return true;
}

inline bool SetStartupShortcutInstalledAtPath(
    const StartupShortcutSpec& spec,
    const std::wstring& shortcutPath,
    bool install,
    std::wstring* error = nullptr)
{
    if (error != nullptr)
    {
        error->clear();
    }
    if (install
            ? !ValidateStartupShortcutSpec(spec, error)
            : !ValidateStartupShortcutIdentity(spec, error))
    {
        return false;
    }
    if (shortcutPath.size() >= MAX_PATH)
    {
        SetStartupShortcutWin32Error(
            error,
            L"Validate Startup shortcut path length",
            ERROR_FILENAME_EXCED_RANGE);
        return false;
    }

    StartupShortcutComScope com;
    if (!com.Ready())
    {
        SetStartupShortcutError(error, L"Initialize COM for Startup shortcut", com.Result());
        return false;
    }

    StartupShortcutMutationLock lock(shortcutPath);
    if (!lock.IsLocked())
    {
        SetStartupShortcutWin32Error(
            error,
            L"Lock Startup shortcut",
            lock.Error());
        return false;
    }

    DWORD attributes = GetFileAttributesW(shortcutPath.c_str());
    bool exists = attributes != INVALID_FILE_ATTRIBUTES;
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        DWORD result = GetLastError();
        if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
        {
            if (!install)
            {
                return true;
            }
            exists = false;
        }
        else
        {
            SetStartupShortcutWin32Error(error, L"Inspect Startup shortcut", result);
            return false;
        }
    }
    if (exists && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        SetStartupShortcutWin32Error(error, L"Inspect Startup shortcut", ERROR_DIRECTORY);
        return false;
    }

    if (install)
    {
        if (exists)
        {
            std::wstring existingTarget;
            std::wstring existingArguments;
            std::wstring existingWorkingDirectory;
            std::wstring loadError;
            if (!LoadStartupShortcut(
                    shortcutPath,
                    existingTarget,
                    existingArguments,
                    existingWorkingDirectory,
                    &loadError))
            {
                if (error != nullptr)
                {
                    *error = L"Refusing to replace a same-named Startup shortcut whose target could not be verified: " +
                        loadError;
                }
                return false;
            }
            if (NormalizeShortcutComparisonPath(existingTarget) !=
                NormalizeShortcutComparisonPath(spec.executablePath))
            {
                if (error != nullptr)
                {
                    *error = L"Refusing to replace a same-named Startup shortcut that points to another executable.";
                }
                return false;
            }
        }
        return WriteStartupShortcut(spec, shortcutPath, error);
    }

    std::wstring targetPath;
    std::wstring arguments;
    std::wstring workingDirectory;
    if (!LoadStartupShortcut(
        shortcutPath,
        targetPath,
        arguments,
        workingDirectory,
        error))
    {
        return false;
    }
    if (NormalizeShortcutComparisonPath(targetPath) !=
        NormalizeShortcutComparisonPath(spec.executablePath))
    {
        if (error != nullptr)
        {
            *error = L"Refusing to remove a same-named Startup shortcut that points to another executable.";
        }
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    if (!DeleteFileW(shortcutPath.c_str()))
    {
        SetStartupShortcutWin32Error(error, L"Remove Startup shortcut", GetLastError());
        return false;
    }
    return true;
}

inline bool SetStartupShortcutInstalled(
    const StartupShortcutSpec& spec,
    bool install,
    std::wstring* error = nullptr)
{
    std::wstring shortcutPath;
    if (!ResolveStartupShortcutPath(spec, shortcutPath, error))
    {
        return false;
    }
    return SetStartupShortcutInstalledAtPath(
        spec,
        shortcutPath,
        install,
        error);
}

inline bool SetStartupShortcutDesiredAtPath(
    const StartupShortcutSpec& spec,
    const std::wstring& shortcutPath,
    bool desired,
    bool& previouslyInstalled,
    std::wstring* error = nullptr)
{
    previouslyInstalled = false;
    if (error != nullptr)
    {
        error->clear();
    }

    // Hold one outer recursive mutex acquisition across query and mutation.
    // Query/Set acquire the same named mutex internally, so this closes the
    // cooperative race between observing the previous state and changing it.
    StartupShortcutMutationLock transactionLock(shortcutPath);
    if (!transactionLock.IsLocked())
    {
        SetStartupShortcutWin32Error(
            error,
            L"Lock Startup shortcut transaction",
            transactionLock.Error());
        return false;
    }

    if (!QueryStartupShortcutInstalledAtPath(
            spec,
            shortcutPath,
            previouslyInstalled,
            error))
    {
        return false;
    }

    // Removal is intentional even if the exact-match query is false: an owned
    // shortcut with stale arguments or working-directory metadata still needs
    // to be removed. SetStartupShortcutInstalledAtPath verifies ownership.
    if ((desired && !previouslyInstalled) || !desired)
    {
        if (!SetStartupShortcutInstalledAtPath(
                spec,
                shortcutPath,
                desired,
                error))
        {
            return false;
        }
    }
    return true;
}

inline bool SetStartupShortcutDesired(
    const StartupShortcutSpec& spec,
    bool desired,
    bool& previouslyInstalled,
    std::wstring* error = nullptr)
{
    std::wstring shortcutPath;
    if (!ResolveStartupShortcutPath(spec, shortcutPath, error))
    {
        previouslyInstalled = false;
        return false;
    }
    return SetStartupShortcutDesiredAtPath(
        spec,
        shortcutPath,
        desired,
        previouslyInstalled,
        error);
}

// Commits an INI setting batch together with its per-user shell:startup
// shortcut. The shared Startup and INI locks remain held across snapshot,
// mismatch repair, directional commit, and any rollback.
template <typename IniMutator>
inline bool CommitStartupShortcutIniState(
    const StartupShortcutSpec& spec,
    const std::wstring& iniPath,
    const std::wstring& iniHeader,
    const wchar_t* startupSection,
    const wchar_t* startupKey,
    bool startupFallback,
    bool desired,
    IniMutator&& mutateIni,
    std::wstring* error = nullptr,
    DWORD iniWriteMutexWaitMs = INFINITE)
{
    if (error != nullptr)
    {
        error->clear();
    }

    std::wstring shortcutPath;
    if (!ResolveStartupShortcutPath(spec, shortcutPath, error))
    {
        return false;
    }

    StartupShortcutMutationLock startupLock(shortcutPath);
    if (!startupLock.IsLocked())
    {
        SetStartupShortcutWin32Error(
            error,
            L"Lock Startup/INI transaction",
            startupLock.Error());
        return false;
    }

    IniWriteMutexGuard iniLock(iniPath, iniWriteMutexWaitMs);
    if (!iniLock.IsLocked())
    {
        SetStartupShortcutWin32Error(
            error,
            L"Lock Startup/INI settings",
            iniLock.Error());
        return false;
    }

    IniTextSnapshot previousIni;
    if (!CaptureIniTextSnapshot(iniPath, previousIni))
    {
        SetStartupShortcutWin32Error(
            error,
            L"Capture Startup/INI settings",
            GetLastError());
        return false;
    }

    bool previousConfigured = startupFallback;
    if (!ReadIniBooleanFromText(
            previousIni.text,
            startupSection,
            startupKey,
            startupFallback,
            previousConfigured))
    {
        if (error != nullptr)
        {
            *error = L"The existing Startup setting is not a valid boolean.";
        }
        return false;
    }

    std::wstring desiredIni = previousIni.text;
    if (desiredIni.empty() && !iniHeader.empty())
    {
        desiredIni = iniHeader;
    }
    SetLastError(ERROR_SUCCESS);
    if (!mutateIni(desiredIni))
    {
        DWORD mutationError = GetLastError();
        SetStartupShortcutWin32Error(
            error,
            L"Prepare Startup/INI settings",
            mutationError == ERROR_SUCCESS ? ERROR_INVALID_DATA : mutationError);
        return false;
    }

    bool installed = false;
    if (!QueryStartupShortcutInstalledAtPath(
            spec,
            shortcutPath,
            installed,
            error))
    {
        return false;
    }

    // Treat the INI as authoritative and repair any mismatch left by an older
    // release or external shortcut edit before choosing a rollback point.
    if (installed != previousConfigured)
    {
        bool ignoredPrevious = false;
        std::wstring reconcileError;
        if (!SetStartupShortcutDesiredAtPath(
                spec,
                shortcutPath,
                previousConfigured,
                ignoredPrevious,
                &reconcileError))
        {
            if (error != nullptr)
            {
                *error = L"Could not reconcile Startup before the INI transaction: " +
                    reconcileError;
            }
            return false;
        }
    }

    auto mutateLaunch = [&](bool state, std::wstring& mutationError)
    {
        bool ignoredPrevious = false;
        return SetStartupShortcutDesiredAtPath(
            spec,
            shortcutPath,
            state,
            ignoredPrevious,
            &mutationError);
    };
    auto persistDesired = [&](std::wstring& persistenceError)
    {
        if (WriteTextFileUtf8Bom(iniPath, desiredIni))
        {
            return true;
        }
        persistenceError = GetLastErrorText(GetLastError());
        return false;
    };
    auto restorePrevious = [&](std::wstring& restorationError)
    {
        if (RestoreIniTextSnapshot(iniPath, previousIni))
        {
            return true;
        }
        restorationError = GetLastErrorText(GetLastError());
        return false;
    };

    return ExecuteCrashConsistentLaunchConfigState(
        desired,
        previousConfigured,
        mutateLaunch,
        persistDesired,
        restorePrevious,
        error);
}
}

#endif
