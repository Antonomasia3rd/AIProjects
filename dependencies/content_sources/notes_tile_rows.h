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
inline std::wstring ResourceRowIcon(ResourceKind resource, StateKind state) {
    const auto game = resource == ResourceKind::Stamina ? L"hsr-stamina" : resource == ResourceKind::Charge ? L"zzz-charge" : L"genshin-resin";
    return std::wstring(L"notes-") + game + (state == StateKind::Error ? L"_error" : state == StateKind::Full ? L"_full" : L"_not_full");
}
inline std::wstring DetailRowIcon(ResourceKind resource, DetailId detail) {
    if (!DetailAvailable(static_cast<int>(resource), detail)) return {};
    const auto game = resource == ResourceKind::Stamina ? L"hsr-" : resource == ResourceKind::Charge ? L"zzz-" : L"genshin-";
    const wchar_t* asset = nullptr;
    switch (detail) {
    case DetailId::Commissions: asset = L"commission"; break;
    case DetailId::Expeditions: asset = L"expedition"; break;
    case DetailId::Realm: asset = L"realm"; break;
    case DetailId::WeeklyBosses: asset = L"weekly_boss"; break;
    case DetailId::Training: asset = L"training"; break;
    case DetailId::Reserve: asset = L"stamina_not_full"; break;
    case DetailId::EchoOfWar: asset = L"echo_of_war"; break;
    case DetailId::Engagement: asset = L"engagement"; break;
    case DetailId::ScratchCard: asset = L"ticket"; break;
    case DetailId::VideoStore: asset = L"tape"; break;
    default: return {};
    }
    return std::wstring(L"notes-") + game + asset;
}
template<class Snapshot, class LabelReader>
inline std::vector<content::Text> BuildNoteTileRows(const Snapshot& snapshot,
    DetailId selected, LabelReader label) {
    const auto resource = static_cast<int>(snapshot.resource);
    if (snapshot.state != StateKind::Ok && snapshot.state != StateKind::Full)
        return {{snapshot.line1, snapshot.line2, L"", ResourceRowIcon(snapshot.resource, snapshot.state)}};
    if (selected != DetailId::Summary) {
        const auto found = snapshot.details.find(selected);
        const auto* detail = FindDetail(selected);
        if (!detail || !DetailAvailable(resource, selected))
            return {{label(L"NotesDetailUnavailable", L"Detail unavailable"), label(L"NotesDetailUnsupported", L"This game does not provide the selected detail."), L""}};
        return {{label((L"NotesDetail" + std::wstring(detail->value)).c_str(), detail->label),
            found == snapshot.details.end() ? label(L"NotesUnavailable", L"Unavailable") : found->second, L"", DetailRowIcon(snapshot.resource, selected)}};
    }
    content::Text main;
    const auto* resourceKey = snapshot.resource == ResourceKind::Stamina ? L"NotesPower" : snapshot.resource == ResourceKind::Charge ? L"NotesCharge" : L"NotesResin";
    main.primary = label(resourceKey, ResourceTileLabel(snapshot.resource)) + L" " + snapshot.line1;
    main.secondary = snapshot.state == StateKind::Full ? label(L"NotesFull", L"Full") :
        (snapshot.recoverySeconds > 0 ? label(L"NotesFullIn", L"Full in") + L" " + snapshot.line2 : snapshot.line2);
    main.iconTopic = ResourceRowIcon(snapshot.resource, snapshot.state);
    std::vector<content::Text> rows{std::move(main)};
    // Show only reported, game-appropriate fields. Do not invent completion
    // states for absent API data or borrow another game's fields.
    for (const auto& detail : Details) {
        if (detail.id == DetailId::Summary || !DetailAvailable(resource, detail.id)) continue;
        const auto found = snapshot.details.find(detail.id);
        if (found == snapshot.details.end() || found->second.empty()) continue;
        rows.push_back({detail.id == DetailId::Expeditions ? label(L"NotesExpeditionsReady", L"Expeditions ready") :
            label((L"NotesDetail" + std::wstring(detail.value)).c_str(), detail.label), found->second, L"", DetailRowIcon(snapshot.resource, detail.id)});
    }
    return rows;
}
template<class Snapshot>
inline std::vector<content::Text> BuildNoteTileRows(const Snapshot& snapshot, DetailId selected = DetailId::Summary) {
    return BuildNoteTileRows(snapshot, selected, [](const wchar_t*, const wchar_t* fallback) { return std::wstring(fallback); });
}
} }
