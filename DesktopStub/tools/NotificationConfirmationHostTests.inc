// Exercise real request/queue/UI-drain routing with a native-toast sink.
static std::vector<std::wstring> confirmationHostTokens;
static bool CaptureConfirmationHostToast(const std::wstring&,const std::wstring&,const std::wstring&,const std::wstring& token)
{confirmationHostTokens.push_back(token);return true;}
static void TestNotificationConfirmationHost()
{
    const auto oldRunning=g_running.load(),oldEnabled=g_notificationsEnabled.load();
    const auto oldRender=g_commandLine.renderOnly,oldConfigure=g_commandLine.configureOnly;
    g_running=true;g_notificationsEnabled=true;g_commandLine.renderOnly=false;g_commandLine.configureOnly=false;
    const auto oldPreference=IniReadS(L"Settings",L"NotificationConfirmations",L"1");
    Check(IniWrite(L"Settings",L"NotificationConfirmations",L"1"),"enable private confirmation fixture");
    ClearUserConfirmations();confirmationHostTokens.clear();g_testNotificationConfirmationToast=CaptureConfirmationHostToast;
    unsigned approved=0;
    Check(ConfirmUserOperation(nullptr,L"first",L"Title",L"Question",[&]{++approved;})&&approved==0,"notification request returns without inline approval");
    auto token=confirmationHostTokens.back();
    QueueUserConfirmationAction(L"review");DrainUserConfirmations();
    Check(approved==0,"body click never means Yes");
    std::thread reply([token]{QueueUserConfirmationAction(L"confirm="+token+L"&choice=yes");});reply.join();
    Check(approved==0,"WinRT callback thread only queues the response");
    DrainUserConfirmations();
    Check(approved==1,"posted UI drain invokes exactly the approved callback");
    QueueUserConfirmationAction(L"confirm="+token+L"&choice=yes");DrainUserConfirmations();
    Check(approved==1,"replayed Yes cannot run the operation twice");
    Check(ConfirmUserOperation(nullptr,L"no",L"Title",L"Question",[&]{++approved;}),"second question queued");
    token=confirmationHostTokens.back();QueueUserConfirmationAction(L"confirm="+token+L"&choice=no");DrainUserConfirmations();
    Check(approved==1&&g_confirmations.Entries().empty(),"No consumes the question and keeps settings untouched");
    Check(ConfirmUserOperation(nullptr,L"replace",L"Title",L"Old",[&]{approved+=100;}),"old question queued");
    auto oldToken=confirmationHostTokens.back();
    Check(ConfirmUserOperation(nullptr,L"replace",L"Title",L"New",[&]{approved+=10;}),"new question supersedes old question");
    token=confirmationHostTokens.back();QueueUserConfirmationAction(L"confirm="+oldToken+L"&choice=yes");DrainUserConfirmations();
    Check(approved==1,"superseded question cannot run an old continuation");
    QueueUserConfirmationAction(L"confirm="+token+L"&choice=yes");DrainUserConfirmations();Check(approved==11,"latest continuation runs");
    const auto profile=UserConfirmationProfile();
    token=L"0123456789abcdef0123456789abcdef";
    Check(g_confirmations.Add(L"expired",token,profile,0,1),"insert deterministic expired request");
    g_confirmationCallbacks[token]=[&]{approved+=1000;};
    QueueUserConfirmationAction(L"confirm="+token+L"&choice=yes");DrainUserConfirmations();
    Check(approved==11&&g_confirmationCallbacks.empty(),"expired callback is removed without execution");
    Check(ConfirmUserOperation(nullptr,L"shutdown",L"Title",L"Question",[&]{++approved;}),"shutdown fixture queued");
    token=confirmationHostTokens.back();ClearUserConfirmations();
    QueueUserConfirmationAction(L"confirm="+token+L"&choice=yes");DrainUserConfirmations();
    Check(approved==11&&g_confirmationCallbacks.empty(),"session cleanup invalidates all action tokens");
    Check(ConfirmUserOperation(nullptr,L"scoped",L"Title",L"Question",[&]{++approved;}),"profile-scoped foreground confirmation fixture queued");
    token=confirmationHostTokens.back();const auto scopedAction=L"confirm="+token+L"&choice=yes";
    QueueUserConfirmationAction(aip::notifications::ConfirmationProfilePrefix(L"D:\\wrong-profile.ini")+scopedAction);DrainUserConfirmations();
    Check(approved==11&&!g_confirmations.Entries().empty(),"another profile's callback cannot resolve a current confirmation");
    QueueUserConfirmationAction(aip::notifications::ConfirmationProfilePrefix(UserConfirmationProfile())+scopedAction);DrainUserConfirmations();
    Check(approved==12,"full foreground launch arguments resolve only their originating profile's live request");
    Check(ConfirmUserOperation(nullptr,L"copy-data",L"Title",L"Question",[&]{++approved;}),"cold launch delivery fixture queued");
    token=confirmationHostTokens.back();auto forwarded=L"confirm="+token+L"&choice=no";
    COPYDATASTRUCT data{NOTIFICATION_CONFIRMATION_COPY_DATA,static_cast<DWORD>((forwarded.size()+1)*sizeof(wchar_t)),&forwarded[0]};
    Check(WndProc(nullptr,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&data))==TRUE&&approved==12,
        "foreground launch IPC queues the choice without synchronous approval work");
    DrainUserConfirmations();Check(approved==12&&g_confirmations.Entries().empty(),"forwarded No dismisses the live question without changing settings");
    forwarded=L"confirm=malformed&choice=yes";data.cbData=static_cast<DWORD>((forwarded.size()+1)*sizeof(wchar_t));data.lpData=&forwarded[0];
    Check(WndProc(nullptr,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&data))==FALSE,"malformed foreground IPC is rejected before queueing");
    g_commandLine.renderOnly=true;
    Check(!ConfirmUserOperation(nullptr,L"offline",L"Title",L"Question",[&]{++approved;}),"render-only cannot ask for a confirmation or act on it");
    g_commandLine.renderOnly=oldRender;g_commandLine.configureOnly=oldConfigure;
    g_testNotificationConfirmationToast=nullptr;ClearUserConfirmations();
    Check(IniWrite(L"Settings",L"NotificationConfirmations",oldPreference.c_str()),"restore private confirmation preference");
    g_running=oldRunning;g_notificationsEnabled=oldEnabled;
}
