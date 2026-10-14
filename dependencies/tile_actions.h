#pragma once
#include "content_engine.h"
namespace aip { namespace content {
struct TileActionDescriptor { const wchar_t* id; const wchar_t* label; };
inline constexpr TileActionDescriptor TileActions[] = {
    {L"toggle", L"Play / pause"}, {L"play", L"Play"}, {L"pause", L"Pause"},
    {L"previous", L"Previous track"}, {L"next", L"Next track"},
    {L"repeat", L"Cycle repeat mode"}, {L"shuffle", L"Toggle shuffle"},
    {L"volume-down", L"Volume down"}, {L"volume-up", L"Volume up"}
};
inline constexpr std::size_t TileActionCount = sizeof(TileActions) / sizeof(TileActions[0]);
inline const TileActionDescriptor* FindTileAction(std::wstring key) {
    key = Lower(Trim(std::move(key)));
    for (const auto& action : TileActions) if (key == action.id) return &action;
    return nullptr;
}
inline bool NormalizeTileActionSetting(const std::wstring& section, const std::wstring& key,
    std::wstring& value, std::wstring& error) {
    const auto s = Lower(section), k = Lower(key);
    if (s == L"quickactions" && k == L"enabled") {
        bool enabled = false;
        if (ParseBool(value, enabled)) { value = enabled ? L"1" : L"0"; return true; }
    } else if (s == L"mediaactions" && k == L"volumesteppercent") {
        int step = 0;
        if (ParseInt(value, 1, 100, step)) { value = std::to_wstring(step); return true; }
    } else if (s.compare(0, 5, L"tile.") == 0 && k == L"clickaction") {
        value = Lower(Trim(value));
        if (value == L"setup" || FindTileAction(value)) return true;
    }
    error = L"Invalid tile action setting: " + section + L"." + key; return false;
}
} }
