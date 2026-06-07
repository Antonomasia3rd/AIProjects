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
    wchar_t buffer[4096] = {};
    DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
    std::wstring executablePath;
    if (length != 0 && length < static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])))
    {
        executablePath.assign(buffer, length);
    }
    return BuildSidecarPathsFromExecutable(executablePath, fallbackBaseName, configOverride);
}
}

#endif
