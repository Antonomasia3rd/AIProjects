// Compile the real host code, but never call its entry point. These tests use
// temporary configuration, in-memory menus and snapshots; no AppX, startup,
// notifications, playback or network operations are requested.
#define NOMINMAX
#include <windows.h>
#include <string>
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

static bool WriteContentFixture(const std::wstring& text)
{
    if (!WriteUtf8BomTextFile(g_iniPath, text)) return false;
    // External edits are intentionally cached by the host for 250 ms. Fixture
    // changes must observe the newly written file before checking its behavior,
    // rather than accidentally exercising whichever preceding profile is cached.
    std::wstring loaded;
    return ReadIniFileForMutation(loaded) && loaded == text;
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
        std::thread thread;
        FixtureDeadline() : thread([this] {
            for (int tick = 0; tick < 450 && !finished; ++tick) std::this_thread::sleep_for(milliseconds(100));
            if (!finished) { std::cerr << "Inert host fixture exceeded 45 seconds.\n"; std::_Exit(124); }
        }) {}
        ~FixtureDeadline() { finished = true; thread.join(); }
    } fixtureDeadline;
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
    {
        const auto defaults = aip::ParseIniDocument(BuildInitialIniTemplate());
        std::wstring prompt;
        Check(aip::ReadIniValueFromDoc(defaults, L"Strings", L"ContentPresetSwitchPrompt", prompt) &&
            prompt == g_ui.contentPresetSwitchPrompt,
            "new preset prompt round-trips through the generated INI without raw line breaks");
        const auto oldStrings = config + L"[Strings]\r\nContentSourceRestartRequired=The content source was saved.\r\n"
            L"ContentSourceRssFeed=RSS / Atom feed\r\nContentSourceWallpaper=My custom wallpaper label\r\n";
        Check(WriteContentFixture(oldStrings), "old preset string fixture");
        UpgradeRenamedStringDefaults();
        LoadUiStrings();
        Check(g_ui.contentPresetSwitchPrompt.find(L"Numbered entries stay saved") != std::wstring::npos &&
            g_ui.contentSourceRssFeed == L"RSS / Atom over wallpaper" &&
            g_ui.contentSourceWallpaper == L"My custom wallpaper label",
            "old profiles receive the new preset warning and retain custom labels");
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

    HMENU menu = CreatePopupMenu();
    AppendContentEngineMenu(menu);
    Check(GetMenuItemCount(menu) == static_cast<int>(_countof(g_contentOptions)) + 8 &&
        GetMenuState(menu, ID_CONTENT_NOW_PLAYING_IMPORT, MF_BYCOMMAND) != 0xffffffffu,
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
    TestSmtcArtworkHost(bytes);
    TestNotesImportsAndHost();
    TestHardwareProfileHostImports();
    TestContentConfigurationSnapshots();
    TestNowPlayingProfileHostImports(bytes);
    Check(WriteContentFixture(config) && ContentEngineTick(true), "media fixtures restore the original custom-image profile");
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
    TestOfflineConfigurationPersistence(directory);
    DeleteFileW(ini.c_str()); DeleteFileW(bitmapPath.c_str()); DeleteFileW(g_logPath.c_str());
    RemoveDirectoryW(directory.c_str());
    std::cout << checks << " content runtime/menu checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}
