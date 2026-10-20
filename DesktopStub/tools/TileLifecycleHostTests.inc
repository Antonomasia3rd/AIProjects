// Private configuration and injected cleanup callbacks only. Never invokes
// Appx_Clear_LiveTileBare, Windows pin enumeration, or native notifications.
#include "../../dependencies/content_sources/notes_tile_account_import.h"
static void TestTileNotesImportPlanHost()
{
    using namespace aip::notes;
    struct Imported { std::wstring key, value; };
    const std::vector<Imported> imported{{L"UID",L"800000009"},{L"Server",L"os_usa"},
        {L"LTokenV2",L"synthetic-new-token"},{L"LTuid_v2",L"200000001"},{L"RefreshSeconds",L"120"}};
    const std::wstring before = L"[Tiles]\r\nInstances=g1,h1,z1\r\n"
        L"[Tile.g1]\r\nType=Genshin\r\nAccount=Account.shared\r\nNotes.UID=800000001\r\nNotes.Server=os_asia\r\nNotes.LToken=stale-local\r\nNotes.ltoken_v2=stale-alias\r\n"
        L"[Tile.h1]\r\nType=HSR\r\nAccount=Account.shared\r\nNotes.UID=800000002\r\nNotes.Server=prod_official_asia\r\n"
        L"[Tile.z1]\r\nType=ZZZ\r\nAccount=Account.other\r\nNotes.UID=100000003\r\nNotes.Server=prod_gf_jp\r\n"
        L"[Account.shared]\r\nLToken=shared-old\r\nLTUid=100000001\r\nLTokenV2Protected=dpapi:aa\r\n"
        L"[Account.other]\r\nLToken=other-token\r\nLTUid=300000001\r\n";
    const auto apply = [&](const TileNotesImportPlan& plan, std::wstring& output) {
        auto candidate = before;
        for (const auto& key : plan.removeKeys) if (!aip::RemoveIniValueFromText(candidate,key.section.c_str(),key.key.c_str())) return false;
        for (const auto& setting : plan.settings)
            if (!aip::WriteIniValueToText(candidate,setting.section.c_str(),setting.key.c_str(),setting.value)) return false;
        output = std::move(candidate); return true;
    };
    TileNotesImportPlan shared; std::wstring error, after;
    Check(BuildTileNotesImportPlan(L"g1",L"Account.shared",imported,shared,error) && apply(shared,after),
        "explicit sign-in import stages and applies a pure shared-login plan");
    auto document = aip::ParseIniDocument(after);
    const auto read = [&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback) {
        std::wstring value; return aip::ReadIniValueFromDoc(document,section,key,value) ? value : std::wstring(fallback);
    };
    std::wstring absent;
    Check(shared.accountSection == L"Account.shared" && read(L"Tile.g1",L"Notes.UID",L"") == L"800000009" &&
        read(L"Tile.g1",L"Notes.Server",L"") == L"os_usa" && read(L"Tile.h1",L"Notes.UID",L"") == L"800000002" &&
        read(L"Tile.h1",L"Notes.Server",L"") == L"prod_official_asia" && !aip::ReadIniValueFromDoc(document,L"Account.shared",L"UID",absent) &&
        !aip::ReadIniValueFromDoc(document,L"Account.shared",L"Server",absent),
        "shared login import stores game identity only on the selected tile and preserves sibling game UID/server");
    Check(!aip::ReadIniValueFromDoc(document,L"Tile.g1",L"Notes.LToken",absent) &&
        !aip::ReadIniValueFromDoc(document,L"Tile.g1",L"Notes.ltoken_v2",absent) &&
        !aip::ReadIniValueFromDoc(document,L"Account.shared",L"LTokenV2Protected",absent) &&
        read(L"Account.shared",L"LToken",L"") == L"synthetic-new-token" && read(L"Account.other",L"LToken",L"") == L"other-token",
        "import removes local and shared stale credential aliases while keeping another selected account intact");
    aip::content::TileInstanceConfiguration cfg;
    Check(aip::content::ReadTileInstanceConfiguration(read,cfg,error) && cfg.tiles.size() == 3 &&
        cfg.tiles[0].configuration.notes.accounts[0].uid == L"800000009" &&
        cfg.tiles[1].configuration.notes.accounts[1].uid == L"800000002" &&
        cfg.tiles[1].configuration.notes.accounts[1].token == L"synthetic-new-token" &&
        cfg.tiles[2].configuration.notes.accounts[2].token == L"other-token",
        "real instance reader uses shared credentials with separate per-game identities after import");
    TileNotesImportPlan legacy;
    Check(BuildTileNotesImportPlan(L"g1",L"Notes.Resin",imported,legacy,error) && apply(legacy,after),
        "legacy per-game account target remains supported by the pure import plan");
    document = aip::ParseIniDocument(after);
    Check(read(L"Notes.Resin",L"UID",L"") == L"800000009" && read(L"Notes.Resin",L"Server",L"") == L"os_usa" &&
        read(L"Tile.g1",L"Account",L"") == L"Notes.Resin" && !aip::ReadIniValueFromDoc(document,L"Tile.g1",L"Notes.UID",absent) &&
        read(L"Account.shared",L"LToken",L"") == L"shared-old",
        "legacy target saves the complete game account and removes selected tile overrides without changing a different shared account");
    TileNotesImportPlan own;
    Check(BuildTileNotesImportPlan(L"g1",L"",imported,own,error) && own.accountSection == L"Account.g1",
        "new independent sign-in has a stable tile-specific account target");
    const auto savedSize = own.settings.size(); const auto savedSection = own.accountSection;
    const std::vector<Imported> incomplete{{L"UID",L"800000009"},{L"LToken",L"synthetic-only-token"}};
    Check(!BuildTileNotesImportPlan(L"g1",L"Account.shared",incomplete,own,error) && own.accountSection == savedSection && own.settings.size() == savedSize &&
        !BuildTileNotesImportPlan(L"../invalid",L"Account.shared",imported,own,error) &&
        !BuildTileNotesImportPlan(L"g1",L"Settings",imported,own,error), "invalid or incomplete imports preserve the caller's previous complete plan");
    Check(imported.front().value == L"800000009" && imported[2].key == L"LTokenV2" && imported[2].value == L"synthetic-new-token",
        "import planning does not rewrite the caller's original cookie fields");
}
static void TestTileLifecycleHost(const std::wstring& bitmapPath)
{
    TestTileNotesImportPlanHost();
    using namespace aip::content;
    std::wstring original, error; Check(ReadIniFileForMutation(original), "lifecycle fixture captures its private profile");
    const auto ledgerPath = aip::gallery::runtime::LedgerPath(aip::MakeAbsolutePath(g_iniPath));
    std::vector<BYTE> oldLedger;
    const bool ledgerExisted = aip::ReadWholeFileBytes(ledgerPath, oldLedger, aip::gallery::MaxLedgerBytes);
    const std::wstring profile = L"[Content]\r\nEnabled=0\r\nCount=1\r\n[Gallery]\r\nEnabled=0\r\nGroupId=lifecycle\r\nTileCount=2\r\n"
        L"[Gallery.Tile.1]\r\nId=active\r\nName=One\r\nFolder=C:\\synthetic\r\n"
        L"[Gallery.Tile.2]\r\nId=disabled\r\nName=Two\r\nEnabled=0\r\nFolder=C:\\synthetic\r\n";
    Check(WriteContentFixture(profile), "lifecycle fixture uses disabled gallery configuration without scanning it");
    aip::gallery::Ledger ledger;
    Check(aip::gallery::CreateLedger(L"lifecycle", L"cleanup_epoch", true, ledger, error), "lifecycle fixture creates synthetic ownership in memory");
    ledger.tiles.emplace(L"historical", aip::gallery::TileLedger{});
    std::string encoded;
    Check(aip::gallery::SerializeLedger(ledger, encoded, error) && WriteWholeFileBytes(ledgerPath, std::vector<BYTE>(encoded.begin(), encoded.end())),
        "lifecycle fixture publishes only its private historical owner ledger");
    std::vector<BYTE> photoBefore; Check(aip::ReadWholeFileBytes(bitmapPath, photoBefore), "lifecycle fixture captures private image bytes");
    const auto owned = OwnedSecondaryTileIdsForClear();
    Check(owned.count(L"AipGallery_lifecycle.active") && owned.count(L"AipGallery_lifecycle.disabled") &&
        owned.count(L"AipGallery_lifecycle.historical") && owned.count(L"AipSource_rss") && !owned.count(L"AipGallery_other.active"),
        "cleanup includes disabled and historical owned galleries without claiming another profile");
    std::set<std::wstring> pinned{L"AipGallery_lifecycle.active", L"AipGallery_lifecycle.disabled", L"AipGallery_lifecycle.historical", L"AipSource_rss", L"AipGallery_other.active"};
    const auto pinsBefore = pinned;
    std::set<std::wstring> cleared;
    const auto failures = ClearOwnedSecondaryTileNotifications(owned,
        [&](const auto& id) { return pinned.count(id) != 0; },
        [&](const auto& id) { if (id == L"AipGallery_lifecycle.disabled") throw std::runtime_error("synthetic failure"); cleared.insert(id); });
    Check(failures == std::vector<std::wstring>{L"AipGallery_lifecycle.disabled"} && cleared.count(L"AipGallery_lifecycle.active") &&
        cleared.count(L"AipGallery_lifecycle.historical") && cleared.count(L"AipSource_rss") && !cleared.count(L"AipGallery_other.active"),
        "one failed clear does not strand other owned queues or touch foreign pins");
    std::vector<BYTE> ledgerAfter, photoAfter;
    Check(pinned == pinsBefore && aip::ReadWholeFileBytes(ledgerPath, ledgerAfter) && ledgerAfter == std::vector<BYTE>(encoded.begin(), encoded.end()) &&
        aip::ReadWholeFileBytes(bitmapPath, photoAfter) && photoBefore == photoAfter,
        "notification cleanup leaves pin membership, ownership history, and source image bytes intact");
    for (const auto state : {TilePinMenuState::Unavailable, TilePinMenuState::Unknown, TilePinMenuState::Pinned, TilePinMenuState::Unpinned}) {
        HMENU menu = CreatePopupMenu(); TrayMenuCommands commands;
        AppendTilePinCommandsForState(menu, state, 61001, 61002, 61003); InspectTrayMenu(menu, commands);
        Check(commands.count(61001) == (state == TilePinMenuState::Unpinned) && commands.count(61002) == (state == TilePinMenuState::Pinned) &&
            commands.count(61003) == (state == TilePinMenuState::Unknown), "native menu exposes exactly the applicable pin operation or honest retry");
        DestroyMenu(menu);
    }
    Check(TilePinState(true, false, false) == TilePinMenuState::Unknown && TilePinState(true, false, true) == TilePinMenuState::Unknown &&
        TilePinState(false, true, true) == TilePinMenuState::Unavailable, "unknown and unavailable inventory never masquerade as unpinned");
    Snapshot first; first.active = true; first.secondaryTileId = L"AipSource_rss"; first.name = L"Selected feed"; first.text.primary = L"Selected text";
    auto second = first; second.secondaryTileId = L"AipSource_smtc"; second.name = L"Other source"; second.text.primary = L"Other text";
    Snapshot gallery; gallery.active = true; gallery.galleryFrame = true; gallery.secondaryTileId = L"AipGallery_lifecycle.active"; gallery.name = L"Photos";
    gallery.backgroundPath = L"C:\\private-cache\\one.png"; gallery.nativeQueueTag = L"g1";
    GalleryStatusSnapshots window; window.frames = {gallery}; window.submitted = true;
    const auto selectedId = first.secondaryTileId;
    const auto pages = BuildTileStatusPagesIncludingGallery({first, second}, {window}, TileStatusLabels{}, L"Submitted queue", &selectedId);
    Check(pages.size() == 1 && pages[0].find(L"Selected text") != std::wstring::npos && pages[0].find(L"Other text") == std::wstring::npos &&
        pages[0].find(L"Photos") == std::wstring::npos, "selected source status contains only that destination instead of the global overview");
    const auto galleryPages = BuildTileStatusPagesIncludingGallery({first, second}, {window}, TileStatusLabels{}, L"Submitted queue", &gallery.secondaryTileId);
    Check(galleryPages.size() == 1 && galleryPages[0].find(L"Submitted queue") != std::wstring::npos && galleryPages[0].find(L"one.png") != std::wstring::npos &&
        galleryPages[0].find(L"Selected text") == std::wstring::npos, "selected gallery status retains its submitted queue without unrelated content");
    const auto all = BuildTileStatusPagesIncludingGallery({first, second}, {window}, TileStatusLabels{}, L"Submitted queue");
    Check(all.size() == 3, "global overview remains an explicit overview of all three destinations");
    std::wstring dynamic = L"[Tiles]\r\nInstances=desktop,news-one,news-two\r\n[Tile.desktop]\r\nType=Wallpaper\r\nPrimary=1\r\n"
        L"[Tile.news-one]\r\nType=RSS\r\nName=First independent feed\r\nBranding=None\r\nTextPreset=RssHeadlines\r\n"
        L"[Tile.news-two]\r\nType=RSS\r\nName=Second independent feed\r\n";
    Check(WriteContentFixture(dynamic), "metadata regression selects only private independent tile instances");
    TileInstance tile; TileInstanceConfiguration instances;
    Check(ReadCurrentTileInstances(instances, error) && instances.tiles.size() == 3 &&
        CanonicalSecondaryTileRuntimeId(L"news-one") == L"AipSource_news-one" && IsKnownSecondaryMetadataId(L"AipSource_news-one") &&
        CanonicalSecondaryTileRuntimeId(L"AipSource_desktop").empty() && !IsKnownSecondaryMetadataId(L"AipSource_rss"),
        "instance metadata accepts configured native IDs and rejects primary or absent legacy IDs");
    Check(FindConfiguredSecondaryTileInstance(L"AipSource_news-one", tile) && TileInstanceArtworkTopic(tile) == L"rss" &&
        tile.appearance.branding == L"None" && tile.presentation.preset == L"RssHeadlines",
        "dynamic native metadata follows its tile type, appearance and independent text policy");
    SourceTileNames names; Check(ReadSourceTileNames(IniReadS, names, error) && names.size() == 2 &&
        names.at(L"AipSource_news-two") == L"Second independent feed", "rename catalog contains independently named secondaries only");
    Check(StageExplicitSourceTileNameIntents(dynamic, {{L"Tile.news-one", L"Name", L"First independent feed"}}, error) && WriteContentFixture(dynamic),
        "explicit dynamic source rename stages durable metadata without pinning");
    SourceTileNameIntents intents;
    Check(ReadCurrentSourceTileNameIntents(intents) && intents.size() == 1 && intents.at(L"AipSource_news-one").name == names.at(L"AipSource_news-one"),
        "dynamic rename intent resolves the configured stable instance ID");
    struct FakeTile {
        std::wstring id = L"AipSource_news-one", name = L"Old Start name", arguments = L"keep arguments", logo = L"keep logo";
        unsigned writes = 0;
        const std::wstring& TileId() const { return id; }
        const std::wstring& DisplayName() const { return name; }
        void DisplayName(const std::wstring& value) { name = value; ++writes; }
    } fake;
    Check(ApplyExplicitSourceTileName(fake, fake.id, names.at(fake.id), &names) && fake.writes == 1 && fake.arguments == L"keep arguments" && fake.logo == L"keep logo" &&
        !ApplyExplicitSourceTileName(fake, L"AipSource_missing", L"Unknown", &names), "dynamic rename mutates only the existing configured tile display name");
    const auto dynamicOwned = OwnedSecondaryTileIdsForClear();
    Check(dynamicOwned.count(L"AipSource_news-one") && dynamicOwned.count(L"AipSource_news-two") && !dynamicOwned.count(L"AipSource_desktop") &&
        !dynamicOwned.count(L"AipSource_rss"), "cleanup ownership follows configured independent instances and retains historical gallery owners");
    const auto savedGlobalClear=g_clearLiveTileOnShutdown.load();
    auto scopedExit=dynamic;
    Check(WriteIniValueToText(scopedExit,L"Tile.desktop",L"Delivery.ClearOnShutdown",L"0")&&
        WriteIniValueToText(scopedExit,L"Tile.news-one",L"Delivery.ClearOnShutdown",L"0")&&
        WriteIniValueToText(scopedExit,L"Tile.news-two",L"Delivery.ClearOnShutdown",L"1")&&WriteContentFixture(scopedExit),
        "exit policy fixture changes only local flags in its private profile");
    g_clearLiveTileOnShutdown=false;
    const auto selectedExit=OwnedSecondaryTileIdsForClear(true);
    Check(!TileInstancePrimaryClearsOnShutdown()&&AnyTileInstanceClearsOnShutdown()&&selectedExit.count(L"AipSource_news-two")&&
        !selectedExit.count(L"AipSource_news-one")&&!selectedExit.count(L"AipGallery_lifecycle.historical"),
        "a local clear preference works independently of the application flag while another tile and gallery history remain untouched");
    const auto manualClear=OwnedSecondaryTileIdsForClear(false);
    Check(manualClear.count(L"AipSource_news-one")&&manualClear.count(L"AipSource_news-two"),
        "an explicit manual clear retains its complete owned-target scope rather than inheriting exit-only preferences");
    g_clearLiveTileOnShutdown=true;
    const auto galleryExit=OwnedSecondaryTileIdsForClear(true);
    Check(galleryExit.count(L"AipGallery_lifecycle.historical")&&!galleryExit.count(L"AipSource_news-one"),
        "gallery's existing global shutdown behavior does not override an ordinary tile's local keep preference");
    g_clearLiveTileOnShutdown=false;
    scopedExit+=L"\r\n[Gallery]\r\nGroupId=lifecycle\r\nTileCount=2\r\n[Gallery.Tile.1]\r\nId=active\r\nName=Keep\r\nDelivery.ClearOnShutdown=0\r\n"
        L"[Gallery.Tile.2]\r\nId=disabled\r\nName=Clear\r\nDelivery.ClearOnShutdown=1\r\n";
    Check(WriteContentFixture(scopedExit),"gallery row exit preferences are staged in the private instance profile");
    const auto perGalleryExit=OwnedSecondaryTileIdsForClear(true);
    Check(perGalleryExit.count(L"AipGallery_lifecycle.disabled")&&!perGalleryExit.count(L"AipGallery_lifecycle.active")&&
        !perGalleryExit.count(L"AipGallery_lifecycle.historical"),"configured gallery rows honor their own exit preferences while removed historical owners retain the global fallback");
    g_clearLiveTileOnShutdown=savedGlobalClear;Check(WriteContentFixture(dynamic),"exit policy fixture restores its previous private document and application flag");
    const auto savedCommand = g_commandLine; const auto savedLine = contentTestCommandLine;
    std::wstring section, key, value;
    Check(TryParseCommandLineOverrideSpec(L"Tile.news-one.RssFeed.FeedUrl=https://example.invalid/feed.atom", section, key, value, error) &&
        section == L"Tile.news-one" && key == L"RssFeed.FeedUrl", "CLI preserves a dotted per-instance provider key under the stable tile section");
    Check(TryParseCommandLineOverrideSpec(L"Gallery.Tile.12.Cache.MaxEntries=7",section,key,value,error)&&
        section==L"Gallery.Tile.12"&&key==L"Cache.MaxEntries"&&value==L"7"&&
        NormalizeDesktopStubTypedSetting(section,key,value,error),
        "CLI preserves and validates a dotted gallery policy under its exact numbered tile section");
    value = L"0"; Check(!NormalizeDesktopStubTypedSetting(L"Tile.news-one", L"RssFeed.UpdateIntervalSeconds", value, error),
        "CLI validates prefixed RSS numbers before any settings transaction");
    const auto noLegacyDependency = dynamic + L"[Content]\r\nEnabled=invalid-old-flag\r\nCount=invalid-old-count\r\n";
    Check(WriteContentFixture(noLegacyDependency), "offline instance fixture keeps deliberately unused invalid legacy content fields");
    contentTestCommandLine = L"DesktopStub.exe --configure-only --set Tile.news-one.RssFeed.UpdateIntervalSeconds=120";
    Check(ParseCommandLineOptions() && RunRenderOnlyCommand() == 0 &&
        IniReadS(L"Tile.news-one", L"RssFeed.UpdateIntervalSeconds", L"") == L"120" &&
        IniReadS(L"Content", L"Enabled", L"") == L"invalid-old-flag",
        "configure-only edits an independent tile without loading or rewriting unused legacy content logic");
    std::wstring beforeRejectedCommand, afterRejectedCommand;
    Check(ReadIniFileForMutation(beforeRejectedCommand), "legacy composition rejection captures the private instance profile");
    g_commandLine = {}; g_commandLine.presetName = L"Wallpaper";
    Check(!ValidateDesktopStubContentProfile(true,error) && error.find(L"Add tile") != std::wstring::npos &&
        !g_commandLine.presetPreviewLoaded && ReadIniFileForMutation(afterRejectedCommand) && afterRejectedCommand == beforeRejectedCommand,
        "independent tiles reject global presets before previewing or changing the INI with actionable add-tile guidance");
    g_commandLine = {}; g_commandLine.nowPlayingImports.push_back(L"synthetic-never-opened.ini");
    Check(!ValidateDesktopStubContentProfile(true,error) && error.find(L"Add tile") != std::wstring::npos &&
        !g_commandLine.presetPreviewLoaded && ReadIniFileForMutation(afterRejectedCommand) && afterRejectedCommand == beforeRejectedCommand,
        "independent tiles reject legacy NowPlaying composition import without touching the saved profile");
    const std::wstring legacyProfile = L"[Content]\r\nEnabled=0\r\nCount=1\r\n";
    Check(WriteContentFixture(legacyProfile), "legacy composition preflight regression uses a sparse private legacy profile");
    g_commandLine = {}; g_commandLine.presetName = L"Wallpaper";
    Check(ValidateDesktopStubContentProfile(true,error) && g_commandLine.presetPreviewLoaded &&
        ReadIniFileForMutation(afterRejectedCommand) && afterRejectedCommand == legacyProfile,
        "legacy profiles can still preview their existing preset without a persistence side effect");
    g_commandLine = {}; g_commandLine.presetName = L"Wallpaper";
    g_commandLine.settings.push_back({L"Tiles",L"Instances",L""});
    Check(!ValidateDesktopStubContentProfile(true,error) && error.find(L"Add tile") != std::wstring::npos && !g_commandLine.presetPreviewLoaded,
        "prospective instance-mode overrides cannot combine a legacy preset with newly independent tiles");
    g_commandLine = savedCommand; contentTestCommandLine = savedLine;
    Check(WriteContentFixture(original), "lifecycle fixture restores the private profile");
    if (ledgerExisted) Check(WriteWholeFileBytes(ledgerPath, oldLedger), "lifecycle fixture restores pre-existing private ownership");
    else Check(DeleteFileW(ledgerPath.c_str()) != FALSE, "lifecycle fixture removes only the ledger it created");
}
