// Pure async-result simulation plus inert cache/profile routing. No native
// pinning, package enumeration, gallery clear, media or network is invoked.
static void TestGalleryPinBatchHost()
{
    aip::content::SecondaryPinBatch batch;
    std::vector<std::wstring> begun;
    const auto begin = [&](const auto& request) { begun.push_back(request.first); return request.first != L"prepare-error"; };
    batch.Enqueue({{L"first", L"A"}, {L"second", L"B"}, {L"first", L"duplicate"}, {L"prepare-error", L"bad"}, {L"last", L"C"}});
    batch.Advance(begin); batch.Advance(begin);
    Check(begun == std::vector<std::wstring>{L"first"} && batch.Waiting() == 3, "pin batches deduplicate and begin only one native confirmation at a time");
    batch.Complete(L"unrelated"); batch.Advance(begin);
    Check(begun.size() == 1, "unrelated or late pin completions cannot advance the current batch item");
    batch.Complete(L"first"); // Windows No / declined result.
    batch.Advance(begin);
    Check(begun.back() == L"second" && begun.size() == 2, "declining one Windows pin request advances to the next item");
    batch.Complete(L"second"); // Error/cancel terminal result.
    batch.Advance(begin);
    Check(begun == std::vector<std::wstring>({L"first", L"second", L"prepare-error", L"last"}), "terminal errors and preparation failures do not strand the rest of a pin batch");
    batch.Complete(L"last"); batch.Advance(begin);
    Check(!batch.Busy(), "the last pin completion releases the batch");
    batch.Enqueue({{L"shutdown", L"D"}}); batch.Advance(begin); batch.Clear();
    Check(!batch.Busy(), "shutdown drops pending pin requests without launching another native prompt");
    aip::content::SecondaryPinInventory inventory;
    Check(!inventory.Known() && inventory.Snapshot().empty() && inventory.RequestDue(0) && !inventory.RequestDue(1),
        "unavailable pin inventory is distinct from confirmed empty and only one enumeration is requested");
    inventory.Complete(true, {L"AipSource_smtc", L"AipGallery_historical.old"}, 100);
    Check(inventory.Known() && inventory.Contains(L"AipGallery_historical.old") && !inventory.RequestDue(30099) && inventory.RequestDue(30100),
        "successful enumeration keeps all historical gallery IDs and waits thirty seconds before reconciliation");
    inventory.Complete(false, {}, 30200);
    Check(inventory.Known() && inventory.Contains(L"AipSource_smtc") && !inventory.RequestDue(60199),
        "enumeration failures retain the last successful snapshot with a bounded retry cadence");
    inventory.Mutation(L"AipAction_play", true); inventory.Mutation(L"AipSource_smtc", false);
    Check(inventory.Contains(L"AipAction_play") && !inventory.Contains(L"AipSource_smtc"), "own acknowledged pin and unpin update membership immediately");
    const auto oldIds = CachedSecondaryTileIds(); const auto oldKnown = SecondaryTileInventoryReady();
    std::wstring before; ReadIniFileForMutation(before);
    const auto savedCommand = g_commandLine; g_commandLine = {};
    const std::wstring profile = L"[Content]\r\nEnabled=0\r\nCount=1\r\n[Content.1]\r\nEnabled=1\r\nBackground=Transparent\r\nTextSources=CustomText\r\nText=Primary\r\n[Tiles]\r\nEnabled=0\r\n[Tile.smtc]\r\nEnabled=0\r\nContent=2\r\n";
    Check(WriteContentFixture(profile), "pin selection fixture uses the private synthetic INI");
    g_secondaryPinInventory.Complete(true, {L"AipSource_smtc"}, GetTickCount64());
    aip::content::Configuration content; aip::content::SecondaryTileConfiguration tiles; std::wstring error;
    Check(CompositionRequested() && ReadContentConfiguration(content, error, &tiles) && content.enabled && tiles.enabled && tiles.tiles[0].enabled &&
        content.entries.size() == 2 && content.entries[1].enabled && aip::content::EntryUsesSource(content.entries[1], aip::content::SourceId::Smtc),
        "a pinned source is composed despite saved engine/master/source enable flags and missing default content rows");
    Check(content.entries[0].custom.primary == L"Primary" && std::count_if(tiles.tiles.begin(), tiles.tiles.end(), [](const auto& tile) { return tile.enabled; }) == 1,
        "pin selection preserves primary fields and does not activate unpinned sources");
    g_secondaryPinInventory.Mutation(L"AipSource_smtc", false);
    Check(!CompositionRequested() && ReadContentConfiguration(content, error, &tiles) && !content.enabled,
        "unpinning the last source removes its background provider demand without changing saved configuration");
    std::wstring after; Check(ReadIniFileForMutation(after) && after == profile, "pin-driven eligibility never rewrites legacy configuration flags");
    HMENU menu = CreatePopupMenu(); AppendSecondaryTilesMenu(menu); AppendQuickActionsMenu(menu);
    std::map<UINT, TrayMenuProbe> commands; InspectTrayMenu(menu, commands);
    Check(!commands.count(ID_SECONDARY_TILES_ENABLED) && !commands.count(ID_SECONDARY_TILE_COMMANDS) && commands.count(ID_QUICK_ACTIONS_PIN_ALL),
        "source update checkboxes are retired and Quick Actions expose one explicit Pin all command");
    DestroyMenu(menu);
    Check(!RequestSecondaryTilePinBatch(nullptr, {{L"AipSource_smtc", L"Music"}}) &&
        !ClearGalleryQueuesForRebalance({L"AipGallery_historical.old"}, error), "default inert validation cannot pin or clear real Windows queues");
    g_secondaryPinInventory.Reset();
    if (oldKnown) g_secondaryPinInventory.Complete(true, oldIds, GetTickCount64());
    else for (const auto& id : oldIds) g_secondaryPinInventory.Mutation(id, true);
    g_commandLine = savedCommand;
    Check(WriteContentFixture(before), "pin batch fixture restores the original private profile");
}
