#pragma once
// Bounded recovery record for two atomic files. It contains gallery settings
// and ownership only; account settings and the rest of the INI stay outside.
#include "gallery_engine.h"
namespace aip { namespace gallery { namespace transaction {
inline constexpr std::size_t MaxConfigurationBytes = 8u * 1024u * 1024u;
inline constexpr std::size_t MaxJournalBytes = MaxLedgerBytes + 2 * MaxConfigurationBytes + 2048;
inline std::wstring Path(const std::wstring& profile) { return profile + L".gallery-transaction"; }
struct Record { std::string before, after, ledger; std::wstring epoch; };
inline bool Validate(const Record& record, std::wstring& error) {
    Ledger old, reset;
    if (record.before.empty() || record.after.empty() || record.before.size() > MaxConfigurationBytes || record.after.size() > MaxConfigurationBytes ||
        !ParseLedger(record.ledger, old, error) || !ResetLedger(old, record.epoch, true, reset, error)) {
        if (error.empty()) error = L"The gallery recovery record is incomplete or invalid.";
        return false;
    }
    error.clear(); return true;
}
inline bool Serialize(const Record& record, std::string& result, std::wstring& error) {
    if (!Validate(record, error)) return false;
    std::string output = "AIPGALLERYTX1\n";
    for (const auto* part : {&record.before, &record.after, &record.ledger}) output += std::to_string(part->size()) + "\n" + *part;
    const auto epoch = detail::Hex(record.epoch); output += std::to_string(epoch.size()) + "\n" + epoch;
    result = std::move(output); return true;
}
inline bool Parse(const std::string& bytes, Record& result, std::wstring& error) {
    const std::string magic = "AIPGALLERYTX1\n"; Record record; std::size_t offset = magic.size();
    if (bytes.size() > MaxJournalBytes || bytes.compare(0, magic.size(), magic)) { error = L"The gallery recovery record has an unsupported format."; return false; }
    const auto take = [&](std::string& part, std::size_t maximum) {
        const auto end = bytes.find('\n', offset);
        if (end == std::string::npos || end == offset || end - offset > 20) return false;
        std::size_t length = 0;
        for (auto i = offset; i < end; ++i) {
            const auto c = bytes[i]; if (c < '0' || c > '9' || length > maximum / 10 ||
                (length == maximum / 10 && static_cast<std::size_t>(c - '0') > maximum % 10)) return false;
            length = length * 10 + static_cast<std::size_t>(c - '0');
        }
        offset = end + 1; if (length > bytes.size() - offset) return false;
        part.assign(bytes, offset, length); offset += length; return true;
    };
    std::string epoch;
    if (!take(record.before, MaxConfigurationBytes) || !take(record.after, MaxConfigurationBytes) || !take(record.ledger, MaxLedgerBytes) ||
        !take(epoch, MaxIdentityUnits * 4) || offset != bytes.size() || !detail::Unhex(epoch, record.epoch) || !Validate(record, error)) {
        if (error.empty()) error = L"The gallery recovery record is truncated or invalid.";
        return false;
    }
    result = std::move(record); return true;
}
enum class Recovery { Cancel, Finish, Restore, Conflict };
inline Recovery Decide(const Record& record, const std::string& currentConfiguration, const std::string& currentLedger, std::wstring& error) {
    Ledger previous, reset; std::string resetBytes;
    if (!Validate(record, error) || !ParseLedger(record.ledger, previous, error) || !ResetLedger(previous, record.epoch, true, reset, error) ||
        !SerializeLedger(reset, resetBytes, error)) return Recovery::Conflict;
    if (currentConfiguration == record.before && currentLedger == record.ledger) { error.clear(); return Recovery::Cancel; }
    if (currentLedger == resetBytes) {
        if (currentConfiguration == record.after) { error.clear(); return Recovery::Finish; }
        if (currentConfiguration == record.before) { error.clear(); return Recovery::Restore; }
    }
    error = L"Gallery settings or ownership changed outside the interrupted transaction. Recovery kept both files and paused gallery publication; restore the matching INI/ledger or review the recovery record.";
    return Recovery::Conflict;
}
} } }
