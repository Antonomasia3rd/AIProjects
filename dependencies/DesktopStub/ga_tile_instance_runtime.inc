// Independent tile demand, provider state and immutable publication. A tile's
// settings never replace another tile's provider key or refresh deadline.
static bool EnsureTileInstanceProfile(std::wstring& error)
{
    return MutateDesktopStubIniWithWait([&](std::wstring& text) {
        const auto document = aip::ParseIniDocument(text);
        const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            std::wstring value; return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
        };
        aip::content::TileInstanceMigrationPlan plan;
        if (!aip::content::PlanLegacyTileInstanceMigration(read, plan, error)) return false;
        if (!plan.changed) return true;
        auto candidate = text;
        for (const auto& setting : plan.settings)
            if (!aip::WriteIniValueToText(candidate, setting.section.c_str(), setting.key.c_str(), setting.value)) {
                error = L"Could not stage the tile configuration."; return false;
            }
        const auto staged = aip::ParseIniDocument(candidate);
        aip::content::TileInstanceConfiguration validated;
        if (!ValidateTileInstanceHostConfiguration([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            std::wstring value; return aip::ReadIniValueFromDoc(staged, section, key, value) ? value : std::wstring(fallback);
        }, validated, error)) return false;
        // This immutable pre-conversion backup contains the exact original
        // account/configuration bytes. Never overwrite a previous backup.
        const auto backup = g_iniPath + L".before-tile-instances.bak";
        const auto attributes = GetFileAttributesW(backup.c_str());
        std::wstring priorBackup;
        if (attributes != INVALID_FILE_ATTRIBUTES && ((attributes & FILE_ATTRIBUTE_DIRECTORY) || !ReadTextFile(backup, priorBackup))) {
            error = L"The pre-conversion backup path is not a readable file."; return false;
        }
        if (attributes == INVALID_FILE_ATTRIBUTES && !CopyFileW(g_iniPath.c_str(), backup.c_str(), TRUE)) {
            error = L"Could not save the pre-conversion profile backup."; return false;
        }
        text = std::move(candidate); error.clear(); return true;
    }, CurrentIniWriteLockWaitMs());
}
struct TileInstanceProviderState {
    aip::content::TileInstanceType type = aip::content::TileInstanceType::Wallpaper;
    bool forceRefresh = false;
    std::unique_ptr<ContentRssProvider> rss;
    std::unique_ptr<ContentPrayerProvider> prayer;
    std::unique_ptr<ContentNotesProvider> notes;
    std::unique_ptr<ContentDiscordProvider> discord;
    std::wstring backgroundKey, backgroundPath;
    ULONGLONG nextBackgroundRead = 0;
    void Stop() {
        if (rss) rss->Cancel(); if (prayer) prayer->Cancel(); if (notes) notes->Cancel();
        if (discord) discord->Cancel();
        if (rss) rss->Stop(); if (prayer) prayer->Stop(); if (notes) notes->Stop(); if (discord) discord->Stop();
    }
};
static std::map<std::wstring, std::unique_ptr<TileInstanceProviderState>> g_tileInstanceProviders;
static std::mutex g_tileInstanceRefreshMutex;
static std::set<std::wstring> g_tileInstanceRefreshRequests;
static void StopTileInstanceProviders()
{
    for (auto& item : g_tileInstanceProviders) item.second->Stop();
    g_tileInstanceProviders.clear();
}
static bool ReadTileInstancePlugins(aip::content::Configuration& result)
{
    static std::wstring profile;
    static unsigned long long revision = ~0ull;
    static aip::content::Configuration cached;
    static bool valid = false;
    std::vector<aip::IniSectionData> document;
    {
        std::lock_guard<std::mutex> lock(g_iniCacheMutex);
        if (profile != g_iniCacheProfile || revision != g_iniCacheRevision) {
            document = g_iniCacheDoc; profile = g_iniCacheProfile; revision = g_iniCacheRevision;
        } else { result = cached; return valid; }
    }
    const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        const auto s = aip::content::Lower(section);
        std::wstring value;
        return (s == L"capsblink" || s == L"asusblink") && aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
    };
    std::wstring error;
    valid = aip::content::ReadConfiguration(read, cached, error, ContentAsusEventsFromDocument(document));
    result = cached; return valid;
}
static void RefreshTileInstanceNow(const std::wstring& id)
{
    // UI only signals; the poll thread alone owns the provider map.
    { std::lock_guard<std::mutex> lock(g_tileInstanceRefreshMutex); g_tileInstanceRefreshRequests.insert(id); }
    WakePollThread();
}
static std::wstring TileInstanceProfileActivation(const std::wstring& arguments)
{
    std::wstring profile;
    { std::lock_guard<std::mutex> lock(g_pathMutex); profile = g_iniPath; }
    return L"--ini " + aip::QuoteCommandLineArg(profile) + L" " + arguments;
}
static std::wstring TileInstanceActivation(const aip::content::TileInstance& tile)
{
    // A live notification overrides the static pin's activation arguments.
    // Preserve the profile identity there too, including paths with spaces.
    return TileInstanceProfileActivation(L"--tile-setup " + tile.id);
}
static aip::content::Snapshot ComposeTileInstance(const aip::content::TileInstance& tile,
    TileInstanceProviderState& state, const aip::content::SmtcMediaSnapshot& media, bool synchronous)
{
    using namespace aip::content;
    Snapshot frame; frame.active = true; frame.name = tile.name;
    frame.secondaryTileId = TileInstanceNativeId(tile); frame.secondaryBranding = tile.appearance.branding;
    frame.presentation = tile.presentation; frame.textMode = tile.configuration.textMode;
    frame.activationArguments = TileInstanceActivation(tile);
    if (!tile.error.empty()) {
        state.Stop();
        state.rss.reset(); state.prayer.reset(); state.notes.reset(); state.discord.reset();
        Entry background; background.background = L"Transparent";
        frame.backgroundId = BackgroundId::Transparent;
        if (state.backgroundKey != L"configuration-error" || !IsUsableWallpaperPath(state.backgroundPath)) {
            state.backgroundPath = ContentBackgroundPath(background); state.backgroundKey = L"configuration-error";
        }
        frame.backgroundPath = state.backgroundPath;
        frame.text = {L"Check tile settings", tile.error, L"Click this tile to repair its settings."};
        frame.wrapSourceText = true; frame.secondaryFrameKey = ContentSnapshotKey(frame); return frame;
    }
    const auto& entry = tile.configuration.entries.front();
    frame.backgroundId = BackgroundIdFor(entry.background);
    const auto backgroundKey = entry.background + L"\n" + entry.imagePath + L"\n" +
        (frame.backgroundId == BackgroundId::SmtcArtwork && media.artwork ? media.artwork->path : L"");
    const bool wallpaper = frame.backgroundId == BackgroundId::Wallpaper || frame.backgroundId == BackgroundId::LiveWallpaper;
    if (state.backgroundKey != backgroundKey || !IsUsableWallpaperPath(state.backgroundPath) || synchronous || state.forceRefresh ||
        (wallpaper && GetTickCount64() >= state.nextBackgroundRead)) {
        state.backgroundPath = ContentBackgroundPath(entry, &media); state.backgroundKey = backgroundKey;
        state.nextBackgroundRead = GetTickCount64() + static_cast<ULONGLONG>(tile.configuration.refreshSeconds) * 1000;
    }
    frame.backgroundPath = state.backgroundPath;
    if (frame.backgroundId == BackgroundId::SmtcArtwork) frame.backgroundLease = media.artwork;
    switch (tile.type) {
    case TileInstanceType::Wallpaper:
        frame.text = entry.custom;
        break;
    case TileInstanceType::Rss: {
        if (!state.rss) state.rss = std::make_unique<ContentRssProvider>();
        const auto settings = ReadRssFeedSettingsWith([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            return TileInstanceRead(tile, IniReadS, section, key, fallback);
        });
        // An unconfigured feed is a local setup state, not a polling worker.
        RssFeedSnapshot feed;
        if (!settings.valid || settings.feedUrl.empty()) {
            state.rss->Cancel(); feed.status = settings.valid ? L"Choose a feed URL in this tile's settings." : settings.validationError;
        } else { state.rss->Refresh(settings, synchronous, state.forceRefresh); feed = state.rss->Read(settings); }
        if (!feed.ok || feed.items.empty()) { frame.text = {L"Set up RSS", feed.status, L""}; break; }
        for (std::size_t i = 0; i < (std::min)(feed.items.size(), std::size_t(5)); ++i)
            frame.rssHeadlines.push_back({feed.items[i].title, feed.items[i].summary, L""});
        const auto selected = (std::min)(static_cast<std::size_t>(entry.rssItem - 1), feed.items.size() - 1);
        frame.text = {feed.items[selected].title, feed.sourceTitle, feed.items[selected].summary};
        std::wstring url, error;
        if (tile.clickAction == L"source" && ValidateRssFeedUrlSetting(feed.items[selected].link, false, url, error))
            frame.activationArguments = TileInstanceProfileActivation(L"--open-url " + aip::QuoteCommandLineArg(url));
        break;
    }
    case TileInstanceType::NowPlaying:
        frame.smtcSource = true; frame.smtcState = media.playbackState;
        frame.smtcIndicator = tile.configuration.smtc.indicatorMode;
        frame.smtcShowRepeatShuffle = tile.configuration.smtc.showRepeatShuffle;
        frame.smtcRepeat = media.repeat; frame.smtcShuffle = media.shuffle; frame.smtcAppName = media.source;
        if (media.appIcon) { frame.smtcAppIconPath = media.appIcon->path; frame.smtcAppIconLease = media.appIcon; }
        frame.text = media.hasSession ? Text{media.title, media.artist, MediaStatusCaption(frame)} : media.text;
        if (!media.hasSession) frame.text.badge = MediaStatusCaption(frame);
        break;
    case TileInstanceType::PrayerTimes: {
        if (!state.prayer) state.prayer = std::make_unique<ContentPrayerProvider>();
        if (state.forceRefresh) state.prayer->forceRequested = true;
        state.prayer->Refresh(tile.configuration.prayer, synchronous);
        const auto view = state.prayer->Read(tile.configuration.prayer);
        frame.prayerSource = true; frame.prayerClock = view.clock;
        frame.text = view.clock.Available() ? Text{view.clock.time, view.clock.prayer + L"\nat " + view.clock.location,
            view.clock.countdown + (view.clock.details.empty() ? L"" : L"\n" + view.clock.details)} : Text{view.title, view.secondary, view.badge};
        for (const auto& row : view.detailRows) frame.detailRows.push_back({row.label, row.value, L""});
        break;
    }
    case TileInstanceType::Genshin: case TileInstanceType::Hsr: case TileInstanceType::Zzz: {
        if (!state.notes) state.notes = std::make_unique<ContentNotesProvider>();
        const auto resource = DescribeTileInstanceType(tile.type).notesResource;
        if (state.forceRefresh) state.notes->refreshRequested.fetch_or(1u << resource);
        state.notes->Refresh(tile.configuration.notes, resource, synchronous);
        const auto notes = state.notes->ReadSnapshot(tile.configuration.notes, resource);
        frame.text = ContentNotesProvider::FormatSnapshot(notes, entry.notesDetail);
        frame.detailRows = aip::notes::BuildNoteTileRows(notes, entry.notesDetail,
            [](const wchar_t* key, const wchar_t* fallback) { return IniReadS(L"Strings", key, fallback); });
        frame.detailTopic = aip::notes::ResourceTileTopic(notes.resource);
        if (notes.state == aip::notes::StateKind::Error) frame.activationArguments += L" --tile-sign-in";
        break;
    }
    case TileInstanceType::Discord:
        if (!state.discord) state.discord = std::make_unique<ContentDiscordProvider>();
        if (state.forceRefresh) state.discord->Cancel();
        state.discord->Refresh(tile.configuration.discord, synchronous); frame.text = state.discord->Read(); frame.wrapSourceText = true;
        break;
    case TileInstanceType::QuickAction:
        frame.textMode = TextMode::Off;
        break;
    case TileInstanceType::Gallery:
        frame.text = {L"Photo gallery", L"Choose a folder and gallery tiles in Photo galleries.", L""};
        break;
    }
    state.forceRefresh = false;
    frame.secondaryFrameKey = ContentSnapshotKey(frame);
    return frame;
}
static bool TileInstanceEngineTick(bool synchronous, bool queueGeneration)
{
    using namespace aip::content;
    if (g_shutdownState.IsRequested()) return false;
    TileInstanceConfiguration cfg; std::wstring error;
    static std::wstring reportedError;
    if (!ReadCurrentTileInstances(cfg, error)) {
        StopTileInstanceProviders(); g_contentSmtc.Cancel(); g_contentCaps.Cancel(); g_contentAsus.Cancel();
        PublishSecondarySnapshots({}); PublishContentSnapshot({});
        if (error != reportedError) { Log(L"Tile configuration: %ls", error.c_str()); reportedError = error; }
        return false;
    }
    reportedError.clear();
    const auto pins = CachedSecondaryTileIds();
    std::set<std::wstring> refresh;
    { std::lock_guard<std::mutex> lock(g_tileInstanceRefreshMutex); refresh.swap(g_tileInstanceRefreshRequests); }
    const auto demanded = [&](const TileInstance& tile) { return tile.primary || pins.count(TileInstanceNativeId(tile)) != 0; };
    std::set<std::wstring> used;
    bool smtcNeeded = false, artworkNeeded = false, appIconNeeded = false;
    int smtcSeconds = 86400;
    for (const auto& tile : cfg.tiles) if (demanded(tile)) {
        if (tile.type == TileInstanceType::QuickAction && tile.error.empty()) continue;
        used.insert(tile.id);
        if (tile.error.empty() && (tile.type == TileInstanceType::NowPlaying || (!tile.configuration.entries.empty() &&
            BackgroundIdFor(tile.configuration.entries.front().background) == BackgroundId::SmtcArtwork))) {
            smtcNeeded = true; smtcSeconds = (std::min)(smtcSeconds, tile.configuration.refreshSeconds);
            artworkNeeded |= BackgroundIdFor(tile.configuration.entries.front().background) == BackgroundId::SmtcArtwork;
            appIconNeeded |= tile.configuration.smtc.indicatorMode == MediaIndicatorMode::AppIcon;
        }
    }
    for (auto it = g_tileInstanceProviders.begin(); it != g_tileInstanceProviders.end();)
        if (!used.count(it->first)) { it->second->Stop(); it = g_tileInstanceProviders.erase(it); } else ++it;
    if (smtcNeeded) g_contentSmtc.Refresh(smtcSeconds, synchronous, artworkNeeded, ContentDirectLiveDeliveryAvailable(), appIconNeeded);
    else g_contentSmtc.Cancel();
    const auto media = smtcNeeded ? g_contentSmtc.Read() : SmtcMediaSnapshot{};
    // Independent plugins intentionally don't contribute text to any tile.
    Configuration plugins;
    if (ReadTileInstancePlugins(plugins)) {
        if (plugins.caps.actionsEnabled) g_contentCaps.Refresh(plugins.caps); else g_contentCaps.Cancel();
        if (plugins.asus.hardwareEnabled) g_contentAsus.Refresh(plugins.asus); else g_contentAsus.Cancel();
    } else { g_contentCaps.Cancel(); g_contentAsus.Cancel(); }
    std::vector<Snapshot> frames; Snapshot primary;
    for (const auto& tile : cfg.tiles) if (demanded(tile)) {
        // Quick Actions are static native topic logos. They need metadata and
        // activation only; sending a transparent notification would hide them.
        if (tile.type == TileInstanceType::QuickAction && tile.error.empty()) continue;
        auto& state = g_tileInstanceProviders[tile.id];
        if (state && state->type != tile.type) { state->Stop(); state.reset(); }
        if (!state) { state = std::make_unique<TileInstanceProviderState>(); state->type = tile.type; }
        state->forceRefresh = refresh.count(tile.id) != 0;
        if (state->forceRefresh) ForgetSecondaryTileDelivery(TileInstanceNativeId(tile));
        auto frame = ComposeTileInstance(tile, *state, media, synchronous);
        if (tile.primary) primary = frame;
        if (IsUsableWallpaperPath(frame.backgroundPath)) frames.push_back(std::move(frame));
    }
    for (auto gallery : ContentGalleryFrames()) if (IsUsableWallpaperPath(gallery.backgroundPath)) frames.push_back(std::move(gallery));
    PublishContentSnapshot(primary);
    static bool primarySeen = false;
    static Snapshot cleared;
    if (primary.active) cleared = {};
    if (primarySeen && !primary.active && !cleared.active) {
        // Removing the primary wallpaper type stops observation immediately,
        // and replaces its old notification once without removing its pin.
        Entry blank; blank.background = L"Transparent";
        cleared.active = true; cleared.textMode = TextMode::Off; cleared.backgroundId = BackgroundId::Transparent;
        cleared.backgroundPath = ContentBackgroundPath(blank); cleared.secondaryBranding = L"Name";
        cleared.secondaryFrameKey = ContentSnapshotKey(cleared);
    }
    bool clearUsable = cleared.active && IsUsableWallpaperPath(cleared.backgroundPath);
    if (cleared.active && !clearUsable) {
        Entry blank; blank.background = L"Transparent";
        cleared.backgroundPath = ContentBackgroundPath(blank);
        clearUsable = IsUsableWallpaperPath(cleared.backgroundPath);
    }
    if (clearUsable) frames.push_back(cleared);
    PublishSecondarySnapshots(frames);
    if (primary.active && !primarySeen && g_hwnd && !g_contentFrameReadyPosted.exchange(true)) {
        if (!PostMessageW(g_hwnd, WM_APP_CONTENT_FRAME_READY, 0, 0)) g_contentFrameReadyPosted = false;
    }
    primarySeen = primary.active;
    if (synchronous || !queueGeneration) return !frames.empty();
    if (!GenerationWorkPending() && GetTickCount64() >= g_contentQueueRetryAfter.load()) {
        auto changed = PrepareSecondaryBatch(frames);
        if (!changed.empty() && ContentDirectLiveDeliveryAvailable())
            DESKTOPSTUB_SECONDARY_BATCH_GENERATE(std::move(changed), L"Changed tile instances");
        else if (primary.active && !ContentDirectLiveDeliveryAvailable()) {
            static std::wstring last;
            if (primary.secondaryFrameKey != last && DESKTOPSTUB_CONTENT_QUEUE_GENERATE(primary.backgroundPath, L"Primary wallpaper tile") != 0)
                last = primary.secondaryFrameKey;
        }
    }
    return !frames.empty();
}
