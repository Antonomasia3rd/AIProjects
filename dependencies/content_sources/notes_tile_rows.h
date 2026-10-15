#pragma once
// Portable presentation of one already-captured Notes snapshot. No account or
// provider access is performed by the tile renderer.
#include "../content_engine.h"
#include "notes_model.h"

namespace aip { namespace notes {
inline const wchar_t* ResourceTileTopic(ResourceKind resource) {
    switch (resource) {
    case ResourceKind::Resin: return L"resin";
    case ResourceKind::Stamina: return L"stamina";
    case ResourceKind::Charge: return L"charge";
    default: return L"";
    }
}
inline const wchar_t* ResourceTileLabel(ResourceKind resource) {
    switch (resource) {
    case ResourceKind::Resin: return L"Resin";
    case ResourceKind::Stamina: return L"Power";
    case ResourceKind::Charge: return L"Charge";
    default: return L"Game resource";
    }
}
template<class Snapshot>
inline std::vector<content::Text> BuildNoteTileRows(const Snapshot& snapshot,
    DetailId selected = DetailId::Summary) {
    const auto resource = static_cast<int>(snapshot.resource);
    if (snapshot.state != StateKind::Ok && snapshot.state != StateKind::Full)
        return {{snapshot.line1, snapshot.line2, L""}};
    if (selected != DetailId::Summary) {
        const auto found = snapshot.details.find(selected);
        const auto* detail = FindDetail(selected);
        if (!detail || !DetailAvailable(resource, selected))
            return {{L"Detail unavailable", L"This game does not provide the selected detail.", L""}};
        return {{detail->label, found == snapshot.details.end() ? L"Unavailable" : found->second, L""}};
    }
    content::Text main;
    main.primary = std::wstring(ResourceTileLabel(snapshot.resource)) + L" " + snapshot.line1;
    main.secondary = snapshot.state == StateKind::Full ? L"Full" :
        (snapshot.recoverySeconds > 0 ? L"Full in " + snapshot.line2 : snapshot.line2);
    std::vector<content::Text> rows{std::move(main)};
    // Show only reported, game-appropriate fields. Do not invent completion
    // states for absent API data or borrow another game's fields.
    for (const auto& detail : Details) {
        if (detail.id == DetailId::Summary || !DetailAvailable(resource, detail.id)) continue;
        const auto found = snapshot.details.find(detail.id);
        if (found == snapshot.details.end() || found->second.empty()) continue;
        rows.push_back({detail.id == DetailId::Expeditions ? L"Expeditions ready" : detail.label, found->second, L""});
    }
    return rows;
}
} }
