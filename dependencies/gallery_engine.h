#pragma once
// Pure gallery ownership and queue planning. No filesystem, clock, Windows,
// decoding, random device, pinning, transport, or configuration writer lives here.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "tile_appearance.h"

namespace aip { namespace gallery {
inline constexpr std::size_t MaxTiles = 1024, MaxQueue = 5, MaxImages = 65536, MaxHistoricalTiles = 65536;
inline constexpr std::size_t MaxIdentityUnits = 256, MaxLedgerBytes = 64u * 1024u * 1024u;
inline constexpr wchar_t NativeTilePrefix[] = L"AipGallery_";
inline constexpr std::size_t NativeTileMaxLength = 64;
inline constexpr std::size_t MaxCombinedTileIdLength = NativeTileMaxLength - (sizeof(NativeTilePrefix) / sizeof(wchar_t) - 1) - 1;
enum class Order { Sequential, Shuffle };
enum class Fit { Fill, Fit };
struct TileConfiguration {
    std::wstring id, name; bool enabled = true; aip::content::TileAppearance appearance{};
    std::wstring folder, galleryName; // Empty folder inherits the legacy group folder.
};
struct Configuration {
    bool enabled = false;
    std::wstring groupId, folder;
    unsigned queueSize = 5, refreshSeconds = 900;
    Order order = Order::Shuffle;
    Fit fit = Fit::Fill;
    std::uint64_t seed = 1;
    std::vector<TileConfiguration> tiles;
};
inline const std::wstring& EffectiveFolder(const Configuration& cfg, const TileConfiguration& tile) { return tile.folder.empty() ? cfg.folder : tile.folder; }
namespace detail {
inline std::wstring Trim(const std::wstring& value) {
    const auto first = value.find_first_not_of(L" \t\r\n"), last = value.find_last_not_of(L" \t\r\n");
    return first == std::wstring::npos ? std::wstring() : value.substr(first, last - first + 1);
}
inline std::wstring Lower(std::wstring value) { for (auto& c : value) if (c >= L'A' && c <= L'Z') c += L'a' - L'A'; return value; }
inline bool UInt(const std::wstring& value, std::uint64_t maximum, std::uint64_t& result) {
    if (value.empty()) return false;
    std::uint64_t parsed = 0;
    for (const auto c : value) {
        if (c < L'0' || c > L'9' || parsed > maximum / 10 ||
            (parsed == maximum / 10 && static_cast<std::uint64_t>(c - L'0') > maximum % 10)) return false;
        parsed = parsed * 10 + static_cast<std::uint64_t>(c - L'0');
    }
    result = parsed; return true;
}
inline bool Bool(const std::wstring& value, bool& result) {
    const auto text = Lower(Trim(value));
    if (text == L"1" || text == L"true" || text == L"yes" || text == L"on" || text == L"enabled") result = true;
    else if (text == L"0" || text == L"false" || text == L"no" || text == L"off" || text == L"disabled") result = false;
    else return false;
    return true;
}
// Canonical UTF-16 units keep the wire format/hash stable with either 16-bit
// Windows wchar_t or 32-bit wchar_t in portable tests.
inline bool Units(const std::wstring& value, std::vector<std::uint16_t>& result, std::size_t maximum) {
    result.clear();
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto c = static_cast<std::uint32_t>(value[i]);
        if (c < 0x20 || c == 0x7f || c > 0x10ffff) return false;
        if constexpr (sizeof(wchar_t) == 2) {
            if (c >= 0xd800 && c <= 0xdbff) {
                if (++i == value.size()) return false;
                const auto low = static_cast<std::uint32_t>(value[i]);
                if (low < 0xdc00 || low > 0xdfff) return false;
                result.push_back(static_cast<std::uint16_t>(c)); result.push_back(static_cast<std::uint16_t>(low));
            } else { if (c >= 0xdc00 && c <= 0xdfff) return false; result.push_back(static_cast<std::uint16_t>(c)); }
        } else {
            if (c >= 0xd800 && c <= 0xdfff) return false;
            if (c > 0xffff) { c -= 0x10000; result.push_back(static_cast<std::uint16_t>(0xd800 + (c >> 10))); result.push_back(static_cast<std::uint16_t>(0xdc00 + (c & 1023))); }
            else result.push_back(static_cast<std::uint16_t>(c));
        }
        if (result.size() > maximum) return false;
    }
    return true;
}
inline bool Text(const std::wstring& value, std::size_t maximum, bool empty = true) {
    std::vector<std::uint16_t> units; return (empty || !value.empty()) && Units(value, units, maximum);
}
inline std::string Hex(const std::wstring& value) {
    static const char digits[] = "0123456789abcdef";
    std::vector<std::uint16_t> units; if (!Units(value, units, MaxIdentityUnits)) return {};
    std::string result;
    for (const auto c : units) for (int shift : {12, 8, 4, 0}) result.push_back(digits[(c >> shift) & 15]);
    return result;
}
inline bool Unhex(const std::string& value, std::wstring& result) {
    if (value.empty() || value.size() % 4 || value.size() > MaxIdentityUnits * 4) return false;
    std::vector<std::uint16_t> units;
    for (std::size_t i = 0; i < value.size(); i += 4) {
        unsigned code = 0;
        for (std::size_t j = 0; j < 4; ++j) {
            const char c = value[i + j];
            const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0) return false;
            code = code * 16 + static_cast<unsigned>(digit);
        }
        units.push_back(static_cast<std::uint16_t>(code));
    }
    std::wstring decoded;
    for (std::size_t i = 0; i < units.size(); ++i) {
        std::uint32_t c = units[i];
        if (c < 0x20 || c == 0x7f || (c >= 0xdc00 && c <= 0xdfff)) return false;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == units.size() || units[i] < 0xdc00 || units[i] > 0xdfff) return false;
            if constexpr (sizeof(wchar_t) == 2) { decoded.push_back(static_cast<wchar_t>(c)); decoded.push_back(static_cast<wchar_t>(units[i])); }
            else decoded.push_back(static_cast<wchar_t>(0x10000 + ((c - 0xd800) << 10) + units[i] - 0xdc00));
        } else decoded.push_back(static_cast<wchar_t>(c));
    }
    result = std::move(decoded); return true;
}
inline std::uint64_t Rank(const std::wstring& id, std::uint64_t seed) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char c : Hex(id)) { hash ^= c; hash *= 1099511628211ull; }
    hash += seed + 0x9e3779b97f4a7c15ull;
    hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ull;
    hash = (hash ^ (hash >> 27)) * 0x94d049bb133111ebull;
    return hash ^ (hash >> 31);
}
} // namespace detail
inline bool StableTileId(const std::wstring& value) {
    return !value.empty() && value.size() <= 32 && std::all_of(value.begin(), value.end(), [](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_';
    });
}
inline bool OpaqueImageId(const std::wstring& value) { return detail::Text(value, MaxIdentityUnits, false); }
inline std::wstring NativeTileId(const std::wstring& groupId, const std::wstring& tileId) {
    if (!StableTileId(groupId) || !StableTileId(tileId) || groupId.size() + tileId.size() > MaxCombinedTileIdLength) return {};
    // IDs exclude periods, so this boundary is unambiguous even when either
    // component contains underscores. Windows SecondaryTile IDs allow <=64.
    return std::wstring(NativeTilePrefix) + groupId + L"." + tileId;
}
inline int TileSectionNumber(const std::wstring& section) {
    const auto text = detail::Lower(detail::Trim(section)); const std::wstring prefix = L"gallery.tile.";
    if (text.compare(0, prefix.size(), prefix) != 0) return 0;
    std::uint64_t index = 0;
    if (!detail::UInt(text.substr(prefix.size()), MaxTiles, index) || !index || text.substr(prefix.size()) != std::to_wstring(index)) return 0;
    return static_cast<int>(index);
}
inline bool KnownGallerySection(const std::wstring& section) { return detail::Lower(detail::Trim(section)) == L"gallery" || TileSectionNumber(section) != 0; }
inline bool NormalizeSetting(const std::wstring& section, const std::wstring& key, std::wstring& value, std::wstring& error) {
    error.clear(); const bool root = detail::Lower(detail::Trim(section)) == L"gallery";
    const int tile = TileSectionNumber(section); const auto name = detail::Lower(detail::Trim(key));
    if (!root && !tile) { error = L"Unknown gallery section."; return false; }
    auto normalized = detail::Trim(value); std::uint64_t number = 0; bool enabled = false;
    if (name == L"enabled") { if (!detail::Bool(normalized, enabled)) { error = L"Gallery Enabled must be Boolean."; return false; } normalized = enabled ? L"1" : L"0"; }
    else if ((root && name == L"groupid") || (tile && name == L"id")) {
        if (!normalized.empty() && !StableTileId(normalized)) { error = L"Gallery IDs require 1-32 ASCII letters, digits, or underscores."; return false; }
    } else if (tile && (name == L"background" || name == L"branding" || name == L"backgroundcolor")) {
        return aip::content::NormalizeTileAppearanceSetting(key, value, error);
    } else if (root && (name == L"tilecount" || name == L"queuesize" || name == L"refreshseconds" || name == L"seed")) {
        const auto maximum = name == L"tilecount" ? MaxTiles : name == L"queuesize" ? MaxQueue : name == L"refreshseconds" ? 86400ull : (std::numeric_limits<std::uint64_t>::max)();
        const auto minimum = name == L"seed" ? 0ull : name == L"refreshseconds" ? 60ull : 1ull;
        if (!detail::UInt(normalized, maximum, number) || number < minimum) { error = L"Gallery numeric setting is outside its supported range."; return false; }
        normalized = std::to_wstring(number);
    } else if (root && name == L"order") {
        const auto order = detail::Lower(normalized);
        if (order != L"shuffle" && order != L"sequential") { error = L"Gallery Order must be Shuffle or Sequential."; return false; }
        normalized = order == L"shuffle" ? L"Shuffle" : L"Sequential";
    } else if (root && name == L"fit") {
        const auto fit = detail::Lower(normalized);
        if (fit != L"fill" && fit != L"fit") { error = L"Gallery Fit must be Fill or Fit."; return false; }
        normalized = fit == L"fill" ? L"Fill" : L"Fit";
    } else if (name == L"folder") {
        if (!detail::Text(value, 32768)) { error = L"Gallery Folder is invalid single-line text."; return false; }
        normalized = value;
    } else if (tile && name == L"galleryname") {
        if (!detail::Text(value, 256)) { error = L"Gallery name is invalid single-line text."; return false; }
        normalized = value;
    } else if (tile && name == L"name") {
        if (normalized.empty() || !detail::Text(value, 256)) { error = L"Gallery tile Name must be nonempty single-line text."; return false; }
        normalized = value;
    } else { error = L"Unknown gallery setting."; return false; }
    value = std::move(normalized); return true;
}
inline bool ValidateConfiguration(const Configuration& cfg, std::wstring& error) {
    error.clear();
    if ((!cfg.groupId.empty() && !StableTileId(cfg.groupId)) || (cfg.enabled && cfg.groupId.empty()) ||
        cfg.tiles.size() > MaxTiles || (cfg.enabled && cfg.tiles.empty()) || cfg.queueSize < 1 || cfg.queueSize > MaxQueue ||
        cfg.refreshSeconds < 60 || cfg.refreshSeconds > 86400 || !detail::Text(cfg.folder, 32768) ||
        (cfg.order != Order::Shuffle && cfg.order != Order::Sequential) || (cfg.fit != Fit::Fill && cfg.fit != Fit::Fit)) {
        error = L"Gallery configuration has an invalid group, count, cadence, layout, or order."; return false;
    }
    std::set<std::wstring> ids;
    for (const auto& tile : cfg.tiles) {
        for (const auto& field : {std::make_pair(L"Background", tile.appearance.background), std::make_pair(L"Branding", tile.appearance.branding), std::make_pair(L"BackgroundColor", tile.appearance.backgroundColor)}) {
            auto normalized = field.second;
            if (!aip::content::NormalizeTileAppearanceSetting(field.first, normalized, error) || normalized != field.second) {
                if (error.empty()) error = L"Gallery appearance values must use their canonical typed form.";
                return false;
            }
        }
        if ((!tile.id.empty() && (!StableTileId(tile.id) || !ids.insert(detail::Lower(tile.id)).second)) ||
            (!cfg.groupId.empty() && !tile.id.empty() && NativeTileId(cfg.groupId, tile.id).empty()) ||
            (cfg.enabled && tile.id.empty()) || detail::Trim(tile.name).empty() || !detail::Text(tile.name, 256) ||
            !detail::Text(tile.folder, 32768) || !detail::Text(tile.galleryName, 256)) {
            error = L"Gallery tiles need unique stable IDs, a combined native ID of at most 64 characters, and nonempty names."; return false;
        }
    }
    return true;
}
template<class Reader> bool ReadConfiguration(Reader read, Configuration& result, std::wstring& error) {
    Configuration cfg; bool valid = true; std::uint64_t count = 2, number = 0;
    const auto get = [&](const std::wstring& section, const wchar_t* key, const wchar_t* fallback) {
        auto value = read(section.c_str(), key, fallback); std::wstring issue;
        if (!NormalizeSetting(section, key, value, issue)) { if (valid) error = issue; valid = false; }
        return value;
    };
    cfg.enabled = get(L"Gallery", L"Enabled", L"0") == L"1";
    cfg.groupId = get(L"Gallery", L"GroupId", L""); cfg.folder = get(L"Gallery", L"Folder", L"");
    detail::UInt(get(L"Gallery", L"TileCount", L"2"), MaxTiles, count);
    detail::UInt(get(L"Gallery", L"QueueSize", L"5"), MaxQueue, number); cfg.queueSize = static_cast<unsigned>(number);
    detail::UInt(get(L"Gallery", L"RefreshSeconds", L"900"), 86400, number); cfg.refreshSeconds = static_cast<unsigned>(number);
    detail::UInt(get(L"Gallery", L"Seed", L"1"), (std::numeric_limits<std::uint64_t>::max)(), cfg.seed);
    cfg.order = get(L"Gallery", L"Order", L"Shuffle") == L"Shuffle" ? Order::Shuffle : Order::Sequential;
    cfg.fit = get(L"Gallery", L"Fit", L"Fill") == L"Fill" ? Fit::Fill : Fit::Fit;
    for (std::uint64_t i = 1; i <= count; ++i) {
        const auto section = L"Gallery.Tile." + std::to_wstring(i), name = L"Gallery " + std::to_wstring(i);
        TileConfiguration tile; tile.id = get(section, L"Id", L""); tile.name = get(section, L"Name", name.c_str());
        tile.folder = get(section, L"Folder", L""); tile.galleryName = get(section, L"GalleryName", L"");
        tile.enabled = get(section, L"Enabled", L"1") == L"1"; cfg.tiles.push_back(std::move(tile));
        if (!aip::content::ReadTileAppearance(read, section, cfg.tiles.back().appearance, error)) return false;
    }
    if (!valid || !ValidateConfiguration(cfg, error)) return false;
    result = std::move(cfg); error.clear(); return true;
}

struct Reservation { std::wstring owner; std::uint64_t ordinal = 0; };
struct PendingWindow { std::uint64_t ticket = 0, nextCursor = 0; std::vector<std::wstring> images; };
struct TileLedger { std::uint64_t cursor = 0; PendingWindow pending; };
struct Ledger {
    std::wstring groupId, epochId;
    std::uint64_t revision = 0, nextOrdinal = 1, nextTicket = 1;
    std::map<std::wstring, Reservation> images;
    std::map<std::wstring, TileLedger> tiles;
};
inline bool ValidateLedger(const Ledger& ledger, std::wstring& error) {
    error.clear();
    if (!StableTileId(ledger.groupId) || !OpaqueImageId(ledger.epochId) || ledger.images.size() > MaxImages ||
        ledger.tiles.size() > MaxHistoricalTiles || ledger.nextOrdinal != ledger.images.size() + 1 || !ledger.nextTicket) {
        error = L"The gallery ownership ledger is missing, invalid, or exceeds its limits. Do not rebuild it from current pins."; return false;
    }
    std::set<std::wstring> owners; std::set<std::uint64_t> ordinals, tickets;
    for (const auto& tile : ledger.tiles) {
        const auto& pending = tile.second.pending;
        if (NativeTileId(ledger.groupId, tile.first).empty() || !owners.insert(detail::Lower(tile.first)).second || tile.second.cursor >= MaxImages ||
            pending.images.size() > MaxQueue || pending.nextCursor >= MaxImages ||
            (pending.images.empty() && (pending.ticket || pending.nextCursor)) ||
            (!pending.images.empty() && (!pending.ticket || pending.ticket >= ledger.nextTicket || !tickets.insert(pending.ticket).second))) {
            error = L"The gallery ledger has invalid owner or pending-window state."; return false;
        }
        std::set<std::wstring> unique;
        for (const auto& image : pending.images) {
            const auto found = ledger.images.find(image);
            if (!unique.insert(image).second || found == ledger.images.end() || found->second.owner != tile.first) {
                error = L"A pending gallery window does not belong entirely to its tile."; return false;
            }
        }
    }
    for (const auto& image : ledger.images) {
        if (!OpaqueImageId(image.first) || !image.second.ordinal || image.second.ordinal >= ledger.nextOrdinal ||
            !ordinals.insert(image.second.ordinal).second || ledger.tiles.find(image.second.owner) == ledger.tiles.end()) {
            error = L"The gallery ledger has an invalid or duplicate image reservation."; return false;
        }
    }
    return true;
}
// Caller must attest there is no earlier gallery history, or ALL its historical
// pins/queues have been retired. New TileIDs or a missing/corrupt file are not
// proof and must never be used to bypass existing ownership history.
inline bool CreateLedger(const std::wstring& groupId, const std::wstring& epochId, bool galleryHistoryKnownNewOrRetired,
    Ledger& result, std::wstring& error) {
    if (!galleryHistoryKnownNewOrRetired) { error = L"Gallery initialization requires no earlier gallery history or confirmed retirement of ALL historical pins and queues."; return false; }
    Ledger candidate; candidate.groupId = groupId; candidate.epochId = epochId;
    if (!ValidateLedger(candidate, error)) return false;
    result = std::move(candidate); return true;
}
inline bool ResetLedger(const Ledger& previous, const std::wstring& newEpoch, bool hostConfirmedAllOldPinsAndQueuesRetired,
    Ledger& result, std::wstring& error) {
    if (!ValidateLedger(previous, error)) return false;
    if (!hostConfirmedAllOldPinsAndQueuesRetired || newEpoch == previous.epochId) {
        error = L"Gallery reset needs host confirmation that ALL historical pins/queues are retired and a new epoch ID."; return false;
    }
    return CreateLedger(previous.groupId, newEpoch, true, result, error);
}
struct TilePool { std::wstring tileId; std::vector<std::wstring> images; std::size_t reservedCount = 0; };
struct PoolSnapshot {
    std::wstring groupId, epochId, folder;
    std::uint64_t nextOrdinal = 0, seed = 0;
    Order order = Order::Shuffle;
    std::map<std::wstring, bool> members;
    std::map<std::wstring, std::wstring> folders;
    std::vector<TilePool> pools;
};
struct OwnershipPlan { std::uint64_t expectedRevision = 0; Ledger ledger; PoolSnapshot pools; bool requiresLedgerCommit = false; };
inline std::map<std::wstring, bool> Membership(const Configuration& cfg) {
    std::map<std::wstring, bool> result; for (const auto& tile : cfg.tiles) result.emplace(tile.id, tile.enabled); return result;
}
inline std::map<std::wstring, std::wstring> FolderMembership(const Configuration& cfg) {
    std::map<std::wstring, std::wstring> result; for (const auto& tile : cfg.tiles) result.emplace(tile.id, EffectiveFolder(cfg, tile)); return result;
}
using EligibilityMap = std::map<std::wstring, std::shared_ptr<const std::set<std::wstring>>>;
inline bool ReconcileOwnership(const Configuration& cfg, const std::vector<std::wstring>& usableImageIds,
    const Ledger& previous, OwnershipPlan& result, std::wstring& error, const EligibilityMap* eligibility = nullptr) {
    if (!ValidateConfiguration(cfg, error) || !ValidateLedger(previous, error)) return false;
    if (cfg.groupId != previous.groupId || usableImageIds.size() > MaxImages) { error = L"Gallery group/ledger identity differs or the inventory exceeds its limit."; return false; }
    std::set<std::wstring> present;
    for (const auto& id : usableImageIds) { if (!OpaqueImageId(id)) { error = L"Gallery inventory contains an invalid stable image identity."; return false; } present.insert(id); }
    const auto eligible = [&](const std::wstring& tile, const std::wstring& image) {
        if (!eligibility) return true;
        const auto found = eligibility->find(tile); return found != eligibility->end() && found->second && found->second->find(image) != found->second->end();
    };
    OwnershipPlan candidate; candidate.expectedRevision = previous.revision; candidate.ledger = previous;
    std::map<std::wstring, std::size_t> available;
    if (cfg.enabled) for (const auto& tile : cfg.tiles) if (tile.enabled) available.emplace(tile.id, 0);
    for (const auto& id : present) {
        const auto found = previous.images.find(id);
        if (found != previous.images.end()) { const auto owner = available.find(found->second.owner); if (owner != available.end() && eligible(owner->first, id)) ++owner->second; }
    }
    for (const auto& id : present) {
        if (candidate.ledger.images.find(id) != candidate.ledger.images.end() || available.empty()) continue;
        if (candidate.ledger.images.size() == MaxImages) { error = L"Gallery reservations are full. Retain the ledger; retirement confirmation is required before resetting it."; return false; }
        auto chosen = available.end();
        for (auto owner = available.begin(); owner != available.end(); ++owner)
            if (eligible(owner->first, id) && (chosen == available.end() || owner->second < chosen->second)) chosen = owner;
        if (chosen == available.end()) continue;
        candidate.ledger.tiles.emplace(chosen->first, TileLedger{});
        candidate.ledger.images.emplace(id, Reservation{chosen->first, candidate.ledger.nextOrdinal++}); ++chosen->second;
        candidate.requiresLedgerCommit = true;
    }
    if (candidate.requiresLedgerCommit) {
        if (previous.revision == (std::numeric_limits<std::uint64_t>::max)()) { error = L"Gallery ledger revision is exhausted."; return false; }
        candidate.ledger.revision = previous.revision + 1;
    }
    if (!ValidateLedger(candidate.ledger, error)) return false;
    auto& snapshot = candidate.pools; snapshot.groupId = cfg.groupId; snapshot.epochId = previous.epochId;
    snapshot.folder = cfg.folder; snapshot.nextOrdinal = candidate.ledger.nextOrdinal; snapshot.order = cfg.order; snapshot.seed = cfg.seed; snapshot.members = Membership(cfg);
    snapshot.folders = FolderMembership(cfg);
    std::map<std::wstring, std::vector<std::pair<std::wstring, std::uint64_t>>> owned;
    for (const auto& image : candidate.ledger.images) owned[image.second.owner].push_back({image.first, image.second.ordinal});
    for (const auto& tile : cfg.tiles) {
        TilePool pool; pool.tileId = tile.id;
        for (const auto& image : owned[tile.id]) {
            ++pool.reservedCount; if (present.find(image.first) != present.end() && eligible(tile.id, image.first)) pool.images.push_back(image.first);
        }
        std::sort(pool.images.begin(), pool.images.end(), [&](const auto& a, const auto& b) {
            if (cfg.order == Order::Sequential) return candidate.ledger.images.at(a).ordinal < candidate.ledger.images.at(b).ordinal;
            const auto left = detail::Rank(a, cfg.seed), right = detail::Rank(b, cfg.seed); return left == right ? detail::Hex(a) < detail::Hex(b) : left < right;
        });
        snapshot.pools.push_back(std::move(pool));
    }
    result = std::move(candidate); error.clear(); return true;
}
enum class WindowStatus { Ready, EmptyPool, PendingImagesUnavailable, Disabled };
struct QueueWindow {
    std::wstring groupId, tileId, epochId;
    std::uint64_t ticket = 0;
    WindowStatus status = WindowStatus::EmptyPool;
    bool retry = false;
    std::vector<std::wstring> images;
};
struct QueueBatch {
    std::uint64_t expectedRevision = 0;
    Ledger ledger;
    bool requiresLedgerCommit = false;
    unsigned nextRefreshSeconds = 900;
    std::vector<QueueWindow> windows;
};
// Supply a cached pool snapshot rebuilt only on inventory/membership/order
// changes. This step selects <=5 IDs per due tile; it never scans a directory,
// decodes an asset, or predicts which Windows notification is currently visible.
inline bool PrepareQueueBatch(const Configuration& cfg, const PoolSnapshot& pools, const Ledger& previous,
    const std::set<std::wstring>& dueTileIds, QueueBatch& result, std::wstring& error) {
    if (!ValidateConfiguration(cfg, error) || !ValidateLedger(previous, error)) return false;
    if (pools.groupId != previous.groupId || cfg.groupId != previous.groupId || pools.epochId != previous.epochId ||
        pools.nextOrdinal != previous.nextOrdinal || pools.folder != cfg.folder || pools.order != cfg.order || pools.seed != cfg.seed || pools.members != Membership(cfg) || pools.folders != FolderMembership(cfg)) {
        error = L"Gallery pools must be rebuilt after an ownership, inventory, membership, or order change."; return false;
    }
    for (const auto& id : dueTileIds) if (pools.members.find(id) == pools.members.end()) { error = L"A due gallery tile is not in this group."; return false; }
    QueueBatch candidate; candidate.expectedRevision = previous.revision; candidate.ledger = previous; candidate.nextRefreshSeconds = cfg.refreshSeconds;
    for (const auto& tile : cfg.tiles) {
        if (dueTileIds.find(tile.id) == dueTileIds.end()) continue;
        QueueWindow window; window.groupId = previous.groupId; window.tileId = tile.id; window.epochId = previous.epochId;
        if (!cfg.enabled || !tile.enabled) { window.status = WindowStatus::Disabled; candidate.windows.push_back(std::move(window)); continue; }
        const auto pool = std::find_if(pools.pools.begin(), pools.pools.end(), [&](const auto& value) { return value.tileId == tile.id; });
        if (pool == pools.pools.end() || pool->images.size() > MaxImages) { error = L"A cached gallery pool is missing or invalid."; return false; }
        auto state = candidate.ledger.tiles.find(tile.id);
        if (state != candidate.ledger.tiles.end() && state->second.pending.ticket) {
            window.ticket = state->second.pending.ticket; window.images = state->second.pending.images; window.retry = true; window.status = WindowStatus::Ready;
            for (const auto& id : window.images) if (std::find(pool->images.begin(), pool->images.end(), id) == pool->images.end()) window.status = WindowStatus::PendingImagesUnavailable;
        } else if (!pool->images.empty()) {
            if (candidate.ledger.nextTicket == (std::numeric_limits<std::uint64_t>::max)()) { error = L"Gallery delivery ticket space is exhausted."; return false; }
            auto& progress = candidate.ledger.tiles[tile.id];
            auto& pending = progress.pending; pending.ticket = candidate.ledger.nextTicket++;
            const auto start = static_cast<std::size_t>(progress.cursor % pool->images.size()), count = (std::min)(static_cast<std::size_t>(cfg.queueSize), pool->images.size());
            for (std::size_t i = 0; i < count; ++i) pending.images.push_back(pool->images[(start + i) % pool->images.size()]);
            pending.nextCursor = (start + count) % pool->images.size(); window.ticket = pending.ticket; window.images = pending.images; window.status = WindowStatus::Ready;
            candidate.requiresLedgerCommit = true;
        }
        std::set<std::wstring> unique;
        for (const auto& id : window.images) {
            const auto owned = previous.images.find(id);
            if (!unique.insert(id).second || owned == previous.images.end() || owned->second.owner != tile.id) { error = L"A gallery window cannot contain another tile's reserved image."; return false; }
        }
        candidate.windows.push_back(std::move(window));
    }
    if (candidate.requiresLedgerCommit) {
        if (previous.revision == (std::numeric_limits<std::uint64_t>::max)()) { error = L"Gallery ledger revision is exhausted."; return false; }
        candidate.ledger.revision = previous.revision + 1;
    }
    if (!ValidateLedger(candidate.ledger, error)) return false;
    result = std::move(candidate); error.clear(); return true;
}
inline bool ValidateWindowForPublication(const Ledger& durableLedger, const QueueWindow& window, std::wstring& error) {
    if (!ValidateLedger(durableLedger, error)) return false;
    const auto tile = durableLedger.tiles.find(window.tileId);
    if (window.status != WindowStatus::Ready || window.groupId != durableLedger.groupId || window.epochId != durableLedger.epochId || !window.ticket ||
        tile == durableLedger.tiles.end() || tile->second.pending.ticket != window.ticket || tile->second.pending.images != window.images) {
        error = L"Gallery publication requires its exact pending window in the durable ownership ledger."; return false;
    }
    error.clear(); return true;
}
inline bool AcknowledgeDelivery(const Ledger& previous, const QueueWindow& window, bool accepted,
    Ledger& result, std::wstring& error) {
    if (!ValidateLedger(previous, error)) return false;
    const auto tile = previous.tiles.find(window.tileId);
    if (window.groupId != previous.groupId || window.epochId != previous.epochId || !window.ticket || tile == previous.tiles.end() ||
        tile->second.pending.ticket != window.ticket || tile->second.pending.images != window.images ||
        (accepted && window.status != WindowStatus::Ready)) { error = L"Gallery delivery acknowledgement is stale or does not match its pending window."; return false; }
    if (previous.revision == (std::numeric_limits<std::uint64_t>::max)()) { error = L"Gallery ledger revision is exhausted."; return false; }
    auto candidate = previous; auto& progress = candidate.tiles.at(window.tileId);
    if (accepted) progress.cursor = progress.pending.nextCursor;
    progress.pending = {}; candidate.revision = previous.revision + 1;
    result = std::move(candidate); error.clear(); return true;
}

// Strict bounded ASCII line format; Unicode opaque IDs are hex UTF-16 units.
// Hosts write these bytes atomically and CAS expectedRevision under their file
// lock. Parse failure must never be treated as an empty ownership ledger.
inline bool SerializeLedger(const Ledger& ledger, std::string& result, std::wstring& error) {
    if (!ValidateLedger(ledger, error)) return false;
    std::string text;
    const auto line = [&](const std::string& value) {
        if (value.size() + 1 > MaxLedgerBytes - text.size()) return false;
        text += value; text += '\n'; return true;
    };
    if (!line("AIPGALLERY 1") || !line("G " + detail::Hex(ledger.groupId)) || !line("E " + detail::Hex(ledger.epochId)) ||
        !line("V " + std::to_string(ledger.revision) + " " + std::to_string(ledger.nextOrdinal) + " " + std::to_string(ledger.nextTicket)) ||
        !line("I " + std::to_string(ledger.images.size()))) { error = L"Gallery ledger exceeds its wire-size limit."; return false; }
    for (const auto& image : ledger.images) if (!line(detail::Hex(image.first) + " " + detail::Hex(image.second.owner) + " " + std::to_string(image.second.ordinal))) { error = L"Gallery ledger exceeds its wire-size limit."; return false; }
    if (!line("T " + std::to_string(ledger.tiles.size()))) { error = L"Gallery ledger exceeds its wire-size limit."; return false; }
    for (const auto& tile : ledger.tiles) {
        const auto& state = tile.second;
        if (!line(detail::Hex(tile.first) + " " + std::to_string(state.cursor) + " " + std::to_string(state.pending.ticket) + " " + std::to_string(state.pending.nextCursor) + " " + std::to_string(state.pending.images.size()))) { error = L"Gallery ledger exceeds its wire-size limit."; return false; }
        for (const auto& id : state.pending.images) if (!line(detail::Hex(id))) { error = L"Gallery ledger exceeds its wire-size limit."; return false; }
    }
    if (!line("END")) { error = L"Gallery ledger exceeds its wire-size limit."; return false; }
    result = std::move(text); error.clear(); return true;
}
inline bool ParseLedger(const std::string& text, Ledger& result, std::wstring& error) {
    error = L"The gallery ownership ledger is malformed, unsupported, or exceeds its limits. Keep it and stop publication.";
    if (text.empty() || text.size() > MaxLedgerBytes) return false;
    std::size_t position = 0;
    const auto tokens = [&](std::vector<std::string>& fields, std::size_t count) {
        const auto end = text.find('\n', position); if (end == std::string::npos || end - position > 2048) return false;
        auto row = text.substr(position, end - position); position = end + 1;
        if (!row.empty() && row.back() == '\r') row.pop_back();
        fields.clear(); std::size_t start = 0;
        for (;;) { const auto space = row.find(' ', start); const auto value = row.substr(start, space == std::string::npos ? space : space - start); if (value.empty()) return false; fields.push_back(value); if (space == std::string::npos) break; start = space + 1; }
        return fields.size() == count;
    };
    const auto number = [](const std::string& value, std::uint64_t maximum, std::uint64_t& parsed) {
        return detail::UInt(std::wstring(value.begin(), value.end()), maximum, parsed);
    };
    Ledger candidate; std::vector<std::string> row; std::uint64_t count = 0;
    if (!tokens(row, 2) || row[0] != "AIPGALLERY" || row[1] != "1" ||
        !tokens(row, 2) || row[0] != "G" || !detail::Unhex(row[1], candidate.groupId) ||
        !tokens(row, 2) || row[0] != "E" || !detail::Unhex(row[1], candidate.epochId) ||
        !tokens(row, 4) || row[0] != "V" || !number(row[1], (std::numeric_limits<std::uint64_t>::max)(), candidate.revision) ||
        !number(row[2], MaxImages + 1, candidate.nextOrdinal) || !number(row[3], (std::numeric_limits<std::uint64_t>::max)(), candidate.nextTicket) ||
        !tokens(row, 2) || row[0] != "I" || !number(row[1], MaxImages, count)) return false;
    for (std::uint64_t i = 0; i < count; ++i) {
        std::wstring id, owner; Reservation reservation;
        if (!tokens(row, 3) || !detail::Unhex(row[0], id) || !detail::Unhex(row[1], owner) || !number(row[2], MaxImages, reservation.ordinal)) return false;
        reservation.owner = owner; if (!candidate.images.emplace(id, reservation).second) return false;
    }
    if (!tokens(row, 2) || row[0] != "T" || !number(row[1], MaxHistoricalTiles, count)) return false;
    for (std::uint64_t i = 0; i < count; ++i) {
        std::wstring id; TileLedger tile; std::uint64_t pendingCount = 0;
        if (!tokens(row, 5) || !detail::Unhex(row[0], id) || !number(row[1], MaxImages - 1, tile.cursor) ||
            !number(row[2], (std::numeric_limits<std::uint64_t>::max)(), tile.pending.ticket) ||
            !number(row[3], MaxImages - 1, tile.pending.nextCursor) || !number(row[4], MaxQueue, pendingCount)) return false;
        for (std::uint64_t j = 0; j < pendingCount; ++j) { std::wstring image; if (!tokens(row, 1) || !detail::Unhex(row[0], image)) return false; tile.pending.images.push_back(std::move(image)); }
        if (!candidate.tiles.emplace(id, std::move(tile)).second) return false;
    }
    if (!tokens(row, 1) || row[0] != "END" || position != text.size() || !ValidateLedger(candidate, error)) return false;
    result = std::move(candidate); error.clear(); return true;
}
} }
