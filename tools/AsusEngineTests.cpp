#include "../dependencies/asusblink/service.h"
#include "../dependencies/asusblink/protocol.h"
#include "../dependencies/content_sources/asus.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace aip::asus;
namespace {
int checks = 0;
void Check(bool value, const char* text) { ++checks; if (!value) throw std::runtime_error(text); }
Event Make(Device device, std::vector<int> states, std::vector<std::uint64_t> intervals = {0}, std::int64_t duration = -1) {
    Event event; event.name = device == Device::Mic ? L"mic" : L"keyboard"; event.device = device;
    event.states = std::move(states); event.intervalsMs = std::move(intervals); event.durationMs = duration; return event;
}
void ParserAndProtocol() {
    std::uint64_t time = 0;
    Check(ParseTime(L"0.5s", time) && time == 500, "fractional seconds preserve whole milliseconds");
    Check(ParseTime(L"2h", time) && time == 7200000, "hours parse");
    Check(ParseTime(L"1e3ms", time) && time == 1000, "legacy scientific time spelling parses");
    for (auto bad : {L"nan", L"inf", L"-1", L"0.1ms", L"9223372036854775808", L"1s junk"})
        Check(!ParseTime(bad, time), "invalid or fractional milliseconds reject");
    Configuration cfg; std::wstring error;
    Configuration allLegacySlots; allLegacySlots.events.resize(3332);
    Check(Validate(allLegacySlots, error), "native configuration does not impose an invented event-count cap");
    std::map<std::wstring, std::wstring> settings = {
        {L"MicState", L"0,1"}, {L"MicInterval", L"100,200"}, {L"MicDuration", L"once"},
        {L"KeyboardState", L"off"}, {L"Event2KeyboardState", L"130"}, {L"Event1HddState", L"128,129,130,131"},
        {L"Event1HddDuration", L"0"}, {L"ErrorAction", L"log,pause"}, {L"RunAtStartup", L"true"}
    };
    Check(ParseConfiguration(settings, cfg, error) && cfg.events.size() == 3 && cfg.errorAction == ErrorAction::Pause, "legacy base and numbered settings parse without host side effects");
    auto saved = cfg; settings[L"Event2KeyboardState"] = L"256";
    Check(!ParseConfiguration(settings, cfg, error) && cfg.events.size() == saved.events.size(), "invalid setting batch preserves configuration");
    settings[L"Event2KeyboardState"] = L"130"; settings[L"Mic_Interval"] = L"50";
    Check(!ParseConfiguration(settings, cfg, error), "duplicate semantic setting aliases reject");
    auto packet = SetRequest(Device::Mic, 1);
    const std::array<unsigned char, 16> expected{{0x44,0x45,0x56,0x53,8,0,0,0,0x17,0,4,0,1,0,0,0}};
    Check(packet == expected, "native DEVS packet exactly matches active C# mic protocol");
    auto keyboard = SetRequest(Device::Keyboard, 130);
    Check(keyboard[8] == 0x21 && keyboard[10] == 5 && keyboard[12] == 130, "keyboard protocol uses only brightness device");
    unsigned char accepted[16] = {1};
    Check(AcceptedResponse(accepted, 4) && AcceptedResponse(accepted, 16), "valid bounded firmware response accepted");
    Check(!AcceptedResponse(accepted, 3) && !AcceptedResponse(accepted, 17), "truncated and oversized firmware responses reject");
    accepted[0] = 0; Check(!AcceptedResponse(accepted, 16), "firmware rejection is not success");
}
void Patterns() {
    Configuration cfg; cfg.events = {Make(Device::Mic, {0,1}, {100,200})};
    PatternEngine engine; std::wstring error;
    Check(engine.Reset(cfg, error), "pattern reset");
    auto actions = engine.Step(0); Check(actions.size() == 1 && actions[0].state == 0, "first state applies immediately");
    Check(engine.Step(99).empty(), "holds until exact interval");
    actions = engine.Step(100); Check(actions.size() == 1 && actions[0].state == 1, "next state at interval boundary");
    Check(engine.Step(299).empty() && !engine.Read().complete, "one-shot includes last state hold interval");
    engine.Step(300); Check(engine.Read().complete, "one-shot completes after last interval");
    engine.Reset(cfg, error); engine.Step(0); engine.Step(50, true); engine.Step(1050, true); engine.Step(1050, false);
    Check(engine.Step(1099).empty(), "paused time is excluded");
    actions = engine.Step(1100); Check(actions.size() == 1 && actions[0].state == 1, "resume preserves remaining interval");
    cfg.events = {Make(Device::Keyboard, {128,129,130}, {0}, 0)}; engine.Reset(cfg, error);
    Check(engine.Step(0)[0].state == 128 && engine.Step(199).empty() && engine.Step(200)[0].state == 128,
        "zero-interval infinite pattern holds one state and reapplies every 200ms");
    cfg.events = {Make(Device::Keyboard, {128,129}, {0}, 300)}; engine.Reset(cfg, error);
    Check(engine.Step(0)[0].state == 128 && engine.Step(200)[0].state == 129, "finite zero intervals use 200ms steps");
    engine.Step(400); Check(engine.Read().complete, "finite duration ends after its final hold");
    cfg.events = {Make(Device::Mic, {1}, {0}), Make(Device::Mic, {0}, {0})};
    cfg.events[0].priority = 2; cfg.events[1].priority = 1; engine.Reset(cfg, error);
    actions = engine.Step(0); Check(actions.size() == 2 && actions[0].state == 0 && actions[1].state == 1, "events serialize by numeric device priority");
    cfg.events[1].durationMs = 0; engine.Reset(cfg, error);
    actions = engine.Step(0); Check(actions.size() == 1 && actions[0].state == 0, "infinite earlier event keeps later event pending");
    auto hdd = Make(Device::Keyboard, {128,129,130}, {0}, 0); hdd.hddActivity = true; cfg.events = {hdd}; engine.Reset(cfg, error);
    Check(engine.Step(0, false, 0)[0].state == 128, "HDD idle mapping");
    Check(engine.Step(499, false, 20000000).empty(), "HDD zero interval falls back to 500ms");
    Check(engine.Step(500, false, 20000000)[0].state == 130, "HDD level clamps to last configured state");
    Check(DiskLevel(1) == 1 && DiskLevel(102400) == 1 && DiskLevel(102401) == 2 && DiskLevel(1048576) == 2 &&
        DiskLevel(1048577) == 3 && DiskLevel(10485760) == 3 && DiskLevel(10485761) == 4, "disk activity boundaries match legacy thresholds");
    hdd.durationMs = 300; cfg.events = {hdd}; engine.Reset(cfg, error); engine.Step(0);
    actions = engine.Step(500); Check(actions.size() == 1 && engine.Read().complete, "timed HDD retains legacy final sample before completion");
    cfg.events = {Make(Device::Keyboard, {128,129,130,131}, {25,50}, 0)}; engine.Reset(cfg, error);
    std::uint64_t now = 0, random = 7919;
    for (int i = 0; i < 100000; ++i) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17; now += random % 1000;
        bool paused = random % 11 == 0; actions = engine.Step(now, paused, random);
        Check(!paused || actions.empty(), "stress: paused step emits no outputs");
        for (const auto& action : actions) Check(action.state >= 128 && action.state <= 131, "stress: output remains in configured states");
        Check(actions.size() <= 64, "stress: catch-up work is bounded");
    }
}
struct Activity {
    std::atomic<int> created{0}, applied{0}, sampled{0}, destroyed{0};
    std::atomic<bool> fail{false}, block{false};
};
class FakeBackend final : public Backend {
    std::shared_ptr<Activity> activity_;
public:
    explicit FakeBackend(std::shared_ptr<Activity> activity) : activity_(std::move(activity)) { ++activity_->created; }
    ~FakeBackend() override { ++activity_->destroyed; }
    bool Apply(Device, int, const Cancel& cancel, std::wstring& error) override {
        ++activity_->applied;
        while (activity_->block && !cancel()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (cancel() || activity_->fail) { error = L"fixture failure"; return false; }
        error.clear(); return true;
    }
    bool ReadDiskBytes(std::uint64_t& bytes, const Cancel&, std::wstring& error) override { ++activity_->sampled; bytes = 20000000; error.clear(); return true; }
};
template<class Predicate> Snapshot Wait(Service& service, Predicate predicate) {
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do { auto snapshot = service.Read(); if (predicate(snapshot)) return snapshot; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    while (std::chrono::steady_clock::now() < until);
    throw std::runtime_error("bounded fixture snapshot wait expired");
}
void ServiceFixtures() {
    auto activity = std::make_shared<Activity>(); Options options;
    options.configuration.events = {Make(Device::Mic, {1})};
    options.backendFactory = [activity] { return std::unique_ptr<Backend>(new FakeBackend(activity)); };
    Service service; std::wstring error;
    Check(service.Start(options, error), "preview service starts");
    auto preview = Wait(service, [](const Snapshot& s) { return s.revision > 0; });
    Check(activity->created == 0 && activity->applied == 0 && activity->sampled == 0, "preview constructs no hardware or counter backend");
    Check(aip::content::AsusContent(preview).primary == L"ASUS preview" && aip::content::AsusContent(preview).badge.empty(), "tile adapter labels preview without hiding medium secondary state");
    auto bad = options; bad.configuration.retries = -1;
    Check(!service.Reload(bad, error), "invalid reload preserves running profile");
    options.hardwareEnabled = true; Check(service.Reload(options, error), "explicit control enables fake backend only");
    Wait(service, [](const Snapshot& s) { return s.appliedMic == 1; });
    Check(activity->created == 1 && activity->applied == 1, "one-shot applies exactly one fake output");
    Check(service.Stop(1000) && activity->destroyed == 1, "service stop closes its owned backend");
    auto hdd = Make(Device::Keyboard, {128,129,130,131}, {10}, 0); hdd.hddActivity = true;
    options.hardwareEnabled = false; options.readDiskActivity = true; options.configuration.events = {hdd};
    Check(service.Start(options, error), "read-only HDD preview starts");
    Wait(service, [](const Snapshot& s) { return s.pattern.diskLevel == 4; });
    Check(activity->sampled > 0 && activity->applied == 1, "status sampling does not imply hardware actions");
    service.Pause(true); Wait(service, [](const Snapshot& s) { return s.status == L"Paused"; });
    Check(service.Stop(1000), "paused service stops cooperatively");
    activity->fail = true; options.hardwareEnabled = true; options.readDiskActivity = false;
    options.configuration.events = {Make(Device::Keyboard, {130})}; options.configuration.retries = 0; options.configuration.errorAction = ErrorAction::Fault;
    Check(service.Start(options, error), "fault-policy fake service starts");
    auto fault = Wait(service, [](const Snapshot& s) { return !s.running; });
    Check(fault.faulted && !fault.error.empty(), "legacy crash action faults only the source and leaves this test host alive");
    Check(service.Stop(1000), "faulted worker is joined");
    activity->fail = false; activity->block = true; options.configuration.errorAction = ErrorAction::Continue;
    int before = activity->applied; Check(service.Start(options, error), "cancellable fake request starts");
    Wait(service, [activity, before](const Snapshot&) { return activity->applied > before; });
    Check(service.Stop(1000), "in-flight fake hardware request is cancelled without touching a driver");
    Check(activity->created == activity->destroyed, "all fixture backends drain");
}
}
int main() {
    try { ParserAndProtocol(); Patterns(); ServiceFixtures();
        std::cout << "ASUS engine tests passed (" << checks << " checks; 100000 randomized steps; native backend excluded).\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "ASUS fixture failure after " << checks << " checks: " << error.what() << "\n"; return 1; }
}
