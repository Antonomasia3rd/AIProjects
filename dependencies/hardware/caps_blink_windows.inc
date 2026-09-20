#ifndef AIP_HARDWARE_CAPS_BLINK_WINDOWS_INC
#define AIP_HARDWARE_CAPS_BLINK_WINDOWS_INC

#include "caps_blink_engine.h"
#include "../desktop_app_baseline.h"
#include <wincrypt.h>
#include <atomic>

namespace aip { namespace caps {
using LogicalCapsReader = std::function<bool(bool&, std::wstring&)>;
namespace windows_detail {
inline bool Fail(std::wstring& error, const wchar_t* operation, DWORD code = GetLastError())
{
    error = std::wstring(operation) + L": " + aip::GetLastErrorText(code ? code : ERROR_GEN_FAILURE);
    return false;
}
inline bool DeviceMutexName(const std::wstring& target, std::wstring& name, std::wstring& error)
{
    std::wstring upper = target;
    for (auto& ch : upper) if (ch >= L'a' && ch <= L'z') ch = ch - L'a' + L'A';
    std::string bytes;
    if (!aip::TryWideToUtf8(upper, bytes)) return Fail(error, L"Encode keyboard device identity");
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return Fail(error, L"Create device identity hash provider");
    bool ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
    if (ok) ok = CryptHashData(hash, reinterpret_cast<const BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()), 0) != FALSE;
    BYTE digest[32]{};
    DWORD length = sizeof(digest);
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0) != FALSE && length == sizeof(digest);
    const DWORD code = ok ? ERROR_SUCCESS : GetLastError();
    if (hash) CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!ok) return Fail(error, L"Hash keyboard device identity", code);
    static const wchar_t hex[] = L"0123456789ABCDEF";
    name = L"Global\\AIProjects.capsblink.Device.";
    // Same first-eight-byte uppercase SHA-256 suffix as ManagedNamedObjects.
    for (size_t i = 0; i < 8; ++i) { name += hex[digest[i] >> 4]; name += hex[digest[i] & 15]; }
    return true;
}
struct DeviceHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~DeviceHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct IoData {
    std::shared_ptr<DeviceHandle> device;
    OVERLAPPED overlapped{};
    Indicators indicators;
    bool write = false;
    ~IoData() { if (overlapped.hEvent) CloseHandle(overlapped.hEvent); }
};
// Created before an IOCTL is issued. If cancellation has not completed by the
// operation deadline, this one-shot wait owns all kernel-visible storage until
// the event signals; it never calls back into the Controller/Backend object.
struct CompletionWait {
    std::shared_ptr<IoData> data;
    PTP_WAIT wait = nullptr;
    explicit CompletionWait(std::shared_ptr<IoData> value) : data(std::move(value))
    { wait = CreateThreadpoolWait(Completed, this, nullptr); }
    ~CompletionWait() { if (wait) CloseThreadpoolWait(wait); }
    static void CALLBACK Completed(PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT)
    {
        // CloseThreadpoolWait releases asynchronously if this callback is
        // still outstanding. The wait is never re-armed, so no later callback
        // can access this context.
        delete static_cast<CompletionWait*>(context);
    }
};
} // namespace windows_detail

class WindowsBackend final : public Backend {
    static constexpr DWORD QueryIndicators = (0x0000000b << 16) | (0x0010 << 2);
    static constexpr DWORD SetIndicators = (0x0000000b << 16) | (0x0002 << 2);
    std::shared_ptr<windows_detail::DeviceHandle> device_;
    std::shared_ptr<windows_detail::IoData> pending_;
    HANDLE mutex_ = nullptr;
    bool ownsMutex_ = false, mapping_ = false;
    std::wstring mappingName_, target_;
    LogicalCapsReader logical_;
    std::chrono::milliseconds operationTimeout_{1000};

    bool WaitEvent(HANDLE event, const Cancel& canceled, std::wstring& error)
    {
        const auto deadline = std::chrono::steady_clock::now() + operationTimeout_;
        for (;;)
        {
            const DWORD result = WaitForSingleObject(event, 0);
            if (result == WAIT_OBJECT_0) return true;
            if (result == WAIT_FAILED) return windows_detail::Fail(error, L"Wait for keyboard operation");
            if (canceled && canceled()) { error = L"Keyboard operation canceled"; return false; }
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) { error = L"Keyboard operation timed out"; return false; }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            const auto wait = static_cast<DWORD>((std::max)(1LL, (std::min)(20LL, static_cast<long long>(remaining))));
            const DWORD waited = WaitForSingleObject(event, wait);
            if (waited == WAIT_OBJECT_0) return true;
            if (waited == WAIT_FAILED) return windows_detail::Fail(error, L"Wait for keyboard operation");
        }
    }
    bool Ioctl(bool write, Indicators& indicators, const Cancel& canceled, std::wstring& error)
    {
        if (!device_) { error = L"Keyboard device is not open"; return false; }
        if (pending_)
        {
            // Never issue restoration behind an unfinished write: a late
            // blink completion could otherwise undo the restored state.
            if (!WaitEvent(pending_->overlapped.hEvent, canceled, error)) return false;
            auto completed = std::move(pending_);
            if (!completed->write && !write)
            {
                DWORD returned = 0;
                if (!GetOverlappedResult(completed->device->value, &completed->overlapped, &returned, FALSE))
                    return windows_detail::Fail(error, L"Complete pending indicator query");
                if (returned < sizeof(Indicators)) { error = L"Keyboard driver returned a truncated indicator response"; return false; }
                indicators = completed->indicators;
                return true;
            }
        }
        if (canceled && canceled()) { error = L"Keyboard operation canceled"; return false; }
        auto data = std::make_shared<windows_detail::IoData>();
        data->device = device_;
        data->indicators = indicators;
        data->write = write;
        data->overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!data->overlapped.hEvent) return windows_detail::Fail(error, L"Create keyboard operation event");
        auto completion = std::make_unique<windows_detail::CompletionWait>(data);
        if (!completion->wait) return windows_detail::Fail(error, L"Create keyboard completion wait");
        DWORD returned = 0;
        const BOOL immediate = DeviceIoControl(device_->value, write ? SetIndicators : QueryIndicators,
            write ? &data->indicators : nullptr, write ? sizeof(Indicators) : 0,
            write ? nullptr : &data->indicators, write ? 0 : sizeof(Indicators), &returned, &data->overlapped);
        if (!immediate)
        {
            if (GetLastError() != ERROR_IO_PENDING) return windows_detail::Fail(error, L"Issue keyboard indicator operation");
            bool finished = false;
            try { finished = WaitEvent(data->overlapped.hEvent, canceled, error); }
            catch (...)
            {
                CancelIoEx(device_->value, &data->overlapped);
                pending_ = data;
                auto* retained = completion.release();
                SetThreadpoolWait(retained->wait, data->overlapped.hEvent, nullptr);
                throw;
            }
            if (!finished)
            {
                CancelIoEx(device_->value, &data->overlapped);
                pending_ = data;
                auto* retained = completion.release();
                SetThreadpoolWait(retained->wait, data->overlapped.hEvent, nullptr);
                return false;
            }
        }
        if (!GetOverlappedResult(device_->value, &data->overlapped, &returned, FALSE))
            return windows_detail::Fail(error, L"Complete keyboard indicator operation");
        if (!write && returned < sizeof(Indicators))
        {
            error = L"Keyboard driver returned a truncated indicator response";
            return false;
        }
        indicators = data->indicators;
        return true;
    }
public:
    explicit WindowsBackend(LogicalCapsReader logical = {}) : logical_(std::move(logical)) {}
    ~WindowsBackend() override { try { std::wstring ignored; Close(ignored); } catch (...) {} }

    bool Open(const std::wstring& target, const Cancel& canceled, std::wstring& error) override
    {
        std::wstring previousError;
        if (!Close(previousError)) { error = previousError; return false; }
        Config validation;
        validation.target = target;
        if (!Normalize(validation, error)) return false;
        target_ = validation.target;
        if (canceled && canceled()) { error = L"Keyboard open canceled"; return false; }
        std::wstring mutexName;
        if (!windows_detail::DeviceMutexName(target_, mutexName, error)) return false;
        mutex_ = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (!mutex_) return windows_detail::Fail(error, L"Create keyboard ownership mutex");
        const DWORD wait = WaitForSingleObject(mutex_, 0);
        ownsMutex_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        if (!ownsMutex_) { error = L"Another capsblink instance already controls this keyboard target"; return false; }
        static std::atomic<unsigned long> sequence{0};
        mappingName_ = L"capsblinkKBD_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++sequence);
        if (!DefineDosDeviceW(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, mappingName_.c_str(), target_.c_str()))
            return windows_detail::Fail(error, L"Define keyboard device mapping");
        mapping_ = true;
        auto handle = std::make_shared<windows_detail::DeviceHandle>();
        handle->value = CreateFileW((L"\\\\.\\" + mappingName_).c_str(), GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (handle->value == INVALID_HANDLE_VALUE) return windows_detail::Fail(error, L"Open keyboard class device");
        device_ = std::move(handle);
        return true;
    }
    bool ReadIndicators(Indicators& indicators, const Cancel& canceled, std::wstring& error) override
    { return Ioctl(false, indicators, canceled, error); }
    bool WriteIndicators(Indicators indicators, const Cancel& canceled, std::wstring& error) override
    { return Ioctl(true, indicators, canceled, error); }
    bool ReadLogicalCaps(bool& enabled, const Cancel& canceled, std::wstring& error) override
    {
        if (canceled && canceled()) { error = L"Logical Caps Lock read canceled"; return false; }
        // The default matches the legacy implementation. Hosts can inject a
        // UI-thread-sampled logical state without adding a global hook.
        if (logical_) return logical_(enabled, error);
        enabled = (GetKeyState(VK_CAPITAL) & 1) != 0;
        return true;
    }
    bool HasPendingOperation() const noexcept override
    {
        return pending_ && WaitForSingleObject(pending_->overlapped.hEvent, 0) != WAIT_OBJECT_0;
    }
    bool Close(std::wstring& error) override
    {
        error.clear();
        bool ok = true;
        if (device_) CancelIoEx(device_->value, nullptr);
        if (HasPendingOperation())
        {
            error = L"Keyboard cancellation is still pending; target ownership is retained";
            return false;
        }
        // Any unfinished operation retains its handle, event and buffer until
        // completion. The Controller keeps this owner thread, mapping and
        // mutex alive until that completion, then retries restoration before
        // calling Close. A nonresponsive driver can delay final shutdown.
        pending_.reset();
        device_.reset();
        if (mapping_)
        {
            if (!DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE | DDD_RAW_TARGET_PATH |
                DDD_NO_BROADCAST_SYSTEM, mappingName_.c_str(), target_.c_str()))
                ok = windows_detail::Fail(error, L"Remove keyboard device mapping");
            else mapping_ = false;
        }
        if (ownsMutex_ && !ReleaseMutex(mutex_)) ok = windows_detail::Fail(error, L"Release keyboard ownership mutex");
        ownsMutex_ = false;
        if (mutex_ && !CloseHandle(mutex_)) ok = windows_detail::Fail(error, L"Close keyboard ownership mutex");
        mutex_ = nullptr;
        return ok;
    }
};
inline std::unique_ptr<Backend> MakeWindowsBackend(LogicalCapsReader logical = {})
{ return std::make_unique<WindowsBackend>(std::move(logical)); }
static_assert(sizeof(Indicators) == 4, "Keyboard indicator IOCTL structure must remain two USHORT fields");
} } // namespace aip::caps
#endif
