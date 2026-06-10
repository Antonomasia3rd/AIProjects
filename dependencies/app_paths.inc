#ifndef AIP_DEPENDENCIES_APP_PATHS_INC
#define AIP_DEPENDENCIES_APP_PATHS_INC

namespace aip
{
struct SidecarPaths
{
    std::wstring exePath;
    std::wstring exeDir;
    std::wstring exeBaseName;
    std::wstring configPath;
    std::wstring defaultLogPath;
};

enum class DefaultLogPathPolicy
{
    BesideConfig,
    BesideExecutable
};

inline std::wstring GetCurrentExecutablePath()
{
    std::vector<wchar_t> buffer(4096);
    for (;;)
    {
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return L"";
        }

        if (length < buffer.size() - 1)
        {
            return std::wstring(buffer.data(), length);
        }

        if (buffer.size() >= 32768)
        {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(buffer.size() * 2);
    }
}

// unchecked path builder: callers that accept user-provided config paths should
// use TryBuildSidecarPathsFromExecutable/TryResolveConfigFilePath instead so
// directory and trailing-separator mistakes fail before write-time.
inline SidecarPaths BuildSidecarPathsFromExecutable(
    const std::wstring& executablePath,
    const std::wstring& fallbackBaseName,
    const std::wstring& configOverride = L"",
    DefaultLogPathPolicy defaultLogPathPolicy = DefaultLogPathPolicy::BesideConfig)
{
    SidecarPaths paths;
    paths.exePath = executablePath.empty() ? fallbackBaseName : executablePath;
    paths.exeDir = GetDirectoryName(paths.exePath);
    paths.exeBaseName = GetFileBaseName(paths.exePath);
    if (paths.exeBaseName.empty())
    {
        paths.exeBaseName = fallbackBaseName;
    }

    if (!configOverride.empty())
    {
        paths.configPath = MakeAbsolutePath(configOverride);
        std::wstring configDir = GetDirectoryName(paths.configPath);
        std::wstring configBase = GetFileBaseName(paths.configPath);
        if (configBase.empty())
        {
            configBase = paths.exeBaseName;
        }
        paths.defaultLogPath = defaultLogPathPolicy == DefaultLogPathPolicy::BesideExecutable
            ? PathJoin(paths.exeDir, paths.exeBaseName + L".log")
            : PathJoin(configDir, configBase + L".log");
    }
    else
    {
        paths.configPath = PathJoin(paths.exeDir, paths.exeBaseName + L".ini");
        paths.defaultLogPath = PathJoin(paths.exeDir, paths.exeBaseName + L".log");
    }

    return paths;
}


inline std::wstring BuildExecutableSidecarLogPath(const SidecarPaths& paths)
{
    std::wstring baseName = paths.exeBaseName.empty() ? L"Application" : paths.exeBaseName;
    return PathJoin(paths.exeDir, baseName + L".log");
}

inline std::wstring BuildExecutableSidecarLogPath(
    const std::wstring& executablePath,
    const std::wstring& fallbackBaseName)
{
    SidecarPaths paths = BuildSidecarPathsFromExecutable(executablePath, fallbackBaseName);
    return BuildExecutableSidecarLogPath(paths);
}



inline bool ConfigFileNameContainsColon(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t start = slash == std::wstring::npos ? 0 : slash + 1;
    return path.find(L':', start) != std::wstring::npos;
}

inline std::wstring ConfigFileNamePart(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t start = slash == std::wstring::npos ? 0 : slash + 1;
    return path.substr(start);
}

inline bool ConfigFileNameHasInvalidCharacters(const std::wstring& path)
{
    std::wstring name = ConfigFileNamePart(path);
    if (name.empty())
    {
        return true;
    }
    if (!name.empty() && (name.back() == L' ' || name.back() == L'.'))
    {
        return true;
    }

    for (wchar_t ch : name)
    {
        if (ch < 0x20 || ch == L'<' || ch == L'>' || ch == L'"' ||
            ch == L'|' || ch == L'?' || ch == L'*')
        {
            return true;
        }
    }
    return false;
}

inline bool IsReservedWindowsDeviceBaseName(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t start = slash == std::wstring::npos ? 0 : slash + 1;
    std::wstring name = path.substr(start);
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.'))
    {
        name.pop_back();
    }

    size_t dot = name.find(L'.');
    std::wstring base = dot == std::wstring::npos ? name : name.substr(0, dot);
    base = ToLower(base);
    if (base == L"con" || base == L"prn" || base == L"aux" || base == L"nul" ||
        base == L"conin$" || base == L"conout$")
    {
        return true;
    }

    if (base.size() == 4 && (base.rfind(L"com", 0) == 0 || base.rfind(L"lpt", 0) == 0))
    {
        return base[3] >= L'1' && base[3] <= L'9';
    }
    return false;
}

inline bool TryValidateConfigFilePath(const std::wstring& path, std::wstring* error = nullptr)
{
    if (path.empty())
    {
        if (error)
        {
            *error = L"path is empty";
        }
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    wchar_t last = path.back();
    if (last == L'\\' || last == L'/')
    {
        if (error)
        {
            *error = L"path refers to a directory, not an INI file";
        }
        SetLastError(ERROR_DIRECTORY);
        return false;
    }

    if (ConfigFileNameContainsColon(path))
    {
        if (error)
        {
            *error = L"path uses an alternate data stream or device-style filename, not an INI file";
        }
        SetLastError(ERROR_INVALID_NAME);
        return false;
    }

    if (ConfigFileNameHasInvalidCharacters(path))
    {
        if (error)
        {
            *error = L"path filename contains invalid Windows filename characters, control characters, or trailing space/dot";
        }
        SetLastError(ERROR_INVALID_NAME);
        return false;
    }

    if (IsReservedWindowsDeviceBaseName(path))
    {
        if (error)
        {
            *error = L"path uses a reserved Windows device name, not an INI file";
        }
        SetLastError(ERROR_INVALID_NAME);
        return false;
    }

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        if (error)
        {
            *error = L"path refers to a directory, not an INI file";
        }
        SetLastError(ERROR_DIRECTORY);
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    return true;
}

inline bool TryResolveConfigFilePath(const std::wstring& path, std::wstring& absolutePath, std::wstring* error = nullptr)
{
    absolutePath.clear();

    // Validate the caller-provided spelling before normalizing it. On Windows,
    // GetFullPathNameW can normalize away trailing spaces/dots, so checking only
    // the resolved path would accidentally accept inputs such as "settings.ini."
    // or "settings.ini ".
    if (!TryValidateConfigFilePath(path, error))
    {
        return false;
    }

    if (!TryMakeAbsolutePath(path, absolutePath, error))
    {
        return false;
    }

    if (!TryValidateConfigFilePath(absolutePath, error))
    {
        return false;
    }

    return true;
}


inline bool TryBuildSidecarPathsFromExecutable(
    const std::wstring& executablePath,
    const std::wstring& fallbackBaseName,
    const std::wstring& configOverride,
    SidecarPaths& paths,
    std::wstring* error = nullptr,
    DefaultLogPathPolicy defaultLogPathPolicy = DefaultLogPathPolicy::BesideConfig)
{
    paths = {};
    if (configOverride.empty())
    {
        paths = BuildSidecarPathsFromExecutable(executablePath, fallbackBaseName);
        return true;
    }

    std::wstring absoluteConfig;
    if (!TryResolveConfigFilePath(configOverride, absoluteConfig, error))
    {
        return false;
    }

    paths = BuildSidecarPathsFromExecutable(executablePath, fallbackBaseName, absoluteConfig, defaultLogPathPolicy);
    paths.configPath = absoluteConfig;
    return true;
}

inline bool TryBuildSidecarPathsFromExecutable(
    const std::wstring& executablePath,
    const std::wstring& fallbackBaseName,
    const std::wstring& configOverride,
    SidecarPaths& paths,
    DefaultLogPathPolicy defaultLogPathPolicy,
    std::wstring* error = nullptr)
{
    return TryBuildSidecarPathsFromExecutable(
        executablePath,
        fallbackBaseName,
        configOverride,
        paths,
        error,
        defaultLogPathPolicy);
}


inline bool TryBuildCurrentProcessSidecarPaths(
    const std::wstring& fallbackBaseName,
    const std::wstring& configOverride,
    SidecarPaths& paths,
    std::wstring* error = nullptr,
    DefaultLogPathPolicy defaultLogPathPolicy = DefaultLogPathPolicy::BesideConfig)
{
    return TryBuildSidecarPathsFromExecutable(
        GetCurrentExecutablePath(),
        fallbackBaseName,
        configOverride,
        paths,
        error,
        defaultLogPathPolicy);
}

inline bool TryBuildCurrentProcessSidecarPaths(
    const std::wstring& fallbackBaseName,
    const std::wstring& configOverride,
    SidecarPaths& paths,
    DefaultLogPathPolicy defaultLogPathPolicy,
    std::wstring* error = nullptr)
{
    return TryBuildCurrentProcessSidecarPaths(
        fallbackBaseName,
        configOverride,
        paths,
        error,
        defaultLogPathPolicy);
}

inline SidecarPaths BuildCurrentProcessSidecarPaths(
    const std::wstring& fallbackBaseName,
    const std::wstring& configOverride = L"",
    DefaultLogPathPolicy defaultLogPathPolicy = DefaultLogPathPolicy::BesideConfig)
{
    return BuildSidecarPathsFromExecutable(GetCurrentExecutablePath(), fallbackBaseName, configOverride, defaultLogPathPolicy);
}
}

#endif
