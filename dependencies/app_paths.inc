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

inline SidecarPaths BuildCurrentProcessSidecarPaths(
    const std::wstring& fallbackBaseName,
    const std::wstring& configOverride = L"",
    DefaultLogPathPolicy defaultLogPathPolicy = DefaultLogPathPolicy::BesideConfig)
{
    return BuildSidecarPathsFromExecutable(GetCurrentExecutablePath(), fallbackBaseName, configOverride, defaultLogPathPolicy);
}
}

#endif
