// Explicit Mail account setup. Only Connect opens Google; no browser cookies,
// passwords, another tile's grant or installed Mail account store is inspected.
#ifndef DESKTOPSTUB_INERT_HARDWARE
#include "../content_sources/notes_signin_workers.h"
namespace gmail_setup {
static constexpr UINT WM_READY=WM_APP+196;
static constexpr int ID_IMPORT=310,ID_CONNECT=311,ID_DISCONNECT=312,ID_CANCEL_JOB=313;
struct Job {
    std::atomic<bool> canceled{false},done{false};unsigned long long sequence=0;bool connected=false;
    aip::gmail::Configuration configuration;aip::google_calendar::Credentials credentials;
    std::wstring error,mailboxAddress;std::shared_ptr<aip::gmail::Session> session;
};
struct State {
    HWND window=nullptr,instructions=nullptr,client=nullptr,import=nullptr,connect=nullptr,disconnect=nullptr,cancel=nullptr,close=nullptr,status=nullptr;
    bool closed=false,busy=false;ULONGLONG started=0;unsigned long long sequence=0;
    std::wstring profile,tileId,fingerprint;aip::gmail::Configuration configuration;
    std::shared_ptr<aip::gmail::Session> session;std::shared_ptr<Job> job;
};
static std::map<HWND,std::shared_ptr<State>> windows;static aip::notes::SignInWorkers workers;static unsigned long long nextSequence=0;
static std::wstring Text(const wchar_t* key,const wchar_t* fallback){return IniReadS(L"Strings",key,fallback);}
static bool Live(const std::shared_ptr<State>& s){return s&&!s->closed&&IsWindow(s->window);}
static void Status(const std::shared_ptr<State>& s,const std::wstring& value){if(Live(s))SetWindowTextW(s->status,UiLiteralText(value).c_str());}
static std::wstring Fingerprint(const aip::content::TileInstance& tile){return aip::gmail::ConfigurationKey(GmailConfigurationForTile(tile));}
static bool Current(const std::shared_ptr<State>& s,aip::content::TileInstance& output,std::wstring& error,bool same=true){
    if(!Live(s)||!IEquals(aip::MakeAbsolutePath(g_iniPath),s->profile)){error=Text(L"GmailProfileChanged",L"The active profile changed. Reopen Mail setup before continuing.");return false;}
    aip::content::TileInstanceConfiguration cfg;if(!ReadCurrentTileInstances(cfg,error))return false;
    const auto tile=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& item){return item.id==s->tileId;});
    if(tile==cfg.tiles.end()||tile->type!=aip::content::TileInstanceType::Mail||(same&&Fingerprint(*tile)!=s->fingerprint)){
        error=Text(L"GmailTileChanged",L"This Mail tile or its read permissions changed. Reopen setup; no previous account was replaced.");return false;}
    output=*tile;return true;
}
static void Buttons(const std::shared_ptr<State>& s,bool busy){
    s->busy=busy;if(!Live(s))return;EnableWindow(s->import,!busy);EnableWindow(s->connect,!busy&&!s->configuration.account.clientId.empty());
    EnableWindow(s->disconnect,!busy&&!s->configuration.account.refreshTokenProtected.empty());EnableWindow(s->cancel,busy);
}
static void Cancel(const std::shared_ptr<State>& s) noexcept {
    if(s->job)s->job->canceled=true;s->sequence=++nextSequence;s->job.reset();
    try{if(s->session)s->session->Clear();}catch(...){}s->session.reset();if(s->window)KillTimer(s->window,1);
}
static bool Reload(const std::shared_ptr<State>& s){
    aip::content::TileInstance tile;std::wstring error;if(!Current(s,tile,error,false)){Status(s,error);return false;}
    s->configuration=GmailConfigurationForTile(tile);s->fingerprint=Fingerprint(tile);
    const auto label=s->configuration.account.clientId.empty()?Text(L"GmailClientMissing",L"No Desktop OAuth client imported."):
        s->configuration.account.refreshTokenProtected.empty()?Text(L"GmailClientReady",L"Desktop OAuth client ready. Choose Connect to Google."):
        Text(L"GmailGrantPresent",L"A Google credential is saved. Reconnect after changing Headers/Snippet access.");
    SetWindowTextW(s->client,UiLiteralText(label).c_str());Buttons(s,s->busy);return true;
}
static bool Save(const std::shared_ptr<State>& s,const std::vector<std::pair<const wchar_t*,std::wstring>>& settings,std::wstring& error){
    if(!Live(s))return false;const auto expected=s->fingerprint;std::wstring committed;
    const bool saved=MutateDesktopStubIniWithWait([&](std::wstring& text){
        if(!IEquals(aip::MakeAbsolutePath(g_iniPath),s->profile)){error=Text(L"GmailProfileChanged",L"The active profile changed. Reopen Mail setup before continuing.");return false;}
        auto document=aip::ParseIniDocument(text);aip::content::TileInstanceConfiguration cfg;
        const auto read=[&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){std::wstring value;return aip::ReadIniValueFromDoc(document,section,key,value)?value:std::wstring(fallback);};
        if(!ValidateTileInstanceHostConfiguration(read,cfg,error))return false;
        const auto tile=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& item){return item.id==s->tileId;});
        if(tile==cfg.tiles.end()||tile->type!=aip::content::TileInstanceType::Mail||Fingerprint(*tile)!=expected){error=Text(L"GmailTileChanged",L"This Mail tile or its read permissions changed. Reopen setup; no previous account was replaced.");return false;}
        auto candidate=text;const auto section=aip::content::TileInstanceSection(s->tileId);
        for(const auto& setting:settings)if(!aip::WriteIniValueToText(candidate,section.c_str(),setting.first,setting.second))return false;
        document=aip::ParseIniDocument(candidate);if(!ValidateTileInstanceHostConfiguration(read,cfg,error))return false;
        const auto result=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& item){return item.id==s->tileId;});
        if(result==cfg.tiles.end())return false;committed=Fingerprint(*result);text=std::move(candidate);return true;
    },CurrentIniWriteLockWaitMs());
    if(!saved){if(error.empty())error=Text(L"GmailSaveFailed",L"Mail account settings could not be saved. Previous settings were kept.");return false;}
    if(!Reload(s)||s->fingerprint!=committed){error=Text(L"GmailChangedAfterSave",L"Settings changed again after saving. Reopen Mail setup before continuing.");return false;}
    RefreshTileInstanceNow(s->tileId);return true;
}
static bool Import(const std::shared_ptr<State>& s,const std::wstring& path){
    if(!Live(s)||s->busy||path.empty())return false;aip::content::TileInstance tile;std::wstring error;if(!Current(s,tile,error)){Status(s,error);return false;}
    std::vector<BYTE> bytes;if(!aip::ReadWholeFileBytes(path,bytes,131072)){Status(s,Text(L"GmailImportFailed",L"Choose an available Google Desktop OAuth JSON file of at most 128 KiB."));return false;}
    std::string json(bytes.begin(),bytes.end());if(json.size()>=3&&json.compare(0,3,"\xEF\xBB\xBF")==0)json.erase(0,3);
    auto account=s->configuration.account;const bool parsed=aip::google_calendar::ImportDesktopClientJson(json,account,error);
    aip::google_calendar::detail::Wipe(json);if(!bytes.empty())SecureZeroMemory(bytes.data(),bytes.size());
    if(!parsed){Status(s,error);return false;}
    if(!Save(s,{{L"Mail.ClientId",account.clientId},{L"Mail.ClientSecret",account.clientSecret},{L"Mail.RefreshTokenProtected",L""}},error)){Status(s,error);return false;}
    Cancel(s);Buttons(s,false);Status(s,Text(L"GmailImported",L"Desktop client imported. Enable the Gmail API in that Google Cloud project, then choose Connect to Google."));return true;
}
static void ChooseImport(const std::shared_ptr<State>& s){
    wchar_t file[32768]{};OPENFILENAMEW picker{};picker.lStructSize=sizeof(picker);picker.hwndOwner=s->window;picker.lpstrFile=file;
    picker.nMaxFile=static_cast<DWORD>(std::size(file));picker.lpstrFilter=L"Google OAuth JSON (*.json)\0*.json\0All files\0*.*\0";
    const auto title=Text(L"GmailImportTitle",L"Import your Google Cloud Desktop OAuth credentials");picker.lpstrTitle=title.c_str();
    picker.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;if(GetOpenFileNameW(&picker))Import(s,file);
}
static void Complete(const std::shared_ptr<State>& s){
    if(!Live(s)||!s->job||!s->job->done.load(std::memory_order_acquire)||s->job->canceled.load()||s->job->sequence!=s->sequence)return;
    if(GetTickCount64()-s->started>240000){Cancel(s);Buttons(s,false);Status(s,Text(L"GmailExpired",L"Gmail setup expired. No pending account was saved. Start Connect again when ready."));return;}
    const auto job=s->job;KillTimer(s->window,1);aip::content::TileInstance tile;std::wstring error;
    if(!Current(s,tile,error)){Cancel(s);Buttons(s,false);Status(s,error);return;}
    if(job->connected){
        if(!Save(s,{{L"Mail.RefreshTokenProtected",job->credentials.refreshTokenProtected}},error)){Cancel(s);Buttons(s,false);Status(s,error);return;}
        NotifyUserOperation(L"gmail-connected-"+s->tileId,Text(L"GmailConnectedTitle",L"Gmail connected"),Text(L"GmailConnectedMessage",L"Read-only Gmail access was saved for this Mail tile with Windows protection."));
        Status(s,job->error.empty()?Text(L"GmailConnected",L"Gmail is connected. This tile refreshes while pinned; you may close this window."):aip::gmail::detail::MailError(job->error));
    }else Status(s,job->error.empty()?Text(L"GmailConnectFailed",L"Gmail sign-in did not complete. Previous saved settings were kept."):aip::gmail::detail::MailError(job->error));
    s->job.reset();Buttons(s,false);workers.ReapCompleted();
}
static void Start(const std::shared_ptr<State>& s){
    if(!Live(s)||s->busy)return;workers.ReapCompleted();if(workers.Size()>=8){Status(s,Text(L"GmailWorkerBusy",L"Previous sign-ins are stopping. Try again shortly."));return;}
    aip::content::TileInstance tile;std::wstring error;if(!Current(s,tile,error)){Status(s,error);return;}
    auto job=std::make_shared<Job>();job->configuration=GmailConfigurationForTile(tile);job->session=std::make_shared<aip::gmail::Session>();
    if(!aip::google_calendar::RuntimeConfigValidate(job->configuration.account,error,false)){Status(s,aip::gmail::detail::MailError(error));return;}
    job->sequence=s->sequence=++nextSequence;s->job=job;s->session=job->session;s->started=GetTickCount64();Buttons(s,true);
    Status(s,Text(L"GmailConnecting",L"Continue Google sign-in in your browser. Cancel closes the local listener; no Google password is entered here."));
    if(!SetTimer(s->window,1,250,nullptr)){Cancel(s);Buttons(s,false);Status(s,Text(L"GmailTimerFailed",L"Windows could not start sign-in completion monitoring. No account request was started."));return;}
    const auto window=s->window;const auto failure=Text(L"GmailConnectFailed",L"Gmail sign-in did not complete. Previous saved settings were kept.");
    const std::weak_ptr<Job> weak=job;
    if(!workers.Start([weak]{if(const auto active=weak.lock())active->canceled=true;},[job,window,failure]{
        struct Done{std::shared_ptr<Job> job;HWND window;~Done(){job->done.store(true,std::memory_order_release);if(!job->canceled.load()&&g_running.load())PostMessageW(window,WM_READY,static_cast<WPARAM>(job->sequence),0);}}done{job,window};
        const auto canceled=[job]{return job->canceled.load()||!g_running.load();};
        try{
            job->connected=aip::google_calendar::Connect(job->configuration.account,job->credentials,job->error,canceled,{},job->session.get());
            if(job->connected&&!canceled()){auto connected=job->configuration;connected.account.refreshTokenProtected=job->credentials.refreshTokenProtected;
                aip::gmail::FetchMailboxAddress(connected,job->mailboxAddress,job->error,canceled,{},job->session.get());}
        }catch(...){job->connected=false;job->error=failure;}
    })){Cancel(s);Buttons(s,false);Status(s,Text(L"GmailWorkerFailed",L"The Gmail sign-in worker could not start. No account was changed."));}
}
static void Disconnect(const std::shared_ptr<State>& s){
    Cancel(s);std::wstring error;if(!Save(s,{{L"Mail.RefreshTokenProtected",L""}},error)){Buttons(s,false);Status(s,error);return;}
    Buttons(s,false);Status(s,Text(L"GmailDisconnected",L"Disconnected on this device. No email was changed or deleted. Google account permissions can be reviewed separately."));
    NotifyUserOperation(L"gmail-disconnected-"+s->tileId,Text(L"GmailDisconnectedTitle",L"Gmail disconnected"),Text(L"GmailDisconnectedMessage",L"This Mail tile's saved Google grant was removed. No email was changed."));
}
static void Layout(const std::shared_ptr<State>& s){
    if(!Live(s))return;RECT area{};GetClientRect(s->window,&area);const int width=(std::max)(580,static_cast<int>(area.right));
    MoveWindow(s->instructions,12,12,width-24,76,TRUE);MoveWindow(s->client,12,94,width-24,32,TRUE);
    MoveWindow(s->import,12,136,190,30,TRUE);MoveWindow(s->connect,214,136,165,30,TRUE);MoveWindow(s->disconnect,391,136,180,30,TRUE);
    MoveWindow(s->status,12,182,width-24,(std::max)(48,static_cast<int>(area.bottom)-238),TRUE);
    MoveWindow(s->cancel,12,area.bottom-44,190,30,TRUE);MoveWindow(s->close,width-112,area.bottom-44,100,30,TRUE);
}
static LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) noexcept {
    try{const auto found=windows.find(window);if(found==windows.end())return DefWindowProcW(window,message,wParam,lParam);const auto s=found->second;
        if(message==WM_SIZE){Layout(s);return 0;}if(message==WM_GETMINMAXINFO){reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize={620,350};return 0;}
        if(message==WM_READY){if(s->job&&s->job->sequence==static_cast<unsigned long long>(wParam))Complete(s);return 0;}
        if(message==WM_TIMER&&wParam==1){Complete(s);if(s->busy&&GetTickCount64()-s->started>240000){Cancel(s);Buttons(s,false);Status(s,Text(L"GmailExpired",L"Gmail setup expired. No pending account was saved. Start Connect again when ready."));}return 0;}
        if(message==WM_COMMAND){switch(LOWORD(wParam)){
            case ID_IMPORT:if(!s->busy)ChooseImport(s);return 0;case ID_CONNECT:Start(s);return 0;
            case ID_DISCONNECT:if(!s->busy)Disconnect(s);return 0;case ID_CANCEL_JOB:Cancel(s);Buttons(s,false);Status(s,Text(L"GmailCanceled",L"Sign-in canceled. You may close the Google tab. Saved account settings were kept."));return 0;
            case IDCANCEL:DestroyWindow(window);return 0;}}
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}if(message==WM_NCDESTROY){s->closed=true;Cancel(s);windows.erase(found);}
    }catch(...){try{const auto found=windows.find(window);if(found!=windows.end()){Cancel(found->second);Buttons(found->second,false);Status(found->second,Text(L"GmailSetupFailed",L"Mail setup could not complete. Reopen it to retry; previous settings were kept."));}}catch(...){}return 0;}
    return DefWindowProcW(window,message,wParam,lParam);
}
}
static bool OpenGmailSetup(HWND owner,const std::wstring& tileId) try {
    using namespace gmail_setup;workers.ReapCompleted();const auto profile=aip::MakeAbsolutePath(g_iniPath);
    for(const auto& entry:windows)if(entry.second->tileId==tileId&&IEquals(entry.second->profile,profile)){ShowWindow(entry.first,SW_RESTORE);SetForegroundWindow(entry.first);return true;}
    if(windows.size()>=8)return false;aip::content::TileInstanceConfiguration cfg;std::wstring error;if(!ReadCurrentTileInstances(cfg,error))return false;
    const auto tile=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& item){return item.id==tileId;});if(tile==cfg.tiles.end()||tile->type!=aip::content::TileInstanceType::Mail)return false;
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=WindowProc;cls.lpszClassName=L"DesktopStubGmailSetup";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;auto s=std::make_shared<State>();s->tileId=tileId;s->profile=profile;s->configuration=GmailConfigurationForTile(*tile);s->fingerprint=Fingerprint(*tile);
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);RECT work{};if(GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor))work=monitor.rcWork;else SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const int width=660,height=370;const auto title=tile->name+Text(L"GmailWindowSuffix",L" — Gmail account");
    s->window=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,cls.lpszClassName,UiLiteralText(title,true).c_str(),WS_OVERLAPPEDWINDOW,
        (work.left+work.right-width)/2,(work.top+work.bottom-height)/2,width,height,owner,nullptr,cls.hInstance,nullptr);if(!s->window)return false;
    struct Guard{HWND window;bool released=false;~Guard(){if(!released&&IsWindow(window))DestroyWindow(window);}}guard{s->window};windows.emplace(s->window,s);
    const auto child=[&](const wchar_t* type,const std::wstring& caption,DWORD style,int id){const auto control=CreateWindowExW(0,type,UiLiteralText(caption).c_str(),WS_CHILD|WS_VISIBLE|style,0,0,1,1,s->window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),cls.hInstance,nullptr);
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return control;};
    s->instructions=child(L"STATIC",Text(L"GmailInstructions",L"Import your Google Cloud Desktop app JSON with the Gmail API enabled, then connect in your browser. Headers access reads sender/subject only. Snippet access is optional in the tile's source settings and requires reconnecting. No sending or editing permission is requested."),SS_NOPREFIX,0);
    s->client=child(L"STATIC",L"",SS_NOPREFIX,0);s->import=child(L"BUTTON",Text(L"GmailImportButton",L"Import Desktop OAuth JSON..."),BS_PUSHBUTTON|WS_TABSTOP,ID_IMPORT);
    s->connect=child(L"BUTTON",Text(L"GmailConnectButton",L"Connect to Google..."),BS_PUSHBUTTON|WS_TABSTOP,ID_CONNECT);
    s->disconnect=child(L"BUTTON",Text(L"GmailDisconnectButton",L"Disconnect on this device"),BS_PUSHBUTTON|WS_TABSTOP,ID_DISCONNECT);
    s->cancel=child(L"BUTTON",Text(L"GmailCancelButton",L"Cancel sign-in"),BS_PUSHBUTTON|WS_TABSTOP,ID_CANCEL_JOB);
    s->close=child(L"BUTTON",Text(L"GmailCloseButton",L"Close"),BS_PUSHBUTTON|WS_TABSTOP,IDCANCEL);
    s->status=child(L"STATIC",Text(L"GmailInitial",L"No browser or account request starts until you choose Connect."),SS_NOPREFIX,0);
    if(!s->instructions||!s->client||!s->import||!s->connect||!s->disconnect||!s->cancel||!s->close||!s->status)return false;
    if(!Reload(s))return false;Layout(s);ShowWindow(s->window,SW_SHOW);SetForegroundWindow(s->window);SetFocus(s->import);guard.released=true;return true;
}catch(...){return false;}
static bool ImportGmailClientForTile(HWND owner,const std::wstring& tileId,const std::wstring& path) try {
    if(!OpenGmailSetup(owner,tileId))return false;const auto profile=aip::MakeAbsolutePath(g_iniPath);
    for(const auto& entry:gmail_setup::windows)if(entry.second->tileId==tileId&&IEquals(entry.second->profile,profile))return gmail_setup::Import(entry.second,path);return false;
}catch(...){return false;}
static bool IsGmailSetupMessage(MSG& message){for(const auto& entry:gmail_setup::windows)if(IsDialogMessageW(entry.first,&message))return true;return false;}
static void CloseGmailSetups(){for(const auto& entry:gmail_setup::windows)gmail_setup::Cancel(entry.second);gmail_setup::workers.CancelAndJoin();
    while(!gmail_setup::windows.empty()){const auto window=gmail_setup::windows.begin()->first;if(!IsWindow(window)||!DestroyWindow(window))gmail_setup::windows.erase(window);}}
#else
static bool OpenGmailSetup(HWND,const std::wstring&){return false;}
static bool ImportGmailClientForTile(HWND,const std::wstring&,const std::wstring&){return false;}
static bool IsGmailSetupMessage(MSG&){return false;}
static void CloseGmailSetups(){}
#endif
