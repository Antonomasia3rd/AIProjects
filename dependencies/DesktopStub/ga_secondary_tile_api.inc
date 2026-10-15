// Explicit Start pin/unpin actions. Default/inert smoke never enters WinRT or
// creates a window. Native completion delegates post plain mailbox records;
// all WinRT state/results and owning windows are released on the UI thread.
#include "../secondary_tiles.h"
#include "../tile_actions.h"
#include "../tile_visual_assets.inc"
#include "../winrt_activation.h"
#include <winrt/Windows.UI.StartScreen.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <shobjidl.h>
#include <deque>
#include <set>

static constexpr UINT WM_APP_SECONDARY_TILE_COMPLETE = WM_APP + 113;
static constexpr int SECONDARY_TILE_CANCEL_ID = 51001;
static void WakePollThread();

[[maybe_unused]] static std::wstring CanonicalSecondaryTileRuntimeId(std::wstring id)
{
    const std::wstring prefix = L"AipSource_";
    const std::wstring actionPrefix = L"AipAction_";
    if (id.compare(0, actionPrefix.size(), actionPrefix) == 0) {
        auto key = id.substr(actionPrefix.size());
        std::replace(key.begin(), key.end(), L'_', L'-');
        const auto* action = aip::content::FindTileAction(key);
        if (!action) return {};
        std::wstring runtime = actionPrefix + action->id;
        std::replace(runtime.begin() + actionPrefix.size(), runtime.end(), L'-', L'_');
        return runtime;
    }
    if (id.compare(0, prefix.size(), prefix) == 0) id.erase(0, prefix.size());
    const auto* descriptor = aip::content::FindSecondaryTileDescriptor(id);
    return descriptor ? prefix + descriptor->id : std::wstring{};
}

#ifndef DESKTOPSTUB_INERT_HARDWARE
namespace secondary_ui {
using NativeTile = winrt::Windows::UI::StartScreen::SecondaryTile;
using BoolOperation = winrt::Windows::Foundation::IAsyncOperation<bool>;
using TileListOperation = winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Foundation::Collections::IVectorView<NativeTile>>;
using AsyncStatus = winrt::Windows::Foundation::AsyncStatus;
enum class Kind { Pin, Unpin, List, RefreshList, RefreshLogo, VerifyLogos };
struct Apartment {
    HRESULT status;
    DWORD thread = GetCurrentThreadId();
    explicit Apartment(RO_INIT_TYPE type) : status(RoInitialize(type)) {}
    ~Apartment() { if (SUCCEEDED(status) && thread == GetCurrentThreadId()) RoUninitialize(); }
};
struct Completion { std::uint64_t id = 0; AsyncStatus status = AsyncStatus::Started; };
struct Mailbox {
    std::mutex mutex;
    bool accepting = true;
    std::deque<Completion> complete;
};
struct Pending {
    Kind kind = Kind::Pin;
    bool updateExisting = false;
    std::wstring id, name;
    HWND owner = nullptr;
    NativeTile tile{nullptr};
    BoolOperation mutation{nullptr};
    TileListOperation list{nullptr};
    aip::tile_visual::Assets expectedLogos;
};
static std::shared_ptr<Apartment> apartment;
static DWORD uiThread = 0;
static HWND host = nullptr;
static std::shared_ptr<Mailbox> mailbox;
static std::map<std::uint64_t, Pending> pending;
static std::uint64_t nextId = 0;
static bool shuttingDown = false;
static std::mutex pinnedMutex;
static std::set<std::wstring> pinned;
static bool logoRefreshAttempted = false;
static std::deque<NativeTile> logoRefreshQueue;
static bool logoEnumerationCompleted = false, logoVerificationStarted = false;
static std::map<std::wstring, aip::tile_visual::Assets> logoRefreshExpected;

static void Report(HWND owner, const std::wstring& text)
{
    Log(L"[!] Secondary tile: %ls", text.c_str());
    if (!shuttingDown)
        MessageBoxW(owner, text.c_str(), L"DesktopStub source tiles", MB_OK | MB_ICONWARNING);
}
static std::wstring ErrorText(const winrt::hresult_error& error)
{
    wchar_t code[16]{};
    swprintf_s(code, L"%08X", static_cast<unsigned>(error.code().value));
    return L"Windows could not complete the source tile request (0x" + std::wstring(code) + L"): " + std::wstring(error.message());
}
static bool EnsureUi(HWND owner, bool quiet = false)
{
    DWORD process = 0;
    const auto thread = owner ? GetWindowThreadProcessId(owner, &process) : 0;
    if (!thread || process != GetCurrentProcessId() || thread != GetCurrentThreadId() ||
        (uiThread && uiThread != thread) || shuttingDown) return false;
    if (!CurrentProcessHasPackageIdentity()) {
        if (!quiet) Report(owner, L"Source tiles require the registered, packaged DesktopStub launch. Enable Live Tile delivery and open that registered app first.");
        return false;
    }
    if (!apartment) {
        auto created = std::make_shared<Apartment>(RO_INIT_SINGLETHREADED);
        if (FAILED(created->status)) {
            if (!quiet) Report(owner, L"The source tile confirmation requires a Windows Runtime UI apartment.");
            return false;
        }
        apartment = std::move(created); uiThread = thread; host = owner;
        mailbox = std::make_shared<Mailbox>();
    }
    return true;
}
static void PostCompletion(const std::weak_ptr<Mailbox>& weak, HWND destination, std::uint64_t id, AsyncStatus status) noexcept
{
    // No WinRT results, HWND-owned state, globals or process enumeration here.
    // Shutdown closes the mailbox before canceling/releasing the UI objects.
    try {
        const auto box = weak.lock();
        if (!box || status == AsyncStatus::Started) return;
        {
            std::lock_guard<std::mutex> lock(box->mutex);
            if (!box->accepting) return;
            box->complete.push_back({id, status});
        }
        PostMessageW(destination, WM_APP_SECONDARY_TILE_COMPLETE, 0, 0);
    } catch (...) {}
}
static void CancelOwner(HWND owner)
{
    for (auto& item : pending) if (item.second.owner == owner) {
        try { if (item.second.mutation) item.second.mutation.Cancel(); } catch (...) {}
        item.second.owner = nullptr;
    }
    if (IsWindow(owner)) DestroyWindow(owner);
}
static LRESULT CALLBACK OwnerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == SECONDARY_TILE_CANCEL_ID)) {
        CancelOwner(window); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
static HWND CreateOwner(HWND parent, const std::wstring& name, bool pin)
{
    const wchar_t* className = L"DesktopStubSecondaryTileOwner";
    WNDCLASSW definition{};
    definition.lpfnWndProc = OwnerProc; definition.hInstance = GetModuleHandleW(nullptr);
    definition.lpszClassName = className; definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    definition.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&definition) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    auto shownName = name.substr(0, 80);
    std::replace(shownName.begin(), shownName.end(), L'\r', L' ');
    std::replace(shownName.begin(), shownName.end(), L'\n', L' ');
    const auto title = std::wstring(L"DesktopStub - ") + (pin ? L"Pin " : L"Unpin ") + shownName;
    const auto window = CreateWindowExW(WS_EX_TOOLWINDOW, className, title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 430, 145,
        parent, nullptr, definition.hInstance, nullptr);
    if (!window) return nullptr;
    const auto label = CreateWindowExW(0, L"STATIC", L"Confirm or decline the Windows Start tile request. Cancel closes this request.",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 14, 12, 390, 42, window, nullptr, definition.hInstance, nullptr);
    const auto cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        310, 64, 92, 26, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(SECONDARY_TILE_CANCEL_ID)), definition.hInstance, nullptr);
    if (!label || !cancel) { DestroyWindow(window); return nullptr; }
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    ShowWindow(window, SW_SHOWNORMAL); SetForegroundWindow(window); UpdateWindow(window);
    return window;
}
static winrt::Windows::Foundation::Uri AssetUri(const std::wstring& relative)
{
    using namespace winrt::Windows::Foundation;
    const auto factory = aip::AcquireUncachedActivationFactory<Uri, IUriRuntimeClassFactory>();
    return factory.CreateUri(winrt::hstring(aip::tile_visual::PackageUri(relative)));
}
static aip::tile_visual::Assets ReadLogos(const NativeTile& tile)
{
    const auto text = [](const winrt::Windows::Foundation::Uri& uri) {
        return uri ? std::wstring(uri.AbsoluteUri()) : std::wstring{};
    };
    const auto visuals = tile.VisualElements();
    return {text(visuals.Square150x150Logo()), text(visuals.Wide310x150Logo()), text(visuals.Square310x310Logo()),
        text(visuals.Square70x70Logo()), text(visuals.Square44x44Logo()), text(visuals.Square30x30Logo())};
}
static void LogLogoMetadata(const std::wstring& id, const aip::tile_visual::Assets& logos)
{
    Log(L"Source tile %ls static logos: medium=%ls; wide=%ls; large=%ls; small=%ls; 44=%ls; 30=%ls",
        id.c_str(), logos.medium.c_str(), logos.wide.c_str(), logos.large.c_str(),
        logos.smallTile.c_str(), logos.logo44.c_str(), logos.logo30.c_str());
}
static void ApplyLogos(NativeTile& tile, const aip::tile_visual::Assets& logos)
{
    auto visuals = tile.VisualElements();
    aip::tile_visual::ApplyTopicLogos(visuals, logos, AssetUri);
}
static NativeTile CreatePinTile(const std::wstring& id, const std::wstring& name)
{
    using namespace winrt::Windows::UI::StartScreen;
    aip::tile_visual::Assets logos;
    std::wstring artworkError;
    if (!aip::tile_visual::EnsureTileTopicAssets(id, GetExeDir(), logos, artworkError))
        throw winrt::hresult_error(E_FAIL, winrt::hstring(artworkError));
    const auto factory = aip::AcquireUncachedActivationFactory<NativeTile, ISecondaryTileFactory2>();
    std::wstring profile;
    { std::lock_guard<std::mutex> lock(g_pathMutex); profile = g_iniPath; }
    const bool action = id.compare(0, std::wstring(L"AipAction_").size(), L"AipAction_") == 0;
    auto activationKey = id.substr(10u);
    if (action) std::replace(activationKey.begin(), activationKey.end(), L'_', L'-');
    const auto arguments = L"--ini " + aip::QuoteCommandLineArg(profile) +
        (action ? L" --media-action " : L" --tile-setup ") + activationKey;
    if (arguments.size() > 2048) throw winrt::hresult_error(E_INVALIDARG, L"The profile path is too long for Windows secondary tile activation arguments.");
    auto tile = factory.CreateMinimalTile(id, name, arguments, AssetUri(logos.medium), TileSize::Default);
    ApplyLogos(tile, logos);
    auto visuals = tile.VisualElements();
    visuals.ShowNameOnSquare150x150Logo(true);
    visuals.ShowNameOnWide310x150Logo(true);
    visuals.ShowNameOnSquare310x310Logo(true);
    return tile;
}
static bool UserMutationPending()
{
    return std::any_of(pending.begin(), pending.end(), [](const auto& item) {
        return item.second.kind == Kind::Pin || item.second.kind == Kind::Unpin;
    });
}
static void StartNextLogoRefresh()
{
    if (shuttingDown || UserMutationPending() || std::any_of(pending.begin(), pending.end(), [](const auto& item) {
        return item.second.kind == Kind::RefreshLogo;
    })) return;
    const auto keepApartment = apartment;
    while (!logoRefreshQueue.empty() && !shuttingDown) {
        Pending state; state.kind = Kind::RefreshLogo;
        state.tile = std::move(logoRefreshQueue.front()); logoRefreshQueue.pop_front();
        const auto request = ++nextId;
        try {
            state.id = std::wstring(state.tile.TileId());
            if (!aip::tile_visual::IsKnownSecondaryArtworkId(state.id)) continue;
            aip::tile_visual::Assets logos; std::wstring error;
            if (!aip::tile_visual::EnsureTileTopicAssets(state.id, GetExeDir(), logos, error)) {
                Log(L"[!] Source tile %ls artwork refresh skipped: %ls", state.id.c_str(), error.c_str()); continue;
            }
            state.expectedLogos = aip::tile_visual::LogoUris(logos);
            logoRefreshExpected[state.id] = state.expectedLogos;
            const auto before = ReadLogos(state.tile);
            LogLogoMetadata(state.id, before);
            if (!aip::tile_visual::NeedsTopicLogoUpdate(state.id, before, state.expectedLogos)) continue;
            // This is the existing FindAllAsync object. Preserve its ID, name,
            // arguments, color, branding and current Start placement. No pin or
            // notification API is called by this migration.
            ApplyLogos(state.tile, logos);
            state.mutation = state.tile.UpdateAsync();
            auto inserted = pending.emplace(request, std::move(state));
            const std::weak_ptr<Mailbox> weak = mailbox; const auto destination = host;
            inserted.first->second.mutation.Completed([weak, destination, request](const auto&, AsyncStatus status) noexcept {
                PostCompletion(weak, destination, request, status);
            });
            return; // Sequential updates; completion schedules the next tile.
        } catch (const winrt::hresult_error& error) {
            const auto found = pending.find(request);
            if (found != pending.end()) { state = std::move(found->second); pending.erase(found); }
            try { if (state.mutation) state.mutation.Cancel(); } catch (...) {}
            Log(L"[!] Static source tile artwork update failed: %ls", ErrorText(error).c_str());
        } catch (...) {
            const auto found = pending.find(request);
            if (found != pending.end()) { state = std::move(found->second); pending.erase(found); }
            try { if (state.mutation) state.mutation.Cancel(); } catch (...) {}
            LogText(L"[!] Could not prepare existing source tile artwork update.");
        }
    }
    if (!shuttingDown && logoEnumerationCompleted && !logoVerificationStarted && !logoRefreshExpected.empty()) {
        logoVerificationStarted = true;
        const auto request = ++nextId;
        try {
            Pending state; state.kind = Kind::VerifyLogos;
            const auto factory = aip::AcquireUncachedActivationFactory<NativeTile, winrt::Windows::UI::StartScreen::ISecondaryTileStatics>();
            state.list = factory.FindAllAsync();
            auto inserted = pending.emplace(request, std::move(state));
            const std::weak_ptr<Mailbox> weak = mailbox; const auto destination = host;
            inserted.first->second.list.Completed([weak, destination, request](const auto&, AsyncStatus status) noexcept {
                PostCompletion(weak, destination, request, status);
            });
        } catch (...) {
            pending.erase(request);
            LogText(L"[!] Could not reread source tile logo metadata after updating existing pins.");
        }
    }
}
static bool RequestMutation(HWND owner, const std::wstring& supplied, const std::wstring& name, bool pin)
{
    const auto id = CanonicalSecondaryTileRuntimeId(supplied);
    if (id.empty() || !EnsureUi(owner)) return false;
    const auto keepApartment = apartment;
    if (std::any_of(pending.begin(), pending.end(), [](const auto& item) {
        return item.second.kind == Kind::Pin || item.second.kind == Kind::Unpin || item.second.kind == Kind::RefreshLogo;
    })) {
        Report(owner, UserMutationPending() ? L"Finish or cancel the current Start tile confirmation before opening another." :
            L"Windows is finishing the current tile update. Please try this action again in a moment.");
        return false;
    }
    const auto request = ++nextId;
    Pending state; state.kind = pin ? Kind::Pin : Kind::Unpin; state.id = id; state.name = name.empty() ? id : name;
    try {
        state.owner = CreateOwner(owner, state.name, pin);
        if (!state.owner || !IsWindowVisible(state.owner)) throw winrt::hresult_error(E_FAIL, L"Could not create a visible owner for the Windows confirmation.");
        if (pin) state.tile = CreatePinTile(id, state.name);
        else {
            const auto factory = aip::AcquireUncachedActivationFactory<NativeTile, winrt::Windows::UI::StartScreen::ISecondaryTileFactory>();
            state.tile = factory.CreateWithId(id);
        }
        winrt::check_hresult(state.tile.as<IInitializeWithWindow>()->Initialize(state.owner));
        if (pin) {
            const auto statics = aip::AcquireUncachedActivationFactory<NativeTile, winrt::Windows::UI::StartScreen::ISecondaryTileStatics>();
            state.updateExisting = statics.Exists(id);
            state.mutation = state.updateExisting ? state.tile.UpdateAsync() : state.tile.RequestCreateAsync();
        } else state.mutation = state.tile.RequestDeleteAsync();
        auto inserted = pending.emplace(request, std::move(state));
        const std::weak_ptr<Mailbox> weak = mailbox; const auto destination = host;
        inserted.first->second.mutation.Completed([weak, destination, request](const auto&, AsyncStatus status) noexcept {
            PostCompletion(weak, destination, request, status);
        });
        return true; // Accepted async request, never a claim that Windows pinned it.
    } catch (const winrt::hresult_error& error) {
        const auto found = pending.find(request);
        if (found != pending.end()) { state = std::move(found->second); pending.erase(found); }
        try { if (state.mutation) state.mutation.Cancel(); } catch (...) {}
        if (state.owner && IsWindow(state.owner)) DestroyWindow(state.owner);
        state.mutation = nullptr; state.tile = nullptr; state.list = nullptr;
        Report(owner, ErrorText(error));
    } catch (...) {
        const auto found = pending.find(request);
        if (found != pending.end()) { state = std::move(found->second); pending.erase(found); }
        try { if (state.mutation) state.mutation.Cancel(); } catch (...) {}
        if (state.owner && IsWindow(state.owner)) DestroyWindow(state.owner);
        state.mutation = nullptr; state.tile = nullptr; state.list = nullptr;
        Report(owner, L"Could not prepare the source tile request.");
    }
    return false;
}
}
#endif

static bool SecondaryTileExists(const std::wstring& supplied)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    (void)supplied; return false;
#else
    const auto id = CanonicalSecondaryTileRuntimeId(supplied);
    if (id.empty() || !CurrentProcessHasPackageIdentity()) return false;
    try {
        secondary_ui::Apartment apartment(RO_INIT_MULTITHREADED);
        if (FAILED(apartment.status) && apartment.status != RPC_E_CHANGED_MODE) return false;
        const auto factory = aip::AcquireUncachedActivationFactory<secondary_ui::NativeTile, winrt::Windows::UI::StartScreen::ISecondaryTileStatics>();
        return factory.Exists(id);
    } catch (...) { return false; }
#endif
}
static bool RequestPinSecondaryTile(HWND owner, const std::wstring& id, const std::wstring& displayName)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    (void)owner; (void)id; (void)displayName; return false;
#else
    return secondary_ui::RequestMutation(owner, id, displayName, true);
#endif
}
static bool RequestUnpinSecondaryTile(HWND owner, const std::wstring& id)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    (void)owner; (void)id; return false;
#else
    const auto runtime = CanonicalSecondaryTileRuntimeId(id);
    return secondary_ui::RequestMutation(owner, runtime, runtime, false);
#endif
}
static bool RequestSecondaryTileList(HWND owner)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    (void)owner; return false;
#else
    using namespace secondary_ui;
    if (!EnsureUi(owner)) return false;
    const auto keepApartment = apartment;
    if (std::any_of(pending.begin(), pending.end(), [](const auto& item) { return item.second.kind == Kind::List; })) return true;
    const auto request = ++nextId;
    try {
        Pending state; state.kind = Kind::List;
        const auto factory = aip::AcquireUncachedActivationFactory<NativeTile, winrt::Windows::UI::StartScreen::ISecondaryTileStatics>();
        state.list = factory.FindAllAsync();
        auto inserted = pending.emplace(request, std::move(state));
        const std::weak_ptr<Mailbox> weak = mailbox; const auto destination = host;
        inserted.first->second.list.Completed([weak, destination, request](const auto&, AsyncStatus status) noexcept { PostCompletion(weak, destination, request, status); });
        return true;
    } catch (const winrt::hresult_error& error) { pending.erase(request); Report(owner, ErrorText(error)); }
    catch (...) { pending.erase(request); Report(owner, L"Could not list the pinned source tiles."); }
    return false;
#endif
}
// Normal startup upgrade hook. Existing pins are refreshed in place; missing
// destinations are never pinned and current notifications are never cleared.
static bool RequestRefreshExistingSecondaryTileLogos(HWND owner)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    (void)owner; return false;
#else
    using namespace secondary_ui;
    if (!EnsureUi(owner, true)) return false;
    const auto keepApartment = apartment;
    if (logoRefreshAttempted) return true;
    const auto request = ++nextId;
    try {
        Pending state; state.kind = Kind::RefreshList;
        const auto factory = aip::AcquireUncachedActivationFactory<NativeTile, winrt::Windows::UI::StartScreen::ISecondaryTileStatics>();
        state.list = factory.FindAllAsync();
        auto inserted = pending.emplace(request, std::move(state));
        const std::weak_ptr<Mailbox> weak = mailbox; const auto destination = host;
        inserted.first->second.list.Completed([weak, destination, request](const auto&, AsyncStatus status) noexcept {
            PostCompletion(weak, destination, request, status);
        });
        logoRefreshAttempted = true;
        return true;
    } catch (const winrt::hresult_error& error) {
        pending.erase(request); Log(L"[!] Existing source tile artwork enumeration failed: %ls", ErrorText(error).c_str());
    } catch (...) { pending.erase(request); LogText(L"[!] Could not enumerate existing source tiles for artwork refresh."); }
    return false;
#endif
}
static std::vector<std::wstring> ListKnownSecondaryTileIds()
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return {};
#else
    std::lock_guard<std::mutex> lock(secondary_ui::pinnedMutex);
    return {secondary_ui::pinned.begin(), secondary_ui::pinned.end()};
#endif
}
static bool SecondaryTileHandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)wParam; (void)lParam;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    (void)window; (void)message; return false;
#else
    using namespace secondary_ui;
    if (message != WM_APP_SECONDARY_TILE_COMPLETE || window != host || GetCurrentThreadId() != uiThread || !mailbox) return false;
    const auto keepApartment = apartment;
    std::deque<Completion> ready;
    { std::lock_guard<std::mutex> lock(mailbox->mutex); ready.swap(mailbox->complete); }
    for (const auto& completion : ready) {
        const auto found = pending.find(completion.id);
        if (found == pending.end()) continue;
        // A synchronous COM accessor may pump window messages. Keep this
        // operation and apartment local before reading any completed result so
        // shutdown cannot invalidate a reference into the pending map.
        auto state = std::move(found->second); pending.erase(found);
        try {
            if (completion.status != AsyncStatus::Completed && completion.status != AsyncStatus::Error && completion.status != AsyncStatus::Canceled)
                throw winrt::hresult_error(E_UNEXPECTED, L"Windows returned an unknown secondary tile completion state.");
            if (completion.status == AsyncStatus::Canceled)
                throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
            if (state.kind == Kind::List || state.kind == Kind::RefreshList || state.kind == Kind::VerifyLogos) {
                std::set<std::wstring> result;
                for (const auto& tile : state.list.GetResults()) {
                    const auto id = std::wstring(tile.TileId());
                    if (!aip::tile_visual::IsKnownSecondaryArtworkId(id) || !result.insert(id).second) continue;
                    if (state.kind == Kind::RefreshList) logoRefreshQueue.push_back(tile);
                    if (state.kind == Kind::VerifyLogos) {
                        const auto expected = logoRefreshExpected.find(id);
                        if (expected != logoRefreshExpected.end()) {
                            const auto actual = ReadLogos(tile);
                            LogLogoMetadata(id, actual);
                            Log(L"%ls Source tile %ls topic logo metadata %ls after update.",
                                aip::tile_visual::SamePublishedLogos(actual, expected->second) ? L"[+]" : L"[!]", id.c_str(),
                                aip::tile_visual::SamePublishedLogos(actual, expected->second) ? L"verified" : L"did not match the requested assets");
                        }
                    }
                }
                if (state.kind == Kind::RefreshList) logoEnumerationCompleted = true;
                if (state.kind == Kind::VerifyLogos) for (const auto& expected : logoRefreshExpected)
                    if (!result.count(expected.first)) Log(L"Source tile %ls is no longer pinned; metadata refresh did not recreate it.", expected.first.c_str());
                { std::lock_guard<std::mutex> lock(pinnedMutex); pinned = std::move(result); }
                LogText(L"Pinned source tile list refreshed.");
            } else {
                const bool accepted = state.mutation.GetResults(); // Terminal only, no blocking wait/get.
                if (accepted) {
                    if (state.kind == Kind::RefreshLogo) {
                        Log(L"Source tile %ls topic artwork metadata published: %ls", state.id.c_str(), state.expectedLogos.medium.c_str());
                    } else {
                        { std::lock_guard<std::mutex> lock(pinnedMutex); if (state.kind == Kind::Pin) pinned.insert(state.id); else pinned.erase(state.id); }
                        ForgetSecondaryTileDelivery(state.id);
                        Log(L"Source tile %ls: %ls", state.id.c_str(), state.kind == Kind::Pin ?
                            (state.updateExisting ? L"updated after the requested metadata change" : L"pinned after Windows confirmation") : L"unpinned after Windows confirmation");
                    }
                } else Log(L"Source tile %ls: Windows did not apply the requested tile change.", state.id.c_str());
            }
        } catch (const winrt::hresult_error& error) {
            // Completion runs with UI-owned proxies alive. Do not enter a
            // nested modal loop which could dispatch shutdown before release.
            if (error.code() != HRESULT_FROM_WIN32(ERROR_CANCELLED)) Log(L"[!] Secondary tile: %ls", ErrorText(error).c_str());
            else LogText(L"Source tile request cancelled.");
        } catch (...) { LogText(L"[!] Secondary tile: could not retrieve the completed request."); }
        if (state.owner && IsWindow(state.owner)) DestroyWindow(state.owner);
        state.mutation = nullptr; state.tile = nullptr; state.list = nullptr;
        StartNextLogoRefresh();
        WakePollThread();
    }
    return true;
#endif
}
static void ShutdownSecondaryTileUi()
{
#ifndef DESKTOPSTUB_INERT_HARDWARE
    using namespace secondary_ui;
    shuttingDown = true;
    if (!apartment || GetCurrentThreadId() != uiThread) return;
    { std::lock_guard<std::mutex> lock(mailbox->mutex); mailbox->accepting = false; mailbox->complete.clear(); }
    for (auto& item : pending) {
        try { if (item.second.mutation) item.second.mutation.Cancel(); if (item.second.list) item.second.list.Cancel(); } catch (...) {}
        if (item.second.owner && IsWindow(item.second.owner)) DestroyWindow(item.second.owner);
    }
    pending.clear(); logoRefreshQueue.clear(); logoRefreshExpected.clear(); mailbox.reset();
    apartment.reset(); uiThread = 0; host = nullptr;
#endif
}
