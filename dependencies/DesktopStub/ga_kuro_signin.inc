// User-directed overseas WuWa SDK sign-in. The password stays in this
// temporary dialog/worker; only a game token is saved, always using DPAPI.
// The embedded browser receives a human CAPTCHA, never email/password/token.
#ifndef DESKTOPSTUB_INERT_HARDWARE
#include "../third_party/WebView2/WebView2.h"
#include "../content_sources/kuro_auth.inc"
#include "../content_sources/notes_signin_workers.h"
#include <wrl.h>
namespace kuro_signin {
using Microsoft::WRL::ComPtr;using Microsoft::WRL::Callback;
static constexpr UINT WM_READY=WM_APP+185;
static constexpr int ID_LOGIN=110,ID_ROLES=111,ID_SAVE=112;
struct Job {
    std::atomic<bool> canceled{false},done{false};unsigned long long sequence=0;
    std::shared_ptr<aip::kuro::auth::Credentials> credentials;
    aip::kuro::auth::CaptchaProof proof;aip::kuro::auth::Result result;
    std::vector<aip::kuro::Role> roles;std::wstring issue;
};
struct State {
    HWND window=nullptr,email=nullptr,password=nullptr,login=nullptr,roles=nullptr,save=nullptr,status=nullptr;
    bool closed=false,busy=false,captcha=false;unsigned long long sequence=0,browserGeneration=0;
    std::wstring profile,tileId,fingerprint,deviceId,nonce,browserData;
    std::chrono::steady_clock::time_point deadline;
    std::shared_ptr<aip::kuro::auth::Credentials> credentials;std::shared_ptr<Job> job;
    ComPtr<ICoreWebView2Environment> environment;ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2> view;
    ~State(){aip::kuro::auth::Wipe(fingerprint);}
};
static std::map<HWND,std::shared_ptr<State>> windows;
static aip::notes::SignInWorkers workers;
static unsigned long long nextSequence=0;
static bool Live(const std::shared_ptr<State>& state){return state&&!state->closed&&IsWindow(state->window);}
static std::wstring Text(const wchar_t* key,const wchar_t* fallback){return IniReadS(L"Strings",key,fallback);}
static void Status(const std::shared_ptr<State>& state,const std::wstring& message){if(Live(state))SetWindowTextW(state->status,UiLiteralText(message).c_str());}
static void Busy(const std::shared_ptr<State>& state,bool busy){
    state->busy=busy;EnableWindow(state->email,!busy);EnableWindow(state->password,!busy);EnableWindow(state->login,!busy);
    EnableWindow(state->save,!busy&&state->job&&!state->job->roles.empty());EnableWindow(state->roles,!busy);
}
static std::wstring RandomId(bool compact=false){
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))return {};wchar_t text[40]{};StringFromGUID2(guid,text,40);
    std::wstring value;for(const auto ch:std::wstring(text))if(ch!=L'{'&&ch!=L'}'&&(!compact||ch!=L'-'))
        value+=compact?static_cast<wchar_t>(towlower(ch)):static_cast<wchar_t>(towupper(ch));return value;
}
static void Resize(const std::shared_ptr<State>& state){
    if(!Live(state)||!state->controller)return;RECT area{};GetClientRect(state->window,&area);area.left=12;area.top=225;area.right-=12;area.bottom-=12;
    if(area.bottom<area.top)area.bottom=area.top;state->controller->put_Bounds(area);
}
static void CloseBrowser(const std::shared_ptr<State>& state){
    ++state->browserGeneration;state->captcha=false;aip::kuro::auth::Wipe(state->nonce);
    if(state->controller)state->controller->Close();state->view.Reset();state->controller.Reset();state->environment.Reset();
}
static void Cancel(const std::shared_ptr<State>& state){
    if(state->job)state->job->canceled=true;state->sequence=++nextSequence;CloseBrowser(state);
    SetWindowTextW(state->password,L"");state->credentials.reset();state->job.reset();KillTimer(state->window,1);
}
static bool ReadControl(HWND control,std::wstring& result,size_t maximum){
    const auto size=GetWindowTextLengthW(control);if(size<0||static_cast<size_t>(size)>maximum)return false;
    result.resize(static_cast<size_t>(size)+1);const auto copied=GetWindowTextW(control,result.data(),size+1);
    if(copied!=size){aip::kuro::auth::Wipe(result);return false;}result.resize(size);return true;
}
static bool CurrentBrowser(const std::shared_ptr<State>& state,unsigned long long generation){
    return Live(state)&&state->captcha&&state->browserGeneration==generation&&state->credentials&&
        std::chrono::steady_clock::now()<state->deadline;
}
static void BrowserFailure(const std::weak_ptr<State>& weak) noexcept {
    try{const auto state=weak.lock();if(Live(state)){
        Cancel(state);Busy(state,false);
        SetWindowTextW(state->status,L"Verification could not complete. The password was discarded; retry sign-in.");
    }}catch(...){}
}
struct BrowserString {
    LPWSTR value=nullptr;bool secret=false;
    ~BrowserString(){if(secret&&value)SecureZeroMemory(value,wcsnlen(value,16385)*sizeof(wchar_t));CoTaskMemFree(value);}
};
static void StartJob(const std::shared_ptr<State>& state,const aip::kuro::auth::CaptchaProof& proof={});
static void StartCaptcha(const std::shared_ptr<State>& state){
    CloseBrowser(state);state->captcha=true;state->nonce=RandomId(true);const auto generation=++state->browserGeneration;
    if(state->nonce.empty()){Status(state,L"Windows could not start account verification. Retry sign-in.");Busy(state,false);return;}
    const auto weak=std::weak_ptr<State>(state);
    const auto requested=CreateCoreWebView2EnvironmentWithOptions(nullptr,state->browserData.c_str(),nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([weak,generation](HRESULT result,ICoreWebView2Environment* environment) noexcept ->HRESULT{try{
            const auto state=weak.lock();if(!state||!CurrentBrowser(state,generation))return S_OK;
            if(FAILED(result)||!environment){Cancel(state);Busy(state,false);Status(state,L"Kuro requires human verification. Install/update Microsoft Edge WebView2 Runtime, then retry sign-in.");return S_OK;}
            state->environment=environment;ComPtr<ICoreWebView2Environment10> environment10;ComPtr<ICoreWebView2ControllerOptions> options;
            auto prepared=environment->QueryInterface(IID_PPV_ARGS(&environment10));
            if(SUCCEEDED(prepared))prepared=environment10->CreateCoreWebView2ControllerOptions(&options);
            if(SUCCEEDED(prepared))prepared=options->put_ProfileName(L"KuroHumanVerification");
            if(SUCCEEDED(prepared))prepared=options->put_IsInPrivateModeEnabled(TRUE);
            if(FAILED(prepared)){Cancel(state);Busy(state,false);Status(state,L"Update WebView2 Runtime to use an isolated private Kuro verification session.");return S_OK;}
            const auto started=environment10->CreateCoreWebView2ControllerWithOptions(state->window,options.Get(),
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([weak,generation](HRESULT error,ICoreWebView2Controller* controller) noexcept ->HRESULT{try{
                    const auto state=weak.lock();if(!state||!CurrentBrowser(state,generation)){if(controller)controller->Close();return S_OK;}
                    if(FAILED(error)||!controller){Cancel(state);Busy(state,false);Status(state,L"The human verification view could not start. Retry sign-in.");return S_OK;}
                    state->controller=controller;controller->get_CoreWebView2(&state->view);
                    if(!state->view){Cancel(state);Busy(state,false);return S_OK;}
                    ComPtr<ICoreWebView2Settings> settings;if(SUCCEEDED(state->view->get_Settings(&settings))&&settings){
                        settings->put_AreDevToolsEnabled(FALSE);settings->put_AreDefaultContextMenusEnabled(FALSE);settings->put_IsStatusBarEnabled(FALSE);}
                    EventRegistrationToken event{};
                    state->view->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                        [](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args) noexcept ->HRESULT{
                            LPWSTR uri=nullptr;if(!args)return S_OK;
                            const bool allowed=SUCCEEDED(args->get_Uri(&uri))&&uri&&wcscmp(uri,L"about:blank")==0;
                            args->put_Cancel(!allowed);CoTaskMemFree(uri);return S_OK;
                        }).Get(),&event);
                    state->view->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                        [](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args) noexcept ->HRESULT{if(args)args->put_Handled(TRUE);return S_OK;}).Get(),&event);
                    state->view->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                        [weak,generation](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args) noexcept ->HRESULT{try{
                            const auto state=weak.lock();if(!state||!CurrentBrowser(state,generation)||!args)return S_OK;
                            BrowserString source,raw;raw.secret=true;
                            const bool origin=SUCCEEDED(args->get_Source(&source.value))&&source.value&&wcscmp(source.value,L"about:blank")==0;
                            const bool got=origin&&SUCCEEDED(args->TryGetWebMessageAsString(&raw.value))&&raw.value;
                            aip::kuro::auth::CaptchaProof proof;
                            const bool valid=got&&wcsnlen(raw.value,16385)<=16384&&aip::kuro::auth::ParseCaptchaMessage(raw.value,state->nonce,proof);
                            if(valid){CloseBrowser(state);Status(state,L"Verification completed. Finishing Kuro sign-in...");StartJob(state,proof);}return S_OK;
                        }catch(...){BrowserFailure(weak);return E_FAIL;}}).Get(),&event);
                    state->view->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
                        [weak,generation](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*) noexcept ->HRESULT{try{
                            const auto state=weak.lock();if(state&&CurrentBrowser(state,generation)){Cancel(state);Busy(state,false);Status(state,L"Verification browser stopped. Retry sign-in; the password was discarded.");}return S_OK;
                        }catch(...){BrowserFailure(weak);return E_FAIL;}}).Get(),&event);
                    Resize(state);controller->put_IsVisible(TRUE);
                    if(FAILED(state->view->NavigateToString(aip::kuro::auth::CaptchaHtml(state->nonce).c_str()))){Cancel(state);Busy(state,false);Status(state,L"The verification page could not open. Retry sign-in.");}
                    return S_OK;
                }catch(...){BrowserFailure(weak);return E_FAIL;}}).Get());
            if(FAILED(started)){Cancel(state);Busy(state,false);Status(state,L"The verification view could not be requested. Retry sign-in.");}return S_OK;
        }catch(...){BrowserFailure(weak);return E_FAIL;}}).Get());
    if(FAILED(requested)){Cancel(state);Busy(state,false);Status(state,L"The human verification browser could not start. Retry sign-in.");}
}
static void StartJob(const std::shared_ptr<State>& state,const aip::kuro::auth::CaptchaProof& proof){
    if(!Live(state)||!state->credentials)return;
    if(state->job)state->job->canceled=true;
    auto job=std::make_shared<Job>();job->sequence=state->sequence=++nextSequence;job->credentials=state->credentials;job->proof=proof;state->job=job;Busy(state,true);
    const auto window=state->window;
    if(!workers.Start([job]{job->canceled=true;},[job,window]{
        const auto canceled=[job]{return job->canceled.load()||!g_running.load();};
        try{
        job->result=aip::kuro::auth::Login(*job->credentials,job->proof,aip::FetchHttpBytes,canceled);job->proof.Clear();
        if(job->result.status!=aip::kuro::auth::Status::CaptchaRequired){
            aip::kuro::auth::Wipe(job->credentials->password);aip::kuro::auth::Wipe(job->credentials->email);
        }
        if(!canceled()&&job->result.status==aip::kuro::auth::Status::Authenticated){
            aip::kuro::Configuration cfg;cfg.gameToken=job->result.token;cfg.deviceId=job->credentials->deviceId;cfg.timeoutSeconds=15;
            const bool discovered=aip::kuro::DiscoverRoles(cfg,job->roles,job->issue,aip::FetchHttpBytes,{},canceled);
            aip::kuro::auth::Wipe(cfg.gameToken);
            if(!discovered||job->roles.empty()){
                if(job->issue.empty())job->issue=L"No overseas Wuthering Waves role is linked to this Kuro account.";
                aip::kuro::auth::Wipe(job->result.token);job->result.status=aip::kuro::auth::Status::Error;
            }
        }
        }catch(...){
            aip::kuro::auth::Wipe(job->credentials->password);aip::kuro::auth::Wipe(job->credentials->email);job->proof.Clear();
            aip::kuro::auth::Wipe(job->result.token);job->result.status=aip::kuro::auth::Status::Error;
            job->result.message=L"Kuro sign-in could not complete. No account was saved; retry when ready.";
        }
        job->done.store(true,std::memory_order_release);
        if(!canceled())PostMessageW(window,WM_READY,static_cast<WPARAM>(job->sequence),0);
    })){
        Cancel(state);Busy(state,false);Status(state,L"The sign-in worker could not start. No account was saved.");
    }
}
static bool Save(const std::shared_ptr<State>& state){
    if(!Live(state)||state->busy||!state->job||state->job->canceled.load()||!state->job->done.load(std::memory_order_acquire))return false;
    if(state->job->result.status!=aip::kuro::auth::Status::Authenticated||state->job->result.token.empty())return false;
    const auto selected=SendMessageW(state->roles,CB_GETCURSEL,0,0);
    if(selected==CB_ERR||static_cast<size_t>(selected)>=state->job->roles.size()){Status(state,L"Choose the Wuthering Waves character and server to use for this tile.");return false;}
    const auto role=state->job->roles[static_cast<size_t>(selected)];std::wstring protectedToken,error;
    try{protectedToken=aip::ProtectSecretForCurrentUser(state->job->result.token);}catch(...){}
    if(protectedToken.empty()){Status(state,L"Windows could not protect the game token. Nothing was saved.");return false;}
    const auto saved=MutateDesktopStubIniWithWait([&](std::wstring& text){
        if(!IEquals(aip::MakeAbsolutePath(g_iniPath),state->profile)){error=L"The active profile changed. Reopen sign-in before saving.";return false;}
        auto document=aip::ParseIniDocument(text);aip::content::TileInstanceConfiguration cfg;
        const auto read=[&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){std::wstring value;return aip::ReadIniValueFromDoc(document,section,key,value)?value:std::wstring(fallback);};
        if(!ValidateTileInstanceHostConfiguration(read,cfg,error))return false;
        const auto found=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& tile){return tile.id==state->tileId;});
        if(found==cfg.tiles.end()||found->type!=aip::content::TileInstanceType::Wuwa||aip::kuro::ConfigurationKey(found->kuro)!=state->fingerprint){
            error=L"This tile or its account settings changed. Reopen sign-in; the previous account was kept.";return false;}
        auto candidate=text;const auto section=aip::content::TileInstanceSection(state->tileId);
        for(const auto& setting:std::vector<std::pair<const wchar_t*,std::wstring>>{{L"Kuro.GameToken",L""},{L"Kuro.GameTokenProtected",protectedToken},
            {L"Kuro.PlayerId",role.playerId},{L"Kuro.Region",role.region},{L"Kuro.DeviceId",state->deviceId}})
            if(!aip::WriteIniValueToText(candidate,section.c_str(),setting.first,setting.second))return false;
        document=aip::ParseIniDocument(candidate);if(!ValidateTileInstanceHostConfiguration(read,cfg,error))return false;
        text=std::move(candidate);return true;
    },CurrentIniWriteLockWaitMs());
    if(!saved){Status(state,error.empty()?L"The game account could not be saved. The previous settings were kept.":error);return false;}
    RefreshTileInstanceNow(state->tileId);Cancel(state);Busy(state,false);
    Status(state,L"Wuthering Waves account saved with Windows protection. This tile will refresh automatically. You may close this window.");
    NotifyUserOperation(L"kuro-account-saved-"+state->tileId,Text(L"KuroAccountSavedTitle",L"Wuthering Waves account saved"),
        Text(L"KuroAccountSavedMessage",L"The selected character is connected. Its tile will refresh automatically."));return true;
}
static LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) noexcept {
    try{
        const auto found=windows.find(window);if(found==windows.end())return DefWindowProcW(window,message,wParam,lParam);
        const auto state=found->second;workers.ReapCompleted();
        if(message==WM_SIZE){Resize(state);return 0;}
        if(message==WM_TIMER&&wParam==1){Cancel(state);Busy(state,false);Status(state,L"Sign-in expired after five minutes. The password was discarded. Start again when ready.");return 0;}
        if(message==WM_COMMAND){const auto id=LOWORD(wParam);
            if(id==IDCANCEL){DestroyWindow(window);return 0;}
            if(id==ID_SAVE){Save(state);return 0;}
            if(id==ID_LOGIN&&!state->busy){
                auto credentials=std::make_shared<aip::kuro::auth::Credentials>();credentials->deviceId=state->deviceId;
                const bool read=ReadControl(state->email,credentials->email,320)&&ReadControl(state->password,credentials->password,1024);
                Cancel(state);
                if(!read||!aip::kuro::auth::ValidCredentials(*credentials)){Status(state,L"Enter your Kuro account email and password. Google/Apple-only sign-in is not supported in this dialog.");return 0;}
                state->credentials=credentials;state->deadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);
                if(!SetTimer(window,1,300000,nullptr)){Cancel(state);Busy(state,false);Status(state,L"Windows could not start the sign-in expiry timer. The password was discarded; retry sign-in.");return 0;}
                SendMessageW(state->roles,CB_RESETCONTENT,0,0);Status(state,L"Signing in to Kuro over HTTPS...");StartJob(state);return 0;
            }
        }
        if(message==WM_READY){
            if(!state->job||state->job->sequence!=static_cast<unsigned long long>(wParam)||state->job->canceled||!state->job->done.load(std::memory_order_acquire))return 0;
            if(!IEquals(aip::MakeAbsolutePath(g_iniPath),state->profile)){Cancel(state);Busy(state,false);Status(state,L"The active profile changed. Reopen sign-in; no account was saved.");return 0;}
            if(state->job->result.status==aip::kuro::auth::Status::CaptchaRequired){Status(state,state->job->result.message);StartCaptcha(state);return 0;}
            state->credentials.reset();KillTimer(window,1);Busy(state,false);
            if(state->job->result.status==aip::kuro::auth::Status::Error){Status(state,state->job->issue.empty()?state->job->result.message:state->job->issue);state->job.reset();return 0;}
            SendMessageW(state->roles,CB_RESETCONTENT,0,0);
            for(const auto& role:state->job->roles){const auto label=role.name+L" — "+role.region+L" — UID "+role.playerId+L" — level "+std::to_wstring(role.level);
                SendMessageW(state->roles,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
            if(state->job->roles.size()==1)SendMessageW(state->roles,CB_SETCURSEL,0,0);EnableWindow(state->save,TRUE);
            Status(state,L"Signed in. Select your character/server, then choose Save selected account. The password has been discarded.");return 0;
        }
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}
        if(message==WM_NCDESTROY){Cancel(state);state->closed=true;windows.erase(found);}
    }catch(...){try{const auto found=windows.find(window);if(found!=windows.end()){
        Cancel(found->second);Busy(found->second,false);SetWindowTextW(found->second->status,L"Sign-in could not complete. No account was saved; retry when ready.");
    }}catch(...){}return 0;}
    return DefWindowProcW(window,message,wParam,lParam);
}
}
static bool OpenKuroSignInForTile(HWND owner,const std::wstring& tileId){
    using namespace kuro_signin;workers.ReapCompleted();const auto profile=aip::MakeAbsolutePath(g_iniPath);
    for(const auto& item:windows)if(item.second->tileId==tileId&&IEquals(item.second->profile,profile)){ShowWindow(item.first,SW_RESTORE);SetForegroundWindow(item.first);return true;}
    aip::content::TileInstanceConfiguration cfg;std::wstring error;if(!ReadCurrentTileInstances(cfg,error))return false;
    const auto found=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& tile){return tile.id==tileId;});
    if(found==cfg.tiles.end()||found->type!=aip::content::TileInstanceType::Wuwa)return false;
    static thread_local struct Apartment{HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);~Apartment(){if(SUCCEEDED(result))CoUninitialize();}} apartment;
    if(FAILED(apartment.result))return false;
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=WindowProc;cls.lpszClassName=L"DesktopStubKuroSignIn";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    auto state=std::make_shared<State>();state->tileId=tileId;state->profile=profile;state->fingerprint=aip::kuro::ConfigurationKey(found->kuro);
    state->deviceId=found->kuro.deviceId.empty()?RandomId():found->kuro.deviceId;
    wchar_t temporary[MAX_PATH]{};const auto temporarySize=GetTempPathW(MAX_PATH,temporary);const auto browserId=RandomId(true);
    if(!temporarySize||temporarySize>=MAX_PATH||state->deviceId.empty()||browserId.empty())return false;
    state->browserData=std::wstring(temporary)+L"DesktopStub-KuroVerification-"+browserId;
    // InPrivate mode is mandatory. This directory is never used to import a
    // browser or game session, and the browser never receives the password.
    if(!EnsureDirectoryTreeExists(state->browserData))return false;
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);const int width=900,height=700;
    const auto title=found->name+L" — Wuthering Waves sign-in";
    state->window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,UiLiteralText(title,true).c_str(),WS_OVERLAPPEDWINDOW,
        (work.left+work.right-width)/2,(work.top+work.bottom-height)/2,width,height,owner,nullptr,cls.hInstance,nullptr);
    if(!state->window)return false;windows.emplace(state->window,state);
    const auto child=[&](const wchar_t* type,const wchar_t* caption,DWORD style,int x,int y,int w,int h,int id){
        const auto localized=UiLiteralText(caption);auto control=CreateWindowExW(std::wstring(type)==L"EDIT"?WS_EX_CLIENTEDGE:0,type,localized.c_str(),WS_CHILD|WS_VISIBLE|style,
            x,y,w,h,state->window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),cls.hInstance,nullptr);
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return control;
    };
    child(L"STATIC",L"Sign in with your overseas Kuro email/password. Complete any human verification below, then choose your character. Passwords are never saved.",0,12,10,850,36,0);
    child(L"STATIC",L"Kuro email",0,12,56,100,20,0);state->email=child(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,115,52,320,26,100);
    child(L"STATIC",L"Password",0,452,56,80,20,0);state->password=child(L"EDIT",L"",ES_PASSWORD|ES_AUTOHSCROLL|WS_TABSTOP,535,52,325,26,101);
    SendMessageW(state->email,EM_SETLIMITTEXT,320,0);SendMessageW(state->password,EM_SETLIMITTEXT,1024,0);
    state->login=child(L"BUTTON",L"Sign in",BS_PUSHBUTTON|WS_TABSTOP,12,90,120,30,ID_LOGIN);
    state->roles=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,145,91,715,240,ID_ROLES);
    state->save=child(L"BUTTON",L"Save selected account",BS_PUSHBUTTON|WS_TABSTOP,12,132,190,30,ID_SAVE);
    child(L"BUTTON",L"Close",BS_PUSHBUTTON|WS_TABSTOP,770,132,90,30,IDCANCEL);
    state->status=child(L"STATIC",L"Use your Kuro game account, not a HoYoLAB account. Google/Apple-only sign-in is not implemented here. No credentials are read from another app.",0,12,175,850,44,102);
    Busy(state,false);ShowWindow(state->window,SW_SHOW);SetForegroundWindow(state->window);SetFocus(state->email);return true;
}
static void CloseKuroSignIns(){
    for(const auto& item:kuro_signin::windows)kuro_signin::Cancel(item.second);
    kuro_signin::workers.CancelAndJoin();std::vector<HWND> open;for(const auto& item:kuro_signin::windows)open.push_back(item.first);
    for(const auto window:open)if(IsWindow(window))DestroyWindow(window);
}
#else
static bool OpenKuroSignInForTile(HWND,const std::wstring&){return false;}
static void CloseKuroSignIns(){}
#endif
