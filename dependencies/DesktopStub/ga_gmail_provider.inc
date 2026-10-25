// Gmail owns one asynchronous source and one purpose-bound OAuth session per
// demanded Mail tile. No sign-in, body fetch or background work starts here.
#include "../content_sources/gmail.inc"
static aip::gmail::Configuration GmailConfigurationForTile(const aip::content::TileInstance& tile)
{
    auto cfg=tile.mail;std::wstring profile;
    {std::lock_guard<std::mutex> lock(g_pathMutex);profile=aip::MakeAbsolutePath(g_iniPath);}
    const auto user=CurrentTileRegistrationUserScope();
    cfg.account.ownerKey=user.empty()||profile.empty()?L"":aip::notifications::StableId(L"Gmail|"+user+L"|"+ToLowerCopy(profile)+L"|"+tile.id);
    cfg.account=aip::gmail::AccountConfiguration(cfg);return cfg;
}
struct ContentGmailView {
    bool ok=false,configured=false,pending=false,stale=false;
    int unreadMessages=0,unreadThreads=0;
    std::vector<aip::gmail::Message> messages;
    std::wstring status,mailboxAddress;
};
class ContentGmailProvider {
    using Result=std::shared_ptr<const aip::gmail::Snapshot>;
    aip::content::AsyncSource<Result> source;
    std::shared_ptr<aip::gmail::Session> session=std::make_shared<aip::gmail::Session>();
    std::wstring key,accountKey;Result lastGood;
public:
#ifdef DESKTOPSTUB_INERT_HARDWARE
    aip::gmail::Hooks testHooks;
#endif
    ~ContentGmailProvider(){Stop();}
    void Refresh(const aip::gmail::Configuration& cfg,bool synchronous,bool force=false)
    {
        std::wstring error;const auto account=aip::gmail::AccountConfiguration(cfg);
        if(!aip::google_calendar::RuntimeConfigValidate(account,error,true)){Cancel();return;}
        const auto requested=aip::gmail::ConfigurationKey(cfg),requestedAccount=aip::google_calendar::ConfigurationKey(account);
        if(key!=requested){source.CancelPending();lastGood.reset();key=requested;}
        if(accountKey!=requestedAccount){session->Clear();accountKey=requestedAccount;}
        aip::gmail::Hooks hooks;
#ifdef DESKTOPSTUB_INERT_HARDWARE
        hooks=testHooks;hooks.inert=true;
#endif
        const auto sharedSession=session;
        source.Request(requested,std::chrono::milliseconds(static_cast<long long>(account.refreshSeconds)*1000),
            [cfg,hooks,sharedSession](const aip::content::AsyncSource<Result>::Cancel& canceled)->Result{
                auto result=std::make_shared<aip::gmail::Snapshot>();std::wstring error;
                aip::gmail::FetchInbox(cfg,*result,error,[&]{return canceled()||!g_running.load();},hooks,sharedSession.get());
                return result;
            },WakePollThread,force);
        if(synchronous&&!source.Wait(requested,std::chrono::seconds(45)))source.CancelPending();
    }
    ContentGmailView Read(const aip::gmail::Configuration& cfg,bool* completed=nullptr)
    {
        if(completed)*completed=false;ContentGmailView view;std::wstring error;
        const auto account=aip::gmail::AccountConfiguration(cfg);
        view.configured=aip::google_calendar::RuntimeConfigValidate(account,error,true);
        if(!view.configured){view.status=account.clientId.empty()?L"Import a Google Desktop OAuth client in this Mail tile's source settings.":L"Connect this Mail tile to Gmail in source settings.";return view;}
        const auto requested=aip::gmail::ConfigurationKey(cfg);Result result;std::string privateError;
        if(requested!=key||!source.Read(requested,result,privateError)||!result){view.pending=privateError.empty();view.status=view.pending?L"Fetching Gmail inbox...":L"Gmail refresh could not complete.";return view;}
        if(completed)*completed=true;
        if(result->ok)lastGood=result;
        else if(lastGood){view.stale=true;view.status=result->error;result=lastGood;}
        else{view.status=result->error.empty()?L"Gmail refresh failed. Check the account and API access in source settings.":result->error;return view;}
        view.ok=true;view.unreadMessages=result->unreadMessages;view.unreadThreads=result->unreadThreads;
        view.messages=result->messages;view.mailboxAddress=result->mailboxAddress;return view;
    }
    void Cancel(){source.CancelPending();session->Clear();key.clear();accountKey.clear();lastGood.reset();}
    void Stop(){source.CancelPending();source.Stop();session->Clear();key.clear();accountKey.clear();lastGood.reset();}
};
