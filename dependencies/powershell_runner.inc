#ifndef AIP_DEPENDENCIES_POWERSHELL_RUNNER_INC
#define AIP_DEPENDENCIES_POWERSHELL_RUNNER_INC

// Shared "spawn powershell.exe, capture output, enforce a timeout" primitive.
//
// This unifies two implementations that had drifted apart:
//  - DesktopStub's ga_registration.inc PS_Run, which has INI-configurable
//    timeout/poll/terminate values and a hook for tracking processes that
//    could not be confirmed terminated after a timeout, but had no cap on
//    captured output size.
//  - RssLiveTile's RunPowerShellCommand, which capped captured output at
//    1 MiB but hardcoded its timeout values and had no unresolved-process
//    tracking hook.
//
// DesktopStub's behavior is treated as the reference implementation (it is
// the more mature, widely-used surface); RssLiveTile's output cap is folded
// in as a strict improvement since unbounded capture of a runaway process's
// output is a real (if rare) resource-exhaustion gap. Assumes
// desktop_app_baseline.h (or at least core.inc + config_ini.inc) is already
// included, matching the convention used by dpapi.inc and tray.inc.

namespace aip
{

// ---------------------------------------------------------------------------
// Text quoting / encoding helpers for building -EncodedCommand invocations
// ---------------------------------------------------------------------------

// Quotes a value for embedding inside a single-quoted PowerShell string
// literal (doubles embedded single quotes, per PowerShell's own escaping
// rule -- this is unrelated to QuoteCommandLineArg, which quotes a Win32
// argv entry).
inline std::wstring PowerShellSingleQuotedString(const std::wstring& value)
{
    std::wstring quoted = L"'";
    quoted.reserve(value.size() + 2);
    for (wchar_t ch : value)
    {
        if (ch == L'\'')
        {
            quoted += L"''";
        }
        else
        {
            quoted.push_back(ch);
        }
    }
    quoted.push_back(L'\'');
    return quoted;
}

inline std::wstring Base64EncodeBytes(const BYTE* data, size_t len)
{
    static const wchar_t alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3)
    {
        DWORD block = (static_cast<DWORD>(data[i])) << 16;
        bool have2 = i + 1 < len;
        bool have3 = i + 2 < len;
        if (have2) block |= (static_cast<DWORD>(data[i + 1])) << 8;
        if (have3) block |= data[i + 2];

        out.push_back(alphabet[(block >> 18) & 0x3F]);
        out.push_back(alphabet[(block >> 12) & 0x3F]);
        out.push_back(have2 ? alphabet[(block >> 6) & 0x3F] : L'=');
        out.push_back(have3 ? alphabet[block & 0x3F] : L'=');
    }

    return out;
}

inline std::wstring PowerShellEncodedCommand(const std::wstring& command)
{
    const BYTE* bytes = reinterpret_cast<const BYTE*>(command.data());
    return Base64EncodeBytes(bytes, command.size() * sizeof(wchar_t));
}

inline std::wstring PowerShellUtf8Preamble()
{
    return L"[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; "
        L"$OutputEncoding = [System.Text.Encoding]::UTF8; "
        L"$ProgressPreference = 'SilentlyContinue'; ";
}

// ---------------------------------------------------------------------------
// powershell.exe path resolution
// ---------------------------------------------------------------------------

inline std::wstring DefaultPowerShellExe()
{
    std::wstring systemDirectory = GetSystemDirectoryPath();
    if (!systemDirectory.empty())
    {
        return systemDirectory + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    }
    return L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
}

inline bool IsLocalAbsoluteFilePath(const std::wstring& path)
{
    std::wstring trimmed = Trim(path);
    if (trimmed.rfind(L"\\\\?\\", 0) == 0)
    {
        trimmed.erase(0, 4);
    }

    return trimmed.size() >= 3 &&
        iswalpha(trimmed[0]) &&
        trimmed[1] == L':' &&
        (trimmed[2] == L'\\' || trimmed[2] == L'/');
}

// Resolves the effective powershell.exe path from a (possibly empty)
// configured override string. Sets usedInvalidOverride when the override was
// non-empty but not a local absolute file path, so the caller can log a
// fallback warning with its own localized text -- this function never logs,
// so it stays usable from any product regardless of its logging setup.
inline std::wstring ResolveConfiguredPowerShellExe(const std::wstring& configuredOverride, bool& usedInvalidOverride)
{
    usedInvalidOverride = false;
    std::wstring fallback = DefaultPowerShellExe();
    std::wstring configured = Trim(configuredOverride);
    if (configured.empty())
    {
        return fallback;
    }
    if (!IsLocalAbsoluteFilePath(configured))
    {
        usedInvalidOverride = true;
        return fallback;
    }
    return configured;
}

// Decodes captured process output bytes: BOM-aware text detection first,
// then explicit UTF-8, then the system ANSI code page as a last resort.
inline std::wstring DecodePowerShellOutputBytes(const std::vector<BYTE>& bytes)
{
    std::wstring out;
    if (DecodeTextBytes(bytes, out))
    {
        return out;
    }
    if (DecodeCodePageText(CP_UTF8, bytes.data(), bytes.size(), out, 0))
    {
        return out;
    }
    if (DecodeCodePageText(CP_ACP, bytes.data(), bytes.size(), out, 0))
    {
        return out;
    }
    return L"";
}

// ---------------------------------------------------------------------------
// Process execution
// ---------------------------------------------------------------------------

using PowerShellUnresolvedHandler = void(*)(const std::wstring& reason, HANDLE processHandle, DWORD processId);

struct PowerShellRunOptions
{
    std::wstring powerShellExe;
    DWORD timeoutMs = 120000;
    DWORD pollMs = 50;
    DWORD terminateWaitMs = 5000;
    // 0 means unbounded capture. Callers that previously had no cap
    // (DesktopStub) can keep that behavior explicitly; new/updated callers
    // should set a real limit (RssLiveTile uses 1 MiB).
    size_t outputLimitBytes = 0;
    // Invoked only if, after a timeout and the terminate-wait window, the
    // process still could not be confirmed to have exited. Receives an
    // owning HANDLE the callback becomes responsible for eventually closing.
    // When null, the runner itself closes the handle and drops the case.
    PowerShellUnresolvedHandler onUnresolvedAfterTimeout = nullptr;
    // Reason text passed to onUnresolvedAfterTimeout. Callers with their own
    // localized string (e.g. DesktopStub's g_ui.powerShellTimedOut) should
    // set this explicitly; the default only matters for callers that supply
    // onUnresolvedAfterTimeout but not a custom reason.
    std::wstring unresolvedReason = L"PowerShell command timed out and could not be confirmed terminated.";
};

struct PowerShellRunResult
{
    bool ok = false;
    bool started = true;
    DWORD exitCode = 1;
    std::wstring rawOutput;
    bool timedOut = false;
    bool terminateConfirmed = false;
    bool waitFailed = false;
    DWORD waitError = ERROR_SUCCESS;
    bool outputLimitExceeded = false;
    DWORD processId = 0;
    // Set when 'started' is false, naming the Win32 call that failed. Empty
    // otherwise. Callers that want a formatted GetLastError message can pair
    // this with GetLastErrorText() themselves, preserving each product's own
    // logging phrasing.
    std::wstring failedCall;
    DWORD failedCallError = ERROR_SUCCESS;
};

inline PowerShellRunResult RunPowerShellCaptured(const std::wstring& command, const PowerShellRunOptions& options)
{
    PowerShellRunResult result;

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE r = nullptr;
    HANDLE w = nullptr;
    if (!CreatePipe(&r, &w, &sa, 0))
    {
        result.started = false;
        result.failedCall = L"CreatePipe";
        result.failedCallError = GetLastError();
        return result;
    }
    if (!SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0))
    {
        result.started = false;
        result.failedCall = L"SetHandleInformation(pipe read handle)";
        result.failedCallError = GetLastError();
        CloseHandle(r);
        CloseHandle(w);
        return result;
    }

    HANDLE nulIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nulIn == INVALID_HANDLE_VALUE)
    {
        result.started = false;
        result.failedCall = L"CreateFileW(NUL)";
        result.failedCallError = GetLastError();
        CloseHandle(r);
        CloseHandle(w);
        return result;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nulIn;
    si.hStdOutput = w;
    si.hStdError = w;

    PROCESS_INFORMATION pi{};
    std::wstring cmdline = QuoteCommandLineArg(options.powerShellExe) +
        L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " +
        PowerShellEncodedCommand(PowerShellUtf8Preamble() + command);

    BOOL started = CreateProcessW(nullptr, &cmdline[0], nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(w);
    CloseHandle(nulIn);
    if (!started)
    {
        result.started = false;
        result.failedCall = L"CreateProcessW";
        result.failedCallError = GetLastError();
        CloseHandle(r);
        return result;
    }

    result.processId = pi.dwProcessId;

    char buf[2048];
    std::vector<BYTE> outBytes;
    bool outputLimitExceeded = false;

    auto drainPipe = [&]()
    {
        for (;;)
        {
            DWORD available = 0;
            if (!PeekNamedPipe(r, nullptr, 0, nullptr, &available, nullptr) || available == 0)
            {
                break;
            }

            DWORD toRead = (std::min)(available, static_cast<DWORD>(sizeof(buf)));
            DWORD n = 0;
            if (!ReadFile(r, buf, toRead, &n, nullptr) || n == 0)
            {
                break;
            }

            if (options.outputLimitBytes > 0)
            {
                size_t remaining = options.outputLimitBytes > outBytes.size()
                    ? options.outputLimitBytes - outBytes.size()
                    : 0;
                DWORD toKeep = static_cast<DWORD>((std::min)(static_cast<size_t>(n), remaining));
                if (toKeep > 0)
                {
                    outBytes.insert(outBytes.end(), buf, buf + toKeep);
                }
                if (toKeep != n)
                {
                    outputLimitExceeded = true;
                    return;
                }
            }
            else
            {
                outBytes.insert(outBytes.end(), buf, buf + n);
            }
        }
    };

    const ULONGLONG psStart = GetTickCount64();
    bool timedOut = false;
    bool waitFailed = false;
    DWORD waitError = ERROR_SUCCESS;
    bool timeoutExitConfirmed = false;

    for (;;)
    {
        drainPipe();
        if (outputLimitExceeded)
        {
            TerminateProcess(pi.hProcess, ERROR_FILE_TOO_LARGE);
            if (options.terminateWaitMs > 0)
            {
                WaitForSingleObject(pi.hProcess, options.terminateWaitMs);
            }
            break;
        }

        DWORD waitResult = WaitForSingleObject(pi.hProcess, options.pollMs);
        if (waitResult == WAIT_OBJECT_0)
        {
            drainPipe();
            break;
        }
        if (waitResult == WAIT_FAILED)
        {
            // Distinct from DesktopStub's original PS_Run, which just logged
            // and broke here without terminating: an invalid wait almost
            // never happens on a live handle, but if it does, terminate
            // rather than leave the child running unmonitored. Matches
            // RssLiveTile's slightly more defensive handling.
            waitFailed = true;
            waitError = GetLastError();
            TerminateProcess(pi.hProcess, waitError);
            if (options.terminateWaitMs > 0)
            {
                WaitForSingleObject(pi.hProcess, options.terminateWaitMs);
            }
            break;
        }
        if (options.timeoutMs > 0 && (GetTickCount64() - psStart) >= options.timeoutMs)
        {
            timedOut = true;
            TerminateProcess(pi.hProcess, WAIT_TIMEOUT);
            if (options.terminateWaitMs > 0)
            {
                DWORD terminateWaitResult = WaitForSingleObject(pi.hProcess, options.terminateWaitMs);
                timeoutExitConfirmed = terminateWaitResult == WAIT_OBJECT_0;
            }
            else if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
            {
                timeoutExitConfirmed = true;
            }
            drainPipe();
            break;
        }
    }

    CloseHandle(r);

    DWORD exitCode = STILL_ACTIVE;
    bool gotExitCode = GetExitCodeProcess(pi.hProcess, &exitCode) != FALSE;
    if (!gotExitCode)
    {
        exitCode = 1;
    }
    if (timedOut)
    {
        exitCode = WAIT_TIMEOUT;
    }
    if (outputLimitExceeded)
    {
        exitCode = ERROR_FILE_TOO_LARGE;
    }

    if (timedOut && !timeoutExitConfirmed)
    {
        DWORD finalWait = WaitForSingleObject(pi.hProcess, 0);
        if (finalWait == WAIT_OBJECT_0)
        {
            timeoutExitConfirmed = true;
        }
        else if (options.onUnresolvedAfterTimeout)
        {
            options.onUnresolvedAfterTimeout(options.unresolvedReason, pi.hProcess, pi.dwProcessId);
            pi.hProcess = nullptr; // Ownership transferred to the callback.
        }
    }

    if (pi.hProcess)
    {
        CloseHandle(pi.hProcess);
    }
    CloseHandle(pi.hThread);

    result.rawOutput = DecodePowerShellOutputBytes(outBytes);
    result.exitCode = exitCode;
    result.timedOut = timedOut;
    result.terminateConfirmed = timeoutExitConfirmed;
    result.waitFailed = waitFailed;
    result.waitError = waitError;
    result.outputLimitExceeded = outputLimitExceeded;
    result.ok = gotExitCode && exitCode == 0 && !timedOut && !waitFailed && !outputLimitExceeded;
    return result;
}

} // namespace aip

#endif
