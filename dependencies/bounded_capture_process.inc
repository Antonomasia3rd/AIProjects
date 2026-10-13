#pragma once
// Owned, suspended-start child containment for synchronous capture work.
// Uses only the shared handle/command-line primitives; no existing PID attach.
#include "desktop_app_baseline.h"
#include "live_capture_protocol.h"
#include <functional>
#include <atomic>

namespace aip { namespace capture {
using Cancel = std::function<bool()>;
struct ProcessRequest {
    std::wstring executable, destination;
    HelperRequest helper;
    ULONGLONG deadline = 0;
    Cancel canceled;
    // Entire command-line suffix injection is for inert sleeping/synthetic
    // fixture children. Production leaves these callbacks empty.
    std::function<std::wstring(const HelperRequest&)> arguments;
    std::function<bool(HANDLE, HANDLE)> assignJob;
    std::function<void(DWORD, HANDLE)> started;
};
using ProcessRunner = std::function<Reply(const ProcessRequest&)>;
namespace process_detail {
struct Attributes {
    std::vector<BYTE> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    ~Attributes() { if (list) DeleteProcThreadAttributeList(list); }
    bool Initialize(HANDLE* inherited, SIZE_T count) {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        if (!size) return false;
        storage.resize(size);
        auto candidate = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(candidate, 1, 0, &size)) return false;
        list = candidate;
        return UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
            count * sizeof(HANDLE), nullptr, nullptr) != FALSE;
    }
};
struct Session {
    UniqueKernelHandle job, process, thread;
    bool reaped = false, cleanupAttempted = false;
    void Terminate(DWORD code) noexcept {
        if (process && !reaped && !cleanupAttempted) {
            cleanupAttempted = true;
            if (job) TerminateJobObject(job.Get(), code);
            // A failed assignment leaves the child suspended and uncontained.
            // This handle still identifies ONLY the process just created here.
            TerminateProcess(process.Get(), code);
            reaped = WaitForSingleObject(process.Get(), 1000) == WAIT_OBJECT_0;
        }
    }
    ~Session() { if (process && !reaped) Terminate(ERROR_PROCESS_ABORTED); }
};
struct Stage {
    std::wstring path;
    bool owned = false;
    ~Stage() { if (owned) DeleteFileW(path.c_str()); }
};
inline bool ValidBitmap(const std::wstring& path, const Reply& reply) {
    UniqueKernelHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) return false;
    LARGE_INTEGER size{}; BITMAPFILEHEADER header{}; BITMAPINFOHEADER info{}; DWORD read = 0;
    if (!GetFileSizeEx(file.Get(), &size) || size.QuadPart < 54 ||
        !ReadFile(file.Get(), &header, sizeof(header), &read, nullptr) || read != sizeof(header) ||
        !ReadFile(file.Get(), &info, sizeof(info), &read, nullptr) || read != sizeof(info)) return false;
    if (header.bfType != 0x4d42 || header.bfOffBits != 54 || info.biSize != 40 || info.biWidth <= 0 || info.biHeight <= 0 ||
        info.biWidth != reply.width || info.biHeight != reply.height || info.biPlanes != 1 || info.biBitCount != 32 || info.biCompression != BI_RGB ||
        info.biWidth < reply.minimumWidth || info.biHeight < reply.minimumHeight) return false;
    const auto bytes = static_cast<std::uint64_t>(info.biWidth) * static_cast<std::uint64_t>(info.biHeight) * 4;
    return bytes && bytes <= MaximumBitmapBytes && bytes + 54 == static_cast<std::uint64_t>(size.QuadPart) &&
        header.bfSize == bytes + 54 && info.biSizeImage == bytes;
}
} // namespace process_detail

inline Reply RunOwnedProcess(const ProcessRequest& options) {
    auto result = MakeReply(options.helper, Status::ProcessFailed);
    try {
        const auto stopped = [&] { return options.canceled && options.canceled(); };
        if (stopped()) return MakeReply(options.helper, Status::Canceled, ERROR_CANCELLED);
        if (options.executable.empty() || options.destination.empty() || !options.helper.requestId ||
            options.helper.minimumWidth <= 0 || options.helper.minimumHeight <= 0) return MakeReply(options.helper, Status::InvalidRequest, ERROR_INVALID_PARAMETER);
        if (GetTickCount64() >= options.deadline) return MakeReply(options.helper, Status::TimedOut, ERROR_TIMEOUT);
        process_detail::Stage stage;
        stage.path = options.destination + L".capture-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(options.helper.requestId) + L".tmp";
        SECURITY_ATTRIBUTES inherited{}; inherited.nLength = sizeof(inherited); inherited.bInheritHandle = TRUE;
        UniqueKernelHandle output(CreateFileW(stage.path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherited, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!output) { result.error = GetLastError(); return result; }
        stage.owned = true;
        HANDLE readRaw = nullptr, writeRaw = nullptr;
        if (!CreatePipe(&readRaw, &writeRaw, &inherited, 4096)) { result.error = GetLastError(); return result; }
        UniqueKernelHandle read(readRaw), write(writeRaw);
        if (!SetHandleInformation(read.Get(), HANDLE_FLAG_INHERIT, 0)) { result.error = GetLastError(); return result; }
        auto helper = options.helper;
        helper.outputHandle = reinterpret_cast<std::uintptr_t>(output.Get()); helper.replyHandle = reinterpret_cast<std::uintptr_t>(write.Get());
        HANDLE handles[] = {output.Get(), write.Get()};
        process_detail::Attributes attributes;
        if (!attributes.Initialize(handles, _countof(handles))) { result.error = GetLastError(); return result; }
        process_detail::Session session;
        session.job.Reset(CreateJobObjectW(nullptr, nullptr));
        if (!session.job) { result.error = GetLastError(); return result; }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit = 1;
        if (!SetInformationJobObject(session.job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) { result.error = GetLastError(); return result; }
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startup.StartupInfo.wShowWindow = SW_HIDE; startup.lpAttributeList = attributes.list;
        const auto suffix = options.arguments ? options.arguments(helper) : HelperArguments(helper);
        auto command = QuoteCommandLineArg(options.executable) + L" " + suffix;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(options.executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &process)) {
            result.error = GetLastError(); return result;
        }
        session.process.Reset(process.hProcess); session.thread.Reset(process.hThread);
        const bool assigned = options.assignJob ? options.assignJob(session.job.Get(), session.process.Get()) :
            AssignProcessToJobObject(session.job.Get(), session.process.Get()) != FALSE;
        if (!assigned) { result.error = GetLastError(); session.Terminate(ERROR_PROCESS_ABORTED); return result; }
        write.Reset(); output.Reset(); // Only the child's inherited copies remain.
        const bool stoppedBeforeStart = stopped();
        if (stoppedBeforeStart || GetTickCount64() >= options.deadline) {
            result = MakeReply(helper, stoppedBeforeStart ? Status::Canceled : Status::TimedOut, stoppedBeforeStart ? ERROR_CANCELLED : ERROR_TIMEOUT);
            session.Terminate(result.error); return result;
        }
        if (ResumeThread(session.thread.Get()) == static_cast<DWORD>(-1)) { result.error = GetLastError(); session.Terminate(ERROR_PROCESS_ABORTED); return result; }
        if (options.started) options.started(process.dwProcessId, session.process.Get());
        for (;;) {
            if (stopped()) { result = MakeReply(helper, Status::Canceled, ERROR_CANCELLED); break; }
            const auto now = GetTickCount64();
            if (now >= options.deadline) { result = MakeReply(helper, Status::TimedOut, ERROR_TIMEOUT); break; }
            const auto waited = WaitForSingleObject(session.process.Get(), static_cast<DWORD>((std::min)(25ull, options.deadline - now)));
            if (waited == WAIT_OBJECT_0) { session.reaped = true; break; }
            if (waited != WAIT_TIMEOUT) { result.error = GetLastError(); break; }
        }
        if (!session.reaped) { session.Terminate(result.error ? result.error : ERROR_PROCESS_ABORTED); return result; }
        DWORD exit = 0, available = 0, got = 0;
        if (!GetExitCodeProcess(session.process.Get(), &exit) || !PeekNamedPipe(read.Get(), nullptr, 0, nullptr, &available, nullptr) ||
            available != sizeof(Reply) || !ReadFile(read.Get(), &result, sizeof(result), &got, nullptr) || got != sizeof(result) || !ValidReply(helper, result))
            return MakeReply(helper, Status::InvalidReply, ERROR_INVALID_DATA);
        if (result.status != Status::Success) return result;
        if (exit != 0 || !process_detail::ValidBitmap(stage.path, result)) return MakeReply(helper, Status::InvalidBitmap, ERROR_INVALID_DATA);
        const bool stoppedBeforePublish = stopped();
        if (stoppedBeforePublish || GetTickCount64() >= options.deadline)
            return MakeReply(helper, stoppedBeforePublish ? Status::Canceled : Status::TimedOut, stoppedBeforePublish ? ERROR_CANCELLED : ERROR_TIMEOUT);
        if (!MoveFileExW(stage.path.c_str(), options.destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            result.status = Status::CaptureFailed; result.error = GetLastError(); return result;
        }
        stage.owned = false; return result;
    } catch (...) { return MakeReply(options.helper, Status::ProcessFailed, ERROR_NOT_ENOUGH_MEMORY); }
}
} }
