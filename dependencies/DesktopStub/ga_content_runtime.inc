// Built-in data providers share one asynchronous lifecycle and result cache.
#ifndef DESKTOPSTUB_CONTENT_QUEUE_GENERATE
#define DESKTOPSTUB_CONTENT_QUEUE_GENERATE QueueGenerate
#endif
#ifndef DESKTOPSTUB_CONTENT_QUEUE_BATCH_GENERATE
#define DESKTOPSTUB_CONTENT_QUEUE_BATCH_GENERATE QueueGenerateContentBatch
#endif
#ifndef DESKTOPSTUB_SECONDARY_BATCH_GENERATE
#define DESKTOPSTUB_SECONDARY_BATCH_GENERATE QueueGenerateSecondaryBatch
#endif
#include "../secondary_tiles.h"
#include "../content_sources/smtc.inc"
#include "../content_source_host.h"
#include "../content_sources/notes.inc"
#include "../content_sources/notes_tile_rows.h"
#include "../content_sources/notes_profile.inc"
#include "../content_sources/discord.h"

// Hardware sources have a separate compile boundary.  The normal product build
// keeps them enabled for compatibility, while a host built with 0 contains no
// raw keyboard or ASUS ACPI backend.
#ifndef DESKTOPSTUB_ENABLE_HARDWARE_SOURCES
#define DESKTOPSTUB_ENABLE_HARDWARE_SOURCES 1
#endif
#if DESKTOPSTUB_ENABLE_HARDWARE_SOURCES != 0 && DESKTOPSTUB_ENABLE_HARDWARE_SOURCES != 1
#error DESKTOPSTUB_ENABLE_HARDWARE_SOURCES must be 0 or 1.
#endif

static bool ReadContentLogicalCaps(bool& enabled, std::wstring& error)
{
#ifndef DESKTOPSTUB_INERT_HARDWARE
    const HWND window = g_shutdownWindow.load();
    DWORD_PTR state = 0;
    if (window && !g_shutdownState.IsRequested() && SendMessageTimeoutW(window, WM_APP_CONTENT_LOGICAL_CAPS,
            0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &state)) {
        enabled = state != 0; return true;
    }
#endif
    const int cached = g_contentLogicalCaps.load();
    if (cached >= 0) { enabled = cached != 0; return true; }
    error = L"The logical Caps Lock state is unavailable."; return false;
}

#if DESKTOPSTUB_ENABLE_HARDWARE_SOURCES
#include "ga_content_hardware.inc"
#else
#include "ga_content_hardware_unavailable.inc"
#endif

struct ContentDiscordProvider
{
    std::unique_ptr<aip::discord::Service> service;
    bool started = false, stopping = false;
    std::wstring key, error;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    std::function<void(aip::discord::Options&)> configureTestOptions;
#endif
    void RequestStop()
    {
        stopping = service && !service->Stop(0);
        if (!stopping) started = false;
    }
    void Refresh(const aip::content::DiscordConfiguration& cfg, bool synchronous)
    {
        if (stopping) { RequestStop(); if (stopping) return; }
        if (!service) service = std::make_unique<aip::discord::Service>();
        aip::discord::Options options;
        options.sendEnabled = cfg.send; options.clearOnStop = cfg.clearOnStop;
        options.refreshMs = cfg.refreshSeconds * 1000; options.operationTimeoutMs = cfg.timeoutMs;
        auto path = TrimCopy(cfg.profilePath);
        if (!path.empty()) {
            if (PathIsRelativeW(path.c_str())) path = ParentDirectoryOfPath(g_iniPath) + L"\\" + path;
            options.profilePath = path;
        } else {
            auto profile = std::make_shared<aip::discord::Profile>();
            profile->values[{L"general", L"client_id"}] = cfg.clientId;
            profile->values[{L"general", L"activity_name"}] = cfg.name;
            profile->values[{L"general", L"details_template"}] = cfg.details;
            profile->values[{L"general", L"state_template"}] = cfg.state;
            profile->values[{L"general", L"transport_mode"}] = L"ipc";
            options.profileOverride = profile;
        }
#ifdef DESKTOPSTUB_INERT_HARDWARE
        if (configureTestOptions) configureTestOptions(options);
#endif
        auto requestedKey = std::to_wstring(cfg.send) + L":" + std::to_wstring(cfg.clearOnStop) + L":" + std::to_wstring(cfg.refreshSeconds) + L":" + std::to_wstring(cfg.timeoutMs);
        for (const auto& field : {path, FileFingerprint(path), cfg.clientId, cfg.name, cfg.details, cfg.state})
            requestedKey += L":" + std::to_wstring(field.size()) + L":" + field;
        if (started && !service->Read().running) { RequestStop(); if (stopping) return; key.clear(); }
        if (key != requestedKey) {
            key = requestedKey;
            const bool ok = started ? service->Reload(options, error) : service->Start(options, error);
            if (ok) { started = true; error.clear(); }
            else RequestStop();
        }
        if (synchronous && started && error.empty()) {
            const auto deadline = steady_clock::now() + milliseconds(cfg.timeoutMs + 1000);
            for (;;) {
                const auto snapshot = service->Read();
                if (snapshot.phase == aip::discord::Phase::Preview || snapshot.phase == aip::discord::Phase::Active || snapshot.phase == aip::discord::Phase::Error) break;
                if (!g_running || steady_clock::now() >= deadline) {
                    error = L"The Discord source did not finish before the one-shot deadline.";
                    key.clear(); RequestStop(); break;
                }
                std::this_thread::sleep_for(milliseconds(5));
            }
        }
    }
    aip::content::Text Read()
    {
        if (!error.empty()) return {L"Discord source unavailable", L"Error: " + error, L""};
        return aip::content::DiscordContent(service ? service->Read() : aip::discord::Snapshot{});
    }
    void Cancel() { key.clear(); error.clear(); RequestStop(); }
    void Stop() { Cancel(); if (service && service->Stop(5000)) { service.reset(); stopping = false; } started = false; }
};
static ContentDiscordProvider g_contentDiscord;

struct ContentNotesProvider
{
    std::array<aip::content::AsyncSource<aip::notes::Snapshot>, 3> sources;
    std::array<bool, 3> waitExpired{};
    std::array<std::wstring, 3> keys;
    std::atomic<unsigned> refreshRequested{0};
#ifdef DESKTOPSTUB_INERT_HARDWARE
    aip::notes::CredentialDecoder testDecoder;
    aip::notes::HttpTransport testTransport;
#endif
    static std::wstring Key(const aip::content::NotesConfiguration& cfg, int resource)
    {
        const auto& account = cfg.accounts[resource];
        return std::to_wstring(resource) + L":" + account.uid + L"\n" + account.server + L"\n" + account.token + L"\n" + account.userId +
            L"\n" + account.tokenProtected + L"\n" + account.userIdProtected + L"\n" + std::to_wstring(account.refreshSeconds) +
            L"\n" + std::to_wstring(cfg.refreshSeconds) + L":" + std::to_wstring(cfg.timeoutSeconds) + L":" + std::to_wstring(cfg.maximumBytes);
    }
    void Refresh(const aip::content::NotesConfiguration& cfg, int resource, bool synchronous)
    {
        waitExpired[resource] = false;
        aip::notes::Configuration settings;
        settings.resource = static_cast<aip::notes::ResourceKind>(resource);
        settings.refreshSeconds = static_cast<UINT>(cfg.accounts[resource].refreshSeconds ? cfg.accounts[resource].refreshSeconds : cfg.refreshSeconds);
        settings.timeout = milliseconds(cfg.timeoutSeconds * 1000);
        settings.maximumBytes = static_cast<size_t>(cfg.maximumBytes);
        const auto key = Key(cfg, resource);
        const bool force = (refreshRequested.fetch_and(~(1u << resource)) & (1u << resource)) != 0;
        if (keys[resource] != key || force) sources[resource].CancelPending();
        keys[resource] = key;
        aip::notes::CredentialDecoder decoder;
        aip::notes::HttpTransport transport;
#ifdef DESKTOPSTUB_INERT_HARDWARE
        decoder = testDecoder;
        transport = testTransport ? testTransport : [](const auto&, auto&, std::wstring& error) { error = L"The inert host has no Notes network transport."; return false; };
#else
        decoder = aip::notes::DecodeNativeCredential;
        transport = aip::FetchHttpBytes;
#endif
        sources[resource].Request(key, milliseconds(static_cast<long long>(settings.refreshSeconds) * 1000),
            [settings, account = cfg.accounts[resource], decoder, transport](const aip::content::AsyncSource<aip::notes::Snapshot>::Cancel& canceled) mutable {
                settings.canceled = [canceled] { return !g_running.load() || canceled(); };
                if (settings.canceled()) return aip::notes::MakeError(settings.resource, L"Canceled", L"Notes refresh was canceled", settings.refreshSeconds);
                aip::notes::Account resolved;
                std::wstring error;
                if (!aip::notes::ResolveStoredAccount(account, resolved, error, decoder))
                    return aip::notes::MakeError(settings.resource, L"Configuration error", error, settings.refreshSeconds);
                settings.uid = resolved.uid; settings.ltoken = resolved.ltoken; settings.ltuid = resolved.ltuid;
                settings.server = resolved.server;
                return aip::notes::Fetch(settings, transport);
            }, WakePollThread, synchronous || force);
        if (synchronous && !sources[resource].Wait(key, settings.timeout + milliseconds(1000))) {
            waitExpired[resource] = true; sources[resource].CancelPending();
        }
    }
    aip::notes::Snapshot ReadSnapshot(const aip::content::NotesConfiguration& cfg, int resource)
    {
        aip::notes::Snapshot snapshot;
        snapshot.resource = static_cast<aip::notes::ResourceKind>(resource);
        if (waitExpired[resource]) {
            return aip::notes::MakeError(snapshot.resource, L"Notes refresh timed out", L"No fresh result before the one-shot deadline.",
                static_cast<UINT>(cfg.accounts[resource].refreshSeconds ? cfg.accounts[resource].refreshSeconds : cfg.refreshSeconds));
        }
        std::string error;
        if (!sources[resource].Read(Key(cfg, resource), snapshot, error)) {
            snapshot.line1 = L"Real-Time Notes";
            snapshot.line2 = error.empty() ? L"Loading resource information..." : L"The notes source could not complete.";
        }
        return snapshot;
    }
    static aip::content::Text FormatSnapshot(const aip::notes::Snapshot& snapshot, aip::notes::DetailId detail)
    {
        if (detail == aip::notes::DetailId::Summary || snapshot.state == aip::notes::StateKind::Error || snapshot.state == aip::notes::StateKind::Loading)
            return {snapshot.line1, snapshot.line2, L""};
        const auto* choice = aip::notes::FindDetail(detail);
        const auto found = snapshot.details.find(detail);
        if (!choice || found == snapshot.details.end()) return {L"Notes detail unavailable", choice ? choice->label : L"Unknown detail", L""};
        return {choice->label, found->second, L""};
    }
    aip::content::Text Read(const aip::content::NotesConfiguration& cfg, int resource, aip::notes::DetailId detail = aip::notes::DetailId::Summary)
    {
        return FormatSnapshot(ReadSnapshot(cfg, resource), detail);
    }
    void Cancel(int resource) { sources[resource].CancelPending(); keys[resource].clear(); }
    void Cancel() { for (auto& source : sources) source.CancelPending(); }
    void Stop() { Cancel(); for (auto& source : sources) source.Stop(); }
};
static ContentNotesProvider g_contentNotes;
static void RequestContentNotesRefresh(int resource)
{
    if (resource < 0 || resource > 3) return;
    const auto mask = resource == 3 ? 7u : 1u << resource;
    g_contentNotes.refreshRequested.fetch_or(mask);
    for (int index = 0; index < 3; ++index) if (mask & (1u << index)) g_contentNotes.sources[index].CancelPending();
    WakePollThread();
}

struct ContentSmtcProvider
{
    // Declared before source: source joins/cleans its worker before the last
    // host-side owner of the (already reset) native session can be destroyed.
    std::shared_ptr<aip::content::SmtcWorkerSession> nativeSession;
    aip::content::AsyncSource<aip::content::SmtcMediaSnapshot> source;
    bool waitExpired = false;
    std::wstring key, profile;
    std::shared_ptr<aip::content::SmtcArtworkCache> cache;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    aip::content::SmtcSessionReader testReader;
#endif
    ContentSmtcProvider()
        : nativeSession(std::make_shared<aip::content::SmtcWorkerSession>()),
          source([session = nativeSession] { session->Reset(); }) {}
    void Refresh(int seconds, bool synchronous, bool artworkNeeded = false, bool useEvents = false, bool appIconNeeded = false)
    {
        waitExpired = false;
        std::wstring currentProfile;
        { std::lock_guard<std::mutex> lock(g_pathMutex); currentProfile = aip::MakeAbsolutePath(g_iniPath); }
        if (profile != currentProfile) { profile = currentProfile; cache = std::make_shared<aip::content::SmtcArtworkCache>(L"DesktopStub", profile); }
        const auto requestedKey = L"SMTC:" + profile + (artworkNeeded ? L":artwork" : L":text") + (useEvents ? L":events" : L":poll") + (appIconNeeded ? L":appicon" : L"");
        const bool changed = requestedKey != key;
        key = requestedKey;
        // An event storm must never force-cancel the fetch already obtaining a
        // coherent text/artwork pair. Dirty stays set until a later idle tick.
        if (useEvents && !changed && !synchronous && source.HasPendingWork()) return;
        int effectiveSeconds = seconds;
        if (useEvents) {
            aip::content::SmtcMediaSnapshot cached;
            std::string cachedError;
            const bool haveCached = source.Read(key, cached, cachedError);
            if (!haveCached || nativeSession->ReportEventsSupported()) effectiveSeconds = (std::max)(30, seconds);
        }
        const bool eventRefresh = useEvents && nativeSession->ConsumeDirty();
        aip::content::SmtcSessionReader reader;
#ifdef DESKTOPSTUB_INERT_HARDWARE
        reader = testReader ? testReader : [](auto, bool, const auto&) -> aip::content::SmtcSessionData {
            throw std::runtime_error("The inert host has no system SMTC reader.");
        };
#else
        reader = [session = nativeSession, useEvents, appIconNeeded](auto deadline, bool artwork, const auto& canceled) {
            return session->Read(deadline, artwork, canceled, useEvents, WakePollThread, appIconNeeded);
        };
#endif
        source.Request(key, milliseconds(static_cast<long long>(effectiveSeconds) * 1000),
            [reader, artworkNeeded, appIconNeeded, artworkCache = cache](const aip::content::AsyncSource<aip::content::SmtcMediaSnapshot>::Cancel& canceled) {
                aip::content::SmtcMediaSnapshot result;
                if (canceled()) return result;
                std::wstring error;
                if (!aip::content::ReadSmtcMedia(result, error, milliseconds(2500), artworkNeeded, reader, canceled, artworkCache.get(), appIconNeeded)) {
                    result.text.primary = L"Media source unavailable"; result.text.secondary = error;
                }
                return result;
            }, WakePollThread, synchronous || eventRefresh);
        if (synchronous && !source.Wait(key, milliseconds(3000))) { waitExpired = true; source.CancelPending(); }
    }
    aip::content::SmtcMediaSnapshot Read()
    {
        aip::content::SmtcMediaSnapshot result;
        if (waitExpired) { result.text = {L"Media refresh timed out", L"No fresh result before the one-shot deadline.", L""}; return result; }
        std::string error;
        if (!source.Read(key, result, error)) {
            result.text.primary = L"Now playing";
            result.text.secondary = error.empty() ? L"Loading media information..." : aip::Utf8ToWide(error);
        }
        return result;
    }
    void Cancel() { source.CancelPending(); }
    void Stop() { source.Stop(); }
};
static ContentSmtcProvider g_contentSmtc;

struct ContentRssProvider
{
    aip::content::AsyncSource<RssFeedSnapshot> source;
    bool waitExpired = false;
    static std::wstring Key(const RssFeedSettings& settings)
    {
        return settings.feedUrl + L"\n" + settings.userAgent + L"\n" +
            std::to_wstring(settings.maxItems) + L":" + std::to_wstring(settings.httpTimeoutSeconds) +
            L":" + std::to_wstring(settings.maxFeedBytes) + L":" + settings.validationError;
    }
    void Refresh(const RssFeedSettings& settings, bool synchronous)
    {
        waitExpired = false;
        const auto key = Key(settings);
        source.Request(key, milliseconds(static_cast<long long>(settings.updateIntervalSeconds) * 1000),
            [settings](const aip::content::AsyncSource<RssFeedSnapshot>::Cancel& canceled) {
                RssFeedSnapshot result;
                if (!settings.valid) { result.status = settings.validationError; return result; }
                try { return ReadRssFeedSnapshot(settings, canceled); }
                catch (const winrt::hresult_error& ex) { result.status = ex.message().c_str(); return result; }
            }, WakePollThread, synchronous);
        if (synchronous && !source.Wait(key, milliseconds(static_cast<long long>(settings.httpTimeoutSeconds) * 1000 + 1000))) {
            waitExpired = true; source.CancelPending();
        }
    }
    RssFeedSnapshot Read(const RssFeedSettings& settings)
    {
        RssFeedSnapshot result;
        if (waitExpired) { result.status = L"RSS refresh timed out before the one-shot deadline."; return result; }
        std::string error;
        if (!source.Read(Key(settings), result, error))
            result.status = error.empty() ? L"Loading RSS feed..." : aip::Utf8ToWide(error);
        return result;
    }
    void Cancel() { source.CancelPending(); }
    void Stop() { source.Stop(); }
};
static ContentRssProvider g_contentRss;

static bool ReadContentConfiguration(aip::content::Configuration& cfg, std::wstring& error,
        aip::content::SecondaryTileConfiguration* tiles)
{
    // Refresh the normal cache once, then copy one complete published document.
    // Subsequent writers may update the cache, but cannot mix versions within
    // this configuration's scalar values, numbered events or RSS fallback.
    std::vector<aip::IniSectionData> document;
    {
        // Only the existing named path lock is needed here. Windows mutex
        // ownership is recursive on this thread, so a mutation callback may
        // inspect its last published document without reentering the host's
        // nonrecursive process write mutex. Refresh I/O cannot overlap a
        // cooperating replacement; release before parsing/provider work.
        aip::IniWriteMutexGuard snapshotScope(g_iniPath, CurrentIniWriteLockWaitMs());
        if (!snapshotScope.IsLocked()) { error = L"The content configuration snapshot is temporarily unavailable."; return false; }
        std::vector<IniEntry> refreshed;
        ReadIniSectionCached(L"Content", refreshed);
        std::lock_guard<std::mutex> lock(g_iniCacheMutex);
        if (!g_iniCacheLoaded) { error = L"The content configuration could not be read."; return false; }
        document = g_iniCacheDoc;
    }
    const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        std::wstring value;
        return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
    };
    if (!aip::content::ReadConfiguration(read, cfg, error, ContentAsusEventsFromDocument(document))) return false;

    // Existing RSS profiles migrate in memory. Keep their INI intact, retain
    // headline cycling, and provide wallpaper behind the feed by default.
    if (!cfg.enabled && ParseContentSourceValue(read(L"Settings", L"ContentSource", L"Wallpaper")) == ContentSource::RssFeed)
    {
        cfg.enabled = true;
        cfg.cycle = true;
        cfg.entries.clear();
        int count = 5;
        aip::ParseIntValue(read(L"RssFeed", L"MaxItems", L"5"), count);
        count = (std::max)(1, (std::min)(20, count));
        for (int i = 0; i < count; ++i) {
            aip::content::Entry entry;
            entry.name = L"RSS " + std::to_wstring(i + 1);
            entry.sources = {L"RssFeed"};
            cfg.entries.push_back(std::move(entry));
        }
    }
    return !tiles || aip::content::ReadSecondaryTileConfiguration(read, cfg.entries.size(), *tiles, error);
}
static bool ReadContentConfiguration(aip::content::Configuration& cfg, std::wstring& error)
{
    return ReadContentConfiguration(cfg, error, nullptr);
}

static std::wstring ContentBackgroundPath(const aip::content::Entry& entry, const aip::content::SmtcMediaSnapshot* media = nullptr)
{
    const auto background = aip::content::BackgroundIdFor(entry.background);
    switch (background) {
    case aip::content::BackgroundId::Image: {
        auto path = TrimCopy(entry.imagePath);
        if (PathIsRelativeW(path.c_str())) {
            std::lock_guard<std::mutex> lock(g_pathMutex);
            path = ParentDirectoryOfPath(g_iniPath) + L"\\" + path;
        }
        return path;
    }
    case aip::content::BackgroundId::Wallpaper:
    case aip::content::BackgroundId::LiveWallpaper: {
        auto path = GetWallpaper(background == aip::content::BackgroundId::LiveWallpaper);
        if (!path.empty()) return path;
        return {};
    }
    case aip::content::BackgroundId::None:
    case aip::content::BackgroundId::Transparent:
        break;
    case aip::content::BackgroundId::SmtcArtwork:
        if (media && media->artwork) return media->artwork->path;
        break; // Absent/bad artwork uses the existing solid/text fallback.
    case aip::content::BackgroundId::Unknown:
    default:
        return {};
    }

    // Blank backgrounds have no source file. Supply a tiny local image so
    // text-only content still uses the established image pipeline.
    std::wstring profile;
    { std::lock_guard<std::mutex> lock(g_pathMutex); profile = g_iniPath; }
    std::uint64_t hash = 14695981039346656037ull;
    HashWide(hash, profile);
    if (aip::GetTemporaryDirectoryPath().empty()) return {};
    const auto directory = GetLiveWallpaperSnapshotDirectory();
    if (directory.empty()) return {};
    if (background == aip::content::BackgroundId::Transparent) {
        // One RGBA pixel with zero alpha. A stored DEFLATE block keeps this
        // deterministic and avoids loading an image encoder on each refresh.
        const std::vector<BYTE> bytes = {
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
            0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89,
            0x00, 0x00, 0x00, 0x10, 0x49, 0x44, 0x41, 0x54,
            0x78, 0x01, 0x01, 0x05, 0x00, 0xfa, 0xff,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01,
            0x64, 0x78, 0x95, 0x38,
            0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
            0xae, 0x42, 0x60, 0x82,
        };
        const auto path = directory + L"\\ContentTransparent-" + Hex64(hash) + L".png";
        std::vector<BYTE> previous;
        if ((!ReadWholeFileBytes(path, previous) || previous != bytes) && !WriteWholeFileBytes(path, bytes))
            return {};
        return path;
    }
    const auto color = SimulatedTileBackgroundColor();
    const auto path = directory + L"\\ContentSolid-" + Hex64(hash) + L".bmp";
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42; file.bfSize = 58; file.bfOffBits = 54;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info); info.biWidth = 1; info.biHeight = 1;
    info.biPlanes = 1; info.biBitCount = 24; info.biSizeImage = 4;
    std::vector<BYTE> bytes(58, 0), previous;
    memcpy(bytes.data(), &file, sizeof(file));
    memcpy(bytes.data() + 14, &info, sizeof(info));
    bytes[54] = color.GetB(); bytes[55] = color.GetG(); bytes[56] = color.GetR();
    if ((!ReadWholeFileBytes(path, previous) || previous != bytes) && !WriteWholeFileBytes(path, bytes))
        return {};
    return path;
}

static std::wstring ContentSnapshotKey(const aip::content::Snapshot& snapshot)
{
    ContentRenderScope scope(&snapshot);
    std::wstring key = FileFingerprint(snapshot.backgroundPath) + L":" + std::to_wstring(static_cast<int>(snapshot.textMode));
    if (snapshot.textMode != aip::content::TextMode::Off)
        for (const auto& value : {snapshot.text.primary, snapshot.text.secondary, snapshot.text.badge})
            key += L":" + std::to_wstring(value.size()) + L":" + value;
    key += L":" + std::to_wstring(snapshot.activationArguments.size()) + L":" + snapshot.activationArguments;
    key += L":branding:" + snapshot.secondaryBranding;
    if (snapshot.textMode != aip::content::TextMode::Off) {
    for (const auto& headline : snapshot.rssHeadlines)
        key += L":" + std::to_wstring(headline.primary.size()) + L":" + headline.primary + L":" + headline.secondary;
    for (const auto& row : snapshot.detailRows)
        for (const auto& field : {row.primary, row.secondary}) key += L":detail:" + std::to_wstring(field.size()) + L":" + field;
    key += L":detailTopic:" + snapshot.detailTopic + L":wrap:" + std::to_wstring(snapshot.wrapSourceText);
    if (snapshot.smtcSource) {
        key += L":smtc:" + std::to_wstring(static_cast<int>(snapshot.smtcState)) + L":" +
            std::to_wstring(static_cast<int>(snapshot.smtcIndicator)) + L":" + std::to_wstring(snapshot.smtcShowRepeatShuffle) + L":" +
            std::to_wstring(static_cast<int>(snapshot.smtcRepeat)) + L":" + std::to_wstring(static_cast<int>(snapshot.smtcShuffle)) + L":" +
            snapshot.smtcAppName + L":" + FileFingerprint(snapshot.smtcAppIconPath);
    }
    }
    // Include render settings to update static content after an INI/tray edit.
    key += L":" + BuildGenerationStateKey(snapshot.backgroundPath);
    return key;
}

static bool ContentEngineTick(bool synchronous, bool queueGeneration)
{
    static aip::content::Cycle cycle;
    static std::wstring lastFrame, lastError, lastWarning, lastSecondaryBackgroundError;
    struct BackgroundCache { std::wstring path; ULONGLONG nextRead = 0; };
    static std::map<std::wstring, BackgroundCache> backgroundCache;
    static int backgroundRefreshSeconds = 0;
    static bool frameReadySeen = false;
    static bool wasNativeQueue = false;
    if (g_shutdownState.IsRequested()) return false;
    aip::content::Configuration cfg;
    aip::content::SecondaryTileConfiguration tiles;
    std::wstring error;
    if (!ReadContentConfiguration(cfg, error, &tiles)) {
        // Invalid edits must not leave a previously enabled actuator running.
        g_contentCaps.Cancel(); g_contentAsus.Cancel(); g_contentDiscord.Cancel();
        g_contentNotes.Cancel(); g_contentRss.Cancel(); g_contentSmtc.Cancel();
        PublishSecondarySnapshots({}); PublishContentSnapshot({}); lastFrame.clear(); frameReadySeen = false; backgroundCache.clear();
        if (error != lastError) {
            Log(L"Content configuration: %s", error.c_str());
            NotifyTrayBalloon(g_ui.notificationTitle, error, NIIF_WARNING);
            lastError = error;
        }
        return false;
    }
    lastError.clear();
    // Hardware control is independent of whether indicator text is selected.
    if (cfg.caps.actionsEnabled) g_contentCaps.Refresh(cfg.caps); else g_contentCaps.Cancel();
    if (!cfg.enabled) {
        g_contentAsus.Cancel(); g_contentDiscord.Cancel(); g_contentNotes.Cancel(); g_contentRss.Cancel(); g_contentSmtc.Cancel();
        PublishSecondarySnapshots({}); PublishContentSnapshot({}); lastFrame.clear(); cycle = {}; frameReadySeen = false; backgroundCache.clear();
        if ((wasNativeQueue || ContentQueueHasAcknowledgedFrames()) && queueGeneration && !synchronous &&
            !GenerationWorkPending() && GetTickCount64() >= g_contentQueueRetryAfter.load())
            wasNativeQueue = DESKTOPSTUB_CONTENT_QUEUE_GENERATE(CurrentGenerationBackground(), L"Content queue disabled") == 0;
        return true;
    }
    const bool separateTiles = tiles.enabled;
    auto warning = separateTiles ? (ContentDirectLiveDeliveryAvailable() ? std::wstring{} :
        L"Separate source tiles are waiting for direct Live Tile delivery with package identity and completed registration. Content.1 remains the primary tile.") :
        ContentTimingWarning(cfg);
    if (warning != lastWarning) {
        lastWarning = warning;
        if (!warning.empty()) {
            LogText(warning);
            NotifyTrayBalloon(g_ui.notificationTitle, warning, NIIF_WARNING);
        }
    }
    const auto separateIndices = separateTiles ? aip::content::SecondaryTileContentIndices(tiles, cfg) : std::set<std::size_t>{};
    if (!separateTiles) PublishSecondarySnapshots({});
    const bool nativeQueue = !separateTiles && ContentNativeQueueAvailable(cfg);
    // An identical first frame must still replace and disable the OS queue
    // when returning to app-managed rotation.
    if (nativeQueue != wasNativeQueue) frameReadySeen = false;
    if (nativeQueue != wasNativeQueue || (!nativeQueue && ContentQueueHasAcknowledgedFrames())) lastFrame.clear();
    wasNativeQueue = nativeQueue;
    auto selection = cfg;
    if (nativeQueue || separateTiles) selection.cycle = false;
    auto index = separateTiles ? std::size_t{0} : cycle.Select(selection, GetTickCount64());
    if (index == static_cast<std::size_t>(-1)) {
        g_contentAsus.Cancel(); g_contentDiscord.Cancel(); g_contentNotes.Cancel(); g_contentRss.Cancel(); g_contentSmtc.Cancel();
        frameReadySeen = false; backgroundCache.clear();
        // Turning off every item replaces the prior queue with an empty tile.
        // Keep a renderable blank frame until the ordinary update succeeds;
        // otherwise Windows would keep showing the last enabled items.
        aip::content::Entry blank; blank.background = L"None";
        aip::content::Snapshot emptyFrame;
        emptyFrame.active = true; emptyFrame.textMode = aip::content::TextMode::Off;
        emptyFrame.backgroundId = aip::content::BackgroundId::None;
        emptyFrame.backgroundPath = ContentBackgroundPath(blank);
        PublishContentSnapshot(emptyFrame);
        const auto blankKey = ContentSnapshotKey(emptyFrame);
        if ((blankKey != lastFrame || ContentQueueHasAcknowledgedFrames()) && queueGeneration && !synchronous &&
            !GenerationWorkPending() && GetTickCount64() >= g_contentQueueRetryAfter.load() &&
            IsUsableWallpaperPath(emptyFrame.backgroundPath) &&
            DESKTOPSTUB_CONTENT_QUEUE_GENERATE(emptyFrame.backgroundPath, L"All contents disabled") != 0)
            lastFrame = blankKey;
        return true;
    }
    const auto& selectedEntry = cfg.entries[index];
    const auto usesSmtc = [](const aip::content::Entry& candidate) {
        return aip::content::EntryUsesSource(candidate, aip::content::SourceId::Smtc) ||
            aip::content::BackgroundIdFor(candidate.background) == aip::content::BackgroundId::SmtcArtwork;
    };
    const auto sourceNeeded = [&](aip::content::SourceId id) {
        if (separateTiles) return std::any_of(separateIndices.begin(), separateIndices.end(), [&](std::size_t selected) {
            const auto& candidate = cfg.entries[selected];
            return id == aip::content::SourceId::Smtc ? usesSmtc(candidate) : aip::content::EntryUsesSource(candidate, id);
        });
        if (!nativeQueue && !aip::content::SourceRefreshesWhileInactive(id))
            return aip::content::EntryUsesSource(selectedEntry, id);
        return std::any_of(cfg.entries.begin(), cfg.entries.end(), [id, &usesSmtc](const aip::content::Entry& candidate) {
            return candidate.enabled && (id == aip::content::SourceId::Smtc ? usesSmtc(candidate) : aip::content::EntryUsesSource(candidate, id));
        });
    };
    const bool needsRss = sourceNeeded(aip::content::SourceId::RssFeed);
    const bool needsSmtc = sourceNeeded(aip::content::SourceId::Smtc);
    const bool needsDiscord = sourceNeeded(aip::content::SourceId::DiscordRpc);
    const bool needsCaps = sourceNeeded(aip::content::SourceId::CapsBlink);
    const bool needsAsus = sourceNeeded(aip::content::SourceId::AsusBlink);
    std::array<bool, 3> needsNotes{};
    if (separateTiles) {
        for (const auto selected : separateIndices) {
            const auto& candidate = cfg.entries[selected];
            if (aip::content::EntryUsesSource(candidate, aip::content::SourceId::Notes)) needsNotes[candidate.notesResource] = true;
        }
    } else if (nativeQueue || aip::content::SourceRefreshesWhileInactive(aip::content::SourceId::Notes)) {
        for (const auto& candidate : cfg.entries)
            if (candidate.enabled && aip::content::EntryUsesSource(candidate, aip::content::SourceId::Notes))
                needsNotes[candidate.notesResource] = true;
    } else if (aip::content::EntryUsesSource(selectedEntry, aip::content::SourceId::Notes)) {
        needsNotes[selectedEntry.notesResource] = true;
    }
    RssFeedSettings rssSettings;
    if (needsRss) { rssSettings = ReadRssFeedSettings(); g_contentRss.Refresh(rssSettings, synchronous); }
    else g_contentRss.Cancel();
    const bool needsArtwork = separateTiles ? std::any_of(separateIndices.begin(), separateIndices.end(), [&](std::size_t selected) {
        return aip::content::BackgroundIdFor(cfg.entries[selected].background) == aip::content::BackgroundId::SmtcArtwork;
    }) : std::any_of(cfg.entries.begin(), cfg.entries.end(), [](const aip::content::Entry& candidate) {
        return candidate.enabled && aip::content::BackgroundIdFor(candidate.background) == aip::content::BackgroundId::SmtcArtwork;
    });
    if (needsSmtc) g_contentSmtc.Refresh(cfg.refreshSeconds, synchronous, needsArtwork, ContentDirectLiveDeliveryAvailable(),
        cfg.smtc.indicatorMode == aip::content::MediaIndicatorMode::AppIcon);
    else g_contentSmtc.Cancel();
    // Read once so a worker publication cannot pair one track's text with
    // another track's background while this frame is composed.
    const auto media = needsSmtc ? g_contentSmtc.Read() : aip::content::SmtcMediaSnapshot{};
    for (int resource = 0; resource < 3; ++resource) {
        if (needsNotes[resource]) g_contentNotes.Refresh(cfg.notes, resource, synchronous);
        else g_contentNotes.Cancel(resource);
    }
    if (needsDiscord) g_contentDiscord.Refresh(cfg.discord, synchronous);
    else g_contentDiscord.Cancel();
    if (needsCaps && !cfg.caps.actionsEnabled) g_contentCaps.Refresh(cfg.caps);
    if (needsAsus) g_contentAsus.Refresh(cfg.asus); else g_contentAsus.Cancel();
    const auto feed = needsRss ? g_contentRss.Read(rssSettings) : RssFeedSnapshot{};
    const auto capsText = needsCaps ? g_contentCaps.Read() : aip::content::Text{};
    const auto asusText = needsAsus ? g_contentAsus.Read() : aip::content::Text{};
    const auto discordText = needsDiscord ? g_contentDiscord.Read() : aip::content::Text{};
    std::array<aip::notes::Snapshot, 3> notesSnapshots;
    for (int resource = 0; resource < 3; ++resource)
        if (needsNotes[resource]) notesSnapshots[resource] = g_contentNotes.ReadSnapshot(cfg.notes, resource);

    std::set<std::wstring> usedBackgroundKeys;
    struct PruneBackgroundCache {
        std::map<std::wstring, BackgroundCache>& cache;
        const std::set<std::wstring>& used;
        ~PruneBackgroundCache() {
            for (auto item = cache.begin(); item != cache.end();)
                if (!used.count(item->first)) item = cache.erase(item); else ++item;
        }
    } pruneBackgroundCache{backgroundCache, usedBackgroundKeys};
    const auto compose = [&](std::size_t contentIndex) {
    const auto& entry = cfg.entries[contentIndex];
    aip::content::Snapshot snapshot;
    snapshot.active = true; snapshot.name = entry.name; snapshot.textMode = cfg.textMode;
    if (separateTiles && !entry.enabled) {
        aip::content::Entry blank; blank.background = L"None";
        snapshot.textMode = aip::content::TextMode::Off; snapshot.backgroundId = aip::content::BackgroundId::None;
        snapshot.backgroundPath = ContentBackgroundPath(blank);
        return snapshot;
    }
    snapshot.backgroundId = aip::content::BackgroundIdFor(entry.background);
    auto backgroundKey = entry.background + L"\n" + entry.imagePath;
    const bool artworkBackground = aip::content::BackgroundIdFor(entry.background) == aip::content::BackgroundId::SmtcArtwork;
    if (artworkBackground) {
        backgroundKey += L"\n" + (media.artwork ? media.artwork->path : L"");
        snapshot.backgroundLease = media.artwork;
    }
    if (backgroundRefreshSeconds != cfg.refreshSeconds) {
        backgroundRefreshSeconds = cfg.refreshSeconds;
        backgroundCache.clear();
    }
    usedBackgroundKeys.insert(backgroundKey);
    auto& cachedBackground = backgroundCache[backgroundKey];
    if (GetTickCount64() >= cachedBackground.nextRead || synchronous) {
        cachedBackground.path = ContentBackgroundPath(entry, &media);
        cachedBackground.nextRead = GetTickCount64() + static_cast<ULONGLONG>(cfg.refreshSeconds) * 1000;
    }
    snapshot.backgroundPath = cachedBackground.path;
    if (artworkBackground && !media.artwork && !aip::content::EntryUsesSource(entry, aip::content::SourceId::Smtc))
        snapshot.text = media.text; // Artwork-only entries also retain a useful fallback.
    for (const auto& source : entry.sources) {
        aip::content::Text text;
        switch (aip::content::SourceIdFor(source)) {
        case aip::content::SourceId::CustomText:
            text = entry.custom;
            break;
        case aip::content::SourceId::CapsBlink:
            text = capsText;
            break;
        case aip::content::SourceId::AsusBlink:
            text = asusText;
            break;
        case aip::content::SourceId::DiscordRpc:
            text = discordText;
            snapshot.wrapSourceText = entry.sources.size() == 1;
            break;
        case aip::content::SourceId::Notes:
            text = ContentNotesProvider::FormatSnapshot(notesSnapshots[entry.notesResource], entry.notesDetail);
            if (entry.sources.size() == 1) {
                snapshot.detailRows = aip::notes::BuildNoteTileRows(notesSnapshots[entry.notesResource], entry.notesDetail);
                snapshot.detailTopic = aip::notes::ResourceTileTopic(notesSnapshots[entry.notesResource].resource);
            }
            break;
        case aip::content::SourceId::Smtc:
            text = media.text;
            break;
        case aip::content::SourceId::CapsLock: {
            bool enabled = false;
            std::wstring stateError;
            if (ReadContentLogicalCaps(enabled, stateError)) text.primary = enabled ? L"Caps Lock is on" : L"Caps Lock is off";
            else { text.primary = L"Caps Lock"; text.secondary = stateError; }
            break;
        }
        case aip::content::SourceId::RssFeed: {
            if (feed.ok && !feed.items.empty()) {
                bool numberedProfile = false;
                aip::content::ParseBool(IniReadS(L"Content", L"Enabled", L"0"), numberedProfile);
                const auto itemIndex = numberedProfile ? static_cast<std::size_t>(entry.rssItem - 1) : contentIndex % feed.items.size();
                if (itemIndex >= feed.items.size()) {
                    text.primary = L"RSS headline unavailable";
                    text.secondary = L"The feed has " + std::to_wstring(feed.items.size()) + L" items; content selects item " + std::to_wstring(entry.rssItem) + L".";
                    aip::content::Overlay(snapshot.text, text);
                    continue;
                }
                const auto& item = feed.items[itemIndex];
                std::wstring normalizedUrl, urlError;
                if (ValidateRssFeedUrlSetting(item.link, false, normalizedUrl, urlError))
                    snapshot.activationArguments = L"--open-url " + aip::QuoteCommandLineArg(normalizedUrl);
                text.primary = item.title;
                text.secondary = feed.sourceTitle;
                aip::content::AppendBounded(text.secondary, item.summary);
            } else { text.primary = L"RSS"; text.secondary = feed.status; }
            break;
        }
        case aip::content::SourceId::Unknown:
        default:
            text.primary = L"Content source unavailable";
            text.secondary = source;
            break;
        }
        aip::content::Overlay(snapshot.text, text);
    }
    if ((entry.sources.size() == 1 && aip::content::EntryUsesSource(entry, aip::content::SourceId::Smtc)) ||
        (entry.sources.empty() && artworkBackground && !media.artwork)) {
        snapshot.smtcSource = true; snapshot.smtcState = media.playbackState;
        snapshot.smtcIndicator = cfg.smtc.indicatorMode; snapshot.smtcShowRepeatShuffle = cfg.smtc.showRepeatShuffle;
        snapshot.smtcRepeat = media.repeat; snapshot.smtcShuffle = media.shuffle; snapshot.smtcAppName = media.source;
        if (media.appIcon) { snapshot.smtcAppIconPath = media.appIcon->path; snapshot.smtcAppIconLease = media.appIcon; }
        if (media.hasSession) snapshot.text = {media.title, media.artist, aip::content::MediaStatusCaption(snapshot)};
        else snapshot.text.badge = aip::content::MediaStatusCaption(snapshot);
    }
    if (artworkBackground && !media.artworkError.empty()) aip::content::AppendBounded(snapshot.text.secondary, media.artworkError);
    snapshot.nativeQueue = nativeQueue;
    if (nativeQueue) {
        snapshot.nativeQueueTag = aip::content::NativeQueueTag(contentIndex);
        snapshot.nativeQueueLayout = ContentNativeQueueLayout(cfg);
        snapshot.nativeQueueFrameKey = ContentSnapshotKey(snapshot);
    }
    return snapshot;
    };
    auto snapshot = compose(index);
    if (separateTiles) {
        std::map<std::size_t, aip::content::Snapshot> composed;
        composed.emplace(index, snapshot);
        std::vector<aip::content::Snapshot> frames;
        std::wstring unavailable;
        const bool primaryReady = IsUsableWallpaperPath(snapshot.backgroundPath);
        const auto appendReady = [&](aip::content::Snapshot frame) {
            if (IsUsableWallpaperPath(frame.backgroundPath)) frames.push_back(std::move(frame));
            else {
                if (!unavailable.empty()) unavailable += L"; ";
                unavailable += (frame.secondaryTileId.empty() ? std::wstring(L"primary") : frame.secondaryTileId) +
                    L": " + frame.backgroundPath;
            }
        };
        snapshot.secondaryFrameKey = ContentSnapshotKey(snapshot);
        appendReady(snapshot); // Empty ID is the primary application tile.
        for (const auto& tile : tiles.tiles) {
            const auto contentIndex = static_cast<std::size_t>(tile.content - 1);
            if (!tile.enabled || !cfg.entries[contentIndex].enabled) continue;
            auto found = composed.find(contentIndex);
            if (found == composed.end()) found = composed.emplace(contentIndex, compose(contentIndex)).first;
            auto frame = found->second;
            frame.name = tile.name; frame.secondaryTileId = L"AipSource_" + tile.id;
            frame.secondaryBranding = tile.appearance.branding;
            if (tile.id == L"rss" && feed.ok) {
                for (std::size_t headline = 0; headline < (std::min)(feed.items.size(), std::size_t(5)); ++headline)
                    frame.rssHeadlines.push_back({feed.items[headline].title, feed.items[headline].summary, L""});
            }
            if (frame.activationArguments.empty()) frame.activationArguments = L"--tile-setup " + tile.id;
            if (std::wstring(aip::content::FindSecondaryTileDescriptor(tile.id)->source) == L"Notes" &&
                notesSnapshots[cfg.entries[contentIndex].notesResource].state == aip::notes::StateKind::Error)
                frame.activationArguments = L"--tile-setup " + tile.id + L" --tile-sign-in";
            frame.secondaryFrameKey = ContentSnapshotKey(frame);
            appendReady(std::move(frame));
        }
        if (unavailable != lastSecondaryBackgroundError) {
            lastSecondaryBackgroundError = unavailable;
            if (!unavailable.empty()) LogText(L"Separate tile background unavailable: " + unavailable);
        }
        PublishSecondarySnapshots(frames);
        if (primaryReady && !frameReadySeen) {
            frameReadySeen = true;
            if (g_hwnd && !g_contentFrameReadyPosted.exchange(true) &&
                !PostMessageW(g_hwnd, WM_APP_CONTENT_FRAME_READY, 0, 0)) g_contentFrameReadyPosted = false;
        }
        if (synchronous || !queueGeneration) return primaryReady || !frames.empty();
        if (!ContentDirectLiveDeliveryAvailable()) {
            // Registration/helper fallback keeps only Content.1 on the primary
            // tile. Source tiles wait for direct delivery rather than cycling
            // their information onto the wallpaper tile.
            const auto frame = ContentSnapshotKey(snapshot);
            if (primaryReady && frame != lastFrame && !GenerationWorkPending() &&
                GetTickCount64() >= g_contentQueueRetryAfter.load() &&
                DESKTOPSTUB_CONTENT_QUEUE_GENERATE(snapshot.backgroundPath, L"Primary tile awaiting direct source-tile delivery") != 0)
                lastFrame = frame;
            return primaryReady;
        }
        if (!GenerationWorkPending() && GetTickCount64() >= g_contentQueueRetryAfter.load()) {
            auto changed = PrepareSecondaryBatch(frames);
            if (!changed.empty()) DESKTOPSTUB_SECONDARY_BATCH_GENERATE(std::move(changed), L"Changed separate tile contents");
        }
        return primaryReady || !frames.empty();
    }
    if (nativeQueue) {
        std::vector<aip::content::Snapshot> frames;
        for (std::size_t contentIndex = 0; contentIndex < cfg.entries.size(); ++contentIndex)
            if (cfg.entries[contentIndex].enabled) {
                auto frame = contentIndex == index ? snapshot : compose(contentIndex);
                if (!IsUsableWallpaperPath(frame.backgroundPath)) {
                    PublishContentQueueSnapshots({}); frameReadySeen = false;
                    const auto unavailable = L"Content background is unavailable: " + frame.backgroundPath;
                    if (lastFrame != unavailable) { LogText(unavailable); lastFrame = unavailable; }
                    return false;
                }
                frames.push_back(std::move(frame));
            }
        PublishContentQueueSnapshots(frames);
        if (synchronous || !queueGeneration) return true;
        auto changed = PrepareContentQueueBatch(frames);
        if (!changed.empty() && !GenerationWorkPending() && GetTickCount64() >= g_contentQueueRetryAfter.load())
            DESKTOPSTUB_CONTENT_QUEUE_BATCH_GENERATE(std::move(changed), L"Changed Windows queue contents");
        return true;
    }
    PublishContentSnapshot(snapshot);
    if (!IsUsableWallpaperPath(snapshot.backgroundPath)) {
        auto message = L"Content background is unavailable: " + snapshot.backgroundPath;
        if (lastFrame != message) { LogText(message); NotifyTrayBalloon(g_ui.notificationTitle, message, NIIF_WARNING); lastFrame = message; }
        return false;
    }
    if (!frameReadySeen) {
        frameReadySeen = true;
        if (g_hwnd && !g_contentFrameReadyPosted.exchange(true) &&
            !PostMessageW(g_hwnd, WM_APP_CONTENT_FRAME_READY, 0, 0)) g_contentFrameReadyPosted = false;
    }
    auto frame = ContentSnapshotKey(snapshot);
    if (synchronous || !queueGeneration) return true; // Caller queues its own generation.
    if (frame != lastFrame && !GenerationWorkPending() && GetTickCount64() >= g_contentQueueRetryAfter.load()) {
        if (DESKTOPSTUB_CONTENT_QUEUE_GENERATE(snapshot.backgroundPath, L"Content source update") != 0) lastFrame = frame;
    }
    return true;
}

static void StopContentProviders() {
    // Request hardware cancellation before waiting on other sources.
    g_contentCaps.Cancel(); g_contentAsus.Cancel();
    g_contentDiscord.Stop(); g_contentNotes.Stop(); g_contentRss.Stop(); g_contentSmtc.Stop();
    g_contentAsus.Stop(); g_contentCaps.Stop();
}
