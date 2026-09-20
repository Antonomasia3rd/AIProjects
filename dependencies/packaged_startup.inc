#ifndef AIP_DEPENDENCIES_PACKAGED_STARTUP_INC
#define AIP_DEPENDENCIES_PACKAGED_STARTUP_INC

// Optional baseline facility for packaged desktop applications. Include after
// desktop_app_baseline.h. Unlike startup_shortcut.inc, this deliberately uses
// the package manifest + Windows.ApplicationModel.StartupTask contract and
// never creates a Startup-folder shortcut or registry entry.
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>

#include <chrono>
#include <exception>
#include <thread>

namespace aip
{
// The caller must still use a background worker for UI responsiveness. These
// deadlines bound the asynchronous OS waits; they do not terminate COM/RPC calls.
template <typename AsyncOperation>
inline auto AwaitPackagedStartupOperation(
    const AsyncOperation& operation,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
{
    using winrt::Windows::Foundation::AsyncStatus;
    auto state = operation.Status();
    if (state == AsyncStatus::Started && timeout.count() > 0)
        state = operation.wait_for(timeout);
    if (state == AsyncStatus::Started)
    {
        // Cancellation is cooperative. Do not report success or automatically
        // retry an enable whose final OS state is still unknown after timeout.
        try { operation.Cancel(); } catch (...) {}
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT),
            L"The Windows startup task did not respond before the timeout. Refresh its state before retrying.");
    }
    return operation.GetResults();
}

enum class PackagedStartupState
{
    Disabled,
    DisabledByUser,
    Enabled,
    DisabledByPolicy,
    EnabledByPolicy,
    Unknown
};

struct PackagedStartupStatus
{
    PackagedStartupState state = PackagedStartupState::Unknown;
    bool enabled = false;
};

inline const wchar_t* PackagedStartupStateName(PackagedStartupState state)
{
    switch (state)
    {
    case PackagedStartupState::Disabled: return L"disabled";
    case PackagedStartupState::DisabledByUser: return L"disabled by the user";
    case PackagedStartupState::Enabled: return L"enabled";
    case PackagedStartupState::DisabledByPolicy: return L"disabled by policy";
    case PackagedStartupState::EnabledByPolicy: return L"enabled by policy";
    case PackagedStartupState::Unknown:
    default: return L"unknown";
    }
}

inline std::wstring PackagedStartupHresultText(HRESULT result)
{
    wchar_t text[16] = {};
    swprintf_s(text, L"%08X", static_cast<unsigned int>(result));
    return text;
}

inline PackagedStartupStatus DescribePackagedStartupState(
    winrt::Windows::ApplicationModel::StartupTaskState state)
{
    using winrt::Windows::ApplicationModel::StartupTaskState;
    switch (state)
    {
    case StartupTaskState::Disabled:
        return { PackagedStartupState::Disabled, false };
    case StartupTaskState::DisabledByUser:
        return { PackagedStartupState::DisabledByUser, false };
    case StartupTaskState::Enabled:
        return { PackagedStartupState::Enabled, true };
    case StartupTaskState::DisabledByPolicy:
        return { PackagedStartupState::DisabledByPolicy, false };
    case StartupTaskState::EnabledByPolicy:
        return { PackagedStartupState::EnabledByPolicy, true };
    default:
        return { PackagedStartupState::Unknown, false };
    }
}

class PackagedStartupTaskMutationLock
{
public:
    explicit PackagedStartupTaskMutationLock(const std::wstring& identity)
    {
        const std::wstring name = L"Local\\AIProjects.PackagedStartup." +
            StableHashHex64(ToLower(identity));
        handle_.Reset(CreateMutexW(nullptr, FALSE, name.c_str()));
        if (!handle_)
        {
            error_ = GetLastError();
            return;
        }

        const DWORD wait = WaitForSingleObject(handle_.Get(), 10000);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED)
        {
            locked_ = true;
            return;
        }
        error_ = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        if (error_ == ERROR_SUCCESS)
        {
            error_ = ERROR_GEN_FAILURE;
        }
    }

    ~PackagedStartupTaskMutationLock()
    {
        if (locked_)
        {
            ReleaseMutex(handle_.Get());
        }
    }

    bool IsLocked() const { return locked_; }
    DWORD Error() const { return error_; }

    PackagedStartupTaskMutationLock(const PackagedStartupTaskMutationLock&) = delete;
    PackagedStartupTaskMutationLock& operator=(const PackagedStartupTaskMutationLock&) = delete;

private:
    UniqueKernelHandle handle_;
    bool locked_ = false;
    DWORD error_ = ERROR_SUCCESS;
};

template <typename Operation>
inline bool RunPackagedStartupOperationOnMta(
    Operation&& operation,
    std::wstring* error)
{
    if (error != nullptr)
    {
        error->clear();
    }

    bool succeeded = false;
    std::wstring workerError;
    try
    {
        std::thread worker([&]()
        {
            bool apartmentInitialized = false;
            try
            {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                apartmentInitialized = true;
                succeeded = operation(workerError);
            }
            catch (const winrt::hresult_error& ex)
            {
                workerError = L"Packaged StartupTask operation failed (0x" +
                    PackagedStartupHresultText(ex.code().value) + L"): " +
                    std::wstring(ex.message().c_str());
            }
            catch (const std::exception& ex)
            {
                workerError = L"Packaged StartupTask operation failed: " +
                    Utf8ToWide(ex.what());
            }
            catch (...)
            {
                workerError = L"Packaged StartupTask operation failed with an unknown exception.";
            }

            if (apartmentInitialized)
            {
                try
                {
                    winrt::uninit_apartment();
                }
                catch (...)
                {
                }
            }
        });
        worker.join();
    }
    catch (const std::exception& ex)
    {
        workerError = L"Could not start the packaged StartupTask worker: " +
            Utf8ToWide(ex.what());
    }
    catch (...)
    {
        workerError = L"Could not start the packaged StartupTask worker.";
    }

    if (!succeeded && error != nullptr)
    {
        *error = workerError.empty()
            ? L"The packaged StartupTask operation failed."
            : workerError;
    }
    return succeeded;
}

inline bool ValidatePackagedStartupTaskRequest(
    const std::wstring& taskId,
    std::wstring* error)
{
    if (taskId.empty())
    {
        if (error != nullptr)
        {
            *error = L"The packaged StartupTask id is empty.";
        }
        return false;
    }
    if (!CurrentProcessHasPackageIdentity())
    {
        if (error != nullptr)
        {
            *error = L"Packaged StartupTask state is available only to a process with package identity.";
        }
        return false;
    }
    return true;
}

inline bool QueryPackagedStartupTask(
    const std::wstring& taskId,
    PackagedStartupStatus& status,
    std::wstring* error = nullptr)
{
    status = {};
    if (!ValidatePackagedStartupTaskRequest(taskId, error))
    {
        return false;
    }

    return RunPackagedStartupOperationOnMta(
        [&](std::wstring& workerError)
        {
            auto task = AwaitPackagedStartupOperation(winrt::Windows::ApplicationModel::StartupTask::GetAsync(taskId));
            if (!task)
            {
                workerError = L"The package manifest does not declare StartupTask id '" +
                    taskId + L"'.";
                return false;
            }
            status = DescribePackagedStartupState(task.State());
            return true;
        },
        error);
}

inline bool SetPackagedStartupTaskEnabled(
    const std::wstring& taskId,
    bool desired,
    PackagedStartupStatus& finalStatus,
    std::wstring* error = nullptr)
{
    finalStatus = {};
    if (!ValidatePackagedStartupTaskRequest(taskId, error))
    {
        return false;
    }

    return RunPackagedStartupOperationOnMta(
        [&](std::wstring& workerError)
        {
            using winrt::Windows::ApplicationModel::StartupTask;
            using winrt::Windows::ApplicationModel::StartupTaskState;

            StartupTask task = AwaitPackagedStartupOperation(StartupTask::GetAsync(taskId));
            if (!task)
            {
                workerError = L"The package manifest does not declare StartupTask id '" +
                    taskId + L"'.";
                return false;
            }

            StartupTaskState state = task.State();
            if (desired)
            {
                if (state == StartupTaskState::Disabled)
                {
                    state = AwaitPackagedStartupOperation(task.RequestEnableAsync());
                }
            }
            else if (state == StartupTaskState::Enabled)
            {
                task.Disable();
                state = task.State();
            }

            finalStatus = DescribePackagedStartupState(state);
            if (finalStatus.enabled == desired)
            {
                return true;
            }

            workerError = L"StartupTask '" + taskId + L"' is " +
                PackagedStartupStateName(finalStatus.state) + L".";
            if (desired && finalStatus.state == PackagedStartupState::DisabledByUser)
            {
                workerError += L" Re-enable it manually in Task Manager's Startup apps page.";
            }
            else if (finalStatus.state == PackagedStartupState::DisabledByPolicy ||
                     finalStatus.state == PackagedStartupState::EnabledByPolicy)
            {
                workerError += L" The administrator policy cannot be overridden by this app.";
            }
            return false;
        },
        error);
}

// Packaged counterpart to CommitStartupShortcutIniState. It uses only the
// manifest-declared StartupTask API and holds both the task/profile lock and
// the shared INI lock across the directional commit.
template <typename IniMutator>
inline bool CommitPackagedStartupTaskIniState(
    const std::wstring& taskId,
    const std::wstring& lockIdentity,
    const std::wstring& iniPath,
    const std::wstring& iniHeader,
    const wchar_t* startupSection,
    const wchar_t* startupKey,
    bool startupFallback,
    bool desired,
    IniMutator&& mutateIni,
    PackagedStartupStatus& finalStatus,
    std::wstring* error = nullptr,
    DWORD iniWriteMutexWaitMs = 10000)
{
    finalStatus = {};
    if (error != nullptr)
    {
        error->clear();
    }
    if (!ValidatePackagedStartupTaskRequest(taskId, error))
    {
        return false;
    }

    PackagedStartupTaskMutationLock startupLock(lockIdentity);
    if (!startupLock.IsLocked())
    {
        if (error != nullptr)
        {
            *error = L"Could not lock the packaged StartupTask/INI transaction: " +
                GetLastErrorText(startupLock.Error());
        }
        return false;
    }

    IniWriteMutexGuard iniLock(iniPath, iniWriteMutexWaitMs);
    if (!iniLock.IsLocked())
    {
        if (error != nullptr)
        {
            *error = L"Could not lock the packaged StartupTask/INI settings: " +
                GetLastErrorText(iniLock.Error());
        }
        return false;
    }

    IniTextSnapshot previousIni;
    if (!CaptureIniTextSnapshot(iniPath, previousIni))
    {
        if (error != nullptr)
        {
            *error = L"Could not capture the packaged StartupTask/INI settings: " +
                GetLastErrorText(GetLastError());
        }
        return false;
    }

    bool previousConfigured = startupFallback;
    if (!ReadIniBooleanFromText(
            previousIni.text,
            startupSection,
            startupKey,
            startupFallback,
            previousConfigured))
    {
        if (error != nullptr)
        {
            *error = L"The existing packaged StartupTask setting is not a valid boolean.";
        }
        return false;
    }

    std::wstring desiredIni = previousIni.text;
    if (desiredIni.empty() && !iniHeader.empty())
    {
        desiredIni = iniHeader;
    }
    SetLastError(ERROR_SUCCESS);
    if (!mutateIni(desiredIni))
    {
        DWORD mutationError = GetLastError();
        if (error != nullptr)
        {
            *error = L"Could not prepare the packaged StartupTask/INI settings: " +
                GetLastErrorText(
                    mutationError == ERROR_SUCCESS
                        ? ERROR_INVALID_DATA
                        : mutationError);
        }
        return false;
    }

    PackagedStartupStatus previousStartup;
    if (!QueryPackagedStartupTask(taskId, previousStartup, error))
    {
        return false;
    }

    if (previousStartup.enabled != previousConfigured)
    {
        PackagedStartupStatus reconciled;
        std::wstring reconcileError;
        if (!SetPackagedStartupTaskEnabled(
                taskId,
                previousConfigured,
                reconciled,
                &reconcileError))
        {
            // An explicit Task Manager or administrator-policy decision must
            // remain authoritative. If the requested value already matches
            // that actual state, allow the transaction to repair only the INI;
            // otherwise report that the OS decision cannot be overridden.
            if (desired != previousStartup.enabled)
            {
                if (error != nullptr)
                {
                    *error = L"Could not reconcile the packaged StartupTask before the INI transaction: " +
                        reconcileError;
                }
                return false;
            }
            previousConfigured = previousStartup.enabled;
        }
        else
        {
            previousStartup = reconciled;
        }
    }

    finalStatus = previousStartup;
    auto mutateLaunch = [&](bool state, std::wstring& mutationError)
    {
        PackagedStartupStatus status;
        if (!SetPackagedStartupTaskEnabled(
                taskId,
                state,
                status,
                &mutationError))
        {
            finalStatus = status;
            return false;
        }
        finalStatus = status;
        return true;
    };
    auto persistDesired = [&](std::wstring& persistenceError)
    {
        if (WriteTextFileUtf8Bom(iniPath, desiredIni))
        {
            return true;
        }
        persistenceError = GetLastErrorText(GetLastError());
        return false;
    };
    auto restorePrevious = [&](std::wstring& restorationError)
    {
        if (RestoreIniTextSnapshot(iniPath, previousIni))
        {
            return true;
        }
        restorationError = GetLastErrorText(GetLastError());
        return false;
    };

    return ExecuteCrashConsistentLaunchConfigState(
        desired,
        previousConfigured,
        mutateLaunch,
        persistDesired,
        restorePrevious,
        error);
}
}

#endif
