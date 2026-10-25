// One demanded Battery tile owns a bounded asynchronous sampler. Configuration
// and rendering paths never query power policy or battery device drivers.
#include "../content_sources/battery_win32.inc"
class ContentBatteryProvider {
    using Result=std::shared_ptr<const aip::battery::native::Collection>;
    aip::content::AsyncSource<Result> source;
    std::wstring key;
    static std::wstring Key(const aip::battery::Configuration& cfg){
        return std::to_wstring(cfg.refreshSeconds)+L":"+std::to_wstring(cfg.deviceDetails)+L":"+
            std::to_wstring(cfg.collectionTimeoutMs)+L":"+std::to_wstring(cfg.deviceTimeoutMs);
    }
public:
#ifdef DESKTOPSTUB_INERT_HARDWARE
    std::function<aip::battery::native::Collection(const aip::battery::native::CollectOptions&)> testCollect;
#endif
    ~ContentBatteryProvider(){Stop();}
    void Refresh(const aip::battery::Configuration& cfg,bool synchronous,bool force=false){
        const auto requested=Key(cfg);if(key!=requested){source.CancelPending();key=requested;}
        aip::battery::native::CollectOptions options;options.includeDevices=cfg.deviceDetails;
        options.timeoutMilliseconds=static_cast<std::uint32_t>(cfg.collectionTimeoutMs);
        options.ioTimeoutMilliseconds=static_cast<std::uint32_t>(cfg.deviceTimeoutMs);
#ifdef DESKTOPSTUB_INERT_HARDWARE
        const auto injected=testCollect;
#endif
        source.Request(requested,std::chrono::milliseconds(static_cast<long long>(cfg.refreshSeconds)*1000),
            [options
#ifdef DESKTOPSTUB_INERT_HARDWARE
            ,injected
#endif
            ](const aip::content::AsyncSource<Result>::Cancel& canceled)mutable->Result{
                options.canceled=[&]{return canceled()||!g_running.load();};
#ifdef DESKTOPSTUB_INERT_HARDWARE
                if(injected)return std::make_shared<aip::battery::native::Collection>(injected(options));
#endif
                return std::make_shared<aip::battery::native::Collection>(aip::battery::native::Collect(options));
            },WakePollThread,force);
        if(synchronous&&!source.Wait(requested,std::chrono::milliseconds(cfg.collectionTimeoutMs+1000)))source.CancelPending();
    }
    aip::battery::View Read(const aip::battery::Configuration& cfg,bool& completed,std::wstring& diagnostic){
        completed=false;diagnostic.clear();Result result;std::string failure;
        if(key!=Key(cfg)||!source.Read(key,result,failure)||!result){
            completed=!failure.empty();
            diagnostic=failure.empty()?L"Reading battery information...":L"Battery sampling could not complete.";
            return aip::battery::BuildView(cfg,{});
        }
        completed=!result->canceled;diagnostic=result->raw.diagnostic;
        return aip::battery::BuildView(cfg,result->raw);
    }
    void Cancel(){source.CancelPending();key.clear();}
    void Stop(){source.CancelPending();source.Stop();key.clear();}
};
