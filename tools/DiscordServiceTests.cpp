#include "../dependencies/DiscordRPC/service.h"
#include "../dependencies/DiscordRPC/service_cancel.h"
#include "../dependencies/content_sources/discord.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace aip::discord;
namespace {
int checks = 0;
void Check(bool value, const char* label) { ++checks; if (!value) throw std::runtime_error(label); }
struct Traffic {
    std::atomic<int> created{0}, sent{0}, cleared{0}, destroyed{0};
    std::atomic<bool> fail{false}, block{false}, ignoreCancel{false}, release{false};
};
class FakeTransport final : public Transport {
    std::shared_ptr<Traffic> traffic_;
public:
    explicit FakeTransport(std::shared_ptr<Traffic> traffic) : traffic_(std::move(traffic)) { ++traffic_->created; }
    ~FakeTransport() override { ++traffic_->destroyed; }
    void Send(const std::string& json, const std::wstring&, const Cancel& cancel) override {
        if (json.find("Fixture app") == std::string::npos) throw std::runtime_error("real builder did not produce fixture activity");
        ++traffic_->sent;
        if (traffic_->fail) throw std::runtime_error("fixture transport error");
        while (traffic_->block && !traffic_->release) {
            if (!traffic_->ignoreCancel && cancel()) throw std::runtime_error("fixture send cancelled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void Clear(const std::wstring&, const Cancel& cancel) override { if (!cancel()) ++traffic_->cleared; }
};
Options Fixture(const std::wstring& details = L"ALPHA") {
    Options options;
    options.profilePath = L"fixture-only-do-not-open.ini";
    options.refreshMs = 20;
    options.operationTimeoutMs = 200;
    auto profile = std::make_shared<Profile>();
    profile->values[{L"general", L"activity_name"}] = L"Fixture app";
    profile->values[{L"general", L"client_id"}] = L"123456789012345678";
    profile->values[{L"general", L"details_template"}] = L"{fixture}";
    profile->values[{L"general", L"state_template"}] = L"fixture state";
    options.profileOverride = profile;
    auto context = std::make_shared<Context>(); context->tokens[L"fixture"] = details;
    options.contextOverride = context;
    return options;
}
template<class Predicate> Snapshot Wait(Service& service, Predicate predicate) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        auto snapshot = service.Read();
        if (predicate(snapshot)) return snapshot;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < until);
    const auto snapshot = service.Read();
    std::cerr << "wait expired at phase " << static_cast<int>(snapshot.phase) << " revision " << snapshot.revision << "\n";
    std::wcerr << L"fixture details=" << snapshot.details << L" error=" << snapshot.error << L"\n";
    throw std::runtime_error("bounded wait for service snapshot expired");
}
void PreviewAndReload() {
    Service service; std::wstring error;
    Check(!service.Read().running && service.Stop(1), "new service is stopped");
    auto missingClient = Fixture(); missingClient.sendEnabled = true;
    auto missingProfile = std::make_shared<Profile>(*missingClient.profileOverride);
    missingProfile->values[{L"general", L"client_id"}] = L"";
    missingClient.profileOverride = missingProfile;
    Check(!service.Start(missingClient, error) && !error.empty(), "enabled sending rejects a missing client before starting a transport");
    auto options = Fixture();
    auto traffic = std::make_shared<Traffic>();
    options.transportFactory = [traffic] { return std::unique_ptr<Transport>(new FakeTransport(traffic)); };
    Check(service.Start(options, error), "preview service starts from immutable fixture data");
    auto first = Wait(service, [](const Snapshot& s) { return s.phase == Phase::Preview; });
    Check(first.name == L"Fixture app" && first.details == L"ALPHA" && first.state == L"fixture state", "actual presence builder supplies preview fields");
    Check(traffic->created == 0 && traffic->sent == 0, "network-off default never constructs any transport");
    auto text = aip::content::DiscordContent(first);
    Check(text.primary == L"ALPHA" && text.secondary == L"Preview\nfixture state" && text.badge.empty(), "tile adapter preserves preview and state in the medium text layout");
    first.details = L"changed copy";
    Check(service.Read().details == L"ALPHA", "mutating a returned snapshot cannot alter service state");
    Options invalid = options; invalid.refreshMs = 0;
    Check(!service.Reload(invalid, error) && !error.empty(), "invalid reload is rejected");
    Check(service.Read().details == L"ALPHA", "invalid reload preserves previous preview");
    auto replacement = Fixture(L"BETA");
    Check(service.Reload(replacement, error), "valid reload accepted");
    Wait(service, [](const Snapshot& s) { return s.phase == Phase::Preview && s.details == L"BETA"; });
    Check(service.Stop(2000), "preview stops within deadline");
    Check(!service.Read().running && service.Read().phase == Phase::Stopped, "stopped snapshot is explicit");
    Check(traffic->created == 0 && traffic->cleared == 0, "preview lifecycle never reads credentials or clears a transport");
}
void SendingAndErrors() {
    Service service; std::wstring error;
    auto traffic = std::make_shared<Traffic>(); auto options = Fixture(); options.sendEnabled = true;
    options.transportFactory = [traffic] { return std::unique_ptr<Transport>(new FakeTransport(traffic)); };
    Check(service.Start(options, error), "fake sending service starts");
    Wait(service, [](const Snapshot& s) { return s.phase == Phase::Active; });
    Check(traffic->sent > 0 && traffic->created == 1, "owned fake transport receives the real activity JSON");
    traffic->fail = true;
    auto failed = Wait(service, [](const Snapshot& s) { return s.phase == Phase::Error; });
    Check(failed.error == L"fixture transport error" && failed.details == L"ALPHA", "transport errors retain useful preview fields");
    Check(aip::content::DiscordContent(failed).secondary.find(L"Error") == 0 && aip::content::DiscordContent(failed).badge.empty(), "failed service is visible without selecting a counter layout");
    traffic->fail = false;
    Wait(service, [](const Snapshot& s) { return s.phase == Phase::Active; });
    Check(service.Stop(2000), "active fake service stops");
    Check(traffic->cleared == 1 && traffic->created == traffic->destroyed, "active transport clears and releases at stop");
}
void CancellationAndLifetime() {
    Service service; std::wstring error;
    auto traffic = std::make_shared<Traffic>(); traffic->block = true;
    auto options = Fixture(); options.sendEnabled = true;
    options.transportFactory = [traffic] { return std::unique_ptr<Transport>(new FakeTransport(traffic)); };
    Check(service.Start(options, error), "blocking fake transport starts");
    Wait(service, [traffic](const Snapshot&) { return traffic->sent > 0; });
    Check(service.Stop(1000), "stop interrupts a cooperative in-flight send");
    Check(traffic->created == traffic->destroyed, "cancelled transport is released");
    traffic = std::make_shared<Traffic>(); traffic->block = true; traffic->ignoreCancel = true;
    options.transportFactory = [traffic] { return std::unique_ptr<Transport>(new FakeTransport(traffic)); };
    Check(service.Start(options, error), "service can restart after a completed stop");
    Wait(service, [traffic](const Snapshot&) { return traffic->sent > 0; });
    Check(!service.Stop(1), "noncooperative transport reports pending cleanup rather than pretending to stop");
    Check(service.Read().phase == Phase::Stopping, "pending cleanup is visible to the host");
    const auto before = std::chrono::steady_clock::now();
    service.Read();
    Check(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(100), "snapshot reads do not wait for transport cleanup");
    traffic->release = true;
    Check(service.Stop(1000), "pending transport can complete cleanup after its release");
    Check(traffic->created == traffic->destroyed, "pending cleanup retains no dead host references");
}
void SnapshotStress() {
    Service first, second; std::wstring error;
    Check(first.Start(Fixture(L"FIRST"), error) && second.Start(Fixture(L"SECOND"), error), "independent profiles start together");
    Wait(first, [](const Snapshot& s) { return s.phase == Phase::Preview; });
    Wait(second, [](const Snapshot& s) { return s.phase == Phase::Preview; });
    std::atomic<bool> bad{false};
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) readers.emplace_back([&] {
        for (int i = 0; i < 5000; ++i) {
            const auto a = first.Read(), b = second.Read();
            if ((!a.details.empty() && a.details != L"FIRST" && a.details != L"RELOADED") || b.details != L"SECOND") bad = true;
        }
    });
    for (int i = 0; i < 100; ++i) Check(first.Reload(Fixture(L"RELOADED"), error), "stress reload accepted");
    for (auto& reader : readers) reader.join();
    Wait(first, [](const Snapshot& s) { return s.details == L"RELOADED"; });
    Check(!bad, "40000 concurrent snapshot reads preserve profile and field isolation");
    Check(first.Stop(2000) && second.Stop(2000), "stress services drain workers");
}
void CancellationRelayLifetime() {
    CancellationRelay relay;
    const auto observe = relay.Callback();
    Check(!observe(), "idle transport has no expired operation deadline");
    std::atomic<bool> expired{false};
    {
        auto operation = relay.Begin([&expired] { return expired.load(); });
        Check(!observe(), "active operation initially remains uncancelled");
        expired = true;
        Check(observe(), "operation deadline still cancels while its scope is active");
    }
    Check(!observe(), "completed operation deadline cannot poison idle Gateway callbacks");
    {
        auto operation = relay.Begin([] { return false; });
        Check(!observe(), "later operation does not inherit the previous deadline");
    }
    Cancel surviving;
    std::weak_ptr<int> retained;
    {
        auto owner = std::make_unique<CancellationRelay>();
        auto marker = std::make_shared<int>(7); retained = marker;
        surviving = owner->Callback();
        auto operation = owner->Begin([marker] { return *marker == 7; });
        marker.reset(); owner.reset();
        Check(surviving() && !retained.expired(), "callbacks retain cancellation state after the transport owner disappears");
    }
    Check(!surviving() && retained.expired(), "completed scope releases callback captures without dangling owner access");
    std::atomic<bool> ready{false}, bad{false};
    std::atomic<int> calls{0};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) readers.emplace_back([&] {
        while (!ready) std::this_thread::yield();
        for (int read = 0; read < 20000; ++read) observe();
    });
    ready = true;
    for (int value = 0; value < 5000; ++value) {
        auto marker = std::make_shared<const int>(value);
        auto operation = relay.Begin([marker, value, &bad, &calls] {
            if (*marker != value) bad = true;
            ++calls; return (value & 1) != 0;
        });
        observe();
        std::this_thread::yield();
    }
    for (auto& reader : readers) reader.join();
    Check(!bad && calls >= 5000, "concurrent relay readers never observe a torn mutable callback");
    Check(!observe(), "concurrent operation scopes leave idle cancellation clear");
}
}
int main() {
    try { PreviewAndReload(); SendingAndErrors(); CancellationAndLifetime(); SnapshotStress(); CancellationRelayLifetime();
        std::cout << "Discord service fixture tests passed (" << checks << " checks; 40000 snapshot reads; 80000 relay reads; no real network or token files).\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "Discord service fixture failure after " << checks << " checks: " << error.what() << "\n"; return 1; }
}
