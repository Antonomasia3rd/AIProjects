// A per-tile read-only WuWa worker. The owning tile drops this provider when
// unpinned or changed to another type; no global account poller is created.
#include "../content_sources/kuro_notes.inc"
struct ContentKuroProvider {
    aip::content::AsyncSource<aip::kuro::Snapshot> source;
    std::wstring key, immediateError;
    bool waitExpired = false;
    std::shared_ptr<std::atomic<std::uint64_t>> sequence=std::make_shared<std::atomic<std::uint64_t>>(0);
    std::optional<aip::kuro::Snapshot> lastGood;
    std::uint64_t observedSequence=0,timeoutSequence=0;
    unsigned consecutiveFailures=0;
    aip::kuro::FailureKind lastFailure=aip::kuro::FailureKind::None;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    aip::kuro::HttpTransport testTransport;
    aip::kuro::CredentialDecoder testDecoder;
#endif
    static std::wstring Key(const aip::kuro::Configuration& cfg) { return aip::kuro::ConfigurationKey(cfg); }
    void Refresh(const aip::kuro::Configuration& cfg, bool synchronous, bool force = false) {
        waitExpired = false;
        if (cfg.gameToken.empty() && cfg.gameTokenProtected.empty()) {
            Cancel();
            immediateError = L"Sign in to Kuro and choose your character in this tile's source settings.";
            return;
        }
        immediateError.clear();
        const auto requested = Key(cfg);
        if (requested != key) { source.CancelPending(); key = requested; lastGood.reset();observedSequence=0;consecutiveFailures=0;lastFailure=aip::kuro::FailureKind::None; }
        aip::kuro::HttpTransport transport;
        aip::kuro::CredentialDecoder decoder;
#ifdef DESKTOPSTUB_INERT_HARDWARE
        transport = testTransport ? testTransport : [](const auto&, auto&, std::wstring& error) { error = L"Kuro has no network transport in inert validation."; return false; };
        decoder = testDecoder;
#else
        transport = aip::FetchHttpBytes;
        decoder = aip::kuro::detail::DecodeNativeCredential;
#endif
        source.Request(requested, std::chrono::milliseconds(static_cast<long long>((std::max)(30, cfg.refreshSeconds)) * 1000),
            [cfg, transport, decoder,counter=sequence](const aip::content::AsyncSource<aip::kuro::Snapshot>::Cancel& canceled) {
                aip::kuro::Snapshot result;
                try{result=aip::kuro::Fetch(cfg, transport, decoder, [canceled] { return !g_running.load() || canceled(); });}
                catch(...){result=aip::kuro::MakeError(L"Kuro refresh failed",L"The Wuthering Waves source could not complete. The saved login was kept; DesktopStub will retry.",aip::kuro::FailureKind::Transient);}
                result.refreshSequence=++*counter;return result;
            }, WakePollThread, force);
        if (synchronous && !source.Wait(requested, std::chrono::milliseconds(static_cast<long long>((std::max)(1, cfg.timeoutSeconds)) * 1000 + 1000))) {
            waitExpired = true;timeoutSequence=++*sequence; source.CancelPending();
        }
    }
    aip::kuro::Snapshot Read(const aip::kuro::Configuration& cfg, bool* completed = nullptr) {
        if (completed) *completed = false;
        if (!immediateError.empty()) { if (completed) *completed = true; return aip::kuro::MakeError(L"Wuthering Waves setup", immediateError,aip::kuro::FailureKind::Setup); }
        if (waitExpired) { if (completed) *completed = true;auto result=aip::kuro::MakeError(L"Kuro refresh timed out", L"No result arrived before the one-shot deadline. The saved login was kept.",aip::kuro::FailureKind::Transient);result.refreshSequence=timeoutSequence;return Present(std::move(result)); }
        aip::kuro::Snapshot result; std::string error;
        const bool ready = source.Read(Key(cfg), result, error);
        if (completed) *completed = ready || !error.empty();
        // AsyncSource exception strings can contain transport/account details.
        // Keep those private just like native request failures.
        if (!ready && !error.empty()) return Present(aip::kuro::MakeError(L"Kuro refresh failed", L"The Wuthering Waves source could not complete."));
        return ready?Present(std::move(result)):result;
    }
    aip::kuro::Snapshot Present(aip::kuro::Snapshot result) {
        using namespace aip::kuro;
        if(result.refreshSequence!=observedSequence||result.refreshSequence==0){
            observedSequence=result.refreshSequence;lastFailure=result.failure;
            if(result.state==State::Ok||result.state==State::Full){lastGood=result;consecutiveFailures=0;lastFailure=FailureKind::None;}
            else if(result.state==State::Error&&consecutiveFailures<UINT_MAX)++consecutiveFailures;
        }
        if(result.state==State::Error&&lastGood&&result.failure!=FailureKind::Setup){
            auto cached=*lastGood;cached.stale=true;cached.failure=result.failure;cached.refreshSequence=result.refreshSequence;
            cached.diagnostic=L"Showing the last successful Wuthering Waves snapshot. "+result.diagnostic;
            return cached;
        }
        return result;
    }
    bool ShouldNotifyFailure() const {return lastFailure!=aip::kuro::FailureKind::None&&
        (lastFailure!=aip::kuro::FailureKind::Transient||consecutiveFailures>=2);}
    void Cancel() { source.CancelPending(); key.clear(); immediateError.clear(); waitExpired = false;lastGood.reset();observedSequence=0;consecutiveFailures=0;lastFailure=aip::kuro::FailureKind::None; }
    void Stop() { Cancel(); source.Stop(); }
};
