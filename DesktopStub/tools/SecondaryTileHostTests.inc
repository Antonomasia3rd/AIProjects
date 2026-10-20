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
    Check(WriteContentFixture(L"[Notes.Resin]\r\nUID=800000001\r\nServer=os_asia\r\n"), "server migration uses a private synthetic profile");
    g_commandLine.settings = {{L"Notes.Resin", L"UID", L"900000001"}};
    PrepareCommandLineNotesServerChanges();
    Check(std::any_of(g_commandLine.settings.begin(), g_commandLine.settings.end(), [](const auto& setting) {
        return setting.key == L"Server" && setting.value.empty(); }), "a CLI UID replacement clears stale inferred region preference");
    g_commandLine.settings = {{L"Notes.Resin", L"Server", L"os_cht"}, {L"Notes.Resin", L"UID", L"900000001"}};
    PrepareCommandLineNotesServerChanges();
    Check(g_commandLine.settings.size() == 2 && g_commandLine.settings.front().value == L"os_cht", "an explicit server in the same command survives UID order");
    g_commandLine = savedOptions;
    Check(WriteContentFixture(original), "server mutation fixture restores original private data");
    Check(aip::content::TileActionCount == 9 && aip::content::FindTileAction(L" NEXT ") != nullptr,
        "nine explicit media actions have one normalized catalog");
    auto actionValue = std::wstring(L"volume-down");
    Check(NormalizeDesktopStubTypedSetting(L"Tile.smtc", L"ClickAction", actionValue, issue), "tile clicks accept typed media actions");
    actionValue = L"0";
    Check(!NormalizeDesktopStubTypedSetting(L"MediaActions", L"VolumeStepPercent", actionValue, issue), "zero volume step is rejected");
    actionValue = L"5";
    Check(NormalizeDesktopStubTypedSetting(L"MediaActions", L"VolumeStepPercent", actionValue, issue), "volume steps are configurable");
    {
        const auto savedWindow = g_hwnd;
        g_hwnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(1)); // Injected destination, never a native window.
        std::wstring document = L"[Content]\r\nEnabled=0\r\nCount=1\r\n";
        Check(WriteContentFixture(document), "appearance notice fixture starts from a synthetic profile");
        std::wstring fingerprint;
        Check(CurrentSecondaryAppearanceFingerprint(fingerprint), "poller reads one valid appearance-only fingerprint");
        g_secondaryAppearanceNotice.RecordScheduled(fingerprint); // Startup already scheduled its migration once.
        const auto posts = secondaryAppearancePosts, refreshes = secondaryAppearanceRefreshes;
        PollSecondaryTileAppearance();
        Check(secondaryAppearancePosts == posts, "unchanged startup appearance does not enumerate pins a second time");
        aip::WriteIniValueToText(document, L"Notes.Resin", L"UID", L"123456789");
        aip::WriteIniValueToText(document, L"Content.1", L"Text", L"Ordinary text changed");
        Check(WriteContentFixture(document), "unrelated raw INI edits publish to the private fixture");
        PollSecondaryTileAppearance();
        Check(secondaryAppearancePosts == posts, "account and ordinary text edits do not post an appearance refresh");
        aip::WriteIniValueToText(document, L"Tile.smtc", L"Branding", L"None");
        Check(WriteContentFixture(document), "source appearance raw edit publishes to the private fixture");
        PollSecondaryTileAppearance();
        Check(secondaryAppearancePosts == posts + 1 && secondaryAppearanceRefreshes == refreshes &&
            secondaryAppearanceDestination == g_hwnd && secondaryAppearanceMessage == WM_APP_SECONDARY_APPEARANCE_CHANGED,
            "polling only posts the main-window message; it cannot enumerate or mutate native pins");
        aip::WriteIniValueToText(document, L"MediaActions", L"Background", L"Color");
        Check(WriteContentFixture(document), "another appearance edit arrives before the UI consumes its notice");
        PollSecondaryTileAppearance(); PollSecondaryTileAppearance();
        Check(secondaryAppearancePosts == posts + 1, "pending appearance notices coalesce later edits");
        Check(SecondaryTileHandleMessage(g_hwnd, WM_APP_SECONDARY_APPEARANCE_CHANGED, 0, 0) &&
            secondaryAppearanceRefreshes == refreshes + 1, "main-window consumption invokes exactly one injected refresh");
        PollSecondaryTileAppearance();
        Check(secondaryAppearancePosts == posts + 1, "a consumed appearance key does not cause a native retry loop");
        aip::WriteIniValueToText(document, L"MediaActions", L"Branding", L"None");
        Check(WriteContentFixture(document), "message delivery retry fixture publishes another appearance change");
        secondaryAppearancePostSucceeds = false; PollSecondaryTileAppearance();
        secondaryAppearancePostSucceeds = true; PollSecondaryTileAppearance();
        Check(secondaryAppearancePosts == posts + 3, "failed posting retries the same key on the next poll");
        SecondaryTileHandleMessage(g_hwnd, WM_APP_SECONDARY_APPEARANCE_CHANGED, 0, 0);
        Check(secondaryAppearanceRefreshes == refreshes + 2, "post retry still produces only one UI refresh");
        g_hwnd = savedWindow;
        Check(WriteContentFixture(original), "appearance notice regression restores private profile and main-window identity");
    }
    {
        const std::wstring base = L"[Content]\r\nEnabled=0\r\nCount=1\r\n";
        g_commandLine = {};
        Check(WriteContentFixture(base) && ValidateDesktopStubContentProfile(false, issue),
            "missing MediaActions appearance fields use compatible raw-profile defaults");
        struct InvalidAppearance { const wchar_t* key; const wchar_t* bad; const wchar_t* good; };
        for (const auto& setting : {InvalidAppearance{L"Background", L"Wallpaper", L"Transparent"},
                InvalidAppearance{L"Branding", L"UnknownBranding", L"Name"},
                InvalidAppearance{L"BackgroundColor", L"#12GG56", L"#123456"}}) {
            const auto candidate = base + L"[MediaActions]\r\n\"" + setting.key + L"\"=\"" + setting.bad + L"\"\r\n";
            g_commandLine = {};
            Check(WriteContentFixture(candidate) && !ValidateDesktopStubContentProfile(false, issue) &&
                issue.find(std::wstring(L"MediaActions.") + setting.key) != std::wstring::npos,
                "raw INI load rejects invalid action appearance before native pin refresh");
            aip::content::Configuration composition; aip::content::SecondaryTileConfiguration tiles;
            Check(!ReadSecondaryTileMenuText(candidate, composition, tiles, issue),
                "tray candidate validation rejects pre-existing invalid action appearance");
            Check(!ValidateCommandLinePresetDocument(candidate, issue),
                "ordinary CLI transaction revalidates action appearance under the mutation lock");
            g_commandLine.presetName = L"Wallpaper";
            Check(!ValidateCommandLinePresetDocument(candidate, issue) &&
                issue.find(std::wstring(L"MediaActions.") + setting.key) != std::wstring::npos,
                "preset transaction validation rejects invalid action appearance in its candidate document");
            g_commandLine = {};
            g_commandLine.settings = {{L"MediaActions", setting.key, setting.good}};
            Check(ValidateDesktopStubContentProfile(true, issue),
                "a valid typed override repairs the corresponding raw-INI appearance field");
            std::wstring unchanged;
            Check(ReadIniFileForMutation(unchanged) && unchanged == candidate,
                "profile/candidate validation does not mutate the original INI");
        }
        g_commandLine = savedOptions;
        Check(WriteContentFixture(original), "action appearance regression restores the private fixture profile");
    }
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
    {
        auto branded = frame;
        branded.secondaryBranding = L"Name";
        const auto nameKey = ContentSnapshotKey(branded);
        ContentRenderScope render(&branded);
        Check(std::wstring(ContentLiveTileBrandingXmlValue()) == L"name", "secondary live tiles default to a name without the corner branding logo");
        render.snapshot.secondaryBranding = L"None";
        Check(std::wstring(ContentLiveTileBrandingXmlValue()) == L"none", "secondary tiles can hide both name and branding logo");
        render.snapshot.secondaryBranding = L"Logo";
        Check(std::wstring(ContentLiveTileBrandingXmlValue()) == L"logo", "secondary tiles can explicitly choose a branding logo");
        branded.secondaryBranding = L"NameAndLogo";
        Check(ContentSnapshotKey(branded) != nameKey, "a branding-only edit resends XML without requiring media text to change");
        render.snapshot.secondaryTileId.clear();
        render.snapshot.secondaryBranding.clear();
        Check(std::wstring(ContentLiveTileBrandingXmlValue()) == LiveTileBrandingXmlValue(), "legacy primary snapshots without an instance branding override retain the global setting");
    }
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
    {
        ContentRenderScope render;
        render.snapshot.textMode = aip::content::TextMode::Overlay;
        render.snapshot.detailRows = {{L"Resin", L"20/200", L""}, {L"Expeditions ready", L"1/5", L""}};
        const auto first = BuildGenerationStateKey(bitmapPath);
        render.snapshot.detailRows.back().secondary = L"2/5";
        Check(BuildGenerationStateKey(bitmapPath) != first, "changed secondary Notes detail invalidates baked image cache");
        const auto second = BuildGenerationStateKey(bitmapPath);
        render.snapshot.smtcSource = true;
        render.snapshot.smtcState = aip::content::MediaPlaybackState::Paused;
        Check(BuildGenerationStateKey(bitmapPath) != second, "explicit playback status invalidates the bitmap cache without parsing caption text");
    }
    Check(WriteContentFixture(original), "secondary fixture restores private configuration");
    PublishSecondarySnapshots({}); contentNativeQueueAvailable = direct;
    ContentEngineTick(false, false);
}
