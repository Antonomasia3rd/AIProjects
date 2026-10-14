// Optional static secondary tiles; no extra resident or tray icon.
static constexpr UINT ID_QUICK_ACTIONS_ENABLED = 29000;
static constexpr UINT ID_QUICK_ACTIONS_VOLUME = 29001;
static constexpr UINT ID_QUICK_ACTIONS_CLICK = 29100;
static constexpr UINT ID_QUICK_ACTIONS_SETUP = 29199;
static constexpr UINT ID_QUICK_ACTIONS_PIN = 29200;
static void AppendQuickActionsMenu(HMENU parent)
{
    HMENU menu = aip::BeginTrayNestedMenu(parent);
    const bool enabled = IniReadI(L"QuickActions", L"Enabled", 0) != 0;
    aip::AppendTrayMenuItem(menu, ID_QUICK_ACTIONS_ENABLED, L"Enable pinning Quick Actions tiles", enabled, true);
    aip::AppendTrayMenuItem(menu, ID_QUICK_ACTIONS_VOLUME, L"Volume step (%)...", false, true);
    HMENU click = aip::BeginTrayNestedMenu(menu);
    const auto current = IniReadS(L"Tile.smtc", L"ClickAction", L"toggle");
    aip::AppendTrayMenuItem(click, ID_QUICK_ACTIONS_SETUP, L"Open source settings", current == L"setup", true);
    for (std::size_t index = 0; index < aip::content::TileActionCount; ++index) {
        const auto& action = aip::content::TileActions[index];
        aip::AppendTrayMenuItem(click, ID_QUICK_ACTIONS_CLICK + static_cast<UINT>(index), action.label, current == action.id, true);
    }
    aip::EndTrayNestedMenu(menu, click, L"Now playing tile click");
    for (std::size_t index = 0; index < aip::content::TileActionCount; ++index) {
        const auto& action = aip::content::TileActions[index];
        HMENU tile = aip::BeginTrayNestedMenu(menu);
        const bool available = SecondaryTilePinUiAvailable();
        const auto id = CanonicalSecondaryTileRuntimeId(L"AipAction_" + std::wstring(action.id));
        aip::AppendTrayMenuItem(tile, ID_QUICK_ACTIONS_PIN + static_cast<UINT>(index) * 2, L"Pin/update tile...", false, available && enabled);
        aip::AppendTrayMenuItem(tile, ID_QUICK_ACTIONS_PIN + static_cast<UINT>(index) * 2 + 1, L"Unpin from Start...", false, available && SecondaryTileExists(id));
        aip::EndTrayNestedMenu(menu, tile, action.label);
    }
    aip::EndTrayNestedMenu(parent, menu, L"Quick Actions");
}
static bool DispatchQuickActionsCommand(HWND owner, UINT command)
{
    if (command == ID_QUICK_ACTIONS_ENABLED) {
        SaveSecondaryTileMenuSettings(owner, {{L"QuickActions", L"Enabled", IniReadI(L"QuickActions", L"Enabled", 0) ? L"0" : L"1"}}); return true;
    }
    if (command == ID_QUICK_ACTIONS_VOLUME) {
        auto value = IniReadS(L"MediaActions", L"VolumeStepPercent", L"5");
        if (ShowTextInputDialog(owner, L"Media volume", L"Change the default output volume by this many percentage points (1–100)", value, true) == IDOK)
            SaveSecondaryTileMenuSettings(owner, {{L"MediaActions", L"VolumeStepPercent", value}});
        return true;
    }
    if (command == ID_QUICK_ACTIONS_SETUP || (command >= ID_QUICK_ACTIONS_CLICK && command < ID_QUICK_ACTIONS_CLICK + aip::content::TileActionCount)) {
        const auto value = command == ID_QUICK_ACTIONS_SETUP ? L"setup" : aip::content::TileActions[command - ID_QUICK_ACTIONS_CLICK].id;
        SaveSecondaryTileMenuSettings(owner, {{L"Tile.smtc", L"ClickAction", value}}); return true;
    }
    if (command >= ID_QUICK_ACTIONS_PIN && command < ID_QUICK_ACTIONS_PIN + 2 * aip::content::TileActionCount) {
        if ((command - ID_QUICK_ACTIONS_PIN) % 2 == 0 && !IniReadI(L"QuickActions", L"Enabled", 0)) return true;
        const auto& action = aip::content::TileActions[(command - ID_QUICK_ACTIONS_PIN) / 2];
        const auto id = CanonicalSecondaryTileRuntimeId(L"AipAction_" + std::wstring(action.id));
        if ((command - ID_QUICK_ACTIONS_PIN) % 2) RequestUnpinSecondaryTile(owner, id);
        else RequestPinSecondaryTile(owner, id, action.label);
        return true;
    }
    return false;
}
