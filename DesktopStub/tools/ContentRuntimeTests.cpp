// Compile the real host code, but never call its entry point. These tests use
// temporary configuration, in-memory menus and snapshots; no AppX, startup,
// notifications, playback or network operations are requested.
#define NOMINMAX
#include <windows.h>
static int contentDialogs = 0;
static int ContentTestMessageBox(HWND, LPCWSTR, LPCWSTR, UINT flags)
{
    ++contentDialogs;
    return (flags & MB_YESNO) ? IDNO : IDOK;
}
#define DESKTOPSTUB_CONTENT_MESSAGEBOX ContentTestMessageBox
#define DESKTOPSTUB_INERT_HARDWARE
static int contentLogicalCaps = 0, contentLogicalSamples = 0;
static int ContentTestLogicalCaps() { ++contentLogicalSamples; return contentLogicalCaps; }
#define DESKTOPSTUB_TEST_LOGICAL_CAPS ContentTestLogicalCaps
#define wWinMain DesktopStubUnusedEntryPoint
#include "../DesktopStub.cpp"
#undef wWinMain
#include <iostream>

static int checks = 0, failures = 0;
static void Check(bool value, const char* name)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
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

int wmain()
{
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
    const std::wstring config = L"[Content]\r\nEnabled=true\r\nCount=2\r\nCycleEnabled=false\r\nTextMode=Overlay\r\n"
        L"[Content.1]\r\nName=First\r\nBackground=Image\r\nImagePath=image.bmp\r\nTextSources=CustomText\r\nText=alpha\r\nSecondaryText=beta\r\nBadgeText=42\r\n"
        L"[Content.2]\r\nEnabled=false\r\nName=Second\r\nBackground=None\r\nTextSources=CustomText\r\nText=gamma\r\n";
    Check(WriteUtf8BomTextFile(ini, config), "temporary INI fixture");
    LoadUiStrings();
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

    HMENU menu = CreatePopupMenu();
    AppendContentEngineMenu(menu);
    Check(GetMenuItemCount(menu) == static_cast<int>(_countof(g_contentOptions)) + 7, "menu contains provider settings, add action and entries");
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
    Check(previewText.primary == L"fixture details" && previewText.secondary.find(L"fixture state") != std::wstring::npos && previewText.secondary.find(L"Preview") != std::wstring::npos && previewText.badge.empty(), "Discord snapshot maps to tile preview without transport");
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
    TestServiceHostLifecycles();
    TestCapsHostAndShutdownCache();
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
    DeleteFileW(ini.c_str()); DeleteFileW(bitmapPath.c_str()); DeleteFileW(g_logPath.c_str());
    RemoveDirectoryW(directory.c_str());
    std::cout << checks << " content runtime/menu checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}
