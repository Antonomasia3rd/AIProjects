// User-directed HoYoLAB embedding. Browser storage is separate from imported
// Notes credentials; local cleanup never invokes a server-side logout.
#ifndef DESKTOPSTUB_INERT_HARDWARE
#include "../third_party/WebView2/WebView2.h"
#include "../content_sources/notes_accounts.inc"
#include "../content_sources/notes_signin_workers.h"
#include <wrl.h>
#include <atomic>

namespace notes_signin {
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
static constexpr UINT WM_SAVE_ACCOUNT = WM_APP + 171, WM_ACCOUNTS_READY = WM_APP + 172,
    WM_CLEAR_LOCAL_READY = WM_APP + 173, WM_CLEAR_LOCAL_RESULT = WM_APP + 174;
static constexpr int ID_FIND = 110, ID_MANUAL = 111, ID_ACCOUNTS = 112, ID_RETRY = 113,
    ID_GAME_PAGE = 114, ID_CLEAR_LOCAL = 115, ID_RETAIN = 116;
struct Lookup {
    std::atomic<bool> canceled{false}, done{false};
    unsigned long long request = 0;
    bool saving = false, protect = false, success = false;
    std::wstring uid, server, token, user, error;
    std::vector<aip::notes::BoundGameAccount> accounts;
};
struct State {
    HWND window = nullptr, uid = nullptr, protect = nullptr, import = nullptr, status = nullptr,
        accounts = nullptr, manual = nullptr, find = nullptr, retain = nullptr;
    int resource = 0;
    bool closed = false, busy = false, retained = false, processFailed = false, cleanupSlow = false;
    unsigned long long generation = 0, request = 0, cleanupSequence = 0;
    aip::notes::LocalBrowserCleanup cleanup;
    HRESULT cleanupResult = E_PENDING;
    std::chrono::steady_clock::time_point cleanupDeadline{};
    UINT64 navigation = 0;
    std::wstring profile, browserData;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> view;
    ComPtr<ICoreWebView2Profile2> cleanupProfile;
    std::shared_ptr<Lookup> lookup;
    std::vector<aip::notes::BoundGameAccount> choices;
    std::vector<aip::IniSetting> pending;
};
static std::map<HWND, std::shared_ptr<State>> windows;
static aip::notes::SignInWorkers workers;
static bool Live(const std::shared_ptr<State>& state) { return !state->closed && IsWindow(state->window); }
static bool CurrentBrowser(const std::shared_ptr<State>& state, unsigned long long generation) {
    return Live(state) && generation == state->generation;
}
static void SetSignInStatus(const std::shared_ptr<State>& state, const std::wstring& text) {
    if (Live(state)) SetWindowTextW(state->status, text.c_str());
}
static void Failure(const std::shared_ptr<State>& state, const wchar_t* operation, HRESULT code) {
    wchar_t value[24]{}; swprintf_s(value, L"0x%08lX", static_cast<unsigned long>(code));
    SetSignInStatus(state, std::wstring(operation) + L" (" + value + L"). Select Retry. No account was changed.");
}
static void SetBusy(const std::shared_ptr<State>& state, bool busy) {
    state->busy = busy;
    if (!Live(state)) return;
    EnableWindow(state->find, !busy && state->view && !state->processFailed && !state->cleanup.Pending());
    EnableWindow(state->import, !busy && state->view && !state->processFailed && !state->cleanup.Pending());
    EnableWindow(state->accounts, !busy);
    EnableWindow(state->manual, !busy);
    EnableWindow(state->retain, !state->cleanup.Pending());
    EnableWindow(state->uid, !busy && SendMessageW(state->manual, BM_GETCHECK, 0, 0) == BST_CHECKED);
}
static void CancelRequests(const std::shared_ptr<State>& state, bool clearChoices) {
    ++state->request;
    if (state->lookup) state->lookup->canceled = true;
    state->lookup.reset(); state->pending.clear();
    if (clearChoices) { state->choices.clear(); SendMessageW(state->accounts, CB_RESETCONTENT, 0, 0); }
    SetBusy(state, false);
}
static bool HoYoDomain(std::wstring domain) {
    return aip::notes::IsHoYoLabCookieDomain(std::move(domain));
}
static bool CookieString(ICoreWebView2Cookie* cookie, HRESULT (STDMETHODCALLTYPE ICoreWebView2Cookie::*get)(LPWSTR*),
    std::wstring& value, std::size_t maximum) {
    LPWSTR raw = nullptr;
    if (FAILED((cookie->*get)(&raw)) || !raw) return false;
    const auto count = wcsnlen_s(raw, maximum + 1);
    if (count <= maximum) value.assign(raw, count);
    CoTaskMemFree(raw); return count <= maximum;
}
static void ResizeSignInView(const std::shared_ptr<State>& state) {
    if (!Live(state)) return;
    RECT bounds{}; GetClientRect(state->window, &bounds);
    MoveWindow(state->status, 12, 145, (std::max)(120L, bounds.right - 24), 48, TRUE);
    if (state->controller) { bounds.top = 200; state->controller->put_Bounds(bounds); }
}
static void StageAccount(const std::shared_ptr<State>& state, const std::wstring& uid, const std::wstring& server,
    const std::wstring& token, const std::wstring& user, bool protect) {
    if (!Live(state) || !IEquals(aip::MakeAbsolutePath(g_iniPath), state->profile)) {
        SetBusy(state, false); SetSignInStatus(state, L"The active profile changed. Reopen sign-in; no account was saved."); return;
    }
    std::string json = "{"; bool first = true;
    aip::JsonFieldString(json, first, "UID", uid); aip::JsonFieldString(json, first, "Server", server);
    aip::JsonFieldString(json, first, protect ? "LTokenProtected" : "LToken", protect ? aip::ProtectSecretForCurrentUser(token) : token);
    aip::JsonFieldString(json, first, protect ? "LTUidProtected" : "LTUid", protect ? aip::ProtectSecretForCurrentUser(user) : user);
    const auto section = aip::notes::HostAccountSection(state->resource);
    int refresh = 0;
    if (!aip::content::ParseInt(IniReadS(section.c_str(), L"RefreshSeconds", IniReadS(section.c_str(), L"RefreshIntervalSeconds", L"0").c_str()), 0, 86400, refresh)) throw std::runtime_error("refresh");
    aip::JsonFieldNumber(json, first, "RefreshSeconds", refresh); json += '}';
    std::wstring error;
    if (!aip::notes::ImportCookieJson(state->resource, aip::Utf8ToWide(json), state->pending, error) ||
        !PostMessageW(state->window, WM_SAVE_ACCOUNT, static_cast<WPARAM>(state->request), 0)) throw std::runtime_error("stage");
}
static void BeginLookup(const std::shared_ptr<State>& state, const std::wstring& token, const std::wstring& user,
    bool v2, bool saving, const std::wstring& uid, const std::wstring& server, bool protect) {
    aip::HttpFetchOptions request; std::wstring error;
    const auto resource = static_cast<aip::notes::ResourceKind>(state->resource);
    if (!aip::notes::BuildBoundGameAccountsRequest(resource, token, user, v2, request, error)) {
        SetBusy(state, false); SetSignInStatus(state, error); return;
    }
    const auto work = std::make_shared<Lookup>();
    work->request = state->request; work->saving = saving; work->protect = protect; work->uid = uid; work->server = server;
    if (saving) { work->token = token; work->user = user; }
    const std::weak_ptr<Lookup> weakWork = work;
    request.canceled = [weakWork] { const auto current = weakWork.lock(); return !current || current->canceled.load(); };
    state->lookup = work;
    const HWND window = state->window;
    // Only copied HTTP inputs/results cross threads. Closing/resetting cancels
    // publication; this bounded worker never reads COM or host configuration.
    if (!workers.Start([weakWork] { if (const auto current = weakWork.lock()) current->canceled = true; }, [work, request, resource, window] {
        work->success = aip::notes::FetchBoundGameAccounts(request, resource, work->accounts, work->error);
        work->done.store(true, std::memory_order_release);
        if (!work->canceled.load()) PostMessageW(window, WM_ACCOUNTS_READY, static_cast<WPARAM>(work->request), 0);
    })) {
        work->canceled = true; state->lookup.reset(); SetBusy(state, false);
        SetSignInStatus(state, L"Could not start game-account lookup. Retry or use manual UID entry. No account was saved."); return;
    }
    SetSignInStatus(state, saving ? L"Checking the selected game account against this browser session..." : L"Finding accounts bound to this HoYoLAB sign-in...");
}
static void ReadCookies(const std::shared_ptr<State>& state, bool saving) {
    if (!Live(state) || !state->view || state->busy || state->cleanup.Pending() || state->processFailed) return;
    std::wstring uid, server, issue;
    const bool manual = SendMessageW(state->manual, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (saving && manual) {
        wchar_t buffer[34]{}; GetWindowTextW(state->uid, buffer, _countof(buffer)); uid = buffer;
        if (uid.empty() || !aip::content::NormalizeSetting(aip::notes::HostAccountSection(state->resource), L"UID", uid, issue)) {
            SetSignInStatus(state, L"Enter this game's numeric UID from the in-game profile. It is different from the HoYoLAB account ID."); return;
        }
    } else if (saving) {
        const auto selected = SendMessageW(state->accounts, CB_GETCURSEL, 0, 0);
        if (selected == CB_ERR || static_cast<std::size_t>(selected) >= state->choices.size()) {
            SetSignInStatus(state, L"Select Find game accounts, then choose the desired UID/server. If discovery is unavailable, select manual UID entry."); return;
        }
        uid = state->choices[static_cast<std::size_t>(selected)].uid; server = state->choices[static_cast<std::size_t>(selected)].server;
    }
    CancelRequests(state, false); SetBusy(state, true);
    const auto request = state->request, generation = state->generation;
    const bool protect = SendMessageW(state->protect, BM_GETCHECK, 0, 0) == BST_CHECKED;
    ComPtr<ICoreWebView2_2> view2; ComPtr<ICoreWebView2CookieManager> manager;
    if (FAILED(state->view.As(&view2)) || FAILED(view2->get_CookieManager(&manager))) {
        SetBusy(state, false); SetSignInStatus(state, L"Update WebView2 Runtime to read this embedded session."); return;
    }
    const std::weak_ptr<State> weak = state;
    const HRESULT requested = manager->GetCookies(L"https://act.hoyolab.com/",
        Callback<ICoreWebView2GetCookiesCompletedHandler>([weak, request, generation, saving, manual, uid, server, protect](HRESULT result, ICoreWebView2CookieList* cookies) -> HRESULT {
            const auto state = weak.lock(); if (!state || !CurrentBrowser(state, generation) || request != state->request) return S_OK;
            try {
                UINT count = 0;
                if (FAILED(result) || !cookies || FAILED(cookies->get_Count(&count)) || count > 512) throw std::runtime_error("cookies");
                std::map<std::wstring, std::wstring> values;
                for (UINT index = 0; index < count; ++index) {
                    ComPtr<ICoreWebView2Cookie> cookie; std::wstring domain, name, value;
                    if (FAILED(cookies->GetValueAtIndex(index, &cookie)) || !cookie ||
                        !CookieString(cookie.Get(), &ICoreWebView2Cookie::get_Domain, domain, 255) || !HoYoDomain(domain) ||
                        !CookieString(cookie.Get(), &ICoreWebView2Cookie::get_Name, name, 255)) continue;
                    name = aip::ToLower(name);
                    if (name != L"ltoken" && name != L"ltuid" && name != L"ltoken_v2" && name != L"ltuid_v2") continue;
                    if (!CookieString(cookie.Get(), &ICoreWebView2Cookie::get_Value, value, 4096)) throw std::runtime_error("size");
                    const auto inserted = values.emplace(name, value);
                    if (!inserted.second && inserted.first->second != value) throw std::runtime_error("ambiguous");
                }
                const bool v2 = !values[L"ltoken_v2"].empty() && !values[L"ltuid_v2"].empty();
                const auto token = v2 ? values[L"ltoken_v2"] : values[L"ltoken"];
                const auto user = v2 ? values[L"ltuid_v2"] : values[L"ltuid"];
                if (token.empty() || user.empty()) {
                    SetBusy(state, false); SetSignInStatus(state, L"Complete HoYoLAB sign-in and any security verification, then select Find game accounts. No account was saved."); return S_OK;
                }
                if (saving && manual) StageAccount(state, uid, L"", token, user, protect);
                else BeginLookup(state, token, user, v2, saving, uid, server, protect);
            } catch (...) {
                state->pending.clear(); SetBusy(state, false);
                SetSignInStatus(state, L"Could not read/import this session safely. Check sign-in and retry. No account was saved.");
            }
            return S_OK;
        }).Get());
    if (FAILED(requested)) { SetBusy(state, false); Failure(state, L"Reading this embedded session failed", requested); }
}
static void AccountsReady(const std::shared_ptr<State>& state) {
    const auto work = state->lookup;
    if (!work || !work->done.load(std::memory_order_acquire) ||
        !aip::notes::SignInResultCurrent(work->request, state->request, state->closed, work->canceled.load())) return;
    state->lookup.reset();
    if (!work->success) { SetBusy(state, false); SetSignInStatus(state, work->error); return; }
    if (work->saving) {
        const auto found = std::find_if(work->accounts.begin(), work->accounts.end(), [&](const auto& account) { return account.uid == work->uid && account.server == work->server; });
        if (found == work->accounts.end()) {
            SetBusy(state, false); SetSignInStatus(state, L"The selected UID/server does not belong to this session. Find game accounts again; no account was saved."); return;
        }
        StageAccount(state, work->uid, work->server, work->token, work->user, work->protect); return;
    }
    state->choices = work->accounts; SendMessageW(state->accounts, CB_RESETCONTENT, 0, 0);
    for (const auto& account : state->choices) {
        const auto label = account.uid + L" - " + (account.serverName.empty() ? account.server : account.serverName) + (account.nickname.empty() ? L"" : L" - " + account.nickname);
        SendMessageW(state->accounts, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(state->accounts, CB_SETCURSEL, state->choices.size() == 1 ? 0 : static_cast<WPARAM>(-1), 0);
    SendMessageW(state->manual, BM_SETCHECK, BST_UNCHECKED, 0); SetBusy(state, false);
    SetSignInStatus(state, state->choices.empty() ? L"No accounts for this game were returned. Check the HoYoLAB account, or select manual UID entry." :
        state->choices.size() == 1 ? L"Game account found. Check the UID/server, then select Save selected account. Nothing has been saved yet." :
        L"Several game accounts were found. Choose a UID/server before saving; no account was selected automatically.");
}
static void StartBrowser(const std::shared_ptr<State>& state);
static void CloseBrowser(const std::shared_ptr<State>& state) {
    ++state->generation; ++state->cleanupSequence; state->cleanup.Abandon(); state->cleanupProfile.Reset();
    CancelRequests(state, true); KillTimer(state->window, 1); KillTimer(state->window, 2);
    if (state->controller) state->controller->Close();
    state->view.Reset(); state->controller.Reset(); state->environment.Reset(); SetBusy(state, false);
}
static void PostCleanupResult(const std::shared_ptr<State>& state, HRESULT result) {
    state->cleanupResult = result; KillTimer(state->window, 2);
    PostMessageW(state->window, WM_CLEAR_LOCAL_RESULT, static_cast<WPARAM>(state->cleanupSequence), 0);
}
static void BeginClearLocalData(const std::shared_ptr<State>& state) {
    if (!Live(state) || state->cleanup.Phase() != aip::notes::LocalCleanupPhase::Clearing || !state->cleanupProfile) return;
    SetSignInStatus(state, L"The page is stopped. Clearing local browser data; saved Notes credentials are kept. Close waits for completion.");
    const auto generation = state->generation, sequence = state->cleanupSequence;
    const std::weak_ptr<State> weak = state;
    const HRESULT requested = state->cleanupProfile->ClearBrowsingDataAll(Callback<ICoreWebView2ClearBrowsingDataCompletedHandler>([weak, generation, sequence](HRESULT result) -> HRESULT {
        const auto state = weak.lock();
        if (!state || !CurrentBrowser(state, generation) || sequence != state->cleanupSequence || !state->cleanup.Complete(sequence, SUCCEEDED(result))) return S_OK;
        PostCleanupResult(state, result); return S_OK;
    }).Get());
    if (FAILED(requested) && state->cleanup.Fail(sequence)) PostCleanupResult(state, requested);
}
static void FinishLocalCleanup(const std::shared_ptr<State>& state) {
    if (!Live(state) || state->cleanup.Pending() ||
        (state->cleanup.Phase() != aip::notes::LocalCleanupPhase::Completed && state->cleanup.Phase() != aip::notes::LocalCleanupPhase::Failed)) return;
    const bool close = state->cleanup.CloseRequested(), success = SUCCEEDED(state->cleanupResult);
    state->cleanupProfile.Reset(); KillTimer(state->window, 2); SetBusy(state, false);
    if (state->controller) state->controller->put_IsVisible(TRUE);
    if (close) {
        if (!success) DESKTOPSTUB_CONTENT_MESSAGEBOX(state->window,
            L"Local browser cleanup did not finish successfully. Some browser data may remain. Imported Notes credentials were not changed.",
            L"Browser cleanup", MB_OK | MB_ICONWARNING);
        if (Live(state)) DestroyWindow(state->window);
    } else if (success) {
        CloseBrowser(state); StartBrowser(state);
        SetSignInStatus(state, L"Local browser data cleared. Sign in with any account. Imported Notes credentials were kept.");
    } else Failure(state, L"Local browser cleanup did not complete; some local data may remain", state->cleanupResult);
}
static void ClearLocalSession(const std::shared_ptr<State>& state) {
    if (!Live(state) || !state->view || state->cleanup.Pending()) return;
    if (DESKTOPSTUB_CONTENT_MESSAGEBOX(state->window,
        L"Clear cookies, cache, and other data for the current embedded browser session? Saved Notes credentials and other games are kept. This does not call HoYoLAB Logout. In fresh mode, an earlier remembered session is kept; select Remember first to clear that stored profile instead.",
        L"Clear local sign-in data", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES) return;
    if (!Live(state) || !state->view) return;
    ComPtr<ICoreWebView2_13> view13; ComPtr<ICoreWebView2Profile> profile; ComPtr<ICoreWebView2Profile2> profile2;
    if (FAILED(state->view.As(&view13)) || FAILED(view13->get_Profile(&profile)) || FAILED(profile.As(&profile2))) {
        SetSignInStatus(state, L"Update WebView2 Runtime to clear local browser storage. Saved Notes credentials were not changed."); return;
    }
    CancelRequests(state, true); state->cleanup.Begin(++state->cleanupSequence); state->cleanupResult = E_PENDING;
    state->cleanupSlow = false; state->cleanupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    state->cleanupProfile = profile2; SetBusy(state, true); KillTimer(state->window, 1);
    SetSignInStatus(state, L"Stopping the game page before clearing this local browser session. Imported Notes credentials are kept. Close waits for completion.");
    if (state->controller) state->controller->put_IsVisible(FALSE);
    state->view->Stop(); SetTimer(state->window, 2, 30000, nullptr);
    const auto navigating = state->view->Navigate(L"about:blank");
    if (FAILED(navigating) && state->cleanup.Fail(state->cleanupSequence)) PostCleanupResult(state, navigating);
}
static bool AllowedNavigation(const std::wstring& uri) {
    // Official sign-in may redirect to account.hoyoverse.com or an HTTPS identity
    // provider. URI values/tickets are never logged.
    return uri == L"about:blank" || aip::StartsWithI(uri, L"https://");
}
static void ConfigureBrowser(const std::shared_ptr<State>& state, ICoreWebView2Controller* controller, unsigned long long generation) {
    state->controller = controller;
    const auto got = controller->get_CoreWebView2(&state->view);
    if (FAILED(got) || !state->view) { Failure(state, L"The embedded view is unavailable", got); return; }
    const std::weak_ptr<State> weak = state; EventRegistrationToken event{};
    state->view->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
        [weak, generation](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            const auto state = weak.lock(); if (!state || !CurrentBrowser(state, generation) || !args) return S_OK;
            LPWSTR raw = nullptr;
            UINT64 navigation = 0; args->get_NavigationId(&navigation);
            const bool gotUri = SUCCEEDED(args->get_Uri(&raw)) && raw;
            if (state->cleanup.Pending()) {
                const bool allowed = gotUri && state->cleanup.AllowNavigation(state->cleanupSequence, raw, navigation);
                args->put_Cancel(!allowed); CoTaskMemFree(raw); return S_OK;
            }
            if (!gotUri || !AllowedNavigation(raw)) args->put_Cancel(TRUE);
            CoTaskMemFree(raw); state->navigation = navigation;
            CancelRequests(state, true); SetSignInStatus(state, L"Loading the official game/sign-in page. Complete any account security verification in the page."); SetTimer(state->window, 1, 30000, nullptr);
            return S_OK;
        }).Get(), &event);
    state->view->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [weak, generation](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
            const auto state = weak.lock(); if (!state || !CurrentBrowser(state, generation) || !args) return S_OK;
            UINT64 navigation = 0; args->get_NavigationId(&navigation);
            if (state->cleanup.Pending()) {
                BOOL success = FALSE; args->get_IsSuccess(&success);
                const auto next = state->cleanup.NavigationCompleted(state->cleanupSequence, navigation, success != FALSE);
                if (next == aip::notes::LocalCleanupStep::ClearData)
                    PostMessageW(state->window, WM_CLEAR_LOCAL_READY, static_cast<WPARAM>(state->cleanupSequence), 0);
                else if (next == aip::notes::LocalCleanupStep::Failed) PostCleanupResult(state, E_FAIL);
                return S_OK;
            }
            if (navigation != state->navigation) return S_OK;
            KillTimer(state->window, 1); BOOL success = FALSE; args->get_IsSuccess(&success);
            ComPtr<ICoreWebView2NavigationCompletedEventArgs2> details; int http = 0;
            if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&details)))) details->get_HttpStatusCode(&http);
            if (!success) {
                COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN; args->get_WebErrorStatus(&status);
                SetSignInStatus(state, L"The page could not load (navigation error " + std::to_wstring(static_cast<int>(status)) + L"). Check the connection, then Retry or Open game page.");
            } else if (http >= 400) SetSignInStatus(state, L"The page returned HTTP " + std::to_wstring(http) + L". Select Retry or Open game page. Saved Notes credentials are kept.");
            else SetSignInStatus(state, L"Sign in to HoYoLAB below, then select Find game accounts. If the page is blank, use Retry or Open game page. Saved Notes credentials are kept.");
            SetBusy(state, state->busy); return S_OK;
        }).Get(), &event);
    state->view->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
        [weak, generation](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
            const auto state = weak.lock(); if (!state || !CurrentBrowser(state, generation)) return S_OK;
            COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
            if (args) args->get_ProcessFailedKind(&kind);
            state->processFailed = true; CancelRequests(state, true); KillTimer(state->window, 1);
            if (state->cleanup.Fail(state->cleanupSequence)) { PostCleanupResult(state, E_FAIL); return S_OK; }
            SetSignInStatus(state, L"The embedded browser stopped responding (process failure " + std::to_wstring(static_cast<int>(kind)) + L"). Select Retry to recreate the view. Saved Notes credentials are kept."); return S_OK;
        }).Get(), &event);
    state->view->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [weak, generation](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            const auto state = weak.lock(); LPWSTR uri = nullptr;
            if (args) { args->put_Handled(TRUE); if (state && CurrentBrowser(state, generation) && !state->cleanup.Pending() && SUCCEEDED(args->get_Uri(&uri)) && uri && AllowedNavigation(uri)) state->view->Navigate(uri); CoTaskMemFree(uri); }
            return S_OK;
        }).Get(), &event);
    ResizeSignInView(state); controller->put_IsVisible(TRUE);
    const auto navigating = state->view->Navigate(aip::notes::SignInGames[state->resource].page);
    if (FAILED(navigating)) Failure(state, L"The official game page could not be opened", navigating);
    SetBusy(state, false);
}
static void StartBrowser(const std::shared_ptr<State>& state) {
    if (!Live(state) || state->cleanup.Pending()) return;
    state->processFailed = false; const auto generation = ++state->generation;
    SetSignInStatus(state, state->retained ? L"Opening this game's retained browser profile..." : L"Opening a fresh private sign-in session for this game...");
    const std::weak_ptr<State> weak = state;
    const HRESULT created = CreateCoreWebView2EnvironmentWithOptions(nullptr, state->browserData.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([weak, generation](HRESULT error, ICoreWebView2Environment* environment) -> HRESULT {
            const auto state = weak.lock(); if (!state || !CurrentBrowser(state, generation)) return S_OK;
            if (FAILED(error) || !environment) { Failure(state, L"WebView2 could not start; install/update the Evergreen Runtime", error); return S_OK; }
            state->environment = environment;
            ComPtr<ICoreWebView2Environment10> environment10; ComPtr<ICoreWebView2ControllerOptions> options;
            HRESULT prepared = environment->QueryInterface(IID_PPV_ARGS(&environment10));
            if (SUCCEEDED(prepared)) prepared = environment10->CreateCoreWebView2ControllerOptions(&options);
            const auto profile = L"Notes_" + std::wstring(aip::notes::SignInGames[state->resource].profile);
            if (SUCCEEDED(prepared)) prepared = options->put_ProfileName(profile.c_str());
            if (SUCCEEDED(prepared)) prepared = options->put_IsInPrivateModeEnabled(!state->retained);
            if (FAILED(prepared)) { Failure(state, L"Update WebView2 Runtime for isolated/private profiles", prepared); return S_OK; }
            const HRESULT requested = environment10->CreateCoreWebView2ControllerWithOptions(state->window, options.Get(),
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([weak, generation](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                    const auto state = weak.lock();
                    if (!state || !CurrentBrowser(state, generation)) { if (controller) controller->Close(); return S_OK; }
                    if (FAILED(result) || !controller) { Failure(state, L"The embedded view could not be created", result); return S_OK; }
                    try { ConfigureBrowser(state, controller, generation); }
                    catch (...) { SetSignInStatus(state, L"The embedded view could not initialize. Select Retry. No account was changed."); }
                    return S_OK;
                }).Get());
            if (FAILED(requested)) Failure(state, L"The embedded browser controller could not be requested", requested);
            return S_OK;
        }).Get());
    if (FAILED(created)) Failure(state, L"WebView2 startup failed", created);
}
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
    try {
        const auto found = windows.find(window);
        if (found == windows.end()) return DefWindowProcW(window, message, wParam, lParam);
        workers.ReapCompleted();
        const auto state = found->second;
        if (message == WM_GETMINMAXINFO) { auto* info = reinterpret_cast<MINMAXINFO*>(lParam); info->ptMinTrackSize = {1040, 540}; return 0; }
        if (message == WM_SIZE) { ResizeSignInView(state); return 0; }
        if (message == WM_TIMER && wParam == 1) { KillTimer(window, 1); SetSignInStatus(state, L"The page is still loading. If it stays blank, select Retry or Open game page. A visible security-verification page may need you to finish verification."); return 0; }
        if (message == WM_TIMER && wParam == 2) {
            KillTimer(window, 2); state->cleanupSlow = true;
            if (state->cleanup.Pending()) SetSignInStatus(state, L"Browser cleanup is still pending after 30 seconds. You can wait, or select Close to explicitly cancel; some local browser data may remain. Imported Notes credentials are kept.");
            return 0;
        }
        if (message == WM_CLEAR_LOCAL_READY) { if (wParam == static_cast<WPARAM>(state->cleanupSequence)) BeginClearLocalData(state); return 0; }
        if (message == WM_CLEAR_LOCAL_RESULT) { if (wParam == static_cast<WPARAM>(state->cleanupSequence)) FinishLocalCleanup(state); return 0; }
        if (message == WM_ACCOUNTS_READY) { if (wParam == static_cast<WPARAM>(state->request)) AccountsReady(state); return 0; }
        if (message == WM_COMMAND) {
            const auto id = LOWORD(wParam);
            if (id == IDOK) { ReadCookies(state, true); return 0; }
            if (id == ID_FIND) { ReadCookies(state, false); return 0; }
            if (id == IDCANCEL) { PostMessageW(window, WM_CLOSE, 0, 0); return 0; }
            if (id == ID_MANUAL) { SetBusy(state, state->busy); SetSignInStatus(state, L"Manual fallback: enter the UID shown in this game's in-game profile, not your HoYoLAB account number. No account is saved until Save selected account."); return 0; }
            if (id == ID_CLEAR_LOCAL) { ClearLocalSession(state); return 0; }
            if (id == ID_RETRY || id == ID_GAME_PAGE) {
                if (state->cleanup.Pending()) return 0;
                CancelRequests(state, true);
                if (!state->view || state->processFailed) { CloseBrowser(state); StartBrowser(state); }
                else { const auto result = id == ID_RETRY ? state->view->Reload() : state->view->Navigate(aip::notes::SignInGames[state->resource].page); if (FAILED(result)) Failure(state, L"Retry/navigation failed", result); }
                return 0;
            }
            if (id == ID_RETAIN) {
                const bool retained = SendMessageW(state->retain, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (state->cleanup.Pending() || !IEquals(aip::MakeAbsolutePath(g_iniPath), state->profile)) {
                    SendMessageW(state->retain, BM_SETCHECK, state->retained ? BST_CHECKED : BST_UNCHECKED, 0);
                    SetSignInStatus(state, L"Finish browser cleanup, or reopen sign-in if the active profile changed. No preference was saved."); return 0;
                }
                if (!SaveSecondaryTileMenuSettings(window, {{aip::notes::SignInGames[state->resource].section, L"RetainSession", retained ? L"1" : L"0"}})) {
                    SendMessageW(state->retain, BM_SETCHECK, state->retained ? BST_CHECKED : BST_UNCHECKED, 0); return 0;
                }
                state->retained = retained; CloseBrowser(state); StartBrowser(state); return 0;
            }
        }
        if (message == WM_SAVE_ACCOUNT) {
            if (wParam != static_cast<WPARAM>(state->request)) return 0;
            if (!IEquals(aip::MakeAbsolutePath(g_iniPath), state->profile)) SetSignInStatus(state, L"The active profile changed. Reopen sign-in; no account was saved.");
            else if (!state->pending.empty() && SaveSecondaryTileMenuSettings(window, state->pending)) {
                RequestContentNotesRefresh(state->resource);
                SetSignInStatus(state, std::wstring(aip::notes::SignInGames[state->resource].name) + L" account saved. Closing a fresh session discards local browser login; imported Notes credentials are kept.");
            } else SetSignInStatus(state, L"The account could not be saved. Existing configuration was kept.");
            state->pending.clear(); SetBusy(state, false); return 0;
        }
        if (message == WM_CLOSE) {
            if (state->cleanup.RequestClose()) { DestroyWindow(window); return 0; }
            if (!state->cleanupSlow && std::chrono::steady_clock::now() < state->cleanupDeadline) {
                SetSignInStatus(state, L"Finishing local browser cleanup before closing. Imported Notes credentials are kept. If cleanup remains pending, explicit cancellation becomes available after 30 seconds."); return 0;
            }
            if (DESKTOPSTUB_CONTENT_MESSAGEBOX(window,
                L"Browser cleanup has not completed. Cancel it and close now? Some local cookies/cache may remain. Imported Notes credentials will be kept; this does not call HoYoLAB Logout.",
                L"Cancel pending browser cleanup", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) == IDYES && Live(state)) {
                // CloseBrowser invalidates the operation before closing the COM
                // view. A late callback cannot report canceled cleanup success.
                CloseBrowser(state); DestroyWindow(window);
            }
            return 0;
        }
        if (message == WM_NCDESTROY) { CloseBrowser(state); state->closed = true; windows.erase(found); }
    } catch (...) {
        const auto found = windows.find(window);
        if (found != windows.end()) { found->second->pending.clear(); SetBusy(found->second, false); SetSignInStatus(found->second, L"This operation could not complete. No pending account was saved. Select Retry."); }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

static bool OpenEmbeddedNotesSignIn(HWND owner, int resource) {
    if (resource < 0 || resource >= 3) return false;
    using namespace notes_signin;
    workers.ReapCompleted();
    const auto profile = aip::MakeAbsolutePath(g_iniPath);
    for (const auto& item : windows) if (item.second->resource == resource && IEquals(item.second->profile, profile)) {
        ShowWindow(item.first, SW_RESTORE); SetForegroundWindow(item.first); return true;
    }
    static thread_local struct Apartment {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartment;
    if (FAILED(apartment.result)) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"Embedded sign-in needs the app's UI thread. Reopen it from Tiles.", L"HoYoLAB sign-in", MB_OK | MB_ICONWARNING); return false;
    }
    LPWSTR runtime = nullptr;
    const auto available = GetAvailableCoreWebView2BrowserVersionString(nullptr, &runtime); CoTaskMemFree(runtime);
    if (FAILED(available)) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"Install or update Microsoft Edge WebView2 Evergreen Runtime from https://developer.microsoft.com/microsoft-edge/webview2/ and reopen sign-in. No installer is run automatically.", L"HoYoLAB sign-in", MB_OK | MB_ICONINFORMATION); return false;
    }
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = WindowProc;
    cls.lpszClassName = L"DesktopStubNotesEmbeddedSignIn"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    const auto state = std::make_shared<State>(); state->resource = resource; state->profile = profile;
    std::wstring preference = IniReadS(aip::notes::SignInGames[resource].section, L"RetainSession", L"0"), error;
    if (!aip::notes::NormalizeNotesSignInSetting(aip::notes::SignInGames[resource].section, L"RetainSession", preference, error)) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"HoYoLAB sign-in", MB_OK | MB_ICONWARNING); return false;
    }
    state->retained = preference == L"1";
    const auto title = std::wstring(aip::notes::SignInGames[resource].name) + L" - HoYoLAB sign-in";
    state->window = CreateWindowExW(WS_EX_APPWINDOW, cls.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1040, 820, owner, nullptr, cls.hInstance, nullptr);
    if (!state->window) return false;
    windows.emplace(state->window, state);
    const auto child = [&](const wchar_t* type, const wchar_t* label, DWORD style, int x, int y, int w, int h, int id) {
        HWND control = CreateWindowExW(type == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, type, label,
            WS_CHILD | WS_VISIBLE | style, x, y, w, h, state->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), cls.hInstance, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); return control;
    };
    const auto heading = std::wstring(aip::notes::SignInGames[resource].name) + L": sign in below, find your game accounts, choose a UID/server, then save.";
    child(L"STATIC", heading.c_str(), 0, 12, 8, 990, 22, 0);
    state->find = child(L"BUTTON", L"Find game accounts", BS_PUSHBUTTON | WS_TABSTOP, 12, 34, 170, 28, ID_FIND);
    state->accounts = child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 192, 35, 800, 200, ID_ACCOUNTS);
    state->manual = child(L"BUTTON", L"Manual game UID", BS_AUTOCHECKBOX | WS_TABSTOP, 12, 70, 156, 24, ID_MANUAL);
    const auto section = aip::notes::HostAccountSection(resource);
    state->uid = child(L"EDIT", IniReadS(section.c_str(), L"UID", L"").c_str(), ES_AUTOHSCROLL | WS_TABSTOP, 176, 68, 210, 26, 100);
    SendMessageW(state->uid, EM_SETLIMITTEXT, 32, 0);
    state->protect = child(L"BUTTON", L"Protect saved login fields with Windows DPAPI (optional)", BS_AUTOCHECKBOX | WS_TABSTOP, 402, 70, 580, 24, 101);
    state->import = child(L"BUTTON", L"Save selected account", BS_PUSHBUTTON | WS_TABSTOP, 12, 105, 170, 28, IDOK);
    child(L"BUTTON", L"Retry", BS_PUSHBUTTON | WS_TABSTOP, 190, 105, 70, 28, ID_RETRY);
    child(L"BUTTON", L"Open game page", BS_PUSHBUTTON | WS_TABSTOP, 268, 105, 125, 28, ID_GAME_PAGE);
    child(L"BUTTON", L"Clear local browser data...", BS_PUSHBUTTON | WS_TABSTOP, 401, 105, 190, 28, ID_CLEAR_LOCAL);
    state->retain = child(L"BUTTON", L"Remember sign-in (restarts this game view)", BS_AUTOCHECKBOX | WS_TABSTOP, 602, 107, 330, 24, ID_RETAIN);
    SendMessageW(state->retain, BM_SETCHECK, state->retained ? BST_CHECKED : BST_UNCHECKED, 0);
    child(L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, 937, 105, 65, 28, IDCANCEL);
    state->status = child(L"STATIC", L"Starting embedded sign-in...", 0, 12, 145, 990, 48, 102);
    SetBusy(state, false); ShowWindow(state->window, SW_SHOW); SetForegroundWindow(state->window);
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local))) { DestroyWindow(state->window); return false; }
    std::wstring identity, hash;
    const bool keyed = aip::named_objects::NormalizeFileIdentity(profile, identity) && aip::named_objects::StableIdentityHash(identity, hash);
    state->browserData = std::wstring(local) + L"\\DesktopStub\\NotesSignIn\\" + hash; CoTaskMemFree(local);
    if (!keyed || !EnsureDirectoryTreeExists(state->browserData)) { SetSignInStatus(state, L"Could not prepare this profile's writable embedded sign-in folder."); return false; }
    StartBrowser(state); return true;
}
static void CloseEmbeddedNotesSignIns() {
    // All worker cancellation is issued before joining. WinHTTP checks it every
    // pending wait; the lookup's one 15-second deadline remains the upper bound.
    // Join before COM/window and program-global teardown; no thread is detached.
    notes_signin::workers.CancelAndJoin();
    std::vector<HWND> open;
    for (const auto& item : notes_signin::windows) open.push_back(item.first);
    for (const auto window : open) if (IsWindow(window)) DestroyWindow(window);
}
#else
static bool OpenEmbeddedNotesSignIn(HWND, int) { return false; }
static void CloseEmbeddedNotesSignIns() {}
#endif
