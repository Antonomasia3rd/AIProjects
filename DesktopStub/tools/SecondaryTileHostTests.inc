static void TestSecondaryTileHost(const std::wstring& bitmapPath)
{
    std::wstring original; ReadIniFileForMutation(original);
    std::wstring defaults = L"; preserve\r\n[Content]\r\nEnabled=0\r\nCount=1\r\n[Notes.Resin]\r\nUID=123456789\r\n";
    std::wstring issue;
    const auto savedOptions = g_commandLine;
    contentTestCommandLine = L"DesktopStub.exe /tileid AipSource_hsr";
    Check(ParseCommandLineOptions() && g_commandLine.tileSetupKey == L"hsr", "Windows-only tile metadata routes to the correct source");
    contentTestCommandLine = L"DesktopStub.exe --tile-setup genshin /tileid=AipSource_genshin";
    Check(ParseCommandLineOptions() && g_commandLine.tileSetupKey == L"genshin", "Windows tile metadata does not reject explicit source activation");
    contentTestCommandLine = L"DesktopStub.exe /tileid AipAction_volume_down";
    Check(ParseCommandLineOptions() && g_commandLine.mediaAction == L"volume-down", "Windows action metadata routes without driving audio in the parser");
    contentTestCommandLine = L"DesktopStub.exe /tileid";
    Check(!ParseCommandLineOptions(), "missing Windows tile metadata value is rejected");
    contentTestCommandLine = L"DesktopStub.exe"; g_commandLine = savedOptions;
    Check(aip::content::TileActionCount == 9 && aip::content::FindTileAction(L" NEXT ") != nullptr,
        "nine explicit media actions have one normalized catalog");
    auto actionValue = std::wstring(L"volume-down");
    Check(NormalizeDesktopStubTypedSetting(L"Tile.smtc", L"ClickAction", actionValue, issue), "tile clicks accept typed media actions");
    actionValue = L"0";
    Check(!NormalizeDesktopStubTypedSetting(L"MediaActions", L"VolumeStepPercent", actionValue, issue), "zero volume step is rejected");
    actionValue = L"5";
    Check(NormalizeDesktopStubTypedSetting(L"MediaActions", L"VolumeStepPercent", actionValue, issue), "volume steps are configurable");
    Check(CanonicalSecondaryTileRuntimeId(L"AipAction_volume-down") == L"AipAction_volume_down", "volume action uses a Windows-safe tile ID");
    Check(!RunNativeMediaAction(L"toggle", 5, issue) && !issue.empty(), "inert host cannot drive playback");
    std::wstring musicXml;
    Check(aip::content::AppendMediaTileBinding(musicXml, L"TileWide", {L"Title <&>", L"Artist", L"Paused - Player"}, L"ms-appx:///icon.png") &&
        musicXml.find(L"hint-weight=\"20\"") != std::wstring::npos && musicXml.find(L"Title &lt;&amp;&gt;") != std::wstring::npos,
        "music layout gives a right icon column and escapes text");
    Check(StageSecondaryTileDefaultLayout(defaults, true, issue) &&
        defaults.find(L"; preserve") != std::wstring::npos && defaults.find(L"UID=123456789") != std::wstring::npos,
        "new-user six-tile initialization preserves existing comments and account fields");
    const auto firstDefaults = defaults;
    Check(StageSecondaryTileDefaultLayout(defaults, true, issue) && defaults == firstDefaults,
        "repeated tile setup is an idempotent configuration change");
    const bool direct = contentNativeQueueAvailable;
    contentNativeQueueAvailable = true;
    const auto before = contentGenerationRequests;
    secondaryTestBatches.clear();
    { std::lock_guard<std::mutex> lock(g_secondaryDeliveryMutex); g_secondaryDelivered.clear(); }
    std::wstring document = L"[Content]\r\nEnabled=1\r\nCount=7\r\nCycleEnabled=1\r\nTextMode=Auto\r\n[Tiles]\r\nEnabled=1\r\n";
    for (int index = 1; index <= 7; ++index)
        document += L"[Content." + std::to_wstring(index) + L"]\r\nEnabled=1\r\nBackground=Image\r\nImagePath=" + bitmapPath +
            L"\r\nTextSources=CustomText\r\nText=Frame " + std::to_wstring(index) + L"\r\n";
    for (const auto& tile : aip::content::SecondaryTiles)
        document += L"[Tile." + std::wstring(tile.id) + L"]\r\nEnabled=" + (std::wstring(tile.id) == L"smtc" ? L"1" : L"0") + L"\r\n";
    Check(WriteContentFixture(document) && ContentEngineTick(), "one host composes primary and a mapped secondary without starting providers");
    Check(secondaryTestBatches.size() == 1 && secondaryTestBatches[0].size() == 2 &&
        secondaryTestBatches[0][0].secondaryTileId.empty() && secondaryTestBatches[0][1].secondaryTileId == L"AipSource_smtc",
        "primary and secondary destinations form one coherent generation batch");
    { ContentRenderScope scope; Check(scope.snapshot.text.primary == L"Frame 1", "separate tiles leave Content1 on the primary tile even with cycling enabled"); }
    Check(ContentEngineTick() && secondaryTestBatches.size() == 1 && contentGenerationRequests == before + 1,
        "unchanged secondary targets do not create repeated updates");
    aip::WriteIniValueToText(document, L"Content.2", L"Text", L"Changed secondary");
    Check(WriteContentFixture(document) && ContentEngineTick() && secondaryTestBatches.size() == 2 && secondaryTestBatches.back().size() == 1 &&
        secondaryTestBatches.back()[0].secondaryTileId == L"AipSource_smtc", "only the changed secondary is updated");
    const auto frame = secondaryTestBatches.back()[0]; ForgetSecondaryTileDelivery(frame.secondaryTileId);
    secondaryTestPinned = false;
    Check(PrepareSecondaryBatch({frame}).empty(), "unpinned destinations cannot consume an update acknowledgement");
    secondaryTestPinned = true;
    Check(PrepareSecondaryBatch({frame}).size() == 1, "a newly pinned destination can receive the current snapshot");
    std::vector<aip::content::Text> headlines;
    for (int index=0; index<5; ++index) headlines.push_back({L"Title <&> " + std::to_wstring(index), L"Summary", L""});
    std::wstring medium, large;
    Check(aip::content::AppendRssTileBinding(medium, L"TileMedium", 150, headlines, L"") &&
        medium.find(L"3. Title") != std::wstring::npos && medium.find(L"4. Title") == std::wstring::npos &&
        medium.find(L"&lt;&amp;&gt;") != std::wstring::npos, "medium RSS stacks three escaped headlines");
    Check(aip::content::AppendRssTileBinding(large, L"TileLarge", 310, headlines, L"") &&
        large.find(L"5. Title") != std::wstring::npos && large.find(L"captionSubtle") != std::wstring::npos,
        "large RSS stacks five headlines with short summaries");
    Check(WriteContentFixture(original), "secondary fixture restores private configuration");
    PublishSecondarySnapshots({}); contentNativeQueueAvailable = direct;
    ContentEngineTick(false, false);
}
