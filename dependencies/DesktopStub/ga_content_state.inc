// One immutable data snapshot per generation. Provider refreshes never change
// half of an image or mutate TileText INI values while assets are being rendered.
static std::mutex g_contentSnapshotMutex;
static aip::content::Snapshot g_contentSnapshot;
static std::vector<aip::content::Snapshot> g_contentQueueSnapshots;
#include "../content_queue.h"
static std::mutex g_contentQueueDeliveryMutex;
static aip::content::QueueDelta g_contentQueueDelivery;
static std::atomic<ULONGLONG> g_contentQueueRetryAfter{0};
static bool LiveTileModeReregistrationPending();
static bool ContentDirectLiveDeliveryAvailable()
{
#ifdef DESKTOPSTUB_TEST_NATIVE_QUEUE_AVAILABLE
    return DESKTOPSTUB_TEST_NATIVE_QUEUE_AVAILABLE();
#else
    // Compatibility helpers acknowledge through a shared pending file and
    // generic trace. They cannot safely acknowledge individual queued slots
    // or fence a late reset after a timeout; use direct updates for batching.
    return UseExperimentalLiveTileUpdate() && !LiveTileModeReregistrationPending() && CurrentProcessHasPackageIdentity();
#endif
}
static bool ContentNativeQueueAvailable(const aip::content::Configuration& configuration)
{
    return aip::content::NativeQueueEligible(configuration, ContentDirectLiveDeliveryAvailable());
}
static std::wstring ContentTimingWarning(const aip::content::Configuration& configuration)
{
    const bool live = UseExperimentalLiveTileUpdate();
    if (aip::content::NativeQueueEligible(configuration, live) && !ContentNativeQueueAvailable(configuration))
        return L"Windows-managed rotation needs a resident with package identity and completed mode registration. DesktopStub uses its own cycle timer while compatibility helpers are needed.";
    return aip::content::TimingWarning(configuration, live);
}
static std::wstring ContentTimingChangeWarning(const aip::content::Configuration& before, const aip::content::Configuration& after)
{
    if (!after.enabled) return {};
    const auto warning = ContentTimingWarning(after);
    if (!warning.empty() && (!before.enabled || warning != ContentTimingWarning(before) || after.cycleSeconds < before.cycleSeconds))
        return warning;
    return {};
}
static std::wstring ContentNativeQueueLayout(const aip::content::Configuration& configuration)
{
    return aip::content::NativeQueueLayoutKey(configuration) + L":" +
        ManifestCompatibilityTargetIniValue(ConfiguredManifestCompatibilityTarget()) + L":" +
        LiveTileTemplateStyleIniValue(ConfiguredLiveTileTemplateStyle());
}
static thread_local const aip::content::Snapshot* g_renderContent = nullptr;
static bool ContentHasTransparentBackground()
{
    return g_renderContent && g_renderContent->backgroundId == aip::content::BackgroundId::Transparent;
}
static constexpr UINT WM_APP_CONTENT_LOGICAL_CAPS = WM_APP + 80;
static constexpr UINT WM_APP_CONTENT_FRAME_READY = WM_APP + 81;
static std::atomic<bool> g_contentFrameReadyPosted{false};
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
    if (!g_contentSnapshot.nativeQueue) g_contentQueueSnapshots.clear();
}
static void PublishContentQueueSnapshots(std::vector<aip::content::Snapshot> snapshots)
{
    std::lock_guard<std::mutex> lock(g_contentSnapshotMutex);
    g_contentQueueSnapshots = std::move(snapshots);
    g_contentSnapshot = g_contentQueueSnapshots.empty() ? aip::content::Snapshot{} : g_contentQueueSnapshots.front();
}
static std::vector<aip::content::Snapshot> CurrentContentQueueSnapshots()
{
    std::lock_guard<std::mutex> lock(g_contentSnapshotMutex);
    return g_contentQueueSnapshots;
}
static std::vector<aip::content::Snapshot> PrepareContentQueueBatch(
    const std::vector<aip::content::Snapshot>& snapshots, bool force = false)
{
    if (snapshots.empty()) return {};
    std::lock_guard<std::mutex> lock(g_contentQueueDeliveryMutex);
    return g_contentQueueDelivery.Plan(snapshots.front().nativeQueueLayout, snapshots, force);
}
static void AcknowledgeContentQueueFrame(const aip::content::Snapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(g_contentQueueDeliveryMutex);
    if (snapshot.nativeQueue) g_contentQueueDelivery.Ack(snapshot);
    else g_contentQueueDelivery.LeaveQueue();
}
static bool ContentQueueHasAcknowledgedFrames()
{
    std::lock_guard<std::mutex> lock(g_contentQueueDeliveryMutex);
    return g_contentQueueDelivery.CommittedCount() != 0;
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
    explicit ContentRenderScope(const aip::content::Snapshot* supplied = nullptr)
    {
        if (supplied) snapshot = *supplied;
        else if (CompositionRequested())
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
