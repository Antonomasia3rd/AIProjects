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
        const std::string serviceWrapper = ReadSource("SecureDesktopLauncherService.cpp");
        const std::string passwordWrapper = ReadSource("SecureDesktopPasswordLauncher.cpp");
        const std::string service = ReadSource("../../dependencies/SecureDesktopLauncher/service_app.inc");
        const std::string password = ReadSource("../../dependencies/SecureDesktopLauncher/password_app.inc");
        const std::string trust = ReadSource("../../dependencies/privileged_path_trust.h");
        const std::vector<Check> checks = {
            {"service project entry point is a dependency overlay", &serviceWrapper, R"rx(#include "\.\./\.\./dependencies/SecureDesktopLauncher/service_app\.inc")rx"},
            {"password project entry point is a dependency overlay", &passwordWrapper, R"rx(#include "\.\./\.\./dependencies/SecureDesktopLauncher/password_app\.inc")rx"},
            {"service pins its own executable before resolving and pinning the sidecar config", &service, R"rx(OpenPinnedProtectedFile\( CurrentExePath\(\), aip::PrivilegedRootPolicy::ProgramFilesOnly, L"service executable", serviceExecutable, validationError\).*FindConfigPath\(serviceExecutable\.finalPath\).*OpenPinnedProtectedFile\( config\.configPath, aip::PrivilegedRootPolicy::ProgramFilesOnly, L"service configuration", configFile, validationError\).*config\.configPath = configFile\.finalPath)rx"},
            {"service canonicalizes protected target and working-directory paths before retaining a program", &service, R"rx(OpenPinnedProtectedFile\( program\.path, aip::PrivilegedRootPolicy::WindowsOrProgramFiles, L"configured LocalSystem launch target", targetFile, validationError\).*OpenPinnedProtectedDirectory\( program\.workingDirectory, aip::PrivilegedRootPolicy::WindowsOrProgramFiles, L"configured LocalSystem working directory", workingDirectory, validationError\).*program\.path = targetFile\.finalPath.*program\.workingDirectory = workingDirectory\.finalPath.*config\.programs\.push_back\(program\))rx"},
            {"service re-pins the protected target immediately before keeping lpApplicationName canonical", &service, R"rx(LaunchProgramOnDesktop\(.*OpenPinnedProtectedFile\( program\.path, aip::PrivilegedRootPolicy::WindowsOrProgramFiles, L"LocalSystem launch target", targetFile, validationError\).*verifiedProgram\.path = targetFile\.finalPath.*CreateProcessAsUserW\( primaryToken\.Get\(\), verifiedProgram\.path\.c_str\(\), commandLine\.empty\(\) \? nullptr : &commandLine\[0\])rx"},
            {"privileged path policy is limited to canonical Windows and Program Files roots", &trust, R"rx(TrustedPrivilegedRoots\( PrivilegedRootPolicy policy\).*FOLDERID_ProgramFiles.*FOLDERID_ProgramFilesX86.*FOLDERID_ProgramFilesX64.*GetWindowsDirectoryW\(.*FindTrustedRootForPath\( const std::wstring& path, PrivilegedRootPolicy policy)rx"},
            {"privileged path policy inspects ownership and writers through open handles", &trust, R"rx(GetSecurityInfo\( handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION \| DACL_SECURITY_INFORMATION.*IsTrustedPrivilegedWriterSid\(owner\).*HasDangerousWriteAccess\(mask, dangerous\).*IsTrustedPrivilegedWriterSid\(writer\))rx"},
            {"privileged path policy rejects alternate streams and opens every component without following reparse points", &trust, R"rx(HasAlternateDataStream\(.*FILE_FLAG_OPEN_REPARSE_POINT.*FILE_ATTRIBUTE_REPARSE_POINT)rx"},
            {"privileged path policy bounds SIDs inside each ACE", &trust, R"rx(GetSidLengthRequired\(sidBytes\[1\]\).*required > header->AceSize - sidOffset.*IsValidSid\(sid\))rx"},
            {"privileged path pins retain every component and deny write/delete sharing", &trust, R"rx(std::wstring current = fullPath\.substr\(0, 3\).*CreateFileW\( current\.c_str\(\), access, FILE_SHARE_READ,.*FinalPathForHandle\(pin\.Get\(\), componentFinalPath\).*result\.handles\.push_back\(std::move\(pin\)\))rx"},
            {"protected directories reject untrusted sibling creation below the trusted root", &trust, R"rx(if \(IsPathAtOrBelow\(current, trustedRoot\)\).*dangerous \|= FILE_ADD_FILE \| FILE_ADD_SUBDIRECTORY)rx"},
            {"unsafe executable or config paths stop the service before it reaches running state", &service, R"rx(ServiceMain\(.*OpenPinnedProtectedFile\( CurrentExePath\(\), aip::PrivilegedRootPolicy::ProgramFilesOnly, L"service executable".*IsLocalSystemProcess\(GetCurrentProcessId\(\)\).*OpenPinnedProtectedFile\( configPath, aip::PrivilegedRootPolicy::ProgramFilesOnly, L"service configuration".*SetServiceState\(SERVICE_STOPPED, ERROR_ACCESS_DENIED\).*SetServiceState\(SERVICE_RUNNING\))rx"},
            {"service validates every enabled program before reporting running", &service, R"rx(bool valid = false.*if \(!program\.enabled\).*continue.*invalidProgramSection = true.*config\.valid = !invalidProgramSection.*AppConfig initialConfig = LoadConfig\(\).*if \(!initialConfig\.valid\).*SetServiceState\(SERVICE_STOPPED, ERROR_INVALID_DATA\).*SetServiceState\(SERVICE_RUNNING\))rx"},
            {"service installation refuses unsafe executable or config paths before SCM mutation", &service, R"rx(InstallService\(\).*OpenPinnedProtectedFile\( CurrentExePath\(\), aip::PrivilegedRootPolicy::ProgramFilesOnly, L"service executable".*OpenPinnedProtectedFile\( configPath, aip::PrivilegedRootPolicy::ProgramFilesOnly, L"service configuration".*OpenSCManagerW.*CreateServiceW)rx"},
            {"service installation and updates explicitly select LocalSystem", &service, R"rx(CreateServiceW\(.*L"LocalSystem".*ChangeServiceConfigW\(.*L"LocalSystem")rx"},
            {"existing service updates request SCM connect rights and propagate core configuration failures", &service, R"rx(SC_MANAGER_CREATE_SERVICE \| SC_MANAGER_CONNECT.*configured = ChangeServiceConfigW\(.*configurationError = GetLastError\(\).*const bool success = service != nullptr && configured.*return success \? 0 : 1)rx"},
            {"service description failures are reported without lying about an already completed install", &service, R"rx(if \(!ChangeServiceConfig2W\(.*service installed/updated, but its description could not be set)rx"},
            {"LocalSystem diagnostics use the Windows Event Log instead of a sidecar path", &service, R"rx(RegisterEventSourceW\(nullptr, kServiceName\).*ReportEventW\(.*DeregisterEventSource\(eventSource\))rx"},
            {"service retargets the duplicated token to the selected session before launch", &service, R"rx(SetTokenInformation\(primaryToken\.Get\(\),\s*TokenSessionId,\s*&tokenSessionId,\s*sizeof\(tokenSessionId\)\).*CreateProcessAsUserW)rx"},
            {"service stops queueing and drains session workers before child cleanup", &service, R"rx(gStopping\.store\(true\).*WaitForEnsureWorkersToDrain\(\).*StopConfiguredProcesses\(\))rx"},
            {"service launch path checks shutdown before creating a process", &service, R"rx(CriticalSectionLock lock\(launchLock\).*gStopping\.load\(\).*CreateProcessAsUserW)rx"},
            {"service consumes the shared desktop baseline", &service, R"rx(#include "\.\./desktop_app_baseline\.h")rx"},
            {"password launcher consumes the shared desktop baseline", &password, R"rx(#include "\.\./desktop_app_baseline\.h")rx"},
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
            {"service prunes exited tracked children using creation-time identity", &service, R"rx(TrackedProcessIsStillRunning\(.*WaitForSingleObject\(process\.Get\(\), 0\) != WAIT_TIMEOUT.*FileTimeEquals\(creationTime, record\.creationTime\).*gLaunchedProcesses\.erase\(existing\))rx"},
            {"service does not strand cleanup-enabled children after a tracking failure", &service, R"rx(verifiedProgram\.stopOnServiceStop && !RecordLaunchedProcess\(verifiedProgram, pi\).*TerminateProcess\(pi\.hProcess, ERROR_GEN_FAILURE\).*ok = FALSE)rx"},
            {"service shutdown uses the effective stop policy captured with each launched process", &service, R"rx(record\.stopOnServiceStop = program\.stopOnServiceStop.*program\.stopOnServiceStop = ReadIniBool\( document, section, L"StopOnServiceStop", config\.stopOnServiceStop\).*StopConfiguredProcesses\(\).*if \(!target\.stopOnServiceStop \|\|)rx"},
            {"service child cleanup enforces one deadline across scan, terminate, and wait", &service, R"rx(kChildShutdownDeadlineMs = 10000.*const ULONGLONG started = GetTickCount64\(\).*targets\.swap\(gLaunchedProcesses\).*GetTickCount64\(\) - started >= kChildShutdownDeadlineMs.*terminatedProcesses\.push_back\(std::move\(process\)\).*Timed out while validating, terminating, or waiting)rx"},
            {"service checks launch-spacing waits", &service, R"rx(wait = WaitForSingleObject\(gStopEvent, verifiedProgram\.launchSpacingMs\).*wait == WAIT_FAILED)rx"},
            {"service callback exception reporting cannot rethrow", &service, R"rx(LogServiceWarningNoThrow\(.*noexcept.*LogServiceExceptionNoThrow\(.*noexcept)rx"},
            {"service status and stop signalling contain synchronization exceptions", &service, R"rx(SetServiceState\(.*noexcept.*publishing service status threw an exception.*SignalStopEvent\(\) noexcept.*signalling the stop event threw an exception)rx"},
            {"service control handler contains C++ exceptions", &service, R"rx(ServiceHandlerEx\(.*try \{.*catch \(const std::exception& ex\).*catch \(\.\.\.\).*ERROR_UNHANDLED_EXCEPTION)rx"},
            {"service initialization, main loop, and shutdown cleanup contain C++ exceptions", &service, R"rx(ServiceMain\(.*OpenPinnedProtectedFile\(.*catch \(const std::exception& ex\).*ReconcileSessions\(\).*catch \(const std::exception& ex\).*WaitForEnsureWorkersToDrain\(\).*StopConfiguredProcesses\(\).*catch \(const std::exception& ex\))rx"},
            {"service validates stop-event signalling", &service, R"rx(if \(!SignalStopEvent\(\))rx"},
            {"password launcher requires and pins a protected config before loading launch policy", &password, R"rx(config\.configPath = FindConfigPath\(executablePath\).*enforceValidation && !configExists.*OpenPinnedProtectedFile\( config\.configPath, aip::PrivilegedRootPolicy::ProgramFilesOnly, L"password launcher configuration", configFile, validationError\).*config\.configPath = configFile\.finalPath)rx"},
            {"password launcher re-pins protected target and working directory through suspended creation", &password, R"rx(LaunchConfiguredTarget\(.*OpenPinnedProtectedFile\( state->config\.launchPath, aip::PrivilegedRootPolicy::WindowsOrProgramFiles, L"password-gated LocalSystem launch target".*OpenPinnedProtectedDirectory\( state->config\.workingDirectory, aip::PrivilegedRootPolicy::WindowsOrProgramFiles, L"password-gated LocalSystem working directory".*DWORD creationFlags = .*CREATE_SUSPENDED.*CreateProcessW\( verifiedTarget\.c_str\(.*AssignProcessToJobObject.*ResumeThread)rx"},
            {"password launcher writes PBKDF2 hash and keeps legacy hash opt-in", &password, R"rx(PasswordPbkdf2HashHex.*KeepLegacySha256Hash.*keepLegacySha256Hash)rx"},
            {"password launcher saves configuration atomically while retaining a protected parent pin", &password, R"rx(MutateProtectedConfig\(.*OpenPinnedProtectedDirectory\( DirectoryOf\(configPath\), aip::PrivilegedRootPolicy::ProgramFilesOnly.*IniConfigStore\(effectivePath, L"", 5000\)\.MutateFresh.*OpenPinnedProtectedFile\( effectivePath, aip::PrivilegedRootPolicy::ProgramFilesOnly, L"rewritten password launcher configuration")rx"},
            {"password launcher checks target thread resume", &password, R"rx(if \(ResumeThread\(pi\.hThread\) == static_cast<DWORD>\(-1\)\))rx"},
            {"password launcher requires target wait registration", &password, R"rx(if \(!RegisterWaitForSingleObject\(.*WT_EXECUTEONLYONCE\)\))rx"},
            {"password launcher rolls back a registered wait if process tracking allocation fails", &password, R"rx(state->processes\.push_back\(launched\).*catch \(\.\.\.\).*UnregisterWaitEx\(launched\.wait, INVALID_HANDLE_VALUE\).*TerminateProcess\(pi\.hProcess, 1\))rx"},
            {"password launcher checks process-exit message delivery", &password, R"rx(!PostMessageW\(hwnd, kProcessExitedMessage)rx"},
            {"password launcher prunes tracked processes without allocation and preserves wait failures", &password, R"rx(while \(process != state->processes\.end\(\)\).*wait == WAIT_FAILED.*\+\+process.*state->processes\.erase\(process\))rx"},
            {"password launcher handles message-loop failures", &password, R"rx(GetMessageW\(.*if \(result == -1\))rx"},
            {"password launcher keeps help/version side-effect free but pins its image before privileged commands", &password, R"rx(wWinMain\(.*CommandLineToArgvW.*--help.*--version.*return 0.*OpenPinnedProtectedFile\( CurrentExePath\(\), aip::PrivilegedRootPolicy::ProgramFilesOnly, L"password launcher executable".*if \(setPasswordCommand\).*SetPassword\(launcherExecutable\.finalPath\))rx"},
            {"password attempt budgets and lockouts survive restarts and parallel launchers", &password, R"rx(ReservePasswordAttempt\(.*MutateProtectedConfig\( config\.configPath, true.*PasswordAttemptCount.*LockoutUntilUtcFileTime.*GrantedAndLockoutArmed.*WaitForPasswordLockout\(waitUntil\).*ResetPasswordAttemptState\(config, resetError\))rx"},
            {"password state failures deny access", &password, R"rx(The protected password-attempt state could not be updated\. Access is denied\..*The password was correct, but the protected attempt state could not be reset\. Access is denied\.)rx"},
            {"password reset preserves ShowWindow", &password, R"rx(L"Launch", L"ShowWindow", std::to_wstring\(config\.showWindow\))rx"},
            {"service test is a dry validation alias and unknown commands cannot enter the dispatcher", &service, R"rx(ValidateServiceConfiguration\(\).*L"validate".*L"test".*return ValidateServiceConfiguration\(\).*Unknown SecureDesktopLauncher command.*return 2.*StartServiceCtrlDispatcherW)rx"},
            {"service help and version are side-effect free", &service, R"rx(wmain\(.*--help.*ServiceCommandLineHelp\(\).*return 0.*--version.*ReleaseVersionDisplayText\(\).*return 0.*InstallService\(\))rx"},
            {"password launcher diagnostics use Event Log and debug output without a sidecar", &password, R"rx(OutputDebugStringW\(debugLine\.c_str\(\)\).*RegisterEventSourceW\( nullptr, L"SecureDesktopPasswordLauncher"\).*ReportEventW\(.*DeregisterEventSource\(eventSource\))rx"}
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
            password.find("WriteFile(file, timestamp") != std::string::npos ||
            service.find("AppendUtf16LineToFile") != std::string::npos ||
            password.find("AppendUtf16LineToFile") != std::string::npos)
            throw std::runtime_error("SecureDesktopLauncher source guardrail failed: log records bypass the synchronized shared appender");
        std::cout << "ok - privileged launchers do not append sidecar logs\n";
        const std::string forbiddenAclApi1 = std::string("SetNamed") + "SecurityInfoW";
        const std::string forbiddenAclApi2 = std::string("Set") + "SecurityInfo(";
        const std::string forbiddenAclApi3 = std::string("Set") + "File" + "SecurityW";
        if (service.find(forbiddenAclApi1) != std::string::npos ||
            service.find(forbiddenAclApi2) != std::string::npos ||
            service.find(forbiddenAclApi3) != std::string::npos ||
            trust.find(forbiddenAclApi1) != std::string::npos ||
            trust.find(forbiddenAclApi2) != std::string::npos ||
            trust.find(forbiddenAclApi3) != std::string::npos)
            throw std::runtime_error("SecureDesktopLauncher source guardrail failed: protected-path policy mutates ACLs or ownership");
        std::cout << "ok - protected-path policy refuses unsafe state without mutating ACLs or ownership\n";
        std::cout << "SecureDesktopLauncher source checks passed.\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
