#pragma once

// Pure per-source secondary tile mapping. This does not pin tiles, contact
// providers, or load Windows APIs; the host composes and delivers snapshots.
#include "content_engine.h"
#include "tile_actions.h"
#include <set>
#include <deque>

namespace aip { namespace content {
// A batch has at most one native confirmation in flight. A declined/error
// completion retires that item; it does not cancel the remaining user request.
class SecondaryPinBatch {
    std::deque<std::pair<std::wstring, std::wstring>> waiting_;
    std::wstring active_;
public:
    void Enqueue(const std::vector<std::pair<std::wstring, std::wstring>>& requests) {
        for (const auto& item : requests) {
            if (item.first.empty() || item.first == active_ || std::any_of(waiting_.begin(), waiting_.end(), [&](const auto& queued) { return queued.first == item.first; })) continue;
            waiting_.push_back(item);
        }
    }
    template<class Begin> void Advance(Begin begin) {
        if (!active_.empty()) return;
        while (!waiting_.empty()) {
            auto next = std::move(waiting_.front()); waiting_.pop_front(); active_ = next.first;
            if (begin(next)) return;
            active_.clear(); // A preparation failure also advances to the next item.
        }
    }
    void Complete(const std::wstring& id) { if (active_ == id) active_.clear(); }
    void Clear() { waiting_.clear(); active_.clear(); }
    bool Busy() const { return !active_.empty() || !waiting_.empty(); }
    std::size_t Waiting() const { return waiting_.size(); }
};
class SecondaryPinInventory {
    mutable std::mutex mutex_;
    std::set<std::wstring> ids_;
    bool known_ = false, requested_ = false;
    std::uint64_t retryAfter_ = 0;
public:
    void Reset() { std::lock_guard<std::mutex> lock(mutex_); ids_.clear(); known_ = false; requested_ = false; retryAfter_ = 0; }
    bool Known() const { std::lock_guard<std::mutex> lock(mutex_); return known_; }
    bool Contains(const std::wstring& id) const { std::lock_guard<std::mutex> lock(mutex_); return ids_.count(id) != 0; }
    std::set<std::wstring> Snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return ids_; }
    bool RequestDue(std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (requested_ || now < retryAfter_) return false;
        requested_ = true; return true;
    }
    void Complete(bool success, const std::set<std::wstring>& ids, std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (success) { ids_ = ids; known_ = true; }
        requested_ = false; retryAfter_ = now + 30000;
    }
    void Mutation(const std::wstring& id, bool pinned) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pinned) ids_.insert(id); else ids_.erase(id);
    }
};
struct SecondaryTileDescriptor {
    const wchar_t* id;
    const wchar_t* name;
    int content;
    const wchar_t* source;
    const wchar_t* background;
    int notesResource;
    bool defaultEnabled = true;
};
inline constexpr SecondaryTileDescriptor SecondaryTiles[] = {
    {L"smtc", L"Now playing", 2, L"SMTC", L"Transparent", 0},
    {L"rss", L"RSS", 3, L"RssFeed", L"Transparent", 0},
    {L"genshin", L"Genshin Impact", 4, L"Notes", L"Transparent", 0},
    {L"hsr", L"Honkai: Star Rail", 5, L"Notes", L"Transparent", 1},
    {L"zzz", L"Zenless Zone Zero", 6, L"Notes", L"Transparent", 2},
    {L"discord", L"Discord", 7, L"DiscordRPC", L"Transparent", 0},
    {L"prayer", L"Prayer times", 8, L"PrayerTimes", L"Transparent", 0, false},
};
inline constexpr std::size_t SecondaryTileCount = sizeof(SecondaryTiles) / sizeof(SecondaryTiles[0]);
struct SecondaryTile {
    std::wstring id, name;
    bool enabled = true;
    int content = 1; // One-based [Content.N] index.
    TileAppearance appearance;
    TextPresentation presentation;
};
struct SecondaryTileConfiguration {
    bool enabled = false;
    std::vector<SecondaryTile> tiles;
};
inline const SecondaryTileDescriptor* FindSecondaryTileDescriptor(const std::wstring& id) {
    const auto normalized = Lower(Trim(id));
    for (const auto& tile : SecondaryTiles) if (normalized == tile.id) return &tile;
    return nullptr;
}
inline std::wstring SecondaryTileSection(const std::wstring& id) { return L"Tile." + id; }
template<class Reader> bool ReadSecondaryAppearanceFingerprint(Reader read, std::wstring& result, std::wstring& error) {
    std::wstring candidate;
    const auto add = [&](const std::wstring& section) {
        TileAppearance appearance;
        if (!ReadTileAppearance(read, section, appearance, error)) return false;
        candidate += section + L":" + appearance.background + L":" + appearance.branding + L":" + appearance.backgroundColor + L";";
        return true;
    };
    for (const auto& descriptor : SecondaryTiles) if (!add(SecondaryTileSection(descriptor.id))) return false;
    if (!add(L"MediaActions")) return false;
    result = std::move(candidate); error.clear(); return true;
}
inline bool NormalizeSecondaryTileSetting(const std::wstring& section, const std::wstring& key,
        std::wstring& value, std::wstring& error) {
    const auto normalizedSection = Lower(Trim(section)), normalizedKey = Lower(Trim(key));
    const bool root = normalizedSection == L"tiles";
    const bool tile = normalizedSection.compare(0, 5, L"tile.") == 0 &&
        FindSecondaryTileDescriptor(normalizedSection.substr(5));
    error = section + L"." + key + L": ";
    if (!root && !tile) { error += L"unknown secondary tile section."; return false; }
    if (tile && IsTextPresentationKey(key)) return NormalizeTextPresentationSetting(key, value, error, true);
    if (normalizedKey == L"enabled") {
        bool enabled = false;
        if (!ParseBool(value, enabled)) { error += L"expected an enabled/disabled boolean."; return false; }
        value = enabled ? L"1" : L"0";
    } else if (tile && (normalizedKey == L"background" || normalizedKey == L"branding" || normalizedKey == L"backgroundcolor")) {
        return NormalizeTileAppearanceSetting(key, value, error);
    } else if (tile && normalizedKey == L"clickaction") {
        return NormalizeTileActionSetting(section, key, value, error);
    } else if (tile && normalizedKey == L"content") {
        int content = 0;
        if (!ParseInt(value, 1, MaxEntries, content)) { error += L"expected a content number from 1 to 32."; return false; }
        value = std::to_wstring(content);
    } else if (tile && normalizedKey == L"name") {
        if (Trim(value).empty() || value.size() > MaxText || value.find(L'\0') != std::wstring::npos) {
            error += L"expected a nonempty name of at most 4096 characters without NUL."; return false;
        }
    } else { error += L"unknown secondary tile setting."; return false; }
    error.clear(); return true;
}
inline bool ValidateSecondaryTileConfiguration(const SecondaryTileConfiguration& cfg,
        std::size_t contentCount, std::wstring& error) {
    std::set<std::wstring> ids;
    for (const auto& tile : cfg.tiles) {
        const auto* descriptor = FindSecondaryTileDescriptor(tile.id);
        if (!descriptor || tile.id != descriptor->id || !ids.insert(tile.id).second) {
            error = L"Secondary tiles require unique IDs from the source tile catalog."; return false;
        }
        if (tile.content < 1 || tile.content > MaxEntries ||
            (cfg.enabled && tile.enabled && static_cast<std::size_t>(tile.content) > contentCount)) {
            error = SecondaryTileSection(tile.id) + L".Content must select an existing numbered content."; return false;
        }
        if (Trim(tile.name).empty() || tile.name.size() > MaxText || tile.name.find(L'\0') != std::wstring::npos) {
            error = SecondaryTileSection(tile.id) + L".Name is invalid."; return false;
        }
        auto appearance = tile.appearance;
        if (!NormalizeTileAppearanceSetting(L"Background", appearance.background, error) ||
            !NormalizeTileAppearanceSetting(L"Branding", appearance.branding, error) ||
            !NormalizeTileAppearanceSetting(L"BackgroundColor", appearance.backgroundColor, error)) {
            error = SecondaryTileSection(tile.id) + L": " + error; return false;
        }
    }
    if (cfg.tiles.size() != SecondaryTileCount) { error = L"Secondary tile configuration requires all catalog mappings."; return false; }
    error.clear(); return true;
}
template<class Reader> bool ReadSecondaryTileConfiguration(Reader read, std::size_t contentCount,
        SecondaryTileConfiguration& result, std::wstring& error) {
    SecondaryTileConfiguration cfg;
    bool valid = true;
    const auto get = [&](const std::wstring& section, const wchar_t* key, const wchar_t* fallback) {
        auto value = read(section.c_str(), key, fallback);
        std::wstring issue;
        if (!NormalizeSecondaryTileSetting(section, key, value, issue)) {
            if (valid) error = issue;
            valid = false;
        }
        return value;
    };
    cfg.enabled = get(L"Tiles", L"Enabled", L"0") == L"1";
    TextPresentation globalPresentation;
    std::wstring presentationError;
    if (!ReadTextPresentation(read, L"TileText", globalPresentation, presentationError)) { error = presentationError; return false; }
    for (const auto& descriptor : SecondaryTiles) {
        SecondaryTile tile;
        tile.id = descriptor.id;
        const auto section = SecondaryTileSection(tile.id);
        tile.enabled = get(section, L"Enabled", descriptor.defaultEnabled ? L"1" : L"0") == L"1";
        const auto defaultContent = std::to_wstring(descriptor.content);
        ParseInt(get(section, L"Content", defaultContent.c_str()), 1, MaxEntries, tile.content);
        tile.name = get(section, L"Name", descriptor.name);
        if (!ReadTextPresentation(read, section, tile.presentation, presentationError, &globalPresentation)) {
            if (valid) error = presentationError;
            return false;
        }
        std::wstring appearanceError;
        if (!ReadTileAppearance(read, section, tile.appearance, appearanceError)) {
            if (valid) error = appearanceError;
            valid = false;
        }
        cfg.tiles.push_back(std::move(tile));
    }
    if (!valid || !ValidateSecondaryTileConfiguration(cfg, contentCount, error)) return false;
    result = std::move(cfg); error.clear(); return true;
}
inline std::set<std::size_t> SecondaryTileContentIndices(const SecondaryTileConfiguration& tiles,
        const Configuration& content) {
    std::set<std::size_t> result;
    if (!content.enabled || content.entries.empty()) return result;
    if (content.entries.front().enabled) result.insert(0); // Primary wallpaper/content remains independent.
    if (!tiles.enabled) return result;
    for (const auto& tile : tiles.tiles) {
        if (!tile.enabled || tile.content < 1) continue;
        const auto index = static_cast<std::size_t>(tile.content - 1);
        if (index < content.entries.size() && content.entries[index].enabled) result.insert(index);
    }
    return result;
}
inline void ApplyPinnedSecondaryTiles(Configuration& content, SecondaryTileConfiguration& tiles,
        const std::set<std::wstring>& pinned) {
    bool sourcePinned = false, galleryPinned = false;
    const auto previousSize = content.entries.size(); std::set<std::size_t> initialized;
    for (const auto& id : pinned) if (id.compare(0, 11, L"AipGallery_") == 0) galleryPinned = true;
    for (auto& tile : tiles.tiles) {
        tile.enabled = pinned.count(L"AipSource_" + tile.id) != 0;
        if (!tile.enabled || tile.content < 1 || tile.content > MaxEntries) continue;
        sourcePinned = true;
        const auto index = static_cast<std::size_t>(tile.content - 1);
        if (content.entries.size() <= index) content.entries.resize(index + 1);
        auto& entry = content.entries[index];
        if (index >= previousSize && initialized.insert(index).second) {
            const auto* descriptor = FindSecondaryTileDescriptor(tile.id);
            entry.name = tile.name; entry.background = descriptor->background;
            entry.sources = {descriptor->source}; entry.notesResource = descriptor->notesResource;
        }
        entry.enabled = true; // Saved enable flags do not pause a pinned source.
    }
    tiles.enabled = tiles.enabled || sourcePinned || galleryPinned; // Preserve the saved primary layout while removing its secondary-update gate.
    if (tiles.enabled) content.enabled = true;
}
} }
