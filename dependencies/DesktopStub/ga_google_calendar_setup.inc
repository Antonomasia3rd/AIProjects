// Modeless, explicitly opened Google Calendar setup. Browser authorization and
// Calendar HTTP requests run on copied, cancelable worker inputs. No password,
// browser cookie or credential from another application is inspected.
#ifndef DESKTOPSTUB_INERT_HARDWARE
#include "../content_sources/google_calendar.inc"
#include "../content_sources/notes_signin_workers.h"
namespace google_calendar_setup {
static constexpr UINT WM_READY=WM_APP+187;
static constexpr int ID_IMPORT=210,ID_CONNECT=211,ID_LIST=212,ID_CALENDARS=213,ID_SAVE=214,ID_DISCONNECT=215,ID_CANCEL_JOB=216;
enum class Operation {Connect,List};
struct Job {
    std::atomic<bool> canceled{false},done{false};unsigned long long sequence=0;
    Operation operation=Operation::Connect;bool connected=false,listed=false;
    aip::google_calendar::Configuration configuration;
    aip::google_calendar::Credentials credentials;
    std::vector<aip::google_calendar::CalendarChoice> calendars;std::wstring error,failureText;
    std::shared_ptr<aip::google_calendar::Session> session;
};
struct State {
    HWND window=nullptr,instructions=nullptr,client=nullptr,import=nullptr,connect=nullptr,list=nullptr,calendars=nullptr,
        save=nullptr,disconnect=nullptr,cancelJob=nullptr,close=nullptr,status=nullptr;
    bool closed=false,busy=false;unsigned long long sequence=0;ULONGLONG started=0;
    std::wstring profile,tileId,fingerprint,choiceAccountKey;
    aip::google_calendar::Configuration configuration;
    std::vector<aip::google_calendar::CalendarChoice> choices;
    std::shared_ptr<aip::google_calendar::Session> session=std::make_shared<aip::google_calendar::Session>();
    std::shared_ptr<Job> job;
};
static std::map<HWND,std::shared_ptr<State>> windows;
static aip::notes::SignInWorkers workers;
static unsigned long long nextSequence=0;
static std::wstring Text(const wchar_t* key,const wchar_t* fallback){return IniReadS(L"Strings",key,fallback);}
static bool Live(const std::shared_ptr<State>& state){return state&&!state->closed&&IsWindow(state->window);}
static void Status(const std::shared_ptr<State>& state,const std::wstring& text){if(Live(state))SetWindowTextW(state->status,UiLiteralText(text).c_str());}
static std::wstring Fingerprint(const aip::content::TileInstance& tile){return tile.calendar.source+L"\n"+
    aip::google_calendar::ConfigurationKey(GoogleCalendarConfigurationForTile(tile));}
static std::wstring AccountKey(const aip::google_calendar::Configuration& configuration){return configuration.ownerKey+L"\n"+
    configuration.clientId+L"\n"+configuration.clientSecret+L"\n"+configuration.refreshTokenProtected;}
static void EmptyChoices(const std::shared_ptr<State>& state);
static bool Current(const std::shared_ptr<State>& state,aip::content::TileInstance& tile,std::wstring& error,bool requireSame=true){
    if(!Live(state)||!IEquals(aip::MakeAbsolutePath(g_iniPath),state->profile)){error=Text(L"GoogleCalendarProfileChanged",L"The active profile changed. Reopen Google Calendar setup before continuing.");return false;}
    aip::content::TileInstanceConfiguration cfg;if(!ReadCurrentTileInstances(cfg,error))return false;
    const auto found=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& item){return item.id==state->tileId;});
    if(found==cfg.tiles.end()||found->type!=aip::content::TileInstanceType::Calendar||
        (requireSame&&Fingerprint(*found)!=state->fingerprint)){
        error=Text(L"GoogleCalendarTileChanged",L"This tile or its Google settings changed. Reopen setup; no previous account was replaced.");return false;}
    tile=*found;return true;
}
static void Buttons(const std::shared_ptr<State>& state,bool busy){
    state->busy=busy;if(!Live(state))return;
    EnableWindow(state->import,!busy);EnableWindow(state->connect,!busy&&!state->configuration.clientId.empty());
    EnableWindow(state->list,!busy&&!state->configuration.refreshTokenProtected.empty());
    EnableWindow(state->calendars,!busy&&!state->choices.empty());EnableWindow(state->save,!busy&&!state->choices.empty());
    EnableWindow(state->disconnect,!state->configuration.refreshTokenProtected.empty());EnableWindow(state->cancelJob,busy);
}
static void Cancel(const std::shared_ptr<State>& state) noexcept {
    if(state->job)state->job->canceled=true;state->sequence=++nextSequence;state->job.reset();
    try{if(state->session)state->session->Clear();}catch(...){}
    // The worker retains its own session reference until it has stopped. A
    // later operation allocates a fresh session; closing a window never allocates.
    state->session.reset();
    if(state->window)KillTimer(state->window,1);
}
static bool Reload(const std::shared_ptr<State>& state){
    aip::content::TileInstance tile;std::wstring error;if(!Current(state,tile,error,false)){Status(state,error);return false;}
    const auto configuration=GoogleCalendarConfigurationForTile(tile);
    if(!state->choices.empty()&&state->choiceAccountKey!=AccountKey(configuration))EmptyChoices(state);
    state->configuration=configuration;state->fingerprint=Fingerprint(tile);
    const auto status=state->configuration.clientId.empty()?Text(L"GoogleCalendarClientMissing",L"No Desktop OAuth client imported."):
        state->configuration.refreshTokenProtected.empty()?Text(L"GoogleCalendarClientReady",L"Desktop OAuth client ready. Choose Connect to Google."):
        Text(L"GoogleCalendarClientConnected",L"Google account connected. Refresh the calendar list or choose a calendar.");
    SetWindowTextW(state->client,UiLiteralText(status).c_str());Buttons(state,state->busy);return true;
}
static bool Save(const std::shared_ptr<State>& state,const std::vector<std::pair<const wchar_t*,std::wstring>>& settings,std::wstring& error){
    if(!Live(state))return false;
    const auto expected=state->fingerprint;std::wstring committedFingerprint;
    const auto saved=MutateDesktopStubIniWithWait([&](std::wstring& text){
        if(!IEquals(aip::MakeAbsolutePath(g_iniPath),state->profile)){error=Text(L"GoogleCalendarProfileChanged",L"The active profile changed. Reopen Google Calendar setup before continuing.");return false;}
        auto document=aip::ParseIniDocument(text);aip::content::TileInstanceConfiguration cfg;
        const auto read=[&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){std::wstring value;return aip::ReadIniValueFromDoc(document,section,key,value)?value:std::wstring(fallback);};
        if(!ValidateTileInstanceHostConfiguration(read,cfg,error))return false;
        const auto found=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& tile){return tile.id==state->tileId;});
        if(found==cfg.tiles.end()||found->type!=aip::content::TileInstanceType::Calendar||Fingerprint(*found)!=expected){
            error=Text(L"GoogleCalendarTileChanged",L"This tile or its Google settings changed. Reopen setup; no previous account was replaced.");return false;}
        auto candidate=text;const auto section=aip::content::TileInstanceSection(state->tileId);
        for(const auto& setting:settings)if(!aip::WriteIniValueToText(candidate,section.c_str(),setting.first,setting.second))return false;
        document=aip::ParseIniDocument(candidate);if(!ValidateTileInstanceHostConfiguration(read,cfg,error))return false;
        const auto committed=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& tile){return tile.id==state->tileId;});
        if(committed==cfg.tiles.end())return false;committedFingerprint=Fingerprint(*committed);
        text=std::move(candidate);return true;
    },CurrentIniWriteLockWaitMs());
    if(!saved){if(error.empty())error=Text(L"GoogleCalendarSaveFailed",L"Google Calendar settings could not be saved. Previous settings were kept.");return false;}
    if(!Reload(state))return false;
    if(state->fingerprint!=committedFingerprint){error=Text(L"GoogleCalendarChangedAfterSave",L"Settings changed again after this save. Reopen setup to use the current account and calendar.");return false;}
    RefreshTileInstanceNow(state->tileId);return true;
}
static void EmptyChoices(const std::shared_ptr<State>& state){state->choices.clear();state->choiceAccountKey.clear();SendMessageW(state->calendars,CB_RESETCONTENT,0,0);}
static bool Import(const std::shared_ptr<State>& state,const std::wstring& path){
    if(!Live(state)||state->busy||path.empty())return false;aip::content::TileInstance tile;std::wstring error;
    if(!Current(state,tile,error)){Status(state,error);return false;}
    std::vector<BYTE> bytes;if(!aip::ReadWholeFileBytes(path,bytes,131072)){Status(state,Text(L"GoogleCalendarImportReadFailed",L"Could not read the selected Desktop OAuth JSON. The file must be available and at most 128 KiB."));return false;}
    std::string json(bytes.begin(),bytes.end());if(json.size()>=3&&json.compare(0,3,"\xEF\xBB\xBF")==0)json.erase(0,3);
    auto configuration=state->configuration;
    const bool parsed=aip::google_calendar::ImportDesktopClientJson(json,configuration,error);
    aip::google_calendar::detail::Wipe(json);if(!bytes.empty())SecureZeroMemory(bytes.data(),bytes.size());
    if(!parsed){Status(state,error);return false;}
    const bool saved=Save(state,{{L"GoogleCalendar.ClientId",configuration.clientId},{L"GoogleCalendar.ClientSecret",configuration.clientSecret},
        {L"GoogleCalendar.RefreshTokenProtected",L""},{L"Calendar.Source",L"Google"}},error);
    if(!saved){Status(state,error);return false;}Cancel(state);EmptyChoices(state);Buttons(state,false);
    Status(state,Text(L"GoogleCalendarImported",L"Desktop OAuth client imported. Choose Connect to Google, then approve read-only Calendar access in your browser."));return true;
}
static void ChooseImport(const std::shared_ptr<State>& state){
    wchar_t file[32768]{};OPENFILENAMEW picker{};picker.lStructSize=sizeof(picker);picker.hwndOwner=state->window;
    picker.lpstrFile=file;picker.nMaxFile=static_cast<DWORD>(std::size(file));picker.lpstrFilter=L"Google OAuth JSON (*.json)\0*.json\0All files\0*.*\0";
    const auto title=Text(L"GoogleCalendarImportTitle",L"Import your Google Cloud Desktop OAuth credentials");picker.lpstrTitle=title.c_str();
    picker.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
    if(GetOpenFileNameW(&picker))Import(state,file);
}
static void Complete(const std::shared_ptr<State>& state);
static void Start(const std::shared_ptr<State>& state,Operation operation){
    if(!Live(state)||state->busy)return;workers.ReapCompleted();if(workers.Size()>=8){Status(state,Text(L"GoogleCalendarWorkerBusy",L"Previous requests are stopping. Try again in a moment."));return;}
    aip::content::TileInstance tile;std::wstring error;if(!Current(state,tile,error)){Status(state,error);return;}
    if(!state->session)state->session=std::make_shared<aip::google_calendar::Session>();
    auto job=std::make_shared<Job>();job->configuration=GoogleCalendarConfigurationForTile(tile);job->operation=operation;job->session=state->session;
    job->failureText=Text(L"GoogleCalendarWorkerFailure",L"Google Calendar setup could not complete. No new account was saved; try again when ready.");
    if(!aip::google_calendar::RuntimeConfigValidate(job->configuration,error,operation==Operation::List)){Status(state,error);return;}
    job->sequence=state->sequence=++nextSequence;state->job=job;state->started=GetTickCount64();Buttons(state,true);
    Status(state,operation==Operation::Connect?Text(L"GoogleCalendarConnecting",L"Continue Google sign-in in your default browser. This window stays available; Cancel operation closes the local listener."):
        Text(L"GoogleCalendarListing",L"Reading the calendars you allowed this app to access..."));
    const auto window=state->window;
    if(!SetTimer(window,1,250,nullptr)){
        Cancel(state);Buttons(state,false);Status(state,Text(L"GoogleCalendarTimerFailed",L"Windows could not start the setup completion timer. No browser or account request was started; try again."));return;
    }
    if(!workers.Start([job]{job->canceled=true;},[job,window]{
        struct Completion {
            std::shared_ptr<Job> job;HWND window;
            ~Completion(){job->done.store(true,std::memory_order_release);
                if(!job->canceled.load()&&g_running.load())PostMessageW(window,WM_READY,static_cast<WPARAM>(job->sequence),0);}
        } completion{job,window};
        const auto canceled=[job]{return job->canceled.load()||!g_running.load();};
        try{
            if(job->operation==Operation::Connect){job->connected=aip::google_calendar::Connect(job->configuration,job->credentials,job->error,canceled,{},job->session.get());
                if(job->connected&&!canceled()){auto connected=job->configuration;connected.refreshTokenProtected=job->credentials.refreshTokenProtected;
                    job->listed=aip::google_calendar::ListCalendars(connected,job->calendars,job->error,canceled,{},job->session.get());}}
            else job->listed=aip::google_calendar::ListCalendars(job->configuration,job->calendars,job->error,canceled,{},job->session.get());
        }catch(...){job->connected=false;job->listed=false;job->error=std::move(job->failureText);}
    })){
        Cancel(state);Buttons(state,false);Status(state,Text(L"GoogleCalendarWorkerFailed",L"The Google Calendar worker could not start. No account was changed."));
    }
}
static std::wstring ChoiceLabel(const aip::google_calendar::CalendarChoice& calendar){
    auto label=calendar.name;for(auto& ch:label)if(ch<0x20||ch==0x7f)ch=L' ';if(label.size()>180)label=label.substr(0,177)+L"...";
    if(calendar.primary)label+=Text(L"GoogleCalendarPrimarySuffix",L" (primary)");return label;
}
static void Complete(const std::shared_ptr<State>& state){
    if(!Live(state)||!state->job||!state->job->done.load(std::memory_order_acquire)||state->job->sequence!=state->sequence||state->job->canceled.load())return;
    const auto job=state->job;KillTimer(state->window,1);aip::content::TileInstance tile;std::wstring error;
    if(!Current(state,tile,error)){Cancel(state);Buttons(state,false);Status(state,error);return;}
    if(job->operation==Operation::Connect&&job->connected){
        if(!Save(state,{{L"GoogleCalendar.RefreshTokenProtected",job->credentials.refreshTokenProtected},{L"Calendar.Source",L"Google"}},error)){
            Cancel(state);Buttons(state,false);Status(state,error);return;}
        NotifyUserOperation(L"google-calendar-connected-"+state->tileId,Text(L"GoogleCalendarConnectedTitle",L"Google Calendar connected"),
            Text(L"GoogleCalendarConnectedMessage",L"Read-only Calendar access was saved for this tile with Windows protection."));
    }
    if(job->listed){
        EmptyChoices(state);state->choices=job->calendars;state->choiceAccountKey=AccountKey(state->configuration);int selected=-1;
        for(std::size_t index=0;index<state->choices.size();++index){const auto& choice=state->choices[index];const auto label=ChoiceLabel(choice);
            SendMessageW(state->calendars,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            if(choice.id==state->configuration.calendarId||(state->configuration.calendarId==L"primary"&&choice.primary))selected=static_cast<int>(index);}
        if(selected<0&&state->choices.size()==1)selected=0;if(selected>=0)SendMessageW(state->calendars,CB_SETCURSEL,selected,0);
        Status(state,state->choices.empty()?Text(L"GoogleCalendarNoCalendars",L"No readable calendars were returned. Check this account's calendar sharing permissions."):
            Text(L"GoogleCalendarChoose",L"Choose a calendar and click Use selected calendar. Each Calendar tile can select its own account and calendar."));
    }else Status(state,job->error.empty()?Text(L"GoogleCalendarRequestFailed",L"Google Calendar could not complete this request. Previous saved settings were kept."):job->error);
    state->job.reset();Buttons(state,false);workers.ReapCompleted();
}
static void SaveSelected(const std::shared_ptr<State>& state){
    if(state->busy)return;const auto selected=SendMessageW(state->calendars,CB_GETCURSEL,0,0);
    if(state->choiceAccountKey!=AccountKey(state->configuration)){EmptyChoices(state);Buttons(state,false);
        Status(state,Text(L"GoogleCalendarTileChanged",L"This tile or its Google settings changed. Reopen setup; no previous account was replaced."));return;}
    if(selected==CB_ERR||static_cast<std::size_t>(selected)>=state->choices.size()){Status(state,Text(L"GoogleCalendarSelectFirst",L"Select a calendar from the list first."));return;}
    const auto calendar=state->choices[static_cast<std::size_t>(selected)];std::wstring error;
    if(!Save(state,{{L"GoogleCalendar.CalendarId",calendar.id},{L"Calendar.Source",L"Google"}},error)){Status(state,error);return;}
    Status(state,Text(L"GoogleCalendarSelectedSaved",L"Selected calendar saved. This tile will refresh automatically. You may close this window."));
    NotifyUserOperation(L"google-calendar-selected-"+state->tileId,Text(L"GoogleCalendarSelectedTitle",L"Calendar selected"),
        Text(L"GoogleCalendarSelectedMessage",L"This Calendar tile will display the selected Google calendar's upcoming events."));
}
static void Disconnect(const std::shared_ptr<State>& state){
    Cancel(state);std::wstring error;if(!Save(state,{{L"GoogleCalendar.RefreshTokenProtected",L""}},error)){Buttons(state,false);Status(state,error);return;}
    EmptyChoices(state);Buttons(state,false);Status(state,Text(L"GoogleCalendarDisconnected",L"Disconnected on this device. The saved grant and memory token were removed. Google account permissions can be reviewed separately in your account."));
    NotifyUserOperation(L"google-calendar-disconnected-"+state->tileId,Text(L"GoogleCalendarDisconnectedTitle",L"Google Calendar disconnected"),
        Text(L"GoogleCalendarDisconnectedMessage",L"This tile's saved Google grant was removed. No Google calendar or event was deleted."));
}
static void Layout(const std::shared_ptr<State>& state){
    if(!Live(state))return;RECT area{};GetClientRect(state->window,&area);const int width=(std::max)(400,static_cast<int>(area.right)),margin=12;
    MoveWindow(state->instructions,margin,12,width-24,48,TRUE);MoveWindow(state->client,margin,66,width-24,32,TRUE);
    MoveWindow(state->import,margin,106,190,30,TRUE);MoveWindow(state->connect,214,106,165,30,TRUE);MoveWindow(state->list,391,106,190,30,TRUE);
    MoveWindow(state->calendars,margin,150,width-24,250,TRUE);MoveWindow(state->save,margin,192,190,30,TRUE);
    MoveWindow(state->disconnect,214,192,165,30,TRUE);MoveWindow(state->cancelJob,391,192,190,30,TRUE);
    MoveWindow(state->status,margin,238,width-24,(std::max)(48,static_cast<int>(area.bottom)-294),TRUE);MoveWindow(state->close,width-112,area.bottom-44,100,30,TRUE);
}
static LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) noexcept {
    try{
        const auto found=windows.find(window);if(found==windows.end())return DefWindowProcW(window,message,wParam,lParam);const auto state=found->second;
        if(message==WM_SIZE){Layout(state);return 0;}
        if(message==WM_GETMINMAXINFO){auto info=reinterpret_cast<MINMAXINFO*>(lParam);info->ptMinTrackSize={620,360};return 0;}
        if(message==WM_READY){if(state->job&&state->job->sequence==static_cast<unsigned long long>(wParam))Complete(state);return 0;}
        if(message==WM_TIMER&&wParam==1){Complete(state);if(state->busy&&GetTickCount64()-state->started>240000){Cancel(state);Buttons(state,false);
            Status(state,Text(L"GoogleCalendarExpired",L"Google Calendar setup expired. No pending account was saved. Start Connect again when ready."));}return 0;}
        if(message==WM_COMMAND){switch(LOWORD(wParam)){
            case ID_IMPORT:if(!state->busy)ChooseImport(state);return 0;
            case ID_CONNECT:Start(state,Operation::Connect);return 0;
            case ID_LIST:Start(state,Operation::List);return 0;
            case ID_SAVE:SaveSelected(state);return 0;
            case ID_DISCONNECT:Disconnect(state);return 0;
            case ID_CANCEL_JOB:Cancel(state);Buttons(state,false);Status(state,Text(L"GoogleCalendarCanceled",L"The pending operation was canceled. You may close the Google browser tab. Saved account settings were kept."));return 0;
            case IDCANCEL:DestroyWindow(window);return 0;
        }}
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}
        if(message==WM_NCDESTROY){state->closed=true;Cancel(state);windows.erase(found);}
    }catch(...){try{const auto found=windows.find(window);if(found!=windows.end()){Cancel(found->second);Buttons(found->second,false);
        Status(found->second,Text(L"GoogleCalendarSetupFailed",L"Google Calendar setup could not complete. Previous saved settings were kept; reopen setup to retry."));}}catch(...){}return 0;}
    return DefWindowProcW(window,message,wParam,lParam);
}
}
static bool OpenGoogleCalendarSetup(HWND owner,const std::wstring& tileId) try {
    using namespace google_calendar_setup;workers.ReapCompleted();const auto profile=aip::MakeAbsolutePath(g_iniPath);
    for(const auto& item:windows)if(item.second->tileId==tileId&&IEquals(item.second->profile,profile)){ShowWindow(item.first,SW_RESTORE);SetForegroundWindow(item.first);return true;}
    if(windows.size()>=8)return false;aip::content::TileInstanceConfiguration cfg;std::wstring error;if(!ReadCurrentTileInstances(cfg,error))return false;
    const auto found=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& tile){return tile.id==tileId;});
    if(found==cfg.tiles.end()||found->type!=aip::content::TileInstanceType::Calendar)return false;
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=WindowProc;cls.lpszClassName=L"DesktopStubGoogleCalendarSetup";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    auto state=std::make_shared<State>();state->tileId=tileId;state->profile=profile;state->configuration=GoogleCalendarConfigurationForTile(*found);state->fingerprint=Fingerprint(*found);
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);RECT work{};
    if(GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor))work=monitor.rcWork;else SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const int width=700,height=410;const auto title=found->name+Text(L"GoogleCalendarWindowSuffix",L" — Google Calendar");
    state->window=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,cls.lpszClassName,UiLiteralText(title,true).c_str(),WS_OVERLAPPEDWINDOW,
        (work.left+work.right-width)/2,(work.top+work.bottom-height)/2,width,height,owner,nullptr,cls.hInstance,nullptr);
    if(!state->window)return false;
    struct NewWindowGuard {HWND window;bool released=false;~NewWindowGuard(){if(!released&&IsWindow(window))DestroyWindow(window);}} guard{state->window};
    windows.emplace(state->window,state);
    const auto child=[&](const wchar_t* type,const std::wstring& caption,DWORD style,int id){
        auto control=CreateWindowExW(std::wstring(type)==L"COMBOBOX"?WS_EX_CLIENTEDGE:0,type,UiLiteralText(caption).c_str(),WS_CHILD|WS_VISIBLE|style,
            0,0,1,1,state->window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),cls.hInstance,nullptr);
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return control;};
    state->instructions=child(L"STATIC",Text(L"GoogleCalendarSetupInstructions",L"Import the credentials JSON for your Google Cloud Desktop app, then connect in your default browser. Only read-only Calendar access is requested; DesktopStub never asks for your Google password."),SS_NOPREFIX,0);
    state->client=child(L"STATIC",L"",SS_NOPREFIX,0);
    state->import=child(L"BUTTON",Text(L"GoogleCalendarImportButton",L"Import Desktop OAuth JSON..."),BS_PUSHBUTTON|WS_TABSTOP,ID_IMPORT);
    state->connect=child(L"BUTTON",Text(L"GoogleCalendarConnectButton",L"Connect to Google..."),BS_PUSHBUTTON|WS_TABSTOP,ID_CONNECT);
    state->list=child(L"BUTTON",Text(L"GoogleCalendarListButton",L"Refresh calendar list"),BS_PUSHBUTTON|WS_TABSTOP,ID_LIST);
    state->calendars=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,ID_CALENDARS);
    SendMessageW(state->calendars,CB_SETDROPPEDWIDTH,640,0);
    state->save=child(L"BUTTON",Text(L"GoogleCalendarUseButton",L"Use selected calendar"),BS_PUSHBUTTON|WS_TABSTOP,ID_SAVE);
    state->disconnect=child(L"BUTTON",Text(L"GoogleCalendarDisconnectButton",L"Disconnect on this device"),BS_PUSHBUTTON|WS_TABSTOP,ID_DISCONNECT);
    state->cancelJob=child(L"BUTTON",Text(L"GoogleCalendarCancelButton",L"Cancel operation"),BS_PUSHBUTTON|WS_TABSTOP,ID_CANCEL_JOB);
    state->status=child(L"STATIC",Text(L"GoogleCalendarSetupInitial",L"No browser or account request starts until you choose Connect or Refresh calendar list."),SS_NOPREFIX,0);
    state->close=child(L"BUTTON",Text(L"GoogleCalendarCloseButton",L"Close"),BS_PUSHBUTTON|WS_TABSTOP,IDCANCEL);
    if(!state->instructions||!state->client||!state->import||!state->connect||!state->list||!state->calendars||!state->save||!state->disconnect||!state->cancelJob||!state->status||!state->close){DestroyWindow(state->window);return false;}
    if(!Reload(state))return false;Buttons(state,false);Layout(state);ShowWindow(state->window,SW_SHOW);SetForegroundWindow(state->window);SetFocus(state->import);guard.released=true;return true;
}catch(...){return false;}
static bool ImportGoogleCalendarClientForTile(HWND owner,const std::wstring& tileId,const std::wstring& path) try {
    if(!OpenGoogleCalendarSetup(owner,tileId))return false;const auto profile=aip::MakeAbsolutePath(g_iniPath);
    for(const auto& item:google_calendar_setup::windows)if(item.second->tileId==tileId&&IEquals(item.second->profile,profile))return google_calendar_setup::Import(item.second,path);
    return false;
}catch(...){return false;}
static bool IsGoogleCalendarSetupMessage(MSG& message){
    for(const auto& item:google_calendar_setup::windows)if(IsDialogMessageW(item.first,&message))return true;return false;
}
static void CloseGoogleCalendarSetups(){
    for(const auto& item:google_calendar_setup::windows)google_calendar_setup::Cancel(item.second);
    google_calendar_setup::workers.CancelAndJoin();
    while(!google_calendar_setup::windows.empty()){
        const auto window=google_calendar_setup::windows.begin()->first;
        if(!IsWindow(window)||!DestroyWindow(window))google_calendar_setup::windows.erase(window);
    }
}
#else
static bool OpenGoogleCalendarSetup(HWND,const std::wstring&){return false;}
static bool ImportGoogleCalendarClientForTile(HWND,const std::wstring&,const std::wstring&){return false;}
static bool IsGoogleCalendarSetupMessage(MSG&){return false;}
static void CloseGoogleCalendarSetups(){}
#endif
