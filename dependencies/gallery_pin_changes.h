#pragma once
#include "gallery_engine.h"

namespace aip { namespace gallery {
struct PinMembershipChange {
    std::vector<std::pair<std::wstring, std::wstring>> removedNative, addedNative;
    std::vector<std::wstring> removedRegistration;
};
// Compare stable IDs, never numbered INI sections: shrinking one gallery can
// renumber every row in a later gallery without removing those later tiles.
inline PinMembershipChange PlanPinMembershipChange(const Configuration& before, const Configuration& after) {
    PinMembershipChange result;
    std::set<std::wstring> oldIds, newIds;
    for (const auto& tile : before.tiles) oldIds.insert(NativeTileId(before.groupId, tile.id));
    for (const auto& tile : after.tiles) newIds.insert(NativeTileId(after.groupId, tile.id));
    for (const auto& tile : before.tiles) {
        const auto id = NativeTileId(before.groupId, tile.id);
        if (!id.empty() && !newIds.count(id)) {
            if (UsesRegistration(tile)) result.removedRegistration.push_back(tile.name);
            else result.removedNative.emplace_back(id, tile.name);
        }
    }
    for (const auto& tile : after.tiles) {
        const auto id = NativeTileId(after.groupId, tile.id);
        if (!id.empty() && !oldIds.count(id) && !UsesRegistration(tile)) result.addedNative.emplace_back(id, tile.name);
    }
    return result;
}
} }
