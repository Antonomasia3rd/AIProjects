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
    const std::wstring& configOverride = L"")
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
        paths.defaultLogPath = PathJoin(configDir, configBase + L".log");
    }
    else
    {
        paths.configPath = PathJoin(paths.exeDir, paths.exeBaseName + L".ini");
        paths.defaultLogPath = PathJoin(paths.exeDir, paths.exeBaseName + L".log");
    }

    return paths;
}

inline SidecarPaths BuildCurrentProcessSidecarPaths(
    const std::wstring& fallbackBaseName,
    const std::wstring& configOverride = L"")
{
    return BuildSidecarPathsFromExecutable(GetCurrentExecutablePath(), fallbackBaseName, configOverride);
}
}

#endif
