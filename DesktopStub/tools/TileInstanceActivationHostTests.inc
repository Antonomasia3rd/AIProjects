// Private profiles, injected feed data and XML construction only. Never launches
// a URL, settings popup, media action, package operation or native notification.
static void TestTileInstanceActivationHost()
{
    using namespace aip::content;
    const auto originalProfile = g_iniPath;
    const auto profile = originalProfile + L".activation profile.ini";
    const auto oldBranding = g_liveTileBranding.load();
    const auto oldTemplate = g_liveTileTemplateStyle.load();
    const auto oldTarget = g_manifestCompatibilityTarget.load();
    {
        std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = profile;
    }
    const std::wstring fixture = L"[Tiles]\r\nInstances=desktop,rss-0123456789abcdef\r\n"
        L"[Tile.desktop]\r\nType=Wallpaper\r\nPrimary=1\r\nBranding=None\r\nContent.Background=Transparent\r\nContent.Text=Clock title\r\n"
        L"[Tile.rss-0123456789abcdef]\r\nType=RSS\r\nName=Private feed\r\nBranding=Name\r\nRssFeed.FeedUrl=https://rss-fixture.invalid/feed.atom\r\n"
        L"[TileText]\r\nApplyToMediumTile=1\r\n";
    Check(WriteContentFixture(fixture), "activation regression creates only a private profile whose path contains spaces");
    TileInstanceConfiguration cfg; std::wstring error;
    Check(ReadCurrentTileInstances(cfg,error) && cfg.tiles.size() == 2, "activation regression reads both independent instance types");
    if (cfg.tiles.size() == 2) {
        TileInstanceProviderState wallpaper;
        auto primary = ComposeTileInstance(cfg.tiles[0],wallpaper,{},false);
        int count = 0;
        auto args = CommandLineToArgvW(primary.activationArguments.c_str(),&count);
        Check(args && count == 4 && std::wstring(args[0]) == L"--ini" && std::wstring(args[1]) == profile &&
            std::wstring(args[2]) == L"--tile-setup" && std::wstring(args[3]) == L"desktop",
            "primary live activation round-trips the exact quoted custom profile and configured ID");
        if (args) LocalFree(args);
        TileInstanceProviderState rss; rss.type = TileInstanceType::Rss;
        rss.rss = std::make_unique<ContentRssProvider>();
        rss.rss->testReader = [](const RssFeedSettings&) {
            RssFeedSnapshot feed; feed.ok = true; feed.sourceTitle = L"Synthetic feed";
            feed.items.push_back({L"Synthetic headline",L"Synthetic description",L"https://rss-fixture.invalid/article?one=1&two=2",L""});
            return feed;
        };
        auto secondary = ComposeTileInstance(cfg.tiles[1],rss,{},true);
        args = CommandLineToArgvW(secondary.activationArguments.c_str(),&count);
        Check(args && count == 4 && std::wstring(args[0]) == L"--ini" && std::wstring(args[1]) == profile &&
            std::wstring(args[2]) == L"--open-url" && std::wstring(args[3]) == L"https://rss-fixture.invalid/article?one=1&two=2",
            "RSS live activation keeps the quoted custom profile when replacing setup with its featured article");
        if (args) LocalFree(args);
        std::wstring url = L"unchanged";
        Check(ReadTileInstanceSourceActivation(secondary.activationArguments,url) && url == L"https://rss-fixture.invalid/article?one=1&two=2",
            "static RSS pin activation resolves the current same-profile notification URL without launching it");
        {
            std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = originalProfile;
        }
        url = L"unchanged";
        Check(!ReadTileInstanceSourceActivation(secondary.activationArguments,url) && url == L"unchanged",
            "RSS source activation rejects another profile and preserves its caller output");
        {
            std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = profile;
        }
        Check(!ReadTileInstanceSourceActivation(L"--open-url https://rss-fixture.invalid/article",url) &&
            !ReadTileInstanceSourceActivation(TileInstanceProfileActivation(L"--open-url file:///C:/synthetic.txt"),url),
            "unscoped or unsupported source activations cannot reach a URL launch");
        g_liveTileBranding = static_cast<int>(LiveTileBranding::NameAndLogo);
        g_liveTileTemplateStyle = static_cast<int>(LiveTileTemplateStyle::Adaptive);
        g_manifestCompatibilityTarget = static_cast<int>(ManifestCompatibilityTarget::Windows10);
        ManifestDisplayInfo manifest;
        const std::vector<LiveTileUpdateAsset> assets{{L"LiveMediumTile",L"unused.png",L"TileMedium",L"missing-activation-test.png",150,150}};
        const auto xmlFor = [&](const Snapshot& frame) {
            ContentRenderScope scope(&frame);
            return BuildLiveTileXml(ParentDirectoryOfPath(profile),manifest,assets);
        };
        const auto primaryXml = xmlFor(primary), secondaryXml = xmlFor(secondary);
        Check(!primaryXml.empty() && primaryXml.find(L"branding=\"none\"") != std::wstring::npos &&
            primaryXml.find(L"arguments=\"" + aip::XmlEscape(primary.activationArguments) + L"\"") != std::wstring::npos,
            "real primary notification XML honors instance branding and custom-profile activation over global defaults");
        aip::tile_notification::Payload decoded;
        Check(aip::tile_notification::Decode(secondaryXml,decoded,error) && decoded.options.secondaryTileId == L"AipSource_rss-0123456789abcdef" &&
            decoded.xml.find(L"branding=\"name\"") != std::wstring::npos &&
            decoded.xml.find(L"arguments=\"" + aip::XmlEscape(secondary.activationArguments) + L"\"") != std::wstring::npos,
            "real secondary notification transport retains its dynamic ID, branding and quoted custom-profile article arguments");
        primary.secondaryBranding.clear();
        const auto legacyXml = xmlFor(primary);
        Check(!legacyXml.empty() && legacyXml.find(L"branding=\"nameAndLogo\"") != std::wstring::npos,
            "legacy primary snapshots with no instance branding retain the global branding setting");
        rss.Stop();
    }
    {
        std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = originalProfile;
    }
    g_liveTileBranding = oldBranding; g_liveTileTemplateStyle = oldTemplate; g_manifestCompatibilityTarget = oldTarget;
    std::wstring original;
    Check(ReadIniFileForMutation(original), "activation regression restores the original private host profile identity");
    Check(DeleteFileW(profile.c_str()) != FALSE, "activation regression removes only the additional private profile it created");
}
