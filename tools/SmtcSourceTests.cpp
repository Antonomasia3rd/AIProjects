#define NOMINMAX
#include "../dependencies/content_sources/smtc.inc"
#include <winrt/Windows.Foundation.Collections.h>
#include "../dependencies/content_source_host.h"
#include <iostream>
#include <atomic>
#include <thread>

namespace {
int failures = 0;
int checks = 0;
std::atomic<int> successfulReads{0}, unavailableReads{0};
void Check(bool condition, const char* message)
{
    ++checks;
    std::cout << (condition ? "ok - " : "FAIL - ") << message << '\n';
    if (!condition) ++failures;
}

std::vector<std::uint8_t> BitmapBytes(UINT width = 2, UINT height = 2, BYTE color = 64)
{
    const UINT stride = (width * 3u + 3u) & ~3u;
    std::vector<std::uint8_t> bytes(54 + stride * height, color);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42; file.bfSize = static_cast<DWORD>(bytes.size()); file.bfOffBits = 54;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info); info.biWidth = static_cast<LONG>(width); info.biHeight = -static_cast<LONG>(height);
    info.biPlanes = 1; info.biBitCount = 24; info.biSizeImage = stride * height;
    memcpy(bytes.data(), &file, sizeof(file)); memcpy(bytes.data() + 14, &info, sizeof(info));
    return bytes;
}

void TestInjectedMediaAndArtwork()
{
    using namespace aip::content;
    Check(smtc_artwork_detail::AllowedDimensions(8192, 2048) &&
        !smtc_artwork_detail::AllowedDimensions(8192, 2049) &&
        !smtc_artwork_detail::AllowedDimensions(0, 1) && !smtc_artwork_detail::AllowedDimensions(1, 8193),
        "decoded dimensions independently enforce zero, dimension, and total pixel limits");
    SmtcSessionData data;
    data.hasSession = true; data.title = L"First song"; data.albumArtist = L"Album artist";
    data.appId = L"Vendor.Player_123!App"; data.status = L"Playing"; data.artworkBytes = BitmapBytes();
    int reads = 0;
    const auto reader = [&](auto deadline, bool includeArtwork, const auto&) {
        ++reads;
        Check(deadline > smtc_detail::Clock::now(), "injected media read receives the shared deadline");
        auto value = data;
        if (!includeArtwork) value.artworkBytes.clear();
        return value;
    };
    SmtcMediaSnapshot media;
    std::wstring error;
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader) &&
        reads == 1 && media.text.primary == L"First song" && media.artist == L"Album artist" &&
        media.text.secondary == L"Album artist\nPlaying - Player" && media.artworkBytes == data.artworkBytes,
        "one injected session read pairs title/status and its thumbnail without querying the OS");
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), false, reader) &&
        media.artworkBytes.empty() && !media.artwork && media.text.primary == L"First song",
        "text-only media reads retain their presentation without artwork I/O");
    SmtcArtworkCache cache(L"SmtcFixture", L"C:\\fixture-only\\profile-one.ini");
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, {}, &cache) &&
        media.artwork && media.artworkBytes.empty() && media.artwork->width == 2 && media.artwork->height == 2,
        "the same injected media read decodes bounded artwork into its profile cache");
    auto first = media.artwork;
    const auto firstPath = first ? first->path : L"";
    Check(!firstPath.empty() && GetFileAttributesW(firstPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
        firstPath.find(L"AIProjects-SMTC-") != std::wstring::npos,
        "media artwork is writable temporary content instead of an executable-directory artifact");
    auto same = cache.Store(data.artworkBytes, error, smtc_detail::Clock::now() + std::chrono::seconds(2));
    Check(same == first, "unchanged artwork reuses the immutable leased file");
    SmtcArtworkCache otherProfile(L"SmtcFixture", L"C:\\fixture-only\\profile-two.ini");
    auto other = otherProfile.Store(data.artworkBytes, error, smtc_detail::Clock::now() + std::chrono::seconds(2));
    Check(other && other->path != firstPath, "profiles never share artwork-file ownership");
    data.title = L"Second song"; data.artworkBytes = BitmapBytes(3, 2, 128);
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, {}, &cache) &&
        media.artwork && media.artwork->path != firstPath && media.title == L"Second song",
        "a track change publishes its new text and image together");
    Check(GetFileAttributesW(firstPath.c_str()) != INVALID_FILE_ATTRIBUTES,
        "an older rendered snapshot's artwork remains valid while its lease is retained");
    data.artworkBytes.clear();
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, {}, &cache) &&
        !media.artwork && media.artworkBytes.empty() && media.title == L"Second song",
        "a track without a thumbnail clears preceding artwork instead of showing stale art");
    data.hasSession = false; data.artworkBytes = BitmapBytes();
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, {}, &cache) &&
        !media.artwork && media.artworkBytes.empty() && media.text.primary == L"No media playing",
        "idle ignores stale injected properties and thumbnail data");
    Check(!ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, [] { return true; }, &cache) &&
        !media.artwork && media.text.primary.empty(), "cancellation clears all earlier media fields");
    data.hasSession = true; data.artworkBytes.assign(SmtcMaximumArtworkBytes + 1, 0);
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, {}, &cache) &&
        !media.artwork && media.artworkBytes.empty() && !media.artworkError.empty() && media.title == L"Second song",
        "oversized thumbnails fail separately from usable media text");
    data.artworkBytes = {1, 2, 3, 4};
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), true, reader, {}, &cache) &&
        !media.artwork && !media.artworkError.empty() && media.title == L"Second song",
        "invalid image decode produces a text fallback with an artwork diagnostic");
    auto wide = cache.Store(BitmapBytes(2048, 1), error, smtc_detail::Clock::now() + std::chrono::seconds(2));
    Check(wide && wide->width == SmtcCachedArtworkDimension && wide->height == 1,
        "large permitted images are resized to a bounded cache bitmap while preserving aspect ratio");
    auto enormous = BitmapBytes();
    BITMAPINFOHEADER info{}; memcpy(&info, enormous.data() + 14, sizeof(info));
    info.biWidth = 8193; memcpy(enormous.data() + 14, &info, sizeof(info));
    Check(!cache.Store(enormous, error, smtc_detail::Clock::now() + std::chrono::seconds(2)) && !error.empty(),
        "oversized decoded dimensions are rejected before copying image pixels");
    Check(!cache.Store(BitmapBytes(), error, smtc_detail::Clock::now() - std::chrono::milliseconds(1)) && !error.empty(),
        "an expired deadline starts no image decode or cache write");
    Check(!cache.Store(BitmapBytes(), error, smtc_detail::Clock::now() + std::chrono::seconds(2), [] { return true; }),
        "canceled artwork storage starts no decode or cache write");
    other.reset(); wide.reset();
    first.reset();
    Check(GetFileAttributesW(firstPath.c_str()) != INVALID_FILE_ATTRIBUTES,
        "a second immutable snapshot lease still keeps old artwork readable");
    same.reset();
    Check(GetFileAttributesW(firstPath.c_str()) == INVALID_FILE_ATTRIBUTES,
        "releasing the final snapshot removes its obsolete artwork file");
}

struct FakeOperation {
    mutable bool canceled = false, waited = false;
    mutable std::chrono::milliseconds wait{0};
    winrt::Windows::Foundation::AsyncStatus status;
    bool completes = false;
    auto Status() const { return status; }
    auto wait_for(std::chrono::milliseconds duration) const {
        waited = true;
        wait = duration;
        return completes ? winrt::Windows::Foundation::AsyncStatus::Completed : status;
    }
    void Cancel() const { canceled = true; }
    int GetResults() const {
        if (status == winrt::Windows::Foundation::AsyncStatus::Error)
            throw winrt::hresult_error(E_ACCESSDENIED);
        return 42;
    }
};

void TestDeadline()
{
    using namespace aip::content::smtc_detail;
    using winrt::Windows::Foundation::AsyncStatus;
    FakeOperation pending{ false, false, std::chrono::milliseconds(0), AsyncStatus::Started };
    bool timedOut = false;
    try { Await(pending, Clock::now() + std::chrono::milliseconds(250)); }
    catch (const winrt::hresult_error& ex) { timedOut = ex.code() == HRESULT_FROM_WIN32(ERROR_TIMEOUT); }
    Check(timedOut && pending.canceled && pending.waited && pending.wait.count() <= 250,
        "pending SMTC operations use their remaining deadline and request cancellation");

    FakeOperation expired{ false, false, std::chrono::milliseconds(0), AsyncStatus::Started };
    try { Await(expired, Clock::now() - std::chrono::milliseconds(1)); } catch (...) {}
    Check(expired.canceled && !expired.waited, "an expired SMTC deadline starts no additional wait");

    FakeOperation completed{ false, false, std::chrono::milliseconds(0), AsyncStatus::Completed };
    Check(Await(completed, Clock::now()) == 42 && !completed.waited && !completed.canceled,
        "completed SMTC operations read results without an unbounded get or second completion handler");

    FakeOperation denied{ false, false, std::chrono::milliseconds(0), AsyncStatus::Error };
    bool propagated = false;
    try { Await(denied, Clock::now()); }
    catch (const winrt::hresult_error& ex) { propagated = ex.code() == E_ACCESSDENIED; }
    Check(propagated && !denied.waited, "SMTC terminal errors retain their HRESULT");
}

void TestContract()
{
    using namespace aip::content;
    Text text{ L"stale", L"stale", L"stale" };
    std::wstring error;
    Check(!ReadSmtcContent(text, error, std::chrono::milliseconds(0)) &&
        text.primary.empty() && text.secondary.empty() && text.badge.empty() && !error.empty(),
        "invalid SMTC timeout clears the previous snapshot and reports a useful error");
    Check(!ReadSmtcContent(text, error, std::chrono::milliseconds(60001)),
        "SMTC rejects unbounded timeout requests");
    Check(smtc_detail::Clean(std::wstring(L"\0 artist ", 9)) == L"artist" &&
        smtc_detail::Clean(std::wstring(MaxText - 1, L'x') + L"\xD83D\xDE00").size() == MaxText - 1,
        "SMTC text is visible, bounded, and preserves complete Unicode pairs");
    Check(smtc_detail::SourceName(L"Vendor.Player_123!App") == L"Player" &&
        smtc_detail::SourceName(L"") == L"SMTC",
        "SMTC app identifiers become readable source names");

    HRESULT initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    Check(SUCCEEDED(initialized), "SMTC test initializes an STA fixture");
    if (SUCCEEDED(initialized))
    {
        Check(!ReadSmtcContent(text, error) && error.find(L"multithreaded") != std::wstring::npos,
            "SMTC rejects UI-thread STA blocking before requesting media data");
        RoUninitialize();
    }
}

struct FakeManagerState {
    std::vector<winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession> sessions;
    std::atomic<int> created{0}, released{0}, reads{0}, invalidRelease{0}, acquisitionAttempts{0};
    std::atomic<HRESULT> readFailure{S_OK}, acquisitionFailure{S_OK};
    DWORD thread = 0;
    std::mutex eventMutex;
    winrt::Windows::Foundation::TypedEventHandler<aip::content::SmtcManager,
        winrt::Windows::Media::Control::CurrentSessionChangedEventArgs> handler{nullptr};
    std::atomic<int> subscriptions{0}, revocations{0};
    std::atomic<bool> rejectEvents{false};
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession current{nullptr};
};
struct FakeSmtcManager : winrt::implements<FakeSmtcManager,
    winrt::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionManager> {
    std::shared_ptr<FakeManagerState> state;
    explicit FakeSmtcManager(std::shared_ptr<FakeManagerState> value) : state(std::move(value)) { ++state->created; }
    ~FakeSmtcManager() {
        APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
        if (state->thread != GetCurrentThreadId() || FAILED(CoGetApartmentType(&type, &qualifier)) || type != APTTYPE_MTA)
            ++state->invalidRelease;
        ++state->released;
    }
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession GetCurrentSession() {
        ++state->reads;
        winrt::check_hresult(state->readFailure.exchange(S_OK));
        std::lock_guard<std::mutex> lock(state->eventMutex); return state->current;
    }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession> GetSessions() {
        return winrt::single_threaded_vector<winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession>(std::vector<winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession>(state->sessions)).GetView();
    }
    winrt::event_token CurrentSessionChanged(const winrt::Windows::Foundation::TypedEventHandler<aip::content::SmtcManager,
        winrt::Windows::Media::Control::CurrentSessionChangedEventArgs>& handler) {
        if (state->rejectEvents) throw winrt::hresult_error(E_NOTIMPL);
        std::lock_guard<std::mutex> lock(state->eventMutex); state->handler = handler; ++state->subscriptions; return {1};
    }
    void CurrentSessionChanged(winrt::event_token) noexcept {
        std::lock_guard<std::mutex> lock(state->eventMutex); state->handler = nullptr; ++state->revocations;
    }
    winrt::event_token SessionsChanged(const winrt::Windows::Foundation::TypedEventHandler<aip::content::SmtcManager,
        winrt::Windows::Media::Control::SessionsChangedEventArgs>&) { return {}; }
    void SessionsChanged(winrt::event_token) noexcept {}
};

using FakeSession = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using FakeMediaHandler = winrt::Windows::Foundation::TypedEventHandler<FakeSession, winrt::Windows::Media::Control::MediaPropertiesChangedEventArgs>;
using FakePlaybackHandler = winrt::Windows::Foundation::TypedEventHandler<FakeSession, winrt::Windows::Media::Control::PlaybackInfoChangedEventArgs>;
struct FakeSessionState {
    std::wstring appId=L"Fixture.Player";
    std::mutex gate;
    FakeMediaHandler media{nullptr}; FakePlaybackHandler playback{nullptr};
    std::atomic<int> propertyReads{0}, mediaAdds{0}, playbackAdds{0}, mediaRemoves{0}, playbackRemoves{0}, timelineAdds{0};
};
struct FakeEventSession : winrt::implements<FakeEventSession,
    winrt::Windows::Media::Control::IGlobalSystemMediaTransportControlsSession> {
    std::shared_ptr<FakeSessionState> state;
    explicit FakeEventSession(std::shared_ptr<FakeSessionState> value) : state(std::move(value)) {}
    winrt::hstring SourceAppUserModelId() { return winrt::hstring(state->appId); }
    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties> TryGetMediaPropertiesAsync() {
        ++state->propertyReads;
        // The event-binding fixture deliberately has no system property API.
        // Snapshot/text/artwork behavior is covered by the injected data tests.
        throw winrt::hresult_error(E_ACCESSDENIED);
    }
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionTimelineProperties GetTimelineProperties() { return nullptr; }
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackInfo GetPlaybackInfo() { return nullptr; }
#define FAKE_UNUSED_ACTION(name) winrt::Windows::Foundation::IAsyncOperation<bool> name() { return nullptr; }
    FAKE_UNUSED_ACTION(TryPlayAsync) FAKE_UNUSED_ACTION(TryPauseAsync) FAKE_UNUSED_ACTION(TryStopAsync)
    FAKE_UNUSED_ACTION(TryRecordAsync) FAKE_UNUSED_ACTION(TryFastForwardAsync) FAKE_UNUSED_ACTION(TryRewindAsync)
    FAKE_UNUSED_ACTION(TrySkipNextAsync) FAKE_UNUSED_ACTION(TrySkipPreviousAsync)
    FAKE_UNUSED_ACTION(TryChangeChannelUpAsync) FAKE_UNUSED_ACTION(TryChangeChannelDownAsync) FAKE_UNUSED_ACTION(TryTogglePlayPauseAsync)
#undef FAKE_UNUSED_ACTION
    winrt::Windows::Foundation::IAsyncOperation<bool> TryChangeAutoRepeatModeAsync(winrt::Windows::Media::MediaPlaybackAutoRepeatMode) { return nullptr; }
    winrt::Windows::Foundation::IAsyncOperation<bool> TryChangePlaybackRateAsync(double) { return nullptr; }
    winrt::Windows::Foundation::IAsyncOperation<bool> TryChangeShuffleActiveAsync(bool) { return nullptr; }
    winrt::Windows::Foundation::IAsyncOperation<bool> TryChangePlaybackPositionAsync(std::int64_t) { return nullptr; }
    winrt::event_token TimelinePropertiesChanged(const winrt::Windows::Foundation::TypedEventHandler<FakeSession,
        winrt::Windows::Media::Control::TimelinePropertiesChangedEventArgs>&) { ++state->timelineAdds; return {3}; }
    void TimelinePropertiesChanged(winrt::event_token) noexcept {}
    winrt::event_token MediaPropertiesChanged(const FakeMediaHandler& callback) {
        std::lock_guard<std::mutex> lock(state->gate); state->media = callback; ++state->mediaAdds; return {1};
    }
    void MediaPropertiesChanged(winrt::event_token) noexcept {
        std::lock_guard<std::mutex> lock(state->gate); state->media = nullptr; ++state->mediaRemoves;
    }
    winrt::event_token PlaybackInfoChanged(const FakePlaybackHandler& callback) {
        std::lock_guard<std::mutex> lock(state->gate); state->playback = callback; ++state->playbackAdds; return {2};
    }
    void PlaybackInfoChanged(winrt::event_token) noexcept {
        std::lock_guard<std::mutex> lock(state->gate); state->playback = nullptr; ++state->playbackRemoves;
    }
};

void TestSelectedSessionEvents()
{
    using namespace aip::content;
    using Source = AsyncSource<SmtcMediaSnapshot>;
    auto manager = std::make_shared<FakeManagerState>();
    auto first = std::make_shared<FakeSessionState>(), second = std::make_shared<FakeSessionState>();
    manager->current = winrt::make<FakeEventSession>(first).as<FakeSession>();
    std::atomic<int> wakes{0};
    auto reader = std::make_shared<SmtcWorkerSession>([manager](auto, const auto&) -> SmtcManager {
        manager->thread = GetCurrentThreadId(); return winrt::make<FakeSmtcManager>(manager).as<SmtcManager>();
    });
    Source source([reader] { reader->Reset(); });
    const auto fetch = [reader, &wakes](const auto& cancel) {
        SmtcMediaSnapshot snapshot; std::wstring error;
        ReadSmtcMedia(snapshot, error, std::chrono::milliseconds(500), false,
            [reader, &wakes](auto deadline, bool art, const auto& stop) { return reader->Read(deadline, art, stop, true, [&wakes] { ++wakes; }); }, cancel);
        return snapshot;
    };
    source.Request(L"selected", std::chrono::hours(1), fetch);
    Check(source.Wait(L"selected", std::chrono::seconds(2)) && reader->ReportEventsSupported() &&
        first->mediaAdds == 1 && first->playbackAdds == 1 && first->timelineAdds == 0,
        "selected session subscribes metadata and playback while avoiding timeline-progress events");
    FakeMediaHandler oldMedia{nullptr}; FakePlaybackHandler oldPlayback{nullptr};
    { std::lock_guard<std::mutex> lock(first->gate); oldMedia = first->media; oldPlayback = first->playback; }
    const int before = first->propertyReads;
    oldMedia(nullptr, nullptr); oldPlayback(nullptr, nullptr);
    Check(wakes == 1 && first->propertyReads == before && reader->ConsumeDirty(),
        "metadata and playback callbacks coalesce without reading session properties");
    { std::lock_guard<std::mutex> lock(manager->eventMutex); manager->current = winrt::make<FakeEventSession>(second).as<FakeSession>(); }
    source.Request(L"selected", std::chrono::hours(1), fetch, {}, true);
    Check(source.Wait(L"selected", std::chrono::seconds(2)) && first->mediaRemoves == 1 && first->playbackRemoves == 1 &&
        second->mediaAdds == 1 && second->playbackAdds == 1 && second->timelineAdds == 0,
        "current-session replacement revokes old tokens before registering the replacement");
    oldMedia(nullptr, nullptr); oldPlayback(nullptr, nullptr);
    Check(wakes == 1 && !reader->ConsumeDirty(), "late callbacks from the prior selected session are permanently closed");
    source.Stop();
    Check(second->mediaRemoves == 1 && second->playbackRemoves == 1 && manager->invalidRelease == 0,
        "Stop revokes selected-session tokens on its original worker before apartment teardown");
}

void TestEventEpochCoalescing()
{
    using namespace aip::content;
    std::atomic<int> wakes{0};
    auto state = std::make_shared<SmtcEventState>(); state->wake = [&] { ++wakes; };
    const std::weak_ptr<SmtcEventState> weak = state;
    for (int n = 0; n < 10000; ++n) SmtcEventState::Notify(weak);
    Check(wakes == 1 && state->dirty.exchange(false), "ten thousand duplicate events coalesce into one dirty wake without any media read");
    auto epoch = std::make_shared<SmtcSessionEventEpoch>(); epoch->destination = state;
    const std::weak_ptr<SmtcSessionEventEpoch> oldSession = epoch;
    SmtcSessionEventEpoch::Notify(oldSession);
    Check(wakes == 2 && state->dirty.exchange(false), "session metadata/playback callbacks feed the same coalesced dirty state");
    epoch->Close(); SmtcSessionEventEpoch::Notify(oldSession);
    Check(wakes == 2 && !state->dirty, "a callback queued by a replaced session cannot wake the new session");
    std::atomic<bool> ready{false}, release{false};
    state->wake = [&] {
        ready = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!release && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ++wakes;
    };
    std::thread callback([&] { SmtcEventState::Notify(weak); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!ready && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::atomic<bool> closed{false}, closeEntered{false};
    std::thread closing([&] { closeEntered = true; state->Close(); closed = true; });
    const auto closeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!closeEntered && std::chrono::steady_clock::now() < closeDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool waited = !closed.load();
    release = true; callback.join(); closing.join();
    const auto before = wakes.load();
    for (int n = 0; n < 10000; ++n) SmtcEventState::Notify(weak);
    Check(ready && closeEntered && waited && closed && wakes == before && !state->dirty,
        "closing a callback epoch waits for its old wake and permanently blocks late notifications");
}

void TestRetainedManagerEvents()
{
    using namespace aip::content;
    using Source = AsyncSource<SmtcMediaSnapshot>;
    const auto manager = std::make_shared<FakeManagerState>();
    std::atomic<int> wakes{0};
    const auto session = std::make_shared<SmtcWorkerSession>([manager](auto, const auto&) -> SmtcManager {
        manager->thread = GetCurrentThreadId();
        return winrt::make<FakeSmtcManager>(manager).as<SmtcManager>();
    });
    Source source([session] { session->Reset(); });
    const auto fetch = [session, &wakes](const Source::Cancel& canceled) {
        SmtcMediaSnapshot media; std::wstring error;
        ReadSmtcMedia(media, error, std::chrono::milliseconds(500), false,
            [session, &wakes](auto deadline, bool artwork, const auto& cancel) {
                return session->Read(deadline, artwork, cancel, true, [&wakes] { ++wakes; });
            }, canceled);
        return media;
    };
    Check(source.Request(L"events", std::chrono::hours(1), fetch) && source.Wait(L"events", std::chrono::seconds(2)) &&
        session->ReportEventsSupported() && manager->subscriptions == 1, "idle media retains one worker-owned current-session subscription");
    decltype(manager->handler) oldHandler{nullptr};
    { std::lock_guard<std::mutex> lock(manager->eventMutex); oldHandler = manager->handler; }
    const int reads = manager->reads;
    for (int n = 0; n < 10000; ++n) oldHandler(nullptr, nullptr);
    Check(wakes == 1 && manager->reads == reads && session->ConsumeDirty() && !session->ConsumeDirty(),
        "current-session notifications only mark dirty and wake; callbacks never query the COM manager");
    source.Request(L"events", std::chrono::hours(1), fetch, {}, true);
    Check(source.Wait(L"events", std::chrono::seconds(2)) && manager->subscriptions == 1,
        "ordinary snapshots reuse existing event tokens instead of subscribing again");
    source.Stop();
    oldHandler(nullptr, nullptr);
    Check(manager->revocations == 1 && !session->ReportEventsSupported() && !session->ConsumeDirty() && manager->invalidRelease == 0 && wakes == 1,
        "worker Stop closes the epoch, revokes the manager and releases proxies before its MTA ends");
    auto unsupported = std::make_shared<FakeManagerState>(); unsupported->rejectEvents = true;
    auto unavailable = std::make_shared<SmtcWorkerSession>([unsupported](auto, const auto&) -> SmtcManager {
        unsupported->thread = GetCurrentThreadId(); return winrt::make<FakeSmtcManager>(unsupported).as<SmtcManager>();
    });
    Source fallback([unavailable] { unavailable->Reset(); });
    fallback.Request(L"unsupported", std::chrono::hours(1), [unavailable](const auto& cancel) {
        SmtcMediaSnapshot result; std::wstring error;
        ReadSmtcMedia(result, error, std::chrono::milliseconds(500), false,
            [unavailable](auto deadline, bool artwork, const auto& stop) { return unavailable->Read(deadline, artwork, stop, true); }, cancel);
        return result;
    });
    Check(fallback.Wait(L"unsupported", std::chrono::seconds(2)) && !unavailable->ReportEventsSupported(),
        "unsupported event interfaces retain valid polling snapshots without claiming event coverage");
    fallback.Stop();
}

void TestPersistentWorkerManager()
{
    using namespace aip::content;
    using Source = AsyncSource<SmtcMediaSnapshot>;
    const auto state = std::make_shared<FakeManagerState>();
    const auto session = std::make_shared<SmtcWorkerSession>([state](auto, const auto&) -> SmtcManager {
        state->thread = GetCurrentThreadId();
        ++state->acquisitionAttempts;
        winrt::check_hresult(state->acquisitionFailure.load());
        return winrt::make<FakeSmtcManager>(state).as<SmtcManager>();
    });
    Source source([session] { session->Reset(); });
    const auto fetch = [session](const Source::Cancel& canceled) {
        SmtcMediaSnapshot media;
        std::wstring error;
        const bool ok = ReadSmtcMedia(media, error, std::chrono::milliseconds(500), false,
            [session](auto deadline, bool artwork, const auto& cancel) { return session->Read(deadline, artwork, cancel); }, canceled);
        if (!ok) media.text = {L"Error", error, L""};
        return media;
    };
    Check(state->created == 0 && state->acquisitionAttempts == 0, "constructing a persistent SMTC reader never activates system media");
    Check(source.Request(L"profile-one", std::chrono::hours(1), fetch) && source.Wait(L"profile-one", std::chrono::seconds(2)),
        "the persistent reader creates its fake manager lazily on the source worker");
    for (int n = 0; n < 100; ++n) {
        source.Request(L"profile-one", std::chrono::hours(1), fetch, {}, true);
        Check(source.Wait(L"profile-one", std::chrono::seconds(2)), "repeated media snapshots finish under the inert deadline");
    }
    Check(state->created == 1 && state->acquisitionAttempts == 1 && state->reads == 101 && state->released == 0,
        "101 media refreshes retain one manager and one owning apartment");
    state->readFailure = RPC_E_DISCONNECTED;
    source.Request(L"profile-one", std::chrono::hours(1), fetch, {}, true);
    Check(source.Wait(L"profile-one", std::chrono::seconds(2)) && state->released == 1 && state->acquisitionAttempts == 1,
        "a disconnected manager is released without an inline reconnect loop");
    for (int n = 0; n < 1000; ++n) source.Request(L"profile-one", std::chrono::hours(1), fetch);
    Check(state->acquisitionAttempts == 1, "poll wakes after disconnect cannot bypass the configured refresh interval");
    source.Request(L"profile-one", std::chrono::hours(1), fetch, {}, true);
    Check(source.Wait(L"profile-one", std::chrono::seconds(2)) && state->created == 2,
        "a later explicit refresh reconnects once after the manager failure");
    source.Request(L"profile-two", std::chrono::hours(1), fetch);
    Check(source.Wait(L"profile-two", std::chrono::seconds(2)) && state->created == 3 && state->released == 2,
        "changing profiles releases the old manager before obtaining the new one");
    source.CancelPending();
    const auto cleanupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (state->released != 3 && std::chrono::steady_clock::now() < cleanupDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(state->released == 3 && state->invalidRelease == 0, "disabling media releases its manager on the original MTA worker");
    state->acquisitionFailure = E_ACCESSDENIED;
    source.Request(L"denied", std::chrono::hours(1), fetch);
    Check(source.Wait(L"denied", std::chrono::seconds(2)), "an acquisition error completes as an ordinary media error");
    const int attempts = state->acquisitionAttempts;
    for (int n = 0; n < 1000; ++n) source.Request(L"denied", std::chrono::hours(1), fetch);
    Check(state->acquisitionAttempts == attempts, "acquisition failures remain bounded by the source cadence");
    state->acquisitionFailure = S_OK;
    source.Request(L"denied", std::chrono::hours(1), fetch, {}, true);
    Check(source.Wait(L"denied", std::chrono::seconds(2)) && state->created == 4,
        "reenabling a repaired reader creates exactly one replacement manager");
    source.Stop();
    Check(state->created == state->released && state->invalidRelease == 0,
        "source Stop releases all manager references before the worker uninitializes its MTA");
}

bool ReadOnlySmoke()
{
    aip::content::Text text;
    std::wstring error;
    const auto start = std::chrono::steady_clock::now();
    const bool ok = aip::content::ReadSmtcContent(text, error, std::chrono::milliseconds(400));
    if (ok) ++successfulReads;
    else ++unavailableReads;
    const auto elapsed = std::chrono::steady_clock::now() - start;
    // No playback, registration, file, tray, or configuration mutation occurs.
    // Do not print the user's song title/artist in a test log.
    const bool contract = ok
        ? !text.primary.empty() && !text.secondary.empty() && text.badge.empty() && error.empty()
        : text.primary.empty() && text.secondary.empty() && text.badge.empty() && !error.empty();
    return contract && elapsed < std::chrono::seconds(5) &&
        text.primary.size() <= aip::content::MaxText &&
        text.secondary.size() <= aip::content::MaxText &&
        text.badge.size() <= aip::content::MaxText;
}

void TestSystemReadOnly()
{
    Check(ReadOnlySmoke(), "read-only SMTC smoke returns media, idle, or a useful bounded error");
    DWORD handlesBefore = 0, handlesAfter = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
    std::atomic<int> errors{0};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker)
        workers.emplace_back([&errors] {
            for (int iteration = 0; iteration < 5; ++iteration)
                if (!ReadOnlySmoke()) ++errors;
        });
    for (auto& worker : workers) worker.join();
    GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
    Check(errors == 0, "twenty concurrent SMTC snapshot reads leave each result isolated");
    std::cout << "SMTC successful/idle snapshots: " << successfulReads.load() <<
        "; unavailable/error snapshots: " << unavailableReads.load() << '\n';
    Check(handlesAfter <= handlesBefore + 128,
        "SMTC concurrent smoke has no runaway handle growth (allows WinRT cache warmup)");
}
}

static void TestExplicitPresentationMetadata()
{
    using namespace aip::content;
    SmtcSessionData data;
    data.hasSession = true; data.title = L"Fixture song"; data.appId = L"Fixture.Player";
    data.status = L"localized metadata"; data.playbackState = MediaPlaybackState::Paused;
    data.repeat = MediaRepeatMode::Track; data.shuffle = MediaToggleState::On;
    data.appIconBytes = BitmapBytes(3, 2, 96);
    const auto reader = [&](auto, bool, const auto&) { return data; };
    SmtcArtworkCache cache(L"SmtcIconFixture", L"C:\\fixture-only\\media-icons.ini");
    SmtcMediaSnapshot media; std::wstring error;
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), false, reader, {}, &cache, true) &&
        media.playbackState == MediaPlaybackState::Paused && media.repeat == MediaRepeatMode::Track &&
        media.shuffle == MediaToggleState::On && media.appIcon && !media.artwork,
        "explicit current playback/repeat/shuffle and an application icon share one injected media snapshot");
    auto icon = media.appIcon;
    std::vector<BYTE> saved(data.appIconBytes.size()); DWORD copied = 0;
    const auto file = icon ? CreateFileW(icon->path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) : INVALID_HANDLE_VALUE;
    const bool identical = file != INVALID_HANDLE_VALUE && ReadFile(file, saved.data(), static_cast<DWORD>(saved.size()), &copied, nullptr) &&
        copied == saved.size() && saved == data.appIconBytes;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    Check(identical, "validated icon storage retains its original encoding instead of discarding source alpha");
    data.playbackState = MediaPlaybackState::Unknown; data.status = L"Not Playing";
    data.repeat = MediaRepeatMode::Unknown; data.shuffle = MediaToggleState::Unknown;
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), false, reader, {}, &cache) &&
        media.playbackState == MediaPlaybackState::Unknown && media.repeat == MediaRepeatMode::Unknown &&
        media.shuffle == MediaToggleState::Unknown && !media.appIcon,
        "missing metadata stays unknown and disabled app-icon mode performs no icon cache work");
    data.appIconBytes.assign(1024 * 1024 + 1, 0);
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), false, reader, {}, &cache, true) &&
        !media.appIcon && !media.appIconError.empty() && media.title == L"Fixture song",
        "oversized application icons fail independently of usable media text");
    data.appIconBytes = BitmapBytes(257, 1);
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), false, reader, {}, &cache, true) &&
        !media.appIcon && !media.appIconError.empty(), "large raw application logos are rejected before the renderer can decode them");
    data.appIconBytes = {1, 2, 3};
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), false, reader, {}, &cache, true) &&
        !media.appIcon && !media.appIconError.empty(), "invalid application icons leave an honest status fallback");
    data.hasSession = false;
    Check(ReadSmtcMedia(media, error, std::chrono::milliseconds(2000), false, reader, {}, &cache, true) &&
        !media.hasSession && media.playbackState == MediaPlaybackState::Stopped && !media.appIcon &&
        media.repeat == MediaRepeatMode::Unknown, "an absent session clears old icon/metadata and reports no active playback");
}

void TestApplicationSelection()
{
    using namespace aip::content;smtc_detail::Apartment apartment;
    auto state=std::make_shared<FakeManagerState>();state->thread=GetCurrentThreadId();
    auto a=std::make_shared<FakeSessionState>(),b=std::make_shared<FakeSessionState>(),a2=std::make_shared<FakeSessionState>();a->appId=a2->appId=L"A";b->appId=L"B";
    auto first=winrt::make<FakeEventSession>(a).as<FakeSession>(),second=winrt::make<FakeEventSession>(b).as<FakeSession>(),third=winrt::make<FakeEventSession>(a2).as<FakeSession>();
    state->sessions={first,second,third};state->current=second;
    auto manager=winrt::make<FakeSmtcManager>(state).as<SmtcManager>();std::vector<std::wstring> roster;
    Check(winrt::get_abi(SelectNativeSmtcSession(manager,{},&roster))==winrt::get_abi(second)&&roster==std::vector<std::wstring>({L"A",L"B"}),"real native selector retains Windows-current and catalogs distinct applications without reading track properties");
    SmtcSelection select;select.mode=L"Application";select.appId=L"A";
    Check(winrt::get_abi(SelectNativeSmtcSession(manager,select))==winrt::get_abi(first),"chosen application does not follow another application's current session");
    state->current=third;Check(winrt::get_abi(SelectNativeSmtcSession(manager,select))==winrt::get_abi(third),"default occurrence prefers Windows-current when it belongs to the chosen application");
    select.instance=1;Check(winrt::get_abi(SelectNativeSmtcSession(manager,select))==winrt::get_abi(first),"explicit first occurrence bypasses the current-session preference");
    select.instance=2;Check(winrt::get_abi(SelectNativeSmtcSession(manager,select))==winrt::get_abi(third),"second session from the same application can be selected independently");
    select.instance=3;Check(SelectNativeSmtcSession(manager,select)==nullptr,"missing selected occurrence never controls a different session");
    select.instance=0;select.appId=L"missing";Check(SelectNativeSmtcSession(manager,select)==nullptr,"missing selected application never falls through to Windows-current");
    std::wstring value=L"65",error;Check(!NormalizeSmtcSelectionSetting(L"sessioninstance",value,error),"session occurrence is bounded");
    value=L"bad\napp";Check(!NormalizeSmtcSelectionSetting(L"sessionappid",value,error),"application identifiers reject multiline content");
    state->current=nullptr;state->sessions.clear();manager=nullptr;
}
int main(int argc, char** argv)
{
    bool allowSystemMedia = false;
    for (int argument = 1; argument < argc; ++argument) {
        if (std::string(argv[argument]) == "--allow-system-media") allowSystemMedia = true;
        else { std::cerr << "Unknown test argument\n"; return 2; }
    }
    TestDeadline();
    TestContract();
    TestInjectedMediaAndArtwork();
    TestExplicitPresentationMetadata();
    TestPersistentWorkerManager();
    TestEventEpochCoalescing();
    TestRetainedManagerEvents();
    TestSelectedSessionEvents();
    TestApplicationSelection();
    if (allowSystemMedia) TestSystemReadOnly();
    std::cout << checks << " checks; " << failures << " failures; system media reads " <<
        (allowSystemMedia ? "explicitly enabled" : "disabled") << ".\n";
    std::cout << (failures == 0 ? "SMTC source tests passed.\n" : "SMTC source tests failed.\n");
    return failures == 0 ? 0 : 1;
}
