// Pure synthetic gallery fixtures: no filesystem, photos, clocks, Windows,
// pinning, transport, configuration mutation, or resident processes.
#include "../dependencies/gallery_engine.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace aip::gallery;
int checks = 0;
void Check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
Configuration Config(unsigned count = 2) {
    Configuration cfg; cfg.enabled = true; cfg.groupId = L"group1"; cfg.folder = L"synthetic inventory; never read";
    for (unsigned i = 0; i < count; ++i) cfg.tiles.push_back({L"tile" + std::to_wstring(i + 1), L"Gallery " + std::to_wstring(i + 1), true});
    return cfg;
}
std::vector<std::wstring> Images(unsigned count, unsigned first = 1) {
    std::vector<std::wstring> result;
    for (unsigned i = first; i < first + count; ++i) result.push_back(L"image_" + std::to_wstring(i));
    return result;
}
Ledger NewLedger() {
    Ledger result; std::wstring error;
    Check(CreateLedger(L"group1", L"epoch_1", true, result, error), "new fixture ledger has explicit no-history attestation"); return result;
}
std::string Wire(const Ledger& ledger) {
    std::string text; std::wstring error;
    Check(SerializeLedger(ledger, text, error), "fixture ledger serializes with validated ownership"); return text;
}
const TilePool& Pool(const PoolSnapshot& snapshot, const std::wstring& id) {
    const auto found = std::find_if(snapshot.pools.begin(), snapshot.pools.end(), [&](const auto& pool) { return pool.tileId == id; });
    if (found == snapshot.pools.end()) throw std::runtime_error("missing fixture pool");
    return *found;
}
const QueueWindow& Window(const QueueBatch& batch, const std::wstring& id) {
    const auto found = std::find_if(batch.windows.begin(), batch.windows.end(), [&](const auto& window) { return window.tileId == id; });
    if (found == batch.windows.end()) throw std::runtime_error("missing fixture window");
    return *found;
}
std::set<std::wstring> Due(const Configuration& cfg) { std::set<std::wstring> result; for (const auto& tile : cfg.tiles) result.insert(tile.id); return result; }
void CheckDisjoint(const PoolSnapshot& snapshot, const Ledger& ledger) {
    std::set<std::wstring> every;
    for (const auto& pool : snapshot.pools) for (const auto& id : pool.images) {
        Check(every.insert(id).second, "entire owner pools are disjoint, including images outside the five-slot queue");
        Check(ledger.images.at(id).owner == pool.tileId, "each available image retains its durable owner");
    }
}
void TestOwnershipAndMembership() {
    auto cfg = Config(); auto files = Images(16); const auto original = NewLedger();
    OwnershipPlan plan; std::wstring error;
    Check(ReconcileOwnership(cfg, files, original, plan, error) && plan.requiresLedgerCommit && plan.expectedRevision == 0 && plan.ledger.revision == 1,
        "initial ownership is proposed as a new durable revision without mutating its input");
    Check(original.images.empty() && plan.ledger.images.size() == 16 && Pool(plan.pools, L"tile1").images.size() == 8 && Pool(plan.pools, L"tile2").images.size() == 8,
        "simultaneously created tiles receive balanced disjoint pools");
    CheckDisjoint(plan.pools, plan.ledger);
    const auto saved = Wire(plan.ledger); const auto firstPools = plan.pools; const auto owned = plan.ledger;
    std::reverse(files.begin(), files.end()); std::reverse(cfg.tiles.begin(), cfg.tiles.end());
    Check(ReconcileOwnership(cfg, files, owned, plan, error) && !plan.requiresLedgerCommit && Wire(plan.ledger) == saved &&
        Pool(plan.pools, L"tile1").images == Pool(firstPools, L"tile1").images,
        "reordering tiles or discovered files changes neither reservations nor seeded queue order");
    cfg.seed = 99;
    Check(ReconcileOwnership(cfg, files, owned, plan, error) && Wire(plan.ledger) == saved,
        "shuffle changes order inside existing pools without moving ownership");
    CheckDisjoint(plan.pools, plan.ledger);
    cfg.order = Order::Sequential;
    Check(ReconcileOwnership(cfg, files, owned, plan, error) && Wire(plan.ledger) == saved,
        "sequential ordering preserves the same durable reservations");
    std::uint64_t ordinal = 0;
    for (const auto& id : Pool(plan.pools, L"tile1").images) { Check(owned.images.at(id).ordinal > ordinal, "sequential order follows durable append order"); ordinal = owned.images.at(id).ordinal; }
    cfg.tiles.push_back({L"tile3", L"New gallery", true});
    Check(ReconcileOwnership(cfg, files, owned, plan, error) && !plan.requiresLedgerCommit && Pool(plan.pools, L"tile3").images.empty(),
        "membership increase leaves a new tile empty rather than stealing any old reservation");
    auto more = Images(9, 100); files.insert(files.end(), more.begin(), more.end());
    Check(ReconcileOwnership(cfg, files, owned, plan, error) && plan.requiresLedgerCommit && Pool(plan.pools, L"tile3").images.size() >= 8,
        "new members can acquire only newly discovered unowned images");
    for (const auto& image : owned.images) Check(plan.ledger.images.at(image.first).owner == image.second.owner, "adding a member never silently rebalances existing reservations");
    auto grown = plan.ledger;
    const auto missing = Pool(plan.pools, L"tile1").images.front();
    files.erase(std::remove(files.begin(), files.end(), missing), files.end());
    Check(ReconcileOwnership(cfg, files, grown, plan, error) && !plan.requiresLedgerCommit && plan.ledger.images.count(missing) == 1 &&
        Pool(plan.pools, L"tile1").reservedCount > Pool(plan.pools, L"tile1").images.size(),
        "missing images retain ownership tombstones while disappearing from the usable pool");
    cfg.tiles.erase(std::remove_if(cfg.tiles.begin(), cfg.tiles.end(), [](const auto& tile) { return tile.id == L"tile1"; }), cfg.tiles.end());
    files.push_back(missing); files.push_back(L"new_after_removal");
    Check(ReconcileOwnership(cfg, files, grown, plan, error) && plan.ledger.images.at(missing).owner == L"tile1" &&
        plan.ledger.images.at(L"new_after_removal").owner != L"tile1" && plan.ledger.tiles.count(L"tile1") == 1,
        "removing a destination retains its reservations and cursor even when its missing image reappears");
    grown = plan.ledger; cfg.tiles.push_back({L"tile1", L"Returned gallery", true});
    Check(ReconcileOwnership(cfg, files, grown, plan, error) &&
        std::find(Pool(plan.pools, L"tile1").images.begin(), Pool(plan.pools, L"tile1").images.end(), missing) != Pool(plan.pools, L"tile1").images.end(),
        "restoring a historical TileID restores its original pool rather than reallocating it");
    CheckDisjoint(plan.pools, plan.ledger);
    cfg.tiles.back().enabled = false; files.push_back(L"new_while_paused");
    Check(ReconcileOwnership(cfg, files, plan.ledger, plan, error) && plan.ledger.images.at(L"new_while_paused").owner != L"tile1" &&
        plan.ledger.images.at(missing).owner == L"tile1", "pausing a tile neither releases old reservations nor allocates new files to that paused member");
    cfg.enabled = false; files.push_back(L"new_while_group_off");
    const auto beforeOff = Wire(plan.ledger);
    Check(ReconcileOwnership(cfg, files, plan.ledger, plan, error) && Wire(plan.ledger) == beforeOff &&
        plan.ledger.images.count(L"new_while_group_off") == 0, "disabled groups leave newly discovered images unowned and retain all historical ownership");
}
void TestQueuePlanningAndFailures() {
    const auto cfg = Config(); const auto files = Images(20); std::wstring error;
    OwnershipPlan ownership; const auto initial = NewLedger();
    Check(ReconcileOwnership(cfg, files, initial, ownership, error), "queue fixture reserves its pools first");
    auto durable = ownership.ledger; QueueBatch batch;
    Check(PrepareQueueBatch(cfg, ownership.pools, durable, {}, batch, error) && batch.windows.empty() && !batch.requiresLedgerCommit,
        "a scheduler with no due destinations produces no queue work or persistence churn");
    auto oneSlot = cfg; oneSlot.queueSize = 1;
    Check(PrepareQueueBatch(oneSlot, ownership.pools, durable, Due(oneSlot), batch, error) &&
        Window(batch, L"tile1").images.size() == 1 && Window(batch, L"tile2").images.size() == 1,
        "a smaller configured queue size selects only that many snapshots from each existing pool");
    Check(PrepareQueueBatch(cfg, ownership.pools, durable, Due(cfg), batch, error) && batch.windows.size() == 2 && batch.requiresLedgerCommit,
        "a due batch stages pending windows before any delivery is authorized");
    const auto first = Window(batch, L"tile1"), second = Window(batch, L"tile2");
    Check(first.images.size() == 5 && second.images.size() == 5 && first.ticket != second.ticket,
        "each destination receives at most five distinct snapshots with a unique delivery ticket");
    for (const auto& id : first.images) Check(std::find(second.images.begin(), second.images.end(), id) == second.images.end(), "queued snapshots cannot overlap across owner pools");
    Check(!ValidateWindowForPublication(durable, first, error), "failed pending-ledger persistence leaves delivery unauthorized");
    Check(!ValidateWindowForPublication(initial, first, error), "failed ownership-ledger persistence also forbids publishing images to a destination");
    durable = batch.ledger;
    Check(ValidateWindowForPublication(durable, first, error) && ValidateWindowForPublication(durable, second, error),
        "publication becomes valid only against the exact durable pending window");
    const auto reservedCount = durable.images.size(); const auto cursor = durable.tiles.at(L"tile1").cursor;
    Ledger after;
    Check(AcknowledgeDelivery(durable, first, false, after, error) && after.tiles.at(L"tile1").cursor == cursor &&
        after.images.size() == reservedCount && after.tiles.at(L"tile1").pending.ticket == 0,
        "partial or failed OS delivery clears its ticket without advancing or releasing reservations");
    const auto failure = after;
    Check(PrepareQueueBatch(cfg, ownership.pools, failure, {L"tile1"}, batch, error) && Window(batch, L"tile1").images == first.images &&
        Window(batch, L"tile1").ticket != first.ticket, "a failed window can be replanned without skipping its pool or accepting an old callback");
    durable = batch.ledger;
    Check(!AcknowledgeDelivery(durable, first, true, after, error), "late delivery callbacks cannot acknowledge a newer pending ticket");
    const auto retry = Window(batch, L"tile1");
    Check(AcknowledgeDelivery(durable, retry, true, after, error) && after.tiles.at(L"tile1").cursor == 5,
        "accepted submission advances by the prepared batch rather than guessing the visible fifth notification");
    Check(!ValidateWindowForPublication(after, retry, error), "already acknowledged tickets cannot be republished as current pending work");
    Check(PrepareQueueBatch(cfg, ownership.pools, after, {L"tile1"}, batch, error) && Window(batch, L"tile1").images != first.images,
        "the next scheduled batch advances inside the same owner's larger pool");
    durable = batch.ledger;
    const auto crashWindow = Window(batch, L"tile1");
    Ledger restarted; Check(ParseLedger(Wire(durable), restarted, error), "pending windows survive a serialized restart");
    Check(PrepareQueueBatch(cfg, ownership.pools, restarted, {L"tile1"}, batch, error) && !batch.requiresLedgerCommit &&
        Window(batch, L"tile1").retry && Window(batch, L"tile1").ticket == crashWindow.ticket && Window(batch, L"tile1").images == crashWindow.images,
        "a crash or failed acknowledgement save retries the same durable window without reallocating images");
    auto missing = files; missing.erase(std::remove(missing.begin(), missing.end(), crashWindow.images.front()), missing.end());
    Check(ReconcileOwnership(cfg, missing, restarted, ownership, error) &&
        PrepareQueueBatch(cfg, ownership.pools, restarted, {L"tile1"}, batch, error) &&
        Window(batch, L"tile1").status == WindowStatus::PendingImagesUnavailable &&
        !ValidateWindowForPublication(restarted, Window(batch, L"tile1"), error),
        "a retained pending window with a missing usable asset needs failure acknowledgement rather than unsafe publication");
    Check(AcknowledgeDelivery(restarted, Window(batch, L"tile1"), false, after, error) &&
        PrepareQueueBatch(cfg, ownership.pools, after, {L"tile1"}, batch, error) &&
        std::find(Window(batch, L"tile1").images.begin(), Window(batch, L"tile1").images.end(), crashWindow.images.front()) == Window(batch, L"tile1").images.end(),
        "retry after a missing-asset failure selects remaining owned assets while retaining the missing identity tombstone");
    auto crossGroup = Window(batch, L"tile1"); crossGroup.groupId = L"other";
    Check(!ValidateWindowForPublication(batch.ledger, crossGroup, error) && !AcknowledgeDelivery(batch.ledger, crossGroup, true, after, error),
        "equal-looking tickets from another gallery group cannot publish or advance this ledger");
    auto changed = cfg; changed.seed++;
    Check(!PrepareQueueBatch(changed, ownership.pools, after, Due(changed), batch, error), "order changes require a refreshed cached pool snapshot");
    auto small = Config(3); OwnershipPlan few;
    Check(ReconcileOwnership(small, {L"only_image"}, initial, few, error) && PrepareQueueBatch(small, few.pools, few.ledger, Due(small), batch, error),
        "too few images remain a valid explicitly empty-pool layout");
    unsigned ready = 0, empty = 0;
    for (const auto& window : batch.windows) { if (window.status == WindowStatus::Ready) { ++ready; Check(window.images.size() == 1, "a short pool never duplicates an image to fill five queue slots"); } else if (window.status == WindowStatus::EmptyPool) ++empty; }
    Check(ready == 1 && empty == 2, "new tiles without available unowned files expose friendly empty status instead of sharing images");
}
void TestPersistenceAndReset() {
    auto cfg = Config(1); const auto initial = NewLedger(); std::wstring error;
    OwnershipPlan plan;
    Check(ReconcileOwnership(cfg, {L"photo_\u6f22\u5b57_\U0001f642", L"image2"}, initial, plan, error), "opaque Unicode image IDs are accepted without filesystem interpretation");
    const auto wire = Wire(plan.ledger); Ledger copy;
    Check(ParseLedger(wire, copy, error) && Wire(copy) == wire, "strict ASCII ledger serialization round-trips Unicode identities and stable ownership");
    const auto unchanged = wire;
    for (const auto& malformed : std::vector<std::string>{"", "AIPGALLERY 2\n", wire + "extra\n", wire.substr(0, wire.size() - 1),
        "AIPGALLERY 1\nG invalid\n", "AIPGALLERY 1\nG 0067\nE 0065\nV 0 1 1\nI 65537\n",
        "AIPGALLERY 1\nG 0067\nE 0065\nV 0 1 1\nI 0\nT 1025\n",
        "AIPGALLERY 1\nG 0067\nE d800\nV 0 1 1\nI 0\nT 0\nEND\n"}) {
        Check(!ParseLedger(malformed, copy, error) && Wire(copy) == unchanged && !error.empty(),
            "malformed unsupported truncated oversized or trailing ledger data fails without clearing previous ownership");
    }
    auto corrupt = plan.ledger; corrupt.images.begin()->second.owner = L"not_in_ledger";
    Check(!ValidateLedger(corrupt, error), "a reservation cannot name a nonexistent owner record");
    corrupt = plan.ledger; corrupt.images.begin()->second.ordinal = corrupt.images.rbegin()->second.ordinal;
    Check(!ValidateLedger(corrupt, error), "duplicate durable ordinals cannot masquerade as an append-only ledger");
    Check(!CreateLedger(L"group1", L"new_epoch", false, copy, error) && Wire(copy) == unchanged,
        "missing history never authorizes creating an empty replacement ledger");
    Check(!ResetLedger(plan.ledger, L"new_epoch", false, copy, error) && Wire(copy) == unchanged,
        "reset without host confirmation of all historical pins and queues changes nothing");
    Check(!ResetLedger(plan.ledger, plan.ledger.epochId, true, copy, error), "reset cannot reuse an epoch and accept old delivery tickets");
    QueueBatch pending;
    Check(PrepareQueueBatch(cfg, plan.pools, plan.ledger, Due(cfg), pending, error), "retirement fixture has an old delivery ticket");
    Check(ResetLedger(plan.ledger, L"new_epoch", true, copy, error) && copy.images.empty() && copy.tiles.empty() && copy.epochId == L"new_epoch",
        "explicit retirement confirmation permits a new empty ownership epoch");
    Ledger acknowledged;
    Check(!ValidateWindowForPublication(copy, pending.windows.front(), error) && !AcknowledgeDelivery(copy, pending.windows.front(), true, acknowledged, error),
        "old publication and acknowledgement tickets remain invalid after a confirmed reset");
    auto wrongGroup = cfg; wrongGroup.groupId = L"another_group";
    Check(!ReconcileOwnership(wrongGroup, Images(3), plan.ledger, plan, error), "changing GroupId cannot silently bypass historical reservations");
}
void TestTypedConfigurationAndDefenses() {
    std::map<std::pair<std::wstring, std::wstring>, std::wstring> values;
    const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        const auto found = values.find({section, key}); return found == values.end() ? std::wstring(fallback) : found->second;
    };
    Configuration cfg; std::wstring error;
    Check(ReadConfiguration(read, cfg, error) && !cfg.enabled && cfg.queueSize == 5 && cfg.refreshSeconds == 900 && cfg.order == Order::Shuffle && cfg.tiles.size() == 2,
        "unconfigured galleries default off with five snapshots seeded shuffle and a fifteen-minute cadence");
    values[{L"Gallery", L"Enabled"}] = L"yes"; values[{L"Gallery", L"GroupId"}] = L"group1";
    values[{L"Gallery.Tile.1", L"Id"}] = L"tile1"; values[{L"Gallery.Tile.2", L"Id"}] = L"tile2";
    Check(ReadConfiguration(read, cfg, error) && cfg.enabled, "explicit stable group and destination IDs enable a typed gallery configuration");
    Check(cfg.tiles.front().appearance.background == L"Transparent" && cfg.tiles.front().appearance.branding == L"Name",
        "gallery appearance defaults match source tiles without colored plates or corner logos");
    values[{L"Gallery.Tile.1", L"Branding"}] = L"none"; values[{L"Gallery.Tile.1", L"Background"}] = L"color"; values[{L"Gallery.Tile.1", L"BackgroundColor"}] = L"abcdef";
    Check(ReadConfiguration(read, cfg, error) && cfg.tiles.front().appearance.branding == L"None" && cfg.tiles.front().appearance.backgroundColor == L"#ABCDEF",
        "gallery appearance uses the shared canonical typed palette and branding policy");
    values[{L"Gallery.Tile.1", L"Branding"}] = L"invalid";
    Check(!ReadConfiguration(read, cfg, error) && cfg.tiles.front().appearance.branding == L"None", "invalid gallery appearance rejects without partial configuration publication");
    values.erase({L"Gallery.Tile.1", L"Branding"}); values.erase({L"Gallery.Tile.1", L"Background"}); values.erase({L"Gallery.Tile.1", L"BackgroundColor"});
    Check(ReadConfiguration(read, cfg, error), "appearance fixtures restore canonical defaults");
    const auto saved = cfg;
    for (const auto& invalid : std::vector<std::pair<std::wstring, std::wstring>>{{L"QueueSize", L"6"}, {L"TileCount", L"9"}, {L"RefreshSeconds", L"1"},
        {L"Seed", L"18446744073709551616"}, {L"Seed", L"-1"}, {L"Order", L"random_device"}, {L"Fit", L"guess"}, {L"GroupId", L"bad-id"}, {L"Enabled", L"maybe"}}) {
        values[{L"Gallery", invalid.first}] = invalid.second;
        Check(!ReadConfiguration(read, cfg, error) && cfg.groupId == saved.groupId && cfg.tiles.size() == saved.tiles.size(), "an invalid gallery option rejects the whole read without publishing a partial configuration");
        values.erase({L"Gallery", invalid.first});
        values[{L"Gallery", L"Enabled"}] = L"1"; values[{L"Gallery", L"GroupId"}] = L"group1";
    }
    values[{L"Gallery.Tile.2", L"Id"}] = L"TILE1";
    Check(!ReadConfiguration(read, cfg, error), "case-only destination aliases cannot create two Windows gallery IDs");
    auto tooMany = Config(9); Check(!ValidateConfiguration(tooMany, error), "one gallery group cannot exceed eight tiles");
    auto value = std::wstring(L" sequential ");
    Check(NormalizeSetting(L"gallery", L"Order", value, error) && value == L"Sequential", "typed order names canonicalize without touching source data");
    Check(!KnownGallerySection(L"Gallery.Tile.09") && !KnownGallerySection(L"Gallery.Tile.9") && KnownGallerySection(L"Gallery.Tile.8"), "numbered gallery sections have a bounded unambiguous grammar");
    Check(NativeTileId(L"group1", L"tile1") == L"AipGallery_group1.tile1" && NativeTileId(std::wstring(32, L'g'), std::wstring(20, L't')).size() == 64 &&
        NativeTileId(L"bad-id", L"tile1").empty(), "stable bounded IDs map to one native destination without path or punctuation ambiguity");
    Check(MaxCombinedTileIdLength == 52 && NativeTileId(std::wstring(32, L'g'), std::wstring(21, L't')).empty() &&
        NativeTileId(std::wstring(32, L'g'), std::wstring(32, L't')).empty(), "native IDs reject combined components beyond the exact 64-character Windows bound");
    Check(NativeTileId(L"a_b", L"c") != NativeTileId(L"a", L"b_c") && NativeTileId(L"a_b", L"c") == L"AipGallery_a_b.c" &&
        NativeTileId(L"a", L"b_c") == L"AipGallery_a.b_c", "period boundary prevents collisions when either component contains underscores");
    auto boundary = Config(1); boundary.groupId = std::wstring(32, L'g'); boundary.tiles[0].id = std::wstring(20, L't');
    Check(ValidateConfiguration(boundary, error), "full configuration accepts exactly 64-character native IDs");
    boundary.tiles[0].id.push_back(L't');
    Check(!ValidateConfiguration(boundary, error), "full configuration rejects individually legal components whose native ID is too long");
    boundary.enabled = false;
    Check(!ValidateConfiguration(boundary, error), "inactive configuration cannot retain an impossible native destination identity");
    Ledger overlong; Check(CreateLedger(std::wstring(32, L'g'), L"epoch", true, overlong, error), "long legal group ID can initialize before member IDs exist");
    overlong.tiles.emplace(std::wstring(21, L't'), TileLedger{});
    Check(!ValidateLedger(overlong, error), "historical ledger owners also obey the native destination length limit");
    Check(!NormalizeSetting(L"Gallery.Tile.1", L"Folder", value, error), "per-tile settings cannot implicitly change the group's inventory source");
    const auto initial = NewLedger(); OwnershipPlan plan; auto normal = Config();
    Check(ReconcileOwnership(normal, Images(12), initial, plan, error), "defensive fixture reserves a valid inventory");
    const auto originalWire = Wire(plan.ledger); OwnershipPlan sentinel = plan;
    Ledger missing;
    Check(!ReconcileOwnership(normal, Images(12), missing, sentinel, error) && Wire(sentinel.ledger) == originalWire,
        "a missing ledger fails closed instead of allocating a second set of owners");
    Check(!ReconcileOwnership(normal, {std::wstring(MaxIdentityUnits + 1, L'x')}, plan.ledger, sentinel, error) && Wire(sentinel.ledger) == originalWire,
        "invalid opaque identity input cannot partially change a proposed ownership plan");
    std::vector<std::wstring> excessive(MaxImages + 1, L"same_alias");
    Check(!ReconcileOwnership(normal, excessive, plan.ledger, sentinel, error), "raw inventory input is bounded even when it repeats one identity");
    auto aliasFiles = Images(12); aliasFiles.push_back(aliasFiles.front());
    Check(ReconcileOwnership(normal, aliasFiles, plan.ledger, sentinel, error) && Wire(sentinel.ledger) == originalWire,
        "host-canonicalized aliases with the same identity cannot receive another owner");
    auto badPools = plan.pools;
    badPools.pools[0].images[0] = badPools.pools[1].images[0]; QueueBatch batch;
    Check(!PrepareQueueBatch(normal, badPools, plan.ledger, Due(normal), batch, error), "even a corrupted cached pool cannot publish another destination's owned image");
    Check(!PrepareQueueBatch(normal, plan.pools, plan.ledger, {L"unknown_tile"}, batch, error), "unknown scheduled destinations reject without inventing a new owner");
    auto newer = Images(13); Check(ReconcileOwnership(normal, newer, plan.ledger, sentinel, error), "new unowned inventory creates a later ownership generation");
    Check(!PrepareQueueBatch(normal, plan.pools, sentinel.ledger, Due(normal), batch, error), "an older cached pool cannot schedule against newer ownership reservations");
}
}
int main() {
    try {
        TestOwnershipAndMembership(); TestQueuePlanningAndFailures(); TestPersistenceAndReset(); TestTypedConfigurationAndDefenses();
        std::cout << checks << " gallery checks passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL after " << checks << " gallery checks: " << error.what() << '\n'; return 1; }
}
