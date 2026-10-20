#pragma once
// Explicit source-name changes only. A resident's initial configuration is a
// baseline, never permission to overwrite a user's existing Start tile names.
#include "secondary_tiles.h"
#include "tile_instances.h"
#include <map>
#include <mutex>
#include <cstdint>
#include <limits>
namespace aip { namespace content {
using SourceTileNames = std::map<std::wstring, std::wstring>;
inline constexpr const wchar_t* SourceTileNameStateSection = L"State.SourceTileNames";
struct SourceTileNameIntent { std::wstring name; std::uint64_t revision = 0; };
using SourceTileNameIntents = std::map<std::wstring, SourceTileNameIntent>;
inline bool ParseSourceNameRevision(const std::wstring& value, std::uint64_t& result) {
    if (value.empty() || value.size() > 20 || (value.size() > 1 && value.front() == L'0')) return false;
    std::uint64_t candidate = 0;
    for (const auto ch : value) {
        if (ch < L'0' || ch > L'9' || candidate > ((std::numeric_limits<std::uint64_t>::max)() - (ch - L'0')) / 10) return false;
        candidate = candidate * 10 + (ch - L'0');
    }
    result = candidate; return true;
}
template<class Reader> bool ReadSourceTileNames(Reader read, SourceTileNames& result, std::wstring& error) {
    SourceTileNames candidate;
    TileInstanceConfiguration instances;
    if (!ReadTileInstanceConfiguration(read, instances, error)) return false;
    if (instances.configured) {
        for (const auto& tile : instances.tiles) if (!tile.primary) candidate.emplace(TileInstanceNativeId(tile), tile.name);
    } else for (const auto& descriptor : SecondaryTiles) {
        const auto section = SecondaryTileSection(descriptor.id);
        auto name = read(section.c_str(), L"Name", descriptor.name);
        if (!NormalizeSecondaryTileSetting(section, L"Name", name, error)) return false;
        candidate.emplace(L"AipSource_" + std::wstring(descriptor.id), std::move(name));
    }
    result = std::move(candidate); error.clear(); return true;
}
template<class Reader> bool ReadSourceTileNameIntents(Reader read, SourceTileNameIntents& result, std::uint64_t& sequence, std::wstring& error) {
    SourceTileNameIntents candidate; std::uint64_t counter = 0;
    if (!ParseSourceNameRevision(read(SourceTileNameStateSection, L"Sequence", L"0"), counter)) {
        error = L"Source tile name intent sequence is invalid."; return false;
    }
    SourceTileNames configured;
    if (!ReadSourceTileNames(read, configured, error)) return false;
    for (const auto& tile : configured) {
        const auto key = tile.first.substr(10);
        const auto wire = read(SourceTileNameStateSection, key.c_str(), L"");
        if (wire.empty()) continue;
        const auto colon = wire.find(L':'); SourceTileNameIntent intent;
        if (colon == std::wstring::npos || !ParseSourceNameRevision(wire.substr(0, colon), intent.revision) || !intent.revision || intent.revision > counter) {
            error = L"A source tile name intent has invalid revision metadata."; return false;
        }
        intent.name = wire.substr(colon + 1);
        if (!NormalizeTileInstanceSetting(TileInstanceSection(key), L"Name", intent.name, error)) return false;
        candidate.emplace(tile.first, std::move(intent));
    }
    result = std::move(candidate); sequence = counter; return true;
}
class SourceTileNameNotice {
    std::mutex mutex_;
    bool known_ = false, posted_ = false;
    SourceTileNames observed_, pending_;
    std::map<std::wstring, std::uint64_t> intentRevisions_;
    bool ShouldPost() { if (pending_.empty() || posted_) return false; posted_ = true; return true; }
public:
    bool Observe(const SourceTileNames& names) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (known_) for (const auto& name : names) {
            const auto previous = observed_.find(name.first);
            if (previous != observed_.end() && previous->second != name.second) pending_[name.first] = name.second;
        }
        observed_ = names; known_ = true; return ShouldPost();
    }
    bool Explicit(const SourceTileNames& names) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& name : names) { pending_[name.first] = name.second; if (known_) observed_[name.first] = name.second; }
        return ShouldPost();
    }
    bool ObserveIntents(const SourceTileNameIntents& intents) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& intent : intents) if (intentRevisions_[intent.first] != intent.second.revision) {
            pending_[intent.first] = intent.second.name; intentRevisions_[intent.first] = intent.second.revision;
        }
        return ShouldPost();
    }
    void RecordIntents(const SourceTileNameIntents& intents) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& intent : intents) intentRevisions_[intent.first] = intent.second.revision;
    }
    SourceTileNames Consume() {
        std::lock_guard<std::mutex> lock(mutex_);
        SourceTileNames result; result.swap(pending_); posted_ = false; return result;
    }
    void PostFailed() { std::lock_guard<std::mutex> lock(mutex_); posted_ = false; }
    void Clear() { std::lock_guard<std::mutex> lock(mutex_); known_ = posted_ = false; observed_.clear(); pending_.clear(); intentRevisions_.clear(); }
};
// The only property this operation writes is DisplayName on an existing object.
// A fake object in inert tests exercises this exact property-selection policy.
template<class Tile> bool ApplyExplicitSourceTileName(Tile& tile, const std::wstring& id, const std::wstring& name,
        const SourceTileNames* configured = nullptr) {
    if (id.compare(0, 10, L"AipSource_") != 0) return false;
    const auto key = id.substr(10);
    const auto* descriptor = FindSecondaryTileDescriptor(key);
    auto validated = name; std::wstring error;
    if (!ValidTileInstanceId(key) || (configured ? configured->find(id) == configured->end() || configured->at(id) != name : !descriptor || key != descriptor->id) ||
        !NormalizeTileInstanceSetting(TileInstanceSection(key), L"Name", validated, error) ||
        std::wstring(tile.TileId()) != id || std::wstring(tile.DisplayName()) == name) return false;
    tile.DisplayName(name); return true;
}
} }
