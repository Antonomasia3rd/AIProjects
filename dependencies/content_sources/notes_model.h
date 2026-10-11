#pragma once
#include <string>

namespace aip { namespace notes {
enum class DetailId { Summary, Commissions, Expeditions, Realm, WeeklyBosses, Training, Reserve, EchoOfWar, Engagement, ScratchCard, VideoStore };
struct DetailChoice { const wchar_t* value; const wchar_t* label; DetailId id; unsigned resources; };
inline constexpr DetailChoice Details[] = {
    {L"Summary", L"Resource and recovery", DetailId::Summary, 7},
    {L"Commissions", L"Commissions", DetailId::Commissions, 1},
    {L"Expeditions", L"Expeditions", DetailId::Expeditions, 3},
    {L"Realm", L"Realm", DetailId::Realm, 1},
    {L"WeeklyBosses", L"Weekly Bosses", DetailId::WeeklyBosses, 1},
    {L"Training", L"Training", DetailId::Training, 2},
    {L"Reserve", L"Reserve", DetailId::Reserve, 2},
    {L"EchoOfWar", L"Echo of War", DetailId::EchoOfWar, 2},
    {L"Engagement", L"Engagement", DetailId::Engagement, 4},
    {L"ScratchCard", L"Scratch Card", DetailId::ScratchCard, 4},
    {L"VideoStore", L"Video Store", DetailId::VideoStore, 4},
};
inline const DetailChoice* FindDetail(DetailId id) {
    for (const auto& detail : Details) if (detail.id == id) return &detail;
    return nullptr;
}
inline bool DetailAvailable(int resource, DetailId id) {
    const auto* detail = FindDetail(id);
    return detail && resource >= 0 && resource < 3 && (detail->resources & (1u << resource)) != 0;
}
} }
