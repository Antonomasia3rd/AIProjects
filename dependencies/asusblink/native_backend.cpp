#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include "service.h"
#include "protocol.h"

namespace aip { namespace asus {
namespace {
class NativeBackend final : public Backend {
    HANDLE device_ = INVALID_HANDLE_VALUE;
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER disk_ = nullptr;
    ULONGLONG lastSample_ = 0;
    std::uint64_t lastBytes_ = 0;
public:
    ~NativeBackend() override {
        if (device_ != INVALID_HANDLE_VALUE) CloseHandle(device_);
        if (query_) PdhCloseQuery(query_);
    }
    bool Apply(Device device, int state, const Cancel& cancel, std::wstring& error) override {
        if (cancel()) { error = L"ASUS request cancelled."; return false; }
        if ((device != Device::Mic && device != Device::Keyboard) || state < 0 || state > (device == Device::Mic ? 1 : 255)) {
            error = L"Invalid ASUS output device/state."; return false;
        }
        // Device access occurs only from an explicitly enabled hardware action.
        if (device_ == INVALID_HANDLE_VALUE) device_ = CreateFileW(L"\\\\.\\ATKACPI", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        if (device_ == INVALID_HANDLE_VALUE) { error = L"Cannot open ASUS ATKACPI (error " + std::to_wstring(GetLastError()) + L")."; return false; }
        auto input = SetRequest(device, state);
        std::array<unsigned char, 16> output{};
        OVERLAPPED request{}; request.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!request.hEvent) { error = L"Cannot create ASUS request completion event."; return false; }
        DWORD bytes = 0;
        BOOL ok = DeviceIoControl(device_, ControlCode, input.data(), static_cast<DWORD>(input.size()), output.data(),
            static_cast<DWORD>(output.size()), &bytes, &request);
        DWORD failure = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && failure == ERROR_IO_PENDING) {
            for (;;) {
                DWORD wait = WaitForSingleObject(request.hEvent, 50);
                if (wait == WAIT_OBJECT_0) { ok = GetOverlappedResult(device_, &request, &bytes, FALSE); break; }
                if (wait == WAIT_FAILED || cancel()) {
                    CancelIoEx(device_, &request);
                    // Retain the request buffers until the driver acknowledges
                    // cancellation. Service::Stop reports a pending timeout if
                    // a broken driver fails to complete; never free live I/O.
                    GetOverlappedResult(device_, &request, &bytes, TRUE);
                    failure = ERROR_OPERATION_ABORTED; ok = FALSE; break;
                }
            }
            if (!ok && failure == ERROR_IO_PENDING) failure = GetLastError();
        }
        CloseHandle(request.hEvent);
        if (!ok) { error = L"ASUS device request failed (error " + std::to_wstring(failure) + L")."; return false; }
        if (!AcceptedResponse(output.data(), bytes)) { error = L"ASUS firmware returned a truncated or rejected response."; return false; }
        error.clear(); return true;
    }
    bool ReadDiskBytes(std::uint64_t& bytes, const Cancel& cancel, std::wstring& error) override {
        if (cancel()) { error = L"Disk sample cancelled."; return false; }
        if (!query_) {
            if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS ||
                PdhAddEnglishCounterW(query_, L"\\PhysicalDisk(_Total)\\Disk Bytes/sec", 0, &disk_) != ERROR_SUCCESS) {
                if (query_) PdhCloseQuery(query_); query_ = nullptr;
                error = L"Disk activity performance counter is unavailable."; return false;
            }
            PdhCollectQueryData(query_); lastSample_ = GetTickCount64(); bytes = 0; return true;
        }
        if (GetTickCount64() - lastSample_ < 200) { bytes = lastBytes_; return true; }
        lastSample_ = GetTickCount64();
        PDH_FMT_COUNTERVALUE value{};
        if (PdhCollectQueryData(query_) != ERROR_SUCCESS || PdhGetFormattedCounterValue(disk_, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS ||
            (value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA) || !std::isfinite(value.doubleValue) || value.doubleValue < 0) {
            error = L"Disk activity sample failed."; return false;
        }
        lastBytes_ = value.doubleValue >= 18446744073709551616.0 ? std::numeric_limits<std::uint64_t>::max() : static_cast<std::uint64_t>(value.doubleValue);
        bytes = lastBytes_; error.clear(); return true;
    }
};
}
std::unique_ptr<Backend> CreateNativeBackend() { return std::unique_ptr<Backend>(new NativeBackend()); }
} }
