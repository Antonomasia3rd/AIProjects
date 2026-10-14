#ifndef AIP_HARDWARE_CAPS_BLINK_WINDOWS_INC
#define AIP_HARDWARE_CAPS_BLINK_WINDOWS_INC

#include "caps_blink_engine.h"
#include "caps_blink_group.h"
#include "../desktop_app_baseline.h"
#include <wincrypt.h>
#include <setupapi.h>
#include <atomic>
#pragma comment(lib, "Setupapi.lib")

namespace aip { namespace caps {
using LogicalCapsReader = std::function<bool(bool&, std::wstring&)>;
namespace windows_detail {
inline bool Fail(std::wstring& error, const wchar_t* operation, DWORD code = GetLastError())
{
    error = std::wstring(operation) + L": " + aip::GetLastErrorText(code ? code : ERROR_GEN_FAILURE);
    return false;
}
inline bool DiscoverKeyboards(std::vector<std::wstring>& paths, const Cancel& canceled, std::wstring& error)
{
    if (canceled && canceled()) { error = L"Keyboard discovery canceled"; return false; }
    // Documented keyboard class-driver interface; do not guess class numbers
    // or probe nonexistent raw device names. Enumeration itself writes no LEDs.
    static const GUID keyboardInterface = {0x884b96c3, 0x56ef, 0x11d1, {0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd}};
    struct DeviceInfoOwner {
        HDEVINFO value = INVALID_HANDLE_VALUE;
        ~DeviceInfoOwner() { if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value); }
    } devices;
    devices.value = SetupDiGetClassDevsW(&keyboardInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices.value == INVALID_HANDLE_VALUE) return Fail(error, L"Enumerate present keyboard class interfaces");
    paths.clear();
    for (DWORD index = 0; ; ++index) {
        if (canceled && canceled()) { error = L"Keyboard discovery canceled"; return false; }
        SP_DEVICE_INTERFACE_DATA data{}; data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInterfaces(devices.value, nullptr, &keyboardInterface, index, &data)) {
            const auto code = GetLastError();
            if (code == ERROR_NO_MORE_ITEMS) break;
            return Fail(error, L"Read keyboard interface", code);
        }
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(devices.value, &data, nullptr, 0, &required, nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || required > 1024 * 1024)
            return Fail(error, L"Read keyboard interface size", ERROR_INVALID_DATA);
        std::vector<BYTE> buffer(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devices.value, &data, detail, required, nullptr, nullptr))
            return Fail(error, L"Read keyboard interface path");
        const auto capacity = (required - offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath)) / sizeof(wchar_t);
        const auto end = std::find(detail->DevicePath, detail->DevicePath + capacity, L'\0');
        if (end == detail->DevicePath + capacity) return Fail(error, L"Validate keyboard interface path", ERROR_INVALID_DATA);
        paths.emplace_back(detail->DevicePath, end);
    }
    error.clear(); return true;
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
    bool deviceInterface_ = false;
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
    explicit WindowsBackend(LogicalCapsReader logical = {}, bool deviceInterface = false)
        : logical_(std::move(logical)), deviceInterface_(deviceInterface) {}
    ~WindowsBackend() override { try { std::wstring ignored; Close(ignored); } catch (...) {} }

    bool Open(const std::wstring& target, const Cancel& canceled, std::wstring& error) override
    {
        std::wstring previousError;
        if (!Close(previousError)) { error = previousError; return false; }
        if (deviceInterface_) target_ = target; // Only internal discovery supplies these paths.
        else {
            Config validation; validation.target = target;
            if (!Normalize(validation, error)) return false;
            if (validation.target == L"Auto") { error = L"Automatic keyboard targets require the grouped backend"; return false; }
            target_ = validation.target;
        }
        if (canceled && canceled()) { error = L"Keyboard open canceled"; return false; }
        std::wstring mutexName;
        if (!windows_detail::DeviceMutexName(target_, mutexName, error)) return false;
        mutex_ = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (!mutex_) return windows_detail::Fail(error, L"Create keyboard ownership mutex");
        const DWORD wait = WaitForSingleObject(mutex_, 0);
        ownsMutex_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        if (!ownsMutex_) { error = L"Another capsblink instance already controls this keyboard target"; return false; }
        std::wstring openPath = target_;
        if (!deviceInterface_) {
            static std::atomic<unsigned long> sequence{0};
            mappingName_ = L"capsblinkKBD_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++sequence);
            if (!DefineDosDeviceW(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, mappingName_.c_str(), target_.c_str()))
                return windows_detail::Fail(error, L"Define keyboard device mapping");
            mapping_ = true;
            openPath = L"\\\\.\\" + mappingName_;
        }
        auto handle = std::make_shared<windows_detail::DeviceHandle>();
        handle->value = CreateFileW(openPath.c_str(), GENERIC_WRITE, 0, nullptr,
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
{
    return std::make_unique<GroupBackend>(windows_detail::DiscoverKeyboards,
        [logical = std::move(logical)](const std::wstring&, bool discoveredInterface) {
            return std::make_unique<WindowsBackend>(logical, discoveredInterface);
        });
}
static_assert(sizeof(Indicators) == 4, "Keyboard indicator IOCTL structure must remain two USHORT fields");
} } // namespace aip::caps
#endif
