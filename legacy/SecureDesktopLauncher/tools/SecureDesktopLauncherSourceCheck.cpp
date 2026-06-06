#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

struct Check
{
    const char* name;
    const std::string* source;
    const char* pattern;
};

static std::string ReadSource(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error(std::string("Missing source file: ") + path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::string NormalizeSource(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    bool wasSpace = false;
    for (unsigned char ch : text)
    {
        if (ch <= ' ')
        {
            if (!wasSpace)
            {
                out.push_back(' ');
                wasSpace = true;
            }
            continue;
        }
        out.push_back(static_cast<char>(ch));
        wasSpace = false;
    }
    return out;
}

static void AssertMatches(const Check& check)
{
    std::regex re(check.pattern, std::regex_constants::ECMAScript);
    if (!std::regex_search(NormalizeSource(*check.source), re))
        throw std::runtime_error(std::string("SecureDesktopLauncher source guardrail failed: ") + check.name);
    std::cout << "ok - " << check.name << "\n";
}

int main()
{
    try
    {
        const std::string service = ReadSource("SecureDesktopLauncherService.cpp");
        const std::string password = ReadSource("SecureDesktopPasswordLauncher.cpp");
        const std::vector<Check> checks = {
            {"service requires an existing config path before loading programs", &service, R"rx(config\.configPath = FindConfigPath\(\).*FileExists\(config\.configPath\))rx"},
            {"service requires configured program path and working directory to exist", &service, R"rx(!program\.path\.empty\(\).*ExistingFilePath\(program\.path\).*\(program\.workingDirectory\.empty\(\) \|\| ExistingDirectoryPath\(program\.workingDirectory\)\).*config\.programs\.push_back\(program\))rx"},
            {"service keeps lpApplicationName pinned to configured Path even when CommandLine is configured", &service, R"rx(CreateProcessAsUserW\(\s*primaryToken,\s*program\.path\.c_str\(\),\s*commandLine\.empty\(\) \? nullptr : &commandLine\[0\])rx"},
            {"service retargets the duplicated token to the selected session before launch", &service, R"rx(SetTokenInformation\(primaryToken,\s*TokenSessionId,\s*&tokenSessionId,\s*sizeof\(tokenSessionId\)\).*CreateProcessAsUserW)rx"},
            {"service stops queueing and drains session workers before child cleanup", &service, R"rx(gStopping\.store\(true\).*WaitForSingleObject\(gEnsureWorkersDrainedEvent, INFINITE\).*StopConfiguredProcesses\(\))rx"},
            {"service launch path checks shutdown before creating a process", &service, R"rx(EnterCriticalSection\(&launchLock\).*gStopping\.load\(\).*CreateProcessAsUserW)rx"},
            {"password launcher requires config file before loading launch policy", &password, R"rx(config\.configPath = FindConfigPath\(\).*FileExists\(config\.configPath\))rx"},
            {"password launcher revalidates target existence immediately before CreateProcessW", &password, R"rx(ExistingFilePath\(state->config\.launchPath, validationError, L"Launch target"\).*ExistingDirectoryPath\(state->config\.workingDirectory, validationError, L"Launch working directory"\).*CreateProcessW)rx"},
            {"password launcher writes PBKDF2 hash and keeps legacy hash opt-in", &password, R"rx(PasswordPbkdf2HashHex.*KeepLegacySha256Hash.*keepLegacySha256Hash)rx"}
        };

        std::cout << "Running SecureDesktopLauncher source checks...\n";
        std::cout << "These checks validate launch invariants only; they do not install or start services.\n";
        for (const auto& check : checks)
            AssertMatches(check);
        std::cout << "SecureDesktopLauncher source checks passed.\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
