#ifndef AIP_CONTENT_SOURCES_SMTC_INC
#define AIP_CONTENT_SOURCES_SMTC_INC

// Read-only SMTC text provider. Call on a worker, never the host's UI thread.
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

inline bool ReadSmtcContent(
    Text& text,
    std::wstring& error,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(1500))
{
    text = {};
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
        using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
        const auto deadline = smtc_detail::Clock::now() + timeout;
        const auto manager = smtc_detail::Await(Manager::RequestAsync(), deadline);
        const auto session = manager.GetCurrentSession();
        if (session == nullptr)
        {
            text.primary = L"No media playing";
            text.secondary = L"SMTC";
            return true;
        }
        const auto properties = smtc_detail::Await(session.TryGetMediaPropertiesAsync(), deadline);
        Text snapshot;
        snapshot.primary = smtc_detail::Clean(std::wstring(properties.Title()));
        if (snapshot.primary.empty()) snapshot.primary = L"(untitled media)";
        snapshot.secondary = smtc_detail::Clean(std::wstring(properties.Artist()));
        if (snapshot.secondary.empty()) snapshot.secondary = smtc_detail::Clean(std::wstring(properties.AlbumArtist()));
        if (snapshot.secondary.empty()) snapshot.secondary = smtc_detail::Clean(std::wstring(properties.Subtitle()));
        auto playbackText = smtc_detail::SourceName(std::wstring(session.SourceAppUserModelId()));
        const auto playback = session.GetPlaybackInfo();
        if (playback != nullptr)
        {
            playbackText = std::wstring(smtc_detail::PlaybackStatus(playback.PlaybackStatus())) + L" - " + playbackText;
        }
        AppendBounded(snapshot.secondary, smtc_detail::Clean(std::move(playbackText)));
        text = std::move(snapshot);
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
} } // namespace aip::content

#endif
