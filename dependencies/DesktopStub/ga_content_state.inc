// One immutable data snapshot per generation. Provider refreshes never change
// half of an image or mutate TileText INI values while assets are being rendered.
static std::mutex g_contentSnapshotMutex;
static aip::content::Snapshot g_contentSnapshot;
static thread_local const aip::content::Snapshot* g_renderContent = nullptr;
static bool ContentHasTransparentBackground()
{
    return g_renderContent && g_renderContent->backgroundId == aip::content::BackgroundId::Transparent;
}
static constexpr UINT WM_APP_CONTENT_LOGICAL_CAPS = WM_APP + 80;
static std::atomic<int> g_contentLogicalCaps{-1};

// Called only on the UI thread. Worker cleanup uses this atomic cache after
// shutdown stops dispatching window messages. The inert harness injects a
// synthetic sampler and never reads the real keyboard toggle.
static int CaptureContentLogicalCapsState()
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
#ifdef DESKTOPSTUB_TEST_LOGICAL_CAPS
    const int state = DESKTOPSTUB_TEST_LOGICAL_CAPS();
#else
    const int state = -1;
#endif
#else
    const int state = (GetKeyState(VK_CAPITAL) & 1) ? 1 : 0;
#endif
    if (state >= 0) g_contentLogicalCaps = state != 0 ? 1 : 0;
    return g_contentLogicalCaps.load();
}

static bool CompositionRequested()
{
    bool enabled = false;
    // Invalid live edits must reach the configuration error reporter rather
    // than silently routing into the wallpaper worker.
    if (!aip::content::ParseBool(IniReadS(L"Content", L"Enabled", L"0"), enabled)) return true;
    return enabled ||
        ParseContentSourceValue(IniReadS(L"Settings", L"ContentSource", L"Wallpaper")) == ContentSource::RssFeed;
}

static void PublishContentSnapshot(aip::content::Snapshot snapshot)
{
    std::lock_guard<std::mutex> lock(g_contentSnapshotMutex);
    g_contentSnapshot = std::move(snapshot);
}

static bool ReadContentConfiguration(aip::content::Configuration& cfg, std::wstring& error);

static bool RefreshGenerationConfiguration()
{
    // Generation is an explicit publication boundary. Observe external edits
    // even inside the ordinary polling cache's refresh interval, without writes.
    aip::IniWriteMutexGuard configurationScope(g_iniPath, CurrentIniWriteLockWaitMs());
    if (!configurationScope.IsLocked()) return false;
    std::wstring text;
    return ReadIniFileForMutation(text);
}

static std::wstring CurrentGenerationBackground(const std::wstring& wallpaperOverride = {})
{
    if (!RefreshGenerationConfiguration()) return {};
    if (CompositionRequested())
    {
        // The desktop can have a solid-color background while the tile uses
        // an image, media artwork, solid color or transparency of its own.
        // Never substitute desktop wallpaper for an unavailable content frame.
        aip::content::Configuration configuration;
        std::wstring error;
        if (!ReadContentConfiguration(configuration, error) || !configuration.enabled)
            return {};
        std::lock_guard<std::mutex> lock(g_contentSnapshotMutex);
        return g_contentSnapshot.active ? g_contentSnapshot.backgroundPath : std::wstring{};
    }
    return wallpaperOverride.empty() ? GetWallpaper() : wallpaperOverride;
}

struct ContentRenderScope
{
    aip::content::Snapshot snapshot;
    const aip::content::Snapshot* previous = g_renderContent;
    ContentRenderScope()
    {
        if (CompositionRequested())
        {
            std::lock_guard<std::mutex> lock(g_contentSnapshotMutex);
            snapshot = g_contentSnapshot;
        }
        g_renderContent = snapshot.active ? &snapshot : nullptr;
    }
    ~ContentRenderScope() { g_renderContent = previous; }
    ContentRenderScope(const ContentRenderScope&) = delete;
    ContentRenderScope& operator=(const ContentRenderScope&) = delete;
};

static bool ContentEngineTick(bool synchronous = false, bool queueGeneration = true);
static void StopContentProviders();
