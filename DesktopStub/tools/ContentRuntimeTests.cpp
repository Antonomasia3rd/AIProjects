// Compile the real host code, but never call its entry point. These tests use
// temporary configuration, in-memory menus and snapshots; no AppX, startup,
// notifications, playback or network operations are requested.
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <atomic>
static std::atomic<unsigned> idleTestTrimCalls(0);
static void IdleTestTrim(bool, bool) { ++idleTestTrimCalls; }
#define DESKTOPSTUB_TEST_IDLE_MEMORY_TRIM IdleTestTrim
#include "../../dependencies/content_engine.h"
static bool contentNativeQueueAvailable = false, contentNativeQueueTestActive = false, contentQueueNormalAckEnabled = true;
static std::vector<std::vector<aip::content::Snapshot>> secondaryTestBatches;
static bool secondaryTestPinned = true;
static unsigned galleryConfigurationParses = 0;
#define DESKTOPSTUB_TEST_GALLERY_CONFIGURATION_PARSED() (++galleryConfigurationParses)
static unsigned contentBaseConfigurationParses = 0, contentSecondaryConfigurationParses = 0;
static std::wstring contentCacheSwitchProfileOnParse;
static void ContentTestConfigurationParsed(bool secondary);
#define DESKTOPSTUB_TEST_CONTENT_CONFIGURATION_PARSED(secondary) ContentTestConfigurationParsed(secondary)
static unsigned contentWallpaperBackgroundRequests = 0;
static unsigned contentBackgroundRequests = 0;
static void ContentTestBackgroundRequested(const aip::content::Entry& entry) {
    ++contentBackgroundRequests;
    const auto id = aip::content::BackgroundIdFor(entry.background);
    if (id == aip::content::BackgroundId::Wallpaper || id == aip::content::BackgroundId::LiveWallpaper) ++contentWallpaperBackgroundRequests;
}
#define DESKTOPSTUB_TEST_CONTENT_BACKGROUND_REQUEST(entry) ContentTestBackgroundRequested(entry)
static std::vector<std::wstring> contentAssetCleanupDirectories;
static void ContentTestVersionedAssetCleanup(const std::wstring& directory) { contentAssetCleanupDirectories.push_back(directory); }
#define DESKTOPSTUB_TEST_VERSIONED_ASSET_CLEANUP(directory) ContentTestVersionedAssetCleanup(directory)
static int secondaryTestExistsCalls = 0;
static bool ContentTestSecondaryExists(const std::wstring&) { ++secondaryTestExistsCalls; return secondaryTestPinned; }
static std::vector<aip::content::Snapshot> GalleryTestFrames();
struct GalleryStatusSnapshots;
static std::vector<GalleryStatusSnapshots> GalleryTestStatusWindows();
#define DESKTOPSTUB_TEST_GALLERY_STATUS_WINDOWS() GalleryTestStatusWindows()
static bool GalleryTestValidate(const aip::content::Snapshot&);
static void GalleryTestAck(const aip::content::Snapshot&);
static void GalleryTestFail(const aip::content::Snapshot&);
static bool GallerySetupTestOpenFolder(const std::wstring&);
static bool GallerySetupTestDirectoryExists(const std::wstring&);
static bool galleryUiClearSucceeds=false;
static std::vector<std::wstring> galleryUiClearedIds;
static bool GalleryUiTestClear(const std::vector<std::wstring>& ids,std::wstring& error) {
    galleryUiClearedIds=ids;if(!galleryUiClearSucceeds){error=L"Synthetic clear failure";return false;}error.clear();return true;
}
#define DESKTOPSTUB_TEST_GALLERY_CLEAR_QUEUES GalleryUiTestClear
#define DESKTOPSTUB_TEST_GALLERY_FRAMES GalleryTestFrames
#define DESKTOPSTUB_TEST_GALLERY_VALIDATE GalleryTestValidate
#define DESKTOPSTUB_TEST_GALLERY_ACK GalleryTestAck
#define DESKTOPSTUB_TEST_GALLERY_FAIL GalleryTestFail
#define DESKTOPSTUB_TEST_GALLERY_OPEN_FOLDER GallerySetupTestOpenFolder
#define DESKTOPSTUB_TEST_GALLERY_DIRECTORY_EXISTS GallerySetupTestDirectoryExists
static int secondaryAppearancePosts = 0, secondaryAppearanceRefreshes = 0;
static bool secondaryAppearancePostSucceeds = true;
static HWND secondaryAppearanceDestination = nullptr;
static UINT secondaryAppearanceMessage = 0;
static bool ContentTestAppearancePost(HWND window, UINT message) {
    ++secondaryAppearancePosts; secondaryAppearanceDestination = window; secondaryAppearanceMessage = message;
    return secondaryAppearancePostSucceeds;
}
static void ContentTestAppearanceRefresh(HWND window) { ++secondaryAppearanceRefreshes; secondaryAppearanceDestination = window; }
#define DESKTOPSTUB_TEST_APPEARANCE_POST(window, message) ContentTestAppearancePost(window, message)
#define DESKTOPSTUB_TEST_APPEARANCE_REFRESH(window) ContentTestAppearanceRefresh(window)
static unsigned long long ContentTestSecondaryBatch(std::vector<aip::content::Snapshot>, std::wstring);
#define DESKTOPSTUB_TEST_SECONDARY_EXISTS(id) ContentTestSecondaryExists(id)
#define DESKTOPSTUB_SECONDARY_BATCH_GENERATE ContentTestSecondaryBatch
static std::vector<std::vector<aip::content::Snapshot>> contentQueueBatches;
static void ContentTestQueueAcknowledgeNormal();
static unsigned long long ContentTestQueueBatch(std::vector<aip::content::Snapshot>, std::wstring);
#define DESKTOPSTUB_TEST_NATIVE_QUEUE_AVAILABLE() contentNativeQueueAvailable
#define DESKTOPSTUB_CONTENT_QUEUE_BATCH_GENERATE ContentTestQueueBatch
static std::vector<std::wstring> contentFileDialogFilters;
static BOOL ContentTestOpenFileName(OPENFILENAMEW* dialog)
{
    contentFileDialogFilters.clear();
    const wchar_t* current = dialog->lpstrFilter;
    for (int pairs = 0; current && *current && pairs < 8; ++pairs)
    {
        contentFileDialogFilters.emplace_back(current);
        current += contentFileDialogFilters.back().size() + 1;
    }
    return FALSE; // Inspect the actual dialog request; never show a window.
}
#define GetOpenFileNameW ContentTestOpenFileName
static int contentDialogs = 0;
static int contentDialogResponse = IDNO;
static std::wstring contentDialogText;
static int ContentTestMessageBox(HWND, LPCWSTR text, LPCWSTR, UINT flags)
{
    ++contentDialogs;
    contentDialogText = text ? text : L"";
    return (flags & MB_YESNO) ? contentDialogResponse : IDOK;
}
#define DESKTOPSTUB_CONTENT_MESSAGEBOX ContentTestMessageBox
static int ContentTestPagedDialog(HWND, const wchar_t*, const std::vector<std::wstring>&) { return IDOK; }
#define DESKTOPSTUB_TEST_PAGED_TEXT_DIALOG ContentTestPagedDialog
static unsigned long long contentGenerationRequests = 0;
static std::wstring contentQueuedBackground;
static unsigned long long ContentTestQueueGenerate(std::wstring background, std::wstring)
{
    contentQueuedBackground = std::move(background);
    if (contentNativeQueueTestActive) ContentTestQueueAcknowledgeNormal();
    return ++contentGenerationRequests;
}
#define DESKTOPSTUB_CONTENT_QUEUE_GENERATE ContentTestQueueGenerate
#define DESKTOPSTUB_INERT_HARDWARE
static int contentLogicalCaps = 0, contentLogicalSamples = 0;
static int ContentTestLogicalCaps() { ++contentLogicalSamples; return contentLogicalCaps; }
#define DESKTOPSTUB_TEST_LOGICAL_CAPS ContentTestLogicalCaps
#define wWinMain DesktopStubUnusedEntryPoint
static std::wstring contentTestCommandLine = L"DesktopStub.exe";
static LPWSTR ContentTestGetCommandLine() { return contentTestCommandLine.data(); }
#define GetCommandLineW ContentTestGetCommandLine
#include "../DesktopStub.cpp"
#undef wWinMain
#include <iostream>
#include <map>

static int checks = 0, failures = 0;
static void ContentTestConfigurationParsed(bool secondary)
{
    if (secondary) ++contentSecondaryConfigurationParses;
    else {
        ++contentBaseConfigurationParses;
        if (!contentCacheSwitchProfileOnParse.empty()) {
            std::lock_guard<std::mutex> lock(g_pathMutex);
            g_iniPath = std::move(contentCacheSwitchProfileOnParse); contentCacheSwitchProfileOnParse.clear();
        }
    }
}
static void Check(bool value, const char* name)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}
static void ContentTestQueueAcknowledgeNormal() { if (contentQueueNormalAckEnabled) AcknowledgeContentQueueFrame({}); }
static unsigned long long ContentTestSecondaryBatch(std::vector<aip::content::Snapshot> frames, std::wstring) {
    secondaryTestBatches.push_back(frames);
    for (const auto& frame : frames) {
        if (frame.galleryFrame) ContentAcknowledgeGalleryFrame(frame);
        AcknowledgeSecondaryFrame(frame);
    }
    return ++contentGenerationRequests;
}
static unsigned long long ContentTestQueueBatch(std::vector<aip::content::Snapshot> snapshots, std::wstring)
{
    contentQueueBatches.push_back(snapshots);
    for (const auto& snapshot : snapshots) AcknowledgeContentQueueFrame(snapshot);
    return ++contentGenerationRequests;
}

struct TrayMenuProbe { std::wstring path; UINT state = 0; };
using TrayMenuCommands = std::map<UINT, TrayMenuProbe>;
static void InspectTrayMenu(HMENU menu, TrayMenuCommands& commands, const std::wstring& parent = L"")
{
    for (int position = 0; position < GetMenuItemCount(menu); ++position) {
        wchar_t label[2048]{};
        MENUITEMINFOW item{}; item.cbSize = sizeof(item);
        item.fMask = MIIM_SUBMENU | MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_STRING;
        item.dwTypeData = label; item.cch = _countof(label);
        Check(GetMenuItemInfoW(menu, position, TRUE, &item) != FALSE, "menu item can be inspected without opening a popup");
        const auto path = parent.empty() ? std::wstring(label) : parent + L" / " + label;
        if (item.hSubMenu) InspectTrayMenu(item.hSubMenu, commands, path);
        else if (!(item.fType & MFT_SEPARATOR) && item.wID && item.wID != static_cast<UINT>(-1))
            Check(commands.emplace(item.wID, TrayMenuProbe{path, item.fState}).second, "every command appears exactly once in the menu tree");
    }
}

static bool WriteContentFixture(const std::wstring& text)
{
    if (!WriteUtf8BomTextFile(g_iniPath, text)) return false;
    // External edits are intentionally cached by the host for 250 ms. Fixture
    // changes must observe the newly written file before checking its behavior,
    // rather than accidentally exercising whichever preceding profile is cached.
    std::wstring loaded;
    return ReadIniFileForMutation(loaded) && loaded == text;
}
// Legacy compatibility fixtures intentionally exercise the numbered-content
// model. Their complete defaults must not accidentally opt into the fresh
// installation's independent Desktop-only schema.
static std::wstring BuildLegacyContentFixtureTemplate()
{
    auto text=BuildInitialIniTemplate();
    aip::RemoveIniSectionFromText(text,L"Tiles");
    aip::RemoveIniSectionFromText(text,L"Tile.desktop");
    return text;
}
#include "PrimaryDesktopDefaultsHostTests.inc"

#include "DisplaySettingsTests.inc"
#include "ContentPresetHostTests.inc"
#include "NativeQueueHostTests.inc"
#include "SecondaryTileHostTests.inc"
#include "GalleryHostTests.inc"
#include "GalleryStatusRetentionHostTests.inc"
#include "GalleryLedgerHostTests.inc"
#include "GallerySetupHostTests.inc"
#include "GalleryPinBatchHostTests.inc"
#include "TileStatusHostTests.inc"
#include "GalleryUiFlowHostTests.inc"
#include "PrimaryAssetRetentionHostTests.inc"
#include "PagedTextDialogHostTests.inc"
#include "IdleMaintenanceHostTests.inc"
#include "GalleryCpuHostTests.inc"
#include "ContentCpuCacheHostTests.inc"
#include "SourceTileRenameHostTests.inc"
#include "PublishedSecondaryAssetReuseHostTests.inc"
#include "PrayerCacheHostTests.inc"
#include "PrayerTimesHostTests.inc"
#include "TextPresentationHostTests.inc"
#include "TileLifecycleHostTests.inc"
#include "GenerationAdmissionHostTests.inc"
#include "TileRegistrationHostTests.inc"
#include "TileInstanceRuntimeHostTests.inc"
#include "TileInstanceActivationHostTests.inc"
#include "TileInstanceTrayHostTests.inc"
#include "SourceAlertHostTests.inc"
#include "GalleryImageActivationHostTests.inc"
#include "DateTimeTileHostTests.inc"
#include "DesktopHostSetupHostTests.inc"
#include "FreshPresetPreviewHostTests.inc"

static bool SeedContentFixtureDefaults()
{
    std::wstring original;
    if (!ReadIniFileForMutation(original)) return false;
    auto complete = BuildLegacyContentFixtureTemplate();
    for (const auto& section : aip::ParseIniDocument(original))
        for (const auto& entry : section.entries)
            if (!aip::WriteIniValueToText(complete, section.name.c_str(), entry.key.c_str(), entry.value)) return false;
    // Import behavior uses the effective source/host values above. Missing
    // default initialization is exercised separately by the persistence group.
    return WriteContentFixture(complete);
}

template<class Ready> static bool HostUntil(Ready ready)
{
    const auto deadline = steady_clock::now() + milliseconds(2500);
    while (!ready() && steady_clock::now() < deadline) std::this_thread::sleep_for(milliseconds(1));
    return ready();
}
struct HostGate {
    std::atomic<bool> entered{false}, release{false}, fail{false};
    std::atomic<int> created{0}, destroyed{0}, active{0}, maximum{0};
    void Open() {
        ++created; const int count = ++active; int old = maximum.load();
        while (old < count && !maximum.compare_exchange_weak(old, count)) {}
    }
    void Wait(const std::function<bool()>& cancel) {
        entered = true;
        const auto deadline = steady_clock::now() + milliseconds(2000);
        while (!release && !cancel() && steady_clock::now() < deadline) std::this_thread::sleep_for(milliseconds(1));
    }
};
class HostAsusBackend final : public aip::asus::Backend {
    std::shared_ptr<HostGate> gate_;
public:
    explicit HostAsusBackend(std::shared_ptr<HostGate> gate) : gate_(std::move(gate)) { gate_->Open(); }
    ~HostAsusBackend() override { --gate_->active; ++gate_->destroyed; }
    bool Apply(aip::asus::Device, int, const aip::asus::Cancel&, std::wstring& error) override {
        // Simulate outstanding driver cleanup after cancellation, but always
        // finish within a private deadline even if an assertion fails.
        gate_->Wait([] { return false; });
        if (gate_->fail) { error = L"Synthetic firmware failure"; return false; }
        return true;
    }
    bool ReadDiskBytes(std::uint64_t& value, const aip::asus::Cancel&, std::wstring&) override { value = 0; return true; }
};
class HostDiscordTransport final : public aip::discord::Transport {
    std::shared_ptr<HostGate> gate_;
public:
    explicit HostDiscordTransport(std::shared_ptr<HostGate> gate) : gate_(std::move(gate)) { gate_->Open(); }
    ~HostDiscordTransport() override { --gate_->active; ++gate_->destroyed; }
    void Send(const std::string&, const std::wstring&, const aip::discord::Cancel&) override {}
    void Clear(const std::wstring&, const aip::discord::Cancel& cancel) override { gate_->Wait(cancel); }
};
static void TestServiceHostLifecycles()
{
    {
        auto gate = std::make_shared<HostGate>();
        ContentAsusProvider provider;
        provider.configureTestOptions = [gate](aip::asus::Options& options) {
            options.backendFactory = [gate] { return std::make_unique<HostAsusBackend>(gate); };
        };
        aip::content::AsusConfiguration cfg;
        cfg.hardwareEnabled = true; // Fake backend only; native backend is not linked.
        cfg.settings = {{L"MicState", L"0,1"}, {L"MicInterval", L"100"}, {L"MicDuration", L"once"}};
        provider.Refresh(cfg);
        Check(HostUntil([&] { return gate->entered.load(); }), "ASUS wrapper starts the explicitly injected fake backend");
        const auto before = steady_clock::now();
        provider.Cancel();
        Check(provider.stopping && provider.key.empty() && steady_clock::now() - before < milliseconds(500),
            "ASUS cancellation immediately invalidates its key without waiting for pending cleanup");
        provider.Refresh(cfg);
        Check(provider.stopping && gate->created == 1 && provider.Read().badge.empty() &&
            provider.Read().secondary.find(L"Stopping") != std::wstring::npos,
            "same-key ASUS reenable waits for cleanup and exposes stopping as normal tile text");
        gate->release = true;
        Check(HostUntil([&] { provider.Refresh(cfg); return !provider.stopping && gate->created == 2; }),
            "same-key ASUS reenable starts again after asynchronous Stop completes");
        Check(gate->maximum == 1 && provider.error.empty(), "ASUS wrapper never overlaps old and replacement backend ownership");
        provider.restart = true;
        Check(HostUntil([&] { provider.Refresh(cfg); return gate->created >= 3 && !provider.stopping; }),
            "explicit ASUS restart works without modifying the persisted profile key");
        auto invalid = cfg;
        invalid.settings[L"MicState"] = L"invalid";
        provider.Refresh(invalid);
        Check(!provider.error.empty() && provider.Read().badge.empty(), "invalid ASUS settings report an ordinary text error and stop old actions");
        Check(HostUntil([&] { provider.Refresh(invalid); return !provider.stopping && !provider.service->Read().running; }),
            "invalid ASUS profile cleanup completes while the invalid key remains cached");
        const int invalidCount = gate->created;
        provider.Refresh(invalid);
        Check(gate->created == invalidCount && !provider.error.empty(), "invalid ASUS configuration does not churn restart attempts");
        Check(HostUntil([&] { provider.Refresh(cfg); return provider.started && gate->created > invalidCount; }),
            "repairing an ASUS profile resumes after the rejected profile stopped");
        auto fault = cfg;
        fault.settings[L"ErrorRetry"] = L"0"; fault.settings[L"ErrorAction"] = L"stop";
        gate->fail = true;
        provider.Refresh(fault);
        Check(HostUntil([&] { return !provider.service->Read().running; }), "ASUS Stop error policy reaches a terminal worker state");
        const int faultCount = gate->created;
        for (int i = 0; i < 20; ++i) provider.Refresh(fault);
        Check(gate->created == faultCount && !provider.started, "same-key polling respects the ASUS Stop error policy instead of auto-restarting hardware");
        gate->fail = false; provider.restart = true;
        Check(HostUntil([&] { provider.Refresh(cfg); return provider.started && gate->created > faultCount; }),
            "explicit restart recovers a terminal ASUS service");
        provider.Stop();
        Check(gate->active == 0 && provider.key.empty(), "ASUS wrapper Stop clears ownership and its cached request");
    }
    {
        auto gate = std::make_shared<HostGate>();
        ContentDiscordProvider provider;
        provider.configureTestOptions = [gate](aip::discord::Options& options) {
            options.contextOverride = std::make_shared<aip::discord::Context>();
            options.transportFactory = [gate] { return std::make_unique<HostDiscordTransport>(gate); };
        };
        aip::content::DiscordConfiguration cfg;
        cfg.send = true; cfg.clientId = L"123456789012345678";
        cfg.details = L"Synthetic details"; cfg.state = L"Synthetic state";
        provider.Refresh(cfg, false);
        Check(HostUntil([&] { return provider.service->Read().phase == aip::discord::Phase::Active; }),
            "Discord wrapper uses fake transport and synthetic context exclusively");
        provider.Cancel();
        Check(HostUntil([&] { return gate->entered.load(); }) && provider.stopping && provider.key.empty(),
            "Discord cancel clears its cache while fake presence cleanup remains pending");
        cfg.details = L"Replacement details";
        provider.Refresh(cfg, false);
        Check(provider.stopping && provider.error.empty() && gate->created == 1,
            "Discord reconfiguration does not Reload an already canceled worker");
        gate->release = true;
        Check(HostUntil([&] { provider.Refresh(cfg, false); return gate->created == 2 &&
            provider.service->Read().details == cfg.details && provider.service->Read().phase == aip::discord::Phase::Active; }),
            "Discord replacement starts once asynchronous cleanup completes");
        Check(gate->maximum == 1, "Discord transport ownership does not overlap across cancel/reenable");
        auto invalid = cfg; invalid.timeoutMs = 0;
        provider.Refresh(invalid, false);
        Check(!provider.error.empty() && provider.Read().badge.empty(), "invalid Discord options stop the previous sender and preserve error text");
        Check(HostUntil([&] { provider.Refresh(invalid, false); return !provider.stopping && !provider.service->Read().running; }),
            "invalid Discord configuration drains instead of remaining active");
        Check(HostUntil([&] { provider.Refresh(cfg, false); return provider.started && gate->created == 3; }),
            "a corrected Discord profile can restart after rejection");
        provider.Stop();
        Check(gate->active == 0 && provider.key.empty(), "Discord wrapper Stop releases fake transport and cached request");
    }
}

struct HostCapsState {
    std::atomic<bool> holdCleanup{false};
    std::atomic<int> opens{0}, closes{0};
    aip::caps::Indicators indicators{0, 3};
};
class HostCapsBackend final : public aip::caps::Backend {
    std::shared_ptr<HostCapsState> state_;
    aip::caps::Cancel stopped_;
    steady_clock::time_point cleanupDeadline_{};
public:
    explicit HostCapsBackend(std::shared_ptr<HostCapsState> state) : state_(std::move(state)) {}
    bool Open(const std::wstring&, const aip::caps::Cancel& canceled, std::wstring&) override {
        stopped_ = canceled; ++state_->opens; cleanupDeadline_ = steady_clock::now() + milliseconds(2000); return true;
    }
    bool ReadIndicators(aip::caps::Indicators& value, const aip::caps::Cancel&, std::wstring&) override { value = state_->indicators; return true; }
    bool ReadLogicalCaps(bool& enabled, const aip::caps::Cancel&, std::wstring&) override { enabled = false; return true; }
    bool WriteIndicators(aip::caps::Indicators value, const aip::caps::Cancel&, std::wstring&) override { state_->indicators = value; return true; }
    bool HasPendingOperation() const noexcept override {
        return state_->holdCleanup && stopped_ && stopped_() && steady_clock::now() < cleanupDeadline_;
    }
    bool Close(std::wstring&) override { ++state_->closes; return true; }
};
static void TestCapsHostAndShutdownCache()
{
    {
        auto state = std::make_shared<HostCapsState>();
        ContentCapsProvider provider;
        provider.controller = std::make_unique<aip::caps::Controller>(std::make_unique<HostCapsBackend>(state));
        aip::caps::Config cfg; cfg.actionsEnabled = true; cfg.intervalMs = 86400000;
        provider.Refresh(cfg);
        Check(HostUntil([&] { return provider.controller->GetSnapshot().iterations > 0; }), "Caps host action starts only through an injected fake backend");
        state->holdCleanup = true;
        auto invalid = cfg; invalid.target = L"invalid";
        const auto before = steady_clock::now();
        provider.Refresh(invalid);
        Check(!provider.error.empty() && steady_clock::now() - before < milliseconds(500),
            "invalid Caps configuration requests stop without joining a pending driver");
        Check(HostUntil([&] { return provider.controller->GetSnapshot().cleanupPending; }) && state->closes == 0,
            "Caps invalidation retains fake target ownership until cleanup settles");
        provider.restart = true;
        provider.Refresh(cfg);
        Check(state->opens == 1 && provider.key.empty(), "Caps restart waits for the old owner instead of joining or reopening during cleanup");
        state->holdCleanup = false;
        Check(HostUntil([&] { provider.Refresh(cfg); return state->opens == 2 && provider.controller->GetSnapshot().iterations > 0; }),
            "Caps restart resumes the same configuration after pending cleanup completes");
        provider.Cancel();
        Check(HostUntil([&] { return !provider.controller->GetSnapshot().running; }), "Caps cancellation reaches completion with fake cleanup released");
        provider.Stop();
        Check(state->closes == 2 && provider.Read().badge.empty(), "Caps host closes each fake session and keeps status out of numeric badges");
    }
    g_contentLogicalCaps = 0; contentLogicalCaps = 1;
    const int samples = contentLogicalSamples;
    RequestGracefulShutdown(nullptr);
    bool logical = false; std::wstring error;
    Check(contentLogicalSamples > samples && ReadContentLogicalCaps(logical, error) && logical,
        "direct tray-style shutdown captures the latest synthetic UI Caps toggle for worker cleanup");
    contentLogicalCaps = 0;
    WndProc(nullptr, WM_APP_SHUTDOWN_READY, 0, 0);
    Check(ReadContentLogicalCaps(logical, error) && !logical,
        "deferred shutdown refreshes the UI cache immediately before its quit message");
    MSG message{};
    while (PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
    g_shutdownState.Cancel();
}

static void TestOfflineConfigurationPersistence(const std::wstring& directory)
{
    const auto savedPath = g_iniPath;
    const auto savedCommandLine = g_commandLine;
    g_commandLine = {};
    g_commandLine.configureOnly = true;

    g_iniPath = directory + L"\\missing-parent\\settings.ini";
    Check(RunRenderOnlyCommand() == 2,
        "configure-only rejects an initial default-file write failure without a setting batch");
    Check(GetFileAttributesW(g_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES,
        "failed initialization does not claim to have created its INI");
    g_iniPath = directory;
    Check(RunRenderOnlyCommand() == 2, "configure-only rejects a directory as its INI path");

    const auto fixturePath = directory + L"\\configure-only.ini";
    g_iniPath = fixturePath;
    Check(RunRenderOnlyCommand() == 0, "configure-only creates a new default profile successfully");
    bool created = true;
    Check(TryEnsureInitialIniTemplate(created) && !created,
        "an existing profile is a successful initialization without a creation notification");
    Check(!EnsureInitialIniTemplate(), "resident initialization retains its existing-file result");

    const auto complete = BuildInitialIniTemplate();
    const auto stringsAt = complete.find(L"[Strings]");
    Check(stringsAt != std::wstring::npos, "default template contains a strings section");
    const auto expectLockedFailure = [&](const std::wstring& profile, const char* label) {
        g_iniPath = fixturePath;
        if (!WriteContentFixture(profile)) {
            Check(false, "write persistence failure fixture");
            return;
        }
        std::vector<BYTE> before;
        Check(ReadWholeFileBytes(g_iniPath, before), "read exact original fixture bytes");
        // Readers remain permitted, so validation succeeds. Replacing the file
        // is denied, without altering ACLs or any non-fixture file attributes.
        HANDLE reader = CreateFileW(g_iniPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(reader != INVALID_HANDLE_VALUE, "hold fixture file against replacement");
        if (reader == INVALID_HANDLE_VALUE) return;
        const int result = RunRenderOnlyCommand();
        CloseHandle(reader);
        Check(result == 2, label);
        std::vector<BYTE> after;
        Check(ReadWholeFileBytes(g_iniPath, after) && before == after,
            "a rejected write preserves the exact previous profile bytes");
    };
    expectLockedFailure(L"; preserve this comment\r\n[Settings]\r\nTrayIcon=0\r\n",
        "configure-only reports setting-default write failure with no requested assignments");
    if (stringsAt != std::wstring::npos)
        expectLockedFailure(complete.substr(0, stringsAt),
            "configure-only reports string-default write failure with all setting defaults present");
    g_commandLine.settings.push_back({L"TileText", L"Text", L"saved fixture"});
    expectLockedFailure(complete, "configure-only reports explicit setting-batch write failure");
    Check(RunRenderOnlyCommand() == 0 && IniReadS(L"TileText", L"Text", L"") == L"saved fixture",
        "configure-only succeeds and refreshes the cached setting after the write lock is released");
    g_commandLine.settings.clear();
    std::vector<BYTE> before, after;
    Check(ReadWholeFileBytes(g_iniPath, before) && RunRenderOnlyCommand() == 0 &&
        ReadWholeFileBytes(g_iniPath, after) && before == after,
        "configure-only with no changes preserves an already complete profile");
    Check(GetFileAttributesW((directory + L"\\AppxManifest.xml").c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((directory + L"\\Assets").c_str()) == INVALID_FILE_ATTRIBUTES,
        "configure-only persistence fixtures do not generate package files or images");
    Check(DeleteFileW(fixturePath.c_str()) != FALSE, "remove temporary configure-only profile");
    g_iniPath = savedPath;
    g_commandLine = savedCommandLine;
    std::wstring restored;
    Check(ReadIniFileForMutation(restored), "restore the original fixture profile cache");
}

static void TestSmtcArtworkHost(const std::vector<BYTE>& bytes)
{
    int scene = 1, reads = 0;
    bool requestedArtwork = false;
    g_contentSmtc.testReader = [&](auto, bool artwork, const auto&) {
        ++reads; requestedArtwork = artwork;
        aip::content::SmtcSessionData data;
        data.hasSession = scene != 0;
        data.title = scene == 1 ? L"First fixture song" : L"Second fixture song";
        data.artist = L"Fixture artist"; data.appId = L"Fixture.Player"; data.status = L"Playing";
        if (scene == 1 || scene == 2) {
            data.artworkBytes = bytes;
            if (scene == 2) data.artworkBytes[54] = 200;
        }
        if (scene == 4) data.artworkBytes = {1, 2, 3};
        return data;
    };
    const std::wstring mediaProfile =
        L"[Content]\r\nEnabled=1\r\nCount=1\r\nTextMode=Overlay\r\n"
        L"[Content.1]\r\nBackground=SMTCArtwork\r\nTextSources=SMTC\r\n";
    Check(WriteContentFixture(mediaProfile) && ContentEngineTick(true), "artwork background composes through the host with an injected session");
    aip::content::Snapshot first;
    { ContentRenderScope scope; first = scope.snapshot; }
    Check(requestedArtwork && reads == 1 && first.backgroundLease && first.text.primary == L"First fixture song" &&
        first.backgroundPath.find(L"AIProjects-SMTC-") != std::wstring::npos,
        "the host publishes text and artwork from one session/property snapshot");
    const auto firstPath = first.backgroundPath;
    {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token = 0;
        const bool started = Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
        if (started) {
            { Gdiplus::Bitmap image(firstPath.c_str());
                Check(image.GetLastStatus() == Gdiplus::Ok && image.GetWidth() == 1 && image.GetHeight() == 1,
                    "the production image decoder accepts the generated bounded artwork BMP");
            }
            Gdiplus::GdiplusShutdown(token);
        } else Check(false, "initialize inert image decoder for the cached artwork fixture");
    }
    scene = 2;
    Check(ContentEngineTick(true), "changed-track fixture refreshes without native SMTC");
    { ContentRenderScope scope;
        Check(scope.snapshot.text.primary == L"Second fixture song" && scope.snapshot.backgroundPath != firstPath && scope.snapshot.backgroundLease,
            "track changes replace composed text and background atomically");
    }
    Check(GetFileAttributesW(firstPath.c_str()) != INVALID_FILE_ATTRIBUTES,
        "an old generation lease prevents source refresh from deleting its artwork");
    first = {};
    Check(GetFileAttributesW(firstPath.c_str()) == INVALID_FILE_ATTRIBUTES,
        "old artwork is deleted after the final old generation lease ends");
    scene = 3;
    Check(ContentEngineTick(true), "no-thumbnail fixture composes a usable fallback");
    { ContentRenderScope scope;
        Check(!scope.snapshot.backgroundLease && scope.snapshot.backgroundPath.find(L"ContentSolid-") != std::wstring::npos &&
            scope.snapshot.text.primary == L"Second fixture song", "absent artwork uses current song text over a solid background");
    }
    scene = 0;
    Check(ContentEngineTick(true), "idle media fixture composes without stale artwork");
    { ContentRenderScope scope;
        Check(!scope.snapshot.backgroundLease && scope.snapshot.text.primary == L"No media playing",
            "idle clears the former track and reports its own status");
    }
    scene = 4;
    Check(ContentEngineTick(true), "malformed image fixture retains media data");
    { ContentRenderScope scope;
        Check(!scope.snapshot.backgroundLease && scope.snapshot.text.primary == L"Second fixture song" &&
            scope.snapshot.text.secondary.find(L"artwork") != std::wstring::npos,
            "decode errors fall back to current text and expose the artwork diagnostic");
    }
    scene = 1;
    const std::wstring artworkOnly =
        L"[Content]\r\nEnabled=1\r\nCount=1\r\nTextMode=Overlay\r\n"
        L"[Content.1]\r\nBackground=smtcartwork\r\nTextSources=\r\n";
    Check(WriteContentFixture(artworkOnly) && ContentEngineTick(true), "artwork-only content starts the media provider without selecting SMTC text");
    { ContentRenderScope scope;
        Check(scope.snapshot.backgroundLease && scope.snapshot.text.primary.empty(), "artwork-only content retains a pure image while artwork exists");
    }
    scene = 3;
    Check(ContentEngineTick(true), "artwork-only content refreshes a missing-thumbnail fallback");
    { ContentRenderScope scope;
        Check(!scope.snapshot.backgroundLease && scope.snapshot.text.primary == L"Second fixture song",
            "an artwork-only entry falls back to useful media text when its image is absent");
    }
    const std::wstring futureArtwork =
        L"[Content]\r\nEnabled=1\r\nCount=2\r\nCycleEnabled=1\r\nCycleSeconds=60\r\nTextMode=Overlay\r\n"
        L"[Content.1]\r\nBackground=None\r\nTextSources=CustomText\r\nText=Visible fixture\r\n"
        L"[Content.2]\r\nBackground=SMTCArtwork\r\nTextSources=SMTC\r\n";
    Check(WriteContentFixture(futureArtwork) && ContentEngineTick(true) && requestedArtwork,
        "a future artwork cycle slot can prefetch its read-only source");
    { ContentRenderScope scope;
        Check(!scope.snapshot.backgroundLease && scope.snapshot.text.primary == L"Visible fixture",
            "prefetched media does not replace the active entry's configured text or background");
    }
    g_contentSmtc.Cancel();
    g_contentSmtc.testReader = {};
}

static void TestSmtcCadenceAndInvalidCancellation()
{
    std::atomic<int> reads{0};
    g_contentSmtc.Cancel();
    g_contentSmtc.testReader = [&](auto, bool, const auto&) {
        ++reads;
        aip::content::SmtcSessionData data;
        data.hasSession = true; data.title = L"Unchanged inert media"; data.artist = L"Fixture artist";
        data.appId = L"Fixture.Player"; data.status = L"Playing";
        return data;
    };
    const std::wstring configuration =
        L"[Content]\r\nEnabled=1\r\nCount=1\r\nCycleEnabled=0\r\nRefreshSeconds=86400\r\nTextMode=Auto\r\n"
        L"[Content.1]\r\nBackground=None\r\nTextSources=SMTC\r\n";
    Check(WriteContentFixture(configuration) && ContentEngineTick(true), "cadence fixture obtains one injected media snapshot without publishing a tile");
    const auto before = contentGenerationRequests;
    Check(ContentEngineTick(), "unchanged media first enters the inert generation queue");
    for (int n = 0; n < 30; ++n) Check(ContentEngineTick(), "same-frame media poll remains usable");
    Check(reads == 1 && contentGenerationRequests == before + 1,
        "31 same-frame polls neither refetch media nor queue duplicate tile generations");
    const auto mediaKey = g_contentSmtc.key;
    g_contentRss.source.Request(L"inert-rss", hours(1), [](const auto&) { return RssFeedSnapshot{}; });
    Check(g_contentRss.source.Wait(L"inert-rss", seconds(2)), "seed an inert pending-source cache for invalid-edit cancellation");
    for (int resource = 0; resource < 3; ++resource) {
        const auto key = L"inert-notes-" + std::to_wstring(resource);
        g_contentNotes.sources[resource].Request(key, hours(1), [](const auto&) { return aip::notes::Snapshot{}; });
        Check(g_contentNotes.sources[resource].Wait(key, seconds(2)), "seed each inert Notes cache without accessing accounts");
    }
    const auto previousCacheInterval = g_iniCacheRefreshMs.load();
    g_iniCacheRefreshMs = 60000;
    Check(WriteUtf8BomTextFile(g_iniPath, L"[Content]\r\nEnabled=invalid\r\nCount=1\r\n"),
        "write an external invalid edit without refreshing the host's cached valid document");
    Check(!Generate(L"fixture desktop wallpaper", L"pre-poll invalid guard", true) && CurrentGenerationBackground().empty(),
        "generation rejects an external invalid edit before the poll or cache interval expires, despite its old active frame");
    g_iniCacheRefreshMs = previousCacheInterval;
    Check(!ContentEngineTick(), "an invalid content edit enters the configuration error path");
    {
        ContentRenderScope invalidScope;
        Check(!invalidScope.snapshot.active && CurrentGenerationBackground().empty(),
            "invalid content clears the published frame instead of retaining an old generation input");
        Check(!Generate(L"fixture desktop wallpaper", L"invalid content guard", true),
            "a queued invalid content generation cannot fall back to desktop wallpaper or register a package");
    }
    std::string error;
    RssFeedSnapshot rss;
    aip::content::SmtcMediaSnapshot media;
    Check(!g_contentRss.source.Read(L"inert-rss", rss, error) && !g_contentSmtc.source.Read(mediaKey, media, error),
        "invalid content configuration immediately invalidates RSS and media source caches");
    for (int resource = 0; resource < 3; ++resource) {
        aip::notes::Snapshot notes;
        Check(!g_contentNotes.sources[resource].Read(L"inert-notes-" + std::to_wstring(resource), notes, error),
            "invalid content configuration cancels every Notes resource");
    }
    g_contentSmtc.Cancel(); g_contentSmtc.testReader = {};
}

static void TestCompositionGenerationRequests(const std::wstring& configuration, const std::wstring& bitmapPath)
{
    Check(WriteContentFixture(configuration), "prepare composed generation requests");
    PublishContentSnapshot({});
    const auto initialRequests = contentGenerationRequests;
    Check(CurrentGenerationBackground(bitmapPath).empty(),
        "unpublished composition never substitutes an explicit desktop wallpaper");
    Check(ContentEngineTick(false, false) && contentGenerationRequests == initialRequests,
        "startup priming publishes a composed frame without waiting for providers or queuing generation");
    Check(CurrentGenerationBackground() == bitmapPath && CurrentGenerationBackground(L"wrong desktop wallpaper") == bitmapPath,
        "current composed background takes precedence over desktop and command-line wallpaper");

    const auto requests = contentGenerationRequests;
    DispatchTrayCommand(nullptr, ID_GENERATE_NOW);
    Check(contentGenerationRequests == requests + 1 && contentQueuedBackground == bitmapPath,
        "Update tile now queues the composition's image without a desktop wallpaper lookup");
    QueueCommandLineGenerationRequest();
    Check(contentGenerationRequests == requests + 2 && contentQueuedBackground == bitmapPath,
        "resident CLI generation uses the same composed background as the tray");
    Check(QueueLiveTileModeReregistration() && contentGenerationRequests == requests + 3 && contentQueuedBackground == bitmapPath,
        "mode re-registration queues the composed background without desktop wallpaper");
    SetLiveTileModeReregistrationPending(false);

    Check(IniWrite(L"Settings", L"LiveTileTemplateStyle", L"Adaptive"), "prepare template switch fixture");
    g_liveTileTemplateStyle = static_cast<int>(LiveTileTemplateStyle::Adaptive);
    DispatchTrayCommand(nullptr, ID_LIVE_TILE_TEMPLATE_WINDOWS81_PRESET);
    Check(contentGenerationRequests == requests + 4 && contentQueuedBackground == bitmapPath,
        "template switch queues the content background");
    Check(IniWrite(L"Settings", L"LiveTileBranding", L"Auto"), "prepare branding switch fixture");
    g_liveTileBranding = static_cast<int>(LiveTileBranding::Auto);
    DispatchTrayCommand(nullptr, ID_LIVE_TILE_BRANDING_NONE);
    Check(contentGenerationRequests == requests + 5 && contentQueuedBackground == bitmapPath,
        "branding switch queues the content background");

    Check(IniWrite(L"Settings", L"GeneratedAssetCache", L"1") &&
        IniWrite(L"Settings", L"GeneratedAssetPrecache", L"1") && !GeneratedAssetPrecacheEnabled(),
        "composition defers a stored optional wallpaper precache preference");
    QueueGeneratedAssetPrecache(bitmapPath);
    RunGeneratedAssetPrecache(bitmapPath);
    Check(!g_generatedAssetPrecacheWorkerStarted && !g_generatedAssetPrecacheThread.joinable() && !g_generatedAssetPrecacheQueued,
        "composed contents never start the incompatible sibling-wallpaper precache worker");
    Check(WriteContentFixture(configuration), "restore composition settings after request checks");
    Check(ContentEngineTick(true), "restore the composed snapshot after request checks");

    ChoosePowerShellExe(nullptr);
    Check(contentFileDialogFilters == std::vector<std::wstring>{L"Executable files", L"*.exe", L"All files", L"*.*"},
        "PowerShell picker supplies complete filter name/pattern pairs to the common dialog");
}

static void TestNotesImportsAndHost()
{
    const auto section = L"Notes.Resin";
    const auto source = aip::GetDirectoryName(g_iniPath) + L"\\legacy-notes.ini";
    const std::wstring donor = L"[Settings]\nRunAtStartup=1\nRefreshIntervalSeconds=600\n[Account.Resin]\nUID=800000001\nLTokenV2Protected=dpapi:0102\nLTuidV2Protected=dpapi:0304\nRefreshIntervalSeconds=90\n";
    Check(WriteUtf8BomTextFile(source, donor), "write synthetic donor account profile");
    const std::wstring base = L"[Content]\nEnabled=1\nCount=1\n[Content.1]\nBackground=None\nTextSources=Notes\nNotesResource=Resin\n";
    Check(WriteContentFixture(base), "prepare inert Notes host configuration");
    Check(ImportContentNotesFile(nullptr, -1, source) && IniReadS(section, L"LTokenProtected", L"") == L"dpapi:0102" &&
        IniReadS(section, L"LTUidProtected", L"") == L"dpapi:0304" && IniReadS(section, L"RefreshSeconds", L"") == L"90",
        "native tray/import adapter commits protected aliases and per-account refresh without decrypting");
    std::wstring unchanged, error;
    Check(aip::notes::ReadImportFile(source, unchanged, error) && unchanged == donor,
        "importing leaves the source profile unchanged");
    const auto before = IniReadS(section, L"LTokenProtected", L"");
    Check(WriteUtf8BomTextFile(source, L"[Account.Resin]\nUID=800000001\nLTokenV2Protected=bad\nLTuidV2Protected=dpapi:01\n"), "write invalid protected source fixture");
    std::wstring original;
    ReadIniFileForMutation(original);
    Check(!ImportContentNotesFile(nullptr, -1, source), "invalid protected import is rejected");
    std::wstring after;
    Check(ReadIniFileForMutation(after) && after == original && IniReadS(section, L"LTokenProtected", L"") == before,
        "a malformed source causes zero target-byte changes");
    Check(WriteUtf8BomTextFile(source, donor), "restore synthetic donor profile");
    Check(SeedContentFixtureDefaults(), "seed real host defaults before configure-only Notes import behavior");
    const auto savedCommand = g_commandLine;
    g_commandLine = {}; g_commandLine.configureOnly = true;
    g_commandLine.notesImports.push_back({-1, source});
    g_commandLine.settings.push_back({section, L"UID", L"800000002"});
    Check(ExpandCommandLineNotesImports(error) && RunRenderOnlyCommand() == 0 && IniReadS(section, L"UID", L"") == L"800000002",
        "CLI import expands as one configure-only batch with explicit setting precedence");
    g_commandLine = {}; g_commandLine.configureOnly = true; g_commandLine.notesImports.push_back({-1, source});
    g_commandLine.settings = {{section, L"ltoken_v2", L"synthetic-token"}, {section, L"LTUid", L"synthetic-user"}};
    aip::content::Configuration importedCredentials;
    Check(ExpandCommandLineNotesImports(error) && RunRenderOnlyCommand() == 0 && ReadContentConfiguration(importedCredentials, error) &&
        importedCredentials.notes.accounts[0].token == L"synthetic-token" && importedCredentials.notes.accounts[0].userId == L"synthetic-user" &&
        importedCredentials.notes.accounts[0].tokenProtected.empty() && importedCredentials.notes.accounts[0].userIdProtected.empty(),
        "explicit plain credential aliases replace protected import families without values being logged");
    Check(WriteUtf8BomTextFile(source, L"[Account.Resin]\nUID=800000001\nLToken=synthetic-token\nLTUid=synthetic-user\n"),
        "prepare plain synthetic account donor for reverse precedence");
    g_commandLine = {}; g_commandLine.configureOnly = true; g_commandLine.notesImports.push_back({-1, source});
    g_commandLine.settings = {{section, L"LTokenV2Protected", L"dpapi:0102"}, {section, L"LTuidV2Protected", L"dpapi:0304"}};
    Check(ExpandCommandLineNotesImports(error) && RunRenderOnlyCommand() == 0 && ReadContentConfiguration(importedCredentials, error) &&
        importedCredentials.notes.accounts[0].token.empty() && importedCredentials.notes.accounts[0].userId.empty() &&
        importedCredentials.notes.accounts[0].tokenProtected == L"dpapi:0102" && importedCredentials.notes.accounts[0].userIdProtected == L"dpapi:0304",
        "explicit protected aliases replace plain import families without opening credentials");
    g_commandLine = savedCommand;
    ContentNotesProvider provider;
    aip::content::NotesConfiguration cfg;
    cfg.accounts[0].uid = L"800000001"; cfg.accounts[0].tokenProtected = L"dpapi:01"; cfg.accounts[0].userIdProtected = L"dpapi:02";
    cfg.accounts[0].refreshSeconds = 90;
    std::atomic<int> decryptions{0}, fetches{0};
    std::atomic<bool> entered{false}, release{true};
    provider.testDecoder = [&](const auto&, auto& plain) { ++decryptions; plain = L"fixture-only"; return true; };
    provider.testTransport = [&](const auto& request, auto& response, auto&) {
        ++fetches; entered = true;
        const auto deadline = steady_clock::now() + milliseconds(2000);
        while (!release && steady_clock::now() < deadline) std::this_thread::sleep_for(milliseconds(1));
        response.statusCode = 200;
        const std::string body = R"({"retcode":0,"data":{"current_resin":20,"max_resin":200,"resin_recovery_time":"7200","finished_task_num":2,"total_task_num":4}})";
        response.body.assign(body.begin(), body.end());
        return true;
    };
    provider.Refresh(cfg, 0, true);
    const auto summary = provider.Read(cfg, 0);
    const auto detail = provider.Read(cfg, 0, aip::notes::DetailId::Commissions);
    Check(summary.primary == L"20/200" && summary.secondary == L"2h 0m" && detail.primary == L"Commissions" && detail.secondary == L"2/4" &&
        fetches == 1 && decryptions == 2, "summary and selectable detail share one injected fetch snapshot");
    provider.refreshRequested = 1;
    release = false; entered = false;
    provider.Refresh(cfg, 0, false);
    Check(HostUntil([&] { return entered.load(); }), "start a bounded stale-account transport fixture");
    const auto oldCfg = cfg;
    cfg.accounts[0] = {};
    provider.Refresh(cfg, 0, false);
    Check(provider.Read(cfg, 0).primary != L"20/200", "clear/reconfiguration immediately hides the old cached account data");
    release = true;
    Check(HostUntil([&] { return provider.Read(cfg, 0).primary == L"Configuration error"; }) && fetches == 2 && decryptions == 4,
        "late stale fetch cannot publish after clear and the cleared account starts no decryption or HTTP");
    provider.Stop();
    HandleSingleInstanceRequest(SINGLE_INSTANCE_REQUEST_NOTES_REFRESH_BASE + 1);
    Check((g_contentNotes.refreshRequested.load() & 2u) != 0, "existing-resident Notes IPC queues the specific game without lifecycle work");
    Check(DispatchContentEngineCommand(nullptr, ID_NOTES_SETTINGS + 16 + 7) && IniReadS(section, L"UID", L"missing").empty() &&
        IniReadS(section, L"LTokenProtected", L"missing").empty(), "tray clear atomically clears plain/protected credentials and cancels cached requests");
    g_contentNotes.refreshRequested = 0;
    DeleteFileW(source.c_str());
}

static void TestNowPlayingProfileHostImports(const std::vector<BYTE>& artworkBytes)
{
    const auto source = aip::GetDirectoryName(g_iniPath) + L"\\now-playing-donor.ini";
    const auto savedCommand = g_commandLine;
    const auto savedResponse = contentDialogResponse; contentDialogResponse = IDYES;
    int reads = 0;
    g_contentSmtc.testReader = [&](auto, bool artwork, const auto&) {
        ++reads; aip::content::SmtcSessionData data;
        data.hasSession = true; data.title = L"Imported fixture song"; data.artist = L"Fixture artist"; data.status = L"Playing";
        if (artwork) data.artworkBytes = artworkBytes;
        return data;
    };
    const std::wstring base = L"[Settings]\nRunAtStartup=1\nRunAtStartupPackaged=0\n[Content]\nEnabled=0\nCount=4\nCycleSeconds=23\n"
        L"[Content.1]\nBackground=Image\nImagePath=obsolete.bmp\nNotesDetail=invalid-old-detail\n[Content.4]\nText=stale-four\n"
        L"[Unrelated]\nKeep=unchanged\n";
    for (const auto* layout : {L"Text", L"Artwork", L"Combined", L"Cycle"}) {
        const auto donor = L"; untouched donor\n[Settings]\nTileLayout=" + std::wstring(layout) +
            L"\nUpdateIntervalSeconds=6\nTileRefreshSeconds=240\nRunAtStartup=false\n";
        Check(WriteUtf8BomTextFile(source, donor) && WriteContentFixture(base), "prepare synthetic NowPlaying donor and stale host entries");
        const auto beforeReads = reads;
        Check(ImportContentNowPlayingFile(nullptr, source) && reads == beforeReads,
            "tray NowPlaying import commits without querying the injected media source");
        aip::content::Configuration cfg; std::wstring error, unchanged;
        Check(ReadContentConfiguration(cfg, error) && cfg.enabled && cfg.entries.size() == (std::wstring(layout) == L"Cycle" ? 3u : 1u) &&
            cfg.refreshSeconds == 6 && cfg.cycleSeconds == 23 && IniReadS(L"Settings", L"RunAtStartup", L"") == L"1" &&
            IniReadS(L"Unrelated", L"Keep", L"") == L"unchanged" && IniReadS(L"Content.4", L"Text", L"missing") == L"missing" &&
            contentDialogText.find(L"Donor RunAtStartup=false") != std::wstring::npos,
            "tray replacement removes dormant entries, preserves native Startup and reports deferred donor preference");
        Check(ReadTextFile(source, unchanged) && unchanged == donor && ContentEngineTick(true),
            "imported layout composes through the real host using an injected session and leaves its donor unchanged");
        { ContentRenderScope scope;
            const bool pureArtwork = std::wstring(layout) == L"Artwork";
            Check((pureArtwork ? scope.snapshot.text.primary.empty() : scope.snapshot.text.primary == L"Imported fixture song") &&
                (pureArtwork || std::wstring(layout) == L"Combined" ? bool(scope.snapshot.backgroundLease) : !scope.snapshot.backgroundLease),
                "native imported Text, Artwork, Combined and initial Cycle slots publish their expected composition");
        }
    }
    std::wstring error, before, after;
    Check(WriteContentFixture(base), "restore stale content before configure-only projection preflight");
    Check(SeedContentFixtureDefaults(), "seed real host defaults while retaining stale NowPlaying entries for configure-only import");
    g_commandLine = {}; g_commandLine.configureOnly = true; g_commandLine.nowPlayingImports = {source};
    g_commandLine.settings = {{L"Content", L"RefreshSeconds", L"9"}, {L"Content.1", L"Name", L"Explicit name"}};
    const auto beforeReads = reads;
    Check(ExpandCommandLineNowPlayingImports(error) && RunRenderOnlyCommand() == 0 && reads == beforeReads &&
        IniReadS(L"Content", L"RefreshSeconds", L"") == L"9" && IniReadS(L"Content.1", L"Name", L"") == L"Explicit name" &&
        IniReadS(L"Content.1", L"NotesDetail", L"missing") == L"missing" && !g_commandLine.nowPlayingImportPlan.snapshot.runAtStartup,
        "configure-only preflight uses fresh-entry defaults and explicit settings win without media or Startup work");
    ReadIniFileForMutation(before);
    for (const auto* invalid : {L"[Settings]\nTileLayout=bad\n", L"[Settings]\nUpdateIntervalSeconds=61\n", L"[Settings]\nRunAtStartup=perhaps\n", L"[Settings\nTileLayout=Text\n"}) {
        Check(WriteUtf8BomTextFile(source, invalid), "create a malformed synthetic NowPlaying profile");
        g_commandLine = {}; g_commandLine.configureOnly = true; g_commandLine.nowPlayingImports = {source};
        Check(!ExpandCommandLineNowPlayingImports(error) && g_commandLine.settings.empty() &&
            !ImportContentNowPlayingFile(nullptr, source) && ReadIniFileForMutation(after) && after == before && reads == beforeReads,
            "malformed CLI/tray imports publish no settings or target writes and query no media");
    }
    aip::now_playing_profile::ImportPlan plan; aip::now_playing_profile::ConfigurationProjection projection;
    Check(!ReadContentNowPlayingImport(g_iniPath, plan, projection, error) && projection.settings.empty() &&
        ReadIniFileForMutation(after) && after == before, "self-import is rejected without altering the source/output profile");
    g_contentSmtc.Cancel(); g_contentSmtc.testReader = {};
    g_commandLine = savedCommand; contentDialogResponse = savedResponse;
    DeleteFileW(source.c_str());
}

static void TestHardwareProfileHostImports()
{
    const auto directory = aip::GetDirectoryName(g_iniPath);
    const auto capsSource = directory + L"\\caps-donor.ini", asusSource = directory + L"\\asus-donor.ini";
    const std::wstring capsText = L"; source stays exact\n[Settings]\nKeyboardTargetPath=\\Device\\KeyboardClass03\nBlinkIntervalMs=750\nRunAtStartup=1\n";
    const std::wstring asusText = L"; legacy donor\n[Options]\nmic-state=bad\nkeyboard-state=130\nno-tray=\n[Settings]\nMicState=1\nEvent001HddState=128,129\nEvent001HddInterval=0.5s\n";
    Check(WriteUtf8BomTextFile(capsSource, capsText) && WriteUtf8BomTextFile(asusSource, asusText), "create synthetic Caps/ASUS donor import files");
    const std::wstring base = L"; preserve host\n[Content]\nEnabled=0\nCount=1\n[Content.1]\nBackground=None\nTextSources=CustomText\n"
        L"[CapsBlink]\nHardwareEnabled=0\n[AsusBlink]\nHardwareEnabled=0\nReadDiskActivity=0\nPaused=1\nProfilePath=old-draft.ini\nEvent2MicState=1\nEvent2MicInterval=bad\nVendor=keep\n";
    Check(WriteContentFixture(base) && ImportContentHardwareFile(nullptr, true, asusSource) && ImportContentHardwareFile(nullptr, false, capsSource),
        "tray import adapters replace stale invalid event fields and clear external profile selection atomically");
    aip::content::Configuration cfg; std::wstring error;
    Check(ReadContentConfiguration(cfg, error) && cfg.asus.settings.count(L"Event001HddState") && !cfg.asus.settings.count(L"Event2MicState") &&
        cfg.asus.profilePath.empty() && cfg.asus.paused && !cfg.asus.hardwareEnabled && !cfg.asus.readDiskActivity && !cfg.caps.actionsEnabled &&
        cfg.caps.target == L"\\Device\\KeyboardClass03" && cfg.caps.intervalMs == 750 && IniReadS(L"AsusBlink", L"Vendor", L"") == L"keep",
        "runtime typed reads retain imported events and preserve flags, unrelated fields and Caps target/interval");
    std::wstring capsAfter, asusAfter;
    Check(ReadTextFile(capsSource, capsAfter) && ReadTextFile(asusSource, asusAfter) && capsAfter == capsText && asusAfter == asusText,
        "hardware tray imports leave both donor sources and comments unchanged");
    Check(SaveContentAsusEventSetting(nullptr, L"event-001-hdd-state", L"130") && SaveContentAsusEventSetting(nullptr, L"Event01MicState", L"1") &&
        ReadContentConfiguration(cfg, error) && cfg.asus.settings.at(L"Event001HddState") == L"130" && cfg.asus.settings.count(L"Event01MicState"),
        "advanced tray setting route validates aliases and retains leading-zero numbered identities");
    std::wstring before, after;
    ReadIniFileForMutation(before);
    Check(!SaveContentAsusEventSetting(nullptr, L"Event01MicState", L"2") && ReadIniFileForMutation(after) && before == after,
        "invalid advanced edits make zero target byte changes");
    Check(WriteUtf8BomTextFile(asusSource, L"[Settings]\nEvent001MicState=2\n"), "create malformed donor replacement fixture");
    Check(!ImportContentHardwareFile(nullptr, true, asusSource) && ReadIniFileForMutation(after) && before == after,
        "malformed donor replacement cannot remove existing events or publish a partial map");
    Check(WriteUtf8BomTextFile(asusSource, asusText), "restore synthetic modern/legacy donor source");
    Check(SeedContentFixtureDefaults(), "seed real host defaults before configure-only hardware import behavior");
    const auto savedCommand = g_commandLine;
    g_commandLine = {}; g_commandLine.configureOnly = true;
    g_commandLine.hardwareImports = {{true, asusSource}, {false, capsSource}};
    g_commandLine.settings = {{L"AsusBlink", L"Event001HddState", L"140"}, {L"CapsBlink", L"BlinkIntervalMs", L"900"}};
    Check(ExpandCommandLineHardwareImports(error) && RunRenderOnlyCommand() == 0 &&
        IniReadS(L"AsusBlink", L"Event001HddState", L"") == L"140" && IniReadS(L"CapsBlink", L"BlinkIntervalMs", L"") == L"900" &&
        IniReadS(L"AsusBlink", L"Event01MicState", L"missing") == L"missing",
        "configure-only imports replace prior events with explicit setting precedence in one saved batch");
    Check(IniReadS(L"CapsBlink", L"HardwareEnabled", L"") == L"0" && IniReadS(L"AsusBlink", L"HardwareEnabled", L"") == L"0" &&
        IniReadS(L"AsusBlink", L"ReadDiskActivity", L"") == L"0" && IniReadS(L"AsusBlink", L"Paused", L"") == L"1",
        "configure-only hardware imports leave independent activation and pause flags untouched");
    ReadIniFileForMutation(before);
    HANDLE lock = CreateFileW(g_iniPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(lock != INVALID_HANDLE_VALUE, "hold imported target against atomic replacement");
    if (lock != INVALID_HANDLE_VALUE) {
        Check(RunRenderOnlyCommand() == 2, "failed import publication reports a configure-only error");
        CloseHandle(lock);
        Check(ReadIniFileForMutation(after) && before == after && RunRenderOnlyCommand() == 0,
            "failed import publication preserves full target bytes and allows retry after the lock is released");
    }
    Check(!ReadContentHardwareImport(g_iniPath, true, g_commandLine.settings, error), "a donor source cannot equal the host output profile");
    g_commandLine = savedCommand;
    ContentAsusProvider provider; aip::content::AsusConfiguration external;
    external.profilePath = asusSource;
    std::atomic<int> backends{0}; bool inert = false;
    provider.configureTestOptions = [&](aip::asus::Options& options) {
        inert = !options.hardwareEnabled && !options.readDiskActivity;
        options.backendFactory = [&]() -> std::unique_ptr<aip::asus::Backend> { ++backends; return {}; };
    };
    provider.Refresh(external);
    Check(HostUntil([&] { return provider.service && provider.service->Read().pattern.micState == 1; }) &&
        inert && backends == 0 && provider.error.empty(), "external ProfilePath loads legacy Options fallback through the shared reader with no hardware/disk backend");
    provider.Stop();
    Check(ReadTextFile(asusSource, asusAfter) && asusAfter == asusText, "external profile runtime also leaves the donor source unchanged");
    DeleteFileW(capsSource.c_str()); DeleteFileW(asusSource.c_str());
}

static void TestDiscordAdvancedHost()
{
    const auto directory = aip::GetDirectoryName(g_iniPath);
    const auto profile = directory + L"\\discord-advanced.ini";
    const auto created = directory + L"\\discord-created.ini";
    const auto savedCommand = g_commandLine;
    const std::wstring hostText = L"[Content]\r\nEnabled=0\r\n[Discord]\r\nProfilePath=discord-advanced.ini\r\nRefreshSeconds=5\r\nSendPresence=0\r\n";
    const std::wstring profileText = L"; untouched source comment\r\n[general]\r\nactivity_name=Fixture\r\ntoken=private-legacy-preserve\r\ntoken_protected=dpapi:012345\r\n[app]\r\nrun_at_startup=true\r\n";
    Check(WriteContentFixture(hostText) && WriteUtf8BomTextFile(profile, profileText), "prepare isolated Discord host/profile pair");
    std::wstring error;
    DiscordHostEditOptions options;
    Check(CommitDiscordHostProfile({}, {{L"general", L"update_interval", L"17"}, {L"general", L"activity_type", L"2"}}, options, error) &&
        IniReadS(L"Discord", L"RefreshSeconds", L"") == L"17", "advanced cadence and profile edits commit together with deliberate host mapping");
    aip::discord::ProfileDocument document;
    Check(aip::discord::ReadProfileDocument(profile, document, error) && document.profile.values[{L"general", L"activity_type"}] == L"2" &&
        document.profile.values[{L"general", L"token"}] == L"private-legacy-preserve" && document.profile.values[{L"general", L"token_protected"}] == L"dpapi:012345",
        "host advanced edit preserves both credential representations and donor host metadata without applying it");
    std::vector<BYTE> hostBefore, profileBefore, after;
    ReadWholeFileBytes(g_iniPath, hostBefore); ReadWholeFileBytes(profile, profileBefore);
    Check(!CommitDiscordHostProfile({{L"Discord", L"RefreshSeconds", L"9"}}, {{L"general", L"update_interval", L"17"}}, options, error) &&
        ReadWholeFileBytes(g_iniPath, after) && after == hostBefore && ReadWholeFileBytes(profile, after) && after == profileBefore,
        "conflicting explicit host/profile cadence is rejected with zero changes to either file");
    Check(!CommitDiscordHostProfile({{L"CustomText", L"Text", L"partial host"}},
        {{L"general", L"activity_name", L"partial profile"}, {L"general", L"activity_type", L"4"}}, options, error) &&
        ReadWholeFileBytes(g_iniPath, after) && after == hostBefore && ReadWholeFileBytes(profile, after) && after == profileBefore,
        "late advanced validation failure publishes neither staged host nor profile changes");
    Check(CommitDiscordHostProfile({{L"Discord", L"RefreshSeconds", L"13"}}, {{L"general", L"update_interval", L"13"}}, options, error) &&
        IniReadS(L"Discord", L"RefreshSeconds", L"") == L"13" && aip::discord::ReadProfileDocument(profile, document, error) &&
        document.profile.values[{L"general", L"update_interval"}] == L"13", "matching explicit cadences commit one coherent preference");
    ReadWholeFileBytes(profile, profileBefore);
    Check(!CommitDiscordHostProfile({}, {{L"general", L"token", L"not-logged"}}, options, error) &&
        ReadWholeFileBytes(profile, after) && after == profileBefore, "unacknowledged private CLI/API edit writes no credential data");
    options.privateInput = true;
    std::wstring effectiveToken, credentialError; int decodes = 0;
    const aip::discord::CredentialDecoder fixtureDecoder = [&](const std::wstring&) { ++decodes; return L"decoded-host-fixture"; };
    Check(CommitDiscordHostProfile({}, {{L"general", L"token", L"fixture-explicit-private"}}, options, error) &&
        aip::discord::ReadProfileDocument(profile, document, error) && document.profile.values[{L"general", L"token_protected"}].empty() &&
        aip::discord::ResolveConfiguredToken(document.profile.values[{L"general", L"token"}], document.profile.values[{L"general", L"token_protected"}],
            fixtureDecoder, effectiveToken, credentialError) && effectiveToken == L"fixture-explicit-private" && decodes == 0,
        "explicit private plaintext replacement becomes effective without encryption or decoding the old protected value");
    ReadWholeFileBytes(g_iniPath, hostBefore); ReadWholeFileBytes(profile, profileBefore);
    Check(!CommitDiscordHostProfile({}, {{L"general", L"token", L"ambiguous-private"}, {L"general", L"token_protected", L"opaque-fixture"}}, options, error) &&
        ReadWholeFileBytes(g_iniPath, after) && after == hostBefore && ReadWholeFileBytes(profile, after) && after == profileBefore,
        "ambiguous nonempty private choices preserve exact host/profile bytes");
    Check(CommitDiscordHostProfile({}, {{L"general", L"token_protected", L"opaque-fixture"}}, options, error) &&
        aip::discord::ReadProfileDocument(profile, document, error) && document.profile.values[{L"general", L"token"}].empty() &&
        aip::discord::ResolveConfiguredToken(document.profile.values[{L"general", L"token"}], document.profile.values[{L"general", L"token_protected"}],
            fixtureDecoder, effectiveToken, credentialError) && effectiveToken == L"decoded-host-fixture" && decodes == 1,
        "explicit protected replacement selects the fake decoded value and clears the stale plaintext");
    Check(CommitDiscordHostProfile({}, {{L"general", L"token_protected", L""}}, options, error) &&
        aip::discord::ReadProfileDocument(profile, document, error) && document.profile.values[{L"general", L"token"}].empty() &&
        document.profile.values[{L"general", L"token_protected"}].empty(), "blank private replacement clears the configured credential family");
    const auto staleInlineHost = hostText + L"ClientId=stale-invalid-inline\r\n";
    Check(WriteContentFixture(staleInlineHost) && CommitDiscordHostProfile({}, {{L"general", L"activity_name", L"active advanced"}}, options, error) &&
        IniReadS(L"Discord", L"ClientId", L"") == L"stale-invalid-inline", "advanced host editing preserves inactive invalid inline credentials without blocking reload");
    HMENU discordMenu = CreatePopupMenu(); AppendContentEngineMenu(discordMenu);
    const auto inlineClientState = GetMenuState(discordMenu, ID_DISCORD_SETTINGS + 2, MF_BYCOMMAND);
    Check(inlineClientState != 0xffffffffu && !(inlineClientState & (MF_DISABLED | MF_GRAYED)),
        "inactive inline client control stays editable for repair before switching back");
    DestroyMenu(discordMenu);
    ReadWholeFileBytes(g_iniPath, hostBefore); ReadWholeFileBytes(profile, profileBefore);
    Check(!CommitDiscordHostProfile({{L"Discord", L"ProfilePath", L""}}, {{L"general", L"activity_name", L"cannot switch"}}, options, error) &&
        ReadWholeFileBytes(g_iniPath, after) && after == hostBefore && ReadWholeFileBytes(profile, after) && after == profileBefore,
        "switching back to invalid stored inline settings rejects the complete host/profile batch");
    Check(CommitDiscordHostProfile({{L"Discord", L"ClientId", L"12345678901234567"}}, {{L"general", L"activity_name", L"repair prepared"}}, options, error),
        "inactive inline values can be repaired explicitly while retaining the advanced profile");
    Check(SaveContentMenuSettings(nullptr, {{L"Discord", L"ProfilePath", L""}}) && IniReadS(L"Discord", L"ProfilePath", L"active").empty(),
        "the shared tray save route can switch to inline after explicit stored-value repair");
    // Preserve non-default encodings as raw bytes when the second save fails.
    const auto utf16 = [](const std::wstring& value) {
        std::vector<BYTE> bytes{0xff, 0xfe};
        const auto* data = reinterpret_cast<const BYTE*>(value.data());
        bytes.insert(bytes.end(), data, data + value.size() * sizeof(wchar_t)); return bytes;
    };
    hostBefore = utf16(hostText); profileBefore = utf16(profileText);
    Check(WriteWholeFileBytes(g_iniPath, hostBefore) && WriteWholeFileBytes(profile, profileBefore), "prepare exact UTF-16 rollback fixtures");
    options.privateInput = false;
    const auto invalidHost = utf16(hostText + L"; unrelated invalid UTF-16 comment " + std::wstring(1, static_cast<wchar_t>(0xd800)) + L"\r\n");
    Check(WriteWholeFileBytes(g_iniPath, invalidHost) &&
        !CommitDiscordHostProfile({}, {{L"general", L"activity_name", L"cannot publish"}}, options, error) &&
        ReadWholeFileBytes(g_iniPath, after) && after == invalidHost && ReadWholeFileBytes(profile, after) && after == profileBefore,
        "invalid Unicode in an unrelated host comment rejects both staged byte sequences before any publication");
    Check(WriteWholeFileBytes(g_iniPath, hostBefore), "restore valid exact-byte fixture after Unicode rejection");
    options.failHostWrite = [&] { const std::vector<BYTE> partial{0xef, 0xbb, 0xbf, 'x'}; WriteWholeFileBytes(g_iniPath, partial); return true; };
    Check(!CommitDiscordHostProfile({}, {{L"general", L"update_interval", L"19"}, {L"general", L"activity_name", L"rejected"}}, options, error),
        "second-file failure aborts the coordinated edit");
    Check(ReadWholeFileBytes(g_iniPath, after) && after == hostBefore && ReadWholeFileBytes(profile, after) && after == profileBefore,
        "rollback restores exact raw host/profile bytes including encoding and protected blob data");
    options.failHostWrite = {};
    options.optimistic = true; options.expectedHost = hostText; options.expectedProfile = profileText;
    Check(WriteUtf8BomTextFile(profile, profileText + L"; concurrent change\r\n") &&
        !CommitDiscordHostProfile({}, {{L"general", L"activity_name", L"stale editor"}}, options, error), "optimistic tray snapshot refuses to overwrite a concurrent profile edit");
    options = {}; options.create = true; options.createPath = created;
    Check(!CommitDiscordHostProfile({{L"Discord", L"ProfilePath", L"different-profile.ini"}}, {}, options, error) &&
        GetFileAttributesW(created.c_str()) == INVALID_FILE_ATTRIBUTES,
        "explicit setup rejects a contradictory host binding before creating a profile");
    options.failHostWrite = [] { return true; };
    Check(!CommitDiscordHostProfile({}, {}, options, error) && GetFileAttributesW(created.c_str()) == INVALID_FILE_ATTRIBUTES,
        "failed host binding removes a profile created by this transaction");
    options.failHostWrite = {};
    Check(CommitDiscordHostProfile({{L"Discord", L"RefreshSeconds", L"11"}}, {}, options, error) && IniReadS(L"Discord", L"ProfilePath", L"") == created &&
        IniReadS(L"Discord", L"RefreshSeconds", L"") == L"11" && aip::discord::ReadProfileDocument(created, document, error) &&
        document.profile.values[{L"general", L"update_interval"}] == L"11", "explicit setup stages normal host overrides and one coherent source cadence");
    ReadWholeFileBytes(created, profileBefore);
    Check(!CommitDiscordHostProfile({}, {}, options, error) && ReadWholeFileBytes(created, after) && after == profileBefore,
        "host explicit creation cannot replace an existing profile");
    g_commandLine = {}; g_commandLine.configureOnly = true;
    g_commandLine.discordProfileSettings = {{L"general", L"activity_name", L"passive command"}};
    Check(RunRenderOnlyCommand() == 0, "advanced command uses the passive configuration-only transaction path");
    Check(!g_contentDiscord.service || !g_contentDiscord.service->Read().running, "advanced edit/setup opens no Discord provider or transport");
    g_commandLine = savedCommand;
    DeleteFileW(profile.c_str()); DeleteFileW(created.c_str());
}

static void TestContentConfigurationSnapshots()
{
    const auto originalRefresh = g_iniCacheRefreshMs.load();
    g_iniCacheRefreshMs = 0;
    const std::wstring oldDocument = L"[Settings]\nContentSource=RssFeed\n[RssFeed]\nMaxItems=2\n[Content]\nEnabled=0\nCount=1\n"
        L"[CapsBlink]\nHardwareEnabled=0\nKeyboardTargetPath=\\Device\\KeyboardClass0\nBlinkIntervalMs=500\n"
        L"[AsusBlink]\nHardwareEnabled=0\nReadDiskActivity=0\nMicState=1\nErrorRetry=1\nEvent1HddState=128\n"
        L"[Notes.Resin]\nUID=800000001\nLToken=old-fixture-token\nLTUid=old-fixture-user\nRefreshSeconds=60\n";
    const std::wstring newDocument = L"[Settings]\nContentSource=RssFeed\n[RssFeed]\nMaxItems=4\n[Content]\nEnabled=0\nCount=1\n"
        L"[CapsBlink]\nHardwareEnabled=0\nKeyboardTargetPath=\\Device\\KeyboardClass1\nBlinkIntervalMs=600\n"
        L"[AsusBlink]\nHardwareEnabled=0\nReadDiskActivity=0\nMicState=0\nErrorRetry=7\nEvent1HddState=255\n"
        L"[Notes.Resin]\nUID=800000002\nLToken=new-fixture-token\nLTUid=new-fixture-user\nRefreshSeconds=90\n";
    const auto coherent = [](const aip::content::Configuration& cfg) {
        const bool old = cfg.notes.accounts[0].uid == L"800000001";
        if (!old && cfg.notes.accounts[0].uid != L"800000002") return false;
        const auto field = [&](const wchar_t* key, const wchar_t* expected) {
            const auto found = cfg.asus.settings.find(key);
            return found != cfg.asus.settings.end() && found->second == expected;
        };
        return cfg.enabled && cfg.cycle && cfg.entries.size() == (old ? 2u : 4u) &&
            cfg.caps.target == (old ? L"\\Device\\KeyboardClass0" : L"\\Device\\KeyboardClass1") && cfg.caps.intervalMs == (old ? 500 : 600) &&
            !cfg.caps.actionsEnabled && !cfg.asus.hardwareEnabled && !cfg.asus.readDiskActivity &&
            field(L"MicState", old ? L"1" : L"0") && field(L"ErrorRetry", old ? L"1" : L"7") && field(L"Event1HddState", old ? L"128" : L"255") &&
            cfg.notes.accounts[0].token == (old ? L"old-fixture-token" : L"new-fixture-token") &&
            cfg.notes.accounts[0].userId == (old ? L"old-fixture-user" : L"new-fixture-user") && cfg.notes.accounts[0].refreshSeconds == (old ? 60 : 90);
    };
    aip::content::Configuration cfg; std::wstring error;
    Check(WriteContentFixture(oldDocument) && ReadContentConfiguration(cfg, error) && coherent(cfg), "old configuration snapshot contains coherent scalar, event, account and RSS fallback values");
    Check(WriteContentFixture(newDocument) && ReadContentConfiguration(cfg, error) && coherent(cfg), "new configuration snapshot contains coherent scalar, event, account and RSS fallback values");
    std::atomic<bool> start{false}, writerFailed{false};
    std::atomic<DWORD> writerError{0};
    std::thread writer([&] {
        while (!start) std::this_thread::yield();
        for (int index = 0; index < 300; ++index) {
            const auto& document = index % 2 ? oldDocument : newDocument;
            if (!MutateDesktopStubIniWithWait([&](std::wstring& text) { text = document; return true; }, 1500)) {
                writerError = GetLastError(); writerFailed = true; return;
            }
            std::this_thread::sleep_for(milliseconds(1));
        }
    });
    start = true;
    int mixed = 0, readFailures = 0, emptyAccountSnapshots = 0;
    for (int index = 0; index < 200; ++index) {
        if (!ReadContentConfiguration(cfg, error)) ++readFailures;
        else if (!coherent(cfg)) { ++mixed; if (cfg.notes.accounts[0].uid.empty()) ++emptyAccountSnapshots; }
    }
    writer.join();
    if (writerFailed || readFailures || mixed) std::cerr << "Configuration snapshot diagnostics: writerError=" << writerError.load()
        << ", readFailures=" << readFailures << ", mixed=" << mixed << ", emptyAccountSnapshots=" << emptyAccountSnapshots << '\n';
    Check(!writerFailed && !readFailures && !mixed, "200 reads across 300 cooperating atomic publications return only whole old/new configurations");
    bool nested = false;
    Check(MutateDesktopStubIniWithWait([&](std::wstring&) {
        aip::content::Configuration published; std::wstring issue;
        nested = ReadContentConfiguration(published, issue) && coherent(published);
        return nested;
    }, 1500) && nested, "configuration snapshot acquisition safely reenters a same-thread named mutation scope");
    const int savedWait = g_iniWriteLockWaitMs.load();
    std::atomic<bool> holderReady{false}, holderLocked{false}, releaseHolder{false};
    std::thread holder([&] {
        aip::IniWriteMutexGuard scope(g_iniPath, 1500);
        holderLocked = scope.IsLocked(); holderReady = true;
        while (!releaseHolder) std::this_thread::sleep_for(milliseconds(1));
    });
    Check(HostUntil([&] { return holderReady.load(); }) && holderLocked, "a cooperating fixture owns the path lock for bounded snapshot failure validation");
    g_iniWriteLockWaitMs = 0;
    const auto retainedUid = cfg.notes.accounts[0].uid;
    Check(!ReadContentConfiguration(cfg, error) && !error.empty() && cfg.notes.accounts[0].uid == retainedUid,
        "snapshot lock failure returns unavailable status without changing the previous typed configuration");
    g_iniWriteLockWaitMs = savedWait;
    releaseHolder = true; holder.join();
    const auto inactive = L"; retain stale inline fields\n[AsusBlink]\nHardwareEnabled=0\nReadDiskActivity=0\nProfilePath=external-profile.ini\nMicState=bad\nEvent1MicState=2\nEvent1MicInterval=bad\n";
    Check(WriteContentFixture(inactive) && ReadContentConfiguration(cfg, error) && cfg.asus.profilePath == L"external-profile.ini" &&
        cfg.asus.settings.at(L"MicState") == L"bad" && cfg.asus.settings.at(L"Event1MicInterval") == L"bad",
        "runtime external-profile selection preserves and ignores invalid inactive inline patterns");
    Check(MutateDesktopStubIniWithWait([](std::wstring& text) { return WriteIniValueToText(text, L"AsusBlink", L"ProfilePath", L""); }, 1500) &&
        !ReadContentConfiguration(cfg, error), "returning to invalid inline patterns rejects the typed reload");
    g_iniCacheRefreshMs = originalRefresh;
}

int wmain()
{
    struct FixtureDeadline {
        std::atomic<bool> finished{false};
        const steady_clock::time_point expires = steady_clock::now() + std::chrono::seconds(45);
        std::thread thread;
        FixtureDeadline() : thread([this] {
            while (!finished && steady_clock::now() < expires) {
                const auto remaining = duration_cast<milliseconds>(expires - steady_clock::now());
                if (remaining.count() > 0) std::this_thread::sleep_for(std::min(milliseconds(100), remaining));
            }
            if (!finished) { std::cerr << "Inert host fixture exceeded 45 seconds.\n"; std::_Exit(124); }
        }) {}
        ~FixtureDeadline() { finished = true; thread.join(); }
    } fixtureDeadline;
    const auto fixtureStarted = steady_clock::now();
    const auto fixtureGroup = [&](const char* group) {
        std::cerr << "Host fixture group: " << group << "; " << duration_cast<milliseconds>(steady_clock::now() - fixtureStarted).count()
            << " ms; " << checks << " checks\n";
    };
    fixtureGroup("setup/menu");
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(_countof(temporary), temporary)) return 2;
    const auto directory = std::wstring(temporary) + L"DesktopStubContentTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    if (!CreateDirectoryW(directory.c_str(), nullptr)) return 2;
    const auto ini = directory + L"\\content.ini";
    const auto bitmapPath = directory + L"\\image.bmp";
    g_iniPath = ini; g_logPath = directory + L"\\test.log";
    g_exePath = aip::GetCurrentExecutablePath();
    g_logging = false; g_notificationsEnabled = false; g_console = false;

    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42; file.bfSize = 58; file.bfOffBits = 54;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info); info.biWidth = 1; info.biHeight = 1;
    info.biPlanes = 1; info.biBitCount = 24; info.biSizeImage = 4;
    std::vector<BYTE> bytes(58, 64);
    memcpy(bytes.data(), &file, sizeof(file)); memcpy(bytes.data() + 14, &info, sizeof(info));
    Check(WriteWholeFileBytes(bitmapPath, bytes), "temporary image fixture");
    aip::content::Entry solidEntry;
    solidEntry.background = L"None";
    const auto solidPath = ContentBackgroundPath(solidEntry);
    Check(!solidPath.empty() && GetFileAttributesW(solidPath.c_str()) != INVALID_FILE_ATTRIBUTES,
        "solid content background is generated in the product temporary snapshot directory");
    if (!solidPath.empty()) DeleteFileW(solidPath.c_str());
    aip::content::Entry unknownBackground;
    unknownBackground.background = L"Unknown";
    Check(ContentBackgroundPath(unknownBackground).empty(),
        "unknown content backgrounds do not silently become solid-color backgrounds");
    const std::wstring config = L"[Content]\r\nEnabled=true\r\nCount=2\r\nCycleEnabled=false\r\nTextMode=Overlay\r\n"
        L"[Content.1]\r\nName=First\r\nBackground=Image\r\nImagePath=image.bmp\r\nTextSources=CustomText\r\nText=alpha\r\nSecondaryText=beta\r\nBadgeText=42\r\n"
        L"[Content.2]\r\nEnabled=false\r\nName=Second\r\nBackground=None\r\nTextSources=CustomText\r\nText=gamma\r\n";
    Check(WriteContentFixture(config), "temporary INI fixture");
    LoadUiStrings();
#ifdef DESKTOPSTUB_REVISION_HOST_ONLY
#ifdef DESKTOPSTUB_NEW_SOURCE_HOST_ONLY
    fixtureGroup("source alerts and date/time/WuWa integration");
    TestSourceAlertHost();
    TestDateTimeTileHost(directory,bitmapPath);
    TestDesktopHostSetup(bitmapPath);
    TestFreshPresetPreview(directory);
    TestGalleryImageActivationHost(bitmapPath);
#else
#ifdef DESKTOPSTUB_GALLERY_LIFECYCLE_HOST_ONLY
    fixtureGroup("gallery recovery, CPU, idle maintenance and explicit names");
    TestGalleryUiFlowHost();
    fixtureGroup("idle maintenance");
    TestIdleMaintenanceHost();
    fixtureGroup("gallery CPU cache");
    TestGalleryCpuHost(directory, bitmapPath);
    fixtureGroup("content CPU cache");
    TestContentCpuCacheHost(directory);
    fixtureGroup("explicit source names");
    TestSourceTileRenameHost();
    fixtureGroup("published secondary asset reuse");
    TestPublishedSecondaryAssetReuseHost(directory);
#else
    fixtureGroup("Prayer cache, clock and presentation");
    TestPrayerCacheHost(directory);
    TestPrayerTimesHost(bitmapPath);
    TestTextPresentationHost();
    TestTileLifecycleHost(bitmapPath);
    fixtureGroup("independent instance providers and notifications");
    TestTileInstanceRuntimeHost();
    TestTileInstanceActivationHost();
    fixtureGroup("independent instance tray and policy settings");
    TestTileInstanceTrayHost();
    TestGenerationAdmissionHost(bitmapPath);
    fixtureGroup("owned Registration image publication and cleanup");
    TestTileRegistrationHost(bitmapPath);
#endif
#endif
    StopContentProviders(); PublishContentSnapshot({});
    DeleteFileW(ini.c_str()); DeleteFileW(bitmapPath.c_str()); DeleteFileW(g_logPath.c_str());
    RemoveDirectoryW(directory.c_str());
    fixtureGroup("complete");
    std::cout << checks << " extended host checks; " << failures << " failures\n";
    return failures ? 1 : 0;
#else
    TestPrimaryDesktopFactoryDefaults();
    {
        const auto defaults = aip::ParseIniDocument(BuildInitialIniTemplate());
        std::wstring prompt;
        Check(aip::ReadIniValueFromDoc(defaults, L"Strings", L"ContentPresetSwitchPrompt", prompt) &&
            prompt == g_ui.contentPresetSwitchPrompt,
            "new preset prompt round-trips through the generated INI without raw line breaks");
        const auto oldStrings = config + L"[Strings]\r\nContentSourceRestartRequired=The content source was saved.\r\n"
            L"ContentSourceRssFeed=RSS / Atom feed\r\nContentSourceWallpaper=My custom wallpaper label\r\n"
            L"GeneralTitle=General:\r\nAppearanceAssetsTitle=My custom display\r\nTileTextTitle=Windows tile text\r\n"
            L"TrayContentsTitle=My contents\r\nShowMenuAsDropdown=Show menu as dropdown\r\nGenerateNow=Generate now\r\n";
        Check(WriteContentFixture(oldStrings), "old preset string fixture");
        UpgradeRenamedStringDefaults();
        LoadUiStrings();
        Check(g_ui.contentPresetSwitchPrompt.find(L"Numbered entries stay saved") != std::wstring::npos &&
            g_ui.contentSourceRssFeed == L"RSS / Atom over wallpaper" &&
            g_ui.contentSourceWallpaper == L"My custom wallpaper label",
            "old profiles receive the new preset warning and retain custom labels");
        Check(g_ui.generalTitle == L"Application:" && g_ui.tileTextTitle == L"Default text (without tile contents)" &&
            g_ui.showMenuAsDropdown == L"Group settings into submenus" && g_ui.generateNow == L"Update tile now",
            "old stock settings labels upgrade to the new organization");
        Check(g_ui.appearanceAssetsTitle == L"My custom display" && g_ui.trayContentsTitle == L"My contents",
            "custom category labels survive the upgrade");
        Check(WriteContentFixture(config), "preset string regression restores test profile");
        LoadUiStrings();
    }
    Check(CompositionRequested(), "direct INI boolean agrees with typed config");
    Check(ContentEngineTick(true), "compose without generation or provider network");
    {
        ContentRenderScope scope;
        Check(scope.snapshot.active, "composition snapshot active");
        Check(scope.snapshot.backgroundPath == bitmapPath, "image path resolves against INI directory");
        Check(TileTextPrimaryText() == L"alpha" && TileTextSecondaryText() == L"beta" && TileTextBadgeText() == L"42", "all fields use composed snapshot");
        Check(TileTextOverlayEnabled() && ShouldBakeTileTextIntoAssets(true), "forced overlay works in live tile mode");
        Check(TileTextXmlText().empty() && TileTextXmlSecondaryText().empty() && TileTextXmlBadgeText().empty(), "baked text is not duplicated in native XML");
        auto next = scope.snapshot; next.text.primary = L"new";
        PublishContentSnapshot(next);
        Check(TileTextPrimaryText() == L"alpha", "provider update cannot split active generation");
        { ContentRenderScope nested; Check(TileTextPrimaryText() == L"new", "next generation gets new snapshot"); }
        Check(TileTextPrimaryText() == L"alpha", "nested snapshot restores parent");
    }
    Check(g_renderContent == nullptr, "render scope restores thread-local pointer");

    TestDisplaySettingsParity();
    TestCompositionGenerationRequests(config, bitmapPath);

    HMENU menu = CreatePopupMenu();
    std::vector<BYTE> menuIniBefore, menuIniAfter;
    Check(ReadWholeFileBytes(g_iniPath, menuIniBefore), "capture configuration before constructing settings");
    PopulateDesktopStubTrayMenu(menu, true, false);
    TrayMenuCommands groupedCommands;
    InspectTrayMenu(menu, groupedCommands);
    const UINT requiredCommands[] = {
        ID_CONTENT_OPTIONS, ID_CONTENT_OPTIONS + 1, ID_CONTENT_OPTIONS + 2,
        ID_CONTENT_OPTIONS + 3, ID_CONTENT_OPTIONS + 4, ID_CONTENT_ADD,
        ID_CONTENT_NOW_PLAYING_IMPORT, ID_CONTENT_SOURCE_WALLPAPER, ID_CONTENT_SOURCE_RSS_FEED,
        ID_RSS_FEED_URL, ID_RSS_FEED_USER_AGENT, ID_RSS_FEED_UPDATE_INTERVAL,
        ID_RSS_FEED_MAX_ITEMS, ID_RSS_FEED_HTTP_TIMEOUT, ID_RSS_FEED_MAX_FEED_BYTES,
        ID_RUN_AT_STARTUP, ID_RUN_AT_STARTUP_PACKAGED, ID_RUN_AT_STARTUP_PACKAGED_STATUS,
        ID_LIVE_TILE_MODE_LIVE_TILE, ID_LIVE_TILE_MODE_REGISTRATION, ID_LIVE_TILE_MODE_AUTO,
        ID_TILE_TEXT_ENABLED, ID_CAPS_SETTINGS + 30, ID_CAPS_SETTINGS + 31,
        ID_ASUS_SETTINGS + 30, ID_ASUS_SETTINGS + 31, ID_ASUS_SETTINGS + 32,
        ID_DISCORD_SETTINGS + 80, ID_NOTES_SETTINGS + 72,
        ID_GENERATED_ASSET_CACHE, ID_GENERATED_ASSET_PRECACHE, ID_GENERATE_NOW, ID_EXIT,
    };
    for (const auto command : requiredCommands)
        Check(groupedCommands.count(command) == 1, "existing configuration and action commands stay reachable");
    for (UINT index = 0; index < aip::discord::ProfileOptions().size(); ++index)
        Check(groupedCommands.count(ID_DISCORD_PROFILE_OPTION + index) == 1, "every advanced Discord field survives category grouping");
    for (UINT index = ID_MANIFEST_EDIT_IDENTITY_NAME; index <= ID_MANIFEST_EDIT_LIVE_TILE_BACKGROUND_TASK_NAME; ++index)
        Check(groupedCommands.count(index) == 1, "every manifest field survives category grouping");
    const auto inCategory = [&](UINT command, const wchar_t* title) {
        const auto found = groupedCommands.find(command);
        return found != groupedCommands.end() && found->second.path.find(title) != std::wstring::npos;
    };
    Check(inCategory(ID_CONTENT_ENTRY_OPTIONS + 3, L"Tile contents") &&
        inCategory(ID_CONTENT_ENTRY_OPTIONS + 3, L"Background"), "content image belongs to its entry background");
    Check(inCategory(ID_CONTENT_ENTRY_OPTIONS + 5, L"Custom text"), "custom text belongs to the content entry");
    Check(inCategory(ID_DISCORD_SETTINGS + 2, L"Sources and accounts") &&
        inCategory(ID_RSS_FEED_URL, L"Sources and accounts"), "provider setup is separated from content editing");
    Check(inCategory(ID_CONTENT_OPTIONS + 3, L"Updates and cycling"), "rotation timing has its own category");
    Check(inCategory(ID_CONTENT_TEXT_MODE, L"Tile display"), "text display lives with tile appearance");
    Check(inCategory(ID_CONTENT_NOW_PLAYING_IMPORT, L"Presets and imports"), "layout import lives with presets and imports");
    Check(inCategory(ID_MANIFEST_EDIT_IDENTITY_NAME, L"Advanced") &&
        inCategory(ID_GENERATED_ASSET_CACHE, L"Advanced"), "package and cache internals stay in Advanced");
    HMENU flatMenu = CreatePopupMenu();
    PopulateDesktopStubTrayMenu(flatMenu, false, false);
    TrayMenuCommands flatCommands; InspectTrayMenu(flatMenu, flatCommands);
    Check(flatCommands.size() == groupedCommands.size(), "flat and grouped menus expose the same command count");
    for(int i=0;i<GetMenuItemCount(menu);++i) {
        const auto category=GetSubMenu(menu,i);if(!category)continue;
        for(int row=0;row<GetMenuItemCount(category);++row) {
            MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_FTYPE;
            Check(GetMenuItemInfoW(category,row,TRUE,&item)&&!(item.fType&MFT_SEPARATOR),
                "first-level categories contain settings/submenus without unlabeled separators");
        }
    }
    for(int i=0;i<GetMenuItemCount(flatMenu);++i) {
        MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_FTYPE;
        if(!GetMenuItemInfoW(flatMenu,i,TRUE,&item)||!(item.fType&MFT_SEPARATOR))continue;
        wchar_t title[512]{};MENUITEMINFOW next{sizeof(next)};
        next.fMask=MIIM_ID|MIIM_STATE|MIIM_STRING;next.dwTypeData=title;next.cch=511;
        const bool read=GetMenuItemInfoW(flatMenu,i+1,TRUE,&next)!=FALSE;const std::wstring label=title;
        Check(read&&(next.wID==ID_EXIT||(!label.empty()&&label.back()==L':'&&(next.fState&MFS_DISABLED))),
            "flat separators introduce a disabled category heading or the final exit action");
    }
    for (const auto& command : groupedCommands) {
        const auto found = flatCommands.find(command.first);
        if (command.first == ID_SHOW_MENU_DROPDOWN) {
            Check(found != flatCommands.end() && (command.second.state & MFS_CHECKED) && !(found->second.state & MFS_CHECKED),
                "layout checkbox reflects grouped versus flat presentation");
            continue;
        }
        Check(found != flatCommands.end() && found->second.state == command.second.state,
            "flat and grouped menus preserve command IDs, checks and disabled states");
    }
    DestroyMenu(flatMenu);
    {
        std::wstring before;Check(ReadIniFileForMutation(before),"wording fixture captures the private profile");
        Check(!UiIsLabelControl(L"EDIT",WS_CHILD|WS_VISIBLE|WS_BORDER)&&!UiIsLabelControl(WINDOW_CLASS_NAME,WS_OVERLAPPEDWINDOW)&&
            UiIsLabelControl(L"STATIC",WS_CHILD|WS_VISIBLE)&&UiIsLabelControl(L"DialogWindow",WS_OVERLAPPEDWINDOW),
            "wording translation excludes editable values and hidden resident identity while recognizing labels and caption windows");
        auto changed=before;
        Check(WriteContentProfileSetting(changed,{L"Strings",L"Menu19002",L"My Desktop cycle switch"})&&WriteContentFixture(changed),
            "menu wording accepts the same Strings key stored by INI/CLI/tray editing");
        Check(WriteContentProfileSetting(changed,{L"Strings",UiLiteralKey(L"Original dialog wording"),L"Custom dialog wording"})&&WriteContentFixture(changed)&&
            UiLiteralText(L"Original dialog wording")==L"Custom dialog wording"&&UiLiteralText(L"Unchanged dialog wording")==L"Unchanged dialog wording",
            "dialog wording uses exact stable Strings overrides without changing unmatched text or configuration data");
        HMENU custom=CreatePopupMenu();PopulateDesktopStubTrayMenu(custom,true,false);TrayMenuCommands words;InspectTrayMenu(custom,words);
        Check(words.count(ID_CONTENT_OPTIONS+2)&&words[ID_CONTENT_OPTIONS+2].path.find(L"My Desktop cycle switch")!=std::wstring::npos,
            "menu wording override changes visible text while preserving its command ID");
        DestroyMenu(custom);Check(WriteContentFixture(before),"wording fixture restores the original private profile");
    }
    const DWORD menuResourcesBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int iteration = 0; iteration < 12; ++iteration) {
        HMENU repeated = CreatePopupMenu();
        PopulateDesktopStubTrayMenu(repeated, (iteration % 2) != 0, false);
        Check(DestroyMenu(repeated) != FALSE, "repeated menu trees are destroyed completely");
    }
    Check(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) == menuResourcesBefore,
        "repeated grouped and flat construction does not leak USER menu objects");
    Check(ReadWholeFileBytes(g_iniPath, menuIniAfter) && menuIniBefore == menuIniAfter,
        "constructing the entire menu does not write configuration");
    Check(!g_packagedStartup.worker.joinable(), "inert menu construction never starts a StartupTask worker");
    Check(ContentMenuValue(L"A & B") == L"A && B", "user content names keep literal ampersands instead of accidental menu accelerators");
    Check(WriteContentFixture(config + L"[Notes.Resin]\r\nLToken=fixture-menu-secret\r\nLTUid=fixture-menu-secret\r\n"),
        "synthetic account menu fixture");
    HMENU maskedMenu = CreatePopupMenu(); PopulateDesktopStubTrayMenu(maskedMenu, true, false);
    TrayMenuCommands maskedCommands; InspectTrayMenu(maskedMenu, maskedCommands);
    Check(std::all_of(maskedCommands.begin(), maskedCommands.end(), [](const auto& item) {
        return item.second.path.find(L"fixture-menu-secret") == std::wstring::npos;
    }), "regrouped account menus keep credential values masked");
    DestroyMenu(maskedMenu);
    Check(IniWrite(L"Content", L"Count", L"32"), "maximum content-list menu fixture");
    HMENU largestMenu = CreatePopupMenu(); PopulateDesktopStubTrayMenu(largestMenu, true, false);
    TrayMenuCommands largestCommands; InspectTrayMenu(largestMenu, largestCommands);
    for (UINT entry = 0; entry < 32; ++entry)
        Check(largestCommands.count(ID_CONTENT_ENTRY_OPTIONS + entry * CONTENT_ENTRY_COMMANDS + 3) == 1,
            "maximum-size content list retains every entry's image editor");
    DestroyMenu(largestMenu);
    Check(WriteContentFixture(config), "menu fixtures restore original content configuration");
    Check(GetMenuState(menu, ID_CONTENT_NOW_PLAYING_IMPORT, MF_BYCOMMAND) != 0xffffffffu,
        "menu contains provider settings, add action, NowPlaying import and entries");
    Check((GetMenuState(menu, ID_CONTENT_OPTIONS, MF_BYCOMMAND) & MF_CHECKED) != 0, "menu checks direct INI true");
    Check(DispatchContentEngineCommand(nullptr, ID_CONTENT_ENTRY_OPTIONS), "entry toggle dispatches through real menu handler");
    Check(IniReadS(L"Content.1", L"Enabled", L"1") == L"0", "tray toggle persists canonical value");
    Check(DispatchContentEngineCommand(nullptr, ID_CONTENT_ENTRY_OPTIONS), "entry toggles back");
    Check(IniReadS(L"Content.1", L"Enabled", L"0") == L"1", "tray and INI agree after round trip");
    Check(!DispatchContentEngineCommand(nullptr, ID_CONTENT_ENTRY_OPTIONS + 39), "unused dynamic menu ID is rejected");
    Check(DispatchContentEngineCommand(nullptr, ID_CONTENT_ENTRY_OPTIONS + 26), "source checkbox dispatches");
    Check(IniReadS(L"Content.1", L"TextSources", L"") == L"CustomText,CapsLock", "checkbox appends source preserving layer order");
    DispatchContentEngineCommand(nullptr, ID_CONTENT_ENTRY_OPTIONS + 26);
    Check(IniReadS(L"Content.1", L"TextSources", L"") == L"CustomText", "checkbox removes only selected source");
    DispatchContentEngineCommand(nullptr, ID_CONTENT_TEXT_MODE);
    Check(IniReadS(L"Content", L"TextMode", L"") == L"Auto", "text delivery choice persists canonical enum");
    DispatchContentEngineCommand(nullptr, ID_CONTENT_TEXT_MODE + 1);
    Check(SaveContentMenuSettings(nullptr, {{L"Content.1", L"RssItem", L"3"}}), "RSS item uses common typed setting path");
    Check(MoveContentMenuEntry(nullptr, 1, 2), "reorder commits once");
    Check(IniReadS(L"Content.2", L"Text", L"") == L"alpha" && IniReadS(L"Content.1", L"Text", L"") == L"gamma", "reorder moves entire content sections");
    Check(IniReadS(L"Content.2", L"RssItem", L"") == L"3", "reorder preserves extra source fields");
    Check(MoveContentMenuEntry(nullptr, 2, 1), "reorder restores original order");
    DispatchContentEngineCommand(nullptr, ID_CONTENT_ENTRY_OPTIONS + 41);
    Check(IniReadS(L"Content.1", L"NotesResource", L"") == L"Stamina", "notes resource choice uses typed setting path");
    Check(SaveContentMenuSettings(nullptr, {{L"Notes.Stamina", L"UID", L"600000001"}, {L"Notes.Stamina", L"LToken", L"fixture-only"}}), "synthetic notes account saves locally without fetch");
    Check(aip::content::SensitiveSetting(L"Notes.Stamina", L"LToken"), "notes credential is classified for log redaction");
    Check(IniReadS(L"Discord", L"SendPresence", L"0") == L"0", "Discord sending defaults off");
    Check(contentDialogs == 0, "routine settings do not produce modal prompts before preset tests");
    contentDialogs = 0;
    contentDialogText.clear();
    contentDialogResponse = IDNO;
    DispatchTrayCommand(nullptr, ID_CONTENT_SOURCE_WALLPAPER);
    Check(
        IniReadS(L"Content", L"Enabled", L"0") == L"true" &&
        contentDialogs == 1 &&
        contentDialogText.find(L"Numbered entries stay saved") != std::wstring::npos,
        "legacy preset warns before disabling layered content and leaves it unchanged when declined");
    contentDialogResponse = IDYES;
    DispatchTrayCommand(nullptr, ID_CONTENT_SOURCE_WALLPAPER);
    Check(
        IniReadS(L"Content", L"Enabled", L"") == L"0" &&
        IniReadS(L"Settings", L"ContentSource", L"") == L"Wallpaper" &&
        IniReadS(L"Content.1", L"Text", L"") == L"alpha" &&
        IniReadS(L"Content.2", L"Text", L"") == L"gamma",
        "accepted legacy preset disables layered content while retaining its saved entries");
    Check(WriteContentFixture(config), "legacy preset regression restores layered test profile");
    LoadUiStrings();
    contentDialogs = 0;
    contentDialogText.clear();
    contentDialogResponse = IDNO;
    const auto inactiveCapsConfig =
        L"[Content]\r\nEnabled=true\r\nCount=2\r\nCycleEnabled=true\r\nCycleSeconds=60\r\nTextMode=Overlay\r\n"
        L"[Content.1]\r\nEnabled=true\r\nName=Visible\r\nBackground=Image\r\nImagePath=" + bitmapPath +
        L"\r\nTextSources=CustomText\r\nText=visible\r\n"
        L"[Content.2]\r\nEnabled=true\r\nName=Later\r\nBackground=None\r\nTextSources=CapsBlink\r\n"
        L"[CapsBlink]\r\nHardwareEnabled=0\r\nKeyboardTargetPath=\\Device\\KeyboardClass0\r\nBlinkIntervalMs=500\r\n";
    g_contentCaps.Stop();
    g_contentCaps.controller.reset();
    Check(WriteContentFixture(inactiveCapsConfig) && ContentEngineTick(true),
        "inactive hardware source scope profile composes without an external provider");
    Check(!g_contentCaps.controller,
        "an enabled CapsBlink entry outside the active cycle slot does not start its inert controller");
    const auto activeCapsConfig =
        L"[Content]\r\nEnabled=true\r\nCount=2\r\nCycleEnabled=true\r\nCycleSeconds=60\r\nTextMode=Overlay\r\n"
        L"[Content.1]\r\nEnabled=true\r\nName=Visible\r\nBackground=Image\r\nImagePath=" + bitmapPath +
        L"\r\nTextSources=CapsBlink\r\n"
        L"[Content.2]\r\nEnabled=true\r\nName=Later\r\nBackground=None\r\nTextSources=CustomText\r\nText=later\r\n"
        L"[CapsBlink]\r\nHardwareEnabled=0\r\nKeyboardTargetPath=\\Device\\KeyboardClass0\r\nBlinkIntervalMs=500\r\n";
    Check(WriteContentFixture(activeCapsConfig) && ContentEngineTick(true),
        "active hardware source scope profile composes with an inert provider");
    Check(g_contentCaps.controller && !g_contentCaps.controller->GetSnapshot().actionsEnabled,
        "the selected CapsBlink entry starts only the injected inert preview controller");
    g_contentCaps.Stop();
    Check(WriteContentFixture(config) && ContentEngineTick(true),
        "cycle-scope regression restores the baseline temporary configuration");
    aip::caps::Config capsPreview;
    g_contentCaps.Refresh(capsPreview);
    Check(g_contentCaps.controller && !g_contentCaps.controller->GetSnapshot().actionsEnabled &&
        g_contentCaps.Read().badge.empty() && g_contentCaps.Read().secondary.find(L"Preview") != std::wstring::npos, "Caps preview works with no native backend linked");
    aip::content::AsusConfiguration asusPreview;
    asusPreview.settings = {{L"MicState", L"0,1"}, {L"MicInterval", L"100"}, {L"MicDuration", L"once"}};
    g_contentAsus.Refresh(asusPreview);
    const auto asusDeadline = steady_clock::now() + milliseconds(2000);
    while (g_contentAsus.service && g_contentAsus.service->Read().pattern.micState < 0 && steady_clock::now() < asusDeadline)
        std::this_thread::sleep_for(milliseconds(1));
    Check(g_contentAsus.service && !g_contentAsus.service->Read().hardwareEnabled &&
        g_contentAsus.Read().badge.empty() && g_contentAsus.Read().secondary.find(L"Preview") != std::wstring::npos, "ASUS preview runs without native driver or disk-counter backend");
    g_contentAsus.Cancel(); g_contentCaps.Cancel();
    aip::discord::Snapshot preview;
    preview.phase = aip::discord::Phase::Preview;
    preview.details = L"fixture details"; preview.state = L"fixture state";
    const auto previewText = aip::content::DiscordContent(preview);
    Check(previewText.primary == L"Discord presence" && previewText.secondary.find(L"fixture details") != std::wstring::npos &&
        previewText.secondary.find(L"fixture state") != std::wstring::npos && previewText.secondary.find(L"Preview") != std::wstring::npos &&
        previewText.badge.empty(), "Discord preview keeps a compact headline and preserves wrapped details and state without transport");
    {
        // Construct the separately linked service after all module globals.
        // This catches the host's earlier static-initialization crash without
        // reading a user profile/context or creating an actual transport.
        aip::discord::Service fixtureService;
        aip::discord::Options options;
        auto profile = std::make_shared<aip::discord::Profile>();
        profile->values[{L"general", L"details_template"}] = L"fixture details";
        options.profileOverride = profile;
        options.contextOverride = std::make_shared<aip::discord::Context>();
        std::wstring error;
        const bool started = fixtureService.Start(options, error);
        Check(started, "linked Discord service starts with synthetic inputs");
        const auto deadline = steady_clock::now() + milliseconds(2000);
        while (started && fixtureService.Read().phase == aip::discord::Phase::Starting && steady_clock::now() < deadline)
            std::this_thread::sleep_for(milliseconds(1));
        Check(fixtureService.Read().phase == aip::discord::Phase::Preview, "linked service renders preview without real system context or sending");
        Check(fixtureService.Stop(2000), "linked fixture service stops within deadline");
    }
    Check(contentDialogs == 0, "routine settings do not produce modal prompts");
    fixtureGroup("SMTC artwork");
    TestSmtcArtworkHost(bytes);
    fixtureGroup("SMTC cadence and invalid edits");
    TestSmtcCadenceAndInvalidCancellation();
    fixtureGroup("Notes imports");
    TestNotesImportsAndHost();
    fixtureGroup("hardware imports");
    TestHardwareProfileHostImports();
    fixtureGroup("configuration snapshots");
    TestContentConfigurationSnapshots();
    fixtureGroup("NowPlaying imports");
    TestNowPlayingProfileHostImports(bytes);
    fixtureGroup("presets and import confirmations");
    TestNativeQueueHost(bitmapPath);
    TestSecondaryTileHost(bitmapPath);
    fixtureGroup("gallery delivery, ledger and setup");
    TestGalleryHost(bitmapPath);
    fixtureGroup("gallery submitted status retention");
    TestGalleryStatusRetentionHost(bitmapPath);
    fixtureGroup("gallery native private ledger");
    TestGalleryLedgerHost(directory);
    fixtureGroup("gallery setup staging");
    TestGallerySetupHost();
    fixtureGroup("gallery pin batch policy");
    TestGalleryPinBatchHost();
    fixtureGroup("tile status and icon lifetime");
    TestTileStatusHost();
    fixtureGroup("primary asset retention");
    TestPrimaryAssetRetentionHost(directory);
    fixtureGroup("hidden paged text dialog");
    TestPagedTextDialogHost();
    fixtureGroup("content presets and confirmation policy");
    TestContentPresetsAndImportConfirmation();
    fixtureGroup("Discord advanced profile");
    TestDiscordAdvancedHost();
    fixtureGroup("service lifecycles");
    Check(WriteContentFixture(config) && ContentEngineTick(true), "media fixtures restore the original custom-image profile");
    TestServiceHostLifecycles();
    TestCapsHostAndShutdownCache();
    fixtureGroup("snapshot stress");
    DestroyMenu(menu);

    aip::content::Snapshot stable;
    stable.active = true; stable.backgroundPath = bitmapPath;
    stable.text = {L"0", L"0", L"0"};
    PublishContentSnapshot(stable);
    std::atomic<bool> done{false};
    std::atomic<int> mixed{0};
    DWORD handlesBefore = 0, handlesAfter = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
    std::vector<std::thread> readers;
    for (int n = 0; n < 4; ++n) readers.emplace_back([&] {
        while (!done.load()) {
            ContentRenderScope scope;
            if (TileTextPrimaryText() != TileTextSecondaryText() || TileTextPrimaryText() != TileTextBadgeText()) ++mixed;
        }
    });
    for (int n = 0; n < 20000; ++n) {
        auto text = std::to_wstring(n);
        stable.text = {text, text, text}; PublishContentSnapshot(stable);
    }
    done = true;
    for (auto& reader : readers) reader.join();
    readers.clear();
    GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
    Check(mixed == 0, "20,000 updates preserve snapshot consistency across four readers");
    Check(handlesAfter <= handlesBefore + 2, "snapshot stress does not leak thread handles");
    Check(IniReadS(L"Content.1", L"Text", L"") == L"alpha", "providers do not overwrite configured custom text");
    StopContentProviders();
    PublishContentSnapshot({});
    fixtureGroup("offline persistence");
    TestOfflineConfigurationPersistence(directory);
    fixtureGroup("complete");
    DeleteFileW(ini.c_str()); DeleteFileW(bitmapPath.c_str()); DeleteFileW(g_logPath.c_str());
    RemoveDirectoryW(directory.c_str());
    std::cout << checks << " content runtime/menu checks; " << failures << " failures\n";
    return failures ? 1 : 0;
#endif
}
