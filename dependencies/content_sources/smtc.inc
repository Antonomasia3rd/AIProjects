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
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
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
};
struct SmtcMediaSnapshot {
    bool hasSession = false;
    Text text;
    std::wstring title, artist, source, status, artworkError;
    std::vector<std::uint8_t> artworkBytes;
    std::shared_ptr<const SmtcArtworkFile> artwork;
};
// Injection replaces the complete session/property read, so fixtures never
// request real system media sessions. Native text and thumbnail use one session,
// one property object, and the same total operation deadline.
using SmtcSessionReader = std::function<SmtcSessionData(
    smtc_detail::Clock::time_point, bool, const SmtcCancel&)>;

inline SmtcSessionData ReadNativeSmtcSession(
    smtc_detail::Clock::time_point deadline, bool includeArtwork, const SmtcCancel& canceled)
{
    using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
    if (canceled && canceled()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    const auto manager = smtc_detail::Await(Manager::RequestAsync(), deadline);
    const auto session = manager.GetCurrentSession();
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
    if (includeArtwork && properties.Thumbnail() != nullptr) {
        try {
            if (canceled && canceled()) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
            const auto stream = smtc_detail::Await(properties.Thumbnail().OpenReadAsync(), deadline);
            const auto size = stream.Size();
            if (size > SmtcMaximumArtworkBytes) data.artworkError = L"The media artwork exceeds the encoded byte limit.";
            else if (size != 0) {
                winrt::Windows::Storage::Streams::DataReader reader(stream.GetInputStreamAt(0));
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

inline bool ReadSmtcMedia(
    SmtcMediaSnapshot& media,
    std::wstring& error,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(1500),
    bool includeArtwork = true,
    const SmtcSessionReader& reader = {},
    const SmtcCancel& canceled = {},
    SmtcArtworkCache* cache = nullptr)
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
