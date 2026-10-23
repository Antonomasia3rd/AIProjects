// Pure property selection and injected UI notices. No WinRT/pins/providers.
static std::vector<aip::content::SourceTileNames> sourceRenameTestRequests;
static unsigned sourceRenameTestPosts = 0;
static bool sourceRenameTestPostSucceeds = true;
static bool SourceRenameTestPost(HWND, UINT message) {
    Check(message == WM_APP_SECONDARY_NAMES_CHANGED, "source name notice has its own UI message");
    ++sourceRenameTestPosts; return sourceRenameTestPostSucceeds;
}
static bool SourceRenameTestRequest(HWND, const aip::content::SourceTileNames& names) {
    sourceRenameTestRequests.push_back(names); return true;
}
static void TestSecondaryAppearanceViewCacheHost()
{
    using namespace aip::content;
    std::wstring original; ReadIniFileForMutation(original);
    const auto savedWindow = g_hwnd;
    const auto savedRefresh = g_iniCacheRefreshMs.load();
    const auto savedWait = g_iniWriteLockWaitMs.load();
    std::wstring profile; { std::lock_guard<std::mutex> lock(g_pathMutex); profile = g_iniPath; }
    g_hwnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(1));
    std::wstring text = L"[Content]\r\nEnabled=0\r\nCount=1\r\n[Tile.rss]\r\nName=Cache baseline\r\n";
    Check(WriteContentFixture(text), "appearance view cache starts from a private sparse profile");
    std::wstring fingerprint; SourceTileNames names; SourceTileNameIntents intents;
    Check(CurrentSecondaryAppearanceFingerprint(fingerprint, &names, &intents), "appearance view caches one validated immutable snapshot");
    const auto parsed = g_testSecondaryAppearanceParses.load();
    g_sourceTileNameNotice.Clear(); g_secondaryAppearanceNotice.RecordScheduled(fingerprint);
    for (int tick = 0; tick < 12; ++tick) PollSecondaryTileAppearance();
    Check(g_testSecondaryAppearanceParses == parsed,
        "repeated unchanged appearance polls reuse fingerprint, names and intents without copying/parsing the full INI");
    const auto baseline = fingerprint;
    aip::WriteIniValueToText(text, L"Tile.rss", L"Name", L"Cache renamed");
    Check(WriteContentFixture(text) && CurrentSecondaryAppearanceFingerprint(fingerprint, &names, &intents) && fingerprint == baseline &&
        names.at(L"AipSource_rss") == L"Cache renamed" && g_testSecondaryAppearanceParses == parsed + 1,
        "Name revision changes invalidate cached names while preserving the appearance-only fingerprint");
    aip::WriteIniValueToText(text, L"Tile.rss", L"Branding", L"None");
    Check(WriteContentFixture(text) && CurrentSecondaryAppearanceFingerprint(fingerprint, &names, &intents) && fingerprint != baseline &&
        g_testSecondaryAppearanceParses == parsed + 2, "appearance revision changes rebuild exactly one derived view");
    const auto renamedAppearance = fingerprint;
    std::wstring error;
    Check(StageExplicitSourceTileNameIntents(text, {{L"Tile.rss", L"Name", L"Cache renamed"}}, error) && WriteContentFixture(text) &&
        CurrentSecondaryAppearanceFingerprint(fingerprint, &names, &intents) && intents.size() == 1 && fingerprint == renamedAppearance &&
        g_testSecondaryAppearanceParses == parsed + 3, "intent-only revision changes publish new durable intent metadata without changing the appearance key");
    aip::WriteIniValueToText(text, L"Content.1", L"Text", L"Unrelated text revision");
    Check(WriteContentFixture(text) && CurrentSecondaryAppearanceFingerprint(fingerprint, &names, &intents) &&
        g_testSecondaryAppearanceParses == parsed + 4, "any new INI revision revalidates rather than trusting an unchanged derived fingerprint");
    const auto valid = text;
    aip::WriteIniValueToText(text, L"Tile.rss", L"Background", L"Wallpaper");
    Check(WriteContentFixture(text), "invalid appearance revision uses only the private fixture");
    std::wstring refused = L"retain caller value";
    Check(!CurrentSecondaryAppearanceFingerprint(refused, &names, &intents) && refused == L"retain caller value",
        "invalid appearance refuses publication without replacing the caller's previous value");
    const auto invalidParsed = g_testSecondaryAppearanceParses.load();
    Check(!CurrentSecondaryAppearanceFingerprint(refused, &names, &intents) && g_testSecondaryAppearanceParses == invalidParsed,
        "unchanged invalid revisions reuse their refusal without repeated validation work");
    Check(WriteContentFixture(valid) && CurrentSecondaryAppearanceFingerprint(fingerprint, &names, &intents),
        "a new valid revision recovers from a cached configuration refusal");
    const auto beforeFailure = g_testSecondaryAppearanceParses.load();
    std::atomic<bool> ready{false}, locked{false}, release{false};
    std::thread holder([&] {
        aip::IniWriteMutexGuard scope(profile, 500); locked = scope.IsLocked(); ready = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!release && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!ready && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(ready && locked, "appearance cache failure fixture owns only the private profile's named lock");
    g_iniWriteLockWaitMs = 0;
    Check(!CurrentSecondaryAppearanceFingerprint(refused, &names, &intents) && refused == L"retain caller value" &&
        g_testSecondaryAppearanceParses == beforeFailure, "a warm derived cache cannot bypass a failed named snapshot lock");
    g_iniWriteLockWaitMs = savedWait; release = true; holder.join();
    HANDLE denied = CreateFileW(profile.c_str(), GENERIC_READ | FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(denied != INVALID_HANDLE_VALUE, "refresh failure fixture exclusively owns its existing private INI");
    if (denied != INVALID_HANDLE_VALUE) {
        std::vector<BYTE> blockedBytes;
        const bool dataReadBlocked = !ReadWholeFileBytes(profile, blockedBytes);
        const auto blockedError = GetLastError();
        Check(dataReadBlocked && blockedError == ERROR_SHARING_VIOLATION,
            "refresh failure fixture proves the production data-read API is denied before asserting cache refusal");
        FILETIME modified{};
        Check(GetFileTime(denied, nullptr, nullptr, &modified) != FALSE, "refresh failure fixture reads its own file timestamp");
        ULARGE_INTEGER stamp{}; stamp.LowPart = modified.dwLowDateTime; stamp.HighPart = modified.dwHighDateTime; stamp.QuadPart += 100000;
        modified.dwLowDateTime = stamp.LowPart; modified.dwHighDateTime = stamp.HighPart;
        Check(SetFileTime(denied, nullptr, nullptr, &modified) != FALSE, "refresh failure fixture changes only its own timestamp to require a normal refresh");
        // Force the cache's stale timestamp comparison deterministically even
        // on filesystems that defer publishing write times until handle close.
        { std::lock_guard<std::mutex> lock(g_iniCacheMutex); g_iniCacheWriteTime = {}; }
        g_iniCacheRefreshMs = 0;
        Check(!CurrentSecondaryAppearanceFingerprint(refused, &names, &intents) && refused == L"retain caller value" &&
            g_testSecondaryAppearanceParses == beforeFailure, "same-profile refresh I/O failure cannot reuse an already validated derived view");
        g_iniCacheRefreshMs = 60000;
        std::vector<IniEntry> blocked; bool blockedReady = true;
        Check(!ReadIniSectionCached(L"AbsentAppearanceFixtureSection", blocked, &blockedReady) && !blockedReady,
            "a failed refresh cannot become ready from stale bytes on the next call inside the configured TTL");
        CloseHandle(denied);
    }
    g_iniCacheRefreshMs = 60000;
    { std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = aip::GetDirectoryName(profile) + L"\\missing-appearance-profile\\missing.ini"; }
    Check(!CurrentSecondaryAppearanceFingerprint(refused, &names, &intents) && refused == L"retain caller value" &&
        g_testSecondaryAppearanceParses == beforeFailure, "missing profile refresh refuses an old cached view even inside the configured TTL");
    { std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = profile; }
    std::vector<IniEntry> absent; bool cacheReady = false;
    Check(!ReadIniSectionCached(L"AbsentAppearanceFixtureSection", absent, &cacheReady) && cacheReady,
        "a valid sparse profile with an absent section remains cache-ready, distinct from refresh failure");
    g_iniCacheRefreshMs = savedRefresh; g_hwnd = savedWindow; g_sourceTileNameNotice.Clear();
    Check(WriteContentFixture(original), "appearance view cache fixture restores the prior private profile");
    if (CurrentSecondaryAppearanceFingerprint(fingerprint)) g_secondaryAppearanceNotice.RecordScheduled(fingerprint);
}
static void TestSourceTileRenameHost()
{
    TestSecondaryAppearanceViewCacheHost();
    using namespace aip::content;
    struct FakeTile {
        std::wstring id = L"AipSource_rss", name = L"My Start name", arguments = L"saved activation", logos = L"saved logos";
        unsigned position = 7, nameWrites = 0;
        const std::wstring& TileId() const { return id; }
        const std::wstring& DisplayName() const { return name; }
        void DisplayName(const std::wstring& value) { name = value; ++nameWrites; }
    } tile;
    Check(ApplyExplicitSourceTileName(tile, L"AipSource_rss", L"Feed <name>") && tile.name == L"Feed <name>" && tile.nameWrites == 1 &&
        tile.id == L"AipSource_rss" && tile.arguments == L"saved activation" && tile.logos == L"saved logos" && tile.position == 7,
        "explicit rename changes only the existing tile's display name and preserves ID, activation, logos and Start position");
    Check(!ApplyExplicitSourceTileName(tile, L"AipSource_rss", tile.name) && tile.nameWrites == 1,
        "an already-matching native display name requires no update");
    Check(!ApplyExplicitSourceTileName(tile, L"AipSource_hsr", L"Wrong target") && !ApplyExplicitSourceTileName(tile, L"AipAction_play", L"Wrong kind") &&
        !ApplyExplicitSourceTileName(tile, L"AipSource_rss", L" ") && tile.nameWrites == 1,
        "name-only mutation rejects another target, actions and invalid names");
    SourceTileNameNotice notice;
    SourceTileNames names{{L"AipSource_rss", L"Configured feed"}, {L"AipSource_hsr", L"Star Rail"}};
    Check(!notice.Observe(names) && notice.Consume().empty(), "startup names seed a baseline without overwriting native user names");
    names[L"AipSource_rss"] = L"First rename";
    Check(notice.Observe(names), "an explicit raw INI Name delta posts a notice");
    names[L"AipSource_rss"] = L"Latest rename";
    Check(!notice.Observe(names) && notice.Consume() == SourceTileNames{{L"AipSource_rss", L"Latest rename"}},
        "multiple pending Name edits coalesce to the latest name for each stable target");
    Check(!notice.Observe(names), "unchanged names never repeat native rename work");
    Check(notice.Explicit({{L"AipSource_rss", L"Latest rename"}}), "an explicit same-value rename can retry a prior Windows failure");
    notice.PostFailed();
    Check(notice.Observe(names) && notice.Consume().size() == 1, "failed UI message posting retries without dropping explicit intent");
    notice.Clear();
    Check(!notice.Observe(names) && notice.Consume().empty(), "a fresh resident baseline never treats old profile names as an explicit startup rename");

    std::wstring original, issue; ReadIniFileForMutation(original);
    const auto savedWindow = g_hwnd;
    const auto savedPost = g_testSourceRenamePost; const auto savedRequest = g_testSourceRenameRequest;
    g_hwnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(1)); // Injected only, never a native window.
    g_testSourceRenamePost = SourceRenameTestPost; g_testSourceRenameRequest = SourceRenameTestRequest;
    sourceRenameTestRequests.clear(); sourceRenameTestPosts = 0; sourceRenameTestPostSucceeds = true; g_sourceTileNameNotice.Clear();
    std::wstring text = L"[Content]\r\nEnabled=0\r\nCount=1\r\n[Tile.rss]\r\nName=Feed baseline\r\n";
    Check(WriteContentFixture(text), "rename fixture writes only its private configuration");
    std::wstring fingerprint;
    Check(CurrentSecondaryAppearanceFingerprint(fingerprint), "rename baseline has valid existing appearance metadata");
    g_secondaryAppearanceNotice.RecordScheduled(fingerprint);
    PollSecondaryTileAppearance();
    Check(sourceRenameTestPosts == 0 && sourceRenameTestRequests.empty(), "normal startup neither posts nor executes a source rename");
    aip::WriteIniValueToText(text, L"Content.1", L"RssUrl", L"https://example.invalid/feed");
    aip::WriteIniValueToText(text, L"Tile.rss", L"Branding", L"None");
    Check(WriteContentFixture(text), "feed and appearance edits stay local to the synthetic profile");
    PollSecondaryTileAppearance();
    Check(sourceRenameTestPosts == 0 && sourceRenameTestRequests.empty(), "feed URL changes and appearance migration cannot overwrite native display names");
    aip::WriteIniValueToText(text, L"Tile.rss", L"Name", L"Changed INI feed");
    Check(WriteContentFixture(text), "raw INI source Name edit publishes to the fixture");
    PollSecondaryTileAppearance(); PollSecondaryTileAppearance();
    Check(sourceRenameTestPosts == 1 && sourceRenameTestRequests.empty(), "polling coalesces Name changes and never invokes a native mutation itself");
    Check(SecondaryTileHandleMessage(g_hwnd, WM_APP_SECONDARY_NAMES_CHANGED, 0, 0) && sourceRenameTestRequests.size() == 1 &&
        sourceRenameTestRequests.back() == SourceTileNames{{L"AipSource_rss", L"Changed INI feed"}},
        "UI consumption requests a name-only update solely for the explicitly changed source");
    Check(SaveSecondaryTileMenuSettings(g_hwnd, {{L"Tile.rss", L"Name", L"Tray feed"}}), "tray source Name batch saves successfully");
    SecondaryTileHandleMessage(g_hwnd, WM_APP_SECONDARY_NAMES_CHANGED, 0, 0);
    Check(sourceRenameTestRequests.size() == 2 && sourceRenameTestRequests.back().at(L"AipSource_rss") == L"Tray feed",
        "successful tray rename submits the saved configured name to the same explicit pathway");
    const auto postsAfterTray = sourceRenameTestPosts;
    PollSecondaryTileAppearance();
    Check(sourceRenameTestPosts == postsAfterTray,
        "polling after an explicit tray save does not rediscover its handled Name as a second raw INI edit");
    const auto requests = sourceRenameTestRequests.size();
    Check(!SaveSecondaryTileMenuSettings(g_hwnd, {{L"Tile.rss", L"Name", L"Rejected"}, {L"Tile.rss", L"Content", L"0"}}) &&
        IniReadS(L"Tile.rss", L"Name", L"") == L"Tray feed", "invalid rename batches leave the saved name unchanged");
    SecondaryTileHandleMessage(g_hwnd, WM_APP_SECONDARY_NAMES_CHANGED, 0, 0);
    Check(sourceRenameTestRequests.size() == requests, "a rejected atomic batch does not submit a native rename");
    sourceRenameTestPostSucceeds = false;
    Check(SaveSecondaryTileMenuSettings(g_hwnd, {{L"Tile.hsr", L"Name", L"Retry name"}}), "post-failure fixture saves its valid name");
    sourceRenameTestPostSucceeds = true; PollSecondaryTileAppearance();
    SecondaryTileHandleMessage(g_hwnd, WM_APP_SECONDARY_NAMES_CHANGED, 0, 0);
    Check(!sourceRenameTestRequests.empty() && sourceRenameTestRequests.back() == SourceTileNames{{L"AipSource_hsr", L"Retry name"}}, "failed posting retains the saved name until a later successful UI handoff");
    SourceTileNameIntents pending;
    Check(ReadCurrentSourceTileNameIntents(pending) && pending.count(L"AipSource_rss") && pending.count(L"AipSource_hsr"),
        "explicit source names persist as bounded revisioned intent until native completion");
    const auto old = pending.find(L"AipSource_rss");
    if (old != pending.end()) {
        const auto oldIntent = old->second;
        Check(SaveSecondaryTileMenuSettings(g_hwnd, {{L"Tile.rss", L"Name", L"Newest feed"}}), "new explicit name supersedes an older pending native operation");
        Check(ClearSourceTileNameIntent(L"AipSource_rss", oldIntent.revision, oldIntent.name) &&
            ReadCurrentSourceTileNameIntents(pending) && pending.count(L"AipSource_rss") && pending.at(L"AipSource_rss").name == L"Newest feed" &&
            pending.at(L"AipSource_rss").revision > oldIntent.revision,
            "late old native success cannot clear a newer revision/value intent");
        const auto current = pending.find(L"AipSource_rss");
        if (current != pending.end()) Check(ClearSourceTileNameIntent(current->first, current->second.revision, current->second.name) &&
            ReadCurrentSourceTileNameIntents(pending) && !pending.count(L"AipSource_rss") && pending.count(L"AipSource_hsr"),
            "matching completion clears only its own intent and preserves other source changes");
    }
    const auto savedCommandLine = g_commandLine;
    auto offline = BuildLegacyContentFixtureTemplate();
    aip::WriteIniValueToText(offline, L"Content", L"Enabled", L"0");
    Check(WriteContentFixture(offline), "offline rename uses a complete private template without sparse-default work");
    g_commandLine = {}; g_commandLine.configureOnly = true;
    g_commandLine.settings = {{L"Tile.rss", L"Name", L"Offline explicit feed"}};
    const auto beforeOffline = sourceRenameTestRequests.size();
    Check(RunRenderOnlyCommand() == 0 && ReadCurrentSourceTileNameIntents(pending) && pending.size() == 1 &&
        pending.at(L"AipSource_rss").name == L"Offline explicit feed" && sourceRenameTestRequests.size() == beforeOffline,
        "configure-only source Name edit atomically saves intent without invoking resident/native tile work");
    g_commandLine = savedCommandLine;
    g_sourceTileNameNotice.Clear(); sourceRenameTestPosts = 0;
    PollSecondaryTileAppearance();
    Check(sourceRenameTestPosts == 1, "a later normal startup honors explicit saved intent without treating baseline names as renames");
    SecondaryTileHandleMessage(g_hwnd, WM_APP_SECONDARY_NAMES_CHANGED, 0, 0);
    Check(sourceRenameTestRequests.size() == beforeOffline + 1 && sourceRenameTestRequests.back() ==
        SourceTileNames{{L"AipSource_rss", L"Offline explicit feed"}}, "saved explicit intent reaches the normal UI rename pathway after restart");
    PollSecondaryTileAppearance();
    Check(sourceRenameTestPosts == 1, "retained unsuccessful intent does not cause per-poll native retries");
    std::wstring corrupt = offline;
    aip::WriteIniValueToText(corrupt, L"State.SourceTileNames", L"Sequence", L"not-a-revision");
    const auto unchanged = corrupt;
    Check(!StageExplicitSourceTileNameIntents(corrupt, {{L"Tile.rss", L"Name", L"Uncommitted"}}, issue) && corrupt == unchanged,
        "malformed intent metadata rejects staging without changing candidate bytes");
    g_sourceTileNameNotice.Clear(); g_testSourceRenamePost = savedPost; g_testSourceRenameRequest = savedRequest; g_hwnd = savedWindow;
    Check(WriteContentFixture(original), "rename fixture restores the prior private INI");
    if (CurrentSecondaryAppearanceFingerprint(fingerprint)) g_secondaryAppearanceNotice.RecordScheduled(fingerprint);
}
