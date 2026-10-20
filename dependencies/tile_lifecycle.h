#pragma once
// Notification clearing and pin menu policy have no pin mutation, file deletion,
// ledger mutation, or Windows dependency. Native operations are injected at the
// narrow boundary, so a failure for one tile cannot strand another tile's queue.
#include <set>
#include <string>
#include <vector>

namespace aip { namespace content {
enum class TilePinMenuState { Unavailable, Unknown, Pinned, Unpinned };
inline TilePinMenuState TilePinState(bool available, bool known, bool pinned) {
    return !available ? TilePinMenuState::Unavailable : !known ? TilePinMenuState::Unknown :
        pinned ? TilePinMenuState::Pinned : TilePinMenuState::Unpinned;
}
template<class Exists, class Clear>
std::vector<std::wstring> ClearOwnedSecondaryTileNotifications(const std::set<std::wstring>& owned, Exists exists, Clear clear) {
    std::vector<std::wstring> failures;
    for (const auto& id : owned) {
        if (id.empty()) continue;
        try { if (exists(id)) clear(id); }
        catch (...) { failures.push_back(id); }
    }
    return failures;
}
}} // namespace aip::content
