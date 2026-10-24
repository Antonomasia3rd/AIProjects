// A per-tile read-only WuWa worker. The owning tile drops this provider when
// unpinned or changed to another type; no global account poller is created.
#include "../content_sources/kuro_notes.inc"
struct ContentKuroProvider {
    aip::content::AsyncSource<aip::kuro::Snapshot> source;
    std::wstring key, immediateError;
    bool waitExpired = false;
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
        if (requested != key) { source.CancelPending(); key = requested; }
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
            [cfg, transport, decoder](const aip::content::AsyncSource<aip::kuro::Snapshot>::Cancel& canceled) {
                return aip::kuro::Fetch(cfg, transport, decoder, [canceled] { return !g_running.load() || canceled(); });
            }, WakePollThread, force);
        if (synchronous && !source.Wait(requested, std::chrono::milliseconds(static_cast<long long>((std::max)(1, cfg.timeoutSeconds)) * 1000 + 1000))) {
            waitExpired = true; source.CancelPending();
        }
    }
    aip::kuro::Snapshot Read(const aip::kuro::Configuration& cfg, bool* completed = nullptr) {
        if (completed) *completed = false;
        if (!immediateError.empty()) { if (completed) *completed = true; return aip::kuro::MakeError(L"Wuthering Waves setup", immediateError); }
        if (waitExpired) { if (completed) *completed = true; return aip::kuro::MakeError(L"Kuro refresh timed out", L"No result arrived before the one-shot deadline."); }
        aip::kuro::Snapshot result; std::string error;
        const bool ready = source.Read(Key(cfg), result, error);
        if (completed) *completed = ready || !error.empty();
        // AsyncSource exception strings can contain transport/account details.
        // Keep those private just like native request failures.
        if (!ready && !error.empty()) return aip::kuro::MakeError(L"Kuro refresh failed", L"The Wuthering Waves source could not complete.");
        return result;
    }
    void Cancel() { source.CancelPending(); key.clear(); immediateError.clear(); waitExpired = false; }
    void Stop() { Cancel(); source.Stop(); }
};
