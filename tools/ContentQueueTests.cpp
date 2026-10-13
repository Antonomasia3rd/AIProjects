#include "../dependencies/content_queue.h"
#include <iostream>
#include <stdexcept>

using namespace aip::content;
static int checks = 0;
static void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
static Configuration BaseConfiguration() {
    Configuration cfg;
    cfg.enabled = cfg.cycle = true; cfg.cycleDriver = CycleDriver::Windows;
    cfg.entries.resize(MaxEntries);
    for (auto& entry : cfg.entries) entry.enabled = false;
    cfg.entries[8].enabled = cfg.entries[9].enabled = true;
    cfg.entries[8].sources = cfg.entries[9].sources = {L"CustomText"};
    return cfg;
}
static Snapshot Frame(std::size_t index, const std::wstring& key) {
    Snapshot snapshot;
    snapshot.active = true; snapshot.name = L"Fixture";
    snapshot.backgroundPath = L"not opened";
    snapshot.nativeQueueTag = NativeQueueTag(index);
    snapshot.nativeQueueFrameKey = key;
    snapshot.nativeQueueReset = true; snapshot.nativeQueueLayout = L"input must remain unchanged";
    return snapshot;
}
static void TestEligibility() {
    auto cfg = BaseConfiguration();
    Check(NativeQueueEligible(cfg, true), "two enabled passive contents use Windows queue on native delivery");
    Check(!NativeQueueEligible(cfg, false), "registration delivery retains host cycling");
    cfg.cycleDriver = CycleDriver::Auto;
    Check(NativeQueueEligible(cfg, true), "automatic driver uses native queue when eligible");
    cfg.cycleDriver = CycleDriver::DesktopStub;
    Check(!NativeQueueEligible(cfg, true), "explicit host driver preserves its timer");
    cfg = BaseConfiguration(); cfg.enabled = false;
    Check(!NativeQueueEligible(cfg, true), "disabled composition cannot enqueue contents");
    cfg.enabled = true; cfg.cycle = false;
    Check(!NativeQueueEligible(cfg, true), "disabled cycling cannot enqueue contents");
    for (int count = 0; count <= 6; ++count) {
        cfg = BaseConfiguration();
        for (auto& entry : cfg.entries) entry.enabled = false;
        for (int index = 0; index < count; ++index) cfg.entries[static_cast<std::size_t>(index)].enabled = true;
        Check(NativeQueueEligible(cfg, true) == (count >= 2 && count <= 5), "queue eligibility counts enabled entries and enforces Windows five-item bound");
    }
    for (const auto* source : {L"DiscordRPC", L"CapsBlink", L"AsusBlink"}) {
        cfg = BaseConfiguration(); cfg.entries[8].sources = {source};
        Check(NativeQueueEligible(cfg, true), "preview-only source keeps native queue eligibility");
        if (std::wstring(source) == L"DiscordRPC") cfg.discord.send = true;
        else if (std::wstring(source) == L"CapsBlink") cfg.caps.actionsEnabled = true;
        else cfg.asus.hardwareEnabled = true;
        Check(!NativeQueueEligible(cfg, true), "visibility-dependent presence/hardware action falls back to host cycle");
    }
    cfg = BaseConfiguration(); cfg.entries[3].sources = {L"CapsBlink"}; cfg.caps.actionsEnabled = true;
    Check(NativeQueueEligible(cfg, true), "disabled actuator entry does not incorrectly block passive enabled contents");
}
static void TestDeltaAndOwnership() {
    QueueDelta delivery;
    auto cfg = BaseConfiguration();
    const auto layout = NativeQueueLayoutKey(cfg);
    std::vector<Snapshot> frames{Frame(8, L"visual-nine"), Frame(9, L"visual-ten")};
    auto plan = delivery.Plan(layout, frames);
    Check(plan.size() == 2 && plan[0].nativeQueueTag == L"Content9" && plan[1].nativeQueueTag == L"Content10",
        "native plan preserves source order rather than lexical ordering of slots nine/ten");
    Check(plan[0].nativeQueue && plan[0].nativeQueueReset && !plan[1].nativeQueueReset && plan[0].nativeQueueLayout == layout,
        "fresh process plans one initial reset followed by ordinary tagged slots");
    Check(!frames[0].nativeQueue && frames[0].nativeQueueReset && frames[0].nativeQueueLayout == L"input must remain unchanged" &&
        frames[0].backgroundPath == L"not opened", "planner only modifies copies and never reads or changes source snapshots");
    Check(delivery.CommittedCount() == 0 && delivery.Layout().empty(), "planning alone does not acknowledge or mutate committed delivery state");
    Check(delivery.Plan(layout, frames).front().nativeQueueReset, "a failed initial API with no Ack retains the initial reset requirement");
    Check(delivery.Ack(plan[0]) && delivery.CommittedCount() == 1, "only a successful initial slot is acknowledged after reset");
    auto remainder = delivery.Plan(layout, frames);
    Check(remainder.size() == 1 && remainder[0].nativeQueueTag == L"Content10" && !remainder[0].nativeQueueReset,
        "partial batch success retries only the unacknowledged slot without clearing the successful first item");
    Check(delivery.Ack(remainder[0]) && delivery.CommittedCount() == 2 && delivery.Plan(layout, frames).empty(),
        "all successful slots suppress unchanged republishes");
    cfg.cycleSeconds = 86400; cfg.refreshSeconds = 300;
    Check(NativeQueueLayoutKey(cfg) == layout && delivery.Plan(NativeQueueLayoutKey(cfg), frames).empty() && NativeQueueEligible(cfg, true),
        "app timer/refresh interval changes do not invent a Windows rotation timer or reset unchanged visuals");
    frames[1].nativeQueueFrameKey = L"new track art and text";
    auto changed = delivery.Plan(layout, frames);
    Check(changed.size() == 1 && changed[0].nativeQueueTag == L"Content10" && !changed[0].nativeQueueReset,
        "media-only visual change replaces its semantic tag without a layout reset");
    Check(delivery.CommittedFrame(L"Content10") == L"visual-ten", "a failed changed-slot API with no Ack leaves committed visual key intact");
    Check(delivery.Ack(changed[0]) && delivery.CommittedFrame(L"Content10") == L"new track art and text" && delivery.CommittedCount() == 2,
        "successful replacement updates only its committed tag");
    auto forced = delivery.Plan(layout, frames, true);
    Check(forced.size() == 2 && !forced[0].nativeQueueReset && !forced[1].nativeQueueReset,
        "explicit force republishes all visuals without clearing an unchanged layout");
    Check(delivery.Ack(forced[0]) && delivery.Ack(forced[1]) && delivery.CommittedCount() == 2,
        "forced acknowledgements preserve all committed tags");
    cfg.entries[8].enabled = false; cfg.entries[7].enabled = true;
    const auto newLayout = NativeQueueLayoutKey(cfg);
    const std::vector<Snapshot> replacement{Frame(7, L"visual-eight"), Frame(9, L"new track art and text")};
    auto newPlan = delivery.Plan(newLayout, replacement);
    Check(newPlan.size() == 2 && newPlan[0].nativeQueueReset && !newPlan[1].nativeQueueReset && delivery.CommittedCount() == 2,
        "layout replacement schedules one reset but does not clear state before API success");
    Check(delivery.Ack(newPlan[0]) && delivery.CommittedCount() == 1 && delivery.CommittedFrame(L"Content9").empty(),
        "successful new-layout reset drops old tags and records only its successful slot");
    Check(delivery.Ack(newPlan[1]) && delivery.CommittedCount() == 2 && delivery.Plan(newLayout, replacement).empty(),
        "later new-layout slots do not erase earlier successful acknowledgements");
    delivery.LeaveQueue();
    Check(delivery.CommittedCount() == 0 && delivery.Layout().empty(), "departure clears only planner state without any OS call");
    Check(delivery.Plan(newLayout, replacement).front().nativeQueueReset, "reentry requires a reset of any persisted Windows queue");
    QueueDelta restarted;
    Check(restarted.Plan(newLayout, replacement).front().nativeQueueReset, "new process state forces initial reset without disk or registry persistence");
}
static void TestInvalidPlansAndAcks() {
    QueueDelta delivery;
    auto valid = Frame(0, L"valid");
    auto plan = delivery.Plan(L"layout", {valid});
    Check(delivery.Ack(plan.front()), "initialize strict planner fixture with successful metadata");
    for (int invalid = 0; invalid < 5; ++invalid) {
        auto candidate = valid;
        if (invalid == 0) candidate.active = false;
        else if (invalid == 1) candidate.nativeQueueTag.clear();
        else if (invalid == 2) candidate.nativeQueueTag = L"invalid tag";
        else if (invalid == 3) candidate.nativeQueueTag = L"1234567890ABCDEFG";
        else candidate.nativeQueueFrameKey.clear();
        bool rejected = false;
        try { delivery.Plan(L"layout", {candidate}); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected && delivery.CommittedCount() == 1 && delivery.CommittedFrame(valid.nativeQueueTag) == L"valid",
            "invalid ready frame cannot change the plan's committed state");
        candidate.nativeQueue = true; candidate.nativeQueueLayout = L"different";
        Check(!delivery.Ack(candidate) && delivery.CommittedCount() == 1 && delivery.Layout() == L"layout",
            "invalid acknowledgement cannot clear an existing layout");
    }
    for (const auto& invalidPlan : {std::vector<Snapshot>{valid, valid}, std::vector<Snapshot>(6, valid)}) {
        bool rejected = false;
        try { delivery.Plan(L"layout", invalidPlan); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected && delivery.CommittedCount() == 1, "duplicate or oversized plans are rejected instead of silently merging/truncating content");
    }
    bool noLayout = false;
    try { delivery.Plan(L"", {valid}); } catch (const std::invalid_argument&) { noLayout = true; }
    Check(noLayout && !delivery.Ack(valid) && delivery.CommittedCount() == 1, "missing-layout or unqueued acknowledgement cannot commit queue state");
    Check(delivery.Plan(L"layout", {}).empty() && delivery.CommittedCount() == 1, "empty ready set does not implicitly acknowledge a layout reset");
}
int main() {
    try {
        TestEligibility(); TestDeltaAndOwnership(); TestInvalidPlansAndAcks();
        std::cout << "Native content queue planner: " << checks << " checks passed; no OS/timer/file operations\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
