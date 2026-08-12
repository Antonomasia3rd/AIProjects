// -----------------------------------------------------------------------------
// PowerShell execution
// -----------------------------------------------------------------------------
static void MarkRegistrationUnresolved(const std::wstring& reason, HANDLE processHandle, DWORD processId);

// Base64EncodeBytes / PowerShellEncodedCommand / PowerShellUtf8Preamble /
// DefaultPowerShellExe / IsLocalAbsoluteFilePath / ResolveConfiguredPowerShellExe
// now live in the shared dependencies/powershell_runner.inc (aip::), exposed
// unqualified here via the `using aip::...;` declarations in ga_core.inc.
// RssLiveTile shares the same implementations instead of maintaining its own
// independent copies.

static std::wstring ReadPowerShellExe()
{
    bool usedInvalidOverride = false;
    std::wstring resolved = aip::ResolveConfiguredPowerShellExe(IniReadS(L"Settings", L"PowerShellExe", L""), usedInvalidOverride);
    if (usedInvalidOverride)
        Log(g_ui.powerShellExeInvalidPath.c_str(), resolved.c_str());

    return resolved;
}

static bool IsAsciiHexDigit(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9') ||
        (ch >= L'a' && ch <= L'f') ||
        (ch >= L'A' && ch <= L'F');
}

static bool IsErrorCodeBoundary(const std::wstring& text, size_t pos)
{
    if (pos >= text.size())
        return true;

    wchar_t ch = text[pos];
    return !IsAsciiHexDigit(ch) && ch != L'x' && ch != L'X';
}

static bool HasErrorCodeBoundaryBefore(const std::wstring& text, size_t pos)
{
    return pos == 0 || IsErrorCodeBoundary(text, pos - 1);
}

static bool ContainsPowerShellErrorCode(const std::wstring& output, const std::wstring& codeWithPrefix, const std::wstring& codeWithoutPrefix)
{
    std::wstring haystack = ToLowerCopy(output);
    std::wstring prefixed = ToLowerCopy(codeWithPrefix);
    std::wstring bare = ToLowerCopy(codeWithoutPrefix);

    size_t pos = 0;
    while ((pos = haystack.find(prefixed, pos)) != std::wstring::npos)
    {
        if (HasErrorCodeBoundaryBefore(haystack, pos) &&
            IsErrorCodeBoundary(haystack, pos + prefixed.size()))
        {
            return true;
        }
        ++pos;
    }

    pos = 0;
    while ((pos = haystack.find(bare, pos)) != std::wstring::npos)
    {
        if (HasErrorCodeBoundaryBefore(haystack, pos) &&
            IsErrorCodeBoundary(haystack, pos + bare.size()))
        {
            return true;
        }
        ++pos;
    }

    return false;
}

static bool NormalizePowerShellErrorCodeKey(const std::wstring& raw, std::wstring& withPrefix, std::wstring& withoutPrefix)
{
    std::wstring code = TrimCopy(raw);
    if (code.size() >= 2 && code[0] == L'0' && (code[1] == L'x' || code[1] == L'X'))
        code.erase(0, 2);
    if (code.empty() || code.size() > 8)
        return false;

    errno = 0;
    wchar_t* end = nullptr;
    unsigned long parsed = wcstoul(code.c_str(), &end, 16);
    if (end == code.c_str() || errno == ERANGE)
        return false;
    while (end && *end && iswspace(*end) != 0)
        ++end;
    if (end && *end)
        return false;

    wchar_t buf[16];
    swprintf(buf, _countof(buf), L"%08lX", parsed);
    withoutPrefix = buf;
    withPrefix = L"0x" + withoutPrefix;
    return true;
}

static bool LookupConfiguredPowerShellErrorMessage(const std::wstring& output, std::wstring& message)
{
    std::vector<IniEntry> entries;
    bool hasConfiguredSection = ReadIniSectionCached(POWERSHELL_ERROR_MESSAGES_SECTION, entries);
    if (!hasConfiguredSection)
    {
        for (const auto& d : g_powerShellErrorMessageDefaults)
            entries.push_back(IniEntry{ d.key, d.value });
    }

    for (const auto& entry : entries)
    {
        std::wstring value = TrimCopy(entry.value);
        if (value.empty())
            continue;

        std::wstring codeWithPrefix;
        std::wstring codeWithoutPrefix;
        if (!NormalizePowerShellErrorCodeKey(entry.key, codeWithPrefix, codeWithoutPrefix))
            continue;

        if (ContainsPowerShellErrorCode(output, codeWithPrefix, codeWithoutPrefix))
        {
            message = value;
            return true;
        }
    }

    message.clear();
    return false;
}

void PS_Clear()
{
    std::lock_guard<std::mutex> lk(g_psMutex);
    g_psErr=false; g_psOut.clear(); g_psMsg.clear(); g_psCode=0;
}

bool PS_Run(const std::wstring& cmd)
{
    LogText(g_ui.launchingPowerShellRegistration);
    Log(g_ui.powerShellCommand.c_str(), cmd.c_str());
    PS_Clear();

    aip::PowerShellRunOptions options;
    options.powerShellExe = ReadPowerShellExe();
    options.timeoutMs = static_cast<DWORD>(IniReadClampedI(L"Settings", L"PowerShellTimeoutMs", 120000, 0, 600000));
    options.pollMs = static_cast<DWORD>(IniReadClampedI(L"Settings", L"PowerShellPollMs", 50, 10, 1000));
    options.terminateWaitMs = static_cast<DWORD>(IniReadClampedI(L"Settings", L"PowerShellTerminateWaitMs", 5000, 0, 60000));
    // Historically unbounded; defaults to a generous 4 MiB cap now so a
    // runaway/verbose PowerShell invocation can't grow captured output
    // without limit. Set to 0 to restore the old unbounded behavior.
    options.outputLimitBytes = static_cast<size_t>(IniReadClampedI(L"Settings", L"PowerShellOutputLimitBytes", 4 * 1024 * 1024, 0, 64 * 1024 * 1024));
    options.onUnresolvedAfterTimeout = &MarkRegistrationUnresolved;
    options.unresolvedReason = g_ui.powerShellTimedOut;

    std::wstring timeoutDisplay = TimeoutDisplayText(static_cast<int>(options.timeoutMs));
    Log(g_ui.powerShellLaunchDetails.c_str(), options.powerShellExe.c_str(), timeoutDisplay.c_str(), static_cast<int>(options.pollMs), static_cast<int>(options.terminateWaitMs));

    aip::PowerShellRunResult psResult = aip::RunPowerShellCaptured(cmd, options);

    if (!psResult.started)
    {
        LogWin32Failure(psResult.failedCall.c_str(), psResult.failedCallError);
        return false;
    }
    Log(g_ui.powerShellProcessStarted.c_str(), psResult.processId);

    if (psResult.waitFailed)
        LogWin32Failure(L"WaitForSingleObject", psResult.waitError);

    if (psResult.outputLimitExceeded)
        Log(L"PowerShell output exceeded the configured limit (pid %lu); output was truncated and the process was terminated.", psResult.processId);

    bool timedOut = psResult.timedOut;
    if (timedOut)
    {
        Log(g_ui.powerShellTerminateRequested.c_str(), psResult.processId);
        if (psResult.terminateConfirmed)
            Log(g_ui.powerShellTerminatedAfterTimeout.c_str(), psResult.processId);
        else
            Log(g_ui.powerShellStillRunningAfterTimeout.c_str(), psResult.processId);
        // aip::RunPowerShellCaptured already invoked MarkRegistrationUnresolved
        // itself (via options.onUnresolvedAfterTimeout) if the process still
        // could not be confirmed terminated after every wait window.
    }

    DWORD ec = psResult.exitCode;

    std::wstring out = FormatPowerShellOutput(psResult.rawOutput);
    std::wstring psMsg;
    bool psErr = false;
    if (ec == 0 && !timedOut) {
        psMsg = g_ui.powerShellCompleted;
        {
            std::lock_guard<std::mutex> lk(g_psMutex);
            g_psOut = out;
            g_psCode = ec;
            g_psErr = false;
            g_psMsg = psMsg;
        }
        LogText(g_ui.powerShellCompleted);
        return true;
    }

    psErr = true;

    if (timedOut) {
        psMsg = g_ui.powerShellTimedOut;
        {
            std::lock_guard<std::mutex> lk(g_psMutex);
            g_psOut = out;
            g_psCode = ec;
            g_psErr = psErr;
            g_psMsg = psMsg;
        }
        LogText(g_ui.powerShellTimedOut);
    } else if (LookupConfiguredPowerShellErrorMessage(out, psMsg)) {
        {
            std::lock_guard<std::mutex> lk(g_psMutex);
            g_psOut = out;
            g_psCode = ec;
            g_psErr = psErr;
            g_psMsg = psMsg;
        }
        LogText(psMsg);
    } else {
        psMsg = FormatWide(g_ui.powerShellErrorCode.c_str(), ec);
        {
            std::lock_guard<std::mutex> lk(g_psMutex);
            g_psOut = out;
            g_psCode = ec;
            g_psErr = psErr;
            g_psMsg = psMsg;
        }
        Log(g_ui.powerShellErrorCode.c_str(), ec);
    }

    LogText(g_ui.powerShellOutputFollows);
    Log(L"%ls", out.c_str());

    return false;
}

enum class AppxRegistrationResult
{
    Success,
    Failed,
    TimeoutUncertain
};

using DeploymentResult = winrt::Windows::Management::Deployment::DeploymentResult;
using DeploymentProgress = winrt::Windows::Management::Deployment::DeploymentProgress;
using DeploymentOperation = winrt::Windows::Foundation::IAsyncOperationWithProgress<DeploymentResult, DeploymentProgress>;
using FoundationAsyncStatus = winrt::Windows::Foundation::AsyncStatus;

static void LogComRegistrationDeploymentResult(const DeploymentResult& result)
{
    HRESULT extendedError = result.ExtendedErrorCode().value;
    if (extendedError != S_OK)
        Log(g_ui.comRegistrationDeploymentError.c_str(), static_cast<unsigned int>(extendedError));

    std::wstring errorText = result.ErrorText().c_str();
    if (!TrimCopy(errorText).empty())
        Log(g_ui.comRegistrationDeploymentMessage.c_str(), errorText.c_str());
}

static void LogComRegistrationOperationFailure(
    const DeploymentOperation& op,
    FoundationAsyncStatus status)
{
    Log(g_ui.comRegistrationStatusFailed.c_str(), static_cast<int>(status));

    HRESULT asyncError = op.ErrorCode().value;
    if (asyncError != S_OK)
        Log(g_ui.comRegistrationAsyncError.c_str(), static_cast<unsigned int>(asyncError));

    try
    {
        LogComRegistrationDeploymentResult(op.GetResults());
    }
    catch (const winrt::hresult_error& e)
    {
        HRESULT getResultsError = e.code().value;
        if (getResultsError != S_OK && getResultsError != asyncError)
            Log(g_ui.comRegistrationAsyncError.c_str(), static_cast<unsigned int>(getResultsError));
        if (!std::wstring(e.message().c_str()).empty())
            Log(g_ui.comExceptionMessage.c_str(), e.message().c_str());
    }
}

struct WinrtApartmentGuard
{
    bool initialized = false;

    explicit WinrtApartmentGuard(winrt::apartment_type type)
    {
        winrt::init_apartment(type);
        initialized = true;
    }

    ~WinrtApartmentGuard()
    {
        if (initialized)
        {
            winrt::clear_factory_cache();
            winrt::uninit_apartment();
        }
    }

    WinrtApartmentGuard(const WinrtApartmentGuard&) = delete;
    WinrtApartmentGuard& operator=(const WinrtApartmentGuard&) = delete;
};

static AppxRegistrationResult Appx_Register_COM_InProcess(const std::wstring& manifestPath)
{
    try
    {
        LogText(g_ui.usingComRegistration);

        WinrtApartmentGuard apartment(winrt::apartment_type::single_threaded);

        using namespace winrt::Windows::Management::Deployment;
        using namespace winrt::Windows::Foundation;
        using namespace winrt::Windows::Foundation::Collections;

        PackageManager pm;

        std::wstring uriStr = MakeFileUri(manifestPath);
        if (uriStr.empty())
        {
            LogText(g_ui.invalidManifestPath);
            return AppxRegistrationResult::Failed;
        }
        Log(g_ui.comRegistrationUri.c_str(), uriStr.c_str());
        Uri uri{ winrt::hstring(uriStr) };

        IVector<Uri> deps = winrt::single_threaded_vector<Uri>();

        auto op = pm.RegisterPackageAsync(
            uri,
            deps,
            DeploymentOptions::ForceUpdateFromAnyVersion
        );

        int timeoutMs = IniReadClampedI(L"Settings", L"ComRegistrationTimeoutMs", 120000, 0, 600000);
        int cancelWaitMs = IniReadClampedI(L"Settings", L"ComRegistrationCancelWaitMs", 5000, 0, 60000);
        std::wstring timeoutDisplay = TimeoutDisplayText(timeoutMs);
        Log(g_ui.comRegistrationTimeoutDetails.c_str(), timeoutDisplay.c_str(), cancelWaitMs);
        if (timeoutMs > 0)
        {
            auto status = op.wait_for(std::chrono::milliseconds(timeoutMs));
            if (status == winrt::Windows::Foundation::AsyncStatus::Started)
            {
                op.Cancel();
                LogText(g_ui.comRegistrationTimedOut);

                if (cancelWaitMs <= 0)
                    return AppxRegistrationResult::TimeoutUncertain;

                status = op.wait_for(std::chrono::milliseconds(cancelWaitMs));
                if (status == winrt::Windows::Foundation::AsyncStatus::Started)
                {
                    LogText(g_ui.comRegistrationCancelStillRunning);
                    return AppxRegistrationResult::TimeoutUncertain;
                }
            }
            if (status == winrt::Windows::Foundation::AsyncStatus::Canceled)
            {
                LogComRegistrationOperationFailure(op, status);
                return AppxRegistrationResult::Failed;
            }
            if (status == winrt::Windows::Foundation::AsyncStatus::Error)
            {
                LogComRegistrationOperationFailure(op, status);
                return AppxRegistrationResult::Failed;
            }
        }

        DeploymentResult result = op.get(); // wait/throw if Windows deployment reports an error

        if (op.Status() == winrt::Windows::Foundation::AsyncStatus::Completed)
        {
            LogComRegistrationDeploymentResult(result);
            LogText(g_ui.comRegistrationSuccess);
            return AppxRegistrationResult::Success;
        }
        else
        {
            LogComRegistrationOperationFailure(op, op.Status());
            return AppxRegistrationResult::Failed;
        }
    }
    catch (const winrt::hresult_error& e)
    {
        Log(g_ui.comRegistrationException.c_str(), e.code().value);
        Log(g_ui.comExceptionMessage.c_str(), e.message().c_str());
        return AppxRegistrationResult::Failed;
    }
}

static void InitializeComRegistrationHelperRuntime()
{
    bool initialIniCreated = EnsureInitialIniTemplate();
    if (!initialIniCreated)
        NormalizeIniToUtf8BomIfNeeded();

    EnsureIniDefaults();
    EnsureIniStringDefaults();
    UpgradeRenamedStringDefaults();
    LoadUiStrings();
    ValidateFormatStrings();
    NormalizeSeparatorStrings();

    g_logging = IniReadI(L"Settings", L"Logging", 1) != 0;
    SetLogPathCopy(EffectiveConfiguredLogPath());
    g_logBufferLines = IniReadClampedI(L"Settings", L"LogBufferLines", 128, 1, 100000);
    g_logAppendLockWaitMs = IniReadClampedI(L"Settings", L"LogAppendLockWaitMs", 1000, 0, 60000);
    g_iniWriteLockWaitMs = IniReadClampedI(L"Settings", L"IniWriteLockWaitMs", 5000, 0, 60000);
    g_iniCacheRefreshMs = IniReadClampedI(L"Settings", L"IniCacheRefreshMs", 250, 0, 60000);
    FlushStartupWarnings();
}

static int RunComRegistrationHelper(const std::wstring& manifestPath)
{
    InitializeComRegistrationHelperRuntime();

    if (TrimCopy(manifestPath).empty())
    {
        LogText(g_ui.invalidManifestPath);
        return 1;
    }

    AppxRegistrationResult result = Appx_Register_COM_InProcess(manifestPath);
    switch (result)
    {
    case AppxRegistrationResult::Success:
        return 0;
    case AppxRegistrationResult::TimeoutUncertain:
        return 2;
    case AppxRegistrationResult::Failed:
    default:
        return 1;
    }
}

static AppxRegistrationResult Appx_Register_COM_OutOfProcess(const std::wstring& manifestPath)
{
    LogText(g_ui.usingComRegistration);

    const int timeoutMs = IniReadClampedI(L"Settings", L"ComRegistrationTimeoutMs", 120000, 0, 600000);
    const int cancelWaitMs = IniReadClampedI(L"Settings", L"ComRegistrationCancelWaitMs", 5000, 0, 60000);
    std::wstring timeoutDisplay = TimeoutDisplayText(timeoutMs);
    Log(g_ui.comRegistrationTimeoutDetails.c_str(), timeoutDisplay.c_str(), cancelWaitMs);

    if (g_exePath.empty())
    {
        LogWin32Failure(L"GetModuleFileNameW", ERROR_INVALID_DATA);
        return AppxRegistrationResult::Failed;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::wstring cmdline = QuoteCommandLineArg(g_exePath) + L" " +
        COM_REGISTRATION_HELPER_ARG + L" " + QuoteCommandLineArg(manifestPath);
    AppendCommandLineForwardedOptions(cmdline);
    std::wstring workingDir = GetExeDir();

    BOOL ok = CreateProcessW(g_exePath.c_str(), &cmdline[0], nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, workingDir.empty() ? nullptr : workingDir.c_str(), &si, &pi);
    if (!ok)
    {
        LogWin32Failure(L"CreateProcessW(COM registration helper)");
        return AppxRegistrationResult::Failed;
    }

    Log(g_ui.comRegistrationHelperStarted.c_str(), pi.dwProcessId);

    DWORD waitMs = timeoutMs > 0 ? static_cast<DWORD>(timeoutMs) : INFINITE;
    DWORD waitResult = WaitForSingleObject(pi.hProcess, waitMs);
    if (waitResult == WAIT_TIMEOUT)
    {
        LogText(g_ui.comRegistrationHelperTimedOut);
        bool helperExitConfirmed = false;
        if (!TerminateProcess(pi.hProcess, 3))
        {
            Log(g_ui.comRegistrationHelperTerminateFailed.c_str(), pi.dwProcessId);
        }
        else if (cancelWaitMs > 0)
        {
            DWORD terminateWait = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(cancelWaitMs));
            if (terminateWait == WAIT_TIMEOUT)
                Log(g_ui.comRegistrationHelperStillRunning.c_str(), pi.dwProcessId);
            else if (terminateWait == WAIT_FAILED)
                LogWin32Failure(L"WaitForSingleObject(COM registration helper terminate)");
            else
            {
                helperExitConfirmed = true;
                Log(g_ui.comRegistrationHelperTerminated.c_str(), pi.dwProcessId);
            }
        }
        else if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
        {
            helperExitConfirmed = true;
            Log(g_ui.comRegistrationHelperTerminated.c_str(), pi.dwProcessId);
        }

        if (!helperExitConfirmed)
        {
            DWORD finalWait = WaitForSingleObject(pi.hProcess, 0);
            if (finalWait == WAIT_OBJECT_0)
            {
                Log(g_ui.comRegistrationHelperTerminated.c_str(), pi.dwProcessId);
            }
            else if (finalWait == WAIT_FAILED)
            {
                LogWin32Failure(L"WaitForSingleObject(COM registration helper final)");
            }
            else
            {
                MarkRegistrationUnresolved(g_ui.comRegistrationHelperTimedOut, pi.hProcess, pi.dwProcessId);
                pi.hProcess = nullptr;
            }
        }

        CloseHandle(pi.hThread);
        if (pi.hProcess)
            CloseHandle(pi.hProcess);
        return AppxRegistrationResult::TimeoutUncertain;
    }

    if (waitResult == WAIT_FAILED)
    {
        LogWin32Failure(L"WaitForSingleObject(COM registration helper)");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return AppxRegistrationResult::Failed;
    }

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        LogWin32Failure(L"GetExitCodeProcess(COM registration helper)");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return AppxRegistrationResult::Failed;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    Log(g_ui.comRegistrationHelperExitCode.c_str(), exitCode);
    if (exitCode == 0)
        return AppxRegistrationResult::Success;
    if (exitCode == 2)
        return AppxRegistrationResult::TimeoutUncertain;
    if ((exitCode & 0x80000000UL) != 0)
        Log(g_ui.comRegistrationHelperCrashed.c_str(), exitCode);
    return AppxRegistrationResult::Failed;
}

AppxRegistrationResult Appx_Register_COM(const std::wstring& manifestPath)
{
    if (IniReadI(L"Settings", L"ComRegistrationUseHelper", 0) != 0)
        return Appx_Register_COM_OutOfProcess(manifestPath);
    return Appx_Register_COM_InProcess(manifestPath);
}
