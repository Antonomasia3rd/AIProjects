// Injected coordinator tests. No host/Windows wrappers, filesystem, photos,
// package APIs, provider/session access, user INI, or tile delivery is executed.
#define AIP_GALLERY_RUNTIME_NO_HOST
#include "../dependencies/DesktopStub/ga_gallery_runtime.inc"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <stdexcept>

namespace {
using namespace aip::gallery;
namespace runtime = aip::gallery::runtime;
int checks = 0;
void Check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
Configuration Config() {
    Configuration cfg; cfg.enabled = true; cfg.groupId = L"group1"; cfg.folder = L"synthetic folder with spaces\\";
    cfg.tiles = {{L"one", L"Gallery one", true}, {L"two", L"Gallery two", true}}; return cfg;
}
runtime::Input Input() {
    runtime::Input input{L"C:\\synthetic\\DesktopStub.ini", Config(), true, true, true}; input.pinnedKnown = true;
    for (const auto& tile : input.configuration.tiles) input.pinnedIds.insert(NativeTileId(input.configuration.groupId, tile.id));
    return input;
}
std::vector<InventoryImage> Images(unsigned count, const std::wstring& prefix = L"image_") {
    std::vector<InventoryImage> result;
    for (unsigned i = 0; i < count; ++i) result.push_back({prefix + std::to_wstring(i), L"synthetic cache/" + prefix + std::to_wstring(i) + L".png"});
    return result;
}
std::string Wire(const Ledger& ledger) {
    std::string result; std::wstring error;
    if (!SerializeLedger(ledger, result, error)) throw std::runtime_error("fixture ledger serialization failed");
    return result;
}
struct Harness {
    std::mutex mutex;
    std::map<std::wstring, std::string> files;
    std::atomic<std::uint64_t> now{0};
    std::atomic<int> scans{0}, wakes{0}, saves{0}, creates{0}, plans{0};
    bool failSave = false, failPendingSave = false;
    std::function<InventoryResult(const std::wstring&, const InventoryIndex&, const InventoryCancel&)> scanner;
    runtime::Services Services() {
        runtime::Services services;
        services.now = [&] { return now.load(); }; services.wake = [&] { ++wakes; };
        services.planning = [&] { ++plans; };
        services.cacheDirectory = [](const auto&) { return std::wstring(L"synthetic cache"); };
        services.readLedger = [&](const auto& path, auto& ledger, auto& error) {
            std::lock_guard<std::mutex> lock(mutex); const auto found = files.find(path);
            if (found == files.end()) return runtime::ReadResult::Missing;
            return ParseLedger(found->second, ledger, error) ? runtime::ReadResult::Ok : runtime::ReadResult::Error;
        };
        services.createLedger = [&](const auto& path, const auto& ledger, auto& error) {
            std::lock_guard<std::mutex> lock(mutex); ++creates;
            if (files.find(path) != files.end()) { error = L"Existing ledger kept."; return false; }
            files.emplace(path, Wire(ledger)); return true;
        };
        services.saveLedger = [&](const auto& path, const auto& expected, const auto& candidate, auto& error) {
            std::lock_guard<std::mutex> lock(mutex); ++saves;
            if (failSave || (failPendingSave && candidate.nextTicket > expected.nextTicket)) { error = L"Injected ledger write failure."; return runtime::SaveResult::Error; }
            const auto found = files.find(path); if (found == files.end()) return runtime::SaveResult::Conflict;
            Ledger fresh; if (!ParseLedger(found->second, fresh, error)) return runtime::SaveResult::Conflict;
            std::string bytes; const auto decision = runtime::PrepareLedgerCas(expected, fresh, candidate, bytes, error);
            if (decision == runtime::SaveResult::Saved) found->second = std::move(bytes);
            return decision;
        };
        services.scan = [&](const auto& folder, const auto&, const auto& previous, const auto& cancel) {
            ++scans;
            if (scanner) return scanner(folder, previous, cancel);
            InventoryResult result; result.index = previous; result.complete = true; result.images = Images(20); return result;
        };
        return services;
    }
    Ledger Load(const std::wstring& profile = Input().profile) {
        std::lock_guard<std::mutex> lock(mutex); Ledger ledger; std::wstring error;
        const auto found = files.find(runtime::LedgerPath(profile));
        if (found == files.end() || !ParseLedger(found->second, ledger, error)) throw std::runtime_error("fixture ledger missing");
        return ledger;
    }
    void Initialize(const runtime::Input& input) {
        std::wstring error; Check(runtime::Initialize(Services(), input.profile, input.configuration, L"epoch1", true, error), "fixture creates an explicitly attested initial ledger");
    }
};
template<class Predicate> void Pump(runtime::Coordinator& coordinator, const runtime::Input& input, Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do { coordinator.Refresh(input); if (ready()) return; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("bounded coordinator fixture did not reach its expected state");
}
std::vector<runtime::Frame> ForTile(const std::vector<runtime::Frame>& frames, const std::wstring& tile) {
    std::vector<runtime::Frame> result;
    for (const auto& frame : frames) if (frame.receipt.tileId == tile) result.push_back(frame);
    return result;
}
void TestExplicitInitializationAndCas() {
    Harness host; const auto input = Input(); auto services = host.Services(); std::wstring error;
    Check(!runtime::Initialize(services, input.profile, input.configuration, L"epoch1", false, error) && host.creates == 0,
        "missing ownership history never automatically authorizes an initial ledger");
    host.Initialize(input); const auto initial = host.Load();
    Check(runtime::LedgerPath(input.profile) == input.profile + L".gallery-ledger", "ledger location is fixed by profile rather than folder or GroupId");
    Check(!runtime::Initialize(services, input.profile, input.configuration, L"epoch2", true, error) && Wire(host.Load()) == Wire(initial),
        "explicit create-only initialization cannot replace an existing ledger");
    OwnershipPlan first, second;
    Check(ReconcileOwnership(input.configuration, {L"a", L"b"}, initial, first, error) &&
        ReconcileOwnership(input.configuration, {L"c", L"d"}, initial, second, error), "competing writers prepare independent synthetic ownership candidates");
    std::atomic<int> saved{0}, conflicted{0};
    const auto save = [&](const Ledger& candidate) { std::wstring issue; const auto outcome = services.saveLedger(runtime::LedgerPath(input.profile), initial, candidate, issue);
        if (outcome == runtime::SaveResult::Saved) ++saved; else if (outcome == runtime::SaveResult::Conflict) ++conflicted; };
    std::thread left([&] { save(first.ledger); }), right([&] { save(second.ledger); }); left.join(); right.join();
    Check(saved == 1 && conflicted == 1 && host.Load().images.size() == 2, "headless CAS admits one current writer and preserves its ownership against a stale contender");
    auto current = host.Load(); auto candidate = current; ++candidate.revision;
    auto changed = current; changed.images.begin()->second.owner = changed.images.begin()->second.owner == L"one" ? L"two" : L"one";
    std::string bytes = "unchanged";
    Check(runtime::PrepareLedgerCas(current, changed, candidate, bytes, error) == runtime::SaveResult::Conflict && bytes == "unchanged",
        "CAS compares the full current ledger even when an external edit leaves its revision unchanged");
    candidate.images.begin()->second.owner = candidate.images.begin()->second.owner == L"one" ? L"two" : L"one";
    Check(runtime::PrepareLedgerCas(current, current, candidate, bytes, error) == runtime::SaveResult::Error && bytes == "unchanged",
        "an ordinary runtime save cannot reassign a historical image owner");
    candidate = current; ++candidate.revision; candidate.epochId = L"new_epoch";
    Check(runtime::PrepareLedgerCas(current, current, candidate, bytes, error) == runtime::SaveResult::Error,
        "an ordinary save cannot smuggle an ownership reset through a new epoch");
    runtime::Input changedGroup = input; changedGroup.configuration.groupId = L"othergroup";
    runtime::Coordinator coordinator(services); coordinator.Refresh(changedGroup);
    Check(coordinator.CurrentFrames().empty() && host.scans == 0 && Wire(host.Load()) == Wire(current),
        "changing GroupId does not select a fresh ledger or scan photos before ownership is resolved");
}
void TestPublicationAndSlotAcknowledgements() {
    Harness host; const auto input = Input(); host.Initialize(input); host.failSave = true;
    runtime::Coordinator coordinator(host.Services());
    Pump(coordinator, input, [&] { return host.saves.load() > 0; });
    Check(coordinator.CurrentFrames().empty() && host.Load().images.empty(), "failed ownership persistence exposes no publishable photo frame");
    host.failSave = false; host.now = 5000;
    Pump(coordinator, input, [&] { return coordinator.CurrentFrames().size() == 10; });
    auto frames = coordinator.CurrentFrames(); const auto durable = host.Load();
    const auto pendingPlans = host.plans.load();
    for (int tick = 0; tick < 10; ++tick) coordinator.Refresh(input);
    Check(host.plans == pendingPlans, "unchanged pending windows reuse their cached plan without validating/copying the whole ledger every poll");
    Check(coordinator.IsProfile(input.profile) && !coordinator.IsProfile(L"C:\\another\\DesktopStub.ini"),
        "publication is scoped to the currently captured INI profile even when another profile copies the same gallery IDs");
    Check(durable.images.size() == 20 && durable.tiles.at(L"one").pending.images.size() == 5 && durable.tiles.at(L"two").pending.images.size() == 5,
        "every published frame is backed by persisted ownership and a complete pending window");
    for (const auto& frame : frames) {
        const auto& window = durable.tiles.at(frame.receipt.tileId).pending;
        Check(window.ticket == frame.receipt.ticket && durable.images.at(window.images.at(frame.receipt.slot)).owner == frame.receipt.tileId,
            "each slot belongs to its exact durable owner and delivery ticket");
    }
    auto one = ForTile(frames, L"one"), two = ForTile(frames, L"two");
    Check(one.front().reset && one.front().tag == L"g1" && one.back().tag == L"g5" && one.front().activationArguments.find(L"--ini \"") == 0 &&
        one.front().activationArguments.find(L" --open-folder \"") != std::wstring::npos,
        "initial native queue uses stable slots, one reset, and quoted folder activation");
    const auto before = host.saves.load(); coordinator.Acknowledge(one.front().receipt); coordinator.Refresh(input);
    Check(host.saves == before && host.Load().tiles.at(L"one").pending.ticket != 0, "one successful slot cannot advance the entire window");
    for (std::size_t i = 1; i < one.size(); ++i) coordinator.Acknowledge(one[i].receipt);
    Check(host.saves == before && ForTile(coordinator.CurrentFrames(), L"one").empty(), "delivery callbacks only collect all-slot success and suppress duplicates; they perform no writes");
    coordinator.Refresh(input);
    Check(host.Load().tiles.at(L"one").pending.ticket == 0 && host.Load().tiles.at(L"one").cursor == 5,
        "the poller durably acknowledges only after all slots succeed");
    coordinator.Acknowledge(two.front().receipt); coordinator.Fail(two[2].receipt);
    Check(coordinator.CurrentFrames().empty() && !coordinator.Validate(two.front().receipt), "partial failure invalidates old attempt keys and all old success bits");
    host.now = 9999; coordinator.Refresh(input); Check(coordinator.CurrentFrames().empty(), "failed delivery is held to a bounded retry backoff");
    host.now = 10000; coordinator.Refresh(input); auto retried = ForTile(coordinator.CurrentFrames(), L"two");
    Check(retried.size() == 5 && retried.front().receipt.ticket == two.front().receipt.ticket && retried.front().receipt.frameKey != two.front().receipt.frameKey,
        "retry preserves durable ownership/ticket while distinguishing a fresh all-slot attempt");
    coordinator.Acknowledge(two.front().receipt);
    for (std::size_t i = 1; i < retried.size(); ++i) coordinator.Acknowledge(retried[i].receipt);
    coordinator.Refresh(input); Check(host.Load().tiles.at(L"two").pending.ticket != 0, "stale old-attempt success cannot fill a missing slot in the retry");
    coordinator.Acknowledge(retried.front().receipt); coordinator.Refresh(input);
    Check(host.Load().tiles.at(L"two").pending.ticket != 0,
        "a late reset slot clears earlier successes because those native slots were cleared by the reset");
    for (std::size_t i = 1; i < retried.size(); ++i) coordinator.Acknowledge(retried[i].receipt);
    coordinator.Refresh(input);
    Check(host.Load().tiles.at(L"two").pending.ticket == 0 && coordinator.CurrentFrames().empty(), "completed windows rest until the next scheduled batch");
    const auto idlePlans = host.plans.load();
    for (int tick = 0; tick < 10; ++tick) coordinator.Refresh(input);
    Check(host.plans == idlePlans, "idle gallery ticks with no due destinations never enter the full historical-ledger planner");
    host.now = 910000; coordinator.Refresh(input); frames = coordinator.CurrentFrames();
    Check(frames.size() == 10 && !frames.front().reset, "later equal-sized batches replace stable tags without predicting a visible fifth item");
    auto smaller = input; smaller.configuration.queueSize = 2; coordinator.Refresh(smaller); frames = coordinator.CurrentFrames();
    Check(frames.size() == 4 && ForTile(frames, L"one").front().reset && ForTile(frames, L"two").front().reset,
        "shrinking queue size repairs retained pending windows and resets old extra slots");
    const auto ownership = host.Load().images.size(); smaller.configuration.enabled = false; coordinator.Refresh(smaller);
    Check(coordinator.CurrentFrames().size() == 4 && host.Load().images.size() == ownership, "legacy enabled flags do not stop already pinned galleries or release historical ownership");
    smaller.pinnedIds.clear(); coordinator.Refresh(smaller);
    Check(coordinator.CurrentFrames().empty() && host.Load().images.size() == ownership, "unpinning every destination pauses publication while keeping ownership");
}
void TestRestartConflictAndUnavailableMode() {
    Harness host; const auto input = Input(); host.Initialize(input);
    std::vector<runtime::Frame> old;
    { runtime::Coordinator first(host.Services()); Pump(first, input, [&] { return first.CurrentFrames().size() == 10; }); old = first.CurrentFrames(); }
    const auto pending = Wire(host.Load()); runtime::Coordinator restarted(host.Services());
    Pump(restarted, input, [&] { return restarted.CurrentFrames().size() == 10; });
    Check(Wire(host.Load()) == pending && restarted.CurrentFrames().front().receipt.ticket == old.front().receipt.ticket,
        "restart resumes the same persisted pending window rather than advancing or reallocating images");
    auto unavailable = input; unavailable.directAvailable = false; const auto scans = host.scans.load(); restarted.Refresh(unavailable);
    Check(restarted.CurrentFrames().empty() && host.scans == scans && Wire(host.Load()) == pending && restarted.Status().find(L"registration") != std::wstring::npos,
        "unsupported direct delivery pauses galleries without registration loops or ledger changes");
    Harness missing; runtime::Coordinator noHistory(missing.Services()); noHistory.Refresh(input);
    Check(noHistory.CurrentFrames().empty() && missing.scans == 0 && missing.creates == 0 && noHistory.Status().find(L"missing") != std::wstring::npos,
        "missing ledger is an explicit setup state and never triggers scanning or automatic initialization");
    auto menu = noHistory.MenuStates(input.profile, input.configuration);
    Check(menu.size() == 2 && menu.front().nativeId == L"AipGallery_group1.one" && missing.scans == 0,
        "menu status and pin IDs are pure data reads without eager inventory work");
    Harness pendingFailure; pendingFailure.Initialize(input); pendingFailure.failPendingSave = true;
    runtime::Coordinator pendingCoordinator(pendingFailure.Services());
    Pump(pendingCoordinator, input, [&] { return pendingFailure.saves.load() >= 2; });
    Check(pendingCoordinator.CurrentFrames().empty() && pendingFailure.Load().images.size() == 20 && pendingFailure.Load().nextTicket == 1,
        "successful ownership persistence alone cannot publish when pending-window persistence fails");
    pendingFailure.failPendingSave = false; pendingFailure.now = 5000;
    Pump(pendingCoordinator, input, [&] { return pendingCoordinator.CurrentFrames().size() == 10; });
    const auto beforeReload = pendingCoordinator.CurrentFrames().front().receipt;
    pendingCoordinator.RequestRefresh(true); pendingCoordinator.Refresh(input);
    Check(!pendingCoordinator.Validate(beforeReload) && !pendingCoordinator.CurrentFrames().empty(),
        "same-ticket ledger reload gives a new attempt key so old generation callbacks cannot satisfy fresh reset slots");
}
void TestResumableInventoryAndShutdown() {
    Harness host; const auto input = Input(); host.Initialize(input);
    std::atomic<std::size_t> seenCursor{0};
    host.scanner = [&](const auto&, const InventoryIndex& previous, const auto&) {
        InventoryResult result; result.index = previous; seenCursor = previous.cursor;
        if (!previous.cursor) { result.index.cursor = 7; result.complete = false; result.deferredFiles = 10; result.images = Images(2, L"not_complete_"); }
        else { result.complete = true; result.images = Images(20); }
        return result;
    };
    runtime::Coordinator coordinator(host.Services()); coordinator.Refresh(input);
    Pump(coordinator, input, [&] { return host.wakes.load() >= 1; }); coordinator.Refresh(input);
    Check(coordinator.CurrentFrames().empty() && host.Load().images.empty(), "partial inventories retain progress without publishing or reserving a misleading subset");
    host.now = 250; Pump(coordinator, input, [&] { return coordinator.CurrentFrames().size() == 10; });
    Check(seenCursor == 7, "the next bounded pass receives its matching prior index cursor");
    Harness blocking; blocking.Initialize(input); std::mutex mutex; std::condition_variable changed; bool started = false; std::atomic<bool> exited{false};
    blocking.scanner = [&](const auto&, const InventoryIndex&, const InventoryCancel& cancel) {
        { std::lock_guard<std::mutex> lock(mutex); started = true; } changed.notify_all();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!cancel() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        InventoryResult result; result.complete = true; result.images = Images(20, L"late_"); exited = true; return result;
    };
    runtime::Coordinator owned(blocking.Services()); owned.Refresh(input);
    { std::unique_lock<std::mutex> lock(mutex); changed.wait_for(lock, std::chrono::seconds(1), [&] { return started; }); }
    Check(started, "shutdown fixture starts one owned cancellable inventory worker"); owned.Stop();
    Check(exited.load() && owned.CurrentFrames().empty() && blocking.Load().images.empty(), "shutdown joins the worker before captured state teardown and rejects its late inventory result");
    owned.Refresh(input); Check(blocking.scans == 1, "a stopped coordinator cannot silently start another provider worker");
    Harness noProgress; noProgress.Initialize(input);
    noProgress.scanner = [](const auto&, const auto& previous, const auto&) { InventoryResult result; result.index = previous; result.diagnostics = {L"Synthetic listing limit."}; return result; };
    runtime::Coordinator throttled(noProgress.Services()); throttled.Refresh(input);
    Pump(throttled, input, [&] { return noProgress.wakes.load() >= 1; }); throttled.Refresh(input);
    noProgress.now = 29999; throttled.Refresh(input);
    Check(noProgress.scans == 1 && throttled.CurrentFrames().empty(), "incomplete inventory with no deferred cursor progress backs off instead of rescanning a permanent listing limit every250ms");
}
void TestManyTilesAndFolders() {
    auto input = Input(); input.configuration.tiles.clear(); input.pinnedIds.clear();
    input.configuration.enabled = false; input.contentEnabled = input.tilesEnabled = false;
    for (int i = 0; i < 28; ++i) {
        TileConfiguration tile; tile.id = L"tile" + std::to_wstring(i + 1); tile.name = L"Photo " + std::to_wstring(i + 1);
        tile.folder = i < 14 ? L"folder A" : L"folder B"; tile.galleryName = i < 14 ? L"Album A" : L"Album B"; tile.enabled = false;
        input.configuration.tiles.push_back(std::move(tile));
    }
    Harness host; host.Initialize(input); std::atomic<int> firstScans{0}, secondScans{0};
    host.scanner = [&](const std::wstring& folder, const InventoryIndex& previous, const InventoryCancel&) {
        InventoryResult result; result.index = previous; result.complete = true;
        if (folder == L"folder A") { ++firstScans; result.images = Images(70, L"a_"); }
        else if (folder == L"folder B") { ++secondScans; result.images = Images(70, L"b_"); }
        else throw std::runtime_error("unexpected synthetic folder");
        const auto shared = Images(28, L"shared_"); result.images.insert(result.images.end(), shared.begin(), shared.end()); return result;
    };
    runtime::Coordinator coordinator(host.Services());
    for (int i = 0; i < 5; ++i) coordinator.Refresh(input);
    Check(host.scans == 0 && coordinator.CurrentFrames().empty(), "many configured but unpinned tiles never start a directory scan");
    input.pinnedIds.insert(NativeTileId(input.configuration.groupId, L"tile1"));
    Pump(coordinator, input, [&] { return coordinator.CurrentFrames().size() == 5; });
    auto owned = host.Load();
    Check(firstScans == 1 && secondScans == 0 && owned.tiles.size() == 14 && owned.images.size() == 98,
        "the first pin scans one folder once and reserves its photos across all fourteen configured siblings, including unpinned legacy-disabled rows");
    const auto originalOwners = owned.images;
    for (const auto& tile : input.configuration.tiles) input.pinnedIds.insert(NativeTileId(input.configuration.groupId, tile.id));
    Pump(coordinator, input, [&] { return coordinator.CurrentFrames().size() == 140 && firstScans == 2 && secondScans == 1; });
    owned = host.Load();
    Check(firstScans == 2 && secondScans == 1 && owned.images.size() == 168 && owned.tiles.size() == 28,
        "twenty-eight pinned tiles use two distinct folder inventories and deduplicate shared exact image IDs globally");
    Check(std::all_of(originalOwners.begin(), originalOwners.end(), [&](const auto& old) {
        return owned.images.at(old.first).owner == old.second.owner;
    }), "adding another folder never steals an image already reserved by the first folder");
    const auto frames = coordinator.CurrentFrames();
    Check(std::all_of(frames.begin(), frames.end(), [&](const auto& frame) {
        const auto tile = std::find_if(input.configuration.tiles.begin(), input.configuration.tiles.end(), [&](const auto& row) { return row.id == frame.receipt.tileId; });
        const auto image = owned.tiles.at(frame.receipt.tileId).pending.images.at(frame.receipt.slot);
        return tile != input.configuration.tiles.end() && frame.activationArguments.find(L"--open-folder \"" + tile->folder + L"\"") != std::wstring::npos &&
            (image.compare(0, 7, L"shared_") == 0 || image.compare(0, 2, tile->folder == L"folder A" ? L"a_" : L"b_") == 0);
    }), "every frame uses its own folder's eligible image and folder activation path");
    const auto plans = host.plans.load();
    for (int i = 0; i < 10; ++i) coordinator.Refresh(input);
    Check(host.plans == plans && firstScans == 2 && secondScans == 1, "unchanged pending windows do not restart per-folder scans or full ledger planning");
    const auto pendingWire = Wire(owned); coordinator.Stop(); runtime::Coordinator restarted(host.Services());
    Pump(restarted, input, [&] { return restarted.CurrentFrames().size() == 140; });
    Check(Wire(host.Load()) == pendingWire, "restart retains one global multi-folder ledger and the same twenty-eight pending windows");
    const auto receipt = restarted.CurrentFrames().front().receipt; restarted.Suspend();
    Check(restarted.CurrentFrames().empty() && !restarted.Validate(receipt), "redistribution suspension immediately revokes every old publishable receipt");
    const auto beforeScans = host.scans.load(); restarted.Refresh(input);
    Check(host.scans == beforeScans && Wire(host.Load()) == pendingWire, "a suspended coordinator neither scans nor modifies durable history");
    restarted.Resume(); Pump(restarted, input, [&] { return restarted.CurrentFrames().size() == 140; });
    Check(!restarted.Validate(receipt), "resuming reloads history with fresh attempt keys and keeps old callbacks invalid");
    std::string bytes = "unchanged"; std::wstring error; const auto fresh = host.Load();
    Check(runtime::PrepareLedgerReset(fresh, fresh, L"epoch2", false, bytes, error) == runtime::SaveResult::Error && bytes == "unchanged",
        "partial or unconfirmed queue clearing cannot reset ownership");
    auto changed = fresh; ++changed.revision;
    Check(runtime::PrepareLedgerReset(fresh, changed, L"epoch2", true, bytes, error) == runtime::SaveResult::Conflict && bytes == "unchanged",
        "explicit redistribution still refuses a stale full-ledger comparison");
    Ledger reset;
    Check(runtime::PrepareLedgerReset(fresh, fresh, L"epoch2", true, bytes, error) == runtime::SaveResult::Saved && ParseLedger(bytes, reset, error) &&
        reset.images.empty() && reset.tiles.empty() && reset.groupId == fresh.groupId && reset.epochId == L"epoch2",
        "confirmed full queue retirement permits an explicit empty new epoch while preserving stable native IDs");
}
void TestUnavailableFolderIsolation() {
    auto input = Input(); input.configuration.tiles[0].folder = L"good folder"; input.configuration.tiles[1].folder = L"unavailable folder";
    Harness host; host.Initialize(input); std::atomic<int> unavailableScans{0};
    host.scanner = [&](const std::wstring& folder, const InventoryIndex& previous, const InventoryCancel&) {
        InventoryResult result; result.index = previous;
        if (folder == L"good folder") { result.complete = true; result.images = Images(10, L"good_"); }
        else { ++unavailableScans; result.diagnostics = {L"Synthetic inaccessible folder."}; }
        return result;
    };
    runtime::Coordinator coordinator(host.Services());
    Pump(coordinator, input, [&] { return coordinator.CurrentFrames().size() == 5 && unavailableScans >= 1; });
    Check(ForTile(coordinator.CurrentFrames(), L"one").size() == 5 && ForTile(coordinator.CurrentFrames(), L"two").empty() && host.Load().images.size() == 10,
        "one inaccessible new gallery folder cannot block another folder's usable photo queue");
    const auto scans = unavailableScans.load();
    for (int i = 0; i < 5; ++i) coordinator.Refresh(input);
    Check(unavailableScans == scans && ForTile(coordinator.CurrentFrames(), L"one").size() == 5,
        "an unavailable folder backs off while the successful folder keeps its pending frames");
}
}
int main() {
    try { TestExplicitInitializationAndCas(); TestPublicationAndSlotAcknowledgements(); TestRestartConflictAndUnavailableMode(); TestResumableInventoryAndShutdown(); TestManyTilesAndFolders(); TestUnavailableFolderIsolation();
        std::cout << checks << " gallery runtime checks passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL after " << checks << " runtime checks: " << error.what() << '\n'; return 1; }
}
