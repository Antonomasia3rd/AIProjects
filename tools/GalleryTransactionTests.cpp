// Portable recovery-record tests. No Windows headers, filesystem, clocks,
// providers, queues, configuration writes or process APIs are used.
#include "../dependencies/gallery_transaction.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace aip::gallery;
using namespace aip::gallery::transaction;
int checks = 0;
void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
std::string LedgerBytes(const Ledger& ledger) {
    std::string bytes; std::wstring error;
    Check(SerializeLedger(ledger, bytes, error), "synthetic ledger serializes with validated ownership"); return bytes;
}
Ledger OriginalLedger() {
    Ledger ledger; std::wstring error;
    Check(CreateLedger(L"group_1", L"old_epoch", true, ledger, error), "synthetic history starts with explicit no-history attestation");
    ledger.revision = 7; ledger.nextOrdinal = 3; ledger.nextTicket = 2;
    ledger.images.emplace(L"photo_\u2603", Reservation{L"tile_1", 1});
    ledger.images.emplace(L"photo_2", Reservation{L"tile_2", 2});
    ledger.tiles.emplace(L"tile_1", TileLedger{}); ledger.tiles.emplace(L"tile_2", TileLedger{});
    ledger.tiles[L"tile_1"].pending = {1, 1, {L"photo_\u2603"}};
    Check(ValidateLedger(ledger, error), "synthetic old ledger retains owners, ordinals and an uncommitted queue window");
    return ledger;
}
Record Fixture() {
    return {"[Gallery]\r\nGroupId=group_1\r\nTileCount=2\r\n", "[Gallery]\r\nGroupId=group_1\r\nTileCount=3\r\n", LedgerBytes(OriginalLedger()), L"new_epoch"};
}
std::string Part(const std::string& value) { return std::to_string(value.size()) + "\n" + value; }
std::string Raw(const Record& record, const std::string& epochHex) {
    return "AIPGALLERYTX1\n" + Part(record.before) + Part(record.after) + Part(record.ledger) + Part(epochHex);
}
void TestCodec() {
    auto record = Fixture(); std::wstring error; std::string wire;
    record.before += "Name=Snowman \xE2\x98\x83\r\n";
    Check(Serialize(record, wire, error) && error.empty(), "bounded version-one journal encodes opaque UTF-8 gallery settings and full ownership");
    Record parsed;
    Check(Parse(wire, parsed, error) && parsed.before == record.before && parsed.after == record.after &&
        parsed.ledger == record.ledger && parsed.epoch == record.epoch, "journal round trip preserves exact endpoint and ledger bytes");
    Check(Path(L"profile.ini") == L"profile.ini.gallery-transaction", "recovery record belongs beside its selected profile");
    const auto reject = [&](const std::string& input) {
        auto preserved = record; error.clear();
        Check(!Parse(input, preserved, error) && !error.empty() && preserved.before == record.before &&
            preserved.after == record.after && preserved.ledger == record.ledger && preserved.epoch == record.epoch,
            "invalid record reports an error without partially publishing parsed state");
    };
    for (std::size_t length = 0; length < wire.size(); ++length) reject(wire.substr(0, length));
    reject("AIPGALLERYTX2\n"); reject("AIPGALLERYTX0\n"); reject("unsupported\n");
    for (const auto* length : {"", "-1", "+1", "1x", "999999999999999999999", "18446744073709551615"})
        reject(std::string("AIPGALLERYTX1\n") + length + "\nx");
    reject("AIPGALLERYTX1\n" + std::to_string(MaxConfigurationBytes + 1) + "\nx");
    reject("AIPGALLERYTX1\n" + Part("x") + Part("y") + std::to_string(MaxLedgerBytes + 1) + "\nx");
    reject("AIPGALLERYTX1\n" + Part("x") + Part("y") + Part(record.ledger) + std::to_string(MaxIdentityUnits * 4 + 1) + "\nx");
    reject(Raw(record, "")); reject(Raw(record, "zzzz")); reject(Raw(record, "0061f"));
    reject(wire + "trailing"); reject(wire + std::string(1, '\0'));
    auto invalid = record; invalid.before.clear(); std::string unchanged = "keep";
    Check(!Serialize(invalid, unchanged, error) && unchanged == "keep", "empty configuration endpoint cannot replace encoded output");
    invalid = record; invalid.epoch = L"old_epoch";
    Check(!Serialize(invalid, unchanged, error), "a reset must use a different epoch");
    invalid = record; invalid.ledger = "corrupt ownership";
    Check(!Serialize(invalid, unchanged, error), "a corrupt old ledger cannot authorize a recovery record");
    invalid = record; invalid.before.assign(MaxConfigurationBytes + 1, 'x');
    Check(!Serialize(invalid, unchanged, error), "oversized configuration is rejected before journal publication");
    invalid.before.resize(MaxConfigurationBytes);
    Check(Validate(invalid, error), "the exact configuration byte limit remains supported");
    Check(MaxJournalBytes >= MaxLedgerBytes + 2 * MaxConfigurationBytes,
        "aggregate journal limit includes both bounded endpoint records and ownership");
}
void TestRecoveryEndpoints() {
    const auto record = Fixture(); std::wstring error; Ledger old, reset;
    Check(ParseLedger(record.ledger, old, error) && ResetLedger(old, record.epoch, true, reset, error), "recovery fixture derives its exact unused reset ledger");
    const auto fresh = LedgerBytes(reset);
    Check(Decide(record, record.before, record.ledger, error) == Recovery::Cancel && error.empty(),
        "before configuration plus original ledger cancels an unstarted or partially cleared transaction");
    Check(Decide(record, record.before, fresh, error) == Recovery::Restore && error.empty(),
        "old configuration plus exact unused reset requires restoration of original ownership");
    Check(Decide(record, record.after, fresh, error) == Recovery::Finish && error.empty(),
        "committed new configuration plus exact reset finishes cleanup");
    Check(Decide(record, record.after, record.ledger, error) == Recovery::Conflict && !error.empty(),
        "a new configuration with an old ledger is not guessed to be committed");
    Check(Decide(record, record.before + "ExternalEdit=1\n", fresh, error) == Recovery::Conflict,
        "external configuration edits preserve both files and block automatic recovery");
    auto newerOld = old; ++newerOld.revision;
    Check(Decide(record, record.before, LedgerBytes(newerOld), error) == Recovery::Conflict,
        "a cooperating writer's newer ownership revision is never overwritten");
    auto otherGroup = reset; otherGroup.groupId = L"other_group";
    Check(Decide(record, record.after, LedgerBytes(otherGroup), error) == Recovery::Conflict,
        "a ledger from a different group cannot finish or roll back this transaction");
    auto otherEpoch = reset; otherEpoch.epochId = L"external_epoch";
    Check(Decide(record, record.after, LedgerBytes(otherEpoch), error) == Recovery::Conflict,
        "an unrelated reset epoch is preserved instead of replaying old ownership");
    auto allocated = reset; allocated.revision = 1; allocated.nextOrdinal = 2;
    allocated.tiles.emplace(L"tile_3", TileLedger{}); allocated.images.emplace(L"new_photo", Reservation{L"tile_3", 1});
    Check(Decide(record, record.after, LedgerBytes(allocated), error) == Recovery::Conflict,
        "unexpected allocation while journal-fenced is never treated as the unused reset endpoint");
    auto unchanged = record; unchanged.after = unchanged.before;
    Check(Decide(unchanged, unchanged.before, unchanged.ledger, error) == Recovery::Cancel,
        "unchanged-settings redistribution remains cancellable before resetting");
    Check(Decide(unchanged, unchanged.before, fresh, error) == Recovery::Finish && error.empty(),
        "unchanged-settings redistribution finishes an already requested exact reset without ambiguity");
    auto invalid = record; invalid.epoch = L"old_epoch";
    Check(Decide(invalid, record.before, record.ledger, error) == Recovery::Conflict,
        "an invalid intent cannot authorize rollback even when current old bytes happen to match");
}
}
int main() {
    try { TestCodec(); TestRecoveryEndpoints(); std::cout << checks << " gallery transaction checks passed; no files or Windows APIs used\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "After " << checks << " checks: " << error.what() << '\n'; return 1; }
}
