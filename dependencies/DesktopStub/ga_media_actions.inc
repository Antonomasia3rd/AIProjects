// Explicit user media actions run on one bounded worker, never in a shell
// synchronous input callback. Default/inert smoke cannot drive playback/audio.
#include "../tile_actions.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <winrt/Windows.Media.h>
#ifndef DESKTOPSTUB_CONTENT_MESSAGEBOX
#define DESKTOPSTUB_CONTENT_MESSAGEBOX MessageBoxW
#endif
static constexpr UINT WM_APP_MEDIA_ACTION_COMPLETE = WM_APP + 114;
static std::thread g_mediaActionThread;
static std::atomic<bool> g_mediaActionBusy{false};
static std::mutex g_mediaActionResultMutex;
static std::wstring g_mediaActionError;

static bool RunNativeMediaAction(const std::wstring& key, int volumeStep, std::wstring& error)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    (void)key; (void)volumeStep;
    error = L"Media actions are disabled in inert validation."; return false;
#else
    using namespace winrt::Windows::Media::Control;
    try {
        aip::content::smtc_detail::Apartment apartment;
        winrt::check_hresult(apartment.Result());
        if (key == L"volume-down" || key == L"volume-up") {
            winrt::com_ptr<IMMDeviceEnumerator> enumerator;
            winrt::check_hresult(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), enumerator.put_void()));
            winrt::com_ptr<IMMDevice> device;
            winrt::check_hresult(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, device.put()));
            winrt::com_ptr<IAudioEndpointVolume> volume;
            winrt::check_hresult(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER,
                nullptr, volume.put_void()));
            float level = 0; winrt::check_hresult(volume->GetMasterVolumeLevelScalar(&level));
            level += (key == L"volume-up" ? 1.0f : -1.0f) * volumeStep / 100.0f;
            winrt::check_hresult(volume->SetMasterVolumeLevelScalar(std::clamp(level, 0.0f, 1.0f), nullptr));
            return true;
        }
        const auto deadline = aip::content::smtc_detail::Clock::now() + std::chrono::seconds(3);
        auto manager = aip::content::AcquireNativeSmtcManager(deadline, {});
        auto session = manager.GetCurrentSession();
        if (!session) { error = L"No current Windows media session. Start playback in your music app first."; return false; }
        auto info = session.GetPlaybackInfo();
        if (!info) { error = L"The player has no playback controls available yet."; return false; }
        auto controls = info.Controls();
        if (!controls) { error = L"The player has no playback controls available yet."; return false; }
        winrt::Windows::Foundation::IAsyncOperation<bool> operation{nullptr};
        if (key == L"toggle" && controls.IsPlayPauseToggleEnabled()) operation = session.TryTogglePlayPauseAsync();
        else if (key == L"play" && controls.IsPlayEnabled()) operation = session.TryPlayAsync();
        else if (key == L"pause" && controls.IsPauseEnabled()) operation = session.TryPauseAsync();
        else if (key == L"next" && controls.IsNextEnabled()) operation = session.TrySkipNextAsync();
        else if (key == L"previous" && controls.IsPreviousEnabled()) operation = session.TrySkipPreviousAsync();
        else if (key == L"shuffle" && controls.IsShuffleEnabled()) {
            auto current = info.IsShuffleActive();
            operation = session.TryChangeShuffleActiveAsync(!(current && current.Value()));
        } else if (key == L"repeat" && controls.IsRepeatEnabled()) {
            using Mode = winrt::Windows::Media::MediaPlaybackAutoRepeatMode;
            auto current = info.AutoRepeatMode();
            auto mode = current ? current.Value() : Mode::None;
            mode = mode == Mode::None ? Mode::List : mode == Mode::List ? Mode::Track : Mode::None;
            operation = session.TryChangeAutoRepeatModeAsync(mode);
        }
        if (!operation) { error = L"The current player does not support this media action."; return false; }
        if (!aip::content::smtc_detail::Await(operation, deadline)) {
            error = L"The player declined the media action."; return false;
        }
        return true;
    } catch (const winrt::hresult_error& e) {
        error = L"Windows could not complete the media action (" + std::to_wstring(e.code().value) + L").";
    } catch (...) { error = L"Could not complete the media action."; }
    return false;
#endif
}
static void QueueMediaTileAction(HWND owner, const std::wstring& action, int requestedStep = -1)
{
    if (!aip::content::FindTileAction(action)) return;
    if (g_mediaActionBusy.exchange(true)) return; // Do not accumulate repeated clicks.
    if (g_mediaActionThread.joinable()) g_mediaActionThread.join();
    const int step = requestedStep > 0 ? std::clamp(requestedStep, 1, 100) : IniReadClampedI(L"MediaActions", L"VolumeStepPercent", 5, 1, 100);
    try {
        g_mediaActionThread = std::thread([owner, action, step] {
            std::wstring error;
            RunNativeMediaAction(action, step, error);
            { std::lock_guard<std::mutex> lock(g_mediaActionResultMutex); g_mediaActionError = std::move(error); }
            g_mediaActionBusy = false;
            PostMessageW(owner, WM_APP_MEDIA_ACTION_COMPLETE, 0, 0);
        });
    } catch (...) { g_mediaActionBusy = false; }
}
static bool MediaTileActionHandleMessage(HWND owner, UINT message)
{
    if (message != WM_APP_MEDIA_ACTION_COMPLETE) return false;
    std::wstring error;
    { std::lock_guard<std::mutex> lock(g_mediaActionResultMutex); error.swap(g_mediaActionError); }
    if (!error.empty()) DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Media action", MB_OK | MB_ICONINFORMATION);
    else WakePollThread();
    return true;
}
static void ShutdownMediaTileActions() { if (g_mediaActionThread.joinable()) g_mediaActionThread.join(); }
