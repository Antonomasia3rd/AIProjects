// Interactive confirmations are deliberately scoped to this resident session.
// A toast can never carry executable settings or approve an operation after exit.
#include "../notification_confirmation.h"
#include <functional>
#include <deque>
static constexpr UINT WM_APP_NOTIFICATION_CONFIRMATION=WM_APP+131;
static std::mutex g_confirmationMutex;
static aip::notifications::ConfirmationRegistry g_confirmations;
static std::map<std::wstring,std::function<void()>> g_confirmationCallbacks;
static std::deque<std::wstring> g_confirmationActions;
#ifdef DESKTOPSTUB_INERT_HARDWARE
static bool (*g_testNotificationConfirmationToast)(const std::wstring&,const std::wstring&,const std::wstring&,const std::wstring&)=nullptr;
#endif

static std::wstring UserConfirmationProfile()
{
    std::lock_guard<std::mutex> lock(g_pathMutex);
    auto profile=g_iniPath;
    std::transform(profile.begin(),profile.end(),profile.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
    return profile;
}
static void QueueUserConfirmationAction(const std::wstring& arguments)
{
    aip::notifications::ConfirmationAction parsed;
    if(!aip::notifications::ParseConfirmationAction(arguments,parsed))return;
    {
        std::lock_guard<std::mutex> lock(g_confirmationMutex);
        if(g_confirmationActions.size()>=64)return;
        g_confirmationActions.push_back(arguments);
    }
#ifndef DESKTOPSTUB_TEST_NOTIFICATION_TOAST
    if(g_hwnd)PostMessageW(g_hwnd,WM_APP_NOTIFICATION_CONFIRMATION,0,0);
#endif
}

#if !defined(DESKTOPSTUB_INERT_HARDWARE) && !defined(DESKTOPSTUB_TEST_NOTIFICATION_TOAST)
struct NativeUserConfirmation
{
    winrt::Windows::UI::Notifications::ToastNotification toast{nullptr};
    winrt::Windows::UI::Notifications::ToastNotifier notifier{nullptr};
    winrt::event_token activated{},dismissed{},failed{};
};
static std::map<std::wstring,NativeUserConfirmation> g_nativeConfirmations;
static void RetireNativeUserConfirmations(std::vector<NativeUserConfirmation> records)
{
    const HRESULT initialized=RoInitialize(RO_INIT_MULTITHREADED);
    for(auto& record:records){
        try{record.toast.Activated(record.activated);record.toast.Dismissed(record.dismissed);record.toast.Failed(record.failed);record.notifier.Hide(record.toast);}catch(...){}
        record.toast=nullptr;record.notifier=nullptr;
    }
    records.clear();
    if(SUCCEEDED(initialized))RoUninitialize();
}
static void PruneNativeUserConfirmations(const std::vector<aip::notifications::PendingConfirmation>& pending)
{
    std::vector<NativeUserConfirmation> removed;
    {
        std::lock_guard<std::mutex> lock(g_confirmationMutex);
        for(auto it=g_nativeConfirmations.begin();it!=g_nativeConfirmations.end();){
            const bool keep=std::any_of(pending.begin(),pending.end(),[&](const auto& row){return row.token==it->first;});
            if(keep){++it;continue;}
            removed.push_back(std::move(it->second));it=g_nativeConfirmations.erase(it);
        }
    }
    RetireNativeUserConfirmations(std::move(removed));
}
static bool ShowUserConfirmationToast(const std::wstring& key,const std::wstring& title,const std::wstring& message,const std::wstring& token)
{
    const HRESULT initialized=RoInitialize(RO_INIT_MULTITHREADED);
    struct Cleanup{HRESULT result;~Cleanup(){if(SUCCEEDED(result))RoUninitialize();}} cleanup{initialized};
    if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return false;
    NativeUserConfirmation record;
    try{
        using namespace winrt::Windows::UI::Notifications;
        UINT32 size=0;
        if(GetCurrentApplicationUserModelId(&size,nullptr)!=ERROR_INSUFFICIENT_BUFFER||size<2||size>1024)return false;
        std::vector<wchar_t> id(size);
        if(GetCurrentApplicationUserModelId(&size,id.data())!=ERROR_SUCCESS)return false;
        const auto manager=aip::AcquireUncachedActivationFactory<ToastNotificationManager,IToastNotificationManagerStatics>();
        record.notifier=manager.CreateToastNotifier(id.data());
        if(record.notifier.Setting()!=NotificationSetting::Enabled)return false;
        const auto factory=aip::AcquireUncachedActivationFactory<winrt::Windows::Data::Xml::Dom::XmlDocument,winrt::Windows::Foundation::IActivationFactory>();
        auto xml=factory.ActivateInstance<winrt::Windows::Data::Xml::Dom::XmlDocument>();
        xml.LoadXml(aip::notifications::XMLConfirmationXml(title,message,token,
            IniReadS(L"Strings",L"NotificationConfirmationYes",L"Yes"),IniReadS(L"Strings",L"NotificationConfirmationNo",L"No")));
        const auto toastFactory=aip::AcquireUncachedActivationFactory<ToastNotification,IToastNotificationFactory>();
        record.toast=toastFactory.CreateToastNotification(xml);
        record.toast.Tag(aip::notifications::StableId(L"confirmation-"+key));record.toast.Group(NotificationGroup());
        const auto expires=winrt::clock::now()+std::chrono::minutes(5);
        record.toast.ExpirationTime(winrt::box_value(expires).as<winrt::Windows::Foundation::IReference<winrt::Windows::Foundation::DateTime>>());
        record.activated=record.toast.Activated([](const auto&,const winrt::Windows::Foundation::IInspectable& args){
            try{auto action=args.as<ToastActivatedEventArgs>();QueueUserConfirmationAction(std::wstring(action.Arguments()));}catch(...){}
        });
        record.dismissed=record.toast.Dismissed([token](const auto&,const ToastDismissedEventArgs& args){
            // TimedOut only hides the popup. Its Action Center buttons remain
            // usable until our own deadline, so it must not cancel consent.
            if(args.Reason()!=ToastDismissalReason::TimedOut)QueueUserConfirmationAction(L"confirm="+token+L"&choice=no");
        });
        record.failed=record.toast.Failed([token](const auto&,const auto&){QueueUserConfirmationAction(L"confirm="+token+L"&choice=no");});
        {
            std::lock_guard<std::mutex> lock(g_confirmationMutex);
            g_nativeConfirmations.emplace(token,record);
        }
        record.notifier.Show(record.toast);
        return true;
    }catch(...){
        // Retire all handlers before the apartment is released, including a
        // partially constructed toast. A failed display is never approval.
        {std::lock_guard<std::mutex> lock(g_confirmationMutex);g_nativeConfirmations.erase(token);}
        if(record.toast){try{record.toast.Activated(record.activated);record.toast.Dismissed(record.dismissed);record.toast.Failed(record.failed);}catch(...){}}
        record.toast=nullptr;record.notifier=nullptr;
        return false;
    }
}
#else
static void PruneNativeUserConfirmations(const std::vector<aip::notifications::PendingConfirmation>&){}
static bool ShowUserConfirmationToast(const std::wstring& key,const std::wstring& title,const std::wstring& message,const std::wstring& token)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(g_testNotificationConfirmationToast)return g_testNotificationConfirmationToast(key,title,message,token);
#endif
    (void)key;(void)title;(void)message;(void)token;return false;
}
#endif

static void PruneUserConfirmations()
{
    std::vector<aip::notifications::PendingConfirmation> pending;
    {
        std::lock_guard<std::mutex> lock(g_confirmationMutex);
        g_confirmations.Expire(GetTickCount64());pending=g_confirmations.Entries();
        for(auto it=g_confirmationCallbacks.begin();it!=g_confirmationCallbacks.end();){
            const bool keep=std::any_of(pending.begin(),pending.end(),[&](const auto& row){return row.token==it->first;});
            if(keep)++it;else it=g_confirmationCallbacks.erase(it);
        }
    }
    PruneNativeUserConfirmations(pending);
}
static void ClearUserConfirmations()
{
    {std::lock_guard<std::mutex> lock(g_confirmationMutex);g_confirmations.Clear();g_confirmationCallbacks.clear();g_confirmationActions.clear();}
    PruneNativeUserConfirmations({});
}
static void DrainUserConfirmations()
{
    const auto profile=UserConfirmationProfile();
    for(;;){
        std::function<void()> approved;
        {
            std::lock_guard<std::mutex> lock(g_confirmationMutex);
            if(g_confirmationActions.empty())break;
            const auto arguments=std::move(g_confirmationActions.front());g_confirmationActions.pop_front();
            aip::notifications::ConfirmationAction action;
            if(!aip::notifications::ParseConfirmationAction(arguments,action))continue;
            auto resolved=g_confirmations.Resolve(action.token,action.choice,profile,GetTickCount64());
            if(resolved){
                auto callback=g_confirmationCallbacks.find(resolved->token);
                if(callback!=g_confirmationCallbacks.end()){
                    if(resolved->approved)approved=std::move(callback->second);
                    g_confirmationCallbacks.erase(callback);
                }
            }
        }
        if(approved&&g_running.load())approved();
    }
    PruneUserConfirmations();
}
static bool ConfirmUserOperation(HWND owner,const std::wstring& key,const std::wstring& title,const std::wstring& message,std::function<void()> accepted)
{
    if(!accepted||g_commandLine.renderOnly||g_commandLine.configureOnly||!g_running.load())return false;
    PruneUserConfirmations();
    bool toastAllowed=NotificationAllowed(true)&&IniReadI(L"Settings",L"NotificationConfirmations",1)!=0&&ConfiguredNotificationApi()==aip::notifications::Api::Toast;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(g_testNotificationConfirmationToast)toastAllowed=IniReadI(L"Settings",L"NotificationConfirmations",1)!=0&&NotificationAllowed(true);
#endif
    if(toastAllowed){
        GUID guid{};wchar_t text[40]{};std::wstring token;
        if(SUCCEEDED(CoCreateGuid(&guid))&&StringFromGUID2(guid,text,40))for(wchar_t c:std::wstring(text))if(iswxdigit(c))token+=static_cast<wchar_t>(towlower(c));
        const auto profile=UserConfirmationProfile();
        bool added=false;
        {std::lock_guard<std::mutex> lock(g_confirmationMutex);added=g_confirmations.Add(key,token,profile,GetTickCount64());if(added)g_confirmationCallbacks[token]=accepted;}
        PruneUserConfirmations();
        if(added&&ShowUserConfirmationToast(key,title,message,token))return true;
        {std::lock_guard<std::mutex> lock(g_confirmationMutex);g_confirmations.CancelToken(token);g_confirmationCallbacks.erase(token);}
        PruneUserConfirmations();
    }
    // Classic balloons have no Yes/No buttons. An explicit menu action still
    // needs a usable confirmation when notifications are disabled/unavailable.
#ifdef DESKTOPSTUB_CONTENT_MESSAGEBOX
    const auto answer=DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,message.c_str(),title.c_str(),MB_YESNO|MB_DEFBUTTON2|MB_ICONINFORMATION);
#else
    const auto answer=MessageBoxW(owner,message.c_str(),title.c_str(),MB_YESNO|MB_DEFBUTTON2|MB_ICONINFORMATION);
#endif
    if(answer!=IDYES)return false;
    accepted();return true;
}
