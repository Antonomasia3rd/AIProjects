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
            {"service keeps lpApplicationName pinned to configured Path even when CommandLine is configured", &service, R"rx(CreateProcessAsUserW\(\s*primaryToken\.Get\(\),\s*program\.path\.c_str\(\),\s*commandLine\.empty\(\) \? nullptr : &commandLine\[0\])rx"},
            {"service retargets the duplicated token to the selected session before launch", &service, R"rx(SetTokenInformation\(primaryToken\.Get\(\),\s*TokenSessionId,\s*&tokenSessionId,\s*sizeof\(tokenSessionId\)\).*CreateProcessAsUserW)rx"},
            {"service stops queueing and drains session workers before child cleanup", &service, R"rx(gStopping\.store\(true\).*WaitForEnsureWorkersToDrain\(\).*StopConfiguredProcesses\(\))rx"},
            {"service launch path checks shutdown before creating a process", &service, R"rx(CriticalSectionLock lock\(launchLock\).*gStopping\.load\(\).*CreateProcessAsUserW)rx"},
            {"service consumes the shared desktop baseline", &service, R"rx(dependencies/desktop_app_baseline\.h)rx"},
            {"service log records use the shared synchronized UTF-16 appender", &service, R"rx(aip::AppendUtf16LineToFile\()rx"},
            {"service config is loaded from one shared parsed snapshot", &service, R"rx(aip::LoadIniDocument\(config\.configPath,\s*document\))rx"},
            {"service program sections are enumerated from the parsed snapshot", &service, R"rx(for \(const aip::IniSectionData& sectionData : document\))rx"},
            {"service program sections follow case-insensitive INI semantics", &service, R"rx(aip::StartsWithI\(section,\s*prefix\.c_str\(\)\))rx"},
            {"service boolean settings use the shared strict parser", &service, R"rx(aip::ParseBoolValue\(ReadIniString)rx"},
            {"service requires a user environment block before process creation", &service, R"rx(if \(!CreateEnvironmentBlock\(&environment\.value, primaryToken\.Get\(\), FALSE\)\).*CreateProcessAsUserW)rx"},
            {"service pending states advance SCM checkpoints", &service, R"rx(dwCheckPoint =.*SERVICE_START_PENDING.*SERVICE_STOP_PENDING.*checkpoint\+\+)rx"},
            {"service status publication is serialized across SCM threads", &service, R"rx(SetServiceState\(.*lock\(gServiceStatusMutex\).*SetServiceStatus)rx"},
            {"service stop-event signalling and closure are serialized", &service, R"rx(SignalStopEvent\(\).*lock\(gStopEventMutex\).*SetEvent\(gStopEvent\).*lock\(gStopEventMutex\).*CloseHandle\(gStopEvent\))rx"},
            {"service session worker catches exceptions before completing", &service, R"rx(try \{.*EnsureProgramsForSession\(sessionId\).*catch \(const std::exception& ex\).*catch \(\.\.\.\).*CompleteEnsureWorker\(\))rx"},
            {"service session worker allocation is non-throwing", &service, R"rx(new \(std::nothrow\) DWORD\(sessionId\).*Could not allocate a session worker request)rx"},
            {"service validates worker event reset and signal operations", &service, R"rx(if \(!SetEvent\(gEnsureWorkersDrainedEvent\)\).*if \(!ResetEvent\(gEnsureWorkersDrainedEvent\)\))rx"},
            {"service drain wait verifies protected worker count", &service, R"rx(WaitForEnsureWorkersToDrain\(\).*lock\(gEnsureWorkersMutex\).*gEnsureWorkerCount == 0.*WaitForSingleObject\(gEnsureWorkersDrainedEvent, 1000\))rx"},
            {"service validates stop-event wait", &service, R"rx(stopWait = WaitForSingleObject\(gStopEvent, INFINITE\).*stopWait != WAIT_OBJECT_0)rx"},
            {"service process-launch lock is exception safe", &service, R"rx(InitOnceExecuteOnce\(.*CriticalSectionLock lock\(launchLock\))rx"},
            {"service token and process handles use shared RAII ownership", &service, R"rx(UniqueKernelHandle selfToken.*UniqueKernelHandle primaryToken.*UniqueKernelHandle process.*UniqueKernelHandle thread)rx"},
            {"service environment block uses scope cleanup", &service, R"rx(struct EnvironmentBlockGuard.*DestroyEnvironmentBlock\(value\).*CreateEnvironmentBlock\(&environment\.value)rx"},
            {"service launched-process tracking contains allocation failures", &service, R"rx(RecordLaunchedProcess\(.*noexcept.*gLaunchedProcesses\.push_back\(record\).*catch \(const std::exception& ex\))rx"},
            {"service checks launch-spacing waits", &service, R"rx(wait = WaitForSingleObject\(gStopEvent, program\.launchSpacingMs\).*wait == WAIT_FAILED)rx"},
            {"service callback exception reporting cannot rethrow", &service, R"rx(LogServiceWarningNoThrow\(.*noexcept.*LogServiceExceptionNoThrow\(.*noexcept)rx"},
            {"service control handler contains C++ exceptions", &service, R"rx(ServiceHandlerEx\(.*try \{.*catch \(const std::exception& ex\).*catch \(\.\.\.\).*ERROR_UNHANDLED_EXCEPTION)rx"},
            {"service main loop and shutdown cleanup contain C++ exceptions", &service, R"rx(ServiceMain\(.*try \{.*ReconcileSessions\(\).*catch \(const std::exception& ex\).*WaitForEnsureWorkersToDrain\(\).*StopConfiguredProcesses\(\).*catch \(const std::exception& ex\))rx"},
            {"service validates stop-event signalling", &service, R"rx(if \(!SignalStopEvent\(\))rx"},
            {"password launcher requires config file before loading launch policy", &password, R"rx(config\.configPath = FindConfigPath\(\).*FileExists\(config\.configPath\))rx"},
            {"password launcher revalidates target existence immediately before CreateProcessW", &password, R"rx(ExistingFilePath\(state->config\.launchPath, validationError, L"Launch target"\).*ExistingDirectoryPath\(state->config\.workingDirectory, validationError, L"Launch working directory"\).*CreateProcessW)rx"},
            {"password launcher writes PBKDF2 hash and keeps legacy hash opt-in", &password, R"rx(PasswordPbkdf2HashHex.*KeepLegacySha256Hash.*keepLegacySha256Hash)rx"},
            {"password launcher saves configuration atomically through the shared store", &password, R"rx(aip::IniConfigStore\(config\.configPath.*MutateFresh)rx"},
            {"password launcher checks target thread resume", &password, R"rx(if \(ResumeThread\(pi\.hThread\) == static_cast<DWORD>\(-1\)\))rx"},
            {"password launcher requires target wait registration", &password, R"rx(if \(!RegisterWaitForSingleObject\(.*WT_EXECUTEONLYONCE\)\))rx"},
            {"password launcher rolls back a registered wait if process tracking allocation fails", &password, R"rx(state->processes\.push_back\(launched\).*catch \(\.\.\.\).*UnregisterWaitEx\(launched\.wait, INVALID_HANDLE_VALUE\).*TerminateProcess\(pi\.hProcess, 1\))rx"},
            {"password launcher checks process-exit message delivery", &password, R"rx(!PostMessageW\(hwnd, kProcessExitedMessage)rx"},
            {"password launcher prunes tracked processes without allocation and preserves wait failures", &password, R"rx(while \(process != state->processes\.end\(\)\).*wait == WAIT_FAILED.*\+\+process.*state->processes\.erase\(process\))rx"},
            {"password launcher handles message-loop failures", &password, R"rx(GetMessageW\(.*if \(result == -1\))rx"},
            {"password log records use the shared synchronized UTF-16 appender", &password, R"rx(aip::AppendUtf16LineToFile\()rx"}
        };

        std::cout << "Running SecureDesktopLauncher source checks...\n";
        std::cout << "These checks validate launch invariants only; they do not install or start services.\n";
        for (const auto& check : checks)
            AssertMatches(check);
        if (password.find("WritePrivateProfileStringW") != std::string::npos)
            throw std::runtime_error("SecureDesktopLauncher source guardrail failed: password writes bypass the shared atomic INI store");
        std::cout << "ok - password writes do not bypass the shared atomic INI store\n";
        if (service.find("GetPrivateProfileSectionNamesW") != std::string::npos)
            throw std::runtime_error("SecureDesktopLauncher source guardrail failed: service section enumeration bypasses the shared UTF-aware parser");
        std::cout << "ok - service section enumeration does not use legacy profile APIs\n";
        if (service.find("WriteFile(file, timestamp") != std::string::npos ||
            password.find("WriteFile(file, timestamp") != std::string::npos)
            throw std::runtime_error("SecureDesktopLauncher source guardrail failed: log records bypass the synchronized shared appender");
        std::cout << "ok - log records do not use unchecked multi-write appends\n";
        std::cout << "SecureDesktopLauncher source checks passed.\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
