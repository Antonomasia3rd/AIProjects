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
    if (!TryMakeAbsolutePath(configOverride, absoluteConfig, error))
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
