// Explicit embedded HoYoLAB sign-in. No browser harness, script injection,
// external browser profile, clipboard, or automatic cookie import is used.
#ifndef DESKTOPSTUB_INERT_HARDWARE
#include "../third_party/WebView2/WebView2.h"
#include <wrl.h>

namespace notes_signin {
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
static constexpr UINT WM_SAVE_ACCOUNT = WM_APP + 171;
struct State {
    HWND window = nullptr, uid = nullptr, protect = nullptr, import = nullptr, status = nullptr;
    int resource = 0;
    bool closed = false, busy = false;
    std::wstring profile;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> view;
    std::vector<aip::IniSetting> pending;
};
static std::map<HWND, std::shared_ptr<State>> windows;
static void SetSignInStatus(const std::shared_ptr<State>& state, const wchar_t* text) {
    if (!state->closed) SetWindowTextW(state->status, text);
}
static bool Live(const std::shared_ptr<State>& state) { return !state->closed && IsWindow(state->window); }
static bool HoYoDomain(std::wstring domain) {
    domain = aip::ToLower(aip::Trim(domain));
    if (!domain.empty() && domain.front() == L'.') domain.erase(domain.begin());
    return domain == L"hoyolab.com" || (domain.size() > 12 && domain.compare(domain.size() - 12, 12, L".hoyolab.com") == 0);
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
    if (!Live(state) || !state->controller) return;
    RECT bounds{}; GetClientRect(state->window, &bounds); bounds.top = 96;
    state->controller->put_Bounds(bounds);
}
static void Import(const std::shared_ptr<State>& state) {
    if (!Live(state) || !state->view || state->busy) return;
    wchar_t uidBuffer[34]{}; GetWindowTextW(state->uid, uidBuffer, _countof(uidBuffer));
    std::wstring uid(uidBuffer), issue;
    if (uid.empty() || !aip::content::NormalizeSetting(L"Notes.Resin", L"UID", uid, issue)) {
        SetSignInStatus(state, L"Enter the numeric game UID from your game profile before importing."); return;
    }
    const bool protect = SendMessageW(state->protect, BM_GETCHECK, 0, 0) == BST_CHECKED;
    ComPtr<ICoreWebView2_2> view2;
    ComPtr<ICoreWebView2CookieManager> manager;
    if (FAILED(state->view.As(&view2)) || FAILED(view2->get_CookieManager(&manager))) {
        SetSignInStatus(state, L"Update the Microsoft Edge WebView2 Runtime to import this signed-in account."); return;
    }
    state->busy = true; EnableWindow(state->import, FALSE);
    SetSignInStatus(state, L"Reading this embedded HoYoLAB session after your import request...");
    const std::weak_ptr<State> weak = state;
    const HRESULT requested = manager->GetCookies(L"https://www.hoyolab.com/",
        Callback<ICoreWebView2GetCookiesCompletedHandler>([weak, uid, protect](HRESULT result, ICoreWebView2CookieList* cookies) -> HRESULT {
            const auto state = weak.lock(); if (!state || !Live(state)) return S_OK;
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
                    SetSignInStatus(state, L"Complete HoYoLAB sign-in, then select Use signed-in account. Required login cookies are not available yet.");
                    state->busy = false; EnableWindow(state->import, TRUE); return S_OK;
                }
                std::string json = "{"; bool first = true;
                aip::JsonFieldString(json, first, "UID", uid);
                aip::JsonFieldString(json, first, protect ? "LTokenProtected" : "LToken", protect ? aip::ProtectSecretForCurrentUser(token) : token);
                aip::JsonFieldString(json, first, protect ? "LTUidProtected" : "LTUid", protect ? aip::ProtectSecretForCurrentUser(user) : user);
                const auto section = aip::notes::HostAccountSection(state->resource);
                int refresh = 0; aip::content::ParseInt(IniReadS(section.c_str(), L"RefreshSeconds", L"0"), 0, 86400, refresh);
                aip::JsonFieldNumber(json, first, "RefreshSeconds", refresh); json += '}';
                std::wstring error;
                if (!aip::notes::ImportCookieJson(state->resource, aip::Utf8ToWide(json), state->pending, error)) throw std::runtime_error("validation");
                if (!PostMessageW(state->window, WM_SAVE_ACCOUNT, 0, 0)) throw std::runtime_error("queue");
            } catch (...) {
                state->pending.clear(); state->busy = false; EnableWindow(state->import, TRUE);
                SetSignInStatus(state, L"Could not import this session safely. Check sign-in, the game UID, and the WebView2 Runtime, then try again. No account was saved.");
            }
            return S_OK;
        }).Get());
    if (FAILED(requested)) { state->busy = false; EnableWindow(state->import, TRUE); SetSignInStatus(state, L"The embedded browser could not read the signed-in session. No account was saved."); }
}
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
    try {
        const auto found = windows.find(window);
        if (found == windows.end()) return DefWindowProcW(window, message, wParam, lParam);
        const auto state = found->second;
        if (message == WM_SIZE) { ResizeSignInView(state); return 0; }
        if (message == WM_COMMAND && LOWORD(wParam) == IDOK) { Import(state); return 0; }
        if (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL) { DestroyWindow(window); return 0; }
        if (message == WM_SAVE_ACCOUNT) {
            if (!IEquals(aip::MakeAbsolutePath(g_iniPath), state->profile)) {
                SetSignInStatus(state, L"The active profile changed. Close this window and open sign-in again; no account was saved.");
            } else if (!state->pending.empty() && SaveSecondaryTileMenuSettings(window, state->pending)) {
                RequestContentNotesRefresh(state->resource);
                SetSignInStatus(state, L"Account saved for this game. You can close sign-in; the resident will refresh its tile.");
            } else SetSignInStatus(state, L"The account could not be saved. Existing configuration was kept.");
            state->pending.clear(); state->busy = false; EnableWindow(state->import, TRUE); return 0;
        }
        if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
        if (message == WM_NCDESTROY) {
            state->closed = true; state->pending.clear();
            if (state->controller) state->controller->Close();
            state->view.Reset(); state->controller.Reset(); state->environment.Reset(); windows.erase(found);
        }
    } catch (...) { return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

static bool OpenEmbeddedNotesSignIn(HWND owner, int resource)
{
    if (resource < 0 || resource >= 3) return false;
    using namespace notes_signin;
    const auto profile = aip::MakeAbsolutePath(g_iniPath);
    for (const auto& item : windows) if (item.second->resource == resource && IEquals(item.second->profile, profile)) {
        ShowWindow(item.first, SW_RESTORE); SetForegroundWindow(item.first); return true;
    }
    static thread_local struct Apartment {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartment;
    if (FAILED(apartment.result)) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"Embedded sign-in needs the app's UI thread. Close this prompt and reopen sign-in from Tiles.", L"HoYoLAB sign-in", MB_OK | MB_ICONWARNING); return false;
    }
    LPWSTR runtime = nullptr;
    if (FAILED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &runtime))) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"Microsoft Edge WebView2 Runtime is not available. Install the Evergreen Runtime from https://developer.microsoft.com/microsoft-edge/webview2/ and reopen sign-in. No installer is run automatically.", L"HoYoLAB sign-in", MB_OK | MB_ICONINFORMATION); return false;
    }
    CoTaskMemFree(runtime);
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = WindowProc;
    cls.lpszClassName = L"DesktopStubNotesEmbeddedSignIn"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    const auto state = std::make_shared<State>(); state->resource = resource; state->profile = profile;
    state->window = CreateWindowExW(WS_EX_APPWINDOW, cls.lpszClassName, L"HoYoLAB sign-in for game tiles", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 760, owner, nullptr, cls.hInstance, nullptr);
    if (!state->window) return false;
    windows.emplace(state->window, state);
    const auto child = [&](const wchar_t* type, const wchar_t* label, DWORD style, int x, int y, int w, int h, int id) {
        HWND control = CreateWindowExW(type == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, type, label,
            WS_CHILD | WS_VISIBLE | style, x, y, w, h, state->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), cls.hInstance, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); return control;
    };
    child(L"STATIC", L"Game UID", 0, 12, 12, 72, 24, 0);
    const auto section = aip::notes::HostAccountSection(resource);
    state->uid = child(L"EDIT", IniReadS(section.c_str(), L"UID", L"").c_str(), ES_AUTOHSCROLL | WS_TABSTOP, 90, 8, 210, 26, 100);
    SendMessageW(state->uid, EM_SETLIMITTEXT, 32, 0);
    state->protect = child(L"BUTTON", L"Protect saved login fields with Windows DPAPI (optional)", BS_AUTOCHECKBOX | WS_TABSTOP, 315, 10, 440, 24, 101);
    state->import = child(L"BUTTON", L"Use signed-in account", BS_PUSHBUTTON | WS_TABSTOP, 12, 44, 180, 28, IDOK);
    child(L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, 202, 44, 90, 28, IDCANCEL);
    state->status = child(L"STATIC", L"Sign in below, enter your game UID, then explicitly import. Only HoYoLAB login cookies are saved; nothing is read from another browser.", 0, 310, 42, 660, 44, 102);
    EnableWindow(state->import, FALSE); ShowWindow(state->window, SW_SHOW); SetForegroundWindow(state->window);
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local))) { DestroyWindow(state->window); return false; }
    std::wstring identity, hash;
    const bool keyed = aip::named_objects::NormalizeFileIdentity(profile, identity) && aip::named_objects::StableIdentityHash(identity, hash);
    const auto browserData = std::wstring(local) + L"\\DesktopStub\\NotesSignIn\\" + hash; CoTaskMemFree(local);
    if (!keyed || !EnsureDirectoryTreeExists(browserData)) { SetSignInStatus(state, L"Could not prepare this profile's writable embedded sign-in folder."); return false; }
    const std::weak_ptr<State> weak = state;
    const HRESULT created = CreateCoreWebView2EnvironmentWithOptions(nullptr, browserData.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([weak](HRESULT error, ICoreWebView2Environment* environment) -> HRESULT {
            const auto state = weak.lock(); if (!state || !Live(state)) return S_OK;
            if (FAILED(error) || !environment) { SetSignInStatus(state, L"WebView2 could not start. Install or update the Evergreen Runtime, then reopen sign-in."); return S_OK; }
            state->environment = environment;
            const HRESULT requested = environment->CreateCoreWebView2Controller(state->window,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([weak](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                    const auto state = weak.lock(); if (!state || !Live(state)) { if (controller) controller->Close(); return S_OK; }
                    if (FAILED(result) || !controller) { SetSignInStatus(state, L"The embedded sign-in view could not be created. Update WebView2 and try again."); return S_OK; }
                    state->controller = controller;
                    if (FAILED(controller->get_CoreWebView2(&state->view))) { SetSignInStatus(state, L"The embedded sign-in view is unavailable."); return S_OK; }
                    EventRegistrationToken popup{};
                    state->view->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                        [weak](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                            const auto state = weak.lock(); LPWSTR uri = nullptr;
                            if (args) {
                                args->put_Handled(TRUE);
                                if (state && Live(state) && SUCCEEDED(args->get_Uri(&uri)) && uri &&
                                    aip::StartsWithI(uri, L"https://")) state->view->Navigate(uri);
                                CoTaskMemFree(uri);
                            }
                            return S_OK;
                        }).Get(), &popup);
                    ResizeSignInView(state); controller->put_IsVisible(TRUE);
                    if (FAILED(state->view->Navigate(L"https://www.hoyolab.com/"))) { SetSignInStatus(state, L"The official sign-in page could not be opened."); return S_OK; }
                    EnableWindow(state->import, TRUE); return S_OK;
                }).Get());
            if (FAILED(requested)) SetSignInStatus(state, L"The embedded sign-in controller could not be requested.");
            return S_OK;
        }).Get());
    if (FAILED(created)) { SetSignInStatus(state, L"WebView2 startup failed. Check the Runtime installation and reopen sign-in."); return false; }
    return true;
}
static void CloseEmbeddedNotesSignIns()
{
    std::vector<HWND> open;
    for (const auto& item : notes_signin::windows) open.push_back(item.first);
    for (const auto window : open) if (IsWindow(window)) DestroyWindow(window);
}
#else
static bool OpenEmbeddedNotesSignIn(HWND, int) { return false; }
static void CloseEmbeddedNotesSignIns() {}
#endif
