// Inert tests of generated excerpts from the actual candidate source.
// Windows images are copied into ordinary data buffers, never loaded/executed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

void ModLog(const wchar_t*, ...) {}
#include "logonui-resolver-excerpt.inc"

namespace RollbackTest {
enum class Resource : uintptr_t { Stop = 1, Battery, BatteryResolver, ClockResolver, Hook };
HANDLE handle(Resource resource) { return reinterpret_cast<HANDLE>(resource); }
std::atomic<bool> g_unloading{false};
std::atomic<DWORD> g_lateAttachProbeThreadId{0}, g_hotCleanupProbeThreadId{0};
HANDLE g_stopEvent = nullptr, g_batteryNotificationThread = nullptr;
HANDLE g_batteryResolverThread = nullptr, g_classicClockResolverThread = nullptr;
HANDLE g_hookThread = nullptr;
bool failEvent = false, failHook = true, needBattery = true, stopSignaled = false;
bool classRegistered = false;
std::vector<HANDLE> liveWorkers;
std::vector<HANDLE> joinedWorkers;
unsigned checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void ClearKnownXamlThreads() {}
void LoadSettings() {}
void ModLog(const wchar_t*, ...) {}
DWORD GetLastError() { return ERROR_NOT_ENOUGH_MEMORY; }
HANDLE CreateEventW(void*, BOOL, BOOL, const wchar_t*) {
    return failEvent ? nullptr : handle(Resource::Stop);
}
BOOL SetEvent(HANDLE event) {
    require(event == g_stopEvent && event == handle(Resource::Stop), "invalid stop event");
    stopSignaled = true;
    return TRUE;
}
DWORD WaitForSingleObject(HANDLE worker, DWORD) {
    require(g_stopEvent == handle(Resource::Stop), "stop event closed before worker exit");
    require(stopSignaled, "worker waited without signaling stop");
    require(std::find(liveWorkers.begin(), liveWorkers.end(), worker) != liveWorkers.end(),
            "wait did not target a live worker");
    joinedWorkers.push_back(worker);
    return WAIT_OBJECT_0;
}
BOOL CloseHandle(HANDLE resource) {
    if (resource == handle(Resource::Stop)) {
        require(liveWorkers.empty(), "stop event closed with surviving workers");
        require(!classRegistered, "battery window class survived rollback");
    } else {
        const auto worker = std::find(liveWorkers.begin(), liveWorkers.end(), resource);
        require(worker != liveWorkers.end(), "closed unknown worker");
        const auto joined = std::find(joinedWorkers.begin(), joinedWorkers.end(), resource);
        require(joined != joinedWorkers.end(), "closed a worker handle before joining it");
        joinedWorkers.erase(joined);
        liveWorkers.erase(worker);
    }
    return TRUE;
}
void start(HANDLE& worker, Resource resource) {
    worker = handle(resource);
    liveWorkers.push_back(worker);
}
void StartBatteryFlyoutSymbolPrefetch() { start(g_batteryResolverThread, Resource::BatteryResolver); }
void StartClassicClockAbiPrefetch() { start(g_classicClockResolverThread, Resource::ClockResolver); }
void EnsureBatteryNotificationWatcherStarted() {
    if (needBattery) {
        start(g_batteryNotificationThread, Resource::Battery);
        classRegistered = true;
    }
}
constexpr const wchar_t* kBatteryNotifyClass = L"inert-test-class";
LRESULT CALLBACK BatteryNotifyWndProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
void UnregisterPrivateProcessWindowClass(const wchar_t*, WNDPROC) {
    require(!g_batteryNotificationThread, "unregistered battery class before joining worker");
    classRegistered = false;
}
DWORD WINAPI HookThreadProc(void*) { return 0; }
HANDLE CreateThread(void*, SIZE_T, LPTHREAD_START_ROUTINE, void*, DWORD, DWORD*) {
    if (failHook) return nullptr;
    start(g_hookThread, Resource::Hook);
    return g_hookThread;
}
namespace UserStatus {
BOOL Initialize() { return TRUE; }
void BeforeUninit() { require(stopSignaled, "UserStatus teardown began before shared stop"); }
void Uninitialize() {}
}
#include "logonui-init-excerpt.inc"

void reset() {
    g_unloading = false;
    g_stopEvent = g_batteryNotificationThread = g_batteryResolverThread = nullptr;
    g_classicClockResolverThread = g_hookThread = nullptr;
    stopSignaled = classRegistered = false;
    liveWorkers.clear();
    joinedWorkers.clear();
}
void run() {
    for (bool watcher : {false, true}) {
        reset();
        failEvent = false;
        failHook = true;
        needBattery = watcher;
        require(!Wh_ModInit(), "injected hook-thread failure unexpectedly succeeded");
        require(stopSignaled, "rollback did not signal stop");
        require(!g_stopEvent && liveWorkers.empty(), "rollback retained event or worker");
        require(!g_batteryNotificationThread && !classRegistered, "battery rollback incomplete");
    }
    reset();
    failEvent = true;
    require(!Wh_ModInit(), "injected event failure unexpectedly succeeded");
    require(liveWorkers.empty(), "event failure started producers");
    reset();
    failEvent = false;
    failHook = false;
    needBattery = true;
    require(Wh_ModInit(), "successful mock startup failed");
    require(!stopSignaled && liveWorkers.size() == 4, "normal startup unexpectedly rolled back");
    reset(); // Mock resources only; no OS worker/handle was created.
    std::printf("Initialization rollback: %u checks passed (all Win32 effects mocked).\n", checks);
}
}

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static std::vector<BYTE> mapAsData(const wchar_t* path) {
    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    require(stream.good(), "cannot read input image");
    std::vector<BYTE> raw{std::istreambuf_iterator<char>(stream), {}};
    require(raw.size() >= sizeof(IMAGE_DOS_HEADER), "short DOS header");
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    require(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 &&
            static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) <= raw.size(),
            "invalid image header bounds");
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data() + dos->e_lfanew);
    require(nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
            nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC, "input is not AMD64 PE");
    const size_t sectionOffset = reinterpret_cast<const BYTE*>(IMAGE_FIRST_SECTION(nt)) - raw.data();
    require(sectionOffset + nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) <= raw.size(),
            "short section headers");
    require(nt->OptionalHeader.SizeOfImage >= 0x1000 && nt->OptionalHeader.SizeOfImage < 0x10000000,
            "invalid image size");
    std::vector<BYTE> image(nt->OptionalHeader.SizeOfImage);
    require(nt->OptionalHeader.SizeOfHeaders <= raw.size() &&
            nt->OptionalHeader.SizeOfHeaders <= image.size(), "invalid header size");
    std::copy_n(raw.begin(), nt->OptionalHeader.SizeOfHeaders, image.begin());
    auto sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        require(uint64_t(section.PointerToRawData) + section.SizeOfRawData <= raw.size() &&
                uint64_t(section.VirtualAddress) + section.SizeOfRawData <= image.size(),
                "invalid raw section bounds");
        std::copy_n(raw.begin() + section.PointerToRawData, section.SizeOfRawData,
                    image.begin() + section.VirtualAddress);
    }
    return image;
}

static void testImage(const wchar_t* path, DWORD expected) {
    auto image = mapAsData(path);
    auto module = reinterpret_cast<HMODULE>(image.data());
    const auto target = static_cast<BYTE*>(ResolveOnLoadedByStructure(module));
    require(target && static_cast<DWORD>(target - image.data()) == expected,
            "structural resolver disagrees with independently named PDB target");
    StobjectPeView view{};
    require(GetStobjectPeView(module, &view), "mapped data PE rejected");
    auto nt = const_cast<IMAGE_NT_HEADERS64*>(view.nt);
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_ARM64;
    require(!ResolveOnLoadedByStructure(module), "x64 resolver accepted ARM64 machine");
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    const auto first = image[expected];
    image[expected] = 0;
    require(!ResolveOnLoadedByStructure(module), "altered handler prefix still resolved");
    image[expected] = first;
    auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    const auto exceptionSize = directory.Size;
    directory.Size = 0;
    require(!ResolveOnLoadedByStructure(module), "missing unwind table still resolved");
    directory.Size = exceptionSize;
    const auto functions = reinterpret_cast<RUNTIME_FUNCTION*>(image.data() + directory.VirtualAddress);
    const auto functionCount = directory.Size / sizeof(RUNTIME_FUNCTION);
    DWORD originalEnd = 0;
    for (size_t i = 0; i < functionCount; ++i)
        if (functions[i].BeginAddress == expected) originalEnd = functions[i].EndAddress;
    require(originalEnd > expected, "PDB target is not a runtime-function start");
    const auto text = FindPeSectionByName(view, ".text");
    require(text && text->Misc.VirtualSize > 0x1000, "missing test text space");
    DWORD duplicate = text->VirtualAddress + text->Misc.VirtualSize - 0x800;
    require(duplicate != expected && uint64_t(duplicate) + originalEnd - expected <= image.size(),
            "invalid duplicate-candidate test range");
    std::memcpy(image.data() + duplicate, image.data() + expected, 64);
    functions[functionCount - 1].BeginAddress = duplicate;
    functions[functionCount - 1].EndAddress = duplicate + originalEnd - expected;
    require(!ResolveOnLoadedByStructure(module), "ambiguous handler candidates were accepted");
    std::wprintf(L"Resolver: %ls => 0x%lX; architecture, changed-prefix, missing-unwind, ambiguity rejection passed.\n",
                 path, static_cast<unsigned long>(expected));
}

int wmain(int argc, wchar_t** argv) {
    try {
        RollbackTest::run();
        require(argc >= 3 && argc % 2 == 1, "provide image-path / expected-RVA pairs");
        for (int i = 1; i < argc; i += 2) {
            wchar_t* end = nullptr;
            const auto rva = std::wcstoul(argv[i + 1], &end, 0);
            require(end && *end == 0, "invalid expected RVA");
            testImage(argv[i], static_cast<DWORD>(rva));
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
}
