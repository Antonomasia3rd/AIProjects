// Optional static secondary tiles; no extra resident or tray icon.
static constexpr UINT ID_QUICK_ACTIONS_VOLUME = 29001;
static constexpr UINT ID_QUICK_ACTIONS_CLICK = 29100;
static constexpr UINT ID_QUICK_ACTIONS_SETUP = 29199;
static constexpr UINT ID_QUICK_ACTIONS_PIN = 29200;
static constexpr UINT QUICK_ACTIONS_PIN_STRIDE = 3;
static constexpr UINT ID_QUICK_ACTIONS_APPEARANCE = 29300;
static constexpr UINT ID_QUICK_ACTIONS_PIN_ALL = 29320;
static void AppendQuickActionsMenu(HMENU parent)
{
    HMENU menu = aip::BeginTrayNestedMenu(parent);
    aip::AppendTrayMenuItem(menu, ID_QUICK_ACTIONS_PIN_ALL, SecondaryTileUiString(L"QuickActionsPinAll", L"Pin all Quick Actions..."), false, SecondaryTilePinUiAvailable());
    AppendTileAppearanceMenu(menu, L"MediaActions", ID_QUICK_ACTIONS_APPEARANCE);
    aip::AppendTrayMenuItem(menu, ID_QUICK_ACTIONS_VOLUME, L"Volume step (%)...", false, true);
    if (!TileInstanceModeRequested()) {
        HMENU click = aip::BeginTrayNestedMenu(menu);
        const auto current = IniReadS(L"Tile.smtc", L"ClickAction", L"toggle");
        aip::AppendTrayMenuItem(click, ID_QUICK_ACTIONS_SETUP, L"Open source settings", current == L"setup", true);
        for (std::size_t index = 0; index < aip::content::TileActionCount; ++index) {
            const auto& action = aip::content::TileActions[index];
            aip::AppendTrayMenuItem(click, ID_QUICK_ACTIONS_CLICK + static_cast<UINT>(index), action.label, current == action.id, true);
        }
        aip::EndTrayNestedMenu(menu, click, L"Now playing tile click");
    }
    for (std::size_t index = 0; index < aip::content::TileActionCount; ++index) {
        const auto& action = aip::content::TileActions[index];
        HMENU tile = aip::BeginTrayNestedMenu(menu);
        const auto id = CanonicalSecondaryTileRuntimeId(L"AipAction_" + std::wstring(action.id));
        const auto base = ID_QUICK_ACTIONS_PIN + static_cast<UINT>(index) * QUICK_ACTIONS_PIN_STRIDE;
        AppendSecondaryTilePinCommands(tile, id, base, base + 1, base + 2);
        aip::EndTrayNestedMenu(menu, tile, action.label);
    }
    aip::EndTrayNestedMenu(parent, menu, L"Quick Actions");
}
static bool DispatchQuickActionsCommand(HWND owner, UINT command)
{
    if (command == ID_QUICK_ACTIONS_PIN_ALL) {
        std::vector<std::pair<std::wstring, std::wstring>> requests;
        for (const auto& action : aip::content::TileActions)
            requests.emplace_back(CanonicalSecondaryTileRuntimeId(L"AipAction_" + std::wstring(action.id)), action.label);
        RequestSecondaryTilePinBatch(owner, requests); return true;
    }
    if (DispatchTileAppearanceCommand(owner, command, ID_QUICK_ACTIONS_APPEARANCE, L"MediaActions")) return true;
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
    if (command >= ID_QUICK_ACTIONS_PIN && command < ID_QUICK_ACTIONS_PIN + QUICK_ACTIONS_PIN_STRIDE * aip::content::TileActionCount) {
        const auto& action = aip::content::TileActions[(command - ID_QUICK_ACTIONS_PIN) / QUICK_ACTIONS_PIN_STRIDE];
        const auto id = CanonicalSecondaryTileRuntimeId(L"AipAction_" + std::wstring(action.id));
        const auto operation = (command - ID_QUICK_ACTIONS_PIN) % QUICK_ACTIONS_PIN_STRIDE;
        if (operation == 2) RequestSecondaryTileList(owner);
        else if (operation == 1) RequestUnpinSecondaryTile(owner, id);
        else RequestPinSecondaryTile(owner, id, action.label);
        return true;
    }
    return false;
}
