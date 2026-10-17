// Real configuration/cache readers, private INIs and in-memory pin inventory.
// No source worker, account, hardware, package, pinning or resident is started.
static void TestContentCpuCacheHost(const std::wstring& directory)
{
    std::wstring original; Check(ReadIniFileForMutation(original), "content CPU cache fixture owns its existing private profile");
    std::wstring profile;
    { std::lock_guard<std::mutex> lock(g_pathMutex); profile = g_iniPath; }
    const auto oldPins = CachedSecondaryTileIds(); const bool oldKnown = SecondaryTileInventoryReady();
    const auto oldRefresh = g_iniCacheRefreshMs.load(); g_iniCacheRefreshMs = 0; g_secondaryPinInventory.Reset();
    const std::wstring initial = L"[Content]\nEnabled=0\nCount=1\n[Content.1]\nEnabled=1\nBackground=Transparent\nTextSources=CustomText\nText=CPU_BASE_A\n"
        L"[Tiles]\nEnabled=0\n[Tile.smtc]\nEnabled=0\nContent=2\n";
    Check(WriteContentFixture(initial), "write synthetic cache base without enabling providers");
    const auto beforeBase = contentBaseConfigurationParses, beforeSecondary = contentSecondaryConfigurationParses;
    aip::content::Configuration cfg; aip::content::SecondaryTileConfiguration tiles; std::wstring error;
    Check(ReadContentConfiguration(cfg, error) && cfg.entries[0].custom.primary == L"CPU_BASE_A", "initial base read parses real content and ASUS defaults once");
    bool unchanged = true;
    for (unsigned tick = 0; tick < 80; ++tick) unchanged = ReadContentConfiguration(cfg, error) && unchanged;
    Check(unchanged && contentBaseConfigurationParses == beforeBase + 1 && contentSecondaryConfigurationParses == beforeSecondary,
        "eighty unchanged base-only reads reuse one immutable parse and never parse secondary settings");
    for (unsigned tick = 0; tick < 40; ++tick) unchanged = ReadContentConfiguration(cfg, error, &tiles) && unchanged;
    Check(unchanged && contentBaseConfigurationParses == beforeBase + 1 && contentSecondaryConfigurationParses == beforeSecondary + 1,
        "secondary settings parse lazily once for the requested content-count validation variant");
    cfg.entries[0].custom.primary = L"caller mutation";
    Check(ReadContentConfiguration(cfg, error) && cfg.entries[0].custom.primary == L"CPU_BASE_A", "caller changes cannot mutate the cached base");

    g_secondaryPinInventory.Complete(true, {L"AipSource_smtc"}, GetTickCount64());
    Check(ReadContentConfiguration(cfg, error, &tiles) && cfg.enabled && cfg.entries.size() == 2 && tiles.tiles[0].enabled,
        "pin mutation immediately selects a missing source from a copied base without an INI edit");
    const auto pinnedVariantParses = contentSecondaryConfigurationParses;
    g_secondaryPinInventory.Mutation(L"AipSource_smtc", false);
    Check(ReadContentConfiguration(cfg, error, &tiles) && !cfg.enabled && cfg.entries.size() == 1 && !tiles.tiles[0].enabled &&
        contentBaseConfigurationParses == beforeBase + 1 && contentSecondaryConfigurationParses == pinnedVariantParses,
        "unpinning immediately removes demand and cannot leave pin-derived rows in either cache");
    Check(ReadContentConfiguration(cfg, error) && !cfg.enabled && cfg.entries.size() == 1,
        "base-only reads remain independent of pin-derived configuration");

    const std::wstring rss = L"[Settings]\nContentSource=RssFeed\n[Content]\nEnabled=0\nCount=1\n[RssFeed]\nMaxItems=3\n";
    Check(WriteContentFixture(rss) && ReadContentConfiguration(cfg, error) && cfg.enabled && cfg.cycle && cfg.entries.size() == 3,
        "legacy RSS migration is captured inside the cached base");
    const auto rssParses = contentBaseConfigurationParses;
    Check(ReadContentConfiguration(cfg, error) && cfg.entries.size() == 3 && contentBaseConfigurationParses == rssParses,
        "unchanged legacy RSS profiles retain migrated behavior without repeated parsing");

    const auto badSecondary = initial + L"[Tile.rss]\nContent=invalid\n";
    Check(WriteContentFixture(badSecondary) && ReadContentConfiguration(cfg, error), "invalid secondary settings remain irrelevant to base-only readers");
    const auto beforeBadSecondary = contentSecondaryConfigurationParses;
    const auto retained = cfg.entries[0].custom.primary;
    Check(!ReadContentConfiguration(cfg, error, &tiles) && !error.empty() && cfg.entries[0].custom.primary == retained,
        "requesting invalid secondary settings rejects without publishing partial output");
    Check(!ReadContentConfiguration(cfg, error, &tiles) && contentSecondaryConfigurationParses == beforeBadSecondary + 1,
        "an unchanged invalid secondary variant remains rejected without parsing on every tick");
    Check(ReadContentConfiguration(cfg, error) && error.empty(), "a cached secondary error cannot invalidate a subsequent base-only read");
    auto invalid = initial; Check(WriteIniValueToText(invalid, L"Content", L"Count", L"0") && WriteContentFixture(invalid), "write only a private invalid base edit");
    const auto beforeInvalid = contentBaseConfigurationParses;
    Check(!ReadContentConfiguration(cfg, error) && !error.empty() && cfg.entries[0].custom.primary == retained,
        "changed invalid base bytes reject instead of reusing the previous valid view");
    Check(!ReadContentConfiguration(cfg, error) && contentBaseConfigurationParses == beforeInvalid + 1,
        "unchanged invalid base bytes remain rejected through the cached error");

    const auto secondProfile = directory + L"\\content-cpu-profile.ini";
    auto other = initial; Check(WriteIniValueToText(other, L"Content.1", L"Text", L"CPU_BASE_B") && WriteUtf8BomTextFile(secondProfile, other), "create one named owned alternate profile");
    Check(WriteContentFixture(initial) && ReadContentConfiguration(cfg, error), "restore a valid base before profile identity checks");
    { std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = secondProfile; }
    Check(ReadContentConfiguration(cfg, error) && cfg.entries[0].custom.primary == L"CPU_BASE_B", "profile identity forces the current document and typed view to change");
    { std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = profile; }
    Check(ReadContentConfiguration(cfg, error) && cfg.entries[0].custom.primary == L"CPU_BASE_A", "returning to the first profile cannot reuse another profile's document");
    auto switched = initial; Check(WriteIniValueToText(switched, L"Content.1", L"Text", L"CPU_GUARD_A") && WriteContentFixture(switched), "force a new parse for the profile-race guard");
    contentCacheSwitchProfileOnParse = secondProfile;
    Check(!ReadContentConfiguration(cfg, error) && cfg.entries[0].custom.primary == L"CPU_BASE_A" && !error.empty(),
        "a profile changed during parsing rejects the old snapshot before changing caller output");
    { std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = profile; }
    contentCacheSwitchProfileOnParse.clear();

    const auto missing = directory + L"\\content-cpu-missing.ini";
    { std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = missing; }
    Check(!ReadContentConfiguration(cfg, error) && cfg.entries[0].custom.primary == L"CPU_BASE_A", "failed document refresh cannot authorize a cached previous profile");
    { std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = profile; }
    Check(WriteContentFixture(original), "restore the private profile after cache tests");
    DeleteFileW(secondProfile.c_str());
    g_secondaryPinInventory.Reset();
    if (oldKnown) g_secondaryPinInventory.Complete(true, oldPins, GetTickCount64());
    else for (const auto& id : oldPins) g_secondaryPinInventory.Mutation(id, true);
    g_iniCacheRefreshMs = oldRefresh;
}
