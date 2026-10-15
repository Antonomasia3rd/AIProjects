// Included after the real host and shared fixture helpers. These seams never
// open Explorer, inspect user folders, initialize COM or request a native pin.
static std::set<std::wstring> gallerySetupTestDirectories;
static std::wstring gallerySetupTestOpened;
static unsigned gallerySetupTestOpenCount = 0;
static bool GallerySetupTestOpenFolder(const std::wstring& path)
{
    ++gallerySetupTestOpenCount; gallerySetupTestOpened = path; return true;
}
static bool GallerySetupTestDirectoryExists(const std::wstring& path)
{
    return gallerySetupTestDirectories.count(path) != 0;
}
static void TestGallerySetupHost()
{
    std::wstring original; ReadIniFileForMutation(original);
    const auto savedOptions = g_commandLine;
    const auto savedHistory = g_galleryPinHistory;
    const auto savedDraft = g_gallerySetupDraft;
    const auto savedCommand = contentTestCommandLine;
    g_commandLine = {};
    std::wstring issue;
    const std::wstring base = L"; keep this comment\r\n[Content]\r\nEnabled=0\r\nCount=1\r\n";
    aip::gallery::Configuration configuration;
    Check(ReadGalleryMenuText(base, configuration, issue) && !configuration.enabled && configuration.groupId.empty(),
        "absent gallery sections stay disabled without creating ownership history");
    auto staged = base;
    Check(StageGalleryMenuSettings(staged, {{L"Gallery", L"GroupId", L"g_a"}, {L"Gallery", L"Folder", L"pictures"},
        {L"Gallery", L"TileCount", L"3"}, {L"Gallery.Tile.1", L"Id", L"tile_1"}, {L"Gallery.Tile.2", L"Id", L"tile_2"},
        {L"Gallery.Tile.3", L"Id", L"tile_3"}, {L"Gallery.Tile.1", L"Name", L"Nature & sky"},
        {L"Gallery.Tile.2", L"Name", L"Travel"}, {L"Gallery.Tile.3", L"Name", L"Family"}, {L"Gallery", L"Enabled", L"1"}}, issue),
        "gallery settings stage atomically using the same typed INI writer as CLI");
    Check(ReadGalleryMenuText(staged, configuration, issue) && configuration.tiles.size() == 3 &&
        configuration.tiles[0].name == L"Nature & sky" && staged.find(L"; keep this comment") == 0,
        "three explicit named tiles preserve unrelated profile text");
    const auto documentValue = [](const std::wstring& text, const wchar_t* section, const wchar_t* key) {
        std::wstring value; aip::ReadIniValueFromDoc(aip::ParseIniDocument(text), section, key, value); return value;
    };
    auto firstSetup = base + L"[Tiles]\r\nEnabled=0\r\n[Content.1]\r\nName=My primary\r\nBackground=Transparent\r\nTextSources=CustomText\r\nText=Keep primary text\r\n[Notes.Resin]\r\nUID=800000001\r\n";
    std::vector<aip::IniSetting> setupSettings;
    Check(PrepareGalleryConfigurationSettings(configuration, setupSettings, issue) && StageGalleryInitialLayout(firstSetup, issue) &&
        StageGalleryMenuSettings(firstSetup, setupSettings, issue) && ValidateDesktopStubProfileDocument(firstSetup, issue),
        "fresh Count1 disabled content/tile profile becomes a completely valid gallery setup candidate");
    Check(documentValue(firstSetup, L"Content", L"Count") == L"1" && documentValue(firstSetup, L"Content", L"Enabled") == L"1" &&
        documentValue(firstSetup, L"Tiles", L"Enabled") == L"1" && documentValue(firstSetup, L"Content.1", L"Name") == L"My primary" &&
        documentValue(firstSetup, L"Content.1", L"Background") == L"Transparent" && documentValue(firstSetup, L"Content.1", L"Text") == L"Keep primary text" &&
        documentValue(firstSetup, L"Notes.Resin", L"UID") == L"800000001", "gallery baseline preserves primary content and accounts without growing Count for unused sources");
    for (const auto& source : aip::content::SecondaryTiles)
        Check(documentValue(firstSetup, aip::content::SecondaryTileSection(source.id).c_str(), L"Enabled") == L"0",
            "first gallery setup does not activate an unrequested default source");
    auto explicitSource = base + L"[Tile.smtc]\r\nEnabled=1\r\nContent=4\r\n[Content.4]\r\nName=Saved media\r\nEnabled=0\r\nTextSources=CustomText\r\nText=Saved paused source\r\n";
    Check(StageGalleryInitialLayout(explicitSource, issue) && documentValue(explicitSource, L"Content", L"Count") == L"4" &&
        documentValue(explicitSource, L"Tile.smtc", L"Enabled") == L"1" && documentValue(explicitSource, L"Content.4", L"Enabled") == L"0" &&
        documentValue(explicitSource, L"Content.4", L"TextSources") == L"CustomText" && documentValue(explicitSource, L"Content.4", L"Text") == L"Saved paused source",
        "existing source flags/mappings are preserved and Count grows only as required without rewriting mapped content");
    auto legacyText = base + L"[TileText]\r\nEnabled=1\r\nText=Legacy title\r\nSecondaryText=Legacy detail\r\nBadgeText=7\r\n";
    Check(StageGalleryInitialLayout(legacyText, issue) && documentValue(legacyText, L"Content.1", L"TextSources") == L"CustomText" &&
        documentValue(legacyText, L"Content.1", L"Text") == L"Legacy title" && documentValue(legacyText, L"Content.1", L"SecondaryText") == L"Legacy detail" &&
        documentValue(legacyText, L"Content.1", L"BadgeText") == L"7", "uncomposed legacy primary text migrates only into absent numbered fields");
    auto legacyRss = base + L"[Settings]\r\nContentSource=RssFeed\r\n[RssFeed]\r\nFeedUrl=https://example.invalid/feed\r\n";
    Check(StageGalleryInitialLayout(legacyRss, issue) && documentValue(legacyRss, L"Content.1", L"TextSources") == L"RssFeed" &&
        documentValue(legacyRss, L"RssFeed", L"FeedUrl") == L"https://example.invalid/feed", "legacy RSS remains the primary text source without changing its feed configuration");
    auto hiddenLegacy = base + L"[TileText]\r\nEnabled=0\r\nText=Saved disabled text\r\n";
    Check(StageGalleryInitialLayout(hiddenLegacy, issue) && documentValue(hiddenLegacy, L"Content.1", L"TextSources").empty() &&
        documentValue(hiddenLegacy, L"Content.1", L"Text") == L"Saved disabled text", "legacy text disabled by the user stays inactive while its saved value is preserved");
    auto invalidCombined = staged;
    Check(!StageGalleryMenuSettings(invalidCombined, {{L"Tiles", L"Enabled", L"1"}}, issue) && invalidCombined == staged,
        "gallery saves reject invalid enabled source mappings using complete-profile validation");
    for (const auto& setting : std::vector<aip::IniSetting>{{L"Gallery", L"TileCount", L"0"}, {L"Gallery", L"TileCount", L"9"},
        {L"Gallery", L"QueueSize", L"6"}, {L"Gallery", L"RefreshSeconds", L"59"}, {L"Gallery", L"Order", L"Fast"},
        {L"Gallery", L"Fit", L"Stretch"}, {L"Gallery.Tile.1", L"Name", L"bad\nname"},
        {L"Gallery.Tile.2", L"Id", L"tile_1"}, {L"Gallery.Tile.1", L"Branding", L"Bad"},
        {L"Gallery.Tile.1", L"BackgroundColor", L"#XYZ123"}}) {
        auto candidate = staged;
        Check(!StageGalleryMenuSettings(candidate, {setting}, issue) && candidate == staged,
            "invalid range, duplicate identity, multiline name and appearance leave the complete candidate unchanged");
    }
    auto appearance = staged;
    Check(StageGalleryMenuSettings(appearance, {{L"Gallery.Tile.1", L"Background", L"Color"},
        {L"Gallery.Tile.1", L"BackgroundColor", L"#123456"}, {L"Gallery.Tile.1", L"Branding", L"None"}}, issue) &&
        ReadGalleryMenuText(appearance, configuration, issue) && configuration.tiles[0].appearance.background == L"Color" &&
        configuration.tiles[0].appearance.branding == L"None", "gallery slots have the same typed appearance controls as source tiles");
    Check(ValidateCommandLinePresetDocument(staged, issue), "raw valid gallery profile passes locked CLI candidate validation");
    auto badRaw = staged; WriteContentProfileSetting(badRaw, {L"Gallery.Tile.2", L"Branding", L"invalid"});
    Check(!ValidateCommandLinePresetDocument(badRaw, issue), "raw invalid gallery appearance is rejected without needing a preset");
    Check(WriteContentFixture(badRaw) && !ValidateDesktopStubContentProfile(false, issue),
        "raw invalid gallery appearance also fails ordinary complete-profile loading");
    auto shrinking = staged;
    Check(StageGalleryMenuSettings(shrinking, {{L"Gallery", L"TileCount", L"1"}}, issue) &&
        shrinking.find(L"tile_3") != std::wstring::npos && StageGalleryMenuSettings(shrinking, {{L"Gallery", L"TileCount", L"3"}}, issue) &&
        ReadGalleryMenuText(shrinking, configuration, issue) && configuration.tiles[2].id == L"tile_3",
        "reducing and restoring visible slots preserves stable numbered identities and names");
    aip::gallery::Configuration fresh; Check(ReadGalleryMenuText(base, fresh, issue), "fresh setup configuration reads");
    aip::gallery::Ledger ledger;
    Check(aip::gallery::CreateLedger(L"retained_group", L"epoch", true, ledger, issue), "safe setup recovery uses a pure valid empty ledger");
    GalleryPinHistory history;
    Check(!GallerySetupHistoryAllowed(fresh, aip::gallery::runtime::ReadResult::Missing, ledger, history, issue),
        "an empty uninitialized native cache does not attest no historical pins");
    history.ready = true; history.pending = true;
    Check(!GallerySetupHistoryAllowed(fresh, aip::gallery::runtime::ReadResult::Missing, ledger, history, issue),
        "an enumeration still in flight cannot authorize first setup");
    history.pending = false; history.error = L"unavailable";
    Check(!GallerySetupHistoryAllowed(fresh, aip::gallery::runtime::ReadResult::Missing, ledger, history, issue),
        "native enumeration errors never become empty ownership history");
    history.error.clear(); history.ids.insert(L"AipGallery_unknown.historical");
    Check(!GallerySetupHistoryAllowed(fresh, aip::gallery::runtime::ReadResult::Missing, ledger, history, issue),
        "unknown historical gallery pins block replacement history even outside the current config");
    history.ids.clear();
    Check(GallerySetupHistoryAllowed(fresh, aip::gallery::runtime::ReadResult::Missing, ledger, history, issue),
        "successful all-prefix empty enumeration permits new create-only history");
    Check(GallerySetupHistoryAllowed(fresh, aip::gallery::runtime::ReadResult::Ok, ledger, history, issue),
        "unused durable ledger can survive a failed INI save and be reused without replacement");
    auto oldLedger = ledger; oldLedger.images.emplace(L"photo", aip::gallery::Reservation{L"old_tile", 1}); oldLedger.nextOrdinal = 2;
    oldLedger.tiles.emplace(L"old_tile", aip::gallery::TileLedger{});
    Check(!GallerySetupHistoryAllowed(fresh, aip::gallery::runtime::ReadResult::Ok, oldLedger, history, issue),
        "a populated ledger is never treated as first setup even when every old tile is unpinned");
    Check(!GallerySetupHistoryAllowed(fresh, aip::gallery::runtime::ReadResult::Error, ledger, history, issue),
        "missing-versus-corrupt history has distinct safe setup behavior");
    fresh.groupId = L"already_configured";
    Check(!GallerySetupHistoryAllowed(fresh, aip::gallery::runtime::ReadResult::Missing, ledger, history, issue),
        "an established configuration with a lost ledger never creates replacement history");
    Check(WriteContentFixture(staged), "gallery activation fixture uses only the existing private test INI");
    const auto id = aip::gallery::NativeTileId(L"g_a", L"tile_1");
    Check(CanonicalSecondaryTileRuntimeId(id) == id && IsKnownSecondaryMetadataId(id),
        "known full native gallery IDs participate in native pin and metadata routing");
    Check(CanonicalSecondaryTileRuntimeId(L"AipGallery_g.a_tile_1").empty() &&
        CanonicalSecondaryTileRuntimeId(L"AipGallery_unknown.tile_1").empty(),
        "gallery components containing underscores are never guessed or split");
    Check(!GalleryTilePinReady(id), "a valid configuration alone cannot pin without durable ownership history");
    std::wstring fingerprint, changed;
    Check(CurrentSecondaryAppearanceFingerprint(fingerprint), "gallery identity and appearance enter the immutable metadata fingerprint");
    auto changedText = staged; WriteContentProfileSetting(changedText, {L"Gallery", L"Folder", L"different pictures"});
    WriteContentProfileSetting(changedText, {L"Gallery.Tile.1", L"Name", L"New ordinary name"});
    Check(WriteContentFixture(changedText) && CurrentSecondaryAppearanceFingerprint(changed) && fingerprint == changed,
        "ordinary gallery names and folders do not trigger native appearance enumeration");
    WriteContentProfileSetting(changedText, {L"Gallery.Tile.1", L"Branding", L"Logo"});
    Check(WriteContentFixture(changedText) && CurrentSecondaryAppearanceFingerprint(changed) && fingerprint != changed,
        "raw INI gallery appearance changes trigger the existing coalesced native migration path");
    Check(WriteContentFixture(staged), "restore known gallery activation profile");
    const auto parsed = [&](const std::wstring& arguments) {
        contentTestCommandLine = L"DesktopStub.exe " + arguments; return ParseCommandLineOptions();
    };
    Check(parsed(L"--open-folder \"holiday pictures\"") && g_commandLine.galleryFolderActivation && g_commandLine.openFolder == L"holiday pictures",
        "open-folder accepts a quoted relative directory without a provider or resident action");
    const auto relative = aip::MakeAbsolutePath(ParentDirectoryOfPath(g_iniPath) + L"\\holiday pictures");
    gallerySetupTestDirectories.insert(relative);
    std::wstring resolved;
    Check(ResolveGalleryFolderActivation(g_commandLine, resolved, issue) && resolved == relative,
        "relative folder activation resolves beside the selected INI rather than the working directory");
    const auto opens = gallerySetupTestOpenCount;
    Check(OpenGalleryFolder(resolved) && gallerySetupTestOpenCount == opens + 1 && gallerySetupTestOpened == relative,
        "folder activation uses the injected Explorer boundary without executing commands");
    Check(!OpenGalleryFolder(relative + L".missing") && gallerySetupTestOpenCount == opens + 1,
        "missing folders fail before the external open boundary");
    Check(parsed(L"/tileid " + id) && g_commandLine.galleryFolderActivation && g_commandLine.openFolder.empty(),
        "Windows metadata-only gallery activation is deferred until the selected profile is available");
    const auto configuredFolder = aip::MakeAbsolutePath(ParentDirectoryOfPath(g_iniPath) + L"\\pictures");
    gallerySetupTestDirectories.insert(configuredFolder);
    Check(ResolveGalleryFolderActivation(g_commandLine, resolved, issue) && resolved == configuredFolder,
        "metadata-only activation opens the current configured gallery directory");
    Check(parsed(L"--open-folder \"stale old location\" /tileid " + id) && ResolveGalleryFolderActivation(g_commandLine, resolved, issue) && resolved == configuredFolder,
        "known pin activation follows a changed configured folder instead of stale static arguments");
    Check(parsed(L"/tileid AipGallery_foreign.tile") && !ResolveGalleryFolderActivation(g_commandLine, resolved, issue),
        "unknown historical tile activation is refused without a resident fallback or arbitrary ID parsing");
    for (const auto* arguments : {L"--open-folder", L"--open-folder=", L"--open-folder x --configure-only",
        L"--open-folder x --set Gallery.QueueSize=2", L"--open-folder x --render-only", L"--open-folder x --media-action next",
        L"--open-folder x --tile-setup smtc", L"--open-folder x /tileid AipSource_smtc", L"--open-folder x --exit"})
        Check(!parsed(arguments), "folder activation rejects incomplete and incompatible mutations/actions");
    Check(parsed(L"--help --open-folder pictures --configure-only") && g_commandLine.helpRequested,
        "help stays side-effect-free even when other inert command flags are present");
    Check(parsed(L"--configure-only --set Gallery.TileCount=3 --set Gallery.QueueSize=5 --set Gallery.Tile.1.Branding=None"),
        "gallery controls share normal configure-only typed CLI settings");
    Check(!parsed(L"--configure-only --set Gallery.Tile.1.Branding=Invalid"), "CLI rejects an unknown gallery branding choice before persistence");
    g_commandLine = {}; HMENU menu = CreatePopupMenu();
    AppendGalleryTilesMenu(menu);
    std::map<UINT, TrayMenuProbe> commands; InspectTrayMenu(menu, commands);
    Check(commands.count(ID_GALLERY_FOLDER) && commands.count(ID_GALLERY_SEED) && commands.count(ID_GALLERY_CADENCE) &&
        commands.count(ID_GALLERY_TILE + 12), "gallery tray contains folder, shuffle seed, cadence and shared per-tile branding controls");
    Check((commands.at(ID_GALLERY_TILE + 2).state & (MFS_DISABLED | MFS_GRAYED)) != 0,
        "default inert menus cannot request native pinning even with valid gallery configuration");
    DestroyMenu(menu);
    Check(!RequestGalleryPinHistory(nullptr), "default inert fixture cannot enumerate real package tiles");
    const auto* photo = aip::tile_visual::FindTopic(L"gallery");
    Check(photo && aip::tile_visual::NativeGlyphFor(photo->glyph).primary == 0xe91b,
        "gallery fallback uses the documented native Photo glyph with existing vector fallback");
    gallerySetupTestDirectories.clear(); gallerySetupTestOpened.clear();
    g_galleryPinHistory = savedHistory; g_gallerySetupDraft = savedDraft;
    g_commandLine = savedOptions; contentTestCommandLine = savedCommand;
    Check(WriteContentFixture(original), "gallery setup fixture restores the original private profile");
}
