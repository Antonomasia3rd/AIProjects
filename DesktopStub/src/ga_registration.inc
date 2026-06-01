// -----------------------------------------------------------------------------
// PowerShell execution
// -----------------------------------------------------------------------------
static void MarkRegistrationUnresolved(const std::wstring& reason, HANDLE processHandle, DWORD processId);

static std::wstring Base64EncodeBytes(const BYTE* data, size_t len)
{
    static const wchar_t alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3)
    {
        DWORD block = ((DWORD)data[i]) << 16;
        bool have2 = i + 1 < len;
        bool have3 = i + 2 < len;
        if (have2) block |= ((DWORD)data[i + 1]) << 8;
        if (have3) block |= data[i + 2];

        out.push_back(alphabet[(block >> 18) & 0x3F]);
        out.push_back(alphabet[(block >> 12) & 0x3F]);
        out.push_back(have2 ? alphabet[(block >> 6) & 0x3F] : L'=');
        out.push_back(have3 ? alphabet[block & 0x3F] : L'=');
    }

    return out;
}

static std::wstring PowerShellEncodedCommand(const std::wstring& command)
{
    const BYTE* bytes = reinterpret_cast<const BYTE*>(command.c_str());
    return Base64EncodeBytes(bytes, command.size() * sizeof(wchar_t));
}

static std::wstring PowerShellUtf8Preamble()
{
    return L"[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; "
        L"$OutputEncoding = [System.Text.Encoding]::UTF8; "
        L"$ProgressPreference = 'SilentlyContinue'; ";
}

static std::wstring DefaultPowerShellExe()
{
    wchar_t systemDirectory[MAX_PATH] = {};
    UINT len = GetSystemDirectoryW(systemDirectory, ARRAYSIZE(systemDirectory));
    if (len > 0 && len < ARRAYSIZE(systemDirectory))
        return std::wstring(systemDirectory) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";

    return L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
}

static bool IsLocalAbsoluteFilePath(const std::wstring& path)
{
    std::wstring trimmed = TrimCopy(path);
    if (trimmed.rfind(L"\\\\?\\", 0) == 0)
        trimmed.erase(0, 4);

    return trimmed.size() >= 3 &&
        iswalpha(trimmed[0]) &&
        trimmed[1] == L':' &&
        (trimmed[2] == L'\\' || trimmed[2] == L'/');
}

static std::wstring ReadPowerShellExe()
{
    std::wstring fallback = DefaultPowerShellExe();
    std::wstring configured = TrimCopy(IniReadS(L"Settings", L"PowerShellExe", L""));
    if (configured.empty())
        return fallback;

    if (!IsLocalAbsoluteFilePath(configured))
    {
        Log(g_ui.powerShellExeInvalidPath.c_str(), fallback.c_str());
        return fallback;
    }

    return configured;
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

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE r=0,w=0;
    if (!CreatePipe(&r,&w,&sa,0))
    {
        LogWin32Failure(L"CreatePipe");
        return false;
    }
    if (!SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0))
    {
        LogWin32Failure(L"SetHandleInformation(pipe read handle)");
        CloseHandle(r);
        CloseHandle(w);
        return false;
    }

    HANDLE nulIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nulIn == INVALID_HANDLE_VALUE)
    {
        LogWin32Failure(L"CreateFileW(NUL)");
        CloseHandle(r);
        CloseHandle(w);
        return false;
    }

    STARTUPINFOW si{}; si.cb=sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE; si.hStdInput=nulIn; si.hStdOutput=w; si.hStdError=w;

    PROCESS_INFORMATION pi{};
    std::wstring powerShellExe = ReadPowerShellExe();
    const int timeoutMs = IniReadClampedI(L"Settings", L"PowerShellTimeoutMs", 120000, 0, 600000);
    const int pollMs = IniReadClampedI(L"Settings", L"PowerShellPollMs", 50, 10, 1000);
    const int terminateWaitMs = IniReadClampedI(L"Settings", L"PowerShellTerminateWaitMs", 5000, 0, 60000);
    std::wstring timeoutDisplay = TimeoutDisplayText(timeoutMs);
    Log(g_ui.powerShellLaunchDetails.c_str(), powerShellExe.c_str(), timeoutDisplay.c_str(), pollMs, terminateWaitMs);

    std::wstring cmdline = QuoteCommandLineArg(powerShellExe) + L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " + PowerShellEncodedCommand(PowerShellUtf8Preamble() + cmd);

    BOOL ok = CreateProcessW(nullptr, &cmdline[0], nullptr,nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr,nullptr, &si, &pi);

    CloseHandle(w);
    CloseHandle(nulIn);
    if (!ok)
    {
        LogWin32Failure(L"CreateProcessW");
        CloseHandle(r);
        return false;
    }
    Log(g_ui.powerShellProcessStarted.c_str(), pi.dwProcessId);

    char buf[2048];
    std::vector<BYTE> outBytes;

    auto appendOutputBytes = [&](const char* data, DWORD bytes)
    {
        outBytes.insert(outBytes.end(), reinterpret_cast<const BYTE*>(data), reinterpret_cast<const BYTE*>(data) + bytes);
    };

    auto drainPipe = [&]()
    {
        for (;;)
        {
            DWORD available = 0;
            if (!PeekNamedPipe(r, nullptr, 0, nullptr, &available, nullptr) || available == 0)
                break;

            DWORD toRead = std::min<DWORD>(available, sizeof(buf));
            DWORD n = 0;
            if (!ReadFile(r, buf, toRead, &n, nullptr) || n == 0)
                break;

            appendOutputBytes(buf, n);
        }
    };

    const auto psStart = steady_clock::now();
    bool timedOut = false;
    bool timeoutExitConfirmed = false;

    for (;;)
    {
        drainPipe();

        DWORD waitResult = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(pollMs));
        if (waitResult == WAIT_OBJECT_0)
        {
            drainPipe();
            break;
        }
        if (waitResult == WAIT_FAILED)
        {
            LogWin32Failure(L"WaitForSingleObject");
            break;
        }
        if (timeoutMs > 0 && ElapsedMs(psStart, steady_clock::now()) >= timeoutMs)
        {
            timedOut = true;
            Log(g_ui.powerShellTerminateRequested.c_str(), pi.dwProcessId);
            if (!TerminateProcess(pi.hProcess, WAIT_TIMEOUT))
            {
                Log(g_ui.powerShellTerminateFailed.c_str(), pi.dwProcessId);
                LogWin32Failure(L"TerminateProcess");
            }
            if (terminateWaitMs > 0)
            {
                DWORD terminateWaitResult = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(terminateWaitMs));
                if (terminateWaitResult == WAIT_OBJECT_0)
                {
                    timeoutExitConfirmed = true;
                    Log(g_ui.powerShellTerminatedAfterTimeout.c_str(), pi.dwProcessId);
                }
                else if (terminateWaitResult == WAIT_TIMEOUT)
                    Log(g_ui.powerShellStillRunningAfterTimeout.c_str(), pi.dwProcessId);
                else if (terminateWaitResult == WAIT_FAILED)
                    LogWin32Failure(L"WaitForSingleObject(PowerShell terminate)");
            }
            else if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
            {
                timeoutExitConfirmed = true;
                Log(g_ui.powerShellTerminatedAfterTimeout.c_str(), pi.dwProcessId);
            }
            drainPipe();
            break;
        }
    }

    CloseHandle(r);
    DWORD ec = STILL_ACTIVE;
    if (!GetExitCodeProcess(pi.hProcess, &ec))
    {
        LogWin32Failure(L"GetExitCodeProcess(PowerShell)");
        ec = 1;
    }
    if (timedOut)
        ec = WAIT_TIMEOUT;

    if (timedOut && !timeoutExitConfirmed)
    {
        DWORD finalWait = WaitForSingleObject(pi.hProcess, 0);
        if (finalWait == WAIT_OBJECT_0)
        {
            timeoutExitConfirmed = true;
            Log(g_ui.powerShellTerminatedAfterTimeout.c_str(), pi.dwProcessId);
        }
        else
        {
            MarkRegistrationUnresolved(g_ui.powerShellTimedOut, pi.hProcess, pi.dwProcessId);
            pi.hProcess = nullptr;
        }
    }

    if (pi.hProcess)
        CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::wstring out = FormatPowerShellOutput(DecodeProcessOutput(outBytes));
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
