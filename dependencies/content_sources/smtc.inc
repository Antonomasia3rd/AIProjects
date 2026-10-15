#ifndef AIP_CONTENT_SOURCES_SMTC_INC
#define AIP_CONTENT_SOURCES_SMTC_INC

// Read-only SMTC media provider. Call on a worker, never the host's UI thread.
// Windows 10 1809+ exposes this API; older systems return a provider error.
// Link windowsapp.lib and runtimeobject.lib. No package registration required.
#include "../content_engine.h"
#include <windows.h>
#include <roapi.h>
#include <chrono>
#include <cwchar>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <atomic>
#include "../winrt_activation.h"
#include "smtc_artwork.inc"

namespace aip { namespace content {
namespace smtc_detail {
using Clock = std::chrono::steady_clock;

class Apartment {
    HRESULT result_;
public:
    Apartment() : result_(RoInitialize(RO_INIT_MULTITHREADED)) {}
    ~Apartment() { if (SUCCEEDED(result_)) RoUninitialize(); }
    HRESULT Result() const { return result_; }
    Apartment(const Apartment&) = delete;
    Apartment& operator=(const Apartment&) = delete;
};

template<class Operation>
inline auto Await(Operation const& operation, Clock::time_point deadline)
{
    using winrt::Windows::Foundation::AsyncStatus;
    const auto now = Clock::now();
    auto status = operation.Status();
    if (status == AsyncStatus::Started && now < deadline)
    {
        // Only one completion handler is attached to this operation. Calling
        // wait_for repeatedly on a WinRT operation would attach it twice.
        status = operation.wait_for(std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now));
    }
    if (status == AsyncStatus::Started)
    {
        // Cancellation is cooperative. The WinRT operation owns its callback
        // after this function returns; no callback captures caller stack data.
        try { operation.Cancel(); } catch (...) {}
        throw winrt::hresult_error(
            HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"Timed out retrieving media information.");
    }
    // GetResults does not wait. It also propagates terminal error/cancel states.
    return operation.GetResults();
}

inline std::wstring Clean(std::wstring value)
{
    std::replace(value.begin(), value.end(), L'\0', L' ');
    value = Trim(std::move(value));
    if (value.size() > MaxText)
    {
        std::size_t size = MaxText;
        if (value[size - 1] >= 0xD800 && value[size - 1] <= 0xDBFF &&
            value[size] >= 0xDC00 && value[size] <= 0xDFFF)
            --size;
        value.resize(size);
    }
    return value;
}

inline std::wstring SourceName(std::wstring source)
{
    source = Clean(std::move(source));
    const auto bang = source.find(L'!');
    if (bang != std::wstring::npos) source.resize(bang);
    const auto suffix = source.find(L'_');
    if (suffix != std::wstring::npos) source.resize(suffix);
    const auto dot = source.find_last_of(L'.');
    if (dot != std::wstring::npos && dot + 1 < source.size()) source.erase(0, dot + 1);
    return source.empty() ? L"SMTC" : source;
}

inline const wchar_t* PlaybackStatus(
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus status)
{
    using Status = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
    switch (status)
    {
    case Status::Closed: return L"Closed";
    case Status::Opened: return L"Opened";
    case Status::Changing: return L"Changing";
    case Status::Stopped: return L"Stopped";
    case Status::Playing: return L"Playing";
    case Status::Paused: return L"Paused";
    default: return L"Unknown";
    }
}
} // namespace smtc_detail

struct SmtcSessionData {
    bool hasSession = false;
    std::wstring title, artist, albumArtist, subtitle, appId, status;
    std::vector<std::uint8_t> artworkBytes;
    std::wstring artworkError;
    MediaPlaybackState playbackState = MediaPlaybackState::Unknown;
    MediaRepeatMode repeat = MediaRepeatMode::Unknown;
    MediaToggleState shuffle = MediaToggleState::Unknown;
    std::vector<std::uint8_t> appIconBytes;
    std::wstring appIconError;
};
struct SmtcMediaSnapshot {
    bool hasSession = false;
    Text text;
    std::wstring title, artist, source, status, artworkError;
    std::vector<std::uint8_t> artworkBytes;
    std::shared_ptr<const SmtcArtworkFile> artwork;
    MediaPlaybackState playbackState = MediaPlaybackState::Unknown;
    MediaRepeatMode repeat = MediaRepeatMode::Unknown;
    MediaToggleState shuffle = MediaToggleState::Unknown;
    std::wstring appIconError;
    std::shared_ptr<const SmtcArtworkFile> appIcon;
};
// Injection replaces the complete session/property read, so fixtures never
// request real system media sessions. Native text and thumbnail use one session,
// one property object, and the same total operation deadline.
using SmtcSessionReader = std::function<SmtcSessionData(
    smtc_detail::Clock::time_point, bool, const SmtcCancel&)>;

using SmtcManager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using SmtcManagerFactory = std::function<SmtcManager(smtc_detail::Clock::time_point, const SmtcCancel&)>;

inline SmtcManager AcquireNativeSmtcManager(smtc_detail::Clock::time_point deadline, const SmtcCancel& canceled)
{
    if (canceled && canceled()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    const auto factory = aip::AcquireUncachedActivationFactory<SmtcManager,
        winrt::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionManagerStatics>();
    auto manager = smtc_detail::Await(factory.RequestAsync(), deadline);
    if (canceled && canceled()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    return manager;
}

inline SmtcSessionData ReadSmtcSessionProperties(
    const winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession& session,
    smtc_detail::Clock::time_point deadline, bool includeArtwork, const SmtcCancel& canceled)
{
    if (canceled && canceled()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    SmtcSessionData data;
    if (session == nullptr) return data;
    const auto properties = smtc_detail::Await(session.TryGetMediaPropertiesAsync(), deadline);
    data.hasSession = true;
    data.title = std::wstring(properties.Title());
    data.artist = std::wstring(properties.Artist());
    data.albumArtist = std::wstring(properties.AlbumArtist());
    data.subtitle = std::wstring(properties.Subtitle());
    data.appId = std::wstring(session.SourceAppUserModelId());
    const auto playback = session.GetPlaybackInfo();
    data.status = playback == nullptr ? L"" : smtc_detail::PlaybackStatus(playback.PlaybackStatus());
    if (playback != nullptr) {
        using Status = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
        switch (playback.PlaybackStatus()) {
        case Status::Playing: data.playbackState = MediaPlaybackState::Playing; break;
        case Status::Paused: data.playbackState = MediaPlaybackState::Paused; break;
        case Status::Stopped: case Status::Closed: data.playbackState = MediaPlaybackState::Stopped; break;
        default: break;
        }
        // Nullable playback metadata is optional. A player omitting it must
        // not be represented as repeat/shuffle off.
        try {
            if (const auto repeat = playback.AutoRepeatMode()) {
                using Mode = winrt::Windows::Media::MediaPlaybackAutoRepeatMode;
                switch (repeat.Value()) {
                case Mode::None: data.repeat = MediaRepeatMode::Off; break;
                case Mode::List: data.repeat = MediaRepeatMode::List; break;
                case Mode::Track: data.repeat = MediaRepeatMode::Track; break;
                default: break;
                }
            }
        } catch (const winrt::hresult_error&) {}
        try { if (const auto shuffle = playback.IsShuffleActive()) data.shuffle = shuffle.Value() ? MediaToggleState::On : MediaToggleState::Off; }
        catch (const winrt::hresult_error&) {}
    }
    if (includeArtwork && properties.Thumbnail() != nullptr) {
        try {
            if (canceled && canceled()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
            const auto stream = smtc_detail::Await(properties.Thumbnail().OpenReadAsync(), deadline);
            const auto size = stream.Size();
            if (size > SmtcMaximumArtworkBytes) data.artworkError = L"The media artwork exceeds the encoded byte limit.";
            else if (size != 0) {
                using namespace winrt::Windows::Storage::Streams;
                const auto readerFactory = aip::AcquireUncachedActivationFactory<DataReader, IDataReaderFactory>();
                const auto reader = readerFactory.CreateDataReader(stream.GetInputStreamAt(0));
                const auto loaded = smtc_detail::Await(reader.LoadAsync(static_cast<std::uint32_t>(size)), deadline);
                if (loaded != size) data.artworkError = L"The media artwork stream was incomplete.";
                else {
                    data.artworkBytes.resize(loaded);
                    reader.ReadBytes(winrt::array_view<std::uint8_t>(data.artworkBytes));
                }
            }
        } catch (...) {
            data.artworkBytes.clear();
            data.artworkError = L"Could not read the media artwork before its deadline.";
        }
    }
    return data;
}

inline SmtcSessionData ReadSmtcManagerSession(const SmtcManager& manager,
    smtc_detail::Clock::time_point deadline, bool includeArtwork, const SmtcCancel& canceled)
{
    return ReadSmtcSessionProperties(manager.GetCurrentSession(), deadline, includeArtwork, canceled);
}

inline SmtcSessionData ReadNativeSmtcSession(
    smtc_detail::Clock::time_point deadline, bool includeArtwork, const SmtcCancel& canceled)
{
    const auto manager = AcquireNativeSmtcManager(deadline, canceled);
    return ReadSmtcManagerSession(manager, deadline, includeArtwork, canceled);
}

// One instance belongs to one AsyncSource worker. Constructing it is inert;
// Read lazily pins that worker's MTA for the retained media-service client.
// The owner's worker-cleanup hook MUST call Reset on cancellation/profile
// changes and Stop before this object is destroyed on another thread.
struct SmtcEventState {
    std::mutex gate;
    std::atomic<bool> dirty{false}, supported{false};
    bool closed = false;
    std::function<void()> wake;
    void Close() noexcept {
        std::lock_guard<std::mutex> lock(gate);
        closed = true; wake = {}; dirty = false; supported = false;
    }
    static void Notify(const std::weak_ptr<SmtcEventState>& weak) noexcept {
        if (auto state = weak.lock()) {
            std::lock_guard<std::mutex> lock(state->gate);
            if (state->closed || state->dirty.exchange(true)) return;
            // Wake is a nonreentrant, non-COM host notification. Closing this
            // gate waits for the last in-flight wake before clearing it.
            if (state->wake) { try { state->wake(); } catch (...) {} }
        }
    }
};
struct SmtcSessionEventEpoch {
    std::mutex gate;
    bool closed = false;
    std::weak_ptr<SmtcEventState> destination;
    void Close() noexcept { std::lock_guard<std::mutex> lock(gate); closed = true; destination.reset(); }
    static void Notify(const std::weak_ptr<SmtcSessionEventEpoch>& weak) noexcept {
        if (auto state = weak.lock()) {
            std::lock_guard<std::mutex> lock(state->gate);
            if (!state->closed) SmtcEventState::Notify(state->destination);
        }
    }
};
class SmtcWorkerSession {
    using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
    std::unique_ptr<smtc_detail::Apartment> apartment_;
    SmtcManager manager_{nullptr};
    SmtcManagerFactory factory_;
    Session watchedSession_{nullptr};
    winrt::event_token currentToken_{}, mediaToken_{}, playbackToken_{};
    bool managerSubscribed_ = false, mediaSubscribed_ = false, playbackSubscribed_ = false;
    bool managerEventsAttempted_ = false, sessionEventsAttempted_ = false;
    std::shared_ptr<SmtcEventState> eventState_;
    std::shared_ptr<SmtcSessionEventEpoch> sessionEpoch_;
    std::wstring appIconKey_, appIconError_;
    std::vector<std::uint8_t> appIconBytes_;
    smtc_detail::Clock::time_point appIconRetryAfter_{};
    void ReadAppIcon(SmtcSessionData& data, smtc_detail::Clock::time_point deadline, const SmtcCancel& canceled) {
        if (appIconKey_ != data.appId) {
            appIconKey_ = data.appId; appIconBytes_.clear(); appIconError_.clear(); appIconRetryAfter_ = {};
        }
        if (appIconBytes_.empty() && smtc_detail::Clock::now() >= appIconRetryAfter_) {
            appIconRetryAfter_ = smtc_detail::Clock::now() + std::chrono::minutes(5);
            try {
                if (canceled && canceled()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
                using namespace winrt::Windows::ApplicationModel;
                const auto factory = aip::AcquireUncachedActivationFactory<AppInfo, IAppInfoStatics>();
                const auto app = factory.GetFromAppUserModelId(data.appId);
                if (app == nullptr) throw winrt::hresult_error(E_FAIL);
                const auto logo = app.DisplayInfo().GetLogo({48, 48});
                const auto stream = smtc_detail::Await(logo.OpenReadAsync(), deadline);
                const auto size = stream.Size();
                if (!size || size > 1024 * 1024) throw winrt::hresult_error(E_INVALIDARG);
                using namespace winrt::Windows::Storage::Streams;
                const auto readerFactory = aip::AcquireUncachedActivationFactory<DataReader, IDataReaderFactory>();
                const auto reader = readerFactory.CreateDataReader(stream.GetInputStreamAt(0));
                const auto loaded = smtc_detail::Await(reader.LoadAsync(static_cast<std::uint32_t>(size)), deadline);
                if (loaded != size) throw winrt::hresult_error(E_FAIL);
                std::vector<std::uint8_t> bytes(loaded);
                reader.ReadBytes(winrt::array_view<std::uint8_t>(bytes));
                if (canceled && canceled()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
                appIconBytes_ = std::move(bytes); appIconError_.clear();
            } catch (const winrt::hresult_error&) {
                appIconError_ = L"The player's application icon is unavailable; showing playback status.";
            }
        }
        data.appIconBytes = appIconBytes_; data.appIconError = appIconError_;
    }
    void RevokeSession() noexcept {
        if (sessionEpoch_) sessionEpoch_->Close();
        if (watchedSession_ != nullptr) {
            if (mediaSubscribed_) { try { watchedSession_.MediaPropertiesChanged(mediaToken_); } catch (...) {} }
            if (playbackSubscribed_) { try { watchedSession_.PlaybackInfoChanged(playbackToken_); } catch (...) {} }
        }
        mediaSubscribed_ = playbackSubscribed_ = sessionEventsAttempted_ = false;
        watchedSession_ = nullptr;
        sessionEpoch_.reset();
    }
    void DisableEvents() noexcept {
        if (auto state = std::atomic_load(&eventState_)) state->Close();
        RevokeSession();
        if (manager_ != nullptr && managerSubscribed_) { try { manager_.CurrentSessionChanged(currentToken_); } catch (...) {} }
        managerSubscribed_ = managerEventsAttempted_ = false;
        std::atomic_store(&eventState_, std::shared_ptr<SmtcEventState>{});
    }
    void Watch(const Session& session, bool useEvents, const std::function<void()>& wake) {
        if (!useEvents) { DisableEvents(); return; }
        auto state = std::atomic_load(&eventState_);
        if (!state) {
            state = std::make_shared<SmtcEventState>(); state->wake = wake;
            std::atomic_store(&eventState_, state);
        }
        const std::weak_ptr<SmtcEventState> weak = state;
        if (!managerEventsAttempted_) {
            managerEventsAttempted_ = true;
            try {
                currentToken_ = manager_.CurrentSessionChanged([weak](const auto&, const auto&) { SmtcEventState::Notify(weak); });
                managerSubscribed_ = true;
            } catch (...) { managerSubscribed_ = false; }
        }
        if (winrt::get_abi(watchedSession_) != winrt::get_abi(session)) { RevokeSession(); watchedSession_ = session; }
        if (session != nullptr && !sessionEventsAttempted_) {
            sessionEventsAttempted_ = true;
            sessionEpoch_ = std::make_shared<SmtcSessionEventEpoch>(); sessionEpoch_->destination = state;
            const std::weak_ptr<SmtcSessionEventEpoch> sessionWeak = sessionEpoch_;
            try {
                mediaToken_ = session.MediaPropertiesChanged([sessionWeak](const auto&, const auto&) { SmtcSessionEventEpoch::Notify(sessionWeak); });
                mediaSubscribed_ = true;
                playbackToken_ = session.PlaybackInfoChanged([sessionWeak](const auto&, const auto&) { SmtcSessionEventEpoch::Notify(sessionWeak); });
                playbackSubscribed_ = true;
            } catch (...) {
                if (mediaSubscribed_) { try { session.MediaPropertiesChanged(mediaToken_); } catch (...) {} }
                mediaSubscribed_ = playbackSubscribed_ = false;
            }
        }
        state->supported = managerSubscribed_ && (session == nullptr || (mediaSubscribed_ && playbackSubscribed_));
    }
public:
    explicit SmtcWorkerSession(SmtcManagerFactory factory = {}) : factory_(std::move(factory)) {}
    SmtcWorkerSession(const SmtcWorkerSession&) = delete;
    SmtcWorkerSession& operator=(const SmtcWorkerSession&) = delete;
    void Reset() noexcept {
        // Release all manager proxies while their owning MTA still exists.
        DisableEvents();
        manager_ = nullptr;
        appIconKey_.clear(); appIconBytes_.clear(); appIconError_.clear(); appIconRetryAfter_ = {};
        apartment_.reset();
    }
    bool ReportEventsSupported() const noexcept {
        const auto state = std::atomic_load(&eventState_);
        return state && state->supported.load();
    }
    bool ConsumeDirty() noexcept {
        const auto state = std::atomic_load(&eventState_);
        return state && state->dirty.exchange(false);
    }
    SmtcSessionData Read(smtc_detail::Clock::time_point deadline, bool includeArtwork, const SmtcCancel& canceled,
        bool useEvents = false, const std::function<void()>& wake = {}, bool includeAppIcon = false) {
        if (canceled && canceled()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        if (!apartment_) {
            auto apartment = std::make_unique<smtc_detail::Apartment>();
            winrt::check_hresult(apartment->Result());
            apartment_ = std::move(apartment);
        }
        try {
            if (manager_ == nullptr) manager_ = factory_ ? factory_(deadline, canceled) : AcquireNativeSmtcManager(deadline, canceled);
            if (manager_ == nullptr) throw winrt::hresult_error(E_UNEXPECTED, L"The media session manager is unavailable.");
            if (canceled && canceled()) { Reset(); throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED)); }
            const auto session = manager_.GetCurrentSession();
            Watch(session, useEvents, wake);
            auto data = ReadSmtcSessionProperties(session, deadline, includeArtwork, canceled);
            if (includeAppIcon && data.hasSession) ReadAppIcon(data, deadline, canceled);
            return data;
        } catch (const winrt::hresult_error& exception) {
            const auto code = static_cast<HRESULT>(exception.code());
            if (code == RPC_E_DISCONNECTED || code == RPC_E_SERVER_DIED || code == RPC_E_SERVER_DIED_DNE ||
                code == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE) || code == HRESULT_FROM_WIN32(RPC_S_CALL_FAILED) ||
                code == HRESULT_FROM_WIN32(ERROR_CANCELLED)) Reset();
            // No immediate reconnect loop: the source's next configured
            // refresh is the only retry, including acquisition failures.
            throw;
        }
    }
};

inline bool ReadSmtcMedia(
    SmtcMediaSnapshot& media,
    std::wstring& error,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(1500),
    bool includeArtwork = true,
    const SmtcSessionReader& reader = {},
    const SmtcCancel& canceled = {},
    SmtcArtworkCache* cache = nullptr, bool includeAppIcon = false)
{
    media = {};
    error.clear();
    if (timeout.count() < 1 || timeout.count() > 60000)
    {
        error = L"SMTC timeout must be between 1 and 60000 milliseconds.";
        return false;
    }
    smtc_detail::Apartment apartment;
    if (FAILED(apartment.Result()))
    {
        error = apartment.Result() == RPC_E_CHANGED_MODE
            ? L"SMTC must run on a worker with a multithreaded COM apartment."
            : L"Could not initialize the SMTC worker's COM apartment.";
        return false;
    }
    try
    {
        const auto deadline = smtc_detail::Clock::now() + timeout;
        if (canceled && canceled()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        auto data = reader ? reader(deadline, includeArtwork, canceled) : ReadNativeSmtcSession(deadline, includeArtwork, canceled);
        if (canceled && canceled()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        if (!data.hasSession)
        {
            media.text = {L"No media playing", L"SMTC", L""};
            media.title = media.text.primary; media.source = L"SMTC"; media.status = L"Idle";
            media.playbackState = MediaPlaybackState::Stopped;
            return true;
        }
        SmtcMediaSnapshot snapshot;
        snapshot.hasSession = true;
        snapshot.title = smtc_detail::Clean(std::move(data.title));
        if (snapshot.title.empty()) snapshot.title = L"(untitled media)";
        snapshot.artist = smtc_detail::Clean(std::move(data.artist));
        if (snapshot.artist.empty()) snapshot.artist = smtc_detail::Clean(std::move(data.albumArtist));
        if (snapshot.artist.empty()) snapshot.artist = smtc_detail::Clean(std::move(data.subtitle));
        snapshot.source = smtc_detail::SourceName(std::move(data.appId));
        snapshot.status = smtc_detail::Clean(std::move(data.status));
        snapshot.playbackState = data.playbackState;
        // Reader injection and legacy adapters may supply only the provider's
        // canonical status. The renderer never infers state from visible text.
        if (snapshot.playbackState == MediaPlaybackState::Unknown) {
            const auto status = Lower(snapshot.status);
            if (status == L"playing") snapshot.playbackState = MediaPlaybackState::Playing;
            else if (status == L"paused") snapshot.playbackState = MediaPlaybackState::Paused;
            else if (status == L"stopped" || status == L"closed") snapshot.playbackState = MediaPlaybackState::Stopped;
        }
        snapshot.repeat = data.repeat; snapshot.shuffle = data.shuffle;
        snapshot.text = {snapshot.title, snapshot.artist, L""};
        AppendBounded(snapshot.text.secondary, snapshot.status.empty() ? snapshot.source : snapshot.status + L" - " + snapshot.source);
        if (includeArtwork) {
            if (data.artworkBytes.size() > SmtcMaximumArtworkBytes)
                snapshot.artworkError = L"The media artwork exceeds the encoded byte limit.";
            else snapshot.artworkBytes = std::move(data.artworkBytes);
            if (snapshot.artworkError.empty()) snapshot.artworkError = smtc_detail::Clean(std::move(data.artworkError));
            if (cache && !snapshot.artworkBytes.empty()) {
                snapshot.artwork = cache->Store(snapshot.artworkBytes, snapshot.artworkError, deadline, canceled);
                snapshot.artworkBytes.clear();
                snapshot.artworkBytes.shrink_to_fit();
            }
        }
        if (includeAppIcon) {
            snapshot.appIconError = smtc_detail::Clean(std::move(data.appIconError));
            if (data.appIconBytes.size() > 1024 * 1024) snapshot.appIconError = L"The application icon exceeds its byte limit.";
            else if (cache && !data.appIconBytes.empty())
                snapshot.appIcon = cache->Store(data.appIconBytes, snapshot.appIconError, deadline, canceled, true);
        }
        media = std::move(snapshot);
        return true;
    }
    catch (const winrt::hresult_error& exception)
    {
        const auto code = static_cast<HRESULT>(exception.code());
        if (code == REGDB_E_CLASSNOTREG || code == E_NOINTERFACE)
            error = L"SMTC media information requires Windows 10 version 1809 or later.";
        else
        {
            wchar_t codeText[16] = {};
            swprintf_s(codeText, L"%08X", static_cast<unsigned int>(code));
            error = L"SMTC read failed (0x" + std::wstring(codeText) + L"): " +
                smtc_detail::Clean(std::wstring(exception.message()));
        }
    }
    catch (const std::exception&)
    {
        error = L"SMTC could not allocate or copy the media snapshot.";
    }
    catch (...)
    {
        error = L"SMTC could not retrieve media information.";
    }
    return false;
}

inline bool ReadSmtcContent(Text& text, std::wstring& error,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(1500))
{
    SmtcMediaSnapshot snapshot;
    const bool ok = ReadSmtcMedia(snapshot, error, timeout, false);
    text = std::move(snapshot.text);
    return ok;
}
} } // namespace aip::content

#endif
