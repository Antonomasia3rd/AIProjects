#include "service.h"
#include "profile_edit.h"
#include "service_cancel.h"
#define AIP_DISCORD_ENGINE_CONTEXT
#include "drpc_environment.inc"
#include <condition_variable>
#include <chrono>

static thread_local const aip::discord::Options* g_embeddedOptions = nullptr;
#include "drpc_types.inc"
#include "drpc_core.inc"
#include "drpc_config_defaults.inc"
#include "drpc_presence.inc"
#include "drpc_ipc.inc"
#include "drpc_gateway.inc"

namespace aip { namespace discord {
bool ResolveConfiguredToken(const std::wstring& plaintext, const std::wstring& protectedValue,
    const CredentialDecoder& decoder, std::wstring& token, std::wstring& error) {
    token.clear(); error.clear();
    const auto protectedToken = aip::Trim(protectedValue);
    if (!protectedToken.empty()) {
        if (!decoder) { error = L"A protected Discord token could not be decoded."; return false; }
        try { token = aip::Trim(decoder(protectedToken)); }
        catch (...) { error = L"A protected Discord token could not be decoded."; return false; }
        if (token.empty()) { error = L"A protected Discord token decoded to an empty value."; return false; }
        return true;
    }
    token = aip::Trim(plaintext);
    if (token.empty()) { error = L"No Discord token is configured."; return false; }
    return true;
}
std::vector<ProfileOptionDescriptor> ProfileOptions() {
    std::vector<ProfileOptionDescriptor> options;
    const std::vector<std::wstring> sections = {L"general", L"censor_map", L"large_time_ranges", L"small_time_ranges", L"large_assets", L"small_assets", L"layout", L"buttons", L"afk", L"ipc", L"gateway"};
    for (const auto& value : kDefaults) {
        if (std::find(sections.begin(), sections.end(), value.section) == sections.end()) continue;
        ProfileOptionDescriptor option;
        option.section = value.section; option.key = value.key; option.label = option.section + L"." + option.key; option.defaultValue = value.value;
        const auto integer = [&](int minimum, int maximum) { option.kind = ProfileOptionKind::Integer; option.minimum = minimum; option.maximum = maximum; };
        if ((option.section == L"general" && option.key == L"window_title_detection_enabled") ||
            (option.section == L"censor_map" && option.key == L"apply_pattern_on_raw") || option.section == L"layout" ||
            (option.section == L"afk" && option.key == L"show_idle_time") || (option.section == L"gateway" && option.key == L"supported")) option.kind = ProfileOptionKind::Boolean;
        else if (option.section == L"general" && option.key == L"activity_type") option.kind = ProfileOptionKind::ActivityType;
        else if (option.section == L"general" && option.key == L"client_id") option.kind = ProfileOptionKind::ClientId;
        else if (option.section == L"general" && option.key == L"token_protected") option.kind = ProfileOptionKind::OpaqueSecret;
        else if (option.section == L"general" && option.key == L"update_interval") { integer(1, 86400); option.hostCadence = true; }
        else if (option.section == L"general" && option.key == L"transport_mode") { option.kind = ProfileOptionKind::Enumeration; option.choices = {L"ipc", L"gateway", L"auto"}; }
        else if (option.section == L"general" && option.key == L"status") { option.kind = ProfileOptionKind::Enumeration; option.choices = {L"online", L"idle", L"dnd", L"invisible"}; }
        else if (option.section == L"large_time_ranges" || option.section == L"small_time_ranges") integer(0, 24);
        else if (option.section == L"afk" && option.key == L"idle_threshold") integer(0, 86400);
        else if (option.section == L"ipc") integer(option.key == L"connect_timeout_ms" ? 50 : 250, option.key == L"connect_timeout_ms" ? 30000 : 120000);
        else if (option.section == L"gateway") {
            if (option.key == L"client_build_number") integer(1, INT_MAX);
            else if (option.key == L"capabilities") integer(0, INT_MAX);
            else if (option.key == L"close_timeout_ms") integer(0, 30000);
            else if (option.key.size() >= 11 && option.key.substr(option.key.size() - 11) == L"_timeout_ms") integer(1000, option.key == L"ready_timeout_ms" ? 180000 : 120000);
        }
        options.push_back(std::move(option));
    }
    ProfileOptionDescriptor legacy;
    legacy.section = L"general"; legacy.key = L"token"; legacy.label = L"general.token (private input only)"; legacy.kind = ProfileOptionKind::SecretText;
    options.push_back(std::move(legacy));
    return options;
}
bool ValidateProfileFieldGrammar(const std::wstring& section, const std::wstring& key, const std::wstring& value, std::wstring& error) {
    error.clear();
    if (section != L"censor_map") return true;
    if (key == L"rule_order") {
        std::vector<std::wstring> unknown;
        ParseCensorRuleOrder(value, &unknown);
        if (!unknown.empty()) { error = L"Censor rule order contains an unknown rule kind."; return false; }
    } else if (key == L"full_replace" || key == L"word_replace" || key == L"pattern_replace") {
        const auto rules = ParseReplacementRules(value);
        if (!Trim(value).empty() && rules.empty()) { error = L"Censor replacement rules require a source and replacement separator."; return false; }
        if (key == L"pattern_replace") {
            try { for (const auto& rule : rules) { const std::wregex pattern(rule.first, std::regex_constants::icase); (void)pattern; } }
            catch (...) { error = L"A censor regular expression is invalid (value not shown)."; return false; }
        }
    }
    return true;
}
namespace {
class ActualTransport final : public Transport {
    CancellationRelay cancellation_; // Outlives client cleanup; callbacks also retain its state.
    std::unique_ptr<DiscordIpcClient> ipc_;
    std::unique_ptr<DiscordGatewayClient> gateway_;
public:
    void Send(const std::string& activity, const std::wstring& status, const Cancel& cancel) override {
        auto operation = cancellation_.Begin(cancel);
        const auto client = Trim(IniReadS(L"general", L"client_id", L""));
        if (client.empty()) throw std::runtime_error("Missing [general] client_id.");
        const auto ipc = [&] {
            gateway_.reset();
            if (!ipc_) ipc_.reset(new DiscordIpcClient(client, false, LoadIpcOptions(), cancellation_.Callback()));
            if (!ipc_->IsConnected()) ipc_->Connect();
            ipc_->SetActivity(activity);
        };
        const auto gateway = [&] {
            ipc_.reset();
            if (!IniReadB(L"gateway", L"supported", true)) throw std::runtime_error("Gateway transport is disabled in the profile.");
            // Credentials are never resolved in preview mode or by injected
            // transports. No migration, encryption or configuration write occurs.
            if (!gateway_) {
                auto token = ResolveDiscordToken();
                if (token.empty()) throw std::runtime_error("Gateway sending needs a configured token.");
                gateway_.reset(new DiscordGatewayClient(token, client, false, LoadGatewayOptions(), cancellation_.Callback()));
            }
            if (!gateway_->IsConnected()) gateway_->Connect(activity, status);
            gateway_->SetPresence(activity, status);
        };
        const auto mode = NormalizeTransportMode(IniReadS(L"general", L"transport_mode", L"ipc"));
        if (mode == L"gateway") gateway();
        else if (mode == L"auto") {
            try { ipc(); }
            catch (...) { if (cancel()) throw; gateway(); }
        } else ipc();
    }
    void Clear(const std::wstring& status, const Cancel& cancel) override {
        auto operation = cancellation_.Begin(cancel);
        if (ipc_ && ipc_->IsConnected()) ipc_->ClearActivity();
        if (gateway_ && gateway_->IsConnected()) gateway_->ClearPresence(status);
    }
};
bool Validate(const Options& options, std::wstring& error) {
    if (!options.profileOverride && (options.profilePath.empty() || PathIsRelativeW(options.profilePath.c_str())))
        error = L"An explicit absolute Discord profile path is required.";
    else if (!options.profileOverride && (GetFileAttributesW(options.profilePath.c_str()) == INVALID_FILE_ATTRIBUTES ||
        (GetFileAttributesW(options.profilePath.c_str()) & FILE_ATTRIBUTE_DIRECTORY)))
        error = L"The Discord profile must be an existing file; the embedded service never creates one.";
    else if (options.refreshMs < 10 || options.refreshMs > 86400000)
        error = L"Refresh interval must be between 10 and 86400000 milliseconds.";
    else if (options.operationTimeoutMs < 100 || options.operationTimeoutMs > 30000)
        error = L"Operation timeout must be between 100 and 30000 milliseconds.";
    else { error.clear(); return true; }
    return false;
}
bool PrepareOptions(const Options& source, Options& result, std::wstring& error) {
    if (!Validate(source, error)) return false;
    result = source;
    auto profile = std::make_shared<Profile>();
    if (source.profileOverride) {
        for (const auto& entry : source.profileOverride->values)
            profile->values[{ToLower(entry.first.first), ToLower(entry.first.second)}] = entry.second;
    } else {
        std::vector<aip::IniSectionData> document;
        if (!aip::LoadIniDocument(source.profilePath, document)) { error = L"Could not read the Discord profile."; return false; }
        for (const auto& section : document)
            for (const auto& entry : section.entries)
                profile->values[{ToLower(section.name), ToLower(entry.key)}] = entry.value;
    }
    result.profileOverride = std::move(profile);
    if (source.sendEnabled) {
        const auto client = result.profileOverride->values.find({L"general", L"client_id"});
        if (client == result.profileOverride->values.end() || Trim(client->second).empty()) {
            error = L"Missing [general] client_id for sending.";
            return false;
        }
    }
    if (source.contextOverride) result.contextOverride = std::make_shared<Context>(*source.contextOverride);
    return true;
}
}

struct Service::Impl {
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        Options options;
        Snapshot snapshot;
        std::atomic<bool> stop{false};
        std::atomic<std::uint64_t> generation{1};
        bool done = false;
    };
    std::shared_ptr<State> state;
    std::thread worker;
    mutable std::mutex lifecycle;
    mutable std::mutex statePointer;

    static void Publish(const std::shared_ptr<State>& state, Snapshot snapshot) {
        std::lock_guard<std::mutex> lock(state->mutex);
        snapshot.revision = state->snapshot.revision + 1;
        state->snapshot = std::move(snapshot);
    }
    static void Run(std::shared_ptr<State> state) noexcept {
        std::unique_ptr<Transport> transport;
        Options current;
        std::uint64_t generation = 0;
        Snapshot snapshot;
        try {
            for (;;) {
                if (state->stop.load()) break;
                const auto requested = state->generation.load();
                if (requested != generation) {
                    transport.reset();
                    { std::lock_guard<std::mutex> lock(state->mutex); current = state->options; }
                    generation = requested;
                    g_iniPath = current.profilePath;
                    g_embeddedOptions = &current;
                }
                snapshot.running = true;
                snapshot.sendingEnabled = current.sendEnabled;
                snapshot.error.clear();
                snapshot.phase = Phase::Starting;
                try {
                    TemplateContext context;
                    if (current.contextOverride) { context.tokens = current.contextOverride->tokens; context.idleSeconds = current.contextOverride->idleSeconds; }
                    auto payload = BuildPresencePayload(current.contextOverride ? &context : nullptr);
                    snapshot.activityJson = std::move(payload.activityJson);
                    snapshot.name = ExtractJsonStringValue(snapshot.activityJson, "name");
                    snapshot.details = ExtractJsonStringValue(snapshot.activityJson, "details");
                    snapshot.state = ExtractJsonStringValue(snapshot.activityJson, "state");
                    snapshot.status = NormalizeStatus(IniReadS(L"general", L"status", L"online"));
                    if (!current.sendEnabled) {
                        snapshot.phase = Phase::Preview;
                    } else {
                        snapshot.phase = Phase::Sending;
                        Publish(state, snapshot);
                        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(current.operationTimeoutMs);
                        const Cancel cancel = [state, generation, deadline] {
                            return state->stop.load() || state->generation.load() != generation || std::chrono::steady_clock::now() >= deadline;
                        };
                        if (!transport) transport = current.transportFactory ? current.transportFactory() : std::unique_ptr<Transport>(new ActualTransport());
                        if (!transport) throw std::runtime_error("Transport factory returned no transport.");
                        transport->Send(snapshot.activityJson, snapshot.status, cancel);
                        if (cancel()) throw std::runtime_error("Presence update cancelled or timed out.");
                        snapshot.phase = Phase::Active;
                    }
                } catch (const std::exception& error) {
                    snapshot.phase = Phase::Error;
                    snapshot.error = Utf8ToWide(error.what());
                    transport.reset();
                } catch (...) {
                    snapshot.phase = Phase::Error;
                    snapshot.error = L"Unexpected presence provider failure.";
                    transport.reset();
                }
                if (state->generation.load() == generation) Publish(state, snapshot);
                std::unique_lock<std::mutex> lock(state->mutex);
                state->changed.wait_for(lock, std::chrono::milliseconds(current.refreshMs), [&] {
                    return state->stop.load() || state->generation.load() != generation;
                });
            }
            if (transport && current.sendEnabled && current.clearOnStop) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::min(current.operationTimeoutMs, 1000));
                try { transport->Clear(snapshot.status, [deadline] { return std::chrono::steady_clock::now() >= deadline; }); }
                catch (...) { }
            }
            transport.reset();
        } catch (...) {
            snapshot.error = L"The Discord service stopped after an unexpected internal failure.";
        }
        g_embeddedOptions = nullptr;
        snapshot.running = false;
        snapshot.phase = Phase::Stopped;
        Publish(state, snapshot);
        { std::lock_guard<std::mutex> lock(state->mutex); state->done = true; }
        state->changed.notify_all();
    }
};

Service::Service() : impl_(new Impl()) {
    static std::once_flag configured;
    std::call_once(configured, [] {
        // Engine diagnostics stay in snapshots. Never configure app logging,
        // console, tray, startup, mutexes, or token migration from this TU.
        g_exePath = aip::GetCurrentExecutablePath();
    });
}
Service::~Service() {
    if (!Stop(5000)) {
        // A transport that violates its cancellation contract must not retain
        // a host reference. Its worker owns State until it eventually unwinds.
        std::lock_guard<std::mutex> lock(impl_->lifecycle);
        if (impl_->worker.joinable()) impl_->worker.detach();
    }
}
bool Service::Start(const Options& options, std::wstring& error) {
    Options prepared;
    if (!PrepareOptions(options, prepared, error)) return false;
    std::lock_guard<std::mutex> lock(impl_->lifecycle);
    if (impl_->worker.joinable()) { error = L"The Discord service is already started; use Reload or Stop first."; return false; }
    auto state = std::make_shared<Impl::State>();
    state->options = std::move(prepared); state->snapshot.phase = Phase::Starting; state->snapshot.running = true;
    try { impl_->worker = std::thread(&Impl::Run, state); }
    catch (...) { error = L"Could not create the Discord service worker."; return false; }
    { std::lock_guard<std::mutex> stateLock(impl_->statePointer); impl_->state = std::move(state); }
    return true;
}
bool Service::Reload(const Options& options, std::wstring& error) {
    Options prepared;
    if (!PrepareOptions(options, prepared, error)) return false;
    std::lock_guard<std::mutex> lock(impl_->lifecycle);
    auto state = impl_->state;
    if (!state || state->stop.load()) { error = L"The Discord service is not running."; return false; }
    { std::lock_guard<std::mutex> stateLock(state->mutex); state->options = std::move(prepared); state->generation.fetch_add(1); }
    state->changed.notify_all(); return true;
}
bool Service::Stop(unsigned timeoutMs) {
    std::lock_guard<std::mutex> lock(impl_->lifecycle);
    if (!impl_->worker.joinable()) return true;
    auto state = impl_->state; state->stop = true; state->changed.notify_all();
    std::unique_lock<std::mutex> stateLock(state->mutex);
    if (!state->done) { state->snapshot.phase = Phase::Stopping; ++state->snapshot.revision; }
    if (!state->changed.wait_for(stateLock, std::chrono::milliseconds(std::min(timeoutMs, 30000u)), [&] { return state->done; })) return false;
    stateLock.unlock(); impl_->worker.join(); return true;
}
Snapshot Service::Read() const {
    std::shared_ptr<Impl::State> state;
    { std::lock_guard<std::mutex> lock(impl_->statePointer); state = impl_->state; }
    if (!state) return {};
    std::lock_guard<std::mutex> stateLock(state->mutex);
    return state->snapshot;
}
} }
