// Per-tile WuWa source, account-bound last-good cache and reusable health policy.
#include "../content_sources/kuro_notes.inc"
#include "../source_health.h"
#include <mutex>
struct ContentKuroProvider {
    using HealthConfiguration=aip::source_health::Configuration;
    struct HealthControl {std::mutex mutex;aip::source_health::Tracker tracker;std::optional<aip::kuro::Snapshot> lastGood,lastFailure;};
    aip::content::AsyncSource<aip::kuro::Snapshot> source;
    std::wstring key,accountKey,immediateError;bool waitExpired=false;
    std::shared_ptr<std::atomic<std::uint64_t>> sequence=std::make_shared<std::atomic<std::uint64_t>>(0);
    std::shared_ptr<HealthControl> health=std::make_shared<HealthControl>();
#ifdef DESKTOPSTUB_INERT_HARDWARE
    aip::kuro::HttpTransport testTransport;aip::kuro::CredentialDecoder testDecoder;
    std::function<std::uint64_t()> testNow;
#endif
    static std::uint64_t Clock(){const auto value=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();return value<0?0:static_cast<std::uint64_t>(value);}
    std::uint64_t Now()const{
#ifdef DESKTOPSTUB_INERT_HARDWARE
        if(testNow)return testNow();
#endif
        return Clock();
    }
    static aip::source_health::Outcome Outcome(const aip::kuro::Snapshot& result){
        using namespace aip::kuro;using H=aip::source_health::Outcome;
        if(result.state==State::Ok||result.state==State::Full)return H::Success;
        if(result.state!=State::Error)return H::None;
        switch(result.failure){case FailureKind::Transient:return H::Transient;case FailureKind::Rejected:return H::Rejected;case FailureKind::Setup:return H::Setup;default:return H::InvalidData;}
    }
    static std::wstring Key(const aip::kuro::Configuration& cfg){return aip::kuro::ConfigurationKey(cfg);}
    static std::wstring AccountKey(const aip::kuro::Configuration& cfg){return cfg.gameToken+L"\n"+cfg.gameTokenProtected+L"\n"+cfg.playerId+L"\n"+cfg.region+L"\n"+cfg.deviceId;}
    void Refresh(const aip::kuro::Configuration& cfg,bool synchronous,bool force=false){
        waitExpired=false;
        if(cfg.gameToken.empty()&&cfg.gameTokenProtected.empty()){Cancel();immediateError=L"Sign in to Kuro and choose your character in this tile's source settings.";return;}
        immediateError.clear();const auto requested=Key(cfg);
        if(requested!=key){source.CancelPending();key=requested;const auto account=AccountKey(cfg);
            if(account!=accountKey){accountKey=account;health=std::make_shared<HealthControl>();}}
        aip::kuro::HttpTransport transport;aip::kuro::CredentialDecoder decoder;std::function<std::uint64_t()> clock=Clock;
#ifdef DESKTOPSTUB_INERT_HARDWARE
        transport=testTransport?testTransport:[](const auto&,auto&,std::wstring& error){error=L"Kuro has no network transport in inert validation.";return false;};decoder=testDecoder;if(testNow)clock=testNow;
#else
        transport=aip::FetchHttpBytes;decoder=aip::kuro::detail::DecodeNativeCredential;
#endif
        source.Request(requested,std::chrono::milliseconds(static_cast<long long>((std::max)(30,cfg.refreshSeconds))*1000),
            [cfg,transport,decoder,clock,counter=sequence,control=health](const aip::content::AsyncSource<aip::kuro::Snapshot>::Cancel& canceled){
                aip::kuro::Snapshot result;
                try{result=aip::kuro::Fetch(cfg,transport,decoder,[canceled]{return !g_running.load()||canceled();});}
                catch(...){result=aip::kuro::MakeError(L"Kuro refresh failed",L"The Wuthering Waves source could not complete. The saved login was kept; DesktopStub will retry.",aip::kuro::FailureKind::Transient);}
                result.refreshSequence=++*counter;
                // Count actual completion here, not repeated cached/UI reads.
                if(!canceled()&&g_running.load()){
                    const auto outcome=Outcome(result);const auto completedAt=clock();std::lock_guard<std::mutex> lock(control->mutex);
                    control->tracker.Observe(result.refreshSequence,outcome,completedAt);
                    if(outcome==aip::source_health::Outcome::Success){control->lastGood=result;control->lastFailure.reset();}
                    else if(outcome!=aip::source_health::Outcome::None)control->lastFailure=result;
                }return result;
            },WakePollThread,force);
        if(synchronous&&!source.Wait(requested,std::chrono::milliseconds(static_cast<long long>((std::max)(1,cfg.timeoutSeconds))*1000+1000))){waitExpired=true;source.CancelPending();}
    }
    aip::source_health::State HealthState()const{std::lock_guard<std::mutex> lock(health->mutex);return health->tracker.Read();}
    bool ShouldNotifyFailure(const HealthConfiguration& cfg={})const{return aip::source_health::PopupFailure(cfg,HealthState(),Now());}
    bool PopupFailureReached(const HealthConfiguration& cfg)const{return aip::source_health::PopupFailureReached(cfg,HealthState(),Now());}
    std::wstring NoticeText(const std::wstring& pattern,const HealthConfiguration& cfg,const std::wstring& error,const std::wstring& tile)const{
        return aip::source_health::Format(pattern,cfg,HealthState(),Now(),error,tile);
    }
    aip::kuro::Snapshot Present(aip::kuro::Snapshot result,const HealthConfiguration& cfg,const std::wstring& tileName=L""){
        using namespace aip::kuro;aip::source_health::State state;std::optional<Snapshot> cached;
        {std::lock_guard<std::mutex> lock(health->mutex);state=health->tracker.Read();cached=health->lastGood;}
        const auto now=Now();const bool failed=result.state==State::Error;
        const auto diagnostic=result.diagnostic;const auto failure=result.failure;const auto attempt=result.refreshSequence;
        if(failed&&cached&&cfg.retainLastGood&&failure!=FailureKind::Setup){
            result=*cached;result.stale=true;result.failure=failure;result.refreshSequence=attempt;
            result.diagnostic=L"Showing the last successful Wuthering Waves snapshot. "+diagnostic;
        }
        const bool tileFailure=failed&&aip::source_health::TileFailure(cfg,state,now);
        if(failed&&(!cached||!cfg.retainLastGood||failure==FailureKind::Setup)&&!tileFailure){
            result.state=State::Loading;result.line1=L"Wuthering Waves";result.line2=aip::source_health::Format(cfg.noDataText,cfg,state,now,diagnostic,tileName);result.rows.clear();
        }
        auto ageState=state;if(failed)ageState.failing=true;
        std::wstring status=aip::source_health::AgeText(cfg,ageState,now,tileName);
        if(tileFailure){const auto message=aip::source_health::Format(cfg.tileFailureText,cfg,state,now,diagnostic,tileName);
            if(!status.empty()&&!message.empty())status+=L"\n";status+=message;
            if(cfg.tileFailureMode==L"Replace"){result.state=State::Error;result.line1=L"Wuthering Waves";result.line2=status;result.rows.clear();}
        }
        // Subtle status row shares the actual game layout and contents dialog.
        // The retained resource fields remain unchanged below it.
        if(!status.empty()){result.rows.insert(result.rows.begin(),{L"",status,L""});if(result.state==State::Loading||result.state==State::Error)result.line2=status;}
        return result;
    }
    aip::kuro::Snapshot Read(const aip::kuro::Configuration& cfg,const HealthConfiguration& healthCfg,bool* completed=nullptr,const std::wstring& tileName=L""){
        if(completed)*completed=false;
        if(!immediateError.empty()){if(completed)*completed=true;return aip::kuro::MakeError(L"Wuthering Waves setup",immediateError,aip::kuro::FailureKind::Setup);}
        if(Key(cfg)!=key)return {};
        if(waitExpired){if(completed)*completed=true;return Present(aip::kuro::MakeError(L"Kuro refresh timed out",L"No result arrived before the one-shot deadline. The saved login was kept.",aip::kuro::FailureKind::Transient),healthCfg,tileName);}
        aip::kuro::Snapshot result;std::string error;const bool ready=source.Read(Key(cfg),result,error);
        if(completed)*completed=ready||!error.empty();
        if(!ready&&!error.empty())return aip::kuro::MakeError(L"Kuro refresh failed",L"The Wuthering Waves source worker could not complete.",aip::kuro::FailureKind::Setup);
        if(ready)return Present(std::move(result),healthCfg,tileName);
        // A cadence/timeout change supersedes its request without discarding
        // useful data from this exact unchanged account while the new fetch runs.
        if(healthCfg.retainLastGood){std::lock_guard<std::mutex> lock(health->mutex);if(health->lastFailure)result=*health->lastFailure;else if(health->lastGood)result=*health->lastGood;}
        return result.state==aip::kuro::State::Loading?result:Present(std::move(result),healthCfg,tileName);
    }
    aip::kuro::Snapshot Read(const aip::kuro::Configuration& cfg,bool* completed=nullptr){return Read(cfg,HealthConfiguration{},completed);}
    void Cancel(){source.CancelPending();key.clear();accountKey.clear();immediateError.clear();waitExpired=false;health=std::make_shared<HealthControl>();}
    void Stop(){Cancel();source.Stop();}
};
