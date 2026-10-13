// Fresh owned sleeping/synthetic children only. No window, screen, media,
// package, Startup or resident integration API is present in this fixture.
#define NOMINMAX
#include "../dependencies/bounded_capture_process.inc"
#include <shellapi.h>
#include <iostream>
#include <stdexcept>
#include <array>

static int checks = 0;
static void Check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
static std::wstring Executable() {
    wchar_t path[32768]{};
    const auto length = GetModuleFileNameW(nullptr, path, _countof(path));
    if (!length || length >= _countof(path)) throw std::runtime_error("fixture executable unavailable");
    return path;
}
static int Child(const std::wstring& mode, const aip::capture::HelperRequest& request) {
    if (mode == L"sleep") { Sleep(10000); return 0; }
    if (mode == L"crash") return 13;
    auto reply = aip::capture::MakeReply(request, aip::capture::Status::Success);
    reply.width = reply.height = 1;
    BYTE bitmap[58]{};
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42; header.bfSize = sizeof(bitmap); header.bfOffBits = 54;
    BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = info.biHeight = 1;
    info.biPlanes = 1; info.biBitCount = 32; info.biSizeImage = 4;
    memcpy(bitmap, &header, sizeof(header)); memcpy(bitmap + 14, &info, sizeof(info));
    bitmap[54] = 17; bitmap[55] = 34; bitmap[56] = 51; bitmap[57] = 255;
    DWORD written = 0;
    const auto output = reinterpret_cast<HANDLE>(request.outputHandle), pipe = reinterpret_cast<HANDLE>(request.replyHandle);
    if (mode == L"blank") { reply.status = aip::capture::Status::CaptureFailed; reply.flags = aip::capture::RejectedBlank; }
    else if (mode == L"small") {
        reply.status = aip::capture::Status::CaptureFailed; reply.flags = aip::capture::RejectedTooSmall;
        reply.width = 0; reply.height = 1;
    } else if (mode == L"bad-reply") ++reply.requestId;
    else if (mode == L"partial") WriteFile(output, bitmap, 20, &written, nullptr);
    else if (mode == L"success") WriteFile(output, bitmap, sizeof(bitmap), &written, nullptr);
    else return 2;
    WriteFile(pipe, &reply, sizeof(reply), &written, nullptr);
    CloseHandle(output); CloseHandle(pipe);
    return reply.status == aip::capture::Status::Success ? 0 : 1;
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 5 && std::wstring(argv[1]) == L"--fixture-parent-exit") {
        std::uint64_t outerHandle = 0, responseHandle = 0;
        if (!aip::capture::ParseUnsigned(argv[2], UINTPTR_MAX, outerHandle) || !aip::capture::ParseUnsigned(argv[3], UINTPTR_MAX, responseHandle)) return 2;
        aip::capture::ProcessRequest options; options.executable = Executable(); options.destination = argv[4];
        options.deadline = GetTickCount64() + 2000; options.helper.window = options.helper.windowProcessId = 1;
        options.helper.minimumWidth = options.helper.minimumHeight = 1; options.helper.requestId = 123;
        options.arguments = [](const auto& helper) { return L"--fixture-child sleep " + aip::capture::HelperArguments(helper); };
        options.started = [&](DWORD, HANDLE child) {
            HANDLE observed = nullptr;
            if (!DuplicateHandle(GetCurrentProcess(), child, reinterpret_cast<HANDLE>(outerHandle), &observed, SYNCHRONIZE, FALSE, 0)) ExitProcess(3);
            const auto value = reinterpret_cast<std::uint64_t>(observed); DWORD written = 0;
            if (!WriteFile(reinterpret_cast<HANDLE>(responseHandle), &value, sizeof(value), &written, nullptr) || written != sizeof(value)) ExitProcess(4);
            ExitProcess(0); // Intentionally skip C++ cleanup; the capture Job must kill its child.
        };
        aip::capture::RunOwnedProcess(options); return 5;
    }
    if (argc >= 3 && std::wstring(argv[1]) == L"--fixture-child") {
        std::vector<std::wstring> args{argv[0]};
        for (int i = 3; i < argc; ++i) args.emplace_back(argv[i]);
        aip::capture::HelperRequest request; bool recognized = false;
        if (!aip::capture::ParseHelperArguments(args, request, recognized)) return 2;
        return Child(argv[2], request);
    }
    std::wstring directory;
    try {
        using namespace aip::capture;
        wchar_t temporary[32768]{};
        const auto size = GetTempPathW(_countof(temporary), temporary);
        Check(size && size < _countof(temporary), "fixture temporary directory available");
        directory = std::wstring(temporary) + L"AIProjects-CaptureFixture-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        Check(CreateDirectoryW(directory.c_str(), nullptr) != FALSE, "fresh isolated fixture directory created");
        const auto destination = directory + L"\\snapshot.bmp";
        const auto executable = Executable();
        unsigned sequence = 0;
        auto run = [&](const std::wstring& mode, unsigned timeout, Cancel canceled = {}, bool rejectJob = false, bool throwStarted = false) {
            ProcessRequest options; options.executable = executable; options.destination = destination; options.deadline = GetTickCount64() + timeout;
            options.helper.window = 1; options.helper.windowProcessId = 1; options.helper.minimumWidth = options.helper.minimumHeight = 1;
            options.helper.requestId = ++sequence; options.canceled = std::move(canceled);
            options.arguments = [mode](const auto& helper) { return L"--fixture-child " + mode + L" " + HelperArguments(helper); };
            aip::UniqueKernelHandle child;
            const auto observe = [&](HANDLE process) {
                HANDLE duplicate = nullptr;
                if (!DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &duplicate, SYNCHRONIZE, FALSE, 0))
                    throw std::runtime_error("owned child observation handle unavailable");
                child.Reset(duplicate);
            };
            if (rejectJob) options.assignJob = [&](HANDLE, HANDLE process) { observe(process); SetLastError(ERROR_ACCESS_DENIED); return false; };
            options.started = [&](DWORD, HANDLE process) {
                observe(process);
                if (throwStarted) throw std::runtime_error("synthetic parent-side exception");
            };
            const auto before = GetTickCount64();
            const auto reply = RunOwnedProcess(options);
            Check(GetTickCount64() - before < timeout + 1800ull, "owned process work and cleanup remain inside the private deadline margin");
            Check(!child || WaitForSingleObject(child.Get(), 0) == WAIT_OBJECT_0, "the freshly-created observed child has exited before returning");
            return reply;
        };
        auto success = run(L"success", 2000);
        Check(success.status == Status::Success && process_detail::ValidBitmap(destination, success), "synthetic child output publishes only after validated reply and BMP header");
        std::array<BYTE, 58> baseline{};
        {
            aip::UniqueKernelHandle file(CreateFileW(destination.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            DWORD read = 0;
            Check(file && ReadFile(file.Get(), baseline.data(), static_cast<DWORD>(baseline.size()), &read, nullptr) && read == baseline.size(),
                "record the complete synthetic last-good bitmap for preservation checks");
        }
        const auto preserved = [&] {
            aip::UniqueKernelHandle file(CreateFileW(destination.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            std::array<BYTE, 58> bytes{}; DWORD read = 0;
            return file && ReadFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size() && bytes == baseline;
        };
        Check(run(L"sleep", 150).status == Status::TimedOut && preserved(), "a hanging capture child is killed while its last-good destination remains intact");
        const auto cancellationTime = GetTickCount64() + 100;
        Check(run(L"sleep", 2000, [cancellationTime] { return GetTickCount64() >= cancellationTime; }).status == Status::Canceled && preserved(),
            "shutdown cancellation kills only the owned child and keeps published output");
        Check(run(L"success", 2000, {}, true).status == Status::ProcessFailed && preserved(), "Job assignment failure kills the suspended child instead of running it uncontained");
        Check(run(L"sleep", 2000, {}, false, true).status == Status::ProcessFailed && preserved(), "a parent-side exception still reaps its owned sleeping child");
        Check(run(L"crash", 2000).status == Status::InvalidReply && preserved(), "a crashed child cannot publish incomplete output");
        Check(run(L"bad-reply", 2000).status == Status::InvalidReply && preserved(), "a mismatched request ID cannot replace the published bitmap");
        Check(run(L"partial", 2000).status == Status::InvalidBitmap && preserved(), "a success reply with truncated BMP is rejected before publishing");
        const auto blank = run(L"blank", 2000);
        Check(blank.status == Status::CaptureFailed && (blank.flags & RejectedBlank) && preserved(), "blank rejection metadata crosses the helper boundary without altering output");
        const auto small = run(L"small", 2000);
        Check(small.status == Status::CaptureFailed && (small.flags & RejectedTooSmall) && small.minimumWidth == 1 && small.height == 1 && preserved(),
            "dimension rejection metadata crosses the helper boundary intact");
        {
            // The outer Job is only a fixture safety net. Its two-process
            // budget permits the fresh parent and that parent's capture child.
            // Keeping it open while testing proves the INNER production Job,
            // not this safety net, kills the child when its parent exits.
            process_detail::Session parent;
            parent.job.Reset(CreateJobObjectW(nullptr, nullptr));
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
            limits.BasicLimitInformation.ActiveProcessLimit = 2;
            Check(parent.job && SetInformationJobObject(parent.job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
                "parent-death fixture installs an independent two-process safety net");
            SECURITY_ATTRIBUTES inherited{}; inherited.nLength = sizeof(inherited); inherited.bInheritHandle = TRUE;
            HANDLE reader = nullptr, writer = nullptr, self = nullptr;
            Check(CreatePipe(&reader, &writer, &inherited, 4096) &&
                DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &self, PROCESS_DUP_HANDLE, TRUE, 0),
                "parent-death fixture inherits only a response pipe and handle to this fixture process");
            aip::UniqueKernelHandle read(reader), write(writer), outer(self);
            Check(SetHandleInformation(read.Get(), HANDLE_FLAG_INHERIT, 0), "parent-death response reader stays private");
            HANDLE handles[] = {write.Get(), outer.Get()};
            process_detail::Attributes attributes;
            Check(attributes.Initialize(handles, _countof(handles)), "parent-death helper has an explicit inherited-handle list");
            STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes.list;
            startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW; startup.StartupInfo.wShowWindow = SW_HIDE;
            auto command = aip::QuoteCommandLineArg(executable) + L" --fixture-parent-exit " +
                std::to_wstring(reinterpret_cast<std::uintptr_t>(outer.Get())) + L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(write.Get())) +
                L" " + aip::QuoteCommandLineArg(directory + L"\\parent-death.bmp");
            PROCESS_INFORMATION process{};
            Check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &process),
                "parent-death helper is created suspended as a fresh fixture child");
            parent.process.Reset(process.hProcess); parent.thread.Reset(process.hThread);
            Check(AssignProcessToJobObject(parent.job.Get(), parent.process.Get()), "parent-death helper is contained before its first instruction");
            write.Reset(); outer.Reset();
            Check(ResumeThread(parent.thread.Get()) != static_cast<DWORD>(-1), "start the isolated parent-death fixture");
            parent.reaped = WaitForSingleObject(parent.process.Get(), 3000) == WAIT_OBJECT_0;
            Check(parent.reaped, "the test parent exits without C++ destructors within its own deadline");
            DWORD code = 0, available = 0, got = 0; std::uint64_t observed = 0;
            Check(GetExitCodeProcess(parent.process.Get(), &code) && code == 0 && PeekNamedPipe(read.Get(), nullptr, 0, nullptr, &available, nullptr) &&
                available == sizeof(observed) && ReadFile(read.Get(), &observed, sizeof(observed), &got, nullptr) && got == sizeof(observed) && observed,
                "the parent transfers a synchronization handle for only its newly-created capture child");
            aip::UniqueKernelHandle capture(reinterpret_cast<HANDLE>(observed));
            Check(WaitForSingleObject(capture.Get(), 1000) == WAIT_OBJECT_0,
                "parent process death alone closes the production capture Job and terminates its sleeping child");
            WIN32_FIND_DATAW abandoned{};
            HANDLE stage = FindFirstFileW((directory + L"\\parent-death.bmp.capture-*.tmp").c_str(), &abandoned);
            if (stage != INVALID_HANDLE_VALUE) {
                do { Check(DeleteFileW((directory + L"\\" + abandoned.cFileName).c_str()), "remove only the parent-death fixture's abandoned staging file"); }
                while (FindNextFileW(stage, &abandoned));
                FindClose(stage);
            }
        }
        WIN32_FIND_DATAW entry{};
        HANDLE find = FindFirstFileW((destination + L".capture-*.tmp").c_str(), &entry);
        Check(find == INVALID_HANDLE_VALUE, "owned staging files are removed after success, timeout, cancellation and failure");
        if (find != INVALID_HANDLE_VALUE) FindClose(find);
        Check(DeleteFileW(destination.c_str()) && RemoveDirectoryW(directory.c_str()), "only the fixture's named output and own directory are removed");
        std::cout << "Bounded capture process: " << checks << " checks passed; no window APIs exercised\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
