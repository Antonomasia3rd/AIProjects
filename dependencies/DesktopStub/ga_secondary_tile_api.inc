// Explicit Start pin/unpin actions. Default/inert smoke never enters WinRT or
// creates a window. Native completion delegates post plain mailbox records;
// all WinRT state/results and owning windows are released on the UI thread.
#include "../secondary_tiles.h"
#include "../tile_actions.h"
#include "../tile_visual_assets.inc"
#include "../winrt_activation.h"
#include <winrt/Windows.UI.StartScreen.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <shobjidl.h>
#include <deque>
#include <set>

static constexpr UINT WM_APP_SECONDARY_TILE_COMPLETE = WM_APP + 113;
static constexpr UINT WM_APP_SECONDARY_APPEARANCE_CHANGED = WM_APP + 115;
static constexpr UINT WM_APP_GALLERY_SETUP_READY = WM_APP + 116;
static constexpr UINT WM_APP_SECONDARY_PIN_INVENTORY = WM_APP + 117;
static constexpr UINT_PTR GALLERY_HISTORY_TIMER = 0x47414c53;
static void WakePollThread();
static bool RequestRefreshExistingSecondaryTileLogos(HWND owner, bool force = false);
static bool RequestSecondaryTileList(HWND owner);
static void StartNextSecondaryPin();
static aip::content::AppearanceRefreshNotice g_secondaryAppearanceNotice;
static aip::content::SecondaryPinBatch g_secondaryPinBatch;
static aip::content::SecondaryPinInventory g_secondaryPinInventory;
static std::set<std::wstring> CachedSecondaryTileIds() { return g_secondaryPinInventory.Snapshot(); }
static bool SecondaryTileInventoryReady() { return g_secondaryPinInventory.Known(); }
static std::wstring SecondaryTileUiString(const wchar_t* key, const wchar_t* fallback) { return IniReadS(L"Strings", key, fallback); }
static void CompleteGallerySetup(HWND owner);
struct GalleryPinHistory {
    bool ready = false, pending = false;
    std::set<std::wstring> ids;
    std::wstring error;
};
static GalleryPinHistory g_galleryPinHistory;

static bool FindConfiguredGalleryTile(const std::wstring& id, aip::gallery::Configuration& configuration,
    aip::gallery::TileConfiguration& result)
{
    std::wstring error;
    if (!ReadGalleryConfiguration(configuration, error)) return false;
    for (const auto& tile : configuration.tiles)
        if (!id.empty() && id == aip::gallery::NativeTileId(configuration.groupId, tile.id)) { result = tile; return true; }
    return false;
}
static bool GalleryTilePinReady(const std::wstring& id)
{
    aip::gallery::Configuration configuration; aip::gallery::TileConfiguration tile;
    if (!FindConfiguredGalleryTile(id, configuration, tile) || aip::gallery::EffectiveFolder(configuration, tile).empty()) return false;
    std::wstring profile; { std::lock_guard<std::mutex> lock(g_pathMutex); profile = g_iniPath; }
    aip::gallery::Ledger ledger; std::wstring error;
    return ReadGalleryLedgerNative(aip::gallery::runtime::LedgerPath(aip::MakeAbsolutePath(profile)), ledger, error) ==
        aip::gallery::runtime::ReadResult::Ok && ledger.groupId == configuration.groupId;
}

static bool CurrentSecondaryAppearanceFingerprint(std::wstring& result)
{
    // Refresh through the established cache, then use one immutable document
    // for source/action and configured gallery appearances. Account/text
    // values never enter the fingerprint.
    std::wstring ignored;
    ReadIniValueCached(L"MediaActions", L"Branding", ignored);
    std::vector<IniSectionData> document;
    { std::lock_guard<std::mutex> lock(g_iniCacheMutex); if (!g_iniCacheLoaded) return false; document = g_iniCacheDoc; }
    const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        std::wstring value;
        return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
    };
    std::wstring error;
    std::wstring candidate;
    if (!aip::content::ReadSecondaryAppearanceFingerprint(read, candidate, error)) return false;
    aip::gallery::Configuration gallery;
    if (!aip::gallery::ReadConfiguration(read, gallery, error)) return false;
    if (!gallery.groupId.empty()) {
        // Identity selects the existing native destinations. Account fields,
        // folders, photo order/cadence, and ordinary text never trigger COM.
        candidate += L"Gallery:" + gallery.groupId + L":" + std::to_wstring(gallery.tiles.size()) + L";";
        for (const auto& tile : gallery.tiles)
            candidate += tile.id + L":" + tile.appearance.background + L":" + tile.appearance.branding + L":" + tile.appearance.backgroundColor + L";";
    }
    result = std::move(candidate); return true;
}
static void PollSecondaryTileAppearance()
{
#ifndef DESKTOPSTUB_INERT_HARDWARE
    if (g_hwnd && g_secondaryPinInventory.RequestDue(GetTickCount64()) &&
        !PostMessageW(g_hwnd, WM_APP_SECONDARY_PIN_INVENTORY, 0, 0))
        g_secondaryPinInventory.Complete(false, {}, GetTickCount64());
#endif
#if defined(DESKTOPSTUB_INERT_HARDWARE) && !defined(DESKTOPSTUB_TEST_APPEARANCE_POST)
    return;
#else
    if (!g_hwnd) return;
    std::wstring fingerprint;
    if (!CurrentSecondaryAppearanceFingerprint(fingerprint) || !g_secondaryAppearanceNotice.Observe(fingerprint)) return;
#ifdef DESKTOPSTUB_TEST_APPEARANCE_POST
    const bool posted = DESKTOPSTUB_TEST_APPEARANCE_POST(g_hwnd, WM_APP_SECONDARY_APPEARANCE_CHANGED);
#else
    const bool posted = PostMessageW(g_hwnd, WM_APP_SECONDARY_APPEARANCE_CHANGED, 0, 0) != FALSE;
#endif
    if (!posted) g_secondaryAppearanceNotice.PostFailed();
#endif
}

[[maybe_unused]] static std::wstring CanonicalSecondaryTileRuntimeId(std::wstring id)
{
    if (id.compare(0, std::wstring(aip::gallery::NativeTilePrefix).size(), aip::gallery::NativeTilePrefix) == 0) {
        aip::gallery::Configuration configuration; aip::gallery::TileConfiguration tile;
        return FindConfiguredGalleryTile(id, configuration, tile) ? id : std::wstring{};
    }
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
static bool IsKnownSecondaryMetadataId(const std::wstring& id)
{
    if (aip::tile_visual::IsKnownSecondaryArtworkId(id)) return true;
    if (id.compare(0, std::wstring(aip::gallery::NativeTilePrefix).size(), aip::gallery::NativeTilePrefix) != 0) return false;
    return !CanonicalSecondaryTileRuntimeId(id).empty();
}

#ifndef DESKTOPSTUB_INERT_HARDWARE
namespace secondary_ui {
using NativeTile = winrt::Windows::UI::StartScreen::SecondaryTile;
using BoolOperation = winrt::Windows::Foundation::IAsyncOperation<bool>;
using TileListOperation = winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Foundation::Collections::IVectorView<NativeTile>>;
using AsyncStatus = winrt::Windows::Foundation::AsyncStatus;
enum class Kind { Pin, Unpin, List, RefreshList, RefreshLogo, VerifyLogos, GalleryHistory };
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
    NativeTile tile{nullptr};
    BoolOperation mutation{nullptr};
    TileListOperation list{nullptr};
    aip::tile_visual::Assets expectedLogos;
    aip::content::PublishedTileAppearance expectedAppearance;
};
static std::shared_ptr<Apartment> apartment;
static DWORD uiThread = 0;
static HWND host = nullptr;
static std::shared_ptr<Mailbox> mailbox;
static std::map<std::uint64_t, Pending> pending;
static std::uint64_t nextId = 0;
static bool shuttingDown = false;
static bool logoRefreshAttempted = false;
static bool logoRefreshAgain = false;
static std::deque<NativeTile> logoRefreshQueue;
static bool logoEnumerationCompleted = false, logoVerificationStarted = false;
static std::map<std::wstring, aip::tile_visual::Assets> logoRefreshExpected;
static std::map<std::wstring, aip::content::PublishedTileAppearance> appearanceRefreshExpected;

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
static aip::content::TileAppearance ConfiguredAppearance(const std::wstring& id)
{
    if (id.compare(0, std::wstring(aip::gallery::NativeTilePrefix).size(), aip::gallery::NativeTilePrefix) == 0) {
        aip::gallery::Configuration gallery; aip::gallery::TileConfiguration tile;
        if (!FindConfiguredGalleryTile(id, gallery, tile)) throw winrt::hresult_error(E_INVALIDARG, L"Unknown gallery tile appearance.");
        return tile.appearance;
    }
    const auto section = id.compare(0, 10, L"AipAction_") == 0 ? std::wstring(L"MediaActions") :
        aip::content::SecondaryTileSection(id.substr(10));
    aip::content::TileAppearance appearance; std::wstring error;
    if (!aip::content::ReadTileAppearance(IniReadS, section, appearance, error))
        throw winrt::hresult_error(E_INVALIDARG, winrt::hstring(error));
    return appearance;
}
static aip::content::PublishedTileAppearance ReadAppearance(const NativeTile& tile)
{
    const auto visuals = tile.VisualElements(); const auto color = visuals.BackgroundColor();
    return {(static_cast<std::uint32_t>(color.A) << 24) | (static_cast<std::uint32_t>(color.R) << 16) |
        (static_cast<std::uint32_t>(color.G) << 8) | color.B,
        visuals.ShowNameOnSquare150x150Logo(), visuals.ShowNameOnWide310x150Logo(), visuals.ShowNameOnSquare310x310Logo(),
        visuals.ForegroundText() == winrt::Windows::UI::StartScreen::ForegroundText::Dark};
}
static void ApplyAppearance(NativeTile& tile, const aip::content::PublishedTileAppearance& appearance)
{
    auto visuals = tile.VisualElements();
    aip::content::ApplyPublishedTileAppearance(visuals, appearance, [](std::uint32_t argb) {
        return winrt::Windows::UI::Color{static_cast<BYTE>(argb >> 24), static_cast<BYTE>(argb >> 16),
            static_cast<BYTE>(argb >> 8), static_cast<BYTE>(argb)};
    }, winrt::Windows::UI::StartScreen::ForegroundText::Dark, winrt::Windows::UI::StartScreen::ForegroundText::Light);
}
static bool PrepareAppearanceAssets(const std::wstring& id, aip::tile_visual::Assets& logos,
    aip::content::PublishedTileAppearance& published, std::wstring& error)
{
    const auto configured = ConfiguredAppearance(id);
    const bool gallery = id.compare(0, std::wstring(aip::gallery::NativeTilePrefix).size(), aip::gallery::NativeTilePrefix) == 0;
    const auto topicKey = gallery ? std::wstring(L"gallery") : id;
    const auto* topic = aip::tile_visual::FindTopic(topicKey);
    if (!topic) { error = L"Unknown tile artwork topic."; return false; }
    published = aip::content::PublishTileAppearance(configured, topic->rgb);
    return aip::tile_visual::EnsureTileTopicAssets(topicKey, GetExeDir(), logos, error,
        {configured.background == L"Color", aip::content::TileAppearanceRgb(configured, topic->rgb)});
}
static NativeTile CreatePinTile(const std::wstring& id, const std::wstring& name)
{
    using namespace winrt::Windows::UI::StartScreen;
    aip::tile_visual::Assets logos;
    aip::content::PublishedTileAppearance appearance;
    std::wstring artworkError;
    if (!PrepareAppearanceAssets(id, logos, appearance, artworkError))
        throw winrt::hresult_error(E_FAIL, winrt::hstring(artworkError));
    const auto factory = aip::AcquireUncachedActivationFactory<NativeTile, ISecondaryTileFactory2>();
    std::wstring profile;
    { std::lock_guard<std::mutex> lock(g_pathMutex); profile = g_iniPath; }
    const bool action = id.compare(0, std::wstring(L"AipAction_").size(), L"AipAction_") == 0;
    auto activationKey = id.substr(10u);
    if (action) std::replace(activationKey.begin(), activationKey.end(), L'_', L'-');
    std::wstring arguments = L"--ini " + aip::QuoteCommandLineArg(profile);
    aip::gallery::Configuration gallery; aip::gallery::TileConfiguration galleryTile;
    if (FindConfiguredGalleryTile(id, gallery, galleryTile))
        arguments += L" --open-folder " + aip::QuoteCommandLineArg(aip::gallery::EffectiveFolder(gallery, galleryTile));
    else arguments += (action ? L" --media-action " : L" --tile-setup ") + activationKey;
    if (arguments.size() > 2048) throw winrt::hresult_error(E_INVALIDARG, L"The profile path is too long for Windows secondary tile activation arguments.");
    auto tile = factory.CreateMinimalTile(id, name, arguments, AssetUri(logos.medium), TileSize::Default);
    ApplyLogos(tile, logos);
    ApplyAppearance(tile, appearance);
    return tile;
}
static bool UserMutationPending()
{
    return std::any_of(pending.begin(), pending.end(), [](const auto& item) {
        return item.second.kind == Kind::Pin || item.second.kind == Kind::Unpin;
    });
}
static bool AppearanceRefreshPending()
{
    return !logoRefreshQueue.empty() || std::any_of(pending.begin(), pending.end(), [](const auto& item) {
        return item.second.kind == Kind::RefreshList || item.second.kind == Kind::RefreshLogo || item.second.kind == Kind::VerifyLogos;
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
            if (!IsKnownSecondaryMetadataId(state.id)) continue;
            aip::tile_visual::Assets logos; std::wstring error;
            if (!PrepareAppearanceAssets(state.id, logos, state.expectedAppearance, error)) {
                Log(L"[!] Source tile %ls artwork refresh skipped: %ls", state.id.c_str(), error.c_str()); continue;
            }
            state.expectedLogos = aip::tile_visual::LogoUris(logos);
            logoRefreshExpected[state.id] = state.expectedLogos;
            appearanceRefreshExpected[state.id] = state.expectedAppearance;
            const auto before = ReadLogos(state.tile);
            LogLogoMetadata(state.id, before);
            if (aip::tile_visual::SamePublishedLogos(before, state.expectedLogos) &&
                aip::content::SamePublishedTileAppearance(ReadAppearance(state.tile), state.expectedAppearance)) continue;
            // This is the existing FindAllAsync object. Preserve its ID, name,
            // arguments and current Start placement. Only configured logos,
            // background and branding are refreshed. No pin or
            // notification API is called by this migration.
            ApplyLogos(state.tile, logos);
            ApplyAppearance(state.tile, state.expectedAppearance);
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
    if (!shuttingDown && logoRefreshAgain && !AppearanceRefreshPending()) {
        logoRefreshAgain = false;
        ::RequestRefreshExistingSecondaryTileLogos(host, true);
    }
}
static bool RequestMutation(HWND owner, const std::wstring& supplied, const std::wstring& name, bool pin)
{
    const auto id = CanonicalSecondaryTileRuntimeId(supplied);
    if (id.empty() || !EnsureUi(owner)) return false;
    if (pin && id.compare(0, std::wstring(aip::gallery::NativeTilePrefix).size(), aip::gallery::NativeTilePrefix) == 0 && !GalleryTilePinReady(id)) {
        Report(owner, L"Set up this gallery and its permanent ownership history before pinning.");
        return false;
    }
    const auto keepApartment = apartment;
    if (std::any_of(pending.begin(), pending.end(), [](const auto& item) {
        return item.second.kind == Kind::Pin || item.second.kind == Kind::Unpin || item.second.kind == Kind::RefreshLogo ||
            item.second.kind == Kind::RefreshList || item.second.kind == Kind::VerifyLogos;
    })) {
        Report(owner, UserMutationPending() ? L"Finish or cancel the current Start tile confirmation before opening another." :
            L"Windows is finishing the current tile update. Please try this action again in a moment.");
        return false;
    }
    const auto request = ++nextId;
    Pending state; state.kind = pin ? Kind::Pin : Kind::Unpin; state.id = id; state.name = name.empty() ? id : name;
    try {
        if (pin) state.tile = CreatePinTile(id, state.name);
        else {
            const auto factory = aip::AcquireUncachedActivationFactory<NativeTile, winrt::Windows::UI::StartScreen::ISecondaryTileFactory>();
            state.tile = factory.CreateWithId(id);
        }
        winrt::check_hresult(state.tile.as<IInitializeWithWindow>()->Initialize(owner));
        if (pin) {
            const auto statics = aip::AcquireUncachedActivationFactory<NativeTile, winrt::Windows::UI::StartScreen::ISecondaryTileStatics>();
            state.updateExisting = statics.Exists(id);
            POINT anchor{}; GetCursorPos(&anchor);
            state.mutation = state.updateExisting ? state.tile.UpdateAsync() : state.tile.RequestCreateForSelectionAsync(
                winrt::Windows::Foundation::Rect{static_cast<float>(anchor.x), static_cast<float>(anchor.y), 1.0f, 1.0f});
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
        state.mutation = nullptr; state.tile = nullptr; state.list = nullptr;
        Report(owner, ErrorText(error));
    } catch (...) {
        const auto found = pending.find(request);
        if (found != pending.end()) { state = std::move(found->second); pending.erase(found); }
        try { if (state.mutation) state.mutation.Cancel(); } catch (...) {}
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
    return g_secondaryPinInventory.Contains(supplied); // No synchronous COM or full-set copy on poll/menu paths.
#endif
}
static void StartNextSecondaryPin()
{
#ifndef DESKTOPSTUB_INERT_HARDWARE
    if (secondary_ui::shuttingDown || secondary_ui::UserMutationPending() || secondary_ui::AppearanceRefreshPending()) return;
    g_secondaryPinBatch.Advance([](const auto& item) {
        return secondary_ui::RequestMutation(secondary_ui::host, item.first, item.second, true);
    });
#endif
}
static bool RequestSecondaryTilePinBatch(HWND owner, const std::vector<std::pair<std::wstring, std::wstring>>& requests)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    (void)owner; (void)requests; return false;
#else
    if (!secondary_ui::EnsureUi(owner)) return false;
    aip::gallery::Configuration gallery; std::wstring error;
    std::set<std::wstring> galleryIds;
    if (ReadGalleryConfiguration(gallery, error)) for (const auto& tile : gallery.tiles)
        galleryIds.insert(aip::gallery::NativeTileId(gallery.groupId, tile.id));
    std::vector<std::pair<std::wstring, std::wstring>> normalized;
    for (const auto& item : requests) {
        const bool galleryId = item.first.compare(0, std::wstring(aip::gallery::NativeTilePrefix).size(), aip::gallery::NativeTilePrefix) == 0;
        const auto id = galleryId ? (galleryIds.count(item.first) ? item.first : std::wstring{}) : CanonicalSecondaryTileRuntimeId(item.first);
        if (id.empty()) return false; // Validate the complete batch before enqueueing anything.
        normalized.emplace_back(id, item.second.empty() ? id : item.second);
    }
    g_secondaryPinBatch.Enqueue(normalized); StartNextSecondaryPin(); return !normalized.empty();
#endif
}
// Explicit redistribution only: the caller has confirmed, paused gallery work
// and drained generation before calling. Never pin/unpin, rename or move tiles.
static bool ClearGalleryQueuesForRebalance(const std::vector<std::wstring>& ids, std::wstring& error)
{
#ifdef DESKTOPSTUB_TEST_GALLERY_CLEAR_QUEUES
    return DESKTOPSTUB_TEST_GALLERY_CLEAR_QUEUES(ids, error);
#elif defined(DESKTOPSTUB_INERT_HARDWARE)
    (void)ids; error = L"Gallery queue changes are unavailable in inert validation."; return false;
#else
    if (!secondary_ui::EnsureUi(g_hwnd, true)) { error = L"Open the registered DesktopStub app before redistributing gallery photos."; return false; }
    std::set<std::wstring> unique(ids.begin(), ids.end());
    for (const auto& id : unique) if (id.compare(0, 11, L"AipGallery_") != 0 || id.size() > aip::gallery::NativeTileMaxLength) {
        error = L"The gallery history contains an invalid native tile ID."; return false;
    }
    try {
        const auto tiles = aip::AcquireUncachedActivationFactory<secondary_ui::NativeTile, winrt::Windows::UI::StartScreen::ISecondaryTileStatics>();
        const auto updates = aip::AcquireUncachedActivationFactory<winrt::Windows::UI::Notifications::TileUpdateManager,
            winrt::Windows::UI::Notifications::ITileUpdateManagerStatics>();
        for (const auto& id : unique) if (tiles.Exists(id)) {
            updates.CreateTileUpdaterForSecondaryTile(id).Clear();
            ForgetSecondaryTileDelivery(id);
        }
        error.clear(); return true;
    } catch (const winrt::hresult_error& failure) { error = secondary_ui::ErrorText(failure); }
    catch (...) { error = L"Windows could not clear every existing gallery queue. Ownership history was kept."; }
    return false;
#endif
}
static bool RequestPinSecondaryTile(HWND owner, const std::wstring& id, const std::wstring& displayName)
{
    return RequestSecondaryTilePinBatch(owner, {{id, displayName}});
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
    if (!EnsureUi(owner, true)) { g_secondaryPinInventory.Complete(false, {}, GetTickCount64()); return false; }
    const auto keepApartment = apartment;
    if (std::any_of(pending.begin(), pending.end(), [](const auto& item) { return item.second.kind == Kind::List || item.second.kind == Kind::RefreshList || item.second.kind == Kind::VerifyLogos; })) return true;
    const auto request = ++nextId;
    try {
        Pending state; state.kind = Kind::List;
        const auto factory = aip::AcquireUncachedActivationFactory<NativeTile, winrt::Windows::UI::StartScreen::ISecondaryTileStatics>();
        state.list = factory.FindAllAsync();
        auto inserted = pending.emplace(request, std::move(state));
        const std::weak_ptr<Mailbox> weak = mailbox; const auto destination = host;
        inserted.first->second.list.Completed([weak, destination, request](const auto&, AsyncStatus status) noexcept { PostCompletion(weak, destination, request, status); });
        return true;
    } catch (const winrt::hresult_error& error) { pending.erase(request); Log(L"[!] Pin inventory: %ls", ErrorText(error).c_str()); }
    catch (...) { pending.erase(request); LogText(L"[!] Could not refresh the cached pinned tile list."); }
    g_secondaryPinInventory.Complete(false, {}, GetTickCount64()); return false;
#endif
}
// Normal startup upgrade hook. Existing pins are refreshed in place; missing
// destinations are never pinned and current notifications are never cleared.
static bool RequestRefreshExistingSecondaryTileLogos(HWND owner, bool force)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    (void)owner; (void)force; return false;
#else
    using namespace secondary_ui;
    if (!EnsureUi(owner, true)) return false;
    std::wstring scheduledFingerprint;
    if (CurrentSecondaryAppearanceFingerprint(scheduledFingerprint)) g_secondaryAppearanceNotice.RecordScheduled(scheduledFingerprint);
    const auto keepApartment = apartment;
    if (AppearanceRefreshPending() || UserMutationPending()) { if (force) logoRefreshAgain = true; return true; }
    if (logoRefreshAttempted && !force) return true;
    logoEnumerationCompleted = false; logoVerificationStarted = false;
    logoRefreshExpected.clear(); appearanceRefreshExpected.clear();
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
    const auto ids = CachedSecondaryTileIds(); return {ids.begin(), ids.end()};
}
// Unlike the normal source menu cache, first setup must enumerate every
// gallery-prefixed pin, including IDs absent from the current configuration.
// An empty cache or failed enumeration is never evidence of empty history.
static bool RequestGalleryPinHistory(HWND owner)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    (void)owner; return false;
#else
    using namespace secondary_ui;
    if (!EnsureUi(owner) || g_galleryPinHistory.pending || UserMutationPending()) return false;
    g_galleryPinHistory = {}; g_galleryPinHistory.pending = true;
    if (!SetTimer(host, GALLERY_HISTORY_TIMER, 15000, nullptr)) {
        g_galleryPinHistory.pending = false; g_galleryPinHistory.error = L"Windows could not start the bounded gallery history check."; return false;
    }
    const auto keepApartment = apartment;
    const auto request = ++nextId;
    try {
        Pending state; state.kind = Kind::GalleryHistory;
        const auto factory = aip::AcquireUncachedActivationFactory<NativeTile, winrt::Windows::UI::StartScreen::ISecondaryTileStatics>();
        state.list = factory.FindAllAsync();
        auto inserted = pending.emplace(request, std::move(state));
        const std::weak_ptr<Mailbox> weak = mailbox; const auto destination = host;
        inserted.first->second.list.Completed([weak, destination, request](const auto&, AsyncStatus status) noexcept {
            PostCompletion(weak, destination, request, status);
        });
        return true;
    } catch (const winrt::hresult_error& error) {
        pending.erase(request); g_galleryPinHistory.pending = false; g_galleryPinHistory.error = ErrorText(error);
    } catch (...) {
        pending.erase(request); g_galleryPinHistory.pending = false; g_galleryPinHistory.error = L"Windows could not enumerate existing gallery pins. No history was initialized.";
    }
    KillTimer(host, GALLERY_HISTORY_TIMER);
    return false;
#endif
}
static bool SecondaryTileHandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)wParam; (void)lParam;
    if (message == WM_APP_SECONDARY_PIN_INVENTORY && window == g_hwnd) {
#ifndef DESKTOPSTUB_INERT_HARDWARE
        RequestSecondaryTileList(window);
#endif
        return true;
    }
#ifndef DESKTOPSTUB_INERT_HARDWARE
    if (message == WM_TIMER && wParam == GALLERY_HISTORY_TIMER && window == secondary_ui::host) {
        KillTimer(window, GALLERY_HISTORY_TIMER);
        if (!g_galleryPinHistory.pending) return true; // An already queued timer cannot invalidate a completed enumeration.
        // Cancel and release on the owning UI thread before posting a later
        // UI notification. Late completions cannot satisfy a new setup check.
        for (auto entry = secondary_ui::pending.begin(); entry != secondary_ui::pending.end();) {
            if (entry->second.kind != secondary_ui::Kind::GalleryHistory) { ++entry; continue; }
            auto state = std::move(entry->second); entry = secondary_ui::pending.erase(entry);
            try { if (state.list) state.list.Cancel(); } catch (...) {}
        }
        g_galleryPinHistory = {}; g_galleryPinHistory.error = L"Windows did not finish checking gallery pins within 15 seconds. Try setup again; no ownership history was created.";
        PostMessageW(window, WM_APP_GALLERY_SETUP_READY, 0, 0); return true;
    }
#endif
    if (message == WM_APP_GALLERY_SETUP_READY && window == g_hwnd) {
#ifndef DESKTOPSTUB_INERT_HARDWARE
        CompleteGallerySetup(window);
#endif
        return true;
    }
    if (message == WM_APP_SECONDARY_APPEARANCE_CHANGED && window == g_hwnd) {
        if (g_secondaryAppearanceNotice.Consume()) {
#ifdef DESKTOPSTUB_TEST_APPEARANCE_REFRESH
            DESKTOPSTUB_TEST_APPEARANCE_REFRESH(window);
#elif !defined(DESKTOPSTUB_INERT_HARDWARE)
            RequestRefreshExistingSecondaryTileLogos(window, true);
#endif
        }
        return true;
    }
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
            if (state.kind == Kind::GalleryHistory) {
                std::set<std::wstring> history;
                for (const auto& tile : state.list.GetResults()) {
                    const auto id = std::wstring(tile.TileId());
                    if (id.compare(0, std::wstring(aip::gallery::NativeTilePrefix).size(), aip::gallery::NativeTilePrefix) == 0)
                        history.insert(id);
                }
                g_galleryPinHistory.ids = std::move(history);
                g_galleryPinHistory.ready = true; g_galleryPinHistory.error.clear();
            } else if (state.kind == Kind::List || state.kind == Kind::RefreshList || state.kind == Kind::VerifyLogos) {
                std::set<std::wstring> result, allIds;
                for (const auto& tile : state.list.GetResults()) {
                    const auto id = std::wstring(tile.TileId());
                    allIds.insert(id); // Includes historic galleries absent from the current configuration.
                    if (state.kind == Kind::List) continue; // Ordinary pin reconciliation needs no profile parsing or artwork inspection.
                    if (!IsKnownSecondaryMetadataId(id) || !result.insert(id).second) continue;
                    if (state.kind == Kind::RefreshList) logoRefreshQueue.push_back(tile);
                    if (state.kind == Kind::VerifyLogos) {
                        const auto expected = logoRefreshExpected.find(id);
                        if (expected != logoRefreshExpected.end()) {
                            const auto actual = ReadLogos(tile);
                            const auto appearance = appearanceRefreshExpected.find(id);
                            const bool matches = aip::tile_visual::SamePublishedLogos(actual, expected->second) &&
                                appearance != appearanceRefreshExpected.end() &&
                                aip::content::SamePublishedTileAppearance(ReadAppearance(tile), appearance->second);
                            LogLogoMetadata(id, actual);
                            Log(L"%ls Source tile %ls topic logo metadata %ls after update.",
                                matches ? L"[+]" : L"[!]", id.c_str(),
                                matches ? L"verified with configured background/branding" : L"did not match the requested appearance");
                        }
                    }
                }
                if (state.kind == Kind::RefreshList) logoEnumerationCompleted = true;
                if (state.kind == Kind::VerifyLogos) for (const auto& expected : logoRefreshExpected)
                    if (!result.count(expected.first)) Log(L"Source tile %ls is no longer pinned; metadata refresh did not recreate it.", expected.first.c_str());
                g_secondaryPinInventory.Complete(true, allIds, GetTickCount64());
                LogText(L"Pinned source tile list refreshed.");
            } else {
                const bool accepted = state.mutation.GetResults(); // Terminal only, no blocking wait/get.
                if (accepted) {
                    if (state.kind == Kind::RefreshLogo) {
                        Log(L"Source tile %ls topic artwork metadata published: %ls", state.id.c_str(), state.expectedLogos.medium.c_str());
                    } else {
                        g_secondaryPinInventory.Mutation(state.id, state.kind == Kind::Pin);
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
            if (state.kind == Kind::GalleryHistory) g_galleryPinHistory.error = ErrorText(error);
            if (state.kind == Kind::List || state.kind == Kind::RefreshList || state.kind == Kind::VerifyLogos)
                g_secondaryPinInventory.Complete(false, {}, GetTickCount64());
        } catch (...) {
            LogText(L"[!] Secondary tile: could not retrieve the completed request.");
            if (state.kind == Kind::List || state.kind == Kind::RefreshList || state.kind == Kind::VerifyLogos)
                g_secondaryPinInventory.Complete(false, {}, GetTickCount64());
        }
        state.mutation = nullptr; state.tile = nullptr; state.list = nullptr;
        if (state.kind == Kind::Pin) g_secondaryPinBatch.Complete(state.id);
        if (state.kind == Kind::GalleryHistory) {
            KillTimer(window, GALLERY_HISTORY_TIMER);
            g_galleryPinHistory.pending = false;
            if (!g_galleryPinHistory.ready && g_galleryPinHistory.error.empty())
                g_galleryPinHistory.error = L"Windows could not confirm gallery pin history. No configuration was saved.";
            PostMessageW(window, WM_APP_GALLERY_SETUP_READY, 0, 0);
        }
        StartNextLogoRefresh();
        StartNextSecondaryPin();
        WakePollThread();
    }
    return true;
#endif
}
static void ShutdownSecondaryTileUi()
{
    g_secondaryPinBatch.Clear();
    g_galleryPinHistory = {};
#ifndef DESKTOPSTUB_INERT_HARDWARE
    using namespace secondary_ui;
    shuttingDown = true;
    if (!apartment || GetCurrentThreadId() != uiThread) return;
    KillTimer(host, GALLERY_HISTORY_TIMER);
    { std::lock_guard<std::mutex> lock(mailbox->mutex); mailbox->accepting = false; mailbox->complete.clear(); }
    for (auto& item : pending) {
        try { if (item.second.mutation) item.second.mutation.Cancel(); if (item.second.list) item.second.list.Cancel(); } catch (...) {}
    }
    pending.clear(); logoRefreshQueue.clear(); logoRefreshExpected.clear(); appearanceRefreshExpected.clear(); mailbox.reset();
    apartment.reset(); uiThread = 0; host = nullptr;
#endif
}
