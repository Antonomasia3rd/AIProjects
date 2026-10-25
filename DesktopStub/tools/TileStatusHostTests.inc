// Cached snapshots, compiled icon bytes and private generated image fixtures
// only; no providers, pins, account files, browser or native tile APIs.
static void TestTileIconUriLifetime()
{
    bool idle = false;
    { std::lock_guard<std::mutex> lock(g_gdiPlusMutex); idle = g_gdiPlusUseCount == 0; }
    Check(idle, "fresh icon URI regression enters with zero GDI+ scope references");
    if (!idle) return;
    const bool savedKeep = g_keepGdiPlusLoaded.exchange(false);
    ReleaseIdleGdiPlusIfAllowed();
    { std::lock_guard<std::mutex> lock(g_gdiPlusMutex);
        Check(g_gdiPlusUseCount == 0 && !g_gdiPlusToken, "fresh icon URI regression starts after GDI+ is fully unloaded"); }
    const auto directory = aip::GetDirectoryName(g_iniPath) + L"\\IconUriLifetime";
    const bool created = CreateDirectoryW(directory.c_str(), nullptr) != FALSE;
    Check(created, "icon URI regression owns a new private output directory");
    if (created) {
        // No outer ScopedGdiPlus: each production helper must acquire its own
        // scope, and destroy its temporary Bitmap before releasing that scope.
        const auto notesUri = NotesTileIconUri(L"notes-genshin-resin_not_full", directory);
        aip::content::Snapshot media;
        media.smtcAppIconPath = aip::GetDirectoryName(g_iniPath) + L"\\image.bmp";
        media.smtcAppIconLease = std::make_shared<int>(0);
        const auto mediaUri = MediaAppIconUri(directory, media);
        Check(!notesUri.empty() && !mediaUri.empty(), "fresh Notes and media app icon URIs are created without an outer GDI+ scope");
        { std::lock_guard<std::mutex> lock(g_gdiPlusMutex);
            Check(g_gdiPlusUseCount == 0 && !g_gdiPlusToken, "icon URI helpers release their GDI+ scopes after destroying temporary images"); }
        const auto pathFromUri = [&](const std::wstring& uri) {
            const std::wstring prefix = L"ms-appx:///";
            return uri.compare(0, prefix.size(), prefix) == 0 ? ResolveManifestAssetPath(directory, uri.substr(prefix.size())) : std::wstring{};
        };
        const auto notesPath = pathFromUri(notesUri), mediaPath = pathFromUri(mediaUri);
        {
            ScopedGdiPlus decode;
            Check(decode.ok(), "icon URI output verification acquires a separate GDI+ scope");
            if (decode.ok()) {
                Gdiplus::Bitmap notes(notesPath.c_str()), app(mediaPath.c_str());
                Check(notes.GetLastStatus() == Gdiplus::Ok && notes.GetWidth() > 0 &&
                    app.GetLastStatus() == Gdiplus::Ok && app.GetWidth() == 1 && app.GetHeight() == 1,
                    "fresh helper outputs are valid PNGs when decoded under a later independent scope");
            }
        }
        if (!notesPath.empty()) DeleteFileW(notesPath.c_str());
        if (!mediaPath.empty()) DeleteFileW(mediaPath.c_str());
        for (const auto* relative : {L"\\Assets\\NotesRows\\v1", L"\\Assets\\NotesRows", L"\\Assets\\MediaIndicators", L"\\Assets"})
            RemoveDirectoryW((directory + relative).c_str());
        Check(RemoveDirectoryW(directory.c_str()) != FALSE, "icon URI regression removes only its private generated files and directories");
    }
    g_keepGdiPlusLoaded = savedKeep;
}
static void TestTileStatusHost()
{
    using namespace aip::content;
    TileStatusLabels labels;
    Snapshot rss; rss.active = true; rss.name = L"Feed tile"; rss.text.primary = L"Wrong raw source summary";
    for (int i = 1; i <= 6; ++i) rss.rssHeadlines.push_back({L"Headline " + std::to_wstring(i), L"Description " + std::to_wstring(i), L""});
    const auto medium = TileSnapshotVisibleText(rss, 150, labels), wide = TileSnapshotVisibleText(rss, 150, labels, 310),
        large = TileSnapshotVisibleText(rss, 310, labels, 310);
    Check(medium.find(L"Headline 1") != std::wstring::npos && medium.find(L"Headline 2") == std::wstring::npos &&
        medium.find(L"Description 1") != std::wstring::npos, "status Medium RSS text uses the same single rich headline and summary as native XML");
    Check(wide.find(L"Headline 2") != std::wstring::npos && wide.find(L"Description 2") != std::wstring::npos &&
        wide.find(L"Headline 3") == std::wstring::npos, "status Wide RSS text uses the same two rich rows as native XML");
    Check(large.find(L"Headline 3") != std::wstring::npos && large.find(L"Description 3") != std::wstring::npos &&
        large.find(L"Headline 4") == std::wstring::npos, "status Large RSS text uses the same three rich rows and descriptions as native XML");
    auto compact = rss; compact.presentation.preset = L"Custom";
    Check(TileSnapshotVisibleText(compact,150,labels).find(L"3. Headline 3") != std::wstring::npos,
        "explicit custom RSS list composition is preserved in status alongside readable defaults");
    Snapshot notes; notes.active = true; notes.name = L"Game tile"; notes.detailTopic = L"resin"; notes.text.primary = L"Wrong scalar resource";
    const wchar_t* topics[] = {L"notes-genshin-resin_not_full", L"notes-genshin-commission", L"notes-genshin-expedition", L"notes-genshin-realm", L"notes-genshin-weekly_boss"};
    for (int i = 0; i < 5; ++i) notes.detailRows.push_back({L"Activity " + std::to_wstring(i), L"Value " + std::to_wstring(i), L"", topics[i]});
    const auto status = BuildTileStatusTextWithLabels({rss, notes}, labels);
    Check(status.find(L"Wrong raw") == std::wstring::npos && status.find(L"Wrong scalar") == std::wstring::npos &&
        status.find(L"\r\n\r\nGame tile\r\n") != std::wstring::npos && status.find(L"Medium / Wide:") != std::wstring::npos &&
        status.find(L"\r\nMedium:\r\n") != std::wstring::npos && status.find(L"\r\nWide:\r\n") != std::wstring::npos &&
        status.find(L"Large:") != std::wstring::npos && status.find(L"Activity 4") != std::wstring::npos,
        "status uses prepared headline/detail rows, labels size-specific limits and separates tile states by a blank line");
    auto hidden = notes; hidden.textMode = TextMode::Off;
    Check(TileSnapshotVisibleText(hidden, 310, labels).find(L"Activity") == std::wstring::npos,
        "hidden tile text is not misrepresented as currently rendered content");
    Snapshot media; media.active = true; media.name = L"Music"; media.smtcSource = true; media.smtcState = MediaPlaybackState::Paused;
    media.smtcShowRepeatShuffle = true; media.smtcRepeat = MediaRepeatMode::Track; media.smtcShuffle = MediaToggleState::Off;
    media.text = {L"Title", L"Artist", MediaStatusCaption(media)};
    const auto playback = BuildTileStatusTextWithLabels({media}, labels);
    Check(playback.find(L"Paused") != std::wstring::npos && playback.find(L"Repeat track") != std::wstring::npos &&
        playback.find(L"Shuffle off") != std::wstring::npos && playback.find(L"Title") != std::wstring::npos,
        "media status includes the same title, artist and actual current playback/repeat/shuffle caption");
    Snapshot image; image.active = true; image.galleryFrame = true; image.name = L"Photos"; image.secondaryTileId = L"AipGallery_test.one";
    image.galleryTicket = 3; image.gallerySlot = 0; image.nativeQueueTag = L"g1"; image.backgroundPath = L"C:\\private-cache\\one.png";
    auto second = image; second.gallerySlot = 1; second.nativeQueueTag = L"g2"; second.backgroundPath = L"C:\\private-cache\\two.png";
    const auto queue = BuildTileStatusTextWithLabels({image, second}, labels);
    Check(queue.find(L"Windows chooses the visible image") != std::wstring::npos && queue.find(L"Slot 2 [g2] two.png") != std::wstring::npos &&
        queue.find(L"private-cache") == std::wstring::npos && queue.find(L"Photos", queue.find(L"Photos") + 1) == std::wstring::npos,
        "gallery status groups cached queue slots without asserting the visible image or disclosing full cache paths");
    labels.noSnapshot = L"Custom empty state"; labels.medium = L"Custom compact size";
    Check(BuildTileStatusTextWithLabels({}, labels) == L"Custom empty state" &&
        BuildTileStatusTextWithLabels({rss}, labels).find(L"Custom compact size") != std::wstring::npos,
        "status wording can be supplied through the same configurable label model");
    Check(!BuildTileStatusText({}).empty(), "host status hook resolves its configured labels without source calls");
    Check(BuildTileStatusPagesWithLabels({}, labels) == std::vector<std::wstring>{labels.noSnapshot},
        "paged status provides one configurable empty page");
    auto inactive = rss; inactive.active = false;
    Check(BuildTileStatusPagesWithLabels({inactive}, labels) == std::vector<std::wstring>{labels.noSnapshot},
        "inactive-only status does not create empty destination pages");
    Check(BuildTileStatusPagesWithLabels({media}, labels) == std::vector<std::wstring>{BuildTileStatusTextWithLabels({media}, labels)},
        "a single destination page preserves the existing exact status formatter output");
    std::vector<Snapshot> many;
    auto primary = media; primary.name = L"Primary"; primary.secondaryTileId.clear();
    primary.text.primary = L"Long complete data: " + std::wstring(12000, L'x') + L" :retained end";
    many.push_back(primary);
    for (int destination = 0; destination < 6; ++destination) {
        auto source = media; source.secondaryTileId = L"AipSource_test" + std::to_wstring(destination);
        source.name = L"Source " + std::to_wstring(destination); source.text.primary = L"Source data " + std::to_wstring(destination);
        many.push_back(source);
    }
    for (int destination = 0; destination < 28; ++destination) for (unsigned slot = 0; slot < 5; ++slot) {
        auto gallery = image; gallery.secondaryTileId = L"AipGallery_pages.tile" + std::to_wstring(destination);
        gallery.name = L"Gallery " + std::to_wstring(destination); gallery.gallerySlot = slot;
        gallery.nativeQueueTag = L"g" + std::to_wstring(slot);
        gallery.backgroundPath = L"C:\\private-cache\\photo-" + std::to_wstring(destination) + L"-" + std::to_wstring(slot) + L".png";
        many.push_back(gallery);
    }
    const auto pages = BuildTileStatusPagesWithLabels(many, labels);
    Check(pages.size() == 35 && pages.front().find(primary.text.primary) != std::wstring::npos,
        "twenty-eight gallery destinations plus sources and primary get individual pages without text truncation");
    bool allData = pages.size() == 35;
    for (int destination = 0; destination < 6 && allData; ++destination)
        allData = pages[1 + destination].find(L"Source data " + std::to_wstring(destination)) != std::wstring::npos;
    for (int destination = 0; destination < 28 && allData; ++destination) {
        const auto& page = pages[7 + destination]; const auto title = L"Gallery " + std::to_wstring(destination) + L"\r\n";
        allData = page.find(title) == 0 && page.find(title, title.size()) == std::wstring::npos;
        for (unsigned slot = 0; slot < 5 && allData; ++slot)
            allData = page.find(L"photo-" + std::to_wstring(destination) + L"-" + std::to_wstring(slot) + L".png") != std::wstring::npos;
    }
    Check(allData, "gallery pages group all five slots once per destination and retain every prepared queue filename and source value");
    Check(BuildTileStatusPages({}).size() == 1, "paged host hook resolves configurable empty wording without source calls");
    TestTileIconUriLifetime();
    ScopedGdiPlus gdiplus;
    Check(gdiplus.ok(), "Notes icon fixture acquires only offscreen GDI+");
    if (gdiplus.ok()) {
        bool allDecoded = true;
        for (const auto& icon : aip::notes::assets::Icons) {
            auto bitmap = LoadNotesTileIcon(icon.topic);
            if (!bitmap || !bitmap->GetWidth() || !bitmap->GetHeight() || bitmap->GetWidth() > 128 || bitmap->GetHeight() > 128) allDecoded = false;
        }
        Check(allDecoded, "all original bundled game icons decode from bounded embedded bytes");
        Check(!LoadNotesTileIcon(L"unknown-topic"), "missing original artwork uses the clean fallback without filesystem probing");
        Gdiplus::Bitmap canvas(310, 310, PixelFormat32bppARGB);
        { Gdiplus::Graphics graphics(&canvas); graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
            Check(RenderDetailRowsSimulation(graphics, notes, 310, 310, 1), "offscreen game row renderer accepts one original icon per row"); }
        bool everyRowHasLeftIcon = true;
        for (std::size_t row = 0; row < notes.detailRows.size(); ++row) {
            const auto geometry = ComputeDetailRowGeometry(310, 310, notes.detailRows.size(), row, true);
            bool painted = false;
            for (int y = static_cast<int>(geometry.iconY); y < static_cast<int>(geometry.iconY + geometry.iconSide); ++y)
                for (int x = 10; x < 34; ++x) { Gdiplus::Color color; if (canvas.GetPixel(x, y, &color) == Gdiplus::Ok && color.GetA()) painted = true; }
            everyRowHasLeftIcon = everyRowHasLeftIcon && painted;
        }
        Check(everyRowHasLeftIcon, "each resource/activity row paints its icon in the left column instead of only the first row's right column");
        auto game=notes;game.gameSource=true;game.presentation={};game.detailRows={{L"Readable commission title that can wrap",L"5/5 complete",L"",L"notes-genshin-resin_full"}};
        const auto coloredIn=[](Gdiplus::Bitmap& bitmap,int first,int last){unsigned count=0;for(int y=0;y<static_cast<int>(bitmap.GetHeight());++y)for(int x=first;x<last;++x){Gdiplus::Color pixel;
                if(bitmap.GetPixel(x,y,&pixel)==Gdiplus::Ok&&pixel.GetA()&&
                    (std::max)({pixel.GetR(),pixel.GetG(),pixel.GetB()})-(std::min)({pixel.GetR(),pixel.GetG(),pixel.GetB()})>12)++count;}return count;};
        // Match ApplyTileTextOverlay's grayscale PNG text configuration. The
        // system default can bake colored ClearType fringes into white text.
        const auto drawGame=[&](Gdiplus::Bitmap& bitmap){Gdiplus::Graphics graphics(&bitmap);graphics.Clear(Gdiplus::Color(0,0,0,0));
            return ConfigureTileTextGraphics(graphics)&&RenderDetailRowsSimulation(graphics,game,150,150,1);};
        Gdiplus::Bitmap gameRight(150,150,PixelFormat32bppARGB);Check(drawGame(gameRight),"readable game rows render offscreen without a global content pointer");
        Check(coloredIn(gameRight,110,140)>0&&coloredIn(gameRight,10,40)==0,"default game row artwork is on the right beside wrapped text");
        game.presentation.iconPlacement=L"Left";Gdiplus::Bitmap gameLeft(150,150,PixelFormat32bppARGB);Check(drawGame(gameLeft)&&coloredIn(gameLeft,10,40)>0,"explicit Left restores the icon column without changing fetched notes");
        game.presentation.iconPlacement=L"None";Gdiplus::Bitmap gameNone(150,150,PixelFormat32bppARGB);Check(drawGame(gameNone)&&coloredIn(gameNone,0,150)==0,"None draws readable notes without row images");
        game.presentation.iconPlacement=L"Right";game.detailRows[0].iconTopic=L"unavailable-wuwa-topic";Gdiplus::Bitmap gameMissing(150,150,PixelFormat32bppARGB);
        Check(drawGame(gameMissing)&&coloredIn(gameMissing,0,150)==0,"missing WuWa artwork uses the text fallback instead of a misleading resource icon");
        const auto gap=GameRowSpacingUri(aip::GetDirectoryName(g_iniPath),150,6);Check(!gap.empty(),"pixel row spacing resolves to a private immutable package asset");
        auto prayer=game;prayer.artworkTopic=L"prayer";prayer.prayerSource=true;prayer.secondaryBranding=L"NameAndLogo";
        Gdiplus::Bitmap prayerCorner(150,150,PixelFormat32bppARGB);{Gdiplus::Graphics graphics(&prayerCorner);graphics.Clear(Gdiplus::Color(0,0,0,0));
            Check(DrawStaticSourceCorner(graphics,prayer,150,150,1,true),"Registration source corner uses the same tight glyph artwork as native branding");}
        bool visible=false,outside=false;for(int y=0;y<150;++y)for(int x=0;x<150;++x){Gdiplus::Color pixel;if(prayerCorner.GetPixel(x,y,&pixel)==Gdiplus::Ok&&pixel.GetA()){
                visible=true;if(x<124||x>=140||y<126||y>=142)outside=true;}}
        Check(visible&&!outside,"Registration corner is small, bottom-right and clear of the text footer");
        Check(StaticSourceCornerTopic(prayer,false).empty(),"native Live Tile and Overlay delivery do not bake a duplicate source corner");
        prayer.secondaryBranding=L"Name";Check(StaticSourceCornerTopic(prayer,true).empty(),"explicit Name continues to omit the corner icon");
        prayer.secondaryBranding=L"Logo";prayer.galleryFrame=true;Check(StaticSourceCornerTopic(prayer,true).empty(),"photo frames retain their entire image without forced branding overlays");
        prayer.galleryFrame=false;prayer.clockSource=true;Check(StaticSourceCornerTopic(prayer,true).empty(),"Clock keeps its separate native branding contract");
    }
}
