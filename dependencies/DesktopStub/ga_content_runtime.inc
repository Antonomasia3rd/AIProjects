// Built-in data providers share one asynchronous lifecycle and result cache.
#include "../content_sources/smtc.inc"
#include "../content_source_host.h"
#include "../content_sources/notes.inc"
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
    static std::wstring Key(const aip::content::NotesConfiguration& cfg, int resource)
    {
        const auto& account = cfg.accounts[resource];
        return std::to_wstring(resource) + L":" + account.uid + L"\n" + account.token + L"\n" + account.userId +
            L"\n" + std::to_wstring(cfg.timeoutSeconds) + L":" + std::to_wstring(cfg.maximumBytes);
    }
    void Refresh(const aip::content::NotesConfiguration& cfg, int resource, bool synchronous)
    {
        waitExpired[resource] = false;
        aip::notes::Configuration settings;
        settings.resource = static_cast<aip::notes::ResourceKind>(resource);
        settings.uid = aip::WideToUtf8(cfg.accounts[resource].uid);
        settings.ltoken = aip::WideToUtf8(cfg.accounts[resource].token);
        settings.ltuid = aip::WideToUtf8(cfg.accounts[resource].userId);
        settings.refreshSeconds = static_cast<UINT>(cfg.refreshSeconds);
        settings.timeout = milliseconds(cfg.timeoutSeconds * 1000);
        settings.maximumBytes = static_cast<size_t>(cfg.maximumBytes);
        const auto key = Key(cfg, resource);
        sources[resource].Request(key, milliseconds(static_cast<long long>(cfg.refreshSeconds) * 1000),
            [settings](const aip::content::AsyncSource<aip::notes::Snapshot>::Cancel& canceled) mutable {
                settings.canceled = [canceled] { return !g_running.load() || canceled(); };
                return aip::notes::Fetch(settings);
            }, WakePollThread, synchronous);
        if (synchronous && !sources[resource].Wait(key, settings.timeout + milliseconds(1000))) {
            waitExpired[resource] = true; sources[resource].CancelPending();
        }
    }
    aip::content::Text Read(const aip::content::NotesConfiguration& cfg, int resource)
    {
        if (waitExpired[resource]) return {L"Notes refresh timed out", L"No fresh result before the one-shot deadline.", L""};
        aip::notes::Snapshot snapshot;
        std::string error;
        if (!sources[resource].Read(Key(cfg, resource), snapshot, error))
            return {L"Real-Time Notes", error.empty() ? L"Loading resource information..." : L"The notes source could not complete.", L""};
        return {snapshot.line1, snapshot.line2, L""};
    }
    void Cancel(int resource) { sources[resource].CancelPending(); }
    void Cancel() { for (auto& source : sources) source.CancelPending(); }
    void Stop() { Cancel(); for (auto& source : sources) source.Stop(); }
};
static ContentNotesProvider g_contentNotes;

struct ContentSmtcProvider
{
    aip::content::AsyncSource<aip::content::Text> source;
    bool waitExpired = false;
    void Refresh(int seconds, bool synchronous)
    {
        waitExpired = false;
        source.Request(L"SMTC", milliseconds(static_cast<long long>(seconds) * 1000),
            [](const aip::content::AsyncSource<aip::content::Text>::Cancel& canceled) {
                aip::content::Text result;
                if (canceled()) return result;
                std::wstring error;
                if (!aip::content::ReadSmtcContent(result, error)) {
                    result.primary = L"Media source unavailable"; result.secondary = error;
                }
                return result;
            }, WakePollThread, synchronous);
        if (synchronous && !source.Wait(L"SMTC", milliseconds(3000))) { waitExpired = true; source.CancelPending(); }
    }
    aip::content::Text Read()
    {
        if (waitExpired) return {L"Media refresh timed out", L"No fresh result before the one-shot deadline.", L""};
        aip::content::Text result;
        std::string error;
        if (!source.Read(L"SMTC", result, error)) {
            result.primary = L"Now playing";
            result.secondary = error.empty() ? L"Loading media information..." : aip::Utf8ToWide(error);
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

static bool ReadContentConfiguration(aip::content::Configuration& cfg, std::wstring& error)
{
    if (!aip::content::ReadConfiguration([](const wchar_t* s, const wchar_t* k, const wchar_t* d) {
        return IniReadS(s, k, d);
    }, cfg, error)) return false;

    // Existing RSS profiles migrate in memory. Keep their INI intact, retain
    // headline cycling, and provide wallpaper behind the feed by default.
    if (!cfg.enabled && ParseContentSourceValue(IniReadS(L"Settings", L"ContentSource", L"Wallpaper")) == ContentSource::RssFeed)
    {
        cfg.enabled = true;
        cfg.cycle = true;
        cfg.entries.clear();
        int count = IniReadClampedI(L"RssFeed", L"MaxItems", 5, 1, 20);
        for (int i = 0; i < count; ++i) {
            aip::content::Entry entry;
            entry.name = L"RSS " + std::to_wstring(i + 1);
            entry.sources = {L"RssFeed"};
            cfg.entries.push_back(std::move(entry));
        }
    }
    return true;
}

static std::wstring ContentBackgroundPath(const aip::content::Entry& entry)
{
    if (entry.background == L"Image") {
        auto path = TrimCopy(entry.imagePath);
        if (PathIsRelativeW(path.c_str())) {
            std::lock_guard<std::mutex> lock(g_pathMutex);
            path = ParentDirectoryOfPath(g_iniPath) + L"\\" + path;
        }
        return path;
    }
    if (entry.background != L"None") {
        auto path = GetWallpaper(entry.background == L"LiveWallpaper");
        if (!path.empty()) return path;
    }

    // A solid-color desktop has no source file. Supply a tiny BMP so text-only
    // content and RSS still use the established image pipeline.
    auto color = SimulatedTileBackgroundColor();
    std::wstring profile;
    { std::lock_guard<std::mutex> lock(g_pathMutex); profile = g_iniPath; }
    std::uint64_t hash = 14695981039346656037ull;
    HashWide(hash, profile);
    auto path = GetExeDir() + L"\\.DesktopStubContent-" + Hex64(hash) + L".bmp";
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
    std::wstring key = FileFingerprint(snapshot.backgroundPath) + L":" + std::to_wstring(static_cast<int>(snapshot.textMode));
    for (const auto& value : {snapshot.text.primary, snapshot.text.secondary, snapshot.text.badge, snapshot.activationArguments})
        key += L":" + std::to_wstring(value.size()) + L":" + value;
    // Include render settings to update static content after an INI/tray edit.
    key += L":" + BuildGenerationStateKey(snapshot.backgroundPath);
    return key;
}

static bool ContentEngineTick(bool synchronous)
{
    static aip::content::Cycle cycle;
    static std::wstring lastFrame, lastError, lastWarning, lastBackgroundEntry;
    static std::wstring cachedBackground;
    static ULONGLONG nextBackgroundRead = 0;
    static int backgroundRefreshSeconds = 0;
    if (g_shutdownState.IsRequested()) return false;
    aip::content::Configuration cfg;
    std::wstring error;
    if (!ReadContentConfiguration(cfg, error)) {
        // Invalid edits must not leave a previously enabled actuator running.
        g_contentCaps.Cancel(); g_contentAsus.Cancel(); g_contentDiscord.Cancel();
        if (error != lastError) {
            Log(L"Content configuration: %s", error.c_str());
            NotifyTrayBalloon(g_ui.notificationTitle, error, NIIF_WARNING);
            lastError = error;
        }
        return false;
    }
    lastError.clear();
    if (!cfg.enabled) { g_contentCaps.Cancel(); g_contentAsus.Cancel(); g_contentDiscord.Cancel(); g_contentNotes.Cancel(); g_contentRss.Cancel(); g_contentSmtc.Cancel(); PublishContentSnapshot({}); lastFrame.clear(); cycle = {}; return true; }
    auto warning = aip::content::TimingWarning(cfg, UseExperimentalLiveTileUpdate());
    if (warning != lastWarning) {
        lastWarning = warning;
        if (!warning.empty()) {
            LogText(warning);
            NotifyTrayBalloon(g_ui.notificationTitle, warning, NIIF_WARNING);
        }
    }
    auto index = cycle.Select(cfg, GetTickCount64());
    if (index == static_cast<std::size_t>(-1)) {
        g_contentCaps.Cancel(); g_contentAsus.Cancel(); g_contentDiscord.Cancel(); g_contentNotes.Cancel(); g_contentRss.Cancel(); g_contentSmtc.Cancel();
        PublishContentSnapshot({}); lastFrame.clear();
        return true;
    }
    const auto& entry = cfg.entries[index];
    const auto sourceNeeded = [&](aip::content::SourceId id) {
        if (!aip::content::SourceRefreshesWhileInactive(id))
            return aip::content::EntryUsesSource(entry, id);
        return std::any_of(cfg.entries.begin(), cfg.entries.end(), [id](const aip::content::Entry& candidate) {
            return candidate.enabled && aip::content::EntryUsesSource(candidate, id);
        });
    };
    const bool needsRss = sourceNeeded(aip::content::SourceId::RssFeed);
    const bool needsSmtc = sourceNeeded(aip::content::SourceId::Smtc);
    const bool needsDiscord = sourceNeeded(aip::content::SourceId::DiscordRpc);
    const bool needsCaps = sourceNeeded(aip::content::SourceId::CapsBlink);
    const bool needsAsus = sourceNeeded(aip::content::SourceId::AsusBlink);
    std::array<bool, 3> needsNotes{};
    if (aip::content::SourceRefreshesWhileInactive(aip::content::SourceId::Notes)) {
        for (const auto& candidate : cfg.entries)
            if (candidate.enabled && aip::content::EntryUsesSource(candidate, aip::content::SourceId::Notes))
                needsNotes[candidate.notesResource] = true;
    } else if (aip::content::EntryUsesSource(entry, aip::content::SourceId::Notes)) {
        needsNotes[entry.notesResource] = true;
    }
    RssFeedSettings rssSettings;
    if (needsRss) { rssSettings = ReadRssFeedSettings(); g_contentRss.Refresh(rssSettings, synchronous); }
    else g_contentRss.Cancel();
    if (needsSmtc) g_contentSmtc.Refresh(cfg.refreshSeconds, synchronous);
    else g_contentSmtc.Cancel();
    for (int resource = 0; resource < 3; ++resource) {
        if (needsNotes[resource]) g_contentNotes.Refresh(cfg.notes, resource, synchronous);
        else g_contentNotes.Cancel(resource);
    }
    if (needsDiscord) g_contentDiscord.Refresh(cfg.discord, synchronous);
    else g_contentDiscord.Cancel();
    if (needsCaps) g_contentCaps.Refresh(cfg.caps); else g_contentCaps.Cancel();
    if (needsAsus) g_contentAsus.Refresh(cfg.asus); else g_contentAsus.Cancel();

    aip::content::Snapshot snapshot;
    snapshot.active = true; snapshot.name = entry.name; snapshot.textMode = cfg.textMode;
    auto backgroundKey = entry.background + L"\n" + entry.imagePath;
    if (backgroundRefreshSeconds != cfg.refreshSeconds) {
        backgroundRefreshSeconds = cfg.refreshSeconds;
        nextBackgroundRead = 0;
    }
    if (backgroundKey != lastBackgroundEntry || GetTickCount64() >= nextBackgroundRead || synchronous) {
        cachedBackground = ContentBackgroundPath(entry);
        lastBackgroundEntry = backgroundKey;
        nextBackgroundRead = GetTickCount64() + static_cast<ULONGLONG>(cfg.refreshSeconds) * 1000;
    }
    snapshot.backgroundPath = cachedBackground;
    for (const auto& source : entry.sources) {
        aip::content::Text text;
        switch (aip::content::SourceIdFor(source)) {
        case aip::content::SourceId::CustomText:
            text = entry.custom;
            break;
        case aip::content::SourceId::CapsBlink:
            text = g_contentCaps.Read();
            break;
        case aip::content::SourceId::AsusBlink:
            text = g_contentAsus.Read();
            break;
        case aip::content::SourceId::DiscordRpc:
            text = g_contentDiscord.Read();
            break;
        case aip::content::SourceId::Notes:
            text = g_contentNotes.Read(cfg.notes, entry.notesResource);
            break;
        case aip::content::SourceId::Smtc:
            text = g_contentSmtc.Read();
            break;
        case aip::content::SourceId::CapsLock: {
            bool enabled = false;
            std::wstring stateError;
            if (ReadContentLogicalCaps(enabled, stateError)) text.primary = enabled ? L"Caps Lock is on" : L"Caps Lock is off";
            else { text.primary = L"Caps Lock"; text.secondary = stateError; }
            break;
        }
        case aip::content::SourceId::RssFeed: {
            const auto feed = g_contentRss.Read(rssSettings);
            if (feed.ok && !feed.items.empty()) {
                bool numberedProfile = false;
                aip::content::ParseBool(IniReadS(L"Content", L"Enabled", L"0"), numberedProfile);
                const auto itemIndex = numberedProfile ? static_cast<std::size_t>(entry.rssItem - 1) : index % feed.items.size();
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
    PublishContentSnapshot(snapshot);
    if (!IsUsableWallpaperPath(snapshot.backgroundPath)) {
        auto message = L"Content background is unavailable: " + snapshot.backgroundPath;
        if (lastFrame != message) { LogText(message); NotifyTrayBalloon(g_ui.notificationTitle, message, NIIF_WARNING); lastFrame = message; }
        return false;
    }
    auto frame = ContentSnapshotKey(snapshot);
    if (synchronous) return true; // --once invokes Generate exactly once itself.
    if (frame != lastFrame && !GenerationWorkPending()) {
        if (QueueGenerate(snapshot.backgroundPath, L"Content source update") != 0) lastFrame = frame;
    }
    return true;
}

static void StopContentProviders() {
    // Request hardware cancellation before waiting on other sources.
    g_contentCaps.Cancel(); g_contentAsus.Cancel();
    g_contentDiscord.Stop(); g_contentNotes.Stop(); g_contentRss.Stop(); g_contentSmtc.Stop();
    g_contentAsus.Stop(); g_contentCaps.Stop();
}
