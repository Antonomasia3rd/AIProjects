// One immutable data snapshot per generation. Provider refreshes never change
// half of an image or mutate TileText INI values while assets are being rendered.
static std::mutex g_contentSnapshotMutex;
static aip::content::Snapshot g_contentSnapshot;
static std::vector<aip::content::Snapshot> g_contentQueueSnapshots;
#include "../content_queue.h"
#include "../secondary_tiles.h"
static bool SecondaryTileExists(const std::wstring& id);
static std::set<std::wstring> CachedSecondaryTileIds();
static bool SecondaryTileInventoryReady();
static std::wstring LocalAppDataPath();
static void WakePollThread();
static void RefreshGallerySource();
static std::vector<aip::content::Snapshot> CurrentGalleryFrames();
static bool ValidateGalleryFrame(const aip::content::Snapshot& frame);
static void AcknowledgeGalleryFrame(const aip::content::Snapshot& frame);
static void FailGalleryFrame(const aip::content::Snapshot& frame);
static void StopGallerySource();
static std::vector<aip::content::Snapshot> ContentGalleryFrames()
{
#ifdef DESKTOPSTUB_TEST_GALLERY_FRAMES
    return DESKTOPSTUB_TEST_GALLERY_FRAMES();
#else
    return CurrentGalleryFrames();
#endif
}
static bool ContentGalleryFrameCurrent(const aip::content::Snapshot& frame)
{
#ifdef DESKTOPSTUB_TEST_GALLERY_VALIDATE
    return DESKTOPSTUB_TEST_GALLERY_VALIDATE(frame);
#else
    return ValidateGalleryFrame(frame);
#endif
}
static void ContentAcknowledgeGalleryFrame(const aip::content::Snapshot& frame)
{
#ifdef DESKTOPSTUB_TEST_GALLERY_ACK
    DESKTOPSTUB_TEST_GALLERY_ACK(frame);
#else
    AcknowledgeGalleryFrame(frame);
#endif
}
static void ContentFailGalleryFrame(const aip::content::Snapshot& frame) noexcept
{
    if (!frame.galleryFrame) return;
    try {
#ifdef DESKTOPSTUB_TEST_GALLERY_FAIL
        DESKTOPSTUB_TEST_GALLERY_FAIL(frame);
#else
        FailGalleryFrame(frame);
#endif
    } catch (...) {} // Failure reporting must not replace the generation error.
}
struct GalleryGenerationGuard {
    const aip::content::Snapshot* frame = nullptr;
    bool delivered = false;
    GalleryGenerationGuard(const aip::content::Snapshot* source, bool renderOnly) : frame(!renderOnly && source && source->galleryFrame ? source : nullptr) {}
    ~GalleryGenerationGuard() { if (frame && !delivered) ContentFailGalleryFrame(*frame); }
};
struct GalleryBatchFailureGuard {
    const std::vector<aip::content::Snapshot>& frames;
    std::size_t next = 0;
    ~GalleryBatchFailureGuard() { for (std::size_t index = next; index < frames.size(); ++index) ContentFailGalleryFrame(frames[index]); }
};
static std::wstring SecondaryDeliveryKey(const aip::content::Snapshot& frame)
{
    return frame.galleryFrame||((frame.calendarSource||frame.mailSource)&&frame.nativeQueue)?frame.secondaryTileId+L"|"+frame.nativeQueueTag:frame.secondaryTileId;
}
static bool TileSnapshotUsesRegistration(const aip::content::Snapshot& frame);
static bool TileSnapshotUsesSeparateRegistration(const aip::content::Snapshot& frame);
static bool GalleryWindowComplete(const std::vector<aip::content::Snapshot>& frames, const aip::content::Snapshot& wanted)
{
    if (!wanted.gallerySlotCount || wanted.gallerySlotCount > 5) return false;
    const bool registration = TileSnapshotUsesRegistration(wanted);
    if (registration && wanted.gallerySlotCount != 1) return false;
    std::set<unsigned> slots;
    for (const auto& frame : frames) if (frame.galleryFrame && frame.secondaryTileId == wanted.secondaryTileId &&
        frame.galleryEpoch == wanted.galleryEpoch && frame.galleryTicket == wanted.galleryTicket) {
        if (frame.gallerySlotCount != wanted.gallerySlotCount || frame.gallerySlot >= frame.gallerySlotCount ||
            frame.nativeQueueTag != L"g" + std::to_wstring(frame.gallerySlot + 1) ||
            frame.nativeQueue == registration || TileSnapshotUsesRegistration(frame) != registration ||
            !slots.insert(frame.gallerySlot).second || !IsUsableWallpaperPath(frame.backgroundPath) || !ContentGalleryFrameCurrent(frame)) return false;
    }
    return slots.size() == wanted.gallerySlotCount;
}
static std::mutex g_secondarySnapshotMutex;
static std::vector<aip::content::Snapshot> g_secondarySnapshots;
static std::mutex g_secondaryDeliveryMutex;
static std::map<std::wstring, std::wstring> g_secondaryDelivered;
static void PublishSecondarySnapshots(std::vector<aip::content::Snapshot> snapshots)
{
    std::lock_guard<std::mutex> lock(g_secondarySnapshotMutex);
    g_secondarySnapshots = std::move(snapshots);
    std::lock_guard<std::mutex> primaryLock(g_contentSnapshotMutex);
    const auto primary = std::find_if(g_secondarySnapshots.begin(), g_secondarySnapshots.end(), [](const auto& frame) {
        return frame.secondaryTileId.empty();
    });
    g_contentSnapshot = primary == g_secondarySnapshots.end() ? aip::content::Snapshot{} : *primary;
    g_contentQueueSnapshots.clear();
}
static std::vector<aip::content::Snapshot> CurrentSecondarySnapshots()
{
    std::lock_guard<std::mutex> lock(g_secondarySnapshotMutex);
    return g_secondarySnapshots;
}
static bool CalendarQueueFrameCurrent(const aip::content::Snapshot& frame)
{
    using namespace aip::content;
    if(!frame.calendarSource||!frame.nativeQueue||frame.secondaryTileId.empty()||frame.nativeQueueLayout.empty())return false;
    const bool first=frame.nativeQueueTag==L"cal1"&&frame.calendarFlavor==CalendarFlavor::NextEvent;
    const bool agenda=frame.nativeQueueTag==L"cal2"&&frame.calendarFlavor==CalendarFlavor::Agenda;
    if(!first&&!agenda)return false;
    std::lock_guard<std::mutex> lock(g_secondarySnapshotMutex);
    return std::any_of(g_secondarySnapshots.begin(),g_secondarySnapshots.end(),[&](const Snapshot& current){
        return current.calendarSource&&current.nativeQueue&&current.secondaryTileId==frame.secondaryTileId&&
            current.nativeQueueTag==frame.nativeQueueTag&&current.nativeQueueLayout==frame.nativeQueueLayout&&
            current.secondaryFrameKey==frame.secondaryFrameKey;
    });
}
static bool TileSnapshotUsesRegistration(const aip::content::Snapshot& frame);
static bool TileRegistrationReadyForId(const std::wstring& id);
static bool MailQueueFrameCurrent(const aip::content::Snapshot& frame)
{
    if(!frame.mailSource||!frame.nativeQueue||frame.secondaryTileId.empty()||frame.nativeQueueLayout.empty()||
        frame.mailMessageIndex>=5||frame.nativeQueueTag!=L"mail"+std::to_wstring(frame.mailMessageIndex+1))return false;
    std::lock_guard<std::mutex> lock(g_secondarySnapshotMutex);
    return std::any_of(g_secondarySnapshots.begin(),g_secondarySnapshots.end(),[&](const auto& current){
        return current.mailSource&&current.nativeQueue&&current.secondaryTileId==frame.secondaryTileId&&
            current.nativeQueueTag==frame.nativeQueueTag&&current.nativeQueueLayout==frame.nativeQueueLayout&&
            current.secondaryFrameKey==frame.secondaryFrameKey;
    });
}
static std::vector<aip::content::Snapshot> PrepareSecondaryBatch(const std::vector<aip::content::Snapshot>& frames, bool force = false)
{
    std::vector<aip::content::Snapshot> changed;
    std::map<std::wstring, bool> pinned, completeWindows;
    for (const auto& frame : frames) {
        if (!frame.active || frame.secondaryFrameKey.empty()) continue;
        {
            std::lock_guard<std::mutex> lock(g_secondaryDeliveryMutex);
            const auto found = g_secondaryDelivered.find(SecondaryDeliveryKey(frame));
            if (!force && found != g_secondaryDelivered.end() && found->second == frame.secondaryFrameKey) continue;
        }
        if (!IsUsableWallpaperPath(frame.backgroundPath)) { ContentFailGalleryFrame(frame); continue; }
        const bool registration = TileSnapshotUsesRegistration(frame);
        if(frame.calendarSource&&frame.nativeQueue&&!CalendarQueueFrameCurrent(frame))continue;
        if(frame.mailSource&&frame.nativeQueue&&!MailQueueFrameCurrent(frame))continue;
        if (registration && TileSnapshotUsesSeparateRegistration(frame) && !TileRegistrationReadyForId(frame.policy.owner)) continue;
        if (!registration && !frame.secondaryTileId.empty() && pinned.find(frame.secondaryTileId) == pinned.end()) {
#ifdef DESKTOPSTUB_TEST_SECONDARY_EXISTS
            pinned[frame.secondaryTileId] = DESKTOPSTUB_TEST_SECONDARY_EXISTS(frame.secondaryTileId);
#else
            pinned[frame.secondaryTileId] = SecondaryTileExists(frame.secondaryTileId);
#endif
        }
        if (!registration && !frame.secondaryTileId.empty() && !pinned[frame.secondaryTileId]) continue;
        if (frame.galleryFrame) {
            const auto window = frame.secondaryTileId + L"|" + frame.galleryEpoch + L"|" + std::to_wstring(frame.galleryTicket);
            auto ready = completeWindows.find(window);
            if (ready == completeWindows.end()) ready = completeWindows.emplace(window, GalleryWindowComplete(frames, frame)).first;
            if (!ready->second) { ContentFailGalleryFrame(frame); continue; }
        }
        changed.push_back(frame);
    }
    return changed;
}
static void AcknowledgeSecondaryFrame(const aip::content::Snapshot& frame)
{
    if (frame.secondaryFrameKey.empty()) return;
    std::lock_guard<std::mutex> lock(g_secondaryDeliveryMutex);
    if (!frame.nativeQueue||((frame.galleryFrame||frame.calendarSource||frame.mailSource)&&frame.nativeQueueReset)) {
        const auto prefix = frame.secondaryTileId + L"|";
        g_secondaryDelivered.erase(frame.secondaryTileId);
        for (auto entry = g_secondaryDelivered.begin(); entry != g_secondaryDelivered.end();) {
            if (entry->first.compare(0, prefix.size(), prefix) == 0) entry = g_secondaryDelivered.erase(entry); else ++entry;
        }
    }
    g_secondaryDelivered[SecondaryDeliveryKey(frame)] = frame.secondaryFrameKey;
}
static void ForgetSecondaryTileDelivery(const std::wstring& id)
{
    std::lock_guard<std::mutex> lock(g_secondaryDeliveryMutex);
    g_secondaryDelivered.erase(id);
    const auto prefix = id + L"|";
    for (auto entry = g_secondaryDelivered.begin(); entry != g_secondaryDelivered.end();) {
        if (entry->first.compare(0, prefix.size(), prefix) == 0) entry = g_secondaryDelivered.erase(entry); else ++entry;
    }
}
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
static bool TilePolicyUsesRegistration(const aip::content::TileDeliveryPolicy& policy)
{
    if (!policy.included) return false;
    if (policy.Mode()==L"Registration") return true;
    if (policy.Mode()==L"LiveTile") return false;
#ifdef DESKTOPSTUB_TEST_NATIVE_QUEUE_AVAILABLE
    // The existing injected direct-transport capability stands in for package
    // identity in inert host fixtures; never inspect the test runner's identity.
    return !DESKTOPSTUB_TEST_NATIVE_QUEUE_AVAILABLE();
#else
    aip::content::TilePolicyReadScope scope(policy);
    return !UseExperimentalLiveTileUpdate();
#endif
}
static bool TileSnapshotUsesRegistration(const aip::content::Snapshot& frame)
{
    return TilePolicyUsesRegistration(frame.policy);
}
static bool TileSnapshotUsesSeparateRegistration(const aip::content::Snapshot& frame)
{
    return TileSnapshotUsesRegistration(frame)&&(!frame.secondaryTileId.empty()||
        frame.policy.Read(L"Settings",L"PrimaryRegistration",L"Separate")!=L"Main");
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
static bool TryContentWallpaperFit(FitMode& result)
{
    if(!g_renderContent||g_renderContent->capturedWallpaperFit<static_cast<int>(FitMode::Fill)||
        g_renderContent->capturedWallpaperFit>static_cast<int>(FitMode::Span))return false;
    result=static_cast<FitMode>(g_renderContent->capturedWallpaperFit);return true;
}
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

static bool TileInstanceModeRequested()
{
    return IniReadS(L"Tiles", L"Instances", L"\x1") != L"\x1";
}
static bool ReadCurrentTileInstances(aip::content::TileInstanceConfiguration& result, std::wstring& error)
{
    // A typed view belongs to a complete published INI revision. No per-tick
    // reparsing and no provider callbacks while the INI cache is locked.
    static std::mutex mutex;
    static std::wstring lastProfile, lastError;
    static unsigned long long lastRevision = ~0ull;
    static aip::content::TileInstanceConfiguration cached;
    static bool valid = false;
    std::vector<IniEntry> refreshed; bool ready = false;
    ReadIniSectionCached(L"Tiles", refreshed, &ready);
    if (!ready) { error = L"The tile configuration is temporarily unavailable."; return false; }
    std::lock_guard<std::mutex> viewLock(mutex);
    std::vector<aip::IniSectionData> document;
    std::wstring profile; unsigned long long revision = 0;
    {
        std::lock_guard<std::mutex> cacheLock(g_iniCacheMutex);
        profile = g_iniCacheProfile; revision = g_iniCacheRevision;
        if (lastProfile != profile || lastRevision != revision) document = g_iniCacheDoc;
    }
    if (lastProfile != profile || lastRevision != revision) {
        const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            std::wstring value;
            return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
        };
        aip::content::TileInstanceConfiguration next;
        valid = ValidateTileInstanceHostConfiguration(read, next, lastError);
        if (!valid && read(L"Tiles", L"Instances", L"\x1") != L"\x1") {
            // A malformed source belongs to its tile. Keep other tiles alive
            // and publish a clickable repair state for the affected instance.
            std::vector<std::wstring> ids;
            std::wstring version = read(L"Tiles", L"SchemaVersion", L"1"), issue;
            if (aip::content::ParseTileInstanceIds(read(L"Tiles", L"Instances", L""), ids, issue) &&
                aip::content::NormalizeTileInstanceSetting(L"Tiles", L"SchemaVersion", version, issue)) {
                next = {}; next.configured = true; bool primarySeen = false;
                for (const auto& id : ids) {
                    aip::content::TileInstanceConfiguration one;
                    const auto isolatedRead = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
                        if (IEquals(section, L"Tiles") && IEquals(key, L"Instances")) return id;
                        return read(section, key, fallback);
                    };
                    aip::content::TileInstance tile;
                    if (ValidateTileInstanceHostConfiguration(isolatedRead, one, issue) && !one.tiles.empty()) tile = std::move(one.tiles.front());
                    else {
                        tile.id = id; tile.name = read(aip::content::TileInstanceSection(id).c_str(), L"Name", id.c_str()); tile.error = issue;
                        tile.accountReference = read(aip::content::TileInstanceSection(id).c_str(), L"Account", L"");
                        const auto* descriptor = aip::content::FindTileInstanceType(read(aip::content::TileInstanceSection(id).c_str(), L"Type", L""));
                        if (descriptor) tile.type = descriptor->type;
                        aip::content::ParseBool(read(aip::content::TileInstanceSection(id).c_str(), L"Primary", L"0"), tile.primary);
                        tile.primary = tile.primary && tile.type == aip::content::TileInstanceType::Wallpaper;
                        aip::content::Entry entry; entry.background = L"Transparent"; tile.configuration.entries.push_back(std::move(entry));
                    }
                    if (tile.primary && primarySeen) { tile.error = L"Only one Wallpaper tile can use the primary Desktop identity."; tile.primary = false; }
                    primarySeen = primarySeen || tile.primary;
                    next.tiles.push_back(std::move(tile));
                }
                valid = true; lastError.clear();
            }
        }
        cached = valid ? std::move(next) : aip::content::TileInstanceConfiguration{};
        lastProfile = profile; lastRevision = revision;
    }
    if (!valid) { error = lastError; return false; }
    result = cached; error.clear(); return true;
}
static bool CompositionRequested()
{
    if (TileInstanceModeRequested()) return true;
    for (const auto& id : CachedSecondaryTileIds())
        if (id.compare(0, 10, L"AipSource_") == 0 || id.compare(0, 11, L"AipGallery_") == 0) return true;
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
    if (snapshot.galleryFrame) return; // Gallery tickets never acknowledge/reset the primary queue.
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
static bool ReadContentConfiguration(aip::content::Configuration& cfg, std::wstring& error, aip::content::SecondaryTileConfiguration* tiles);

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
    std::unique_ptr<aip::content::TilePolicyReadScope> policyScope;
    explicit ContentRenderScope(const aip::content::Snapshot* supplied = nullptr)
    {
        if (supplied) snapshot = *supplied;
        else if (CompositionRequested())
        {
            std::lock_guard<std::mutex> lock(g_contentSnapshotMutex);
            snapshot = g_contentSnapshot;
        }
        g_renderContent = snapshot.active ? &snapshot : nullptr;
        policyScope = std::make_unique<aip::content::TilePolicyReadScope>(snapshot.policy);
    }
    ~ContentRenderScope() { g_renderContent = previous; }
    ContentRenderScope(const ContentRenderScope&) = delete;
    ContentRenderScope& operator=(const ContentRenderScope&) = delete;
};

static bool ContentEngineTick(bool synchronous = false, bool queueGeneration = true);
static void StopContentProviders();
